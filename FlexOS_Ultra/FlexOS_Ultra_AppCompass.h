// #############################################################
// ##  FLEX OS ULTRA  ·  FLEX COMPASS  ·  brujula sobre el BNO085
// ##  ----------------------------------------------------------
// ##  App nativa (id IC_BRUJULA de APP_REG). Toda la orientacion sale
// ##  del servicio compartido FlexOS_Ultra_IMU.h: aqui NO hay driver,
// ##  ni un segundo bus, ni una segunda verdad sobre el sensor.
// ##
// ##  LO QUE ESTA APP NO HACE
// ##  ----------------------------------------------------------
// ##  No ensena un rumbo cuando no hay rumbo. Sin BNO085 no sale un 0,
// ##  ni un 360, ni una aguja quieta: sale la pantalla de requisito con
// ##  el estado REAL del sondeo. Si el modulo se desenchufa con la app
// ##  abierta, el ultimo dato NO se queda en pantalla haciendose pasar
// ##  por actual.
// ##
// ##  DOS RUMBOS, A PROPOSITO
// ##  ----------------------------------------------------------
// ##    · el del SENSOR   (imuHeading(), del servicio) se actualiza cuando
// ##      llega un vector de rotacion y no espera a ninguna animacion;
// ##    · el VISUAL       (cmpHeadVis) persigue al anterior por el
// ##      camino angular MAS CORTO, asi que 359 -> 0 recorre un grado
// ##      y no trescientos cincuenta y nueve al reves.
// ##  El numero grande ensena el visual, que es el que acompana a la
// ##  rosa: si ensenara el crudo, el texto y la aguja se contradirian.
// ##
// ##  DIBUJO POR BANDAS
// ##  ----------------------------------------------------------
// ##  Nada se repinta entero "por si acaso". Cada vuelta marca la banda
// ##  sucia (la rosa, el modulo, los datos tecnicos o el viewport
// ##  completo si se esta arrastrando) y se publica UNA sola vez. Con la
// ##  brujula fuera de pantalla -- desplazada hacia arriba -- su
// ##  animacion no cuesta ni un pixel.
// ##
// ##  COMO ENCAJA ESTE ARCHIVO
// ##  ----------------------------------------------------------
// ##  Es una PARTE del sketch FlexOS_Ultra.ino, no una unidad de
// ##  traduccion independiente. Se incluye en la cadena de modulos, en
// ##  su sitio; no lo incluyas por tu cuenta desde otro lado.
// #############################################################
#pragma once
#include "FlexOS_Ultra_Recovery.h"   // eslabon anterior de la cadena

// ---- Vistas y capas ----
#define CMPV_SEARCH   0     // "Requiere modulo IMU" / "Buscando modulo IMU"
#define CMPV_COMPASS  1     // brujula + secciones desplazables

#define CMPO_NONE     0
#define CMPO_MENU     1     // menu de los tres puntos
#define CMPO_INFO     2     // ficha "Informacion del sensor"

#define CMP_XFADE_MS  220   // fundido entre vistas (mitad y mitad)
#define CMP_ANIM_MS    33   // ~30 fps para la rosa
#define CMP_MOD_MS     50   // ~20 fps para el modulo 2.5D
#define CMP_TECH_MS   250   // los datos tecnicos no necesitan mas
#define CMP_MENU_W    272
#define CMP_MENU_RH    54
#define CMP_MENU_PAD   10
#define CMP_MENU_N      1

// ---- Estado de la app ----
static uint8_t  cmpView      = CMPV_SEARCH;   // la vista que se esta DIBUJANDO
static uint8_t  cmpWant      = CMPV_SEARCH;   // la que pide el estado del servicio
static uint32_t cmpXfadeMs   = 0;             // 0 = sin transicion en curso
static uint8_t  cmpOverlay   = CMPO_NONE;
static bool     cmpHoldsImu  = false;         // esta app tiene adquirido el servicio

// Desplazamiento (fisica identica a la del resto del sistema: seguimiento del
// dedo, inercia y rebote elastico contra los limites, todo por tiempo real).
static float    cmpScroll    = 0.0f;
static float    cmpScrollVel = 0.0f;
static bool     cmpDrag      = false;
static int      cmpDragY0    = -1, cmpDragX0 = 0;
static float    cmpDragS0    = 0.0f;
static int      cmpLastY     = 0;
static uint32_t cmpLastMoveMs = 0;
static uint32_t cmpPhysMs    = 0;

// LECTURA DEL SERVICIO, TOMADA UNA VEZ POR VUELTA. Dibujar llamando al
// servicio en cada primitiva significaria que el numero y la aguja pudieran
// salir de informes distintos dentro del mismo cuadro. Aqui se toma una vez y
// todo el cuadro usa lo mismo. `have` en false = NO hay orientacion: quien
// dibuja tiene que decir "No disponible", nunca un cero.
static bool     cmpHave      = false;
static float    cmpHead      = 0.0f;
static float    cmpPitch     = 0.0f, cmpRoll = 0.0f;
static uint8_t  cmpAcc       = 0;       // 0..3 tal cual lo publica el BNO085

// Rumbo visual (ver la cabecera) y ultimos valores dibujados.
static float    cmpHeadVis   = 0.0f;
static bool     cmpHeadInit  = false;
static float    cmpDrawnHead = -1000.0f;
static float    cmpDrawnPitch = -1000.0f, cmpDrawnRoll = -1000.0f;
static uint32_t cmpAnimMs    = 0, cmpModMs = 0, cmpTechMs = 0;
static uint8_t  cmpDrawnState = 255;
// Fundido corto del renglon de estado de la pantalla de requisito. Es lo que
// hace que "Detectando" -> "Conectado" -> "No detectado" no salten de golpe,
// sin caer en una animacion aparatosa: aparece, no da volteretas.
#define CMP_STFADE_MS 180
static uint32_t cmpStateFadeMs = 0;

// Banda sucia acumulada en la vuelta (se publica una sola vez al final).
static int      cmpDirtyY0 = 1, cmpDirtyY1 = 0;

// Geometria del documento desplazable. La calcula cmpLayout(): el dibujo y el
// tactil leen de aqui, nunca de constantes repetidas.
static int cmpRoseR = 140, cmpRoseCY = 180;
static int cmpYNum = 0, cmpYDir = 0, cmpYDirLong = 0, cmpYAcc = 0, cmpYHint = 0;
static int cmpYCard = 0, cmpHCard = 0;
static int cmpYImuTitle = 0, cmpYModule = 0, cmpHModule = 0, cmpYChecks = 0;
static int cmpYTechTitle = 0, cmpYTech = 0;
static int cmpContentH = 0;
static int cmpAnimTop = 0, cmpAnimBot = 0;      // banda animada de la brujula (coords de documento)

// #############################################################
// ##  CAJA UTIL
// ##  La cabecera (uiHdrDraw) es FIJA y de ella para abajo manda el
// ##  viewport. La franja de la barra de navegacion la estampa el
// ##  sistema dentro de flxFlush, asi que aqui solo se reserva.
// #############################################################
static inline int cmpVpTop(){ return UIHDR_H; }
static int cmpVpBot(){
  if(gHosted) return SCR_H - 8;
  return SCR_H - (gNavMode == 0 ? NAV_H : 34);
}
static int cmpVpH(){ int h = cmpVpBot() - cmpVpTop(); return h < 160 ? 160 : h; }

// ---- Formateo de angulos SIN coma flotante en printf -----------------------
// snprintf("%f") arrastra el formateador de coma flotante entero; aqui se
// compone a mano con enteros, que es lo que hace el resto del sistema.
static void cmpFmtDeg(char* out, size_t n, float v, bool withSign){
  long t = (long)(v * 10.0f + (v >= 0 ? 0.5f : -0.5f));
  bool neg = t < 0; if(neg) t = -t;
  snprintf(out, n, "%s%ld.%ld\xC2\xB0", neg ? "-" : (withSign ? "+" : ""), t / 10, t % 10);
}
static void cmpFmtHeading(char* out, size_t n, float v){
  long t = (long)(imuNorm360(v) * 10.0f + 0.5f);
  if(t >= 3600) t -= 3600;
  snprintf(out, n, "%ld.%ld\xC2\xB0", t / 10, t % 10);
}

