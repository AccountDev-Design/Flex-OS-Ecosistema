// #############################################################
// ##  FLEX OS ULTRA  ·  CARPETA SEGURA  ·  el espacio seguro como app
// ##  ----------------------------------------------------------
// ##  Flex Vault deja de ser una pantalla escondida en Ajustes y pasa a ser
// ##  una APLICACION del sistema (IC_SECFOLDER) con su propio escritorio, su
// ##  propia tarea y su sitio en Recientes. Lo que NO cambia es nada de lo que
// ##  protege: la clave, el PBKDF2, la clave maestra envuelta, el cifrado
// ##  AES-256-GCM, el indice, el registro de seguridad y el contador de
// ##  intentos siguen siendo EXACTAMENTE los de FlexOS_Vault.cpp. Aqui no hay
// ##  ni una linea de criptografia, ni un segundo PIN, ni un segundo almacen.
// ##
// ##  COMO ENCAJA ESTE ARCHIVO
// ##  ----------------------------------------------------------
// ##  Es una PARTE del sketch FlexOS_Ultra.ino, no una unidad de traduccion
// ##  independiente. FlexOS_Ultra.ino lo incluye en el orden que fija la
// ##  cadena de cabeceras (cada modulo incluye al anterior), asi que todo el
// ##  sistema sigue compilandose como UN SOLO archivo. Va el ULTIMO de la
// ##  cadena a proposito: necesita el registro de apps, el escritorio, el
// ##  selector de Recientes, el teclado, el material Liquid Glass y las vistas
// ##  de la boveda, que son justamente todo lo que reutiliza.
// ##
// ##  POR QUE UNA APP Y NO OTRO gState
// ##  ----------------------------------------------------------
// ##  El motivo por el que Flex Vault NO era una app (ver la cabecera de
// ##  FlexOS_Ultra_Vault.h) era que APP_REG traia tres fugas: la miniatura de
// ##  Recientes, el buscador de Modo PC y las capas del sistema pintando
// ##  encima. Las tres se cierran aqui de forma explicita, y por eso ahora SI
// ##  puede ser una app:
// ##    · MINIATURA: una tarea del espacio seguro nunca entra en Recientes con
// ##      captura (appSuspend la manda por swPushNoThumb y ademas suelta la
// ##      que hubiera). Su tarjeta se dibuja con el candado y el rotulo
// ##      "Contenido protegido": ni un pixel del contenido llega a PSRAM.
// ##    · MODO PC: secfEnter se niega a abrirse dentro de una ventana de DeX
// ##      (gHosted / gLand) y lo DICE, en vez de componer contenido privado
// ##      dentro del escritorio compartido.
// ##    · CAPAS DEL SISTEMA: el panel rapido y la isla de notificaciones se
// ##      apagan solos cuando la boveda esta abierta (ver qsCanOpen y
// ##      notifPaused), que es la misma proteccion que ya tenian.
// ##
// ##  UNA SOLA CLAVE, UNA SOLA PANTALLA DE CLAVE
// ##  ----------------------------------------------------------
// ##  La pantalla de clave de aqui usa flexVaultUnlock(), flexVaultWaitMs(),
// ##  flexVaultSecretLen() y flexVaultLockType(): las MISMAS funciones que
// ##  usaba la boveda. El teclado numerico es el del bloqueo del sistema
// ##  (lsuPinRect, PIN_KEYS) y el alfanumerico es el teclado global, igual que
// ##  hacia FlexOS_Ultra_Vault.h. No hay un segundo teclado que mantener ni un
// ##  segundo sitio donde pueda quedarse una clave.
// ##
// ##  POLITICA DE CIERRE: LA DE SIEMPRE
// ##  ----------------------------------------------------------
// ##  No se inventa ninguna: se reusa la de Flex Vault, que es "la boveda se
// ##  cierra al salir y no sobrevive a un arranque". Los caminos de cierre son
// ##  los que ya existian -- pantalla apagada, auto-bloqueo, apagado, peticion
// ##  de clave del sistema, Recientes, inactividad con flexVaultAutoLockMs() --
// ##  mas el que corresponde al ciclo de vida de una app: pasar a segundo
// ##  plano (secfSuspend). Volver a la tarea vuelve a pedir la clave.
// #############################################################
#pragma once
#include "FlexOS_Ultra_TheftUI.h"   // eslabon anterior de la cadena

// -------------------------------------------------------------
//  VISTAS DEL ESPACIO
//  ------------------------------------------------------------
//  No son gStates: la navegacion de dentro del espacio es asunto del espacio,
//  igual que setView en Ajustes o vwView en la boveda.
// -------------------------------------------------------------
enum { SEC_VW_AUTH = 0,   // pantalla de clave (la unica del espacio)
       SEC_VW_HOME,       // escritorio del espacio seguro
       SEC_VW_VAULT,      // una vista de la boveda, hospedada dentro de la app
       SEC_VW_SETUP,      // todavia no existe: alta de la Carpeta segura
       SEC_VW_NOFS };     // sin almacenamiento: no hay nada que abrir

// Acciones de una celda del escritorio seguro.
enum { SEC_ACT_NONE = 0,
       SEC_ACT_GALLERY,   // Galeria privada
       SEC_ACT_NOTES,     // Notas privadas
       SEC_ACT_FILES,     // Archivos privados
       SEC_ACT_PAPP,      // una app privada anadida (usa .app)
       SEC_ACT_ADDAPPS,   // anadir apps al espacio
       SEC_ACT_MANAPPS,   // gestionar apps privadas
       SEC_ACT_LOG };     // registro de seguridad

struct SecCell { uint8_t act; int8_t app; };

// ---- GEOMETRIA. Fija y calculada para 480x800 con la franja de sistema de
// ---- NAV_H px abajo. Un solo sitio: dibujo y hit-test la comparten, que es
// ---- lo que impide que un icono se vea en un lado y responda en otro.
#define SECH_TITLE_Y   54
#define SECH_SUB_Y     94
#define SECH_CARD_X    16
#define SECH_CARD_Y    122
#define SECH_CARD_W    (SCR_W - 32)
#define SECH_CARD_H    104
#define SECH_ICON_S    72
#define SECH_COLS      4
#define SECH_ROWS      3
#define SECH_COLSTEP   (SCR_W / SECH_COLS)                 // 120
#define SECH_ROWSTEP   112
#define SECH_GX0       ((SECH_COLSTEP - SECH_ICON_S) / 2)  // 24
#define SECH_GY0       250
#define SECH_SLOTS     (SECH_COLS * SECH_ROWS)             // 12 celdas por pagina
#define SECH_DOTS_Y    588
// Banda que cambia al pasar de pagina: rejilla + etiquetas + puntos. Todo lo de
// arriba (barra de estado, titulo, tarjeta) y lo de abajo (dock, franja del
// sistema) es identico en todas las paginas, asi que el deslizamiento mueve
// esta banda y ni una fila mas.
#define SECH_BAND_TOP  (SECH_GY0 - 8)                      // 242
#define SECH_BAND_BOT  (SECH_DOTS_Y + 16)                  // 604
#define SECH_DOCK_X    24
#define SECH_DOCK_Y    616
#define SECH_DOCK_W    (SCR_W - 48)
#define SECH_DOCK_H    84
// GEOMETRIA DE LA PANTALLA DE CLAVE, cuadrada contra los 480x800 reales:
//   barra de estado 16..52 · candado 106..154 · titulo 174..206 ·
//   puntos 232..248 · "Clave incorrecta" 266..284 · teclado numerico desde 300
//   (lsuPinRect) o alfanumerico desde KB_Y (>= 434 con la franja reservada).
// Nada se pisa en ninguno de los dos modos, y lo comprueba testCarpetaSegura().
#define SECA_LOCK_Y   120
#define SECA_DOTS_Y   240
#define SECA_MSG_Y    266

