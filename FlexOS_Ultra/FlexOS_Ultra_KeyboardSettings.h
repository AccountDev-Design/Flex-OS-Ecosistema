// #############################################################
// ##  FLEX OS ULTRA  ·  AJUSTES DEL TECLADO  (Fase E)
// ##  ----------------------------------------------------------
// ##  Pantalla propia navegable desde Ajustes -> Personalizacion -> Teclado.
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
#include "FlexOS_Ultra_AppNotes.h"   // eslabon anterior de la cadena

// #############################################################
// ##  FASE E · AJUSTES DEL TECLADO  (pantalla propia, navegable)
// ##  ------------------------------------------------------
// ##  Se llega desde Ajustes -> Personalizacion -> Teclado y
// ##  desde el engranaje de la barra de la Fase C.
// ##
// ##  POR QUE UNA PANTALLA PROPIA Y NO UNA 13a CATEGORIA EN LA
// ##  BARRA LATERAL DE AJUSTES: esa barra tiene 12 tarjetas de
// ##  52 px desde y=100, o sea que termina en y=724. Una 13a
// ##  caeria encima de la barra de navegacion (y=748). Meterla
// ##  ahi obligaba a rehacer el layout de Ajustes entero, que no
// ##  es lo que se pidio. Las tarjetas, colores y el patron de
// ##  filas son los mismos de setRowCard, asi que se ve como una
// ##  seccion mas de Ajustes.
// ##
// ##  LO QUE NO SE IMPLEMENTA Y POR QUE (se muestra, atenuado,
// ##  en vez de esconderlo):
// ##   · Entrada de voz -> necesita reconocimiento en la nube.
// ##   · Guardar capturas en portapapeles -> este FlexOS no tiene
// ##     funcion de captura de pantalla de usuario.
// ##   · Respuesta hapticas -> la placa no lleva motor vibrador.
// ##   · Dividir teclado -> con 480 px de ancho no caben dos
// ##     mitades usables; en su lugar esta el selector de tamano.
// #############################################################
#define KBS_MAIN   0
#define KBS_DESIGN 1
#define KBS_TOUCH  2
#define KBS_SYMS   3
#define KBS_SHORT  4
#define KBS_SCEDIT 5
#define KBS_ABOUT  6
#define KBS_LANG   7
#define KBS_ROW_MAX 20
static int  kbsPage = KBS_MAIN;
static int  kbsScroll = 0, kbsContentH = 0;
static int  kbsRowY0[KBS_ROW_MAX], kbsRowY1[KBS_ROW_MAX], kbsRowN = 0;
static int  kbsRet = 0;               // 0 = volver a Notas, 1 = volver a Ajustes
static int  kbsScSel = 0;             // atajo que se esta editando
static int  kbsScField = 0;           // 0 = abreviacion, 1 = expansion
static char kbsScA[KB_SC_ABR], kbsScE[KB_SC_EXP];
static int  kbsDragY0 = 0, kbsDragS0 = 0;   // arrastre de la lista
static bool kbsDragging = false;
#define KBS_TOP    100
#define KBS_BOT    (SCR_H - 24)

