// #############################################################
// ##  FLEX OS ULTRA  ·  PROTECCION CONTRA ROBO  ·  nucleo
// ##  ----------------------------------------------------------
// ##  Ajustes -> Seguridad y privacidad -> Proteccion contra robo.
// ##
// ##  Aqui vive TODO lo que no dibuja: el estado de la funcion, la
// ##  preferencia en NVS, el enganche al sensor, la alimentacion del
// ##  clasificador, el Event Manager (correlacion de incidentes), el
// ##  historial persistente y el bloqueo de seguridad. Las pantallas y
// ##  la animacion viven en el modulo siguiente de la cadena
// ##  (FlexOS_Ultra_TheftUI.h), igual que Device Care reparte su app
// ##  entre DeviceCare / DeviceTests / FallAlert.
// ##
// ##  QUE PUEDE AFIRMAR ESTA FUNCION
// ##  ----------------------------------------------------------
// ##  Un IMU mide aceleracion y giro. NO sabe quien sujeta el aparato
// ##  ni con que intencion. Por eso en todo el sistema -- interfaz,
// ##  historial, aviso de bloqueo y este codigo -- se dice siempre
// ##  POSIBLE ARREBATO, nunca "robo". Lo que se detecta es un PATRON
// ##  DE MOVIMIENTO COMPATIBLE con que alguien arranque el aparato de
// ##  la mano, y la cifra que lo acompana es una confianza.
// ##
// ##  DE DONDE SALEN LOS DATOS
// ##  ----------------------------------------------------------
// ##      FlexOS_BNO085.cpp     driver (bus del tactil, hilo del tactil)
// ##            |
// ##      Flex IMU Service      quien manda sobre el sensor
// ##            |
// ##      Flex Motion Engine    UNA muestra por informe
// ##            |
// ##      +-----+------------------------+
// ##      |                              |
// ##  Flex Device Care          Proteccion contra robo
// ##  (FlexOS_FallDetect)       (FlexOS_Theft)
// ##      |                              |
// ##      +----------> Event Manager <---+
// ##
// ##  NO se inicializa el BNO085 por segunda vez, NO se abre un
// ##  segundo bus y NO se lee el sensor desde otra tarea: se adquiere
// ##  el servicio (imuAcquire/imuRelease, con su conteo de
// ##  consumidores) y se consume la muestra que el motor publica.
// ##
// ##  COMO ENCAJA ESTE ARCHIVO
// ##  ----------------------------------------------------------
// ##  Es una PARTE del sketch FlexOS_Ultra.ino, no una unidad de
// ##  traduccion independiente. FlexOS_Ultra.ino lo incluye en el
// ##  orden que fija la cadena de cabeceras (cada modulo incluye al
// ##  anterior), asi que todo el sistema se sigue compilando como UN
// ##  SOLO archivo. No lo incluyas por tu cuenta desde otro sitio.
// #############################################################
#pragma once
#include "FlexOS_Ultra_AppCompass.h"   // eslabon anterior de la cadena
#include "FlexOS_Theft.h"              // clasificador puro (probado en el PC)

