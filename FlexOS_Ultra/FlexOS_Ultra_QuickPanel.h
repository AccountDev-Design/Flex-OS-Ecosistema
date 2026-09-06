// #############################################################
// ##  FLEX OS ULTRA  ·  PANEL RAPIDO  ·  modelo, catalogo y render
// ##  ----------------------------------------------------------
// ##  La cortina estilo One UI: catalogo de controles (QsCtl), disposicion
// ##  guardada (QpItem), cabecera, capsulas, brillo y volumen.
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
#include "FlexOS_Ultra_DeXInput.h"   // eslabon anterior de la cadena

// #############################################################
// ##  PANEL RAPIDO  ·  REDISENO ESTILO ONE UI 8.5
// ##  ------------------------------------------------------
// ##  480x800 vertical, motor grafico propio de Flex OS,
// ##  framebuffers en PSRAM y repintado por bandas sucias.
// ##
// ##  ESTRUCTURA (la del video de referencia, adaptada):
// ##    · Cabecera FIJA: hora grande, fecha, estado real de red,
// ##      lapiz (editar), apagado y engranaje.
// ##    · Contenido con SCROLL vertical:
// ##        - capsulas de conectividad (2x1),
// ##        - tarjeta expandible con la cuadricula de circulos
// ##          (4 columnas) y su asa inferior,
// ##        - modulos anchos (slider de brillo, 4x1),
// ##        - modulos de dos columnas (2x1) para acciones.
// ##    · Modo EDICION propio (mover, quitar, redimensionar,
// ##      cambiar orientacion, restablecer, Cancelar / Listo).
// ##    · Catalogo "Anadir un control" a pantalla completa.
// ##
// ##  REGLA QUE MANDA SOBRE EL DISENO: **NADA FALSO**. El
// ##  registro QS_REG solo contiene controles cuya accion la
// ##  ejecuta codigo REAL de este mismo firmware (ver la columna
// ##  'avail' de cada entrada). Por eso NO existen Bluetooth en
// ##  el P4 (SOC_BLE_SUPPORTED = 0 -> qpAvBle() devuelve false),
// ##  volumen (no hay ruta de audio), multimedia (no hay
// ##  reproductor real que acepte ordenes), NFC, Dolby, Alexa,
// ##  llamadas, datos moviles, Smart View, GPS, dispositivos
// ##  cercanos ni Home Control. El video es la referencia de
// ##  COMPOSICION e INTERACCION, no un permiso para inventar
// ##  funciones.
// ##
// ##  MODO PC / DeX HORIZONTAL: la cortina sigue DESACTIVADA a
// ##  proposito (ver qsCanOpen). Su geometria esta escrita para
// ##  480x800 en vertical.
// #############################################################

// ---- ESTADO BASE DE LA CORTINA (contrato con el resto del sistema) ----
// Estos cuatro simbolos los leen o los escriben otras partes del archivo
// (uiTick, loop, autoLockTick, suspWakeLockScreen, enterApp/enterHome...).
// Sus nombres y su semantica NO cambian con este rediseno.
static int  qsPanelY   = 0;      // 0 = oculto, SCR_H = abierto del todo
static bool qsDragging = false;  // hay un gesto de cortina en curso
static bool qsPower    = false;  // Ahorro Ultra (frecuencia REAL de la CPU)

#define QS_EDGE_H        30              // franja del borde superior que captura el gesto
#define QS_OPEN_PCT      40              // al soltar: >=40% del recorrido -> abrir; si no, cerrar
#define QS_SHADOW_H      18              // alto de la sombra bajo el borde movil
#define QS_HANDLE_MARGIN 22              // margen por encima del borde donde vive el asa

// ---- GEOMETRIA DE LA CUADRICULA LOGICA (4 columnas) ----
// Una sola fuente para dibujo y para hit-test: no puede desalinearse lo
// pintado de lo pulsable. 16 + 4*103 + 3*12 + 16 = 480 EXACTOS.
#define QP_MX      16                                   // margen lateral
#define QP_CONT_W  (SCR_W - 2 * QP_MX)                  // 448
#define QP_GAP     12                                   // separacion entre columnas
#define QP_CW      ((QP_CONT_W - 3 * QP_GAP) / 4)       // 103 px por columna
#define qpSpanW(w) ((w) * QP_CW + ((w) - 1) * QP_GAP)   // ancho de un tramo de w columnas
#define qpColX(c)  (QP_MX + (c) * (QP_CW + QP_GAP))     // x de la columna c

#define QP_HDR_H   116                  // cabecera FIJA (hora, fecha, botones)
#define QP_FOOT_H  34                   // franja inferior: asa de cierre de la cortina
#define QP_VIEW_Y0 QP_HDR_H
#define QP_VIEW_Y1 (SCR_H - QP_FOOT_H - 1)
#define QP_VIEW_H  (QP_VIEW_Y1 - QP_VIEW_Y0 + 1)

#define QP_RH1     74                   // alto de un modulo de 1 fila logica
#define QP_RH2     (QP_RH1 * 2 + QP_GAP)// alto de un modulo de 2 filas logicas
#define QP_VGAP    12                   // separacion vertical entre bloques
#define QP_RAD     26                   // radio de las tarjetas grandes
#define QP_RAD_S   20                   // radio de los modulos pequenos

// Tarjeta expandible de controles circulares
#define QP_GPAD    14                   // padding interior de la tarjeta
#define QP_TROW    98                   // alto de una fila de circulos (circulo + 2 lineas)
#define QP_TCIRC   62                   // diametro del circulo de un control 1x1
#define QP_HANDLE_H 22                  // franja del asa inferior de la tarjeta
#define QP_GROWS_MIN 2
#define QP_GROWS_MAX 5
#define qpGroupH(rows) (QP_GPAD + (rows) * QP_TROW + QP_HANDLE_H)

#define QP_TOUCH_MIN 44                 // area tactil minima de cualquier boton importante

// ---- IDENTIFICADORES ESTABLES DE CONTROL ----------------------------
// NUNCA se reordenan ni se reutilizan: van a NVS. Los controles nuevos se
// anaden AL FINAL y suben QP_CFG_VER para que qpAdoptNew() los ofrezca sin
// destruir la configuracion del usuario.
enum {
  QSID_WIFI = 0, QSID_AIRPLANE, QSID_BLE, QSID_BRIGHT, QSID_THEME, QSID_GLASS,
  QSID_POWERSAVE, QSID_SETTINGS, QSID_CONN, QSID_DEX, QSID_VAULT, QSID_OTA,
  QSID_FILES, QSID_RETIRED_13, QSID_CAMERA, QSID_GALLERY, QSID_CRONO, QSID_LOCK,
  QSID_POWEROFF, QSID_NTP,
  // Van al FINAL a proposito: los identificadores anteriores estan
  // guardados en NVS y colar uno en medio reordenaria el panel del
  // usuario. Los dos solo aparecen si el codec de audio contesta de
  // verdad (ver qpAvAudio): sin altavoz no hay control de volumen.
  QSID_VOLUME, QSID_MUTE,
  QSID_COUNT
};

// Tipo de control. Decide COMO se dibuja y como responde al toque.
enum { QT_TOGGLE = 0,   // interruptor con estado real ON/OFF
       QT_ACTION,       // ejecuta una accion / abre una pantalla (NUNCA finge estado)
       QT_SLIDER };     // control continuo (brillo)

// Tamanos permitidos (mascara). 1x1 = circulo dentro de la tarjeta expandible;
// 2x1 = capsula/modulo; 4x1 = modulo ancho (slider); 2x2 = tarjeta grande.
#define QSZ_1x1 0x01
#define QSZ_2x1 0x02
#define QSZ_4x1 0x04
#define QSZ_2x2 0x08

