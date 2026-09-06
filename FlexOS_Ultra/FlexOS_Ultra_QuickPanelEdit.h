// #############################################################
// ##  FLEX OS ULTRA  ·  PANEL RAPIDO  ·  modo edicion
// ##  ----------------------------------------------------------
// ##  Reordenar, redimensionar, anadir y quitar controles sobre una COPIA
// ##  temporal que solo se confirma al guardar.
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
#include "FlexOS_Ultra_QuickPanelGlass.h"   // eslabon anterior de la cadena

// #############################################################
// ##  MODO EDICION  (One UI 8.5)
// ##  ------------------------------------------------------
// ##  Trabaja SIEMPRE sobre una copia temporal (qpEdIt/qpEdN/
// ##  qpEdGrows). "Cancelar" la tira; "Listo" valida, normaliza,
// ##  guarda en NVS de UNA sola vez y sale. Nunca se guarda un
// ##  movimiento suelto, asi que un reinicio en mitad de la
// ##  edicion deja la configuracion anterior INTACTA.
// #############################################################
static void qpEditEnter(){
  if(!qpLoaded) qpLoad();
  if(flexOtaOwnsScreen() || flexOtaOverlayActive()) return;   // la pantalla es del OTA
  memcpy(qpEdIt, qpIt, sizeof(qpEdIt));
  qpEdN = qpN; qpEdGrows = qpGrows;
  qpEdDrag = -1; qpEdResize = -1; qpEdRejectF = -1;
  qpScrollF = 0; qpScrollVel = 0;
  qpG = QG_NONE;
  qpMode = QPM_EDIT;
  qpRelayout();
  qpMarkAll();
}
static void qpEditCancel(){
  qpMode = QPM_PANEL;
  qpEdDrag = -1; qpEdResize = -1;
  qpG = QG_NONE;
  qpScrollF = 0; qpScrollVel = 0;
  qpRelayout();
  qpMarkAll();
}
static void qpEditCommit(){
  qpEdN = qpNormalize(qpEdIt, qpEdN, qpEdGrows);
  if(qpEdN == 0){ qpFactory(); qpN = qpNormalize(qpIt, qpN, qpGrows); }
  else { memcpy(qpIt, qpEdIt, sizeof(qpIt)); qpN = qpEdN; qpGrows = qpEdGrows; }
  qpSave();
  qpGH = (float)qpGroupH(qpGrows);
  qpGScrollF = 0; qpGScrollVel = 0;
  qpMode = QPM_PANEL;
  qpEdDrag = -1; qpEdResize = -1;
  qpG = QG_NONE;
  qpScrollF = 0; qpScrollVel = 0;
  qpRelayout();
  qpMarkAll();
}
static void qpEditReset(){
  QpItem save[QP_MAX_ITEMS]; uint8_t sn = qpN, sg = qpGrows;
  memcpy(save, qpIt, sizeof(save));
  qpFactory();                                  // escribe en qpIt/qpGrows
  memcpy(qpEdIt, qpIt, sizeof(qpEdIt));
  qpEdN = qpNormalize(qpEdIt, qpN, qpGrows);
  qpEdGrows = qpGrows;
  memcpy(qpIt, save, sizeof(save)); qpN = sn; qpGrows = sg;   // lo vivo no cambia hasta "Listo"
  qpEdDrag = -1; qpEdResize = -1;
  qpRelayout();
  qpMarkAll();
}
// Quita un elemento de la copia temporal. Nunca deja el panel vacio: si es el
// ultimo, se rechaza con el mismo destello que un tamano incompatible.
static bool qpEditRemove(int idx){
  if(idx < 0 || idx >= qpEdN) return false;
  if(qpEdN <= 1){ qpEdRejectF = qpEdIt[idx].id; qpEdRejectMs = millis(); return false; }
  for(int i = idx; i < qpEdN - 1; i++) qpEdIt[i] = qpEdIt[i + 1];
  qpEdN--;
  qpEdIt[qpEdN].id = 0; qpEdIt[qpEdN].vis = 0;
  qpRelayout(); qpMarkAll();
  return true;
}
// Mueve un elemento a otra posicion (reordenamiento EN TIEMPO REAL).
static void qpEditMove(int from, int to){
  if(from == to || from < 0 || to < 0 || from >= qpEdN || to >= qpEdN) return;
  QpItem tmp = qpEdIt[from];
  if(from < to) for(int i = from; i < to; i++) qpEdIt[i] = qpEdIt[i + 1];
  else          for(int i = from; i > to; i--) qpEdIt[i] = qpEdIt[i - 1];
  qpEdIt[to] = tmp;
}
// Anade un control del catalogo en el PRIMER hueco valido (el final del
// flujo). Si no cabe, no se pierde: el contenido desplazable crece.
static bool qpEditAdd(int id){
  if(qpEdN >= QP_MAX_ITEMS) return false;
  if(!qpCtlAvail(id)) return false;
  for(int i = 0; i < qpEdN; i++) if(qpEdIt[i].id == id) return false;
  uint8_t w, h; qpFirstSize(QS_REG[id].sizes, w, h);
  qpEdIt[qpEdN].id = (uint8_t)id; qpEdIt[qpEdN].w = w; qpEdIt[qpEdN].h = h;
  qpEdIt[qpEdN].ori = (QS_REG[id].oris & QOR_H) ? QOR_H : QOR_V;
  qpEdIt[qpEdN].vis = 1;
  qpEdN++;
  qpRelayout(); qpMarkAll();
  return true;
}

