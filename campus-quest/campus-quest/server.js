/*
 * Campus Quest server - Node.js 22.13 or newer.
 *   Start:   node server.js            (then open http://localhost:4000)
 *   Players: http://<this-computer-ip>:4000        Admin portal: http://<this-computer-ip>:4000/admin
 * Data is kept in data/campus-quest.sqlite. Admin password: config.json ("adminPassword") or ADMIN_PASSWORD.
 */
'use strict';
const http = require('http');
const fs = require('fs');
const path = require('path');
const os = require('os');
const crypto = require('crypto');
const { promisify } = require('util');
const { DatabaseSync } = require('node:sqlite');

const PORT = parseInt(process.env.PORT || '4000', 10);
const MAX_GAME_SECONDS = 20 * 60;
const CORRECT_SNIPPETS = new Set([0, 1, 2, 3, 4, 5, 6, 7, 8, 9]);
const ROOT = __dirname;
const PUBLIC = path.join(ROOT, 'public');
const DATA_DIR = process.env.DATA_DIR || path.join(ROOT, 'data');
const DB_FILE = path.join(DATA_DIR, 'campus-quest.sqlite');
const LEGACY_DB_FILE = path.join(DATA_DIR, 'db.json');

const configPath = path.join(ROOT, 'config.json');
const config = fs.existsSync(configPath) ? JSON.parse(fs.readFileSync(configPath, 'utf8')) : {};
const ADMIN_PASSWORD = String(process.env.ADMIN_PASSWORD || config.adminPassword || '');
if (!ADMIN_PASSWORD || ADMIN_PASSWORD === 'admin@123') {
  console.error('Set a unique admin password in config.json or ADMIN_PASSWORD before starting the server.');
  process.exit(1);
}

