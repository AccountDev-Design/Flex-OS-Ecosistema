// #############################################################
// ##  FLEX OS ULTRA  ·  MOTOR GRAFICO NATIVO 480x800
// ##  ----------------------------------------------------------
// ##  Framebuffers en PSRAM, flush MIPI-DSI sincronizado y todas las
// ##  primitivas de dibujo (rectangulos, redondeos, lineas, mezcla alfa,
// ##  recorte gClipY0/gClipY1, present() por bandas).
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
#include "FlexOS_Ultra_HAL.h"   // eslabon anterior de la cadena

// #############################################################
// ##  MOTOR GRAFICO NATIVO 480x800  (original de FlexOS)
// ##  Framebuffers en PSRAM + flush DMA2D sincronizado + primitivas
// #############################################################

// Tres capas en PSRAM (la placa tiene de sobra):
//   fb       -> cuadro logico que flxFlush sube al panel
//   lockBuf  -> pantalla de bloqueo pre-renderizada (swipe fluido)
//   homeBuf  -> escritorio pre-renderizado
static uint16_t* fb      = NULL;
static uint16_t* bbuf    = NULL;   // back buffer: se compone el frame aqui y se vuelca de una vez (anti-flicker)
static uint16_t* lockBuf = NULL;
static uint16_t* homeBuf = NULL;

// Destino de dibujo actual (todas las primitivas escriben aqui)
static uint16_t* gBuf = NULL;

// ---- Redireccion del destino de render (hosting de apps en Modo PC) ----
// Toda app hace setBuf(fb) y termina con flxFlush(): esta cableada a la
// pantalla. Para poder ejecutar una app DE VERDAD dentro de una ventana de DeX
// hace falta desviarla a un lienzo propio sin tocar ni una linea de las 16 apps.
// Con gRtTarget != NULL:
//   · setBuf(fb) va al lienzo de la ventana (cualquier otro buffer se respeta),
//   · flxFlush/present no vuelcan nada al panel, solo anotan que hubo dibujo,
//   · fbCopyBand escribe en el lienzo en vez de en fb.
// Asi la app cree que pinta a pantalla completa y en realidad esta pintando su
// propio 480x800 fuera de pantalla, que luego DeX escala dentro del marco.
static uint16_t* gRtTarget = NULL;
static bool      gRtDirty  = false;   // la app pidio volcar algo desde el ultimo reset
static inline void setBuf(uint16_t* b){ gBuf = (gRtTarget && b == fb) ? gRtTarget : b; }

// Banda de recorte vertical (para listas con scroll). Por defecto: toda la pantalla.
static int gClipY0 = 0, gClipY1 = SCR_H - 1;
static int gClipX0 = 0, gClipX1 = SCR_W - 1;   // recorte horizontal
// PRESENTACION SIN HILO PARALELO.
//
// El panel DPI refresca continuamente su framebuffer interno. La version
// anterior lanzaba esp_lcd_panel_draw_bitmap() desde una segunda tarea y
// protegia fb con un mutex. Eso no alcanzaba: la mayor parte de las primitivas
// dibuja directamente en fb, fuera del mutex, y podia empezar el cuadro N+1
// mientras DMA2D todavia copiaba el N. El resultado real en la placa eran
// iconos dobles, franjas de dos paginas y, si draw_bitmap quedaba esperando
// mientras retenia el mutex, loopTask bloqueada en portMAX_DELAY hasta TASK_WDT.
//
// Ahora hay UN solo propietario del pipeline: la tarea de UI. flxFlush() inicia
// la transferencia y espera el callback on_color_trans_done ANTES de devolver;
// es el mismo contrato de "flush ready" del ejemplo MIPI-DSI de Espressif. La
// espera duerme la tarea (no gira ni mata al idle task) y esta acotada: nunca
// existe una espera infinita dentro del compositor.
static bool     flxFlushFault = false;
static uint32_t flxFlushFaultMs = 0;

