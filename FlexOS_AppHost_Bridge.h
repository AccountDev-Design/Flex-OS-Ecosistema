#ifndef FLEXOS_APPHOST_BRIDGE_H
#define FLEXOS_APPHOST_BRIDGE_H
// #############################################################
//  FLEX OS ULTRA · PUENTE DEL RUNTIME flex-app-v1
//  ------------------------------------------------------------
//  Este archivo es la UNICA frontera entre una app descargable y el
//  hardware. Se incluye al final de FlexOS_Ultra.ino, cuando las
//  primitivas graficas, el tactil, el tema y el sistema de archivos
//  ya existen -- misma regla que el puente del OTA o el de Flex Store.
//
//  LO QUE HACE:
//    · reserva de UNA vez las regiones de la app en PSRAM y las suelta
//      al cerrarla (nunca hay una reserva por cuadro);
//    · rellena la vtable FlexAppHostApi con las primitivas reales;
//    · dibuja la cabecera, el area de la app y los avisos de estado;
//    · traduce el tactil del sistema a eventos de la app;
//    · aplica orientacion horizontal y pantalla exclusiva, y las
//      RESTAURA siempre que la app se va, salga por donde salga.
//
//  LO QUE NO HACE, A PROPOSITO:
//    · no le da a la app ni un puntero al framebuffer;
//    · no crea tareas: la app corre dentro de storeTick(), en la misma
//      tarea de interfaz que el resto del sistema;
//    · no toca el watchdog: lo sigue alimentando loop().
// #############################################################

// Area de la app en VERTICAL. En horizontal la app ocupa el lienzo
// logico entero (800x480), igual que hace la app Juegos.
#define APPV1_TOP     88
#define APPV1_BOTTOM  (SCR_H - 64)

// Reservas de PSRAM que una app puede pedir con system.psram.measure.
#define APPV1_PSRAM_SLOTS   FLEXAPP_PSRAM_SLOTS
#define APPV1_PSRAM_MAX_KB  8192u

static FlexAppManager  av1Mgr;
static FlexAppLaunch   av1Launch;
static FlexPkgInfo     av1Info;
static bool            av1Open = false;
static char            av1Title[48] = "";
static char            av1Status[FLEXAPP_REASON_MAX] = "";
static uint32_t        av1LastFrameUs = 0;
static uint32_t        av1FrameStartUs = 0;
static bool            av1NeedFlush = false;

// Regiones de la app. Se reservan al abrir y se sueltan al cerrar; entre
// medias no se reserva ni un byte mas.
static uint8_t*     av1Mem = nullptr;
static int32_t*     av1Globals = nullptr;
static int32_t*     av1Stack = nullptr;
static int32_t*     av1Locals = nullptr;
static FlexVmFrame* av1Frames = nullptr;
static uint8_t*     av1Scratch = nullptr;
static uint8_t*     av1Image = nullptr;
static uint32_t     av1ImageLen = 0;
static uint8_t      av1Grant[FLEXGRANT_BYTES];
static uint32_t     av1GrantLen = 0;

// Reservas de PSRAM pedidas por la app (system.psram.measure).
static uint8_t*  av1Res[APPV1_PSRAM_SLOTS] = { nullptr, nullptr, nullptr, nullptr };
static uint32_t  av1ResBytes[APPV1_PSRAM_SLOTS] = { 0, 0, 0, 0 };

// Estado de la pantalla ANTES de que la app la tocara, para restaurarlo.
static bool av1PrevLand = false;
static bool av1Exclusive = false;

static void av1Close(FlexAppStopReason reason);

// -------------------------------------------------------------
//  Geometria del lienzo de la app
// -------------------------------------------------------------
static inline bool av1Landscape(){ return av1Mgr.landscapeApplied != 0; }
static inline int  av1OriginX(){ return 0; }
static inline int  av1OriginY(){ return (av1Landscape() || av1Exclusive) ? 0 : APPV1_TOP; }
static inline int  av1CanvasW(){ return av1Landscape() ? SCR_H : SCR_W; }
static inline int  av1CanvasH(){
  if(av1Landscape()) return SCR_W;
  return av1Exclusive ? SCR_H : (APPV1_BOTTOM - APPV1_TOP);
}

// Recorte del area de la app. En VERTICAL acota filas y columnas fisicas.
// En HORIZONTAL las primitivas rotan (putPhys) y gClipY0/gClipY1 acotan la
// x LOGICA; por eso ahi el recorte por y no se puede expresar con la banda
// y se resuelve acotando las coordenadas de cada primitiva.
static void av1ClipToApp(){
  if(av1Landscape()){ gClipY0 = 0; gClipY1 = SCR_H - 1; gClipX0 = 0; gClipX1 = SCR_W - 1; return; }
  gClipY0 = av1OriginY();
  gClipY1 = av1OriginY() + av1CanvasH() - 1;
  gClipX0 = 0;
  gClipX1 = SCR_W - 1;
}
static void av1ClipReset(){
  gClipY0 = 0; gClipY1 = SCR_H - 1; gClipX0 = 0; gClipX1 = SCR_W - 1;
}

