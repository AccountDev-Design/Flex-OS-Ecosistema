// #############################################################
// ##  FLEX OS ULTRA  ·  APP ALMACENAMIENTO  ·  y detalles de memoria
// ##  ----------------------------------------------------------
// ##  Los dos volumenes (interno y microSD) por separado, el aviso de poco
// ##  espacio y la pantalla de detalles de memoria y sistema, con el boton
// ##  Optimizar Flex OS. Aqui vive tambien simpBar(), la fila de medidor
// ##  que reutilizan las apps simples.
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
#include "FlexOS_Ultra_KeyboardSettings.h"   // eslabon anterior de la cadena

// #############################################################
// ##  APPS SIMPLES: Almacenamiento, Educacion, Navegador,
// ##  Code IDE, Paint (funcional), Juegos
// #############################################################
// Barra etiqueta+valor+progreso, dimensionada al lienzo (la usan Almacenamiento
// y cualquier app que quiera una fila de medidor).
static int simpBar(int y, const char* label, const char* val, int pct, uint16_t col){
  int bxx, byy, bww, bhh; uiBox(bxx, byy, bww, bhh);
  int pad = uiPad();
  int bx = bxx + pad * 2, bw = bww - pad * 4;
  if(bw < 60){ bx = bxx + pad; bw = bww - 2 * pad; }
  int fs = uiFontFit(label, bw / 2, 2);
  drawText(bx, y, label, fs, TH_TXT);
  drawTextR(bx + bw, y, val, uiFontFit(val, bw / 2, 2), TH_TXT2);
  int barH = bhh / 22; if(barH < 9) barH = 9; if(barH > 20) barH = 20;
  int byr = y + uiLineH(fs) + 6;
  fillRoundRect(bx, byr, bw, barH, barH / 2, TH_TRACK);     // track de la barra (apagado)
  if(pct > 0) fillRoundRect(bx, byr, bw * pct / 100, barH, barH / 2, col);
  return byr + barH + uiPad();
}
// #############################################################
// ##  ALMACENAMIENTO  ·  todo lo que muestra sale de leer el
// ##  sistema de archivos y el SDK, en el momento de pintarlo
// ##  ------------------------------------------------------
// ##  De donde sale cada numero:
// ##    · "Almacenamiento interno" -> LittleFS.totalBytes() y
// ##      LittleFS.usedBytes() (via flexFsTotalBytes/UsedBytes).
// ##      Es el espacio de la PARTICION DE DATOS, que es el
// ##      unico que el usuario puede llenar; no el tamano del
// ##      chip de flash, que incluye el propio firmware y no se
// ##      puede usar para guardar nada.
// ##    · "PSRAM" -> heap_caps_get_total_size/free_size con
// ##      MALLOC_CAP_SPIRAM. En una placa sin PSRAM (Pro) el
// ##      total es 0 y la fila lo dice, no finge un porcentaje.
// ##    · Categorias -> suma recursiva de los ficheros de cada
// ##      carpeta real (flexFsCatSize).
// ##    · "Archivos grandes" -> recorrido completo del arbol
// ##      quedandose con los mayores (flexFsLargest).
// ##    · "Tarjeta SD" -> flexSd*(): capacidad, tipo de tarjeta y
// ##      sistema de archivos leidos del volumen MONTADO. Sin
// ##      tarjeta no se pinta una barra al 0% que parezca una
// ##      tarjeta vacia: se dice que no hay y por que.
// ##
// ##  LOS DOS VOLUMENES SE PRESENTAN POR SEPARADO. "Memoria
// ##  interna" es la particion de datos (LittleFS) y "Tarjeta SD"
// ##  es el volumen extraible; ni se suman ni se mezclan sus
// ##  numeros, porque son dos sitios distintos con dos
// ##  comportamientos distintos (uno siempre esta, el otro puede
// ##  irse a mitad de una lectura).
// #############################################################
#define ALM_BIG_MAX 3

static FlexFsBig almBig[ALM_BIG_MAX];
static int       almBigN = 0;
static uint32_t  almCat[FLEXFS_CAT_N];
static int       almVerY0 = 0, almVerY1 = 0;      // zona pulsable real de "Ver..."

static const char* ALM_CAT_NAME[FLEXFS_CAT_N] = { "Documentos", "Sistema", "Aplicaciones", "Papelera" };
static const uint16_t ALM_CAT_COL[FLEXFS_CAT_N] = {
  rgb565(245,85,85), rgb565(250,215,95), rgb565(95,225,110), rgb565(60,205,240) };

static void filesEnter();                          // explorador (ST_FILES), mas abajo
static void filesEnterAt(const char* dir);         // ...abierto en una carpeta concreta

// Relee TODO lo que se muestra. Se llama al entrar a la app y al volver
// del explorador: si el usuario borro algo alli, aqui se ve al instante.
static void almScan(){
  for(int i = 0; i < FLEXFS_CAT_N; i++) almCat[i] = flexFsCatSize(i);
  almBigN = flexFsLargest(almBig, ALM_BIG_MAX);
}

// Icono de carpeta (el mismo que usa el explorador).
static void almFolderIcon(int x, int y, int s){
  uint16_t body = rgb565(250,205,90), tab = rgb565(240,175,60);
  fillRoundRect(x, y + s / 5, s, s * 3 / 4, s / 8, tab);
  fillRoundRect(x, y + s / 5, s * 5 / 9, s / 4, s / 10, tab);
  fillRoundRect(x + s / 10, y + s / 3, s - s / 10, s * 3 / 5, s / 9, body);
}

