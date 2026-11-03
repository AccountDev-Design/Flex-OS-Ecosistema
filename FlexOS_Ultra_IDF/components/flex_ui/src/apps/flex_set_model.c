#include "flex_set_model.h"

#include <stdio.h>
#include <string.h>

#define SCR_W 480
#define DP_X  FLEX_SET_CARD_X
#define DP_W  FLEX_SET_CARD_W
#define RGB565(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

const char *const flex_set_cat[FLEX_SET_NCAT] = {"General",   "Pantalla",    "Sonido",         "Red e Internet",
                                                 "Dispositivos", "Personalizaci\xC3\xB3n", "Seguridad y privacidad", "Bater\xC3\xAD"
                                                                                                                  "a",
                                                 "Almacenamiento", "Desarrollador", "Sistema", "Acerca de"};
const char *const flex_set_sub[FLEX_SET_NCAT] = {"Idioma, fecha, hora", "Brillo, fondo, tema", "Volumen, tonos",  "WiFi, Bluetooth",
                                                 "GPIO, perifericos",   "Temas, iconos",      "Bloqueo y permisos", "Ahorro de energia",
                                                 "Interna, SD",         "Opciones dev",       "Sistema, logs",    "Version, creditos"};
const char *const flex_set_desc[FLEX_SET_NCAT] = {"Configura las opciones basicas del sistema.", "Brillo, fondo de pantalla y modo oscuro.",
                                                  "Volumen, tonos y notificaciones.",            "Conexiones de red (offline por ahora).",
                                                  "GPIO, modulos y perifericos.",                "Temas, iconos y estilo del sistema.",
                                                  "Bloqueo, permisos y privacidad.",             "Estado de la bateria y ahorro de energia.",
                                                  "Memoria interna y tarjeta SD.",               "Herramientas y diagnostico de desarrollo.",
                                                  "Informacion del sistema y registros.",        "Version, hardware y creditos de FlexOS."};
const uint16_t flex_set_accent[FLEX_SET_NCAT] = {
    RGB565(70, 120, 235), RGB565(240, 170, 50), RGB565(70, 120, 235), RGB565(60, 150, 235), RGB565(80, 180, 120), RGB565(90, 110, 235),
    RGB565(90, 95, 110),  RGB565(80, 190, 110), RGB565(150, 90, 210), RGB565(70, 75, 90),   RGB565(70, 120, 235), RGB565(70, 120, 235)};

const uint32_t flex_set_autolock_ms[FLEX_AUTOLOCK_N] = {30000UL, 60000UL, 300000UL, 600000UL, 1800000UL, 0UL};
const char *const flex_set_autolock_name[FLEX_AUTOLOCK_N] = {"30 segundos", "1 minuto", "5 minutos", "10 minutos", "30 minutos", "Nunca"};

// ---- ayudas de texto -------------------------------------------------------------------------------------------------------
const char *flex_set_lang_name(int lang)
{
    static const char *const endonym[5] = {"Espa\xC3\xB1ol", "English", "Fran\xC3\xA7"
                                                                        "ais",
                                           "Portugu\xC3\xAas", "Italiano"};
    if (lang == 5) {
        return "Chinese";
    }
    return (lang >= 0 && lang < 5) ? endonym[lang] : "English";
}

void flex_set_datetime_str(char *out, size_t n, int d, int mo, int y, int h, int min)
{
    int h12 = h % 12;
    if (h12 == 0) {
        h12 = 12;
    }
    snprintf(out, n, "%02d/%02d/%04d  %d:%02d %s", d, mo, y, h12, min, h < 12 ? "AM" : "PM");
}

void flex_set_uptime(unsigned long ms, char *out, size_t n)
{
    unsigned long s = ms / 1000UL;
    unsigned long d = s / 86400;
    s %= 86400;
    unsigned long h = s / 3600;
    s %= 3600;
    unsigned long m = s / 60;
    if (d > 0) {
        snprintf(out, n, "%lud %luh %lum", d, h, m);
    } else if (h > 0) {
        snprintf(out, n, "%luh %lum", h, m);
    } else {
        snprintf(out, n, "%lum", m);
    }
}

