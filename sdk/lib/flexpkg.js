'use strict';
// #############################################################
//  FLEX SDK · EMPAQUETADO Y FIRMA DE .flexpkg  (FLXP v1)
//  ------------------------------------------------------------
//  Escribe los bytes que el firmware valida en FlexOS_PkgCore.cpp:
//
//    0   "FLXP" | 4 ver(u16)=1 | 6 flags(u16)=0
//    8   manifestLen | 12 indexLen | 16 payloadLen   (u32 LE)
//    20  pubKeyLen(u16)=65 | 22 sigLen(u16)=64
//    24  signedHash[32] = SHA-256(manifest || index || payload)
//    56  grantLen(u32)  (0 = sin permisos firmados)
//    60  reservado[4] = 0
//    64  manifest | index | payload | pubKey[65] | sig[64] | grant?
//
//  EL GRANT VA AL FINAL A PROPOSITO: queda FUERA de signedHash, y por
//  eso puede referirse al hash del propio paquete sin morderse la cola.
// #############################################################
const crypto = require('crypto');
const fs = require('fs');
const path = require('path');
const { canonicalManifest } = require('./manifest');

const HDR = 64;

function sha256(buf) { return crypto.createHash('sha256').update(buf).digest(); }

// Clave publica en formato SEC1 sin comprimir (65 bytes: 0x04 || X || Y).
function publicKeyRaw(keyObject) {
  const jwk = keyObject.export({ format: 'jwk' });
  const x = Buffer.from(jwk.x, 'base64url');
  const y = Buffer.from(jwk.y, 'base64url');
  if (x.length !== 32 || y.length !== 32) throw new Error('la clave no es P-256');
  return Buffer.concat([Buffer.from([0x04]), x, y]);
}

// Firma ECDSA P-256 en formato crudo r||s (64 bytes), que es lo que lee el
// firmware (Node produce DER por defecto: se pide 'ieee-p1363').
//
// SE FIRMA EL MENSAJE COMPLETO, no su hash: crypto.sign('sha256', msg)
// calcula ECDSA(SHA-256(msg)), que es exactamente lo que verifica
// FlexOS_PkgCore. Pasar el hash con algoritmo null NO es equivalente en Node
// -- se comprobo, y produce una firma que la placa rechaza.
function signMessage(privateKey, message) {
  const sig = crypto.sign('sha256', message, { key: privateKey, dsaEncoding: 'ieee-p1363' });
  if (sig.length !== 64) throw new Error(`firma de ${sig.length} bytes, se esperaban 64`);
  return sig;
}

function loadKeyPair(pemPath) {
  const pem = fs.readFileSync(pemPath, 'utf8');
  const privateKey = crypto.createPrivateKey(pem);
  const publicKey = crypto.createPublicKey(privateKey);
  if (privateKey.asymmetricKeyType !== 'ec') throw new Error('la clave no es EC');
  const raw = publicKeyRaw(publicKey);
  return { privateKey, publicKey, raw, fingerprint: sha256(raw).toString('hex') };
}

function generateKeyPair() {
  const { privateKey, publicKey } = crypto.generateKeyPairSync('ec', { namedCurve: 'prime256v1' });
  const raw = publicKeyRaw(publicKey);
  return {
    privateKey, publicKey, raw,
    fingerprint: sha256(raw).toString('hex'),
    pem: privateKey.export({ format: 'pem', type: 'pkcs8' }).toString()
  };
}

// Recorre un directorio y devuelve [{ path, data }] con rutas relativas
// POSIX, en orden estable (el orden del indice es parte del formato).
function collectFiles(root) {
  const out = [];
  const walk = (dir, prefix) => {
    for (const name of fs.readdirSync(dir).sort()) {
      const full = path.join(dir, name);
      const rel = prefix ? `${prefix}/${name}` : name;
      const st = fs.statSync(full);
      if (st.isDirectory()) walk(full, rel);
      else if (st.isFile()) out.push({ path: rel, data: fs.readFileSync(full) });
    }
  };
  walk(root, '');
  return out;
}

// Construye el paquete. `files` son [{path, data}] con rutas ya validadas.
function build({ app, files, key, grant }) {
  if (!files.length) throw new Error('el paquete no tiene archivos');
  if (files.length > 128) throw new Error('un paquete no puede llevar mas de 128 archivos');
  if (!files.some((f) => f.path === app.entry)) {
    throw new Error(`el entry "${app.entry}" no esta entre los archivos del paquete`);
  }
  const seen = new Set();
  for (const f of files) {
    if (seen.has(f.path)) throw new Error(`ruta duplicada: ${f.path}`);
    seen.add(f.path);
    if (f.data.length > 8 * 1024 * 1024) throw new Error(`${f.path} pasa de 8 MB`);
  }

  const manifest = Buffer.from(canonicalManifest(app, key.fingerprint), 'utf8');

  const parts = [];
  const index = [];
  let offset = 0;
  for (const f of files) {
    index.push({ path: f.path, offset, size: f.data.length, sha256: sha256(f.data).toString('hex') });
    parts.push(f.data);
    offset += f.data.length;
  }
  const payload = Buffer.concat(parts);
  const indexBuf = Buffer.from(JSON.stringify(index), 'utf8');

  const signedMessage = Buffer.concat([manifest, indexBuf, payload]);
  const signedHash = sha256(signedMessage);
  const signature = signMessage(key.privateKey, signedMessage);

  const header = Buffer.alloc(HDR);
  header.write('FLXP', 0, 'ascii');
  header.writeUInt16LE(1, 4);
  header.writeUInt16LE(0, 6);
  header.writeUInt32LE(manifest.length, 8);
  header.writeUInt32LE(indexBuf.length, 12);
  header.writeUInt32LE(payload.length, 16);
  header.writeUInt16LE(65, 20);
  header.writeUInt16LE(64, 22);
  signedHash.copy(header, 24);
  header.writeUInt32LE(grant ? grant.length : 0, 56);

  const chunks = [header, manifest, indexBuf, payload, key.raw, signature];
  if (grant) chunks.push(grant);
  return {
    bytes: Buffer.concat(chunks),
    signedHash,
    manifest: manifest.toString('utf8'),
    index: indexBuf.toString('utf8'),
    fileCount: files.length,
    payloadBytes: payload.length
  };
}

module.exports = { build, collectFiles, loadKeyPair, generateKeyPair, sha256, publicKeyRaw, signMessage };
