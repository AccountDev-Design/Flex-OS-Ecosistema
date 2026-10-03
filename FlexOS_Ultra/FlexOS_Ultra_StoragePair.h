// #############################################################
// ##  FLEX OS ULTRA  ·  FLEX STORAGE: APROBAR UN TELEFONO
// ##  ----------------------------------------------------------
// ##  Cuando un telefono pide emparejarse (la web de Flex OS le dio una
// ##  oferta de un solo uso y Flex Phone la uso), Flex OS NO lo acepta
// ##  por su cuenta: aparece este cuadro ENCIMA de lo que haya en
// ##  pantalla con el codigo de 6 cifras que el telefono ensena A LA VEZ
// ##  (sale del mismo intercambio ECDH en los dos lados). La persona
// ##  compara los dos codigos y decide. Asi un tercero en la misma Wi-Fi
// ##  no puede colarse en medio: su codigo seria otro.
// ##
// ##  Lo que se aprueba es POCO: que ese telefono sea el destino de Flex
// ##  Cloud (el espacio que el telefono reserva). El telefono no gana
// ##  ningun acceso a Flex OS; es Flex OS quien le pide y le entrega.
// ##
// ##  NUNCA con la pantalla bloqueada (ni en la del robo): aprobar exige
// ##  un aparato desbloqueado. Si llega bloqueado, espera -- dentro de los
// ##  2 minutos que dura el emparejamiento -- y sale al desbloquear.
// ##
// ##  MISMO PATRON QUE EL AVISO DE CAIDA (FlexOS_Ultra_FallAlert.h): se
// ##  arma fuera de la fase de dibujo, captura la banda que va a ocupar,
// ##  compone en bbuf, publica con un solo present() y al cerrarse la
// ##  devuelve pixel a pixel; mientras esta a la vista es el unico que
// ##  dibuja y recibe toques (loop() le cede la pantalla). Si no puede
// ##  dibujar, suelta la pantalla en el acto. La decision vive en
// ##  FlexOS_StorageCore (probado en el PC); aqui solo hay interfaz.
// ##
// ##  COMO ENCAJA ESTE ARCHIVO
// ##  ----------------------------------------------------------
// ##  Es una PARTE del sketch FlexOS_Ultra.ino, no una unidad de
// ##  traduccion independiente. FlexOS_Ultra.ino lo incluye en el
// ##  orden que fija la cadena de cabeceras (cada modulo incluye al
// ##  anterior), asi que todo el sistema sigue compilandose como UN
// ##  SOLO archivo. No lo incluyas por tu cuenta desde otro sitio.
// #############################################################
#pragma once
#include "FlexOS_Ultra_FallAlert.h"   // eslabon anterior de la cadena

enum { SPA_HIDDEN = 0, SPA_ARMED, SPA_IN, SPA_SHOWN, SPA_OUT };
static int       spaState = SPA_HIDDEN;
static uint32_t  spaT0 = 0, spaShownMs = 0;
static bool      spaLand = false;
static bool      spaDirty = false;            // la cuenta atras cambio: recomponer
static FlexStorageInfo spaInfo;               // el emparejamiento que se ensena
// Clave del emparejamiento ya decidido en pantalla (codigo + telefono): el
// nucleo tarda un instante en dejar de publicarlo y no debe volver a salir.
static char      spaDecided[96] = "";
static char      spaTold[96] = "";            // ya se aviso por la isla de que espera
static uint32_t  spaWatchMs = 0;
static uint8_t   spaLastState = 0xFF;         // estado del telefono visto (avisos por flanco)
// Banda capturada (se pide al abrir y se suelta al cerrar, como el aviso de caida).
static uint16_t* spaBak = NULL;
static size_t    spaBakCap = 0;
static int       spaBakY0 = 0, spaBakY1 = -1;
static int16_t   spaBtnOk[4] = {0,0,0,0};     // x0,y0,x1,y1 (coords del dibujo)
static int16_t   spaBtnNo[4] = {0,0,0,0};

