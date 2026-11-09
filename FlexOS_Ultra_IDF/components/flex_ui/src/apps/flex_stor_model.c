#include "flex_stor_model.h"

#include <stdio.h>
#include <string.h>
#include "flex_fs_names.h"

#define RGB565(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

const int flex_st_sec_h[FLEX_ST_SEC_N] = {96, 240, 168, 236, 200};

int flex_st_sec_top(int i)
{
    int y = 0;
    for (int k = 0; k < i && k < FLEX_ST_SEC_N; k++) {
        y += flex_st_sec_h[k];
    }
    return y;
}

int flex_st_content_h(void)
{
    return flex_st_sec_top(FLEX_ST_SEC_N);
}

int flex_st_max_scroll(void)
{
    int m = flex_st_content_h() - (FLEX_ST_DET_VY1 - FLEX_ST_DET_VY0 + 1);
    return m > 0 ? m : 0;
}

// ---- utilidades de la lista ------------------------------------------------------------------------------------------------------
static flex_st_op_t *add(flex_st_page_t *pg, int kind)
{
    if (pg->n >= FLEX_ST_OPS_MAX) {
        return NULL;
    }
    flex_st_op_t *o = &pg->op[pg->n++];
    memset(o, 0, sizeof(*o));
    o->kind = (uint8_t)kind;
    return o;
}

static void add_shape(flex_st_page_t *pg, int kind, int x, int y, int w, int h, int r, int role, uint16_t rgb)
{
    flex_st_op_t *o = add(pg, kind);
    if (o) {
        o->x = (int16_t)x;
        o->y = (int16_t)y;
        o->w = (int16_t)w;
        o->h = (int16_t)h;
        o->r = (int16_t)r;
        o->role = (uint8_t)role;
        o->rgb = rgb;
    }
}

static void add_text(flex_st_page_t *pg, int kind, int x, int y, int size, int role, const char *text, int max_right)
{
    flex_st_op_t *o = add(pg, kind);
    if (o) {
        o->x = (int16_t)x;
        o->y = (int16_t)y;
        o->size = (uint8_t)size;
        o->role = (uint8_t)role;
        o->max_right = (int16_t)max_right;
        snprintf(o->text, sizeof(o->text), "%.*s", (int)sizeof(o->text) - 1, text);   // un texto largo se recorta, nunca desborda
    }
}

void flex_st_folder_rects(int x, int y, int s, flex_st_rect_t out[3])
{
    uint16_t body = RGB565(250, 205, 90), tab = RGB565(240, 175, 60);
    out[0] = (flex_st_rect_t){x, y + s / 5, s, s * 3 / 4, s / 8, tab};
    out[1] = (flex_st_rect_t){x, y + s / 5, s * 5 / 9, s / 4, s / 10, tab};
    out[2] = (flex_st_rect_t){x + s / 10, y + s / 3, s - s / 10, s * 3 / 5, s / 9, body};
}

// ---- telefono ----------------------------------------------------------------------------------------------------------------------------
void flex_st_phone_state(const flex_st_phone_t *p, char *out, size_t cap, int *role)
{
    const char *nm = p->name[0] ? p->name : "Tel\xC3\xA9" "fono";
    switch (p->state) {
    case FST_PH_READY:
        if (p->reachable) {
            snprintf(out, cap, "%s \xC2\xB7 conectado", nm);
            *role = FSC_OK;
        } else if (p->never_ok) {
            snprintf(out, cap, "%s \xC2\xB7 conectando...", nm);
            *role = FSC_PRIM;
        } else {
            snprintf(out, cap, "%s \xC2\xB7 desconectado", nm);
            *role = FSC_WARN;
        }
        break;
    case FST_PH_OFF:
        snprintf(out, cap, "%s \xC2\xB7 en pausa", nm);
        *role = FSC_MUTE;
        break;
    case FST_PH_REJECTED:
        snprintf(out, cap, "Hay que volver a emparejar");
        *role = FSC_ERR;
        break;
    case FST_PH_UNAVAILABLE:   // sin el servicio de telefono (Flex Storage): no se finge "Sin telefono" ni "actívalo desde la web"
        snprintf(out, cap, "No disponible todav\xC3\xAD" "a");
        *role = FSC_MUTE;
        break;
    default:
        snprintf(out, cap, "Sin tel\xC3\xA9" "fono");
        *role = FSC_MUTE;
        break;
    }
}

