// Referencia: el modelo de avisos de Arduino (historial con huella, notifPush,
// notifRemove, sysNotify) y la cola del banner (fpbRank, fpbEnqueue, fpbSameSys,
// fpbOffer, fpbMsgInit, fpbPushSystemKeyed), tal cual. El recorte UTF-8 es el
// de FlexOS_FlexLink.cpp (se enlaza el original).
#include "ref_prelude.h"
#include <stdio.h>
#include "FlexOS_FlexLink.h"

enum ModuleType { MOD_UNKNOWN, MOD_MEDIA };
enum NotifPhase { NP_IN, NP_IDLE, NP_DRAG, NP_OUT, NP_SPRING };
static uint32_t s_ms;
static uint32_t millis() { return s_ms; }
static int notifDragIdx = -1;
enum { FLP_PRI_MIN = 0, FLP_PRI_LOW, FLP_PRI_DEFAULT, FLP_PRI_HIGH, FLP_PRI_URGENT };
#define FPN_ENTRY_APP   32
#define FPN_ENTRY_TITLE 64
#define FPN_ENTRY_BODY  120
typedef struct {
  char     app[FPN_ENTRY_APP];
  char     title[FPN_ENTRY_TITLE];
  char     body[FPN_ENTRY_BODY];
  uint32_t id;
  uint32_t key;
  uint32_t shownMs;
  uint8_t  src;
  uint8_t  pri;
  uint8_t  icon;
} FlexPhoneBannerMsg;
static void fpbPushSystemKeyed(uint32_t key, uint8_t type, const char* title, const char* body);

#include "ref_ntf.inc"

extern "C" {
void ref_ntf_reset(void)
{
    memset(gNotifs, 0, sizeof(gNotifs));
    gNotifCount = 0;
    fpbState = FPB_HIDDEN;
    memset(&fpbCur, 0, sizeof(fpbCur));
    memset(fpbQueue, 0, sizeof(fpbQueue));
    fpbQueueN = 0;
    fpbMore = 0;
    fpbRefresh = false;
}
void ref_ntf_sys(int type, const char *title, const char *sub, uint32_t now)
{
    s_ms = now;
    if (type == 0) {
        sysNotify(title, sub);
    } else {
        mediaNotify((ModuleType)type, title, sub);
    }
}
void ref_ntf_remove(int idx) { notifRemove(idx); }
int ref_ntf_count(void) { return gNotifCount; }
void ref_ntf_get(int i, int *type, const char **title, const char **sub, uint32_t *born, uint32_t *key)
{
    *type = gNotifs[i].mod.type;
    *title = gNotifs[i].mod.name;
    *sub = gNotifs[i].mod.sub;
    *born = gNotifs[i].bornMs;
    *key = gNotifs[i].key;
}
void ref_bq_push(int src, uint32_t id, const char *app, const char *title, const char *body, int pri)
{
    fpbPush((uint8_t)src, id, app, title, body, (uint8_t)pri);
}
void ref_bq_set_cur(bool live, const void *msg)
{
    fpbState = live ? FPB_SHOWN : FPB_HIDDEN;
    if (msg) memcpy(&fpbCur, msg, sizeof(fpbCur));
}
void ref_bq_requeue_front(const void *msg) { fpbEnqueue((const FlexPhoneBannerMsg *)msg, true); }
// El arranque del siguiente de la cola, como fpbTick (FlexPhone_Overlay.h:787-797)
bool ref_bq_pop(void)
{
    if (fpbQueueN == 0) return false;
    fpbCur = fpbQueue[0];
    for (int i = 1; i < fpbQueueN; i++) fpbQueue[i - 1] = fpbQueue[i];
    fpbQueueN--;
    fpbState = FPB_SHOWN;
    fpbRefresh = false;
    return true;
}
int ref_bq_n(void) { return fpbQueueN; }
const void *ref_bq_at(int i) { return &fpbQueue[i]; }
const void *ref_bq_cur(void) { return &fpbCur; }
uint32_t ref_bq_more(void) { return fpbMore; }
bool ref_bq_refresh(void) { return fpbRefresh; }
size_t ref_utf8_copy(char *o, size_t n, const char *s) { return flexLinkUtf8Copy(o, n, s); }
}
