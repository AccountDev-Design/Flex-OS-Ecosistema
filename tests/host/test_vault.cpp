// #############################################################
//  PRUEBAS DE HOST DE FLEX VAULT (Carpeta segura)
//  ------------------------------------------------------------
//  Compila y ejecuta FlexOS_Vault.cpp EN EL PC, con:
//    · un sistema de archivos en memoria que implementa la misma
//      API que FlexOS_FS.cpp (incluidas las funciones flexFsPriv*),
//    · una NVS en memoria (stub/Preferences.h),
//    · OpenSSL como backend criptografico.
//
//  QUE SE COMPRUEBA DE VERDAD AQUI
//    1. Que PBKDF2-HMAC-SHA256 de este modulo da el resultado
//       correcto (vectores del RFC 6070 adaptados a SHA-256).
//    2. Que la contrasena DESENVUELVE la clave maestra y no es la
//       clave de los ficheros: cambiarla no toca ni un byte de los
//       blobs y los ficheros siguen abriendose.
//    3. Que un solo byte cambiado en un blob, en la cabecera, en el
//       tag o en el ORDEN de los bloques hace que el descifrado
//       falle en vez de devolver datos manipulados.
//    4. Que un fichero mas grande que un bloque se cifra y descifra
//       por bloques y vuelve identico.
//    5. Que el contador de intentos persiste y que las esperas
//       crecen.
//    6. Que mover un elemento a la boveda lo QUITA del sistema de
//       archivos normal, y que devolverlo lo restaura igual.
//    7. Que ni un nombre ni un byte de contenido privado aparecen
//       en los bytes que quedan en la particion.
//
//  LIMITE HONESTO: la primitiva AES-GCM que se ejercita aqui es la
//  de OpenSSL; en la placa es la de mbedTLS con el acelerador del
//  ESP32-P4. Lo que estas pruebas verifican es la LOGICA de esta
//  capa (esquema de sobre, derivacion, troceado, formato, indice,
//  bloqueo), que es la parte que se escribio aqui y la que puede
//  estar mal. Ver el comentario del backend en FlexOS_Vault.cpp.
// #############################################################
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <map>
#include <vector>
#include <algorithm>

#include "Arduino.h"
#include "Preferences.h"
#include "../../FlexOS_FS.h"
#include "../../FlexOS_Vault.h"

// -------------------------------------------------------------
//  Reloj virtual y utilidades del arnes
// -------------------------------------------------------------
static unsigned long gMs = 1000;
unsigned long millis(){ return gMs; }
unsigned long micros(){ return gMs * 1000; }
void delay(unsigned long ms){ gMs += ms; }

static int gFails = 0, gChecks = 0;
static void chk(bool ok, const char* what){
  gChecks++;
  if(!ok){ printf("  FALLO: %s\n", what); gFails++; }
}

// -------------------------------------------------------------
//  SISTEMA DE ARCHIVOS EN MEMORIA
//  ------------------------------------------------------------
//  Implementa la misma API que FlexOS_FS.cpp. No es un doble que
//  devuelve "no disponible": guarda bytes de verdad, para que las
//  pruebas ejerciten el troceado, los desplazamientos y el borrado
//  igual que en la placa. El filtro de la boveda se reproduce tal
//  cual (flexFsIsVaultPath), porque es parte de lo que hay que
//  comprobar.
// -------------------------------------------------------------
static std::map<std::string, std::vector<uint8_t>> gFs;
static bool gMounted = true;

bool flexFsIsVaultPath(const char* path){
  if(!path || path[0] != '/') return false;
  size_t vl = strlen(FLEXFS_DIR_VAULT);
  if(strncmp(path, FLEXFS_DIR_VAULT, vl) != 0) return false;
  return path[vl] == 0 || path[vl] == '/';
}

bool flexFsReady(){ return gMounted; }
const char* flexFsError(){ return gMounted ? "" : "sin montar"; }
bool flexFsVaultInit(){ return gMounted; }

static std::vector<uint8_t>* fsGet(const char* p){
  auto it = gFs.find(p);
  return it == gFs.end() ? NULL : &it->second;
}

