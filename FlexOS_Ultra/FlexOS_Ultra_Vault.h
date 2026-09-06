// #############################################################
// ##  FLEX OS ULTRA  ·  FLEX VAULT  ·  interfaz de la Carpeta segura
// ##  ----------------------------------------------------------
// ##  Todo lo que se VE de la boveda. La criptografia y el almacen viven en
// ##  FlexOS_Vault.cpp. Es un gState propio (ST_VAULT), no una app.
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
#include "FlexOS_Ultra_AppGallery.h"   // eslabon anterior de la cadena

// #############################################################
// ##  FLEX VAULT  ·  INTERFAZ (Carpeta segura)
// ##  Ajustes -> Seguridad y privacidad -> Flex Vault
// ##  ------------------------------------------------------
// ##  Todo lo que se ve de la Carpeta segura esta aqui. Lo que NO
// ##  esta aqui, a proposito, es una sola linea de criptografia: las
// ##  claves, el cifrado y el almacen viven en FlexOS_Vault.cpp. Esta
// ##  pantalla no puede leer un byte privado sin pasar por
// ##  flexVaultRead(), y no tiene forma de saber la clave maestra.
// ##
// ##  POR QUE ES UN gState Y NO UNA APP DE APP_REG
// ##  Si Flex Vault fuera una app del escritorio heredaria TRES cosas
// ##  que arruinarian su privacidad:
// ##    · Recientes: appClose() llama a swPushAndCapture(), que guarda
// ##      una MINIATURA del ultimo cuadro en PSRAM. Una miniatura de
// ##      la galeria privada en el selector de apps es exactamente la
// ##      fuga que esto tiene que evitar.
// ##    · El cajon y el buscador de Modo PC (dexFilterApps) recorren
// ##      APP_REG: la boveda saldria en el buscador del escritorio.
// ##    · El panel rapido (qsCanOpen) y la isla de notificaciones solo
// ##      se dibujan sobre ST_HOME y ST_APP, asi que como app tendria
// ##      capas del sistema pintando encima del contenido privado.
// ##  Siendo un estado propio (ST_VAULT), las tres cosas quedan
// ##  descartadas POR CONSTRUCCION, no por acordarse de filtrarlas.
// ##
// ##  CIERRE AUTOMATICO
// ##  vaultLockNow() es el unico camino de cierre y lo llaman: el
// ##  temporizador de inactividad de aqui, apagar la pantalla
// ##  (suspEnter), el auto-bloqueo, el apagado, cualquier peticion de
// ##  clave del sistema, entrar en Recientes y salir de la boveda.
// ##  Ademas la boveda arranca SIEMPRE cerrada (flexVaultBegin no
// ##  descifra nada), asi que reiniciar tampoco la deja abierta.
// #############################################################

// Vistas internas de la boveda. Igual que setView en Ajustes, NO son
// gStates nuevos: la navegacion de dentro es asunto de la boveda.
enum { VW_SETUP_SEL = 0,   // primera vez: elegir PIN o contrasena
       VW_KEYPAD,          // teclado de clave (alta, apertura o cambio)
       VW_HOME,            // estado + secciones
       VW_LIST,            // galeria / notas / archivos privados
       VW_ITEM,            // un elemento abierto
       VW_APPS,            // anadir apps a Carpeta segura
       VW_APPMAN,          // gestionar apps privadas
       VW_REMOVE,          // que hacer con los datos al quitar una app
       VW_LOG,             // registro de seguridad
       VW_NOTE,            // editor de una nota privada
       VW_APPDET };        // una app privada: abrir, bloquear, quitar

// Para que sirve el teclado que esta en pantalla.
enum { VK_CREATE = 0,      // crear la clave de la boveda
       VK_OPEN,            // abrir la boveda
       VK_CHG_OLD,         // cambio de clave: la actual
       VK_CHG_NEW };       // cambio de clave: la nueva

#define VW_MAX_ITEMS   24        // elementos por pantalla de lista
#define VW_ROW_H       62
// Primera fila del area con SCROLL. La cabecera (chevron, titulo y
// subtitulo) ocupa hasta y=52; por debajo manda el viewport y nada de lo
// que se arrastra puede escribir por encima de esta linea.
#define VW_VP_TOP      56
#define VW_ROWS_MAX    30
#define VW_TEXT_MAX    2048      // nota privada descifrada en RAM
#define VW_BLOB_MAX    (512 * 1024)   // tope para previsualizar en RAM

static int      vwView    = VW_HOME;
static int      vwKind    = FXV_KIND_PHOTO;
// De QUIEN son los elementos de la lista abierta: -1 = los que el usuario
// movio a mano desde Galeria/Notas/Archivos; >= 0 = los de esa app privada.
// Es lo que mantiene los datos de una app privada separados de verdad: la
// seccion general de Notas privadas NO ensena las notas de la Notas privada,
// y al quitar la app se sabe exactamente cuales son suyas.
static int      vwListApp = -1;
static int      vwPendApp = -1;      // app privada que hay que abrir tras la clave
static int      vwKeyFor  = VK_OPEN;
static int      vwKeyMode = FLEXVAULT_LOCK_PIN;   // PIN o contrasena en el teclado
static int      vwScroll  = 0;
static int      vwDragY0 = 0, vwDragS0 = 0;
static bool     vwDragging = false;
static uint32_t vwLastTouch = 0;                  // inactividad DENTRO de la boveda
static uint32_t vwMsgMs = 0;
static char     vwMsg[96] = "";
static int      vwAppSel  = -1;                   // app sobre la que actua VW_REMOVE
static uint16_t vwOpenId  = 0;                    // elemento abierto en VW_ITEM
static int      vwItemMenu = -1;                  // indice con el menu de acciones abierto
static bool     vwLongFired = false;

// Clave que se esta escribiendo. Se BORRA en cuanto se usa: ver
// vwKeyClear(). Nunca se guarda en NVS ni se copia a ningun sitio.
static char     vwPin[16]  = "";
static char     vwPass[FLEXVAULT_SECRET_MAX] = "";
static char     vwOldKey[FLEXVAULT_SECRET_MAX] = "";   // solo durante el cambio
static uint32_t vwWrongMs = 0;
static uint32_t vwKbAnim  = 0;

// Contenido descifrado en RAM. Solo existe mientras se esta MIRANDO un
// elemento, y se borra al salir de el o al cerrar la boveda.
static char     vwText[VW_TEXT_MAX];
static int      vwTextN = 0;
static uint8_t* vwBlob  = NULL;
static size_t   vwBlobN = 0;

// "Mover a Carpeta segura" pendiente de que el usuario meta la clave, y la
// pantalla a la que hay que devolverlo al salir de la boveda (ver
// vaultMoveRequest y vaultExit).
static char vwPendPath[FLEXFS_PATH_MAX] = "";
static int  vwPendKind = FXV_KIND_FILE;
static int  vwRetState = -1;
static int  vwRetApp   = -1;
static const char* vwMoveErr = "";

static FlexVaultItem vwItems[VW_MAX_ITEMS];
static int           vwItemsN = 0;
static FlexVaultLog  vwLog[FLEXVAULT_LOG_MAX];
static int           vwLogN = 0;

// Filas dibujadas en el ultimo repintado, para poder resolver el toque
// sin repetir la aritmetica del layout en dos sitios (que es como se
// desalinean los toques de los dibujos).
static int vwRowsN = 0;
static int vwRowY0[VW_ROWS_MAX], vwRowY1[VW_ROWS_MAX], vwRowAct[VW_ROWS_MAX];

// Acciones de fila. Los valores por encima de 1000 son "elemento i de
// la lista", para no necesitar una tabla aparte.
enum { VA_NONE = 0, VA_GAL, VA_NOTES, VA_FILES, VA_APPS, VA_APPMAN, VA_LOG,
       VA_CHGKEY, VA_LOCKNOW, VA_AUTOLOCK, VA_NEWNOTE,
       VA_APPOPEN, VA_APPLOCK, VA_APPDEL,
       VA_ITEM_BASE = 1000, VA_APP_BASE = 2000 };

static void vaultRender();
static void vaultLockNow(int reason);
static void vwOpenList(int kind);

// -------------------------------------------------------------
//  Borrado de lo sensible que tiene esta pantalla en RAM
// -------------------------------------------------------------
static void vwKeyClear(){
  flexVaultWipe(vwPin,  sizeof(vwPin));
  flexVaultWipe(vwPass, sizeof(vwPass));
}
static void vwContentClear(){
  // El texto de una nota privada y los bytes de una imagen privada son
  // contenido del usuario: no basta con "dejar de usarlos".
  flexVaultWipe(vwText, sizeof(vwText));
  vwTextN = 0;
  if(vwBlob){ flexVaultWipe(vwBlob, vwBlobN); free(vwBlob); vwBlob = NULL; }
  vwBlobN = 0;
}
static void vwForget(){
  vwKeyClear();
  flexVaultWipe(vwOldKey, sizeof(vwOldKey));
  vwContentClear();
  // La lista tambien lleva nombres reales descifrados.
  flexVaultWipe(vwItems, sizeof(vwItems));
  vwItemsN = 0;
  vwLogN = 0;
}

static void vwToast(const char* m){
  snprintf(vwMsg, sizeof(vwMsg), "%s", m ? m : "");
  vwMsgMs = millis();
}

// -------------------------------------------------------------
//  Textos de estado
// -------------------------------------------------------------
// Marca de tiempo legible de un epoch. Si el reloj nunca se ancló
// (sin NTP y sin hora guardada) se dice "sin hora", no una fecha
// inventada.
static void vwStamp(uint32_t epoch, char* out, size_t n){
  if(!epoch){ snprintf(out, n, "sin registrar"); return; }
  long local = (long)epoch + FLEXOS_TZ_OFFSET_SEC;
  long days = local / 86400L, rem = local % 86400L;
  if(rem < 0){ rem += 86400L; days--; }
  int y, mo, d; clkCivilFromDays(days, y, mo, d);
  int hh = (int)(rem / 3600L), mm = (int)((rem % 3600L) / 60L);
  long today = ((long)clkNowUtc() + FLEXOS_TZ_OFFSET_SEC) / 86400L;
  if(days == today)          snprintf(out, n, "hoy %02d:%02d", hh, mm);
  else if(days == today - 1) snprintf(out, n, "ayer %02d:%02d", hh, mm);
  else                       snprintf(out, n, "%02d/%02d/%04d %02d:%02d", d, mo, y, hh, mm);
}

static void vwAutoLockText(char* out, size_t n){
  uint32_t ms = flexVaultAutoLockMs();
  if(!ms)                snprintf(out, n, "Solo al salir");
  else if(ms < 60000u)   snprintf(out, n, "%u segundos", (unsigned)(ms / 1000u));
  else                   snprintf(out, n, "%u minuto%s", (unsigned)(ms / 60000u),
                                  (ms / 60000u) == 1 ? "" : "s");
}

