// #############################################################
// ##  FlexOS_OTA_Bridge.h  ·  PUENTE OTA <-> FLEXOS
// ##  ------------------------------------------------------
// ##  QUE ES Y POR QUE EXISTE
// ##  ----------------------
// ##  Las primitivas graficas de FlexOS (fillRect, drawText,
// ##  present, setBuf...) son `static` DENTRO de cada .ino. Eso
// ##  las hace invisibles para FlexOS_OTA.cpp, que el compilador
// ##  trata como una unidad de traduccion aparte.
// ##
// ##  Este fichero es la unica pieza que conoce a la vez los dos
// ##  mundos. Se incluye UNA sola vez, casi al final del .ino
// ##  (cuando todas las primitivas ya estan definidas), y ahi
// ##  implementa en una linea cada funcion otaHost* que el modulo
// ##  declaro en FlexOS_OTA.h.
// ##
// ##  ES EL MISMO FICHERO PARA LAS TRES PLACAS. No hay una copia
// ##  por plataforma: lo poco que difiere de verdad -como se
// ##  recupera el fondo del escritorio, que en Ultra/S3 sale de la
// ##  cache homeBuf y en Pro se repinta porque no hay PSRAM para
// ##  esa cache- se resuelve con #ifdef aqui dentro.
// ##
// ##  COMO SE USA (2 lineas en el .ino):
// ##      arriba, con los demas includes :  #include "FlexOS_OTA.h"
// ##      justo antes de setup()         :  #include "FlexOS_OTA_Bridge.h"
// #############################################################

#ifndef FLEXOS_OTA_BRIDGE_H
#define FLEXOS_OTA_BRIDGE_H

#include "FlexOS_OTA.h"

// =============================================================
//  CONTEXTO
// =============================================================
int  otaHostScrW(){ return SCR_W; }
int  otaHostScrH(){ return SCR_H; }

// "El escritorio esta visible y libre". Misma condicion exacta que usa
// la isla de notificaciones para permitirse dibujar: Home, sin cortina
// desplegada y sin Modo Edicion. Fuera de ahi, el fondo no se puede
// restaurar banda a banda con garantias.
bool otaHostIsHome(){ return gState == ST_HOME && qsPanelY == 0 && !editMode; }

// No usar WiFi.status() desde el autocheck: en P4 esa consulta despierta
// esp-hosted/SDIO. El sketch publica el enlace real cuando la tarea de
// conexion termina.
bool otaHostNetworkReady(){ return gNetOnline; }

// APARIENCIA y MATERIAL del sistema. Este par es el UNICO punto por el que el
// modulo OTA (compartido por Ultra, Ultra S3 y Pro) conoce el tema: no ve la
// paleta semantica del sketch -- que es `static` y vive dentro del .ino-- sino
// las dos preferencias, y resuelve sus propios colores claros/oscuros a partir
// de ellas (cBg/cCard/cTxtHi... en FlexOS_OTA.cpp).
// Por eso las pantallas de OTA ya siguen el tema: al cambiar gDark o uiGlass,
// la siguiente vez que el modulo pinta, pinta con la apariencia nueva.
bool otaHostDark() { return gDark;   }
bool otaHostGlass(){ return uiGlass; }

// =============================================================
//  EXCLUSION PANEL <-> FLASH
//  -------------------------------------------------------------
//  flxFbMux es el mismo mutex que el presenter toma alrededor de
//  esp_lcd_panel_draw_bitmap() + la espera de fin de DMA. Prestarselo
//  a la tarea OTA para que envuelva Update.write() garantiza que una
//  escritura en flash (que apaga la cache de ambos nucleos) nunca
//  coincida con una subida al panel en vuelo -> se acabo el destello
//  cian. No hay riesgo de interbloqueo: ninguno de los dos lados lo
//  toma de forma anidada, y es un mutex con herencia de prioridad.
// =============================================================
void otaHostLockPanel()  { fbLock();   }
void otaHostUnlockPanel(){ fbUnlock(); }

// =============================================================
//  COMPOSICION
//  -------------------------------------------------------------
//  El OTA compone SIEMPRE en bbuf y publica la banda ya terminada
//  con present(). Nunca escribe en el framebuffer de una app: en
//  Modo PC las apps se desvian a su propio lienzo con gRtTarget, y
//  este camino (bbuf -> present) es justo el que no lo toca.
// =============================================================
void otaHostBeginCompose(){
  setBuf(bbuf);
  gClipY0 = 0; gClipY1 = SCR_H - 1;
  gClipX0 = 0; gClipX1 = SCR_W - 1;
}

void otaHostEndCompose(int y0, int y1){
  present(y0, y1);
  setBuf(fb);
}

