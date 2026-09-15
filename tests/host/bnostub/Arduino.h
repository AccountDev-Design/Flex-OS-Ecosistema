#pragma once
// #############################################################
//  Doble MINIMO de Arduino para la prueba del driver del GY-BNO085.
//  ------------------------------------------------------------
//  FlexOS_BNO085.cpp solo usa millis() y Wire; no hay nada mas que
//  simular. El reloj es VIRTUAL y lo mueve la prueba, de modo que
//  "cuanto tiempo se ha ido en el bus" es un numero medible y no una
//  medicion de pared que dependa de la maquina que ejecuta el test.
// #############################################################
#include <stdint.h>
#include <stddef.h>
#include <string.h>

extern uint32_t gFakeMs;
static inline uint32_t millis(){ return gFakeMs; }
