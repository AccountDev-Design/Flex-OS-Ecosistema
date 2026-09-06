// #############################################################
// ##  FLEX OS ULTRA  ·  MODO PC / DeX  ·  entrada y ciclo de vida
// ##  ----------------------------------------------------------
// ##  Toques (traducidos a la app hospedada), APP_REG del modo PC y el
// ##  ciclo de vida real: que se conserva y que se suelta al suspender.
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
#include "FlexOS_Ultra_DeXDraw.h"   // eslabon anterior de la cadena

// #############################################################
// ##  ENTRADA
// #############################################################
static void dexPointer(){
  pPressed = pReleased = pTap = pLong = pDTap = false;
  int lx = T.y, ly = (SCR_W - 1) - T.x;            // fisico -> landscape
  bool wasDown = pDown;

  if(dexPadOn){
    int px, py, pw, ph; dexPadGeom(px, py, pw, ph);
    if(T.down && !wasDown && dexInBox(lx, ly, px, py, pw, ph)){
      dexPadGrab = true; dexPadLX = lx; dexPadLY = ly; dexLongFired = false;
      dexPressX = dexCurX; dexPressY = dexCurY;
    }
    if(dexPadGrab){
      if(T.down){                                  // movimiento RELATIVO del cursor
        int ncx = dexCurX + (int)((lx - dexPadLX) * 1.45f);
        int ncy = dexCurY + (int)((ly - dexPadLY) * 1.45f);
        if(ncx < 0) ncx = 0; if(ncx > LW - 1) ncx = LW - 1;
        if(ncy < 0) ncy = 0; if(ncy > LH - 1) ncy = LH - 1;
        if(ncx != dexCurX || ncy != dexCurY){
          dexMark(dexCurX - 16, dexCurX + 20);        // borrar donde estaba
          dexCurX = ncx; dexCurY = ncy;
          dexMark(dexCurX - 16, dexCurX + 20);        // pintar donde esta
          dexDirty = true;
        }
        dexPadLX = lx; dexPadLY = ly;
      }
      pX = dexCurX; pY = dexCurY;
      // El pad NO arrastra ventanas: sintetiza clic (toque) y clic derecho
      // (mantener). Arrastrar y redimensionar siguen siendo con el dedo directo.
      if(!dexLongFired && T.down && !T.moved && millis() - T.downMs > DEX_LONG_MS){
        dexLongFired = true; pLong = true;
      }
      if(!T.down && wasDown){
        if(!T.moved && !dexLongFired) pTap = true;    // mismo criterio de clic que arriba
        dexPadGrab = false; dexLongFired = false;
      }
      pDown = T.down;
      return;
    }
  }

  pX = lx; pY = ly;
  if(dexPadOn && (dexCurX != lx || dexCurY != ly) && T.down){    // el cursor sigue al dedo
    dexMark(dexCurX - 16, dexCurX + 20);
    dexCurX = lx; dexCurY = ly;
    dexMark(dexCurX - 16, dexCurX + 20);
    dexDirty = true;
  }
  pDown = T.down;
  if(T.down && !wasDown){ pPressed = true; dexLongFired = false; dexPressX = pX; dexPressY = pY; }
  if(!T.down && wasDown) pReleased = true;
  if(!dexLongFired && T.down && !T.moved && millis() - T.downMs > DEX_LONG_MS){
    dexLongFired = true; pLong = true;
  }
  // Clic = soltar sin haber arrastrado y sin que haya saltado la pulsacion larga.
  // A PROPOSITO no se usa T.tap: ese exige ademas dur < 550 ms, asi que una
  // pulsacion un poco mas lenta sobre una tecla del buscador o un icono de la
  // barra se perdia ENTERA y habia que repetirla. Aqui no hay limite superior:
  // lo que separa un clic de una pulsacion larga es dexLongFired, no el reloj.
  if(pReleased && !T.moved && !dexLongFired){
    pTap = true;
    if(millis() - dexTapMs < DEX_DTAP_MS && abs(pX - dexTapX) < 26 && abs(pY - dexTapY) < 26){
      pDTap = true; dexTapMs = 0;
    } else { dexTapMs = millis(); dexTapX = pX; dexTapY = pY; }
  }
  if(!T.down) dexLongFired = false;
}