// Ya no hay un lector de fb en paralelo. Se conservan estos dos wrappers porque
// varias capturas de overlays delimitan con ellos una lectura coherente; son
// no-op deliberados y evitan mantener un mutex que pueda volver a introducir
// una espera circular en el futuro.
static inline void fbLock(){}
static inline void fbUnlock(){}

// FASE 4 del Modo Kiosco: se define mucho mas abajo (necesita las primitivas de
// dibujo), pero se declara aqui porque flxFlush -el unico punto por el que TODO
// acaba llegando al panel- tiene que llamarla antes de publicar la banda.
static void kioskStampBadge(int y0, int y1);
// Barra de navegacion del sistema: se estampa por el MISMO camino y por el
// mismo motivo que el candado del kiosco (ver el bloque de navegacion).
static void navStampBar(int y0, int y1);

static void flxFlush(int y0, int y1){
  if(gRtTarget){ gRtDirty = true; return; }   // app hospedada: no toca el panel
  if(y0 < 0) y0 = 0; if(y1 >= SCR_H) y1 = SCR_H - 1;
  if(y0 > y1) return;
  // El candado del kiosco se estampa ANTES de transferir la banda: ninguna
  // actualizacion llega al panel sin el, aunque la app repinte esa esquina.
  kioskStampBadge(y0, y1);
  navStampBar(y0, y1);
  if(!flxPanel || !flxDpiSem || !fb) return;

  // Un token viejo haria que la espera siguiente terminase ANTES que la DMA
  // actual. Se drena siempre justo antes de iniciar una transferencia.
  (void)xSemaphoreTake(flxDpiSem, 0);    // binario: como maximo puede haber un token viejo
  esp_err_t rc = esp_lcd_panel_draw_bitmap(flxPanel, 0, y0, SCR_W, y1 + 1,
                                            fb + (size_t)y0 * SCR_W);
  if(rc != ESP_OK){
    uint32_t now = millis();
    if(!flxFlushFault || now - flxFlushFaultMs > 2000)
      Serial.println(F("[GFX] draw_bitmap fallo; cuadro descartado"));
    flxFlushFault = true; flxFlushFaultMs = now;
    return;
  }
  // 120 ms son mas de siete periodos a 60 Hz. Si no llega el callback hay un
  // fallo del driver/panel; se sale en vez de retener un mutex para siempre.
  if(xSemaphoreTake(flxDpiSem, pdMS_TO_TICKS(120)) != pdTRUE){
    uint32_t now = millis();
    if(!flxFlushFault || now - flxFlushFaultMs > 2000)
      Serial.println(F("[GFX] timeout esperando DMA2D; compositor liberado"));
    flxFlushFault = true; flxFlushFaultMs = now;
    return;
  }
  flxFlushFault = false;
}
static inline void flxFlushAll(){ flxFlush(0, SCR_H - 1); }
// Vuelca la banda [y0,y1] del back buffer a fb de una sola pasada.
// Componer en bbuf y presentar asi evita publicar cuadros a medias.
// Copia la banda [y0,y1] de src a fb de una sola pasada por fila completa.
static void fbCopyBand(const uint16_t* src, int y0, int y1){
  if(!src) return;
  if(y0 < 0) y0 = 0;
  if(y1 >= SCR_H) y1 = SCR_H - 1;
  if(y0 > y1) return;
  uint16_t* dst = gRtTarget ? gRtTarget : fb;      // app hospedada: a su lienzo
  if(dst == src) return;
  memcpy(dst + (size_t)y0 * SCR_W, src + (size_t)y0 * SCR_W, (size_t)(y1 - y0 + 1) * SCR_W * 2);
}

static void present(int y0, int y1){
  if(y0 < 0) y0 = 0; if(y1 >= SCR_H) y1 = SCR_H - 1; if(y0 > y1) return;
  fbCopyBand(bbuf, y0, y1);
  flxFlush(y0, y1);
}

