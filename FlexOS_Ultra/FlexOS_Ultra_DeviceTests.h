// #############################################################
// ##  FLEX OS ULTRA  ·  FLEX DEVICE CARE  ·  PRUEBAS Y DIAGNOSTICO
// ##  ----------------------------------------------------------
// ##  El motor de diagnostico y las cuatro pruebas que ejecuta:
// ##  sistema, IMU, pantalla y tactil. Ademas, las pantallas de Salud
// ##  de Flex OS, Optimizacion y el resultado.
// ##
// ##  UN SOLO MOTOR PARA LOS DOS CAMINOS. El "Diagnostico completo"
// ##  (lanzado por el usuario) y el "Post-Impact Check" (lanzado por
// ##  la notificacion de caida) recorren EXACTAMENTE las mismas
// ##  pruebas, con el mismo codigo: lo unico que cambia es quien lo
// ##  arranca y que el segundo se pone en marcha solo. Duplicar el
// ##  diagnostico habria garantizado que las dos copias se
// ##  desincronizaran.
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
#include "FlexOS_Ultra_DeviceCare.h"   // eslabon anterior de la cadena

// #############################################################
// ##  MOTOR DE DIAGNOSTICO
// ##  ------------------------------------------------------
// ##  Cuatro etapas. Las dos primeras son MEDIDAS y corren solas; las
// ##  dos ultimas necesitan al usuario (mirar la pantalla, tocarla) y
// ##  se lanzan automaticamente una detras de otra. Nada bloquea: una
// ##  etapa por vuelta de loop, separadas por tiempo con millis().
// #############################################################
enum { DG_SYS = 0, DG_IMU, DG_SCREEN, DG_TOUCH, DG_DONE, DG_N };
#define DC_DIAG_STEP_MS 620       // lo que dura a la vista cada etapa automatica

static int      dcDiagStage = DG_SYS;
static uint32_t dcDiagMs    = 0;
static bool     dcDiagPost  = false;    // true = Post-Impact Check

// ---- Resultados. -1 = no probado; nunca se rellenan "por defecto". ----
static uint8_t dcResSys      = 0;
static bool    dcResSysOk    = false;
static uint8_t dcResImuMask  = 0;
static bool    dcResImuAvail = false;
static int8_t  dcResScreen   = -1;      // 0 = el usuario dijo que no, 1 = si
static int8_t  dcResTouch    = -1;      // cobertura 0..100
static uint8_t dcResFinal    = 0;
static bool    dcResFinalOk  = false;

// Detalle medible del sistema (§ "Test de sistema"): solo lo que se
// puede leer de verdad en esta placa.
struct DcSysRow { const char* label; char value[28]; uint8_t level; };
#define DC_SYS_ROWS 9
static DcSysRow dcSysRow[DC_SYS_ROWS];

static void dcSysSet(int i, const char* label, uint8_t lv, const char* fmt, ...){
  dcSysRow[i].label = label;
  dcSysRow[i].level = lv;
  va_list ap; va_start(ap, fmt);
  vsnprintf(dcSysRow[i].value, sizeof(dcSysRow[i].value), fmt, ap);
  va_end(ap);
}

// Mide el sistema. Todo sale de una lectura; lo que no se puede medir
// dice "No disponible" en vez de un numero plausible.
static void dcRunSystemTest(){
  memSampleNow();
  memSampleFlashNow();
  const FlexMemSnap* s = memSnap();
  char b[24];
  int i = 0;

  uint32_t mhz = getCpuFrequencyMhz();
  if(mhz) dcSysSet(i++, "CPU", DCL_OK, "%lu MHz", (unsigned long)mhz);
  else    dcSysSet(i++, "CPU", DCL_NA, "%s", dct(DCS_NOTAVAIL));

  if(s->inTotal){
    flexMemFmt(s->inFree, b, sizeof(b));
    dcSysSet(i++, "RAM", s->inFree < FLEXMEM_SRAM_MIN_BYTES ? DCL_BAD :
                        (s->inFree < FLEXMEM_SRAM_LOW_BYTES ? DCL_WARN : DCL_OK), "%s", b);
  } else dcSysSet(i++, "RAM", DCL_NA, "%s", dct(DCS_NOTAVAIL));

  if(s->psTotal){
    flexMemFmt(s->psFree, b, sizeof(b));
    int lv = flexMemLevel(s);
    dcSysSet(i++, "PSRAM", lv >= FLEXMEM_LV_CRITICAL ? DCL_BAD :
                          (lv >= FLEXMEM_LV_WARN ? DCL_WARN : DCL_OK), "%s", b);
  } else dcSysSet(i++, "PSRAM", DCL_NA, "%s", dct(DCS_NOTAVAIL));

  {
    uint32_t fw = (uint32_t)ESP.getSketchSize();
    if(fw){ flexMemFmt(fw, b, sizeof(b)); dcSysSet(i++, "Flash", DCL_OK, "%s", b); }
    else    dcSysSet(i++, "Flash", DCL_NA, "%s", dct(DCS_NOTAVAIL));
  }

  if(flexFsReady() && flexFsTotalBytes()){
    uint32_t tot = flexFsTotalBytes(), usd = flexFsUsedBytes();
    int pct = (int)((uint64_t)usd * 100 / tot);
    dcSysSet(i++, dct(DCS_STORAGE), pct >= 95 ? DCL_BAD : (pct >= 85 ? DCL_WARN : DCL_OK), "%d%%", pct);
  } else dcSysSet(i++, dct(DCS_STORAGE), DCL_NA, "%s", dct(DCS_NOTAVAIL));

  {
    int okn = 0;
    if(flexFsReady()) okn++;
    if(clkAnchored)   okn++;
    if(gTimeNvsOk)    okn++;
    dcSysSet(i++, dct(DCS_SERVICES), okn == 3 ? DCL_OK : (okn >= 2 ? DCL_WARN : DCL_BAD), "%d/3", okn);
  }

  {
    bool wd = (esp_task_wdt_status(NULL) == ESP_OK);
    dcSysSet(i++, dct(DCS_WATCHDOG), wd ? DCL_OK : DCL_WARN, "%s",
             wd ? dct(DCS_NORMAL) : dct(DCS_UNAVAILABLE));
  }

  {
    int errs = 0;
    if(flxFlushFault)  errs++;
    if(!gTimeNvsOk)    errs++;
    if(!flexFsReady()) errs++;
    dcSysSet(i++, dct(DCS_ERRORS), errs == 0 ? DCL_OK : (errs == 1 ? DCL_WARN : DCL_BAD), "%d", errs);
  }

  dcSysSet(i++, dct(DCS_REBOOTS), dcBootAbnormal == 0 ? DCL_OK :
                                 (dcBootAbnormal < 3 ? DCL_WARN : DCL_BAD),
           "%lu", (unsigned long)dcBootAbnormal);

  dcResSysOk = dcHealthScore(&dcResSys);
}