static int dexWinAt(int x, int y){
  for(int k = 3; k >= 0; k--){
    int i = dexOrder[k];
    if(!pwins[i].open || pwins[i].mini) continue;
    if(x >= pwins[i].x - DEX_GRIP && x < pwins[i].x + pwins[i].w + DEX_GRIP &&
       y >= pwins[i].y - DEX_GRIP && y < pwins[i].y + pwins[i].h + DEX_GRIP) return i;
  }
  return -1;
}
// 8 zonas de agarre. Arriba solo cuentan los primeros px, para no robarle el
// arrastre a la barra de titulo.
static uint8_t dexResizeMask(int i, int x, int y){
  PWin* w = &pwins[i]; uint8_t m = 0;
  // Asimetrico a proposito: hacia FUERA se agarra con holgura, hacia DENTRO
  // solo unos pixeles. Con +-DEX_GRIP a ambos lados, el borde se comia 14 px de
  // area de cliente por cada lado y esos toques nunca llegaban a la app.
  const int GIN = 6;                              // hacia dentro, lo justo para no
                                                  // robarle area de cliente a la app
  if(x >= w->x - DEX_GRIP && x <= w->x + GIN) m |= 1;
  if(x >= w->x + w->w - GIN && x <= w->x + w->w + DEX_GRIP) m |= 2;
  if(y >= w->y - DEX_GRIP && y <= w->y + GIN) m |= 4;
  if(y >= w->y + w->h - GIN && y <= w->y + w->h + DEX_GRIP) m |= 8;
  if((m & 1) && (m & 2)) m &= ~2;
  if((m & 4) && (m & 8)) m &= ~8;
  return m;
}

static void dexMenuRun(uint8_t k, int i){
  dexMenuOn = false; dexMarkAll(); dexDirty = true;
  if(k == 0){
    if(i == 0) dexWall = (uint8_t)((dexWall + 1) % 3);
    else if(i == 1) dexCascade();
    else if(i == 2) dexOvOpen(DXO_RECENTS);
    else dexOvOpen(DXO_DRAWER);
    return;
  }
  if(k == 1){
    if(i == 0){ dexTbAuto = !dexTbAuto; if(dexTbAuto) dexTbIdle = millis(); else dexTbShow(); dexClampAll(); }
    else if(i == 1) dexBigIcons = !dexBigIcons;
    else if(i == 2) dexOvOpen(DXO_RECENTS);
    else pcExit();
    return;
  }
  int w = dexMenuWin;
  if(w < 0 || w > 3 || !pwins[w].open) return;
  if(i == 0) dexMinimize(w);
  else if(i == 1) dexToggleMax(w);
  else if(i == 2) dexApplySnap(w, SNAP_L);
  else if(i == 3) dexApplySnap(w, SNAP_R);
  else dexCloseWin(w);
}
static void dexMenuOpen(uint8_t kind, int x, int y, int win){
  dexMenuOn = true; dexMenuKind = kind; dexMenuX = x; dexMenuY = y; dexMenuWin = win;
  dexMarkAll(); dexDirty = true;
}

