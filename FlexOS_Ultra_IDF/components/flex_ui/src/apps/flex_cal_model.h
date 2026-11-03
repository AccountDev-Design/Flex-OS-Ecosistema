// Flex OS Ultra · Calendario: logica pura (docs/spec/03 §4; Arduino: AppsBasic.h:351-403, Clock.h:79).
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

int flex_cal_dim(int y, int m);                       // dias del mes (m 1..12), bisiestos incluidos
// Columna (0 = domingo) del dia 1: ((wd - (d - 1)) % 7 + 7) % 7 con wd = dia de la semana de HOY (d)
int flex_cal_first(int wd, int d);
int flex_cal_rows(int first, int dim);                // filas de la rejilla (1..6)
// Panel lateral "Hoy": solo si el lienzo da >= 430 de ancho y quedan >= 28 px por celda. Devuelve si se ve y
// rellena sideW = min(bw/3, 210) y gridW (con o sin panel)
int flex_cal_side(int bw, int pad, int gap, int *side_w, int *grid_w);

#ifdef __cplusplus
}
#endif
