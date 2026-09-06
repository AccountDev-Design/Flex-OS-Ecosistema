# Terceros, SOLO para las pruebas de host

Nada de esta carpeta entra en el binario de la placa.

## cJSON 1.7.18 — MIT

`FlexOS_PkgCore.cpp` analiza el manifest y el índice de un `.flexpkg` con
cJSON. En la placa cJSON **ya viene** con el core `esp32 by Espressif Systems`,
así que el firmware sólo hace `#include <cJSON.h>`.

En el PC no existe ese core. Se versiona aquí **la misma implementación** para
que la prueba de host recorra exactamente el mismo camino de código que la
placa: si se usara otro analizador, la prueba no diría nada útil sobre lo que
de verdad se ejecuta en el P4.

* Origen: https://github.com/DaveGamble/cJSON, etiqueta `v1.7.18`.
* Licencia: MIT (`cJSON/LICENSE`).
* Sin modificar. Para actualizarlo, se sustituyen los tres archivos por los de
  la etiqueta nueva y se vuelve a ejecutar `make` en `tests/host`.
