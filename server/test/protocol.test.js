'use strict';
// #############################################################
//  Pruebas del codec FBP/1 del servidor.
//
//  Lo importante aqui no es que el servidor se entienda consigo mismo,
//  sino que produzca EXACTAMENTE los bytes que espera el dispositivo.
//  Por eso los casos comprueban desplazamientos y valores concretos,
//  los mismos que lee FlexOS_Browser.cpp.
// #############################################################

const test = require('node:test');
const assert = require('node:assert');
const p = require('../src/protocol');

function hdr(buf) {
  return {
    magic: String.fromCharCode(buf[0], buf[1]),
    version: buf[2], type: buf[3], flags: buf[4], channel: buf[5],
    seq: buf.readUInt16LE(6), len: buf.readUInt32LE(8)
  };
}

test('la cabecera tiene 12 bytes little-endian con la magia FB', () => {
  const b = p.frame(p.S.STATE, 3, 0x1234, Buffer.from([1, 2, 3]));
  const h = hdr(b);
  assert.strictEqual(h.magic, 'FB');
  assert.strictEqual(h.version, p.VERSION);
  assert.strictEqual(h.type, p.S.STATE);
  assert.strictEqual(h.channel, 3);
  assert.strictEqual(h.seq, 0x1234);
  assert.strictEqual(h.len, 3);
  assert.strictEqual(b.length, p.HDR + 3);
});

test('readHeader rechaza lo que no cuadra', () => {
  const ok = p.frame(p.S.PONG, 0, 1, Buffer.alloc(0));
  assert.ok(p.readHeader(ok));

  const badMagic = Buffer.from(ok); badMagic[0] = 0x58;
  assert.strictEqual(p.readHeader(badMagic), null);

  const badVer = Buffer.from(ok); badVer[2] = 9;
  assert.strictEqual(p.readHeader(badVer), null);

  assert.strictEqual(p.readHeader(ok.subarray(0, 8)), null);

  // Longitud declarada distinta de los bytes que hay: un mensaje = una
  // trama, asi que esto solo puede ser corrupcion o un intento de colar
  // datos de mas.
  const mismatch = p.frame(p.S.STATE, 0, 1, Buffer.alloc(4));
  mismatch.writeUInt32LE(999, 8);
  assert.strictEqual(p.readHeader(mismatch), null);

  assert.strictEqual(p.readHeader('no soy un buffer'), null);
});

test('WELCOME se codifica en el orden que lee el dispositivo', () => {
  const b = p.buildWelcome(7, {
    viewW: 480, viewH: 800, caps: 0x0f, maxTabs: 4,
    maxFrameBytes: 0x30000, sessionId: 'abc123'
  });
  const m = p.readHeader(b);
  assert.strictEqual(m.type, p.S.WELCOME);
  const q = m.payload;
  assert.strictEqual(q[0], p.VERSION);
  assert.strictEqual(q.readUInt16LE(1), 480);
  assert.strictEqual(q.readUInt16LE(3), 800);
  assert.strictEqual(q.readUInt32LE(5), 0x0f);
  assert.strictEqual(q.readUInt16LE(9), 4);
  assert.strictEqual(q.readUInt32LE(11), 0x30000);
  assert.strictEqual(q.readUInt16LE(15), 6);
  assert.strictEqual(q.toString('utf8', 17, 23), 'abc123');
});

test('STATE: banderas, progreso y cadenas con longitud', () => {
  const b = p.buildState(1, 2, {
    flags: p.ST.LOADING | p.ST.SECURE, progress: 42,
    title: 'Titulo', url: 'https://example.com/'
  });
  const m = p.readHeader(b);
  const q = m.payload;
  assert.strictEqual(q[0], p.ST.LOADING | p.ST.SECURE);
  assert.strictEqual(q[1], 42);
  const tl = q.readUInt16LE(2);
  assert.strictEqual(q.toString('utf8', 4, 4 + tl), 'Titulo');
  const ul = q.readUInt16LE(4 + tl);
  assert.strictEqual(q.toString('utf8', 6 + tl, 6 + tl + ul), 'https://example.com/');
});

