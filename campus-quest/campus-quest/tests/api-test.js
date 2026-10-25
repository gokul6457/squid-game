'use strict';

const assert = require('node:assert/strict');
const { spawn } = require('node:child_process');
const fs = require('node:fs');
const http = require('node:http');
const os = require('node:os');
const path = require('node:path');
const { DatabaseSync } = require('node:sqlite');

const root = path.resolve(__dirname, '..');
const dataDir = fs.mkdtempSync(path.join(os.tmpdir(), 'campus-quest-test-'));
const adminPassword = 'test-admin-password-123';
let port;
let server;
let base;

async function availablePort() {
  return new Promise((resolve, reject) => {
    const listener = http.createServer();
    listener.once('error', reject);
    listener.listen(0, '127.0.0.1', () => {
      const address = listener.address();
      listener.close((error) => error ? reject(error) : resolve(address.port));
    });
  });
}

function spawnServer(password) {
  const env = { ...process.env, PORT: String(port), DATA_DIR: dataDir };
  if (password) env.ADMIN_PASSWORD = password;
  else delete env.ADMIN_PASSWORD;
  return spawn(process.execPath, [path.join(root, 'server.js')], {
    cwd: root,
    env,
    stdio: 'ignore',
    windowsHide: true,
  });
}

async function waitForServer(child) {
  for (let attempt = 0; attempt < 100; attempt++) {
    if (child.exitCode !== null) throw new Error('Server exited before becoming ready.');
    try {
      const response = await fetch(base + '/');
      if (response.ok) return;
    } catch (error) {
      await new Promise((resolve) => setTimeout(resolve, 100));
    }
  }
  throw new Error('Server did not become ready.');
}

async function stopServer() {
  if (!server || server.exitCode !== null) return;
  await new Promise((resolve, reject) => {
    const timeout = setTimeout(() => {
      server.kill();
      reject(new Error('Server did not stop.'));
    }, 5000);
    server.once('exit', () => { clearTimeout(timeout); resolve(); });
    server.kill();
  });
  server = null;
}

async function request(method, route, body, token) {
  const headers = {};
  if (body !== undefined) headers['Content-Type'] = 'application/json';
  if (token) headers.Authorization = 'Bearer ' + token;
  const response = await fetch(base + route, {
    method,
    headers,
    body: body === undefined ? undefined : JSON.stringify(body),
  });
  const text = await response.text();
  let json = {};
  try { json = text ? JSON.parse(text) : {}; } catch (error) { /* non-JSON error response */ }
  return { status: response.status, json, headers: response.headers };
}

