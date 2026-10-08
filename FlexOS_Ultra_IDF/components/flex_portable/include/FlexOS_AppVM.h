#ifndef FLEXOS_APPVM_H
#define FLEXOS_APPVM_H
// #############################################################
//  FLEX OS · MAQUINA flex-app-v1  (FlexOS_AppVM.h/.cpp)
//  ------------------------------------------------------------
//  QUE ES. El intérprete aislado en el que corre la LOGICA de una app
//  descargable de Flex Store. No es un parser de JSON con otro nombre:
//  es una máquina de pila con memoria lineal acotada, validador
//  estático previo a la ejecución y comprobación de límites en cada
//  acceso.
//
//  LO QUE UNA APP **NO** PUEDE HACER, POR CONSTRUCCION:
//    · no hay punteros en el modelo de datos (todo es int32);
//    · no puede leer ni escribir fuera de SU memoria lineal;
//    · no ve el framebuffer, ni globals del firmware, ni el heap;
//    · no crea tareas, ni toca el watchdog, ni llama a delay();
//    · no salta a una dirección que no sea el principio de una
//      instrucción validada de SU propio código;
//    · no puede pasarse del presupuesto de instrucciones del tick:
//      el control vuelve SIEMPRE al sistema.
//
//  POR QUE VIVE FUERA DEL SKETCH. Es lógica PURA (sólo <stdint.h>,
//  <stddef.h> y <string.h>): no toca Arduino, ni el framebuffer, ni el
//  sistema de archivos, ni reserva un solo byte. Mismo criterio que
//  FlexOS_Mem.cpp o FlexOS_Media.cpp, y por el mismo motivo: así se
//  compila y se ejercita EN EL PC con AddressSanitizer y
//  UndefinedBehaviorSanitizer (tests/host/test_appvm.cpp).
//
//  CERO RESERVAS. La VM no llama a malloc jamás. Todas las regiones
//  (memoria lineal, pila, locales, marcos, globals) las entrega ya
//  reservadas quien la usa, una sola vez al arrancar la app.
//
//  El formato de bytecode está documentado en
//  FlexOS_Ultra/docs/FLEX_APP_V1_BYTECODE.md.
// #############################################################
#include <stdint.h>
#include <stddef.h>

// ---- Contenedor FLXB v1 -------------------------------------------------
#define FLEXVM_MAGIC0 'F'
#define FLEXVM_MAGIC1 'L'
#define FLEXVM_MAGIC2 'X'
#define FLEXVM_MAGIC3 'B'
#define FLEXVM_FORMAT_VERSION 1
#define FLEXVM_HEADER_BYTES   48
#define FLEXVM_FUNC_BYTES     8
#define FLEXVM_NO_HANDLER     0xFFFFu

// Techos ABSOLUTOS del formato. El manifest de la app puede pedir menos,
// nunca más: flexVmLoad rechaza cualquier imagen que los supere aunque el
// manifest la hubiera aceptado.
#define FLEXVM_MAX_CODE       (128u * 1024u)
#define FLEXVM_MAX_CONST      (64u * 1024u)
#define FLEXVM_MAX_MEM        (1024u * 1024u)
#define FLEXVM_MAX_GLOBALS    256u
#define FLEXVM_MAX_STACK      1024u
#define FLEXVM_MAX_FRAMESLOTS 1024u
#define FLEXVM_MAX_CALLDEPTH  64u
#define FLEXVM_MAX_FUNCS      512u
#define FLEXVM_MAX_ARGS       8u      // argumentos por syscall y por función

// ---- Resultado de una tanda de ejecución --------------------------------
enum FlexVmRun : uint8_t {
  FLEXVM_DONE = 0,     // el manejador terminó; su valor está en vm->result
  FLEXVM_YIELD,        // se agotó el presupuesto o la app cedió: reanudable
  FLEXVM_TRAP,         // error: vm->trap dice cuál. NO reanudable
  FLEXVM_IDLE          // no había ningún manejador en curso
};

