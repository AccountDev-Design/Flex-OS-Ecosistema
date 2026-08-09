# Pruebas de host del navegador de Flex OS

Compilan y ejecutan **en el PC** el mismo código que va a la placa. No
hace falta `arduino-cli`, ni el core de ESP32, ni la placa.

```bash
cd tests/host
make                # las cuatro baterías (perfil Ultra/P4)
make all-boards     # el código de dispositivo en los tres perfiles
make tools          # jpegcheck, que usa la prueba e2e del servicio
make clean
```

## Qué hay aquí

```
tests/
  host/
    test_jpeg.cpp      decodificador JPEG contra libjpeg-turbo
    test_browser.cpp   omnibox, validación de URL, codec FBP/1, SHA-1/base64
    test_app.cpp       código de dispositivo: ciclo de vida, dibujo, capacidades
    test_bridge.cpp    el puente, contra un .ino simulado
    jpegcheck.cpp      herramienta: decodifica un JPEG con el decodificador del firmware
    stub/              entorno Arduino simulado (ver stub/README.md)
    Makefile
  tools/
    make-fixtures.js   genera tests/fixtures (necesita Node + sharp)
  fixtures/            imágenes de prueba y sus referencias, versionadas
```

## Por qué se prueba así

* **El decodificador JPEG** no se compara con números escritos a mano
  sino con lo que produce **libjpeg-turbo** sobre las mismas imágenes.
  Las tolerancias se miden en *pasos de RGB565*, no en niveles de 8
  bits: un paso es el mínimo que la pantalla puede representar, así que
  la métrica distingue un fallo real del redondeo normal.
* **Los ficheros de prueba están versionados** para que la batería se
  pueda ejecutar sin red y sin Node. `tools/make-fixtures.js` solo hace
  falta para regenerarlos.
* **Los casos de reinicio (DRI) y los submuestreos 4:2:2 y 4:4:0** los
  produce un codificador mínimo escrito dentro del propio generador:
  libvips no expone el intervalo de reinicio y sharp solo ofrece 4:2:0 y
  4:4:4. Ese codificador se **verifica contra libjpeg-turbo** antes de
  aceptar su salida como referencia (error 0).
* **El código de dispositivo** (FreeRTOS, WiFi, TLS, framebuffer) se
  compila contra `stub/` y se ejecuta con un puente `brHost*` que dibuja
  en un lienzo de memoria. Así se comprueba de verdad que cerrar la app
  libera **todo** y que ni un píxel de la página sale del área de
  contenido — dos cosas que de otro modo solo se verían (o no) en la
  placa.
* **Los tres perfiles de placa** se compilan y ejecutan por separado
  (`make all-boards`): P4, S3 y ESP32 clásico cambian presupuestos de
  memoria, número de pestañas y capacidades.

## Regenerar los ficheros de prueba

```bash
cd tests/tools
npm install
node make-fixtures.js
```

## Resultado esperado

```
=== FlexOS · decodificador JPEG · ficheros en ../fixtures ===
=== 191 comprobaciones, 0 fallos ===
=== FlexOS · nucleo del navegador ===
=== 382 comprobaciones, 0 fallos ===
=== FlexOS · app del navegador (Flex OS Ultra) ===
=== 41 comprobaciones, 0 fallos ===
=== FlexOS · puente del navegador ===
=== 22 comprobaciones, 0 fallos ===
```

Las pruebas del servicio remoto están en `server/` (`npm test`), e
incluyen una de extremo a extremo que decodifica lo que el servicio
envía con `host/build/jpegcheck`, es decir, con el decodificador del
propio firmware.
