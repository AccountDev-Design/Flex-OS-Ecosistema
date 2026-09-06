// #############################################################
// ##  FLEX OS ULTRA  ·  SESIONES EN LITTLEFS, MODO SEGURO Y RESTABLECIMIENTO
// ##  ----------------------------------------------------------
// ##  El puente con FlexOS_FS para las sesiones de app y los datos de
// ##  usuario, la deteccion de reinicios anormales que activa el modo
// ##  seguro y el estado TRANSACCIONAL del restablecimiento de fabrica.
// ##
// ##  COMO ENCAJA ESTE ARCHIVO
// ##  ----------------------------------------------------------
// ##  Es una PARTE del sketch FlexOS_Ultra.ino, no una unidad de
// ##  traduccion independiente. FlexOS_Ultra.ino lo incluye en el
// ##  orden que fija la cadena de cabeceras (cada modulo incluye al
// ##  anterior), asi que todo el sistema sigue compilandose como UN
// ##  SOLO archivo, exactamente igual que antes de separarlo.
// ##
// ##  Consecuencias practicas, y son las que mantienen esto seguro:
// ##    · Las variables globales se DEFINEN una sola vez, aqui, en el
// ##      modulo al que pertenecen. No hace falta `extern` ni existe
// ##      el riesgo de una definicion duplicada en el enlazado.
// ##    · El ORDEN de definicion es el mismo que tenia el .ino: una
// ##      funcion `static` solo se puede llamar despues de definirse,
// ##      y esa relacion se conserva modulo a modulo.
// ##    · La cadena de includes es LINEAL (Types -> ... -> Recovery),
// ##      asi que no hay dependencias circulares posibles.
// ##    · No lo incluyas por tu cuenta desde otro sitio: el punto de
// ##      entrada del sistema es siempre FlexOS_Ultra.ino.
// #############################################################
#pragma once
#include "FlexOS_Ultra_Prefs.h"   // eslabon anterior de la cadena

// #############################################################
// ##  FLEXOS FS  ·  sesiones de app y datos de usuario
// ##  ------------------------------------------------------
// ##  LittleFS sobre la particion de datos de fabrica (etiqueta
// ##  "spiffs", que es la que trae cualquier esquema de particiones
// ##  del IDE con almacenamiento). Se usa SOLO para lo que no cabe
// ##  en NVS: sesiones de app, borradores y caches. Los valores
// ##  sueltos (banderas, contadores, indices) siguen en NVS, que es
// ##  donde llevan viviendo desde el Milestone 1.
// ##
// ##  REGLA DE ESCRITURA -- nunca se sobrescribe un archivo bueno:
// ##    1) se escribe todo en "<ruta>.tmp",
// ##    2) se cierra y se RELEE para comprobar longitud y CRC,
// ##    3) solo entonces se renombra encima del definitivo.
// ##  Si se corta la corriente a mitad, el .tmp queda huerfano (y se
// ##  barre en el siguiente arranque) y el archivo anterior sigue
// ##  intacto. Es lo que hace que "reiniciar mientras guardaba" no
// ##  pueda dejar una nota o un dibujo a medias.
// ##
// ##  Si la particion no existe o no monta, flexFsReady() queda en false y
// ##  TODO el sistema sigue funcionando: las sesiones se conservan en
// ##  RAM (que es el nivel rapido) y solo se pierde la recuperacion
// ##  tras reinicio. Nunca se bloquea nada por no tener FS.
// #############################################################
#define FS_DIR_SESS      "/System/Sessions"
#define FS_DIR_CACHE     "/System/Cache"
#define SESS_MAGIC       0x584C4631u   // '1FLX' en little endian: marca de archivo de sesion
#define SESS_IO_MAX      1024          // estado de UI, nunca el documento completo

static uint8_t gSessIo[SESS_IO_MAX];   // buffer unico: cero malloc durante las transiciones

// CRC-32 (polinomio de Ethernet, forma reflejada) SIN tabla: 256 entradas de
// tabla serian 1 KB de RAM permanente para algo que se calcula cuatro veces por
// minuto como mucho. Es la MISMA suma que usa cualquier crc32 estandar.
static uint32_t crc32u(const uint8_t* d, size_t n, uint32_t crc){
  crc = ~crc;
  while(n--){
    crc ^= *d++;
    for(int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1)));
  }
  return ~crc;
}