// Mide el IMU: no hay nada que "ejecutar", solo leer lo que el driver
// ha comprobado de verdad hasta ahora.
static void dcRunImuTest(){
  dcResImuAvail = flexBnoAvailable();
  dcResImuMask  = flexBnoChecks();
}

// #############################################################
// ##  TEST DE PANTALLA
// ##  ------------------------------------------------------
// ##  Ocho pasadas: negro, blanco, rojo, verde, azul, gris, degradado
// ##  y patron de rejilla. Se avanza tocando. Al final, la pregunta.
// ##
// ##  Se dibuja a pantalla COMPLETA. La barra de navegacion del
// ##  sistema (en modo botones) la sigue estampando el sistema en sus
// ##  64 px: es el unico propietario de esa franja y no se le pisa --
// ##  ademas de ser la salida del usuario si algo va mal.
// #############################################################
enum { DCP_BLACK = 0, DCP_WHITE, DCP_RED, DCP_GREEN, DCP_BLUE, DCP_GRAY,
       DCP_GRAD, DCP_PATTERN, DCP_ASK, DCP_N };
static int dcScrStep = DCP_BLACK;

enum { DCH_SCR_YES = 160, DCH_SCR_NO, DCH_TOUCH_END, DCH_RES_OK, DCH_RES_AGAIN,
       DCH_OPT_RUN, DCH_HEALTH_DIAG };

static void dcScrTestRender(){
  setBuf(fb);
  dcHitClear();
  int h = SCR_H;
  switch(dcScrStep){
    case DCP_BLACK: fillRect(0, 0, SCR_W, h, rgb565(0,0,0)); break;
    case DCP_WHITE: fillRect(0, 0, SCR_W, h, rgb565(255,255,255)); break;
    case DCP_RED:   fillRect(0, 0, SCR_W, h, rgb565(255,0,0)); break;
    case DCP_GREEN: fillRect(0, 0, SCR_W, h, rgb565(0,255,0)); break;
    case DCP_BLUE:  fillRect(0, 0, SCR_W, h, rgb565(0,0,255)); break;
    case DCP_GRAY:  fillRect(0, 0, SCR_W, h, rgb565(128,128,128)); break;
    case DCP_GRAD:
      // Degradado vertical: una linea por fila, sin buffer intermedio.
      for(int y = 0; y < h; y++){
        int v = y * 255 / (h - 1);
        hLine(0, y, SCR_W, rgb565((uint8_t)v, (uint8_t)v, (uint8_t)v));
      }
      // Tres rampas de color debajo, para ver bandas y cortes de tono.
      for(int y = 0; y < 60; y++){
        int v = y * 255 / 59;
        hLine(0, h / 2 - 90 + y, SCR_W / 3, rgb565((uint8_t)v, 0, 0));
        hLine(SCR_W / 3, h / 2 - 90 + y, SCR_W / 3, rgb565(0, (uint8_t)v, 0));
        hLine(2 * SCR_W / 3, h / 2 - 90 + y, SCR_W - 2 * SCR_W / 3, rgb565(0, 0, (uint8_t)v));
      }
      break;
    case DCP_PATTERN: {
      // Rejilla de un pixel y tablero: delatan pixeles muertos, lineas
      // perdidas y problemas de sincronismo del panel.
      fillRect(0, 0, SCR_W, h, rgb565(0,0,0));
      for(int x = 0; x < SCR_W; x += 2)  vLine(x, 0, h, rgb565(255,255,255));
      int cs = 40;
      for(int y = 0; y < h; y += cs)
        for(int x = 0; x < SCR_W; x += cs)
          if(((x / cs) + (y / cs)) & 1)
            fillRect(x, y, cs, cs, rgb565(255,255,255));
      drawRect(0, 0, SCR_W, h, rgb565(255,0,0));
      drawRect(1, 1, SCR_W - 2, h - 2, rgb565(255,0,0));
      break;
    }
    default: break;
  }
  if(dcScrStep < DCP_ASK){
    // Indicador de avance, siempre legible sobre cualquier fondo.
    int n = DCP_ASK;
    int bwid = n * 14, bx = (SCR_W - bwid) / 2, byy = h - 130;
    fillRoundRect(bx - 10, byy - 8, bwid + 20, 24, 12, rgb565(0,0,0));
    fillRoundRectA(bx - 10, byy - 8, bwid + 20, 24, 12, rgb565(255,255,255), 40);
    for(int i = 0; i < n; i++)
      fillCircle(bx + i * 14 + 5, byy + 4, i == dcScrStep ? 5 : 3,
                 i == dcScrStep ? rgb565(255,255,255) : rgb565(120,120,120));
    flxFlush(0, SCR_H - 1);
    return;
  }
  // Pregunta final.
  fillRect(0, 0, SCR_W, h, WIN_BG);
  dcHeader(dct(DCS_SCREEN));
  int bx, by, bw, bh; uiBox(bx, by, bw, bh);
  int pad = uiPad();
  dcSectionIcon(DCI_DIAG, bx + bw / 2 - 40, by + bh / 4, 80, TH_TXT2, wallAccent());
  int fs = uiFontFit(dct(DCS_SCRASK1), bw - 2 * pad, 2);
  drawTextC(bx + bw / 2, by + bh / 4 + 100, dct(DCS_SCRASK1), fs, TH_TXT);
  drawTextC(bx + bw / 2, by + bh / 4 + 126, dct(DCS_SCRASK2), fs, TH_TXT);
  int bhh = 48, bwid = bw - 2 * pad;
  int byy = by + bh - pad - bhh;
  dcButton(bx + pad, byy,             bwid, bhh, dct(DCS_NO),  DCB_RETRY, TH_SURF, TH_DANGER, DCH_SCR_NO);
  dcButton(bx + pad, byy - bhh - 10,  bwid, bhh, dct(DCS_YES), DCB_CHECK, TH_PRIM, TH_ONACC,  DCH_SCR_YES);
  flxFlush(0, SCR_H - 1);
}

