// #############################################################
//  test_appvm.cpp  ·  pruebas de host de la maquina flex-app-v1
// #############################################################
//
//  El modulo bajo prueba es EXACTAMENTE el que va a la placa. Aqui es
//  donde se comprueba que una app descargada de internet NO puede
//  salirse de su caja, porque en placa eso solo se veria... cuando ya
//  ha pasado.
//
//  Que se comprueba:
//    1) El validador rechaza todo lo que no es decodificable: opcode
//       desconocido, instruccion truncada, salto fuera y salto a mitad
//       de otra instruccion, indices imposibles y tamanos incoherentes.
//    2) Aritmetica con desbordamiento DEFINIDO y division por cero.
//    3) Memoria lineal: ni un byte fuera, con direcciones negativas y
//       enormes incluidas.
//    4) Pila, marcos y locales: desbordamiento detectado, no ignorado.
//    5) Presupuesto: agotarlo NO es un error y se reanuda exactamente
//       donde iba; el YIELD voluntario hace lo mismo.
//    6) Ruido: miles de imagenes aleatorias no pueden colgar ni
//       corromper el validador (con ASan/UBSan detras).

#include "../../FlexOS_Ultra/FlexOS_AppVM.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>

static int g_fail = 0, g_run = 0;
#define CHECK(cond, ...) do { g_run++; if(!(cond)){ g_fail++; \
  std::printf("  FALLO %s:%d  ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while(0)

#include "vmimage.h"

// ---- Entorno de host minimo --------------------------------------------
static int32_t g_lastArg = 0;
static int     g_sysCalls = 0;
static bool    g_forceFatal = false;

static uint8_t testArity(void*, uint16_t id){
  if(id == 1) return 1;      // eco(x) -> x
  if(id == 2) return 0;      // contador() -> nº de llamadas
  return 0xFF;
}
static int32_t testSyscall(void*, uint16_t id, const int32_t* a, uint8_t argc, bool* fatal){
  g_sysCalls++;
  if(g_forceFatal){ *fatal = true; return 0; }
  if(id == 1 && argc == 1){ g_lastArg = a[0]; return a[0]; }
  if(id == 2) return g_sysCalls;
  *fatal = true;
  return 0;
}

// Regiones de trabajo compartidas por las pruebas.
struct Regions {
  uint8_t mem[4096];
  int32_t globals[FLEXVM_MAX_GLOBALS];
  int32_t stack[FLEXVM_MAX_STACK];
  int32_t locals[FLEXVM_MAX_FRAMESLOTS];
  FlexVmFrame frames[FLEXVM_MAX_CALLDEPTH];
  uint8_t scratch[FLEXVM_MAX_CODE / 8 + 8];
};
static Regions R;

// La VM guarda PUNTEROS a la imagen: quien la carga tiene que mantenerla
// viva mientras la maquina exista (asi lo dice FlexOS_AppVM.h y asi lo
// comprueba AddressSanitizer si alguien lo olvida). Por eso la imagen se
// copia aqui, a un buffer que vive tanto como la prueba.
static std::vector<uint8_t> g_img;

static FlexVmLoad loadImage(FlexVm& vm, const std::vector<uint8_t>& img){
  g_img = img;
  return flexVmValidate(&vm, g_img.data(), (uint32_t)g_img.size(),
                        R.scratch, sizeof(R.scratch), 0, 0, sizeof(R.mem),
                        testArity, nullptr);
}

static bool bindVm(FlexVm& vm){
  return flexVmBind(&vm, R.mem, sizeof(R.mem), R.globals, FLEXVM_MAX_GLOBALS,
                    R.stack, FLEXVM_MAX_STACK, R.locals, FLEXVM_MAX_FRAMESLOTS,
                    R.frames, FLEXVM_MAX_CALLDEPTH, testSyscall, nullptr) == FLEXVM_LOAD_OK;
}

// Ejecuta onTick(arg) hasta el final y devuelve el resultado en `out`.
static FlexVmRun runTick(FlexVm& vm, int32_t arg, int32_t* out, uint32_t budget = 100000){
  if(!flexVmCall(&vm, FLEXVM_H_TICK, &arg, 1)) return FLEXVM_IDLE;
  FlexVmRun r = flexVmRun(&vm, budget, nullptr);
  if(out) *out = vm.result;
  return r;
}

// Imagen con un unico onTick(a) formado por `body`.
static Image tickImage(){
  Image im;
  im.funcs.push_back({0, 1, 2});
  im.h[FLEXVM_H_TICK] = 0;
  return im;
}

// =============================================================
static void testValidator(){
  std::printf("-- validador: lo que no se decodifica, no entra --\n");
  FlexVm vm;

  { // opcode desconocido
    Image im = tickImage();
    im.b(0xAB); im.b(OP_RET);
    CHECK(loadImage(vm, im.build()) == FLEXVM_LOAD_CODE, "un opcode inexistente tiene que rechazarse");
  }
  { // instruccion truncada al final del codigo
    Image im = tickImage();
    im.b(OP_PUSH_I32); im.b(1); im.b(2);          // faltan dos bytes
    CHECK(loadImage(vm, im.build()) == FLEXVM_LOAD_CODE, "una instruccion truncada tiene que rechazarse");
  }
  { // salto fuera del codigo
    Image im = tickImage();
    im.u16(OP_JMP, 0x7FF0);
    im.b(OP_RET);
    CHECK(loadImage(vm, im.build()) == FLEXVM_LOAD_CODE, "un salto fuera de rango tiene que rechazarse");
  }
  { // salto a MITAD de otra instruccion: el caso clasico
    Image im = tickImage();
    size_t j = im.here(); im.u16(OP_JMP, 0);
    im.i32(0x11223344);                            // 5 bytes
    im.b(OP_RET);
    im.patchRel(j, j + 3 + 2);                     // cae en medio del push
    CHECK(loadImage(vm, im.build()) == FLEXVM_LOAD_CODE, "saltar a mitad de instruccion tiene que rechazarse");
  }
  { // global fuera de rango
    Image im = tickImage();
    im.globals = 2;
    im.u16(OP_LD_GLOBAL, 5); im.b(OP_DROP); im.b(OP_RET);
    CHECK(loadImage(vm, im.build()) == FLEXVM_LOAD_CODE, "una global inexistente tiene que rechazarse");
  }
  { // funcion inexistente
    Image im = tickImage();
    im.u16(OP_CALL, 9); im.b(OP_DROP); im.b(OP_RET);
    CHECK(loadImage(vm, im.build()) == FLEXVM_LOAD_CODE, "una funcion inexistente tiene que rechazarse");
  }
  { // syscall que este firmware no tiene
    Image im = tickImage();
    im.u16(OP_SYS, 0x4242); im.b(OP_DROP); im.b(OP_RET);
    CHECK(loadImage(vm, im.build()) == FLEXVM_LOAD_CODE, "una syscall desconocida no puede ni cargarse");
  }
  { // manejador con aridad equivocada
    Image im;
    im.funcs.push_back({0, 0, 0});
    im.h[FLEXVM_H_TICK] = 0;                        // onTick necesita 1 argumento
    im.b(OP_RET);
    CHECK(loadImage(vm, im.build()) == FLEXVM_LOAD_HANDLER, "onTick con aridad distinta de 1 se rechaza");
  }
  { // sin ningun manejador
    Image im;
    im.funcs.push_back({0, 0, 0});
    im.b(OP_RET);
    CHECK(loadImage(vm, im.build()) == FLEXVM_LOAD_HANDLER, "una imagen sin manejadores no es una app");
  }
  { // tamano declarado != tamano real
    Image im = tickImage();
    im.b(OP_RET);
    std::vector<uint8_t> raw = im.build();
    raw.push_back(0);
    CHECK(loadImage(vm, raw) == FLEXVM_LOAD_SIZE, "un byte de mas invalida la imagen");
  }
  { // magia
    Image im = tickImage();
    im.b(OP_RET);
    std::vector<uint8_t> raw = im.build();
    raw[1] = 'X';
    CHECK(loadImage(vm, raw) == FLEXVM_LOAD_MAGIC, "sin FLXB no hay imagen");
  }
  { // memoria por encima del techo que impone el manifest
    Image im = tickImage();
    im.mem = 8192;                                   // pide 8 KB
    im.b(OP_RET);
    std::vector<uint8_t> raw = im.build();
    FlexVm v2;
    CHECK(flexVmValidate(&v2, raw.data(), (uint32_t)raw.size(), R.scratch, sizeof(R.scratch),
                         0, 0, 4096, testArity, nullptr) == FLEXVM_LOAD_LIMITS,
          "el techo del manifest manda sobre lo que pide el bytecode");
  }
  { // buffer de validacion insuficiente
    Image im = tickImage();
    for(int i = 0; i < 200; i++) im.b(OP_NOP);
    im.b(OP_RET);
    std::vector<uint8_t> raw = im.build();
    FlexVm v3;
    uint8_t tiny[4];
    CHECK(flexVmValidate(&v3, raw.data(), (uint32_t)raw.size(), tiny, sizeof(tiny),
                         0, 0, sizeof(R.mem), testArity, nullptr) == FLEXVM_LOAD_SCRATCH,
          "sin sitio para validar, no se valida (y no se ejecuta)");
  }
  { // una imagen valida SI entra
    Image im = tickImage();
    im.b(OP_RET);
    CHECK(loadImage(vm, im.build()) == FLEXVM_LOAD_OK, "una imagen valida tiene que cargarse");
  }
}

// =============================================================
static void testArithmetic(){
  std::printf("-- aritmetica: desbordamiento definido, division por cero atrapada --\n");
  FlexVm vm;

  auto evalBin = [&](uint8_t op, int32_t a, int32_t b, int32_t* out) -> FlexVmRun {
    Image im = tickImage();
    im.i32(a); im.i32(b); im.b(op); im.b(OP_RETV);
    if(loadImage(vm, im.build()) != FLEXVM_LOAD_OK) return FLEXVM_TRAP;
    if(!bindVm(vm)) return FLEXVM_TRAP;
    return runTick(vm, 0, out);
  };

  int32_t r = 0;
  CHECK(evalBin(OP_ADD, 7, 5, &r) == FLEXVM_DONE && r == 12, "7 + 5 = 12");
  CHECK(evalBin(OP_SUB, 7, 5, &r) == FLEXVM_DONE && r == 2, "7 - 5 = 2");
  CHECK(evalBin(OP_MUL, 7, 5, &r) == FLEXVM_DONE && r == 35, "7 * 5 = 35");
  CHECK(evalBin(OP_DIV, -7, 2, &r) == FLEXVM_DONE && r == -3, "-7 / 2 = -3 (trunca hacia cero)");
  CHECK(evalBin(OP_MOD, -7, 2, &r) == FLEXVM_DONE && r == -1, "-7 %% 2 = -1");
  CHECK(evalBin(OP_ADD, 2147483647, 1, &r) == FLEXVM_DONE && r == (-2147483647 - 1),
        "el desbordamiento da la vuelta, no es comportamiento indefinido");
  CHECK(evalBin(OP_MUL, 65536, 65536, &r) == FLEXVM_DONE && r == 0, "65536^2 da la vuelta a 0");
  CHECK(evalBin(OP_DIV, -2147483647 - 1, -1, &r) == FLEXVM_DONE && r == (-2147483647 - 1),
        "INT_MIN / -1 esta definido y no aborta el proceso");
  CHECK(evalBin(OP_MOD, -2147483647 - 1, -1, &r) == FLEXVM_DONE && r == 0, "INT_MIN %% -1 = 0");

  CHECK(evalBin(OP_DIV, 5, 0, &r) == FLEXVM_TRAP && vm.trap == FLEXVM_TRAP_DIVZERO,
        "dividir por cero es una trampa limpia, no un cuelgue");
  CHECK(evalBin(OP_MOD, 5, 0, &r) == FLEXVM_TRAP && vm.trap == FLEXVM_TRAP_DIVZERO,
        "el modulo por cero, igual");

  // Desplazamientos: cuenta negativa o >= 32 no puede ser comportamiento indefinido.
  CHECK(evalBin(OP_SHL, 1, 33, &r) == FLEXVM_DONE && r == 2, "shl toma la cuenta modulo 32");
  CHECK(evalBin(OP_SHR, -8, 1, &r) == FLEXVM_DONE && r == -4, "shr con signo mantiene el signo");
  CHECK(evalBin(OP_USHR, -1, 28, &r) == FLEXVM_DONE && r == 15, "ushr rellena con ceros");
  CHECK(evalBin(OP_SHL, 1, -1, &r) == FLEXVM_DONE, "un desplazamiento negativo no es UB");
  CHECK(evalBin(OP_LT, -3, 2, &r) == FLEXVM_DONE && r == 1, "-3 < 2");
  CHECK(evalBin(OP_MIN, -3, 2, &r) == FLEXVM_DONE && r == -3, "min(-3,2)");
}

// =============================================================
static void testMemory(){
  std::printf("-- memoria lineal: ni un byte fuera --\n");
  FlexVm vm;

  auto store = [&](int32_t addr, uint8_t op) -> FlexVmRun {
    Image im = tickImage();
    im.mem = 256;
    im.i32(addr); im.i32(0x41424344); im.b(op); im.b(OP_RET);
    if(loadImage(vm, im.build()) != FLEXVM_LOAD_OK) return FLEXVM_TRAP;
    if(!bindVm(vm)) return FLEXVM_TRAP;
    return runTick(vm, 0, nullptr);
  };

  CHECK(store(0, OP_ST32) == FLEXVM_DONE, "escribir al principio vale");
  CHECK(store(252, OP_ST32) == FLEXVM_DONE, "escribir en el ultimo hueco vale");
  CHECK(store(253, OP_ST32) == FLEXVM_TRAP && vm.trap == FLEXVM_TRAP_MEMORY,
        "una escritura de 4 bytes que se sale por 1 se atrapa");
  CHECK(store(-1, OP_ST8) == FLEXVM_TRAP && vm.trap == FLEXVM_TRAP_MEMORY,
        "una direccion negativa se atrapa");
  CHECK(store(2147483647, OP_ST32) == FLEXVM_TRAP && vm.trap == FLEXVM_TRAP_MEMORY,
        "una direccion enorme se atrapa (sin dar la vuelta)");
  CHECK(store(255, OP_ST16) == FLEXVM_TRAP && vm.trap == FLEXVM_TRAP_MEMORY,
        "16 bits que cruzan el final se atrapan");

  { // ida y vuelta con signo
    Image im = tickImage();
    im.i32(8); im.i32(-1); im.b(OP_ST8);
    im.i32(8); im.b(OP_LD8); im.b(OP_RETV);
    CHECK(loadImage(vm, im.build()) == FLEXVM_LOAD_OK && bindVm(vm), "imagen de ida y vuelta valida");
    int32_t r = 0;
    CHECK(runTick(vm, 0, &r) == FLEXVM_DONE && r == -1, "ld8 extiende el signo");
  }
  { // pool de constantes: solo lectura y con limite
    Image im = tickImage();
    im.konst = { 'A', 'B', 'C', 'D' };
    im.i32(3); im.b(OP_KLD8U); im.b(OP_RETV);
    CHECK(loadImage(vm, im.build()) == FLEXVM_LOAD_OK && bindVm(vm), "imagen con constantes valida");
    int32_t r = 0;
    CHECK(runTick(vm, 0, &r) == FLEXVM_DONE && r == 'D', "se lee la ultima constante");

    Image bad = tickImage();
    bad.konst = { 'A', 'B', 'C', 'D' };
    bad.i32(4); bad.b(OP_KLD8U); bad.b(OP_RETV);
    CHECK(loadImage(vm, bad.build()) == FLEXVM_LOAD_OK && bindVm(vm), "imagen de constante fuera valida");
    CHECK(runTick(vm, 0, &r) == FLEXVM_TRAP && vm.trap == FLEXVM_TRAP_CONST,
          "leer mas alla del pool de constantes se atrapa");
  }
  { // la memoria arranca a cero en cada lanzamiento
    Image im = tickImage();
    im.i32(16); im.b(OP_LD32); im.b(OP_RETV);
    CHECK(loadImage(vm, im.build()) == FLEXVM_LOAD_OK, "imagen valida");
    memset(R.mem, 0xAB, sizeof(R.mem));
    CHECK(bindVm(vm), "bind valido");
    int32_t r = 1;
    CHECK(runTick(vm, 0, &r) == FLEXVM_DONE && r == 0,
          "bind pone la memoria de la app a cero: no hereda la sesion anterior");
  }
}

// =============================================================
static void testStackAndFrames(){
  std::printf("-- pila, marcos y locales --\n");
  FlexVm vm;

  { // vaciar la pila de mas
    Image im = tickImage();
    im.b(OP_ADD); im.b(OP_RET);
    CHECK(loadImage(vm, im.build()) == FLEXVM_LOAD_OK && bindVm(vm), "imagen valida");
    CHECK(runTick(vm, 0, nullptr) == FLEXVM_TRAP && vm.trap == FLEXVM_TRAP_STACK,
          "sacar de una pila vacia se atrapa");
  }
  { // desbordar la pila
    Image im = tickImage();
    im.stack = 8;
    for(int i = 0; i < 20; i++) im.i8(OP_PUSH_I8, 1);
    im.b(OP_RET);
    CHECK(loadImage(vm, im.build()) == FLEXVM_LOAD_OK && bindVm(vm), "imagen valida");
    CHECK(runTick(vm, 0, nullptr) == FLEXVM_TRAP && vm.trap == FLEXVM_TRAP_STACK,
          "desbordar la pila se atrapa");
  }
  { // local fuera del marco
    Image im = tickImage();       // onTick tiene 1 arg + 2 locales = 3 slots
    im.u8(OP_LD_LOCAL, 7); im.b(OP_DROP); im.b(OP_RET);
    CHECK(loadImage(vm, im.build()) == FLEXVM_LOAD_OK && bindVm(vm), "imagen valida");
    CHECK(runTick(vm, 0, nullptr) == FLEXVM_TRAP && vm.trap == FLEXVM_TRAP_LOCAL,
          "leer una local que no existe se atrapa");
  }
  { // recursion infinita: profundidad de llamada acotada
    Image im;
    im.calldepth = 8;
    im.frames = 64;
    im.funcs.push_back({0, 1, 0});     // onTick(a) -> se llama a si misma
    im.h[FLEXVM_H_TICK] = 0;
    im.u8(OP_PUSH_I8, 0);
    im.u16(OP_CALL, 0);
    im.b(OP_RETV);
    CHECK(loadImage(vm, im.build()) == FLEXVM_LOAD_OK && bindVm(vm), "imagen recursiva valida");
    CHECK(runTick(vm, 0, nullptr) == FLEXVM_TRAP && vm.trap == FLEXVM_TRAP_FRAME,
          "la recursion infinita choca contra el limite de marcos, no contra la pila del sistema");
  }
  { // llamada normal con argumentos y valor de retorno
    Image im;
    im.funcs.push_back({0, 1, 0});     // onTick
    im.h[FLEXVM_H_TICK] = 0;
    im.u8(OP_LD_LOCAL, 0);
    im.i8(OP_PUSH_I8, 10);
    im.u16(OP_CALL, 1);
    im.b(OP_RETV);
    uint32_t sumOff = (uint32_t)im.here();
    im.funcs.push_back({sumOff, 2, 0});   // suma(a,b)
    im.u8(OP_LD_LOCAL, 0); im.u8(OP_LD_LOCAL, 1); im.b(OP_ADD); im.b(OP_RETV);
    CHECK(loadImage(vm, im.build()) == FLEXVM_LOAD_OK && bindVm(vm), "imagen con llamada valida");
    int32_t r = 0;
    CHECK(runTick(vm, 32, &r) == FLEXVM_DONE && r == 42, "onTick(32) + 10 = 42");
  }
  { // RET siempre deja UN valor: usar drop no descuadra la pila
    Image im = tickImage();
    im.i8(OP_PUSH_I8, 3);
    im.b(OP_DUP); im.b(OP_DROP);
    im.b(OP_RETV);
    CHECK(loadImage(vm, im.build()) == FLEXVM_LOAD_OK && bindVm(vm), "imagen valida");
    int32_t r = 0;
    CHECK(runTick(vm, 0, &r) == FLEXVM_DONE && r == 3, "dup + drop deja la pila como estaba");
  }
}

// =============================================================
static void testBudgetAndYield(){
  std::printf("-- presupuesto y cesion cooperativa --\n");
  FlexVm vm;

  { // bucle largo: se agota el presupuesto y se REANUDA donde iba
    Image im;
    im.globals = 2;
    im.funcs.push_back({0, 1, 1});
    im.h[FLEXVM_H_TICK] = 0;
    // local1 = 0; while(local1 < 5000) local1++;  return local1;
    im.i8(OP_PUSH_I8, 0); im.u8(OP_ST_LOCAL, 1);
    size_t loop = im.here();
    im.u8(OP_LD_LOCAL, 1); im.i32(5000); im.b(OP_LT);
    size_t jz = im.here(); im.u16(OP_JZ, 0);
    im.u8(OP_LD_LOCAL, 1); im.i8(OP_PUSH_I8, 1); im.b(OP_ADD); im.u8(OP_ST_LOCAL, 1);
    size_t jmp = im.here(); im.u16(OP_JMP, 0);
    size_t end = im.here();
    im.u8(OP_LD_LOCAL, 1); im.b(OP_RETV);
    im.patchRel(jz, end);
    im.patchRel(jmp, loop);
    CHECK(loadImage(vm, im.build()) == FLEXVM_LOAD_OK && bindVm(vm), "imagen del bucle valida");

    int32_t arg = 0;
    CHECK(flexVmCall(&vm, FLEXVM_H_TICK, &arg, 1), "se puede llamar a onTick");
    int rounds = 0;
    FlexVmRun r = FLEXVM_YIELD;
    uint32_t totalUsed = 0;
    while(r == FLEXVM_YIELD && rounds < 500){
      uint32_t used = 0;
      r = flexVmRun(&vm, 1000, &used);      // presupuesto pequeño a proposito
      totalUsed += used;
      rounds++;
      CHECK(used <= 1000, "nunca se ejecutan mas instrucciones que el presupuesto");
    }
    CHECK(r == FLEXVM_DONE, "el bucle acaba tras varias tandas");
    CHECK(rounds > 10, "hicieron falta varias tandas (%d)", rounds);
    CHECK(vm.result == 5000, "el resultado es correcto tras reanudar %d veces", rounds);
    CHECK(totalUsed > 20000, "se contaron todas las instrucciones (%u)", totalUsed);
  }
  { // YIELD voluntario
    Image im = tickImage();
    im.i8(OP_PUSH_I8, 7);
    im.b(OP_YIELD);
    im.b(OP_RETV);
    CHECK(loadImage(vm, im.build()) == FLEXVM_LOAD_OK && bindVm(vm), "imagen con yield valida");
    int32_t arg = 0;
    CHECK(flexVmCall(&vm, FLEXVM_H_TICK, &arg, 1), "llamada valida");
    CHECK(flexVmRun(&vm, 100000, nullptr) == FLEXVM_YIELD && vm.yielded == 1,
          "YIELD devuelve el control aunque quede presupuesto");
    CHECK(flexVmRun(&vm, 100000, nullptr) == FLEXVM_DONE && vm.result == 7,
          "y se continua exactamente donde estaba");
  }
  { // HALT termina el manejador entero
    Image im = tickImage();
    im.b(OP_HALT);
    im.i8(OP_PUSH_I8, 99); im.b(OP_RETV);
    CHECK(loadImage(vm, im.build()) == FLEXVM_LOAD_OK && bindVm(vm), "imagen con halt valida");
    int32_t r = -1;
    CHECK(runTick(vm, 0, &r) == FLEXVM_DONE && r == 0 && !vm.running, "halt corta el manejador");
  }
  { // TRAP explicito de la app
    Image im = tickImage();
    im.u8(OP_TRAP, 5);
    CHECK(loadImage(vm, im.build()) == FLEXVM_LOAD_OK && bindVm(vm), "imagen con trap valida");
    CHECK(runTick(vm, 0, nullptr) == FLEXVM_TRAP && vm.trap == FLEXVM_TRAP_ABORT && vm.result == 5,
          "la app puede abortarse a si misma con un codigo");
  }
  { // abortar desde fuera deja la VM limpia
    Image im = tickImage();
    im.i8(OP_PUSH_I8, 1); im.b(OP_YIELD); im.b(OP_RETV);
    CHECK(loadImage(vm, im.build()) == FLEXVM_LOAD_OK && bindVm(vm), "imagen valida");
    int32_t arg = 0;
    flexVmCall(&vm, FLEXVM_H_TICK, &arg, 1);
    flexVmRun(&vm, 100, nullptr);
    CHECK(vm.running == 1, "hay un manejador a medias");
    flexVmAbort(&vm, FLEXVM_TRAP_HOST);
    CHECK(vm.running == 0 && vm.sp == 0 && vm.frameCount == 0 && vm.localTop == 0,
          "abortar deja pila, marcos y locales a cero");
    CHECK(flexVmRun(&vm, 100, nullptr) == FLEXVM_IDLE, "y ya no hay nada que ejecutar");
  }
}

// =============================================================
static void testSyscalls(){
  std::printf("-- syscalls: aridad, orden de argumentos y fallo del host --\n");
  FlexVm vm;
  g_sysCalls = 0; g_forceFatal = false;

  { // la syscall recibe los argumentos en el orden en que se apilaron
    Image im = tickImage();
    im.i32(1234);
    im.u16(OP_SYS, 1);
    im.b(OP_RETV);
    CHECK(loadImage(vm, im.build()) == FLEXVM_LOAD_OK && bindVm(vm), "imagen con syscall valida");
    int32_t r = 0;
    CHECK(runTick(vm, 0, &r) == FLEXVM_DONE && r == 1234 && g_lastArg == 1234,
          "el argumento llega tal cual y el resultado se apila");
  }
  { // sin argumentos suficientes en la pila
    Image im = tickImage();
    im.u16(OP_SYS, 1);      // necesita 1 argumento y la pila esta vacia
    im.b(OP_RETV);
    CHECK(loadImage(vm, im.build()) == FLEXVM_LOAD_OK && bindVm(vm), "imagen valida");
    CHECK(runTick(vm, 0, nullptr) == FLEXVM_TRAP && vm.trap == FLEXVM_TRAP_STACK,
          "una syscall sin sus argumentos se atrapa");
  }
  { // el host declara un fallo irrecuperable
    Image im = tickImage();
    im.u16(OP_SYS, 2);
    im.b(OP_RETV);
    CHECK(loadImage(vm, im.build()) == FLEXVM_LOAD_OK && bindVm(vm), "imagen valida");
    g_forceFatal = true;
    CHECK(runTick(vm, 0, nullptr) == FLEXVM_TRAP && vm.trap == FLEXVM_TRAP_HOST,
          "si el host dice que no, la app se para");
    g_forceFatal = false;
  }
}

// =============================================================
static void testFuzz(){
  std::printf("-- ruido: imagenes aleatorias contra el validador --\n");
  // Semilla FIJA: si un caso falla, se reproduce exactamente.
  unsigned seed = 20260906u;
  int loaded = 0, rejected = 0;
  std::vector<uint8_t> buf;
  FlexVm vm;
  for(int iter = 0; iter < 4000; iter++){
    // Se parte de una imagen valida y se corrompe un byte al azar: asi el
    // ruido cae dentro de la estructura y no se descarta por la magia.
    Image im = tickImage();
    im.konst = { 1, 2, 3, 4, 5, 6, 7, 8 };
    for(int i = 0; i < 24; i++) im.b((uint8_t)(seed % 0x50));
    im.b(OP_RET);
    buf = im.build();
    for(int k = 0; k < 3; k++){
      seed = seed * 1103515245u + 12345u;
      size_t at = 4 + (seed >> 8) % (buf.size() - 4);
      seed = seed * 1103515245u + 12345u;
      buf[at] = (uint8_t)(seed >> 16);
    }
    FlexVmLoad lr = flexVmValidate(&vm, buf.data(), (uint32_t)buf.size(),
                                   R.scratch, sizeof(R.scratch), 0, 0, sizeof(R.mem),
                                   testArity, nullptr);
    if(lr == FLEXVM_LOAD_OK){
      loaded++;
      // Si el validador la acepta, ejecutarla no puede corromper nada: como
      // mucho tiene que TRAMPAR. Con ASan/UBSan detras, esto es la prueba.
      if(bindVm(vm)){
        int32_t arg = (int32_t)seed;
        if(flexVmCall(&vm, FLEXVM_H_TICK, &arg, 1)){
          FlexVmRun r = FLEXVM_YIELD;
          int guard = 0;
          while(r == FLEXVM_YIELD && guard++ < 64) r = flexVmRun(&vm, 4096, nullptr);
          CHECK(r == FLEXVM_DONE || r == FLEXVM_TRAP || r == FLEXVM_YIELD,
                "ejecutar ruido validado sale por DONE, TRAP o YIELD");
        }
      }
    } else rejected++;
  }
  CHECK(rejected > 0, "el validador rechaza ruido (%d rechazadas)", rejected);
  CHECK(loaded > 0, "y acepta lo que sigue siendo decodificable (%d aceptadas)", loaded);
  std::printf("   %d imagenes aceptadas y ejecutadas, %d rechazadas\n", loaded, rejected);
}

// =============================================================
int main(){
  std::printf("\n=== FlexOS · maquina aislada flex-app-v1 ===\n");
  testValidator();
  testArithmetic();
  testMemory();
  testStackAndFrames();
  testBudgetAndYield();
  testSyscalls();
  testFuzz();
  std::printf("=== %d comprobaciones, %d fallos ===\n", g_run, g_fail);
  return g_fail ? 1 : 0;
}
