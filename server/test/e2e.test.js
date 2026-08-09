'use strict';
// #############################################################
//  e2e.test.js  ·  PRUEBA DE EXTREMO A EXTREMO
//  ------------------------------------------------------------
//  Levanta el servicio DE VERDAD (Chromium incluido), se conecta
//  como si fuera la placa hablando FBP/1, navega a una pagina
//  servida en local y comprueba que:
//
//    1. la autenticacion funciona y una credencial mala se rechaza;
//    2. llegan STATE y bandas FRAME con la geometria pedida;
//    3. cada banda JPEG la decodifica EL DECODIFICADOR DEL FIRMWARE
//       (tests/host/build/jpegcheck) -- no basta con que sea "un
//       JPEG valido": tiene que ser baseline y del tamano correcto,
//       que es lo unico que la placa sabe leer;
//    4. un destino interno se bloquea aunque el dispositivo insista;
//    5. el toque y el desplazamiento llegan a la pagina;
//    6. cerrar la conexion libera la sesion y el contexto.
//
//  La prueba se SALTA sola (no falla) si falta Chromium o si no se
//  ha compilado jpegcheck: asi el resto de la bateria sigue siendo
//  ejecutable en cualquier maquina.
//    make -C ../tests/host tools     # compila jpegcheck
//    FLEXBR_CHROMIUM=/ruta/chrome npm test
// #############################################################

const test = require('node:test');
const assert = require('node:assert');
const http = require('node:http');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { execFileSync } = require('node:child_process');
const { WebSocket } = require('ws');

const proto = require('../src/protocol');
const configMod = require('../src/config');
const { BrowserService } = require('../src/server');

const JPEGCHECK = path.resolve(__dirname, '../../tests/host/build/jpegcheck');
const TOKEN = 'token-de-prueba-1234567890';

function chromiumPath() {
  if (process.env.FLEXBR_CHROMIUM) return process.env.FLEXBR_CHROMIUM;
  // Rutas habituales de una instalacion de Playwright ya hecha.
  const roots = [process.env.PLAYWRIGHT_BROWSERS_PATH, '/opt/pw-browsers',
                 path.join(os.homedir(), '.cache/ms-playwright')].filter(Boolean);
  for (const r of roots) {
    let dirs = [];
    try { dirs = fs.readdirSync(r); } catch { continue; }
    for (const d of dirs.filter((x) => x.startsWith('chromium-'))) {
      const p = path.join(r, d, 'chrome-linux', 'chrome');
      if (fs.existsSync(p)) return p;
    }
  }
  return null;
}

const CHROME = chromiumPath();
const HAVE_CHECK = fs.existsSync(JPEGCHECK);
const SKIP = !CHROME ? 'no se encontro Chromium (define FLEXBR_CHROMIUM)'
                     : (!HAVE_CHECK ? 'falta tests/host/build/jpegcheck (make -C tests/host tools)' : false);

// -------------------------------------------------------------
//  Cliente FBP minimo (hace de "placa")
// -------------------------------------------------------------
class FakeDevice {
  constructor(url) {
    this.ws = new WebSocket(url);
    this.ws.binaryType = 'nodebuffer';
    this.msgs = [];
    this.waiters = [];
    this.seq = 1;
    this.ready = new Promise((res, rej) => {
      this.ws.on('open', res);
      this.ws.on('error', rej);
    });
    this.ws.on('message', (d) => {
      const m = proto.readHeader(Buffer.isBuffer(d) ? d : Buffer.from(d));
      if (!m) return;
      this.msgs.push(m);
      for (let i = this.waiters.length - 1; i >= 0; i--) {
        if (this.waiters[i].match(m)) { this.waiters[i].resolve(m); this.waiters.splice(i, 1); }
      }
    });
  }
  send(buf) { this.ws.send(buf, { binary: true }); }
  next() { this.seq = (this.seq + 1) & 0xffff; return this.seq || 1; }

  wait(match, ms = 20000) {
    const found = this.msgs.find(match);
    if (found) return Promise.resolve(found);
    return new Promise((resolve, reject) => {
      const w = { match, resolve };
      this.waiters.push(w);
      setTimeout(() => {
        const i = this.waiters.indexOf(w);
        if (i >= 0) { this.waiters.splice(i, 1); reject(new Error('tiempo agotado esperando un mensaje')); }
      }, ms).unref();
    });
  }