static void dcScrTestDone(bool ok);      // definido abajo, junto al motor

static void dcScrTestTick(){
  if(!T.tap) return;
  if(dcScrStep < DCP_ASK){
    T.tap = false;
    dcScrStep++;
    dcScrTestRender();
    return;
  }
  int id = dcHitTest(T.x, T.y);
  T.tap = false;
  if(id == DCH_SCR_YES) dcScrTestDone(true);
  else if(id == DCH_SCR_NO) dcScrTestDone(false);
}

// #############################################################
// ##  TEST TACTIL
// ##  ------------------------------------------------------
// ##  Rejilla interactiva sobre el area de contenido. Se enciende la
// ##  celda que toca el dedo -- tanto al tocar como al arrastrar --, y
// ##  la cobertura final es celdas encendidas / celdas totales. Se
// ##  ensena tambien la ULTIMA coordenada leida, que es lo que permite
// ##  ver a simple vista si el panel devuelve puntos coherentes.
// ##
// ##  Reutiliza el tactil del sistema (struct Touch): no hay ningun
// ##  camino paralelo de lectura.
// #############################################################
#define DC_TT_COLS 6
#define DC_TT_ROWS 8
static uint8_t dcTtCell[DC_TT_COLS * DC_TT_ROWS];
static int     dcTtHit = 0;
static int     dcTtLastX = -1, dcTtLastY = -1;

static void dcTouchTestGeom(int &gx, int &gy, int &cw, int &ch, int &foot){
  int bx, by, bw, bh; uiBox(bx, by, bw, bh);
  int pad = uiPad();
  foot = 96;                                   // franja inferior: cifras + boton
  gx = bx + pad;
  gy = by + pad + 26;
  cw = (bw - 2 * pad) / DC_TT_COLS;
  ch = (bh - 2 * pad - 26 - foot) / DC_TT_ROWS;
  if(ch < 12) ch = 12;
}

static void dcTouchTestRender(){
  setBuf(fb);
  dcHitClear();
  int bx, by, bw, bh; uiBox(bx, by, bw, bh);
  fillRect(bx, by, bw, bh, WIN_BG);
  int pad = uiPad();
  int gx, gy, cw, ch, foot;
  dcTouchTestGeom(gx, gy, cw, ch, foot);

  drawTextC(bx + bw / 2, by + pad, dct(DCS_TTHINT), 1, TH_TXT2);
  for(int r = 0; r < DC_TT_ROWS; r++){
    for(int c = 0; c < DC_TT_COLS; c++){
      int x = gx + c * cw, y = gy + r * ch;
      bool on = dcTtCell[r * DC_TT_COLS + c] != 0;
      if(on){
        fillRoundRect(x + 2, y + 2, cw - 4, ch - 4, 6, TH_OK);
        dcBtnGlyphCheck(x + cw / 2, y + ch / 2, ch / 5, TH_ONACC);
      } else {
        fillRoundRect(x + 2, y + 2, cw - 4, ch - 4, 6, TH_SURF);
        drawRoundRect(x + 2, y + 2, cw - 4, ch - 4, 6, TH_DIV);
      }
    }
  }
  int cov = dcTtHit * 100 / (DC_TT_COLS * DC_TT_ROWS);
  char b[64];
  int fy = by + bh - foot;
  snprintf(b, sizeof(b), "%d%%  \xC2\xB7  %d/%d", cov, dcTtHit, DC_TT_COLS * DC_TT_ROWS);
  drawText(bx + pad, fy, b, 2, cov >= 95 ? TH_OK : (cov >= 60 ? TH_WARN : TH_TXT2));
  if(dcTtLastX >= 0) snprintf(b, sizeof(b), "X %d  Y %d", dcTtLastX, dcTtLastY);
  else               snprintf(b, sizeof(b), "%s", dct(DCS_NOTAVAIL));
  drawTextR(bx + bw - pad, fy + 2, b, 1, TH_MUTE);
  dcButton(bx + pad, by + bh - pad - 46, bw - 2 * pad, 46, dct(DCS_DONE), DCB_CHECK,
           TH_PRIM, TH_ONACC, DCH_TOUCH_END);
  flxFlush(WIN_TOP, WIN_BOT);
}

static void dcTouchTestDone();

