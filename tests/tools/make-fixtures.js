#!/usr/bin/env node
// #############################################################
//  make-fixtures.js  ·  genera los ficheros de prueba del
//  decodificador JPEG de Flex OS (tests/fixtures).
// #############################################################
//
//  POR QUE SE GENERAN Y SE COMMITEAN
//  ---------------------------------
//  El test de host (tests/host/test_jpeg.cpp) tiene que poder
//  ejecutarse SIN red y SIN Node instalado, asi que los ficheros
//  quedan versionados. Este script solo hace falta para volver a
//  crearlos si se cambia el conjunto de casos.
//
//  Para cada imagen se guardan DOS ficheros:
//    · <nombre>.jpg  -> la entrada
//    · <nombre>.rgb  -> la referencia RGB de 8 bits (ancho*alto*3)
//                       decodificada por libjpeg-turbo (a traves de
//                       sharp). El test compara contra ESO, no
//                       contra numeros escritos a mano.
//  Y un indice fixtures.txt con "nombre ancho alto componentes".
//
//  El caso `restart` NO lo puede producir sharp (libvips no expone
//  el intervalo de reinicio), asi que lleva su propio codificador
//  minimo mas abajo: una rejilla de MCU de color solido con DRI=1.
//  Al ser DC puro, el resultado esperado se calcula exactamente.

'use strict';
const fs = require('fs');
const path = require('path');
const sharp = require('sharp');

const OUT = path.join(__dirname, '..', 'fixtures');
fs.mkdirSync(OUT, { recursive: true });

const index = [];

// ---------------------------------------------------------------
//  Generadores de escena (RGB de 8 bits, sin comprimir)
// ---------------------------------------------------------------
function sceneGradient(w, h) {
  const b = Buffer.alloc(w * h * 3);
  for (let y = 0; y < h; y++) {
    for (let x = 0; x < w; x++) {
      const i = (y * w + x) * 3;
      b[i] = (x * 255 / Math.max(1, w - 1)) | 0;
      b[i + 1] = (y * 255 / Math.max(1, h - 1)) | 0;
      b[i + 2] = ((x + y) * 255 / Math.max(1, w + h - 2)) | 0;
    }
  }
  return b;
}

// Escena tipo "pagina web": fondo claro, bandas de texto oscuras y un
// bloque de color. Es la que de verdad se parece a lo que enviara el
// servicio, y por tanto la que vale para medir el error de croma.
function scenePage(w, h) {
  const b = Buffer.alloc(w * h * 3, 0xF5);
  const put = (x0, y0, x1, y1, r, g, bl) => {
    for (let y = Math.max(0, y0); y < Math.min(h, y1); y++)
      for (let x = Math.max(0, x0); x < Math.min(w, x1); x++) {
        const i = (y * w + x) * 3;
        b[i] = r; b[i + 1] = g; b[i + 2] = bl;
      }
  };
  put(0, 0, w, 40, 0x1a, 0x1d, 0x24);              // barra superior
  for (let row = 0; row < 14; row++) {              // "lineas de texto"
    const y = 70 + row * 26;
    put(24, y, w - 40 - (row % 5) * 30, y + 11, 0x22, 0x24, 0x2b);
  }
  put(24, 470, w - 24, 470 + 160, 0x2f, 0x6f, 0xd8); // bloque de color
  return b;
}

async function emit(name, w, h, rgb, jpegOpts) {
  const jpg = await sharp(rgb, { raw: { width: w, height: h, channels: 3 } })
    .jpeg(Object.assign({ progressive: false, mozjpeg: false }, jpegOpts))
    .toBuffer();
  fs.writeFileSync(path.join(OUT, name + '.jpg'), jpg);
  // Referencia: la MISMA imagen comprimida, decodificada por libjpeg-turbo.
  const ref = await sharp(jpg).raw().toBuffer();
  fs.writeFileSync(path.join(OUT, name + '.rgb'), ref);
  const meta = await sharp(jpg).metadata();
  index.push(`${name} ${w} ${h} ${meta.channels}`);
  console.log(`  ${name}.jpg  ${w}x${h}  ${jpg.length} bytes`);
}