const char *flex_set_unparen(char *s)
{
    size_t l = strlen(s);
    if (l > 1 && s[0] == '(' && s[l - 1] == ')') {
        s[l - 1] = '\0';
        return s + 1;
    }
    return s;
}

int flex_set_autolock_idx(uint32_t ms)
{
    for (int i = 0; i < FLEX_AUTOLOCK_N; i++) {
        if (flex_set_autolock_ms[i] == ms) {
            return i;
        }
    }
    return 1;   // AUTOLOCK_DEFAULT_IDX: 1 minuto
}

uint32_t flex_set_autolock_norm(uint32_t ms)
{
    for (int i = 0; i < FLEX_AUTOLOCK_N; i++) {
        if (flex_set_autolock_ms[i] == ms) {
            return ms;
        }
    }
    return flex_set_autolock_ms[1];
}

// ---- constructor de paginas --------------------------------------------------------------------------------------------------
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

static void text_r(flex_set_page_t *p, int x, int y, const char *s, int size, int role)
{
    flex_set_op_t *o = push(p, FLEX_SO_TEXT_R);
    if (o) {
        o->x = (int16_t)x;
        o->y = (int16_t)y;
        o->size = (uint8_t)size;
        o->role = (uint8_t)role;
        snprintf(o->text, sizeof(o->text), "%s", s ? s : "");
    }
}

static void chev(flex_set_page_t *p, int cx, int cy)
{
    flex_set_op_t *o = push(p, FLEX_SO_CHEV);
    if (o) {
        o->x = (int16_t)cx;
        o->y = (int16_t)cy;
    }
}

// setRowCard: tarjeta + glifo + titulo + valor + chevron; devuelve la y siguiente
static int row(flex_set_page_t *p, int y, int glyph, uint16_t icol, const char *title, const char *val, bool chevron)
{
    const int rh = 64, mr = DP_X + DP_W - 30;
    if (p->rows < FLEX_SET_ROW_MAX) {
        p->row_y0[p->rows] = y;
        p->row_y1[p->rows] = y + rh;
        p->row_op[p->rows] = p->n;
        p->rows++;
    }
    flex_set_op_t *c = push(p, FLEX_SO_CARD);
    if (c) {
        c->x = DP_X;
        c->y = (int16_t)y;
        c->w = DP_W;
        c->h = (int16_t)(rh - 8);
        c->r = 14;
    }
    flex_set_op_t *g = push(p, FLEX_SO_GLYPH);
    if (g) {
        g->glyph = (uint8_t)glyph;
        g->x = DP_X + 26;
        g->y = (int16_t)(y + (rh - 8) / 2);
        g->color = icol;
    }
    text(p, DP_X + 52, y + 10, title, 2, FLEX_SR_HI, mr);
    if (val) {
        text(p, DP_X + 52, y + 34, val, 1, FLEX_SR_LO, mr);
    }
    if (chevron) {
        chev(p, DP_X + DP_W - 20, y + (rh - 8) / 2);
    }
    return y + rh;
}

static int info_line(flex_set_page_t *p, int y, const char *label, const char *val)
{
    text(p, DP_X, y, label, 1, FLEX_SR_LO, 0);
    text_r(p, SCR_W - 12, y, val, 1, FLEX_SR_HI);
    return y + 24;
}

static int device_info(flex_set_page_t *p, int y, const flex_set_sys_t *s)
{
    text(p, DP_X, y, "Dispositivo", 2, FLEX_SR_HI, 0);
    y += 30;
    char v[48];
    y = info_line(p, y, "Nombre", s->name);
    y = info_line(p, y, "Modelo", "ESP32-P4 DevKit");
    y = info_line(p, y, "Version", "FlexOS Ultra 1.0");
    y = info_line(p, y, "Actividad", s->uptime);
    snprintf(v, sizeof(v), "%u KB libre", s->free_kb);
    y = info_line(p, y, "RAM", v);
    snprintf(v, sizeof(v), "%u / %u MB", s->psram_free_mb, s->psram_total_mb);
    y = info_line(p, y, "PSRAM", v);
    y = info_line(p, y, "Flash", "16 MB");
    y = info_line(p, y, "CPU", "RISC-V dual 360 MHz");
    return y;
}

