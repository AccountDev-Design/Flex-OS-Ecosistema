'use strict';
// #############################################################
//  Pruebas del guardian anti-SSRF.
//  Ejecutar:  npm test   (o  node --test test/)
//
//  Es la pieza del servicio con mas consecuencias si falla, asi que
//  se prueba caso por caso y no "en general".
// #############################################################

const test = require('node:test');
const assert = require('node:assert');
const guard = require('../src/guard');

test('clasificacion de direcciones IPv4', () => {
  const interna = [
    '127.0.0.1', '127.1.2.3', '10.0.0.1', '10.255.255.255',
    '172.16.0.1', '172.31.255.255', '192.168.0.1', '192.168.255.255',
    '169.254.169.254',            // metadatos de AWS/GCP/Azure
    '100.64.0.1',                 // CGNAT
    '0.0.0.0', '224.0.0.1', '240.0.0.1',
    '192.0.0.1', '198.18.0.1', '203.0.113.1'
  ];
  for (const ip of interna) {
    assert.ok(guard.classifyV4(ip), `${ip} deberia estar clasificada como interna`);
  }
  const publicas = ['8.8.8.8', '1.1.1.1', '93.184.216.34', '172.32.0.1', '11.0.0.1', '172.15.255.255'];
  for (const ip of publicas) {
    assert.strictEqual(guard.classifyV4(ip), null, `${ip} deberia ser publica`);
  }
});

test('los bordes de cada rango se clasifican bien', () => {
  // Justo dentro y justo fuera de 172.16.0.0/12.
  assert.ok(guard.classifyV4('172.16.0.0'));
  assert.ok(guard.classifyV4('172.31.255.255'));
  assert.strictEqual(guard.classifyV4('172.15.255.255'), null);
  assert.strictEqual(guard.classifyV4('172.32.0.0'), null);
  // 100.64.0.0/10
  assert.ok(guard.classifyV4('100.64.0.0'));
  assert.ok(guard.classifyV4('100.127.255.255'));
  assert.strictEqual(guard.classifyV4('100.128.0.0'), null);
  assert.strictEqual(guard.classifyV4('100.63.255.255'), null);
});

test('clasificacion de direcciones IPv6', () => {
  for (const ip of ['::1', '::', 'fc00::1', 'fd12:3456::1', 'fe80::1', 'ff02::1', '2001:db8::1']) {
    assert.ok(guard.classifyV6(ip), `${ip} deberia estar clasificada como interna`);
  }
  // IPv4 embebida: es la via de escape clasica y tiene que cerrarse.
  assert.ok(guard.classifyV6('::ffff:127.0.0.1'));
  assert.ok(guard.classifyV6('::ffff:10.0.0.1'));
  assert.ok(guard.classifyV6('::ffff:169.254.169.254'));
  assert.strictEqual(guard.classifyV6('2606:4700::1111'), null);
  assert.strictEqual(guard.classifyV6('::ffff:8.8.8.8'), null);
});

test('deteccion de direcciones escritas como literal', () => {
  for (const h of ['127.0.0.1', '10.0.0.1', '2130706433', '0x7f000001', '010.0.0.1',
                   '1.2.3', '[::1]', '::1', 'fe80::1']) {
    assert.ok(guard.isIpLiteral(h), `${h} deberia detectarse como IP literal`);
  }
  for (const h of ['example.com', 'a.b.c.example', 'xn--maana-pta.es', 'v2.api.example.org']) {
    assert.ok(!guard.isIpLiteral(h), `${h} no es una IP literal`);
  }
});

test('nombres de red local', () => {
  for (const h of ['localhost', 'nas.local', 'router.lan', 'srv.internal',
                   'x.home.arpa', 'algo.corp', 'y.test', 'z.invalid']) {
    assert.ok(guard.isLocalName(h), `${h} deberia ser un nombre local`);
  }
  assert.ok(!guard.isLocalName('example.com'));
  assert.ok(!guard.isLocalName('localhost.example.com'));
});

