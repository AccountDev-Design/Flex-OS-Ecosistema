// #############################################################
// ##  Flex Web Server de extremo a extremo, con un navegador de verdad
// #############################################################
//
// Arranca tests/host/build/flexweb_host (el nucleo del servidor del P4
// detras de un socket TCP real) y maneja la web desde Chromium sin cabeza,
// con tamano de movil:
//
//   emparejar con el codigo del QR · subir una foto compatible, un PNG
//   grande (se convierte a JPEG EN EL NAVEGADOR), un WAV estereo (a IMA
//   ADPCM mono), un WebM grabado aqui mismo (a AVI MJPEG) y un "MP3" como
//   original con su portada · ver cada cosa en el visor · pulsacion larga,
//   seleccionar todo y descargar un ZIP · bloquear desde el "P4" y
//   desbloquear con el PIN · olvidar el movil.
//
// Lo que queda en el disco se juzga con los analizadores DEL FIRMWARE
// (mediacheck). Cualquier error de JavaScript o violacion de la CSP en la
// pagina hace fallar la prueba.
//
// Necesita Playwright (npm) y su Chromium. Si no estan, la prueba dice que
// se OMITE (no que pasa) y sale con 0.
'use strict';
const fs = require('fs');
const os = require('os');
const path = require('path');
const cp = require('child_process');
const zlib = require('zlib');
const readline = require('readline');

const ROOT = path.join(__dirname, '..', '..');
const BUILD = path.join(ROOT, 'tests', 'host', 'build');
const HOST = process.env.FLEXWEB_HOST || path.join(BUILD, 'flexweb_host');
const MC = process.env.MEDIACHECK || path.join(BUILD, 'mediacheck');
const FX = require(path.join(ROOT, 'FlexOS_Ultra', 'webui', 'app.js'));

function loadPlaywright() {
  try { return require('playwright'); } catch (e) { /* global */ }
  try { return require(path.join(cp.execSync('npm root -g', { encoding: 'utf8' }).trim(), 'playwright')); } catch (e) { return null; }
}
const pw = loadPlaywright();
if (!pw) { console.log('=== e2e OMITIDA: no hay Playwright en esta maquina (no cuenta como pasada) ==='); process.exit(0); }

let run = 0, fail = 0;
function check(cond, msg) { run++; if (!cond) { fail++; console.log('  FALLO: ' + msg); } }
let step = 'inicio';
function section(t) { step = t; console.log('-- ' + t + ' --'); }
function mark(t) { step = t; if (process.env.E2E_TRACE) console.log('   · ' + t); }
// E2E_SHOTS=1 deja capturas de cada paso en tests/host/build/e2e_*.png (para revisar el aspecto).
let shotPage = null;
async function shot(name) { if (process.env.E2E_SHOTS && shotPage) await shotPage.screenshot({ path: path.join(BUILD, 'e2e_' + name + '.png') }); }
function mc(...args) { return JSON.parse(cp.execFileSync(MC, args, { encoding: 'utf8' }).trim().split('\n').pop()); }
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

const cat = (...parts) => Buffer.concat(parts.map((p) => Buffer.isBuffer(p) ? p : typeof p === 'string' ? Buffer.from(p, 'latin1') : Buffer.from(p)));
const be32 = (v) => { const b = Buffer.alloc(4); b.writeUInt32BE(v >>> 0); return b; };
const le16 = (v) => { const b = Buffer.alloc(2); b.writeUInt16LE(v); return b; };
const le32 = (v) => { const b = Buffer.alloc(4); b.writeUInt32LE(v >>> 0); return b; };

