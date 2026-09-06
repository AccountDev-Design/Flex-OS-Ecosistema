'use strict';
// #############################################################
//  FLEX SDK · GRANT DE PERMISOS FLXG v1
//  ------------------------------------------------------------
//  Escribe el bloque de 296 bytes que valida FlexOS_AppGrant.cpp.
//
//  QUIEN FIRMA ESTO EN PRODUCCION. La clave privada de Flex Store vive
//  UNICAMENTE en el backend de Flex Developer Studio: este modulo
//  existe para que Studio pueda emitir el bloque y para que las pruebas
//  del SDK puedan ejercitar el formato con una clave de usar y tirar.
//  Un grant firmado con cualquier otra clave lo rechaza la placa, que
//  lleva la publica de Flex Store incrustada (FlexOS_TrustedKeys.h).
// #############################################################
const crypto = require('crypto');
const { SYS_PERMISSIONS } = require('./isa');
const { signMessage } = require('./flexpkg');

const SIGNED_BYTES = 232;
const TOTAL_BYTES = 296;
const PURPOSE_APP = 1;

function maskOf(names) {
  let m = 0;
  for (const n of names) {
    const bit = SYS_PERMISSIONS[n];
    if (!bit) throw new Error(`permiso de sistema desconocido: ${n}`);
    if (m & bit) throw new Error(`permiso repetido: ${n}`);
    m |= bit;
  }
  return m >>> 0;
}

function popcount(v) {
  let c = 0;
  for (let x = v >>> 0; x; x >>>= 1) c += (x & 1);
  return c;
}

function writeU64LE(buf, off, value) {
  const big = BigInt(value);
  buf.writeUInt32LE(Number(big & 0xFFFFFFFFn), off);
  buf.writeUInt32LE(Number((big >> 32n) & 0xFFFFFFFFn), off + 4);
}

function writeFixed(buf, off, width, text) {
  const b = Buffer.from(text, 'ascii');
  if (b.length > width) throw new Error(`"${text}" no cabe en ${width} bytes`);
  b.copy(buf, off);
}

// { packageId, versionName, versionCode, packageSha256 (hex o Buffer),
//   developerKeySha256 (hex), permissions: [nombres], notBefore, notAfter }
function build(spec, storePrivateKey) {
  if (!spec.permissions || !spec.permissions.length) {
    throw new Error('un grant que no concede nada no tiene sentido');
  }
  const mask = maskOf(spec.permissions);
  const pkgHash = Buffer.isBuffer(spec.packageSha256)
    ? spec.packageSha256 : Buffer.from(spec.packageSha256, 'hex');
  const devHash = Buffer.isBuffer(spec.developerKeySha256)
    ? spec.developerKeySha256 : Buffer.from(spec.developerKeySha256, 'hex');
  if (pkgHash.length !== 32) throw new Error('packageSha256 tiene que ser de 32 bytes');
  if (devHash.length !== 32) throw new Error('developerKeySha256 tiene que ser de 32 bytes');

  const b = Buffer.alloc(TOTAL_BYTES);
  b.write('FLXG', 0, 'ascii');
  b.writeUInt16LE(1, 4);
  b.writeUInt16LE(0, 6);
  b.writeUInt16LE(PURPOSE_APP, 8);
  b.writeUInt16LE(popcount(mask), 10);
  b.writeUInt32LE(spec.versionCode >>> 0, 12);
  writeU64LE(b, 16, spec.notBefore || 0);
  writeU64LE(b, 24, spec.notAfter || 0);
  pkgHash.copy(b, 32);
  devHash.copy(b, 64);
  writeFixed(b, 96, 96, spec.packageId);
  writeFixed(b, 192, 32, spec.versionName);
  b.writeUInt32LE(mask, 224);
  b.writeUInt32LE(0, 228);

  // Igual que en el paquete: se firma el MENSAJE (los 232 bytes de cabecera
  // del grant), y el firmware verifica ECDSA sobre su SHA-256.
  const sig = signMessage(storePrivateKey, b.subarray(0, SIGNED_BYTES));
  sig.copy(b, SIGNED_BYTES);
  return b;
}

// Lectura para inspeccion (NO valida la firma: eso lo hace el firmware).
function describe(buf) {
  if (!Buffer.isBuffer(buf) || buf.length !== TOTAL_BYTES) throw new Error('grant de tamano invalido');
  if (buf.toString('ascii', 0, 4) !== 'FLXG') throw new Error('no es un grant FLXG');
  const mask = buf.readUInt32LE(224);
  const names = Object.keys(SYS_PERMISSIONS).filter((n) => mask & SYS_PERMISSIONS[n]);
  const readU64 = (off) =>
    Number(BigInt(buf.readUInt32LE(off)) | (BigInt(buf.readUInt32LE(off + 4)) << 32n));
  return {
    formatVersion: buf.readUInt16LE(4),
    purpose: buf.readUInt16LE(8),
    permissionCount: buf.readUInt16LE(10),
    versionCode: buf.readUInt32LE(12),
    notBefore: readU64(16),
    notAfter: readU64(24),
    packageSha256: buf.toString('hex', 32, 64),
    developerKeySha256: buf.toString('hex', 64, 96),
    packageId: buf.toString('ascii', 96, 192).replace(/\0+$/, ''),
    versionName: buf.toString('ascii', 192, 224).replace(/\0+$/, ''),
    mask,
    permissions: names
  };
}

function generateStoreKey() {
  const { privateKey, publicKey } = crypto.generateKeyPairSync('ec', { namedCurve: 'prime256v1' });
  return { privateKey, publicKey };
}

module.exports = { build, describe, maskOf, generateStoreKey, TOTAL_BYTES, SIGNED_BYTES, PURPOSE_APP };