// ---- Toques dentro de cada overlay (siempre devuelven true: el overlay
//      captura el toque, como en DeX real) ----
static bool dexDrawerTouch(){
  int w = DEX_DRW_W, h = DEX_DRW_H;
  int fx = (LW - w) / 2, fy = (dexWorkBottom() - h) / 2;
  if(pTap && !dexInBox(pX, pY, fx, fy, w, h)){ dexOvClose(); return true; }   // fuera -> cerrar
  if(!pTap) return true;
  if(dexQLen > 0 && dexInBox(pX, pY, fx + w - 44, fy + 14, 32, 36)){          // limpiar
    dexQLen = 0; dexQuery[0] = 0; dexOvMark(DXO_DRAWER); dexDirty = true; return true;
  }
  if(dexKbTap(fx + 14, fy + DEX_DRW_KBY, w - 28, pX, pY)){ dexOvMark(DXO_DRAWER); dexDirty = true; return true; }
  int list[18]; int n = dexFilterApps(list, 18);
  for(int i = 0; i < n; i++){
    int x, y, s; dexDrawerGrid(fx, fy, i, x, y, s);
    if(dexInBox(pX, pY, x - 8, y - 6, s + 16, s + 24)){
      dexOvClose();
      dexOpenFrom(list[i], x, y, s);                 // crece DESDE el icono del cajon
      return true;
    }
  }
  return true;
}
static bool dexFinderTouch(){
  int w = DEX_FND_W, h = DEX_FND_H;
  int fx = (LW - w) / 2, fy = (dexWorkBottom() - h) / 2;
  if(pTap && !dexInBox(pX, pY, fx, fy, w, h)){ dexOvClose(); return true; }
  if(!pTap) return true;
  if(dexQLen > 0 && dexInBox(pX, pY, fx + w - 44, fy + 14, 32, 36)){
    dexQLen = 0; dexQuery[0] = 0; dexOvMark(DXO_FINDER); dexDirty = true; return true;
  }
  if(dexKbTap(fx + 14, fy + DEX_FND_KBY, w - 28, pX, pY)){ dexOvMark(DXO_FINDER); dexDirty = true; return true; }
  int apps[DEX_FND_APPS], sets[DEX_FND_SETS], na, ns;
  dexFinderRows(na, ns, apps, sets);
  int y = fy + 62;
  if(na > 0){
    y += 15;
    for(int i = 0; i < na; i++){
      if(dexInBox(pX, pY, fx + 14, y, w - 28, DEX_FND_ROW)){
        dexOvClose(); dexOpenFrom(apps[i], fx + 20, y + 2, 20); return true;
      }
      y += DEX_FND_ROW;
    }
    y += 6;
  }
  if(ns > 0){
    y += 15;
    for(int i = 0; i < ns; i++){
      if(dexInBox(pX, pY, fx + 14, y, w - 28, DEX_FND_ROW)){
        dexOvClose(); dexOpen(IC_AJUSTES); return true;       // los ajustes viven en su app
      }
      y += DEX_FND_ROW;
    }
  }
  return true;
}
static bool dexNotifTouch(){
  int nx, ny, nw, nh; dexNpRect(nx, ny, nw, nh);
  if(pTap && !dexInBox(pX, pY, nx, ny, nw, nh)){ dexOvClose(); return true; }
  if(!dexOvSettled()) return true;
  for(int i = 0; i < 4; i++){
    int x, y, w, h; dexNpTile(i, nx, ny, x, y, w, h);
    if(pTap && dexInBox(pX, pY, x, y, w, h)){
      // Material y apariencia son ORTOGONALES: cada uno cambia su flag y pasa
      // por el mismo themeChanged() que Ajustes (NVS + invalidacion de caches).
      if(i == 0){ uiGlass = !uiGlass; themeChanged(); }
      else if(i == 1){ gDark = !gDark; themeChanged(); }
      else if(i == 2){ dexTbAuto = !dexTbAuto; if(dexTbAuto) dexTbIdle = millis(); else dexTbShow(); dexClampAll(); }
      else { dexPadOn = !dexPadOn; if(dexPadOn){ dexCurX = LW / 2; dexCurY = LH / 2 - 40; } }
      dexMarkAll(); dexDirty = true; return true;
    }
  }
  if(pTap && dexInBox(pX, pY, nx + 12, dexNpExitY(ny, nh), nw - 24, DEX_NP_EXIT_H)){
    dexOvClose(); pcExit(); return true;                       // salida visible de DeX
  }
  int by = dexNpBrightY(ny), bx = nx + 12, bw = nw - 24, bh = 26;
  if(pDown && dexInBox(pX, pY, bx, by + 14, bw, bh)){          // brillo REAL (PWM)
    int v = (pX - bx) * 100 / bw;
    if(v < 5) v = 5; if(v > 100) v = 100;
    if(v != gBright){ setBacklight(v); cfgSavePrefs(); dexOvMark(DXO_NOTIF); dexDirty = true; }
  }
  return true;
}
static bool dexRecentsTouch(){
  int list[4]; int n = dexRecList(list);
  if(pPressed){
    dexRecDrag = -1; dexRecDY = 0; dexRecY0 = pY;
    for(int i = 0; i < n; i++){
      int x, y; dexRecCard(i, n, x, y);
      if(dexInBox(pX, pY, x, y, DEX_REC_W, DEX_REC_H)){ dexRecDrag = i; break; }
    }
    if(dexRecDrag < 0){ dexOvClose(); return true; }
    return true;
  }
  if(pDown && dexRecDrag >= 0){
    int dy = pY - dexRecY0; if(dy > 0) dy = 0;
    if(dy != dexRecDY){
      int cx, cy; dexRecCard(dexRecDrag, n, cx, cy);
      dexRecDY = dy; dexMark(cx - 10, cx + DEX_REC_W + 10); dexDirty = true;
    }
    return true;
  }
  if(pReleased && dexRecDrag >= 0){
    int idx = dexRecDrag, dy = dexRecDY;
    dexRecDrag = -1; dexRecDY = 0;
    dexMarkAll(); dexDirty = true;
    if(idx >= n) return true;
    if(dy < -45){ dexCloseWin(list[idx]); if(n <= 1) dexOvClose(); return true; }   // arriba = cerrar
    if(pTap){
      int x, y; dexRecCard(idx, n, x, y);
      if(dexInBox(pX, pY, x + DEX_REC_W - 26, y, 26, 24)){                          // X
        dexCloseWin(list[idx]); if(n <= 1) dexOvClose();
      } else { dexOvClose(); dexRestore(list[idx]); }
    }
  }
  return true;
}

