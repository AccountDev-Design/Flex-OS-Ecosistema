// Referencia: la lista de Notas (AppNotes.h:75-143) y su sesion notas.bin v3 (:354-533) de la version Arduino, tal cual.
#include "ref_prelude.h"
#include <stdio.h>
#include <string.h>

#define IC_NOTAS 5
#define FS_DIR_SESS "/System/Sessions"
#define FLEXFS_NAME_MAX 48
static uint8_t g_file[160]; static size_t g_file_n; static bool g_fs = true, g_exists = true;
static bool flexFsReady() { return g_fs; }
static bool flexFsExists(const char *) { return g_exists; }
static bool sessWrite(const char *, uint16_t, uint16_t, const void *d, size_t n) { memcpy(g_file, d, n); g_file_n = n; return true; }
static size_t sessRead(const char *, uint16_t, uint16_t, void *o, size_t max) { size_t n = g_file_n < max ? g_file_n : max; memcpy(o, g_file, n); return n; }
static char g_buf[8192];
static char *noteBuffer = g_buf;
static int noteCur = 0, noteSelA = -1, noteSelB = -1, noteEditorScroll = 0;
static uint8_t noteKbLayout = 0, noteKbFlags = 0;
static bool noteKbStateValid = false;
static void noteSave() {}
static void noteCaptureKbState() {}

#include "ref_notes.inc"

extern "C" {
void ref_nl_set(int n, int scroll) { noteListN = n; noteScroll = scroll; }
void ref_nl_rect(int i, int *x, int *y, int *w, int *h) { noteCardRect(i, *x, *y, *w, *h); }
int ref_nl_max_scroll(void) { return noteMaxScroll(); }
void ref_nl_fab(int *x, int *y, int *r) { noteFabRect(*x, *y, *r); }
int ref_nl_card_h(void) { return noteCardH(); }
// sesion: guardar con el estado dado
int ref_notes_save(int view, const char *path, int kbl, int kbf, const char *text, int cur, int a, int b, int escroll, int lscroll, unsigned char *out)
{
    noteView = view; snprintf(notePath, sizeof(notePath), "%s", path);
    snprintf(g_buf, sizeof(g_buf), "%s", text);
    noteKbLayout = (uint8_t)kbl; noteKbFlags = (uint8_t)kbf; noteCur = cur; noteSelA = a; noteSelB = b; noteEditorScroll = escroll; noteScroll = lscroll;
    noteSaveSess();
    memcpy(out, g_file, g_file_n);
    return (int)g_file_n;
}
// sesion: cargar un archivo dado
void ref_notes_load(const unsigned char *in, size_t n, int exists, int *view, char *path, int *kbl, int *kbf, int *cur, int *a, int *b, int *escroll, int *lscroll, int *valid)
{
    memcpy(g_file, in, n); g_file_n = n; g_exists = exists;
    noteView = 0; notePath[0] = 0; noteCur = 0; noteSelA = noteSelB = -1; noteEditorScroll = noteScroll = 0; noteKbLayout = 9; noteKbFlags = 77; noteKbStateValid = false;
    noteLoadSess();
    *view = noteView; strcpy(path, notePath); *kbl = noteKbLayout; *kbf = noteKbFlags; *cur = noteCur; *a = noteSelA; *b = noteSelB; *escroll = noteEditorScroll; *lscroll = noteScroll; *valid = noteKbStateValid;
}
}