// ---- TOQUE DEL EDITOR ------------------------------------------------
#define QP_EDLONG_MS 320
static int qpEdPendIdx = -1;      // elemento bajo el dedo, aun sin decidir
static int qpEdPendBlk = -1;

// Indice (en qpEdIt) del elemento bajo el punto, o -1. Cubre tanto los
// bloques exteriores como los circulos de la tarjeta.
static int qpEdItemAt(int px, int py, int &blkOut){
  blkOut = -1;
  int b = qpBlockAt(px, py);
  if(b < 0) return -1;
  blkOut = b;
  if(qpBlk[b].kind == QB_ITEM) return qpBlk[b].item;
  if(qpBlk[b].kind == QB_GROUP){
    int k = qpTileAt(px, py);
    if(k >= 0 && k < qpTileN) return qpTiles[k];
  }
  return -1;
}

static bool qpEditTouch(){
  uint32_t now = millis();
  if(T.pressed && qpG == QG_NONE){
    qpGx0 = T.x; qpGy0 = T.y; qpGPrevY = T.y; qpGPrevMs = now;
    qpGLong = false; qpEdPendIdx = -1; qpEdPendBlk = -1;
    qpScrollVel = 0;
    // 1) cabecera fija del editor
    if(T.y < QP_EDH_H){
      qpG = QG_PENDING;
      return true;
    }
    int blk = -1;
    int idx = qpEdItemAt(T.x, T.y, blk);
    if(blk >= 0 && qpBlk[blk].kind == QB_ADD){ qpG = QG_PENDING; qpEdPendBlk = blk; return true; }
    if(idx >= 0 && blk >= 0){
      int top = QP_VIEW_Y0 - (int)(qpScrollF + 0.5f);
      int bx = qpBlk[blk].x, by = top + qpBlk[blk].y, bw = qpBlk[blk].w, bh = qpBlk[blk].h;
      if(qpBlk[blk].kind == QB_ITEM){
        // "-" (quitar): esquina superior izquierda, zona tactil de 44 px
        if(T.x <= bx + 26 && T.y <= by + 26){ qpEditRemove(idx); qpG = QG_NONE; return true; }
        // Asa de redimension: borde DERECHO
        if(T.x >= bx + bw - 26){
          uint8_t nw, nh;
          if(qpNextSize(qpEdIt[idx].id, qpEdIt[idx].w, qpEdIt[idx].h, +1, nw, nh) ||
             qpNextSize(qpEdIt[idx].id, qpEdIt[idx].w, qpEdIt[idx].h, -1, nw, nh)){
            qpG = QG_EDDRAG; qpEdResize = idx; qpEdResX0 = T.x; qpEdDrag = -1;
            return true;
          }
          qpEdRejectF = qpEdIt[idx].id; qpEdRejectMs = now; qpMarkAll(); qpG = QG_NONE; return true;
        }
        // Boton de orientacion: esquina inferior izquierda
        const QsCtl* c = qpCtl(qpEdIt[idx].id);
        if(c && c->oris == (QOR_H | QOR_V) && T.x <= bx + 30 && T.y >= by + bh - 30){
          qpEdIt[idx].ori = (qpEdIt[idx].ori == QOR_H) ? QOR_V : QOR_H;
          qpRelayout(); qpMarkAll(); qpG = QG_NONE; return true;
        }
      } else {
        // Circulo de la tarjeta: el "-" va sobre su esquina superior izquierda
        // y el asa de redimension en el borde derecho de su celda.
        int k = qpTileAt(T.x, T.y);
        if(k >= 0){
          int gy = top + qpBlk[blk].y;
          int gyTop = gy + QP_GPAD - (int)(qpGScrollF + 0.5f);
          int cx, cy; qpTileCenter(k, gyTop, cx, cy);
          if(T.x <= cx - QP_TCIRC / 2 + 20 && T.y <= cy - QP_TCIRC / 2 + 20){
            qpEditRemove(idx); qpG = QG_NONE; return true;
          }
          if(T.x >= cx + QP_TCOLW / 2 - 22 && T.y >= cy - 24 && T.y <= cy + 24){
            uint8_t nw, nh;
            if(qpNextSize(qpEdIt[idx].id, qpEdIt[idx].w, qpEdIt[idx].h, +1, nw, nh) ||
               qpNextSize(qpEdIt[idx].id, qpEdIt[idx].w, qpEdIt[idx].h, -1, nw, nh)){
              qpG = QG_EDDRAG; qpEdResize = idx; qpEdResX0 = T.x; qpEdDrag = -1;
              return true;
            }
            qpEdRejectF = qpEdIt[idx].id; qpEdRejectMs = now; qpMarkAll();
            qpG = QG_NONE; return true;
          }
        }
      }
      qpEdPendIdx = idx; qpEdPendBlk = blk; qpG = QG_PENDING;
      return true;
    }
    qpG = QG_PENDING;
    return true;
  }

  // ---- REDIMENSION POR EL BORDE DERECHO ----
  if(qpG == QG_EDDRAG){
    if(T.down){
      int d = T.x - qpEdResX0;
      if(abs(d) >= 46 && qpEdResize >= 0 && qpEdResize < qpEdN){
        uint8_t nw, nh;
        if(qpNextSize(qpEdIt[qpEdResize].id, qpEdIt[qpEdResize].w, qpEdIt[qpEdResize].h,
                      d > 0 ? +1 : -1, nw, nh)){
          qpEdIt[qpEdResize].w = nw; qpEdIt[qpEdResize].h = nh;
          qpEdResX0 = T.x;
          qpRelayout(); qpMarkAll();                 // feedback: el bloque salta al tamano valido
        } else {
          qpEdRejectF = qpEdIt[qpEdResize].id; qpEdRejectMs = now;   // rechazo limpio
          qpEdResX0 = T.x;
          qpMarkAll();
        }
      }
      return true;
    }
    qpG = QG_NONE; qpEdResize = -1;
    return true;
  }

  // ---- ARRASTRE DE UN ELEMENTO (mover) ----
  if(qpG == QG_SCROLL && qpEdDrag >= 0){
    if(T.down){
      qpEdDragX = T.x; qpEdDragY = T.y;
      // Desplazamiento automatico cerca de los bordes del editor. La velocidad
      // va en px/ms y se multiplica por el TIEMPO transcurrido: el editor se
      // desplaza igual de rapido a 30 que a 60 fps.
      uint32_t dt = now - qpGPrevMs; if(dt < 1) dt = 1; if(dt > 100) dt = 100;
      qpGPrevMs = now;
      const float AUTO = 0.45f;                       // px/ms (~450 px/s)
      if(T.y < QP_EDH_H + 60)   { qpScrollF -= AUTO * (float)dt; qpClampScroll(false); }
      else if(T.y > SCR_H - 70) { qpScrollF += AUTO * (float)dt; qpClampScroll(false); }
      int blk = -1;
      int tgt = qpEdItemAt(T.x, T.y, blk);
      if(tgt >= 0 && tgt != qpEdDrag){
        qpEditMove(qpEdDrag, tgt);                   // reordenamiento en TIEMPO REAL
        qpEdDrag = tgt;
        qpRelayout();
      }
      qpMarkAll();
      return true;
    }
    qpEdDrag = -1; qpG = QG_NONE;
    qpRelayout(); qpMarkAll();
    return true;
  }

  // ---- SCROLL DEL EDITOR ----
  if(qpG == QG_SCROLL){
    if(T.down){
      uint32_t dt = now - qpGPrevMs; if(dt < 1) dt = 1; if(dt > 100) dt = 100;
      int d = qpGPrevY - T.y;
      qpScrollF += d; qpClampScroll(true);
      qpScrollVel += ((float)d / (float)dt - qpScrollVel) * ((float)dt / ((float)dt + 45.0f));
      qpGPrevY = T.y; qpGPrevMs = now;
      qpMarkAll();
      return true;
    }
    qpG = QG_NONE;
    return true;
  }

  // ---- PENDIENTE ----
  if(qpG == QG_PENDING){
    if(T.down){
      if(qpEdPendIdx >= 0 && !qpGLong && (now - T.downMs) > QP_EDLONG_MS){
        qpGLong = true;                              // mantener pulsado -> mover
        qpEdDrag = qpEdPendIdx;
        qpEdDragX = T.x; qpEdDragY = T.y;
        qpG = QG_SCROLL;                             // reutiliza la rama de arrastre
        qpMarkAll();
        return true;
      }
      if(abs(T.y - qpGy0) > QP_DRAG_TH){
        qpG = QG_SCROLL; qpEdDrag = -1;
        qpGPrevY = T.y; qpGPrevMs = now;
        return true;
      }
      return true;
    }
    uint8_t g = qpG; qpG = QG_NONE; (void)g;
    if(qpGLong) return true;
    // Cabecera: Cancelar / Listo / Restablecer
    if(qpGy0 < QP_EDH_H){
      if(qpGy0 >= QP_EDH_BTN_Y0 && qpGy0 <= QP_EDH_BTN_Y1){
        if(qpGx0 < 150){ qpEditCancel(); return true; }
        if(qpGx0 > SCR_W - 150){ qpEditCommit(); return true; }
        return true;
      }
      if(qpGy0 >= QP_EDH_RST_Y0 && qpGy0 <= QP_EDH_RST_Y1 &&
         qpGx0 > SCR_W / 2 - 110 && qpGx0 < SCR_W / 2 + 110){ qpEditReset(); return true; }
      return true;
    }
    if(qpEdPendBlk >= 0 && qpEdPendBlk < qpBlkN && qpBlk[qpEdPendBlk].kind == QB_ADD){
      qpCatBuild(); qpCatSel = -1; qpCatScrollF = 0;
      qpMode = QPM_CAT; qpMarkAll();
      return true;
    }
    return true;
  }

  if(!T.down && qpG != QG_NONE) qpG = QG_NONE;
  return true;
}

