// #############################################################
//  PRUEBAS DE HOST DE LA CLAVE DEL SISTEMA
//  ------------------------------------------------------------
//  Compila y ejecuta FlexOS_Passcode.cpp EN EL PC, con la NVS en
//  memoria del arnes (inostub/Preferences.h) y OpenSSL como backend.
//
//  QUE SE COMPRUEBA DE VERDAD AQUI
//    1. Que PBKDF2-HMAC-SHA256 de este modulo da el resultado
//       correcto (vectores del RFC 6070 adaptados a SHA-256).
//    2. Que el PIN y la contrasena NUNCA quedan en NVS en texto
//       legible, y que dos configuraciones de la MISMA clave dan
//       hashes distintos (eso es lo que aporta la sal).
//    3. Que la migracion del usuario que ya tenia clave en texto
//       legible conserva el acceso y borra el original.
//    4. Que la VERIFICACION A PLAZOS -- la que usa la pantalla de
//       bloqueo para no parar el bucle en el ultimo digito -- da
//       exactamente el mismo veredicto que la de una sentada,
//       cualquiera que sea el tamano de la tanda.
//
//  LIMITE HONESTO: la primitiva HMAC que se ejercita aqui es la de
//  OpenSSL; en la placa es la de mbedTLS con el acelerador del
//  ESP32-P4. Lo que estas pruebas verifican es la LOGICA de esta
//  capa, que es la parte que se escribio aqui.
// #############################################################
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Arduino.h"
#include "Preferences.h"
#include "../../FlexOS_Ultra/FlexOS_Passcode.h"

// Particion virgen: es lo que usa cada bloque para empezar de cero.
static void prefsTestClear(){ flexPrefsWipe(); }

static int gChecks = 0, gFails = 0;
static void chk(bool ok, const char* what){
  gChecks++;
  if(ok) printf("  ok   %s\n", what);
  else { printf("  FALLO %s\n", what); gFails++; }
}

static void hexdump(const uint8_t* p, size_t n, char* out){
  for(size_t i = 0; i < n; i++) sprintf(out + i * 2, "%02x", p[i]);
}

static void testKdf(){
  printf("Derivacion de clave (PBKDF2-HMAC-SHA256)\n");
  uint8_t dk[32]; char hex[80];

  flexLockKdf("password", (const uint8_t*)"salt", 4, 1, dk, 32);
  hexdump(dk, 32, hex);
  chk(!strcmp(hex, "120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b"),
      "vector 1 (1 iteracion)");

  flexLockKdf("password", (const uint8_t*)"salt", 4, 2, dk, 32);
  hexdump(dk, 32, hex);
  chk(!strcmp(hex, "ae4d0c95af6b46d32d0adff928f06dd02a303f8ef3c251dfd6e2d85a95474c43"),
      "vector 2 (2 iteraciones)");

  flexLockKdf("password", (const uint8_t*)"salt", 4, 4096, dk, 32);
  hexdump(dk, 32, hex);
  chk(!strcmp(hex, "c5e478d59288c841aa530db6845c4c8d962893a001ce4e11a4963873aa98134a"),
      "vector 3 (4096 iteraciones)");

  // Una sal distinta con la misma contrasena da una clave distinta:
  // es LA razon de que exista la sal.
  uint8_t a[32], b[32];
  flexLockKdf("clave", (const uint8_t*)"AAAAAAAAAAAAAAAA", 16, 100, a, 32);
  flexLockKdf("clave", (const uint8_t*)"BBBBBBBBBBBBBBBB", 16, 100, b, 32);
  chk(memcmp(a, b, 32) != 0, "la sal cambia la clave derivada");

  chk(flexLockEqualCT(a, a, 32),  "comparacion en tiempo constante: iguales");
  chk(!flexLockEqualCT(a, b, 32), "comparacion en tiempo constante: distintos");

  uint8_t z[16]; memset(z, 0xAA, sizeof(z));
  flexLockWipe(z, sizeof(z));
  bool zero = true;
  for(size_t i = 0; i < sizeof(z); i++) if(z[i]) zero = false;
  chk(zero, "el borrado de memoria sensible deja ceros");
}

