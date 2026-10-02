// #############################################################
//  FLEX STORAGE · EMPAREJAMIENTO DE EXTREMO A EXTREMO
//  ------------------------------------------------------------
//  La app del telefono (AttachClient.kt, el codigo de Flex Phone, en la JVM)
//  contra el servidor web REAL del P4 (flexweb_host: FlexOS_MediaWeb +
//  FlexOS_StorageCore compilados en el PC, por sockets de verdad):
//
//    navegador: codigo del QR -> sesion -> "Activar Flex Cloud" -> oferta
//    app:       ECDH + codigo de 6 cifras + sondeo con su prueba
//    "pantalla del P4" (stdin de flexweb_host): aprobar / rechazar
//
//  Se comprueba que los dos lados ensenan el MISMO codigo, que nada se
//  guarda hasta aprobarlo, que la oferta es de un solo uso, que un telefono
//  ya emparejado entra sin preguntar y que rechazar deja al telefono fuera.
//
//    node pair_e2e.js        (lo lanza phone_e2e.sh; necesita Java y Gradle)
// #############################################################
'use strict';
const cp = require('child_process');
const fs = require('fs');
const os = require('os');
const path = require('path');
const http = require('http');
const readline = require('readline');

const HERE = __dirname;
const HOST_BIN = path.join(HERE, 'build', 'flexweb_host');
const PHONE_DIR = path.join(HERE, '..', '..', 'android', 'FlexPhone');

let run = 0, fail = 0;
function check(cond, msg) { run++; if (!cond) { fail++; console.log('  FALLO: ' + msg); } else if (process.env.E2E_VERBOSE) console.log('  ok: ' + msg); }

function classpath() {
  const out = cp.execSync('gradle -q --console=plain :storage:printTestClasspath', { cwd: PHONE_DIR, encoding: 'utf8', stdio: ['ignore', 'pipe', 'ignore'] });
  const line = out.split('\n').find((l) => l.startsWith('CLASSPATH='));
  if (!line) throw new Error('sin classpath de :storage');
  return line.slice('CLASSPATH='.length);
}

// Lineas JSON de un flujo, con espera por condicion.
function jsonLines(stream) {
  const lines = [];
  const waiters = [];
  readline.createInterface({ input: stream }).on('line', (l) => {
    let j = null;
    try { j = JSON.parse(l); } catch (e) { return; }
    lines.push(j);
    for (const w of waiters.slice()) if (w.pred(j)) { waiters.splice(waiters.indexOf(w), 1); w.resolve(j); }
  });
  return {
    lines,
    wait(pred, ms = 30000) {
      const hit = lines.find(pred);
      if (hit) return Promise.resolve(hit);
      return new Promise((resolve, reject) => {
        const w = { pred, resolve };
        waiters.push(w);
        setTimeout(() => { const i = waiters.indexOf(w); if (i >= 0) { waiters.splice(i, 1); reject(new Error('plazo vencido')); } }, ms);
      });
    },
  };
}

function req(port, method, p, body, headers = {}) {
  return new Promise((resolve) => {
    const data = body ? Buffer.from(body) : null;
    const r = http.request({ host: '127.0.0.1', port, method, path: p, headers: Object.assign({ Host: '127.0.0.1:' + port }, data ? { 'Content-Length': data.length } : {}, headers) }, (res) => {
      const chunks = [];
      res.on('data', (c) => chunks.push(c));
      res.on('end', () => resolve({ status: res.statusCode, headers: res.headers, body: Buffer.concat(chunks).toString('utf8') }));
    });
    r.on('error', () => resolve({ status: -1, headers: {}, body: '' }));
    if (data) r.write(data);
    r.end();
  });
}

function attach(cpath, port, offer, phoneId, pairFile) {
  const p = cp.spawn('java', ['-cp', cpath, 'com.flexos.flexphone.cloud.AttachCliKt', '127.0.0.1:' + port, offer, phoneId, '47830', pairFile],
    { stdio: ['ignore', 'pipe', 'pipe'] });
  p.stderr.on('data', () => {});
  return { proc: p, out: jsonLines(p.stdout) };
}

