// #############################################################
// ##  FLEX OS ULTRA  ·  FLEX DEVICE CARE  ·  nucleo de la app
// ##  ----------------------------------------------------------
// ##  La app Flex Device Care: estado del dispositivo, salud de Flex
// ##  OS, optimizacion, historial persistente y la pantalla de
// ##  Deteccion de caidas -- con el grafico del modulo GY-BNO085
// ##  dibujado por CODIGO, sin una sola imagen en flash.
// ##
// ##  Las pruebas (pantalla, tactil, IMU y sistema), el Diagnostico
// ##  completo y el Post-Impact Check viven en el modulo siguiente
// ##  (FlexOS_Ultra_DeviceTests.h), y la notificacion global de caida
// ##  en el de despues (FlexOS_Ultra_FallAlert.h). Aqui se declaran
// ##  por prototipo, como hace el resto del sketch.
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
// ##
// ##  QUE MIDE Y QUE NO
// ##  ----------------------------------------------------------
// ##  Ni una cifra de esta app esta inventada. Cada fila sale de una
// ##  medida real (heap_caps_*, LittleFS, esp_reset_reason,
// ##  esp_task_wdt_status, el ritmo del bucle, el propio BNO085), y lo
// ##  que no se puede medir dice "No disponible" en vez de ensenar un
// ##  numero plausible. Es la misma regla que Almacenamiento ->
// ##  Detalles de memoria y sistema, y por el mismo motivo.
// #############################################################
#pragma once
#include "FlexOS_Ultra_IMU.h"       // eslabon anterior de la cadena (y el servicio del IMU)
#include <stdarg.h>                 // vsnprintf de dcMetricSet
#include "FlexOS_BNO085.h"          // driver del IMU (unidad de traduccion aparte)
#include "FlexOS_FallDetect.h"      // logica de caidas (probada en el PC)

// #############################################################
// ##  TEXTOS  ·  mismo mecanismo de idioma que el resto del sistema
// ##  ------------------------------------------------------
// ##  Tabla propia con el MISMO indice de idioma (LI()) que usa t().
// ##  Vive aqui, junto a la funcion, en vez de engordar el enum
// ##  compartido de FlexOS_Ultra_Session.h: son cadenas de una sola
// ##  app y asi se anaden o se corrigen sin tocar el nucleo. El
// ##  contrato es identico -- columnas ES, EN, FR, PT, IT, y el chino
// ##  cae en EN, igual que en todas las demas tablas.
// #############################################################
enum {
  DCS_APPTITLE, DCS_STATUS, DCS_DIAG, DCS_FALL, DCS_HEALTH, DCS_OPT, DCS_HIST,
  DCS_ALLGOOD, DCS_ATTENTION, DCS_CRITICAL, DCS_SCORE, DCS_LASTCHECK, DCS_NEVER,
  DCS_RUN, DCS_REPEAT, DCS_DONE, DCS_UNDERSTOOD, DCS_ENABLE, DCS_DISABLE,
  DCS_AVAILABLE, DCS_UNAVAILABLE, DCS_NEEDHW, DCS_NEEDHW_1, DCS_NEEDHW_2,
  DCS_NEEDHW_3, DCS_MODULE, DCS_IMUNAME, DCS_I2CLINK, DCS_ACCEL, DCS_GYRO,
  DCS_MAG, DCS_FUSION, DCS_MONITORING, DCS_MONITOR_OFF, DCS_TESTMODULE,
  DCS_CONFIDENCE, DCS_NOEVENTS, DCS_EMPTYHIST, DCS_MEMORY, DCS_PSRAM,
  DCS_STABILITY, DCS_WATCHDOG, DCS_SERVICES, DCS_ERRORS, DCS_REBOOTS,
  DCS_STORAGE, DCS_NOTAVAIL, DCS_SAFEACTIONS, DCS_OPTNOTE, DCS_OPTRUN,
  DCS_EVDIAG, DCS_EVFALL, DCS_EVPOST, DCS_EVOPT, DCS_IMPACT, DCS_AFTERCHECK,
  DCS_NORMAL, DCS_EXPERIMENTAL, DCS_UPTIME, DCS_LOOPRATE, DCS_DETECTING,
  DCS_YES, DCS_NO, DCS_SCRASK1, DCS_SCRASK2, DCS_TTHINT,
  DCS_SYSTEM, DCS_SCREEN, DCS_TOUCH,
  DCS_FALLTITLE, DCS_FALLBODY, DCS_REVIEW, DCS_DISMISS,
  DCS_OPT1, DCS_OPT2, DCS_OPT3, DCS_OPT4, DCS_OPTNOFW,
  DCS_N
};
static const char* DCH[DCS_N][5] = {
  {"Device Care","Device Care","Device Care","Device Care","Device Care"},
  {"Estado del dispositivo","Device status","\xC3\x89tat de l'appareil","Estado do dispositivo","Stato del dispositivo"},
  {"Diagn\xC3\xB3stico completo","Full diagnostics","Diagnostic complet","Diagn\xC3\xB3stico completo","Diagnostica completa"},
  {"Detecci\xC3\xB3n de ca\xC3\xAD" "das","Fall detection","D\xC3\xA9tection de chute","Detec\xC3\xA7\xC3\xA3o de queda","Rilevamento cadute"},
  {"Salud de Flex OS","Flex OS health","Sant\xC3\xA9 de Flex OS","Sa\xC3\xBA" "de do Flex OS","Salute di Flex OS"},
  {"Optimizaci\xC3\xB3n","Optimization","Optimisation","Otimiza\xC3\xA7\xC3\xA3o","Ottimizzazione"},
  {"Historial","History","Historique","Hist\xC3\xB3rico","Cronologia"},
  {"Todo correcto","All good","Tout va bien","Tudo certo","Tutto a posto"},
  {"Necesita atenci\xC3\xB3n","Needs attention","\xC3\x80 surveiller","Precisa aten\xC3\xA7\xC3\xA3o","Richiede attenzione"},
  {"Estado cr\xC3\xADtico","Critical","\xC3\x89tat critique","Estado cr\xC3\xADtico","Stato critico"},
  {"Puntuaci\xC3\xB3n","Score","Score","Pontua\xC3\xA7\xC3\xA3o","Punteggio"},
  {"\xC3\x9Altima revisi\xC3\xB3n","Last check","Dernier contr\xC3\xB4le","\xC3\x9Altima revis\xC3\xA3o","Ultimo controllo"},
  {"Nunca","Never","Jamais","Nunca","Mai"},
  {"Ejecutar","Run","Lancer","Executar","Avvia"},
  {"Repetir","Repeat","Relancer","Repetir","Ripeti"},
  {"Hecho","Done","Termin\xC3\xA9","Conclu\xC3\xAD" "do","Fatto"},
  {"Entendido","Got it","Compris","Entendi","Ho capito"},
  {"Activar","Enable","Activer","Ativar","Attiva"},
  {"Desactivar","Disable","D\xC3\xA9sactiver","Desativar","Disattiva"},
  {"Disponible","Available","Disponible","Dispon\xC3\xADvel","Disponibile"},
  {"No disponible","Unavailable","Indisponible","Indispon\xC3\xADvel","Non disponibile"},
  {"Esta funci\xC3\xB3n requiere un m\xC3\xB3" "dulo IMU compatible.",
   "This feature needs a compatible IMU module.",
   "Cette fonction n\xC3\xA9" "cessite un module IMU compatible.",
   "Este recurso exige um m\xC3\xB3" "dulo IMU compat\xC3\xADvel.",
   "Questa funzione richiede un modulo IMU compatibile."},
  {"M\xC3\xB3" "dulo compatible actualmente:","Currently supported module:","Module compatible actuel :","M\xC3\xB3" "dulo compat\xC3\xADvel atual:","Modulo compatibile attuale:"},
  {"Con\xC3\xA9" "ctalo por I\xC2\xB2""C para usar esta funci\xC3\xB3n.",
   "Connect it over I\xC2\xB2""C to use this feature.",
   "Branche-le en I\xC2\xB2""C pour utiliser cette fonction.",
   "Conecte-o por I\xC2\xB2""C para usar este recurso.",
   "Collegalo via I\xC2\xB2""C per usare questa funzione."},
  {"3V3 \xC2\xB7 GND \xC2\xB7 SCL \xC2\xB7 SDA","3V3 \xC2\xB7 GND \xC2\xB7 SCL \xC2\xB7 SDA","3V3 \xC2\xB7 GND \xC2\xB7 SCL \xC2\xB7 SDA","3V3 \xC2\xB7 GND \xC2\xB7 SCL \xC2\xB7 SDA","3V3 \xC2\xB7 GND \xC2\xB7 SCL \xC2\xB7 SDA"},
  {"M\xC3\xB3" "dulo","Module","Module","M\xC3\xB3" "dulo","Modulo"},
  {"9-DOF IMU","9-DOF IMU","IMU 9 axes","IMU 9-DOF","IMU 9 assi"},
  {"Comunicaci\xC3\xB3n I\xC2\xB2""C","I\xC2\xB2""C link","Liaison I\xC2\xB2""C","Comunica\xC3\xA7\xC3\xA3o I\xC2\xB2""C","Comunicazione I\xC2\xB2""C"},
  {"Aceler\xC3\xB3metro","Accelerometer","Acc\xC3\xA9l\xC3\xA9rom\xC3\xA8tre","Aceler\xC3\xB4metro","Accelerometro"},
  {"Giroscopio","Gyroscope","Gyroscope","Girosc\xC3\xB3pio","Giroscopio"},
  {"Magnet\xC3\xB3metro","Magnetometer","Magn\xC3\xA9tom\xC3\xA8tre","Magnet\xC3\xB4metro","Magnetometro"},
  {"Sensor Fusion","Sensor fusion","Fusion de capteurs","Fus\xC3\xA3o de sensores","Fusione sensori"},
  {"Monitorizando","Monitoring","Surveillance","Monitorando","Monitoraggio"},
  {"Sin monitorizar","Not monitoring","Sans surveillance","Sem monitoramento","Nessun monitoraggio"},
  {"Probar m\xC3\xB3" "dulo","Test module","Tester le module","Testar m\xC3\xB3" "dulo","Prova modulo"},
  {"Confianza","Confidence","Confiance","Confian\xC3\xA7" "a","Attendibilit\xC3\xA0"},
  {"Sin eventos","No events","Aucun \xC3\xA9v\xC3\xA9nement","Sem eventos","Nessun evento"},
  {"Todav\xC3\xAD" "a no hay nada registrado.","Nothing recorded yet.","Rien d'enregistr\xC3\xA9 pour l'instant.","Ainda n\xC3\xA3o h\xC3\xA1 registros.","Ancora nessuna registrazione."},
  {"Memoria","Memory","M\xC3\xA9moire","Mem\xC3\xB3ria","Memoria"},
  {"PSRAM","PSRAM","PSRAM","PSRAM","PSRAM"},
  {"Estabilidad","Stability","Stabilit\xC3\xA9","Estabilidade","Stabilit\xC3\xA0"},
  {"Watchdog","Watchdog","Watchdog","Watchdog","Watchdog"},
  {"Servicios","Services","Services","Servi\xC3\xA7os","Servizi"},
  {"Errores","Errors","Erreurs","Erros","Errori"},
  {"Reinicios","Reboots","Red\xC3\xA9marrages","Reinicializa\xC3\xA7\xC3\xB5" "es","Riavvii"},
  {"Almacenamiento","Storage","Stockage","Armazenamento","Archivi"},
  {"No disponible","Not available","Non disponible","N\xC3\xA3o dispon\xC3\xADvel","Non disponibile"},
  {"Solo acciones seguras","Safe actions only","Actions s\xC3\xBBres uniquement","Apenas a\xC3\xA7\xC3\xB5" "es seguras","Solo azioni sicure"},
  {"No se borran notas, dibujos ni archivos.","Notes, drawings and files are never deleted.","Ni notes, ni dessins, ni fichiers ne sont effac\xC3\xA9s.","Notas, desenhos e arquivos n\xC3\xA3o se apagam.","Note, disegni e file non si cancellano."},
  {"Optimizar Flex OS","Optimize Flex OS","Optimiser Flex OS","Otimizar Flex OS","Ottimizza Flex OS"},
  {"Diagn\xC3\xB3stico","Diagnostics","Diagnostic","Diagn\xC3\xB3stico","Diagnostica"},
  {"Posible ca\xC3\xAD" "da","Possible fall","Chute possible","Poss\xC3\xADvel queda","Possibile caduta"},
  {"Revisi\xC3\xB3n tras impacto","Post-impact check","Contr\xC3\xB4le apr\xC3\xA8s impact","Revis\xC3\xA3o ap\xC3\xB3s impacto","Controllo post-impatto"},
  {"Optimizaci\xC3\xB3n","Optimization","Optimisation","Otimiza\xC3\xA7\xC3\xA3o","Ottimizzazione"},
  {"Impacto","Impact","Impact","Impacto","Impatto"},
  {"Diagn\xC3\xB3stico posterior","Follow-up check","Contr\xC3\xB4le ult\xC3\xA9rieur","Diagn\xC3\xB3stico posterior","Controllo successivo"},
  {"Normal","Normal","Normal","Normal","Normale"},
  {"Detecci\xC3\xB3n experimental de eventos f\xC3\xADsicos.","Experimental physical-event detection.","D\xC3\xA9tection exp\xC3\xA9rimentale d'\xC3\xA9v\xC3\xA9nements physiques.","Detec\xC3\xA7\xC3\xA3o experimental de eventos f\xC3\xADsicos.","Rilevamento sperimentale di eventi fisici."},
  {"Tiempo encendido","Uptime","Temps allum\xC3\xA9","Tempo ligado","Tempo acceso"},
  {"Ritmo del sistema","System rate","Rythme du syst\xC3\xA8me","Ritmo do sistema","Ritmo del sistema"},
  {"Detectando m\xC3\xB3" "dulo\xE2\x80\xA6","Detecting module\xE2\x80\xA6","D\xC3\xA9tection du module\xE2\x80\xA6","Detectando m\xC3\xB3" "dulo\xE2\x80\xA6","Rilevamento modulo\xE2\x80\xA6"},
  {"S\xC3\xAD","Yes","Oui","Sim","S\xC3\xAC"},
  {"No","No","Non","N\xC3\xA3o","No"},
  {"\xC2\xBFLa pantalla reproduce correctamente",
   "Does the screen show colors and",
   "L'\xC3\xA9" "cran affiche-t-il correctement",
   "A tela reproduz corretamente",
   "Lo schermo riproduce correttamente"},
  {"los colores y patrones?","patterns correctly?","les couleurs et les motifs ?","as cores e os padr\xC3\xB5" "es?","i colori e i motivi?"},
  {"Recorre toda la rejilla con el dedo","Sweep the whole grid with your finger","Parcours toute la grille au doigt","Percorra toda a grade com o dedo","Percorri tutta la griglia con il dito"},
  {"Sistema","System","Syst\xC3\xA8me","Sistema","Sistema"},
  {"Pantalla","Screen","\xC3\x89" "cran","Tela","Schermo"},
  {"T\xC3\xA1" "ctil","Touch","Tactile","Toque","Tocco"},
  {"POSIBLE CA\xC3\x8D" "DA DETECTADA","POSSIBLE FALL DETECTED","CHUTE POSSIBLE D\xC3\x89TECT\xC3\x89" "E","POSS\xC3\x8DVEL QUEDA DETECTADA","POSSIBILE CADUTA RILEVATA"},
  {"Se detect\xC3\xB3 un impacto que podr\xC3\xAD" "a corresponder a una ca\xC3\xAD" "da.",
   "An impact was detected that could correspond to a fall.",
   "Un impact pouvant correspondre \xC3\xA0 une chute a \xC3\xA9t\xC3\xA9 d\xC3\xA9tect\xC3\xA9.",
   "Foi detectado um impacto que pode corresponder a uma queda.",
   "\xC3\x88 stato rilevato un impatto che potrebbe essere una caduta."},
  {"Revisar dispositivo","Check device","V\xC3\xA9rifier l'appareil","Revisar dispositivo","Controlla il dispositivo"},
  {"Descartar","Dismiss","Ignorer","Descartar","Ignora"},
  {"Analizar la memoria y medirla de nuevo","Analyze memory and measure it again","Analyser la m\xC3\xA9moire et la remesurer","Analisar a mem\xC3\xB3ria e medi-la de novo","Analizzare la memoria e rimisurarla"},
  {"Vaciar la cach\xC3\xA9 temporal del sistema","Clear the system temporary cache","Vider le cache temporaire du syst\xC3\xA8me","Esvaziar o cache tempor\xC3\xA1rio do sistema","Svuotare la cache temporanea di sistema"},
  {"Soltar recursos reconstruibles de apps en pausa","Release rebuildable resources of paused apps","Lib\xC3\xA9rer les ressources reconstructibles des apps en pause","Liberar recursos reconstru\xC3\xADveis de apps em pausa","Rilasciare risorse ricostruibili delle app in pausa"},
  {"Verificar la estabilidad tras liberar","Verify stability after releasing","V\xC3\xA9rifier la stabilit\xC3\xA9 apr\xC3\xA8s lib\xC3\xA9ration","Verificar a estabilidade ap\xC3\xB3s liberar","Verificare la stabilit\xC3\xA0 dopo il rilascio"},
  {"No se toca la actualizaci\xC3\xB3n del sistema ni el firmware.",
   "System updates and firmware are never touched.",
   "Ni la mise \xC3\xA0 jour du syst\xC3\xA8me ni le micrologiciel ne sont touch\xC3\xA9s.",
   "A atualiza\xC3\xA7\xC3\xA3o do sistema e o firmware n\xC3\xA3o se tocam.",
   "L'aggiornamento di sistema e il firmware non si toccano."},
};
static const char* dct(int id){ return DCH[id][LI()]; }

