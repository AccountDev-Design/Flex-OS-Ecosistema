// #############################################################
// ##  FLEX PHONE -- modelo de datos: implementacion
// ##  Ver FlexOS_FlexPhone.h para el contrato.
// #############################################################
#include "FlexOS_FlexPhone.h"

// Copia segura a un campo de tamano fijo. Trunca en frontera UTF-8
// y SIEMPRE termina en 0. Es la unica forma de meter texto en el
// modelo: no hay strcpy en este fichero.
static inline void setStr(char* dst, size_t dstN, const char* src){
  flexLinkUtf8Copy(dst, dstN, src);
}

void flexPhoneModelInit(FlexPhoneModel* m){
  if(!m) return;
  memset(m, 0, sizeof(*m));
  m->phone.battery = 255;              // 255 = desconocido, no "0%"
  m->priv.hideBodyOnLock = true;       // por defecto, lo prudente
  m->priv.hideSensitive  = true;
  m->priv.keepHistory    = true;
  m->priv.keepHours      = 48;
}

void flexPhoneModelClear(FlexPhoneModel* m, bool wipeDrafts){
  if(!m) return;
  memset(m->notif, 0, sizeof(m->notif));
  memset(m->conv,  0, sizeof(m->conv));
  memset(&m->phone, 0, sizeof(m->phone));
  memset(&m->media, 0, sizeof(m->media));
  memset(&m->relay, 0, sizeof(m->relay));
  m->phone.battery = 255;
  m->notifCount = 0;
  m->lastSyncMs = 0;
  if(wipeDrafts) memset(m->draft, 0, sizeof(m->draft));
  m->dirty = true;
}

// =============================================================
//  Notificaciones
// =============================================================
int flexPhoneNotifFind(const FlexPhoneModel* m, uint32_t id){
  if(!m) return -1;
  for(int i = 0; i < FLP_NOTIF_MAX; i++)
    if(m->notif[i].used && m->notif[i].id == id) return i;
  return -1;
}

int flexPhoneNotifCountPkg(const FlexPhoneModel* m, const char* pkg){
  if(!m || !pkg) return 0;
  int k = 0;
  for(int i = 0; i < FLP_NOTIF_MAX; i++)
    if(m->notif[i].used && strncmp(m->notif[i].pkg, pkg, FLP_PKG_MAX) == 0) k++;
  return k;
}

// Politica de expulsion: la mas ANTIGUA de la prioridad MAS BAJA
// presente. Recorre una vez buscando el minimo (prioridad, rxMs).
// No expulsa una urgente mientras quede cualquier otra cosa.
int flexPhoneNotifEvict(FlexPhoneModel* m){
  if(!m) return -1;
  int best = -1;
  uint8_t bestPri = 255;
  uint32_t bestMs = 0;
  for(int i = 0; i < FLP_NOTIF_MAX; i++){
    if(!m->notif[i].used) continue;
    const uint8_t p = m->notif[i].pri;
    const uint32_t t = m->notif[i].rxMs;
    if(best < 0 || p < bestPri || (p == bestPri && t < bestMs)){
      best = i; bestPri = p; bestMs = t;
    }
  }
  if(best < 0) return -1;
  memset(&m->notif[best], 0, sizeof(m->notif[best]));
  if(m->notifCount) m->notifCount--;
  m->stats.evicted++;
  return best;
}

