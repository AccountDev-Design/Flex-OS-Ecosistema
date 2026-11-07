#include "flex_kbs_model.h"

#include <stdio.h>
#include <string.h>

#define SCR_W 480

void flex_kbs_init(flex_kbs_t *s, const flex_kb_prefs_t *pr, bool lang_es, int mt_max)
{
    memset(s, 0, sizeof(*s));
    s->pr = *pr;
    s->lang_es = lang_es;
    s->mt_max = mt_max;
    s->page = FLEX_KBS_MAIN;
}

const char *flex_kbs_title(int page)
{
    switch (page) {
    case FLEX_KBS_DESIGN:
        return "Dise\xC3\xB1o y tama\xC3\xB1o";
    case FLEX_KBS_TOUCH:
        return "Deslizar y tocar";
    case FLEX_KBS_SYMS:
        return "S\xC3\xADmbolos";
    case FLEX_KBS_SHORT:
        return "Atajos de texto";
    case FLEX_KBS_SCEDIT:
        return "Editar atajo";
    case FLEX_KBS_ABOUT:
        return "Sobre teclado";
    case FLEX_KBS_LANG:
        return "Idiomas y tipos";
    default:
        return "Teclado";
    }
}

// ---- constructor -------------------------------------------------------------------------------------------------------------------
static flex_set_op_t *push(flex_set_page_t *p, uint8_t kind)
{
    if (p->n >= FLEX_SET_OPS_MAX) {
        return NULL;
    }
    flex_set_op_t *o = &p->op[p->n++];
    memset(o, 0, sizeof(*o));
    o->kind = kind;
    return o;
}

static void text(flex_set_page_t *p, int x, int y, const char *s, int size, int role, int max_right)
{
    flex_set_op_t *o = push(p, FLEX_SO_TEXT);
    if (o) {
        o->x = (int16_t)x;
        o->y = (int16_t)y;
        o->size = (uint8_t)size;
        o->role = (uint8_t)role;
        o->max_right = (int16_t)max_right;
        snprintf(o->text, sizeof(o->text), "%s", s ? s : "");
    }
}

static void text_c(flex_set_page_t *p, int cx, int y, const char *s, int size, int role)
{
    flex_set_op_t *o = push(p, FLEX_SO_TEXT_C);
    if (o) {
        o->x = (int16_t)cx;
        o->y = (int16_t)y;
        o->size = (uint8_t)size;
        o->role = (uint8_t)role;
        snprintf(o->text, sizeof(o->text), "%s", s ? s : "");
    }
}

// kbsRow: tarjeta a todo el ancho; apagada por hardware, sin chevron y con el color de control desactivado
static int row(flex_set_page_t *p, int y, const char *title, const char *val, bool chevron, bool enabled)
{
    const int rh = 62, x = 12, w = SCR_W - 24;
    if (p->rows < FLEX_SET_ROW_MAX) {
        p->row_y0[p->rows] = y;
        p->row_y1[p->rows] = y + rh;
        p->row_op[p->rows] = p->n;
        p->rows++;
    }
    flex_set_op_t *c = push(p, FLEX_SO_CARD);
    if (c) {
        c->x = x;
        c->y = (int16_t)y;
        c->w = w;
        c->h = (int16_t)(rh - 8);
        c->r = 12;
    }
    text(p, x + 16, y + 8, title, 2, enabled ? FLEX_SR_HI : FLEX_SR_DIS, x + w - 28);
    if (val) {
        text(p, x + 16, y + 32, val, 1, enabled ? FLEX_SR_LO : FLEX_SR_DIS, x + w - 28);
    }
    if (chevron && enabled) {
        flex_set_op_t *v = push(p, FLEX_SO_CHEV);
        if (v) {
            v->x = (int16_t)(x + w - 18);
            v->y = (int16_t)(y + (rh - 8) / 2);
        }
    }
    return y + rh;
}

static int section(flex_set_page_t *p, int y, const char *t)
{
    text(p, 16, y, t, 2, FLEX_SR_HI, 0);
    return y + 30;
}

static int note(flex_set_page_t *p, int y, const char *t, int step)
{
    text(p, 16, y, t, 1, FLEX_SR_MUTE, SCR_W - 16);
    return y + step;
}