// almPhoneCard: tarjeta de 52 px; tocarla abre la pantalla propia
static void phone_card(flex_st_page_t *pg, const flex_st_phone_t *p, int x, int y, int w)
{
    add_shape(pg, FST_SURF, x, y, w, 52, 14, FSC_SURF_ELEV, 0);
    add_shape(pg, FST_CLOUD, x + 26, y + 27, 0, 0, 13, FSC_ACCENT, 0);
    add_text(pg, FST_TEXT, x + 50, y + 5, 2, FSC_TXT, "Flex Cloud en tu tel\xC3\xA9" "fono", 0);
    char st[80];
    int role = FSC_MUTE;
    flex_st_phone_state(p, st, sizeof(st), &role);
    add_shape(pg, FST_DOT, x + 54, y + 37, 0, 0, 4, role, 0);
    char line[200];
    if (p->state == FST_PH_READY && p->quota_valid) {
        snprintf(line, sizeof(line), "%s  \xC2\xB7  %s", st, p->quota);
    } else if (p->state == FST_PH_NONE) {
        snprintf(line, sizeof(line), "%s \xC2\xB7 act\xC3\xADvalo desde la web", st);
    } else {
        snprintf(line, sizeof(line), "%s", st);
    }
    add_text(pg, FST_TEXT, x + 64, y + 31, 1, FSC_TXT2, line, x + w - 30);
    add_shape(pg, FST_CHEV, x + w - 22, y + 20, 0, 0, 0, FSC_TXT2, 0);
}

// ---- barra de medidor (simpBar) ----------------------------------------------------------------------------------------------------------
static int simp_bar(flex_st_page_t *pg, int y, const char *label, const char *val, int pct, uint16_t rgb, int bxx, int bww, int bhh,
                    int pad, flex_ui_textw_fn tw, void *ctx)
{
    int bx = bxx + pad * 2, bw = bww - pad * 4;
    if (bw < 60) {
        bx = bxx + pad;
        bw = bww - 2 * pad;
    }
    int fs = flex_ui_font_fit(label, bw / 2, 2, tw, ctx);
    add_text(pg, FST_TEXT, bx, y, fs, FSC_TXT, label, 0);
    add_text(pg, FST_TEXT_R, bx + bw, y, flex_ui_font_fit(val, bw / 2, 2, tw, ctx), FSC_TXT2, val, 0);
    int bar_h = bhh / 22;
    if (bar_h < 9) {
        bar_h = 9;
    }
    if (bar_h > 20) {
        bar_h = 20;
    }
    int byr = y + flex_ui_line_h(fs) + 6;
    add_shape(pg, FST_RRECT, bx, byr, bw, bar_h, bar_h / 2, FSC_TRACK, 0);
    if (pct > 0) {
        add_shape(pg, FST_RRECT, bx, byr, bw * pct / 100, bar_h, bar_h / 2, FSC_RGB, rgb);
    }
    return byr + bar_h + pad;
}

static const char *const CAT_NAME[FLEX_FSX_CAT_N] = {"Documentos", "Sistema", "Aplicaciones", "Papelera"};
static const uint16_t CAT_COL[FLEX_FSX_CAT_N] = {RGB565(245, 85, 85), RGB565(250, 215, 95), RGB565(95, 225, 110), RGB565(60, 205, 240)};

