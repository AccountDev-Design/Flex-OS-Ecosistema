// #############################################################
//  Pruebas del backend de Flex Intelligence
//  ------------------------------------------------------------
//  node --test test/*.test.js   (Node 20 o superior)
//
//  Se prueba lo que de verdad protege al dispositivo: que solo se
//  acepta lo que manda el firmware, y que NUNCA sale una accion
//  fuera de la lista blanca ni un argumento que parezca una ruta.
// #############################################################

'use strict';

const test = require('node:test');
const assert = require('node:assert');

const { parseRequest, shapeResponse, shapeError, ACTIONS, LIMITS } = require('../src/protocol');
const { localProvider, pickText } = require('../src/providers');

const good = (over = {}) =>
  JSON.stringify({ v: 1, device: 'flexos-p4-001', task: 'summarize', text: 'hola mundo', ...over });

test('acepta una peticion valida del firmware', () => {
  const r = parseRequest(good());
  assert.equal(r.ok, true);
  assert.equal(r.req.device, 'flexos-p4-001');
  assert.equal(r.req.task, 'summarize');
  assert.equal(r.req.text, 'hola mundo');
});

test('rechaza lo que no viene de este firmware', () => {
  // Version distinta: se dice que no se entiende en vez de adivinar.
  assert.equal(parseRequest(good({ v: 2 })).ok, false);
  assert.equal(parseRequest(good({ v: undefined })).ok, false);
  // Tarea inventada.
  assert.equal(parseRequest(good({ task: 'rm -rf' })).ok, false);
  assert.equal(parseRequest(good({ task: 123 })).ok, false);
  // Sin dispositivo.
  assert.equal(parseRequest(good({ device: '' })).ok, false);
  assert.equal(parseRequest(good({ device: 'x'.repeat(LIMITS.DEVICE_MAX + 1) })).ok, false);
  // Basura.
  assert.equal(parseRequest('no soy json').ok, false);
  assert.equal(parseRequest('[]').ok, false);
  assert.equal(parseRequest('null').ok, false);
  assert.equal(parseRequest('').ok, false);
  assert.equal(parseRequest(undefined).ok, false);
});

test('rechaza cuerpos y textos demasiado grandes', () => {
  const big = parseRequest('x'.repeat(LIMITS.BODY_MAX + 1));
  assert.equal(big.ok, false);
  assert.equal(big.status, 413);
  const bigText = parseRequest(good({ text: 'a'.repeat(LIMITS.INPUT_MAX + 1) }));
  assert.equal(bigText.ok, false);
  assert.equal(bigText.status, 413);
});

test('un JSON truncado en cualquier punto no revienta', () => {
  const full = good();
  for (let k = 0; k <= full.length; k++) parseRequest(full.slice(0, k));
  assert.ok(true);
});

test('LA REGLA: una accion fuera de la lista blanca se degrada a show_text', () => {
  for (const evil of ['delete_file', 'run', 'exec', 'http_get', 'open_appx', '', null, 42, {}]) {
    const r = shapeResponse({ text: 'ok', action: evil });
    assert.equal(r.action, 'show_text', `${JSON.stringify(evil)} no puede pasar`);
  }
  // Y las cinco buenas si pasan.
  for (const a of ACTIONS) {
    assert.equal(shapeResponse({ text: 'x', action: a }).action, a);
  }
});

test('el argumento nunca puede parecer una ruta', () => {
  for (const bad of ['../../System/config', '/Notas/x.txt', 'a/b', 'a\\b', '..']) {
    const r = shapeResponse({ text: 'x', action: 'create_note', arg: bad });
    assert.equal(r.arg, undefined, `${bad} no puede salir`);
  }
  // Un nombre normal si sobrevive.
  assert.equal(shapeResponse({ text: 'x', action: 'create_note', arg: 'Resumen' }).arg, 'Resumen');
});

test('show_text no lleva argumento', () => {
  const r = shapeResponse({ text: 'x', action: 'show_text', arg: 'Resumen' });
  assert.equal(r.arg, undefined);
});

test('el texto y el argumento se cortan a lo que el firmware puede guardar', () => {
  const r = shapeResponse({
    text: 'a'.repeat(LIMITS.TEXT_MAX * 3),
    action: 'create_note',
    arg: 'b'.repeat(LIMITS.ARG_MAX * 3),
  });
  assert.equal(r.text.length, LIMITS.TEXT_MAX);
  assert.equal(r.arg.length, LIMITS.ARG_MAX);
});

test('shapeResponse sobrevive a entradas raras', () => {
  assert.equal(shapeResponse().action, 'show_text');
  assert.equal(shapeResponse({}).text, '');
  assert.equal(typeof shapeResponse({ text: 42 }).text, 'string');
});

test('el error tiene la forma que lee aiParseResponse', () => {
  const e = shapeError('token invalido');
  assert.equal(typeof e.error, 'string');
  assert.ok(e.error.includes('token'));      // el firmware lo usa para decir "revisa tu token"
  assert.ok(shapeError('x'.repeat(500)).error.length <= 120);
});

test('el proveedor local no finge lo que no puede hacer', () => {
  // Traducir y cambiar el tono necesitan un modelo: se DICE, y no se
  // devuelve el original disimulado.
  for (const task of ['translate', 'tone']) {
    const r = localProvider({ task, text: 'hola mundo', extra: '' });
    assert.equal(r.action, 'show_text');
    assert.notEqual(r.text, 'hola mundo');
    assert.ok(/no puede/i.test(r.text));
  }
  // Las imagenes tampoco.
  assert.ok(/no analiza im/i.test(localProvider({ task: 'image', text: '' }).text));
  // La busqueda es del dispositivo y se dice.
  assert.ok(/dispositivo/i.test(localProvider({ task: 'find', text: 'x' }).text));
});

test('el proveedor local hace de verdad lo que si puede', () => {
  const s = localProvider({ task: 'summarize', text: 'Uno. Dos. Tres. Cuatro.' });
  assert.ok(s.text.startsWith('Uno. Dos.'));
  assert.equal(s.action, 'create_note');

  // Corregir solo propone sustituir si CAMBIO algo.
  const c1 = localProvider({ task: 'correct', text: 'hola   mundo ,  que tal' });
  assert.equal(c1.action, 'replace_text');
  assert.equal(c1.text, 'hola mundo, que tal');
  const c2 = localProvider({ task: 'correct', text: 'hola mundo' });
  assert.equal(c2.action, 'show_text');

  const a = localProvider({ task: 'analyze', text: 'Una frase. Y otra.' });
  assert.ok(/palabras/.test(a.text));
});

test('la respuesta del proveedor local pasa la lista blanca sin cambios', () => {
  for (const task of ['summarize', 'correct', 'analyze', 'explain', 'translate', 'tone', 'find', 'image']) {
    const raw = localProvider({ task, text: 'Una frase. Y otra mas.', extra: '' });
    const shaped = shapeResponse(raw);
    assert.ok(ACTIONS.includes(shaped.action));
    assert.ok(shaped.text.length <= LIMITS.TEXT_MAX);
  }
});

test('pickText saca el texto de las formas mas comunes', () => {
  assert.equal(pickText('directo'), 'directo');
  assert.equal(pickText({ text: 'a' }), 'a');
  assert.equal(pickText({ output_text: 'b' }), 'b');
  assert.equal(pickText({ content: [{ text: 'c' }] }), 'c');
  assert.equal(pickText({ choices: [{ message: { content: 'd' } }] }), 'd');
  assert.equal(typeof pickText({ raro: 1 }), 'string');
});