// Tope de celdas: las tres secciones de contenido, las apps privadas que
// admite la boveda y las tres entradas de gestion, con holgura.
#define SECH_CELLS_MAX 24

// ---- Estado del espacio. Es SUYO: ni una variable se comparte con el
// ---- escritorio normal, asi que la pagina, la seleccion y el scroll de los
// ---- dos no pueden mezclarse por accidente.
static int      secView   = SEC_VW_AUTH;
static SecCell  secCells[SECH_CELLS_MAX];
static int      secCellN  = 0;
static int      secPage   = 0;
static int      secPageN  = 1;
static uint32_t secMsgMs  = 0;
static char     secMsg[80] = "";
static uint32_t secLastTouch = 0;        // inactividad DENTRO del espacio

// Clave que se esta escribiendo. Se BORRA en cuanto se usa (secKeyClear) y
// nunca sale de aqui: no se guarda, no se copia y no se imprime.
static char     secPin[16] = "";
static char     secPass[FLEXVAULT_SECRET_MAX] = "";
static uint32_t secWrongMs = 0;          // destello de "clave incorrecta"
static uint32_t secKbAnim  = 0;          // deslizamiento de entrada del teclado
static uint32_t secWaitMs  = 0;          // ultimo refresco de la cuenta atras

// Gesto horizontal de cambio de pagina (el mismo del escritorio normal).
static bool     secDrag    = false;
static int      secDragX0  = 0, secDragDx = 0;
static int      secPressCell = -1;

// ---- RESTAURACION EXACTA DE LA TAREA -------------------------------------
// Al pasar a segundo plano se cierra la boveda y se borra de RAM todo lo
// descifrado, pero SI se recuerda -- en RAM, dentro de la tarea viva -- que
// estaba mirando el usuario. Tras acertar la clave se vuelve a ESE punto:
// misma vista, misma pagina, misma seccion, mismo elemento y mismo scroll, con
// el contenido descifrado otra vez desde el almacen. Nada de esto se escribe
// en disco: si la tarea muere o la placa arranca, no queda rastro.
static bool     secRsValid   = false;
static uint8_t  secRsView    = SEC_VW_HOME;
static uint8_t  secRsPage    = 0;
static uint8_t  secRsVwView  = VW_HOME;
static int8_t   secRsKind    = FXV_KIND_PHOTO;
static int8_t   secRsListApp = -1;
static uint16_t secRsOpenId  = 0;
static int      secRsScroll  = 0;

static void secRender();
static void secBuildCells();

// -------------------------------------------------------------
//  Estado publico del espacio (lo consultan Recientes y el cierre)
// -------------------------------------------------------------
static bool secWorkspaceOpen(){ return flexVaultUnlocked(); }
static bool secAnyTask(){ return appTaskSecure(IC_SECFOLDER); }

static void secKeyClear(){
  flexVaultWipe(secPin,  sizeof(secPin));
  flexVaultWipe(secPass, sizeof(secPass));
}
static void secToast(const char* m){
  snprintf(secMsg, sizeof(secMsg), "%s", m ? m : "");
  secMsgMs = millis();
}

// -------------------------------------------------------------
//  CATALOGO DE CELDAS
//  ------------------------------------------------------------
//  Se reconstruye cada vez que se entra en el escritorio seguro porque su
//  contenido depende del estado REAL de la boveda (que apps privadas hay
//  anadidas ahora mismo), no de una copia que pudiera quedarse vieja.
// -------------------------------------------------------------
static void secAddCell(uint8_t act, int app){
  if(secCellN >= SECH_CELLS_MAX) return;
  secCells[secCellN].act = act;
  secCells[secCellN].app = (int8_t)app;
  secCellN++;
}
static void secBuildCells(){
  secCellN = 0;
  // 1. El contenido que el usuario movio a mano desde Galeria, Notas y
  //    Archivos. Son las tres secciones generales de la boveda.
  secAddCell(SEC_ACT_GALLERY, -1);
  secAddCell(SEC_ACT_NOTES,   -1);
  secAddCell(SEC_ACT_FILES,   -1);
  // 2. Las apps privadas ANADIDAS, con su icono y su nombre de verdad. Sus
  //    datos viven separados de las secciones generales (flexVaultListFor con
  //    appId), asi que aqui son celdas distintas y no duplicados.
  for(int id = 0; id < APP_N; id++)
    if(flexVaultAppAdded(id)) secAddCell(SEC_ACT_PAPP, id);
  // 3. Gestion del espacio.
  secAddCell(SEC_ACT_ADDAPPS, -1);
  secAddCell(SEC_ACT_MANAPPS, -1);
  secAddCell(SEC_ACT_LOG,     -1);

  secPageN = (secCellN + SECH_SLOTS - 1) / SECH_SLOTS;
  if(secPageN < 1) secPageN = 1;
  if(secPage >= secPageN) secPage = secPageN - 1;
  if(secPage < 0) secPage = 0;
}

// Nombre visible de una celda. Nunca revela contenido: son rotulos de seccion.
static const char* secCellName(const SecCell& c){
  switch(c.act){
    case SEC_ACT_GALLERY: return "Galer\xC3\xAD" "a";
    case SEC_ACT_NOTES:   return "Notas";
    case SEC_ACT_FILES:   return "Archivos";
    case SEC_ACT_PAPP:    return (c.app >= 0 && c.app < APP_N) ? appName(c.app) : "App";
    case SEC_ACT_ADDAPPS: return "A\xC3\xB1" "adir apps";
    case SEC_ACT_MANAPPS: return "Apps privadas";
    case SEC_ACT_LOG:     return "Registro";
  }
  return "";
}

// Distintivo de "esto vive cifrado": candado pequeno en la esquina del icono.
// Es el mismo lenguaje que el candado de la cabecera de la boveda, y va SOBRE
// el icono de la app normal para que se lea "esta, pero privada".
static void secLockBadge(int x, int y, int S){
  int r = S / 4; if(r < 9) r = 9;
  int cx = x + S - r + 2, cy = y + S - r + 2;
  fillCircle(cx, cy, r, rgb565(78,56,150));
  fillCircle(cx, cy, r - 2, rgb565(112,86,196));
  int bw = r, bh = (r * 2) / 3;
  int ar = r / 3; if(ar < 2) ar = 2;
  arcStroke(cx, cy - 1, ar, 180, 360, 2, rgb565(240,238,252));
  fillRoundRect(cx - bw / 2, cy - 1, bw, bh, 2, rgb565(240,238,252));
}