// Zona pulsable de la fila "Ver..." de la tarjeta (0 = no hay).
static int almSdY0 = 0, almSdY1 = 0;

// Icono de tarjeta SD: el contorno con la esquina cortada, que es
// como se reconoce de un vistazo. Cambia de color con el estado
// real, no siempre igual.
static void almSdIcon(int x, int y, int s, uint16_t col){
  int cut = s / 3;
  fillRoundRect(x, y, s, s * 4 / 3, s / 8, col);
  fillTriangle(x + s - cut, y, x + s, y, x + s, y + cut, TH_PAGE);
  for(int i = 0; i < 4; i++)
    fillRect(x + s / 6 + i * (s / 6), y + s / 8, s / 12, s / 3, TH_PAGE);
}

// Dibuja el bloque de la tarjeta y devuelve la Y siguiente.
static int almSdBlock(int bx, int by, int bw, int bh, int y){
  const int pad = uiPad(), gap = uiGap();
  const bool ok = flexSdReady();
  const int cardH = ok ? 96 : 74;
  almSdY0 = almSdY1 = 0;
  if(y + cardH > by + bh - pad) return y;

  if(uiGlass) drawLiquidGlassPanel(bx + pad, y, bw - 2 * pad, cardH, 14, TH_GLASS);
  else        fillRoundRect(bx + pad, y, bw - 2 * pad, cardH, 14, TH_SURF);

  almSdIcon(bx + pad * 2, y + 14, 26, ok ? rgb565(90,190,130) : rgb565(120,124,140));
  drawText(bx + pad * 2 + 44, y + 12, "Tarjeta SD", 2, TH_TXT);

  if(!ok){
    // Sin tarjeta (o con un fallo): se dice el motivo REAL que
    // devuelve el modulo, no un texto generico.
    drawTextClip(bx + pad * 2 + 44, y + 36, flexSdError(), 1,
                 flexSdState() == FLEXSD_ABSENT ? TH_MUTE : TH_ERR, bx + bw - pad * 2);
    drawTextClip(bx + pad * 2 + 44, y + 54, "Formato recomendado: FAT32", 1, TH_MUTE,
                 bx + bw - pad * 2);
    return y + cardH + gap / 2;
  }

  uint64_t tot = flexSdTotalBytes(), usd = flexSdUsedBytes();
  int pct = tot ? (int)((usd * 100ull) / tot) : 0;
  char su[24], st[24], sf[24], line[72];
  flexFsFmtSize((uint32_t)(usd / 1048576ull), su, sizeof(su));
  flexFsFmtSize((uint32_t)(tot / 1048576ull), st, sizeof(st));
  flexFsFmtSize((uint32_t)(flexSdFreeBytes() / 1048576ull), sf, sizeof(sf));
  // Las cifras van en MB reales; flexFsFmtSize da la unidad, asi que
  // se compone "1234 MB" -> se ensena tal cual con su sufijo.
  snprintf(line, sizeof(line), "%s de %s usados  ·  %s libres", su, st, sf);
  drawTextClip(bx + pad * 2 + 44, y + 34, line, 1, TH_TXT2, bx + bw - pad * 2);
  snprintf(line, sizeof(line), "%s  ·  %s", flexSdCardTypeName(), flexSdFsName());
  drawTextR(bx + bw - pad * 2, y + 12, line, 1, TH_TXT2);

  int barY = y + 54, barX = bx + pad * 2, barW = bw - pad * 4;
  fillRoundRect(barX, barY, barW, 10, 5, TH_TRACK);
  if(pct > 0) fillRoundRect(barX, barY, barW * pct / 100, 10, 5, rgb565(90,190,130));
  drawTextR(bx + bw - pad * 2, y + 70, "Ver archivos...", 2, TH_ACCS);
  almSdY0 = y; almSdY1 = y + cardH;
  return y + cardH + gap / 2;
}

// Pantalla interna activa de la app. La segunda (DETALLE) es la que anade la
// Fase 5; la primera es exactamente la de siempre.
enum { ALM_SCR_MAIN = 0, ALM_SCR_DETAIL };
static int almScreen = ALM_SCR_MAIN;
static int almDetY0 = 0, almDetY1 = 0;      // zona pulsable de la fila "Detalles..."
static int almOptX0 = 0, almOptX1 = 0;      // ...y del boton "Optimizar" dentro de ella
static void almDetailEnter();
static void almRenderMain();
// Repintado de la pantalla que este activa. Lo usan el optimizador (al
// cerrarse), el explorador (al volver) y el propio ciclo de vida.
static void almDetailRepaint();
static void almRender(){
  if(almScreen == ALM_SCR_DETAIL) almDetailRepaint();   // conserva el desplazamiento
  else                            almRenderMain();
}

