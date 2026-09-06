'use strict';
// #############################################################
//  PRUEBAS DEL SDK DE FLEX STORE
//  ------------------------------------------------------------
//  Lo importante no es que el SDK "funcione": es que lo que produce
//  sea EXACTAMENTE lo que el firmware acepta. Por eso cada paquete que
//  se construye aqui se valida despues con tests/host/build/flexpkgcheck,
//  que compila el mismo FlexOS_PkgCore.cpp que corre en el ESP32-P4, y
//  la app de ejemplo se EJECUTA con tests/host/build/flexapprun, que usa
//  el mismo FlexAppManager.
//
//    node --test sdk/test/sdk.test.js
//
//  Requiere haber compilado antes las herramientas:
//    make -C tests/host tools
//  Si no estan, las pruebas que las necesitan se SALTAN diciendolo (no
//  se dan por buenas en silencio).
// #############################################################
const test = require('node:test');
const assert = require('node:assert');
const fs = require('fs');
const os = require('os');
const path = require('path');
const crypto = require('crypto');
const { execFileSync } = require('child_process');

const ROOT = path.resolve(__dirname, '..', '..');
const SDK = path.join(ROOT, 'sdk');
const CHECK = path.join(ROOT, 'tests', 'host', 'build', 'flexpkgcheck');
const RUN = path.join(ROOT, 'tests', 'host', 'build', 'flexapprun');

const { assemble, AsmError } = require('../lib/asm');
const isa = require('../lib/isa');
const mf = require('../lib/manifest');
const pkg = require('../lib/flexpkg');
const grant = require('../lib/grant');

const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'flexsdk-'));
function tmpFile(name, data) {
  const p = path.join(tmp, name);
  fs.mkdirSync(path.dirname(p), { recursive: true });
  fs.writeFileSync(p, data);
  return p;
}
function haveTool(p) { return fs.existsSync(p); }
function firmwareCheck(bytes) {
  const p = tmpFile(`chk-${crypto.randomUUID()}.flexpkg`, bytes);
  try {
    return JSON.parse(execFileSync(CHECK, [p, '--json']).toString());
  } catch (e) {
    // flexpkgcheck sale con 1 cuando el paquete es invalido: eso NO es un
    // error de la prueba, es la respuesta.
    if (e.stdout) return JSON.parse(e.stdout.toString());
    throw e;
  }
}

// ---------------------------------------------------------------------
test('el ensamblador rechaza lo que el firmware rechazaria', () => {
  assert.throws(() => assemble('.func onTick args=a\n  chachi\n.end\n'), AsmError);
  assert.throws(() => assemble('.func onTick args=a\n  jmp ninguna\n.end\n'), AsmError);
  assert.throws(() => assemble('.func onTick args=a,b\n  ret\n.end\n'), AsmError,
    'onTick tiene que llevar exactamente un argumento');
  assert.throws(() => assemble('.func suma args=a\n  ret\n.end\n'), AsmError,
    'un programa sin manejadores no es una app');
  assert.throws(() => assemble('.func onTick args=a\n  sys no.existe\n.end\n'), AsmError);
  assert.throws(() => assemble('.mem 7\n.func onTick args=a\n  ret\n.end\n'), AsmError,
    '.mem tiene que ser multiplo de 4');
});

test('el ensamblador produce un contenedor FLXB coherente', () => {
  const r = assemble([
    '.mem 256', '.stack 32', '.frames 32', '.calldepth 8',
    '.const HOLA "hola"',
    '.global n',
    '.func onStart args=',
    '  pushk HOLA',
    '  sys sys.log',
    '  drop',
    '  push 41',
    '  push 1',
    '  add',
    '  stglobal n',
    '  ret',
    '.end',
    '.func onTick args=ms locals=x',
    '  ldglobal n',
    '  stlocal x',
    '  ret',
    '.end'
  ].join('\n'));
  assert.strictEqual(r.image.toString('ascii', 0, 4), 'FLXB');
  assert.strictEqual(r.image.readUInt16LE(4), 1);
  assert.strictEqual(r.stats.mem, 256);
  assert.deepStrictEqual(r.stats.handlers, ['onStart', 'onTick']);
  assert.deepStrictEqual(r.usedSyscalls, ['sys.log']);
  // tamaño declarado == tamaño real (la regla que aplica flexVmValidate)
  const codeLen = r.image.readUInt32LE(8);
  const constLen = r.image.readUInt32LE(12);
  const funcs = r.image.readUInt16LE(28);
  assert.strictEqual(48 + funcs * 8 + constLen + codeLen, r.image.length);
});