void flex_st_main_build(flex_st_page_t *pg, flex_st_zones_t *z, const flex_st_main_in_t *in, int bx, int by, int bw, int bh,
                        flex_ui_textw_fn tw, void *ctx)
{
    pg->n = 0;
    memset(z, 0, sizeof(*z));
    int pad = flex_ui_pad(bw, bh), gap = flex_ui_gap(pad);
    int y = by + pad;
    // uiTitle: tamano que cabe, centrado, y la siguiente y
    {
        const char *t = "Almacenamiento";
        int fs = flex_ui_font_fit(t, bw, flex_ui_font_h(bh / 12), tw, ctx);
        add_text(pg, FST_TEXT_C, bx + bw / 2, y, fs, FSC_TXT, t, 0);
        y += flex_ui_line_h(fs) + gap / 2;
    }
    y += gap / 2;

    if (!in->fs_ready) {
        add_text(pg, FST_TEXT_C, bx + bw / 2, y + 40, 3, FSC_ERR, "Sin almacenamiento", 0);
        add_text(pg, FST_TEXT_C, bx + bw / 2, y + 84, 1, FSC_TXT2, in->fs_err, 0);
        // Arduino aconseja elegir un Partition Scheme en el IDE; aqui la tabla es fija (mismo texto que el kit de archivos)
        add_text(pg, FST_TEXT_C, bx + bw / 2, y + 112, 1, FSC_MUTE, "Reinstala el firmware o repara el almacenamiento en Ajustes", 0);
        return;
    }

    // ---- barra 1: particion de datos (lo unico que el usuario llena) ----
    {
        int pct = in->fs_total ? (int)((uint64_t)in->fs_used * 100 / in->fs_total) : 0;
        char vt[96], su[24], st[24];
        flex_fsn_fmt_size(in->fs_used, su, sizeof(su));
        flex_fsn_fmt_size(in->fs_total, st, sizeof(st));
        snprintf(vt, sizeof(vt), "%s / %s  (%d%%)", su, st, pct);
        y = simp_bar(pg, y, "Memoria interna", vt, pct, RGB565(90, 160, 240), bx, bw, bh, pad, tw, ctx);
    }
    // ---- barra 2: PSRAM (sin PSRAM se dice, no se pinta una barra a 0 que parezca vacia) ----
    if (in->ps_total > 0) {
        int pct = (int)(100 - (uint64_t)in->ps_free * 100 / in->ps_total);
        char vp[48];
        snprintf(vp, sizeof(vp), "%u / %u MB  (%d%%)", (unsigned)((in->ps_total - in->ps_free) / 1048576u),
                 (unsigned)(in->ps_total / 1048576u), pct);
        y = simp_bar(pg, y, "PSRAM", vp, pct, RGB565(90, 180, 120), bx, bw, bh, pad, tw, ctx);
    } else {
        y = simp_bar(pg, y, "PSRAM", "No disponible en esta placa", 0, RGB565(120, 124, 140), bx, bw, bh, pad, tw, ctx);
    }

    // ---- "Detalles de memoria y sistema": colapsado ya dice el estado; Optimizar vive aqui porque hace falta cuando va lento ----
    if (y + 52 <= by + bh - pad) {
        add_shape(pg, FST_SURF, bx + pad, y, bw - 2 * pad, 50, 14, FSC_SURF_ELEV, 0);
        add_text(pg, FST_TEXT, bx + pad * 2, y + 4, 2, FSC_TXT, "Detalles de memoria y sistema", 0);
        int hh = in->health;
        const char *hn = hh == FLEXMEM_H_CRIT ? "Cr\xC3\xADtico" : (hh == FLEXMEM_H_WATCH ? "Atenci\xC3\xB3n" : "\xC3\x93ptimo");
        int hc = hh == FLEXMEM_H_CRIT ? FSC_ERR : (hh == FLEXMEM_H_WATCH ? FSC_WARN : FSC_OK);
        add_shape(pg, FST_DOT, bx + pad * 2 + 6, y + 32, 0, 0, 5, hc, 0);
        add_text(pg, FST_TEXT, bx + pad * 2 + 18, y + 26, 1, FSC_TXT2, hn, 0);
        int ow = 118, ox = bx + bw - pad * 2 - ow + 8, oy = y + 9;
        add_shape(pg, FST_RRECT, ox, oy, ow, 32, 16, FSC_PRIM, 0);
        add_text(pg, FST_TEXT_C, ox + ow / 2, oy + 8, 1, FSC_ONACC, "Optimizar", 0);
        z->opt_x0 = ox;
        z->opt_x1 = ox + ow;
        z->det_y0 = y;
        z->det_y1 = y + 50;
        y += 50 + gap / 2;
    }

    // ---- Flex Cloud en el telefono ----
    if (y + 54 <= by + bh - pad) {
        phone_card(pg, &in->phone, bx + pad, y, bw - 2 * pad);
        z->phone_y0 = y;
        z->phone_y1 = y + 52;
        y += 52 + gap / 2;
    }

    // ---- categorias: tamano REAL de cada conjunto de carpetas ----
    y += gap / 2;
    int row_h = flex_ui_line_h(2) + 12;
    for (int i = 0; i < FLEX_FSX_CAT_N; i++) {
        if (y + row_h > by + bh - pad) {
            break;
        }
        add_shape(pg, FST_DOT, bx + pad * 2 + 8, y + row_h / 2 - 2, 0, 0, 8, FSC_RGB, CAT_COL[i]);
        add_text(pg, FST_TEXT, bx + pad * 2 + 26, y, 2, FSC_TXT, CAT_NAME[i], 0);
        char sz[24];
        if (in->scanned && in->scan_err) {
            snprintf(sz, sizeof(sz), "--");
        } else if (in->scanned) {
            flex_fsn_fmt_size(in->cat[i], sz, sizeof(sz));
        } else {
            snprintf(sz, sizeof(sz), "...");   // se mide en la tarea de almacenamiento: no se inventa una cifra mientras tanto
        }
        add_text(pg, FST_TEXT_R, bx + bw - pad * 2, y, 2, FSC_TXT2, sz, 0);
        y += row_h;
    }

    // ---- archivos grandes: los mayores del sistema, de verdad ----
    // Diferencia con Arduino (documentada): la fila "Todos los archivos ... Ver..." es la UNICA entrada al explorador, asi que tiene prioridad
    // sobre los archivos grandes. Arduino la dejaba fuera cuando los archivos grandes ocupaban el sitio (con 640 px de alto, ya con dos) y el
    // explorador quedaba inalcanzable justo cuando mas archivos hay. Aqui un archivo grande solo se pinta si despues sigue cabiendo la fila.
    y += gap;
    const int bot = by + bh;
    const int ver_room = gap / 2 + 46;   // lo que necesita la fila de debajo: separacion y 46 px
    if (y + 40 < bot - pad) {
        int ty = y + flex_ui_line_h(2) + 6;   // primera linea bajo el titulo
        int nshow = 0;
        if (in->scanned && !in->scan_err) {
            bool ver_fits = ty + ver_room <= bot - pad / 2;   // con el titulo y sin ningun archivo, la fila cabe
            int yy = ty;
            for (int i = 0; i < in->nbig; i++) {
                if (yy + 44 > bot - pad) {
                    break;   // la regla de Arduino
                }
                if (ver_fits && yy + 46 + ver_room > bot - pad / 2) {
                    break;   // la fila de debajo tiene prioridad
                }
                nshow++;
                yy += 46;
            }
        }
        add_text(pg, FST_TEXT, bx + pad * 2, y, 2, FSC_TXT, "Archivos grandes", 0);
        y = ty;
        if (!in->scanned) {
            add_text(pg, FST_TEXT, bx + pad * 2, y, 1, FSC_MUTE, "Calculando...", 0);
            y += 22;
        } else if (in->scan_err) {
            add_text(pg, FST_TEXT, bx + pad * 2, y, 1, FSC_MUTE, "No se pudo leer", 0);
            y += 22;
        } else if (in->nbig == 0) {
            add_text(pg, FST_TEXT, bx + pad * 2, y, 1, FSC_MUTE, "No hay archivos guardados", 0);
            y += 22;
        }
        for (int i = 0; i < nshow; i++) {
            add_shape(pg, FST_FOLDER, bx + pad * 2, y, 34, 0, 0, FSC_TXT, 0);
            const char *nm = strrchr(in->big[i].path, '/');
            nm = nm ? nm + 1 : in->big[i].path;
            add_text(pg, FST_TEXT, bx + pad * 2 + 46, y, 2, FSC_TXT, nm, bx + bw - pad * 2);
            char sz[24], ln[sizeof(in->big[i].path) + 40];
            flex_fsn_fmt_size(in->big[i].size, sz, sizeof(sz));
            snprintf(ln, sizeof(ln), "%s  \xC2\xB7  %s", in->big[i].path, sz);
            add_text(pg, FST_TEXT, bx + pad * 2 + 46, y + 22, 1, FSC_TXT2, ln, bx + bw - pad);
            y += 46;
        }
    }

    // ---- fila "Todos los archivos ... Ver..." -> explorador REAL ----
    y += gap / 2;
    if (y + 46 <= by + bh - pad / 2) {
        add_shape(pg, FST_SURF, bx + pad, y, bw - 2 * pad, 44, 12, FSC_SURF_CARD, 0);
        add_text(pg, FST_TEXT, bx + pad * 2, y + 12, 2, FSC_TXT, "Todos los archivos", 0);
        add_text(pg, FST_TEXT_R, bx + bw - pad * 2, y + 12, 2, FSC_ACCS, "Ver...", 0);
        z->ver_y0 = y;
        z->ver_y1 = y + 44;
    }
}