// Toma la orientacion del servicio. Devuelve true si cambio algo que importe.
static void cmpSample(){
  float h, p, r;
  bool ok = imuHeading(&h) && imuPitchRoll(&p, &r);
  if(!ok){ cmpHave = false; return; }
  cmpHave = true;
  cmpHead = h; cmpPitch = p; cmpRoll = r;
  uint8_t a = flexBnoFusionAcc();
  cmpAcc = (a == 0xFF) ? 0 : a;
}

// ---- Textos de estado (todos salen del servicio, ninguno es decorativo) ----
static const char* cmpStateText(){
  switch(imuState()){
    case FIMU_DETECTING:    return "Detectando";
    case FIMU_CONNECTED:    return "Conectado";
    case FIMU_AHRS_ACTIVE:  return "Conectado";
    case FIMU_DISCONNECTED: return "Desconectada";
    case FIMU_ERROR:        return "Error del bus";
    default:                return "No detectado";
  }
}
static uint16_t cmpStateColor(){
  switch(imuState()){
    case FIMU_CONNECTED:
    case FIMU_AHRS_ACTIVE:  return TH_OK;
    case FIMU_DETECTING:    return TH_WARN;
    case FIMU_ERROR:        return TH_ERR;
    default:                return TH_DANGER;
  }
}
static const char* cmpAccText(uint8_t a){
  switch(a){
    case 3:  return "Alta";
    case 2:  return "Media";
    case 1:  return "Baja";
    default: return "Sin calibrar";
  }
}

// #############################################################
// ##  MAQUETACION
// #############################################################
static void cmpLayout(){
  int H = cmpVpH();
  // --- Hero: la brujula ocupa la primera pantalla ENTERA, para que la
  // --- tarjeta de estado no se vea en el primer cuadro (es secundaria).
  int belowNeeded = uiLineH(5) + 10 + uiLineH(4) + 4 + uiLineH(2) + 14 + 30 + 26;
  int r = (H - 46 - belowNeeded) / 2;
  if(r > 150) r = 150;
  if(r < 74)  r = 74;
  cmpRoseR  = r;
  // El bloque entero (rosa + lectura) se CENTRA en el hueco util. Anclado
  // arriba dejaba un vacio grande debajo del rumbo en cuanto la pantalla daba
  // de si, y la brujula se veia descolgada del centro.
  int blockH = 2 * r + belowNeeded;
  int top = (H - 46 - blockH) / 2;
  if(top < 16) top = 16;
  cmpRoseCY = top + r;
  int y = cmpRoseCY + r + 26;
  cmpYNum     = y;              y += uiLineH(5) + 10;
  cmpYDir     = y;              y += uiLineH(4) + 4;
  cmpYDirLong = y;              y += uiLineH(2) + 14;
  cmpYAcc     = y;              y += 30;
  cmpYHint    = H - 40;
  if(cmpYHint < y + 8) cmpYHint = y + 8;
  cmpAnimTop  = cmpRoseCY - r - 16;
  cmpAnimBot  = cmpYAcc + 34;

  // --- Secundario: empieza justo DESPUES de la primera pantalla.
  y = H + 18;
  cmpHCard = 104;
  cmpYCard = y;                 y += cmpHCard + 34;
  cmpYImuTitle = y;             y += uiLineH(3) + 16;
  cmpHModule   = 230;
  cmpYModule   = y;             y += cmpHModule + 18;
  cmpYChecks   = y;             y += 4 * 42 + 30;
  cmpYTechTitle = y;            y += uiLineH(3) + 16;
  cmpYTech     = y;             y += 8 * 38 + 20;
  cmpContentH  = y + 30;
}
static int cmpScrollMax(){
  int m = cmpContentH - cmpVpH();
  return m > 0 ? m : 0;
}

// Marca banda sucia (coordenadas de PANTALLA).
static void cmpMark(int y0, int y1){
  if(y1 < y0) return;
  if(cmpDirtyY0 > cmpDirtyY1){ cmpDirtyY0 = y0; cmpDirtyY1 = y1; return; }
  if(y0 < cmpDirtyY0) cmpDirtyY0 = y0;
  if(y1 > cmpDirtyY1) cmpDirtyY1 = y1;
}
// Banda de pantalla ocupada por un tramo del documento, recortada al viewport.
// Devuelve false si ese tramo no se ve ahora mismo.
static bool cmpBandOf(int docY0, int docY1, int* sy0, int* sy1){
  int base = cmpVpTop() - (int)cmpScroll;
  int a = base + docY0, b = base + docY1;
  int vt = cmpVpTop(), vb = cmpVpBot();
  if(a < vt) a = vt;
  if(b > vb) b = vb;
  if(a > b) return false;
  *sy0 = a; *sy1 = b;
  return true;
}

// #############################################################
// ##  PIEZAS DE DIBUJO
// #############################################################

// Punto de estado (el "●" de las maquetas), con su color real.
static void cmpDotA(int cx, int cy, uint16_t col, uint8_t a){
  if(a == 0) return;
  fillCircleA(cx, cy, 5, col, a);
  fillCircleA(cx, cy, 8, col, (uint8_t)((int)a * 60 / 255));
}
static inline void cmpDot(int cx, int cy, uint16_t col){ cmpDotA(cx, cy, col, 255); }
// Fila "etiqueta ........ valor" dentro de un ancho dado.
static void cmpRow(int x, int y, int w, const char* label, const char* value,
                   uint16_t vcol, int fs){
  drawText(x, y, label, fs, TH_TXT2);
  drawTextR(x + w, y, value, fs, vcol);
}
// Fila con punto de estado a la izquierda del valor.
static void cmpRowDot(int x, int y, int w, const char* label, const char* value, uint16_t col){
  drawText(x, y, label, 3, TH_TXT2);
  int vw = textW(value, 3);
  cmpDot(x + w - vw - 18, y + uiLineH(3) / 2 - 2, col);
  drawTextR(x + w, y, value, 3, col);
}
// Marca de comprobacion / aspa, vectorial.
static void cmpCheck(int cx, int cy, bool on){
  uint16_t c = on ? TH_OK : TH_MUTE;
  if(on){
    strokeSegAA((float)(cx - 8), (float)cy,       (float)(cx - 2), (float)(cy + 6), 2.2f, c);
    strokeSegAA((float)(cx - 2), (float)(cy + 6), (float)(cx + 9), (float)(cy - 7), 2.2f, c);
  } else {
    strokeSegAA((float)(cx - 7), (float)(cy - 7), (float)(cx + 7), (float)(cy + 7), 2.2f, c);
    strokeSegAA((float)(cx + 7), (float)(cy - 7), (float)(cx - 7), (float)(cy + 7), 2.2f, c);
  }
}

// #############################################################
// ##  MODULO BNO085 DIBUJADO POR CODIGO
// ##  ----------------------------------------------------------
// ##  Ni un bitmap: es una caja en 3D (placa + grosor) cuyos ocho
// ##  vertices se rotan con la orientacion REAL del sensor y se
// ##  proyectan con una axonometria sencilla. Con los tres angulos a
// ##  cero sale la vista limpia que usa la pantalla de requisito; con
// ##  datos del sensor, la placa se inclina como la de verdad.
// ##
// ##  Es geometria, asi que escala a cualquier tamano sin perder
// ##  nitidez, y cuesta media docena de cuadrilateros -- no un modelo
// ##  3D con malla, que no tendria sentido en un ESP32-P4.
// #############################################################
struct CmpVec3 { float x, y, z; };

