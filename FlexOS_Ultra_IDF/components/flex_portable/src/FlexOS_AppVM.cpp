// #############################################################
//  FLEX OS · MAQUINA flex-app-v1  ·  implementación
//  ------------------------------------------------------------
//  Ver la cabecera FlexOS_AppVM.h para el contrato y
//  FlexOS_Ultra/docs/FLEX_APP_V1_BYTECODE.md para el formato.
//
//  DOS DEFENSAS, NO UNA:
//    1. VALIDACION ESTATICA antes de ejecutar una sola instrucción:
//       todo el código se decodifica linealmente, se apunta dónde
//       empieza cada instrucción y se comprueba que ningún salto cae
//       fuera ni a mitad de otra. Los índices que se pueden conocer
//       sin ejecutar (global, función, syscall) se comprueban aquí.
//    2. COMPROBACION EN EJECUCION de todo lo que depende del estado:
//       pila, marcos, locales, memoria lineal y pool de constantes.
//  La primera hace que el código sea decodificable; la segunda hace
//  que ningún valor calculado por la app pueda salirse de su caja.
// #############################################################
#include "FlexOS_AppVM.h"

#include <string.h>

#ifndef INT32_MIN
#define INT32_MIN (-2147483647 - 1)
#endif

namespace {

// ---- Opcodes (ver el documento de formato) ------------------------------
enum : uint8_t {
  OP_NOP = 0x00, OP_PUSH_I8 = 0x01, OP_PUSH_I32 = 0x02,
  OP_DUP = 0x03, OP_DROP = 0x04, OP_SWAP = 0x05, OP_OVER = 0x06,

  OP_LD_LOCAL = 0x10, OP_ST_LOCAL = 0x11,
  OP_LD_GLOBAL = 0x12, OP_ST_GLOBAL = 0x13,

  OP_LD8 = 0x18, OP_LD8U = 0x19, OP_LD16 = 0x1A, OP_LD16U = 0x1B, OP_LD32 = 0x1C,
  OP_ST8 = 0x1D, OP_ST16 = 0x1E, OP_ST32 = 0x1F,

  OP_KLD8U = 0x20, OP_KLD16U = 0x21, OP_KLD32 = 0x22,

  OP_ADD = 0x30, OP_SUB = 0x31, OP_MUL = 0x32, OP_DIV = 0x33, OP_MOD = 0x34,
  OP_NEG = 0x35, OP_AND = 0x36, OP_OR = 0x37, OP_XOR = 0x38, OP_NOT = 0x39,
  OP_SHL = 0x3A, OP_SHR = 0x3B, OP_USHR = 0x3C,
  OP_MIN = 0x3D, OP_MAX = 0x3E, OP_ABS = 0x3F,

  OP_EQ = 0x40, OP_NE = 0x41, OP_LT = 0x42, OP_LE = 0x43, OP_GT = 0x44, OP_GE = 0x45,

  OP_JMP = 0x50, OP_JZ = 0x51, OP_JNZ = 0x52, OP_CALL = 0x53,
  OP_RET = 0x54, OP_RETV = 0x55, OP_HALT = 0x56,

