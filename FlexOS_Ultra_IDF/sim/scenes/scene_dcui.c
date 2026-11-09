// Device Care · el pintor (flex_dc_ui.c): cada tipo de operacion de dibujo sale en LVGL donde y como dice la operacion. Una pagina hecha a mano con una
// operacion de cada tipo se pinta sobre un fondo propio y se leen los pixeles del panel: color de cada rol de la paleta, relleno, contorno, disco,
// circulo, anillo (radio exterior y grosor), arco (radio de la LINEA CENTRAL, extremos redondos, vuelta entera, barrido cero), trazo con extremos
// redondos, triangulo y los cuatro textos (izquierda, centrado, derecha, recortado). Y las reglas de actualizacion: solo vectores o solo el
// contenido de un texto se cambian en su sitio; un cambio de estructura obliga a reconstruir.
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "flex_dc_screens.h"
#include "flex_dc_ui.h"
#include "flex_frame.h"
#include "flex_theme.h"
#include "lvgl.h"
#include "ui_sim.h"

uint16_t *sim_fb(void);

#define CHK(c)                                                       \
    do {                                                             \
        if (!(c)) {                                                  \
            printf("dcui: FALLO linea %d: %s\n", __LINE__, #c);      \
            ok = false;                                              \
        }                                                            \
    } while (0)

static flex_dc_page_t P;
#define MAGENTA 0xF81F

static uint16_t px(int x, int y) { return sim_fb()[(size_t)y * 480 + (size_t)x]; }
static uint16_t c16(lv_color_t c) { return lv_color_to_u16(c); }

static flex_dc_op_t *add(int kind, int role)
{
    flex_dc_op_t *o = &P.op[P.n++];
    memset(o, 0, sizeof(*o));
    o->kind = (uint8_t)kind;
    o->role = (uint8_t)role;
    return o;
}
static void rect(int kind, int x, int y, int w, int h, int r, int role)
{
    flex_dc_op_t *o = add(kind, role);
    o->x = (int16_t)x;
    o->y = (int16_t)y;
    o->w = (int16_t)w;
    o->h = (int16_t)h;
    o->r = (int16_t)r;
}
static void text(int kind, int x, int y, int size, int role, const char *s, int mr)
{
    flex_dc_op_t *o = add(kind, role);
    o->x = (int16_t)x;
    o->y = (int16_t)y;
    o->size = (uint8_t)size;
    o->mr = (int16_t)mr;
    snprintf(o->text, sizeof(o->text), "%s", s);
}

static lv_obj_t *find_label(lv_obj_t *o, const char *txt)
{
    if (lv_obj_check_type(o, &lv_label_class) && !strcmp(lv_label_get_text(o), txt)) {
        return o;
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) {
        lv_obj_t *r = find_label(lv_obj_get_child(o, (int32_t)i), txt);
        if (r) {
            return r;
        }
    }
    return NULL;
}

static lv_obj_t *make_host(void)
{
    lv_obj_t *h = flex_box(lv_layer_top());
    lv_obj_set_pos(h, 0, 0);
    lv_obj_set_size(h, 480, 800);
    lv_obj_set_style_bg_color(h, lv_color_hex(0xFF00FF), 0);
    lv_obj_set_style_bg_opa(h, LV_OPA_COVER, 0);
    return h;
}

bool scene_dcui_run(void)
{
    bool ok = true;
    const flex_palette_t *t = flex_th();
    lv_obj_t *host = make_host();

    // ---- 1) cada rol sale con el color de su ficha de la paleta (tabla escrita aqui aparte) -----------------------------------------------------
    {
        lv_color_t exp[DCC_N];
        exp[DCC_TXT] = t->txt;
        exp[DCC_TXT2] = t->txt2;
        exp[DCC_MUTE] = t->mute;
        exp[DCC_OK] = t->ok;
        exp[DCC_WARN] = t->warn;
        exp[DCC_ERR] = t->err;
        exp[DCC_PRIM] = t->primary;
        exp[DCC_ONACC] = t->on_acc;
        exp[DCC_ACCS] = t->acc_soft;
        exp[DCC_TRACK] = t->track;
        exp[DCC_BORDER] = t->border;
        exp[DCC_SURF] = t->surf;
        exp[DCC_SURF2] = t->surf2;
        exp[DCC_DIV] = t->divider;
        exp[DCC_DANGER] = t->danger;
        exp[DCC_ACCENT] = flex_accent();
        exp[DCC_WINBG] = t->win;
        P.n = P.nhit = 0;
        for (int r = 0; r < DCC_N; r++) {
            rect(DCO_RRECT, 10 + 22 * r, 10, 16, 16, 0, r);
        }
        lv_obj_t *v = flex_dc_ui_render(host, &P);
        sim_run(60);
        for (int r = 0; r < DCC_N; r++) {
            CHK(px(18 + 22 * r, 18) == c16(exp[r]));
            CHK(c16(flex_dc_ui_color(r)) == c16(exp[r]));
        }
        lv_obj_delete(v);
    }

    // ---- 1b) con una paleta de fondo puesta el acento NO es el primario: el rol sigue al acento del sistema ----------------------------------------
    {
        bool prev = flex_look()->wall_pal;
        flex_theme_set_wall_accent(lv_color_to_u16(lv_color_make(200, 40, 120)), lv_color_to_u16(lv_color_make(220, 120, 180)));
        flex_theme_set_wall_palette(true);
        sim_run(60);
        CHK(c16(flex_accent()) != c16(t->primary));
        P.n = P.nhit = 0;
        rect(DCO_RRECT, 10, 10, 16, 16, 0, DCC_ACCENT);
        lv_obj_t *va = flex_dc_ui_render(host, &P);
        sim_run(60);
        CHK(px(18, 18) == c16(flex_accent()) && px(18, 18) != c16(t->primary));
        lv_obj_delete(va);
        flex_theme_set_wall_palette(prev);
        sim_run(60);
    }

    // ---- 2) formas ---------------------------------------------------------------------------------------------------------------------------------
    P.n = P.nhit = 0;
    rect(DCO_RECT, 10, 60, 50, 40, 0, DCC_OK);                 // relleno
    rect(DCO_RECT, 10, 110, 50, 40, 0, DCC_WINBG);            // el fondo de pagina NO se pinta: se ve el del contenedor
    rect(DCO_RRECT, 10, 160, 60, 60, 24, DCC_PRIM);            // esquinas redondeadas
    rect(DCO_OUTLINE, 100, 60, 80, 50, 12, DCC_ERR);           // contorno de un pixel
    {
        flex_dc_op_t *o = add(DCO_DISC, DCC_WARN);              // disco
        o->x = 240; o->y = 100; o->r = 20;
        o = add(DCO_CIRCLE, DCC_ERR);                           // circulo
        o->x = 320; o->y = 100; o->r = 20;
        o = add(DCO_RING, DCC_OK);                              // anillo: radio EXTERIOR 40, grosor 8
        o->x = 100; o->y = 250; o->r = 40; o->th = 8;
        o = add(DCO_ARC, DCC_PRIM);                             // cuarto de vuelta de arriba a la derecha; radio de la linea central 30, grosor 8
        o->f[0] = 240; o->f[1] = 250; o->f[2] = 30; o->f[3] = -90; o->f[4] = 0; o->th = 8;
        o = add(DCO_ARC, DCC_WARN);                             // vuelta entera
        o->f[0] = 380; o->f[1] = 250; o->f[2] = 30; o->f[3] = -90; o->f[4] = 270; o->th = 8;
        o = add(DCO_ARC, DCC_ERR);                              // barrido cero: no pinta nada
        o->f[0] = 100; o->f[1] = 350; o->f[2] = 30; o->f[3] = 40; o->f[4] = 40; o->th = 8;
        o = add(DCO_STROKE, DCC_OK);                            // trazo con extremos redondos de radio 3
        o->f[0] = 200; o->f[1] = 350; o->f[2] = 260; o->f[3] = 350; o->f[4] = 3;
        o = add(DCO_TRI, DCC_PRIM);
        o->p[0] = 300; o->p[1] = 400; o->p[2] = 380; o->p[3] = 400; o->p[4] = 340; o->p[5] = 340;
        o = add(DCO_SURF, DCC_SURF2);                           // superficie elevada (UIS_ELEVATED) y de tarjeta (UIS_CARD)
        o->x = 10; o->y = 700; o->w = 100; o->h = 60; o->r = 10; o->surf = 1;
        o = add(DCO_SURF, DCC_SURF);
        o->x = 130; o->y = 700; o->w = 100; o->h = 60; o->r = 10; o->surf = 0;
    }
    lv_obj_t *v = flex_dc_ui_render(host, &P);
    sim_run(60);
    CHK(px(30, 80) == c16(t->ok));
    CHK(px(30, 130) == MAGENTA && px(11, 111) == MAGENTA);                      // el RECT de rol WINBG se salta
    CHK(px(40, 190) == c16(t->primary) && px(11, 161) != c16(t->primary));      // la esquina redondeada no esta rellena
    CHK(px(100, 80) == c16(t->err) && px(179, 80) == c16(t->err) && px(140, 60) == c16(t->err) && px(140, 109) == c16(t->err));   // el contorno
    CHK(px(140, 85) == MAGENTA && px(101, 80) == MAGENTA && px(140, 61) == MAGENTA && px(178, 80) == MAGENTA);   // y dentro no hay relleno: el contorno es de UN pixel
    CHK(px(240, 100) == c16(t->warn) && px(255, 100) == c16(t->warn) && px(262, 100) == MAGENTA);   // disco r = 20
    CHK(px(340, 100) == c16(t->err) && px(320, 100) == MAGENTA && px(330, 100) == MAGENTA);          // circulo: solo el borde
    CHK(px(100 + 36, 250) == c16(t->ok) && px(100 + 38, 250) == c16(t->ok) && px(100, 250) == MAGENTA && px(100 + 30, 250) == MAGENTA && px(100 + 45, 250) == MAGENTA);
    {
        float a = -45.0f * 3.14159265f / 180.0f;
        CHK(px(240 + (int)lroundf(30 * cosf(a)), 250 + (int)lroundf(30 * sinf(a))) == c16(t->primary));          // en medio del cuarto
        a = 135.0f * 3.14159265f / 180.0f;
        CHK(px(240 + (int)lroundf(30 * cosf(a)), 250 + (int)lroundf(30 * sinf(a))) == MAGENTA);                  // al otro lado: nada
        CHK(px(240, 250 - 30) == c16(t->primary) && px(240 + 30, 250) == c16(t->primary));                       // el principio y el final
        CHK(px(240, 250) == MAGENTA && px(240, 250 - 22) == MAGENTA && px(240, 250 - 38) == MAGENTA);             // el grosor es 8 alrededor de la linea central
        a = -110.0f * 3.14159265f / 180.0f;
        CHK(px(240 + (int)lroundf(30 * cosf(a)), 250 + (int)lroundf(30 * sinf(a))) == MAGENTA);                  // pasado el extremo redondo, nada
        CHK(px(380 + 30, 250) == c16(t->warn) && px(380 - 30, 250) == c16(t->warn) && px(380, 250 + 30) == c16(t->warn) && px(380, 250) == MAGENTA);   // vuelta entera
        CHK(px(100 + 30, 350) == MAGENTA && px(100 - 30, 350) == MAGENTA && px(100, 350 + 30) == MAGENTA);        // barrido cero
        a = 40.0f * 3.14159265f / 180.0f;
        CHK(px(100 + (int)lroundf(30 * cosf(a)), 350 + (int)lroundf(30 * sinf(a))) == MAGENTA);                   // ni un punto de los extremos redondos
    }
    CHK(px(230, 350) == c16(t->ok) && px(230, 356) == MAGENTA && px(198, 350) == c16(t->ok) && px(250, 350) == c16(t->ok));   // el trazo y su extremo redondo
    CHK(px(340, 380) == c16(t->primary) && px(310, 340) == MAGENTA && px(300, 410) == MAGENTA);                // el triangulo
    CHK(px(60, 730) == c16(t->surf2) && px(180, 730) == c16(t->surf));
    lv_obj_delete(v);

    // ---- 3) textos: izquierda, centrado, derecha (borde derecho en x) y recortado ---------------------------------------------------------------------
    P.n = P.nhit = 0;
    text(DCO_TEXT, 30, 500, 2, DCC_TXT, "izquierda", 0);
    text(DCO_TEXT_C, 240, 540, 2, DCC_TXT2, "centrado", 0);
    text(DCO_TEXT_R, 400, 580, 2, DCC_MUTE, "derecha", 0);
    text(DCO_TEXT_CLIP, 30, 620, 2, DCC_OK, "texto muy largo que no cabe en la caja", 150);
    v = flex_dc_ui_render(host, &P);
    sim_run(60);
    lv_obj_t *l1 = find_label(v, "izquierda"), *l2 = find_label(v, "centrado"), *l3 = find_label(v, "derecha"), *l4 = find_label(v, "texto muy largo que no cabe en la caja");
    CHK(l1 && l2 && l3 && l4);
    if (l1 && l2 && l3 && l4) {
        lv_area_t a1, a2, a3, a4;
        lv_obj_get_coords(l1, &a1);
        lv_obj_get_coords(l2, &a2);
        lv_obj_get_coords(l3, &a3);
        lv_obj_get_coords(l4, &a4);
        CHK(a1.x1 == 30);
        CHK(a2.x1 + (a2.x2 - a2.x1) / 2 >= 238 && a2.x1 + (a2.x2 - a2.x1) / 2 <= 242);   // centrado en x
        CHK(a3.x2 >= 399 && a3.x2 <= 401);                                                  // el borde derecho del texto en x
        CHK(a4.x1 == 30 && a4.x2 - a4.x1 + 1 == 120);                                       // recortado en mr = 150, sin puntos suspensivos
        CHK(lv_label_get_long_mode(l4) == LV_LABEL_LONG_MODE_CLIP);
        CHK(lv_obj_get_style_text_color(l1, 0).red == t->txt.red && lv_obj_get_style_text_color(l3, 0).blue == t->mute.blue);
    }
    // el texto se pinta de verdad en su color (algun pixel del rectangulo del primer texto es el del rol)
    {
        bool seen = false;
        for (int y = 495; y < 525 && !seen; y++) {
            for (int x = 30; x < 110 && !seen; x++) {
                seen = px(x, y) == c16(t->txt);
            }
        }
        CHK(seen);
    }
    lv_obj_delete(v);

    // ---- 4) actualizar: solo vectores o solo contenidos de texto se cambian en su sitio; la estructura obliga a reconstruir -------------------------------
    P.n = P.nhit = 0;
    rect(DCO_RRECT, 20, 100, 80, 40, 0, DCC_OK);
    text(DCO_TEXT_C, 240, 200, 3, DCC_TXT, "42", 0);
    text(DCO_TEXT_R, 400, 260, 2, DCC_MUTE, "8%", 0);
    {
        flex_dc_op_t *o = add(DCO_SURF, DCC_SURF);
        o->x = 20; o->y = 400; o->w = 200; o->h = 100; o->r = 20;
    }
    int b0 = flex_dc_ui_builds(), u0 = flex_dc_ui_vec_updates();
    v = flex_dc_ui_render(host, &P);
    sim_run(60);
    CHK(flex_dc_ui_builds() == b0 + 1);
    CHK(px(40, 120) == c16(t->ok));
    CHK(px(120, 450) == c16(t->surf));   // la superficie del sistema (tarjeta): en "Plano", el color de tarjeta del tema
    // otro dibujo, mismos textos: en su sitio
    P.op[0].role = DCC_ERR;
    P.op[0].w = 100;
    CHK(flex_dc_ui_update(v, &P));
    sim_run(60);
    CHK(flex_dc_ui_builds() == b0 + 1 && flex_dc_ui_vec_updates() == u0 + 1);
    CHK(px(40, 120) == c16(t->err) && px(115, 120) == c16(t->err));
    // otro contenido de texto: la misma etiqueta cambia (y la de la derecha se recoloca para que su borde siga en x)
    lv_obj_t *n42 = find_label(v, "42"), *r8 = find_label(v, "8%");
    CHK(n42 && r8);
    snprintf(P.op[1].text, sizeof(P.op[1].text), "%s", "73");
    snprintf(P.op[2].text, sizeof(P.op[2].text), "%s", "100%");
    CHK(flex_dc_ui_update(v, &P));
    sim_run(60);
    CHK(flex_dc_ui_builds() == b0 + 1 && flex_dc_ui_vec_updates() == u0 + 2);
    CHK(find_label(v, "73") == n42 && !find_label(v, "42"));
    {
        lv_obj_t *r100 = find_label(v, "100%");
        CHK(r100 == r8);
        lv_area_t a;
        lv_obj_get_coords(r100, &a);
        CHK(a.x2 >= 399 && a.x2 <= 401);
    }
    // cambia la estructura (posicion, rol de un texto, un texto mas, una superficie): reconstruir
    P.op[1].x = 241;
    CHK(!flex_dc_ui_update(v, &P));
    P.op[1].x = 240;
    P.op[1].role = DCC_OK;
    CHK(!flex_dc_ui_update(v, &P));
    P.op[1].role = DCC_TXT;
    P.op[3].w = 201;
    CHK(!flex_dc_ui_update(v, &P));
    P.op[3].w = 200;
    CHK(flex_dc_ui_update(v, &P));   // y de vuelta a lo de antes, vale
    text(DCO_TEXT, 30, 300, 2, DCC_TXT, "uno mas", 0);
    CHK(!flex_dc_ui_update(v, &P));
    P.n--;
    // sin vectores, y otra vez con vectores: el objeto de dibujo aparece / desaparece
    P.op[0] = P.op[3];   // el rectangulo pasa a ser otra superficie
    CHK(!flex_dc_ui_update(v, &P));
    CHK(!flex_dc_ui_update(NULL, &P));
    lv_obj_delete(v);
    // la misma estructura de textos con un vector de menos / de mas: el objeto de dibujo aparece o desaparece, se reconstruye
    P.n = P.nhit = 0;
    text(DCO_TEXT, 30, 300, 2, DCC_TXT, "fijo", 0);
    rect(DCO_RRECT, 20, 100, 80, 40, 0, DCC_OK);
    v = flex_dc_ui_render(host, &P);
    P.n = 1;                                   // se quita el vector: quedan los mismos textos
    CHK(!flex_dc_ui_update(v, &P));
    lv_obj_delete(v);
    v = flex_dc_ui_render(host, &P);           // pagina sin vectores
    rect(DCO_RRECT, 20, 100, 80, 40, 0, DCC_OK);
    CHK(!flex_dc_ui_update(v, &P));            // y ahora con uno: no hay donde pintarlo
    lv_obj_delete(v);

    // ---- 5) alto propio: la lista del historial se desplaza ---------------------------------------------------------------------------------------------
    P.n = P.nhit = 0;
    rect(DCO_RRECT, 20, 1300, 60, 40, 0, DCC_PRIM);
    v = flex_dc_ui_render_h(host, &P, 1400);
    lv_obj_update_layout(v);
    CHK(lv_obj_get_height(v) == 1400 && lv_obj_get_width(v) == 480);
    CHK(lv_obj_get_child_count(v) == 1 && lv_obj_get_height(lv_obj_get_child(v, 0)) == 1400);   // y el dibujo cubre todo el alto
    lv_obj_delete(v);

    lv_obj_delete(host);
    sim_run(60);
    return ok;
}
