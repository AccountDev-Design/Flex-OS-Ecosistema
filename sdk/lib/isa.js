'use strict';
// #############################################################
//  FLEX SDK · TABLA DE INSTRUCCIONES Y SYSCALLS DE flex-app-v1
//  ------------------------------------------------------------
//  ESTE ARCHIVO ES UN ESPEJO. La fuente de verdad es el firmware:
//    · opcodes y longitudes -> FlexOS_AppVM.cpp (opLength)
//    · syscalls y aridades  -> FlexOS_AppHost.cpp (tabla kSys)
//  Para que no se separen en silencio, tests/host/test_apphost.cpp
//  imprime la tabla del firmware y sdk/test/sdk.test.js la compara
//  contra ésta: si alguien cambia una aridad en un lado, la prueba
//  falla en el otro.
// #############################################################

// nombre -> { op, imm }
//   imm: null | 'i8' | 'i32' | 'u8' | 'u16' | 'rel16' | 'func' | 'sys'
const OPCODES = {
  'nop':        { op: 0x00, imm: null },
  'push.i8':    { op: 0x01, imm: 'i8' },
  'push.i32':   { op: 0x02, imm: 'i32' },
  'dup':        { op: 0x03, imm: null },
  'drop':       { op: 0x04, imm: null },
  'swap':       { op: 0x05, imm: null },
  'over':       { op: 0x06, imm: null },

  'ldlocal':    { op: 0x10, imm: 'u8' },
  'stlocal':    { op: 0x11, imm: 'u8' },
  'ldglobal':   { op: 0x12, imm: 'u16' },
  'stglobal':   { op: 0x13, imm: 'u16' },

  'ld8':        { op: 0x18, imm: null },
  'ld8u':       { op: 0x19, imm: null },
  'ld16':       { op: 0x1A, imm: null },
  'ld16u':      { op: 0x1B, imm: null },
  'ld32':       { op: 0x1C, imm: null },
  'st8':        { op: 0x1D, imm: null },
  'st16':       { op: 0x1E, imm: null },
  'st32':       { op: 0x1F, imm: null },

  'kld8u':      { op: 0x20, imm: null },
  'kld16u':     { op: 0x21, imm: null },
  'kld32':      { op: 0x22, imm: null },

  'add':        { op: 0x30, imm: null },
  'sub':        { op: 0x31, imm: null },
  'mul':        { op: 0x32, imm: null },
  'div':        { op: 0x33, imm: null },
  'mod':        { op: 0x34, imm: null },
  'neg':        { op: 0x35, imm: null },
  'and':        { op: 0x36, imm: null },
  'or':         { op: 0x37, imm: null },
  'xor':        { op: 0x38, imm: null },
  'not':        { op: 0x39, imm: null },
  'shl':        { op: 0x3A, imm: null },
  'shr':        { op: 0x3B, imm: null },
  'ushr':       { op: 0x3C, imm: null },
  'min':        { op: 0x3D, imm: null },
  'max':        { op: 0x3E, imm: null },
  'abs':        { op: 0x3F, imm: null },

  'eq':         { op: 0x40, imm: null },
  'ne':         { op: 0x41, imm: null },
  'lt':         { op: 0x42, imm: null },
  'le':         { op: 0x43, imm: null },
  'gt':         { op: 0x44, imm: null },
  'ge':         { op: 0x45, imm: null },

  'jmp':        { op: 0x50, imm: 'rel16' },
  'jz':         { op: 0x51, imm: 'rel16' },
  'jnz':        { op: 0x52, imm: 'rel16' },
  'call':       { op: 0x53, imm: 'func' },
  'ret':        { op: 0x54, imm: null },
  'retv':       { op: 0x55, imm: null },
  'halt':       { op: 0x56, imm: null },

  'sys':        { op: 0x60, imm: 'sys' },
  'yield':      { op: 0x70, imm: null },
  'trap':       { op: 0x7F, imm: 'u8' }
};