static void almRenderMain(){
  setBuf(fb);
  int bx, by, bw, bh; uiBox(bx, by, bw, bh);
  fillRect(bx, by, bw, bh, WIN_BG);
  int pad = uiPad(), gap = uiGap();
  int y = by + pad;
  y = uiTitle(bx, y, bw, "Almacenamiento", TH_TXT, uiFontH(bh / 12));
  y += gap / 2;

  if(!flexFsReady()){
    drawTextC(bx + bw / 2, y + 40, "Sin almacenamiento", 3, TH_ERR);
    drawTextC(bx + bw / 2, y + 84, flexFsError(), 1, TH_TXT2);
    drawTextC(bx + bw / 2, y + 112, "Elige un Partition Scheme con SPIFFS", 1, TH_MUTE);
    almVerY0 = almVerY1 = 0;
    flxFlush(WIN_TOP, WIN_BOT);
    return;
  }
  almScan();

  // ---- Barra 1: particion de datos (lo unico que el usuario llena) ----
  uint32_t tot = flexFsTotalBytes(), usd = flexFsUsedBytes();
  int pctFs = tot ? (int)((uint64_t)usd * 100 / tot) : 0;
  char vt[48], su[24], st[24];
  flexFsFmtSize(usd, su, sizeof(su));
  flexFsFmtSize(tot, st, sizeof(st));
  snprintf(vt, sizeof(vt), "%s / %s  (%d%%)", su, st, pctFs);
  y = simpBar(y, "Memoria interna", vt, pctFs, rgb565(90,160,240));

  // ---- Barra 2: PSRAM ----
  size_t pt = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
  size_t pf = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  if(pt > 0){
    int pctPs = (int)(100 - (uint64_t)pf * 100 / pt);
    char vp[48];
    snprintf(vp, sizeof(vp), "%u / %u MB  (%d%%)",
             (unsigned)((pt - pf) / 1048576u), (unsigned)(pt / 1048576u), pctPs);
    y = simpBar(y, "PSRAM", vp, pctPs, rgb565(90,180,120));
  } else {
    // Sin PSRAM (ESP32 clasico): se dice, no se pinta una barra a 0 que
    // parezca una PSRAM vacia.
    y = simpBar(y, "PSRAM", "No disponible en esta placa", 0, rgb565(120,124,140));
  }

  // ---- Panel expandible: "Detalles de memoria y sistema" ----
  // Va justo DEBAJO de la barra de PSRAM, como se pidio. Colapsado ya dice lo
  // esencial (el estado, con su color) para que no haya que abrirlo solo para
  // saber si todo va bien; al tocarlo se abre la pantalla de detalle DENTRO de
  // la app, con su propio desplazamiento y el boton atras del sistema.
  //
  // POR QUE UNA PANTALLA Y NO UN DESPLIEGUE EN LINEA: el detalle son mas de
  // treinta cifras reales (PSRAM, SRAM, flash, tarjeta y estado del sistema).
  // Desplegarlas aqui empujaria la tarjeta SD, las categorias y los archivos
  // grandes fuera de una pantalla de 480x800 que hoy NO tiene desplazamiento:
  // el resultado seria justo la "pantalla confusa" que habia que evitar.
  almDetY0 = almDetY1 = 0;
  almOptX0 = almOptX1 = 0;
  if(y + 52 <= by + bh - pad){
    uiSurface(bx + pad, y, bw - 2 * pad, 50, 14, UIS_ELEVATED);
    drawText(bx + pad * 2, y + 4, "Detalles de memoria y sistema", 2, TH_TXT);
    const FlexMemSnap* m = memSnap();
    int hh = flexMemHealth(m);
    const char* hn = (hh == FLEXMEM_H_CRIT) ? "Cr\xC3\xADtico" : (hh == FLEXMEM_H_WATCH ? "Atenci\xC3\xB3n" : "\xC3\x93ptimo");
    uint16_t hc = (hh == FLEXMEM_H_CRIT) ? TH_ERR : (hh == FLEXMEM_H_WATCH ? TH_WARN : TH_OK);
    fillCircle(bx + pad * 2 + 6, y + 32, 5, hc);
    drawText(bx + pad * 2 + 18, y + 26, hn, 1, TH_TXT2);
    // "Optimizar" vive AQUI, en la pantalla principal, y no solo dentro del
    // detalle: es la accion que hace falta cuando el sistema va lento, y
    // esconderla detras de otra pantalla la volveria inutil justo entonces.
    // El resto de la fila abre el detalle.
    int ow = 118, ox = bx + bw - pad * 2 - ow + 8, oy = y + 9;
    fillRoundRect(ox, oy, ow, 32, 16, TH_PRIM);
    drawTextC(ox + ow / 2, oy + 8, "Optimizar", 1, TH_ONACC);
    almOptX0 = ox; almOptX1 = ox + ow;
    almDetY0 = y; almDetY1 = y + 50;
    y += 50 + gap / 2;
  }

  // ---- Tarjeta SD: volumen APARTE, nunca sumado al interno ----
  y = almSdBlock(bx, by, bw, bh, y);

  // ---- Categorias: tamano REAL de cada conjunto de carpetas ----
  y += gap / 2;
  int rowH = uiLineH(2) + 12;
  for(int i = 0; i < FLEXFS_CAT_N; i++){
    if(y + rowH > by + bh - pad) break;
    fillCircle(bx + pad * 2 + 8, y + rowH / 2 - 2, 8, ALM_CAT_COL[i]);
    drawText(bx + pad * 2 + 26, y, ALM_CAT_NAME[i], 2, TH_TXT);
    char sz[24]; flexFsFmtSize(almCat[i], sz, sizeof(sz));
    drawTextR(bx + bw - pad * 2, y, sz, 2, TH_TXT2);
    y += rowH;
  }

  // ---- Archivos grandes: los mayores del sistema, de verdad ----
  y += gap;
  if(y + 40 < by + bh - pad){
    drawText(bx + pad * 2, y, "Archivos grandes", 2, TH_TXT);
    y += uiLineH(2) + 6;
    if(almBigN == 0){
      drawText(bx + pad * 2, y, "No hay archivos guardados", 1, TH_MUTE);
      y += 22;
    }
    for(int i = 0; i < almBigN; i++){
      if(y + 44 > by + bh - pad) break;
      almFolderIcon(bx + pad * 2, y, 34);
      const char* nm = strrchr(almBig[i].path, '/');
      nm = nm ? nm + 1 : almBig[i].path;
      drawTextClip(bx + pad * 2 + 46, y, nm, 2, TH_TXT, bx + bw - pad * 2);
      char sz[24], ln[64]; flexFsFmtSize(almBig[i].size, sz, sizeof(sz));
      snprintf(ln, sizeof(ln), "%s  ·  %s", almBig[i].path, sz);
      drawTextClip(bx + pad * 2 + 46, y + 22, ln, 1, TH_TXT2, bx + bw - pad);
      y += 46;
    }
  }

  // ---- Fila "Todos los archivos ... Ver..." -> explorador REAL ----
  y += gap / 2;
  if(y + 46 <= by + bh - pad / 2){
    if(uiGlass) drawLiquidGlassPanel(bx + pad, y, bw - 2 * pad, 44, 12, TH_GLASS);
    else fillRoundRect(bx + pad, y, bw - 2 * pad, 44, 12, TH_SURF);
    drawText(bx + pad * 2, y + 12, "Todos los archivos", 2, TH_TXT);
    drawTextR(bx + bw - pad * 2, y + 12, "Ver...", 2, TH_ACCS);
    almVerY0 = y; almVerY1 = y + 44;
  } else {
    almVerY0 = almVerY1 = 0;
  }
  flxFlush(WIN_TOP, WIN_BOT);
}