// Rotacion Rz(yaw) * Ry(pitch) * Rx(roll) aplicada a un punto local.
static CmpVec3 cmpRot3(float x, float y, float z, float cy_, float sy_, float cp, float sp, float cr, float sr){
  // Rx(roll)
  float y1 = y * cr - z * sr;
  float z1 = y * sr + z * cr;
  // Ry(pitch)
  float x2 = x * cp + z1 * sp;
  float z2 = -x * sp + z1 * cp;
  // Rz(yaw)
  CmpVec3 o;
  o.x = x2 * cy_ - y1 * sy_;
  o.y = x2 * sy_ + y1 * cy_;
  o.z = z2;
  return o;
}
// Proyeccion axonometrica: la profundidad se comprime y la altura sube.
static inline void cmpProj(const CmpVec3& v, int cx, int cy, float s, int* sx, int* sy){
  *sx = cx + (int)(v.x * s + 0.5f);
  *sy = cy + (int)((-v.y * 0.62f - v.z * 0.78f) * s + 0.5f);
}

// Dibuja el modulo. (cx,cy) es su centro, `w` el ancho total RESERVADO en px
// (la placa mide 0,77 de ese ancho: el resto es el sitio que necesita al girar).
// yaw/pitch/roll en GRADOS; con los tres a 0 la placa sale plana y de frente.
static void cmpDrawModule(int cx, int cy, int w, float yawDeg, float pitchDeg, float rollDeg){
  const float BW = 1.00f, BH = 0.70f, BT = 0.055f;   // placa: ancho, fondo, grosor (unidades)
  const float PY = 0.72f, PZ = 0.85f;                // compresion de la profundidad y de la altura
  float s  = (float)w / 1.30f;                       // escala px por unidad
  float ry = yawDeg   * 0.0174532925f;
  float rp = pitchDeg * 0.0174532925f;
  float rr = rollDeg  * 0.0174532925f;
  float cyw = cosf(ry), syw = sinf(ry);
  float cp  = cosf(rp), sp  = sinf(rp);
  float cr  = cosf(rr), sr  = sinf(rr);
  const float hx = BW / 2, hy = BH / 2, hz = BT / 2;

  // Punto de la placa en coordenadas de serigrafia (u,v en [-1,1]; z = +1 cara
  // superior, -1 cara inferior) proyectado a pantalla.
  auto face = [&](float u, float v, float zz, int* ox, int* oy){
    CmpVec3 q = cmpRot3(u * hx, v * hy, zz * hz, cyw, syw, cp, sp, cr, sr);
    *ox = cx + (int)(q.x * s + 0.5f);
    *oy = cy + (int)((-q.y * PY - q.z * PZ) * s + 0.5f);
  };
  int tx0, ty0, tx1, ty1, tx2, ty2, tx3, ty3;        // cara superior
  int bx0, by0, bx1, by1, bx2, by2, bx3, by3;        // cara inferior
  face(-1,  1,  1, &tx0, &ty0); face( 1,  1,  1, &tx1, &ty1);
  face( 1, -1,  1, &tx2, &ty2); face(-1, -1,  1, &tx3, &ty3);
  face(-1,  1, -1, &bx0, &by0); face( 1,  1, -1, &bx1, &by1);
  face( 1, -1, -1, &bx2, &by2); face(-1, -1, -1, &bx3, &by3);

  // ¿SE VE LA CARA SUPERIOR? Area con signo del cuadrilatero proyectado: con la
  // placa de frente sale positiva, y al voltearla cambia de signo. Asi la
  // serigrafia no se dibuja "a traves" de la placa cuando esta boca abajo, que
  // es lo que delataria que esto no es un objeto sino cuatro poligonos.
  long area = (long)tx0 * ty1 - (long)tx1 * ty0 + (long)tx1 * ty2 - (long)tx2 * ty1
            + (long)tx2 * ty3 - (long)tx3 * ty2 + (long)tx3 * ty0 - (long)tx0 * ty3;
  bool topUp = (area > 0);

  uint16_t pcb   = rgb565(20, 88, 72);      // verde placa
  uint16_t pcbD  = rgb565(11, 54, 45);      // canto
  uint16_t pcbB  = rgb565(14, 66, 55);      // cara inferior
  uint16_t silk  = rgb565(234, 244, 240);
  uint16_t chipC = rgb565(24, 26, 32);
  uint16_t chipE = rgb565(64, 70, 82);
  uint16_t gold  = rgb565(214, 176, 82);

  // Cantos primero: la cara que toque los tapa por donde corresponde.
  fillQuad(tx3, ty3, tx2, ty2, bx2, by2, bx3, by3, pcbD);
  fillQuad(tx2, ty2, tx1, ty1, bx1, by1, bx2, by2, pcbD);
  fillQuad(tx0, ty0, tx3, ty3, bx3, by3, bx0, by0, pcbD);
  fillQuad(tx1, ty1, tx0, ty0, bx0, by0, bx1, by1, pcbD);

  if(!topUp){                                        // placa boca abajo: solo el dorso
    fillQuad(bx0, by0, bx1, by1, bx2, by2, bx3, by3, pcbB);
    return;
  }
  fillQuad(tx0, ty0, tx1, ty1, tx2, ty2, tx3, ty3, pcb);

  int ax, ay, bx_, by_, ccx, ccy, dx_, dy_;
  // Borde serigrafiado.
  face(-0.93f,  0.90f, 1, &ax, &ay);  face( 0.93f,  0.90f, 1, &bx_, &by_);
  face( 0.93f, -0.90f, 1, &ccx, &ccy); face(-0.93f, -0.90f, 1, &dx_, &dy_);
  strokeSegAA((float)ax,  (float)ay,  (float)bx_, (float)by_, 1.1f, silk);
  strokeSegAA((float)bx_, (float)by_, (float)ccx, (float)ccy, 1.1f, silk);
  strokeSegAA((float)ccx, (float)ccy, (float)dx_, (float)dy_, 1.1f, silk);
  strokeSegAA((float)dx_, (float)dy_, (float)ax,  (float)ay,  1.1f, silk);

  // Encapsulado del BNO085 y su marca de pin 1.
  face(-0.23f,  0.36f, 1, &ax, &ay);  face( 0.23f,  0.36f, 1, &bx_, &by_);
  face( 0.23f, -0.14f, 1, &ccx, &ccy); face(-0.23f, -0.14f, 1, &dx_, &dy_);
  fillQuad(ax, ay, bx_, by_, ccx, ccy, dx_, dy_, chipC);
  strokeSegAA((float)ax,  (float)ay,  (float)bx_, (float)by_, 0.8f, chipE);
  strokeSegAA((float)dx_, (float)dy_, (float)ccx, (float)ccy, 0.8f, chipE);
  { int q, r; face(-0.16f, 0.28f, 1, &q, &r); fillCircle(q, r, w / 90 + 1, silk); }

  // Tira de pines de la placa y taladros de sujecion en las cuatro esquinas.
  for(int i = 0; i < 6; i++){
    int q, r; face(-0.60f + i * 0.24f, -0.74f, 1, &q, &r);
    fillCircle(q, r, w / 56 + 1, gold);
  }
  { int q, r;
    face(-0.85f,  0.80f, 1, &q, &r); drawCircle(q, r, w / 48 + 1, silk);
    face( 0.85f,  0.80f, 1, &q, &r); drawCircle(q, r, w / 48 + 1, silk);
    face(-0.85f, -0.80f, 1, &q, &r); drawCircle(q, r, w / 48 + 1, silk);
    face( 0.85f, -0.80f, 1, &q, &r); drawCircle(q, r, w / 48 + 1, silk); }

  // SERIGRAFIA. El texto del sistema no se puede rotar, asi que se ancla al
  // punto de la placa que le toca y se escoge el tamano que CABE en el ancho
  // realmente proyectado. Si la placa esta tan de canto que no cabe ni el
  // cuerpo mas pequeno, no se dibuja: es preferible a una etiqueta encajada a
  // la fuerza sobre una rendija.
  face(-0.88f, 0.0f, 1, &ax, &ay); face(0.88f, 0.0f, 1, &bx_, &by_);
  int projW = (int)sqrtf((float)((bx_ - ax) * (bx_ - ax) + (by_ - ay) * (by_ - ay)));
  if(projW >= 54){
    int fs = uiFontFit("BNO085", projW - 8, projW >= 190 ? 3 : 2);
    int q, r; face(0.0f, 0.66f, 1, &q, &r);
    drawTextC(q, r - uiLineH(fs) / 2, "BNO085", fs, silk);
    int fs2 = uiFontFit("9-DOF AHRS", projW - 8, fs > 1 ? fs - 1 : 1);
    face(0.0f, -0.42f, 1, &q, &r);
    drawTextC(q, r - uiLineH(fs2) / 2, "9-DOF AHRS", fs2, rgb565(180, 202, 194));
  }
}

