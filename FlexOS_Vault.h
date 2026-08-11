// #############################################################
// ##  FlexOS · FLEX VAULT (Carpeta segura)  ·  SOLO ESP32-P4
// ##  Cifrado autenticado real, gestion de claves y almacen
// ##  privado. Aqui NO hay ni una linea de interfaz.
// #############################################################
//
//  PARA QUE SIRVE ESTE MODULO
//  --------------------------
//  Flex Vault es la Carpeta segura de Flex OS Ultra: contenido y
//  datos de apps que viven CIFRADOS y separados del resto del
//  sistema. Este fichero es la mitad que no se ve: derivacion de
//  claves, cifrado, almacen, indice, registro de seguridad y
//  contador de intentos. La interfaz (Ajustes -> Seguridad y
//  privacidad -> Flex Vault) vive en FlexOS_Ultra.ino y solo llama
//  a las funciones de aqui.
//
//  Se separa en su propia unidad de traduccion por el mismo motivo
//  que FlexOS_FS.cpp y FlexOS_OTA.cpp: el .ino es enorme y mezclar
//  criptografia con codigo de dibujo es la forma mas rapida de que
//  una clave acabe en un printf de depuracion. Aqui dentro no se
//  imprime NUNCA una clave, ni un nombre de fichero privado, ni un
//  byte de contenido.
//
//  MODELO DE CLAVES (por que no se cifra "con la contrasena")
//  ---------------------------------------------------------
//  Si los ficheros se cifraran directamente con una clave derivada
//  de la contrasena, cambiar la contrasena obligaria a redescifrar
//  y volver a cifrar TODO. Aqui se usa el esquema de sobre:
//
//     contrasena --PBKDF2-HMAC-SHA256(sal, iters)--> KEK (32 B)
//     clave maestra MK (32 B) = ALEATORIA por cada Flex Vault
//     MK se guarda envuelta:  AES-256-GCM(KEK, nonce, MK)
//     clave de cada fichero:  HMAC-SHA256(MK, "fxv-file-v1"|id|salt)
//     clave del indice:       HMAC-SHA256(MK, "fxv-meta-v1"|salt)
//
//  Consecuencias reales de esto:
//    · La contrasena DESENVUELVE la clave maestra; no es la clave de
//      cifrado de los ficheros.
//    · Cambiar la contrasena solo reescribe 76 bytes de sobre. Los
//      ficheros no se tocan y no se pierde nada.
//    · Cada fichero tiene su propia clave, asi que un fallo en uno
//      no compromete al resto.
//    · El tag GCM del sobre ES la verificacion de la contrasena: no
//      hace falta guardar aparte ningun "hash de la clave". Si la
//      contrasena es incorrecta, el desenvuelto falla la
//      autenticacion y no se obtiene ninguna clave.
//
//  POR QUE AES-256-GCM
//  -------------------
//  Es cifrado autenticado (AEAD): protege contenido Y detecta
//  manipulacion, que es exactamente lo que hace falta cuando los
//  bytes viven en una particion que cualquiera puede reescribir con
//  un lector de flash. Y el ESP32-P4 lo trae en hardware: mbedTLS,
//  tal como viene en el core de arduino-esp32, usa el acelerador AES
//  del chip, asi que el coste por bloque es una fraccion del de una
//  implementacion en C. No se usa AES-CBC + HMAC (dos pasadas, mas
//  codigo, mas formas de equivocarse) ni ChaCha20-Poly1305 (correcto
//  pero SIN acelerador en este chip: seria mas lento aqui).
//
//  POR QUE PBKDF2 Y NO scrypt / Argon2
//  -----------------------------------
//  En el stack de ESP-IDF/arduino-esp32 no hay scrypt ni Argon2. Lo
//  que hay es PBKDF2-HMAC-SHA256, que es una funcion de derivacion
//  estandar y aceptable siempre que el numero de iteraciones sea
//  alto. Se implementa aqui sobre HMAC-SHA256 (20 lineas) en vez de
//  llamar a mbedtls_pkcs5_*: esa API cambio de nombre entre mbedTLS
//  2.x y 3.x y no merece la pena atarse a ella. El numero de
//  iteraciones se GUARDA en cada boveda, asi que subirlo en el
//  futuro no invalida las bovedas ya creadas.
//
//  LIMITE HONESTO DE SEGURIDAD (hay que decirlo)
//  ---------------------------------------------
//  Por peticion expresa, en esta version NO se activan Secure Boot,
//  Flash Encryption, eFuses ni modo produccion. Eso significa que
//  quien tenga la placa en la mano puede leer la flash y obtener la
//  sal y la clave maestra ENVUELTA. Lo que le protege entonces es
//  unicamente el coste de PBKDF2 sobre su contrasena: una
//  contrasena buena sigue siendo inviable de romper, un PIN de 4
//  digitos NO lo es frente a un ataque con la flash volcada. El
//  modulo lo refleja en la interfaz en vez de prometer lo que no
//  puede cumplir. La arquitectura ya esta lista para que activar
//  Flash Encryption mas adelante cierre ese hueco sin cambiar el
//  formato (ver FLEXVAULT_FORMAT_VERSION).