// Orientaciones permitidas de un modulo 2x1 o 2x2 (mascara).
#define QOR_H   0x01   // icono a la izquierda, texto a la derecha (por defecto)
#define QOR_V   0x02   // icono arriba, texto debajo

// Categorias del catalogo
enum { QCAT_CONN = 0, QCAT_SCREEN, QCAT_SYSTEM, QCAT_TOOLS, QCAT_COUNT };
static const char* const QP_CAT_NAME[QCAT_COUNT] = {
  "Conectividad", "Pantalla", "Sistema", "Herramientas"
};

// ---- DECLARACIONES ADELANTADAS ---------------------------------------
// Todo lo que el registro necesita llamar y que vive MAS ABAJO en el
// archivo. Se declara aqui a mano (y no se confia en los prototipos que
// autogenera el IDE) por la misma razon que el resto del fichero.
static bool connWifiOn();
static void connWifiSet(bool on);
static void connWifiSub(char* out, size_t n);
static void connBleSub(char* out, size_t n);
static void connAirplaneSet(bool on);
static void filesEnter();
static void cronoStart();
static void cronoPause();
static void ntpRequestSync(bool userAsked);
static void qsForceClose();
static void qsRestoreBg();
static void qsAnimTo(int target);
static void qpInvalidateAll();
static bool qsTapTile(int px, int py);
static void qsTick();

// ---- PUENTE DE ACCIONES ----------------------------------------------
// Una accion del panel que CAMBIA de pantalla tiene que dejar el sistema
// limpio antes: devolver a la pantalla lo que habia debajo, cerrar la
// cortina (que ademas suelta la captura de la app) y cerrar la app que
// hubiera abierta por su camino normal. Sin esto, abrir Ajustes desde
// encima del Navegador dejaria su tarea de red y sus buffers vivos.
static void qpLeaveToApp(int appId){
  qsRestoreBg();
  qsForceClose();
  if(gState == ST_APP) appClose();
  enterApp(appId);
}
// Varias pantallas del sistema (Conectividad y Flex Vault) salen
// con "gState = ST_APP; settingsRender();": dan por hecho que Ajustes esta
// abierto debajo. Se entra a Ajustes ANTES para que el camino de vuelta sea
// coherente, en vez de dejar al usuario en una app que no habia abierto.
static void qpLeaveToSettingsSub(void (*sub)()){
  qpLeaveToApp(IC_AJUSTES);
  if(sub) sub();
}

// ---- DISPONIBILIDAD REAL ---------------------------------------------
static bool qpAvTrue(){ return true; }
static bool qpAvFalse(){ return false; }
static bool qpAvWifi(){ return FLEXOS_ENABLE_WIFI ? true : false; }
// BLE: lo decide el SDK (soc_caps.h) en tiempo de COMPILACION. En el
// ESP32-P4 SOC_BLE_SUPPORTED no existe -> el control NO aparece en el panel
// ni en el catalogo. No hay interruptor decorativo que cambie de color sin
// controlar radio.
static bool qpAvBle(){ return (FLEXOS_BLE_HW && FLEXOS_ENABLE_BLE) ? true : false; }
static bool qpAvFs(){ return flexFsReady(); }
static bool qpAvPoweroff(){ return POWEROFF_ON ? true : false; }
// "Bloquear" solo se ofrece si hay PIN/contrasena: sin clave configurada no
// bloquea nada y seria un boton que promete lo que no hace.
static bool qpAvLock(){ return (SUSPEND_ON && SUSPEND_LOCK_ON) && gLockType != 0; }
static bool qpAvNtp(){ return FLEXOS_ENABLE_WIFI && !gAirplane; }
static bool qpAvDex(){ return !gHosted; }

// ---- LECTURA DE ESTADO REAL ------------------------------------------
static bool qpStWifi(){ return connWifiOn(); }
static bool qpStAirplane(){ return gAirplane; }
static bool qpStBle(){ return gBleOn; }
static bool qpStTheme(){ return !gDark; }          // ON = tema CLARO
static bool qpStGlass(){ return uiGlass; }
static bool qpStPower(){ return qsPower; }
static bool qpStCrono(){ return gCronoSt == CRONO_RUN; }

// ---- EJECUCION REAL DEL TOQUE ----------------------------------------
static void qpApplyPower(){ setCpuFrequencyMhz(qsPower ? 160 : 360); }
static void qpTapWifi(){
  if(gAirplane) return;                      // bloqueo real, no visual
  connWifiSet(!connWifiOn());
}
static void qpTapAirplane(){ connAirplaneSet(!gAirplane); }
static void qpTapBle(){
#if FLEXOS_ENABLE_BLE
  if(gAirplane) return;
  if(gBleOn) flexBleStop(); else flexBleStart();
#endif
}
static void qpTapTheme(){ gDark = !gDark; themeChanged(); qpInvalidateAll(); }
static void qpTapGlass(){ uiGlass = !uiGlass; themeChanged(); qpInvalidateAll(); }
static void qpTapPower(){ qsPower = !qsPower; qpApplyPower(); }
static void qpTapCrono(){ if(gCronoSt == CRONO_RUN) cronoPause(); else cronoStart(); }
static void qpTapSettings(){ qpLeaveToApp(IC_AJUSTES); }
static void qpTapConn(){ qpLeaveToSettingsSub(connEnter); }
static void qpTapVault(){ qpLeaveToSettingsSub(vaultSettingsEnter); }
static void qpTapOta(){ qsRestoreBg(); qsForceClose(); flexOtaOpenSettings(); }
static void qpTapDex(){ qpLeaveToApp(IC_MODOPC); }
static void qpTapCamera(){ qpLeaveToApp(IC_CAMARA); }
static void qpTapGallery(){ qpLeaveToApp(IC_GALERIA); }
// El explorador sale con "gState = ST_APP; ... almEnter()": se entra antes a
// Almacenamiento para que "atras" devuelva a una pantalla coherente.
static void qpTapFiles(){ qpLeaveToApp(IC_ALMACEN); filesEnter(); }
static void qpTapLock(){ qsRestoreBg(); qsForceClose(); suspEnter(); }
static void qpTapPoweroff(){ qsRestoreBg(); qsForceClose(); poffEnter(); }
static void qpTapNtp(){ ntpRequestSync(true); }
// AUDIO. La disponibilidad NO es "esta placa lleva codec": es que el
// ES8311 haya contestado su identificacion en el bus I2C y que la
// salida I2S haya arrancado. Si no, estos dos controles no existen:
// ni en el panel ni en el catalogo. Un deslizador de volumen que no
// mueve nada es peor que no tener deslizador.
static bool qpAvAudio(){ return flexAudioAvailable(); }
static bool qpStMute(){ return flexAudioMuted(); }
static void qpTapMute(){ flexAudioSetMuted(!flexAudioMuted()); }
static void qpTapVolume(){}                        // el slider se atiende aparte
static void qpSubVolume(char* o, size_t n){ snprintf(o, n, "%d%%", (int)flexAudioVolume()); }
static void qpSubMute(char* o, size_t n){
  snprintf(o, n, "%s", flexAudioMuted() ? "Silenciado" : "Con sonido");
}

static void qpTapBright(){}                        // el slider se atiende aparte

