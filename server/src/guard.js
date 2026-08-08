'use strict';
// #############################################################
//  guard.js  ·  VALIDACION DE DESTINOS (anti-SSRF)
//  ------------------------------------------------------------
//  ESTA ES LA PIEZA DE SEGURIDAD DEL SERVICIO. El proceso ejecuta un
//  Chromium DE VERDAD dentro de una red que casi seguro tiene otras
//  cosas: el router, un NAS, la API de metadatos del proveedor de
//  nube, el propio panel de administracion del servidor. Un
//  dispositivo (o una pagina que consiga provocar una navegacion)
//  que pidiera "http://169.254.169.254/latest/meta-data/" estaria
//  usando el servicio como puente hacia dentro. Eso es SSRF, y es el
//  fallo con mas impacto que puede tener un servicio como este.
//
//  QUE HACE ESTE MODULO
//  --------------------
//    1. Solo deja pasar http y https. Cualquier otro esquema fuera:
//       file:, data:, blob:, ftp:, chrome:, view-source:, javascript:
//    2. Rechaza credenciales incrustadas (usuario:clave@).
//    3. RESUELVE EL DNS y comprueba TODAS las direcciones devueltas,
//       no solo la primera. Un nombre publico puede resolver a una IP
//       privada (el ataque clasico de "DNS rebinding" en su version
//       mas simple), asi que mirar solo el texto del host no basta.
//    4. Bloquea por RANGO, no por lista de direcciones sueltas:
//       loopback, privadas, enlace local (169.254/16, que es donde
//       viven los metadatos de AWS/GCP/Azure), CGNAT, multicast,
//       reservadas, y sus equivalentes en IPv6.
//    5. Se vuelve a comprobar EN CADA PETICION DE NAVEGACION, asi que
//       una redireccion 302 hacia un destino interno tampoco pasa.
//
//  LO QUE ESTE MODULO NO PUEDE HACER SOLO
//  --------------------------------------
//  Entre que se resuelve el DNS aqui y que Chromium abre la conexion
//  hay una ventana en la que el mismo nombre podria resolver a otra
//  IP (DNS rebinding de verdad). Cerrar esa ventana del todo exige
//  aislamiento de red. Por eso el README insiste en ejecutar el
//  servicio en un contenedor con la salida a redes privadas
//  bloqueada por firewall: este modulo es la primera barrera, no la
//  unica. Ver server/README.md ("Aislamiento de red").
// #############################################################

const dns = require('node:dns').promises;
const net = require('node:net');

const ALLOWED_SCHEMES = new Set(['http:', 'https:']);

// Sufijos que nunca deberian salir de una red local.
const LOCAL_SUFFIXES = [
  '.local', '.internal', '.localdomain', '.lan', '.home', '.home.arpa',
  '.intranet', '.corp', '.private', '.test', '.invalid', '.localhost'
];

const REASONS = {
  OK: null,
  SCHEME: 'esquema no permitido',
  USERINFO: 'la URL lleva credenciales incrustadas',
  HOSTNAME: 'nombre de host no valido',
  LOCALNAME: 'nombre de red local',
  IPLITERAL: 'direcciones IP directas no permitidas',
  PRIVATE: 'la direccion resuelve a una red interna',
  DNS: 'no se pudo resolver el nombre',
  PORT: 'puerto no permitido',
  LENGTH: 'URL demasiado larga'
};

// -------------------------------------------------------------
//  Clasificacion de direcciones
// -------------------------------------------------------------
function ipv4ToInt(ip) {
  const p = ip.split('.');
  if (p.length !== 4) return null;
  let v = 0;
  for (const s of p) {
    if (!/^\d{1,3}$/.test(s)) return null;
    const n = Number(s);
    if (n > 255) return null;
    v = (v * 256) + n;
  }
  return v >>> 0;
}

function inCidr4(ipInt, base, bits) {
  const mask = bits === 0 ? 0 : (0xffffffff << (32 - bits)) >>> 0;
  return (ipInt & mask) === (ipv4ToInt(base) & mask);
}

// Rangos IPv4 que NO se pueden alcanzar desde el navegador remoto.
// La lista sale de los registros de IANA (RFC 6890 y siguientes); se
// bloquea por rango y no por direccion concreta a proposito: asi no hay
// que ir anadiendo casos segun aparecen.
const BLOCKED_V4 = [
  ['0.0.0.0', 8, 'esta red'],
  ['10.0.0.0', 8, 'privada'],
  ['100.64.0.0', 10, 'CGNAT'],
  ['127.0.0.0', 8, 'loopback'],
  ['169.254.0.0', 16, 'enlace local / metadatos de nube'],
  ['172.16.0.0', 12, 'privada'],
  ['192.0.0.0', 24, 'asignaciones de protocolo IETF'],
  ['192.0.2.0', 24, 'documentacion'],
  ['192.88.99.0', 24, '6to4 obsoleto'],
  ['192.168.0.0', 16, 'privada'],
  ['198.18.0.0', 15, 'pruebas de rendimiento'],
  ['198.51.100.0', 24, 'documentacion'],
  ['203.0.113.0', 24, 'documentacion'],
  ['224.0.0.0', 4, 'multicast'],
  ['240.0.0.0', 4, 'reservada']
];

function classifyV4(ip) {
  const v = ipv4ToInt(ip);
  if (v === null) return 'no es IPv4';
  for (const [base, bits, why] of BLOCKED_V4) if (inCidr4(v, base, bits)) return why;
  return null;                    // publica
}