// Icono de una celda. Las que representan una app usan el icono REAL de esa
// app (drawAppIcon) con el candado encima: no hay una segunda iconografia que
// mantener. Las tres de gestion son propias porque no representan ninguna app.
static void secDrawCellIcon(const SecCell& c, int x, int y, int S){
  int cx = x + S / 2, cy = y + S / 2;
  switch(c.act){
    case SEC_ACT_GALLERY: drawAppIcon(IC_GALERIA, x, y, S); secLockBadge(x, y, S); return;
    case SEC_ACT_NOTES:   drawAppIcon(IC_NOTAS,   x, y, S); secLockBadge(x, y, S); return;
    case SEC_ACT_FILES:   drawAppIcon(IC_ALMACEN, x, y, S); secLockBadge(x, y, S); return;
    case SEC_ACT_PAPP:
      if(c.app >= 0 && c.app < APP_N) drawAppIcon(c.app, x, y, S);
      secLockBadge(x, y, S);
      return;
    case SEC_ACT_ADDAPPS: {
      iconBase(x, y, S, rgb565(112,86,196), 22);
      int t = S / 9; if(t < 3) t = 3;
      fillRoundRect(cx - (int)(S * 0.24f), cy - t / 2, (int)(S * 0.48f), t, t / 2, rgb565(245,244,252));
      fillRoundRect(cx - t / 2, cy - (int)(S * 0.24f), t, (int)(S * 0.48f), t / 2, rgb565(245,244,252));
    } return;
    case SEC_ACT_MANAPPS: {
      iconBase(x, y, S, rgb565(96,102,126), 22);
      uint16_t w = rgb565(240,241,246);
      for(int k = 0; k < 4; k++){
        int gx = x + (int)(S * (k % 2 ? 0.52f : 0.22f));
        int gy = y + (int)(S * (k / 2 ? 0.52f : 0.22f));
        fillRoundRect(gx, gy, (int)(S * 0.26f), (int)(S * 0.26f), 4, w);
      }
    } return;
    case SEC_ACT_LOG: {
      iconBase(x, y, S, rgb565(86,132,146), 22);
      uint16_t w = rgb565(240,246,248);
      for(int k = 0; k < 3; k++){
        int ly = y + (int)(S * (0.28f + 0.17f * k));
        fillCircle(x + (int)(S * 0.27f), ly + 2, (int)(S * 0.045f) + 1, w);
        fillRoundRect(x + (int)(S * 0.38f), ly, (int)(S * 0.36f), (int)(S * 0.06f) + 1, 2, w);
      }
    } return;
    default: return;
  }
}

// -------------------------------------------------------------
//  FONDO
//  ------------------------------------------------------------
//  El MISMO wallpaper desenfocado que ya usan Recientes y el desbloqueo
//  (blurBg, compuesto una sola vez en toda la sesion). Ni un byte de PSRAM
//  nuevo, y el espacio seguro se apoya en el material del sistema en vez de
//  inventarse un fondo propio. Sin PSRAM se cae al fondo de pagina del tema.
// -------------------------------------------------------------
static void secBgBand(int y0, int y1){
  if(y0 < 0) y0 = 0;
  if(y1 > SCR_H - 1) y1 = SCR_H - 1;
  if(blurBg){
    for(int j = y0; j <= y1; j++)
      memcpy(gBuf + (size_t)j * SCR_W, blurBg + (size_t)j * SCR_W, SCR_W * 2);
  } else {
    fillRect(0, y0, SCR_W, y1 - y0 + 1, TH_PAGE);
  }
}

// Aviso breve (lo que en un movil seria un "toast"). Se dibuja al final de cada
// repintado completo, asi que nunca queda tapado por una tarjeta ni por la
// rejilla. Tiene DOS sitios porque las dos pantallas del espacio tienen sitios
// libres distintos, y un aviso que tapa un boton es peor que no avisar:
//   · en el escritorio, sobre el DOCK -- que son dos accesos permanentes, se
//     pueden ocultar tres segundos, y asi no pisa ni la rejilla ni los puntos;
//   · en la pantalla de clave, en la MISMA linea que "Clave incorrecta" -- la
//     unica franja libre entre los puntos y el teclado --, y como texto, no
//     como tarjeta, para no tapar el teclado ni un pixel. El error manda: si
//     los dos coinciden, se ve el error.
static void secDrawMsg(){
  if(!secMsg[0]) return;
  if(millis() - secMsgMs > 3000){ secMsg[0] = 0; return; }
  if(secView == SEC_VW_AUTH){
    if(secWrongMs && millis() - secWrongMs < 1200) return;   // manda el error
    int fs = uiFontFit(secMsg, SCR_W - 48, 2);
    drawTextC(SCR_W / 2, SECA_MSG_Y, secMsg, fs, TH_ONWALL2);
    return;
  }
  int w = SCR_W - 48, h = 48, x = 24, y = SECH_DOCK_Y + (SECH_DOCK_H - 48) / 2;
  uiWallSurface(x, y, w, h, 16, uiGlass ? TH_WALLSURF2 : TH_WALLSURF, 10);
  int fs = uiFontFit(secMsg, w - 28, 2);
  drawTextC(SCR_W / 2, y + (h - (fs == 1 ? 10 : 18)) / 2, secMsg, fs, TH_ONWALL);
}

// -------------------------------------------------------------
//  ESCRITORIO SEGURO  ·  barra de estado, titulo y tarjeta
// -------------------------------------------------------------
static void secDrawStatusBar(){
  cronoBarClock(16, TH_ONWALL);
  char sd[48]; buildShortDate(sd, sizeof(sd));
  drawText(20, 40, sd, 1, TH_ONWALL2);
  drawWifi(SCR_W - 66, 28, 11, TH_ONWALL);
  drawBattery(SCR_W - 46, 20, 30, 15, 82, TH_ONWALL);
}

// Tarjeta de estado. Dice lo que el usuario necesita saber del espacio y NADA
// mas: nunca cuantos elementos privados hay de cada clase con su nombre, ni
// nada que se pudiera leer por encima del hombro. Es el equivalente de los
// widgets del escritorio normal, con el mismo material Liquid Glass.
static void secDrawStatusCard(){
  uiWallSurface(SECH_CARD_X, SECH_CARD_Y, SECH_CARD_W, SECH_CARD_H, 20,
                uiGlass ? TH_WALLSURF2 : TH_WALLSURF, 12);
  int lx = 44, ly = SECH_CARD_Y + 26;
  fillRoundRect(lx - 13, ly, 26, 20, 4, TH_OK);
  arcStroke(lx, ly, 8, 180, 360, 3, TH_OK);
  drawText(74, SECH_CARD_Y + 16, "Protegida y abierta", 3, TH_ONWALL);

  char v[64];
  flexFsFmtSize(flexVaultUsedBytes(), v, sizeof(v));
  char line[80];
  char al[32]; vwAutoLockText(al, sizeof(al));
  snprintf(line, sizeof(line), "%s en uso  \xC2\xB7  se cierra: %s", v, al);
  drawTextClip(74, SECH_CARD_Y + 48, line, 1, TH_ONWALL2, SECH_CARD_X + SECH_CARD_W - 16);

  int total = flexVaultCount(FXV_KIND_ANY);
  snprintf(line, sizeof(line), "%d elemento%s cifrado%s", total, total == 1 ? "" : "s", total == 1 ? "" : "s");
  drawTextClip(74, SECH_CARD_Y + 70, line, 1, TH_ONWALL2, SECH_CARD_X + SECH_CARD_W - 16);
}