// Escritura ATOMICA con cabecera versionada + CRC. Devuelve false si no habia
// FS, no habia sitio, o la relectura de verificacion no cuadro -- y en ese caso
// el archivo anterior NO se ha tocado.
static bool sessWrite(const char* path, uint16_t ver, uint16_t app, const void* data, size_t len){
  if(!flexFsReady() || !path || !data || len + sizeof(SessHdr) > sizeof(gSessIo)) return false;
  SessHdr h;
  h.magic = SESS_MAGIC; h.ver = ver; h.app = app; h.len = (uint32_t)len;
  h.crc = crc32u((const uint8_t*)data, len, 0);
  memcpy(gSessIo, &h, sizeof(h));
  memcpy(gSessIo + sizeof(h), data, len);
  return flexFsWriteBinAtomic(path, gSessIo, sizeof(h) + len);
}

// Lectura verificada. Devuelve el numero de bytes utiles copiados en out, o 0
// si el archivo no existe, es de otra version, es de otra app o esta corrupto.
// Una sesion que no valida NO se intenta reparar: se ignora y la app abre en un
// estado seguro, que es justo lo que pide no arrastrar datos a medias.
static size_t sessRead(const char* path, uint16_t wantVer, uint16_t app, void* out, size_t maxLen){
  if(!flexFsReady() || !path || !out) return 0;
  uint32_t sz = flexFsSize(path);
  if(sz < sizeof(SessHdr) || sz > sizeof(gSessIo)) return 0;
  int got = flexFsReadBin(path, gSessIo, sz);
  if(got != (int)sz) return 0;
  SessHdr h; memcpy(&h, gSessIo, sizeof(h));
  if(h.magic != SESS_MAGIC || h.ver != wantVer || h.app != app ||
     h.len > maxLen || sizeof(SessHdr) + h.len != sz) return 0;
  memcpy(out, gSessIo + sizeof(SessHdr), h.len);
  return crc32u((const uint8_t*)out, h.len, 0) == h.crc ? h.len : 0;
}

// Borra el CONTENIDO de un directorio (no el directorio). Devuelve cuantos
// archivos elimino. Se usa en "limpiar caches" del Modo seguro y en el
// restablecimiento por etapas.
static int fsWipeDir(const char* dir){
  if(!flexFsReady() || !dir) return 0;
  int n = flexFsCount(dir);
  if(!flexFsDelete(dir)) return 0;
  flexFsMkdir(dir);
  return n;
}

// #############################################################
// ##  MODO SEGURO  ·  deteccion de reinicios anormales
// ##  ------------------------------------------------------
// ##  Reutiliza la deteccion que YA existia para la banda forense
// ##  (esp_reset_reason + showBootBanner): lo unico nuevo es un
// ##  contador PERSISTENTE de reinicios anormales CONSECUTIVOS.
// ##
// ##  QUE CUENTA COMO ANORMAL: PANIC (crash), INT_WDT, TASK_WDT, WDT
// ##  y BROWNOUT. NO cuentan: encendido normal (POWERON), reinicio
// ##  por software -- que es por donde salen el reinicio voluntario y
// ##  el que hace el OTA al terminar de instalar -- ni el despertar
// ##  de deep sleep. Por eso una actualizacion correcta jamas puede
// ##  acercar el sistema al Modo seguro.
// ##
// ##  El contador se limpia despues de SAFE_STABLE_MS de funcionar
// ##  sin reiniciar: si el sistema aguanta un minuto entero, el
// ##  problema anterior no era una cadena de crashes.
// #############################################################
#define SAFE_NVS_NS     "flexsafe"
#define SAFE_FAIL_MAX   3            // reinicios anormales seguidos que activan el modo
#define SAFE_STABLE_MS  60000UL      // arranque estable que limpia el contador
static bool     gSafeMode   = false; // este arranque es en Modo seguro
static uint8_t  gSafeFails  = 0;     // reinicios anormales consecutivos (NVS "fails")
static int      gSafeCause  = 0;     // esp_reset_reason_t del arranque actual
static bool     gSafeCleared = false;// el contador ya se limpio en este arranque
static uint32_t gSafeBootMs = 0;     // millis del arranque, para medir la estabilidad