static void dcTouchTestTick(){
  int gx, gy, cw, ch, foot;
  dcTouchTestGeom(gx, gy, cw, ch, foot);
  // El dedo enciende celdas mientras esta apoyado: tocar y arrastrar
  // valen igual, que es como se recorre una rejilla de verdad.
  if(T.down || T.pressed){
    dcTtLastX = T.x; dcTtLastY = T.y;
    int c = (T.x - gx) / (cw > 0 ? cw : 1);
    int r = (T.y - gy) / (ch > 0 ? ch : 1);
    if(c >= 0 && c < DC_TT_COLS && r >= 0 && r < DC_TT_ROWS){
      int idx = r * DC_TT_COLS + c;
      if(!dcTtCell[idx]){
        dcTtCell[idx] = 1;
        dcTtHit++;
        // Solo se repinta la celda y la fila de cifras: recorrer la
        // rejilla no puede costar una pantalla entera por celda.
        setBuf(fb);
        int x = gx + c * cw, y = gy + r * ch;
        fillRoundRect(x + 2, y + 2, cw - 4, ch - 4, 6, TH_OK);
        dcBtnGlyphCheck(x + cw / 2, y + ch / 2, ch / 5, TH_ONACC);
        flxFlush(y, y + ch);
        int bx, by, bw, bh; uiBox(bx, by, bw, bh);
        int pad = uiPad();
        int fy = by + bh - foot;
        fillRect(bx, fy - 2, bw, 24, WIN_BG);
        char b[64];
        int cov = dcTtHit * 100 / (DC_TT_COLS * DC_TT_ROWS);
        snprintf(b, sizeof(b), "%d%%  \xC2\xB7  %d/%d", cov, dcTtHit, DC_TT_COLS * DC_TT_ROWS);
        drawText(bx + pad, fy, b, 2, cov >= 95 ? TH_OK : (cov >= 60 ? TH_WARN : TH_TXT2));
        snprintf(b, sizeof(b), "X %d  Y %d", dcTtLastX, dcTtLastY);
        drawTextR(bx + bw - pad, fy + 2, b, 1, TH_MUTE);
        flxFlush(fy - 2, fy + 22);
      }
    }
  }
  if(T.tap){
    int id = dcHitTest(T.x, T.y);
    T.tap = false;
    if(id == DCH_TOUCH_END) dcTouchTestDone();
  }
}

// #############################################################
// ##  PANTALLA DEL MOTOR  ·  lista de etapas con marcas
// #############################################################
static void dcDiagRender(){
  setBuf(fb);
  dcHitClear();
  int bx, by, bw, bh; uiBox(bx, by, bw, bh);
  fillRect(bx, by, bw, bh, WIN_BG);
  int pad = uiPad();
  int x = bx + pad, w = bw - 2 * pad;

  // Anillo de progreso: cuantas etapas van hechas.
  int rr = 54;
  int rcx = bx + bw / 2, rcy = by + pad + rr + 8;
  int th = rr / 6;
  fillRing(rcx, rcy, rr, th, TH_TRACK);
  float p = (float)dcDiagStage / (float)(DG_N - 1);
  if(p > 1) p = 1;
  arcStroke(rcx, rcy, rr - th / 2, -90.0f, -90.0f + 360.0f * p, th, TH_PRIM);
  // Latido mientras trabaja: circulo interior que respira con el tiempo.
  if(dcDiagStage < DG_DONE){
    uint32_t e = (millis() - dcAnimT0) % 1200u;
    float ph = (e < 600u) ? (float)e / 600.0f : 1.0f - (float)(e - 600u) / 600.0f;
    fillCircle(rcx, rcy, (int)(rr * 0.34f + rr * 0.10f * ph), TH_SURF2);
    dcSectionIcon(DCI_DIAG, rcx - 22, rcy - 22, 44, TH_TXT2, wallAccent());
  } else {
    fillCircle(rcx, rcy, (int)(rr * 0.42f), TH_OK);
    dcBtnGlyphCheck(rcx, rcy, rr / 4, TH_ONACC);
  }
  int y = rcy + rr + uiGap();

  drawTextC(bx + bw / 2, y, dcDiagPost ? dct(DCS_EVPOST) : dct(DCS_DIAG), 3, TH_TXT);
  y += uiLineH(3) + uiGap();

  const char* nm[DG_N];
  nm[DG_SYS]    = dct(DCS_SYSTEM);
  nm[DG_IMU]    = dct(DCS_IMUNAME);
  nm[DG_SCREEN] = dct(DCS_SCREEN);
  nm[DG_TOUCH]  = dct(DCS_TOUCH);
  nm[DG_DONE]   = dct(DCS_DONE);
  for(int i = 0; i < DG_N; i++){
    bool done = (dcDiagStage > i);
    bool cur  = (dcDiagStage == i);
    if(done){
      fillCircle(x + 12, y + 9, 10, TH_OK);
      dcBtnGlyphCheck(x + 12, y + 9, 5, TH_ONACC);
    } else {
      drawCircle(x + 12, y + 9, 10, cur ? TH_PRIM : TH_DIV);
      if(cur) fillCircle(x + 12, y + 9, 5, TH_PRIM);
    }
    drawTextClip(x + 32, y, nm[i], 2, cur ? TH_TXT : (done ? TH_TXT2 : TH_MUTE), x + w);
    y += 30;
  }
  y += 4;
  drawTextClip(x, y, dct(DCS_OPTNOTE), 1, TH_MUTE, x + w);
  flxFlush(WIN_TOP, WIN_BOT);
}

// #############################################################
// ##  PANTALLA DE RESULTADO
// #############################################################
static void dcResultCompute(){
  // Puntuacion final: media de lo que SE PUDO medir. Una prueba que no
  // se hizo (IMU ausente, pantalla sin responder) no puntua ni a favor
  // ni en contra; el resultado dice cuantas entraron.
  int sum = 0, n = 0;
  if(dcResSysOk){ sum += dcResSys; n++; }
  if(dcResImuAvail){
    int bits = 0;
    for(int i = 0; i < 5; i++) if(dcResImuMask & (1 << i)) bits++;
    sum += bits * 100 / 5; n++;
  }
  if(dcResScreen >= 0){ sum += dcResScreen ? 100 : 0; n++; }
  if(dcResTouch  >= 0){ sum += dcResTouch; n++; }
  dcResFinalOk = (n > 0);
  dcResFinal   = dcResFinalOk ? (uint8_t)(sum / n) : 0;
  if(dcResFinalOk)
    dcHistAdd(dcDiagPost ? DC_EV_POST : DC_EV_DIAG, dcResFinal,
              (uint8_t)((dcResScreen == 1 ? 1 : 0) | (dcResImuAvail ? 2 : 0)), 0);
}