int flexPhoneNotifPut(FlexPhoneModel* m, const FlexPhoneNotif* n, uint32_t nowMs){
  if(!m || !n) return -1;
  // Una notificacion sin paquete no es identificable: se rechaza en
  // vez de guardar una fila anonima que la interfaz no sabe agrupar.
  if(!n->pkg[0]) return -1;

  int idx = flexPhoneNotifFind(m, n->id);
  if(idx >= 0){                              // ACTUALIZACION en sitio
    FlexPhoneNotif* d = &m->notif[idx];
    const uint32_t born = d->rxMs;
    *d = *n;
    d->used = true;
    d->rxMs = born;                          // conserva su sitio en el orden
    m->stats.rxUpdate++;
    m->dirty = true;
    return idx;
  }

  // Hueco libre; si no hay, se expulsa.
  int free_ = -1;
  for(int i = 0; i < FLP_NOTIF_MAX; i++) if(!m->notif[i].used){ free_ = i; break; }
  if(free_ < 0){
    m->stats.queueFull++;
    free_ = flexPhoneNotifEvict(m);
    if(free_ < 0) return -1;
  }
  m->notif[free_] = *n;
  m->notif[free_].used = true;
  m->notif[free_].rxMs = nowMs;
  m->notifCount++;
  m->stats.rxNotif++;
  m->dirty = true;
  return free_;
}

bool flexPhoneNotifRemove(FlexPhoneModel* m, uint32_t id){
  const int i = flexPhoneNotifFind(m, id);
  if(i < 0) return false;
  memset(&m->notif[i], 0, sizeof(m->notif[i]));
  if(m->notifCount) m->notifCount--;
  m->stats.rxRemove++;
  m->dirty = true;
  return true;
}

void flexPhoneNotifClearAll(FlexPhoneModel* m){
  if(!m) return;
  memset(m->notif, 0, sizeof(m->notif));
  m->notifCount = 0;
  m->dirty = true;
}

int flexPhoneNotifExpire(FlexPhoneModel* m, uint32_t nowMs, uint32_t maxAgeMs){
  if(!m || maxAgeMs == 0) return 0;
  int k = 0;
  for(int i = 0; i < FLP_NOTIF_MAX; i++){
    if(!m->notif[i].used) continue;
    // Resta sin signo: correcta aunque millis() haya dado la vuelta.
    if((uint32_t)(nowMs - m->notif[i].rxMs) >= maxAgeMs){
      memset(&m->notif[i], 0, sizeof(m->notif[i]));
      if(m->notifCount) m->notifCount--;
      k++;
    }
  }
  if(k) m->dirty = true;
  return k;
}

bool flexPhoneNotifBody(const FlexPhoneModel* m, const FlexPhoneNotif* n,
                        bool onLockScreen, char* out, size_t outN){
  if(out && outN) out[0] = 0;
  if(!m || !n || !out || outN == 0) return false;
  // Sensible (OTP, banca) con la opcion puesta: no se enseria NUNCA,
  // ni desbloqueado. Es la regla mas fuerte y va primero.
  if(n->sensitive && m->priv.hideSensitive) return false;
  // El telefono ya mando el cuerpo oculto: no hay nada que enseriar.
  if(n->hidden) return false;
  // En bloqueo, si el usuario eligio ocultar el cuerpo, solo queda
  // app y remitente -- que los pinta la interfaz, no esta funcion.
  if(onLockScreen && m->priv.hideBodyOnLock) return false;
  if(!n->text[0]) return false;
  flexLinkUtf8Copy(out, outN, n->text);
  return out[0] != 0;
}

// =============================================================
//  Conversaciones y borradores
// =============================================================
int flexPhoneConvFind(const FlexPhoneModel* m, const char* pkg, const char* who){
  if(!m || !pkg || !who) return -1;
  for(int i = 0; i < FLP_CONV_MAX; i++){
    if(!m->conv[i].used) continue;
    if(strncmp(m->conv[i].pkg, pkg, FLP_PKG_MAX) == 0 &&
       strncmp(m->conv[i].who, who, FLP_TITLE_MAX) == 0) return i;
  }
  return -1;
}

