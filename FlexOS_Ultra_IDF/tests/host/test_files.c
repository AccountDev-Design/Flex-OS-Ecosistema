// Archivos, la mitad local (docs/spec/03 §8): recarga con la biblioteca oculta, subir un nivel, entrar en carpetas, abrir un fichero por su clase,
// seleccion multiple, menu contextual con sus acciones, pulsacion larga, resultados de los dialogos y el contenido de las filas y de la barra
// frente a FlexOS_Ultra_AppFiles.h de Arduino, compilado tal cual con dobles que registran lo que se les pide.
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "flex_files_model.h"
#include "flex_test.h"

void ref_files_src(const char *names, const unsigned *sizes, const int *dirs, const int *items, int n);
int ref_files_enter(const char *dir, char *dir_out, int cap, char *names_out, int ncap);
void ref_files_set(int sel, int multi, unsigned mask, int scroll);
void ref_files_get(char *dir, int cap, int *sel, int *multi, unsigned *mask, int *n);
int ref_files_tap(int x, int y, char *act, int cap);
int ref_files_hold(int sx, int sy, int x, int y, unsigned held, char *act, int cap);
int ref_files_menu(int act_id, char *act, int cap);
int ref_files_ask(int ret, char *act, int cap);
int ref_files_name(int ret, const char *name, char *act, int cap);
int ref_files_render(int scroll, int multi, unsigned mask, char *log, int cap);

static uint32_t s_seed = 21;
static uint32_t rnd(void)
{
    s_seed = s_seed * 1103515245u + 12345u;
    return s_seed >> 8;
}

#define NMAX 24
static flex_fsx_ent_t s_ents[FLEX_FILES_MAX];
static flex_files_t F;

static const char *const NAMES[] = {"a.jpg", "B.JPEG", "c.avi", "d.wav", "e.fxp", "f.mp3", "g.png", "h.txt", "i.bin", "noext", ".oculto", "k.tar.gz", "Mi nota.txt"};

static void make_src(int n, char *names, unsigned *sizes, int *dirs, int *items, bool with_lib)
{
    memset(sizes, 0, sizeof(unsigned) * NMAX);
    memset(dirs, 0, sizeof(int) * NMAX);
    memset(items, 0, sizeof(int) * NMAX);
    char *p = names;
    for (int i = 0; i < n; i++) {
        bool d = rnd() % 3 == 0;
        const char *nm;
        char buf[48];
        if (d) {
            if (with_lib && i == 0) {
                nm = "Media";
            } else {
                snprintf(buf, sizeof(buf), "Carpeta %d", i);
                nm = buf;
            }
        } else {
            snprintf(buf, sizeof(buf), "%d_%s", i, NAMES[rnd() % 13]);
            nm = buf;
        }
        strcpy(p, nm);
        p += strlen(nm) + 1;
        sizes[i] = d ? 0 : rnd() % 4000000u;
        dirs[i] = d;
        items[i] = d ? (int)(rnd() % 12 == 0 ? 0xFFFF : rnd() % 40) : 0;
    }
}

static void to_model(int n, const char *names, const unsigned *sizes, const int *dirs, const int *items, flex_fsx_ent_t *out)
{
    const char *p = names;
    for (int i = 0; i < n; i++) {
        memset(&out[i], 0, sizeof(out[i]));
        size_t L = strlen(p);
        memcpy(out[i].name, p, L < sizeof(out[i].name) - 1 ? L : sizeof(out[i].name) - 1);
        out[i].name[L < sizeof(out[i].name) - 1 ? L : sizeof(out[i].name) - 1] = 0;
        p += L + 1;
        out[i].size = sizes[i];
        out[i].dir = dirs[i] != 0;
        out[i].items = (uint16_t)items[i];
    }
}

