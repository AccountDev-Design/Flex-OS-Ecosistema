// #############################################################
// ##  FlexOS · CAPA REAL DE SISTEMA DE ARCHIVOS  ·  LittleFS
// ##  Implementacion comun a Ultra (P4), Ultra S3 y Pro.
// #############################################################
//
//  Todo lo que hay aqui toca ficheros de verdad. No hay ni un
//  contador cacheado, ni un tamano aproximado, ni un valor de
//  ejemplo: cuando la interfaz pide "cuantos elementos tiene
//  /Notas", se abre /Notas y se cuentan. Es mas caro que guardar
//  un numero en RAM, pero es la unica forma de que el numero no
//  mienta despues de un borrado, un renombrado o un reinicio.

#include "FlexOS_FS.h"
#include <LittleFS.h>
#include <FS.h>

// -------------------------------------------------------------
//  Estado del modulo
// -------------------------------------------------------------
static bool        fsMounted = false;
static const char* fsErr     = "sin montar";
static const char* fsLabel   = "spiffs";

// Profundidad maxima al recorrer el arbol. No es un capricho: las
// funciones recursivas de aqui corren en la misma tarea que dibuja,
// y un arbol profundo (o un ciclo por un fichero corrupto) se
// notaria como un cuelgue de la interfaz. Con 5 niveles sobra para
// la estructura del sistema.
#define FS_MAX_DEPTH 5

// -------------------------------------------------------------
//  FILTRO DEL ALMACEN DE FLEX VAULT
//  ------------------------------------------------------------
//  Una sola funcion decide si una ruta pertenece a la boveda, y
//  TODOS los recorridos publicos de este modulo la consultan. Es la
//  razon por la que el contenido de la Carpeta segura no puede
//  aparecer en Archivos, en Almacenamiento, en la Galeria, en el
//  Finder de Modo PC ni en ninguna pantalla futura: no es que la
//  interfaz lo esconda, es que la capa que abre los directorios no
//  lo entrega.
//
//  Se compara el prefijo completo y ademas se exige que lo que sigue
//  sea '/' o fin de cadena, para que un fichero legitimo llamado
//  "/.fxvaultXY" (que no es la boveda) no quede oculto por error.
// -------------------------------------------------------------
bool flexFsIsVaultPath(const char* path){
  if(!path) return false;
  const char* p = path;
  if(p[0] != '/') return false;
  size_t vl = strlen(FLEXFS_DIR_VAULT);
  if(strncmp(p, FLEXFS_DIR_VAULT, vl) != 0) return false;
  return p[vl] == 0 || p[vl] == '/';
}

// El nombre suelto de una entrada de directorio, visto desde la
// raiz. Los recorridos de la raiz reciben "\.fxvault" sin ruta, asi
// que ademas del filtro por ruta hace falta este por nombre.
static bool isVaultEntryName(const char* name){
  if(!name) return false;
  return strcmp(name, FLEXFS_DIR_VAULT + 1) == 0;   // ".fxvault"
}

// -------------------------------------------------------------
//  Utilidades de ruta (sin String: buffers fijos)
// -------------------------------------------------------------

// Devuelve solo el nombre, por si el core entrega la ruta completa
// en File::name() (cambio entre arduino-esp32 2.x y 3.x).
static const char* baseName(const char* p){
  if(!p) return "";
  const char* s = strrchr(p, '/');
  return s ? s + 1 : p;
}

static void joinPath(const char* dir, const char* name, char* out, size_t n){
  if(!dir || !dir[0] || !strcmp(dir, "/")) snprintf(out, n, "/%s", name);
  else snprintf(out, n, "%s/%s", dir, name);
}

// Copia la carpeta contenedora de `path` en `out` ("/Paint/a.fxp" -> "/Paint").
static void parentDir(const char* path, char* out, size_t n){
  const char* s = strrchr(path, '/');
  if(!s || s == path){ snprintf(out, n, "/"); return; }
  size_t len = (size_t)(s - path);
  if(len >= n) len = n - 1;
  memcpy(out, path, len);
  out[len] = 0;
}

// -------------------------------------------------------------
//  Montaje
// -------------------------------------------------------------
bool flexFsReady(){ return fsMounted; }
const char* flexFsError(){ return fsErr; }

// Etiquetas de particion de datos que se prueban, en orden. NO es una lista
// arbitraria: LittleFS del core de Arduino busca por defecto la etiqueta
// "spiffs", pero los esquemas de particion recomendados en los tres sketches
// no coinciden entre si -- el Ultra S3 arranca con un esquema FATFS, cuya
// particion se llama "ffat". Probando las etiquetas habituales, el sistema de
// archivos funciona con el esquema que ya tenga elegido el usuario en el IDE,
// sin obligarle a cambiarlo (que era la forma mas facil de que "no le
// aparecieran" sus dibujos y notas).
//
// AVISO IMPORTANTE: si la particion encontrada no tiene todavia un LittleFS
// (por ejemplo, es una particion FAT recien creada), se FORMATEA. En este
// proyecto eso es seguro porque hasta ahora ninguna version guardaba nada en
// esa zona -- toda la persistencia iba en NVS, que es otra particion distinta
// y no se toca aqui.
static const char* FS_LABELS[] = { "spiffs", "littlefs", "ffat", "storage" };