static uint16_t kbsBg(){    return PAGE_BG; }
static uint16_t kbsCard(){  return thCard(); }
// Fila a todo el ancho, con el mismo lenguaje visual que setRowCard (tarjeta,
// titulo, valor y chevron). "on" atenua el texto cuando la fila esta apagada
// por hardware (voz, capturas, haptica).
static int kbsRow(int y, const char* title, const char* val, bool chevron, bool enabled){
  int rh = 62, x = 12, w = SCR_W - 24;
  if(kbsRowN < KBS_ROW_MAX){ kbsRowY0[kbsRowN] = y; kbsRowY1[kbsRowN] = y + rh; kbsRowN++; }
  // El vidrio ya NO se apaga durante el arrastre (drawGlassCardFlat lo resuelve
  // una vez y lo vuelca por filas): el material se mantiene mientras se hace
  // scroll, tal cual esta configurado.
  if(uiGlass) drawGlassCardFlat(x, y, w, rh - 8, 12, kbsCard(), kbsBg());
  else fillRoundRect(x, y, w, rh - 8, 12, kbsCard());
  // Fila DESACTIVADA (funcion que este hardware no puede dar): color de control
  // desactivado del tema, no un gris fijo.
  uint16_t tc = enabled ? SET_TXT_HI : TH_DIS;
  uint16_t vc = enabled ? SET_TXT_LO : TH_DIS;
  drawTextClip(x + 16, y + 8, title, 2, tc, x + w - 28);
  if(val) drawTextClip(x + 16, y + 32, val, 1, vc, x + w - 28);
  if(chevron && enabled){
    int chx = x + w - 18, chy = y + (rh - 8) / 2;
    strokeSegAA(chx - 3, chy - 6, chx + 3, chy, 2.0f, SET_CHEV);
    strokeSegAA(chx + 3, chy, chx - 3, chy + 6, 2.0f, SET_CHEV);
  }
  return y + rh;
}
static int kbsSection(int y, const char* t){
  drawText(16, y, t, 2, SET_TXT_HI);
  return y + 30;
}
static const char* kbsSizeName(){ return gKbSize == KB_SIZE_COMPACT ? "Compacto" : gKbSize == KB_SIZE_BIG ? "Grande" : "Normal"; }
static const char* kbsStyleName(){ return gKbStyle == 1 ? "Cuadrada" : gKbStyle == 2 ? "Contorno" : "Redondeada"; }
static const char* kbsFontName(){ return gKbFontSc == 0 ? "Peque\xC3\xB1" "a" : gKbFontSc == 2 ? "Grande" : "Normal"; }
static const char* kbsFxName(){ return gKbFxMs == 60 ? "Corta" : gKbFxMs == 160 ? "Larga" : "Normal"; }
static const char* kbsOnOff(bool b){ return b ? "Activado" : "Desactivado"; }

// ---- Teclado incrustado del editor de atajos ----
// Es el MISMO teclado (misma geometria, mismos colores, mismo kbCellAt), solo
// que escribe en el campo enfocado en vez de en una nota.
static void kbsEditorKb(){
  kbPaintPanel(KB_Y - 4, kbColPanel());
  int fs = kbFontSize();
  for(int r = 0; r < KB_ROWS; r++) for(int c = 0; c < KB_COLS; c++){
    int x = KB_X + c * (KB_KW + KB_GAP), y = KB_Y + r * (KB_KH + KB_GAP);
    char u[6];
    const char* k = kbResolveKey(mapaActivo[r][c], u, false);
    kbPaintKey(x, y, KB_KW, KB_KH, k, fs, kbColKey(), kbColKeyTxt(), false);
  }
  int fy = kbFuncY();
  const char* lb[KB_FKEYS] = { "shift", kbLayerLabel(), kbLangEs ? "ES" : "EN", "espacio", "<-", "OK" };
  for(int i = 0; i < KB_FKEYS; i++) kbFKey(kbFKeyX(i), fy, kbFKeyW(i), lb[i], (i == 0) && kbShift);
}
static void kbsAppendField(const char* s){
  char* dst = kbsScField == 0 ? kbsScA : kbsScE;
  int cap = kbsScField == 0 ? KB_SC_ABR : KB_SC_EXP;
  int L = strlen(dst), sl = strlen(s);
  if(L + sl >= cap - 1) return;
  memcpy(dst + L, s, sl); dst[L + sl] = 0;
}
static void kbsBackField(){
  char* dst = kbsScField == 0 ? kbsScA : kbsScE;
  int L = strlen(dst);
  if(L <= 0) return;
  int q = L - 1; while(q > 0 && (dst[q] & 0xC0) == 0x80) q--;
  dst[q] = 0;
}