// nombre canónico -> { id, argc, perm }
// perm: '' o el nombre del permiso de sistema que exige.
const SYSCALLS = {
  'gfx.clear':         { id: 0x0001, argc: 1, perm: '' },
  'gfx.fill_rect':     { id: 0x0002, argc: 5, perm: '' },
  'gfx.rect':          { id: 0x0003, argc: 5, perm: '' },
  'gfx.line':          { id: 0x0004, argc: 5, perm: '' },
  'gfx.round_rect':    { id: 0x0005, argc: 6, perm: '' },
  'gfx.fill_rect_a':   { id: 0x0006, argc: 6, perm: '' },
  'gfx.text':          { id: 0x0007, argc: 6, perm: '' },
  'gfx.text_mem':      { id: 0x0008, argc: 6, perm: '' },
  'gfx.text_width':    { id: 0x0009, argc: 3, perm: '' },
  'gfx.clip':          { id: 0x000A, argc: 4, perm: '' },
  'gfx.clip_reset':    { id: 0x000B, argc: 0, perm: '' },
  'gfx.blit':          { id: 0x000C, argc: 5, perm: '' },
  'gfx.blit_scaled':   { id: 0x000D, argc: 7, perm: '' },
  'gfx.blit_rot':      { id: 0x000E, argc: 6, perm: '' },
  'gfx.blit_alpha':    { id: 0x000F, argc: 6, perm: '' },
  'gfx.width':         { id: 0x0010, argc: 0, perm: '' },
  'gfx.height':        { id: 0x0011, argc: 0, perm: '' },
  'gfx.color':         { id: 0x0012, argc: 3, perm: '' },
  'gfx.pixel':         { id: 0x0013, argc: 3, perm: '' },
  'gfx.present':       { id: 0x0014, argc: 0, perm: '' },

  'ui.card':           { id: 0x0020, argc: 4, perm: '' },
  'ui.title':          { id: 0x0021, argc: 4, perm: '' },
  'ui.label':          { id: 0x0022, argc: 4, perm: '' },
  'ui.caption':        { id: 0x0023, argc: 4, perm: '' },
  'ui.button':         { id: 0x0024, argc: 7, perm: '' },
  'ui.list_row':       { id: 0x0025, argc: 7, perm: '' },
  'ui.nav_title':      { id: 0x0026, argc: 2, perm: '' },

  'time.now_ms':       { id: 0x0030, argc: 0, perm: '' },
  'time.now_us_lo':    { id: 0x0031, argc: 0, perm: '' },
  'time.now_us_hi':    { id: 0x0032, argc: 0, perm: '' },
  'time.timer_set':    { id: 0x0033, argc: 3, perm: '' },
  'time.timer_clear':  { id: 0x0034, argc: 1, perm: '' },
  'time.budget_left':  { id: 0x0035, argc: 0, perm: '' },

  'input.points':      { id: 0x0038, argc: 0, perm: '' },
  'input.point_x':     { id: 0x0039, argc: 1, perm: '' },
  'input.point_y':     { id: 0x003A, argc: 1, perm: '' },
  'input.point_id':    { id: 0x003B, argc: 1, perm: '' },

  'store.write':       { id: 0x0040, argc: 4, perm: 'storage.app' },
  'store.read':        { id: 0x0041, argc: 4, perm: 'storage.app' },
  'store.size':        { id: 0x0042, argc: 2, perm: 'storage.app' },
  'store.delete':      { id: 0x0043, argc: 2, perm: 'storage.app' },
  'store.used':        { id: 0x0044, argc: 0, perm: 'storage.app' },
  'store.quota':       { id: 0x0045, argc: 0, perm: 'storage.app' },

  'sys.screen_w':      { id: 0x0050, argc: 0, perm: '' },
  'sys.screen_h':      { id: 0x0051, argc: 0, perm: '' },
  'sys.os_version':    { id: 0x0052, argc: 2, perm: '' },
  'sys.model':         { id: 0x0053, argc: 2, perm: '' },
  'sys.caps':          { id: 0x0054, argc: 0, perm: '' },
  'sys.log':           { id: 0x0055, argc: 2, perm: '' },
  'sys.notify':        { id: 0x0056, argc: 4, perm: '' },
  'sys.exit':          { id: 0x0057, argc: 0, perm: '' },

  'display.landscape':   { id: 0x0060, argc: 1, perm: 'display.landscape' },
  'display.exclusive':   { id: 0x0061, argc: 1, perm: 'display.exclusive' },
  'display.orientation': { id: 0x0062, argc: 0, perm: '' },

  'perf.metric':       { id: 0x0070, argc: 1, perm: 'system.performance.metrics' },
  'mem.psram_free':    { id: 0x0071, argc: 0, perm: 'system.psram.measure' },
  'mem.psram_total':   { id: 0x0072, argc: 0, perm: 'system.psram.measure' },
  'mem.psram_largest': { id: 0x0073, argc: 0, perm: 'system.psram.measure' },
  'mem.reserve':       { id: 0x0074, argc: 1, perm: 'system.psram.measure' },
  'mem.release':       { id: 0x0075, argc: 1, perm: 'system.psram.measure' },
  'mem.poke':          { id: 0x0076, argc: 3, perm: 'system.psram.measure' },
  'mem.peek':          { id: 0x0077, argc: 2, perm: 'system.psram.measure' },
  'temp.milli_c':      { id: 0x0078, argc: 0, perm: 'system.temperature.read' },
  'cpu.stress_begin':  { id: 0x0079, argc: 2, perm: 'system.cpu.stress' },
  'cpu.stress_slice':  { id: 0x007A, argc: 0, perm: 'system.cpu.stress' },
  'cpu.stress_lo':     { id: 0x007B, argc: 0, perm: 'system.cpu.stress' },
  'cpu.stress_hi':     { id: 0x007C, argc: 0, perm: 'system.cpu.stress' },
  'cpu.stress_stop':   { id: 0x007D, argc: 0, perm: 'system.cpu.stress' },
  'bench.begin':       { id: 0x007E, argc: 1, perm: 'benchmark.run' },
  'bench.end':         { id: 0x007F, argc: 0, perm: 'benchmark.run' }
};

