// #############################################################
// ##  FLEX OS ULTRA  ·  APLICACIONES DESCARGADAS EN EL SISTEMA
// ##  ----------------------------------------------------------
// ##  El modelo que convierte el REGISTRO REAL de paquetes instalados
// ##  (/FlexApps, el que mantiene FlexOS_Package de forma transaccional)
// ##  en una lista que la Caja de aplicaciones puede pintar a 30 fps.
// ##
// ##  COMO ENCAJA ESTE ARCHIVO
// ##  ----------------------------------------------------------
// ##  Es una PARTE del sketch FlexOS_Ultra.ino. Va entre Lock y
// ##  AppDrawer: la caja necesita esta lista, y este modulo no necesita
// ##  nada de la caja. No conoce el puente de Flex Store (que se incluye
// ##  mucho despues, al final del .ino), asi que la apertura de una app
// ##  se hace por PETICION: aqui se anota que id hay que abrir y el
// ##  puente la atiende. Es la unica direccion posible sin romper el
// ##  orden de la cadena de cabeceras.
// ##
// ##  LAS TRES REGLAS QUE HACEN QUE ESTO SEA VIABLE EN UN P4
// ##  ----------------------------------------------------------
// ##  1. NADA DE ESCANEAR POR CUADRO. La lista se reconstruye SOLO
// ##     cuando flexPkgRevision() cambia, y ese contador solo sube al
// ##     recuperar el almacenamiento en el arranque, al instalar, al
// ##     actualizar, al desinstalar y al detener/reactivar una app.
// ##     Comparar un uint32_t por cuadro es gratis; recorrer /FlexApps
// ##     por cuadro habria costado la fluidez de la caja.
// ##  2. GENERICO, NUNCA POR NOMBRE. Aqui no hay ni una lista de apps
// ##     conocidas ni un caso especial para ninguna. Cualquier paquete
// ##     que el instalador haya dado por bueno entra; ninguno mas.
// ##  3. UN PAQUETE ROTO NO ROMPE LA CAJA. Una entrada del registro que
// ##     no se pueda leer, o cuyo manifiesto no cuadre, sencillamente NO
// ##     APARECE. Una que aparezca pero no se pueda abrir queda marcada
// ##     y lo dice, sin bloquear la rejilla ni el resto de apps.
// #############################################################
#pragma once
#include "FlexOS_Ultra_Lock.h"

// Tope de la lista: el mismo del registro, para que no pueda haber una app
// instalada que la caja no sea capaz de representar.
#define PKGAPP_MAX          FLEXPKG_MAX_INSTALLED
#define PKGAPP_LABEL_MAX    28

// ICONO DEL PAQUETE. Convenio del sistema: un archivo "icon.f565" en la raiz de
// la version activa, RGB565 little-endian, exactamente 64x64. Va DENTRO del
// paquete, asi que su hash y la firma del desarrollador ya lo cubren: cuando se
// lee aqui, esos bytes ya estan verificados. Cualquier otro tamano se descarta y
// la app usa el icono generico. No se anade ningun campo al manifiesto ni se
// toca el formato firmado: una app que no traiga el archivo funciona igual.
#define PKGAPP_ICON_PX      64
#define PKGAPP_ICON_BYTES   ((size_t)PKGAPP_ICON_PX * PKGAPP_ICON_PX * 2)
#define PKGAPP_ICON_FILE    "icon.f565"
// Cuatro iconos cacheados = 32 KB de PSRAM como techo absoluto. Con mas apps
// instaladas, las que no tienen hueco usan el icono generico: la caja sigue
// completa y el consumo no crece con el catalogo.
#define PKGAPP_ICON_SLOTS   4

// ---- DIAGNOSTICO TEMPORAL DEL CAJON -------------------------------------
// Puesto para cazar el reinicio al desplazar la caja. Es DEPURACION: se apaga
// poniendo esto a 0, y no deja ni una linea en Serial cuando esta apagado.
// Cuando esta encendido, nunca inunda el puerto: una linea al abrir la caja,
// una como mucho cada 500 ms mientras se desplaza, y una por cuadro LENTO
// (>60 ms), que es el unico que puede acercarse al watchdog de tarea.
#define FLEXDRW_DIAG 1