// #############################################################
// ##  ALMACENAMIENTO · DETALLES DE MEMORIA Y SISTEMA  (Fase 5)
// ##  ------------------------------------------------------
// ##  DE DONDE SALE CADA CIFRA. Todas, sin excepcion, de una medida:
// ##    · PSRAM y SRAM interna -> heap_caps_* (via memSnap(), que es la
// ##      unica medida publicada del sistema);
// ##    · pico de PSRAM y minimo de SRAM -> historicos que lleva el
// ##      propio muestreo, con la MISMA medida que se ensena;
// ##    · flash -> flexFsTotalBytes/UsedBytes y flexFsCatSize/DirSize;
// ##    · firmware -> ESP.getSketchSize(), UNA vez por arranque;
// ##    · tarjeta -> flexSdState()/flexSdError() y, solo si esta
// ##      MONTADA, sus totales. Aqui NO se sondea la tarjeta: se LEE el
// ##      estado que el modulo ya mantiene. Ese es el punto entero de
// ##      esta pantalla respecto a la inestabilidad conocida de la SD.
// ##  Lo que no se puede medir dice "No disponible" y punto.
// ##
// ##  COMO SE REFRESCA SIN REDIBUJAR LA PANTALLA. El contenido esta
// ##  partido en SECCIONES con altura fija. Cada una lleva una FIRMA de
// ##  sus valores; una vez por segundo se recalculan las firmas y solo
// ##  se repinta la banda de las secciones que (a) han cambiado de
// ##  verdad y (b) estan dentro de la ventana visible. Si no cambia
// ##  nada -- lo normal cuando el sistema esta en reposo -- no se
// ##  publica ni una fila. La barra de estado, la cabecera de la app y
// ##  la barra de navegacion no se tocan nunca.
// ##
// ##  CERO RESERVAS POR REFRESCO: los textos se componen con snprintf
// ##  en buffers de pila del propio dibujo.
// #############################################################
#define ALMD_VY0     WIN_TOP
#define ALMD_VY1     (WIN_BOT - 1)
#define ALMD_SEC_N   6
// Alturas de cada seccion, en coordenadas de CONTENIDO. Estan calculadas sobre
// el contenido REAL de cada una (titulo 26 px + 24 px por fila + 18 px por
// nota) con holgura: si una seccion se pasara de su alto, su ultima linea
// caeria dentro de la banda de la siguiente y el repintado parcial de esa
// otra la borraria -- un fallo que solo se ve cuando cambia justo ese dato.
static const int ALMD_SEC_H[ALMD_SEC_N] = { 96, 240, 168, 236, 140, 200 };
static int      almDetScroll = 0;
static uint32_t almDetSig[ALMD_SEC_N];
static uint32_t almDetMs = 0;
static uint32_t almFwSize = 0xFFFFFFFFu;      // 0xFFFFFFFF = aun sin medir
static bool     almDetDrag = false;
// almDetTouching: el contacto empezo DENTRO de la ventana de contenido. Sin
// esta marca, un dedo que baja en la cabecera de la app o en la barra del
// sistema y luego entra en la lista se tomaria como un arrastre que arranca
// en una posicion vieja, y la lista daria un salto.
static bool     almDetTouching = false;
static float    almDetStartY = 0, almDetStart0 = 0;
// Boton "Optimizar Flex OS" de la cabecera (geometria unica: dibujo y hit-test).
#define ALMD_BW  (SCR_W - 72)
#define ALMD_BX  36
#define ALMD_BH  44