// #############################################################
// ##  TEXTOS  ·  mismo mecanismo de idioma que el resto del sistema
// ##  ------------------------------------------------------
// ##  Tabla propia con el MISMO indice de idioma (LI()) que usa t().
// ##  Vive aqui, junto a la funcion, en vez de engordar el enum
// ##  compartido: son cadenas de UNA sola pantalla y asi se anaden o
// ##  se corrigen sin tocar el nucleo. Columnas ES, EN, FR, PT, IT.
// #############################################################
enum {
  TPS_TITLE, TPS_PROTECTION, TPS_ON, TPS_OFF, TPS_MONITORING,
  TPS_PAUSED, TPS_PAUSEDWHY, TPS_LOCKEDST, TPS_MODULESUB,
  TPS_STATE, TPS_CONNECTED, TPS_NOTFOUND, TPS_DETECTING, TPS_LOST, TPS_BUSERR,
  TPS_NEEDIMU, TPS_NEEDBODY, TPS_RECHECK, TPS_LASTEVENT, TPS_NOEVENTS,
  TPS_EMPTYHIST, TPS_HISTORY, TPS_SENS, TPS_SNATCH, TPS_SNATCHFALL,
  TPS_LOCKON, TPS_LOCKOFF, TPS_BEFOREIMPACT, TPS_LOCKTITLE, TPS_LOCKBODY,
  TPS_LOCKHINT, TPS_TODAY,
  TPS_YESTERDAY, TPS_DETAIL, TPS_CONF, TPS_SEQUENCE, TPS_IMPACT, TPS_NOIMPACT,
  TPS_LOCKFIELD, TPS_DISCONN, TPS_DISCONNSUB,
  TPS_RECONN, TPS_RECONNSUB, TPS_FOOT, TPS_PULL, TPS_ESCAPE,
  TPS_BURST, TPS_N
};
static const char* TPH[TPS_N][5] = {
  {"Protecci\xC3\xB3n contra robo","Theft protection","Protection antivol","Prote\xC3\xA7\xC3\xA3o contra roubo","Protezione antifurto"},
  {"Protecci\xC3\xB3n","Protection","Protection","Prote\xC3\xA7\xC3\xA3o","Protezione"},
  {"Activada","On","Activ\xC3\xA9""e","Ativada","Attiva"},
  {"Desactivada","Off","D\xC3\xA9sactiv\xC3\xA9""e","Desativada","Disattiva"},
  {"Monitoreando movimiento\xE2\x80\xA6","Monitoring motion\xE2\x80\xA6","Surveillance du mouvement\xE2\x80\xA6","Monitorando movimento\xE2\x80\xA6","Monitoraggio del movimento\xE2\x80\xA6"},
  {"Protecci\xC3\xB3n pausada","Protection paused","Protection en pause","Prote\xC3\xA7\xC3\xA3o pausada","Protezione in pausa"},
  {"La detecci\xC3\xB3n de movimiento no est\xC3\xA1 disponible.",
   "Motion detection is not available.",
   "La d\xC3\xA9tection de mouvement n'est pas disponible.",
   "A detec\xC3\xA7\xC3\xA3o de movimento n\xC3\xA3o est\xC3\xA1 dispon\xC3\xADvel.",
   "Il rilevamento del movimento non \xC3\xA8 disponibile."},
  {"Bloqueado por seguridad","Locked for safety","Verrouill\xC3\xA9 par s\xC3\xA9""curit\xC3\xA9","Bloqueado por seguran\xC3\xA7""a","Bloccato per sicurezza"},
  {"TENSTAR / GY-BNO085","TENSTAR / GY-BNO085","TENSTAR / GY-BNO085","TENSTAR / GY-BNO085","TENSTAR / GY-BNO085"},
  {"Estado","State","\xC3\x89tat","Estado","Stato"},
  {"Conectado","Connected","Connect\xC3\xA9","Conectado","Connesso"},
  {"No detectado","Not detected","Non d\xC3\xA9tect\xC3\xA9","N\xC3\xA3o detectado","Non rilevato"},
  {"Detectando\xE2\x80\xA6","Detecting\xE2\x80\xA6","D\xC3\xA9tection\xE2\x80\xA6","Detectando\xE2\x80\xA6","Rilevamento\xE2\x80\xA6"},
  {"Desconectado","Disconnected","D\xC3\xA9""connect\xC3\xA9","Desconectado","Disconnesso"},
  {"Error del bus","Bus error","Erreur de bus","Erro do barramento","Errore del bus"},
  {"Requiere m\xC3\xB3""dulo IMU","Needs an IMU module","Module IMU requis","Requer m\xC3\xB3""dulo IMU","Richiede un modulo IMU"},
  {"Protecci\xC3\xB3n contra robo necesita un m\xC3\xB3""dulo IMU compatible para detectar movimientos bruscos y patrones de posible arrebato.",
   "Theft protection needs a compatible IMU module to detect sharp movements and possible-snatch patterns.",
   "La protection antivol a besoin d'un module IMU compatible pour d\xC3\xA9tecter les mouvements brusques et les sch\xC3\xA9mas d'arrachage possible.",
   "A prote\xC3\xA7\xC3\xA3o contra roubo precisa de um m\xC3\xB3""dulo IMU compat\xC3\xADvel para detectar movimentos bruscos e padr\xC3\xB5""es de poss\xC3\xADvel arrebatamento.",
   "La protezione antifurto richiede un modulo IMU compatibile per rilevare movimenti bruschi e schemi di possibile scippo."},
  {"Volver a comprobar","Check again","V\xC3\xA9rifier \xC3\xA0 nouveau","Verificar de novo","Controlla di nuovo"},
  {"\xC3\x9Altimo evento","Last event","Dernier \xC3\xA9v\xC3\xA9nement","\xC3\x9Altimo evento","Ultimo evento"},
  {"Sin eventos","No events","Aucun \xC3\xA9v\xC3\xA9nement","Sem eventos","Nessun evento"},
  {"Todav\xC3\xAD""a no hay nada registrado.","Nothing recorded yet.","Rien d'enregistr\xC3\xA9 pour l'instant.","Ainda n\xC3\xA3o h\xC3\xA1 registros.","Ancora nessuna registrazione."},
  {"Historial de seguridad","Security history","Historique de s\xC3\xA9""curit\xC3\xA9","Hist\xC3\xB3rico de seguran\xC3\xA7""a","Cronologia di sicurezza"},
  {"Sensibilidad","Sensitivity","Sensibilit\xC3\xA9","Sensibilidade","Sensibilit\xC3\xA0"},
  {"Posible arrebato","Possible snatch","Arrachage possible","Poss\xC3\xADvel arrebatamento","Possibile scippo"},
  {"Posible arrebato + ca\xC3\xAD""da","Possible snatch + fall","Arrachage possible + chute","Poss\xC3\xADvel arrebatamento + queda","Possibile scippo + caduta"},
  {"Bloqueo activado","Lock engaged","Verrouillage activ\xC3\xA9","Bloqueio ativado","Blocco attivato"},
  {"No activado","Not engaged","Non activ\xC3\xA9","N\xC3\xA3o ativado","Non attivato"},
  {"Arrebato detectado antes del impacto",
   "Snatch detected before the impact",
   "Arrachage d\xC3\xA9tect\xC3\xA9 avant l'impact",
   "Arrebatamento detectado antes do impacto",
   "Scippo rilevato prima dell'impatto"},
  {"FLEX OS BLOQUEADO","FLEX OS LOCKED","FLEX OS VERROUILL\xC3\x89","FLEX OS BLOQUEADO","FLEX OS BLOCCATO"},
  {"Movimiento brusco compatible con un posible arrebato.",
   "Sharp movement consistent with a possible snatch.",
   "Mouvement brusque compatible avec un arrachage possible.",
   "Movimento brusco compat\xC3\xADvel com um poss\xC3\xADvel arrebatamento.",
   "Movimento brusco compatibile con un possibile scippo."},
  {"Por seguridad, desbloquea para continuar.",
   "For your safety, unlock to continue.",
   "Par s\xC3\xA9""curit\xC3\xA9, d\xC3\xA9verrouille pour continuer.",
   "Por seguran\xC3\xA7""a, desbloqueia para continuar.",
   "Per sicurezza, sblocca per continuare."},
  {"HOY","TODAY","AUJOURD'HUI","HOJE","OGGI"},
  {"AYER","YESTERDAY","HIER","ONTEM","IERI"},
  {"Detalle del evento","Event detail","D\xC3\xA9tail de l'\xC3\xA9v\xC3\xA9nement","Detalhe do evento","Dettaglio evento"},
  {"Confianza","Confidence","Confiance","Confian\xC3\xA7""a","Attendibilit\xC3\xA0"},
  {"Secuencia","Sequence","S\xC3\xA9quence","Sequ\xC3\xAAncia","Sequenza"},
  {"Impacto posterior","Impact afterwards","Impact ensuite","Impacto posterior","Impatto successivo"},
  {"Sin impacto","No impact","Aucun impact","Sem impacto","Nessun impatto"},
  {"Bloqueo","Lock","Verrouillage","Bloqueio","Blocco"},
  {"M\xC3\xB3""dulo IMU desconectado","IMU module disconnected","Module IMU d\xC3\xA9""connect\xC3\xA9","M\xC3\xB3""dulo IMU desconectado","Modulo IMU disconnesso"},
  {"Protecci\xC3\xB3n pausada","Protection paused","Protection en pause","Prote\xC3\xA7\xC3\xA3o pausada","Protezione in pausa"},
  {"BNO085 reconectado","BNO085 reconnected","BNO085 reconnect\xC3\xA9","BNO085 reconectado","BNO085 riconnesso"},
  {"Protecci\xC3\xB3n contra robo restaurada","Theft protection restored","Protection antivol r\xC3\xA9tablie","Prote\xC3\xA7\xC3\xA3o contra roubo restaurada","Protezione antifurto ripristinata"},
  {"El IMU solo puede ver un patr\xC3\xB3n de movimiento, no una intenci\xC3\xB3n.",
   "An IMU can only see a motion pattern, never an intent.",
   "Un IMU ne voit qu'un sch\xC3\xA9ma de mouvement, jamais une intention.",
   "O IMU s\xC3\xB3 v\xC3\xAA um padr\xC3\xA3o de movimento, nunca uma inten\xC3\xA7\xC3\xA3o.",
   "Un IMU vede solo uno schema di movimento, mai un'intenzione."},
  {"Tir\xC3\xB3n","Pull","Traction","Puxao","Strattone"},
  {"Separaci\xC3\xB3n","Separation","S\xC3\xA9paration","Separa\xC3\xA7\xC3\xA3o","Separazione"},
  {"Duraci\xC3\xB3n","Duration","Dur\xC3\xA9""e","Dura\xC3\xA7\xC3\xA3o","Durata"}
};
static const char* tpt(int id){ return TPH[id][LI()]; }