static void dexInput(){
  dexPointer();
  if(!pDown && !pPressed && !pReleased && !pTap && !pLong) return;
  // La barra reaparece al acercarse al borde inferior (o al tocarla), no con
  // cualquier toque: si no, "auto-ocultar" no ocultaria nunca.
  if(pDown && (pY >= LH - DEX_TB_REVEAL || pY >= dexTbY())) dexTbShow();

  // 1) Menu contextual: se lo come todo (y se cierra al tocar fuera)
  if(dexMenuOn){
    if(!pTap) return;
    int x, y, w, h; dexMenuGeom(x, y, w, h);
    if(!dexInBox(pX, pY, x, y, w, h)){ dexMenuOn = false; dexMarkAll(); dexDirty = true; return; }
    int i = (pY - y - 5) / DEX_MENU_IH;
    if(i >= 0 && i < dexMenuCount(dexMenuKind)) dexMenuRun(dexMenuKind, i);
    return;
  }

  // 2) Arrastre / redimension en curso
  if(dexGrab == DXG_MOVE){
    PWin* w = &pwins[dexGrabWin];
    if(pDown){
      int nx = pX - dexGrabDX, ny = pY - dexGrabDY;
      int nw = w->w, nh = w->h;
      // Clamp en CADA paso del arrastre, no solo al soltar.
      flxClampRect(nx, ny, nw, nh, LW, dexWorkBottom(), DEX_MIN_W, DEX_MIN_H, DEX_KEEP);
      if(nx != w->x || ny != w->y || nw != w->w || nh != w->h){
        dexMarkWin(w->x, w->w); dexMarkWin(nx, nw);
        w->x = nx; w->y = ny; w->w = nw; w->h = nh; dexDirty = true;
      }
      uint8_t g = dexSnapHit(pX, pY);
      if(g != dexSnapGhost){ dexSnapGhost = g; dexMarkAll(); dexDirty = true; }
      return;
    }
    uint8_t g = dexSnapGhost;
    int win = dexGrabWin;
    dexGrab = DXG_NONE; dexSnapGhost = SNAP_FREE;
    if(g != SNAP_FREE) dexApplySnap(win, g);
    else { w->rx = w->x; w->ry = w->y; w->rw = w->w; w->rh = w->h; }
    dexMarkAll(); dexDirty = true;
    return;
  }
  if(dexGrab == DXG_RESIZE){
    PWin* w = &pwins[dexGrabWin];
    if(pDown){
      int dx = pX - dexGrabDX, dy = pY - dexGrabDY, H = dexWorkBottom();
      int nx = dexRzX0, ny = dexRzY0, nw = dexRzW0, nh = dexRzH0;
      if(dexRzMask & 1){ nx = dexRzX0 + dx; nw = dexRzW0 - dx; }
      if(dexRzMask & 2){ nw = dexRzW0 + dx; }
      if(dexRzMask & 4){ ny = dexRzY0 + dy; nh = dexRzH0 - dy; }
      if(dexRzMask & 8){ nh = dexRzH0 + dy; }
      if(nx < 0){ nw += nx; nx = 0; }
      if(ny < 0){ nh += ny; ny = 0; }
      if(nw < DEX_MIN_W){ if(dexRzMask & 1) nx = dexRzX0 + dexRzW0 - DEX_MIN_W; nw = DEX_MIN_W; }
      if(nh < DEX_MIN_H){ if(dexRzMask & 4) ny = dexRzY0 + dexRzH0 - DEX_MIN_H; nh = DEX_MIN_H; }
      // Forzar el minimo podia volver a sacar la ventana: con nx alto, nw subia a
      // DEX_MIN_W DESPUES de recortar contra LW y el borde derecho se salia.
      // Aqui se acota el rect entero y luego se reencaja dentro del area util:
      // redimensionar nunca debe empujar la ventana fuera de lo visible.
      flxClampRect(nx, ny, nw, nh, LW, H, DEX_MIN_W, DEX_MIN_H, DEX_KEEP);
      if(nx + nw > LW) nx = LW - nw;
      if(ny + nh > H)  ny = H - nh;
      if(nx < 0) nx = 0;
      if(ny < 0) ny = 0;
      if(nx != w->x || ny != w->y || nw != w->w || nh != w->h){
        dexMarkWin(w->x, w->w); dexMarkWin(nx, nw);
        w->x = nx; w->y = ny; w->w = nw; w->h = nh; dexDirty = true;
      }
      return;
    }
    w->rx = w->x; w->ry = w->y; w->rw = w->w; w->rh = w->h;
    dexGrab = DXG_NONE; dexMarkAll(); dexDirty = true;
    return;
  }

  // 3) Overlays
  if(dexOv != DXO_NONE && !dexOvClosing){
    if(dexOv == DXO_DRAWER  && dexDrawerTouch())  return;
    if(dexOv == DXO_FINDER  && dexFinderTouch())  return;
    if(dexOv == DXO_NOTIF   && dexNotifTouch())   return;
    if(dexOv == DXO_RECENTS && dexRecentsTouch()) return;
  }

  // 4) Barra de tareas
  int ty = dexTbY();
  if(pY >= ty && ty < LH){
    if(pLong){ dexMenuOpen(1, pX - DEX_MENU_W / 2, ty - 12 - (10 + 4 * DEX_MENU_IH), -1); return; }
    if(!pTap) return;
    if(dexIn(pX, pY, dexRDrw)){
      if(dexOv == DXO_DRAWER && !dexOvClosing) dexOvClose(); else dexOvOpen(DXO_DRAWER);
      return;
    }
    if(dexIn(pX, pY, dexRFnd)){
      if(dexOv == DXO_FINDER && !dexOvClosing) dexOvClose(); else dexOvOpen(DXO_FINDER);
      return;
    }
    if(dexIn(pX, pY, dexRPad)){
      dexPadOn = !dexPadOn;
      if(dexPadOn){ dexCurX = LW / 2; dexCurY = LH / 2 - 40; }
      dexMarkAll(); dexDirty = true; return;
    }
    if(dexIn(pX, pY, dexRBell) || dexIn(pX, pY, dexRGear)){
      if(dexOv == DXO_NOTIF && !dexOvClosing) dexOvClose(); else dexOvOpen(DXO_NOTIF);
      return;
    }
    if(pX >= dexRClkX - 100 && pX <= dexRClkX){ dexOvOpen(DXO_RECENTS); return; }   // reloj -> Recientes
    int app[10]; bool op[10];
    int n = dexTbItems(app, op);
    for(int i = 0; i < n; i++){
      int x, y, s; dexTbItemRect(i, n, x, y, s);
      if(!dexInBox(pX, pY, x - 8, y - 6, s + 16, s + 14)) continue;
      int win = -1;
      for(int k = 0; k < 4; k++) if(pwins[k].open && pwins[k].app == app[i]) win = k;
      if(win < 0) dexOpenFrom(app[i], x, y, s);
      else if(pwins[win].mini) dexRestore(win);
      else if(win == dexFocus) dexMinimize(win);                // tocar la activa = minimizar
      else dexRaise(win);
      dexDirty = true;
      return;
    }
    return;
  }

  // 5) Ventanas
  int hit = dexWinAt(pX, pY);
  if(hit >= 0){
    PWin* w = &pwins[hit];
    if(pPressed){
      uint8_t m = dexResizeMask(hit, pX, pY);
      if(m){                                                    // redimensionar (8 zonas)
        dexRaise(hit);
        w->snap = SNAP_FREE;
        dexGrab = DXG_RESIZE; dexGrabWin = hit; dexRzMask = m;
        dexGrabDX = pX; dexGrabDY = pY;
        dexRzX0 = w->x; dexRzY0 = w->y; dexRzW0 = w->w; dexRzH0 = w->h;
        dexDirty = true; return;
      }
      // Zona de arrastre: la barra de titulo mas un margen por encima, para que
      // no haya una franja muerta entre el borde de la ventana y el titulo.
      if(dexInBox(pX, pY, w->x - 4, w->y - 6, w->w + 8, DEX_TTL_H + 6)){
        bool onBtn = false;
        for(int k = 0; k < 3; k++){
          int bx, by, bw, bh; dexWinBtnHit(w->x, w->y, w->w, k, bx, by, bw, bh);
          if(dexInBox(pX, pY, bx, by, bw, bh)) onBtn = true;
        }
        if(!onBtn){                                             // arrastrar por la barra de titulo
          dexRaise(hit);
          if(w->snap != SNAP_FREE){                             // "despegar" de su anclaje
            float fr = (w->w > 0) ? (float)(pX - w->x) / (float)w->w : 0.5f;
            w->w = w->rw; w->h = w->rh; w->snap = SNAP_FREE;
            w->x = pX - (int)(fr * w->w);
            w->y = pY - DEX_TTL_H / 2;
            dexClampWin(w);                       // el rect restaurado puede venir de otra area util
          }
          dexGrab = DXG_MOVE; dexGrabWin = hit;
          dexGrabDX = pX - w->x; dexGrabDY = pY - w->y;
          dexSnapGhost = SNAP_FREE;
          dexDirty = true; return;
        }
      }
      if(hit != dexFocus){ dexRaise(hit); dexDirty = true; }
    }
    if(pLong && dexInBox(pX, pY, w->x, w->y, w->w, DEX_TTL_H)){ dexMenuOpen(2, pX, pY, hit); return; }
    // AREA DE CLIENTE -> la app real. Nada de la barra de titulo, de sus tres
    // controles ni del marco de agarre llega aqui: para cuando se evalua esto,
    // esos casos ya han hecho return mas arriba.
    {
      int cx, cy, cw, ch; dexClientRect(hit, cx, cy, cw, ch);
      if(dexGrab == DXG_NONE && dexHost[hit].surf && dexInBox(pX, pY, cx, cy, cw, ch)){
        if(hit != dexFocus){ dexRaise(hit); dexDirty = true; }
        dexHostTouch(hit, cx, cy, cw, ch);
        dexHostServe(hit);
        return;
      }
    }
    if(pTap){
      for(int k = 0; k < 3; k++){
        int bx, by, bw, bh; dexWinBtnHit(w->x, w->y, w->w, k, bx, by, bw, bh);
        if(!dexInBox(pX, pY, bx, by, bw, bh)) continue;
        if(k == 0) dexMinimize(hit);
        else if(k == 1) dexToggleMax(hit);
        else dexCloseWin(hit);
        return;
      }
      // doble toque en la barra de titulo -> maximizar / restaurar
      if(pDTap && dexInBox(pX, pY, w->x, w->y, w->w, DEX_TTL_H)){ dexToggleMax(hit); return; }
    }
    return;
  }

  // 6) Escritorio
  if(pLong){ dexMenuOpen(0, pX, pY, -1); return; }
  if(pTap && dexOv != DXO_NONE) dexOvClose();
}