static int almDetSecTop(int i){
  int y = 0;
  for(int k = 0; k < i && k < ALMD_SEC_N; k++) y += ALMD_SEC_H[k];
  return y;
}
static int almDetContentH(){ return almDetSecTop(ALMD_SEC_N); }
static int almDetMaxScroll(){
  int m = almDetContentH() - (ALMD_VY1 - ALMD_VY0 + 1);
  return m > 0 ? m : 0;
}

// ---- Utilidades de fila (etiqueta izquierda, valor derecha) ----
static int almDetRow(int y, const char* label, const char* value, uint16_t vcol){
  int pad = uiPad();
  drawTextClip(pad * 2, y, label, 1, TH_TXT2, SCR_W / 2 + 20);
  drawTextR(SCR_W - pad * 2, y - 3, value, 2, vcol);
  return y + 24;
}
static int almDetNote(int y, const char* txt){
  drawTextClip(uiPad() * 2, y, txt, 1, TH_MUTE, SCR_W - uiPad() * 2);
  return y + 18;
}
static int almDetTitle(int y, const char* txt){
  drawText(uiPad() * 2, y, txt, 2, TH_TXT);
  return y + 26;
}

// Motivo REAL del ultimo arranque. Incluye los normales, no solo los fallos:
// decir "Reinicio inesperado" tras un encendido normal seria mentir.
static const char* almBootCause(){
  switch(esp_reset_reason()){
    case ESP_RST_POWERON:   return "Encendido normal";
    case ESP_RST_SW:        return "Reinicio por software";
    case ESP_RST_DEEPSLEEP: return "Despertar de suspensi\xC3\xB3n";
    case ESP_RST_PANIC:     return "Fallo del sistema (crash)";
    case ESP_RST_TASK_WDT:  return "Watchdog de tarea";
    case ESP_RST_INT_WDT:   return "Watchdog de interrupci\xC3\xB3n";
    case ESP_RST_WDT:       return "Watchdog del chip";
    case ESP_RST_BROWNOUT:  return "Ca\xC3\xAD" "da de tensi\xC3\xB3n";
    case ESP_RST_EXT:       return "Reinicio externo";
    default:                return "No disponible";
  }
}
// Estado de la tarjeta en las palabras del usuario. LECTURA PURA: no monta,
// no reintenta y no toca el controlador.
static const char* almSdStateText(){
  switch(flexSdState()){
    case FLEXSD_READY:     return "Lista";
    case FLEXSD_ABSENT:    return "No insertada";
    case FLEXSD_ERR_MOUNT: return "Error al montar";
    case FLEXSD_ERR_FS:    return "Formato no compatible";
    case FLEXSD_ERR_IO:    return "Error de lectura";
    case FLEXSD_ERR_HW:    return "No disponible";
    default:               return "No disponible";
  }
}

// Las dos apps abiertas que mas PSRAM MEDIDA retienen. Sin medida no entran:
// una lista de "mayor consumo" con numeros inventados no informa de nada.
static void almTopApps(int* a, int* b){
  *a = *b = -1;
  for(int i = 0; i < APP_N; i++){
    if(gAppState[i] == ALIFE_CLOSED || !appMemHas(i) || appMemBytes(i) == 0) continue;
    if(*a < 0 || appMemBytes(i) > appMemBytes(*a)){ *b = *a; *a = i; }
    else if(*b < 0 || appMemBytes(i) > appMemBytes(*b)) *b = i;
  }
}

// ---- Firma de una seccion: si no cambia, no se repinta ----
static uint32_t almDetSecSig(int sec){
  const FlexMemSnap* m = memSnap();
  switch(sec){
    case 1: return m->psFree ^ (m->psLargest << 1) ^ (m->psPeakUsed >> 3) ^ m->psTotal;
    case 2: return m->inFree ^ (m->inMin << 1) ^ m->inTotal;
    case 3: return m->fsUsed ^ (m->fsTotal << 1) ^ (uint32_t)m->fsValid;
    case 4: return (uint32_t)flexSdState() ^ (uint32_t)(flexSdUsedBytes() >> 10) ^ flexSdGeneration();
    case 5: {
      uint32_t up = millis() / 60000u;      // el rotulo solo cambia por minutos
      uint32_t apps = 0;
      for(int i = 0; i < APP_N; i++) apps = apps * 3u + gAppState[i];
      return up ^ (apps << 3) ^ (uint32_t)(gNetOnline ? 1 : 0) ^ (gLoopRate << 16);
    }
    default: return 0;
  }
}