// #############################################################
// ##  PANTALLAS DE LA APP
// ##  ------------------------------------------------------
// ##  Una sola variable manda: dcScreen. El boton "atras" del sistema
// ##  retrocede UNA pantalla por el gancho backScreen (igual que en
// ##  Almacenamiento, Notas o Clima), y solo desde el inicio de la app
// ##  se sale al escritorio.
// #############################################################
enum {
  DC_HOME = 0,
  DC_STATUS,     // Estado del dispositivo
  DC_FALL,       // Deteccion de caidas (presente / ausente)
  DC_IMUTEST,    // Prueba del GY-BNO085 con lecturas en vivo
  DC_HEALTH,     // Salud de Flex OS
  DC_OPTIM,      // Optimizacion
  DC_HIST,       // Historial
  DC_DIAG,       // Diagnostico completo (motor de pruebas)
  DC_POST,       // Post-Impact Check (mismo motor, arranque automatico)
  DC_SCRTEST,    // Test de pantalla
  DC_TOUCHTEST,  // Test tactil
  DC_RESULT      // Resultado del diagnostico
};

static int  dcScreen  = DC_HOME;
static int  dcPrev    = DC_HOME;      // a donde vuelve el resultado del diagnostico
static int  dcScroll  = 0;
static bool dcDrag = false, dcTouching = false;
static float dcDragY0 = 0, dcDragS0 = 0;

// Animacion: instante en que entro la pantalla actual. TODA la
// animacion es funcion del tiempo (no un paso por cuadro), asi que
// perder un cuadro no cambia la velocidad de nada.
static uint32_t dcAnimT0 = 0;
static uint32_t dcAnimMs = 0;         // ultimo cuadro publicado

// Zonas pulsables de la pantalla actual. Se rellenan al dibujar y se
// leen al tocar: geometria UNICA, sin numeros repetidos entre el
// dibujo y el hit-test.
#define DC_HIT_MAX 12
struct DcHit { int16_t x0, y0, x1, y1; uint8_t id; };
static DcHit dcHits[DC_HIT_MAX];
static int   dcHitN = 0;
static void  dcHitClear(){ dcHitN = 0; }
static void  dcHitAdd(int x, int y, int w, int h, uint8_t id){
  if(dcHitN >= DC_HIT_MAX) return;
  dcHits[dcHitN].x0 = (int16_t)x;      dcHits[dcHitN].y0 = (int16_t)y;
  dcHits[dcHitN].x1 = (int16_t)(x + w); dcHits[dcHitN].y1 = (int16_t)(y + h);
  dcHits[dcHitN].id = id;
  dcHitN++;
}
static int dcHitTest(int px, int py){
  for(int i = 0; i < dcHitN; i++)
    if(px >= dcHits[i].x0 && px < dcHits[i].x1 && py >= dcHits[i].y0 && py < dcHits[i].y1)
      return dcHits[i].id;
  return -1;
}

// #############################################################
// ##  HISTORIAL PERSISTENTE
// ##  ------------------------------------------------------
// ##  Un solo archivo pequeno en la particion de datos, escrito con la
// ##  misma escritura recuperable que usa el resto del sistema
// ##  (flexFsWriteBinAtomic: temporal, verificacion y sustitucion). No
// ##  hay ninguna base de datos: son 16 registros de 12 bytes, es
// ##  decir menos de 200 bytes en total.
// #############################################################
#define DC_HIST_PATH   FLEXFS_DIR_SYS "/devcare.bin"
#define DC_HIST_MAGIC  0x44434152u        // 'DCAR'
#define DC_HIST_VER    1
#define DC_HIST_MAX    16

enum { DC_EV_DIAG = 0, DC_EV_FALL, DC_EV_POST, DC_EV_OPT };

struct DcRec {
  uint32_t utc;      // instante (epoca UTC del reloj del sistema)
  uint8_t  kind;     // DC_EV_*
  uint8_t  score;    // 0..100 (diagnostico) o confianza (caida)
  uint8_t  flags;    // motivos de la caida / resultado del diagnostico
  uint8_t  extra;    // pico del impacto en decimas de g, acotado a 255
};
struct DcHistFile {
  uint32_t magic;
  uint16_t ver;
  uint16_t n;
  DcRec    rec[DC_HIST_MAX];
};

static DcRec  dcHist[DC_HIST_MAX];
static int    dcHistN     = 0;
static bool   dcHistDirty = false;
static bool   dcHistLoaded = false;

// Resultado guardado del ultimo diagnostico (para la tarjeta de inicio).
static uint8_t  dcLastScore = 0;
static uint32_t dcLastCheck = 0;      // epoca UTC; 0 = nunca

static void dcHistLoad(){
  if(dcHistLoaded) return;
  dcHistLoaded = true;
  dcHistN = 0;
  if(!flexFsReady()) return;
  DcHistFile f;
  int n = flexFsReadBin(DC_HIST_PATH, &f, sizeof(f));
  if(n < (int)(sizeof(uint32_t) + 2 * sizeof(uint16_t))) return;
  if(f.magic != DC_HIST_MAGIC || f.ver != DC_HIST_VER) return;   // otra version: se ignora, no se interpreta
  int cnt = f.n;
  if(cnt < 0) cnt = 0;
  if(cnt > DC_HIST_MAX) cnt = DC_HIST_MAX;
  // Solo se aceptan los registros que de verdad venian en el archivo.
  int avail = (n - (int)(sizeof(uint32_t) + 2 * sizeof(uint16_t))) / (int)sizeof(DcRec);
  if(cnt > avail) cnt = avail;
  for(int i = 0; i < cnt; i++) dcHist[i] = f.rec[i];
  dcHistN = cnt;
  for(int i = 0; i < dcHistN; i++){
    if(dcHist[i].kind == DC_EV_DIAG || dcHist[i].kind == DC_EV_POST){
      dcLastScore = dcHist[i].score;
      dcLastCheck = dcHist[i].utc;
      break;                                  // el mas reciente esta el primero
    }
  }
}

static void dcHistSave(){
  if(!dcHistDirty) return;
  if(!flexFsReady()) return;                  // sin particion no se pierde nada: no habia donde escribir
  DcHistFile f;
  memset(&f, 0, sizeof(f));
  f.magic = DC_HIST_MAGIC;
  f.ver   = DC_HIST_VER;
  f.n     = (uint16_t)dcHistN;
  for(int i = 0; i < dcHistN; i++) f.rec[i] = dcHist[i];
  if(flexFsWriteBinAtomic(DC_HIST_PATH, &f, sizeof(f))) dcHistDirty = false;
}

// Inserta al principio (lo mas reciente arriba) y descarta lo mas viejo.
static void dcHistAdd(uint8_t kind, uint8_t score, uint8_t flags, uint8_t extra){
  dcHistLoad();
  for(int i = DC_HIST_MAX - 1; i > 0; i--) dcHist[i] = dcHist[i - 1];
  dcHist[0].utc   = clkNowUtc();
  dcHist[0].kind  = kind;
  dcHist[0].score = score;
  dcHist[0].flags = flags;
  dcHist[0].extra = extra;
  if(dcHistN < DC_HIST_MAX) dcHistN++;
  dcHistDirty = true;
  if(kind == DC_EV_DIAG || kind == DC_EV_POST){
    dcLastScore = score;
    dcLastCheck = dcHist[0].utc;
  }
  dcHistSave();
}