// #############################################################
// ##  ROSA DE LOS VIENTOS
// ##  La rosa GIRA con el rumbo (-heading) y el indice de arriba se
// ##  queda quieto: lo que marca el indice es hacia donde apunta el
// ##  aparato. Las marcas se reparten cada 5 grados sobre los 360.
// #############################################################
static void cmpDrawRose(int cx, int cy, int R, float heading){
  const float PR = 0.0174532925f;
  uint16_t acc = wallAccent();
  uint16_t nCol = TH_DANGER;

  // Disco y anillo. Relleno SOLIDO a proposito, tambien con Liquid Glass
  // activo: la brujula es el contenido, y meter desenfoque y mezcla por pixel
  // detras de un disco de 150 px de radio que se repinta 30 veces por segundo
  // seria pagar el material mas caro del sistema justo donde no aporta nada.
  // El vidrio se reserva para las tarjetas, que es donde se ve.
  fillCircle(cx, cy, R, TH_SURF);
  fillRing(cx, cy, R, 2, TH_BORDER);
  fillRing(cx, cy, R - 34, 1, TH_DIV);

  for(int b = 0; b < 360; b += 5){
    float th = ((float)b - heading) * PR;
    float sn = sinf(th), cs = cosf(th);
    bool major = (b % 45) == 0;
    bool med   = (b % 15) == 0;
    int len = major ? 17 : (med ? 11 : 7);
    float w  = major ? 2.2f : (med ? 1.5f : 1.0f);
    uint16_t col = (b == 0) ? nCol : (major ? TH_TXT : (med ? TH_TXT2 : TH_MUTE));
    float r0 = (float)(R - 3), r1 = (float)(R - 3 - len);
    strokeSegAA(cx + r0 * sn, cy - r0 * cs, cx + r1 * sn, cy - r1 * cs, w, col);
  }
  // Etiquetas de los ocho rumbos principales.
  int lr = R - 34 - 16;
  if(lr > 20){
    for(int k = 0; k < 8; k++){
      int b = k * 45;
      float th = ((float)b - heading) * PR;
      const char* s = IMU_DIR_SHORT[k * 2];
      int fs = (b % 90) == 0 ? 3 : 2;
      if(R < 110) fs = (b % 90) == 0 ? 2 : 1;
      int lx = cx + (int)(lr * sinf(th) + 0.5f);
      int ly = cy - (int)(lr * cosf(th) + 0.5f);
      drawTextC(lx, ly - uiLineH(fs) / 2, s, fs, (b == 0) ? nCol : TH_TXT2);
    }
  }
  // Aguja del aparato: apunta siempre hacia ARRIBA, porque marca el "delante"
  // del equipo. Cuerpo con acento y cola gris, como una aguja de verdad: un
  // solo triangulo fino parecia un pincho y no se leia como aguja.
  int nl = (int)(R * 0.60f); if(nl < 20) nl = 20;
  int tl = (int)(R * 0.24f); if(tl < 10) tl = 10;
  fillTriangle(cx, cy + tl, cx - 10, cy - 4, cx + 10, cy - 4, TH_TXT2);
  fillTriangle(cx, cy - nl, cx - 12, cy + 6, cx + 12, cy + 6, acc);
  fillCircle(cx, cy, 7, TH_SURF);
  fillRing(cx, cy, 7, 2, acc);
  // Indice fijo, fuera del anillo.
  fillTriangle(cx, cy - R + 2, cx - 8, cy - R - 12, cx + 8, cy - R - 12, acc);
}

// #############################################################
// ##  VISTA 1  ·  REQUISITO DE IMU  (y estado real del sondeo)
// #############################################################
static void cmpDrawSearch(int y0, int y1){
  int vt = cmpVpTop(), vb = cmpVpBot();
  int c0 = vt > y0 ? vt : y0, c1 = vb < y1 ? vb : y1;
  if(c0 > c1) return;
  int ox0 = gClipY0, ox1 = gClipY1;
  gClipY0 = c0; gClipY1 = c1;

  uint8_t st = imuState();
  bool detecting = (st == FIMU_DETECTING);
  int H = vb - vt, x = 30, w = SCR_W - 60;
  int y = vt + (H > 640 ? 44 : 22);

  drawTextC(SCR_W / 2, y, detecting ? "Buscando m\xC3\xB3" "dulo IMU" : "Requiere m\xC3\xB3" "dulo IMU", 4, TH_TXT);
  y += uiLineH(4) + 16;
  const char* l1 = detecting ? "Comprobando el bus I2C en busca de una"
                             : "Flex Compass necesita un m\xC3\xB3" "dulo";
  const char* l2 = detecting ? "IMU compatible. Un momento."
                             : "IMU compatible para determinar";
  const char* l3 = detecting ? "" : "la orientaci\xC3\xB3n y el Norte.";
  drawTextC(SCR_W / 2, y, l1, 2, TH_TXT2); y += uiLineH(2) + 6;
  drawTextC(SCR_W / 2, y, l2, 2, TH_TXT2); y += uiLineH(2) + 6;
  if(l3[0]){ drawTextC(SCR_W / 2, y, l3, 2, TH_TXT2); y += uiLineH(2); }
  y += 22;

  drawTextC(SCR_W / 2, y, "M\xC3\xB3" "dulo compatible", 2, TH_MUTE);
  y += uiLineH(2) + 12;

  // Tarjeta con el modulo dibujado por codigo. Material del sistema.
  int ch = 250;
  if(y + ch > vb - 120) ch = (vb - 120) - y;
  if(ch > 140){
    uiSurface(x, y, w, ch, 26, UIS_CARD);
    cmpDrawModule(SCR_W / 2, y + ch / 2 - 16, w - 90, 0.0f, 0.0f, 0.0f);
    drawTextC(SCR_W / 2, y + ch - 40, "BNO085  \xC2\xB7  9-DOF AHRS", 2, TH_TXT);
    y += ch + 22;
  }
  // Estado REAL del servicio, con el fundido corto del cambio de estado.
  char ln[64];
  snprintf(ln, sizeof(ln), "Estado: %s", cmpStateText());
  uint8_t a = 255;
  if(cmpStateFadeMs){
    uint32_t el = millis() - cmpStateFadeMs;
    a = (el >= CMP_STFADE_MS) ? 255 : (uint8_t)(70 + el * 185 / CMP_STFADE_MS);
  }
  int tw = textW(ln, 3);
  cmpDotA(SCR_W / 2 - tw / 2 - 16, y + uiLineH(3) / 2 - 2, cmpStateColor(), a);
  drawTextCA(SCR_W / 2 + 10, y, ln, 3, TH_TXT, a);

  gClipY0 = ox0; gClipY1 = ox1;
}

