#!/usr/bin/env node
'use strict';
// #############################################################
//  FLEX SDK · HERRAMIENTA DE LINEA DE ORDENES
//  ------------------------------------------------------------
//    flexpkg keygen   <salida.pem>          crea una clave de desarrollador
//    flexpkg validate <proyecto>            comprueba el manifest y el codigo
//    flexpkg asm      <proyecto>            ensambla el .flexasm a .flxb
//    flexpkg build    <proyecto> -k <pem>   ensambla, empaqueta y firma
//    flexpkg grant    <paquete> -k <pem-de-store> ...   emite un grant
//    flexpkg inspect  <paquete.flexpkg>     enseña lo que lleva dentro
//
//  Sin dependencias externas: solo Node. La criptografia es la del
//  propio Node (P-256, SHA-256), que produce exactamente los bytes que
//  el firmware verifica.
// #############################################################
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');

const { assemble, AsmError } = require('../lib/asm');
const mf = require('../lib/manifest');
const pkg = require('../lib/flexpkg');
const grant = require('../lib/grant');

function die(msg) {
  console.error(`flexpkg: ${msg}`);
  process.exit(1);
}

function readProject(dir) {
  const file = path.join(dir, 'flexapp.json');
  if (!fs.existsSync(file)) die(`no encuentro ${file}`);
  let app;
  try { app = JSON.parse(fs.readFileSync(file, 'utf8')); }
  catch (e) { die(`${file} no es JSON valido: ${e.message}`); }
  const { errors, warnings } = mf.validate(app);
  for (const w of warnings) console.log(`  aviso: ${w}`);
  if (errors.length) {
    for (const e of errors) console.error(`  error: ${e}`);
    die(`${file} no es valido`);
  }
  return app;
}

function flag(argv, name, fallback) {
  const i = argv.indexOf(name);
  return i >= 0 && i + 1 < argv.length ? argv[i + 1] : fallback;
}

// ---- keygen -------------------------------------------------------------
function cmdKeygen(argv) {
  const out = argv[0];
  if (!out) die('uso: flexpkg keygen <salida.pem>');
  if (fs.existsSync(out)) die(`${out} ya existe: no se sobrescribe una clave`);
  const k = pkg.generateKeyPair();
  fs.writeFileSync(out, k.pem, { mode: 0o600 });
  console.log(`clave de desarrollador escrita en ${out} (permisos 600)`);
  console.log(`huella developerKeySha256: ${k.fingerprint}`);
  console.log('');
  console.log('GUARDALA BIEN. Es la identidad de tus apps: una actualizacion');
  console.log('solo se acepta si va firmada con la MISMA clave. Si la pierdes,');
  console.log('no puedes actualizar lo que ya esta instalado.');
  console.log('NO la subas al repositorio (sdk/.gitignore ya ignora *.pem).');
}

// ---- asm ----------------------------------------------------------------
function assembleProject(dir, app) {
  const src = path.join(dir, app.source || 'src/main.flexasm');
  if (!fs.existsSync(src)) die(`no encuentro el fuente ${src}`);
  let res;
  try { res = assemble(fs.readFileSync(src, 'utf8')); }
  catch (e) {
    if (e instanceof AsmError) die(`${src}: ${e.message}`);
    throw e;
  }
  // El manifest tiene que declarar TODO permiso de sistema que el codigo use,
  // y no mas: pedir de mas es lo que hace que un revisor no se fie del paquete.
  const { SYSCALLS } = require('../lib/isa');
  const needed = new Set();
  for (const name of res.usedSyscalls) {
    const p = SYSCALLS[name] && SYSCALLS[name].perm;
    if (p) needed.add(p);
  }
  const declared = new Set(app.systemPermissions || []);
  for (const n of needed) {
    if (!declared.has(n)) die(`el codigo usa un servicio que exige "${n}" y el manifest no lo declara`);
  }
  for (const d of declared) {
    if (!needed.has(d)) console.log(`  aviso: el manifest declara "${d}" y el codigo no lo usa`);
  }
  // La memoria lineal del bytecode no puede pasarse de lo que declara el
  // manifest: el firmware lo rechaza al instalar, asi que se avisa aqui.
  const maxMem = app.limits.memoryKB * 1024;
  if (res.stats.mem > maxMem) {
    die(`.mem pide ${res.stats.mem} bytes y limits.memoryKB solo permite ${maxMem}`);
  }
  return res;
}