// #############################################################
// ##  ESTADO DEL IMU Y DE LA DETECCION DE CAIDAS
// ##  ------------------------------------------------------
// ##  El driver corre SIEMPRE que la deteccion este activada, tambien
// ##  con la app cerrada: esa es la unica forma de que una caida se
// ##  detecte cuando el usuario no esta mirando Device Care. Con la
// ##  deteccion apagada el modulo se detiene del todo y el bus I2C
// ##  vuelve a ser exclusivamente del tactil.
// #############################################################
static bool        dcFallOn     = false;       // el usuario activo la deteccion (NVS)
static bool        dcSensorOn   = false;       // el driver esta en marcha
static FlexFallDet dcDet;
static bool        dcDetReady   = false;
static uint32_t    dcSampleMs   = 0;           // ultima muestra alimentada
static uint32_t    dcBootAbnormal = 0;         // reinicios inesperados acumulados (NVS)
static uint8_t     dcBootReason = 0;           // esp_reset_reason() de ESTE arranque
static uint32_t    dcLastEvtMs  = 0;           // millis del ultimo evento evaluado
static FlexFallEvent dcLastEvt;
static bool        dcLastEvtOk  = false;
// "Revisar dispositivo" abre la app y quiere el Post-Impact Check ya en
// marcha. La app no se dibuja en el acto (enterApp arranca una
// transicion y el enter() corre en su ultimo cuadro), asi que la
// intencion viaja en esta bandera y la recoge dcEnter/dcResume: si se
// llamara a dcDiagEnter aqui mismo, la transicion pintaria encima.
static bool        dcPendingPost = false;

// Prototipos de lo que vive en los DOS modulos siguientes de la cadena.
static void dcDiagEnter(bool postImpact);
static void dcDiagTick();
static void dcDiagRender();
static void dcScrTestTick();  static void dcScrTestRender();
static void dcTouchTestTick(); static void dcTouchTestRender();
static void dcResultRender();  static void dcResultTick();
static void dcImuTestRender(); static void dcImuTestTick();
static void dcHealthRender();  static void dcHealthTick();
static void dcOptimRender();   static void dcOptimTick();
static bool dcHealthScore(uint8_t* out);       // puntuacion REAL 0..100
static void faRaise(const FlexFallEvent* e);   // notificacion global (modulo FallAlert)
static void faPendingTick();                   // ...y su reintento cuando la pantalla estaba ocupada

// Prototipos de este mismo modulo que se usan antes de definirse.
static void dcRender();
static void dcGoto(int screen);

// #############################################################
// ##  ARRANQUE DEL MODULO  (lo llama setup())
// ##  ------------------------------------------------------
// ##  Solo lee NVS y contabiliza el motivo del arranque. NO toca el
// ##  bus I2C ni enciende el sensor: eso ocurre cuando el usuario
// ##  activa la deteccion, no en cada encendido.
// #############################################################
static void dcBegin(){
  flexFallInit(&dcDet);
  dcDetReady = true;
  esp_reset_reason_t rr = esp_reset_reason();
  dcBootReason = (uint8_t)rr;
  bool abnormal = !(rr == ESP_RST_POWERON || rr == ESP_RST_SW || rr == ESP_RST_DEEPSLEEP);
  Preferences p;
  if(p.begin("flexcare", false)){
    dcBootAbnormal = (uint32_t)p.getInt("badboot", 0);
    dcFallOn       = p.getBool("fallon", false);
    if(abnormal){
      dcBootAbnormal++;
      p.putInt("badboot", (int)dcBootAbnormal);
    }
    p.end();
  }
}
static void dcSaveFallPref(){
  Preferences p;
  if(p.begin("flexcare", false)){ p.putBool("fallon", dcFallOn); p.end(); }
}

// Enciende o apaga el sensor de verdad. Idempotente.
// AHORA PASAN POR EL FLEX IMU SERVICE, no por el driver directamente.
// El motivo es concreto: flexBnoBegin()/flexBnoStop() son absolutos -- el
// ultimo que llama gana --, y desde que Flex Compass tambien lee el sensor,
// salir de la brujula apagaria el BNO085 por debajo de la deteccion de
// caidas. El servicio lleva la cuenta de consumidores y solo apaga de verdad
// cuando no queda ninguno. El comportamiento de Device Care no cambia: sigue
// encendiendo y apagando el sensor exactamente en los mismos sitios.
static void dcSensorStart(){
  if(dcSensorOn) return;
  if(!gtOk) return;                 // sin bus I2C inicializado no hay nada que sondear
  imuAcquire();
  dcSensorOn = true;
  flexFallReset(&dcDet);
  dcSampleMs = 0;
}
static void dcSensorStop(){
  if(!dcSensorOn) return;
  imuRelease();
  dcSensorOn = false;
  flexFallReset(&dcDet);
}

// #############################################################
// ##  TICK DE SENSOR  ·  se llama desde loop(), no desde el dibujo
// ##  ------------------------------------------------------
// ##  Va en el MISMO contexto que flexPollTouch() y que la deteccion
// ##  I2C incremental, por la misma razon de siempre: el BNO085 cuelga
// ##  del bus del GT911 y dos tareas hablando por ahi sin proteccion
// ##  es lo que corrompe el bus y cuelga el panel.
// ##
// ##  NO BLOQUEA: el driver lee como mucho unos pocos paquetes por
// ##  vuelta y la evaluacion de la caida es aritmetica sobre una
// ##  muestra. La interfaz no se entera de que esto existe.
// ##
// ##  FALLO SEGURO: si el sensor se pierde (cable suelto, deja de
// ##  responder, datos invalidos), el detector se reinicia, la
// ##  deteccion queda sin efecto y el sistema SIGUE FUNCIONANDO. La
// ##  interfaz lo dice; nadie se queda esperando.
// #############################################################
static void dcSensorTick(){
  if(!dcSensorOn) return;
  uint32_t now = millis();
  // El sondeo del driver ya lo hizo imuServiceTick() al principio de la vuelta
  // -- el servicio es quien mueve el sensor ahora, porque puede haber mas de un
  // consumidor. Aqui solo se consume la muestra.

  if(!flexBnoAvailable()){
    if(flexFallState(&dcDet) != FLEXFALL_IDLE) flexFallReset(&dcDet);
    return;
  }
  // Una muestra por informe nuevo, y como mucho a la cadencia del
  // sensor: alimentar el detector con la MISMA lectura repetida
  // inventaria quietud que no se ha medido.
  if(now - dcSampleMs < (1000u / FLEXBNO_REPORT_HZ)) return;
  float a[3], g[3], q[4];
  if(!flexBnoAccel(a)) return;                 // sin acelerometro no hay deteccion posible
  bool haveG = flexBnoGyro(g);
  bool haveQ = flexBnoQuat(q);
  dcSampleMs = now;

  FlexFallSample s;
  memset(&s, 0, sizeof(s));
  s.tMs = now;
  s.ax = a[0]; s.ay = a[1]; s.az = a[2];
  if(haveG){ s.gx = g[0]; s.gy = g[1]; s.gz = g[2]; }
  if(haveQ){ s.qi = q[0]; s.qj = q[1]; s.qk = q[2]; s.qr = q[3]; s.haveQuat = 1; }

  FlexFallEvent ev;
  if(!flexFallFeed(&dcDet, &s, &ev)) return;
  dcLastEvt   = ev;
  dcLastEvtOk = true;
  dcLastEvtMs = now;
  if(!ev.fall) return;                          // evaluado y descartado: no molesta a nadie
  uint8_t pk = (uint8_t)(ev.peakG * 10.0f > 255.0f ? 255.0f : ev.peakG * 10.0f);
  dcHistAdd(DC_EV_FALL, ev.confidence, ev.reasons, pk);
  faRaise(&ev);                                 // la notificacion global decide si se ve
}

// Aplica la preferencia del usuario. Se llama al activar/desactivar y
// al arrancar la app.
static void dcApplyFallPref(){
  if(dcFallOn) dcSensorStart();
  else         dcSensorStop();
}

// #############################################################
// ##  ICONOS VECTORIALES DE LA APP
// ##  ------------------------------------------------------
// ##  Cada seccion tiene SU glifo, dibujado con las mismas primitivas
// ##  que el resto del sistema (nada de bitmaps). Todos caben en su
// ##  caja S x S, igual que los iconos de app.
// #############################################################
enum { DCI_STATUS = 0, DCI_DIAG, DCI_FALL, DCI_HEALTH, DCI_OPT, DCI_HIST };

// Silueta de telefono reutilizada por varios glifos.
static void dcGlyphPhone(int cx, int cy, int h, uint16_t col, float th){
  int w = h * 52 / 100;
  drawRoundRect(cx - w / 2, cy - h / 2, w, h, w / 5, col);
  drawRoundRect(cx - w / 2 + 1, cy - h / 2 + 1, w - 2, h - 2, w / 5, col);
  strokeSegAA(cx - w / 8, cy - h / 2 + 3, cx + w / 8, cy - h / 2 + 3, th, col);
}

static void dcSectionIcon(int which, int x, int y, int S, uint16_t col, uint16_t acc){
  int cx = x + S / 2, cy = y + S / 2;
  float th = S / 16.0f; if(th < 1.4f) th = 1.4f;
  switch(which){
    case DCI_STATUS: {                       // telefono con marca de verificacion
      dcGlyphPhone(cx, cy, (int)(S * 0.74f), col, th);
      int r = S / 4;
      fillCircle(cx + S / 5, cy + S / 5, r, acc);
      strokeSegAA(cx + S / 5 - r * 0.42f, cy + S / 5,
                  cx + S / 5 - r * 0.08f, cy + S / 5 + r * 0.36f, th, TH_ONACC);
      strokeSegAA(cx + S / 5 - r * 0.08f, cy + S / 5 + r * 0.36f,
                  cx + S / 5 + r * 0.45f, cy + S / 5 - r * 0.34f, th, TH_ONACC);
    } break;
    case DCI_DIAG: {                         // pantalla con linea de pulso
      int w = (int)(S * 0.78f), h = (int)(S * 0.58f);
      drawRoundRect(cx - w / 2, cy - h / 2 - S / 12, w, h, 5, col);
      float bx = cx - w / 2 + 4, byy = cy - S / 12;
      strokeSegAA(bx, byy, bx + w * 0.22f, byy, th, acc);
      strokeSegAA(bx + w * 0.22f, byy, bx + w * 0.34f, byy - h * 0.28f, th, acc);
      strokeSegAA(bx + w * 0.34f, byy - h * 0.28f, bx + w * 0.48f, byy + h * 0.26f, th, acc);
      strokeSegAA(bx + w * 0.48f, byy + h * 0.26f, bx + w * 0.60f, byy, th, acc);
      strokeSegAA(bx + w * 0.60f, byy, bx + w - 8, byy, th, acc);
      fillRect(cx - w / 8, cy + h / 2 - S / 12, w / 4, S / 8, col);
      fillRect(cx - w / 4, cy + h / 2 + S / 24, w / 2, 2, col);
    } break;
    case DCI_FALL: {                         // telefono inclinado cayendo, con arcos
      int w = (int)(S * 0.34f), h = (int)(S * 0.56f);
      // Cuerpo inclinado: se dibuja como cuatro trazos girados 25 grados.
      float a = -0.44f, ca = cosf(a), sa = sinf(a);
      float px1[4] = { -w/2.0f,  w/2.0f,  w/2.0f, -w/2.0f };
      float py1[4] = { -h/2.0f, -h/2.0f,  h/2.0f,  h/2.0f };
      float rx[4], ry[4];
      for(int i = 0; i < 4; i++){
        rx[i] = cx + px1[i] * ca - py1[i] * sa;
        ry[i] = cy - S / 10 + px1[i] * sa + py1[i] * ca;
      }
      for(int i = 0; i < 4; i++) strokeSegAA(rx[i], ry[i], rx[(i+1)%4], ry[(i+1)%4], th, col);
      // Flecha de caida
      strokeSegAA(cx + S * 0.30f, cy - S * 0.22f, cx + S * 0.30f, cy + S * 0.24f, th, acc);
      fillTriangle((int)(cx + S * 0.30f), (int)(cy + S * 0.38f),
                   (int)(cx + S * 0.22f), (int)(cy + S * 0.20f),
                   (int)(cx + S * 0.38f), (int)(cy + S * 0.20f), acc);
      // Suelo
      strokeSegAA(cx - S * 0.36f, cy + S * 0.40f, cx + S * 0.06f, cy + S * 0.40f, th, col);
    } break;
    case DCI_HEALTH: {                       // corazon con pulso
      int r = S / 5;
      fillCircle(cx - r, cy - r / 2, r, acc);
      fillCircle(cx + r, cy - r / 2, r, acc);
      fillTriangle(cx - 2 * r, cy - r / 2, cx + 2 * r, cy - r / 2, cx, cy + 2 * r - 2, acc);
      strokeSegAA(cx - r * 1.6f, cy, cx - r * 0.6f, cy, th, TH_ONACC);
      strokeSegAA(cx - r * 0.6f, cy, cx - r * 0.2f, cy - r * 0.7f, th, TH_ONACC);
      strokeSegAA(cx - r * 0.2f, cy - r * 0.7f, cx + r * 0.2f, cy + r * 0.6f, th, TH_ONACC);
      strokeSegAA(cx + r * 0.2f, cy + r * 0.6f, cx + r * 0.6f, cy, th, TH_ONACC);
      strokeSegAA(cx + r * 0.6f, cy, cx + r * 1.6f, cy, th, TH_ONACC);
    } break;
    case DCI_OPT: {                          // aguja de cuadro de mandos
      int r = (int)(S * 0.34f);
      arcStroke(cx, cy + r / 3, r, 180, 360, (int)(th * 2), col);
      for(int k = 0; k <= 4; k++){
        float ang = (180 + k * 45) * 0.0174532925f;
        strokeSegAA(cx + cosf(ang) * (r - th * 2), cy + r / 3 + sinf(ang) * (r - th * 2),
                    cx + cosf(ang) * (r - th * 4), cy + r / 3 + sinf(ang) * (r - th * 4),
                    th * 0.7f, col);
      }
      float na = (180 + 62) * 0.0174532925f;
      strokeSegAA(cx, cy + r / 3, cx + cosf(na) * r * 0.82f, cy + r / 3 + sinf(na) * r * 0.82f, th, acc);
      fillCircle(cx, cy + r / 3, (int)(th * 1.4f), acc);
    } break;
    case DCI_HIST: {                         // reloj con flecha hacia atras
      int r = (int)(S * 0.32f);
      arcStroke(cx, cy, r, 300, 640, (int)(th * 1.8f), col);
      fillTriangle(cx - r, cy - r / 2, cx - r, cy + r / 2, cx - r + (int)(th * 3), cy, col);
      strokeSegAA(cx, cy, cx, cy - r * 0.52f, th, acc);
      strokeSegAA(cx, cy, cx + r * 0.40f, cy, th, acc);
      fillCircle(cx, cy, (int)(th * 1.1f), acc);
    } break;
    default: break;
  }
}

