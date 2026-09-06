// #############################################################
// ##  FLEX OS ULTRA  ·  CODE IDE  ·  asistente de hardware
// ##  ----------------------------------------------------------
// ##  Panel de solo lectura que lista los modulos I2C detectados y genera
// ##  el esqueleto de sketch correspondiente.
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
#include "FlexOS_Ultra_AppGames.h"   // eslabon anterior de la cadena

// #############################################################
// ##  ASISTENTE DE HARDWARE  (FASE 3, dentro de Code IDE)
// ##  ------------------------------------------------------
// ##  Panel de SOLO LECTURA (no se refactoriza el editor, que
// ##  hoy es una demo). Lista los modulos I2C detectados en la
// ##  Fase 2 (detectedModules[]) y, al elegir uno, genera el
// ##  codigo de inicializacion y lo muestra como texto para
// ##  copiar al portapapeles global (clipboard[]). Se dibuja
// ##  a fb con flxFlush una sola vez por cambio de estado
// ##  (nada lo redibuja por frame durante ST_APP con el IDE),
// ##  asi que no hay parpadeo ni otra transferencia en paralelo.
// #############################################################

// Estado del asistente
static bool  hwWizardActive = false;   // panel abierto
static int   hwSelModule    = -1;      // -1 = lista; >=0 = indice en detectedModules (vista codigo)
static bool  hwCopied       = false;   // feedback del boton "Copiar"
static char  hwCode[512]    = "";      // codigo generado (tambien va al portapapeles)

// Geometria: boton del editor
#define HW_BTN_X   (SCR_W / 2 - 140)
#define HW_BTN_Y   (WIN_BOT - 104)
#define HW_BTN_W   280
#define HW_BTN_H   46
// Geometria: lista de modulos
#define HW_LIST_Y0 (WIN_TOP + 56)
#define HW_ROW_H   56
#define HW_CARD_X  24
#define HW_CARD_W  (SCR_W - 48)
#define HW_CARD_H  48
// Geometria: boton "Cerrar" (vista lista)
#define HW_CLOSE_X (SCR_W / 2 - 80)
#define HW_CLOSE_Y (WIN_BOT - 60)
#define HW_CLOSE_W 160
#define HW_CLOSE_H 44
// Geometria: botones "Volver"/"Copiar" (vista codigo)
#define HW_BACK_X  24
#define HW_COPY_X  (SCR_W - 24 - 150)
#define HW_ACT_Y   (WIN_BOT - 60)
#define HW_ACT_W   150
#define HW_ACT_H   44

// drawModuleIcon() se define mas abajo (bloque de la isla); forward-decl para
// poder reutilizar el mismo mapeo tipo->icono aqui.
static void drawModuleIcon(ModuleType type, int x, int y, int S);

// CODE IDE · adaptativo.
//   Esencial   : listado de codigo con numeros de linea + boton del Asistente.
//   Opcional 1 : minimapa / panel de simbolos a la derecha -- aparece cuando el
//                lienzo pasa de 470 px de ancho (el codigo necesita >= 300 px
//                para no cortarse y el panel >= 140 px para leerse).
//   Opcional 2 : pie de aclaracion -- aparece si sobran >= 16 px.
static void ideEnter(){
  setBuf(fb);
  int bx, by, bw, bh; uiBox(bx, by, bw, bh);
  fillRect(bx, by, bw, bh, TH_WIN);
  int pad = uiPad(), gap = uiGap();
  int y = by + pad;
  int fsT = uiFontFit("Code IDE", bw - 2 * pad, uiFontH(bh / 12));
  drawTextC(bx + bw / 2, y, "Code IDE", fsT, TH_TXT);
  y += uiLineH(fsT) + gap;
  int sideW = bw / 4; if(sideW > 190) sideW = 190;
  uint8_t aSide = uiSection(0, bw >= 470);
  int codeW = aSide ? (bw - 2 * pad - sideW - gap) : (bw - 2 * pad);
  const char* code[8] = {
    "#include <FlexOS.h>", "", "void setup() {", "  screen.begin();",
    "  ui.drawHome();", "}", "", "void loop() { ui.tick(); }" };
  int btnH = bh / 9; if(btnH < 30) btnH = 30; if(btnH > 50) btnH = 50;
  int codeBot = by + bh - pad - btnH - gap;
  int lineH = (codeBot - y) / 8; if(lineH < 9) lineH = 9;
  int fsC = lineH >= 20 ? 2 : 1;
  int gut = textW("88", fsC) + 8;
  for(int i = 0; i < 8; i++){
    int ly = y + i * lineH;
    if(ly + lineH > codeBot) break;
    char ln[8]; snprintf(ln, sizeof(ln), "%2d", i + 1);
    drawText(bx + pad, ly, ln, fsC, TH_MUTE);                        // numero de linea
    // El COLOR DEL CODIGO es resaltado de sintaxis: es contenido del editor y
    // se conserva igual en las dos apariencias (como en cualquier IDE).
    drawText(bx + pad + gut, ly, code[i], uiFontFit(code[i], codeW - gut, fsC), rgb565(150,220,180));
  }
  if(aSide){
    int sx = bx + pad + codeW + gap;
    uiRectA(sx, y, sideW, codeBot - y, pad, thCard(), aSide);
    uiTextC(sx + sideW / 2, y + pad, "Simbolos", uiFontFit("Simbolos", sideW - 12, 2), TH_TXT2, aSide);
    const char* sym[3] = { "setup()", "loop()", "FlexOS.h" };
    int syy = y + pad + uiLineH(2) + gap;
    for(int i = 0; i < 3; i++){
      uiText(sx + pad, syy, sym[i], uiFontFit(sym[i], sideW - 2 * pad, 2), TH_TXT, aSide);
      syy += uiLineH(2) + 6;
    }
  }
  hwWizardActive = false; hwSelModule = -1; hwCopied = false;
  int bwn = bw - 2 * pad; if(bwn > 320) bwn = 320;
  int bxn = bx + (bw - bwn) / 2, byn = by + bh - pad - btnH;
  fillRoundRect(bxn, byn, bwn, btnH, btnH / 4, TH_PRIM);
  drawTextC(bx + bw / 2, byn + btnH / 2 - uiLineH(2), "Asistente de Hardware",
            uiFontFit("Asistente de Hardware", bwn - 12, 3), TH_ONACC);
  flxFlush(WIN_TOP, WIN_BOT);
}