typedef struct {
    char *p;
    int cap, n;
} sb_t;
static void sb_add(sb_t *s, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int k = vsnprintf(s->p + s->n, (size_t)(s->cap - s->n), fmt, ap);
    va_end(ap);
    if (k > 0 && s->n + k < s->cap) {
        s->n += k;
    }
}
static unsigned tok(int role, unsigned rgb)
{
    switch (role) {
    case FSC_TXT: return 0xF101;
    case FSC_TXT2: return 0xF102;
    case FSC_MUTE: return 0xF103;
    case FSC_WARN: return 0xF111;
    case FSC_SEL: return 0xF117;
    default: return rgb;
    }
}
static void expand(const flex_st_page_t *pg, sb_t *s)
{
    for (int i = 0; i < pg->n; i++) {
        const flex_st_op_t *o = &pg->op[i];
        switch (o->kind) {
        case FST_SURF:
            sb_add(s, "Q%d,%d,%d,%d,%d,%u;", o->x, o->y, o->w, o->h, o->r, o->role == FSC_SURF_ELEV ? 0xF10Du : 0xF105u);
            break;
        case FST_RRECT:
            sb_add(s, "Q%d,%d,%d,%d,%d,%u;", o->x, o->y, o->w, o->h, o->r, tok(o->role, o->rgb));
            break;
        case FST_FOLDER:
            sb_add(s, "I%d,%d,%d;", o->x, o->y, o->w);
            break;
        case FST_TEXT:
            if (o->max_right > 0) sb_add(s, "K%d,%d,%d,%u,%d:%s;", o->x, o->y, o->size, tok(o->role, 0), o->max_right, o->text);
            else sb_add(s, "T%d,%d,%d,%u:%s;", o->x, o->y, o->size, tok(o->role, 0), o->text);
            break;
        case FST_TEXT_R:
            sb_add(s, "N%d,%d,%d,%u:%s;", o->x, o->y, o->size, tok(o->role, 0), o->text);
            break;
        case FST_TEXT_C:
            sb_add(s, "M%d,%d,%d,%u:%s;", o->x, o->y, o->size, tok(o->role, 0), o->text);
            break;
        default:
            break;
        }
    }
}

static void show_diff(const char *what, const char *a, const char *b)
{
    size_t i = 0;
    while (a[i] && a[i] == b[i]) i++;
    size_t s = i > 60 ? i - 60 : 0;
    fprintf(stderr, "  %s: difieren en %zu\n  ref: ...%.200s\n  mod: ...%.200s\n", what, i, a + s, b + s);
}

// estado cargado en los dos: carpeta, lista, seleccion, modo seleccion
static void load_both(const char *dir, int n, bool with_lib, int sel, bool multi, unsigned mask, int scroll)
{
    static char names[NMAX * 60];
    unsigned sizes[NMAX];
    int dirs[NMAX], items[NMAX];
    make_src(n, names, sizes, dirs, items, with_lib);
    ref_files_src(names, sizes, dirs, items, n);
    char rdir[160], rnames[2000];
    ref_files_enter(dir, rdir, sizeof(rdir), rnames, sizeof(rnames));
    flex_fsx_ent_t src[NMAX];
    to_model(n, names, sizes, dirs, items, src);
    F.ent = s_ents;
    flex_files_enter_at(&F, dir);
    flex_files_reload_dir(&F);
    flex_files_set_list(&F, src, n, (size_t)n);
    CHECK(strcmp(rdir, F.dir) == 0);
    // los nombres que quedan en la lista
    char mn[2000] = "";
    for (int i = 0; i < F.n; i++) snprintf(mn + strlen(mn), sizeof(mn) - strlen(mn), "%s;", F.ent[i].name);
    if (strcmp(rnames, mn) != 0) show_diff("lista tras recargar", rnames, mn);
    CHECK(strcmp(rnames, mn) == 0);
    if (sel >= F.n) sel = -1;
    F.sel = sel;
    F.multi = multi;
    for (int i = 0; i < FLEX_FILES_MAX; i++) F.mark[i] = (i < 32 && (mask >> i & 1) && multi) ? 1 : 0;
    unsigned m = 0;
    for (int i = 0; i < 32 && i < F.n; i++) m |= (unsigned)(F.mark[i] ? 1u : 0u) << i;
    ref_files_set(sel, multi, m, scroll);
}