test('la tabla de syscalls del SDK coincide con la del firmware', () => {
  // Vectores dorados: los mismos que fija tests/host/test_apphost.cpp.
  const golden = {
    'gfx.fill_rect': [0x0002, 5, ''],
    'gfx.text': [0x0007, 6, ''],
    'gfx.blit_scaled': [0x000D, 7, ''],
    'gfx.present': [0x0014, 0, ''],
    'ui.button': [0x0024, 7, ''],
    'time.now_ms': [0x0030, 0, ''],
    'time.timer_set': [0x0033, 3, ''],
    'store.write': [0x0040, 4, 'storage.app'],
    'sys.notify': [0x0056, 4, ''],
    'display.landscape': [0x0060, 1, 'display.landscape'],
    'perf.metric': [0x0070, 1, 'system.performance.metrics'],
    'mem.reserve': [0x0074, 1, 'system.psram.measure'],
    'temp.milli_c': [0x0078, 0, 'system.temperature.read'],
    'cpu.stress_begin': [0x0079, 2, 'system.cpu.stress'],
    'bench.begin': [0x007E, 1, 'benchmark.run']
  };
  for (const [name, [id, argc, perm]] of Object.entries(golden)) {
    const s = isa.SYSCALLS[name];
    assert.ok(s, `falta la syscall ${name}`);
    assert.strictEqual(s.id, id, `id de ${name}`);
    assert.strictEqual(s.argc, argc, `aridad de ${name}`);
    assert.strictEqual(s.perm, perm, `permiso de ${name}`);
  }
  // Ningun id repetido.
  const ids = Object.values(isa.SYSCALLS).map((s) => s.id);
  assert.strictEqual(new Set(ids).size, ids.length, 'hay ids de syscall repetidos');
  // Los 8 permisos de sistema del enunciado, ni uno mas ni uno menos.
  assert.deepStrictEqual(Object.keys(isa.SYS_PERMISSIONS).sort(), [
    'benchmark.run', 'display.exclusive', 'display.landscape', 'storage.app',
    'system.cpu.stress', 'system.performance.metrics', 'system.psram.measure',
    'system.temperature.read'
  ]);
});

test('el validador de manifest aplica las reglas del firmware', () => {
  const base = {
    id: 'com.flexos.ejemplo.demo', name: 'Demo', versionName: '1.0.0', versionCode: 1,
    minFlexOS: '1.0.0', runtime: 'flex-ui-1', entry: 'app/main.json',
    limits: { memoryKB: 128, storageKB: 0 }, permissions: []
  };
  assert.deepStrictEqual(mf.validate(base).errors, []);
  const bad = (patch, needle) => {
    const { errors } = mf.validate({ ...base, ...patch });
    assert.ok(errors.some((e) => e.includes(needle)), `esperaba un error sobre "${needle}", hubo: ${errors}`);
  };
  bad({ id: 'Com.Flexos.Demo' }, 'id invalido');
  bad({ id: 'com.demo' }, 'id invalido');
  bad({ entry: '../fuera.json' }, 'entry invalido');
  bad({ entry: '/absoluta.json' }, 'entry invalido');
  bad({ versionName: '1.0' }, 'versionName');
  bad({ versionCode: 0 }, 'versionCode');
  bad({ runtime: 'flex-super-9' }, 'runtime invalido');
  bad({ limits: { memoryKB: 8, storageKB: 0 } }, 'memoryKB');
  bad({ permissions: ['telepatia'] }, 'permiso de manifest desconocido');
  bad({ systemPermissions: ['system.cpu.stress'] }, 'flex-ui-1 no puede pedir permisos de sistema');
  bad({ name: 'con "comillas"' }, 'name invalido');
});

