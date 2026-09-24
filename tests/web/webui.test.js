// #############################################################
// ##  Pruebas de la web del movil (FlexOS_Ultra/webui/app.js)
// #############################################################
//
// Ejecuta en Node las funciones PURAS de la web (FX) y juzga lo que
// producen con referencias que no son ellas mismas:
//   · CRC-32 contra zlib de Node.
//   · Formato real contra flexMlSniff DEL FIRMWARE (mediacheck sniff).
//   · AVI MJPEG y WAV (PCM e IMA ADPCM) contra los analizadores y
//     decodificadores DEL FIRMWARE (mediacheck avi / wav): lo que acepta el
//     P4 es lo unico que cuenta.
//   · Etiquetas ID3 / MP4 / FLAC construidas aqui byte a byte.
//   · Que cada id que usa la interfaz exista en index.html.
//
//   node tests/web/webui.test.js      (MEDIACHECK=ruta si no esta en tests/host/build)
'use strict';
const fs = require('fs');
const os = require('os');
const path = require('path');
const cp = require('child_process');
const zlib = require('zlib');

const ROOT = path.join(__dirname, '..', '..');
const WEB = path.join(ROOT, 'FlexOS_Ultra', 'webui');
const FX = require(path.join(WEB, 'app.js'));
const MC = process.env.MEDIACHECK || path.join(ROOT, 'tests', 'host', 'build', 'mediacheck');
const TMP = fs.mkdtempSync(path.join(os.tmpdir(), 'flexweb-'));

let run = 0, fail = 0;
function check(cond, msg) {
  run++;
  if (!cond) { fail++; console.log('  FALLO: ' + msg); }
}
function section(t) { console.log('-- ' + t + ' --'); }

function mc(...args) {
  const out = cp.execFileSync(MC, args, { encoding: 'utf8' });
  return JSON.parse(out.trim().split('\n').pop());
}
function tmpFile(name, bytes) { const p = path.join(TMP, name); fs.writeFileSync(p, bytes); return p; }

// ---- bytes ----
const cat = (...parts) => Buffer.concat(parts.map((p) => Buffer.isBuffer(p) ? p : typeof p === 'string' ? Buffer.from(p, 'latin1') : Buffer.from(p)));
const le16 = (v) => { const b = Buffer.alloc(2); b.writeUInt16LE(v); return b; };
const le32 = (v) => { const b = Buffer.alloc(4); b.writeUInt32LE(v >>> 0); return b; };
const be16 = (v) => { const b = Buffer.alloc(2); b.writeUInt16BE(v); return b; };
const be24 = (v) => Buffer.from([(v >> 16) & 255, (v >> 8) & 255, v & 255]);
const be32 = (v) => { const b = Buffer.alloc(4); b.writeUInt32BE(v >>> 0); return b; };
const syncsafe = (n) => Buffer.from([(n >> 21) & 127, (n >> 14) & 127, (n >> 7) & 127, n & 127]);
const utf8 = (s) => Buffer.from(s, 'utf8');
const u8 = (b) => new Uint8Array(b.buffer, b.byteOffset, b.length);

let seed = 12345;
function rnd() { seed = (seed * 1103515245 + 12345) >>> 0; return seed >>> 8; }

function jpeg(w, h, s) {
  const p = path.join(TMP, 'f' + w + 'x' + h + '_' + s + '.jpg');
  const r = mc('jpeg', String(w), String(h), String(s), p);
  if (!r.ok) throw new Error('mediacheck jpeg');
  return fs.readFileSync(p);
}

// =============================================================
section('CRC-32 (el de zlib)');
check(FX.crc32(utf8('123456789')) === 0xCBF43926, 'vector estandar 123456789');
check(FX.crc32(new Uint8Array(0)) === 0, 'vacio = 0');
for (let i = 0; i < 40; i++) {
  const n = rnd() % 5000, b = Buffer.alloc(n);
  for (let k = 0; k < n; k++) b[k] = rnd() & 255;
  const ref = typeof zlib.crc32 === 'function' ? zlib.crc32(b) : null;
  const cut = rnd() % (n + 1);
  const chained = FX.crc32(u8(b.subarray(cut)), FX.crc32(u8(b.subarray(0, cut))));
  if (ref !== null) check(FX.crc32(u8(b)) === ref, 'igual que zlib (n=' + n + ')');
  check(chained === FX.crc32(u8(b)), 'encadenado = de una vez (corte ' + cut + ')');
}

