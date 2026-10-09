#include "flex_icons.h"

#include <math.h>
#include <stdlib.h>
#include "flex_theme.h"

#define RGB(r, g, b) lv_color_make((r), (g), (b))
#define DEG 0.0174532925f

// ---- primitivas sobre una capa (equivalentes a las de la version Arduino) ----
typedef struct {
    lv_layer_t *L;
    lv_opa_t opa;
} pen_t;

static void p_rect(pen_t *p, float x, float y, float w, float h, int32_t r, lv_color_t c, lv_opa_t a)
{
    if (w <= 0 || h <= 0) {
        return;
    }
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = c;
    d.bg_opa = (lv_opa_t)((uint32_t)a * p->opa / 255);
    d.radius = r;
    lv_area_t ar = {(int32_t)x, (int32_t)y, (int32_t)x + (int32_t)w - 1, (int32_t)y + (int32_t)h - 1};
    lv_draw_rect(p->L, &d, &ar);
}

static void p_circle(pen_t *p, float cx, float cy, int32_t r, lv_color_t c, lv_opa_t a)
{
    if (r < 0) {
        return;
    }
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = c;
    d.bg_opa = (lv_opa_t)((uint32_t)a * p->opa / 255);
    d.radius = LV_RADIUS_CIRCLE;
    int32_t x = (int32_t)cx, y = (int32_t)cy;
    lv_area_t ar = {x - r, y - r, x + r, y + r};
    lv_draw_rect(p->L, &d, &ar);
}

static void p_ring(pen_t *p, float cx, float cy, int32_t r_out, int32_t t, lv_color_t c)
{
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_opa = LV_OPA_TRANSP;
    d.border_color = c;
    d.border_width = t;
    d.border_opa = p->opa;
    d.radius = LV_RADIUS_CIRCLE;
    int32_t x = (int32_t)cx, y = (int32_t)cy;
    lv_area_t ar = {x - r_out, y - r_out, x + r_out, y + r_out};
    lv_draw_rect(p->L, &d, &ar);
}

static void p_rrect_outline(pen_t *p, float x, float y, float w, float h, int32_t r, lv_color_t c)
{
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_opa = LV_OPA_TRANSP;
    d.border_color = c;
    d.border_width = 1;
    d.border_opa = p->opa;
    d.radius = r;
    lv_area_t ar = {(int32_t)x, (int32_t)y, (int32_t)(x + w) - 1, (int32_t)(y + h) - 1};
    lv_draw_rect(p->L, &d, &ar);
}

// strokeSeg(x0,y0,x1,y1,rad): discos de radio rad a lo largo -> trazo de 2*rad+1
static void p_seg(pen_t *p, float x0, float y0, float x1, float y1, int32_t rad, lv_color_t c)
{
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.color = c;
    d.opa = p->opa;
    d.width = 2 * rad + 1;
    d.round_start = 1;
    d.round_end = 1;
    d.p1.x = (lv_value_precise_t)(x0 + 0.5f);
    d.p1.y = (lv_value_precise_t)(y0 + 0.5f);
    d.p2.x = (lv_value_precise_t)(x1 + 0.5f);
    d.p2.y = (lv_value_precise_t)(y1 + 0.5f);
    lv_draw_line(p->L, &d);
}

// Arco en el sentido de LVGL: de start a end en sentido horario (0 = derecha).
static void p_arc_cw(pen_t *p, float cx, float cy, float r, float start, float end, int32_t thick, lv_color_t c)
{
    int32_t rad = thick / 2 < 1 ? 1 : thick / 2;
    lv_draw_arc_dsc_t d;
    lv_draw_arc_dsc_init(&d);
    d.color = c;
    d.opa = p->opa;
    d.width = 2 * rad + 1;
    d.rounded = 1;
    d.center.x = (int32_t)(cx + 0.5f);
    d.center.y = (int32_t)(cy + 0.5f);
    d.radius = (uint16_t)(r + rad + 0.5f);
    d.start_angle = (lv_value_precise_t)start;
    d.end_angle = (lv_value_precise_t)end;
    lv_draw_arc(p->L, &d);
}

// arcStroke de Arduino: recorre de a0 a a1 (grados, 0 = derecha, sentido
// horario en pantalla), es decir el intervalo [min, max] sea cual sea el orden.
static void p_arc(pen_t *p, float cx, float cy, float r, float a0, float a1, int32_t thick, lv_color_t c)
{
    float lo = a0 < a1 ? a0 : a1, hi = a0 < a1 ? a1 : a0;
    while (lo < 0) {
        lo += 360;
        hi += 360;
    }
    if (hi - lo >= 360.0f) {
        p_arc_cw(p, cx, cy, r, 0, 360, thick, c);
    } else {
        p_arc_cw(p, cx, cy, r, fmodf(lo, 360.0f), fmodf(hi, 360.0f), thick, c);
    }
}

static void p_tri(pen_t *p, float x0, float y0, float x1, float y1, float x2, float y2, lv_color_t c)
{
    lv_draw_triangle_dsc_t d;
    lv_draw_triangle_dsc_init(&d);
    d.color = c;
    d.opa = p->opa;
    d.p[0].x = (lv_value_precise_t)x0;
    d.p[0].y = (lv_value_precise_t)y0;
    d.p[1].x = (lv_value_precise_t)x1;
    d.p[1].y = (lv_value_precise_t)y1;
    d.p[2].x = (lv_value_precise_t)x2;
    d.p[2].y = (lv_value_precise_t)y2;
    lv_draw_triangle(p->L, &d);
}