#pragma once
#include <Arduino.h>

// -------------------------------------------------------------
//  Version del formato en disco. Se guarda en cada blob y en el
//  sobre. Si algun dia cambia el esquema, esto permite leer lo
//  viejo en vez de dejar al usuario sin sus ficheros.
// -------------------------------------------------------------
#define FLEXVAULT_FORMAT_VERSION 1

// Iteraciones de PBKDF2 con las que se crean bovedas NUEVAS. Cada
// boveda recuerda las suyas, asi que subir esto no rompe nada.
// 25.000 con el acelerador SHA del P4 es del orden de medio segundo:
// bastante para encarecer la fuerza bruta, poco para que abrir la
// carpeta se sienta lento.
#define FLEXVAULT_KDF_ITERS  25000

// Tamano de bloque de plano. Un fichero grande NO se carga entero en
// RAM: se cifra y descifra bloque a bloque, con su propio tag.
#define FLEXVAULT_CHUNK      4096

// Topes del almacen. Cortos a proposito: el indice vive en un buffer
// contiguo y tiene que caber sin pelearse con el resto del sistema.
#define FLEXVAULT_MAX_ITEMS  96
#define FLEXVAULT_NAME_MAX   48
#define FLEXVAULT_ORIGIN_MAX 96
#define FLEXVAULT_LOG_MAX    64

// Longitud maxima de PIN/contrasena que se acepta. La misma que usa
// el bloqueo del sistema.
#define FLEXVAULT_SECRET_MAX 64

// Tipos de clave de la boveda. Son los mismos valores que gLockType
// en el sketch para que no haya dos convenios distintos.
#define FLEXVAULT_LOCK_NONE  0
#define FLEXVAULT_LOCK_PIN   1
#define FLEXVAULT_LOCK_PASS  2

// Clase de contenido. Determina en que seccion de la boveda aparece
// (Galeria privada / Notas privadas / Archivos privados) y nada mas:
// el cifrado es identico para todas.
enum {
  FXV_KIND_ANY   = -1,
  FXV_KIND_PHOTO = 0,   // imagenes y dibujos -> Galeria privada
  FXV_KIND_NOTE  = 1,   // texto              -> Notas privadas
  FXV_KIND_FILE  = 2,   // cualquier otro     -> Archivos privados
  FXV_KIND_APP   = 3    // datos de una app privada (no se listan como contenido)
};

// Eventos del registro de seguridad. Se guardan como CODIGO, nunca
// como texto: el registro no puede revelar nombres ni contenido.
enum {
  FXV_EV_CREATE   = 1,   // boveda creada
  FXV_EV_UNLOCK   = 2,   // apertura correcta
  FXV_EV_FAIL     = 3,   // intento fallido
  FXV_EV_LOCK     = 4,   // cierre (manual, inactividad, pantalla, reinicio)
  FXV_EV_CHANGEKEY= 5,   // cambio de clave
  FXV_EV_IN       = 6,   // entro un elemento
  FXV_EV_OUT      = 7,   // salio un elemento a la app normal
  FXV_EV_DEL      = 8,   // elemento borrado definitivamente
  FXV_EV_APPADD   = 9,   // app anadida a la boveda
  FXV_EV_APPDEL   = 10,  // app quitada de la boveda
  FXV_EV_WIPE     = 11   // datos de una app eliminados definitivamente
};

// Motivo del cierre, para el registro. Sirve para poder auditar
// "se cerro porque se apago la pantalla" sin guardar nada privado.
enum {
  FXV_LOCK_MANUAL = 0,
  FXV_LOCK_IDLE   = 1,   // paso el tiempo configurado sin actividad
  FXV_LOCK_SCREEN = 2,   // pantalla apagada / suspension / bloqueo
  FXV_LOCK_EXIT   = 3,   // el usuario salio de la boveda
  FXV_LOCK_BOOT   = 4    // arranque: la boveda nunca queda abierta
};