// =============================================================
section('formato real: la web decide lo mismo que el P4');
const base = jpeg(64, 48, 1);
const samples = {
  'jpeg.jpg': base,
  'prog.jpg': fs.readFileSync(path.join(ROOT, 'tests', 'fixtures', 'progressive.jpg')),
  'a.png': cat([0x89], 'PNG\r\n\x1a\n', be32(13), 'IHDR', be32(2000), be32(1500), [8, 2, 0, 0, 0], Buffer.alloc(40)),
  'a.gif': cat('GIF89a', le16(320), le16(200), Buffer.alloc(40)),
  'a.bmp': cat('BM', le32(1000), le32(0), le32(54), le32(40), le32(300), le32(0xFFFFFF38), Buffer.alloc(40)),
  'a.webp': cat('RIFF', le32(100), 'WEBPVP8 ', Buffer.alloc(40)),
  'a.heic': cat(be32(24), 'ftypheic', be32(0), 'mif1heic', Buffer.alloc(40)),
  'a.avif': cat(be32(20), 'ftypavif', be32(0), 'avif', Buffer.alloc(40)),
  'a.mp4': cat(be32(20), 'ftypisom', be32(0), 'isom', Buffer.alloc(40)),
  'a.mov': cat(be32(20), 'ftypqt  ', be32(0), 'qt  ', Buffer.alloc(40)),
  'a.m4a': cat(be32(20), 'ftypM4A ', be32(0), 'M4A ', Buffer.alloc(40)),
  'a.webm': cat([0x1A, 0x45, 0xDF, 0xA3, 0x9F, 0x42, 0x86, 0x81, 0x01, 0x42, 0x82, 0x84], 'webm', Buffer.alloc(40)),
  'a.mkv': cat([0x1A, 0x45, 0xDF, 0xA3, 0x9F, 0x42, 0x86, 0x81, 0x01, 0x42, 0x82, 0x88], 'matroska', Buffer.alloc(40)),
  'a.flac': cat('fLaC', Buffer.alloc(60)),
  'a.ogg': cat('OggS', Buffer.alloc(60)),
  'id3.mp3': cat('ID3', [3, 0, 0], syncsafe(0), Buffer.alloc(60)),
  'sync.mp3': cat([0xFF, 0xFB, 0x90, 0x44], Buffer.alloc(60)),
  'a.aac': cat([0xFF, 0xF1, 0x50, 0x80], Buffer.alloc(60)),
  'pcm.wav': Buffer.from(FX.pcmWav(new Int16Array(100), 22050, 1)),
  'ima.wav': Buffer.from(FX.imaWav(new Int16Array(3000), 22050)),
  'float.wav': cat('RIFF', le32(100), 'WAVEfmt ', le32(16), le16(3), le16(1), le32(44100), le32(176400), le16(4), le16(32), 'data', le32(8), Buffer.alloc(8)),
  'mjpeg.avi': Buffer.from(FX.concat(FX.aviMux([u8(base)], 64, 48, 10))),
  'h264.avi': Buffer.from(FX.concat(FX.aviMux([u8(base)], 64, 48, 10))),
  'junk.bin': Buffer.from('esto no es ningun formato conocido, solo texto'),
};
// El AVI "h264" lleva otro codigo de codec en strh.
{ const b = samples['h264.avi']; const i = b.indexOf('vids'); b.write('H264', i + 4, 'latin1'); }
for (const [name, bytes] of Object.entries(samples)) {
  const js = FX.sniff(u8(bytes));
  const c = mc('sniff', tmpFile('s_' + name, bytes));
  const jsName = js ? FX.FMT[js].name : 'Desconocido';
  const jsKind = js ? FX.FMT[js].kind : 'none';
  check(jsName === c.fmt && jsKind === c.kind && (js ? FX.FMT[js].play : false) === !!c.playable,
    name + ': web=' + jsName + '/' + jsKind + '  P4=' + c.fmt + '/' + c.kind);
}
for (let i = 0; i < 3000; i++) {
  const n = rnd() % 64, b = new Uint8Array(n);
  for (let k = 0; k < n; k++) b[k] = rnd() & 255;
  if (i % 3 === 0 && n >= 4) b.set([0x52, 0x49, 0x46, 0x46]);
  if (i % 5 === 0 && n >= 3) b.set([0xFF, 0xD8, 0xFF]);
  let ok = true;
  try { const f = FX.sniff(b); ok = f === null || !!FX.FMT[f]; } catch (e) { ok = false; }
  if (!ok) { check(false, 'sniff con ruido no revienta'); break; }
}
check(true, 'sniff: 3000 entradas de ruido sin excepcion');

