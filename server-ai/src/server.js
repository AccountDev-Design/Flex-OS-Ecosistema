// #############################################################
//  BACKEND DE FLEX INTELLIGENCE  ·  servidor HTTPS
//  ------------------------------------------------------------
//  Node 20 o superior. CERO dependencias: todo sale de los
//  modulos que trae Node (node:https, node:fs, node:crypto).
//
//  NO HAY NINGUN SECRETO EN ESTE FICHERO. El token, el certificado
//  y la clave del proveedor se leen del entorno; sin token
//  configurado el servidor no arranca, en vez de quedarse abierto
//  a cualquiera.
//
//  Ver README.md para levantarlo en Ubuntu y configurarlo desde
//  Flex Intelligence.
// #############################################################

'use strict';

const https = require('node:https');
const http = require('node:http');
const fs = require('node:fs');
const crypto = require('node:crypto');

const { parseRequest, shapeResponse, shapeError, LIMITS } = require('./protocol');
const providers = require('./providers');

const env = process.env;

// -------------------------------------------------------------
//  CONFIGURACION  (toda por entorno, nada en disco)
// -------------------------------------------------------------
const PORT = Number(env.FLEXAI_PORT || 8443);
const HOST = env.FLEXAI_HOST || '0.0.0.0';
const TOKEN = env.FLEXAI_TOKEN || '';
const CERT = env.FLEXAI_TLS_CERT || '';
const KEY = env.FLEXAI_TLS_KEY || '';
// Solo para pruebas en una red de confianza. Se avisa en voz alta al
// arrancar: el firmware EXIGE https y no se conectara por aqui.
const ALLOW_PLAIN = env.FLEXAI_ALLOW_PLAINTEXT === '1';

if (!TOKEN || TOKEN.length < 16) {
  console.error('[flexai] FLEXAI_TOKEN no configurado (o de menos de 16 caracteres).');
  console.error('[flexai] Genera uno con:  openssl rand -hex 24');
  process.exit(1);
}

// COMPARACION EN TIEMPO CONSTANTE. Un `===` sobre un token filtra por
// el tiempo de respuesta cuantos caracteres iniciales acerto quien
// prueba, y eso convierte una fuerza bruta imposible en una posible.
const TOKEN_BUF = Buffer.from(TOKEN, 'utf8');
function tokenOk(given) {
  if (typeof given !== 'string') return false;
  const b = Buffer.from(given, 'utf8');
  if (b.length !== TOKEN_BUF.length) return false;     // la longitud si se filtra; no importa
  return crypto.timingSafeEqual(b, TOKEN_BUF);
}

// -------------------------------------------------------------
//  LIMITE DE PETICIONES POR DISPOSITIVO
//  -------------------------------------------------------------
//  Un dispositivo no hace mas de unas pocas consultas por minuto:
//  las lanza una persona tocando un boton. Un tope generoso corta un
//  bucle accidental del firmware sin estorbar al uso normal.
// -------------------------------------------------------------
const RATE_WINDOW_MS = 60000;
const RATE_MAX = Number(env.FLEXAI_RATE_MAX || 30);
const rate = new Map();
function rateOk(device) {
  const now = Date.now();
  const e = rate.get(device);
  if (!e || now - e.t0 > RATE_WINDOW_MS) {
    rate.set(device, { t0: now, n: 1 });
    return true;
  }
  e.n += 1;
  return e.n <= RATE_MAX;
}
// La tabla no crece sin fin: se limpia lo caducado cada tanto.
setInterval(() => {
  const now = Date.now();
  for (const [k, v] of rate) if (now - v.t0 > RATE_WINDOW_MS * 2) rate.delete(k);
}, RATE_WINDOW_MS).unref();

