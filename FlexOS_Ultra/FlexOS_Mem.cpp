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
//  Nivel con histeresis
//  ------------------------------------------------------------
//  Empeorar es inmediato. Mejorar exige rebasar el umbral de SALIDA del
//  nivel en el que se esta AHORA -- no el de entrada del nivel de abajo --,
//  que es lo que crea la banda muerta y mata el parpadeo.
// -------------------------------------------------------------
int flexMemLevelHyst(const FlexMemSnap* s, int prevLevel){
  int raw = flexMemLevel(s);
  if(!s || s->psTotal == 0) return raw;
  if(prevLevel < FLEXMEM_LV_OK || prevLevel > FLEXMEM_LV_CRITICAL) return raw;
  if(raw >= prevLevel) return raw;                 // empeora (o sigue igual): inmediato

  // Mejorar: la condicion que nos metio aqui tiene que haberse despejado con
  // margen. Se comprueban las TRES por las que se entra a CRITICO, porque
  // cualquiera de ellas basta para quedarse.
  switch(prevLevel){
    case FLEXMEM_LV_CRITICAL:
      if(s->psFree < FLEXMEM_EXIT_CRIT_BYTES)            return FLEXMEM_LV_CRITICAL;
      if(s->psLargest < FLEXMEM_EXIT_BLOCK_BYTES)        return FLEXMEM_LV_CRITICAL;
      if(s->inTotal && s->inFree < FLEXMEM_EXIT_SRAM_BYTES) return FLEXMEM_LV_CRITICAL;
      break;
    case FLEXMEM_LV_WARN:
      if(s->psFree < FLEXMEM_EXIT_WARN_BYTES)            return FLEXMEM_LV_WARN;
      break;
    case FLEXMEM_LV_NOTICE:
      if(s->psFree < FLEXMEM_EXIT_NOTICE_BYTES)          return FLEXMEM_LV_NOTICE;
      break;
    default: break;
  }
  // SE BAJA DE UNO EN UNO. Sin esto queda un hueco: el umbral de SALIDA de
  // CRITICO (6 MB) coincide con el de ENTRADA a WARN, asi que al recuperarse
  // hasta 6,06 MB se saltaba a NOTICE... y el primer repintado que bajara a
  // 5,94 MB volvia a ser un EMPEORAMIENTO -- y con el, una notificacion. Es
  // exactamente el parpadeo que la histeresis existe para evitar, solo que
  // desplazado un nivel. Bajando un escalon por evaluacion, salir de CRITICO
  // deja en WARN, y de WARN solo se sale por encima de 7 MB.
  //
  // No cuesta tiempo perceptible: cada vuelta de loop() evalua una vez, asi
  // que recuperarse del todo son tres vueltas -- milisegundos.
  if(raw < prevLevel - 1) return prevLevel - 1;
  return raw;
}

int flexMemLevelStep(const FlexMemSnap* s, FlexMemAlerts* a){
  if(!a) return flexMemLevel(s);
  int prev = a->levelSeen ? (int)a->level : FLEXMEM_LV_OK;
  int lv   = a->levelSeen ? flexMemLevelHyst(s, prev) : flexMemLevel(s);
  a->rose      = (a->levelSeen && lv > prev) ? 1 : 0;
  a->level     = (uint8_t)lv;
  a->levelSeen = 1;
  return lv;
}

int flexMemReliefDue(const FlexMemAlerts* a, uint32_t nowMs){
  if(!a) return 0;
  if(!a->levelSeen || a->level < FLEXMEM_LV_WARN) return 0;   // solo con el sistema apretado
  if(!a->reliefSeen) return 1;                                // nunca se alivio: toca
  return (uint32_t)(nowMs - a->reliefMs) >= FLEXMEM_RELIEF_MS;
}
void flexMemReliefDone(FlexMemAlerts* a, uint32_t nowMs){
  if(!a) return;
  a->reliefMs = nowMs;
  a->reliefSeen = 1;
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
    default:                  return FLEXMEM_CD_MEM;
  }
}

// true si 'kind' puede darse ahora (nunca dado, o ya vencio su enfriamiento).
static int memAlertReady(const FlexMemAlerts* a, int kind, uint32_t now){
  if(kind <= FLEXMEM_AL_NONE || kind >= FLEXMEM_AL_N) return 0;
  if(!(a->seen & (uint16_t)(1u << kind))) return 1;         // primera vez
  return (uint32_t)(now - a->lastMs[kind]) >= memAlertCooldown(kind);
}

int flexMemAlertPick(const FlexMemSnap* s, uint32_t nowMs, FlexMemAlerts* a){
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

  // Los de PSRAM NO se generan aqui (ver la cabecera): mientras la condicion
  // dura, repetir el mismo aviso solo seria insistir. De eso se encarga la
  // transicion de nivel, que ocurre UNA vez.
  if(s->inTotal && s->inFree < FLEXMEM_SRAM_LOW_BYTES) cand[n++] = FLEXMEM_AL_SRAM;
  // La fragmentacion solo se avisa si ademas hay memoria de sobra: con poca
  // memoria libre el aviso util es el de memoria, no el de reparto.
  if(s->psTotal && s->psFree >= FLEXMEM_WARN_BYTES && flexMemFragClass(s) == FLEXMEM_FRAG_HIGH)
    cand[n++] = FLEXMEM_AL_FRAG;
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