// ---- iconos de app (FlexOS_Ultra_Icons.h:88-336) -----------------------------
static const uint8_t k_brand[FLEX_APP_N][3] = {
    {245, 245, 247}, {255, 255, 255}, {27, 95, 217},  {59, 123, 217}, {30, 58, 110},  {232, 167, 90},
    {46, 155, 230},  {16, 42, 96},    {241, 231, 210}, {142, 30, 30},  {138, 143, 152}, {58, 58, 60},
    {255, 255, 255}, {74, 74, 78},    {48, 124, 226},  {105, 91, 230}, {38, 132, 255},  {20, 132, 122},
    {236, 76, 108},
};

lv_color_t flex_app_icon_color(int id)
{
    if (id < 0 || id >= FLEX_APP_N) {
        return RGB(90, 90, 96);
    }
    return RGB(k_brand[id][0], k_brand[id][1], k_brand[id][2]);
}

static void draw_glyph(pen_t *p, int id, float x, float y, int32_t S)
{
    const lv_color_t WHITE = lv_color_white();
    float cx = x + S / 2, cy = y + S / 2;
    int32_t tk = S / 12 < 2 ? 2 : S / 12;
    switch (id) {
    case IC_RELOJ: {
        lv_color_t g = RGB(70, 70, 74);
        p_ring(p, cx, cy, (int32_t)(S * 0.36f), 2, g);
        p_rect(p, cx - 1, y + (int32_t)(S * 0.16f), 2, S / 12, 0, g, 255);
        p_rect(p, cx - 1, y + S - (int32_t)(S * 0.16f) - S / 12, 2, S / 12, 0, g, 255);
        p_rect(p, x + (int32_t)(S * 0.16f), cy - 1, S / 12, 2, 0, g, 255);
        p_rect(p, x + S - (int32_t)(S * 0.16f) - S / 12, cy - 1, S / 12, 2, 0, g, 255);
        p_seg(p, cx, cy, cx - S * 0.14f, cy - S * 0.10f, tk / 2 + 1, RGB(30, 30, 30));
        p_seg(p, cx, cy, cx + S * 0.12f, cy - S * 0.20f, tk / 2, RGB(245, 140, 30));
        p_circle(p, cx, cy, tk / 2 + 1, RGB(30, 30, 30), 255);
        break;
    }
    case IC_GALERIA: {
        static const uint8_t cols[8][3] = {{233, 64, 64},  {240, 150, 40}, {240, 210, 50}, {90, 200, 90},
                                           {50, 190, 190}, {60, 120, 235}, {120, 80, 220}, {220, 80, 200}};
        float d = S * 0.17f;
        int32_t pr = (int32_t)(S * 0.135f);
        for (int k = 0; k < 8; k++) {
            float a = k * 45 * DEG;
            p_circle(p, (int32_t)(cx + d * cosf(a)), (int32_t)(cy + d * sinf(a)), pr,
                     RGB(cols[k][0], cols[k][1], cols[k][2]), 255);
        }
        p_circle(p, cx, cy, (int32_t)(S * 0.11f), WHITE, 255);
        break;
    }
    case IC_MULTIMEDIA:
        p_tri(p, cx - (int32_t)(S * 0.12f), cy - (int32_t)(S * 0.18f), cx - (int32_t)(S * 0.12f),
              cy + (int32_t)(S * 0.18f), cx + (int32_t)(S * 0.22f), cy, WHITE);
        break;
    case IC_ALMACEN: {
        lv_color_t fol = RGB(225, 236, 250), cl = RGB(205, 222, 245);
        p_rect(p, x + (int32_t)(S * 0.20f), y + (int32_t)(S * 0.28f), (int32_t)(S * 0.30f), (int32_t)(S * 0.12f), 3,
               fol, 255);
        p_rect(p, x + (int32_t)(S * 0.18f), y + (int32_t)(S * 0.36f), (int32_t)(S * 0.64f), (int32_t)(S * 0.34f), 5,
               fol, 255);
        p_circle(p, x + (int32_t)(S * 0.60f), y + (int32_t)(S * 0.34f), (int32_t)(S * 0.10f), cl, 255);
        p_circle(p, x + (int32_t)(S * 0.72f), y + (int32_t)(S * 0.36f), (int32_t)(S * 0.08f), cl, 255);
        p_rect(p, x + (int32_t)(S * 0.58f), y + (int32_t)(S * 0.36f), (int32_t)(S * 0.18f), (int32_t)(S * 0.07f), 0, cl,
               255);
        break;
    }
    case IC_MODOPC:
        p_rect(p, x + (int32_t)(S * 0.16f), y + (int32_t)(S * 0.24f), (int32_t)(S * 0.68f), (int32_t)(S * 0.40f), 4,
               RGB(235, 240, 250), 255);
        p_rect(p, x + (int32_t)(S * 0.22f), y + (int32_t)(S * 0.30f), (int32_t)(S * 0.56f), (int32_t)(S * 0.28f), 0,
               RGB(45, 95, 205), 255);
        p_rect(p, cx - (int32_t)(S * 0.05f), y + (int32_t)(S * 0.64f), (int32_t)(S * 0.10f), (int32_t)(S * 0.08f), 0,
               RGB(200, 210, 225), 255);
        p_rect(p, cx - (int32_t)(S * 0.16f), y + (int32_t)(S * 0.72f), (int32_t)(S * 0.32f), (int32_t)(S * 0.06f), 2,
               RGB(200, 210, 225), 255);
        break;
    case IC_NOTAS:
        p_rect(p, x + (int32_t)(S * 0.22f), y + (int32_t)(S * 0.18f), (int32_t)(S * 0.48f), (int32_t)(S * 0.62f), 5,
               WHITE, 255);
        for (int i = 0; i < 3; i++) {
            p_rect(p, x + (int32_t)(S * 0.30f), y + (int32_t)(S * 0.30f) + i * (int32_t)(S * 0.12f),
                   (int32_t)(S * 0.32f), 2, 0, RGB(180, 180, 185), 255);
        }
        p_seg(p, x + S * 0.56f, y + S * 0.66f, x + S * 0.78f, y + S * 0.40f, tk / 2 + 1, RGB(120, 90, 40));
        p_circle(p, (int32_t)(x + S * 0.78f), (int32_t)(y + S * 0.40f), tk / 2 + 1, RGB(245, 210, 90), 255);
        break;
    case IC_NAV:
        p_ring(p, cx, cy, (int32_t)(S * 0.30f), 2, WHITE);
        p_rect(p, cx, cy - (int32_t)(S * 0.30f), 1, (int32_t)(S * 0.60f), 0, WHITE, 255);
        p_rect(p, cx - (int32_t)(S * 0.30f), cy, (int32_t)(S * 0.60f), 1, 0, WHITE, 255);
        p_arc(p, cx, cy, S * 0.18f, 90, 270, 2, WHITE);
        p_arc(p, cx, cy, S * 0.18f, -90, 90, 2, WHITE);
        break;
    case IC_PAINT:
        p_circle(p, cx - (int32_t)(S * 0.04f), cy + (int32_t)(S * 0.02f), (int32_t)(S * 0.27f), RGB(236, 226, 205),
                 255);
        p_circle(p, cx + (int32_t)(S * 0.11f), cy + (int32_t)(S * 0.11f), (int32_t)(S * 0.06f), RGB(241, 231, 210),
                 255);
        p_circle(p, cx - (int32_t)(S * 0.14f), cy - (int32_t)(S * 0.06f), (int32_t)(S * 0.045f), RGB(230, 70, 70), 255);
        p_circle(p, cx - (int32_t)(S * 0.02f), cy - (int32_t)(S * 0.13f), (int32_t)(S * 0.045f), RGB(240, 200, 60),
                 255);
        p_circle(p, cx + (int32_t)(S * 0.10f), cy - (int32_t)(S * 0.07f), (int32_t)(S * 0.045f), RGB(70, 130, 235),
                 255);
        p_circle(p, cx - (int32_t)(S * 0.16f), cy + (int32_t)(S * 0.09f), (int32_t)(S * 0.045f), RGB(80, 190, 90), 255);
        p_seg(p, cx + S * 0.02f, cy - S * 0.16f, cx + S * 0.26f, cy - S * 0.30f, tk / 2 + 1, RGB(140, 100, 60));
        break;
    case IC_JUEGOS:
        p_rrect_outline(p, x + (int32_t)(S * 0.14f), y + (int32_t)(S * 0.34f), (int32_t)(S * 0.72f),
                        (int32_t)(S * 0.30f), (int32_t)(S * 0.13f), WHITE);
        p_rrect_outline(p, x + (int32_t)(S * 0.14f) + 1, y + (int32_t)(S * 0.34f) + 1, (int32_t)(S * 0.72f) - 2,
                        (int32_t)(S * 0.30f) - 2, (int32_t)(S * 0.12f), WHITE);
        p_rect(p, cx - (int32_t)(S * 0.22f) - 1, cy + (int32_t)(S * 0.02f), (int32_t)(S * 0.12f), 3, 0, WHITE, 255);
        p_rect(p, cx - (int32_t)(S * 0.16f) - 1, cy - (int32_t)(S * 0.04f), 3, (int32_t)(S * 0.12f), 0, WHITE, 255);
        p_circle(p, cx + (int32_t)(S * 0.14f), cy - (int32_t)(S * 0.01f), 3, WHITE, 255);
        p_circle(p, cx + (int32_t)(S * 0.22f), cy + (int32_t)(S * 0.05f), 3, WHITE, 255);
        break;
    case IC_AJUSTES: {
        lv_color_t g = RGB(70, 74, 84);
        p_circle(p, cx, cy, (int32_t)(S * 0.26f), g, 255);
        for (int k = 0; k < 8; k++) {
            float a = k * 45 * DEG;
            p_circle(p, (int32_t)(cx + S * 0.30f * cosf(a)), (int32_t)(cy + S * 0.30f * sinf(a)),
                     (int32_t)(S * 0.075f), g, 255);
        }
        // hueco central: en LVGL se pinta del color de la base (en Plano coincide;
        // en Vidrio se ve el color de marca, que es lo que pretendia el original)
        p_circle(p, cx, cy, (int32_t)(S * 0.10f), RGB(138, 143, 152), 255);
        break;
    }
    case IC_CALC:
        p_rect(p, x + (int32_t)(S * 0.18f), y + (int32_t)(S * 0.16f), (int32_t)(S * 0.64f), (int32_t)(S * 0.16f), 3,
               RGB(210, 210, 215), 255);
        for (int rr = 0; rr < 3; rr++) {
            for (int c = 0; c < 4; c++) {
                lv_color_t bc = c == 3 ? RGB(245, 150, 30) : RGB(150, 150, 155);
                p_rect(p, x + (int32_t)(S * 0.18f) + c * (int32_t)(S * 0.17f),
                       y + (int32_t)(S * 0.40f) + rr * (int32_t)(S * 0.15f), (int32_t)(S * 0.12f),
                       (int32_t)(S * 0.10f), 2, bc, 255);
            }
        }
        break;
    case IC_CALEND: {
        p_rect(p, x + (int32_t)(S * 0.16f), y + (int32_t)(S * 0.18f), (int32_t)(S * 0.68f), (int32_t)(S * 0.16f), 0,
               RGB(232, 70, 70), 255);
        // '1' vectorial (drawBigChar con capH = 0.44 S y grosor 3)
        float capH = S * 0.44f, DW = capH * 0.60f, m = 1.5f;
        float ox = cx - (int32_t)(S * 0.60f * 0.30f), oy = y + (int32_t)(S * 0.36f);
        float T = oy + m, B = oy + capH - m, MX = ox + DW * 0.5f, sx = MX + DW * 0.06f;
        lv_color_t ink = RGB(60, 60, 64);
        p_seg(p, sx, T, sx, B, 1, ink);
        p_seg(p, MX - DW * 0.26f, T + capH * 0.16f, sx, T, 1, ink);
        p_seg(p, MX - DW * 0.30f, B, MX + DW * 0.34f, B, 1, ink);
        break;
    }
    case IC_CLIMA: {
        p_circle(p, cx + (int32_t)(S * 0.10f), cy - (int32_t)(S * 0.13f), (int32_t)(S * 0.26f), RGB(255, 214, 96), 70);
        p_circle(p, cx + (int32_t)(S * 0.10f), cy - (int32_t)(S * 0.13f), (int32_t)(S * 0.18f), RGB(255, 206, 72), 255);
        int32_t r = (int32_t)(S * 0.13f) < 2 ? 2 : (int32_t)(S * 0.13f);
        float ccx = cx - (int32_t)(S * 0.05f), ccy = cy + (int32_t)(S * 0.12f);
        p_circle(p, ccx - r, ccy, r, WHITE, 255);
        p_circle(p, ccx + r, ccy, (r * 4) / 5, WHITE, 255);
        p_circle(p, ccx, ccy - (r * 3) / 4, (r * 6) / 5, WHITE, 255);
        p_rect(p, ccx - r - r / 2, ccy, (int32_t)(S * 0.40f), r + 1, 0, WHITE, 255);
        p_circle(p, ccx - r - r / 2, ccy, r / 2 + 1, WHITE, 255);
        break;
    }
    case IC_FLEXSTORE: {
        float bx = x + (int32_t)(S * 0.22f), by = y + (int32_t)(S * 0.34f);
        p_rect(p, bx, by, (int32_t)(S * 0.56f), (int32_t)(S * 0.46f), (int32_t)(S * 0.09f), WHITE, 255);
        float ty = y + (int32_t)(S * 0.22f);
        p_seg(p, cx - (int32_t)(S * 0.16f), by + 2, cx - (int32_t)(S * 0.10f), ty, 2, WHITE);
        p_seg(p, cx - (int32_t)(S * 0.10f), ty, cx + (int32_t)(S * 0.10f), ty, 2, WHITE);
        p_seg(p, cx + (int32_t)(S * 0.10f), ty, cx + (int32_t)(S * 0.16f), by + 2, 2, WHITE);
        p_rect(p, cx - (int32_t)(S * 0.16f), by + (int32_t)(S * 0.13f), (int32_t)(S * 0.32f), (int32_t)(S * 0.09f),
               (int32_t)(S * 0.04f), RGB(105, 91, 230), 255);
        break;
    }
    case IC_FLEXPHONE: {
        int32_t pw = (int32_t)(S * 0.34f), ph = (int32_t)(S * 0.54f);
        float px0 = cx - pw / 2, py0 = y + (int32_t)(S * 0.23f);
        p_rect(p, px0, py0, pw, ph, (int32_t)(S * 0.07f), WHITE, 255);
        p_rect(p, px0 + (int32_t)(S * 0.04f), py0 + (int32_t)(S * 0.07f), pw - (int32_t)(S * 0.08f),
               ph - (int32_t)(S * 0.16f), (int32_t)(S * 0.03f), RGB(38, 132, 255), 255);
        p_rect(p, cx - (int32_t)(S * 0.05f), py0 + ph - (int32_t)(S * 0.07f), (int32_t)(S * 0.10f),
               (int32_t)(S * 0.03f), 1, WHITE, 255);
        // Dos arcos de enlace A LA DERECHA del aparato (lo que dice el codigo
        // original; su arcStroke(300 -> 60) recorria el lado izquierdo y los
        // pintaba sobre la pantalla del telefono: rareza que no se copia).
        p_arc_cw(p, px0 + pw, cy, (int32_t)(S * 0.16f), 300, 60, 2, WHITE);
        p_arc_cw(p, px0 + pw, cy, (int32_t)(S * 0.26f), 310, 50, 2, WHITE);
        break;
    }
    case IC_DEVCARE: {
        float sw = S * 0.30f, sh = S * 0.34f;
        p_rect(p, (int32_t)(cx - sw), (int32_t)(cy - sh), (int32_t)(sw * 2), (int32_t)(sh * 1.35f),
               (int32_t)(S * 0.07f), WHITE, 255);
        p_tri(p, (int32_t)(cx - sw), (int32_t)(cy + sh * 0.30f), (int32_t)(cx + sw), (int32_t)(cy + sh * 0.30f), cx,
              (int32_t)(cy + sh * 1.05f), WHITE);
        lv_color_t ac = RGB(20, 132, 122);
        float px0 = cx - sw * 0.78f, py0 = cy - sh * 0.10f, st = sw * 0.39f;
        p_seg(p, px0, py0, px0 + st, py0, 2, ac);
        p_seg(p, px0 + st, py0, px0 + st * 1.5f, py0 - sh * 0.44f, 2, ac);
        p_seg(p, px0 + st * 1.5f, py0 - sh * 0.44f, px0 + st * 2.1f, py0 + sh * 0.40f, 2, ac);
        p_seg(p, px0 + st * 2.1f, py0 + sh * 0.40f, px0 + st * 2.6f, py0, 2, ac);
        p_seg(p, px0 + st * 2.6f, py0, px0 + st * 4.0f, py0, 2, ac);
        break;
    }
    case IC_MUSICA: {
        int32_t hr = (int32_t)(S * 0.085f) < 2 ? 2 : (int32_t)(S * 0.085f);
        int32_t st = S / 16 + 1;
        float x1 = x + (int32_t)(S * 0.36f), x2 = x + (int32_t)(S * 0.66f);
        float yb1 = y + (int32_t)(S * 0.70f), yb2 = y + (int32_t)(S * 0.64f);
        float yt1 = y + (int32_t)(S * 0.28f), yt2 = y + (int32_t)(S * 0.22f);
        int32_t bh = (int32_t)(S * 0.09f) + 1;
        p_circle(p, x1 - hr + st, yb1, hr, WHITE, 255);
        p_circle(p, x2 - hr + st, yb2, hr, WHITE, 255);
        p_rect(p, x1, yt1, st, yb1 - yt1, 0, WHITE, 255);
        p_rect(p, x2, yt2, st, yb2 - yt2, 0, WHITE, 255);
        p_tri(p, x1, yt1, x2 + st, yt2, x2 + st, yt2 + bh, WHITE);
        p_tri(p, x1, yt1, x2 + st, yt2 + bh, x1, yt1 + bh, WHITE);
        break;
    }
    case IC_BRUJULA: {
        p_circle(p, cx, cy, (int32_t)(S * 0.34f), RGB(246, 248, 252), 255);
        p_ring(p, cx, cy, (int32_t)(S * 0.34f), (int32_t)(S * 0.05f) + 1, RGB(30, 66, 138));
        for (int k = 0; k < 4; k++) {
            float a = k * 90.0f * DEG, r0 = S * 0.29f, r1 = S * 0.22f;
            p_seg(p, cx + r0 * sinf(a), cy - r0 * cosf(a), cx + r1 * sinf(a), cy - r1 * cosf(a), S >= 56 ? 1 : 0,
                  RGB(96, 116, 150));
        }
        float L = S * 0.24f, Wd = S * 0.085f;
        p_tri(p, cx, cy - (int32_t)L, cx - (int32_t)Wd, cy, cx + (int32_t)Wd, cy, RGB(226, 62, 62));
        p_tri(p, cx, cy + (int32_t)L, cx - (int32_t)Wd, cy, cx + (int32_t)Wd, cy, RGB(108, 116, 132));
        p_circle(p, cx, cy, (int32_t)(S * 0.055f) + 1, RGB(30, 40, 58), 255);
        break;
    }
    case IC_CAMARA:
        p_circle(p, cx, cy, (int32_t)(S * 0.27f), RGB(30, 30, 32), 255);
        p_ring(p, cx, cy, (int32_t)(S * 0.27f), 3, RGB(120, 120, 128));
        p_circle(p, cx, cy, (int32_t)(S * 0.16f), RGB(60, 72, 95), 255);
        p_circle(p, cx - (int32_t)(S * 0.06f), cy - (int32_t)(S * 0.06f), (int32_t)(S * 0.05f), RGB(150, 175, 205), 255);
        p_rect(p, x + (int32_t)(S * 0.66f), y + (int32_t)(S * 0.18f), (int32_t)(S * 0.10f), (int32_t)(S * 0.06f), 2,
               RGB(190, 190, 195), 255);
        break;
    default:
        p_circle(p, cx, cy, S / 6, WHITE, 255);
        break;
    }
}

