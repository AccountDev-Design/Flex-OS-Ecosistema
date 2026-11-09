// Flex OS Ultra · app Juegos: Jumper (docs/spec/03 §14; Arduino: FlexOS_Ultra_AppGames.h + FlexOS_Jumper.h; inventario A10).
//
// El juego es el de Arduino TAL CUAL, sobre el motor de dibujo de Arduino extraido, dentro del componente flex_games (flex_jumper.h): fisica de paso
// fijo a 120 Hz, nivel, particulas, intro, selector, pausa y records. Esta capa solo pone lo que el sistema daba a una app horizontal:
//  * un lienzo FISICO de 480 x 800 en RGB565 (el motor gira 90 grados al dibujar: el lienzo logico es de 800 x 480) mostrado a pantalla completa;
//  * el tactil del GT911 en coordenadas fisicas (el juego las gira al lienzo logico), con el flanco de apoyo de cada cuadro;
//  * el reloj en microsegundos y un temporizador que da una vuelta al juego (el propio juego decide cuando toca presentar: 60 Hz exactos);
//  * la salida (appClose), suspender / reanudar / cerrar.
// Es una app FLEX_APP_LAND: el marco la deja a pantalla completa, sin barra de navegacion ni gestos del sistema (flex_shell_land) y sin miniatura
// en Recientes, como Arduino (Core.h:997).
#include <string.h>
#include "flex_app.h"
#include "flex_frame.h"
#include "flex_jumper.h"
#include "flex_theme.h"
#include "flex_touch_lvgl.h"

#ifdef FLEX_SIM
#define NOW_US() ((uint32_t)lv_tick_get() * 1000u)   // el simulador lleva su propio reloj
#else
#include "esp_timer.h"
#define NOW_US() ((uint32_t)esp_timer_get_time())
#endif

#define TICK_MS 4   // el juego presenta a 60 Hz por su cuenta; esto solo le da vueltas a menudo para que no pierda el flanco de un toque

static struct {
    lv_obj_t *root, *img, *hit;
    lv_draw_buf_t *buf;
    lv_timer_t *tmr;
    bool open, susp, press_edge, down;
    int x, y;
    uint32_t frames, laps, presses;
} G;

static void tick_cb(lv_timer_t *t)
{
    (void)t;
    if (!G.open || G.susp || !G.buf) {
        return;
    }
    bool pressed = G.press_edge;
    G.press_edge = false;
    G.presses += pressed ? 1u : 0u;
    flex_jmp_tick(NOW_US(), pressed, G.down, G.x, G.y);
    G.laps++;
    if (flex_jmp_take_frame()) {
        G.frames++;
        lv_obj_invalidate(G.img);   // el cuadro es completo: el juego lo recompone entero y lo presenta una vez
    }
    if (flex_jmp_take_close()) {
        flex_app_close();   // la flecha del selector (appClose)
    }
}

static void touch_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    if (code == LV_EVENT_PRESSED) {
        G.press_edge = true;
        G.down = true;
        G.x = (int)p.x;
        G.y = (int)p.y;
    } else if (code == LV_EVENT_PRESSING) {
        G.down = true;
        G.x = (int)p.x;
        G.y = (int)p.y;
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        G.down = false;
    }
}

static void timer_sync(void)
{
    if ((G.susp || !G.open) && G.tmr) {
        lv_timer_delete(G.tmr);
        G.tmr = NULL;
    } else if (G.open && !G.susp && !G.tmr) {
        G.tmr = lv_timer_create(tick_cb, TICK_MS, NULL);
    }
}

static void on_create(lv_obj_t *root, const lv_area_t *area)
{
    (void)area;
    memset(&G, 0, sizeof(G));
    G.root = root;
    // el lienzo fisico: 480 x 800 contiguo (el motor direcciona por fila de 480 pixeles)
    G.buf = lv_draw_buf_create(FLEX_JMP_W, FLEX_JMP_H, LV_COLOR_FORMAT_RGB565, FLEX_JMP_W * 2);
    if (!G.buf || G.buf->header.stride != FLEX_JMP_W * 2) {
        if (G.buf) {
            lv_draw_buf_destroy(G.buf);
            G.buf = NULL;
        }
        const flex_palette_t *t = flex_th();
        lv_obj_t *l = flex_label(root, "No hay memoria para Jumper", FLEX_FONT_S2, t->txt2);
        lv_obj_center(l);
        return;
    }
    memset(G.buf->data, 0, FLEX_JMP_BYTES);
    G.img = lv_image_create(root);
    lv_obj_set_pos(G.img, 0, 0);
    lv_obj_set_size(G.img, FLEX_JMP_W, FLEX_JMP_H);
    lv_obj_set_clickable(G.img, false);
    lv_image_set_src(G.img, G.buf);
    G.hit = flex_box(root);   // recoge el dedo en toda la pantalla (la app posee TODO el tactil: APP_OWN_TOUCH)
    lv_obj_set_pos(G.hit, 0, 0);
    lv_obj_set_size(G.hit, 480, 800);
    lv_obj_set_clickable(G.hit, true);
    lv_obj_set_style_bg_opa(G.hit, LV_OPA_TRANSP, 0);
    lv_obj_set_press_lock(G.hit, true);
    lv_obj_add_event_cb(G.hit, touch_cb, LV_EVENT_ALL, NULL);
    flex_touch_drop_all();   // el toque que abrio la app no cuenta como del juego
    G.open = true;
    flex_jmp_open((uint16_t *)G.buf->data, NOW_US());
    lv_obj_invalidate(G.img);
    timer_sync();
}

static void on_resume(void)
{
    G.susp = false;
    if (!G.open) {
        return;
    }
    G.press_edge = G.down = false;
    flex_jmp_resume(NOW_US());   // gamesResume: si estaba en pausa repinta la pausa; si no, da una vuelta
    lv_obj_invalidate(G.img);
    timer_sync();
}

static void on_suspend(void)
{
    G.susp = true;
    if (G.open) {
        flex_jmp_suspend();   // gamesSuspend: guarda y deja la partida en pausa
    }
    timer_sync();
}

static void on_close(void)
{
    if (G.open) {
        flex_jmp_close();   // gamesCloseApp: guarda lo pendiente
    }
    if (G.tmr) {
        lv_timer_delete(G.tmr);
    }
    if (G.img) {
        lv_obj_delete(G.img);   // la imagen apunta al buffer: primero ella
    }
    if (G.buf) {
        lv_draw_buf_destroy(G.buf);
    }
    memset(&G, 0, sizeof(G));
}

// ---- pruebas ----
uint32_t flex_app_games_frames(void) { return G.open ? G.frames : 0; }
uint32_t flex_app_games_laps(void) { return G.open ? G.laps : 0; }
bool flex_app_games_open(void) { return G.open; }
uint32_t flex_app_games_presses(void) { return G.open ? G.presses : 0; }   // vueltas que vieron un flanco de apoyo
bool flex_app_games_down(void) { return G.open && G.down; }
const uint16_t *flex_app_games_canvas(void) { return G.buf ? (const uint16_t *)G.buf->data : NULL; }

const flex_app_ops_t flex_app_juegos_ops = {
    .on_create = on_create,
    .on_resume = on_resume,
    .on_suspend = on_suspend,
    .on_close = on_close,
};
