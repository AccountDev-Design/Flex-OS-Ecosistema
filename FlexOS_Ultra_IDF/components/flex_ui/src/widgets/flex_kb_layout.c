// Flex OS Ultra · teclado del sistema: logica pura (ver flex_kb_layout.h).
//
// Cada funcion cita la de Arduino que reproduce (FlexOS_Ultra_Keyboard.h salvo
// que se diga otra cosa). Las pruebas de host las comparan bit a bit con el
// codigo Arduino extraido: si se cambia algo aqui, cambia tambien alli o la
// prueba falla.
#include "flex_kb_layout.h"

#include <string.h>
#include "flex_storage.h"

// ---- Mapas (Keyboard.h:44-59). La n con tilde es "\xC3\xB1". ----------------
const char *const flex_kb_maps[4][FLEX_KB_ROWS][FLEX_KB_COLS] = {
    {   // LAYOUT_ES
        {"q", "w", "e", "r", "t", "y", "u", "i", "o", "p"},
        {"a", "s", "d", "f", "g", "h", "j", "k", "l", "\xC3\xB1"},
        {"z", "x", "c", "v", "b", "n", "m", ",", ".", "?"},
    },
    {   // LAYOUT_EN
        {"q", "w", "e", "r", "t", "y", "u", "i", "o", "p"},
        {"a", "s", "d", "f", "g", "h", "j", "k", "l", ";"},
        {"z", "x", "c", "v", "b", "n", "m", ",", ".", "?"},
    },
    {   // LAYOUT_NUM
        {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0"},
        {"@", "#", "$", "%", "&", "-", "_", "(", ")", "/"},
        {"*", "\"", "'", ":", ";", "!", "?", "+", "=", "."},
    },
    {   // LAYOUT_EMOJI: emoticonos de TEXTO (ASCII), los dibuja la fuente Outfit
        {":)", ":D", ":(", ";)", ":P", "xD", ":o", ":|", "<3", ":3"},
        {"^^", "o_o", ">:(", ":'(", "B)", "-_-", "=)", "D:", ":v", ":c"},
        {"uwu", ":*", "<_<", ">_>", "(y)", "!!", ":]", "[:", "T_T", "o/"},
    },
};

// ---- Estado -----------------------------------------------------------------
static int letters(const flex_kb_state_t *st)
{
    return st->lang_es ? FLEX_KB_LAYOUT_ES : FLEX_KB_LAYOUT_EN;
}

void flex_kb_state_init(flex_kb_state_t *st, bool lang_es)
{
    st->lang_es = lang_es;
    st->shift = false;
    st->layout = (uint8_t)letters(st);
}

const char *flex_kb_key_base(const flex_kb_state_t *st, int cell)
{
    if (!st || cell < 0 || cell >= FLEX_KB_CELLS || st->layout > FLEX_KB_LAYOUT_EMOJI) {
        return "";
    }
    return flex_kb_maps[st->layout][cell / FLEX_KB_COLS][cell % FLEX_KB_COLS];
}

// kbResolveKey (Keyboard.h:69)
const char *flex_kb_resolve(flex_kb_state_t *st, const char *base, char out[6], bool consume_shift)
{
    if (!base || !base[0] || !st->shift) {
        return base;
    }
    bool changed = false;
    if (base[1] == 0 && base[0] >= 'a' && base[0] <= 'z') {
        out[0] = (char)(base[0] - 32);
        out[1] = 0;
        changed = true;
    } else if (!strcmp(base, "\xC3\xB1")) {
        out[0] = (char)0xC3;
        out[1] = (char)0x91;
        out[2] = 0;
        changed = true;
    } else if (st->layout == FLEX_KB_LAYOUT_NUM && base[1] == 0 && (base[0] == '(' || base[0] == ')')) {
        out[0] = base[0] == '(' ? '{' : '}';
        out[1] = 0;
        changed = true;
    }
    if (!changed) {
        return base;
    }
    if (consume_shift) {
        st->shift = false;
    }
    return out;
}

// kbLayerLabel (Keyboard.h:1036)
const char *flex_kb_layer_label(const flex_kb_state_t *st)
{
    return st->layout == FLEX_KB_LAYOUT_NUM ? "emoji" : st->layout == FLEX_KB_LAYOUT_EMOJI ? "ABC" : "?123";
}

const char *flex_kb_lang_label(const flex_kb_state_t *st)
{
    return st->lang_es ? "ES" : "EN";
}

// noteFuncKey (Keyboard.h:1221) y lsuTick (Power.h:559-565), parte de estado
int flex_kb_fn_apply(flex_kb_state_t *st, int i)
{
    switch (i) {
    case FLEX_KB_FN_SHIFT:
        st->shift = !st->shift;
        break;
    case FLEX_KB_FN_LAYER:
        if (st->layout == FLEX_KB_LAYOUT_NUM) {
            st->layout = FLEX_KB_LAYOUT_EMOJI;
        } else if (st->layout == FLEX_KB_LAYOUT_EMOJI) {
            st->layout = (uint8_t)letters(st);
        } else {
            st->layout = FLEX_KB_LAYOUT_NUM;
        }
        break;
    case FLEX_KB_FN_LANG:
        st->lang_es = !st->lang_es;
        if (st->layout == FLEX_KB_LAYOUT_ES || st->layout == FLEX_KB_LAYOUT_EN) {
            st->layout = (uint8_t)letters(st);
        }
        break;
    case FLEX_KB_FN_SPACE:
    case FLEX_KB_FN_BACKSPACE:
    case FLEX_KB_FN_ENTER:
        break;
    default:
        return -1;
    }
    return i;
}

void flex_kb_set_layout_state(flex_kb_state_t *st, int layout)
{
    if (layout < FLEX_KB_LAYOUT_ES || layout > FLEX_KB_LAYOUT_EMOJI) {
        layout = FLEX_KB_LAYOUT_ES;
    }
    st->layout = (uint8_t)layout;
    if (layout == FLEX_KB_LAYOUT_ES || layout == FLEX_KB_LAYOUT_EN) {
        st->lang_es = layout == FLEX_KB_LAYOUT_ES;
    }
}

// ---- Geometria (Keyboard.h:118-215) -----------------------------------------
// Pesos de la fila de funciones: shift, capa, idioma, espacio, borrar, enter.
static const float KB_FW[FLEX_KB_FKEYS] = {0.135f, 0.125f, 0.110f, 0.420f, 0.100f, 0.110f};

void flex_kb_geom_init(flex_kb_geom_t *g, int size, int bot_reserve, int top_h)
{
    // kbApplySize (KB_SIZE_CONFIG_ON = 1)
    if (size == FLEX_KB_SIZE_COMPACT) {
        g->kw = 43;
        g->kh = 50;
        g->gap = 4;
    } else if (size == FLEX_KB_SIZE_BIG) {
        g->kw = 45;
        g->kh = 72;
        g->gap = 2;
    } else {
        g->kw = 45;
        g->kh = 60;
        g->gap = 2;
    }
    int gw = FLEX_KB_COLS * g->kw + (FLEX_KB_COLS - 1) * g->gap;
    int x = (FLEX_KB_SCR_W - gw) / 2;
    g->x = (int16_t)(x < 2 ? 2 : x);
    g->bot_reserve = (int16_t)(bot_reserve < 0 ? 0 : bot_reserve);
    g->top_h = (int16_t)(top_h < 0 ? 0 : top_h);
}

int flex_kb_grid_w(const flex_kb_geom_t *g)
{
    return FLEX_KB_COLS * g->kw + (FLEX_KB_COLS - 1) * g->gap;
}

int flex_kb_rows_top(const flex_kb_geom_t *g)
{
    return FLEX_KB_SCR_H - g->bot_reserve - 4 * (g->kh + g->gap) - 6;
}

int flex_kb_panel_top(const flex_kb_geom_t *g)
{
    return flex_kb_rows_top(g) - 4 - g->top_h;
}

int flex_kb_toolbar_y(const flex_kb_geom_t *g)
{
    return flex_kb_panel_top(g) + 4;
}

int flex_kb_chips_y(const flex_kb_geom_t *g, int toolbar_h)
{
    return flex_kb_toolbar_y(g) + toolbar_h;
}

int flex_kb_func_y(const flex_kb_geom_t *g)
{
    return flex_kb_rows_top(g) + 3 * (g->kh + g->gap);
}

int flex_kb_fkey_w(const flex_kb_geom_t *g, int i)
{
    if (i < 0 || i >= FLEX_KB_FKEYS) {
        return 0;
    }
    int usable = flex_kb_grid_w(g) - (FLEX_KB_FKEYS - 1) * g->gap;
    // Misma expresion en float que Arduino. Ningun producto de los tres tamanos
    // cae cerca de .5 (lo comprueba la prueba de host), asi que una posible
    // contraccion a FMA del compilador no cambia el resultado.
    int w = (int)((float)usable * KB_FW[i] + 0.5f);
    return w < 20 ? 20 : w;
}

int flex_kb_fkey_x(const flex_kb_geom_t *g, int i)
{
    int x = g->x;
    for (int k = 0; k < i; k++) {
        x += flex_kb_fkey_w(g, k) + g->gap;
    }
    return x;
}

// kbCellAt (Keyboard.h:281)
int flex_kb_cell_at(const flex_kb_geom_t *g, int px, int py)
{
    int pitch_x = g->kw + g->gap, pitch_y = g->kh + g->gap;
    int dx = px - g->x, dy = py - flex_kb_rows_top(g);
    if (dx < 0 || dy < 0) {
        return -1;
    }
    int c = dx / pitch_x, r = dy / pitch_y;
    if (c >= FLEX_KB_COLS || r >= FLEX_KB_ROWS) {
        return -1;
    }
    return r * FLEX_KB_COLS + c;
}

// kbFRowHit (Keyboard.h:197)
int flex_kb_frow_hit(const flex_kb_geom_t *g, int px, int py)
{
    int fy = flex_kb_func_y(g);
    if (py < fy - g->gap || py > fy + g->kh + g->gap) {
        return -1;
    }
    if (px < g->x - g->gap) {
        return -1;
    }
    for (int i = 0; i < FLEX_KB_FKEYS; i++) {
        int x = flex_kb_fkey_x(g, i);
        if (px <= x + flex_kb_fkey_w(g, i) + g->gap) {
            return i;
        }
    }
    return -1;
}

void flex_kb_cell_xy(const flex_kb_geom_t *g, int cell, int *x, int *y)
{
    int r = cell / FLEX_KB_COLS, c = cell % FLEX_KB_COLS;
    *x = g->x + c * (g->kw + g->gap);
    *y = flex_kb_rows_top(g) + r * (g->kh + g->gap);
}

// kbSizeCheck (Keyboard.h:209)
bool flex_kb_size_check(const flex_kb_geom_t *g)
{
    int gw = flex_kb_grid_w(g);
    int fw = flex_kb_fkey_x(g, FLEX_KB_FKEYS - 1) + flex_kb_fkey_w(g, FLEX_KB_FKEYS - 1);
    int bot = flex_kb_func_y(g) + g->kh;
    return g->x >= 0 && g->x + gw <= FLEX_KB_SCR_W && fw <= FLEX_KB_SCR_W &&
           bot <= FLEX_KB_SCR_H - g->bot_reserve - 4 && flex_kb_panel_top(g) > 120;
}

// kbToolbarH / kbChipsWant / kbChipsH (Keyboard.h:141-151)
int flex_kb_toolbar_h(bool extras, bool toolbar_pref)
{
    return (extras && toolbar_pref) ? 56 : 0;
}

int flex_kb_chips_h(bool extras, int layout, bool predict_pref)
{
    if (!extras) {
        return 0;
    }
    if (layout == FLEX_KB_LAYOUT_NUM) {
        return 32;   // simbolos personalizados (KB_SETTINGS_ON)
    }
    return predict_pref ? 32 : 0;
}

int flex_kb_slide_off(int kbh, uint32_t elapsed_ms)
{
    float p = (float)elapsed_ms / (float)FLEX_KB_SLIDE_MS;
    if (p >= 1.0f) {
        return 0;
    }
    return (int)((1.0f - p) * (float)kbh);
}

bool flex_kb_is_tap(int dx, int dy, uint32_t dur_ms)
{
    return dx > -16 && dx < 16 && dy > -16 && dy < 16 && dur_ms < 550;
}

// ---- Preferencias (Prefs.h:157-241, KeyboardSettings.h:395) -----------------
void flex_kb_shortcuts_defaults(flex_kb_prefs_t *p)
{
    memset(p->sc_abr, 0, sizeof(p->sc_abr));
    memset(p->sc_exp, 0, sizeof(p->sc_exp));
    strcpy(p->sc_abr[0], "xq");
    strcpy(p->sc_exp[0], "porque");
    strcpy(p->sc_abr[1], "q");
    strcpy(p->sc_exp[1], "que");
    strcpy(p->sc_abr[2], "tb");
    strcpy(p->sc_exp[2], "tambi\xC3\xA9n");
    strcpy(p->sc_abr[3], "pf");
    strcpy(p->sc_exp[3], "por favor");
}

void flex_kb_prefs_defaults(flex_kb_prefs_t *p)
{
    p->size = FLEX_KB_SIZE_NORMAL;
    p->fast = true;
    p->toolbar = true;
    p->predict = true;
    p->spell = false;
    p->emoji_sug = false;
    p->hicon = false;
    p->opacity = 100;
    p->style = 0;
    p->font = 1;
    p->lp_ms = 500;
    p->fx_ms = 100;
    for (int i = 0; i < FLEX_KB_SYMS; i++) {
        p->sym[i] = i;
    }
    flex_kb_shortcuts_defaults(p);
}

void flex_kb_prefs_normalize(flex_kb_prefs_t *p)
{
    if (p->size < 0 || p->size > 2) {
        p->size = FLEX_KB_SIZE_NORMAL;
    }
    if (p->style < 0 || p->style > 2) {
        p->style = 0;
    }
    if (p->font < 0 || p->font > 2) {
        p->font = 1;
    }
    if (p->opacity < 40) {
        p->opacity = 40;
    }
    if (p->opacity > 100) {
        p->opacity = 100;
    }
    if (p->lp_ms != 350 && p->lp_ms != 500 && p->lp_ms != 700) {
        p->lp_ms = 500;
    }
    if (p->fx_ms != 60 && p->fx_ms != 100 && p->fx_ms != 160) {
        p->fx_ms = 100;
    }
    for (int i = 0; i < FLEX_KB_SYMS; i++) {
        if (p->sym[i] < 0 || p->sym[i] > 15) {
            p->sym[i] = i;
        }
    }
    for (int i = 0; i < FLEX_KB_SC_MAX; i++) {
        p->sc_abr[i][FLEX_KB_SC_ABR - 1] = 0;
        p->sc_exp[i][FLEX_KB_SC_EXP - 1] = 0;
    }
}

// kbPrefsLoad (Prefs.h:204): mismas claves, tipos y valores por defecto.
void flex_kb_prefs_load(flex_kb_prefs_t *p)
{
    flex_kb_prefs_defaults(p);
    p->size = flex_cfg_get_i32("kbsize", FLEX_KB_SIZE_NORMAL);
    p->fast = flex_cfg_get_bool("kbfast", true);
    p->toolbar = flex_cfg_get_bool("kbtool", true);
    p->predict = flex_cfg_get_bool("kbpred", true);
    p->spell = flex_cfg_get_bool("kbspell", false);
    p->emoji_sug = flex_cfg_get_bool("kbemoji", false);
    p->hicon = flex_cfg_get_bool("kbhicon", false);
    p->opacity = flex_cfg_get_i32("kbopa", 100);
    p->style = flex_cfg_get_i32("kbstyle", 0);
    p->font = flex_cfg_get_i32("kbfont", 1);
    p->lp_ms = flex_cfg_get_i32("kblp", 500);
    p->fx_ms = flex_cfg_get_i32("kbfx", 100);
    uint8_t sb[FLEX_KB_SYMS];
    if (flex_cfg_get_blob("kbsyms", sb, sizeof(sb)) == FLEX_KB_SYMS) {
        for (int i = 0; i < FLEX_KB_SYMS; i++) {
            p->sym[i] = sb[i];
        }
    }
    size_t na = flex_cfg_get_blob("kbscabr", p->sc_abr, sizeof(p->sc_abr));
    size_t ne = flex_cfg_get_blob("kbscexp", p->sc_exp, sizeof(p->sc_exp));
    if (na != sizeof(p->sc_abr) || ne != sizeof(p->sc_exp)) {
        flex_kb_shortcuts_defaults(p);
    }
    flex_kb_prefs_normalize(p);
}

// kbFontSize / kbFontDy / kbRadius (Keyboard.h:238-241)
int flex_kb_font_size(const flex_kb_prefs_t *p)
{
    return p->font == 0 ? 1 : p->font == 2 ? 3 : 2;
}

int flex_kb_font_dy(const flex_kb_prefs_t *p)
{
    return p->font == 0 ? 4 : p->font == 2 ? 12 : 8;
}

int flex_kb_radius(const flex_kb_prefs_t *p)
{
    return p->style == 1 ? 0 : 6;
}

// KB_SYM_POOL / kbSymAt (Keyboard.h:294)
const char *const flex_kb_sym_pool[FLEX_KB_SYM_POOL_N] = {
    "@", "#", "$", "%", "&", "*", "+", "=", "/", "\\", "(", ")", "[", "]", "<", ">"};

const char *flex_kb_sym_at(const flex_kb_prefs_t *p, int i)
{
    if (i < 0 || i >= FLEX_KB_SYMS) {
        return "";
    }
    int k = p->sym[i];
    if (k < 0 || k >= FLEX_KB_SYM_POOL_N) {
        k = 0;
    }
    return flex_kb_sym_pool[k];
}

// ---- Destello (Keyboard.h:406-449) ------------------------------------------
void flex_kb_fx_reset(flex_kb_fx_t *fx)
{
    fx->cell = -1;
    fx->t0 = 0;
}

void flex_kb_fx_start(flex_kb_fx_t *fx, int cell, uint32_t now)
{
    if (cell < 0) {
        return;
    }
    fx->cell = cell;
    fx->t0 = now;
}

int flex_kb_fx_level(const flex_kb_fx_t *fx, int cell, uint32_t now, int fx_ms)
{
    if (fx->cell < 0 || cell != fx->cell || fx_ms <= 0) {
        return 0;
    }
    uint32_t dt = now - fx->t0;
    if ((int)dt >= fx_ms) {
        return 0;
    }
    return 255 - (int)(dt * 255 / (uint32_t)fx_ms);
}

int flex_kb_fx_tick(flex_kb_fx_t *fx, uint32_t now, int fx_ms)
{
    if (fx->cell < 0) {
        return -1;
    }
    if ((int)(now - fx->t0) < fx_ms) {
        return -1;
    }
    int cell = fx->cell;
    fx->cell = -1;
    return cell;
}

// ---- UTF-8 ------------------------------------------------------------------
// utf8Prev (Keyboard.h:764)
int flex_kb_utf8_prev(const char *s, int i)
{
    if (i <= 0) {
        return 0;
    }
    i--;
    while (i > 0 && ((unsigned char)s[i] & 0xC0) == 0x80) {
        i--;
    }
    return i;
}

// utf8Count (Power.h:72)
int flex_kb_utf8_count(const char *s)
{
    int n = 0;
    for (; *s; s++) {
        if (((unsigned char)*s & 0xC0) != 0x80) {
            n++;
        }
    }
    return n;
}

// lsuPassAppend (Power.h:73) / kbsAppendField (KeyboardSettings.h:126)
bool flex_kb_buf_append(char *buf, size_t cap, const char *s)
{
    size_t L = strlen(buf), sl = strlen(s);
    if (cap < 1 || L + sl >= cap - 1) {
        return false;
    }
    memcpy(buf + L, s, sl);
    buf[L + sl] = 0;
    return true;
}

// Tecla "<-" de la clave (Power.h:564) y del editor de atajos (KeyboardSettings.h:133)
bool flex_kb_buf_backspace(char *buf)
{
    int L = (int)strlen(buf);
    if (L <= 0) {
        return false;
    }
    int q = L - 1;
    while (q > 0 && ((unsigned char)buf[q] & 0xC0) == 0x80) {
        q--;
    }
    buf[q] = 0;
    return true;
}

// ---- Acentos (Keyboard.h:828-842) -------------------------------------------
int flex_kb_variants(char b, const char *var[4])
{
    switch (b) {
    case 'a':
        var[0] = "\xC3\xA1";
        var[1] = "\xC3\xA0";
        var[2] = "\xC3\xA2";
        var[3] = "\xC3\xA3";
        return 4;
    case 'e':
        var[0] = "\xC3\xA9";
        var[1] = "\xC3\xA8";
        var[2] = "\xC3\xAA";
        return 3;
    case 'i':
        var[0] = "\xC3\xAD";
        var[1] = "\xC3\xAC";
        var[2] = "\xC3\xAE";
        return 3;
    case 'o':
        var[0] = "\xC3\xB3";
        var[1] = "\xC3\xB2";
        var[2] = "\xC3\xB4";
        var[3] = "\xC3\xB5";
        return 4;
    case 'u':
        var[0] = "\xC3\xBA";
        var[1] = "\xC3\xB9";
        var[2] = "\xC3\xBB";
        var[3] = "\xC3\xBC";
        return 4;
    }
    return 0;
}

bool flex_kb_is_vowel_cell(const flex_kb_state_t *st, int cell)
{
    if (cell < 0 || cell >= FLEX_KB_CELLS || !(st->layout == FLEX_KB_LAYOUT_ES || st->layout == FLEX_KB_LAYOUT_EN)) {
        return false;
    }
    const char *k = flex_kb_maps[st->layout][cell / FLEX_KB_COLS][cell % FLEX_KB_COLS];
    return k[1] == 0 && (k[0] == 'a' || k[0] == 'e' || k[0] == 'i' || k[0] == 'o' || k[0] == 'u');
}

// ---- Autocompletado (Keyboard.h:543-707) ------------------------------------
// Diccionarios: copia literal de KB_DICT_ES / KB_DICT_EN (~250 palabras por idioma).
// clang-format off
static const char *const k_dict_es[] = {
  "a","abajo","abrir","acaso","aceptar","acuerdo","adelante","adem\xC3\xA1s","agua","ahora",
  "algo","alguien","alguno","alli","alto","amigo","amor","antes","a\xC3\xB1o","apagar",
  "aplicacion","aprender","aqui","archivo","arriba","asi","ayer","ayuda","bajar","bastante",
  "bien","borrar","brillo","buenas","bueno","buscar","caja","calle","cambiar","camino",
  "cargar","casa","caso","celular","cerca","cerrar","cielo","ciudad","claro","codigo",
  "color","comenzar","comida","como","completo","compartir","comprar","con","conectar","conocer",
  "contacto","contra","copiar","correo","cosa","crear","cuando","cuenta","dar","datos",
  "deber","decir","dejar","delante","dentro","desde","despu\xC3\xA9s","dia","dinero","dispositivo",
  "donde","dormir","durante","el","ella","empezar","encender","encontrar","entender","entonces",
  "entrar","enviar","error","escribir","escuchar","espacio","esperar","est\xC3\xA1","este","estar",
  "falta","familia","favor","fecha","final","forma","foto","fuera","fuerte","funcionar",
  "gente","grande","gracias","guardar","gustar","haber","hablar","hacer","hasta","hecho",
  "hola","hombre","hora","hoy","idea","idioma","imagen","importante","informacion","instalar",
  "internet","ir","juego","jugar","junto","lado","largo","leer","lento","letra",
  "libro","limpiar","llamar","llegar","llevar","luego","lugar","luz","madre","mal",
  "mandar","manera","ma\xC3\xB1" "ana","mano","mas","mayor","mejor","memoria","menos","mensaje",
  "mes","mientras","minuto","mirar","mismo","modo","momento","mostrar","mover","mucho",
  "mujer","mundo","musica","muy","nada","necesitar","ni\xC3\xB1" "o","noche","nombre","normal",
  "nosotros","noticia","nuevo","numero","nunca","ocurrir","oir","opcion","orden","otro",
  "padre","pagina","palabra","pantalla","papel","para","parecer","parte","pasar","pedir",
  "pelicula","pensar","peque\xC3\xB1o","perder","pero","persona","poco","poder","poner","porque",
  "posible","primero","probar","problema","pronto","propio","punto","quedar","querer","qui\xC3\xA9n",
  "quitar","rapido","razon","recibir","reiniciar","respuesta","resultado","saber","salir","seguir",
  "segundo","seguro","seleccionar","semana","sentir","se\xC3\xB1" "al","ser","servicio","siempre","siguiente",
  "silencio","sistema","sitio","sobre","solo","sonido","tama\xC3\xB1o","tambi\xC3\xA9n","tarde","teclado",
  "telefono","tema","tener","texto","tiempo","tipo","tocar","todo","tomar","trabajo",
  "traer","tratar","ultimo","usar","usuario","valor","venir","ventana","ver","verdad",
  "vez","viaje","vida","volver","voz","ya","zona" };
static const char *const k_dict_en[] = {
  "about","above","accept","account","add","after","again","against","all","allow",
  "almost","also","always","and","another","answer","any","app","apply","are",
  "around","ask","away","back","battery","because","become","been","before","begin",
  "behind","being","believe","below","best","better","between","big","bit","book",
  "both","bring","build","button","buy","call","camera","can","cancel","car",
  "care","carry","case","change","check","child","choose","city","clean","clear",
  "click","close","code","cold","color","come","company","computer","connect","contact",
  "continue","copy","could","country","create","cut","dark","data","day","delete",
  "device","different","display","does","done","door","down","download","draw","drive",
  "during","each","early","easy","edit","email","end","enough","enter","error",
  "even","ever","every","example","exit","face","fact","fail","family","far",
  "fast","feel","field","file","fill","find","fine","first","folder","follow",
  "font","food","for","force","form","free","friend","from","full","game",
  "get","give","good","great","group","hand","happen","happy","hard","have",
  "head","hear","help","here","high","hold","home","hope","hour","house",
  "how","idea","image","import","inside","install","just","keep","key","keyboard",
  "kind","know","language","large","last","late","learn","leave","left","less",
  "let","letter","level","life","light","like","line","list","little","live",
  "load","local","lock","long","look","love","made","make","many","mark",
  "may","mean","memory","menu","message","might","mind","minute","miss","mode",
  "money","month","more","morning","most","move","much","music","must","name",
  "near","need","network","never","new","news","next","nice","night","none",
  "note","nothing","now","number","off","offer","often","once","only","open",
  "option","order","other","over","page","paper","part","password","people","phone",
  "photo","pick","place","play","please","point","power","press","print","question",
  "quick","quit","read","ready","real","reason","record","remove","repeat","reply",
  "report","reset","rest","result","return","right","room","run","same","save",
  "say","screen","search","second","see","select","send","server","service","set",
  "settings","share","short","should","show","side","sign","since","size","small",
  "some","soon","sound","space","speak","start","state","stay","step","still",
  "stop","store","story","study","such","support","sure","system","table","take",
  "talk","tell","test","text","than","thank","that","their","them","then",
  "there","these","they","thing","think","this","time","today","together","too",
  "tool","touch","try","turn","type","under","until","update","upload","use",
  "user","very","view","wait","walk","want","watch","water","way","week",
  "well","what","when","where","which","while","white","who","why","will",
  "window","with","word","work","world","would","write","year","yes","your" };
// clang-format on

#define DICT_ES_N ((int)(sizeof(k_dict_es) / sizeof(k_dict_es[0])))
#define DICT_EN_N ((int)(sizeof(k_dict_en) / sizeof(k_dict_en[0])))

int flex_kb_dict_n(bool lang_es)
{
    return lang_es ? DICT_ES_N : DICT_EN_N;
}

// kbFoldCh: siguiente caracter en minuscula y sin tilde.
char flex_kb_fold_ch(const char **ps)
{
    const char *s = *ps;
    unsigned char c = (unsigned char)s[0];
    if (c == 0) {
        return 0;
    }
    if (c < 0x80) {
        *ps = s + 1;
        return (char)((c >= 'A' && c <= 'Z') ? c + 32 : c);
    }
    if (c == 0xC3 && s[1]) {
        unsigned char d = (unsigned char)s[1];
        *ps = s + 2;
        if ((d >= 0x80 && d <= 0x85) || (d >= 0xA0 && d <= 0xA5)) {
            return 'a';
        }
        if ((d >= 0x88 && d <= 0x8B) || (d >= 0xA8 && d <= 0xAB)) {
            return 'e';
        }
        if ((d >= 0x8C && d <= 0x8F) || (d >= 0xAC && d <= 0xAF)) {
            return 'i';
        }
        if ((d >= 0x92 && d <= 0x96) || (d >= 0xB2 && d <= 0xB6)) {
            return 'o';
        }
        if ((d >= 0x99 && d <= 0x9C) || (d >= 0xB9 && d <= 0xBC)) {
            return 'u';
        }
        if (d == 0x91 || d == 0xB1) {
            return 'n';
        }
        return '?';
    }
    s++;
    while (((unsigned char)*s & 0xC0) == 0x80) {
        s++;
    }
    *ps = s;
    return '?';
}

bool flex_kb_starts_with(const char *word, const char *pref)
{
    const char *w = word, *p = pref;
    for (;;) {
        const char *pp = p;
        char pc = flex_kb_fold_ch(&pp);
        if (pc == 0) {
            return true;
        }
        const char *ww = w;
        char wc = flex_kb_fold_ch(&ww);
        if (wc == 0 || wc != pc) {
            return false;
        }
        p = pp;
        w = ww;
    }
}

bool flex_kb_same_word(const char *a, const char *b)
{
    const char *x = a, *y = b;
    for (;;) {
        const char *xx = x;
        char ac = flex_kb_fold_ch(&xx);
        const char *yy = y;
        char bc = flex_kb_fold_ch(&yy);
        if (ac != bc) {
            return false;
        }
        if (ac == 0) {
            return true;
        }
        x = xx;
        y = yy;
    }
}

bool flex_kb_dict_has(const flex_kb_prefs_t *p, bool lang_es, const char *w)
{
    if (!w || !w[0]) {
        return true;
    }
    const char *const *d = lang_es ? k_dict_es : k_dict_en;
    int n = flex_kb_dict_n(lang_es);
    for (int i = 0; i < n; i++) {
        if (flex_kb_same_word(d[i], w)) {
            return true;
        }
    }
    for (int i = 0; i < FLEX_KB_SC_MAX; i++) {
        if (p->sc_abr[i][0] && flex_kb_same_word(p->sc_abr[i], w)) {
            return true;
        }
    }
    return false;
}

// Emoticonos sugeridos: disparador -> emoticono de LAYOUT_EMOJI.
#define KB_EMOSUG_N 10
static const char *const k_emosug_w[KB_EMOSUG_N] = {"risa", "amor", "triste", "guino", "abrazo",
                                                    "laugh", "love", "sad", "wink", "hug"};
static const char *const k_emosug_e[KB_EMOSUG_N] = {":D", "<3", ":(", ";)", "(y)", ":D", "<3", ":(", ";)", "(y)"};

// kbSuggest (Keyboard.h:675)
int flex_kb_suggest(const flex_kb_prefs_t *p, bool lang_es, const char *pref, const char **out, int maxn)
{
    int n = 0;
    if (!p->predict || !pref || !pref[0] || maxn <= 0) {
        return 0;
    }
    for (int i = 0; i < FLEX_KB_SC_MAX && n < maxn; i++) {
        if (p->sc_abr[i][0] && p->sc_exp[i][0] && flex_kb_same_word(p->sc_abr[i], pref)) {
            out[n++] = p->sc_exp[i];
        }
    }
    const char *const *d = lang_es ? k_dict_es : k_dict_en;
    int dn = flex_kb_dict_n(lang_es);
    for (int i = 0; i < dn && n < maxn; i++) {
        if (!flex_kb_starts_with(d[i], pref)) {
            continue;
        }
        bool dup = false;
        for (int k = 0; k < n; k++) {
            if (flex_kb_same_word(out[k], d[i])) {
                dup = true;
            }
        }
        if (!dup) {
            out[n++] = d[i];
        }
    }
    if (p->emoji_sug && n < maxn) {
        for (int i = 0; i < KB_EMOSUG_N && n < maxn; i++) {
            if (flex_kb_starts_with(k_emosug_w[i], pref)) {
                out[n++] = k_emosug_e[i];
                break;
            }
        }
    }
    return n;
}

// kbCurrentWord (Keyboard.h:695)
int flex_kb_current_word(const char *buf, int cur, char *out, int outsz)
{
    out[0] = 0;
    if (!buf || cur <= 0 || outsz <= 1) {
        return 0;
    }
    int a = cur;
    while (a > 0) {
        unsigned char c = (unsigned char)buf[a - 1];
        if (c == ' ' || c == '\n' || c == '\t') {
            break;
        }
        a--;
    }
    int n = cur - a;
    if (n > outsz - 1) {
        n = outsz - 1;
    }
    memcpy(out, buf + a, (size_t)n);
    out[n] = 0;
    return n;
}