// Opciones del bloqueo automatico de la boveda. Mas cortas que las del
// sistema a proposito: una carpeta segura abierta media hora sin
// vigilancia no es una carpeta segura.
static const uint32_t VW_AUTOLOCK_OPTS[5] = { 15000u, 30000u, 60000u, 300000u, 0u };
static void vwCycleAutoLock(){
  uint32_t cur = flexVaultAutoLockMs();
  int at = 0;
  for(int i = 0; i < 5; i++) if(VW_AUTOLOCK_OPTS[i] == cur){ at = i; break; }
  flexVaultSetAutoLockMs(VW_AUTOLOCK_OPTS[(at + 1) % 5]);
  vwLastTouch = millis();
}

static const char* vwKindName(int kind){
  if(kind == FXV_KIND_PHOTO) return "Galer\xC3\xAD" "a privada";
  if(kind == FXV_KIND_NOTE)  return "Notas privadas";
  return "Archivos privados";
}

// -------------------------------------------------------------
//  Piezas de dibujo
// -------------------------------------------------------------
static void vwRowsReset(){ vwRowsN = 0; }

static int vwRow(int y, int act, const char* title, const char* value,
                 bool chev, uint16_t accent){
  if(vwRowsN < VW_ROWS_MAX && act != VA_NONE){
    vwRowY0[vwRowsN] = y; vwRowY1[vwRowsN] = y + VW_ROW_H; vwRowAct[vwRowsN] = act;
    vwRowsN++;
  }
  if(y + VW_ROW_H >= 58 && y <= SCR_H - 30){          // fuera de la ventana: ni se dibuja
    if(uiGlass) drawGlassCardFlat(12, y, SCR_W - 24, VW_ROW_H, 14, TH_GLASS, TH_PAGE);
    else        fillRoundRect(12, y, SCR_W - 24, VW_ROW_H, 14, thCard());
    int tx = 26;
    if(accent){ fillRoundRect(20, y + VW_ROW_H / 2 - 12, 24, 24, 7, accent); tx = 56; }
    drawTextClip(tx, y + 10, title, 3, TH_TXT, SCR_W - 52);
    if(value && value[0]) drawTextClip(tx, y + 38, value, 1, TH_TXT2, SCR_W - 52);
    if(chev){
      int cx = SCR_W - 34, cy = y + VW_ROW_H / 2;
      strokeSegAA(cx - 4, cy - 7, cx + 3, cy, 2.2f, TH_MUTE);
      strokeSegAA(cx + 3, cy, cx - 4, cy + 7, 2.2f, TH_MUTE);
    }
  }
  return y + VW_ROW_H + 6;
}

static int vwHitRow(int px, int py){
  if(px < 12 || px > SCR_W - 12) return VA_NONE;
  for(int i = 0; i < vwRowsN; i++)
    if(py >= vwRowY0[i] && py <= vwRowY1[i]) return vwRowAct[i];
  return VA_NONE;
}

// Cabecera comun: chevron de volver + titulo. El candado del titulo NO
// es decoracion: es la senal de que lo que hay debajo esta cifrado.
static void vwHeader(const char* title, const char* sub){
  strokeSegAA(30, 30, 18, 22, 2.4f, TH_NAV);
  strokeSegAA(18, 22, 30, 14, 2.4f, TH_NAV);
  drawText(46, 12, title, 4, TH_TXT);
  if(sub && sub[0]) drawText(46, 44, sub, 1, TH_TXT2);
}
static bool vwBackHit(){ return T.tap && T.x < 52 && T.y < 56; }

// Aviso breve (lo que en un movil seria un "toast"). Se dibuja al final
// de cada repintado, asi que nunca queda tapado por una tarjeta.
static void vwDrawMsg(){
  if(!vwMsg[0]) return;
  if(millis() - vwMsgMs > 3200){ vwMsg[0] = 0; return; }
  int w = SCR_W - 40, h = 52, x = 20, y = SCR_H - 96;
  if(uiGlass) drawLiquidGlassPanel(x, y, w, h, 16, TH_GLASS2);
  else        fillRoundRect(x, y, w, h, 16, TH_SURF2);
  drawTextClip(x + 14, y + 16, vwMsg, 2, TH_TXT, x + w - 12);
}

// -------------------------------------------------------------
//  PANTALLA: estado de la boveda (VW_HOME)
// -------------------------------------------------------------
static void vwRenderHome(){
  setBuf(fb);
  fillRect(0, 0, SCR_W, SCR_H, TH_PAGE);
  vwHeader("Flex Vault", "Carpeta segura");
  vwRowsReset();

  uiClipViewport(VW_VP_TOP, SCR_H - 1);   // nada del contenido puede pisar la cabecera
  int y = 78 - vwScroll;

  // ---- Tarjeta de estado ----
  int ch = 176;
  if(y + ch >= 58){
    if(uiGlass) drawGlassCardFlat(12, y, SCR_W - 24, ch, 16, TH_GLASS, TH_PAGE);
    else        fillRoundRect(12, y, SCR_W - 24, ch, 16, thCard());
    // Candado cerrado, dibujado a mano como el resto de los iconos.
    int lx = 40, ly = y + 30;
    fillRoundRect(lx - 13, ly, 26, 20, 4, TH_OK);
    arcStroke(lx, ly, 8, 180, 360, 3, TH_OK);
    drawText(66, y + 18, "Flex Vault protegida", 3, TH_TXT);

    char v[64];
    int ry = y + 62;
    vwStamp(flexVaultLastAccess(), v, sizeof(v));
    drawText(26, ry, "\xC3\x9A" "ltimo acceso", 1, TH_TXT2);
    drawTextR(SCR_W - 26, ry, v, 1, TH_TXT); ry += 24;

    vwAutoLockText(v, sizeof(v));
    drawText(26, ry, "Bloqueo autom\xC3\xA1" "tico", 1, TH_TXT2);
    drawTextR(SCR_W - 26, ry, v, 1, TH_TXT); ry += 24;

    int f = flexVaultFails();
    snprintf(v, sizeof(v), "%d", f);
    drawText(26, ry, "Intentos fallidos", 1, TH_TXT2);
    drawTextR(SCR_W - 26, ry, v, 1, f ? TH_WARN : TH_TXT); ry += 24;

    flexFsFmtSize(flexVaultUsedBytes(), v, sizeof(v));
    drawText(26, ry, "Almacenamiento usado", 1, TH_TXT2);
    drawTextR(SCR_W - 26, ry, v, 1, TH_TXT);
  }
  y += ch + 10;

  // ---- Secciones de contenido ----
  char sub[48];
  snprintf(sub, sizeof(sub), "%d elemento%s", flexVaultCount(FXV_KIND_PHOTO),
           flexVaultCount(FXV_KIND_PHOTO) == 1 ? "" : "s");
  y = vwRow(y, VA_GAL, "Galer\xC3\xAD" "a privada", sub, true, rgb565(230,120,90));
  snprintf(sub, sizeof(sub), "%d nota%s", flexVaultCount(FXV_KIND_NOTE),
           flexVaultCount(FXV_KIND_NOTE) == 1 ? "" : "s");
  y = vwRow(y, VA_NOTES, "Notas privadas", sub, true, rgb565(235,180,60));
  snprintf(sub, sizeof(sub), "%d archivo%s", flexVaultCount(FXV_KIND_FILE),
           flexVaultCount(FXV_KIND_FILE) == 1 ? "" : "s");
  y = vwRow(y, VA_FILES, "Archivos privados", sub, true, rgb565(70,140,225));

  y += 8;
  if(y <= SCR_H - 30) drawText(26, y, "APPS", 1, TH_MUTE);
  y += 22;
  y = vwRow(y, VA_APPS,   "A\xC3\xB1" "adir apps a Carpeta segura",
            "Elige que apps tienen version privada", true, rgb565(150,110,220));
  y = vwRow(y, VA_APPMAN, "Gestionar apps privadas",
            "Almacenamiento, acceso, bloquear y quitar", true, rgb565(120,120,140));

  y += 8;
  if(y <= SCR_H - 30) drawText(26, y, "SEGURIDAD", 1, TH_MUTE);
  y += 22;
  vwAutoLockText(sub, sizeof(sub));
  y = vwRow(y, VA_AUTOLOCK, "Bloqueo autom\xC3\xA1" "tico", sub, false, rgb565(90,160,230));
  y = vwRow(y, VA_CHGKEY, "Cambiar clave de Flex Vault",
            "No se pierde ning\xC3\xBA" "n archivo", true, rgb565(220,120,120));
  y = vwRow(y, VA_LOG, "Registro de seguridad",
            "Aperturas, cierres y fallos", true, rgb565(120,150,160));
  y = vwRow(y, VA_LOCKNOW, "Bloquear ahora",
            "Cierra la b\xC3\xB3veda y borra las claves de la memoria", false, TH_OK);

  // Aviso honesto sobre el limite real de proteccion de esta version.
  y += 10;
  if(y <= SCR_H - 60){
    drawText(26, y, "El contenido va cifrado con AES-256-GCM.", 1, TH_MUTE); y += 18;
    drawText(26, y, "Secure Boot y Flash Encryption siguen", 1, TH_MUTE); y += 18;
    drawText(26, y, "desactivados: la fuerza de la clave importa.", 1, TH_MUTE); y += 18;
  }
  uiClipFull();                           // el aviso flotante va por encima de todo
  vwDragS0 = y + vwScroll;                       // alto total del contenido
  vwDrawMsg();
  flxFlushAll();
}

// -------------------------------------------------------------
//  PANTALLA: lista de contenido privado (VW_LIST)
// -------------------------------------------------------------
static void vwReload(){
  vwItemsN = flexVaultListFor(vwKind, vwListApp, vwItems, VW_MAX_ITEMS);
}

// La clase de contenido "natural" de cada app privada: es lo que se abre al
// pulsar Abrir en Gestionar apps privadas.
static int vwAppKind(int appId){
  if(appId == 5) return FXV_KIND_NOTE;    // Notas
  if(appId == 1) return FXV_KIND_PHOTO;   // Galeria
  return FXV_KIND_FILE;                   // Archivos y el resto
}