/* ---------------- SQLite storage ---------------- */
fs.mkdirSync(DATA_DIR, { recursive: true });
const sql = new DatabaseSync(DB_FILE);
sql.exec('PRAGMA journal_mode = WAL; PRAGMA foreign_keys = ON; PRAGMA busy_timeout = 5000;');
sql.exec(`CREATE TABLE IF NOT EXISTS accounts (
  id TEXT PRIMARY KEY, email TEXT NOT NULL UNIQUE COLLATE NOCASE,
  salt TEXT NOT NULL, password_hash TEXT NOT NULL, created_at INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS participants (
  id TEXT PRIMARY KEY, account_id TEXT REFERENCES accounts(id), name TEXT NOT NULL,
  roll_no TEXT NOT NULL, team TEXT NOT NULL, registered_at INTEGER NOT NULL,
  started_at INTEGER, finished_at INTEGER, score INTEGER NOT NULL DEFAULT 0,
  right_count INTEGER NOT NULL DEFAULT 0, wrong_count INTEGER NOT NULL DEFAULT 0,
  seconds INTEGER NOT NULL DEFAULT 0, session_key TEXT, heartbeat_at INTEGER,
  finish_reason TEXT, snippet_tokens TEXT, retry_count INTEGER NOT NULL DEFAULT 0
);
CREATE TABLE IF NOT EXISTS auth_sessions (
  token_hash TEXT PRIMARY KEY, account_id TEXT REFERENCES accounts(id) ON DELETE CASCADE,
  expires_at INTEGER NOT NULL, kind TEXT NOT NULL CHECK(kind IN ('admin', 'player'))
);
CREATE INDEX IF NOT EXISTS participant_team ON participants(team COLLATE NOCASE);`);
// Migrate databases created by older Campus Quest builds.
for (const [column, type] of [['session_key', 'TEXT'], ['heartbeat_at', 'INTEGER'], ['finish_reason', 'TEXT'], ['snippet_tokens', 'TEXT'], ['retry_count', 'INTEGER NOT NULL DEFAULT 0']]) {
  try { sql.exec(`ALTER TABLE participants ADD COLUMN ${column} ${type}`); } catch (e) {
    if (!String(e.message).includes('duplicate column name')) throw e;
  }
}
sql.exec("UPDATE participants SET finish_reason='completed' WHERE finished_at IS NOT NULL AND finish_reason IS NULL");
const accountRows = sql.prepare('SELECT * FROM accounts').all();
const participantRows = sql.prepare('SELECT * FROM participants').all();
let db = {
  accounts: accountRows.map((a) => ({ id: a.id, email: a.email, salt: a.salt, passwordHash: a.password_hash, createdAt: a.created_at })),
  participants: participantRows.map((p) => ({ id: p.id, accountId: p.account_id, name: p.name, rollNo: p.roll_no, team: p.team,
    registeredAt: p.registered_at, startedAt: p.started_at, finishedAt: p.finished_at, score: p.score, right: p.right_count, wrong: p.wrong_count, seconds: p.seconds,
    sessionKey: p.session_key || null, heartbeatAt: p.heartbeat_at || null, finishReason: p.finish_reason || null,
    snippetTokens: p.snippet_tokens ? JSON.parse(p.snippet_tokens) : null, retryCount: p.retry_count || 0 }))
};
if (db.participants.length === 0 && db.accounts.length === 0 && fs.existsSync(LEGACY_DB_FILE)) {
  try {
    const legacy = JSON.parse(fs.readFileSync(LEGACY_DB_FILE, 'utf8'));
    if (Array.isArray(legacy.accounts)) db.accounts = legacy.accounts;
    if (Array.isArray(legacy.participants)) db.participants = legacy.participants.map((p) => ({
      ...p, finishReason: p.finishReason || (p.finishedAt ? 'completed' : null), retryCount: p.retryCount || 0,
      snippetTokens: p.snippetTokens || null
    }));
    save();
  } catch (e) { console.error('Could not import legacy data/db.json:', e.message); }
}
function save() {
  try {
    sql.exec('BEGIN IMMEDIATE');
    const insertAccount = sql.prepare(`INSERT INTO accounts(id,email,salt,password_hash,created_at) VALUES(?,?,?,?,?)
      ON CONFLICT(id) DO UPDATE SET email=excluded.email,salt=excluded.salt,password_hash=excluded.password_hash,created_at=excluded.created_at`);
    const insertParticipant = sql.prepare(`INSERT INTO participants(id,account_id,name,roll_no,team,registered_at,started_at,finished_at,score,right_count,wrong_count,seconds,session_key,heartbeat_at,finish_reason,snippet_tokens,retry_count) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)
      ON CONFLICT(id) DO UPDATE SET account_id=excluded.account_id,name=excluded.name,roll_no=excluded.roll_no,team=excluded.team,
      registered_at=excluded.registered_at,started_at=excluded.started_at,finished_at=excluded.finished_at,score=excluded.score,
      right_count=excluded.right_count,wrong_count=excluded.wrong_count,seconds=excluded.seconds,session_key=excluded.session_key,heartbeat_at=excluded.heartbeat_at,
      finish_reason=excluded.finish_reason,snippet_tokens=excluded.snippet_tokens,retry_count=excluded.retry_count`);
    const accountIds = new Set(db.accounts.map((a) => a.id));
    const participantIds = new Set(db.participants.map((p) => p.id));
    const oldAccounts = sql.prepare('SELECT id FROM accounts').all();
    const oldParticipants = sql.prepare('SELECT id FROM participants').all();
    for (const a of db.accounts) insertAccount.run(a.id, a.email, a.salt, a.passwordHash, a.createdAt);
    for (const p of db.participants) insertParticipant.run(p.id, accountIds.has(p.accountId) ? p.accountId : null, p.name, p.rollNo, p.team,
      p.registeredAt, p.startedAt || null, p.finishedAt || null, p.score || 0, p.right || 0, p.wrong || 0, p.seconds || 0,
      p.sessionKey || null, p.heartbeatAt || null, p.finishReason || null, p.snippetTokens ? JSON.stringify(p.snippetTokens) : null, p.retryCount || 0);
    for (const p of oldParticipants) if (!participantIds.has(p.id)) sql.prepare('DELETE FROM participants WHERE id=?').run(p.id);
    for (const a of oldAccounts) if (!accountIds.has(a.id)) sql.prepare('DELETE FROM accounts WHERE id=?').run(a.id);
    sql.exec('COMMIT');
  } catch (e) {
    try { sql.exec('ROLLBACK'); } catch (_) { /* transaction may not have started */ }
    console.error('could not save database:', e.message);
    throw e;
  }
}

/* ---------------- helpers ---------------- */
const MIME = { '.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8', '.css': 'text/css; charset=utf-8',
  '.wasm': 'application/wasm', '.json': 'application/json', '.png': 'image/png', '.svg': 'image/svg+xml', '.ico': 'image/x-icon', '.txt': 'text/plain; charset=utf-8' };