static void dcResultRender(){
  setBuf(fb);
  dcHitClear();
  int bx, by, bw, bh; uiBox(bx, by, bw, bh);
  fillRect(bx, by, bw, bh, WIN_BG);
  int pad = uiPad();
  int x = bx + pad, w = bw - 2 * pad;
  int y = by + pad;

  int rr = 62, rcx = bx + bw / 2, rcy = y + rr + 6;
  if(dcResFinalOk){
    // El anillo entra barriendo, igual que el de la pantalla de inicio.
    uint32_t e = millis() - dcAnimT0;
    float p = e >= DC_RING_MS ? 1.0f : (float)e / (float)DC_RING_MS;
    p = 1.0f - (1.0f - p) * (1.0f - p);
    dcScoreRing(rcx, rcy, rr, dcResFinal, p, dcScoreColor(dcResFinal));
  } else {
    fillRing(rcx, rcy, rr, rr / 6, TH_TRACK);
    drawTextC(rcx, rcy - 10, "--", 4, TH_MUTE);
  }
  y = rcy + rr + uiGap();
  drawTextC(bx + bw / 2, y, dcResFinalOk ? dcScoreWord(dcResFinal) : dct(DCS_NOTAVAIL), 3, TH_TXT);
  y += uiLineH(3) + uiGap();

  char v[32];
  if(dcResSysOk) snprintf(v, sizeof(v), "%u/100", (unsigned)dcResSys);
  else           snprintf(v, sizeof(v), "%s", dct(DCS_NOTAVAIL));
  y = dcRowLed(x, y, w, dct(DCS_SYSTEM), v, dcResSysOk ? (dcResSys >= 80 ? DCL_OK : (dcResSys >= 55 ? DCL_WARN : DCL_BAD)) : DCL_NA);

  if(dcResImuAvail){
    int bits = 0;
    for(int i = 0; i < 5; i++) if(dcResImuMask & (1 << i)) bits++;
    snprintf(v, sizeof(v), "%d/5", bits);
    y = dcRowLed(x, y, w, dct(DCS_IMUNAME), v, bits == 5 ? DCL_OK : (bits >= 3 ? DCL_WARN : DCL_BAD));
  } else {
    y = dcRowLed(x, y, w, dct(DCS_IMUNAME), dct(DCS_NOTAVAIL), DCL_NA);
  }

  if(dcResScreen >= 0)
    y = dcRowLed(x, y, w, dct(DCS_SCREEN), dcResScreen ? dct(DCS_NORMAL) : dct(DCS_ATTENTION),
                 dcResScreen ? DCL_OK : DCL_BAD);
  else
    y = dcRowLed(x, y, w, dct(DCS_SCREEN), dct(DCS_NOTAVAIL), DCL_NA);

  if(dcResTouch >= 0){
    snprintf(v, sizeof(v), "%d%%", dcResTouch);
    y = dcRowLed(x, y, w, dct(DCS_TOUCH), v, dcResTouch >= 95 ? DCL_OK : (dcResTouch >= 60 ? DCL_WARN : DCL_BAD));
  } else {
    y = dcRowLed(x, y, w, dct(DCS_TOUCH), dct(DCS_NOTAVAIL), DCL_NA);
  }

  // ---- Detalle del test de sistema ----
  // Se midio en la etapa 1 (dcRunSystemTest); si no se ensenara, se
  // habria medido para nada. Dos columnas, y cada fila con su semaforo:
  // lo que no se pudo leer pone "No disponible", no un numero plausible.
  {
    y += 8;
    int colw = w / 2;
    int y0 = y;
    for(int i = 0; i < DC_SYS_ROWS; i++){
      if(!dcSysRow[i].label || !dcSysRow[i].label[0]) continue;
      int col = i % 2, row = i / 2;
      int rx = x + col * colw, ry = y0 + row * 22;
      if(ry + 20 > by + bh - pad - 108) break;      // no invadir los botones
      fillCircle(rx + 5, ry + 7, 4, dcLevelColor(dcSysRow[i].level));
      drawTextClip(rx + 14, ry, dcSysRow[i].label, 1, TH_TXT2, rx + colw - 62);
      drawTextR(rx + colw - 8, ry, dcSysRow[i].value, 1, TH_TXT);
    }
    y = y0 + ((DC_SYS_ROWS + 1) / 2) * 22 + 6;
  }

  // En un Post-Impact Check se recuerda que evento lo disparo.
  if(dcDiagPost && dcLastEvtOk){
    y += 6;
    snprintf(v, sizeof(v), "%s %u \xC2\xB7 %.1f g", dct(DCS_CONFIDENCE),
             (unsigned)dcLastEvt.confidence, (double)dcLastEvt.peakG);
    drawTextClip(x, y, v, 1, TH_TXT2, x + w);
  }

  int bhh = 46, byy = by + bh - pad - bhh;
  dcButton(x, byy, w, bhh, dct(DCS_DONE), DCB_CHECK, TH_PRIM, TH_ONACC, DCH_RES_OK);
  dcButton(x, byy - bhh - 10, w, bhh, dct(DCS_REPEAT), DCB_RETRY, TH_SURF, TH_TXT, DCH_RES_AGAIN);
  flxFlush(WIN_TOP, WIN_BOT);
}

static void dcResultTick(){
  if(T.tap){
    int id = dcHitTest(T.x, T.y);
    T.tap = false;
    if(id == DCH_RES_OK){ dcGoto(DC_HOME); return; }
    if(id == DCH_RES_AGAIN){ dcDiagEnter(dcDiagPost); return; }
  }
  // Barrido del anillo: solo mientras dura.
  uint32_t e = millis() - dcAnimT0;
  if(e > DC_RING_MS) return;
  if(millis() - dcAnimMs < 33) return;
  dcAnimMs = millis();
  dcResultRender();
}