static const char *size_name(int v)
{
    return v == FLEX_KB_SIZE_COMPACT ? "Compacto" : v == FLEX_KB_SIZE_BIG ? "Grande" : "Normal";
}
static const char *style_name(int v)
{
    return v == 1 ? "Cuadrada" : v == 2 ? "Contorno" : "Redondeada";
}
static const char *font_name(int v)
{
    return v == 0 ? "Peque\xC3\xB1"
                    "a"
                  : v == 2 ? "Grande"
                           : "Normal";
}
static const char *fx_name(int v)
{
    return v == 60 ? "Corta" : v == 160 ? "Larga" : "Normal";
}
static const char *on_off(bool b)
{
    return b ? "Activado" : "Desactivado";
}

void flex_kbs_build(flex_set_page_t *p, const flex_kbs_t *s)
{
    memset(p, 0, sizeof(*p));
    const flex_kb_prefs_t *k = &s->pr;
    int y = 0;
    char v[64];
    if (s->page == FLEX_KBS_MAIN) {
        y = row(p, y, "Idiomas y tipos", s->lang_es ? "Espa\xC3\xB1ol (ES) - activo" : "English (EN) - activo", true, true);
        y = row(p, y, "Texto predictivo", on_off(k->predict), false, true);
        y = row(p, y, "Revisi\xC3\xB3n ortogr\xC3\xA1"
                      "fica b\xC3\xA1sica",
                on_off(k->spell), false, true);
        y = row(p, y, "Sugerir emojis", on_off(k->emoji_sug), false, true);
        y = row(p, y, "Atajos de texto", "Abreviaci\xC3\xB3n -> expansi\xC3\xB3n", true, true);
        y += 6;
        y = section(p, y, "Teclado");
        y = row(p, y, "Barra de herramientas del teclado", on_off(k->toolbar), false, true);
        y = row(p, y, "Teclado de contraste alto", on_off(k->hicon), false, true);
        if (k->fast) {
            snprintf(v, sizeof(v), "Activado - el panel ha dado %d dedo%s a la vez", s->mt_max, s->mt_max == 1 ? "" : "s");
        } else {
            snprintf(v, sizeof(v), "Desactivado - se escribe al soltar");
        }
        y = row(p, y, "Escritura r\xC3\xA1pida (multitoque)", v, false, true);
        y = row(p, y, "Dise\xC3\xB1o y tama\xC3\xB1o", size_name(k->size), true, true);
        y = row(p, y, "Deslizar, tocar y respuesta t\xC3\xA1"
                      "ctil",
                fx_name(k->fx_ms), true, true);
        y += 6;
        y = section(p, y, "No disponible en este hardware");
        y = row(p, y, "Entrada de voz", "Necesita reconocimiento en la nube", false, false);
        y = row(p, y, "Guardar capturas en portapapeles", "FlexOS no tiene captura de pantalla", false, false);
        y += 6;
        y = section(p, y, "Otros");
        y = row(p, y, "Restablecer ajustes del teclado", "Vuelve a los valores por defecto", false, true);
        y = row(p, y, "Sobre teclado", "Teclado FlexOS", true, true);
    } else if (s->page == FLEX_KBS_LANG) {
        y = row(p, y, "Espa\xC3\xB1ol (ES)", s->lang_es ? "Activo" : "Toca para activar", false, true);
        y = row(p, y, "English (EN)", s->lang_es ? "Toca para activar" : "Activo", false, true);
        y = row(p, y, "Capa num\xC3\xA9rica y de s\xC3\xADmbolos", "Siempre disponible (?123)", false, false);
        y = row(p, y, "Emoticonos de texto", "Capa emoji, glifos de la fuente", false, false);
        y += 8;
        y = note(p, y, "El teclado soporta estos dos idiomas: son los que", 18);
        y = note(p, y, "tienen mapa de teclas y diccionario propios.", 22);
    } else if (s->page == FLEX_KBS_DESIGN) {
        y = row(p, y, "Tama\xC3\xB1o de teclado", size_name(k->size), false, true);
        snprintf(v, sizeof(v), "%d%% de opacidad", (int)k->opacity);
        y = row(p, y, "Tama\xC3\xB1o y transparencia", v, false, true);
        y = row(p, y, "Dise\xC3\xB1o", style_name(k->style), false, true);
        y = row(p, y, "Tama\xC3\xB1o de fuente", font_name(k->font), false, true);
        snprintf(v, sizeof(v), "%s %s %s %s", flex_kb_sym_at(k, 0), flex_kb_sym_at(k, 1), flex_kb_sym_at(k, 2), flex_kb_sym_at(k, 3));
        y = row(p, y, "S\xC3\xADmbolos personalizados", v, true, true);
        y += 8;
        y = note(p, y, "Sin \"dividir teclado\": con 480 px de ancho no caben", 18);
        y = note(p, y, "dos mitades usables. En su lugar, los tres tamanos.", 22);
    } else if (s->page == FLEX_KBS_TOUCH) {
        y = row(p, y, "Animaci\xC3\xB3n de tecla presionada", fx_name(k->fx_ms), false, true);
        snprintf(v, sizeof(v), "%d ms", (int)k->lp_ms);
        y = row(p, y, "Pulsaci\xC3\xB3n larga (acentos)", v, false, true);
        y = row(p, y, "Respuesta h\xC3\xA1ptica", "La placa no tiene motor vibrador", false, false);
        y += 8;
        y = note(p, y, "Todo el retorno de esta pantalla es VISUAL.", 22);
    } else if (s->page == FLEX_KBS_SYMS) {
        for (int i = 0; i < FLEX_KB_SYMS; i++) {
            char t[24];
            snprintf(t, sizeof(t), "S\xC3\xADmbolo %d", i + 1);
            y = row(p, y, t, flex_kb_sym_at(k, i), false, true);
        }
        y += 8;
        y = note(p, y, "Toca una fila para cambiar el simbolo. Salen en la", 18);
        y = note(p, y, "franja de arriba al entrar en la capa ?123.", 22);
    } else if (s->page == FLEX_KBS_SHORT) {
        for (int i = 0; i < FLEX_KB_SC_MAX; i++) {
            if (k->sc_abr[i][0] && k->sc_exp[i][0]) {
                snprintf(v, sizeof(v), "%s -> %s", k->sc_abr[i], k->sc_exp[i]);
            } else {
                snprintf(v, sizeof(v), "Vacio - toca para crear");
            }
            char t[24];
            snprintf(t, sizeof(t), "Atajo %d", i + 1);
            y = row(p, y, t, v, true, true);
        }
        y += 8;
        y = note(p, y, "Al escribir la abreviacion, el chip de sugerencia", 18);
        y = note(p, y, "ofrece la expansion completa.", 22);
    } else if (s->page == FLEX_KBS_ABOUT) {
        y = row(p, y, "Teclado FlexOS", "Version 1.0", false, false);
        snprintf(v, sizeof(v), "%d palabras ES / %d EN", flex_kb_dict_n(true), flex_kb_dict_n(false));
        y = row(p, y, "Diccionario local", v, false, false);
        y = row(p, y, "Idiomas", "Espa\xC3\xB1ol, English", false, false);
        y += 10;
        y = note(p, y, "El autocompletado es una LISTA LOCAL FIJA escrita", 18);
        y = note(p, y, "en el propio firmware. No es un modelo de IA: no", 18);
        y = note(p, y, "aprende, no entiende el contexto y no predice la", 18);
        y = note(p, y, "palabra siguiente. Solo busca por prefijo.", 24);
        y = note(p, y, "La revision ortografica compara contra esa misma", 18);
        y = note(p, y, "lista: subraya lo que no encuentra, nada mas.", 22);
    } else if (s->page == FLEX_KBS_SCEDIT) {
        for (int f = 0; f < 2; f++) {
            int fy = f * 76;
            text(p, 16, fy, f == 0 ? "Abreviaci\xC3\xB3n (lo que escribes)" : "Expansi\xC3\xB3n (lo que aparece)", 1, FLEX_SR_LO, 0);
            flex_set_op_t *c = push(p, FLEX_SO_CARD);
            if (c) {
                c->x = 12;
                c->y = (int16_t)(fy + 18);
                c->w = SCR_W - 24;
                c->h = 42;
                c->r = 10;
            }
            if (s->sc_field == f) {
                flex_set_op_t *fr = push(p, FLEX_SO_FRAME);
                if (fr) {
                    fr->x = 12;
                    fr->y = (int16_t)(fy + 18);
                    fr->w = SCR_W - 24;
                    fr->h = 42;
                    fr->r = 10;
                    fr->glyph = 1;
                }
            }
            text(p, 24, fy + 30, f == 0 ? s->sc_a : s->sc_e, 2, FLEX_SR_HI, SCR_W - 30);
        }
        int by = 160, hw = (SCR_W - 36) / 2;
        for (int b = 0; b < 2; b++) {
            flex_set_op_t *o = push(p, FLEX_SO_BTN);
            if (o) {
                o->x = (int16_t)(b == 0 ? 12 : 24 + hw);
                o->y = (int16_t)by;
                o->w = (int16_t)hw;
                o->h = 46;
                o->r = 12;
                o->glyph = (uint8_t)b;
            }
            text_c(p, (b == 0 ? 12 : 24 + hw) + (SCR_W - 36) / 4, by + 14, b == 0 ? "Guardar" : "Borrar", 2, FLEX_SR_ONACC);
        }
    }
    p->content_h = y + 20;
}