static void t_reload(void)
{
    s_seed = 1234;
    const char *const dirs[] = {"/", "/System", "/System/Media", "/System/Media/Thumbs", "/System/Mediax", "/Documentos", "/Paint/x"};   // (anidadas: ver t_taps)
    for (int it = 0; it < 2000; it++) {
        const char *d = dirs[rnd() % 7];
        int n = (int)(rnd() % (NMAX + 1));
        load_both(d, n, d[1] == 'S' && strlen(d) == 7, -1, false, 0, 0);
    }
    CHECK(flex_files_is_library("/System/Media") && flex_files_is_library("/System/Media/x") && !flex_files_is_library("/System/Mediax") &&
          !flex_files_is_library("/System"));
}

// normaliza lo que hizo el modelo al registro del doble de Arduino
static void model_act_string(flex_files_t *f, const flex_files_out_t *o, char *out, size_t cap, bool *cloud_tab)
{
    out[0] = 0;
    *cloud_tab = false;
    char p[FLEX_FSX_PATH_MAX];
    switch (o->act) {
    case FILES_A_MENU:
        snprintf(out, cap, "MENU:%d,%d,%d;", o->x, o->y, o->cloud ? 1 : 0);
        break;
    case FILES_A_EXIT:
        snprintf(out, cap, "EXIT;");
        break;
    case FILES_A_CLOUD_TAB:
        *cloud_tab = true;
        snprintf(out, cap, "CLOUD;");
        break;
    case FILES_A_MULTI_TRASH:
        for (int i = 0; i < f->n; i++)
            if (f->mark[i]) {
                flex_files_path_of(f, i, p, sizeof(p));
                snprintf(out + strlen(out), cap - strlen(out), "TRASH:%s;", p);
            }
        flex_files_end_multi(f);
        break;
    case FILES_A_OPEN_MEDIA:
        flex_files_path_of(f, o->idx, p, sizeof(p));
        snprintf(out, cap, "PLAY:%s;", p);
        break;
    case FILES_A_NOTIFY:
        snprintf(out, cap, "NOTE:%s:%s;", f->ent[o->idx].name, o->text);
        break;
    case FILES_A_UPLOAD:
        flex_files_path_of(f, o->idx, p, sizeof(p));
        // la peticion y, en Arduino, el aviso de que se encolo (la interfaz solo lo da si el servicio de nube existe)
        snprintf(out, cap, "UP:%s:%s;SYS:%s:Subiendo a Flex Cloud (se conserva aqu\xC3\xAD);", p, f->ent[o->idx].name, f->ent[o->idx].name);
        break;
    case FILES_A_ASK_DELETE:
        snprintf(out, cap, "ASK:\xC2\xBF" "Borrar definitivamente?:%s;", f->ent[o->idx].name);
        break;
    case FILES_A_NAME:
        snprintf(out, cap, "NAME:Renombrar:%s;", o->text);
        break;
    case FILES_A_TRASH_ONE:
        flex_files_path_of(f, o->idx, p, sizeof(p));
        snprintf(out, cap, "TRASH:%s;", p);
        break;
    case FILES_A_OPEN_TRASH:
        snprintf(out, cap, "TRASHOPEN;");
        break;
    case FILES_A_DELETE:
        flex_files_path_of(f, o->idx, p, sizeof(p));
        snprintf(out, cap, "DEL:%s;", p);
        break;
    default:   // ENTER, UP, TOGGLE, MULTI_EXIT: solo cambian el estado
        break;
    }
}