int flex_st_main_hit(const flex_st_zones_t *z, int x, int y)
{
    if (z->phone_y1 > z->phone_y0 && y >= z->phone_y0 && y <= z->phone_y1) {
        return FST_HIT_PHONE;
    }
    if (z->det_y1 > z->det_y0 && y >= z->det_y0 && y <= z->det_y1) {
        return (z->opt_x1 > z->opt_x0 && x >= z->opt_x0 && x <= z->opt_x1) ? FST_HIT_OPTIMIZE : FST_HIT_DETAIL;
    }
    if (z->ver_y1 > z->ver_y0 && y >= z->ver_y0 && y <= z->ver_y1) {
        return FST_HIT_FILES;
    }
    return FST_HIT_NONE;
}

// ---- detalles de memoria y sistema -------------------------------------------------------------------------------------------------------
// valores de esp_reset_reason_t (esp_system.h; flex_boot_model.c ya comprueba que coinciden)
enum { RST_POWERON = 1, RST_EXT = 2, RST_SW = 3, RST_PANIC = 4, RST_INT_WDT = 5, RST_TASK_WDT = 6, RST_WDT = 7, RST_DEEPSLEEP = 8, RST_BROWNOUT = 9 };

const char *flex_st_boot_cause(int reason)
{
    switch (reason) {
    case RST_POWERON: return "Encendido normal";
    case RST_SW: return "Reinicio por software";
    case RST_DEEPSLEEP: return "Despertar de suspensi\xC3\xB3n";
    case RST_PANIC: return "Fallo del sistema (crash)";
    case RST_TASK_WDT: return "Watchdog de tarea";
    case RST_INT_WDT: return "Watchdog de interrupci\xC3\xB3n";
    case RST_WDT: return "Watchdog del chip";
    case RST_BROWNOUT: return "Ca\xC3\xAD" "da de tensi\xC3\xB3n";
    case RST_EXT: return "Reinicio externo";
    default: return "No disponible";
    }
}