// #############################################################
// ##  ESTADO DE LA FUNCION  (enunciado 22: sin estados ambiguos)
// #############################################################
enum {
  TP_ST_OFF = 0,     // DESACTIVADA  · no monitorea
  TP_ST_ACTIVE,      // ACTIVA       · monitoreando
  TP_ST_PAUSED,      // PAUSADA      · el IMU no esta disponible
  TP_ST_DETECTED,    // ARREBATO_DETECTADO · bloqueo en curso
  TP_ST_LOCKED       // BLOQUEADA    · hasta el desbloqueo explicito
};

static bool        tpOn        = false;   // el usuario activo la funcion (NVS)
static uint8_t     tpSens      = FLEXTHEFT_SENS_NORMAL;
static bool        tpHold      = false;   // este modulo tiene el servicio IMU adquirido
static FlexTheftDet tpDet;                // ~2,4 KB, estatico: cero reservas dinamicas
static uint32_t    tpMotionSeq = 0;       // ultima muestra vista del Flex Motion Engine
static bool        tpImuLive   = false;   // ultimo estado conocido del sensor
static uint32_t    tpPausedMs  = 0;       // millis en que se perdio el sensor

// ---- Bloqueo de seguridad ----
// PERSISTE EN NVS a proposito. Si el bloqueo solo viviera en RAM, quitar la
// bateria o reiniciar seria la via de escape de la propia proteccion.
static bool     tpLocked   = false;
static uint32_t tpLockUtc  = 0;           // epoca UTC del evento que lo activo
static uint8_t  tpLockKind = 0;           // TP_EV_*
static bool     tpLockPending = false;    // hay que bloquear en cuanto se pueda
static uint32_t tpLockPendMs  = 0;