// ---- SUBTITULOS CON DATO REAL ----------------------------------------
static void qpSubWifi(char* o, size_t n){ connWifiSub(o, n); }
static void qpSubBle(char* o, size_t n){ connBleSub(o, n); }
static void qpSubAirplane(char* o, size_t n){ snprintf(o, n, gAirplane ? "Activado" : "Desactivado"); }
static void qpSubTheme(char* o, size_t n){ snprintf(o, n, gDark ? "Oscuro" : "Claro"); }
static void qpSubGlass(char* o, size_t n){ snprintf(o, n, uiGlass ? "Liquid Glass" : "Plano"); }
static void qpSubPower(char* o, size_t n){ snprintf(o, n, qsPower ? "160 MHz" : "360 MHz"); }
static void qpSubBright(char* o, size_t n){ snprintf(o, n, "%d%%", gBright); }
static void qpSubCrono(char* o, size_t n){
  if(gCronoSt == CRONO_RUN)   { snprintf(o, n, "En marcha"); return; }
  if(gCronoSt == CRONO_PAUSE) { snprintf(o, n, "En pausa");  return; }
  snprintf(o, n, "Detenido");
}
static void qpSubOta(char* o, size_t n){
  const char* lv = flexOtaLocalVersion();
  snprintf(o, n, "Versi\xC3\xB3n %s", (lv && lv[0]) ? lv : "?");
}

// ---- ICONOS ----------------------------------------------------------
// Se dibujan con las primitivas propias de Flex OS (no se copia ningun
// recurso grafico ajeno). Firma comun (cx, cy, s, col) con s = lado util,
// para que el mismo glifo sirva en el circulo 1x1, en la capsula y en el
// catalogo sin tener tres versiones.
//
// qpIcoBg = color de la SUPERFICIE sobre la que se esta pintando. Los
// glifos con hueco (luna, engranaje, camara) lo necesitan para "recortar"
// sin inventarse un color fijo que romperia el tema claro.
static uint16_t qpIcoBg = 0;
// Toma el color de recorte del PIXEL REAL ya compuesto. Adivinarlo con una
// mezcla teorica fallaba: sobre vidrio la superficie nunca llega al tinte
// puro, asi que la luna y el engranaje se recortaban con un color que no
// estaba en pantalla y quedaban como manchas oscuras.
static inline void qpIcoBgAt(int cx, int cy){
  if(cx < 0 || cx >= SCR_W || cy < 0 || cy >= SCR_H || !gBuf) return;
  qpIcoBg = gBuf[(size_t)cy * SCR_W + cx];
}

static void qpIcoWifi(int cx, int cy, int s, uint16_t col){
  float r = s * 0.42f;
  arcStroke(cx, cy + s * 0.26f, r,         225, 315, 3, col);
  arcStroke(cx, cy + s * 0.26f, r * 0.66f, 225, 315, 3, col);
  fillCircle(cx, (int)(cy + s * 0.26f), 3, col);
}
static void qpIcoAirplane(int cx, int cy, int s, uint16_t col){
  float u = s * 0.5f;
  // Alas en delta (apex arriba), fuselaje con morro y estabilizador de cola.
  fillTriangle((int)(cx - u), (int)(cy + u * 0.34f), (int)(cx + u), (int)(cy + u * 0.34f),
               cx, (int)(cy - u * 0.16f), col);
  fillRoundRect((int)(cx - u * 0.15f), (int)(cy - u * 0.62f), (int)(u * 0.30f), (int)(u * 1.44f), 2, col);
  fillTriangle((int)(cx - u * 0.15f), (int)(cy - u * 0.56f), (int)(cx + u * 0.15f), (int)(cy - u * 0.56f),
               cx, (int)(cy - u * 0.98f), col);                            // morro
  fillTriangle((int)(cx - u * 0.42f), (int)(cy + u * 0.94f), (int)(cx + u * 0.42f), (int)(cy + u * 0.94f),
               cx, (int)(cy + u * 0.56f), col);                            // cola
}
static void qpIcoBle(int cx, int cy, int s, uint16_t col){
  float u = s * 0.46f;
  strokeSegAA(cx, cy - u, cx, cy + u, 1.7f, col);
  strokeSegAA(cx, cy - u, cx + u * 0.66f, cy - u * 0.42f, 1.7f, col);
  strokeSegAA(cx + u * 0.66f, cy - u * 0.42f, cx - u * 0.62f, cy + u * 0.42f, 1.7f, col);
  strokeSegAA(cx, cy + u, cx + u * 0.66f, cy + u * 0.42f, 1.7f, col);
  strokeSegAA(cx + u * 0.66f, cy + u * 0.42f, cx - u * 0.62f, cy - u * 0.42f, 1.7f, col);
}
// Altavoz con ondas. El numero de ondas sale del volumen REAL, asi
// que el icono dice algo en vez de ser siempre el mismo dibujo.
static void qpIcoSpeaker(int cx, int cy, int s, uint16_t col){
  int r = s / 2;
  fillRect(cx - r / 2, cy - r / 4, r / 3, r / 2, col);                 // cuerpo
  fillTriangle(cx - r / 6, cy, cx + r / 6, cy - r / 2, cx + r / 6, cy + r / 2, col);
  int v = flexAudioMuted() ? 0 : (int)flexAudioVolume();
  if(v > 5)  strokeSegAA(cx + r / 3, cy - r / 5, cx + r / 3, cy + r / 5, 2.0f, col);
  if(v > 45) strokeSegAA(cx + r / 2, cy - r / 3, cx + r / 2, cy + r / 3, 2.0f, col);
}
// Altavoz tachado: el estado de silencio se ve, no se deduce.
static void qpIcoMute(int cx, int cy, int s, uint16_t col){
  int r = s / 2;
  fillRect(cx - r / 2, cy - r / 4, r / 3, r / 2, col);
  fillTriangle(cx - r / 6, cy, cx + r / 6, cy - r / 2, cx + r / 6, cy + r / 2, col);
  strokeSegAA(cx + r / 4, cy - r / 3, cx + r / 2, cy + r / 3, 2.2f, col);
  strokeSegAA(cx + r / 2, cy - r / 3, cx + r / 4, cy + r / 3, 2.2f, col);
}