// #############################################################
// ##  PUNTOS DE ENTRADA DEL FRAMEWORK DE APPS (APP_REG)
// #############################################################
static void pcExit(){
  // pcExit se llama SIEMPRE desde dentro de dexInput() (menu de la barra o boton
  // del panel), o sea desde dentro de pcTick. appClose() ya nos deja en ST_HOME
  // con el escritorio pintado... y al volver, pcTick seguia su curso y llamaba a
  // dexPaint(), que repintaba la banda sucia de DeX ENCIMA del launcher y volvia
  // a poner gLand=true. De ahi salian las dos cosas a la vez: las franjas de la
  // barra/ventanas de DeX pegadas sobre el Home, y el launcher girado del que ya
  // no se salia. dexExiting corta el tick en seco en cuanto se pide la salida.
  dexExiting = true;
  gLand = false;
  pcStartOpen = false;
  dexOv = DXO_NONE; dexOvClosing = false;
  dexMenuOn = false; dexGrab = DXG_NONE;
  dexPadOn = false; dexPadGrab = false;
  dexSnapGhost = SNAP_FREE;
  dexDirty = false; dexBX0 = 0x7FFF; dexBX1 = -1;
  gClipY0 = 0; gClipY1 = SCR_H - 1;
  setBuf(fb);
  for(int i = 0; i < 4; i++) dexHostClose(i);       // libera los lienzos de las apps
  dexBgFree();                                      // 768 KB de PSRAM no se retienen fuera de DeX
  appClose();
}
static void pcOpen(int app){ dexOpen(app); }        // se conserva la firma original