// ---------------------------------------------------------------------
test('un paquete del SDK lo acepta el nucleo del firmware', { skip: !haveTool(CHECK) && 'falta tests/host/build/flexpkgcheck (make -C tests/host tools)' }, () => {
  const key = pkg.generateKeyPair();
  const app = {
    id: 'com.flexos.ejemplo.demo', name: 'Demo', versionName: '1.2.3', versionCode: 5,
    minFlexOS: '1.0.0', runtime: 'flex-ui-1', entry: 'app/main.json',
    summary: 'Una app declarativa', category: 'Utilidades',
    limits: { memoryKB: 128, storageKB: 0 }, permissions: ['notifications']
  };
  const entry = Buffer.from(JSON.stringify({
    schema: 1, startScreen: 'home',
    screens: [{ id: 'home', title: 'Hola', components: [
      { type: 'text', id: 't', text: 'Hola Flex', x: 20, y: 120, width: 400 }] }]
  }));
  const built = pkg.build({ app, files: [{ path: 'app/main.json', data: entry }], key });
  const r = firmwareCheck(built.bytes);
  assert.ok(r.ok, `el firmware lo rechazo: ${r.error}`);
  assert.strictEqual(r.id, app.id);
  assert.strictEqual(r.versionCode, 5);
  assert.strictEqual(r.runtime, 'flex-ui-1');
  assert.strictEqual(r.developerKeySha256, key.fingerprint);
  assert.strictEqual(r.packageSha256, built.signedHash.toString('hex'));
  assert.strictEqual(r.grantBytes, 0);
});

test('el firmware rechaza un paquete manipulado', { skip: !haveTool(CHECK) && 'falta flexpkgcheck' }, () => {
  const key = pkg.generateKeyPair();
  const app = {
    id: 'com.flexos.ejemplo.demo', name: 'Demo', versionName: '1.0.0', versionCode: 1,
    minFlexOS: '1.0.0', runtime: 'flex-ui-1', entry: 'app/main.json',
    limits: { memoryKB: 128, storageKB: 0 }, permissions: []
  };
  const entry = Buffer.from(JSON.stringify({
    schema: 1, startScreen: 'home',
    screens: [{ id: 'home', title: 'Hola', components: [] }]
  }));
  const built = pkg.build({ app, files: [{ path: 'app/main.json', data: entry }], key });

  const tamperPayload = Buffer.from(built.bytes);
  tamperPayload[tamperPayload.length - 200] ^= 0xFF;
  assert.ok(!firmwareCheck(tamperPayload).ok, 'un byte cambiado tiene que tumbarlo');

  const tamperSig = Buffer.from(built.bytes);
  tamperSig[tamperSig.length - 10] ^= 0xFF;
  assert.ok(!firmwareCheck(tamperSig).ok, 'una firma alterada tiene que tumbarlo');

  const truncated = built.bytes.subarray(0, built.bytes.length - 1);
  assert.ok(!firmwareCheck(truncated).ok, 'un paquete truncado tiene que tumbarlo');
});

test('el grant del SDK viaja en el paquete y cuadra con el', { skip: !haveTool(CHECK) && 'falta flexpkgcheck' }, () => {
  const key = pkg.generateKeyPair();
  const store = grant.generateStoreKey();
  const app = {
    id: 'com.flexos.ejemplo.contador', name: 'Contador', versionName: '1.0.0', versionCode: 1,
    minFlexOS: '1.0.0', runtime: 'flex-app-v1', entry: 'app/main.flxb',
    limits: { memoryKB: 128, storageKB: 16 }, permissions: [],
    systemPermissions: ['storage.app']
  };
  const code = assemble('.mem 64\n.func onTick args=ms\n  ret\n.end\n').image;
  const first = pkg.build({ app, files: [{ path: 'app/main.flxb', data: code }], key });

  const blob = grant.build({
    packageId: app.id, versionName: app.versionName, versionCode: app.versionCode,
    packageSha256: first.signedHash, developerKeySha256: key.fingerprint,
    permissions: ['storage.app']
  }, store.privateKey);
  assert.strictEqual(blob.length, grant.TOTAL_BYTES);

  const withGrant = pkg.build({ app, files: [{ path: 'app/main.flxb', data: code }], key, grant: blob });
  // EL TRAILER NO CAMBIA EL HASH FIRMADO: por eso el grant puede referirse al
  // hash del propio paquete sin morderse la cola.
  assert.deepStrictEqual(withGrant.signedHash, first.signedHash);

  const r = firmwareCheck(withGrant.bytes);
  assert.ok(r.ok, `el firmware lo rechazo: ${r.error}`);
  assert.strictEqual(r.grantBytes, grant.TOTAL_BYTES);
  assert.strictEqual(r.runtime, 'flex-app-v1');

  const d = grant.describe(blob);
  assert.strictEqual(d.packageSha256, first.signedHash.toString('hex'));
  assert.deepStrictEqual(d.permissions, ['storage.app']);
  assert.strictEqual(d.permissionCount, 1);
});