static void qpIcoSun(int cx, int cy, int s, uint16_t col){
  fillCircleAA(cx, cy, s * 0.21f, col);
  for(int k = 0; k < 8; k++){
    float a = k * 0.7853982f;
    strokeSegAA(cx + cosf(a) * s * 0.32f, cy + sinf(a) * s * 0.32f,
                cx + cosf(a) * s * 0.46f, cy + sinf(a) * s * 0.46f, 1.6f, col);
  }
}
static void qpIcoMoon(int cx, int cy, int s, uint16_t col){
  fillCircleAA(cx, cy, s * 0.44f, col);
  fillCircleAA(cx + s * 0.24f, cy - s * 0.22f, s * 0.40f, qpIcoBg);   // recorte de la media luna
}
static void qpIcoGlass(int cx, int cy, int s, uint16_t col){
  drawRoundRect((int)(cx - s * 0.46f), (int)(cy - s * 0.30f), (int)(s * 0.66f), (int)(s * 0.66f), 6, col);
  drawRoundRect((int)(cx - s * 0.16f), (int)(cy - s * 0.46f), (int)(s * 0.62f), (int)(s * 0.62f), 6, col);
}
static void qpIcoBattSave(int cx, int cy, int s, uint16_t col){
  drawRoundRect((int)(cx - s * 0.42f), (int)(cy - s * 0.28f), (int)(s * 0.76f), (int)(s * 0.56f), 4, col);
  fillRect((int)(cx + s * 0.36f), (int)(cy - s * 0.10f), (int)(s * 0.10f), (int)(s * 0.22f), col);
  fillTriangle((int)(cx - s * 0.06f), (int)(cy - s * 0.20f), (int)(cx + s * 0.14f), (int)(cy - s * 0.20f),
               (int)(cx - s * 0.02f), cy, col);
  fillTriangle((int)(cx - s * 0.02f), cy, (int)(cx + s * 0.18f), cy,
               (int)(cx - s * 0.04f), (int)(cy + s * 0.22f), col);
}
static void qpIcoGear(int cx, int cy, int s, uint16_t col){
  fillCircleAA(cx, cy, s * 0.26f, col);
  for(int k = 0; k < 8; k++){
    float a = k * 0.7853982f;
    fillCircleAA(cx + cosf(a) * s * 0.42f, cy + sinf(a) * s * 0.42f, s * 0.10f, col);
  }
  fillCircleAA(cx, cy, s * 0.11f, qpIcoBg);
}
static void qpIcoSignal(int cx, int cy, int s, uint16_t col){
  for(int k = 0; k < 4; k++){
    int bw = (int)(s * 0.13f), bh = (int)(s * (0.18f + k * 0.16f));
    fillRoundRect((int)(cx - s * 0.44f + k * s * 0.24f), (int)(cy + s * 0.42f - bh), bw, bh, 2, col);
  }
}
static void qpIcoMonitor(int cx, int cy, int s, uint16_t col){
  drawRoundRect((int)(cx - s * 0.46f), (int)(cy - s * 0.40f), (int)(s * 0.92f), (int)(s * 0.62f), 4, col);
  fillRect((int)(cx - s * 0.12f), (int)(cy + s * 0.22f), (int)(s * 0.24f), (int)(s * 0.14f), col);
  fillRect((int)(cx - s * 0.32f), (int)(cy + s * 0.36f), (int)(s * 0.64f), (int)(s * 0.10f), col);
}
static void qpIcoShield(int cx, int cy, int s, uint16_t col){
  float u = s * 0.46f;
  fillTriangle((int)(cx - u), (int)(cy - u * 0.70f), (int)(cx + u), (int)(cy - u * 0.70f), cx, (int)(cy + u), col);
  fillRect((int)(cx - u), (int)(cy - u * 0.80f), (int)(2 * u), (int)(u * 0.42f), col);
  fillCircleAA(cx, cy - s * 0.06f, s * 0.11f, qpIcoBg);
  fillRect((int)(cx - s * 0.04f), (int)(cy - s * 0.06f), (int)(s * 0.09f), (int)(s * 0.20f), qpIcoBg);
}
static void qpIcoUpdate(int cx, int cy, int s, uint16_t col){
  fillRect((int)(cx - s * 0.08f), (int)(cy - s * 0.44f), (int)(s * 0.17f), (int)(s * 0.42f), col);
  fillTriangle((int)(cx - s * 0.26f), (int)(cy - s * 0.06f), (int)(cx + s * 0.26f), (int)(cy - s * 0.06f),
               cx, (int)(cy + s * 0.24f), col);
  fillRoundRect((int)(cx - s * 0.34f), (int)(cy + s * 0.32f), (int)(s * 0.68f), (int)(s * 0.11f), 2, col);
}
static void qpIcoFolder(int cx, int cy, int s, uint16_t col){
  fillRoundRect((int)(cx - s * 0.46f), (int)(cy - s * 0.34f), (int)(s * 0.42f), (int)(s * 0.16f), 3, col);
  fillRoundRect((int)(cx - s * 0.46f), (int)(cy - s * 0.24f), (int)(s * 0.92f), (int)(s * 0.60f), 5, col);
  fillRoundRect((int)(cx - s * 0.34f), (int)(cy - s * 0.10f), (int)(s * 0.68f), (int)(s * 0.32f), 3, qpIcoBg);
}
static void qpIcoCamera(int cx, int cy, int s, uint16_t col){
  fillRoundRect((int)(cx - s * 0.16f), (int)(cy - s * 0.44f), (int)(s * 0.32f), (int)(s * 0.12f), 3, col);
  fillRoundRect((int)(cx - s * 0.48f), (int)(cy - s * 0.32f), (int)(s * 0.96f), (int)(s * 0.68f), 6, col);
  fillCircleAA(cx, cy + s * 0.02f, s * 0.21f, qpIcoBg);
  fillCircleAA(cx, cy + s * 0.02f, s * 0.12f, col);
}
static void qpIcoImage(int cx, int cy, int s, uint16_t col){
  drawRoundRect((int)(cx - s * 0.46f), (int)(cy - s * 0.36f), (int)(s * 0.92f), (int)(s * 0.72f), 5, col);
  fillCircleAA(cx - s * 0.20f, cy - s * 0.14f, s * 0.09f, col);
  fillTriangle((int)(cx - s * 0.36f), (int)(cy + s * 0.30f), (int)(cx - s * 0.02f), (int)(cy + s * 0.30f),
               (int)(cx - s * 0.19f), (int)(cy + s * 0.02f), col);
  fillTriangle((int)(cx - s * 0.16f), (int)(cy + s * 0.30f), (int)(cx + s * 0.40f), (int)(cy + s * 0.30f),
               (int)(cx + s * 0.12f), (int)(cy - s * 0.06f), col);
}
static void qpIcoStopwatch(int cx, int cy, int s, uint16_t col){
  arcStroke(cx, cy + s * 0.06f, s * 0.38f, 0, 360, 3, col);
  fillRect((int)(cx - s * 0.13f), (int)(cy - s * 0.48f), (int)(s * 0.26f), (int)(s * 0.10f), col);
  strokeSegAA(cx, cy + s * 0.06f, cx + s * 0.20f, cy - s * 0.14f, 1.6f, col);
}
static void qpIcoLock(int cx, int cy, int s, uint16_t col){
  arcStroke(cx, cy - s * 0.10f, s * 0.26f, 180, 360, 3, col);
  fillRoundRect((int)(cx - s * 0.36f), (int)(cy - s * 0.10f), (int)(s * 0.72f), (int)(s * 0.52f), 5, col);
  fillCircleAA(cx, cy + s * 0.14f, s * 0.08f, qpIcoBg);
}
static void qpIcoPower(int cx, int cy, int s, uint16_t col){
  // arcStroke toma GRADOS; 270 apunta hacia ARRIBA (la Y crece hacia abajo),
  // asi que el hueco 248..292 queda centrado justo donde entra la barra.
  arcStroke(cx, cy + s * 0.04f, s * 0.36f, -68, 248, 3, col);
  fillRect((int)(cx - s * 0.05f), (int)(cy - s * 0.42f), (int)(s * 0.11f), (int)(s * 0.40f), col);
}
static void qpIcoClock(int cx, int cy, int s, uint16_t col){
  arcStroke(cx, cy, s * 0.42f, 0, 360, 3, col);
  strokeSegAA(cx, cy, cx, cy - s * 0.26f, 1.6f, col);
  strokeSegAA(cx, cy, cx + s * 0.20f, cy + s * 0.06f, 1.6f, col);
}
static void qpIcoPencil(int cx, int cy, int s, uint16_t col){
  strokeSegAA(cx - s * 0.30f, cy + s * 0.30f, cx + s * 0.28f, cy - s * 0.28f, 2.4f, col);
  fillTriangle((int)(cx - s * 0.42f), (int)(cy + s * 0.42f), (int)(cx - s * 0.34f), (int)(cy + s * 0.14f),
               (int)(cx - s * 0.14f), (int)(cy + s * 0.34f), col);
}
static void qpIcoPlus(int cx, int cy, int s, uint16_t col){
  fillRoundRect((int)(cx - s * 0.36f), (int)(cy - s * 0.07f), (int)(s * 0.72f), (int)(s * 0.14f), 2, col);
  fillRoundRect((int)(cx - s * 0.07f), (int)(cy - s * 0.36f), (int)(s * 0.14f), (int)(s * 0.72f), 2, col);
}
static void qpIcoMinus(int cx, int cy, int s, uint16_t col){
  fillRoundRect((int)(cx - s * 0.34f), (int)(cy - s * 0.07f), (int)(s * 0.68f), (int)(s * 0.14f), 2, col);
}