bool flexFsExists(const char* path){
  if(flexFsIsVaultPath(path)) return false;
  return fsGet(path) != NULL;
}
bool flexFsIsDir(const char* path){ (void)path; return false; }
uint32_t flexFsSize(const char* path){
  if(flexFsIsVaultPath(path)) return 0;
  std::vector<uint8_t>* v = fsGet(path);
  return v ? (uint32_t)v->size() : 0;
}
bool flexFsDelete(const char* path){
  if(flexFsIsVaultPath(path)) return false;
  return gFs.erase(path) > 0;
}
int flexFsReadAt(const char* path, uint32_t off, void* buf, size_t n){
  if(flexFsIsVaultPath(path)) return -1;
  std::vector<uint8_t>* v = fsGet(path);
  if(!v) return -1;
  if(off >= v->size()) return 0;
  size_t c = v->size() - off;
  if(c > n) c = n;
  memcpy(buf, v->data() + off, c);
  return (int)c;
}
bool flexFsAppendBin(const char* path, const void* buf, size_t n){
  if(flexFsIsVaultPath(path)) return false;
  std::vector<uint8_t>& v = gFs[path];
  const uint8_t* p = (const uint8_t*)buf;
  for(size_t i = 0; i < n; i++) v.push_back(p[i]);
  return true;
}
bool flexFsWriteBin(const char* path, const void* buf, size_t n){
  if(flexFsIsVaultPath(path)) return false;
  std::vector<uint8_t>& v = gFs[path];
  v.assign((const uint8_t*)buf, (const uint8_t*)buf + n);
  return true;
}
void flexFsStem(const char* name, char* out, size_t n){
  const char* nm = strrchr(name, '/');
  nm = nm ? nm + 1 : name;
  const char* d = strrchr(nm, '.');
  size_t len = d ? (size_t)(d - nm) : strlen(nm);
  if(len >= n) len = n - 1;
  memcpy(out, nm, len);
  out[len] = 0;
}

// ---- acceso privilegiado (solo rutas de la boveda) ----
bool flexFsPrivExists(const char* path){
  return flexFsIsVaultPath(path) && fsGet(path) != NULL;
}
uint32_t flexFsPrivSize(const char* path){
  if(!flexFsIsVaultPath(path)) return 0;
  std::vector<uint8_t>* v = fsGet(path);
  return v ? (uint32_t)v->size() : 0;
}
int flexFsPrivRead(const char* path, uint32_t off, void* buf, size_t n){
  if(!flexFsIsVaultPath(path)) return -1;
  std::vector<uint8_t>* v = fsGet(path);
  if(!v) return -1;
  if(off >= v->size()) return 0;
  size_t c = v->size() - off;
  if(c > n) c = n;
  memcpy(buf, v->data() + off, c);
  return (int)c;
}
bool flexFsPrivAppend(const char* path, const void* buf, size_t n){
  if(!flexFsIsVaultPath(path)) return false;
  std::vector<uint8_t>& v = gFs[path];
  const uint8_t* p = (const uint8_t*)buf;
  for(size_t i = 0; i < n; i++) v.push_back(p[i]);
  return true;
}
bool flexFsPrivWrite(const char* path, const void* buf, size_t n){
  if(!flexFsIsVaultPath(path)) return false;
  std::vector<uint8_t>& v = gFs[path];
  v.assign((const uint8_t*)buf, (const uint8_t*)buf + n);
  return true;
}
bool flexFsPrivDelete(const char* path){
  if(!flexFsIsVaultPath(path)) return false;
  gFs.erase(path);
  return true;
}
uint32_t flexFsPrivDirSize(const char* dir){
  if(!flexFsIsVaultPath(dir)) return 0;
  uint32_t total = 0;
  size_t dl = strlen(dir);
  for(auto& kv : gFs){
    if(kv.first.compare(0, dl, dir) != 0) continue;
    // Solo los hijos DIRECTOS, igual que la version de LittleFS.
    const char* rest = kv.first.c_str() + dl;
    if(*rest != '/') continue;
    if(strchr(rest + 1, '/')) continue;
    total += (uint32_t)kv.second.size();
  }
  return total;
}

// -------------------------------------------------------------
//  Vectores de PBKDF2-HMAC-SHA256 (RFC 6070 con SHA-256)
// -------------------------------------------------------------
static void hexdump(const uint8_t* p, size_t n, char* out){
  for(size_t i = 0; i < n; i++) sprintf(out + i * 2, "%02x", p[i]);
}

