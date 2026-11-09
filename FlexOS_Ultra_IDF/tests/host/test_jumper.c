// Juegos · Jumper: el juego de Arduino TAL CUAL sobre el motor de dibujo de Arduino extraido (components/flex_games). Aqui se prueba lo que
// pone el adaptador y que el juego de verdad corre con el: intro, selector, partida, muerte y reaparicion, pausa, records que sobreviven
// a cerrar la app, la salida, el tactil en coordenadas fisicas (el juego gira 90 grados), la determinacion (mismo guion = mismos pixeles)
// y que nada se escribe fuera del lienzo.
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "flex_jumper.h"
#include "flex_storage.h"
#include "flex_test.h"

void test_cfg_reset(void);

// el lienzo con un colchon de guarda a cada lado: lo que se escriba fuera se ve
#define GUARD 4096
static uint16_t s_mem[GUARD + FLEX_JMP_W * FLEX_JMP_H + GUARD];
#define CANVAS (s_mem + GUARD)
static uint32_t s_now;

static uint32_t crc(void)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < (size_t)FLEX_JMP_W * FLEX_JMP_H; i++) {
        h ^= CANVAS[i];
        h *= 16777619u;
    }
    return h;
}
static bool guard_clean(void)
{
    for (int i = 0; i < GUARD; i++) {
        if (s_mem[i] != 0xBEEF || s_mem[GUARD + FLEX_JMP_W * FLEX_JMP_H + i] != 0xBEEF) {
            return false;
        }
    }
    return true;
}
static void tick(bool pressed, bool down, int x, int y)
{
    s_now += 16667;   // 60 Hz
    flex_jmp_tick(s_now, pressed, down, x, y);
}
static void idle(int frames)
{
    for (int i = 0; i < frames; i++) {
        tick(false, false, 0, 0);
    }
}
// un toque en el lienzo LOGICO (800 x 480): el panel lo da girado (x = 479 - ly, y = lx)
static void tap_l(int lx, int ly)
{
    int px = 479 - ly, py = lx;
    tick(true, true, px, py);
    tick(false, false, px, py);
}
static void open_game(void)
{
    memset(s_mem, 0, sizeof(s_mem));
    for (int i = 0; i < GUARD; i++) {
        s_mem[i] = 0xBEEF;
        s_mem[GUARD + FLEX_JMP_W * FLEX_JMP_H + i] = 0xBEEF;
    }
    s_now = 5000000;
    flex_jmp_open(CANVAS, s_now);
}
static int nonblank(void)
{
    int n = 0;
    for (size_t i = 0; i < (size_t)FLEX_JMP_W * FLEX_JMP_H; i++) {
        n += CANVAS[i] != 0;
    }
    return n;
}
// de la intro al selector
static void to_select(void)
{
    for (int i = 0; i < 600 && flex_jmp_screen() != FLEX_JMP_S_SELECT; i++) {
        idle(1);
    }
}

