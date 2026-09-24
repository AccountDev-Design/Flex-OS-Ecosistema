/* Flex OS · Biblioteca web (Flex Web Server)
 *
 * La sirve el propio P4 desde su memoria (FlexOS_WebUI.h, que genera
 * webui/gen_header.py a partir de este archivo). Tiene dos partes:
 *
 *   FX  Funciones PURAS: CRC32, deteccion del formato real, perfiles de
 *       conversion, AVI MJPEG, WAV PCM / IMA ADPCM, etiquetas ID3/MP4/FLAC.
 *       No tocan el DOM: tests/web/webui.test.js las ejecuta en Node y
 *       comprueba lo que producen con los analizadores DEL FIRMWARE.
 *   UI  La aplicacion: emparejar, biblioteca, subir, ver y descargar.
 *
 * Nada se da por hecho en el movil: lo que se sube lo vuelve a comprobar el
 * P4 (CRC de extremo a extremo, formato real, decodificacion completa) y
 * esta pagina solo dice "guardado" cuando el P4 lo confirma. Las barras de
 * progreso son bytes reales.
 */
(function () {
'use strict';

const FX = {};

// ---------------------------------------------------------------------------
//  Bytes
// ---------------------------------------------------------------------------
function rd16le(b, o) { return b[o] | (b[o + 1] << 8); }
function rd32le(b, o) { return (b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24)) >>> 0; }
function rd16be(b, o) { return (b[o] << 8) | b[o + 1]; }
function rd24be(b, o) { return (b[o] << 16) | (b[o + 1] << 8) | b[o + 2]; }
function rd32be(b, o) { return ((b[o] << 24) | (b[o + 1] << 16) | (b[o + 2] << 8) | b[o + 3]) >>> 0; }
function cc(b, o, s) {
  if (o < 0 || o + s.length > b.length) return false;
  for (let i = 0; i < s.length; i++) if (b[o + i] !== s.charCodeAt(i)) return false;
  return true;
}
function str4(b, o) { return String.fromCharCode(b[o], b[o + 1], b[o + 2], b[o + 3]); }

FX.concat = function (parts) {
  let n = 0;
  for (const p of parts) n += p.length;
  const out = new Uint8Array(n);
  let o = 0;
  for (const p of parts) { out.set(p, o); o += p.length; }
  return out;
};

// ---------------------------------------------------------------------------
//  CRC-32 (el de zlib): el P4 lo recalcula sobre lo que recibe y lo compara
// ---------------------------------------------------------------------------
const CRC_T = (function () {
  const t = new Int32Array(256);
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320 ^ (c >>> 1)) : (c >>> 1);
    t[n] = c;
  }
  return t;
})();
FX.crc32 = function (u8, crc) {
  let c = ((crc >>> 0) ^ 0xFFFFFFFF) | 0;
  for (let i = 0; i < u8.length; i++) c = CRC_T[(c ^ u8[i]) & 0xFF] ^ (c >>> 8);
  return (c ^ 0xFFFFFFFF) >>> 0;
};

// ---------------------------------------------------------------------------
//  Formatos. Mismos nombres y misma decision que FlexOS_MediaLib (flexMlSniff):
//  la web anticipa lo que el P4 va a decidir con los mismos bytes.
// ---------------------------------------------------------------------------
const FMT = {
  'jpeg':      { name: 'JPEG',            kind: 'photo', play: true,  ext: '.jpg' },
  'jpeg-prog': { name: 'JPEG progresivo', kind: 'photo', play: false, ext: '.jpg' },
  'png':       { name: 'PNG',             kind: 'photo', play: false, ext: '.png' },
  'gif':       { name: 'GIF',             kind: 'photo', play: false, ext: '.gif' },
  'bmp':       { name: 'BMP',             kind: 'photo', play: false, ext: '.bmp' },
  'webp':      { name: 'WebP',            kind: 'photo', play: false, ext: '.webp' },
  'heic':      { name: 'HEIC',            kind: 'photo', play: false, ext: '.heic' },
  'avif':      { name: 'AVIF',            kind: 'photo', play: false, ext: '.avif' },
  'avi-mjpeg': { name: 'AVI MJPEG',       kind: 'video', play: true,  ext: '.avi' },
  'avi-other': { name: 'AVI',             kind: 'video', play: false, ext: '.avi' },
  'mp4':       { name: 'MP4',             kind: 'video', play: false, ext: '.mp4' },
  'mov':       { name: 'MOV',             kind: 'video', play: false, ext: '.mov' },
  'webm':      { name: 'WebM',            kind: 'video', play: false, ext: '.webm' },
  'mkv':       { name: 'MKV',             kind: 'video', play: false, ext: '.mkv' },
  'wav-pcm':   { name: 'WAV PCM',         kind: 'audio', play: true,  ext: '.wav' },
  'wav-adpcm': { name: 'WAV IMA ADPCM',   kind: 'audio', play: true,  ext: '.wav' },
  'wav-other': { name: 'WAV',             kind: 'audio', play: false, ext: '.wav' },
  'mp3':       { name: 'MP3',             kind: 'audio', play: false, ext: '.mp3' },
  'aac':       { name: 'AAC',             kind: 'audio', play: false, ext: '.aac' },
  'm4a':       { name: 'M4A',             kind: 'audio', play: false, ext: '.m4a' },
  'flac':      { name: 'FLAC',            kind: 'audio', play: false, ext: '.flac' },
  'ogg':       { name: 'OGG',             kind: 'audio', play: false, ext: '.ogg' }
};
FX.FMT = FMT;
FX.kindOf = function (fmt) { return FMT[fmt] ? FMT[fmt].kind : null; };

function sniffJpeg(h) {
  let i = 2;
  const n = h.length;
  while (i + 4 <= n) {
    if (h[i] !== 0xFF) return 'jpeg';
    const m = h[i + 1];
    if (m === 0xFF) { i++; continue; }
    if (m === 0xD8 || (m >= 0xD0 && m <= 0xD7) || m === 0x01) { i += 2; continue; }
    if (m === 0xC0 || m === 0xC1) return 'jpeg';
    if (m === 0xC2 || m === 0xC3 || (m >= 0xC5 && m <= 0xC7) || (m >= 0xC9 && m <= 0xCB) ||
        (m >= 0xCD && m <= 0xCF)) return 'jpeg-prog';
    if (m === 0xDA || m === 0xD9) return 'jpeg';
    const len = rd16be(h, i + 2);
    if (len < 2) return 'jpeg';
    i += 2 + len;
  }
  return 'jpeg';
}

function sniffRiff(h) {
  const n = h.length;
  if (n < 12) return null;
  if (cc(h, 8, 'WEBP')) return 'webp';
  if (cc(h, 8, 'AVI ')) {
    for (let i = 12; i + 16 <= n; i++) {
      if (cc(h, i, 'strh') && cc(h, i + 8, 'vids')) {
        const c = str4(h, i + 12).toLowerCase();
        return (c === 'mjpg' || c === 'jpeg' || c === 'dmb1' || c === 'mjpa') ? 'avi-mjpeg' : 'avi-other';
      }
    }
    return 'avi-mjpeg';
  }
  if (cc(h, 8, 'WAVE')) {
    let p = 12;
    while (p + 8 <= n) {
      const len = rd32le(h, p + 4);
      if (cc(h, p, 'fmt ') && p + 8 + 16 <= n) {
        const v = p + 8;
        let tag = rd16le(h, v);
        const ch = rd16le(h, v + 2), bits = rd16le(h, v + 14);
        if (tag === 0xFFFE && len >= 40 && p + 8 + 26 <= n) tag = rd16le(h, v + 24);
        if (tag === 1 && (bits === 8 || bits === 16) && ch >= 1 && ch <= 2) return 'wav-pcm';
        if (tag === 0x11 && bits === 4 && ch >= 1 && ch <= 2) return 'wav-adpcm';
        return 'wav-other';
      }
      if (len > n) break;
      p += 8 + len + (len & 1);
    }
    return 'wav-other';
  }
  return null;
}

// Formato REAL por la firma de los primeros bytes (nunca por la extension).
FX.sniff = function (h) {
  const n = h.length;
  if (n < 4) return null;
  if (n >= 3 && h[0] === 0xFF && h[1] === 0xD8 && h[2] === 0xFF) return sniffJpeg(h);
  if (n >= 8 && h[0] === 0x89 && cc(h, 1, 'PNG\r\n\x1a\n')) return 'png';
  if (n >= 6 && (cc(h, 0, 'GIF87a') || cc(h, 0, 'GIF89a'))) return 'gif';
  if (n >= 14 && h[0] === 0x42 && h[1] === 0x4D && rd32le(h, 6) === 0 && rd32le(h, 10) < (1 << 20)) return 'bmp';
  if (cc(h, 0, 'RIFF')) return sniffRiff(h);
  if (n >= 12 && cc(h, 4, 'ftyp')) {
    const b = str4(h, 8);
    if (['heic', 'heix', 'hevc', 'hevx', 'heim', 'heis', 'mif1', 'msf1'].indexOf(b) >= 0) return 'heic';
    if (b === 'avif' || b === 'avis') return 'avif';
    if (b === 'M4A ' || b === 'M4B ' || b === 'M4P ') return 'm4a';
    if (b === 'qt  ') return 'mov';
    return 'mp4';
  }
  if (h[0] === 0x1A && h[1] === 0x45 && h[2] === 0xDF && h[3] === 0xA3) {
    for (let i = 4; i + 4 <= n && i < 64; i++) if (cc(h, i, 'webm')) return 'webm';
    return 'mkv';
  }
  if (cc(h, 0, 'fLaC')) return 'flac';
  if (cc(h, 0, 'OggS')) return 'ogg';
  if (n >= 3 && cc(h, 0, 'ID3')) return 'mp3';
  if (h[0] === 0xFF && (h[1] & 0xF6) === 0xF0) return 'aac';
  if (h[0] === 0xFF && (h[1] & 0xE0) === 0xE0 && ((h[1] >> 1) & 3) === 1 &&
      ((h[1] >> 3) & 3) !== 1 && (h[2] >> 4) !== 15 && ((h[2] >> 2) & 3) !== 3) return 'mp3';
  return null;
};

// ---------------------------------------------------------------------------
//  JPEG: tamano, progresivo, orientacion EXIF y fecha de captura
// ---------------------------------------------------------------------------
function exifRead(h, o, end, out) {
  if (o + 14 > end || !cc(h, o, 'Exif') || h[o + 4] || h[o + 5]) return;
  const t = o + 6;
  const le = h[t] === 0x49 && h[t + 1] === 0x49;
  const be = h[t] === 0x4D && h[t + 1] === 0x4D;
  if (!le && !be) return;
  const r16 = (p) => le ? rd16le(h, p) : rd16be(h, p);
  const r32 = (p) => le ? rd32le(h, p) : rd32be(h, p);
  const ifd = (off, visit) => {
    const at = t + off;
    if (off < 8 || at + 2 > end) return;
    const cnt = r16(at);
    for (let k = 0; k < cnt && k < 512; k++) {
      const e = at + 2 + 12 * k;
      if (e + 12 > end) return;
      visit(r16(e), e);
    }
  };
  let exifOff = 0;
  ifd(r32(t + 4), (tag, e) => {
    if (tag === 0x0112) { const v = r16(e + 8); if (v >= 1 && v <= 8) out.orient = v; }
    if (tag === 0x8769) exifOff = r32(e + 8);
  });
  if (exifOff) {
    ifd(exifOff, (tag, e) => {
      if (tag !== 0x9003 || r32(e + 4) < 19) return;
      const p = t + r32(e + 8);
      if (p + 19 > end) return;
      const s = String.fromCharCode.apply(null, h.subarray(p, p + 19));
      const m = /^(\d{4}):(\d\d):(\d\d) (\d\d):(\d\d):(\d\d)$/.exec(s);
      if (m && +m[1] >= 1990) out.taken = { y: +m[1], mo: +m[2], d: +m[3], h: +m[4], mi: +m[5], s: +m[6] };
    });
  }
}

FX.jpegInfo = function (h) {
  const r = { w: 0, h: 0, prog: false, orient: 1, taken: null, sof: false };
  if (!(h.length >= 4 && h[0] === 0xFF && h[1] === 0xD8)) return r;
  let i = 2;
  const n = h.length;
  while (i + 4 <= n) {
    if (h[i] !== 0xFF) break;
    const m = h[i + 1];
    if (m === 0xFF) { i++; continue; }
    if (m === 0xD8 || (m >= 0xD0 && m <= 0xD7) || m === 0x01) { i += 2; continue; }
    if (m === 0xDA || m === 0xD9) break;
    const len = rd16be(h, i + 2);
    if (len < 2) break;
    if (m >= 0xC0 && m <= 0xCF && m !== 0xC4 && m !== 0xC8 && m !== 0xCC) {
      if (i + 9 <= n) { r.h = rd16be(h, i + 5); r.w = rd16be(h, i + 7); r.sof = true; }
      r.prog = !(m === 0xC0 || m === 0xC1);
      break;
    }
    if (m === 0xE1) exifRead(h, i + 4, Math.min(n, i + 2 + len), r);
    i += 2 + len;
  }
  return r;
};

// Tamano de una imagen sin decodificarla (los formatos que lo dicen al principio).
FX.imageDims = function (h, fmt) {
  if (fmt === 'jpeg' || fmt === 'jpeg-prog') { const j = FX.jpegInfo(h); return j.sof ? { w: j.w, h: j.h } : null; }
  if (fmt === 'png' && h.length >= 24 && cc(h, 12, 'IHDR')) return { w: rd32be(h, 16), h: rd32be(h, 20) };
  if (fmt === 'gif' && h.length >= 10) return { w: rd16le(h, 6), h: rd16le(h, 8) };
  if (fmt === 'bmp' && h.length >= 26) {
    const hh = rd32le(h, 22) | 0;
    return { w: rd32le(h, 18) | 0, h: Math.abs(hh) };
  }
  return null;
};