static void testKdf(){
  printf("Derivacion de clave (PBKDF2-HMAC-SHA256)\n");
  uint8_t dk[32]; char hex[80];

  flexVaultKdf("password", (const uint8_t*)"salt", 4, 1, dk, 32);
  hexdump(dk, 32, hex);
  chk(!strcmp(hex, "120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b"),
      "vector 1 (1 iteracion)");

  flexVaultKdf("password", (const uint8_t*)"salt", 4, 2, dk, 32);
  hexdump(dk, 32, hex);
  chk(!strcmp(hex, "ae4d0c95af6b46d32d0adff928f06dd02a303f8ef3c251dfd6e2d85a95474c43"),
      "vector 2 (2 iteraciones)");

  flexVaultKdf("password", (const uint8_t*)"salt", 4, 4096, dk, 32);
  hexdump(dk, 32, hex);
  chk(!strcmp(hex, "c5e478d59288c841aa530db6845c4c8d962893a001ce4e11a4963873aa98134a"),
      "vector 3 (4096 iteraciones)");

  // Una sal distinta con la misma contrasena da una clave distinta:
  // es LA razon de que exista la sal.
  uint8_t a[32], b[32];
  flexVaultKdf("clave", (const uint8_t*)"AAAAAAAAAAAAAAAA", 16, 100, a, 32);
  flexVaultKdf("clave", (const uint8_t*)"BBBBBBBBBBBBBBBB", 16, 100, b, 32);
  chk(memcmp(a, b, 32) != 0, "la sal cambia la clave derivada");

  chk(flexVaultEqualCT(a, a, 32),  "comparacion en tiempo constante: iguales");
  chk(!flexVaultEqualCT(a, b, 32), "comparacion en tiempo constante: distintos");

  uint8_t z[16]; memset(z, 0xAA, sizeof(z));
  flexVaultWipe(z, sizeof(z));
  bool zero = true;
  for(size_t i = 0; i < sizeof(z); i++) if(z[i]) zero = false;
  chk(zero, "el borrado de memoria sensible deja ceros");
}

// -------------------------------------------------------------
//  Ciclo de vida
// -------------------------------------------------------------
static void testCiclo(){
  printf("Creacion, apertura y cierre\n");
  gFs.clear();
  prefsTestClear();
  chk(flexVaultBegin(), "arranca con almacenamiento disponible");
  chk(!flexVaultExists(), "al principio no hay boveda");
  chk(!flexVaultUnlocked(), "y por tanto esta cerrada");

  flexVaultSetClock(1700000000u);
  chk(flexVaultCreate("123", FLEXVAULT_LOCK_PIN) == FXV_ERR_ARG,
      "una clave de 3 caracteres se rechaza");
  chk(flexVaultCreate("2468", FLEXVAULT_LOCK_PIN) == FXV_OK, "se crea con PIN de 4");
  chk(flexVaultExists(),   "ya existe");
  chk(flexVaultUnlocked(), "y queda abierta tras crearla");
  chk(flexVaultLockType() == FLEXVAULT_LOCK_PIN, "el tipo de clave se recuerda");
  chk(flexVaultSecretLen() == 4, "la longitud del PIN se recuerda (para autoconfirmar)");
  chk(flexVaultCreate("9999", FLEXVAULT_LOCK_PIN) == FXV_ERR_STATE,
      "no se puede crear dos veces");

  flexVaultLock(FXV_LOCK_MANUAL);
  chk(!flexVaultUnlocked(), "se cierra a mano");

  chk(flexVaultUnlock("1111") == FXV_ERR_WRONG, "una clave incorrecta no abre");
  chk(!flexVaultUnlocked(), "y sigue cerrada");
  chk(flexVaultFails() == 1, "el fallo se cuenta");
  chk(flexVaultUnlock("2468") == FXV_OK, "la clave correcta abre");
  chk(flexVaultFails() == 0, "un acierto pone el contador a cero");

  // Un arranque nuevo (como reiniciar la placa): la boveda NO puede
  // quedar abierta.
  chk(flexVaultBegin(), "vuelve a arrancar");
  chk(flexVaultExists(),   "la boveda sigue existiendo tras el reinicio");
  chk(!flexVaultUnlocked(), "tras un reinicio la boveda esta SIEMPRE cerrada");
  chk(flexVaultUnlock("2468") == FXV_OK, "y se abre con la misma clave");
}

// -------------------------------------------------------------
//  Contenido: entrada, salida, troceado
// -------------------------------------------------------------
static bool fsHas(const char* path){ return gFs.find(path) != gFs.end(); }

// Busca una secuencia de bytes en TODO lo que hay en la particion.
// Es la comprobacion de que nada privado se queda en claro.
static bool fsContains(const void* needle, size_t n){
  const uint8_t* p = (const uint8_t*)needle;
  for(auto& kv : gFs){
    const std::vector<uint8_t>& v = kv.second;
    if(v.size() < n) continue;
    for(size_t i = 0; i + n <= v.size(); i++)
      if(!memcmp(v.data() + i, p, n)) return true;
  }
  return false;
}

