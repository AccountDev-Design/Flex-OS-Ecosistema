// #############################################################
// ##  FlexOS · CLAVE DEL SISTEMA  ·  PIN y contrasena de bloqueo
// ##  ----------------------------------------------------------
// ##  Sal aleatoria + PBKDF2-HMAC-SHA256 + comparacion en tiempo
// ##  constante para la clave de la PANTALLA DE BLOQUEO. Es lo unico
// ##  que hay aqui: ni almacen, ni contenido, ni apps privadas.
// ##
// ##  DE DONDE SALE ESTE MODULO
// ##  ----------------------------------------------------------
// ##  Estas funciones vivian dentro de FlexOS_Vault.cpp porque la
// ##  criptografia ya estaba alli. Al retirarse Flex Vault del
// ##  sistema, la clave del sistema -- que NO es la de la boveda y la
// ##  usa medio Flex OS (desbloquear la pantalla, abrir una app con
// ##  candado, salir del kiosco, apagado seguro) -- se queda en su
// ##  propio modulo, con su propia prueba de host.
// ##
// ##  POR QUE ES UN .cpp Y NO UNA CABECERA DEL SKETCH
// ##  ----------------------------------------------------------
// ##  Es codigo PORTABLE: no toca la pantalla, ni el tactil, ni una
// ##  global del sketch. Se compila y se ejercita entero en el PC
// ##  (tests/host/test_passcode.cpp) contra OpenSSL y una NVS en
// ##  memoria, igual que el resto del nucleo portable.
// #############################################################
#pragma once
#include <stdint.h>
#include <stddef.h>

// Longitud maxima de una clave del sistema, con su terminador.
#define FLEXLOCK_SECRET_MAX 64

// Tipos de clave. Son los MISMOS valores que guarda la clave
// "locktype" de NVS desde siempre: una placa que actualiza no ve
// cambiar su bloqueo.
#define FLEXLOCK_NONE  0
#define FLEXLOCK_PIN   1
#define FLEXLOCK_PASS  2

// -------------------------------------------------------------
//  PRIMITIVAS
//  ------------------------------------------------------------
//  Publicas porque la prueba de host las ejercita contra vectores
//  conocidos: un PBKDF2 que se desvie no puede pasar desapercibido.
// -------------------------------------------------------------
// Borrado que el compilador no puede optimizar (volatile por dentro).
void     flexLockWipe(void* p, size_t n);
// Comparacion en tiempo constante: no filtra cuantos bytes coincidian.
bool     flexLockEqualCT(const void* a, const void* b, size_t n);
// Bytes aleatorios del generador de hardware (esp_fill_random en la placa).
void     flexLockRandomBytes(void* out, size_t n);
// PBKDF2-HMAC-SHA256 con outLen <= 32 (un solo bloque).
void     flexLockKdf(const char* secret, const uint8_t* salt, size_t saltLen,
                     uint32_t iters, uint8_t* out, size_t outLen);

// -------------------------------------------------------------
//  BLOQUEO DEL SISTEMA (Ajustes -> Seguridad y privacidad ->
//  Bloqueo)
//  ------------------------------------------------------------
//  El PIN y la contrasena se guardan como sal de 16 bytes +
//  PBKDF2-HMAC-SHA256 de 32, comparados en tiempo constante. Nunca
//  en texto legible.
//
//  MIGRACION DE QUIEN YA TIENE CLAVE PUESTA
//  flexLockMigrate() se llama en cada arranque. Si encuentra una
//  clave en texto legible de una version antigua, la convierte a
//  hash con sal y BORRA la clave antigua de NVS. El usuario no nota
//  nada: sigue entrando con el mismo PIN. Si no hay nada que migrar,
//  no escribe.
//
//  LO QUE SE SIGUE GUARDANDO EN CLARO, Y POR QUE
//  La LONGITUD del PIN (no el PIN). La pantalla de bloqueo
//  autoconfirma al llegar al numero de digitos guardado, y los
//  puntos en pantalla ya revelan esa longitud mientras se escribe.
// -------------------------------------------------------------
// 0 = sin clave, 1 = PIN, 2 = contrasena.
int      flexLockType();
// Longitud del PIN guardado (0 si es contrasena o no hay clave).
int      flexLockLen();
// Guarda una clave nueva (genera sal nueva). type = 1 PIN, 2 contrasena.
bool     flexLockSet(const char* secret, int type);
// Comprueba una clave contra el hash guardado, en tiempo constante.
// Hace TODO el trabajo de una vez: sirve para las rutas que no estan
// en el camino critico de un cuadro (migracion, pruebas).
bool     flexLockVerify(const char* secret);
// Quita la clave del sistema (deja el bloqueo en "Deslizar").
bool     flexLockClear();
// 1 = habia una clave en texto legible y se ha migrado a hash,
// 0 = no habia nada que migrar, -1 = no se pudo completar.
int      flexLockMigrate();