void test_jumper(void)
{
    test_cfg_reset();
    open_game();
    // ---- intro: pinta ya en el primer cuadro y no se salta con el toque que abre la app ----
    CHECK_EQ_I(flex_jmp_screen(), FLEX_JMP_S_INTRO);
    CHECK(flex_jmp_take_frame());
    tap_l(400, 240);   // antes de 300 ms no la salta (el mismo toque que abre la app no puede saltarsela)
    CHECK_EQ_I(flex_jmp_screen(), FLEX_JMP_S_INTRO);
    idle(60);          // un segundo de animacion: el titulo y el fondo ya estan pintados
    CHECK(nonblank() > 100000);
    to_select();
    CHECK_EQ_I(flex_jmp_screen(), FLEX_JMP_S_SELECT);
    double t_sel = (s_now - 5000000) / 1e6;
    CHECK(t_sel > 2.6 && t_sel < 2.8);   // JINTRO_T = 2.65 s
    uint32_t sel_crc = crc();
    CHECK(flex_jmp_take_frame());
    CHECK(!flex_jmp_take_frame());        // se limpia al leerlo
    CHECK(guard_clean());

    // ---- el selector: las flechas no cambian de pantalla; "Normal" y "Practica" arrancan; la "i" abre la informacion ----
    tap_l(40, 240);                       // flecha izquierda (14,190,74,100)
    CHECK_EQ_I(flex_jmp_screen(), FLEX_JMP_S_SELECT);
    tap_l(760, 40);                       // boton de informacion (730,8,62,62)
    CHECK_EQ_I(flex_jmp_screen(), FLEX_JMP_S_INFO);
    idle(20);
    tap_l(400, 240);                      // un toque la cierra (pasados 200 ms)
    CHECK_EQ_I(flex_jmp_screen(), FLEX_JMP_S_SELECT);
    idle(60);
    CHECK(crc() != 0 && sel_crc != 0);
    tap_l(400, 287);                      // "Normal" (163,262,474,50)
    CHECK_EQ_I(flex_jmp_screen(), FLEX_JMP_S_PLAY);
    CHECK(!flex_jmp_practice());

    // ---- partida: el nivel avanza solo; sin saltar el cubo muere en el primer pico y reaparece (intento + 1) ----
    int a0 = flex_jmp_attempts();
    float x0 = flex_jmp_player_x();
    idle(30);
    CHECK(flex_jmp_player_x() > x0 + 100.0f);   // 468 px/s: medio segundo, unos 230 px
    CHECK(flex_jmp_progress() > 0.0f && flex_jmp_progress() < 0.1f && flex_jmp_progress() == flex_jmp_player_x() / (900 * 45.0f));
    bool died = false;
    for (int i = 0; i < 60 * 8 && !died; i++) {
        idle(1);
        died = flex_jmp_screen() == FLEX_JMP_S_DEAD;
    }
    CHECK(died);
    uint32_t dead_crc = crc();
    CHECK(dead_crc != sel_crc);
    for (int i = 0; i < 60 * 3 && flex_jmp_screen() != FLEX_JMP_S_PLAY; i++) {
        idle(1);
    }
    CHECK_EQ_I(flex_jmp_screen(), FLEX_JMP_S_PLAY);   // reaparece solo
    CHECK(flex_jmp_attempts() > a0);
    CHECK(flex_jmp_player_x() < 200.0f);
    CHECK(flex_jmp_best_normal() >= 1);               // el record es el porcentaje que se llego

    // ---- saltar: un toque levanta al cubo y vuelve al suelo ----
    for (int i = 0; i < 60 * 3 && flex_jmp_screen() == FLEX_JMP_S_PLAY && flex_jmp_player_x() < 60.0f; i++) {
        idle(1);
    }
    float y_ground = flex_jmp_player_y();
    tap_l(500, 300);                      // fuera del HUD
    idle(6);
    CHECK(fabsf(flex_jmp_player_y() - y_ground) > 20.0f);

    // ---- pausa: el HUD (arriba a la izquierda) pausa; "Continuar" sigue donde estaba ----
    tap_l(30, 30);
    CHECK_EQ_I(flex_jmp_screen(), FLEX_JMP_S_PAUSE);
    idle(10);
    float x_paused = flex_jmp_player_x();
    idle(30);
    CHECK(flex_jmp_player_x() == x_paused);           // en pausa no avanza
    tap_l(400, 212);                      // "CONTINUAR" (x + 40, y + 76, w - 80, 42) con w = 420, y = 115
    CHECK_EQ_I(flex_jmp_screen(), FLEX_JMP_S_PLAY);
    tap_l(30, 30);
    tap_l(400, 264);                      // "REINTENTAR"
    CHECK_EQ_I(flex_jmp_screen(), FLEX_JMP_S_PLAY);
    CHECK(flex_jmp_player_x() < 100.0f);
    tap_l(30, 30);
    tap_l(400, 316);                      // "NIVELES"
    CHECK_EQ_I(flex_jmp_screen(), FLEX_JMP_S_SELECT);

    // ---- los records se guardan solos y sobreviven a cerrar la app y a abrirla otra vez ----
    int best = flex_jmp_best_normal(), att = flex_jmp_attempts();
    CHECK(best >= 1 && att >= 2);
    flex_jmp_close();
    CHECK_EQ_I(flex_cfg_get_u32("jmp_bn", 0), best);
    // los intentos se guardan en los puntos de guardado del juego (record nuevo, cierre con algo pendiente): como en Arduino, un intento
    // posterior al ultimo guardado no se cuenta
    uint32_t saved = flex_cfg_get_u32("jmp_att", 0);
    CHECK(saved >= 2 && saved <= (uint32_t)att);
    open_game();
    CHECK_EQ_I(flex_jmp_best_normal(), best);
    CHECK_EQ_I(flex_jmp_attempts(), saved);

    // ---- cerrar justo tras morir, con el record nuevo sin guardar (el juego lo guarda al reaparecer): cerrar lo guarda ----
    test_cfg_reset();
    open_game();
    to_select();
    tap_l(400, 287);
    for (int i = 0; i < 60 * 8 && flex_jmp_screen() != FLEX_JMP_S_DEAD; i++) {
        idle(1);
    }
    CHECK_EQ_I(flex_jmp_screen(), FLEX_JMP_S_DEAD);
    int best2 = flex_jmp_best_normal();
    CHECK(best2 >= 1);
    CHECK_EQ_I(flex_cfg_get_u32("jmp_bn", 0), 0);   // aun sin guardar
    flex_jmp_close();
    CHECK_EQ_I(flex_cfg_get_u32("jmp_bn", 0), best2);

    // ---- la flecha del selector sale de la app (appClose) ----
    open_game();
    to_select();
    CHECK(!flex_jmp_take_close());
    tap_l(40, 40);                        // chevron (10,4,78,76)
    CHECK(flex_jmp_take_close());
    CHECK(!flex_jmp_take_close());        // se limpia al leerlo

    // ---- suspender en plena partida deja la pausa; reanudar repinta sin avanzar ----
    open_game();
    to_select();
    tap_l(400, 287);
    idle(20);
    CHECK_EQ_I(flex_jmp_screen(), FLEX_JMP_S_PLAY);
    flex_jmp_suspend();
    CHECK_EQ_I(flex_jmp_screen(), FLEX_JMP_S_PAUSE);
    float xs = flex_jmp_player_x();
    s_now += 30 * 1000000u;               // media hora... 30 s en segundo plano
    flex_jmp_resume(s_now);
    CHECK_EQ_I(flex_jmp_screen(), FLEX_JMP_S_PAUSE);
    CHECK(flex_jmp_player_x() == xs);
    CHECK(flex_jmp_take_frame());

    // ---- determinacion: el mismo guion da los mismos pixeles (el estado del juego no depende de nada oculto) ----
    uint32_t c[2];
    for (int run = 0; run < 2; run++) {
        test_cfg_reset();
        open_game();
        to_select();
        tap_l(400, 287);
        for (int i = 0; i < 400; i++) {
            if (i % 55 == 30) {
                tap_l(500, 300);
            } else {
                idle(1);
            }
        }
        c[run] = crc();
    }
    CHECK(c[0] == c[1]);
    CHECK(guard_clean());                 // ni una escritura fuera del lienzo
    flex_jmp_close();
}