static void testContenido(){
  printf("Mover contenido a la boveda y devolverlo\n");
  gFs.clear();
  prefsTestClear();
  flexVaultBegin();
  flexVaultSetClock(1700000000u);
  chk(flexVaultCreate("clave-larga-de-prueba", FLEXVAULT_LOCK_PASS) == FXV_OK,
      "se crea con contrasena");

  const char* secreto = "esto-es-el-contenido-privado-de-la-nota";
  flexFsWriteBin("/Notas/Privada.txt", secreto, strlen(secreto));
  chk(fsHas("/Notas/Privada.txt"), "el fichero normal existe antes de moverlo");
  chk(fsContains(secreto, strlen(secreto)), "y su contenido esta en claro en la particion");

  chk(flexVaultImport("/Notas/Privada.txt", FXV_KIND_NOTE, -1), "se mueve a la boveda");
  chk(!fsHas("/Notas/Privada.txt"), "DESAPARECE del sistema de archivos normal");
  chk(!flexFsExists("/Notas/Privada.txt"), "y la capa normal ya no lo ve");
  chk(!fsContains(secreto, strlen(secreto)),
      "el contenido ya NO aparece en claro en ningun byte de la particion");
  chk(!fsContains("Privada.txt", 11),
      "el NOMBRE tampoco aparece en claro (vive cifrado en el indice)");
  chk(flexVaultCount(FXV_KIND_NOTE) == 1, "aparece en Notas privadas");
  chk(flexVaultCount(FXV_KIND_PHOTO) == 0, "y no en Galeria privada");

  FlexVaultItem it[8];
  int n = flexVaultList(FXV_KIND_NOTE, it, 8);
  chk(n == 1, "la lista privada tiene un elemento");
  chk(!strcmp(it[0].name, "Privada.txt"), "con su nombre real ya descifrado");
  chk(it[0].size == strlen(secreto), "y su tamano real");

  char buf[128];
  int rd = flexVaultRead(it[0].id, buf, sizeof(buf));
  chk(rd == (int)strlen(secreto) && !memcmp(buf, secreto, rd),
      "se descifra byte a byte igual que entro");

  // Con la boveda cerrada no hay forma de leerlo.
  uint16_t id = it[0].id;
  flexVaultLock(FXV_LOCK_EXIT);
  chk(flexVaultRead(id, buf, sizeof(buf)) < 0, "cerrada, no se puede leer");
  chk(flexVaultList(FXV_KIND_NOTE, it, 8) == 0, "cerrada, no se puede listar");
  chk(flexVaultCount(FXV_KIND_ANY) == 0, "cerrada, ni siquiera se cuenta");
  flexVaultUnlock("clave-larga-de-prueba");

  // Devolverlo a la app normal.
  char out[FLEXFS_PATH_MAX];
  chk(flexVaultExport(id, out, sizeof(out)), "se devuelve a la app normal");
  chk(!strcmp(out, "/Notas/Privada.txt"), "a su ruta original");
  chk(fsHas("/Notas/Privada.txt"), "el fichero vuelve a existir");
  chk(flexFsSize("/Notas/Privada.txt") == strlen(secreto), "con su tamano original");
  chk(fsContains(secreto, strlen(secreto)), "y su contenido intacto");
  chk(flexVaultCount(FXV_KIND_NOTE) == 0, "y ya no esta en la boveda");
}

static void testTroceado(){
  printf("Ficheros grandes por bloques\n");
  gFs.clear();
  prefsTestClear();
  flexVaultBegin();
  flexVaultCreate("clave-de-prueba", FLEXVAULT_LOCK_PASS);

  // 10 KB y pico: fuerza tres bloques y un ultimo bloque PARCIAL,
  // que es donde se rompen las implementaciones que dan por hecho
  // que todos los bloques son del mismo tamano.
  const size_t N = FLEXVAULT_CHUNK * 2 + 1234;
  std::vector<uint8_t> big(N);
  for(size_t i = 0; i < N; i++) big[i] = (uint8_t)((i * 31 + 7) & 0xFF);
  flexFsWriteBin("/Documentos/foto.jpg", big.data(), N);

  chk(flexVaultImport("/Documentos/foto.jpg", FXV_KIND_PHOTO, -1),
      "un fichero de varios bloques entra en la boveda");
  FlexVaultItem it[4];
  chk(flexVaultList(FXV_KIND_PHOTO, it, 4) == 1, "aparece en Galeria privada");
  chk(it[0].size == N, "con su tamano exacto");

  std::vector<uint8_t> back(N + 16, 0);
  int rd = flexVaultRead(it[0].id, back.data(), back.size());
  chk(rd == (int)N, "se descifra el tamano exacto");
  chk(!memcmp(back.data(), big.data(), N), "y los bytes son identicos");

  // Lectura por bloques: la que usa la interfaz para no llenar la RAM.
  struct Acc { std::vector<uint8_t> v; };
  Acc acc;
  auto cb = [](void* user, uint32_t off, const uint8_t* d, size_t n) -> bool {
    Acc* a = (Acc*)user;
    if(a->v.size() != off) return false;          // los bloques llegan en orden
    a->v.insert(a->v.end(), d, d + n);
    return true;
  };
  chk(flexVaultReadStream(it[0].id, cb, &acc), "la lectura por bloques funciona");
  chk(acc.v.size() == N && !memcmp(acc.v.data(), big.data(), N),
      "y entrega el fichero completo y en orden");

  chk(!fsContains(big.data(), 256), "ningun trozo del original queda en claro");
}