// #############################################################
// ##  VISTA 2  ·  BRUJULA + SECCIONES
// #############################################################
static void cmpDrawCompass(int y0, int y1){
  int vt = cmpVpTop(), vb = cmpVpBot();
  int c0 = vt > y0 ? vt : y0, c1 = vb < y1 ? vb : y1;
  if(c0 > c1) return;
  int ox0 = gClipY0, ox1 = gClipY1;
  gClipY0 = c0; gClipY1 = c1;                 // RECORTE EXCLUSIVO del viewport

  int base = vt - (int)cmpScroll;
  int x = 26, w = SCR_W - 52;
  char buf[48];

  // ---- Hero: rosa + lectura ----
  if(base + cmpAnimBot >= c0 && base + cmpAnimTop <= c1){
    cmpDrawRose(SCR_W / 2, base + cmpRoseCY, cmpRoseR, cmpHeadVis);
    cmpFmtHeading(buf, sizeof(buf), cmpHeadVis);
    drawTextC(SCR_W / 2, base + cmpYNum, buf, 5, TH_TXT);
    drawTextC(SCR_W / 2, base + cmpYDir, imuDirShort(cmpHeadVis), 4, wallAccent());
    drawTextC(SCR_W / 2, base + cmpYDirLong, imuDirLong(cmpHeadVis), 2, TH_TXT2);
    // PRECISION: no es una barra decorativa. Los cuatro escalones son el valor
    // que publica el propio BNO085 en cada vector de rotacion.
    const int segW = 22, gap = 5, n = 4;
    const char* alab = cmpAccText(cmpAcc);
    int barW = n * segW + (n - 1) * gap;
    int bx = SCR_W / 2 - (barW + 12 + textW(alab, 2)) / 2;
    for(int i = 0; i < n; i++)
      fillRoundRect(bx + i * (segW + gap), base + cmpYAcc + 6, segW, 8, 4,
                    (i < (int)cmpAcc) ? TH_OK : TH_TRACK);
    drawText(bx + barW + 12, base + cmpYAcc, alab, 2, TH_TXT2);
  }
  // Pista de desplazamiento (se apaga en cuanto el usuario desplaza).
  if(cmpScroll < 6.0f && base + cmpYHint >= c0 - 30 && base + cmpYHint <= c1 + 30){
    int hy = base + cmpYHint;
    drawTextC(SCR_W / 2, hy, "Desliza para ver el sensor", 1, TH_MUTE);
    strokeSegAA(SCR_W / 2 - 8, (float)(hy - 12), SCR_W / 2, (float)(hy - 19), 1.8f, TH_MUTE);
    strokeSegAA(SCR_W / 2,     (float)(hy - 19), SCR_W / 2 + 8, (float)(hy - 12), 1.8f, TH_MUTE);
  }

  // ---- Tarjeta de estado del sensor (Liquid Glass del sistema) ----
  if(base + cmpYCard + cmpHCard >= c0 && base + cmpYCard <= c1){
    int cy0 = base + cmpYCard;
    uiSurface(x, cy0, w, cmpHCard, 24, UIS_CARD);
    cmpRowDot(x + 22, cy0 + 16, w - 44, "BNO085", cmpStateText(), cmpStateColor());
    fillRect(x + 22, cy0 + cmpHCard / 2 + 2, w - 44, 1, TH_DIV);
    bool ahrs = imuAhrsActive();
    cmpRowDot(x + 22, cy0 + cmpHCard / 2 + 14, w - 44, "AHRS",
              ahrs ? "Activo" : "Inactivo", ahrs ? TH_OK : TH_MUTE);
  }

  // ---- Seccion "Sensor IMU" ----
  if(base + cmpYImuTitle + uiLineH(3) >= c0 && base + cmpYImuTitle <= c1)
    drawText(x, base + cmpYImuTitle, "Sensor IMU", 3, TH_TXT);

  if(base + cmpYModule + cmpHModule >= c0 && base + cmpYModule <= c1){
    int my = base + cmpYModule;
    uiSurface(x, my, w, cmpHModule, 24, UIS_CARD);
    // RECORTE A SU TARJETA. Inclinada del todo, la caja proyectada crece: sin
    // esto, una vuelta completa de la placa podria pintar por encima de la
    // lista de sensores. El recorte lo resuelve de una vez y no cuesta nada.
    int q0 = gClipY0, q1 = gClipY1, qx0 = gClipX0, qx1 = gClipX1;
    if(gClipY0 < my + 6) gClipY0 = my + 6;
    if(gClipY1 > my + cmpHModule - 7) gClipY1 = my + cmpHModule - 7;
    if(gClipX0 < x + 6) gClipX0 = x + 6;
    if(gClipX1 > x + w - 7) gClipX1 = x + w - 7;
    // El ancho se acota tambien por el ALTO de la tarjeta: girada, la placa
    // ocupa como mucho 0,71 veces su ancho en vertical.
    int mw = w - 110;
    int byH = (cmpHModule - 24) * 100 / 80;
    if(mw > byH) mw = byH;
    // Orientacion REAL si la hay; con el sensor callado, la vista plana.
    // Yaw = el rumbo REAL (con signo cambiado: girar el aparato a la derecha
    // gira la placa a la izquierda respecto al Norte). Nada escalado ni
    // suavizado a ojo: es el mismo dato que mueve la rosa.
    if(cmpHave) cmpDrawModule(SCR_W / 2, my + cmpHModule / 2, mw,
                              -cmpHeadVis, cmpPitch, cmpRoll);
    else        cmpDrawModule(SCR_W / 2, my + cmpHModule / 2, mw, 0.0f, 0.0f, 0.0f);
    gClipY0 = q0; gClipY1 = q1; gClipX0 = qx0; gClipX1 = qx1;
  }
  if(base + cmpYChecks + 4 * 42 >= c0 && base + cmpYChecks <= c1){
    const char* names[4] = { "Aceler\xC3\xB3metro", "Giroscopio", "Magnet\xC3\xB3metro", "AHRS" };
    const uint8_t srcs[4] = { FLEXBNO_CHK_ACCEL, FLEXBNO_CHK_GYRO, FLEXBNO_CHK_MAG, FLEXBNO_CHK_FUSION };
    for(int i = 0; i < 4; i++){
      int ry = base + cmpYChecks + i * 42;
      if(ry + 42 < c0 || ry > c1) continue;
      bool on = imuSrcLive(srcs[i]);
      drawText(x + 6, ry + 8, names[i], 3, TH_TXT2);
      cmpCheck(x + w - 20, ry + 8 + uiLineH(3) / 2, on);
      if(i < 3) fillRect(x + 6, ry + 40, w - 12, 1, TH_DIV);
    }
  }

  // ---- Seccion "Datos tecnicos" ----
  if(base + cmpYTechTitle + uiLineH(3) >= c0 && base + cmpYTechTitle <= c1)
    drawText(x, base + cmpYTechTitle, "Datos t\xC3\xA9" "cnicos", 3, TH_TXT);

  if(base + cmpYTech + 8 * 38 >= c0 && base + cmpYTech <= c1){
    const char* labels[8] = { "Heading", "Pitch", "Roll", "AHRS",
                              "Magnet\xC3\xB3metro", "Giroscopio", "Aceler\xC3\xB3metro", "Precisi\xC3\xB3n" };
    for(int i = 0; i < 8; i++){
      int ry = base + cmpYTech + i * 38;
      if(ry + 38 < c0 || ry > c1) continue;
      char val[40];
      uint16_t vc = TH_TXT;
      switch(i){
        case 0: if(cmpHave) cmpFmtHeading(val, sizeof(val), cmpHeadVis);
                else { snprintf(val, sizeof(val), "No disponible"); vc = TH_MUTE; } break;
        case 1: if(cmpHave) cmpFmtDeg(val, sizeof(val), cmpPitch, false);
                else { snprintf(val, sizeof(val), "No disponible"); vc = TH_MUTE; } break;
        case 2: if(cmpHave) cmpFmtDeg(val, sizeof(val), cmpRoll, false);
                else { snprintf(val, sizeof(val), "No disponible"); vc = TH_MUTE; } break;
        case 3: { bool on = imuAhrsActive(); snprintf(val, sizeof(val), "%s", on ? "Activo" : "Inactivo"); vc = on ? TH_OK : TH_MUTE; } break;
        case 4: case 5: case 6: {
          uint8_t chk = (i == 4) ? FLEXBNO_CHK_MAG : (i == 5 ? FLEXBNO_CHK_GYRO : FLEXBNO_CHK_ACCEL);
          bool on = imuSrcLive(chk);
          snprintf(val, sizeof(val), "%s", on ? "Activo" : "Inactivo");
          vc = on ? TH_OK : TH_MUTE;
        } break;
        default:
          // PRECISION. Es el nivel 0..3 que declara el PROPIO BNO085 en cada
          // vector de rotacion, no una barra de adorno. El driver no expone la
          // estimacion en radianes del informe, asi que aqui no se inventa un
          // "±X grados" que nadie ha medido.
          if(cmpHave){ snprintf(val, sizeof(val), "%s", cmpAccText(cmpAcc)); }
          else { snprintf(val, sizeof(val), "No disponible"); vc = TH_MUTE; }
          break;
      }
      cmpRow(x + 6, ry + 6, w - 12, labels[i], val, vc, 2);
      if(i < 7) fillRect(x + 6, ry + 34, w - 12, 1, TH_DIV);
    }
  }
  gClipY0 = ox0; gClipY1 = ox1;
}