enum FlexVmTrap : uint8_t {
  FLEXVM_TRAP_NONE = 0,
  FLEXVM_TRAP_OPCODE,      // opcode desconocido (no debería pasar el validador)
  FLEXVM_TRAP_STACK,       // desbordamiento o vaciado de la pila de operandos
  FLEXVM_TRAP_FRAME,       // profundidad de llamada o slots de locales agotados
  FLEXVM_TRAP_MEMORY,      // acceso fuera de la memoria lineal
  FLEXVM_TRAP_CONST,       // acceso fuera del pool de constantes
  FLEXVM_TRAP_GLOBAL,      // índice de global fuera de rango
  FLEXVM_TRAP_LOCAL,       // índice de local fuera del marco
  FLEXVM_TRAP_DIVZERO,     // división o módulo por cero
  FLEXVM_TRAP_PC,          // salto fuera de código o a mitad de instrucción
  FLEXVM_TRAP_FUNC,        // índice de función inválido
  FLEXVM_TRAP_SYSCALL,     // syscall desconocida o rechazada por el host
  FLEXVM_TRAP_ABORT,       // la app ejecutó TRAP a propósito
  FLEXVM_TRAP_HOST         // el host declaró un fallo irrecuperable
};

// ---- Códigos de validación ----------------------------------------------
enum FlexVmLoad : uint8_t {
  FLEXVM_LOAD_OK = 0,
  FLEXVM_LOAD_SIZE,        // imagen truncada o tamaño declarado incoherente
  FLEXVM_LOAD_MAGIC,       // no es FLXB v1
  FLEXVM_LOAD_LIMITS,      // pide más de lo permitido por el formato o el manifest
  FLEXVM_LOAD_FUNCTAB,     // tabla de funciones inválida
  FLEXVM_LOAD_HANDLER,     // manejador declarado que no existe o con aridad rara
  FLEXVM_LOAD_CODE,        // opcode desconocido, operando fuera de rango o salto inválido
  FLEXVM_LOAD_SCRATCH,     // el buffer de validación no da para el código
  FLEXVM_LOAD_REGIONS      // las regiones de trabajo no son las que pide la imagen
};

// ---- Manejadores ---------------------------------------------------------
enum FlexVmHandler : uint8_t {
  FLEXVM_H_START = 0,   // onStart()                       -> void
  FLEXVM_H_EVENT,       // onEvent(type, a, b)             -> void
  FLEXVM_H_TICK,        // onTick(msSinceStart)            -> void
  FLEXVM_H_STOP,        // onStop(reason)                  -> void
  FLEXVM_H_COUNT
};

// TODA función devuelve EXACTAMENTE un valor (RET empuja 0, RETV empuja el
// suyo). Así no existe el desajuste "el llamante esperaba valor y la función
// no lo dejó": para descartarlo se usa DROP, y la pila siempre cuadra.
struct FlexVmFrame {
  uint32_t retPc;
  uint16_t retSp;
  uint16_t localBase;
  uint16_t localCount;   // args + locales de este marco
};

// El host resuelve las syscalls. `argc` es SIEMPRE el que devolvió arity()
// para ese id, y los argumentos llegan en el orden en que la app los apiló.
// Devolver un valor: toda syscall empuja EXACTAMENTE un int32.
// Poner *fatal = true aborta la app con FLEXVM_TRAP_HOST.
typedef int32_t (*FlexVmSyscallFn)(void* user, uint16_t id, const int32_t* args,
                                   uint8_t argc, bool* fatal);
// Aridad de una syscall, o 0xFF si el host no la conoce. Se consulta al
// VALIDAR (no sólo al ejecutar): una app que llame a una syscall que este
// firmware no tiene no llega ni a cargarse.
typedef uint8_t (*FlexVmArityFn)(void* user, uint16_t id);

struct FlexVm {
  // ---- Imagen (sólo lectura; la memoria es del llamante) ----
  const uint8_t* code;      uint32_t codeLen;
  const uint8_t* kdata;     uint32_t kdataLen;
  const uint8_t* funcTab;   uint16_t funcCount;
  uint16_t handler[FLEXVM_H_COUNT];

  // ---- Regiones de trabajo (del llamante, reservadas una sola vez) ----
  uint8_t*  mem;       uint32_t memBytes;
  int32_t*  globals;   uint16_t globalCount;
  int32_t*  stack;     uint16_t stackSlots;
  int32_t*  locals;    uint16_t frameSlots;
  FlexVmFrame* frames; uint16_t callDepth;