function classifyV6(ip) {
  const s = ip.toLowerCase().replace(/^\[|\]$/g, '');
  if (s === '::' || s === '::1') return 'loopback / sin especificar';
  // IPv4 embebida (::ffff:10.0.0.1, ::ffff:0:10.0.0.1): se clasifica la
  // IPv4 de dentro. Sin esto, ::ffff:127.0.0.1 seria una via de escape.
  const m = s.match(/(\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3})$/);
  if (m) {
    const inner = classifyV4(m[1]);
    if (inner) return `IPv4 embebida (${inner})`;
    if (s.startsWith('::ffff:') || s.startsWith('::')) return null;
  }
  if (/^f[cd]/.test(s)) return 'unica local (fc00::/7)';
  if (/^fe[89ab]/.test(s)) return 'enlace local (fe80::/10)';
  if (/^ff/.test(s)) return 'multicast';
  if (/^2001:0?db8/.test(s)) return 'documentacion';
  if (/^64:ff9b:/.test(s)) return 'NAT64';
  return null;
}

function classifyAddress(ip) {
  const v = net.isIP(ip);
  if (v === 4) return classifyV4(ip);
  if (v === 6) return classifyV6(ip);
  return 'direccion no reconocida';
}

// Un host es "literal" si el usuario escribio una direccion en vez de un
// nombre. Se detecta en TODAS las formas que acepta un navegador --
// decimal con puntos, decimal a secas, hexadecimal y IPv6 -- porque
// todas se rechazan igual y asi no hay que acertar con la variante.
function isIpLiteral(host) {
  if (!host) return false;
  if (host.startsWith('[')) return true;
  if (net.isIP(host)) return true;
  if (/^0x[0-9a-f]+$/i.test(host)) return true;
  if (/^\d+$/.test(host)) return true;                 // 2130706433
  if (/^[\d.]+$/.test(host)) return true;              // 010.0.0.1, 1.2.3
  return false;
}

function isLocalName(host) {
  const h = host.toLowerCase();
  if (h === 'localhost') return true;
  return LOCAL_SUFFIXES.some((s) => h.endsWith(s));
}

// -------------------------------------------------------------
//  Comprobacion sincrona (forma de la URL)
// -------------------------------------------------------------
function checkUrlShape(rawUrl, opts = {}) {
  const { allowPrivate = false, allowHosts = [], maxLength = 2048, allowedPorts = null } = opts;

  if (typeof rawUrl !== 'string' || rawUrl.length === 0) return { ok: false, reason: REASONS.HOSTNAME };
  if (rawUrl.length > maxLength) return { ok: false, reason: REASONS.LENGTH };

  let u;
  try { u = new URL(rawUrl); } catch { return { ok: false, reason: REASONS.HOSTNAME }; }

  if (!ALLOWED_SCHEMES.has(u.protocol)) return { ok: false, reason: `${REASONS.SCHEME} (${u.protocol})` };
  if (u.username || u.password) return { ok: false, reason: REASONS.USERINFO };

  const host = u.hostname;
  if (!host) return { ok: false, reason: REASONS.HOSTNAME };

  // Lista blanca explicita (desarrollo). Se compara con el host TAL CUAL
  // lo normaliza la clase URL, no con el texto que llego.
  const whitelisted = allowHosts.some((h) => h.toLowerCase() === host.toLowerCase());
  if (whitelisted) return { ok: true, url: u, whitelisted: true };

  if (u.port) {
    const p = Number(u.port);
    if (!Number.isInteger(p) || p < 1 || p > 65535) return { ok: false, reason: REASONS.PORT };
    if (allowedPorts && !allowedPorts.includes(p)) return { ok: false, reason: `${REASONS.PORT} (${p})` };
  }

  if (!allowPrivate) {
    if (isIpLiteral(host)) {
      // Aunque sea una IP publica: si el dispositivo quiere una IP
      // concreta, que la ponga en la lista blanca. Permitir IP sueltas
      // abre la puerta a recorrer la red por fuerza bruta.
      const why = classifyAddress(host.replace(/^\[|\]$/g, ''));
      return { ok: false, reason: why ? `${REASONS.PRIVATE} (${why})` : REASONS.IPLITERAL };
    }
    if (isLocalName(host)) return { ok: false, reason: REASONS.LOCALNAME };
    if (!host.includes('.')) return { ok: false, reason: REASONS.LOCALNAME };
  }

  return { ok: true, url: u, whitelisted: false };
}

// -------------------------------------------------------------
//  Comprobacion completa (forma + DNS)
// -------------------------------------------------------------
async function checkUrl(rawUrl, opts = {}) {
  const shape = checkUrlShape(rawUrl, opts);
  if (!shape.ok) return shape;
  if (shape.whitelisted || opts.allowPrivate) return { ok: true, url: shape.url, addresses: [] };

  const host = shape.url.hostname;
  let records;
  try {
    // `all: true` para ver TODAS las direcciones: si una sola de ellas es
    // interna, el destino se rechaza entero. Quedarse con la primera
    // dejaria pasar un nombre con varios registros A.
    records = await dns.lookup(host, { all: true, verbatim: true });
  } catch {
    return { ok: false, reason: REASONS.DNS };
  }
  if (!records || records.length === 0) return { ok: false, reason: REASONS.DNS };

  for (const r of records) {
    const why = classifyAddress(r.address);
    if (why) return { ok: false, reason: `${REASONS.PRIVATE}: ${r.address} (${why})` };
  }
  return { ok: true, url: shape.url, addresses: records.map((r) => r.address) };
}

module.exports = {
  REASONS, ALLOWED_SCHEMES, BLOCKED_V4,
  ipv4ToInt, classifyV4, classifyV6, classifyAddress,
  isIpLiteral, isLocalName, checkUrlShape, checkUrl
};