static bool flxGfxInit(){
  size_t bytes = (size_t)SCR_W * SCR_H * 2;
  // Alineados a 64 bytes = tamano de linea de cache de la PSRAM del P4.
  // flxFlush vuelca BANDAS parciales (fb + y0*SCR_W) a la DMA2D, que
  // exige un write-back de cache limpio del origen antes de leer. Si fb
  // no arranca en una frontera de 64B, el offset y0*SCR_W de una banda
  // puede caer a mitad de linea de cache: el write-back deja sin
  // sincronizar el principio de esa fila y la DMA2D lee PSRAM vieja ahi
  // -> esa fila sale desplazada/con basura, y como esto se repite en
  // cada flush con distinto y0, el resultado visual es una costura
  // diagonal de pixeles de colores erraticos. Cada fila aqui son
  // 480*2=960 bytes (multiplo exacto de 64), asi que con el buffer
  // alineado TODOS los offsets de fila quedan tambien alineados.
  fb      = (uint16_t*)heap_caps_aligned_alloc(64, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  bbuf    = (uint16_t*)heap_caps_aligned_alloc(64, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  lockBuf = (uint16_t*)heap_caps_aligned_alloc(64, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  homeBuf = (uint16_t*)heap_caps_aligned_alloc(64, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if(!fb || !bbuf || !lockBuf || !homeBuf){
    Serial.println(F("[GFX] ERROR: sin PSRAM para framebuffers"));
    return false;
  }
  memset(fb, 0, bytes);
  setBuf(fb);
  // Primer volcado en negro
  (void)xSemaphoreTake(flxDpiSem, 0);
  if(esp_lcd_panel_draw_bitmap(flxPanel, 0, 0, SCR_W, SCR_H, fb) != ESP_OK){
    Serial.println(F("[GFX] ERROR: fallo el primer flush DMA2D"));
    return false;
  }
  if(xSemaphoreTake(flxDpiSem, pdMS_TO_TICKS(200)) != pdTRUE)
    Serial.println(F("[GFX] aviso: primer flush sin callback DMA2D"));
  return true;
}

// ---------------- Color ----------------
static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b){
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
// Division exacta por 255 SIN instruccion de division, para v en [0, 65534].
// Identidad clasica: v/255 == (v + 1 + (v>>8)) >> 8 en ese rango.
// Aqui el numerador maximo es 63*255 = 16065 (canal verde, 6 bits), muy por
// debajo del limite, asi que el resultado es BIT A BIT identico al de /255.
// Motivo: la division entera en el RISC-V del P4 cuesta decenas de ciclos y
// mix565 se ejecuta por PIXEL en cada alpha, cada panel de vidrio y cada blur.
#define DIV255(v)  (uint16_t)(((v) + 1u + ((v) >> 8)) >> 8)

// mezcla a<-b con peso t (0..255). t=0 => a, t=255 => b
static inline uint16_t mix565(uint16_t a, uint16_t b, uint8_t t){
  uint32_t ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
  uint32_t br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
  uint32_t it = 255u - t;
  uint32_t rr = ar * it + br * t;
  uint32_t rg = ag * it + bg * t;
  uint32_t rb = ab * it + bb * t;
  return (uint16_t)((DIV255(rr) << 11) | (DIV255(rg) << 5) | DIV255(rb));
}

// ---------------- Utilidades ----------------
// Raiz entera por restas binarias: SIN division (la version anterior hacia
// una division dentro del bucle de Newton). Se llama una vez por FILA de cada
// rectangulo redondeado, circulo y panel de vidrio. Resultado identico.
static inline int isqrt32(int v){
  if(v <= 0) return 0;
  uint32_t op = (uint32_t)v, res = 0, one = 1u << 30;
  while(one > op) one >>= 2;
  while(one){
    if(op >= res + one){ op -= res + one; res += one << 1; }
    res >>= 1; one >>= 2;
  }
  return (int)res;
}

// ---------------- Primitivas (escriben en gBuf) ----------------
// Rotacion landscape (Modo PC). Cuando gLand=true, las coords logicas
// (lx 0..799, ly 0..479) se escriben rotadas 90 sobre el panel portrait.
static bool gLand = false;
static inline void putPhys(int lx, int ly, uint16_t c){
  if((unsigned)lx >= SCR_H || (unsigned)ly >= SCR_W) return;
  int x = (SCR_W - 1) - ly, y = lx;
  if(y < gClipY0 || y > gClipY1) return;
  gBuf[(size_t)y * SCR_W + x] = c;
}
static inline void putPhysA(int lx, int ly, uint16_t c, uint8_t a){
  if((unsigned)lx >= SCR_H || (unsigned)ly >= SCR_W) return;
  int x = (SCR_W - 1) - ly, y = lx;
  if(y < gClipY0 || y > gClipY1) return;
  if(a >= 255){ gBuf[(size_t)y * SCR_W + x] = c; return; }
  if(a == 0) return;
  size_t i = (size_t)y * SCR_W + x; gBuf[i] = mix565(gBuf[i], c, a);
}
static inline void px(int x, int y, uint16_t c){
  if(gLand){ putPhys(x, y, c); return; }
  if((unsigned)x >= SCR_W || (unsigned)y >= SCR_H) return;
  if(y < gClipY0 || y > gClipY1 || x < gClipX0 || x > gClipX1) return;
  gBuf[(size_t)y * SCR_W + x] = c;
}
// pixel con alpha (0..255) sobre lo que ya hay en gBuf
static inline void pxA(int x, int y, uint16_t c, uint8_t a){
  if(gLand){ putPhysA(x, y, c, a); return; }
  if((unsigned)x >= SCR_W || (unsigned)y >= SCR_H) return;
  if(y < gClipY0 || y > gClipY1 || x < gClipX0 || x > gClipX1) return;
  if(a >= 255){ gBuf[(size_t)y * SCR_W + x] = c; return; }
  if(a == 0) return;
  size_t i = (size_t)y * SCR_W + x;
  gBuf[i] = mix565(gBuf[i], c, a);
}
static void hLine(int x, int y, int w, uint16_t c){
  if(w <= 0) return;
  if(gLand){ for(int i = 0; i < w; i++) putPhys(x + i, y, c); return; }
  if((unsigned)y >= SCR_H) return;
  if(y < gClipY0 || y > gClipY1) return;
  if(x < 0){ w += x; x = 0; }
  if(x + w > SCR_W) w = SCR_W - x;
  if(x < gClipX0){ w -= (gClipX0 - x); x = gClipX0; }     // recorte horizontal
  if(x + w > gClipX1 + 1) w = gClipX1 + 1 - x;
  if(w <= 0) return;
  uint16_t* p = gBuf + (size_t)y * SCR_W + x;
  for(int i = 0; i < w; i++) p[i] = c;
}
// Igual que hLine pero mezclando. Antes llamaba a pxA por pixel, lo que repetia
// TODOS los recortes (bordes + banda vertical + viewport) en cada pixel. Ahora
// recorta UNA vez y mezcla en linea recta: mismas reglas de recorte que hLine,
// mismo resultado, sin el coste por pixel. fillRectA/fillCircleA/fillRoundRectA
// cuelgan de aqui, asi que esto abarata todo el relleno translucido del sistema.
static void hLineA(int x, int y, int w, uint16_t c, uint8_t a){
  if(a >= 255){ hLine(x, y, w, c); return; }
  if(a == 0 || w <= 0) return;
  if(gLand){ for(int i = 0; i < w; i++) pxA(x + i, y, c, a); return; }   // landscape: ruta original
  if((unsigned)y >= SCR_H) return;
  if(y < gClipY0 || y > gClipY1) return;
  if(x < 0){ w += x; x = 0; }
  if(x + w > SCR_W) w = SCR_W - x;
  if(x < gClipX0){ w -= (gClipX0 - x); x = gClipX0; }
  if(x + w > gClipX1 + 1) w = gClipX1 + 1 - x;
  if(w <= 0) return;
  uint16_t* p = gBuf + (size_t)y * SCR_W + x;
  for(int i = 0; i < w; i++) p[i] = mix565(p[i], c, a);
}
static void vLine(int x, int y, int h, uint16_t c){
  if(h <= 0) return;
  if(gLand){ for(int i = 0; i < h; i++) putPhys(x, y + i, c); return; }
  if((unsigned)x >= SCR_W || x < gClipX0 || x > gClipX1) return;   // recorte horizontal
  if(y < 0){ h += y; y = 0; }
  if(y < gClipY0){ h -= (gClipY0 - y); y = gClipY0; }
  if(y + h > SCR_H) h = SCR_H - y;
  if(y + h > gClipY1 + 1) h = gClipY1 + 1 - y;
  if(h <= 0) return;
  uint16_t* p = gBuf + (size_t)y * SCR_W + x;
  for(int i = 0; i < h; i++){ *p = c; p += SCR_W; }
}
// --- Relleno rapido en LANDSCAPE (gLand) -------------------------------------
// La rotacion mapea (lx,ly) -> idx = lx*SCR_W + (SCR_W-1-ly). Es decir: para un
// lx FIJO, recorrer ly da direcciones CONSECUTIVAS. Una linea logica horizontal,
// en cambio, salta SCR_W*2 = 960 bytes por pixel, o sea UNA LINEA DE CACHE POR
// PIXEL contra PSRAM -- que es lo que hacia lento todo el Modo PC (un relleno de
// 430x268 son 115k escrituras dispersas).
// Por eso aqui se rellena por COLUMNA LOGICA: cada span queda secuencial. Mismo
// resultado pixel a pixel, mismo recorte (gClipY0/gClipY1 acotan filas fisicas,
// que en landscape son justo el eje lx).
static void fillSpanLand(int lx, int ly, int n, uint16_t c){
  if(n <= 0) return;
  if((unsigned)lx >= SCR_H) return;
  if(lx < gClipY0 || lx > gClipY1) return;
  if(ly < 0){ n += ly; ly = 0; }
  if(ly + n > SCR_W) n = SCR_W - ly;
  if(n <= 0) return;
  uint16_t* p = gBuf + (size_t)lx * SCR_W + (SCR_W - (ly + n));
  for(int i = 0; i < n; i++) p[i] = c;
}
static void fillSpanLandA(int lx, int ly, int n, uint16_t c, uint8_t a){
  if(a >= 255){ fillSpanLand(lx, ly, n, c); return; }
  if(a == 0 || n <= 0) return;
  if((unsigned)lx >= SCR_H) return;
  if(lx < gClipY0 || lx > gClipY1) return;
  if(ly < 0){ n += ly; ly = 0; }
  if(ly + n > SCR_W) n = SCR_W - ly;
  if(n <= 0) return;
  uint16_t* p = gBuf + (size_t)lx * SCR_W + (SCR_W - (ly + n));
  for(int i = 0; i < n; i++) p[i] = mix565(p[i], c, a);
}
static void fillRect(int x, int y, int w, int h, uint16_t c){
  if(gLand){ for(int i = 0; i < w; i++) fillSpanLand(x + i, y, h, c); return; }
  for(int j = 0; j < h; j++) hLine(x, y + j, w, c);
}
static void fillRectA(int x, int y, int w, int h, uint16_t c, uint8_t a){
  if(gLand){ for(int i = 0; i < w; i++) fillSpanLandA(x + i, y, h, c, a); return; }
  for(int j = 0; j < h; j++) hLineA(x, y + j, w, c, a);
}
static void drawRect(int x, int y, int w, int h, uint16_t c){
  hLine(x, y, w, c); hLine(x, y + h - 1, w, c);
  vLine(x, y, h, c); vLine(x + w - 1, y, h, c);
}

// Encaja un rect dentro de [0,limW) x [0,limH) respetando un tamano minimo y
// dejando SIEMPRE al menos `keep` px alcanzables dentro de la pantalla:
//   · en horizontal, `keep` px del rect siguen dentro por el lado que sea;
//   · en vertical, el borde SUPERIOR queda en [0, limH-keep], de modo que la
//     zona de agarre (barra de titulo, cabecera) nunca se va debajo del area.
// Es la puerta por la que debe pasar cualquier geometria movible: sin esto un
// arrastre o un resize puede dejar un elemento fuera de lo visible y volverlo
// imposible de tocar -- y con el, bloquear toda la interaccion.
// Devuelve true si hubo que corregir algo.
static bool flxClampRect(int &x, int &y, int &w, int &h,
                         int limW, int limH, int minW, int minH, int keep){
  int ox = x, oy = y, ow = w, oh = h;
  if(limW < 1) limW = 1;
  if(limH < 1) limH = 1;
  if(minW > limW) minW = limW;
  if(minH > limH) minH = limH;
  if(w < minW) w = minW;
  if(h < minH) h = minH;
  if(w > limW) w = limW;
  if(h > limH) h = limH;
  if(keep > w) keep = w;
  if(keep > h) keep = h;
  if(keep < 1) keep = 1;
  if(x > limW - keep) x = limW - keep;          // no se escapa por la derecha
  if(x + w < keep)    x = keep - w;             // ni por la izquierda
  if(y < 0) y = 0;
  int maxY = limH - keep; if(maxY < 0) maxY = 0;
  if(y > maxY) y = maxY;                        // el agarre siempre visible
  return x != ox || y != oy || w != ow || h != oh;
}

// Rectangulo redondeado relleno (esquinas suaves via inset por fila).
// En landscape se recorre por COLUMNA logica (mismo motivo que fillRect: asi
// cada span es memoria contigua). Es la TRANSPUESTA del mismo calculo, con lo
// que la silueta es la misma salvo, como mucho, 1 px en la diagonal de cada
// esquina -- y las dos variantes (opaca y alpha) usan la misma, asi que al
// superponerlas encajan exactamente.
static int rrInset(int k, int len, int r){
  if(k < r){ int d = r - 1 - k; return r - isqrt32(r * r - d * d); }
  if(k >= len - r){ int d = k - (len - r); return r - isqrt32(r * r - d * d); }
  return 0;
}
static void fillRoundRect(int x, int y, int w, int h, int r, uint16_t c){
  if(w <= 0 || h <= 0) return;
  if(r < 0) r = 0;
  if(2 * r > w) r = w / 2;
  if(2 * r > h) r = h / 2;
  if(gLand){
    for(int i = 0; i < w; i++){
      int in = rrInset(i, w, r);
      fillSpanLand(x + i, y + in, h - 2 * in, c);
    }
    return;
  }
  for(int j = 0; j < h; j++){
    int inset = rrInset(j, h, r);
    hLine(x + inset, y + j, w - 2 * inset, c);
  }
}
static void fillRoundRectA(int x, int y, int w, int h, int r, uint16_t c, uint8_t a){
  if(w <= 0 || h <= 0) return;
  if(r < 0) r = 0;
  if(2 * r > w) r = w / 2;
  if(2 * r > h) r = h / 2;
  if(gLand){
    for(int i = 0; i < w; i++){
      int in = rrInset(i, w, r);
      fillSpanLandA(x + i, y + in, h - 2 * in, c, a);
    }
    return;
  }
  for(int j = 0; j < h; j++){
    int inset = rrInset(j, h, r);
    hLineA(x + inset, y + j, w - 2 * inset, c, a);
  }
}
// Borde redondeado (1 px) para tarjetas
static void drawRoundRect(int x, int y, int w, int h, int r, uint16_t c){
  if(2 * r > w) r = w / 2;
  if(2 * r > h) r = h / 2;
  hLine(x + r, y, w - 2 * r, c);
  hLine(x + r, y + h - 1, w - 2 * r, c);
  vLine(x, y + r, h - 2 * r, c);
  vLine(x + w - 1, y + r, h - 2 * r, c);
  // esquinas
  int f = 1 - r, ddx = 1, ddy = -2 * r, xx = 0, yy = r;
  while(xx < yy){
    if(f >= 0){ yy--; ddy += 2; f += ddy; }
    xx++; ddx += 2; f += ddx;
    px(x + r - xx, y + r - yy, c); px(x + w - r - 1 + xx, y + r - yy, c);
    px(x + r - yy, y + r - xx, c); px(x + w - r - 1 + yy, y + r - xx, c);
    px(x + r - xx, y + h - r - 1 + yy, c); px(x + w - r - 1 + xx, y + h - r - 1 + yy, c);
    px(x + r - yy, y + h - r - 1 + xx, c); px(x + w - r - 1 + yy, y + h - r - 1 + xx, c);
  }
}

static void fillCircle(int cx, int cy, int r, uint16_t c){
  if(r <= 0){ px(cx, cy, c); return; }
  for(int dy = -r; dy <= r; dy++){
    int dx = isqrt32(r * r - dy * dy);
    hLine(cx - dx, cy + dy, 2 * dx + 1, c);
  }
}
static void fillCircleA(int cx, int cy, int r, uint16_t c, uint8_t a){
  if(r <= 0){ pxA(cx, cy, c, a); return; }
  for(int dy = -r; dy <= r; dy++){
    int dx = isqrt32(r * r - dy * dy);
    hLineA(cx - dx, cy + dy, 2 * dx + 1, c, a);
  }
}
static void drawCircle(int cx, int cy, int r, uint16_t c){
  int f = 1 - r, ddx = 1, ddy = -2 * r, x = 0, y = r;
  px(cx, cy + r, c); px(cx, cy - r, c); px(cx + r, cy, c); px(cx - r, cy, c);
  while(x < y){
    if(f >= 0){ y--; ddy += 2; f += ddy; }
    x++; ddx += 2; f += ddx;
    px(cx + x, cy + y, c); px(cx - x, cy + y, c);
    px(cx + x, cy - y, c); px(cx - x, cy - y, c);
    px(cx + y, cy + x, c); px(cx - y, cy + x, c);
    px(cx + y, cy - x, c); px(cx - y, cy - x, c);
  }
}
// anillo de grosor t
static void fillRing(int cx, int cy, int rOut, int t, uint16_t c){
  int rin = rOut - t; if(rin < 0) rin = 0;
  for(int dy = -rOut; dy <= rOut; dy++){
    int dxo = isqrt32(rOut * rOut - dy * dy);
    int inr2 = rin * rin - dy * dy;
    if(inr2 > 0){
      int dxi = isqrt32(inr2);
      hLine(cx - dxo, cy + dy, dxo - dxi, c);
      hLine(cx + dxi + 1, cy + dy, dxo - dxi, c);
    } else {
      hLine(cx - dxo, cy + dy, 2 * dxo + 1, c);
    }
  }
}
static void lineTo(int x0, int y0, int x1, int y1, uint16_t c){
  int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1, err = dx + dy;
  for(;;){
    px(x0, y0, c);
    if(x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if(e2 >= dy){ err += dy; x0 += sx; }
    if(e2 <= dx){ err += dx; y0 += sy; }
  }
}
// trazo grueso con puntas redondeadas: estampa discos por el segmento
static void strokeSeg(float x0, float y0, float x1, float y1, int rad, uint16_t c){
  float dx = x1 - x0, dy = y1 - y0;
  float len = sqrtf(dx * dx + dy * dy);
  int steps = (int)len + 1;
  for(int i = 0; i <= steps; i++){
    float t = (steps > 0) ? (float)i / steps : 0;
    fillCircle((int)(x0 + dx * t + 0.5f), (int)(y0 + dy * t + 0.5f), rad, c);
  }
}

// ---------------- Primitivas ANTI-ALIASING (bordes suaves) ----------------
// Cobertura por sub-pixel: los bordes se mezclan con alpha en vez de
// dibujarse "duros". Esto da curvas suaves al reloj y a los acentos.
static float distToSeg(float px, float py, float ax, float ay, float bx, float by){
  float dx = bx - ax, dy = by - ay;
  float l2 = dx * dx + dy * dy;
  float t = (l2 > 0.0f) ? ((px - ax) * dx + (py - ay) * dy) / l2 : 0.0f;
  if(t < 0) t = 0; else if(t > 1) t = 1;
  float qx = ax + t * dx - px, qy = ay + t * dy - py;
  return sqrtf(qx * qx + qy * qy);
}
static void fillCircleAA(float cx, float cy, float r, uint16_t col){
  int x0 = (int)floorf(cx - r - 1), x1 = (int)ceilf(cx + r + 1);
  int y0 = (int)floorf(cy - r - 1), y1 = (int)ceilf(cy + r + 1);
  for(int y = y0; y <= y1; y++) for(int x = x0; x <= x1; x++){
    float dx = x - cx, dy = y - cy;
    float cov = r + 0.5f - sqrtf(dx * dx + dy * dy);
    if(cov <= 0) continue; if(cov > 1) cov = 1;
    pxA(x, y, col, (uint8_t)(cov * 255));
  }
}
// Segmento grueso con puntas redondeadas y bordes suaves (para el reloj)
static void strokeSegAA(float x0, float y0, float x1, float y1, float rad, uint16_t col){
  int minx = (int)floorf(fminf(x0, x1) - rad - 1), maxx = (int)ceilf(fmaxf(x0, x1) + rad + 1);
  int miny = (int)floorf(fminf(y0, y1) - rad - 1), maxy = (int)ceilf(fmaxf(y0, y1) + rad + 1);
  for(int y = miny; y <= maxy; y++) for(int x = minx; x <= maxx; x++){
    float cov = rad + 0.5f - distToSeg((float)x, (float)y, x0, y0, x1, y1);
    if(cov <= 0) continue; if(cov > 1) cov = 1;
    pxA(x, y, col, (uint8_t)(cov * 255));
  }
}

// Empaquetado RGB565 en tiempo de COMPILACION (mismo bit a bit que rgb565(),
// que no es constexpr). Vive aqui y no en el bloque del tema semantico porque
// los fondos integrados de justo debajo son su primer uso del archivo.
#define TC(r,g,b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

// ---------------- Fondo (wallpaper) ----------------
// Degradado diagonal 3 paradas: verde (arriba-dcha) -> azul (centro)
// -> violeta (abajo-izq), igual que tus imagenes. Opcional: blobs.
// El degradado se repinta ENTERO en cada renderHome()/renderLock(), o sea una
// vez por minuto. Para las paginas del escritorio tambien se puede pedir solo
// la banda de iconos: evita regenerar 768 KB cuando realmente cambian 354 filas.
// La version anterior hacia, por cada uno de los 384.000 pixeles:
// 2 divisiones + 1 mix565 (que a su vez hacia 3 divisiones mas).
// Dos observaciones lo tiran casi todo abajo:
//   1) 'ty' NO depende de x, pero se recalculaba 480 veces por fila.
//   2) el color solo depende de t = (tx+ty)/2, que vive en [0,255]: caben
//      los 256 colores posibles en una tabla y el bucle interior pasa a ser
//      una simple consulta. Coste: 992 B de RAM estatica, una sola vez.
// El resultado en pantalla es BIT A BIT el mismo que antes.
static uint16_t wallGradLut[256];                  // color por t          (512 B)
static uint8_t  wallTxLut[SCR_W];                  // rampa horizontal      (480 B)
static bool     wallLutReady = false;
static void wallpaperEnsureLut(){
  if(!wallLutReady){
    const uint16_t green  = rgb565(80, 224, 74);   // arriba-derecha
    const uint16_t blue   = rgb565(40, 150, 245);  // centro
    const uint16_t purple = rgb565(112, 46, 230);  // abajo-izquierda
    for(int t = 0; t < 256; t++)
      wallGradLut[t] = (t < 128) ? mix565(purple, blue,  (uint8_t)(t * 2))
                                 : mix565(blue,   green, (uint8_t)((t - 128) * 2));
    for(int x = 0; x < SCR_W; x++) wallTxLut[x] = (uint8_t)((x * 255) / (SCR_W - 1));
    wallLutReady = true;
  }
}