static void vwRenderList(){
  setBuf(fb);
  fillRect(0, 0, SCR_W, SCR_H, TH_PAGE);
  char sub[64], ttl[48];
  if(vwListApp >= 0){
    snprintf(ttl, sizeof(ttl), "%s privada", appName(vwListApp));
    snprintf(sub, sizeof(sub), "%d elemento%s, separados de la app normal",
             vwItemsN, vwItemsN == 1 ? "" : "s");
  } else {
    snprintf(ttl, sizeof(ttl), "%s", vwKindName(vwKind));
    snprintf(sub, sizeof(sub), "%d elemento%s dentro de Flex Vault",
             vwItemsN, vwItemsN == 1 ? "" : "s");
  }
  vwHeader(ttl, sub);
  vwRowsReset();

  uiClipViewport(VW_VP_TOP, SCR_H - 1);   // nada del contenido puede pisar la cabecera
  int y = 78 - vwScroll;
  if(vwItemsN == 0){
    drawTextC(SCR_W / 2, 300, "Todav\xC3\xAD" "a no hay nada aqu\xC3\xAD", 3, TH_TXT2);
    drawTextC(SCR_W / 2, 344, "Mant\xC3\xA9n pulsado un elemento en Galer\xC3\xAD" "a,", 1, TH_MUTE);
    drawTextC(SCR_W / 2, 364, "Notas o Archivos y elige", 1, TH_MUTE);
    drawTextC(SCR_W / 2, 384, "\"Mover a Carpeta segura\"", 1, TH_MUTE);
  }
  for(int i = 0; i < vwItemsN; i++){
    char v[64], sz[16], st[32];
    flexFsFmtSize(vwItems[i].size, sz, sizeof(sz));
    vwStamp(vwItems[i].added, st, sizeof(st));
    snprintf(v, sizeof(v), "%s  \xC2\xB7  %s", sz, st);
    y = vwRow(y, VA_ITEM_BASE + i, vwItems[i].name, v, true,
              vwKind == FXV_KIND_PHOTO ? rgb565(230,120,90) :
              vwKind == FXV_KIND_NOTE  ? rgb565(235,180,60) : rgb565(70,140,225));
  }
  if(vwKind == FXV_KIND_NOTE){
    y += 8;
    y = vwRow(y, VA_NEWNOTE, "Nueva nota privada",
              "Se crea ya cifrada dentro de la b\xC3\xB3veda", false, TH_OK);
  }
  uiClipFull();                           // el aviso flotante va por encima de todo
  vwDragS0 = y + vwScroll;

  // Menu de acciones del elemento (se dibuja encima).
  if(vwItemMenu >= 0 && vwItemMenu < vwItemsN){
    int w = 300, h = 4 * 52 + 20, x = (SCR_W - w) / 2, my = (SCR_H - h) / 2;
    if(uiGlass) drawLiquidGlassPanel(x, my, w, h, 18, TH_GLASS2);
    else        fillRoundRect(x, my, w, h, 18, TH_SURF2);
    const char* lb[4] = { "Abrir", "Renombrar", "Sacar de la b\xC3\xB3veda",
                          "Eliminar definitivamente" };
    uint16_t cl[4] = { TH_TXT, TH_TXT, TH_TXT, TH_ERR };
    for(int i = 0; i < 4; i++)
      drawTextClip(x + 18, my + 10 + i * 52 + 16, lb[i], 2, cl[i], x + w - 12);
  }
  vwDrawMsg();
  flxFlushAll();
}

// Rectangulo de la opcion i del menu de acciones (misma aritmetica que
// el dibujo, en un solo sitio).
static void vwMenuRect(int i, int &x, int &y, int &w, int &h){
  w = 300; h = 52;
  x = (SCR_W - w) / 2;
  int th = 4 * 52 + 20;
  y = (SCR_H - th) / 2 + 10 + i * 52;
}

// -------------------------------------------------------------
//  PANTALLA: un elemento abierto (VW_ITEM)
//  ------------------------------------------------------------
//  Aqui esta la parte que hace que la Galeria privada sea real y no un
//  listado de nombres: el contenido se descifra A RAM y se pinta desde
//  ahi. En ningun momento se escribe una copia en claro en la
//  particion -- que es justo lo que haria falta si hubiera que
//  reutilizar el visor normal, que lee de un fichero.
//    · .jpg -> flexJpegDecode() decodifica desde el buffer de RAM
//    · .fxp -> flexPaintReplayMem() reproduce los trazos desde RAM
//    · .txt -> se pinta el texto descifrado
//    · lo demas -> se dice honestamente que no hay visor, con la
//      opcion de sacarlo de la boveda para abrirlo fuera.
// -------------------------------------------------------------
static int vwImgX0, vwImgY0, vwImgW, vwImgH, vwImgRow;

static bool vwJpegRow(void* user, int y, int w, const uint16_t* rgb){
  (void)user;
  int dy = vwImgY0 + y;
  if(dy < 0 || dy >= SCR_H) return true;
  int n = w; if(vwImgX0 + n > SCR_W) n = SCR_W - vwImgX0;
  if(n > 0) memcpy(gBuf + (size_t)dy * SCR_W + vwImgX0, rgb, (size_t)n * 2);
  vwImgRow = y;
  return true;
}
static void* vwJpegAlloc(size_t n){
  void* p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return p ? p : malloc(n);
}
static void vwJpegFree(void* p){ heap_caps_free(p); }

static void vwPaintSeg(int x0, int y0, int x1, int y1, uint16_t color, int radius, void* user){
  (void)user;
  strokeSegAA(vwImgX0 + x0, vwImgY0 + y0, vwImgX0 + x1, vwImgY0 + y1,
              (float)(radius > 0 ? radius : 1), color);
}

// Comparacion de extension sin distinguir mayusculas. NO usa strcasecmp:
// vive en <strings.h> (POSIX) y no esta garantizada en todos los cores de
// Arduino, por eso esta comparacion se implementa localmente.
static bool vwNameEndsWith(const char* name, const char* ext){
  size_t ln = strlen(name), le = strlen(ext);
  if(ln < le) return false;
  const char* a = name + ln - le;
  for(size_t i = 0; i < le; i++){
    char ca = a[i], cb = ext[i];
    if(ca >= 'A' && ca <= 'Z') ca = (char)(ca + 32);
    if(cb >= 'A' && cb <= 'Z') cb = (char)(cb + 32);
    if(ca != cb) return false;
  }
  return true;
}