(async () => {
  console.log('=== Flex Storage: emparejamiento app (Kotlin) <-> servidor web del P4 (C++) ===');
  const cpath = classpath();
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'pair-e2e-'));
  const host = cp.spawn(HOST_BIN, [path.join(dir, 'disco'), '--storage'], { stdio: ['pipe', 'pipe', 'pipe'] });
  const hostOut = jsonLines(host.stdout);
  const hostErr = jsonLines(host.stderr);
  const cleanup = () => { try { host.stdin.end(); } catch (e) { /* */ } try { host.kill(); } catch (e) { /* */ } fs.rmSync(dir, { recursive: true, force: true }); };
  try {
    const first = await hostOut.wait((j) => j.port);
    const port = first.port;
    // ---- el navegador: codigo del QR y oferta
    const pr = await req(port, 'POST', '/api/pair', 'code=' + first.code, { 'X-Flex': '1', 'Content-Type': 'application/x-www-form-urlencoded' });
    const cookie = ((pr.headers['set-cookie'] || [''])[0]).split(';')[0];
    check(pr.status === 200 && cookie.startsWith('fxs='), 'el navegador se empareja con el codigo del QR');
    const auth = { 'X-Flex': '1', Cookie: cookie };
    const offerOf = async () => JSON.parse((await req(port, 'POST', '/api/fs/phone/offer', '', auth)).body);
    let of = await offerOf();
    check(/^[0-9a-f]{32}$/.test(of.offer) && of.link === 'flexstorage://attach?h=127.0.0.1%3A' + port + '&o=' + of.offer, 'oferta y enlace para abrir la app');
    check((await req(port, 'POST', '/api/fs/phone/offer', '', { 'X-Flex': '1' })).status === 401, 'sin sesion web no hay oferta');
    check((await req(port, 'POST', '/api/fs/phone/pair', 'offer=' + of.offer, {})).status === 403, 'emparejar sin X-Flex: 403');

    // ---- la app: codigo de verificacion en los dos lados, aprobado en "pantalla"
    const pairFile = path.join(dir, 'telefono.json');
    let a = attach(cpath, port, of.offer, 'a55-e2e', pairFile);
    const sasApp = await a.out.wait((j) => j.sas);
    const sasP4 = await hostErr.wait((j) => j.pairWaiting === 1);
    check(sasApp.sas === sasP4.sas && /^\d{6}$/.test(sasApp.sas), 'el MISMO codigo de 6 cifras en la app y en el P4 (' + sasApp.sas + ')');
    check(sasApp.p4 === 'Flex OS Ultra (pruebas)', 'la app ensena el nombre del P4');
    await new Promise((r) => setTimeout(r, 1700));
    check(!fs.existsSync(pairFile), 'nada guardado en el telefono antes de aprobarlo');
    host.stdin.write('approve\n');
    const done = await a.out.wait((j) => j.paired !== undefined, 20000);
    check(done.paired === true && done.p4Id === 'flexos-a1b2c3d4e5f6', 'emparejado: la app verifico la prueba del P4');
    await hostErr.wait((j) => j.paired);
    const saved = JSON.parse(fs.readFileSync(pairFile, 'utf8'));
    check(/^[0-9a-f]{64}$/.test(saved.key) && saved.p4Id === 'flexos-a1b2c3d4e5f6', 'la app guardo la clave (en Android, con el Keystore)');
    const ov = JSON.parse((await req(port, 'GET', '/api/fs/overview', null, { Cookie: cookie })).body);
    check(ov.phone && ov.phone.state === 'ready', 'el P4 tiene el telefono emparejado');

    // ---- la oferta era de un solo uso
    a = attach(cpath, port, of.offer, 'a55-e2e', path.join(dir, 'otro.json'));
    const reused = await a.out.wait((j) => j.paired !== undefined, 20000);
    check(reused.paired === false && /ya no vale/.test(reused.error), 'la misma oferta no vale dos veces');

    // ---- el mismo telefono vuelve (otra oferta): demuestra su clave y entra sin preguntar
    const nWaiting = hostErr.lines.filter((j) => j.pairWaiting === 1).length;
    of = await offerOf();
    a = attach(cpath, port, of.offer, 'a55-e2e', pairFile);
    const again = await a.out.wait((j) => j.paired !== undefined, 20000);
    check(again.paired === true && !a.out.lines.some((j) => j.sas), 'telefono conocido: emparejado sin preguntar ni ensenar codigo');
    check(hostErr.lines.filter((j) => j.pairWaiting === 1).length === nWaiting, 'y en la pantalla del P4 no aparecio nada');
    const saved2 = JSON.parse(fs.readFileSync(pairFile, 'utf8'));
    check(saved2.key !== saved.key, 'con una clave NUEVA (ECDH nuevo)');

    // ---- otro telefono: rechazado en la pantalla
    of = await offerOf();
    const intruder = path.join(dir, 'intruso.json');
    a = attach(cpath, port, of.offer, 'intruso', intruder);
    await a.out.wait((j) => j.sas);
    host.stdin.write('deny\n');
    const denied = await a.out.wait((j) => j.paired !== undefined, 20000);
    check(denied.paired === false && /Rechazado/.test(denied.error) && !fs.existsSync(intruder), 'rechazado en la pantalla: la app no guarda nada');
    const ov2 = JSON.parse((await req(port, 'GET', '/api/fs/overview', null, { Cookie: cookie })).body);
    check(ov2.phone.state === 'ready', 'el telefono emparejado sigue siendo el de antes');
  } catch (e) {
    fail++; console.log('  FALLO: ' + e.message);
  } finally {
    cleanup();
  }
  console.log('=== ' + run + ' comprobaciones, ' + fail + ' fallos ===');
  process.exit(fail ? 1 : 0);
})();