// ---- TOQUE DEL CATALOGO ----------------------------------------------
static int qpCatAt(int px, int py){
  if(py < QP_CAT_HDR) return -1;
  int top = QP_CAT_HDR - (int)(qpCatScrollF + 0.5f) + 12;
  for(int k = 0; k < qpCatN; k++){
    int r = k / 4, c = k % 4;
    int cx = qpColX(c) + QP_CW / 2, cy = top + r * QP_CAT_ROW + QP_TCIRC / 2;
    int hw = QP_CW / 2; if(hw < QP_TOUCH_MIN / 2) hw = QP_TOUCH_MIN / 2;
    if(px >= cx - hw && px <= cx + hw && py >= cy - QP_TCIRC / 2 - 6 && py <= cy + QP_CAT_ROW / 2)
      return k;
  }
  return -1;
}
static bool qpCatTouch(){
  uint32_t now = millis();
  if(T.pressed && qpG == QG_NONE){
    qpGx0 = T.x; qpGy0 = T.y; qpGPrevY = T.y; qpGPrevMs = now; qpGLong = false;
    qpG = QG_PENDING;
    return true;
  }
  if(qpG == QG_CATSCROLL){
    if(T.down){
      int d = qpGPrevY - T.y;
      qpCatScrollF += d;
      int mx = qpCatScrollMax();
      if(qpCatScrollF < 0) qpCatScrollF = qpRubber(qpCatScrollF);
      if(qpCatScrollF > mx) qpCatScrollF = mx + qpRubber(qpCatScrollF - mx);
      qpGPrevY = T.y; qpGPrevMs = now;
      qpMarkAll();
      return true;
    }
    int mx = qpCatScrollMax();
    if(qpCatScrollF < 0) qpCatScrollF = 0;
    if(qpCatScrollF > mx) qpCatScrollF = (float)mx;
    qpG = QG_NONE; qpMarkAll();
    return true;
  }
  if(qpG == QG_PENDING){
    if(T.down){
      if(abs(T.y - qpGy0) > QP_DRAG_TH){ qpG = QG_CATSCROLL; qpGPrevY = T.y; qpGPrevMs = now; }
      return true;
    }
    qpG = QG_NONE;
    if(qpGy0 < QP_CAT_HDR){
      // "Atras": vuelve al editor SIN perder lo ya editado.
      if(qpGx0 < 150){ qpMode = QPM_EDIT; qpRelayout(); qpMarkAll(); }
      return true;
    }
    int k = qpCatAt(qpGx0, qpGy0);
    if(k >= 0 && k < qpCatN){
      int id = qpCatIds[k];
      qpCatSel = k;
      if(qpEditAdd(id)){
        // Vuelve al editor con el control ya colocado: la animacion es la del
        // propio repintado, que lo ensena en su nueva posicion.
        qpCatBuild(); qpCatSel = -1;
        qpMode = QPM_EDIT; qpScrollF = (float)qpScrollMax();
        qpRelayout(); qpMarkAll();
      } else qpMarkAll();
    }
    return true;
  }
  if(!T.down && qpG != QG_NONE) qpG = QG_NONE;
  return true;
}