void flexPhoneConvRebuild(FlexPhoneModel* m){
  if(!m) return;
  memset(m->conv, 0, sizeof(m->conv));
  for(int i = 0; i < FLP_NOTIF_MAX; i++){
    const FlexPhoneNotif* n = &m->notif[i];
    // Solo entran las que de verdad se pueden contestar: si Android
    // no dio RemoteInput, no hay conversacion que ofrecer.
    if(!n->used || !n->canReply || !n->title[0]) continue;
    int c = flexPhoneConvFind(m, n->pkg, n->title);
    if(c < 0){
      for(int j = 0; j < FLP_CONV_MAX; j++) if(!m->conv[j].used){ c = j; break; }
      if(c < 0){
        // Sin sitio: se queda la mas reciente. Se busca la mas
        // antigua y se reemplaza solo si esta es posterior.
        int oldest = 0;
        for(int j = 1; j < FLP_CONV_MAX; j++)
          if(m->conv[j].lastMs < m->conv[oldest].lastMs) oldest = j;
        if(m->conv[oldest].lastMs >= n->rxMs) continue;
        c = oldest;
      }
      memset(&m->conv[c], 0, sizeof(m->conv[c]));
      setStr(m->conv[c].pkg, FLP_PKG_MAX,   n->pkg);
      setStr(m->conv[c].who, FLP_TITLE_MAX, n->title);
      m->conv[c].used = true;
    }
    if(n->rxMs >= m->conv[c].lastMs){
      m->conv[c].lastMs      = n->rxMs;
      m->conv[c].lastNotifId = n->id;
      m->conv[c].canReply    = true;
    }
  }
}

int flexPhoneDraftFind(const FlexPhoneModel* m, const char* pkg, const char* who){
  if(!m || !pkg || !who) return -1;
  for(int i = 0; i < FLP_DRAFT_MAX; i++){
    if(!m->draft[i].used) continue;
    if(strncmp(m->draft[i].pkg, pkg, FLP_PKG_MAX) == 0 &&
       strncmp(m->draft[i].who, who, FLP_TITLE_MAX) == 0) return i;
  }
  return -1;
}

int flexPhoneDraftPut(FlexPhoneModel* m, const char* pkg, const char* who,
                      const char* text, uint32_t nowMs){
  if(!m || !pkg || !who || !text) return -1;
  int i = flexPhoneDraftFind(m, pkg, who);
  if(i < 0){
    for(int j = 0; j < FLP_DRAFT_MAX; j++) if(!m->draft[j].used){ i = j; break; }
    if(i < 0){                       // sin sitio: se pisa el mas antiguo
      i = 0;
      for(int j = 1; j < FLP_DRAFT_MAX; j++)
        if(m->draft[j].savedMs < m->draft[i].savedMs) i = j;
    }
    memset(&m->draft[i], 0, sizeof(m->draft[i]));
    setStr(m->draft[i].pkg, FLP_PKG_MAX,   pkg);
    setStr(m->draft[i].who, FLP_TITLE_MAX, who);
    m->draft[i].used = true;
  }
  setStr(m->draft[i].text, FLP_REPLY_MAX, text);
  m->draft[i].savedMs = nowMs;
  m->dirty = true;
  return i;
}

bool flexPhoneDraftRemove(FlexPhoneModel* m, int idx){
  if(!m || idx < 0 || idx >= FLP_DRAFT_MAX || !m->draft[idx].used) return false;
  memset(&m->draft[idx], 0, sizeof(m->draft[idx]));
  m->dirty = true;
  return true;
}

