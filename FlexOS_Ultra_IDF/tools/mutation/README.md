# Mutacion

`tools/automut.py` muta un archivo (operadores de comparacion, logicos y aritmeticos, y constantes) y ejecuta un runner por mutante; los runners de aqui
cubren la OTA. La regla del proyecto: cada fallo de las pruebas se mata, o se razona por que el mutante es equivalente.

| Runner | Que prueba | Archivos que se mutan |
|---|---|---|
| `qota.sh` | `test_ota.c` contra `FlexOS_OTA.cpp` extraido (4 configuraciones) | `components/flex_ota/src/flex_ota.c`, `flex_ota_model.c` |
| `qport.sh` | `test_ota_port.c`: el transporte de la placa con dobles de ESP-IDF | `components/flex_ota/src/flex_ota_port.c` |
| `qsimota.sh` | la escena `ota` del simulador | `flex_ota_ui.c` y el pegamento (`flex_shell.c`, `flex_qs.c`, `flex_drawer.c`, `flex_factory_ui.c`, `app_settings.c`) |

```bash
# (el runner se ejecuta en tests/host: ruta absoluta)
tools/automut.py components/flex_ota/src/flex_ota.c "bash $PWD/tools/mutation/qota.sh 3" 1000 7
tools/automut.py components/flex_ui/src/shell/flex_ota_ui.c "bash $PWD/tools/mutation/qsimota.sh" 700 51 235 250   # solo las lineas 235-250
```

Lecciones (pagadas):
* El runner TIENE que acabar siempre: `timeout` dentro (un mutante en bucle dejaba un proceso huerfano que bloqueaba el binario y hacia pasar por "no compila"
  a todos los siguientes).
* Una sola ejecucion por arbol: cada mutante compila lo que haya en el arbol, asi que dos mutaciones a la vez se contaminan. Para paralelizar, un `git worktree`
  por ejecucion (`FLEX_ROOT=<worktree>`).
* Un reinicio del entorno mata la ejecucion y deja el mutante puesto: `automut.py` guarda el original en `<archivo>.automut_orig` y lo restaura al arrancar la siguiente
  (comprobar siempre con `git diff` antes de seguir).
* Los `runtime error` de ASan/UBSan salen como INVALIDO, pero son muertes.
* En colores RGB565 un cambio de 1 unidad que no cruza el cuanto de 5/6 bits es un mutante equivalente; las huellas (CRC de objetos y de pixeles) matan el resto.