// Las dos apps abiertas que mas PSRAM MEDIDA retienen. Sin medida no entran: una lista con numeros inventados no informa de nada.
static void top_apps(const flex_st_sys_t *s, int *a, int *b)
{
    *a = *b = -1;
    for (int i = 0; i < s->napps && i < FLEX_ST_APPS; i++) {
        if (s->app_state[i] == 0 || !s->app_mem_has[i] || s->app_mem[i] == 0) {
            continue;
        }
        if (*a < 0 || s->app_mem[i] > s->app_mem[*a]) {
            *b = *a;
            *a = i;
        } else if (*b < 0 || s->app_mem[i] > s->app_mem[*b]) {
            *b = i;
        }
    }
}

typedef struct {
    flex_st_page_t *pg;
    int pad;
} sec_t;

// almDetRow: etiqueta a la izquierda (recortada a 260), valor a la derecha (y - 3)
static int det_row(sec_t *c, int y, const char *label, const char *value, int role)
{
    add_text(c->pg, FST_TEXT, c->pad * 2, y, 1, FSC_TXT2, label, 480 / 2 + 20);
    add_text(c->pg, FST_TEXT_R, 480 - c->pad * 2, y - 3, 2, role, value, 0);
    return y + 24;
}

static int det_note(sec_t *c, int y, const char *txt)
{
    add_text(c->pg, FST_TEXT, c->pad * 2, y, 1, FSC_MUTE, txt, 480 - c->pad * 2);
    return y + 18;
}