bool flexFsBegin(){
  if(fsMounted) return true;

  // formatOnFail = true cubre el caso "particion virgen": la primera vez que se
  // graba este firmware, la zona de datos no tiene ningun sistema de archivos y
  // hay que crearlo. Si NINGUNA etiqueta existe, begin() falla en todas y aqui
  // se refleja como un error real, no como "0 MB usados".
  bool ok = false;
  for(size_t i = 0; i < sizeof(FS_LABELS) / sizeof(FS_LABELS[0]); i++){
    if(LittleFS.begin(true, "/littlefs", 10, FS_LABELS[i])){ fsLabel = FS_LABELS[i]; ok = true; break; }
  }
  if(!ok){
    fsMounted = false;
    fsErr = "sin particion de datos (elige un Partition Scheme con SPIFFS)";
    return false;
  }
  fsMounted = true;
  fsErr = "";

  // Estructura base. mkdir sobre una carpeta que ya existe devuelve
  // false y no es un error: por eso no se comprueba el retorno.
  LittleFS.mkdir(FLEXFS_DIR_PAINT);
  LittleFS.mkdir(FLEXFS_DIR_NOTAS);
  LittleFS.mkdir(FLEXFS_DIR_SYS);
  LittleFS.mkdir(FLEXFS_DIR_DOCS);
  LittleFS.mkdir(FLEXFS_DIR_TRASH);
  return true;
}

uint32_t flexFsTotalBytes(){ return fsMounted ? (uint32_t)LittleFS.totalBytes() : 0; }
uint32_t flexFsUsedBytes(){  return fsMounted ? (uint32_t)LittleFS.usedBytes()  : 0; }

bool flexFsExists(const char* path){
  if(!fsMounted || !path || !path[0]) return false;
  if(flexFsIsVaultPath(path)) return false;      // la boveda no existe para la capa normal
  return LittleFS.exists(path);
}

bool flexFsIsDir(const char* path){
  if(!fsMounted) return false;
  if(flexFsIsVaultPath(path)) return false;
  File f = LittleFS.open(path);
  if(!f) return false;
  bool d = f.isDirectory();
  f.close();
  return d;
}

uint32_t flexFsSize(const char* path){
  if(!fsMounted) return 0;
  if(flexFsIsVaultPath(path)) return 0;
  File f = LittleFS.open(path, "r");
  if(!f) return 0;
  uint32_t s = f.isDirectory() ? 0 : (uint32_t)f.size();
  f.close();
  return s;
}

bool flexFsMkdir(const char* path){
  if(!fsMounted) return false;
  if(flexFsIsVaultPath(path)) return false;      // solo flexFsVaultInit crea la boveda
  if(LittleFS.exists(path)) return true;
  return LittleFS.mkdir(path);
}

// -------------------------------------------------------------
//  Recorridos
// -------------------------------------------------------------
int flexFsCount(const char* dir){
  if(!fsMounted) return 0;
  if(flexFsIsVaultPath(dir)) return 0;
  File d = LittleFS.open(dir);
  if(!d || !d.isDirectory()){ if(d) d.close(); return 0; }
  int n = 0;
  File e = d.openNextFile();
  while(e){
    if(!isVaultEntryName(baseName(e.name()))) n++;    // la boveda no se cuenta
    e.close(); e = d.openNextFile();
  }
  d.close();
  return n;
}

static uint32_t dirSizeRec(const char* dir, int depth){
  if(depth > FS_MAX_DEPTH) return 0;
  if(flexFsIsVaultPath(dir)) return 0;
  File d = LittleFS.open(dir);
  if(!d || !d.isDirectory()){ if(d) d.close(); return 0; }
  uint32_t total = 0;
  File e = d.openNextFile();
  while(e){
    if(isVaultEntryName(baseName(e.name()))){         // los bytes cifrados no
      e.close(); e = d.openNextFile(); continue;      // entran en ningun total
    }
    if(e.isDirectory()){
      char sub[FLEXFS_PATH_MAX];
      joinPath(dir, baseName(e.name()), sub, sizeof(sub));
      e.close();
      total += dirSizeRec(sub, depth + 1);
    } else {
      total += (uint32_t)e.size();
      e.close();
    }
    e = d.openNextFile();
  }
  d.close();
  return total;
}

uint32_t flexFsDirSize(const char* dir){
  if(!fsMounted) return 0;
  return dirSizeRec(dir, 0);
}

// Orden de la lista: carpetas antes que ficheros y, dentro de cada
// grupo, alfabetico sin distinguir mayusculas. Importa que sea
// ESTABLE: LittleFS entrega las entradas en el orden fisico del
// directorio, que cambia al borrar y crear, y sin ordenar la
// rejilla de la galeria se reordenaria sola entre repintados.
static int cmpEntry(const FlexFsEntry* a, const FlexFsEntry* b){
  if(a->dir != b->dir) return a->dir ? -1 : 1;
  return strcasecmp(a->name, b->name);
}