static void pcEnter(){
  for(int i = 0; i < 4; i++){
    pwins[i].open = false; pwins[i].mini = false; pwins[i].snap = SNAP_FREE;
    dexOrder[i] = (uint8_t)i;
  }
  dexFocus = -1;
  dexOv = DXO_NONE; dexOvClosing = false;
  dexMenuOn = false; pcStartOpen = false;
  dexGrab = DXG_NONE; dexGrabWin = -1; dexSnapGhost = SNAP_FREE;
  dexRecDrag = -1; dexRecDY = 0;
  dexQLen = 0; dexQuery[0] = 0;
  dexAK = DXA_NONE; dexAW = -1;
  dexTbOff = 0; dexTbTgt = 0; dexTbFrom = 0; dexTbIdle = millis();
  dexPadOn = false; dexPadGrab = false;
  dexCurX = LW / 2; dexCurY = LH / 2;
  pDown = false; dexLongFired = false; dexTapMs = 0;
  for(int i = 0; i < 4; i++) dexHostClose(i);       // sin lienzos colgando de una sesion previa
  dexExiting = false; dexOvDone = false;
  gLand = true;
  dexBgBuild();                                     // cache del fondo (ver dexWallpaper)
  dexTbLayout();
  dexOpen(IC_MODOPC);                               // ventana de bienvenida
  pcRender();
}

