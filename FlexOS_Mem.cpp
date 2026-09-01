// #############################################################
//  FLEX OS · NUCLEO DE MEMORIA Y MULTITAREA  ·  implementacion
//  ------------------------------------------------------------
//  Ver la cabecera FlexOS_Mem.h para el porque. Aqui solo hay
//  aritmetica entera sobre una medida ya tomada: ni una reserva,
//  ni una llamada al SDK, ni un float en la ruta de decision.
// #############################################################
#include "FlexOS_Mem.h"
#include <stdio.h>
#include <string.h>

// -------------------------------------------------------------
//  Consultas
// -------------------------------------------------------------
uint32_t flexMemUsed(const FlexMemSnap* s){
  if(!s || s->psTotal == 0) return 0;
  return s->psFree >= s->psTotal ? 0u : (uint32_t)(s->psTotal - s->psFree);
}

int flexMemUsedPct(const FlexMemSnap* s){
  if(!s || s->psTotal == 0) return 0;
  uint64_t u = (uint64_t)flexMemUsed(s) * 100u / s->psTotal;
  return u > 100 ? 100 : (int)u;
}

uint32_t flexMemCost(int weight){
  if(weight == FLEXMEM_W_HEAVY)  return FLEXMEM_COST_HEAVY;
  if(weight == FLEXMEM_W_MEDIUM) return FLEXMEM_COST_MEDIUM;
  return FLEXMEM_COST_LIGHT;
}

// Bloque contiguo minimo exigible a cada clase de peso.
static uint32_t memBlockNeed(int weight){
  if(weight == FLEXMEM_W_HEAVY)  return FLEXMEM_BLOCK_HEAVY;
  if(weight == FLEXMEM_W_MEDIUM) return FLEXMEM_BLOCK_MEDIUM;
  return 0;                                   // una app ligera no pide un bloque grande
}

int flexMemLevel(const FlexMemSnap* s){
  if(!s || s->psTotal == 0) return FLEXMEM_LV_OK;   // placa sin PSRAM: esta escala no aplica
  // La SRAM interna puede mandar por si sola: sin ella no hay Wi-Fi ni tactil,
  // por muchos megas de PSRAM que queden.
  if(s->inTotal && s->inFree < FLEXMEM_SRAM_MIN_BYTES) return FLEXMEM_LV_CRITICAL;
  if(s->psFree < FLEXMEM_CRIT_BYTES)   return FLEXMEM_LV_CRITICAL;
  // Bloque contiguo insuficiente con MB libres: es el caso que el usuario
  // percibe como "no me abre la foto" aunque la barra diga que sobra memoria.
  if(s->psLargest < FLEXMEM_BLOCK_HEAVY) return FLEXMEM_LV_CRITICAL;
  if(s->psFree < FLEXMEM_WARN_BYTES)   return FLEXMEM_LV_WARN;
  if(s->psFree < FLEXMEM_NOTICE_BYTES) return FLEXMEM_LV_NOTICE;
  return FLEXMEM_LV_OK;
}

int flexMemHealth(const FlexMemSnap* s){
  switch(flexMemLevel(s)){
    case FLEXMEM_LV_CRITICAL: return FLEXMEM_H_CRIT;
    case FLEXMEM_LV_WARN:
    case FLEXMEM_LV_NOTICE:   return FLEXMEM_H_WATCH;
    default:                  return FLEXMEM_H_GOOD;
  }
}

// Fragmentacion = cuanto de la memoria libre NO esta en el mayor hueco.
// Con 8 MB libres y 8 MB de mayor bloque da 0; con 8 MB libres y 400 KB de
// mayor bloque da 95. Es la definicion que se le puede explicar al usuario.
int flexMemFragPct(const FlexMemSnap* s){
  if(!s || s->psFree == 0) return 0;
  if(s->psLargest >= s->psFree) return 0;
  uint64_t p = 100u - ((uint64_t)s->psLargest * 100u / s->psFree);
  return p > 100 ? 100 : (int)p;
}

int flexMemFragClass(const FlexMemSnap* s){
  int p = flexMemFragPct(s);
  if(p >= 65) return FLEXMEM_FRAG_HIGH;
  if(p >= 35) return FLEXMEM_FRAG_MED;
  return FLEXMEM_FRAG_LOW;
}