#if FLEXDRW_DIAG
static uint32_t pkgDiagLastMs = 0;
// La marca de agua de la pila del hilo de interfaz: los bytes que NUNCA se han
// usado. Es la medida que delata un marco de pila desbocado, que fue justo la
// causa del reinicio (ver pkgAppsRebuild).
// En ESP-IDF esta llamada ya devuelve BYTES (no palabras, como el FreeRTOS
// original). Es la medida que delata un marco de pila desbocado.
static uint32_t pkgDiagStackFree(){
  return (uint32_t)uxTaskGetStackHighWaterMark(NULL);
}
#endif

// Estado de una app descargada, tal y como lo ve la caja.
enum PkgAppStatus : uint8_t {
  PKGAPP_ST_OK = 0,        // valida: se puede abrir
  PKGAPP_ST_UPDATING,      // Flex Store la esta descargando o instalando ahora
  PKGAPP_ST_ERROR          // instalada pero no abrible (detenida, o fallo al abrir)
};

struct PkgAppEntry {
  char     id[FLEXPKG_ID_MAX + 1];
  char     name[FLEXPKG_NAME_MAX + 1];
  char     version[FLEXPKG_VERSION_MAX + 1];
  uint32_t versionCode;
  uint8_t  runtime;        // FlexPkgRuntime
  uint8_t  state;          // FlexPkgAppState del registro
  uint8_t  status;         // PkgAppStatus
  int8_t   icon;           // ranura en la cache de iconos, -1 = generico
  uint8_t  tint;           // color del icono generico, derivado del id
};

static PkgAppEntry pkgApps[PKGAPP_MAX];
static int         pkgAppsN   = 0;
static uint32_t    pkgAppsRev = 0;      // revision con la que se construyo la lista
static bool        pkgAppsBuilt = false;

