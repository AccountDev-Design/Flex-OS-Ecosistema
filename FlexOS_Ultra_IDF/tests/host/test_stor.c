// Almacenamiento (docs/spec/03 §9): la pantalla principal (medidores, tarjetas, categorias, archivos grandes, "Todos los archivos"), las zonas
// que responden al toque y las cinco secciones de "Detalles de memoria y sistema" con sus firmas frente a FlexOS_Ultra_AppStorage.h de Arduino,
// compilado tal cual con dobles que registran lo dibujado.
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "flex_stor_model.h"
#include "flex_test.h"

void ref_stor_k(int k);
void ref_stor_ms(unsigned ms);
void ref_stor_main_in(int fs_ready, const char *err, unsigned fs_total, unsigned fs_used, unsigned ps_total, unsigned ps_free, int health,
                      const unsigned *cat, const char *const *bigp, const unsigned *bigsz, int nbig, int state, const char *name, int reach, int never,
                      int quota_valid, const char *quota);
int ref_stor_health(void);
int ref_stor_folder(int x, int y, int s, char *log, int cap);
int ref_stor_main(int w, int bot, int glass, char *log, int cap, int *zones);
int ref_stor_tick(int x, int y, char *log, int cap);
void ref_stor_sys(const FlexMemSnap *m, unsigned fw, unsigned sys, unsigned apps, unsigned docs, unsigned cache, int reason, const char *uptime, int net,
                  unsigned loop, const unsigned char *state, const unsigned char *shed, const unsigned char *has, const unsigned *bytes,
                  const char *const *names);
int ref_stor_section(int sec, int sy, char *log, int cap);
unsigned ref_stor_sig(int sec);
int ref_stor_sec_top(int i);
int ref_stor_content_h(void);
int ref_stor_max_scroll(void);
const char *ref_stor_boot(int r);
int ref_stor_det_tap(int scroll, int x, int y);

static uint32_t s_seed = 11;
static uint32_t rnd(void)
{
    s_seed = s_seed * 1103515245u + 12345u;
    return s_seed >> 8;
}

static int s_k = 6;
static int tw(const char *t, int fs, void *ctx)
{
    (void)ctx;
    return (int)strlen(t) * s_k * fs;
}

// donde empiezan a diferir dos registros (con un poco de contexto), para no tener que leer 4 KB
static void show_diff(const char *what, const char *a, const char *b)
{
    size_t i = 0;
    while (a[i] && a[i] == b[i]) i++;
    size_t s = i > 60 ? i - 60 : 0;
    fprintf(stderr, "  %s: difieren en %zu\n  ref: ...%.160s\n  mod: ...%.160s\n", what, i, a + s, b + s);
}