#define SPA_ANIM_MS   200
#define SPA_WATCH_MS  250
// Valvula de seguridad: el emparejamiento caduca a los 2 minutos en el nucleo
// (y entonces el cuadro se va solo); si algo lo dejara a la vista mas, se va.
#define SPA_AUTO_MS   (FST_PAIR_TTL_MS + 10000u)

static inline bool spaVisible(){ return spaState != SPA_HIDDEN; }
static void spaFreeBand(){ if(spaBak){ heap_caps_free(spaBak); spaBak = NULL; } spaBakCap = 0; }
static void spaInvalidateBand(){ spaBakY1 = spaBakY0 - 1; }
static inline bool spaBandReady(){ return spaBak && spaBakY1 >= spaBakY0; }

static void spaKey(const FlexStorageInfo& s, char* out, size_t cap){
  snprintf(out, cap, "%s|%s|%s", s.sas, s.pairName, s.pairIp);
}

// ¿Se puede ensenar ahora? Las pantallas en las que el sistema no notifica
// (arranque, OOBE, BLOQUEO, alta de PIN, apagado), las que tienen la pantalla
// en exclusiva, la proteccion contra robo, Modo PC y los otros modales.
static bool spaCanShow(){
  if(notifSecureScreen()) return false;              // nunca con la pantalla bloqueada
  if(gState == ST_THEFT) return false;               // ni con la proteccion contra robo delante
  if(gFrPending || gState == ST_FACTORY || gSafeMode) return false;
  if(flexOtaOwnsScreen() || optActive()) return false;
  if(faDexActive()) return false;                    // Modo PC: se avisa por la isla y se espera
  if(gSuspOn) return false;
  if(appTrOwnsScreen()) return false;
  if(cronoCardVisible() || faVisible()) return false;
  if(qsPanelY != 0 || qsAnimOn) return false;
  return true;
}

// #############################################################
// ##  GEOMETRIA: vertical (botones apilados, al alcance del pulgar)
// ##  y horizontal (dos columnas, botones en fila). Coordenadas
// ##  logicas; en horizontal la x logica es la fila fisica.
// #############################################################
#define SPA_V_X   24
#define SPA_V_W   (SCR_W - 48)
#define SPA_V_Y   112
#define SPA_V_H   440
#define SPA_L_X   80
#define SPA_L_W   (SCR_H - 160)
#define SPA_L_Y   70
#define SPA_L_H   340

static void spaBand(bool land, int &y0, int &y1){
  if(land){ y0 = SPA_L_X - 12; y1 = SPA_L_X + SPA_L_W + 12; }
  else    { y0 = SPA_V_Y - 12; y1 = SPA_V_Y + SPA_V_H + 12; }
  if(y0 < 0) y0 = 0;
  if(y1 > SCR_H - 1) y1 = SCR_H - 1;
}

// Telefono vectorial con el simbolo de enlace (sin imagenes).
static void spaPhoneGlyph(int cx, int cy, int s, uint16_t col, uint16_t on){
  int w = s * 6 / 10, h = s;
  fillRoundRect(cx - w / 2, cy - h / 2, w, h, s / 7, col);
  fillRoundRect(cx - w / 2 + 3, cy - h / 2 + 5, w - 6, h - 14, 3, on);
  fillRect(cx - s / 10, cy + h / 2 - 6, s / 5, 2, on);
}

// "123 456" grande dentro de su caja: lo que se compara con el telefono.
static void spaSasBox(int x, int y, int w, int h){
  fillRoundRect(x, y, w, h, 18, TH_SURF);
  drawRoundRect(x, y, w, h, 18, TH_BORDER);
  char code[8];
  snprintf(code, sizeof(code), "%c%c%c %c%c%c", spaInfo.sas[0], spaInfo.sas[1], spaInfo.sas[2],
           spaInfo.sas[3], spaInfo.sas[4], spaInfo.sas[5]);
  drawTextC(x + w / 2, y + h / 2 - 22, code, 5, TH_TXT);
}