// Glifos pequenos que acompanan a los botones: reproducir, reintentar,
// visto y aspa. Todo boton de esta app lleva el suyo.
static void dcBtnGlyphPlay(int cx, int cy, int r, uint16_t col){
  fillTriangle(cx - r / 2, cy - r, cx - r / 2, cy + r, cx + r, cy, col);
}
static void dcBtnGlyphRetry(int cx, int cy, int r, uint16_t col){
  arcStroke(cx, cy, r, 40, 330, 3, col);
  fillTriangle(cx + r - 4, cy - r + 1, cx + r + 4, cy - r + 3, cx + r - 1, cy - r + 8, col);
}
static void dcBtnGlyphCheck(int cx, int cy, int r, uint16_t col){
  strokeSegAA(cx - r, cy, cx - r * 0.25f, cy + r * 0.7f, 2.4f, col);
  strokeSegAA(cx - r * 0.25f, cy + r * 0.7f, cx + r, cy - r * 0.75f, 2.4f, col);
}
static void dcBtnGlyphPower(int cx, int cy, int r, uint16_t col){
  arcStroke(cx, cy, r, 300, 600, 3, col);
  strokeSegAA(cx, cy - r, cx, cy - r * 0.15f, 2.6f, col);
}

// #############################################################
// ##  EL GRAFICO DEL TENSTAR GY-BNO085  ·  DIBUJADO POR CODIGO
// ##  ------------------------------------------------------
// ##  NO es una foto ni un PNG: no hay ni un byte de imagen en flash
// ##  por culpa de esta ilustracion. Son las mismas primitivas que usa
// ##  el resto de Flex OS -- rectangulos redondeados, circulos,
// ##  lineas, triangulos y texto -- compuestas para que se reconozca
// ##  el modulo real: placa morada, taladros de montaje, chip central
// ##  con su marca de orientacion, columnas de pads con sus etiquetas,
// ##  serigrafia y la rosa de ejes X/Y/Z que el modulo lleva impresa.
// ##
// ##  DONDE SE DIBUJA: SOLO en la pantalla de requisito de hardware
// ##  (Device Care -> Deteccion de caidas, con el modulo ausente). Ni
// ##  en el inicio, ni en el lanzador, ni en el diagnostico, ni cuando
// ##  el BNO085 esta conectado. Ver dcRenderFallMissing().
// ##
// ##  ANIMACION: el unico elemento vivo es el enlace I2C punteado, que
// ##  avanza con millis(). Es funcion del TIEMPO, no un paso por
// ##  cuadro, asi que su velocidad no depende de los fps -- y se
// ##  repinta SOLO su banda, no la pantalla.
// #############################################################
#define DC_PCB_COL      rgb565(104, 38, 132)   // morado de la placa
#define DC_PCB_EDGE     rgb565( 68, 22,  92)
#define DC_PAD_COL      rgb565(214, 176,  92)  // acabado dorado de los pads
#define DC_CHIP_COL     rgb565( 34, 34,  40)
#define DC_SILK_COL     rgb565(226, 214, 236)
#define DC_SMD_COL      rgb565( 46, 46,  54)

// Etiquetas reales de la columna de pads del modulo.
static const char* DC_PAD_L[10] = { "VCC","GND","SCL","SDA","AD0","CS","INT","RST","PS1","PS0" };

// Dibuja el modulo dentro de la caja dada. `t` es el tiempo en ms
// desde que aparecio la pantalla: solo lo usa el enlace punteado.
static void dcDrawBnoModule(int x, int y, int w, int h, uint32_t t){
  // ---- Placa ----
  int r = w / 14; if(r < 4) r = 4;
  fillRoundRect(x, y, w, h, r, DC_PCB_COL);
  drawRoundRect(x, y, w, h, r, DC_PCB_EDGE);
  // Brillo de mascara: una banda superior muy tenue, como la del resto
  // de superficies del sistema.
  fillRoundRectA(x, y, w, h / 3, r, rgb565(255,255,255), 16);

  // ---- Taladros de montaje (los dos grandes del modulo real) ----
  int hr = w / 13; if(hr < 4) hr = 4;
  int hx  = x + w - hr - w / 12;                 // los dos van en la misma columna
  int hy1 = y + hr + h / 10;
  int hy2 = y + h - hr - h / 10;
  for(int k = 0; k < 2; k++){
    int hy = (k == 0) ? hy1 : hy2;
    fillCircle(hx, hy, hr, DC_PAD_COL);                    // corona metalizada
    fillCircle(hx, hy, hr - 3 > 1 ? hr - 3 : 1, TH_WIN);   // el taladro deja ver el fondo
  }

  // ---- Columna de pads con sus etiquetas ----
  int px = x + w / 9;
  int pr = w / 22; if(pr < 3) pr = 3;
  int p0 = y + h / 12 + pr;
  int pstep = (h - 2 * (h / 12) - 2 * pr) / 9;
  if(pstep < 2 * pr + 1) pstep = 2 * pr + 1;
  for(int i = 0; i < 10; i++){
    int py = p0 + i * pstep;
    if(py + pr > y + h - 2) break;
    fillCircle(px, py, pr, DC_PAD_COL);
    fillCircle(px, py, pr - 2 > 1 ? pr - 2 : 1, DC_PCB_EDGE);
    // La etiqueta solo entra si hay sitio: por debajo de cierto tamano
    // seria una mancha, no un texto.
    if(pstep >= 13 && w >= 120)
      drawText(px + pr + 4, py - 4, DC_PAD_L[i], 1, DC_SILK_COL);
  }

  // ---- Chip central (el BNO085 en su QFN, con la marca del pin 1) ----
  int cw = w * 34 / 100, chh = cw;
  int cxp = x + w / 2 + w / 14 - cw / 2, cyp = y + h / 2 - chh / 2;
  fillRoundRect(cxp, cyp, cw, chh, 3, DC_CHIP_COL);
  fillRoundRectA(cxp, cyp, cw, chh / 2, 3, rgb565(255,255,255), 18);
  fillCircle(cxp + 5, cyp + 5, 2, DC_SILK_COL);          // marca del pin 1
  // Patillas del encapsulado: cuatro filas cortas de trazos claros.
  for(int i = 0; i < 5; i++){
    int o = 3 + i * (cw - 6) / 5;
    fillRect(cxp + o, cyp - 2, 2, 2, DC_PAD_COL);
    fillRect(cxp + o, cyp + chh, 2, 2, DC_PAD_COL);
    fillRect(cxp - 2, cyp + o, 2, 2, DC_PAD_COL);
    fillRect(cxp + cw, cyp + o, 2, 2, DC_PAD_COL);
  }

  // ---- Componentes pasivos (unos pocos, no el esquema entero) ----
  int sw = w / 22, sh = w / 34; if(sw < 3) sw = 3; if(sh < 2) sh = 2;
  fillRect(cxp - sw - 6, cyp - chh / 3, sw, sh, DC_SMD_COL);
  fillRect(cxp + cw + 6, cyp - chh / 3, sw, sh, DC_SMD_COL);
  fillRect(cxp + cw / 3, cyp + chh + 8, sw, sh, DC_SMD_COL);
  fillRect(cxp - sw - 6, cyp + chh / 2, sw, sh, DC_SMD_COL);

  // ---- Serigrafia ----
  if(w >= 120){
    drawText(cxp - 2, y + h / 12 - 2, "GY-BNO085", 1, DC_SILK_COL);
    // Rosa de ejes, como la impresa en la cara del modulo.
    int ax = x + w - w / 5, ay = y + h / 2 + h / 5;
    strokeSegAA(ax, ay, ax + w / 12, ay, 1.2f, DC_SILK_COL);           // X
    strokeSegAA(ax, ay, ax, ay - h / 12, 1.2f, DC_SILK_COL);           // Y
    strokeSegAA(ax, ay, ax - w / 20, ay + h / 20, 1.2f, DC_SILK_COL);  // Z
    drawText(ax + w / 12 + 1, ay - 4, "X", 1, DC_SILK_COL);
    drawText(ax - 2, ay - h / 12 - 9, "Y", 1, DC_SILK_COL);
  }

  // ---- Enlace I2C punteado: lo unico que se mueve ----
  // Dos lineas de puntos que suben desde los pads SCL y SDA hacia el
  // borde izquierdo, avanzando con el tiempo. Muestra QUE hay que
  // conectar sin escribir una sola palabra mas.
  int scl = p0 + 2 * pstep, sda = p0 + 3 * pstep;
  int x0  = x - w / 5;
  if(x0 < 0) x0 = 0;
  int phase = (int)((t / 40u) % 10u);
  for(int lane = 0; lane < 2; lane++){
    int ly = (lane == 0) ? scl : sda;
    for(int dx = phase; dx < px - pr - x0; dx += 10){
      int dot = x0 + dx;
      if(dot < 0 || dot >= x) break;
      fillCircle(dot, ly, 2, (lane == 0) ? TH_PRIM : TH_ACCS);
    }
  }
}

// #############################################################
// ##  SALUD DE FLEX OS  ·  metricas REALES
// ##  ------------------------------------------------------
// ##  Ocho filas, ocho medidas. Ninguna puntuacion fija: el total sale
// ##  de las mismas lecturas que se ensenan encima, asi que si una
// ##  metrica empeora, la puntuacion baja de verdad.
// ##
// ##  Una metrica que NO se puede medir (p.ej. PSRAM en una placa sin
// ##  ella) no puntua ni a favor ni en contra: se reparte el peso
// ##  entre las que si se midieron y la fila dice "No disponible".
// #############################################################
enum { DCH_MEM = 0, DCH_PSRAM, DCH_STAB, DCH_WDT, DCH_SVC, DCH_ERR, DCH_BOOT, DCH_STO, DCH_N };
enum { DCL_OK = 0, DCL_WARN, DCL_BAD, DCL_NA };

// Estado y detalle de una metrica. `pct` es 0..100 cuando la metrica
// se puede expresar como porcentaje; -1 cuando no.
struct DcMetric { uint8_t level; int8_t pct; char detail[40]; };

static void dcMetricSet(DcMetric* m, uint8_t lv, int pct, const char* fmt, ...){
  m->level = lv;
  m->pct   = (int8_t)(pct < -1 ? -1 : (pct > 100 ? 100 : pct));
  va_list ap; va_start(ap, fmt);
  vsnprintf(m->detail, sizeof(m->detail), fmt, ap);
  va_end(ap);
}

