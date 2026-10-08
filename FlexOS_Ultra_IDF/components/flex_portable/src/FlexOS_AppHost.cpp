// #############################################################
//  FLEX OS · GESTOR DE APLICACIONES flex-app-v1  ·  implementación
//  ------------------------------------------------------------
//  Ver FlexOS_AppHost.h para el contrato completo.
//
//  LAS TRES REGLAS QUE ORDENAN ESTE ARCHIVO
//  ----------------------------------------
//  1. TODO permiso se comprueba EN EL PUNTO DE LA LLAMADA, contra la
//     máscara que salió de verificar el grant al arrancar ESTA sesión.
//     Ni el manifest ni una comprobación de hace diez minutos abren
//     nada por su cuenta.
//  2. TODA entrada de la app (dirección, longitud, coordenada, índice)
//     se valida antes de tocar nada del sistema. Una syscall no
//     "confía en que el bytecode ya lo hizo bien".
//  3. TODA salida —normal, por error, por cancelación o por seguridad—
//     pasa por releaseAll(): orientación, pantalla exclusiva, reservas
//     de PSRAM y sesión de carga se sueltan SIEMPRE.
// #############################################################
#include "FlexOS_AppHost.h"

#include <string.h>
#include <stdio.h>

namespace {

// ---- Tabla de syscalls --------------------------------------------------
// id · aridad · permiso exigido · nombre. Es la ÚNICA fuente: el validador de
// bytecode, el despachador y el SDK leen de aquí (el SDK con su copia, atada
// por los vectores dorados de tests/host/test_apphost.cpp).
struct SysDef { uint16_t id; uint8_t argc; uint32_t perm; const char* name; };

const SysDef kSys[] = {
  { FLEXSYS_GFX_CLEAR,        1, 0, "gfx.clear" },
  { FLEXSYS_GFX_FILL_RECT,    5, 0, "gfx.fill_rect" },
  { FLEXSYS_GFX_RECT,         5, 0, "gfx.rect" },
  { FLEXSYS_GFX_LINE,         5, 0, "gfx.line" },
  { FLEXSYS_GFX_ROUND_RECT,   6, 0, "gfx.round_rect" },
  { FLEXSYS_GFX_FILL_RECT_A,  6, 0, "gfx.fill_rect_a" },
  { FLEXSYS_GFX_TEXT_K,       6, 0, "gfx.text" },
  { FLEXSYS_GFX_TEXT_M,       6, 0, "gfx.text_mem" },
  { FLEXSYS_GFX_TEXT_WIDTH,   3, 0, "gfx.text_width" },
  { FLEXSYS_GFX_CLIP,         4, 0, "gfx.clip" },
  { FLEXSYS_GFX_CLIP_RESET,   0, 0, "gfx.clip_reset" },
  { FLEXSYS_GFX_BLIT,         5, 0, "gfx.blit" },
  { FLEXSYS_GFX_BLIT_SCALED,  7, 0, "gfx.blit_scaled" },
  { FLEXSYS_GFX_BLIT_ROT,     6, 0, "gfx.blit_rot" },
  { FLEXSYS_GFX_BLIT_ALPHA,   6, 0, "gfx.blit_alpha" },
  { FLEXSYS_GFX_WIDTH,        0, 0, "gfx.width" },
  { FLEXSYS_GFX_HEIGHT,       0, 0, "gfx.height" },
  { FLEXSYS_GFX_COLOR,        3, 0, "gfx.color" },
  { FLEXSYS_GFX_PIXEL,        3, 0, "gfx.pixel" },
  { FLEXSYS_GFX_PRESENT,      0, 0, "gfx.present" },

  { FLEXSYS_UI_CARD,          4, 0, "ui.card" },
  { FLEXSYS_UI_TITLE,         4, 0, "ui.title" },
  { FLEXSYS_UI_LABEL,         4, 0, "ui.label" },
  { FLEXSYS_UI_CAPTION,       4, 0, "ui.caption" },
  { FLEXSYS_UI_BUTTON,        7, 0, "ui.button" },
  { FLEXSYS_UI_LIST_ROW,      7, 0, "ui.list_row" },
  { FLEXSYS_UI_NAV_TITLE,     2, 0, "ui.nav_title" },

  { FLEXSYS_TIME_MS,          0, 0, "time.now_ms" },
  { FLEXSYS_TIME_US_LO,       0, 0, "time.now_us_lo" },
  { FLEXSYS_TIME_US_HI,       0, 0, "time.now_us_hi" },
  { FLEXSYS_TIMER_SET,        3, 0, "time.timer_set" },
  { FLEXSYS_TIMER_CLEAR,      1, 0, "time.timer_clear" },
  { FLEXSYS_BUDGET_LEFT,      0, 0, "time.budget_left" },

  { FLEXSYS_INPUT_POINTS,     0, 0, "input.points" },
  { FLEXSYS_INPUT_POINT_X,    1, 0, "input.point_x" },
  { FLEXSYS_INPUT_POINT_Y,    1, 0, "input.point_y" },
  { FLEXSYS_INPUT_POINT_ID,   1, 0, "input.point_id" },

  { FLEXSYS_STORE_WRITE,      4, FLEXPERM_SYS_STORAGE_APP, "store.write" },
  { FLEXSYS_STORE_READ,       4, FLEXPERM_SYS_STORAGE_APP, "store.read" },
  { FLEXSYS_STORE_SIZE,       2, FLEXPERM_SYS_STORAGE_APP, "store.size" },
  { FLEXSYS_STORE_DELETE,     2, FLEXPERM_SYS_STORAGE_APP, "store.delete" },
  { FLEXSYS_STORE_USED,       0, FLEXPERM_SYS_STORAGE_APP, "store.used" },
  { FLEXSYS_STORE_QUOTA,      0, FLEXPERM_SYS_STORAGE_APP, "store.quota" },

  { FLEXSYS_SCREEN_W,         0, 0, "sys.screen_w" },
  { FLEXSYS_SCREEN_H,         0, 0, "sys.screen_h" },
  { FLEXSYS_OS_VERSION,       2, 0, "sys.os_version" },
  { FLEXSYS_MODEL,            2, 0, "sys.model" },
  { FLEXSYS_CAPS,             0, 0, "sys.caps" },
  { FLEXSYS_LOG,              2, 0, "sys.log" },
  { FLEXSYS_NOTIFY,           4, 0, "sys.notify" },
  { FLEXSYS_EXIT,             0, 0, "sys.exit" },

  { FLEXSYS_DISPLAY_LANDSCAPE, 1, FLEXPERM_SYS_DISPLAY_LANDSCAPE, "display.landscape" },
  { FLEXSYS_DISPLAY_EXCLUSIVE, 1, FLEXPERM_SYS_DISPLAY_EXCLUSIVE, "display.exclusive" },
  { FLEXSYS_DISPLAY_ORIENT,    0, 0,                              "display.orientation" },

  { FLEXSYS_PERF_METRIC,      1, FLEXPERM_SYS_PERF_METRICS,     "perf.metric" },
  { FLEXSYS_PSRAM_FREE,       0, FLEXPERM_SYS_PSRAM_MEASURE,    "mem.psram_free" },
  { FLEXSYS_PSRAM_TOTAL,      0, FLEXPERM_SYS_PSRAM_MEASURE,    "mem.psram_total" },
  { FLEXSYS_PSRAM_LARGEST,    0, FLEXPERM_SYS_PSRAM_MEASURE,    "mem.psram_largest" },
  { FLEXSYS_PSRAM_RESERVE,    1, FLEXPERM_SYS_PSRAM_MEASURE,    "mem.reserve" },
  { FLEXSYS_PSRAM_RELEASE,    1, FLEXPERM_SYS_PSRAM_MEASURE,    "mem.release" },
  { FLEXSYS_PSRAM_POKE,       3, FLEXPERM_SYS_PSRAM_MEASURE,    "mem.poke" },
  { FLEXSYS_PSRAM_PEEK,       2, FLEXPERM_SYS_PSRAM_MEASURE,    "mem.peek" },
  { FLEXSYS_TEMP_MILLIC,      0, FLEXPERM_SYS_TEMPERATURE_READ, "temp.milli_c" },
  { FLEXSYS_CPU_STRESS_BEGIN, 2, FLEXPERM_SYS_CPU_STRESS,       "cpu.stress_begin" },
  { FLEXSYS_CPU_STRESS_SLICE, 0, FLEXPERM_SYS_CPU_STRESS,       "cpu.stress_slice" },
  { FLEXSYS_CPU_STRESS_LO,    0, FLEXPERM_SYS_CPU_STRESS,       "cpu.stress_lo" },
  { FLEXSYS_CPU_STRESS_HI,    0, FLEXPERM_SYS_CPU_STRESS,       "cpu.stress_hi" },
  { FLEXSYS_CPU_STRESS_STOP,  0, FLEXPERM_SYS_CPU_STRESS,       "cpu.stress_stop" },
  { FLEXSYS_BENCH_BEGIN,      1, FLEXPERM_SYS_BENCHMARK_RUN,    "bench.begin" },
  { FLEXSYS_BENCH_END,        0, FLEXPERM_SYS_BENCHMARK_RUN,    "bench.end" }
};
const size_t kSysCount = sizeof(kSys) / sizeof(kSys[0]);

const SysDef* findSys(uint16_t id){
  for(size_t i = 0; i < kSysCount; i++) if(kSys[i].id == id) return &kSys[i];
  return nullptr;
}

// Rodaja de instrucciones entre dos miradas al reloj. 2048 instrucciones son
// microsegundos en el P4: suficientemente corto para no desbordar el
// presupuesto de tiempo, suficientemente largo para que micros() no domine.
const uint32_t kSlice = 2048;

// Un onStop nunca puede quedarse colgado: tiene presupuesto propio y acotado.
const uint32_t kStopInstrBudget = 200000;

uint32_t clampU32(uint32_t v, uint32_t lo, uint32_t hi){
  return v < lo ? lo : (v > hi ? hi : v);
}

// Sanea texto que viene de la app: sólo imprimibles ASCII, longitud acotada.
// Un carácter de control en un drawText o en el registro serie no es un
// detalle estético: es lo que permite falsear una interfaz o ensuciar un log.
void sanitizeText(char* dst, size_t cap, const uint8_t* src, uint32_t len){
  size_t o = 0;
  for(uint32_t i = 0; i < len && o + 1 < cap; i++){
    uint8_t c = src[i];
    if(c >= 0x20 && c < 0x7F) dst[o++] = (char)c;
    else if(c == '\t' || c == '\n') dst[o++] = ' ';
    // el resto se descarta a propósito
  }
  dst[o] = 0;
}

// Nombre de archivo dentro de la carpeta privada de la app. Reglas duras:
// 1..FLEXAPP_NAME_MAX, sólo [A-Za-z0-9._-], sin empezar por punto y sin "..".
bool sanitizeName(char* dst, size_t cap, const uint8_t* src, uint32_t len){
  if(len == 0 || len > FLEXAPP_NAME_MAX || cap < (size_t)len + 1) return false;
  for(uint32_t i = 0; i < len; i++){
    uint8_t c = src[i];
    bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
    if(!ok) return false;
    dst[i] = (char)c;
  }
  dst[len] = 0;
  if(dst[0] == '.' || dst[0] == '-') return false;
  if(strstr(dst, "..")) return false;
  return true;
}

} // namespace