// ---------------------------------------------------------------------------
//  WAV: cabecera y decodificacion (PCM 8/16 e IMA ADPCM de Microsoft)
// ---------------------------------------------------------------------------
FX.wavInfo = function (h) {
  if (!(h.length >= 12 && cc(h, 0, 'RIFF') && cc(h, 8, 'WAVE'))) return null;
  const r = { tag: 0, ch: 0, rate: 0, bits: 0, blockAlign: 0, spb: 0, dataOff: 0, dataLen: 0, fact: 0 };
  let p = 12;
  while (p + 8 <= h.length) {
    const id = str4(h, p), len = rd32le(h, p + 4), v = p + 8;
    if (id === 'fmt ' && v + 16 <= h.length) {
      r.tag = rd16le(h, v); r.ch = rd16le(h, v + 2); r.rate = rd32le(h, v + 4);
      r.blockAlign = rd16le(h, v + 12); r.bits = rd16le(h, v + 14);
      if (r.tag === 0xFFFE && len >= 40 && v + 26 <= h.length) r.tag = rd16le(h, v + 24);
      if (r.tag === 0x11 && len >= 20 && v + 20 <= h.length) r.spb = rd16le(h, v + 18);
    } else if (id === 'fact' && len >= 4 && v + 4 <= h.length) {
      r.fact = rd32le(h, v);
    } else if (id === 'data') {
      r.dataOff = v;
      r.dataLen = Math.min(len, h.length - v);
      break;
    }
    p = v + len + (len & 1);
  }
  return r.tag && r.dataOff ? r : null;
};

const IMA_STEP = [7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80,
  88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
  876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
  5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086,
  29794, 32767];
const IMA_IDX = [-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8];

// Bloque tipico de Microsoft: 256 bytes por canal y por cada 11 kHz.
FX.imaBlockAlign = function (rate, ch) { return 256 * ch * Math.max(1, Math.floor(rate / 11025)); };
FX.imaSpb = function (ba, ch) { return ((ba - 4 * ch) * 8) / (4 * ch) + 1; };

function imaNib(st, nib) {
  const step = IMA_STEP[st.idx];
  let d = step >> 3;
  if (nib & 4) d += step;
  if (nib & 2) d += step >> 1;
  if (nib & 1) d += step >> 2;
  st.pred += (nib & 8) ? -d : d;
  if (st.pred > 32767) st.pred = 32767; else if (st.pred < -32768) st.pred = -32768;
  st.idx += IMA_IDX[nib];
  if (st.idx < 0) st.idx = 0; else if (st.idx > 88) st.idx = 88;
  return st.pred;
}

// Codifica PCM 16 bits MONO en bloques IMA ADPCM de Microsoft. El primer
// valor de cada bloque va en su cabecera; el indice de paso se arrastra de
// un bloque al siguiente. El ultimo bloque se completa (el 'fact' dice
// cuantas muestras son de verdad).
FX.imaEncode = function (pcm, ba) {
  const spb = FX.imaSpb(ba, 1);
  const blocks = Math.ceil(pcm.length / spb);
  const out = new Uint8Array(blocks * ba);
  const st = { pred: 0, idx: 0 };
  for (let b = 0; b < blocks; b++) {
    const base = b * spb, o = b * ba;
    const s0 = base < pcm.length ? pcm[base] : 0;
    st.pred = s0;
    out[o] = s0 & 0xFF; out[o + 1] = (s0 >> 8) & 0xFF; out[o + 2] = st.idx; out[o + 3] = 0;
    for (let i = 1; i < spb; i++) {
      const s = base + i < pcm.length ? pcm[base + i] : st.pred;
      let diff = s - st.pred, step = IMA_STEP[st.idx], nib = 0;
      if (diff < 0) { nib = 8; diff = -diff; }
      if (diff >= step) { nib |= 4; diff -= step; }
      step >>= 1;
      if (diff >= step) { nib |= 2; diff -= step; }
      step >>= 1;
      if (diff >= step) nib |= 1;
      imaNib(st, nib);
      const k = i - 1, at = o + 4 + (k >> 1);
      if (k & 1) out[at] |= nib << 4; else out[at] = nib;
    }
  }
  return out;
};

function wavHeader(dataLen, fmtChunk, extra) {
  const size = 12 + 8 + fmtChunk.length + (extra ? extra.length : 0) + 8;
  const b = new Uint8Array(size);
  const dv = new DataView(b.buffer);
  let p = 0;
  const s = (t) => { for (let i = 0; i < 4; i++) b[p++] = t.charCodeAt(i); };
  s('RIFF'); dv.setUint32(p, size - 8 + dataLen + (dataLen & 1), true); p += 4; s('WAVE');
  s('fmt '); dv.setUint32(p, fmtChunk.length, true); p += 4; b.set(fmtChunk, p); p += fmtChunk.length;
  if (extra) { b.set(extra, p); p += extra.length; }
  s('data'); dv.setUint32(p, dataLen, true);
  return b;
}

FX.pcmWav = function (pcm, rate, ch) {
  const fmt = new Uint8Array(16), dv = new DataView(fmt.buffer);
  dv.setUint16(0, 1, true); dv.setUint16(2, ch, true); dv.setUint32(4, rate, true);
  dv.setUint32(8, rate * ch * 2, true); dv.setUint16(12, ch * 2, true); dv.setUint16(14, 16, true);
  const data = new Uint8Array(pcm.length * 2), dd = new DataView(data.buffer);
  for (let i = 0; i < pcm.length; i++) dd.setInt16(i * 2, pcm[i], true);
  return FX.concat([wavHeader(data.length, fmt, null), data]);
};

FX.imaWav = function (pcm, rate) {
  const ba = FX.imaBlockAlign(rate, 1), spb = FX.imaSpb(ba, 1);
  const data = FX.imaEncode(pcm, ba);
  const fmt = new Uint8Array(20), dv = new DataView(fmt.buffer);
  dv.setUint16(0, 0x11, true); dv.setUint16(2, 1, true); dv.setUint32(4, rate, true);
  dv.setUint32(8, Math.floor(rate * ba / spb), true); dv.setUint16(12, ba, true); dv.setUint16(14, 4, true);
  dv.setUint16(16, 2, true); dv.setUint16(18, spb, true);
  const fact = new Uint8Array(12), fv = new DataView(fact.buffer);
  fact.set([0x66, 0x61, 0x63, 0x74]); fv.setUint32(4, 4, true); fv.setUint32(8, pcm.length, true);
  return FX.concat([wavHeader(data.length, fmt, fact), data]);
};

// PCM intercalado (Int16) a partir de un WAV que el P4 reproduce. null si no.
FX.wavDecode = function (u8) {
  const w = FX.wavInfo(u8);
  if (!w || w.ch < 1 || w.ch > 2) return null;
  const d = u8.subarray(w.dataOff, w.dataOff + w.dataLen);
  if (w.tag === 1 && w.bits === 16) {
    const n = Math.floor(d.length / 2), pcm = new Int16Array(n);
    for (let i = 0; i < n; i++) pcm[i] = (d[2 * i] | (d[2 * i + 1] << 8)) << 16 >> 16;
    return { rate: w.rate, ch: w.ch, pcm };
  }
  if (w.tag === 1 && w.bits === 8) {
    const pcm = new Int16Array(d.length);
    for (let i = 0; i < d.length; i++) pcm[i] = (d[i] - 128) << 8;
    return { rate: w.rate, ch: w.ch, pcm };
  }
  if (w.tag === 0x11 && w.bits === 4 && w.blockAlign > 4 * w.ch) {
    const ch = w.ch, ba = w.blockAlign, spb = FX.imaSpb(ba, ch);
    const blocks = Math.floor(d.length / ba);
    let frames = blocks * spb;
    if (w.fact && w.fact < frames) frames = w.fact;
    const pcm = new Int16Array(frames * ch);
    let f = 0;
    for (let b = 0; b < blocks && f < frames; b++) {
      const o = b * ba, st = [];
      for (let c = 0; c < ch; c++) {
        const s0 = (d[o + 4 * c] | (d[o + 4 * c + 1] << 8)) << 16 >> 16;
        st.push({ pred: s0, idx: Math.min(88, d[o + 4 * c + 2]) });
        pcm[f * ch + c] = s0;
      }
      f++;
      if (ch === 1) {
        for (let k = 0; k < spb - 1 && f < frames; k++) {
          const byte = d[o + 4 + (k >> 1)];
          pcm[f++] = imaNib(st[0], (k & 1) ? byte >> 4 : byte & 15);
        }
      } else {
        // Estereo: grupos de 4 bytes (8 muestras) de cada canal, alternando.
        const groups = (ba - 8) / 8;
        for (let g = 0; g < groups; g++) {
          for (let c = 0; c < 2; c++) {
            for (let k = 0; k < 8; k++) {
              const byte = d[o + 8 + g * 8 + c * 4 + (k >> 1)];
              const fi = f + g * 8 + k;
              const v = imaNib(st[c], (k & 1) ? byte >> 4 : byte & 15);
              if (fi < frames) pcm[fi * 2 + c] = v;
            }
          }
        }
        f = Math.min(frames, f + groups * 8);
      }
    }
    return { rate: w.rate, ch, pcm };
  }
  return null;
};

FX.floatToInt16 = function (f) {
  const out = new Int16Array(f.length);
  for (let i = 0; i < f.length; i++) {
    const s = f[i] > 1 ? 1 : f[i] < -1 ? -1 : f[i];
    out[i] = s < 0 ? Math.round(s * 32768) : Math.round(s * 32767);
  }
  return out;
};

// Mezcla a mono y cambia la frecuencia. Solo se usa si el navegador no tiene
// OfflineAudioContext: filtro de media movil (antialias) + interpolacion.
FX.resampleMono = function (chs, srcRate, dstRate, frames) {
  const n = chs[0].length;
  const mono = new Float32Array(n);
  for (const c of chs) for (let i = 0; i < n; i++) mono[i] += c[i] / chs.length;
  const ratio = srcRate / dstRate;
  let src = mono;
  const win = Math.floor(ratio);
  if (win >= 2) {
    src = new Float32Array(n);
    let acc = 0;
    for (let i = 0; i < n; i++) {
      acc += mono[i];
      if (i >= win) acc -= mono[i - win];
      src[i] = acc / Math.min(i + 1, win);
    }
  }
  const outN = frames != null ? frames : Math.floor(n / ratio);
  const out = new Float32Array(outN);
  const shift = win >= 2 ? (win - 1) / 2 : 0;         // retardo del filtro
  for (let i = 0; i < outN; i++) {
    const x = i * ratio + shift, k = Math.floor(x), t = x - k;
    const a = k < n ? src[k] : 0, b = k + 1 < n ? src[k + 1] : a;
    out[i] = a + (b - a) * t;
  }
  return out;
};

// ---------------------------------------------------------------------------
//  AVI MJPEG: lo unico de video que el P4 reproduce de verdad
// ---------------------------------------------------------------------------
FX.AVI_FRAME_MAX = 192 * 1024;     // = VID_FRAME_CAP / FLEXTH_AVI_FRAME en el P4
FX.aviOverhead = function (frames) { return 224 + 8 + frames * 24; };

FX.aviMux = function (frames, w, h, fps) {
  const n = frames.length;
  let maxF = 0, moviLen = 4;
  for (const f of frames) { if (f.length > maxF) maxF = f.length; moviLen += 8 + f.length + (f.length & 1); }
  const hdrlLen = 4 + 64 + 124;
  const riffLen = 4 + (8 + hdrlLen) + (8 + moviLen) + (8 + 16 * n);
  const head = new Uint8Array(224);
  const dv = new DataView(head.buffer);
  let p = 0;
  const s = (t) => { for (let i = 0; i < 4; i++) head[p++] = t.charCodeAt(i); };
  const u32 = (v) => { dv.setUint32(p, v >>> 0, true); p += 4; };
  const u16 = (v) => { dv.setUint16(p, v, true); p += 2; };
  s('RIFF'); u32(riffLen); s('AVI ');
  s('LIST'); u32(hdrlLen); s('hdrl');
  s('avih'); u32(56);
  u32(Math.round(1e6 / fps)); u32(Math.ceil(maxF * fps)); u32(0); u32(0x10); u32(n); u32(0); u32(1);
  u32(maxF + 8); u32(w); u32(h); u32(0); u32(0); u32(0); u32(0);
  s('LIST'); u32(4 + 64 + 48); s('strl');
  s('strh'); u32(56);
  s('vids'); s('MJPG'); u32(0); u16(0); u16(0); u32(0); u32(1); u32(fps); u32(0); u32(n); u32(maxF + 8);
  u32(0xFFFFFFFF); u32(0); u16(0); u16(0); u16(w); u16(h);
  s('strf'); u32(40);
  u32(40); u32(w); u32(h); u16(1); u16(24); s('MJPG'); u32(w * h * 3); u32(0); u32(0); u32(0); u32(0);
  s('LIST'); u32(moviLen); s('movi');
  const parts = [head];
  const idx = new Uint8Array(8 + 16 * n), iv = new DataView(idx.buffer);
  idx.set([0x69, 0x64, 0x78, 0x31]); iv.setUint32(4, 16 * n, true);
  let off = 4;
  for (let i = 0; i < n; i++) {
    const f = frames[i], ch = new Uint8Array(8), cv = new DataView(ch.buffer);
    ch.set([0x30, 0x30, 0x64, 0x63]); cv.setUint32(4, f.length, true);
    parts.push(ch, f);
    if (f.length & 1) parts.push(new Uint8Array(1));
    const e = 8 + 16 * i;
    idx.set([0x30, 0x30, 0x64, 0x63], e);
    iv.setUint32(e + 4, 0x10, true); iv.setUint32(e + 8, off, true); iv.setUint32(e + 12, f.length, true);
    off += 8 + f.length + (f.length & 1);
  }
  parts.push(idx);
  return parts;
};