  hello(token, w = 480, h = 640) {
    const head = Buffer.alloc(17);
    head[0] = proto.VERSION;
    head.writeUInt16LE(w, 1);
    head.writeUInt16LE(h, 3);
    head[5] = 62; head[6] = 1;
    head.writeUInt32LE(0x2f, 7);
    head[11] = proto.IMG.JPEG;
    head[12] = 2;
    head.writeUInt32LE(192 * 1024, 13);
    const s = (x) => { const b = Buffer.from(x); const l = Buffer.alloc(2); l.writeUInt16LE(b.length, 0); return Buffer.concat([l, b]); };
    this.send(proto.frame(proto.C.HELLO, 0, this.next(), Buffer.concat([head, s(token), s('flexos-e2e')])));
  }
  navigate(url, ch = 1) {
    const b = Buffer.from(url); const l = Buffer.alloc(2); l.writeUInt16LE(b.length, 0);
    this.send(proto.frame(proto.C.NAVIGATE, ch, this.next(), Buffer.concat([l, b])));
  }
  ack() { const b = Buffer.alloc(3); this.send(proto.frame(proto.C.ACK, 0, this.next(), b)); }
  tap(x, y, ch = 1) {
    const b = Buffer.alloc(6);
    b[0] = proto.PTR.TAP; b.writeUInt16LE(x, 1); b.writeUInt16LE(y, 3); b[5] = 0;
    this.send(proto.frame(proto.C.POINTER, ch, this.next(), b));
  }
  scroll(dx, dy, ch = 1) {
    const b = Buffer.alloc(4); b.writeInt16LE(dx, 0); b.writeInt16LE(dy, 2);
    this.send(proto.frame(proto.C.SCROLL, ch, this.next(), b));
  }
  reqFrame(ch = 1) { this.send(proto.frame(proto.C.REQ_FRAME, ch, this.next(), Buffer.alloc(0))); }
  close() { try { this.ws.close(); } catch { /* ya cerrado */ } }
}

// Pagina de prueba: texto, un bloque de color y un boton que cambia el
// titulo. Sirve para comprobar el render Y que el toque llega.
const PAGE = `<!doctype html><html lang="es"><head><meta charset="utf-8">
<title>Pagina de prueba de Flex OS</title>
<style>
 body{font:16px system-ui,sans-serif;margin:0;background:#fff;color:#111}
 header{background:#1a1d24;color:#fff;padding:12px 16px;font-weight:600}
 p{margin:10px 16px;line-height:1.5}
 .bloque{height:420px;background:linear-gradient(#2f6fd8,#7fd2ff);margin:16px}
 button{margin:16px;padding:12px 18px;font-size:16px}
</style></head><body>
<header>Flex OS · pagina de prueba</header>
<p>Texto para comprobar que el render llega legible al dispositivo.</p>
<p id="eco">sin toques</p>
<button id="b" onclick="document.title='PULSADO';document.getElementById('eco').textContent='toque recibido'">Pulsar</button>
<div class="bloque"></div>
<p>Fin de la pagina, muy abajo, para poder desplazarse.</p>
<div style="height:1200px"></div>
<p id="final">FINAL</p>
</body></html>`;