// =============================================================
section('JPEG: tamano, progresivo, orientacion EXIF y fecha');
{
  const j = FX.jpegInfo(u8(base));
  check(j.sof && j.w === 64 && j.h === 48 && !j.prog && j.orient === 1, 'baseline 64x48');
  const p = FX.jpegInfo(u8(samples['prog.jpg']));
  check(p.sof && p.prog, 'progresivo detectado');
  // APP1 EXIF: IFD0 con Orientation y puntero a la SubIFD con DateTimeOriginal.
  const exif = (le) => {
    const w16 = le ? le16 : be16, w32 = le ? le32 : be32;
    const date = cat('2023:07:14 18:05:09', [0]);
    const ifd0 = cat(w16(2), w16(0x0112), w16(3), w32(1), w16(6), w16(0), w16(0x8769), w16(4), w32(1), w32(38), w32(0));
    const sub = cat(w16(1), w16(0x9003), w16(2), w32(20), w32(56), w32(0));
    const tiff = cat(le ? 'II' : 'MM', w16(42), w32(8), ifd0, sub, date);
    const body = cat('Exif', [0, 0], tiff);
    return cat([0xFF, 0xE1], be16(body.length + 2), body);
  };
  for (const le of [true, false]) {
    const withExif = cat(base.subarray(0, 2), exif(le), base.subarray(2));
    const e = FX.jpegInfo(u8(withExif));
    check(e.orient === 6 && e.w === 64 && e.taken && e.taken.y === 2023 && e.taken.mo === 7 && e.taken.d === 14 &&
      e.taken.h === 18 && e.taken.mi === 5 && e.taken.s === 9, 'EXIF ' + (le ? 'II' : 'MM') + ': orientacion 6 y fecha');
    check(mc('thumb', tmpFile('exif' + le + '.jpg', withExif)).ok === 1, 'el P4 sigue leyendo el JPEG con EXIF');
  }
  for (let i = 0; i < 2000; i++) {
    const b = new Uint8Array(cat(base.subarray(0, 2), exif(i & 1), base.subarray(2, 200)));
    for (let k = 0; k < 4; k++) b[rnd() % b.length] = rnd() & 255;
    try { FX.jpegInfo(b.subarray(0, rnd() % b.length)); } catch (e) { check(false, 'jpegInfo con ruido revienta'); break; }
  }
  check(true, 'jpegInfo: 2000 cabeceras mutiladas sin excepcion');
  const d = FX.imageDims(u8(samples['a.png']), 'png');
  check(d && d.w === 2000 && d.h === 1500, 'PNG: tamano del IHDR');
  const g = FX.imageDims(u8(samples['a.gif']), 'gif');
  check(g && g.w === 320 && g.h === 200, 'GIF: tamano');
  const bm = FX.imageDims(u8(samples['a.bmp']), 'bmp');
  check(bm && bm.w === 300 && bm.h === 200, 'BMP: tamano (alto negativo = de arriba abajo)');
}

// =============================================================
section('geometria');
check(JSON.stringify(FX.fitBox(4032, 3024, 1600)) === '{"w":1600,"h":1200}', 'foto 4032x3024 -> 1600x1200');
check(JSON.stringify(FX.fitBox(800, 600, 1600)) === '{"w":800,"h":600}', 'nunca se agranda');
check(JSON.stringify(FX.fitVideo(1920, 1080, 640)) === '{"w":640,"h":360}', 'video 1080p -> 640x360');
check(JSON.stringify(FX.fitVideo(1080, 1920, 640)) === '{"w":360,"h":640}', 'video vertical -> 360x640');
check(JSON.stringify(FX.fitVideo(100, 60, 640)) === '{"w":96,"h":56}', 'video pequeno: multiplos de 8, sin agrandar');
check(JSON.stringify(FX.coverRect(300, 200)) === '{"x":50,"y":0,"s":200}', 'recorte centrado');