static void icon_draw_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target_obj(e);
    lv_layer_t *L = lv_event_get_layer(e);
    int id = (int)(intptr_t)lv_event_get_user_data(e);
    lv_area_t c;
    lv_obj_get_coords(obj, &c);
    pen_t p = {L, lv_obj_get_style_opa_recursive(obj, LV_PART_MAIN)};
    draw_glyph(&p, id, (float)c.x1, (float)c.y1, lv_area_get_width(&c));
}

lv_obj_t *flex_app_icon_create(lv_obj_t *parent, int id, int32_t S, flex_backdrop_t bd)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, S, S);
    lv_obj_set_scrollable(o, false);
    lv_obj_set_style_radius(o, S * 22 / 100, 0);   // 22 % de S (iconBase)
    flex_surface(o, FLEX_SURF_TINT, bd);
    flex_surface_set_tint(o, flex_app_icon_color(id));
    flex_surface_force_material(o, 2);
    flex_surface_set_flat_sheen(o, true);
    lv_obj_add_event_cb(o, icon_draw_cb, LV_EVENT_DRAW_MAIN, (void *)(intptr_t)id);
    return o;
}

void flex_app_icon_set_id(lv_obj_t *icon, int id)
{
    uint32_t n = lv_obj_get_event_count(icon);
    for (uint32_t i = 0; i < n; i++) {
        lv_event_dsc_t *d = lv_obj_get_event_dsc(icon, i);
        if (d && lv_event_dsc_get_cb(d) == icon_draw_cb) {
            lv_obj_remove_event(icon, i);
            break;
        }
    }
    flex_surface_set_tint(icon, flex_app_icon_color(id));
    lv_obj_add_event_cb(icon, icon_draw_cb, LV_EVENT_DRAW_MAIN, (void *)(intptr_t)id);
    lv_obj_invalidate(icon);
}

