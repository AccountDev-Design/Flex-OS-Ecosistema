#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int x, y;
    bool pressed;
    int fingers;     // dedos del cuadro (1 por defecto; 2 para los gestos del sistema)
} sim_touch_t;

void sim_run(uint32_t ms);
void sim_touch(int x, int y, bool pressed);
void sim_touch_n(int x, int y, int fingers);   // fingers = 0: soltar
void sim_tap(int x, int y);
void sim_drag(int x0, int y0, int x1, int y1, uint32_t ms);
void sim_shot(const char *name);

typedef struct {
    const char *name;
    bool (*run)(void);   // false = la escena detecto un fallo
} sim_scene_t;
extern const sim_scene_t sim_scenes[];