// #############################################################
// ##  CAPAS: MENU, FICHA DEL SENSOR Y CALIBRACION
// #############################################################
static const char* CMP_MENU_LBL[CMP_MENU_N] = { "Informaci\xC3\xB3n del sensor" };

static void cmpMenuGeom(int &x, int &y, int &w, int &h){
  w = CMP_MENU_W;
  h = CMP_MENU_N * CMP_MENU_RH + 2 * CMP_MENU_PAD;
  x = SCR_W - 10 - w;
  y = UIHDR_ZONE + 4;
}
static void cmpDrawMenu(){
  int x, y, w, h; cmpMenuGeom(x, y, w, h);
  fillRectA(0, cmpVpTop(), SCR_W, cmpVpBot() - cmpVpTop(), TH_SCRIM, 90);
  uiSurface(x, y, w, h, 20, UIS_ELEVATED);
  for(int i = 0; i < CMP_MENU_N; i++){
    int ry = y + CMP_MENU_PAD + i * CMP_MENU_RH;
    drawTextClip(x + 18, ry + (CMP_MENU_RH - uiLineH(2)) / 2, CMP_MENU_LBL[i], 2,
                 TH_TXT, x + w - 16);
    if(i < CMP_MENU_N - 1) fillRect(x + 14, ry + CMP_MENU_RH - 1, w - 28, 1, TH_DIV);
  }
}
static int cmpMenuHit(int px, int py){
  int x, y, w, h; cmpMenuGeom(x, y, w, h);
  if(px < x || px > x + w || py < y || py > y + h) return -1;
  int i = (py - y - CMP_MENU_PAD) / CMP_MENU_RH;
  if(i < 0) i = 0;
  if(i > CMP_MENU_N - 1) i = CMP_MENU_N - 1;
  return i;
}

static void cmpSheetGeom(int &x, int &y, int &w, int &h){
  w = SCR_W - 56; x = 28;
  h = 346;
  y = cmpVpTop() + (cmpVpH() - h) / 2;
  if(y < cmpVpTop() + 8) y = cmpVpTop() + 8;
}
static void cmpDrawInfo(){
  int x, y, w, h; cmpSheetGeom(x, y, w, h);
  fillRectA(0, cmpVpTop(), SCR_W, cmpVpBot() - cmpVpTop(), TH_SCRIM, 175);
  uiSurface(x, y, w, h, 28, UIS_ELEVATED);
  int iy = y + 26;
  drawTextC(SCR_W / 2, iy, "Flex Compass", 4, TH_TXT); iy += uiLineH(4) + 20;
  struct { const char* k; const char* v; } rows[6];
  char fw[40], ad[16];
  // Solo lo que el sensor ha contestado de verdad (reporte 0xF1). Sin
  // identificacion valida se dice "No disponible", no una version inventada.
  if(flexBnoChecks() & FLEXBNO_CHK_I2C)
    snprintf(fw, sizeof(fw), "%u.%u", (unsigned)flexBnoSwMajor(), (unsigned)flexBnoSwMinor());
  else
    snprintf(fw, sizeof(fw), "No disponible");
  if(flexBnoAddr()) snprintf(ad, sizeof(ad), "0x%02X", (unsigned)flexBnoAddr());
  else              snprintf(ad, sizeof(ad), "No disponible");
  rows[0].k = "Sensor";        rows[0].v = "BNO085";
  rows[1].k = "Orientaci\xC3\xB3n"; rows[1].v = "AHRS";
  rows[2].k = "Sensores";      rows[2].v = "9-DOF";
  rows[3].k = "Estado";        rows[3].v = cmpStateText();
  rows[4].k = "Bus I2C";      rows[4].v = ad;
  rows[5].k = "Firmware";      rows[5].v = fw;
  for(int i = 0; i < 6; i++){
    drawText(x + 24, iy, rows[i].k, 2, TH_TXT2);
    drawTextR(x + w - 24, iy, rows[i].v, 2, (i == 3) ? cmpStateColor() : TH_TXT);
    iy += uiLineH(2) + 10;
    if(i < 5){ fillRect(x + 24, iy - 5, w - 48, 1, TH_DIV); }
  }
  int by = y + h - 62;
  uiSurface(x + 24, by, w - 48, 44, 22, UIS_ACCENT);
  drawTextC(SCR_W / 2, by + (44 - uiLineH(2)) / 2, "Cerrar", 2, uiSurfOn(UIS_ACCENT));
}
static bool cmpInfoCloseHit(int px, int py){
  int x, y, w, h; cmpSheetGeom(x, y, w, h);
  if(px < x || px > x + w || py < y || py > y + h) return true;     // fuera: cierra
  int by = y + h - 62;
  return (py >= by && py <= by + 44 && px >= x + 24 && px <= x + w - 24);
}

// #############################################################
// ##  COMPOSICION  ·  siempre en bbuf, siempre por bandas
// #############################################################
static void cmpCompose(int y0, int y1){
  if(y0 < 0) y0 = 0;
  if(y1 > SCR_H - 1) y1 = SCR_H - 1;
  if(y0 > y1) return;
  setBuf(bbuf);
  int ox0 = gClipX0, ox1 = gClipX1, oy0 = gClipY0, oy1 = gClipY1;
  gClipX0 = 0; gClipX1 = SCR_W - 1; gClipY0 = y0; gClipY1 = y1;

  fillRect(0, y0, SCR_W, y1 - y0 + 1, TH_PAGE);
  if(y0 < UIHDR_H) uiHdrDraw("Flex Compass", 4, TH_TXT, TH_NAV, true);

  if(cmpView == CMPV_COMPASS) cmpDrawCompass(y0, y1);
  else                        cmpDrawSearch(y0, y1);

  // FUNDIDO ENTRE VISTAS. Se hace "a traves del color de pagina": la primera
  // mitad oscurece la vista que se va, la segunda revela la que entra. Cuesta
  // un rectangulo con alpha, no una segunda composicion.
  if(cmpXfadeMs){
    uint32_t el = millis() - cmpXfadeMs;
    if(el < CMP_XFADE_MS){
      int half = CMP_XFADE_MS / 2;
      uint32_t a = (el < (uint32_t)half) ? (el * 255 / half) : ((CMP_XFADE_MS - el) * 255 / half);
      if(a > 255) a = 255;
      fillRectA(0, cmpVpTop(), SCR_W, cmpVpBot() - cmpVpTop(), TH_PAGE, (uint8_t)a);
    }
  }
  if(cmpOverlay == CMPO_MENU)      cmpDrawMenu();
  else if(cmpOverlay == CMPO_INFO) cmpDrawInfo();

  gClipX0 = ox0; gClipX1 = ox1; gClipY0 = oy0; gClipY1 = oy1;
  setBuf(fb);
}
static void cmpPresent(int y0, int y1){
  if(y0 < 0) y0 = 0;
  if(y1 > SCR_H - 1) y1 = SCR_H - 1;
  if(y0 > y1) return;
  cmpCompose(y0, y1);
  present(y0, y1);
}
static inline void cmpFull(){ cmpPresent(0, SCR_H - 1); }