static inline void secSlotXY(int slot, int &x, int &y){
  int c = slot % SECH_COLS, r = slot / SECH_COLS;
  x = SECH_GX0 + c * SECH_COLSTEP;
  y = SECH_GY0 + r * SECH_ROWSTEP;
}

// Rejilla de UNA pagina, con desplazamiento horizontal (xoff) para el gesto de
// cambio de pagina. No limpia el fondo: quien llama ya lo ha puesto.
static void secDrawGrid(int page, int xoff){
  int base = page * SECH_SLOTS;
  for(int i = 0; i < SECH_SLOTS; i++){
    int idx = base + i;
    if(idx >= secCellN) break;
    int ix, iy; secSlotXY(i, ix, iy);
    ix += xoff;
    if(ix + SECH_ICON_S < 0 || ix > SCR_W) continue;   // fuera: ni se dibuja
    secDrawCellIcon(secCells[idx], ix, iy, SECH_ICON_S);
    const char* nm = secCellName(secCells[idx]);
    if(nm && nm[0]){
      // MISMO TRUNCADO QUE LA CAJA DE APLICACIONES: la etiqueta no puede
      // invadir la columna vecina, venga el nombre de una seccion o de una app.
      int lblW = SECH_COLSTEP - 14;
      int fs = uiFontFit(nm, lblW, 2);
      char lbl[40];
      uiLabelFit(nm, lblW, fs, lbl, sizeof(lbl));
      drawTextC(ix + SECH_ICON_S / 2, iy + SECH_ICON_S + 6, lbl, fs, TH_ONWALL);
    }
  }
}

// Puntos de pagina. Con una sola pagina no se dibuja nada: un indicador de una
// sola posicion no informa de nada y ocupa sitio.
static void secDrawDots(float frac){
  if(secPageN <= 1) return;
  int n = secPageN, gap = 18, x0 = SCR_W / 2 - (n - 1) * gap / 2;
  for(int i = 0; i < n; i++) fillCircleA(x0 + i * gap, SECH_DOTS_Y, 3, TH_ONWALL2, 150);
  float pos = (float)secPage + frac;
  if(pos < 0) pos = 0;
  if(pos > n - 1) pos = (float)(n - 1);
  fillCircleA((int)(x0 + pos * gap + 0.5f), SECH_DOTS_Y, 5, TH_ONWALL, 255);
}

// ---- Dock del espacio: dos accesos permanentes, iguales en todas las paginas.
#define SECD_BW  ((SECH_DOCK_W - 16) / 2)
static void secDockRect(int i, int &x, int &y, int &w, int &h){
  x = SECH_DOCK_X + i * (SECD_BW + 16);
  y = SECH_DOCK_Y; w = SECD_BW; h = SECH_DOCK_H;
}
static void secDrawDock(){
  uiWallSurface(SECH_DOCK_X, SECH_DOCK_Y, SECH_DOCK_W, SECH_DOCK_H, 24,
                uiGlass ? TH_WALLSURF2 : TH_WALLSURF, 12);
  const char* lbl[2] = { "Ajustes", "Bloquear" };
  for(int i = 0; i < 2; i++){
    int x, y, w, h; secDockRect(i, x, y, w, h);
    int cx = x + w / 2, cy = y + 30;
    if(i == 0){                                   // engranaje simplificado
      fillRing(cx, cy, 13, 3, TH_ONWALL);
      for(int k = 0; k < 6; k++){
        float a = k * 60.0f * 0.0174532925f;
        strokeSegAA(cx + 13 * cosf(a), cy + 13 * sinf(a),
                    cx + 19 * cosf(a), cy + 19 * sinf(a), 2.0f, TH_ONWALL);
      }
    } else {                                      // candado cerrado
      fillRoundRect(cx - 12, cy - 2, 24, 18, 4, TH_ONWALL);
      arcStroke(cx, cy - 2, 8, 180, 360, 3, TH_ONWALL);
    }
    drawTextC(cx, y + h - 26, lbl[i], 2, TH_ONWALL);
  }
}

// Repintado SOLO de la banda de la rejilla. Es lo unico que se mueve al pasar
// de pagina: repintar la pantalla entera por un gesto horizontal es justo el
// redibujado completo innecesario que no se quiere.
static void secRenderBand(int xoff, float frac){
  setBuf(fb);
  uiClipViewport(SECH_BAND_TOP, SECH_BAND_BOT);
  secBgBand(SECH_BAND_TOP, SECH_BAND_BOT);
  secDrawGrid(secPage, xoff);
  if(xoff > 0 && secPage > 0)              secDrawGrid(secPage - 1, xoff - SCR_W);
  if(xoff < 0 && secPage < secPageN - 1)   secDrawGrid(secPage + 1, xoff + SCR_W);
  secDrawDots(frac);
  uiClipFull();
  flxFlush(SECH_BAND_TOP, SECH_BAND_BOT);
}

static void secRenderHome(){
  setBuf(fb);
  uiClipFull();
  secBgBand(0, SCR_H - 1);
  secDrawStatusBar();
  drawTextC(SCR_W / 2, SECH_TITLE_Y, "Carpeta segura", 4, TH_ONWALL);
  drawTextC(SCR_W / 2, SECH_SUB_Y, "Contenido cifrado de este dispositivo", 1, TH_ONWALL2);
  secDrawStatusCard();
  secDrawGrid(secPage, 0);
  secDrawDots(0.0f);
  secDrawDock();
  secDrawMsg();
  flxFlushAll();
}

// -------------------------------------------------------------
//  PANTALLA DE CLAVE
//  ------------------------------------------------------------
//  La MISMA clave, la MISMA verificacion y la MISMA espera por intentos
//  fallidos que Flex Vault: flexVaultUnlock() es quien decide, y su contador
//  vive en NVS, asi que reiniciar la placa no perdona ni un segundo de espera.
//  Lo unico propio de aqui es el dibujo.
// -------------------------------------------------------------

static void secDrawAuthDots(){
  int n = (flexVaultLockType() == FLEXVAULT_LOCK_PASS) ? utf8Count(secPass) : (int)strlen(secPin);
  uint16_t dc = (secWrongMs && millis() - secWrongMs < 600) ? TH_ERR : TH_PRIM;
  for(int i = 0; i < 8; i++){
    int cx = SCR_W / 2 - 4 * 28 + 14 + i * 28;
    if(i < n) fillCircle(cx, SECA_DOTS_Y, 8, dc);
    else      drawCircle(cx, SECA_DOTS_Y, 8, TH_ONWALL2);
  }
  if(secWrongMs && millis() - secWrongMs < 1200)
    drawTextC(SCR_W / 2, SECA_MSG_Y, "Clave incorrecta", 2, TH_ERR);
}