// =============================================================
section('plan de subida por perfil');
const LIM = { photo: 6 << 20, video: 8 << 20, audio: 8 << 20 };
const plan = (f, prof, room) => FX.plan(Object.assign({ size: 1 << 20, w: 0, h: 0, orient: 1, durMs: 0 }, f), prof, LIM, room);
{
  let p = plan({ kind: 'photo', fmt: 'jpeg', w: 4032, h: 3024 }, 'rec');
  check(p.act === 'photo' && p.w === 1600 && p.h === 1200 && /desde 4032×3024/.test(p.note), 'foto grande: se reduce (' + p.note + ')');
  p = plan({ kind: 'photo', fmt: 'jpeg', w: 1200, h: 900 }, 'rec');
  check(p.act === 'as-is' && /compatible/.test(p.note), 'JPEG que ya cabe: sin tocar');
  p = plan({ kind: 'photo', fmt: 'jpeg', w: 1200, h: 900, orient: 6 }, 'rec');
  check(p.act === 'photo' && p.w === 900 && p.h === 1200 && /enderezada/.test(p.note), 'EXIF girada: se endereza (900x1200)');
  p = plan({ kind: 'photo', fmt: 'heic' }, 'rec');
  check(p.act === 'photo' && p.est > 0 && /HEIC → JPEG/.test(p.note), 'HEIC: a JPEG');
  p = plan({ kind: 'photo', fmt: 'heic' }, 'orig');
  check(p.act === 'as-is' && p.level === 'warn' && /no puede mostrar HEIC/.test(p.note), 'Original HEIC: se guarda y se avisa');
  p = plan({ kind: 'photo', fmt: 'jpeg', size: 7 << 20, w: 4000, h: 3000 }, 'orig');
  check(p.act === 'none' && p.level === 'bad' && /6,0 MB/.test(p.note), 'Original de 7 MB: supera el limite de fotos');
  p = plan({ kind: 'photo', fmt: 'jpeg', w: 4000, h: 3000 }, 'rec', 100 * 1024);
  check(p.act === 'none' && /No cabe/.test(p.note), 'sin sitio: no se intenta');
  p = plan({ kind: 'video', fmt: 'mp4', w: 1920, h: 1080, durMs: 60000, decodable: true }, 'rec');
  check(p.act === 'video' && p.w === 640 && p.h === 360 && p.cutMs > 0 && p.est <= LIM.video && p.level === 'warn' &&
    /primeros/.test(p.note), 'MP4 de 1 min: AVI 640x360 y aviso de recorte (' + p.note + ')');
  p = plan({ kind: 'video', fmt: 'mp4', w: 1920, h: 1080, durMs: 5000, decodable: true }, 'light');
  check(p.act === 'video' && p.cutMs === 0 && p.w === 480 && p.level === '', 'MP4 de 5 s en Ligero: entero');
  p = plan({ kind: 'video', fmt: 'mkv', decodable: false }, 'rec');
  check(p.act === 'none' && /navegador no puede/.test(p.note), 'video que el navegador no abre: se dice');
  p = plan({ kind: 'video', fmt: 'avi-mjpeg' }, 'rec');
  check(p.act === 'as-is', 'AVI MJPEG: ya compatible');
  p = plan({ kind: 'video', fmt: 'avi-other' }, 'rec');
  check(p.act === 'none' && /Original/.test(p.note), 'AVI con otro codec: solo como original');
  p = plan({ kind: 'audio', fmt: 'mp3', durMs: 180000, decodable: true }, 'rec');
  check(p.act === 'audio' && p.out === 'wav-adpcm' && p.cutMs === 0 && Math.abs(p.est - FX.wavBytes(180000, FX.PROFILES.audio.rec)) === 0,
    'MP3 de 3 min: WAV IMA ADPCM entero (' + FX.fmtSize(p.est) + ')');
  p = plan({ kind: 'audio', fmt: 'mp3', durMs: 600000, decodable: true }, 'high');
  check(p.act === 'audio' && p.out === 'wav-pcm' && p.cutMs > 180000 && p.cutMs < 200000 && p.est <= LIM.audio, 'MP3 de 10 min en PCM: recortado a ~3 min');
  p = plan({ kind: 'audio', fmt: 'wav-adpcm', wav: { ch: 1, rate: 22050 } }, 'rec');
  check(p.act === 'as-is', 'WAV IMA mono 22 kHz: tal cual');
  p = plan({ kind: 'audio', fmt: 'wav-pcm', wav: { ch: 2, rate: 44100 }, durMs: 10000, decodable: true }, 'rec');
  check(p.act === 'audio', 'WAV estereo 44 kHz: se convierte');
  p = plan({ kind: null, fmt: null }, 'rec');
  check(p.act === 'none' && p.level === 'bad', 'formato desconocido: no');
  // Invariante: nada de lo que se planea supera el limite de su clase.
  for (let i = 0; i < 3000; i++) {
    const kinds = ['photo', 'video', 'audio'], k = kinds[rnd() % 3];
    const fmts = Object.keys(FX.FMT).filter((f) => FX.FMT[f].kind === k), fmt = fmts[rnd() % fmts.length];
    const f = { kind: k, fmt, size: rnd() % (12 << 20), w: rnd() % 5000, h: rnd() % 5000, orient: 1 + rnd() % 8,
      durMs: rnd() % 3600000, decodable: !!(rnd() & 1), wav: { ch: 1 + rnd() % 2, rate: 8000 + rnd() % 40000 } };
    const prof = ['light', 'rec', 'high', 'orig'][rnd() % 4];
    const room = rnd() % 3 ? null : rnd() % (10 << 20);
    const q = FX.plan(f, prof, LIM, room);
    if (q.act !== 'none' && (q.est > LIM[k] || (room != null && q.est > room))) { check(false, 'plan pasa del limite: ' + JSON.stringify([f, prof, room, q])); break; }
  }
  check(true, 'plan: 3000 casos al azar, nunca por encima del limite ni del sitio');
  // Varios a la vez: el espacio se descuenta en orden, con la misma cuenta que el P4.
  const MB = 1 << 20, reserve = 512 * 1024;
  const free = reserve + 2 * FX.spaceNeed(MB) + 1000;
  const all = FX.planAll([1, 2, 3].map(() => ({ kind: 'photo', fmt: 'jpeg', size: MB, w: 800, h: 600, orient: 1 })), 'rec',
    { free, reserve, lim: LIM });
  check(all[0].act === 'as-is' && all[1].act === 'as-is' && all[2].act === 'none' && /No cabe/.test(all[2].note),
    'tres fotos donde caben dos: la tercera se queda');
}