// #############################################################
// ##  HISTORIAL PERSISTENTE
// ##  ------------------------------------------------------
// ##  Un archivo pequeno en la particion de datos, con la misma
// ##  escritura recuperable que usa el resto del sistema
// ##  (flexFsWriteBinAtomic: temporal, verificacion y sustitucion).
// ##  Son 16 registros de 16 bytes: menos de 300 bytes en total.
// ##
// ##  SOLO EVENTOS. Aqui NO se guarda ni una muestra del IMU: el flujo
// ##  del sensor vive en la ventana temporal de RAM y muere con ella.
// ##  Lo unico que sobrevive es lo que paso, cuando y con que pruebas.
// #############################################################
#define TP_HIST_PATH   FLEXFS_DIR_SYS "/theft.bin"
#define TP_HIST_MAGIC  0x5446544Du        // 'TFTM'
#define TP_HIST_VER    1
#define TP_HIST_MAX    16

enum { TP_EV_SNATCH = 0, TP_EV_SNATCH_FALL };
#define TP_F_LOCKED   0x01                // el bloqueo se activo con este evento

struct TpRec {
  uint32_t utc;       // instante (epoca UTC del reloj del sistema)
  uint8_t  kind;      // TP_EV_*
  uint8_t  conf;      // confianza 0..100
  uint16_t reasons;   // FLEXTHEFT_R_*
  uint8_t  impact;    // pico del impacto posterior en decimas de g (0 = ninguno)
  uint8_t  pull;      // pico del tiron en decimas de g
  uint16_t escape;    // ms de separacion posterior
  uint16_t burst;     // ms que duro el tiron
  uint8_t  flags;     // TP_F_*
  uint8_t  pad;
};
struct TpHistFile {
  uint32_t magic;
  uint16_t ver;
  uint16_t n;
  TpRec    rec[TP_HIST_MAX];
};

static TpRec tpHist[TP_HIST_MAX];
static int   tpHistN      = 0;
static bool  tpHistDirty  = false;
static bool  tpHistLoaded = false;

static void tpHistLoad(){
  if(tpHistLoaded) return;
  tpHistLoaded = true;
  tpHistN = 0;
  if(!flexFsReady()) return;
  TpHistFile f;
  int n = flexFsReadBin(TP_HIST_PATH, &f, sizeof(f));
  if(n < (int)(sizeof(uint32_t) + 2 * sizeof(uint16_t))) return;
  if(f.magic != TP_HIST_MAGIC || f.ver != TP_HIST_VER) return;   // otra version: se ignora
  int cnt = f.n;
  if(cnt < 0) cnt = 0;
  if(cnt > TP_HIST_MAX) cnt = TP_HIST_MAX;
  // Solo se aceptan los registros que de verdad venian en el archivo.
  int avail = (n - (int)(sizeof(uint32_t) + 2 * sizeof(uint16_t))) / (int)sizeof(TpRec);
  if(cnt > avail) cnt = avail;
  for(int i = 0; i < cnt; i++) tpHist[i] = f.rec[i];
  tpHistN = cnt;
}

static void tpHistSave(){
  if(!tpHistDirty) return;
  if(!flexFsReady()) return;             // sin particion no habia donde escribir
  TpHistFile f;
  memset(&f, 0, sizeof(f));
  f.magic = TP_HIST_MAGIC;
  f.ver   = TP_HIST_VER;
  f.n     = (uint16_t)tpHistN;
  for(int i = 0; i < tpHistN; i++) f.rec[i] = tpHist[i];
  if(flexFsWriteBinAtomic(TP_HIST_PATH, &f, sizeof(f))) tpHistDirty = false;
}