static int g_lx, g_ly, g_lscroll, g_ln;
static void same_state(const char *what, int it)
{
    char rdir[160];
    int rsel, rmulti, rn;
    unsigned rmask;
    ref_files_get(rdir, sizeof(rdir), &rsel, &rmulti, &rmask, &rn);
    unsigned m = 0;
    for (int i = 0; i < 32 && i < F.n; i++) m |= (unsigned)(F.mark[i] ? 1u : 0u) << i;
    if (strcmp(rdir, F.dir) != 0 || rsel != F.sel || rmulti != (int)F.multi || rmask != m) {
        fprintf(stderr, "  %s #%d (%d,%d) scroll %d n %d: estado distinto: dir [%s | %s] sel %d/%d multi %d/%d mask %x/%x\n", what, it, g_lx, g_ly, g_lscroll, g_ln, rdir, F.dir, rsel, F.sel, rmulti, F.multi, rmask, m);
    }
    CHECK(strcmp(rdir, F.dir) == 0);
    CHECK_EQ_I(rsel, F.sel);
    CHECK_EQ_I(rmulti, (int)F.multi);
    CHECK_EQ_I(rmask, m);
}

// Diferencia deliberada: una fila desplazada por debajo de la cabecera, el selector o la ruta, o por debajo del borde inferior de la lista,
// NO recoge toques (Arduino los aceptaba). Aqui se fija esa regla: ningun toque fuera de la ventana cambia nada.
static void t_outside(void)
{
    s_seed = 777;
    int tested = 0;
    for (int it = 0; it < 3000; it++) {
        int n = 8 + (int)(rnd() % 17);
        int scroll = 120 + (int)(rnd() % 300);
        load_both(rnd() % 2 ? "/" : "/Documentos", n, false, -1, rnd() % 4 == 0, rnd(), scroll);
        int y = rnd() % 2 ? 60 + (int)(rnd() % (FLEX_FILES_VP_TOP - 60)) : 736 + (int)(rnd() % 60);
        int x = 60 + (int)(rnd() % 300);
        if (F.multi && y >= FLEX_FILES_BAR_Y && y <= FLEX_FILES_BAR_Y + FLEX_FILES_BAR_H) {
            continue;
        }
        if (y >= FLEX_FILES_SEG_Y && y <= FLEX_FILES_SEG_Y + FLEX_FILES_SEG_H) {
            continue;
        }
        flex_files_t before = F;
        flex_files_out_t out;
        flex_files_tap(&F, scroll, 736, x, y, &out);
        CHECK_EQ_I(out.act, FILES_A_NONE);
        CHECK(!memcmp(F.mark, before.mark, sizeof(F.mark)) && F.sel == before.sel && F.multi == before.multi && !strcmp(F.dir, before.dir));
        tested++;
    }
    CHECK(tested > 1500);
}

