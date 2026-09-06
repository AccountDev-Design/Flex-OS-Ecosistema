// #############################################################
// ##  FLEX LINK -- implementacion del nucleo puro
// ##  Ver FlexOS_FlexLink.h para el contrato y el porque.
// ##
// ##  REGLA DE ESTE FICHERO: todo tamano que venga del aire se
// ##  valida ANTES de usarse como indice o como longitud de copia.
// ##  No hay strcpy, no hay sprintf, no hay memcpy sin comprobar.
// #############################################################
#include "FlexOS_FlexLink.h"

// -------------------------------------------------------------
//  Magia. Dos bytes: descarta de inmediato el ruido de un
//  caracteristica GATT que no sea la nuestra.
// -------------------------------------------------------------
#define FLNK_MAGIC0 0xF1
#define FLNK_MAGIC1 0x58   // 'X'

const char* flexLinkErrName(uint8_t code){
  switch(code){
    case FLNK_E_NONE:      return "sin error";
    case FLNK_E_VERSION:   return "version incompatible";
    case FLNK_E_CRC:       return "datos corruptos";
    case FLNK_E_TOOBIG:    return "mensaje demasiado grande";
    case FLNK_E_BADFRAG:   return "fragmento invalido";
    case FLNK_E_TIMEOUT:   return "tiempo agotado";
    case FLNK_E_NOTPAIRED: return "dispositivo no emparejado";
    case FLNK_E_NOREPLY:   return "esta notificacion no admite respuesta";
    case FLNK_E_GONE:      return "la notificacion ya no existe";
    case FLNK_E_DENIED:    return "permiso denegado en el telefono";
    case FLNK_E_BUSY:      return "ocupado";
    default:               return "error interno";
  }
}

// =============================================================
//  CRC16-CCITT
// =============================================================
// Sin tabla: 244 bytes como mucho por trama, y la tabla de 512 B
// en flash no compensa frente a un bucle que el P4 hace en
// microsegundos.
uint16_t flexLinkCrc16(const uint8_t* data, size_t n){
  uint16_t crc = 0xFFFF;
  if(!data) return crc;
  for(size_t i = 0; i < n; i++){
    crc ^= (uint16_t)data[i] << 8;
    for(int b = 0; b < 8; b++)
      crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
  }
  return crc;
}