// -------------------------------------------------------------
//  RESPUESTAS
// -------------------------------------------------------------
function send(res, status, obj) {
  const body = JSON.stringify(obj);
  res.writeHead(status, {
    'content-type': 'application/json; charset=utf-8',
    'content-length': Buffer.byteLength(body),
    'cache-control': 'no-store',
    // El firmware no es un navegador, pero estas dos no cuestan nada y
    // evitan sorpresas si algun dia se prueba desde uno.
    'x-content-type-options': 'nosniff',
    'referrer-policy': 'no-referrer',
  });
  res.end(body);
}

async function handle(req, res) {
  // SOLO se acepta lo que manda el firmware: POST a /v1/flex.
  if (req.method !== 'POST') return send(res, 405, shapeError('metodo no permitido'));
  const path = (req.url || '').split('?')[0];
  if (path !== '/v1/flex') return send(res, 404, shapeError('ruta desconocida'));

  const auth = req.headers['authorization'] || '';
  const given = auth.startsWith('Bearer ') ? auth.slice(7) : '';
  if (!tokenOk(given)) {
    // El texto lleva "token" a proposito: aiParseResponse lo reconoce y
    // el dispositivo puede decir "revisa tu token" en vez de un
    // generico "error del servidor".
    return send(res, 401, shapeError('token invalido'));
  }

  // Cuerpo acotado ANTES de acumular: sin este corte, mandar 100 MB
  // llenaria la memoria del proceso.
  let raw = '';
  let tooBig = false;
  req.setEncoding('utf8');
  for await (const chunk of req) {
    if (raw.length + chunk.length > LIMITS.BODY_MAX) { tooBig = true; break; }
    raw += chunk;
  }
  if (tooBig) return send(res, 413, shapeError('cuerpo demasiado grande'));

  const parsed = parseRequest(raw);
  if (!parsed.ok) return send(res, parsed.status, shapeError(parsed.error));

  if (!rateOk(parsed.req.device)) return send(res, 429, shapeError('demasiadas peticiones'));

  try {
    const out = await providers.run(parsed.req, env);
    return send(res, 200, shapeResponse(out));
  } catch (e) {
    // NO se filtra el detalle interno al dispositivo: puede llevar la
    // URL del proveedor o parte de una clave. Al log si, que es de quien
    // administra el servidor.
    console.error('[flexai] fallo del proveedor:', e && e.message);
    return send(res, 502, shapeError('el proveedor no respondio'));
  }
}

function start() {
  const wrap = (req, res) => {
    handle(req, res).catch((e) => {
      console.error('[flexai] error no controlado:', e && e.message);
      try { send(res, 500, shapeError('error interno')); } catch { /* socket ya cerrado */ }
    });
  };

  let server;
  if (CERT && KEY) {
    server = https.createServer({ cert: fs.readFileSync(CERT), key: fs.readFileSync(KEY) }, wrap);
  } else if (ALLOW_PLAIN) {
    console.warn('[flexai] AVISO: arrancando SIN TLS (FLEXAI_ALLOW_PLAINTEXT=1).');
    console.warn('[flexai] Flex Intelligence EXIGE https y NO se conectara asi.');
    console.warn('[flexai] Sirve solo para probar el protocolo con curl.');
    server = http.createServer(wrap);
  } else {
    console.error('[flexai] Falta el certificado: define FLEXAI_TLS_CERT y FLEXAI_TLS_KEY.');
    console.error('[flexai] Genera uno de pruebas con:  npm run cert');
    process.exit(1);
  }

  server.headersTimeout = 15000;
  server.requestTimeout = 30000;
  server.listen(PORT, HOST, () => {
    const proto = CERT && KEY ? 'https' : 'http';
    console.log(`[flexai] escuchando en ${proto}://${HOST}:${PORT}/v1/flex`);
    console.log(`[flexai] proveedor: ${(env.FLEXAI_PROVIDER || 'local').toLowerCase()}`);
    console.log('[flexai] token cargado del entorno (no se muestra)');
  });
}

if (require.main === module) start();

module.exports = { tokenOk, start };