// #############################################################
// ##  MODO PC / DeX  ·  CICLO DE VIDA (multitarea real)
// ##  ------------------------------------------------------
// ##  QUE SE CONSERVA al pasar a segundo plano: la DISPOSICION -- que
// ##  ventanas hay, donde, de que tamano, cual esta minimizada, el orden
// ##  de apilado y cual tiene el foco. Eso son cuatro structs PWin y dos
// ##  arrays de cuatro bytes: nada.
// ##
// ##  QUE SE SUELTA: todo lo que pesa y se sabe reconstruir. El lienzo de
// ##  cada ventana es un 480x800 completo (768 KB) mas su version
// ##  escalada, y el fondo compuesto del escritorio otros 768 KB. Con
// ##  cuatro ventanas eso son casi 4 MB que no tienen por que seguir
// ##  reservados mientras el usuario esta en otra app -- que es justo el
// ##  caso que el presupuesto de memoria tiene que poder resolver.
// ##
// ##  AL VOLVER se rehacen los lienzos en ORDEN DE APILADO y solo
// ##  mientras quede margen de memoria: si no cabe el cuarto, sus tres
// ##  companeras se ven igual y esa ventana se rehara cuando el sistema
// ##  tenga aire. Reservar 4 MB de golpe "porque tocaba" seria justo lo
// ##  que la reserva de seguridad existe para impedir.
// #############################################################
static void pcSuspend(){
  dexOv = DXO_NONE; dexOvClosing = false;
  dexMenuOn = false; pcStartOpen = false;
  dexGrab = DXG_NONE; dexGrabWin = -1; dexSnapGhost = SNAP_FREE;
  dexPadOn = false; dexPadGrab = false;
  dexAK = DXA_NONE; dexAW = -1;                  // ninguna animacion sobrevive a la suspension
  dexDirty = false; dexBX0 = 0x7FFF; dexBX1 = -1;
  for(int i = 0; i < 4; i++) dexHostClose(i);    // lienzos de las ventanas (768 KB cada uno)
  dexBgFree();                                   // fondo compuesto del escritorio
  gLand = false;
}
static void pcResume(){
  dexExiting = false; dexOvDone = false;
  pDown = false; dexLongFired = false; dexTapMs = 0;
  dexQLen = 0; dexQuery[0] = 0;
  dexTbOff = 0; dexTbTgt = 0; dexTbFrom = 0; dexTbIdle = millis();
  dexCurX = LW / 2; dexCurY = LH / 2;
  gLand = true;
  dexBgBuild();
  dexTbLayout();
  // Lienzos: de la mas al frente a la mas al fondo, y solo mientras quede
  // margen. dexOrder[3] es la ventana con el foco (ver dexRaise).
  for(int k = 3; k >= 0; k--){
    int i = dexOrder[k];
    if(i < 0 || i > 3 || !pwins[i].open || pwins[i].mini) continue;
    if(memFreePsram() < FLEXMEM_CRIT_BYTES + (size_t)SCR_W * SCR_H * 2) break;
    dexHostOpen(i);
  }
  pcRender();
}
// Cerrar la tarjeta de Recientes SI borra la disposicion: la proxima apertura
// empieza limpia, que es lo que espera cualquiera que cierre una app.
static void pcCloseApp(){
  pcSuspend();
  for(int i = 0; i < 4; i++){
    pwins[i].open = false; pwins[i].mini = false; pwins[i].snap = SNAP_FREE;
    dexOrder[i] = (uint8_t)i;
  }
  dexFocus = -1;
}