// Indice de fotogramas de un AVI MJPEG (para verlo en el navegador).
FX.aviIndex = function (u8) {
  if (!(u8.length >= 12 && cc(u8, 0, 'RIFF') && cc(u8, 8, 'AVI '))) return null;
  const r = { w: 0, h: 0, us: 0, frames: [] };
  let p = 12, movi = -1, moviEnd = 0;
  while (p + 8 <= u8.length) {
    const id = str4(u8, p), len = rd32le(u8, p + 4);
    if (id === 'LIST') {
      const t = str4(u8, p + 8);
      if (t === 'movi') { movi = p + 12; moviEnd = Math.min(u8.length, p + 8 + len); p = p + 8 + len + (len & 1); continue; }
      p += 12; continue;
    }
    if (id === 'avih' && p + 8 + 40 <= u8.length) {
      r.us = rd32le(u8, p + 8); r.w = rd32le(u8, p + 8 + 32); r.h = rd32le(u8, p + 8 + 36);
    } else if (id === 'strh' && p + 8 + 28 <= u8.length && cc(u8, p + 8, 'vids')) {
      const scale = rd32le(u8, p + 8 + 20), rate = rd32le(u8, p + 8 + 24);
      if (scale && rate) r.us = Math.round(scale * 1e6 / rate);
    }
    if (len > u8.length) break;
    p = p + 8 + len + (len & 1);
  }
  if (movi < 0) return null;
  for (let q = movi; q + 8 <= moviEnd;) {
    const len = rd32le(u8, q + 4);
    if (q + 8 + len > moviEnd) break;
    if ((u8[q + 2] === 0x64 && (u8[q + 3] === 0x63 || u8[q + 3] === 0x62)) && u8[q + 8] === 0xFF && u8[q + 9] === 0xD8)
      r.frames.push({ off: q + 8, len });
    if (cc(u8, q, 'LIST')) { q += 12; continue; }
    q += 8 + len + (len & 1);
  }
  if (!r.us) r.us = 40000;
  return r;
};

// ---------------------------------------------------------------------------
//  Etiquetas de audio: titulo, artista, album y portada
// ---------------------------------------------------------------------------
function decodeText(bytes, enc) {
  let label = 'utf-8', b = bytes;
  if (enc === 0) label = 'iso-8859-1';
  else if (enc === 1) {
    if (b.length >= 2 && b[0] === 0xFE && b[1] === 0xFF) { label = 'utf-16be'; b = b.subarray(2); }
    else { label = 'utf-16le'; if (b.length >= 2 && b[0] === 0xFF && b[1] === 0xFE) b = b.subarray(2); }
  } else if (enc === 2) label = 'utf-16be';
  let s;
  try { s = new TextDecoder(label).decode(b); } catch (e) { s = ''; }
  const z = s.indexOf('\u0000');
  return (z >= 0 ? s.slice(0, z) : s).trim();
}
function termLen(b, p, end, wide) {
  if (wide) { for (let i = p; i + 1 < end; i += 2) if (b[i] === 0 && b[i + 1] === 0) return i - p; return end - p; }
  for (let i = p; i < end; i++) if (b[i] === 0) return i - p;
  return end - p;
}
function syncsafe(b, o) { return ((b[o] & 0x7F) << 21) | ((b[o + 1] & 0x7F) << 14) | ((b[o + 2] & 0x7F) << 7) | (b[o + 3] & 0x7F); }
function unsync(b) {
  const out = [];
  for (let i = 0; i < b.length; i++) { out.push(b[i]); if (b[i] === 0xFF && b[i + 1] === 0x00) i++; }
  return Uint8Array.from(out);
}

FX.id3Size = function (h) { return (h.length >= 10 && cc(h, 0, 'ID3')) ? 10 + syncsafe(h, 6) + ((h[5] & 0x10) ? 10 : 0) : 0; };

function id3(u8) {
  const ver = u8[3], flags = u8[5];
  let tag = u8.subarray(10, Math.min(u8.length, 10 + syncsafe(u8, 6)));
  if ((flags & 0x80) && ver < 4) tag = unsync(tag);
  let p = 0;
  if ((flags & 0x40) && ver === 3 && tag.length >= 4) p = 4 + rd32be(tag, 0);
  if ((flags & 0x40) && ver === 4 && tag.length >= 4) p = syncsafe(tag, 0);
  const out = {};
  const hl = ver === 2 ? 6 : 10;
  while (p + hl <= tag.length) {
    const id = ver === 2 ? String.fromCharCode(tag[p], tag[p + 1], tag[p + 2]) : str4(tag, p);
    if (!/^[A-Z0-9]{3,4}$/.test(id)) break;
    const size = ver === 2 ? rd24be(tag, p + 3) : ver === 4 ? syncsafe(tag, p + 4) : rd32be(tag, p + 4);
    const fflags = ver === 2 ? 0 : rd16be(tag, p + 8);
    let s = p + hl;
    const e = s + size;
    if (size <= 0 || e > tag.length) break;
    p = e;
    if (ver >= 3 && (fflags & (ver === 4 ? 0x000C : 0x00C0))) continue;   // comprimido o cifrado
    let fr = tag.subarray(s, e);
    if (ver === 4 && (fflags & 0x0001)) fr = fr.subarray(4);
    if (ver === 4 && (fflags & 0x0002)) fr = unsync(fr);
    if (fr.length < 2) continue;
    const enc = fr[0];
    const key = { TIT2: 'title', TT2: 'title', TPE1: 'artist', TP1: 'artist', TALB: 'album', TAL: 'album' }[id];
    if (key && !out[key]) { out[key] = decodeText(fr.subarray(1), enc); continue; }
    if ((id === 'APIC' || id === 'PIC') && !out.pic) {
      let q = 1, mime;
      if (id === 'PIC') { mime = String.fromCharCode(fr[1], fr[2], fr[3]).toUpperCase() === 'PNG' ? 'image/png' : 'image/jpeg'; q = 4; }
      else { const ml = termLen(fr, 1, fr.length, false); mime = String.fromCharCode.apply(null, fr.subarray(1, 1 + ml)).toLowerCase(); q = 2 + ml; }
      q += 1;                                            // tipo de imagen
      const wide = enc === 1 || enc === 2;
      q += termLen(fr, q, fr.length, wide) + (wide ? 2 : 1);
      if (q < fr.length) out.pic = { mime: /png/.test(mime) ? 'image/png' : 'image/jpeg', data: fr.slice(q) };
    }
  }
  return out;
}

// Recorre cajas MP4 entre [s, e).
function mp4Boxes(b, s, e, fn) {
  let p = s;
  while (p + 8 <= e) {
    let size = rd32be(b, p), hl = 8;
    const type = str4(b, p + 4);
    if (size === 1) { if (p + 16 > e) break; size = rd32be(b, p + 8) * 4294967296 + rd32be(b, p + 12); hl = 16; }
    else if (size === 0) size = e - p;
    if (size < hl || p + size > e) break;
    if (fn(type, p + hl, p + size) === false) return;
    p += size;
  }
}
function mp4Tags(b) {
  const out = {};
  const walk = (s, e, path) => mp4Boxes(b, s, e, (t, ds, de) => {
    if (path === '' && t === 'moov') walk(ds, de, 'moov');
    else if (path === 'moov' && t === 'udta') walk(ds, de, 'udta');
    else if (path === 'udta' && t === 'meta') walk(ds + 4, de, 'meta');
    else if (path === 'meta' && t === 'ilst') walk(ds, de, 'ilst');
    else if (path === 'ilst') {
      mp4Boxes(b, ds, de, (dt, xs, xe) => {
        if (dt !== 'data' || xe - xs < 8) return;
        const type = rd32be(b, xs) & 0xFFFFFF, v = b.subarray(xs + 8, xe);
        const key = { '©nam': 'title', '©ART': 'artist', '©alb': 'album' }[t];
        if (key && type === 1 && !out[key]) out[key] = decodeText(v, 3);
        if (t === 'covr' && !out.pic && (type === 13 || type === 14)) out.pic = { mime: type === 14 ? 'image/png' : 'image/jpeg', data: v.slice() };
      });
    }
  });
  walk(0, b.length, '');
  return out;
}
function flacTags(b) {
  const out = {};
  let p = 4;
  while (p + 4 <= b.length) {
    const last = b[p] & 0x80, type = b[p] & 0x7F, len = rd24be(b, p + 1), s = p + 4, e = s + len;
    if (e > b.length) break;
    if (type === 4 && len >= 8) {
      let q = s + 4 + rd32le(b, s);
      const n = q + 4 <= e ? rd32le(b, q) : 0;
      q += 4;
      for (let i = 0; i < n && q + 4 <= e; i++) {
        const l = rd32le(b, q), v = decodeText(b.subarray(q + 4, Math.min(e, q + 4 + l)), 3);
        q += 4 + l;
        const k = v.indexOf('=');
        if (k < 0) continue;
        const key = { TITLE: 'title', ARTIST: 'artist', ALBUM: 'album' }[v.slice(0, k).toUpperCase()];
        if (key && !out[key]) out[key] = v.slice(k + 1).trim();
      }
    } else if (type === 6 && len > 32 && !out.pic) {
      let q = s + 4;
      const ml = rd32be(b, q); q += 4;
      const mime = String.fromCharCode.apply(null, b.subarray(q, Math.min(e, q + ml))); q += ml;
      q += 4 + rd32be(b, q) + 16;
      const dl = q + 4 <= e ? rd32be(b, q) : 0; q += 4;
      if (dl && q + dl <= e) out.pic = { mime: /png/.test(mime) ? 'image/png' : 'image/jpeg', data: b.slice(q, q + dl) };
    }
    if (last) break;
    p = e;
  }
  return out;
}
FX.tags = function (u8, fmt) {
  let t = {};
  try {
    if (FX.id3Size(u8)) t = id3(u8);
    else if (fmt === 'm4a' || fmt === 'mp4') t = mp4Tags(u8);
    else if (fmt === 'flac' && cc(u8, 0, 'fLaC')) t = flacTags(u8);
  } catch (e) { t = {}; }
  return t;
};

// ---------------------------------------------------------------------------
//  Perfiles de conversion y plan de subida
// ---------------------------------------------------------------------------
// bpp = bits por pixel ESTIMADOS de un JPEG a esa calidad: solo sirven para
// anunciar un tamano aproximado ("≈") antes de convertir. El corte real se
// hace con los bytes reales que salen del codificador.
FX.PROFILES = {
  photo: {
    light: { max: 1280, q: 0.80, bpp: 1.3 },
    rec:   { max: 1600, q: 0.85, bpp: 1.7 },
    high:  { max: 2048, q: 0.90, bpp: 2.4 }
  },
  video: {
    light: { long: 480, fps: 10, q: 0.55, bpp: 1.1 },
    rec:   { long: 640, fps: 12, q: 0.62, bpp: 1.3 },
    high:  { long: 800, fps: 15, q: 0.70, bpp: 1.7 }
  },
  audio: {
    light: { rate: 16000, codec: 'ima' },
    rec:   { rate: 22050, codec: 'ima' },
    high:  { rate: 22050, codec: 'pcm' }
  }
};
FX.PROFILE_HINT = {
  light: 'Ocupa lo mínimo: fotos de 1280 px, vídeo de 480 px a 10 fps y audio a 16 kHz.',
  rec: 'El equilibrio para Flex OS: fotos de 1600 px, vídeo de 640 px a 12 fps y audio a 22 kHz.',
  high: 'Más calidad y más espacio: fotos de 2048 px, vídeo de 800 px a 15 fps y audio a 22 kHz sin comprimir.',
  orig: 'Se envía cada archivo tal cual. Lo que Flex OS no pueda abrir se guarda para descargarlo después.'
};

FX.fitBox = function (w, h, max) {
  if (w <= max && h <= max) return { w, h };
  const s = max / Math.max(w, h);
  return { w: Math.max(1, Math.round(w * s)), h: Math.max(1, Math.round(h * s)) };
};
// Video: lado largo acotado, multiplos de 8 (bloques JPEG enteros), nunca a mas.
FX.fitVideo = function (w, h, long) {
  const s = Math.min(1, long / Math.max(w, h));
  return { w: Math.max(16, Math.floor(w * s / 8) * 8), h: Math.max(16, Math.floor(h * s / 8) * 8) };
};
FX.coverRect = function (w, h) {
  const s = Math.min(w, h);
  return { x: Math.floor((w - s) / 2), y: Math.floor((h - s) / 2), s };
};

FX.estFrameBytes = function (w, h, Q) { return Math.round(w * h * Q.bpp / 8); };
FX.aviBytes = function (frames, frameBytes) { return FX.aviOverhead(frames) + frames * (frameBytes + 1); };
FX.aviMaxFrames = function (cap, frameBytes) { return Math.max(0, Math.floor((cap - FX.aviOverhead(0)) / (frameBytes + 25))); };
FX.wavBytes = function (ms, Q) {
  const n = Math.ceil(ms * Q.rate / 1000);
  if (Q.codec === 'pcm') return 44 + n * 2;
  const ba = FX.imaBlockAlign(Q.rate, 1), spb = FX.imaSpb(ba, 1);
  return 60 + Math.ceil(n / spb) * ba;
};
FX.wavMaxMs = function (cap, Q) {
  if (Q.codec === 'pcm') return Math.max(0, Math.floor(Math.floor((cap - 44) / 2) * 1000 / Q.rate));
  const ba = FX.imaBlockAlign(Q.rate, 1), spb = FX.imaSpb(ba, 1);
  return Math.max(0, Math.floor(Math.floor((cap - 60) / ba) * spb / Q.rate * 1000));
};