static void safeSaveFails(){
  Preferences p;
  if(!p.begin(SAFE_NVS_NS, false)) return;
  p.putInt("fails", (int)gSafeFails);
  p.putInt("cause", gSafeCause);
  p.end();
}

// Motivo REAL del arranque, con el texto que ve el usuario en el Modo seguro.
// No se inventa ninguna causa: si el chip no sabe por que reinicio, se dice.
static const char* safeCauseText(){
  switch((esp_reset_reason_t)gSafeCause){
    case ESP_RST_PANIC:    return "Fallo del sistema (crash)";
    case ESP_RST_TASK_WDT: return "Watchdog de tarea (TASK_WDT)";
    case ESP_RST_INT_WDT:  return "Watchdog de interrupcion (INT_WDT)";
    case ESP_RST_WDT:      return "Watchdog del chip";
    case ESP_RST_BROWNOUT: return "Caida de tension (brownout)";
    default:               return "Reinicio inesperado";
  }
}

// Se llama UNA vez, muy temprano en setup(). Decide si este arranque entra en
// Modo seguro y deja el contador actualizado.
static void safeBootEval(){
  esp_reset_reason_t rr = esp_reset_reason();
  gSafeCause  = (int)rr;
  gSafeBootMs = millis();
  bool abnormal = (rr == ESP_RST_PANIC || rr == ESP_RST_INT_WDT ||
                   rr == ESP_RST_TASK_WDT || rr == ESP_RST_WDT || rr == ESP_RST_BROWNOUT);
  Preferences p;
  if(p.begin(SAFE_NVS_NS, true)){
    int savedFails = p.getInt("fails", 0);
    if(savedFails < 0) savedFails = 0; if(savedFails > 250) savedFails = 250;
    gSafeFails = (uint8_t)savedFails;
    if(!abnormal) gSafeCause = p.getInt("cause", (int)rr);   // conserva la causa que disparo la cadena
    p.end();
  }
  if(abnormal){
    if(gSafeFails < 250) gSafeFails++;
    gSafeCause = (int)rr;
    safeSaveFails();
  }
  gSafeMode = (gSafeFails >= SAFE_FAIL_MAX);
  Serial.printf("[SAFE] motivo=%d anormal=%s fallos=%u modo_seguro=%s\n",
                (int)rr, abnormal ? "si" : "no", (unsigned)gSafeFails, gSafeMode ? "SI" : "no");
}

// Arranque estable: a los SAFE_STABLE_MS sin reiniciar, el contador vuelve a 0.
// UNA sola escritura en NVS por arranque (y solo si habia algo que limpiar):
// esto corre desde loop(), asi que no puede permitirse escribir por vuelta.
static void safeStableTick(){
  if(gSafeCleared || gSafeMode) return;
  if(millis() - gSafeBootMs < SAFE_STABLE_MS) return;
  gSafeCleared = true;
  if(gSafeFails == 0) return;
  gSafeFails = 0;
  safeSaveFails();
  Serial.println(F("[SAFE] arranque estable: contador de reinicios anormales a cero"));
}

// Salir del Modo seguro a mano ("Reiniciar normalmente"): pone el contador a 0
// ANTES de reiniciar, o el siguiente arranque volveria a entrar en Modo seguro.
static void safeExitAndReboot(){
  gSafeFails = 0;
  safeSaveFails();
  Serial.println(F("[SAFE] saliendo del Modo seguro -> reinicio normal"));
  delay(40);
  esp_restart();
}