// -------------------------------------------------------------
//  Integridad: manipular los bytes tiene que NOTARSE
// -------------------------------------------------------------
static std::string blobPathOf(uint16_t id){
  char p[64];
  snprintf(p, sizeof(p), "%s/%u.b", FLEXFS_DIR_VAULTD, (unsigned)id);
  return std::string(p);
}

static void testIntegridad(){
  printf("Deteccion de manipulacion\n");
  const char* txt = "contenido autenticado";

  // Cada caso arranca de cero: manipular un blob lo deja inservible
  // a proposito, asi que no se pueden encadenar.
  struct Caso { const char* nombre; int modo; };
  Caso casos[] = {
    { "un byte del cifrado cambiado no descifra",   0 },
    { "un byte del tag cambiado no descifra",       1 },
    { "un byte de la cabecera cambiado no descifra",2 },
    { "recortar el final no descifra",              3 },
    { "cambiar el tamano de la cabecera no descifra",4 }
  };

  for(size_t c = 0; c < sizeof(casos) / sizeof(casos[0]); c++){
    gFs.clear();
    prefsTestClear();
    flexVaultBegin();
    flexVaultCreate("clave-de-prueba", FLEXVAULT_LOCK_PASS);
    flexFsWriteBin("/Documentos/a.txt", txt, strlen(txt));
    flexVaultImport("/Documentos/a.txt", FXV_KIND_NOTE, -1);
    FlexVaultItem it[2];
    flexVaultList(FXV_KIND_NOTE, it, 2);
    std::vector<uint8_t>& b = gFs[blobPathOf(it[0].id)];
    switch(casos[c].modo){
      case 0: b[b.size() - 1] ^= 0x01; break;              // cifrado
      case 1: b[24] ^= 0x01; break;                        // primer byte del tag
      case 2: b[16] ^= 0x01; break;                        // sal de escritura (cabecera)
      case 3: b.pop_back(); break;                         // truncado
      case 4: b[12] = (uint8_t)(b[12] + 1); break;         // tamano declarado
    }
    char buf[128];
    chk(flexVaultRead(it[0].id, buf, sizeof(buf)) < 0, casos[c].nombre);
  }

  // El caso mas sutil: intercambiar dos bloques enteros. Cada bloque
  // es autentico por separado, pero su NUMERO entra en los datos
  // autenticados, asi que colocado en otro sitio no valida.
  gFs.clear();
  prefsTestClear();
  flexVaultBegin();
  flexVaultCreate("clave-de-prueba", FLEXVAULT_LOCK_PASS);
  const size_t N = FLEXVAULT_CHUNK * 2;
  std::vector<uint8_t> big(N);
  for(size_t i = 0; i < N; i++) big[i] = (uint8_t)(i & 0xFF);
  flexFsWriteBin("/Documentos/b.bin", big.data(), N);
  flexVaultImport("/Documentos/b.bin", FXV_KIND_FILE, -1);
  FlexVaultItem it2[2];
  flexVaultList(FXV_KIND_FILE, it2, 2);
  {
    std::vector<uint8_t>& b = gFs[blobPathOf(it2[0].id)];
    const size_t H = 24, U = 16 + FLEXVAULT_CHUNK;      // tag + cifrado
    chk(b.size() == H + 2 * U, "el blob tiene dos bloques del mismo tamano");
    std::vector<uint8_t> tmp(b.begin() + H, b.begin() + H + U);
    std::copy(b.begin() + H + U, b.begin() + H + 2 * U, b.begin() + H);
    std::copy(tmp.begin(), tmp.end(), b.begin() + H + U);
  }
  std::vector<uint8_t> back(N + 16);
  chk(flexVaultRead(it2[0].id, back.data(), back.size()) < 0,
      "intercambiar dos bloques enteros no descifra (el orden esta autenticado)");
}

