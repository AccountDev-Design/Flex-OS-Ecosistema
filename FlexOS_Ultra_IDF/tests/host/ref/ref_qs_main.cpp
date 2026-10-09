// Referencia: el modelo del panel rapido de Arduino (QS_REG, fabrica,
// normalizacion, blob flexqs/qp1, maquetacion, "snap" de la tarjeta, editor y
// catalogo), tal cual. Los punteros a funcion del registro (backends, iconos)
// se sustituyen por funciones vacias: aqui solo cuenta el modelo. La
// disponibilidad (qpCtlAvail) la fija la prueba.
#include "ref_prelude.h"
#include <math.h>
#include <stddef.h>
#include <stdio.h>

static bool ref_b() { return true; }
static void ref_v() {}
static void ref_s(char *, size_t) {}
static void ref_i(int, int, int, uint16_t) {}
#define qpAvWifi ref_b
#define qpAvTrue ref_b
#define qpAvFalse ref_b
#define qpAvBle ref_b
#define qpAvFs ref_b
#define qpAvLock ref_b
#define qpAvPoweroff ref_b
#define qpAvNtp ref_b
#define qpAvDex ref_b
#define qpAvAudio ref_b
#define qpAvDnd ref_b
#define qpStWifi ref_b
#define qpStAirplane ref_b
#define qpStBle ref_b
#define qpStTheme ref_b
#define qpStGlass ref_b
#define qpStPower ref_b
#define qpStCrono ref_b
#define qpStMute ref_b
#define qpStDnd ref_b
#define qpTapWifi ref_v
#define qpTapAirplane ref_v
#define qpTapBle ref_v
#define qpTapBright ref_v
#define qpTapTheme ref_v
#define qpTapGlass ref_v
#define qpTapPower ref_v
#define qpTapSettings ref_v
#define qpTapConn ref_v
#define qpTapDex ref_v
#define qpTapOta ref_v
#define qpTapFiles ref_v
#define qpTapCamera ref_v
#define qpTapGallery ref_v
#define qpTapCrono ref_v
#define qpTapLock ref_v
#define qpTapPoweroff ref_v
#define qpTapNtp ref_v
#define qpTapVolume ref_v
#define qpTapMute ref_v
#define qpTapDnd ref_v
#define qpTapGlassFx ref_v
#define qpSubWifi ref_s
#define qpSubAirplane ref_s
#define qpSubBle ref_s
#define qpSubBright ref_s
#define qpSubTheme ref_s
#define qpSubGlass ref_s
#define qpSubPower ref_s
#define qpSubOta ref_s
#define qpSubCrono ref_s
#define qpSubVolume ref_s
#define qpSubMute ref_s
#define qpSubDnd ref_s
#define qpSubGlassFx ref_s
#define qpIcoWifi ref_i
#define qpIcoAirplane ref_i
#define qpIcoBle ref_i
#define qpIcoSun ref_i
#define qpIcoMoon ref_i
#define qpIcoGlass ref_i
#define qpIcoBattSave ref_i
#define qpIcoGear ref_i
#define qpIcoSignal ref_i
#define qpIcoMonitor ref_i
#define qpIcoUpdate ref_i
#define qpIcoFolder ref_i
#define qpIcoCamera ref_i
#define qpIcoImage ref_i
#define qpIcoStopwatch ref_i
#define qpIcoLock ref_i
#define qpIcoPower ref_i
#define qpIcoClock ref_i
#define qpIcoSpeaker ref_i
#define qpIcoMute ref_i
#define qpIcoDnd ref_i
#define qpIcoGlassFx ref_i

#include "ref_qs_a.inc"

static uint32_t s_avail;   // bit por id
static bool uiGlass = true;
static inline bool qpCtlAvail(int id)
{
    const QsCtl *c = qpCtl(id);
    return c && ((s_avail >> id) & 1u);
}

// qpLoad: Preferences sobre un blob de la prueba
static const uint8_t *s_blob;
static size_t s_blob_n;
struct Preferences {
    bool begin(const char *, bool) { return true; }
    void end() {}
    size_t getBytes(const char *, void *b, size_t n)
    {
        if (!s_blob) return 0;
        size_t k = s_blob_n < n ? s_blob_n : n;
        memcpy(b, s_blob, k);
        return s_blob_n;   // Preferences devuelve el tamano real del blob
    }
};
static Preferences prefs;
static struct { void println(const char *) {} } Serial;
#define F(x) x
static uint32_t s_ms;
static uint32_t millis() { return s_ms; }
static void qpMarkAll() {}

#include "ref_qs_b.inc"