// #############################################################
// ##  RESTABLECIMIENTO DE FABRICA  ·  estado transaccional
// ##  ------------------------------------------------------
// ##  El borrado NO puede quedar a medias si se corta la corriente.
// ##  Por eso antes de tocar un solo dato se escribe un marcador
// ##  persistente en su PROPIO namespace de NVS ("flexreset"), con
// ##  version de formato y etapa en curso. Ese namespace es el UNICO
// ##  que el borrado no limpia: se elimina al final del todo, y solo
// ##  cuando el arranque limpio esta confirmado.
// ##
// ##  Si el aparato se reinicia con el marcador puesto, el arranque
// ##  RETOMA el borrado desde la etapa anotada, antes de montar datos
// ##  privados o abrir ninguna app.
// #############################################################
#define FR_NVS_NS   "flexreset"
#define FR_FMT_VER  1
// Etapas. El orden IMPORTA: es el orden en que se ejecutan y el que se
// retoma tras un corte. Se anaden siempre al final para que un marcador
// escrito por una version anterior nunca signifique otra cosa.
enum {
  FR_ST_IDLE = 0,    // no hay restablecimiento en curso
  FR_ST_ARMED,       // seguridad validada, escrituras normales detenidas
  FR_ST_TOKENS,      // invalidar y borrar credenciales y tokens locales
  FR_ST_REMOTE,      // intento (best-effort) de cerrar sesion remota
  FR_ST_APPDATA,     // datos de apps: sesiones, borradores, caches
  FR_ST_FILES,       // archivos del usuario: formateo de LittleFS
  FR_ST_NVS,         // namespaces conocidos de NVS (menos el propio marcador)
  FR_ST_DEFAULTS,    // valores predeterminados de fabrica
  FR_ST_DONE,        // terminado: falta reiniciar y confirmar el arranque limpio
  FR_ST_FAIL         // una etapa fallo: pantalla de recuperacion con reintento
};
static bool    gFrPending = false;   // hay un restablecimiento a medias (NVS "pending")
static uint8_t gFrStage   = FR_ST_IDLE;
static int     gFrErr     = 0;       // etapa que fallo (0 = ninguna)
// El borrado ya termino y el aparato ha reiniciado, pero el arranque limpio
// todavia no se ha confirmado: el marcador NO se borra hasta llegar al OOBE
// como dispositivo nuevo. Vive aqui arriba porque lo consulta enterOobeLang().
static bool    gFrConfirmPending = false;

static bool frSaveState(){
  Preferences p;
  if(!p.begin(FR_NVS_NS, false)) return false;
  bool wrote = p.putBool("pending", gFrPending) == sizeof(uint8_t) &&
               p.putInt("ver", FR_FMT_VER) == sizeof(int32_t) &&
               p.putInt("stage", gFrStage) == sizeof(int32_t) &&
               p.putInt("err", gFrErr) == sizeof(int32_t);
  p.end();
  if(!wrote) return false;
  // Antes de borrar nada se verifica el marcador leyendolo desde NVS. Si una
  // escritura falla, el motor se detiene: nunca se confia solo en la copia RAM.
  Preferences v;
  if(!v.begin(FR_NVS_NS, true)) return false;
  bool ok = v.getBool("pending", !gFrPending) == gFrPending &&
            v.getInt("ver", 0) == FR_FMT_VER &&
            v.getInt("stage", 0) == gFrStage &&
            v.getInt("err", -1) == gFrErr;
  v.end();
  return ok;
}
static void frLoadState(){
  Preferences p;
  gFrPending = false; gFrStage = FR_ST_IDLE; gFrErr = 0;
  if(!p.begin(FR_NVS_NS, true)) return;
  bool pend = p.getBool("pending", false);
  int ver = p.getInt("ver", 0);
  int st  = p.getInt("stage", FR_ST_IDLE);
  int err     = p.getInt("err", 0);
  p.end();
  // Marcador de otra version de formato: no se sabe que significan sus etapas,
  // asi que se descarta en vez de interpretarlo mal y borrar de mas.
  if(pend && ver == FR_FMT_VER && st == FR_ST_DONE){
    gFrConfirmPending = true;   // el siguiente OOBE confirma y elimina el marcador
  } else if(pend && ver == FR_FMT_VER && st > FR_ST_IDLE && st <= FR_ST_FAIL){
    gFrPending = true; gFrStage = st; gFrErr = err;
  } else if(pend){
    Preferences q;
    if(q.begin(FR_NVS_NS, false)){ q.clear(); q.end(); }
  }
}
// Se llama SOLO cuando el arranque limpio esta confirmado (el sistema ya llego
// al OOBE como dispositivo nuevo). Es el ultimo paso de la transaccion.
static void frClearMarker(){
  Preferences p;
  if(p.begin(FR_NVS_NS, false)){ p.clear(); p.end(); }
  gFrPending = false; gFrStage = FR_ST_IDLE; gFrErr = 0;
  Serial.println(F("[RESET] arranque limpio confirmado: marcador de recuperacion borrado"));
}