// ---- glifos del sistema ------------------------------------------------------
typedef struct {
    flex_glyph_t g;
    int32_t value;
} glyph_t;

static void glyph_draw_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target_obj(e);
    glyph_t *gd = lv_event_get_user_data(e);
    lv_layer_t *L = lv_event_get_layer(e);
    lv_area_t c;
    lv_obj_get_coords(obj, &c);
    pen_t p = {L, lv_obj_get_style_opa_recursive(obj, LV_PART_MAIN)};
    lv_color_t col = lv_obj_get_style_text_color(obj, LV_PART_MAIN);
    float w = (float)lv_area_get_width(&c), h = (float)lv_area_get_height(&c);
    float cx = c.x1 + w / 2, cy = c.y1 + h / 2;
    float s = (w < h ? w : h);
    switch (gd->g) {
    case FLEX_GLYPH_WIFI: {
        // drawWifi(cx, by, R): tres arcos 225..315 y punto en la base
        float R = s * 0.5f - 1, by = cy + R * 0.45f;
        int bars = gd->value;
        lv_opa_t dim = 70;
        for (int k = 0; k < 3; k++) {
            float r = R * (k == 0 ? 1.0f : k == 1 ? 0.66f : 0.33f);
            pen_t q = p;
            if (bars >= 0 && 3 - k > bars) {
                q.opa = (lv_opa_t)((uint32_t)p.opa * dim / 255);
            }
            p_arc(&q, cx, by, r, 225, 315, 2, col);
        }
        p_circle(&p, cx, by, 2, col, bars < 0 ? dim : 255);
        if (bars < 0) {
            p_seg(&p, cx - R * 0.6f, by - R * 0.9f, cx + R * 0.6f, by, 1, col);
        }
        break;
    }
    case FLEX_GLYPH_BATTERY: {
        // drawBattery(x,y,w,h,level): contorno doble r2, borne, relleno
        float bw = w - 3, bh = h * 0.5f, bx = c.x1, byy = cy - bh / 2;
        p_rrect_outline(&p, bx, byy, bw, bh, 2, col);
        p_rrect_outline(&p, bx + 1, byy + 1, bw - 2, bh - 2, 2, col);
        p_rect(&p, bx + bw, byy + bh / 3, 2, bh / 3, 0, col, 255);
        if (gd->value >= 0) {
            float fw = (bw - 6) * (gd->value > 100 ? 100 : gd->value) / 100;
            p_rect(&p, bx + 3, byy + 3, fw, bh - 6, 0, col, 255);
        }
        break;
    }
    case FLEX_GLYPH_BACK:   // triangulo (cx-10, ny+8), (cx+8, ny-2), (cx+8, ny+18), centrado
        p_tri(&p, cx - 9, cy, cx + 9, cy - 10, cx + 9, cy + 10, col);
        break;
    case FLEX_GLYPH_HOME:
        p_ring(&p, cx, cy, 12, 2, col);
        break;
    case FLEX_GLYPH_RECENTS:
        p_rrect_outline(&p, cx - 11, cy - 11, 22, 22, 4, col);
        p_rrect_outline(&p, cx - 10, cy - 10, 20, 20, 3, col);
        break;
    case FLEX_GLYPH_CHEVRON_LEFT:   // (32,20)->(24,28)->(32,36) en la zona 56x56
        p_seg(&p, cx + 4, cy - 8, cx - 4, cy, 1, col);
        p_seg(&p, cx - 4, cy, cx + 4, cy + 8, 1, col);
        break;
    case FLEX_GLYPH_CHEVRON_RIGHT:
        p_seg(&p, cx - 3, cy - 6, cx + 3, cy, 1, col);
        p_seg(&p, cx + 3, cy, cx - 3, cy + 6, 1, col);
        break;
    case FLEX_GLYPH_MENU:
        for (int k = -1; k <= 1; k++) {
            p_circle(&p, cx, cy + k * 14, 3, col, 255);
        }
        break;
    case FLEX_GLYPH_PLUS:
        p_seg(&p, cx - s * 0.3f, cy, cx + s * 0.3f, cy, 1, col);
        p_seg(&p, cx, cy - s * 0.3f, cx, cy + s * 0.3f, 1, col);
        break;
    case FLEX_GLYPH_MINUS:
        p_seg(&p, cx - s * 0.3f, cy, cx + s * 0.3f, cy, 1, col);
        break;
    case FLEX_GLYPH_CLOSE:
        p_seg(&p, cx - s * 0.25f, cy - s * 0.25f, cx + s * 0.25f, cy + s * 0.25f, 1, col);
        p_seg(&p, cx + s * 0.25f, cy - s * 0.25f, cx - s * 0.25f, cy + s * 0.25f, 1, col);
        break;
    case FLEX_GLYPH_CHECK:
        p_seg(&p, cx - s * 0.28f, cy, cx - s * 0.08f, cy + s * 0.2f, 1, col);
        p_seg(&p, cx - s * 0.08f, cy + s * 0.2f, cx + s * 0.3f, cy - s * 0.22f, 1, col);
        break;
    case FLEX_GLYPH_GEAR:
        p_circle(&p, cx, cy, (int32_t)(s * 0.26f), col, 255);
        for (int k = 0; k < 8; k++) {
            float a = k * 45 * DEG;
            p_circle(&p, cx + s * 0.32f * cosf(a), cy + s * 0.32f * sinf(a), (int32_t)(s * 0.08f), col, 255);
        }
        p_ring(&p, cx, cy, (int32_t)(s * 0.10f), 1, col);
        break;
    case FLEX_GLYPH_SUN:
        p_circle(&p, cx, cy, (int32_t)(s * 0.2f), col, 255);
        for (int k = 0; k < 8; k++) {
            float a = k * 45 * DEG;
            p_seg(&p, cx + s * 0.3f * cosf(a), cy + s * 0.3f * sinf(a), cx + s * 0.42f * cosf(a),
                  cy + s * 0.42f * sinf(a), 1, col);
        }
        break;
    case FLEX_GLYPH_MOON:
        p_arc(&p, cx, cy, s * 0.32f, 60, 300, 4, col);
        break;
    case FLEX_GLYPH_LOCK:
        p_rect(&p, cx - s * 0.28f, cy - s * 0.05f, s * 0.56f, s * 0.42f, 3, col, 255);
        p_arc(&p, cx, cy - s * 0.08f, s * 0.18f, 180, 360, 2, col);
        break;
    case FLEX_GLYPH_POWER:
        p_arc(&p, cx, cy, s * 0.32f, -68, 248, 2, col);
        p_seg(&p, cx, cy - s * 0.42f, cx, cy - s * 0.05f, 1, col);
        break;
    case FLEX_GLYPH_SEARCH:
        p_ring(&p, cx - s * 0.08f, cy - s * 0.08f, (int32_t)(s * 0.24f), 2, col);
        p_seg(&p, cx + s * 0.1f, cy + s * 0.1f, cx + s * 0.32f, cy + s * 0.32f, 1, col);
        break;
    case FLEX_GLYPH_BELL:
        p_arc(&p, cx, cy, s * 0.24f, 180, 360, 3, col);
        p_rect(&p, cx - s * 0.26f, cy, s * 0.52f, s * 0.2f, 2, col, 255);
        p_circle(&p, cx, cy + s * 0.3f, (int32_t)(s * 0.07f), col, 255);
        break;
    case FLEX_GLYPH_AIRPLANE:
        p_seg(&p, cx, cy - s * 0.4f, cx, cy + s * 0.4f, 1, col);
        p_tri(&p, cx, cy - s * 0.12f, cx - s * 0.42f, cy + s * 0.1f, cx + s * 0.42f, cy + s * 0.1f, col);
        p_tri(&p, cx, cy + s * 0.22f, cx - s * 0.18f, cy + s * 0.38f, cx + s * 0.18f, cy + s * 0.38f, col);
        break;
    case FLEX_GLYPH_BLUETOOTH: {
        float t = s * 0.36f, xr = s * 0.2f;
        p_seg(&p, cx, cy - t, cx, cy + t, 0, col);
        p_seg(&p, cx, cy - t, cx + xr, cy - t / 2, 0, col);
        p_seg(&p, cx + xr, cy - t / 2, cx - xr, cy + t / 2, 0, col);
        p_seg(&p, cx, cy + t, cx + xr, cy + t / 2, 0, col);
        p_seg(&p, cx + xr, cy + t / 2, cx - xr, cy - t / 2, 0, col);
        break;
    }
    case FLEX_GLYPH_SPEAKER: {
        p_rect(&p, cx - s * 0.36f, cy - s * 0.12f, s * 0.16f, s * 0.24f, 0, col, 255);
        p_tri(&p, cx - s * 0.22f, cy - s * 0.12f, cx, cy - s * 0.32f, cx, cy + s * 0.32f, col);
        p_tri(&p, cx - s * 0.22f, cy - s * 0.12f, cx, cy + s * 0.32f, cx - s * 0.22f, cy + s * 0.12f, col);
        if (gd->value > 5) {
            p_arc(&p, cx + s * 0.02f, cy, s * 0.16f, -45, 45, 2, col);
        }
        if (gd->value > 45) {
            p_arc(&p, cx + s * 0.02f, cy, s * 0.3f, -45, 45, 2, col);
        }
        break;
    }
    case FLEX_GLYPH_GLASS:
        p_rrect_outline(&p, cx - s * 0.36f, cy - s * 0.3f, s * 0.46f, s * 0.46f, 4, col);
        p_rect(&p, cx - s * 0.1f, cy - s * 0.16f, s * 0.46f, s * 0.46f, 4, col, 140);
        break;
    case FLEX_GLYPH_EYE: {
        // drwGlyphEye (AppDrawer.h:552): dos parabolas dy = h/2 (1 - t^2), pupila r s/6
        float ew = s, eh = s / 2;
        float px0 = cx - ew / 2, py0 = cy;
        for (int k = 1; k <= 8; k++) {
            float t = -1.0f + k / 4.0f;
            float x1 = cx + t * ew / 2, dy = eh * 0.5f * (1.0f - t * t);
            float tp = -1.0f + (k - 1) / 4.0f, dyp = eh * 0.5f * (1.0f - tp * tp);
            p_seg(&p, px0, py0 - dyp + (py0 - cy), x1, cy - dy, 0, col);
            p_seg(&p, px0, cy + dyp, x1, cy + dy, 0, col);
            px0 = x1;
            py0 = cy;
        }
        p_circle(&p, cx, cy, (int32_t)(s / 6), col, 235);
        if (gd->value == 0) {
            p_seg(&p, cx - ew / 2, cy + eh / 2, cx + ew / 2, cy - eh / 2, 1, col);
        }
        break;
    }
    case FLEX_GLYPH_INFO:
        p_ring(&p, cx, cy, (int32_t)(s / 2 - 1), 1, col);
        p_rect(&p, cx - 1, cy - 5, 2, 10, 0, col, 255);
        p_rect(&p, cx - 1, cy - 9, 2, 2, 0, col, 255);
        break;
    case FLEX_GLYPH_OPEN:
        p_seg(&p, cx - s / 2, cy, cx + s / 2, cy, 1, col);
        p_seg(&p, cx + s / 2 - 9, cy - 8, cx + s / 2, cy, 1, col);
        p_seg(&p, cx + s / 2 - 9, cy + 8, cx + s / 2, cy, 1, col);
        break;
    case FLEX_GLYPH_RING:
        p_ring(&p, cx, cy, (int32_t)(s / 3), 4, col);
        break;
    case FLEX_GLYPH_TRASH: {
        float bw = s - 8, bh = s - 8, bx = cx - s / 2 + 4, by = cy - s / 2 + 6;
        p_rrect_outline(&p, bx, by + 4, bw, bh - 4, 3, col);
        p_rect(&p, bx - 2, by, bw + 4, 2, 0, col, 255);
        p_rect(&p, bx + bw / 2 - 4, by - 3, 8, 3, 0, col, 255);
        p_rect(&p, bx + bw / 3, by + 9, 2, bh - 16, 0, col, 255);
        p_rect(&p, bx + 2 * bw / 3, by + 9, 2, bh - 16, 0, col, 255);
        break;
    }
    case FLEX_GLYPH_CHEVRON_DOWN:
        p_seg(&p, cx - 9, cy - 4, cx, cy + 5, 1, col);
        p_seg(&p, cx + 9, cy - 4, cx, cy + 5, 1, col);
        break;
    }
}