// ---- Dibujo de UNA seccion. 'sy' es su borde superior EN PANTALLA ----
static void almDetDrawSection(int sec, int sy){
  const FlexMemSnap* m = memSnap();
  char v[64];
  int y = sy + 8;
  switch(sec){
    case 0: {                       // cabecera + acceso a Optimizar
      drawText(uiPad() * 2, y, "Detalles de memoria y sistema", 2, TH_TXT);
      y += 26;
      uiSurface(ALMD_BX, y, ALMD_BW, ALMD_BH, ALMD_BH / 2, UIS_ELEVATED);
      drawRoundRect(ALMD_BX, y, ALMD_BW, ALMD_BH, ALMD_BH / 2, TH_BORDER);
      drawTextC(SCR_W / 2, y + (ALMD_BH - 18) / 2, "Optimizar Flex OS", 2, TH_ACCS);
      break;
    }
    case 1: {                       // PSRAM
      y = almDetTitle(y, "PSRAM (memoria de trabajo)");
      if(!m->psTotal){
        almDetNote(y, "No disponible en esta placa.");
        break;
      }
      flexMemFmt(m->psTotal, v, sizeof(v));  y = almDetRow(y, "Total detectada", v, TH_TXT);
      char u[24]; flexMemFmt(flexMemUsed(m), u, sizeof(u));
      snprintf(v, sizeof(v), "%s (%d%%)", u, flexMemUsedPct(m));
      y = almDetRow(y, "En uso", v, TH_TXT);
      flexMemFmt(m->psFree, v, sizeof(v));   y = almDetRow(y, "Libre", v, TH_TXT);
      flexMemFmt(m->psPeakUsed, v, sizeof(v));
      y = almDetRow(y, "Pico de uso desde el arranque", v, TH_TXT2);
      flexMemFmt(m->psLargest, v, sizeof(v));
      y = almDetRow(y, "Bloque libre m\xC3\xA1s grande", v, TH_TXT2);
      int fc = flexMemFragClass(m);
      snprintf(v, sizeof(v), "%s (%d%%)",
               fc == FLEXMEM_FRAG_HIGH ? "Alta" : (fc == FLEXMEM_FRAG_MED ? "Media" : "Baja"),
               flexMemFragPct(m));
      y = almDetRow(y, "Fragmentaci\xC3\xB3n", v,
                    fc == FLEXMEM_FRAG_HIGH ? TH_WARN : TH_TXT2);
      int hh = flexMemHealth(m);
      y = almDetRow(y, "Estado",
                    hh == FLEXMEM_H_CRIT ? "Cr\xC3\xADtico" : (hh == FLEXMEM_H_WATCH ? "Atenci\xC3\xB3n" : "\xC3\x93ptimo"),
                    hh == FLEXMEM_H_CRIT ? TH_ERR : (hh == FLEXMEM_H_WATCH ? TH_WARN : TH_OK));
      int ta, tb; almTopApps(&ta, &tb);
      if(ta >= 0){
        char m1[24]; flexMemFmt(appMemBytes(ta), m1, sizeof(m1));
        if(tb >= 0){
          char m2[24]; flexMemFmt(appMemBytes(tb), m2, sizeof(m2));
          snprintf(v, sizeof(v), "Mayor consumo: %s %s  \xC2\xB7  %s %s",
                   appName(ta), m1, appName(tb), m2);
        } else snprintf(v, sizeof(v), "Mayor consumo: %s %s", appName(ta), m1);
        y = almDetNote(y, v);
      } else {
        y = almDetNote(y, "Mayor consumo: sin medida todav\xC3\xAD" "a.");
      }
      almDetNote(y, "La usan las apps abiertas, las im\xC3\xA1genes y el doble b\xC3\xBA" "fer de pantalla.");
      break;
    }
    case 2: {                       // SRAM interna
      y = almDetTitle(y, "Memoria interna del chip (SRAM)");
      if(!m->inTotal){ almDetNote(y, "No disponible."); break; }
      flexMemFmt(m->inTotal, v, sizeof(v)); y = almDetRow(y, "Total utilizable", v, TH_TXT);
      flexMemFmt(m->inTotal > m->inFree ? m->inTotal - m->inFree : 0, v, sizeof(v));
      y = almDetRow(y, "En uso", v, TH_TXT);
      flexMemFmt(m->inFree, v, sizeof(v));
      y = almDetRow(y, "Libre", v, m->inFree < FLEXMEM_SRAM_LOW_BYTES ? TH_WARN : TH_TXT);
      flexMemFmt(m->inMin, v, sizeof(v));
      y = almDetRow(y, "M\xC3\xADnimo desde el arranque", v, TH_TXT2);
      y = almDetNote(y, "La usan el sistema, el t\xC3\xA1" "ctil, el Wi-Fi y las tareas internas.");
      almDetNote(y, "No conserva datos: al reiniciar se vac\xC3\xAD" "a entera.");
      break;
    }
    case 3: {                       // Flash / LittleFS
      y = almDetTitle(y, "Almacenamiento interno (flash)");
      if(!m->fsValid){ almDetNote(y, "No disponible: la partici\xC3\xB3n de datos no est\xC3\xA1 montada."); break; }
      flexMemFmt(m->fsTotal, v, sizeof(v)); y = almDetRow(y, "Capacidad de datos", v, TH_TXT);
      { char u[24]; flexMemFmt(m->fsUsed, u, sizeof(u));
        int fp = flexMemFlashPct(m);
        snprintf(v, sizeof(v), "%s (%d%%)", u, fp < 0 ? 0 : fp);
        y = almDetRow(y, "Usado", v, (fp >= 90) ? TH_ERR : (fp >= 80 ? TH_WARN : TH_TXT)); }
      flexMemFmt(m->fsTotal > m->fsUsed ? m->fsTotal - m->fsUsed : 0, v, sizeof(v));
      y = almDetRow(y, "Libre", v, TH_TXT);
      if(almFwSize != 0xFFFFFFFFu && almFwSize > 0){ flexMemFmt(almFwSize, v, sizeof(v)); }
      else snprintf(v, sizeof(v), "No disponible");
      y = almDetRow(y, "Tama\xC3\xB1o del firmware", v, TH_TXT2);
      flexMemFmt(flexFsCatSize(FLEXFS_CAT_SYS), v, sizeof(v));
      y = almDetRow(y, "Recursos del sistema", v, TH_TXT2);
      flexMemFmt(flexFsCatSize(FLEXFS_CAT_APPS), v, sizeof(v));
      y = almDetRow(y, "Datos de apps", v, TH_TXT2);
      flexMemFmt(flexFsDirSize(FS_DIR_CACHE), v, sizeof(v));
      y = almDetRow(y, "Cach\xC3\xA9 temporal", v, TH_TXT2);
      flexMemFmt(flexFsCatSize(FLEXFS_CAT_DOCS), v, sizeof(v));
      almDetRow(y, "Archivos de usuario", v, TH_TXT2);
      break;
    }
    case 4: {                       // microSD
      y = almDetTitle(y, "Tarjeta microSD");
      y = almDetRow(y, "Estado", almSdStateText(),
                    flexSdReady() ? TH_OK : (flexSdState() == FLEXSD_ABSENT ? TH_TXT2 : TH_ERR));
      if(flexSdReady()){
        flexMemFmt(flexSdTotalBytes(), v, sizeof(v)); y = almDetRow(y, "Capacidad", v, TH_TXT);
        flexMemFmt(flexSdUsedBytes(),  v, sizeof(v)); y = almDetRow(y, "Usado", v, TH_TXT);
        flexMemFmt(flexSdFreeBytes(),  v, sizeof(v)); y = almDetRow(y, "Libre", v, TH_TXT);
      } else {
        y = almDetNote(y, flexSdError());
        almDetNote(y, "Flex OS no repite el montaje por su cuenta: evita bloquearse.");
      }
      break;
    }
    case 5: {                       // Estado del sistema
      y = almDetTitle(y, "Estado del sistema");
      buildUptime(v, sizeof(v));               y = almDetRow(y, "Tiempo encendido", v, TH_TXT);
      snprintf(v, sizeof(v), "%s", almBootCause());
      y = almDetRow(y, "\xC3\x9Altimo arranque", v, TH_TXT2);
      snprintf(v, sizeof(v), "Flex OS %s", flexOtaLocalVersion());
      y = almDetRow(y, "Versi\xC3\xB3n", v, TH_TXT2);
      snprintf(v, sizeof(v), "%s", gNetOnline ? "Conectado" : "Desconectado");
      y = almDetRow(y, "Wi-Fi", v, gNetOnline ? TH_OK : TH_TXT2);
      snprintf(v, sizeof(v), "%u vueltas/s", (unsigned)gLoopRate);
      y = almDetRow(y, "Ritmo del sistema", v, TH_TXT2);
      { int act = 0, pau = 0, sus = 0;
        for(int i = 0; i < APP_N; i++){
          if(gAppState[i] == ALIFE_CLOSED) continue;
          if(gAppState[i] == ALIFE_SUSPENDED){ if(gAppShed[i]) sus++; else pau++; }
          else act++;
        }
        snprintf(v, sizeof(v), "%d / %d / %d", act, pau, sus); }
      y = almDetRow(y, "Apps activas / pausadas / guardadas", v, TH_TXT2);
      almDetNote(y, "No hay medidor global de FPS: el ritmo del bucle es la medida real.");
      break;
    }
    default: break;
  }
}

