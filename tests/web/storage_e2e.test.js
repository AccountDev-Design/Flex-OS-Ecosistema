// #############################################################
// ##  Flex Storage de extremo a extremo, con un navegador de verdad
// #############################################################
//
// Tres procesos de verdad, hablando por sockets:
//
//   el TELEFONO  DevServer.kt: el servidor de Flex Cloud y el cliente de
//                emparejamiento de Flex Phone (modulo :storage, en la JVM),
//                arrancado SIN emparejar y con 24 MB de cuota.
//   el P4        tests/host/build/flexweb_host --storage: el Flex Web Server
//                y el nucleo de Flex Storage DE LA PLACA, compilados en el PC.
//                Su stdin es la "pantalla": approve / deny.
//   la web       Chromium sin cabeza (Playwright), tamano de movil.
//
// Recorrido: emparejar el navegador con el QR · activar Flex Cloud en el
// telefono (oferta de un solo uso, el MISMO codigo de 6 cifras en el telefono
// y en el P4, aprobado en la pantalla) · cuota real del telefono · subir
// originales por partes con SHA-256 · carpetas, mover, renombrar, miniatura ·
// ver y descargar directo del telefono con enlace firmado · papelera ·
// reanudar una subida cortada · copiar/mover Flex OS <-> Flex Cloud (la web lo
// pide; en este P4 de pruebas las copias son un doble que la prueba mueve) ·
// renombrar y eliminar en Flex OS · telefono desconectado.
//
// Lo que llega al telefono se juzga con la huella que calcula EL TELEFONO.
// Cualquier error de JavaScript o violacion de la CSP hace fallar la prueba.
//
// Necesita Playwright, Java y Gradle; si falta algo, dice que se OMITE (no que
// pasa) y sale con 0.
//
//   node tests/web/storage_e2e.test.js     (E2E_SHOTS=1 deja capturas)
'use strict';
const fs = require('fs');
const os = require('os');
const path = require('path');
const cp = require('child_process');
const crypto = require('crypto');
const readline = require('readline');

const ROOT = path.join(__dirname, '..', '..');
const BUILD = path.join(ROOT, 'tests', 'host', 'build');
const HOST = process.env.FLEXWEB_HOST || path.join(BUILD, 'flexweb_host');
const MC = process.env.MEDIACHECK || path.join(BUILD, 'mediacheck');
const PHONE_DIR = path.join(ROOT, 'android', 'FlexPhone');
const QUOTA = 24 * 1024 * 1024;

function loadPlaywright() {
  try { return require('playwright'); } catch (e) { /* global */ }
  try { return require(path.join(cp.execSync('npm root -g', { encoding: 'utf8' }).trim(), 'playwright')); } catch (e) { return null; }
}
function classpath() {
  try {
    const out = cp.execSync('gradle -q --console=plain :storage:printTestClasspath', { cwd: PHONE_DIR, encoding: 'utf8', stdio: ['ignore', 'pipe', 'ignore'] });
    const line = out.split('\n').find((l) => l.startsWith('CLASSPATH='));
    return line ? line.slice('CLASSPATH='.length) : null;
  } catch (e) { return null; }
}
const pw = loadPlaywright();
if (!pw) { console.log('=== storage e2e OMITIDA: no hay Playwright en esta maquina (no cuenta como pasada) ==='); process.exit(0); }
const CPATH = classpath();
if (!CPATH) { console.log('=== storage e2e OMITIDA: no hay Java/Gradle para el telefono de pruebas (no cuenta como pasada) ==='); process.exit(0); }

let run = 0, fail = 0;
function check(cond, msg) { run++; if (!cond) { fail++; console.log('  FALLO: ' + msg); } else if (process.env.E2E_VERBOSE) console.log('  ok: ' + msg); }
let step = 'inicio';
function section(t) { step = t; console.log('-- ' + t + ' --'); }
function mark(t) { step = t; if (process.env.E2E_TRACE) console.log('   · ' + t); }
let shotPage = null;
async function shot(name) { if (process.env.E2E_SHOTS && shotPage) await shotPage.screenshot({ path: path.join(BUILD, 'fs_' + name + '.png') }); }
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
function mc(...args) { return JSON.parse(cp.execFileSync(MC, args, { encoding: 'utf8' }).trim().split('\n').pop()); }