// ---- AVANCE POR CUADRO ------------------------------------------------
// SEPARACION ESTRICTA DE ETAPAS. El orden de una vuelta de loop() es:
//   1. lectura de touch      -> flexPollTouch()
//   2. actualizacion de estado -> qsGlobalHandle() (no dibuja NADA)
//   3. animacion posterior al gesto -> qsAnimStep / qpGroupAnimStep /
//      qpScrollAnimStep, todas con el tiempo transcurrido acotado
//   4. composicion Liquid Glass (perezosa) + render -> qsRender(), UNA vez
// Antes las etapas 2 y 4 estaban mezcladas: el manejador tactil llamaba a
// qsRender() y uiTick() llamaba a otro, o sea dos publicaciones por vuelta,
// cada una esperando al DMA2D.
static void qsAnimStep(){
  if(!qsAnimOn) return;
  uint32_t e = millis() - qsAnimT0; if(e > qsAnimDur) e = qsAnimDur;
  float p = (float)e / (float)qsAnimDur;
  float ip = 1.0f - p;
  p = (p < 0.5f) ? (4.0f * p * p * p) : (1.0f - 4.0f * ip * ip * ip);
  int ny = qsAnimFrom + (int)((qsAnimDest - qsAnimFrom) * p + (qsAnimDest > qsAnimFrom ? 0.5f : -0.5f));
  if(ny < 0) ny = 0; if(ny > SCR_H) ny = SCR_H;
  if(ny != qsPanelY){
    int lo = ny < qsPanelY ? ny : qsPanelY;
    int hi = ny > qsPanelY ? ny : qsPanelY;
    qsPanelY = ny; qsPosF = (float)ny;
    qpMark(lo - QS_HANDLE_MARGIN, hi + QS_SHADOW_H);
  }
  if(e < qsAnimDur) return;
  qsAnimOn = false;
  qsPanelY = qsAnimDest; qsPosF = (float)qsAnimDest;
  if(qsAnimDest <= 0) qsSettleClosed();     // aqui SI se vuelca el fondo entero, una vez
  else                qpMark(0, SCR_H - 1);
}
// Cancela la animacion en curso y devuelve el control al dedo desde donde
// este el panel AHORA. Sin esto, tocar durante el "snap" no hacia nada hasta
// que la animacion terminaba.
static void qsAnimCancel(){
  qsAnimOn = false;
  qsPosF = (float)qsPanelY;
}
static uint32_t qpTickMs = 0;
static void qsTick(){
  uint32_t now = millis();
  uint32_t dt = now - qpTickMs; if(dt < 1) dt = 1; if(dt > 100) dt = 100;   // dt acotado
  qpTickMs = now;
  // 3) animaciones (solo despues del gesto; con el dedo abajo no corren)
  if(qsAnimOn) qsAnimStep();
  else if(qsPanelY > 0){
    qpGroupAnimStep();
    qpScrollAnimStep(dt);
  }
  // 4) composicion + publicacion: UN solo punto de render en todo el panel
  if(qsPanelY > 0 || qsLastY > 0) qsRender(false);
  // 5) trabajo diferido que NO puede ocurrir dentro del gesto ni antes de
  //    publicar: una escritura de flash son decenas de milisegundos.
  if(qpSavePanel){ qpSavePanel = false; qpSave(); }
  if(qpSavePrefs){ qpSavePrefs = false; cfgSavePrefs(); }
}