function png(w, h) {
  const raw = Buffer.alloc((w * 3 + 1) * h);
  for (let y = 0; y < h; y++) {
    const o = y * (w * 3 + 1);
    for (let x = 0; x < w; x++) {
      raw[o + 1 + x * 3] = (x * 255 / w) | 0;
      raw[o + 2 + x * 3] = (y * 255 / h) | 0;
      raw[o + 3 + x * 3] = ((x >> 4) ^ (y >> 4)) & 1 ? 220 : 40;
    }
  }
  const chunk = (t, d) => cat(be32(d.length), t, d, be32(FX.crc32(cat(t, d))));
  return cat([0x89], 'PNG\r\n\x1a\n', chunk('IHDR', cat(be32(w), be32(h), [8, 2, 0, 0, 0])),
    chunk('IDAT', zlib.deflateSync(raw)), chunk('IEND', Buffer.alloc(0)));
}
function wavStereo(sec, rate) {
  const n = sec * rate, d = Buffer.alloc(n * 4);
  for (let i = 0; i < n; i++) {
    d.writeInt16LE(Math.round(9000 * Math.sin(2 * Math.PI * 440 * i / rate)), i * 4);
    d.writeInt16LE(Math.round(9000 * Math.sin(2 * Math.PI * 660 * i / rate)), i * 4 + 2);
  }
  return cat('RIFF', le32(36 + d.length), 'WAVEfmt ', le32(16), le16(1), le16(2), le32(rate), le32(rate * 4), le16(4), le16(16),
    'data', le32(d.length), d);
}