// Que hacer con los datos al quitar una app de la boveda.
enum {
  FXV_APPDATA_EXPORT = 0,   // devolverlos a la app normal
  FXV_APPDATA_KEEP   = 1,   // mantenerlos cifrados dentro
  FXV_APPDATA_WIPE   = 2    // eliminarlos definitivamente
};

// Resultados de flexVaultUnlock / flexVaultChangeSecret.
enum {
  FXV_OK        = 0,
  FXV_ERR_WRONG = -1,   // clave incorrecta (cuenta como fallo)
  FXV_ERR_WAIT  = -2,   // hay una espera por intentos fallidos en curso
  FXV_ERR_STATE = -3,   // no existe la boveda, o ya esta abierta/cerrada
  FXV_ERR_IO    = -4,   // el almacen no responde
  FXV_ERR_ARG   = -5    // parametro invalido (clave demasiado corta, etc.)
};

// Un elemento del indice, tal como lo ve la interfaz. Es una COPIA:
// nunca se devuelve un puntero al indice interno, para que la UI
// pueda quedarse la lista mientras dibuja (igual que FlexFsEntry).
struct FlexVaultItem {
  uint16_t id;
  int8_t   kind;
  int8_t   appId;                        // -1 = no pertenece a una app privada
  uint32_t size;                         // bytes de PLANO
  uint32_t added;                        // epoch de entrada (0 = sin reloj)
  uint32_t lastAcc;                      // epoch del ultimo acceso
  char     name[FLEXVAULT_NAME_MAX];     // nombre visible (ya descifrado)
};

// Una linea del registro de seguridad.
struct FlexVaultLog {
  uint32_t ts;      // epoch (0 si no habia hora)
  uint8_t  ev;      // FXV_EV_*
  int8_t   aux;     // motivo de cierre / appId / -1
};

// -------------------------------------------------------------
//  ARRANQUE Y ESTADO
// -------------------------------------------------------------
// Carga el sobre y los contadores de NVS. NO desbloquea nada: tras
// un arranque la boveda SIEMPRE esta cerrada, por diseno. Devuelve
// false solo si el almacen no esta disponible.
bool     flexVaultBegin();

// true si el usuario ya creo su Flex Vault.
bool     flexVaultExists();

// true mientras la clave maestra esta en RAM (boveda abierta).
bool     flexVaultUnlocked();

// FLEXVAULT_LOCK_PIN o FLEXVAULT_LOCK_PASS (0 si no existe).
int      flexVaultLockType();

// Longitud del PIN guardado. Sirve para que la pantalla de PIN pueda
// autoconfirmar al llegar al numero de digitos, igual que hace el
// bloqueo del sistema. Solo revela la longitud, que los puntos de la
// pantalla ya ensenan. Para contrasena devuelve 0.
int      flexVaultSecretLen();

// Tiempo de bloqueo automatico por inactividad, en ms (0 = nunca).
uint32_t flexVaultAutoLockMs();
void     flexVaultSetAutoLockMs(uint32_t ms);

// Epoch del ultimo desbloqueo correcto (0 = nunca).
uint32_t flexVaultLastAccess();

// Intentos fallidos acumulados. Persisten en NVS: reiniciar la placa
// NO es una via de escape.
int      flexVaultFails();

// Espera pendiente por intentos fallidos, en ms (0 = se puede
// intentar ya). Crece con los fallos y sobrevive al reinicio.
uint32_t flexVaultWaitMs();

// Bytes REALES que ocupa el almacen cifrado (recorre el directorio).
uint32_t flexVaultUsedBytes();

// Numero de elementos de una clase (FXV_KIND_* o FXV_KIND_ANY).
int      flexVaultCount(int kind);

// Motivo legible del ultimo fallo, para poder ensenarlo. Nunca
// incluye nombres de ficheros privados ni fragmentos de contenido.
const char* flexVaultError();

// -------------------------------------------------------------
//  CREACION, APERTURA Y CIERRE
// -------------------------------------------------------------
// Crea la boveda con su propia clave, distinta del bloqueo general.
// Genera la clave maestra con el generador de numeros aleatorios del
// hardware y la guarda envuelta. Deja la boveda ABIERTA (el usuario
// acaba de demostrar que sabe la clave).
int      flexVaultCreate(const char* secret, int lockType);