// ---- acciones ----------------------------------------------------------------------------------------------------------------------
static void defaults(flex_kbs_t *s)
{
    flex_kb_prefs_defaults(&s->pr);   // kbsResetDefaults: tamano, rapida, barra, predictivo, ortografia, emojis, contraste, opacidad, estilo, fuente, pulsacion, destello, simbolos y atajos
}

void flex_kbs_row_action(flex_kbs_t *s, int idx, flex_kbs_act_t *out)
{
    memset(out, 0, sizeof(*out));
    out->go = -1;
    flex_kb_prefs_t *k = &s->pr;
    if (s->page == FLEX_KBS_MAIN) {
        switch (idx) {
        case 0:
            out->go = FLEX_KBS_LANG;
            return;
        case 1:
            k->predict = !k->predict;
            break;
        case 2:
            k->spell = !k->spell;
            break;
        case 3:
            k->emoji_sug = !k->emoji_sug;
            break;
        case 4:
            out->go = FLEX_KBS_SHORT;
            return;
        case 5:
            k->toolbar = !k->toolbar;
            break;
        case 6:
            k->hicon = !k->hicon;
            break;
        case 7:
            k->fast = !k->fast;
            out->flags |= FLEX_KBSF_MT_RESET;
            break;
        case 8:
            out->go = FLEX_KBS_DESIGN;
            return;
        case 9:
            out->go = FLEX_KBS_TOUCH;
            return;
        case 10:
        case 11:
            return;   // filas informativas (hardware)
        case 12:
            defaults(s);
            out->flags = FLEX_KBSF_SAVE | FLEX_KBSF_APPLY_SIZE;
            out->render = true;
            return;
        case 13:
            out->go = FLEX_KBS_ABOUT;
            return;
        default:
            return;
        }
        out->flags |= FLEX_KBSF_SAVE;
        out->render = true;
        return;
    }
    if (s->page == FLEX_KBS_LANG) {
        if (idx == 0 || idx == 1) {
            s->lang_es = (idx == 0);
            out->render = true;
        }
        return;
    }
    if (s->page == FLEX_KBS_DESIGN) {
        if (idx == 0) {
            k->size = (k->size + 1) % 3;
            out->flags |= FLEX_KBSF_APPLY_SIZE;
        } else if (idx == 1) {
            k->opacity -= 15;
            if (k->opacity < 40) {
                k->opacity = 100;
            }
        } else if (idx == 2) {
            k->style = (k->style + 1) % 3;
        } else if (idx == 3) {
            k->font = (k->font + 1) % 3;
        } else if (idx == 4) {
            out->go = FLEX_KBS_SYMS;
            return;
        } else {
            return;
        }
        out->flags |= FLEX_KBSF_SAVE;
        out->render = true;
        return;
    }
    if (s->page == FLEX_KBS_TOUCH) {
        if (idx == 0) {
            k->fx_ms = (k->fx_ms == 60) ? 100 : (k->fx_ms == 100) ? 160 : 60;
        } else if (idx == 1) {
            k->lp_ms = (k->lp_ms == 350) ? 500 : (k->lp_ms == 500) ? 700 : 350;
        } else {
            return;
        }
        out->flags |= FLEX_KBSF_SAVE;
        out->render = true;
        return;
    }
    if (s->page == FLEX_KBS_SYMS) {
        if (idx >= 0 && idx < FLEX_KB_SYMS) {
            k->sym[idx] = (k->sym[idx] + 1) % FLEX_KB_SYM_POOL_N;
            out->flags |= FLEX_KBSF_SAVE;
            out->render = true;
        }
        return;
    }
    if (s->page == FLEX_KBS_SHORT) {
        if (idx >= 0 && idx < FLEX_KB_SC_MAX) {
            flex_kbs_edit_open(s, idx);
            out->go = FLEX_KBS_SCEDIT;
        }
        return;
    }
}

