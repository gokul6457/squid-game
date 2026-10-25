/* Campus Quest - admin portal (password protected; data comes from /api/admin/data) */
(function () {
  'use strict';
  const $ = (id) => document.getElementById(id);
  const fmt = (s) => {
    const n = Number(s);
    if (s == null || !Number.isFinite(n) || n < 0) return '—';
    return String(Math.floor(n / 60)).padStart(2, '0') + ':' + String(Math.floor(n % 60)).padStart(2, '0');
  };
  const when = (t) => t ? new Date(t).toLocaleString([], { day: '2-digit', month: 'short', hour: '2-digit', minute: '2-digit', second: '2-digit' }) : '—';
  let token = sessionStorage.getItem('cq_admin') || '', data = null, tab = 'reg', timer = null;

  function el(tag, cls, text) { const e = document.createElement(tag); if (cls) e.className = cls; if (text != null) e.textContent = text; return e; }

  function table(cols, rows, allowActions) {
    const box = $('tableBox'); box.innerHTML = '';
    if (!rows.length) { box.appendChild(el('div', 'empty', 'Nothing here yet.')); return; }
    const t = el('table'), th = el('thead'), hr = el('tr');
    cols.forEach((c) => hr.appendChild(el('th', c.num ? 'num' : '', c.h)));
    if (allowActions) hr.appendChild(el('th', '', 'Action'));
    th.appendChild(hr); t.appendChild(th);
    const tb = el('tbody');
    rows.forEach((r) => {
      const tr = el('tr');
      cols.forEach((c) => {
        const td = el('td', c.num ? 'num' : '');
        const v = c.get(r);
        if (c.pill) { td.appendChild(el('span', 'pill ' + v, v)); } else { td.textContent = v; }
        if (c.cls && c.cls(r)) td.className += ' ' + c.cls(r);
        tr.appendChild(td);
      });
      if (allowActions) {
        const td = el('td');
        if (r.canRetry) {
          const retry = el('button', 'resetBtn', 'Allow retry');
          retry.type = 'button';
          retry.addEventListener('click', async () => {
            if (!window.confirm('Allow one retry for ' + (r.email || r.name) + '? Ask the player to sign in again after this.')) return;
            retry.disabled = true;
            try {
              const response = await fetch('/api/admin/allow-retry', {
                method: 'POST', headers: { 'Content-Type': 'application/json', Authorization: 'Bearer ' + token },
                body: JSON.stringify({ participantId: r.id }),
              });
              const result = await response.json();
              if (!response.ok) throw new Error(result.error || 'Could not allow another attempt.');
              await load();
            } catch (e) { window.alert(e.message); retry.disabled = false; }
          });
          td.appendChild(retry);
        }
        if (r.passwordSet) {
          const reset = el('button', 'resetBtn', 'Reset password');
          reset.type = 'button';
          reset.addEventListener('click', async () => {
            if (!window.confirm('Reset the password for ' + (r.email || r.name) + '? The current password cannot be recovered.')) return;
            reset.disabled = true;
            try {
              const response = await fetch('/api/admin/reset-password', {
                method: 'POST', headers: { 'Content-Type': 'application/json', Authorization: 'Bearer ' + token },
                body: JSON.stringify({ participantId: r.id }),
              });
              const result = await response.json();
              if (!response.ok) throw new Error(result.error || 'Could not reset this password.');
              window.prompt('New password for ' + (r.email || r.name) + '. Copy it now and share it privately:', result.newPassword);
              await load();
            } catch (e) { window.alert(e.message); }
            finally { reset.disabled = false; }
          });
          td.appendChild(reset);
        }
        const button = el('button', 'removeBtn', 'Remove');
        button.type = 'button';
        button.addEventListener('click', async () => {
          const label = r.email || r.name || r.id;
          if (!window.confirm('Remove ' + label + ' and their saved game record? This cannot be undone.')) return;
          button.disabled = true;
          try {
            const response = await fetch('/api/admin/remove-player', {
              method: 'POST', headers: { 'Content-Type': 'application/json', Authorization: 'Bearer ' + token },
              body: JSON.stringify({ participantId: r.id }),
            });
            const result = await response.json();
            if (!response.ok) throw new Error(result.error || 'Could not remove this player.');
            await load();
          } catch (e) { window.alert(e.message); button.disabled = false; }
        });
        td.appendChild(button); tr.appendChild(td);
      }
      tb.appendChild(tr);
    });
    t.appendChild(tb); box.appendChild(t);
  }

  const VIEWS = {
    reg: { rows: () => data.participants, cols: [
      { h: '#', num: true, get: (r) => data.participants.indexOf(r) + 1 }, { h: 'Email', get: (r) => r.email || r.name },
      { h: 'Team', get: (r) => r.team }, { h: 'Password', get: (r) => r.passwordSet ? 'Set' : '—' },
      { h: 'Status', get: (r) => r.status, pill: true }, { h: 'Registered', get: (r) => when(r.registeredAt) },
      { h: 'Finish reason', get: (r) => ({ completed: 'Completed', forfeit: 'Forfeit', timeout: 'Time out' })[r.reason] || '—' },
      { h: 'Correct', num: true, get: (r) => r.status === 'Finished' && r.right != null ? r.right : '—' },
      { h: 'Wrong', num: true, get: (r) => r.status === 'Finished' && r.wrong != null ? r.wrong : '—' },
      { h: 'Score / 100', num: true, get: (r) => r.status === 'Finished' && r.score != null ? r.score : '—' },
      { h: 'Time played', num: true, get: (r) => fmt(r.seconds) }] },
    ind: { rows: () => data.individuals, cols: [
      { h: 'Rank', num: true, get: (r) => r.rank, cls: (r) => r.rank === 1 ? 'rank1' : '' }, { h: 'Email', get: (r) => r.email || r.name },
      { h: 'Team', get: (r) => r.team }, { h: 'Score / 100', num: true, get: (r) => r.score }, { h: 'Correct', num: true, get: (r) => r.right },
      { h: 'Wrong', num: true, get: (r) => r.wrong }, { h: 'Time taken', num: true, get: (r) => fmt(r.seconds) }] },
    team: { rows: () => data.teams, cols: [
      { h: 'Rank', num: true, get: (r) => r.rank == null ? '—' : r.rank, cls: (r) => r.rank === 1 ? 'rank1' : '' }, { h: 'Team', get: (r) => r.team },
      { h: 'Members', num: true, get: (r) => r.registered }, { h: 'Finished', num: true, get: (r) => r.finished },
      { h: 'Correct total', num: true, get: (r) => r.totalRight }, { h: 'Wrong total', num: true, get: (r) => r.totalWrong },
      { h: 'Total score', num: true, get: (r) => r.totalScore }, { h: 'Average score', num: true, get: (r) => r.avgScore },
      { h: 'Total time', num: true, get: (r) => fmt(r.totalSeconds) }, { h: 'Average time', num: true, get: (r) => fmt(r.avgSeconds) },
      { h: 'Players', get: (r) => r.members.join(', ') }] },
  };

  function render() {
    if (!data) return;
    $('apiWarning').classList.toggle('hidden', data.apiVersion === 3);
    $('cReg').textContent = data.summary.registered; $('cPlay').textContent = data.summary.playing;
    $('cFin').textContent = data.summary.finished; $('cTeams').textContent = data.summary.teams;
    $('updTime').textContent = new Date(data.generatedAt).toLocaleTimeString(); $('updated').classList.remove('hidden');
    const v = VIEWS[tab]; table(v.cols, v.rows(), tab === 'reg');
    document.querySelectorAll('.tab').forEach((b) => b.classList.toggle('on', b.dataset.tab === tab));
  }

  async function load() {
    try {
      const r = await fetch('/api/admin/data', { headers: { Authorization: 'Bearer ' + token }, cache: 'no-store' });
      if (r.status === 401) return showLogin();
      data = await r.json(); render();
    } catch (e) { /* server busy: try again on the next tick */ }
  }
  function showDash() {
    $('loginBox').classList.add('hidden'); $('dash').classList.remove('hidden'); $('logout').classList.remove('hidden');
    load(); clearInterval(timer); timer = setInterval(load, 5000);
  }
  function showLogin() {
    clearInterval(timer); token = ''; sessionStorage.removeItem('cq_admin');
    $('dash').classList.add('hidden'); $('logout').classList.add('hidden'); $('updated').classList.add('hidden'); $('loginBox').classList.remove('hidden');
    $('pw').value = ''; $('pw').focus();
  }

  $('loginForm').addEventListener('submit', async (ev) => {
    ev.preventDefault(); $('loginErr').textContent = ''; $('loginBtn').disabled = true;
    try {
      const r = await fetch('/api/admin/login', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ password: $('pw').value }) });
      const j = await r.json();
      if (!r.ok) throw new Error(j.error || 'Login failed.');
      token = j.token; sessionStorage.setItem('cq_admin', token); showDash();
    } catch (e) { $('loginErr').textContent = e.message === 'Failed to fetch' ? 'Cannot reach the server.' : e.message; }
    finally { $('loginBtn').disabled = false; }
  });
  $('logout').addEventListener('click', async () => {
    try { await fetch('/api/admin/logout', { method: 'POST', headers: { Authorization: 'Bearer ' + token } }); } catch (e) { /* ignore */ }
    showLogin();
  });
  document.querySelectorAll('.tab').forEach((b) => b.addEventListener('click', () => { tab = b.dataset.tab; render(); }));

  $('csv').addEventListener('click', () => {
    if (!data) return;
    const v = VIEWS[tab], q = (s) => '"' + String(s).replace(/"/g, '""') + '"';
    const lines = [v.cols.map((c) => q(c.h)).join(',')].concat(v.rows().map((r) => v.cols.map((c) => q(c.get(r))).join(',')));
    const blob = new Blob(['\ufeff' + lines.join('\r\n')], { type: 'text/csv;charset=utf-8' });
    const a = document.createElement('a'); a.href = URL.createObjectURL(blob);
    a.download = 'campus-quest-' + ({ reg: 'registrations', ind: 'individual-results', team: 'team-results' })[tab] + '.csv';
    document.body.appendChild(a); a.click(); a.remove();
  });

  if (token) showDash();
})();