// #############################################################
// ##  EVENT MANAGER
// ##  ------------------------------------------------------
// ##  Correlaciona incidentes en vez de apilar registros sueltos.
// ##
// ##  EL CASO QUE LO JUSTIFICA (enunciado 5):
// ##
// ##     14:32:16  patron compatible con posible arrebato -> BLOQUEO
// ##     14:32:17  durante la huida el aparato cae al suelo -> IMPACTO
// ##
// ##  Son DOS deteccciones de DOS clasificadores distintos, pero UN
// ##  solo incidente. Sin correlacion, el historial diria "posible
// ##  arrebato" y, debajo, "posible caida", como si no tuvieran nada
// ##  que ver -- y el usuario tendria que deducir la secuencia.
// ##
// ##  REGLA, Y ES DE UN SOLO SENTIDO:
// ##    · un arrebato reciente + una caida cercana en el tiempo
// ##      -> el registro que YA existe se ASCIENDE a
// ##         "posible arrebato + caida", conservando su hora, su
// ##         confianza y sus motivos;
// ##    · una caida SOLA nunca crea un registro aqui: la pregunta
// ##      "¿hubo una caida?" es de Flex Device Care y alli sigue
// ##      registrandose, como siempre;
// ##    · un arrebato JAMAS se borra, se cancela ni se degrada por lo
// ##      que pase despues. El impacto se anade al evento, no lo
// ##      sustituye.
// #############################################################
#define TP_CORR_MS  8000       // ventana de correlacion arrebato <-> caida

static uint32_t tpLastEvtMs = 0;          // millis del ultimo arrebato registrado
static bool     tpHaveLast  = false;
static FlexTheftEvent tpLastEvt;

static uint8_t tpTenths(float g){
  float v = g * 10.0f;
  if(!(v == v) || v < 0.0f) return 0;
  if(v > 255.0f) v = 255.0f;
  return (uint8_t)v;
}

// Inserta al principio (lo mas reciente arriba) y descarta lo mas viejo.
static void tpHistAdd(const FlexTheftEvent* ev, uint8_t kind, uint8_t flags){
  tpHistLoad();
  for(int i = TP_HIST_MAX - 1; i > 0; i--) tpHist[i] = tpHist[i - 1];
  memset(&tpHist[0], 0, sizeof(TpRec));
  tpHist[0].utc     = clkNowUtc();
  tpHist[0].kind    = kind;
  tpHist[0].conf    = ev ? ev->confidence : 0;
  tpHist[0].reasons = ev ? ev->reasons : 0;
  tpHist[0].impact  = (ev && ev->hadImpact) ? tpTenths(ev->impactG) : 0;
  tpHist[0].pull    = ev ? tpTenths(ev->pullG) : 0;
  tpHist[0].escape  = ev ? ev->escapeMs : 0;
  tpHist[0].burst   = ev ? ev->burstMs : 0;
  tpHist[0].flags   = flags;
  if(tpHistN < TP_HIST_MAX) tpHistN++;
  tpHistDirty = true;
  tpHistSave();
}

// EL GANCHO DE CORRELACION. Lo llama dcSensorTick() cuando el detector de
// caidas confirma una caida. NO decide nada sobre la caida -- eso ya esta
// hecho, registrado y avisado por Device Care --: solo mira si ese impacto
// pertenece a un arrebato que acabamos de registrar.
static void tpNoteFall(const FlexFallEvent* e){
  if(!e || !e->fall) return;
  if(!tpHaveLast) return;
  uint32_t now = millis();
  if(now < tpLastEvtMs || (now - tpLastEvtMs) > TP_CORR_MS) return;
  tpHistLoad();
  if(tpHistN <= 0) return;
  TpRec* r = &tpHist[0];
  if(r->kind != TP_EV_SNATCH) return;         // ya estaba correlacionado: no se duplica
  r->kind   = TP_EV_SNATCH_FALL;              // ASCIENDE el registro, no crea otro
  r->reasons |= FLEXTHEFT_R_IMPACT;
  uint8_t pk = tpTenths(e->peakG);
  if(pk > r->impact) r->impact = pk;          // se conserva el impacto mas fuerte
  tpHistDirty = true;
  tpHistSave();
}

// #############################################################
// ##  PREFERENCIAS
// ##  ------------------------------------------------------
// ##  Namespace propio en NVS. El bloqueo va aqui y no en RAM porque
// ##  si no, reiniciar seria la via de escape de la propia proteccion.
// #############################################################
static void tpSavePrefs(){
  Preferences p;
  if(!p.begin("flextheft", false)) return;
  // putInt/putBool son las que usa el resto del sistema en NVS (ver dcBegin);
  // aqui se siguen usando las mismas para no introducir un tipo distinto por
  // un uint8_t.
  p.putBool("on",    tpOn);
  p.putInt ("sens",  (int)tpSens);
  p.putBool("lk",    tpLocked);
  p.putUInt("lkutc", tpLockUtc);
  p.putInt ("lkknd", (int)tpLockKind);
  p.end();
}

// Prototipos de lo que vive en el modulo SIGUIENTE de la cadena
// (FlexOS_Ultra_TheftUI.h): las pantallas y la animacion.
static void theftEnter();
static void theftTick();
static void theftRender();

