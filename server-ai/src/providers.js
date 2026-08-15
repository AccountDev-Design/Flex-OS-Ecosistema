// #############################################################
//  PROVEEDORES  ·  quien produce la respuesta
//  ------------------------------------------------------------
//  De fabrica va el proveedor "local": no llama a ningun servicio,
//  no necesita ninguna clave y hace transformaciones deterministas
//  y honestas sobre el texto. Sirve para probar el camino COMPLETO
//  -- firmware, TLS, token, protocolo, acciones -- sin contratar
//  nada ni mandar el texto del usuario a un tercero.
//
//  NO PRETENDE SER UN MODELO DE LENGUAJE, y por eso no finge
//  serlo: cuando le piden algo que no puede hacer de verdad
//  (traducir, cambiar el tono), lo DICE en el propio texto de
//  respuesta en vez de devolver el original disimulado. Un backend
//  de pruebas que miente hace que se den por buenas cosas que no
//  funcionan.
//
//  Para conectar un proveedor real: FLEXAI_PROVIDER=http y las
//  variables de FLEXAI_UPSTREAM_*. La clave se lee del entorno y
//  NUNCA se escribe en disco ni se registra.
// #############################################################

'use strict';

// --- utilidades de texto, sin dependencias ---
function sentences(t) {
  return String(t)
    .replace(/\s+/g, ' ')
    .trim()
    .split(/(?<=[.!?])\s+/)
    .filter(Boolean);
}
function words(t) {
  return String(t).trim().split(/\s+/).filter(Boolean);
}

/**
 * Proveedor LOCAL. Determinista, sin red y sin claves.
 * Devuelve { text, action, arg } sin dar forma: de eso se encarga
 * protocol.shapeResponse().
 */
function localProvider({ task, text, extra }) {
  const t = String(text || '');
  switch (task) {
    case 'summarize': {
      // Resumen extractivo: las dos primeras frases. Es un resumen de
      // verdad -- pobre, pero real -- y se dice de donde sale.
      const s = sentences(t);
      const head = s.slice(0, 2).join(' ');
      const body = head || t.slice(0, 200);
      return {
        text: `${body}\n\n(resumen extractivo local: primeras frases, ${words(t).length} palabras de origen)`,
        action: 'create_note',
        arg: 'Resumen',
      };
    }
    case 'correct': {
      // El corrector DE VERDAD vive en el firmware (FlexOS_Spell) y
      // funciona sin servidor. Aqui solo se normalizan los espacios:
      // fingir una correccion gramatical seria tapar que el
      // dispositivo ya hace la buena.
      const fixed = t.replace(/[ \t]+/g, ' ').replace(/ +([,.;:!?])/g, '$1').trim();
      return {
        text: fixed,
        action: fixed !== t ? 'replace_text' : 'show_text',
      };
    }
    case 'analyze': {
      const w = words(t);
      const s = sentences(t);
      const avg = w.length && s.length ? (w.length / s.length).toFixed(1) : '0';
      return {
        text:
          `Analisis local (sin modelo de lenguaje):\n` +
          `- ${w.length} palabras en ${s.length} frase(s)\n` +
          `- media de ${avg} palabras por frase\n` +
          `- ${t.length} caracteres\n\n` +
          `Para respuestas con lenguaje natural, configura FLEXAI_PROVIDER=http.`,
        action: 'show_text',
      };
    }
    case 'explain': {
      return {
        text:
          `El proveedor local no interpreta errores de compilacion.\n` +
          `Texto recibido (${t.length} caracteres):\n\n${t.slice(0, 400)}`,
        action: 'show_text',
      };
    }
    case 'translate':
    case 'tone': {
      // No se puede hacer sin un modelo. Se dice, y no se devuelve el
      // original como si estuviera traducido.
      return {
        text:
          `El proveedor local no puede ${task === 'translate' ? 'traducir' : 'cambiar el tono'}: ` +
          `hace falta un modelo de lenguaje.\n` +
          `Configura FLEXAI_PROVIDER=http para conectar uno.` +
          (extra ? `\n(peticion: ${extra})` : ''),
        action: 'show_text',
      };
    }
    case 'find':
      // La busqueda de archivos es LOCAL en el dispositivo y no deberia
      // llegar hasta aqui. Si llega, se dice claramente.
      return {
        text: 'La busqueda de archivos se resuelve en el dispositivo y no se envia al servidor.',
        action: 'show_text',
      };
    case 'image':
      return {
        text: 'Este backend no analiza imagenes.',
        action: 'show_text',
      };
    default:
      return { text: 'Tarea no soportada por el proveedor local.', action: 'show_text' };
  }
}

/**
 * Proveedor HTTP generico, para conectar un servicio real.
 *
 * DELIBERADAMENTE MINIMO Y SIN CLAVES EN EL CODIGO: la URL, la
 * cabecera de autorizacion y el modelo salen del entorno. Cada
 * proveedor tiene su formato, asi que aqui solo se hace la llamada y se
 * saca el texto de las dos formas mas comunes; adaptarlo a uno concreto
 * es cambiar `pickText`.
 */
async function httpProvider({ task, text, extra }, env) {
  const url = env.FLEXAI_UPSTREAM_URL;
  const key = env.FLEXAI_UPSTREAM_KEY;
  const model = env.FLEXAI_UPSTREAM_MODEL || '';
  if (!url) throw new Error('FLEXAI_UPSTREAM_URL no configurada');

  const payload = {
    model,
    task,
    extra,
    input: text,
  };
  const headers = { 'content-type': 'application/json' };
  if (key) headers.authorization = `Bearer ${key}`;

  const ctrl = new AbortController();
  const timer = setTimeout(() => ctrl.abort(), Number(env.FLEXAI_UPSTREAM_TIMEOUT_MS || 20000));
  try {
    const r = await fetch(url, {
      method: 'POST',
      headers,
      body: JSON.stringify(payload),
      signal: ctrl.signal,
    });
    if (!r.ok) throw new Error(`upstream ${r.status}`);
    const j = await r.json();
    return { text: pickText(j), action: 'show_text' };
  } finally {
    clearTimeout(timer);
  }
}

// Saca el texto de las formas mas habituales sin atarse a un proveedor.
function pickText(j) {
  if (typeof j === 'string') return j;
  if (typeof j?.text === 'string') return j.text;
  if (typeof j?.output_text === 'string') return j.output_text;
  if (Array.isArray(j?.content) && typeof j.content[0]?.text === 'string') return j.content[0].text;
  if (typeof j?.choices?.[0]?.message?.content === 'string') return j.choices[0].message.content;
  return JSON.stringify(j).slice(0, 512);
}

async function run(req, env) {
  const provider = (env.FLEXAI_PROVIDER || 'local').toLowerCase();
  if (provider === 'http') return httpProvider(req, env);
  return localProvider(req);
}

module.exports = { run, localProvider, pickText };