// #############################################################
// ##  EL MOTOR  ·  una etapa por vuelta, sin bloquear
// #############################################################
static void dcDiagEnter(bool postImpact){
  dcDiagPost   = postImpact;
  dcDiagStage  = DG_SYS;
  dcDiagMs     = millis();
  dcAnimT0     = millis();
  dcAnimMs     = 0;
  dcResSys = 0; dcResSysOk = false;
  dcResImuMask = 0; dcResImuAvail = false;
  dcResScreen = -1; dcResTouch = -1;
  dcResFinal = 0; dcResFinalOk = false;
  dcScrStep = DCP_BLACK;
  memset(dcTtCell, 0, sizeof(dcTtCell));
  dcTtHit = 0; dcTtLastX = dcTtLastY = -1;
  for(int i = 0; i < DC_SYS_ROWS; i++){ dcSysRow[i].label = ""; dcSysRow[i].value[0] = 0; dcSysRow[i].level = DCL_NA; }
  dcScreen = postImpact ? DC_POST : DC_DIAG;
  touchDropAll();
  dcHeader(postImpact ? dct(DCS_EVPOST) : dct(DCS_DIAG));
  dcDiagRender();
}

static void dcScrTestDone(bool ok){
  dcResScreen = ok ? 1 : 0;
  dcScreen    = dcDiagPost ? DC_POST : DC_DIAG;
  dcDiagStage = DG_TOUCH;
  dcDiagMs    = millis();
  touchDropAll();
  dcHeader(dcDiagPost ? dct(DCS_EVPOST) : dct(DCS_DIAG));
  dcDiagRender();
}

static void dcTouchTestDone(){
  dcResTouch  = dcTtHit * 100 / (DC_TT_COLS * DC_TT_ROWS);
  dcScreen    = dcDiagPost ? DC_POST : DC_DIAG;
  dcDiagStage = DG_DONE;
  dcDiagMs    = millis();
  touchDropAll();
  dcHeader(dcDiagPost ? dct(DCS_EVPOST) : dct(DCS_DIAG));
  dcDiagRender();
}

static void dcDiagTick(){
  // Una etapa por vuelta, separada por tiempo. El trabajo de cada
  // etapa corre UNA sola vez, al entrar en ella.
  if(millis() - dcDiagMs < DC_DIAG_STEP_MS){
    // Mientras espera, late el anillo. Solo su banda.
    if(millis() - dcAnimMs >= 60){
      dcAnimMs = millis();
      int bx, by, bw, bh; uiBox(bx, by, bw, bh);
      (void)bw; (void)bh;
      int rr = 54, rcy = by + uiPad() + rr + 8;
      int b0 = rcy - rr - 2, b1 = rcy + rr + 2;
      if(b0 < by) b0 = by;
      setBuf(fb);
      int c0 = gClipY0, c1 = gClipY1;
      gClipY0 = b0; gClipY1 = b1;
      fillRect(bx, b0, bw, b1 - b0 + 1, WIN_BG);
      int rcx = bx + bw / 2, th = rr / 6;
      fillRing(rcx, rcy, rr, th, TH_TRACK);
      float p = (float)dcDiagStage / (float)(DG_N - 1);
      if(p > 1) p = 1;
      arcStroke(rcx, rcy, rr - th / 2, -90.0f, -90.0f + 360.0f * p, th, TH_PRIM);
      if(dcDiagStage < DG_DONE){
        uint32_t e = (millis() - dcAnimT0) % 1200u;
        float ph = (e < 600u) ? (float)e / 600.0f : 1.0f - (float)(e - 600u) / 600.0f;
        fillCircle(rcx, rcy, (int)(rr * 0.34f + rr * 0.10f * ph), TH_SURF2);
        dcSectionIcon(DCI_DIAG, rcx - 22, rcy - 22, 44, TH_TXT2, wallAccent());
      } else {
        fillCircle(rcx, rcy, (int)(rr * 0.42f), TH_OK);
        dcBtnGlyphCheck(rcx, rcy, rr / 4, TH_ONACC);
      }
      gClipY0 = c0; gClipY1 = c1;
      flxFlush(b0, b1);
    }
    return;
  }
  dcDiagMs = millis();
  switch(dcDiagStage){
    case DG_SYS:
      dcRunSystemTest();
      dcDiagStage = DG_IMU;
      dcDiagRender();
      break;
    case DG_IMU:
      dcRunImuTest();
      dcDiagStage = DG_SCREEN;
      dcDiagRender();
      break;
    case DG_SCREEN:
      // Las pruebas disponibles se ejecutan SOLAS: el usuario no tiene
      // que ir a buscarlas (esa es la mitad del punto de un Post-Impact
      // Check).
      dcScrStep = DCP_BLACK;
      dcScreen  = DC_SCRTEST;
      touchDropAll();
      dcScrTestRender();
      break;
    case DG_TOUCH:
      dcScreen = DC_TOUCHTEST;
      touchDropAll();
      dcHeader(dct(DCS_TOUCH));
      dcTouchTestRender();
      break;
    case DG_DONE:
      dcResultCompute();
      dcScreen = DC_RESULT;
      dcAnimT0 = millis();
      dcAnimMs = 0;
      touchDropAll();
      dcHeader(dct(DCS_DIAG));
      dcResultRender();
      break;
    default: break;
  }
}

// #############################################################
// ##  PRUEBA DEL GY-BNO085 CON LECTURAS EN VIVO
// ##  ------------------------------------------------------
// ##  Las cinco comprobaciones y, debajo, los valores REALES que esta
// ##  entregando el sensor ahora mismo: aceleracion, velocidad angular
// ##  y orientacion. Un tick marcado aqui significa que llego un
// ##  informe de ese sensor, no que "deberia estar".
// #############################################################
static void dcImuBar(int x, int y, int w, const char* label, float v, float full, uint16_t col){
  drawText(x, y, label, 1, TH_TXT2);
  char b[16];
  snprintf(b, sizeof(b), "%+.2f", (double)v);
  drawTextR(x + w, y, b, 1, TH_TXT);
  int by2 = y + 12, bh = 6;
  fillRoundRect(x, by2, w, bh, bh / 2, TH_TRACK);
  float n = v / (full > 0.0001f ? full : 1.0f);
  if(n >  1) n =  1;
  if(n < -1) n = -1;
  int half = w / 2;
  int len = (int)(half * (n < 0 ? -n : n));
  if(len > 0){
    if(n >= 0) fillRoundRect(x + half, by2, len, bh, bh / 2, col);
    else       fillRoundRect(x + half - len, by2, len, bh, bh / 2, col);
  }
  fillRect(x + half, by2 - 2, 1, bh + 4, TH_DIV);
}