  OP_SYS = 0x60,
  OP_YIELD = 0x70, OP_TRAP = 0x7F
};

// Aridad declarada de cada manejador (tiene que coincidir con la función).
const uint8_t kHandlerArgs[FLEXVM_H_COUNT] = { 0, 3, 1, 1 };

inline uint16_t rd16(const uint8_t* p){ return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
inline uint32_t rd32(const uint8_t* p){
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// Longitud total de la instrucción que empieza en `op`, o 0 si el opcode no
// existe. Es la ÚNICA tabla de tamaños: el validador y el intérprete la usan
// los dos, así que no pueden discrepar.
uint8_t opLength(uint8_t op){
  switch(op){
    case OP_PUSH_I8: case OP_LD_LOCAL: case OP_ST_LOCAL: case OP_TRAP:
      return 2;
    case OP_LD_GLOBAL: case OP_ST_GLOBAL: case OP_JMP: case OP_JZ: case OP_JNZ:
    case OP_CALL: case OP_SYS:
      return 3;
    case OP_PUSH_I32:
      return 5;
    case OP_NOP: case OP_DUP: case OP_DROP: case OP_SWAP: case OP_OVER:
    case OP_LD8: case OP_LD8U: case OP_LD16: case OP_LD16U: case OP_LD32:
    case OP_ST8: case OP_ST16: case OP_ST32:
    case OP_KLD8U: case OP_KLD16U: case OP_KLD32:
    case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV: case OP_MOD: case OP_NEG:
    case OP_AND: case OP_OR: case OP_XOR: case OP_NOT:
    case OP_SHL: case OP_SHR: case OP_USHR:
    case OP_MIN: case OP_MAX: case OP_ABS:
    case OP_EQ: case OP_NE: case OP_LT: case OP_LE: case OP_GT: case OP_GE:
    case OP_RET: case OP_RETV: case OP_HALT: case OP_YIELD:
      return 1;
    default:
      return 0;
  }
}

inline void markBit(uint8_t* bits, uint32_t i){ bits[i >> 3] |= (uint8_t)(1u << (i & 7)); }
inline bool testBit(const uint8_t* bits, uint32_t i){ return (bits[i >> 3] >> (i & 7)) & 1u; }

// Multiplicación y suma con desbordamiento definido: la app puede llegar a
// cualquier valor, y en C++ el desbordamiento de int32 con signo es UB. Se
// hace en uint32_t y se reinterpreta, que es lo que documenta el formato.
inline int32_t wrapAdd(int32_t a, int32_t b){ return (int32_t)((uint32_t)a + (uint32_t)b); }
inline int32_t wrapSub(int32_t a, int32_t b){ return (int32_t)((uint32_t)a - (uint32_t)b); }
inline int32_t wrapMul(int32_t a, int32_t b){ return (int32_t)((uint32_t)a * (uint32_t)b); }
inline int32_t wrapNeg(int32_t a){ return (int32_t)(0u - (uint32_t)a); }

} // namespace

// -------------------------------------------------------------------------
void flexVmInit(FlexVm* vm){
  if(!vm) return;
  memset(vm, 0, sizeof(*vm));
  for(int i = 0; i < FLEXVM_H_COUNT; i++) vm->handler[i] = FLEXVM_NO_HANDLER;
}

// -------------------------------------------------------------------------
//  VALIDACION
// -------------------------------------------------------------------------
FlexVmLoad flexVmValidate(FlexVm* vm, const uint8_t* image, uint32_t imageLen,
                          uint8_t* scratchBits, uint32_t scratchLen,
                          uint32_t maxCode, uint32_t maxConst, uint32_t maxMem,
                          FlexVmArityFn arity, void* user){
  if(!vm || !image || !arity) return FLEXVM_LOAD_SIZE;
  flexVmInit(vm);
  if(imageLen < FLEXVM_HEADER_BYTES) return FLEXVM_LOAD_SIZE;
  if(image[0] != FLEXVM_MAGIC0 || image[1] != FLEXVM_MAGIC1 ||
     image[2] != FLEXVM_MAGIC2 || image[3] != FLEXVM_MAGIC3) return FLEXVM_LOAD_MAGIC;
  if(rd16(image + 4) != FLEXVM_FORMAT_VERSION || rd16(image + 6) != 0) return FLEXVM_LOAD_MAGIC;

  uint32_t codeLen  = rd32(image + 8);
  uint32_t constLen = rd32(image + 12);
  uint32_t memBytes = rd32(image + 16);
  uint16_t globals  = rd16(image + 20);
  uint16_t stackSl  = rd16(image + 22);
  uint16_t frameSl  = rd16(image + 24);
  uint16_t callD    = rd16(image + 26);
  uint16_t funcN    = rd16(image + 28);
  if(rd16(image + 30) != 0 || rd32(image + 40) != 0 || rd32(image + 44) != 0)
    return FLEXVM_LOAD_MAGIC;

  // Techos: primero los del FORMATO, después los que impuso el manifest.
  uint32_t capCode  = maxCode  ? (maxCode  < FLEXVM_MAX_CODE  ? maxCode  : FLEXVM_MAX_CODE)  : FLEXVM_MAX_CODE;
  uint32_t capConst = maxConst ? (maxConst < FLEXVM_MAX_CONST ? maxConst : FLEXVM_MAX_CONST) : FLEXVM_MAX_CONST;
  uint32_t capMem   = maxMem   ? (maxMem   < FLEXVM_MAX_MEM   ? maxMem   : FLEXVM_MAX_MEM)   : FLEXVM_MAX_MEM;
  if(codeLen == 0 || codeLen > capCode) return FLEXVM_LOAD_LIMITS;
  if(constLen > capConst) return FLEXVM_LOAD_LIMITS;
  if(memBytes > capMem || (memBytes & 3u)) return FLEXVM_LOAD_LIMITS;
  if(globals > FLEXVM_MAX_GLOBALS) return FLEXVM_LOAD_LIMITS;
  if(stackSl == 0 || stackSl > FLEXVM_MAX_STACK) return FLEXVM_LOAD_LIMITS;
  if(frameSl > FLEXVM_MAX_FRAMESLOTS) return FLEXVM_LOAD_LIMITS;
  if(callD == 0 || callD > FLEXVM_MAX_CALLDEPTH) return FLEXVM_LOAD_LIMITS;
  if(funcN == 0 || funcN > FLEXVM_MAX_FUNCS) return FLEXVM_LOAD_LIMITS;

  // Tamaño declarado == tamaño real, calculado en 64 bits para que no pueda
  // dar la vuelta.
  uint64_t need = (uint64_t)FLEXVM_HEADER_BYTES + (uint64_t)funcN * FLEXVM_FUNC_BYTES +
                  (uint64_t)constLen + (uint64_t)codeLen;
  if(need != (uint64_t)imageLen) return FLEXVM_LOAD_SIZE;

  const uint8_t* funcTab = image + FLEXVM_HEADER_BYTES;
  const uint8_t* kdata   = funcTab + (uint32_t)funcN * FLEXVM_FUNC_BYTES;
  const uint8_t* code    = kdata + constLen;

  uint32_t bitsNeeded = (codeLen + 7u) / 8u;
  if(!scratchBits || scratchLen < bitsNeeded) return FLEXVM_LOAD_SCRATCH;
  memset(scratchBits, 0, bitsNeeded);

  // --- Pasada 1: decodificación lineal ---------------------------------
  // El código es un flujo puro de instrucciones: no hay datos intercalados
  // (para eso está el pool de constantes). Si una instrucción se sale del
  // final, o queda un byte sin cubrir, la imagen se rechaza.
  uint32_t at = 0;
  while(at < codeLen){
    uint8_t op = code[at];
    uint8_t len = opLength(op);
    if(len == 0) return FLEXVM_LOAD_CODE;
    if((uint64_t)at + len > (uint64_t)codeLen) return FLEXVM_LOAD_CODE;
    markBit(scratchBits, at);
    // Operandos comprobables sin ejecutar.
    if(op == OP_LD_GLOBAL || op == OP_ST_GLOBAL){
      if(rd16(code + at + 1) >= globals) return FLEXVM_LOAD_CODE;
    } else if(op == OP_CALL){
      if(rd16(code + at + 1) >= funcN) return FLEXVM_LOAD_CODE;
    } else if(op == OP_SYS){
      uint8_t a = arity(user, rd16(code + at + 1));
      if(a == 0xFF || a > FLEXVM_MAX_ARGS) return FLEXVM_LOAD_CODE;
    }
    at += len;
  }
  if(at != codeLen) return FLEXVM_LOAD_CODE;

  // --- Pasada 2: destinos de salto --------------------------------------
  at = 0;
  while(at < codeLen){
    uint8_t op = code[at];
    uint8_t len = opLength(op);
    if(op == OP_JMP || op == OP_JZ || op == OP_JNZ){
      int32_t rel = (int32_t)(int16_t)rd16(code + at + 1);
      int64_t tgt = (int64_t)at + len + rel;
      if(tgt < 0 || tgt >= (int64_t)codeLen) return FLEXVM_LOAD_CODE;
      if(!testBit(scratchBits, (uint32_t)tgt)) return FLEXVM_LOAD_CODE;
    }
    at += len;
  }

  // --- Tabla de funciones ------------------------------------------------
  for(uint16_t i = 0; i < funcN; i++){
    const uint8_t* e = funcTab + (uint32_t)i * FLEXVM_FUNC_BYTES;
    uint32_t off = rd32(e);
    uint8_t  argc = e[4], locn = e[5];
    if(rd16(e + 6) != 0) return FLEXVM_LOAD_FUNCTAB;
    if(off >= codeLen || !testBit(scratchBits, off)) return FLEXVM_LOAD_FUNCTAB;
    if(argc > FLEXVM_MAX_ARGS) return FLEXVM_LOAD_FUNCTAB;
    if((uint32_t)argc + locn > frameSl) return FLEXVM_LOAD_FUNCTAB;
  }

  // --- Manejadores -------------------------------------------------------
  bool any = false;
  for(int h = 0; h < FLEXVM_H_COUNT; h++){
    uint16_t idx = rd16(image + 32 + h * 2);
    if(idx == FLEXVM_NO_HANDLER){ vm->handler[h] = FLEXVM_NO_HANDLER; continue; }
    if(idx >= funcN) return FLEXVM_LOAD_HANDLER;
    if(funcTab[(uint32_t)idx * FLEXVM_FUNC_BYTES + 4] != kHandlerArgs[h]) return FLEXVM_LOAD_HANDLER;
    vm->handler[h] = idx;
    any = true;
  }
  // Una app que no declara ni un manejador no es una app: no podría hacer
  // nada nunca, y aceptarla sólo sirve para ocupar una ranura instalada.
  if(!any) return FLEXVM_LOAD_HANDLER;

  vm->code = code;       vm->codeLen = codeLen;
  vm->kdata = kdata;     vm->kdataLen = constLen;
  vm->funcTab = funcTab; vm->funcCount = funcN;
  vm->memBytes = memBytes;
  vm->globalCount = globals;
  vm->stackSlots = stackSl;
  vm->frameSlots = frameSl;
  vm->callDepth = callD;
  vm->arity = arity;
  vm->user = user;
  return FLEXVM_LOAD_OK;
}

uint32_t flexVmNeedMem(const FlexVm* vm){ return vm ? vm->memBytes : 0; }
uint16_t flexVmNeedGlobals(const FlexVm* vm){ return vm ? vm->globalCount : 0; }
uint16_t flexVmNeedStack(const FlexVm* vm){ return vm ? vm->stackSlots : 0; }
uint16_t flexVmNeedFrameSlots(const FlexVm* vm){ return vm ? vm->frameSlots : 0; }
uint16_t flexVmNeedCallDepth(const FlexVm* vm){ return vm ? vm->callDepth : 0; }

bool flexVmHasHandler(const FlexVm* vm, FlexVmHandler h){
  return vm && h < FLEXVM_H_COUNT && vm->handler[h] != FLEXVM_NO_HANDLER;
}

FlexVmLoad flexVmBind(FlexVm* vm, uint8_t* mem, uint32_t memBytes,
                      int32_t* globals, uint16_t globalCount,
                      int32_t* stack, uint16_t stackSlots,
                      int32_t* locals, uint16_t frameSlots,
                      FlexVmFrame* frames, uint16_t callDepth,
                      FlexVmSyscallFn syscall, void* user){
  if(!vm || !vm->code || !syscall) return FLEXVM_LOAD_REGIONS;
  if(memBytes < vm->memBytes || globalCount < vm->globalCount ||
     stackSlots < vm->stackSlots || frameSlots < vm->frameSlots ||
     callDepth < vm->callDepth) return FLEXVM_LOAD_REGIONS;
  if((vm->memBytes && !mem) || (vm->globalCount && !globals) || !stack ||
     (vm->frameSlots && !locals) || !frames) return FLEXVM_LOAD_REGIONS;
  vm->mem = mem;
  vm->globals = globals;
  vm->stack = stack;
  vm->locals = locals;
  vm->frames = frames;
  vm->syscall = syscall;
  vm->user = user;
  flexVmReset(vm);
  return FLEXVM_LOAD_OK;
}

void flexVmReset(FlexVm* vm){
  if(!vm) return;
  if(vm->mem && vm->memBytes) memset(vm->mem, 0, vm->memBytes);
  if(vm->globals && vm->globalCount) memset(vm->globals, 0, (size_t)vm->globalCount * sizeof(int32_t));
  vm->pc = 0; vm->sp = 0; vm->frameCount = 0; vm->localTop = 0;
  vm->result = 0; vm->instrTotal = 0;
  vm->trap = FLEXVM_TRAP_NONE; vm->running = 0; vm->yielded = 0;
}

void flexVmAbort(FlexVm* vm, FlexVmTrap reason){
  if(!vm) return;
  vm->running = 0; vm->yielded = 0;
  vm->sp = 0; vm->frameCount = 0; vm->localTop = 0; vm->pc = 0;
  vm->trap = (uint8_t)reason;
}

bool flexVmCall(FlexVm* vm, FlexVmHandler h, const int32_t* args, uint8_t argc){
  if(!vm || !vm->code || !vm->stack || vm->running) return false;
  if(h >= FLEXVM_H_COUNT || vm->handler[h] == FLEXVM_NO_HANDLER) return false;
  if(argc != kHandlerArgs[h] || (argc && !args)) return false;
  uint16_t fi = vm->handler[h];
  const uint8_t* e = vm->funcTab + (uint32_t)fi * FLEXVM_FUNC_BYTES;
  uint32_t off = rd32(e);
  uint8_t fargs = e[4], flocals = e[5];
  if(fargs != argc) return false;
  if((uint32_t)fargs + flocals > vm->frameSlots) return false;
  if(vm->callDepth == 0) return false;

  vm->sp = 0; vm->frameCount = 0; vm->localTop = 0;
  vm->trap = FLEXVM_TRAP_NONE; vm->result = 0; vm->yielded = 0;

  FlexVmFrame& fr = vm->frames[0];
  fr.retPc = 0; fr.retSp = 0; fr.localBase = 0;
  fr.localCount = (uint16_t)fargs + flocals;
  for(uint8_t i = 0; i < fargs; i++) vm->locals[i] = args[i];
  for(uint16_t i = fargs; i < fr.localCount; i++) vm->locals[i] = 0;
  vm->localTop = fr.localCount;
  vm->frameCount = 1;
  vm->pc = off;
  vm->running = 1;
  return true;
}

// -------------------------------------------------------------------------
//  INTERPRETE
// -------------------------------------------------------------------------
FlexVmRun flexVmRun(FlexVm* vm, uint32_t budget, uint32_t* used){
  if(used) *used = 0;
  if(!vm || !vm->running) return FLEXVM_IDLE;
  if(!vm->code || !vm->stack || !vm->frames || !vm->syscall){
    flexVmAbort(vm, FLEXVM_TRAP_HOST);
    return FLEXVM_TRAP;
  }

  const uint8_t* code = vm->code;
  const uint32_t codeLen = vm->codeLen;
  int32_t* stack = vm->stack;
  uint32_t pc = vm->pc;
  uint16_t sp = vm->sp;
  uint32_t n = 0;
  uint8_t trap = FLEXVM_TRAP_NONE;
  FlexVmRun outcome = FLEXVM_YIELD;

  vm->yielded = 0;

  #define VM_FAIL(t) do { trap = (t); goto trapped; } while(0)
  #define VM_POP(dst) do { if(sp == 0) VM_FAIL(FLEXVM_TRAP_STACK); dst = stack[--sp]; } while(0)
  #define VM_PUSH(v)  do { if(sp >= vm->stackSlots) VM_FAIL(FLEXVM_TRAP_STACK); stack[sp++] = (v); } while(0)

  while(n < budget){
    if(pc >= codeLen) VM_FAIL(FLEXVM_TRAP_PC);
    uint8_t op = code[pc];
    uint8_t len = opLength(op);
    if(len == 0 || (uint64_t)pc + len > (uint64_t)codeLen) VM_FAIL(FLEXVM_TRAP_OPCODE);
    const uint8_t* imm = code + pc + 1;
    uint32_t next = pc + len;
    n++;

    switch(op){
      case OP_NOP: break;
      case OP_PUSH_I8:  VM_PUSH((int32_t)(int8_t)imm[0]); break;
      case OP_PUSH_I32: VM_PUSH((int32_t)rd32(imm)); break;
      case OP_DUP: { if(sp == 0) VM_FAIL(FLEXVM_TRAP_STACK); int32_t v = stack[sp - 1]; VM_PUSH(v); break; }
      case OP_DROP: { int32_t t; VM_POP(t); (void)t; break; }
      case OP_SWAP: { if(sp < 2) VM_FAIL(FLEXVM_TRAP_STACK);
                      int32_t t = stack[sp - 1]; stack[sp - 1] = stack[sp - 2]; stack[sp - 2] = t; break; }
      case OP_OVER: { if(sp < 2) VM_FAIL(FLEXVM_TRAP_STACK); int32_t v = stack[sp - 2]; VM_PUSH(v); break; }

      case OP_LD_LOCAL: {
        const FlexVmFrame& fr = vm->frames[vm->frameCount - 1];
        if(imm[0] >= fr.localCount) VM_FAIL(FLEXVM_TRAP_LOCAL);
        VM_PUSH(vm->locals[fr.localBase + imm[0]]);
        break;
      }
      case OP_ST_LOCAL: {
        const FlexVmFrame& fr = vm->frames[vm->frameCount - 1];
        if(imm[0] >= fr.localCount) VM_FAIL(FLEXVM_TRAP_LOCAL);
        int32_t v; VM_POP(v);
        vm->locals[fr.localBase + imm[0]] = v;
        break;
      }
      case OP_LD_GLOBAL: {
        uint16_t g = rd16(imm);
        if(g >= vm->globalCount) VM_FAIL(FLEXVM_TRAP_GLOBAL);
        VM_PUSH(vm->globals[g]);
        break;
      }
      case OP_ST_GLOBAL: {
        uint16_t g = rd16(imm);
        if(g >= vm->globalCount) VM_FAIL(FLEXVM_TRAP_GLOBAL);
        int32_t v; VM_POP(v);
        vm->globals[g] = v;
        break;
      }

      // Memoria lineal. Toda dirección es un int32 de la app; se comprueba
      // en aritmética de 64 bits sin signo para que ni un valor negativo ni
      // uno enorme puedan dar la vuelta.
      case OP_LD8: case OP_LD8U: case OP_LD16: case OP_LD16U: case OP_LD32: {
        int32_t a; VM_POP(a);
        uint32_t width = (op == OP_LD8 || op == OP_LD8U) ? 1u : (op == OP_LD32 ? 4u : 2u);
        if(a < 0 || (uint64_t)(uint32_t)a + width > (uint64_t)vm->memBytes) VM_FAIL(FLEXVM_TRAP_MEMORY);
        const uint8_t* p = vm->mem + (uint32_t)a;
        int32_t v = 0;
        if(width == 1) v = (op == OP_LD8) ? (int32_t)(int8_t)p[0] : (int32_t)p[0];
        else if(width == 2){ uint16_t raw = rd16(p); v = (op == OP_LD16) ? (int32_t)(int16_t)raw : (int32_t)raw; }
        else v = (int32_t)rd32(p);
        VM_PUSH(v);
        break;
      }
      case OP_ST8: case OP_ST16: case OP_ST32: {
        int32_t v, a; VM_POP(v); VM_POP(a);
        uint32_t width = (op == OP_ST8) ? 1u : (op == OP_ST32 ? 4u : 2u);
        if(a < 0 || (uint64_t)(uint32_t)a + width > (uint64_t)vm->memBytes) VM_FAIL(FLEXVM_TRAP_MEMORY);
        uint8_t* p = vm->mem + (uint32_t)a;
        uint32_t u = (uint32_t)v;
        p[0] = (uint8_t)(u & 0xFF);
        if(width >= 2) p[1] = (uint8_t)((u >> 8) & 0xFF);
        if(width == 4){ p[2] = (uint8_t)((u >> 16) & 0xFF); p[3] = (uint8_t)((u >> 24) & 0xFF); }
        break;
      }
      case OP_KLD8U: case OP_KLD16U: case OP_KLD32: {
        int32_t a; VM_POP(a);
        uint32_t width = (op == OP_KLD8U) ? 1u : (op == OP_KLD32 ? 4u : 2u);
        if(a < 0 || (uint64_t)(uint32_t)a + width > (uint64_t)vm->kdataLen) VM_FAIL(FLEXVM_TRAP_CONST);
        const uint8_t* p = vm->kdata + (uint32_t)a;
        int32_t v = (width == 1) ? (int32_t)p[0] : (width == 2 ? (int32_t)rd16(p) : (int32_t)rd32(p));
        VM_PUSH(v);
        break;
      }

      case OP_ADD: { int32_t b, a; VM_POP(b); VM_POP(a); VM_PUSH(wrapAdd(a, b)); break; }
      case OP_SUB: { int32_t b, a; VM_POP(b); VM_POP(a); VM_PUSH(wrapSub(a, b)); break; }
      case OP_MUL: { int32_t b, a; VM_POP(b); VM_POP(a); VM_PUSH(wrapMul(a, b)); break; }
      case OP_DIV: { int32_t b, a; VM_POP(b); VM_POP(a);
                     if(b == 0) VM_FAIL(FLEXVM_TRAP_DIVZERO);
                     // INT32_MIN / -1 desborda: se define como INT32_MIN.
                     VM_PUSH((a == INT32_MIN && b == -1) ? INT32_MIN : a / b); break; }
      case OP_MOD: { int32_t b, a; VM_POP(b); VM_POP(a);
                     if(b == 0) VM_FAIL(FLEXVM_TRAP_DIVZERO);
                     VM_PUSH((a == INT32_MIN && b == -1) ? 0 : a % b); break; }
      case OP_NEG: { int32_t a; VM_POP(a); VM_PUSH(wrapNeg(a)); break; }
      case OP_AND: { int32_t b, a; VM_POP(b); VM_POP(a); VM_PUSH(a & b); break; }
      case OP_OR:  { int32_t b, a; VM_POP(b); VM_POP(a); VM_PUSH(a | b); break; }
      case OP_XOR: { int32_t b, a; VM_POP(b); VM_POP(a); VM_PUSH(a ^ b); break; }
      case OP_NOT: { int32_t a; VM_POP(a); VM_PUSH(~a); break; }
      // Los desplazamientos toman la cuenta módulo 32 y se hacen sin signo:
      // así ni un valor negativo ni uno >= 32 son comportamiento indefinido.
      case OP_SHL: { int32_t b, a; VM_POP(b); VM_POP(a); VM_PUSH((int32_t)((uint32_t)a << (b & 31))); break; }
      case OP_SHR: { int32_t b, a; VM_POP(b); VM_POP(a); int s = b & 31;
                     VM_PUSH(a < 0 ? (int32_t)~((~(uint32_t)a) >> s) : (int32_t)((uint32_t)a >> s)); break; }
      case OP_USHR:{ int32_t b, a; VM_POP(b); VM_POP(a); VM_PUSH((int32_t)((uint32_t)a >> (b & 31))); break; }
      case OP_MIN: { int32_t b, a; VM_POP(b); VM_POP(a); VM_PUSH(a < b ? a : b); break; }
      case OP_MAX: { int32_t b, a; VM_POP(b); VM_POP(a); VM_PUSH(a > b ? a : b); break; }
      case OP_ABS: { int32_t a; VM_POP(a); VM_PUSH(a < 0 ? wrapNeg(a) : a); break; }

      case OP_EQ: { int32_t b, a; VM_POP(b); VM_POP(a); VM_PUSH(a == b ? 1 : 0); break; }
      case OP_NE: { int32_t b, a; VM_POP(b); VM_POP(a); VM_PUSH(a != b ? 1 : 0); break; }
      case OP_LT: { int32_t b, a; VM_POP(b); VM_POP(a); VM_PUSH(a <  b ? 1 : 0); break; }
      case OP_LE: { int32_t b, a; VM_POP(b); VM_POP(a); VM_PUSH(a <= b ? 1 : 0); break; }
      case OP_GT: { int32_t b, a; VM_POP(b); VM_POP(a); VM_PUSH(a >  b ? 1 : 0); break; }
      case OP_GE: { int32_t b, a; VM_POP(b); VM_POP(a); VM_PUSH(a >= b ? 1 : 0); break; }

      case OP_JMP: case OP_JZ: case OP_JNZ: {
        bool take = true;
        if(op != OP_JMP){ int32_t c; VM_POP(c); take = (op == OP_JZ) ? (c == 0) : (c != 0); }
        if(take){
          int64_t tgt = (int64_t)next + (int32_t)(int16_t)rd16(imm);
          if(tgt < 0 || tgt >= (int64_t)codeLen) VM_FAIL(FLEXVM_TRAP_PC);
          next = (uint32_t)tgt;
        }
        break;
      }
      case OP_CALL: {
        uint16_t fi = rd16(imm);
        if(fi >= vm->funcCount) VM_FAIL(FLEXVM_TRAP_FUNC);
        const uint8_t* e = vm->funcTab + (uint32_t)fi * FLEXVM_FUNC_BYTES;
        uint32_t off = rd32(e);
        uint8_t fargs = e[4], flocals = e[5];
        if(off >= codeLen) VM_FAIL(FLEXVM_TRAP_PC);
        if(vm->frameCount >= vm->callDepth) VM_FAIL(FLEXVM_TRAP_FRAME);
        uint32_t slots = (uint32_t)fargs + flocals;
        if((uint32_t)vm->localTop + slots > vm->frameSlots) VM_FAIL(FLEXVM_TRAP_FRAME);
        if(sp < fargs) VM_FAIL(FLEXVM_TRAP_STACK);
        FlexVmFrame& fr = vm->frames[vm->frameCount];
        fr.retPc = next; fr.retSp = (uint16_t)(sp - fargs);
        fr.localBase = vm->localTop; fr.localCount = (uint16_t)slots;
        // Los argumentos se apilaron de izquierda a derecha: el más profundo
        // es el primero.
        for(uint8_t i = 0; i < fargs; i++) vm->locals[vm->localTop + i] = stack[sp - fargs + i];
        for(uint32_t i = fargs; i < slots; i++) vm->locals[vm->localTop + i] = 0;
        sp = fr.retSp;
        vm->localTop = (uint16_t)(vm->localTop + slots);
        vm->frameCount++;
        next = off;
        break;
      }
      case OP_RET: case OP_RETV: {
        int32_t v = 0;
        if(op == OP_RETV) VM_POP(v);
        if(vm->frameCount == 0) VM_FAIL(FLEXVM_TRAP_FRAME);
        FlexVmFrame fr = vm->frames[--vm->frameCount];
        vm->localTop = fr.localBase;
        sp = fr.retSp;
        if(vm->frameCount == 0){
          vm->result = v; vm->running = 0;
          pc = fr.retPc; outcome = FLEXVM_DONE; goto finish;
        }
        if(sp >= vm->stackSlots) VM_FAIL(FLEXVM_TRAP_STACK);
        stack[sp++] = v;
        next = fr.retPc;
        break;
      }
      case OP_HALT: {
        vm->result = 0; vm->running = 0;
        vm->sp = 0; vm->frameCount = 0; vm->localTop = 0;
        pc = next; sp = 0;
        outcome = FLEXVM_DONE; goto finish;
      }

      case OP_SYS: {
        uint16_t id = rd16(imm);
        uint8_t a = vm->arity ? vm->arity(vm->user, id) : 0xFF;
        if(a == 0xFF || a > FLEXVM_MAX_ARGS) VM_FAIL(FLEXVM_TRAP_SYSCALL);
        if(sp < a) VM_FAIL(FLEXVM_TRAP_STACK);
        int32_t argv[FLEXVM_MAX_ARGS];
        for(uint8_t i = 0; i < a; i++) argv[i] = stack[sp - a + i];
        sp = (uint16_t)(sp - a);
        bool fatal = false;
        // La syscall puede llamar de vuelta a estado del host, pero NUNCA a
        // esta VM: el contrato del host lo prohíbe y por eso pc/sp se pueden
        // conservar en registros locales sin miedo a una reentrada.
        int32_t r = vm->syscall(vm->user, id, argv, a, &fatal);
        if(fatal) VM_FAIL(FLEXVM_TRAP_HOST);
        VM_PUSH(r);
        break;
      }

      case OP_YIELD: {
        vm->yielded = 1;
        pc = next; outcome = FLEXVM_YIELD; goto finish;
      }
      case OP_TRAP: {
        vm->result = (int32_t)imm[0];
        trap = FLEXVM_TRAP_ABORT;
        goto trapped;
      }
      default:
        VM_FAIL(FLEXVM_TRAP_OPCODE);
    }
    pc = next;
  }

  // Presupuesto agotado: NO es un error. Se guarda el punto exacto y el tick
  // siguiente continúa por donde iba.
  outcome = FLEXVM_YIELD;

finish:
  vm->pc = pc; vm->sp = sp;
  if(used) *used = n;
  vm->instrTotal += n;
  return outcome;

trapped:
  if(used) *used = n;
  vm->instrTotal += n;
  vm->pc = pc; vm->sp = sp;
  {
    int32_t abortCode = vm->result;
    flexVmAbort(vm, (FlexVmTrap)trap);
    if(trap == FLEXVM_TRAP_ABORT) vm->result = abortCode;
  }
  return FLEXVM_TRAP;

  #undef VM_FAIL
  #undef VM_POP
  #undef VM_PUSH
}

// -------------------------------------------------------------------------
const char* flexVmTrapText(uint8_t trap){
  switch(trap){
    case FLEXVM_TRAP_NONE:    return "Sin error";
    case FLEXVM_TRAP_OPCODE:  return "Instruccion desconocida";
    case FLEXVM_TRAP_STACK:   return "Pila de la app desbordada";
    case FLEXVM_TRAP_FRAME:   return "Demasiadas llamadas anidadas";
    case FLEXVM_TRAP_MEMORY:  return "Acceso fuera de la memoria de la app";
    case FLEXVM_TRAP_CONST:   return "Acceso fuera de las constantes";
    case FLEXVM_TRAP_GLOBAL:  return "Variable global invalida";
    case FLEXVM_TRAP_LOCAL:   return "Variable local invalida";
    case FLEXVM_TRAP_DIVZERO: return "Division por cero";
    case FLEXVM_TRAP_PC:      return "Salto invalido";
    case FLEXVM_TRAP_FUNC:    return "Funcion invalida";
    case FLEXVM_TRAP_SYSCALL: return "Llamada al sistema no permitida";
    case FLEXVM_TRAP_ABORT:   return "La app se detuvo por su cuenta";
    case FLEXVM_TRAP_HOST:    return "Fallo del sistema al atender la app";
    default:                  return "Error desconocido";
  }
}

const char* flexVmLoadText(uint8_t code){
  switch(code){
    case FLEXVM_LOAD_OK:      return "Correcto";
    case FLEXVM_LOAD_SIZE:    return "Bytecode truncado o de tamano incoherente";
    case FLEXVM_LOAD_MAGIC:   return "No es un bytecode flex-app-v1";
    case FLEXVM_LOAD_LIMITS:  return "El bytecode pide mas de lo permitido";
    case FLEXVM_LOAD_FUNCTAB: return "Tabla de funciones invalida";
    case FLEXVM_LOAD_HANDLER: return "Manejador declarado invalido";
    case FLEXVM_LOAD_CODE:    return "Codigo invalido o salto fuera de rango";
    case FLEXVM_LOAD_SCRATCH: return "Sin memoria para validar el bytecode";
    case FLEXVM_LOAD_REGIONS: return "Regiones de trabajo insuficientes";
    default:                  return "Bytecode rechazado";
  }
}