static void secPaintAuth(int yoff){
  secBgBand(0, SCR_H - 1);
  secDrawStatusBar();
  // Candado grande: la misma senal que la cabecera de la boveda.
  int cy = SECA_LOCK_Y;
  fillRoundRect(SCR_W / 2 - 22, cy, 44, 34, 7, TH_ONWALL);
  arcStroke(SCR_W / 2, cy, 14, 180, 360, 5, TH_ONWALL);
  drawTextC(SCR_W / 2, cy + 54, "Carpeta segura", 4, TH_ONWALL);

  // Espera por intentos fallidos: el teclado queda inerte y se dice cuanto
  // falta. La espera la lleva el modulo de la boveda y sobrevive al reinicio,
  // asi que salir de aqui no la perdona.
  uint32_t wait = flexVaultWaitMs();
  if(wait > 0){
    int secs = (int)((wait + 999) / 1000);
    char cd[24];
    if(secs >= 60) snprintf(cd, sizeof(cd), "%d:%02d", secs / 60, secs % 60);
    else           snprintf(cd, sizeof(cd), "%d s", secs);
    drawTextC(SCR_W / 2, 300, "Demasiados intentos fallidos", 2, TH_ERR);
    drawTextC(SCR_W / 2, 340, cd, 4, TH_ONWALL);
    drawTextC(SCR_W / 2, 400, "La espera sigue aunque reinicies", 1, TH_ONWALL2);
    return;
  }
  secDrawAuthDots();

  if(flexVaultLockType() != FLEXVAULT_LOCK_PASS){
    for(int i = 0; i < 12; i++){
      int x, y, w, h; lsuPinRect(i, x, y, w, h);
      uiWallSurface(x, y, w, h, 16, uiGlass ? TH_WALLSURF2 : TH_WALLSURF, 10);
      uint16_t col = (i == 9) ? TH_WARN : (i == 11) ? TH_OK : TH_ONWALL;
      drawTextC(x + w / 2, y + h / 2 - 12, PIN_KEYS[i], 3, col);
    }
    return;
  }
  // Contrasena: el teclado alfanumerico del sistema, igual que la pantalla de
  // contrasena del bloqueo y que la de la boveda. No hay un tercer teclado.
  int ky = KB_Y + yoff;
  if(uiGlass) drawLiquidGlassPanel(0, ky - 4, SCR_W, SCR_H - (ky - 4), 0, SET_CARD_GLASS);
  else        fillRect(0, ky - 4, SCR_W, SCR_H - (ky - 4), PAGE_BG);
  int fs = kbFontSize();
  for(int r = 0; r < KB_ROWS; r++) for(int c = 0; c < KB_COLS; c++){
    int x = KB_X + c * (KB_KW + KB_GAP), y = ky + r * (KB_KH + KB_GAP);
    char u[6];
    const char* k = kbResolveKey(mapaActivo[r][c], u, false);
    int cell = r * KB_COLS + c;
    kbPaintKey(x, y, KB_KW, KB_KH, k, fs, SET_CARD_BG, TH_TXT,
               kbCellHeld(cell) || kbFxLevel(cell) > 0);
  }
  int fy = ky + 3 * (KB_KH + KB_GAP);
  const char* lb[KB_FKEYS] = { "shift", kbLayerLabel(), kbLangEs ? "ES" : "EN", "espacio", "<-", "OK" };
  for(int i = 0; i < KB_FKEYS; i++) kbFKey(kbFKeyX(i), fy, kbFKeyW(i), lb[i], (i == 0) && kbShift);
}

static void secRenderAuth(int yoff){
  setBuf(fb);
  uiClipFull();
  secPaintAuth(yoff);
  secDrawMsg();
  flxFlushAll();
}

// Repintado MINIMO del error de clave: solo la franja de los puntos y el
// rotulo. Una clave fallida no puede costar un redibujado de toda la pantalla
// -- ese es el parpadeo que se ve como un tiron.
static void secRenderAuthDots(){
  setBuf(fb);
  uiClipViewport(SECA_DOTS_Y - 14, SECA_MSG_Y + 24);
  secBgBand(SECA_DOTS_Y - 14, SECA_MSG_Y + 24);
  secDrawAuthDots();
  secDrawMsg();                     // comparte franja con el error: se repinta con el
  uiClipFull();
  flxFlush(SECA_DOTS_Y - 14, SECA_MSG_Y + 24);
}

// -------------------------------------------------------------
//  NAVEGACION
// -------------------------------------------------------------
static void secGoAuth(){
  secView = SEC_VW_AUTH;
  secKeyClear();
  secWrongMs = 0;
  secWaitMs = 0;
  // Teclado limpio: sin barra de sugerencias ni chips en una pantalla de
  // clave, igual que en el bloqueo del sistema y que en la boveda.
  if(flexVaultLockType() == FLEXVAULT_LOCK_PASS){
    mapaActivo = LAYOUT_ES; kbLangEs = true; kbShift = false;
    kbExtrasOn = false;
    // La franja del sistema es suya: el teclado se apoya ENCIMA, no debajo.
    kbBotReserve = navBarVisible() ? NAV_H : 0;
    kbApplySize(); kbMtSurfaceReset();
    secKbAnim = millis();
  } else secKbAnim = 0;
  secLastTouch = millis();
}

static void secGoHome(){
  vwHosted = true;
  // Dos guardas, y las dos existen por un camino real:
  //  · cancelar el alta ("atras" en el asistente) deja la carpeta SIN crear, y
  //    un escritorio seguro de una carpeta que no existe no tiene sentido: se
  //    sale de la app, que es lo que el usuario acaba de pedir.
  //  · si la boveda se cerro entre medias (inactividad, pantalla apagada), lo
  //    que toca es la clave, no una rejilla que no se puede usar.
  if(!flexVaultExists()){ appClose(); return; }
  if(!flexVaultUnlocked()){ secGoAuth(); secRenderAuth(0); return; }
  secView = SEC_VW_HOME;
  secBuildCells();
  secLastTouch = millis();
  secRenderHome();
}

// La boveda se ha cerrado con el espacio en pantalla: vuelta a la clave. Es el
// unico camino, asi que ningun cierre puede dejar a la vista una lista privada
// que ya no se puede releer.
static void secOnVaultLocked(){
  secKeyClear();
  if(!secfForeground()){ secView = SEC_VW_AUTH; return; }
  secGoAuth();
  secRenderAuth(0);
}

// Abre una vista de la boveda DENTRO de la app. Es el mismo codigo de siempre
// (vaultRender / vaultTick); lo unico que cambia es que "atras" vuelve al
// escritorio seguro en vez de a Ajustes.
static void secOpenVault(int view){
  vwHosted = true;
  vwScroll = 0;
  vwItemMenu = -1;
  vwView = (uint8_t)view;
  secView = SEC_VW_VAULT;
  secLastTouch = millis();
  vwLastTouch = millis();
  vaultRender();
}