// =============================================================
section('texto y consulta de subida');
check(FX.fmtSize(0) === '0 KB' && FX.fmtSize(1) === '1 KB' && FX.fmtSize(1025) === '2 KB', 'KB redondeando hacia arriba');
check(FX.fmtSize(1048576) === '1,0 MB' && FX.fmtSize(1572864) === '1,5 MB' && FX.fmtSize(6 << 20) === '6,0 MB', 'MB con coma, como el P4');
check(FX.fmtDur(0) === '0:00' && FX.fmtDur(61000) === '1:01' && FX.fmtDur(3601000) === '1:00:01', 'duraciones');
check(FX.uploadName('IMG_1234.HEIC', '.jpg') === 'IMG_1234.jpg', 'extension del formato real');
check(FX.uploadName('a/b\\c.png', '.jpg') === 'a_b_c.jpg', 'sin barras');
check(FX.uploadName('', '.wav') === 'Archivo.wav', 'nombre vacio');
{
  const long = '写真'.repeat(40) + '🎉'.repeat(10) + '.jpeg';
  const n = FX.uploadName(long, '.jpg');
  check(FX.utf8Len(n) <= 63 && n.endsWith('.jpg') && Buffer.from(n, 'utf8').toString('utf8') === n, 'nombre largo: 63 bytes, UTF-8 entero, con extension');
  const q = FX.uploadQuery({ kind: 'audio', name: n, size: 123456, crc: 0xDEADBEEF, created: 1700000000, dur: 61000,
    title: 'Título '.repeat(20), artist: '艺术家'.repeat(20), album: 'Álbum 🎵'.repeat(20) });
  check(q.length <= 500, 'consulta dentro del limite del P4 (' + q.length + ' <= 511)');
  const params = new URLSearchParams(q);
  check(params.get('name') === n && params.get('crc') === String(0xDEADBEEF) && params.get('dur') === '61000',
    'la consulta se lee de vuelta: nombre, CRC y duracion');
  check(FX.utf8Len(params.get('title') || '') <= 47 && FX.utf8Len(params.get('artist') || '') <= 31, 'etiquetas recortadas a lo que guarda el catalogo');
}

// =============================================================
section('AVI MJPEG: lo que genera la web lo lee el P4');
{
  const frames = [];
  const sizes = new Set();
  for (let s = 1; frames.length < 24; s++) { const f = jpeg(160, 96, s); frames.push(u8(f)); sizes.add(f.length & 1); }
  check(sizes.size === 2, 'hay fotogramas de longitud par e impar (relleno RIFF)');
  const avi = FX.concat(FX.aviMux(frames, 160, 96, 12));
  const total = frames.reduce((a, f) => a + f.length + (f.length & 1), 0);
  check(avi.length === FX.aviOverhead(frames.length) + total, 'tamano exacto = el que se preve al convertir');
  const r = mc('avi', tmpFile('web.avi', avi));
  check(r.ok === 1 && r.codec === 'MJPG', 'flexAviOpen lo abre (MJPG)');
  check(r.w === 160 && r.h === 96 && r.frames === 24 && r.declared === 24, '160x96, 24 fotogramas');
  check(r.decoded === 24 && r.bad === 0, 'el decodificador del P4 lee LOS 24 fotogramas');
  // 1e6/12 no es entero: el P4 guarda 83333 us y 24 fotogramas le dan 1999 ms.
  check(r.us === 83333 && Math.abs(r.dur - 2000) <= 1, '12 fps: 83333 us y 2 s (' + r.dur + ' ms)');
  check(r.index === 1, 'usa el idx1 (busqueda fiable)');
  check(r.thumb === 1, 'la miniatura del P4 sale del primer fotograma');
  const ix = FX.aviIndex(avi);
  check(ix && ix.frames.length === 24 && ix.w === 160 && ix.us === 83333 &&
    ix.frames.every((f, i) => f.len === frames[i].length && avi[f.off] === 0xFF && avi[f.off + 1] === 0xD8), 'el visor web encuentra los mismos fotogramas');
  let broke = false;
  for (let i = 0; i < 1500 && !broke; i++) {
    const b = avi.slice(0, rnd() % avi.length);
    for (let k = 0; k < 3; k++) if (b.length) b[rnd() % b.length] = rnd() & 255;
    try { FX.aviIndex(b); } catch (e) { broke = true; }
  }
  check(!broke, 'aviIndex: 1500 AVI mutilados sin excepcion');
  const big = FX.AVI_FRAME_MAX;
  check(big === 192 * 1024, 'tope de fotograma = el del reproductor del P4');
}