// -------- Idiomas --------
#define NLANG 6
static const char* LANG_ENDONYM[NLANG] = {
  "Espa\xC3\xB1ol", "English", "Fran\xC3\xA7" "ais", "Portugu\xC3\xAas", "Italiano", "\xE4\xB8\xAD\xE6\x96\x87" };
// idx de arrays de fecha (ZH no tiene glifos -> usa EN)
static inline int LI(){ return (cfgLang == 5) ? 1 : cfgLang; }

// Cadenas de interfaz. Columnas: ES,EN,FR,PT,IT. ZH usa EN.
enum { S_SELLANG, S_CONTINUE, S_YOURNAME, S_NAMEHINT, S_START, S_SWIPE,
       S_WEATHER, S_NOEVENTS, S_NOTIFS, S_NONOTIFS, S_SOON, S_M2, S_BACK, S_WELCOME,
       // CRONOMETRO (pestanas de Reloj, tabla de vueltas y botones). Se localiza
       // como todo lo demas: el ES de la columna 0 es el de la referencia.
       S_CRN_HOUR, S_CRN_STOPW, S_CRN_TIMER, S_CRN_LAP, S_CRN_SPLIT, S_CRN_TOTAL,
       S_CRN_BLAP, S_CRN_BSTOP, S_CRN_BRESET, S_CRN_BSTART, S_CRN_NOLAPS,
       S_NSTR };
static const char* CH[S_NSTR][5] = {
  {"Selecciona tu idioma","Select your language","Choisis ta langue","Selecione o idioma","Seleziona la lingua"},
  {"Continuar","Continue","Continuer","Continuar","Continua"},
  {"\xC2\xBF" "C\xC3\xB3mo se llama el equipo?","Name your device","Nomme ton appareil","Nomeie o dispositivo","Nomina il dispositivo"},
  {"Toca para escribir","Tap to type","Touche pour \xC3\xA9" "crire","Toque para escrever","Tocca per scrivere"},
  {"Comenzar","Get started","Commencer","Come\xC3\xA7" "ar","Inizia"},
  {"Desliza arriba para desbloquear","Swipe up to unlock","Glisse vers le haut","Deslize para desbloquear","Scorri per sbloccare"},
  {"Clima","Weather","M\xC3\xA9t\xC3\xA9o","Clima","Meteo"},
  {"(Sin eventos)","(No events)","(Aucun \xC3\xA9v\xC3\xA9nement)","(Sem eventos)","(Nessun evento)"},
  {"Notificaciones","Notifications","Notifications","Notifica\xC3\xA7\xC3\xB5" "es","Notifiche"},
  {"(Sin notificaciones)","(No notifications)","(Aucune notification)","(Sem notifica\xC3\xA7\xC3\xB5" "es)","(Nessuna notifica)"},
  {"En construcci\xC3\xB3n","Coming soon","Bient\xC3\xB4t disponible","Em breve","Prossimamente"},
  {"Llega en el Milestone 2","Arrives in Milestone 2","Arrive au Milestone 2","Chega no Milestone 2","Arriva nel Milestone 2"},
  {"Volver","Back","Retour","Voltar","Indietro"},
  {"Bienvenido a","Welcome to","Bienvenue sur","Bem-vindo ao","Benvenuto in"},
  {"Hora","Clock","Heure","Hora","Ora"},
  {"Cron\xC3\xB3metro","Stopwatch","Chronom\xC3\xA8tre","Cron\xC3\xB4metro","Cronometro"},
  {"Temporizador","Timer","Minuteur","Temporizador","Timer"},
  {"Vuelta","Lap","Tour","Volta","Giro"},
  {"Tiempo parcial","Lap time","Temps interm.","Tempo parcial","Tempo parziale"},
  {"Tiempo total","Total time","Temps total","Tempo total","Tempo totale"},
  {"Parcial","Lap","Tour","Parcial","Parziale"},
  {"Det.","Stop","Arr\xC3\xAAt","Parar","Stop"},
  {"Reinic.","Reset","R\xC3\xA9init.","Zerar","Azzera"},
  {"Iniciar","Start","D\xC3\xA9marrer","Iniciar","Avvia"},
  {"(Sin vueltas)","(No laps)","(Aucun tour)","(Sem voltas)","(Nessun giro)"},
};
static const char* t(int id){ return CH[id][LI()]; }