// Desenvuelve la clave maestra. FXV_OK, FXV_ERR_WRONG (suma fallo y
// arma la espera), FXV_ERR_WAIT o FXV_ERR_STATE.
int      flexVaultUnlock(const char* secret);

// Cierra: borra de RAM la clave maestra y cualquier resto sensible.
// Idempotente, asi que se puede llamar desde todos los caminos de
// cierre sin comprobar nada antes.
void     flexVaultLock(int reason);

// Cambia la clave SIN tocar los ficheros: solo se vuelve a envolver
// la clave maestra. Requiere la clave actual.
int      flexVaultChangeSecret(const char* oldSecret, const char* newSecret, int lockType);

// -------------------------------------------------------------
//  CONTENIDO
// -------------------------------------------------------------
// Lista elementos de una clase, mas recientes primero.
int      flexVaultList(int kind, FlexVaultItem* out, int maxn);

// Datos de un elemento por id.
bool     flexVaultGet(uint16_t id, FlexVaultItem* out);

// Mueve un fichero normal DENTRO de la boveda: lo cifra por bloques
// y solo despues borra el original. Si algo falla, el original sigue
// donde estaba (nunca se pierde un fichero por un error de cifrado).
bool     flexVaultImport(const char* path, int kind, int appId);

// Devuelve un elemento a la app normal, descifrandolo por bloques a
// su ruta original (o a un nombre libre si ya hay algo ahi). Al
// terminar, el elemento desaparece de la boveda.
bool     flexVaultExport(uint16_t id, char* outPath, size_t n);

// Borrado definitivo dentro de la boveda.
bool     flexVaultDelete(uint16_t id);

// Renombra (el nombre vive cifrado en el indice).
bool     flexVaultRename(uint16_t id, const char* name);

// Crea un elemento vacio (lo usan las Notas privadas). Devuelve su id.
bool     flexVaultCreateItem(const char* name, int kind, int appId, uint16_t* outId);

// Reemplaza el contenido de un elemento. Cada escritura usa una sal
// de escritura nueva, asi que la clave de fichero cambia y no puede
// repetirse un par (clave, nonce).
bool     flexVaultWrite(uint16_t id, const void* buf, size_t n);

// Descifra un elemento a `buf`. Devuelve los bytes escritos, o -1 si
// no se pudo (clave cerrada, autenticacion fallida, no cabe).
int      flexVaultRead(uint16_t id, void* buf, size_t n);

// Version por bloques para ficheros grandes: llama a `cb` con cada
// bloque de plano ya autenticado, sin tenerlo todo en RAM a la vez.
// Devolver false en el callback aborta la lectura.
typedef bool (*FlexVaultChunkCb)(void* user, uint32_t off, const uint8_t* data, size_t n);
bool     flexVaultReadStream(uint16_t id, FlexVaultChunkCb cb, void* user);

// -------------------------------------------------------------
//  APPS PRIVADAS
// -------------------------------------------------------------
// Apps que Flex OS Ultra puede separar de verdad. La lista NO es
// decorativa: solo entra una app si TODO su estado persistente se
// puede redirigir al almacen cifrado. Ver flexVaultAppSupported.
bool     flexVaultAppSupported(int appId);

// Motivo por el que una app NO es compatible (texto para la
// interfaz). Devuelve NULL si si lo es.
const char* flexVaultAppReason(int appId);

// App critica del sistema: nunca puede entrar en la boveda.
bool     flexVaultAppForbidden(int appId);

bool     flexVaultAppAdded(int appId);
bool     flexVaultAppAdd(int appId);
// dataAction = FXV_APPDATA_*
bool     flexVaultAppRemove(int appId, int dataAction);

// Candado por app dentro de la boveda ("Bloquear" en Gestionar apps
// privadas): al abrirla se vuelve a pedir la clave de la boveda.
bool     flexVaultAppLocked(int appId);
void     flexVaultAppSetLocked(int appId, bool on);

// Bytes cifrados y ultimo acceso de una app privada.
uint32_t flexVaultAppBytes(int appId);
uint32_t flexVaultAppLast(int appId);
void     flexVaultAppTouch(int appId);