// Indices de los modulos activos (para mapear filas de la lista <-> detectedModules)
static int hwActiveList(int* out, int maxn){
  int c = 0;
  for(int i = 0; i < detectedCount && c < maxn; i++)
    if(detectedModules[i].active) out[c++] = i;
  return c;
}

// Genera el codigo de inicializacion del modulo en hwCode[] (char[] + snprintf)
static void hwGenCode(const DetectedModule* m){
  size_t n = 0;
  hwCode[0] = 0;
  #define HWCAT(...) do{ int _w = snprintf(hwCode + n, sizeof(hwCode) - n, __VA_ARGS__); \
                         if(_w > 0){ n += (size_t)_w; if(n >= sizeof(hwCode)) n = sizeof(hwCode) - 1; } }while(0)
  HWCAT("// Inicializacion automatica\n");
  HWCAT("// Modulo: %s", m->name);
  if(m->i2cAddr) HWCAT(" (0x%02X)", m->i2cAddr);
  HWCAT("\n#include <Wire.h>\n\n");
  HWCAT("void setup() {\n");
  HWCAT("  Wire.begin(7, 8);   // SDA=7 SCL=8\n");
  if(m->i2cAddr){
    HWCAT("  Wire.beginTransmission(0x%02X);\n", m->i2cAddr);
    HWCAT("  bool ok = (Wire.endTransmission() == 0);\n");
  }
  switch(m->type){
    case MOD_BME280:  HWCAT("  // Lib sugerida: Adafruit_BME280\n"); break;
    case MOD_MPU6050: HWCAT("  // Lib sugerida: MPU6050 (I2Cdev)\n"); break;
    default: break;
  }
  HWCAT("}\n");
  #undef HWCAT
}