(async () => {
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'flexweb-e2e-'));
  const disk = path.join(tmp, 'p4');
  const host = cp.spawn(HOST, [disk, String(10 * 1024 * 1024)], { stdio: ['pipe', 'pipe', 'pipe'] });
  const lines = readline.createInterface({ input: host.stdout });
  const waiting = [];
  const outQ = [];
  lines.on('line', (l) => { if (waiting.length) waiting.shift()(l); else outQ.push(l); });
  const nextLine = () => new Promise((r) => { if (outQ.length) r(outQ.shift()); else waiting.push(r); });
  const events = [];
  readline.createInterface({ input: host.stderr }).on('line', (l) => { try { events.push(JSON.parse(l)); } catch (e) { /* ASan, etc. */ console.log('  [host] ' + l); } });
  const hello = JSON.parse(await nextLine());
  const base = 'http://127.0.0.1:' + hello.port;
  const cmd = async (c) => { host.stdin.write(c + '\n'); return nextLine(); };
  const dump = async () => JSON.parse(await cmd('dump')).items;
  // Espera a que la pagina termine sus transferencias (ninguna tarjeta activa
  // ni en cola) o a que el catalogo tenga `want` elementos. Tope: 3 min.
  const settle = async (want) => {
    let items = [];
    for (let i = 0; i < 360; i++) {
      await sleep(500);
      items = await dump();
      const busy = await page.evaluate(() => !!document.querySelector('.xfer.queue') ||
        Array.from(document.querySelectorAll('.xfer')).some((x) => !x.hidden && !x.classList.contains('done') && !x.classList.contains('fail')));
      if (items.length >= want || (!busy && i > 2)) break;
    }
    return items;
  };

  const browser = await pw.chromium.launch();
  // Vigilante: una prueba colgada dice DONDE se colgo y deja una captura.
  const watchdog = setTimeout(async () => {
    console.log('  FALLO: la prueba lleva 4 min parada en: ' + step);
    try { await page.screenshot({ path: path.join(BUILD, 'e2e_colgada.png') }); } catch (e) { /* nada */ }
    process.exit(1);
  }, 240000);
  const ctx = await browser.newContext({ viewport: { width: 390, height: 844 }, deviceScaleFactor: 2, acceptDownloads: true, locale: 'es-ES' });
  const page = await ctx.newPage();
  shotPage = page;
  const errors = [], csp = [];
  page.on('pageerror', (e) => errors.push(String(e)));
  page.on('console', (m) => {
    if (m.type() !== 'error') return;
    const t = m.text();
    if (/Content Security Policy|Refused to/.test(t)) csp.push(t);
    else if (!/Failed to load resource/.test(t)) errors.push(t);
  });

  try {
    // =========================================================
    section('emparejar con el codigo del QR');
    await page.goto(base + '/?k=' + hello.code);
    await page.waitForSelector('#libView:not([hidden])', { timeout: 10000 });
    check(!page.url().includes('k='), 'el codigo desaparece de la direccion (historial)');
    check(await page.waitForSelector('#empty:not([hidden])', { timeout: 5000 }).then(() => true, () => false), 'biblioteca vacia con su mensaje');
    await shot('1_vacia');
    check((await page.textContent('#meterTxt')).includes('libres'), 'medidor de memoria: ' + await page.textContent('#meterTxt'));
    await page.evaluate(() => {
      window.__xfer = [];
      new MutationObserver(() => {
        for (const x of document.querySelectorAll('.xfer')) { const t = x.textContent; if (window.__xfer[window.__xfer.length - 1] !== t) window.__xfer.push(t); }
      }).observe(document.getElementById('xfers'), { subtree: true, childList: true, characterData: true });
    });

    // =========================================================
    section('subir y convertir en el navegador (perfil Recomendado)');
    const photo = fs.readFileSync((() => { const p = path.join(tmp, 'foto.jpg'); mc('jpeg', '640', '480', '4', p); return p; })());
    const big = png(2400, 1800);
    const wav = wavStereo(3, 44100);
    mark('grabando WebM en la pagina');
    const webmB64 = await page.evaluate(async () => {
      const c = document.createElement('canvas');
      c.width = 320; c.height = 240;
      const g = c.getContext('2d');
      const rec = new MediaRecorder(c.captureStream(30), { mimeType: 'video/webm;codecs=vp8' });
      const parts = [];
      rec.ondataavailable = (e) => parts.push(e.data);
      rec.start(100);
      const t0 = performance.now();
      await new Promise((res) => {
        (function draw() {
          const t = (performance.now() - t0) / 1000;
          g.fillStyle = 'hsl(' + ((t * 160) | 0) + ',70%,45%)'; g.fillRect(0, 0, 320, 240);
          g.fillStyle = '#fff'; g.fillRect(20 + t * 110, 90, 60, 60);
          if (t < 2.2) requestAnimationFrame(draw); else res();
        })();
      });
      rec.stop();
      await new Promise((r) => { rec.onstop = r; });
      const b = new Uint8Array(await new Blob(parts).arrayBuffer());
      let s = '';
      for (let i = 0; i < b.length; i++) s += String.fromCharCode(b[i]);
      return btoa(s);
    });
    const webm = Buffer.from(webmB64, 'base64');
    check(webm.length > 1000 && FX.sniff(new Uint8Array(webm)) === 'webm', 'WebM grabado en la pagina (' + webm.length + ' bytes)');
    mark('eligiendo archivos');
    await page.setInputFiles('#picker', [
      { name: 'foto.jpg', mimeType: 'image/jpeg', buffer: photo },
      { name: 'grande.png', mimeType: 'image/png', buffer: big },
      { name: 'tono.wav', mimeType: 'audio/wav', buffer: wav },
      { name: 'clip.webm', mimeType: 'video/webm', buffer: webm },
    ]);
    await page.waitForSelector('#planGo:not([disabled])', { timeout: 20000 });
    await shot('2_plan');
    const planTxt = await page.$$eval('#planList li', (li) => li.map((x) => x.textContent));
    check(planTxt.length === 4, 'plan con 4 archivos');
    check(/Ya es compatible/.test(planTxt[0]), 'JPEG 640x480: sin tocar · ' + planTxt[0]);
    check(/PNG → JPEG · 1600×1200 \(desde 2400×1800\)/.test(planTxt[1]), 'PNG: a JPEG 1600x1200 · ' + planTxt[1]);
    check(/WAV IMA ADPCM · 22,05 kHz mono/.test(planTxt[2]), 'WAV estereo: a IMA mono · ' + planTxt[2]);
    check(/AVI MJPEG · 320×240 · 12 fps/.test(planTxt[3]), 'WebM: a AVI MJPEG · ' + planTxt[3]);
    check((await page.textContent('#planGo')) === 'Subir 4', 'boton "Subir 4"');
    mark('subiendo');
    await page.click('#planGo');
    await sleep(1200);
    await shot('3_transferencia');
    let items = await settle(4);
    const failTxt = await page.$$eval('.xfer.fail', (x) => x.map((e) => e.textContent));
    check(failTxt.length === 0, 'ninguna transferencia fallida ' + failTxt.join(' | '));
    check(items.length === 4, '4 elementos en el catalogo del servidor (' + items.length + ')');
    const byName = (re) => items.find((it) => re.test(it.fn));
    const iPhoto = byName(/^foto/), iBig = byName(/^grande/), iWav = byName(/^tono/), iVid = byName(/^clip/);
    check(iPhoto && iPhoto.fmt === 'JPEG' && iPhoto.w === 640 && iPhoto.p === 1 && iPhoto.k === 'photo', 'foto tal cual: JPEG 640x480 reproducible');
    check(iBig && iBig.fmt === 'JPEG' && iBig.w === 1600 && iBig.h === 1200 && iBig.p === 1 && iBig.fn === 'grande.jpg', 'PNG convertido: grande.jpg 1600x1200');
    check(iWav && iWav.k === 'audio' && iWav.fmt === 'WAV IMA ADPCM' && iWav.p === 1 && Math.abs(iWav.d - 3000) <= 50 && iWav.n === 'tono',
      'WAV: IMA ADPCM de 3 s en Musica, titulo "tono"');
    check(iVid && iVid.k === 'video' && iVid.fmt === 'AVI MJPEG' && iVid.p === 1 && iVid.w === 320 && iVid.h === 240 && iVid.d >= 1900 && iVid.d <= 2400,
      'WebM convertido: AVI MJPEG 320x240 de ~2 s (' + (iVid && iVid.d) + ' ms)');
    check([iPhoto, iBig, iVid].every((it) => it && it.t != null) && iWav && iWav.t == null,
      'fotos y video con miniatura hecha por el P4; el audio sin portada, sin ella');
    const onDisk = (it) => path.join(disk, it ? it.path || '' : '');
    // El catalogo del volcado no trae la ruta: se busca en las carpetas.
    const find = (dir, re) => { const d = path.join(disk, dir); const f = fs.readdirSync(d).find((x) => re.test(x)); return f ? path.join(d, f) : null; };
    const jpgBig = find('Imagenes', /^grande.*\.jpg$/);
    const t = jpgBig && mc('thumb', jpgBig);
    check(t && t.ok === 1 && t.w === 1600 && t.h === 1200, 'en disco: el JPEG del navegador lo decodifica el P4 entero');
    const wavF = find('Musica', /^tono.*\.wav$/);
    const w = wavF && mc('wav', wavF, path.join(tmp, 'w.raw'));
    check(w && w.ok === 1 && w.format === 17 && w.rate === 22050 && w.ch === 1 && Math.abs(w.dur - 3000) <= 50, 'en disco: WAV IMA 22050 mono de 3 s');
    const aviF = find('Videos', /^clip.*\.avi$/);
    const a = aviF && mc('avi', aviF);
    check(a && a.ok === 1 && a.frames > 20 && a.decoded === a.frames && a.bad === 0 && a.w === 320, 'en disco: AVI con ' + (a && a.frames) + ' fotogramas, todos decodificables');
    check(fs.readdirSync(path.join(disk, 'System', 'Media', 'tmp')).length === 0, 'ningun temporal olvidado');
    const ev = (n) => events.filter((e) => e.ev === n).length;
    check(ev(1) === 4 && ev(4) === 4 && ev(3) === 4 && ev(2) >= 4, 'la pantalla del P4 recibe inicio, progreso, comprobacion y fin de cada subida');
    void onDisk;
    const log = await page.evaluate(() => window.__xfer);
    check(log.some((x) => /Convirtiendo en el móvil/.test(x)) && log.some((x) => /Enviando .* de .* %/.test(x)) &&
      log.some((x) => /Flex OS está comprobando/.test(x)), 'tarjetas con progreso real: convertir, enviar, comprobar');
    check(log.some((x) => /Guardado en Galería y Multimedia/.test(x)) && log.some((x) => /Guardado en Música/.test(x)), 'fotos/videos a Galeria y Multimedia, audio a Musica');

    // =========================================================
    section('original con portada (perfil Original)');
    const cover = fs.readFileSync((() => { const p = path.join(tmp, 'cov.jpg'); mc('jpeg', '120', '120', '9', p); return p; })());
    const apic = cat([0], 'image/jpeg', [0, 3, 0], cover);
    const tit = cat([3], Buffer.from('Canción original', 'utf8'));
    const f23 = (id, p) => cat(id, be32(p.length), [0, 0], p);
    const tag = cat(f23('TIT2', tit), f23('TPE1', cat([3], 'Grupo')), f23('APIC', apic));
    const ss = (n) => Buffer.from([(n >> 21) & 127, (n >> 14) & 127, (n >> 7) & 127, n & 127]);
    const mp3 = cat('ID3', [3, 0, 0], ss(tag.length), tag, Buffer.alloc(3000, 0x55));
    await page.click('#fab');
    await page.setInputFiles('#picker', [{ name: 'cancion.mp3', mimeType: 'audio/mpeg', buffer: mp3 }]);
    await page.waitForSelector('#planList li:not(.busy)', { timeout: 20000 });
    const recTxt = await page.textContent('#planList li');
    check(/navegador no puede abrir/.test(recTxt) && await page.isDisabled('#planGo'), 'en Recomendado: el navegador no puede con este MP3 y se dice');
    await page.click('#profiles button[data-p="orig"]');
    const origTxt = await page.textContent('#planList li');
    check(/Se guardará tal cual: Flex OS no puede reproducir MP3/.test(origTxt) && !(await page.isDisabled('#planGo')), 'en Original: se guardara y se avisa');
    await page.click('#planGo');
    items = await settle(5);
    let iMp3 = items.find((it) => it.fmt === 'MP3');
    for (let i = 0; i < 40 && iMp3 && iMp3.t == null; i++) { await sleep(250); iMp3 = (await dump()).find((it) => it.fmt === 'MP3'); }
    check(iMp3 && iMp3.p === 0 && iMp3.n === 'Canción original' && iMp3.ar === 'Grupo', 'MP3 guardado como original, con titulo y artista de su ID3');
    check(iMp3 && iMp3.t != null, 'con la portada del ID3 como miniatura (la aporto el navegador)');
    const log2 = await page.evaluate(() => window.__xfer);
    check(log2.some((x) => /Guardado para descargar · MP3/.test(x)), 'la tarjeta explica que solo se puede descargar');
    items = await dump();

    // =========================================================
    section('biblioteca y visor');
    const cardOf = (id) => '#grid .card[data-id="' + id + '"]';
    await page.click('#menuBtn');
    await page.click('#mRefresh');
    await page.waitForFunction(() => document.querySelectorAll('#grid .card').length === 5);
    await page.waitForFunction(() => document.querySelectorAll('#grid .card img.ok').length === 4, null, { timeout: 10000 });
    await shot('4_biblioteca');
    check(await page.$(cardOf(iWav.id) + ' .wave') !== null, '5 tarjetas: 4 miniaturas cargadas y el audio sin portada con su onda');
    await page.click(cardOf(iBig.id));
    await page.waitForFunction(() => { const i = document.querySelector('#viewerBody img'); return i && i.naturalWidth > 0; });
    check(await page.$eval('#viewerBody img', (i) => i.naturalWidth) === 1600, 'visor: la foto convertida a tamano real');
    await shot('5_visor');
    check(/JPEG · 1600×1200/.test(await page.textContent('#viewerInfo')), 'visor: formato y tamano');
    await page.click('#viewerClose');
    await page.waitForSelector('#viewer', { state: 'hidden' });
    await page.click(cardOf(iVid.id));
    await page.waitForSelector('#viewerBody canvas', { timeout: 10000 });
    await sleep(400);
    check(await page.$eval('#viewerBody canvas', (c) => c.width) === 320 && (await page.textContent('#viewerBody .vctl')) === 'Pausa',
      'visor: el AVI MJPEG se reproduce en el navegador');
    await page.click('#viewerClose');
    await page.click(cardOf(iWav.id));
    await page.waitForSelector('#viewerBody .vctl', { timeout: 10000 });
    await page.click('#viewerBody .vctl');
    check((await page.textContent('#viewerBody .vctl')) === 'Detener', 'visor: el WAV IMA se decodifica y suena en la pagina');
    await page.click('#viewerClose');
    await page.click('#tabs button[data-tab="audio"]');
    check(await page.$$eval('#grid .card', (c) => c.length) === 2, 'pestana Musica: los 2 audios');
    await page.click('#tabs button[data-tab="all"]');

    // =========================================================
    section('pulsacion larga, seleccionar todo y ZIP');
    const box = await page.$eval(cardOf(iPhoto.id), (c) => { const r = c.getBoundingClientRect(); return { x: r.x + r.width / 2, y: r.y + r.height / 2 }; });
    await page.mouse.move(box.x, box.y);
    await page.mouse.down();
    await sleep(650);
    await page.mouse.up();
    await page.waitForSelector('#selbar:not([hidden])');
    check((await page.textContent('#selCount')) === '1 seleccionado', 'pulsacion larga: modo seleccion con 1');
    check(!(await page.isVisible('#viewer')), 'la pulsacion larga no abre el visor');
    await page.click('#selAll');
    check((await page.textContent('#selCount')) === '5 seleccionados' && (await page.textContent('#selAll')) === 'Quitar selección', 'seleccionar todo: 5, y el boton cambia');
    await page.click('#selAll');
    check((await page.textContent('#selCount')) === '0 seleccionados' && await page.isDisabled('#selDl'), 'quitar seleccion: 0 y sin descarga');
    await page.click('#selAll');
    await shot('6_seleccion');
    const [dl] = await Promise.all([page.waitForEvent('download'), page.click('#selDl')]);
    const zipPath = path.join(tmp, 'descarga.zip');
    await dl.saveAs(zipPath);
    const names = cp.execFileSync('python3', ['-c', 'import zipfile,sys;z=zipfile.ZipFile(sys.argv[1]);assert z.testzip() is None;print("|".join(sorted(z.namelist())))', zipPath], { encoding: 'utf8' }).trim();
    check(names === 'cancion.mp3|clip.avi|foto.jpg|grande.jpg|tono.wav', 'ZIP valido con los 5 y sus nombres: ' + names);
    check(/Flex OS \(5 archivos\)\.zip/.test(dl.suggestedFilename()), 'nombre del ZIP: ' + dl.suggestedFilename());
    check(!(await page.isVisible('#selbar')), 'tras descargar se sale de la seleccion');

    // =========================================================
    section('bloqueado desde el P4, desbloqueado con el PIN del sistema');
    const pubFile = find('Imagenes', /^foto.*\.jpg$/);
    const lockRes = JSON.parse(await cmd('lock ' + iPhoto.id));
    const lockedFile = path.join(disk, 'System', 'Media', 'Protegido', iPhoto.id + '.jpg');
    check(lockRes.ok === 1 && lockRes.path === '/System/Media/Protegido/' + iPhoto.id + '.jpg', 'el P4 bloquea la foto: ' + JSON.stringify(lockRes));
    check(pubFile && !fs.existsSync(pubFile) && fs.existsSync(lockedFile) && fs.readFileSync(lockedFile).equals(photo),
      'en disco: el original, intacto, sale de /Imagenes a la carpeta protegida con nombre neutro');
    check(!fs.existsSync(path.join(disk, 'System', 'Media', 'th', iPhoto.id + '.jpg')) &&
      fs.existsSync(path.join(disk, 'System', 'Media', 'Protegido', iPhoto.id + '.t.jpg')), 'y su miniatura sale de la carpeta publica');
    const nBefore = (await dump()).length;
    check(JSON.parse(await cmd('scan')).n === nBefore, 'la reconciliacion no lo duplica ni lo pierde');
    await page.click('#menuBtn');
    await page.click('#mRefresh');
    await page.waitForSelector(cardOf(iPhoto.id) + '.locked');
    check(await page.$(cardOf(iPhoto.id) + ' img') === null && /Protegido/.test(await page.textContent(cardOf(iPhoto.id))), 'solo candado: ni miniatura ni nombre');
    const pub = await page.evaluate(async (id) => (await (await fetch('/api/library')).json()).items.find((i) => i.id === id), iPhoto.id);
    check(JSON.stringify(pub) === JSON.stringify({ id: iPhoto.id, lock: 1 }), 'la API solo dice que existe: ' + JSON.stringify(pub));
    const th = await page.evaluate(async (id) => (await fetch('/api/thumb/' + id)).status, iPhoto.id);
    check(th === 403, 'su miniatura: 403');
    await page.click(cardOf(iPhoto.id));
    await page.waitForSelector('#loginDlg:not([hidden])');
    check(/PIN/.test(await page.textContent('#loginText')), 'tocarla pide el PIN de Flex OS');
    await shot('7_pin');
    await page.fill('#loginSecret', '1111');
    await page.click('#loginGo');
    await page.waitForFunction(() => /PIN incorrecto/.test(document.getElementById('loginErr').textContent));
    check(/Quedan 4 intentos/.test(await page.textContent('#loginErr')), 'PIN malo: ' + await page.textContent('#loginErr'));
    check((await page.inputValue('#loginSecret')) === '', 'el campo se vacia: la clave no se queda en la pagina');
    await page.fill('#loginSecret', '2468');
    await page.click('#loginGo');
    await page.waitForSelector('#ownerChip:not([hidden])');
    await page.waitForSelector(cardOf(iPhoto.id) + ' img.ok');
    check(await page.$(cardOf(iPhoto.id) + ' .lockb') !== null, 'con el PIN: se ve, con la marca de protegido');
    const got = await page.evaluate(async (id) => { const r = await fetch('/api/file/' + id + '?dl=1'); return { s: r.status, b: Array.from(new Uint8Array(await r.arrayBuffer())) }; }, iPhoto.id);
    check(got.s === 200 && Buffer.from(got.b).equals(photo), 'con el PIN: se descarga el original exacto desde la carpeta protegida');
    await shot('8_propietario');
    await page.click('#ownerOff');
    await page.waitForSelector(cardOf(iPhoto.id) + '.locked');
    check(await page.isHidden('#ownerChip'), '"Ocultar": vuelve a quedar solo el candado');
    const noOwner = await page.evaluate(async (id) => (await fetch('/api/file/' + id + '?dl=1')).status, iPhoto.id);
    check(noOwner === 403, 'sin el PIN, su contenido: 403');
    const unl = JSON.parse(await cmd('unlock ' + iPhoto.id));
    check(unl.ok === 1 && unl.path === '/Imagenes/foto.jpg' && fs.readFileSync(path.join(disk, 'Imagenes', 'foto.jpg')).equals(photo) &&
      !fs.existsSync(lockedFile), 'desbloquear desde el P4: vuelve a /Imagenes con su nombre: ' + JSON.stringify(unl));
    check(fs.existsSync(path.join(disk, 'System', 'Media', 'th', iPhoto.id + '.jpg')), 'con su miniatura otra vez en la carpeta publica');

    // =========================================================
    section('olvidar este movil');
    await page.click('#menuBtn');
    await page.click('#mUnpair');
    await page.waitForSelector('#pairView:not([hidden])');
    const st = await page.evaluate(async () => (await fetch('/api/library')).status);
    check(st === 401, 'sin sesion: la biblioteca pide el codigo (401)');
  } catch (e) {
    check(false, 'excepcion en la prueba: ' + (e && e.stack || e));
    try { await page.screenshot({ path: path.join(BUILD, 'e2e_fallo.png') }); console.log('  captura: tests/host/build/e2e_fallo.png'); } catch (x) { /* nada */ }
  }
  check(errors.length === 0, 'sin errores de JavaScript en la pagina ' + errors.join(' | '));
  check(csp.length === 0, 'sin violaciones de la CSP ' + csp.join(' | '));
  clearTimeout(watchdog);
  await browser.close();
  host.stdin.end();
  await new Promise((r) => host.on('exit', r));
  fs.rmSync(tmp, { recursive: true, force: true });
  console.log('=== e2e: ' + run + ' comprobaciones, ' + fail + ' fallos ===');
  process.exit(fail ? 1 : 0);
})();
