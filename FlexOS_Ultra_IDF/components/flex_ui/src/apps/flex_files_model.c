#include "flex_files_model.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "FlexOS_Media.h"
#include "flex_fs_names.h"

#define SCR_W 480
#define RGB565(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))
#define LIB_DIR "/System/Media"      // FML_DIR_ROOT
#define SYS_DIR "/System"            // FLEXFS_DIR_SYS

// ---- ruta y biblioteca ------------------------------------------------------------------------------------------------------------------
bool flex_files_is_library(const char *dir)
{
    size_t n = strlen(LIB_DIR);
    return !strncmp(dir, LIB_DIR, n) && (dir[n] == 0 || dir[n] == '/');
}

void flex_files_enter_at(flex_files_t *f, const char *dir)
{
    snprintf(f->dir, sizeof(f->dir), "%s", dir && dir[0] ? dir : "/");
    f->sel = -1;
    f->multi = false;
    memset(f->mark, 0, sizeof(f->mark));
    f->n = 0;
    f->total = 0;
}

const char *flex_files_reload_dir(flex_files_t *f)
{
    if (flex_files_is_library(f->dir)) {
        snprintf(f->dir, sizeof(f->dir), "%s", SYS_DIR);
    }
    return f->dir;
}

void flex_files_set_list(flex_files_t *f, const flex_fsx_ent_t *src, int n, size_t total)
{
    if (n < 0) {
        n = 0;
    }
    if (n > FLEX_FILES_MAX) {
        n = FLEX_FILES_MAX;
    }
    if (src != f->ent) {
        memcpy(f->ent, src, (size_t)n * sizeof(*src));
    }
    f->n = n;
    f->total = total;
    if (!strcmp(f->dir, SYS_DIR)) {
        for (int i = 0; i < f->n;) {
            char p[FLEX_FSX_PATH_MAX];
            if (snprintf(p, sizeof(p), "%s/%s", f->dir, f->ent[i].name) >= (int)sizeof(p)) {
                i++;
                continue;
            }
            if (flex_files_is_library(p)) {   // se quita conservando el orden
                memmove(&f->ent[i], &f->ent[i + 1], sizeof(f->ent[0]) * (size_t)(f->n - 1 - i));
                f->n--;
                continue;
            }
            i++;
        }
    }
    if (f->sel >= f->n) {
        f->sel = -1;
    }
}

bool flex_files_has_up(const flex_files_t *f)
{
    return strcmp(f->dir, "/") != 0;
}

int flex_files_rows(const flex_files_t *f)
{
    return f->n + (flex_files_has_up(f) ? 1 : 0);
}

int flex_files_row_y(int row)
{
    return FLEX_FILES_TOP + row * FLEX_FILES_RH;
}

void flex_files_path_of(const flex_files_t *f, int i, char *out, size_t n)
{
    // flex_fsx_list no entrega entradas cuya ruta no quepa en FLEX_FSX_PATH_MAX, asi que aqui nunca se recorta; si n fuese menor, se corta
    // sin desbordar (a mano: un snprintf("%s/%s") con dos cadenas acotadas no puede demostrar al compilador que no se trunca)
    size_t k = 0;
    const char *dir = !strcmp(f->dir, "/") ? "" : f->dir;
    for (const char *s = dir; *s && k + 1 < n; s++) {
        out[k++] = *s;
    }
    if (k + 1 < n) {
        out[k++] = '/';
    }
    for (const char *s = f->ent[i].name; *s && k + 1 < n; s++) {
        out[k++] = *s;
    }
    if (n) {
        out[k] = 0;
    }
}

void flex_files_end_multi(flex_files_t *f)
{
    f->multi = false;
    memset(f->mark, 0, sizeof(f->mark));
}

void flex_files_go_up(flex_files_t *f)
{
    char *s = strrchr(f->dir, '/');
    if (!s || s == f->dir) {
        snprintf(f->dir, sizeof(f->dir), "/");
    } else {
        *s = 0;
    }
    if (f->dir[0] == 0) {   // nunca por encima de la raiz
        snprintf(f->dir, sizeof(f->dir), "/");
    }
    f->sel = -1;
    f->multi = false;
    memset(f->mark, 0, sizeof(f->mark));
}