// ---- Contenido de cada pagina ----
static void kbsContent(){
  kbsRowN = 0;
  int y = KBS_TOP - kbsScroll;
  char v[64];
  if(kbsPage == KBS_MAIN){
    y = kbsRow(y, "Idiomas y tipos", kbLangEs ? "Espa\xC3\xB1ol (ES) - activo" : "English (EN) - activo", true, true);
    y = kbsRow(y, "Texto predictivo", kbsOnOff(gKbPredict), false, true);
    y = kbsRow(y, "Revisi\xC3\xB3n ortogr\xC3\xA1" "fica b\xC3\xA1sica", kbsOnOff(gKbSpell), false, true);
    y = kbsRow(y, "Sugerir emojis", kbsOnOff(gKbEmojiSug), false, true);
    y = kbsRow(y, "Atajos de texto", "Abreviaci\xC3\xB3n -> expansi\xC3\xB3n", true, true);
    y += 6; y = kbsSection(y, "Teclado");
    y = kbsRow(y, "Barra de herramientas del teclado", kbsOnOff(gKbToolbar), false, true);
    y = kbsRow(y, "Teclado de contraste alto", kbsOnOff(gKbHiCon), false, true);
    // Diagnostico honesto al lado del interruptor: cuantos dedos a la vez ha
    // llegado a reportar el GT911 desde que arranco la placa. Si aqui pone "1
    // dedo" por mucho que se teclee con dos, el problema NO es el firmware: es
    // que ese panel solo esta dando un punto. Sin PC no hay otra forma de saberlo.
    if(gKbFastType) snprintf(v, sizeof(v), "Activado - el panel ha dado %d dedo%s a la vez",
                             kbMtMaxPts, kbMtMaxPts == 1 ? "" : "s");
    else snprintf(v, sizeof(v), "Desactivado - se escribe al soltar");
    y = kbsRow(y, "Escritura r\xC3\xA1pida (multitoque)", v, false, true);
    y = kbsRow(y, "Dise\xC3\xB1o y tama\xC3\xB1o", KB_SIZE_CONFIG_ON ? kbsSizeName() : kbsStyleName(), true, true);
    y = kbsRow(y, "Deslizar, tocar y respuesta t\xC3\xA1" "ctil", kbsFxName(), true, true);
    y += 6; y = kbsSection(y, "No disponible en este hardware");
    y = kbsRow(y, "Entrada de voz", "Necesita reconocimiento en la nube", false, false);
    y = kbsRow(y, "Guardar capturas en portapapeles", "FlexOS no tiene captura de pantalla", false, false);
    y += 6; y = kbsSection(y, "Otros");
    y = kbsRow(y, "Restablecer ajustes del teclado", "Vuelve a los valores por defecto", false, true);
    y = kbsRow(y, "Sobre teclado", "Teclado FlexOS", true, true);
  } else if(kbsPage == KBS_LANG){
    y = kbsRow(y, "Espa\xC3\xB1ol (ES)", kbLangEs ? "Activo" : "Toca para activar", false, true);
    y = kbsRow(y, "English (EN)", kbLangEs ? "Toca para activar" : "Activo", false, true);
    y = kbsRow(y, "Capa num\xC3\xA9rica y de s\xC3\xADmbolos", "Siempre disponible (?123)", false, false);
    y = kbsRow(y, "Emoticonos de texto", "Capa emoji, glifos de la fuente", false, false);
    y += 8;
    drawTextClip(16, y, "El teclado soporta estos dos idiomas: son los que", 1, SET_TXT_MUTE, SCR_W - 16); y += 18;
    drawTextClip(16, y, "tienen mapa de teclas y diccionario propios.", 1, SET_TXT_MUTE, SCR_W - 16); y += 22;
  } else if(kbsPage == KBS_DESIGN){
#if KB_SIZE_CONFIG_ON
    // Con KB_SIZE_CONFIG_ON a 0 esta fila NI SE DIBUJA: el teclado usa los
    // valores fijos de siempre y ofrecer un selector que no hace nada seria
    // mentir. Las filas de abajo se recolocan solas (ver kbsRowAction).
    y = kbsRow(y, "Tama\xC3\xB1o de teclado", kbsSizeName(), false, true);
#endif
    snprintf(v, sizeof(v), "%d%% de opacidad", gKbOpacity);
    y = kbsRow(y, "Tama\xC3\xB1o y transparencia", v, false, true);
    y = kbsRow(y, "Dise\xC3\xB1o", kbsStyleName(), false, true);
    y = kbsRow(y, "Tama\xC3\xB1o de fuente", kbsFontName(), false, true);
    snprintf(v, sizeof(v), "%s %s %s %s", kbSymAt(0), kbSymAt(1), kbSymAt(2), kbSymAt(3));
    y = kbsRow(y, "S\xC3\xADmbolos personalizados", v, true, true);
    y += 8;
    drawTextClip(16, y, "Sin \"dividir teclado\": con 480 px de ancho no caben", 1, SET_TXT_MUTE, SCR_W - 16); y += 18;
    drawTextClip(16, y, "dos mitades usables. En su lugar, los tres tamanos.", 1, SET_TXT_MUTE, SCR_W - 16); y += 22;
  } else if(kbsPage == KBS_TOUCH){
    y = kbsRow(y, "Animaci\xC3\xB3n de tecla presionada", kbsFxName(), false, true);
    snprintf(v, sizeof(v), "%d ms", gKbLpMs);
    y = kbsRow(y, "Pulsaci\xC3\xB3n larga (acentos)", v, false, true);
    y = kbsRow(y, "Respuesta h\xC3\xA1ptica", "La placa no tiene motor vibrador", false, false);
    y += 8;
    drawTextClip(16, y, "Todo el retorno de esta pantalla es VISUAL.", 1, SET_TXT_MUTE, SCR_W - 16); y += 22;
  } else if(kbsPage == KBS_SYMS){
    for(int i = 0; i < KB_SYMS; i++){
      char t[24]; snprintf(t, sizeof(t), "S\xC3\xADmbolo %d", i + 1);
      y = kbsRow(y, t, kbSymAt(i), false, true);
    }
    y += 8;
    drawTextClip(16, y, "Toca una fila para cambiar el simbolo. Salen en la", 1, SET_TXT_MUTE, SCR_W - 16); y += 18;
    drawTextClip(16, y, "franja de arriba al entrar en la capa ?123.", 1, SET_TXT_MUTE, SCR_W - 16); y += 22;
  } else if(kbsPage == KBS_SHORT){
    for(int i = 0; i < KB_SC_MAX; i++){
      if(gKbScAbr[i][0] && gKbScExp[i][0]) snprintf(v, sizeof(v), "%s -> %s", gKbScAbr[i], gKbScExp[i]);
      else snprintf(v, sizeof(v), "Vacio - toca para crear");
      char t[24]; snprintf(t, sizeof(t), "Atajo %d", i + 1);
      y = kbsRow(y, t, v, true, true);
    }
    y += 8;
    drawTextClip(16, y, "Al escribir la abreviacion, el chip de sugerencia", 1, SET_TXT_MUTE, SCR_W - 16); y += 18;
    drawTextClip(16, y, "ofrece la expansion completa.", 1, SET_TXT_MUTE, SCR_W - 16); y += 22;
  } else if(kbsPage == KBS_ABOUT){
    y = kbsRow(y, "Teclado FlexOS", "Version 1.0", false, false);
    snprintf(v, sizeof(v), "%d palabras ES / %d EN", KB_DICT_ES_N, KB_DICT_EN_N);
    y = kbsRow(y, "Diccionario local", v, false, false);
    y = kbsRow(y, "Idiomas", "Espa\xC3\xB1ol, English", false, false);
    y += 10;
    drawTextClip(16, y, "El autocompletado es una LISTA LOCAL FIJA escrita", 1, SET_TXT_MUTE, SCR_W - 16); y += 18;
    drawTextClip(16, y, "en el propio firmware. No es un modelo de IA: no", 1, SET_TXT_MUTE, SCR_W - 16); y += 18;
    drawTextClip(16, y, "aprende, no entiende el contexto y no predice la", 1, SET_TXT_MUTE, SCR_W - 16); y += 18;
    drawTextClip(16, y, "palabra siguiente. Solo busca por prefijo.", 1, SET_TXT_MUTE, SCR_W - 16); y += 24;
    drawTextClip(16, y, "La revision ortografica compara contra esa misma", 1, SET_TXT_MUTE, SCR_W - 16); y += 18;
    drawTextClip(16, y, "lista: subraya lo que no encuentra, nada mas.", 1, SET_TXT_MUTE, SCR_W - 16); y += 22;
  } else if(kbsPage == KBS_SCEDIT){
    // Dos campos + teclado real debajo. El campo enfocado lleva borde azul.
    for(int f = 0; f < 2; f++){
      int fy = KBS_TOP + f * 76;
      const char* lbl = f == 0 ? "Abreviaci\xC3\xB3n (lo que escribes)" : "Expansi\xC3\xB3n (lo que aparece)";
      drawText(16, fy, lbl, 1, SET_TXT_LO);
      fillRoundRect(12, fy + 18, SCR_W - 24, 42, 10, uiGlass ? SET_CARD_GLASS : SET_CARD_BG);
      if(kbsScField == f) drawRoundRect(12, fy + 18, SCR_W - 24, 42, 10, TH_PRIM);   // campo enfocado
      drawTextClip(24, fy + 30, f == 0 ? kbsScA : kbsScE, 2, SET_TXT_HI, SCR_W - 30);
    }
    { int by = KBS_TOP + 160;
      fillRoundRect(12, by, (SCR_W - 36) / 2, 46, 12, TH_PRIM);            // accion primaria
      drawTextC(12 + (SCR_W - 36) / 4, by + 14, "Guardar", 2, TH_ONACC);
      fillRoundRect(24 + (SCR_W - 36) / 2, by, (SCR_W - 36) / 2, 46, 12, TH_DANGER);   // destructiva
      drawTextC(24 + (SCR_W - 36) / 2 + (SCR_W - 36) / 4, by + 14, "Borrar", 2, TH_ONACC); }
    kbsEditorKb();
  }
  kbsContentH = (y + kbsScroll) - KBS_TOP + 20;
}
static const char* kbsTitle(){
  switch(kbsPage){
    case KBS_DESIGN: return "Dise\xC3\xB1o y tama\xC3\xB1o";
    case KBS_TOUCH:  return "Deslizar y tocar";
    case KBS_SYMS:   return "S\xC3\xADmbolos";
    case KBS_SHORT:  return "Atajos de texto";
    case KBS_SCEDIT: return "Editar atajo";
    case KBS_ABOUT:  return "Sobre teclado";
    case KBS_LANG:   return "Idiomas y tipos";
    default:         return "Teclado";
  }
}
// Pinta la pantalla completa en el buffer que toque (fb directo, o bbuf cuando
// se va a animar la transicion).
static void kbsPaint(){
  fillRect(0, 0, SCR_W, SCR_H, kbsBg());
  strokeSegAA(30, 40, 18, 32, 2.4f, SET_TXT_HI);            // flecha de volver
  strokeSegAA(18, 32, 30, 24, 2.4f, SET_TXT_HI);
  drawText(52, 20, kbsTitle(), 3, SET_TXT_HI);
  int c0 = gClipY0, c1 = gClipY1;
  gClipY0 = KBS_TOP - 8; gClipY1 = KBS_BOT;
  kbsContent();
  gClipY0 = c0; gClipY1 = c1;
}
// Se compone en bbuf y se publica de una vez: el cuadro llega entero al panel,
// nunca a medias (flxFlush no devuelve hasta que DMA2D libera el cuadro).
static void kbsRender(){
  setBuf(bbuf);
  kbsPaint();
  present(0, SCR_H - 1);
  setBuf(fb);
}
// Repintado SOLO de la banda de la lista. Es lo que se usa cuadro a cuadro
// mientras el dedo arrastra: ni cabecera ni fondo completo, solo la banda. El
// vidrio de las tarjetas SI se mantiene (drawGlassCardFlat lo resuelve una vez
// y lo vuelca por filas), asi que el scroll va pegado al dedo sin perder el
// material ni parpadear.
static void kbsRenderList(){
  setBuf(bbuf);
  fillRect(0, KBS_TOP - 10, SCR_W, KBS_BOT - (KBS_TOP - 10), kbsBg());
  int c0 = gClipY0, c1 = gClipY1;
  gClipY0 = KBS_TOP - 10; gClipY1 = KBS_BOT;
  kbsContent();
  gClipY0 = c0; gClipY1 = c1;
  present(KBS_TOP - 10, KBS_BOT);
  setBuf(fb);
}
// FASE G - transicion entre pantallas del teclado. NO se inventa un estilo
// nuevo: se reusa gAnimStyle (0 zoom, 1 fundido, 2 deslizar), el mismo ajuste
// que ya gobierna la apertura de apps. Se compone en bbuf y se vuelca; nunca
// se dibuja a medias en pantalla.
static void kbsRenderAnim(){
  if(!KB_ANIM_POLISH_ON || !bbuf){ kbsRender(); return; }
  setBuf(bbuf);
  kbsPaint();
  setBuf(fb);
  const int steps = 6;
  for(int s = 1; s <= steps; s++){
    float p = (float)s / steps;
    if(gAnimStyle == 2){                                   // deslizar desde la derecha
      int off = (int)((1.0f - p) * SCR_W);
      for(int y = 0; y < SCR_H; y++){
        uint16_t* d = fb + (size_t)y * SCR_W;
        const uint16_t* sp = bbuf + (size_t)y * SCR_W;
        for(int x = 0; x < off; x++) d[x] = kbsBg();
        memcpy(d + off, sp, (size_t)(SCR_W - off) * 2);
      }
    } else if(gAnimStyle == 1){                            // fundido
      uint8_t a = (uint8_t)(p * 255);
      for(int y = 0; y < SCR_H; y++){
        uint16_t* d = fb + (size_t)y * SCR_W;
        const uint16_t* sp = bbuf + (size_t)y * SCR_W;
        for(int x = 0; x < SCR_W; x++) d[x] = mix565(d[x], sp[x], a);
      }
    } else {                                               // zoom (del 88% al 100%)
      float k = 0.88f + 0.12f * p;
      int cw = (int)(SCR_W * k), ch = (int)(SCR_H * k);
      int ox = (SCR_W - cw) / 2, oy = (SCR_H - ch) / 2;
      for(int y = 0; y < SCR_H; y++){
        uint16_t* d = fb + (size_t)y * SCR_W;
        int sy = (y - oy) * SCR_H / (ch > 0 ? ch : 1);
        if(y < oy || y >= oy + ch || sy < 0 || sy >= SCR_H){ for(int x = 0; x < SCR_W; x++) d[x] = kbsBg(); continue; }
        const uint16_t* sp = bbuf + (size_t)sy * SCR_W;
        for(int x = 0; x < SCR_W; x++){
          int sx = (x - ox) * SCR_W / (cw > 0 ? cw : 1);
          d[x] = (x < ox || x >= ox + cw || sx < 0 || sx >= SCR_W) ? kbsBg() : sp[sx];
        }
      }
    }
    flxFlushAll();
    delay(12);
  }
  // Fotograma final EXACTO (las interpolaciones dejan redondeos): se copia el
  // buffer bueno tal cual, para que la pantalla que queda no sea la aproximada.
  fbCopyBand(bbuf, 0, SCR_H - 1);
  flxFlushAll();
}
static void kbsGo(int page){
  kbsPage = page; kbsScroll = 0;
  kbsDragging = false;                         // cambiar de pagina cancela cualquier arrastre vivo
  kbsRenderAnim();
}
static void kbsEnter(){
  if(!KB_SETTINGS_ON) return;
  kbsRet = (gState == ST_APP && gAppId == 12) ? 1 : 0;   // 12 = app Ajustes
  kbExtrasOn = false;                                    // aqui el teclado no lleva barra ni chips
  kbBotReserve = 0;                                      // pantalla propia: sin barra de navegacion debajo
  kbApplySize(); kbMtSurfaceReset();
  gState = ST_KBSET; kbsPage = KBS_MAIN; kbsScroll = 0;
  kbsDragging = false;
  kbsRender();
}
static void kbsExit(){
  if(kbsRet == 1){ gState = ST_APP; settingsRender(); return; }
  gState = ST_APP; kbExtrasOn = true; kbBotReserve = navBarVisible() ? NAV_H : 0;
  kbApplySize(); kbChipsBuild(); noteRenderAll();
}
static void kbsResetDefaults(){
  gKbSize = KB_SIZE_NORMAL; gKbFastType = true; gKbToolbar = true; gKbPredict = true;
  gKbSpell = false; gKbEmojiSug = false; gKbHiCon = false; gKbOpacity = 100;
  gKbStyle = 0; gKbFontSc = 1; gKbLpMs = 500; gKbFxMs = 100;
  for(int i = 0; i < KB_SYMS; i++) gKbSym[i] = i;
  kbShortcutsDefaults();
  kbApplySize(); kbPrefsSave();
}
// Accion al tocar la fila idx de la pagina actual.
static void kbsRowAction(int idx){
  if(kbsPage == KBS_MAIN){
    switch(idx){
      case 0: kbsGo(KBS_LANG); return;
      case 1: gKbPredict = !gKbPredict; break;
      case 2: gKbSpell = !gKbSpell; break;
      case 3: gKbEmojiSug = !gKbEmojiSug; break;
      case 4: kbsGo(KBS_SHORT); return;
      case 5: gKbToolbar = !gKbToolbar; break;
      case 6: gKbHiCon = !gKbHiCon; break;
      case 7: gKbFastType = !gKbFastType; kbMtSurfaceReset(); break;
      case 8: kbsGo(KBS_DESIGN); return;
      case 9: kbsGo(KBS_TOUCH); return;
      case 10: case 11: return;                       // filas informativas (hardware)
      case 12: kbsResetDefaults(); kbsRender(); return;
      case 13: kbsGo(KBS_ABOUT); return;
      default: return;
    }
    kbPrefsSave(); kbsRender(); return;
  }
  if(kbsPage == KBS_LANG){
    if(idx == 0 || idx == 1){
      kbLangEs = (idx == 0);
      if(mapaActivo == LAYOUT_ES || mapaActivo == LAYOUT_EN) mapaActivo = kbLangEs ? LAYOUT_ES : LAYOUT_EN;
      kbsRender();
    }
    return;
  }
  if(kbsPage == KBS_DESIGN){
    int k = idx + (KB_SIZE_CONFIG_ON ? 0 : 1);      // sin fila de tamano, todo sube un puesto
    if(k == 0){ gKbSize = (gKbSize + 1) % 3; kbApplySize(); }
    else if(k == 1){ gKbOpacity -= 15; if(gKbOpacity < 40) gKbOpacity = 100; }
    else if(k == 2){ gKbStyle = (gKbStyle + 1) % 3; }
    else if(k == 3){ gKbFontSc = (gKbFontSc + 1) % 3; }
    else if(k == 4){ kbsGo(KBS_SYMS); return; }
    else return;
    kbPrefsSave(); kbsRender(); return;
  }
  if(kbsPage == KBS_TOUCH){
    if(idx == 0){ gKbFxMs = (gKbFxMs == 60) ? 100 : (gKbFxMs == 100) ? 160 : 60; }
    else if(idx == 1){ gKbLpMs = (gKbLpMs == 350) ? 500 : (gKbLpMs == 500) ? 700 : 350; }
    else return;
    kbPrefsSave(); kbsRender(); return;
  }
  if(kbsPage == KBS_SYMS){
    if(idx >= 0 && idx < KB_SYMS){
      gKbSym[idx] = (gKbSym[idx] + 1) % KB_SYM_POOL_N;
      kbPrefsSave(); kbsRender();
    }
    return;
  }
  if(kbsPage == KBS_SHORT){
    if(idx >= 0 && idx < KB_SC_MAX){
      kbsScSel = idx; kbsScField = 0;
      snprintf(kbsScA, sizeof(kbsScA), "%s", gKbScAbr[idx]);
      snprintf(kbsScE, sizeof(kbsScE), "%s", gKbScExp[idx]);
      kbsGo(KBS_SCEDIT);
    }
    return;
  }
}
static void kbsTick(){
  if(!KB_SETTINGS_ON){ gState = ST_APP; return; }
  if(T.tap && T.x < 48 && T.y < 52){                      // volver
    if(kbsPage == KBS_MAIN) kbsExit();
    else if(kbsPage == KBS_SCEDIT) kbsGo(KBS_SHORT);
    else if(kbsPage == KBS_SYMS) kbsGo(KBS_DESIGN);
    else kbsGo(KBS_MAIN);
    return;
  }
  if(kbsPage == KBS_SCEDIT){
    if(!T.tap) return;
    for(int f = 0; f < 2; f++){
      int fy = KBS_TOP + f * 76;
      if(T.y >= fy + 18 && T.y <= fy + 60){ kbsScField = f; kbsRender(); return; }
    }
    { int by = KBS_TOP + 160, hw = (SCR_W - 36) / 2;
      if(T.y >= by && T.y <= by + 46){
        if(T.x >= 12 && T.x <= 12 + hw){                                  // Guardar
          if(kbsScA[0] && kbsScE[0]){ snprintf(gKbScAbr[kbsScSel], KB_SC_ABR, "%s", kbsScA); snprintf(gKbScExp[kbsScSel], KB_SC_EXP, "%s", kbsScE); }
          kbPrefsSave(); kbsGo(KBS_SHORT); return;
        }
        if(T.x >= 24 + hw){                                               // Borrar el atajo
          gKbScAbr[kbsScSel][0] = 0; gKbScExp[kbsScSel][0] = 0;
          kbPrefsSave(); kbsGo(KBS_SHORT); return;
        }
      } }
    int fi = kbFRowHit(T.x, T.y);
    if(fi >= 0){
      if(fi == 0) kbShift = !kbShift;
      else if(fi == 1) mapaActivo = (mapaActivo == LAYOUT_NUM) ? LAYOUT_EMOJI : (mapaActivo == LAYOUT_EMOJI) ? (kbLangEs ? LAYOUT_ES : LAYOUT_EN) : LAYOUT_NUM;
      else if(fi == 2){ kbLangEs = !kbLangEs; if(mapaActivo == LAYOUT_ES || mapaActivo == LAYOUT_EN) mapaActivo = kbLangEs ? LAYOUT_ES : LAYOUT_EN; }
      else if(fi == 3) kbsAppendField(" ");
      else if(fi == 4) kbsBackField();
      else { if(kbsScA[0] && kbsScE[0]){ snprintf(gKbScAbr[kbsScSel], KB_SC_ABR, "%s", kbsScA); snprintf(gKbScExp[kbsScSel], KB_SC_EXP, "%s", kbsScE); kbPrefsSave(); kbsGo(KBS_SHORT); return; } }
      kbsRender(); return;
    }
    int cell = kbCellAt(T.x, T.y);
    if(cell >= 0){
      char u[6];
      const char* k = kbResolveKey(mapaActivo[cell / KB_COLS][cell % KB_COLS], u, true);
      kbsAppendField(k);
      kbsRender();
    }
    return;
  }
  // ARRASTRE REAL de la lista (antes eran saltos de 140 px al soltar). El
  // contenido sigue al dedo cuadro a cuadro, repintando solo su banda y con las
  // tarjetas planas mientras dura; al soltar, un repintado bueno con vidrio.
  int vp = KBS_BOT - KBS_TOP, maxS = kbsContentH - vp; if(maxS < 0) maxS = 0;
  if(T.pressed){ kbsDragY0 = T.y; kbsDragS0 = kbsScroll; kbsDragging = false; }
  if(T.down && maxS > 0){
    int dy = kbsDragY0 - T.y;
    if(!kbsDragging && abs(dy) > 6){ kbsDragging = true; }
    if(kbsDragging){
      int ns = kbsDragS0 + dy;
      if(ns < 0) ns = 0; if(ns > maxS) ns = maxS;
      if(ns != kbsScroll){ kbsScroll = ns; kbsRenderList(); }
      return;
    }
  }
  if(T.released && kbsDragging){ kbsDragging = false; kbsRender(); return; }
  if(T.tap && !kbsDragging && T.y >= KBS_TOP - 8 && T.y <= KBS_BOT){
    for(int i = 0; i < kbsRowN; i++) if(T.y >= kbsRowY0[i] && T.y < kbsRowY1[i]){ kbsRowAction(i); return; }
  }
}