// -------------------------------------------------------------
//  REGISTRO DE SEGURIDAD
// -------------------------------------------------------------
// Lee el registro, mas reciente primero. Solo codigos y marcas de
// tiempo: jamas nombres ni contenido.
int      flexVaultLogRead(FlexVaultLog* out, int maxn);
void     flexVaultLogAdd(uint8_t ev, int8_t aux);

// -------------------------------------------------------------
//  RELOJ
// -------------------------------------------------------------
// El modulo no sabe la hora: se la da el sketch (que la obtiene por
// NTP). Si nunca se llama, las marcas de tiempo valen 0 y la
// interfaz lo dice ("sin hora"), en vez de inventar una fecha.
void     flexVaultSetClock(uint32_t epoch);
uint32_t flexVaultNow();

// -------------------------------------------------------------
//  DERIVACION DE CLAVES REUTILIZABLE
//  ------------------------------------------------------------
//  El bloqueo del sistema (Ajustes -> Seguridad -> Bloqueo) guardaba
//  el PIN y la contrasena EN TEXTO PLANO en NVS. Se corrige usando
//  estas dos funciones desde el sketch, en vez de duplicar ahi una
//  segunda implementacion de PBKDF2. Son las mismas primitivas que
//  usa la boveda, con su propia sal.
// -------------------------------------------------------------
// Sal aleatoria del generador del hardware.
void     flexVaultRandomBytes(void* out, size_t n);

// PBKDF2-HMAC-SHA256. `outLen` suele ser 32.
void     flexVaultKdf(const char* secret, const uint8_t* salt, size_t saltLen,
                      uint32_t iters, uint8_t* out, size_t outLen);

// Comparacion en tiempo constante (para no filtrar por cuanto tarda
// en fallar cuantos bytes coincidian).
bool     flexVaultEqualCT(const void* a, const void* b, size_t n);

// Borrado de memoria sensible que el compilador NO puede eliminar
// por "codigo muerto".
void     flexVaultWipe(void* p, size_t n);

// -------------------------------------------------------------
//  BLOQUEO DEL SISTEMA (Ajustes -> Seguridad y privacidad ->
//  Bloqueo)  ·  ahora con hash y sal
//  ------------------------------------------------------------
//  Hasta esta version, el PIN y la contrasena de la PANTALLA DE
//  BLOQUEO se guardaban en NVS TAL CUAL, en texto legible (claves
//  "lockpin" y "lockpass"). Cualquiera que volcara la particion de
//  NVS los leia directamente, y ademas quedaban ahi para siempre.
//
//  Estas funciones lo sustituyen por sal aleatoria de 16 bytes +
//  PBKDF2-HMAC-SHA256, comparado en tiempo constante. Viven en este
//  modulo, y no en el sketch, por dos razones: la criptografia ya
//  esta aqui (no hay que duplicar PBKDF2) y asi se puede probar en el
//  PC con NVS y cripto de verdad.
//
//  MIGRACION DE QUIEN YA TIENE CLAVE PUESTA
//  flexLockMigrate() se llama en cada arranque. Si encuentra una
//  clave en texto legible, la convierte a hash con sal y BORRA la
//  clave antigua de NVS. El usuario no nota nada: sigue entrando con
//  el mismo PIN. Si no hay nada que migrar, no escribe.
//
//  LO QUE SE SIGUE GUARDANDO EN CLARO, Y POR QUE
//  La LONGITUD del PIN (no el PIN). La pantalla de bloqueo
//  autoconfirma al llegar al numero de digitos guardado, que es como
//  se comportaba antes y lo que hace cualquier telefono. Los puntos
//  en pantalla ya revelan esa longitud mientras se escribe, asi que
//  guardarla no anade ninguna informacion que un observador no tenga.
// -------------------------------------------------------------
// 0 = sin clave, 1 = PIN, 2 = contrasena.
int      flexLockType();
// Longitud del PIN guardado (0 si es contrasena o no hay clave).
int      flexLockLen();
// Guarda una clave nueva (genera sal nueva). type = 1 PIN, 2 contrasena.
bool     flexLockSet(const char* secret, int type);
// Comprueba una clave contra el hash guardado, en tiempo constante.
bool     flexLockVerify(const char* secret);
// Quita la clave del sistema (deja el bloqueo en "Deslizar").
bool     flexLockClear();
// 1 = habia una clave en texto legible y se ha migrado a hash,
// 0 = no habia nada que migrar, -1 = no se pudo completar.
int      flexLockMigrate();