// Lo que el P4 pide libre para aceptar `size` bytes (flexMlCheckUpload).
FX.spaceNeed = function (size) { return size + Math.floor(size / 32) + 16384; };
// Cuanto puede ocupar un archivo con `free` libres, sin tocar la reserva.
FX.roomFor = function (free, reserve) { return Math.max(0, Math.floor((free - reserve - 16384) * 32 / 33)); };

const KIND_PL = { photo: 'fotos', video: 'vídeos', audio: 'audio' };

// Decide que se hace con un archivo. `f` = lo que se sabe de el (formato real,
// tamano, dimensiones, duracion, WAV); `room` = bytes que caben ahora mismo.
FX.plan = function (f, prof, lim, room) {
  const P = { act: 'none', kind: f.kind, est: 0, cutMs: 0, w: 0, h: 0, note: '', level: '', out: null };
  const F = FMT[f.fmt];
  const L = (lim && lim[f.kind]) || 0;
  if (!F || !f.kind) { P.level = 'bad'; P.note = 'Formato no reconocido: Flex OS no lo admite'; return P; }
  const cap = room == null ? L : Math.min(L, room);
  const asIs = (note, level) => {
    if (L && f.size > L) { P.level = 'bad'; P.note = 'Pesa ' + FX.fmtSize(f.size) + ' y el límite para ' + KIND_PL[f.kind] + ' es ' + FX.fmtSize(L); return P; }
    if (room != null && f.size > room) { P.level = 'bad'; P.note = 'No cabe: quedan ' + FX.fmtSize(Math.max(0, room)) + ' libres en Flex OS'; return P; }
    P.act = 'as-is'; P.out = f.fmt; P.est = f.size; P.w = f.w || 0; P.h = f.h || 0; P.note = note; P.level = level || '';
    return P;
  };
  if (prof === 'orig') {
    if (!F.play) return asIs('Se guardará tal cual: Flex OS no puede ' + (f.kind === 'audio' ? 'reproducir ' : 'mostrar ') + F.name + '. Podrás descargarlo desde aquí.', 'warn');
    if (f.fmt === 'jpeg' && f.orient > 1) return asIs('Se envía sin cambios (en Flex OS puede verse girada)', 'warn');
    return asIs('Compatible: se envía sin cambios');
  }
  if (f.kind === 'photo') {
    const Q = FX.PROFILES.photo[prof];
    const upright = !f.orient || f.orient === 1;
    if (f.fmt === 'jpeg' && upright && f.w && f.h && Math.max(f.w, f.h) <= Q.max) return asIs('Ya es compatible: se envía sin cambios');
    P.act = 'photo'; P.out = 'jpeg';
    let bits = [f.fmt === 'jpeg' ? 'JPEG' : F.name + ' → JPEG'];
    if (f.w && f.h) {
      const sw = f.orient >= 5 ? f.h : f.w, sh = f.orient >= 5 ? f.w : f.h;
      const o = FX.fitBox(sw, sh, Q.max);
      P.w = o.w; P.h = o.h;
      bits.push(o.w + '×' + o.h + (o.w < sw ? ' (desde ' + sw + '×' + sh + ')' : ''));
    }
    if (!upright) bits.push('enderezada');
    P.est = Math.round((P.w ? P.w * P.h : Q.max * Q.max * 0.75) * Q.bpp / 8);
    if (P.est > cap) { P.act = 'none'; P.level = 'bad'; P.note = 'No cabe: quedan ' + FX.fmtSize(Math.max(0, cap)) + ' libres en Flex OS'; return P; }
    P.note = bits.join(' · ');
    return P;
  }
  if (f.kind === 'video') {
    if (f.fmt === 'avi-mjpeg') return asIs('Ya es compatible (AVI MJPEG): se envía sin cambios');
    if (f.fmt === 'avi-other') { P.level = 'bad'; P.note = 'Este AVI no se puede convertir en el navegador. Elige «Original» para guardarlo tal cual.'; return P; }
    if (f.decodable === false) { P.level = 'bad'; P.note = 'Este navegador no puede abrir el vídeo para convertirlo. Elige «Original» para guardarlo tal cual.'; return P; }
    const Q = FX.PROFILES.video[prof];
    P.act = 'video'; P.out = 'avi-mjpeg';
    if (f.w && f.h) { const o = FX.fitVideo(f.w, f.h, Q.long); P.w = o.w; P.h = o.h; }
    const fb = FX.estFrameBytes(P.w || Q.long, P.h || Math.round(Q.long * 9 / 16), Q);
    let note = 'AVI MJPEG' + (P.w ? ' · ' + P.w + '×' + P.h : '') + ' · ' + Q.fps + ' fps';
    if (f.durMs) {
      const frames = Math.max(1, Math.ceil(f.durMs * Q.fps / 1000));
      P.est = FX.aviBytes(frames, fb);
      if (P.est > cap) {
        const mf = FX.aviMaxFrames(cap, fb);
        if (mf < Q.fps) { P.act = 'none'; P.level = 'bad'; P.note = 'No cabe: quedan ' + FX.fmtSize(Math.max(0, cap)) + ' libres en Flex OS'; return P; }
        P.cutMs = Math.floor(mf * 1000 / Q.fps);
        P.est = FX.aviBytes(mf, fb);
        note += ' · se enviarán los primeros ' + FX.fmtDur(P.cutMs) + ' de ' + FX.fmtDur(f.durMs);
        P.level = 'warn';
      }
    }
    P.note = note;
    return P;
  }
  if (f.kind === 'audio') {
    const Q = FX.PROFILES.audio[prof];
    if ((f.fmt === 'wav-pcm' || f.fmt === 'wav-adpcm') && f.wav && f.wav.ch === 1 && f.wav.rate <= Q.rate)
      return asIs('Ya es compatible: se envía sin cambios');
    if (f.decodable === false) { P.level = 'bad'; P.note = 'Este navegador no puede abrir el audio para convertirlo. Elige «Original» para guardarlo tal cual.'; return P; }
    P.act = 'audio'; P.out = Q.codec === 'ima' ? 'wav-adpcm' : 'wav-pcm';
    let note = 'WAV ' + (Q.codec === 'ima' ? 'IMA ADPCM' : 'PCM') + ' · ' + (Q.rate / 1000).toString().replace('.', ',') + ' kHz mono';
    if (f.durMs) {
      P.est = FX.wavBytes(f.durMs, Q);
      if (P.est > cap) {
        P.cutMs = FX.wavMaxMs(cap, Q);
        if (P.cutMs < 1000) { P.act = 'none'; P.level = 'bad'; P.note = 'No cabe: quedan ' + FX.fmtSize(Math.max(0, cap)) + ' libres en Flex OS'; return P; }
        P.est = FX.wavBytes(P.cutMs, Q);
        note += ' · se enviarán los primeros ' + FX.fmtDur(P.cutMs) + ' de ' + FX.fmtDur(f.durMs);
        P.level = 'warn';
      }
    }
    P.note = note;
    return P;
  }
  P.level = 'bad'; P.note = 'Tipo de archivo no admitido';
  return P;
};

// Plan de TODOS los archivos, descontando del espacio libre lo que ocupa cada
// uno en el orden en que se van a subir.
FX.planAll = function (files, prof, lib) {
  let free = lib.free;
  return files.map((f) => {
    const p = FX.plan(f, prof, lib.lim, FX.roomFor(free, lib.reserve));
    if (p.act !== 'none') free -= FX.spaceNeed(p.est || f.size);
    return p;
  });
};

// ---------------------------------------------------------------------------
//  Texto
// ---------------------------------------------------------------------------
FX.fmtSize = function (b) {
  b = Math.max(0, Math.floor(b));
  if (b >= 1048576) { const t = Math.floor((b * 10 + 524288) / 1048576); return Math.floor(t / 10) + ',' + (t % 10) + ' MB'; }
  return Math.ceil(b / 1024) + ' KB';
};
FX.fmtDur = function (ms) {
  const s = Math.max(0, Math.round(ms / 1000)), h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60), ss = s % 60;
  return (h ? h + ':' + String(m).padStart(2, '0') : String(m)) + ':' + String(ss).padStart(2, '0');
};
FX.utf8Len = function (s) { let n = 0; for (const ch of s) { const c = ch.codePointAt(0); n += c < 0x80 ? 1 : c < 0x800 ? 2 : c < 0x10000 ? 3 : 4; } return n; };
FX.utf8Trunc = function (s, max) {
  let out = '', n = 0;
  for (const ch of s) {
    const c = ch.codePointAt(0), l = c < 0x80 ? 1 : c < 0x800 ? 2 : c < 0x10000 ? 3 : 4;
    if (n + l > max) break;
    out += ch; n += l;
  }
  return out;
};
FX.stem = function (name) { const d = name.lastIndexOf('.'); return d > 0 ? name.slice(0, d) : name; };
// Nombre para el P4: el del usuario, con la extension del formato REAL y sin
// pasarse de los 63 bytes del catalogo (se recorta la raiz, no la extension).
FX.uploadName = function (name, ext) {
  const base = FX.stem(String(name || '').replace(/[\u0000-\u001f\u007f/\\]/g, '_')).trim() || 'Archivo';
  return FX.utf8Trunc(base, 63 - FX.utf8Len(ext)).trim() + ext;
};
// Cadena de consulta de /api/upload. El P4 no acepta mas de 511 bytes: si las
// etiquetas no caben enteras se acortan (primero album, luego artista).
FX.UPLOAD_QUERY_MAX = 500;
FX.uploadQuery = function (m) {
  let q = 'kind=' + m.kind + '&name=' + encodeURIComponent(m.name) + '&size=' + m.size + '&crc=' + (m.crc >>> 0);
  if (m.created) q += '&created=' + Math.floor(m.created);
  if (m.w && m.h) q += '&w=' + m.w + '&h=' + m.h;
  if (m.dur) q += '&dur=' + Math.round(m.dur);
  for (const [k, max] of [['title', 47], ['artist', 31], ['album', 31]]) {
    const v = m[k] ? String(m[k]) : '';
    for (let lim = max; v && lim > 0; lim -= 4) {
      const add = '&' + k + '=' + encodeURIComponent(FX.utf8Trunc(v, lim).trim());
      if (q.length + add.length <= FX.UPLOAD_QUERY_MAX) { q += add; break; }
    }
  }
  return q;
};

if (typeof module !== 'undefined' && module.exports) module.exports = FX;
if (typeof document === 'undefined') return;

// ===========================================================================
//  UI
// ===========================================================================
const $ = (id) => document.getElementById(id);
function el(tag, cls, text) {
  const e = document.createElement(tag);
  if (cls) e.className = cls;
  if (text != null) e.textContent = text;
  return e;
}
const S = {
  paired: false, owner: false, lockType: 0, up: true,
  rev: 0, items: [], byId: new Map(), free: 0, total: 0, reserve: 0, lim: { photo: 0, video: 0, audio: 0 },
  tab: 'all', selecting: false, picked: new Set(), painted: false,
  profile: 'rec', plan: null, queue: [], running: null, offline: false, pollMs: 4000
};
const KIND_LABEL = { photo: 'FOTO', video: 'VÍDEO', audio: 'AUDIO' };

// ---------- red ----------
async function api(path, opt) {
  opt = opt || {};
  const init = { method: opt.method || 'GET', credentials: 'same-origin', cache: 'no-store', headers: {} };
  if (init.method === 'POST') init.headers['X-Flex'] = '1';
  if (opt.form != null) { init.headers['Content-Type'] = 'application/x-www-form-urlencoded'; init.body = opt.form; }
  if (opt.body) { init.headers['Content-Type'] = opt.type || 'application/octet-stream'; init.body = opt.body; }
  let r;
  try { r = await fetch(path, init); } catch (e) { return { status: 0, json: null }; }
  let j = null;
  try { j = await r.json(); } catch (e) { j = null; }
  return { status: r.status, json: j };
}
function errText(r, fallback) {
  if (r.status === 0) return 'Sin conexión con Flex OS. Comprueba que el móvil sigue en la misma Wi‑Fi.';
  return (r.json && r.json.error) || fallback || ('Error ' + r.status);
}
function waitText(s) { s = Math.max(1, s | 0); return s >= 60 ? Math.ceil(s / 60) + ' min' : s + ' s'; }

// ---------- avisos ----------
let toastT = 0;
function toast(msg, ms) {
  const t = $('toast');
  t.textContent = msg;
  t.hidden = false;
  clearTimeout(toastT);
  toastT = setTimeout(() => { t.hidden = true; }, ms || 3200);
}

// ---------- capas (hoja, dialogo, visor) ----------
function scrim(on) { $('scrim').hidden = !on; }
function closeMenu() { $('menu').hidden = true; }
function closeLayers() {
  closeMenu();
  if (!$('loginDlg').hidden) closeLogin();
  if (!$('planSheet').hidden) closePlan();
}