// ---- toque -----------------------------------------------------------------------------------------------------------------------------------------
static void out_clear(flex_files_out_t *o)
{
    memset(o, 0, sizeof(*o));
    o->act = FILES_A_NONE;
    o->idx = -1;
}

void flex_files_open_entry(const flex_files_t *f, int i, flex_files_out_t *out)
{
    out_clear(out);
    const char *name = f->ent[i].name;
    int kind = flexMediaClassify(name);
    out->idx = i;
    if (kind == FLEXMED_PHOTO || kind == FLEXMED_DRAW || kind == FLEXMED_VIDEO || kind == FLEXMED_AUDIO) {
        out->act = FILES_A_OPEN_MEDIA;   // al salir del visor se vuelve a Almacenamiento
        return;
    }
    if (kind == FLEXMED_UNSUP) {
        const char *why = flexMediaUnsupportedReason(name);
        out->act = FILES_A_NOTIFY;
        snprintf(out->text, sizeof(out->text), "%s", why ? why : "Formato no compatible");
        return;
    }
    // Ni medio ni conocido: las acciones de siempre sobre el fichero
    out->act = FILES_A_MENU;
    out->x = SCR_W / 2 - 60;
    out->y = 200;
    out->cloud = false;
}

void flex_files_tap(flex_files_t *f, int scroll, int vp_bottom, int x, int y, flex_files_out_t *out)
{
    out_clear(out);
    // zonas de la cabecera (las mismas que dibuja uiHdrDraw)
    if (x >= SCR_W - FLEX_FILES_ZONE && x < SCR_W && y >= 0 && y < FLEX_FILES_ZONE) {
        f->sel = -1;
        out->act = FILES_A_MENU;
        out->x = SCR_W - 40;
        out->y = FLEX_FILES_ZONE;
        return;
    }
    if (x >= 0 && x < FLEX_FILES_ZONE && y >= 0 && y < FLEX_FILES_ZONE) {
        out->act = FILES_A_EXIT;
        return;
    }
    if (flex_files_seg_hit(x, y) == 1) {
        f->multi = false;
        memset(f->mark, 0, sizeof(f->mark));
        out->act = FILES_A_CLOUD_TAB;
        return;
    }
    if (f->multi) {
        int by = FLEX_FILES_BAR_Y;
        if (y >= by && y <= by + FLEX_FILES_BAR_H) {
            if (x > SCR_W - 100) {
                f->multi = false;
                memset(f->mark, 0, sizeof(f->mark));
                out->act = FILES_A_MULTI_EXIT;
                return;
            }
            if (x > SCR_W - 230) {
                out->act = FILES_A_MULTI_TRASH;
                return;
            }
        }
    }
    // la lista solo recoge toques dentro de su ventana (Arduino los aceptaba tambien sobre la cabecera y la ruta si una fila
    // desplazada pasaba por ahi)
    if (y < FLEX_FILES_VP_TOP || y >= vp_bottom) {
        return;
    }
    int base = flex_files_has_up(f) ? 1 : 0;
    if (base) {
        int y0 = flex_files_row_y(0) - scroll;
        if (y >= y0 && y < y0 + FLEX_FILES_RH - 8) {
            flex_files_go_up(f);
            out->act = FILES_A_UP;
            return;
        }
    }
    for (int i = 0; i < f->n; i++) {
        int y0 = flex_files_row_y(base + i) - scroll;
        if (y >= y0 && y < y0 + FLEX_FILES_RH - 8) {
            out->idx = i;
            if (f->multi) {
                f->mark[i] ^= 1;
                out->act = FILES_A_TOGGLE;
            } else if (f->ent[i].dir) {
                char p[FLEX_FSX_PATH_MAX];
                flex_files_path_of(f, i, p, sizeof(p));
                snprintf(f->dir, sizeof(f->dir), "%s", p);
                f->sel = -1;
                out->act = FILES_A_ENTER;
            } else {
                f->sel = i;
                flex_files_open_entry(f, i, out);   // un toque corto abre; la pulsacion larga saca el menu
            }
            return;
        }
    }
}

