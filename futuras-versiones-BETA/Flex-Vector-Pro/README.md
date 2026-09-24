# Flex Vector Pro — archivo para futuras versiones BETA

Este editor vectorial se retiró por completo de la versión activa de Flex OS
Ultra. Nada dentro de esta carpeta se incluye, enlaza ni ejecuta al compilar
`FlexOS_Ultra.ino`, y el `Makefile` de `tests/host/` ya no la mira.

La última integración completa está en el commit
`bac04f89cd0b991d79abf8d0140bc6559e64222e`.

## Qué se conserva

### `firmware-modules/`

| Archivo | Qué es |
|---|---|
| `FlexOS_Vector.h` / `.cpp` | el **motor vectorial** portable: modelo de documento, capas, `FlexVecElem`, Bézier cúbicas, transformaciones afines, rasterizador por líneas de barrido, booleanas, gradientes, motivos, deshacer/rehacer, serialización `.fxv`, exportación SVG y PNG |
| `FlexOS_Ultra_HttpShare.h` | la mitad de **placa** del servicio de compartir un solo archivo: socket de escucha, tarea FreeRTOS propia y pegamento con `gNetOnline` / `wifiConnIP` |
| `FlexOS_Ultra_AppVector.h` | la **app**: interfaz Liquid Glass, panel de control, tira de herramientas, paneles acoplados, gestos, pluma, edición de nodos, galería de documentos y compartir por Wi-Fi |

### `tests/`

* `test_vector.cpp` — la batería portable del motor, con ASan+UBSan. Se compila
  y pasa desde aquí.
* `ino_compile_testVectorPro.cpp.inc` — las comprobaciones de **interfaz**
  (toque con deriva, cajas táctiles de 49×49, tira sin zonas muertas en las dos
  orientaciones, traducción al panel). **No es una unidad de traducción**: vivía
  dentro de `tests/host/ino_compile.cpp`, que incluye el `.ino` entero, y hay
  que pegarlo de vuelta ahí para volver a ejecutarlo.

### `docs/`

`FLEX-VECTOR-PRO.md` — el contrato de diseño: presupuestos de memoria,
clasificación de cada función de Illustrator (Completa / Simplificada / Futura /
Omitida) con su justificación técnica, las dos fases y sus criterios de
aceptación, y en la §11 las cinco correcciones hechas sobre la placa.

## Por qué salió de la versión activa

Es una decisión de alcance, no de calidad: la versión actual de Flex OS Ultra no
lleva editor vectorial. Con la app fuera, el QR y el servidor HTTP local se
quedaban **sin un solo consumidor** en todo el firmware —se comprobó una por una
cada referencia—, así que se archivan con ella en vez de dejar una tarea de red
escuchando y un codificador que nadie llama.

**Actualización (Flex Media Ecosystem).** Eso es exactamente lo que pasó:
`FlexOS_HttpShare` (el protocolo HTTP) y `FlexOS_QR` (el codificador QR) volvieron
al firmware activo como piezas de **Flex Web Server**, el servidor de la
biblioteca de medios. Viven en `FlexOS_Ultra/` y sus pruebas (`test_httpshare`,
`test_qr`) corren en `tests/host/`. La API que usaba Flex Vector Pro sigue
intacta; el protocolo solo se amplió. Aquí siguen el motor vectorial, la app y
`FlexOS_Ultra_HttpShare.h` (el servicio de compartir UN archivo, que era de la
app).

## Qué se tocó al retirarla

En el sistema activo no quedó ningún hueco:

* `IC_VECTOR` desapareció del enum y `APP_N` bajó de 21 a 20. **Ningún índice
  anterior se movió**, así que el escritorio guardado, las apps favoritas y el
  candado por app de una placa que actualiza siguen apuntando a lo mismo. El id
  20 queda **libre**: la próxima app nueva lo ocupa.
* `FLEXFS_DIR_VECTOR` (`/Vector`) y `FLEXFS_EXT_VECTOR` (`.fxv`) salieron de
  `FlexOS_FS`. El sistema ya no crea la carpeta ni la suma en
  `FLEXFS_CAT_APPS`. **Los documentos que un usuario ya tenga en `/Vector` no se
  borran**: siguen en la NOR Flash y se ven desde Archivos.
* `FlexOS_Ultra_NTP.h` pasó a colgar de `FlexOS_Ultra_Network.h` en la cadena
  lineal de módulos, que es el eslabón que quedaba delante.

## Condiciones para volver a integrarlo

1. Devolver los cuatro archivos de `firmware-modules/` a `FlexOS_Ultra/` y
   reponer la cadena con `Ultra_HttpShare` y `AppVector` detrás del eslabón de
   red (hoy `Network → WebServer → NTP`). `FlexOS_HttpShare` y `FlexOS_QR` ya
   están en el firmware activo.
2. Reponer el `#include` portable de `FlexOS_Vector.h` y la llamada a
   `flexShareTick()` en `loop()`, dentro del `if(!gSafeMode)`. Ojo: el puerto
   8080 ya lo usa Flex Web Server; el servicio de un solo archivo tendría que
   usar otro o colgarse del servidor de medios.
3. Reponer `IC_VECTOR` **al final** del enum con el siguiente id libre —nunca
   reutilizando uno anterior— y subir `APP_N`, más la entrada de `APP_REG`,
   `H_VECTOR`, la fila de `APP[]`, la clase de `APP_WEIGHT` y el icono.
4. Reponer `FLEXFS_DIR_VECTOR` / `FLEXFS_EXT_VECTOR` en **las dos** copias de
   `FlexOS_FS` (raíz y `FlexOS_Ultra/`): `check_shared.py` las compara por hash.
5. Reponer en `tests/host/Makefile` la batería `test_vector` y su objeto en
   `test_ino`, y pegar `ino_compile_testVectorPro.cpp.inc` en `ino_compile.cpp`.
6. Volver a validar **en placa** lo que ningún doble de host reproduce: el
   destello a pantalla completa (temporización entre la caché de los dos núcleos
   y la DMA del presentador MIPI-DSI) y la fluidez real a 60 Hz. Está explicado
   en la §11.5 de `docs/FLEX-VECTOR-PRO.md`.

## Pruebas de los módulos archivados

Desde esta carpeta:

```bash
make
```

Compila y ejecuta la batería portable del motor con ASan+UBSan. Valida la lógica
del motor; **no** valida la integración gráfica ni convierte por sí sola el
archivo en una función lista para publicar.