// ---- CIERRE LIMPIO Y OBLIGATORIO -------------------------------------
// Lo llama TODO cambio de estado que pueda dejar la cortina a medias: abrir
// o cerrar una app, volver al escritorio, bloquear, apagar la pantalla,
// apagar el equipo, entrar en Modo PC, multitarea, kiosco y el OTA. Deja la
// cortina en 0, ABANDONA el editor sin guardar nada, suelta el toque y
// LIBERA los buffers temporales (fondo cacheado y captura de la app).
static void qsForceClose(){
  bool wasOpen = (qsPanelY != 0) || qsDragging || qsAnimOn;
  qsAnimOn = false; qsDragging = false; qsDragMoved = false;
  qsPanelY = 0; qsPosF = 0; qsLastY = 0; qsVel = 0;
  qpMode = QPM_PANEL;                 // una edicion a medias se descarta ENTERA
  qpEdDrag = -1; qpEdResize = -1; qpEdRejectF = -1;
  qpG = QG_NONE; qpGAnim = false;
  qpScrollF = 0; qpScrollVel = 0; qpGScrollVel = 0;
  qpFlashIdx = -1; qpFlashKind = -1;
  qpDy0 = 0x7FFF; qpDy1 = -1;
  qpCy0 = 0x7FFF; qpCy1 = -1;
  qsComposedTo = -1;
  if(wasOpen) qpProfReport("cierre forzado");
  qpFreeBuffers();
  if(wasOpen){
    qsDirty = true;
    T.tap = false; T.pressed = false; T.released = false; T.moved = false;
    T.swipeUp = T.swipeDown = T.swipeLeft = T.swipeRight = false;
  }
}
// Resolucion de un TOQUE sobre el panel abierto: primero los circulos de la
// tarjeta, despues los modulos exteriores. Devuelve true si alguien lo
// consumio. Conserva el nombre historico porque es el punto por el que pasa
// todo toque de control del panel.
static bool qsTapTile(int px, int py){
  int k = qpTileAt(px, py);
  if(k >= 0 && k < qpTileN){
    int id = qpIt[qpTiles[k]].id;
    qpFlashTileK(k);
    qpExecCtl(id, false);
    return true;
  }
  int b = qpBlockAt(px, py);
  if(b >= 0 && qpBlk[b].kind == QB_ITEM){
    int i = qpBlk[b].item;
    if(i >= 0 && i < qpN){
      qpFlashBlock(b);
      qpExecCtl(qpIt[i].id, false);
      return true;
    }
  }
  return false;
}