  // ---- Entorno ----
  FlexVmSyscallFn syscall;
  FlexVmArityFn   arity;
  void*           user;

  // ---- Estado de ejecución ----
  uint32_t pc;
  uint16_t sp;            // slots ocupados de la pila de operandos
  uint16_t frameCount;
  uint16_t localTop;      // slots de locales en uso
  int32_t  result;        // valor devuelto por el último manejador
  uint64_t instrTotal;    // instrucciones ejecutadas desde flexVmReset
  uint8_t  trap;
  uint8_t  running;       // hay un manejador en curso
  uint8_t  yielded;       // la última salida fue YIELD voluntario (opcode YIELD)
};

// Deja la VM en un estado conocido. No toca las regiones.
void flexVmInit(FlexVm* vm);

// Valida una imagen FLXB v1 COMPLETA y rellena los campos de imagen de `vm`.
// `image` sigue siendo del llamante y debe vivir mientras la VM viva.
// `scratchBits` es un mapa de bits de inicios de instrucción: necesita
// (codeLen + 7) / 8 bytes. El validador no reserva nada.
// `maxMem`, `maxCode`, ... son los techos que impuso el manifest (0 = el techo
// del formato). Devuelve FLEXVM_LOAD_OK o el motivo exacto del rechazo.
FlexVmLoad flexVmValidate(FlexVm* vm, const uint8_t* image, uint32_t imageLen,
                          uint8_t* scratchBits, uint32_t scratchLen,
                          uint32_t maxCode, uint32_t maxConst, uint32_t maxMem,
                          FlexVmArityFn arity, void* user);

// Conecta las regiones de trabajo. Deben ser al menos las que pide la imagen
// (memBytes, globalCount, stackSlots, frameSlots, callDepth), leídas con los
// flexVmNeed*() de abajo tras validar. Pone la memoria lineal y los globals a
// cero: una app arranca SIEMPRE con estado limpio.
FlexVmLoad flexVmBind(FlexVm* vm, uint8_t* mem, uint32_t memBytes,
                      int32_t* globals, uint16_t globalCount,
                      int32_t* stack, uint16_t stackSlots,
                      int32_t* locals, uint16_t frameSlots,
                      FlexVmFrame* frames, uint16_t callDepth,
                      FlexVmSyscallFn syscall, void* user);

// Lo que la imagen validada pide. Válidos tras flexVmValidate.
uint32_t flexVmNeedMem(const FlexVm* vm);
uint16_t flexVmNeedGlobals(const FlexVm* vm);
uint16_t flexVmNeedStack(const FlexVm* vm);
uint16_t flexVmNeedFrameSlots(const FlexVm* vm);
uint16_t flexVmNeedCallDepth(const FlexVm* vm);

// true si la imagen declara ese manejador.
bool flexVmHasHandler(const FlexVm* vm, FlexVmHandler h);

// Prepara la llamada a un manejador. Falla si ya hay uno en curso, si el
// manejador no existe o si la aridad no coincide con la de la función.
bool flexVmCall(FlexVm* vm, FlexVmHandler h, const int32_t* args, uint8_t argc);

// Ejecuta como mucho `budget` instrucciones del manejador en curso.
// `used` (opcional) recibe cuántas se ejecutaron de verdad.
// El TIEMPO lo controla quien llama: se invoca en tandas cortas y entre tanda
// y tanda se mira el reloj. Así la VM no depende de ningún reloj y sigue
// siendo verificable en el PC.
FlexVmRun flexVmRun(FlexVm* vm, uint32_t budget, uint32_t* used);

// Aborta el manejador en curso sin ejecutar nada más. La app queda
// cancelada de forma limpia: pila, marcos y locales vuelven a cero.
void flexVmAbort(FlexVm* vm, FlexVmTrap reason);

// Borra TODO el estado de ejecución (pila, marcos, globals y memoria lineal)
// conservando la imagen: es lo que se hace entre dos lanzamientos de la misma
// app para que no se filtre nada de la sesión anterior.
void flexVmReset(FlexVm* vm);

const char* flexVmTrapText(uint8_t trap);
const char* flexVmLoadText(uint8_t code);

#endif