// ---- REGISTRO CENTRAL DE CONTROLES -----------------------------------
// UNA sola tabla en flash. Nada de decenas de "if" repartidos por el
// archivo: dibujo, hit-test, catalogo, editor y persistencia leen todos de
// aqui. Sin String ni asignaciones de heap.
//
//   id      -> identificador ESTABLE (va a NVS)
//   name    -> etiqueta corta (circulo 1x1)
//   title   -> etiqueta larga (capsula / modulo)
//   type    -> QT_TOGGLE / QT_ACTION / QT_SLIDER
//   sizes   -> mascara de tamanos permitidos
//   oris    -> mascara de orientaciones permitidas
//   cat     -> categoria del catalogo
//   avail   -> DISPONIBILIDAD REAL (hardware/SDK/estado). false = ni se
//              dibuja ni aparece en el catalogo.
//   state   -> lectura del estado REAL (NULL en las acciones: una accion
//              NUNCA finge ON/OFF)
//   tap     -> ejecucion real del toque
//   detail  -> accion secundaria (pulsacion larga): abre la pantalla real
//   sub     -> subtitulo con dato real
//   icon    -> glifo vectorial

static const QsCtl QS_REG[QSID_COUNT] = {
  // id             name          title                     tipo       tamanos                    oris          cat
  { QSID_WIFI,      "Wi-Fi",      "Wi-Fi",                  QT_TOGGLE, QSZ_1x1|QSZ_2x1,           QOR_H|QOR_V,  QCAT_CONN,
    qpAvWifi,   qpStWifi,     qpTapWifi,     qpTapConn,     qpSubWifi,     qpIcoWifi },
  { QSID_AIRPLANE,  "Modo avi\xC3\xB3n", "Modo avi\xC3\xB3n", QT_TOGGLE, QSZ_1x1|QSZ_2x1,        QOR_H|QOR_V,  QCAT_CONN,
    qpAvTrue,   qpStAirplane, qpTapAirplane, qpTapConn,     qpSubAirplane, qpIcoAirplane },
  { QSID_BLE,       "Bluetooth",  "Bluetooth",              QT_TOGGLE, QSZ_1x1|QSZ_2x1,           QOR_H|QOR_V,  QCAT_CONN,
    qpAvBle,    qpStBle,      qpTapBle,      qpTapConn,     qpSubBle,      qpIcoBle },
  { QSID_BRIGHT,    "Brillo",     "Brillo",                 QT_SLIDER, QSZ_4x1,                   QOR_H,        QCAT_SCREEN,
    qpAvTrue,   NULL,         qpTapBright,   qpTapSettings, qpSubBright,   qpIcoSun },
  { QSID_THEME,     "Tema",       "Modo oscuro",            QT_TOGGLE, QSZ_1x1|QSZ_2x1,           QOR_H|QOR_V,  QCAT_SCREEN,
    qpAvTrue,   qpStTheme,    qpTapTheme,    qpTapSettings, qpSubTheme,    qpIcoMoon },
  { QSID_GLASS,     "Vidrio",     "Liquid Glass",           QT_TOGGLE, QSZ_1x1|QSZ_2x1,           QOR_H|QOR_V,  QCAT_SCREEN,
    qpAvTrue,   qpStGlass,    qpTapGlass,    qpTapSettings, qpSubGlass,    qpIcoGlass },
  { QSID_POWERSAVE, "Ahorro",     "Ahorro Ultra",           QT_TOGGLE, QSZ_1x1|QSZ_2x1,           QOR_H|QOR_V,  QCAT_SYSTEM,
    qpAvTrue,   qpStPower,    qpTapPower,    qpTapSettings, qpSubPower,    qpIcoBattSave },
  { QSID_SETTINGS,  "Ajustes",    "Ajustes",                QT_ACTION, QSZ_1x1|QSZ_2x1,           QOR_H|QOR_V,  QCAT_SYSTEM,
    qpAvTrue,   NULL,         qpTapSettings, NULL,          NULL,          qpIcoGear },
  { QSID_CONN,      "Conexiones", "Conectividad",           QT_ACTION, QSZ_1x1|QSZ_2x1,           QOR_H|QOR_V,  QCAT_CONN,
    qpAvTrue,   NULL,         qpTapConn,     NULL,          NULL,          qpIcoSignal },
  { QSID_DEX,       "Modo PC",    "Modo PC",                QT_ACTION, QSZ_1x1|QSZ_2x1,           QOR_H|QOR_V,  QCAT_SYSTEM,
    qpAvDex,    NULL,         qpTapDex,      NULL,          NULL,          qpIcoMonitor },
  { QSID_VAULT,     "Vault",      "Flex Vault",             QT_ACTION, QSZ_1x1|QSZ_2x1,           QOR_H|QOR_V,  QCAT_SYSTEM,
    qpAvFs,     NULL,         qpTapVault,    NULL,          NULL,          qpIcoShield },
  { QSID_OTA,       "Actualizar", "Actualizaciones",        QT_ACTION, QSZ_1x1|QSZ_2x1,           QOR_H|QOR_V,  QCAT_SYSTEM,
    qpAvTrue,   NULL,         qpTapOta,      NULL,          qpSubOta,      qpIcoUpdate },
  { QSID_FILES,     "Archivos",   "Archivos",               QT_ACTION, QSZ_1x1|QSZ_2x1,           QOR_H|QOR_V,  QCAT_TOOLS,
    qpAvFs,     NULL,         qpTapFiles,    NULL,          NULL,          qpIcoFolder },
  { QSID_RETIRED_13, "",          "",                      QT_ACTION, QSZ_1x1,                    QOR_H,        QCAT_TOOLS,
    qpAvFalse,  NULL,         NULL,          NULL,          NULL,          qpIcoGear },
  { QSID_CAMERA,    "C\xC3\xA1mara", "C\xC3\xA1mara",       QT_ACTION, QSZ_1x1|QSZ_2x1,           QOR_H|QOR_V,  QCAT_TOOLS,
    qpAvTrue,   NULL,         qpTapCamera,   NULL,          NULL,          qpIcoCamera },
  { QSID_GALLERY,   "Galer\xC3\xAD" "a", "Galer\xC3\xAD" "a",     QT_ACTION, QSZ_1x1|QSZ_2x1,           QOR_H|QOR_V,  QCAT_TOOLS,
    qpAvTrue,   NULL,         qpTapGallery,  NULL,          NULL,          qpIcoImage },
  { QSID_CRONO,     "Cron\xC3\xB3metro", "Cron\xC3\xB3metro", QT_TOGGLE, QSZ_1x1|QSZ_2x1,         QOR_H|QOR_V,  QCAT_TOOLS,
    qpAvTrue,   qpStCrono,    qpTapCrono,    NULL,          qpSubCrono,    qpIcoStopwatch },
  { QSID_LOCK,      "Bloquear",   "Bloquear ahora",         QT_ACTION, QSZ_1x1|QSZ_2x1,           QOR_H|QOR_V,  QCAT_SYSTEM,
    qpAvLock,   NULL,         qpTapLock,     NULL,          NULL,          qpIcoLock },
  { QSID_POWEROFF,  "Apagar",     "Apagar",                 QT_ACTION, QSZ_1x1|QSZ_2x1,           QOR_H|QOR_V,  QCAT_SYSTEM,
    qpAvPoweroff, NULL,       qpTapPoweroff, NULL,          NULL,          qpIcoPower },
  { QSID_NTP,       "Hora",       "Sincronizar hora",       QT_ACTION, QSZ_1x1|QSZ_2x1,           QOR_H|QOR_V,  QCAT_SYSTEM,
    qpAvNtp,    NULL,         qpTapNtp,      NULL,          NULL,          qpIcoClock },
  // VOLUMEN Y SILENCIO. Mismo patron que el brillo: el deslizador
  // escribe el registro de volumen del DAC del codec y el
  // interruptor silencia de verdad; no cambian solo un icono.
  // Los dos cuelgan de qpAvAudio, asi que si el ES8311 no contesta
  // en el bus, sencillamente NO existen: ni en el panel ni en el
  // catalogo de "Anadir un control".
  //
  // El ORDEN DE ESTE ARRAY ES EL DEL ENUM: QS_REG se indexa por id
  // (ver qpCtl), asi que una entrada nueva va SIEMPRE al final, con
  // su id tambien al final del enum. Colarla en medio desplazaria
  // todos los controles guardados en NVS.
  { QSID_VOLUME,    "Volumen",    "Volumen",                QT_SLIDER, QSZ_4x1,                   QOR_H,        QCAT_SYSTEM,
    qpAvAudio,  NULL,         qpTapVolume,   qpTapSettings, qpSubVolume,   qpIcoSpeaker },
  { QSID_MUTE,      "Silencio",   "Silenciar",              QT_TOGGLE, QSZ_1x1|QSZ_2x1,           QOR_H|QOR_V,  QCAT_SYSTEM,
    qpAvAudio,  qpStMute,     qpTapMute,     qpTapSettings, qpSubMute,     qpIcoMute },
};