// ---- interfaz C para la prueba -------------------------------------------------
extern "C" {

void ref_qs_set(uint32_t avail, bool glass)
{
    s_avail = avail;
    uiGlass = glass;
}

void ref_qs_meta(int id, const char **name, const char **title, uint8_t *t, uint8_t *sz, uint8_t *ori, uint8_t *cat)
{
    const QsCtl *c = qpCtl(id);
    *name = c->name; *title = c->title; *t = c->type; *sz = c->sizes; *ori = c->oris; *cat = c->cat;
}

bool ref_qs_shown(int id) { return qpCtlShown(id); }
void ref_qs_first_size(uint8_t mask, uint8_t *w, uint8_t *h) { qpFirstSize(mask, *w, *h); }
bool ref_qs_size_allowed(int id, int w, int h) { return qpSizeAllowed(id, w, h); }
bool ref_qs_next_size(int id, int w, int h, int dir, uint8_t *nw, uint8_t *nh) { return qpNextSize(id, w, h, dir, *nw, *nh); }

static void get_cfg(uint8_t *items, uint8_t *n, uint8_t *grows)
{
    memcpy(items, qpIt, sizeof(qpIt));
    *n = qpN;
    *grows = qpGrows;
}

void ref_qs_factory(uint8_t *items, uint8_t *n, uint8_t *grows)
{
    memset(qpIt, 0, sizeof(qpIt));
    qpFactory();
    get_cfg(items, n, grows);
}

uint8_t ref_qs_normalize(uint8_t *items, uint8_t n, uint8_t *grows)
{
    return qpNormalize((QpItem *)items, n, *grows);
}

bool ref_qs_deserialize(const uint8_t *b, uint8_t *items, uint8_t *n, uint8_t *grows)
{
    memset(qpIt, 0, sizeof(qpIt));
    qpN = 0;
    qpGrows = 3;
    bool ok = qpDeserialize(b);
    get_cfg(items, n, grows);
    return ok;
}

void ref_qs_load(const uint8_t *b, size_t rd, uint8_t *items, uint8_t *n, uint8_t *grows)
{
    memset(qpIt, 0, sizeof(qpIt));
    qpN = 0;
    qpGrows = 3;
    qpLoaded = false;
    s_blob = b;
    s_blob_n = rd;
    qpLoad();
    get_cfg(items, n, grows);
}

void ref_qs_serialize(const uint8_t *items, uint8_t n, uint8_t grows, uint8_t *b)
{
    memcpy(qpIt, items, sizeof(qpIt));
    qpN = n;
    qpGrows = grows;
    qpSerialize(b);
}

// blocks: 6 int16 por bloque (kind, item, x, y, w, h)
int ref_qs_layout(const uint8_t *items, int n, int group_px, bool edit, int16_t *blocks, uint8_t *tiles, int *tile_n,
                  int *content_h, int *group_blk)
{
    static QpItem src[QP_MAX_ITEMS];
    memcpy(src, items, sizeof(src));
    qpLaySrc = src;
    qpLayN = n;
    qpLayGroupPx = group_px;
    qpLayEdit = edit;
    qpLayout();
    for (int i = 0; i < qpBlkN; i++) {
        int16_t *o = blocks + i * 6;
        o[0] = qpBlk[i].kind; o[1] = qpBlk[i].item; o[2] = qpBlk[i].x; o[3] = qpBlk[i].y; o[4] = qpBlk[i].w; o[5] = qpBlk[i].h;
    }
    memcpy(tiles, qpTiles, sizeof(qpTiles));
    *tile_n = qpTileN;
    *content_h = qpContentH;
    *group_blk = qpGroupBlk;
    return qpBlkN;
}

// qpRelayout en modo panel (gh = qpGH) o edicion (grows = qpEdGrows)
int ref_qs_relayout(const uint8_t *items, uint8_t n, bool edit, float gh, uint8_t grows, float *gh_out,
                    int *content_h, int *scroll_max)
{
    if (edit) {
        memcpy(qpEdIt, items, sizeof(qpEdIt));
        qpEdN = n;
        qpEdGrows = grows;
        qpMode = QPM_EDIT;
    } else {
        memcpy(qpIt, items, sizeof(qpIt));
        qpN = n;
        qpGrows = grows;
        qpMode = QPM_PANEL;
    }
    qpGH = gh;
    qpGScrollF = 0;
    qpRelayout();
    *gh_out = qpGH;
    *content_h = qpContentH;
    *scroll_max = qpScrollMax();
    return qpLayGroupPx;
}

// qpGroupSnap tras maquetar la configuracion en modo panel
float ref_qs_group_snap(const uint8_t *items, uint8_t n, float gh, uint8_t *rows, uint32_t *dur)
{
    memcpy(qpIt, items, sizeof(qpIt));
    qpN = n;
    qpGrows = 3;
    qpMode = QPM_PANEL;
    qpGH = gh;
    qpRelayout();
    qpGH = gh;   // el arrastre del asa deja qpGH donde lo dejo el dedo
    qpGroupSnap();
    *rows = qpGrows;
    *dur = qpGDur;
    return qpGTo;
}

void ref_qs_tile_center(int k, int gy, int *cx, int *cy) { qpTileCenter(k, gy, *cx, *cy); }

// Editor: op 0 = quitar(a), 1 = mover(a, b), 2 = anadir(a)
bool ref_qs_edit(uint8_t *items, uint8_t *n, int op, int a, int b)
{
    memcpy(qpEdIt, items, sizeof(qpEdIt));
    qpEdN = *n;
    qpEdGrows = 3;
    qpMode = QPM_EDIT;
    bool r = true;
    if (op == 0) r = qpEditRemove(a);
    else if (op == 1) qpEditMove(a, b);
    else r = qpEditAdd(a);
    memcpy(items, qpEdIt, sizeof(qpEdIt));
    *n = qpEdN;
    return r;
}

int ref_qs_catalog(const uint8_t *items, uint8_t n, uint8_t *out)
{
    memcpy(qpEdIt, items, sizeof(qpEdIt));
    qpEdN = n;
    qpCatBuild();
    memcpy(out, qpCatIds, (size_t)qpCatN);
    return qpCatN;
}
}