function cmdAsm(argv) {
  const dir = argv[0] || '.';
  const app = readProject(dir);
  if (app.runtime !== 'flex-app-v1') die('solo las apps flex-app-v1 se ensamblan');
  const res = assembleProject(dir, app);
  // `entry` es la ruta DENTRO del paquete, y `filesDir` es la carpeta cuyo
  // contenido se convierte en la raiz del paquete: el bytecode se escribe
  // donde luego lo va a recoger el empaquetado.
  const out = path.join(dir, app.filesDir || 'app', app.entry);
  fs.mkdirSync(path.dirname(out), { recursive: true });
  fs.writeFileSync(out, res.image);
  console.log(`${out}: ${res.stats.bytes} bytes`);
  console.log(`  codigo ${res.stats.code} B · constantes ${res.stats.consts} B · funciones ${res.stats.funcs}`);
  console.log(`  memoria ${res.stats.mem} B · pila ${res.stats.stack} · locales ${res.stats.frames} · llamadas ${res.stats.calldepth}`);
  console.log(`  manejadores: ${res.stats.handlers.join(', ')}`);
  if (res.usedSyscalls.length) console.log(`  servicios: ${res.usedSyscalls.join(', ')}`);
  return res;
}

// ---- validate -----------------------------------------------------------
function cmdValidate(argv) {
  const dir = argv[0] || '.';
  const app = readProject(dir);
  console.log(`manifest valido: ${app.id} ${app.versionName} (codigo ${app.versionCode}), runtime ${app.runtime}`);
  if (app.runtime === 'flex-app-v1') {
    const res = assembleProject(dir, app);
    console.log(`codigo valido: ${res.stats.bytes} bytes, manejadores ${res.stats.handlers.join(', ')}`);
  }
  const root = path.join(dir, app.filesDir || 'app');
  if (fs.existsSync(root)) {
    for (const f of pkg.collectFiles(root)) {
      if (!mf.isSafePath(f.path)) die(`ruta no permitida en el paquete: ${f.path}`);
    }
  }
  console.log('todo listo para empaquetar.');
}

// ---- build --------------------------------------------------------------
function cmdBuild(argv) {
  const dir = argv[0] || '.';
  const keyPath = flag(argv, '-k') || flag(argv, '--key');
  if (!keyPath) die('hace falta la clave de desarrollador: -k <clave.pem>');
  const app = readProject(dir);
  if (app.runtime === 'flex-app-v1') cmdAsm([dir]);

  const filesDir = path.join(dir, app.filesDir || 'app');
  if (!fs.existsSync(filesDir)) die(`no encuentro la carpeta de archivos ${filesDir}`);
  const files = pkg.collectFiles(filesDir);
  for (const f of files) if (!mf.isSafePath(f.path)) die(`ruta no permitida: ${f.path}`);

  const key = pkg.loadKeyPair(keyPath);
  let grantBuf = null;
  const grantPath = flag(argv, '--grant');
  if (grantPath) {
    grantBuf = fs.readFileSync(grantPath);
    if (grantBuf.length !== grant.TOTAL_BYTES) die(`el grant tiene ${grantBuf.length} bytes y deberia tener ${grant.TOTAL_BYTES}`);
  }

  const built = pkg.build({ app, files, key, grant: grantBuf });
  const out = flag(argv, '-o') || path.join(dir, `${app.id}-${app.versionName}.flexpkg`);
  fs.writeFileSync(out, built.bytes);
  console.log(`${out}: ${built.bytes.length} bytes`);
  console.log(`  archivos ${built.fileCount} · payload ${built.payloadBytes} B`);
  console.log(`  developerKeySha256 ${key.fingerprint}`);
  console.log(`  packageSha256      ${built.signedHash.toString('hex')}`);
  if (grantBuf) console.log(`  grant              ${grantBuf.length} bytes`);
  else if ((app.systemPermissions || []).length) {
    console.log('  grant              NINGUNO: la app se instalara y funcionara, pero SIN permisos de sistema.');
    console.log('                     Pidelo en Flex Developer Studio con el packageSha256 de arriba.');
  }
}

// ---- grant --------------------------------------------------------------
function cmdGrant(argv) {
  const pkgPath = argv[0];
  if (!pkgPath) die('uso: flexpkg grant <paquete.flexpkg> -k <clave-de-store.pem> -p perm1,perm2 [--from <epoch>] [--to <epoch>]');
  const keyPath = flag(argv, '-k') || flag(argv, '--key');
  if (!keyPath) die('hace falta la clave de FLEX STORE: -k <clave.pem>');
  const permList = (flag(argv, '-p') || flag(argv, '--permissions') || '').split(',').filter(Boolean);
  if (!permList.length) die('hace falta al menos un permiso: -p storage.app,display.landscape');

  const bytes = fs.readFileSync(pkgPath);
  const info = readPackage(bytes);
  const storeKey = crypto.createPrivateKey(fs.readFileSync(keyPath, 'utf8'));

  const blob = grant.build({
    packageId: info.id,
    versionName: info.versionName,
    versionCode: info.versionCode,
    packageSha256: info.signedHash,
    developerKeySha256: info.developerKeySha256,
    permissions: permList,
    notBefore: Number(flag(argv, '--from', '0')),
    notAfter: Number(flag(argv, '--to', '0'))
  }, storeKey);

  const out = flag(argv, '-o') || pkgPath.replace(/\.flexpkg$/, '') + '.flexgrant';
  fs.writeFileSync(out, blob);
  console.log(`${out}: ${blob.length} bytes`);
  console.log(`  para ${info.id} ${info.versionName} (codigo ${info.versionCode})`);
  console.log(`  paquete ${info.signedHash.toString('hex')}`);
  console.log(`  permisos ${permList.join(', ')}`);
  console.log('');
  console.log('Vuelve a empaquetar con  flexpkg build <proyecto> -k <tu-clave> --grant ' + out);
}

