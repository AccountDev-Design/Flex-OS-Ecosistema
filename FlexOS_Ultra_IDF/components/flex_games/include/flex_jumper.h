// Flex OS Ultra · Juegos: Jumper (docs/spec/03 §14; FlexOS_Jumper.h / FlexOS_Jumper_Level.h de Arduino).
//
// El juego es el de Arduino TAL CUAL (src/FlexOS_Jumper*.h, copias identicas que comprueba tools/check_portable.py) sobre el motor de dibujo de
// Arduino extraido tal cual (src/jumper_engine.inc, generado por tools/gen_jumper_engine.py): el mismo rasterizador y la misma fuente, asi que
// la fisica, el nivel y cada pixel son los de la version Arduino. Este adaptador solo da al juego lo que el sketch le daba: un lienzo
// fisico de 480x800 en RGB565 donde el motor gira 90 grados el dibujo (lienzo logico 800x480), el tactil, el reloj, los records (NVS) y
// la peticion de cerrar la app.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FLEX_JMP_W 480            // lienzo FISICO (vertical); el juego dibuja en logico 800 x 480
#define FLEX_JMP_H 800
#define FLEX_JMP_BYTES ((size_t)FLEX_JMP_W * FLEX_JMP_H * 2)

// Abre el juego (jmpEnter) sobre `canvas` (FLEX_JMP_BYTES, RGB565). now_us = reloj en microsegundos.
void flex_jmp_open(uint16_t *canvas, uint32_t now_us);
// Una vuelta (jmpTick). La entrada es la del GT911 en coordenadas FISICAS del panel: pressed = flanco de apoyo de este cuadro, down = dedo apoyado.
void flex_jmp_tick(uint32_t now_us, bool pressed, bool down, int x, int y);
// true si desde la ultima llamada el juego presento un cuadro nuevo (hay que repintar el lienzo)
bool flex_jmp_take_frame(void);
// true si el juego pidio salir (appClose); se limpia al leerlo
bool flex_jmp_take_close(void);
// suspender / reanudar / cerrar (gamesSuspend / gamesResume / gamesCloseApp)
void flex_jmp_suspend(void);
void flex_jmp_resume(uint32_t now_us);
void flex_jmp_close(void);

// ---- pruebas / estado ----
enum { FLEX_JMP_S_INTRO = 0, FLEX_JMP_S_SELECT, FLEX_JMP_S_PLAY, FLEX_JMP_S_DEAD, FLEX_JMP_S_DONE, FLEX_JMP_S_PAUSE, FLEX_JMP_S_INFO };
int flex_jmp_screen(void);
int flex_jmp_best_normal(void);
int flex_jmp_best_practice(void);
int flex_jmp_attempts(void);
float flex_jmp_progress(void);        // 0..1 de la partida en curso
float flex_jmp_player_x(void);
float flex_jmp_player_y(void);
bool flex_jmp_practice(void);

#ifdef __cplusplus
}
#endif
