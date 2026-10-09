// Referencia: la lista de Recientes de Arduino (swPush, swDropCard,
// swThumbTrim, captureThumb) tal cual; appTerminate anota la app cerrada.
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define APP_N 19
#define SCR_W 480
#define SCR_H 800
static uint16_t *fb;
static int s_terminated = -1;
static bool appTerminate(int id, bool) { s_terminated = id; return true; }
#include "ref_recents.inc"

extern "C" void ref_rc_reset(void)
{
    for (int i = 0; i < swCount; i++) { free(swTasks[i].thumb); swTasks[i].thumb = NULL; }
    swCount = 0;
}
extern "C" int ref_rc_push(int id) { s_terminated = -1; swPush((uint8_t)id); return s_terminated; }
extern "C" void ref_rc_drop(int idx) { swDropCard(idx); }
extern "C" void ref_rc_trim(int keep) { swThumbTrim(keep); }
extern "C" void ref_rc_set_thumb0(void) { if (swCount && !swTasks[0].thumb) swTasks[0].thumb = (uint16_t *)malloc(8); }
extern "C" int ref_rc_count(void) { return swCount; }
extern "C" int ref_rc_app(int i) { return swTasks[i].appID; }
extern "C" bool ref_rc_has_thumb(int i) { return swTasks[i].thumb != NULL; }
extern "C" void ref_rc_capture(uint16_t *src, uint16_t *dst) { fb = src; captureThumb(dst); }