// =============================================================
//  Tabla local de iconos
// =============================================================
// NO se envian iconos por BLE (un PNG por notificacion arruinaria
// el enlace). Aqui hay una tabla de los paquetes mas comunes; lo
// que no este cae al icono generico o al de su categoria.
typedef struct { const char* pkg; uint8_t icon; } FlpIconRow;
static const FlpIconRow FLP_ICONS[] = {
  { "com.whatsapp",                         FLP_ICON_CHAT   },
  { "com.whatsapp.w4b",                     FLP_ICON_CHAT   },
  { "org.telegram.messenger",               FLP_ICON_CHAT   },
  { "com.facebook.orca",                    FLP_ICON_CHAT   },
  { "org.thoughtcrime.securesms",           FLP_ICON_CHAT   },
  { "com.google.android.apps.messaging",    FLP_ICON_CHAT   },
  { "com.android.mms",                      FLP_ICON_CHAT   },
  { "com.discord",                          FLP_ICON_CHAT   },
  { "com.google.android.gm",                FLP_ICON_MAIL   },
  { "com.microsoft.office.outlook",         FLP_ICON_MAIL   },
  { "com.android.dialer",                   FLP_ICON_CALL   },
  { "com.google.android.dialer",            FLP_ICON_CALL   },
  { "com.instagram.android",                FLP_ICON_SOCIAL },
  { "com.twitter.android",                  FLP_ICON_SOCIAL },
  { "com.facebook.katana",                  FLP_ICON_SOCIAL },
  { "com.spotify.music",                    FLP_ICON_MUSIC  },
  { "com.google.android.youtube",           FLP_ICON_MUSIC  },
  { "com.google.android.apps.youtube.music",FLP_ICON_MUSIC  },
  { "com.google.android.calendar",          FLP_ICON_CALENDAR },
  { "com.google.android.deskclock",         FLP_ICON_ALARM  },
};
#define FLP_ICON_ROWS ((int)(sizeof(FLP_ICONS)/sizeof(FLP_ICONS[0])))

uint8_t flexPhoneIconFor(const char* pkg, uint8_t cat){
  if(pkg && pkg[0]){
    for(int i = 0; i < FLP_ICON_ROWS; i++)
      if(strncmp(pkg, FLP_ICONS[i].pkg, FLP_PKG_MAX) == 0) return FLP_ICONS[i].icon;
  }
  // Sin coincidencia exacta: la categoria que dio Android es mejor
  // pista que el generico.
  switch(cat){
    case FLP_CAT_MSG:    return FLP_ICON_CHAT;
    case FLP_CAT_EMAIL:  return FLP_ICON_MAIL;
    case FLP_CAT_CALL:   return FLP_ICON_CALL;
    case FLP_CAT_SOCIAL: return FLP_ICON_SOCIAL;
    case FLP_CAT_TRANSPORT: return FLP_ICON_MUSIC;
    case FLP_CAT_ALARM:  return FLP_ICON_ALARM;
    default:             return FLP_ICON_GENERIC;
  }
}

const char* flexPhoneIconName(uint8_t icon){
  switch(icon){
    case FLP_ICON_CHAT:     return "chat";
    case FLP_ICON_MAIL:     return "correo";
    case FLP_ICON_CALL:     return "llamada";
    case FLP_ICON_SOCIAL:   return "social";
    case FLP_ICON_MUSIC:    return "musica";
    case FLP_ICON_CALENDAR: return "calendario";
    case FLP_ICON_ALARM:    return "alarma";
    case FLP_ICON_BANK:     return "banco";
    case FLP_ICON_SHOP:     return "compras";
    default:                return "generico";
  }
}

// =============================================================
//  Codecs
// =============================================================
// Disposicion de FLNK_T_NOTIF_ADD / _UPDATE:
//   u32 id · str pkg · str app · str title · str text · u32 when
//   u8 cat · u8 pri · u8 flags · u8 replyAction · u8 nActions
//   nActions x str label
// flags: bit0 hidden · bit1 sensitive · bit2 canReply
int flexPhoneEncNotif(uint8_t* out, size_t outN, const FlexPhoneNotif* n){
  if(!out || !n) return -1;
  FlexLinkWr w; flexLinkWrInit(&w, out, outN);
  flexLinkWrU32(&w, n->id);
  flexLinkWrStr(&w, n->pkg,   FLP_PKG_MAX     - 1);
  flexLinkWrStr(&w, n->app,   FLP_APPNAME_MAX - 1);
  flexLinkWrStr(&w, n->title, FLP_TITLE_MAX   - 1);
  flexLinkWrStr(&w, n->text,  FLP_TEXT_MAX    - 1);
  flexLinkWrU32(&w, n->whenMs);
  flexLinkWrU8(&w, n->cat);
  flexLinkWrU8(&w, n->pri);
  uint8_t flags = 0;
  if(n->hidden)    flags |= 0x01;
  if(n->sensitive) flags |= 0x02;
  if(n->canReply)  flags |= 0x04;
  flexLinkWrU8(&w, flags);
  flexLinkWrU8(&w, n->replyAction);
  const uint8_t k = n->actionCount > FLP_ACTION_MAX ? FLP_ACTION_MAX : n->actionCount;
  flexLinkWrU8(&w, k);
  for(uint8_t i = 0; i < k; i++)
    flexLinkWrStr(&w, n->actions[i], FLP_ACTLABEL_MAX - 1);
  return flexLinkWrOk(&w) ? (int)w.at : -1;
}