static void dcImuTestBody(){
  int bx, by, bw, bh; uiBox(bx, by, bw, bh);
  int pad = uiPad();
  int x = bx + pad, w = bw - 2 * pad;
  int y = by + pad;
  uint8_t c = flexBnoChecks();
  char b[64];

  drawTextC(bx + bw / 2, y, "GY-BNO085", 3, TH_TXT);
  y += uiLineH(3) + 6;
  snprintf(b, sizeof(b), "%s \xC2\xB7 %lu %s", flexBnoError(),
           (unsigned long)flexBnoReportCount(), "informes");
  drawTextC(bx + bw / 2, y, b, 1, TH_MUTE);
  y += 20;

  y = dcCheckRow(x, y, w, dct(DCS_I2CLINK), (c & FLEXBNO_CHK_I2C)   != 0, NULL);
  y = dcCheckRow(x, y, w, dct(DCS_ACCEL),   (c & FLEXBNO_CHK_ACCEL) != 0, NULL);
  y = dcCheckRow(x, y, w, dct(DCS_GYRO),    (c & FLEXBNO_CHK_GYRO)  != 0, NULL);
  y = dcCheckRow(x, y, w, dct(DCS_MAG),     (c & FLEXBNO_CHK_MAG)   != 0, NULL);
  y = dcCheckRow(x, y, w, dct(DCS_FUSION),  (c & FLEXBNO_CHK_FUSION)!= 0, NULL);
  y += 8;

  float a[3], g[3], m[3], rpy[3];
  if(flexBnoAccel(a)){
    snprintf(b, sizeof(b), "%s  \xC2\xB7  %.2f g", dct(DCS_ACCEL), (double)flexFallMagG(a[0], a[1], a[2]));
    drawText(x, y, b, 2, TH_TXT); y += 22;
    dcImuBar(x, y, w, "X  m/s\xC2\xB2", a[0], 20.0f, TH_PRIM);  y += 26;
    dcImuBar(x, y, w, "Y  m/s\xC2\xB2", a[1], 20.0f, TH_PRIM);  y += 26;
    dcImuBar(x, y, w, "Z  m/s\xC2\xB2", a[2], 20.0f, TH_PRIM);  y += 30;
  } else {
    drawText(x, y, dct(DCS_ACCEL), 2, TH_MUTE);
    drawTextR(x + w, y + 2, dct(DCS_NOTAVAIL), 1, TH_MUTE); y += 26;
  }
  if(flexBnoGyro(g)){
    snprintf(b, sizeof(b), "%s  \xC2\xB7  %.2f rad/s", dct(DCS_GYRO),
             (double)sqrtf(g[0]*g[0] + g[1]*g[1] + g[2]*g[2]));
    drawText(x, y, b, 2, TH_TXT); y += 22;
    dcImuBar(x, y, w, "X  rad/s", g[0], 8.0f, TH_ACCS); y += 26;
    dcImuBar(x, y, w, "Y  rad/s", g[1], 8.0f, TH_ACCS); y += 26;
    dcImuBar(x, y, w, "Z  rad/s", g[2], 8.0f, TH_ACCS); y += 30;
  } else {
    drawText(x, y, dct(DCS_GYRO), 2, TH_MUTE);
    drawTextR(x + w, y + 2, dct(DCS_NOTAVAIL), 1, TH_MUTE); y += 26;
  }
  if(flexBnoMag(m)){
    snprintf(b, sizeof(b), "%s  \xC2\xB7  %.1f \xC2\xB5T", dct(DCS_MAG),
             (double)sqrtf(m[0]*m[0] + m[1]*m[1] + m[2]*m[2]));
    drawText(x, y, b, 2, TH_TXT); y += 24;
  } else {
    drawText(x, y, dct(DCS_MAG), 2, TH_MUTE);
    drawTextR(x + w, y + 2, dct(DCS_NOTAVAIL), 1, TH_MUTE); y += 24;
  }
  if(flexBnoEuler(rpy)){
    snprintf(b, sizeof(b), "%s  %.0f\xC2\xB0 / %.0f\xC2\xB0 / %.0f\xC2\xB0",
             dct(DCS_FUSION), (double)rpy[0], (double)rpy[1], (double)rpy[2]);
    drawText(x, y, b, 2, TH_TXT);
  } else {
    drawText(x, y, dct(DCS_FUSION), 2, TH_MUTE);
    drawTextR(x + w, y + 2, dct(DCS_NOTAVAIL), 1, TH_MUTE);
  }
}

static void dcImuTestRender(){
  setBuf(fb);
  dcHitClear();
  int bx, by, bw, bh; uiBox(bx, by, bw, bh);
  fillRect(bx, by, bw, bh, WIN_BG);
  dcImuTestBody();
  flxFlush(WIN_TOP, WIN_BOT);
}

static void dcImuTestTick(){
  // Refresco a ~8 fps: son lecturas de sensor, no una animacion, y
  // repintar mas a menudo solo gastaria bus y bateria.
  if(millis() - dcAnimMs < 120) return;
  dcAnimMs = millis();
  dcImuTestRender();
}

