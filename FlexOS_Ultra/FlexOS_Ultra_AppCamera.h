// #############################################################
// ##  FLEX OS ULTRA  ·  APP CAMARA
// ##  ----------------------------------------------------------
// ##  Interfaz de camara con zoom digital por recorte-central y su ciclo
// ##  de vida (una app en segundo plano no graba).
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
#include "FlexOS_Ultra_AppMultimedia.h"   // eslabon anterior de la cadena

// #############################################################
// ##  APP CAMARA (ESQUELETO estilo iPhone)
// ##  Zoom digital x1..x50 por recorte-central + reescalado (funciona
// ##  sobre una ESCENA SINTETICA en PSRAM). EIS = scaffold (offset).
// ##  FUENTE = escena generada. Para camara real, sustituir camGenScene()
// ##  por la captura del sensor (esp_cam / DVP) hacia camScene.
// #############################################################
#define CAM_SW SCR_W
#define CAM_SH SCR_H
// BASE INFERIOR de la interfaz de la Camara. La franja de la barra de
// navegacion del sistema es intocable, asi que todo el grupo de controles de
// abajo (niveles de zoom, regleta, disparador y modos) se mide desde aqui y no
// desde el borde fisico. Es una sola constante: dibujo y hit-test la comparten,
// asi que no pueden quedar descolocados uno respecto del otro.
#define CAM_BASE (SCR_H - navBarH())
static uint16_t* camScene = NULL;   // "sensor" simulado en PSRAM
static float camZoom = 1.0f;        // x1..x50
static bool  camRec = false, camNight = false, camRaw = false;
static int   camMode = 0;           // 0 FOTO,1 VIDEO,2 CINE,3 ACCION,4 PRORES
static int   camExpo = 50;          // 0..100
static int   camEisX = 0, camEisY = 0;   // <<< EIS: offset suavizado (real: de vectores de movimiento)