bool flexPhoneDecNotif(const uint8_t* in, size_t n, FlexPhoneNotif* out){
  if(!in || !out) return false;
  memset(out, 0, sizeof(*out));
  FlexLinkRd r; flexLinkRdInit(&r, in, n);
  out->id = flexLinkRdU32(&r);
  flexLinkRdStr(&r, out->pkg,   FLP_PKG_MAX);
  flexLinkRdStr(&r, out->app,   FLP_APPNAME_MAX);
  flexLinkRdStr(&r, out->title, FLP_TITLE_MAX);
  flexLinkRdStr(&r, out->text,  FLP_TEXT_MAX);
  out->whenMs = flexLinkRdU32(&r);
  out->cat = flexLinkRdU8(&r);
  out->pri = flexLinkRdU8(&r);
  const uint8_t flags = flexLinkRdU8(&r);
  out->hidden      = (flags & 0x01) != 0;
  out->sensitive   = (flags & 0x02) != 0;
  out->canReply    = (flags & 0x04) != 0;
  out->replyAction = flexLinkRdU8(&r);
  uint8_t k = flexLinkRdU8(&r);
  if(k > FLP_ACTION_MAX) k = FLP_ACTION_MAX;   // se recorta, no se confia
  for(uint8_t i = 0; i < k; i++)
    flexLinkRdStr(&r, out->actions[i], FLP_ACTLABEL_MAX);
  out->actionCount = k;
  if(!flexLinkRdOk(&r)) return false;          // longitudes que no cuadran
  // Saneado: valores fuera de rango se normalizan en vez de
  // propagarse a un indice de tabla.
  if(out->cat >= FLP_CAT_N)     out->cat = FLP_CAT_OTHER;
  if(out->pri > FLP_PRI_URGENT) out->pri = FLP_PRI_DEFAULT;
  // Coherencia: no se puede responder a una accion que no existe.
  if(out->canReply && out->replyAction >= out->actionCount) out->canReply = false;
  if(!out->pkg[0]) return false;
  out->used = true;
  return true;
}

// FLNK_T_REPLY_REQ: u32 id · u8 action · str text
int flexPhoneEncReply(uint8_t* out, size_t outN, uint32_t notifId,
                      uint8_t action, const char* text){
  if(!out || !text) return -1;
  FlexLinkWr w; flexLinkWrInit(&w, out, outN);
  flexLinkWrU32(&w, notifId);
  flexLinkWrU8(&w, action);
  flexLinkWrStr(&w, text, FLP_REPLY_MAX - 1 > 255 ? 255 : FLP_REPLY_MAX - 1);
  return flexLinkWrOk(&w) ? (int)w.at : -1;
}

// FLNK_T_REPLY_RESULT: u32 id · u8 codigo FLNK_E_*
bool flexPhoneDecReplyResult(const uint8_t* in, size_t n,
                             uint32_t* notifId, uint8_t* errCode){
  if(!in) return false;
  FlexLinkRd r; flexLinkRdInit(&r, in, n);
  const uint32_t id = flexLinkRdU32(&r);
  const uint8_t  ec = flexLinkRdU8(&r);
  if(!flexLinkRdOk(&r)) return false;
  if(notifId) *notifId = id;
  if(errCode) *errCode = ec;
  return true;
}