static void dcCollect(DcMetric* m){
  memSampleNow();
  const FlexMemSnap* s = memSnap();
  char b1[24], b2[24];

  // 1) SRAM interna. Es la que se agota primero y la que tumba la radio.
  if(s->inTotal){
    int used = (int)(100 - (uint64_t)s->inFree * 100 / s->inTotal);
    flexMemFmt(s->inFree, b1, sizeof(b1));
    dcMetricSet(&m[DCH_MEM], s->inFree < FLEXMEM_SRAM_MIN_BYTES ? DCL_BAD :
                            (s->inFree < FLEXMEM_SRAM_LOW_BYTES ? DCL_WARN : DCL_OK),
                100 - used, "%s %s", b1, "libres");
  } else dcMetricSet(&m[DCH_MEM], DCL_NA, -1, "%s", dct(DCS_NOTAVAIL));

  // 2) PSRAM: presion y fragmentacion, con los mismos cortes que usa
  //    el presupuesto de memoria del sistema.
  if(s->psTotal){
    int lv = flexMemLevel(s);
    flexMemFmt(s->psFree, b1, sizeof(b1));
    flexMemFmt(s->psLargest, b2, sizeof(b2));
    dcMetricSet(&m[DCH_PSRAM], lv >= FLEXMEM_LV_CRITICAL ? DCL_BAD :
                              (lv >= FLEXMEM_LV_WARN ? DCL_WARN : DCL_OK),
                100 - flexMemUsedPct(s), "%s (bloque %s)", b1, b2);
  } else dcMetricSet(&m[DCH_PSRAM], DCL_NA, -1, "%s", dct(DCS_NOTAVAIL));

  // 3) Estabilidad: como arranco esta vez y cuanto lleva encendido.
  {
    bool bad = !(dcBootReason == (uint8_t)ESP_RST_POWERON ||
                 dcBootReason == (uint8_t)ESP_RST_SW ||
                 dcBootReason == (uint8_t)ESP_RST_DEEPSLEEP);
    uint32_t up = millis() / 1000u;
    if(up >= 3600) snprintf(b1, sizeof(b1), "%luh %lum", (unsigned long)(up / 3600), (unsigned long)((up % 3600) / 60));
    else           snprintf(b1, sizeof(b1), "%lum %lus", (unsigned long)(up / 60), (unsigned long)(up % 60));
    dcMetricSet(&m[DCH_STAB], bad ? DCL_WARN : DCL_OK, -1, "%s: %s", dct(DCS_UPTIME), b1);
  }

  // 4) Watchdog: se le pregunta al SDK si esta tarea sigue vigilada.
  {
    bool wd = (esp_task_wdt_status(NULL) == ESP_OK);
    dcMetricSet(&m[DCH_WDT], wd ? DCL_OK : DCL_WARN, -1,
                "%s: %lu/s", dct(DCS_LOOPRATE), (unsigned long)gLoopRate);
  }

  // 5) Servicios: los que el sistema sabe comprobar de verdad.
  {
    int okn = 0, tot = 3;
    if(flexFsReady()) okn++;
    if(clkAnchored)   okn++;
    if(gTimeNvsOk)    okn++;
    dcMetricSet(&m[DCH_SVC], okn == tot ? DCL_OK : (okn >= tot - 1 ? DCL_WARN : DCL_BAD),
                okn * 100 / tot, "%d/%d", okn, tot);
  }

  // 6) Errores recientes: fallo de volcado del panel y NVS de la hora.
  {
    int errs = 0;
    if(flxFlushFault) errs++;
    if(!gTimeNvsOk)   errs++;
    if(!flexFsReady()) errs++;
    dcMetricSet(&m[DCH_ERR], errs == 0 ? DCL_OK : (errs == 1 ? DCL_WARN : DCL_BAD), -1,
                errs == 0 ? "0" : "%d", errs);
  }

  // 7) Reinicios inesperados acumulados (contados en cada arranque).
  dcMetricSet(&m[DCH_BOOT], dcBootAbnormal == 0 ? DCL_OK : (dcBootAbnormal < 3 ? DCL_WARN : DCL_BAD),
              -1, "%lu", (unsigned long)dcBootAbnormal);

  // 8) Almacenamiento: la particion de datos, que es la que se llena.
  if(flexFsReady() && flexFsTotalBytes()){
    uint32_t tot = flexFsTotalBytes(), usd = flexFsUsedBytes();
    int pct = (int)((uint64_t)usd * 100 / tot);
    flexFsFmtSize(tot - usd, b1, sizeof(b1));
    dcMetricSet(&m[DCH_STO], pct >= 95 ? DCL_BAD : (pct >= 85 ? DCL_WARN : DCL_OK),
                100 - pct, "%s %s", b1, "libres");
  } else dcMetricSet(&m[DCH_STO], DCL_NA, -1, "%s", dct(DCS_NOTAVAIL));
}

// Puntuacion 0..100 a partir de las metricas reales. Devuelve false si
// no se pudo medir NADA (entonces la interfaz no ensena puntuacion).
static bool dcScoreFrom(const DcMetric* m, uint8_t* out){
  int sum = 0, n = 0;
  for(int i = 0; i < DCH_N; i++){
    if(m[i].level == DCL_NA) continue;         // no medida: no puntua
    int v;
    if(m[i].pct >= 0){
      // Metrica con porcentaje: es su propio valor, penalizado si su
      // nivel es de aviso o critico.
      v = m[i].pct;
      if(m[i].level == DCL_WARN) v = v * 70 / 100;
      if(m[i].level == DCL_BAD)  v = v * 35 / 100;
    } else {
      v = (m[i].level == DCL_OK) ? 100 : (m[i].level == DCL_WARN ? 62 : 25);
    }
    sum += v; n++;
  }
  if(n == 0) return false;
  int sc = sum / n;
  if(sc < 0) sc = 0;
  if(sc > 100) sc = 100;
  if(out) *out = (uint8_t)sc;
  return true;
}
static bool dcHealthScore(uint8_t* out){
  DcMetric m[DCH_N];
  dcCollect(m);
  return dcScoreFrom(m, out);
}

static uint16_t dcLevelColor(uint8_t lv){
  switch(lv){
    case DCL_OK:   return TH_OK;
    case DCL_WARN: return TH_WARN;
    case DCL_BAD:  return TH_ERR;
    default:       return TH_MUTE;
  }
}
static uint16_t dcScoreColor(int sc){
  if(sc >= 80) return TH_OK;
  if(sc >= 55) return TH_WARN;
  return TH_ERR;
}
static const char* dcScoreWord(int sc){
  if(sc >= 80) return dct(DCS_ALLGOOD);
  if(sc >= 55) return dct(DCS_ATTENTION);
  return dct(DCS_CRITICAL);
}

// #############################################################
// ##  PIEZAS DE INTERFAZ COMPARTIDAS
// #############################################################
// Anillo de puntuacion con barrido animado. `p` (0..1) es cuanto del
// arco esta dibujado; sale del tiempo, no de un contador por cuadro.
static void dcScoreRing(int cx, int cy, int r, int score, float p, uint16_t col){
  if(p < 0) p = 0;
  if(p > 1) p = 1;
  int th = r / 6; if(th < 4) th = 4;
  fillRing(cx, cy, r, th, TH_TRACK);
  float a1 = -90.0f + 360.0f * (score / 100.0f) * p;
  arcStroke(cx, cy, r - th / 2, -90.0f, a1, th, col);
  char b[8];
  snprintf(b, sizeof(b), "%d", (int)(score * p + 0.5f));
  int fs = uiFontFit(b, r * 3 / 2, 6);
  drawTextC(cx, cy - uiLineH(fs) / 2 - 2, b, fs, TH_TXT);
  drawTextC(cx, cy + r / 3 + 2, "/100", 1, TH_MUTE);
}

// Fila etiqueta / valor con semaforo. La usan Salud y Estado.
static int dcRowLed(int x, int y, int w, const char* label, const char* value, uint8_t lv){
  fillCircle(x + 9, y + 9, 6, dcLevelColor(lv));
  drawTextClip(x + 24, y, label, 2, TH_TXT, x + w / 2 + 24);
  drawTextR(x + w, y + 1, value, 1, TH_TXT2);
  return y + 26;
}

// Boton con glifo. TODO boton de esta app pasa por aqui: asi ninguno
// se queda sin icono y la geometria del dibujo y la del toque son la
// misma (se registra su zona pulsable al dibujarlo).
enum { DCB_PLAY = 0, DCB_RETRY, DCB_CHECK, DCB_POWER, DCB_NONE };
static void dcButton(int x, int y, int w, int h, const char* label, int glyph,
                     uint16_t bg, uint16_t fg, uint8_t hitId){
  fillRoundRect(x, y, w, h, h / 2, bg);
  int gx = x + h / 2 + 2, gy = y + h / 2, gr = h / 5;
  int tw = textW(label, 2);
  int total = (glyph == DCB_NONE) ? tw : (gr * 2 + 10 + tw);
  int sx = x + (w - total) / 2;
  if(glyph != DCB_NONE){
    gx = sx + gr;
    switch(glyph){
      case DCB_PLAY:  dcBtnGlyphPlay(gx, gy, gr, fg); break;
      case DCB_RETRY: dcBtnGlyphRetry(gx, gy, gr, fg); break;
      case DCB_CHECK: dcBtnGlyphCheck(gx, gy, gr, fg); break;
      case DCB_POWER: dcBtnGlyphPower(gx, gy, gr, fg); break;
      default: break;
    }
    sx += gr * 2 + 10;
  }
  drawText(sx, y + (h - uiLineH(2)) / 2, label, 2, fg);
  if(hitId != 0xFF) dcHitAdd(x, y, w, h, hitId);
}

// Cabecera propia de la app: chevron atras + titulo + subtitulo.
// Es APP_CUSTOM_HEADER porque el titulo cambia con la pantalla interna.
static void dcHeader(const char* title){
  if(gHosted) return;                     // dentro de DeX el nombre lo pone la ventana
  fillRect(0, 0, SCR_W, WIN_TOP, WIN_BG);
  // Barra de estado con la MISMA geometria que el marco estandar de app
  // y que el escritorio: la hora sale de la unica funcion del sistema
  // que la dibuja, no de una copia.
  uint16_t W = TH_NAV;
  cronoBarClock(16, W);
  drawWifi(SCR_W - 66, 28, 11, W);
  drawBattery(SCR_W - 46, 20, 30, 15, 82, W);
  strokeSegAA(30, 62, 18, 54, 2.4f, TH_TXT);
  strokeSegAA(18, 54, 30, 46, 2.4f, TH_TXT);
  int fs = uiFontFit(title, SCR_W - 120, 3);
  drawTextC(SCR_W / 2, 49, title, fs, TH_TXT);
}

// #############################################################
// ##  PANTALLA 1  ·  INICIO DE DEVICE CARE
// ##  ------------------------------------------------------
// ##  Tarjeta de estado con el anillo de puntuacion (animado al
// ##  entrar) y una rejilla de seis accesos, cada uno con su glifo
// ##  vectorial. Maquetada contra el lienzo real (uiBox), asi que
// ##  tambien vale dentro de una ventana de Modo PC.
// #############################################################
enum { DCH_HERO = 100, DCH_T0, DCH_T1, DCH_T2, DCH_T3, DCH_T4, DCH_T5 };

static uint8_t dcHomeScore = 0;
static bool    dcHomeScoreOk = false;

// Geometria de la rejilla, calculada una vez por repintado.
struct DcGrid { int x, y, cw, ch, gap; };
static DcGrid dcGrid;

static void dcHomeLayout(){
  int bx, by, bw, bh; uiBox(bx, by, bw, bh);
  int pad = uiPad();
  dcGrid.gap = uiGap();
  dcGrid.x   = bx + pad;
  dcGrid.cw  = (bw - 2 * pad - dcGrid.gap) / 2;
  int avail  = bh - (bh * 30 / 100) - 2 * pad;
  dcGrid.ch  = (avail - 2 * dcGrid.gap) / 3;
  if(dcGrid.ch > 132) dcGrid.ch = 132;
  if(dcGrid.ch < 56)  dcGrid.ch = 56;
  dcGrid.y   = by + bh - pad - 3 * dcGrid.ch - 2 * dcGrid.gap;
}

static void dcDrawTile(int idx, int x, int y, int w, int h, const char* label, int icon){
  uiSurface(x, y, w, h, 20, UIS_CARD);
  drawRoundRect(x, y, w, h, 20, TH_BORDER);
  int S = h * 42 / 100; if(S > 56) S = 56;
  dcSectionIcon(icon, x + (w - S) / 2, y + h / 8, S, TH_TXT2, wallAccent());
  int fs = uiFontFit(label, w - 12, 2);
  drawTextC(x + w / 2, y + h - uiLineH(fs) - 10, label, fs, TH_TXT);
  dcHitAdd(x, y, w, h, (uint8_t)(DCH_T0 + idx));
}