// -------------------------------------------------------------
//  BLOQUEO DEL SISTEMA: hash con sal y migracion del texto legible
// -------------------------------------------------------------
static void testBloqueoSistema(){
  printf("Bloqueo del sistema (hash con sal y migracion)\n");
  prefsTestClear();

  chk(flexLockType() == 0, "sin clave configurada al principio");
  chk(!flexLockVerify("1234"), "y no valida ninguna clave");
  chk(flexLockMigrate() == 0, "sin clave no hay nada que migrar");

  chk(flexLockSet("1234", 1), "se configura un PIN");
  chk(flexLockType() == 1, "queda como PIN");
  chk(flexLockLen() == 4, "y se recuerda su longitud");
  chk(flexLockVerify("1234"),  "el PIN correcto valida");
  chk(!flexLockVerify("1235"), "uno incorrecto no");
  chk(!flexLockVerify(""),     "ni la cadena vacia");

  // El PIN NO puede estar en NVS en texto legible.
  Preferences p;
  p.begin("flexos", true);
  chk(p.getString("lockpin", "@").length() == 1, "no queda ningun lockpin en NVS");
  chk(p.getString("lockpass", "@").length() == 1, "ni ningun lockpass");
  uint8_t h[32];
  chk(p.getBytes("lockhsh", h, sizeof(h)) == sizeof(h), "si hay un hash de 32 bytes");
  uint8_t sl[16];
  chk(p.getBytes("lockslt", sl, sizeof(sl)) == sizeof(sl), "y una sal de 16 bytes");
  p.end();
  // El hash no puede ser el PIN disfrazado.
  chk(memcmp(h, "1234", 4) != 0, "el hash no empieza por el PIN");

  // Dos claves iguales en dos configuraciones distintas dan hashes
  // distintos: eso es lo que aporta la sal.
  uint8_t h1[32]; memcpy(h1, h, 32);
  chk(flexLockSet("1234", 1), "se vuelve a configurar el MISMO PIN");
  p.begin("flexos", true);
  p.getBytes("lockhsh", h, sizeof(h));
  p.end();
  chk(memcmp(h1, h, 32) != 0, "el hash es distinto (sal nueva)");
  chk(flexLockVerify("1234"), "y sigue validando el PIN");

  chk(flexLockSet("mi contrasena larga", 2), "se configura una contrasena");
  chk(flexLockType() == 2, "queda como contrasena");
  chk(flexLockLen() == 0, "y la longitud no se expone para contrasenas");
  chk(flexLockVerify("mi contrasena larga"), "la contrasena valida");
  chk(!flexLockVerify("mi contrasena larga "), "un espacio de mas no");

  chk(flexLockClear(), "se puede quitar la clave");
  chk(flexLockType() == 0, "y el bloqueo vuelve a Deslizar");
  chk(!flexLockVerify("mi contrasena larga"), "ya no valida nada");

  // ---- MIGRACION del usuario que ya tenia PIN en texto legible ----
  prefsTestClear();
  p.begin("flexos", false);
  p.putString("lockpin", "2580");        // exactamente como lo guardaba la version anterior
  p.putInt("locktype", 1);
  p.end();
  chk(flexLockVerify("2580") == false, "antes de migrar no hay hash que validar");
  chk(flexLockMigrate() == 1, "la migracion detecta la clave en texto legible");
  chk(flexLockVerify("2580"), "y despues el MISMO PIN sigue abriendo");
  chk(flexLockType() == 1, "el tipo de bloqueo se conserva");
  chk(flexLockLen() == 4, "y la longitud tambien");
  p.begin("flexos", true);
  chk(p.getString("lockpin", "@").length() == 1, "el PIN en texto legible se BORRO de NVS");
  p.end();
  chk(flexLockMigrate() == 0, "y migrar otra vez no hace nada");

  // Lo mismo con contrasena.
  prefsTestClear();
  p.begin("flexos", false);
  p.putString("lockpass", "contrasena antigua");
  p.putInt("locktype", 2);
  p.end();
  chk(flexLockMigrate() == 1, "migra tambien una contrasena");
  chk(flexLockVerify("contrasena antigua"), "que sigue abriendo igual");
  p.begin("flexos", true);
  chk(p.getString("lockpass", "@").length() == 1, "y desaparece de NVS en claro");
  p.end();

  // Caso mixto: una version intermedia que dejara las dos cosas.
  p.begin("flexos", false);
  p.putString("lockpass", "contrasena antigua");
  p.end();
  chk(flexLockMigrate() == 1, "si reaparece texto legible junto al hash, se limpia");
  p.begin("flexos", true);
  chk(p.getString("lockpass", "@").length() == 1, "y se va de NVS");
  p.end();
  chk(flexLockVerify("contrasena antigua"), "sin perder el acceso");
}