// ---------------------------------------------------------------
//  Codificador minimo con marcadores de reinicio
//  ------------------------------------------------------------
//  Solo produce lo que hace falta para el caso de prueba: 4:2:0,
//  una tabla de cuantizacion plana, las tablas Huffman estandar del
//  Anexo K y bloques DC puros (sin AC). Con eso el valor esperado de
//  cada MCU es exactamente calculable y el test puede comprobar a la
//  vez la colocacion de las MCU y el reinicio de los predictores DC.
// ---------------------------------------------------------------
const STD_DC_LUM_BITS = [0, 0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0];
const STD_DC_LUM_VALS = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11];
const STD_AC_LUM_BITS = [0, 0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d];
const STD_AC_LUM_VALS = [
  0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07,
  0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08, 0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0,
  0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28,
  0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
  0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
  0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
  0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
  0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5,
  0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2,
  0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
  0xf9, 0xfa
];

function huffCodes(bits, vals) {
  const map = new Map();
  let code = 0, k = 0;
  for (let l = 1; l <= 16; l++) {
    for (let i = 0; i < bits[l]; i++) map.set(vals[k++], { code: code + i, len: l });
    code = (code + bits[l]) << 1;
  }
  return map;
}

class BitWriter {
  constructor() { this.bytes = []; this.acc = 0; this.n = 0; }
  put(code, len) {
    for (let i = len - 1; i >= 0; i--) {
      this.acc = (this.acc << 1) | ((code >> i) & 1);
      if (++this.n === 8) {
        this.bytes.push(this.acc & 0xff);
        if ((this.acc & 0xff) === 0xff) this.bytes.push(0x00);  // relleno obligatorio
        this.acc = 0; this.n = 0;
      }
    }
  }
  flushToByte() {                       // rellena con unos, como exige el estandar
    while (this.n !== 0) this.put(1, 1);
  }
  take() { const b = Buffer.from(this.bytes); this.bytes = []; return b; }
}

