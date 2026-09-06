'use strict';
// #############################################################
//  FLEX SDK · ENSAMBLADOR flex-app-v1  (.flexasm -> .flxb)
//  ------------------------------------------------------------
//  Lenguaje deliberadamente pequeño y sin sorpresas: una máquina de
//  pila no necesita más, y todo lo que el ensamblador NO deja escribir
//  es una clase de error que el firmware no tiene que rechazar luego.
//
//  DIRECTIVAS
//    .mem <bytes>            tamaño de la memoria lineal (múltiplo de 4)
//    .stack <slots>          pila de operandos
//    .frames <slots>         reserva total de variables locales
//    .calldepth <n>          profundidad máxima de llamadas
//    .const  <nombre> "texto"   constante de texto
//    .bytes  <nombre> 0A 0B ..  constante binaria (hex)
//    .global <nombre>        declara una variable global
//    .func <nombre> args=a,b locals=x,y
//      ...
//    .end
//
//  MNEMONICOS: los de sdk/lib/isa.js. Además dos comodidades:
//    push <n>          elige push.i8 o push.i32
//    pushk <nombre>    apila (offset, longitud) de una constante
//  Las etiquetas se escriben "nombre:" y se referencian en jmp/jz/jnz.
// #############################################################
const { OPCODES, SYSCALLS, HANDLERS, HANDLER_ARGS, LIMITS } = require('./isa');

class AsmError extends Error {
  constructor(line, msg) {
    super(`linea ${line}: ${msg}`);
    this.line = line;
  }
}

function parseInt32(tok, line) {
  let m = /^-?0x[0-9a-fA-F]+$/.test(tok) ? parseInt(tok, 16) : null;
  if (m === null) {
    if (!/^-?\d+$/.test(tok)) throw new AsmError(line, `numero invalido: ${tok}`);
    m = parseInt(tok, 10);
  }
  if (!Number.isSafeInteger(m) || m < -2147483648 || m > 4294967295) {
    throw new AsmError(line, `numero fuera de rango: ${tok}`);
  }
  return m | 0;
}

// Divide una línea respetando cadenas entre comillas y quitando comentarios.
function tokenize(raw, line) {
  const out = [];
  let i = 0;
  while (i < raw.length) {
    const c = raw[i];
    if (c === ';' || (c === '/' && raw[i + 1] === '/')) break;
    // La coma NO separa: forma parte de "args=a,b". Los sitios que aceptan
    // listas (.local, .func) parten sus propios tokens por coma.
    if (c === ' ' || c === '\t') { i++; continue; }
    if (c === '"') {
      let s = '';
      i++;
      while (i < raw.length && raw[i] !== '"') {
        if (raw[i] === '\\') {
          const n = raw[i + 1];
          if (n === 'n') s += '\n';
          else if (n === 't') s += '\t';
          else if (n === '"') s += '"';
          else if (n === '\\') s += '\\';
          else throw new AsmError(line, `escape desconocido \\${n}`);
          i += 2;
          continue;
        }
        s += raw[i++];
      }
      if (raw[i] !== '"') throw new AsmError(line, 'cadena sin cerrar');
      i++;
      out.push({ str: s });
      continue;
    }
    let j = i;
    while (j < raw.length && !' \t;'.includes(raw[j])) j++;
    out.push({ tok: raw.slice(i, j) });
    i = j;
  }
  return out;
}

