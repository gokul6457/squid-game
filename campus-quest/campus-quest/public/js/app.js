/* Campus Quest - page logic: registration, game box (80% of the page), timer, score at the top, saving the result */
(function () {
  'use strict';
  const $ = (id) => document.getElementById(id);
  const fmt = (s) => String(Math.floor(s / 60)).padStart(2, '0') + ':' + String(s % 60).padStart(2, '0');
  const GAME_LIMIT_SECONDS = 20 * 60;
  function teamPath(team, participantId) {
    let slug = String(team || '').normalize('NFKC').trim().toLowerCase()
      .replace(/[^\p{L}\p{N}]+/gu, '-').replace(/^-+|-+$/g, '');
    if (!slug) slug = 'team-' + String(participantId || '').slice(0, 8);
    if (['admin', 'api', 'index', 'style', 'js', 'game'].includes(slug)) slug = 'team-' + slug;
    return '/' + slug;
  }
  let player = null, game = null, tick = null, heartbeat = null, warningTimer = null, warningDeadline = 0, warningReason = '';
  let finished = false, playing = false, sessionLost = false, fullscreenEntered = false, fullscreenPromise = null, playerToken = localStorage.getItem('cq_token') || '';
  const gameSessionKey = (crypto.randomUUID ? crypto.randomUUID() : Array.from(crypto.getRandomValues(new Uint8Array(16)), x => x.toString(16).padStart(2, '0')).join(''));

  function toast(msg, kind, ms) {
    const t = $('toast'); t.textContent = msg; t.className = 'toast ' + (kind || ''); 
    clearTimeout(toast.h); if (ms !== 0) toast.h = setTimeout(() => t.classList.add('hidden'), ms || 3500);
  }
  async function post(url, body, options) {
    const headers = { 'Content-Type': 'application/json' };
    if (playerToken && !url.startsWith('/api/admin/')) headers.Authorization = 'Bearer ' + playerToken;
    const r = await fetch(url, { method: 'POST', headers, body: JSON.stringify(body), keepalive: !!(options && options.keepalive) });
    let j = {}; try { j = await r.json(); } catch (e) { /* ignore */ }
    if (!r.ok) { const e = new Error(j.error || 'Something went wrong.'); e.status = r.status; e.data = j; throw e; }
    return j;
  }
  async function get(url) {
    const r = await fetch(url, { headers: playerToken ? { Authorization: 'Bearer ' + playerToken } : {}, cache: 'no-store' });
    let j = {}; try { j = await r.json(); } catch (e) { /* ignore */ }
    if (!r.ok) { const error = new Error(j.error || 'Something went wrong.'); error.status = r.status; throw error; }
    return j;
  }

  /* ---- the game box always covers 80% of the free page area ---- */
  function layoutStage() {
    const m = $('main').getBoundingClientRect(), s = $('stage');
    const bw = m.width * 0.8, bh = m.height * 0.8;
    const w = Math.max(320, Math.min(bw, bh * 16 / 9)), h = w * 9 / 16;
    s.style.width = Math.round(w) + 'px'; s.style.height = Math.round(h) + 'px';
  }
  window.addEventListener('resize', layoutStage);

  function requestGameFullscreen() {
    const stage = $('stage');
    if (document.fullscreenElement === stage) return Promise.resolve(true);
    if (!stage.requestFullscreen) return Promise.resolve(false);
    if (fullscreenPromise) return fullscreenPromise;
    fullscreenPromise = stage.requestFullscreen({ navigationUI: 'hide' }).then(() => document.fullscreenElement === stage)
      .catch(() => false).finally(() => { fullscreenPromise = null; });
    return fullscreenPromise;
  }
  document.addEventListener('fullscreenchange', () => {
    const inGameFullscreen = document.fullscreenElement === $('stage');
    document.documentElement.classList.toggle('gameFullscreen', inGameFullscreen);
    if (inGameFullscreen) { fullscreenEntered = true; layoutStage(); }
    else if (fullscreenEntered && playing && !finished) startWarning('fullscreen-exit');
  });
  document.addEventListener('visibilitychange', () => {
    if (document.hidden && playing && !finished) startWarning('tab-hidden');
  });
  window.addEventListener('blur', () => {
    setTimeout(() => { if (playing && !finished && !document.hasFocus()) startWarning('window-blur'); }, 150);
  });
  window.addEventListener('pagehide', () => {
    if (playing && !finished) finishAttempt('page-left');
  });
  window.addEventListener('beforeunload', (event) => {
    if (!playing || finished) return;
    event.preventDefault();
    event.returnValue = '';
  });

  function showAuth(view) {
    $('authChoice').classList.toggle('hidden', view !== 'choice');
    $('signInView').classList.toggle('hidden', view !== 'signin');
    $('registerView').classList.toggle('hidden', view !== 'register');
    if (view === 'signin') $('loginEmail').focus();
    if (view === 'register') $('registerTeam').focus();
  }
  $('chooseSignIn').addEventListener('click', () => showAuth('signin'));
  $('chooseRegister').addEventListener('click', () => showAuth('register'));
  $('toRegister').addEventListener('click', () => showAuth('register'));
  $('toSignIn').addEventListener('click', () => showAuth('signin'));
  document.querySelectorAll('.authBack').forEach((button) => button.addEventListener('click', () => showAuth('choice')));
  try {
    $('loginEmail').value = localStorage.getItem('cq_email') || '';
    $('registerEmail').value = localStorage.getItem('cq_email') || '';
    const previous = JSON.parse(localStorage.getItem('cq_last') || '{}');
    $('registerTeam').value = previous.team || '';
  } catch (e) { /* ignore invalid local storage */ }

  $('registerForm').addEventListener('submit', async (ev) => {
    ev.preventDefault();
    $('registerErr').textContent = ''; $('registerBtn').disabled = true;
    try {
      const result = await post('/api/account/register', {
        email: $('registerEmail').value.trim(), password: $('registerPassword').value, team: $('registerTeam').value.trim(),
      });
      localStorage.setItem('cq_email', result.email);
      localStorage.setItem('cq_last', JSON.stringify({ team: $('registerTeam').value.trim() }));
      $('loginEmail').value = result.email;
      $('registerPassword').value = '';
      showAuth('signin');
      toast('Registration complete. Sign in to start the game.', 'ok');
    } catch (e) {
      $('registerErr').textContent = e.message === 'Failed to fetch' ? 'Cannot reach the server. Check the network and try again.' : e.message;
    } finally { $('registerBtn').disabled = false; }
  });

  $('loginForm').addEventListener('submit', async (ev) => {
    ev.preventDefault();
    $('loginErr').textContent = ''; $('loginBtn').disabled = true;
    try {
      const auth = await post('/api/account/login', { email: $('loginEmail').value.trim(), password: $('loginPassword').value });
      playerToken = auth.token;
      localStorage.setItem('cq_token', playerToken);
      localStorage.setItem('cq_email', auth.email);
      const profile = await get('/api/player/me');
      player = profile.participant;
      $('loginPassword').value = '';
      localStorage.setItem('cq_last', JSON.stringify({ team: player.team }));
      history.replaceState({ team: player.team }, '', teamPath(player.team, player.id));
      document.title = player.team + ' - Campus Quest';
      beginGame();
    } catch (e) {
      $('loginErr').textContent = e.message === 'Failed to fetch' ? 'Cannot reach the server. Check the network and try again.' : e.message;
    } finally { $('loginBtn').disabled = false; }
  });

  async function beginGame() {
    $('regCard').classList.add('hidden'); $('stage').classList.remove('hidden');
    $('pName').textContent = player.name; $('pTeam').textContent = player.team; $('chipPlayer').classList.remove('hidden');
    $('teamSubtitle').textContent = 'Team: ' + player.team;
    $('chipTime').classList.remove('hidden');
    layoutStage();
    try {
        game = await window.CampusGame($('gameCanvas'), {
          onStart, onFinish, onGesture: requestGameFullscreen, onViolation: startWarning, version: '4',
        });
      $('loading').classList.add('hidden');
      $('gameCanvas').focus();
    } catch (e) {
      $('loading').textContent = 'Could not start the game: ' + e.message;
    }
  }

  /* ---- messages from the game ---- */
  function onStart() {
    if (playing || finished) return;
    playing = true;
    game.setActive(false);
    const startServerSession = () => post('/api/start', { id: player.id, sessionKey: gameSessionKey }).then(({ snippetTokens }) => {
      if (finished || !game) return;
      game.setSnippetTokens(snippetTokens);
      game.setActive(true);
      clearInterval(heartbeat);
      heartbeat = setInterval(async () => {
        try { await post('/api/heartbeat', { id: player.id, sessionKey: gameSessionKey }); }
        catch (e) {
          if (e.status === 409) {
            sessionLost = true;
            clearInterval(heartbeat); clearInterval(tick);
            if (game) game.setActive(false);
            toast(e.message || 'This game session has expired. Please contact staff.', 'bad', 0);
          } else {
            toast('Connection interrupted. The game will keep trying to reconnect.', 'bad', 4000);
          }
        }
      }, 15000);
    }).catch((e) => {
      if ((!e.status || e.status >= 500) && !finished) {
        toast('Connection interrupted while starting. Retrying…', 'bad', 4000);
        setTimeout(() => { if (playing && !finished) startServerSession(); }, 3000);
        return;
      }
      playing = false;
      if (game) game.setActive(false);
      toast(e.message, 'bad', 0);
    });
    Promise.resolve(fullscreenPromise || document.fullscreenElement === $('stage'))
      .then((entered) => {
        if (finished) return;
        if (!entered && !fullscreenEntered) toast('Fullscreen was not available. The game will continue in this window.', 'bad', 5000);
        startServerSession();
      });
    clearInterval(tick);
    tick = setInterval(() => {
      if (finished || !game) return;
      const elapsed = Math.min(GAME_LIMIT_SECONDS, game.elapsed());
      $('timer').textContent = fmt(elapsed);
      if (elapsed >= GAME_LIMIT_SECONDS) finishByTimeout();
    }, 250);
  }
  function finishByTimeout() {
    if (finished) return;
    finished = true; clearInterval(tick); clearInterval(heartbeat);
    if (game) game.setActive(false);
    $('timer').textContent = '20:00';
    $('chipTime').classList.add('hidden');
    $('endTitle').textContent = 'TIME OUT';
    $('endMessage').textContent = '20 minutes are up. Your game has ended.';
    $('timeoutOverlay').classList.remove('hidden');
    $('foot').textContent = 'Time is up. This game attempt has ended.';
    save({ score: 0, right: 0, wrong: 0, seconds: GAME_LIMIT_SECONDS, snippetTokens: [] }, 0, false, false, 'timeout');
  }
  function startWarning(reason) {
    if (!playing || finished || warningTimer) return;
    warningReason = reason;
    warningDeadline = Date.now() + 10000;
    game.setActive(false);
    $('endTitle').textContent = 'ARE YOU STILL THERE?';
    $('resumeGame').classList.toggle('hidden', sessionLost);
    $('timeoutOverlay').classList.remove('hidden');
    const updateWarning = () => {
      const remaining = Math.max(0, Math.ceil((warningDeadline - Date.now()) / 1000));
      $('endMessage').textContent = 'Return to the game within ' + remaining + ' seconds or this attempt will end.';
      if (!remaining) finishAttempt(warningReason);
    };
    updateWarning();
    warningTimer = setInterval(updateWarning, 250);
  }
  async function resumeGame() {
    if (!warningTimer || finished || document.hidden || sessionLost) return;
    if (fullscreenEntered && document.fullscreenElement !== $('stage')) {
      const entered = await requestGameFullscreen();
      if (!entered) {
        toast('Use the browser fullscreen control to return to the game.', 'bad');
        return;
      }
    }
    clearInterval(warningTimer); warningTimer = null;
    $('timeoutOverlay').classList.add('hidden');
    if (game) game.setActive(true);
  }
  $('resumeGame').addEventListener('click', resumeGame);
  function finishAttempt(reason) {
    if (!playing || finished) return;
    finished = true; clearInterval(tick); clearInterval(heartbeat);
    clearInterval(warningTimer); warningTimer = null;
    const seconds = Math.min(GAME_LIMIT_SECONDS, game ? game.elapsed() : 0);
    if (game) game.setActive(false);
    $('resumeGame').classList.add('hidden');
    $('chipTime').classList.add('hidden');
    $('endTitle').textContent = 'ATTEMPT ENDED';
    $('endMessage').textContent = reason === 'fullscreen-exit'
      ? 'You left full screen. This attempt has ended.'
      : 'You left the game or changed tabs. This attempt has ended.';
    $('timeoutOverlay').classList.remove('hidden');
    $('foot').textContent = 'This game attempt has ended and cannot be resumed.';
    save({ score: 0, right: 0, wrong: 0, seconds, snippetTokens: [] }, 0, true, true);
  }
  function onFinish(r) {
    if (finished) return;
    finished = true; clearInterval(tick); clearInterval(heartbeat);
    clearInterval(warningTimer); warningTimer = null;
    $('timer').textContent = fmt(r.seconds);
    $('sScore').textContent = r.score; $('sTime').textContent = fmt(r.seconds);
    $('sDetail').textContent = '· ' + r.right + ' right, ' + r.wrong + ' wrong';
    $('chipScore').classList.remove('hidden'); $('chipTime').classList.add('hidden');
    $('foot').textContent = 'Your result is final. Thank you for playing!';
    save(r, 0);
  }
  async function save(r, attempt, keepalive, forfeit, finishReason) {
    try {
      const j = await post('/api/submit', {
        id: player.id, sessionKey: gameSessionKey, snippetTokens: r.snippetTokens || [],
        forfeit: !!forfeit, finishReason,
      }, { keepalive });
      const res = j.result;
      $('sScore').textContent = res.score; $('sTime').textContent = fmt(res.seconds);
      $('sDetail').textContent = '· ' + res.right + ' right, ' + res.wrong + ' wrong';
      toast('Result saved ✓', 'ok');
    } catch (e) {
      if (e.status === 409) { toast(e.message || 'This game session is no longer active. Contact staff.', 'bad', 0); return; }
      if (e.status && e.status < 500) { toast(e.message || 'The result could not be accepted. Contact staff.', 'bad', 0); return; }
      if (attempt < 20) { toast('Could not save the result yet - retrying…', 'bad', 4000); setTimeout(() => save(r, attempt + 1, keepalive, forfeit, finishReason), 4000); }
      else toast('Could not save the result. Tell your staff member.', 'bad', 0);
    }
  }
})();