// FLNK_T_PHONE_STATE: str nombre · u8 bateria · u8 flags · u8 red
bool flexPhoneDecPhoneState(const uint8_t* in, size_t n, FlexPhoneState* out){
  if(!in || !out) return false;
  FlexPhoneState s; memset(&s, 0, sizeof(s));
  FlexLinkRd r; flexLinkRdInit(&r, in, n);
  flexLinkRdStr(&r, s.name, FLP_DEVNAME_MAX);
  s.battery = flexLinkRdU8(&r);
  const uint8_t flags = flexLinkRdU8(&r);
  s.charging = (flags & 0x01) != 0;
  s.net = flexLinkRdU8(&r);
  if(!flexLinkRdOk(&r)) return false;
  if(s.battery > 100 && s.battery != 255) s.battery = 255;   // desconocido
  if(s.net > FLP_NET_MOBILE) s.net = FLP_NET_UNKNOWN;
  s.valid = true;
  *out = s;
  return true;
}

// FLNK_T_MEDIA_STATE: str titulo · str artista · str app · u8 estado
bool flexPhoneDecMedia(const uint8_t* in, size_t n, FlexPhoneMedia* out){
  if(!in || !out) return false;
  FlexPhoneMedia s; memset(&s, 0, sizeof(s));
  FlexLinkRd r; flexLinkRdInit(&r, in, n);
  flexLinkRdStr(&r, s.title,  FLP_MEDIA_TXT_MAX);
  flexLinkRdStr(&r, s.artist, FLP_MEDIA_TXT_MAX);
  flexLinkRdStr(&r, s.app,    FLP_APPNAME_MAX);
  s.status = flexLinkRdU8(&r);
  if(!flexLinkRdOk(&r)) return false;
  if(s.status > FLP_MEDIA_PAUSED) s.status = FLP_MEDIA_STOP;
  // Sin titulo no hay nada que enseriar: se marca invalido en vez de
  // pintar una tarjeta multimedia vacia.
  s.valid = s.title[0] != 0;
  *out = s;
  return true;
}

// FLNK_T_RELAY_INFO: u8 ip[4] · u16 puerto · u8 ver · u8 flags · u16 caps · str error
bool flexPhoneDecRelayInfo(const uint8_t* in, size_t n, FlexPhoneRelay* out){
  if(!in || !out) return false;
  FlexPhoneRelay s; memset(&s, 0, sizeof(s));
  FlexLinkRd r; flexLinkRdInit(&r, in, n);
  flexLinkRdBytes(&r, s.ip, 4);
  s.port     = flexLinkRdU16(&r);
  s.protoVer = flexLinkRdU8(&r);
  const uint8_t flags = flexLinkRdU8(&r);
  s.tls  = (flags & 0x01) != 0;
  s.caps = flexLinkRdU16(&r);
  flexLinkRdStr(&r, s.err, sizeof(s.err));
  if(!flexLinkRdOk(&r)) return false;
  // Un relay sin puerto o sin IP no esta arriba, diga lo que diga el
  // byte de estado: se refleja como error, nunca como "conectado".
  if(s.err[0])                     s.state = FLP_RELAY_ERROR;
  else if(s.port == 0 || (s.ip[0] == 0 && s.ip[1] == 0 && s.ip[2] == 0 && s.ip[3] == 0)){
    s.state = FLP_RELAY_ERROR;
    flexLinkUtf8Copy(s.err, sizeof(s.err), "el telefono no dio una direccion valida");
  } else s.state = FLP_RELAY_UP;
  *out = s;
  return true;
}

int flexPhoneEncMediaCmd(uint8_t* out, size_t outN, uint8_t cmd){
  if(!out) return -1;
  if(cmd < FLP_MCMD_PLAY || cmd > FLP_MCMD_PREV) return -1;
  FlexLinkWr w; flexLinkWrInit(&w, out, outN);
  flexLinkWrU8(&w, cmd);
  return flexLinkWrOk(&w) ? (int)w.at : -1;
}

