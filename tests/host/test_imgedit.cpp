// #############################################################
//  test_imgedit.cpp  ·  el nucleo del editor de la Galeria
// #############################################################
//
//  El MISMO codigo que va a la placa (FlexOS_ImgEdit) sobre imagenes
//  construidas aqui con marcas de colores. Lo que importa:
//    · sin cambios, la salida es la foto EXACTA (ni un nivel de diferencia);
//    · girar, voltear y recortar llevan cada marca a donde debe, y el
//      recorte se va con el contenido al girar;
//    · deshacer y rehacer recorren exactamente los pasos dados;
//    · cada ajuste mueve la imagen en el sentido que dice su nombre;
//    · trazos, formas y textos quedan pegados a la foto al girarla;
//    · guardar produce un JPEG que el DECODIFICADOR DEL FIRMWARE abre, con
//      el tamano esperado y el mismo contenido que la vista previa;
//    · ninguna reserva se queda viva, y cancelar a mitad no deja nada.
#include "../../FlexOS_Ultra/FlexOS_ImgEdit.h"
#include "../../FlexOS_Ultra/FlexOS_JPEG.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>

static int g_fail = 0, g_run = 0;
#define CHECK(cond, ...) do { g_run++; if(!(cond)){ g_fail++; \
  std::printf("  FALLO %s:%d  ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while(0)

static long g_live = 0;
static void* tA(size_t n){ void* p = std::malloc(n); if(p) g_live++; return p; }
static void tF(void* p){ if(p){ g_live--; std::free(p); } }

// Base de prueba: degradado suave + una marca roja arriba a la izquierda y
// una azul abajo a la derecha (4x4).
static std::vector<uint8_t> makeBase(int W, int H){
  std::vector<uint8_t> b((size_t)W * H * 3);
  for(int y = 0; y < H; y++) for(int x = 0; x < W; x++){
    uint8_t* p = &b[((size_t)y * W + x) * 3];
    p[0] = (uint8_t)(60 + x * 100 / W); p[1] = (uint8_t)(80 + y * 100 / H); p[2] = 120;
  }
  for(int y = 0; y < 4; y++) for(int x = 0; x < 4; x++){
    uint8_t* r = &b[((size_t)y * W + x) * 3]; r[0] = 250; r[1] = 10; r[2] = 10;
    uint8_t* z = &b[((size_t)(H - 1 - y) * W + (W - 1 - x)) * 3]; z[0] = 10; z[1] = 10; z[2] = 250;
  }
  return b;
}
static std::vector<uint8_t> render(FlexImgEdit* e, int w, int h, bool withOverlays = true){
  std::vector<uint8_t> o((size_t)w * h * 3), sc((size_t)w * 8);
  for(int y = 0; y < h; y += 8){
    int rows = h - y < 8 ? h - y : 8;
    flexIeRenderRows(e, w, h, y, rows, &o[(size_t)y * w * 3], withOverlays ? sc.data() : nullptr);
  }
  return o;
}
static const uint8_t* at(const std::vector<uint8_t>& img, int w, int x, int y){ return &img[((size_t)y * w + x) * 3]; }
static bool isRed(const uint8_t* p){ return p[0] > 200 && p[1] < 60 && p[2] < 60; }
static bool isBlue(const uint8_t* p){ return p[2] > 200 && p[0] < 60 && p[1] < 60; }
static double meanLum(const std::vector<uint8_t>& img){
  double s = 0; for(size_t i = 0; i < img.size(); i += 3) s += 0.299 * img[i] + 0.587 * img[i + 1] + 0.114 * img[i + 2];
  return s / (img.size() / 3);
}

// Texto de prueba: cada letra es una caja de hpx/2 de ancho con 1 px de aire.
static int boxText(void*, const char* s, int hpx, int row, int col0, uint8_t* cov, int covW){
  int cw = hpx / 2 + 1, n = (int)std::strlen(s), w = n * cw;
  if(row < 0) return w;
  for(int i = 0; i < covW; i++){
    int c = col0 + i;
    cov[i] = (c >= 0 && c < w && (c % cw) != cw - 1 && row > 0 && row < hpx - 1) ? 255 : 0;
  }
  return w;
}

static void testIdentityAndGeometry(){
  std::printf("-- identidad, giros, volteos y recorte --\n");
  const int W = 64, H = 48;
  auto base = makeBase(W, H);
  static FlexImgEdit e;
  flexIeInit(&e, base.data(), W, H);
  int w, h; flexIeOutSize(&e, &w, &h);
  CHECK(w == W && h == H && flexIeIsIdentity(&e), "sin cambios: mismo tamano (%dx%d)", w, h);
  CHECK(render(&e, W, H) == base, "sin cambios, la salida es la foto EXACTA");

  flexIeRotate(&e, +1);
  flexIeOutSize(&e, &w, &h);
  auto r1 = render(&e, w, h);
  CHECK(w == H && h == W, "girar 90: ancho y alto se cambian (%dx%d)", w, h);
  CHECK(isRed(at(r1, w, w - 2, 1)) && isBlue(at(r1, w, 1, h - 2)), "girar 90 a la derecha: la marca roja pasa arriba a la derecha");
  CHECK(!flexIeIsIdentity(&e), "ya no es la original");
  flexIeRotate(&e, +1);
  auto r2 = render(&e, W, H);
  CHECK(isRed(at(r2, W, W - 2, H - 2)) && isBlue(at(r2, W, 1, 1)), "180: la roja abajo a la derecha");
  flexIeRotate(&e, +2);
  CHECK(render(&e, W, H) == base, "cuatro cuartos de vuelta: la foto exacta otra vez");
  flexIeFlip(&e, true);
  auto fh = render(&e, W, H);
  CHECK(isRed(at(fh, W, W - 2, 1)) && isBlue(at(fh, W, 1, H - 2)), "volteo horizontal");
  flexIeFlip(&e, true);
  flexIeFlip(&e, false);
  auto fv = render(&e, W, H);
  CHECK(isRed(at(fv, W, 1, H - 2)) && isBlue(at(fv, W, W - 2, 1)), "volteo vertical");
  flexIeFlip(&e, false);
  CHECK(render(&e, W, H) == base, "voltear dos veces deja la foto como estaba");

  // Recorte: el cuadrante de arriba a la izquierda (se queda la roja).
  flexIeSetCrop(&e, 0.0f, 0.0f, 0.5f, 0.5f);
  flexIeOutSize(&e, &w, &h);
  auto c = render(&e, w, h);
  CHECK(w == 32 && h == 24 && isRed(at(c, w, 1, 1)), "recortar: 32x24 con la marca roja en su esquina");
  CHECK(std::memcmp(at(c, w, 20, 10), at(base, W, 20, 10), 3) == 0, "y cada pixel es el de la foto, sin reescalar");
  // Girar despues de recortar: el recorte va con el contenido.
  flexIeRotate(&e, -1);
  flexIeOutSize(&e, &w, &h);
  auto cr = render(&e, w, h);
  CHECK(w == 24 && h == 32 && isRed(at(cr, w, 1, h - 2)), "girar un recorte: sigue siendo la misma esquina (24x32)");
  // Redimensionar.
  flexIeSetLongSide(&e, 16);
  flexIeOutSize(&e, &w, &h);
  CHECK(w == 12 && h == 16, "redimensionar el lado largo a 16: 12x16 (%dx%d)", w, h);
  flexIeSetLongSide(&e, 500);
  flexIeOutSize(&e, &w, &h);
  CHECK(w == 24 && h == 32, "nunca se agranda por encima de lo recortado (%dx%d)", w, h);
  // Un recorte imposible se corrige, no rompe nada.
  flexIeSetCrop(&e, 0.7f, 0.7f, 0.7f, 0.7f);
  flexIeOutSize(&e, &w, &h);
  CHECK(w >= 1 && h >= 1, "recorte vacio: minimo garantizado (%dx%d)", w, h);
  render(&e, w, h);
  // Vista previa con otro tamano: mismo aspecto.
  flexIeReset(&e);
  int pw, ph; flexIeFitSize(&e, 40, 40, &pw, &ph);
  CHECK(pw == 40 && ph == 30, "vista previa en 40x40: 40x30 (%dx%d)", pw, ph);
}

static void testHistory(){
  std::printf("-- deshacer y rehacer --\n");
  const int W = 40, H = 30;
  auto base = makeBase(W, H);
  static FlexImgEdit e;
  flexIeInit(&e, base.data(), W, H);
  CHECK(!flexIeCanUndo(&e) && !flexIeCanRedo(&e), "recien abierta: nada que deshacer");
  flexIeRotate(&e, 1);
  flexIeFlip(&e, true);
  flexIeSetCrop(&e, 0.1f, 0.1f, 0.9f, 0.9f);
  CHECK(flexIeCanUndo(&e), "tres pasos");
  flexIeUndo(&e); flexIeUndo(&e); flexIeUndo(&e);
  CHECK(flexIeIsIdentity(&e) && !flexIeCanUndo(&e) && flexIeCanRedo(&e), "deshacer tres veces: la original");
  flexIeRedo(&e); flexIeRedo(&e);
  CHECK(e.st.rot == 1 && e.st.flipH != e.st.flipV && e.st.c0x == 0.0f, "rehacer dos: giro y volteo, sin el recorte");
  flexIeAdjustLive(&e, FLEXIE_ADJ_BRIGHT, 40);
  flexIeCommit(&e);
  CHECK(!flexIeCanRedo(&e), "un cambio nuevo tira lo que se podia rehacer");
  flexIeUndo(&e);
  CHECK(e.st.adj[FLEXIE_ADJ_BRIGHT] == 0, "deshacer el ajuste");
  // Deslizar un ajuste NO crea pasos hasta soltar.
  int cur = e.cur;
  for(int v = 0; v <= 50; v += 5) flexIeAdjustLive(&e, FLEXIE_ADJ_CONTRAST, v);
  CHECK(e.cur == cur, "arrastrar un ajuste no llena el historial");
  flexIeCommit(&e);
  CHECK(e.cur == cur + 1, "soltar: un solo paso");
  // Historial lleno: se olvida lo mas viejo, sin salirse.
  for(int i = 0; i < FLEXIE_HIST_MAX * 2; i++) flexIeRotate(&e, 1);
  int undos = 0; while(flexIeUndo(&e)) undos++;
  CHECK(undos == FLEXIE_HIST_MAX - 1, "con el historial lleno se deshacen %d pasos (%d)", FLEXIE_HIST_MAX - 1, undos);
  // Restablecer tambien se deshace.
  flexIeInit(&e, base.data(), W, H);
  flexIeRotate(&e, 1);
  flexIeReset(&e);
  CHECK(flexIeIsIdentity(&e) && flexIeCanUndo(&e), "restablecer deja la original y se puede deshacer");
  flexIeUndo(&e);
  CHECK(e.st.rot == 1, "deshacer restablecer recupera el giro");
}

static void testColor(){
  std::printf("-- ajustes y filtros --\n");
  const int W = 48, H = 32;
  auto base = makeBase(W, H);
  static FlexImgEdit e;
  flexIeInit(&e, base.data(), W, H);
  double m0 = meanLum(render(&e, W, H));
  flexIeAdjustLive(&e, FLEXIE_ADJ_BRIGHT, 50);
  double mb = meanLum(render(&e, W, H));
  flexIeAdjustLive(&e, FLEXIE_ADJ_BRIGHT, -50);
  double md = meanLum(render(&e, W, H));
  CHECK(mb > m0 + 10 && md < m0 - 10, "brillo: sube y baja (%.1f / %.1f / %.1f)", md, m0, mb);
  flexIeAdjustLive(&e, FLEXIE_ADJ_BRIGHT, 0);
  flexIeAdjustLive(&e, FLEXIE_ADJ_EXPOSURE, 60);
  CHECK(meanLum(render(&e, W, H)) > m0 + 10, "exposicion: mas luz");
  flexIeAdjustLive(&e, FLEXIE_ADJ_EXPOSURE, 0);
  // Contraste: los extremos se separan.
  auto spread = [&](){ auto im = render(&e, W, H); int lo = 255, hi = 0;
    for(int y = 8; y < H - 8; y++) for(int x = 8; x < W - 8; x++){ int g = at(im, W, x, y)[1]; if(g < lo) lo = g; if(g > hi) hi = g; }
    return hi - lo; };
  int s0 = spread();
  flexIeAdjustLive(&e, FLEXIE_ADJ_CONTRAST, 80);
  int s1 = spread();
  CHECK(s1 > s0, "contraste: mas separacion entre claros y oscuros (%d -> %d)", s0, s1);
  flexIeAdjustLive(&e, FLEXIE_ADJ_CONTRAST, 0);
  flexIeAdjustLive(&e, FLEXIE_ADJ_SATUR, -100);
  auto gray = render(&e, W, H);
  bool allGray = true;
  for(int y = 6; y < H - 6; y++) for(int x = 6; x < W - 6; x++){ const uint8_t* p = at(gray, W, x, y); if(abs(p[0] - p[1]) > 2 || abs(p[1] - p[2]) > 2) allGray = false; }
  CHECK(allGray, "saturacion -100: gris");
  flexIeAdjustLive(&e, FLEXIE_ADJ_SATUR, 0);
  flexIeAdjustLive(&e, FLEXIE_ADJ_TEMP, 80);
  auto warm = render(&e, W, H);
  const uint8_t* pw = at(warm, W, 24, 16); const uint8_t* p0 = at(base, W, 24, 16);
  CHECK(pw[0] > p0[0] && pw[2] < p0[2], "temperatura +: mas rojo y menos azul");
  flexIeAdjustLive(&e, FLEXIE_ADJ_TEMP, 0);
  flexIeAdjustLive(&e, FLEXIE_ADJ_SHADOWS, 100);
  CHECK(meanLum(render(&e, W, H)) > m0, "sombras +: levanta lo oscuro");
  flexIeAdjustLive(&e, FLEXIE_ADJ_SHADOWS, 0);
  flexIeAdjustLive(&e, FLEXIE_ADJ_HIGHLIGHTS, -100);
  CHECK(meanLum(render(&e, W, H)) < m0, "luces -: baja lo claro");
  flexIeAdjustLive(&e, FLEXIE_ADJ_HIGHLIGHTS, 0);
  CHECK(render(&e, W, H) == base, "todo a cero otra vez: la foto exacta");
  // Filtros.
  flexIeFilterLive(&e, FLEXIE_FILTER_BW, 100);
  auto bw = render(&e, W, H);
  bool bwOk = true;
  for(size_t i = 0; i < bw.size(); i += 3) if(abs(bw[i] - bw[i + 1]) > 1 || abs(bw[i + 1] - bw[i + 2]) > 1) bwOk = false;
  CHECK(bwOk, "blanco y negro: R = G = B");
  flexIeFilterLive(&e, FLEXIE_FILTER_SEPIA, 100);
  auto sp = render(&e, W, H);
  const uint8_t* q = at(sp, W, 24, 16);
  CHECK(q[0] >= q[1] && q[1] >= q[2], "sepia: rojo >= verde >= azul");
  flexIeFilterLive(&e, FLEXIE_FILTER_SEPIA, 0);
  CHECK(render(&e, W, H) == base, "un filtro al 0 %% no cambia nada");
  for(int f = 0; f < FLEXIE_FILTER_N; f++){ flexIeFilterLive(&e, f, 70); render(&e, W, H); }
  flexIeFilterLive(&e, FLEXIE_FILTER_VINTAGE, 100);
  auto vt = render(&e, W, H);
  CHECK(meanLum(std::vector<uint8_t>(vt.begin(), vt.begin() + W * 3)) < meanLum(std::vector<uint8_t>(vt.begin() + (H / 2) * W * 3, vt.begin() + (H / 2 + 1) * W * 3)) + 5,
        "vintage: los bordes no quedan mas claros que el centro");
  flexIeFilterLive(&e, FLEXIE_FILTER_NONE, 100);
  // Nitidez: un borde suave se endurece; en negativo, se suaviza.
  std::vector<uint8_t> edge((size_t)W * H * 3);
  for(int y = 0; y < H; y++) for(int x = 0; x < W; x++){
    int v = x < 22 ? 60 : (x > 25 ? 190 : 60 + (x - 21) * 26);
    uint8_t* p = &edge[((size_t)y * W + x) * 3]; p[0] = p[1] = p[2] = (uint8_t)v;
  }
  flexIeInit(&e, edge.data(), W, H);
  auto e0 = render(&e, W, H);
  flexIeAdjustLive(&e, FLEXIE_ADJ_SHARP, 100);
  auto e1 = render(&e, W, H);
  flexIeAdjustLive(&e, FLEXIE_ADJ_SHARP, -100);
  auto e2 = render(&e, W, H);
  int g0 = at(e0, W, 22, 10)[0] - at(e0, W, 25, 10)[0], g1 = at(e1, W, 22, 10)[0] - at(e1, W, 25, 10)[0];
  CHECK(at(e1, W, 21, 10)[0] < at(e0, W, 21, 10)[0] && at(e1, W, 26, 10)[0] > at(e0, W, 26, 10)[0],
        "nitidez +: el lado oscuro del borde baja y el claro sube");
  CHECK(at(e2, W, 21, 10)[0] >= at(e0, W, 21, 10)[0] && at(e2, W, 26, 10)[0] <= at(e0, W, 26, 10)[0],
        "nitidez -: el borde se suaviza");
  (void)g0; (void)g1;
}

static void testOverlays(){
  std::printf("-- trazos, formas y textos --\n");
  const int W = 80, H = 60;
  std::vector<uint8_t> base((size_t)W * H * 3, 128);
  static FlexImgEdit e;
  flexIeInit(&e, base.data(), W, H);
  flexIeSetTextFn(&e, boxText, nullptr);
  // Trazo horizontal rojo a media altura.
  int s = flexIeStrokeBegin(&e, 0xFF0000, 0.05f, 0.1f, 0.5f);
  CHECK(s >= 0, "empieza un trazo");
  for(int i = 1; i <= 16; i++) flexIeStrokeAdd(&e, s, 0.1f + i * 0.05f, 0.5f);
  flexIeStrokeEnd(&e, s);
  auto im = render(&e, W, H);
  CHECK(isRed(at(im, W, 40, 30)), "el trazo pinta por donde paso el dedo");
  CHECK(at(im, W, 40, 5)[0] == 128 && at(im, W, 40, 5)[1] == 128, "y nada lejos de el");
  CHECK(render(&e, W, H, false) == base, "sin memoria para coberturas, sin trazos (no se inventan)");
  // Rectangulo relleno azul y elipse verde con borde.
  flexIeAddShape(&e, FLEXIE_OV_RECT, 0x0000FF, 0.02f, true, 0.05f, 0.05f, 0.25f, 0.25f);
  flexIeAddShape(&e, FLEXIE_OV_ELLIPSE, 0x00FF00, 0.03f, false, 0.6f, 0.05f, 0.95f, 0.45f);
  flexIeAddShape(&e, FLEXIE_OV_ARROW, 0xFFFF00, 0.02f, false, 0.1f, 0.9f, 0.9f, 0.7f);
  im = render(&e, W, H);
  CHECK(isBlue(at(im, W, 10, 8)), "rectangulo relleno por dentro");
  const uint8_t* ec = at(im, W, 62, 15);
  CHECK(ec[0] == 128 && ec[1] == 128, "elipse sin relleno: el centro sigue igual");
  bool ring = false;
  for(int x = 47; x <= 52; x++){ const uint8_t* p = at(im, W, x, 15); if(p[1] > 200 && p[0] < 80) ring = true; }
  CHECK(ring, "y su borde esta pintado");
  // Texto.
  int t = flexIeAddText(&e, "HOLA", 0xFFFFFF, 0.2f, 0.3f, 0.62f);
  CHECK(t >= 0, "texto anadido");
  im = render(&e, W, H);
  int white = 0;
  for(int y = 37; y < 49; y++) for(int x = 24; x < 60; x++){ const uint8_t* p = at(im, W, x, y); if(p[0] > 240 && p[1] > 240 && p[2] > 240) white++; }
  CHECK(white > 40, "el texto se ve (%d pixeles)", white);
  // Deshacer quita la ultima figura.
  flexIeUndo(&e);
  im = render(&e, W, H);
  white = 0;
  for(int y = 37; y < 49; y++) for(int x = 24; x < 60; x++){ const uint8_t* p = at(im, W, x, y); if(p[0] > 240 && p[1] > 240 && p[2] > 240) white++; }
  CHECK(white == 0, "deshacer: el texto desaparece");
  // Pegado a la foto: un punto arriba a la izquierda sigue en la misma esquina de la foto al girar.
  flexIeInit(&e, base.data(), W, H);
  int d = flexIeStrokeBegin(&e, 0xFF0000, 0.06f, 0.08f, 0.1f);
  flexIeStrokeEnd(&e, d);
  flexIeRotate(&e, 1);
  int w, h; flexIeOutSize(&e, &w, &h);
  im = render(&e, w, h);
  int rx = -1, ry = -1;
  for(int y = 0; y < h && rx < 0; y++) for(int x = 0; x < w; x++) if(isRed(at(im, w, x, y))){ rx = x; ry = y; break; }
  CHECK(rx > w / 2 && ry < h / 2, "girar 90: el punto va arriba a la derecha con la foto (%d,%d)", rx, ry);
  // Limites: trazos y figuras no desbordan sus tablas.
  flexIeInit(&e, base.data(), W, H);
  int ok = 0;
  for(int i = 0; i < FLEXIE_OV_MAX + 5; i++) if(flexIeAddShape(&e, FLEXIE_OV_LINE, 0, 0.01f, false, 0, 0, 1, 1) >= 0) ok++;
  CHECK(ok == FLEXIE_OV_MAX, "como mucho %d figuras (%d)", FLEXIE_OV_MAX, ok);
  flexIeInit(&e, base.data(), W, H);
  int sk = flexIeStrokeBegin(&e, 0, 0.01f, 0, 0);
  int added = 0;
  for(int i = 0; i < FLEXIE_PTS_MAX + 100; i++) added += flexIeStrokeAdd(&e, sk, (i % 97) / 97.0f, (i % 89) / 89.0f) ? 1 : 0;
  CHECK(e.st.nPts <= FLEXIE_PTS_MAX, "los puntos nunca pasan de su tabla (%u)", (unsigned)e.st.nPts);
  render(&e, W, H);
  // Restablecer, dibujar otra cosa y deshacer: vuelven las figuras de antes
  // tal cual (las tablas no se reutilizan por debajo de lo que el historial ve).
  flexIeInit(&e, base.data(), W, H);
  flexIeAddShape(&e, FLEXIE_OV_RECT, 0x0000FF, 0.02f, true, 0.05f, 0.05f, 0.25f, 0.25f);
  auto before = render(&e, W, H);
  flexIeReset(&e);
  CHECK(flexIeIsIdentity(&e) && render(&e, W, H) == base, "restablecer esconde las figuras");
  flexIeAddShape(&e, FLEXIE_OV_ELLIPSE, 0xFF0000, 0.02f, true, 0.6f, 0.6f, 0.9f, 0.9f);
  CHECK(isRed(at(render(&e, W, H), W, 60, 45)), "una figura nueva despues de restablecer");
  flexIeUndo(&e);
  CHECK(render(&e, W, H) == base, "deshacer la figura nueva");
  flexIeUndo(&e);
  CHECK(render(&e, W, H) == before, "deshacer restablecer: el rectangulo azul de antes, intacto");
  CHECK(!flexIeCanRedo(&e) || flexIeRedo(&e), "rehacer sigue siendo coherente");
  // Deshacer un trazo y dibujar otro reutiliza su sitio sin tocar lo anterior.
  flexIeInit(&e, base.data(), W, H);
  int a1 = flexIeStrokeBegin(&e, 0x0000FF, 0.05f, 0.1f, 0.2f); flexIeStrokeAdd(&e, a1, 0.9f, 0.2f); flexIeStrokeEnd(&e, a1);
  auto one = render(&e, W, H);
  int a2 = flexIeStrokeBegin(&e, 0xFF0000, 0.05f, 0.1f, 0.8f); flexIeStrokeAdd(&e, a2, 0.9f, 0.8f); flexIeStrokeEnd(&e, a2);
  flexIeUndo(&e);
  CHECK(render(&e, W, H) == one, "deshacer el segundo trazo deja el primero igual");
  int a3 = flexIeStrokeBegin(&e, 0x00FF00, 0.05f, 0.5f, 0.1f); flexIeStrokeAdd(&e, a3, 0.5f, 0.9f); flexIeStrokeEnd(&e, a3);
  auto two = render(&e, W, H);
  CHECK(!flexIeCanRedo(&e) && isBlue(at(two, W, 20, 12)) && at(two, W, 40, 30)[1] > 200, "el tercero ocupa su sitio y el primero sigue");
  // Empezar un trazo ya ocupa la tabla: lo que se podia rehacer deja de valer
  // en ese momento, no al soltar el dedo.
  flexIeUndo(&e);
  CHECK(flexIeCanRedo(&e), "hay algo que rehacer");
  int a4 = flexIeStrokeBegin(&e, 0xFFFFFF, 0.05f, 0.2f, 0.5f);
  CHECK(a4 >= 0 && !flexIeCanRedo(&e) && !flexIeRedo(&e), "con un trazo a medias no se puede rehacer lo que pisaria");
  flexIeStrokeEnd(&e, a4);
  // Figuras que se salen por los bordes: sin escribir fuera (ASan).
  flexIeInit(&e, base.data(), W, H);
  flexIeAddShape(&e, FLEXIE_OV_ELLIPSE, 0xFF00FF, 0.5f, true, 0.0f, 0.0f, 1.0f, 1.0f);
  flexIeAddText(&e, "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX", 0xFFFFFF, 0.9f, 0.9f, 0.9f);
  render(&e, W, H); render(&e, 7, 5);
}

struct Mem { std::vector<uint8_t> v; };
static bool outMem(void* c, const uint8_t* d, size_t n){ ((Mem*)c)->v.insert(((Mem*)c)->v.end(), d, d + n); return true; }
static int g_prog = 0, g_last = 0; static bool g_mono = true;
static void onProg(void*, int done, int total){ g_prog++; if(done < g_last || done > total) g_mono = false; g_last = done; }
static int g_stopAt = -1;
static bool onStop(void*){ return g_stopAt >= 0 && g_prog >= g_stopAt; }

static void testSave(){
  std::printf("-- guardar: JPEG que el firmware abre --\n");
  const int W = 320, H = 240;
  auto base = makeBase(W, H);
  static FlexImgEdit e;
  flexIeInit(&e, base.data(), W, H);
  flexIeSetTextFn(&e, boxText, nullptr);
  flexIeSetCrop(&e, 0.1f, 0.1f, 0.9f, 0.8f);
  flexIeRotate(&e, 1);
  flexIeAdjustLive(&e, FLEXIE_ADJ_CONTRAST, 30); flexIeCommit(&e);
  int s = flexIeStrokeBegin(&e, 0x00FF00, 0.02f, 0.2f, 0.2f); flexIeStrokeAdd(&e, s, 0.8f, 0.8f); flexIeStrokeEnd(&e, s);
  int w, h; flexIeOutSize(&e, &w, &h);
  Mem m; g_prog = 0; g_last = 0; g_mono = true; g_stopAt = -1;
  int rc = flexIeSave(&e, 92, outMem, &m, onStop, onProg, nullptr, tA, tF);
  CHECK(rc == FLEXJE_OK && m.v.size() > 500, "guardado (%d, %zu bytes)", rc, m.v.size());
  CHECK(g_prog > 0 && g_mono && g_last == h, "progreso real, creciente, hasta %d filas", h);
  CHECK(g_live == 0, "ninguna reserva viva (%ld)", g_live);
  FlexJpegInfo inf;
  std::vector<uint8_t> dec((size_t)w * h * 3);
  struct D { std::vector<uint8_t>* v; int w; } dc = { &dec, w };
  int r = flexJpegDecode888(m.v.data(), m.v.size(), 0, 0, 0, &inf,
    [](void* u, int y, int ww, const uint8_t* rgb) -> bool { D* d = (D*)u; std::memcpy(&(*d->v)[(size_t)y * d->w * 3], rgb, (size_t)ww * 3); return true; },
    &dc, nullptr, nullptr);
  CHECK(r == FLEXJPG_OK && inf.width == w && inf.height == h, "el decodificador del firmware lo abre: %dx%d (%dx%d)", inf.width, inf.height, w, h);
  auto ref = render(&e, w, h);
  double err = 0;
  for(size_t i = 0; i < ref.size(); i++){ double d = (double)ref[i] - dec[i]; err += d * d; }
  double psnr = 10.0 * std::log10(255.0 * 255.0 / (err / ref.size() + 1e-9));
  CHECK(psnr > 30.0, "y es lo que se veia en la vista previa (PSNR %.1f dB)", psnr);
  // Cancelar a mitad: error de fuente y nada vivo.
  Mem m2; g_prog = 0; g_stopAt = 3;
  rc = flexIeSave(&e, 90, outMem, &m2, onStop, onProg, nullptr, tA, tF);
  CHECK(rc == FLEXJE_ERR_SOURCE && g_live == 0, "cancelar: se para y no queda nada reservado (%d)", rc);
  g_stopAt = -1;
}

static void testProxy(){
  std::printf("-- vista previa rapida (copia reducida) --\n");
  const int W = 400, H = 300;
  auto base = makeBase(W, H);
  static FlexImgEdit e;
  flexIeInit(&e, base.data(), W, H);
  const int PW = 100, PH = 75;
  std::vector<uint8_t> px((size_t)PW * PH * 3);
  flexIeMakeProxy(base.data(), W, H, px.data(), PW, PH);
  // La copia es la media de cajas: el primer pixel es la marca roja (4x4 de 4x4).
  CHECK(isRed(&px[0]) && isBlue(&px[((size_t)(PH - 1) * PW + PW - 1) * 3]), "la copia conserva las marcas de las esquinas");
  auto slow = render(&e, 100, 75);
  flexIeSetProxy(&e, px.data(), PW, PH);
  CHECK(flexIeUsesProxy(&e, 100, 75) && !flexIeUsesProxy(&e, 400, 300), "la copia se usa a tamano de pantalla, nunca a tamano completo");
  auto fast = render(&e, 100, 75);
  double err = 0;
  for(size_t i = 0; i < fast.size(); i++){ double d = (double)fast[i] - slow[i]; err += d * d; }
  double psnr = 10.0 * std::log10(255.0 * 255.0 / (err / fast.size() + 1e-9));
  CHECK(psnr > 30.0, "y se ve igual que leyendo la base (PSNR %.1f dB)", psnr);
  CHECK(render(&e, W, H) == base, "a tamano completo sigue saliendo la foto EXACTA");
  // Un recorte fuerte pide mas detalle del que la copia tiene: vuelve a la base.
  flexIeSetCrop(&e, 0.4f, 0.4f, 0.6f, 0.6f);
  CHECK(!flexIeUsesProxy(&e, 100, 75), "recorte al 20 %%: se lee la base");
  flexIeSetProxy(&e, nullptr, 0, 0);
  CHECK(!flexIeUsesProxy(&e, 10, 10), "sin copia, siempre la base");
  // Soltar la base (memoria) y volver a ponerla: las ediciones siguen ahi.
  flexIeInit(&e, base.data(), W, H);
  flexIeRotate(&e, 1);
  flexIeAdjustLive(&e, FLEXIE_ADJ_BRIGHT, 30); flexIeCommit(&e);
  int w0, h0; flexIeOutSize(&e, &w0, &h0);
  auto before = render(&e, w0, h0);
  flexIeSetBase(&e, nullptr, 0, 0);
  std::vector<uint8_t> none((size_t)w0 * 3, 7);
  flexIeRenderRows(&e, w0, 1, 0, 1, none.data(), nullptr);
  CHECK(none[0] == 7 && flexIeSave(&e, 90, outMem, nullptr, nullptr, nullptr, nullptr, tA, tF) == FLEXJE_ERR_ARG,
        "sin base: ni se pinta ni se guarda (y no revienta)");
  flexIeSetBase(&e, base.data(), W, H);
  CHECK(flexIeCanUndo(&e) && e.st.rot == 1 && render(&e, w0, h0) == before, "vuelve la base: mismo estado, misma imagen, mismo historial");
}

static void testDecodeScale(){
  std::printf("-- lo que cabe en PSRAM --\n");
  CHECK(flexIeDecodeScale(1600, 1200, 16u << 20) == 1, "1600x1200: entera");
  CHECK(flexIeDecodeScale(4000, 3000, 16u << 20) == 2, "12 MP: a la mitad (2000x1500)");
  CHECK(flexIeDecodeScale(4000, 3000, 6u << 20) == 4, "con 6 MB libres: a un cuarto");
  CHECK(flexIeDecodeScale(8000, 6000, 32u << 20) == 4, "48 MP: a un cuarto");
  CHECK(flexIeDecodeScale(4000, 3000, 100000) == 0, "sin memoria: no se abre (0)");
}

int main(){
  std::printf("=== FlexOS_ImgEdit ===\n");
  testIdentityAndGeometry();
  testHistory();
  testColor();
  testOverlays();
  testSave();
  testProxy();
  testDecodeScale();
  std::printf("\n%d comprobaciones, %d fallos\n", g_run, g_fail);
  return g_fail ? 1 : 0;
}