static int section(flex_set_page_t *p, int y, const char *title)
{
    y += 8;
    text(p, DP_X, y, title, 2, FLEX_SR_HI, 0);
    return y + 30;
}

static int help(flex_set_page_t *p, int y, const char *s, int size)
{
    y += 8;
    text(p, DP_X, y, s, size, FLEX_SR_MUTE, 0);
    return y + 24;
}

void flex_set_detail_build(flex_set_page_t *p, int cat, const flex_set_sys_t *s)
{
    memset(p, 0, sizeof(*p));
    int y = 6;   // base + 6
    const uint16_t ic = RGB565(70, 120, 225);
    char v[64];
    if (cat == 0) {
        y = row(p, y, FLEX_RI_GLOBE, RGB565(60, 140, 235), "Idioma", flex_set_lang_name(s->lang), true);
        flex_set_datetime_str(v, sizeof(v), s->rtc_d, s->rtc_mo, s->rtc_y, s->rtc_h, s->rtc_min);
        y = row(p, y, FLEX_RI_CAL, RGB565(235, 90, 90), "Fecha y hora", v, true);
        y = row(p, y, FLEX_RI_CLOCK, RGB565(90, 120, 230), "Formato de hora", s->h24 ? "24 horas" : "12 horas", true);
        y = row(p, y, FLEX_RI_PIN, RGB565(230, 80, 80), "Zona horaria", "GMT-05:00 Lima (sin DST)", false);
        y = row(p, y, FLEX_RI_CLOUD, s->time_nvs_ok ? RGB565(60, 170, 220) : RGB565(220, 80, 80), "Sincronizaci\xC3\xB3n autom\xC3\xA1tica",
                s->ntp_state, false);
        y = row(p, y, FLEX_RI_CLOCK, RGB565(90, 120, 230), "\xC3\x9Altima sincronizaci\xC3\xB3n", s->ntp_last, false);
        y = row(p, y, FLEX_RI_REFRESH, RGB565(80, 180, 120), "Sincronizar ahora", s->ntp_busy ? "Sincronizando..." : (s->ntp_available ? "Consultar el servidor de hora" : "No disponible"), true);
        y = row(p, y, FLEX_RI_REFRESH, RGB565(60, 160, 230), "Actualizaciones", s->ota_text, true);
        y = row(p, y, FLEX_RI_CLOUD, RGB565(120, 160, 230), "Copias de seguridad", "Proximamente", true);
        y = row(p, y, FLEX_RI_RESET, RGB565(220, 80, 80), "Restablecer", "Opciones de fabrica", true);
        y = section(p, y, "Cuenta");
        y = row(p, y, FLEX_RI_DOT, RGB565(105, 91, 230), "Flex Account", s->account_text, true);
        y += 12;
        y = device_info(p, y, s);
    } else if (cat == 11) {
        y = device_info(p, y, s);
        y += 8;
        text(p, DP_X, y, "FlexOS Ultra - desde cero", 1, FLEX_SR_MUTE, 0);
        y += 22;
        text(p, DP_X, y, "para ESP32-P4 - 2026", 1, FLEX_SR_MUTE, 0);
        y += 22;
    } else if (cat == 1) {
        snprintf(v, sizeof(v), "%d%%", s->bright);
        y = row(p, y, FLEX_RI_DOT, RGB565(240, 170, 50), "Brillo", v, true);
        y = row(p, y, FLEX_RI_DOT, RGB565(90, 110, 235), "Estilo", s->glass ? "Liquid Glass" : "Plano", true);
        y = row(p, y, FLEX_RI_DOT, s->dark ? RGB565(120, 130, 220) : RGB565(240, 170, 50), "Modo de apariencia", s->dark ? "Oscuro" : "Claro", true);
        y = row(p, y, FLEX_RI_DOT, RGB565(100, 180, 240), "Barra de navegacion", s->navmode == 0 ? "Botones" : "Gestos iOS", true);
        y = help(p, y, "Toca una fila para cambiarla", 1);
        y = section(p, y, "Bloqueo");
        y = row(p, y, FLEX_RI_CLOCK, RGB565(90, 120, 230), "Reloj grande", (s->lockwidgets & FLEX_LW_CLOCK) ? "Activado" : "Desactivado", true);
        y = row(p, y, FLEX_RI_CLOUD, RGB565(90, 170, 235), "Clima", (s->lockwidgets & FLEX_LW_WEATHER) ? "Activado" : "Desactivado", true);
        y = row(p, y, FLEX_RI_CAL, RGB565(235, 110, 90), "Calendario", (s->lockwidgets & FLEX_LW_CAL) ? "Activado" : "Desactivado", true);
        y = row(p, y, FLEX_RI_DOT, RGB565(230, 180, 90), "Notificaciones", (s->lockwidgets & FLEX_LW_NOTIF) ? "Activado" : "Desactivado", true);
        y = help(p, y, "Elige los widgets de la pantalla de bloqueo", 1);
    } else if (cat == 5) {
        y = row(p, y, FLEX_RI_DOT, RGB565(90, 110, 235), "Personalizar UI", s->glass ? "Liquid Glass" : "Plano", true);
        y = row(p, y, FLEX_RI_DOT, RGB565(150, 90, 210), "Iconos", s->iconstyle == 1 ? "Vidrio" : "Plano", true);
        const char *av = s->animstyle == 1 ? "Fundido" : s->animstyle == 2 ? "Deslizar" : "Zoom";
        y = row(p, y, FLEX_RI_DOT, RGB565(90, 200, 160), "Transiciones", av, true);
        y = row(p, y, FLEX_RI_DOT, RGB565(235, 150, 60), "Teclado", s->kbsize == 0 ? "Compacto" : s->kbsize == 2 ? "Grande" : "Normal", true);
        y = help(p, y, "Toca una fila para cambiar su estilo", 1);
    } else if (cat == 3) {
        char rv0[64], rv1[64];
        snprintf(rv0, sizeof(rv0), "%s", s->wifi_sub);
        snprintf(rv1, sizeof(rv1), "%s", s->ble_sub);
        const char *rvp = flex_set_unparen(rv0);
        const char *rbp = flex_set_unparen(rv1);
        y = row(p, y, FLEX_RI_GLOBE, RGB565(60, 150, 235), "Conectividad", s->airplane ? "Modo avi\xC3\xB3n activo" : "Wifi, BLE y modo avi\xC3\xB3n", true);
        y = row(p, y, FLEX_RI_CLOUD, RGB565(70, 120, 225), "Wi-Fi", rvp, true);
        y = row(p, y, FLEX_RI_DOT, RGB565(90, 110, 235), "Bluetooth (BLE)", rbp, true);
        y = help(p, y, "Los interruptores encienden la radio de verdad", 1);
    } else if (cat == 6) {
        const char *lt = s->locktype == 1 ? "PIN configurado" : s->locktype == 2 ? "Contrase\xC3\xB1"
                                                                                  "a configurada"
                                                                                : "Deslizar";
        y = row(p, y, FLEX_RI_DOT, RGB565(220, 120, 120), "Bloqueo", lt, true);
        y = row(p, y, FLEX_RI_CLOCK, RGB565(120, 150, 235), "Bloqueo de inactividad", flex_set_autolock_name[flex_set_autolock_idx(s->autolock_ms)], true);
        y = row(p, y, FLEX_RI_DOT, RGB565(230, 120, 90), "Protecci\xC3\xB3n contra robo", s->theft, true);
        y = row(p, y, FLEX_RI_DOT, RGB565(220, 120, 120), "Apagado seguro", s->locktype == 0 ? "Configura antes un PIN" : (s->poffpin ? "Activado" : "Desactivado"),
                true);
        y = help(p, y, "Toca para configurar PIN o contrase\xC3\xB1"
                       "a",
                 1);
    } else {
        const char *rt[3] = {0, 0, 0};
        const char *rv[3] = {0, 0, 0};
        int rn = 0;
        char s2a[32], s8a[48];
        switch (cat) {
        case 2:
            if (s->audio_avail) {
                snprintf(s2a, sizeof(s2a), "%d%%%s", s->volume, s->muted ? " (silenciado)" : "");
                rt[0] = "Volumen";
                rv[0] = s2a;
                rt[1] = "Salida";
                rv[1] = "Altavoz (ES8311)";
            } else {
                rt[0] = "Volumen";
                rv[0] = "Sin salida de audio";
                rt[1] = "Motivo";
                rv[1] = s->audio_error;
            }
            rn = 2;
            break;
        case 4:
            rt[0] = "GPIO";
            rv[0] = "Configurable";
            rt[1] = "Perifericos";
            rv[1] = "Ninguno";
            rn = 2;
            break;
        case 7:
            rt[0] = "Nivel";
            rv[0] = "82%";
            rt[1] = "Ahorro";
            rv[1] = "Desactivado";
            rn = 2;
            break;
        case 8:
            snprintf(s8a, sizeof(s8a), "%s / %s", s->fs_used, s->fs_total);
            rt[0] = "Memoria interna";
            rv[0] = s->fs_ready ? s8a : "No montada";
            rt[1] = "Papelera";
            rv[1] = s->trash_size;
            rn = 2;
            break;
        case 9:
            rt[0] = "Depuracion";
            rv[0] = "En pantalla";
            rt[1] = "Banda reinicio";
            rv[1] = "Solo crash";
            rn = 2;
            break;
        case 10:
            rt[0] = "Version";
            rv[0] = "FlexOS 1.0";
            rt[1] = "Logs";
            rv[1] = "Puerto serie";
            rn = 2;
            break;
        default:
            rn = 0;
            break;
        }
        for (int i = 0; i < rn; i++) {
            y = row(p, y, FLEX_RI_DOT, ic, rt[i], rv[i], true);
        }
        y = help(p, y, "Mas opciones proximamente", 1);
    }
    p->content_h = y + 10;
}