// Devuelve limpio el tramo [y0,y1] del escritorio en el lienzo de
// composicion, para que la tarjeta flotante pueda animarse encima sin
// arrastrar restos del cuadro anterior.
void otaHostRestoreBand(int y0, int y1){
  if(y0 < 0) y0 = 0;
  if(y1 > SCR_H - 1) y1 = SCR_H - 1;
  if(y0 > y1) return;
#if FLEXOS_OTA_PLAT_PRO
  // ESP32 clasico: sin PSRAM no existe la cache homeBuf, asi que la
  // banda se vuelve a pintar. Es exactamente la misma ruta que ya usa
  // notifRestoreBg() en esta placa.
  renderHomeBand(y0, y1);
#else
  // Ultra / Ultra S3: el escritorio ya esta pre-renderizado en PSRAM.
  // Una copia de filas completas es mucho mas barata que repintarlo.
  if(homeBuf && bbuf)
    memcpy(bbuf + (size_t)y0 * SCR_W, homeBuf + (size_t)y0 * SCR_W,
           (size_t)SCR_W * (size_t)(y1 - y0 + 1) * 2);
#endif
}

// Repintado completo al cerrar una capa a pantalla completa: devuelve
// la pantalla que habia debajo. Solo se llama al cerrar (no por cuadro),
// asi que su coste es irrelevante.
void otaHostRepaint(){
  if(gState == ST_APP && gAppId == IC_AJUSTES){ settingsRender(); return; }  // volvemos a Ajustes
  if(gState == ST_HOME){
#if FLEXOS_OTA_PLAT_PRO
    showHome();                    // en Pro showHome() ya repinta (no hay cache)
#else
    renderHome(); showHome();      // refresca homeBuf y lo publica
#endif
    return;
  }
  flxFlushAll();                   // ultimo recurso: republica lo que haya en fb
}

// =============================================================
//  PRIMITIVAS DE DIBUJO  (una linea cada una)
// =============================================================
uint16_t otaHostRGB(uint8_t r, uint8_t g, uint8_t b){ return rgb565(r, g, b); }

void otaHostFillRect(int x, int y, int w, int h, uint16_t c){ fillRect(x, y, w, h, c); }
void otaHostFillRoundRect(int x, int y, int w, int h, int r, uint16_t c){ fillRoundRect(x, y, w, h, r, c); }
void otaHostFillRoundRectA(int x, int y, int w, int h, int r, uint16_t c, uint8_t a){ fillRoundRectA(x, y, w, h, r, c, a); }
void otaHostDrawRoundRect(int x, int y, int w, int h, int r, uint16_t c){ drawRoundRect(x, y, w, h, r, c); }
void otaHostFillCircle(int cx, int cy, int r, uint16_t c){ fillCircle(cx, cy, r, c); }
void otaHostStroke(float x0, float y0, float x1, float y1, float w, uint16_t c){ strokeSegAA(x0, y0, x1, y1, w, c); }
void otaHostGlassPanel(int x, int y, int w, int h, int rad, uint16_t tint){ drawLiquidGlassPanel(x, y, w, h, rad, tint); }

void otaHostText(int x, int y, const char* s, int size, uint16_t c){ drawText(x, y, s, size, c); }
void otaHostTextC(int cx, int y, const char* s, int size, uint16_t c){ drawTextC(cx, y, s, size, c); }
void otaHostTextR(int rx, int y, const char* s, int size, uint16_t c){ drawTextR(rx, y, s, size, c); }
void otaHostTextClip(int x, int y, const char* s, int size, uint16_t c, int maxRight){ drawTextClip(x, y, s, size, c, maxRight); }
int  otaHostTextW(const char* s, int size){ return textW(s, size); }

void otaHostClip(int y0, int y1){
  if(y0 < 0) y0 = 0;
  if(y1 > SCR_H - 1) y1 = SCR_H - 1;
  gClipY0 = y0; gClipY1 = y1;
}

// =============================================================
//  ENGANCHE DEL TACTIL
//  -------------------------------------------------------------
//  Traduce struct Touch <-> FlexOtaTouch y devuelve a T los flags
//  que el OTA haya consumido, de modo que la pantalla de debajo no
//  llegue a verlos. IGUAL que notifHandleTouch(): NUNCA se escribe
//  T.down -- ese lo gobierna flexPollTouch() y tocarlo corromperia
//  la maquina de estados del tactil.
// =============================================================
static inline void flexOtaTouchBridge(){
  if(!flexOtaOverlayActive()) return;
  FlexOtaTouch ot = { T.down, T.pressed, T.released, T.tap, T.moved,
                      T.swipeUp, T.swipeDown, T.swipeLeft, T.swipeRight,
                      T.x, T.y, T.startX, T.startY, T.dx, T.dy };
  if(flexOtaHandleTouch(&ot)){
    T.pressed   = ot.pressed;   T.released   = ot.released;
    T.tap       = ot.tap;       T.moved      = ot.moved;
    T.swipeUp   = ot.swipeUp;   T.swipeDown  = ot.swipeDown;
    T.swipeLeft = ot.swipeLeft; T.swipeRight = ot.swipeRight;
  }
}

#endif // FLEXOS_OTA_BRIDGE_H