int flexFsList(const char* dir, FlexFsEntry* out, int maxn){
  if(!fsMounted || !out || maxn <= 0) return 0;
  if(flexFsIsVaultPath(dir)) return 0;            // no se puede listar la boveda
  File d = LittleFS.open(dir);
  if(!d || !d.isDirectory()){ if(d) d.close(); return 0; }

  int n = 0;
  File e = d.openNextFile();
  while(e && n < maxn){
    const char* nm = baseName(e.name());
    if(isVaultEntryName(nm)){                     // la boveda no aparece en la lista
      e.close(); e = d.openNextFile(); continue;
    }
    strncpy(out[n].name, nm, FLEXFS_NAME_MAX - 1);
    out[n].name[FLEXFS_NAME_MAX - 1] = 0;
    out[n].dir   = e.isDirectory();
    out[n].items = 0;
    if(out[n].dir){
      char sub[FLEXFS_PATH_MAX];
      joinPath(dir, out[n].name, sub, sizeof(sub));
      e.close();
      // Conteo y tamano REALES de la carpeta: se abre y se recorre.
      out[n].items = (uint16_t)flexFsCount(sub);
      out[n].size  = dirSizeRec(sub, 1);
    } else {
      out[n].size = (uint32_t)e.size();
      e.close();
    }
    n++;
    e = d.openNextFile();
  }
  if(e) e.close();
  d.close();

  for(int i = 1; i < n; i++){                 // insercion: n es pequeno (<= 64)
    FlexFsEntry tmp = out[i];
    int j = i - 1;
    while(j >= 0 && cmpEntry(&tmp, &out[j]) < 0){ out[j + 1] = out[j]; j--; }
    out[j + 1] = tmp;
  }
  return n;
}

uint32_t flexFsCatSize(int cat){
  if(!fsMounted) return 0;
  switch(cat){
    case FLEXFS_CAT_DOCS:  return dirSizeRec(FLEXFS_DIR_DOCS, 0) + dirSizeRec(FLEXFS_DIR_NOTAS, 0);
    case FLEXFS_CAT_SYS:   return dirSizeRec(FLEXFS_DIR_SYS, 0);
    case FLEXFS_CAT_APPS:  return dirSizeRec(FLEXFS_DIR_PAINT, 0);
    case FLEXFS_CAT_TRASH: return dirSizeRec(FLEXFS_DIR_TRASH, 0);
  }
  return 0;
}

// ---- Archivos mas grandes -----------------------------------
// Recorre TODO el arbol y mantiene un top-N por insercion. No se
// guarda una lista intermedia: con 3-4 huecos no hace falta y asi
// el coste en RAM es constante sea cual sea el numero de ficheros.
static void bigWalk(const char* dir, int depth, FlexFsBig* out, int maxn, int* used){
  if(depth > FS_MAX_DEPTH) return;
  if(flexFsIsVaultPath(dir)) return;
  File d = LittleFS.open(dir);
  if(!d || !d.isDirectory()){ if(d) d.close(); return; }
  File e = d.openNextFile();
  while(e){
    if(isVaultEntryName(baseName(e.name()))){      // "los mas grandes" nunca
      e.close(); e = d.openNextFile(); continue;   // delata un fichero privado
    }
    char full[FLEXFS_PATH_MAX];
    joinPath(dir, baseName(e.name()), full, sizeof(full));
    if(e.isDirectory()){
      e.close();
      bigWalk(full, depth + 1, out, maxn, used);
    } else {
      uint32_t sz = (uint32_t)e.size();
      e.close();
      int pos = *used;
      while(pos > 0 && out[pos - 1].size < sz) pos--;
      if(pos < maxn){
        int last = (*used < maxn) ? *used : maxn - 1;
        for(int k = last; k > pos; k--) out[k] = out[k - 1];
        strncpy(out[pos].path, full, FLEXFS_PATH_MAX - 1);
        out[pos].path[FLEXFS_PATH_MAX - 1] = 0;
        out[pos].size = sz;
        if(*used < maxn) (*used)++;
      }
    }
    e = d.openNextFile();
  }
  d.close();
}

int flexFsLargest(FlexFsBig* out, int maxn){
  if(!fsMounted || !out || maxn <= 0) return 0;
  int used = 0;
  bigWalk("/", 0, out, maxn, &used);
  return used;
}

// -------------------------------------------------------------
//  Operaciones destructivas
// -------------------------------------------------------------
static bool deleteRec(const char* path, int depth){
  if(depth > FS_MAX_DEPTH) return false;
  File f = LittleFS.open(path);
  if(!f) return false;
  if(!f.isDirectory()){ f.close(); return LittleFS.remove(path); }

  // Carpeta: hay que vaciarla antes, porque rmdir de LittleFS solo
  // borra directorios vacios.
  File e = f.openNextFile();
  while(e){
    char sub[FLEXFS_PATH_MAX];
    joinPath(path, baseName(e.name()), sub, sizeof(sub));
    e.close();
    deleteRec(sub, depth + 1);
    e = f.openNextFile();
  }
  f.close();
  return LittleFS.rmdir(path);
}

bool flexFsDelete(const char* path){
  if(!fsMounted || !path || !path[0] || !strcmp(path, "/")) return false;
  // Nadie borra la boveda desde la capa normal: eso solo lo hace
  // Flex Vault, con la clave delante.
  if(flexFsIsVaultPath(path)) return false;
  return deleteRec(path, 0);
}