// Rejilla de MCU de color solido, con submuestreo y DRI configurables.
// Devuelve { jpeg, expected, W, H } donde `expected` es el RGB exacto.
// Sirve para dos cosas que sharp no puede generar:
//   · marcadores de reinicio (libvips no expone el intervalo);
//   · submuestreos 4:2:2 y 4:4:0 (sharp solo ofrece 4:2:0 y 4:4:4).
function encodeDcGrid(cols, rows, hY, vY, restartInterval) {
  const MW = 8 * hY, MH = 8 * vY;
  const W = cols * MW, H = rows * MH;
  const QY = 16, QC = 16;                              // tablas planas: DC*Q/8 + 128
  const dcLum = huffCodes(STD_DC_LUM_BITS, STD_DC_LUM_VALS);
  const acLum = huffCodes(STD_AC_LUM_BITS, STD_AC_LUM_VALS);

  const out = [];
  const seg = (marker, payload) => {
    out.push(Buffer.from([0xff, marker]));
    if (payload) {
      const len = Buffer.alloc(2); len.writeUInt16BE(payload.length + 2, 0);
      out.push(len, payload);
    }
  };
  const zig = [
    0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5,
    12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63
  ];

  seg(0xd8);                                            // SOI
  { const q = Buffer.alloc(1 + 64); q[0] = 0x00; q.fill(QY, 1); seg(0xdb, q); }
  { const q = Buffer.alloc(1 + 64); q[0] = 0x01; q.fill(QC, 1); seg(0xdb, q); }
  {                                                     // SOF0
    const p = Buffer.alloc(6 + 9);
    p[0] = 8; p.writeUInt16BE(H, 1); p.writeUInt16BE(W, 3); p[5] = 3;
    p[6] = 1; p[7] = (hY << 4) | vY; p[8] = 0;
    p[9] = 2; p[10] = 0x11; p[11] = 1;
    p[12] = 3; p[13] = 0x11; p[14] = 1;
    seg(0xc0, p);
  }
  const dht = (tc, th, bits, vals) => {
    const p = Buffer.alloc(1 + 16 + vals.length);
    p[0] = (tc << 4) | th;
    for (let l = 1; l <= 16; l++) p[l] = bits[l];
    Buffer.from(vals).copy(p, 17);
    seg(0xc4, p);
  };
  dht(0, 0, STD_DC_LUM_BITS, STD_DC_LUM_VALS);
  dht(1, 0, STD_AC_LUM_BITS, STD_AC_LUM_VALS);
  dht(0, 1, STD_DC_LUM_BITS, STD_DC_LUM_VALS);
  dht(1, 1, STD_AC_LUM_BITS, STD_AC_LUM_VALS);
  { const p = Buffer.alloc(2); p.writeUInt16BE(restartInterval, 0); seg(0xdd, p); }  // DRI
  {                                                     // SOS
    const p = Buffer.alloc(1 + 6 + 3);
    p[0] = 3;
    p[1] = 1; p[2] = 0x00; p[3] = 2; p[4] = 0x11; p[5] = 3; p[6] = 0x11;
    p[7] = 0; p[8] = 63; p[9] = 0;
    seg(0xda, p);
  }

  const bw = new BitWriter();
  const writeCoef = (v, table) => {                     // DC con categoria + bits
    let t = 0, a = Math.abs(v);
    while (a) { t++; a >>= 1; }
    const e = table.get(t);
    bw.put(e.code, e.len);
    if (t) bw.put(v > 0 ? v : v + (1 << t) - 1, t);
  };
  const eob = () => { const e = acLum.get(0x00); bw.put(e.code, e.len); };

  // Valor DC por MCU: una rampa que cambia en cada MCU, para que un
  // predictor DC mal reiniciado se note de inmediato.
  const dcOf = (m) => ((m * 5) % 13) - 6;
  const pred = [0, 0, 0];
  let sinceRestart = 0, rstIdx = 0;
  const mcus = cols * rows;
  for (let m = 0; m < mcus; m++) {
    if (restartInterval && sinceRestart === restartInterval && m > 0) {
      bw.flushToByte();
      out.push(bw.take());
      out.push(Buffer.from([0xff, 0xd0 + (rstIdx & 7)]));
      rstIdx++; sinceRestart = 0; pred[0] = pred[1] = pred[2] = 0;
    }
    sinceRestart++;
    const dY = dcOf(m);
    for (let b = 0; b < hY * vY; b++) { writeCoef(dY - pred[0], dcLum); pred[0] = dY; eob(); }
    for (let c = 1; c <= 2; c++) { writeCoef(0 - pred[c], dcLum); pred[c] = 0; eob(); }
  }
  bw.flushToByte();
  out.push(bw.take());
  out.push(Buffer.from([0xff, 0xd9]));                  // EOI
  void zig;

  // Resultado exacto esperado: cada MCU es un gris uniforme.
  const expected = Buffer.alloc(W * H * 3);
  for (let my = 0; my < rows; my++) {
    for (let mx = 0; mx < cols; mx++) {
      const m = my * cols + mx;
      let g = Math.round(dcOf(m) * QY / 8) + 128;
      g = Math.max(0, Math.min(255, g));
      for (let y = my * MH; y < my * MH + MH; y++)
        for (let x = mx * MW; x < mx * MW + MW; x++) {
          const i = (y * W + x) * 3;
          expected[i] = g; expected[i + 1] = g; expected[i + 2] = g;
        }
    }
  }
  void QC;
  return { jpeg: Buffer.concat(out), expected, W, H };
}