// #############################################################
// ##  LA TARJETA DE ESTADO  ·  UN SOLO SITIO QUE LA DIBUJA
// ##  ------------------------------------------------------
// ##  REGLA DEL LIQUID GLASS, y es la que hay que respetar en TODA
// ##  animacion del sistema: drawLiquidGlassPanel LEE la region del
// ##  buffer, la desenfoca y la ESCRIBE encima. Volver a dibujarlo
// ##  sobre su propia salida desenfoca lo ya desenfocado, y ademas
// ##  vuelve a aplicar tinte, especular y borde. En una animacion eso
// ##  se apila cuadro a cuadro: el texto se emborrona un poco mas cada
// ##  vez hasta quedar ilegible.
// ##
// ##  Por eso el vidrio SIEMPRE se compone sobre un fondo LIMPIO, y
// ##  por eso el cuadro de la animacion no puede ser "repinto solo el
// ##  anillo": tiene que rehacer la tarjeta ENTERA desde el fondo de
// ##  pagina. Esta funcion es ese unico sitio -- la usan el repintado
// ##  completo y cada cuadro de la animacion --, asi que las dos rutas
// ##  no pueden divergir, ni en el material ni en el contenido.
// ##
// ##  `ringP` (0..1) es lo unico que cambia entre cuadros: cuanto del
// ##  arco esta dibujado.
// #############################################################
static void dcHomeCard(int cx, int cy, int cw, int cardH, float ringP){
  uiSurface(cx, cy, cw, cardH, 24, UIS_CARD);
  drawRoundRect(cx, cy, cw, cardH, 24, TH_BORDER);

  int rr = cardH / 2 - 16; if(rr > 54) rr = 54; if(rr < 26) rr = 26;
  int rcx = cx + 24 + rr, rcy = cy + cardH / 2;
  if(dcHomeScoreOk) dcScoreRing(rcx, rcy, rr, dcHomeScore, ringP, dcScoreColor(dcHomeScore));
  else { fillRing(rcx, rcy, rr, rr / 6, TH_TRACK); drawTextC(rcx, rcy - 8, "--", 4, TH_MUTE); }

  int tx = rcx + rr + 18;
  int tw = cx + cw - 16 - tx;
  const char* word = dcHomeScoreOk ? dcScoreWord(dcHomeScore) : dct(DCS_NOTAVAIL);
  drawTextClip(tx, rcy - 30, word, 3, TH_TXT, tx + tw);
  char sub[64];
  if(dcLastCheck){
    int y2, mo, d;
    long local = (long)dcLastCheck + FLEXOS_TZ_OFFSET_SEC;
    long days  = local / 86400L; if(local % 86400L < 0) days--;
    clkCivilFromDays(days, y2, mo, d);
    snprintf(sub, sizeof(sub), "%s: %d %s", dct(DCS_LASTCHECK), d, MO_SHORT[LI()][(mo - 1) % 12]);
  } else {
    snprintf(sub, sizeof(sub), "%s: %s", dct(DCS_LASTCHECK), dct(DCS_NEVER));
  }
  drawTextClip(tx, rcy - 2, sub, 1, TH_TXT2, tx + tw);
  // Estado de la deteccion de caidas, en la misma tarjeta: es lo unico
  // que puede estar corriendo en segundo plano.
  const char* fs2 = dcFallOn ? (flexBnoAvailable() ? dct(DCS_MONITORING) : dct(DCS_UNAVAILABLE))
                             : dct(DCS_MONITOR_OFF);
  uint16_t fc = dcFallOn ? (flexBnoAvailable() ? TH_OK : TH_WARN) : TH_MUTE;
  fillCircle(tx + 6, rcy + 26, 5, fc);
  drawTextClip(tx + 18, rcy + 19, fs2, 1, TH_TXT2, tx + tw);
}

// Geometria de la tarjeta. Tambien en un solo sitio, por el mismo
// motivo: la animacion tiene que componer EXACTAMENTE el mismo
// rectangulo que el repintado completo, o el borde de la banda se
// notaria como una costura.
static void dcHomeCardGeom(int &cx, int &cy, int &cw, int &cardH){
  int bx, by, bw, bh; uiBox(bx, by, bw, bh);
  (void)bh;
  int pad = uiPad();
  dcHomeLayout();
  cardH = dcGrid.y - by - pad - uiGap();
  if(cardH < 96) cardH = 96;
  cx = bx + pad; cy = by + pad; cw = bw - 2 * pad;
}

static void dcRenderHome(){
  setBuf(fb);
  dcHitClear();
  int bx, by, bw, bh; uiBox(bx, by, bw, bh);
  fillRect(bx, by, bw, bh, WIN_BG);

  // ---- Tarjeta de estado ----
  int cx, cy, cw, cardH;
  dcHomeCardGeom(cx, cy, cw, cardH);
  dcHomeScoreOk = dcHealthScore(&dcHomeScore);
  dcHomeCard(cx, cy, cw, cardH, 1.0f);
  dcHitAdd(cx, cy, cw, cardH, DCH_HERO);

  // ---- Rejilla de seis accesos ----
  static const int ICN[6]  = { DCI_STATUS, DCI_DIAG, DCI_FALL, DCI_HEALTH, DCI_OPT, DCI_HIST };
  static const int LBL[6]  = { DCS_STATUS, DCS_DIAG, DCS_FALL, DCS_HEALTH, DCS_OPT, DCS_HIST };
  for(int i = 0; i < 6; i++){
    int col = i % 2, row = i / 2;
    int x = dcGrid.x + col * (dcGrid.cw + dcGrid.gap);
    int y = dcGrid.y + row * (dcGrid.ch + dcGrid.gap);
    dcDrawTile(i, x, y, dcGrid.cw, dcGrid.ch, dct(LBL[i]), ICN[i]);
  }
  flxFlush(WIN_TOP, WIN_BOT);
}

// #############################################################
// ##  BARRIDO DEL ANILLO AL ENTRAR  (~520 ms)
// ##  ------------------------------------------------------
// ##  CADA CUADRO PARTE DE CERO, y esa es toda la clave:
// ##
// ##    1. se compone en bbuf, nunca sobre lo ya publicado;
// ##    2. la tarjeta se rellena ANTES con el fondo de pagina, asi que
// ##       el vidrio desenfoca un fondo LIMPIO -- el mismo que ve el
// ##       repintado completo -- y no su propia salida del cuadro
// ##       anterior. Sin esto, el desenfoque se apila y el numero del
// ##       centro se emborrona un poco mas en cada cuadro;
// ##    3. se dibuja la tarjeta ENTERA con dcHomeCard(), no solo el
// ##       anillo: el panel de vidrio escribe de borde a borde, asi
// ##       que repintar solo el anillo borraba los textos de la
// ##       derecha y no los devolvia;
// ##    4. se publica SOLO la banda del anillo, que es lo unico que
// ##       cambia entre cuadros.
// ##
// ##  El relleno del fondo va SIN recorte a proposito: el tinte
// ##  adaptativo del vidrio muestrea la luminancia de la tarjeta
// ##  COMPLETA (ver drawLiquidGlassPanelEx), asi que si las filas de
// ##  fuera de la banda tuvieran contenido viejo el tinte cambiaria de
// ##  un cuadro a otro y la banda se veria como una costura de otro
// ##  color. El recorte se pone DESPUES, y solo acota lo que se
// ##  escribe y cuantas filas desenfoca el vidrio.
// ##
// ##  Resultado: el cuadro N es identico al cuadro 1 para el mismo p.
// ##  Lo comprueba testLiquidGlassSinApilar() en tests/host.
// #############################################################
#define DC_RING_MS 520
static void dcHomeAnimTick(){
  if(dcScreen != DC_HOME) return;
  uint32_t e = millis() - dcAnimT0;
  if(e > DC_RING_MS) return;
  if(millis() - dcAnimMs < 33) return;
  dcAnimMs = millis();
  if(!bbuf) return;

  int bx, by, bw, bh; uiBox(bx, by, bw, bh);
  (void)bx; (void)bw;
  int cx, cy, cw, cardH;
  dcHomeCardGeom(cx, cy, cw, cardH);
  int rr = cardH / 2 - 16; if(rr > 54) rr = 54; if(rr < 26) rr = 26;
  int rcy = cy + cardH / 2;
  int b0 = rcy - rr - 4, b1 = rcy + rr + 4;
  if(b0 < by) b0 = by;
  if(b1 > by + bh - 1) b1 = by + bh - 1;

  int c0 = gClipY0, c1 = gClipY1, cxx0 = gClipX0, cxx1 = gClipX1;
  setBuf(bbuf);
  gClipY0 = 0; gClipY1 = SCR_H - 1; gClipX0 = 0; gClipX1 = SCR_W - 1;
  // 1) Margenes de la banda. present() publica FILAS ENTERAS, asi que lo
  //    que quede a los lados de la tarjeta tiene que ser lo que ya hay en
  //    pantalla; si no, se publicaria contenido viejo de bbuf.
  for(int j = b0; j <= b1; j++)
    memcpy(bbuf + (size_t)j * SCR_W, fb + (size_t)j * SCR_W, (size_t)SCR_W * 2);
  // 2) La tarjeta ENTERA a fondo de pagina. Sin recorte a proposito: es el
  //    fondo que el vidrio va a desenfocar y el que muestrea su tinte
  //    adaptativo, y los dos miran la tarjeta completa.
  fillRect(cx, cy, cw, cardH, WIN_BG);
  // 3) A partir de aqui solo se escribe -- y solo se desenfoca -- la banda.
  gClipY0 = b0; gClipY1 = b1;
  float p = (float)e / (float)DC_RING_MS;
  p = 1.0f - (1.0f - p) * (1.0f - p);        // ease-out
  dcHomeCard(cx, cy, cw, cardH, p);
  gClipY0 = c0; gClipY1 = c1; gClipX0 = cxx0; gClipX1 = cxx1;
  present(b0, b1);
  setBuf(fb);
}

// #############################################################
// ##  PANTALLA 2  ·  ESTADO DEL DISPOSITIVO
// ##  ------------------------------------------------------
// ##  Un resumen de lo que el sistema SABE de si mismo ahora mismo.
// ##  Cada fila es una lectura; nada de aqui es decorativo.
// #############################################################
enum { DCH_RUNDIAG = 120 };

static void dcRenderStatus(){
  setBuf(fb);
  dcHitClear();
  int bx, by, bw, bh; uiBox(bx, by, bw, bh);
  fillRect(bx, by, bw, bh, WIN_BG);
  int pad = uiPad();
  int x = bx + pad, w = bw - 2 * pad;
  int y = by + pad;

  DcMetric m[DCH_N];
  dcCollect(m);
  uint8_t sc = 0; bool ok = dcScoreFrom(m, &sc);

  // Cabecera: telefono dibujado + veredicto.
  uiSurface(x, y, w, 96, 20, UIS_CARD);
  drawRoundRect(x, y, w, 96, 20, TH_BORDER);
  dcSectionIcon(DCI_STATUS, x + 16, y + 16, 64, TH_TXT2, wallAccent());
  drawTextClip(x + 94, y + 20, ok ? dcScoreWord(sc) : dct(DCS_NOTAVAIL), 3, TH_TXT, x + w - 12);
  char b[64];
  drawTextClip(x + 94, y + 50, "Flex OS Ultra \xC2\xB7 ESP32-P4 \xC2\xB7 480x800", 1, TH_TXT2, x + w - 12);
  // El ritmo del bucle es una medida real: con 0 vueltas/s todavia no
  // hay ninguna y se dice, en vez de ensenar un cero que parece medido.
  if(gLoopRate) snprintf(b, sizeof(b), "%s: %lu/s", dct(DCS_LOOPRATE), (unsigned long)gLoopRate);
  else          snprintf(b, sizeof(b), "%s: %s", dct(DCS_LOOPRATE), dct(DCS_NOTAVAIL));
  drawTextClip(x + 94, y + 68, b, 1, TH_MUTE, x + w - 12);
  y += 96 + uiGap();

  static const int LBL[DCH_N] = { DCS_MEMORY, DCS_PSRAM, DCS_STABILITY, DCS_WATCHDOG,
                                  DCS_SERVICES, DCS_ERRORS, DCS_REBOOTS, DCS_STORAGE };
  for(int i = 0; i < DCH_N; i++){
    if(y + 26 > by + bh - pad - 62) break;
    y = dcRowLed(x, y, w, dct(LBL[i]), m[i].detail, m[i].level);
  }

  // Boton: ejecutar el diagnostico completo desde aqui.
  int bwid = w, bhh = 46, byy = by + bh - pad - bhh;
  dcButton(x, byy, bwid, bhh, dct(DCS_DIAG), DCB_PLAY, TH_PRIM, TH_ONACC, DCH_RUNDIAG);
  flxFlush(WIN_TOP, WIN_BOT);
}