// -------------------------------------------------------------
//  Cambio de clave: los ficheros NO se tocan
// -------------------------------------------------------------
static void testCambioClave(){
  printf("Cambio de clave sin perder ficheros\n");
  gFs.clear();
  prefsTestClear();
  flexVaultBegin();
  flexVaultCreate("clave-antigua", FLEXVAULT_LOCK_PASS);
  const char* txt = "sobrevive-al-cambio-de-clave";
  flexFsWriteBin("/Documentos/c.txt", txt, strlen(txt));
  flexVaultImport("/Documentos/c.txt", FXV_KIND_NOTE, -1);
  FlexVaultItem it[2];
  flexVaultList(FXV_KIND_NOTE, it, 2);
  uint16_t id = it[0].id;

  std::vector<uint8_t> antes = gFs[blobPathOf(id)];

  chk(flexVaultChangeSecret("mal", "clave-nueva", FLEXVAULT_LOCK_PASS) == FXV_ERR_WRONG,
      "sin la clave actual no se cambia");
  chk(flexVaultUnlocked(), "y un intento fallido no cierra la boveda abierta");
  char buf[128];
  chk(flexVaultRead(id, buf, sizeof(buf)) > 0,
      "tras el intento fallido la boveda sigue funcionando");

  chk(flexVaultChangeSecret("clave-antigua", "clave-nueva", FLEXVAULT_LOCK_PASS) == FXV_OK,
      "con la clave actual si se cambia");
  chk(gFs[blobPathOf(id)] == antes,
      "los BYTES CIFRADOS del fichero no han cambiado (no hubo que recifrar)");

  int rd = flexVaultRead(id, buf, sizeof(buf));
  chk(rd == (int)strlen(txt) && !memcmp(buf, txt, rd),
      "y el contenido se sigue leyendo igual");

  flexVaultLock(FXV_LOCK_MANUAL);
  chk(flexVaultUnlock("clave-antigua") == FXV_ERR_WRONG, "la clave vieja ya no vale");
  chk(flexVaultUnlock("clave-nueva") == FXV_OK, "la nueva si");
  rd = flexVaultRead(id, buf, sizeof(buf));
  chk(rd == (int)strlen(txt) && !memcmp(buf, txt, rd),
      "el fichero se abre con la clave nueva");

  // Cambiar de contrasena a PIN tambien vale (y se recuerda).
  chk(flexVaultChangeSecret("clave-nueva", "4321", FLEXVAULT_LOCK_PIN) == FXV_OK,
      "se puede pasar de contrasena a PIN");
  chk(flexVaultLockType() == FLEXVAULT_LOCK_PIN, "y el tipo queda actualizado");
  flexVaultLock(FXV_LOCK_MANUAL);
  chk(flexVaultUnlock("4321") == FXV_OK, "abre con el PIN nuevo");
}

// -------------------------------------------------------------
//  Intentos fallidos: contador persistente y esperas crecientes
// -------------------------------------------------------------
static void testIntentos(){
  printf("Intentos fallidos y esperas\n");
  gFs.clear();
  prefsTestClear();
  flexVaultBegin();
  flexVaultCreate("9876", FLEXVAULT_LOCK_PIN);
  flexVaultLock(FXV_LOCK_MANUAL);

  for(int i = 1; i <= 3; i++) flexVaultUnlock("0000");
  chk(flexVaultFails() == 3, "tres fallos contados");
  chk(flexVaultWaitMs() == 0, "los tres primeros fallos no imponen espera");

  flexVaultUnlock("0000");
  chk(flexVaultFails() == 4, "cuarto fallo");
  uint32_t w = flexVaultWaitMs();
  chk(w > 0 && w <= 30000, "el cuarto fallo impone una espera de medio minuto");
  chk(flexVaultUnlock("9876") == FXV_ERR_WAIT,
      "durante la espera NI LA CLAVE CORRECTA abre");

  gMs += 31000;
  chk(flexVaultWaitMs() == 0, "la espera termina sola");

  // Dos fallos mas hasta llegar a seis. El tiempo se adelanta ENTRE
  // ellos (para poder volver a intentarlo) pero no despues del ultimo:
  // lo que se comprueba es la espera que ese ultimo fallo impone.
  flexVaultUnlock("0000"); gMs += 400000;
  flexVaultUnlock("0000");
  chk(flexVaultFails() == 6, "seis fallos");
  uint32_t w2 = flexVaultWaitMs();
  chk(w2 > 30000, "a partir del sexto la espera es mucho mayor");

  // REINICIO en medio de la espera: el contador vive en NVS, asi que
  // quitar la corriente no la perdona.
  int fallosAntes = flexVaultFails();
  flexVaultBegin();
  chk(flexVaultFails() == fallosAntes, "el contador de fallos sobrevive al reinicio");
  chk(flexVaultWaitMs() > 0, "y la espera se vuelve a cobrar tras reiniciar");

  gMs += 400000;
  chk(flexVaultUnlock("9876") == FXV_OK, "cumplida la espera, la clave correcta abre");
  chk(flexVaultFails() == 0, "y el contador vuelve a cero");
}