static void secOpenCell(int idx){
  if(idx < 0 || idx >= secCellN) return;
  const SecCell c = secCells[idx];
  vwHosted = true;
  switch(c.act){
    case SEC_ACT_GALLERY: vwOpenList(FXV_KIND_PHOTO); secView = SEC_VW_VAULT; break;
    case SEC_ACT_NOTES:   vwOpenList(FXV_KIND_NOTE);  secView = SEC_VW_VAULT; break;
    case SEC_ACT_FILES:   vwOpenList(FXV_KIND_FILE);  secView = SEC_VW_VAULT; break;
    case SEC_ACT_PAPP:
      if(c.app < 0){ return; }
      // Candado POR APP dentro del espacio: si el usuario lo puso, abrirla
      // vuelve a pedir la clave. Es la politica que ya tenia Flex Vault
      // (flexVaultAppLocked), no una nueva.
      if(flexVaultAppLocked(c.app)){
        vwPendApp = c.app;
        vaultLockNow(FXV_LOCK_MANUAL);
        secGoAuth();
        secToast("Esta app privada est\xC3\xA1 bloqueada");
        secRenderAuth(0);
        return;
      }
      vwOpenAppList(c.app);
      secView = SEC_VW_VAULT;
      break;
    case SEC_ACT_ADDAPPS: secOpenVault(VW_APPS);   return;
    case SEC_ACT_MANAPPS: secOpenVault(VW_APPMAN); return;
    case SEC_ACT_LOG:     vwLogN = flexVaultLogRead(vwLog, FLEXVAULT_LOG_MAX);
                          secOpenVault(VW_LOG);    return;
    default: return;
  }
  secView = SEC_VW_VAULT;
  secLastTouch = millis();
  vwLastTouch = millis();
  vaultRender();
}

// -------------------------------------------------------------
//  RESTAURACION EXACTA
// -------------------------------------------------------------
static void secRestoreSnapshot(){
  if(!secRsValid){ secGoHome(); return; }
  secRsValid = false;
  secPage = secRsPage;
  secBuildCells();                         // el catalogo puede haber cambiado
  if(secPage >= secPageN) secPage = secPageN - 1;
  if(secRsView != SEC_VW_VAULT){ secGoHome(); return; }

  vwHosted  = true;
  vwKind    = secRsKind;
  vwListApp = secRsListApp;
  vwOpenId  = secRsOpenId;
  vwScroll  = secRsScroll;
  vwItemMenu = -1;
  vwView    = secRsVwView;
  // Las listas se releen del almacen cifrado: los nombres descifrados se
  // borraron de RAM al cerrar, asi que aqui se vuelven a pedir, no se
  // recuerdan.
  if(vwView == VW_LIST || vwView == VW_ITEM || vwView == VW_NOTE) vwReload();
  if(vwView == VW_ITEM || vwView == VW_NOTE){
    // El contenido del elemento se vuelve a descifrar. Si ya no existe (se
    // borro desde otro sitio) se cae a su lista, que es lo que el usuario
    // esperaria ver, en vez de a una pantalla vacia.
    if(!vwLoadOpen()) vwView = VW_LIST;
    else if(vwView == VW_NOTE){ kbExtrasOn = false; kbBotReserve = navBarVisible() ? NAV_H : 0;
                                kbApplySize(); kbMtSurfaceReset(); kbShift = false; }
  }
  if(vwView == VW_LOG) vwLogN = flexVaultLogRead(vwLog, FLEXVAULT_LOG_MAX);
  secView = SEC_VW_VAULT;
  secLastTouch = millis();
  vwLastTouch = millis();
  vaultRender();
}

static void secSaveSnapshot(){
  secRsView    = (uint8_t)secView;
  secRsPage    = (uint8_t)secPage;
  secRsVwView  = (uint8_t)vwView;
  secRsKind    = (int8_t)vwKind;
  secRsListApp = (int8_t)vwListApp;
  secRsOpenId  = vwOpenId;
  secRsScroll  = vwScroll;
  secRsValid   = (secView == SEC_VW_HOME || secView == SEC_VW_VAULT);
}

static void secAfterUnlock(){
  secKeyClear();
  vwHosted = true;
  vwLogN = flexVaultLogRead(vwLog, FLEXVAULT_LOG_MAX);
  // "Mover a Carpeta segura" que esperaba la clave, y app privada bloqueada
  // que se queria abrir: los dos caminos son los de siempre. Si habia un
  // movimiento pendiente, MANDA el: el usuario venia de otra app a guardar algo
  // aqui, y lo que espera ver es donde ha quedado, no la pantalla anterior.
  bool hadPending = (vwPendPath[0] != 0);
  vwRunPending();
  if(hadPending){
    secBuildCells();
    secRsValid = false;
    secView = SEC_VW_VAULT;
    secLastTouch = millis();
    vwLastTouch = millis();
    vaultRender();
    return;
  }
  if(vwPendApp >= 0){
    int id = vwPendApp; vwPendApp = -1;
    secBuildCells();
    vwOpenAppList(id);
    secView = SEC_VW_VAULT;
    secLastTouch = millis();
    vaultRender();
    return;
  }
  secRestoreSnapshot();
}

// -------------------------------------------------------------
//  CIERRE DEL ESPACIO
//  ------------------------------------------------------------
//  Un solo camino, idempotente, que reutiliza el cierre de la boveda. Todo lo
//  descifrado que haya en RAM (nombres del indice, texto de una nota, bytes de
//  una imagen) lo borra vaultLockNow con flexVaultWipe.
// -------------------------------------------------------------
static void secWorkspaceLock(int reason){
  vaultLockNow(reason);
  secKeyClear();
  secWrongMs = 0;
  secMsg[0] = 0;
}

// -------------------------------------------------------------
//  CICLO DE VIDA DE LA APP
// -------------------------------------------------------------
static void secfEnter(){
  // MODO PC: no se abre dentro de una ventana. Una ventana de DeX compone el
  // contenido de la app en el escritorio compartido (barra de titulo,
  // miniatura, fondo compuesto), y ahi no puede acabar contenido privado.
  // dexHostRun ademas restaura gState al terminar el tick de la app hospedada,
  // asi que el espacio quedaria a medias. Se dice y se vuelve, no se falla en
  // silencio: es la misma regla que ya tenia vaultSettingsEnter.
  if(gHosted){ gHostReq = 1; return; }
  if(gLand){ gLand = false; }
  vwHosted = true;
  gWorkspace = FLEXWS_SECURE;
  appTaskSetSecure(IC_SECFOLDER, true);
  secPressCell = -1; secDrag = false; secDragDx = 0;
  secMsg[0] = 0;
  secLastTouch = millis();
  gLand = false;
  uiClipFull();
  ensureBlurBg();                       // el fondo del sistema, compuesto una sola vez
  if(!flexFsReady()){ secView = SEC_VW_NOFS; secRender(); return; }
  if(!flexVaultExists()){
    // Alta: se reutiliza el asistente de la boveda (elegir PIN o contrasena y
    // crearla). Al terminar, vwAfterUnlock devuelve al escritorio seguro.
    secView = SEC_VW_VAULT;
    vwView = VW_SETUP_SEL;
    vwScroll = 0;
    vaultRender();
    return;
  }
  if(!flexVaultUnlocked()){ secGoAuth(); secRenderAuth(0); return; }
  secGoHome();
}

static void secfResume(){
  // Reanudar NO reabre el espacio: la boveda se cerro al suspender, asi que se
  // vuelve a pedir la clave. Lo que SI sobrevive es donde estaba el usuario
  // (secRs*), para volver a ese punto exacto tras acertar.
  vwHosted = true;
  gWorkspace = FLEXWS_SECURE;
  secPressCell = -1; secDrag = false; secDragDx = 0;
  gLand = false;
  uiClipFull();
  ensureBlurBg();
  if(!flexFsReady()){ secView = SEC_VW_NOFS; secRender(); return; }
  if(!flexVaultExists()){ secRsValid = false; secfEnter(); return; }
  if(!flexVaultUnlocked()){ secGoAuth(); secRenderAuth(0); return; }
  secRestoreSnapshot();
}