uint8_t flexPhoneReplyCheck(const FlexPhoneModel* m, uint32_t notifId, const char* text){
  if(!m || !text) return FLNK_E_INTERNAL;
  const int i = flexPhoneNotifFind(m, notifId);
  // La notificacion ya no esta: se fue del telefono mientras se
  // escribia. Es un error REAL y hay que decirlo.
  if(i < 0) return FLNK_E_GONE;
  // Android no expuso RemoteInput: no hay forma legitima de enviar.
  // Aqui es donde se evita un boton "Enviar" que no hace nada.
  if(!m->notif[i].canReply) return FLNK_E_NOREPLY;
  if(m->notif[i].replyAction >= m->notif[i].actionCount) return FLNK_E_NOREPLY;
  if(!text[0]) return FLNK_E_INTERNAL;              // vacia: no se manda
  if(strnlen(text, FLP_REPLY_MAX) >= FLP_REPLY_MAX) return FLNK_E_TOOBIG;
  return FLNK_E_NONE;
}

// =============================================================
//  Persistencia
// =============================================================
// Formato del blob (v1):
//   u32 magia · u16 version · u16 nNotif · privacidad(4) ·
//   nNotif x notificacion codificada (u16 len + carga) ·
//   u16 nDraft · nDraft x (str pkg, str who, str text, u32 ms)
// Solo se guarda lo que sobrevive a un reinicio con sentido: las
// notificaciones y los borradores. Multimedia, relay y estado del
// telefono NO se persisten: son instantaneos, y resucitarlos
// mostraria un estado que ya no es cierto.
#define FLP_BLOB_VER 1

size_t flexPhoneBlobMaxSize(void){
  return FLP_BLOB_CAP;
}

// Elige las FLP_PERSIST_MAX notificaciones MAS RECIENTES y deja sus
// indices en `out` (mas nueva primero). Devuelve cuantas. Seleccion
// por insercion sobre un array fijo: sin reservas, sin recursion y
// con un coste acotado (40 x 20 comparaciones en el peor caso).
static int pickRecent(const FlexPhoneModel* m, uint8_t* out){
  int k = 0;
  for(int i = 0; i < FLP_NOTIF_MAX; i++){
    if(!m->notif[i].used) continue;
    const uint32_t t = m->notif[i].rxMs;
    int pos = k;
    while(pos > 0 && m->notif[out[pos - 1]].rxMs < t) pos--;
    if(pos >= FLP_PERSIST_MAX) continue;          // mas vieja que todas las guardadas
    const int last = (k < FLP_PERSIST_MAX) ? k : FLP_PERSIST_MAX - 1;
    for(int j = last; j > pos; j--) out[j] = out[j - 1];
    out[pos] = (uint8_t)i;
    if(k < FLP_PERSIST_MAX) k++;
  }
  return k;
}