// Dibuja el panel del asistente (una sola vez por cambio de estado)
static void hwDrawWizard(){
  setBuf(fb);
  fillRect(0, WIN_TOP, SCR_W, WIN_BOT - WIN_TOP, WIN_BG);

  if(hwSelModule < 0){
    // ---- Vista LISTA ----
    drawTextC(SCR_W / 2, WIN_TOP + 16, "Asistente de Hardware", 3, TH_TXT);
    int idxs[MAX_MODULES_DETECTED];
    int nc = hwActiveList(idxs, MAX_MODULES_DETECTED);
    if(nc == 0){
      drawTextC(SCR_W / 2, WIN_TOP + 120, "No hay modulos I2C detectados", 2, TH_TXT2);
      drawTextC(SCR_W / 2, WIN_TOP + 150, "Conecta un sensor al bus (SDA=7, SCL=8)", 1, TH_MUTE);
    } else {
      for(int r = 0; r < nc; r++){
        DetectedModule* m = &detectedModules[idxs[r]];
        int cy = HW_LIST_Y0 + r * HW_ROW_H;
        uiSurface(HW_CARD_X, cy, HW_CARD_W, HW_CARD_H, 12, UIS_CARD);
        drawModuleIcon(m->type, HW_CARD_X + 8, cy + 8, 32);
        char label[48];
        if(m->i2cAddr) snprintf(label, sizeof(label), "%s (0x%02X)", m->name, m->i2cAddr);
        else           snprintf(label, sizeof(label), "%s", m->name);
        drawText(HW_CARD_X + 50, cy + 16, label, 2, TH_TXT);
        drawTextC(HW_CARD_X + HW_CARD_W - 46, cy + 18, "Config", 1, TH_ACCS);
      }
    }
    uiSurface(HW_CLOSE_X, HW_CLOSE_Y, HW_CLOSE_W, HW_CLOSE_H, 14, UIS_ELEVATED);
    drawTextC(SCR_W / 2, HW_CLOSE_Y + 14, "Cerrar", 2, TH_TXT);
  } else {
    // ---- Vista CODIGO (solo lectura) ----
    DetectedModule* m = &detectedModules[hwSelModule];
    char t[40]; snprintf(t, sizeof(t), "Codigo: %s", m->name);
    drawTextC(SCR_W / 2, WIN_TOP + 16, t, 2, TH_TXT);

    int px = 16, py = WIN_TOP + 52, pw = SCR_W - 32, ph = (HW_ACT_Y - 12) - (WIN_TOP + 52);
    uiSurface(px, py, pw, ph, 12, UIS_CARD);

    // Volcar hwCode linea a linea (split por '\n')
    int ly = py + 12, lineNo = 1;
    const char* p = hwCode;
    char line[80];
    while(*p){
      int li = 0;
      while(*p && *p != '\n' && li < 78){ line[li++] = *p++; }
      line[li] = 0;
      if(*p == '\n') p++;
      char num[6]; snprintf(num, sizeof(num), "%2d", lineNo++);
      drawText(px + 10, ly, num,  1, TH_MUTE);
      drawText(px + 38, ly, line, 1, rgb565(150, 220, 180));   // resaltado de sintaxis: contenido
      ly += 18;
      if(ly > py + ph - 16) break;
    }

    uiSurface(HW_BACK_X, HW_ACT_Y, HW_ACT_W, HW_ACT_H, 12, UIS_ELEVATED);
    drawTextC(HW_BACK_X + HW_ACT_W / 2, HW_ACT_Y + 14, "Volver", 2, TH_TXT);
    fillRoundRect(HW_COPY_X, HW_ACT_Y, HW_ACT_W, HW_ACT_H, 12, hwCopied ? TH_OK : TH_PRIM);   // "Copiado" = exito
    drawTextC(HW_COPY_X + HW_ACT_W / 2, HW_ACT_Y + 14, hwCopied ? "Copiado" : "Copiar", 2, TH_ONACC);
  }
  flxFlush(WIN_TOP, WIN_BOT);
}

// tick del Code IDE: gestiona el boton del editor y los toques del asistente.
// El marco (chevron/atras/nav) lo sigue cerrando el framework -> cierra la app.
static void ideTick(){
  if(hwWizardActive){
    if(!T.tap) return;
    if(hwSelModule < 0){
      // Vista lista: tap en una tarjeta -> generar codigo y pasar a vista codigo
      int idxs[MAX_MODULES_DETECTED];
      int nc = hwActiveList(idxs, MAX_MODULES_DETECTED);
      for(int r = 0; r < nc; r++){
        int cy = HW_LIST_Y0 + r * HW_ROW_H;
        if(T.x >= HW_CARD_X && T.x <= HW_CARD_X + HW_CARD_W && T.y >= cy && T.y <= cy + HW_CARD_H){
          hwSelModule = idxs[r]; hwCopied = false;
          hwGenCode(&detectedModules[hwSelModule]);
          hwDrawWizard();
          return;
        }
      }
      // Cerrar -> volver al editor
      if(T.x >= HW_CLOSE_X && T.x <= HW_CLOSE_X + HW_CLOSE_W && T.y >= HW_CLOSE_Y && T.y <= HW_CLOSE_Y + HW_CLOSE_H){
        hwWizardActive = false;
        ideEnter();
        return;
      }
    } else {
      // Vista codigo: Volver
      if(T.x >= HW_BACK_X && T.x <= HW_BACK_X + HW_ACT_W && T.y >= HW_ACT_Y && T.y <= HW_ACT_Y + HW_ACT_H){
        hwSelModule = -1; hwCopied = false; hwDrawWizard();
        return;
      }
      // Copiar al portapapeles global
      if(T.x >= HW_COPY_X && T.x <= HW_COPY_X + HW_ACT_W && T.y >= HW_ACT_Y && T.y <= HW_ACT_Y + HW_ACT_H){
        strncpy(clipboard, hwCode, sizeof(clipboard) - 1);
        clipboard[sizeof(clipboard) - 1] = 0;
        hwCopied = true; hwDrawWizard();
        return;
      }
    }
    return;
  }
  // Editor: abrir el asistente
  if(T.tap && T.x >= HW_BTN_X && T.x <= HW_BTN_X + HW_BTN_W && T.y >= HW_BTN_Y && T.y <= HW_BTN_Y + HW_BTN_H){
    hwWizardActive = true; hwSelModule = -1; hwCopied = false;
    hwDrawWizard();
  }
}