static unsigned tok(int role, unsigned rgb)
{
    switch (role) {
    case FSC_TXT: return 0xF101;
    case FSC_TXT2: return 0xF102;
    case FSC_MUTE: return 0xF103;
    case FSC_SURF: return 0xF105;
    case FSC_PRIM: return 0xF109;
    case FSC_ONACC: return 0xF10A;
    case FSC_OK: return 0xF110;
    case FSC_WARN: return 0xF111;
    case FSC_ERR: return 0xF112;
    case FSC_ACCS: return 0xF113;
    case FSC_TRACK: return 0xF114;
    case FSC_BORDER: return 0xF115;
    case FSC_ACCENT: return 0xF200;
    default: return rgb;
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

// la lista de operaciones expresada en el mismo registro que los dobles de Arduino; dy = desplazamiento vertical de la pagina
static void expand(const flex_st_page_t *pg, int dy, sb_t *s)
{
    for (int i = 0; i < pg->n; i++) {
        const flex_st_op_t *o = &pg->op[i];
        int y = o->y + dy;
        switch (o->kind) {
        case FST_SURF:
            if (o->role == FSC_SURF_ELEV) {
                sb_add(s, "U%d,%d,%d,%d,%d,1;", o->x, y, o->w, o->h, o->r);
            } else {
                sb_add(s, "Q%d,%d,%d,%d,%d,%u;", o->x, y, o->w, o->h, o->r, 0xF105u);
            }
            break;
        case FST_RRECT:
            sb_add(s, "Q%d,%d,%d,%d,%d,%u;", o->x, y, o->w, o->h, o->r, tok(o->role, o->rgb));
            break;
        case FST_OUTLINE:
            sb_add(s, "L%d,%d,%d,%d,%d,%u;", o->x, y, o->w, o->h, o->r, tok(o->role, o->rgb));
            break;
        case FST_DOT:
            sb_add(s, "O%d,%d,%d,%u;", o->x, y, o->r, tok(o->role, o->rgb));
            break;
        case FST_TEXT:
            if (o->max_right > 0) {
                sb_add(s, "K%d,%d,%d,%u,%d:%s;", o->x, y, o->size, tok(o->role, o->rgb), o->max_right, o->text);
            } else {
                sb_add(s, "T%d,%d,%d,%u:%s;", o->x, y, o->size, tok(o->role, o->rgb), o->text);
            }
            break;
        case FST_TEXT_R:
            sb_add(s, "N%d,%d,%d,%u:%s;", o->x, y, o->size, tok(o->role, o->rgb), o->text);
            break;
        case FST_TEXT_C:
            sb_add(s, "M%d,%d,%d,%u:%s;", o->x, y, o->size, tok(o->role, o->rgb), o->text);
            break;
        case FST_FOLDER: {
            flex_st_rect_t r[3];
            flex_st_folder_rects(o->x, y, o->w, r);
            for (int k = 0; k < 3; k++) {
                sb_add(s, "Q%d,%d,%d,%d,%d,%u;", r[k].x, r[k].y, r[k].w, r[k].h, r[k].r, (unsigned)r[k].rgb);
            }
            break;
        }
        case FST_CHEV:
            sb_add(s, "S%d,%d,%d,%d,2.0,%u;", o->x, y, o->x + 7, y + 7, tok(o->role, 0));
            sb_add(s, "S%d,%d,%d,%d,2.0,%u;", o->x + 7, y + 7, o->x, y + 14, tok(o->role, 0));
            break;
        case FST_CLOUD:
            sb_add(s, "G%d,%d,%d,%u;", o->x, y, o->r, tok(FSC_ACCENT, 0));
            break;
        default:
            break;
        }
    }
}

static void t_main(void)
{
    s_seed = 515;
    static char got[20000], exp[20000];
    // (la linea "ruta  ·  tamano" de Arduino cabe en 64 bytes: con una ruta mas larga pierde el tamano; aqui no, ver mas abajo)
    const char *names[] = {"foto grande.jpg", "v.avi", "Un nombre largo para recortar.bin", "a"};
    const char *phone_names[] = {"", "Pixel 8", "Mi telefono de la oficina con nombre largo"};
    int boxes_w[] = {480, 400, 300, 160, 100, 90, 70};
    int boxes_bot[] = {736, 790, 640, 560, 500, 460, 420, 380, 340};
    int variants = 0, fewer_cases = 0, nobig_cases = 0;
    for (int it = 0; it < 9000; it++) {
        int ready = rnd() % 12 != 0;
        unsigned ft = rnd() % 5 == 0 ? 0 : 1000000u + rnd() % 8000000u, fu = ft ? rnd() % (ft + 1) : rnd() % 4000;
        unsigned pt = rnd() % 5 == 0 ? 0 : (8u + rnd() % 25) << 20, pf = pt ? rnd() % (pt + 1) : 0;
        int health = (int)(rnd() % 3);
        unsigned cat[4];
        for (int i = 0; i < 4; i++) cat[i] = rnd() % 4 == 0 ? 0 : rnd() % 3000000u;
        int nbig = (int)(rnd() % 4);
        const char *bigp[3];
        unsigned bigsz[3];
        char pathbuf[3][100];
        for (int i = 0; i < 3; i++) {
            snprintf(pathbuf[i], sizeof(pathbuf[i]), "/Documentos/%s", names[rnd() % 4]);
            bigp[i] = pathbuf[i];
            bigsz[i] = 500 + rnd() % 3000000u;
        }
        int state = (int)(rnd() % 4);
        const char *pname = phone_names[rnd() % 3];
        int reach = (int)(rnd() % 2), never = (int)(rnd() % 2), qv = (int)(rnd() % 2);
        const char *quota = "1.2 GB libres de 5 GB";
        const char *err = rnd() % 2 ? "sin particion de datos" : "";
        ref_stor_main_in(ready, err, ft, fu, pt, pf, health, cat, bigp, bigsz, nbig, state, pname, reach, never, qv, quota);
        CHECK_EQ_I(ref_stor_health(), health);
        s_k = rnd() % 4 == 0 ? 5 : 6;
        ref_stor_k(s_k);
        flex_st_main_in_t in;
        memset(&in, 0, sizeof(in));
        in.fs_ready = ready;
        in.scanned = true;
        snprintf(in.fs_err, sizeof(in.fs_err), "%s", err);
        in.fs_total = ft;
        in.fs_used = fu;
        in.ps_total = pt;
        in.ps_free = pf;
        in.health = health;
        for (int i = 0; i < 4; i++) in.cat[i] = cat[i];
        in.nbig = nbig;
        for (int i = 0; i < nbig; i++) {
            snprintf(in.big[i].path, sizeof(in.big[i].path), "%s", bigp[i]);
            in.big[i].size = bigsz[i];
        }
        in.phone.state = state;
        snprintf(in.phone.name, sizeof(in.phone.name), "%s", pname);
        in.phone.reachable = reach;
        in.phone.never_ok = never;
        in.phone.quota_valid = state == FST_PH_READY && qv;
        snprintf(in.phone.quota, sizeof(in.phone.quota), "%s", quota);
        int w = boxes_w[rnd() % 7], bot = it % 3 == 0 ? boxes_bot[rnd() % 9] : 150 + (it * 7) % 650;   // la mitad barre TODAS las alturas   // y alturas continuas: los bordes de cada "cabe / no cabe"
        int zones[8];
        ref_stor_main(w, bot, 0, got, sizeof(got), zones);
        // Diferencia deliberada (ver flex_stor_model.c): "Ver..." tiene prioridad sobre los archivos grandes. Cuando Arduino se queda sin la
        // fila con archivos grandes, el modelo debe dar lo que da Arduino con MENOS archivos (el mayor numero que deja sitio a la fila); si ni con
        // cero cabe la fila (o no hay sitio para el titulo), Arduino y el modelo coinciden tal cual.
        bool arduino_lost_ver = ready && nbig > 0 && zones[6] == zones[7];
        int fewer_files = 0, k0_candidate = 0;
        if (arduino_lost_ver) {
            int z2[8];
            for (int k = nbig - 1; k >= 1; k--) {
                ref_stor_main_in(ready, err, ft, fu, pt, pf, health, cat, bigp, bigsz, k, state, pname, reach, never, qv, quota);
                ref_stor_k(s_k);
                ref_stor_main(w, bot, 0, got, sizeof(got), z2);
                if (z2[6] != z2[7]) {
                    fewer_files = 1;
                    memcpy(zones, z2, sizeof(zones));
                    break;
                }
            }
            if (!fewer_files) {   // ni con un archivo cabe la fila: o ni siquiera sin archivos (como Arduino) o sin titulo ni archivos
                ref_stor_main_in(ready, err, ft, fu, pt, pf, health, cat, bigp, bigsz, nbig, state, pname, reach, never, qv, quota);
                ref_stor_k(s_k);
                ref_stor_main(w, bot, 0, got, sizeof(got), zones);
                k0_candidate = 1;
            }
        }
        flex_st_page_t pg;
        flex_st_zones_t z;
        flex_st_main_build(&pg, &z, &in, 0, 96, w, bot - 96, tw, NULL);
        sb_t e = {exp, sizeof(exp), 0};
        exp[0] = 0;
        expand(&pg, 0, &e);
        if (!ready) {   // el consejo de Arduino habla del Partition Scheme del IDE: aqui la tabla es fija (mismo texto que el kit de archivos)
            char *at = strstr(got, "Elige un Partition Scheme con SPIFFS");
            CHECK(at != NULL);
            if (at) {
                static char fixed[20000];
                snprintf(fixed, sizeof(fixed), "%.*sReinstala el firmware o repara el almacenamiento en Ajustes;", (int)(at - got), got);
                strcpy(got, fixed);
            }
        }
        bool nobig = k0_candidate && z.ver_y1 > z.ver_y0;
        if (nobig) {
            // ni un archivo cabe junto a la fila: el titulo se queda (como en Arduino) sin ningun archivo, sin "No hay archivos" (los hay), y la fila SI esta
            CHECK(strstr(exp, "Archivos grandes") != NULL && strstr(exp, "No hay archivos") == NULL);
            nobig_cases++;
        } else {
            if (strcmp(got, exp) != 0) {
                char what[80];
                snprintf(what, sizeof(what), "principal w=%d bot=%d ready=%d", w, bot, ready);
                show_diff(what, got, exp);
            }
            CHECK(strcmp(got, exp) == 0);
        }
        if (fewer_files) {
            fewer_cases++;
        }
        if (ready && !nobig) {
            CHECK_EQ_I(zones[0], z.phone_y0);
            CHECK_EQ_I(zones[1], z.phone_y1);
            CHECK_EQ_I(zones[2], z.det_y0);
            CHECK_EQ_I(zones[3], z.det_y1);
            CHECK_EQ_I(zones[4], z.opt_x0);
            CHECK_EQ_I(zones[5], z.opt_x1);
            CHECK_EQ_I(zones[6], z.ver_y0);
            CHECK_EQ_I(zones[7], z.ver_y1);
            // el toque: la misma respuesta que almTick
            for (int k = 0; k < 6; k++) {
                int x = (int)(rnd() % 520) - 20, y = 90 + (int)(rnd() % 700);
                if (k < 3) {   // alrededor de las zonas
                    int ys[] = {z.phone_y0, z.det_y0, z.ver_y0, z.phone_y1, z.det_y1, z.ver_y1};
                    y = ys[rnd() % 6] + (int)(rnd() % 5) - 2;
                    x = (rnd() % 2) ? z.opt_x0 + (int)(rnd() % 5) - 2 : z.opt_x1 + (int)(rnd() % 5) - 2;
                }
                char act[64];
                ref_stor_tick(x, y, act, sizeof(act));
                int h = flex_st_main_hit(&z, x, y);
                const char *want = h == FST_HIT_PHONE ? "PHONE;" : h == FST_HIT_DETAIL ? "DETAIL;" : h == FST_HIT_OPTIMIZE ? "OPT;" : h == FST_HIT_FILES ? "FILES;" : "";
                if (strcmp(act, want) != 0) {
                    fprintf(stderr, "  toque (%d,%d): ref [%s] mod [%s]\n", x, y, act, want);
                }
                CHECK(strcmp(act, want) == 0);
            }
        }
        variants++;
    }
    CHECK(variants == 9000);
    // el icono de carpeta (pantalla principal: 34 px; filas del explorador: 38 px) frente al almFolderIcon de Arduino, para todos los tamanos
    for (int s = 4; s <= 80; s++) {
        char rl[300], ml[300] = "";
        ref_stor_folder(13, 27, s, rl, sizeof(rl));
        flex_st_rect_t r[3];
        flex_st_folder_rects(13, 27, s, r);
        for (int k = 0; k < 3; k++) {
            snprintf(ml + strlen(ml), sizeof(ml) - strlen(ml), "Q%d,%d,%d,%d,%d,%u;", r[k].x, r[k].y, r[k].w, r[k].h, r[k].r, (unsigned)r[k].rgb);
        }
        if (strcmp(rl, ml) != 0) {
            fprintf(stderr, "  carpeta s=%d: ref [%s] mod [%s]\n", s, rl, ml);
        }
        CHECK(strcmp(rl, ml) == 0);
    }
    CHECK(fewer_cases > 30 && nobig_cases > 10);   // la rama deliberada se ejercita de verdad (no un test que nunca entra)
    // una ruta larga: Arduino compone "ruta  ·  tamano" en 64 bytes y el tamano se pierde; aqui se ve siempre
    {
        flex_st_main_in_t in;
        memset(&in, 0, sizeof(in));
        in.fs_ready = true;
        in.scanned = true;
        in.fs_total = 1000000;
        in.nbig = 1;
        snprintf(in.big[0].path, sizeof(in.big[0].path), "/Documentos/Un nombre de archivo bastante largo para recortar.bin");
        in.big[0].size = 2048;
        flex_st_page_t pg;
        flex_st_zones_t z;
        flex_st_main_build(&pg, &z, &in, 0, 96, 480, 640, tw, NULL);
        int found = 0;
        for (int i = 0; i < pg.n; i++) {
            found += pg.op[i].kind == FST_TEXT && strstr(pg.op[i].text, "recortar.bin  \xC2\xB7  2 KB") != NULL;
        }
        CHECK_EQ_I(found, 1);
    }
    // mientras la medida no llega: "..." y "Calculando...", nunca una cifra inventada
    {
        flex_st_main_in_t in;
        memset(&in, 0, sizeof(in));
        in.fs_ready = true;
        in.fs_total = 1000000;
        in.cat[0] = 123456;   // un resto de otra medida: no debe verse
        in.nbig = 1;
        snprintf(in.big[0].path, sizeof(in.big[0].path), "/Documentos/a.bin");
        flex_st_page_t pg;
        flex_st_zones_t z;
        flex_st_main_build(&pg, &z, &in, 0, 96, 480, 640, tw, NULL);
        int dots = 0, calc = 0, leak = 0, fold = 0;
        for (int i = 0; i < pg.n; i++) {
            dots += pg.op[i].kind == FST_TEXT_R && !strcmp(pg.op[i].text, "...");
            calc += pg.op[i].kind == FST_TEXT && !strcmp(pg.op[i].text, "Calculando...");
            leak += strstr(pg.op[i].text, "120 KB") != NULL || strstr(pg.op[i].text, "a.bin") != NULL;
            fold += pg.op[i].kind == FST_FOLDER;
        }
        CHECK_EQ_I(dots, 4);
        CHECK_EQ_I(calc, 1);
        for (int i = 0; i < pg.n; i++) {   // "Calculando..." va bajo el titulo, a su misma sangria y en el tamano pequeno y apagado
            if (!strcmp(pg.op[i].text, "Calculando...")) {
                CHECK(pg.op[i].x == 40 && pg.op[i].size == 1 && pg.op[i].role == FSC_MUTE);
                CHECK_EQ_I(z.ver_y0, pg.op[i].y + 22 + flex_ui_gap(flex_ui_pad(480, 640)) / 2);   // ocupa 22 px y despues va la fila "Ver..."
            }
        }
        CHECK_EQ_I(leak, 0);
        CHECK_EQ_I(fold, 0);
    }
    // la lectura fallo: "--" y "No se pudo leer", sin archivos ni cifras
    {
        flex_st_main_in_t in;
        memset(&in, 0, sizeof(in));
        in.fs_ready = true;
        in.scanned = true;
        in.scan_err = true;
        in.fs_total = 1000000;
        in.cat[1] = 777;
        in.nbig = 1;
        snprintf(in.big[0].path, sizeof(in.big[0].path), "/Documentos/a.bin");
        flex_st_page_t pg;
        flex_st_zones_t z;
        flex_st_main_build(&pg, &z, &in, 0, 96, 480, 640, tw, NULL);
        int dashes = 0, err = 0, leak = 0;
        for (int i = 0; i < pg.n; i++) {
            dashes += pg.op[i].kind == FST_TEXT_R && !strcmp(pg.op[i].text, "--");
            err += pg.op[i].kind == FST_TEXT && !strcmp(pg.op[i].text, "No se pudo leer");
            leak += strstr(pg.op[i].text, "777") != NULL || strstr(pg.op[i].text, "a.bin") != NULL;
        }
        CHECK_EQ_I(dashes, 4);
        CHECK_EQ_I(err, 1);
        for (int i = 0; i < pg.n; i++) {
            if (!strcmp(pg.op[i].text, "No se pudo leer")) {
                CHECK(pg.op[i].x == 40 && pg.op[i].size == 1 && pg.op[i].role == FSC_MUTE);
            }
        }
        CHECK_EQ_I(leak, 0);
    }
    // las zonas con una pantalla pequena: solo lo que cabe se dibuja y responde (sin scroll en esta pantalla)
    s_k = 6;
    ref_stor_k(6);
    // el estado propio de esta version: sin servicio de telefono no se finge "Sin telefono" ni "actívalo desde la web"
    flex_st_phone_t p = {0};
    p.state = FST_PH_UNAVAILABLE;
    char st[96];
    int role;
    flex_st_phone_state(&p, st, sizeof(st), &role);
    CHECK(strstr(st, "No disponible") != NULL && strstr(st, "web") == NULL && role == FSC_MUTE);
}

static void rand_sys(flex_st_sys_t *s, bool ps, bool in, bool fs)
{
    memset(s, 0, sizeof(*s));
    FlexMemSnap *m = &s->m;
    if (ps) {
        m->psTotal = (8u + rnd() % 25) << 20;
        m->psFree = rnd() % (m->psTotal + 1);
        m->psLargest = rnd() % (m->psFree + 1);
        m->psPeakUsed = rnd() % (m->psTotal + 1);
    }
    if (in) {
        m->inTotal = 100000 + rnd() % 400000;
        m->inFree = rnd() % (m->inTotal + 1);
        m->inMin = rnd() % (m->inFree + 1);
        if (rnd() % 4 == 0) m->inFree = rnd() % 70000;
        if (rnd() % 5 == 0) {   // justo en los umbrales de color (SRAM baja / minima): LOW, LOW +- 1, MIN, MIN +- 1
            static const unsigned th[] = {FLEXMEM_SRAM_LOW_BYTES - 1, FLEXMEM_SRAM_LOW_BYTES, FLEXMEM_SRAM_LOW_BYTES + 1, FLEXMEM_SRAM_MIN_BYTES - 1,
                                          FLEXMEM_SRAM_MIN_BYTES, FLEXMEM_SRAM_MIN_BYTES + 1};
            m->inFree = th[rnd() % 6];
            if (m->inTotal < m->inFree) m->inTotal = m->inFree + 1000;
            if (m->inMin > m->inFree) m->inMin = m->inFree;
        }
    }
    if (fs) {
        m->fsValid = 1;
        m->fsTotal = 1000000 + rnd() % 8000000;
        m->fsUsed = rnd() % (m->fsTotal + 1);
        if (rnd() % 3 == 0) m->fsUsed = m->fsTotal / 100 * (80 + rnd() % 20);
    }
    s->fw_size = rnd() % 3 == 0 ? 0xFFFFFFFFu : (rnd() % 5 == 0 ? 0 : 500000 + rnd() % 2000000);
    s->cat_sys = rnd() % 900000;
    s->cat_apps = rnd() % 900000;
    s->cat_docs = rnd() % 900000;
    s->cache = rnd() % 90000;
    s->reset_reason = (int)(rnd() % 14) - 1;
    static const char *const ups[] = {"0m", "12m", "3h 5m", "2d 1h 4m"};
    snprintf(s->uptime, sizeof(s->uptime), "%s", ups[rnd() % 4]);
    s->uptime_min = rnd() % 5000;
    snprintf(s->version, sizeof(s->version), "1.0.0");
    s->net_online = rnd() % 2;
    s->loop_rate = rnd() % 400;
    s->napps = FLEX_ST_APPS;
    for (int i = 0; i < FLEX_ST_APPS; i++) {
        s->app_state[i] = (uint8_t)(rnd() % 3);
        s->app_shed[i] = rnd() % 2;
        s->app_mem_has[i] = rnd() % 3 != 0;
        s->app_mem[i] = rnd() % 4 == 0 ? 0 : (rnd() % 6000000u);
        snprintf(s->app_name[i], sizeof(s->app_name[i]), "App %d", i);
    }
}

static void t_detail(void)
{
    s_seed = 777;
    static char got[8000], exp[8000];
    CHECK_EQ_I(flex_st_content_h(), ref_stor_content_h());
    CHECK_EQ_I(flex_st_max_scroll(), ref_stor_max_scroll());
    for (int i = 0; i <= FLEX_ST_SEC_N; i++) CHECK_EQ_I(flex_st_sec_top(i), ref_stor_sec_top(i));
    CHECK_EQ_I(flex_st_content_h(), 940);
    CHECK_EQ_I(flex_st_max_scroll(), 940 - 640);
    for (int it = 0; it < 3000; it++) {
        flex_st_sys_t s;
        rand_sys(&s, rnd() % 6 != 0, rnd() % 6 != 0, rnd() % 6 != 0);
        const char *names[FLEX_ST_APPS];
        unsigned char st[FLEX_ST_APPS], sh[FLEX_ST_APPS], hs[FLEX_ST_APPS];
        unsigned by[FLEX_ST_APPS];
        for (int i = 0; i < FLEX_ST_APPS; i++) {
            names[i] = s.app_name[i];
            st[i] = s.app_state[i];
            sh[i] = s.app_shed[i];
            hs[i] = s.app_mem_has[i];
            by[i] = s.app_mem[i];
        }
        ref_stor_sys(&s.m, s.fw_size, s.cat_sys, s.cat_apps, s.cat_docs, s.cache, s.reset_reason, s.uptime, s.net_online, s.loop_rate, st, sh, hs, by, names);
        ref_stor_k(6);
        ref_stor_ms(s.uptime_min * 60000u + rnd() % 60000u);   // el rotulo de la firma solo cambia por minutos
        for (int sec = 0; sec < FLEX_ST_SEC_N; sec++) {
            int sy = 96 + flex_st_sec_top(sec) - (int)(rnd() % 300);
            ref_stor_section(sec, sy, got, sizeof(got));
            flex_st_page_t pg;
            flex_st_detail_section(&pg, sec, &s);
            sb_t e = {exp, sizeof(exp), 0};
            exp[0] = 0;
            expand(&pg, sy, &e);
            if (strcmp(got, exp) != 0) {
                char what[40];
                snprintf(what, sizeof(what), "seccion %d", sec);
                show_diff(what, got, exp);
            }
            CHECK(strcmp(got, exp) == 0);
            // las operaciones caben en la altura fija de su seccion (si no, el repintado parcial de la siguiente la borraria)
            for (int i = 0; i < pg.n; i++) {
                int bottom = pg.op[i].y + (pg.op[i].kind == FST_SURF || pg.op[i].kind == FST_RRECT || pg.op[i].kind == FST_OUTLINE ? pg.op[i].h : 18);
                CHECK(bottom <= flex_st_sec_h[sec]);
            }
            if (sec > 0) CHECK_EQ_I(ref_stor_sig(sec), flex_st_detail_sig(sec, &s));
        }
    }
    for (int r = -2; r < 20; r++) CHECK(!strcmp(ref_stor_boot(r), flex_st_boot_cause(r)));
    // el boton "Optimizar Flex OS" con el desplazamiento de la lista
    for (int it = 0; it < 4000; it++) {
        int scroll = (int)(rnd() % 320), x = (int)(rnd() % 520) - 20, y = 90 + (int)(rnd() % 660);
        if (it % 3 == 0) {
            y = 96 - scroll + 34 + (int)(rnd() % 50) - 3;
            x = (it % 2) ? 36 + (int)(rnd() % 5) - 2 : 444 + (int)(rnd() % 5) - 2;
        }
        int want = ref_stor_det_tap(scroll > flex_st_max_scroll() ? flex_st_max_scroll() : scroll, x, y);
        int sc = scroll > flex_st_max_scroll() ? flex_st_max_scroll() : scroll;
        // el toque solo cuenta dentro de la ventana de contenido (96..735)
        bool got_hit = flex_st_opt_hit(sc, x, y) && y >= FLEX_ST_DET_VY0 && y <= FLEX_ST_DET_VY1;
        if (want != (int)got_hit) {
            fprintf(stderr, "  boton optimizar scroll=%d (%d,%d): ref %d mod %d\n", sc, x, y, want, got_hit);
        }
        CHECK_EQ_I(want, got_hit);
    }
}

void test_stor(void)
{
    ref_stor_k(6);
    t_main();
    t_detail();
}