// Recorta un rectangulo de la app al lienzo. Devuelve false si no queda nada.
static bool av1ClampRect(int& x, int& y, int& w, int& h){
  if(w <= 0 || h <= 0) return false;
  int cw = av1CanvasW(), ch = av1CanvasH();
  if(x < 0){ w += x; x = 0; }
  if(y < 0){ h += y; y = 0; }
  if(x + w > cw) w = cw - x;
  if(y + h > ch) h = ch - y;
  return w > 0 && h > 0;
}

// -------------------------------------------------------------
//  Vtable: graficos
// -------------------------------------------------------------
static uint64_t av1Now(void*){ return (uint64_t)micros(); }

static void av1GfxClear(void*, uint16_t color){
  fillRect(av1OriginX(), av1OriginY(), av1CanvasW(), av1CanvasH(), color);
}
static void av1GfxFillRect(void*, int x, int y, int w, int h, uint16_t color, int alpha){
  if(!av1ClampRect(x, y, w, h)) return;
  if(alpha >= 255) fillRect(av1OriginX() + x, av1OriginY() + y, w, h, color);
  else if(alpha > 0) fillRectA(av1OriginX() + x, av1OriginY() + y, w, h, color, (uint8_t)alpha);
}
static void av1GfxRect(void*, int x, int y, int w, int h, uint16_t color){
  if(w <= 0 || h <= 0) return;
  int bx = x, by = y, bw = w, bh = h;
  if(!av1ClampRect(bx, by, bw, bh)) return;
  drawRect(av1OriginX() + x, av1OriginY() + y, w, h, color);
}
static void av1GfxLine(void*, int x0, int y0, int x1, int y1, uint16_t color){
  // Bresenham con el pixel ya acotado al lienzo de la app: una linea de la
  // app no puede pintar ni un pixel fuera de su area.
  int cw = av1CanvasW(), ch = av1CanvasH();
  int dx = x1 - x0, dy = y1 - y0;
  int adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
  int steps = adx > ady ? adx : ady;
  if(steps < 0) return;
  if(steps > 2048) steps = 2048;                 // techo duro por comando
  for(int i = 0; i <= steps; i++){
    int px_ = steps ? x0 + (int)((int64_t)dx * i / steps) : x0;
    int py_ = steps ? y0 + (int)((int64_t)dy * i / steps) : y0;
    if(px_ < 0 || py_ < 0 || px_ >= cw || py_ >= ch) continue;
    px(av1OriginX() + px_, av1OriginY() + py_, color);
  }
}
static void av1GfxRound(void*, int x, int y, int w, int h, int r, uint16_t color){
  int bx = x, by = y, bw = w, bh = h;
  if(!av1ClampRect(bx, by, bw, bh)) return;
  if(r < 0) r = 0;
  int maxr = (w < h ? w : h) / 2;
  if(r > maxr) r = maxr;
  fillRoundRect(av1OriginX() + x, av1OriginY() + y, w, h, r, color);
}
static void av1GfxPixel(void*, int x, int y, uint16_t color){
  if(x < 0 || y < 0 || x >= av1CanvasW() || y >= av1CanvasH()) return;
  px(av1OriginX() + x, av1OriginY() + y, color);
}
static void av1GfxText(void*, const char* text, int x, int y, int size, uint16_t color){
  if(!text || !text[0]) return;
  if(y < -32 || y >= av1CanvasH()) return;
  drawText(av1OriginX() + x, av1OriginY() + y, text, size, color);
}
static int av1GfxTextW(void*, const char* text, int size){
  return text ? textW(text, size) : 0;
}
static void av1GfxClip(void*, int x, int y, int w, int h){
  if(w < 0 || h < 0){ av1ClipToApp(); return; }     // reset
  if(av1Landscape()){
    // En horizontal la banda de recorte acota la x LOGICA.
    int x0 = x < 0 ? 0 : x, x1 = x + w - 1;
    if(x1 > SCR_H - 1) x1 = SCR_H - 1;
    if(x1 < x0) return;
    gClipY0 = x0; gClipY1 = x1;
    return;
  }
  int y0 = av1OriginY() + (y < 0 ? 0 : y);
  int y1 = av1OriginY() + y + h - 1;
  int top = av1OriginY(), bot = av1OriginY() + av1CanvasH() - 1;
  if(y0 < top) y0 = top;
  if(y1 > bot) y1 = bot;
  if(y1 < y0) return;
  gClipY0 = y0; gClipY1 = y1;
  int x0 = x < 0 ? 0 : x, x1 = x + w - 1;
  if(x1 > SCR_W - 1) x1 = SCR_W - 1;
  if(x1 >= x0){ gClipX0 = x0; gClipX1 = x1; }
}