// Repinta la ventana de contenido entera (entrada y desplazamiento).
static void almDetailPaint(){
  setBuf(fb);
  int c0 = gClipY0, c1 = gClipY1;
  gClipY0 = ALMD_VY0; gClipY1 = ALMD_VY1;
  fillRect(0, ALMD_VY0, SCR_W, ALMD_VY1 - ALMD_VY0 + 1, WIN_BG);
  for(int i = 0; i < ALMD_SEC_N; i++){
    int sy = ALMD_VY0 + almDetSecTop(i) - almDetScroll;
    if(sy > ALMD_VY1 || sy + ALMD_SEC_H[i] < ALMD_VY0) continue;
    almDetDrawSection(i, sy);
    almDetSig[i] = almDetSecSig(i);
  }
  gClipY0 = c0; gClipY1 = c1;
  flxFlush(ALMD_VY0, ALMD_VY1);
}

// Repinta SOLO la banda de una seccion (refresco de cifras). Es la ruta que
// hace que actualizar una estadistica no cueste una pantalla entera.
static void almDetailRepaintSection(int i){
  int sy = ALMD_VY0 + almDetSecTop(i) - almDetScroll;
  int b0 = sy, b1 = sy + ALMD_SEC_H[i] - 1;
  if(b0 < ALMD_VY0) b0 = ALMD_VY0;
  if(b1 > ALMD_VY1) b1 = ALMD_VY1;
  if(b1 < b0) return;
  setBuf(fb);
  int c0 = gClipY0, c1 = gClipY1;
  gClipY0 = b0; gClipY1 = b1;
  fillRect(0, b0, SCR_W, b1 - b0 + 1, WIN_BG);
  almDetDrawSection(i, sy);
  gClipY0 = c0; gClipY1 = c1;
  flxFlush(b0, b1);
}