// #############################################################
// ##  PANTALLA 3  ·  DETECCION DE CAIDAS
// ##  ------------------------------------------------------
// ##  Al entrar se comprueba el GY-BNO085 de verdad. Mientras el
// ##  driver dialoga con el modulo la pantalla dice "Detectando"; si
// ##  no aparece, sale el requisito de hardware CON el grafico; si
// ##  aparece, sale el estado de disponibilidad con los sensores que
// ##  se han comprobado UNO A UNO.
// ##
// ##  El cambio de una a otra es automatico: dcFallTick() vuelve a
// ##  dibujar en cuanto el estado del driver cambia, asi que conectar
// ##  el modulo con la pantalla abierta la actualiza sola.
// #############################################################
enum { DCH_FALLTOGGLE = 140, DCH_FALLTEST, DCH_FALLOK, DCH_FALLRETRY };

static int dcFallShown = -1;      // estado del driver que hay dibujado

// Geometria del grafico del modulo. La comparten el dibujo y el
// repintado de su banda animada: dos copias de estos numeros habrian
// acabado separandose, y entonces la animacion repintaria una franja
// que no es la del grafico.
static void dcFallGraphGeom(int &gx, int &gy, int &gw, int &gh){
  int bx, by, bw, bh; uiBox(bx, by, bw, bh);
  (void)bh;
  int pad = uiPad();
  int w = bw - 2 * pad;
  gw = w - 2 * pad; if(gw > 300) gw = 300;
  gh = gw * 62 / 100;
  gx = bx + (bw - gw) / 2;
  gy = by + pad + uiLineH(3) + 6 + uiLineH(uiFontFit(dct(DCS_NEEDHW), w, 2)) + uiGap();
}

static void dcRenderFallMissing(){
  setBuf(fb);
  dcHitClear();
  int bx, by, bw, bh; uiBox(bx, by, bw, bh);
  fillRect(bx, by, bw, bh, WIN_BG);
  int pad = uiPad();
  int w = bw - 2 * pad;
  int y = by + pad;

  drawTextC(bx + bw / 2, y, dct(DCS_FALL), 3, TH_TXT);
  y += uiLineH(3) + 6;
  int fs = uiFontFit(dct(DCS_NEEDHW), w, 2);
  drawTextC(bx + bw / 2, y, dct(DCS_NEEDHW), fs, TH_TXT2);
  y += uiLineH(fs) + uiGap();

  // ---- EL GRAFICO. Solo aqui, y solo con el modulo ausente. ----
  int gx, gy, gw, gh;
  dcFallGraphGeom(gx, gy, gw, gh);
  dcDrawBnoModule(gx, gy, gw, gh, millis() - dcAnimT0);
  y = gy + gh + uiGap();

  drawTextC(bx + bw / 2, y, dct(DCS_NEEDHW_1), 1, TH_MUTE);
  y += 18;
  drawTextC(bx + bw / 2, y, "TENSTAR GY-BNO085", 3, TH_TXT);
  y += uiLineH(3) + 4;
  drawTextC(bx + bw / 2, y, dct(DCS_NEEDHW_3), 2, TH_ACCS);
  y += uiLineH(2) + 8;
  fs = uiFontFit(dct(DCS_NEEDHW_2), w, 1);
  drawTextC(bx + bw / 2, y, dct(DCS_NEEDHW_2), fs, TH_TXT2);

  int bhh = 46, bwid = 200;
  int byy = by + bh - pad - bhh;
  dcButton(bx + (bw - bwid) / 2, byy, bwid, bhh, dct(DCS_UNDERSTOOD), DCB_CHECK,
           TH_PRIM, TH_ONACC, DCH_FALLOK);
  // Reintento explicito: el sondeo no corre en bucle por su cuenta.
  dcButton(bx + (bw - bwid) / 2, byy - bhh - 10, bwid, bhh, dct(DCS_REPEAT), DCB_RETRY,
           TH_SURF, TH_TXT, DCH_FALLRETRY);
  flxFlush(WIN_TOP, WIN_BOT);
}

static void dcRenderFallProbing(){
  setBuf(fb);
  dcHitClear();
  int bx, by, bw, bh; uiBox(bx, by, bw, bh);
  fillRect(bx, by, bw, bh, WIN_BG);
  drawTextC(bx + bw / 2, by + bh / 2 - 60, dct(DCS_FALL), 3, TH_TXT);
  // Tres puntos que laten con el tiempo: la unica animacion posible
  // mientras el driver espera respuesta.
  uint32_t e = millis() - dcAnimT0;
  for(int i = 0; i < 3; i++){
    int ph = (int)((e / 220u + i) % 3u);
    fillCircle(bx + bw / 2 - 20 + i * 20, by + bh / 2, ph == 0 ? 8 : 5,
               ph == 0 ? TH_PRIM : TH_TRACK);
  }
  drawTextC(bx + bw / 2, by + bh / 2 + 30, dct(DCS_DETECTING), 2, TH_TXT2);
  flxFlush(WIN_TOP, WIN_BOT);
}

// Fila de comprobacion: el tick SOLO se dibuja si la comprobacion se
// hizo de verdad (mascara del driver). Si no, un guion.
static int dcCheckRow(int x, int y, int w, const char* label, bool done, const char* extra){
  if(done){
    fillCircle(x + 10, y + 9, 9, TH_OK);
    dcBtnGlyphCheck(x + 10, y + 9, 5, TH_ONACC);
  } else {
    drawCircle(x + 10, y + 9, 9, TH_DIV);
    fillRect(x + 6, y + 8, 8, 2, TH_MUTE);
  }
  drawTextClip(x + 28, y, label, 2, done ? TH_TXT : TH_MUTE, x + w - 90);
  if(extra && extra[0]) drawTextR(x + w, y + 2, extra, 1, TH_TXT2);
  return y + 28;
}

static void dcRenderFallReady(){
  setBuf(fb);
  dcHitClear();
  int bx, by, bw, bh; uiBox(bx, by, bw, bh);
  fillRect(bx, by, bw, bh, WIN_BG);
  int pad = uiPad();
  int x = bx + pad, w = bw - 2 * pad;
  int y = by + pad;

  drawTextC(bx + bw / 2, y, dct(DCS_FALL), 3, TH_TXT);
  y += uiLineH(3) + 8;

  // Tarjeta del modulo: nombre, direccion y version REALES del sensor.
  uiSurface(x, y, w, 92, 20, UIS_CARD);
  drawRoundRect(x, y, w, 92, 20, TH_BORDER);
  fillCircle(x + 24, y + 24, 8, TH_OK);
  drawText(x + 40, y + 16, dct(DCS_AVAILABLE), 2, TH_OK);
  drawTextClip(x + 16, y + 42, "TENSTAR GY-BNO085", 3, TH_TXT, x + w - 12);
  char b[64];
  uint8_t ad = flexBnoAddr();
  if(flexBnoChecks() & FLEXBNO_CHK_I2C)
    snprintf(b, sizeof(b), "%s \xC2\xB7 I\xC2\xB2""C 0x%02X \xC2\xB7 SW %u.%u",
             dct(DCS_IMUNAME), ad, (unsigned)flexBnoSwMajor(), (unsigned)flexBnoSwMinor());
  else
    snprintf(b, sizeof(b), "%s \xC2\xB7 I\xC2\xB2""C 0x%02X", dct(DCS_IMUNAME), ad);
  drawTextClip(x + 16, y + 70, b, 1, TH_TXT2, x + w - 12);
  y += 92 + uiGap();

  uint8_t c = flexBnoChecks();
  y = dcCheckRow(x, y, w, dct(DCS_I2CLINK), (c & FLEXBNO_CHK_I2C)    != 0, NULL);
  y = dcCheckRow(x, y, w, dct(DCS_ACCEL),   (c & FLEXBNO_CHK_ACCEL)  != 0, NULL);
  y = dcCheckRow(x, y, w, dct(DCS_GYRO),    (c & FLEXBNO_CHK_GYRO)   != 0, NULL);
  y = dcCheckRow(x, y, w, dct(DCS_MAG),     (c & FLEXBNO_CHK_MAG)    != 0, NULL);
  y = dcCheckRow(x, y, w, dct(DCS_FUSION),  (c & FLEXBNO_CHK_FUSION) != 0, NULL);
  y += 6;

  // Estado del detector, en vivo. Es la prueba de que la
  // monitorizacion corre en segundo plano.
  snprintf(b, sizeof(b), "%s: %s", dct(DCS_MONITORING), flexFallStateName(flexFallState(&dcDet), LI()));
  drawTextClip(x, y, dcFallOn ? b : dct(DCS_MONITOR_OFF), 1, TH_TXT2, x + w);
  y += 18;
  drawTextClip(x, y, dct(DCS_EXPERIMENTAL), 1, TH_MUTE, x + w);

  int bhh = 46, byy = by + bh - pad - bhh;
  dcButton(x, byy, w, bhh, dcFallOn ? dct(DCS_DISABLE) : dct(DCS_ENABLE), DCB_POWER,
           dcFallOn ? TH_SURF2 : TH_PRIM, dcFallOn ? TH_TXT : TH_ONACC, DCH_FALLTOGGLE);
  dcButton(x, byy - bhh - 10, w, bhh, dct(DCS_TESTMODULE), DCB_PLAY, TH_SURF, TH_TXT, DCH_FALLTEST);
  flxFlush(WIN_TOP, WIN_BOT);
}

static void dcRenderFall(){
  int st = flexBnoState();
  dcFallShown = st;
  if(st == FLEXBNO_ST_READY)      dcRenderFallReady();
  else if(st == FLEXBNO_ST_ABSENT || st == FLEXBNO_ST_LOST) dcRenderFallMissing();
  else                            dcRenderFallProbing();
}

// Anima la pantalla de caidas SIN repintarla entera, y cambia de
// pantalla sola en cuanto el driver cambia de estado (conectar el
// modulo con esto abierto lo actualiza sin tocar nada).
static void dcFallAnimTick(){
  int st = flexBnoState();
  if(st != dcFallShown){
    // El estado del modulo cambio: la pantalla que toca es otra.
    bool wasMissing = (dcFallShown == FLEXBNO_ST_ABSENT || dcFallShown == FLEXBNO_ST_LOST);
    bool isMissing  = (st == FLEXBNO_ST_ABSENT || st == FLEXBNO_ST_LOST);
    bool wasReady   = (dcFallShown == FLEXBNO_ST_READY);
    bool isReady    = (st == FLEXBNO_ST_READY);
    if(wasMissing != isMissing || wasReady != isReady){ dcAnimT0 = millis(); dcRenderFall(); return; }
    dcFallShown = st;
  }
  if(millis() - dcAnimMs < 40) return;
  dcAnimMs = millis();
  int bx, by, bw, bh; uiBox(bx, by, bw, bh);
  if(st == FLEXBNO_ST_ABSENT || st == FLEXBNO_ST_LOST){
    // Solo la banda del grafico: el enlace punteado es lo unico vivo.
    int gx, gy, gw, gh;
    dcFallGraphGeom(gx, gy, gw, gh);
    int b0 = gy, b1 = gy + gh;
    if(b1 > by + bh - 1) b1 = by + bh - 1;
    setBuf(fb);
    int c0 = gClipY0, c1 = gClipY1;
    gClipY0 = b0; gClipY1 = b1;
    fillRect(bx, b0, bw, b1 - b0 + 1, WIN_BG);
    dcDrawBnoModule(gx, gy, gw, gh, millis() - dcAnimT0);
    gClipY0 = c0; gClipY1 = c1;
    flxFlush(b0, b1);
    return;
  }
  if(st == FLEXBNO_ST_READY){
    // Con el modulo presente basta con refrescar la linea de estado del
    // detector una vez por segundo: no hay grafico ninguno.
    static uint32_t last = 0;
    if(millis() - last < 1000) return;
    last = millis();
    dcRenderFallReady();
    return;
  }
  dcRenderFallProbing();
}

// #############################################################
// ##  PANTALLA 4  ·  HISTORIAL
// #############################################################
static void dcHistIcon(uint8_t kind, int x, int y, int S){
  switch(kind){
    case DC_EV_FALL: dcSectionIcon(DCI_FALL,   x, y, S, TH_WARN, TH_WARN); break;
    case DC_EV_POST: dcSectionIcon(DCI_STATUS, x, y, S, TH_TXT2, wallAccent()); break;
    case DC_EV_OPT:  dcSectionIcon(DCI_OPT,    x, y, S, TH_TXT2, wallAccent()); break;
    default:         dcSectionIcon(DCI_DIAG,   x, y, S, TH_TXT2, wallAccent()); break;
  }
}