// Sprite RGB565 desde la memoria lineal de la app. El gestor ya comprobo que
// w*h*2 cabe entero dentro de esa memoria; aqui solo se recorta al lienzo.
static void av1GfxBlit(void*, const uint8_t* pxs, int w, int h, int x, int y,
                       int dw, int dh, int quadrant, int alpha){
  if(!pxs || w <= 0 || h <= 0 || dw <= 0 || dh <= 0) return;
  int cw = av1CanvasW(), ch = av1CanvasH();
  int outW = (quadrant & 1) ? dh : dw;
  int outH = (quadrant & 1) ? dw : dh;
  for(int oy = 0; oy < outH; oy++){
    int ty = y + oy;
    if(ty < 0 || ty >= ch) continue;
    for(int ox = 0; ox < outW; ox++){
      int tx = x + ox;
      if(tx < 0 || tx >= cw) continue;
      // Coordenada en el destino SIN rotar.
      int rx = ox, ry = oy;
      switch(quadrant & 3){
        case 1: rx = oy;              ry = dh - 1 - ox; break;   //  90
        case 2: rx = dw - 1 - ox;     ry = dh - 1 - oy; break;   // 180
        case 3: rx = dw - 1 - oy;     ry = ox;          break;   // 270
        default: break;
      }
      if(rx < 0 || ry < 0 || rx >= dw || ry >= dh) continue;
      int sx = (int)((int64_t)rx * w / dw);       // escalado por vecino mas cercano
      int sy = (int)((int64_t)ry * h / dh);
      if(sx < 0) sx = 0; if(sx >= w) sx = w - 1;
      if(sy < 0) sy = 0; if(sy >= h) sy = h - 1;
      size_t off = ((size_t)sy * (size_t)w + (size_t)sx) * 2u;
      uint16_t c = (uint16_t)pxs[off] | ((uint16_t)pxs[off + 1] << 8);
      if(alpha >= 255) px(av1OriginX() + tx, av1OriginY() + ty, c);
      else if(alpha > 0) pxA(av1OriginX() + tx, av1OriginY() + ty, c, (uint8_t)alpha);
    }
  }
}

static void av1GfxPresent(void*){ av1NeedFlush = true; }
static int  av1CanvasWFn(void*){ return av1CanvasW(); }
static int  av1CanvasHFn(void*){ return av1CanvasH(); }

// Componentes con el tema del sistema: la app pide "una tarjeta" y la pinta
// Flex OS, asi que una app de terceros se ve como el resto del sistema.
static void av1Widget(void*, int kind, const char* text, int x, int y, int w, int h, int state){
  int ox = av1OriginX(), oy = av1OriginY();
  switch(kind){
    case 0: {   // tarjeta
      int bx = x, by = y, bw = w, bh = h;
      if(!av1ClampRect(bx, by, bw, bh)) return;
      fillRoundRect(ox + x, oy + y, w, h, 18, thCard());
      break;
    }
    case 1: drawText(ox + x, oy + y, text, 3, TH_TXT); break;
    case 2: drawText(ox + x, oy + y, text, 2, TH_TXT); break;
    case 3: drawText(ox + x, oy + y, text, 1, TH_TXT2); break;
    case 4: {   // boton
      int bx = x, by = y, bw = w, bh = h;
      if(!av1ClampRect(bx, by, bw, bh)) return;
      uint16_t bg = state ? thCard2() : TH_PRIM;
      fillRoundRect(ox + x, oy + y, w, h, h / 2, bg);
      drawTextC(ox + x + w / 2, oy + y + h / 2 - 8, text, 2, rgb565(255, 255, 255));
      break;
    }
    case 5: {   // fila de lista
      int bx = x, by = y, bw = w, bh = h;
      if(!av1ClampRect(bx, by, bw, bh)) return;
      fillRoundRect(ox + x, oy + y, w, h, 14, state ? thCard2() : thCard());
      drawText(ox + x + 16, oy + y + h / 2 - 8, text, 2, TH_TXT);
      break;
    }
    default: break;
  }
}
static void av1NavTitle(void*, const char* text){
  snprintf(av1Title, sizeof(av1Title), "%s", text ? text : "");
}

// -------------------------------------------------------------
//  Vtable: sistema
// -------------------------------------------------------------
static void av1Log(void*, const char* text){
  // Con enfriamiento: una app no puede inundar el puerto serie.
  static uint32_t last = 0;
  uint32_t now = millis();
  if(now - last < 200) return;
  last = now;
  Serial.printf("[APP %s] %s\n", av1Info.id, text ? text : "");
}

static bool av1Notify(void*, const char* title, const char* sub){
  // La notificacion sigue pidiendo el permiso de MANIFEST de siempre, el
  // mismo que ya usaba flex-ui-1. Se reutiliza la isla existente: no se crea
  // una segunda capa de avisos.
  if(!(av1Info.permissions & FLEXPERM_NOTIFICATIONS)) return false;
  sysNotify(av1Info.name, sub && sub[0] ? sub : title);
  return true;
}

static int av1OsInfo(void*, int which, char* out, int cap){
  const char* s = (which == 0) ? FLEXOS_FW_VERSION : "Flex OS Ultra (ESP32-P4)";
  int n = (int)strlen(s);
  if(n > cap) n = cap;
  if(n > 0) memcpy(out, s, (size_t)n);
  return n;
}

