'use strict';
// #############################################################
//  FLEX SDK · MANIFEST: VALIDADOR Y ESCRITURA CANONICA
//  ------------------------------------------------------------
//  Las reglas de aqui son LAS MISMAS que aplica el firmware en
//  FlexOS_PkgCore.cpp (parseManifestJson). Estan duplicadas a
//  proposito: el SDK tiene que poder decir "esto lo va a rechazar la
//  placa" ANTES de firmar nada, en vez de descubrirlo al instalar.
//  sdk/test/sdk.test.js comprueba las dos implementaciones contra el
//  mismo paquete, asi que no pueden separarse en silencio.
//
//  JSON CANONICO. El firmware exige que el manifest y el indice sean
//  byte a byte lo que cJSON reimprimiria: compacto, sin espacios y en
//  el orden en que estan escritas las claves. Ademas se obliga a que
//  todas las cadenas sean ASCII imprimible sin comillas ni barras
//  invertidas: asi no hay UNA sola forma discutible de escapar nada.
// #############################################################
const { SYS_PERMISSIONS, UI_PERMISSIONS } = require('./isa');

const RUNTIMES = ['flex-ui-1', 'flex-app-v1'];

function isSafeId(s) {
  if (typeof s !== 'string' || s.length < 5 || s.length > 96) return false;
  if (!/^[a-z]/.test(s)) return false;
  if (!/^[a-z0-9_]+(\.[a-z][a-z0-9_]*)+$/.test(s)) return false;
  const dots = (s.match(/\./g) || []).length;
  return dots >= 2 && dots <= 7;
}

function isSafePath(s) {
  if (typeof s !== 'string' || !s.length || s.length > 240) return false;
  if (s.startsWith('/') || s.startsWith('.') || s.includes('\\')) return false;
  for (const part of s.split('/')) {
    if (!part || part === '.' || part === '..') return false;
    if (!/^[A-Za-z0-9._-]+$/.test(part)) return false;
  }
  return true;
}

function isSemver(s) {
  return typeof s === 'string' && /^\d+\.\d+\.\d+(-[a-z0-9.-]+)?$/.test(s);
}

// Cadena apta para el manifest: ASCII imprimible, sin " ni \.
function isPlainText(s, max) {
  return typeof s === 'string' && s.length <= max && /^[\x20-\x7E]*$/.test(s) &&
         !s.includes('"') && !s.includes('\\');
}

function isUint(v, lo, hi) {
  return Number.isInteger(v) && v >= lo && v <= hi;
}