// ===========================================================================
//  Emparejar
// ===========================================================================
function showPair(msg) {
  S.paired = false;
  $('libView').hidden = true;
  $('fab').hidden = true;
  $('pairView').hidden = false;
  $('menuBtn').hidden = true;
  $('meter').hidden = true;
  $('devSub').textContent = 'Sin conectar';
  $('pairErr').textContent = msg || '';
  setTimeout(() => $('pairCode').focus(), 50);
}
async function pair(code) {
  $('pairBtn').disabled = true;
  const r = await api('/api/pair', { method: 'POST', form: 'code=' + encodeURIComponent(code) });
  $('pairBtn').disabled = false;
  if (r.status === 200) return true;
  const j = r.json || {};
  let m;
  if (r.status === 429) m = 'Demasiados intentos. Espera ' + waitText(j.wait) + ' y vuelve a probar.';
  else if (r.status === 403 && j.left != null) m = 'Código incorrecto.' + (j.left > 0 ? ' Quedan ' + j.left + ' intento' + (j.left === 1 ? '' : 's') + '.' : '');
  else m = errText(r, 'No se pudo conectar');
  $('pairErr').textContent = m;
  $('pairCode').value = '';
  const card = document.querySelector('.pair-card');
  card.classList.remove('shake'); void card.offsetWidth; card.classList.add('shake');
  return false;
}
$('pairForm').addEventListener('submit', async (e) => {
  e.preventDefault();
  const code = $('pairCode').value.replace(/\D/g, '');
  if (code.length !== 6) { $('pairErr').textContent = 'El código tiene 6 cifras.'; return; }
  if (await pair(code)) startLibrary(true);
});
$('pairCode').addEventListener('input', () => {
  const v = $('pairCode').value.replace(/\D/g, '').slice(0, 6);
  $('pairCode').value = v;
  $('pairErr').textContent = '';
  if (v.length === 6) $('pairForm').requestSubmit ? $('pairForm').requestSubmit() : $('pairBtn').click();
});

// ===========================================================================
//  Biblioteca
// ===========================================================================
function startLibrary(fresh) {
  S.paired = true;
  $('pairView').hidden = true;
  $('libView').hidden = false;
  $('menuBtn').hidden = false;
  $('meter').hidden = false;
  if (fresh) toast('Conectado con Flex OS');
  if (!S.painted) {
    // Mientras llega la primera respuesta: huecos con brillo, no una pantalla vacia.
    const g = $('grid');
    g.textContent = '';
    for (let i = 0; i < 6; i++) { const c = el('div', 'card skel'); c.append(el('div', 'shimmer')); g.append(c); }
  }
  refresh(true);
  schedulePoll();
}
function lostSession() {
  for (const j of S.queue) j.cancelled = true;
  S.queue = [];
  updateQueueCard();
  S.rev = 0; S.items = []; S.byId.clear(); S.owner = false;
  clearGrid();
  exitSelect();
  showPair('La sesión ha caducado o Flex OS se ha reiniciado. Escribe el código que aparece ahora en su pantalla.');
}

let refreshing = false;
async function refresh(full) {
  if (refreshing || !S.paired) return;
  refreshing = true;
  try {
    for (let pass = 0; pass < 2; pass++) {
      const r = await api('/api/library' + (!full && S.rev ? '?since=' + S.rev : ''));
      if (r.status === 401) { lostSession(); return; }
      if (r.status !== 200 || !r.json) { setOffline(r.status === 0); return; }
      setOffline(false);
      const j = r.json;
      // Sin cambios en el catalogo, pero quiza si en el nivel de propietario
      // (caduco o Flex OS se bloqueo): entonces hace falta la lista entera.
      if (j.same) { if (!!j.owner === S.owner) return; full = true; continue; }
      const wasOwner = S.owner;
      S.rev = j.rev; S.owner = !!j.owner; S.lockType = j.lockType | 0; S.up = !!j.up;
      S.free = j.free; S.total = j.total; S.reserve = j.reserve; S.lim = j.lim || S.lim;
      S.items = (j.items || []).filter((it) => !it.k || it.k === 'photo' || it.k === 'video' || it.k === 'audio');
      S.byId = new Map(S.items.map((it) => [it.id, it]));
      if (wasOwner && !S.owner) toast('El contenido protegido vuelve a estar oculto');
      render();
      return;
    }
  } finally {
    refreshing = false;
  }
}
function setOffline(off) {
  S.offline = off;
  S.pollMs = off ? Math.min(20000, S.pollMs * 2) : 4000;
  updateHeader();
}
let pollT = 0;
function schedulePoll() {
  clearTimeout(pollT);
  pollT = setTimeout(async () => {
    if (S.paired && document.visibilityState === 'visible' && !S.running) await refresh(false);
    schedulePoll();
  }, S.pollMs);
}
document.addEventListener('visibilitychange', () => { if (document.visibilityState === 'visible' && S.paired) refresh(false); });

function updateHeader() {
  const n = S.items.length;
  $('devSub').textContent = S.offline ? 'Sin conexión · reintentando' :
    (n ? n + ' elemento' + (n === 1 ? '' : 's') : 'Biblioteca vacía');
  if (S.total) {
    const used = Math.max(0, S.total - S.free);
    $('meterFill').style.width = Math.min(100, Math.round(used * 100 / S.total)) + '%';
    $('meterTxt').textContent = FX.fmtSize(Math.max(0, S.free - S.reserve)) + ' libres';
  }
  $('fab').hidden = !S.paired || !S.up || S.selecting;
  $('mOwner').hidden = S.owner;
  $('mLock').hidden = !S.owner;
  $('ownerChip').hidden = !S.owner;
}

// ---------- tarjetas ----------
const cards = new Map();
function clearGrid() { cards.clear(); $('grid').textContent = ''; S.painted = false; }
function redacted(it) { return !!it.lock && !it.k; }
function visibleItems() {
  return S.items.filter((it) => redacted(it) ? S.tab === 'all' : (S.tab === 'all' || it.k === S.tab))
    .sort((a, b) => (b.a || 0) - (a.a || 0) || b.id - a.id);
}
function sig(it) { return [it.lock, it.k, it.t, it.st, it.p, it.n, it.ar, it.d].join('|'); }
// 't' es la VERSION de la miniatura y empieza en 0: lo que dice si hay
// miniatura es que el campo exista, no su valor.
function hasThumb(it) { return it.t != null; }

function waveBars(seed) {
  const w = el('div', 'wave');
  let x = seed * 2654435761 >>> 0;
  for (let i = 0; i < 9; i++) {
    x = (x * 1103515245 + 12345) >>> 0;
    const b = el('i');
    b.style.height = (28 + (x >>> 24) % 62) + '%';
    w.append(b);
  }
  return w;
}

function makeCard(it, isNew) {
  const c = el('button', 'card' + (isNew ? ' new' : ''));
  c.type = 'button';
  c.dataset.id = it.id;
  c.append(el('span', 'sel'));
  if (redacted(it)) {
    c.classList.add('locked');
    c.append(el('span', 'lock-ico'), el('span', 'cap', 'Protegido'));
    c.setAttribute('aria-label', 'Elemento protegido');
    return c;
  }
  if (it.k === 'audio') c.classList.add('audio');
  if (hasThumb(it)) {
    const sh = el('div', 'shimmer');
    const img = new Image();
    img.alt = '';
    img.decoding = 'async';
    img.loading = 'lazy';
    img.onload = () => { img.classList.add('ok'); sh.remove(); };
    img.onerror = () => { img.remove(); sh.remove(); };
    img.src = '/api/thumb/' + it.id + '?v=' + it.t;
    c.append(sh, img);
  } else if (it.k === 'audio') {
    c.append(waveBars(it.id));
  } else {
    c.append(el('span', 'ph', it.fmt || ''));
  }
  if (it.k === 'video') c.append(el('span', 'play'));
  if (it.st === 'proc') c.append(el('div', 'shimmer'));
  if (it.st === 'err') c.append(el('span', 'badge warn', 'Error'));
  else if (!it.p) c.append(el('span', 'badge warn', 'Solo descarga'));
  else if (it.d && (it.k === 'video' || it.k === 'audio')) c.append(el('span', 'badge', FX.fmtDur(it.d)));
  if (it.lock) { const b = el('span', 'badge lockb'); b.append(el('span', 'lock-ico mini')); c.append(b); }
  const nm = el('span', 'name', it.n || '');
  if (it.k === 'audio' && it.ar) { nm.append(el('small', null, it.ar)); }
  c.append(nm);
  c.setAttribute('aria-label', (KIND_LABEL[it.k] || '') + ' ' + (it.n || ''));
  return c;
}

function render() {
  updateHeader();
  const grid = $('grid');
  if (!S.painted) grid.textContent = '';                 // fuera el esqueleto
  const vis = visibleItems();
  const keep = new Set();
  let prev = null;
  for (const it of vis) {
    keep.add(it.id);
    let c = cards.get(it.id);
    const s = sig(it);
    if (!c || c.sig !== s) {
      const node = makeCard(it, !c && S.painted);
      if (c) c.el.replaceWith(node);
      c = { el: node, sig: s };
      cards.set(it.id, c);
    }
    const want = prev ? prev.nextSibling : grid.firstChild;
    if (c.el !== want) grid.insertBefore(c.el, want);
    c.el.classList.toggle('picked', S.picked.has(it.id));
    prev = c.el;
  }
  for (const [id, c] of cards) if (!keep.has(id)) { c.el.remove(); cards.delete(id); }
  for (const id of Array.from(S.picked)) if (!keep.has(id)) S.picked.delete(id);
  const empty = vis.length === 0;
  $('empty').hidden = !empty;
  if (empty) {
    const t = { all: 'Aún no hay nada aquí', photo: 'Sin fotos', video: 'Sin vídeos', audio: 'Sin música' }[S.tab];
    $('emptyTitle').textContent = t;
    $('emptyText').textContent = S.up ? 'Sube fotos, vídeos o música desde este móvil con el botón Subir.' :
      'Las subidas están desactivadas en Flex OS.';
  }
  S.painted = true;
  if (S.selecting) updateSelbar();
}

$('tabs').addEventListener('click', (e) => {
  const b = e.target.closest('button[data-tab]');
  if (!b || b.dataset.tab === S.tab) return;
  S.tab = b.dataset.tab;
  for (const x of $('tabs').children) { x.classList.toggle('on', x === b); x.setAttribute('aria-selected', x === b ? 'true' : 'false'); }
  render();
});

// ---------- toque, pulsacion larga, seleccion ----------
let lp = null, lpFired = false;
$('grid').addEventListener('pointerdown', (e) => {
  const c = e.target.closest('.card');
  if (!c || (e.pointerType === 'mouse' && e.button !== 0)) return;
  lpFired = false;
  const id = +c.dataset.id;
  lp = { id, x: e.clientX, y: e.clientY, t: setTimeout(() => { lpFired = true; longPress(id); }, 480) };
});
$('grid').addEventListener('pointermove', (e) => {
  if (lp && Math.hypot(e.clientX - lp.x, e.clientY - lp.y) > 10) { clearTimeout(lp.t); lp = null; }
});
for (const ev of ['pointerup', 'pointercancel', 'pointerleave']) $('grid').addEventListener(ev, () => { if (lp) { clearTimeout(lp.t); lp = null; } });
$('grid').addEventListener('contextmenu', (e) => {
  const c = e.target.closest('.card');
  if (!c) return;
  e.preventDefault();
  lpFired = true;
  longPress(+c.dataset.id);
});
$('grid').addEventListener('click', (e) => {
  const c = e.target.closest('.card');
  if (!c) return;
  if (lpFired) { lpFired = false; return; }
  tap(+c.dataset.id);
});

function selectable(it) { return it && !redacted(it); }
function longPress(id) {
  const it = S.byId.get(id);
  if (!selectable(it)) { tap(id); return; }
  if (navigator.vibrate) { try { navigator.vibrate(12); } catch (e) { /* sin vibracion */ } }
  if (!S.selecting) enterSelect();
  S.picked.add(id);
  render();
}
function tap(id) {
  const it = S.byId.get(id);
  if (!it) return;
  if (redacted(it)) { openLogin(); return; }
  if (S.selecting) {
    if (S.picked.has(id)) S.picked.delete(id); else S.picked.add(id);
    const c = cards.get(id);
    if (c) c.el.classList.toggle('picked', S.picked.has(id));
    updateSelbar();
    return;
  }
  openViewer(it);
}
function enterSelect() {
  S.selecting = true;
  document.body.classList.add('selecting');
  $('selbar').hidden = false;
  $('selActions').hidden = false;
  $('tabs').hidden = true;
  $('fab').hidden = true;
  updateSelbar();
}
function exitSelect() {
  S.selecting = false;
  S.picked.clear();
  document.body.classList.remove('selecting');
  $('selbar').hidden = true;
  $('selActions').hidden = true;
  $('tabs').hidden = false;
  for (const c of cards.values()) c.el.classList.remove('picked');
  updateHeader();
}
function updateSelbar() {
  const n = S.picked.size;
  $('selCount').textContent = n === 1 ? '1 seleccionado' : n + ' seleccionados';
  const all = visibleItems().filter(selectable);
  const every = all.length > 0 && all.every((it) => S.picked.has(it.id));
  $('selAll').textContent = every ? 'Quitar selección' : 'Seleccionar todo';
  $('selAll').disabled = all.length === 0;
  $('selDl').disabled = n === 0;
}
$('selClose').addEventListener('click', exitSelect);
$('selAll').addEventListener('click', () => {
  const all = visibleItems().filter(selectable);
  const every = all.every((it) => S.picked.has(it.id));
  if (every) S.picked.clear(); else for (const it of all) S.picked.add(it.id);
  render();
});
$('selDl').addEventListener('click', () => {
  const ids = Array.from(S.picked).filter((id) => selectable(S.byId.get(id)));
  if (!ids.length) return;
  if (ids.length > 64) { toast('Como máximo 64 elementos por descarga'); return; }
  download(ids);
  exitSelect();
});
function download(ids) {
  const a = el('a');
  a.href = ids.length === 1 ? '/api/file/' + ids[0] + '?dl=1' : '/api/zip?ids=' + ids.join(',');
  a.download = '';
  a.rel = 'noopener';
  document.body.append(a);
  a.click();
  a.remove();
  toast(ids.length === 1 ? 'Descargando…' : 'Preparando un ZIP con ' + ids.length + ' archivos…');
}