static uint32_t av1Caps(void*){
  uint32_t c = FLEXAPP_CAP_LANDSCAPE | FLEXAPP_CAP_NOTIFICATIONS;
  if(heap_caps_get_total_size(MALLOC_CAP_SPIRAM) > 0) c |= FLEXAPP_CAP_PSRAM;
  if(flexFsReady()) c |= FLEXAPP_CAP_STORAGE;
  if(gtOk) c |= FLEXAPP_CAP_MULTITOUCH;
  // FLEXAPP_CAP_TEMP_SENSOR se queda a 0 mientras el firmware no tenga una
  // lectura REAL habilitada (ver av1Temp). Un bit a 0 significa "este equipo
  // no lo tiene", no "no lo hemos mirado".
  return c;
}
static int av1ScreenW(void*){ return SCR_W; }
static int av1ScreenH(void*){ return SCR_H; }

// La orientacion la aplica el SISTEMA, no la app: aqui solo se mueve el
// interruptor del motor grafico. Quien la restaura es av1Close/av1Suspend,
// pase lo que pase con la app.
static bool av1SetLandscape(void*, bool on){ gLand = on; return true; }
static bool av1SetExclusive(void*, bool on){
  av1Exclusive = on;
  return true;
}

// -------------------------------------------------------------
//  Vtable: almacenamiento privado
// -------------------------------------------------------------
static bool av1DataPath(const char* name, char* out, size_t cap){
  char dir[FLEXPKG_ID_MAX + 32];
  if(!flexPkgDataDir(av1Info.id, dir, sizeof(dir))) return false;
  int n = snprintf(out, cap, "%s/%s", dir, name);
  return n > 0 && (size_t)n < cap;
}
static int av1StoreWrite(void*, const char* name, const uint8_t* data, uint32_t len){
  char path[FLEXPKG_ID_MAX + 96];
  if(!flexPkgDataEnsure(av1Info.id) || !av1DataPath(name, path, sizeof(path))) return 0;
  return flexFsWriteBinAtomic(path, data, len) ? 1 : 0;
}
static int av1StoreRead(void*, const char* name, uint8_t* out, uint32_t cap){
  char path[FLEXPKG_ID_MAX + 96];
  if(!av1DataPath(name, path, sizeof(path))) return -1;
  return flexFsReadBin(path, out, cap);
}
static int av1StoreSize(void*, const char* name){
  char path[FLEXPKG_ID_MAX + 96];
  if(!av1DataPath(name, path, sizeof(path)) || !flexFsExists(path)) return -1;
  return (int)flexFsSize(path);
}
static int av1StoreDelete(void*, const char* name){
  char path[FLEXPKG_ID_MAX + 96];
  if(!av1DataPath(name, path, sizeof(path))) return 0;
  return flexFsDelete(path) ? 1 : 0;
}
static uint32_t av1StoreUsed(void*){ return flexPkgDataBytes(av1Info.id); }

// -------------------------------------------------------------
//  Vtable: entrada
// -------------------------------------------------------------
static int av1TouchPoints(void*, FlexAppPoint* out, int maxn){
  int n = gtPollMulti();
  if(n < 0) return -1;                  // el panel no dio un dato fiable
  if(n > maxn) n = maxn;
  int k = 0;
  for(int i = 0; i < n && i < KB_MAXPOINTS; i++){
    if(!gKbPoints[i].active) continue;
    int lx = gKbPoints[i].x, ly = gKbPoints[i].y;
    if(av1Landscape()){
      // Mismo remapeo que usan las apps horizontales del sistema.
      out[k].x = ly;
      out[k].y = (SCR_W - 1) - lx;
    } else {
      out[k].x = lx;
      out[k].y = ly - av1OriginY();
    }
    out[k].id = gKbPoints[i].id;
    k++;
  }
  return k;
}

// -------------------------------------------------------------
//  Vtable: servicios privilegiados
// -------------------------------------------------------------
static int32_t av1Perf(void*, int32_t field){
  switch(field){
    case FLEXAPP_PERF_LOOP_RATE:    return (int32_t)gLoopRate;
    case FLEXAPP_PERF_CPU_MHZ:      return (int32_t)getCpuFrequencyMhz();
    case FLEXAPP_PERF_HEAP_FREE_KB: return (int32_t)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024u);
    case FLEXAPP_PERF_UPTIME_S:     return (int32_t)(millis() / 1000u);
    case FLEXAPP_PERF_FRAME_US:     return (int32_t)av1LastFrameUs;
    default:                        return FLEXAPP_NO_VALUE;
  }
}

static int32_t av1PsramKb(void*, int which){
  size_t total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
  if(total == 0) return FLEXAPP_NO_VALUE;     // esta placa no tiene PSRAM
  switch(which){
    case 0: return (int32_t)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024u);
    case 1: return (int32_t)(total / 1024u);
    case 2: return (int32_t)(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) / 1024u);
    default: return FLEXAPP_NO_VALUE;
  }
}

