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
// Una "foto" para JPEG: degradados, bordes y grano. Con grano, lo que ocupa
// el JPEG depende de la calidad y de la resolucion como en una foto real.
function photoPng(w, h, seed) {
  let sd = seed >>> 0;
  const r = () => { sd = (sd * 1103515245 + 12345) >>> 0; return (sd >>> 16) & 255; };
  const raw = Buffer.alloc((w * 3 + 1) * h);
  for (let y = 0; y < h; y++) {
    const o = y * (w * 3 + 1);
    for (let x = 0; x < w; x++) {
      const base = 128 + 60 * Math.sin(x / 97 + seed) * Math.cos(y / 71);
      const edge = ((x / 150 | 0) + (y / 110 | 0)) & 1 ? 30 : -30;
      for (let c = 0; c < 3; c++) {
        const v = base + edge * (c === 1 ? 1 : 0.5) + (r() - 128) * 0.12 + c * 20;
        raw[o + 1 + x * 3 + c] = Math.max(0, Math.min(255, Math.round(v)));
      }
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
    section('tamano por archivo: «Límite máximo» (100 KB) con confirmacion');
    const KB = 1024;
    const small = fs.readFileSync((() => { const p = path.join(tmp, 'peq.jpg'); mc('jpeg', '320', '240', '7', p); return p; })());
    const noisy = photoPng(2000, 1500, 11);
    const long20 = wavStereo(20, 44100), long22 = wavStereo(22, 44100), long30 = wavStereo(30, 44100);
    const before = (await dump()).length;
    await page.click('#fab');
    await page.setInputFiles('#picker', [
      { name: 'ruido.png', mimeType: 'image/png', buffer: noisy },
      { name: 'peq.jpg', mimeType: 'image/jpeg', buffer: small },
      { name: 'veinte.wav', mimeType: 'audio/wav', buffer: long20 },
      { name: 'veintidos.wav', mimeType: 'audio/wav', buffer: long22 },
      { name: 'treinta.wav', mimeType: 'audio/wav', buffer: long30 },
      { name: 'clip2.webm', mimeType: 'video/webm', buffer: webm },
    ]);
    await page.waitForSelector('#planList li:not(.busy)', { timeout: 30000 });
    await page.click('#profiles button[data-p="rec"]');      // la seccion anterior dejo «Original»
    check(await page.isHidden('#sizeChips') && /perfil elegido/.test(await page.textContent('#sizeHint')), 'por defecto, «Sin límite»: sin opciones de tamano');
    await page.click('#sizeModes button[data-m="max"]');
    check(await page.isVisible('#sizeChips') && (await page.$$eval('#sizeChips button', (b) => b.map((x) => x.textContent))).join('|') === '500 KB|750 KB|1 MB|2 MB|5 MB|10 MB|Otro',
      'Limite maximo: 500 KB, 750 KB, 1 MB, 2 MB, 5 MB, 10 MB y Otro');
    await page.click('#sizeChips button[data-b="custom"]');
    await page.fill('#sizeNum', '20');
    await page.selectOption('#sizeUnit', 'KB');
    check(/entre 50 KB y 1024 MB/.test(await page.textContent('#sizeHint')) && await page.isDisabled('#planGo'), 'Otro = 20 KB: no vale, se dice y no se puede subir');
    await page.fill('#sizeNum', '100');
    await page.waitForFunction(() => /No superar 100 KB/.test(document.getElementById('sizeHint').textContent));
    await shot('9_tamano_plan');
    const sizePlan = await page.$$eval('#planList li', (li) => li.map((x) => x.textContent));
    mark('plan con 100 KB: ' + sizePlan.join(' | '));
    check(sizePlan.length === 6, 'plan con 6 archivos');
    check(/PNG → JPEG · \d+×\d+ \(desde 2000×1500\)/.test(sizePlan[0]) && /\d+,\d MB → ≈ \d+ KB/.test(sizePlan[0]), 'PNG: se reduce y se anuncia antes → despues · ' + sizePlan[0]);
    check(/Ya es compatible/.test(sizePlan[1]), 'JPEG pequeno: tal cual · ' + sizePlan[1]);
    check(/WAV IMA ADPCM · 8 kHz mono/.test(sizePlan[2]) && /se te preguntará/.test(sizePlan[2]), '20 s en 100 KB: 8 kHz y aviso de que se preguntara · ' + sizePlan[2]);
    check(/Ni a 8 kHz cabe en 100 KB/.test(sizePlan[4]), '30 s: no cabe ni a 8 kHz, y se dice · ' + sizePlan[4]);
    check(/AVI MJPEG · \d+×\d+ · \d+ fps/.test(sizePlan[5]), 'WebM: AVI con sus parametros · ' + sizePlan[5]);
    check((await page.textContent('#planGo')) === 'Subir 5', 'boton "Subir 5" (el de 30 s no)');
    await page.click('#planGo');
    // Los que pierden mucha calidad esperan a que se decida; los demas siguen.
    let asks = [];
    for (let i = 0; i < 360; i++) {
      await sleep(500);
      asks = await page.$$eval('.xfer.ask', (x) => x.map((e) => e.textContent));
      const busy = await page.evaluate(() => Array.from(document.querySelectorAll('.xfer')).some((x) => !x.hidden && !x.classList.contains('queue') &&
        !x.classList.contains('done') && !x.classList.contains('fail') && !x.classList.contains('ask')) || !!document.querySelector('.xfer.queue'));
      if (!busy && i > 2) break;
    }
    await shot('10_confirmar');
    mark('esperando decision: ' + asks.join(' | '));
    check(asks.length >= 2 && asks.some((t) => /veinte\.wav/.test(t)) && asks.some((t) => /veintidos\.wav/.test(t)),
      'los dos WAV a 8 kHz esperan confirmacion (' + asks.length + ')');
    check(asks.every((t) => /¿Subirlo así\?/.test(t) && /→/.test(t) && /máximo 100 KB/.test(t) && /Subir así/.test(t) && /Descartar/.test(t)),
      'cada uno dice antes → despues, el maximo, los parametros y pregunta: ' + asks[0]);
    let mid = await dump();
    check(!mid.some((it) => /^veinte/.test(it.fn)) && !mid.some((it) => /^veintidos/.test(it.fn)), 'nada se sube sin que se acepte');
    await page.click('.xfer.ask:has-text("veintidos.wav") button:has-text("Descartar")');
    await page.click('.xfer.ask:has-text("veinte.wav") button:has-text("Subir así")');
    // Si la foto o el video tambien quedaron en perdida grande, se aceptan (y se cuenta).
    for (const extra of await page.$$('.xfer.ask button:has-text("Subir así")')) await extra.click();
    items = await settle(before + 4);
    const log3 = await page.evaluate(() => window.__xfer);
    mark('subidos: ' + items.filter((it) => !mid.some((m) => m.id === it.id)).map((it) => it.fn).join(', '));
    check(items.length === before + 4 && !items.some((it) => /^veintidos/.test(it.fn)) && !items.some((it) => /^treinta/.test(it.fn)),
      '4 nuevos en Flex OS: el descartado y el imposible no (' + (items.length - before) + ')');
    const fRuido = find('Imagenes', /^ruido.*\.jpg$/), fPeq = find('Imagenes', /^peq.*\.jpg$/), fVeinte = find('Musica', /^veinte.*\.wav$/), fClip = find('Videos', /^clip2.*\.avi$/);
    const sz = (p) => p ? fs.statSync(p).size : -1;
    mark('tamanos: ruido ' + sz(fRuido) + ', peq ' + sz(fPeq) + ', veinte ' + sz(fVeinte) + ', clip2 ' + sz(fClip));
    const tR = fRuido && mc('thumb', fRuido);
    check(fRuido && sz(fRuido) <= 100 * KB && sz(fRuido) >= 60 * KB && tR.ok === 1, 'foto: ' + sz(fRuido) + ' bytes <= 100 KB, aprovechando el limite, y el P4 la decodifica');
    check(fPeq && fs.readFileSync(fPeq).equals(small), 'el JPEG que ya cabia: los mismos bytes');
    const wV = fVeinte && mc('wav', fVeinte, path.join(tmp, 'v.raw'));
    check(fVeinte && sz(fVeinte) <= 100 * KB && wV.ok === 1 && wV.rate === 8000 && wV.format === 17 && Math.abs(wV.dur - 20000) <= 60,
      'audio aceptado: IMA ADPCM a 8 kHz, 20 s enteros, ' + sz(fVeinte) + ' bytes');
    const aC = fClip && mc('avi', fClip);
    check(fClip && sz(fClip) <= 100 * KB && aC.ok === 1 && aC.decoded === aC.frames && aC.bad === 0 && aC.dur >= 1900,
      'video: ' + sz(fClip) + ' bytes <= 100 KB, ' + (aC && aC.frames) + ' fotogramas decodificables, sin recortar (' + (aC && aC.dur) + ' ms)');
    check(log3.some((x) => /ruido\.png.*MB → \d+ KB \(−\d+ %\) · máximo 100 KB · \d+×\d+ · calidad \d+ %/.test(x)),
      'la tarjeta de la foto: antes → despues (−%), el maximo y los parametros');
    check(log3.some((x) => /clip2\.webm.*→ \d+ KB .*máximo 100 KB · \d+×\d+ · \d+ fps · calidad \d+ %/.test(x)), 'y la del video, con resolucion, fps y calidad');
    check(log3.some((x) => /veintidos\.wav.*Descartado/.test(x)), 'el descartado lo dice');

    // =========================================================
    section('tamano por archivo: «Objetivo» (500 KB)');
    const noisy2 = photoPng(2000, 1500, 23);
    await page.click('#fab');
    await page.setInputFiles('#picker', [{ name: 'objetivo.png', mimeType: 'image/png', buffer: noisy2 }]);
    await page.waitForSelector('#planList li:not(.busy)', { timeout: 30000 });
    check(await page.isVisible('#sizeCustom') && (await page.inputValue('#sizeNum')) === '100', 'la eleccion anterior se recuerda (Limite 100 KB)');
    await page.click('#sizeModes button[data-m="target"]');
    await page.click('#sizeChips button[data-b="512000"]');
    check(await page.isHidden('#sizeCustom') && /unos 500 KB/.test(await page.textContent('#sizeHint')), 'Objetivo 500 KB: "intentar quedar en unos 500 KB"');
    const dis = await page.$$eval('#profiles button', (b) => b.map((x) => x.dataset.p + ':' + (x.disabled ? 1 : 0)).join(','));
    check(dis === 'light:1,rec:1,high:1,orig:0' && /la calidad la decide el tamaño/.test(await page.textContent('#profileHint')),
      'en Objetivo los perfiles de calidad no aplican (solo Original): ' + dis);
    const objPlan = await page.textContent('#planList li');
    check(/PNG → JPEG/.test(objPlan) && /≈ \d+ KB/.test(objPlan), 'plan: ' + objPlan);
    await page.click('#planGo');
    items = await settle(before + 5);
    const fObj = find('Imagenes', /^objetivo.*\.jpg$/);
    const tO = fObj && mc('thumb', fObj);
    check(fObj && sz(fObj) >= 450 * KB && sz(fObj) <= 550 * KB && tO.ok === 1, 'Objetivo 500 KB: sale ' + sz(fObj) + ' bytes (±10 %) y el P4 la decodifica');
    const log4 = await page.evaluate(() => window.__xfer);
    check(log4.some((x) => /objetivo\.png.*→ \d+ KB \(−\d+ %\) · objetivo 500 KB/.test(x)), 'tarjeta: objetivo 500 KB y el resultado');
    // =========================================================
    section('tamano por archivo: video con grano y AVI de camara (maximo 300 KB)');
    mark('grabando WebM con grano en la pagina');
    const noiseB64 = await page.evaluate(async () => {
      const c = document.createElement('canvas');
      c.width = 320; c.height = 240;
      const g = c.getContext('2d'), img = g.createImageData(320, 240);
      const rec = new MediaRecorder(c.captureStream(30), { mimeType: 'video/webm;codecs=vp8', videoBitsPerSecond: 4000000 });
      const parts = [];
      rec.ondataavailable = (e) => parts.push(e.data);
      rec.start(100);
      const t0 = performance.now();
      await new Promise((res) => {
        (function draw() {
          const t = (performance.now() - t0) / 1000;
          for (let i = 0, p = 0; i < img.data.length; i += 4, p++) {
            const x = p % 320, y = (p / 320) | 0, v = (Math.random() * 90) | 0;
            img.data[i] = (x + t * 90) % 255; img.data[i + 1] = (y * 2 + v) % 255; img.data[i + 2] = 120 + v; img.data[i + 3] = 255;
          }
          g.putImageData(img, 0, 0);
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
    const noiseWebm = Buffer.from(noiseB64, 'base64');
    const camFrames = [];
    for (let k = 0; k < 40; k++) { const p = path.join(tmp, 'c' + k + '.jpg'); mc('jpeg', '640', '480', String(k), p); camFrames.push(new Uint8Array(fs.readFileSync(p))); }
    // Dos fotogramas danados (como los de una tarjeta SD que falla): la web
    // repite el anterior en su lugar, igual que hace el P4 con los vacios.
    for (const k of [5, 17]) { const b = new Uint8Array(4000); b.set([0xFF, 0xD8]); b.set([0xFF, 0xD9], 3998); camFrames[k] = b; }
    const cam = Buffer.from(FX.concat(FX.aviMux(camFrames, 640, 480, 10)));
    const camChk = mc('avi', (() => { const p = path.join(tmp, 'cam.avi'); fs.writeFileSync(p, cam); return p; })());
    check(camChk.frames === 40 && camChk.bad === 2 && cam.length > 400 * KB,
      'AVI MJPEG "de camara": 40 fotogramas 640x480 a 10 fps, 2 danados, ' + cam.length + ' bytes');
    const before2 = (await dump()).length;
    await page.click('#fab');
    await page.setInputFiles('#picker', [
      { name: 'grano.webm', mimeType: 'video/webm', buffer: noiseWebm },
      { name: 'camara.avi', mimeType: 'video/x-msvideo', buffer: cam },
    ]);
    await page.waitForSelector('#planList li:not(.busy)', { timeout: 30000 });
    await page.click('#sizeModes button[data-m="max"]');
    await page.click('#sizeChips button[data-b="custom"]');
    await page.fill('#sizeNum', '300');
    await page.selectOption('#sizeUnit', 'KB');
    await page.waitForFunction(() => /No superar 300 KB/.test(document.getElementById('sizeHint').textContent));
    const vPlan = await page.$$eval('#planList li', (li) => li.map((x) => x.textContent));
    mark('plan: ' + vPlan.join(' | '));
    check(/AVI MJPEG · \d+×\d+ · \d+ fps/.test(vPlan[1]) && /→ ≈ \d+ KB/.test(vPlan[1]), 'el AVI de camara que pasa de 300 KB se recomprime: ' + vPlan[1]);
    await page.click('#planGo');
    for (let i = 0; i < 360; i++) {
      await sleep(500);
      for (const b of await page.$$('.xfer.ask button:has-text("Subir así")')) { mark('aceptando perdida grande'); await b.click(); }
      const n = (await dump()).length;
      if (n >= before2 + 2) break;
      if (await page.$('.xfer.fail')) break;
    }
    items = await settle(before2 + 2);
    const log5 = await page.evaluate(() => window.__xfer);
    const fGrano = find('Videos', /^grano.*\.avi$/), fCam = find('Videos', /^camara.*\.avi$/);
    mark('tamanos: grano ' + sz(fGrano) + ', camara ' + sz(fCam));
    mark('tarjetas: ' + log5.filter((x) => /grano|camara/.test(x)).slice(-4).join(' | '));
    const aG = fGrano && mc('avi', fGrano), aK = fCam && mc('avi', fCam);
    check(fGrano && sz(fGrano) <= 300 * KB && aG.ok === 1 && aG.decoded === aG.frames && aG.bad === 0 && aG.dur >= 1900,
      'video con grano: ' + sz(fGrano) + ' bytes <= 300 KB, entero (' + (aG && aG.dur) + ' ms) y decodificable');
    check(fCam && sz(fCam) <= 300 * KB && sz(fCam) >= 200 * KB && aK.ok === 1 && aK.decoded === aK.frames && aK.bad === 0 && Math.abs(aK.dur - 4000) <= 200,
      'AVI de camara recomprimido: ' + sz(fCam) + ' bytes (<= 300 KB, aprovechandolo), 4 s enteros (' + (aK && aK.dur) + ' ms), ' + (aK && aK.w) + 'x' + (aK && aK.h) +
      ', todos sus fotogramas sanos');
    check(log5.some((x) => /grano\.webm.*→ \d+ KB \(−\d+ %\) · máximo 300 KB · \d+×\d+ · \d+ fps · calidad \d+ %/.test(x)) &&
      log5.some((x) => /camara\.avi.*→ \d+ KB \(−\d+ %\) · máximo 300 KB · \d+×\d+ · \d+ fps · calidad \d+ %/.test(x)),
      'tarjetas: antes → despues, el maximo y resolucion, fps y calidad usados');

    await page.click('#fab');
    await page.setInputFiles('#picker', [{ name: 'x.png', mimeType: 'image/png', buffer: png(64, 64) }]);
    await page.waitForSelector('#planList li:not(.busy)', { timeout: 30000 });
    await page.click('#sizeModes button[data-m="off"]');
    check(await page.isHidden('#sizeChips') && (await page.$$eval('#profiles button[disabled]', (b) => b.length)) === 0, 'volver a «Sin límite»: perfiles de nuevo');
    await page.click('#planCancel');

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
