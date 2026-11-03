// Referencia: el portapapeles de 12 ranuras de Arduino (Keyboard.h:463-520, ClipItem de Types.h) tal cual, con un
// Preferences y un String de mentira.
#include "ref_prelude.h"
#include <map>
#include <string>
#include <stdio.h>
#include <string.h>

#define KB_CLIPBOARD_MULTI_ON 1
#define CLIP_SLOTS 12
#define CLIP_TXT_MAX 200
struct String {
    std::string s;
    String() {}
    String(const std::string &x) : s(x) {}
    int length() const { return (int)s.size(); }
    void toCharArray(char *b, unsigned n) const { snprintf(b, n, "%s", s.c_str()); }
};
static std::map<std::string, std::string> g_nvs;
static struct PrefsT {
    void begin(const char *, bool) {}
    void end() {}
    void putString(const char *k, const char *v) { g_nvs[k] = v; }
    void remove(const char *k) { g_nvs.erase(k); }
    String getString(const char *k, const char *d) { auto it = g_nvs.find(k); return String(it == g_nvs.end() ? std::string(d) : it->second); }
} prefs;
static uint32_t g_now;
static uint32_t millis() { return g_now; }
struct ClipItem { char text[CLIP_TXT_MAX]; bool pinned; bool used; uint32_t ts; };
static ClipItem gClip[CLIP_SLOTS];

#include "ref_clip.inc"

extern "C" {
void ref_clip_reset(void) { memset(gClip, 0, sizeof(gClip)); clipboard[0] = 0; g_nvs.clear(); }
void ref_clip_nvs_put(int i, const char *t) { char k[10]; snprintf(k, sizeof(k), "clip%d", i); if (t) g_nvs[k] = t; else g_nvs.erase(k); }
void ref_clip_load(void) { clipLoadPinned(); }
void ref_clip_clear_ram(void) { memset(gClip, 0, sizeof(gClip)); clipboard[0] = 0; }   // reinicio: la NVS se queda
void ref_clip_push(const char *s, unsigned now) { g_now = now; clipPush(s); }
void ref_clip_del(int i) { clipDel(i); }
void ref_clip_pin(int i) { clipTogglePin(i); }
void ref_clip_clear_unpinned(void) { clipClearUnpinned(); }
int ref_clip_count(void) { return clipCount(); }
void ref_clip_slot(int i, char *text, int *used, int *pinned, unsigned *ts) { strcpy(text, gClip[i].text); *used = gClip[i].used; *pinned = gClip[i].pinned; *ts = gClip[i].ts; }
void ref_clip_classic(char *out) { strcpy(out, clipboard); }
// NVS: "clipN=texto;" ordenado
int ref_clip_nvs(char *out, int cap) { int n = 0; out[0] = 0; for (auto &kv : g_nvs) n += snprintf(out + n, cap - n, "%s=%s;", kv.first.c_str(), kv.second.c_str()); return n; }
}