static void spaLeftText(char* out, size_t cap){
  unsigned s = spaInfo.pairLeftS;
  snprintf(out, cap, "Quedan %u:%02u para decidir", s / 60u, s % 60u);
}

static void spaDrawVertical(float p){
  int h = (int)(SPA_V_H * (0.74f + 0.26f * p));
  int y = SPA_V_Y + (SPA_V_H - h) / 2;
  uint8_t a = (uint8_t)(255.0f * p);
  int by0, by1; spaBand(false, by0, by1);
  fillRectA(0, by0, SCR_W, by1 - by0 + 1, TH_SCRIM, (uint8_t)(120 * p));
  uiSurfaceA(SPA_V_X, y, SPA_V_W, h, 28, UIS_ELEVATED, a);
  drawRoundRect(SPA_V_X, y, SPA_V_W, h, 28, TH_BORDER);
  if(p < 0.6f) return;

  int cx = SCR_W / 2, tw = SPA_V_W - 40;
  spaPhoneGlyph(cx, y + 46, 44, TH_PRIM, TH_WIN);
  const char* title = "\xC2\xBF" "Emparejar este tel\xC3\xA9" "fono?";
  drawTextC(cx, y + 80, title, uiFontFit(title, tw, 3), TH_TXT);
  char who[FST_NAME_MAX + 8];
  snprintf(who, sizeof(who), "%s", spaInfo.pairName[0] ? spaInfo.pairName : "Tel\xC3\xA9" "fono");
  drawTextC(cx, y + 118, who, uiFontFit(who, tw, 2), TH_TXT);
  char sub[FST_MODEL_MAX + FST_IP_MAX + 16];
  snprintf(sub, sizeof(sub), "%s%s%s", spaInfo.pairModel, spaInfo.pairModel[0] ? "  \xC2\xB7  " : "", spaInfo.pairIp);
  drawTextC(cx, y + 146, sub, 1, TH_TXT2);
  spaSasBox(SPA_V_X + 56, y + 168, SPA_V_W - 112, 76);
  mmWrap(SPA_V_X + 30, y + 258, SPA_V_W - 60,
         "Comprueba que el tel\xC3\xA9" "fono ense\xC3\xB1" "a el mismo c\xC3\xB3" "digo. Solo ser\xC3\xA1 el destino de Flex Cloud: no ver\xC3\xA1 nada de Flex OS.",
         1, TH_TXT2, true);
  char left[40]; spaLeftText(left, sizeof(left));
  drawTextC(cx, y + 304, left, 1, TH_MUTE);

  int bw = SPA_V_W - 48, bx = SPA_V_X + 24;
  int b1y = y + h - 118, b2y = y + h - 62;
  fillRoundRect(bx, b1y, bw, 48, 24, TH_PRIM);
  drawTextC(bx + bw / 2, b1y + 14, "Emparejar", 2, TH_ONACC);
  fillRoundRect(bx, b2y, bw, 46, 23, TH_SURF);
  drawTextC(bx + bw / 2, b2y + 13, "Rechazar", 2, TH_TXT2);
  spaBtnOk[0] = (int16_t)bx; spaBtnOk[1] = (int16_t)b1y; spaBtnOk[2] = (int16_t)(bx + bw); spaBtnOk[3] = (int16_t)(b1y + 48);
  spaBtnNo[0] = (int16_t)bx; spaBtnNo[1] = (int16_t)b2y; spaBtnNo[2] = (int16_t)(bx + bw); spaBtnNo[3] = (int16_t)(b2y + 46);
}