static void t_taps(void)
{
    s_seed = 99;
    const char *const dirs[] = {"/", "/Documentos", "/System", "/Paint", "/a", "/Documentos/Cartas", "/a/b/c", "/Paint/x/y"};
    int ntaps = 0;
    for (int it = 0; it < 8000; it++) {
        const char *d = dirs[rnd() % 8];
        int n = (int)(rnd() % (NMAX + 1));
        bool multi = rnd() % 3 == 0;
        unsigned mask = rnd();
        int scroll = rnd() % 3 == 0 ? (int)(rnd() % 400) : 0;
        int sel = rnd() % 3 == 0 ? (int)(rnd() % (n + 1)) - 1 : -1;
        load_both(d, n, false, sel, multi, mask, scroll);
        int x = (int)(rnd() % 520) - 20, y = (int)(rnd() % 820) - 10;
        int mode = (int)(rnd() % 6);
        int base = flex_files_has_up(&F) ? 1 : 0;
        if (mode == 0 && F.n > 0) {   // en los bordes de una fila
            int r = (int)(rnd() % (unsigned)(F.n + base));
            y = flex_files_row_y(r) - scroll + (int)(rnd() % 3) * 61 - (rnd() % 2) * 2 + (int)(rnd() % 3) - 1;
        } else if (mode == 1) {   // cabecera, selector y barra
            static const int ys[] = {0, 20, 55, 56, 74, 100, 110, 111, 672, 700, 732, 733};
            static const int xs[] = {0, 55, 56, 240, 239, 241, 250, 251, 349, 380, 381, 424, 423, 425, 479};
            y = ys[rnd() % 12];
            x = xs[rnd() % 15];
        }
        char ract[600];
        // el doble de Arduino acepta toques sobre la cabecera y la ruta si una fila desplazada pasa por ahi, y debajo de la lista: aqui no
        // (la lista solo recoge toques dentro de su ventana). Se compara donde las dos aceptan: dentro de la ventana o en la cabecera.
        bool in_list = y >= FLEX_FILES_VP_TOP && y < 736;
        bool hdr_zone = y >= 0 && y < 56 && x >= 0 && x < 480 && (x < 56 || x >= 480 - 56);
        bool seg_zone = y >= FLEX_FILES_SEG_Y && y <= FLEX_FILES_SEG_Y + 36 && x >= 240 && x <= 464;
        bool bar_zone = F.multi && y >= FLEX_FILES_BAR_Y && y <= FLEX_FILES_BAR_Y + FLEX_FILES_BAR_H;
        // sin desplazamiento ninguna fila de Arduino puede estar por encima de la lista: los dos aceptan lo mismo (solo la cabecera y el selector)
        bool above_quiet = scroll == 0 && y < FLEX_FILES_VP_TOP;
        if (!in_list && !hdr_zone && !seg_zone && !bar_zone && !above_quiet) {
            continue;
        }
        g_lx = x; g_ly = y; g_lscroll = scroll; g_ln = F.n;
        ref_files_tap(x, y, ract, sizeof(ract));
        flex_files_out_t out;
        flex_files_tap(&F, scroll, 736, x, y, &out);
        char mstr[600];
        bool cloud;
        model_act_string(&F, &out, mstr, sizeof(mstr), &cloud);
        if (strcmp(ract, mstr) != 0) {
            fprintf(stderr, "  toque #%d (%d,%d) scroll %d dir %s n %d multi %d: ref [%s] mod [%s]\n", it, x, y, scroll, d, n, multi, ract, mstr);
        }
        CHECK(strcmp(ract, mstr) == 0);
        same_state("toque", it);
        ntaps++;
    }
    CHECK(ntaps > 3000);
    t_outside();
}