// ---- PUNTO DE ENTRADA GLOBAL -----------------------------------------
static bool qsCanOpen(){
  if(gLand)   return false;    // Modo PC / DeX horizontal: version propia pendiente
  if(gHosted) return false;    // app dentro de una ventana de DeX
  if(editMode) return false;   // Modo Edicion del Home tiene su propio arrastre
  if(KIOSK_ON && kioskOn) return false;
  if(flexOtaOwnsScreen() || flexOtaOverlayActive()) return false;
  return (gState == ST_HOME || gState == ST_APP);
}
static bool qsHandle(){
  // A medio abrir SOLO manda el arrastre de la cortina: los controles no
  // existen todavia como superficie tocable.
  if(qsPanelY < SCR_H && qpG != QG_CURTAIN && qpG != QG_NONE) qpG = QG_NONE;
  if(qsPanelY >= SCR_H){
    if(qpMode == QPM_EDIT) return qpEditTouch();
    if(qpMode == QPM_CAT)  return qpCatTouch();
  }
  return qpPanelTouch();
}
static bool qsGlobalHandle(){
  if(!qsCanOpen()){
    if(qsPanelY != 0 || qsDragging || qsAnimOn) qsForceClose();
    return false;
  }
  // Tocar durante el "snap" de apertura o de cierre CANCELA la animacion y
  // devuelve el control al dedo en el acto, desde donde este el panel.
  if(qsAnimOn){
    if(!T.pressed) return true;
    qsAnimCancel();
    qsDragging = true; qsDragMoved = false;
    qsDragBase = qsPanelY; qsDragY0 = T.y; qsPosF = (float)qsPanelY;
    qsPrevY = T.y; qsPrevMs = millis(); qsVel = 0;
    qpG = QG_CURTAIN;
    return true;
  }
  if(qsPanelY > 0 || qsDragging) return qsHandle();
  if(!(T.pressed && T.startY < QS_EDGE_H)) return false;
  if(gState == ST_APP){
    if(!qsCaptureApp()) return false;              // sin PSRAM: la app conserva el gesto
  } else if(qsOverApp){
    qsFreeApp();
    qsDirty = true;
  }
  qpLoad();
  qpMode = QPM_PANEL;
  qpGH = (float)qpGroupH(qpGrows);
  qpScrollF = 0; qpScrollVel = 0;
  qpGScrollF = 0; qpGScrollVel = 0;
  qpGAnim = false; qpLastMin = -1;
  qpRelayout();
  qpProfReset();
  qsComposedTo = -1;
  qpCy0 = 0x7FFF; qpCy1 = -1;
  qsDirty = true;
  // Agarre: el dedo mueve un DELTA 1:1 desde la posicion actual.
  qsAnimOn = false; qsDragging = true; qsDragMoved = false;
  qsDragBase = qsPanelY; qsDragY0 = T.y; qsPosF = (float)qsPanelY;
  qsPrevY = T.y; qsPrevMs = millis(); qsVel = 0;
  qpG = QG_CURTAIN;
  return true;
}