// ===========================================================================
//  Visor
// ===========================================================================
let viewerStop = null;
function openViewer(it) {
  const body = $('viewerBody');
  body.textContent = '';
  $('viewerName').textContent = it.n || '';
  const info = [it.fmt];
  if (it.w && it.h) info.push(it.w + '×' + it.h);
  if (it.d) info.push(FX.fmtDur(it.d));
  info.push(FX.fmtSize(it.s || 0));
  if (it.k === 'audio' && it.ar) info.unshift(it.ar);
  $('viewerInfo').textContent = info.filter(Boolean).join(' · ');
  $('viewerDl').href = '/api/file/' + it.id + '?dl=1';
  $('viewerDl').setAttribute('download', '');
  const src = '/api/file/' + it.id;
  const note = (t) => body.append(el('p', 'note', t));
  if (it.k === 'photo') {
    const img = new Image();
    img.alt = it.n || '';
    img.onerror = () => { img.remove(); note('Este navegador no puede mostrar ' + it.fmt + '. Descárgalo para abrirlo con otra aplicación.'); };
    img.src = src;
    body.append(img);
  } else if (it.k === 'video' && /^AVI/.test(it.fmt || '')) {
    if (it.p) playMjpeg(it, body); else note('Este AVI no usa MJPEG: ni Flex OS ni el navegador pueden reproducirlo. Puedes descargarlo.');
  } else if (it.k === 'video') {
    const v = el('video');
    v.controls = true; v.playsInline = true; v.preload = 'metadata';
    v.onerror = () => { v.remove(); note('Este navegador no puede reproducir ' + it.fmt + '. Descárgalo para verlo.'); };
    v.src = src;
    body.append(v);
  } else if (it.k === 'audio') {
    if (hasThumb(it)) { const img = new Image(); img.alt = ''; img.className = 'cover'; img.src = '/api/thumb/' + it.id + '?v=' + it.t; body.append(img); }
    if (it.fmt === 'WAV IMA ADPCM') playIma(it, body);
    else {
      const a = el('audio');
      a.controls = true; a.preload = 'metadata';
      a.onerror = () => { a.remove(); note('Este navegador no puede reproducir ' + it.fmt + '. Descárgalo para escucharlo.'); };
      a.src = src;
      body.append(a);
    }
  }
  $('viewer').hidden = false;
  history.pushState({ viewer: it.id }, '');
}
function closeViewer() {
  if ($('viewer').hidden) return;
  if (viewerStop) { viewerStop(); viewerStop = null; }
  for (const m of $('viewerBody').querySelectorAll('video,audio')) { m.pause(); m.removeAttribute('src'); m.load(); }
  $('viewerBody').textContent = '';
  $('viewer').hidden = true;
}
$('viewerClose').addEventListener('click', () => { if (history.state && history.state.viewer) history.back(); else closeViewer(); });
window.addEventListener('popstate', closeViewer);

// Descarga con progreso real (para los formatos que el navegador no abre solo).
async function fetchBytes(url, onProg) {
  const r = await fetch(url, { credentials: 'same-origin' });
  if (!r.ok) throw new Error(r.status === 403 ? 'Contenido protegido' : 'No se pudo leer el archivo (' + r.status + ')');
  const total = +r.headers.get('Content-Length') || 0;
  if (!r.body || !r.body.getReader) return new Uint8Array(await r.arrayBuffer());
  const rd = r.body.getReader(), parts = [];
  let got = 0;
  for (;;) {
    const x = await rd.read();
    if (x.done) break;
    parts.push(x.value); got += x.value.length;
    if (onProg && total) onProg(got / total);
  }
  return FX.concat(parts);
}
function loadingNote(body) {
  const p = el('p', 'note', 'Cargando…');
  body.append(p);
  return (f) => { p.textContent = 'Cargando… ' + Math.round(f * 100) + ' %'; };
}

// AVI MJPEG en el navegador: cada fotograma es un JPEG y se pinta en un lienzo.
async function playMjpeg(it, body) {
  let stopped = false, timer = 0;
  viewerStop = () => { stopped = true; clearTimeout(timer); };
  const prog = loadingNote(body);
  let u8, ix;
  try { u8 = await fetchBytes('/api/file/' + it.id, prog); ix = FX.aviIndex(u8); } catch (e) { body.textContent = ''; body.append(el('p', 'note', e.message)); return; }
  if (stopped) return;
  body.textContent = '';
  if (!ix || !ix.frames.length) { body.append(el('p', 'note', 'El vídeo no tiene fotogramas legibles.')); return; }
  const cv = el('canvas');
  cv.width = ix.w || it.w || 320; cv.height = ix.h || it.h || 240;
  const g = cv.getContext('2d');
  const btn = el('button', 'vctl', 'Pausa');
  body.append(cv, btn);
  let i = 0, playing = true, t0 = performance.now(), f0 = 0;
  const frameAt = async (k) => {
    const f = ix.frames[k];
    const bmp = await createImageBitmap(new Blob([u8.subarray(f.off, f.off + f.len)], { type: 'image/jpeg' }));
    if (cv.width !== bmp.width || cv.height !== bmp.height) { cv.width = bmp.width; cv.height = bmp.height; }
    g.drawImage(bmp, 0, 0);
    if (bmp.close) bmp.close();
  };
  const tick = async () => {
    if (stopped || !playing) return;
    const due = f0 + Math.floor((performance.now() - t0) * 1000 / ix.us);
    i = Math.min(due, ix.frames.length - 1);
    try { await frameAt(i); } catch (e) { /* un fotograma roto se salta */ }
    if (i >= ix.frames.length - 1) { playing = false; btn.textContent = 'Repetir'; return; }
    timer = setTimeout(tick, Math.max(5, ix.us / 1000 - 4));
  };
  btn.addEventListener('click', () => {
    if (playing) { playing = false; clearTimeout(timer); btn.textContent = 'Seguir'; return; }
    if (i >= ix.frames.length - 1) i = 0;
    playing = true; f0 = i; t0 = performance.now(); btn.textContent = 'Pausa';
    tick();
  });
  tick();
}

// WAV IMA ADPCM: el navegador no lo reproduce solo; se decodifica aqui.
async function playIma(it, body) {
  let ac = null;
  viewerStop = () => { if (ac) ac.close(); ac = null; };
  const prog = loadingNote(body);
  let dec;
  try { dec = FX.wavDecode(await fetchBytes('/api/file/' + it.id, prog)); } catch (e) { dec = null; }
  const n = body.querySelector('.note');
  if (n) n.remove();
  if (!dec) { body.append(el('p', 'note', 'No se pudo leer el audio.')); return; }
  const btn = el('button', 'vctl', 'Reproducir');
  body.append(btn);
  let srcNode = null;
  btn.addEventListener('click', () => {
    if (srcNode) { srcNode.stop(); srcNode = null; btn.textContent = 'Reproducir'; return; }
    const AC = window.AudioContext || window.webkitAudioContext;
    if (!ac) ac = new AC();
    const frames = dec.pcm.length / dec.ch;
    const buf = ac.createBuffer(dec.ch, frames, dec.rate);
    for (let c = 0; c < dec.ch; c++) {
      const d = buf.getChannelData(c);
      for (let i = 0; i < frames; i++) d[i] = dec.pcm[i * dec.ch + c] / 32768;
    }
    srcNode = ac.createBufferSource();
    srcNode.buffer = buf;
    srcNode.connect(ac.destination);
    srcNode.onended = () => { srcNode = null; btn.textContent = 'Reproducir'; };
    srcNode.start();
    btn.textContent = 'Detener';
  });
}

// ===========================================================================
//  Contenido protegido (PIN o contrasena DEL SISTEMA)
// ===========================================================================
function openLogin() {
  closeMenu();
  if (!S.lockType) {
    toast('Flex OS no tiene PIN ni contraseña. Configúralos en Ajustes › Seguridad para proteger contenido.', 4500);
    return;
  }
  const pin = S.lockType === 1;
  $('loginText').textContent = 'Introduce ' + (pin ? 'el PIN' : 'la contraseña') + ' de Flex OS para ver el contenido protegido. Se volverá a ocultar tras 5 minutos sin uso o al bloquear Flex OS.';
  $('loginSecret').setAttribute('inputmode', pin ? 'numeric' : 'text');
  $('loginSecret').setAttribute('aria-label', pin ? 'PIN' : 'Contraseña');
  $('loginErr').textContent = '';
  $('loginSecret').value = '';
  $('loginDlg').hidden = false;
  scrim(true);
  setTimeout(() => $('loginSecret').focus(), 60);
}
function closeLogin() {
  $('loginSecret').value = '';
  $('loginDlg').hidden = true;
  scrim(false);
}
$('loginCancel').addEventListener('click', closeLogin);
$('loginForm').addEventListener('submit', async (e) => {
  e.preventDefault();
  let secret = $('loginSecret').value;
  $('loginSecret').value = '';
  if (!secret) return;
  $('loginGo').disabled = true;
  const r = await api('/api/login', { method: 'POST', form: 'secret=' + encodeURIComponent(secret) });
  secret = '';
  $('loginGo').disabled = false;
  if (r.status === 200) {
    closeLogin();
    toast('Contenido protegido visible');
    await refresh(true);
    return;
  }
  const j = r.json || {};
  let m;
  if (r.status === 401 && j.pair) { closeLogin(); lostSession(); return; }
  if (r.status === 429) m = 'Demasiados intentos. Espera ' + waitText(j.wait) + '.';
  else if (r.status === 401) m = (S.lockType === 1 ? 'PIN incorrecto.' : 'Contraseña incorrecta.') + (j.left > 0 ? ' Quedan ' + j.left + ' intento' + (j.left === 1 ? '' : 's') + '.' : '');
  else if (r.status === 409) { closeLogin(); S.lockType = 0; toast(errText(r)); return; }
  else m = errText(r, 'No se pudo comprobar');
  $('loginErr').textContent = m;
  const d = $('loginDlg');
  d.classList.remove('shake'); void d.offsetWidth; d.classList.add('shake');
});
async function hideOwner() {
  await api('/api/logout', { method: 'POST', form: '' });
  await refresh(true);
}
$('ownerOff').addEventListener('click', hideOwner);

// ===========================================================================
//  Menu
// ===========================================================================
$('menuBtn').addEventListener('click', (e) => { e.stopPropagation(); $('menu').hidden = !$('menu').hidden; });
document.addEventListener('click', (e) => { if (!$('menu').hidden && !e.target.closest('#menu')) closeMenu(); });
$('mOwner').addEventListener('click', openLogin);
$('mLock').addEventListener('click', () => { closeMenu(); hideOwner(); });
$('mRefresh').addEventListener('click', () => { closeMenu(); refresh(true); });
$('mUnpair').addEventListener('click', async () => {
  closeMenu();
  if (S.running) { toast('Espera a que terminen las transferencias'); return; }
  await api('/api/unpair', { method: 'POST', form: '' });
  S.rev = 0; S.items = []; S.byId.clear(); clearGrid(); exitSelect();
  showPair('Este móvil se ha desconectado de Flex OS.');
});
document.addEventListener('keydown', (e) => {
  if (e.key !== 'Escape') return;
  if (!$('viewer').hidden) { $('viewerClose').click(); return; }
  if (!$('loginDlg').hidden || !$('planSheet').hidden || !$('menu').hidden) { closeLayers(); return; }
  if (S.selecting) exitSelect();
});
$('scrim').addEventListener('click', closeLayers);

// ===========================================================================
//  Subir: analisis, plan y conversion en el movil
// ===========================================================================
$('fab').addEventListener('click', () => { if (S.selecting) exitSelect(); $('picker').click(); });
$('picker').addEventListener('change', () => {
  const files = Array.from($('picker').files || []);
  $('picker').value = '';
  if (files.length) preparePlan(files);
});

function once(t, ok, bad, ms) {
  return new Promise((res, rej) => {
    let timer = 0;
    const done = (fn, v) => { clearTimeout(timer); t.removeEventListener(ok, a); if (bad) t.removeEventListener(bad, b); fn(v); };
    const a = () => done(res);
    const b = () => done(rej, new Error('error'));
    t.addEventListener(ok, a);
    if (bad) t.addEventListener(bad, b);
    if (ms) timer = setTimeout(() => done(rej, new Error('timeout')), ms);
  });
}
function mediaEl(kind, url) {
  const m = el(kind === 'video' ? 'video' : 'audio');
  m.muted = true; m.preload = 'metadata';
  if (kind === 'video') m.playsInline = true;
  m.src = url;
  return m;
}
async function readHead(file, n) { return new Uint8Array(await file.slice(0, n).arrayBuffer()); }
// Los WebM que graban los navegadores (MediaRecorder) no dicen su duracion:
// el elemento da Infinity hasta que se le pide ir al final.
async function fixDuration(m) {
  if (isFinite(m.duration)) return;
  const ok = new Promise((res) => {
    const t = setTimeout(res, 6000);
    const f = () => { if (isFinite(m.duration)) { clearTimeout(t); m.removeEventListener('durationchange', f); res(); } };
    m.addEventListener('durationchange', f);
  });
  m.currentTime = 1e101;
  await ok;
  m.currentTime = 0;
}