function send(res, code, obj) {
  const body = JSON.stringify(obj);
  res.writeHead(code, {
    'Content-Type': 'application/json; charset=utf-8',
    'Cache-Control': 'no-store',
    'X-Content-Type-Options': 'nosniff',
    'X-Frame-Options': 'DENY',
    'Referrer-Policy': 'no-referrer',
    'Content-Security-Policy': "default-src 'none'; frame-ancestors 'none'"
  });
  res.end(body);
}
function readBody(req) {
  return new Promise((resolve, reject) => {
    let size = 0; const chunks = [];
    req.on('data', (c) => { size += c.length; if (size > 20000) { reject(new Error('too large')); req.destroy(); } else chunks.push(c); });
    req.on('end', () => { try { resolve(chunks.length ? JSON.parse(Buffer.concat(chunks).toString('utf8')) : {}); } catch (e) { reject(new Error('bad json')); } });
    req.on('error', reject);
  });
}
const clean = (s, max) => String(s == null ? '' : s).replace(/[\u0000-\u001f<>]/g, ' ').replace(/\s+/g, ' ').trim().slice(0, max);
const norm = (s) => s.toLowerCase();
function safeEqual(a, b) {
  const ha = crypto.createHash('sha256').update(String(a)).digest(), hb = crypto.createHash('sha256').update(String(b)).digest();
  return crypto.timingSafeEqual(ha, hb);
}
function publicView(p) { return { id: p.id, name: p.name, rollNo: p.rollNo, team: p.team }; }

/* ---------------- authentication sessions ---------------- */
const failures = new Map();               // ip -> [timestamps]
function clientIp(req) { return req.socket.remoteAddress || '?'; }
function tooManyFailures(ip) {
  const now = Date.now(), list = (failures.get(ip) || []).filter((t) => now - t < 5 * 60 * 1000);
  failures.set(ip, list); return list.length >= 5;
}
function adminOk(req) {
  const h = req.headers['authorization'] || '';
  const token = h.startsWith('Bearer ') ? h.slice(7) : '';
  if (!token) return false;
  const hash = crypto.createHash('sha256').update(token).digest('hex');
  const session = sql.prepare("SELECT expires_at FROM auth_sessions WHERE token_hash=? AND kind='admin'").get(hash);
  if (!session) return false;
  if (session.expires_at < Date.now()) {
    sql.prepare('DELETE FROM auth_sessions WHERE token_hash=?').run(hash);
    return false;
  }
  return true;
}
function playerAccount(req) {
  const h = req.headers['authorization'] || '';
  const token = h.startsWith('Bearer ') ? h.slice(7) : '';
  if (!token) return null;
  const hash = crypto.createHash('sha256').update(token).digest('hex');
  const session = sql.prepare("SELECT account_id, expires_at FROM auth_sessions WHERE token_hash=? AND kind='player'").get(hash);
  if (!session) return null;
  if (session.expires_at < Date.now()) {
    sql.prepare('DELETE FROM auth_sessions WHERE token_hash=?').run(hash);
    return null;
  }
  return session.account_id;
}
function revokePlayerSessions(accountId) {
  sql.prepare("DELETE FROM auth_sessions WHERE account_id=? AND kind='player'").run(accountId);
}
function revokeRequestSession(req) {
  const h = req.headers['authorization'] || '';
  const token = h.startsWith('Bearer ') ? h.slice(7) : '';
  if (token) sql.prepare('DELETE FROM auth_sessions WHERE token_hash=?').run(crypto.createHash('sha256').update(token).digest('hex'));
}
function issueSession(accountId, kind, lifetime) {
  const token = crypto.randomBytes(32).toString('hex');
  const hash = crypto.createHash('sha256').update(token).digest('hex');
  sql.prepare('INSERT INTO auth_sessions(token_hash,account_id,expires_at,kind) VALUES(?,?,?,?)')
    .run(hash, accountId, Date.now() + lifetime, kind);
  return token;
}
function issuePlayerSession(accountId) {
  return issueSession(accountId, 'player', 7 * 24 * 3600 * 1000);
}
function issueAdminSession() {
  return issueSession(null, 'admin', 12 * 3600 * 1000);
}
const passwordHash = async (password, salt) => {
  return (await pbkdf2(password, salt, 180000, 32, 'sha256')).toString('hex');
};
const pbkdf2 = promisify(crypto.pbkdf2);