static int32_t av1PsramReserve(void*, uint32_t kb){
  // NUNCA se intenta reservar toda la PSRAM: ademas del techo por app que ya
  // aplica el gestor, aqui se consulta el presupuesto REAL del sistema. Si no
  // cabe con holgura, se devuelve 0 y la app tiene que saber vivir con eso.
  if(kb == 0 || kb > APPV1_PSRAM_MAX_KB) return 0;
  size_t want = (size_t)kb * 1024u;
  size_t freeNow = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  if(freeNow < want + (2u * 1024u * 1024u)) return 0;   // 2 MB de colchon para el sistema
  int slot = -1;
  for(int i = 0; i < APPV1_PSRAM_SLOTS; i++) if(!av1Res[i]){ slot = i; break; }
  if(slot < 0) return 0;
  uint8_t* p = (uint8_t*)heap_caps_malloc(want, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if(!p) return 0;
  memset(p, 0, want);
  av1Res[slot] = p;
  av1ResBytes[slot] = (uint32_t)want;
  return slot + 1;                            // handle = slot + 1 (nunca 0)
}

static bool av1PsramRelease(void*, int32_t handle){
  int slot = handle - 1;
  if(slot < 0 || slot >= APPV1_PSRAM_SLOTS || !av1Res[slot]) return false;
  heap_caps_free(av1Res[slot]);
  av1Res[slot] = nullptr;
  av1ResBytes[slot] = 0;
  return true;
}

static bool av1PsramPoke(void*, int32_t handle, uint32_t off, int32_t value){
  int slot = handle - 1;
  if(slot < 0 || slot >= APPV1_PSRAM_SLOTS || !av1Res[slot]) return false;
  if((uint64_t)off * 4u + 4u > (uint64_t)av1ResBytes[slot]) return false;
  memcpy(av1Res[slot] + (size_t)off * 4u, &value, 4);
  return true;
}
static bool av1PsramPeek(void*, int32_t handle, uint32_t off, int32_t* out){
  int slot = handle - 1;
  if(slot < 0 || slot >= APPV1_PSRAM_SLOTS || !av1Res[slot] || !out) return false;
  if((uint64_t)off * 4u + 4u > (uint64_t)av1ResBytes[slot]) return false;
  memcpy(out, av1Res[slot] + (size_t)off * 4u, 4);
  return true;
}

// TEMPERATURA. No se inventa. Este firmware NO tiene todavia una lectura
// verificada del sensor interno del ESP32-P4, asi que el servicio contesta
// "no disponible" y la app tiene que ensenar eso mismo.
//
// Para habilitarla hay que (a) compilar con -DFLEXOS_TEMP_SENSOR=1, (b) tener
// el driver del SDK, y (c) COMPROBAR la lectura en placa contra una medida
// externa. Hasta que ese tercer paso ocurra, la rama sigue desactivada: un
// numero sin verificar es peor que un "No disponible".
#if defined(FLEXOS_TEMP_SENSOR) && FLEXOS_TEMP_SENSOR && defined(__has_include)
#  if __has_include(<driver/temperature_sensor.h>)
#    include <driver/temperature_sensor.h>
#    define AV1_HAS_TEMP 1
#  endif
#endif
#ifndef AV1_HAS_TEMP
#  define AV1_HAS_TEMP 0
#endif

static int32_t av1Temp(void*){
#if AV1_HAS_TEMP
  static temperature_sensor_handle_t h = nullptr;
  if(!h){
    temperature_sensor_config_t cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
    if(temperature_sensor_install(&cfg, &h) != ESP_OK) return FLEXAPP_NO_VALUE;
    if(temperature_sensor_enable(h) != ESP_OK) return FLEXAPP_NO_VALUE;
  }
  float c = 0.0f;
  if(temperature_sensor_get_celsius(h, &c) != ESP_OK) return FLEXAPP_NO_VALUE;
  return (int32_t)(c * 1000.0f);
#else
  return FLEXAPP_NO_VALUE;
#endif
}

// CARGA DE CPU POR RODAJAS. Trabajo REAL y acotado por tiempo: entra, hace
// aritmetica hasta agotar su plazo y sale. Entre rodaja y rodaja manda el
// gestor, asi que la interfaz sigue respondiendo y la carga se puede cancelar
// en cualquier momento. No hay bucles infinitos ni watchdog tocado.
static int32_t av1Stress(void*, int32_t kind, uint32_t budgetUs, uint64_t* acc){
  if(budgetUs > 4000u) budgetUs = 4000u;         // techo duro por rodaja
  uint32_t t0 = (uint32_t)micros();
  uint64_t work = 0;
  // `volatile` para que el compilador NO borre el trabajo: la rodaja tiene
  // que costar de verdad, o la medida que salga de aqui no valdria nada.
  static volatile uint32_t sink = 0;
  static uint8_t scratch[512];
  while((uint32_t)micros() - t0 < budgetUs){
    // Un bloque pequeno y se vuelve a mirar el reloj: el plazo no se puede
    // pasar por mucho, y entre rodaja y rodaja manda el sistema.
    if(kind == 1){
      // Memoria: escribir y RELEER, para que cuente el acceso, no solo la ALU.
      uint32_t s2 = 0;
      for(int i = 0; i < 64; i++){
        size_t at = (size_t)((i * 7 + (int)work) & 511);
        scratch[at] = (uint8_t)(work + (uint64_t)i);
        s2 += scratch[(at + 251) & 511];
      }
      sink = sink + s2;
      work += 64;
    } else if(kind == 2){
      float f = 1.0f + (float)(work & 1023);
      for(int i = 0; i < 64; i++) f = f * 1.000001f + 0.5f;
      sink = sink + (uint32_t)f;
      work += 64;
    } else {
      uint32_t v = (uint32_t)work + sink;
      for(int i = 0; i < 64; i++) v = v * 1664525u + 1013904223u;
      sink = v;
      work += 64;
    }
  }
  if(acc) *acc += work;
  return (int32_t)(work > 0x7FFFFFFF ? 0x7FFFFFFF : work);
}

// -------------------------------------------------------------
//  Vtable completa
// -------------------------------------------------------------
static const FlexAppHostApi AV1_API = {
  av1Now,
  av1GfxClear, av1GfxFillRect, av1GfxRect, av1GfxLine, av1GfxRound, av1GfxPixel,
  av1GfxText, av1GfxTextW, av1GfxClip, av1GfxBlit, av1GfxPresent,
  av1CanvasWFn, av1CanvasHFn,
  av1Widget, av1NavTitle,
  av1Log, av1Notify, av1OsInfo, av1Caps, av1ScreenW, av1ScreenH,
  av1SetLandscape, av1SetExclusive,
  av1StoreWrite, av1StoreRead, av1StoreSize, av1StoreDelete, av1StoreUsed,
  av1TouchPoints,
  av1Perf, av1PsramKb, av1PsramReserve, av1PsramRelease, av1PsramPoke, av1PsramPeek,
  av1Temp, av1Stress
};

// -------------------------------------------------------------
//  Reserva y liberacion de las regiones
// -------------------------------------------------------------
static void av1FreeRegions(){
  for(int i = 0; i < APPV1_PSRAM_SLOTS; i++){
    if(av1Res[i]){ heap_caps_free(av1Res[i]); av1Res[i] = nullptr; av1ResBytes[i] = 0; }
  }
  if(av1Mem){ heap_caps_free(av1Mem); av1Mem = nullptr; }
  if(av1Globals){ heap_caps_free(av1Globals); av1Globals = nullptr; }
  if(av1Stack){ heap_caps_free(av1Stack); av1Stack = nullptr; }
  if(av1Locals){ heap_caps_free(av1Locals); av1Locals = nullptr; }
  if(av1Frames){ heap_caps_free(av1Frames); av1Frames = nullptr; }
  if(av1Scratch){ heap_caps_free(av1Scratch); av1Scratch = nullptr; }
  if(av1Image){ heap_caps_free(av1Image); av1Image = nullptr; }
  av1ImageLen = 0;
  av1GrantLen = 0;
}

static void* av1Alloc(size_t bytes){
  if(bytes == 0) return heap_caps_malloc(4, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  void* p = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if(p) memset(p, 0, bytes);
  return p;
}

// -------------------------------------------------------------
//  Apertura
// -------------------------------------------------------------
static bool av1Start(const char* packageId){
  if(av1Open) av1Close(FLEXAPP_STOP_SYSTEM);
  av1FreeRegions();
  memset(&av1Info, 0, sizeof(av1Info));
  av1Status[0] = 0;
  av1Exclusive = false;
  av1PrevLand = gLand;

  if(!flexPkgGet(packageId, &av1Info)){
    snprintf(av1Status, sizeof(av1Status), "%s", "La aplicacion ya no esta instalada");
    return false;
  }
  if(av1Info.runtime != FLEXPKG_RT_APP1){
    snprintf(av1Status, sizeof(av1Status), "%s", "Esta app no usa el runtime flex-app-v1");
    return false;
  }
  if(av1Info.state != FLEXPKG_APP_ENABLED){
    snprintf(av1Status, sizeof(av1Status), "%s",
             av1Info.state == FLEXPKG_APP_BLOCKED ? "Detenida por el sistema" : "Detenida por el usuario");
    return false;
  }

  // Bytecode entero en PSRAM. La maquina guarda punteros a esta imagen, asi
  // que vive exactamente lo que vive la app.
  char entry[400];
  if(!flexPkgEntryPath(packageId, entry, sizeof(entry))){
    snprintf(av1Status, sizeof(av1Status), "%s", "No se encuentra el codigo de la app");
    return false;
  }
  uint32_t codeBytes = flexFsSize(entry);
  if(codeBytes < 48 || codeBytes > (uint32_t)av1Info.memoryKB * 1024u + FLEXVM_MAX_CODE){
    snprintf(av1Status, sizeof(av1Status), "%s", "El codigo de la app tiene un tamano invalido");
    return false;
  }
  av1Image = (uint8_t*)av1Alloc(codeBytes);
  if(!av1Image || flexFsReadBin(entry, av1Image, codeBytes) != (int)codeBytes){
    av1FreeRegions();
    snprintf(av1Status, sizeof(av1Status), "%s", "No se pudo leer el codigo de la app");
    return false;
  }
  av1ImageLen = codeBytes;

  av1GrantLen = flexPkgGrant(packageId, av1Grant, sizeof(av1Grant));

  uint32_t memBytes = (uint32_t)av1Info.memoryKB * 1024u;
  av1Mem     = (uint8_t*)av1Alloc(memBytes);
  av1Globals = (int32_t*)av1Alloc(FLEXVM_MAX_GLOBALS * sizeof(int32_t));
  av1Stack   = (int32_t*)av1Alloc(FLEXVM_MAX_STACK * sizeof(int32_t));
  av1Locals  = (int32_t*)av1Alloc(FLEXVM_MAX_FRAMESLOTS * sizeof(int32_t));
  av1Frames  = (FlexVmFrame*)av1Alloc(FLEXVM_MAX_CALLDEPTH * sizeof(FlexVmFrame));
  av1Scratch = (uint8_t*)av1Alloc((codeBytes + 7u) / 8u + 16u);
  if(!av1Mem || !av1Globals || !av1Stack || !av1Locals || !av1Frames || !av1Scratch){
    av1FreeRegions();
    snprintf(av1Status, sizeof(av1Status), "%s", "No hay memoria para abrir la aplicacion");
    return false;
  }

  memset(&av1Launch, 0, sizeof(av1Launch));
  av1Launch.packageId = av1Info.id;
  av1Launch.versionName = av1Info.versionName;
  av1Launch.versionCode = av1Info.versionCode;
  flexPkgCoreHexToBytes(av1Info.packageSha256, av1Launch.packageSha256, 32);
  flexPkgCoreHexToBytes(av1Info.developerKeySha256, av1Launch.developerKeySha256, 32);
  av1Launch.manifestPermissions = av1Info.systemPermissions;
  // Sin hora de red fiable se pasa 0: el grant se comprueba igual, pero su
  // ventana temporal NO se aplica (y queda anotado). Nunca al reves.
  // gNtpLastSyncUtc solo es distinto de 0 cuando ALGUNA sincronizacion real
  // (red o la guardada en NVS) fijo la hora. La semilla de fabrica no cuenta.
  av1Launch.nowEpoch = gNtpLastSyncUtc ? (uint64_t)clkNowUtc() : 0ull;
  av1Launch.image = av1Image;
  av1Launch.imageLen = av1ImageLen;
  av1Launch.grant = av1GrantLen ? av1Grant : nullptr;
  av1Launch.grantLen = av1GrantLen;
  av1Launch.trustedPub = FLEX_STORE_PUBLIC_KEY;
  av1Launch.mem = av1Mem;             av1Launch.memBytes = memBytes;
  av1Launch.globals = av1Globals;     av1Launch.globalCount = FLEXVM_MAX_GLOBALS;
  av1Launch.stack = av1Stack;         av1Launch.stackSlots = FLEXVM_MAX_STACK;
  av1Launch.locals = av1Locals;       av1Launch.frameSlots = FLEXVM_MAX_FRAMESLOTS;
  av1Launch.frames = av1Frames;       av1Launch.callDepth = FLEXVM_MAX_CALLDEPTH;
  av1Launch.scratch = av1Scratch;     av1Launch.scratchLen = (codeBytes + 7u) / 8u + 16u;
  av1Launch.instrPerTick = av1Info.instrPerTick;
  av1Launch.usPerTick = av1Info.usPerTick;
  av1Launch.drawPerFrame = av1Info.drawPerFrame;
  av1Launch.storageQuota = (uint32_t)av1Info.storageKB * 1024u;

  snprintf(av1Title, sizeof(av1Title), "%s", av1Info.name);
  flexAppInit(&av1Mgr, &AV1_API, nullptr);
  if(!flexAppStart(&av1Mgr, &av1Launch)){
    snprintf(av1Status, sizeof(av1Status), "%s", flexAppReason(&av1Mgr));
    gLand = av1PrevLand;
    av1FreeRegions();
    return false;
  }
  av1Open = true;
  av1NeedFlush = true;
  return true;
}

// -------------------------------------------------------------
//  Cierre
// -------------------------------------------------------------
static void av1Close(FlexAppStopReason reason){
  if(!av1Open){ av1FreeRegions(); return; }
  flexAppStop(&av1Mgr, reason);
  // El gestor ya pidio devolver orientacion y exclusiva; aqui se REAFIRMA
  // sobre el estado real del sistema: salir de una app nunca puede dejar el
  // panel girado ni el escritorio sin pantalla.
  gLand = av1PrevLand;
  av1Exclusive = false;
  av1ClipReset();
  // Si el sistema tuvo que pararla por seguridad, queda anotado en el
  // registro de la app para que Flex Store lo pueda enseñar.
  if(av1Mgr.state == FLEXAPP_ST_STOPPED_SECURITY)
    flexPkgSetState(av1Info.id, FLEXPKG_APP_BLOCKED);
  snprintf(av1Status, sizeof(av1Status), "%s", flexAppReason(&av1Mgr));
  av1FreeRegions();
  av1Open = false;
}

// -------------------------------------------------------------
//  Dibujo del marco
// -------------------------------------------------------------
static void av1DrawChrome(){
  if(av1Landscape() || av1Exclusive) return;     // la app tiene toda la pantalla
  fillRect(0, 0, SCR_W, APPV1_TOP, thCard());
  // Flecha de "atras" (mismo glifo que el resto del sistema).
  uint16_t col = TH_TXT;
  for(int i = 0; i < 14; i++){
    hLine(26 + i, 30 + 14 - i, 2, col);
    hLine(26 + i, 30 + 14 + i, 2, col);
  }
  hLine(26, 30 + 14, 24, col);
  drawText(66, 28, av1Title, 3, TH_TXT);
  if(av1Mgr.granted){
    // Marca discreta: esta app tiene permisos de sistema concedidos.
    drawTextR(SCR_W - 20, 34, "permisos", 1, TH_MUTE);
  }
}

static void av1DrawStopped(){
  setBuf(fb);
  fillRect(0, 0, SCR_W, SCR_H, TH_WIN);
  fillRect(0, 0, SCR_W, APPV1_TOP, thCard());
  drawText(28, 28, av1Title[0] ? av1Title : "Aplicacion", 3, TH_TXT);
  drawTextC(SCR_W / 2, SCR_H / 2 - 40, flexAppStateName(av1Mgr.state), 2, TH_TXT);
  drawTextC(SCR_W / 2, SCR_H / 2, av1Status[0] ? av1Status : flexAppReason(&av1Mgr), 1, TH_TXT2);
  drawTextC(SCR_W / 2, SCR_H / 2 + 40, "Toca para volver", 1, TH_MUTE);
  flxFlushAll();
}

// -------------------------------------------------------------
//  Tick: un cuadro de la app
// -------------------------------------------------------------
static bool av1Active(){ return av1Open; }

static void av1Tick(){
  if(!av1Open) return;
  if(flexAppState(&av1Mgr) != FLEXAPP_ST_RUNNING){
    av1DrawStopped();
    if(T.tap){ av1Close(FLEXAPP_STOP_USER); }
    return;
  }

  // 1) Tactil del sistema -> eventos de la app.
  if(T.pressed) flexAppPostEvent(&av1Mgr, FLEXAPP_EV_TOUCH_DOWN, T.x, T.y - av1OriginY());
  if(T.moved)   flexAppPostEvent(&av1Mgr, FLEXAPP_EV_TOUCH_MOVE, T.x, T.y - av1OriginY());
  if(T.released)flexAppPostEvent(&av1Mgr, FLEXAPP_EV_TOUCH_UP,   T.x, T.y - av1OriginY());
  if(T.tap){
    // En vertical, la franja de la cabecera es del SISTEMA: ahi el toque
    // cierra la app y no llega nunca a la app.
    if(!av1Landscape() && !av1Exclusive && T.y < APPV1_TOP && T.x < 100){
      av1Close(FLEXAPP_STOP_USER);
      return;
    }
    flexAppPostEvent(&av1Mgr, FLEXAPP_EV_TOUCH_TAP, T.x, T.y - av1OriginY());
  }

  // 2) Un cuadro, con el recorte puesto y el reloj en marcha.
  av1FrameStartUs = (uint32_t)micros();
  setBuf(fb);
  av1ClipToApp();
  flexAppTick(&av1Mgr);
  av1ClipReset();
  av1LastFrameUs = (uint32_t)micros() - av1FrameStartUs;

  // 3) Publicar. Solo se vuelca cuando la app lo pide (gfx.present) o cuando
  //    el sistema tiene que repintar su marco.
  if(flexAppWantsPresent(&av1Mgr) || av1NeedFlush){
    av1DrawChrome();
    flxFlushAll();
    av1NeedFlush = false;
  }

  // 4) ¿Se cerro sola o la paro el sistema?
  FlexAppState st = flexAppState(&av1Mgr);
  if(st == FLEXAPP_ST_STOPPED || st == FLEXAPP_ST_ERROR || st == FLEXAPP_ST_STOPPED_SECURITY){
    if(st == FLEXAPP_ST_STOPPED) av1Close(FLEXAPP_STOP_USER);
    else {
      snprintf(av1Status, sizeof(av1Status), "%s", flexAppReason(&av1Mgr));
      gLand = av1PrevLand;
      av1Exclusive = false;
      if(st == FLEXAPP_ST_STOPPED_SECURITY) flexPkgSetState(av1Info.id, FLEXPKG_APP_BLOCKED);
      av1DrawStopped();
    }
  }
}

// El boton "atras" del sistema. Primero se le ofrece a la app; si no hay
// nadie escuchando, cierra.
static bool av1Back(){
  if(!av1Open) return false;
  if(flexAppState(&av1Mgr) == FLEXAPP_ST_RUNNING &&
     flexVmHasHandler(&av1Mgr.vm, FLEXVM_H_EVENT)){
    flexAppPostEvent(&av1Mgr, FLEXAPP_EV_BACK, 0, 0);
    return true;
  }
  av1Close(FLEXAPP_STOP_USER);
  return true;
}

static void av1Suspend(){ if(av1Open){ flexAppSuspend(&av1Mgr); gLand = av1PrevLand; av1ClipReset(); } }
static void av1Resume(){  if(av1Open){ flexAppResume(&av1Mgr); av1NeedFlush = true; } }

#endif