// Comprueba una descripcion de app (el flexapp.json del proyecto) y devuelve
// { errors: [], warnings: [] }.
function validate(app) {
  const errors = [];
  const warnings = [];
  const bad = (m) => errors.push(m);

  if (!app || typeof app !== 'object') return { errors: ['el manifest no es un objeto'], warnings };

  if (!isSafeId(app.id)) bad(`id invalido: ${JSON.stringify(app.id)} (forma a.b.c, minusculas, 3..8 segmentos)`);
  if (!isPlainText(app.name, 60) || (app.name || '').length < 2) bad('name invalido (2..60 caracteres ASCII imprimibles)');
  if (!isSemver(app.versionName)) bad(`versionName invalido: ${JSON.stringify(app.versionName)}`);
  if (!isUint(app.versionCode, 1, 0xFFFFFFFF)) bad('versionCode tiene que ser un entero >= 1');
  if (!isSemver(app.minFlexOS)) bad(`minFlexOS invalido: ${JSON.stringify(app.minFlexOS)}`);
  if (!RUNTIMES.includes(app.runtime)) bad(`runtime invalido: ${JSON.stringify(app.runtime)} (${RUNTIMES.join(' o ')})`);
  if (!isSafePath(app.entry)) bad(`entry invalido: ${JSON.stringify(app.entry)}`);
  if (app.summary !== undefined && !isPlainText(app.summary, 159)) bad('summary invalido (max 159 ASCII imprimibles)');
  if (app.category !== undefined && !isPlainText(app.category, 39)) bad('category invalido (max 39 ASCII imprimibles)');

  const lim = app.limits || {};
  if (!isUint(lim.memoryKB, 64, 4096)) bad('limits.memoryKB tiene que estar entre 64 y 4096');
  if (!isUint(lim.storageKB, 0, 8192)) bad('limits.storageKB tiene que estar entre 0 y 8192');
  for (const [k, lo, hi] of [['instrPerTick', 1000, 400000], ['usPerTick', 500, 12000], ['drawPerFrame', 32, 4096]]) {
    if (lim[k] !== undefined && !isUint(lim[k], lo, hi)) bad(`limits.${k} tiene que estar entre ${lo} y ${hi}`);
    if (lim[k] !== undefined && app.runtime !== 'flex-app-v1') bad(`limits.${k} solo tiene sentido en flex-app-v1`);
  }

  const perms = app.permissions || [];
  if (!Array.isArray(perms) || perms.length > 16) bad('permissions tiene que ser una lista de como mucho 16 nombres');
  else {
    const seen = new Set();
    for (const p of perms) {
      if (!UI_PERMISSIONS.includes(p)) bad(`permiso de manifest desconocido: ${JSON.stringify(p)}`);
      if (seen.has(p)) bad(`permiso repetido: ${p}`);
      seen.add(p);
    }
  }

  const sysPerms = app.systemPermissions || [];
  if (!Array.isArray(sysPerms) || sysPerms.length > 16) bad('systemPermissions tiene que ser una lista de como mucho 16 nombres');
  else {
    const seen = new Set();
    for (const p of sysPerms) {
      if (!(p in SYS_PERMISSIONS)) bad(`permiso de sistema desconocido: ${JSON.stringify(p)}`);
      if (seen.has(p)) bad(`permiso de sistema repetido: ${p}`);
      seen.add(p);
    }
    if (sysPerms.length && app.runtime !== 'flex-app-v1') {
      bad('flex-ui-1 no puede pedir permisos de sistema: ese runtime no tiene forma de llamarlos');
    }
    if (sysPerms.length) {
      warnings.push('declarar un permiso de sistema NO lo concede: hace falta un grant firmado por Flex Store');
    }
  }
  return { errors, warnings };
}

function systemMask(names) {
  let m = 0;
  for (const n of names || []) m |= (SYS_PERMISSIONS[n] || 0);
  return m >>> 0;
}

// Escribe el manifest en la forma EXACTA que el firmware espera.
// El orden de las claves es parte del formato: no se reordena.
function canonicalManifest(app, developerKeySha256) {
  const m = {
    schema: 1,
    id: app.id,
    name: app.name,
    version: { name: app.versionName, code: app.versionCode },
    minFlexOS: app.minFlexOS,
    runtime: app.runtime,
    entry: app.entry,
    developerKeySha256
  };
  if (app.summary) m.summary = app.summary;
  if (app.category) m.category = app.category;
  const lim = { memoryKB: app.limits.memoryKB, storageKB: app.limits.storageKB };
  if (app.limits.instrPerTick !== undefined) lim.instrPerTick = app.limits.instrPerTick;
  if (app.limits.usPerTick !== undefined) lim.usPerTick = app.limits.usPerTick;
  if (app.limits.drawPerFrame !== undefined) lim.drawPerFrame = app.limits.drawPerFrame;
  m.limits = lim;
  m.permissions = app.permissions || [];
  if (app.systemPermissions && app.systemPermissions.length) {
    m.systemPermissions = app.systemPermissions;
  }
  return JSON.stringify(m);
}

module.exports = {
  validate, canonicalManifest, systemMask,
  isSafeId, isSafePath, isSemver, isPlainText, RUNTIMES
};
