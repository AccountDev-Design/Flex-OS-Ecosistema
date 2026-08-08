'use strict';
// #############################################################
//  protocol.js  ·  codec del protocolo FBP/1
//  ------------------------------------------------------------
//  ES EL ESPEJO EXACTO de FlexOS_Browser.cpp (lado dispositivo).
//  Si cambias algo aqui, cambialo alli y sube FBP_VERSION en los
//  dos sitios: un dispositivo con una version distinta se rechaza
//  en el HELLO en vez de intentar entenderse a medias.
//
//  Todo little-endian. Cadenas con longitud (uint16) por delante y
//  SIN terminador. Ver PROTOCOL.md para la especificacion.
// #############################################################

const MAGIC0 = 0x46; // 'F'
const MAGIC1 = 0x42; // 'B'
const VERSION = 1;
const HDR = 12;

// Dispositivo -> servicio
const C = {
  HELLO: 0x01, PING: 0x02, ACK: 0x03,
  NAVIGATE: 0x10, BACK: 0x11, FORWARD: 0x12, RELOAD: 0x13, STOP: 0x14,
  POINTER: 0x20, SCROLL: 0x21, KEY: 0x22, VIEWPORT: 0x23,
  TAB_NEW: 0x30, TAB_CLOSE: 0x31, TAB_SELECT: 0x32,
  MEDIA: 0x40, REQ_FRAME: 0x41
};

// Servicio -> dispositivo
const S = {
  WELCOME: 0x81, STATE: 0x82, FRAME: 0x83, MEDIA: 0x84,
  ERROR: 0x85, PONG: 0x86, NAVIGATED: 0x87, TABS: 0x88, DOWNLOAD: 0x89
};

const PTR = { DOWN: 0, UP: 1, MOVE: 2, TAP: 3, CANCEL: 4 };
const KEY = { TEXT: 0, DOWN: 1, UP: 2 };
const VK = { NONE: 0, ENTER: 1, BACKSPACE: 2, TAB: 3, ESC: 4, UP: 5, DOWN: 6, LEFT: 7, RIGHT: 8 };
const MED = { PLAY: 0, PAUSE: 1, SEEK: 2, STOP: 3, VOLUME: 4, OPEN: 5 };

const ST = {
  LOADING: 1 << 0, CAN_BACK: 1 << 1, CAN_FWD: 1 << 2,
  SECURE: 1 << 3, INSECURE: 1 << 4, MEDIA: 1 << 5, EDITING: 1 << 6
};
const FR = { KEYFRAME: 1 << 0, LAST: 1 << 1 };
const IMG = { JPEG: 1 };

const ERR = {
  NONE: 0, AUTH: 1, BLOCKED: 2, NAV: 3, TIMEOUT: 4,
  LIMIT: 5, INTERNAL: 6, CERT: 7, PROTOCOL: 8, EXPIRED: 9
};

// Canales reservados. 1..N son pestanas.
const CH = { SESSION: 0, MEDIA: 200, FAVICON: 201 };

// -------------------------------------------------------------
//  Lectura
// -------------------------------------------------------------
function readHeader(buf) {
  if (!Buffer.isBuffer(buf) || buf.length < HDR) return null;
  if (buf[0] !== MAGIC0 || buf[1] !== MAGIC1) return null;
  if (buf[2] !== VERSION) return null;
  const len = buf.readUInt32LE(8);
  if (len > 16 * 1024 * 1024) return null;         // tope de cordura
  if (buf.length !== HDR + len) return null;       // un mensaje = una trama
  return {
    type: buf[3], flags: buf[4], channel: buf[5],
    seq: buf.readUInt16LE(6), len,
    payload: buf.subarray(HDR, HDR + len)
  };
}