int flexMemFlashPct(const FlexMemSnap* s){
  if(!s || !s->fsValid || s->fsTotal == 0) return -1;
  uint64_t p = (uint64_t)s->fsUsed * 100u / s->fsTotal;
  return p > 100 ? 100 : (int)p;
}

// -------------------------------------------------------------
//  El veredicto
//  ------------------------------------------------------------
//  Orden de las comprobaciones, y por que ese orden:
//    1. SRAM interna. Es lo unico que puede tumbar Wi-Fi y tactil, y no
//       lo arregla soltar caches de PSRAM: si esta en zona peligrosa se
//       niega directamente, sin prometer que "optimizando" se arregla.
//    2. Suelo duro de PSRAM. Por debajo de 5 MB no se abre una app pesada
//       ni aunque se suelte todo -- ese es el margen que mantiene vivo el
//       doble buffer y las cargas repentinas.
//    3. Bloque contiguo. Puede arreglarse soltando caches grandes, asi
//       que primero se pide soltar (NEED_SHED) y solo despues se niega.
//    4. Colchon por clase de peso sobre la reserva del sistema.
// -------------------------------------------------------------
int flexMemCanOpen(const FlexMemSnap* s, int weight){
  if(!s || s->psTotal == 0) return FLEXMEM_OK;      // sin PSRAM medida no se inventa una politica

  if(weight < FLEXMEM_W_LIGHT)  weight = FLEXMEM_W_LIGHT;
  if(weight > FLEXMEM_W_HEAVY)  weight = FLEXMEM_W_HEAVY;

  // 1) SRAM interna
  if(s->inTotal && s->inFree < FLEXMEM_SRAM_MIN_BYTES && weight != FLEXMEM_W_LIGHT)
    return FLEXMEM_DENY_SRAM;

  // Una app ligera nunca se bloquea: Ajustes, Reloj o Notas son justo lo que el
  // usuario necesita PODER abrir cuando el sistema esta apurado.
  if(weight == FLEXMEM_W_LIGHT) return FLEXMEM_OK;

  // 2) suelo duro
  if(s->psFree < FLEXMEM_CRIT_BYTES) return FLEXMEM_DENY_PSRAM;

  // 3) bloque contiguo
  uint32_t need = memBlockNeed(weight);
  if(need && s->psLargest < need){
    // Con mucha memoria libre el problema es de REPARTO, no de cantidad:
    // soltar caches grandes puede devolver un hueco contiguo. El listo se
    // pone por encima de la reserva del sistema (6 MB) mas el bloque que
    // hace falta: por debajo de eso no hay nada que soltar que lo arregle,
    // y prometerlo seria inventar.
    if(s->psFree >= FLEXMEM_WARN_BYTES + need) return FLEXMEM_NEED_SHED;
    return FLEXMEM_DENY_BLOCK;
  }

  // 4) colchon. Abrir no puede dejar la PSRAM libre por debajo del suelo.
  uint32_t cost = flexMemCost(weight);
  if(s->psFree < cost + FLEXMEM_CRIT_BYTES) return FLEXMEM_NEED_SHED;

  // En la banda 5-6 MB una app PESADA exige soltar antes, aunque el colchon
  // salga: es la politica de la reserva de 6 MB del sistema.
  if(weight == FLEXMEM_W_HEAVY && s->psFree < FLEXMEM_WARN_BYTES) return FLEXMEM_NEED_SHED;

  return FLEXMEM_OK;
}

// -------------------------------------------------------------
//  Avisos
// -------------------------------------------------------------
void flexMemAlertsReset(FlexMemAlerts* a){
  if(!a) return;
  memset(a, 0, sizeof(*a));
}

static uint32_t memAlertCooldown(int kind){
  switch(kind){
    case FLEXMEM_AL_FRAG:     return FLEXMEM_CD_FRAG;
    case FLEXMEM_AL_FLASH80:
    case FLEXMEM_AL_FLASH90:  return FLEXMEM_CD_FLASH;
    case FLEXMEM_AL_SD_ERR:   return FLEXMEM_CD_SD;
    default:                  return FLEXMEM_CD_MEM;
  }
}

// true si 'kind' puede darse ahora (nunca dado, o ya vencio su enfriamiento).
static int memAlertReady(const FlexMemAlerts* a, int kind, uint32_t now){
  if(kind <= FLEXMEM_AL_NONE || kind >= FLEXMEM_AL_N) return 0;
  if(!(a->seen & (uint16_t)(1u << kind))) return 1;         // primera vez
  return (uint32_t)(now - a->lastMs[kind]) >= memAlertCooldown(kind);
}