#if FLEXDRW_DIAG
static void pkgDiagLine(const char* que, int scroll, int filas,
                        int band0, int band1, uint32_t frameUs){
  Serial.printf("[CAJON] %s pkg=%d filas=%d scroll=%d banda=%d..%d %luus "
                "pila_libre=%lu heap=%lu int=%lu psram=%lu\n",
                que, pkgAppsN, filas, scroll, band0, band1,
                (unsigned long)frameUs,
                (unsigned long)pkgDiagStackFree(),
                (unsigned long)esp_get_free_heap_size(),
                (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}
#endif

// Cache de iconos. Se llena durante la reconstruccion (que ya es un evento
// raro), nunca durante un cuadro.
static uint16_t*   pkgIconBuf[PKGAPP_ICON_SLOTS] = { NULL, NULL, NULL, NULL };
static int         pkgIconUsed = 0;

// Peticion de apertura pendiente. La escribe la caja al tocar una app y la
// atiende el puente de Flex Store, que es quien sabe abrir cada runtime.
static char        pkgAppLaunchId[FLEXPKG_ID_MAX + 1] = "";
// true cuando la apertura vino de la caja: al salir hay que volver al
// escritorio, no al listado de la tienda.
static bool        pkgAppLaunchFromDrawer = false;
// Motivo del ultimo fallo de apertura, para poder decirlo en la caja.
static char        pkgAppLastError[96] = "";

// ---- Cache de iconos ----------------------------------------------------
static void pkgAppIconsFree(){
  for(int i = 0; i < PKGAPP_ICON_SLOTS; i++){
    if(pkgIconBuf[i]){ heap_caps_free(pkgIconBuf[i]); pkgIconBuf[i] = NULL; }
  }
  pkgIconUsed = 0;
  // Las entradas que apuntaban a una ranura vuelven al icono generico: la caja
  // sigue pintando, solo pierde la imagen propia hasta la siguiente lectura.
  for(int i = 0; i < pkgAppsN; i++) pkgApps[i].icon = -1;
}

// Carga el icono del paquete si existe y es exactamente del tamano acordado.
// Devuelve la ranura usada, o -1 (que significa "usa el generico", no "error").
static int pkgAppIconLoad(const char* packageId){
  if(pkgIconUsed >= PKGAPP_ICON_SLOTS) return -1;
  char root[360], path[400];
  if(!flexPkgActiveRoot(packageId, root, sizeof(root))) return -1;
  int n = snprintf(path, sizeof(path), "%s/%s", root, PKGAPP_ICON_FILE);
  if(n <= 0 || (size_t)n >= sizeof(path)) return -1;
  if(!flexFsExists(path)) return -1;
  // Tamano EXACTO. Un archivo mas grande o mas pequeno no se interpreta ni se
  // rellena: se ignora y la app se queda con el icono generico.
  if(flexFsSize(path) != (uint32_t)PKGAPP_ICON_BYTES) return -1;
  int slot = pkgIconUsed;
  if(!pkgIconBuf[slot]){
    pkgIconBuf[slot] = (uint16_t*)heap_caps_malloc(PKGAPP_ICON_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if(!pkgIconBuf[slot]) return -1;                  // sin PSRAM: generico, y ya
  }
  if(flexFsReadBin(path, pkgIconBuf[slot], PKGAPP_ICON_BYTES) != (int)PKGAPP_ICON_BYTES) return -1;
  pkgIconUsed++;
  return slot;
}

// ---- Construccion de la lista -------------------------------------------
// Color del icono generico: FNV-1a del identificador. El mismo paquete tiene
// siempre el mismo color, en cualquier placa y despues de cualquier
// actualizacion, sin guardar nada.
static uint8_t pkgAppTint(const char* id){
  uint32_t h = 2166136261u;
  for(const char* p = id; *p; p++){ h ^= (uint8_t)*p; h *= 16777619u; }
  return (uint8_t)(h % 8u);
}

// Comparacion de nombres sin distinguir mayusculas. Escrita aqui a proposito:
// el resto del sistema tampoco usa strcasecmp (no es C estandar y su
// comportamiento depende de la localizacion), y estos nombres vienen de un
// manifiesto de terceros.
static inline char pkgAppFold(char c){ return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c; }
static int pkgAppNameCmp(const char* a, const char* b){
  while(*a && *b){
    char ca = pkgAppFold(*a++), cb = pkgAppFold(*b++);
    if(ca != cb) return (int)(unsigned char)ca - (int)(unsigned char)cb;
  }
  return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

// LA UNICA PUERTA. Una entrada del registro que no pase por aqui no existe para
// la caja. Es defensa en profundidad: el instalador ya valida cabecera, rutas,
// hashes, huella del desarrollador, firma y grant antes de activar una version,
// pero la caja no da por hecho que lo que lee de LittleFS sea coherente.
static bool pkgAppAccept(const FlexPkgInfo& info){
  if(!flexPkgCoreSafeId(info.id)) return false;       // id vacio o con rutas dentro
  if(!info.name[0]) return false;                     // sin nombre en el manifiesto
  if(!info.entry[0]) return false;                    // sin punto de entrada
  if(info.runtime != FLEXPKG_RT_UI1 && info.runtime != FLEXPKG_RT_APP1) return false;
  return true;
}

// EL BUFFER DE LECTURA VA EN PSRAM, NUNCA EN LA PILA.
//
// ESTO ES LA REGRESION QUE REINICIABA EL FIRMWARE. Aqui habia un
// `FlexPkgInfo raw[PKGAPP_MAX]` local. sizeof(FlexPkgInfo) es 844 bytes y
// PKGAPP_MAX es 24: 20.256 bytes de datos que, con el resto del marco, medidos
// con -fstack-usage daban 21.360 bytes de PILA en una sola llamada. La siguiente
// funcion mas glotona de TODO el firmware usa 4.464. El loopTask de Arduino en
// ESP32 tiene 8.192 bytes: el marco se comia la pila entera y ~13 KB de lo que
// hubiera debajo (pilas de otras tareas, metadatos del heap, descriptores DMA).
//
// Un desbordamiento asi no falla en la instruccion que lo provoca: corrompe en
// silencio y el sistema muere despues, cuando alguien usa lo pisado -- al
// desplazar la caja, que es cuando la DMA2D y la PSRAM trabajan a fondo. De ahi
// la pantalla cian (el panel alimentado con basura) y el reinicio.
//
// La reconstruccion es un evento RARO (instalar, actualizar, desinstalar,
// detener, arrancar), asi que una reserva temporal en PSRAM no cuesta nada y
// deja el marco de esta funcion en unas decenas de bytes.
static void pkgAppsRebuild(){
  pkgAppIconsFree();

  FlexPkgInfo* raw = (FlexPkgInfo*)heap_caps_malloc(sizeof(FlexPkgInfo) * PKGAPP_MAX,
                                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if(!raw){
    // Sin memoria para leer el registro NO se vacia la lista: se conserva la
    // que hubiera y se reintenta en la siguiente invalidacion. Quedarse sin
    // PSRAM un instante no puede hacer desaparecer las apps del usuario.
    pkgAppsBuilt = pkgAppsN > 0;
    return;
  }

  pkgAppsN = 0;
  int n = flexPkgList(raw, PKGAPP_MAX);
  if(n < 0) n = 0;
  if(n > PKGAPP_MAX) n = PKGAPP_MAX;                 // el registro no puede devolver mas
  for(int i = 0; i < n && pkgAppsN < PKGAPP_MAX; i++){
    if(!pkgAppAccept(raw[i])) continue;
    PkgAppEntry& e = pkgApps[pkgAppsN];
    memset(&e, 0, sizeof(e));
    snprintf(e.id,      sizeof(e.id),      "%s", raw[i].id);
    snprintf(e.name,    sizeof(e.name),    "%s", raw[i].name);
    snprintf(e.version, sizeof(e.version), "%s", raw[i].versionName[0] ? raw[i].versionName : "1.0");
    e.versionCode = raw[i].versionCode;
    e.runtime = raw[i].runtime;
    e.state   = raw[i].state;
    // Una app detenida (por el usuario o por el sistema) SIGUE apareciendo: es
    // suya y tiene que poder volver a activarla. Lo que no hace es abrirse.
    e.status  = (raw[i].state == FLEXPKG_APP_ENABLED) ? PKGAPP_ST_OK : PKGAPP_ST_ERROR;
    e.tint    = pkgAppTint(e.id);
    e.icon    = (int8_t)pkgAppIconLoad(e.id);
    pkgAppsN++;
  }
  heap_caps_free(raw);

  // Orden estable por nombre, sin distinguir mayusculas. flexPkgList devuelve
  // lo que enumere LittleFS, que no tiene por que ser el mismo orden entre
  // arranques: sin esto la caja podria reordenarse sola.
  for(int i = 1; i < pkgAppsN; i++){
    PkgAppEntry key = pkgApps[i];
    int j = i - 1;
    while(j >= 0 && pkgAppNameCmp(pkgApps[j].name, key.name) > 0){ pkgApps[j + 1] = pkgApps[j]; j--; }
    pkgApps[j + 1] = key;
  }
  pkgAppsBuilt = true;
}

// PUNTO DE ENTRADA DEL MODELO. Barato de llamar: lo normal es comparar un
// entero y volver. Solo recorre el registro cuando algo pudo cambiarlo.
static inline void pkgAppsEnsure(){
  uint32_t rev = flexPkgRevision();
  if(pkgAppsBuilt && rev == pkgAppsRev) return;
  pkgAppsRebuild();
  pkgAppsRev = rev;
}

// Invalidacion explicita, para el puente de Flex Store: instalar y desinstalar
// ya suben la revision, pero llamar a esto deja el efecto inmediato y visible
// en el mismo evento, sin esperar a la siguiente apertura de la caja.
static inline void pkgAppsInvalidate(){ pkgAppsBuilt = false; }

static int pkgAppFind(const char* packageId){
  if(!packageId || !packageId[0]) return -1;
  for(int i = 0; i < pkgAppsN; i++) if(!strcmp(pkgApps[i].id, packageId)) return i;
  return -1;
}

// ---- Estado "actualizando" ----------------------------------------------
// No se guarda en la entrada: lo sabe la tienda. Asi una descarga en curso no
// obliga a reconstruir la lista, y al terminar la instalacion la revision sube
// y la entrada se rehace con su version nueva.
//
// PERO ESO NO SE PREGUNTA POR ICONO Y POR CUADRO. flexStoreBusyPackage() toma
// el mutex de Flex Store con portMAX_DELAY, y el hilo de interfaz no puede
// quedarse esperando a la tarea de descarga en mitad de componer una banda: si
// se pasa del plazo, salta el watchdog de tarea y el sistema se reinicia.
// Se MUESTREA una vez por cuadro, antes de dibujar nada, y el dibujo lee esta
// copia. Un cuadro de retraso en un indicador no lo nota nadie; bloquear el
// hilo grafico, si.
static char pkgAppBusyId[FLEXPKG_ID_MAX + 1] = "";
static void pkgAppSampleBusy(){
  pkgAppBusyId[0] = 0;
  flexStoreBusyPackage(pkgAppBusyId, sizeof(pkgAppBusyId));
}
static uint8_t pkgAppStatus(int index){
  if(index < 0 || index >= pkgAppsN) return PKGAPP_ST_ERROR;
  if(pkgAppBusyId[0] && !strcmp(pkgAppBusyId, pkgApps[index].id)) return PKGAPP_ST_UPDATING;
  return pkgApps[index].status;
}
// Version que SI consulta a la tienda. Solo para fuera del camino de pintado
// (la ficha de informacion, que se abre por un toque, no por cuadro).
static uint8_t pkgAppStatusLive(int index){
  if(index < 0 || index >= pkgAppsN) return PKGAPP_ST_ERROR;
  pkgAppSampleBusy();
  return pkgAppStatus(index);
}

static const char* pkgAppStatusText(uint8_t status){
  if(status == PKGAPP_ST_UPDATING) return "Actualizando";
  if(status == PKGAPP_ST_ERROR)    return "No disponible";
  return "";
}

// ---- Peticion de apertura -----------------------------------------------
// Anota que app hay que abrir. Quien la abre de verdad es el puente de Flex
// Store, por el MISMO camino que usa la tienda (bifurcacion por runtime,
// validacion del grant en cada arranque, presupuesto por tick): la caja no abre
// una segunda puerta al runtime.
static void pkgAppRequestLaunch(const char* packageId){
  snprintf(pkgAppLaunchId, sizeof(pkgAppLaunchId), "%s", packageId ? packageId : "");
  pkgAppLaunchFromDrawer = true;
}
static inline bool pkgAppLaunchPending(){ return pkgAppLaunchId[0] != 0; }
static inline void pkgAppLaunchClear(){ pkgAppLaunchId[0] = 0; }

// El puente avisa aqui cuando una apertura falla. La entrada queda marcada
// hasta la siguiente reconstruccion (es decir, hasta que se reinstale, se
// actualice o se reactive), asi que el fallo no se olvida ni se repite en
// silencio, y tampoco obliga a volver a leer el sistema de archivos.
static void pkgAppMarkError(const char* packageId, const char* reason){
  snprintf(pkgAppLastError, sizeof(pkgAppLastError), "%s", reason ? reason : "No se pudo abrir");
  int i = pkgAppFind(packageId);
  if(i >= 0) pkgApps[i].status = PKGAPP_ST_ERROR;
}

// ---- Icono ---------------------------------------------------------------
// Paleta del icono generico. Colores planos y opacos, del mismo registro visual
// que los iconos nativos, que tambien son marca y no siguen al tema.
static uint16_t pkgAppTintColor(uint8_t tint){
  switch(tint & 7){
    case 0:  return rgb565( 60, 120, 245);
    case 1:  return rgb565( 34, 160, 110);
    case 2:  return rgb565(232, 108,  46);
    case 3:  return rgb565(150,  86, 220);
    case 4:  return rgb565(214,  62, 104);
    case 5:  return rgb565( 22, 158, 190);
    case 6:  return rgb565(190, 148,  30);
    default: return rgb565( 92, 100, 118);
  }
}

// Dibuja el icono de una app descargada en la misma caja de SxS que usa
// drawAppIcon() para las nativas. NO toca drawAppIcon: los iconos del sistema
// se quedan exactamente como estan.
static void pkgAppDrawIcon(int index, int x, int y, int S){
  if(index < 0 || index >= pkgAppsN || S <= 0) return;
  // RECORTE POR CAJA, ANTES DE DIBUJAR NADA. La misma guarda que drawAppIcon()
  // tiene para los iconos nativos, y por el mismo motivo: al desplazar la caja
  // la mayoria de los iconos caen FUERA de la banda que se esta componiendo.
  // Sin esto, cada uno ejecutaba igual sus miles de escrituras para que px() las
  // descartara una a una -- trabajo puro por cuadro, justo lo que no cabe en el
  // presupuesto de un desplazamiento a 30 fps.
  {
    const int M = 2;
    if(gLand){
      if(x + S + M <= gClipY0 || x - M > gClipY1) return;
    } else {
      if(y + S + M <= gClipY0 || y - M > gClipY1) return;
      if(x + S + M <= gClipX0 || x - M > gClipX1) return;
    }
  }
  const PkgAppEntry& e = pkgApps[index];
  int r = S * 22 / 100;                                  // mismo radio que iconBase()
  if(e.icon >= 0 && e.icon < PKGAPP_ICON_SLOTS && pkgIconBuf[e.icon]){
    // Icono propio del paquete: vecino mas cercano hasta S, recortado a la
    // esquina redondeada por la propia mascara del fondo.
    fillRoundRect(x, y, S, S, r, rgb565(18, 20, 26));
    const uint16_t* src = pkgIconBuf[e.icon];
    for(int j = 0; j < S; j++){
      // Solo las filas que caen en la banda: el resto ni se recorre.
      if(!gLand && (y + j < gClipY0 || y + j > gClipY1)) continue;
      int sy = j * PKGAPP_ICON_PX / S;
      if(sy < 0) sy = 0; if(sy >= PKGAPP_ICON_PX) sy = PKGAPP_ICON_PX - 1;
      int ins = rrInset(j, S, r);                        // sangrado de la esquina redondeada
      if(ins < 0) ins = 0; if(ins > S / 2) ins = S / 2;
      for(int i = ins; i < S - ins; i++){
        int sx = i * PKGAPP_ICON_PX / S;
        if(sx < 0) sx = 0; if(sx >= PKGAPP_ICON_PX) sx = PKGAPP_ICON_PX - 1;
        px(x + i, y + j, src[(size_t)sy * PKGAPP_ICON_PX + sx]);
      }
    }
  } else {
    // Icono generico SEGURO: color estable derivado del id e inicial del nombre.
    // Nunca depende de datos del paquete mas alla de su nombre, asi que un
    // paquete raro no puede dibujar nada raro.
    iconBase(x, y, S, pkgAppTintColor(e.tint), 22);
    char c0 = e.name[0];
    if(c0 >= 'a' && c0 <= 'z') c0 = (char)(c0 - ('a' - 'A'));
    // Solo se acepta un ASCII imprimible como inicial: un nombre que empiece por
    // un byte UTF-8 suelto pintaria un glifo cualquiera. Con "?" no se miente.
    if(c0 < '0' || c0 > 'z') c0 = '?';
    char ini[2] = { c0, 0 };
    drawTextC(x + S / 2, y + S / 2 - S / 5, ini, S >= 60 ? 5 : 3, rgb565(255, 255, 255));
  }
}