// -------------------------------------------------------------------------
uint8_t flexAppSyscallArity(uint16_t id){
  const SysDef* d = findSys(id);
  return d ? d->argc : 0xFF;
}
const char* flexAppSyscallName(uint16_t id){
  const SysDef* d = findSys(id);
  return d ? d->name : "";
}
uint32_t flexAppSyscallPermission(uint16_t id){
  const SysDef* d = findSys(id);
  return d ? d->perm : 0;
}

// -------------------------------------------------------------------------
namespace {

uint8_t arityBridge(void* user, uint16_t id){
  (void)user;
  return flexAppSyscallArity(id);
}

void setReason(FlexAppManager* m, const char* text);

// Suelta TODO lo que la app pudiera estar reteniendo. Es idempotente y se
// llama desde cualquier camino de salida.
void releaseAll(FlexAppManager* m){
  if(!m) return;
  const FlexAppHostApi* a = m->api;
  for(int i = 0; i < FLEXAPP_PSRAM_SLOTS; i++){
    if(m->psram[i] && a && a->psramRelease) a->psramRelease(m->user, m->psram[i]);
    m->psram[i] = 0;
  }
  m->psramKbHeld = 0;
  m->stressActive = 0; m->stressSlices = 0; m->stressDone = 0; m->stressAcc = 0;
  m->benchActive = 0; m->benchId = 0;
  // La orientación y la pantalla exclusiva se restauran SIEMPRE, y en el
  // orden inverso al que se tomaron: primero se devuelve la exclusiva (el
  // sistema vuelve a poder pintar), después la orientación.
  if(m->exclusiveApplied && a && a->setExclusive) a->setExclusive(m->user, false);
  m->exclusiveApplied = 0; m->wantExclusive = 0;
  if(m->landscapeApplied && a && a->setLandscape) a->setLandscape(m->user, false);
  m->landscapeApplied = 0; m->wantLandscape = 0;
  if(a && a->gfxClip) a->gfxClip(m->user, 0, 0, -1, -1);
  for(int i = 0; i < FLEXAPP_TIMERS; i++) m->timers[i].active = 0;
  m->qHead = m->qTail = m->qCount = 0;
}

void setReason(FlexAppManager* m, const char* text){
  if(!m) return;
  snprintf(m->reason, sizeof(m->reason), "%s", text ? text : "");
}

uint32_t elapsedMs(const FlexAppManager* m){
  if(!m->api || !m->api->nowUs) return 0;
  uint64_t now = m->api->nowUs(m->user);
  return (uint32_t)((now - m->startUs) / 1000ull);
}

// Lee `len` bytes que la app señala desde el pool de constantes (off >= 0) o
// desde su memoria lineal, según el origen pedido. Devuelve NULL si la región
// no cabe entera: NUNCA se lee un byte de más.
const uint8_t* appBytes(FlexAppManager* m, bool fromConst, int32_t off, int32_t len){
  if(len < 0) return nullptr;
  const FlexVm& vm = m->vm;
  uint32_t total = fromConst ? vm.kdataLen : vm.memBytes;
  const uint8_t* base = fromConst ? vm.kdata : vm.mem;
  if(!base && total) return nullptr;
  if(off < 0) return nullptr;
  if((uint64_t)(uint32_t)off + (uint64_t)(uint32_t)len > (uint64_t)total) return nullptr;
  return base + (uint32_t)off;
}

bool takeDrawBudget(FlexAppManager* m, uint32_t cost){
  if(m->drawUsed + cost > m->drawPerFrame){
    m->drawDropped++;
    return false;
  }
  m->drawUsed += cost;
  return true;
}

} // namespace