void flex_set_detail_head(flex_set_page_t *p, int cat)
{
    memset(p, 0, sizeof(*p));
    flex_set_op_t *b = push(p, FLEX_SO_BACK);
    if (b) {
        b->x = 30;
        b->y = 46;
    }
    text(p, FLEX_SET_BACK_W, 42, flex_set_cat[cat], 4, FLEX_SR_HI, SCR_W - 12);
    text(p, FLEX_SET_BACK_W, 94, flex_set_desc[cat], 1, FLEX_SR_LO, SCR_W - 12);
}

void flex_set_list_head(flex_set_page_t *p)
{
    memset(p, 0, sizeof(*p));
    text(p, 16, 40, "Ajustes", 4, FLEX_SR_HI, 0);
}

void flex_set_list_build(flex_set_page_t *p)
{
    memset(p, 0, sizeof(*p));
    for (int i = 0; i < FLEX_SET_NCAT; i++) {
        int y = i * (FLEX_SET_CATCARD_H + FLEX_SET_CATCARD_GAP);
        p->card_op[i] = p->n;
        flex_set_op_t *c = push(p, FLEX_SO_CARD);
        if (c) {
            c->x = FLEX_SET_CARD_X;
            c->y = (int16_t)y;
            c->w = FLEX_SET_CARD_W;
            c->h = FLEX_SET_CATCARD_H;
            c->r = 16;
        }
        flex_set_op_t *ic = push(p, FLEX_SO_CATICON);
        if (ic) {
            ic->glyph = (uint8_t)i;
            ic->x = FLEX_SET_CARD_X + 16;
            ic->y = (int16_t)(y + (FLEX_SET_CATCARD_H - 32) / 2);
            ic->w = 32;
            ic->color = flex_set_accent[i];
        }
        int tx = FLEX_SET_CARD_X + 62, mr = FLEX_SET_CARD_X + FLEX_SET_CARD_W - 30;
        text(p, tx, y + 14, flex_set_cat[i], 2, FLEX_SR_HI, mr);
        text(p, tx, y + 38, flex_set_sub[i], 1, FLEX_SR_LO, mr);
        chev(p, FLEX_SET_CARD_X + FLEX_SET_CARD_W - 20, y + FLEX_SET_CATCARD_H / 2);
    }
    p->content_h = FLEX_SET_NCAT * (FLEX_SET_CATCARD_H + FLEX_SET_CATCARD_GAP) + 10;
}