int flex_files_seg_hit(int px, int py)
{
    if (py < FLEX_FILES_SEG_Y || py > FLEX_FILES_SEG_Y + FLEX_FILES_SEG_H || px < 16 || px > SCR_W - 16) {
        return -1;
    }
    return px < SCR_W / 2 ? 0 : 1;
}

int flex_files_max_scroll(const flex_files_t *f, int view_bottom)
{
    int need = FLEX_FILES_TOP + flex_files_rows(f) * FLEX_FILES_RH + 30;
    int m = need - view_bottom;
    return m > 0 ? m : 0;
}

bool flex_files_long(flex_files_t *f, int scroll, int vp_bottom, bool scrollable, int sx, int sy, int x, int y, unsigned held_ms,
                     flex_files_out_t *out)
{
    out_clear(out);
    // filesTick: con scroll, mas de 8 px en vertical es un arrastre y se sale ANTES de mirar la pulsacion larga
    if (scrollable && abs(sy - y) > 8) {
        return false;
    }
    if (held_ms <= FLEX_FILES_LONG_MS || abs(x - sx) >= FLEX_FILES_LONG_PX || abs(y - sy) >= FLEX_FILES_LONG_PX) {
        return false;
    }
    if (sy < FLEX_FILES_VP_TOP || sy >= vp_bottom) {
        return false;
    }
    int base = flex_files_has_up(f) ? 1 : 0;
    for (int i = 0; i < f->n; i++) {
        int y0 = flex_files_row_y(base + i) - scroll;
        if (sy >= y0 && sy < y0 + FLEX_FILES_RH - 8) {
            f->sel = i;
            out->act = FILES_A_MENU;
            out->idx = i;
            out->x = x;
            out->y = y - 40;
            out->cloud = !f->ent[i].dir;   // un archivo se puede subir; una carpeta, no
            return true;
        }
    }
    return false;
}

void flex_files_menu_act(flex_files_t *f, int act, flex_files_out_t *out)
{
    out_clear(out);
    bool has = f->sel >= 0 && f->sel < f->n;
    out->idx = has ? f->sel : -1;
    switch (act) {
    case 4:   // FLEX_FK_ACT_CLOUD
        if (has && !f->ent[f->sel].dir) {
            out->act = FILES_A_UPLOAD;
        }
        break;
    case 0:   // FLEX_FK_ACT_SEL
        f->multi = true;
        memset(f->mark, 0, sizeof(f->mark));
        if (f->sel >= 0 && f->sel < FLEX_FILES_MAX) {
            f->mark[f->sel] = 1;
        }
        break;
    case 1:   // FLEX_FK_ACT_DEL
        if (has) {
            out->act = FILES_A_ASK_DELETE;
        }
        break;
    case 2:   // FLEX_FK_ACT_REN
        if (has) {
            out->act = FILES_A_NAME;
            flex_fsn_stem(f->ent[f->sel].name, out->text, sizeof(out->text));
        }
        break;
    case 3:   // FLEX_FK_ACT_TRASH
        if (has) {
            out->act = FILES_A_TRASH_ONE;   // idx = lo que se tira (la seleccion de antes)
            f->sel = -1;
        } else {
            out->act = FILES_A_OPEN_TRASH;
        }
        break;
    default:
        break;
    }
}

void flex_files_ask_done(flex_files_t *f, bool ok, flex_files_out_t *out)
{
    out_clear(out);
    if (ok && f->sel >= 0 && f->sel < f->n) {
        out->act = FILES_A_DELETE;
        out->idx = f->sel;
        f->sel = -1;
    }
}

void flex_files_name_done(flex_files_t *f, const char *name, flex_files_out_t *out)
{
    out_clear(out);
    if (name && f->sel >= 0 && f->sel < f->n) {
        out->act = FILES_A_RENAME;
        out->idx = f->sel;
        snprintf(out->text, sizeof(out->text), "%s", name);
        f->sel = -1;
    }
}

// ---- contenido de las filas ------------------------------------------------------------------------------------------------------------------
void flex_files_sub(const flex_fsx_ent_t *e, char *out, size_t n)
{
    if (e->dir && e->items == 0xFFFF) {
        snprintf(out, n, "Carpeta");   // 0xFFFF = "no se sabe": se dice Carpeta en vez de inventar un conteo
    } else if (e->dir) {
        snprintf(out, n, "%u elementos", (unsigned)e->items);
    } else {
        flex_fsn_fmt_size(e->size, out, n);
    }
}