// Etiquetas de apps. Mismo orden que el enum IC_*.
static const char* APP[APP_N][5] = {
  {"Reloj","Clock","Horloge","Rel\xC3\xB3gio","Orologio"},
  {"Galer\xC3\xAD" "a","Gallery","Galerie","Galeria","Galleria"},
  {"Multimedia","Media","Multim\xC3\xA9" "dia","Multim\xC3\xAD" "dia","Multimedia"},
  {"Almacenamiento","Storage","Stockage","Armazenamento","Archivi"},
  {"Modo PC","PC Mode","Mode PC","Modo PC","Modo PC"},
  {"Notas","Notes","Notes","Notas","Note"},
  {"Educaci\xC3\xB3n","Education","\xC3\x89" "ducation","Educa\xC3\xA7\xC3\xA3o","Istruzione"},
  {"Navegador","Browser","Navigateur","Navegador","Browser"},
  {"Code IDE","Code IDE","Code IDE","Code IDE","Code IDE"},
  {"Bienestar","Wellbeing","Bien-\xC3\xAatre","Bem-estar","Benessere"},
  {"Paint","Paint","Dessin","Paint","Disegno"},
  {"Juegos","Games","Jeux","Jogos","Giochi"},
  {"Ajustes","Settings","R\xC3\xA9glages","Ajustes","Impostazioni"},
  {"Calculadora","Calculator","Calculatrice","Calculadora","Calcolatrice"},
  {"Calendario","Calendar","Calendrier","Calend\xC3\xA1rio","Calendario"},
  {"C\xC3\xA1mara","Camera","Appareil","C\xC3\xA2mera","Fotocamera"},
  {"Clima","Weather","M\xC3\xA9t\xC3\xA9o","Clima","Meteo"},
  {"Flex Store","Flex Store","Flex Store","Flex Store","Flex Store"},
  {"Flex Phone","Flex Phone","Flex Phone","Flex Phone","Flex Phone"},
};
static const char* appName(int id){ return APP[id][LI()]; }

// -------- Nombres de dias/meses --------
static const char* WD_FULL[5][7] = {
  {"Domingo","Lunes","Martes","Mi\xC3\xA9rcoles","Jueves","Viernes","S\xC3\xA1" "bado"},
  {"Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"},
  {"Dimanche","Lundi","Mardi","Mercredi","Jeudi","Vendredi","Samedi"},
  {"Domingo","Segunda","Ter\xC3\xA7" "a","Quarta","Quinta","Sexta","S\xC3\xA1" "bado"},
  {"Domenica","Luned\xC3\xAC","Marted\xC3\xAC","Mercoled\xC3\xAC","Gioved\xC3\xAC","Venerd\xC3\xAC","Sabato"},
};
static const char* WD_SHORT[5][7] = {
  {"dom","lun","mar","mi\xC3\xA9","jue","vie","s\xC3\xA1" "b"},
  {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"},
  {"dim","lun","mar","mer","jeu","ven","sam"},
  {"dom","seg","ter","qua","qui","sex","s\xC3\xA1" "b"},
  {"dom","lun","mar","mer","gio","ven","sab"},
};
static const char* MO_FULL[5][12] = {
  {"enero","febrero","marzo","abril","mayo","junio","julio","agosto","septiembre","octubre","noviembre","diciembre"},
  {"January","February","March","April","May","June","July","August","September","October","November","December"},
  {"janvier","f\xC3\xA9vrier","mars","avril","mai","juin","juillet","ao\xC3\xBBt","septembre","octobre","novembre","d\xC3\xA9" "cembre"},
  {"janeiro","fevereiro","mar\xC3\xA7" "o","abril","maio","junho","julho","agosto","setembro","outubro","novembro","dezembro"},
  {"gennaio","febbraio","marzo","aprile","maggio","giugno","luglio","agosto","settembre","ottobre","novembre","dicembre"},
};
static const char* MO_SHORT[5][12] = {
  {"ene","feb","mar","abr","may","jun","jul","ago","sep","oct","nov","dic"},
  {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"},
  {"jan","f\xC3\xA9v","mar","avr","mai","jui","jul","ao\xC3\xBB","sep","oct","nov","d\xC3\xA9" "c"},
  {"jan","fev","mar","abr","mai","jun","jul","ago","set","out","nov","dez"},
  {"gen","feb","mar","apr","mag","giu","lug","ago","set","ott","nov","dic"},
};