// -------------------------------------------------------------
//  Enteros al aire en little-endian, byte a byte. Nada de
//  reinterpret_cast sobre el buffer: la trama no esta alineada.
// -------------------------------------------------------------
static inline void wr16(uint8_t* p, uint16_t v){ p[0] = (uint8_t)(v & 0xFF); p[1] = (uint8_t)(v >> 8); }
static inline void wr32(uint8_t* p, uint32_t v){
  p[0] = (uint8_t)(v & 0xFF);       p[1] = (uint8_t)((v >> 8)  & 0xFF);
  p[2] = (uint8_t)((v >> 16) & 0xFF); p[3] = (uint8_t)((v >> 24) & 0xFF);
}
static inline uint16_t rd16(const uint8_t* p){ return (uint16_t)(p[0] | ((uint16_t)p[1] << 8)); }
static inline uint32_t rd32(const uint8_t* p){
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// =============================================================
//  Enmarcado
// =============================================================
// Disposicion (18 bytes de cabecera):
//   0    magia0        1    magia1
//   2    version       3    tipo
//   4-5  sesion        6-7  paquete
//   8    fragmento     9    total de fragmentos
//   10-11 longitud de carga
//   12-15 contador
//   16-17 CRC16 de [0..15] + carga
int flexLinkWriteFrame(uint8_t* out, size_t outN,
                       const FlexLinkHeader* h,
                       const uint8_t* payload, size_t payloadLen){
  if(!out || !h) return FLNK_ERR_LEN;
  if(payloadLen > FLNK_MAX_PAYLOAD) return FLNK_ERR_LEN;
  if(payloadLen && !payload)        return FLNK_ERR_LEN;
  if(h->fragCount == 0 || h->fragCount > FLNK_MAX_FRAGS) return FLNK_ERR_FRAG;
  if(h->frag >= h->fragCount)                            return FLNK_ERR_FRAG;
  const size_t total = FLNK_HDR_SIZE + payloadLen;
  if(total > outN || total > FLNK_MAX_FRAME) return FLNK_ERR_LEN;

  out[0] = FLNK_MAGIC0;
  out[1] = FLNK_MAGIC1;
  out[2] = h->version;
  out[3] = h->type;
  wr16(out + 4, h->session);
  wr16(out + 6, h->packet);
  out[8] = h->frag;
  out[9] = h->fragCount;
  wr16(out + 10, (uint16_t)payloadLen);
  wr32(out + 12, h->counter);
  if(payloadLen) memcpy(out + FLNK_HDR_SIZE, payload, payloadLen);
  // El CRC cubre la cabecera SIN sus propios dos bytes, mas la carga.
  // Se calcula en dos tramos para no necesitar un buffer temporal.
  uint16_t crc = flexLinkCrc16(out, 16);
  if(payloadLen){
    // Continuacion del CRC sobre la carga: se rehace el bucle con el
    // valor acumulado. Mismo polinomio, mismo orden de bytes.
    for(size_t i = 0; i < payloadLen; i++){
      crc ^= (uint16_t)payload[i] << 8;
      for(int b = 0; b < 8; b++)
        crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
  }
  wr16(out + 16, crc);
  return (int)total;
}

int flexLinkReadFrame(const uint8_t* in, size_t n,
                      FlexLinkHeader* out,
                      const uint8_t** payloadOut, size_t* payloadLenOut){
  if(!in || !out) return FLNK_ERR_SHORT;
  if(n < FLNK_HDR_SIZE)  return FLNK_ERR_SHORT;
  if(n > FLNK_MAX_FRAME) return FLNK_ERR_LEN;
  if(in[0] != FLNK_MAGIC0 || in[1] != FLNK_MAGIC1) return FLNK_ERR_MAGIC;

  const uint8_t  ver  = in[2];
  const uint16_t plen = rd16(in + 10);
  // La longitud declarada tiene que cuadrar EXACTAMENTE con lo
  // recibido. Un declarante que miente es la via clasica a una
  // lectura fuera de rango: aqui se rechaza antes de nada.
  if((size_t)plen + FLNK_HDR_SIZE != n) return FLNK_ERR_LEN;
  if(plen > FLNK_MAX_PAYLOAD)           return FLNK_ERR_LEN;

  const uint8_t frag  = in[8];
  const uint8_t fragN = in[9];
  if(fragN == 0 || fragN > FLNK_MAX_FRAGS) return FLNK_ERR_FRAG;
  if(frag >= fragN)                        return FLNK_ERR_FRAG;

  // CRC antes de dar por buena la version: una trama corrupta puede
  // tener cualquier cosa en el byte de version.
  uint16_t want = rd16(in + 16);
  uint16_t crc  = flexLinkCrc16(in, 16);
  for(size_t i = 0; i < plen; i++){
    crc ^= (uint16_t)in[FLNK_HDR_SIZE + i] << 8;
    for(int b = 0; b < 8; b++)
      crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
  }
  if(crc != want) return FLNK_ERR_CRC;

  // Compatibilidad hacia adelante: una version MAYOR no se
  // interpreta (el emisor recibira FLNK_E_VERSION). Una version
  // menor pero >= FLNK_VERSION_MIN si se acepta.
  if(ver < FLNK_VERSION_MIN || ver > FLNK_VERSION) return FLNK_ERR_VERSION;

  out->version   = ver;
  out->type      = in[3];
  out->session   = rd16(in + 4);
  out->packet    = rd16(in + 6);
  out->frag      = frag;
  out->fragCount = fragN;
  out->len       = plen;
  out->counter   = rd32(in + 12);
  if(payloadOut)    *payloadOut    = plen ? (in + FLNK_HDR_SIZE) : NULL;
  if(payloadLenOut) *payloadLenOut = plen;
  return FLNK_OK;
}

int flexLinkFragCount(size_t msgLen, size_t mtu){
  if(mtu < FLNK_MIN_MTU) return 0;
  if(mtu > FLNK_MAX_FRAME) mtu = FLNK_MAX_FRAME;
  const size_t per = mtu - FLNK_HDR_SIZE;
  if(per == 0) return 0;
  if(msgLen > FLNK_MAX_MESSAGE) return 0;
  if(msgLen == 0) return 1;               // un mensaje vacio sigue siendo un mensaje
  const size_t k = (msgLen + per - 1) / per;
  if(k > FLNK_MAX_FRAGS) return 0;
  return (int)k;
}

// =============================================================
//  Reensamblador
// =============================================================
void flexLinkReasmInit(FlexLinkReasm* r){
  if(!r) return;
  memset(r, 0, sizeof(*r));
}

int flexLinkReasmFeed(FlexLinkReasm* r, const FlexLinkHeader* h,
                      const uint8_t* payload, size_t payloadLen,
                      uint32_t nowMs){
  if(!r || !h) return FLNK_R_DROP;
  if(payloadLen > FLNK_MAX_PAYLOAD){ r->nTooBig++; return FLNK_R_DROP; }
  if(h->frag >= h->fragCount || h->fragCount > FLNK_MAX_FRAGS){
    r->nOutOfOrder++; return FLNK_R_DROP;
  }
  // El mensaje completo, en el PEOR caso, es fragCount tramas llenas.
  // Si eso no cabe en el buffer, se rechaza AQUI: nunca a mitad de
  // copia.
  if((size_t)h->fragCount * FLNK_MAX_PAYLOAD > FLNK_MAX_MESSAGE + FLNK_MAX_PAYLOAD){
    r->nTooBig++; return FLNK_R_DROP;
  }

  // Mensaje distinto al que estabamos montando: se abandona el
  // anterior. Un emisor no puede dejar parciales colgados.
  if(r->active && r->packet != h->packet){
    r->nAbandoned++;
    r->active = false;
  }
  if(!r->active){
    // Un mensaje solo puede EMPEZAR por su fragmento 0. Si llega un
    // fragmento suelto del medio (reordenado tras un abandono), no
    // hay forma de saber donde va: se descarta.
    if(h->frag != 0){ r->nOutOfOrder++; return FLNK_R_DROP; }
    r->active    = true;
    r->packet    = h->packet;
    r->fragCount = h->fragCount;
    r->seenMask  = 0;
    r->len       = 0;
    r->startedMs = nowMs;
  }
  if(h->fragCount != r->fragCount){ r->nOutOfOrder++; return FLNK_R_DROP; }

  const uint32_t bit = (uint32_t)1u << h->frag;
  if(r->seenMask & bit){ r->nDup++; return FLNK_R_DROP; }

  // Cada fragmento salvo el ULTIMO tiene que venir lleno; si no, no
  // se puede calcular el desplazamiento de los siguientes. Se toma
  // el tamano del fragmento 0 como referencia.
  const size_t off = (size_t)h->frag * (size_t)FLNK_MAX_PAYLOAD;
  if(off + payloadLen > FLNK_MAX_MESSAGE){ r->nTooBig++; return FLNK_R_DROP; }
  if(h->frag + 1 < h->fragCount && payloadLen != FLNK_MAX_PAYLOAD){
    // Fragmento intermedio a medias: el emisor no respeta el
    // troceado. Se descarta el mensaje entero.
    r->nBadCrc++; r->active = false; return FLNK_R_DROP;
  }
  if(payloadLen && payload) memcpy(r->buf + off, payload, payloadLen);
  r->seenMask |= bit;
  if(off + payloadLen > r->len) r->len = (uint16_t)(off + payloadLen);

  const uint32_t full = (r->fragCount >= 32) ? 0xFFFFFFFFu
                                             : (((uint32_t)1u << r->fragCount) - 1u);
  if((r->seenMask & full) == full){
    r->active = false;
    r->nOk++;
    return FLNK_R_DONE;
  }
  return FLNK_R_NEED_MORE;
}

bool flexLinkReasmExpire(FlexLinkReasm* r, uint32_t nowMs, uint32_t timeoutMs){
  if(!r || !r->active) return false;
  // Resta sin signo: sobrevive al desbordamiento de millis().
  if((uint32_t)(nowMs - r->startedMs) < timeoutMs) return false;
  r->active = false;
  r->nAbandoned++;
  return true;
}

// =============================================================
//  Ventana anti-repeticion
// =============================================================
void flexLinkAntiReplayInit(FlexLinkAntiReplay* a){
  if(!a) return;
  a->highest = 0; a->mask = 0; a->primed = false;
}

bool flexLinkAntiReplayCheck(FlexLinkAntiReplay* a, uint32_t counter){
  if(!a) return false;
  if(!a->primed){                 // primer paquete de la sesion: fija el suelo
    a->primed  = true;
    a->highest = counter;
    a->mask    = 0;
    return true;
  }
  if(counter == a->highest) return false;          // exactamente el ultimo: repetido
  if(counter > a->highest){
    const uint32_t adv = counter - a->highest;
    // Desplaza la ventana. Un salto de >=32 la vacia entera.
    a->mask = (adv >= 32) ? 0u : (uint32_t)((a->mask << adv) | ((uint32_t)1u << (adv - 1)));
    a->highest = counter;
    return true;
  }
  // Contador antiguo: solo se acepta si cae en la ventana y no se
  // habia visto. BLE puede reordenar; repetir no se permite.
  const uint32_t back = a->highest - counter;
  if(back > 32) return false;                       // demasiado viejo
  const uint32_t bit = (uint32_t)1u << (back - 1);
  if(a->mask & bit) return false;                   // ya visto
  a->mask |= bit;
  return true;
}

// =============================================================
//  Reintentos
// =============================================================
uint32_t flexLinkRetryDelayMs(uint8_t attempt){
  if(attempt >= FLNK_RETRY_MAX) return 0;           // rendirse
  uint32_t d = FLNK_RETRY_BASE_MS;
  for(uint8_t i = 0; i < attempt; i++){
    if(d >= FLNK_RETRY_CAP_MS / 2){ d = FLNK_RETRY_CAP_MS; break; }
    d *= 2;
  }
  return d > FLNK_RETRY_CAP_MS ? FLNK_RETRY_CAP_MS : d;
}

// =============================================================
//  Escritor con cursor
// =============================================================
void flexLinkWrInit(FlexLinkWr* w, uint8_t* buf, size_t n){
  if(!w) return;
  w->p = buf; w->n = buf ? n : 0; w->at = 0; w->ovf = (buf == NULL);
}
static bool wrFit(FlexLinkWr* w, size_t need){
  if(!w || w->ovf) return false;
  if(w->at + need > w->n){ w->ovf = true; return false; }
  return true;
}
void flexLinkWrU8(FlexLinkWr* w, uint8_t v){
  if(!wrFit(w, 1)) return;
  w->p[w->at++] = v;
}
void flexLinkWrU16(FlexLinkWr* w, uint16_t v){
  if(!wrFit(w, 2)) return;
  wr16(w->p + w->at, v); w->at += 2;
}
void flexLinkWrU32(FlexLinkWr* w, uint32_t v){
  if(!wrFit(w, 4)) return;
  wr32(w->p + w->at, v); w->at += 4;
}
void flexLinkWrBytes(FlexLinkWr* w, const void* src, size_t n){
  if(n == 0) return;
  if(!src){ if(w) w->ovf = true; return; }
  if(!wrFit(w, n)) return;
  memcpy(w->p + w->at, src, n); w->at += n;
}
void flexLinkWrStr(FlexLinkWr* w, const char* s, size_t maxLen){
  if(!w) return;
  if(maxLen > 255) maxLen = 255;
  size_t len = 0;
  if(s) len = flexLinkUtf8Trunc(s, maxLen);
  if(!wrFit(w, 1 + len)) return;
  w->p[w->at++] = (uint8_t)len;
  if(len){ memcpy(w->p + w->at, s, len); w->at += len; }
}
bool flexLinkWrOk(const FlexLinkWr* w){ return w && !w->ovf; }

// =============================================================
//  Lector con cursor
// =============================================================
void flexLinkRdInit(FlexLinkRd* r, const uint8_t* buf, size_t n){
  if(!r) return;
  r->p = buf; r->n = buf ? n : 0; r->at = 0; r->ovf = (buf == NULL);
}
static bool rdFit(FlexLinkRd* r, size_t need){
  if(!r || r->ovf) return false;
  if(r->at + need > r->n){ r->ovf = true; return false; }
  return true;
}
uint8_t flexLinkRdU8(FlexLinkRd* r){
  if(!rdFit(r, 1)) return 0;
  return r->p[r->at++];
}
uint16_t flexLinkRdU16(FlexLinkRd* r){
  if(!rdFit(r, 2)) return 0;
  uint16_t v = rd16(r->p + r->at); r->at += 2; return v;
}
uint32_t flexLinkRdU32(FlexLinkRd* r){
  if(!rdFit(r, 4)) return 0;
  uint32_t v = rd32(r->p + r->at); r->at += 4; return v;
}
void flexLinkRdBytes(FlexLinkRd* r, void* dst, size_t n){
  if(n == 0) return;
  if(!dst){ if(r) r->ovf = true; return; }
  if(!rdFit(r, n)){ memset(dst, 0, n); return; }
  memcpy(dst, r->p + r->at, n); r->at += n;
}
void flexLinkRdStr(FlexLinkRd* r, char* out, size_t outN){
  if(out && outN) out[0] = 0;
  if(!rdFit(r, 1)) return;
  const size_t len = r->p[r->at++];
  if(!rdFit(r, len)) return;                  // longitud que no cuadra: se marca ovf
  if(out && outN){
    // Se copia lo que quepa DEJANDO sitio al 0 final, y se recorta en
    // frontera UTF-8: el texto viene del telefono y puede ser
    // cualquier cosa.
    size_t room = outN - 1;
    size_t take = len < room ? len : room;
    take = flexLinkUtf8Trunc((const char*)(r->p + r->at), take);
    if(take) memcpy(out, r->p + r->at, take);
    out[take] = 0;
  }
  r->at += len;
}
bool flexLinkRdOk(const FlexLinkRd* r){ return r && !r->ovf; }

// =============================================================
//  UTF-8
// =============================================================
// Longitud esperada de la secuencia que empieza por `c`. 0 = byte
// de continuacion o inicio invalido.
static inline int u8Len(uint8_t c){
  if(c < 0x80) return 1;
  if((c & 0xE0) == 0xC0) return 2;
  if((c & 0xF0) == 0xE0) return 3;
  if((c & 0xF8) == 0xF0) return 4;
  return 0;
}

size_t flexLinkUtf8Trunc(const char* src, size_t maxBytes){
  if(!src || maxBytes == 0) return 0;
  size_t i = 0;
  while(src[i] && i < maxBytes){
    const int need = u8Len((uint8_t)src[i]);
    if(need <= 0) break;                       // secuencia invalida: se corta aqui
    if(i + (size_t)need > maxBytes) break;      // no cabe entera: no se parte
    // Todos los bytes siguientes tienen que ser de continuacion.
    bool ok = true;
    for(int k = 1; k < need; k++){
      if(((uint8_t)src[i + k] & 0xC0) != 0x80){ ok = false; break; }
    }
    if(!ok) break;
    i += (size_t)need;
  }
  return i;
}

size_t flexLinkUtf8Copy(char* out, size_t outN, const char* src){
  if(!out || outN == 0) return 0;
  out[0] = 0;
  if(!src) return 0;
  const size_t take = flexLinkUtf8Trunc(src, outN - 1);
  if(take) memcpy(out, src, take);
  out[take] = 0;
  return take;
}