// Permisos de sistema: nombre -> bit. Espejo de FlexOS_AppGrant.h.
const SYS_PERMISSIONS = {
  'benchmark.run':              1 << 0,
  'system.cpu.stress':          1 << 1,
  'system.psram.measure':       1 << 2,
  'system.temperature.read':    1 << 3,
  'system.performance.metrics': 1 << 4,
  'display.landscape':          1 << 5,
  'display.exclusive':          1 << 6,
  'storage.app':                1 << 7
};

// Permisos "de manifest" que ya existían para flex-ui-1 (FlexOS_Package.h).
const UI_PERMISSIONS = [
  'network', 'storage.read', 'storage.write', 'notifications',
  'camera', 'microphone', 'location', 'clipboard'
];

const HANDLERS = ['onStart', 'onEvent', 'onTick', 'onStop'];
const HANDLER_ARGS = { onStart: 0, onEvent: 3, onTick: 1, onStop: 1 };

const LIMITS = {
  MAX_CODE: 128 * 1024,
  MAX_CONST: 64 * 1024,
  MAX_MEM: 1024 * 1024,
  MAX_GLOBALS: 256,
  MAX_STACK: 1024,
  MAX_FRAMESLOTS: 1024,
  MAX_CALLDEPTH: 64,
  MAX_FUNCS: 512,
  MAX_ARGS: 8,
  HEADER_BYTES: 48,
  FUNC_BYTES: 8,
  NO_HANDLER: 0xFFFF
};

module.exports = { OPCODES, SYSCALLS, SYS_PERMISSIONS, UI_PERMISSIONS, HANDLERS, HANDLER_ARGS, LIMITS };