// #############################################################
// ##  FISICA  ·  desplazamiento y rumbo visual
// ##  Todo por tiempo real (dt de millis()); ni un delay, ni un bucle
// ##  que no devuelva el control.
// #############################################################
static bool cmpPhysics(uint32_t now){
  float dt = (cmpPhysMs == 0) ? 0.033f : (now - cmpPhysMs) / 1000.0f;
  cmpPhysMs = now;
  if(dt <= 0.0f || dt > 0.25f) dt = 0.033f;
  bool ch = false;
  int maxS = cmpScrollMax();

  if(!T.down && cmpView == CMPV_COMPASS){
    if(fabsf(cmpScrollVel) > 6.0f){
      cmpScroll += cmpScrollVel * dt;
      cmpScrollVel *= powf(0.05f, dt);                 // misma desaceleracion que el resto del sistema
      ch = true;
    } else cmpScrollVel = 0;
    if(cmpScroll < 0){
      cmpScroll += (0.0f - cmpScroll) * (1.0f - powf(0.002f, dt));
      if(cmpScroll > -0.6f) cmpScroll = 0;
      cmpScrollVel = 0; ch = true;
    } else if(cmpScroll > maxS){
      cmpScroll += ((float)maxS - cmpScroll) * (1.0f - powf(0.002f, dt));
      if(cmpScroll < maxS + 0.6f) cmpScroll = (float)maxS;
      cmpScrollVel = 0; ch = true;
    }
  }

  // RUMBO VISUAL. Persigue al del sensor por el camino angular corto: la
  // diferencia se toma con imuAngleDelta, que es lo que hace que 359 -> 0
  // recorra un grado. La constante de tiempo es ~110 ms: suficiente para que
  // no tiemble y poco para que no se note retraso al girar.
  if(cmpHave){
    if(!cmpHeadInit){ cmpHeadVis = cmpHead; cmpHeadInit = true; }
    else {
      float d = imuAngleDelta(cmpHeadVis, cmpHead);
      float k = 1.0f - expf(-dt * 9.0f);
      if(fabsf(d) < 0.05f) cmpHeadVis = cmpHead;
      else                 cmpHeadVis = imuNorm360(cmpHeadVis + d * k);
    }
  } else cmpHeadInit = false;
  return ch;
}

// #############################################################
// ##  TOQUES
// ##  Regla del sistema: TODO estado de gesto se reinicia al soltar y
// ##  al cambiar de capa. Ningun flag puede quedarse atrapado.
// #############################################################
static void cmpResetGesture(){ cmpDrag = false; cmpDragY0 = -1; }
static void cmpStopMotion(){ cmpResetGesture(); cmpScrollVel = 0; }

static void cmpOpenOverlay(uint8_t ov){
  cmpStopMotion();
  cmpOverlay = ov;
  cmpFull();
}
static void cmpCloseOverlay(){
  cmpOverlay = CMPO_NONE;
  cmpStopMotion();
  cmpFull();
}

// Capa propia que "atras" tiene que cerrar antes de salir de la app.
static bool cmpBackLayer(){
  if(cmpOverlay == CMPO_NONE) return false;
  cmpCloseOverlay();
  return true;
}

static void cmpTouch(){
  // Cabecera: las MISMAS zonas que dibuja uiHdrDraw.
  if(T.tap && T.y < UIHDR_ZONE){
    if(uiHdrBackHit(T.x, T.y)){
      if(!cmpBackLayer()) appClose();
      return;
    }
    if(uiHdrMenuHit(T.x, T.y)){
      if(cmpOverlay != CMPO_NONE) cmpCloseOverlay();
      else                        cmpOpenOverlay(CMPO_MENU);
      return;
    }
  }
  // ---- Capas ----
  if(cmpOverlay == CMPO_MENU){
    if(!T.tap) return;
    int i = cmpMenuHit(T.x, T.y);
    if(i < 0){ cmpCloseOverlay(); return; }
    if(i == 0){ cmpOverlay = CMPO_NONE; cmpOpenOverlay(CMPO_INFO); return; }
    return;
  }
  if(cmpOverlay == CMPO_INFO){
    if(T.tap && cmpInfoCloseHit(T.x, T.y)) cmpCloseOverlay();
    return;
  }
  // ---- Desplazamiento del documento (solo en la vista de brujula) ----
  if(T.released) cmpResetGesture();            // SIEMPRE, caiga donde caiga el dedo
  if(cmpView != CMPV_COMPASS) return;

  // El origen del gesto se anota SIEMPRE, tambien si cayo en la cabecera: asi
  // el "cmpDragY0 >= cmpVpTop()" de abajo rechaza de verdad los gestos que
  // empiezan fuera del viewport, en vez de heredar el origen del gesto
  // anterior y arrastrar la lista con un desplazamiento que no le toca.
  if(T.pressed){
    cmpDrag = false;
    cmpDragX0 = T.x; cmpDragY0 = T.y; cmpDragS0 = cmpScroll;
    cmpLastY = T.y; cmpLastMoveMs = millis();
    cmpScrollVel = 0;
  }
  if(T.down && cmpDragY0 >= cmpVpTop()){
    int dy = T.y - cmpDragY0, dx = T.x - cmpDragX0;
    if(!cmpDrag && (abs(dy) > 9 || abs(dx) > 9))
      cmpDrag = (abs(dy) >= abs(dx));          // el eje horizontal es del sistema (cerrar app)
    if(cmpDrag){
      float ns = cmpDragS0 - dy;
      int maxS = cmpScrollMax();
      if(ns < 0)          ns *= 0.42f;         // resistencia al estirar por arriba
      else if(ns > maxS)  ns = maxS + (ns - maxS) * 0.42f;
      uint32_t now = millis();
      if(now > cmpLastMoveMs){
        float v = (float)(cmpLastY - T.y) * 1000.0f / (float)(now - cmpLastMoveMs);
        cmpScrollVel = cmpScrollVel * 0.6f + v * 0.4f;
      }
      cmpLastY = T.y; cmpLastMoveMs = now;
      if((int)ns != (int)cmpScroll) cmpMark(cmpVpTop(), cmpVpBot());
      cmpScroll = ns;
    }
  }
}

// #############################################################
// ##  CICLO DE VIDA
// ##  ----------------------------------------------------------
// ##  abrir   -> adquirir el servicio IMU (arranca deteccion y reports)
// ##  suspender / cerrar -> soltarlo (el servicio APAGA los reports del
// ##  chip y se queda en reposo). El servicio NO se destruye: es global
// ##  y compartido; lo unico que termina es el consumo de esta app.
// #############################################################
static void cmpHoldImu(bool want){
  if(want && !cmpHoldsImu){ imuAcquire(); cmpHoldsImu = true; }
  else if(!want && cmpHoldsImu){ imuRelease(); cmpHoldsImu = false; }
}