// Acceso seguro: un id fuera de rango devuelve NULL en vez de leer basura.
static inline const QsCtl* qpCtl(int id){
  if(id < 0 || id >= QSID_COUNT) return NULL;
  return &QS_REG[id];
}
static inline bool qpCtlAvail(int id){
  const QsCtl* c = qpCtl(id);
  return c && c->avail && c->avail();
}
// Primer tamano permitido por la mascara, en orden 1x1 -> 2x1 -> 4x1 -> 2x2.
static void qpFirstSize(uint8_t mask, uint8_t &w, uint8_t &h){
  if(mask & QSZ_1x1){ w = 1; h = 1; return; }
  if(mask & QSZ_2x1){ w = 2; h = 1; return; }
  if(mask & QSZ_4x1){ w = 4; h = 1; return; }
  if(mask & QSZ_2x2){ w = 2; h = 2; return; }
  w = 1; h = 1;
}
static inline bool qpSizeAllowed(int id, int w, int h){
  const QsCtl* c = qpCtl(id);
  if(!c) return false;
  if(w == 1 && h == 1) return (c->sizes & QSZ_1x1) != 0;
  if(w == 2 && h == 1) return (c->sizes & QSZ_2x1) != 0;
  if(w == 4 && h == 1) return (c->sizes & QSZ_4x1) != 0;
  if(w == 2 && h == 2) return (c->sizes & QSZ_2x2) != 0;
  return false;
}
// Siguiente tamano valido al arrastrar el asa derecha en el editor.
// Devuelve false si el control no admite ningun otro: el editor rechaza el
// gesto en vez de dejar un tamano incompatible.
static bool qpNextSize(int id, int w, int h, int dir, uint8_t &nw, uint8_t &nh){
  static const uint8_t SW[4] = { 1, 2, 4, 2 };
  static const uint8_t SH[4] = { 1, 1, 1, 2 };
  int cur = -1;
  for(int i = 0; i < 4; i++) if(SW[i] == w && SH[i] == h){ cur = i; break; }
  if(cur < 0) cur = 0;
  for(int step = 1; step <= 4; step++){
    int k = cur + dir * step;
    if(k < 0 || k > 3) break;
    if(qpSizeAllowed(id, SW[k], SH[k])){ nw = SW[k]; nh = SH[k]; return true; }
  }
  return false;
}

// ---- MODELO DE DATOS PERSISTIDO --------------------------------------
// Arrays FIJOS y estructura compacta: sin String, sin heap y sin nada que
// se asigne por cuadro. El blob tiene tamano FIJO y se valida entero al
// cargar (igual que homeWgDeserialize).
#define QP_CFG_VER    1
#define QP_MAX_ITEMS  24
#define QP_BLOB_N     (4 + QP_MAX_ITEMS * 5)      // cabecera + 5 bytes por elemento
#define QP_NVS_NS     "flexqs"                    // namespace PROPIO: no colisiona con "flexos"
#define QP_NVS_KEY    "qp1"


static QpItem  qpIt[QP_MAX_ITEMS];       // configuracion VIVA
static uint8_t qpN        = 0;
static uint8_t qpGrows    = 3;           // filas visibles de la tarjeta expandible
static bool    qpLoaded   = false;

static QpItem  qpEdIt[QP_MAX_ITEMS];     // copia TEMPORAL del editor
static uint8_t qpEdN      = 0;
static uint8_t qpEdGrows  = 3;

// Valores de fabrica. Orden = el del video: conectividad arriba, cuadricula
// de circulos, brillo y modulos. Solo entran controles con backend REAL; los
// que no esten disponibles en esta placa se filtran al normalizar.
struct QpDef { uint8_t id, w, h; };
static const QpDef QP_FACTORY[] = {
  // Fila de conectividad: dos capsulas grandes, como en el video.
  { QSID_WIFI,      2, 1 },
  { QSID_AIRPLANE,  2, 1 },
  // Tarjeta expandible: la cuadricula de circulos de 4 columnas.
  { QSID_THEME,     1, 1 },
  { QSID_POWERSAVE, 1, 1 },
  { QSID_GLASS,     1, 1 },
  { QSID_CRONO,     1, 1 },
  { QSID_NTP,       1, 1 },
  { QSID_LOCK,      1, 1 },
  { QSID_VAULT,     1, 1 },
  { QSID_FILES,     1, 1 },
  { QSID_CAMERA,    1, 1 },
  { QSID_GALLERY,   1, 1 },
  { QSID_SETTINGS,  1, 1 },
  // Modulos anchos: los dos sliders reales (PWM del backlight y
  // registro de volumen del codec). Si no hay codec, qpFactory salta
  // la fila de volumen -- no deja un hueco ni un control apagado.
  { QSID_BRIGHT,    4, 1 },
  { QSID_VOLUME,    4, 1 },
  // Modulos inferiores de dos columnas.
  { QSID_DEX,       2, 1 },
  { QSID_OTA,       2, 1 },
  { QSID_CONN,      2, 1 },
};
#define QP_FACTORY_N ((int)(sizeof(QP_FACTORY) / sizeof(QP_FACTORY[0])))

static void qpFactory(){
  qpN = 0;
  for(int i = 0; i < QP_FACTORY_N && qpN < QP_MAX_ITEMS; i++){
    if(!qpCtlAvail(QP_FACTORY[i].id)) continue;           // sin backend real: no entra
    uint8_t w = QP_FACTORY[i].w, h = QP_FACTORY[i].h;
    if(!qpSizeAllowed(QP_FACTORY[i].id, w, h)) qpFirstSize(QS_REG[QP_FACTORY[i].id].sizes, w, h);
    qpIt[qpN].id = QP_FACTORY[i].id; qpIt[qpN].w = w; qpIt[qpN].h = h;
    qpIt[qpN].ori = QOR_H; qpIt[qpN].vis = 1;
    qpN++;
  }
  qpGrows = 3;
}