// Copia byte a byte (respaldo de rename cuando el destino esta en
// otra carpeta y la implementacion no lo admite en un paso).
static bool copyFile(const char* from, const char* to){
  File in = LittleFS.open(from, "r");
  if(!in) return false;
  File out = LittleFS.open(to, "w");
  if(!out){ in.close(); return false; }
  uint8_t buf[256];
  size_t r;
  bool ok = true;
  while((r = in.read(buf, sizeof(buf))) > 0){
    if(out.write(buf, r) != r){ ok = false; break; }
  }
  in.close();
  out.close();
  if(!ok) LittleFS.remove(to);
  return ok;
}

static bool moveFile(const char* from, const char* to){
  if(LittleFS.rename(from, to)) return true;
  if(!copyFile(from, to)) return false;
  return LittleFS.remove(from);
}

// Devuelve la extension de `name` ("" si no tiene). Se usa para no
// perderla al renombrar: un ".fxp" que se convierte en "Mi dibujo"
// a secas deja de abrirse en Paint.
static const char* extOf(const char* name){
  const char* d = strrchr(name, '.');
  return d ? d : "";
}

bool flexFsRename(const char* path, const char* newName){
  if(!fsMounted || !path || !newName || !newName[0]) return false;
  if(flexFsIsVaultPath(path)) return false;
  if(strchr(newName, '/')) return false;              // solo nombre, sin rutas

  char dir[FLEXFS_PATH_MAX]; parentDir(path, dir, sizeof(dir));
  char full[FLEXFS_PATH_MAX];

  // Si el nombre nuevo no trae extension, se conserva la original.
  if(!extOf(newName)[0] && extOf(path)[0]){
    char tmp[FLEXFS_NAME_MAX];
    snprintf(tmp, sizeof(tmp), "%s%s", newName, extOf(path));
    joinPath(dir, tmp, full, sizeof(full));
  } else {
    joinPath(dir, newName, full, sizeof(full));
  }
  if(!strcmp(full, path)) return true;
  if(LittleFS.exists(full)) return false;             // no se pisa nada en silencio
  return LittleFS.rename(path, full);
}

// ---- Papelera -----------------------------------------------
// La ruta original se codifica en el propio nombre cambiando '/'
// por '@'. Asi la papelera se puede restaurar exactamente sin un
// fichero de indice aparte (que es justo lo que se desincroniza
// cuando alguien borra a mano desde el explorador).
static void encodeTrashName(const char* path, char* out, size_t n){
  size_t j = 0;
  for(size_t i = 0; path[i] && j < n - 1; i++) out[j++] = (path[i] == '/') ? '@' : path[i];
  out[j] = 0;
}

bool flexFsTrashOrigin(const char* trashName, char* out, size_t n){
  if(!trashName || !out) return false;
  const char* nm = baseName(trashName);
  size_t j = 0;
  for(size_t i = 0; nm[i] && j < n - 1; i++) out[j++] = (nm[i] == '@') ? '/' : nm[i];
  out[j] = 0;
  return out[0] == '/';
}

bool flexFsTrash(const char* path){
  if(!fsMounted || !path || !path[0]) return false;
  // La papelera no se tira a si misma.
  if(!strncmp(path, FLEXFS_DIR_TRASH, strlen(FLEXFS_DIR_TRASH))) return false;
  // Y la boveda tampoco: un elemento cifrado en la papelera seria
  // exactamente la fuga que Flex Vault existe para evitar.
  if(flexFsIsVaultPath(path)) return false;
  flexFsMkdir(FLEXFS_DIR_TRASH);

  char enc[FLEXFS_NAME_MAX];
  encodeTrashName(path, enc, sizeof(enc));
  char dst[FLEXFS_PATH_MAX];
  joinPath(FLEXFS_DIR_TRASH, enc, dst, sizeof(dst));

  // Colision: ya se tiro antes algo con el mismo nombre. Se numera
  // en vez de pisarlo, para no perder el primero.
  if(LittleFS.exists(dst)){
    for(int k = 2; k < 100; k++){
      char alt[FLEXFS_NAME_MAX];
      snprintf(alt, sizeof(alt), "%s(%d)", enc, k);
      joinPath(FLEXFS_DIR_TRASH, alt, dst, sizeof(dst));
      if(!LittleFS.exists(dst)) break;
    }
  }
  if(flexFsIsDir(path)) return LittleFS.rename(path, dst);   // carpeta entera
  return moveFile(path, dst);
}

bool flexFsRestore(const char* trashName){
  if(!fsMounted || !trashName || !trashName[0]) return false;
  char src[FLEXFS_PATH_MAX];
  joinPath(FLEXFS_DIR_TRASH, baseName(trashName), src, sizeof(src));
  if(!LittleFS.exists(src)) return false;

  char dst[FLEXFS_PATH_MAX];
  if(!flexFsTrashOrigin(trashName, dst, sizeof(dst))) return false;
  // La ruta de destino sale del NOMBRE del fichero de la papelera, o sea de
  // datos que estan en el disco y se pueden manipular. Un nombre fabricado
  // como "@.fxvault@algo" se decodificaria a "/.fxvault/algo" y la restauracion
  // escribiria DENTRO del almacen de Flex Vault, que es la unica carpeta que
  // esta capa no debe tocar nunca. Se comprueba aqui, en el unico sitio en el
  // que una ruta de destino no la elige el codigo.
  if(flexFsIsVaultPath(dst)) return false;

  // La carpeta de destino pudo borrarse mientras el fichero estaba
  // en la papelera: se recrea antes de devolverlo a su sitio.
  char dir[FLEXFS_PATH_MAX]; parentDir(dst, dir, sizeof(dir));
  if(strcmp(dir, "/")) flexFsMkdir(dir);

  if(LittleFS.exists(dst)){                      // ya hay otro con ese nombre
    char stem[FLEXFS_NAME_MAX]; flexFsStem(baseName(dst), stem, sizeof(stem));
    const char* ex = extOf(baseName(dst));
    for(int k = 2; k < 100; k++){
      char alt[FLEXFS_NAME_MAX];
      snprintf(alt, sizeof(alt), "%s (%d)%s", stem, k, ex);
      joinPath(dir, alt, dst, sizeof(dst));
      if(!LittleFS.exists(dst)) break;
    }
  }
  if(flexFsIsDir(src)) return LittleFS.rename(src, dst);
  return moveFile(src, dst);
}

