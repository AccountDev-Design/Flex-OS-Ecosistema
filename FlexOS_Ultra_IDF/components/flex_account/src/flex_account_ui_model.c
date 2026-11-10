// Flex OS Ultra · Flex Account: decisiones de la pantalla (ver flex_account_ui_model.h). Referencia: FlexOS_Account_Bridge.h.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "flex_account_ui_model.h"

bool flex_account_needs_relink(const flex_account_snapshot_t *s)
{
    return s->linked && (s->link == FLEX_LINK_AUTH_REQUIRED || s->link == FLEX_LINK_TOKEN_EXPIRED);
}

uint32_t flex_account_link_rgb(flex_account_link_t link)
{
    switch (link) {
    case FLEX_LINK_LINKED: return 0x2CBE85;
    case FLEX_LINK_LINKED_OFFLINE: return 0x7892C4;
    case FLEX_LINK_NETWORK_UNAVAILABLE: return 0xE8AA42;
    case FLEX_LINK_AUTH_REQUIRED:
    case FLEX_LINK_TOKEN_EXPIRED: return 0xF0805C;
    case FLEX_LINK_ERROR: return 0xEC606E;
    default: return 0x969CB0;
    }
}

const char *flex_account_primary_label(flex_account_state_t state, bool online)
{
    if (!online) {
        return "Conectar Wi-Fi";
    }
    return (state == FLEX_ACCOUNT_ERROR || state == FLEX_ACCOUNT_EXPIRED) ? "Reintentar" : "Iniciar sesion";
}

const char *flex_account_code_note(const flex_account_snapshot_t *s) { return s->error[0] ? s->error : "El codigo vence en 10 minutos"; }

const char *flex_account_back_label(flex_acc_return_t where)
{
    return where == FLEX_ACC_RET_OOBE ? "Continuar" : where == FLEX_ACC_RET_SETTINGS ? "Volver a Ajustes" : "Volver a Flex Store";
}

const char *flex_account_linked_title(const flex_account_snapshot_t *s)
{
    if (!flex_account_needs_relink(s)) {
        return "Cuenta vinculada";
    }
    return s->link == FLEX_LINK_TOKEN_EXPIRED ? "Sesion caducada" : "Vuelve a iniciar sesion";
}

const char *flex_account_unlinked_title(const flex_account_snapshot_t *s)
{
    return s->state == FLEX_ACCOUNT_ERROR ? "No se pudo vincular"
           : s->state == FLEX_ACCOUNT_EXPIRED ? "El codigo expiro"
           : s->link == FLEX_LINK_ERROR ? "No se pudo leer la cuenta"
                                         : "Una cuenta para todo FlexOS";
}

void flex_account_settings_text_for(const flex_account_snapshot_t *s, char *out, size_t n)
{
    if (s->state == FLEX_ACCOUNT_REQUESTING || s->state == FLEX_ACCOUNT_CODE_READY) {
        snprintf(out, n, "Vinculacion en curso");
    } else if (s->linked && s->flex_address[0]) {
        // Vinculada: la direccion SIEMPRE, y el matiz solo si lo hay. Sin red no se dice "sin cuenta": se dice que no hay conexion
        if (s->link == FLEX_LINK_LINKED) {
            snprintf(out, n, "%s", s->flex_address);
        } else if (s->link == FLEX_LINK_AUTH_REQUIRED || s->link == FLEX_LINK_TOKEN_EXPIRED) {
            snprintf(out, n, "%s", flex_account_link_label(s->link));
        } else {
            snprintf(out, n, "%s \xC2\xB7 sin conexi\xC3\xB3n", s->flex_address);
        }
    } else if (s->link == FLEX_LINK_ERROR) {
        snprintf(out, n, "%s", flex_account_link_label(s->link));
    } else {
        snprintf(out, n, "Sin cuenta vinculada");
    }
}

void flex_account_ask_geom(int *x, int *y, int *w, int *h)
{
    *w = FLEX_ACC_W - 64;
    *h = 318;
    *x = 32;
    *y = 236;
}

void flex_account_ask_btns(int *cx, int *dx, int *by, int *bw, int *bh)
{
    int x, y, w, h;
    flex_account_ask_geom(&x, &y, &w, &h);
    *bw = (w - 60) / 2;
    *bh = 52;
    *by = y + h - *bh - 20;
    *cx = x + 20;
    *dx = x + 40 + *bw;
}

bool flex_account_finish_cancels(flex_account_state_t now) { return now == FLEX_ACCOUNT_REQUESTING || now == FLEX_ACCOUNT_CODE_READY; }

flex_acc_action_t flex_account_hit(flex_account_state_t built, bool ask_open, bool first_boot, bool relink, bool online, int x, int y)
{
    // La confirmacion de "Desvincular cuenta" manda: cualquier toque es suyo
    if (ask_open) {
        int cx, dx, by, bw, bh;
        flex_account_ask_btns(&cx, &dx, &by, &bw, &bh);
        if (y >= by && y <= by + bh && x >= dx && x <= dx + bw) {
            return FLEX_ACC_UNLINK_NOW;
        }
        return FLEX_ACC_ASK_DISMISS;
    }
    // Barra inferior / flecha: omite solamente la vinculacion, nunca la configuracion basica
    if (y >= 670 || (!first_boot && x < 64 && y < 78)) {
        return FLEX_ACC_FINISH;
    }
    if (built == FLEX_ACCOUNT_LINKED) {
        if (y >= FLEX_ACC_UNLINK_Y - 4 && y <= FLEX_ACC_UNLINK_Y + FLEX_ACC_UNLINK_H + 2 && abs(x - FLEX_ACC_W / 2) <= 124) {
            return FLEX_ACC_ASK_OPEN;   // primero se pregunta; nada se borra todavia
        }
        if (y >= 530 && y <= 622) {
            if (relink) {
                // Volver a vincular CONSERVA la cuenta guardada hasta que la nueva credencial este aprobada y guardada
                return online ? FLEX_ACC_REQUEST_CODE : FLEX_ACC_WIFI;
            }
            return FLEX_ACC_FINISH;
        }
        return FLEX_ACC_NONE;
    }
    if (built == FLEX_ACCOUNT_REQUESTING || built == FLEX_ACCOUNT_CODE_READY) {
        return (y >= 520 && y <= 622) ? FLEX_ACC_CANCEL_LINK : FLEX_ACC_NONE;
    }
    // Las dos franjas siguen EXACTAMENTE a los dos botones dibujados (452..510 y 526..580), sin solaparse entre si ni con la barra inferior
    if ((y >= 444 && y <= 518) || (y >= 520 && y <= 588)) {
        return online ? FLEX_ACC_REQUEST_CODE : FLEX_ACC_WIFI;
    }
    return FLEX_ACC_NONE;
}