static int det_title(sec_t *c, int y, const char *txt)
{
    add_text(c->pg, FST_TEXT, c->pad * 2, y, 2, FSC_TXT, txt, 0);
    return y + 26;
}

void flex_st_detail_section(flex_st_page_t *pg, int sec, const flex_st_sys_t *s)
{
    pg->n = 0;
    sec_t c = {pg, flex_ui_pad(480, 640)};   // uiPad() de la caja de la app a pantalla completa: 20
    const FlexMemSnap *m = &s->m;
    char v[96];
    int y = 8;
    switch (sec) {
    case 0:   // cabecera + acceso a Optimizar
        add_text(pg, FST_TEXT, c.pad * 2, y, 2, FSC_TXT, "Detalles de memoria y sistema", 0);
        y += 26;
        add_shape(pg, FST_SURF, FLEX_ST_OPT_X, y, FLEX_ST_OPT_W, FLEX_ST_OPT_H, FLEX_ST_OPT_H / 2, FSC_SURF_ELEV, 0);
        add_shape(pg, FST_OUTLINE, FLEX_ST_OPT_X, y, FLEX_ST_OPT_W, FLEX_ST_OPT_H, FLEX_ST_OPT_H / 2, FSC_BORDER, 0);
        add_text(pg, FST_TEXT_C, 480 / 2, y + (FLEX_ST_OPT_H - 18) / 2, 2, FSC_ACCS, "Optimizar Flex OS", 0);
        break;
    case 1: {   // PSRAM
        y = det_title(&c, y, "PSRAM (memoria de trabajo)");
        if (!m->psTotal) {
            det_note(&c, y, "No disponible en esta placa.");
            break;
        }
        flexMemFmt(m->psTotal, v, sizeof(v));
        y = det_row(&c, y, "Total detectada", v, FSC_TXT);
        char u[24];
        flexMemFmt(flexMemUsed(m), u, sizeof(u));
        snprintf(v, sizeof(v), "%s (%d%%)", u, flexMemUsedPct(m));
        y = det_row(&c, y, "En uso", v, FSC_TXT);
        flexMemFmt(m->psFree, v, sizeof(v));
        y = det_row(&c, y, "Libre", v, FSC_TXT);
        flexMemFmt(m->psPeakUsed, v, sizeof(v));
        y = det_row(&c, y, "Pico de uso desde el arranque", v, FSC_TXT2);
        flexMemFmt(m->psLargest, v, sizeof(v));
        y = det_row(&c, y, "Bloque libre m\xC3\xA1s grande", v, FSC_TXT2);
        int fc = flexMemFragClass(m);
        snprintf(v, sizeof(v), "%s (%d%%)", fc == FLEXMEM_FRAG_HIGH ? "Alta" : (fc == FLEXMEM_FRAG_MED ? "Media" : "Baja"), flexMemFragPct(m));
        y = det_row(&c, y, "Fragmentaci\xC3\xB3n", v, fc == FLEXMEM_FRAG_HIGH ? FSC_WARN : FSC_TXT2);
        int hh = flexMemHealth(m);
        y = det_row(&c, y, "Estado", hh == FLEXMEM_H_CRIT ? "Cr\xC3\xADtico" : (hh == FLEXMEM_H_WATCH ? "Atenci\xC3\xB3n" : "\xC3\x93ptimo"),
                    hh == FLEXMEM_H_CRIT ? FSC_ERR : (hh == FLEXMEM_H_WATCH ? FSC_WARN : FSC_OK));
        int ta, tb;
        top_apps(s, &ta, &tb);
        if (ta >= 0) {
            char m1[24];
            flexMemFmt(s->app_mem[ta], m1, sizeof(m1));
            if (tb >= 0) {
                char m2[24];
                flexMemFmt(s->app_mem[tb], m2, sizeof(m2));
                snprintf(v, sizeof(v), "Mayor consumo: %s %s  \xC2\xB7  %s %s", s->app_name[ta], m1, s->app_name[tb], m2);
            } else {
                snprintf(v, sizeof(v), "Mayor consumo: %s %s", s->app_name[ta], m1);
            }
            y = det_note(&c, y, v);
        } else {
            y = det_note(&c, y, "Mayor consumo: sin medida todav\xC3\xAD" "a.");
        }
        det_note(&c, y, "La usan las apps abiertas, las im\xC3\xA1genes y el doble b\xC3\xBA" "fer de pantalla.");
        break;
    }
    case 2:   // SRAM interna
        y = det_title(&c, y, "Memoria interna del chip (SRAM)");
        if (!m->inTotal) {
            det_note(&c, y, "No disponible.");
            break;
        }
        flexMemFmt(m->inTotal, v, sizeof(v));
        y = det_row(&c, y, "Total utilizable", v, FSC_TXT);
        flexMemFmt(m->inTotal > m->inFree ? m->inTotal - m->inFree : 0, v, sizeof(v));
        y = det_row(&c, y, "En uso", v, FSC_TXT);
        flexMemFmt(m->inFree, v, sizeof(v));
        y = det_row(&c, y, "Libre", v, m->inFree < FLEXMEM_SRAM_LOW_BYTES ? FSC_WARN : FSC_TXT);
        flexMemFmt(m->inMin, v, sizeof(v));
        y = det_row(&c, y, "M\xC3\xADnimo desde el arranque", v, FSC_TXT2);
        y = det_note(&c, y, "La usan el sistema, el t\xC3\xA1" "ctil, el Wi-Fi y las tareas internas.");
        det_note(&c, y, "No conserva datos: al reiniciar se vac\xC3\xAD" "a entera.");
        break;
    case 3: {   // flash / LittleFS
        y = det_title(&c, y, "Almacenamiento interno (flash)");
        if (!m->fsValid) {
            det_note(&c, y, "No disponible: la partici\xC3\xB3n de datos no est\xC3\xA1 montada.");
            break;
        }
        flexMemFmt(m->fsTotal, v, sizeof(v));
        y = det_row(&c, y, "Capacidad de datos", v, FSC_TXT);
        {
            char u[24];
            flexMemFmt(m->fsUsed, u, sizeof(u));
            int fp = flexMemFlashPct(m);
            snprintf(v, sizeof(v), "%s (%d%%)", u, fp < 0 ? 0 : fp);
            y = det_row(&c, y, "Usado", v, fp >= 90 ? FSC_ERR : (fp >= 80 ? FSC_WARN : FSC_TXT));
        }
        flexMemFmt(m->fsTotal > m->fsUsed ? m->fsTotal - m->fsUsed : 0, v, sizeof(v));
        y = det_row(&c, y, "Libre", v, FSC_TXT);
        if (s->fw_size != 0xFFFFFFFFu && s->fw_size > 0) {
            flexMemFmt(s->fw_size, v, sizeof(v));
        } else {
            snprintf(v, sizeof(v), "No disponible");
        }
        y = det_row(&c, y, "Tama\xC3\xB1o del firmware", v, FSC_TXT2);
        flexMemFmt(s->cat_sys, v, sizeof(v));
        y = det_row(&c, y, "Recursos del sistema", v, FSC_TXT2);
        flexMemFmt(s->cat_apps, v, sizeof(v));
        y = det_row(&c, y, "Datos de apps", v, FSC_TXT2);
        flexMemFmt(s->cache, v, sizeof(v));
        y = det_row(&c, y, "Cach\xC3\xA9 temporal", v, FSC_TXT2);
        flexMemFmt(s->cat_docs, v, sizeof(v));
        det_row(&c, y, "Archivos de usuario", v, FSC_TXT2);
        break;
    }
    case 4: {   // estado del sistema
        y = det_title(&c, y, "Estado del sistema");
        snprintf(v, sizeof(v), "%s", s->uptime);
        y = det_row(&c, y, "Tiempo encendido", v, FSC_TXT);
        snprintf(v, sizeof(v), "%s", flex_st_boot_cause(s->reset_reason));
        y = det_row(&c, y, "\xC3\x9Altimo arranque", v, FSC_TXT2);
        snprintf(v, sizeof(v), "Flex OS %s", s->version);
        y = det_row(&c, y, "Versi\xC3\xB3n", v, FSC_TXT2);
        snprintf(v, sizeof(v), "%s", s->net_online ? "Conectado" : "Desconectado");
        y = det_row(&c, y, "Wi-Fi", v, s->net_online ? FSC_OK : FSC_TXT2);
        snprintf(v, sizeof(v), "%u vueltas/s", s->loop_rate);
        y = det_row(&c, y, "Ritmo del sistema", v, FSC_TXT2);
        {
            int act = 0, pau = 0, sus = 0;
            for (int i = 0; i < s->napps && i < FLEX_ST_APPS; i++) {
                if (s->app_state[i] == 0) {
                    continue;
                }
                if (s->app_state[i] == 2) {
                    if (s->app_shed[i]) {
                        sus++;
                    } else {
                        pau++;
                    }
                } else {
                    act++;
                }
            }
            snprintf(v, sizeof(v), "%d / %d / %d", act, pau, sus);
        }
        y = det_row(&c, y, "Apps activas / pausadas / guardadas", v, FSC_TXT2);
        det_note(&c, y, "No hay medidor global de FPS: el ritmo del bucle es la medida real.");
        break;
    }
    default:
        break;
    }
}