bool flexFsEmptyTrash(){
  if(!fsMounted) return false;
  File d = LittleFS.open(FLEXFS_DIR_TRASH);
  if(!d || !d.isDirectory()){ if(d) d.close(); return false; }
  File e = d.openNextFile();
  while(e){
    char sub[FLEXFS_PATH_MAX];
    joinPath(FLEXFS_DIR_TRASH, baseName(e.name()), sub, sizeof(sub));
    e.close();
    deleteRec(sub, 1);
    e = d.openNextFile();
  }
  d.close();
  return true;
}

// -------------------------------------------------------------
//  Nombres
// -------------------------------------------------------------
void flexFsStem(const char* name, char* out, size_t n){
  const char* nm = baseName(name);
  const char* d = strrchr(nm, '.');
  size_t len = d ? (size_t)(d - nm) : strlen(nm);
  if(len >= n) len = n - 1;
  memcpy(out, nm, len);
  out[len] = 0;
}

bool flexFsNewName(const char* dir, const char* base, const char* ext, char* out, size_t n){
  if(!fsMounted) return false;
  if(flexFsIsVaultPath(dir)) return false;
  for(int k = 1; k < 1000; k++){
    char nm[FLEXFS_NAME_MAX];
    snprintf(nm, sizeof(nm), "%s %d%s", base, k, ext ? ext : "");
    char full[FLEXFS_PATH_MAX];
    joinPath(dir, nm, full, sizeof(full));
    if(!LittleFS.exists(full)){
      strncpy(out, full, n - 1);
      out[n - 1] = 0;
      return true;
    }
  }
  return false;
}

void flexFsFmtSize(uint32_t b, char* out, size_t n){
  if(b >= 1048576u){
    uint32_t ent = b / 1048576u, dec = (b % 1048576u) * 10u / 1048576u;
    snprintf(out, n, "%lu.%lu MB", (unsigned long)ent, (unsigned long)dec);
  } else if(b >= 1024u){
    snprintf(out, n, "%lu KB", (unsigned long)(b / 1024u));
  } else {
    snprintf(out, n, "%lu B", (unsigned long)b);
  }
}

// -------------------------------------------------------------
//  Notas (texto plano)
// -------------------------------------------------------------
int flexFsReadText(const char* path, char* out, size_t n){
  if(!fsMounted || !out || n == 0) return -1;
  out[0] = 0;
  if(flexFsIsVaultPath(path)) return -1;
  File f = LittleFS.open(path, "r");
  if(!f) return -1;
  size_t r = f.read((uint8_t*)out, n - 1);
  f.close();
  out[r] = 0;
  return (int)r;
}

bool flexFsWriteText(const char* path, const char* txt){
  if(!fsMounted) return false;
  if(flexFsIsVaultPath(path)) return false;
  File f = LittleFS.open(path, "w");
  if(!f) return false;
  size_t len = txt ? strlen(txt) : 0;
  size_t w = len ? f.write((const uint8_t*)txt, len) : 0;
  f.close();
  return w == len;
}

// -------------------------------------------------------------
//  BINARIO
//  ------------------------------------------------------------
//  Las funciones de texto de arriba no valen para datos con bytes 0
//  dentro (dan por hecho una cadena terminada). El historial y los
//  favoritos del navegador son registros de tamano fijo, asi que
//  necesitan este par. Se anaden aqui, en el modulo comun, para no
//  meter LittleFS en los .ino -- que es justo lo que este fichero
//  existe para evitar.
// -------------------------------------------------------------
int flexFsReadBin(const char* path, void* buf, size_t n){
  if(!fsMounted || !buf || n == 0) return -1;
  if(flexFsIsVaultPath(path)) return -1;
  File f = LittleFS.open(path, "r");
  if(!f) return -1;
  size_t r = f.read((uint8_t*)buf, n);
  f.close();
  return (int)r;
}

bool flexFsWriteBin(const char* path, const void* buf, size_t n){
  if(!fsMounted) return false;
  if(flexFsIsVaultPath(path)) return false;
  if(n > 0 && !buf) return false;
  File f = LittleFS.open(path, "w");
  if(!f) return false;
  size_t w = n ? f.write((const uint8_t*)buf, n) : 0;
  f.close();
  return w == n;
}

