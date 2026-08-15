// #############################################################
//  PROTOCOLO DE FLEX INTELLIGENCE  ·  validacion y forma
//  ------------------------------------------------------------
//  Este fichero NO habla con la red ni con ningun proveedor: solo
//  valida lo que llega del firmware y da forma a lo que se le
//  devuelve. Por eso se puede probar entero sin levantar nada
//  (test/protocol.test.js).
//
//  LA REGLA CENTRAL: el firmware solo obedece cinco acciones, y
//  cualquier otra cosa la degrada a "enseña el texto". Este
//  backend aplica LA MISMA lista aqui, antes de responder. No es
//  redundancia inutil: si un proveedor de IA devuelve
//  "delete_file" porque se lo pidieron con maña, aqui se corta y
//  no llega a salir por el socket.
// #############################################################

'use strict';

// Tiene que coincidir EXACTAMENTE con AI_ACTS[] de FlexOS_AI.cpp.
// Si aqui se anade una y alli no, el firmware la ignorara -- que es
// el fallo seguro -- pero la lista dejaria de significar lo mismo en
// los dos lados, y eso siempre acaba mal.
const ACTIONS = Object.freeze([
  'show_text',
  'create_note',
  'replace_text',
  'append_note',
  'open_app',
]);

// Verbos de tarea que manda el firmware (aiKindSlug en FlexOS_AI.cpp).
const TASKS = Object.freeze([
  'correct', 'summarize', 'translate', 'tone', 'explain', 'analyze', 'image', 'find',
]);

// Limites. El firmware tiene buffers FIJOS: AI_RES_TEXT_MAX = 1024 y
// AI_RES_ARG_MAX = 64. Pasarse no rompe nada -- el lector trunca dentro
// del buffer -- pero devolver 8 KB para que el dispositivo tire 7 es
// gastar radio y bateria del usuario. Se corta aqui.
const LIMITS = Object.freeze({
  TEXT_MAX: 1024,
  ARG_MAX: 64,
  BODY_MAX: 4096,     // cuerpo de peticion aceptado (AI_REQ_MAX = 2048, con holgura)
  DEVICE_MAX: 32,
  INPUT_MAX: 2048,
});

/**
 * Valida el cuerpo de una peticion del firmware.
 * Devuelve { ok:true, req:{...} } o { ok:false, status, error }.
 *
 * Se rechaza TODO lo que no encaje, en vez de rellenar huecos con
 * valores por defecto: una peticion que no viene de este firmware no
 * tiene por que entenderse.
 */
function parseRequest(raw) {
  if (typeof raw !== 'string' || raw.length === 0) {
    return { ok: false, status: 400, error: 'cuerpo vacio' };
  }
  if (raw.length > LIMITS.BODY_MAX) {
    return { ok: false, status: 413, error: 'cuerpo demasiado grande' };
  }
  let body;
  try {
    body = JSON.parse(raw);
  } catch {
    return { ok: false, status: 400, error: 'json invalido' };
  }
  if (body === null || typeof body !== 'object' || Array.isArray(body)) {
    return { ok: false, status: 400, error: 'se esperaba un objeto' };
  }
  // Version del protocolo. El firmware manda v:1; si algun dia manda 2,
  // este backend tiene que decir que no lo entiende en vez de adivinar.
  if (body.v !== 1) {
    return { ok: false, status: 400, error: 'version de protocolo no soportada' };
  }
  const device = typeof body.device === 'string' ? body.device : '';
  if (!device || device.length > LIMITS.DEVICE_MAX) {
    return { ok: false, status: 400, error: 'device invalido' };
  }
  const task = typeof body.task === 'string' ? body.task : '';
  if (!TASKS.includes(task)) {
    return { ok: false, status: 400, error: 'task desconocida' };
  }
  const text = typeof body.text === 'string' ? body.text : '';
  if (text.length > LIMITS.INPUT_MAX) {
    return { ok: false, status: 413, error: 'texto demasiado grande' };
  }
  const extra = typeof body.extra === 'string' ? body.extra.slice(0, 64) : '';
  return { ok: true, req: { device, task, text, extra } };
}

/**
 * Da forma a la respuesta que se le manda al firmware.
 *
 * Todo lo que no encaje se degrada a show_text: es el mismo criterio
 * que aplica el dispositivo, aplicado un paso antes. Si un proveedor
 * devolviera una accion inventada, aqui deja de existir.
 */
function shapeResponse({ text = '', action = 'show_text', arg = '' } = {}) {
  let out = typeof text === 'string' ? text : String(text ?? '');
  if (out.length > LIMITS.TEXT_MAX) out = out.slice(0, LIMITS.TEXT_MAX);

  let act = typeof action === 'string' ? action : '';
  if (!ACTIONS.includes(act)) act = 'show_text';

  let a = typeof arg === 'string' ? arg : '';
  if (a.length > LIMITS.ARG_MAX) a = a.slice(0, LIMITS.ARG_MAX);

  // El argumento NO puede ser una ruta. El firmware ya lo reduce a un
  // nombre y fuerza /Notas -- no se fia de nadie, y hace bien -- pero un
  // backend que manda "../../System/config" esta intentando algo, y no
  // hay razon para reenviarlo.
  if (a.includes('/') || a.includes('\\') || a.includes('..')) a = '';

  // show_text no lleva argumento: si lo llevara, seria ruido que el
  // dispositivo tendria que ignorar.
  if (act === 'show_text') a = '';

  const res = { text: out, action: act };
  if (a) res.arg = a;
  return res;
}

/** Respuesta de error, en el formato que lee aiParseResponse(). */
function shapeError(message) {
  return { error: String(message || 'error').slice(0, 120) };
}

module.exports = { ACTIONS, TASKS, LIMITS, parseRequest, shapeResponse, shapeError };