// ---- inspect ------------------------------------------------------------
// Lectura MINIMA del contenedor, solo para enseñar lo que lleva. La
// validacion de verdad la hace el firmware (tests/host/build/flexpkgcheck).
function readPackage(bytes) {
  if (bytes.length < 64 || bytes.toString('ascii', 0, 4) !== 'FLXP') die('no es un .flexpkg');
  const manifestLen = bytes.readUInt32LE(8);
  const indexLen = bytes.readUInt32LE(12);
  const payloadLen = bytes.readUInt32LE(16);
  const grantLen = bytes.readUInt32LE(56);
  const signedHash = bytes.subarray(24, 56);
  const manifest = JSON.parse(bytes.toString('utf8', 64, 64 + manifestLen));
  const index = JSON.parse(bytes.toString('utf8', 64 + manifestLen, 64 + manifestLen + indexLen));
  const sigAt = 64 + manifestLen + indexLen + payloadLen;
  return {
    id: manifest.id,
    name: manifest.name,
    versionName: manifest.version.name,
    versionCode: manifest.version.code,
    runtime: manifest.runtime,
    entry: manifest.entry,
    developerKeySha256: manifest.developerKeySha256,
    permissions: manifest.permissions || [],
    systemPermissions: manifest.systemPermissions || [],
    limits: manifest.limits,
    signedHash, index, grantLen,
    grant: grantLen ? bytes.subarray(sigAt + 65 + 64, sigAt + 65 + 64 + grantLen) : null,
    totalBytes: bytes.length
  };
}

function cmdInspect(argv) {
  const p = argv[0];
  if (!p) die('uso: flexpkg inspect <paquete.flexpkg>');
  const info = readPackage(fs.readFileSync(p));
  console.log(`${info.id}  ${info.name}  ${info.versionName} (codigo ${info.versionCode})`);
  console.log(`  runtime            ${info.runtime}`);
  console.log(`  entry              ${info.entry}`);
  console.log(`  tamano             ${info.totalBytes} bytes`);
  console.log(`  packageSha256      ${info.signedHash.toString('hex')}`);
  console.log(`  developerKeySha256 ${info.developerKeySha256}`);
  console.log(`  limites            ${JSON.stringify(info.limits)}`);
  console.log(`  permisos manifest  ${info.permissions.join(', ') || '(ninguno)'}`);
  console.log(`  permisos sistema   ${info.systemPermissions.join(', ') || '(ninguno declarado)'}`);
  console.log('  archivos:');
  for (const e of info.index) console.log(`    ${String(e.size).padStart(8)}  ${e.path}`);
  if (info.grant) {
    const g = grant.describe(info.grant);
    console.log('  GRANT firmado por Flex Store:');
    console.log(`    para       ${g.packageId} ${g.versionName} (codigo ${g.versionCode})`);
    console.log(`    paquete    ${g.packageSha256}`);
    console.log(`    concede    ${g.permissions.join(', ')}`);
    console.log(`    validez    ${g.notBefore || 'sin limite'} .. ${g.notAfter || 'sin limite'}`);
    const ok = g.packageSha256 === info.signedHash.toString('hex') &&
               g.packageId === info.id && g.versionCode === info.versionCode;
    console.log(`    coherente  ${ok ? 'si' : 'NO -- la placa lo va a rechazar'}`);
  } else {
    console.log('  GRANT: ninguno (la app funcionara sin permisos de sistema)');
  }
}

// ---- main ---------------------------------------------------------------
const [, , cmd, ...rest] = process.argv;
switch (cmd) {
  case 'keygen': cmdKeygen(rest); break;
  case 'validate': cmdValidate(rest); break;
  case 'asm': cmdAsm(rest); break;
  case 'build': cmdBuild(rest); break;
  case 'grant': cmdGrant(rest); break;
  case 'inspect': cmdInspect(rest); break;
  default:
    console.log('flexpkg — herramientas de Flex Store\n');
    console.log('  flexpkg keygen   <salida.pem>                       clave de desarrollador');
    console.log('  flexpkg validate <proyecto>                         manifest + codigo');
    console.log('  flexpkg asm      <proyecto>                         .flexasm -> .flxb');
    console.log('  flexpkg build    <proyecto> -k <clave.pem> [--grant <g>] [-o <salida>]');
    console.log('  flexpkg grant    <paquete> -k <clave-store.pem> -p perm1,perm2 [--from N] [--to N]');
    console.log('  flexpkg inspect  <paquete.flexpkg>');
    process.exit(cmd ? 1 : 0);
}

module.exports = { readPackage };