// Etiquetas de audio: el ID3 va al principio; el 'moov' de un M4A puede ir al
// final (se buscan las cajas de primer nivel sin leer el archivo entero).
async function readTags(file, fmt, head) {
  try {
    const n = FX.id3Size(head);
    if (n) return FX.tags(n <= head.length ? head : await readHead(file, Math.min(n, 8 << 20)), fmt);
    if (fmt === 'flac') return FX.tags(head, fmt);
    if (fmt === 'm4a' || fmt === 'mp4') {
      let p = 0;
      for (let k = 0; k < 64 && p + 8 <= file.size; k++) {
        const h = new Uint8Array(await file.slice(p, p + 16).arrayBuffer());
        let size = rd32be(h, 0);
        if (size === 1) size = rd32be(h, 8) * 4294967296 + rd32be(h, 12);
        else if (size === 0) size = file.size - p;
        if (size < 8) break;
        if (cc(h, 4, 'moov')) return size <= (8 << 20) ? FX.tags(await readHead(file.slice(p, p + size), size), fmt) : {};
        p += size;
      }
    }
  } catch (e) { /* sin etiquetas */ }
  return {};
}

async function analyze(file) {
  const head = await readHead(file, 256 * 1024);
  const fmt = FX.sniff(head);
  const f = { file, name: file.name || 'Archivo', size: file.size, fmt, kind: FX.kindOf(fmt), w: 0, h: 0, orient: 1,
    durMs: 0, wav: null, taken: null, tags: {}, decodable: null, created: Math.floor((file.lastModified || Date.now()) / 1000) };
  if (!f.kind) return f;
  if (f.kind === 'photo') {
    const d = FX.imageDims(head, fmt);
    if (d) { f.w = d.w; f.h = d.h; }
    if (fmt === 'jpeg' || fmt === 'jpeg-prog') {
      const j = FX.jpegInfo(head);
      f.orient = j.orient;
      if (j.taken) f.created = Math.floor(new Date(j.taken.y, j.taken.mo - 1, j.taken.d, j.taken.h, j.taken.mi, j.taken.s).getTime() / 1000);
    }
    return f;
  }
  if (fmt === 'wav-pcm' || fmt === 'wav-adpcm' || fmt === 'wav-other') {
    const w = FX.wavInfo(head);
    if (w) {
      f.wav = { ch: w.ch, rate: w.rate };
      const frames = w.tag === 0x11 ? (w.fact || Math.floor((file.size - w.dataOff) / w.blockAlign) * w.spb) :
        Math.floor((file.size - w.dataOff) / Math.max(1, w.blockAlign));
      if (w.rate) f.durMs = Math.round(frames * 1000 / w.rate);
    }
  }
  if (f.kind === 'audio') f.tags = await readTags(file, fmt, head);
  if (fmt === 'avi-mjpeg' || fmt === 'avi-other') return f;
  // Los WAV que reproduce Flex OS los sabe leer esta pagina aunque el
  // navegador no (IMA ADPCM): no hace falta preguntarle.
  if (fmt === 'wav-pcm' || fmt === 'wav-adpcm') { f.decodable = true; return f; }
  // Duracion y tamano: los da el propio navegador, y de paso dice si sabe abrirlo.
  const url = URL.createObjectURL(file);
  const m = mediaEl(f.kind, url);
  try {
    await once(m, 'loadedmetadata', 'error', 10000);
    await fixDuration(m);
    if (isFinite(m.duration)) f.durMs = Math.round(m.duration * 1000);
    if (f.kind === 'video') { f.w = m.videoWidth; f.h = m.videoHeight; f.decodable = f.w > 0 && isFinite(m.duration); }
    else f.decodable = true;
  } catch (e) {
    f.decodable = false;
  } finally {
    m.removeAttribute('src'); m.load();
    URL.revokeObjectURL(url);
  }
  return f;
}

async function fetchStatus() {
  const r = await api('/api/status');
  if (r.status === 401) { lostSession(); return false; }
  if (r.status === 200 && r.json) { S.free = r.json.free; S.total = r.json.total; updateHeader(); return true; }
  return false;
}

async function preparePlan(files) {
  S.plan = { files: [], ready: false };
  $('planList').textContent = '';
  $('planList').append(el('li', 'busy', 'Analizando ' + files.length + ' archivo' + (files.length === 1 ? '' : 's') + '…'));
  $('planGo').disabled = true;
  $('planSheet').hidden = false;
  scrim(true);
  for (const x of $('profiles').children) x.classList.toggle('on', x.dataset.p === S.profile);
  $('profileHint').textContent = FX.PROFILE_HINT[S.profile];
  const infos = [];
  for (const f of files) { infos.push(await analyze(f)); if (!S.plan) return; }
  await fetchStatus();
  if (!S.plan) return;
  S.plan = { files: infos, ready: true };
  renderPlan();
}
function renderPlan() {
  if (!S.plan || !S.plan.ready) return;
  const plans = FX.planAll(S.plan.files, S.profile, { free: S.free, reserve: S.reserve, lim: S.lim });
  S.plan.plans = plans;
  const ul = $('planList');
  ul.textContent = '';
  let go = 0, total = 0;
  S.plan.files.forEach((f, i) => {
    const p = plans[i];
    const li = el('li');
    const k = el('span', 'k ' + (p.act === 'none' ? 'bad' : f.kind), p.act === 'none' ? '!' : (KIND_LABEL[f.kind] || '?'));
    const t = el('div', 't');
    t.append(el('b', null, f.name));
    const est = p.act !== 'none' ? (p.act === 'as-is' ? FX.fmtSize(p.est) : (p.est ? '≈ ' + FX.fmtSize(p.est) : '')) : '';
    t.append(el('span', p.level || null, p.note + (est ? ' · ' + est : '')));
    li.append(k, t);
    ul.append(li);
    if (p.act !== 'none') { go++; total += p.est || 0; }
  });
  $('planGo').disabled = go === 0;
  $('planGo').textContent = go ? 'Subir ' + go : 'Subir';
  $('planHint').textContent = go ? (total ? 'Ocuparán ' + (plans.some((p) => p.act !== 'as-is' && p.act !== 'none') ? 'unos ' : '') + FX.fmtSize(total) + ' de ' + FX.fmtSize(Math.max(0, S.free - S.reserve)) + ' libres. ' : '') +
    'Flex OS reproduce JPEG, vídeo AVI‑MJPEG y audio WAV; lo demás se convierte aquí, en tu móvil.' : 'Nada de esto se puede subir con este perfil.';
}
$('profiles').addEventListener('click', (e) => {
  const b = e.target.closest('button[data-p]');
  if (!b) return;
  S.profile = b.dataset.p;
  for (const x of $('profiles').children) { x.classList.toggle('on', x === b); x.setAttribute('aria-checked', x === b ? 'true' : 'false'); }
  $('profileHint').textContent = FX.PROFILE_HINT[S.profile];
  renderPlan();
});
function closePlan() { $('planSheet').hidden = true; scrim(false); S.plan = null; }
$('planCancel').addEventListener('click', closePlan);
$('planGo').addEventListener('click', () => {
  if (!S.plan || !S.plan.plans) return;
  const prof = S.profile;
  S.plan.files.forEach((f, i) => { if (S.plan.plans[i].act !== 'none') enqueue(f, prof); });
  closePlan();
  runQueue();
});

// ---------- lienzo y JPEG ----------
function canvas(w, h) { const c = el('canvas'); c.width = w; c.height = h; return c; }
function toJpeg(cv, q) {
  return new Promise((res, rej) => cv.toBlob((b) => b ? res(b) : rej(new Error('El navegador no pudo codificar el JPEG')), 'image/jpeg', q));
}
async function jpegBytes(cv, q) { return new Uint8Array(await (await toJpeg(cv, q)).arrayBuffer()); }
// Reduce a la mitad mientras sobre: mas nitidez que un unico salto grande.
function drawScaled(src, sw, sh, w, h) {
  let cur = src, cw = sw, ch = sh;
  while (cw / 2 >= w * 1.2 && ch / 2 >= h * 1.2) {
    const nw = Math.round(cw / 2), nh = Math.round(ch / 2), c = canvas(nw, nh), g = c.getContext('2d');
    g.imageSmoothingQuality = 'high';
    g.drawImage(cur, 0, 0, cw, ch, 0, 0, nw, nh);
    cur = c; cw = nw; ch = nh;
  }
  const out = canvas(w, h), g = out.getContext('2d');
  g.imageSmoothingQuality = 'high';
  g.drawImage(cur, 0, 0, cw, ch, 0, 0, w, h);
  return out;
}
async function decodeImage(blob) {
  if (window.createImageBitmap) {
    try { return await createImageBitmap(blob, { imageOrientation: 'from-image' }); } catch (e) { /* se prueba con <img> */ }
  }
  const url = URL.createObjectURL(blob);
  try {
    const img = new Image();
    img.src = url;
    await img.decode();
    return img;
  } finally { URL.revokeObjectURL(url); }
}
function dimsOf(src) { return { w: src.naturalWidth || src.videoWidth || src.width, h: src.naturalHeight || src.videoHeight || src.height }; }

async function convertPhoto(job, Q) {
  let src;
  try { src = await decodeImage(job.f.file); } catch (e) { throw new Error('Este navegador no puede abrir ' + FMT[job.f.fmt].name + '. Prueba con otro navegador o elige «Original».'); }
  const d = dimsOf(src);
  const o = FX.fitBox(d.w, d.h, Q.max);
  const cv = drawScaled(src, d.w, d.h, o.w, o.h);
  if (src.close) src.close();
  const blob = await toJpeg(cv, Q.q);
  const head = new Uint8Array(await blob.slice(0, 64 * 1024).arrayBuffer());
  if (FX.sniff(head) !== 'jpeg') throw new Error('El navegador generó un JPEG que Flex OS no puede abrir');
  return { blob, w: o.w, h: o.h, fmt: 'jpeg' };
}

async function seekTo(v, t) {
  if (Math.abs(v.currentTime - t) < 1e-4 && v.readyState >= 2) return;
  const p = once(v, 'seeked', 'error', 8000);
  v.currentTime = t;
  await p;
}
async function openVideo(file) {
  const url = URL.createObjectURL(file);
  const v = mediaEl('video', url);
  v.preload = 'auto';
  try {
    await once(v, 'loadedmetadata', 'error', 15000);
    await fixDuration(v);
    if (!v.videoWidth || !isFinite(v.duration)) throw new Error('x');
    try { await v.play(); v.pause(); } catch (e) { /* iOS: basta con cargar */ }
  } catch (e) {
    URL.revokeObjectURL(url);
    throw new Error('Este navegador no puede abrir el vídeo para convertirlo. Elige «Original» para guardarlo tal cual.');
  }
  v.close = () => { v.removeAttribute('src'); v.load(); URL.revokeObjectURL(url); };
  return v;
}

async function convertVideo(job, Q, cap) {
  const v = await openVideo(job.f.file);
  try {
    const o = FX.fitVideo(v.videoWidth, v.videoHeight, Q.long);
    const cv = canvas(o.w, o.h), g = cv.getContext('2d');
    g.imageSmoothingQuality = 'high';
    const total = Math.max(1, Math.floor(v.duration * Q.fps));
    const frames = [];
    let bytes = FX.aviOverhead(0), cut = false;
    for (let i = 0; i < total; i++) {
      if (job.cancelled) throw new Error('Cancelado');
      await seekTo(v, Math.min(v.duration - 0.01, i / Q.fps + 0.001));
      g.drawImage(v, 0, 0, o.w, o.h);
      let f = await jpegBytes(cv, Q.q);
      if (f.length > FX.AVI_FRAME_MAX) f = await jpegBytes(cv, Q.q * 0.6);
      if (f.length > FX.AVI_FRAME_MAX) throw new Error('Un fotograma sale demasiado grande para Flex OS');
      const add = 8 + f.length + (f.length & 1) + 16;
      if (bytes + add > cap) { cut = true; break; }
      frames.push(f);
      bytes += add;
      if (i === 0) job.preview(cv);
      job.progress('conv', (i + 1) / total);
    }
    if (!frames.length) throw new Error('No cabe ni un segundo de vídeo en el espacio libre');
    const parts = FX.aviMux(frames, o.w, o.h, Q.fps);
    return { blob: new Blob(parts, { type: 'video/x-msvideo' }), w: o.w, h: o.h, durMs: Math.round(frames.length * 1000 / Q.fps), fmt: 'avi-mjpeg', cut };
  } finally { v.close(); }
}

function decodeAudio(ac, buf) {
  return new Promise((res, rej) => {
    const p = ac.decodeAudioData(buf, res, rej);
    if (p && p.then) p.then(res, rej);
  });
}
// Audio decodificado como canales en coma flotante. Primero el navegador; si
// no sabe (un WAV IMA ADPCM, por ejemplo), el decodificador de esta pagina.
async function decodeAnyAudio(job, buf) {
  const AC = window.AudioContext || window.webkitAudioContext;
  if (AC) {
    const ac = new AC();
    try {
      const d = await decodeAudio(ac, buf.slice(0));
      const chs = [];
      for (let c = 0; c < d.numberOfChannels; c++) chs.push(d.getChannelData(c));
      return { chs, rate: d.sampleRate };
    } catch (e) { /* se prueba con el decodificador propio */ } finally { if (ac.close) ac.close(); }
  }
  const w = FX.wavDecode(new Uint8Array(buf));
  if (!w) throw new Error('Este navegador no puede abrir ' + FMT[job.f.fmt].name + '. Elige «Original» para guardarlo tal cual.');
  const n = w.pcm.length / w.ch, chs = [];
  for (let c = 0; c < w.ch; c++) {
    const a = new Float32Array(n);
    for (let i = 0; i < n; i++) a[i] = w.pcm[i * w.ch + c] / 32768;
    chs.push(a);
  }
  return { chs, rate: w.rate };
}
async function convertAudio(job, Q, cap) {
  const buf = await job.f.file.arrayBuffer();
  job.progress('conv', 0.1);
  const src = await decodeAnyAudio(job, buf);
  if (job.cancelled) throw new Error('Cancelado');
  job.progress('conv', 0.4);
  const srcMs = src.chs[0].length * 1000 / src.rate;
  const maxMs = FX.wavMaxMs(cap, Q);
  const durMs = Math.min(Math.floor(srcMs), maxMs);
  if (durMs < 1000) throw new Error('No cabe ni un segundo de audio en el espacio libre');
  const frames = Math.floor(durMs * Q.rate / 1000);
  let mono = null;
  const OAC = window.OfflineAudioContext || window.webkitOfflineAudioContext;
  if (OAC) {
    try {
      const oc = new OAC(1, frames, Q.rate);
      const b = oc.createBuffer(src.chs.length, src.chs[0].length, src.rate);
      src.chs.forEach((c, i) => b.getChannelData(i).set(c));
      const node = oc.createBufferSource();
      node.buffer = b;
      node.connect(oc.destination);
      node.start(0);
      const out = await new Promise((res, rej) => {
        oc.oncomplete = (e) => res(e.renderedBuffer);
        const p = oc.startRendering();
        if (p && p.then) p.then(res, rej);
      });
      mono = out.getChannelData(0);
    } catch (e) { mono = null; }
  }
  if (!mono) mono = FX.resampleMono(src.chs, src.rate, Q.rate, frames);
  job.progress('conv', 0.8);
  const pcm = FX.floatToInt16(mono);
  const bytes = Q.codec === 'ima' ? FX.imaWav(pcm, Q.rate) : FX.pcmWav(pcm, Q.rate, 1);
  job.progress('conv', 1);
  return { blob: new Blob([bytes], { type: 'audio/wav' }), durMs, fmt: Q.codec === 'ima' ? 'wav-adpcm' : 'wav-pcm', cut: srcMs > maxMs + 50 };
}