// -------------------------------------------------------------
//  Apps privadas
// -------------------------------------------------------------
static void testApps(){
  printf("Apps privadas\n");
  gFs.clear();
  prefsTestClear();
  flexVaultBegin();
  flexVaultCreate("clave-de-prueba", FLEXVAULT_LOCK_PASS);

  chk(flexVaultAppSupported(5),  "Notas es compatible");
  chk(flexVaultAppSupported(1),  "Galeria es compatible");
  chk(flexVaultAppSupported(3),  "Archivos es compatible");
  chk(!flexVaultAppSupported(7), "Navegador NO es compatible todavia");
  chk(!flexVaultAppSupported(10),"Paint NO es compatible todavia");
  chk(!flexVaultAppSupported(14),"Calendario NO es compatible todavia");
  chk(flexVaultAppReason(7) && strstr(flexVaultAppReason(7), "compatible"),
      "y el motivo del Navegador se puede ensenar");

  chk(flexVaultAppForbidden(12), "Ajustes esta PROHIBIDO en la boveda");
  chk(flexVaultAppForbidden(4),  "Modo PC esta PROHIBIDO en la boveda");
  chk(!flexVaultAppAdd(12),      "y no se puede anadir Ajustes");
  chk(!flexVaultAppAdd(7),       "ni una app no compatible");

  chk(flexVaultAppAdd(5), "se anade Notas");
  chk(flexVaultAppAdded(5), "y queda anadida");
  flexVaultAppTouch(5);

  // Datos privados de la app.
  const char* d1 = "nota-privada-de-la-app";
  flexFsWriteBin("/Notas/AppPriv.txt", d1, strlen(d1));
  chk(flexVaultImport("/Notas/AppPriv.txt", FXV_KIND_NOTE, 5), "entra un dato de la app");
  chk(flexVaultAppBytes(5) > strlen(d1), "los bytes cifrados de la app se miden");

  chk(!flexVaultAppLocked(5), "sin candado propio al principio");
  flexVaultAppSetLocked(5, true);
  chk(flexVaultAppLocked(5), "se le puede poner candado propio");
  flexVaultAppSetLocked(5, false);

  // Quitar la app MANTENIENDO los datos cifrados.
  chk(flexVaultAppRemove(5, FXV_APPDATA_KEEP), "se quita manteniendo los datos");
  chk(!flexVaultAppAdded(5), "la app ya no esta anadida");
  chk(flexVaultCount(FXV_KIND_NOTE) == 1, "pero sus datos siguen cifrados dentro");
  chk(!fsHas("/Notas/AppPriv.txt"), "y NO han vuelto a la app normal");

  // Volver a anadirla y quitarla DEVOLVIENDO los datos.
  chk(flexVaultAppAdd(5), "se vuelve a anadir");
  chk(flexVaultAppRemove(5, FXV_APPDATA_EXPORT), "se quita devolviendo los datos");
  chk(fsHas("/Notas/AppPriv.txt"), "el dato vuelve a la app normal");
  chk(flexVaultCount(FXV_KIND_NOTE) == 0, "y sale de la boveda");

  // Y la tercera opcion: borrado definitivo.
  flexVaultAppAdd(5);
  flexVaultImport("/Notas/AppPriv.txt", FXV_KIND_NOTE, 5);
  chk(flexVaultCount(FXV_KIND_NOTE) == 1, "vuelve a entrar");
  chk(flexVaultAppRemove(5, FXV_APPDATA_WIPE), "se quita eliminando los datos");
  chk(flexVaultCount(FXV_KIND_NOTE) == 0, "ya no esta en la boveda");
  chk(!fsHas("/Notas/AppPriv.txt"), "ni ha vuelto a la app normal: se elimino");
  chk(!fsContains(d1, strlen(d1)), "y no queda ni un byte suyo en la particion");
}