static void t_long_menu(void)
{
    s_seed = 4242;
    for (int it = 0; it < 6000; it++) {
        const char *d = rnd() % 2 ? "/" : "/Documentos";
        int n = 1 + (int)(rnd() % NMAX);
        int scroll = rnd() % 3 == 0 ? (int)(rnd() % 300) : 0;
        load_both(d, n, false, -1, false, 0, scroll);
        int base = flex_files_has_up(&F) ? 1 : 0;
        int r = (int)(rnd() % (unsigned)(F.n + base));
        int sy = flex_files_row_y(r) - scroll + (int)(rnd() % 68);   // incluye el hueco entre filas (62..69)
        if (sy < FLEX_FILES_VP_TOP || sy >= 736) continue;
        int sx = (int)(rnd() % 480);
        int dx = (int)(rnd() % 31) - 15, dy = (int)(rnd() % 31) - 15;
        unsigned held = rnd() % 3 == 0 ? 540 + (unsigned)(rnd() % 20) : (unsigned)(rnd() % 900);
        char ract[400];
        ref_files_hold(sx, sy, sx + dx, sy + dy, held, ract, sizeof(ract));
        flex_files_out_t out;
        bool hit = flex_files_long(&F, scroll, 736, flex_files_max_scroll(&F, 720) > 0, sx, sy, sx + dx, sy + dy, held, &out);
        char mstr[400];
        bool cloud;
        model_act_string(&F, &out, mstr, sizeof(mstr), &cloud);
        if (!hit) mstr[0] = 0;
        if (strcmp(ract, mstr) != 0) {
            fprintf(stderr, "  larga #%d (%d,%d)+(%d,%d) %u ms: ref [%s] mod [%s]\n", it, sx, sy, dx, dy, held, ract, mstr);
        }
        CHECK(strcmp(ract, mstr) == 0);
        int rsel, rm, rn;
        unsigned rk;
        char rd[160];
        ref_files_get(rd, sizeof(rd), &rsel, &rm, &rk, &rn);
        CHECK_EQ_I(rsel, F.sel);
        // las acciones del menu sobre esa seleccion
        for (int act = 0; act <= 4; act++) {
            // el estado de partida de cada accion: la misma seleccion en los dos
            ref_files_set(F.sel, F.multi, 0, scroll);
            flex_files_t save = F;
            char ra[400], ma[400];
            ref_files_menu(act, ra, sizeof(ra));
            flex_files_out_t mo;
            flex_files_menu_act(&F, act, &mo);
            model_act_string(&F, &mo, ma, sizeof(ma), &cloud);
            if (strcmp(ra, ma) != 0) {
                fprintf(stderr, "  menu %d (sel %d, n %d): ref [%s] mod [%s]\n", act, save.sel, save.n, ra, ma);
            }
            CHECK(strcmp(ra, ma) == 0);
            same_state("menu", it);
            if (act == 0 && F.multi && F.sel >= 0) {   // "Seleccionar" marca la fila; un toque sobre ella la desmarca (y otro la vuelve a marcar)
                int ry = flex_files_row_y(base + F.sel) - scroll + 10;
                for (int again = 0; again < 2 && ry >= FLEX_FILES_VP_TOP && ry < 736; again++) {
                    char rt[400], mt[400];
                    ref_files_tap(100, ry, rt, sizeof(rt));
                    flex_files_out_t to;
                    flex_files_tap(&F, scroll, 736, 100, ry, &to);
                    model_act_string(&F, &to, mt, sizeof(mt), &cloud);
                    CHECK(strcmp(rt, mt) == 0);
                    same_state("alternar", it);
                    CHECK_EQ_I(F.mark[F.sel] != 0, again == 1);
                }
            }
            F = save;
            F.ent = s_ents;
        }
    }
}

static void t_dialogs(void)
{
    s_seed = 777;
    for (int it = 0; it < 3000; it++) {
        int n = (int)(rnd() % (NMAX + 1));
        int sel = n ? (int)(rnd() % (n + 1)) - 1 : -1;
        load_both(rnd() % 2 ? "/" : "/Documentos", n, false, sel, false, 0, 0);
        int ret = (int)(rnd() % 3);   // 0 sigue abierto, 1 acepta, 2 cancela
        char ract[600], mstr[900] = "";
        flex_files_t save = F;
        if (rnd() % 2) {
            ref_files_ask(ret, ract, sizeof(ract));
            if (ret == 1) {
                flex_files_out_t o;
                flex_files_ask_done(&F, true, &o);
                bool cl;
                model_act_string(&F, &o, mstr, sizeof(mstr), &cl);
            }
        } else {
            const char *nm = "Nuevo nombre";
            ref_files_name(ret, nm, ract, sizeof(ract));
            if (ret == 1) {
                flex_files_out_t o;
                flex_files_name_done(&F, nm, &o);
                if (o.act == FILES_A_RENAME) {
                    char p[FLEX_FSX_PATH_MAX];
                    flex_files_path_of(&save, o.idx, p, sizeof(p));
                    snprintf(mstr, sizeof(mstr), "REN:%s>%s;", p, o.text);
                }
            }
        }
        if (strcmp(ract, mstr) != 0) fprintf(stderr, "  dialogo #%d (sel %d n %d ret %d): ref [%s] mod [%s]\n", it, save.sel, save.n, ret, ract, mstr);
        CHECK(strcmp(ract, mstr) == 0);
        if (ret == 1) {   // tras decidir: sin seleccion y sin carpeta cambiada
            CHECK_EQ_I(F.sel, -1);
        }
    }
}