static void put(flex_st_page_t *pg, int kind, int x, int y, int w, int h, int r, int role, uint16_t rgb, int size, int max_right, const char *text)
{
    if (pg->n >= FLEX_ST_OPS_MAX) {
        return;
    }
    flex_st_op_t *o = &pg->op[pg->n++];
    memset(o, 0, sizeof(*o));
    o->kind = (uint8_t)kind;
    o->x = (int16_t)x;
    o->y = (int16_t)y;
    o->w = (int16_t)w;
    o->h = (int16_t)h;
    o->r = (int16_t)r;
    o->role = (uint8_t)role;
    o->rgb = rgb;
    o->size = (uint8_t)size;
    o->max_right = (int16_t)max_right;
    if (text) {
        snprintf(o->text, sizeof(o->text), "%.*s", (int)sizeof(o->text) - 1, text);
    }
}

void flex_files_row_ops(const flex_files_t *f, int row, int y, flex_st_page_t *pg)
{
    pg->n = 0;
    bool up = flex_files_has_up(f);
    if (up && row == 0) {
        put(pg, FST_SURF, 12, y, SCR_W - 24, FLEX_FILES_RH - 8, 12, FSC_SURF_CARD, 0, 0, 0, NULL);
        put(pg, FST_FOLDER, 28, y + 12, 38, 0, 0, FSC_TXT, 0, 0, 0, NULL);
        put(pg, FST_TEXT, 84, y + 18, 0, 0, 0, FSC_TXT, 0, 3, 0, "..");
        put(pg, FST_TEXT_R, SCR_W - 28, y + 22, 0, 0, 0, FSC_TXT2, 0, 1, 0, "subir");
        return;
    }
    int i = row - (up ? 1 : 0);
    if (i < 0 || i >= f->n) {
        return;
    }
    const flex_fsx_ent_t *e = &f->ent[i];
    bool sel = f->multi && f->mark[i];
    if (sel) {
        put(pg, FST_RRECT, 12, y, SCR_W - 24, FLEX_FILES_RH - 8, 12, FSC_SEL, 0, 0, 0, NULL);
    } else {
        put(pg, FST_SURF, 12, y, SCR_W - 24, FLEX_FILES_RH - 8, 12, FSC_SURF_CARD, 0, 0, 0, NULL);
    }
    if (e->dir) {
        put(pg, FST_FOLDER, 28, y + 12, 38, 0, 0, FSC_TXT, 0, 0, 0, NULL);
    } else {   // hoja con la banda de arriba
        put(pg, FST_RRECT, 30, y + 10, 30, 42, 5, FSC_RGB, RGB565(250, 250, 252), 0, 0, NULL);
        put(pg, FST_RRECT, 30, y + 10, 30, 8, 0, FSC_RGB, RGB565(200, 204, 214), 0, 0, NULL);
    }
    put(pg, FST_TEXT, 84, y + 10, 0, 0, 0, FSC_TXT, 0, 3, SCR_W - 150, e->name);
    char sub[32];
    flex_files_sub(e, sub, sizeof(sub));
    put(pg, FST_TEXT_R, SCR_W - 28, y + 40, 0, 0, 0, FSC_TXT2, 0, 2, 0, sub);
}

void flex_files_bar_ops(flex_st_page_t *pg)
{
    pg->n = 0;
    int by = FLEX_FILES_BAR_Y;
    put(pg, FST_SURF, 12, by, SCR_W - 24, FLEX_FILES_BAR_H, 16, FSC_SURF_ELEV, 0, 0, 0, NULL);
    put(pg, FST_TEXT, 28, by + 20, 0, 0, 0, FSC_TXT, 0, 2, 0, "Selecci\xC3\xB3n");
    put(pg, FST_TEXT_R, SCR_W - 140, by + 20, 0, 0, 0, FSC_WARN, 0, 2, 0, "Papelera");
    put(pg, FST_TEXT_R, SCR_W - 28, by + 20, 0, 0, 0, FSC_TXT2, 0, 2, 0, "Salir");
}