async function run() {
  const wasm = await WebAssembly.compile(fs.readFileSync(path.join(root, 'public', 'game', 'game.wasm')));
  const wasmImports = WebAssembly.Module.imports(wasm);
  const wasmExports = new Set(WebAssembly.Module.exports(wasm).map((entry) => entry.name));
  assert.ok(wasmImports.some((entry) => entry.module === 'env' && entry.name === 'js_report'),
    'game binary imports the snippet-report callback');
  for (const name of ['memory', '_initialize', 'game_init', 'game_frame', 'game_elapsed']) {
    assert.ok(wasmExports.has(name), 'game binary exports ' + name);
  }
  const wasmStubs = {};
  for (const entry of wasmImports) {
    (wasmStubs[entry.module] ||= {})[entry.name] = () => 0;
  }
  const instance = await WebAssembly.instantiate(wasm, wasmStubs);
  instance.exports._initialize();
  instance.exports.game_init();
  instance.exports.game_frame();

  port = await availablePort();
  base = 'http://127.0.0.1:' + port;

  for (const invalidPassword of ['', 'admin@123']) {
    const rejected = spawnServer(invalidPassword);
    const rejectedCode = await new Promise((resolve, reject) => {
      const timeout = setTimeout(() => {
        rejected.kill();
        reject(new Error('Server did not reject an unsafe admin password.'));
      }, 5000);
      rejected.once('exit', (code) => { clearTimeout(timeout); resolve(code); });
    });
    assert.equal(rejectedCode, 1, 'server refuses to start without a safe admin password');
  }

  server = spawnServer(adminPassword);
  await waitForServer(server);

  const malformed = await request('GET', '/%E0%A4%A');
  assert.equal(malformed.status, 400, 'malformed percent-encoding receives HTTP 400');
  assert.equal((await request('GET', '/')).status, 200, 'server remains available after a malformed URL');

  const registration = await request('POST', '/api/account/register', {
    email: 'first@example.test', password: 'player-password-123', team: 'Red Hawks',
  });
  assert.equal(registration.status, 200, 'player registration succeeds');

  const login = await request('POST', '/api/account/login', {
    email: 'first@example.test', password: 'player-password-123',
  });
  assert.equal(login.status, 200, 'player login succeeds');
  const playerToken = login.json.token;
  const profile = await request('GET', '/api/player/me', undefined, playerToken);
  assert.equal(profile.status, 200, 'player profile is available');
  const participantId = profile.json.participant.id;

  const adminLogin = await request('POST', '/api/admin/login', { password: adminPassword });
  assert.equal(adminLogin.status, 200, 'admin login succeeds');
  const adminToken = adminLogin.json.token;

  const started = await request('POST', '/api/start', {
    id: participantId, sessionKey: 'first-game-session',
  }, playerToken);
  assert.equal(started.status, 200, 'game starts and receives server-issued snippet tokens');
  assert.equal(started.json.snippetTokens.length, 20, 'challenge contains all 20 snippets');

  const forgedScore = await request('POST', '/api/submit', {
    id: participantId, sessionKey: 'first-game-session', right: 10, wrong: 0, seconds: 1,
  }, playerToken);
  assert.equal(forgedScore.status, 400, 'browser-supplied score without snippet tokens is rejected');

  const invalidTokens = await request('POST', '/api/submit', {
    id: participantId, sessionKey: 'first-game-session', snippetTokens: ['forged-token'],
  }, playerToken);
  assert.equal(invalidTokens.status, 400, 'unissued snippet tokens are rejected');

  await stopServer();
  server = spawnServer(adminPassword);
  await waitForServer(server);
  assert.equal((await request('GET', '/api/player/me', undefined, playerToken)).status, 200,
    'player login session survives a server restart');
  assert.equal((await request('GET', '/api/admin/data', undefined, adminToken)).status, 200,
    'admin login session survives a server restart');
  assert.equal((await request('POST', '/api/heartbeat', {
    id: participantId, sessionKey: 'first-game-session',
  }, playerToken)).status, 200, 'active game session survives a server restart');

  const submitted = await request('POST', '/api/submit', {
    id: participantId,
    sessionKey: 'first-game-session',
    snippetTokens: started.json.snippetTokens.slice(0, 10),
    right: 0,
    wrong: 10,
  }, playerToken);
  assert.equal(submitted.status, 200, 'valid issued snippet tokens can be submitted');
  assert.equal(submitted.json.result.score, 100, 'server computes score from snippet answers, not supplied counts');
  assert.equal(submitted.json.result.right, 10, 'server reports its own correct count');
  assert.equal(submitted.json.result.wrong, 0, 'server reports its own incorrect count');

  const secondRegistration = await request('POST', '/api/account/register', {
    email: 'second@example.test', password: 'another-password-123', team: 'Blue Owls',
  });
  assert.equal(secondRegistration.status, 200, 'second player registration succeeds');
  const secondLogin = await request('POST', '/api/account/login', {
    email: 'second@example.test', password: 'another-password-123',
  });
  const secondToken = secondLogin.json.token;
  const secondProfile = await request('GET', '/api/player/me', undefined, secondToken);
  const secondId = secondProfile.json.participant.id;
  const secondStart = await request('POST', '/api/start', {
    id: secondId, sessionKey: 'second-game-session',
  }, secondToken);
  assert.equal(secondStart.status, 200, 'second player can start');

  const forfeited = await request('POST', '/api/submit', {
    id: secondId, sessionKey: 'second-game-session', snippetTokens: [], forfeit: true,
  }, secondToken);
  assert.equal(forfeited.status, 200, 'player can save an explicit forfeit');
  assert.equal(forfeited.json.result.reason, 'forfeit', 'forfeit reason is retained');
  assert.equal(forfeited.json.result.score, 0, 'forfeit scores zero');

  const retry = await request('POST', '/api/admin/allow-retry', { participantId: secondId }, adminToken);
  assert.equal(retry.status, 200, 'admin can allow one retry after a forfeit');
  assert.equal((await request('GET', '/api/player/me', undefined, secondToken)).status, 200,
    'player can sign in again after a retry is allowed');
  assert.equal((await request('POST', '/api/admin/allow-retry', { participantId: secondId }, adminToken)).status, 409,
    'a player cannot be granted more than one retry');

  const thirdRegistration = await request('POST', '/api/account/register', {
    email: 'third@example.test', password: 'third-password-123', team: 'Green Turtles',
  });
  assert.equal(thirdRegistration.status, 200, 'third player registration succeeds');
  const thirdLogin = await request('POST', '/api/account/login', {
    email: 'third@example.test', password: 'third-password-123',
  });
  const thirdToken = thirdLogin.json.token;
  const thirdProfile = await request('GET', '/api/player/me', undefined, thirdToken);
  const thirdId = thirdProfile.json.participant.id;
  assert.equal((await request('POST', '/api/start', {
    id: thirdId, sessionKey: 'disconnected-game-session',
  }, thirdToken)).status, 200, 'third player can start');
  const testDb = new DatabaseSync(path.join(dataDir, 'campus-quest.sqlite'));
  testDb.prepare('UPDATE participants SET heartbeat_at=? WHERE id=?').run(Date.now() - 60000, thirdId);
  testDb.close();
  await stopServer();
  server = spawnServer(adminPassword);
  await waitForServer(server);

  const disconnectedData = await request('GET', '/api/admin/data', undefined, adminToken);
  const disconnected = disconnectedData.json.participants.find((p) => p.id === thirdId);
  assert.equal(disconnected.status, 'Disconnected', 'admin sees stale sessions as disconnected');
  assert.equal(disconnected.canRetry, true, 'admin can retry a disconnected session');
  assert.equal((await request('POST', '/api/admin/allow-retry', { participantId: thirdId }, adminToken)).status, 200,
    'admin can allow a retry after a disconnected session');

  const data = await request('GET', '/api/admin/data', undefined, adminToken);
  assert.equal(data.json.apiVersion, 3, 'admin data exposes the updated API version');
  assert.equal(data.json.participants.find((p) => p.id === participantId).reason, 'completed',
    'admin data distinguishes completed results');
  console.log('ALL API TESTS PASSED');
}

run().catch((error) => {
  console.error(error);
  process.exitCode = 1;
}).finally(async () => {
  await stopServer();
  fs.rmSync(dataDir, { recursive: true, force: true });
});