async function crcOf(blob, onProg) {
  let crc = 0;
  const step = 1 << 20;
  for (let p = 0; p < blob.size; p += step) {
    crc = FX.crc32(new Uint8Array(await blob.slice(p, p + step).arrayBuffer()), crc);
    if (onProg) onProg(Math.min(1, (p + step) / blob.size));
  }
  return crc;
}

// Miniatura que aporta el movil para lo que el P4 no sabe abrir.
async function thumbFrom(src) {
  const d = dimsOf(src), r = FX.coverRect(d.w, d.h), cv = canvas(264, 264), g = cv.getContext('2d');
  g.imageSmoothingQuality = 'high';
  g.drawImage(src, r.x, r.y, r.s, r.s, 0, 0, 264, 264);
  let b = await toJpeg(cv, 0.82);
  if (b.size > 46 * 1024) b = await toJpeg(cv, 0.6);
  return b;
}
async function sendThumb(id, job) {
  let src = null;
  try {
    if (job.f.kind === 'photo') src = await decodeImage(job.f.file);
    else if (job.f.kind === 'video') {
      src = await openVideo(job.f.file);
      await seekTo(src, Math.min(1, src.duration / 3));
    } else if (job.f.tags && job.f.tags.pic) src = await decodeImage(new Blob([job.f.tags.pic.data], { type: job.f.tags.pic.mime }));
    if (!src) return;
    const b = await thumbFrom(src);
    await api('/api/thumb/' + id, { method: 'POST', body: b, type: 'image/jpeg' });
  } catch (e) {
    /* sin miniatura: Flex OS pinta el icono de su tipo */
  } finally {
    if (src && typeof src.close === 'function') src.close();     // ImageBitmap o el <video> de openVideo
  }
}

function xhrUpload(url, blob, job) {
  return new Promise((res) => {
    const x = new XMLHttpRequest();
    job.xhr = x;
    x.open('POST', url);
    x.setRequestHeader('X-Flex', '1');
    x.setRequestHeader('Content-Type', 'application/octet-stream');
    x.upload.onprogress = (e) => { if (e.lengthComputable) job.progress('send', e.loaded / e.total, e.loaded, e.total); };
    x.upload.onload = () => job.progress('check', 1);
    x.onload = () => { let j = null; try { j = JSON.parse(x.responseText); } catch (e) { j = null; } res({ status: x.status, json: j }); };
    x.onerror = () => res({ status: 0, json: null });
    x.onabort = () => res({ status: -1, json: null });
    x.send(blob);
  });
}

// ---------- cola ----------
let jobSeq = 0;
function enqueue(f, prof) {
  const job = { id: ++jobSeq, f, prof, cancelled: false, xhr: null, card: null };
  job.card = xferCard(job);
  S.queue.push(job);
  updateQueueCard();
}
let wake = null;
async function keepAwake(on) {
  try {
    if (on && !wake && navigator.wakeLock) wake = await navigator.wakeLock.request('screen');
    if (!on && wake) { await wake.release(); wake = null; }
  } catch (e) { wake = null; }
}
async function runQueue() {
  if (S.running) return;
  keepAwake(true);
  while (S.queue.length) {
    const job = S.queue.shift();
    updateQueueCard();
    if (job.cancelled) continue;
    S.running = job;
    job.card.show();
    await runJob(job);
    S.running = null;
    await refresh(false);
  }
  keepAwake(false);
  updateQueueCard();
}
window.addEventListener('beforeunload', (e) => { if (S.running || S.queue.length) { e.preventDefault(); e.returnValue = ''; } });

async function runJob(job) {
  const f = job.f;
  try {
    if (!(await fetchStatus())) throw new Error(S.paired ? 'Sin conexión con Flex OS' : 'Sesión caducada');
    const lim = S.lim[f.kind] || 0;
    const cap = Math.min(lim, FX.roomFor(S.free, S.reserve));
    const p = FX.plan(f, job.prof, S.lim, FX.roomFor(S.free, S.reserve));
    if (p.act === 'none') throw new Error(p.note);
    job.progress('conv', 0);
    let out;
    if (p.act === 'as-is') out = { blob: f.file, w: f.w, h: f.h, durMs: f.durMs, fmt: f.fmt };
    else if (p.act === 'photo') out = await convertPhoto(job, FX.PROFILES.photo[job.prof]);
    else if (p.act === 'video') out = await convertVideo(job, FX.PROFILES.video[job.prof], cap);
    else out = await convertAudio(job, FX.PROFILES.audio[job.prof], cap);
    if (job.cancelled) throw new Error('Cancelado');
    if (out.blob.size > cap) throw new Error('No cabe: pesa ' + FX.fmtSize(out.blob.size) + ' y quedan ' + FX.fmtSize(cap) + ' libres');
    job.progress('crc', 0);
    const crc = await crcOf(out.blob, (x) => job.progress('crc', x));
    const tg = f.tags || {};
    const q = FX.uploadQuery({
      kind: f.kind, name: FX.uploadName(f.name, FMT[out.fmt].ext), size: out.blob.size, crc,
      created: f.created, w: out.w, h: out.h, dur: out.durMs,
      title: f.kind === 'audio' ? (tg.title || FX.stem(f.name)) : '', artist: tg.artist || '', album: tg.album || ''
    });
    let r;
    for (let tries = 0; ; tries++) {
      job.progress('send', 0, 0, out.blob.size);
      r = await xhrUpload('/api/upload?' + q, out.blob, job);
      if (r.status === 503 && r.json && /otra subida|en curso/i.test(r.json.error || '') && tries < 40 && !job.cancelled) {
        job.progress('wait', 0);
        await new Promise((ok) => setTimeout(ok, 3000));
        continue;
      }
      break;
    }
    if (r.status === -1 || job.cancelled) throw new Error('Cancelado');
    if (r.status === 401) { lostSession(); throw new Error('Sesión caducada'); }
    if (r.status !== 201 && r.status !== 200) throw new Error(errText(r, 'Flex OS no aceptó el archivo'));
    const j = r.json || {};
    if (j.dup) { job.finish('Ya estaba en Flex OS', 'done'); return; }
    if (!j.thumb) { job.progress('thumb', 1); await sendThumb(j.id, job); }
    let msg = f.kind === 'audio' ? 'Guardado en Música' : 'Guardado en Galería y Multimedia';
    if (out.cut) msg += ' · recortado a ' + FX.fmtDur(out.durMs);
    if (!j.p && j.why) msg = 'Guardado para descargar · ' + j.why;
    job.finish(msg, !j.p ? 'warn' : 'done');
  } catch (e) {
    job.finish(e && e.message ? e.message : 'No se pudo subir', job.cancelled ? 'cancel' : 'fail');
  }
}

// ---------- tarjetas de transferencia ----------
function xferCard(job) {
  const f = job.f;
  const c = el('div', 'xfer glass');
  c.hidden = true;
  let th;
  if (f.kind === 'photo' && f.fmt !== 'heic' && f.fmt !== 'avif') {
    th = el('img', 'th');
    th.alt = '';
    th.src = URL.createObjectURL(f.file);
    th.onload = th.onerror = () => URL.revokeObjectURL(th.src);
  } else th = el('span', 'th k ' + (f.kind || 'bad'), KIND_LABEL[f.kind] || '?');
  const t = el('div', 't');
  const name = el('b', null, f.name);
  const line = el('span', null, 'En cola');
  const bar = el('div', 'bar'), fill = el('i');
  bar.append(fill);
  t.append(name, line, bar);
  const stop = el('button', 'stop');
  stop.setAttribute('aria-label', 'Cancelar');
  stop.append(el('span', 'x'));
  c.append(th, t, stop);
  $('xfers').append(c);
  let state = 'wait', timer = 0;
  stop.addEventListener('click', () => {
    if (state === 'done' || state === 'fail' || state === 'warn' || state === 'cancel') { remove(); return; }
    job.cancelled = true;
    if (job.xhr) job.xhr.abort();
    if (S.running !== job) job.finish('Cancelado', 'cancel');
  });
  function remove() { clearTimeout(timer); c.remove(); }
  job.preview = (cv) => {
    if (th.tagName === 'IMG') return;
    try {
      const img = el('img', 'th');
      img.alt = '';
      img.src = cv.toDataURL('image/jpeg', 0.5);
      th.replaceWith(img);
      th = img;
    } catch (e) { /* sin vista previa */ }
  };
  job.progress = (st, frac, done, total) => {
    state = st;
    c.classList.remove('done', 'fail');
    const pct = Math.round(Math.max(0, Math.min(1, frac || 0)) * 100);
    const txt = {
      conv: 'Convirtiendo en el móvil… ' + pct + ' %',
      crc: 'Preparando la comprobación… ' + pct + ' %',
      send: total ? 'Enviando ' + FX.fmtSize(done || 0) + ' de ' + FX.fmtSize(total) + ' · ' + pct + ' %' : 'Enviando…',
      check: 'Flex OS está comprobando el archivo…',
      thumb: 'Guardado · preparando la miniatura…',
      wait: 'Esperando: Flex OS está recibiendo otra subida'
    }[st] || '';
    line.textContent = txt;
    fill.style.width = (st === 'check' || st === 'thumb' ? 100 : st === 'wait' ? 0 : pct) + '%';
  };
  job.finish = (msg, st) => {
    state = st;
    line.textContent = msg;
    c.classList.toggle('done', st === 'done' || st === 'warn');
    c.classList.toggle('warn', st === 'warn');
    c.classList.toggle('fail', st === 'fail' || st === 'cancel');
    fill.style.width = '100%';
    c.hidden = false;
    if (st === 'done') timer = setTimeout(remove, 2500);
    if (st === 'cancel') timer = setTimeout(remove, 1500);
    if (st === 'fail' || st === 'warn') {
      const again = el('button', 'chip retry', st === 'fail' ? 'Reintentar' : 'Entendido');
      again.addEventListener('click', () => {
        remove();
        if (st === 'fail') { enqueue(job.f, job.prof); runQueue(); }
      });
      t.append(again);
    }
  };
  job.card = { show() { c.hidden = false; pruneXfers(); }, remove };
  return job.card;
}
// Como mucho tres tarjetas a la vista: las terminadas sin nada que decir se
// van antes de tiempo; las que avisan de algo esperan a que se lean.
function pruneXfers() {
  const vis = Array.from($('xfers').querySelectorAll('.xfer:not(.queue)')).filter((x) => !x.hidden);
  for (const x of vis) {
    if (vis.length <= 3) break;
    if (x.classList.contains('done') && !x.classList.contains('warn')) { x.remove(); vis.splice(vis.indexOf(x), 1); }
  }
}
// Lo que espera turno no ocupa pantalla: una sola tarjeta con el recuento.
let qCard = null;
function updateQueueCard() {
  const n = S.queue.filter((j) => !j.cancelled).length;
  if (!n) { if (qCard) { qCard.el.remove(); qCard = null; } return; }
  if (!qCard) {
    const c = el('div', 'xfer glass queue'), txt = el('span', 't'), b = el('button', 'chip', 'Cancelar');
    b.addEventListener('click', () => {
      for (const j of S.queue) { j.cancelled = true; j.card.remove(); }
      S.queue = [];
      updateQueueCard();
    });
    c.append(txt, b);
    $('xfers').append(c);
    qCard = { el: c, txt };
  }
  qCard.txt.textContent = n === 1 ? '1 archivo más en cola' : n + ' archivos más en cola';
}

// ===========================================================================
//  Arranque
// ===========================================================================
(async function boot() {
  const k = new URLSearchParams(location.search).get('k');
  if (k) history.replaceState(null, '', location.pathname);
  const h = await api('/api/hello');
  if (h.status !== 200 || !h.json) { showPair(errText(h, 'Flex OS no responde')); return; }
  S.lockType = h.json.lockType | 0;
  S.up = !!h.json.up;
  if (h.json.paired) { startLibrary(false); return; }
  if (k && /^\d{6}$/.test(k) && await pair(k)) { startLibrary(true); return; }
  showPair(k ? 'El código del QR ya no es válido: escribe el que aparece ahora en Flex OS.' : '');
})();
})();