// Repintado de la pantalla de detalle SIN reiniciar nada. La usa el retorno del
// optimizador: volver al mismo sitio donde estabas es la mitad del punto de
// tener multitarea de verdad.
static void almDetailRepaint(){
  almDetDrag = false; almDetTouching = false;
  almDetMs = millis();
  almDetailPaint();
}

static void almDetailEnter(){
  almScreen = ALM_SCR_DETAIL;
  almDetScroll = 0;
  almDetDrag = false; almDetTouching = false;
  gMemWantFlash = true;             // a partir de ahora la flash SI se mide...
  memSampleNow();
  memSampleFlashNow();              // ...y la primera lectura es inmediata
  // Tamano del firmware: se mide UNA vez por arranque. Es una lectura de la
  // cabecera de la imagen, no un recorrido de la flash, pero tampoco tiene por
  // que repetirse: no cambia hasta la siguiente actualizacion.
  if(almFwSize == 0xFFFFFFFFu) almFwSize = (uint32_t)ESP.getSketchSize();
  almDetMs = millis();
  almDetailPaint();
}

static void almDetailTick(){
  // ---- Desplazamiento ----
  if(T.pressed){
    almDetDrag = false;
    almDetTouching = (T.y >= ALMD_VY0 && T.y <= ALMD_VY1);
    almDetStartY = T.y; almDetStart0 = (float)almDetScroll;
    return;
  }
  if(T.down){
    if(!almDetTouching) return;
    if(!almDetDrag && fabsf(T.y - almDetStartY) > 10) almDetDrag = true;
    if(almDetDrag){
      int ns = (int)(almDetStart0 + (almDetStartY - T.y));
      int mx = almDetMaxScroll();
      if(ns < 0) ns = 0; if(ns > mx) ns = mx;
      if(ns != almDetScroll){ almDetScroll = ns; almDetailPaint(); }
    }
    return;
  }
  if(T.released || T.tap){
    bool wasDrag = almDetDrag, was = almDetTouching;
    almDetDrag = false; almDetTouching = false;
    if(wasDrag || !was) return;
    // Toque en "Optimizar Flex OS" (seccion 0). Se calcula con la MISMA
    // geometria con la que se dibujo, asi no pueden separarse.
    int sy = ALMD_VY0 + almDetSecTop(0) - almDetScroll;
    int by = sy + 8 + 26;
    if(T.y >= by && T.y <= by + ALMD_BH && T.x >= ALMD_BX && T.x <= ALMD_BX + ALMD_BW){
      optStart();
      return;
    }
    return;
  }
  // ---- Refresco de cifras: por firma, por seccion y con el dedo fuera ----
  if(T.down || millis() - almDetMs < 1000) return;
  almDetMs = millis();
  for(int i = 1; i < ALMD_SEC_N; i++){
    uint32_t sig = almDetSecSig(i);
    if(sig == almDetSig[i]) continue;          // esta seccion no ha cambiado: no se toca
    almDetSig[i] = sig;
    almDetailRepaintSection(i);
  }
}

static void almEnter(){
  // Al abrir Almacenamiento se comprueba la tarjeta EN EL ACTO, UNA vez, sin
  // esperar al periodo de sondeo: es uno de los tres sitios donde el usuario
  // espera ver el estado al dia. Fuera de aqui NO se sondea la tarjeta desde
  // esta app -- ni al desplazarse, ni por cuadro, ni en el refresco de cifras.
  flexSdPoke();
  flexSdTick();
  if(flexSdReady()) flexSdRefreshUsage();
  almScreen = ALM_SCR_MAIN;
  almRenderMain();
}

static void almTick(){
  if(almScreen == ALM_SCR_DETAIL){ almDetailTick(); return; }
  if(!T.tap) return;
  if(almDetY1 > almDetY0 && T.y >= almDetY0 && T.y <= almDetY1){
    if(almOptX1 > almOptX0 && T.x >= almOptX0 && T.x <= almOptX1) optStart();
    else                                                          almDetailEnter();
    return;
  }
  if(almVerY1 > almVerY0 && T.y >= almVerY0 && T.y <= almVerY1){ filesEnter(); return; }
  // La tarjeta abre el MISMO explorador, pero en su raiz: no hay un
  // segundo explorador para la tarjeta.
  if(almSdY1 > almSdY0 && T.y >= almSdY0 && T.y <= almSdY1 && flexSdReady())
    filesEnterAt(FLEXSD_MOUNT);
}

// ---- Ciclo de vida de Almacenamiento ----
// ATRAS desde el detalle vuelve a la lista, no expulsa la app: misma regla que
// ya siguen Galeria, Notas y Multimedia.
static bool almBackScreen(){
  if(almScreen != ALM_SCR_DETAIL) return false;
  almScreen = ALM_SCR_MAIN;
  gMemWantFlash = false;
  almRenderMain();
  return true;
}
// Suspendida, la app deja de pedir la medida cara de la flash. Es la diferencia
// entre "la pantalla esta a la vista" y "la app existe".
static void almSuspend(){ gMemWantFlash = false; }
static void almCloseApp(){ gMemWantFlash = false; almScreen = ALM_SCR_MAIN; almDetScroll = 0; }
static void almResume(){
  if(almScreen == ALM_SCR_DETAIL) almDetailEnter();
  else                            almRenderMain();
}