// <<< HOOK DE CAMARA REAL >>>
// Esta placa (JC4880P443C) SI tiene camara (MIPI-CSI en el ESP32-P4). Para
// capturar de verdad hace falta el driver del sensor (esp_video / esp_cam_sensor
// de ESP-IDF) y el MODELO exacto del sensor + su pinout, que no puedo adivinar.
// Cuando lo tengas: pon CAM_HAS_SENSOR en 1 e implementa camCapture() para volcar
// un frame del sensor frontal en 'dst' (CAM_SW x CAM_SH, RGB565). El resto del
// pipeline (zoom, EIS, UI, grabacion) ya esta listo y usara esos frames reales.
#define CAM_HAS_SENSOR 0
static bool camCapture(uint16_t* dst){
#if CAM_HAS_SENSOR
  // TODO: capturar frame del sensor aqui -> dst ; return true si es valido
  (void)dst; return false;
#else
  (void)dst; return false;   // sin sensor configurado -> patron de prueba
#endif
}
// Patron de prueba honesto (NO es la camara): barras de color + anillos para
// el zoom + aviso "SIN SENAL". Se muestra hasta cablear camCapture().
static void camGenScene(){
  gClipY0 = 0; gClipY1 = SCR_H - 1;
  setBuf(camScene);
  uint16_t bars[7] = { rgb565(200,200,200), rgb565(210,210,0), rgb565(0,200,210),
                       rgb565(0,200,0), rgb565(210,0,210), rgb565(210,0,0), rgb565(0,0,210) };
  int bw = CAM_SW / 7;
  for(int i = 0; i < 7; i++) fillRect(i * bw, 0, (i == 6 ? CAM_SW - 6 * bw : bw), CAM_SH, bars[i]);
  for(int r = 20; r < 300; r += 20) drawCircle(CAM_SW / 2, CAM_SH / 2, r, rgb565(255,255,255));  // detalle para zoom
  strokeSeg(CAM_SW / 2 - 30, CAM_SH / 2, CAM_SW / 2 + 30, CAM_SH / 2, 2, rgb565(255,255,255));
  strokeSeg(CAM_SW / 2, CAM_SH / 2 - 30, CAM_SW / 2, CAM_SH / 2 + 30, 2, rgb565(255,255,255));
  fillRoundRect(CAM_SW / 2 - 150, CAM_SH / 2 - 58, 300, 116, 16, rgb565(14,14,20));
  drawTextC(CAM_SW / 2, CAM_SH / 2 - 40, "SIN SE\xC3\x91" "AL", 4, rgb565(255,255,255));
  drawTextC(CAM_SW / 2, CAM_SH / 2 + 8, "esperando sensor de camara", 2, rgb565(200,205,215));
  setBuf(fb);
}
// Bucle de procesamiento de pixeles: recorta la region central segun el
// zoom y la reescala a pantalla completa (zoom digital real).
static void camRenderPreview(){
  int cw = (int)(CAM_SW / camZoom); if(cw < 4) cw = 4;
  int ch = (int)(CAM_SH / camZoom); if(ch < 4) ch = 4;
  int cx0 = (CAM_SW - cw) / 2 + camEisX, cy0 = (CAM_SH - ch) / 2 + camEisY;
  if(cx0 < 0) cx0 = 0; if(cx0 + cw > CAM_SW) cx0 = CAM_SW - cw;
  if(cy0 < 0) cy0 = 0; if(cy0 + ch > CAM_SH) cy0 = CAM_SH - ch;
  for(int py = 0; py < SCR_H; py++){
    int sy = cy0 + py * ch / SCR_H;
    uint16_t* srow = camScene + (size_t)sy * CAM_SW;
    uint16_t* drow = fb + (size_t)py * SCR_W;
    for(int px = 0; px < SCR_W; px++){ int sx = cx0 + px * cw / SCR_W; drow[px] = srow[sx]; }
  }
  setBuf(fb);
  // EXCEPCION DELIBERADA: el HUD de la Camara vive sobre el VISOR (una imagen
  // que ocupa toda la pantalla y cuyo color no controla el sistema). Como en
  // cualquier camara, sus controles son blancos sobre velos negros
  // semitransparentes: es la unica combinacion legible sea cual sea la escena,
  // y retenirla con la paleta clara daria texto casi negro sobre la foto.
  // Igual criterio que el lienzo de Paint o los colores de los juegos.
  if(camNight) fillRectA(0, 0, SCR_W, SCR_H, rgb565(10,20,45), 120);          // modo noche
  if(camExpo > 55) fillRectA(0, 0, SCR_W, SCR_H, rgb565(255,255,255), (camExpo - 55) * 3);
  else if(camExpo < 45) fillRectA(0, 0, SCR_W, SCR_H, rgb565(0,0,0), (45 - camExpo) * 3);
}
static void camDrawUI(){
  setBuf(fb);
  uint16_t W = rgb565(255,255,255);
  strokeSegAA(30, 26, 18, 18, 2.4f, W); strokeSegAA(18, 18, 30, 10, 2.4f, W);   // back
  fillRoundRectA(60, 8, 40, 36, 10, camNight ? rgb565(60,110,235) : rgb565(0,0,0), camNight ? 255 : 90);  // noche
  fillCircle(80, 26, 9, camNight ? rgb565(255,255,255) : rgb565(205,210,220));
  fillCircle(84, 22, 3, camNight ? rgb565(60,110,235) : rgb565(0,0,0));
  fillRoundRectA(108, 8, 50, 36, 10, camRaw ? rgb565(240,160,40) : rgb565(0,0,0), camRaw ? 255 : 90);     // RAW
  drawTextC(133, 16, "RAW", 2, camRaw ? rgb565(30,30,30) : rgb565(220,220,225));
  { char z[12]; snprintf(z, sizeof(z), "%.1fx", (double)camZoom);
    fillRoundRectA(SCR_W / 2 - 34, 8, 68, 32, 16, rgb565(0,0,0), 110); drawTextC(SCR_W / 2, 14, z, 2, W); }
  { int ex = SCR_W - 26, ey = 130, eh = 280;                                    // exposicion (vertical)
    fillRoundRectA(ex - 8, ey - 22, 32, eh + 44, 14, rgb565(0,0,0), 80);
    fillCircle(ex + 8, ey - 10, 4, rgb565(255,230,120));
    vLine(ex + 8, ey, eh, rgb565(120,124,140));
    int ky = ey + eh - (camExpo * eh / 100); fillCircle(ex + 8, ky, 9, W); }
  { const char* lb[5] = { "0.5x", "1x", "2x", "5x", "50x" }; float lv[5] = { 0.5f, 1, 2, 5, 50 };
    int bw = 56, g = 8, tot = 5 * bw + 4 * g, sx = (SCR_W - tot) / 2, y = CAM_BASE - 192;
    for(int i = 0; i < 5; i++){ int x = sx + i * (bw + g); bool on = fabsf(camZoom - lv[i]) < 0.05f;
      fillRoundRectA(x, y, bw, 34, 16, on ? rgb565(240,200,60) : rgb565(0,0,0), on ? 255 : 100);
      drawTextC(x + bw / 2, y + 9, lb[i], 2, on ? rgb565(30,30,30) : W); } }
  { int zx = 40, zy = CAM_BASE - 150, zw = SCR_W - 80;                          // zoom manual (horizontal)
    fillRoundRect(zx, zy, zw, 6, 3, rgb565(70,74,88));
    fillCircle(zx + (int)((camZoom - 1) / 49.0f * zw), zy + 3, 9, W); }
  { const char* md[5] = { "FOTO", "VIDEO", "CINE", "ACCION", "PRORES" }; int y = CAM_BASE - 30, cw2 = SCR_W / 5;
    for(int i = 0; i < 5; i++) drawTextC(cw2 * i + cw2 / 2, y, md[i], 2, i == camMode ? rgb565(255,220,60) : rgb565(170,176,190)); }
  { int cbx = SCR_W / 2, cby = CAM_BASE - 84; bool vidmode = (camMode >= 1);     // boton captura
    drawCircle(cbx, cby, 34, W); drawCircle(cbx, cby, 33, W);
    if(camRec && vidmode) fillRoundRect(cbx - 12, cby - 12, 24, 24, 5, rgb565(230,60,60));
    else fillCircle(cbx, cby, 27, vidmode ? rgb565(230,60,60) : W); }
  if(camRec){ fillCircle(SCR_W / 2 - 42, 60, 6, rgb565(230,60,60)); drawText(SCR_W / 2 - 30, 54, "REC", 2, rgb565(230,60,60)); }
}
static void camRenderAll(){ gClipY0 = 0; gClipY1 = SCR_H - 1; camRenderPreview(); camDrawUI(); flxFlushAll(); }
// El buffer del "sensor" en PSRAM, aparte del reinicio de ajustes. Los dos
// caminos que lo necesitan -- abrir la app y volver a ella despues de que el
// gestor de memoria lo soltara -- comparten ESTA funcion, y solo el primero
// toca los ajustes del usuario.
static bool camEnsureScene(){
  if(camScene) return true;
  camScene = (uint16_t*)heap_caps_malloc((size_t)CAM_SW * CAM_SH * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if(camScene && !camCapture(camScene)) camGenScene();   // camara real si hay sensor; si no, patron
  return camScene != NULL;
}
static void camEnter(){
  camEnsureScene();
  // ABRIR la app SI reinicia los ajustes: es una sesion nueva.
  camZoom = 1.0f; camRec = false; camMode = 0; camNight = false; camRaw = false; camExpo = 50; camEisX = 0; camEisY = 0;
  if(camScene) camRenderAll();
}
static void camTick(){
  if(!camScene) return;
#if CAM_HAS_SENSOR
  static unsigned long camMs = 0;                       // streaming en vivo (cuando haya sensor)
  if(millis() - camMs > 33){ camMs = millis(); if(camCapture(camScene)) camRenderAll(); }
#endif
  int ex = SCR_W - 26, ey = 130, eh = 280;
  if(T.down && T.x >= ex - 18 && T.x <= ex + 26 && T.y >= ey - 12 && T.y <= ey + eh + 12){
    int v = (ey + eh - T.y) * 100 / eh; if(v < 0) v = 0; if(v > 100) v = 100; camExpo = v; camRenderAll(); return;
  }
  int zx = 40, zy = CAM_BASE - 150, zw = SCR_W - 80;
  if(T.down && T.y >= zy - 14 && T.y <= zy + 18 && T.x >= zx - 14 && T.x <= zx + zw + 14){
    float z = 1 + (float)(T.x - zx) / zw * 49.0f; if(z < 1) z = 1; if(z > 50) z = 50; camZoom = z; camRenderAll(); return;
  }
  if(!T.tap) return;
  if(T.x < 48 && T.y < 48){ sysBack(); return; }
  if(T.x >= 60 && T.x <= 100 && T.y >= 8 && T.y <= 44){ camNight = !camNight; camRenderAll(); return; }
  if(T.x >= 108 && T.x <= 158 && T.y >= 8 && T.y <= 44){ camRaw = !camRaw; camRenderAll(); return; }
  { float lv[5] = { 0.5f, 1, 2, 5, 50 }; int bw = 56, g = 8, tot = 5 * bw + 4 * g, sx = (SCR_W - tot) / 2, y = CAM_BASE - 192;
    for(int i = 0; i < 5; i++){ int x = sx + i * (bw + g); if(T.x >= x && T.x <= x + bw && T.y >= y && T.y <= y + 34){ camZoom = lv[i]; camRenderAll(); return; } } }
  { int y = CAM_BASE - 30, cw2 = SCR_W / 5; if(T.y >= y - 8 && T.y <= y + 22){ int m = T.x / cw2; if(m >= 0 && m < 5){ camMode = m; if(camMode == 0) camRec = false; camRenderAll(); return; } } }
  { int cbx = SCR_W / 2, cby = CAM_BASE - 84; if(T.x >= cbx - 34 && T.x <= cbx + 34 && T.y >= cby - 34 && T.y <= cby + 34){
      if(camMode >= 1) camRec = !camRec; camRenderAll(); return; } }
}

// #############################################################
// ##  CAMARA · CICLO DE VIDA
// ##  Suspender PARA la grabacion (una app en segundo plano no graba)
// ##  y cerrar libera la escena de PSRAM: 768 KB que no tiene sentido
// ##  mantener reservados por una app que ya no esta abierta.
// #############################################################
static void camSuspend(){
  camRec = false;                        // grabar en segundo plano no es una funcion real de este OS
}
// REANUDAR NO ES ABRIR. Antes, si el gestor de memoria habia soltado camScene,
// esto caia en camEnter() y con el se iban el modo, el zoom, la exposicion, el
// modo noche y RAW: volver de Recientes reiniciaba la camara en silencio. Ahora
// se rehace SOLO el buffer -- que es justo lo que 'shed' declara reconstruible
// -- y los ajustes del usuario sobreviven.
static void camResume(){
  camRec = false;                          // grabar no sobrevive a segundo plano
  camEnsureScene();
  if(camScene) camRenderAll();
}
// SOLTAR SIN CERRAR. El "sensor" en PSRAM se regenera al volver (camResume),
// asi que en segundo plano no tiene por que estar reservado.
static size_t camShed(){
  if(!camScene) return 0;
  free(camScene); camScene = NULL;
  return 1;
}
static void camCloseApp(){
  camRec = false;
  if(camScene){ free(camScene); camScene = NULL; }
}