size_t flexPhoneSerialize(const FlexPhoneModel* m, uint8_t* out, size_t outN){
  if(!m || !out) return 0;
  FlexLinkWr w; flexLinkWrInit(&w, out, outN);
  flexLinkWrU32(&w, FLP_BLOB_MAGIC);
  flexLinkWrU16(&w, FLP_BLOB_VER);

  // Cuantas se van a guardar. Si el usuario pidio no conservar
  // historial, se guardan CERO: la privacidad manda sobre la
  // comodidad.
  uint8_t pick[FLP_PERSIST_MAX];
  uint16_t nN = 0;
  if(m->priv.keepHistory) nN = (uint16_t)pickRecent(m, pick);
  flexLinkWrU16(&w, nN);
  uint8_t pf = 0;
  if(m->priv.hideBodyOnLock) pf |= 0x01;
  if(m->priv.hideSensitive)  pf |= 0x02;
  if(m->priv.keepHistory)    pf |= 0x04;
  flexLinkWrU8(&w, pf);
  flexLinkWrU8(&w, 0);                       // reservado (alineacion)
  flexLinkWrU16(&w, m->priv.keepHours);

  // Se escriben de MAS ANTIGUA a MAS NUEVA para que, al restaurar,
  // el indice del bucle sirva de reloj y conserve el orden.
  for(int p = (int)nN - 1; p >= 0; p--){
    uint8_t tmp[FLP_BLOB_PER_NOTIF];
    const int k = flexPhoneEncNotif(tmp, sizeof(tmp), &m->notif[pick[p]]);
    if(k <= 0) continue;                     // no cabe: se omite, no se corrompe
    flexLinkWrU16(&w, (uint16_t)k);
    flexLinkWrBytes(&w, tmp, (size_t)k);
  }

  uint16_t nD = 0;
  for(int i = 0; i < FLP_DRAFT_MAX; i++) if(m->draft[i].used) nD++;
  flexLinkWrU16(&w, nD);
  for(int i = 0; i < FLP_DRAFT_MAX; i++){
    if(!m->draft[i].used) continue;
    flexLinkWrStr(&w, m->draft[i].pkg,  FLP_PKG_MAX   - 1);
    flexLinkWrStr(&w, m->draft[i].who,  FLP_TITLE_MAX - 1);
    flexLinkWrStr(&w, m->draft[i].text, 255);
    flexLinkWrU32(&w, m->draft[i].savedMs);
  }
  return flexLinkWrOk(&w) ? w.at : 0;
}

bool flexPhoneDeserialize(FlexPhoneModel* m, const uint8_t* in, size_t n){
  if(!m || !in) return false;
  FlexLinkRd r; flexLinkRdInit(&r, in, n);
  if(flexLinkRdU32(&r) != FLP_BLOB_MAGIC) return false;
  const uint16_t ver = flexLinkRdU16(&r);
  if(ver != FLP_BLOB_VER) return false;       // version futura: no se adivina
  uint16_t nN = flexLinkRdU16(&r);
  const uint8_t pf = flexLinkRdU8(&r);
  (void)flexLinkRdU8(&r);
  const uint16_t hours = flexLinkRdU16(&r);
  if(!flexLinkRdOk(&r)) return false;
  if(nN > FLP_PERSIST_MAX) return false;       // el blob miente sobre el conteo

  flexPhoneModelClear(m, true);
  m->priv.hideBodyOnLock = (pf & 0x01) != 0;
  m->priv.hideSensitive  = (pf & 0x02) != 0;
  m->priv.keepHistory    = (pf & 0x04) != 0;
  m->priv.keepHours      = hours;

  for(uint16_t i = 0; i < nN; i++){
    const uint16_t len = flexLinkRdU16(&r);
    if(!flexLinkRdOk(&r)) return false;
    if(len == 0 || (size_t)len > n) return false;
    if(r.at + len > r.n) return false;
    FlexPhoneNotif tmp;
    if(flexPhoneDecNotif(r.p + r.at, len, &tmp)){
      // Se respeta el orden guardado usando el indice como reloj:
      // no hay millis() fiable al restaurar.
      flexPhoneNotifPut(m, &tmp, i);
    }
    r.at += len;
  }

  uint16_t nD = flexLinkRdU16(&r);
  if(!flexLinkRdOk(&r)) return true;           // sin borradores: no es un fallo
  if(nD > FLP_DRAFT_MAX) nD = FLP_DRAFT_MAX;
  for(uint16_t i = 0; i < nD; i++){
    char pkg[FLP_PKG_MAX], who[FLP_TITLE_MAX], txt[FLP_REPLY_MAX];
    flexLinkRdStr(&r, pkg, sizeof(pkg));
    flexLinkRdStr(&r, who, sizeof(who));
    flexLinkRdStr(&r, txt, sizeof(txt));
    const uint32_t ms = flexLinkRdU32(&r);
    if(!flexLinkRdOk(&r)) break;
    if(pkg[0]) flexPhoneDraftPut(m, pkg, who, txt, ms);
  }
  flexPhoneConvRebuild(m);
  m->dirty = false;                            // recien cargado = ya persistido
  return true;
}