// -------------------------------------------------------------
//  VERIFICACION A PLAZOS
//  ------------------------------------------------------------
//  Es la que corre en el camino critico del ultimo digito del PIN.
//  Lo unico que no puede pasar es que de un veredicto distinto al de
//  la de una sentada: si divergieran, el aparato se abriria (o no)
//  segun por donde se hubiera preguntado.
// -------------------------------------------------------------
static int runStepped(const char* secret, uint32_t budget, int* vueltas){
  *vueltas = 0;
  if(!flexLockVerifyBegin(secret)) return FLEXLOCK_FAIL;
  for(;;){
    int r = flexLockVerifyStep(budget);
    (*vueltas)++;
    if(r != FLEXLOCK_BUSY) return r;
    if(*vueltas > 100000) return FLEXLOCK_FAIL;    // red de seguridad de la prueba
  }
}

static void testVerificacionAPlazos(){
  printf("Verificacion a plazos (la del ultimo digito del PIN)\n");
  prefsTestClear();
  chk(!flexLockVerifyBegin("1234"), "sin clave guardada no se puede ni empezar");
  chk(!flexLockVerifyActive(), "y no queda ninguna verificacion viva");

  chk(flexLockSet("4821", 1), "se configura un PIN");

  int vueltas = 0;
  chk(runStepped("4821", FLEXLOCK_STEP_ITERS, &vueltas) == FLEXLOCK_OK,
      "el PIN correcto valida a plazos");
  chk(vueltas > 1, "y de verdad hizo falta mas de una vuelta del bucle");
  chk(!flexLockVerifyActive(), "al terminar no queda estado vivo");

  chk(runStepped("4822", FLEXLOCK_STEP_ITERS, &vueltas) == FLEXLOCK_FAIL,
      "un PIN incorrecto falla a plazos");

  // El TAMANO de la tanda no puede cambiar el veredicto: es la misma
  // cadena de HMACs, solo troceada de otra forma.
  for(uint32_t b = 1; b <= 40000; b *= 7){
    chk(runStepped("4821", b, &vueltas) == FLEXLOCK_OK, "acierta con cualquier tanda");
    chk(runStepped("0000", b, &vueltas) == FLEXLOCK_FAIL, "y falla con cualquier tanda");
  }

  // Y coincide con la de una sentada, que es la que usan la migracion
  // y las pruebas.
  chk(flexLockVerify("4821"), "la de una sentada dice lo mismo");
  chk(!flexLockVerify("4820"), "tambien al fallar");

  // Cancelar deja el modulo limpio y no valida nada a medias.
  chk(flexLockVerifyBegin("4821"), "se empieza una verificacion");
  chk(flexLockVerifyActive(), "que queda viva");
  flexLockVerifyCancel();
  chk(!flexLockVerifyActive(), "cancelar la deja sin estado");
  chk(flexLockVerifyStep(FLEXLOCK_STEP_ITERS) == FLEXLOCK_FAIL,
      "y un paso sin verificacion en curso no puede validar nada");

  // Empezar otra CANCELA la anterior: nunca hay dos en vuelo.
  chk(flexLockVerifyBegin("0000"), "se empieza una");
  chk(flexLockVerifyBegin("4821"), "y otra la sustituye");
  int r = FLEXLOCK_BUSY;
  while(r == FLEXLOCK_BUSY) r = flexLockVerifyStep(FLEXLOCK_STEP_ITERS);
  chk(r == FLEXLOCK_OK, "el veredicto es el de la ULTIMA que se empezo");

  // Una clave mas larga de lo que cabe no se acepta en silencio.
  char largo[FLEXLOCK_SECRET_MAX + 8];
  memset(largo, 'a', sizeof(largo) - 1);
  largo[sizeof(largo) - 1] = 0;
  chk(!flexLockVerifyBegin(largo), "una clave imposiblemente larga se rechaza");

  flexLockClear();
}