// #############################################################
// ##  ENGANCHE AL SENSOR
// ##  ------------------------------------------------------
// ##  Pasa SIEMPRE por el Flex IMU Service. flexBnoBegin/Stop son
// ##  absolutos -- el ultimo que llama gana --, asi que llamarlos
// ##  desde aqui apagaria el sensor por debajo de la deteccion de
// ##  caidas o de Flex Compass. El servicio lleva la cuenta y solo
// ##  apaga de verdad cuando no queda ningun consumidor.
// #############################################################
static void tpHoldImu(bool want){
  if(want == tpHold) return;                  // idempotente: nunca descuadra la cuenta
  if(want){
    if(!gtOk) return;                         // sin bus inicializado no hay nada que sondear
    imuAcquire();
    tpHold = true;
    flexTheftReset(&tpDet);
    tpMotionSeq = motionSeqNo();
    tpImuLive = false;
  } else {
    imuRelease();
    tpHold = false;
    flexTheftReset(&tpDet);
  }
}

// RED DE SEGURIDAD DEL ENGANCHE. Entrar en la pantalla adquiere el servicio
// aunque la proteccion este apagada -- si no, no habria forma de decir si el
// modulo esta conectado. Ese enganche lo suelta theftExit(), pero la pantalla
// puede perderse sin pasar por ahi (el panel rapido se lleva la navegacion, el
// apagado, un restablecimiento). Es el MISMO problema que resolvio
// compassIdleGuard() en Flex Compass, y se resuelve igual: si el tick de la
// pantalla lleva segundos sin correr y la proteccion NO esta activada, el
// enganche sobra y se suelta. Con la proteccion activada el enganche es suyo y
// esto no lo toca jamas.
#define TP_IDLE_RELEASE_MS 3000
static uint32_t tpTickMs = 0;             // ultima vuelta de theftTick()

static void tpIdleGuard(){
  if(tpOn || !tpHold || !tpTickMs) return;
  if(millis() - tpTickMs < TP_IDLE_RELEASE_MS) return;
  tpHoldImu(false);
  tpTickMs = 0;
}

static void tpApplyPref(){
  // La proteccion corre TAMBIEN con la pantalla de Ajustes cerrada: esa es
  // la unica forma de que sirva para algo. Con la funcion apagada se suelta
  // el sensor del todo y el bus vuelve a ser exclusivamente del tactil.
  if(tpOn){ tpHoldImu(true); return; }
  // EXCEPCION: con SU PROPIA pantalla delante, el enganche se conserva aunque
  // la funcion se apague. Sin esto, apagar el interruptor soltaba el sensor,
  // el servicio pasaba a IDLE y la pantalla saltaba a "requiere modulo IMU"
  // con el modulo perfectamente conectado -- diciendo que no hay sensor
  // justo despues de haberlo estado usando. Quien suelta ese enganche es
  // theftExit() al salir, o tpIdleGuard() si la pantalla se pierde por otra
  // via.
  if(gState == ST_THEFT) return;
  tpHoldImu(false);
}

// #############################################################
// ##  ARRANQUE  (lo llama setup())
// ##  ------------------------------------------------------
// ##  Solo lee NVS. NO toca el bus I2C ni enciende el sensor: eso
// ##  ocurre en tpApplyPref(), y solo si el usuario dejo la funcion
// ##  activada.
// #############################################################
static void tpBegin(){
  flexTheftInit(&tpDet, FLEXTHEFT_SENS_NORMAL);
  Preferences p;
  if(p.begin("flextheft", true)){
    tpOn       = p.getBool("on", false);
    int sv     = p.getInt("sens", (int)FLEXTHEFT_SENS_NORMAL);
    tpSens     = (uint8_t)((sv < 0 || sv > FLEXTHEFT_SENS_HIGH) ? FLEXTHEFT_SENS_NORMAL : sv);
    tpLocked   = p.getBool("lk", false);
    tpLockUtc  = p.getUInt("lkutc", 0);
    int kv     = p.getInt("lkknd", (int)TP_EV_SNATCH);
    tpLockKind = (uint8_t)((kv == TP_EV_SNATCH_FALL) ? TP_EV_SNATCH_FALL : TP_EV_SNATCH);
    p.end();
  }
  if(tpSens > FLEXTHEFT_SENS_HIGH) tpSens = FLEXTHEFT_SENS_NORMAL;
  flexTheftSetSensitivity(&tpDet, tpSens);
  // EL BLOQUEO SOBREVIVE AL REINICIO. Si el aparato se apago (o lo apagaron)
  // con la proteccion disparada, al encender se vuelve a caer el bloqueo. Se
  // deja PENDIENTE en vez de aplicarlo aqui: setup() todavia no ha dibujado
  // nada y el sistema pasa por el splash y quiza el OOBE.
  if(tpLocked) tpLockPending = true;
}