/* ---------------- results ---------------- */
function adminData() {
  const all = db.participants;
  const accountFor = (p) => db.accounts.find((a) => a.id === p.accountId) || null;
  const emailFor = (p) => (accountFor(p) || {}).email || p.name || '';
  const finished = all.filter((p) => p.finishedAt);
  const individuals = finished.slice().sort((a, b) => b.score - a.score || a.seconds - b.seconds)
    .map((p, i) => ({ rank: i + 1, email: emailFor(p), team: p.team, score: p.score, right: p.right, wrong: p.wrong, seconds: p.seconds }));
  const groups = new Map();
  for (const p of all) {
    const k = norm(p.team);
    if (!groups.has(k)) groups.set(k, { team: p.team, members: [] });
    groups.get(k).members.push(p);
  }
  const teams = [];
  for (const g of groups.values()) {
    const fin = g.members.filter((m) => m.finishedAt);
    const total = fin.reduce((s, m) => s + m.score, 0), secs = fin.reduce((s, m) => s + m.seconds, 0);
    teams.push({ team: g.team, registered: g.members.length, finished: fin.length, totalScore: total,
      totalRight: fin.reduce((s, m) => s + (m.right || 0), 0), totalWrong: fin.reduce((s, m) => s + (m.wrong || 0), 0),
      avgScore: fin.length ? Math.round(total / fin.length * 10) / 10 : 0, totalSeconds: secs,
      avgSeconds: fin.length ? Math.round(secs / fin.length) : 0, members: g.members.map((m) => emailFor(m)) });
  }
  teams.sort((a, b) => (b.finished > 0) - (a.finished > 0) || b.avgScore - a.avgScore || a.avgSeconds - b.avgSeconds || a.team.localeCompare(b.team));
  let r = 0; teams.forEach((t) => { t.rank = t.finished ? ++r : null; });
  const now = Date.now();
  const participants = all.slice().sort((a, b) => a.registeredAt - b.registeredAt).map((p) => {
    const disconnected = Boolean(p.startedAt && (!p.heartbeatAt || now - p.heartbeatAt >= 45000));
    return {
      id: p.id, email: emailFor(p), passwordSet: Boolean(accountFor(p)), team: p.team, registeredAt: p.registeredAt,
      status: p.finishedAt ? 'Finished' : (p.startedAt ? (disconnected ? 'Disconnected' : 'Playing') : 'Registered'),
      right: p.right || 0, wrong: p.wrong || 0, score: p.score || 0,
      reason: p.finishReason || null, retryCount: p.retryCount || 0,
      canRetry: Boolean(((p.finishedAt && p.finishReason !== 'completed') || disconnected) && (p.retryCount || 0) < 1),
      seconds: p.finishedAt ? p.seconds : (p.startedAt ? Math.max(0, Math.floor((now - p.startedAt) / 1000)) : 0)
    };
  });
  return { apiVersion: 3, generatedAt: Date.now(), summary: { registered: all.length, playing: participants.filter((p) => p.status === 'Playing').length, finished: finished.length, teams: teams.length },
      participants, individuals, teams };
}