// =============================================================
section('WAV PCM e IMA ADPCM: lo que genera la web lo decodifica el P4');
// Tono de 440 Hz + barrido + ruido. `sweep` = cuanto sube el barrido (Hz por
// segundo, la mitad): 400 lo deja por debajo de 2 kHz (voz, casi toda la
// musica); 1500 lo lleva hasta 7 kHz, cerca de Nyquist a 16 kHz, que es donde
// IMA ADPCM pierde calidad por diseño (el paso se adapta tarde).
function signal(n, rate, sweep) {
  const s = new Int16Array(n);
  for (let i = 0; i < n; i++) {
    const t = i / rate;
    const chirp = Math.sin(2 * Math.PI * (200 + (sweep || 400) * t) * t);
    s[i] = Math.round(12000 * Math.sin(2 * Math.PI * 440 * t) + 6000 * chirp + ((rnd() % 600) - 300));
  }
  return s;
}
function snr(ref, got) {
  let e = 0, p = 0;
  for (let i = 0; i < ref.length; i++) { p += ref[i] * ref[i]; e += (ref[i] - got[i]) ** 2; }
  return 10 * Math.log10(p / Math.max(1, e));
}
function rawOf(p) { const b = fs.readFileSync(p); return new Int16Array(b.buffer, b.byteOffset, b.length / 2); }
{
  const pcm = signal(22050, 22050);
  const w = FX.pcmWav(pcm, 22050, 1);
  const r = mc('wav', tmpFile('pcm.wav', w), path.join(TMP, 'pcm.raw'));
  const got = rawOf(path.join(TMP, 'pcm.raw'));
  check(r.ok === 1 && r.format === 1 && r.rate === 22050 && r.ch === 1 && r.bits === 16 && r.dur === 1000, 'PCM 16: cabecera que el P4 entiende');
  check(got.length === pcm.length && got.every((v, i) => v === pcm[i]), 'PCM 16: muestras identicas');
  check(w.length === FX.wavBytes(1000, { rate: 22050, codec: 'pcm' }), 'PCM: tamano previsto exacto');

  for (const rate of [22050, 16000]) {
    const n = Math.round(rate * 2.3);
    const src = signal(n, rate);
    const wav = FX.imaWav(src, rate);
    const q = mc('wav', tmpFile('ima' + rate + '.wav', wav), path.join(TMP, 'ima.raw'));
    const c = rawOf(path.join(TMP, 'ima.raw'));
    const js = FX.wavDecode(wav);
    const ba = FX.imaBlockAlign(rate, 1);
    check(q.ok === 1 && q.format === 17 && q.bits === 4 && q.ch === 1 && q.blockAlign === ba && q.spb === FX.imaSpb(ba, 1) && q.frames === n,
      'IMA ' + rate + ': fmt + fact que el P4 acepta (bloque ' + ba + ', ' + q.spb + ' muestras)');
    check(c.length === n && js && js.pcm.length === n && js.pcm.every((v, i) => v === c[i]), 'IMA ' + rate + ': decodificador web = decodificador del P4, muestra a muestra');
    const d = snr(src, c);
    check(d > 28, 'IMA ' + rate + ': SNR tras el P4 ' + d.toFixed(1) + ' dB (> 28) con contenido < 2 kHz');
    const hard = signal(n, rate, 1500), dh = snr(hard, FX.wavDecode(FX.imaWav(hard, rate)).pcm);
    check(dh > 20, 'IMA ' + rate + ': SNR ' + dh.toFixed(1) + ' dB (> 20) con un barrido hasta 7 kHz');
    const ms = Math.round(n * 1000 / rate);
    check(Math.abs(q.dur - ms) <= 1, 'IMA ' + rate + ': duracion ' + q.dur + ' ms');
    check(wav.length === FX.wavBytes(n * 1000 / rate, { rate, codec: 'ima' }), 'IMA ' + rate + ': tamano previsto exacto');
  }
  // Estereo IMA hecho aqui con bloques al azar: los dos decodificadores coinciden.
  const ba = 1024, spb = ba - 7, blocks = 5, data = Buffer.alloc(ba * blocks);
  for (let b = 0; b < blocks; b++) {
    for (let k = 0; k < ba; k++) data[b * ba + k] = rnd() & 255;
    for (let c = 0; c < 2; c++) { data.writeInt16LE((rnd() % 60000) - 30000, b * ba + 4 * c); data[b * ba + 4 * c + 2] = rnd() % 89; data[b * ba + 4 * c + 3] = 0; }
  }
  const st = cat('RIFF', le32(4 + 8 + 20 + 8 + data.length), 'WAVE', 'fmt ', le32(20), le16(0x11), le16(2), le32(44100),
    le32(Math.floor(44100 * ba / spb)), le16(ba), le16(4), le16(2), le16(spb), 'data', le32(data.length), data);
  const sq = mc('wav', tmpFile('st.wav', st), path.join(TMP, 'st.raw'));
  const sc = rawOf(path.join(TMP, 'st.raw'));
  const sj = FX.wavDecode(u8(st));
  check(sq.ok === 1 && sq.ch === 2 && sj && sj.ch === 2 && sj.pcm.length === sc.length && sj.pcm.every((v, i) => v === sc[i]),
    'IMA estereo: web = P4 en ' + sc.length + ' muestras');
  // Presupuestos: nunca pasan del hueco que se les da.
  let okCap = true;
  for (let i = 0; i < 500; i++) {
    const cap = 70000 + rnd() % (8 << 20);
    for (const Q of [FX.PROFILES.audio.light, FX.PROFILES.audio.rec, FX.PROFILES.audio.high]) if (FX.wavBytes(FX.wavMaxMs(cap, Q), Q) > cap) okCap = false;
    const fb = 1000 + rnd() % 90000;
    if (FX.aviBytes(FX.aviMaxFrames(cap, fb), fb) > cap) okCap = false;
  }
  check(okCap, 'wavMaxMs y aviMaxFrames nunca pasan del hueco');
  let broke = false;
  for (let i = 0; i < 1500 && !broke; i++) {
    const b = new Uint8Array(st.subarray(0, rnd() % st.length));
    for (let k = 0; k < 4; k++) if (b.length) b[rnd() % b.length] = rnd() & 255;
    try { FX.wavDecode(b); FX.wavInfo(b); } catch (e) { broke = true; }
  }
  check(!broke, 'wavDecode: 1500 WAV mutilados sin excepcion');
}