bool flex_set_card_visible(int i, int list_scroll)
{
    int y = FLEX_SET_LIST_TOP - list_scroll + i * (FLEX_SET_CATCARD_H + FLEX_SET_CATCARD_GAP);
    return !(y > FLEX_SET_LIST_BOT || y + FLEX_SET_CATCARD_H < FLEX_SET_LIST_TOP);
}

int flex_set_max_scroll(int content_h, bool list)
{
    int top = list ? FLEX_SET_LIST_TOP : FLEX_SET_DLIST_TOP, bot = list ? FLEX_SET_LIST_BOT : FLEX_SET_DLIST_BOT;
    int m = content_h - (bot - top);
    return m > 0 ? m : 0;
}

// ---- acciones ---------------------------------------------------------------------------------------------------------------
void flex_set_row_action(flex_set_sys_t *s, int cat, int idx, flex_set_act_t *out)
{
    memset(out, 0, sizeof(*out));
    if (cat == 0) {
        if (idx == 0) {
            s->lang = (s->lang + 1) % 6;
            out->flags = FLEX_SF_SAVE_LANG | FLEX_SF_HOME_DIRTY;
            out->render = FLEX_SRD_FULL;
        } else if (idx == 2) {
            s->h24 = !s->h24;
            out->flags = FLEX_SF_SAVE_H24 | FLEX_SF_HOME_DIRTY;
            out->render = FLEX_SRD_BAND;
        } else if (idx == 6) {
            if (!s->ntp_available) {
                return;   // sin servicio de hora no hay nada que consultar
            }
            out->open = FLEX_SX_NTP_SYNC;
            out->render = FLEX_SRD_BAND;
        } else if (idx == 7) {
            out->open = FLEX_SX_OTA;
        } else if (idx == 9) {
            out->open = FLEX_SX_FACTORY;
        } else if (idx == 10) {
            out->open = FLEX_SX_ACCOUNT;
        }
    } else if (cat == 1) {
        if (idx == 0) {
            s->bright += 25;
            if (s->bright > 100) {
                s->bright = 25;
            }
            out->flags = FLEX_SF_BACKLIGHT | FLEX_SF_SAVE_BRIGHT;
            out->render = FLEX_SRD_BAND;
        } else if (idx == 1) {
            s->glass = !s->glass;
            out->flags = FLEX_SF_THEME;
        } else if (idx == 2) {
            s->dark = !s->dark;
            out->flags = FLEX_SF_THEME;
        } else if (idx == 3) {
            s->navmode = (s->navmode == 0) ? 1 : 0;
            out->flags = FLEX_SF_SAVE_NAV | FLEX_SF_HOME_DIRTY;
            out->render = FLEX_SRD_FULL;
        } else if (idx >= 4 && idx <= 7) {
            static const int bits[4] = {FLEX_LW_CLOCK, FLEX_LW_WEATHER, FLEX_LW_CAL, FLEX_LW_NOTIF};
            s->lockwidgets ^= bits[idx - 4];
            out->flags = FLEX_SF_SAVE_LOCKW;
            out->render = FLEX_SRD_BAND;
        }
    } else if (cat == 5) {
        if (idx == 0) {
            s->glass = !s->glass;
            out->flags = FLEX_SF_THEME;
        } else if (idx == 1) {
            s->iconstyle = (s->iconstyle == 0) ? 1 : 0;
            out->flags = FLEX_SF_SAVE_ICON | FLEX_SF_HOME_DIRTY;
            out->render = FLEX_SRD_BAND;
        } else if (idx == 2) {
            s->animstyle = (s->animstyle + 1) % 3;
            out->flags = FLEX_SF_SAVE_ANIM;
            out->render = FLEX_SRD_BAND;
        } else if (idx == 3) {
            out->open = FLEX_SX_KEYBOARD;
        }
    } else if (cat == 3) {
        if (idx == 0) {
            out->open = FLEX_SX_CONN;
        } else if (idx == 1) {
            out->open = FLEX_SX_WIFI;
        } else if (idx == 2) {
            if (!s->ble_enabled || s->airplane) {
                return;
            }
            out->open = FLEX_SX_BLE_TOGGLE;
            out->render = FLEX_SRD_BAND;
        }
    } else if (cat == 6) {
        if (idx == 0) {
            out->open = FLEX_SX_LOCKSETUP;
        } else if (idx == 1) {
            s->autolock_ms = flex_set_autolock_ms[(flex_set_autolock_idx(s->autolock_ms) + 1) % FLEX_AUTOLOCK_N];
            out->flags = FLEX_SF_TOUCH_TIMER | FLEX_SF_SAVE_AUTOLOCK;
            out->render = FLEX_SRD_BAND;
        } else if (idx == 2) {
            out->open = FLEX_SX_THEFT;
        } else if (idx == 3) {
            if (s->locktype == 0) {
                return;   // sin clave no hay nada que pedir: activarlo seria una proteccion de mentira
            }
            s->poffpin = !s->poffpin;
            out->flags = FLEX_SF_SAVE_POFFPIN;
            out->render = FLEX_SRD_BAND;
        }
    }
}