static void t_render(void)
{
    s_seed = 31415;
    static char got[20000], exp[20000];
    for (int it = 0; it < 1500; it++) {
        const char *d = rnd() % 3 ? "/" : "/Documentos";
        int n = (int)(rnd() % (NMAX + 1));
        bool multi = rnd() % 2;
        unsigned mask = rnd();
        int scroll = rnd() % 2 ? (int)(rnd() % 600) : 0;
        load_both(d, n, false, -1, multi, mask, scroll);
        unsigned m = 0;
        for (int i = 0; i < 32 && i < F.n; i++) m |= (unsigned)(F.mark[i] ? 1u : 0u) << i;
        ref_files_render(scroll, multi, m, got, sizeof(got));
        // lo de la cabecera y el selector lo pinta el kit; se compara desde la ruta
        char *from = strstr(got, "K16,124,2,");
        CHECK(from != NULL);
        if (!from) continue;
        sb_t e = {exp, sizeof(exp), 0};
        exp[0] = 0;
        sb_add(&e, "K16,124,2,%u,420:%s;<VP>", 0xF102u, F.dir);
        int row = 0;
        flex_st_page_t pg;
        if (flex_files_has_up(&F)) {
            int y = flex_files_row_y(row++) - scroll;
            if (y > 40 && y < 800 - 60) {
                flex_files_row_ops(&F, 0, y, &pg);
                expand(&pg, &e);
            }
        }
        for (int i = 0; i < F.n; i++) {
            int y = flex_files_row_y(row) - scroll;
            int rr = row++;
            if (y + FLEX_FILES_RH < 40 || y > 800 - 50) continue;
            flex_files_row_ops(&F, rr, y, &pg);
            expand(&pg, &e);
        }
        if (F.n == 0) sb_add(&e, "M240,320,3,%u:%s;", 0xF103u, FLEX_FILES_EMPTY_TEXT);
        sb_add(&e, "</VP>");
        if (F.multi) {
            flex_files_bar_ops(&pg);
            expand(&pg, &e);
        }
        if (strcmp(from, exp) != 0) show_diff("filas", from, exp);
        CHECK(strcmp(from, exp) == 0);
    }
}