bool flexFsWriteBinAtomic(const char* path, const void* buf, size_t n){
  if(!fsMounted || !path || (!buf && n)) return false;
  size_t lp = strlen(path);
  if(lp == 0 || lp + 5 >= FLEXFS_PATH_MAX) return false;
  char tmp[FLEXFS_PATH_MAX], bak[FLEXFS_PATH_MAX];
  snprintf(tmp, sizeof(tmp), "%s.tmp", path);
  snprintf(bak, sizeof(bak), "%s.bak", path);
  LittleFS.remove(tmp);
  if(!flexFsWriteBin(tmp, buf, n)) return false;
  if(flexFsSize(tmp) != n){ LittleFS.remove(tmp); return false; }

  LittleFS.remove(bak);
  bool hadOld = LittleFS.exists(path);
  if(hadOld && !LittleFS.rename(path, bak)){
    LittleFS.remove(tmp);
    return false;
  }
  if(!LittleFS.rename(tmp, path)){
    if(hadOld) LittleFS.rename(bak, path);
    LittleFS.remove(tmp);
    return false;
  }
  LittleFS.remove(bak);
  return true;
}

bool flexFsFactoryErase(){
  if(fsMounted){ LittleFS.end(); fsMounted = false; }
  // arduino-esp32 3.2.0 conserva internamente la etiqueta usada por begin();
  // format() no recibe parametros en esa API.
  if(!LittleFS.format()){
    fsErr = "No se pudo formatear LittleFS";
    return false;
  }
  if(!LittleFS.begin(false, "/littlefs", 10, fsLabel)){
    fsErr = "LittleFS no monto tras formatear";
    return false;
  }
  fsMounted = true;
  fsErr = "";
  LittleFS.mkdir(FLEXFS_DIR_PAINT);
  LittleFS.mkdir(FLEXFS_DIR_NOTAS);
  LittleFS.mkdir(FLEXFS_DIR_SYS);
  LittleFS.mkdir(FLEXFS_DIR_DOCS);
  LittleFS.mkdir(FLEXFS_DIR_TRASH);
  return true;
}

// -------------------------------------------------------------
//  PAINT: dibujo por trazos
// -------------------------------------------------------------
static bool paintReadHdr(File& f, FlexPaintHdr* h){
  if(f.read((uint8_t*)h, sizeof(FlexPaintHdr)) != sizeof(FlexPaintHdr)) return false;
  return h->magic[0] == FLEXPAINT_MAGIC0 && h->magic[1] == FLEXPAINT_MAGIC1 &&
         h->magic[2] == FLEXPAINT_MAGIC2 && h->magic[3] == FLEXPAINT_MAGIC3;
}

bool flexPaintCreate(const char* path, uint16_t w, uint16_t h){
  if(!fsMounted) return false;
  FlexPaintHdr hd;
  hd.magic[0] = FLEXPAINT_MAGIC0; hd.magic[1] = FLEXPAINT_MAGIC1;
  hd.magic[2] = FLEXPAINT_MAGIC2; hd.magic[3] = FLEXPAINT_MAGIC3;
  hd.w = w; hd.h = h; hd.strokes = 0; hd.reserved = 0;
  File f = LittleFS.open(path, "w");
  if(!f) return false;
  bool ok = f.write((const uint8_t*)&hd, sizeof(hd)) == sizeof(hd);
  f.close();
  return ok;
}

bool flexPaintHeader(const char* path, FlexPaintHdr* out){
  if(!fsMounted || !out) return false;
  File f = LittleFS.open(path, "r");
  if(!f) return false;
  bool ok = paintReadHdr(f, out);
  f.close();
  return ok;
}

bool flexPaintAppend(const char* path, uint16_t color, uint8_t size,
                     const int16_t* xy, uint16_t pts){
  if(!fsMounted || !xy || pts == 0) return false;

  // Se abre en "r+" para poder anadir al final Y reescribir el
  // contador de la cabecera en la misma pasada. Guardar trazo a
  // trazo (y no al cerrar la app) es lo que hace que un corte de
  // corriente a mitad de dibujo solo pierda el trazo en curso.
  File f = LittleFS.open(path, "r+");
  if(!f) return false;
  FlexPaintHdr hd;
  if(!paintReadHdr(f, &hd)){ f.close(); return false; }

  f.seek(0, SeekEnd);
  FlexPaintStroke st;
  st.color = color; st.size = size; st.flags = 0; st.pts = pts;
  bool ok = f.write((const uint8_t*)&st, sizeof(st)) == sizeof(st);
  if(ok){
    size_t need = (size_t)pts * 4;
    ok = f.write((const uint8_t*)xy, need) == need;
  }
  if(ok){
    hd.strokes++;
    f.seek(0, SeekSet);
    ok = f.write((const uint8_t*)&hd, sizeof(hd)) == sizeof(hd);
  }
  f.close();
  return ok;
}