int flexMemAlertPick(const FlexMemSnap* s, int sdErr, uint32_t nowMs, FlexMemAlerts* a){
  if(!s || !a) return FLEXMEM_AL_NONE;

  // Separacion global: dos avisos seguidos son ruido, aunque sean de cosas
  // distintas. La primera vez no se aplica (no hay nada de lo que separarse).
  if(a->lastAnySeen && (uint32_t)(nowMs - a->lastAnyMs) < FLEXMEM_CD_GLOBAL)
    return FLEXMEM_AL_NONE;

  // Candidatos, de mas grave a menos. Se elige el PRIMERO que ademas cumpla
  // su propio enfriamiento: asi un aviso critico ya dado no deja paso a uno
  // menor que solo sirve para insistir.
  int cand[FLEXMEM_AL_N];
  int n = 0;

  if(s->psTotal){
    if(s->psFree < FLEXMEM_CRIT_BYTES || s->psLargest < FLEXMEM_BLOCK_HEAVY)
      cand[n++] = FLEXMEM_AL_PS_CRIT;
    else if(s->psFree < FLEXMEM_WARN_BYTES)
      cand[n++] = FLEXMEM_AL_PS_WARN;
    else if(s->psFree < FLEXMEM_NOTICE_BYTES)
      cand[n++] = FLEXMEM_AL_PS_NOTICE;
  }
  if(s->inTotal && s->inFree < FLEXMEM_SRAM_LOW_BYTES) cand[n++] = FLEXMEM_AL_SRAM;
  // La fragmentacion solo se avisa si ademas hay memoria de sobra: con poca
  // memoria libre el aviso util es el de memoria, no el de reparto.
  if(s->psTotal && s->psFree >= FLEXMEM_WARN_BYTES && flexMemFragClass(s) == FLEXMEM_FRAG_HIGH)
    cand[n++] = FLEXMEM_AL_FRAG;
  if(sdErr) cand[n++] = FLEXMEM_AL_SD_ERR;
  {
    int fp = flexMemFlashPct(s);
    if(fp >= 90)      cand[n++] = FLEXMEM_AL_FLASH90;
    else if(fp >= 80) cand[n++] = FLEXMEM_AL_FLASH80;
  }

  for(int i = 0; i < n; i++){
    if(!memAlertReady(a, cand[i], nowMs)) continue;
    int k = cand[i];
    a->lastMs[k] = nowMs;
    a->seen |= (uint16_t)(1u << k);
    a->lastAnyMs = nowMs;
    a->lastAnySeen = 1;
    return k;
  }
  return FLEXMEM_AL_NONE;
}

// -------------------------------------------------------------
//  Formato
//  ------------------------------------------------------------
//  Un decimal a partir de 1 MB (8.4 MB dice mas que 8 MB cuando el
//  usuario esta mirando si le cabe algo) y ninguno por debajo.
//  Aritmetica entera: ni un float, para que valga tambien dentro de
//  una ruta de dibujo.
// -------------------------------------------------------------
void flexMemFmt(uint64_t bytes, char* out, size_t n){
  if(!out || n == 0) return;
  if(bytes >= 1024ull * 1024ull * 1024ull){
    uint64_t d = bytes * 10ull / (1024ull * 1024ull * 1024ull);
    snprintf(out, n, "%u.%u GB", (unsigned)(d / 10), (unsigned)(d % 10));
  } else if(bytes >= 1024ull * 1024ull){
    uint64_t d = bytes * 10ull / (1024ull * 1024ull);
    snprintf(out, n, "%u.%u MB", (unsigned)(d / 10), (unsigned)(d % 10));
  } else if(bytes >= 1024ull){
    snprintf(out, n, "%u KB", (unsigned)(bytes / 1024ull));
  } else {
    snprintf(out, n, "%u B", (unsigned)bytes);
  }
}

void flexMemFmtPair(uint64_t a, uint64_t b, char* out, size_t n){
  if(!out || n == 0) return;
  char sa[24], sb[24];
  flexMemFmt(a, sa, sizeof(sa));
  flexMemFmt(b, sb, sizeof(sb));
  snprintf(out, n, "%s / %s", sa, sb);
}