// -------------------------------------------------------------------------
//  SYSCALLS
// -------------------------------------------------------------------------
namespace {

int32_t syscallDispatch(void* user, uint16_t id, const int32_t* v, uint8_t argc, bool* fatal){
  FlexAppManager* m = (FlexAppManager*)user;
  const FlexAppHostApi* a = m->api;
  const SysDef* def = findSys(id);
  if(!def || def->argc != argc){ *fatal = true; return 0; }

  // ---- PERMISO, POR LLAMADA -------------------------------------------
  // Aquí es donde un permiso se hace real. La máscara `granted` sólo existe
  // si el grant firmado cuadró con ESTA app, ESTA versión y ESTE paquete al
  // arrancar. Sin eso, el servicio no se abre ni una vez.
  if(def->perm && (m->granted & def->perm) == 0) return FLEXAPP_NO_VALUE;

  // Reentrada: una syscall no puede volver a entrar en la VM. El puente no
  // debe hacerlo y aquí se comprueba, no se confía.
  if(m->inSyscall){ *fatal = true; return 0; }
  m->inSyscall = 1;
  int32_t out = 0;

  switch(id){
    // ---------------- Render 2D ----------------
    case FLEXSYS_GFX_CLEAR:
      if(takeDrawBudget(m, 4) && a->gfxClear) a->gfxClear(m->user, (uint16_t)v[0]);
      break;
    case FLEXSYS_GFX_FILL_RECT:
      if(takeDrawBudget(m, 1) && a->gfxFillRect) a->gfxFillRect(m->user, v[0], v[1], v[2], v[3], (uint16_t)v[4], 255);
      break;
    case FLEXSYS_GFX_FILL_RECT_A:
      if(takeDrawBudget(m, 2) && a->gfxFillRect){
        int alpha = v[5] < 0 ? 0 : (v[5] > 255 ? 255 : v[5]);
        a->gfxFillRect(m->user, v[0], v[1], v[2], v[3], (uint16_t)v[4], alpha);
      }
      break;
    case FLEXSYS_GFX_RECT:
      if(takeDrawBudget(m, 1) && a->gfxRect) a->gfxRect(m->user, v[0], v[1], v[2], v[3], (uint16_t)v[4]);
      break;
    case FLEXSYS_GFX_LINE:
      if(takeDrawBudget(m, 1) && a->gfxLine) a->gfxLine(m->user, v[0], v[1], v[2], v[3], (uint16_t)v[4]);
      break;
    case FLEXSYS_GFX_ROUND_RECT:
      if(takeDrawBudget(m, 2) && a->gfxRoundRect) a->gfxRoundRect(m->user, v[0], v[1], v[2], v[3], v[4], (uint16_t)v[5]);
      break;
    case FLEXSYS_GFX_PIXEL:
      if(takeDrawBudget(m, 1) && a->gfxPixel) a->gfxPixel(m->user, v[0], v[1], (uint16_t)v[2]);
      break;
    case FLEXSYS_GFX_TEXT_K:
    case FLEXSYS_GFX_TEXT_M: {
      const uint8_t* p = appBytes(m, id == FLEXSYS_GFX_TEXT_K, v[0], v[1]);
      if(p && takeDrawBudget(m, 2) && a->gfxText){
        sanitizeText(m->textBuf, sizeof(m->textBuf), p, (uint32_t)v[1]);
        int size = v[4] < 1 ? 1 : (v[4] > 4 ? 4 : v[4]);
        a->gfxText(m->user, m->textBuf, v[2], v[3], size, (uint16_t)v[5]);
      }
      break;
    }
    case FLEXSYS_GFX_TEXT_WIDTH: {
      const uint8_t* p = appBytes(m, true, v[0], v[1]);
      if(p && a->gfxTextWidth){
        sanitizeText(m->textBuf, sizeof(m->textBuf), p, (uint32_t)v[1]);
        int size = v[2] < 1 ? 1 : (v[2] > 4 ? 4 : v[2]);
        out = a->gfxTextWidth(m->user, m->textBuf, size);
      }
      break;
    }
    case FLEXSYS_GFX_CLIP:
      if(a->gfxClip) a->gfxClip(m->user, v[0], v[1], v[2], v[3]);
      break;
    case FLEXSYS_GFX_CLIP_RESET:
      if(a->gfxClip) a->gfxClip(m->user, 0, 0, -1, -1);
      break;
    case FLEXSYS_GFX_BLIT:
    case FLEXSYS_GFX_BLIT_SCALED:
    case FLEXSYS_GFX_BLIT_ROT:
    case FLEXSYS_GFX_BLIT_ALPHA: {
      int32_t w = v[1], h = v[2];
      // Un sprite es RGB565: 2 bytes por píxel. El tamaño se calcula en 64
      // bits y se comprueba contra la memoria de la app ANTES de mirar nada.
      if(w <= 0 || h <= 0 || w > 4096 || h > 4096) break;
      uint64_t bytes = (uint64_t)w * (uint64_t)h * 2ull;
      if(bytes > (uint64_t)0x7FFFFFFF) break;
      const uint8_t* px = appBytes(m, false, v[0], (int32_t)bytes);
      if(!px) break;
      int dw = w, dh = h, quad = 0, alpha = 255;
      if(id == FLEXSYS_GFX_BLIT_SCALED){
        dw = v[5]; dh = v[6];
        if(dw <= 0 || dh <= 0 || dw > 4096 || dh > 4096) break;
      } else if(id == FLEXSYS_GFX_BLIT_ROT){
        quad = v[5] & 3;
      } else if(id == FLEXSYS_GFX_BLIT_ALPHA){
        alpha = v[5] < 0 ? 0 : (v[5] > 255 ? 255 : v[5]);
      }
      if((uint64_t)dw * (uint64_t)dh > (uint64_t)FLEXAPP_MAX_BLIT_PIXELS) break;
      // El coste se cobra por píxeles de SALIDA en bloques de 4096: un blit
      // grande gasta presupuesto de cuadro igual que muchos rectángulos.
      uint32_t cost = (uint32_t)(((uint64_t)dw * dh + 4095ull) / 4096ull);
      if(cost < 1) cost = 1;
      if(takeDrawBudget(m, cost) && a->gfxBlit)
        a->gfxBlit(m->user, px, w, h, v[3], v[4], dw, dh, quad, alpha);
      break;
    }
    case FLEXSYS_GFX_WIDTH:  out = a->gfxCanvasW ? a->gfxCanvasW(m->user) : 0; break;
    case FLEXSYS_GFX_HEIGHT: out = a->gfxCanvasH ? a->gfxCanvasH(m->user) : 0; break;
    case FLEXSYS_GFX_COLOR: {
      uint32_t r = (uint32_t)(v[0] < 0 ? 0 : (v[0] > 255 ? 255 : v[0]));
      uint32_t g = (uint32_t)(v[1] < 0 ? 0 : (v[1] > 255 ? 255 : v[1]));
      uint32_t b = (uint32_t)(v[2] < 0 ? 0 : (v[2] > 255 ? 255 : v[2]));
      out = (int32_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
      break;
    }
    case FLEXSYS_GFX_PRESENT:
      m->presentRequested = 1;
      m->frames++;
      m->drawUsed = 0;              // el presupuesto de dibujo es POR CUADRO
      if(a->gfxPresent) a->gfxPresent(m->user);
      break;

    // ---------------- Componentes de interfaz ----------------
    case FLEXSYS_UI_CARD:
      if(takeDrawBudget(m, 2) && a->uiWidget) a->uiWidget(m->user, 0, "", v[0], v[1], v[2], v[3], 0);
      break;
    case FLEXSYS_UI_TITLE:
    case FLEXSYS_UI_LABEL:
    case FLEXSYS_UI_CAPTION: {
      const uint8_t* p = appBytes(m, true, v[0], v[1]);
      if(p && takeDrawBudget(m, 2) && a->uiWidget){
        sanitizeText(m->textBuf, sizeof(m->textBuf), p, (uint32_t)v[1]);
        int kind = (id == FLEXSYS_UI_TITLE) ? 1 : (id == FLEXSYS_UI_LABEL ? 2 : 3);
        a->uiWidget(m->user, kind, m->textBuf, v[2], v[3], 0, 0, 0);
      }
      break;
    }
    case FLEXSYS_UI_BUTTON:
    case FLEXSYS_UI_LIST_ROW: {
      const uint8_t* p = appBytes(m, true, v[0], v[1]);
      if(p && takeDrawBudget(m, 3) && a->uiWidget){
        sanitizeText(m->textBuf, sizeof(m->textBuf), p, (uint32_t)v[1]);
        int kind = (id == FLEXSYS_UI_BUTTON) ? 4 : 5;
        a->uiWidget(m->user, kind, m->textBuf, v[2], v[3], v[4], v[5], v[6]);
      }
      break;
    }
    case FLEXSYS_UI_NAV_TITLE: {
      const uint8_t* p = appBytes(m, true, v[0], v[1]);
      if(p && a->uiNavTitle){
        sanitizeText(m->textBuf, sizeof(m->textBuf), p, (uint32_t)v[1]);
        a->uiNavTitle(m->user, m->textBuf);
      }
      break;
    }

    // ---------------- Tiempo ----------------
    case FLEXSYS_TIME_MS:    out = (int32_t)elapsedMs(m); break;
    case FLEXSYS_TIME_US_LO: out = a->nowUs ? (int32_t)(uint32_t)(a->nowUs(m->user) & 0xFFFFFFFFull) : 0; break;
    case FLEXSYS_TIME_US_HI: out = a->nowUs ? (int32_t)(uint32_t)(a->nowUs(m->user) >> 32) : 0; break;
    case FLEXSYS_TIMER_SET: {
      int32_t idx = v[0];
      if(idx < 0 || idx >= FLEXAPP_TIMERS || v[1] < 1){ out = 0; break; }
      FlexAppTimer& t = m->timers[idx];
      t.periodMs = (uint32_t)v[1];
      t.repeat = v[2] ? 1 : 0;
      t.nextMs = elapsedMs(m) + t.periodMs;
      t.active = 1;
      out = 1;
      break;
    }
    case FLEXSYS_TIMER_CLEAR: {
      int32_t idx = v[0];
      if(idx < 0 || idx >= FLEXAPP_TIMERS){ out = 0; break; }
      m->timers[idx].active = 0;
      out = 1;
      break;
    }
    case FLEXSYS_BUDGET_LEFT:
      out = (int32_t)(m->instrThisTick >= m->instrPerTick ? 0 : (m->instrPerTick - m->instrThisTick));
      break;

    // ---------------- Entrada ----------------
    case FLEXSYS_INPUT_POINTS:
    case FLEXSYS_INPUT_POINT_X:
    case FLEXSYS_INPUT_POINT_Y:
    case FLEXSYS_INPUT_POINT_ID: {
      // Si el panel no da un dato fiable, se dice NO_VALUE. No se rellena con
      // el último toque conocido ni con ceros: un multitáctil inventado es
      // exactamente lo que no puede salir de aquí.
      FlexAppPoint pts[FLEXAPP_TOUCH_POINTS];
      int n = a->touchPoints ? a->touchPoints(m->user, pts, FLEXAPP_TOUCH_POINTS) : -1;
      if(n < 0){ out = FLEXAPP_NO_VALUE; break; }
      if(id == FLEXSYS_INPUT_POINTS){ out = n; break; }
      int32_t i = v[0];
      if(i < 0 || i >= n){ out = FLEXAPP_NO_VALUE; break; }
      out = (id == FLEXSYS_INPUT_POINT_X) ? pts[i].x :
            (id == FLEXSYS_INPUT_POINT_Y) ? pts[i].y : pts[i].id;
      break;
    }

    // ---------------- Almacenamiento privado ----------------
    case FLEXSYS_STORE_WRITE: {
      char name[FLEXAPP_NAME_MAX + 1];
      const uint8_t* np = appBytes(m, true, v[0], v[1]);
      const uint8_t* dp = appBytes(m, false, v[2], v[3]);
      if(!np || !dp || !sanitizeName(name, sizeof(name), np, (uint32_t)v[1]) || !a->storeWrite){ out = 0; break; }
      uint32_t used = a->storeUsed ? a->storeUsed(m->user) : 0;
      int32_t prev = a->storeSize ? a->storeSize(m->user, name) : -1;
      uint64_t after = (uint64_t)used - (uint64_t)(prev > 0 ? prev : 0) + (uint64_t)(uint32_t)v[3];
      if(after > (uint64_t)m->storageQuota){ out = 0; break; }   // cuota: no se sobrepasa
      out = a->storeWrite(m->user, name, dp, (uint32_t)v[3]) ? 1 : 0;
      break;
    }
    case FLEXSYS_STORE_READ: {
      char name[FLEXAPP_NAME_MAX + 1];
      const uint8_t* np = appBytes(m, true, v[0], v[1]);
      const uint8_t* dp = appBytes(m, false, v[2], v[3]);
      if(!np || !dp || !sanitizeName(name, sizeof(name), np, (uint32_t)v[1]) || !a->storeRead){ out = -1; break; }
      out = a->storeRead(m->user, name, m->vm.mem + (uint32_t)v[2], (uint32_t)v[3]);
      break;
    }
    case FLEXSYS_STORE_SIZE: {
      char name[FLEXAPP_NAME_MAX + 1];
      const uint8_t* np = appBytes(m, true, v[0], v[1]);
      if(!np || !sanitizeName(name, sizeof(name), np, (uint32_t)v[1]) || !a->storeSize){ out = -1; break; }
      out = a->storeSize(m->user, name);
      break;
    }
    case FLEXSYS_STORE_DELETE: {
      char name[FLEXAPP_NAME_MAX + 1];
      const uint8_t* np = appBytes(m, true, v[0], v[1]);
      if(!np || !sanitizeName(name, sizeof(name), np, (uint32_t)v[1]) || !a->storeDelete){ out = 0; break; }
      out = a->storeDelete(m->user, name) ? 1 : 0;
      break;
    }
    case FLEXSYS_STORE_USED:  out = a->storeUsed ? (int32_t)a->storeUsed(m->user) : 0; break;
    case FLEXSYS_STORE_QUOTA: out = (int32_t)m->storageQuota; break;

    // ---------------- Sistema ----------------
    case FLEXSYS_SCREEN_W: out = a->screenW ? a->screenW(m->user) : 0; break;
    case FLEXSYS_SCREEN_H: out = a->screenH ? a->screenH(m->user) : 0; break;
    case FLEXSYS_OS_VERSION:
    case FLEXSYS_MODEL: {
      int32_t cap = v[1];
      if(cap <= 0 || !appBytes(m, false, v[0], cap) || !a->osInfo){ out = 0; break; }
      char tmp[64];
      int n = a->osInfo(m->user, id == FLEXSYS_OS_VERSION ? 0 : 1, tmp, (int)sizeof(tmp));
      if(n < 0) n = 0;
      if(n > cap) n = cap;
      if(n) memcpy(m->vm.mem + (uint32_t)v[0], tmp, (size_t)n);
      out = n;
      break;
    }
    case FLEXSYS_CAPS: out = a->caps ? (int32_t)a->caps(m->user) : 0; break;
    case FLEXSYS_LOG: {
      const uint8_t* p = appBytes(m, true, v[0], v[1]);
      if(p && a->logLine){
        sanitizeText(m->textBuf, sizeof(m->textBuf), p, (uint32_t)v[1]);
        a->logLine(m->user, m->textBuf);
      }
      break;
    }
    case FLEXSYS_NOTIFY: {
      const uint8_t* tp = appBytes(m, true, v[0], v[1]);
      const uint8_t* sp = appBytes(m, true, v[2], v[3]);
      if(!tp || !sp || !a->notify){ out = 0; break; }
      char title[72];
      sanitizeText(title, sizeof(title), tp, (uint32_t)v[1]);
      sanitizeText(m->textBuf, sizeof(m->textBuf), sp, (uint32_t)v[3]);
      out = a->notify(m->user, title, m->textBuf) ? 1 : 0;
      break;
    }
    case FLEXSYS_EXIT:
      m->exitRequested = 1;
      break;

    // ---------------- Pantalla ----------------
    case FLEXSYS_DISPLAY_LANDSCAPE: {
      bool on = v[0] != 0;
      if(!a->setLandscape){ out = 0; break; }
      out = a->setLandscape(m->user, on) ? 1 : 0;
      if(out){ m->landscapeApplied = on ? 1 : 0; m->wantLandscape = m->landscapeApplied; }
      break;
    }
    case FLEXSYS_DISPLAY_EXCLUSIVE: {
      bool on = v[0] != 0;
      if(!a->setExclusive){ out = 0; break; }
      out = a->setExclusive(m->user, on) ? 1 : 0;
      if(out){ m->exclusiveApplied = on ? 1 : 0; m->wantExclusive = m->exclusiveApplied; }
      break;
    }
    case FLEXSYS_DISPLAY_ORIENT: out = m->landscapeApplied ? 1 : 0; break;

    // ---------------- Servicios privilegiados ----------------
    case FLEXSYS_PERF_METRIC:
      out = a->perfMetric ? a->perfMetric(m->user, v[0]) : FLEXAPP_NO_VALUE;
      break;
    case FLEXSYS_PSRAM_FREE:    out = a->psramKb ? a->psramKb(m->user, 0) : FLEXAPP_NO_VALUE; break;
    case FLEXSYS_PSRAM_TOTAL:   out = a->psramKb ? a->psramKb(m->user, 1) : FLEXAPP_NO_VALUE; break;
    case FLEXSYS_PSRAM_LARGEST: out = a->psramKb ? a->psramKb(m->user, 2) : FLEXAPP_NO_VALUE; break;
    case FLEXSYS_PSRAM_RESERVE: {
      // Nunca se intenta reservar "toda la PSRAM": hay techo por petición,
      // techo acumulado por app y un número fijo de ranuras. Si no se puede,
      // se devuelve 0 limpiamente -- que es lo que la app tiene que saber
      // manejar, igual que un malloc que falla.
      if(v[0] <= 0 || !a->psramReserve){ out = 0; break; }
      uint32_t kb = (uint32_t)v[0];
      if(kb > 8192u){ out = 0; break; }
      if(m->psramKbHeld + kb > 8192u){ out = 0; break; }
      int slot = -1;
      for(int i = 0; i < FLEXAPP_PSRAM_SLOTS; i++) if(!m->psram[i]){ slot = i; break; }
      if(slot < 0){ out = 0; break; }
      int32_t h = a->psramReserve(m->user, kb);
      if(h <= 0){ out = 0; break; }
      m->psram[slot] = h;
      m->psramKbHeld += kb;
      out = h;
      break;
    }
    case FLEXSYS_PSRAM_RELEASE: {
      int slot = -1;
      for(int i = 0; i < FLEXAPP_PSRAM_SLOTS; i++) if(m->psram[i] == v[0] && v[0]){ slot = i; break; }
      if(slot < 0 || !a->psramRelease){ out = 0; break; }
      out = a->psramRelease(m->user, m->psram[slot]) ? 1 : 0;
      m->psram[slot] = 0;
      break;
    }
    case FLEXSYS_PSRAM_POKE:
    case FLEXSYS_PSRAM_PEEK: {
      bool own = false;
      for(int i = 0; i < FLEXAPP_PSRAM_SLOTS; i++) if(m->psram[i] == v[0] && v[0]) own = true;
      if(!own || v[1] < 0){ out = (id == FLEXSYS_PSRAM_POKE) ? 0 : FLEXAPP_NO_VALUE; break; }
      if(id == FLEXSYS_PSRAM_POKE){
        out = (a->psramPoke && a->psramPoke(m->user, v[0], (uint32_t)v[1], v[2])) ? 1 : 0;
      } else {
        int32_t got = 0;
        out = (a->psramPeek && a->psramPeek(m->user, v[0], (uint32_t)v[1], &got)) ? got : FLEXAPP_NO_VALUE;
      }
      break;
    }
    case FLEXSYS_TEMP_MILLIC:
      // Si la placa no expone una lectura REAL, aquí sale NO_VALUE y la app
      // tiene que enseñar "No disponible". No se sintetiza un número.
      out = a->tempMilliC ? a->tempMilliC(m->user) : FLEXAPP_NO_VALUE;
      break;

    case FLEXSYS_CPU_STRESS_BEGIN:
      if(v[1] <= 0 || v[1] > 100000 || !a->cpuStressSlice){ out = 0; break; }
      m->stressActive = 1; m->stressKind = (uint8_t)(v[0] & 0xFF);
      m->stressSlices = (uint32_t)v[1]; m->stressDone = 0; m->stressAcc = 0;
      out = 1;
      break;
    case FLEXSYS_CPU_STRESS_SLICE: {
      if(!m->stressActive || !a->cpuStressSlice){ out = -1; break; }
      if(m->stressDone >= m->stressSlices){ m->stressActive = 0; out = -1; break; }
      // UNA rodaja, acotada en tiempo. Entre rodaja y rodaja el control
      // vuelve a la app y de ahí al sistema: nunca hay un bucle infinito ni
      // una carga que no se pueda cancelar.
      uint32_t slice = m->usPerTick / 2;
      if(slice < 200) slice = 200;
      int32_t did = a->cpuStressSlice(m->user, m->stressKind, slice, &m->stressAcc);
      if(did < 0){ m->stressActive = 0; out = -1; break; }
      m->stressDone++;
      out = (int32_t)m->stressDone;
      break;
    }
    case FLEXSYS_CPU_STRESS_LO: out = (int32_t)(uint32_t)(m->stressAcc & 0xFFFFFFFFull); break;
    case FLEXSYS_CPU_STRESS_HI: out = (int32_t)(uint32_t)(m->stressAcc >> 32); break;
    case FLEXSYS_CPU_STRESS_STOP: m->stressActive = 0; out = 1; break;

    case FLEXSYS_BENCH_BEGIN: m->benchActive = 1; m->benchId = v[0]; out = 1; break;
    case FLEXSYS_BENCH_END:   m->benchActive = 0; out = 1; break;

    default:
      *fatal = true;
      break;
  }

  m->inSyscall = 0;
  return out;
}

} // namespace

// -------------------------------------------------------------------------
//  CICLO DE VIDA
// -------------------------------------------------------------------------
void flexAppInit(FlexAppManager* m, const FlexAppHostApi* api, void* user){
  if(!m) return;
  memset(m, 0, sizeof(*m));
  m->api = api;
  m->user = user;
  m->state = FLEXAPP_ST_INSTALLED;
  m->instrPerTick = FLEXAPP_DEF_INSTR_TICK;
  m->usPerTick = FLEXAPP_DEF_US_TICK;
  m->drawPerFrame = FLEXAPP_DEF_DRAW_FRAME;
  flexVmInit(&m->vm);
  setReason(m, "Sin iniciar");
}

bool flexAppStart(FlexAppManager* m, const FlexAppLaunch* L){
  if(!m || !L || !m->api) return false;
  if(m->state == FLEXAPP_ST_RUNNING || m->state == FLEXAPP_ST_STARTING){
    setReason(m, "La aplicacion ya estaba en marcha");
    return false;
  }
  // Un arranque nuevo NUNCA hereda nada del anterior.
  releaseAll(m);
  const FlexAppHostApi* api = m->api;
  void* user = m->user;
  memset(m, 0, sizeof(*m));
  m->api = api; m->user = user;
  m->state = FLEXAPP_ST_STARTING;
  flexVmInit(&m->vm);

  snprintf(m->id, sizeof(m->id), "%s", L->packageId ? L->packageId : "");
  snprintf(m->versionName, sizeof(m->versionName), "%s", L->versionName ? L->versionName : "");
  m->versionCode = L->versionCode;

  m->instrPerTick  = L->instrPerTick  ? clampU32(L->instrPerTick, 1000u, FLEXAPP_MAX_INSTR_TICK) : FLEXAPP_DEF_INSTR_TICK;
  m->usPerTick     = L->usPerTick     ? clampU32(L->usPerTick, 500u, FLEXAPP_MAX_US_TICK)        : FLEXAPP_DEF_US_TICK;
  m->drawPerFrame  = L->drawPerFrame  ? clampU32(L->drawPerFrame, 32u, FLEXAPP_MAX_DRAW_FRAME)   : FLEXAPP_DEF_DRAW_FRAME;
  m->storageQuota  = L->storageQuota;

  // ---- 1) Permisos ------------------------------------------------------
  // ANTES del bytecode: si el grant no cuadra, la app arranca igual pero SIN
  // privilegios. Un grant roto no impide usar la app; impide medir el sistema.
  FlexGrantExpect exp;
  memset(&exp, 0, sizeof(exp));
  exp.packageId = m->id;
  exp.versionName = m->versionName;
  exp.versionCode = m->versionCode;
  memcpy(exp.packageSha256, L->packageSha256, 32);
  memcpy(exp.developerKeySha256, L->developerKeySha256, 32);
  exp.manifestRequested = L->manifestPermissions;
  exp.nowEpoch = L->nowEpoch;
  FlexGrantResult gr;
  FlexGrantStatus gs = flexGrantCheck(L->grant, L->grantLen, &exp,
                                      L->trustedPub ? L->trustedPub : nullptr, &gr);
  m->grantStatus = (uint8_t)gs;
  m->granted = (gs == FLEXGRANT_OK) ? gr.granted : 0u;
  m->grantWindowChecked = (gs == FLEXGRANT_OK) ? gr.windowChecked : 0;
  if(!(m->granted & FLEXPERM_SYS_STORAGE_APP)) m->storageQuota = 0;

  // ---- 2) Bytecode ------------------------------------------------------
  FlexVmLoad lr = flexVmValidate(&m->vm, L->image, L->imageLen, L->scratch, L->scratchLen,
                                 0, 0, L->memBytes, arityBridge, m);
  if(lr != FLEXVM_LOAD_OK){
    setReason(m, flexVmLoadText(lr));
    m->state = FLEXAPP_ST_ERROR;
    m->stopReason = FLEXAPP_STOP_LOAD;
    return false;
  }
  lr = flexVmBind(&m->vm, L->mem, L->memBytes, L->globals, L->globalCount,
                  L->stack, L->stackSlots, L->locals, L->frameSlots,
                  L->frames, L->callDepth, syscallDispatch, m);
  if(lr != FLEXVM_LOAD_OK){
    setReason(m, flexVmLoadText(lr));
    m->state = FLEXAPP_ST_ERROR;
    m->stopReason = FLEXAPP_STOP_LOAD;
    return false;
  }

  m->startUs = m->api->nowUs ? m->api->nowUs(m->user) : 0;
  m->lastTickUs = m->startUs;

  // ---- 3) onStart -------------------------------------------------------
  if(flexVmHasHandler(&m->vm, FLEXVM_H_START)){
    if(!flexVmCall(&m->vm, FLEXVM_H_START, nullptr, 0)){
      setReason(m, "No se pudo iniciar la aplicacion");
      m->state = FLEXAPP_ST_ERROR; m->stopReason = FLEXAPP_STOP_LOAD;
      releaseAll(m);
      return false;
    }
    m->state = FLEXAPP_ST_RUNNING;
    // onStart corre con el presupuesto normal. Si no termina, sigue en el
    // primer tick: es un manejador más, no una excepción.
    flexAppTick(m);
    if(m->state == FLEXAPP_ST_ERROR || m->state == FLEXAPP_ST_STOPPED_SECURITY){
      // Si onStart llego a tomar algo (pantalla, orientacion, PSRAM) antes de
      // morir, se suelta AQUI: un arranque fallido no deja nada tomado.
      releaseAll(m);
      return false;
    }
  }
  m->state = FLEXAPP_ST_RUNNING;
  setReason(m, "En ejecucion");
  return true;
}

void flexAppPostEvent(FlexAppManager* m, FlexAppEventType type, int32_t a, int32_t b){
  if(!m || type == FLEXAPP_EV_NONE) return;
  if(m->state != FLEXAPP_ST_RUNNING && m->state != FLEXAPP_ST_SUSPENDED) return;
  if(!flexVmHasHandler(&m->vm, FLEXVM_H_EVENT)) return;
  if(m->qCount >= FLEXAPP_EVENT_QUEUE){
    // Cola llena: se tira el MAS ANTIGUO y se cuenta. La cola nunca crece.
    m->qHead = (uint8_t)((m->qHead + 1) % FLEXAPP_EVENT_QUEUE);
    m->qCount--;
    if(m->qDropped < 255) m->qDropped++;
  }
  FlexAppEvent& e = m->queue[m->qTail];
  e.type = (uint8_t)type; e.a = a; e.b = b;
  m->qTail = (uint8_t)((m->qTail + 1) % FLEXAPP_EVENT_QUEUE);
  m->qCount++;
}

namespace {

// Ejecuta el manejador en curso dentro del presupuesto del tick.
// Devuelve true si TODO fue bien (aunque quede trabajo pendiente).
bool runBudgeted(FlexAppManager* m, uint32_t instrBudget, uint32_t usBudget){
  const FlexAppHostApi* a = m->api;
  uint64_t t0 = (a && a->nowUs) ? a->nowUs(m->user) : 0;
  uint32_t usedTotal = 0;
  while(usedTotal < instrBudget){
    uint32_t slice = instrBudget - usedTotal;
    if(slice > kSlice) slice = kSlice;
    uint32_t used = 0;
    FlexVmRun r = flexVmRun(&m->vm, slice, &used);
    usedTotal += used;
    m->instrThisTick += used;
    m->instrThisCall += used;
    m->instrTotal += used;
    if(r == FLEXVM_TRAP){
      char buf[FLEXAPP_REASON_MAX];
      snprintf(buf, sizeof(buf), "%s", flexVmTrapText(m->vm.trap));
      setReason(m, buf);
      m->state = FLEXAPP_ST_ERROR;
      m->stopReason = FLEXAPP_STOP_TRAP;
      return false;
    }
    if(r == FLEXVM_DONE || r == FLEXVM_IDLE) return true;
    if(m->vm.yielded) return true;             // cesión voluntaria: hasta el próximo tick
    if(m->instrThisCall > FLEXAPP_MAX_INSTR_CALL){
      setReason(m, "La aplicacion no devuelve el control (instrucciones)");
      m->state = FLEXAPP_ST_STOPPED_SECURITY;
      m->stopReason = FLEXAPP_STOP_BUDGET;
      flexVmAbort(&m->vm, FLEXVM_TRAP_HOST);
      return false;
    }
    // El TIEMPO manda igual que las instrucciones: se mira entre rodajas.
    if(a && a->nowUs && usBudget){
      uint64_t now = a->nowUs(m->user);
      if(now - t0 >= (uint64_t)usBudget) return true;
    }
  }
  return true;
}

} // namespace

void flexAppTick(FlexAppManager* m){
  if(!m || m->state != FLEXAPP_ST_RUNNING) return;
  m->ticks++;
  m->instrThisTick = 0;

  // 1) ¿Hay un manejador a medias del tick anterior? Se continúa ESE.
  if(m->vm.running){
    if(!runBudgeted(m, m->instrPerTick, m->usPerTick)) return;
    if(m->vm.running){
      m->overrunTicks++;
      if(m->overrunTicks > FLEXAPP_MAX_OVERRUN_TICKS){
        setReason(m, "La aplicacion no devuelve el control (tiempo)");
        m->state = FLEXAPP_ST_STOPPED_SECURITY;
        m->stopReason = FLEXAPP_STOP_BUDGET;
        flexVmAbort(&m->vm, FLEXVM_TRAP_HOST);
        releaseAll(m);
      }
      return;
    }
    m->overrunTicks = 0;
    m->instrThisCall = 0;
  }

  // 2) Temporizadores vencidos -> eventos.
  uint32_t nowMs = elapsedMs(m);
  for(int i = 0; i < FLEXAPP_TIMERS; i++){
    FlexAppTimer& t = m->timers[i];
    if(!t.active) continue;
    if((int32_t)(nowMs - t.nextMs) < 0) continue;
    flexAppPostEvent(m, FLEXAPP_EV_TIMER, i, 0);
    if(t.repeat) t.nextMs = nowMs + t.periodMs;
    else t.active = 0;
  }

  // 3) Un evento por tick, y si no hay, onTick.
  if(m->qCount && flexVmHasHandler(&m->vm, FLEXVM_H_EVENT)){
    FlexAppEvent e = m->queue[m->qHead];
    m->qHead = (uint8_t)((m->qHead + 1) % FLEXAPP_EVENT_QUEUE);
    m->qCount--;
    int32_t args[3] = { (int32_t)e.type, e.a, e.b };
    m->instrThisCall = 0;
    if(flexVmCall(&m->vm, FLEXVM_H_EVENT, args, 3)){
      m->events++;
      if(!runBudgeted(m, m->instrPerTick, m->usPerTick)) return;
    }
  } else if(flexVmHasHandler(&m->vm, FLEXVM_H_TICK)){
    int32_t arg = (int32_t)nowMs;
    m->instrThisCall = 0;
    if(flexVmCall(&m->vm, FLEXVM_H_TICK, &arg, 1)){
      if(!runBudgeted(m, m->instrPerTick, m->usPerTick)) return;
    }
  }

  if(m->vm.running) m->overrunTicks++;
  else { m->overrunTicks = 0; m->instrThisCall = 0; }

  if(m->exitRequested) flexAppStop(m, FLEXAPP_STOP_USER);
}

void flexAppSuspend(FlexAppManager* m){
  if(!m || m->state != FLEXAPP_ST_RUNNING) return;
  // Un manejador a medias NO sobrevive a la suspensión: se aborta de forma
  // limpia. Volver de segundo plano no puede reanudar medio cuadro.
  if(m->vm.running) flexVmAbort(&m->vm, FLEXVM_TRAP_NONE);
  m->state = FLEXAPP_ST_SUSPENDED;
  flexAppPostEvent(m, FLEXAPP_EV_PAUSE, 0, 0);
  // En segundo plano no se conserva NADA privilegiado: ni pantalla exclusiva,
  // ni orientación forzada, ni PSRAM reservada, ni una carga de CPU en curso.
  const FlexAppHostApi* a = m->api;
  for(int i = 0; i < FLEXAPP_PSRAM_SLOTS; i++){
    if(m->psram[i] && a && a->psramRelease) a->psramRelease(m->user, m->psram[i]);
    m->psram[i] = 0;
  }
  m->psramKbHeld = 0;
  m->stressActive = 0;
  if(m->exclusiveApplied && a && a->setExclusive) a->setExclusive(m->user, false);
  m->exclusiveApplied = 0;
  if(m->landscapeApplied && a && a->setLandscape) a->setLandscape(m->user, false);
  m->landscapeApplied = 0;
  setReason(m, "En segundo plano");
}

void flexAppResume(FlexAppManager* m){
  if(!m || m->state != FLEXAPP_ST_SUSPENDED) return;
  m->state = FLEXAPP_ST_RUNNING;
  // La orientación que la app tenía pedida se vuelve a solicitar; si el
  // sistema dice que no, la app se entera por display.orientation().
  if(m->wantLandscape && m->api && m->api->setLandscape)
    m->landscapeApplied = m->api->setLandscape(m->user, true) ? 1 : 0;
  if(m->wantExclusive && m->api && m->api->setExclusive)
    m->exclusiveApplied = m->api->setExclusive(m->user, true) ? 1 : 0;
  m->drawUsed = 0;
  flexAppPostEvent(m, FLEXAPP_EV_RESUME, 0, 0);
  setReason(m, "En ejecucion");
}

void flexAppStop(FlexAppManager* m, FlexAppStopReason reason){
  if(!m) return;
  if(m->state == FLEXAPP_ST_UNINSTALLED) return;
  bool wasLive = (m->state == FLEXAPP_ST_RUNNING || m->state == FLEXAPP_ST_SUSPENDED ||
                  m->state == FLEXAPP_ST_STARTING);

  // onStop tiene su propio presupuesto, acotado. Si no termina, se corta: la
  // app no puede negarse a morir.
  if(wasLive && flexVmHasHandler(&m->vm, FLEXVM_H_STOP) && m->vm.code){
    if(m->vm.running) flexVmAbort(&m->vm, FLEXVM_TRAP_NONE);
    int32_t arg = (int32_t)reason;
    if(flexVmCall(&m->vm, FLEXVM_H_STOP, &arg, 1)){
      uint32_t used = 0;
      uint32_t left = kStopInstrBudget;
      while(left){
        uint32_t slice = left > kSlice ? kSlice : left;
        FlexVmRun r = flexVmRun(&m->vm, slice, &used);
        left -= (used < left) ? used : left;
        if(r == FLEXVM_DONE || r == FLEXVM_TRAP || r == FLEXVM_IDLE) break;
        if(m->vm.yielded) break;
        if(used == 0) break;
      }
      if(m->vm.running) flexVmAbort(&m->vm, FLEXVM_TRAP_HOST);
    }
  }

  releaseAll(m);
  flexVmReset(&m->vm);
  m->stopReason = (uint8_t)reason;
  m->exitRequested = 0;
  m->presentRequested = 0;
  m->drawUsed = 0; m->drawDropped = 0;
  m->instrThisCall = 0; m->instrThisTick = 0; m->overrunTicks = 0;
  m->granted = 0;

  if(reason == FLEXAPP_STOP_UNINSTALL) m->state = FLEXAPP_ST_UNINSTALLED;
  else if(reason == FLEXAPP_STOP_BUDGET) m->state = FLEXAPP_ST_STOPPED_SECURITY;
  else if(reason == FLEXAPP_STOP_TRAP)   m->state = FLEXAPP_ST_ERROR;
  else if(m->state != FLEXAPP_ST_ERROR && m->state != FLEXAPP_ST_STOPPED_SECURITY)
    m->state = FLEXAPP_ST_STOPPED;

  if(reason == FLEXAPP_STOP_USER)           setReason(m, "Cerrada por el usuario");
  else if(reason == FLEXAPP_STOP_SYSTEM)    setReason(m, "Cerrada por el sistema");
  else if(reason == FLEXAPP_STOP_CANCELLED) setReason(m, "Cancelada");
  else if(reason == FLEXAPP_STOP_UNINSTALL) setReason(m, "Desinstalada");
}

FlexAppState flexAppState(const FlexAppManager* m){
  return m ? (FlexAppState)m->state : FLEXAPP_ST_INSTALLED;
}

const char* flexAppStateName(uint8_t state){
  switch(state){
    case FLEXAPP_ST_INSTALLED:        return "Instalada";
    case FLEXAPP_ST_STOPPED:          return "Detenida";
    case FLEXAPP_ST_STARTING:         return "Iniciando";
    case FLEXAPP_ST_RUNNING:          return "Ejecutando";
    case FLEXAPP_ST_SUSPENDED:        return "Suspendida";
    case FLEXAPP_ST_STOPPED_SECURITY: return "Detenida por seguridad";
    case FLEXAPP_ST_ERROR:            return "Error";
    case FLEXAPP_ST_UNINSTALLED:      return "Desinstalada";
    default:                          return "Desconocido";
  }
}

const char* flexAppReason(const FlexAppManager* m){ return m ? m->reason : ""; }

bool flexAppWantsPresent(FlexAppManager* m){
  if(!m || !m->presentRequested) return false;
  m->presentRequested = 0;
  return true;
}

bool flexAppExitRequested(const FlexAppManager* m){ return m && m->exitRequested; }
uint32_t flexAppGranted(const FlexAppManager* m){ return m ? m->granted : 0; }
bool flexAppHasPermission(const FlexAppManager* m, uint32_t bit){
  return m && bit && (m->granted & bit) == bit;
}