int flex_kbs_back_target(int page)
{
    if (page == FLEX_KBS_MAIN) {
        return -1;
    }
    if (page == FLEX_KBS_SCEDIT) {
        return FLEX_KBS_SHORT;
    }
    if (page == FLEX_KBS_SYMS) {
        return FLEX_KBS_DESIGN;
    }
    return FLEX_KBS_MAIN;
}

bool flex_kbs_back_hit(int x, int y)
{
    return x < FLEX_KBS_BACK_X && y < FLEX_KBS_BACK_Y;
}

// ---- editor de atajos ----------------------------------------------------------------------------------------------------------------------
int flex_kbs_edit_hit(int x, int y)
{
    for (int f = 0; f < 2; f++) {
        int fy = FLEX_KBS_TOP + f * 76;
        if (y >= fy + 18 && y <= fy + 60) {
            return f;
        }
    }
    int by = FLEX_KBS_TOP + 160, hw = (SCR_W - 36) / 2;
    if (y >= by && y <= by + 46) {
        if (x >= 12 && x <= 12 + hw) {
            return FLEX_KBS_E_SAVE;
        }
        if (x >= 24 + hw) {
            return FLEX_KBS_E_DELETE;
        }
    }
    return FLEX_KBS_E_NONE;
}

void flex_kbs_edit_open(flex_kbs_t *s, int idx)
{
    s->sc_sel = idx;
    s->sc_field = 0;
    snprintf(s->sc_a, sizeof(s->sc_a), "%s", s->pr.sc_abr[idx]);
    snprintf(s->sc_e, sizeof(s->sc_e), "%s", s->pr.sc_exp[idx]);
}

