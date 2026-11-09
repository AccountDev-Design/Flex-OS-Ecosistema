// Flex OS Ultra · Juegos: Jumper. Ver include/flex_jumper.h. Una sola unidad de traduccion, como en el sketch: el prelogo da lo que el sketch
// daba, el motor extraido de Arduino dibuja y el juego de Arduino manda.
#include "flex_jumper.h"

#include "jumper_prelude.h"
#include "jumper_engine.inc"
#include "FlexOS_Jumper.h"
#include "jumper_games.inc"

static void jmp_input(bool pressed, bool down, int x, int y)
{
    T.pressed = pressed;
    T.down = down;
    T.x = x;
    T.y = y;
}

extern "C" {

void flex_jmp_open(uint16_t *canvas, uint32_t now_us)
{
    fb = bbuf = gBuf = canvas;
    g_now_us = now_us;
    g_frame_ready = false;
    g_close_req = false;
    gClipY0 = 0; gClipY1 = SCR_H - 1; gClipX0 = 0; gClipX1 = SCR_W - 1; gClipLY0 = 0; gClipLY1 = SCR_W - 1;
    jmp_input(false, false, 0, 0);
    jmpEnter();
}

void flex_jmp_tick(uint32_t now_us, bool pressed, bool down, int x, int y)
{
    g_now_us = now_us;
    jmp_input(pressed, down, x, y);
    jmpTick();
}

bool flex_jmp_take_frame(void)
{
    bool r = g_frame_ready;
    g_frame_ready = false;
    return r;
}

bool flex_jmp_take_close(void)
{
    bool r = g_close_req;
    g_close_req = false;
    return r;
}

void flex_jmp_suspend(void)
{
    gamesSuspend();
}

void flex_jmp_resume(uint32_t now_us)
{
    g_now_us = now_us;
    gamesResume();
}

void flex_jmp_close(void)
{
    gamesCloseApp();
    g_frame_ready = false;
    fb = bbuf = gBuf = nullptr;
}

int flex_jmp_screen(void) { return JG.screen; }
int flex_jmp_best_normal(void) { return JG.bestNormal; }
int flex_jmp_best_practice(void) { return JG.bestPractice; }
int flex_jmp_attempts(void) { return (int)JG.attemptsTotal; }
float flex_jmp_progress(void) { return JG.px / JLEN; }
float flex_jmp_player_x(void) { return JG.px; }
float flex_jmp_player_y(void) { return JG.py; }
bool flex_jmp_practice(void) { return JG.practice; }

}  // extern "C"