// NORMALIZACION. Es la unica puerta por la que pasa cualquier configuracion
// -- de fabrica, de NVS o recien editada -- antes de usarse:
//   · elimina ids desconocidos (blob de una version futura o corrupto),
//   · elimina duplicados (deja la primera aparicion),
//   · elimina controles sin disponibilidad real en esta placa,
//   · corrige tamanos incompatibles al primero que el control admita,
//   · corrige orientaciones no permitidas,
//   · acota qpGrows al rango valido.
// Devuelve el numero de elementos que sobrevivieron.
static uint8_t qpNormalize(QpItem* it, uint8_t n, uint8_t &grows){
  bool seen[QSID_COUNT];
  for(int i = 0; i < QSID_COUNT; i++) seen[i] = false;
  uint8_t out = 0;
  for(uint8_t i = 0; i < n && i < QP_MAX_ITEMS; i++){
    int id = it[i].id;
    if(id < 0 || id >= QSID_COUNT) continue;              // id desconocido
    if(seen[id]) continue;                                // duplicado
    if(!qpCtlAvail(id)) continue;                         // sin backend/hardware real
    uint8_t w = it[i].w, h = it[i].h;
    if(!qpSizeAllowed(id, w, h)) qpFirstSize(QS_REG[id].sizes, w, h);
    uint8_t ori = it[i].ori ? it[i].ori : QOR_H;
    if(!(QS_REG[id].oris & ori)) ori = (QS_REG[id].oris & QOR_H) ? QOR_H : QOR_V;
    seen[id] = true;
    it[out].id = (uint8_t)id; it[out].w = w; it[out].h = h;
    it[out].ori = ori; it[out].vis = it[i].vis ? 1 : 0;
    out++;
  }
  if(grows < QP_GROWS_MIN) grows = QP_GROWS_MIN;
  if(grows > QP_GROWS_MAX) grows = QP_GROWS_MAX;
  return out;
}

// ADOPCION DE CONTROLES NUEVOS. Al subir QP_CFG_VER, los controles que la
// version antigua no conocia se anaden al final SIN tocar lo que el usuario
// ya habia colocado. Con fromVer == QP_CFG_VER no hace nada.
static void qpAdoptNew(uint8_t fromVer){
  if(fromVer >= QP_CFG_VER) return;
  bool have[QSID_COUNT];
  for(int i = 0; i < QSID_COUNT; i++) have[i] = false;
  for(uint8_t i = 0; i < qpN; i++) have[qpIt[i].id] = true;
  for(int i = 0; i < QP_FACTORY_N && qpN < QP_MAX_ITEMS; i++){
    int id = QP_FACTORY[i].id;
    if(have[id] || !qpCtlAvail(id)) continue;
    uint8_t w = QP_FACTORY[i].w, h = QP_FACTORY[i].h;
    if(!qpSizeAllowed(id, w, h)) qpFirstSize(QS_REG[id].sizes, w, h);
    qpIt[qpN].id = (uint8_t)id; qpIt[qpN].w = w; qpIt[qpN].h = h;
    qpIt[qpN].ori = QOR_H; qpIt[qpN].vis = 1; qpN++;
    have[id] = true;
  }
}

static void qpSerialize(uint8_t* b){
  memset(b, 0, QP_BLOB_N);
  b[0] = 'Q'; b[1] = QP_CFG_VER; b[2] = qpN; b[3] = qpGrows;
  int o = 4;
  for(int i = 0; i < QP_MAX_ITEMS; i++){
    b[o++] = qpIt[i].id; b[o++] = qpIt[i].w; b[o++] = qpIt[i].h;
    b[o++] = qpIt[i].ori; b[o++] = qpIt[i].vis;
  }
}
// Un blob que no cuadre se DESCARTA entero y se cae a fabrica: mejor un panel
// de fabrica que medio panel colocado en celdas que no existen.
static bool qpDeserialize(const uint8_t* b){
  if(b[0] != 'Q') return false;
  uint8_t ver = b[1], n = b[2], gr = b[3];
  if(ver == 0 || ver > QP_CFG_VER) return false;          // version futura: no se adivina
  if(n > QP_MAX_ITEMS) return false;
  QpItem tmp[QP_MAX_ITEMS];
  int o = 4;
  for(int i = 0; i < QP_MAX_ITEMS; i++){
    tmp[i].id = b[o++]; tmp[i].w = b[o++]; tmp[i].h = b[o++];
    tmp[i].ori = b[o++]; tmp[i].vis = b[o++];
  }
  memcpy(qpIt, tmp, sizeof(qpIt));
  qpN = n; qpGrows = gr;
  qpN = qpNormalize(qpIt, qpN, qpGrows);
  qpAdoptNew(ver);                                        // migracion hacia adelante
  qpN = qpNormalize(qpIt, qpN, qpGrows);
  return qpN > 0;
}

// NVS. Namespace propio ("flexqs"), asi que ni pisa ni lo pisan las claves de
// "flexos". Solo se escribe al confirmar con "Listo" o al restablecer: NUNCA
// por cuadro ni por movimiento.
static void qpSave(){
  uint8_t b[QP_BLOB_N];
  qpSerialize(b);
  prefs.begin(QP_NVS_NS, false);
  prefs.putBytes(QP_NVS_KEY, b, QP_BLOB_N);
  prefs.end();
}
static void qpLoad(){
  if(qpLoaded) return;
  qpLoaded = true;
  uint8_t b[QP_BLOB_N];
  size_t rd = 0;
  prefs.begin(QP_NVS_NS, true);
  rd = prefs.getBytes(QP_NVS_KEY, b, QP_BLOB_N);
  prefs.end();
  if(rd != QP_BLOB_N || !qpDeserialize(b)){
    Serial.println(F("[QP] configuracion ausente o invalida: valores de fabrica"));
    qpFactory();
    qpN = qpNormalize(qpIt, qpN, qpGrows);
  }
  if(qpN == 0){ qpFactory(); qpN = qpNormalize(qpIt, qpN, qpGrows); }
}

// ---- MOTOR DE MAQUETACION --------------------------------------------
// Cuadricula LOGICA de 4 columnas con empaquetado por flujo. El flujo es lo
// que garantiza por CONSTRUCCION las tres invariantes que pide el editor:
// ningun solape, ningun control fuera de pantalla y ningun hueco imposible.
//   · los elementos de 1 columna (1x1) NO viven en el flujo: se recogen en
//     la tarjeta expandible de circulos, igual que en One UI,
//   · la tarjeta se emite en la posicion de flujo del PRIMER 1x1 de la
//     lista, asi que moverlo en el editor mueve la tarjeta entera,
//   · el resto (2x1, 4x1, 2x2) se empaqueta por filas de 4 columnas.
enum { QB_ITEM = 0, QB_GROUP, QB_ADD };
struct QpBlock { uint8_t kind; int8_t item; int16_t x, y, w, h; };

static QpBlock qpBlk[QP_MAX_ITEMS + 2];
static int     qpBlkN     = 0;
static uint8_t qpTiles[QP_MAX_ITEMS];      // indices (en la config) de los 1x1, en orden
static int     qpTileN    = 0;
static int     qpContentH = 0;             // alto total del contenido desplazable
static int     qpGroupBlk = -1;            // indice del bloque de la tarjeta, -1 si no hay

// Entrada de la maquetacion (globales, no parametros: el .ino inserta los
// prototipos autogenerados al principio del fichero y un struct como
// parametro obligaria a subir el tipo al encabezado del archivo).
static QpItem* qpLaySrc   = NULL;
static int     qpLayN     = 0;
static int     qpLayGroupPx = 0;           // alto EN PIXELES de la tarjeta expandible
static bool    qpLayEdit  = false;

