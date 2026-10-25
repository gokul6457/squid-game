/* Campus Quest - browser runner: WebGL renderer + input + loader for game.wasm
   The game itself is the C game compiled to WebAssembly. This file draws what the game asks for. */
(function () {
  'use strict';
  const LW = 1280, LH = 720;                       // the game's own screen size
  const FONT = window.DEBUG_FONT;                  // 190 glyphs x 8 bytes

  window.CampusGame = async function start(canvas, hooks) {
    const gl = canvas.getContext('webgl', { antialias: false, alpha: false, preserveDrawingBuffer: false });
    if (!gl) throw new Error('WebGL is not available in this browser.');

    /* ---------- tiny batched renderer ---------- */
    const vs = 'attribute vec2 p;attribute vec4 c;uniform vec2 u;varying vec4 v;void main(){v=c;gl_Position=vec4(p.x/u.x*2.0-1.0,1.0-p.y/u.y*2.0,0.0,1.0);}';
    const fs = 'precision mediump float;varying vec4 v;void main(){gl_FragColor=v;}';
    function sh(t, s) { const o = gl.createShader(t); gl.shaderSource(o, s); gl.compileShader(o); return o; }
    const prog = gl.createProgram();
    gl.attachShader(prog, sh(gl.VERTEX_SHADER, vs)); gl.attachShader(prog, sh(gl.FRAGMENT_SHADER, fs));
    gl.linkProgram(prog); gl.useProgram(prog);
    const MAXV = 120000, buf = new Float32Array(MAXV * 6);
    let nv = 0;
    const vbo = gl.createBuffer(); gl.bindBuffer(gl.ARRAY_BUFFER, vbo);
    gl.bufferData(gl.ARRAY_BUFFER, buf.byteLength, gl.DYNAMIC_DRAW);
    const lp = gl.getAttribLocation(prog, 'p'), lc = gl.getAttribLocation(prog, 'c');
    gl.enableVertexAttribArray(lp); gl.vertexAttribPointer(lp, 2, gl.FLOAT, false, 24, 0);
    gl.enableVertexAttribArray(lc); gl.vertexAttribPointer(lc, 4, gl.FLOAT, false, 24, 8);
    gl.uniform2f(gl.getUniformLocation(prog, 'u'), LW, LH);
    gl.enable(gl.BLEND); gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);

    let cr = 0, cg = 0, cb = 0, ca = 1, sx = 1, sy = 1, clipOn = false, clip = [0, 0, 0, 0];
    function flush() {
      if (!nv) return;
      gl.bufferSubData(gl.ARRAY_BUFFER, 0, buf.subarray(0, nv * 6));
      gl.drawArrays(gl.TRIANGLES, 0, nv); nv = 0;
    }
    function vert(x, y, r, g, b, a) {
      if (nv >= MAXV) flush();
      const o = nv * 6; buf[o] = x; buf[o + 1] = y; buf[o + 2] = r; buf[o + 3] = g; buf[o + 4] = b; buf[o + 5] = a; nv++;
    }
    function quad(x0, y0, x1, y1) {                 // already in screen pixels
      vert(x0, y0, cr, cg, cb, ca); vert(x1, y0, cr, cg, cb, ca); vert(x0, y1, cr, cg, cb, ca);
      vert(x1, y0, cr, cg, cb, ca); vert(x1, y1, cr, cg, cb, ca); vert(x0, y1, cr, cg, cb, ca);
    }
    function applyClip() {
      flush();
      if (!clipOn) { gl.disable(gl.SCISSOR_TEST); return; }
      const k = canvas.width / LW, kv = canvas.height / LH;
      gl.enable(gl.SCISSOR_TEST);
      gl.scissor(Math.round(clip[0] * k), Math.round(canvas.height - (clip[1] + clip[3]) * kv), Math.round(clip[2] * k), Math.round(clip[3] * kv));
    }
    function glyphIndex(c) {
      if (c <= 32 || (c >= 127 && c <= 160)) return -1;
      if (c >= 190) return 189;
      return c < 127 ? c - 33 : c - 67;
    }

    let mem = null, snippetTokens = [];
    const f32 = (p, n) => new Float32Array(mem.buffer, p, n);
    const i32 = (p, n) => new Int32Array(mem.buffer, p, n);
    function cstr(p) { const u = new Uint8Array(mem.buffer); let e = p; while (u[e]) e++; return u.subarray(p, e); }

    const env = {
      js_color(r, g, b, a) { cr = r; cg = g; cb = b; ca = a; },
      js_scale(a, b) { sx = a; sy = b; },
      js_clip(on, x, y, w, h) { clipOn = !!on; clip = [x * sx, y * sy, w * sx, h * sy]; applyClip(); },
      js_clear() { flush(); gl.clearColor(cr, cg, cb, 1); gl.clear(gl.COLOR_BUFFER_BIT); },
      js_present() { flush(); },
      js_ticks() { return performance.now(); },
      js_fillrect(x, y, w, h) { quad(x * sx, y * sy, (x + w) * sx, (y + h) * sy); },
      js_rect(x, y, w, h) {
        const X0 = x * sx, Y0 = y * sy, X1 = (x + w) * sx, Y1 = (y + h) * sy;
        quad(X0, Y0, X1, Y0 + 1); quad(X0, Y1 - 1, X1, Y1); quad(X0, Y0, X0 + 1, Y1); quad(X1 - 1, Y0, X1, Y1);
      },
      js_line(x1, y1, x2, y2) {
        let dx = (x2 - x1) * sx, dy = (y2 - y1) * sy; const l = Math.hypot(dx, dy) || 1;
        const nx = -dy / l * 0.5, ny = dx / l * 0.5, ax = x1 * sx, ay = y1 * sy, bx = x2 * sx, by = y2 * sy;
        vert(ax + nx, ay + ny, cr, cg, cb, ca); vert(bx + nx, by + ny, cr, cg, cb, ca); vert(ax - nx, ay - ny, cr, cg, cb, ca);
        vert(bx + nx, by + ny, cr, cg, cb, ca); vert(bx - nx, by - ny, cr, cg, cb, ca); vert(ax - nx, ay - ny, cr, cg, cb, ca);
      },
      js_geometry(vp, n, ip, k) {                   // SDL_Vertex = 8 floats: x y r g b a u v
        const v = f32(vp, n * 8), ix = ip ? i32(ip, k) : null, cnt = ip ? k : n;
        for (let i = 0; i < cnt; i++) {
          const j = (ix ? ix[i] : i) * 8;
          vert(v[j] * sx, v[j + 1] * sy, v[j + 2], v[j + 3], v[j + 4], v[j + 5]);
        }
      },
      js_text(x, y, sp) {
        const s = cstr(sp); let cx = x;
        for (let i = 0; i < s.length; i++, cx += 8) {
          const g = glyphIndex(s[i]); if (g < 0) continue;
          for (let iy = 0; iy < 8; iy++) {
            const row = FONT[g * 8 + iy]; let ix = 0;
            while (ix < 8) {
              if (row & (1 << ix)) { let e = ix; while (e < 8 && (row & (1 << e))) e++;
                quad((cx + ix) * sx, (y + iy) * sy, (cx + e) * sx, (y + iy + 1) * sy); ix = e; } else ix++;
            }
          }
        }
      },
      js_started() { hooks.onStart && hooks.onStart(); },
      js_report(score, right, wrong, seconds, idsPtr, count) {
        const snippetIds = Array.from(i32(idsPtr, count));
        hooks.onFinish && hooks.onFinish({
          score, right, wrong, seconds,
          snippetTokens: snippetIds.map((id) => snippetTokens[id]),
        });
      },
    };

    const bytes = await (await fetch('game/game.wasm?v=' + (hooks.version || '1'))).arrayBuffer();
    const mod = await WebAssembly.compile(bytes);
    const imports = { env, wasi_snapshot_preview1: {} };
    for (const im of WebAssembly.Module.imports(mod)) {
      if (im.module === 'wasi_snapshot_preview1') imports.wasi_snapshot_preview1[im.name] = () => 0;
    }
    const inst = await WebAssembly.instantiate(mod, imports);
    const x = inst.exports; mem = x.memory;
    x._initialize && x._initialize();
    x.game_init();

    /* ---------- canvas size (sharp on big / retina screens) ---------- */
    function fit() {
      const r = canvas.getBoundingClientRect(), d = Math.min(window.devicePixelRatio || 1, 2);
      const w = Math.max(320, Math.round(r.width * d)), h = Math.round(w * LH / LW);
      if (canvas.width !== w || canvas.height !== h) { canvas.width = w; canvas.height = h; }
      gl.viewport(0, 0, canvas.width, canvas.height);
    }
    new ResizeObserver(fit).observe(canvas); fit();

    /* ---------- keyboard / mouse ---------- */
    const KEYMAP = { Enter: 13, NumpadEnter: 1005, Escape: 27, Space: 32, Backspace: 8, Delete: 127, ArrowUp: 1001, ArrowDown: 1002, ArrowLeft: 1003, ArrowRight: 1004 };
    const SCAN = { ArrowUp: 82, ArrowDown: 81, ArrowLeft: 80, ArrowRight: 79, ShiftLeft: 225, ShiftRight: 229, Minus: 45, Equal: 46, NumpadSubtract: 45, NumpadAdd: 46 };
    function codes(e) {
      let key = KEYMAP[e.code] || 0, scan = SCAN[e.code] || 0;
      const m = /^Key([A-Z])$/.exec(e.code);
      if (m) { key = m[1].toLowerCase().charCodeAt(0); scan = 4 + m[1].charCodeAt(0) - 65; }
      return [key, scan];
    }
    let active = true;
    function onKey(down) {
      return function (e) {
        if (!active) return;
        const t = e.target && e.target.tagName;
        if (t === 'INPUT' || t === 'TEXTAREA' || t === 'SELECT') return;          // typing in a form: not for the game
        const [key, scan] = codes(e);
        if (!key && !scan) return;
        if (down && e.code === 'Escape') {
          e.preventDefault();
          if (hooks.onViolation) hooks.onViolation('escape');
          return;
        }
        if (e.ctrlKey || e.metaKey || e.altKey) return;
        e.preventDefault();
        if (down && hooks.onGesture) hooks.onGesture();
        x.shim_key(down ? 1 : 0, key, scan, e.repeat ? 1 : 0);
      };
    }
    window.addEventListener('keydown', onKey(true)); window.addEventListener('keyup', onKey(false));
    window.addEventListener('blur', () => x.shim_blur());
    canvas.addEventListener('mousedown', (e) => {
      if (!active) return;
      if (hooks.onGesture) hooks.onGesture();
      const r = canvas.getBoundingClientRect();
      x.shim_mouse((e.clientX - r.left) / r.width * LW, (e.clientY - r.top) / r.height * LH);
      canvas.focus();
    });

    /* ---------- main loop ---------- */
    let paused = false;
    function frame() { if (!paused) x.game_frame(); requestAnimationFrame(frame); }
    requestAnimationFrame(frame);
    return {
      elapsed: () => x.game_elapsed(),
      setActive(v) { active = v; paused = !v; },
      setSnippetTokens(tokens) { snippetTokens = Array.isArray(tokens) ? tokens : []; },
      canvas,
    };
  };
})();