// ---------------------------------------------------------------------
test('la app de ejemplo cuenta, cronometra y guarda de verdad', { skip: !haveTool(RUN) && 'falta tests/host/build/flexapprun' }, () => {
  const dir = path.join(SDK, 'examples', 'contador');
  const app = JSON.parse(fs.readFileSync(path.join(dir, 'flexapp.json'), 'utf8'));
  const res = assemble(fs.readFileSync(path.join(dir, app.source), 'utf8'));
  const flxb = tmpFile('contador.flxb', res.image);

  const out = JSON.parse(execFileSync(RUN, [flxb, '--json']).toString());
  assert.ok(out.started, `no arranco: ${out.reason}`);
  assert.strictEqual(out.navTitle, 'Contador Flex', 'la app pone su titulo con ui.nav_title');
  assert.strictEqual(out.global0, 1, 'el toque en el boton sumo exactamente 1');
  assert.ok(out.global1 >= 1, 'el temporizador de 1 s avanzo el cronometro');
  assert.ok(out.presents >= 5, 'la app publica cuadros');
  assert.ok(out.drawsAfterTap > 0, 'el toque provoca un repintado');
  assert.strictEqual(out.files, 'contador.bin', 'guardo en su carpeta privada');
  assert.strictEqual(out.fileBytes, 8, 'guardo los 8 bytes de estado');
  assert.strictEqual(out.granted, isa.SYS_PERMISSIONS['storage.app'], 'se le concedio storage.app');
  assert.strictEqual(out.drawDropped, 0, 'no se paso del presupuesto de dibujo');
  assert.strictEqual(out.state, 'Detenida', 'cierra limpiamente');
});

test('sin grant, la app de ejemplo funciona pero NO guarda', { skip: !haveTool(RUN) && 'falta flexapprun' }, () => {
  const dir = path.join(SDK, 'examples', 'contador');
  const app = JSON.parse(fs.readFileSync(path.join(dir, 'flexapp.json'), 'utf8'));
  const res = assemble(fs.readFileSync(path.join(dir, app.source), 'utf8'));
  const flxb = tmpFile('contador2.flxb', res.image);

  const out = JSON.parse(execFileSync(RUN, [flxb, '--nogrant', '--json']).toString());
  assert.ok(out.started, 'una app sin permisos privilegiados arranca igual');
  assert.strictEqual(out.granted, 0, 'no se concede nada');
  assert.strictEqual(out.global0, 1, 'la logica sigue funcionando');
  assert.strictEqual(out.files, '', 'y no toca el almacenamiento');
});

test('la plantilla minima arranca', { skip: !haveTool(RUN) && 'falta flexapprun' }, () => {
  const dir = path.join(SDK, 'examples', 'plantilla');
  const app = JSON.parse(fs.readFileSync(path.join(dir, 'flexapp.json'), 'utf8'));
  const res = assemble(fs.readFileSync(path.join(dir, app.source), 'utf8'));
  const flxb = tmpFile('plantilla.flxb', res.image);
  const out = JSON.parse(execFileSync(RUN, [flxb, '--perms', '0', '--json']).toString());
  assert.ok(out.started, `la plantilla no arranco: ${out.reason}`);
  assert.ok(out.presents >= 1, 'y pinta algo');
});

test.after(() => { fs.rmSync(tmp, { recursive: true, force: true }); });