// #############################################################
// ##  SALUD DE FLEX OS
// ##  ------------------------------------------------------
// ##  Las ocho metricas con su semaforo y su barra, y la puntuacion
// ##  calculada a partir de ellas (no una constante).
// #############################################################
static void dcHealthRender(){
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

  int rr = 46, rcx = bx + bw / 2, rcy = y + rr + 4;
  if(ok){
    uint32_t e = millis() - dcAnimT0;
    float p = e >= DC_RING_MS ? 1.0f : (float)e / (float)DC_RING_MS;
    p = 1.0f - (1.0f - p) * (1.0f - p);
    dcScoreRing(rcx, rcy, rr, sc, p, dcScoreColor(sc));
  } else {
    fillRing(rcx, rcy, rr, rr / 6, TH_TRACK);
    drawTextC(rcx, rcy - 8, "--", 4, TH_MUTE);
  }
  y = rcy + rr + 8;
  drawTextC(bx + bw / 2, y, ok ? dcScoreWord(sc) : dct(DCS_NOTAVAIL), 2, TH_TXT2);
  y += uiLineH(2) + uiGap();

  static const int LBL[DCH_N] = { DCS_MEMORY, DCS_PSRAM, DCS_STABILITY, DCS_WATCHDOG,
                                  DCS_SERVICES, DCS_ERRORS, DCS_REBOOTS, DCS_STORAGE };
  for(int i = 0; i < DCH_N; i++){
    if(y + 40 > by + bh - pad - 56) break;
    fillCircle(x + 9, y + 9, 6, dcLevelColor(m[i].level));
    drawTextClip(x + 24, y, dct(LBL[i]), 2, TH_TXT, x + w / 2 + 20);
    drawTextR(x + w, y + 2, m[i].detail, 1, TH_TXT2);
    // Barra solo cuando la metrica ES un porcentaje: inventarse una
    // para "Reinicios" no diria nada.
    if(m[i].pct >= 0){
      int by2 = y + 22, bhh = 5;
      fillRoundRect(x + 24, by2, w - 24, bhh, bhh / 2, TH_TRACK);
      fillRoundRect(x + 24, by2, (w - 24) * m[i].pct / 100, bhh, bhh / 2, dcLevelColor(m[i].level));
      y += 34;
    } else y += 26;
  }

  int bhh = 46, byy = by + bh - pad - bhh;
  dcButton(x, byy, w, bhh, dct(DCS_DIAG), DCB_PLAY, TH_PRIM, TH_ONACC, DCH_HEALTH_DIAG);
  flxFlush(WIN_TOP, WIN_BOT);
}

static void dcHealthTick(){
  if(T.tap){
    int id = dcHitTest(T.x, T.y);
    T.tap = false;
    if(id == DCH_HEALTH_DIAG){ dcPrev = DC_HOME; dcDiagEnter(false); return; }
  }
  uint32_t e = millis() - dcAnimT0;
  // Barrido del anillo al entrar y, despues, un refresco por segundo:
  // las metricas cambian despacio y esto no puede ser un bucle de
  // dibujo.
  uint32_t period = (e < DC_RING_MS) ? 33u : 1000u;
  if(millis() - dcAnimMs < period) return;
  dcAnimMs = millis();
  dcHealthRender();
}

// #############################################################
// ##  OPTIMIZACION
// ##  ------------------------------------------------------
// ##  NO se reimplementa nada: el motor es el MISMO "Optimizar Flex
// ##  OS" que ya existe (FlexOS_Ultra_System.h), con sus cinco etapas
// ##  reales y su regla de oro -- solo se suelta lo que el sistema
// ##  sabe reconstruir. Lo unico que anade Device Care es abrirlo
// ##  desde aqui y volver a ESTA pantalla al terminar (para eso esta
// ##  el aviso de cierre optStartCb), ademas de anotar la pasada en el
// ##  historial.
// #############################################################
static void dcOptimDone(){
  dcHistAdd(DC_EV_OPT, 0, 0, 0);
  dcScreen = DC_OPTIM;
  dcAnimT0 = millis();
  dcAnimMs = 0;
  dcHeader(dct(DCS_OPT));
  dcOptimRender();
}

static void dcOptimRender(){
  setBuf(fb);
  dcHitClear();
  int bx, by, bw, bh; uiBox(bx, by, bw, bh);
  fillRect(bx, by, bw, bh, WIN_BG);
  int pad = uiPad();
  int x = bx + pad, w = bw - 2 * pad;
  int y = by + pad;

  dcSectionIcon(DCI_OPT, bx + bw / 2 - 44, y, 88, TH_TXT2, wallAccent());
  y += 96;
  drawTextC(bx + bw / 2, y, dct(DCS_SAFEACTIONS), 3, TH_TXT);
  y += uiLineH(3) + 8;

  // Que hace de verdad, en el mismo orden en que lo hace el motor.
  static const int ACT[4] = { DCS_OPT1, DCS_OPT2, DCS_OPT3, DCS_OPT4 };
  for(int i = 0; i < 4; i++){
    fillCircle(x + 8, y + 8, 5, wallAccent());
    drawTextClip(x + 24, y, dct(ACT[i]), 1, TH_TXT2, x + w);
    y += 24;
  }
  y += 8;
  drawTextClip(x, y, dct(DCS_OPTNOTE), 1, TH_MUTE, x + w);
  y += 20;
  drawTextClip(x, y, dct(DCS_OPTNOFW), 1, TH_MUTE, x + w);

  // Estado actual de la memoria, para que se vea el antes y el despues.
  const FlexMemSnap* s = memSnap();
  y += 30;
  char b[48], f1[24];
  if(s->psTotal){
    flexMemFmt(s->psFree, f1, sizeof(f1));
    snprintf(b, sizeof(b), "PSRAM: %s", f1);
  } else snprintf(b, sizeof(b), "PSRAM: %s", dct(DCS_NOTAVAIL));
  drawTextClip(x, y, b, 2, TH_TXT, x + w);

  int bhh = 46, byy = by + bh - pad - bhh;
  dcButton(x, byy, w, bhh, dct(DCS_OPTRUN), DCB_PLAY, TH_PRIM, TH_ONACC, DCH_OPT_RUN);
  flxFlush(WIN_TOP, WIN_BOT);
}

static void dcOptimTick(){
  if(!T.tap) return;
  int id = dcHitTest(T.x, T.y);
  T.tap = false;
  if(id == DCH_OPT_RUN) optStartCb(dcOptimDone);
}