// -------------------------------------------------------------
//  VERIFICACION A PLAZOS  ·  la que usa la pantalla de bloqueo
//  ------------------------------------------------------------
//  POR QUE EXISTE. flexLockVerify() deriva el hash con
//  FLEXLOCK_ITERS iteraciones de HMAC-SHA256 de una sentada. Llamarla
//  desde el tick de la pantalla de bloqueo dejaba el bucle del
//  sistema parado ESE rato entero: el ultimo digito del PIN se
//  quedaba sin pintar, la animacion se cortaba y el tactil no se leia
//  hasta que la derivacion terminaba. Eso es el "se congela un
//  momento al meter el ultimo digito".
//
//  COMO SE ARREGLA. PBKDF2 con dkLen <= 32 es una CADENA de HMACs:
//  U(i+1) = HMAC(clave, U(i)), acumulando el XOR. Se puede parar y
//  seguir. Aqui se parte en tandas: cada vuelta del loop hace unas
//  pocas miles de iteraciones -- un presupuesto en iteraciones, no un
//  "hasta que acabe" -- y entre tanda y tanda el sistema sigue
//  pintando, animando y leyendo el tactil.
//
//  El resultado es BIT A BIT el mismo que el de flexLockVerify(): es
//  la misma cadena, solo que troceada.
//
//  USO
//     if(!flexLockVerifyBegin(pin)) -> no hay clave guardada: fallo
//     cada vuelta: int r = flexLockVerifyStep(FLEXLOCK_STEP_ITERS);
//       FLEXLOCK_BUSY -> sigue; FLEXLOCK_OK -> acierto; FLEXLOCK_FAIL -> fallo
//     flexLockVerifyCancel() aborta y borra el estado.
//  El secreto se copia dentro del modulo y se borra al terminar, en
//  todos los caminos de salida.
// -------------------------------------------------------------
#define FLEXLOCK_BUSY  0
#define FLEXLOCK_OK    1
#define FLEXLOCK_FAIL  2
// Tanda por vuelta del loop. 1500 iteraciones de HMAC-SHA256 con el
// acelerador del P4 estan muy por debajo de un cuadro a 60 Hz, asi
// que ni una vuelta se salta por esto.
#define FLEXLOCK_STEP_ITERS 1500

bool     flexLockVerifyBegin(const char* secret);
int      flexLockVerifyStep(uint32_t budgetIters);
void     flexLockVerifyCancel();
bool     flexLockVerifyActive();

// -------------------------------------------------------------
//  VERIFICACION DESDE OTRA TAREA
//  ------------------------------------------------------------
//  La de a plazos guarda su estado en el modulo (hay UNA en vuelo:
//  la de la pantalla). Flex Web Server comprueba la clave del sistema
//  desde su propia tarea, y usar esa misma cadena cancelaria -- o
//  peor, mezclaria -- la verificacion que la pantalla tuviera a
//  medias. Esta hace todo con variables LOCALES: mismo hash, misma
//  comparacion en tiempo constante, y ni lee ni toca el estado de la
//  de plazos. Tarda lo mismo que flexLockVerify(), asi que no se
//  llama desde el bucle de dibujo.
// -------------------------------------------------------------
bool     flexLockVerifyAlone(const char* secret);