function assemble(source, opts = {}) {
  const lines = source.split(/\r?\n/);
  const consts = new Map();          // nombre -> { off, len }
  const globals = new Map();         // nombre -> índice
  const funcs = [];                  // { name, args:[], locals:[], items:[], index }
  const funcByName = new Map();
  let constBuf = [];
  let cfg = { mem: 4096, stack: 128, frames: 128, calldepth: 16 };
  let cur = null;

  const addConst = (name, bytes, line) => {
    if (consts.has(name)) throw new AsmError(line, `constante repetida: ${name}`);
    const off = constBuf.length;
    constBuf.push(...bytes);
    consts.set(name, { off, len: bytes.length });
  };

  // ---- Pasada 1: estructura -------------------------------------------
  for (let li = 0; li < lines.length; li++) {
    const line = li + 1;
    const toks = tokenize(lines[li], line);
    if (toks.length === 0) continue;
    const head = toks[0].tok;
    if (head === undefined) throw new AsmError(line, 'una cadena no puede abrir una linea');

    if (head.startsWith('.')) {
      const d = head.toLowerCase();
      if (d === '.mem' || d === '.stack' || d === '.frames' || d === '.calldepth') {
        if (cur) throw new AsmError(line, `${d} va fuera de una funcion`);
        const v = parseInt32(toks[1].tok, line);
        if (v < 0) throw new AsmError(line, `${d} no puede ser negativo`);
        cfg[d.slice(1)] = v;
        continue;
      }
      if (d === '.const') {
        if (cur) throw new AsmError(line, '.const va fuera de una funcion');
        const name = toks[1].tok;
        if (toks[2] === undefined || toks[2].str === undefined) throw new AsmError(line, '.const necesita un texto entre comillas');
        addConst(name, Array.from(Buffer.from(toks[2].str, 'utf8')), line);
        continue;
      }
      if (d === '.bytes') {
        if (cur) throw new AsmError(line, '.bytes va fuera de una funcion');
        const name = toks[1].tok;
        const bytes = [];
        for (let k = 2; k < toks.length; k++) {
          const t = toks[k].tok;
          if (!/^[0-9a-fA-F]{2}$/.test(t)) throw new AsmError(line, `byte invalido: ${t}`);
          bytes.push(parseInt(t, 16));
        }
        if (!bytes.length) throw new AsmError(line, '.bytes vacio');
        addConst(name, bytes, line);
        continue;
      }
      if (d === '.global') {
        if (cur) throw new AsmError(line, '.global va fuera de una funcion');
        const name = toks[1].tok;
        if (globals.has(name)) throw new AsmError(line, `global repetida: ${name}`);
        globals.set(name, globals.size);
        continue;
      }
      if (d === '.func') {
        if (cur) throw new AsmError(line, '.func dentro de otra .func');
        const name = toks[1] && toks[1].tok;
        if (!name) throw new AsmError(line, '.func necesita un nombre');
        if (funcByName.has(name)) throw new AsmError(line, `funcion repetida: ${name}`);
        const f = { name, args: [], locals: [], items: [], line, index: funcs.length };
        for (let k = 2; k < toks.length; k++) {
          const t = toks[k].tok || '';
          const m = /^(args|locals)=(.*)$/.exec(t);
          if (!m) throw new AsmError(line, `parametro desconocido en .func: ${t}`);
          const names = m[2] === '' ? [] : m[2].split(',').filter(Boolean);
          f[m[1]] = names;
        }
        if (HANDLERS.includes(name) && f.args.length !== HANDLER_ARGS[name]) {
          throw new AsmError(line, `${name} necesita exactamente ${HANDLER_ARGS[name]} argumentos`);
        }
        funcs.push(f);
        funcByName.set(name, f);
        cur = f;
        continue;
      }
      if (d === '.end') {
        if (!cur) throw new AsmError(line, '.end sin .func');
        cur = null;
        continue;
      }
      if (d === '.local') {
        if (!cur) throw new AsmError(line, '.local fuera de una funcion');
        for (let k = 1; k < toks.length; k++)
          for (const n of (toks[k].tok || '').split(',').filter(Boolean)) cur.locals.push(n);
        continue;
      }
      throw new AsmError(line, `directiva desconocida: ${head}`);
    }

    if (head.endsWith(':')) {
      if (!cur) throw new AsmError(line, 'etiqueta fuera de una funcion');
      const name = head.slice(0, -1);
      if (!name) throw new AsmError(line, 'etiqueta vacia');
      cur.items.push({ kind: 'label', name, line });
      if (toks.length > 1) throw new AsmError(line, 'la etiqueta va sola en su linea');
      continue;
    }

    if (!cur) throw new AsmError(line, `instruccion fuera de una funcion: ${head}`);
    cur.items.push({ kind: 'insn', mnemonic: head.toLowerCase(), toks: toks.slice(1), line });
  }
  if (cur) throw new AsmError(lines.length, `.func ${cur.name} sin .end`);
  if (!funcs.length) throw new AsmError(1, 'el programa no tiene funciones');

  // ---- Pasada 2: expandir macros y medir -------------------------------
  // Cada instrucción se convierte en una o varias entradas concretas con su
  // longitud conocida; sólo los saltos quedan pendientes de resolver.
  for (const f of funcs) {
    const slots = new Map();
    f.args.forEach((n, i) => slots.set(n, i));
    f.locals.forEach((n, i) => {
      if (slots.has(n)) throw new AsmError(f.line, `local repetida: ${n}`);
      slots.set(n, f.args.length + i);
    });
    if (f.args.length > LIMITS.MAX_ARGS) throw new AsmError(f.line, 'demasiados argumentos');
    if (f.args.length + f.locals.length > 255) throw new AsmError(f.line, 'demasiadas variables locales');
    f.slots = slots;
    const out = [];
    for (const it of f.items) {
      if (it.kind === 'label') { out.push(it); continue; }
      const { mnemonic: mn, toks, line } = it;

      if (mn === 'push') {
        if (toks.length !== 1) throw new AsmError(line, 'push necesita un valor');
        const t = toks[0];
        const v = t.tok !== undefined ? parseInt32(t.tok, line) : null;
        if (v === null) throw new AsmError(line, 'push no acepta cadenas (usa pushk)');
        if (v >= -128 && v <= 127) out.push({ kind: 'insn', mn: 'push.i8', arg: v, line });
        else out.push({ kind: 'insn', mn: 'push.i32', arg: v, line });
        continue;
      }
      if (mn === 'pushk') {
        if (toks.length !== 1 || toks[0].tok === undefined) throw new AsmError(line, 'pushk necesita el nombre de una constante');
        const c = consts.get(toks[0].tok);
        if (!c) throw new AsmError(line, `constante desconocida: ${toks[0].tok}`);
        const emit = (v) => (v >= -128 && v <= 127)
          ? { kind: 'insn', mn: 'push.i8', arg: v, line }
          : { kind: 'insn', mn: 'push.i32', arg: v, line };
        out.push(emit(c.off), emit(c.len));
        continue;
      }

      const def = OPCODES[mn];
      if (!def) throw new AsmError(line, `instruccion desconocida: ${mn}`);
      let arg = null;
      if (def.imm === null) {
        if (toks.length) throw new AsmError(line, `${mn} no lleva operando`);
      } else {
        if (toks.length !== 1 || toks[0].tok === undefined) throw new AsmError(line, `${mn} necesita un operando`);
        const t = toks[0].tok;
        if (def.imm === 'rel16') arg = t;                       // etiqueta, se resuelve luego
        else if (def.imm === 'func') {
          const g = funcByName.get(t);
          if (!g) throw new AsmError(line, `funcion desconocida: ${t}`);
          arg = g.index;
        } else if (def.imm === 'sys') {
          const s = SYSCALLS[t];
          if (!s) throw new AsmError(line, `syscall desconocida: ${t}`);
          arg = s.id;
        } else if (mn === 'ldglobal' || mn === 'stglobal') {
          if (globals.has(t)) arg = globals.get(t);
          else arg = parseInt32(t, line);
          if (arg < 0 || arg >= LIMITS.MAX_GLOBALS) throw new AsmError(line, `global fuera de rango: ${t}`);
        } else if (mn === 'ldlocal' || mn === 'stlocal') {
          if (slots.has(t)) arg = slots.get(t);
          else arg = parseInt32(t, line);
          if (arg < 0 || arg > 255) throw new AsmError(line, `local fuera de rango: ${t}`);
        } else {
          arg = parseInt32(t, line);
          if (def.imm === 'u8' && (arg < 0 || arg > 255)) throw new AsmError(line, `${mn}: operando fuera de 0..255`);
          if (def.imm === 'u16' && (arg < 0 || arg > 65535)) throw new AsmError(line, `${mn}: operando fuera de 0..65535`);
          if (def.imm === 'i8' && (arg < -128 || arg > 127)) throw new AsmError(line, `${mn}: operando fuera de -128..127`);
        }
      }
      out.push({ kind: 'insn', mn, arg, line });
    }
    f.emitted = out;
  }

  const sizeOf = (mn) => {
    const d = OPCODES[mn];
    if (d.imm === null) return 1;
    if (d.imm === 'i8' || d.imm === 'u8') return 2;
    if (d.imm === 'i32') return 5;
    return 3;
  };

  // ---- Pasada 3: direcciones -------------------------------------------
  let pc = 0;
  const labels = new Map();          // "func/label" -> pc
  for (const f of funcs) {
    f.offset = pc;
    for (const it of f.emitted) {
      if (it.kind === 'label') { labels.set(`${f.name}/${it.name}`, pc); continue; }
      it.pc = pc;
      pc += sizeOf(it.mn);
    }
  }
  const codeLen = pc;
  if (codeLen === 0) throw new AsmError(1, 'el programa no tiene codigo');
  if (codeLen > LIMITS.MAX_CODE) throw new AsmError(1, 'el codigo supera el maximo del formato');

  // ---- Pasada 4: emitir ------------------------------------------------
  const code = Buffer.alloc(codeLen);
  for (const f of funcs) {
    for (const it of f.emitted) {
      if (it.kind === 'label') continue;
      const d = OPCODES[it.mn];
      let p = it.pc;
      code[p++] = d.op;
      if (d.imm === null) continue;
      if (d.imm === 'i8') code.writeInt8(it.arg, p);
      else if (d.imm === 'u8') code.writeUInt8(it.arg, p);
      else if (d.imm === 'i32') code.writeInt32LE(it.arg | 0, p);
      else if (d.imm === 'rel16') {
        const key = `${f.name}/${it.arg}`;
        if (!labels.has(key)) throw new AsmError(it.line, `etiqueta desconocida: ${it.arg}`);
        const target = labels.get(key);
        const rel = target - (it.pc + 3);
        if (rel < -32768 || rel > 32767) throw new AsmError(it.line, 'salto demasiado lejos');
        code.writeInt16LE(rel, p);
      } else {
        code.writeUInt16LE(it.arg, p);
      }
    }
  }

  // ---- Contenedor FLXB --------------------------------------------------
  const constBytes = Buffer.from(constBuf);
  if (constBytes.length > LIMITS.MAX_CONST) throw new AsmError(1, 'el pool de constantes supera el maximo');
  if (funcs.length > LIMITS.MAX_FUNCS) throw new AsmError(1, 'demasiadas funciones');
  if (globals.size > LIMITS.MAX_GLOBALS) throw new AsmError(1, 'demasiadas globales');

  const mem = cfg.mem, stack = cfg.stack, frames = cfg.frames, calld = cfg.calldepth;
  if (mem % 4 !== 0 || mem > LIMITS.MAX_MEM) throw new AsmError(1, '.mem invalido (multiplo de 4 y <= 1 MB)');
  if (stack < 1 || stack > LIMITS.MAX_STACK) throw new AsmError(1, '.stack invalido');
  if (frames > LIMITS.MAX_FRAMESLOTS) throw new AsmError(1, '.frames invalido');
  if (calld < 1 || calld > LIMITS.MAX_CALLDEPTH) throw new AsmError(1, '.calldepth invalido');
  for (const f of funcs) {
    if (f.args.length + f.locals.length > frames) {
      throw new AsmError(f.line, `.frames no da para las variables de ${f.name}`);
    }
  }

  const funcTab = Buffer.alloc(funcs.length * LIMITS.FUNC_BYTES);
  funcs.forEach((f, i) => {
    const b = i * LIMITS.FUNC_BYTES;
    funcTab.writeUInt32LE(f.offset, b);
    funcTab.writeUInt8(f.args.length, b + 4);
    funcTab.writeUInt8(f.locals.length, b + 5);
    funcTab.writeUInt16LE(0, b + 6);
  });

  const header = Buffer.alloc(LIMITS.HEADER_BYTES);
  header.write('FLXB', 0, 'ascii');
  header.writeUInt16LE(1, 4);
  header.writeUInt16LE(0, 6);
  header.writeUInt32LE(codeLen, 8);
  header.writeUInt32LE(constBytes.length, 12);
  header.writeUInt32LE(mem, 16);
  header.writeUInt16LE(globals.size, 20);
  header.writeUInt16LE(stack, 22);
  header.writeUInt16LE(frames, 24);
  header.writeUInt16LE(calld, 26);
  header.writeUInt16LE(funcs.length, 28);
  header.writeUInt16LE(0, 30);
  let declared = 0;
  HANDLERS.forEach((h, i) => {
    const f = funcByName.get(h);
    header.writeUInt16LE(f ? f.index : LIMITS.NO_HANDLER, 32 + i * 2);
    if (f) declared++;
  });
  if (!declared) throw new AsmError(1, 'el programa no declara ningun manejador (onStart/onEvent/onTick/onStop)');

  const image = Buffer.concat([header, funcTab, constBytes, code]);
  return {
    image,
    stats: {
      bytes: image.length,
      code: codeLen,
      consts: constBytes.length,
      funcs: funcs.length,
      globals: globals.size,
      mem, stack, frames, calldepth: calld,
      handlers: HANDLERS.filter((h) => funcByName.has(h))
    },
    symbols: {
      consts: Object.fromEntries([...consts].map(([k, v]) => [k, v])),
      globals: Object.fromEntries(globals),
      funcs: Object.fromEntries(funcs.map((f) => [f.name, f.index]))
    },
    // Las syscalls que el programa usa de verdad: sirve para comprobar que el
    // manifest no pide permisos de más ni de menos.
    usedSyscalls: [...new Set(funcs.flatMap((f) => f.emitted
      .filter((i) => i.kind === 'insn' && i.mn === 'sys')
      .map((i) => Object.keys(SYSCALLS).find((k) => SYSCALLS[k].id === i.arg))))]
      .filter(Boolean).sort()
  };
}

module.exports = { assemble, AsmError };