static void secfSuspend(){
  // Pasar a segundo plano CIERRA el espacio. Es la politica de Flex Vault de
  // siempre ("la boveda se cierra al salir"), aplicada al camino que ahora
  // corresponde: el ciclo de vida de la app. Antes se recuerda donde estaba el
  // usuario para poder volver ahi tras la clave.
  secSaveSnapshot();
  secWorkspaceLock(FXV_LOCK_EXIT);
  secView = SEC_VW_AUTH;
  gWorkspace = FLEXWS_NORMAL;
}

static void secfClose(){
  // Cierre REAL de la tarea (Recientes -> cerrar, "Cerrar todas", desalojo por
  // memoria, apagado). Aqui no queda nada: ni clave, ni contenido, ni el punto
  // al que volver. Cerrar una tarea segura tiene que borrar su rastro, no
  // dejarlo esperando a que alguien reabra la app.
  secWorkspaceLock(FXV_LOCK_EXIT);
  secRsValid = false;
  secCellN = 0;
  secPage = 0;
  secView = SEC_VW_AUTH;
  vwHosted = false;
  gWorkspace = FLEXWS_NORMAL;
  appTaskSetSecure(IC_SECFOLDER, false);
}

// "Atras" (1): capas propias de la pantalla. Dentro del espacio las capas son
// las del kit de archivos y el menu de un elemento, que ya gestiona la boveda.
static bool secfBackLayer(){
  if(secView != SEC_VW_VAULT) return false;
  // Los dialogos del kit de archivos no tienen "cerrar" publico: se bajan
  // igual que hacen sus propios ticks al cancelar, y se olvida el elemento
  // sobre el que actuaban para que no quede una accion a medias apuntando a el.
  if(fkAskOn){ fkAskOn = false; vwItemMenu = -1; vaultRender(); return true; }
  if(fkNameOn){ fkNameOn = false; vwItemMenu = -1; vaultRender(); return true; }
  if(vwView == VW_LIST && vwItemMenu >= 0){ vwItemMenu = -1; vaultRender(); return true; }
  return false;
}

// "Atras" (2): una pantalla interna. Desde una vista de la boveda se vuelve al
// escritorio seguro; desde el escritorio seguro (o desde la clave) ya no hay a
// donde volver dentro del espacio y "atras" sale al escritorio normal, que es
// exactamente lo que pide el boton.
static bool secfBackScreen(){
  if(secView != SEC_VW_VAULT) return false;
  switch(vwView){
    case VW_ITEM:   vwContentClear(); vwView = VW_LIST; vwReload(); vaultRender(); return true;
    case VW_NOTE:   vwNoteSave(); vwContentClear(); vwView = VW_LIST; vwReload(); vaultRender(); return true;
    case VW_REMOVE: vwView = VW_APPDET; vaultRender(); return true;
    case VW_APPDET: vwView = VW_APPMAN; vwAppSel = -1; vaultRender(); return true;
    case VW_LIST:
      if(vwListApp >= 0){ vwListApp = -1; vwView = VW_APPMAN; vaultRender(); return true; }
      secGoHome(); return true;
    case VW_KEYPAD:
      // Cambio de clave: "atras" vuelve a la pantalla de estado desde la que se
      // inicio, y la clave a medias se borra. El alta, en cambio, no tiene a
      // donde volver dentro de una carpeta que todavia no existe: sale de la app.
      if(vwKeyFor == VK_CHG_OLD || vwKeyFor == VK_CHG_NEW){
        flexVaultWipe(vwOldKey, sizeof(vwOldKey));
        vwKeyClear();
        vwGoHome();
        vaultRender();
        return true;
      }
      return false;
    case VW_SETUP_SEL:
      return false;                     // alta de la carpeta: "atras" sale de la app
    default:
      secGoHome(); return true;
  }
}

// -------------------------------------------------------------
//  ENTRADA TACTIL
// -------------------------------------------------------------
static int secCellAt(int px, int py){
  if(py < SECH_GY0 - 6 || py > SECH_GY0 + (SECH_ROWS - 1) * SECH_ROWSTEP + SECH_ICON_S + 20) return -1;
  for(int i = 0; i < SECH_SLOTS; i++){
    int idx = secPage * SECH_SLOTS + i;
    if(idx >= secCellN) break;
    int ix, iy; secSlotXY(i, ix, iy);
    // Area tactil con holgura: la celda entera, no solo el icono. Sin esto un
    // toque entre dos iconos no hace nada -- o peor, activa el de al lado.
    if(px >= ix - 8 && px <= ix + SECH_ICON_S + 8 &&
       py >= iy - 6 && py <= iy + SECH_ICON_S + 20) return idx;
  }
  return -1;
}
static int secDockAt(int px, int py){
  for(int i = 0; i < 2; i++){
    int x, y, w, h; secDockRect(i, x, y, w, h);
    if(px >= x && px <= x + w && py >= y && py <= y + h) return i;
  }
  return -1;
}

// Animacion corta de asentamiento al soltar el gesto de pagina. Dura poco y
// solo repinta la banda de la rejilla: nunca la pantalla entera.
static void secSettlePage(int fromDx){
  const uint32_t MS = 140;
  uint32_t t0 = millis();
  for(;;){
    uint32_t e = millis() - t0; if(e > MS) e = MS;
    float p = (float)e / (float)MS;
    float ease = 1.0f - (1.0f - p) * (1.0f - p);
    int dx = (int)(fromDx * (1.0f - ease));
    secRenderBand(dx, 0.0f);
    flexFeedWdt();
    if(e >= MS) break;
  }
}

static void secTickHome(){
  if(T.pressed){
    secDragX0 = T.x; secDragDx = 0; secDrag = false;
    secPressCell = secCellAt(T.x, T.y);
    return;
  }
  if(T.down){
    int dx = T.x - secDragX0;
    if(!secDrag && (dx < -14 || dx > 14) && secPageN > 1){ secDrag = true; secPressCell = -1; }
    if(secDrag){
      // Resistencia en los extremos: sin pagina a la que ir, el arrastre se
      // frena en vez de ensenar una pagina vacia.
      if((dx > 0 && secPage == 0) || (dx < 0 && secPage == secPageN - 1)) dx /= 3;
      secDragDx = dx;
      secRenderBand(dx, -(float)dx / (float)SCR_W);
    }
    return;
  }
  if(T.released || T.tap){
    if(secDrag){
      int dx = secDragDx;
      secDrag = false; secDragDx = 0; secPressCell = -1;
      int target = secPage;
      if(dx < -70 && secPage < secPageN - 1)      target = secPage + 1;
      else if(dx >  70 && secPage > 0)            target = secPage - 1;
      if(target != secPage){
        // Se parte del desplazamiento que ya tenia la pagina NUEVA (la que
        // asomaba por el borde), no de cero: asi no hay salto entre el
        // recorrido del dedo y la animacion que lo continua.
        int start = (target > secPage) ? dx + SCR_W : dx - SCR_W;
        secPage = target;
        secSettlePage(start);
      } else {
        secSettlePage(dx);
      }
      secLastTouch = millis();
      return;
    }
    if(!T.tap){ secPressCell = -1; return; }
    secLastTouch = millis();
    int d = secDockAt(T.x, T.y);
    if(d == 0){ secOpenVault(VW_HOME); return; }
    if(d == 1){
      secWorkspaceLock(FXV_LOCK_MANUAL);
      secGoAuth();
      secToast("Carpeta segura bloqueada");
      secRenderAuth(0);
      return;
    }
    int cell = secCellAt(T.x, T.y);
    if(cell >= 0 && cell == secPressCell) secOpenCell(cell);
    secPressCell = -1;
  }
}