bool flexPaintReplay(const char* path, float sc, int ox, int oy,
                     FlexPaintSegCb cb, void* user){
  if(!fsMounted || !cb) return false;
  File f = LittleFS.open(path, "r");
  if(!f) return false;
  FlexPaintHdr hd;
  if(!paintReadHdr(f, &hd)){ f.close(); return false; }

  for(uint16_t s = 0; s < hd.strokes; s++){
    FlexPaintStroke st;
    if(f.read((uint8_t*)&st, sizeof(st)) != sizeof(st)) break;
    // El radio tambien se escala: si no, la miniatura saldria con
    // trazos gruesisimos y el dibujo dejaria de parecerse al real.
    int rad = (int)(st.size * sc + 0.5f);
    if(rad < 1) rad = 1;
    int px = 0, py = 0;
    for(uint16_t i = 0; i < st.pts; i++){
      int16_t xy[2];
      if(f.read((uint8_t*)xy, 4) != 4){ s = hd.strokes; break; }
      int x = ox + (int)(xy[0] * sc + 0.5f);
      int y = oy + (int)(xy[1] * sc + 0.5f);
      if(i == 0) cb(x, y, x, y, st.color, rad, user);
      else       cb(px, py, x, y, st.color, rad, user);
      px = x; py = y;
    }
  }
  f.close();
  return true;
}

bool flexPaintClear(const char* path){
  FlexPaintHdr hd;
  if(!flexPaintHeader(path, &hd)) return false;
  return flexPaintCreate(path, hd.w, hd.h);
}

// Deshacer real: se reconstruye el fichero con todos los trazos
// menos el ultimo. LittleFS en Arduino no expone truncate(), asi
// que se copia a un temporal y se sustituye. Es una operacion
// puntual (solo al pulsar "deshacer"), no algo del bucle de
// dibujo, asi que el coste es aceptable a cambio de que el
// deshacer sea de verdad y sobreviva al reinicio.
bool flexPaintUndo(const char* path){
  if(!fsMounted) return false;
  FlexPaintHdr hd;
  if(!flexPaintHeader(path, &hd)) return false;
  if(hd.strokes == 0) return false;

  char tmp[FLEXFS_PATH_MAX];
  snprintf(tmp, sizeof(tmp), "%s.tmp", path);

  File in = LittleFS.open(path, "r");
  if(!in) return false;
  File out = LittleFS.open(tmp, "w");
  if(!out){ in.close(); return false; }

  FlexPaintHdr nh = hd;
  nh.strokes = hd.strokes - 1;
  in.read((uint8_t*)&hd, sizeof(hd));                 // consume la cabecera vieja
  bool ok = out.write((const uint8_t*)&nh, sizeof(nh)) == sizeof(nh);

  uint8_t buf[256];
  for(uint16_t s = 0; ok && s < nh.strokes; s++){
    FlexPaintStroke st;
    if(in.read((uint8_t*)&st, sizeof(st)) != sizeof(st)){ ok = false; break; }
    ok = out.write((const uint8_t*)&st, sizeof(st)) == sizeof(st);
    size_t left = (size_t)st.pts * 4;
    while(ok && left){
      size_t chunk = left > sizeof(buf) ? sizeof(buf) : left;
      if(in.read(buf, chunk) != chunk){ ok = false; break; }
      ok = out.write(buf, chunk) == chunk;
      left -= chunk;
    }
  }
  in.close();
  out.close();
  if(!ok){ LittleFS.remove(tmp); return false; }
  LittleFS.remove(path);
  return LittleFS.rename(tmp, path);
}

// -------------------------------------------------------------
//  PAINT DESDE MEMORIA (lo necesita Flex Vault)
//  ------------------------------------------------------------
//  Un dibujo privado vive CIFRADO. Para previsualizarlo se descifra
//  a RAM y se reproduce desde ahi. La alternativa -escribir un .fxp
//  temporal en claro y llamar a flexPaintReplay- dejaria el dibujo
//  privado a la vista en la particion de datos durante ese rato (y
//  para siempre si se corta la corriente a mitad). De ahi estas dos
//  funciones: misma logica de recorrido, distinta fuente de bytes.
// -------------------------------------------------------------
bool flexPaintHeaderMem(const void* data, size_t len, FlexPaintHdr* out){
  if(!data || !out || len < sizeof(FlexPaintHdr)) return false;
  memcpy(out, data, sizeof(FlexPaintHdr));
  return out->magic[0] == FLEXPAINT_MAGIC0 && out->magic[1] == FLEXPAINT_MAGIC1 &&
         out->magic[2] == FLEXPAINT_MAGIC2 && out->magic[3] == FLEXPAINT_MAGIC3;
}

bool flexPaintReplayMem(const void* data, size_t len, float sc, int ox, int oy,
                        FlexPaintSegCb cb, void* user){
  if(!data || !cb) return false;
  FlexPaintHdr hd;
  if(!flexPaintHeaderMem(data, len, &hd)) return false;

  const uint8_t* p   = (const uint8_t*)data;
  size_t         off = sizeof(FlexPaintHdr);

  for(uint16_t s = 0; s < hd.strokes; s++){
    // Cada lectura comprueba que los bytes existen ANTES de usarlos:
    // estos bytes vienen de descifrar un fichero que pudo quedar a
    // medias, asi que un contador de trazos que no cuadre tiene que
    // acabar en "se dibuja lo que hay", no en leer fuera del buffer.
    if(off + sizeof(FlexPaintStroke) > len) break;
    FlexPaintStroke st;
    memcpy(&st, p + off, sizeof(st));
    off += sizeof(st);
    int rad = (int)(st.size * sc + 0.5f);
    if(rad < 1) rad = 1;
    int px = 0, py = 0;
    for(uint16_t i = 0; i < st.pts; i++){
      if(off + 4 > len){ s = hd.strokes; break; }
      int16_t xy[2];
      memcpy(xy, p + off, 4);
      off += 4;
      int x = ox + (int)(xy[0] * sc + 0.5f);
      int y = oy + (int)(xy[1] * sc + 0.5f);
      if(i == 0) cb(x, y, x, y, st.color, rad, user);
      else       cb(px, py, x, y, st.color, rad, user);
      px = x; py = y;
    }
  }
  return true;
}