static void pcTick(){
  if(dexExiting) return;                    // ya se pidio salir: no pintar NADA mas
  dexAnimTick();
  dexOvTick();
  dexTbAnimTick();
  dexTbLayout();
  dexInput();
  // dexInput() puede haber llamado a pcExit() (menu de la barra, boton del
  // panel). En ese caso ya estamos en ST_HOME y el escritorio esta pintado:
  // cualquier dibujo de aqui en adelante seria basura encima del launcher.
  if(dexExiting || !gLand) return;
  // Tick periodico de las apps hospedadas: es lo que mantiene vivo el reloj, el
  // calendario, Bienestar... Solo cuando cambia el minuto, no por frame: las
  // apps interactivas ya se refrescan en dexHostTouch.
  // Re-maquetado por cambio de tamano y avance de los fundidos.
  {
    bool wasFading = uiFading;
    uiFading = false;
    for(int k = 0; k < 4; k++) dexHostRelayout(dexOrder[k]);
    if(uiFading || wasFading) dexDirty = true;     // sigue pidiendo frames mientras funde
  }
  if(gMinChanged){
    for(int k = 0; k < 4; k++){
      int i = dexOrder[k];
      if(pwins[i].open && !pwins[i].mini && dexHost[i].surf){
        dexHostRun(i, false, true, NULL);
        dexHostServe(i);
      }
    }
    dexMarkAll(); dexDirty = true;
  }
  if(!dexDirty) return;
  // Pasos discretos ligados a millis(): ~33 fps de techo. No bloquea el loop ni
  // el tactil -- si aun no toca frame se sale, y se repinta en la siguiente
  // vuelta con la banda sucia ya acumulada.
  if(millis() - dexFrameMs < 30) return;
  dexFrameMs = millis();
  dexPaint(false);
}