uint32_t flex_st_detail_sig(int sec, const flex_st_sys_t *s)
{
    const FlexMemSnap *m = &s->m;
    switch (sec) {
    case 1:
        return m->psFree ^ (m->psLargest << 1) ^ (m->psPeakUsed >> 3) ^ m->psTotal;
    case 2:
        return m->inFree ^ (m->inMin << 1) ^ m->inTotal;
    case 3:
        return m->fsUsed ^ (m->fsTotal << 1) ^ (uint32_t)m->fsValid;
    case 4: {
        uint32_t apps = 0;
        for (int i = 0; i < s->napps && i < FLEX_ST_APPS; i++) {
            apps = apps * 3u + s->app_state[i];
        }
        return s->uptime_min ^ (apps << 3) ^ (uint32_t)(s->net_online ? 1 : 0) ^ (s->loop_rate << 16);
    }
    default:
        return 0;
    }
}

bool flex_st_opt_hit(int scroll, int x, int y)
{
    int sy = FLEX_ST_DET_VY0 + flex_st_sec_top(0) - scroll;
    int by = sy + FLEX_ST_OPT_Y;
    return y >= by && y <= by + FLEX_ST_OPT_H && x >= FLEX_ST_OPT_X && x <= FLEX_ST_OPT_X + FLEX_ST_OPT_W;
}