test('forma de la URL: esquemas permitidos y prohibidos', () => {
  assert.ok(guard.checkUrlShape('https://example.com/').ok);
  assert.ok(guard.checkUrlShape('http://example.com/x?y=1#z').ok);
  for (const u of ['file:///etc/passwd', 'ftp://example.com/', 'data:text/html,x',
                   'javascript:alert(1)', 'chrome://settings', 'about:blank',
                   'view-source:https://example.com', 'ws://example.com/']) {
    const r = guard.checkUrlShape(u);
    assert.ok(!r.ok, `${u} deberia rechazarse`);
    assert.match(r.reason, /esquema/);
  }
});

test('forma de la URL: credenciales, IP y nombres internos', () => {
  let r = guard.checkUrlShape('https://usuario:clave@example.com/');
  assert.ok(!r.ok);
  assert.match(r.reason, /credenciales/);

  for (const u of ['http://127.0.0.1/', 'http://10.0.0.1/', 'https://169.254.169.254/',
                   'http://[::1]/', 'http://2130706433/', 'http://0x7f000001/',
                   'https://93.184.216.34/']) {
    assert.ok(!guard.checkUrlShape(u).ok, `${u} deberia rechazarse`);
  }
  for (const u of ['http://localhost/', 'http://nas.local/', 'http://intranet/', 'http://router.lan/']) {
    assert.ok(!guard.checkUrlShape(u).ok, `${u} deberia rechazarse`);
  }
});

test('lista blanca de desarrollo y allowPrivate', () => {
  const wl = { allowHosts: ['nav-dev.local'] };
  assert.ok(guard.checkUrlShape('http://nav-dev.local:8080/x', wl).ok);
  assert.ok(!guard.checkUrlShape('http://otro.local:8080/x', wl).ok);
  // allowPrivate abre TODO lo interno: por eso el README insiste en que
  // solo vale en un laboratorio aislado.
  assert.ok(guard.checkUrlShape('http://192.168.1.1/', { allowPrivate: true }).ok);
  // ...pero NUNCA cambia los esquemas ni las credenciales.
  assert.ok(!guard.checkUrlShape('file:///etc/passwd', { allowPrivate: true }).ok);
  assert.ok(!guard.checkUrlShape('http://a:b@192.168.1.1/', { allowPrivate: true }).ok);
});

test('limites de longitud y de puerto', () => {
  const largo = 'https://example.com/' + 'a'.repeat(4000);
  assert.ok(!guard.checkUrlShape(largo).ok);
  assert.ok(guard.checkUrlShape('https://example.com/', { maxLength: 4096 }).ok);

  const soloTls = { allowedPorts: [443] };
  assert.ok(guard.checkUrlShape('https://example.com:443/', soloTls).ok);
  assert.ok(!guard.checkUrlShape('https://example.com:8080/', soloTls).ok);
});

test('entradas basura no revientan el validador', () => {
  for (const u of ['', 'no-es-una-url', '://', 'http://', 'https://:443/', null, undefined, 42, {}]) {
    const r = guard.checkUrlShape(u);
    assert.strictEqual(r.ok, false, `${String(u)} deberia rechazarse`);
    assert.ok(typeof r.reason === 'string' && r.reason.length > 0);
  }
});

test('checkUrl resuelve DNS y rechaza lo que apunte a una red interna', async () => {
  // localhost resuelve a 127.0.0.1 / ::1: la comprobacion de DNS tiene
  // que atraparlo aunque el nombre no estuviera en la lista de sufijos.
  const r = await guard.checkUrl('http://localhost/', {});
  assert.ok(!r.ok);

  // Un nombre que no existe debe dar error de DNS, no pasar.
  const r2 = await guard.checkUrl('https://este-nombre-no-existe-flexos-test.invalid/', {});
  assert.ok(!r2.ok);

  // Con la lista blanca no se resuelve nada: se acepta tal cual.
  const r3 = await guard.checkUrl('http://mi-servidor.local:8080/', { allowHosts: ['mi-servidor.local'] });
  assert.ok(r3.ok);
});