static void spaDrawLandscape(float p){
  int h = (int)(SPA_L_H * (0.74f + 0.26f * p));
  int y = SPA_L_Y + (SPA_L_H - h) / 2;
  uint8_t a = (uint8_t)(255.0f * p);
  fillRectA(SPA_L_X - 12, 0, SPA_L_W + 24, SCR_W, TH_SCRIM, (uint8_t)(120 * p));
  uiSurfaceA(SPA_L_X, y, SPA_L_W, h, 26, UIS_ELEVATED, a);
  drawRoundRect(SPA_L_X, y, SPA_L_W, h, 26, TH_BORDER);
  if(p < 0.6f) return;

  // Columna izquierda: quien y el codigo. Derecha: la explicacion y el tiempo.
  int lx = SPA_L_X + 28, rx = SPA_L_X + SPA_L_W / 2 + 14, rw = SPA_L_W / 2 - 42;
  const char* title = "\xC2\xBF" "Emparejar este tel\xC3\xA9" "fono?";
  drawText(lx, y + 22, title, uiFontFit(title, SPA_L_W - 56, 3), TH_TXT);
  char who[FST_NAME_MAX + 8];
  snprintf(who, sizeof(who), "%s", spaInfo.pairName[0] ? spaInfo.pairName : "Tel\xC3\xA9" "fono");
  drawTextClip(lx, y + 64, who, 2, TH_TXT, rx - 12);
  char sub[FST_MODEL_MAX + FST_IP_MAX + 16];
  snprintf(sub, sizeof(sub), "%s%s%s", spaInfo.pairModel, spaInfo.pairModel[0] ? "  \xC2\xB7  " : "", spaInfo.pairIp);
  drawTextClip(lx, y + 92, sub, 1, TH_TXT2, rx - 12);
  spaSasBox(lx, y + 116, SPA_L_W / 2 - 42, 76);
  mmWrap(rx, y + 70, rw,
         "Comprueba que el tel\xC3\xA9" "fono ense\xC3\xB1" "a el mismo c\xC3\xB3" "digo. Solo ser\xC3\xA1 el destino de Flex Cloud: no ver\xC3\xA1 nada de Flex OS.",
         1, TH_TXT2, true);
  char left[40]; spaLeftText(left, sizeof(left));
  drawText(rx, y + 170, left, 1, TH_MUTE);

  int bh = 46, bw = SPA_L_W / 2 - 42, byy = y + h - bh - 24;
  int bNo = lx, bOk = rx;
  fillRoundRect(bNo, byy, bw, bh, bh / 2, TH_SURF);
  drawTextC(bNo + bw / 2, byy + 13, "Rechazar", 2, TH_TXT2);
  fillRoundRect(bOk, byy, bw, bh, bh / 2, TH_PRIM);
  drawTextC(bOk + bw / 2, byy + 13, "Emparejar", 2, TH_ONACC);
  spaBtnNo[0] = (int16_t)bNo; spaBtnNo[1] = (int16_t)byy; spaBtnNo[2] = (int16_t)(bNo + bw); spaBtnNo[3] = (int16_t)(byy + bh);
  spaBtnOk[0] = (int16_t)bOk; spaBtnOk[1] = (int16_t)byy; spaBtnOk[2] = (int16_t)(bOk + bw); spaBtnOk[3] = (int16_t)(byy + bh);
}

static void spaCompose(float p){
  if(!spaBandReady()) return;
  int c0 = gClipY0, c1 = gClipY1, cx0 = gClipX0, cx1 = gClipX1;
  bool wl = gLand;
  gClipY0 = 0; gClipY1 = SCR_H - 1; gClipX0 = 0; gClipX1 = SCR_W - 1;
  bbufSys(); setBuf(bbuf);
  memcpy(bbuf + (size_t)spaBakY0 * SCR_W, spaBak, (size_t)SCR_W * (spaBakY1 - spaBakY0 + 1) * 2);
  gLand = spaLand;
  if(spaLand) spaDrawLandscape(p); else spaDrawVertical(p);
  gLand = wl;
  present(spaBakY0, spaBakY1);
  setBuf(fb);
  gClipY0 = c0; gClipY1 = c1; gClipX0 = cx0; gClipX1 = cx1;
}

static void spaRestore(){
  if(!spaBandReady()) return;
  fbLock();
  memcpy(fb + (size_t)spaBakY0 * SCR_W, spaBak, (size_t)SCR_W * (spaBakY1 - spaBakY0 + 1) * 2);
  fbUnlock();
  flxFlush(spaBakY0, spaBakY1);
  spaInvalidateBand();
}

