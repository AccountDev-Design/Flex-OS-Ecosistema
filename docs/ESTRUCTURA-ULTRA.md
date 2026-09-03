# Estructura de Flex OS Ultra (ESP32-P4)

`FlexOS_Ultra.ino` era un solo archivo de **37053 líneas**. Ahora es el
**orquestador** del sistema (808 líneas) y todo lo demás vive en 53
módulos `FlexOS_Ultra_*.h` en la misma carpeta del sketch.

Esto es **una reorganización, no un cambio funcional**: el código que se
compila es exactamente el mismo, línea a línea. Ni una función, ni una
pantalla, ni un ajuste se ha quitado o modificado.

---

## 1. Por qué cabeceras y no `.cpp`

En Arduino, cada `.cpp` de la carpeta del sketch es una **unidad de
traducción aparte**. Este sistema comparte cientos de variables globales y
funciones `static` de ámbito de fichero (`fb`, `bbuf`, `gState`, `gDark`,
`gLand`, `drawText`, `present`, `TH()`...). Repartirlo en `.cpp` obligaría
a:

* declarar cada global compartida con `extern` y dejar **una** definición
  en algún sitio (cientos de declaraciones nuevas que pueden
  desincronizarse en silencio);
* renunciar a `static` en casi todas las funciones, o duplicar prototipos;
* revisar a mano el orden de inicialización entre unidades.

Es mucha superficie para un fallo de enlazado, en un proyecto donde el IDE
compila el sketch entero de una sola vez de todas formas. Con cabeceras
incluidas en orden el resultado es **una sola unidad de traducción**, byte
a byte el mismo código que antes, y el IDE de Arduino lo compila sin ningún
ajuste. Es además el patrón que el proyecto **ya usaba**
(`FlexOS_Jumper.h`, `FlexOS_*_Bridge.h`).

Los módulos que sí son código portable y con pruebas propias siguen siendo
`.cpp` de verdad, como hasta ahora: `FlexOS_Media`, `FlexOS_Mem`,
`FlexOS_JPEG`, `FlexOS_Vault`, `FlexOS_Weather`, `FlexOS_FS`, `FlexOS_SD`,
`FlexOS_Browser`, `FlexOS_FlexLink`... Esa frontera no se ha tocado.

## 2. Reglas de los módulos

1. Cada módulo lleva `#pragma once` e **incluye al anterior** de la lista.
   La cadena es lineal (`Types` → ... → `Recovery`), así que el orden queda
   garantizado y no puede haber dependencias circulares.
2. Cada global se **define una sola vez**, en su módulo. No hay `extern` ni
   definiciones repetidas.
3. El orden de la lista es el orden en que el código estaba en el `.ino`:
   una función `static` se sigue definiendo antes de usarse.
4. Añadir un módulo = crearlo, encadenarlo al anterior y ponerlo en la lista
   del `.ino`, en su sitio.
5. Un módulo **no se incluye por su cuenta** desde ningún otro sitio: el
   punto de entrada del sistema es siempre `FlexOS_Ultra.ino`.

## 3. Qué queda en `FlexOS_Ultra.ino`

* La cabecera del proyecto y el mapa del sketch.
* Las librerías del sistema (`Wire`, `Preferences`, `WiFi`, FreeRTOS,
  drivers del P4) y los módulos externos (`FlexOS_SD`, `FlexOS_Media`,
  `FlexOS_OTA`, `FlexOS_FS`, `FlexOS_Vault`, ...).
* La **lista de módulos**, en orden.
* Los **puentes** (`FlexOS_*_Bridge.h`), que siguen yendo al final porque
  necesitan las primitivas gráficas y el teclado ya definidos.
* Los adaptadores de ciclo de vida del navegador y de Flex Store.
* `setup()` y `loop()`.
* La hoja de ruta.

## 4. Los módulos, en orden de inclusión