// =============================================================
section('audio: mezcla, cambio de frecuencia y conversion a 16 bits');
{
  const n = 44100, L = new Float32Array(n), R = new Float32Array(n);
  for (let i = 0; i < n; i++) { L[i] = 0.8 * Math.sin(2 * Math.PI * 1000 * i / 44100); R[i] = L[i]; }
  const out = FX.resampleMono([L, R], 44100, 16000);
  check(out.length === 16000, '1 s a 16 kHz = 16000 muestras');
  let zc = 0;
  for (let i = 1; i < out.length; i++) if ((out[i - 1] < 0) !== (out[i] < 0)) zc++;
  check(Math.abs(zc / 2 - 1000) <= 10, 'el tono de 1 kHz sigue en 1 kHz (' + (zc / 2) + ' Hz)');
  let rms = 0;
  for (let i = 100; i < out.length; i++) rms += out[i] * out[i];
  rms = Math.sqrt(rms / (out.length - 100));
  check(rms > 0.5 && rms < 0.6, 'amplitud conservada (RMS ' + rms.toFixed(3) + ')');
  const anti = FX.resampleMono([L, L.map((v) => -v)], 44100, 22050);
  check(anti.every((v) => Math.abs(v) < 1e-6), 'L y -L se anulan al mezclar a mono');
  const q = FX.floatToInt16(new Float32Array([1.5, 1, 0.5, 0, -0.5, -1, -2]));
  check(Array.from(q).join(',') === '32767,32767,16384,0,-16384,-32768,-32768', 'a 16 bits con saturacion');
}