/* ---------------- API ---------------- */
async function api(req, res, url) {
  const route = req.method + ' ' + url.pathname;
  try {
    if (route === 'GET /api/teams') {
      return send(res, 200, { teams: [...new Set(db.participants.map((p) => p.team))].sort() });
    }
    if (route === 'POST /api/account/register' || route === 'POST /api/account/login' || route === 'POST /api/account/continue') {
      const ip = clientIp(req);
      if (tooManyFailures(ip)) return send(res, 429, { error: 'Too many attempts. Please wait 5 minutes.' });
      const b = await readBody(req);
      const email = clean(b.email, 254).toLowerCase();
      const password = String(b.password || '');
      const combined = route === 'POST /api/account/continue';
      const registering = route === 'POST /api/account/register';
      if (!/^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(email)) return send(res, 400, { error: 'Enter a valid email address.' });
      if (password.length < 8 || password.length > 128) return send(res, 400, { error: 'Password must be 8 to 128 characters.' });
      const teamIn = (combined || registering) ? clean(b.team, 40) : '';
      if (combined && !teamIn) return send(res, 400, { error: 'Enter your team name.' });
      let account = db.accounts.find((a) => a.email === email);
      if (registering && account) {
        if (!safeEqual(await passwordHash(password, account.salt), account.passwordHash))
          return send(res, 409, { error: 'This email already has an account. Use the correct password to finish registration, or choose Sign in.' });
      } else if (route.endsWith('/register') || (combined && !account)) {
        const salt = crypto.randomBytes(16).toString('hex');
        account = { id: crypto.randomBytes(12).toString('hex'), email, salt, passwordHash: await passwordHash(password, salt), createdAt: Date.now() };
        db.accounts.push(account); save();
      } else if (!account || !safeEqual(await passwordHash(password, account.salt), account.passwordHash)) {
        failures.get(ip).push(Date.now());
        return send(res, 401, { error: 'Email or password is incorrect.' });
      }
      if (registering && teamIn) {
        if (db.participants.some((p) => p.accountId === account.id))
          return send(res, 409, { error: 'This account is already registered. Choose Sign in to continue.' });
        const existingTeam = db.participants.find((p) => norm(p.team) === norm(teamIn));
        const team = existingTeam ? existingTeam.team : teamIn;
        const participant = { id: crypto.randomBytes(12).toString('hex'), accountId: account.id, name: account.email, rollNo: account.email,
          team, registeredAt: Date.now(), startedAt: null, finishedAt: null, score: 0, right: 0, wrong: 0, seconds: 0, sessionKey: null, heartbeatAt: null };
        db.participants.push(participant);
        save();
        return send(res, 200, { registered: true, email: account.email });
      }
      if (combined) {
        const old = db.participants.find((p) => p.accountId === account.id);
        if (old && old.finishedAt) return send(res, 409, { error: 'This account has already completed the game.' });
        const existingTeam = db.participants.find((p) => norm(p.team) === norm(teamIn));
        const team = old ? old.team : (existingTeam ? existingTeam.team : teamIn);
        let participant = old;
        if (!participant) {
          participant = { id: crypto.randomBytes(12).toString('hex'), accountId: account.id, name: account.email, rollNo: account.email,
            team, registeredAt: Date.now(), startedAt: null, finishedAt: null, score: 0, right: 0, wrong: 0, seconds: 0, sessionKey: null, heartbeatAt: null };
          db.participants.push(participant);
          save();
        }
        const token = issuePlayerSession(account.id);
        return send(res, 200, { token, email: account.email, participant: publicView(participant) });
      }
      const token = issuePlayerSession(account.id);
      return send(res, 200, { token, email: account.email });
    }
    if (route === 'GET /api/player/me') {
      const accountId = playerAccount(req);
      if (!accountId) return send(res, 401, { error: 'Please sign in again.' });
      const participant = db.participants.find((p) => p.accountId === accountId && !p.finishedAt)
        || db.participants.find((p) => p.accountId === accountId);
      if (!participant) return send(res, 409, { error: 'This account is not registered for the game. Choose Register first.' });
      if (participant.finishedAt) return send(res, 409, { error: 'This account has already completed the game.' });
      return send(res, 200, { participant: publicView(participant) });
    }
    if (route === 'POST /api/account/logout') {
      revokeRequestSession(req);
      return send(res, 200, { ok: true });
    }
    if (route === 'POST /api/register') {
      const accountId = playerAccount(req);
      if (!accountId) return send(res, 401, { error: 'Sign in or create an account first.' });
      const b = await readBody(req);
      const name = clean(b.name, 60), rollNo = clean(b.rollNo, 30), teamIn = clean(b.team, 40);
      if (name.length < 2) return send(res, 400, { error: 'Please enter your full name.' });
      if (!rollNo) return send(res, 400, { error: 'Please enter your register number.' });
      if (!teamIn) return send(res, 400, { error: 'Please enter your team name.' });
      const existingTeam = db.participants.find((p) => norm(p.team) === norm(teamIn));
      const team = existingTeam ? existingTeam.team : teamIn;
      const old = db.participants.find((p) => p.accountId === accountId && norm(p.rollNo) === norm(rollNo));
      if (old) {
        if (old.finishedAt) return send(res, 409, { error: 'This register number has already completed the game. Each student can play once.' });
        return send(res, 200, { participant: publicView(old), resumed: true });       // not finished yet: let them continue
      }
      const usedRoll = db.participants.find((p) => norm(p.rollNo) === norm(rollNo));
      if (usedRoll) return send(res, 409, { error: 'This register number is already linked to another account. Please sign in with the account used for this student.' });
      const p = { id: crypto.randomBytes(12).toString('hex'), accountId, name, rollNo, team, registeredAt: Date.now(), startedAt: null, finishedAt: null, score: 0, right: 0, wrong: 0, seconds: 0 };
      db.participants.push(p); save();
      return send(res, 200, { participant: publicView(p) });
    }
    if (route === 'POST /api/start') {
      const accountId = playerAccount(req);
      if (!accountId) return send(res, 401, { error: 'Please sign in again.' });
      const b = await readBody(req);
      const p = db.participants.find((x) => x.id === b.id && x.accountId === accountId);
      if (!p) return send(res, 404, { error: 'Unknown player.' });
      if (p.finishedAt) return send(res, 409, { error: 'Already finished.' });
      const sessionKey = clean(b.sessionKey, 80);
      if (!sessionKey) return send(res, 400, { error: 'Missing game session key.' });
      const now = Date.now(), leaseMs = 45000;
      const otherActive = db.participants.find((x) => x.accountId === accountId && x.id !== p.id && !x.finishedAt && x.sessionKey && x.heartbeatAt && now - x.heartbeatAt < leaseMs);
      if (otherActive) return send(res, 409, { error: 'This account already has a game open on another tab or computer.' });
      if (p.sessionKey && p.sessionKey !== sessionKey && p.heartbeatAt && now - p.heartbeatAt < leaseMs)
        return send(res, 409, { error: 'This game is already open on another tab or computer.' });
      p.startedAt = p.startedAt || now; p.sessionKey = sessionKey; p.heartbeatAt = now;
      p.snippetTokens = Array.from({ length: 20 }, () => crypto.randomBytes(18).toString('base64url'));
      save();
      return send(res, 200, { ok: true, snippetTokens: p.snippetTokens });
    }
    if (route === 'POST /api/heartbeat') {
      const accountId = playerAccount(req);
      if (!accountId) return send(res, 401, { error: 'Please sign in again.' });
      const b = await readBody(req);
      const p = db.participants.find((x) => x.id === b.id && x.accountId === accountId);
      if (!p) return send(res, 404, { error: 'Unknown player.' });
      if (p.finishedAt || !p.sessionKey || p.sessionKey !== clean(b.sessionKey, 80) || !p.heartbeatAt || Date.now() - p.heartbeatAt >= 45000)
        return send(res, 409, { error: 'This game session expired or is active on another tab/computer.' });
      p.heartbeatAt = Date.now(); save();
      return send(res, 200, { ok: true });
    }
    if (route === 'POST /api/submit') {
      const accountId = playerAccount(req);
      if (!accountId) return send(res, 401, { error: 'Please sign in again.' });
      const b = await readBody(req);
      const p = db.participants.find((x) => x.id === b.id && x.accountId === accountId);
      if (!p) return send(res, 404, { error: 'Unknown player.' });
      if (p.finishedAt) return send(res, 409, { error: 'Result already saved.', result: { score: p.score, right: p.right, wrong: p.wrong, seconds: p.seconds } });
      if (!p.sessionKey || p.sessionKey !== clean(b.sessionKey, 80)) return send(res, 409, { error: 'This game session is not active on this tab/computer.' });
      const serverElapsed = p.startedAt ? Math.floor((Date.now() - p.startedAt) / 1000) : 0;
      const timedOut = serverElapsed >= MAX_GAME_SECONDS;
      const forfeited = b.forfeit === true;
      const timeoutRequested = b.finishReason === 'timeout';
      const submittedTokens = b.snippetTokens;
      if (!timedOut && !forfeited && !timeoutRequested && (!Array.isArray(submittedTokens) || submittedTokens.length > 20))
        return send(res, 400, { error: 'Invalid submitted snippets.' });
      const tokenToSnippet = new Map((p.snippetTokens || []).map((token, index) => [token, index]));
      const ids = timedOut || forfeited || timeoutRequested ? [] : submittedTokens.map((token) => tokenToSnippet.get(token));
      if (!timedOut && !forfeited && !timeoutRequested && (ids.some((id) => id === undefined) || new Set(submittedTokens).size !== submittedTokens.length))
        return send(res, 400, { error: 'One or more submitted snippets are invalid.' });
      const right = timedOut || forfeited || timeoutRequested ? 0 : ids.filter((id) => CORRECT_SNIPPETS.has(id)).length;
      const wrong = timedOut || forfeited || timeoutRequested ? 0 : ids.length - right;
      const seconds = Math.min(serverElapsed, MAX_GAME_SECONDS);
      const finishReason = timedOut || timeoutRequested ? 'timeout' : (forfeited ? 'forfeit' : 'completed');
      p.right = right; p.wrong = wrong; p.score = Math.max(0, (right - wrong) * 10); p.seconds = seconds;
      p.finishedAt = Date.now(); p.heartbeatAt = null; p.finishReason = finishReason;
      save();
      return send(res, 200, { ok: true, result: { score: p.score, right: p.right, wrong: p.wrong, seconds: p.seconds, reason: finishReason } });
    }
    if (route === 'POST /api/admin/login') {
      const ip = clientIp(req);
      if (tooManyFailures(ip)) return send(res, 429, { error: 'Too many wrong attempts. Please wait 5 minutes.' });
      const b = await readBody(req);
      if (!safeEqual(b.password || '', ADMIN_PASSWORD)) {
        failures.get(ip).push(Date.now());
        return send(res, 401, { error: 'Wrong password.' });
      }
      const token = issueAdminSession();
      return send(res, 200, { token });
    }
    if (route === 'POST /api/admin/logout') {
      revokeRequestSession(req);
      return send(res, 200, { ok: true });
    }
    if (route === 'GET /api/admin/data') {
      if (!adminOk(req)) return send(res, 401, { error: 'Please log in.' });
      return send(res, 200, adminData());
    }
    if (route === 'POST /api/admin/reset-password') {
      if (!adminOk(req)) return send(res, 401, { error: 'Please log in.' });
      const b = await readBody(req), participantId = clean(b.participantId, 80);
      const participant = db.participants.find((p) => p.id === participantId);
      if (!participant || !participant.accountId) return send(res, 404, { error: 'This player has no linked account.' });
      const account = db.accounts.find((a) => a.id === participant.accountId);
      if (!account) return send(res, 404, { error: 'Player account was not found.' });
      const newPassword = crypto.randomBytes(9).toString('base64url');
      const previous = { salt: account.salt, passwordHash: account.passwordHash };
      const previousPlayerLeases = db.participants.filter((p) => p.accountId === account.id)
        .map((p) => ({ p, sessionKey: p.sessionKey, heartbeatAt: p.heartbeatAt, snippetTokens: p.snippetTokens }));
      account.salt = crypto.randomBytes(16).toString('hex');
      account.passwordHash = await passwordHash(newPassword, account.salt);
      for (const p of db.participants) if (p.accountId === account.id) {
        p.sessionKey = null; p.heartbeatAt = null; p.snippetTokens = null;
      }
      try { save(); } catch (e) {
        account.salt = previous.salt; account.passwordHash = previous.passwordHash;
        for (const lease of previousPlayerLeases) {
          lease.p.sessionKey = lease.sessionKey; lease.p.heartbeatAt = lease.heartbeatAt; lease.p.snippetTokens = lease.snippetTokens;
        }
        throw e;
      }
      revokePlayerSessions(account.id);
      return send(res, 200, { ok: true, newPassword });
    }
    if (route === 'POST /api/admin/remove-player') {
      if (!adminOk(req)) return send(res, 401, { error: 'Please log in.' });
      const b = await readBody(req), participantId = clean(b.participantId, 80);
      const target = db.participants.find((p) => p.id === participantId);
      if (!target) return send(res, 404, { error: 'Player was not found.' });
      const previousParticipants = db.participants, previousAccounts = db.accounts;
      const accountId = target.accountId;
      db.participants = db.participants.filter((p) => p.id !== participantId);
      if (accountId && !db.participants.some((p) => p.accountId === accountId))
        db.accounts = db.accounts.filter((a) => a.id !== accountId);
      try { save(); } catch (e) { db.participants = previousParticipants; db.accounts = previousAccounts; throw e; }
      if (accountId && !db.accounts.some((a) => a.id === accountId)) revokePlayerSessions(accountId);
      return send(res, 200, { ok: true });
    }
    if (route === 'POST /api/admin/allow-retry') {
      if (!adminOk(req)) return send(res, 401, { error: 'Please log in.' });
      const b = await readBody(req), participantId = clean(b.participantId, 80);
      const participant = db.participants.find((p) => p.id === participantId);
      if (!participant) return send(res, 404, { error: 'Player was not found.' });
      const disconnected = Boolean(participant.startedAt && (!participant.heartbeatAt || Date.now() - participant.heartbeatAt >= 45000));
      const eligibleResult = participant.finishedAt && participant.finishReason !== 'completed';
      if ((!eligibleResult && !disconnected) || (participant.retryCount || 0) >= 1)
        return send(res, 409, { error: 'This player is not eligible for another attempt.' });
      const previous = { finishedAt: participant.finishedAt, startedAt: participant.startedAt, score: participant.score,
        right: participant.right, wrong: participant.wrong, seconds: participant.seconds, finishReason: participant.finishReason,
        retryCount: participant.retryCount, sessionKey: participant.sessionKey, heartbeatAt: participant.heartbeatAt,
        snippetTokens: participant.snippetTokens };
      participant.finishedAt = null; participant.startedAt = null; participant.score = 0;
      participant.right = 0; participant.wrong = 0; participant.seconds = 0;
      participant.finishReason = null; participant.sessionKey = null; participant.heartbeatAt = null;
      participant.snippetTokens = null; participant.retryCount = (participant.retryCount || 0) + 1;
      try { save(); } catch (e) {
        Object.assign(participant, previous);
        throw e;
      }
      return send(res, 200, { ok: true });
    }
    return send(res, 404, { error: 'Not found.' });
  } catch (e) {
    console.error('API request failed:', e.message);
    return send(res, 500, { error: 'Unable to process the request. Please retry.' });
  }
}