test('extremo a extremo: servicio real, Chromium real, JPEG que la placa sabe leer',
  { skip: SKIP, timeout: 120000 }, async (t) => {

  // ---- pagina local ----
  const pageSrv = http.createServer((req, res) => {
    res.writeHead(200, { 'content-type': 'text/html; charset=utf-8' });
    res.end(PAGE);
  });
  await new Promise((r) => pageSrv.listen(0, '127.0.0.1', r));
  const pagePort = pageSrv.address().port;

  // ---- servicio ----
  const cfg = configMod.deepMerge(configMod.DEFAULTS, {
    port: 0,
    host: '127.0.0.1',
    devices: [{ id: 'e2e', token: TOKEN }],
    logLevel: 'warn',
    // localhost en la lista blanca: es la unica forma de que el guardian
    // deje pasar la pagina de prueba, y ademas comprueba que la lista
    // blanca funciona.
    security: { allowPrivate: false, allowHosts: ['localhost'], allowedPorts: null, blockDownloads: true },
    capture: { bandHeight: 160, minIntervalMs: 50, keyframeEveryMs: 8000, jpegQualityMin: 20, jpegQualityMax: 90 },
    browser: { executablePath: CHROME, headless: true, locale: 'es-ES', timezone: 'Europe/Madrid', args: [] }
  });

  const svc = new BrowserService(cfg);
  await svc.startBrowser();
  const server = http.createServer((req, res) => svc.handleHttp(req, res));
  const { WebSocketServer } = require('ws');
  const wss = new WebSocketServer({ server, path: '/v1/session', perMessageDeflate: false });
  wss.on('connection', (ws, req) => { svc.onConnection(ws, req).catch(() => {}); });
  await new Promise((r) => server.listen(0, '127.0.0.1', r));
  const port = server.address().port;

  t.after(async () => {
    await svc.stop();
    await new Promise((r) => server.close(r));
    await new Promise((r) => pageSrv.close(r));
  });

  // ---- 1) API de control ----
  await t.test('la API de control responde', async () => {
    const body = await new Promise((res, rej) => {
      http.get(`http://127.0.0.1:${port}/v1/health`, (r) => {
        let d = ''; r.on('data', (c) => { d += c; }); r.on('end', () => res({ code: r.statusCode, d }));
      }).on('error', rej);
    });
    assert.strictEqual(body.code, 200);
    const j = JSON.parse(body.d);
    assert.strictEqual(j.protocol, `FBP/${proto.VERSION}`);
    assert.strictEqual(j.status, 'ok');
  });

  // ---- 2) credencial invalida ----
  await t.test('una credencial invalida se rechaza', async () => {
    const dev = new FakeDevice(`ws://127.0.0.1:${port}/v1/session`);
    await dev.ready;
    dev.hello('credencial-que-no-vale-xxxxx');
    const err = await dev.wait((m) => m.type === proto.S.ERROR, 8000);
    const e = new proto.Reader(err.payload);
    assert.strictEqual(e.u16(), proto.ERR.AUTH);
    dev.close();
  });

  // ---- 3) sesion completa ----
  const dev = new FakeDevice(`ws://127.0.0.1:${port}/v1/session`);
  await dev.ready;
  dev.hello(TOKEN, 480, 640);

  await t.test('WELCOME con el viewport pedido', async () => {
    const w = await dev.wait((m) => m.type === proto.S.WELCOME, 15000);
    const r = new proto.Reader(w.payload);
    assert.strictEqual(r.u8(), proto.VERSION);
    assert.strictEqual(r.u16(), 480);
    assert.strictEqual(r.u16(), 640);
  });

  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'flexbr-e2e-'));

  await t.test('navegar entrega bandas JPEG que decodifica el firmware', async () => {
    dev.navigate(`http://localhost:${pagePort}/`);

    const frames = [];
    // Se recogen varias bandas confirmando cada una (contrapresion): sin
    // el ACK el servicio no genera la siguiente, que es justo lo que se
    // quiere comprobar.
    for (let i = 0; i < 4; i++) {
      const f = await dev.wait((m) => m.type === proto.S.FRAME && !frames.includes(m), 25000);
      frames.push(f);
      dev.ack();
      if ((f.flags & proto.FR.LAST) && i >= 1) break;
    }
    assert.ok(frames.length >= 1, 'no llego ninguna banda');

    let keyframes = 0;
    for (let i = 0; i < frames.length; i++) {
      const q = frames[i].payload;
      const x = q.readUInt16LE(0), y = q.readUInt16LE(2);
      const w = q.readUInt16LE(4), h = q.readUInt16LE(6);
      const fmt = q[8], flags = q[9];
      const jlen = q.readUInt32LE(14);
      assert.strictEqual(fmt, proto.IMG.JPEG, 'el formato debe ser JPEG');
      assert.strictEqual(x, 0);
      assert.strictEqual(w, 480, 'la banda debe cubrir el ancho del viewport');
      assert.ok(y >= 0 && y < 640, `y fuera de rango: ${y}`);
      assert.ok(h > 0 && y + h <= 640, `banda fuera del viewport: y=${y} h=${h}`);
      assert.strictEqual(q.length, 18 + jlen, 'la longitud declarada no cuadra');
      if (flags & proto.FR.KEYFRAME) keyframes++;

      const file = path.join(tmp, `band${i}.jpg`);
      fs.writeFileSync(file, q.subarray(18, 18 + jlen));

      // EL CIERRE DEL CIRCULO: lo decodifica el mismo codigo que la placa.
      const out = execFileSync(JPEGCHECK, [file], { encoding: 'utf8' }).trim();
      assert.match(out, /^OK /, `jpegcheck rechazo la banda ${i}: ${out}`);
      const [, dw, dh, comps, scale] = out.split(' ');
      assert.strictEqual(Number(dw), w, 'ancho decodificado distinto del anunciado');
      assert.strictEqual(Number(dh), h, 'alto decodificado distinto del anunciado');
      assert.strictEqual(Number(scale), 1, 'la banda deberia caber sin reducirla');
      assert.ok(Number(comps) === 3 || Number(comps) === 1);
      console.log(`      banda ${i}: y=${y} h=${h} ${jlen} B -> ${out}`);
    }
    assert.ok(keyframes >= 1, 'ninguna banda venia marcada como keyframe');
  });

  await t.test('STATE trae titulo, URL y el indicador de HTTPS', async () => {
    const st = await dev.wait((m) => {
      if (m.type !== proto.S.STATE) return false;
      const r = new proto.Reader(m.payload);
      r.u8(); r.u8();
      const title = r.str();
      return title.includes('Flex OS');
    }, 20000);
    const r = new proto.Reader(st.payload);
    const flags = r.u8();
    r.u8();
    const title = r.str();
    const url = r.str();
    assert.match(title, /Flex OS/);
    assert.match(url, new RegExp(`^http://localhost:${pagePort}/`));
    // La pagina se sirve por http, asi que debe marcarse como NO segura.
    assert.ok(flags & proto.ST.INSECURE, 'http deberia marcarse como inseguro');
    assert.ok(!(flags & proto.ST.SECURE), 'http no deberia marcarse como seguro');
  });

  await t.test('el toque llega a la pagina', async () => {
    dev.ack();
    dev.tap(60, 190);          // sobre el boton
    const st = await dev.wait((m) => {
      if (m.type !== proto.S.STATE) return false;
      const r = new proto.Reader(m.payload);
      r.u8(); r.u8();
      return r.str() === 'PULSADO';
    }, 20000);
    assert.ok(st, 'el titulo no cambio tras el toque');
  });

  await t.test('el desplazamiento llega a la pagina', async () => {
    dev.ack();
    const before = dev.msgs.length;
    dev.scroll(0, 600);
    await dev.wait((m) => m.type === proto.S.FRAME && dev.msgs.indexOf(m) >= before, 20000);
    // Si el scroll no hubiera llegado, la deteccion de bandas sucias no
    // habria producido ninguna banda nueva y esto habria caducado.
    assert.ok(true);
  });

  await t.test('un destino interno se bloquea aunque el dispositivo insista', async () => {
    dev.ack();
    for (const bad of ['http://169.254.169.254/latest/meta-data/',
                       'http://10.0.0.1/admin',
                       'file:///etc/passwd',
                       'http://127.0.0.1:1/']) {
      const before = dev.msgs.length;
      dev.navigate(bad);
      const err = await dev.wait((m) => m.type === proto.S.ERROR && dev.msgs.indexOf(m) >= before, 15000);
      const r = new proto.Reader(err.payload);
      assert.strictEqual(r.u16(), proto.ERR.BLOCKED, `${bad} deberia dar BLOCKED`);
      console.log(`      ${bad} -> ${r.str()}`);
    }
  });

  await t.test('un NAVIGATE pegado al HELLO no tumba la sesion', async () => {
    // Regresion de un fallo real en la placa. El dispositivo encola la
    // navegacion mientras aun se esta conectando, asi que el NAVIGATE
    // sale INMEDIATAMENTE detras del HELLO -- antes de que el servicio
    // haya terminado de autenticar y de abrir el contexto de Chromium.
    //
    // Con el manejador de mensajes reentrante, el segundo mensaje veia
    // `authed` todavia en false, respondia "Falta autenticacion" y
    // cerraba una sesion valida; el dispositivo reconectaba y volvia a
    // pasar lo mismo. Sintoma: el navegador se queda cargando para
    // siempre y no llega ni un frame.
    const d2 = new FakeDevice(`ws://127.0.0.1:${port}/v1/session`);
    await d2.ready;
    d2.hello(TOKEN);
    d2.navigate(`http://localhost:${pagePort}/`);     // sin esperar al WELCOME

    const w = await d2.wait((m) => m.type === proto.S.WELCOME, 15000);
    assert.ok(w, 'no llego WELCOME');
    const err = d2.msgs.find((m) => m.type === proto.S.ERROR);
    assert.ok(!err, 'el servicio respondio con un error al NAVIGATE adelantado');
    // Y la navegacion adelantada se atiende: llegan bandas.
    const f = await d2.wait((m) => m.type === proto.S.FRAME, 25000);
    assert.ok(f, 'la navegacion enviada junto al HELLO no produjo ninguna imagen');
    d2.close();
    const t0 = Date.now();
    while (svc.sessions.size > 1 && Date.now() - t0 < 5000) {
      await new Promise((r) => setTimeout(r, 50));
    }
  });

  await t.test('cerrar la conexion libera la sesion', async () => {
    assert.strictEqual(svc.sessions.size, 1);
    dev.close();
    const t0 = Date.now();
    while (svc.sessions.size > 0 && Date.now() - t0 < 5000) {
      await new Promise((r) => setTimeout(r, 50));
    }
    assert.strictEqual(svc.sessions.size, 0, 'la sesion no se libero al cerrar');
  });

  fs.rmSync(tmp, { recursive: true, force: true });
});