// Lector de cadenas con longitud. Devuelve null si el mensaje miente
// sobre el tamano; nunca lee mas alla del buffer.
class Reader {
  constructor(buf) { this.b = buf; this.o = 0; }
  get remaining() { return this.b.length - this.o; }
  u8() { if (this.remaining < 1) throw new RangeError('u8'); return this.b[this.o++]; }
  u16() { if (this.remaining < 2) throw new RangeError('u16'); const v = this.b.readUInt16LE(this.o); this.o += 2; return v; }
  i16() { if (this.remaining < 2) throw new RangeError('i16'); const v = this.b.readInt16LE(this.o); this.o += 2; return v; }
  u32() { if (this.remaining < 4) throw new RangeError('u32'); const v = this.b.readUInt32LE(this.o); this.o += 4; return v; }
  str(max = 8192) {
    const n = this.u16();
    if (n > max) throw new RangeError('cadena demasiado larga');
    if (this.remaining < n) throw new RangeError('cadena truncada');
    const s = this.b.toString('utf8', this.o, this.o + n);
    this.o += n;
    // Se limpian los caracteres de control: lo que llegue de aqui puede
    // acabar en un log o en una URL, y un \r\n incrustado es la via
    // clasica de inyeccion.
    return s.replace(/[\u0000-\u001f\u007f]/g, ' ');
  }
}

// -------------------------------------------------------------
//  Escritura
// -------------------------------------------------------------
function frame(type, channel, seq, payload) {
  const p = payload || Buffer.alloc(0);
  const out = Buffer.allocUnsafe(HDR + p.length);
  out[0] = MAGIC0; out[1] = MAGIC1; out[2] = VERSION;
  out[3] = type; out[4] = 0; out[5] = channel & 0xff;
  out.writeUInt16LE(seq & 0xffff, 6);
  out.writeUInt32LE(p.length, 8);
  p.copy(out, HDR);
  return out;
}

function frameWithFlags(type, flags, channel, seq, payload) {
  const out = frame(type, channel, seq, payload);
  out[4] = flags & 0xff;
  return out;
}

function putStr(s) {
  const b = Buffer.from(String(s == null ? '' : s), 'utf8');
  const out = Buffer.allocUnsafe(2 + b.length);
  out.writeUInt16LE(b.length, 0);
  b.copy(out, 2);
  return out;
}

// Recorta una cadena a `max` BYTES sin partir un caracter UTF-8 por la
// mitad (partirlo dejaria un byte suelto que el dispositivo pintaria
// como basura).
function clampUtf8(s, max) {
  const b = Buffer.from(String(s == null ? '' : s), 'utf8');
  if (b.length <= max) return b.toString('utf8');
  let end = max;
  while (end > 0 && (b[end] & 0xc0) === 0x80) end--;
  return b.subarray(0, end).toString('utf8');
}

function buildWelcome(seq, { viewW, viewH, caps, maxTabs, maxFrameBytes, sessionId }) {
  const head = Buffer.allocUnsafe(15);
  head[0] = VERSION;
  head.writeUInt16LE(viewW, 1);
  head.writeUInt16LE(viewH, 3);
  head.writeUInt32LE(caps >>> 0, 5);
  head.writeUInt16LE(maxTabs, 9);
  head.writeUInt32LE(maxFrameBytes, 11);
  return frame(S.WELCOME, CH.SESSION, seq, Buffer.concat([head, putStr(sessionId)]));
}

function buildState(seq, channel, { flags, progress, title, url }) {
  const head = Buffer.from([flags & 0xff, Math.max(0, Math.min(100, progress | 0))]);
  return frame(S.STATE, channel, seq, Buffer.concat([
    head, putStr(clampUtf8(title, 79)), putStr(clampUtf8(url, 511))
  ]));
}

function buildFrame(seq, channel, { x, y, w, h, flags, frameId, jpeg }) {
  const head = Buffer.allocUnsafe(18);
  head.writeUInt16LE(x, 0);
  head.writeUInt16LE(y, 2);
  head.writeUInt16LE(w, 4);
  head.writeUInt16LE(h, 6);
  head[8] = IMG.JPEG;
  head[9] = flags & 0xff;
  head.writeUInt32LE(frameId >>> 0, 10);
  head.writeUInt32LE(jpeg.length, 14);
  return frame(S.FRAME, channel, seq, Buffer.concat([head, jpeg]));
}