test('STATE acota el progreso y recorta las cadenas largas', () => {
  const b = p.buildState(1, 1, { flags: 0, progress: 250, title: 'T'.repeat(500), url: 'u' });
  const q = p.readHeader(b).payload;
  assert.strictEqual(q[1], 100);
  assert.ok(q.readUInt16LE(2) <= 79, 'el titulo debe caber en el buffer del dispositivo');
});

test('el recorte nunca parte un caracter UTF-8 por la mitad', () => {
  // "ñ" son 2 bytes: recortar a 79 en mitad de uno dejaria basura.
  const s = 'ñ'.repeat(60);
  for (let max = 1; max <= 120; max++) {
    const out = p.clampUtf8(s, max);
    const bytes = Buffer.from(out, 'utf8');
    assert.ok(bytes.length <= max, `max=${max}`);
    assert.strictEqual(out, bytes.toString('utf8'), `max=${max}: se partio un caracter`);
  }
});

test('FRAME: geometria, banderas y longitud exacta del JPEG', () => {
  const jpeg = Buffer.from([0xff, 0xd8, 0xff, 0xe0, 1, 2, 3, 4]);
  const b = p.buildFrame(9, 1, {
    x: 0, y: 200, w: 480, h: 200,
    flags: p.FR.KEYFRAME | p.FR.LAST, frameId: 12345, jpeg
  });
  const q = p.readHeader(b).payload;
  assert.strictEqual(q.readUInt16LE(0), 0);
  assert.strictEqual(q.readUInt16LE(2), 200);
  assert.strictEqual(q.readUInt16LE(4), 480);
  assert.strictEqual(q.readUInt16LE(6), 200);
  assert.strictEqual(q[8], p.IMG.JPEG);
  assert.strictEqual(q[9], p.FR.KEYFRAME | p.FR.LAST);
  assert.strictEqual(q.readUInt32LE(10), 12345);
  assert.strictEqual(q.readUInt32LE(14), jpeg.length);
  // El dispositivo exige que la longitud declarada cuadre EXACTA con lo
  // que queda del mensaje: 18 de cabecera de frame + el JPEG.
  assert.strictEqual(q.length, 18 + jpeg.length);
});

test('ERROR y MEDIA', () => {
  const e = p.readHeader(p.buildError(1, 0, p.ERR.BLOCKED, 'destino interno')).payload;
  assert.strictEqual(e.readUInt16LE(0), p.ERR.BLOCKED);
  assert.strictEqual(e.toString('utf8', 4, 4 + e.readUInt16LE(2)), 'destino interno');

  const m = p.readHeader(p.buildMedia(1, p.CH.MEDIA, {
    kind: 1, durationMs: 61000, w: 640, h: 360,
    mime: 'video/mp4', url: '', title: 'Un video'
  })).payload;
  assert.strictEqual(m[0], 1);
  assert.strictEqual(m.readUInt32LE(1), 61000);
  assert.strictEqual(m.readUInt16LE(5), 640);
  assert.strictEqual(m.readUInt16LE(7), 360);
});