// -------------------------------------------------------------
//  La de otra tarea (Flex Web Server) no puede tocar la de la
//  pantalla: se intercala una en medio de la otra y las dos tienen
//  que dar su veredicto correcto.
// -------------------------------------------------------------
static void testVerificacionSola(){
  printf("Verificacion desde otra tarea (Flex Web Server)\n");
  prefsTestClear();
  chk(!flexLockVerifyAlone("1234"), "sin clave guardada: no");
  chk(flexLockSet("Rio-2024!", 2), "se configura una contrasena");
  chk(flexLockVerifyAlone("Rio-2024!"), "la correcta: si");
  chk(!flexLockVerifyAlone("rio-2024!") && !flexLockVerifyAlone(""), "incorrecta o vacia: no");
  chk(!flexLockVerifyAlone(NULL), "nula: no");
  chk(flexLockVerifyBegin("Rio-2024!"), "la pantalla empieza a comprobar a plazos");
  int r = flexLockVerifyStep(100);
  chk(r == FLEXLOCK_BUSY, "y va por la mitad");
  chk(!flexLockVerifyAlone("mala") && flexLockVerifyAlone("Rio-2024!"), "entretanto, el servidor comprueba dos claves");
  chk(flexLockVerifyActive(), "la de la pantalla sigue viva");
  while(r == FLEXLOCK_BUSY) r = flexLockVerifyStep(FLEXLOCK_STEP_ITERS);
  chk(r == FLEXLOCK_OK, "y termina con SU veredicto, sin mezclas");
  chk(flexLockVerifyBegin("otra"), "otra vez, ahora con la clave mala en pantalla");
  chk(flexLockVerifyAlone("Rio-2024!"), "el servidor acierta en medio");
  r = FLEXLOCK_BUSY;
  while(r == FLEXLOCK_BUSY) r = flexLockVerifyStep(FLEXLOCK_STEP_ITERS);
  chk(r == FLEXLOCK_FAIL, "y la pantalla sigue fallando la suya");
  char largo[FLEXLOCK_SECRET_MAX + 8];
  memset(largo, 'a', sizeof(largo) - 1);
  largo[sizeof(largo) - 1] = 0;
  chk(!flexLockVerifyAlone(largo), "una clave imposiblemente larga se rechaza");
  flexLockClear();
  chk(!flexLockVerifyAlone("Rio-2024!"), "tras quitar la clave: no");
}

int main(){
  printf("\n=== FlexOS \xc2\xb7 clave del sistema ===\n");
  testKdf();
  testBloqueoSistema();
  testVerificacionAPlazos();
  testVerificacionSola();
  printf("=== %d comprobaciones, %d fallos ===\n", gChecks, gFails);
  return gFails ? 1 : 0;
}