// Reserva y captura, en la fase de dibujo (con fb = el ultimo cuadro publicado).
static bool spaArm(){
  spaBand(spaLand, spaBakY0, spaBakY1);
  if(spaBakY1 < spaBakY0) return false;
  size_t need = (size_t)SCR_W * (spaBakY1 - spaBakY0 + 1) * 2;
  if(spaBakCap < need && memFreePsram() < FLEXMEM_CRIT_BYTES + need) return false;   // sin comerse el suelo del sistema
  if(spaBakCap < need) spaFreeBand();
  if(!spaBak){
    spaBak = (uint16_t*)heap_caps_aligned_alloc(64, need, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    spaBakCap = spaBak ? need : 0;
  }
  if(!spaBak) return false;
  fbLock();
  memcpy(spaBak, fb + (size_t)spaBakY0 * SCR_W, need);
  fbUnlock();
  return spaBandReady();
}

// Cierre animado (con restauracion) e inmediato (sin: alguien tomo la pantalla).
static void spaAbandon(){
  spaState = SPA_HIDDEN;
  spaFreeBand();
  spaInvalidateBand();
}
static void spaClose(){
  if(spaState == SPA_HIDDEN || spaState == SPA_OUT) return;
  if(spaState == SPA_ARMED){ spaAbandon(); return; }
  spaState = SPA_OUT;
  spaT0 = millis();
}

// La persona decide. La decision la toma el nucleo; si ya no habia nada que
// decidir (caduco justo antes), se dice.
static void spaDecide(bool allow){
  spaKey(spaInfo, spaDecided, sizeof(spaDecided));
  bool ok = flexStoragePairDecide(allow);
  spaClose();
  if(!ok) sysNotify("Flex Storage", "El emparejamiento ya hab\xC3\xAD" "a caducado");
  else if(allow) sysNotify("Flex Storage", "Aprobado: termina en el tel\xC3\xA9" "fono");
  else sysNotify("Flex Storage", "Tel\xC3\xA9" "fono rechazado");
}

// #############################################################
// ##  VIGILANCIA  ·  cada vuelta de loop(), barata
// ##  ------------------------------------------------------
// ##  Mira el estado publicado de Flex Storage (copia sin cerrojo
// ##  bloqueante) cuatro veces por segundo: abre el cuadro cuando hay
// ##  un emparejamiento esperando, lo cierra si termino por otro lado
// ##  (caducado, el telefono se rindio) y avisa por la isla de los
// ##  cambios del telefono emparejado.
// #############################################################
static void spaWatch(){
  uint32_t now = millis();
  if(spaWatchMs && (uint32_t)(now - spaWatchMs) < SPA_WATCH_MS) return;
  spaWatchMs = now | 1u;
  FlexStorageInfo si;
  flexStorageInfo(&si);

  // Avisos por flanco del telefono emparejado (el primero solo se apunta).
  if(spaLastState != 0xFF && si.state != spaLastState){
    if(si.state == FSP_READY && spaLastState != FSP_OFF){
      char b[40]; snprintf(b, sizeof(b), "Activado en %s", si.name[0] ? si.name : "tu tel\xC3\xA9" "fono");
      sysNotify("Flex Cloud", b);
    } else if(si.state == FSP_REJECTED) sysNotify("Flex Cloud", "Vuelve a emparejar el tel\xC3\xA9" "fono");
  }
  spaLastState = si.state;

  char key[96];
  if(si.pairWaiting){
    spaKey(si, key, sizeof(key));
    if(spaState == SPA_HIDDEN){
      if(!strcmp(key, spaDecided)) return;                  // ya decidido: el nucleo aun lo publica
      if(!spaCanShow()){
        // Ocupado (bloqueo, Modo PC, otro modal...): espera, y se avisa UNA vez.
        if(strcmp(key, spaTold)){
          snprintf(spaTold, sizeof(spaTold), "%s", key);
          sysNotify("Flex Storage", notifSecureScreen() ? "Desbloquea para aprobar el tel\xC3\xA9" "fono" : "Un tel\xC3\xA9" "fono quiere emparejarse");
        }
        return;
      }
      spaInfo = si;
      spaLand = gLand;
      spaState = SPA_ARMED;
      spaT0 = now; spaShownMs = now;
      spaDirty = false;
      touchDropAll();
      return;
    }
    // A la vista: la cuenta atras (o un emparejamiento nuevo que sustituyo al anterior).
    char cur[96]; spaKey(spaInfo, cur, sizeof(cur));
    if(strcmp(cur, key) || si.pairLeftS != spaInfo.pairLeftS){ spaInfo = si; spaDirty = true; }
    return;
  }
  if(spaState != SPA_HIDDEN && spaState != SPA_OUT) spaClose();   // termino por otro lado
}

// #############################################################
// ##  TICK  ·  animacion y toque (con el cuadro dueno de la pantalla)
// #############################################################
static void spaTick(){
  if(spaState == SPA_HIDDEN) return;
  if(gLand != spaLand){ spaAbandon(); return; }                  // la orientacion cambio debajo
  if(flexOtaOwnsScreen() || gFrPending || optActive()){ spaAbandon(); return; }
  if(qsPanelY != 0 || qsAnimOn){ spaAbandon(); return; }
  // Se bloqueo con el cuadro a la vista: fuera YA (sin restaurar: la
  // pantalla de bloqueo se pinta entera). Vuelve a salir al desbloquear.
  if(notifSecureScreen() || gState == ST_THEFT){ spaAbandon(); spaTold[0] = 0; return; }

  if(spaState == SPA_ARMED){
    if(!spaArm()){
      spaAbandon();
      sysNotify("Flex Storage", "Un tel\xC3\xA9" "fono quiere emparejarse");
      return;
    }
    spaState = SPA_IN;
    spaT0 = millis();
    spaCompose(0.0f);
    return;
  }
  if(!spaBandReady()){ spaAbandon(); return; }                   // sin poder dibujar no se queda la pantalla
  // La barra del sistema sigue viva: navegar retira el cuadro (sin decidir)
  // y vuelve a salir donde se este, mientras el emparejamiento siga.
  if(navBarVisible() && navBarHandle()){ spaAbandon(); return; }

  uint32_t e = millis() - spaT0;
  if(spaState == SPA_IN){
    float p = (e >= SPA_ANIM_MS) ? 1.0f : (float)e / (float)SPA_ANIM_MS;
    p = 1.0f - (1.0f - p) * (1.0f - p);
    spaCompose(p);
    if(e >= SPA_ANIM_MS){ spaState = SPA_SHOWN; spaDirty = false; }
    return;
  }
  if(spaState == SPA_OUT){
    if(e >= SPA_ANIM_MS){
      spaRestore();
      spaState = SPA_HIDDEN;
      spaFreeBand();
      spaInvalidateBand();
      touchDropAll();
      return;
    }
    spaCompose(1.0f - (float)e / (float)SPA_ANIM_MS);
    return;
  }

  // SPA_SHOWN
  if(millis() - spaShownMs >= SPA_AUTO_MS){ spaClose(); return; }
  if(spaDirty){ spaDirty = false; spaCompose(1.0f); }
  if(T.tap){
    int px = T.x, py = T.y;
    if(spaLand){ int lx = T.y, ly = (SCR_W - 1) - T.x; px = lx; py = ly; }
    T.tap = false;
    if(px >= spaBtnOk[0] && px < spaBtnOk[2] && py >= spaBtnOk[1] && py < spaBtnOk[3]){ spaDecide(true); return; }
    if(px >= spaBtnNo[0] && px < spaBtnNo[2] && py >= spaBtnNo[1] && py < spaBtnNo[3]){ spaDecide(false); return; }
    // Fuera de los botones NO cierra: decidir sin querer con la palma no.
  }
  T.swipeLeft = T.swipeRight = T.swipeUp = T.swipeDown = false;
  T.pressed = T.released = false;
}