static inline int qpTotalRows(){ return (qpTileN + 3) / 4; }
static inline int qpGroupInnerH(int groupPx){
  int h = groupPx - QP_GPAD - QP_HANDLE_H;
  return h > 0 ? h : 0;
}
static inline int qpGroupMinPx(){ return qpGroupH(QP_GROWS_MIN); }
static inline int qpGroupMaxPx(){
  int rows = qpTotalRows();
  if(rows < QP_GROWS_MIN) rows = QP_GROWS_MIN;
  if(rows > QP_GROWS_MAX) rows = QP_GROWS_MAX;
  return qpGroupH(rows);
}

static void qpLayout(){
  qpBlkN = 0; qpTileN = 0; qpGroupBlk = -1;
  if(!qpLaySrc){ qpContentH = 0; return; }

  for(int i = 0; i < qpLayN && i < QP_MAX_ITEMS; i++){
    const QpItem* it = &qpLaySrc[i];
    if(!it->vis || !qpCtlAvail(it->id)) continue;
    if(it->w == 1 && qpTileN < QP_MAX_ITEMS) qpTiles[qpTileN++] = (uint8_t)i;
  }

  int y = 0, col = 0, rowH = 0;
  bool groupDone = (qpTileN == 0);
  for(int i = 0; i < qpLayN && i < QP_MAX_ITEMS && qpBlkN < QP_MAX_ITEMS + 2; i++){
    const QpItem* it = &qpLaySrc[i];
    if(!it->vis || !qpCtlAvail(it->id)) continue;
    if(it->w == 1){
      if(groupDone) continue;
      if(col > 0){ y += rowH + QP_VGAP; col = 0; rowH = 0; }
      qpGroupBlk = qpBlkN;
      qpBlk[qpBlkN].kind = QB_GROUP; qpBlk[qpBlkN].item = (int8_t)i;
      qpBlk[qpBlkN].x = QP_MX; qpBlk[qpBlkN].y = (int16_t)y;
      qpBlk[qpBlkN].w = QP_CONT_W; qpBlk[qpBlkN].h = (int16_t)qpLayGroupPx;
      qpBlkN++;
      y += qpLayGroupPx + QP_VGAP;
      groupDone = true;
      continue;
    }
    int w = it->w > 4 ? 4 : it->w;
    int bh = (it->h >= 2) ? QP_RH2 : QP_RH1;
    if(col + w > 4){ y += rowH + QP_VGAP; col = 0; rowH = 0; }
    qpBlk[qpBlkN].kind = QB_ITEM; qpBlk[qpBlkN].item = (int8_t)i;
    qpBlk[qpBlkN].x = (int16_t)qpColX(col); qpBlk[qpBlkN].y = (int16_t)y;
    qpBlk[qpBlkN].w = (int16_t)qpSpanW(w);  qpBlk[qpBlkN].h = (int16_t)bh;
    qpBlkN++;
    col += w; if(bh > rowH) rowH = bh;
    if(col >= 4){ y += rowH + QP_VGAP; col = 0; rowH = 0; }
  }
  if(col > 0){ y += rowH + QP_VGAP; col = 0; rowH = 0; }
  // La tarjeta no se pierde nunca: si todos los 1x1 estan detras de los
  // modulos anchos, se emite al final.
  if(!groupDone && qpBlkN < QP_MAX_ITEMS + 2){
    qpGroupBlk = qpBlkN;
    qpBlk[qpBlkN].kind = QB_GROUP; qpBlk[qpBlkN].item = -1;
    qpBlk[qpBlkN].x = QP_MX; qpBlk[qpBlkN].y = (int16_t)y;
    qpBlk[qpBlkN].w = QP_CONT_W; qpBlk[qpBlkN].h = (int16_t)qpLayGroupPx;
    qpBlkN++;
    y += qpLayGroupPx + QP_VGAP;
  }
  if(qpLayEdit && qpBlkN < QP_MAX_ITEMS + 2){
    qpBlk[qpBlkN].kind = QB_ADD; qpBlk[qpBlkN].item = -1;
    qpBlk[qpBlkN].x = QP_MX; qpBlk[qpBlkN].y = (int16_t)y;
    qpBlk[qpBlkN].w = QP_CONT_W; qpBlk[qpBlkN].h = QP_RH1;
    qpBlkN++;
    y += QP_RH1 + QP_VGAP;
  }
  qpContentH = y > 0 ? y - QP_VGAP : 0;
}

// Rect de un circulo 1x1 DENTRO de la tarjeta. La rejilla interior de la
// tarjeta tiene su PROPIO ancho de columna (el de la tarjeta menos su
// padding), no el de la cuadricula exterior: asi los circulos quedan
// centrados dentro de la tarjeta y no pegados a sus bordes.
// gyTop = borde superior del AREA INTERIOR ya desplazada por el scroll.
#define QP_TCOLW ((QP_CONT_W - 2 * QP_GPAD) / 4)
static inline void qpTileCenter(int k, int gyTop, int &cx, int &cy){
  int r = k / 4, c = k % 4;
  cx = QP_MX + QP_GPAD + c * QP_TCOLW + QP_TCOLW / 2;
  cy = gyTop + r * QP_TROW + QP_TCIRC / 2;
}

// ---- ESTADO DE INTERACCION DEL PANEL ---------------------------------
enum { QPM_PANEL = 0, QPM_EDIT, QPM_CAT };
static uint8_t qpMode = QPM_PANEL;

static float qpScrollF   = 0;      // scroll del contenido (px, con rebote)
static float qpScrollVel = 0;      // px/ms
static float qpGH        = 0;      // alto actual de la tarjeta expandible (px)
static float qpGScrollF  = 0;      // scroll INTERNO de la tarjeta
static float qpGScrollVel= 0;
static int   qpFlashKind = -1;     // 0 = circulo de la tarjeta, 1 = bloque exterior
static int   qpFlashIdx  = -1;
static uint32_t qpFlashMs = 0;
#define QP_FLASH_MS 200

// Animaciones por RELOJ (nunca pasos fijos por cuadro ni delay()).
static bool     qpGAnim = false;   // "snap" de la tarjeta al soltar el asa
static float    qpGFrom = 0, qpGTo = 0;
static uint32_t qpGT0 = 0, qpGDur = 1;

// ---- EDITOR: ESTADO --------------------------------------------------
static int      qpEdDrag    = -1;      // indice (en qpEdIt) del elemento arrastrado
static int      qpEdDragX   = 0, qpEdDragY = 0;    // centro del fantasma, en pantalla
static int      qpEdResize  = -1;      // indice del elemento cuyo asa derecha se arrastra
static int      qpEdResX0   = 0;
static int      qpEdRejectF = -1;      // >=0: id con destello de "tamano incompatible"
static uint32_t qpEdRejectMs = 0;
static int      qpCatSel    = -1;      // control resaltado en el catalogo
static float    qpCatScrollF = 0;
static uint8_t  qpCatIds[QSID_COUNT];  // controles ofrecidos ahora mismo
static int      qpCatN      = 0;

// ---- PALETA SEMANTICA DEL PANEL --------------------------------------
// Todo sale de la paleta activa: el panel funciona igual en claro y oscuro y
// no introduce ni un color fijo que rompa el modo claro.
static inline uint16_t qpCard(){    return thCard2(); }
static inline uint16_t qpTileOff(){ return mix565(thCard2(), TH_TXT, 34); }
static inline uint16_t qpCapOn(){   return mix565(thCard2(), TH_PRIM, 70); }