// -------------------------------------------------------------
//  Registro de seguridad
// -------------------------------------------------------------
static void testRegistro(){
  printf("Registro de seguridad\n");
  gFs.clear();
  prefsTestClear();
  flexVaultBegin();
  flexVaultSetClock(1700000000u);
  flexVaultCreate("clave-de-prueba", FLEXVAULT_LOCK_PASS);
  flexVaultLock(FXV_LOCK_SCREEN);
  flexVaultUnlock("mal");
  flexVaultUnlock("clave-de-prueba");

  FlexVaultLog lg[FLEXVAULT_LOG_MAX];
  int n = flexVaultLogRead(lg, FLEXVAULT_LOG_MAX);
  chk(n >= 4, "el registro tiene las lineas de la sesion");
  chk(lg[0].ev == FXV_EV_UNLOCK, "la mas reciente es la apertura");
  bool vioFallo = false, vioCierre = false, vioCreacion = false;
  int8_t motivo = -1;
  for(int i = 0; i < n; i++){
    if(lg[i].ev == FXV_EV_FAIL)   vioFallo = true;
    if(lg[i].ev == FXV_EV_LOCK){  vioCierre = true; motivo = lg[i].aux; }
    if(lg[i].ev == FXV_EV_CREATE) vioCreacion = true;
  }
  chk(vioFallo,    "el intento fallido esta registrado");
  chk(vioCierre,   "el cierre esta registrado");
  chk(vioCreacion, "la creacion esta registrada");
  chk(motivo == FXV_LOCK_SCREEN, "con el MOTIVO del cierre (pantalla)");

  // El registro sobrevive al reinicio y sigue cifrado.
  chk(!fsContains("UNLOCK", 6), "el registro no guarda texto legible");
  flexVaultBegin();
  chk(flexVaultLogRead(lg, FLEXVAULT_LOG_MAX) == 0,
      "cerrada, el registro no se puede leer");
  flexVaultUnlock("clave-de-prueba");
  int n2 = flexVaultLogRead(lg, FLEXVAULT_LOG_MAX);
  chk(n2 >= 4, "al abrir de nuevo, el registro anterior esta ahi");
}

// -------------------------------------------------------------
//  Aislamiento del almacen frente a la capa normal
// -------------------------------------------------------------
static void testAislamiento(){
  printf("Aislamiento del almacen\n");
  chk(flexFsIsVaultPath("/.fxvault"),        "la raiz de la boveda se reconoce");
  chk(flexFsIsVaultPath("/.fxvault/d/3.b"),  "y cualquier ruta de dentro");
  chk(!flexFsIsVaultPath("/.fxvaultXY"),     "un nombre parecido NO es la boveda");
  chk(!flexFsIsVaultPath("/Notas/a.txt"),    "ni una ruta normal");

  // La capa normal se niega a tocar el almacen, aunque se le pida
  // explicitamente con la ruta correcta.
  chk(!flexFsExists("/.fxvault/i"),                  "la capa normal no ve el indice");
  chk(flexFsSize("/.fxvault/i") == 0,                "ni su tamano");
  chk(!flexFsDelete("/.fxvault/i"),                  "ni lo puede borrar");
  chk(flexFsReadAt("/.fxvault/i", 0, (void*)"", 1) < 0, "ni leerlo");
  chk(!flexFsWriteBin("/.fxvault/i", "x", 1),        "ni escribir dentro");

  // Y la puerta de servicio solo abre su habitacion.
  chk(!flexFsPrivExists("/Notas/a.txt"),             "el acceso privilegiado no sale de la boveda");
  chk(flexFsPrivRead("/Notas/a.txt", 0, (void*)"", 1) < 0, "ni lee fuera de ella");
  chk(!flexFsPrivWrite("/Notas/a.txt", "x", 1),      "ni escribe fuera de ella");
  chk(!flexFsPrivDelete("/Notas/a.txt"),             "ni borra fuera de ella");
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

int main(){
  printf("\n=== FlexOS \xc2\xb7 Flex Vault (Carpeta segura) ===\n");
  testKdf();
  testCiclo();
  testContenido();
  testTroceado();
  testIntegridad();
  testCambioClave();
  testIntentos();
  testApps();
  testRegistro();
  testAislamiento();
  testBloqueoSistema();
  printf("=== %d comprobaciones, %d fallos ===\n", gChecks, gFails);
  return gFails ? 1 : 0;
}