// -------------------------------------------------------------
//  LECTURA / ESCRITURA PARCIAL
//  ------------------------------------------------------------
//  Flex Vault cifra un fichero grande por bloques: lee 4 KB del
//  original, los cifra y los anade al blob. Sin estas dos funciones
//  habria que cargar el fichero entero en RAM, que es exactamente lo
//  que no se puede hacer con una foto de varios MB en una placa con
//  la PSRAM compartida con tres framebuffers.
// -------------------------------------------------------------
int flexFsReadAt(const char* path, uint32_t off, void* buf, size_t n){
  if(!fsMounted || !buf || n == 0) return -1;
  if(flexFsIsVaultPath(path)) return -1;
  File f = LittleFS.open(path, "r");
  if(!f) return -1;
  if(off){
    if(!f.seek(off)){ f.close(); return 0; }      // mas alla del final: 0 bytes
  }
  size_t r = f.read((uint8_t*)buf, n);
  f.close();
  return (int)r;
}

bool flexFsAppendBin(const char* path, const void* buf, size_t n){
  if(!fsMounted) return false;
  if(flexFsIsVaultPath(path)) return false;
  if(n > 0 && !buf) return false;
  File f = LittleFS.open(path, "a");
  if(!f) return false;
  size_t w = n ? f.write((const uint8_t*)buf, n) : 0;
  f.close();
  return w == n;
}

// -------------------------------------------------------------
//  ALMACEN DE FLEX VAULT (acceso privilegiado)
//  ------------------------------------------------------------
//  Estas funciones son la puerta de servicio del almacen cifrado.
//  TODAS empiezan comprobando que la ruta esta dentro de la boveda,
//  asi que no se pueden usar para saltarse ningun otro filtro del
//  modulo: lo unico que hacen es dar acceso a la habitacion que las
//  funciones publicas se niegan a abrir.
//
//  Aqui NO hay criptografia. Los bytes que entran y salen ya vienen
//  cifrados de FlexOS_Vault.cpp. Este modulo nunca ve una clave.
// -------------------------------------------------------------
bool flexFsVaultInit(){
  if(!fsMounted) return false;
  if(!LittleFS.exists(FLEXFS_DIR_VAULT))  LittleFS.mkdir(FLEXFS_DIR_VAULT);
  if(!LittleFS.exists(FLEXFS_DIR_VAULTD)) LittleFS.mkdir(FLEXFS_DIR_VAULTD);
  return LittleFS.exists(FLEXFS_DIR_VAULT) && LittleFS.exists(FLEXFS_DIR_VAULTD);
}

bool flexFsPrivExists(const char* path){
  if(!fsMounted || !flexFsIsVaultPath(path)) return false;
  return LittleFS.exists(path);
}

uint32_t flexFsPrivSize(const char* path){
  if(!fsMounted || !flexFsIsVaultPath(path)) return 0;
  File f = LittleFS.open(path, "r");
  if(!f) return 0;
  uint32_t s = f.isDirectory() ? 0 : (uint32_t)f.size();
  f.close();
  return s;
}

int flexFsPrivRead(const char* path, uint32_t off, void* buf, size_t n){
  if(!fsMounted || !flexFsIsVaultPath(path) || !buf || n == 0) return -1;
  File f = LittleFS.open(path, "r");
  if(!f) return -1;
  if(off){
    if(!f.seek(off)){ f.close(); return 0; }
  }
  size_t r = f.read((uint8_t*)buf, n);
  f.close();
  return (int)r;
}

bool flexFsPrivAppend(const char* path, const void* buf, size_t n){
  if(!fsMounted || !flexFsIsVaultPath(path)) return false;
  if(n > 0 && !buf) return false;
  File f = LittleFS.open(path, "a");
  if(!f) return false;
  size_t w = n ? f.write((const uint8_t*)buf, n) : 0;
  f.close();
  return w == n;
}

bool flexFsPrivWrite(const char* path, const void* buf, size_t n){
  if(!fsMounted || !flexFsIsVaultPath(path)) return false;
  if(n > 0 && !buf) return false;
  File f = LittleFS.open(path, "w");
  if(!f) return false;
  size_t w = n ? f.write((const uint8_t*)buf, n) : 0;
  f.close();
  return w == n;
}

bool flexFsPrivDelete(const char* path){
  if(!fsMounted || !flexFsIsVaultPath(path)) return false;
  if(!LittleFS.exists(path)) return true;                  // ya no esta: hecho
  return LittleFS.remove(path);
}

uint32_t flexFsPrivDirSize(const char* dir){
  if(!fsMounted || !flexFsIsVaultPath(dir)) return 0;
  File d = LittleFS.open(dir);
  if(!d || !d.isDirectory()){ if(d) d.close(); return 0; }
  uint32_t total = 0;
  File e = d.openNextFile();
  while(e){
    if(!e.isDirectory()) total += (uint32_t)e.size();
    e.close();
    e = d.openNextFile();
  }
  d.close();
  return total;
}
