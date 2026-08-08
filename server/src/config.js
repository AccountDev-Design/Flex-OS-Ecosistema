'use strict';
// #############################################################
//  config.js  ·  configuracion del servicio
//  ------------------------------------------------------------
//  Orden de precedencia: valores por defecto < fichero JSON
//  (--config o FLEXBR_CONFIG) < variables de entorno.
//
//  NO HAY NINGUN SECRETO EN EL REPOSITORIO. Las credenciales de los
//  dispositivos y los certificados salen del fichero de
//  configuracion o del entorno; config.example.json solo trae
//  marcadores. Si el servicio arranca sin credenciales configuradas,
//  se niega a aceptar conexiones y lo dice: un servicio de
//  navegacion abierto a cualquiera es un proxy abierto.
// #############################################################

const fs = require('node:fs');
const path = require('node:path');

const DEFAULTS = {
  host: '0.0.0.0',
  port: 8443,

  // TLS. Si falta alguno de los dos, el servicio arranca en HTTP/WS
  // PLANO y avisa por consola en cada arranque. Eso solo vale para una
  // red de laboratorio; en produccion, o se ponen los certificados aqui
  // o se pone un proxy inverso con TLS delante (ver README).
  tls: { key: null, cert: null },

  // Credenciales de dispositivo. Formato: [{ id, token }].
  // Tambien se pueden pasar por FLEXBR_DEVICES="id1:token1,id2:token2".
  devices: [],

  // Limites por sesion y globales.
  limits: {
    maxSessions: 4,              // dispositivos simultaneos
    maxTabsPerSession: 6,
    sessionIdleMs: 10 * 60 * 1000,
    navigationTimeoutMs: 25000,
    maxViewportW: 1280,
    maxViewportH: 1280,
    maxFrameBytes: 192 * 1024,   // tope por banda; el HELLO puede bajarlo
    maxBytesPerMinute: 12 * 1024 * 1024,
    maxUrlLength: 2048
  },

  // Captura.
  capture: {
    bandHeight: 200,             // altura de cada banda, en px de pantalla
    minIntervalMs: 90,           // ritmo maximo de captura
    keyframeEveryMs: 8000,       // refresco completo periodico
    jpegQualityMin: 20,
    jpegQualityMax: 90
  },

  // Multimedia. Ninguna de las placas de Flex OS tiene salida de audio,
  // asi que la ruta es de VIDEO: fotogramas JPEG del elemento <video>.
  media: {
    enabled: true,
    fps: 8,
    maxWidth: 640,
    quality: 45
  },

  // Seguridad de destinos.
  security: {
    allowPrivate: false,         // NO tocar salvo en un laboratorio aislado
    allowHosts: [],              // lista blanca explicita de desarrollo
    allowedPorts: null,          // null = cualquiera; ej. [80, 443]
    blockDownloads: true
  },

  browser: {
    executablePath: null,        // null = el que instale Playwright
    headless: true,
    userAgent: null,             // null = el de Chromium
    locale: 'es-ES',
    timezone: 'Europe/Madrid',
    args: []
  },

  logLevel: 'info'
};

function deepMerge(base, extra) {
  if (!extra || typeof extra !== 'object') return base;
  const out = Array.isArray(base) ? base.slice() : Object.assign({}, base);
  for (const [k, v] of Object.entries(extra)) {
    if (v && typeof v === 'object' && !Array.isArray(v) && base[k] && typeof base[k] === 'object' && !Array.isArray(base[k])) {
      out[k] = deepMerge(base[k], v);
    } else {
      out[k] = v;
    }
  }
  return out;
}

function parseDevicesEnv(s) {
  if (!s) return [];
  return s.split(',').map((pair) => {
    const i = pair.indexOf(':');
    if (i < 0) return null;
    const id = pair.slice(0, i).trim();
    const token = pair.slice(i + 1).trim();
    return (id && token) ? { id, token } : null;
  }).filter(Boolean);
}

function load(argv = process.argv, env = process.env) {
  let cfg = DEFAULTS;

  const ci = argv.indexOf('--config');
  const file = (ci >= 0 && argv[ci + 1]) ? argv[ci + 1] : env.FLEXBR_CONFIG;
  if (file) {
    const p = path.resolve(file);
    cfg = deepMerge(cfg, JSON.parse(fs.readFileSync(p, 'utf8')));
    cfg.configPath = p;
  }

  if (env.FLEXBR_HOST) cfg.host = env.FLEXBR_HOST;
  if (env.FLEXBR_PORT) cfg.port = Number(env.FLEXBR_PORT);
  if (env.FLEXBR_TLS_KEY) cfg.tls = Object.assign({}, cfg.tls, { key: env.FLEXBR_TLS_KEY });
  if (env.FLEXBR_TLS_CERT) cfg.tls = Object.assign({}, cfg.tls, { cert: env.FLEXBR_TLS_CERT });
  if (env.FLEXBR_DEVICES) cfg.devices = cfg.devices.concat(parseDevicesEnv(env.FLEXBR_DEVICES));
  if (env.FLEXBR_LOG_LEVEL) cfg.logLevel = env.FLEXBR_LOG_LEVEL;
  if (env.FLEXBR_ALLOW_PRIVATE === '1') cfg.security.allowPrivate = true;
  if (env.FLEXBR_ALLOW_HOSTS) cfg.security.allowHosts = env.FLEXBR_ALLOW_HOSTS.split(',').map((s) => s.trim()).filter(Boolean);
  if (env.FLEXBR_CHROMIUM) cfg.browser.executablePath = env.FLEXBR_CHROMIUM;

  return cfg;
}

// Comprobaciones de arranque. Devuelve la lista de problemas; el
// servidor decide cuales son fatales.
function validate(cfg) {
  const fatal = [];
  const warn = [];

  if (!Array.isArray(cfg.devices) || cfg.devices.length === 0) {
    fatal.push('No hay credenciales de dispositivo configuradas (devices[] o FLEXBR_DEVICES). ' +
               'Sin ellas el servicio seria un proxy abierto.');
  } else {
    for (const d of cfg.devices) {
      if (!d.id || !d.token) fatal.push('Cada dispositivo necesita "id" y "token".');
      else if (String(d.token).length < 16) fatal.push(`La credencial de "${d.id}" es demasiado corta (minimo 16 caracteres).`);
    }
  }

  if (!cfg.tls || !cfg.tls.key || !cfg.tls.cert) {
    warn.push('TLS desactivado: el servicio escuchara en HTTP/WS PLANO. ' +
              'Vale para una red de laboratorio; en produccion pon certificados o un proxy inverso.');
  }
  if (cfg.security.allowPrivate) {
    warn.push('security.allowPrivate esta ACTIVADO: el navegador remoto puede alcanzar redes internas. ' +
              'Usalo solo en un entorno aislado.');
  }
  if (cfg.security.allowHosts.length) {
    warn.push(`Lista blanca de desarrollo activa: ${cfg.security.allowHosts.join(', ')}`);
  }
  return { fatal, warn };
}

module.exports = { DEFAULTS, load, validate, deepMerge, parseDevicesEnv };