static void glyph_delete_cb(lv_event_t *e)
{
    free(lv_event_get_user_data(e));
}

lv_obj_t *flex_glyph_create(lv_obj_t *parent, flex_glyph_t g, int32_t w, int32_t h)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_set_scrollable(o, false);
    lv_obj_set_clickable(o, false);
    glyph_t *gd = calloc(1, sizeof(*gd));
    if (gd) {
        gd->g = g;
        gd->value = 100;
        lv_obj_add_event_cb(o, glyph_draw_cb, LV_EVENT_DRAW_MAIN, gd);
        lv_obj_add_event_cb(o, glyph_delete_cb, LV_EVENT_DELETE, gd);
    }
    lv_obj_set_style_text_color(o, flex_th()->txt, 0);
    return o;
}

void flex_glyph_set(lv_obj_t *obj, flex_glyph_t g, int32_t value)
{
    uint32_t n = lv_obj_get_event_count(obj);
    for (uint32_t i = 0; i < n; i++) {
        lv_event_dsc_t *d = lv_obj_get_event_dsc(obj, i);
        if (d && lv_event_dsc_get_cb(d) == glyph_draw_cb) {
            glyph_t *gd = lv_event_dsc_get_user_data(d);
            if (gd->g != g || gd->value != value) {
                gd->g = g;
                gd->value = value;
                lv_obj_invalidate(obj);
            }
            return;
        }
    }
}