// path_of nunca desborda y cierra la cadena, con cualquier tamano de buffer; row_ops fuera de rango no dibuja nada; set_list corrige la seleccion
static void t_edges(void)
{
    flex_files_enter_at(&F, "/Documentos");
    F.ent = s_ents;
    memset(s_ents, 0, sizeof(s_ents));
    snprintf(s_ents[0].name, sizeof(s_ents[0].name), "abcdef.txt");
    snprintf(s_ents[1].name, sizeof(s_ents[1].name), "Cartas");
    s_ents[1].dir = true;
    F.n = 2;
    char p[40];
    for (size_t n = 1; n < sizeof(p); n++) {
        memset(p, 'Z', sizeof(p));
        flex_files_path_of(&F, 0, p, n);
        const char *full = "/Documentos/abcdef.txt";
        size_t want = strlen(full) < n - 1 ? strlen(full) : n - 1;
        CHECK(strlen(p) == want && !strncmp(p, full, want) && p[sizeof(p) - 1] == 'Z');   // corta, termina y no escribe de mas
    }
    flex_files_enter_at(&F, "/");
    F.n = 2;   // enter_at vacia la lista
    flex_files_path_of(&F, 1, p, sizeof(p));
    CHECK(!strcmp(p, "/Cartas"));
    flex_st_page_t pg;
    flex_files_row_ops(&F, 2, 0, &pg);   // la fila 2 no existe en la raiz (sin ".." y con dos entradas)
    CHECK_EQ_I(pg.n, 0);
    flex_files_row_ops(&F, 1, 0, &pg);
    CHECK(pg.n > 0);
    flex_files_enter_at(&F, "/Documentos");
    F.n = 2;
    flex_files_row_ops(&F, 3, 0, &pg);   // con ".." las filas llegan a 2 (0 = "..", 1 y 2 = entradas)
    CHECK_EQ_I(pg.n, 0);
    flex_files_row_ops(&F, 2, 0, &pg);
    CHECK(pg.n > 0);
    // una seleccion fuera de la lista (sel == n) no tiene fila: las acciones del menu no hacen nada con ella
    F.n = 2;
    F.sel = 2;
    flex_files_out_t eo;
    flex_files_menu_act(&F, 1, &eo);   // Eliminar
    CHECK_EQ_I(eo.act, FILES_A_NONE);
    flex_files_menu_act(&F, 2, &eo);   // Renombrar
    CHECK_EQ_I(eo.act, FILES_A_NONE);
    flex_files_menu_act(&F, 3, &eo);   // Papelera sin seleccion valida: abre la papelera, no tira nada
    CHECK_EQ_I(eo.act, FILES_A_OPEN_TRASH);
    flex_files_menu_act(&F, 4, &eo);   // Subir
    CHECK_EQ_I(eo.act, FILES_A_NONE);
    F.sel = 2;
    flex_files_ask_done(&F, true, &eo);   // confirmar el borrado de una seleccion que ya no existe: no se borra nada
    CHECK_EQ_I(eo.act, FILES_A_NONE);
    F.sel = 2;
    flex_files_name_done(&F, "nuevo", &eo);
    CHECK_EQ_I(eo.act, FILES_A_NONE);
    F.sel = 1;
    flex_files_ask_done(&F, true, &eo);   // y con una valida si
    CHECK(eo.act == FILES_A_DELETE && eo.idx == 1 && F.sel == -1);
    F.sel = 0;
    flex_files_name_done(&F, "nuevo", &eo);
    CHECK(eo.act == FILES_A_RENAME && eo.idx == 0 && !strcmp(eo.text, "nuevo") && F.sel == -1);
    // la pulsacion larga fuera de la ventana de la lista (sobre la cabecera, el selector o por debajo) no abre ningun menu
    {
        flex_files_t g = F;
        g.n = 2;
        g.sel = -1;
        for (int yy = 60; yy < 149; yy += 11) {
            CHECK(!flex_files_long(&g, 200, 736, false, 100, yy, 100, yy, 800, &eo));
        }
        CHECK(!flex_files_long(&g, 0, 736, false, 100, 740, 100, 740, 800, &eo));
        g.n = 12;   // con doce entradas y sin desplazamiento una fila empieza en y = 728 y cubre el pixel 736, que ya esta fuera de la lista
        CHECK(!flex_files_long(&g, 0, 736, false, 100, 736, 100, 736, 800, &eo));   // justo en el borde inferior: fuera
        CHECK(flex_files_long(&g, 0, 736, false, 100, 735, 100, 735, 800, &eo));    // un pixel mas arriba: dentro
        CHECK(!flex_files_long(&g, 0, 736, false, 100, 790, 100, 790, 800, &eo));
        CHECK(flex_files_long(&g, 0, 736, false, 100, flex_files_row_y(1) + 5, 100, flex_files_row_y(1) + 5, 800, &eo) && eo.idx == 1 - 1 + 1 - 0 - (flex_files_has_up(&g) ? 1 : 0));
    }
    // la seleccion que ya no existe tras recargar con menos entradas
    flex_fsx_ent_t src[3];
    memset(src, 0, sizeof(src));
    snprintf(src[0].name, sizeof(src[0].name), "a");
    snprintf(src[1].name, sizeof(src[1].name), "b");
    snprintf(src[2].name, sizeof(src[2].name), "c");
    for (int sel = 0; sel <= 3; sel++) {
        F.sel = sel;
        flex_files_set_list(&F, src, 2, 2);
        CHECK(sel < 2 ? F.sel == sel : F.sel < 0);
    }
}

void test_files(void)
{
    F.ent = s_ents;
    t_reload();
    t_taps();
    t_long_menu();
    t_dialogs();
    t_edges();
    t_render();
}
