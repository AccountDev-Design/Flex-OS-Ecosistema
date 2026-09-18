// #############################################################
// ##  FLEX PHONE TRANSPORT -- interfaz y cola de tramas
// ##  Ver FlexOS_FlexPhone_Transport.h.
// #############################################################
#include "FlexOS_FlexPhone_Transport.h"

const char* flexPhoneTransportName(uint8_t kind){
  switch(kind){
    case FLP_TR_WIFI: return "Wi-Fi";
    case FLP_TR_BLE:  return "Bluetooth LE";
    case FLP_TR_LOOP: return "lazo local";
    default:          return "ninguno";
  }
}

// =============================================================
//  Transporte nulo
// =============================================================
static bool     nullStart (FlexPhoneTransport* t){ (void)t; return false; }
static void     nullStop  (FlexPhoneTransport* t){ (void)t; }
static uint8_t  nullState (FlexPhoneTransport* t){ (void)t; return FLP_TC_DOWN; }
static uint16_t nullMtu   (FlexPhoneTransport* t){ (void)t; return 0; }
static int      nullSend  (FlexPhoneTransport* t, const uint8_t* f, size_t n){
  (void)t; (void)f; (void)n; return FLP_TR_ECLOSED;
}
static int      nullRecv  (FlexPhoneTransport* t, uint8_t* o, size_t c){
  (void)t; (void)o; (void)c; return FLP_TR_OK;
}
static const char* nullStatus(FlexPhoneTransport* t){
  (void)t; return "sin transporte configurado";
}
static void nullPeer(FlexPhoneTransport* t, char* out, size_t outN){
  (void)t; if(out && outN) out[0] = 0;
}

static FlexPhoneTransport gNullTr = {
  FLP_TR_NONE, nullStart, nullStop, nullState, nullMtu,
  nullSend, nullRecv, nullStatus, nullPeer, NULL,
};

FlexPhoneTransport* flexPhoneTransportNull(void){ return &gNullTr; }

// =============================================================
//  Ayudas seguras
// =============================================================
// Todas comprueban el puntero Y el metodo. Una implementacion
// puede dejar fuera lo que no necesite (p.ej. peer()) sin que el
// enlace tenga que saberlo.
bool flexPhoneTrStart(FlexPhoneTransport* t){
  return (t && t->start) ? t->start(t) : false;
}
void flexPhoneTrStop(FlexPhoneTransport* t){
  if(t && t->stop) t->stop(t);
}
uint8_t flexPhoneTrState(FlexPhoneTransport* t){
  return (t && t->state) ? t->state(t) : (uint8_t)FLP_TC_DOWN;
}
uint16_t flexPhoneTrMtu(FlexPhoneTransport* t){
  return (t && t->mtu) ? t->mtu(t) : (uint16_t)0;
}
int flexPhoneTrSend(FlexPhoneTransport* t, const uint8_t* frame, size_t n){
  if(!t || !t->send) return FLP_TR_ECLOSED;
  if(!frame || !n)   return FLP_TR_EBAD;
  return t->send(t, frame, n);
}
int flexPhoneTrRecv(FlexPhoneTransport* t, uint8_t* out, size_t cap){
  if(!t || !t->recv) return FLP_TR_OK;
  if(!out || !cap)   return FLP_TR_EBAD;
  return t->recv(t, out, cap);
}
const char* flexPhoneTrStatus(FlexPhoneTransport* t){
  const char* s = (t && t->status) ? t->status(t) : NULL;
  return s ? s : "";
}
void flexPhoneTrPeer(FlexPhoneTransport* t, char* out, size_t outN){
  if(!out || !outN) return;
  out[0] = 0;
  if(t && t->peer) t->peer(t, out, outN);
}

// =============================================================
//  Cola de tramas
// =============================================================
void flexRingInit(FlexFrameRing* r){
  if(!r) return;
  // Solo los indices y los contadores: memset de los 12 x 244 bytes
  // de carga no aporta nada y se paga en cada arranque del enlace.
  r->head = r->tail = 0;
  r->nPushed = r->nPopped = r->nDropped = 0;
  for(int i = 0; i < FLP_RING_SLOTS; i++) r->len[i] = 0;
}

bool flexRingEmpty(const FlexFrameRing* r){
  return !r || r->head == r->tail;
}

bool flexRingFull(const FlexFrameRing* r){
  if(!r) return true;
  return (uint8_t)((r->tail + 1) % FLP_RING_SLOTS) == r->head;
}

bool flexRingPush(FlexFrameRing* r, const uint8_t* frame, size_t n){
  if(!r || !frame || !n || n > FLP_RING_FRAME) return false;
  if(flexRingFull(r)){
    // Lleno: se tira la MAS ANTIGUA. Con un enlace que se recupera
    // tras un corte, lo viejo ya no describe nada util y lo nuevo si.
    r->head = (uint8_t)((r->head + 1) % FLP_RING_SLOTS);
    r->nDropped++;
  }
  memcpy(r->buf[r->tail], frame, n);
  r->len[r->tail] = (uint16_t)n;
  r->tail = (uint8_t)((r->tail + 1) % FLP_RING_SLOTS);
  r->nPushed++;
  return true;
}

size_t flexRingPop(FlexFrameRing* r, uint8_t* out, size_t cap){
  if(!r || !out || !cap || flexRingEmpty(r)) return 0;
  const size_t n = r->len[r->head];
  if(n > cap){
    // El lector no tiene sitio. Se descarta en vez de entregar media
    // trama: media trama solo consigue que el otro lado descarte por
    // CRC y no se entienda por que.
    r->head = (uint8_t)((r->head + 1) % FLP_RING_SLOTS);
    r->nDropped++;
    return 0;
  }
  memcpy(out, r->buf[r->head], n);
  r->head = (uint8_t)((r->head + 1) % FLP_RING_SLOTS);
  r->nPopped++;
  return n;
}