// #############################################################
// ##  EL BLOQUEO
// ##  ------------------------------------------------------
// ##  NO se inventa una pantalla de bloqueo nueva: se usa LA del
// ##  sistema (autoLockNow, el mismo camino que el bloqueo por
// ##  inactividad), y encima se dibuja un aviso propio. Asi el
// ##  desbloqueo sigue siendo exactamente el de Flex OS -- PIN,
// ##  contrasena o gesto --, con su contador de intentos fallidos y
// ##  sus esperas, sin una segunda puerta que auditar.
// ##
// ##  UNA VEZ ACTIVADO NO SE CANCELA SOLO. Ni porque el aparato deje
// ##  de moverse, ni porque vuelva a moverse, ni porque alguien lo
// ##  recoja, ni porque despues se caiga, ni reiniciando. La unica
// ##  salida es el desbloqueo explicito (ver tpLockCleared).
// #############################################################
static bool tpCanLockNow(){
  if(gFrPending || gState == ST_FACTORY || gSafeMode) return false;
  if(flexOtaOwnsScreen() || optActive()) return false;
  if(gState == ST_SPLASH || gState == ST_OOBE_LANG || gState == ST_OOBE_NAME ||
     gState == ST_OOBE_ACCOUNT) return false;
  if(POWEROFF_ON && (gState == ST_POWEROFF_CONFIRM || gState == ST_POWEROFF_ANIM)) return false;
  if(SUSPEND_ON && gSuspOn) return false;     // pantalla apagada: no se pinta a oscuras
  if(appTrOwnsScreen()) return false;         // hay una transicion de app dibujando
  if(qsPanelY != 0 || qsAnimOn) return false; // la cortina es duena de la pantalla
  return true;
}

// Aplica el bloqueo de verdad. Devuelve false si ahora mismo no se puede
// (entonces sigue pendiente y se reintenta en la vuelta siguiente).
static bool tpApplyLock(){
  if(gState == ST_LOCK){
    // Ya estabamos detras del bloqueo: solo hay que repintarlo para que
    // salga el aviso. Nada de volver a bloquear lo que ya esta bloqueado.
    renderLock();
    if(lockOff == 0) showLock();
    return true;
  }
  // En la pantalla de verificacion de clave el usuario YA esta detras del
  // bloqueo. Bloquear otra vez le sacaria de donde esta sin ganar nada.
  if(gState == ST_LOCKSETUP && gLockVerifyLocked) return true;
  if(!tpCanLockNow()) return false;
  autoLockNow();                              // EL bloqueo del sistema, no otro
  return true;
}

static void tpArmLock(uint8_t kind){
  tpLocked   = true;
  tpLockUtc  = clkNowUtc();
  tpLockKind = kind;
  tpSavePrefs();                              // persiste ANTES de dibujar nada
  tpLockPending = true;
  tpLockPendMs  = millis();
  if(tpApplyLock()) tpLockPending = false;
}

// Reintento del bloqueo aplazado. Se llama desde loop(). Es la misma
// politica que el aviso de caida en espera: un bloqueo que no cupo porque
// habia una descarga a pantalla completa no se pierde, sale al despejarse.
// Y NO caduca: a diferencia de un aviso, esto es una medida de seguridad.
static void tpLockPendingTick(){
  if(!tpLockPending) return;
  if(!tpLocked){ tpLockPending = false; return; }
  if(tpApplyLock()) tpLockPending = false;
}

// EL DESBLOQUEO EXPLICITO. Lo llaman los DOS unicos caminos por los que el
// sistema sale de su pantalla de bloqueo:
//   · lockOnSuccess()  -> PIN o contrasena correctos
//   · lockTick()       -> gesto de deslizar, cuando no hay clave configurada
// No hay un tercero, y por eso no hace falta vigilar nada mas.
static void tpLockCleared(){
  if(!tpLocked && !tpLockPending) return;
  tpLocked      = false;
  tpLockPending = false;
  tpSavePrefs();
  flexTheftReset(&tpDet);       // la ventana de antes del bloqueo ya no describe nada
  tpMotionSeq = motionSeqNo();
}

// ¿Hay que dibujar el aviso sobre la pantalla de bloqueo? Lo consulta
// renderLock(), que esta MUCHO antes en la cadena (modulo Home) y por eso
// lo llama por prototipo, igual que hace con el resto del sistema.
static bool tpLockBannerOn(){ return tpLocked; }

// #############################################################
// ##  ESTADO PUBLICO DE LA FUNCION
// #############################################################
static uint8_t tpStatus(){
  if(!tpOn)                    return TP_ST_OFF;
  if(tpLocked && tpLockPending) return TP_ST_DETECTED;
  if(tpLocked)                 return TP_ST_LOCKED;
  if(!tpHold)                  return TP_ST_PAUSED;
  if(!flexBnoAvailable())      return TP_ST_PAUSED;
  return TP_ST_ACTIVE;
}