// ---- transicion ---------------------------------------------------------------------------------------------------------------
float flex_set_ease(float p)
{
    float ip = 1.0f - p;
    return (p < 0.5f) ? (4.0f * p * p * p) : (1.0f - 4.0f * ip * ip * ip);
}

void flex_set_anim_offsets(int dir, uint32_t ms, int *out_off, int *in_off)
{
    uint32_t e = ms > (uint32_t)FLEX_SET_NAV_MS ? (uint32_t)FLEX_SET_NAV_MS : ms;
    float p = flex_set_ease((float)e / (float)FLEX_SET_NAV_MS);
    if (dir > 0) {
        *out_off = (int)(-FLEX_SET_PARALLAX * p);
        *in_off = (int)(SCR_W * (1.0f - p));
    } else {
        *out_off = (int)(SCR_W * p);
        *in_off = (int)(-FLEX_SET_PARALLAX * (1.0f - p));
    }
}

int flex_set_shadow_alpha(int k)
{
    return 70 - k * 8;
}

bool flex_set_back_swipe_armed(int start_x)
{
    return start_x < 28;
}

bool flex_set_back_swipe_done(int start_x, int start_y, int x, int y)
{
    int dy = y - start_y;
    return (x - start_x) > 70 && (dy < 0 ? -dy : dy) < 90;
}

// ---- sesion ---------------------------------------------------------------------------------------------------------------------
void flex_set_sess_pack(flex_set_sess_t *s, int view, int sel, int scroll, int list_scroll)
{
    s->view = (int16_t)view;
    s->sel = (int16_t)sel;
    s->scroll = (int16_t)scroll;
    s->list_scroll = (int16_t)list_scroll;
}

bool flex_set_sess_normalize(flex_set_sess_t *s)
{
    if (s->sel < 0 || s->sel > 11) {
        return false;   // sesion incoherente: se ignora entera
    }
    s->view = (s->view == 1) ? 1 : 0;
    s->scroll = (s->scroll > 0) ? s->scroll : 0;
    s->list_scroll = (s->list_scroll > 0) ? s->list_scroll : 0;
    return true;
}