/* ---------------- static files ---------------- */
function serveStatic(req, res, url) {
  let p;
  try { p = decodeURIComponent(url.pathname); }
  catch (e) { res.writeHead(400, { 'Content-Type': 'text/plain; charset=utf-8' }); return res.end('Bad request'); }
  if (p === '/admin') p = '/admin.html';
  if (p === '/') p = '/index.html';
  // Team URLs (for example /google) are friendly entry URLs for the same game app.
  if (/^\/[^/]+$/.test(p) && !path.extname(p)) p = '/index.html';
  const file = path.normalize(path.join(PUBLIC, p));
  if (!file.startsWith(PUBLIC + path.sep)) { res.writeHead(403); return res.end('Forbidden'); }
  fs.readFile(file, (err, data) => {
    if (err) { res.writeHead(404, { 'Content-Type': 'text/plain' }); return res.end('Not found'); }
    res.writeHead(200, {
      'Content-Type': MIME[path.extname(file).toLowerCase()] || 'application/octet-stream',
      'Cache-Control': 'no-cache',
      'X-Content-Type-Options': 'nosniff',
      'X-Frame-Options': 'DENY',
      'Referrer-Policy': 'no-referrer',
      'Content-Security-Policy': "default-src 'self'; script-src 'self' 'wasm-unsafe-eval'; style-src 'self' 'unsafe-inline'; img-src 'self' data:; connect-src 'self'; object-src 'none'; base-uri 'self'; frame-ancestors 'none'"
    });
    res.end(data);
  });
}

const server = http.createServer((req, res) => {
  let url;
  try { url = new URL(req.url, 'http://x'); } catch (e) { res.writeHead(400); return res.end(); }
  if (url.pathname.startsWith('/api/')) return api(req, res, url);
  if (req.method !== 'GET' && req.method !== 'HEAD') { res.writeHead(405); return res.end(); }
  serveStatic(req, res, url);
});

server.on('error', (error) => {
  if (error.code === 'EADDRINUSE') {
    console.error('Port ' + PORT + ' is already in use. If Campus Quest is already running, use that server instead of starting another copy.');
    console.error('Otherwise, stop the process using this port or start Campus Quest with a different PORT, for example: set PORT=4001 && node server.js');
    process.exitCode = 1;
    return;
  }
  throw error;
});

server.listen(PORT, '0.0.0.0', () => {
  console.log('\n  CAMPUS QUEST is running');
  console.log('  Players : http://localhost:' + PORT);
  console.log('  Admin   : http://localhost:' + PORT + '/admin');
  for (const [name, list] of Object.entries(os.networkInterfaces()))
    for (const a of list || []) if (a.family === 'IPv4' && !a.internal)
      console.log('  On ' + name + ': http://' + a.address + ':' + PORT);
  console.log('');
});