function buildMedia(seq, channel, { kind, durationMs, w, h, mime, url, title }) {
  const head = Buffer.allocUnsafe(9);
  head[0] = kind & 0xff;
  head.writeUInt32LE(Math.max(0, durationMs | 0), 1);
  head.writeUInt16LE(w | 0, 5);
  head.writeUInt16LE(h | 0, 7);
  return frame(S.MEDIA, channel, seq, Buffer.concat([
    head, putStr(clampUtf8(mime, 39)), putStr(clampUtf8(url, 511)), putStr(clampUtf8(title, 79))
  ]));
}

function buildError(seq, channel, code, msg) {
  const head = Buffer.allocUnsafe(2);
  head.writeUInt16LE(code, 0);
  return frame(S.ERROR, channel, seq, Buffer.concat([head, putStr(clampUtf8(msg, 119))]));
}

function buildNavigated(seq, channel, url) {
  return frame(S.NAVIGATED, channel, seq, putStr(clampUtf8(url, 511)));
}

function buildPong(seq) { return frame(S.PONG, CH.SESSION, seq, Buffer.alloc(0)); }

function buildDownload(seq, channel, { name, size, url }) {
  const n = putStr(clampUtf8(name, 47));
  const sz = Buffer.allocUnsafe(4);
  sz.writeUInt32LE(Math.max(0, size | 0), 0);
  return frame(S.DOWNLOAD, channel, seq, Buffer.concat([n, sz, putStr(clampUtf8(url, 511))]));
}

function buildTabs(seq, tabs, active) {
  const parts = [Buffer.from([tabs.length & 0xff, active & 0xff])];
  for (const t of tabs) parts.push(Buffer.from([t.id & 0xff]), putStr(clampUtf8(t.title, 79)));
  return frame(S.TABS, CH.SESSION, seq, Buffer.concat(parts));
}

// -------------------------------------------------------------
//  Analizadores de los mensajes del dispositivo
// -------------------------------------------------------------
function parseHello(payload) {
  const r = new Reader(payload);
  const protoVer = r.u8();
  const w = r.u16(), h = r.u16();
  const quality = r.u8(), profile = r.u8();
  const caps = r.u32();
  const formats = r.u8();
  const maxTabs = r.u8();
  const maxFrameBytes = r.u32();
  const token = r.str(256);
  const deviceId = r.str(128);
  return { protoVer, w, h, quality, profile, caps, formats, maxTabs, maxFrameBytes, token, deviceId };
}
function parseNavigate(p) { return new Reader(p).str(2048); }
function parsePointer(p) {
  const r = new Reader(p);
  return { action: r.u8(), x: r.u16(), y: r.u16(), button: r.u8() };
}
function parseScroll(p) { const r = new Reader(p); return { dx: r.i16(), dy: r.i16() }; }
function parseKey(p) {
  const r = new Reader(p);
  return { action: r.u8(), code: r.u8(), mods: r.u8(), text: r.str(64) };
}
function parseViewport(p) {
  const r = new Reader(p);
  return { w: r.u16(), h: r.u16(), quality: r.u8(), profile: r.u8(), scalePct: r.u8() };
}
function parseAck(p) { const r = new Reader(p); return { lastSeq: r.u16(), pending: r.u8() }; }
function parseMedia(p) { const r = new Reader(p); return { cmd: r.u8(), arg: r.u32() }; }

module.exports = {
  VERSION, HDR, C, S, PTR, KEY, VK, MED, ST, FR, IMG, ERR, CH,
  readHeader, Reader, frame, frameWithFlags, putStr, clampUtf8,
  buildWelcome, buildState, buildFrame, buildMedia, buildError,
  buildNavigated, buildPong, buildDownload, buildTabs,
  parseHello, parseNavigate, parsePointer, parseScroll, parseKey,
  parseViewport, parseAck, parseMedia
};