// Carga el elemento abierto a RAM. Devuelve false y deja un aviso si no
// se pudo (demasiado grande, integridad, o boveda cerrada).
static bool vwLoadOpen(){
  vwContentClear();
  FlexVaultItem it;
  if(!flexVaultGet(vwOpenId, &it)) return false;
  if(it.kind == FXV_KIND_NOTE || vwNameEndsWith(it.name, ".txt")){
    int rd = flexVaultRead(vwOpenId, vwText, sizeof(vwText) - 1);
    if(rd < 0){ vwToast(flexVaultError()); return false; }
    vwText[rd] = 0; vwTextN = rd;
    return true;
  }
  if(it.size == 0) return true;                       // elemento vacio: nada que cargar
  if(it.size > VW_BLOB_MAX){
    vwToast("Demasiado grande para previsualizar dentro de la b\xC3\xB3veda");
    return false;
  }
  vwBlob = (uint8_t*)heap_caps_malloc(it.size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if(!vwBlob) vwBlob = (uint8_t*)malloc(it.size);
  if(!vwBlob){ vwToast("Sin memoria para abrirlo"); return false; }
  vwBlobN = it.size;
  int rd = flexVaultRead(vwOpenId, vwBlob, vwBlobN);
  if(rd < 0){ vwContentClear(); vwToast(flexVaultError()); return false; }
  vwBlobN = (size_t)rd;
  return true;
}

static void vwRenderItem(){
  setBuf(fb);
  fillRect(0, 0, SCR_W, SCR_H, TH_PAGE);
  FlexVaultItem it;
  if(!flexVaultGet(vwOpenId, &it)){ vwView = VW_LIST; vwRenderList(); return; }
  char sz[16]; flexFsFmtSize(it.size, sz, sizeof(sz));
  vwHeader(vwKindName(vwKind), sz);
  drawTextClip(46, 44, it.name, 1, TH_TXT2, SCR_W - 20);
  vwRowsReset();

  int top = 84, bot = SCR_H - 120;
  vwImgX0 = 12; vwImgY0 = top; vwImgW = SCR_W - 24; vwImgH = bot - top;

  if(vwTextN > 0 || (it.kind == FXV_KIND_NOTE && it.size == 0)){
    if(uiGlass) drawGlassCardFlat(12, top, vwImgW, vwImgH, 14, TH_GLASS, TH_PAGE);
    else        fillRoundRect(12, top, vwImgW, vwImgH, 14, thCard());
    if(vwTextN > 0) fkTextBox(26, top + 14, vwImgW - 28, vwImgH - 28, vwText, 2, TH_TXT);
    else            drawText(26, top + 14, "(vac\xC3\xAD" "a)", 2, TH_MUTE);
  } else if(vwBlob && vwNameEndsWith(it.name, FLEXFS_EXT_PAINT)){
    fillRoundRect(12, top, vwImgW, vwImgH, 14, rgb565(250,250,252));
    FlexPaintHdr hd;
    if(flexPaintHeaderMem(vwBlob, vwBlobN, &hd) && hd.w && hd.h){
      float sc = (float)vwImgW / (float)hd.w;
      float sy = (float)vwImgH / (float)hd.h;
      if(sy < sc) sc = sy;
      vwImgX0 = 12 + (vwImgW - (int)(hd.w * sc)) / 2;
      vwImgY0 = top + (vwImgH - (int)(hd.h * sc)) / 2;
      flexPaintReplayMem(vwBlob, vwBlobN, sc, 0, 0, vwPaintSeg, NULL);
    } else {
      drawTextC(SCR_W / 2, top + vwImgH / 2, "Dibujo no reconocido", 2, TH_MUTE);
    }
  } else if(vwBlob && (vwNameEndsWith(it.name, ".jpg") || vwNameEndsWith(it.name, ".jpeg"))){
    fillRoundRect(12, top, vwImgW, vwImgH, 14, rgb565(20,20,24));
    FlexJpegInfo inf;
    // Se decodifica DESDE RAM y directo al framebuffer, fila a fila. La
    // caja de destino limita el tamano, asi que una foto grande se
    // reduce dentro del decodificador y no hay que reservar la imagen
    // completa a tamano real.
    if(flexJpegProbe(vwBlob, vwBlobN, &inf) == FLEXJPG_OK){
      int dw = inf.width, dh = inf.height;
      int den = 1;
      while(den < 8 && (dw / den > vwImgW || dh / den > vwImgH)) den *= 2;
      dw /= den; dh /= den;
      vwImgX0 = 12 + (vwImgW - dw) / 2;
      vwImgY0 = top + (vwImgH - dh) / 2;
      vwImgRow = -1;
      int r = flexJpegDecode(vwBlob, vwBlobN, vwImgW, vwImgH, 0, NULL,
                             vwJpegRow, NULL, vwJpegAlloc, vwJpegFree);
      if(r != FLEXJPG_OK && vwImgRow < 0)
        drawTextC(SCR_W / 2, top + vwImgH / 2, flexJpegErrStr(r), 2, TH_ONWALL2);
    } else {
      drawTextC(SCR_W / 2, top + vwImgH / 2, "Imagen no reconocida", 2, TH_ONWALL2);
    }
  } else {
    if(uiGlass) drawGlassCardFlat(12, top, vwImgW, vwImgH, 14, TH_GLASS, TH_PAGE);
    else        fillRoundRect(12, top, vwImgW, vwImgH, 14, thCard());
    drawTextC(SCR_W / 2, top + vwImgH / 2 - 30, "Guardado y cifrado", 3, TH_TXT);
    drawTextC(SCR_W / 2, top + vwImgH / 2 + 6,
              "Flex OS todav\xC3\xAD" "a no tiene visor para este tipo", 1, TH_MUTE);
    drawTextC(SCR_W / 2, top + vwImgH / 2 + 26,
              "S\xC3\xA1" "calo de la b\xC3\xB3veda para abrirlo fuera", 1, TH_MUTE);
  }

  int y = SCR_H - 112;
  y = vwRow(y, VA_ITEM_BASE, "Sacar de la b\xC3\xB3veda",
            "Vuelve a la app normal descifrado", false, rgb565(70,140,225));
  vwDrawMsg();
  flxFlushAll();
}


// -------------------------------------------------------------
//  PANTALLA: editor de una nota privada (VW_NOTE)
//  ------------------------------------------------------------
//  Una "nota privada" que no se pueda escribir no seria una nota, asi
//  que aqui esta el editor. Usa EL MISMO teclado del sistema que Notas,
//  la clave de Wi-Fi y el dialogo de nombre: no hay un segundo teclado
//  que mantener.
//
//  Lo que cambia respecto a Notas es donde van los bytes: el texto vive
//  en vwText (RAM) y se guarda con flexVaultWrite(), o sea CIFRADO
//  dentro de la boveda. En ningun momento se escribe un .txt en claro en
//  la particion, y al salir el buffer se borra de RAM.
// -------------------------------------------------------------
#define VW_NOTE_AUTOSAVE_MS 2000
static uint32_t vwNoteDirtyMs = 0;

static void vwNoteSave(){
  if(!vwNoteDirtyMs) return;
  if(!flexVaultWrite(vwOpenId, vwText, (size_t)vwTextN)) vwToast(flexVaultError());
  vwNoteDirtyMs = 0;
}

static void vwNoteInsert(const char* k){
  size_t kl = strlen(k);
  if(vwTextN + (int)kl >= VW_TEXT_MAX - 1){ vwToast("La nota alcanzo su tamano maximo"); return; }
  memcpy(vwText + vwTextN, k, kl);
  vwTextN += (int)kl;
  vwText[vwTextN] = 0;
  vwNoteDirtyMs = millis();
}

static void vwNoteBackspace(){
  if(vwTextN <= 0) return;
  int q = vwTextN - 1;
  while(q > 0 && (vwText[q] & 0xC0) == 0x80) q--;   // no partir un caracter UTF-8
  vwText[q] = 0;
  vwTextN = q;
  vwNoteDirtyMs = millis();
}

static void vwRenderNote(){
  setBuf(fb);
  fillRect(0, 0, SCR_W, SCR_H, TH_PAGE);
  FlexVaultItem it;
  const char* nm = flexVaultGet(vwOpenId, &it) ? it.name : "Nota privada";
  vwHeader("Nota privada", "Cifrada dentro de Flex Vault");
  drawTextClip(46, 44, nm, 1, TH_TXT2, SCR_W - 20);

  int top = 78, bot = KB_Y - 10;
  if(uiGlass) drawGlassCardFlat(12, top, SCR_W - 24, bot - top, 14, TH_GLASS, TH_PAGE);
  else        fillRoundRect(12, top, SCR_W - 24, bot - top, 14, thCard());
  if(vwTextN > 0) fkTextBox(26, top + 12, SCR_W - 52, bot - top - 24, vwText, 2, TH_TXT);
  else            drawText(26, top + 12, "Escribe con el teclado", 2, TH_MUTE);

  int ky = KB_Y;
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
  const char* lb[KB_FKEYS] = { "shift", kbLayerLabel(), kbLangEs ? "ES" : "EN", "espacio", "<-", "Guardar" };
  for(int i = 0; i < KB_FKEYS; i++) kbFKey(kbFKeyX(i), fy, kbFKeyW(i), lb[i], (i == 0) && kbShift);
  vwDrawMsg();
  flxFlushAll();
}

// Devuelve true si el toque era para el editor (y ya se atendio).
static bool vwNoteTick(){
  if(vwNoteDirtyMs && millis() - vwNoteDirtyMs > VW_NOTE_AUTOSAVE_MS) vwNoteSave();
  if(vwBackHit()){
    vwNoteSave();
    vwContentClear();
    vwNoteDirtyMs = 0;
    vwOpenList(FXV_KIND_NOTE);
    vaultRender();
    return true;
  }
  kbFxTick(SET_CARD_BG, TH_TXT);
  if(T.pressed) kbFxPress(kbCellAt(T.x, T.y), SET_CARD_BG, TH_TXT);
  if(!T.tap) return true;
  int fi = kbFRowHit(T.x, T.y);
  if(fi >= 0){
    if(fi == 0) kbShift = !kbShift;
    else if(fi == 1) mapaActivo = (mapaActivo == LAYOUT_NUM) ? LAYOUT_EMOJI :
                                 (mapaActivo == LAYOUT_EMOJI) ? (kbLangEs ? LAYOUT_ES : LAYOUT_EN) : LAYOUT_NUM;
    else if(fi == 2){ kbLangEs = !kbLangEs; if(mapaActivo == LAYOUT_ES || mapaActivo == LAYOUT_EN) mapaActivo = kbLangEs ? LAYOUT_ES : LAYOUT_EN; }
    else if(fi == 3) vwNoteInsert(" ");
    else if(fi == 4) vwNoteBackspace();
    else { vwNoteDirtyMs = millis(); vwNoteSave(); vwToast("Nota guardada cifrada"); }
    vaultRender();
    return true;
  }
  int cell = kbCellAt(T.x, T.y);
  if(cell >= 0){
    char u[6];
    const char* k = kbResolveKey(mapaActivo[cell / KB_COLS][cell % KB_COLS], u, true);
    vwNoteInsert(k);
    kbFxStart(cell);
    vaultRender();
  }
  return true;
}

// -------------------------------------------------------------
//  PANTALLA: anadir apps a Carpeta segura (VW_APPS)
//  ------------------------------------------------------------
//  Aqui NO se ofrece "todas las apps". Se ofrecen las candidatas que
//  se evaluaron de verdad, y cada una dice su estado real: compatible,
//  o el motivo concreto por el que todavia no lo es. Una version
//  privada que comparta datos con la normal seria una mentira, asi que
//  no se ofrece.
// -------------------------------------------------------------
static const int VW_CAND[6] = { 1, 5, 3, 7, 10, 14 };   // Galeria, Notas, Archivos, Navegador, Paint, Calendario

static void vwRenderApps(){
  setBuf(fb);
  fillRect(0, 0, SCR_W, SCR_H, TH_PAGE);
  vwHeader("A\xC3\xB1" "adir apps", "Apps con versi\xC3\xB3n privada dentro de Flex Vault");
  vwRowsReset();
  uiClipViewport(VW_VP_TOP, SCR_H - 1);   // nada del contenido puede pisar la cabecera
  int y = 78 - vwScroll;

  for(int i = 0; i < 6; i++){
    int id = VW_CAND[i];
    bool sup = flexVaultAppSupported(id);
    char v[128];
    if(sup) snprintf(v, sizeof(v), flexVaultAppAdded(id)
                     ? "A\xC3\xB1" "adida  \xC2\xB7  toca para quitarla"
                     : "Compatible  \xC2\xB7  toca para a\xC3\xB1" "adirla");
    else    snprintf(v, sizeof(v), "%s", flexVaultAppReason(id));
    y = vwRow(y, sup ? (VA_APP_BASE + id) : VA_NONE, appName(id), v, false,
              sup ? (flexVaultAppAdded(id) ? TH_OK : rgb565(150,110,220)) : rgb565(110,110,120));
  }

  y += 10;
  if(y <= SCR_H - 40){
    drawText(26, y, "Las apps del sistema no pueden entrar en la", 1, TH_MUTE); y += 18;
    drawText(26, y, "b\xC3\xB3veda: Ajustes, actualizaciones, seguridad,", 1, TH_MUTE); y += 18;
    drawText(26, y, "bloqueo, apagado y Modo PC quedan fuera.", 1, TH_MUTE); y += 18;
  }
  uiClipFull();                           // el aviso flotante va por encima de todo
  vwDragS0 = y + vwScroll;
  vwDrawMsg();
  flxFlushAll();
}

// -------------------------------------------------------------
//  PANTALLA: gestionar apps privadas (VW_APPMAN)
// -------------------------------------------------------------
static void vwRenderAppMan(){
  setBuf(fb);
  fillRect(0, 0, SCR_W, SCR_H, TH_PAGE);
  vwHeader("Apps privadas", "Almacenamiento, acceso y candado de cada una");
  vwRowsReset();
  uiClipViewport(VW_VP_TOP, SCR_H - 1);   // nada del contenido puede pisar la cabecera
  int y = 78 - vwScroll;
  int n = 0;

  for(int i = 0; i < 6; i++){
    int id = VW_CAND[i];
    if(!flexVaultAppAdded(id)) continue;
    n++;
    char v[96], sz[16], st[32];
    flexFsFmtSize(flexVaultAppBytes(id), sz, sizeof(sz));
    vwStamp(flexVaultAppLast(id), st, sizeof(st));
    snprintf(v, sizeof(v), "%s  \xC2\xB7  %s%s", sz, st,
             flexVaultAppLocked(id) ? "  \xC2\xB7  bloqueada" : "");
    y = vwRow(y, VA_APP_BASE + id, appName(id), v, true,
              flexVaultAppLocked(id) ? rgb565(220,120,120) : TH_OK);
  }
  if(n == 0){
    drawTextC(SCR_W / 2, 300, "No hay apps privadas", 3, TH_TXT2);
    drawTextC(SCR_W / 2, 344, "A\xC3\xB1" "adelas desde \"A\xC3\xB1" "adir apps\"", 1, TH_MUTE);
  }
  uiClipFull();                           // el aviso flotante va por encima de todo
  vwDragS0 = y + vwScroll;
  vwDrawMsg();
  flxFlushAll();
}


// -------------------------------------------------------------
//  PANTALLA: una app privada (VW_APPDET)
//  ------------------------------------------------------------
//  Es la pantalla de "Gestionar apps privadas" para UNA app: cuanto
//  ocupa, cuando se uso por ultima vez, y las tres acciones que pide la
//  funcion -- Abrir, Bloquear y Quitar de la boveda.
//
//  Que significa BLOQUEAR de verdad: no es una etiqueta. Con el candado
//  puesto, abrir esa app privada CIERRA la boveda y vuelve a pedir la
//  clave; solo despues se abre. Sin eso, "bloqueada" seria un adorno.
// -------------------------------------------------------------
static void vwRenderAppDet(){
  setBuf(fb);
  fillRect(0, 0, SCR_W, SCR_H, TH_PAGE);
  if(vwAppSel < 0){ vwView = VW_APPMAN; vwRenderAppMan(); return; }
  vwHeader(appName(vwAppSel), "Versi\xC3\xB3n privada dentro de Flex Vault");
  vwRowsReset();

  char v[64], sz[16], st[32];
  flexFsFmtSize(flexVaultAppBytes(vwAppSel), sz, sizeof(sz));
  vwStamp(flexVaultAppLast(vwAppSel), st, sizeof(st));
  int y = 84;
  drawText(26, y, "Almacenamiento usado", 1, TH_TXT2);
  drawTextR(SCR_W - 26, y, sz, 1, TH_TXT); y += 24;
  drawText(26, y, "\xC3\x9A" "ltimo acceso", 1, TH_TXT2);
  drawTextR(SCR_W - 26, y, st, 1, TH_TXT); y += 24;
  snprintf(v, sizeof(v), "%d", flexVaultCountFor(vwAppKind(vwAppSel), vwAppSel));
  drawText(26, y, "Elementos privados", 1, TH_TXT2);
  drawTextR(SCR_W - 26, y, v, 1, TH_TXT); y += 34;

  y = vwRow(y, VA_APPOPEN, "Abrir",
            flexVaultAppLocked(vwAppSel) ? "Pedir\xC3\xA1 la clave de Flex Vault"
                                         : "Sus datos privados, separados de la app normal",
            true, TH_OK);
  y = vwRow(y, VA_APPLOCK,
            flexVaultAppLocked(vwAppSel) ? "Desbloquear" : "Bloquear",
            flexVaultAppLocked(vwAppSel)
              ? "Se abrir\xC3\xA1 sin volver a pedir la clave"
              : "Cerrar\xC3\xA1 la b\xC3\xB3veda y pedir\xC3\xA1 la clave al abrirla",
            false, rgb565(220,120,120));
  y = vwRow(y, VA_APPDEL, "Quitar de la b\xC3\xB3veda",
            "Preguntar\xC3\xA1 qu\xC3\xA9 hacer con sus datos", true, rgb565(120,120,140));
  vwDragS0 = 0;
  vwDrawMsg();
  flxFlushAll();
}

// -------------------------------------------------------------
//  PANTALLA: que hacer con los datos al quitar una app (VW_REMOVE)
// -------------------------------------------------------------
static void vwRenderRemove(){
  setBuf(fb);
  fillRect(0, 0, SCR_W, SCR_H, TH_PAGE);
  vwHeader("Quitar de la b\xC3\xB3veda",
           vwAppSel >= 0 ? appName(vwAppSel) : "");
  vwRowsReset();
  int y = 100;
  char v[64];
  flexFsFmtSize(vwAppSel >= 0 ? flexVaultAppBytes(vwAppSel) : 0, v, sizeof(v));
  drawText(26, 78, "Datos privados de esta app:", 1, TH_TXT2);
  drawTextR(SCR_W - 26, 78, v, 1, TH_TXT);

  y = vwRow(y, VA_APP_BASE + 0, "Mover a la app normal",
            "Se descifran y vuelven a su carpeta de siempre", false, rgb565(70,140,225));
  y = vwRow(y, VA_APP_BASE + 1, "Mantener cifrados en la b\xC3\xB3veda",
            "Siguen dentro, sin app que los abra", false, rgb565(150,110,220));
  y = vwRow(y, VA_APP_BASE + 2, "Eliminar definitivamente",
            "No hay papelera ni forma de recuperarlos", false, TH_ERR);

  y += 14;
  drawText(26, y, "\"Eliminar definitivamente\" borra los datos de", 1, TH_WARN); y += 18;
  drawText(26, y, "esta app dentro de la b\xC3\xB3veda. No pasan por la", 1, TH_WARN); y += 18;
  drawText(26, y, "papelera y no se pueden recuperar despu\xC3\xA9s.", 1, TH_WARN);
  vwDragS0 = 0;
  vwDrawMsg();
  flxFlushAll();
}

// -------------------------------------------------------------
//  PANTALLA: registro de seguridad (VW_LOG)
//  ------------------------------------------------------------
//  Solo codigos y horas. Ni un nombre, ni un tipo de fichero, ni un
//  fragmento de contenido: un registro que dijera "entro DNI.jpg"
//  seria una lista de lo que hay dentro de la boveda.
// -------------------------------------------------------------
static const char* vwEvName(uint8_t ev, int8_t aux){
  switch(ev){
    case FXV_EV_CREATE:    return "Flex Vault creada";
    case FXV_EV_UNLOCK:    return "Apertura correcta";
    case FXV_EV_FAIL:      return "Intento fallido";
    case FXV_EV_LOCK:
      switch(aux){
        case FXV_LOCK_IDLE:   return "Cierre por inactividad";
        case FXV_LOCK_SCREEN: return "Cierre al apagar la pantalla";
        case FXV_LOCK_EXIT:   return "Cierre al salir";
        case FXV_LOCK_BOOT:   return "Cierre por reinicio";
        default:              return "Cierre manual";
      }
    case FXV_EV_CHANGEKEY: return "Cambio de clave";
    case FXV_EV_IN:        return "Entr\xC3\xB3 un elemento";
    case FXV_EV_OUT:       return "Sali\xC3\xB3 un elemento";
    case FXV_EV_DEL:       return "Elemento eliminado";
    case FXV_EV_APPADD:    return "App a\xC3\xB1" "adida";
    case FXV_EV_APPDEL:    return "App quitada";
    case FXV_EV_WIPE:      return "Datos de app eliminados";
  }
  return "Evento";
}

static void vwRenderLog(){
  setBuf(fb);
  fillRect(0, 0, SCR_W, SCR_H, TH_PAGE);
  vwHeader("Registro de seguridad", "Sin nombres ni contenido, y cifrado");
  vwRowsReset();
  uiClipViewport(VW_VP_TOP, SCR_H - 1);   // nada del contenido puede pisar la cabecera
  int y = 78 - vwScroll;
  if(vwLogN == 0) drawTextC(SCR_W / 2, 300, "Sin actividad registrada", 3, TH_TXT2);
  for(int i = 0; i < vwLogN; i++){
    char st[32]; vwStamp(vwLog[i].ts, st, sizeof(st));
    uint16_t ac = (vwLog[i].ev == FXV_EV_FAIL) ? TH_ERR :
                  (vwLog[i].ev == FXV_EV_UNLOCK) ? TH_OK : rgb565(120,150,160);
    y = vwRow(y, VA_NONE, vwEvName(vwLog[i].ev, vwLog[i].aux), st, false, ac);
  }
  uiClipFull();                           // el aviso flotante va por encima de todo
  vwDragS0 = y + vwScroll;
  vwDrawMsg();
  flxFlushAll();
}

// -------------------------------------------------------------
//  PANTALLA: elegir metodo de clave la primera vez (VW_SETUP_SEL)
// -------------------------------------------------------------
static void vwRenderSetupSel(){
  setBuf(fb);
  fillRect(0, 0, SCR_W, SCR_H, TH_PAGE);
  vwHeader("Flex Vault", "");
  drawTextC(SCR_W / 2, 100, "Crea tu Carpeta segura", 4, TH_TXT);
  drawTextC(SCR_W / 2, 150, "Elige una clave PROPIA, distinta de la", 1, TH_TXT2);
  drawTextC(SCR_W / 2, 170, "del bloqueo de Flex OS", 1, TH_TXT2);
  vwRowsReset();
  int bw = SCR_W - 80, bh = 110, y1 = 230, y2 = y1 + bh + 24;
  const char* lbl[2] = { "PIN", "Contrase\xC3\xB1" "a" };
  const char* sub[2] = { "4 a 8 d\xC3\xAD" "gitos", "M\xC3\xA1s larga, mucho m\xC3\xA1s fuerte" };
  int ys[2] = { y1, y2 };
  for(int k = 0; k < 2; k++){
    if(uiGlass) drawLiquidGlassPanel(40, ys[k], bw, bh, 22, mix565(TH_PRIM, TH_SURF, 60));
    else        fillRoundRect(40, ys[k], bw, bh, 22, TH_PRIM);
    drawTextC(SCR_W / 2, ys[k] + 26, lbl[k], 4, TH_ONACC);
    drawTextC(SCR_W / 2, ys[k] + 74, sub[k], 1, TH_ONACC);
    if(vwRowsN < VW_ROWS_MAX){
      vwRowY0[vwRowsN] = ys[k]; vwRowY1[vwRowsN] = ys[k] + bh;
      vwRowAct[vwRowsN] = (k == 0) ? FLEXVAULT_LOCK_PIN : FLEXVAULT_LOCK_PASS;
      vwRowsN++;
    }
  }
  int y = y2 + bh + 30;
  drawText(26, y, "Sin Secure Boot ni Flash Encryption (los dos", 1, TH_MUTE); y += 18;
  drawText(26, y, "siguen desactivados a prop\xC3\xB3sito en esta", 1, TH_MUTE); y += 18;
  drawText(26, y, "versi\xC3\xB3n), quien tenga la placa puede leer la", 1, TH_MUTE); y += 18;
  drawText(26, y, "flash. Lo que le frena es la fuerza de tu clave:", 1, TH_MUTE); y += 18;
  drawText(26, y, "una contrase\xC3\xB1" "a aguanta; un PIN de 4, poco.", 1, TH_MUTE);
  vwDrawMsg();
  flxFlushAll();
}

// -------------------------------------------------------------
//  PANTALLA: teclado de clave (VW_KEYPAD)
//  ------------------------------------------------------------
//  Reutiliza la geometria del teclado numerico del bloqueo del sistema
//  (lsuPinRect, PIN_KEYS) y el teclado alfanumerico del sistema
//  (mapaActivo, kbPaintKey, kbCellAt): no hay un segundo teclado que
//  mantener. Lo que NO se reutiliza es la maquina de estados de
//  lsuTick, para no mezclar la clave del sistema con la de la boveda
//  -- son dos claves distintas a proposito.
// -------------------------------------------------------------
static const char* vwKeyTitle(){
  if(vwKeyFor == VK_CREATE)  return vwKeyMode == FLEXVAULT_LOCK_PIN ? "Crea el PIN de Flex Vault" : "Crea la contrase\xC3\xB1" "a";
  if(vwKeyFor == VK_CHG_OLD) return "Clave actual de Flex Vault";
  if(vwKeyFor == VK_CHG_NEW) return vwKeyMode == FLEXVAULT_LOCK_PIN ? "Nuevo PIN" : "Nueva contrase\xC3\xB1" "a";
  return "Abre Flex Vault";
}

static void vwPaintKeypad(int yoff){
  fillRect(0, 0, SCR_W, SCR_H, TH_PAGE);
  strokeSegAA(30, 30, 18, 22, 2.4f, TH_NAV);
  strokeSegAA(18, 22, 30, 14, 2.4f, TH_NAV);
  const char* ttl = vwKeyTitle();
  drawTextC(SCR_W / 2, 54, ttl, uiFontFit(ttl, SCR_W - 60, 3), TH_TXT);

  // Espera por intentos fallidos: el teclado queda inerte y se dice
  // cuanto falta. La espera vive en el modulo y sobrevive al reinicio,
  // asi que salir de aqui no la perdona.
  uint32_t wait = flexVaultWaitMs();
  if(wait > 0){
    int secs = (int)((wait + 999) / 1000);
    char cd[24];
    if(secs >= 60) snprintf(cd, sizeof(cd), "%d:%02d", secs / 60, secs % 60);
    else           snprintf(cd, sizeof(cd), "%d s", secs);
    drawTextC(SCR_W / 2, 200, "Demasiados intentos fallidos", 2, TH_ERR);
    drawTextC(SCR_W / 2, 240, cd, 4, TH_TXT);
    drawTextC(SCR_W / 2, 320, "La espera sigue aunque reinicies", 1, TH_MUTE);
    return;
  }

  int n = (vwKeyMode == FLEXVAULT_LOCK_PIN) ? (int)strlen(vwPin) : utf8Count(vwPass);
  uint16_t dc = (vwWrongMs && millis() - vwWrongMs < 600) ? TH_ERR : TH_PRIM;
  for(int i = 0; i < 8; i++){
    int cx = SCR_W / 2 - 4 * 28 + 14 + i * 28;
    if(i < n) fillCircle(cx, 130, 8, dc);
    else      drawCircle(cx, 130, 8, TH_TXT2);
  }
  if(vwWrongMs && millis() - vwWrongMs < 1200)
    drawTextC(SCR_W / 2, 166, "Clave incorrecta", 2, TH_ERR);
  else if(vwKeyFor == VK_CREATE || vwKeyFor == VK_CHG_NEW)
    drawTextC(SCR_W / 2, 166, vwKeyMode == FLEXVAULT_LOCK_PIN
              ? "M\xC3\xAD" "nimo 4 d\xC3\xAD" "gitos" : "M\xC3\xAD" "nimo 4 caracteres", 1, TH_MUTE);

  if(vwKeyMode == FLEXVAULT_LOCK_PIN){
    for(int i = 0; i < 12; i++){
      int x, y, w, h; lsuPinRect(i, x, y, w, h);
      if(uiGlass) drawLiquidGlassPanel(x, y, w, h, 16, SET_CARD_GLASS);
      else        fillRoundRect(x, y, w, h, 16, SET_CARD_BG);
      uint16_t col = (i == 9) ? TH_WARN : (i == 11) ? TH_OK : TH_TXT;
      drawTextC(x + w / 2, y + h / 2 - 12, PIN_KEYS[i], 3, col);
    }
    return;
  }
  // Teclado alfanumerico del sistema, con el mismo deslizamiento de
  // entrada que usa la pantalla de contrasena del bloqueo.
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

static void vwRenderKeypad(int yoff){
  setBuf(fb);
  vwPaintKeypad(yoff);
  vwDrawMsg();
  flxFlushAll();
}

// -------------------------------------------------------------
//  Navegacion
// -------------------------------------------------------------
static void vwGoKeypad(int what, int mode){
  vwKeyFor = what;
  vwKeyMode = mode;
  vwKeyClear();
  vwWrongMs = 0;
  vwView = VW_KEYPAD;
  vwScroll = 0;
  // Teclado limpio: sin barra de sugerencias ni chips en una pantalla
  // de clave, igual que en el bloqueo del sistema.
  mapaActivo = LAYOUT_ES; kbLangEs = true; kbShift = false;
  kbExtrasOn = false; kbApplySize(); kbMtSurfaceReset();
  vwKbAnim = (mode == FLEXVAULT_LOCK_PASS) ? millis() : 0;
  vwLastTouch = millis();
}

static void vwGoHome(){
  vwView = VW_HOME;
  vwScroll = 0;
  vwItemMenu = -1;
  vwContentClear();
  vwLastTouch = millis();
}

static void vwOpenList(int kind){
  vwKind = kind;
  vwListApp = -1;                     // secciones generales de la boveda
  vwScroll = 0;
  vwItemMenu = -1;
  vwReload();
  vwView = VW_LIST;
  vwLastTouch = millis();
}

// Abre la version PRIVADA de una app: los mismos gestos que la app normal,
// pero sobre los datos cifrados de la boveda y solo los suyos.
static void vwOpenAppList(int appId){
  vwKind = vwAppKind(appId);
  vwListApp = appId;
  vwScroll = 0;
  vwItemMenu = -1;
  vwReload();
  vwView = VW_LIST;
  flexVaultAppTouch(appId);
  vwLastTouch = millis();
}

static void vaultRender(){
  // Cada pantalla que use viewport lo pone y lo quita, pero se empieza
  // siempre con el recorte completo: ninguna puede heredar el de otra.
  uiClipFull();
  switch(vwView){
    case VW_SETUP_SEL: vwRenderSetupSel(); break;
    case VW_KEYPAD:    vwRenderKeypad(0);  break;
    case VW_LIST:      vwRenderList();     break;
    case VW_ITEM:      vwRenderItem();     break;
    case VW_APPS:      vwRenderApps();     break;
    case VW_APPMAN:    vwRenderAppMan();   break;
    case VW_REMOVE:    vwRenderRemove();   break;
    case VW_LOG:       vwRenderLog();      break;
    case VW_NOTE:      vwRenderNote();     break;
    case VW_APPDET:    vwRenderAppDet();   break;
    default:           vwRenderHome();     break;
  }
}

// Salir de la boveda del todo: vuelve a Ajustes -> Seguridad. Cerrar la
// boveda al salir NO es opcional (es uno de los cierres que pide la
// funcion), asi que se hace aqui y no en la pantalla que llama.
static void vaultExit(){
  vaultLockNow(FXV_LOCK_EXIT);
  flexVaultWipe(vwPendPath, sizeof(vwPendPath));
  vwPendApp = -1;
  vwListApp = -1;
  vwForget();
  // Si se entro desde "Mover a Carpeta segura", se vuelve a la app de la que
  // venia el usuario -- no a Ajustes, que es donde no estaba.
  int ret = vwRetState;
  int retApp = vwRetApp;
  vwRetState = -1; vwRetApp = -1;
  setBuf(fb);
  if(ret == ST_FILES){ filesEnter(); return; }
  gState = ST_APP;
  gAppId = (ret == ST_APP && retApp >= 0) ? retApp : 12;   // 12 = Ajustes
  fillRect(0, 0, SCR_W, SCR_H, gAppId == 12 ? TH_PAGE : WIN_BG);
  if(gAppId == 12){ settingsRender(); return; }
  appDrawChrome(gAppId);
  appDrawHeader(gAppId);
  if(APP_REG[gAppId].enter) APP_REG[gAppId].enter();
  flxFlushAll();
}

// El unico camino de cierre. Idempotente: se puede llamar desde todos
// los sitios sin comprobar nada antes.
static void vaultLockNow(int reason){
  // Si habia una nota privada a medias, se guarda CIFRADA antes de tirar
  // la clave: cerrar por inactividad no puede costarle al usuario lo que
  // acababa de escribir.
  if(vwView == VW_NOTE && flexVaultUnlocked()) vwNoteSave();
  if(flexVaultUnlocked()) flexVaultLock(reason);
  vwKeyClear();
  vwContentClear();
  flexVaultWipe(vwItems, sizeof(vwItems));
  vwItemsN = 0;
  vwLogN = 0;
  vwItemMenu = -1;
}

// Cierre desde FUERA de la boveda (pantalla apagada, auto-bloqueo,
// apagado, cualquier peticion de clave del sistema, Recientes). Si la
// boveda estaba en pantalla, ademas hay que sacar al usuario de ella:
// dejar ST_VAULT en pie con la boveda cerrada ensenaria una lista vacia
// en vez de pedir la clave.
static void vaultLockFromSystem(int reason){
  bool wasOpen = flexVaultUnlocked();
  bool onScreen = (gState == ST_VAULT);
  if(!wasOpen && !onScreen) return;
  vaultLockNow(reason);
  if(onScreen){
    // Se deja preparada la pantalla de clave. Quien haya provocado el
    // cierre (bloqueo, apagado...) decide que se ve ahora; al volver
    // aqui habra que autenticarse otra vez.
    vwView = flexVaultExists() ? VW_KEYPAD : VW_SETUP_SEL;
    vwKeyFor = VK_OPEN;
    vwKeyMode = flexVaultLockType() == FLEXVAULT_LOCK_PASS ? FLEXVAULT_LOCK_PASS : FLEXVAULT_LOCK_PIN;
  }
}

// -------------------------------------------------------------
//  "MOVER A CARPETA SEGURA" DESDE UNA APP
//  ------------------------------------------------------------
//  Lo llaman Galeria, Notas, Archivos y Paint desde su menu de
//  pulsacion larga. Tres caminos posibles, y ninguno mueve nada sin la
//  clave delante:
//    · Boveda ABIERTA -> se cifra y se mueve ya. La app recarga su
//      lista y el elemento ha desaparecido de ella.
//    · Boveda CERRADA -> se recuerda el elemento, se pide la clave, y
//      el movimiento ocurre justo despues de abrirla.
//    · Boveda SIN CREAR -> se lleva al usuario a crearla, y al terminar
//      se mueve el elemento.
//  En los dos ultimos casos se guarda de donde venia el usuario, para
//  devolverlo a SU app al salir de la boveda en vez de dejarlo en
//  Ajustes.
// -------------------------------------------------------------
const char* vaultMoveError(){ return vwMoveErr; }

// Ejecuta el movimiento pendiente (si habia uno). Se llama al abrir o
// crear la boveda.
static void vwRunPending(){
  if(!vwPendPath[0]) return;
  bool ok = flexVaultImport(vwPendPath, vwPendKind, -1);
  int kind = vwPendKind;
  flexVaultWipe(vwPendPath, sizeof(vwPendPath));
  if(ok){
    vwOpenList(kind);
    vwToast("Movido a Carpeta segura");
  } else {
    vwToast(flexVaultError());
  }
}

static bool vaultMoveRequest(const char* path, int kind){
  vwMoveErr = "";
  if(!path || !path[0]){ vwMoveErr = "Elemento no valido"; return false; }
  if(!flexFsReady()){ vwMoveErr = "Sin almacenamiento"; return false; }
  if(flexFsIsDir(path)){
    vwMoveErr = "Las carpetas todav\xC3\xAD" "a no se pueden mover a la b\xC3\xB3veda";
    return false;
  }
  if(KIOSK_ON && kioskOn){ vwMoveErr = "No disponible en modo kiosco"; return false; }
  if(gHosted || gLand){ vwMoveErr = "Sal de Modo PC para usar la Carpeta segura"; return false; }

  if(flexVaultUnlocked()){
    if(!flexVaultImport(path, kind, -1)){ vwMoveErr = flexVaultError(); return false; }
    return true;
  }
  // Hay que autenticarse (o crear la boveda) antes de mover nada.
  snprintf(vwPendPath, sizeof(vwPendPath), "%s", path);
  vwPendKind = kind;
  vwRetState = gState;
  vwRetApp   = gAppId;
  gState = ST_VAULT;
  gLand = false;
  gClipX0 = 0; gClipX1 = SCR_W - 1; gClipY0 = 0; gClipY1 = SCR_H - 1;
  fkMenuOn = false; fkNameOn = false; fkAskOn = false; fkTrashOn = false;
  vwScroll = 0; vwItemMenu = -1; vwMsg[0] = 0;
  vwLastTouch = millis();
  if(!flexVaultExists()) vwView = VW_SETUP_SEL;
  else                   vwGoKeypad(VK_OPEN, flexVaultLockType());
  vaultRender();
  return true;                       // el movimiento se completara tras la clave
}

// Punto de entrada desde Ajustes -> Seguridad y privacidad -> Flex Vault.
static void vaultSettingsEnter(){
  if(KIOSK_ON && kioskOn) return;          // en kiosco no se abre la boveda
  // MODO PC: no se abre dentro de una ventana. dexHostRun restaura gState al
  // acabar el tick de la app hospedada, asi que la boveda quedaria a medias, y
  // ademas se compondria una pantalla completa dentro del lienzo de la ventana.
  // La fila de Ajustes lo dice (ver vaultStatusText), no falla en silencio.
  if(gHosted || gLand) return;
  if(!flexFsReady()){ fkNoFsScreen("Flex Vault"); gState = ST_VAULT; vwView = VW_HOME; return; }
  gState = ST_VAULT;
  gLand = false;
  gClipX0 = 0; gClipX1 = SCR_W - 1; gClipY0 = 0; gClipY1 = SCR_H - 1;
  vwScroll = 0; vwDragging = false; vwItemMenu = -1; vwMsg[0] = 0;
  vwLastTouch = millis();
  if(!flexVaultExists())        { vwView = VW_SETUP_SEL; }
  else if(!flexVaultUnlocked()) { vwGoKeypad(VK_OPEN, flexVaultLockType()); }
  else                          { vwView = VW_HOME; vwLogN = flexVaultLogRead(vwLog, FLEXVAULT_LOG_MAX); }
  vaultRender();
}

// -------------------------------------------------------------
//  Acciones
// -------------------------------------------------------------
static void vwAfterUnlock(){
  vwLogN = flexVaultLogRead(vwLog, FLEXVAULT_LOG_MAX);
  vwGoHome();
  vwRunPending();            // "Mover a Carpeta segura" que esperaba la clave
  if(vwPendApp >= 0){        // app privada bloqueada que se queria abrir
    int id = vwPendApp; vwPendApp = -1;
    vwOpenAppList(id);
  }
  vaultRender();
}

static void vwKeyConfirm(){
  const char* sec = (vwKeyMode == FLEXVAULT_LOCK_PIN) ? vwPin : vwPass;
  int len = (int)strlen(sec);

  if(vwKeyFor == VK_OPEN){
    int r = flexVaultUnlock(sec);
    vwKeyClear();
    if(r == FXV_OK){ vwAfterUnlock(); return; }
    if(r == FXV_ERR_WAIT){ vwToast("Espera antes de volver a intentarlo"); }
    else if(r == FXV_ERR_WRONG){ vwWrongMs = millis(); }
    else vwToast(flexVaultError());
    vaultRender();
    return;
  }
  if(vwKeyFor == VK_CREATE){
    if(len < 4){ vwToast("La clave necesita al menos 4 caracteres"); vaultRender(); return; }
    int r = flexVaultCreate(sec, vwKeyMode);
    vwKeyClear();
    if(r == FXV_OK){
      vwToast("Flex Vault creada y protegida");
      vwAfterUnlock();
      return;
    }
    vwToast(r == FXV_ERR_ARG ? "Clave no valida" : flexVaultError());
    vaultRender();
    return;
  }
  if(vwKeyFor == VK_CHG_OLD){
    // No se comprueba aqui: se guarda y se pide la nueva. El cambio
    // real (que si valida la actual) lo hace flexVaultChangeSecret en
    // una sola operacion, para no tener que quedarse con la clave
    // verificada mas tiempo del necesario.
    snprintf(vwOldKey, sizeof(vwOldKey), "%s", sec);
    vwKeyClear();
    vwGoKeypad(VK_CHG_NEW, vwKeyMode);
    vaultRender();
    return;
  }
  // VK_CHG_NEW
  if(len < 4){ vwToast("La clave necesita al menos 4 caracteres"); vaultRender(); return; }
  int r = flexVaultChangeSecret(vwOldKey, sec, vwKeyMode);
  flexVaultWipe(vwOldKey, sizeof(vwOldKey));
  vwKeyClear();
  if(r == FXV_OK){
    vwToast("Clave cambiada. Los archivos siguen todos dentro");
    vwGoHome();
  } else if(r == FXV_ERR_WRONG){
    vwToast("La clave actual no era correcta");
    vwGoHome();
  } else if(r == FXV_ERR_WAIT){
    vwToast("Espera antes de volver a intentarlo");
    vwGoHome();
  } else {
    vwToast(flexVaultError());
    vwGoHome();
  }
  vaultRender();
}

static void vwItemAction(int act){
  if(vwItemMenu < 0 || vwItemMenu >= vwItemsN) return;
  uint16_t id = vwItems[vwItemMenu].id;
  if(act == 0){                                   // Abrir
    vwItemMenu = -1;
    vwOpenId = id;
    FlexVaultItem it;
    bool isNote = flexVaultGet(id, &it) &&
                  (it.kind == FXV_KIND_NOTE || vwNameEndsWith(it.name, FLEXFS_EXT_NOTE));
    if(vwLoadOpen()){
      // Una nota privada se ABRE PARA ESCRIBIR (como en Notas); el resto
      // se abre en el visor.
      vwView = isNote ? VW_NOTE : VW_ITEM;
      vwNoteDirtyMs = 0;
      if(isNote){ kbExtrasOn = false; kbApplySize(); kbMtSurfaceReset(); kbShift = false; }
    }
    vaultRender();
    return;
  }
  if(act == 1){                                   // Renombrar
    char stem[FLEXVAULT_NAME_MAX];
    snprintf(stem, sizeof(stem), "%s", vwItems[vwItemMenu].name);
    fkNameOpen("Renombrar en la b\xC3\xB3veda", stem);
    return;                                       // el renombrado ocurre al confirmar
  }
  if(act == 2){                                   // Sacar de la boveda
    char out[FLEXFS_PATH_MAX];
    bool ok = flexVaultExport(id, out, sizeof(out));
    vwItemMenu = -1;
    vwReload();
    vwToast(ok ? "Devuelto a la app normal" : flexVaultError());
    vaultRender();
    return;
  }
  // Eliminar definitivamente
  fkAskOpen("\xC2\xBF" "Eliminar de la b\xC3\xB3veda?", vwItems[vwItemMenu].name);
}

static void vwNewPrivateNote(){
  char name[FLEXVAULT_NAME_MAX];
  // Numero libre DE VERDAD dentro de la boveda: se mira la lista real,
  // no un contador en RAM que se reinicia al apagar.
  for(int k = 1; k < 1000; k++){
    snprintf(name, sizeof(name), "Nota privada %d%s", k, FLEXFS_EXT_NOTE);
    bool taken = false;
    for(int i = 0; i < vwItemsN; i++) if(!strcmp(vwItems[i].name, name)){ taken = true; break; }
    if(!taken) break;
  }
  uint16_t id = 0;
  if(!flexVaultCreateItem(name, FXV_KIND_NOTE, vwListApp, &id)){
    vwToast(flexVaultError());
    vaultRender();
    return;
  }
  vwReload();
  vwOpenId = id;
  vwContentClear();
  vwNoteDirtyMs = 0;
  kbExtrasOn = false; kbApplySize(); kbMtSurfaceReset(); kbShift = false;
  vwView = VW_NOTE;
  vaultRender();
}

// -------------------------------------------------------------
//  Toque y tiempo (vaultTick)
// -------------------------------------------------------------
static bool vwScrollTick(){
  // Arrastre vertical compartido por todas las listas de la boveda.
  int maxS = vwDragS0 - (SCR_H - 40);
  if(maxS < 0) maxS = 0;
  if(T.pressed){ vwDragY0 = T.y; vwDragging = false; return false; }
  if(T.down){
    if(!vwDragging && abs(T.y - vwDragY0) > 10) vwDragging = true;
    if(vwDragging){
      int ns = vwScroll - (T.y - vwDragY0);
      if(ns < 0) ns = 0;
      if(ns > maxS) ns = maxS;
      vwDragY0 = T.y;
      if(ns != vwScroll){ vwScroll = ns; vaultRender(); }
      return true;
    }
  }
  if(T.released && vwDragging){ vwDragging = false; return true; }
  return false;
}

static void vaultTick(){
  // ---- Inactividad: la boveda se cierra sola ----
  if(T.down || T.pressed || T.released) vwLastTouch = millis();
  uint32_t al = flexVaultAutoLockMs();
  if(al && flexVaultUnlocked() && vwLastTouch && millis() - vwLastTouch > al){
    vaultLockNow(FXV_LOCK_IDLE);
    vwGoKeypad(VK_OPEN, flexVaultLockType());
    vwToast("Flex Vault se cerr\xC3\xB3 por inactividad");
    vaultRender();
    return;
  }
  // Un aviso que caduca tiene que borrarse de la pantalla.
  if(vwMsg[0] && millis() - vwMsgMs > 3200){ vwMsg[0] = 0; vaultRender(); }

  // ---- Dialogos del kit de ficheros (renombrar / confirmar borrado) ----
  if(fkAskOn){
    int r = fkAskTick();
    if(r == 1 && vwItemMenu >= 0 && vwItemMenu < vwItemsN){
      uint16_t id = vwItems[vwItemMenu].id;
      bool ok = flexVaultDelete(id);
      vwItemMenu = -1;
      vwReload();
      vwToast(ok ? "Eliminado de la b\xC3\xB3veda" : flexVaultError());
    } else if(r != 0) vwItemMenu = -1;
    if(r != 0) vaultRender();
    return;
  }
  if(fkNameOn){
    int r = fkNameTick();
    if(r == 1 && vwItemMenu >= 0 && vwItemMenu < vwItemsN){
      flexVaultRename(vwItems[vwItemMenu].id, fkNameBuf);
      vwItemMenu = -1;
      vwReload();
    } else if(r != 0) vwItemMenu = -1;
    if(r != 0) vaultRender();
    return;
  }

  // ---- Teclado de clave ----
  if(vwView == VW_KEYPAD){
    if(vwKbAnim){                                  // deslizamiento de entrada (0,3 s)
      float p = (millis() - vwKbAnim) / 300.0f;
      if(p >= 1){ p = 1; vwKbAnim = 0; }
      vwRenderKeypad((int)((1.0f - p) * (SCR_H - KB_Y)));
      return;
    }
    // La espera por intentos fallidos se refresca cada segundo sola.
    if(flexVaultWaitMs() > 0){
      static uint32_t lastSec = 0;
      if(millis() - lastSec > 500){ lastSec = millis(); vaultRender(); }
      if(vwBackHit()){ vaultExit(); return; }
      return;
    }
    if(vwWrongMs && millis() - vwWrongMs > 1200){ vwWrongMs = 0; vaultRender(); }
    if(vwBackHit()){
      // Volver desde la clave: si la boveda esta cerrada no hay adonde
      // ir dentro, asi que se sale a Ajustes.
      if(flexVaultUnlocked()){ vwGoHome(); vaultRender(); }
      else vaultExit();
      return;
    }
    if(vwKeyMode == FLEXVAULT_LOCK_PIN){
      if(T.tap){
        for(int i = 0; i < 12; i++){
          int x, y, w, h; lsuPinRect(i, x, y, w, h);
          if(T.x < x || T.x > x + w || T.y < y || T.y > y + h) continue;
          if(i == 9){ int L = strlen(vwPin); if(L > 0) vwPin[L - 1] = 0; }
          else if(i == 11) { vwKeyConfirm(); return; }
          else if(strlen(vwPin) < 8){
            int L = strlen(vwPin);
            vwPin[L] = PIN_KEYS[i][0]; vwPin[L + 1] = 0;
            // Autoconfirmar al completar los digitos del PIN guardado,
            // igual que el bloqueo del sistema.
            if(vwKeyFor == VK_OPEN){
              int want = flexVaultSecretLen();
              if(want > 0 && (int)strlen(vwPin) == want){ vwKeyConfirm(); return; }
            }
          }
          vaultRender();
          return;
        }
      }
      return;
    }
    // Contrasena: teclado alfanumerico del sistema.
    kbFxTick(SET_CARD_BG, TH_TXT);
    if(T.pressed) kbFxPress(kbCellAt(T.x, T.y), SET_CARD_BG, TH_TXT);
    if(T.tap){
      int fi = kbFRowHit(T.x, T.y);
      if(fi >= 0){
        if(fi == 0) kbShift = !kbShift;
        else if(fi == 1) mapaActivo = (mapaActivo == LAYOUT_NUM) ? LAYOUT_EMOJI :
                                     (mapaActivo == LAYOUT_EMOJI) ? (kbLangEs ? LAYOUT_ES : LAYOUT_EN) : LAYOUT_NUM;
        else if(fi == 2){ kbLangEs = !kbLangEs; if(mapaActivo == LAYOUT_ES || mapaActivo == LAYOUT_EN) mapaActivo = kbLangEs ? LAYOUT_ES : LAYOUT_EN; }
        else if(fi == 3){ size_t L = strlen(vwPass); if(L + 1 < sizeof(vwPass)){ vwPass[L] = ' '; vwPass[L + 1] = 0; } }
        else if(fi == 4){ int L = strlen(vwPass); if(L > 0){ int q = L - 1; while(q > 0 && (vwPass[q] & 0xC0) == 0x80) q--; vwPass[q] = 0; } }
        else { vwKeyConfirm(); return; }
        vaultRender();
        return;
      }
      int cell = kbCellAt(T.x, T.y);
      if(cell >= 0){
        char u[6];
        const char* k = kbResolveKey(mapaActivo[cell / KB_COLS][cell % KB_COLS], u, true);
        size_t L = strlen(vwPass), kl = strlen(k);
        if(L + kl < sizeof(vwPass)){ memcpy(vwPass + L, k, kl); vwPass[L + kl] = 0; }
        kbFxStart(cell);
        vaultRender();
      }
    }
    return;
  }

  // ---- Alta: elegir metodo ----
  if(vwView == VW_SETUP_SEL){
    if(vwBackHit()){ vaultExit(); return; }
    if(T.tap){
      int a = vwHitRow(T.x, T.y);
      if(a == FLEXVAULT_LOCK_PIN || a == FLEXVAULT_LOCK_PASS){
        vwGoKeypad(VK_CREATE, a);
        vaultRender();
      }
    }
    return;
  }

  // A partir de aqui TODO exige la boveda abierta. Si no lo esta (por
  // ejemplo porque se cerro por inactividad justo antes de este toque),
  // se pide la clave en vez de ensenar una pantalla vacia.
  if(!flexVaultUnlocked()){
    vwGoKeypad(VK_OPEN, flexVaultLockType());
    vaultRender();
    return;
  }

  if(vwView == VW_NOTE){ vwNoteTick(); return; }

  if(vwScrollTick()) return;

  // ---- Menu de acciones de un elemento ----
  if(vwView == VW_LIST && vwItemMenu >= 0){
    if(T.tap){
      for(int i = 0; i < 4; i++){
        int x, y, w, h; vwMenuRect(i, x, y, w, h);
        if(T.x >= x && T.x <= x + w && T.y >= y && T.y <= y + h){ vwItemAction(i); return; }
      }
      vwItemMenu = -1;
      vaultRender();
    }
    return;
  }

  if(vwBackHit()){
    switch(vwView){
      case VW_HOME:   vaultExit(); return;
      case VW_ITEM:   vwContentClear(); vwView = VW_LIST; vwReload(); vaultRender(); return;
      case VW_REMOVE: vwView = VW_APPDET; vaultRender(); return;
      case VW_APPDET: vwView = VW_APPMAN; vwAppSel = -1; vaultRender(); return;
      case VW_LIST:   if(vwListApp >= 0){ vwListApp = -1; vwView = VW_APPMAN; vaultRender(); return; }
                      vwGoHome(); vaultRender(); return;
      default:        vwGoHome(); vaultRender(); return;
    }
  }

  if(!T.tap){
    // Pulsacion larga sobre un elemento de la lista: abre su menu.
    if(vwView == VW_LIST && T.down && !vwDragging && !vwLongFired &&
       millis() - T.downMs > 500){
      int a = vwHitRow(T.x, T.y);
      if(a >= VA_ITEM_BASE && a < VA_APP_BASE){
        vwItemMenu = a - VA_ITEM_BASE;
        vwLongFired = true;
        vaultRender();
      }
    }
    if(!T.down) vwLongFired = false;
    return;
  }
  vwLongFired = false;

  int act = vwHitRow(T.x, T.y);
  vwLastTouch = millis();

  if(vwView == VW_HOME){
    switch(act){
      case VA_GAL:      vwOpenList(FXV_KIND_PHOTO); vaultRender(); return;
      case VA_NOTES:    vwOpenList(FXV_KIND_NOTE);  vaultRender(); return;
      case VA_FILES:    vwOpenList(FXV_KIND_FILE);  vaultRender(); return;
      case VA_APPS:     vwView = VW_APPS;   vwScroll = 0; vaultRender(); return;
      case VA_APPMAN:   vwView = VW_APPMAN; vwScroll = 0; vaultRender(); return;
      case VA_LOG:      vwLogN = flexVaultLogRead(vwLog, FLEXVAULT_LOG_MAX);
                        vwView = VW_LOG; vwScroll = 0; vaultRender(); return;
      case VA_AUTOLOCK: vwCycleAutoLock(); vaultRender(); return;
      case VA_CHGKEY:   vwGoKeypad(VK_CHG_OLD, flexVaultLockType()); vaultRender(); return;
      case VA_LOCKNOW:  vaultLockNow(FXV_LOCK_MANUAL);
                        vwGoKeypad(VK_OPEN, flexVaultLockType());
                        vwToast("Flex Vault bloqueada");
                        vaultRender(); return;
      default: return;
    }
  }

  if(vwView == VW_LIST){
    if(act == VA_NEWNOTE){ vwNewPrivateNote(); return; }
    if(act >= VA_ITEM_BASE && act < VA_APP_BASE){
      int i = act - VA_ITEM_BASE;
      if(i >= 0 && i < vwItemsN){
        vwItemMenu = i;
        vwItemAction(0);                       // toque simple = abrir
      }
    }
    return;
  }

  if(vwView == VW_ITEM){
    if(act == VA_ITEM_BASE){
      char out[FLEXFS_PATH_MAX];
      bool ok = flexVaultExport(vwOpenId, out, sizeof(out));
      vwContentClear();
      vwReload();
      vwView = VW_LIST;
      vwToast(ok ? "Devuelto a la app normal" : flexVaultError());
      vaultRender();
    }
    return;
  }

  if(vwView == VW_APPS){
    if(act >= VA_APP_BASE){
      int id = act - VA_APP_BASE;
      if(flexVaultAppAdded(id)){
        vwAppSel = id;
        vwView = VW_REMOVE;                   // quitar SIEMPRE pregunta por los datos
        vwScroll = 0;
      } else if(flexVaultAppAdd(id)){
        vwToast("A\xC3\xB1" "adida. Su versi\xC3\xB3n privada ya est\xC3\xA1 en Flex Vault");
      } else {
        vwToast(flexVaultAppReason(id) ? flexVaultAppReason(id) : "No se pudo a\xC3\xB1" "adir");
      }
      vaultRender();
    }
    return;
  }

  if(vwView == VW_APPMAN){
    if(act >= VA_APP_BASE){
      vwAppSel = act - VA_APP_BASE;
      vwView = VW_APPDET; vwScroll = 0;
      vaultRender();
    }
    return;
  }

  if(vwView == VW_APPDET){
    if(vwAppSel < 0) return;
    if(act == VA_APPOPEN){
      if(flexVaultAppLocked(vwAppSel)){
        // BLOQUEADA de verdad: se cierra la boveda y se pide la clave. Al
        // acertar, vwRunPendingApp abre esta app.
        vwPendApp = vwAppSel;
        vaultLockNow(FXV_LOCK_MANUAL);
        vwGoKeypad(VK_OPEN, flexVaultLockType());
        vwToast("Esta app privada est\xC3\xA1 bloqueada");
      } else {
        vwOpenAppList(vwAppSel);
      }
      vaultRender();
      return;
    }
    if(act == VA_APPLOCK){
      flexVaultAppSetLocked(vwAppSel, !flexVaultAppLocked(vwAppSel));
      vwToast(flexVaultAppLocked(vwAppSel)
              ? "Bloqueada: pedir\xC3\xA1 la clave al abrirla"
              : "Desbloqueada dentro de la b\xC3\xB3veda");
      vaultRender();
      return;
    }
    if(act == VA_APPDEL){
      vwView = VW_REMOVE; vwScroll = 0;
      vaultRender();
      return;
    }
    return;
  }

  if(vwView == VW_REMOVE){
    if(act >= VA_APP_BASE && vwAppSel >= 0){
      int what = act - VA_APP_BASE;             // 0 mover, 1 mantener, 2 eliminar
      bool ok = flexVaultAppRemove(vwAppSel, what);
      const char* m = !ok ? flexVaultError() :
                      what == FXV_APPDATA_EXPORT ? "Datos devueltos a la app normal" :
                      what == FXV_APPDATA_KEEP   ? "Datos mantenidos cifrados en la b\xC3\xB3veda" :
                                                   "Datos eliminados definitivamente";
      vwAppSel = -1;
      vwView = VW_APPMAN;
      vwToast(m);
      vaultRender();
    }
    return;
  }
}

// Texto de la fila de Ajustes -> Seguridad y privacidad -> Flex Vault. Dice el
// estado REAL: si no existe, si esta cerrada, o si esta abierta y cuanto ocupa.
static void vaultStatusText(char* out, size_t n){
  if(gHosted || gLand){ snprintf(out, n, "Abre Flex Vault fuera de Modo PC"); return; }
  if(!flexFsReady()){ snprintf(out, n, "Sin almacenamiento"); return; }
  if(!flexVaultExists()){ snprintf(out, n, "Sin configurar - toca para crearla"); return; }
  if(!flexVaultUnlocked()){
    uint32_t w = flexVaultWaitMs();
    if(w) snprintf(out, n, "Bloqueada - espera %u s", (unsigned)((w + 999) / 1000));
    else  snprintf(out, n, "Protegida - pide %s", flexVaultLockType() == FLEXVAULT_LOCK_PIN ? "PIN" : "contrase\xC3\xB1" "a");
    return;
  }
  char sz[16];
  flexFsFmtSize(flexVaultUsedBytes(), sz, sizeof(sz));
  snprintf(out, n, "Abierta ahora - %s en uso", sz);
}