// =============================================================
section('etiquetas de audio (titulo, artista, album, portada)');
{
  const cover = jpeg(40, 40, 3);
  const utf16 = (s) => cat([0xFF, 0xFE], Buffer.from(s, 'utf16le'), [0, 0]);
  const f23 = (id, p) => cat(id, be32(p.length), [0, 0], p);
  const body = cat(f23('TIT2', cat([1], utf16('Canción de prueba'))), f23('TPE1', cat([0], Buffer.from('Beyoncé', 'latin1'))),
    f23('TALB', cat([1], utf16('Álbum'))), f23('APIC', cat([0], 'image/jpeg', [0, 3, 0], cover)));
  const v23 = cat('ID3', [3, 0, 0], syncsafe(body.length + 20), body, Buffer.alloc(20), [0xFF, 0xFB, 0x90, 0x44]);
  let t = FX.tags(u8(v23), 'mp3');
  check(t.title === 'Canción de prueba' && t.artist === 'Beyoncé' && t.album === 'Álbum', 'ID3v2.3: UTF-16 y Latin-1');
  check(t.pic && t.pic.mime === 'image/jpeg' && Buffer.from(t.pic.data).equals(cover), 'ID3v2.3: portada JPEG intacta');
  check(FX.id3Size(u8(v23)) === 10 + body.length + 20, 'tamano de la etiqueta');
  const f24 = (id, p, fl) => cat(id, syncsafe(p.length), be16(fl || 0), p);
  const t24 = cat(utf8('Título ñ'));
  const body4 = cat(f24('TIT2', cat([3], t24)), f24('TPE1', cat(syncsafe(9), [3], utf8('Artista')), 0x0001), f24('TALB', cat([3], utf8('Disco'))));
  t = FX.tags(u8(cat('ID3', [4, 0, 0], syncsafe(body4.length), body4)), 'mp3');
  check(t.title === 'Título ñ' && t.artist === 'Artista' && t.album === 'Disco', 'ID3v2.4: UTF-8 y marca de longitud de datos');
  const png = cat([0x89], 'PNG\r\n\x1a\n', Buffer.alloc(30, 1));
  const f22 = (id, p) => cat(id, be24(p.length), p);
  const body2 = cat(f22('TT2', cat([0], 'Viejo')), f22('TP1', cat([0], 'Grupo')), f22('PIC', cat([0], 'PNG', [3, 0], png)));
  t = FX.tags(u8(cat('ID3', [2, 0, 0], syncsafe(body2.length), body2)), 'mp3');
  check(t.title === 'Viejo' && t.artist === 'Grupo' && t.pic && t.pic.mime === 'image/png' && Buffer.from(t.pic.data).equals(png), 'ID3v2.2 con PIC PNG');
  const box = (type, ...p) => { const b = cat(...p); return cat(be32(8 + b.length), type, b); };
  const dat = (ty, b) => box('data', be32(ty), be32(0), b);
  const moov = box('moov', box('udta', box('meta', Buffer.alloc(4), box('hdlr', Buffer.alloc(25)),
    box('ilst', box('\xa9nam', dat(1, utf8('Tema M4A'))), box('\xa9ART', dat(1, utf8('Intérprete'))), box('\xa9alb', dat(1, utf8('LP'))),
      box('covr', dat(13, cover))))));
  const m4a = cat(box('ftyp', 'M4A ', be32(0), 'M4A isom'), box('mdat', Buffer.alloc(64)), moov);
  t = FX.tags(u8(m4a), 'm4a');
  check(t.title === 'Tema M4A' && t.artist === 'Intérprete' && t.album === 'LP' && t.pic && Buffer.from(t.pic.data).equals(cover), 'M4A: ilst y portada');
  const vc = (s) => cat(le32(utf8(s).length), utf8(s));
  const vcb = cat(vc('libFLAC'), le32(3), vc('TITLE=Pista FLAC'), vc('artist=Banda'), vc('ALBUM=Directo'));
  const pic = cat(be32(3), be32(10), 'image/jpeg', be32(0), be32(40), be32(40), be32(24), be32(0), be32(cover.length), cover);
  const hdr = (ty, last, n) => Buffer.from([(last ? 0x80 : 0) | ty, (n >> 16) & 255, (n >> 8) & 255, n & 255]);
  const flac = cat('fLaC', hdr(0, false, 34), Buffer.alloc(34), hdr(4, false, vcb.length), vcb, hdr(6, true, pic.length), pic);
  t = FX.tags(u8(flac), 'flac');
  check(t.title === 'Pista FLAC' && t.artist === 'Banda' && t.album === 'Directo' && t.pic && Buffer.from(t.pic.data).equals(cover), 'FLAC: Vorbis comment y PICTURE');
  let broke = false;
  for (const src of [v23, cat('ID3', [4, 0, 0], syncsafe(body4.length), body4), m4a, flac]) {
    for (let i = 0; i < 800 && !broke; i++) {
      const b = new Uint8Array(src.subarray(0, rnd() % src.length));
      for (let k = 0; k < 4; k++) if (b.length > 12) b[4 + rnd() % (b.length - 4)] = rnd() & 255;
      try { FX.tags(b, ['mp3', 'm4a', 'flac'][i % 3]); } catch (e) { broke = true; }
    }
  }
  check(!broke, 'etiquetas mutiladas: nunca revienta (3200 casos)');
}

// =============================================================
section('coherencia de la interfaz');
{
  const js = fs.readFileSync(path.join(WEB, 'app.js'), 'utf8');
  const html = fs.readFileSync(path.join(WEB, 'index.html'), 'utf8');
  const css = fs.readFileSync(path.join(WEB, 'app.css'), 'utf8');
  const ids = new Set(Array.from(js.matchAll(/\$\('([A-Za-z0-9_]+)'\)/g), (m) => m[1]));
  const missing = Array.from(ids).filter((id) => !html.includes('id="' + id + '"'));
  check(ids.size > 30 && missing.length === 0, 'cada $(id) de app.js existe en index.html' + (missing.length ? ': faltan ' + missing : ''));
  const cls = new Set();
  for (const m of js.matchAll(/el\('[a-z0-9]+',\s*'([^']+)'/g)) for (const c of m[1].split(/\s+/)) if (c) cls.add(c);
  const noCss = Array.from(cls).filter((c) => !new RegExp('\\.' + c.replace(/[-]/g, '\\-') + '(?![a-zA-Z0-9_-])').test(css));
  check(noCss.length === 0, 'cada clase que crea app.js tiene estilo' + (noCss.length ? ': sin estilo ' + noCss : ''));
  check(!/\beval\s*\(|new Function\s*\(|innerHTML\s*=/.test(js), 'ni eval, ni Function, ni innerHTML (CSP y XSS)');
  check(!/style=/.test(html) && !/<script>/.test(html), 'HTML sin estilos ni scripts en linea');
}

fs.rmSync(TMP, { recursive: true, force: true });
console.log('=== ' + run + ' comprobaciones, ' + fail + ' fallos ===');
process.exit(fail ? 1 : 0);