// RED DE SEGURIDAD DEL CICLO DE VIDA.
//
// El camino normal (abrir / suspender / cerrar) ya suelta el servicio por sus
// ganchos. Pero hay una via que NO pasa por ellos: el hosting de Modo PC cierra
// la ventana de una app liberando su lienzo sin llamar a su close() -- es asi
// para todas las apps, no solo para esta --, y con la pantalla en exclusiva de
// otro subsistema (OTA, Optimizar) el tick tampoco corre.
//
// En vez de parchear DeX -- que tocaria a todas las apps --, Flex Compass se
// vigila a si misma: si su tick lleva CMP_IDLE_RELEASE_MS sin ejecutarse, nadie
// esta mirando la brujula y se suelta el sensor. El primer tick siguiente
// vuelve a adquirirlo. El peor caso es reanudar la deteccion, no quedarse el
// BNO085 reportando para nadie.
#define CMP_IDLE_RELEASE_MS 3000
static uint32_t cmpTickMs = 0;
static void compassIdleGuard(){
  if(!cmpHoldsImu) return;
  if(cmpTickMs == 0){ cmpTickMs = millis(); return; }
  if(millis() - cmpTickMs >= CMP_IDLE_RELEASE_MS){
    cmpHoldImu(false);
    cmpOverlay = CMPO_NONE;
    cmpTickMs  = 0;
  }
}

static void cmpSyncView(bool animate){
  uint8_t want = cmpHave ? CMPV_COMPASS : CMPV_SEARCH;
  if(want == cmpWant) return;
  cmpWant = want;
  if(!animate){ cmpView = want; cmpXfadeMs = 0; return; }
  cmpXfadeMs = millis();                       // el cambio real ocurre a mitad del fundido
}

static void compassEnter(){
  if(!gRelayout){
    cmpOverlay   = CMPO_NONE;
    cmpScroll    = 0; cmpScrollVel = 0;
    cmpResetGesture();
    cmpHeadInit  = false; cmpHeadVis = 0;
    cmpPhysMs    = 0; cmpAnimMs = 0; cmpModMs = 0; cmpTechMs = 0;
    cmpDrawnHead = -1000.0f; cmpDrawnPitch = -1000.0f; cmpDrawnRoll = -1000.0f;
    cmpDrawnState = 255; cmpStateFadeMs = 0;
    cmpHoldImu(true);
    cmpTickMs = millis();
    cmpSample();
    cmpWant = cmpView = cmpHave ? CMPV_COMPASS : CMPV_SEARCH;
    cmpXfadeMs = 0;
  }
  cmpLayout();
  if(cmpScroll > cmpScrollMax()) cmpScroll = (float)cmpScrollMax();
  cmpFull();
}

static void compassTick(){
  uint32_t now = millis();
  cmpTickMs = now;
  cmpHoldImu(true);                            // idempotente: repone el enganche si la red de seguridad lo solto
  cmpSample();                                 // UNA lectura del servicio para todo el cuadro
  // RECONEXION EN CALIENTE. El driver no reintenta solo desde ABSENT/LOST -- y
  // hace bien: un cable suelto no puede dejar el bucle del sistema sondeando
  // I2C para siempre. Pero con la brujula DELANTE del usuario, enchufar el
  // modulo tiene que notarse sin salir y volver a entrar, asi que se pide un
  // re-sondeo acotado mientras esta pantalla esta a la vista.
  imuRetry(2500);
  cmpDirtyY0 = 1; cmpDirtyY1 = 0;

  // Seguridad de gestos: si el episodio tactil se fue por otra via (gesto de
  // la barra, suspension, cambio de pantalla), aqui no llega el "released".
  if(!T.down && !T.released && cmpDrag) cmpStopMotion();
  cmpTouch();
  if(gState != ST_APP) return;                 // la app se ha cerrado dentro de cmpTouch

  bool moved = cmpPhysics(now);
  if(moved) cmpMark(cmpVpTop(), cmpVpBot());

  // ---- Cambio de vista (con fundido) ----
  cmpSyncView(true);
  if(cmpXfadeMs){
    uint32_t el = now - cmpXfadeMs;
    if(el >= (uint32_t)(CMP_XFADE_MS / 2) && cmpView != cmpWant){
      cmpView = cmpWant;
      if(cmpView == CMPV_COMPASS){ cmpScroll = 0; cmpScrollVel = 0; }
    }
    cmpMark(0, SCR_H - 1);
    if(el >= CMP_XFADE_MS) cmpXfadeMs = 0;
  }

  if(cmpOverlay != CMPO_NONE){
    // Las capas ensenan datos vivos (estado y precision): se refrescan solas,
    // sin tocar el resto de la pantalla mas de lo necesario.
    if(now - cmpTechMs >= CMP_TECH_MS){ cmpTechMs = now; cmpMark(0, SCR_H - 1); }
    if(cmpDirtyY0 <= cmpDirtyY1) cmpPresent(cmpDirtyY0, cmpDirtyY1);
    return;
  }

  if(cmpView == CMPV_SEARCH){
    if(imuState() != cmpDrawnState){
      cmpDrawnState  = imuState();
      cmpStateFadeMs = now;
      cmpMark(0, SCR_H - 1);
    }
    if(cmpStateFadeMs){
      cmpMark(0, SCR_H - 1);
      if(now - cmpStateFadeMs >= CMP_STFADE_MS) cmpStateFadeMs = 0;
    }
    if(cmpDirtyY0 <= cmpDirtyY1) cmpPresent(cmpDirtyY0, cmpDirtyY1);
    return;
  }

  // ---- Brujula: solo se repinta lo que de verdad cambia ----
  int s0, s1;
  if(now - cmpAnimMs >= CMP_ANIM_MS){
    if(fabsf(imuAngleDelta(cmpDrawnHead, cmpHeadVis)) >= 0.05f || cmpDrawnHead < -900.0f){
      if(cmpBandOf(cmpAnimTop, cmpAnimBot, &s0, &s1)){
        cmpMark(s0, s1);
        cmpDrawnHead = cmpHeadVis;
        cmpAnimMs = now;
      } else cmpDrawnHead = cmpHeadVis;       // fuera de pantalla: no cuesta nada
    }
  }
  if(now - cmpModMs >= CMP_MOD_MS){
    if(fabsf(cmpPitch - cmpDrawnPitch) >= 0.4f || fabsf(cmpRoll - cmpDrawnRoll) >= 0.4f){
      if(cmpBandOf(cmpYModule, cmpYModule + cmpHModule, &s0, &s1)){
        cmpMark(s0, s1);
        cmpModMs = now;
      }
      cmpDrawnPitch = cmpPitch; cmpDrawnRoll = cmpRoll;
    }
  }
  if(now - cmpTechMs >= CMP_TECH_MS){
    cmpTechMs = now;
    if(cmpBandOf(cmpYCard, cmpYCard + cmpHCard, &s0, &s1)) cmpMark(s0, s1);
    if(cmpBandOf(cmpYChecks, cmpYTech + 8 * 38, &s0, &s1)) cmpMark(s0, s1);
  }
  if(cmpDirtyY0 <= cmpDirtyY1) cmpPresent(cmpDirtyY0, cmpDirtyY1);
}

static void compassSuspend(){
  // En segundo plano la app no necesita orientacion: se suelta el servicio y
  // el sensor deja de reportar. No queda NADA corriendo por esta app.
  cmpStopMotion();
  cmpPhysMs = 0;
  cmpHoldImu(false);
  cmpTickMs = 0;
}
static void compassResume(){
  cmpHoldImu(true);
  cmpTickMs = millis();
  cmpStopMotion();
  cmpPhysMs = 0;
  cmpHeadInit = false;
  cmpDrawnHead = -1000.0f; cmpDrawnState = 255;
  cmpSample();
  cmpLayout();
  if(cmpScroll > cmpScrollMax()) cmpScroll = (float)cmpScrollMax();
  if(cmpScroll < 0) cmpScroll = 0;
  cmpSyncView(false);
  cmpFull();
}
static void compassClose(){
  cmpOverlay = CMPO_NONE;
  cmpStopMotion();
  cmpHoldImu(false);
  cmpTickMs = 0;
}