void flex_kbs_edit_focus(flex_kbs_t *s, int field)
{
    s->sc_field = field ? 1 : 0;
}

static void copy_in(flex_kbs_t *s)
{
    if (s->sc_a[0] && s->sc_e[0]) {
        snprintf(s->pr.sc_abr[s->sc_sel], FLEX_KB_SC_ABR, "%s", s->sc_a);
        snprintf(s->pr.sc_exp[s->sc_sel], FLEX_KB_SC_EXP, "%s", s->sc_e);
    }
}

void flex_kbs_edit_save(flex_kbs_t *s, flex_kbs_act_t *out)
{
    memset(out, 0, sizeof(*out));
    copy_in(s);
    out->flags = FLEX_KBSF_SAVE;
    out->go = FLEX_KBS_SHORT;
}

void flex_kbs_edit_delete(flex_kbs_t *s, flex_kbs_act_t *out)
{
    memset(out, 0, sizeof(*out));
    s->pr.sc_abr[s->sc_sel][0] = 0;
    s->pr.sc_exp[s->sc_sel][0] = 0;
    out->flags = FLEX_KBSF_SAVE;
    out->go = FLEX_KBS_SHORT;
}

void flex_kbs_edit_enter(flex_kbs_t *s, flex_kbs_act_t *out)
{
    memset(out, 0, sizeof(*out));
    out->go = -1;
    if (s->sc_a[0] && s->sc_e[0]) {
        flex_kbs_edit_save(s, out);
        return;
    }
    out->render = true;   // la tecla OK sin los dos textos: solo repinta
}

bool flex_kbs_edit_append(flex_kbs_t *s, const char *utf8)
{
    return s->sc_field == 0 ? flex_kb_buf_append(s->sc_a, sizeof(s->sc_a), utf8) : flex_kb_buf_append(s->sc_e, sizeof(s->sc_e), utf8);
}

bool flex_kbs_edit_backspace(flex_kbs_t *s)
{
    return s->sc_field == 0 ? flex_kb_buf_backspace(s->sc_a) : flex_kb_buf_backspace(s->sc_e);
}

void flex_kbs_apply_lang(flex_kbs_t *s, bool es)
{
    s->lang_es = es;
}