static void secAuthConfirm(){
  const char* sec = (flexVaultLockType() == FLEXVAULT_LOCK_PASS) ? secPass : secPin;
  int r = flexVaultUnlock(sec);
  secKeyClear();
  if(r == FXV_OK){ secAfterUnlock(); return; }
  if(r == FXV_ERR_WAIT){ secToast("Espera antes de volver a intentarlo"); secRenderAuth(0); return; }
  if(r == FXV_ERR_WRONG){
    // Ni se pierde la tarea, ni se vuelve al escritorio, ni se revela nada: solo
    // parpadean los puntos. Y se repinta SOLO esa franja.
    secWrongMs = millis();
    secRenderAuthDots();
    return;
  }
  secToast(flexVaultError());
  secRenderAuth(0);
}

static void secTickAuth(){
  // Deslizamiento de entrada del teclado alfanumerico (0,3 s).
  if(secKbAnim){
    float p = (millis() - secKbAnim) / 300.0f;
    if(p >= 1){ p = 1; secKbAnim = 0; }
    secRenderAuth((int)((1.0f - p) * (SCR_H - KB_Y)));
    return;
  }
  // La espera por intentos fallidos se refresca sola, una vez cada medio
  // segundo. No se repinta por cuadro: seria trabajo para ver el mismo numero.
  if(flexVaultWaitMs() > 0){
    if(millis() - secWaitMs > 500){ secWaitMs = millis(); secRenderAuth(0); }
    return;
  }
  if(secWrongMs && millis() - secWrongMs > 1200){ secWrongMs = 0; secRenderAuthDots(); }

  if(flexVaultLockType() != FLEXVAULT_LOCK_PASS){
    if(!T.tap) return;
    for(int i = 0; i < 12; i++){
      int x, y, w, h; lsuPinRect(i, x, y, w, h);
      if(T.x < x || T.x > x + w || T.y < y || T.y > y + h) continue;
      if(i == 9){ int L = strlen(secPin); if(L > 0) secPin[L - 1] = 0; }
      else if(i == 11){ secAuthConfirm(); return; }
      else if(strlen(secPin) < 8){
        int L = strlen(secPin);
        secPin[L] = PIN_KEYS[i][0]; secPin[L + 1] = 0;
        // Autoconfirmar al completar los digitos del PIN guardado, igual que el
        // bloqueo del sistema y que la boveda. Solo se usa la LONGITUD, que los
        // puntos de la pantalla ya ensenan.
        int want = flexVaultSecretLen();
        if(want > 0 && (int)strlen(secPin) == want){ secAuthConfirm(); return; }
      }
      secRenderAuthDots();
      return;
    }
    return;
  }
  // Contrasena: teclado alfanumerico del sistema.
  kbFxTick(SET_CARD_BG, TH_TXT);
  if(T.pressed) kbFxPress(kbCellAt(T.x, T.y), SET_CARD_BG, TH_TXT);
  if(!T.tap) return;
  int fi = kbFRowHit(T.x, T.y);
  if(fi >= 0){
    if(fi == 0) kbShift = !kbShift;
    else if(fi == 1) mapaActivo = (mapaActivo == LAYOUT_NUM) ? LAYOUT_EMOJI :
                                 (mapaActivo == LAYOUT_EMOJI) ? (kbLangEs ? LAYOUT_ES : LAYOUT_EN) : LAYOUT_NUM;
    else if(fi == 2){ kbLangEs = !kbLangEs; if(mapaActivo == LAYOUT_ES || mapaActivo == LAYOUT_EN) mapaActivo = kbLangEs ? LAYOUT_ES : LAYOUT_EN; }
    else if(fi == 3){ size_t L = strlen(secPass); if(L + 1 < sizeof(secPass)){ secPass[L] = ' '; secPass[L + 1] = 0; } }
    else if(fi == 4){ int L = strlen(secPass); if(L > 0){ int q = L - 1; while(q > 0 && (secPass[q] & 0xC0) == 0x80) q--; secPass[q] = 0; } }
    else { secAuthConfirm(); return; }
    secRenderAuth(0);
    return;
  }
  int cell = kbCellAt(T.x, T.y);
  if(cell >= 0){
    char u[6];
    const char* k = kbResolveKey(mapaActivo[cell / KB_COLS][cell % KB_COLS], u, true);
    size_t L = strlen(secPass), kl = strlen(k);
    if(L + kl < sizeof(secPass)){ memcpy(secPass + L, k, kl); secPass[L + kl] = 0; }
    kbFxStart(cell);
    secRenderAuth(0);
  }
}

static void secRender(){
  switch(secView){
    case SEC_VW_AUTH:  secRenderAuth(0); break;
    case SEC_VW_VAULT: vaultRender();    break;
    case SEC_VW_NOFS:
      setBuf(fb);
      uiClipFull();
      secBgBand(0, SCR_H - 1);
      secDrawStatusBar();
      drawTextC(SCR_W / 2, 300, "Carpeta segura", 4, TH_ONWALL);
      drawTextC(SCR_W / 2, 356, "Sin almacenamiento disponible", 2, TH_ONWALL2);
      drawTextC(SCR_W / 2, 390, "El contenido cifrado sigue intacto", 1, TH_ONWALL2);
      flxFlushAll();
      break;
    default:           secRenderHome();  break;
  }
}

static void secfTick(){
  // ---- Inactividad: la MISMA politica de la boveda (flexVaultAutoLockMs).
  // No se inventa un temporizador nuevo ni un valor nuevo: es el que el usuario
  // configuro en la propia Carpeta segura.
  if(T.down || T.pressed || T.released) secLastTouch = millis();
  uint32_t al = flexVaultAutoLockMs();
  if(al && flexVaultUnlocked() && secView != SEC_VW_AUTH &&
     secLastTouch && millis() - secLastTouch > al){
    secSaveSnapshot();
    secWorkspaceLock(FXV_LOCK_IDLE);
    secGoAuth();
    secToast("Carpeta segura cerrada por inactividad");
    secRenderAuth(0);
    return;
  }
  // Un aviso que caduca tiene que borrarse de la pantalla.
  if(secMsg[0] && millis() - secMsgMs > 3000){ secMsg[0] = 0; secRender(); }

  switch(secView){
    case SEC_VW_AUTH:  secTickAuth(); return;
    case SEC_VW_VAULT: vaultTick();   return;
    case SEC_VW_NOFS:  return;
    default:           secTickHome(); return;
  }
}