| Archivo | Líneas | Responsabilidad |
|---|---:|---|
| `FlexOS_Ultra_Types.h`            |   668 | tipos de firma, interruptores maestros y estado temprano |
| `FlexOS_Ultra_HAL.h`              |   434 | panel MIPI-DSI (ST7701) y tactil GT911  -- capa de hardware |
| `FlexOS_Ultra_Gfx.h`              |   568 | motor grafico 480x800: framebuffers PSRAM, DMA2D y primitivas |
| `FlexOS_Ultra_Wallpaper.h`        |   479 | catalogo de fondos, fondo desde imagen real y paleta |
| `FlexOS_Ultra_Theme.h`            |   746 | tema semantico, claro/oscuro, Liquid Glass y superficies |
| `FlexOS_Ultra_Text.h`             |   170 | tipografia base, acentos, reloj vectorial y triangulos |
| `FlexOS_Ultra_Font.h`             |  1922 | fuente Outfit 4bpp (tablas + rasterizador) |
| `FlexOS_Ultra_Icons.h`            |   292 | iconos vectoriales del sistema y enum IC_* de apps |
| `FlexOS_Ultra_Touch.h`            |   377 | gestos de alto nivel y suspension de pantalla |
| `FlexOS_Ultra_Prefs.h`            |   334 | preferencias en NVS, idiomas y ajustes del teclado |
| `FlexOS_Ultra_Session.h`          |   396 | sesiones en LittleFS, modo seguro y restablecimiento |
| `FlexOS_Ultra_Clock.h`            |   202 | reloj del sistema (epoca UTC) y API de NTP |
| `FlexOS_Ultra_Shell.h`            |   224 | enum ST_*, cadenas, splash y OOBE  -- maquina de estados |
| `FlexOS_Ultra_Home.h`             |  1522 | escritorio por paginas, deslizamiento y modo edicion |
| `FlexOS_Ultra_Widgets.h`          |   421 | widgets del escritorio y su refresco de datos |
| `FlexOS_Ultra_HomeCfg.h`          |  1307 | modo personalizacion del inicio y gesto de pellizco |
| `FlexOS_Ultra_AppFramework.h`     |  1043 | marco de app, transiciones, nav inferior y ciclo de vida |
| `FlexOS_Ultra_Core.h`             |  1058 | memoria, multitarea, los tres botones y rendimiento |
| `FlexOS_Ultra_AppSettings.h`      |   779 | app Ajustes |
| `FlexOS_Ultra_AppsBasic.h`        |   463 | Calculadora, Calendario, Bienestar y marco de Galeria |
| `FlexOS_Ultra_DeX.h`              |   690 | Modo PC / DeX: modelo y estado |
| `FlexOS_Ultra_DeXDraw.h`          |  1148 | Modo PC / DeX: dibujo |
| `FlexOS_Ultra_DeXInput.h`         |   619 | Modo PC / DeX: entrada, APP_REG y ciclo de vida |
| `FlexOS_Ultra_QuickPanel.h`       |   895 | panel rapido: catalogo de controles y render |
| `FlexOS_Ultra_QuickPanelGlass.h`  |  1581 | panel rapido: material Liquid Glass cacheado |
| `FlexOS_Ultra_QuickPanelEdit.h`   |   545 | panel rapido: modo edicion |
| `FlexOS_Ultra_Media.h`            |   454 | nucleo de medios: microSD, clasificacion e indice |
| `FlexOS_Ultra_AppMultimedia.h`    |  1378 | app Multimedia (reproductor real) |
| `FlexOS_Ultra_AppCamera.h`        |   213 | app Camara |
| `FlexOS_Ultra_Keyboard.h`         |  1564 | teclado de 4 capas y maquetacion de texto |
| `FlexOS_Ultra_FileKit.h`          |   479 | kit de archivos: menu, nombre, confirmacion y papelera |
| `FlexOS_Ultra_AppNotes.h`         |   545 | app Notas |
| `FlexOS_Ultra_KeyboardSettings.h` |   502 | ajustes del teclado (pantalla propia) |
| `FlexOS_Ultra_AppStorage.h`       |   729 | app Almacenamiento y detalles de memoria |
| `FlexOS_Ultra_AppFiles.h`         |   463 | explorador de archivos |
| `FlexOS_Ultra_AppGames.h`         |    77 | app Juegos (incluye FlexOS_Jumper.h) |
| `FlexOS_Ultra_AppCodeIDE.h`       |   270 | Code IDE: asistente de hardware |
| `FlexOS_Ultra_AppPaint.h`         |   607 | app Paint |
| `FlexOS_Ultra_WeatherKit.h`       |   717 | clima: iconografia y escenas procedurales |
| `FlexOS_Ultra_AppWeather.h`       |  1341 | app Clima y sus dos widgets |
| `FlexOS_Ultra_AppSwitcher.h`      |   579 | App Switcher / Recientes |
| `FlexOS_Ultra_Lock.h`             |   604 | bloqueo de seguridad y modo kiosco |
| `FlexOS_Ultra_AppDrawer.h`        |  1030 | menu contextual del escritorio y caja de aplicaciones |
| `FlexOS_Ultra_Power.h`            |   934 | desbloqueo, suspension y apagado completo |
| `FlexOS_Ultra_Network.h`          |   839 | arranque seguro de la radio y Wi-Fi |
| `FlexOS_Ultra_NTP.h`              |   297 | cliente NTP en su propia tarea |
| `FlexOS_Ultra_Conn.h`             |   359 | conectividad: Wi-Fi / BLE / modo avion |
| `FlexOS_Ultra_Notif.h`            |   385 | isla dinamica: notificaciones |
| `FlexOS_Ultra_AppChrono.h`        |   917 | cronometro: app, capsula y tarjeta |
| `FlexOS_Ultra_System.h`           |   487 | I2C, soltar caches, Optimizar Flex OS y cambio de tema |
| `FlexOS_Ultra_AppGallery.h`       |   768 | Galeria |
| `FlexOS_Ultra_Vault.h`            |  1744 | Flex Vault: interfaz de la Carpeta segura |
| `FlexOS_Ultra_Recovery.h`         |   691 | restablecer datos de fabrica y modo seguro |

## 5. Cómo se comprueba

Desde `tests/host`:

```bash
make ino     # check_protos + check_wiring + compilación del sketch entero
make         # además, enlaza y EJECUTA el sketch (test_ino) y el resto
```

* `check_protos.py` sigue vigilando la regla de auto-prototipado del IDE de
  Arduino sobre el `.ino`. Ahora hay muy poco que vigilar ahí: los tipos
  viven en cabeceras, donde esa regla **no aplica** y el orden lo comprueba
  el propio compilador.
* `check_wiring.py` **expande** el `.ino` siguiendo sus `#include` de
  módulos antes de comprobar nada, de modo que todos los ganchos
  obligatorios se siguen verificando aunque la función se haya movido de
  archivo. Además comprueba la estructura: que no haya un módulo huérfano,
  que ninguno falte, que todos tengan `#pragma once` y que la cadena de
  inclusión siga siendo lineal.