// Lineas JSON de un flujo, con espera por condicion.
function jsonLines(stream, echo) {
  const lines = [];
  const waiters = [];
  readline.createInterface({ input: stream }).on('line', (l) => {
    let j = null;
    try { j = JSON.parse(l); } catch (e) { if (echo) echo(l); return; }
    lines.push(j);
    for (const w of waiters.slice()) if (w.pred(j, lines.length - 1)) { waiters.splice(waiters.indexOf(w), 1); w.resolve(j); }
  });
  return {
    lines,
    wait(pred, ms = 30000, from = 0) {
      for (let i = from; i < lines.length; i++) if (pred(lines[i], i)) return Promise.resolve(lines[i]);
      return new Promise((resolve, reject) => {
        const w = { pred: (j, i) => i >= from && pred(j, i), resolve };
        waiters.push(w);
        setTimeout(() => { const i = waiters.indexOf(w); if (i >= 0) { waiters.splice(i, 1); reject(new Error('plazo vencido esperando en ' + step)); } }, ms);
      });
    },
  };
}
// Espera a que una condicion de la pagina se cumpla (sondeo).
async function until(page, fn, arg, ms = 20000) {
  try { await page.waitForFunction(fn, arg, { timeout: ms, polling: 150 }); return true; } catch (e) { return false; }
}