test('lectura de los mensajes del dispositivo', () => {
  // HELLO tal cual lo construye fbpBuildHello() en el dispositivo.
  const token = Buffer.from('un-token-de-prueba');
  const devId = Buffer.from('flexos-abc');
  // 17 bytes fijos: ver(1) w(2) h(2) calidad(1) perfil(1) caps(4)
  // formatos(1) maxTabs(1) maxFrameBytes(4). Es el mismo orden exacto
  // que escribe fbpBuildHello() en FlexOS_Browser.cpp.
  const head = Buffer.alloc(17);
  head[0] = p.VERSION;
  head.writeUInt16LE(480, 1);
  head.writeUInt16LE(736, 3);
  head[5] = 62; head[6] = 1;
  head.writeUInt32LE(0x2f, 7);
  head[11] = p.IMG.JPEG;
  head[12] = 6;
  head.writeUInt32LE(0x30000, 13);
  const payload = Buffer.concat([
    head,
    (() => { const b = Buffer.alloc(2); b.writeUInt16LE(token.length, 0); return Buffer.concat([b, token]); })(),
    (() => { const b = Buffer.alloc(2); b.writeUInt16LE(devId.length, 0); return Buffer.concat([b, devId]); })()
  ]);
  const h = p.parseHello(payload);
  assert.strictEqual(h.caps, 0x2f);
  assert.strictEqual(h.formats, p.IMG.JPEG);
  assert.strictEqual(h.maxTabs, 6);
  assert.strictEqual(h.maxFrameBytes, 0x30000);
  assert.strictEqual(h.protoVer, p.VERSION);
  assert.strictEqual(h.w, 480);
  assert.strictEqual(h.h, 736);
  assert.strictEqual(h.quality, 62);
  assert.strictEqual(h.token, 'un-token-de-prueba');
  assert.strictEqual(h.deviceId, 'flexos-abc');

  const ptr = p.parsePointer(Buffer.from([p.PTR.TAP, 0xf0, 0x00, 0x90, 0x01, 0]));
  assert.deepStrictEqual(ptr, { action: p.PTR.TAP, x: 240, y: 400, button: 0 });

  const sc = Buffer.alloc(4);
  sc.writeInt16LE(-120, 0); sc.writeInt16LE(340, 2);
  assert.deepStrictEqual(p.parseScroll(sc), { dx: -120, dy: 340 });
});

test('un mensaje truncado hace fallar al lector, no leer de mas', () => {
  const full = Buffer.concat([
    Buffer.from([p.KEY.TEXT, 0, 0]),
    (() => { const b = Buffer.alloc(2); b.writeUInt16LE(5, 0); return b; })(),
    Buffer.from('hola!')
  ]);
  assert.deepStrictEqual(p.parseKey(full), { action: 0, code: 0, mods: 0, text: 'hola!' });
  for (let cut = 0; cut < full.length; cut++) {
    assert.throws(() => p.parseKey(full.subarray(0, cut)), RangeError, `corte en ${cut}`);
  }
});

test('una cadena que miente sobre su longitud se rechaza', () => {
  const b = Buffer.concat([Buffer.from([0, 0, 0]), Buffer.from([0xff, 0xff]), Buffer.from('x')]);
  assert.throws(() => p.parseKey(b), RangeError);
});

test('los caracteres de control se limpian al leer', () => {
  const txt = Buffer.from('a\r\nb\tc');
  const b = Buffer.concat([
    Buffer.from([0, 0, 0]),
    (() => { const x = Buffer.alloc(2); x.writeUInt16LE(txt.length, 0); return x; })(),
    txt
  ]);
  const k = p.parseKey(b);
  assert.strictEqual(k.text, 'a  b c');
  assert.ok(!/[ -]/.test(k.text));
});

test('los codigos y canales coinciden con los del dispositivo', () => {
  // Si alguno de estos numeros cambia sin cambiarlo en
  // FlexOS_Browser.h, el dispositivo dejaria de entender al servicio.
  assert.strictEqual(p.VERSION, 1);
  assert.strictEqual(p.HDR, 12);
  assert.strictEqual(p.C.HELLO, 0x01);
  assert.strictEqual(p.C.NAVIGATE, 0x10);
  assert.strictEqual(p.C.POINTER, 0x20);
  assert.strictEqual(p.C.VIEWPORT, 0x23);
  assert.strictEqual(p.C.REQ_FRAME, 0x41);
  assert.strictEqual(p.S.WELCOME, 0x81);
  assert.strictEqual(p.S.FRAME, 0x83);
  assert.strictEqual(p.S.DOWNLOAD, 0x89);
  assert.strictEqual(p.CH.MEDIA, 200);
  assert.strictEqual(p.ST.EDITING, 1 << 6);
  assert.strictEqual(p.FR.KEYFRAME, 1);
  assert.strictEqual(p.ERR.EXPIRED, 9);
});