// #############################################################
// ##  TICK DE SENSOR  ·  desde loop(), NO desde el dibujo
// ##  ------------------------------------------------------
// ##  Va en el MISMO contexto que flexPollTouch(), imuServiceTick() y
// ##  dcSensorTick(), por la razon de siempre: el BNO085 cuelga del bus
// ##  del GT911 y dos hilos hablando por ahi sin proteccion es lo que
// ##  corrompe el bus y cuelga el panel. Aqui ademas ni siquiera se
// ##  toca el bus: el motor ya publico la muestra.
// ##
// ##  NO BLOQUEA: alimentar el clasificador es aritmetica sobre una
// ##  muestra, y solo corre cuando hay muestra nueva.
// ##
// ##  FALLO SEGURO: si el sensor se pierde, el clasificador se
// ##  reinicia, la proteccion queda PAUSADA, la interfaz lo dice y el
// ##  sistema SIGUE FUNCIONANDO. Nadie se queda esperando y no se
// ##  genera ni un evento falso.
// #############################################################
static void tpSensorTick(){
  if(!tpOn || !tpHold) return;

  bool live = flexBnoAvailable();
  if(live != tpImuLive){
    // TRANSICION REAL DEL SENSOR. Es el unico sitio donde se avisa, asi que
    // no puede haber un mensaje por vuelta: solo cuando cambia de verdad.
    tpImuLive = live;
    flexTheftReset(&tpDet);
    tpMotionSeq = motionSeqNo();
    if(live){
      sysNotify(tpt(TPS_RECONN), tpt(TPS_RECONNSUB));
    } else {
      tpPausedMs = millis();
      // El `sub` de una notificacion son 40 bytes: la frase larga se
      // truncaria a media palabra. Ahi va el estado, que es lo que importa,
      // y la explicacion completa esta en la pantalla de la funcion.
      sysNotify(tpt(TPS_DISCONN), tpt(TPS_DISCONNSUB));
    }
  }
  if(!live) return;

  // BLOQUEADO: no se sigue clasificando. El veredicto ya esta tomado y no
  // hay nada que un movimiento posterior pueda anadir -- ni quitar.
  if(tpLocked) return;

  const FlexMotionSample* m = motionTake(&tpMotionSeq);
  if(!m || !m->haveA) return;

  FlexTheftSample s;
  memset(&s, 0, sizeof(s));
  s.tMs = m->tMs;
  s.ax = m->a[0]; s.ay = m->a[1]; s.az = m->a[2];
  if(m->haveG){ s.gx = m->g[0]; s.gy = m->g[1]; s.gz = m->g[2]; s.haveGyro = 1; }
  if(m->haveQ){ s.qi = m->q[0]; s.qj = m->q[1]; s.qk = m->q[2]; s.qr = m->q[3]; s.haveQuat = 1; }

  FlexTheftEvent ev;
  if(!flexTheftFeed(&tpDet, &s, &ev)) return;
  tpLastEvt   = ev;
  tpHaveLast  = true;
  if(!ev.snatch) return;                      // evaluado y descartado: no molesta a nadie
  tpLastEvtMs = millis();

  // El propio clasificador ya vio el impacto dentro de su ventana: entonces
  // el incidente nace correlacionado y el gancho de Device Care no tendra
  // nada que ascender.
  uint8_t kind = ev.hadImpact ? TP_EV_SNATCH_FALL : TP_EV_SNATCH;
  tpHistAdd(&ev, kind, TP_F_LOCKED);
  // UNA linea por incidente, y solo por los que superan el umbral. No hay ni
  // un mensaje por ciclo, ni por muestra, ni por evaluacion descartada: el
  // puerto serie no se llena con "IMU OK" mientras la funcion monitoriza.
  Serial.printf("[ROBO] posible arrebato%s: confianza %u/100, tiron %d.%d g, "
                "separacion %u ms -> bloqueo\n",
                ev.hadImpact ? " + caida" : "", (unsigned)ev.confidence,
                (int)(ev.pullG), (int)((ev.pullG - (int)ev.pullG) * 10.0f),
                (unsigned)ev.escapeMs);
  tpArmLock(kind);
}

// #############################################################
// ##  CICLO DE VIDA DE LA PANTALLA
// #############################################################
static void tpSetEnabled(bool on){
  if(tpOn == on) return;
  tpOn = on;
  tpSavePrefs();
  tpApplyPref();
}

static void tpSetSens(uint8_t s){
  if(s > FLEXTHEFT_SENS_HIGH) s = FLEXTHEFT_SENS_NORMAL;
  if(tpSens == s) return;
  tpSens = s;
  flexTheftSetSensitivity(&tpDet, tpSens);
  tpSavePrefs();
}

// Guardado al suspender / apagar: el historial ya se escribe en cada evento,
// asi que esto solo cubre el caso de que la particion no estuviera lista
// entonces.
static void tpFlush(){ tpHistSave(); }

// Subtitulo de la fila de Ajustes -> Seguridad y privacidad. Dice el estado
// REAL, nunca uno plausible: con la funcion apagada y sin haber adquirido el
// servicio no se puede saber si hay modulo, asi que no se afirma que no lo
// haya -- se dice simplemente que esta desactivada.
static const char* theftRowValue(){
  switch(tpStatus()){
    case TP_ST_ACTIVE:   return tpt(TPS_MONITORING);
    case TP_ST_PAUSED:   return tpt(TPS_PAUSED);
    case TP_ST_DETECTED:
    case TP_ST_LOCKED:   return tpt(TPS_LOCKEDST);
    default:             return tpt(TPS_OFF);
  }
}