(async () => {
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'flexstorage-e2e-'));
  // ---------- el telefono ----------
  const phoneLog = [];
  const phone = cp.spawn('java', ['-cp', CPATH, 'com.flexos.flexphone.cloud.DevServerKt', path.join(tmp, 'telefono'), '-', '-', String(QUOTA)],
    { stdio: ['pipe', 'pipe', 'pipe'] });
  readline.createInterface({ input: phone.stderr }).on('line', (l) => { phoneLog.push(l); if (phoneLog.length > 200) phoneLog.shift(); });
  const phoneOut = jsonLines(phone.stdout);
  // ---------- el P4 ----------
  const host = cp.spawn(HOST, [path.join(tmp, 'p4'), String(10 * 1024 * 1024), '--storage'], { stdio: ['pipe', 'pipe', 'pipe'] });
  const hostOut = jsonLines(host.stdout);
  const hostErr = jsonLines(host.stderr, (l) => console.log('  [p4] ' + l));
  const cmd = async (c) => { const n = hostOut.lines.length; host.stdin.write(c + '\n'); return hostOut.wait(() => true, 10000, n); };
  const dump = async () => (await cmd('dump')).items || [];

  let browser = null, page = null;
  const watchdog = setTimeout(async () => {
    console.log('  FALLO: la prueba lleva 6 min parada en: ' + step);
    try { if (page) await page.screenshot({ path: path.join(BUILD, 'fs_colgada.png') }); } catch (e) { /* nada */ }
    try { phone.kill(); host.kill(); } catch (e) { /* nada */ }
    process.exit(1);
  }, 360000);
  const errors = [], csp = [];
  const watchPage = (p) => {
    p.on('pageerror', (e) => errors.push(String(e)));
    p.on('console', (m) => {
      if (m.type() !== 'error') return;
      const t = m.text();
      if (/Content Security Policy|Refused to/.test(t)) csp.push(t);
      else if (!/Failed to load resource/.test(t)) errors.push(t);
    });
  };

  try {
    const pport = (await phoneOut.wait((j) => j.port, 60000)).port;
    const hello = await hostOut.wait((j) => j.port && j.code);
    const base = 'http://127.0.0.1:' + hello.port;
    const phoneOrigin = 'http://127.0.0.1:' + pport;
    browser = await pw.chromium.launch();
    const ctx = await browser.newContext({ viewport: { width: 390, height: 844 }, deviceScaleFactor: 2, acceptDownloads: true, locale: 'es-ES' });
    page = await ctx.newPage();
    shotPage = page;
    watchPage(page);
    const cloudList = (q) => page.evaluate(async (qs) => (await (await fetch('/api/cloud/files?' + qs)).json()), q);
    const hasTile = (name, ms) => until(page, (n) => Array.from(document.querySelectorAll('#cgrid .ctile .nm')).some((x) => x.firstChild.textContent === n), name, ms);
    const tileByName = (name) => page.locator('#cgrid .ctile[data-id]').filter({ has: page.locator('.nm', { hasText: name }) }).first();

    // =========================================================
    section('emparejar el navegador con el codigo del QR');
    await page.goto(base + '/?k=' + hello.code);
    check(await until(page, () => !document.getElementById('homeView').hidden), 'con Flex Storage se abre el Inicio');
    check(!(await page.isHidden('#bnav')), 'barra de secciones: Inicio · Flex OS · Flex Cloud · Transferencias');
    check(await until(page, () => /libres de/.test(document.getElementById('hLocalLine').textContent)), 'Inicio: memoria de Flex OS (' + await page.textContent('#hLocalLine') + ')');
    check((await page.textContent('#hCloudSub')) === 'Sin teléfono' && (await page.textContent('#hCloudBtn')) === 'Activar Flex Cloud en este teléfono',
      'Inicio: sin telefono, se ofrece activarlo');
    await shot('1_inicio');

    // =========================================================
    section('activar Flex Cloud: el telefono se empareja y el P4 lo aprueba en pantalla');
    await page.click('#hCloudBtn');
    check(await until(page, () => /^flexstorage:\/\/attach\?/.test(document.getElementById('phoneOpen').getAttribute('href'))), 'oferta y enlace para abrir Flex Phone');
    const href = await page.getAttribute('#phoneOpen', 'href');
    const u = new URL(href.replace('flexstorage://', 'http://x/'));
    const hp = u.searchParams.get('h'), offer = u.searchParams.get('o');
    check(hp === '127.0.0.1:' + hello.port && /^[0-9a-f]{32}$/.test(offer), 'el enlace lleva la direccion del P4 y una oferta de 128 bits (' + hp + ')');
    check(/Invitación válida [0-3]:\d\d · solo sirve una vez/.test(await page.textContent('#phoneState')), 'cuenta atras de la invitacion: ' + await page.textContent('#phoneState'));
    await shot('2_invitacion');
    const nP = phoneOut.lines.length;
    phone.stdin.write('attach ' + hp + ' ' + offer + '\n');
    const sasPhone = await phoneOut.wait((j) => j.sas, 30000, nP);
    const sasP4 = await hostErr.wait((j) => j.pairWaiting === 1);
    check(/^\d{6}$/.test(sasPhone.sas) && sasPhone.sas === sasP4.sas, 'el MISMO codigo de 6 cifras en el telefono y en el P4 (' + sasPhone.sas + ')');
    check(await until(page, () => /Compara el código/.test(document.getElementById('phoneState').textContent)), 'la web pide comparar el codigo y aceptar en Flex OS');
    await shot('3_comparar');
    host.stdin.write('approve\n');
    const paired = await phoneOut.wait((j) => j.paired !== undefined, 30000, nP);
    check(paired.paired === true && paired.p4Id === 'flexos-a1b2c3d4e5f6', 'el telefono verifico la prueba del P4: emparejado');
    check(await until(page, () => /Flex Cloud activado en Galaxy A55 5G/.test(document.getElementById('phoneState').textContent)),
      'la web lo confirma: Flex Cloud activado en Galaxy A55 5G');
    // La CSP de la pagina se calculo sin telefono: sin nada en marcha, la web
    // se recarga UNA vez para poder ver fotos y videos directos del telefono.
    const reloaded = await page.waitForEvent('load', { timeout: 15000 }).then(() => true, () => false);
    check(reloaded, 'la pagina se recarga una vez tras emparejar (CSP con el telefono)');
    check(await until(page, () => !document.getElementById('homeView').hidden && /Flex Cloud activado en Galaxy A55 5G/.test(document.getElementById('toast').textContent)),
      'y al volver lo dice: Flex Cloud activado en Galaxy A55 5G');
    check(await until(page, () => /de 24 MB/.test(document.getElementById('hCloudLine').textContent), null, 30000),
      'cuota REAL del telefono a traves del P4: ' + await page.textContent('#hCloudLine'));
    check((await page.textContent('#hCloudLine')) === '0 B de 24 MB · 0 %' && (await page.textContent('#hCloudHint')) === 'Quedan 24 MB',
      'Flex Cloud vacio: 0 B de 24 MB · 0 %, quedan 24 MB');
    check(/Galaxy A55 5G · conectado/.test(await page.textContent('#hCloudSub')), 'telefono conectado: ' + await page.textContent('#hCloudSub'));
    await shot('4_activado');

    // =========================================================
    section('subir originales a Flex Cloud: partes de 1 MB con SHA-256');
    const datos = crypto.randomBytes(2621440 + 777);
    const fotoPath = path.join(tmp, 'foto.jpg');
    mc('jpeg', '800', '600', '7', fotoPath);
    const foto = fs.readFileSync(fotoPath);
    const parts = [];
    page.on('request', (r) => { if (r.method() === 'PUT' && /\/api\/cloud\/uploads\/[^/]+\/parts\/\d+$/.test(r.url())) parts.push(r.url()); });
    await page.evaluate(() => {
      window.__xfer = [];
      new MutationObserver(() => {
        for (const x of document.querySelectorAll('.xfer')) { const t = x.textContent; if (window.__xfer[window.__xfer.length - 1] !== t) window.__xfer.push(t); }
      }).observe(document.body, { subtree: true, childList: true, characterData: true });
    });
    await page.click('#fab');
    check(!(await page.isHidden('#destSheet')), '«Subir» pregunta donde guardar');
    check((await page.textContent('#destCloudWhy')) === 'Disponible: 24 MB de 24 MB' && /^Libre: /.test(await page.textContent('#destLocalWhy')),
      'cada destino con su sitio real: ' + await page.textContent('#destLocalWhy') + ' / ' + await page.textContent('#destCloudWhy'));
    await shot('5_destino');
    await page.click('#destCloud');
    await page.setInputFiles('#cloudPicker', [
      { name: 'datos.bin', mimeType: 'application/octet-stream', buffer: datos },
      { name: 'foto.jpg', mimeType: 'image/jpeg', buffer: foto },
    ]);
    let files = [];
    for (let i = 0; i < 120; i++) {
      await sleep(500);
      const l = await cloudList('parentId=root');
      files = (l.items || []).filter((x) => x.type === 'file');
      if (files.length >= 2) break;
    }
    const fDatos = files.find((x) => x.name === 'datos.bin'), fFoto = files.find((x) => x.name === 'foto.jpg');
    check(fDatos && fDatos.size === datos.length && fDatos.sha256 === sha(datos), 'datos.bin en el telefono, con la MISMA huella SHA-256 que el original');
    check(fFoto && fFoto.size === foto.length && fFoto.sha256 === sha(foto) && fFoto.kind === 'photo', 'foto.jpg en el telefono, original y sin tocar');
    check(parts.length === 4, 'por partes de 1 MB: 3 + 1 PUT (' + parts.length + ')');
    check(await until(page, () => window.__xfer.some((t) => /datos\.bin.*Guardado en Flex Cloud/.test(t)) &&
      window.__xfer.some((t) => /foto\.jpg.*Guardado en Flex Cloud/.test(t))), 'tarjetas: «Guardado en Flex Cloud»');
    const prog = await page.evaluate(() => window.__xfer.filter((t) => /datos\.bin.*Enviando/.test(t)));
    check(prog.some((t) => /Enviando \d+(,\d)? (KB|MB) de 2,5 MB/.test(t)), 'progreso real en bytes: ' + (prog[1] || prog[0]));
    check(await until(page, () => /de 24 MB/.test(document.getElementById('hCloudLine').textContent) &&
      !/^0 B/.test(document.getElementById('hCloudLine').textContent), null, 30000), 'la cuota sube con lo subido: ' + await page.textContent('#hCloudLine'));

    // =========================================================
    section('Flex Cloud: carpetas, mover, renombrar y miniatura');
    await page.click('#bnav button[data-v="cloud"]');
    check(await until(page, () => document.querySelectorAll('#cgrid .ctile[data-id]').length === 2), 'la carpeta raiz con los dos archivos');
    check((await page.textContent('#crumbs')).trim() === 'Flex Cloud', 'ruta: Flex Cloud');
    let thumbOk = await until(page, () => !!document.querySelector('#cgrid .ctile img.ok'), null, 4000);
    for (let i = 0; i < 6 && !thumbOk; i++) {
      await page.click('#menuBtn'); await page.click('#mRefresh');
      thumbOk = await until(page, () => !!document.querySelector('#cgrid .ctile img.ok'), null, 2500);
    }
    check(thumbOk, 'la miniatura de la foto (hecha en el navegador, objeto aparte en el telefono) se ve');
    await page.click('#cNewFolder');
    await page.fill('#askInput', 'Viajes');
    await page.click('#askGo');
    check(await until(page, () => Array.from(document.querySelectorAll('#cgrid .ctile.folder .nm')).some((n) => n.firstChild.textContent === 'Viajes')), 'carpeta «Viajes» creada');
    await page.click('#cNewFolder');
    await page.fill('#askInput', 'viajes');
    await page.click('#askGo');
    check(await until(page, () => /Ya existe/.test(document.getElementById('askErr').textContent)), 'nombre repetido: lo dice el telefono (' + await page.textContent('#askErr') + ')');
    await page.click('#askCancel');
    await tileByName('datos.bin').click({ button: 'right' });
    check(!(await page.isHidden('#cselActions')), 'pulsacion larga: seleccion con sus acciones');
    await page.click('#cMove');
    check(await until(page, () => !document.getElementById('folderSheet').hidden && /Viajes/.test(document.getElementById('folderList').textContent)), 'elegir carpeta destino');
    await page.locator('#folderList button', { hasText: 'Viajes' }).click();
    check(await until(page, () => document.getElementById('folderWhere').textContent === 'Flex Cloud › Viajes'), 'dentro de Viajes');
    await page.click('#folderGo');
    check(await until(page, () => !Array.from(document.querySelectorAll('#cgrid .ctile .nm')).some((n) => n.firstChild.textContent === 'datos.bin')), 'datos.bin ya no esta en la raiz');
    await tileByName('Viajes').click();
    check(await until(page, () => document.getElementById('crumbs').textContent.replace(/\s/g, '') === 'FlexCloudViajes'), 'ruta: Flex Cloud › Viajes');
    check(await until(page, () => Array.from(document.querySelectorAll('#cgrid .ctile .nm')).some((n) => n.firstChild.textContent === 'datos.bin')), 'datos.bin dentro de Viajes');
    await tileByName('datos.bin').click({ button: 'right' });
    await page.click('#cRen');
    check((await page.inputValue('#askInput')) === 'datos.bin', 'renombrar propone el nombre actual');
    await page.fill('#askInput', 'datos-2026.bin');
    await page.click('#askGo');
    check(await until(page, () => Array.from(document.querySelectorAll('#cgrid .ctile .nm')).some((n) => n.firstChild.textContent === 'datos-2026.bin')), 'renombrado a datos-2026.bin');
    await page.locator('#crumbs button', { hasText: 'Flex Cloud' }).click();
    check(await until(page, () => document.getElementById('crumbs').textContent.trim() === 'Flex Cloud' && document.querySelectorAll('#cgrid .ctile[data-id]').length === 2),
      'de vuelta en la raiz: Viajes y foto.jpg');
    await shot('6_nube');

    // =========================================================
    section('ver y descargar directamente desde el telefono (enlace firmado)');
    await tileByName('foto.jpg').click();
    check(await until(page, () => { const i = document.querySelector('#viewerBody img'); return !!(i && i.complete && i.naturalWidth === 800); }, null, 15000),
      'la foto se ve a tamano completo');
    const vsrc = await page.getAttribute('#viewerBody img', 'src');
    check(vsrc.startsWith(phoneOrigin + '/api/cloud/d/') && /\?inline=1$/.test(vsrc), 'los bytes vienen DEL TELEFONO con un enlace firmado (no pasan por el P4)');
    check((await page.getAttribute('#viewerDl', 'href')).startsWith(phoneOrigin + '/api/cloud/d/'), 'descargar desde el visor: el mismo enlace');
    await shot('7_visor');
    await page.click('#viewerClose');
    check(await until(page, () => document.getElementById('viewer').hidden), 'visor cerrado');
    await tileByName('foto.jpg').click({ button: 'right' });
    const [dl] = await Promise.all([page.waitForEvent('download', { timeout: 20000 }), page.click('#cDl')]);
    const dlPath = path.join(tmp, 'bajada.jpg');
    await dl.saveAs(dlPath);
    check(sha(fs.readFileSync(dlPath)) === sha(foto), 'la descarga es byte a byte el original');
    // Un enlace firmado inventado no sirve (lo firma el telefono).
    const forged = await page.evaluate(async (o) => { try { const r = await fetch(o + '/api/cloud/d/eyJmIjoiZmlsX3gifQ.AAAA', { mode: 'no-cors' }); return r.type; } catch (e) { return 'bloqueado'; } }, phoneOrigin);
    check(forged === 'bloqueado', 'la CSP no deja a la pagina pedir al telefono por su cuenta (connect-src solo el P4)');

    // =========================================================
    section('papelera: mandar, restaurar y borrar para siempre');
    await tileByName('foto.jpg').click({ button: 'right' });
    await page.click('#cDel');
    check(!(await page.isHidden('#sureDlg')) && /papelera/.test(await page.textContent('#sureText')), 'confirmacion: se puede recuperar desde la papelera');
    await page.click('#sureGo');
    check(await until(page, () => !Array.from(document.querySelectorAll('#cgrid .ctile .nm')).some((n) => n.firstChild.textContent === 'foto.jpg')), 'foto.jpg en la papelera');
    await page.click('#cTrashBtn');
    check(await until(page, () => document.getElementById('crumbs').textContent.trim() === 'Papelera' && document.querySelectorAll('#cgrid .ctile[data-id]').length === 1), 'la papelera la tiene');
    await tileByName('foto.jpg').click();
    check(!(await page.isHidden('#cRestore')) && (await page.isHidden('#cDl')), 'en la papelera: restaurar o eliminar');
    await page.click('#cRestore');
    check(await until(page, () => document.querySelectorAll('#cgrid .ctile[data-id]').length === 0), 'restaurada: la papelera queda vacia');
    await page.click('#cTrashBtn');
    check(await until(page, () => Array.from(document.querySelectorAll('#cgrid .ctile .nm')).some((n) => n.firstChild.textContent === 'foto.jpg')), 'foto.jpg de vuelta en su sitio');
    const usedBefore = (await page.evaluate(async () => (await (await fetch('/api/cloud/quota')).json()).quota)).usedBytes;
    await tileByName('foto.jpg').click({ button: 'right' });
    await page.click('#cDel'); await page.click('#sureGo');
    await page.click('#cTrashBtn');
    await until(page, () => document.querySelectorAll('#cgrid .ctile[data-id]').length === 1);
    await tileByName('foto.jpg').click();
    await page.click('#cPurge');
    check(/no se podrán recuperar/.test(await page.textContent('#sureText')), 'borrar para siempre avisa de que no se recupera');
    await page.click('#sureGo');
    check(await until(page, () => document.querySelectorAll('#cgrid .ctile[data-id]').length === 0), 'eliminada para siempre');
    const usedAfter = (await page.evaluate(async () => (await (await fetch('/api/cloud/quota')).json()).quota)).usedBytes;
    check(usedBefore - usedAfter === foto.length, 'y libera su espacio en el telefono (' + (usedBefore - usedAfter) + ' bytes)');
    await page.click('#cTrashBtn');

    // =========================================================
    section('reanudar: la pagina se cierra a mitad de una subida');
    const grande = crypto.randomBytes(6 * 1024 * 1024);
    const grandePath = path.join(tmp, 'grande.bin');
    fs.writeFileSync(grandePath, grande);
    let seen = 0;
    await page.route('**/api/cloud/uploads/*/parts/*', async (route) => {
      seen++;
      // Dos partes llegan; a partir de la tercera se corta la red (la web
      // espera para reintentar) y en ese momento se cierra la pagina.
      if (seen <= 2) await route.continue(); else await route.abort('internetdisconnected');
    });
    await page.click('#fab');
    await page.click('#destCloud');
    await page.setInputFiles('#cloudPicker', grandePath);
    for (let i = 0; i < 100 && seen < 3; i++) await sleep(100);
    check(seen >= 3, 'dos partes enviadas y la tercera cortada');
    check(await until(page, () => window.__xfer.some((t) => /grande\.bin.*reintento en \d+ s/.test(t)), null, 8000), 'la web espera para reintentar (no se rinde)');
    await page.close();
    // Otra pestana (la misma sesion del navegador), como al recargar.
    page = await ctx.newPage();
    shotPage = page;
    watchPage(page);
    const parts2 = [];
    page.on('request', (r) => { if (r.method() === 'PUT' && /\/parts\/\d+$/.test(r.url())) parts2.push(r.url()); });
    await page.goto(base + '/');
    check(await until(page, () => !document.getElementById('homeView').hidden), 'la sesion sigue: Inicio sin pedir codigo');
    await page.evaluate(() => {
      window.__xfer = [];
      new MutationObserver(() => {
        for (const x of document.querySelectorAll('.xfer')) { const t = x.textContent; if (window.__xfer[window.__xfer.length - 1] !== t) window.__xfer.push(t); }
      }).observe(document.body, { subtree: true, childList: true, characterData: true });
    });
    await until(page, () => /conectado/.test(document.getElementById('hCloudSub').textContent), null, 30000);
    await page.click('#fab');
    await page.click('#destCloud');
    await page.setInputFiles('#cloudPicker', grandePath);
    let fGrande = null;
    for (let i = 0; i < 120 && !fGrande; i++) {
      await sleep(500);
      fGrande = ((await cloudList('parentId=root')).items || []).find((x) => x.name === 'grande.bin');
    }
    check(fGrande && fGrande.sha256 === sha(grande), 'grande.bin completo en el telefono, con su huella');
    check(parts2.length === 4, 'solo se enviaron las 4 partes que faltaban (' + parts2.map((x) => x.replace(/.*\/parts\//, '')).join(',') + ')');
    check(await page.evaluate(() => window.__xfer.some((t) => /grande\.bin.*Continuando donde se quedó/.test(t))), 'la tarjeta dice que continua donde se quedo');

    // =========================================================
    section('Flex OS <-> Flex Cloud: la web lo pide, el P4 lo hace');
    // Algo en Flex OS: una foto por el camino de siempre (preparada para el P4).
    await page.click('#fab');
    await page.click('#destLocal');
    await page.setInputFiles('#picker', fotoPath);
    await page.waitForSelector('#planList li:not(.busy)', { timeout: 30000 });
    await page.click('#planGo');
    let items = [];
    for (let i = 0; i < 120 && !items.length; i++) { await sleep(500); items = await dump(); }
    check(items.length === 1, 'una foto en Flex OS');
    const localId = items[0] && items[0].id;
    await page.click('#bnav button[data-v="lib"]');
    await page.waitForSelector('#grid .card[data-id="' + localId + '"]');
    await page.click('#grid .card[data-id="' + localId + '"]', { button: 'right' });
    check(!(await page.isHidden('#selCloud')) && !(await page.isHidden('#selDel')) && !(await page.isHidden('#selRen')), 'seleccion en Flex OS: A Flex Cloud, Renombrar, Eliminar');
    await page.click('#selCloud');
    check(!(await page.isHidden('#sendSheet')) && (await page.textContent('#sendTitle')) === 'Enviar a Flex Cloud', 'hoja «Enviar a Flex Cloud»');
    await page.click('#sendMode button[data-m="move"]');
    check(/solo cuando el teléfono confirma la copia/.test(await page.textContent('#sendModeHint')), 'mover: el original solo se borra cuando el telefono confirma la copia');
    await page.click('#sendWhere');
    check(await until(page, () => /Viajes/.test(document.getElementById('folderList').textContent)), 'elegir carpeta de Flex Cloud');
    await page.locator('#folderList button', { hasText: 'Viajes' }).click();
    await until(page, () => document.getElementById('folderWhere').textContent === 'Flex Cloud › Viajes');
    await page.click('#folderGo');
    check(await until(page, () => !document.getElementById('sendSheet').hidden && /Viajes/.test(document.getElementById('sendWhereTxt').textContent)), 'carpeta elegida: Viajes');
    const viajes = ((await cloudList('parentId=root')).items || []).find((x) => x.name === 'Viajes');
    const nE = hostErr.lines.length;
    await page.click('#sendGo');
    const upReq = (await hostErr.wait((j) => j.xferReq && j.xferReq.op === 'up', 10000, nE)).xferReq;
    check(upReq.id === localId && upReq.folder === viajes.id && upReq.move === 1, 'el P4 recibe: mover el elemento ' + localId + ' a Viajes (' + JSON.stringify(upReq) + ')');
    await page.click('#bnav button[data-v="xfer"]');
    check(await until(page, () => /Mover a Flex Cloud · En cola/.test(document.getElementById('p4xList').textContent)), 'Transferencias: la del P4, en cola');
    await cmd('xjob ' + upReq.job + ' 2 ' + Math.floor(items[0].s / 2));
    check(await until(page, () => /En curso/.test(document.getElementById('p4xList').textContent) &&
      /^(49|50|51)(\.\d+)?%$/.test(document.querySelector('#p4xList .bar i').style.width)), 'el progreso que publica el P4 (la mitad)');
    check(await until(page, () => !document.getElementById('bnavBadge').hidden && document.getElementById('bnavBadge').textContent === '1'), 'insignia: 1 en curso');
    const nC = hostErr.lines.length;
    await page.locator('#p4xList button', { hasText: 'Cancelar' }).click();
    check((await hostErr.wait((j) => j.xferReq && j.xferReq.op === 'cancel', 10000, nC)).xferReq.job === upReq.job, 'cancelar llega al P4');
    check(await until(page, () => /Cancelado/.test(document.getElementById('p4xList').textContent)), 'y se ve cancelada');
    await cmd('xjob ' + upReq.job + ' 7 0');
    check(await until(page, () => /Reintentar/.test(document.getElementById('p4xList').textContent)), 'si falla: «Reintentar»');
    const nR = hostErr.lines.length;
    await page.locator('#p4xList button', { hasText: 'Reintentar' }).click();
    check((await hostErr.wait((j) => j.xferReq && j.xferReq.op === 'retry', 10000, nR)).xferReq.job === upReq.job, 'reintentar llega al P4');
    await cmd('xjob ' + upReq.job + ' 6 ' + items[0].s);
    check(await until(page, () => !document.getElementById('p4xClear').hidden), 'terminada: se puede quitar de la lista');
    await page.click('#p4xClear');
    check(await until(page, () => !document.getElementById('p4xEmpty').hidden), 'lista limpia');
    await shot('8_transferencias');
    // Y al reves: un archivo de Flex Cloud a Flex OS.
    await page.click('#bnav button[data-v="cloud"]');
    check(await hasTile('grande.bin'), 'grande.bin en la raiz de Flex Cloud');
    await tileByName('grande.bin').click({ button: 'right' });
    await page.click('#cLocal');
    check((await page.textContent('#sendTitle')) === 'Enviar a Flex OS' && /a Archivos › Descargas/.test(await page.textContent('#sendList')),
      'enviar a Flex OS dice donde ira: ' + await page.textContent('#sendList'));
    const nD = hostErr.lines.length;
    await page.click('#sendGo');
    const downReq = (await hostErr.wait((j) => j.xferReq && j.xferReq.op === 'down', 10000, nD)).xferReq;
    check(downReq.file === fGrande.id && downReq.name === 'grande.bin' && downReq.size === grande.length && downReq.sha === sha(grande) && downReq.move === 0,
      'el P4 recibe el archivo con su tamano y su huella para verificar la copia');

    // =========================================================
    section('Flex OS: renombrar y eliminar desde la web');
    await page.click('#bnav button[data-v="lib"]');
    await page.waitForSelector('#grid .card[data-id="' + localId + '"]');
    await page.click('#grid .card[data-id="' + localId + '"]', { button: 'right' });
    await page.click('#selRen');
    await page.fill('#askInput', 'Atardecer');
    await page.click('#askGo');
    let renamed = null;
    for (let i = 0; i < 40 && !renamed; i++) { await sleep(250); renamed = (await dump()).find((it) => it.id === localId && /^Atardecer/.test(it.n)); }
    check(!!renamed, 'renombrado en el catalogo de Flex OS (' + (renamed && renamed.n) + ')');
    await page.waitForSelector('#grid .card[data-id="' + localId + '"]');
    await page.click('#grid .card[data-id="' + localId + '"]', { button: 'right' });
    await page.click('#selDel');
    check(/papelera de Flex OS/.test(await page.textContent('#sureText')), 'eliminar explica a donde va');
    await page.click('#sureGo');
    let gone = false;
    for (let i = 0; i < 40 && !gone; i++) { await sleep(250); gone = !(await dump()).some((it) => it.id === localId); }
    check(gone, 'eliminado de Flex OS');

    // =========================================================
    section('telefono desconectado: se dice, y no se ofrece Flex Cloud');
    phone.stdin.end();
    phone.kill();
    await sleep(500);
    await page.click('#bnav button[data-v="cloud"]');
    await page.click('#menuBtn'); await page.click('#mRefresh');
    check(await until(page, () => !document.getElementById('cBanner').hidden && /desconectado|no contesta/.test(document.getElementById('cBannerTxt').textContent), null, 20000),
      'aviso en Flex Cloud: ' + await page.textContent('#cBannerTxt'));
    await page.click('#fab');
    check((await page.textContent('#destCloudWhy')) === 'Teléfono desconectado' && (await page.getAttribute('#destCloud', 'aria-disabled')) === 'true',
      '«Subir» no ofrece Flex Cloud: Teléfono desconectado');
    await page.click('#destCancel');
    await shot('9_desconectado');
  } catch (e) {
    check(false, 'excepcion en la prueba (' + step + '): ' + (e && e.stack || e));
    try { if (page) { await page.screenshot({ path: path.join(BUILD, 'fs_fallo.png') }); console.log('  captura: tests/host/build/fs_fallo.png'); } } catch (x) { /* nada */ }
    if (phoneLog.length) console.log('--- registro del telefono ---\n' + phoneLog.slice(-25).join('\n'));
  }
  check(errors.length === 0, 'sin errores de JavaScript en la pagina ' + errors.join(' | '));
  check(csp.filter((t) => !/api\/cloud\/d\/eyJmIjoiZmlsX3gifQ/.test(t)).length === 0, 'sin violaciones de la CSP (salvo la provocada) ' + csp.join(' | '));
  clearTimeout(watchdog);
  try { if (browser) await browser.close(); } catch (e) { /* nada */ }
  try { phone.stdin.end(); phone.kill(); } catch (e) { /* nada */ }
  host.stdin.end();
  await new Promise((r) => { host.on('exit', r); setTimeout(r, 3000); });
  fs.rmSync(tmp, { recursive: true, force: true });
  console.log('=== storage e2e: ' + run + ' comprobaciones, ' + fail + ' fallos ===');
  process.exit(fail ? 1 : 0);
})();