// ---------------------------------------------------------------
(async () => {
  console.log('Generando ficheros de prueba en', OUT);

  await emit('grad444', 64, 48, sceneGradient(64, 48), { quality: 92, chromaSubsampling: '4:4:4' });
  await emit('grad420', 64, 48, sceneGradient(64, 48), { quality: 80, chromaSubsampling: '4:2:0' });
  // Dimensiones IMPARES a proposito: la ultima MCU de cada eje queda a
  // medias y el decodificador tiene que recortarla, no pintar el relleno.
  await emit('odd444', 63, 47, sceneGradient(63, 47), { quality: 85, chromaSubsampling: '4:4:4' });
  await emit('odd420', 61, 45, sceneGradient(61, 45), { quality: 78, chromaSubsampling: '4:2:0' });
  await emit('page480', 480, 800, scenePage(480, 800), { quality: 70, chromaSubsampling: '4:2:0' });
  await emit('page240', 240, 320, scenePage(240, 320), { quality: 55, chromaSubsampling: '4:2:0' });

  // Escala de grises: JPEG de UNA sola componente (SOF con Nf=1).
  // Hay que forzar el espacio de color: dandole solo un canal de entrada,
  // sharp escribe igualmente un JPEG YCbCr de 3 componentes.
  {
    const w = 40, h = 24;
    const gray = Buffer.alloc(w * h);
    for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) gray[y * w + x] = (x * 6 + y * 3) & 0xff;
    const jpg = await sharp(gray, { raw: { width: w, height: h, channels: 1 } })
      .toColourspace('b-w')
      .jpeg({ quality: 90, progressive: false }).toBuffer();
    const meta = await sharp(jpg).metadata();
    if (meta.channels !== 1) throw new Error(`gray.jpg salio con ${meta.channels} canales`);
    fs.writeFileSync(path.join(OUT, 'gray.jpg'), jpg);
    // OJO: aunque el JPEG tenga UNA componente, sharp puede devolver el
    // raw ya expandido a 3 canales. Se mira el tamano real en vez de
    // suponerlo -- suponerlo fue justo lo que produjo una referencia
    // desplazada y un "fallo" que no estaba en el decodificador.
    const ref = await sharp(jpg).raw().toBuffer();
    const ch = ref.length / (w * h);
    if (ch !== 1 && ch !== 3) throw new Error(`gray: raw con ${ch} canales`);
    const rgb = Buffer.alloc(w * h * 3);
    for (let i = 0; i < w * h; i++) {
      const v = ch === 1 ? ref[i] : ref[i * 3];
      rgb[i * 3] = v; rgb[i * 3 + 1] = v; rgb[i * 3 + 2] = v;
    }
    fs.writeFileSync(path.join(OUT, 'gray.rgb'), rgb);
    index.push(`gray ${w} ${h} 1`);
    console.log(`  gray.jpg  ${w}x${h}  ${jpg.length} bytes  (1 componente)`);
  }

  // Progresivo: DEBE ser rechazado por el decodificador.
  {
    const jpg = await sharp(sceneGradient(48, 32), { raw: { width: 48, height: 32, channels: 3 } })
      .jpeg({ quality: 80, progressive: true }).toBuffer();
    fs.writeFileSync(path.join(OUT, 'progressive.jpg'), jpg);
    console.log(`  progressive.jpg  48x32  ${jpg.length} bytes  (debe rechazarse)`);
  }

  // Rejillas DC: marcadores de reinicio y los submuestreos que sharp no
  // sabe producir. Cada una se verifica contra libjpeg-turbo antes de
  // aceptarla como referencia.
  const grids = [
    ['restart', 5, 4, 2, 2, 3],   // 4:2:0 con DRI=3  -> reinicio de predictores DC
    ['samp422', 4, 3, 2, 1, 0],   // 4:2:2            -> MCU de 16x8
    ['samp440', 3, 4, 1, 2, 0],   // 4:4:0            -> MCU de 8x16
    ['rst1',    3, 3, 2, 2, 1]    // 4:2:0 con DRI=1  -> un reinicio por MCU
  ];
  for (const [name, cols, rows, hY, vY, dri] of grids) {
    const r = encodeDcGrid(cols, rows, hY, vY, dri);
    fs.writeFileSync(path.join(OUT, name + '.jpg'), r.jpeg);
    fs.writeFileSync(path.join(OUT, name + '.rgb'), r.expected);
    index.push(`${name} ${r.W} ${r.H} 3`);
    const back = await sharp(r.jpeg).raw().toBuffer();
    let worst = 0;
    for (let i = 0; i < back.length; i++) worst = Math.max(worst, Math.abs(back[i] - r.expected[i]));
    console.log(`  ${name}.jpg  ${r.W}x${r.H}  ${r.jpeg.length} bytes  (DRI=${dri}, ` +
                `${hY}x${vY})  error vs libjpeg-turbo = ${worst}`);
    if (worst > 2) throw new Error(`${name}: el codificador de prueba no coincide con libjpeg-turbo`);
  }

  fs.writeFileSync(path.join(OUT, 'fixtures.txt'), index.join('\n') + '\n');
  console.log('Indice escrito en fixtures.txt');
})().catch((e) => { console.error(e); process.exit(1); });