static void dcRenderHist(){
  setBuf(fb);
  dcHitClear();
  dcHistLoad();
  int bx, by, bw, bh; uiBox(bx, by, bw, bh);
  fillRect(bx, by, bw, bh, WIN_BG);
  int pad = uiPad();
  int x = bx + pad, w = bw - 2 * pad;
  int y = by + pad - dcScroll;

  if(dcHistN == 0){
    dcSectionIcon(DCI_HIST, bx + bw / 2 - 32, by + bh / 3, 64, TH_MUTE, TH_MUTE);
    drawTextC(bx + bw / 2, by + bh / 3 + 80, dct(DCS_NOEVENTS), 3, TH_TXT2);
    drawTextC(bx + bw / 2, by + bh / 3 + 112, dct(DCS_EMPTYHIST), 1, TH_MUTE);
    flxFlush(WIN_TOP, WIN_BOT);
    return;
  }

  int c0 = gClipY0, c1 = gClipY1;
  gClipY0 = by; gClipY1 = by + bh - 1;
  int lastDay = -1;
  for(int i = 0; i < dcHistN; i++){
    const DcRec* r = &dcHist[i];
    long local = (long)r->utc + FLEXOS_TZ_OFFSET_SEC;
    long days  = local / 86400L; if(local % 86400L < 0) days--;
    int yy, mo, dd; clkCivilFromDays(days, yy, mo, dd);
    if((int)days != lastDay){
      lastDay = (int)days;
      if(y + 22 > by && y < by + bh){
        char hd[32];
        snprintf(hd, sizeof(hd), "%02d %s", dd, MO_SHORT[LI()][(mo - 1) % 12]);
        drawText(x, y, hd, 2, TH_TXT2);
      }
      y += 26;
    }
    if(y + 62 > by && y < by + bh){
      uiSurface(x, y, w, 58, 16, UIS_CARD);
      dcHistIcon(r->kind, x + 10, y + 11, 36);
      const char* title = (r->kind == DC_EV_FALL) ? dct(DCS_EVFALL) :
                          (r->kind == DC_EV_POST) ? dct(DCS_EVPOST) :
                          (r->kind == DC_EV_OPT)  ? dct(DCS_EVOPT)  : dct(DCS_EVDIAG);
      drawTextClip(x + 54, y + 10, title, 2, TH_TXT, x + w - 60);
      char sub[64];
      if(r->kind == DC_EV_FALL){
        snprintf(sub, sizeof(sub), "%s: %u \xC2\xB7 %s %u.%u g",
                 dct(DCS_CONFIDENCE), (unsigned)r->score, dct(DCS_IMPACT),
                 (unsigned)(r->extra / 10), (unsigned)(r->extra % 10));
        drawTextR(x + w - 12, y + 12, "!", 3, TH_WARN);
      } else if(r->kind == DC_EV_OPT){
        snprintf(sub, sizeof(sub), "%s", dct(DCS_OPTRUN));
      } else {
        snprintf(sub, sizeof(sub), "%s %u/100", dct(DCS_SCORE), (unsigned)r->score);
        char sc[8]; snprintf(sc, sizeof(sc), "%u", (unsigned)r->score);
        drawTextR(x + w - 12, y + 12, sc, 3, dcScoreColor(r->score));
      }
      drawTextClip(x + 54, y + 34, sub, 1, TH_TXT2, x + w - 60);
    }
    y += 66;
  }
  gClipY0 = c0; gClipY1 = c1;
  flxFlush(WIN_TOP, WIN_BOT);
}

// Alto REAL de la lista: 66 px por tarjeta y 26 mas por cada CAMBIO de
// dia. Contar una cabecera por registro sobraria alto y el tope de
// desplazamiento dejaria un hueco vacio al final de la lista.
static int dcHistContentH(){
  int h = uiPad() * 2;
  long lastDay = -1;
  for(int i = 0; i < dcHistN; i++){
    long local = (long)dcHist[i].utc + FLEXOS_TZ_OFFSET_SEC;
    long days  = local / 86400L; if(local % 86400L < 0) days--;
    if(days != lastDay){ h += 26; lastDay = days; }
    h += 66;
  }
  return h;
}

// #############################################################
// ##  NAVEGACION Y CICLO DE VIDA
// #############################################################
static void dcRender(){
  switch(dcScreen){
    case DC_HOME:      dcHeader(dct(DCS_APPTITLE)); dcRenderHome();   break;
    case DC_STATUS:    dcHeader(dct(DCS_STATUS));   dcRenderStatus(); break;
    case DC_FALL:      dcHeader(dct(DCS_FALL));     dcRenderFall();   break;
    case DC_HIST:      dcHeader(dct(DCS_HIST));     dcRenderHist();   break;
    case DC_HEALTH:    dcHeader(dct(DCS_HEALTH));   dcHealthRender(); break;
    case DC_OPTIM:     dcHeader(dct(DCS_OPT));      dcOptimRender();  break;
    case DC_IMUTEST:   dcHeader(dct(DCS_TESTMODULE)); dcImuTestRender(); break;
    case DC_DIAG:      dcHeader(dct(DCS_DIAG));     dcDiagRender();   break;
    case DC_POST:      dcHeader(dct(DCS_EVPOST));   dcDiagRender();   break;
    case DC_SCRTEST:   dcScrTestRender();                             break;
    case DC_TOUCHTEST: dcHeader(dct(DCS_DIAG));     dcTouchTestRender(); break;
    case DC_RESULT:    dcHeader(dct(DCS_DIAG));     dcResultRender(); break;
    default: break;
  }
}

static void dcGoto(int screen){
  dcScreen = screen;
  dcScroll = 0;
  dcDrag = false; dcTouching = false;
  dcAnimT0 = millis();
  dcAnimMs = 0;
  dcFallShown = -1;
  touchDropAll();
  // Entrar en Deteccion de caidas SONDEA el modulo. Es el unico sitio
  // del sistema que lo hace, y es lo que pide el flujo: comprobar el
  // GY-BNO085 justo cuando el usuario abre la funcion.
  if(screen == DC_FALL && !dcSensorOn) dcSensorStart();
  dcRender();
}

// Desplazamiento vertical de las pantallas que lo necesitan.
static bool dcScrollable(){ return dcScreen == DC_HIST; }
static int  dcMaxScroll(){
  if(!dcScrollable()) return 0;
  int bx, by, bw, bh; uiBox(bx, by, bw, bh);
  int m = dcHistContentH() - bh;
  return m > 0 ? m : 0;
}
static void dcScrollTouch(){
  if(!dcScrollable()) return;
  int bx, by, bw, bh; uiBox(bx, by, bw, bh);
  if(T.pressed && T.y >= by && T.y <= by + bh - 1){
    dcTouching = true; dcDrag = false;
    dcDragY0 = (float)T.y; dcDragS0 = (float)dcScroll;
  }
  if(T.down && dcTouching){
    float dy = dcDragY0 - (float)T.y;
    if(!dcDrag && fabsf(dy) > 8) dcDrag = true;
    if(dcDrag){
      int s = (int)(dcDragS0 + dy);
      int mx = dcMaxScroll();
      if(s < 0) s = 0;
      if(s > mx) s = mx;
      if(s != dcScroll){ dcScroll = s; dcRender(); }
      T.tap = false;
    }
  }
  if(!T.down){
    if(dcDrag) T.tap = false;
    dcTouching = false; dcDrag = false;
  }
}

// ---- Puntos de entrada de APP_REG ----
static void dcEnter(){
  dcHistLoad();
  if(dcPendingPost){                     // llegada desde el aviso de caida
    dcPendingPost = false;
    dcDiagEnter(true);
    return;
  }
  if(!gRelayout){
    dcScreen = DC_HOME;
    dcScroll = 0;
  }
  dcAnimT0 = millis();
  dcAnimMs = 0;
  dcFallShown = -1;
  dcHeader(dcScreen == DC_HOME ? dct(DCS_APPTITLE) : dct(DCS_STATUS));
  dcRender();
}

static bool dcBackScreen(){
  if(dcScreen == DC_HOME) return false;
  // Las pruebas y el resultado vuelven a donde se lanzaron; el resto,
  // al inicio de la app.
  if(dcScreen == DC_SCRTEST || dcScreen == DC_TOUCHTEST || dcScreen == DC_RESULT){
    dcGoto(dcPrev == DC_POST ? DC_POST : DC_HOME);
    return true;
  }
  if(dcScreen == DC_IMUTEST){ dcGoto(DC_FALL); return true; }
  dcGoto(DC_HOME);
  return true;
}

static void dcSuspend(){
  dcHistSave();
  // El sensor NO se apaga al pasar a segundo plano: la deteccion de
  // caidas tiene que seguir viva con la app cerrada, que es justo su
  // razon de ser. Lo que se apaga es el dibujo.
}
static void dcResume(){
  if(dcPendingPost){                     // llegada desde el aviso de caida
    dcPendingPost = false;
    dcDiagEnter(true);
    return;
  }
  dcAnimT0 = millis(); dcAnimMs = 0; dcFallShown = -1;
  dcRender();
}
static void dcCloseApp(){
  dcHistSave();
  // Al cerrar la app se conserva la monitorizacion SOLO si el usuario
  // la activo. Si entro a mirar y no la activo, el bus vuelve a ser
  // exclusivamente del tactil.
  if(!dcFallOn) dcSensorStop();
}
static bool dcSaveSess(){ dcHistSave(); return true; }
static void dcLoadSess(){ dcHistLoad(); }
// LISTA BLANCA de segundo plano: con la deteccion activada la app
// tiene trabajo REAL en curso (el detector), asi que ni "Cerrar todo"
// ni el desalojo por memoria deben tumbarla.
static bool dcBgWork(){ return dcFallOn && dcSensorOn; }

static void dcTick(){
  // EL TEST DE PANTALLA VA PRIMERO. Ocupa el panel entero y ahi un toque
  // significa "siguiente patron", incluida la esquina donde en las demas
  // pantallas vive el chevron: si el chevron se atendiera antes, tocar
  // arriba a la izquierda saldria de la prueba en vez de avanzarla. La
  // salida sigue existiendo por la barra de navegacion del sistema, que
  // es suya y se atiende antes que la app.
  if(dcScreen == DC_SCRTEST){ dcScrTestTick(); return; }
  // Cabecera propia: el chevron de "atras" lo atiende la app.
  if(T.tap && T.y <= WIN_TOP && T.x < 72){
    T.tap = false;
    if(!dcBackScreen()) appClose();
    return;
  }
  switch(dcScreen){
    case DC_DIAG: case DC_POST: dcDiagTick();      return;
    case DC_TOUCHTEST:          dcTouchTestTick(); return;
    case DC_RESULT:             dcResultTick();    return;
    case DC_IMUTEST:            dcImuTestTick();   return;
    case DC_HEALTH:             dcHealthTick();    return;
    case DC_OPTIM:              dcOptimTick();     return;
    default: break;
  }

  dcScrollTouch();
  if(T.tap){
    int id = dcHitTest(T.x, T.y);
    T.tap = false;
    switch(id){
      case DCH_HERO:  dcGoto(DC_STATUS); return;
      case DCH_T0:    dcGoto(DC_STATUS); return;
      case DCH_T1:    dcPrev = DC_HOME; dcDiagEnter(false); return;
      case DCH_T2:    dcGoto(DC_FALL);   return;
      case DCH_T3:    dcGoto(DC_HEALTH); return;
      case DCH_T4:    dcGoto(DC_OPTIM);  return;
      case DCH_T5:    dcGoto(DC_HIST);   return;
      case DCH_RUNDIAG: dcPrev = DC_HOME; dcDiagEnter(false); return;
      case DCH_FALLOK:  dcGoto(DC_HOME); return;
      case DCH_FALLRETRY: flexBnoRescan(); dcAnimT0 = millis(); dcRenderFall(); return;
      case DCH_FALLTEST: dcGoto(DC_IMUTEST); return;
      case DCH_FALLTOGGLE:
        dcFallOn = !dcFallOn;
        dcSaveFallPref();
        dcApplyFallPref();
        if(dcFallOn && !dcSensorOn) dcSensorStart();
        dcRenderFall();
        return;
      default: break;
    }
  }

  // Animaciones por pantalla. Todas son funcion del tiempo y repintan
  // SOLO su banda.
  if(dcScreen == DC_HOME) dcHomeAnimTick();
  else if(dcScreen == DC_FALL) dcFallAnimTick();
}
