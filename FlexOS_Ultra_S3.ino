// #############################################################
// ##  FlexOS Ultra  Â·  ESP32-S3 N16R8  Â·  CAMARA OV2640
// ##  TFT SPI 4.0" 480x320 (ST7796 / ILI9488) Â· XPT2046 tactil
// #############################################################
//
//  QUE ES ESTE ARCHIVO
//  -------------------
//  El MISMO FlexOS Ultra de siempre -mismas apps, mismos gestos,
//  mismo motor grafico, misma logica- PORTADO al ESP32-S3 N16R8
//  con camara OV2640 y una pantalla TFT SPI de 4.0 pulgadas con
//  panel tactil resistivo.
//
//  La logica y las apps siguen siendo las mismas, pero la geometria
//  del S3 se adapta a un lienzo LOGICO de 533x800 en PSRAM. Esa es
//  la proporcion real del panel 320x480: los layouts que dependen de
//  SCR_W ganan ancho util y llenan el TFT sin estirar el resultado.
//  Tambien se reescribe la CAPA DE HARDWARE, es decir, los tres
//  puntos por los que el sistema toca el silicio:
//
//    1) ENCENDER LA PANTALLA  -> flexPanelInit()
//         ANTES: LDO 2.5V + bus MIPI-DSI 2 lanes + panel DPI +
//                tabla DCS del ST7701 (todo eso era del P4).
//         AHORA: bus SPI2 (FSPI) + tabla de init del ST7796
//                (o ILI9488) + backlight por LEDC.
//    2) MOSTRAR COLORES       -> el "presenter" (tarea del core 0)
//         ANTES: esp_lcd_panel_draw_bitmap() sobre el panel DPI,
//                1:1, porque el panel ERA de 480x800.
//         AHORA: flxPanelBlitBand(), que reescala la banda sucia
//                del lienzo logico 533x800 al panel FISICO de
//                320x480 y la sube por SPI con DMA. Ambos ejes
//                usan practicamente el mismo factor, con filtro
//                de caja 2x2 para mantener legible el texto pequeno.
//    3) HACER FUNCIONAR EL TACTIL -> gt* + flexTouchInit()
//         ANTES: GT911 capacitivo por I2C, ya calibrado de fabrica.
//         AHORA: XPT2046 resistivo por SPI -- driver, umbral de
//                presion, promediado, formula de calibracion y
//                claves de NVS TRANSPLANTADOS TAL CUAL desde
//                ArduOS Z Ultra Pro v3.33 (ver el bloque
//                "CAPA DE HARDWARE"). Se traduce de coordenadas
//                fisicas 320x480 a las logicas 533x800 con la
//                misma transformacion por extremos que usa el
//                presenter, asi que imagen y hit-boxes coinciden.
//
//  ADEMAS: la app CAMARA ya no dibuja el patron "SIN SENAL". El
//  hook camCapture() que el proyecto dejaba preparado esta ahora
//  implementado de verdad contra el OV2640 (esp_camera, DVP,
//  frames RGB565 en PSRAM). Zoom digital, EIS, modos y grabacion
//  siguen siendo exactamente el mismo codigo de antes.
//
//  TODO LO DEMAS (motor grafico de alto nivel, fuentes, iconos,
//  gestos, arranque, OOBE, bloqueo, home, apps, ajustes, teclado,
//  Modo PC, kiosco, apagado...) esta INTACTO.
//
//  ENTORNO (Arduino IDE 2.x Â· core arduino-esp32 v3.x):
//    Board:              ESP32S3 Dev Module
//    CPU Frequency:      240 MHz
//    Flash Size:         16MB (128Mb)     Â· Flash Mode: QIO 80MHz
//    PSRAM:              OPI PSRAM        <-- OBLIGATORIO (N16R8)
//    Partition Scheme:   16M Flash (3MB APP/9.9MB FATFS)
//                        o cualquiera con >=3MB de app + SPIFFS
//    USB CDC On Boot:    Enabled (recomendado, deja libre UART0)
//    Arduino Runs On / Events Run On: Core 1 (por defecto)
//
//  LIBRERIA EXTRA NECESARIA: ninguna. esp_camera viene dentro del
//  propio core arduino-esp32 v3.x (ESP32 Camera Driver). No existe
//  un User_Setup.h separado: controlador, rotacion, formato de color,
//  frecuencias y TODOS los pines de TFT/tactil/camara se configuran
//  dentro de este mismo .ino. El driver usa esp_lcd + SPI nativos.
//
//  DEPURACION SIN PC (trabajas solo desde el movil):
//    Si algo peta antes de dibujar, el motivo del ultimo reinicio
//    se muestra en una BANDA FORENSE en pantalla al bootear (abajo
//    del todo). Ver showBootBanner().
//
//  CABLEADO COMPLETO: ver la tabla del bloque "PINES" mas abajo.
// #############################################################

#include <Wire.h>
#include <Preferences.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "driver/spi_master.h"   // bus SPI del TFT + XPT2046 (ESP32-S3: SPI2/FSPI)
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"
#include "esp_heap_caps.h"
#include "esp_system.h"          // esp_reset_reason() para la banda forense
#include "esp_camera.h"          // OV2640 por DVP (driver incluido en arduino-esp32 v3.x)
#include <WiFi.h>                // pila WiFi NATIVA del S3 (ya no hay co-procesador)
#include "esp_task_wdt.h"        // TWDT: esp_task_wdt_reset() en loop()
#include "esp_sleep.h"           // deep sleep real + ext1 como fuente de despertar
#include "driver/gpio.h"         // gpio_hold_en / gpio_deep_sleep_hold_en (congelar pines en sleep)

// SISTEMA GLOBAL DE ACTUALIZACIONES OTA. Solo la API publica: toda la
// logica (red, JSON, descarga por streaming, instalacion e interfaz One
// UI) vive en FlexOS_OTA.cpp, que es comun a las tres placas. La version
// de firmware y la URL del manifiesto se definen DENTRO de esa cabecera
// -- no aqui -- porque el .cpp es una unidad de traduccion aparte y un
// #define hecho en este .ino no llegaria hasta ella.
#include "FlexOS_OTA.h"

// -------------------------------------------------------------
//  Tipos usados como PARAMETRO de alguna funcion (FGlyph en fgPix/
//  drawGlyphScaled, PWin en pcDrawWindow, DexFit en dexHostFit/
//  dexHostScale, Touch en dexHostRun). Van AQUI ARRIBA DEL TODO
//  a proposito: el IDE de Arduino auto-genera prototipos de todas
//  las funciones y los inserta al inicio del archivo compilado; si
//  el tipo se define mas abajo, ese prototipo autogenerado no lo
//  conoce todavia y la compilacion falla con "does not name a type"
//  aunque en este .ino el tipo aparezca "antes" de usarse. Definir
//  estos tipos aqui arriba evita el problema pase lo que pase.
// -------------------------------------------------------------
typedef struct { uint8_t w, h; int8_t bx; int8_t topoff; uint8_t adv; uint16_t off; } FGlyph;
// PWin: estado de una ventana de Modo PC / DeX. Los campos nuevos (min, snap,
// rx/ry/rw/rh) viven AQUI ARRIBA por el mismo motivo que el resto del tipo: el
// IDE de Arduino autogenera los prototipos al inicio del archivo compilado.
//   mini          -> minimizada a la barra de tareas (sigue open). Se llama
//                    "mini" y no "min" porque Arduino define min() como macro.
//   snap          -> SNAP_FREE / mitades / cuadrantes / maximizada
//   rx,ry,rw,rh   -> geometria a la que restaurar al des-anclar
struct PWin { bool open; int x, y, w, h, app; bool mini; uint8_t snap; int rx, ry, rw, rh; };
// DexFit: encaje de una app hospedada dentro del area de cliente de su ventana
// (origen y tamano del contenido con la proporcion respetada, pasos de escalado
// en coma fija 16.16, y tamano del lienzo de la app). Vive aqui por la misma
// razon que PWin: dexHostFit y dexHostScale lo reciben por referencia.
struct DexFit { int ox, oy, ow, oh; uint32_t stepX, stepY; int aw, ah; bool land; bool flex; };
// Touch: estado tactil de alto nivel. Vive aqui porque dexHostRun lo recibe por
// puntero para INYECTAR un toque traducido en una app hospedada.
struct Touch {
  bool down=false, pressed=false, released=false, tap=false, moved=false;
  bool swipeUp=false, swipeDown=false, swipeLeft=false, swipeRight=false;
  int  x=0, y=0, startX=0, startY=0, dx=0, dy=0;
  unsigned long downMs=0, lastMs=0;
};

// #############################################################
// ##  TECLADO FLEXOS  Â·  FASES A-G  Â·  INTERRUPTORES MAESTROS
// ##  ------------------------------------------------------
// ##  Cada fase tiene SU PROPIO interruptor y se puede apagar
// ##  sola, sin tocar las demas (mismo patron que GLASS_*_ON,
// ##  KIOSK_ON o APPLOCK_ON). Si algo falla en la placa: baja
// ##  a 0 de la G hacia la A, recompila y mira cual era.
// ##
// ##    KB_SIZE_CONFIG_ON   Fase A - tamano de teclado configurable
// ##                        (Compacto/Normal/Grande). A 0 el teclado
// ##                        usa los valores fijos de siempre 43/48/4/6.
// ##    KB_MULTITOUCH_ON    Fase B - escritura rapida: la tecla se
// ##                        dispara al TOCAR (por ID de contacto del
// ##                        GT911). A 0 vuelve el disparo al soltar.
// ##    KB_TOOLBAR_ON       Fase C - barra superior de 5 accesos.
// ##    KB_CLIPBOARD_MULTI_ON Fase D - portapapeles de 12 ranuras con
// ##                        pin/borrado. A 0 vuelve el buffer unico.
// ##    KB_SETTINGS_ON      Fase E - pantalla de Ajustes del teclado.
// ##    KB_AUTOCOMPLETE_ON  Fase F - chips de autocompletado (lista
// ##                        local fija, NO es un modelo de IA).
// ##    KB_ANIM_POLISH_ON   Fase G - animaciones (apertura, tecla
// ##                        presionada, chips, toast). A 0 todo
// ##                        sigue funcionando, pero con cortes secos.
// #############################################################
#define KB_SIZE_CONFIG_ON     1
#define KB_MULTITOUCH_ON      1
#define KB_TOOLBAR_ON         1
#define KB_CLIPBOARD_MULTI_ON 1
#define KB_SETTINGS_ON        1
#define KB_AUTOCOMPLETE_ON    1
#define KB_ANIM_POLISH_ON     1

// FASE B - un punto de contacto del GT911 tal cual sale del chip.
//   id     -> track ID que asigna el propio GT911 (byte 7 del bloque).
//             Es lo que permite saber que "este dedo" es el mismo entre
//             frames aunque se muevan las coordenadas.
//   x, y   -> ya en coordenadas de FlexOS (mismos flags SWAP/FLIP que gtPoll)
//   active -> ese hueco del array tiene un dedo vivo en este frame
// Vive aqui arriba, con struct Touch, por la restriccion de ctags: el
// generador de prototipos de Arduino no puede ver un tipo definido a
// mitad de archivo.
struct TouchPoint { int id; int x, y; bool active; };
#define KB_MAXPOINTS 5
static TouchPoint gKbPoints[KB_MAXPOINTS];

// PORT ESP32-S3 - una lectura cruda del panel tactil XPT2046.
// Vive AQUI ARRIBA, y no junto al driver, por la MISMA restriccion de
// ctags que obliga a subir FGlyph, PWin, DexFit, Touch y TouchPoint: es
// el tipo de RETORNO de tsSample(), y el IDE de Arduino autogenera
// "static TsSample tsSample();" y lo inserta al principio del archivo
// compilado. Si el struct se definiera abajo, ese prototipo no lo
// conoceria todavia y la compilacion fallaria con
// "'TsSample' does not name a type" -- aunque en este .ino el tipo
// aparezca "antes" de usarse.
//   valid    -> hay un contacto real y x/y son fiables
//   busy     -> el bus SPI lo tenia el panel; NO es un "sin dedo", es un
//               "no se ha podido mirar" (ver la nota larga en tsSample)
//   x, y     -> valores RAW del ADC, sin mapear todavia
//   pressure -> z1 + 4095 - z2, la metrica de presion de ArduOS
struct TsSample { bool valid; bool busy; uint16_t x, y; int16_t pressure; };

// FASE D - una ranura del portapapeles.
//   pinned -> fijada por el usuario: no se descarta al llenarse y
//             SOBREVIVE al reinicio (se guarda en NVS).
//   used   -> la ranura tiene contenido
//   ts     -> millis() de cuando se copio (para saber cual es la mas vieja)
#define CLIP_SLOTS   12
#define CLIP_TXT_MAX 200
struct ClipItem { char text[CLIP_TXT_MAX]; bool pinned; bool used; uint32_t ts; };
static ClipItem gClip[CLIP_SLOTS];


// =============================================================
// GEOMETRIA LOGICA ADAPTADA AL PANEL DEL S3
// =============================================================
// El panel portrait mide 320x480, proporcion 2:3. Para conservar esa
// proporcion sin franjas ni deformacion, FlexOS S3 compone en 533x800:
// 533/800 es la aproximacion entera mas cercana a 2/3. El presenter
// reduce ambos ejes practicamente por el mismo factor (3/5).
//
// Al ampliar el ancho LOGICO en vez de estirar la imagen terminada,
// los layouts basados en SCR_W se redistribuyen: barras, tarjetas,
// teclado, camara y zonas tactiles usan el ancho adicional de verdad.
#define SCR_W   533
#define SCR_H   800
// Modo horizontal (PC): coords logicas landscape 800x533, rotadas 90 al panel
#define LW      SCR_H
#define LH      SCR_W

// =============================================================
// GEOMETRIA FISICA DEL PANEL  (nuevo en el port a ESP32-S3)
// =============================================================
// El TFT SPI de 4.0" es 480x320 en su orientacion natural (landscape).
// FlexOS es un sistema PORTRAIT, asi que el panel se usa girado:
// MADCTL lo deja en 320 (ancho) x 480 (alto) y el presenter escala
// el lienzo logico 533x800 a esa superficie.
#define PX_W    320         // ancho FISICO del panel en portrait
#define PX_H    480         // alto  FISICO del panel en portrait

// ---- PRESENTACION A PANTALLA COMPLETA, SIN DEFORMAR ------------
// El lienzo 533x800 llena los 320x480 pixeles fisicos. No hay margen
// negro ni un escalado diferente por eje. El mapeo horizontal usa los
// extremos exactos (0..532 -> 0..319); el vertical es 5/3 exacto.
#define PX_CW   PX_W
#define PX_X0   0
#define PX_SY_NUM 5
#define PX_SY_DEN 3

// Filtro de caja 2x2 al reducir. Cuesta 3 operaciones enteras por
// pixel (sin desempaquetar el RGB565) y es lo que hace que la fuente
// de 5x7 se siga LEYENDO despues de encoger. Ponlo a 0 para
// vecino-mas-cercano puro si alguna vez necesitas los ultimos ms.
#define TFT_SMOOTH_SCALE  1

// Filtro de caja 3x3 (FALLBACK OPCIONAL, apagado por defecto).
// Solo entra en juego si TFT_SMOOTH_SCALE tambien vale 1.
//
// CUANDO USARLO: si con el escalado proporcional 2x2 el texto todavia te
// parece "blando". El 3x3 promedia 9 pixeles de origen por pixel de
// salida en vez de 4, asi que suaviza mas el aliasing... pero ojo,
// SUAVIZAR MAS NO ES LO MISMO QUE VER MAS NITIDO: a partir de cierto
// punto un filtro mas ancho emborrona en vez de mejorar. Por eso va
// apagado: pruebalo, comparalo, y quedate con el que mejor veas.
//
// COSTE: 8 promedios por pixel en vez de 3 (~2,5x mas trabajo de
// escalado). En una pantalla completa son unos +12 ms, asi que baja
// de ~16 fps a ~12 fps en repintados grandes. En repintados parciales
// -que son la mayoria- casi no se nota.
#define TFT_SMOOTH_SCALE_STRONG  0

// =============================================================
// PINES  Â·  ESP32-S3 N16R8 + OV2640 + TFT SPI 4.0" + XPT2046
// =============================================================
//
//  REGLA DE ORO DEL REPARTO: la camara OV2640 va soldada en la
//  placa y NO se puede mover, asi que ELLA manda. Ocupa 14 GPIO
//  fijos (4,5,6,7,8,9,10,11,12,13,15,16,17,18). A eso hay que
//  restar los pines que el propio chip se reserva:
//     GPIO 26..32  -> flash SPI interna  (N16R8: 16 MB)
//     GPIO 33..37  -> PSRAM octal        (N16R8: 8 MB)  <-- OJO
//     GPIO 19, 20  -> USB D-/D+
//     GPIO 0,3,45,46 -> strapping
//     GPIO 43,44   -> UART0 (consola serie)
//  Lo que sobra es exactamente lo que usa la pantalla, y encaja.
//
//  â”Œâ”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”
//  â”‚ BUS SPI COMPARTIDO (SPI2 / FSPI) - TFT + tactil          â”‚
//  â”œâ”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”¤
//  â”‚ SCK  (CLK)         GPIO 21   -> TFT SCK  + T_CLK         â”‚
//  â”‚ MOSI (SDI/DIN)     GPIO 47   -> TFT MOSI + T_DIN         â”‚
//  â”‚ MISO (SDO/DO)      GPIO 41   -> solo lo usa el XPT2046   â”‚
//  â””â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”˜
//  El TFT no necesita MISO (nunca se le lee), asi que la linea de
//  retorno la usa unicamente el tactil. Compartir el bus ahorra 3
//  GPIO frente al SPI por software de ArduOS y multiplica por ~40
//  la velocidad de lectura del tactil. Las dos rutas nunca se
//  solapan porque ambas pasan por el mutex flxSpiMux.
#define PIN_TFT_SCK   21
#define PIN_TFT_MOSI  47
#define PIN_TFT_MISO  41

#define PIN_LCD_CS    14    // chip select del TFT
#define PIN_LCD_DC     1    // data/command del TFT
#define PIN_LCD_RST    2    // reset del TFT (ST7796 / ILI9488)
#define PIN_LCD_BL    42    // backlight -> PWM por LEDC (brillo real)

#define PIN_TP_CS     40    // chip select del XPT2046
#define PIN_TP_IRQ     3    // PENIRQ del XPT2046. TIENE que ser un GPIO RTC
                            // (0..21 en el S3) porque es la fuente de despertar
                            // ext1 del apagado completo. GPIO3 es strapping pero
                            // su valor por defecto (pull-down interno) coincide
                            // con el estado de reposo del PENIRQ, asi que no
                            // altera el arranque.

// I2C de la FASE 2 (deteccion automatica de modulos externos: BME280,
// MPU6050...). No es el tactil -el XPT2046 es SPI- pero el barrido de
// hwDetectTick() sigue existiendo igual que siempre y necesita un bus.
// Son los dos ultimos GPIO libres de la placa.
#define PIN_I2C_SDA   38
#define PIN_I2C_SCL   39

// Aliases historicos. El resto del proyecto no los usa ya (el GT911
// I2C desaparecio), pero se mantienen definidos para que cualquier
// referencia antigua siga compilando sin tocar nada.
#define PIN_TP_SDA    PIN_I2C_SDA
#define PIN_TP_SCL    PIN_I2C_SCL

// ---- CAMARA OV2640 (DVP) ------------------------------------
// Pinout de las placas "ESP32-S3 N16R8 Camera Development Board"
// (mismo mapa que las Freenove ESP32-S3-WROOM CAM y compatibles).
// Si tu placa fuera de otra serie, es lo UNICO que habria que
// tocar: el resto del driver es independiente del pinout.
#define CAM_PIN_PWDN   -1   // no cableado en estas placas
#define CAM_PIN_RESET  -1   // no cableado (reset por software del sensor)
#define CAM_PIN_XCLK   15
#define CAM_PIN_SIOD    4   // SCCB SDA
#define CAM_PIN_SIOC    5   // SCCB SCL
#define CAM_PIN_D7     16
#define CAM_PIN_D6     17
#define CAM_PIN_D5     18
#define CAM_PIN_D4     12
#define CAM_PIN_D3     10
#define CAM_PIN_D2      8
#define CAM_PIN_D1      9
#define CAM_PIN_D0     11
#define CAM_PIN_VSYNC   6
#define CAM_PIN_HREF    7
#define CAM_PIN_PCLK   13

// ---- Controlador del TFT -------------------------------------
// ArduOS declaraba "ST7796 4.0\" SPI TFT" en su cabecera, asi que
// ese es el que se transplanta y el que va por defecto. El modulo
// de 4.0"/480x320 tambien se vende con ILI9488: si tu pantalla sale
// en negro o con los colores invertidos, cambia este 1 por un 0 y
// recompila -- no hay que tocar nada mas.
//   ATENCION al ILI9488: ese chip NO admite RGB565 por SPI, exige
//   18 bits (3 bytes por pixel). El driver de abajo lo detecta y
//   convierte al vuelo, a costa de un 50% mas de trafico SPI. Por
//   eso, si tu panel es de verdad ST7796, dejalo en 1: va mas rapido.
#define TFT_DRIVER_ST7796  1

// Reloj del bus SPI del TFT. ES EL AJUSTE QUE MAS AFECTA A LA
// FLUIDEZ GENERAL: todo lo que dibuja el sistema pasa por aqui, asi
// que el tiempo de subida de un cuadro es directamente proporcional a
// este numero. Una pantalla completa son 307.200 bytes:
//     40 MHz -> ~61 ms   (~16 fps de repintado total)
//     60 MHz -> ~41 ms   (~24 fps)   <-- valor por defecto
//     80 MHz -> ~31 ms   (~32 fps)
// Scroll, apertura de apps y vista de camara escalan igual.
//
// POR QUE 60 Y NO 80 POR DEFECTO: los pines de este proyecto van por
// la matriz GPIO (los del IOMUX nativo se los queda la camara), y eso
// aÃ±ade un retardo de propagacion que a 80 MHz empieza a apretar los
// margenes de tiempo con cables de protoboard. 60 MHz funciona en
// practicamente cualquier cableado razonable.
//
// SI TU CABLEADO ES CORTO Y BUENO, PRUEBA 80000000: se nota. La
// prueba es mirar la pantalla, no medir nada -- si aparecen pixeles
// sueltos de colores ("nieve"), rayas horizontales o la imagen sale
// desplazada, tu cableado no da para tanto: vuelve a 60000000, y si
// aun asi falla, a 40000000 o 27000000.
#define TFT_SPI_HZ   60000000
#define TFT_SPI_HOST SPI2_HOST
// Reloj del XPT2046. Su maximo absoluto son 2.5 MHz; 1 MHz da un
// margen comodo con cables largos y sigue siendo ~40x mas rapido
// que el bit-banging con delayMicroseconds(1) de ArduOS.
#define TS_SPI_HZ     1000000

// Filas del panel que se componen en cada trozo antes de mandarlo
// por DMA. 20 filas x 320 px x 2 B = 12,5 KB por buffer, y hay dos
// (ping-pong) -> 25 KB de RAM INTERNA. Con eso el calculo del
// escalado y la transferencia DMA se solapan y el volcado de una
// pantalla entera baja de ~95 ms a ~62 ms.
#define TFT_CHUNK_ROWS  20

// #############################################################
// ##  APAGADO DE PANTALLA  Â·  interruptores maestros
// ##  ------------------------------------------------------
// ##  Cada sub-sistema se desactiva por separado (mismo patron que
// ##  GLASS_SHADOW_ON / KIOSK_ON / APPLOCK_ON) para poder aislar
// ##  cualquier problema en pruebas de hardware sin tocar el resto.
// #############################################################
#define SUSPEND_ON        1   // gesto de suspension (doble-tap 2 dedos) y de despertar (doble-tap 1 dedo)
#define SUSPEND_LOCK_ON   1   // al despertar de una suspension, caer en la pantalla de Bloqueo si el
                              // usuario tiene configurado PIN/contrasena (gLockType > 0). Sin clave
                              // configurada no cambia nada: se vuelve directo a donde estabas.
#define POWEROFF_ON       1   // apagado completo: icono en el Panel Rapido + confirmacion + deep sleep
#define POWEROFF_PIN_ON   1   // KILL-SWITCH de compilacion de la proteccion por PIN del apagado.
                              // El toggle real que ve el usuario vive en Ajustes -> Seguridad
                              // (gPoffPin, persistido en NVS). Con esta constante a 0 la
                              // proteccion no existe ni aunque el toggle este activado.
#define PANEL_DCS_SLEEP_ON 1  // comandos DCS de bajo consumo del panel (0x28 DISPOFF / 0x10 SLPIN).
                              // Ponlo a 0 si tu panel no vuelve limpio de DISPON: el fundido de
                              // backlight por si solo ya deja la pantalla en negro absoluto.

// ---- Gesto de suspension / despertar (doble-tap) ------------------------
#define SUSP_TAP_WINDOW_MS 450  // ventana maxima entre el 1er y el 2o toque del doble-tap
#define SUSP_TAP_GAP_MS    45   // separacion MINIMA real entre los dos toques (filtra rebotes del panel tactil)
#define SUSP_TAP_MAX_MS    600  // duracion maxima de un toque para contar como "tap" (mas = long-press)
#define SUSP_TAP_FRAMES    2    // polls CONSECUTIVOS con n>=2 que confirman un toque de 2 dedos
#define SUSP_FADE_STEP_MS  10   // periodo de cada paso del fundido de backlight (no bloqueante)
#define SUSP_FADE_STEP     6    // puntos de brillo (0..100) por paso -> ~170 ms de fundido completo

// ---- Apagado completo: despertar desde deep sleep ------------------------
//
// PIN DE DESPERTAR (POFF_WAKE_GPIO)
// ---------------------------------
// EN EL PORT AL S3 ESTA RUTA PASA A SER LA BUENA. En la version P4 este pin
// valia -1 porque la linea INT del GT911 nunca estuvo cableada y el sistema
// caia al modo degradado por temporizador (el chip despertaba cada 400 ms solo
// para mirar el tactil, gastando bateria a lo tonto).
//
// El XPT2046 SI tiene una salida dedicada de "hay un dedo encima": PENIRQ.
// Esta cableada de serie en todos los modulos de 4.0" (la etiqueta suele ser
// T_IRQ) y es un NIVEL, no un pulso: se mantiene en BAJO todo el tiempo que
// el dedo esta apoyado. Eso es exactamente lo que ext1 necesita.
//
// RESTRICCION DEL ESP32-S3 (soc_caps.h de ESP-IDF v5.x):
//   Â· SOC_DEEP_SLEEP_SUPPORTED    = 1  -> hay deep sleep real.
//   Â· SOC_PM_SUPPORT_EXT1_WAKEUP  = 1  -> ext1 existe.
//   Â· SOC_RTCIO_PIN_COUNT         = 22 -> ext1 admite GPIO 0..21.
// PIN_TP_IRQ vale 3, dentro del rango, asi que el #if de abajo entra por la
// ruta buena. Si algun dia mueves el tactil a un GPIO >21, el mismo #if cae
// solo al temporizador sin que haya que tocar nada.
#define POFF_WAKE_GPIO    PIN_TP_IRQ  // PENIRQ del XPT2046 (GPIO RTC 0..21)
#define POFF_WAKE_LEVEL    0  // nivel que despierta: 0 = BAJO (PENIRQ con dedo encima), 1 = ALTO
#define POFF_WAKE_POLL_MS 400 // ruta alternativa: cada cuanto despierta el temporizador a mirar el tactil
#define POFF_WAKE_HOLD_MS 3000 // presion sostenida necesaria para completar el arranque (requisito: ~3 s)
#define POFF_WAKE_GATE_MS 4200 // ventana total del filtro de arranque antes de rendirse y volver a dormir

// #############################################################
// ##  ISLA DINAMICA Â· tipos y estado  (FASE 1: solo la isla)
// ##  ------------------------------------------------------
// ##  Se definen ARRIBA (antes de cualquier funcion) a proposito:
// ##  las funciones de la isla toman punteros a estos structs, y
// ##  el auto-prototipado de Arduino (ctags) inserta prototipos al
// ##  principio del sketch. Si los tipos no existieran aun, esos
// ##  prototipos no compilarian. Definiendolos aqui, todo prototipo
// ##  generado ya conoce ModuleType/DetectedModule/Notification.
// #############################################################

// ---- Tipos de modulo (compartidos con la futura deteccion I2C, Fase 2) ----
enum ModuleType {
  MOD_UNKNOWN,
  MOD_ULTRASONIC,   // HC-SR04
  MOD_BME280,       // sensor I2C
  MOD_MPU6050,      // IMU I2C
  MOD_LED,
  MOD_BUTTON,
  MOD_SERVO,
  MOD_I2C_GENERIC
};

// Un modulo detectado (o simulado en Fase 1)
struct DetectedModule {
  ModuleType    type;
  char          name[24];       // "Sensor BME280"
  char          sub[28];        // "I2C 0x76 detectado"
  uint8_t       i2cAddr;        // 0 si no es I2C
  uint8_t       pins[4];        // reservado para Fase 2 (asignacion de pines)
  uint8_t       numPins;
  bool          active;
  unsigned long detectedAt;
};

// ---- Geometria de la isla ----
#define NOTIF_MAX         3
#define NOTIF_MARGIN_X    16
#define NOTIF_CARD_W      (SCR_W - 2 * NOTIF_MARGIN_X)                        // 448
#define NOTIF_CARD_H      64
#define NOTIF_GAP         10
#define NOTIF_RAD         28
#define NOTIF_Y0          56                                                  // borde sup. de la 1a tarjeta
#define NOTIF_ENTER_DROP  24                                                  // caida de la animacion de entrada (px)
#define NOTIF_HOLD_MS     5000                                                // ms visible antes de auto-descartarse
#define NOTIF_BAND_TOP    (NOTIF_Y0 - NOTIF_ENTER_DROP - 6)                   // 26
#define NOTIF_BAND_BOT    (NOTIF_Y0 + NOTIF_MAX * (NOTIF_CARD_H + NOTIF_GAP) + 6) // 284
#define NOTIF_BAND_H      (NOTIF_BAND_BOT - NOTIF_BAND_TOP)                   // 258

// ---- Fase de animacion de cada notificacion ----
enum NotifPhase { NP_IN, NP_IDLE, NP_DRAG, NP_OUT, NP_SPRING };

struct Notification {
  DetectedModule mod;
  bool           active;
  NotifPhase     phase;
  uint32_t       bornMs;        // inicio de la animacion de entrada (se fija al ARMARSE en Home)
  float          slideX;        // desplazamiento horizontal (descarte); 0 = en su sitio
  bool           armed;         // false = encolada pero aun no mostrada; se arma al verse en Home
};

// ---- Estado global de la cola ----
static Notification gNotifs[NOTIF_MAX];
static int          gNotifCount  = 0;
static bool         notifBandOn  = false;   // la banda tiene isla activa (o le debe un ultimo frame de limpieza)
static int          notifDragIdx = -1;      // tarjeta que el dedo esta arrastrando
static uint32_t     notifLastMs  = 0;       // throttle de animacion (~30 fps)
static bool         notifPaused    = false; // true mientras gState != ST_HOME (fases de la isla congeladas)
static uint32_t     notifPauseT0   = 0;     // millis() en que empezo la pausa (ver notifTick)

// ---- Deteccion de hardware I2C (FASE 2) ----
// Disparadores DEMO de la Fase 1 (notificacion falsa al llegar a Home + otra
// por cada tap en la esquina superior derecha).
//
// AHORA EN 0. La Fase 2 ya esta terminada y conectada: hwDetectTick() barre el
// bus I2C de verdad y i2cOnDevicePresent() empuja la notificacion real. Con los
// demos en 1 el sistema anunciaba al arrancar un "Sensor BME280 / I2C 0x76
// detectado", un "MPU6050", un "Servo"... de hardware que NO esta conectado, y
// ademas sacaba otro falso cada vez que tocabas cerca de los iconos de bateria
// y wifi (zona x>=SCR_W-52, y 36..56), sin nada que indicara que ese trozo de
// pantalla fuera pulsable. Eso es lo que hacia que la isla pareciera aleatoria
// y que lo que anunciaba no coincidiera con la placa.
//
// Ponlo en 1 solo si quieres volver a probar la isla sin sensores en el bus.
#define NOTIF_DEMO_TRIGGERS   0

#define MAX_MODULES_DETECTED  8
#define I2C_SCAN_LO           0x08     // rango 7-bit valido (evita direcciones reservadas)
#define I2C_SCAN_HI           0x77
#define I2C_SCAN_PER_TICK     8        // direcciones sondeadas por vuelta de loop (no bloquea)
static const uint32_t I2C_SWEEP_INTERVAL = 3000;   // ms entre barridos completos

static DetectedModule detectedModules[MAX_MODULES_DETECTED];
static int      detectedCount = 0;
static uint16_t modSweepId[MAX_MODULES_DETECTED];  // ultimo barrido en que se vio cada modulo
static uint8_t  i2cScanCursor = 0;                 // direccion actual dentro del barrido
static bool     i2cSweeping   = false;             // hay un barrido en curso
static uint32_t i2cLastSweep  = 0;                 // fin del ultimo barrido
static uint16_t i2cSweepId    = 0;                 // id del barrido (para reconciliar presencia)

// Radio (WiFi NATIVO del ESP32-S3: ya no hay co-procesador). Declarado aqui
// ARRIBA -a proposito- porque Ajustes (mas abajo en el archivo, pero
// ANTES que la seccion de radio al final) necesita leerlo para mostrar
// el estado real de la conexion. La logica de arranque/escaneo/conexion
// vive toda junto a bootInitRadioSafe(), al final del archivo.
#define FLEXOS_ENABLE_WIFI 1
static volatile bool gNetOnline = false;   // true tras un WiFi.begin() exitoso; lo lee la UI (Ajustes, icono, etc.)

// #############################################################
// ##  CAPA DE HARDWARE  Â·  ESP32-S3 N16R8
// ##  ------------------------------------------------------
// ##  Es la UNICA parte del proyecto que ha cambiado al portar
// ##  desde el ESP32-P4. Tiene tres piezas, y cada una conserva
// ##  EL MISMO NOMBRE Y LA MISMA FIRMA que tenia antes, para que
// ##  ni una sola linea del resto del sistema tenga que enterarse:
// ##
// ##    flexPanelInit()   enciende la pantalla
// ##    flxPanelBlitBand()sube pixeles al panel (lo usa el presenter)
// ##    flexTouchInit()   +  gtPoll() / gtPollMulti()   -> tactil
// ##
// ##  La configuracion de la PANTALLA y del TACTIL (controlador,
// ##  bus SPI, tabla de init, umbral de presion, promediado,
// ##  formula de calibracion y claves de NVS) esta TRANSPLANTADA
// ##  desde ArduOS Z Ultra Pro v3.33, que es donde estaba probada
// ##  sobre este mismo modulo de 4.0". No se ha copiado de ArduOS
// ##  absolutamente nada mas: ni apps, ni menus, ni graficos, ni
// ##  logica de sistema.
// #############################################################

// ---- BUS SPI COMPARTIDO: TFT + XPT2046 ----------------------
// Los dos perifÃ©ricos cuelgan del MISMO bus (SPI2/FSPI) con su
// propio chip-select y su propio reloj (40 MHz el TFT, 1 MHz el
// tactil). El driver de ESP-IDF cambia la frecuencia y el modo al
// vuelo en cada transaccion, asi que compartir es gratis.
//
// EL MUTEX ES OBLIGATORIO, no decorativo: el presenter vive en el
// core 0 y el tactil se sondea desde loop() en el core 1. Sin el,
// una lectura del XPT2046 podria colarse en medio de la rafaga de
// DMA de un cuadro y el panel escribiria basura. Las dos rutas
// corren SIEMPRE en contexto de tarea (nunca en ISR), asi que un
// mutex normal es exactamente la primitiva correcta.
static spi_device_handle_t flxTftDev  = NULL;
static spi_device_handle_t flxTsDev   = NULL;
static SemaphoreHandle_t   flxSpiMux  = NULL;
static bool                flxPanelOk = false;

// Trozos de composicion (ping-pong) en RAM INTERNA apta para DMA.
// La PSRAM tambien seria valida en el S3, pero la interna evita
// tener que forzar write-backs de cache y es el doble de rapida de
// leer para el DMA.
#if TFT_DRIVER_ST7796
  #define TFT_BPP 2                 // ST7796: RGB565 nativo por SPI
#else
  #define TFT_BPP 3                 // ILI9488: SOLO admite RGB666 por SPI
#endif
#define TFT_CHUNK_BYTES ((size_t)TFT_CHUNK_ROWS * PX_CW * TFT_BPP)
static uint8_t*          flxChunk[2] = { NULL, NULL };
static spi_transaction_t flxTx[2];
static int               flxTxInFlight = 0;

// LUT de columnas: fisico -> logico. Se calcula UNA vez y ahorra
// una division entera por cada pixel de cada cuadro (320x480 =
// 153.600 divisiones por pantalla completa que ya no se hacen).
static uint16_t flxColMap[PX_CW];

// ---- Callback de DC ------------------------------------------
// El driver SPI lo llama justo antes de mover el primer bit, asi
// que la linea D/C siempre esta en el valor correcto sin carreras.
static void IRAM_ATTR flxTftPreCb(spi_transaction_t* t){
  gpio_set_level((gpio_num_t)PIN_LCD_DC, (int)(intptr_t)t->user);
}

// ---- Primitivas de comando del panel -------------------------
// Van por transferencia POLLING (son unos pocos bytes: encolarlas
// costaria mas que enviarlas). OJO: el driver de ESP-IDF prohibe
// mezclar polling con transacciones encoladas todavia en vuelo, por
// eso todo camino que emite comandos llama antes a tftWaitAll().
static void tftCmd(uint8_t c){
  spi_transaction_t t = {};
  t.length    = 8;
  t.flags     = SPI_TRANS_USE_TXDATA;
  t.tx_data[0]= c;
  t.user      = (void*)0;                    // D/C = 0 -> comando
  spi_device_polling_transmit(flxTftDev, &t);
}
// Los parametros se copian a un buffer de PILA a proposito: las
// tablas de init son 'static const' y viven en FLASH, y el DMA del
// S3 no puede leer de flash. La pila esta en DRAM interna, que si
// es apta para DMA.
static void tftDat(const uint8_t* d, size_t n){
  if(!n) return;
  uint8_t tmp[20] __attribute__((aligned(4)));
  if(n > sizeof(tmp)) n = sizeof(tmp);
  memcpy(tmp, d, n);
  spi_transaction_t t = {};
  t.length    = n * 8;
  t.tx_buffer = tmp;
  t.user      = (void*)1;                    // D/C = 1 -> dato
  spi_device_polling_transmit(flxTftDev, &t);
}
static inline void tftCmdDat(uint8_t c, const uint8_t* d, size_t n){ tftCmd(c); tftDat(d, n); }

// Ventana de escritura + apertura de RAM (0x2C). A partir de aqui
// el controlador auto-incrementa, asi que se pueden mandar tantos
// trozos como haga falta sin repetir la ventana.
static void tftWindow(int x0, int y0, int x1, int y1){
  uint8_t d[4];
  d[0] = x0 >> 8; d[1] = (uint8_t)x0; d[2] = x1 >> 8; d[3] = (uint8_t)x1;
  tftCmdDat(0x2A, d, 4);                     // CASET
  d[0] = y0 >> 8; d[1] = (uint8_t)y0; d[2] = y1 >> 8; d[3] = (uint8_t)y1;
  tftCmdDat(0x2B, d, 4);                     // RASET
  tftCmd(0x2C);                              // RAMWR
}

// ---- Transferencias de pixeles: encoladas (DMA asincrono) ----
static void tftPushAsync(const void* buf, size_t bytes, int slot){
  memset(&flxTx[slot], 0, sizeof(spi_transaction_t));
  flxTx[slot].length    = bytes * 8;
  flxTx[slot].tx_buffer = buf;
  flxTx[slot].user      = (void*)1;          // D/C = 1
  if(spi_device_queue_trans(flxTftDev, &flxTx[slot], portMAX_DELAY) == ESP_OK) flxTxInFlight++;
}
static void tftWaitOne(){
  spi_transaction_t* r = NULL;
  if(flxTxInFlight <= 0) return;
  if(spi_device_get_trans_result(flxTftDev, &r, portMAX_DELAY) == ESP_OK) flxTxInFlight--;
  else flxTxInFlight = 0;                    // no dejar el contador desincronizado jamas
}
static void tftWaitAll(){ while(flxTxInFlight > 0) tftWaitOne(); }

// #############################################################
// ##  TABLAS DE INIT DEL CONTROLADOR
// ##  ------------------------------------------------------
// ##  ArduOS declaraba "ST7796 4.0\" SPI TFT" y lo manejaba con
// ##  TFT_eSPI, cuya configuracion real (controlador, bus, tabla
// ##  de arranque) vive fuera del .ino, dentro de User_Setup.h de
// ##  la libreria. Aqui esa configuracion se trae AL SKETCH: es la
// ##  misma secuencia de arranque que usa TFT_eSPI para el ST7796
// ##  (y la del ILI9488 como alternativa), con el mismo MADCTL de
// ##  rotacion 0 -portrait 320x480- que ArduOS aplicaba con
// ##  tft.setRotation(0). Resultado: la pantalla arranca EXACTA-
// ##  MENTE igual que en ArduOS, pero sin depender de que nadie
// ##  edite a mano un fichero de la libreria.
// #############################################################
typedef struct { uint8_t cmd; uint8_t n; uint8_t delayMs; const uint8_t* d; } TftRow;

#if TFT_DRIVER_ST7796
static const uint8_t s01[] = { 0xC3 };                                   // desbloqueo del juego de comandos
static const uint8_t s02[] = { 0x96 };
static const uint8_t s03[] = { 0x48 };                                   // MADCTL: MX + BGR = rotacion 0 (320x480)
static const uint8_t s04[] = { 0x55 };                                   // COLMOD: RGB565 (16 bits)
static const uint8_t s05[] = { 0x01 };                                   // control de inversion
static const uint8_t s06[] = { 0x80, 0x02, 0x3B };
static const uint8_t s07[] = { 0x40, 0x8A, 0x00, 0x00, 0x29, 0x19, 0xA5, 0x33 };
static const uint8_t s08[] = { 0x06 };
static const uint8_t s09[] = { 0xA7 };
static const uint8_t s10[] = { 0x18 };
static const uint8_t s11[] = { 0xF0, 0x09, 0x0B, 0x06, 0x04, 0x15, 0x2F, 0x54, 0x42, 0x3C, 0x17, 0x14, 0x18, 0x1B };
static const uint8_t s12[] = { 0xE0, 0x09, 0x0B, 0x06, 0x04, 0x03, 0x2B, 0x43, 0x42, 0x3B, 0x16, 0x14, 0x17, 0x1B };
static const uint8_t s13[] = { 0x3C };                                   // rebloqueo
static const uint8_t s14[] = { 0x69 };
static const TftRow TFT_INIT[] = {
  { 0x01, 0, 120, NULL },     // SWRESET
  { 0x11, 0, 120, NULL },     // SLPOUT
  { 0xF0, 1,   0, s01 }, { 0xF0, 1,   0, s02 },
  { 0x36, 1,   0, s03 }, { 0x3A, 1,   0, s04 },
  { 0xB4, 1,   0, s05 }, { 0xB6, 3,   0, s06 },
  { 0xE8, 8,   0, s07 },
  { 0xC1, 1,   0, s08 }, { 0xC2, 1,   0, s09 }, { 0xC5, 1, 120, s10 },
  { 0xE0,14,   0, s11 }, { 0xE1,14, 120, s12 },
  { 0xF0, 1,   0, s13 }, { 0xF0, 1, 120, s14 },
  { 0x20, 0,   0, NULL },     // INVOFF (paneles normalmente-blancos)
  { 0x29, 0,  20, NULL },     // DISPON
};
#else   // ---------------- ILI9488 ----------------
static const uint8_t s01[] = { 0x00,0x03,0x09,0x08,0x16,0x0A,0x3F,0x78,0x4C,0x09,0x0A,0x08,0x16,0x1A,0x0F };
static const uint8_t s02[] = { 0x00,0x16,0x19,0x03,0x0F,0x05,0x32,0x45,0x46,0x04,0x0E,0x0D,0x35,0x37,0x0F };
static const uint8_t s03[] = { 0x17, 0x15 };
static const uint8_t s04[] = { 0x41 };
static const uint8_t s05[] = { 0x00, 0x12, 0x80 };
static const uint8_t s06[] = { 0x48 };                                   // MADCTL: MX + BGR = rotacion 0
static const uint8_t s07[] = { 0x66 };                                   // COLMOD: RGB666 (el ILI9488 NO admite 565 por SPI)
static const uint8_t s08[] = { 0x00 };
static const uint8_t s09[] = { 0xA0 };
static const uint8_t s10[] = { 0x02 };
static const uint8_t s11[] = { 0x02, 0x02 };
static const uint8_t s12[] = { 0x00 };
static const uint8_t s13[] = { 0xA9, 0x51, 0x2C, 0x82 };
static const TftRow TFT_INIT[] = {
  { 0x01, 0, 120, NULL },     // SWRESET
  { 0xE0,15,   0, s01 }, { 0xE1,15,   0, s02 },
  { 0xC0, 2,   0, s03 }, { 0xC1, 1,   0, s04 }, { 0xC5, 3,   0, s05 },
  { 0x36, 1,   0, s06 }, { 0x3A, 1,   0, s07 },
  { 0xB0, 1,   0, s08 }, { 0xB1, 1,   0, s09 }, { 0xB4, 1,   0, s10 },
  { 0xB6, 2,   0, s11 }, { 0xE9, 1,   0, s12 }, { 0xF7, 4,   0, s13 },
  { 0x11, 0, 120, NULL },     // SLPOUT
  { 0x29, 0,  20, NULL },     // DISPON
};
#endif

// ---- Brillo real por PWM del backlight (lo controla el Panel Rapido) ----
// SIN CAMBIOS respecto al P4 salvo el numero de GPIO: en el S3 ledcAttach()
// funciona exactamente igual (LEDC de baja velocidad, 20 kHz, 8 bits).
static int  gBright = 80;      // 0..100
static bool gBlPwm  = false;   // true si el PWM se pudo enganchar
// Ultimo porcentaje REALMENTE escrito al PWM. No es lo mismo que gBright: el
// fundido de la suspension baja el PWM a 0 SIN tocar gBright (para no perder el
// brillo del usuario). Los fundidos arrancan desde aqui, no desde gBright --
// si arrancaran desde gBright, el fundido de vuelta empezaria ya en el valor
// final y la pantalla daria un fogonazo en vez de aparecer poco a poco.
static int  gBlPct  = 80;
static void setBacklight(int pct){
  if(pct < 5) pct = 5; if(pct > 100) pct = 100;
  gBright = pct; gBlPct = pct;
  if(gBlPwm) ledcWrite(PIN_LCD_BL, map(pct, 0, 100, 25, 255));
}
// Escribe el PWM del backlight para los FUNDIDOS. Mismo mecanismo de siempre
// (ledcWrite sobre PIN_LCD_BL); nunca se toca el pin a pelo con digitalWrite
// salvo en el mismo fallback "sin PWM" que ya usa flexPanelInit si ledcAttach
// falla. Dos diferencias deliberadas con setBacklight():
//
//  1) NO toca gBright. El brillo elegido por el usuario sigue intacto durante
//     toda la suspension, asi que restaurarlo al despertar es exacto y gratis.
//  2) Rampa LINEAL 0..100 -> duty 0..255, en vez del mapeo 25..255 de
//     setBacklight. Ese 25 es el suelo que impide dejar la pantalla invisible
//     desde el slider del Panel Rapido, pero para un fundido es justo lo que
//     sobra: por debajo de ese suelo el backlight todavia ilumina, asi que el
//     ultimo paso hasta 0 seria un corte seco en vez de un fundido. Con la
//     rampa lineal el negro se alcanza de verdad y de forma continua.
//     En el extremo alto las dos curvas practicamente coinciden (a 80% dan 204
//     y 209 de duty), y el fundido de vuelta termina llamando a setBacklight()
//     con el valor exacto, asi que no queda ninguna diferencia visible.
static void blWritePct(int pct){
  if(pct < 0) pct = 0; if(pct > 100) pct = 100;
  gBlPct = pct;
  if(!gBlPwm){ digitalWrite(PIN_LCD_BL, pct > 0 ? HIGH : LOW); return; }
  ledcWrite(PIN_LCD_BL, (uint32_t)(pct * 255 / 100));
}

// ---- Relleno solido rapido (solo para el borrado inicial) ----
static void tftFillSolid(int x0, int y0, int x1, int y1, uint16_t color){
  if(x1 < x0 || y1 < y0 || !flxChunk[0]) return;
  int w = x1 - x0 + 1;
  int rowsPerChunk = (int)(TFT_CHUNK_BYTES / ((size_t)w * TFT_BPP));
  if(rowsPerChunk < 1) rowsPerChunk = 1;
#if TFT_BPP == 2
  uint16_t be = (uint16_t)__builtin_bswap16(color);
  uint16_t* p = (uint16_t*)flxChunk[0];
  for(int i = 0; i < w * rowsPerChunk; i++) p[i] = be;
#else
  uint8_t* p = flxChunk[0];
  uint8_t r = (uint8_t)((color >> 11) << 3), g = (uint8_t)(((color >> 5) & 0x3F) << 2), b = (uint8_t)((color & 0x1F) << 3);
  for(int i = 0; i < w * rowsPerChunk; i++){ p[i*3] = r; p[i*3+1] = g; p[i*3+2] = b; }
#endif
  tftWaitAll();
  tftWindow(x0, y0, x1, y1);
  for(int y = y0; y <= y1; y += rowsPerChunk){
    int rows = y1 - y + 1; if(rows > rowsPerChunk) rows = rowsPerChunk;
    tftWaitAll();                                   // un solo buffer -> sin ping-pong aqui
    tftPushAsync(flxChunk[0], (size_t)w * rows * TFT_BPP, 0);
  }
  tftWaitAll();
}

// #############################################################
// ##  ESCALADO 533x800 (logico) -> 320x480 (fisico)
// ##  ------------------------------------------------------
// ##  ESTE es el corazon del port. FlexOS S3 dibuja en un lienzo
// ##  533x800, con la misma proporcion del panel. Aqui se reduce
// ##  uniformemente y sin franjas hasta los 320x480 pixeles reales.
// ##
// ##  POR QUE NO SE DEFORMA: 800 -> 480 es exactamente 3/5.
// ##  En horizontal se reparten los extremos 0..532 sobre 0..319;
// ##  la diferencia frente a 3/5 es menor de una milesima y no
// ##  altera visualmente circulos, texto ni la imagen de camara.
// ##  La LUT se calcula una sola vez y evita coma flotante por pixel.
// ##
// ##  POR QUE FILTRO DE CAJA Y NO VECINO MAS CERCANO: reducir con
// ##  vecino-mas-cercano TIRA filas y columnas enteras (2 de cada 5
// ##  en vertical). La fuente del sistema mide 5x7 px: perder 2 de
// ##  sus 7 filas la vuelve ilegible. Promediando un bloque la
// ##  energia de esas filas se conserva y el texto se sigue leyendo.
// ##
// ##  POR QUE ES BARATO: el promedio de dos RGB565 se hace SIN
// ##  desempaquetar los canales, con la identidad clasica
// ##      avg = ((a^b) & 0xF7DE) >> 1  +  (a & b)
// ##  que son 4 instrucciones enteras. Un pixel de salida cuesta
// ##  3 de esos promedios + un byteswap (8 en el modo 3x3). Cero
// ##  divisiones, cero multiplicaciones, cero coma flotante.
// #############################################################
#define RGB565_AVG(a,b)  ((uint16_t)(((((a) ^ (b)) & 0xF7DEu) >> 1) + ((a) & (b))))

// Compone 'rows' filas FISICAS a partir del lienzo logico. Devuelve
// los bytes escritos en dst.
static size_t flxScaleRows(uint8_t* dst, const uint16_t* src, int py, int rows){
  uint8_t* o = dst;
  for(int r = 0; r < rows; r++){
    int sy0 = ((py + r) * PX_SY_NUM) / PX_SY_DEN;
    if(sy0 > SCR_H - 1) sy0 = SCR_H - 1;
#if TFT_SMOOTH_SCALE && TFT_SMOOTH_SCALE_STRONG
    // ---- Caja 3x3 (fallback opcional) ----------------------------
    // Promedia un bloque de 3x3 pixeles de origen. Como el promedio
    // binario solo sabe hacer medias de DOS, el 3x3 se arma por
    // niveles: primero cada fila (izq+centro, luego el resultado con
    // der), despues las tres filas entre si. No es una media
    // aritmetica exacta de los 9 -los pesos quedan 1/4,1/2,1/4 por
    // eje, que es una campana- y eso es justo lo que interesa: da mas
    // peso al pixel central, asi que suaviza el aliasing sin
    // emborronar tanto como una media plana.
    int sym = sy0 - 1; if(sym < 0) sym = 0;
    int syp = sy0 + 1; if(syp > SCR_H - 1) syp = SCR_H - 1;
    const uint16_t* rm = src + (size_t)sym * SCR_W;
    const uint16_t* r0 = src + (size_t)sy0 * SCR_W;
    const uint16_t* rp = src + (size_t)syp * SCR_W;
    for(int px = 0; px < PX_CW; px++){
      int sx0 = flxColMap[px];
      int sxm = sx0 - 1; if(sxm < 0) sxm = 0;
      int sxp = sx0 + 1; if(sxp > SCR_W - 1) sxp = SCR_W - 1;
      uint16_t a = RGB565_AVG(RGB565_AVG(rm[sxm], rm[sxp]), rm[sx0]);
      uint16_t b = RGB565_AVG(RGB565_AVG(r0[sxm], r0[sxp]), r0[sx0]);
      uint16_t d = RGB565_AVG(RGB565_AVG(rp[sxm], rp[sxp]), rp[sx0]);
      uint16_t c = RGB565_AVG(RGB565_AVG(a, d), b);
  #if TFT_BPP == 2
      *(uint16_t*)o = (uint16_t)__builtin_bswap16(c); o += 2;
  #else
      o[0] = (uint8_t)((c >> 11) << 3); o[1] = (uint8_t)(((c >> 5) & 0x3F) << 2); o[2] = (uint8_t)((c & 0x1F) << 3); o += 3;
  #endif
    }
#elif TFT_SMOOTH_SCALE
    // ---- Caja 2x2 (por defecto) ----------------------------------
    int sy1 = sy0 + 1; if(sy1 > SCR_H - 1) sy1 = SCR_H - 1;
    const uint16_t* r0 = src + (size_t)sy0 * SCR_W;
    const uint16_t* r1 = src + (size_t)sy1 * SCR_W;
    for(int px = 0; px < PX_CW; px++){
      int sx0 = flxColMap[px];
      int sx1 = sx0 + 1; if(sx1 > SCR_W - 1) sx1 = SCR_W - 1;
      uint16_t t = RGB565_AVG(r0[sx0], r0[sx1]);
      uint16_t b = RGB565_AVG(r1[sx0], r1[sx1]);
      uint16_t c = RGB565_AVG(t, b);
  #if TFT_BPP == 2
      *(uint16_t*)o = (uint16_t)__builtin_bswap16(c); o += 2;
  #else
      o[0] = (uint8_t)((c >> 11) << 3); o[1] = (uint8_t)(((c >> 5) & 0x3F) << 2); o[2] = (uint8_t)((c & 0x1F) << 3); o += 3;
  #endif
    }
#else
    const uint16_t* r0 = src + (size_t)sy0 * SCR_W;
    for(int px = 0; px < PX_CW; px++){
      uint16_t c = r0[flxColMap[px]];
  #if TFT_BPP == 2
      *(uint16_t*)o = (uint16_t)__builtin_bswap16(c); o += 2;
  #else
      o[0] = (uint8_t)((c >> 11) << 3); o[1] = (uint8_t)(((c >> 5) & 0x3F) << 2); o[2] = (uint8_t)((c & 0x1F) << 3); o += 3;
  #endif
    }
#endif
  }
  return (size_t)(o - dst);
}

// Sube al panel la banda LOGICA [y0,y1] del lienzo 'src'.
// Sustituto exacto de la antigua llamada a esp_lcd_panel_draw_bitmap():
// misma semantica (banda de filas), mismo caracter sincrono al volver
// (cuando retorna, el DMA ya termino y 'src' se puede reescribir).
static void flxPanelBlitBand(const uint16_t* src, int y0, int y1){
  if(!flxPanelOk || !src) return;
  if(y0 < 0) y0 = 0;
  if(y1 > SCR_H - 1) y1 = SCR_H - 1;
  if(y0 > y1) return;
  // Logico -> fisico. Se redondea hacia fuera por los dos lados: es
  // preferible repintar una fila de mas que dejar media linea sin
  // actualizar (eso si se ve, y ademas se queda pegado).
  int py0 = (y0 * PX_SY_DEN) / PX_SY_NUM;
  int py1 = (y1 * PX_SY_DEN) / PX_SY_NUM + 1;
  if(py0 < 0) py0 = 0;
  if(py1 > PX_H - 1) py1 = PX_H - 1;
  if(py0 > py1) return;

  xSemaphoreTake(flxSpiMux, portMAX_DELAY);
  tftWaitAll();
  tftWindow(PX_X0, py0, PX_X0 + PX_CW - 1, py1);
  int slot = 0;
  for(int py = py0; py <= py1; py += TFT_CHUNK_ROWS){
    int rows = py1 - py + 1; if(rows > TFT_CHUNK_ROWS) rows = TFT_CHUNK_ROWS;
    // Antes de reutilizar un buffer hay que asegurarse de que su DMA
    // anterior ya acabo. Con dos buffers basta con esperar uno.
    if(flxTxInFlight >= 2) tftWaitOne();
    size_t bytes = flxScaleRows(flxChunk[slot], src, py, rows);
    tftPushAsync(flxChunk[slot], bytes, slot);
    slot ^= 1;
  }
  tftWaitAll();
  xSemaphoreGive(flxSpiMux);
}

// ---- BUS SPI COMPARTIDO: arranque independiente --------------
// Vive en su propia funcion -y no dentro de flexPanelInit()- por un
// motivo concreto: el FILTRO DE ARRANQUE del apagado completo
// (poffWakeGate) necesita leer el tactil ANTES de encender el panel,
// para poder volver a dormirse sin que el usuario llegue a ver un
// destello. Con el bus separado, ese filtro levanta solo lo que
// necesita (SPI + XPT2046) y deja el panel y el backlight apagados.
// Es idempotente: llamarla dos veces no hace nada la segunda.
static bool flxSpiBusInit(){
  static bool busUp = false;
  if(busUp) return true;
  if(!flxSpiMux) flxSpiMux = xSemaphoreCreateMutex();
  // Los pines NO son los del IOMUX nativo de SPI2 -esos (10..13) se los
  // queda la camara-, asi que se rutan por la matriz GPIO. Es
  // perfectamente valido; el unico efecto es que conviene no pasar de
  // ~40 MHz, que es justo el valor por defecto de TFT_SPI_HZ.
  spi_bus_config_t bus = {};
  bus.mosi_io_num     = PIN_TFT_MOSI;
  bus.miso_io_num     = PIN_TFT_MISO;
  bus.sclk_io_num     = PIN_TFT_SCK;
  bus.quadwp_io_num   = -1;
  bus.quadhd_io_num   = -1;
  bus.max_transfer_sz = (int)TFT_CHUNK_BYTES + 64;
  if(spi_bus_initialize(TFT_SPI_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK){
    Serial.println(F("[HW] ERROR: spi_bus_initialize"));
    return false;
  }
  spi_device_interface_config_t dev = {};
  dev.clock_speed_hz = TFT_SPI_HZ;
  dev.mode           = 0;
  dev.spics_io_num   = PIN_LCD_CS;
  dev.queue_size     = 4;
  dev.pre_cb         = flxTftPreCb;
  dev.flags          = SPI_DEVICE_NO_DUMMY;
  if(spi_bus_add_device(TFT_SPI_HOST, &dev, &flxTftDev) != ESP_OK){
    Serial.println(F("[HW] ERROR: spi_bus_add_device (TFT)"));
    return false;
  }
  // El XPT2046 comparte bus pero NO comparte nada mas: reloj lento,
  // sin callback de D/C (no tiene esa linea) y su propio CS.
  spi_device_interface_config_t ts = {};
  ts.clock_speed_hz = TS_SPI_HZ;
  ts.mode           = 0;
  ts.spics_io_num   = PIN_TP_CS;
  ts.queue_size     = 1;
  if(spi_bus_add_device(TFT_SPI_HOST, &ts, &flxTsDev) != ESP_OK){
    Serial.println(F("[HW] ERROR: spi_bus_add_device (XPT2046)"));
    return false;
  }
  busUp = true;
  return true;
}

// ---- BRING-UP DE LA PANTALLA --------------------------------
static bool flexPanelInit(){
  // Backlight apagado durante el init (evita el flash blanco)
  pinMode(PIN_LCD_BL, OUTPUT);
  digitalWrite(PIN_LCD_BL, LOW);

  if(!flxSpiMux) flxSpiMux = xSemaphoreCreateMutex();

  // 1) LUT de columnas + buffers de composicion (una sola vez)
  if(!flxChunk[0]){
    for(int px = 0; px < PX_CW; px++){
      int sx = (int)(((int64_t)px * (SCR_W - 1) + (PX_CW - 1) / 2) / (PX_CW - 1));
      if(sx > SCR_W - 1) sx = SCR_W - 1;
      flxColMap[px] = (uint16_t)sx;
    }
    for(int i = 0; i < 2; i++){
      flxChunk[i] = (uint8_t*)heap_caps_malloc(TFT_CHUNK_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
      if(!flxChunk[i]){ Serial.println(F("[HW] ERROR: sin RAM interna DMA para el TFT")); return false; }
    }
  }

  // 2) D/C y RST como salidas normales (no las maneja el driver SPI)
  gpio_config_t io = {};
  io.pin_bit_mask = (1ULL << PIN_LCD_DC) | (1ULL << PIN_LCD_RST);
  io.mode         = GPIO_MODE_OUTPUT;
  gpio_config(&io);
  gpio_set_level((gpio_num_t)PIN_LCD_DC, 0);

  // 3) Bus SPI2 (FSPI) con sus dos dispositivos.
  if(!flxSpiBusInit()) return false;

  // 4) Reset fisico del controlador
  gpio_set_level((gpio_num_t)PIN_LCD_RST, 1); delay(10);
  gpio_set_level((gpio_num_t)PIN_LCD_RST, 0); delay(20);
  gpio_set_level((gpio_num_t)PIN_LCD_RST, 1); delay(150);

  // 5) Tabla de arranque del vendor (ST7796 o ILI9488)
  flxPanelOk = true;                        // ya se puede hablar con el panel
  for(size_t i = 0; i < sizeof(TFT_INIT)/sizeof(TFT_INIT[0]); i++){
    tftCmdDat(TFT_INIT[i].cmd, TFT_INIT[i].d, TFT_INIT[i].n);
    if(TFT_INIT[i].delayMs) delay(TFT_INIT[i].delayMs);
  }

  // 6) Panel entero a negro ANTES de encender el backlight. Incluye las
  //    dos franjas laterales del modo AJUSTAR, que el presenter no vuelve
  //    a tocar nunca (por eso se pintan aqui una sola vez).
  tftFillSolid(0, 0, PX_W - 1, PX_H - 1, 0x0000);

  gBlPwm = ledcAttach(PIN_LCD_BL, 20000, 8);   // backlight ON con brillo PWM
  if(gBlPwm) setBacklight(gBright);
  else digitalWrite(PIN_LCD_BL, HIGH);         // fallback: encendido fijo
#if TFT_DRIVER_ST7796
  Serial.printf("[HW] Panel ST7796 SPI %dx%d @ %d MHz OK (lienzo logico %dx%d)\n",
                PX_W, PX_H, TFT_SPI_HZ / 1000000, SCR_W, SCR_H);
#else
  Serial.printf("[HW] Panel ILI9488 SPI %dx%d @ %d MHz OK, RGB666 (lienzo logico %dx%d)\n",
                PX_W, PX_H, TFT_SPI_HZ / 1000000, SCR_W, SCR_H);
#endif
  return true;
}

// ---- Comandos DCS de bajo consumo ---------------------------
// Identicos a los del P4 (0x28 / 0x29 / 0x10 son estandar MIPI-DCS y
// los implementan igual el ST7701, el ST7796 y el ILI9488); lo unico
// que cambia es el canal por el que viajan: antes DBI sobre DSI, ahora
// SPI. Toda la logica de suspension y apagado sigue intacta.
//
//   0x28 DISPOFF -> el panel deja de driver la matriz. Reversible al instante
//                   con 0x29 y SIN reinicializar la tabla del vendor. Es lo que
//                   usa la SUSPENSION.
//   0x10 SLPIN   -> ademas apaga el generador interno. Mas ahorro, pero salir
//                   pide 0x11 + 120 ms. Solo se usa en el APAGADO COMPLETO,
//                   donde el chip se va a deep sleep y al volver flexPanelInit()
//                   rehace el panel entero de todas formas -> riesgo cero.
//
// SIEMPRE se llaman DESPUES de que el backlight haya llegado a 0, nunca antes:
// asi el usuario no llega a ver el efecto del panel entrando en el modo.
static void panelDisplayOff(){
#if PANEL_DCS_SLEEP_ON
  if(!flxPanelOk) return;
  xSemaphoreTake(flxSpiMux, portMAX_DELAY);
  tftWaitAll(); tftCmd(0x28);                                            // DISPOFF
  xSemaphoreGive(flxSpiMux);
#endif
}
static void panelDisplayOn(){
#if PANEL_DCS_SLEEP_ON
  if(!flxPanelOk) return;
  xSemaphoreTake(flxSpiMux, portMAX_DELAY);
  tftWaitAll(); tftCmd(0x29);                                            // DISPON
  xSemaphoreGive(flxSpiMux);
#endif
}
static void panelSleepIn(){
#if PANEL_DCS_SLEEP_ON
  if(!flxPanelOk) return;
  xSemaphoreTake(flxSpiMux, portMAX_DELAY);
  tftWaitAll();
  tftCmd(0x28);                                                          // DISPOFF
  tftCmd(0x10);                                                          // SLPIN
  xSemaphoreGive(flxSpiMux);
#endif
}

// #############################################################
// ##  DRIVER TACTIL: XPT2046 resistivo por SPI
// ##  ------------------------------------------------------
// ##  TRANSPLANTADO DE ArduOS Z Ultra Pro v3.33 (bloque
// ##  "DRIVER TOUCH - XPT2046 Software SPI" + "CALIBRACION TOUCH
// ##  IN-DEVICE"). Se conserva LITERALMENTE:
// ##    Â· los bytes de control  0xD0 / 0x90 / 0xB0 / 0xC0
// ##    Â· la ventana de bits leida (ver nota en tsRaw)
// ##    Â· el umbral de presion 900 (z1 + 4095 - z2)
// ##    Â· el muestreo de 6 lecturas con descarte de min y max
// ##    Â· la ventana de validez 150..3950
// ##    Â· las formulas map() de calibracion y su extrapolacion
// ##    Â· las claves de NVS "TXM" / "TXX" / "TYM" / "TYX"
// ##    Â· los valores por defecto 153 / 1919 / 1966 / 150
// ##
// ##  LO UNICO QUE CAMBIA es el transporte: donde ArduOS movia
// ##  los bits a mano con digitalWrite + delayMicroseconds(1)
// ##  (~200 us por lectura, y son 14 lecturas por sondeo), aqui
// ##  van por el SPI hardware a 1 MHz (~24 us por sondeo entero).
// ##  Eso libera unos 2,5 ms de CPU en CADA vuelta de loop(), que
// ##  es justo lo que necesita el presenter para no perder cuadros.
// ##
// ##  ENVOLTORIO gt*: el resto de FlexOS llama a gtPoll() y
// ##  gtPollMulti() -nombres heredados del GT911- desde una docena
// ##  de sitios. Se mantienen tal cual, con la misma firma y el
// ##  mismo contrato (coordenadas ya en el espacio LOGICO 533x800),
// ##  para que ni el motor de gestos, ni el teclado, ni el kiosco,
// ##  ni la suspension tengan que cambiar una sola linea.
// #############################################################

// Flags de orientacion, por si tu lote de panel sale espejado.
#define TS_SWAP_XY 0
#define TS_FLIP_X  0
#define TS_FLIP_Y  0

// ---- SENSIBILIDAD: umbral de presion ------------------------
// Metrica de ArduOS: presion = z1 + 4095 - z2. Sube cuanto mejor es
// el contacto del dedo con el panel.
//
// ArduOS traia 900 (el mismo que llevaba desde la v3.28, cuando lo
// subieron desde 600 porque 600 dejaba pasar demasiados toques
// fantasma). Aqui se deja EN 900 a proposito y no mas alto: el
// remedio de verdad contra los falsos toques es el DEBOUNCE de dos
// lecturas consecutivas de mas abajo (TS_DEBOUNCE_READS), que filtra
// los picos sin pedirle al usuario que apriete mas fuerte.
//
// SI AUN ASI TE SALEN FALSOS TOQUES: sube este numero a 1000 y luego
// a 1100. Cada escalon exige apretar un poco mas. Pasado ~1300 el
// panel empieza a "no responder" con toques suaves legitimos, asi que
// no subas mas de ahi: si a 1300 sigue habiendo fantasmas, el problema
// es de cableado (T_IRQ sin pull-up, cables largos junto al bus del
// TFT) y no de umbral.
#define TS_PRESSURE_MIN   900

// Lecturas VALIDAS CONSECUTIVAS necesarias para dar un toque por bueno.
// 1 = comportamiento de ArduOS (un solo sondeo bastaba).
// 2 = por defecto aqui. Cuesta ~5 ms de latencia (una vuelta de loop)
//     y elimina practicamente todos los toques fantasma, porque un
//     pico electrico dura un sondeo, nunca dos seguidos.
// 3 = mas estricto todavia, pero ya se empieza a notar el retardo.
#define TS_DEBOUNCE_READS 2

// ---- ZONA MUERTA DE ARRASTRE --------------------------------
// El XPT2046 tiene jitter propio de +-1..2 px logicos incluso con el
// dedo COMPLETAMENTE quieto (es un ADC resistivo, no un sensor
// capacitivo con filtro interno). Sin esta zona muerta, ese ruido
// entra en el scroll como micro-saltos hacia delante y hacia atras:
// eso es exactamente la sensacion de "el scroll se traba".
// Se mide en pixeles LOGICOS (533x800) desde el punto donde bajo el
// dedo; hasta superarla, el toque no cuenta como movimiento.
#define TS_DRAG_DEADZONE  3

// Calibracion (globales, como en ArduOS v3.31+, para poder recalibrar
// en el propio dispositivo). Los valores por defecto son los que ArduOS
// traia probados para el ST7796 + XPT2046 estandar.
#define TS_DEF_XMIN 153
#define TS_DEF_XMAX 1919
#define TS_DEF_YMIN 1966
#define TS_DEF_YMAX 150
static int16_t T_XMIN = TS_DEF_XMIN;
static int16_t T_XMAX = TS_DEF_XMAX;
static int16_t T_YMIN = TS_DEF_YMIN;
static int16_t T_YMAX = TS_DEF_YMAX;
// Ultimos RAW capturados (los usa la pantalla de calibracion)
static uint16_t tLastRawX = 0;
static uint16_t tLastRawY = 0;

// Compatibilidad hacia atras con el codigo que hablaba del GT911:
//   gtOk       -> ahora significa "hay tactil operativo"
//   gtAddr     -> ya no hay tactil en I2C; se deja en 0 para que
//                 i2cIsTouch() siga existiendo y no filtre nada real
//   gtFingers  -> numero de contactos del ultimo sondeo valido
static uint8_t  gtAddr      = 0x00;
static bool     gtOk        = false;
static bool     gI2cOk      = false;   // bus I2C de modulos externos (FASE 2)
static uint8_t  gtFingers   = 0;
static uint32_t gtFingersMs = 0;

// ---- EMULACION DEL GESTO DE 2 DEDOS --------------------------
// El gesto de SUSPENDER de FlexOS es un doble-toque con DOS dedos, y
// un panel resistivo de 4 hilos como este es fisicamente incapaz de
// reportar dos contactos: los dos dedos se leen como uno solo en el
// punto medio. Para no dejar muerta una funcion del sistema, se usa
// la unica senal que SI cambia de forma medible al apoyar dos dedos:
// la presion. Dos contactos ponen las dos resistencias en paralelo y
// el valor de z sube claramente por encima del de un dedo normal.
//
// Es una heuristica, y se declara como tal:
//   Â· Si te suspende la pantalla sin querer, SUBE el umbral (o pon
//     TS_2F_EMULATE_ON a 0 y el gesto simplemente no existira).
//   Â· Un falso positivo es inofensivo: un doble-toque de un dedo
//     vuelve a encender la pantalla al instante.
// El codigo del gesto (suspGestureUpdate) NO se ha tocado: sigue
// leyendo gtFingers exactamente igual que con el GT911.
#define TS_2F_EMULATE_ON  1
#define TS_2F_PRESSURE    3200

// ---- Lectura cruda de un canal del XPT2046 -------------------
// NOTA SOBRE LA VENTANA DE BITS (importante, es deliberado):
// ArduOS emitia los 8 bits de control y, acto seguido, muestreaba 12
// flancos de subida empezando por el reloj 9. Segun la hoja de datos
// el reloj 9 todavia es el ciclo BUSY y el bit DB11 no aparece hasta
// el reloj 10, asi que el valor que ArduOS obtiene esta desplazado un
// bit (viene a ser el valor real dividido por dos). Eso explica que
// sus constantes de calibracion sean de media escala (153..1919 en vez
// de 306..3838).
// Aqui se reproduce EXACTAMENTE esa misma ventana -por eso el >> 4 y
// no el >> 3 canonico- para que TODAS las constantes transplantadas
// (umbral 900, ventana 150..3950, calibraciones por defecto y las que
// el usuario ya tuviera guardadas en NVS) sigan siendo validas tal
// cual. La resolucion efectiva son 11 bits sobre 320/480 px, que sigue
// siendo unas 4 veces mas fina que un pixel.
static uint16_t tsRaw(uint8_t cmd){
  if(!flxTsDev) return 0;
  // Los buffers van alineados a 4 bytes y con holgura: el DMA del SPI
  // escribe la recepcion en palabras completas, asi que un rx de 3 bytes
  // sin alinear seria un desbordamiento silencioso de pila.
  uint8_t tx[4] __attribute__((aligned(4))) = { cmd, 0x00, 0x00, 0x00 };
  uint8_t rx[4] __attribute__((aligned(4))) = { 0, 0, 0, 0 };
  spi_transaction_t t = {};
  t.length    = 24;
  t.rxlength  = 24;
  t.tx_buffer = tx;
  t.rx_buffer = rx;
  if(spi_device_polling_transmit(flxTsDev, &t) != ESP_OK) return 0;
  uint16_t word = (uint16_t)(((uint16_t)rx[1] << 8) | rx[2]);
  return (uint16_t)((word >> 4) & 0x0FFF);
}

// Sondeo completo con el mutex del bus tomado una sola vez (14
// transacciones seguidas sin soltar el bus = menos latencia y cero
// riesgo de colarse en medio de un cuadro).
// El tipo TsSample que devuelve esta declarado ARRIBA DEL TODO del
// archivo, junto a FGlyph/PWin/DexFit/Touch y por el mismo motivo que
// ellos: es el tipo de RETORNO de esta funcion, y el prototipo que
// autogenera el IDE de Arduino se inserta antes de este punto.
static TsSample tsSample(){
  TsSample s = { false, false, 0, 0, 0 };
  if(!flxTsDev) return s;
  if(digitalRead(PIN_TP_IRQ) != LOW) return s;      // sin dedo: ni se toca el bus

  if(xSemaphoreTake(flxSpiMux, pdMS_TO_TICKS(8)) != pdTRUE){ s.busy = true; return s; }
  tftWaitAll();                                     // nunca en medio de una rafaga del panel
  // Verificacion de presion (Z): Z1-Z2, rechaza toques fantasma.
  uint16_t z1 = tsRaw(0xB0);
  uint16_t z2 = tsRaw(0xC0);
  int16_t pressure = (int16_t)(z1 + 4095 - z2);
  // Umbral TS_PRESSURE_MIN (900 por defecto, exactamente el de ArduOS
  // v3.28+, subido en su dia desde 600 porque 600 dejaba pasar demasiados
  // toques fantasma). Ver el bloque de SENSIBILIDAD para ajustarlo.
  if(pressure < TS_PRESSURE_MIN){ xSemaphoreGive(flxSpiMux); return s; }

  // Muestrear 6 veces, descartar outliers, promediar (igual que ArduOS)
  uint16_t sx_arr[6], sy_arr[6];
  uint8_t g = 0;
  for(uint8_t i = 0; i < 6; i++){
    uint16_t ry = tsRaw(0xD0);
    uint16_t rx = tsRaw(0x90);
    if(rx > 150 && rx < 3950 && ry > 150 && ry < 3950){ sx_arr[g] = rx; sy_arr[g] = ry; g++; }
  }
  xSemaphoreGive(flxSpiMux);
  if(g < 3) return s;

  uint32_t sx = 0, sy = 0;
  uint16_t minx = 65535, maxx = 0, miny = 65535, maxy = 0;
  for(uint8_t i = 0; i < g; i++){
    sx += sx_arr[i]; sy += sy_arr[i];
    if(sx_arr[i] < minx) minx = sx_arr[i];
    if(sx_arr[i] > maxx) maxx = sx_arr[i];
    if(sy_arr[i] < miny) miny = sy_arr[i];
    if(sy_arr[i] > maxy) maxy = sy_arr[i];
  }
  if(g >= 4){ sx -= (minx + maxx); sy -= (miny + maxy); g -= 2; }
  s.valid    = true;
  s.x        = (uint16_t)(sx / g);
  s.y        = (uint16_t)(sy / g);
  s.pressure = pressure;
  tLastRawX  = s.x;
  tLastRawY  = s.y;
  return s;
}

// ---- RAW -> coordenadas LOGICAS de FlexOS --------------------
// Dos pasos encadenados, y el segundo es la razon de que ninguna
// hit-box del sistema se haya movido ni un pixel:
//
//   1) RAW -> FISICO (320x480). Es la formula de ArduOS con orient=0,
//      literal:  px = map(avgY, T_XMIN, T_XMAX, 0, 320)
//                py = map(avgX, T_YMIN, T_YMAX, 0, 480)
//      Por eso las constantes de calibracion de ArduOS -y la pantalla
//      de calibracion que las calcula- siguen significando lo mismo.
//
//   2) FISICO -> LOGICO (533x800). Es la INVERSA EXACTA de lo que hace
//      flxPanelBlitBand al pintar. Si el presenter dibuja el pixel
//      logico (lx,ly) en el fisico (16 + lx*3/5, ly*3/5), entonces un
//      toque en (fx,fy) corresponde a ((fx-16)*5/3, fy*5/3). Como las
//      dos transformaciones son la misma razon entera, lo que se ve y
//      lo que se toca coinciden por construccion.
static void tsToLogical(uint16_t rawX, uint16_t rawY, uint16_t &lx, uint16_t &ly){
  long fx = map((long)rawY, T_XMIN, T_XMAX, 0, PX_W);
  long fy = map((long)rawX, T_YMIN, T_YMAX, 0, PX_H);
#if TS_SWAP_XY
  { long t = fx; fx = fy; fy = t; }
#endif
#if TS_FLIP_X
  fx = (PX_W - 1) - fx;
#endif
#if TS_FLIP_Y
  fy = (PX_H - 1) - fy;
#endif
  if(fx < 0) fx = 0; if(fx > PX_W - 1) fx = PX_W - 1;
  if(fy < 0) fy = 0; if(fy > PX_H - 1) fy = PX_H - 1;

  long gx = ((fx - PX_X0) * (long)(SCR_W - 1) + (PX_CW - 1) / 2) / (PX_CW - 1); // fisico -> logico, mismos extremos que el presenter
  long gy = (fy * PX_SY_NUM) / PX_SY_DEN;
  if(gx < 0) gx = 0; if(gx > SCR_W - 1) gx = SCR_W - 1;
  if(gy < 0) gy = 0; if(gy > SCR_H - 1) gy = SCR_H - 1;
  lx = (uint16_t)gx;
  ly = (uint16_t)gy;
}

// ---- Calibracion en NVS (claves de ArduOS) -------------------
// Version 2 corresponde a la geometria 533x800 a pantalla completa.
// Una calibracion anterior se invalida UNA sola vez para evitar que
// coordenadas mal guardadas dejen el equipo sin tactil.
#define TOUCH_CAL_VERSION 2
static void tsCalibLoad(){
  Preferences p;
  p.begin("flexos", true);
  T_XMIN = p.getShort("TXM", TS_DEF_XMIN);
  T_XMAX = p.getShort("TXX", TS_DEF_XMAX);
  T_YMIN = p.getShort("TYM", TS_DEF_YMIN);
  T_YMAX = p.getShort("TYX", TS_DEF_YMAX);
  p.end();
}
static void tsCalibSave(){
  Preferences p;
  p.begin("flexos", false);
  p.putShort("TXM", T_XMIN);
  p.putShort("TXX", T_XMAX);
  p.putShort("TYM", T_YMIN);
  p.putShort("TYX", T_YMAX);
  p.putBool("tcal", true);
  p.putUChar("tcv", TOUCH_CAL_VERSION);
  p.end();
}
static bool tsCalibDone(){
  Preferences p;
  p.begin("flexos", true);
  bool d = p.getBool("tcal", false);
  uint8_t v = p.getUChar("tcv", 0);
  p.end();
  return d && v == TOUCH_CAL_VERSION;
}
// Marca el dispositivo como SIN calibrar. Solo la usa el "Cancelar" de la
// pantalla de calibracion cuando NO habia una calibracion previa: como ahora
// los valores se guardan nada mas terminar las 4 cruces (para poder probarlos
// de verdad), cancelar despues de eso tiene que poder deshacer tambien esa
// marca, o el aparato se quedaria dando por buena una calibracion que el
// usuario acaba de rechazar y no volveria a ofrecerla al arrancar.
static void tsCalibClear(){
  Preferences p;
  p.begin("flexos", false);
  p.putBool("tcal", false);
  p.end();
}

void flexTouchInit(){
  pinMode(PIN_TP_IRQ, INPUT_PULLUP);
  tsCalibLoad();
  gtOk = (flxTsDev != NULL);
  if(!gtOk){ Serial.println(F("[HW] XPT2046 NO disponible (el bus SPI no arranco)")); return; }
  // Una lectura de cortesia: el primer acceso tras encender deja el
  // conversor del XPT2046 en un estado conocido y apaga su referencia.
  xSemaphoreTake(flxSpiMux, portMAX_DELAY);
  tftWaitAll();
  tsRaw(0xD0);
  xSemaphoreGive(flxSpiMux);
  Serial.printf("[HW] Touch XPT2046 (SPI %d kHz) calib X[%d..%d] Y[%d..%d]\n",
                TS_SPI_HZ / 1000, T_XMIN, T_XMAX, T_YMIN, T_YMAX);
}

// Lee un frame del tactil. Devuelve 1=tocando (gx/gy validos),
// 0=soltado, -1=sin datos nuevos. Coords LOGICAS (0..479/0..799).
// MISMO CONTRATO que la version GT911: flexPollTouch() no cambia.
//
// DEBOUNCE EN LA RUTA EN VIVO (contra los falsos toques):
// tsSample() ya filtra bastante -exige que el PENIRQ este bajo, que la
// presion supere TS_PRESSURE_MIN y promedia 6 lecturas descartando el
// maximo y el minimo-, pero todo eso ocurre DENTRO de un unico sondeo.
// Un pico electrico (el backlight conmutando, el bus del TFT acoplando
// en un cable largo, la propia flexion mecanica del panel al apoyar el
// aparato) puede superar las tres cosas a la vez durante ese sondeo.
//
// Lo que NO puede hacer un pico es repetirse identico en el sondeo
// siguiente, ~5 ms despues. Por eso aqui se exige que haya
// TS_DEBOUNCE_READS sondeos VALIDOS CONSECUTIVOS antes de reportar el
// primer "tocando". La pantalla de calibracion ya hacia esto (pedia 4
// lecturas seguidas); ahora lo hace tambien el camino normal.
//
// IMPORTANTE - solo se filtra la ENTRADA, no la permanencia: una vez
// confirmado el toque, cada sondeo valido se reporta al instante. Asi
// el arrastre no pierde ni un frame y el coste del filtro es una unica
// vuelta de loop (~5 ms) al empezar a tocar, que no se percibe.
static uint8_t tsRunLen = 0;        // sondeos validos consecutivos
static int8_t gtPoll(uint16_t &gx, uint16_t &gy){
  if(!gtOk) return -1;
  TsSample s = tsSample();
  if(s.busy) return -1;            // bus ocupado por el panel: sin dato ESTE poll
  if(!s.valid){
    // Sin contacto valido. Se reporta "soltado" (0), que es lo que el
    // GT911 mandaba en su frame de 0 dedos; asi la maquina de gestos
    // cierra el episodio por la via normal y no por el timeout.
    // Ademas, el PENIRQ del XPT2046 sube en cuanto se levanta el dedo,
    // asi que este "soltado" llega ANTES que con el GT911 (que habia que
    // sondear): los taps se sienten mas inmediatos, no menos.
    tsRunLen = 0;
    gtFingers = 0; gtFingersMs = millis();
    return 0;
  }
  if(tsRunLen < 255) tsRunLen++;
  if(tsRunLen < TS_DEBOUNCE_READS){
    // Todavia no hay confirmacion. Se devuelve -1 ("sin dato nuevo") y NO
    // 0: un 0 seria un "soltado" que cortaria un arrastre en curso.
    return -1;
  }
#if TS_2F_EMULATE_ON
  gtFingers = (s.pressure >= TS_2F_PRESSURE) ? 2 : 1;
#else
  gtFingers = 1;
#endif
  gtFingersMs = millis();
  tsToLogical(s.x, s.y, gx, gy);
  return 1;
}

// ---- Lectura "multipunto" (la usa el teclado, Fase B) --------
// El XPT2046 es de UN solo punto por construccion, asi que aqui se
// publica ese unico contacto en la ranura 0 con un track-ID estable.
// La escritura rapida del teclado sigue funcionando (dispara al TOCAR
// en vez de al soltar, que es lo que la hace rapida); lo que no puede
// haber en este panel son dos teclas simultaneas. El Ajuste del
// teclado ya muestra ese dato al usuario ("el panel ha dado N dedos a
// la vez"), asi que la interfaz no miente.
static int gtPollMulti(){
  for(int i = 0; i < KB_MAXPOINTS; i++) gKbPoints[i].active = false;
  if(!gtOk) return -1;
  // Se reutiliza el resultado del sondeo que flexPollTouch() acaba de
  // hacer en esta misma vuelta de loop() (mismo criterio de caducidad
  // de 120 ms que tenia la version GT911): asi no se gasta un segundo
  // acceso al bus por vuelta.
  if(millis() - gtFingersMs > 120) return -1;
  int n = gtFingers > 0 ? 1 : 0;
  if(n){
    uint16_t lx = 0, ly = 0;
    tsToLogical(tLastRawX, tLastRawY, lx, ly);
    gKbPoints[0].id     = 1;                 // un unico dedo -> ID fijo
    gKbPoints[0].x      = (int)lx;
    gKbPoints[0].y      = (int)ly;
    gKbPoints[0].active = true;
  }
  return n;
}

// ---- Bus I2C de modulos externos (FASE 2) -------------------
// El tactil ya NO vive en I2C, pero la deteccion automatica de
// modulos (BME280, MPU6050...) si, y es una funcion del sistema que
// se conserva. Se le dan sus propios dos GPIO libres.
static void flexI2CInit(){
  gI2cOk = Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);
  Serial.printf("[HW] I2C modulos SDA=%d SCL=%d -> %s\n",
                PIN_I2C_SDA, PIN_I2C_SCL, gI2cOk ? "OK" : "no disponible");
}
// #############################################################
// ##  FIN de la capa de hardware
// #############################################################

// #############################################################
// ##  MOTOR GRAFICO NATIVO 533x800  (original de FlexOS)
// ##  Framebuffer en PSRAM + presenter en core 0 + primitivas
// #############################################################

// Tres capas en PSRAM (la placa tiene de sobra):
//   fb       -> lo que el presenter sube al panel
//   lockBuf  -> pantalla de bloqueo pre-renderizada (swipe fluido)
//   homeBuf  -> escritorio pre-renderizado
static uint16_t* fb      = NULL;
static uint16_t* bbuf    = NULL;   // back buffer: se compone el frame aqui y se vuelca de una vez (anti-flicker)
static uint16_t* lockBuf = NULL;
static uint16_t* homeBuf = NULL;

// Destino de dibujo actual (todas las primitivas escriben aqui)
static uint16_t* gBuf = NULL;

// ---- Redireccion del destino de render (hosting de apps en Modo PC) ----
// Toda app hace setBuf(fb) y termina con flxFlush(): esta cableada a la
// pantalla. Para poder ejecutar una app DE VERDAD dentro de una ventana de DeX
// hace falta desviarla a un lienzo propio sin tocar ni una linea de las 16 apps.
// Con gRtTarget != NULL:
//   Â· setBuf(fb) va al lienzo de la ventana (cualquier otro buffer se respeta),
//   Â· flxFlush/present no vuelcan nada al panel, solo anotan que hubo dibujo,
//   Â· fbCopyBand escribe en el lienzo en vez de en fb.
// Asi la app cree que pinta a pantalla completa y en realidad esta pintando su
// propio 480x800 fuera de pantalla, que luego DeX escala dentro del marco.
static uint16_t* gRtTarget = NULL;
static bool      gRtDirty  = false;   // la app pidio volcar algo desde el ultimo reset
static inline void setBuf(uint16_t* b){ gBuf = (gRtTarget && b == fb) ? gRtTarget : b; }

static volatile bool gReady = false;
// Banda de recorte vertical (para listas con scroll). Por defecto: toda la pantalla.
static int gClipY0 = 0, gClipY1 = SCR_H - 1;
static int gClipX0 = 0, gClipX1 = SCR_W - 1;   // recorte horizontal
static volatile int  gDirtyY0 = 0x7FFF, gDirtyY1 = -1;
static portMUX_TYPE  gMux = portMUX_INITIALIZER_UNLOCKED;
// EXCLUSION COMPOSICION <-> SUBIDA AL PANEL.
// El presenter sube fb con flxPanelBlitBand(), que por dentro escala la banda a
// trozos y los manda por DMA de forma ASINCRONA: el DMA sigue LEYENDO fb
// despues de encolar cada trozo (por eso hay una espera de fin de DMA antes de
// reutilizar cada buffer y otra al final). Sin este candado, el hilo de UI podia estar
// escribiendo el cuadro N+1 en fb mientras la DMA todavia leia el cuadro N: la
// mitad de arriba salia con la posicion nueva y la de abajo con la vieja. Ese
// era el "vidrio liquido que se parte en lineas" del Panel Rapido -- y por eso
// dependia de la velocidad del gesto: cuanto mas rapido el dedo, mas distancia
// entre las dos posiciones y mas separadas se veian las costuras.
// El candado lo toma el presenter alrededor de (draw_bitmap + espera de DMA) y
// lo toma tambien todo volcado en bloque a fb (fbCopyBand/blitToFb/present), que
// es por donde pasan TODAS las animaciones compuestas del sistema. Resultado:
// un cuadro nunca se pisa a si mismo a medio subir, a cualquier velocidad.
static SemaphoreHandle_t flxFbMux = NULL;
static inline void fbLock(){   if(flxFbMux) xSemaphoreTake(flxFbMux, portMAX_DELAY); }
static inline void fbUnlock(){ if(flxFbMux) xSemaphoreGive(flxFbMux); }
// Handle del presenter. Sirve para DESPERTARLO en cuanto una banda queda lista,
// en vez de que descubra el trabajo en su siguiente sondeo periodico. Con esto
// baja la latencia de dibujo (antes: hasta ~11 ms de espera muerta) y se elimina
// el micro-stutter que aparecia al desfasar el ritmo de composicion de la UI
// contra la rejilla fija de 11 ms del presenter. NO cambia que pixeles se pintan
// -- solo CUANDO se suben al panel (siempre bandas ya terminadas en fb).
static TaskHandle_t  flxPresenterTask = NULL;

// FASE 4 del Modo Kiosco: se define mucho mas abajo (necesita las primitivas de
// dibujo), pero se declara aqui porque flxFlush -el unico punto por el que TODO
// acaba llegando al panel- tiene que llamarla antes de publicar la banda.
static void kioskStampBadge(int y0, int y1);

static void flxFlush(int y0, int y1){
  if(gRtTarget){ gRtDirty = true; return; }   // app hospedada: no toca el panel
  if(y0 < 0) y0 = 0; if(y1 >= SCR_H) y1 = SCR_H - 1;
  if(y0 > y1) return;
  // El candado del kiosco se estampa ANTES de marcar la banda como sucia: asi
  // ninguna banda llega nunca al presenter sin el, y no puede parpadear aunque
  // la app de encima repinte su esquina en cada frame. Escribe en fb, asi que
  // va bajo el mismo candado que el resto de la composicion (aqui nadie lo
  // tiene tomado todavia: fbCopyBand lo suelta antes de llamar a flxFlush).
  fbLock();
  kioskStampBadge(y0, y1);
  fbUnlock();
  portENTER_CRITICAL(&gMux);
  if(y0 < gDirtyY0) gDirtyY0 = y0;
  if(y1 > gDirtyY1) gDirtyY1 = y1;
  portEXIT_CRITICAL(&gMux);
  // Aviso al presenter. flxFlush SIEMPRE corre en contexto de tarea (nunca en
  // ISR: el fin de DMA lo espera el propio flxPanelBlitBand con el driver SPI),
  // asi que xTaskNotifyGive es correcto. La notificacion de FreeRTOS se "latchea": si
  // llega mientras el presenter todavia no esta bloqueado, se recuerda y el
  // frame no se pierde. Varios avisos seguidos colapsan en un solo despertar que
  // sube la UNION de las bandas sucias ya coalescida arriba -> sin volcados de mas.
  if(flxPresenterTask) xTaskNotifyGive(flxPresenterTask);
}
static inline void flxFlushAll(){ flxFlush(0, SCR_H - 1); }
// Vuelca la banda [y0,y1] del back buffer a fb de una sola pasada y la marca dirty.
// Componer en bbuf y presentar asi evita que el presenter muestre cuadros a medias.
// Copia la banda [y0,y1] de src a fb de una sola pasada por fila completa.
static void fbCopyBand(const uint16_t* src, int y0, int y1){
  if(!src) return;
  if(y0 < 0) y0 = 0;
  if(y1 >= SCR_H) y1 = SCR_H - 1;
  if(y0 > y1) return;
  uint16_t* dst = gRtTarget ? gRtTarget : fb;      // app hospedada: a su lienzo
  if(dst == src) return;
  // Solo hay carrera con la DMA cuando el destino es fb de verdad (el lienzo de
  // una ventana de DeX no lo lee nadie mas).
  bool guard = (dst == fb);
  if(guard) fbLock();
  memcpy(dst + (size_t)y0 * SCR_W, src + (size_t)y0 * SCR_W, (size_t)(y1 - y0 + 1) * SCR_W * 2);
  if(guard) fbUnlock();
}

static void present(int y0, int y1){
  if(y0 < 0) y0 = 0; if(y1 >= SCR_H) y1 = SCR_H - 1; if(y0 > y1) return;
  fbCopyBand(bbuf, y0, y1);
  flxFlush(y0, y1);
}

static void flxPresenter(void*){
  for(;;){
    // Duerme hasta que alguien ensucie una banda (flxFlush -> xTaskNotifyGive).
    // El timeout de 15 ms es una RED DE SEGURIDAD: aunque un aviso nunca deberia
    // perderse (la notificacion se latchea), si por lo que fuera se perdiera, el
    // presenter despierta igual y sube cualquier region pendiente -> jamas se
    // queda un frame "colgado". pdTRUE = limpia la cuenta al salir (se comporta
    // como un semaforo binario). Antes aqui habia un vTaskDelay(11) fijo que
    // dormia SIEMPRE, aunque hubiera un frame listo para subir de inmediato.
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(15));
    if(!gReady) continue;
    int y0, y1;
    portENTER_CRITICAL(&gMux);
    y0 = gDirtyY0; y1 = gDirtyY1;
    gDirtyY0 = 0x7FFF; gDirtyY1 = -1;
    portEXIT_CRITICAL(&gMux);
    if(y1 >= y0){
      // El candado cubre la subida ENTERA (escalado + fin de DMA): mientras el
      // DMA lee fb, ningun volcado de composicion puede reescribirlo debajo.
      // flxPanelBlitBand() no retorna hasta que el ultimo trozo esta enviado,
      // asi que al soltar el candado fb ya se puede reescribir sin riesgo --
      // exactamente el mismo contrato que tenia la version DSI.
      fbLock();
      flxPanelBlitBand(fb, y0, y1);
      fbUnlock();
    }
  }
}

static bool flxGfxInit(){
  size_t bytes = (size_t)SCR_W * SCR_H * 2;
  // Alineados a 64 bytes = tamano de linea de cache de la PSRAM.
  // En el S3 el escalador del presenter LEE bandas parciales de fb
  // (fb + y0*SCR_W) desde PSRAM. Alinear el buffer hace que cada fila
  // (480*2 = 960 bytes, multiplo exacto de 64) empiece tambien en una
  // frontera de linea de cache: el prefetch de la PSRAM trabaja en
  // lineas completas y no se desperdicia media linea por fila. Ademas
  // deja el codigo preparado para volcados por DMA directo desde PSRAM
  // si algun dia se quisiera, que SI exigen esa alineacion.
  //
  // PRESUPUESTO DE PSRAM (N16R8 = 8 MB): 4 lienzos x 480x800x2 =
  // 3,0 MB. La camara pide ~600 KB mas (ver camSensorInit) y la escena
  // de la app Camara otros 750 KB -> ~4,4 MB de pico. Quedan >3 MB
  // libres, asi que no hace falta recortar ningun buffer.
  fb      = (uint16_t*)heap_caps_aligned_alloc(64, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  bbuf    = (uint16_t*)heap_caps_aligned_alloc(64, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  lockBuf = (uint16_t*)heap_caps_aligned_alloc(64, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  homeBuf = (uint16_t*)heap_caps_aligned_alloc(64, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if(!fb || !bbuf || !lockBuf || !homeBuf){
    Serial.println(F("[GFX] ERROR: sin PSRAM para framebuffers"));
    return false;
  }
  memset(fb, 0, bytes);
  setBuf(fb);
  // Antes de arrancar el presenter: si el mutex no existiera, fbLock/fbUnlock
  // son no-ops y el comportamiento seria el de siempre (sin proteccion), nunca
  // un cuelgue.
  flxFbMux = xSemaphoreCreateMutex();
  // Primer volcado en negro
  flxPanelBlitBand(fb, 0, SCR_H - 1);
  gReady = true;
  // El presenter sigue clavado en el CORE 0, igual que en el P4: deja el core 1
  // entero para loop() (tactil, gestos, apps). En el S3 esto importa aun mas,
  // porque ahora el presenter tambien ESCALA, no solo transfiere.
  xTaskCreatePinnedToCore(flxPresenter, "flxPresenter", 4096, NULL, 3, &flxPresenterTask, 0);
  return true;
}

// ---------------- Color ----------------
static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b){
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
// Division exacta por 255 SIN instruccion de division, para v en [0, 65534].
// Identidad clasica: v/255 == (v + 1 + (v>>8)) >> 8 en ese rango.
// Aqui el numerador maximo es 63*255 = 16065 (canal verde, 6 bits), muy por
// debajo del limite, asi que el resultado es BIT A BIT identico al de /255.
// Motivo: la division entera en el RISC-V del P4 cuesta decenas de ciclos y
// mix565 se ejecuta por PIXEL en cada alpha, cada panel de vidrio y cada blur.
#define DIV255(v)  (uint16_t)(((v) + 1u + ((v) >> 8)) >> 8)

// mezcla a<-b con peso t (0..255). t=0 => a, t=255 => b
static inline uint16_t mix565(uint16_t a, uint16_t b, uint8_t t){
  uint32_t ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
  uint32_t br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
  uint32_t it = 255u - t;
  uint32_t rr = ar * it + br * t;
  uint32_t rg = ag * it + bg * t;
  uint32_t rb = ab * it + bb * t;
  return (uint16_t)((DIV255(rr) << 11) | (DIV255(rg) << 5) | DIV255(rb));
}

// ---------------- Utilidades ----------------
// Raiz entera por restas binarias: SIN division (la version anterior hacia
// una division dentro del bucle de Newton). Se llama una vez por FILA de cada
// rectangulo redondeado, circulo y panel de vidrio. Resultado identico.
static inline int isqrt32(int v){
  if(v <= 0) return 0;
  uint32_t op = (uint32_t)v, res = 0, one = 1u << 30;
  while(one > op) one >>= 2;
  while(one){
    if(op >= res + one){ op -= res + one; res += one << 1; }
    res >>= 1; one >>= 2;
  }
  return (int)res;
}

// ---------------- Primitivas (escriben en gBuf) ----------------
// Rotacion landscape (Modo PC). Cuando gLand=true, las coords logicas
// (lx 0..799, ly 0..479) se escriben rotadas 90 sobre el panel portrait.
static bool gLand = false;
static inline void putPhys(int lx, int ly, uint16_t c){
  if((unsigned)lx >= SCR_H || (unsigned)ly >= SCR_W) return;
  int x = (SCR_W - 1) - ly, y = lx;
  if(y < gClipY0 || y > gClipY1) return;
  gBuf[(size_t)y * SCR_W + x] = c;
}
static inline void putPhysA(int lx, int ly, uint16_t c, uint8_t a){
  if((unsigned)lx >= SCR_H || (unsigned)ly >= SCR_W) return;
  int x = (SCR_W - 1) - ly, y = lx;
  if(y < gClipY0 || y > gClipY1) return;
  if(a >= 255){ gBuf[(size_t)y * SCR_W + x] = c; return; }
  if(a == 0) return;
  size_t i = (size_t)y * SCR_W + x; gBuf[i] = mix565(gBuf[i], c, a);
}
static inline void px(int x, int y, uint16_t c){
  if(gLand){ putPhys(x, y, c); return; }
  if((unsigned)x >= SCR_W || (unsigned)y >= SCR_H) return;
  if(y < gClipY0 || y > gClipY1 || x < gClipX0 || x > gClipX1) return;
  gBuf[(size_t)y * SCR_W + x] = c;
}
// pixel con alpha (0..255) sobre lo que ya hay en gBuf
static inline void pxA(int x, int y, uint16_t c, uint8_t a){
  if(gLand){ putPhysA(x, y, c, a); return; }
  if((unsigned)x >= SCR_W || (unsigned)y >= SCR_H) return;
  if(y < gClipY0 || y > gClipY1 || x < gClipX0 || x > gClipX1) return;
  if(a >= 255){ gBuf[(size_t)y * SCR_W + x] = c; return; }
  if(a == 0) return;
  size_t i = (size_t)y * SCR_W + x;
  gBuf[i] = mix565(gBuf[i], c, a);
}
static void hLine(int x, int y, int w, uint16_t c){
  if(w <= 0) return;
  if(gLand){ for(int i = 0; i < w; i++) putPhys(x + i, y, c); return; }
  if((unsigned)y >= SCR_H) return;
  if(y < gClipY0 || y > gClipY1) return;
  if(x < 0){ w += x; x = 0; }
  if(x + w > SCR_W) w = SCR_W - x;
  if(x < gClipX0){ w -= (gClipX0 - x); x = gClipX0; }     // recorte horizontal
  if(x + w > gClipX1 + 1) w = gClipX1 + 1 - x;
  if(w <= 0) return;
  uint16_t* p = gBuf + (size_t)y * SCR_W + x;
  for(int i = 0; i < w; i++) p[i] = c;
}
// Igual que hLine pero mezclando. Antes llamaba a pxA por pixel, lo que repetia
// TODOS los recortes (bordes + banda vertical + viewport) en cada pixel. Ahora
// recorta UNA vez y mezcla en linea recta: mismas reglas de recorte que hLine,
// mismo resultado, sin el coste por pixel. fillRectA/fillCircleA/fillRoundRectA
// cuelgan de aqui, asi que esto abarata todo el relleno translucido del sistema.
static void hLineA(int x, int y, int w, uint16_t c, uint8_t a){
  if(a >= 255){ hLine(x, y, w, c); return; }
  if(a == 0 || w <= 0) return;
  if(gLand){ for(int i = 0; i < w; i++) pxA(x + i, y, c, a); return; }   // landscape: ruta original
  if((unsigned)y >= SCR_H) return;
  if(y < gClipY0 || y > gClipY1) return;
  if(x < 0){ w += x; x = 0; }
  if(x + w > SCR_W) w = SCR_W - x;
  if(x < gClipX0){ w -= (gClipX0 - x); x = gClipX0; }
  if(x + w > gClipX1 + 1) w = gClipX1 + 1 - x;
  if(w <= 0) return;
  uint16_t* p = gBuf + (size_t)y * SCR_W + x;
  for(int i = 0; i < w; i++) p[i] = mix565(p[i], c, a);
}
static void vLine(int x, int y, int h, uint16_t c){
  if(h <= 0) return;
  if(gLand){ for(int i = 0; i < h; i++) putPhys(x, y + i, c); return; }
  if((unsigned)x >= SCR_W || x < gClipX0 || x > gClipX1) return;   // recorte horizontal
  if(y < 0){ h += y; y = 0; }
  if(y < gClipY0){ h -= (gClipY0 - y); y = gClipY0; }
  if(y + h > SCR_H) h = SCR_H - y;
  if(y + h > gClipY1 + 1) h = gClipY1 + 1 - y;
  if(h <= 0) return;
  uint16_t* p = gBuf + (size_t)y * SCR_W + x;
  for(int i = 0; i < h; i++){ *p = c; p += SCR_W; }
}
// --- Relleno rapido en LANDSCAPE (gLand) -------------------------------------
// La rotacion mapea (lx,ly) -> idx = lx*SCR_W + (SCR_W-1-ly). Es decir: para un
// lx FIJO, recorrer ly da direcciones CONSECUTIVAS. Una linea logica horizontal,
// en cambio, salta SCR_W*2 = 960 bytes por pixel, o sea UNA LINEA DE CACHE POR
// PIXEL contra PSRAM -- que es lo que hacia lento todo el Modo PC (un relleno de
// 430x268 son 115k escrituras dispersas).
// Por eso aqui se rellena por COLUMNA LOGICA: cada span queda secuencial. Mismo
// resultado pixel a pixel, mismo recorte (gClipY0/gClipY1 acotan filas fisicas,
// que en landscape son justo el eje lx).
static void fillSpanLand(int lx, int ly, int n, uint16_t c){
  if(n <= 0) return;
  if((unsigned)lx >= SCR_H) return;
  if(lx < gClipY0 || lx > gClipY1) return;
  if(ly < 0){ n += ly; ly = 0; }
  if(ly + n > SCR_W) n = SCR_W - ly;
  if(n <= 0) return;
  uint16_t* p = gBuf + (size_t)lx * SCR_W + (SCR_W - (ly + n));
  for(int i = 0; i < n; i++) p[i] = c;
}
static void fillSpanLandA(int lx, int ly, int n, uint16_t c, uint8_t a){
  if(a >= 255){ fillSpanLand(lx, ly, n, c); return; }
  if(a == 0 || n <= 0) return;
  if((unsigned)lx >= SCR_H) return;
  if(lx < gClipY0 || lx > gClipY1) return;
  if(ly < 0){ n += ly; ly = 0; }
  if(ly + n > SCR_W) n = SCR_W - ly;
  if(n <= 0) return;
  uint16_t* p = gBuf + (size_t)lx * SCR_W + (SCR_W - (ly + n));
  for(int i = 0; i < n; i++) p[i] = mix565(p[i], c, a);
}
static void fillRect(int x, int y, int w, int h, uint16_t c){
  if(gLand){ for(int i = 0; i < w; i++) fillSpanLand(x + i, y, h, c); return; }
  for(int j = 0; j < h; j++) hLine(x, y + j, w, c);
}
static void fillRectA(int x, int y, int w, int h, uint16_t c, uint8_t a){
  if(gLand){ for(int i = 0; i < w; i++) fillSpanLandA(x + i, y, h, c, a); return; }
  for(int j = 0; j < h; j++) hLineA(x, y + j, w, c, a);
}
static void drawRect(int x, int y, int w, int h, uint16_t c){
  hLine(x, y, w, c); hLine(x, y + h - 1, w, c);
  vLine(x, y, h, c); vLine(x + w - 1, y, h, c);
}

// Encaja un rect dentro de [0,limW) x [0,limH) respetando un tamano minimo y
// dejando SIEMPRE al menos `keep` px alcanzables dentro de la pantalla:
//   Â· en horizontal, `keep` px del rect siguen dentro por el lado que sea;
//   Â· en vertical, el borde SUPERIOR queda en [0, limH-keep], de modo que la
//     zona de agarre (barra de titulo, cabecera) nunca se va debajo del area.
// Es la puerta por la que debe pasar cualquier geometria movible: sin esto un
// arrastre o un resize puede dejar un elemento fuera de lo visible y volverlo
// imposible de tocar -- y con el, bloquear toda la interaccion.
// Devuelve true si hubo que corregir algo.
static bool flxClampRect(int &x, int &y, int &w, int &h,
                         int limW, int limH, int minW, int minH, int keep){
  int ox = x, oy = y, ow = w, oh = h;
  if(limW < 1) limW = 1;
  if(limH < 1) limH = 1;
  if(minW > limW) minW = limW;
  if(minH > limH) minH = limH;
  if(w < minW) w = minW;
  if(h < minH) h = minH;
  if(w > limW) w = limW;
  if(h > limH) h = limH;
  if(keep > w) keep = w;
  if(keep > h) keep = h;
  if(keep < 1) keep = 1;
  if(x > limW - keep) x = limW - keep;          // no se escapa por la derecha
  if(x + w < keep)    x = keep - w;             // ni por la izquierda
  if(y < 0) y = 0;
  int maxY = limH - keep; if(maxY < 0) maxY = 0;
  if(y > maxY) y = maxY;                        // el agarre siempre visible
  return x != ox || y != oy || w != ow || h != oh;
}

// Rectangulo redondeado relleno (esquinas suaves via inset por fila).
// En landscape se recorre por COLUMNA logica (mismo motivo que fillRect: asi
// cada span es memoria contigua). Es la TRANSPUESTA del mismo calculo, con lo
// que la silueta es la misma salvo, como mucho, 1 px en la diagonal de cada
// esquina -- y las dos variantes (opaca y alpha) usan la misma, asi que al
// superponerlas encajan exactamente.
static int rrInset(int k, int len, int r){
  if(k < r){ int d = r - 1 - k; return r - isqrt32(r * r - d * d); }
  if(k >= len - r){ int d = k - (len - r); return r - isqrt32(r * r - d * d); }
  return 0;
}
static void fillRoundRect(int x, int y, int w, int h, int r, uint16_t c){
  if(w <= 0 || h <= 0) return;
  if(r < 0) r = 0;
  if(2 * r > w) r = w / 2;
  if(2 * r > h) r = h / 2;
  if(gLand){
    for(int i = 0; i < w; i++){
      int in = rrInset(i, w, r);
      fillSpanLand(x + i, y + in, h - 2 * in, c);
    }
    return;
  }
  for(int j = 0; j < h; j++){
    int inset = rrInset(j, h, r);
    hLine(x + inset, y + j, w - 2 * inset, c);
  }
}
static void fillRoundRectA(int x, int y, int w, int h, int r, uint16_t c, uint8_t a){
  if(w <= 0 || h <= 0) return;
  if(r < 0) r = 0;
  if(2 * r > w) r = w / 2;
  if(2 * r > h) r = h / 2;
  if(gLand){
    for(int i = 0; i < w; i++){
      int in = rrInset(i, w, r);
      fillSpanLandA(x + i, y + in, h - 2 * in, c, a);
    }
    return;
  }
  for(int j = 0; j < h; j++){
    int inset = rrInset(j, h, r);
    hLineA(x + inset, y + j, w - 2 * inset, c, a);
  }
}
// Borde redondeado (1 px) para tarjetas
static void drawRoundRect(int x, int y, int w, int h, int r, uint16_t c){
  if(2 * r > w) r = w / 2;
  if(2 * r > h) r = h / 2;
  hLine(x + r, y, w - 2 * r, c);
  hLine(x + r, y + h - 1, w - 2 * r, c);
  vLine(x, y + r, h - 2 * r, c);
  vLine(x + w - 1, y + r, h - 2 * r, c);
  // esquinas
  int f = 1 - r, ddx = 1, ddy = -2 * r, xx = 0, yy = r;
  while(xx < yy){
    if(f >= 0){ yy--; ddy += 2; f += ddy; }
    xx++; ddx += 2; f += ddx;
    px(x + r - xx, y + r - yy, c); px(x + w - r - 1 + xx, y + r - yy, c);
    px(x + r - yy, y + r - xx, c); px(x + w - r - 1 + yy, y + r - xx, c);
    px(x + r - xx, y + h - r - 1 + yy, c); px(x + w - r - 1 + xx, y + h - r - 1 + yy, c);
    px(x + r - yy, y + h - r - 1 + xx, c); px(x + w - r - 1 + yy, y + h - r - 1 + xx, c);
  }
}

static void fillCircle(int cx, int cy, int r, uint16_t c){
  if(r <= 0){ px(cx, cy, c); return; }
  for(int dy = -r; dy <= r; dy++){
    int dx = isqrt32(r * r - dy * dy);
    hLine(cx - dx, cy + dy, 2 * dx + 1, c);
  }
}
static void fillCircleA(int cx, int cy, int r, uint16_t c, uint8_t a){
  if(r <= 0){ pxA(cx, cy, c, a); return; }
  for(int dy = -r; dy <= r; dy++){
    int dx = isqrt32(r * r - dy * dy);
    hLineA(cx - dx, cy + dy, 2 * dx + 1, c, a);
  }
}
static void drawCircle(int cx, int cy, int r, uint16_t c){
  int f = 1 - r, ddx = 1, ddy = -2 * r, x = 0, y = r;
  px(cx, cy + r, c); px(cx, cy - r, c); px(cx + r, cy, c); px(cx - r, cy, c);
  while(x < y){
    if(f >= 0){ y--; ddy += 2; f += ddy; }
    x++; ddx += 2; f += ddx;
    px(cx + x, cy + y, c); px(cx - x, cy + y, c);
    px(cx + x, cy - y, c); px(cx - x, cy - y, c);
    px(cx + y, cy + x, c); px(cx - y, cy + x, c);
    px(cx + y, cy - x, c); px(cx - y, cy - x, c);
  }
}
// anillo de grosor t
static void fillRing(int cx, int cy, int rOut, int t, uint16_t c){
  int rin = rOut - t; if(rin < 0) rin = 0;
  for(int dy = -rOut; dy <= rOut; dy++){
    int dxo = isqrt32(rOut * rOut - dy * dy);
    int inr2 = rin * rin - dy * dy;
    if(inr2 > 0){
      int dxi = isqrt32(inr2);
      hLine(cx - dxo, cy + dy, dxo - dxi, c);
      hLine(cx + dxi + 1, cy + dy, dxo - dxi, c);
    } else {
      hLine(cx - dxo, cy + dy, 2 * dxo + 1, c);
    }
  }
}
static void lineTo(int x0, int y0, int x1, int y1, uint16_t c){
  int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1, err = dx + dy;
  for(;;){
    px(x0, y0, c);
    if(x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if(e2 >= dy){ err += dy; x0 += sx; }
    if(e2 <= dx){ err += dx; y0 += sy; }
  }
}
// trazo grueso con puntas redondeadas: estampa discos por el segmento
static void strokeSeg(float x0, float y0, float x1, float y1, int rad, uint16_t c){
  float dx = x1 - x0, dy = y1 - y0;
  float len = sqrtf(dx * dx + dy * dy);
  int steps = (int)len + 1;
  for(int i = 0; i <= steps; i++){
    float t = (steps > 0) ? (float)i / steps : 0;
    fillCircle((int)(x0 + dx * t + 0.5f), (int)(y0 + dy * t + 0.5f), rad, c);
  }
}

// ---------------- Primitivas ANTI-ALIASING (bordes suaves) ----------------
// Cobertura por sub-pixel: los bordes se mezclan con alpha en vez de
// dibujarse "duros". Esto da curvas suaves al reloj y a los acentos.
static float distToSeg(float px, float py, float ax, float ay, float bx, float by){
  float dx = bx - ax, dy = by - ay;
  float l2 = dx * dx + dy * dy;
  float t = (l2 > 0.0f) ? ((px - ax) * dx + (py - ay) * dy) / l2 : 0.0f;
  if(t < 0) t = 0; else if(t > 1) t = 1;
  float qx = ax + t * dx - px, qy = ay + t * dy - py;
  return sqrtf(qx * qx + qy * qy);
}
static void fillCircleAA(float cx, float cy, float r, uint16_t col){
  int x0 = (int)floorf(cx - r - 1), x1 = (int)ceilf(cx + r + 1);
  int y0 = (int)floorf(cy - r - 1), y1 = (int)ceilf(cy + r + 1);
  for(int y = y0; y <= y1; y++) for(int x = x0; x <= x1; x++){
    float dx = x - cx, dy = y - cy;
    float cov = r + 0.5f - sqrtf(dx * dx + dy * dy);
    if(cov <= 0) continue; if(cov > 1) cov = 1;
    pxA(x, y, col, (uint8_t)(cov * 255));
  }
}
// Segmento grueso con puntas redondeadas y bordes suaves (para el reloj)
static void strokeSegAA(float x0, float y0, float x1, float y1, float rad, uint16_t col){
  int minx = (int)floorf(fminf(x0, x1) - rad - 1), maxx = (int)ceilf(fmaxf(x0, x1) + rad + 1);
  int miny = (int)floorf(fminf(y0, y1) - rad - 1), maxy = (int)ceilf(fmaxf(y0, y1) + rad + 1);
  for(int y = miny; y <= maxy; y++) for(int x = minx; x <= maxx; x++){
    float cov = rad + 0.5f - distToSeg((float)x, (float)y, x0, y0, x1, y1);
    if(cov <= 0) continue; if(cov > 1) cov = 1;
    pxA(x, y, col, (uint8_t)(cov * 255));
  }
}

// ---------------- Fondo (wallpaper) ----------------
// Degradado diagonal 3 paradas: verde (arriba-dcha) -> azul (centro)
// -> violeta (abajo-izq), igual que tus imagenes. Opcional: blobs.
// El degradado se repinta ENTERO en cada renderHome()/renderLock(), o sea una
// vez por minuto. La version anterior hacia, por cada uno de los 384.000
// pixeles: 2 divisiones + 1 mix565 (que a su vez hacia 3 divisiones mas).
// Dos observaciones lo tiran casi todo abajo:
//   1) 'ty' NO depende de x, pero se recalculaba 480 veces por fila.
//   2) el color solo depende de t = (tx+ty)/2, que vive en [0,255]: caben
//      los 256 colores posibles en una tabla y el bucle interior pasa a ser
//      una simple consulta. Coste: 992 B de RAM estatica, una sola vez.
// El resultado en pantalla es BIT A BIT el mismo que antes.
static void drawWallpaper(uint16_t* buf, bool blobs){
  const uint16_t green  = rgb565(80, 224, 74);    // arriba-derecha
  const uint16_t blue   = rgb565(40, 150, 245);   // centro
  const uint16_t purple = rgb565(112, 46, 230);   // abajo-izquierda
  static uint16_t gradLut[256];                   // color por t          (512 B)
  static uint8_t  txLut[SCR_W];                   // rampa horizontal      (480 B)
  static bool     lutReady = false;
  if(!lutReady){
    for(int t = 0; t < 256; t++)
      gradLut[t] = (t < 128) ? mix565(purple, blue,  (uint8_t)(t * 2))
                             : mix565(blue,   green, (uint8_t)((t - 128) * 2));
    for(int x = 0; x < SCR_W; x++) txLut[x] = (uint8_t)((x * 255) / (SCR_W - 1));
    lutReady = true;
  }
  uint16_t* old = gBuf; setBuf(buf);
  for(int y = 0; y < SCR_H; y++){
    // t=1 arriba-derecha, t=0 abajo-izquierda. 'ty' es invariante en x.
    int ty = ((SCR_H - 1 - y) * 255) / (SCR_H - 1);
    uint16_t* row = buf + (size_t)y * SCR_W;
    for(int x = 0; x < SCR_W; x++) row[x] = gradLut[(txLut[x] + ty) >> 1];
  }
  if(blobs){
    // dos manchas suaves mas claras (como el escritorio de tus imagenes)
    fillCircleA(360, 150, 220, rgb565(150, 235, 180), 60);
    fillCircleA(90,  560, 260, rgb565(150, 160, 240), 55);
  }
  setBuf(old);
}

// #############################################################
// ##  LIQUID GLASS (aproximacion iOS 26 en software)
// ##  Panel reutilizable: desenfoca el fondo real (box-blur),
// ##  tinte sutil y gradiente de grosor.
// #############################################################
static bool uiGlass = false;               // estilo activo (togglea en Ajustes)
// Modo de apariencia (Ajustes -> Pantalla -> Modo de apariencia). true = Modo
// oscuro (comportamiento de siempre, por defecto). false = Modo claro.
// ALCANCE: por ahora retematiza la app Ajustes (donde vive el selector),
// que es donde vive PAGE_BG y los colores de tarjeta/texto de esa app (ver
// mas abajo, junto a PAGE_BG). Inicio, Bloqueo, notificaciones, ventanas y
// el resto de apps tienen su propia paleta oscura, muy afinada a mano, y no
// se tocan en esta pasada -- retematizarlas de verdad necesita un diseno de
// colores propio por pantalla, no un cambio mecanico.
static bool gDark = true;
static int  gIconStyle = 0;                // estilo de iconos: 0 = Plano, 1 = Vidrio (fondo Liquid Glass en drawAppIcon)
static uint16_t* glassBuf = NULL;          // scratch de region (PSRAM)
// NOTA: aqui vivia 'wallBuf' ("wallpaper limpio para animar el brillo"). Era
// memoria muerta: se reservaban 768 KB y se copiaban enteros en CADA
// renderHome(), pero NINGUNA funcion lo leia jamas. Eliminado: -768 KB de
// PSRAM y -768 KB de memcpy por cada repintado del escritorio.
// Linea temporal para el blur. Se indexa por ANCHO (pasada horizontal) y
// por ALTO (pasada vertical) del panel de turno, asi que debe cubrir el
// mayor de los dos lados de la pantalla, no solo SCR_H, o un panel mas
// ancho que alto desbordaria este buffer y corromperia memoria vecina.
static uint16_t  glLine[(SCR_W > SCR_H ? SCR_W : SCR_H)];

static inline void un565(uint16_t c, int &r, int &g, int &b){ r = (c >> 11) & 0x1F; g = (c >> 5) & 0x3F; b = c & 0x1F; }
static inline uint16_t pk565(int r, int g, int b){ return (uint16_t)((r << 11) | (g << 5) | b); }

// Luma aproximada directamente en dominio 565, sin float y sin multiplicacion
// real: el compilador reduce *5 a (x<<2)+x y *2 a x<<1. Devuelve 0..266 en vez
// de 0..255, y NO se normaliza a proposito: solo se usa para comparar dos lumas
// entre si (la del fondo contra la del tinte), y ambas viven en este mismo
// dominio, asi que la resta es consistente. El error medio contra la luma
// perceptual real es de ~5 niveles sobre 255, de sobra para decidir cuanto
// tinte aplicar. Reutiliza un565 en vez de repetir el desempaquetado.
static inline int glassLuma(uint16_t c){
  int r, g, b; un565(c, r, g, b);
  return ((r + g) * 5 + b * 2) >> 1;
}

// box-blur (suma corrediza) sobre glassBuf de ancho w, alto h
static void glassBlur(int w, int h, int R){
  int r, g, b;
  for(int j = 0; j < h; j++){                         // horizontal
    uint16_t* row = glassBuf + (size_t)j * w;
    for(int i = 0; i < w; i++) glLine[i] = row[i];
    int sr = 0, sg = 0, sb = 0, win = 0;
    for(int i = 0; i <= R && i < w; i++){ un565(glLine[i], r, g, b); sr += r; sg += g; sb += b; win++; }
    for(int i = 0; i < w; i++){
      row[i] = pk565(sr / win, sg / win, sb / win);
      int add = i + R + 1, rem = i - R;
      if(add < w){ un565(glLine[add], r, g, b); sr += r; sg += g; sb += b; win++; }
      if(rem >= 0){ un565(glLine[rem], r, g, b); sr -= r; sg -= g; sb -= b; win--; }
    }
  }
  for(int i = 0; i < w; i++){                          // vertical
    for(int j = 0; j < h; j++) glLine[j] = glassBuf[(size_t)j * w + i];
    int sr = 0, sg = 0, sb = 0, win = 0;
    for(int j = 0; j <= R && j < h; j++){ un565(glLine[j], r, g, b); sr += r; sg += g; sb += b; win++; }
    for(int j = 0; j < h; j++){
      glassBuf[(size_t)j * w + i] = pk565(sr / win, sg / win, sb / win);
      int add = j + R + 1, rem = j - R;
      if(add < h){ un565(glLine[add], r, g, b); sr += r; sg += g; sb += b; win++; }
      if(rem >= 0){ un565(glLine[rem], r, g, b); sr -= r; sg -= g; sb -= b; win--; }
    }
  }
}
static int glInset(int j, int h, int rad){
  if(j < rad){ int dy = rad - 1 - j; return rad - isqrt32(rad * rad - dy * dy); }
  if(j >= h - rad){ int dy = j - (h - rad); return rad - isqrt32(rad * rad - dy * dy); }
  return 0;
}
// Panel Liquid Glass reutilizable (estatico: blur + tinte + gradiente).
// "Ex" permite fijar el radio del box-blur (blurR). glassBlur() es una suma
// corrediza O(w*h) que NO depende de blurR (ver mas arriba), asi que subir
// blurR no cuesta rendimiento extra -- solo cambia cuanto se difumina el
// fondo. drawLiquidGlassPanel() de siempre (abajo) sigue llamando a esta con
// blurR=6, es decir: mismo blur que antes en los ~19 sitios existentes que ya
// la usan. Se penso para el panel rapido, que quiere un vidrio mas
// "esmerilado" que el resto del sistema.
//
// TINTE ADAPTATIVO: el porcentaje de mezcla del tinte ya no es el 58 fijo de
// antes; se mueve dentro de [GLASS_TINT_MIN..GLASS_TINT_MAX] segun cuanto
// difiera la luminancia del tinte respecto a la del fondo que quedo debajo del
// panel. Esto SI cambia el aspecto de los ~19 sitios existentes (cambio pedido
// y aprobado a proposito, no un efecto colateral): el blur y la geometria son
// los de siempre, solo respira el tinte. GLASS_TINT_BASE es el valor historico
// y queda como respaldo defensivo por si no se pudo tomar ninguna muestra.
// GLASS_TINT_DIFF_MAX es potencia de dos a proposito: convierte la division
// del mapeo en un desplazamiento.
static const uint8_t GLASS_TINT_BASE = 58, GLASS_TINT_MIN = 46, GLASS_TINT_MAX = 70;
static const int     GLASS_TINT_DIFF_MAX = 128;
static void drawLiquidGlassPanelEx(int x, int y, int w, int h, int rad, uint16_t tint, int blurR){
  // GUARDA DE LANDSCAPE (Modo PC). Esta funcion lee y escribe el buffer con
  // indexacion VERTICAL directa (gBuf + (y+j)*SCR_W + x), asi que ignora por
  // completo la rotacion de gLand. En Modo PC cada drawAppIcon() de estilo
  // "Vidrio" pintaba su panel en coordenadas rotadas: por eso aparecian paneles
  // de cristal FANTASMA flotando por el escritorio (una columna a la derecha =
  // los iconos de la barra de tareas, una fila abajo = los del escritorio).
  // fillRoundRectA() si respeta la rotacion (pasa por putPhys), asi que en
  // landscape se usa el panel plano tintado: mismo sitio, sin fantasmas. El
  // cristal propio de Modo PC lo dibuja pcGlassPanel(), que si es landscape-safe.
  if(gLand){ fillRoundRectA(x, y, w, h, rad, tint, 210); return; }
  if(!glassBuf) glassBuf = (uint16_t*)heap_caps_malloc((size_t)SCR_W * SCR_H * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if(!glassBuf){ fillRoundRectA(x, y, w, h, rad, tint, 210); return; }   // fallback sin PSRAM
  if(x < 0){ w += x; x = 0; } if(y < 0){ h += y; y = 0; }
  if(x + w > SCR_W) w = SCR_W - x; if(y + h > SCR_H) h = SCR_H - y;
  if(w <= 0 || h <= 0) return;
  if(2 * rad > w) rad = w / 2; if(2 * rad > h) rad = h / 2;
  // Sampleo del fondo para el tinte adaptativo. Se engancha al loop de memcpy
  // que YA existe -- sin pasada extra, sin buffer nuevo, solo dos acumuladores
  // en stack -- y lee 1 de cada 4 filas por 1 de cada 8 columnas, o sea 1/32 de
  // los pixeles. Se mide ANTES del blur, sobre la region recien copiada. Se
  // deja el memcpy en bloque en vez de acumular pixel a pixel porque copiar por
  // bytes seria mas caro que el propio muestreo disperso.
  uint32_t lumaSum = 0; int lumaN = 0;
  for(int j = 0; j < h; j++){
    uint16_t* row = glassBuf + (size_t)j * w;
    memcpy(row, gBuf + (size_t)(y + j) * SCR_W + x, w * 2);
    if((j & 3) == 0) for(int i = 0; i < w; i += 8){ lumaSum += (uint32_t)glassLuma(row[i]); lumaN++; }
  }
  // Cuanto MAS se parecen en luminancia el tinte y el fondo, MENOS tinte se
  // aplica: si ya comparten tono, cargarle tinte solo lo aplana en un bloque
  // liso, y conviene dejar ver el fondo. Cuanto mas difieren, mas tinte, para
  // que el panel afirme su propio color en vez de lavarse contra el fondo. Un
  // solo valor absoluto cubre los cuatro casos sin ramas: tinte oscuro sobre
  // fondo oscuro y tinte claro sobre fondo claro dan diferencia chica (poco
  // tinte); los dos cruzados dan diferencia grande (mas tinte). Todo esto se
  // calcula UNA vez por panel, no por pixel.
  uint8_t tintMix = GLASS_TINT_BASE;
  if(lumaN > 0){
    int dif = (int)(lumaSum / (uint32_t)lumaN) - glassLuma(tint);
    if(dif < 0) dif = -dif;
    if(dif > GLASS_TINT_DIFF_MAX) dif = GLASS_TINT_DIFF_MAX;
    tintMix = (uint8_t)(GLASS_TINT_MIN + (dif * (GLASS_TINT_MAX - GLASS_TINT_MIN)) / GLASS_TINT_DIFF_MAX);
  }
  glassBlur(w, h, blurR);
  for(int j = 0; j < h; j++){
    int yy = y + j; if(yy < gClipY0 || yy > gClipY1) continue;   // respeta la banda de recorte
    int ins = glInset(j, h, rad);
    uint16_t* src = glassBuf + (size_t)j * w;
    uint16_t* dst = gBuf + (size_t)yy * SCR_W + x;
    float fj = (float)j;
    for(int i = ins; i < w - ins; i++){
      uint16_t out = mix565(src[i], tint, tintMix);   // tinte adaptativo (ver arriba), antes fijo en 58
      if(fj < h * 0.45f) out = mix565(out, rgb565(255,255,255), (uint8_t)((1.0f - fj / (h * 0.45f)) * 26));
      else               out = mix565(out, rgb565(0,0,0), (uint8_t)(((fj - h * 0.45f) / (h * 0.55f)) * 30));
      dst[i] = out;
    }
    // Highlight direccional (luz simulada desde la esquina superior-izquierda):
    // mismo bcol de siempre por fila (blanco arriba, negro abajo), pero la
    // FUERZA de la mezcla se pondera distinto por lado en vez de usar 130 fijo
    // en los dos bordes. Asi las 4 esquinas quedan con peso propio (arriba-
    // izq. blanco fuerte, arriba-der. blanco tenue, abajo-izq. sombra tenue,
    // abajo-der. sombra fuerte) en vez de una franja horizontal identica en
    // ambos bordes. GLASS_CORNER_STRONG/WEAK promedian 130 (el valor de antes)
    // para no cambiar el "peso" total del borde, solo redistribuirlo: el delta
    // es de +-20% sobre ese 130. Sigue siendo funcion de j nada mas: mismos 2
    // pixeles por fila de siempre. Si hay que retocar la intensidad, mover los
    // dos valores de forma simetrica alrededor de 130 (STRONG = 130 + d,
    // WEAK = 130 - d) para que el borde no gane ni pierda peso total.
    const uint8_t GLASS_CORNER_STRONG = 156, GLASS_CORNER_WEAK = 104;
    bool topZone = (j < h / 2);
    uint8_t sL = topZone ? GLASS_CORNER_STRONG : GLASS_CORNER_WEAK;   // izquierda: blanco fuerte / sombra tenue
    uint8_t sR = topZone ? GLASS_CORNER_WEAK   : GLASS_CORNER_STRONG; // derecha: blanco tenue / sombra fuerte
    uint16_t bcol = (j < 3) ? rgb565(255,255,255) : (j < h / 2 ? rgb565(205,214,228) : rgb565(22,28,40));
    dst[ins] = mix565(dst[ins], bcol, sL);
    dst[w - 1 - ins] = mix565(dst[w - 1 - ins], bcol, sR);
  }
}
static void drawLiquidGlassPanel(int x, int y, int w, int h, int rad, uint16_t tint){
  drawLiquidGlassPanelEx(x, y, w, h, rad, tint, 6);   // blur original, sin cambios, para el resto del sistema
}

// #############################################################
// ##  TARJETA LIQUID GLASS CACHEADA (fondos PLANOS)
// ##  ------------------------------------------------------
// ##  Por que existe: drawLiquidGlassPanel copia la region, la desenfoca y la
// ##  mezcla. Con ocho tarjetas por cuadro eso era demasiado caro para seguir
// ##  al dedo, asi que las listas con scroll (Ajustes, Ajustes del teclado)
// ##  DESACTIVABAN el vidrio mientras se arrastraba y lo devolvian al soltar:
// ##  de ahi que "al hacer scroll el material perdiera el desenfoque y las
// ##  transparencias".
// ##  La observacion que lo arregla: sobre un fondo de color UNIFORME el
// ##  box-blur devuelve ese mismo color, asi que el resultado del panel no
// ##  depende de DONDE se dibuje -- solo de (w, h, radio, tinte, color de
// ##  fondo). Se calcula UNA vez, se guarda y a partir de ahi cada tarjeta es
// ##  un memcpy por fila. El resultado en pantalla es identico pixel a pixel
// ##  al de la version cara, asi que el vidrio ya puede quedarse encendido
// ##  durante todo el desplazamiento.
// ##  Si el usuario tiene el Liquid Glass DESACTIVADO no se llama aqui
// ##  siquiera: cada llamante conserva su rama plana de siempre.
// #############################################################
#define GLC_MAX_H 96                       // alto maximo de tarjeta cacheable (las filas miden ~52-62)
static uint16_t* glcScratch = NULL;        // lienzo de trabajo (stride SCR_W, GLC_MAX_H filas)
static uint16_t* glcCard    = NULL;        // tarjeta ya resuelta (w x h compactos)
static int       glcW = 0, glcH = 0, glcRad = -1;
static uint16_t  glcTint = 0, glcBg = 0;
static bool      glcValid = false;

static bool glcBuild(int w, int h, int rad, uint16_t tint, uint16_t bg){
  if(!glcScratch) glcScratch = (uint16_t*)heap_caps_malloc((size_t)SCR_W * GLC_MAX_H * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if(!glcCard)    glcCard    = (uint16_t*)heap_caps_malloc((size_t)SCR_W * GLC_MAX_H * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if(!glcScratch || !glcCard) return false;
  for(int j = 0; j < h; j++){                       // fondo plano: la premisa de todo esto
    uint16_t* r = glcScratch + (size_t)j * SCR_W;
    for(int i = 0; i < w; i++) r[i] = bg;
  }
  uint16_t* oBuf = gBuf;                            // gBuf directo, no setBuf: no debe desviarse a un lienzo de DeX
  int oc0 = gClipY0, oc1 = gClipY1, ox0 = gClipX0, ox1 = gClipX1;
  gBuf = glcScratch;
  gClipY0 = 0; gClipY1 = h - 1; gClipX0 = 0; gClipX1 = SCR_W - 1;
  drawLiquidGlassPanel(0, 0, w, h, rad, tint);
  gBuf = oBuf; gClipY0 = oc0; gClipY1 = oc1; gClipX0 = ox0; gClipX1 = ox1;
  for(int j = 0; j < h; j++)
    memcpy(glcCard + (size_t)j * w, glcScratch + (size_t)j * SCR_W, (size_t)w * 2);
  glcW = w; glcH = h; glcRad = rad; glcTint = tint; glcBg = bg; glcValid = true;
  return true;
}
// Tarjeta de vidrio sobre fondo plano. Cae al panel de siempre (mismo dibujo,
// solo mas caro) si el tamano no cabe en la cache o si estamos en landscape,
// donde la indexacion directa no vale.
static void drawGlassCardFlat(int x, int y, int w, int h, int rad, uint16_t tint, uint16_t bg){
  if(gLand || w <= 0 || h <= 0 || w > SCR_W || h > GLC_MAX_H){
    drawLiquidGlassPanel(x, y, w, h, rad, tint); return;
  }
  if(!glcValid || glcW != w || glcH != h || glcRad != rad || glcTint != tint || glcBg != bg){
    if(!glcBuild(w, h, rad, tint, bg)){ drawLiquidGlassPanel(x, y, w, h, rad, tint); return; }
  }
  for(int j = 0; j < h; j++){
    int yy = y + j;
    if(yy < 0 || yy >= SCR_H || yy < gClipY0 || yy > gClipY1) continue;
    int xs = x, xe = x + w - 1, sx = 0;
    if(xs < gClipX0){ sx = gClipX0 - xs; xs = gClipX0; }
    if(xe > gClipX1) xe = gClipX1;
    if(xs < 0){ sx += -xs; xs = 0; }
    if(xe > SCR_W - 1) xe = SCR_W - 1;
    if(xs > xe) continue;
    memcpy(gBuf + (size_t)yy * SCR_W + xs, glcCard + (size_t)j * w + sx, (size_t)(xe - xs + 1) * 2);
  }
}
// Wallpaper desenfocado reutilizable (fondo del desbloqueo y de Recientes, estilo iOS)
static uint16_t* blurBg = NULL;
static void ensureBlurBg(){
  if(blurBg) return;
  blurBg = (uint16_t*)heap_caps_malloc((size_t)SCR_W * SCR_H * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if(!blurBg) return;
  uint16_t* old = gBuf;
  // Este buffer se compone UNA SOLA VEZ en toda la sesion (el early-return de
  // arriba). Si el primer llamante llega con gLand=true o con el recorte
  // estrechado -- p.ej. saliendo del Modo Kiosco desde Juegos, que deja
  // gLand=true en cada frame-- el fillRectA de abajo se aplicaria girado o a
  // media pantalla y el fondo quedaria roto PARA SIEMPRE. Se fuerza portrait y
  // recorte completo aqui, no en cada llamante.
  bool wl = gLand; gLand = false;
  int sx0 = gClipX0, sx1 = gClipX1, sy0 = gClipY0, sy1 = gClipY1;
  gClipX0 = 0; gClipX1 = SCR_W - 1; gClipY0 = 0; gClipY1 = SCR_H - 1;
  drawWallpaper(blurBg, true);
  setBuf(blurBg);
  fillRectA(0, 0, SCR_W, SCR_H, rgb565(8,10,18), 70);
  setBuf(old);
  gLand = wl;
  gClipX0 = sx0; gClipX1 = sx1; gClipY0 = sy0; gClipY1 = sy1;
}

// #############################################################
// ##  TIPOGRAFIA + RELOJ VECTORIAL + TRIANGULOS  (original)
// #############################################################

// Fuente 5x7 (column-major, bit0 = arriba). ASCII 0x20..0x7E.
static const uint8_t FONT5x7[95][5] = {
  {0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5F,0x00,0x00},{0x00,0x07,0x00,0x07,0x00},
  {0x14,0x7F,0x14,0x7F,0x14},{0x24,0x2A,0x7F,0x2A,0x12},{0x23,0x13,0x08,0x64,0x62},
  {0x36,0x49,0x55,0x22,0x50},{0x00,0x05,0x03,0x00,0x00},{0x00,0x1C,0x22,0x41,0x00},
  {0x00,0x41,0x22,0x1C,0x00},{0x14,0x08,0x3E,0x08,0x14},{0x08,0x08,0x3E,0x08,0x08},
  {0x00,0x50,0x30,0x00,0x00},{0x08,0x08,0x08,0x08,0x08},{0x00,0x60,0x60,0x00,0x00},
  {0x20,0x10,0x08,0x04,0x02},{0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},
  {0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},{0x18,0x14,0x12,0x7F,0x10},
  {0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
  {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},{0x00,0x36,0x36,0x00,0x00},
  {0x00,0x56,0x36,0x00,0x00},{0x08,0x14,0x22,0x41,0x00},{0x14,0x14,0x14,0x14,0x14},
  {0x00,0x41,0x22,0x14,0x08},{0x02,0x01,0x51,0x09,0x06},{0x32,0x49,0x79,0x41,0x3E},
  {0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
  {0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},
  {0x3E,0x41,0x49,0x49,0x7A},{0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},
  {0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},{0x7F,0x40,0x40,0x40,0x40},
  {0x7F,0x02,0x0C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
  {0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},
  {0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7F,0x01,0x01},{0x3F,0x40,0x40,0x40,0x3F},
  {0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},{0x63,0x14,0x08,0x14,0x63},
  {0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43},{0x00,0x7F,0x41,0x41,0x00},
  {0x02,0x04,0x08,0x10,0x20},{0x00,0x41,0x41,0x7F,0x00},{0x04,0x02,0x01,0x02,0x04},
  {0x40,0x40,0x40,0x40,0x40},{0x00,0x01,0x02,0x04,0x00},{0x20,0x54,0x54,0x54,0x78},
  {0x7F,0x48,0x44,0x44,0x38},{0x38,0x44,0x44,0x44,0x20},{0x38,0x44,0x44,0x48,0x7F},
  {0x38,0x54,0x54,0x54,0x18},{0x08,0x7E,0x09,0x01,0x02},{0x0C,0x52,0x52,0x52,0x3E},
  {0x7F,0x08,0x04,0x04,0x78},{0x00,0x44,0x7D,0x40,0x00},{0x20,0x40,0x44,0x3D,0x00},
  {0x7F,0x10,0x28,0x44,0x00},{0x00,0x41,0x7F,0x40,0x00},{0x7C,0x04,0x18,0x04,0x78},
  {0x7C,0x08,0x04,0x04,0x78},{0x38,0x44,0x44,0x44,0x38},{0x7C,0x14,0x14,0x14,0x08},
  {0x08,0x14,0x14,0x18,0x7C},{0x7C,0x08,0x04,0x04,0x08},{0x48,0x54,0x54,0x54,0x20},
  {0x04,0x3F,0x44,0x40,0x20},{0x3C,0x40,0x40,0x20,0x7C},{0x1C,0x20,0x40,0x20,0x1C},
  {0x3C,0x40,0x30,0x40,0x3C},{0x44,0x28,0x10,0x28,0x44},{0x0C,0x50,0x50,0x50,0x3C},
  {0x44,0x64,0x54,0x4C,0x44},{0x00,0x08,0x36,0x41,0x00},{0x00,0x00,0x7F,0x00,0x00},
  {0x00,0x41,0x36,0x08,0x00},{0x08,0x04,0x08,0x10,0x08}
};
// Glifos extra: Â¿ (invertido) y Â¡ (invertido)
static const uint8_t GLYPH_INVQ[5]    = {0x30,0x48,0x45,0x40,0x20};
static const uint8_t GLYPH_INVEXCL[5] = {0x00,0x00,0x7D,0x00,0x00};

// Tipos de acento (se dibujan sobre/bajo el glifo base)
enum { ACC_NONE=0, ACC_ACUTE, ACC_GRAVE, ACC_TILDE, ACC_DIAER, ACC_CIRC, ACC_CED };

// Mapea un codepoint Unicode a (glifo base, acento). base 1=Â¿, 2=Â¡.
static void mapCP(uint32_t cp, uint8_t &base, uint8_t &acc){
  acc = ACC_NONE;
  if(cp < 0x80){ base = (cp >= 0x20 && cp <= 0x7E) ? (uint8_t)cp : '?'; return; }
  switch(cp){
    case 0xE1: base='a'; acc=ACC_ACUTE; return;   case 0xE9: base='e'; acc=ACC_ACUTE; return;
    case 0xED: base='i'; acc=ACC_ACUTE; return;   case 0xF3: base='o'; acc=ACC_ACUTE; return;
    case 0xFA: base='u'; acc=ACC_ACUTE; return;   case 0xFC: base='u'; acc=ACC_DIAER; return;
    case 0xF1: base='n'; acc=ACC_TILDE; return;
    case 0xC1: base='A'; acc=ACC_ACUTE; return;   case 0xC9: base='E'; acc=ACC_ACUTE; return;
    case 0xCD: base='I'; acc=ACC_ACUTE; return;   case 0xD3: base='O'; acc=ACC_ACUTE; return;
    case 0xDA: base='U'; acc=ACC_ACUTE; return;   case 0xD1: base='N'; acc=ACC_TILDE; return;
    case 0xDC: base='U'; acc=ACC_DIAER; return;
    case 0xE0: base='a'; acc=ACC_GRAVE; return;   case 0xE8: base='e'; acc=ACC_GRAVE; return;
    case 0xEC: base='i'; acc=ACC_GRAVE; return;   case 0xF2: base='o'; acc=ACC_GRAVE; return;
    case 0xF9: base='u'; acc=ACC_GRAVE; return;
    case 0xE2: base='a'; acc=ACC_CIRC; return;    case 0xEA: base='e'; acc=ACC_CIRC; return;
    case 0xEE: base='i'; acc=ACC_CIRC; return;    case 0xF4: base='o'; acc=ACC_CIRC; return;
    case 0xFB: base='u'; acc=ACC_CIRC; return;
    case 0xE3: base='a'; acc=ACC_TILDE; return;   case 0xF5: base='o'; acc=ACC_TILDE; return;
    case 0xE7: base='c'; acc=ACC_CED; return;     case 0xC7: base='C'; acc=ACC_CED; return;
    case 0xBF: base=1; return;                    case 0xA1: base=2; return;
    default:   base='?'; return;
  }
}

static void drawGlyphRaw(int x, int y, uint8_t base, int s, uint16_t col){
  const uint8_t* g;
  if(base == 1) g = GLYPH_INVQ;
  else if(base == 2) g = GLYPH_INVEXCL;
  else { int idx = (int)base - 0x20; if(idx < 0 || idx > 94) return; g = FONT5x7[idx]; }
  for(int c = 0; c < 5; c++){
    uint8_t bits = g[c];
    for(int r = 0; r < 7; r++){
      if(bits & (1 << r)){
        if(s == 1) px(x + c, y + r, col);
        else fillRect(x + c * s, y + r * s, s, s, col);
      }
    }
  }
}
static void drawAccent(int x, int y, int s, uint8_t acc, uint16_t col){
  float cx = x + 2.5f * s;
  float r = s * 0.55f; if(r < 1.0f) r = 1.0f;
  switch(acc){
    case ACC_ACUTE: strokeSegAA(cx - s, y - s, cx + s, y - 3 * s, r, col); break;
    case ACC_GRAVE: strokeSegAA(cx - s, y - 3 * s, cx + s, y - s, r, col); break;
    case ACC_CIRC:  strokeSegAA(cx - 1.6f * s, y - s, cx, y - 3 * s, r, col);
                    strokeSegAA(cx, y - 3 * s, cx + 1.6f * s, y - s, r, col); break;
    case ACC_TILDE: strokeSegAA(cx - 2 * s, y - 2 * s, cx - 0.4f * s, y - 2.9f * s, r, col);
                    strokeSegAA(cx - 0.4f * s, y - 2.9f * s, cx + 0.6f * s, y - 1.9f * s, r, col);
                    strokeSegAA(cx + 0.6f * s, y - 1.9f * s, cx + 2 * s, y - 2.7f * s, r, col); break;
    case ACC_DIAER: fillCircleAA(cx - s, y - 2 * s, r, col);
                    fillCircleAA(cx + s, y - 2 * s, r, col); break;
    case ACC_CED:   strokeSegAA(cx, y + 7 * s - s, cx - 1.2f * s, y + 7 * s + 0.6f * s, r, col); break;
  }
}

// Glifo del cuerpo con SUPERSAMPLING (3x3) -> bordes suaves.
// A tamano 1 (etiquetas diminutas) se dibuja nitido para no emborronar.
static void drawGlyphSmooth(int x, int y, uint8_t base, int s, uint16_t col, uint8_t alpha){
  const uint8_t* g;
  if(base == 1) g = GLYPH_INVQ;
  else if(base == 2) g = GLYPH_INVEXCL;
  else { int idx = (int)base - 0x20; if(idx < 0 || idx > 94) return; g = FONT5x7[idx]; }
  if(s <= 1){
    for(int c = 0; c < 5; c++){ uint8_t bits = g[c];
      for(int r = 0; r < 7; r++) if(bits & (1 << r)) pxA(x + c, y + r, col, alpha); }
    return;
  }
  const int SS = 3;
  int W = 5 * s, H = 7 * s;
  for(int ty = 0; ty < H; ty++){
    for(int tx = 0; tx < W; tx++){
      int cov = 0;
      for(int sy = 0; sy < SS; sy++) for(int sx = 0; sx < SS; sx++){
        int ix = (int)((tx + (sx + 0.5f) / SS) / s);
        int iy = (int)((ty + (sy + 0.5f) / SS) / s);
        if(ix >= 0 && ix < 5 && iy >= 0 && iy < 7 && (g[ix] & (1 << iy))) cov++;
      }
      if(cov) pxA(x + tx, y + ty, col, (uint8_t)((cov * alpha) / (SS * SS)));
    }
  }
}

// Ancho de texto en px (para centrar). Cada glifo avanza 6*s.

// #############################################################
// ##  FUENTE OUTFIT antialiased 4bpp  (reemplaza al 5x7 POR DEBAJO)
// ##  Mismas firmas publicas: drawText/drawTextC/drawTextR/textW.
// #############################################################
// Fuente Outfit-Regular antialiased 4bpp (generada). NO editar a mano.
#define FONT_LINEH 51
#define FONT_ASC 40
static const FGlyph FG[127] = {
  {0,0,0,0,8,0},{5,30,3,10,10,0},{12,10,2,11,16,90},{21,29,2,11,26,150},{19,37,2,7,24,469},{22,29,2,11,26,839},
  {24,29,2,11,26,1158},{5,10,2,11,9,1506},{10,36,2,9,12,1536},{10,36,1,9,12,1716},{16,17,2,9,20,1896},{17,19,2,17,22,2032},
  {6,11,2,35,11,2203},{13,4,3,27,18,2236},{5,5,3,35,12,2264},{16,32,0,10,16,2279},{23,29,2,11,26,2535},{10,29,1,11,14,2883},
  {19,29,1,11,22,3028},{19,29,1,11,22,3318},{22,29,1,11,24,3608},{20,29,0,11,22,3927},{19,29,2,11,23,4217},{18,29,1,11,21,4507},
  {19,29,2,11,22,4768},{20,29,2,11,23,5058},{5,18,3,22,11,5348},{6,24,2,22,11,5402},{17,19,2,17,22,5474},{17,13,2,20,22,5645},
  {17,19,2,17,22,5762},{18,30,1,10,20,5933},{26,27,2,17,30,6203},{27,29,1,11,28,6554},{20,29,3,11,25,6960},{25,29,2,11,28,7250},
  {24,29,3,11,30,7627},{19,29,3,11,24,7975},{18,29,3,11,23,8265},{27,29,2,11,31,8526},{22,29,3,11,28,8932},{4,29,3,11,10,9251},
  {16,29,1,11,20,9309},{23,29,3,11,27,9541},{18,29,3,11,22,9889},{27,29,3,11,34,10150},{22,29,3,11,28,10556},{28,29,2,11,32,10875},
  {19,29,3,11,24,11281},{30,31,2,11,33,11571},{21,29,3,11,25,12036},{19,29,1,11,22,12355},{23,29,1,11,25,12645},{22,29,3,11,27,12993},
  {26,29,1,11,28,13312},{37,29,1,11,39,13689},{26,29,1,11,28,14240},{25,29,0,11,27,14617},{20,29,2,11,23,14994},{9,33,3,11,14,15284},
  {16,32,0,10,16,15449},{9,33,1,11,14,15705},{14,11,2,10,18,15870},{19,4,1,41,20,15947},{9,9,1,9,11,15987},{19,20,1,20,23,16032},
  {19,29,3,11,23,16232},{18,20,1,20,20,16522},{19,29,1,11,23,16702},{19,20,1,20,21,16992},{18,30,0,10,16,17192},{19,28,1,20,23,17462},
  {17,29,3,11,22,17742},{5,29,2,11,9,18003},{13,37,-5,11,9,18090},{17,29,3,11,20,18349},{4,29,3,11,9,18610},{29,20,3,20,34,18668},
  {17,20,3,20,22,18968},{20,20,1,20,23,19148},{19,28,3,20,23,19348},{20,28,1,20,23,19628},{14,20,3,20,17,19908},{16,20,0,20,17,20048},
  {14,28,0,12,15,20208},{16,20,2,20,21,20404},{20,20,0,20,20,20564},{30,20,0,20,30,20764},{20,20,0,20,20,21064},{20,28,0,20,21,21264},
  {16,20,1,20,18,21544},{11,33,1,11,13,21704},{4,37,4,8,11,21902},{11,33,1,11,13,21976},{18,6,2,23,22,22174},{19,31,1,9,23,22228},
  {19,31,1,9,21,22538},{9,32,0,8,9,22848},{20,31,1,9,23,23008},{16,32,2,8,21,23318},{16,28,2,12,21,23574},{17,29,3,11,22,23798},
  {27,40,1,0,28,24059},{19,40,3,0,24,24619},{9,40,0,0,10,25019},{28,40,2,0,32,25219},{22,40,3,0,27,25779},{22,37,3,3,27,26219},
  {22,38,3,2,28,26626},{19,31,1,9,23,27044},{19,31,1,9,21,27354},{9,31,0,9,9,27664},{20,31,1,9,23,27819},{16,31,2,9,21,28129},
  {19,31,1,9,23,28377},{19,31,1,9,21,28687},{15,31,-3,9,9,28997},{20,31,1,9,23,29245},{16,31,2,9,21,29555},{19,29,1,11,23,29803},
  {20,29,1,11,23,30093},{18,29,1,20,20,30383},{25,38,2,11,28,30644},{16,28,2,20,20,31138},{5,28,3,20,11,31362},{12,13,2,11,16,31446},
  {5,5,0,23,5,31524},
};
static const uint8_t FBM[31539] = {
  15,255,176,15,255,176,15,255,176,15,255,176,15,255,176,15,255,176,15,255,
  176,15,255,176,15,255,176,15,255,176,15,255,176,15,255,176,15,255,176,15,
  255,176,15,255,176,15,255,176,15,255,176,15,255,176,15,255,176,15,255,176,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,44,252,32,207,255,
  192,255,255,240,207,255,192,44,252,32,63,255,160,10,255,244,47,255,144,9,
  255,242,15,255,128,8,255,241,14,255,96,6,255,240,13,255,80,5,255,208,
  12,255,64,4,255,192,11,255,48,3,255,176,9,255,16,1,255,144,8,255,
  0,0,255,128,7,254,0,0,239,112,0,0,0,95,250,0,0,13,255,32,
  0,0,0,0,127,248,0,0,31,255,0,0,0,0,0,159,246,0,0,63,
  253,0,0,0,0,0,207,244,0,0,95,251,0,0,0,0,0,239,242,0,
  0,127,249,0,0,0,0,1,255,240,0,0,159,246,0,0,0,0,3,255,
  192,0,0,191,244,0,0,0,0,5,255,160,0,0,223,242,0,0,9,153,
  156,255,217,153,153,255,249,153,144,15,255,255,255,255,255,255,255,255,255,240,
  15,255,255,255,255,255,255,255,255,255,240,15,255,255,255,255,255,255,255,255,
  255,240,0,0,31,254,0,0,9,255,96,0,0,0,0,63,252,0,0,11,
  255,64,0,0,0,0,95,250,0,0,14,255,32,0,0,0,0,143,248,0,
  0,31,255,0,0,0,0,0,175,246,0,0,63,253,0,0,0,255,255,255,
  255,255,255,255,255,255,255,0,255,255,255,255,255,255,255,255,255,255,0,255,
  255,255,255,255,255,255,255,255,255,0,153,155,255,233,153,153,239,250,153,153,
  0,0,6,255,160,0,0,239,242,0,0,0,0,8,255,128,0,1,255,240,
  0,0,0,0,10,255,96,0,3,255,192,0,0,0,0,12,255,48,0,5,
  255,160,0,0,0,0,14,255,16,0,7,255,128,0,0,0,0,31,254,0,
  0,10,255,96,0,0,0,0,79,252,0,0,12,255,64,0,0,0,0,111,
  250,0,0,14,255,32,0,0,0,0,0,0,0,13,255,0,0,0,0,0,
  0,0,0,13,255,0,0,0,0,0,0,0,0,13,255,0,0,0,0,0,
  0,0,0,13,255,0,0,0,0,0,0,0,57,207,255,200,48,0,0,0,
  0,43,255,255,255,255,250,16,0,0,2,239,255,255,255,255,255,211,0,0,
  13,255,255,222,255,239,255,254,32,0,111,255,228,13,255,6,239,254,48,0,
  191,255,64,13,255,0,61,227,0,0,239,253,0,13,255,0,2,48,0,0,
  255,251,0,13,255,0,0,0,0,0,239,253,0,13,255,0,0,0,0,0,
  207,255,64,13,255,0,0,0,0,0,143,255,228,13,255,0,0,0,0,0,
  30,255,255,174,255,0,0,0,0,0,5,255,255,255,255,16,0,0,0,0,
  0,77,255,255,255,233,48,0,0,0,0,1,125,255,255,255,249,16,0,0,
  0,0,0,94,255,255,255,193,0,0,0,0,0,13,255,239,255,251,0,0,
  0,0,0,13,255,24,255,255,64,0,0,0,0,13,255,0,143,255,160,0,
  0,0,0,13,255,0,30,255,208,0,0,0,0,13,255,0,12,255,240,0,
  18,0,0,13,255,0,11,255,240,1,204,16,0,13,255,0,14,255,208,29,
  255,193,0,13,255,0,127,255,160,62,255,254,80,13,255,24,255,255,80,4,
  255,255,254,190,255,255,255,251,0,0,78,255,255,255,255,255,255,193,0,0,
  2,191,255,255,255,255,249,16,0,0,0,3,140,239,255,183,32,0,0,0,
  0,0,0,13,255,0,0,0,0,0,0,0,0,13,255,0,0,0,0,0,
  0,0,0,13,255,0,0,0,0,0,0,0,0,6,136,0,0,0,0,0,
  41,223,217,32,0,0,0,6,255,243,3,239,255,255,227,0,0,0,30,255,
  144,30,255,254,255,254,16,0,0,159,254,16,143,253,32,61,255,128,0,3,
  255,246,0,223,244,0,4,255,208,0,12,255,192,0,255,240,0,0,255,240,
  0,111,255,48,0,255,240,0,0,255,224,1,239,249,0,0,207,244,0,5,
  255,192,9,255,225,0,0,127,253,48,78,255,112,63,255,96,0,0,29,255,
  255,255,253,16,207,252,0,0,0,2,223,255,255,210,6,255,243,0,0,0,
  0,24,206,200,16,30,255,144,0,0,0,0,0,0,0,0,159,254,16,0,
  0,0,0,0,0,0,3,255,246,0,0,0,0,0,0,0,0,12,255,176,
  0,0,0,0,0,0,0,0,111,255,48,0,0,0,0,0,0,0,1,239,
  249,0,0,0,0,0,0,0,0,9,255,225,1,124,236,129,0,0,0,0,
  63,255,80,45,255,255,253,32,0,0,0,207,251,0,223,255,255,255,209,0,
  0,6,255,243,7,255,228,4,223,247,0,0,30,255,128,12,255,80,0,79,
  252,0,0,159,254,16,14,255,16,0,15,255,0,3,255,245,0,14,255,16,
  0,15,255,0,12,255,176,0,12,255,80,0,79,252,0,111,255,48,0,7,
  255,211,2,223,248,1,239,248,0,0,1,223,255,239,255,225,9,255,209,0,
  0,0,62,255,255,254,48,63,255,80,0,0,0,1,157,253,146,0,0,0,
  0,5,173,255,217,48,0,0,0,0,0,0,2,207,255,255,255,250,16,0,
  0,0,0,0,46,255,255,255,255,255,193,0,0,0,0,0,207,255,252,154,
  223,255,252,0,0,0,0,6,255,254,64,0,5,239,251,0,0,0,0,11,
  255,244,0,0,0,62,144,0,0,0,0,14,255,208,0,0,0,3,0,0,
  0,0,0,15,255,176,0,0,0,0,0,0,0,0,0,14,255,192,0,0,
  0,0,0,0,0,0,0,12,255,241,0,0,0,0,0,0,0,0,0,7,
  255,249,0,0,0,0,0,0,0,0,0,1,239,255,80,0,0,0,0,0,
  0,0,0,0,111,255,227,0,0,0,0,0,0,0,0,4,223,255,254,32,
  0,0,0,0,0,0,0,111,255,255,255,209,0,0,0,0,0,0,5,255,
  254,108,255,252,16,0,0,0,0,0,30,255,227,1,223,255,176,0,0,0,
  0,0,127,255,96,0,45,255,249,0,0,0,0,0,191,254,16,0,3,239,
  255,128,0,0,0,0,239,252,0,0,0,63,255,246,0,0,0,0,255,251,
  0,0,0,4,255,255,80,0,0,0,239,253,0,0,0,0,111,255,227,0,
  0,0,207,255,48,0,0,0,7,255,254,32,0,0,127,255,193,0,0,0,
  0,191,255,209,0,0,30,255,252,64,0,0,42,255,255,252,16,0,6,255,
  255,253,169,172,255,255,255,255,176,0,0,143,255,255,255,255,255,255,124,255,
  250,0,0,5,223,255,255,255,255,179,1,207,255,128,0,0,5,173,255,236,
  131,0,0,45,255,246,63,255,160,47,255,144,15,255,128,14,255,96,13,255,
  80,12,255,64,11,255,48,9,255,16,8,255,0,7,254,0,0,0,0,3,
  0,0,0,0,127,128,0,0,7,255,246,0,0,79,255,160,0,1,239,252,
  0,0,10,255,226,0,0,63,255,112,0,0,175,254,16,0,2,255,248,0,
  0,8,255,242,0,0,13,255,192,0,0,47,255,128,0,0,111,255,64,0,
  0,159,255,16,0,0,191,254,0,0,0,223,253,0,0,0,239,252,0,0,
  0,255,251,0,0,0,255,251,0,0,0,239,251,0,0,0,223,252,0,0,
  0,207,254,0,0,0,175,255,16,0,0,127,255,48,0,0,63,255,96,0,
  0,14,255,160,0,0,10,255,225,0,0,4,255,246,0,0,0,223,252,0,
  0,0,127,255,80,0,0,13,255,208,0,0,5,255,248,0,0,0,175,255,
  80,0,0,28,255,244,0,0,1,223,193,0,0,0,24,16,0,48,0,0,
  0,27,228,0,0,0,159,255,64,0,0,28,255,226,0,0,2,239,251,0,
  0,0,95,255,96,0,0,11,255,208,0,0,3,255,246,0,0,0,207,253,
  0,0,0,111,255,48,0,0,31,255,128,0,0,12,255,208,0,0,8,255,
  241,0,0,6,255,244,0,0,3,255,247,0,0,2,255,249,0,0,1,255,
  250,0,0,0,255,250,0,0,0,255,251,0,0,0,255,250,0,0,1,255,
  249,0,0,3,255,248,0,0,5,255,245,0,0,7,255,243,0,0,11,255,
  224,0,0,14,255,160,0,0,79,255,96,0,0,175,254,16,0,1,255,249,
  0,0,8,255,242,0,0,47,255,144,0,0,207,254,32,0,9,255,246,0,
  0,143,255,144,0,0,62,250,0,0,0,2,128,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,10,179,0,0,0,60,16,0,30,255,96,0,
  3,239,160,0,111,254,16,0,10,255,247,0,191,245,0,0,0,143,255,66,
  255,176,0,0,0,5,255,233,255,32,0,0,0,0,61,255,247,0,0,0,
  0,0,41,255,255,255,255,247,0,91,255,255,255,255,255,245,142,255,255,143,
  241,89,223,243,159,255,195,15,244,0,2,97,47,231,0,15,248,0,0,0,
  6,32,0,15,252,0,0,0,0,0,0,15,255,16,0,0,0,0,0,15,
  255,80,0,0,0,0,0,10,134,32,0,0,0,0,0,15,255,176,0,0,
  0,0,0,0,15,255,176,0,0,0,0,0,0,15,255,176,0,0,0,0,
  0,0,15,255,176,0,0,0,0,0,0,15,255,176,0,0,0,0,0,0,
  15,255,176,0,0,0,0,0,0,15,255,176,0,0,0,153,153,153,159,255,
  217,153,153,144,255,255,255,255,255,255,255,255,240,255,255,255,255,255,255,255,
  255,240,255,255,255,255,255,255,255,255,240,0,0,0,15,255,176,0,0,0,
  0,0,0,15,255,176,0,0,0,0,0,0,15,255,176,0,0,0,0,0,
  0,15,255,176,0,0,0,0,0,0,15,255,176,0,0,0,0,0,0,15,
  255,176,0,0,0,0,0,0,15,255,176,0,0,0,0,0,0,8,136,80,
  0,0,0,2,207,178,11,255,251,15,255,255,12,255,253,3,223,249,0,143,
  242,1,239,144,8,255,32,30,249,0,43,242,0,0,32,0,153,153,153,153,
  153,153,144,255,255,255,255,255,255,240,255,255,255,255,255,255,240,255,255,255,
  255,255,255,240,44,252,32,207,255,192,255,255,240,191,255,176,44,252,32,0,
  0,0,0,0,7,255,208,0,0,0,0,0,13,255,128,0,0,0,0,0,
  63,255,48,0,0,0,0,0,143,253,0,0,0,0,0,0,223,247,0,0,
  0,0,0,3,255,242,0,0,0,0,0,9,255,192,0,0,0,0,0,14,
  255,112,0,0,0,0,0,79,255,32,0,0,0,0,0,159,251,0,0,0,
  0,0,0,239,246,0,0,0,0,0,5,255,241,0,0,0,0,0,10,255,
  176,0,0,0,0,0,30,255,96,0,0,0,0,0,95,255,16,0,0,0,
  0,0,191,250,0,0,0,0,0,1,255,245,0,0,0,0,0,6,255,225,
  0,0,0,0,0,11,255,160,0,0,0,0,0,47,255,64,0,0,0,0,
  0,127,254,0,0,0,0,0,0,207,249,0,0,0,0,0,2,255,244,0,
  0,0,0,0,7,255,208,0,0,0,0,0,12,255,128,0,0,0,0,0,
  63,255,48,0,0,0,0,0,143,253,0,0,0,0,0,0,223,248,0,0,
  0,0,0,3,255,242,0,0,0,0,0,9,255,192,0,0,0,0,0,14,
  255,112,0,0,0,0,0,79,255,32,0,0,0,0,0,0,0,0,4,157,
  239,236,130,0,0,0,0,0,0,3,207,255,255,255,255,145,0,0,0,0,
  0,95,255,255,255,255,255,253,32,0,0,0,5,255,255,253,169,190,255,255,
  226,0,0,0,63,255,251,48,0,0,94,255,253,0,0,0,207,255,144,0,
  0,0,1,223,255,128,0,5,255,251,0,0,0,0,0,46,255,241,0,12,
  255,242,0,0,0,0,0,7,255,248,0,47,255,160,0,0,0,0,0,1,
  239,253,0,111,255,80,0,0,0,0,0,0,175,255,32,175,255,16,0,0,
  0,0,0,0,111,255,96,207,254,0,0,0,0,0,0,0,63,255,128,239,
  252,0,0,0,0,0,0,0,31,255,144,255,251,0,0,0,0,0,0,0,
  15,255,160,255,251,0,0,0,0,0,0,0,15,255,176,255,251,0,0,0,
  0,0,0,0,15,255,160,239,252,0,0,0,0,0,0,0,31,255,144,207,
  254,0,0,0,0,0,0,0,63,255,128,159,255,32,0,0,0,0,0,0,
  111,255,80,111,255,80,0,0,0,0,0,0,175,255,32,47,255,160,0,0,
  0,0,0,1,239,253,0,11,255,242,0,0,0,0,0,7,255,247,0,4,
  255,251,0,0,0,0,0,46,255,225,0,0,191,255,144,0,0,0,1,223,
  255,128,0,0,46,255,251,48,0,0,93,255,253,0,0,0,4,255,255,253,
  169,174,255,255,226,0,0,0,0,78,255,255,255,255,255,253,48,0,0,0,
  0,2,191,255,255,255,255,145,0,0,0,0,0,0,3,156,239,236,130,0,
  0,0,0,143,255,255,255,251,143,255,255,255,251,143,255,255,255,251,73,153,
  153,255,251,0,0,0,255,251,0,0,0,255,251,0,0,0,255,251,0,0,
  0,255,251,0,0,0,255,251,0,0,0,255,251,0,0,0,255,251,0,0,
  0,255,251,0,0,0,255,251,0,0,0,255,251,0,0,0,255,251,0,0,
  0,255,251,0,0,0,255,251,0,0,0,255,251,0,0,0,255,251,0,0,
  0,255,251,0,0,0,255,251,0,0,0,255,251,0,0,0,255,251,0,0,
  0,255,251,0,0,0,255,251,0,0,0,255,251,0,0,0,255,251,0,0,
  0,255,251,0,0,0,255,251,0,0,4,156,239,237,147,0,0,0,0,2,
  191,255,255,255,255,161,0,0,0,78,255,255,255,255,255,253,16,0,4,255,
  255,253,169,174,255,255,160,0,46,255,252,48,0,1,143,255,244,0,143,255,
  160,0,0,0,8,255,249,0,26,251,0,0,0,0,1,239,253,0,0,130,
  0,0,0,0,0,207,255,0,0,0,0,0,0,0,0,191,255,0,0,0,
  0,0,0,0,0,223,254,0,0,0,0,0,0,0,3,255,252,0,0,0,
  0,0,0,0,10,255,248,0,0,0,0,0,0,0,95,255,242,0,0,0,
  0,0,0,2,239,255,144,0,0,0,0,0,0,29,255,253,16,0,0,0,
  0,0,1,207,255,227,0,0,0,0,0,0,11,255,255,64,0,0,0,0,
  0,0,175,255,246,0,0,0,0,0,0,9,255,255,112,0,0,0,0,0,
  0,143,255,248,0,0,0,0,0,0,7,255,255,128,0,0,0,0,0,0,
  111,255,249,0,0,0,0,0,0,5,255,255,160,0,0,0,0,0,0,79,
  255,250,0,0,0,0,0,0,3,239,255,176,0,0,0,0,0,0,46,255,
  255,169,153,153,153,153,153,144,223,255,255,255,255,255,255,255,255,240,255,255,
  255,255,255,255,255,255,255,240,255,255,255,255,255,255,255,255,255,240,0,255,
  255,255,255,255,255,255,255,240,0,255,255,255,255,255,255,255,255,240,0,255,
  255,255,255,255,255,255,255,208,0,153,153,153,153,153,158,255,254,32,0,0,
  0,0,0,0,143,255,243,0,0,0,0,0,0,6,255,255,80,0,0,0,
  0,0,0,79,255,247,0,0,0,0,0,0,3,239,255,144,0,0,0,0,
  0,0,29,255,250,0,0,0,0,0,0,1,207,255,193,0,0,0,0,0,
  0,11,255,253,16,0,0,0,0,0,0,143,255,255,218,64,0,0,0,0,
  0,207,255,255,255,250,16,0,0,0,0,207,221,239,255,255,192,0,0,0,
  0,32,0,3,175,255,248,0,0,0,0,0,0,0,5,255,255,32,0,0,
  0,0,0,0,0,143,255,128,0,0,0,0,0,0,0,31,255,192,0,0,
  0,0,0,0,0,12,255,224,0,0,0,0,0,0,0,11,255,240,0,0,
  0,0,0,0,0,12,255,224,0,0,0,0,0,0,0,31,255,208,0,37,
  0,0,0,0,0,111,255,144,1,223,64,0,0,0,3,239,255,64,29,255,
  249,32,0,0,110,255,251,0,12,255,255,252,169,190,255,255,226,0,1,191,
  255,255,255,255,255,254,48,0,0,7,239,255,255,255,255,178,0,0,0,0,
  22,173,255,236,131,0,0,0,0,0,0,0,0,175,255,128,0,0,0,0,
  0,0,0,3,255,254,16,0,0,0,0,0,0,0,11,255,247,0,0,0,
  0,0,0,0,0,79,255,208,0,0,0,0,0,0,0,0,207,255,96,0,
  0,0,0,0,0,0,5,255,252,0,0,0,0,0,0,0,0,13,255,244,
  0,0,0,0,0,0,0,0,111,255,176,0,0,0,0,0,0,0,0,223,
  255,48,0,0,0,0,0,0,0,6,255,250,0,0,34,34,0,0,0,0,
  30,255,242,0,0,255,251,0,0,0,0,127,255,144,0,0,255,251,0,0,
  0,1,239,255,32,0,0,255,251,0,0,0,8,255,248,0,0,0,255,251,
  0,0,0,47,255,225,0,0,0,255,251,0,0,0,159,255,112,0,0,0,
  255,251,0,0,2,255,253,16,0,0,0,255,251,0,0,10,255,246,0,0,
  0,0,255,251,0,0,63,255,255,255,255,255,255,255,255,255,248,143,255,255,
  255,255,255,255,255,255,255,248,143,255,255,255,255,255,255,255,255,255,248,73,
  153,153,153,153,153,153,255,253,153,148,0,0,0,0,0,0,0,255,251,0,
  0,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,0,255,
  251,0,0,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,
  0,255,251,0,0,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,
  0,0,0,255,251,0,0,0,5,255,255,255,255,255,255,255,80,0,7,255,
  255,255,255,255,255,255,80,0,8,255,255,255,255,255,255,255,80,0,9,255,
  233,153,153,153,153,153,48,0,11,255,160,0,0,0,0,0,0,0,12,255,
  144,0,0,0,0,0,0,0,14,255,112,0,0,0,0,0,0,0,15,255,
  96,0,0,0,0,0,0,0,31,255,64,0,0,0,0,0,0,0,63,255,
  48,0,0,0,0,0,0,0,79,255,39,206,255,218,64,0,0,0,111,255,
  239,255,255,255,252,32,0,0,127,255,255,255,255,255,255,227,0,0,79,255,
  253,185,155,255,255,253,16,0,5,251,48,0,0,24,255,255,128,0,0,48,
  0,0,0,0,111,255,225,0,0,0,0,0,0,0,10,255,245,0,0,0,
  0,0,0,0,4,255,248,0,0,0,0,0,0,0,1,255,250,0,0,0,
  0,0,0,0,0,255,251,0,0,0,0,0,0,0,1,255,250,0,0,0,
  0,0,0,0,5,255,248,0,22,0,0,0,0,0,12,255,244,1,191,112,
  0,0,0,0,143,255,208,11,255,250,48,0,0,42,255,255,80,10,255,255,
  253,169,172,255,255,249,0,0,175,255,255,255,255,255,255,144,0,0,5,223,
  255,255,255,255,229,0,0,0,0,5,173,239,253,165,16,0,0,0,0,0,
  0,0,207,255,96,0,0,0,0,0,0,8,255,251,0,0,0,0,0,0,
  0,63,255,226,0,0,0,0,0,0,1,223,255,96,0,0,0,0,0,0,
  9,255,250,0,0,0,0,0,0,0,79,255,225,0,0,0,0,0,0,1,
  223,255,80,0,0,0,0,0,0,9,255,249,0,0,0,0,0,0,0,95,
  255,209,0,0,0,0,0,0,1,239,255,152,152,81,0,0,0,0,10,255,
  255,255,255,254,112,0,0,0,95,255,255,255,255,255,251,16,0,1,239,255,
  255,255,255,255,255,192,0,8,255,255,213,16,38,223,255,248,0,30,255,250,
  0,0,0,10,255,255,32,111,255,192,0,0,0,0,207,255,112,175,255,64,
  0,0,0,0,79,255,176,223,254,0,0,0,0,0,14,255,224,255,251,0,
  0,0,0,0,11,255,240,255,251,0,0,0,0,0,11,255,240,239,253,0,
  0,0,0,0,13,255,208,191,255,32,0,0,0,0,47,255,160,127,255,144,
  0,0,0,0,159,255,96,47,255,245,0,0,0,5,255,254,16,8,255,255,
  112,0,1,127,255,246,0,0,207,255,254,169,174,255,255,160,0,0,28,255,
  255,255,255,255,251,16,0,0,1,159,255,255,255,255,112,0,0,0,0,2,
  140,239,236,113,0,0,0,255,255,255,255,255,255,255,255,255,255,255,255,255,
  255,255,255,255,255,255,255,255,255,255,255,255,255,254,153,153,153,153,153,153,
  154,255,249,0,0,0,0,0,0,6,255,243,0,0,0,0,0,0,12,255,
  208,0,0,0,0,0,0,63,255,112,0,0,0,0,0,0,143,255,32,0,
  0,0,0,0,0,239,251,0,0,0,0,0,0,5,255,245,0,0,0,0,
  0,0,11,255,225,0,0,0,0,0,0,31,255,144,0,0,0,0,0,0,
  127,255,64,0,0,0,0,0,0,223,253,0,0,0,0,0,0,3,255,248,
  0,0,0,0,0,0,9,255,242,0,0,0,0,0,0,30,255,192,0,0,
  0,0,0,0,111,255,96,0,0,0,0,0,0,191,254,16,0,0,0,0,
  0,2,255,250,0,0,0,0,0,0,8,255,244,0,0,0,0,0,0,13,
  255,208,0,0,0,0,0,0,79,255,128,0,0,0,0,0,0,175,255,32,
  0,0,0,0,0,1,255,252,0,0,0,0,0,0,6,255,246,0,0,0,
  0,0,0,12,255,241,0,0,0,0,0,0,63,255,160,0,0,0,0,0,
  0,143,255,64,0,0,0,0,0,0,3,140,239,236,131,0,0,0,0,1,
  159,255,255,255,255,145,0,0,0,28,255,255,255,255,255,252,16,0,0,175,
  255,254,169,174,255,255,160,0,3,255,255,129,0,1,143,255,243,0,8,255,
  249,0,0,0,9,255,248,0,10,255,242,0,0,0,2,255,250,0,11,255,
  240,0,0,0,0,255,251,0,9,255,242,0,0,0,2,255,249,0,5,255,
  249,0,0,0,9,255,245,0,0,223,255,129,0,1,143,255,208,0,0,63,
  255,254,169,174,255,255,48,0,0,4,223,255,255,255,255,228,0,0,0,4,
  223,255,255,255,255,195,0,0,0,127,255,255,255,255,255,255,80,0,6,255,
  255,164,16,37,207,255,243,0,30,255,245,0,0,0,9,255,252,0,127,255,
  112,0,0,0,0,191,255,48,207,254,16,0,0,0,0,79,255,112,239,252,
  0,0,0,0,0,31,255,160,255,251,0,0,0,0,0,15,255,176,239,253,
  0,0,0,0,0,63,255,160,207,255,64,0,0,0,0,143,255,112,127,255,
  209,0,0,0,3,255,255,48,30,255,253,64,0,0,110,255,251,0,6,255,
  255,253,169,174,255,255,226,0,0,143,255,255,255,255,255,254,64,0,0,5,
  223,255,255,255,255,178,0,0,0,0,5,173,255,236,148,0,0,0,0,0,
  1,107,239,253,165,0,0,0,0,0,94,255,255,255,255,212,0,0,0,9,
  255,255,255,255,255,255,112,0,0,159,255,255,185,172,255,255,247,0,5,255,
  255,145,0,0,59,255,255,48,13,255,246,0,0,0,0,175,255,176,95,255,
  160,0,0,0,0,13,255,243,175,255,32,0,0,0,0,6,255,247,223,253,
  0,0,0,0,0,2,255,250,255,251,0,0,0,0,0,0,255,251,255,252,
  0,0,0,0,0,1,255,250,239,254,0,0,0,0,0,4,255,249,191,255,
  80,0,0,0,0,9,255,246,127,255,209,0,0,0,0,63,255,241,30,255,
  252,16,0,0,3,239,255,176,7,255,255,231,48,19,159,255,255,48,0,175,
  255,255,255,255,255,255,250,0,0,10,255,255,255,255,255,255,225,0,0,0,
  93,255,255,255,255,255,64,0,0,0,0,71,153,126,255,249,0,0,0,0,
  0,0,0,143,255,193,0,0,0,0,0,0,4,255,255,48,0,0,0,0,
  0,0,30,255,247,0,0,0,0,0,0,0,191,255,176,0,0,0,0,0,
  0,6,255,254,16,0,0,0,0,0,0,63,255,245,0,0,0,0,0,0,
  1,223,255,144,0,0,0,0,0,0,9,255,253,16,0,0,0,0,0,0,
  95,255,243,0,0,0,0,0,44,252,32,207,255,192,255,255,240,207,255,192,
  44,252,32,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,44,252,32,207,255,192,255,255,240,207,255,192,44,
  252,32,2,207,194,12,255,252,15,255,255,12,255,252,2,207,194,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,2,191,178,11,255,251,15,255,255,12,255,253,3,223,249,0,111,242,0,
  223,144,7,255,32,30,250,0,26,242,0,0,32,0,0,0,0,0,0,0,
  0,5,192,0,0,0,0,0,0,6,223,240,0,0,0,0,0,23,239,255,
  240,0,0,0,0,40,239,255,255,224,0,0,0,57,255,255,255,214,16,0,
  0,75,255,255,255,180,0,0,0,92,255,255,255,147,0,0,0,109,255,255,
  253,113,0,0,0,0,255,255,252,80,0,0,0,0,0,255,255,230,16,0,
  0,0,0,0,191,255,255,232,32,0,0,0,0,3,175,255,255,250,48,0,
  0,0,0,2,159,255,255,252,80,0,0,0,0,1,142,255,255,253,113,0,
  0,0,0,1,125,255,255,254,144,0,0,0,0,0,92,255,255,240,0,0,
  0,0,0,0,75,255,240,0,0,0,0,0,0,0,58,240,0,0,0,0,
  0,0,0,0,32,255,255,255,255,255,255,255,255,240,255,255,255,255,255,255,
  255,255,240,255,255,255,255,255,255,255,255,240,153,153,153,153,153,153,153,153,
  144,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,153,153,153,153,153,153,153,153,144,255,255,255,255,255,
  255,255,255,240,255,255,255,255,255,255,255,255,240,255,255,255,255,255,255,255,
  255,240,197,0,0,0,0,0,0,0,0,255,214,0,0,0,0,0,0,0,
  255,255,231,16,0,0,0,0,0,239,255,255,232,32,0,0,0,0,22,223,
  255,255,249,48,0,0,0,0,4,191,255,255,251,64,0,0,0,0,3,159,
  255,255,252,80,0,0,0,0,1,125,255,255,253,96,0,0,0,0,0,92,
  255,255,240,0,0,0,0,0,22,239,255,240,0,0,0,0,40,239,255,255,
  176,0,0,0,58,255,255,255,163,0,0,0,92,255,255,255,146,0,0,1,
  125,255,255,254,129,0,0,0,158,255,255,253,113,0,0,0,0,255,255,252,
  80,0,0,0,0,0,255,251,64,0,0,0,0,0,0,250,48,0,0,0,
  0,0,0,0,32,0,0,0,0,0,0,0,0,0,0,4,157,239,236,130,
  0,0,0,2,191,255,255,255,255,112,0,0,78,255,255,255,255,255,250,0,
  2,239,255,253,169,191,255,255,112,11,255,252,48,0,2,191,255,225,10,255,
  193,0,0,0,12,255,246,0,142,32,0,0,0,4,255,249,0,2,0,0,
  0,0,1,255,251,0,0,0,0,0,0,0,255,250,0,0,0,0,0,0,
  3,255,249,0,0,0,0,0,0,8,255,245,0,0,0,0,0,0,79,255,
  225,0,0,0,0,0,56,255,255,112,0,0,0,0,255,255,255,250,0,0,
  0,0,0,255,255,255,144,0,0,0,0,0,255,255,180,0,0,0,0,0,
  0,255,251,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,255,
  251,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,255,251,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,2,207,194,0,0,0,0,0,0,12,255,252,0,0,0,0,0,0,15,
  255,255,0,0,0,0,0,0,12,255,252,0,0,0,0,0,0,2,207,194,
  0,0,0,0,0,0,0,22,156,222,219,132,0,0,0,0,0,0,0,41,
  255,255,255,255,255,214,0,0,0,0,0,6,239,255,255,255,255,255,255,194,
  0,0,0,0,159,255,252,115,16,18,90,255,253,32,0,0,9,255,253,64,
  0,0,0,0,44,255,209,0,0,111,255,193,0,0,0,0,0,0,175,251,
  0,2,239,253,16,0,0,0,0,0,0,12,255,80,9,255,244,0,3,156,
  203,114,153,96,4,255,192,31,255,176,0,143,255,255,254,255,160,0,207,243,
  111,255,80,7,255,255,255,255,255,160,0,127,247,175,255,16,47,255,146,2,
  159,255,160,0,79,251,223,253,0,143,249,0,0,10,255,160,0,31,253,239,
  251,0,207,242,0,0,4,255,160,0,15,254,255,251,0,207,240,0,0,2,
  255,160,0,15,254,239,251,0,191,242,0,0,4,255,160,0,31,254,223,253,
  0,127,249,0,0,10,255,160,0,47,252,191,255,0,30,255,146,2,175,255,
  160,0,95,250,127,255,64,5,255,255,255,255,255,255,255,255,245,47,255,144,
  0,78,255,255,251,255,255,255,255,224,11,255,242,0,1,88,151,48,68,68,
  68,68,32,3,255,251,0,0,0,0,0,0,0,0,0,0,0,159,255,144,
  0,0,0,0,0,1,0,0,0,0,12,255,251,32,0,0,0,0,61,144,
  0,0,0,1,207,255,233,65,0,1,90,255,248,0,0,0,0,25,255,255,
  255,237,239,255,255,211,0,0,0,0,0,76,255,255,255,255,255,231,0,0,
  0,0,0,0,0,56,189,255,236,149,0,0,0,0,0,0,0,0,0,1,
  255,242,0,0,0,0,0,0,0,0,0,0,0,7,255,248,0,0,0,0,
  0,0,0,0,0,0,0,13,255,253,0,0,0,0,0,0,0,0,0,0,
  0,79,255,255,80,0,0,0,0,0,0,0,0,0,0,175,255,255,176,0,
  0,0,0,0,0,0,0,0,1,255,255,255,242,0,0,0,0,0,0,0,
  0,0,7,255,252,255,247,0,0,0,0,0,0,0,0,0,13,255,226,255,
  253,0,0,0,0,0,0,0,0,0,79,255,160,175,255,64,0,0,0,0,
  0,0,0,0,175,255,64,79,255,160,0,0,0,0,0,0,0,1,255,253,
  0,13,255,241,0,0,0,0,0,0,0,7,255,247,0,7,255,247,0,0,
  0,0,0,0,0,13,255,241,0,2,255,253,0,0,0,0,0,0,0,79,
  255,160,0,0,191,255,64,0,0,0,0,0,0,175,255,64,0,0,95,255,
  160,0,0,0,0,0,1,255,253,0,0,0,14,255,241,0,0,0,0,0,
  7,255,247,0,0,0,8,255,247,0,0,0,0,0,13,255,242,0,0,0,
  2,255,253,0,0,0,0,0,63,255,176,0,0,0,0,191,255,64,0,0,
  0,0,159,255,255,255,255,255,255,255,255,160,0,0,0,1,239,255,255,255,
  255,255,255,255,255,241,0,0,0,6,255,255,255,255,255,255,255,255,255,247,
  0,0,0,12,255,249,153,153,153,153,153,154,255,252,0,0,0,63,255,176,
  0,0,0,0,0,0,207,255,48,0,0,159,255,80,0,0,0,0,0,0,
  111,255,144,0,1,239,254,0,0,0,0,0,0,0,31,255,225,0,6,255,
  249,0,0,0,0,0,0,0,10,255,246,0,12,255,243,0,0,0,0,0,
  0,0,4,255,252,0,63,255,192,0,0,0,0,0,0,0,0,223,255,48,
  255,255,255,255,255,254,201,48,0,0,255,255,255,255,255,255,255,250,16,0,
  255,255,255,255,255,255,255,255,193,0,255,253,153,153,153,154,239,255,249,0,
  255,251,0,0,0,0,8,255,255,32,255,251,0,0,0,0,0,159,255,112,
  255,251,0,0,0,0,0,47,255,160,255,251,0,0,0,0,0,15,255,176,
  255,251,0,0,0,0,0,31,255,160,255,251,0,0,0,0,0,79,255,112,
  255,251,0,0,0,0,1,207,255,32,255,251,0,0,0,1,92,255,248,0,
  255,255,255,255,255,255,255,255,176,0,255,255,255,255,255,255,255,252,0,0,
  255,255,255,255,255,255,255,255,177,0,255,253,153,153,153,153,207,255,252,16,
  255,251,0,0,0,0,3,207,255,144,255,251,0,0,0,0,0,29,255,242,
  255,251,0,0,0,0,0,6,255,246,255,251,0,0,0,0,0,1,255,249,
  255,251,0,0,0,0,0,0,255,251,255,251,0,0,0,0,0,2,255,250,
  255,251,0,0,0,0,0,6,255,248,255,251,0,0,0,0,0,30,255,244,
  255,251,0,0,0,0,3,223,255,208,255,253,153,153,153,154,207,255,255,64,
  255,255,255,255,255,255,255,255,246,0,255,255,255,255,255,255,255,253,64,0,
  255,255,255,255,255,255,218,96,0,0,0,0,0,0,5,156,239,254,201,80,
  0,0,0,0,0,0,7,239,255,255,255,255,253,96,0,0,0,0,3,223,
  255,255,255,255,255,255,252,32,0,0,0,95,255,255,253,169,155,239,255,255,
  228,0,0,5,255,255,232,32,0,0,3,175,255,249,0,0,46,255,251,32,
  0,0,0,0,4,223,160,0,0,207,255,160,0,0,0,0,0,0,40,0,
  0,6,255,252,16,0,0,0,0,0,0,0,0,0,12,255,243,0,0,0,
  0,0,0,0,0,0,0,63,255,176,0,0,0,0,0,0,0,0,0,0,
  143,255,80,0,0,0,0,0,0,0,0,0,0,191,255,16,0,0,0,0,
  0,0,0,0,0,0,223,253,0,0,0,0,0,0,0,0,0,0,0,255,
  252,0,0,0,0,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,
  0,0,0,0,0,239,252,0,0,0,0,0,0,0,0,0,0,0,223,253,
  0,0,0,0,0,0,0,0,0,0,0,191,255,16,0,0,0,0,0,0,
  0,0,0,0,143,255,80,0,0,0,0,0,0,0,0,0,0,63,255,176,
  0,0,0,0,0,0,0,0,0,0,12,255,243,0,0,0,0,0,0,0,
  0,0,0,5,255,252,16,0,0,0,0,0,0,0,0,0,0,207,255,160,
  0,0,0,0,0,0,25,16,0,0,46,255,251,32,0,0,0,0,2,207,
  193,0,0,4,255,255,232,32,0,0,3,159,255,252,0,0,0,95,255,255,
  253,169,155,223,255,255,246,0,0,0,3,223,255,255,255,255,255,255,253,64,
  0,0,0,0,7,239,255,255,255,255,254,113,0,0,0,0,0,0,5,156,
  239,254,201,80,0,0,0,255,255,255,255,255,254,201,80,0,0,0,0,255,
  255,255,255,255,255,255,254,113,0,0,0,255,255,255,255,255,255,255,255,253,
  64,0,0,255,253,153,153,153,154,223,255,255,246,0,0,255,251,0,0,0,
  0,2,142,255,255,80,0,255,251,0,0,0,0,0,2,191,255,243,0,255,
  251,0,0,0,0,0,0,10,255,252,0,255,251,0,0,0,0,0,0,1,
  207,255,96,255,251,0,0,0,0,0,0,0,63,255,208,255,251,0,0,0,
  0,0,0,0,11,255,243,255,251,0,0,0,0,0,0,0,5,255,248,255,
  251,0,0,0,0,0,0,0,1,255,251,255,251,0,0,0,0,0,0,0,
  0,223,253,255,251,0,0,0,0,0,0,0,0,191,254,255,251,0,0,0,
  0,0,0,0,0,191,255,255,251,0,0,0,0,0,0,0,0,191,255,255,
  251,0,0,0,0,0,0,0,0,223,253,255,251,0,0,0,0,0,0,0,
  1,255,251,255,251,0,0,0,0,0,0,0,5,255,248,255,251,0,0,0,
  0,0,0,0,11,255,243,255,251,0,0,0,0,0,0,0,63,255,208,255,
  251,0,0,0,0,0,0,1,207,255,96,255,251,0,0,0,0,0,0,10,
  255,252,0,255,251,0,0,0,0,0,2,191,255,243,0,255,251,0,0,0,
  0,2,142,255,255,96,0,255,253,153,153,153,154,223,255,255,246,0,0,255,
  255,255,255,255,255,255,255,253,64,0,0,255,255,255,255,255,255,255,254,129,
  0,0,0,255,255,255,255,255,254,201,81,0,0,0,0,255,255,255,255,255,
  255,255,255,255,128,255,255,255,255,255,255,255,255,255,128,255,255,255,255,255,
  255,255,255,255,128,255,253,153,153,153,153,153,153,153,64,255,251,0,0,0,
  0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,255,251,0,0,0,
  0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,255,251,0,0,0,
  0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,255,251,0,0,0,
  0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,255,253,153,153,153,
  153,153,153,144,0,255,255,255,255,255,255,255,255,240,0,255,255,255,255,255,
  255,255,255,240,0,255,255,255,255,255,255,255,255,240,0,255,251,0,0,0,
  0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,255,251,0,0,0,
  0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,255,251,0,0,0,
  0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,255,251,0,0,0,
  0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,255,251,0,0,0,
  0,0,0,0,0,255,253,153,153,153,153,153,153,153,64,255,255,255,255,255,
  255,255,255,255,128,255,255,255,255,255,255,255,255,255,128,255,255,255,255,255,
  255,255,255,255,128,255,255,255,255,255,255,255,255,248,255,255,255,255,255,255,
  255,255,248,255,255,255,255,255,255,255,255,248,255,253,153,153,153,153,153,153,
  148,255,251,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,255,
  251,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,255,251,0,
  0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,255,251,0,0,0,
  0,0,0,0,255,251,0,0,0,0,0,0,0,255,251,0,0,0,0,0,
  0,0,255,255,255,255,255,255,255,255,240,255,255,255,255,255,255,255,255,240,
  255,255,255,255,255,255,255,255,240,255,253,153,153,153,153,153,153,144,255,251,
  0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,255,251,0,0,
  0,0,0,0,0,255,251,0,0,0,0,0,0,0,255,251,0,0,0,0,
  0,0,0,255,251,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,
  0,255,251,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,255,
  251,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,255,251,0,
  0,0,0,0,0,0,0,0,0,0,4,140,239,254,201,81,0,0,0,0,
  0,0,0,6,223,255,255,255,255,254,129,0,0,0,0,0,2,207,255,255,
  255,255,255,255,254,80,0,0,0,0,78,255,255,253,185,154,223,255,255,247,
  0,0,0,4,255,255,248,32,0,0,2,142,255,255,96,0,0,46,255,252,
  32,0,0,0,0,1,191,255,80,0,0,191,255,176,0,0,0,0,0,0,
  9,245,0,0,5,255,252,16,0,0,0,0,0,0,0,64,0,0,12,255,
  243,0,0,0,0,0,0,0,0,0,0,0,63,255,176,0,0,0,0,0,
  0,0,0,0,0,0,127,255,80,0,0,0,0,0,0,0,0,0,0,0,
  191,255,16,0,0,0,0,0,0,0,0,0,0,0,223,253,0,0,0,0,
  0,0,0,0,0,0,0,0,255,251,0,0,0,0,4,153,153,153,153,153,
  153,144,255,251,0,0,0,0,7,255,255,255,255,255,255,240,239,251,0,0,
  0,0,7,255,255,255,255,255,255,224,223,253,0,0,0,0,7,255,255,255,
  255,255,255,208,191,255,16,0,0,0,0,0,0,0,0,13,255,176,127,255,
  80,0,0,0,0,0,0,0,0,31,255,144,63,255,176,0,0,0,0,0,
  0,0,0,95,255,80,12,255,244,0,0,0,0,0,0,0,0,191,255,16,
  4,255,253,16,0,0,0,0,0,0,4,255,250,0,0,191,255,176,0,0,
  0,0,0,0,46,255,243,0,0,46,255,251,32,0,0,0,0,3,223,255,
  144,0,0,4,255,255,232,32,0,0,3,159,255,253,16,0,0,0,78,255,
  255,253,169,155,223,255,255,210,0,0,0,0,2,207,255,255,255,255,255,255,
  252,32,0,0,0,0,0,6,223,255,255,255,255,253,96,0,0,0,0,0,
  0,0,5,156,239,254,200,64,0,0,0,0,255,251,0,0,0,0,0,0,
  0,255,251,255,251,0,0,0,0,0,0,0,255,251,255,251,0,0,0,0,
  0,0,0,255,251,255,251,0,0,0,0,0,0,0,255,251,255,251,0,0,
  0,0,0,0,0,255,251,255,251,0,0,0,0,0,0,0,255,251,255,251,
  0,0,0,0,0,0,0,255,251,255,251,0,0,0,0,0,0,0,255,251,
  255,251,0,0,0,0,0,0,0,255,251,255,251,0,0,0,0,0,0,0,
  255,251,255,251,0,0,0,0,0,0,0,255,251,255,251,0,0,0,0,0,
  0,0,255,251,255,253,153,153,153,153,153,153,153,255,251,255,255,255,255,255,
  255,255,255,255,255,251,255,255,255,255,255,255,255,255,255,255,251,255,255,255,
  255,255,255,255,255,255,255,251,255,251,0,0,0,0,0,0,0,255,251,255,
  251,0,0,0,0,0,0,0,255,251,255,251,0,0,0,0,0,0,0,255,
  251,255,251,0,0,0,0,0,0,0,255,251,255,251,0,0,0,0,0,0,
  0,255,251,255,251,0,0,0,0,0,0,0,255,251,255,251,0,0,0,0,
  0,0,0,255,251,255,251,0,0,0,0,0,0,0,255,251,255,251,0,0,
  0,0,0,0,0,255,251,255,251,0,0,0,0,0,0,0,255,251,255,251,
  0,0,0,0,0,0,0,255,251,255,251,0,0,0,0,0,0,0,255,251,
  255,251,0,0,0,0,0,0,0,255,251,255,251,255,251,255,251,255,251,255,
  251,255,251,255,251,255,251,255,251,255,251,255,251,255,251,255,251,255,251,255,
  251,255,251,255,251,255,251,255,251,255,251,255,251,255,251,255,251,255,251,255,
  251,255,251,255,251,255,251,255,251,0,0,0,0,0,0,255,251,0,0,0,
  0,0,0,255,251,0,0,0,0,0,0,255,251,0,0,0,0,0,0,255,
  251,0,0,0,0,0,0,255,251,0,0,0,0,0,0,255,251,0,0,0,
  0,0,0,255,251,0,0,0,0,0,0,255,251,0,0,0,0,0,0,255,
  251,0,0,0,0,0,0,255,251,0,0,0,0,0,0,255,251,0,0,0,
  0,0,0,255,251,0,0,0,0,0,0,255,251,0,0,0,0,0,0,255,
  251,0,0,0,0,0,0,255,251,0,0,0,0,0,0,255,251,0,0,0,
  0,0,0,255,251,0,0,0,0,0,0,255,251,0,0,0,0,0,0,255,
  251,0,0,0,0,0,0,255,251,0,0,0,0,0,0,255,250,0,0,0,
  0,0,2,255,249,0,0,0,0,0,7,255,246,1,163,0,0,0,29,255,
  242,27,254,80,0,2,207,255,160,175,255,253,169,191,255,254,32,45,255,255,
  255,255,255,244,0,1,191,255,255,255,253,64,0,0,4,157,239,235,97,0,
  0,255,251,0,0,0,0,0,0,159,255,245,0,255,251,0,0,0,0,0,
  9,255,255,80,0,255,251,0,0,0,0,0,159,255,245,0,0,255,251,0,
  0,0,0,8,255,255,80,0,0,255,251,0,0,0,0,143,255,245,0,0,
  0,255,251,0,0,0,8,255,255,80,0,0,0,255,251,0,0,0,143,255,
  245,0,0,0,0,255,251,0,0,8,255,255,80,0,0,0,0,255,251,0,
  0,143,255,245,0,0,0,0,0,255,251,0,8,255,255,80,0,0,0,0,
  0,255,251,0,143,255,245,0,0,0,0,0,0,255,251,8,255,255,96,0,
  0,0,0,0,0,255,251,143,255,246,0,0,0,0,0,0,0,255,254,255,
  255,96,0,0,0,0,0,0,0,255,254,255,255,160,0,0,0,0,0,0,
  0,255,251,111,255,249,0,0,0,0,0,0,0,255,251,7,255,255,128,0,
  0,0,0,0,0,255,251,0,143,255,247,0,0,0,0,0,0,255,251,0,
  9,255,255,96,0,0,0,0,0,255,251,0,0,175,255,245,0,0,0,0,
  0,255,251,0,0,10,255,255,64,0,0,0,0,255,251,0,0,0,191,255,
  244,0,0,0,0,255,251,0,0,0,28,255,254,48,0,0,0,255,251,0,
  0,0,1,223,255,226,0,0,0,255,251,0,0,0,0,45,255,253,32,0,
  0,255,251,0,0,0,0,2,239,255,209,0,0,255,251,0,0,0,0,0,
  62,255,252,16,0,255,251,0,0,0,0,0,3,239,255,193,0,255,251,0,
  0,0,0,0,0,79,255,251,0,255,251,0,0,0,0,0,0,0,255,251,
  0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,255,251,0,0,
  0,0,0,0,0,255,251,0,0,0,0,0,0,0,255,251,0,0,0,0,
  0,0,0,255,251,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,
  0,255,251,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,255,
  251,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,255,251,0,
  0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,255,251,0,0,0,
  0,0,0,0,255,251,0,0,0,0,0,0,0,255,251,0,0,0,0,0,
  0,0,255,251,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,
  255,251,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,255,251,
  0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,255,251,0,0,
  0,0,0,0,0,255,251,0,0,0,0,0,0,0,255,253,153,153,153,153,
  153,153,148,255,255,255,255,255,255,255,255,248,255,255,255,255,255,255,255,255,
  248,255,255,255,255,255,255,255,255,248,255,209,0,0,0,0,0,0,0,0,
  0,1,223,240,255,247,0,0,0,0,0,0,0,0,0,7,255,240,255,254,
  32,0,0,0,0,0,0,0,0,46,255,240,255,255,160,0,0,0,0,0,
  0,0,0,175,255,240,255,255,244,0,0,0,0,0,0,0,4,255,255,240,
  255,255,252,0,0,0,0,0,0,0,12,255,255,240,255,255,255,96,0,0,
  0,0,0,0,111,255,255,240,255,255,255,225,0,0,0,0,0,1,239,255,
  255,240,255,254,255,249,0,0,0,0,0,9,255,255,255,240,255,251,191,255,
  48,0,0,0,0,63,255,203,255,240,255,251,63,255,176,0,0,0,0,191,
  255,59,255,240,255,251,9,255,245,0,0,0,5,255,249,11,255,240,255,251,
  1,239,253,16,0,0,29,255,225,11,255,240,255,251,0,111,255,128,0,0,
  143,255,112,11,255,240,255,251,0,12,255,242,0,2,255,253,0,11,255,240,
  255,251,0,4,255,250,0,10,255,244,0,11,255,240,255,251,0,0,175,255,
  64,79,255,160,0,11,255,240,255,251,0,0,46,255,192,207,255,32,0,11,
  255,240,255,251,0,0,7,255,252,255,248,0,0,11,255,240,255,251,0,0,
  0,223,255,255,209,0,0,11,255,240,255,251,0,0,0,95,255,255,80,0,
  0,11,255,240,255,251,0,0,0,11,255,251,0,0,0,11,255,240,255,251,
  0,0,0,2,255,243,0,0,0,11,255,240,255,251,0,0,0,0,17,16,
  0,0,0,11,255,240,255,251,0,0,0,0,0,0,0,0,0,11,255,240,
  255,251,0,0,0,0,0,0,0,0,0,11,255,240,255,251,0,0,0,0,
  0,0,0,0,0,11,255,240,255,251,0,0,0,0,0,0,0,0,0,11,
  255,240,255,251,0,0,0,0,0,0,0,0,0,11,255,240,255,225,0,0,
  0,0,0,0,0,255,251,255,251,0,0,0,0,0,0,0,255,251,255,255,
  96,0,0,0,0,0,0,255,251,255,255,243,0,0,0,0,0,0,255,251,
  255,255,253,16,0,0,0,0,0,255,251,255,255,255,144,0,0,0,0,0,
  255,251,255,255,255,245,0,0,0,0,0,255,251,255,253,255,254,32,0,0,
  0,0,255,251,255,251,127,255,192,0,0,0,0,255,251,255,251,11,255,248,
  0,0,0,0,255,251,255,251,1,239,255,64,0,0,0,255,251,255,251,0,
  79,255,225,0,0,0,255,251,255,251,0,8,255,251,0,0,0,255,251,255,
  251,0,0,207,255,112,0,0,255,251,255,251,0,0,46,255,243,0,0,255,
  251,255,251,0,0,5,255,253,16,0,255,251,255,251,0,0,0,175,255,160,
  0,255,251,255,251,0,0,0,29,255,245,0,255,251,255,251,0,0,0,3,
  255,254,32,255,251,255,251,0,0,0,0,127,255,192,255,251,255,251,0,0,
  0,0,11,255,248,255,251,255,251,0,0,0,0,1,239,255,255,251,255,251,
  0,0,0,0,0,79,255,255,251,255,251,0,0,0,0,0,8,255,255,251,
  255,251,0,0,0,0,0,0,207,255,251,255,251,0,0,0,0,0,0,46,
  255,251,255,251,0,0,0,0,0,0,5,255,251,255,251,0,0,0,0,0,
  0,0,175,251,255,251,0,0,0,0,0,0,0,31,251,0,0,0,0,4,
  156,239,254,200,64,0,0,0,0,0,0,0,7,223,255,255,255,255,253,96,
  0,0,0,0,0,3,207,255,255,255,255,255,255,252,32,0,0,0,0,78,
  255,255,253,169,155,223,255,255,228,0,0,0,4,255,255,232,32,0,0,3,
  159,255,255,64,0,0,46,255,252,32,0,0,0,0,2,207,255,226,0,0,
  191,255,176,0,0,0,0,0,0,28,255,251,0,5,255,253,16,0,0,0,
  0,0,0,1,223,255,80,12,255,243,0,0,0,0,0,0,0,0,79,255,
  192,63,255,176,0,0,0,0,0,0,0,0,11,255,243,143,255,80,0,0,
  0,0,0,0,0,0,5,255,247,191,255,16,0,0,0,0,0,0,0,0,
  1,255,251,223,253,0,0,0,0,0,0,0,0,0,0,223,253,255,251,0,
  0,0,0,0,0,0,0,0,0,207,254,255,251,0,0,0,0,0,0,0,
  0,0,0,191,255,239,252,0,0,0,0,0,0,0,0,0,0,191,254,223,
  253,0,0,0,0,0,0,0,0,0,0,223,253,191,255,16,0,0,0,0,
  0,0,0,0,1,255,251,127,255,80,0,0,0,0,0,0,0,0,5,255,
  247,47,255,176,0,0,0,0,0,0,0,0,11,255,243,11,255,244,0,0,
  0,0,0,0,0,0,79,255,192,4,255,253,16,0,0,0,0,0,0,1,
  223,255,64,0,191,255,177,0,0,0,0,0,0,28,255,251,0,0,30,255,
  252,32,0,0,0,0,2,207,255,226,0,0,3,239,255,249,32,0,0,2,
  159,255,254,48,0,0,0,62,255,255,253,169,154,223,255,255,228,0,0,0,
  0,2,207,255,255,255,255,255,255,252,32,0,0,0,0,0,6,223,255,255,
  255,255,253,96,0,0,0,0,0,0,0,4,140,239,254,201,64,0,0,0,
  0,255,255,255,255,255,254,182,16,0,0,255,255,255,255,255,255,255,229,0,
  0,255,255,255,255,255,255,255,255,128,0,255,253,153,153,153,172,255,255,245,
  0,255,251,0,0,0,0,60,255,254,16,255,251,0,0,0,0,1,207,255,
  96,255,251,0,0,0,0,0,79,255,176,255,251,0,0,0,0,0,13,255,
  224,255,251,0,0,0,0,0,11,255,240,255,251,0,0,0,0,0,11,255,
  240,255,251,0,0,0,0,0,13,255,208,255,251,0,0,0,0,0,79,255,
  176,255,251,0,0,0,0,1,207,255,96,255,251,0,0,0,0,60,255,254,
  16,255,253,153,153,153,156,255,255,245,0,255,255,255,255,255,255,255,255,112,
  0,255,255,255,255,255,255,255,229,0,0,255,255,255,255,255,254,182,16,0,
  0,255,251,0,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,
  0,255,251,0,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,
  0,255,251,0,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,
  0,255,251,0,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,
  0,255,251,0,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,
  0,255,251,0,0,0,0,0,0,0,0,0,0,0,0,4,156,239,254,201,
  64,0,0,0,0,0,0,0,0,6,223,255,255,255,255,253,96,0,0,0,
  0,0,0,2,207,255,255,255,255,255,255,252,32,0,0,0,0,0,78,255,
  255,253,169,155,223,255,255,228,0,0,0,0,4,255,255,232,32,0,0,2,
  159,255,255,64,0,0,0,46,255,252,32,0,0,0,0,2,207,255,226,0,
  0,0,191,255,176,0,0,0,0,0,0,27,255,251,0,0,5,255,253,16,
  0,0,0,0,0,0,1,223,255,80,0,12,255,243,0,0,0,0,0,0,
  0,0,79,255,192,0,63,255,176,0,0,0,0,0,0,0,0,11,255,243,
  0,127,255,80,0,0,0,0,0,0,0,0,5,255,247,0,191,255,16,0,
  0,0,0,0,0,0,0,1,255,251,0,223,253,0,0,0,0,0,0,0,
  0,0,0,223,253,0,255,251,0,0,0,0,0,0,0,0,0,0,207,254,
  0,255,251,0,0,0,0,0,0,0,0,0,0,191,255,0,239,252,0,0,
  0,0,0,0,182,0,0,0,191,254,0,223,253,0,0,0,0,0,11,255,
  96,0,0,223,253,0,191,255,16,0,0,0,0,111,255,246,0,1,255,251,
  0,127,255,96,0,0,0,0,9,255,255,80,5,255,247,0,47,255,192,0,
  0,0,0,0,159,255,245,11,255,243,0,11,255,244,0,0,0,0,0,9,
  255,255,143,255,192,0,4,255,253,16,0,0,0,0,0,159,255,255,255,80,
  0,0,175,255,177,0,0,0,0,0,9,255,255,251,0,0,0,29,255,252,
  48,0,0,0,0,2,239,255,246,0,0,0,3,239,255,249,48,0,0,2,
  143,255,255,255,64,0,0,0,62,255,255,253,185,154,223,255,255,255,255,228,
  0,0,0,2,191,255,255,255,255,255,255,252,58,255,254,48,0,0,0,5,
  223,255,255,255,255,253,96,0,191,255,227,0,0,0,0,4,140,239,254,201,
  64,0,0,11,255,246,0,0,0,0,0,0,0,0,0,0,0,0,0,191,
  112,0,0,0,0,0,0,0,0,0,0,0,0,0,21,0,255,255,255,255,
  255,254,200,32,0,0,0,255,255,255,255,255,255,255,248,0,0,0,255,255,
  255,255,255,255,255,255,176,0,0,255,253,153,153,153,155,239,255,249,0,0,
  255,251,0,0,0,0,24,255,255,48,0,255,251,0,0,0,0,0,159,255,
  144,0,255,251,0,0,0,0,0,31,255,208,0,255,251,0,0,0,0,0,
  12,255,224,0,255,251,0,0,0,0,0,11,255,240,0,255,251,0,0,0,
  0,0,13,255,224,0,255,251,0,0,0,0,0,63,255,176,0,255,251,0,
  0,0,0,1,207,255,112,0,255,251,0,0,0,2,109,255,254,16,0,255,
  255,255,255,255,255,255,255,245,0,0,255,255,255,255,255,255,255,255,96,0,
  0,255,255,255,255,255,255,255,179,0,0,0,255,253,153,223,255,249,98,0,
  0,0,0,255,251,0,46,255,251,0,0,0,0,0,255,251,0,4,255,255,
  128,0,0,0,0,255,251,0,0,127,255,245,0,0,0,0,255,251,0,0,
  10,255,254,32,0,0,0,255,251,0,0,1,207,255,209,0,0,0,255,251,
  0,0,0,46,255,251,0,0,0,255,251,0,0,0,4,255,255,128,0,0,
  255,251,0,0,0,0,127,255,245,0,0,255,251,0,0,0,0,10,255,254,
  32,0,255,251,0,0,0,0,1,207,255,209,0,255,251,0,0,0,0,0,
  46,255,250,0,255,251,0,0,0,0,0,4,255,255,112,0,0,0,57,206,
  254,217,64,0,0,0,0,43,255,255,255,255,252,64,0,0,2,239,255,255,
  255,255,255,247,0,0,13,255,255,218,154,223,255,255,96,0,111,255,228,0,
  0,4,207,255,64,0,191,255,48,0,0,0,10,244,0,0,239,252,0,0,
  0,0,0,48,0,0,255,251,0,0,0,0,0,0,0,0,239,253,0,0,
  0,0,0,0,0,0,207,255,80,0,0,0,0,0,0,0,143,255,246,0,
  0,0,0,0,0,0,46,255,255,197,0,0,0,0,0,0,6,255,255,255,
  232,32,0,0,0,0,0,94,255,255,255,251,64,0,0,0,0,1,158,255,
  255,255,250,16,0,0,0,0,1,108,255,255,255,210,0,0,0,0,0,0,
  57,255,255,252,0,0,0,0,0,0,0,42,255,255,80,0,0,0,0,0,
  0,0,175,255,160,0,0,0,0,0,0,0,31,255,208,0,0,0,0,0,
  0,0,12,255,240,0,32,0,0,0,0,0,11,255,240,3,231,0,0,0,
  0,0,14,255,208,62,255,96,0,0,0,0,111,255,160,143,255,251,48,0,
  0,24,255,255,80,10,255,255,253,169,155,239,255,251,0,0,159,255,255,255,
  255,255,255,193,0,0,5,223,255,255,255,255,249,16,0,0,0,5,156,239,
  253,183,32,0,0,143,255,255,255,255,255,255,255,255,255,255,240,143,255,255,
  255,255,255,255,255,255,255,255,240,143,255,255,255,255,255,255,255,255,255,255,
  240,73,153,153,153,153,255,253,153,153,153,153,144,0,0,0,0,0,255,251,
  0,0,0,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,
  0,0,255,251,0,0,0,0,0,0,0,0,0,0,255,251,0,0,0,0,
  0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,0,0,255,251,
  0,0,0,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,
  0,0,255,251,0,0,0,0,0,0,0,0,0,0,255,251,0,0,0,0,
  0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,0,0,255,251,
  0,0,0,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,
  0,0,255,251,0,0,0,0,0,0,0,0,0,0,255,251,0,0,0,0,
  0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,0,0,255,251,
  0,0,0,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,
  0,0,255,251,0,0,0,0,0,0,0,0,0,0,255,251,0,0,0,0,
  0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,0,0,255,251,
  0,0,0,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,
  0,0,255,251,0,0,0,0,0,0,0,0,0,0,255,251,0,0,0,0,
  0,0,0,0,0,0,255,251,0,0,0,0,0,255,251,0,0,0,0,0,
  0,0,255,251,255,251,0,0,0,0,0,0,0,255,251,255,251,0,0,0,
  0,0,0,0,255,251,255,251,0,0,0,0,0,0,0,255,251,255,251,0,
  0,0,0,0,0,0,255,251,255,251,0,0,0,0,0,0,0,255,251,255,
  251,0,0,0,0,0,0,0,255,251,255,251,0,0,0,0,0,0,0,255,
  251,255,251,0,0,0,0,0,0,0,255,251,255,251,0,0,0,0,0,0,
  0,255,251,255,251,0,0,0,0,0,0,0,255,251,255,251,0,0,0,0,
  0,0,0,255,251,255,251,0,0,0,0,0,0,0,255,251,255,251,0,0,
  0,0,0,0,0,255,251,255,251,0,0,0,0,0,0,0,255,251,255,251,
  0,0,0,0,0,0,0,255,251,255,251,0,0,0,0,0,0,0,255,251,
  255,251,0,0,0,0,0,0,0,255,251,255,251,0,0,0,0,0,0,0,
  255,251,239,253,0,0,0,0,0,0,2,255,250,191,255,16,0,0,0,0,
  0,5,255,247,127,255,112,0,0,0,0,0,11,255,243,47,255,226,0,0,
  0,0,0,79,255,208,10,255,252,16,0,0,0,3,239,255,96,2,239,255,
  230,0,0,1,127,255,251,0,0,79,255,255,235,154,191,255,255,210,0,0,
  4,239,255,255,255,255,255,252,32,0,0,0,42,255,255,255,255,255,129,0,
  0,0,0,0,56,206,255,219,113,0,0,0,159,255,128,0,0,0,0,0,
  0,0,14,255,242,63,255,208,0,0,0,0,0,0,0,95,255,176,12,255,
  244,0,0,0,0,0,0,0,191,255,80,6,255,249,0,0,0,0,0,0,
  1,255,254,0,1,255,254,16,0,0,0,0,0,7,255,248,0,0,175,255,
  96,0,0,0,0,0,13,255,242,0,0,79,255,176,0,0,0,0,0,63,
  255,192,0,0,13,255,242,0,0,0,0,0,159,255,96,0,0,8,255,247,
  0,0,0,0,0,239,254,16,0,0,2,255,253,0,0,0,0,5,255,249,
  0,0,0,0,191,255,64,0,0,0,11,255,243,0,0,0,0,95,255,144,
  0,0,0,47,255,208,0,0,0,0,14,255,225,0,0,0,127,255,112,0,
  0,0,0,9,255,245,0,0,0,223,255,16,0,0,0,0,3,255,251,0,
  0,3,255,250,0,0,0,0,0,0,207,255,32,0,9,255,244,0,0,0,
  0,0,0,127,255,112,0,30,255,208,0,0,0,0,0,0,31,255,208,0,
  95,255,128,0,0,0,0,0,0,10,255,244,0,191,255,32,0,0,0,0,
  0,0,4,255,249,2,255,251,0,0,0,0,0,0,0,0,223,254,23,255,
  245,0,0,0,0,0,0,0,0,143,255,93,255,225,0,0,0,0,0,0,
  0,0,47,255,223,255,144,0,0,0,0,0,0,0,0,11,255,255,255,48,
  0,0,0,0,0,0,0,0,6,255,255,252,0,0,0,0,0,0,0,0,
  0,1,239,255,246,0,0,0,0,0,0,0,0,0,0,159,255,241,0,0,
  0,0,0,0,0,0,0,0,63,255,160,0,0,0,0,0,0,0,0,0,
  0,12,255,64,0,0,0,0,0,159,255,48,0,0,0,0,0,14,255,32,
  0,0,0,0,0,14,255,192,79,255,128,0,0,0,0,0,63,255,96,0,
  0,0,0,0,79,255,112,14,255,192,0,0,0,0,0,143,255,176,0,0,
  0,0,0,143,255,32,9,255,242,0,0,0,0,0,207,255,241,0,0,0,
  0,0,223,253,0,5,255,246,0,0,0,0,2,255,255,245,0,0,0,0,
  3,255,248,0,1,239,251,0,0,0,0,6,255,255,249,0,0,0,0,7,
  255,243,0,0,175,255,16,0,0,0,11,255,255,254,0,0,0,0,12,255,
  208,0,0,111,255,80,0,0,0,31,255,207,255,48,0,0,0,31,255,144,
  0,0,31,255,144,0,0,0,95,255,62,255,128,0,0,0,111,255,64,0,
  0,11,255,224,0,0,0,159,253,10,255,192,0,0,0,175,254,0,0,0,
  6,255,244,0,0,0,239,249,6,255,242,0,0,1,239,250,0,0,0,2,
  255,248,0,0,3,255,244,1,255,246,0,0,5,255,245,0,0,0,0,207,
  253,0,0,8,255,225,0,207,251,0,0,9,255,241,0,0,0,0,127,255,
  32,0,12,255,160,0,127,255,16,0,14,255,176,0,0,0,0,63,255,112,
  0,47,255,96,0,63,255,80,0,63,255,96,0,0,0,0,13,255,176,0,
  111,255,16,0,13,255,144,0,143,255,16,0,0,0,0,8,255,241,0,191,
  252,0,0,9,255,224,0,207,252,0,0,0,0,0,4,255,246,1,255,247,
  0,0,4,255,243,2,255,247,0,0,0,0,0,0,239,250,5,255,243,0,
  0,0,239,248,7,255,242,0,0,0,0,0,0,159,254,9,255,208,0,0,
  0,175,252,11,255,208,0,0,0,0,0,0,95,255,78,255,144,0,0,0,
  95,255,63,255,128,0,0,0,0,0,0,30,255,207,255,64,0,0,0,31,
  255,191,255,48,0,0,0,0,0,0,10,255,255,254,16,0,0,0,11,255,
  255,253,0,0,0,0,0,0,0,5,255,255,250,0,0,0,0,7,255,255,
  249,0,0,0,0,0,0,0,1,255,255,246,0,0,0,0,2,255,255,244,
  0,0,0,0,0,0,0,0,191,255,242,0,0,0,0,0,223,255,224,0,
  0,0,0,0,0,0,0,111,255,192,0,0,0,0,0,143,255,160,0,0,
  0,0,0,0,0,0,47,255,112,0,0,0,0,0,79,255,80,0,0,0,
  0,0,0,0,0,12,255,48,0,0,0,0,0,14,255,16,0,0,0,0,
  29,255,248,0,0,0,0,0,0,0,159,255,160,4,255,255,48,0,0,0,
  0,0,4,255,254,16,0,159,255,209,0,0,0,0,0,29,255,244,0,0,
  29,255,249,0,0,0,0,0,159,255,144,0,0,3,255,255,64,0,0,0,
  4,255,253,16,0,0,0,127,255,209,0,0,0,29,255,244,0,0,0,0,
  12,255,250,0,0,0,159,255,128,0,0,0,0,2,239,255,80,0,5,255,
  252,0,0,0,0,0,0,111,255,226,0,30,255,243,0,0,0,0,0,0,
  10,255,251,0,175,255,112,0,0,0,0,0,0,1,239,255,101,255,252,0,
  0,0,0,0,0,0,0,95,255,238,255,226,0,0,0,0,0,0,0,0,
  9,255,255,255,96,0,0,0,0,0,0,0,0,1,223,255,251,0,0,0,
  0,0,0,0,0,0,0,191,255,252,0,0,0,0,0,0,0,0,0,6,
  255,255,255,112,0,0,0,0,0,0,0,0,46,255,255,255,243,0,0,0,
  0,0,0,0,0,191,255,104,255,252,0,0,0,0,0,0,0,7,255,251,
  0,207,255,112,0,0,0,0,0,0,63,255,226,0,63,255,242,0,0,0,
  0,0,0,223,255,80,0,8,255,252,0,0,0,0,0,9,255,250,0,0,
  1,223,255,112,0,0,0,0,79,255,209,0,0,0,79,255,242,0,0,0,
  1,223,255,64,0,0,0,9,255,252,0,0,0,10,255,248,0,0,0,0,
  1,223,255,112,0,0,95,255,208,0,0,0,0,0,79,255,226,0,2,239,
  255,48,0,0,0,0,0,9,255,251,0,11,255,247,0,0,0,0,0,0,
  1,223,255,112,127,255,192,0,0,0,0,0,0,0,79,255,226,30,255,246,
  0,0,0,0,0,0,0,159,255,160,6,255,254,16,0,0,0,0,0,3,
  255,254,32,0,191,255,144,0,0,0,0,0,12,255,247,0,0,47,255,243,
  0,0,0,0,0,111,255,192,0,0,8,255,252,0,0,0,0,1,239,255,
  64,0,0,1,223,255,96,0,0,0,10,255,249,0,0,0,0,95,255,225,
  0,0,0,79,255,225,0,0,0,0,10,255,249,0,0,0,223,255,96,0,
  0,0,0,2,239,255,64,0,7,255,251,0,0,0,0,0,0,127,255,192,
  0,46,255,242,0,0,0,0,0,0,13,255,247,0,175,255,128,0,0,0,
  0,0,0,4,255,254,36,255,253,16,0,0,0,0,0,0,0,175,255,173,
  255,244,0,0,0,0,0,0,0,0,30,255,255,255,160,0,0,0,0,0,
  0,0,0,6,255,255,254,32,0,0,0,0,0,0,0,0,0,207,255,247,
  0,0,0,0,0,0,0,0,0,0,63,255,208,0,0,0,0,0,0,0,
  0,0,0,15,255,176,0,0,0,0,0,0,0,0,0,0,15,255,176,0,
  0,0,0,0,0,0,0,0,0,15,255,176,0,0,0,0,0,0,0,0,
  0,0,15,255,176,0,0,0,0,0,0,0,0,0,0,15,255,176,0,0,
  0,0,0,0,0,0,0,0,15,255,176,0,0,0,0,0,0,0,0,0,
  0,15,255,176,0,0,0,0,0,0,0,0,0,0,15,255,176,0,0,0,
  0,0,0,0,0,0,0,15,255,176,0,0,0,0,0,0,0,0,0,0,
  15,255,176,0,0,0,0,0,0,0,0,0,0,15,255,176,0,0,0,0,
  0,0,0,0,0,0,15,255,176,0,0,0,0,0,15,255,255,255,255,255,
  255,255,255,255,15,255,255,255,255,255,255,255,255,255,15,255,255,255,255,255,
  255,255,255,255,9,153,153,153,153,153,153,158,255,247,0,0,0,0,0,0,
  0,95,255,192,0,0,0,0,0,0,1,239,255,48,0,0,0,0,0,0,
  10,255,247,0,0,0,0,0,0,0,111,255,192,0,0,0,0,0,0,2,
  239,255,48,0,0,0,0,0,0,11,255,248,0,0,0,0,0,0,0,111,
  255,208,0,0,0,0,0,0,2,239,255,64,0,0,0,0,0,0,11,255,
  249,0,0,0,0,0,0,0,111,255,209,0,0,0,0,0,0,2,239,255,
  64,0,0,0,0,0,0,11,255,249,0,0,0,0,0,0,0,111,255,225,
  0,0,0,0,0,0,2,239,255,80,0,0,0,0,0,0,11,255,250,0,
  0,0,0,0,0,0,127,255,225,0,0,0,0,0,0,2,239,255,80,0,
  0,0,0,0,0,11,255,250,0,0,0,0,0,0,0,127,255,226,0,0,
  0,0,0,0,2,255,255,96,0,0,0,0,0,0,12,255,251,0,0,0,
  0,0,0,0,127,255,251,153,153,153,153,153,153,153,255,255,255,255,255,255,
  255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
  255,255,255,255,255,255,255,255,240,255,255,255,255,240,255,254,238,238,224,255,
  240,0,0,0,255,240,0,0,0,255,240,0,0,0,255,240,0,0,0,255,
  240,0,0,0,255,240,0,0,0,255,240,0,0,0,255,240,0,0,0,255,
  240,0,0,0,255,240,0,0,0,255,240,0,0,0,255,240,0,0,0,255,
  240,0,0,0,255,240,0,0,0,255,240,0,0,0,255,240,0,0,0,255,
  240,0,0,0,255,240,0,0,0,255,240,0,0,0,255,240,0,0,0,255,
  240,0,0,0,255,240,0,0,0,255,240,0,0,0,255,240,0,0,0,255,
  240,0,0,0,255,240,0,0,0,255,240,0,0,0,255,255,255,255,240,255,
  255,255,255,240,255,255,255,255,240,79,255,32,0,0,0,0,0,14,255,112,
  0,0,0,0,0,9,255,192,0,0,0,0,0,3,255,242,0,0,0,0,
  0,0,223,248,0,0,0,0,0,0,143,253,0,0,0,0,0,0,63,255,
  48,0,0,0,0,0,12,255,128,0,0,0,0,0,7,255,208,0,0,0,
  0,0,2,255,244,0,0,0,0,0,0,207,249,0,0,0,0,0,0,127,
  254,0,0,0,0,0,0,47,255,64,0,0,0,0,0,11,255,160,0,0,
  0,0,0,6,255,225,0,0,0,0,0,1,255,245,0,0,0,0,0,0,
  191,250,0,0,0,0,0,0,95,255,16,0,0,0,0,0,30,255,96,0,
  0,0,0,0,10,255,176,0,0,0,0,0,5,255,241,0,0,0,0,0,
  0,239,246,0,0,0,0,0,0,159,251,0,0,0,0,0,0,79,255,32,
  0,0,0,0,0,14,255,112,0,0,0,0,0,9,255,192,0,0,0,0,
  0,3,255,242,0,0,0,0,0,0,223,247,0,0,0,0,0,0,143,253,
  0,0,0,0,0,0,63,255,48,0,0,0,0,0,13,255,128,0,0,0,
  0,0,7,255,208,255,255,255,255,240,255,255,255,255,240,238,238,238,255,240,
  0,0,0,255,240,0,0,0,255,240,0,0,0,255,240,0,0,0,255,240,
  0,0,0,255,240,0,0,0,255,240,0,0,0,255,240,0,0,0,255,240,
  0,0,0,255,240,0,0,0,255,240,0,0,0,255,240,0,0,0,255,240,
  0,0,0,255,240,0,0,0,255,240,0,0,0,255,240,0,0,0,255,240,
  0,0,0,255,240,0,0,0,255,240,0,0,0,255,240,0,0,0,255,240,
  0,0,0,255,240,0,0,0,255,240,0,0,0,255,240,0,0,0,255,240,
  0,0,0,255,240,0,0,0,255,240,0,0,0,255,240,255,255,255,255,240,
  255,255,255,255,240,255,255,255,255,240,0,0,2,136,48,0,0,0,0,11,
  255,192,0,0,0,0,79,255,245,0,0,0,0,207,255,253,0,0,0,5,
  255,184,255,96,0,0,13,255,49,239,225,0,0,111,249,0,127,248,0,1,
  239,226,0,30,255,32,8,255,128,0,6,255,144,46,254,16,0,0,223,243,
  159,246,0,0,0,95,251,153,153,153,153,153,153,153,153,153,144,255,255,255,
  255,255,255,255,255,255,240,255,255,255,255,255,255,255,255,255,240,255,255,255,
  255,255,255,255,255,255,240,0,3,0,0,0,0,143,64,0,0,8,255,226,
  0,0,46,255,252,0,0,3,223,255,144,0,0,44,255,246,0,0,1,175,
  255,48,0,0,8,255,64,0,0,0,85,0,0,0,3,140,239,235,96,11,
  255,240,0,1,159,255,255,255,253,59,255,240,0,28,255,255,255,255,255,253,
  255,240,0,207,255,255,185,172,255,255,255,240,8,255,255,129,0,0,42,255,
  255,240,30,255,246,0,0,0,0,143,255,240,127,255,160,0,0,0,0,13,
  255,240,191,255,32,0,0,0,0,7,255,240,239,253,0,0,0,0,0,4,
  255,240,255,251,0,0,0,0,0,2,255,240,255,251,0,0,0,0,0,3,
  255,240,239,253,0,0,0,0,0,5,255,240,191,255,32,0,0,0,0,8,
  255,240,127,255,160,0,0,0,0,13,255,240,31,255,246,0,0,0,0,159,
  255,240,8,255,255,145,0,0,42,255,255,240,1,207,255,255,185,156,255,255,
  255,240,0,44,255,255,255,255,255,253,255,240,0,1,159,255,255,255,253,75,
  255,240,0,0,3,156,239,235,97,11,255,240,255,240,0,0,0,0,0,0,
  0,0,255,240,0,0,0,0,0,0,0,0,255,240,0,0,0,0,0,0,
  0,0,255,240,0,0,0,0,0,0,0,0,255,240,0,0,0,0,0,0,
  0,0,255,240,0,0,0,0,0,0,0,0,255,240,0,0,0,0,0,0,
  0,0,255,240,0,0,0,0,0,0,0,0,255,240,0,0,0,0,0,0,
  0,0,255,240,1,107,239,236,130,0,0,0,255,240,77,255,255,255,255,145,
  0,0,255,245,255,255,255,255,255,252,16,0,255,255,255,252,169,191,255,255,
  192,0,255,255,250,32,0,1,143,255,248,0,255,255,144,0,0,0,6,255,
  255,16,255,253,0,0,0,0,0,175,255,112,255,246,0,0,0,0,0,47,
  255,176,255,242,0,0,0,0,0,13,255,224,255,240,0,0,0,0,0,11,
  255,240,255,240,0,0,0,0,0,11,255,240,255,242,0,0,0,0,0,13,
  255,224,255,246,0,0,0,0,0,63,255,176,255,253,0,0,0,0,0,175,
  255,112,255,255,144,0,0,0,6,255,255,32,255,255,250,32,0,1,159,255,
  248,0,255,255,255,252,169,191,255,255,193,0,255,246,255,255,255,255,255,253,
  32,0,255,240,94,255,255,255,255,145,0,0,255,240,1,107,239,236,147,0,
  0,0,0,0,1,123,239,253,165,0,0,0,0,126,255,255,255,255,212,0,
  0,27,255,255,255,255,255,255,112,0,191,255,255,185,156,255,255,209,7,255,
  255,145,0,0,25,253,32,30,255,246,0,0,0,0,82,0,111,255,160,0,
  0,0,0,0,0,191,255,32,0,0,0,0,0,0,239,253,0,0,0,0,
  0,0,0,255,251,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,
  0,223,253,0,0,0,0,0,0,0,191,255,32,0,0,0,0,0,0,111,
  255,160,0,0,0,0,0,0,30,255,246,0,0,0,0,83,0,6,255,255,
  145,0,0,24,254,32,0,175,255,255,185,155,255,255,209,0,10,255,255,255,
  255,255,255,112,0,0,110,255,255,255,255,212,0,0,0,1,107,239,253,165,
  0,0,0,0,0,0,0,0,0,11,255,240,0,0,0,0,0,0,0,11,
  255,240,0,0,0,0,0,0,0,11,255,240,0,0,0,0,0,0,0,11,
  255,240,0,0,0,0,0,0,0,11,255,240,0,0,0,0,0,0,0,11,
  255,240,0,0,0,0,0,0,0,11,255,240,0,0,0,0,0,0,0,11,
  255,240,0,0,0,0,0,0,0,11,255,240,0,0,3,156,239,235,96,11,
  255,240,0,1,159,255,255,255,253,59,255,240,0,28,255,255,255,255,255,237,
  255,240,0,207,255,255,185,172,255,255,255,240,8,255,255,129,0,0,43,255,
  255,240,30,255,246,0,0,0,0,175,255,240,127,255,160,0,0,0,0,13,
  255,240,191,255,32,0,0,0,0,7,255,240,239,253,0,0,0,0,0,4,
  255,240,255,251,0,0,0,0,0,2,255,240,255,251,0,0,0,0,0,3,
  255,240,239,253,0,0,0,0,0,5,255,240,191,255,32,0,0,0,0,8,
  255,240,127,255,160,0,0,0,0,13,255,240,31,255,246,0,0,0,0,159,
  255,240,8,255,255,145,0,0,43,255,255,240,1,207,255,255,185,172,255,255,
  255,240,0,45,255,255,255,255,255,253,255,240,0,1,175,255,255,255,253,59,
  255,240,0,0,3,157,255,235,96,11,255,240,0,0,1,107,239,253,165,0,
  0,0,0,0,110,255,255,255,255,195,0,0,0,10,255,255,255,255,255,254,
  64,0,0,175,255,254,185,172,255,255,226,0,6,255,255,113,0,0,60,255,
  251,0,30,255,244,0,0,0,1,223,255,48,111,255,128,0,0,0,0,95,
  255,144,191,255,16,0,0,0,0,14,255,192,239,255,255,255,255,255,255,255,
  255,224,255,255,255,255,255,255,255,255,255,240,255,255,255,255,255,255,255,255,
  255,224,223,255,153,153,153,153,153,153,153,128,191,255,32,0,0,0,0,0,
  0,0,111,255,144,0,0,0,0,0,0,0,30,255,246,0,0,0,0,38,
  0,0,6,255,255,145,0,0,6,239,144,0,0,175,255,255,201,155,239,255,
  247,0,0,10,255,255,255,255,255,255,177,0,0,0,110,255,255,255,255,232,
  0,0,0,0,1,106,223,254,183,16,0,0,0,0,0,0,40,206,253,146,
  0,0,0,0,6,239,255,255,254,80,0,0,0,95,255,255,255,255,208,0,
  0,1,239,255,234,156,253,32,0,0,8,255,251,16,0,98,0,0,0,12,
  255,242,0,0,0,0,0,0,14,255,192,0,0,0,0,0,0,15,255,176,
  0,0,0,0,0,0,15,255,176,0,0,0,0,0,0,15,255,176,0,0,
  0,0,143,255,255,255,255,255,255,128,0,143,255,255,255,255,255,255,128,0,
  143,255,255,255,255,255,255,128,0,73,153,159,255,217,153,153,64,0,0,0,
  15,255,176,0,0,0,0,0,0,15,255,176,0,0,0,0,0,0,15,255,
  176,0,0,0,0,0,0,15,255,176,0,0,0,0,0,0,15,255,176,0,
  0,0,0,0,0,15,255,176,0,0,0,0,0,0,15,255,176,0,0,0,
  0,0,0,15,255,176,0,0,0,0,0,0,15,255,176,0,0,0,0,0,
  0,15,255,176,0,0,0,0,0,0,15,255,176,0,0,0,0,0,0,15,
  255,176,0,0,0,0,0,0,15,255,176,0,0,0,0,0,0,15,255,176,
  0,0,0,0,0,0,15,255,176,0,0,0,0,0,0,15,255,176,0,0,
  0,0,0,0,4,173,255,218,64,11,255,240,0,1,191,255,255,255,251,27,
  255,240,0,45,255,255,255,255,255,204,255,240,1,223,255,254,185,174,255,255,
  255,240,9,255,255,112,0,0,110,255,255,240,47,255,244,0,0,0,4,255,
  255,240,127,255,144,0,0,0,0,143,255,240,191,255,32,0,0,0,0,47,
  255,240,239,253,0,0,0,0,0,13,255,240,255,251,0,0,0,0,0,11,
  255,240,255,251,0,0,0,0,0,11,255,240,239,253,0,0,0,0,0,13,
  255,240,191,255,32,0,0,0,0,31,255,240,127,255,144,0,0,0,0,143,
  255,240,47,255,244,0,0,0,3,255,255,240,9,255,255,112,0,0,94,255,
  255,240,1,223,255,254,185,173,255,255,255,240,0,45,255,255,255,255,255,204,
  255,240,0,2,191,255,255,255,251,27,255,240,0,0,4,173,255,218,64,11,
  255,240,0,0,0,0,0,0,0,14,255,208,0,0,0,0,0,0,0,79,
  255,144,3,214,0,0,0,0,1,223,255,64,95,255,163,0,0,0,93,255,
  251,0,62,255,255,219,153,190,255,255,226,0,4,239,255,255,255,255,255,254,
  48,0,0,42,255,255,255,255,255,161,0,0,0,0,40,190,255,236,130,0,
  0,0,255,251,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,
  255,251,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,255,251,
  0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,255,251,0,0,
  0,0,0,0,0,255,251,0,0,0,0,0,0,0,255,251,0,0,0,0,
  0,0,0,255,251,0,90,239,236,113,0,0,255,251,27,255,255,255,253,64,
  0,255,252,223,255,255,255,255,243,0,255,255,255,235,154,239,255,253,0,255,
  255,250,16,0,25,255,255,96,255,255,176,0,0,0,175,255,176,255,255,32,
  0,0,0,47,255,224,255,253,0,0,0,0,13,255,240,255,251,0,0,0,
  0,11,255,240,255,251,0,0,0,0,11,255,240,255,251,0,0,0,0,11,
  255,240,255,251,0,0,0,0,11,255,240,255,251,0,0,0,0,11,255,240,
  255,251,0,0,0,0,11,255,240,255,251,0,0,0,0,11,255,240,255,251,
  0,0,0,0,11,255,240,255,251,0,0,0,0,11,255,240,255,251,0,0,
  0,0,11,255,240,255,251,0,0,0,0,11,255,240,255,251,0,0,0,0,
  11,255,240,44,252,32,207,255,192,255,255,240,207,255,192,44,252,32,0,0,
  0,0,0,0,0,0,0,0,0,0,191,255,0,191,255,0,191,255,0,191,
  255,0,191,255,0,191,255,0,191,255,0,191,255,0,191,255,0,191,255,0,
  191,255,0,191,255,0,191,255,0,191,255,0,191,255,0,191,255,0,191,255,
  0,191,255,0,191,255,0,191,255,0,0,0,0,0,44,252,32,0,0,0,
  0,207,255,192,0,0,0,0,255,255,240,0,0,0,0,207,255,192,0,0,
  0,0,44,252,32,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,191,255,0,
  0,0,0,0,191,255,0,0,0,0,0,191,255,0,0,0,0,0,191,255,
  0,0,0,0,0,191,255,0,0,0,0,0,191,255,0,0,0,0,0,191,
  255,0,0,0,0,0,191,255,0,0,0,0,0,191,255,0,0,0,0,0,
  191,255,0,0,0,0,0,191,255,0,0,0,0,0,191,255,0,0,0,0,
  0,191,255,0,0,0,0,0,191,255,0,0,0,0,0,191,255,0,0,0,
  0,0,191,255,0,0,0,0,0,191,255,0,0,0,0,0,191,255,0,0,
  0,0,0,191,255,0,0,0,0,0,191,255,0,0,0,0,0,191,255,0,
  0,0,0,0,191,255,0,0,0,0,0,223,254,0,0,163,0,7,255,251,
  0,11,255,185,207,255,246,0,127,255,255,255,255,192,0,10,255,255,255,252,
  16,0,0,91,239,235,96,0,0,255,251,0,0,0,0,0,0,0,255,251,
  0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,255,251,0,0,
  0,0,0,0,0,255,251,0,0,0,0,0,0,0,255,251,0,0,0,0,
  0,0,0,255,251,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,
  0,255,251,0,0,0,0,0,0,0,255,251,0,0,0,4,255,254,48,255,
  251,0,0,0,62,255,244,0,255,251,0,0,2,239,255,80,0,255,251,0,
  0,29,255,246,0,0,255,251,0,1,207,255,112,0,0,255,251,0,11,255,
  248,0,0,0,255,251,0,175,255,160,0,0,0,255,251,8,255,251,0,0,
  0,0,255,251,127,255,193,0,0,0,0,255,254,255,255,16,0,0,0,0,
  255,252,223,255,144,0,0,0,0,255,251,62,255,247,0,0,0,0,255,251,
  4,255,255,80,0,0,0,255,251,0,95,255,244,0,0,0,255,251,0,8,
  255,254,32,0,0,255,251,0,0,159,255,210,0,0,255,251,0,0,11,255,
  252,16,0,255,251,0,0,1,207,255,176,0,255,251,0,0,0,46,255,249,
  0,255,251,0,0,0,3,239,255,112,255,251,255,251,255,251,255,251,255,251,
  255,251,255,251,255,251,255,251,255,251,255,251,255,251,255,251,255,251,255,251,
  255,251,255,251,255,251,255,251,255,251,255,251,255,251,255,251,255,251,255,251,
  255,251,255,251,255,251,255,251,255,251,0,107,239,235,96,0,0,41,223,236,
  130,0,0,255,251,61,255,255,255,253,48,7,255,255,255,254,96,0,255,253,
  239,255,255,255,255,226,143,255,255,255,255,245,0,255,255,255,234,155,239,255,
  253,255,253,169,223,255,254,16,255,255,248,16,0,26,255,255,255,112,0,6,
  255,255,128,255,255,144,0,0,0,207,255,248,0,0,0,111,255,192,255,255,
  16,0,0,0,79,255,225,0,0,0,14,255,224,255,252,0,0,0,0,31,
  255,192,0,0,0,12,255,240,255,251,0,0,0,0,15,255,176,0,0,0,
  11,255,240,255,251,0,0,0,0,15,255,176,0,0,0,11,255,240,255,251,
  0,0,0,0,15,255,176,0,0,0,11,255,240,255,251,0,0,0,0,15,
  255,176,0,0,0,11,255,240,255,251,0,0,0,0,15,255,176,0,0,0,
  11,255,240,255,251,0,0,0,0,15,255,176,0,0,0,11,255,240,255,251,
  0,0,0,0,15,255,176,0,0,0,11,255,240,255,251,0,0,0,0,15,
  255,176,0,0,0,11,255,240,255,251,0,0,0,0,15,255,176,0,0,0,
  11,255,240,255,251,0,0,0,0,15,255,176,0,0,0,11,255,240,255,251,
  0,0,0,0,15,255,176,0,0,0,11,255,240,255,251,0,0,0,0,15,
  255,176,0,0,0,11,255,240,255,251,0,90,239,235,96,0,0,255,251,27,
  255,255,255,252,32,0,255,252,223,255,255,255,255,209,0,255,255,255,235,154,
  239,255,251,0,255,255,250,16,0,25,255,255,64,255,255,176,0,0,0,175,
  255,160,255,255,32,0,0,0,47,255,208,255,253,0,0,0,0,13,255,240,
  255,251,0,0,0,0,11,255,240,255,251,0,0,0,0,11,255,240,255,251,
  0,0,0,0,11,255,240,255,251,0,0,0,0,11,255,240,255,251,0,0,
  0,0,11,255,240,255,251,0,0,0,0,11,255,240,255,251,0,0,0,0,
  11,255,240,255,251,0,0,0,0,11,255,240,255,251,0,0,0,0,11,255,
  240,255,251,0,0,0,0,11,255,240,255,251,0,0,0,0,11,255,240,255,
  251,0,0,0,0,11,255,240,0,0,1,123,239,253,165,0,0,0,0,0,
  110,255,255,255,255,212,0,0,0,10,255,255,255,255,255,255,112,0,0,175,
  255,255,185,172,255,255,246,0,6,255,255,129,0,0,59,255,255,48,30,255,
  246,0,0,0,0,175,255,176,111,255,144,0,0,0,0,29,255,242,191,255,
  32,0,0,0,0,7,255,247,239,253,0,0,0,0,0,2,255,249,255,251,
  0,0,0,0,0,0,255,251,255,251,0,0,0,0,0,1,255,251,223,253,
  0,0,0,0,0,3,255,249,175,255,48,0,0,0,0,7,255,246,111,255,
  160,0,0,0,0,29,255,242,30,255,246,0,0,0,0,175,255,160,6,255,
  255,145,0,0,59,255,254,32,0,175,255,255,185,172,255,255,246,0,0,10,
  255,255,255,255,255,255,96,0,0,0,110,255,255,255,255,212,0,0,0,0,
  1,123,239,253,165,0,0,0,255,240,1,107,239,236,130,0,0,0,255,240,
  77,255,255,255,255,145,0,0,255,245,255,255,255,255,255,252,16,0,255,255,
  255,252,169,191,255,255,192,0,255,255,250,32,0,1,143,255,248,0,255,255,
  144,0,0,0,6,255,255,16,255,253,0,0,0,0,0,175,255,112,255,246,
  0,0,0,0,0,47,255,176,255,242,0,0,0,0,0,13,255,224,255,240,
  0,0,0,0,0,11,255,240,255,240,0,0,0,0,0,11,255,240,255,242,
  0,0,0,0,0,13,255,224,255,246,0,0,0,0,0,63,255,176,255,253,
  0,0,0,0,0,175,255,112,255,255,144,0,0,0,6,255,255,32,255,255,
  250,32,0,1,159,255,248,0,255,255,255,252,169,191,255,255,193,0,255,246,
  255,255,255,255,255,253,32,0,255,240,94,255,255,255,255,145,0,0,255,240,
  1,107,239,236,147,0,0,0,255,240,0,0,0,0,0,0,0,0,255,240,
  0,0,0,0,0,0,0,0,255,240,0,0,0,0,0,0,0,0,255,240,
  0,0,0,0,0,0,0,0,255,240,0,0,0,0,0,0,0,0,255,240,
  0,0,0,0,0,0,0,0,255,240,0,0,0,0,0,0,0,0,255,240,
  0,0,0,0,0,0,0,0,0,0,2,140,239,235,97,2,255,248,0,1,
  159,255,255,255,253,66,255,248,0,28,255,255,255,255,255,246,255,248,0,207,
  255,255,185,172,255,255,255,248,8,255,255,129,0,0,43,255,255,248,31,255,
  246,0,0,0,0,175,255,248,127,255,160,0,0,0,0,30,255,248,191,255,
  32,0,0,0,0,8,255,248,239,253,0,0,0,0,0,4,255,248,255,251,
  0,0,0,0,0,3,255,248,255,251,0,0,0,0,0,3,255,248,239,253,
  0,0,0,0,0,4,255,248,191,255,32,0,0,0,0,8,255,248,127,255,
  160,0,0,0,0,30,255,248,47,255,246,0,0,0,0,191,255,248,8,255,
  255,145,0,0,43,255,255,248,1,207,255,255,185,172,255,255,255,248,0,45,
  255,255,255,255,255,246,255,248,0,1,159,255,255,255,253,66,255,248,0,0,
  3,156,239,235,97,2,255,248,0,0,0,0,0,0,0,2,255,248,0,0,
  0,0,0,0,0,2,255,248,0,0,0,0,0,0,0,2,255,248,0,0,
  0,0,0,0,0,2,255,248,0,0,0,0,0,0,0,2,255,248,0,0,
  0,0,0,0,0,2,255,248,0,0,0,0,0,0,0,2,255,248,0,0,
  0,0,0,0,0,2,255,248,255,251,2,157,255,199,16,255,251,95,255,255,
  255,210,255,253,255,255,255,255,244,255,255,255,234,155,255,112,255,255,247,0,
  0,55,0,255,255,128,0,0,0,0,255,255,16,0,0,0,0,255,252,0,
  0,0,0,0,255,251,0,0,0,0,0,255,251,0,0,0,0,0,255,251,
  0,0,0,0,0,255,251,0,0,0,0,0,255,251,0,0,0,0,0,255,
  251,0,0,0,0,0,255,251,0,0,0,0,0,255,251,0,0,0,0,0,
  255,251,0,0,0,0,0,255,251,0,0,0,0,0,255,251,0,0,0,0,
  0,255,251,0,0,0,0,0,0,0,23,190,255,218,64,0,0,3,223,255,
  255,255,251,32,0,46,255,255,255,255,255,210,0,159,255,251,154,223,255,247,
  0,223,255,48,0,4,223,128,0,255,251,0,0,0,22,0,0,239,254,16,
  0,0,0,0,0,175,255,213,0,0,0,0,0,46,255,255,234,81,0,0,
  0,3,223,255,255,254,146,0,0,0,5,191,255,255,254,64,0,0,0,1,
  107,255,255,225,0,0,0,0,0,44,255,247,0,0,0,0,0,2,255,250,
  0,75,16,0,0,1,255,251,5,255,212,0,0,8,255,249,12,255,255,218,
  154,223,255,244,1,207,255,255,255,255,255,176,0,24,255,255,255,255,250,16,
  0,0,39,206,255,217,64,0,0,0,15,255,176,0,0,0,0,15,255,176,
  0,0,0,0,15,255,176,0,0,0,0,15,255,176,0,0,0,0,15,255,
  176,0,0,0,0,15,255,176,0,0,0,0,15,255,176,0,0,0,0,15,
  255,176,0,0,143,255,255,255,255,255,248,143,255,255,255,255,255,248,143,255,
  255,255,255,255,248,73,153,159,255,217,153,148,0,0,15,255,176,0,0,0,
  0,15,255,176,0,0,0,0,15,255,176,0,0,0,0,15,255,176,0,0,
  0,0,15,255,176,0,0,0,0,15,255,176,0,0,0,0,15,255,176,0,
  0,0,0,15,255,176,0,0,0,0,15,255,176,0,0,0,0,15,255,176,
  0,0,0,0,15,255,176,0,0,0,0,15,255,176,0,0,0,0,15,255,
  176,0,0,0,0,15,255,176,0,0,0,0,15,255,176,0,0,0,0,15,
  255,176,0,0,255,251,0,0,0,0,191,255,255,251,0,0,0,0,191,255,
  255,251,0,0,0,0,191,255,255,251,0,0,0,0,191,255,255,251,0,0,
  0,0,191,255,255,251,0,0,0,0,191,255,255,251,0,0,0,0,191,255,
  255,251,0,0,0,0,191,255,255,251,0,0,0,0,191,255,255,251,0,0,
  0,0,191,255,255,251,0,0,0,0,191,255,255,251,0,0,0,0,191,255,
  239,252,0,0,0,0,207,254,207,255,16,0,0,1,255,252,143,255,128,0,
  0,8,255,248,63,255,246,0,0,111,255,243,9,255,255,218,157,255,255,144,
  1,207,255,255,255,255,252,16,0,26,255,255,255,255,161,0,0,0,73,223,
  253,164,0,0,95,255,160,0,0,0,0,1,255,252,13,255,241,0,0,0,
  0,7,255,245,7,255,247,0,0,0,0,13,255,208,1,239,253,0,0,0,
  0,95,255,112,0,159,255,80,0,0,0,191,255,16,0,63,255,176,0,0,
  2,255,249,0,0,11,255,242,0,0,8,255,243,0,0,4,255,248,0,0,
  30,255,176,0,0,0,223,254,0,0,111,255,64,0,0,0,111,255,96,0,
  207,253,0,0,0,0,30,255,192,3,255,246,0,0,0,0,8,255,243,9,
  255,225,0,0,0,0,2,255,249,31,255,128,0,0,0,0,0,175,254,143,
  255,32,0,0,0,0,0,79,255,255,250,0,0,0,0,0,0,12,255,255,
  244,0,0,0,0,0,0,6,255,255,192,0,0,0,0,0,0,0,239,255,
  96,0,0,0,0,0,0,0,127,254,0,0,0,0,0,0,0,0,31,247,
  0,0,0,0,79,255,112,0,0,0,3,255,48,0,0,0,7,255,244,13,
  255,208,0,0,0,9,255,144,0,0,0,13,255,208,8,255,243,0,0,0,
  14,255,224,0,0,0,63,255,128,2,255,248,0,0,0,79,255,244,0,0,
  0,143,255,32,0,191,253,0,0,0,159,255,249,0,0,0,223,251,0,0,
  111,255,64,0,0,239,255,254,0,0,4,255,246,0,0,30,255,144,0,4,
  255,255,255,64,0,9,255,225,0,0,9,255,224,0,10,255,186,255,160,0,
  14,255,144,0,0,4,255,244,0,30,255,85,255,225,0,79,255,64,0,0,
  0,223,250,0,95,254,16,239,245,0,175,253,0,0,0,0,143,254,16,175,
  250,0,159,250,1,239,248,0,0,0,0,47,255,81,255,244,0,79,255,21,
  255,242,0,0,0,0,11,255,166,255,208,0,13,255,106,255,192,0,0,0,
  0,5,255,252,255,128,0,8,255,207,255,96,0,0,0,0,1,239,255,255,
  48,0,2,255,255,254,16,0,0,0,0,0,159,255,252,0,0,0,207,255,
  250,0,0,0,0,0,0,79,255,247,0,0,0,111,255,244,0,0,0,0,
  0,0,13,255,241,0,0,0,31,255,208,0,0,0,0,0,0,7,255,176,
  0,0,0,11,255,128,0,0,0,0,0,0,2,255,80,0,0,0,5,255,
  32,0,0,0,9,255,251,0,0,0,0,111,255,192,1,223,255,112,0,0,
  2,239,254,32,0,63,255,243,0,0,11,255,245,0,0,7,255,252,0,0,
  111,255,160,0,0,0,207,255,128,2,239,253,16,0,0,0,46,255,243,11,
  255,244,0,0,0,0,5,255,253,127,255,128,0,0,0,0,0,175,255,255,
  252,0,0,0,0,0,0,29,255,255,243,0,0,0,0,0,0,4,255,255,
  128,0,0,0,0,0,0,9,255,255,209,0,0,0,0,0,0,95,255,255,
  250,0,0,0,0,0,2,239,254,223,255,80,0,0,0,0,11,255,245,79,
  255,225,0,0,0,0,127,255,160,9,255,251,0,0,0,3,255,253,16,1,
  223,255,96,0,0,12,255,244,0,0,79,255,226,0,0,143,255,144,0,0,
  9,255,251,0,4,255,253,16,0,0,1,223,255,112,29,255,244,0,0,0,
  0,63,255,243,47,255,208,0,0,0,0,4,255,250,11,255,243,0,0,0,
  0,10,255,244,5,255,249,0,0,0,0,47,255,192,0,239,254,16,0,0,
  0,143,255,96,0,143,255,96,0,0,0,239,254,0,0,47,255,192,0,0,
  5,255,247,0,0,10,255,243,0,0,11,255,225,0,0,4,255,249,0,0,
  47,255,144,0,0,0,223,254,0,0,159,255,32,0,0,0,127,255,80,1,
  239,251,0,0,0,0,31,255,176,6,255,244,0,0,0,0,9,255,242,12,
  255,192,0,0,0,0,3,255,248,63,255,96,0,0,0,0,0,207,254,175,
  254,0,0,0,0,0,0,111,255,255,247,0,0,0,0,0,0,30,255,255,
  241,0,0,0,0,0,0,9,255,255,144,0,0,0,0,0,0,2,255,255,
  32,0,0,0,0,0,0,1,255,251,0,0,0,0,0,0,0,8,255,244,
  0,0,0,0,0,0,0,30,255,192,0,0,0,0,0,0,0,127,255,96,
  0,0,0,0,0,0,1,239,253,0,0,0,0,0,0,0,7,255,247,0,
  0,0,0,0,0,0,13,255,225,0,0,0,0,0,0,0,111,255,128,0,
  0,0,0,0,0,0,223,255,32,0,0,0,0,0,0,5,255,250,0,0,
  0,0,0,0,15,255,255,255,255,255,255,255,15,255,255,255,255,255,255,255,
  15,255,255,255,255,255,255,251,9,153,153,153,153,255,255,225,0,0,0,0,
  8,255,255,64,0,0,0,0,63,255,248,0,0,0,0,1,223,255,192,0,
  0,0,0,10,255,254,32,0,0,0,0,95,255,245,0,0,0,0,2,239,
  255,144,0,0,0,0,11,255,252,0,0,0,0,0,127,255,226,0,0,0,
  0,3,255,255,80,0,0,0,0,29,255,249,0,0,0,0,0,159,255,209,
  0,0,0,0,5,255,255,48,0,0,0,0,46,255,253,153,153,153,153,144,
  191,255,255,255,255,255,255,240,255,255,255,255,255,255,255,240,255,255,255,255,
  255,255,255,240,0,0,23,206,255,128,0,2,223,255,255,128,0,11,255,255,
  238,112,0,63,255,113,0,0,0,111,250,0,0,0,0,143,247,0,0,0,
  0,143,247,0,0,0,0,111,248,0,0,0,0,95,249,0,0,0,0,63,
  251,0,0,0,0,47,252,0,0,0,0,31,253,0,0,0,0,15,254,0,
  0,0,0,31,253,0,0,0,1,143,249,0,0,0,255,255,210,0,0,0,
  255,255,64,0,0,0,221,255,227,0,0,0,0,111,250,0,0,0,0,15,
  253,0,0,0,0,15,254,0,0,0,0,31,253,0,0,0,0,47,252,0,
  0,0,0,79,251,0,0,0,0,95,249,0,0,0,0,111,248,0,0,0,
  0,143,247,0,0,0,0,143,247,0,0,0,0,111,251,0,0,0,0,63,
  255,129,0,0,0,11,255,255,255,112,0,1,223,255,255,128,0,0,23,206,
  255,128,255,251,255,251,255,251,255,251,255,251,255,251,255,251,255,251,255,251,
  255,251,255,251,255,251,255,251,255,251,255,251,255,251,255,251,255,251,255,251,
  255,251,255,251,255,251,255,251,255,251,255,251,255,251,255,251,255,251,255,251,
  255,251,255,251,255,251,255,251,255,251,255,251,255,251,255,251,255,253,163,0,
  0,0,255,255,255,112,0,0,238,255,255,244,0,0,0,5,239,250,0,0,
  0,0,127,254,0,0,0,0,79,255,0,0,0,0,79,255,0,0,0,0,
  95,254,0,0,0,0,111,252,0,0,0,0,143,251,0,0,0,0,159,250,
  0,0,0,0,175,248,0,0,0,0,191,247,0,0,0,0,159,248,0,0,
  0,0,111,254,48,0,0,0,12,255,255,128,0,0,2,239,255,128,0,0,
  29,255,237,96,0,0,127,252,16,0,0,0,175,247,0,0,0,0,191,247,
  0,0,0,0,175,248,0,0,0,0,159,250,0,0,0,0,127,251,0,0,
  0,0,111,253,0,0,0,0,95,254,0,0,0,0,79,255,0,0,0,0,
  79,255,0,0,0,0,143,254,0,0,0,21,239,250,0,0,255,255,255,244,
  0,0,255,255,255,112,0,0,255,253,163,0,0,0,0,4,137,133,0,0,
  0,1,0,2,207,255,255,214,0,0,110,80,46,255,255,255,255,234,156,255,
  244,175,255,255,255,255,255,255,255,226,29,249,32,22,223,255,255,254,48,2,
  96,0,0,5,190,253,129,0,0,0,0,0,0,23,0,0,0,0,0,0,
  0,0,0,191,128,0,0,0,0,0,0,0,8,255,247,0,0,0,0,0,
  0,0,79,255,248,0,0,0,0,0,0,2,239,255,96,0,0,0,0,0,
  0,12,255,228,0,0,0,0,0,0,0,143,254,48,0,0,0,0,0,0,
  0,62,194,0,0,0,0,0,0,0,0,3,16,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  3,140,239,235,96,11,255,240,0,1,159,255,255,255,253,59,255,240,0,28,
  255,255,255,255,255,253,255,240,0,207,255,255,185,172,255,255,255,240,8,255,
  255,129,0,0,42,255,255,240,30,255,246,0,0,0,0,143,255,240,127,255,
  160,0,0,0,0,13,255,240,191,255,32,0,0,0,0,7,255,240,239,253,
  0,0,0,0,0,4,255,240,255,251,0,0,0,0,0,2,255,240,255,251,
  0,0,0,0,0,3,255,240,239,253,0,0,0,0,0,5,255,240,191,255,
  32,0,0,0,0,8,255,240,127,255,160,0,0,0,0,13,255,240,31,255,
  246,0,0,0,0,159,255,240,8,255,255,145,0,0,42,255,255,240,1,207,
  255,255,185,156,255,255,255,240,0,44,255,255,255,255,255,253,255,240,0,1,
  159,255,255,255,253,75,255,240,0,0,3,156,239,235,97,11,255,240,0,0,
  0,0,0,84,0,0,0,0,0,0,0,0,2,239,64,0,0,0,0,0,
  0,0,12,255,227,0,0,0,0,0,0,0,159,255,229,0,0,0,0,0,
  0,6,255,254,48,0,0,0,0,0,0,63,255,194,0,0,0,0,0,0,
  0,223,251,16,0,0,0,0,0,0,0,127,144,0,0,0,0,0,0,0,
  0,4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,1,107,239,253,165,0,0,0,0,0,
  110,255,255,255,255,195,0,0,0,10,255,255,255,255,255,254,64,0,0,175,
  255,254,185,172,255,255,226,0,6,255,255,113,0,0,60,255,251,0,30,255,
  244,0,0,0,1,223,255,48,111,255,128,0,0,0,0,95,255,144,191,255,
  16,0,0,0,0,14,255,192,239,255,255,255,255,255,255,255,255,224,255,255,
  255,255,255,255,255,255,255,240,255,255,255,255,255,255,255,255,255,224,223,255,
  153,153,153,153,153,153,153,128,191,255,32,0,0,0,0,0,0,0,111,255,
  144,0,0,0,0,0,0,0,30,255,246,0,0,0,0,38,0,0,6,255,
  255,145,0,0,6,239,144,0,0,175,255,255,201,155,239,255,247,0,0,10,
  255,255,255,255,255,255,177,0,0,0,110,255,255,255,255,232,0,0,0,0,
  1,106,223,254,183,16,0,0,0,0,0,16,0,0,0,8,177,0,0,0,
  95,251,16,0,3,239,255,160,0,29,255,252,32,0,191,255,161,0,8,255,
  248,0,0,30,255,80,0,0,3,195,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,15,255,176,0,0,15,255,176,0,0,15,
  255,176,0,0,15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,15,
  255,176,0,0,15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,15,
  255,176,0,0,15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,15,
  255,176,0,0,15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,15,
  255,176,0,0,15,255,176,0,0,0,0,0,0,53,0,0,0,0,0,0,
  0,0,1,223,80,0,0,0,0,0,0,0,11,255,245,0,0,0,0,0,
  0,0,143,255,246,0,0,0,0,0,0,5,255,254,64,0,0,0,0,0,
  0,46,255,210,0,0,0,0,0,0,0,223,252,16,0,0,0,0,0,0,
  0,127,160,0,0,0,0,0,0,0,0,4,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  1,123,239,253,165,0,0,0,0,0,110,255,255,255,255,212,0,0,0,10,
  255,255,255,255,255,255,112,0,0,175,255,255,185,172,255,255,246,0,6,255,
  255,129,0,0,59,255,255,48,30,255,246,0,0,0,0,175,255,176,111,255,
  144,0,0,0,0,29,255,242,191,255,32,0,0,0,0,7,255,247,239,253,
  0,0,0,0,0,2,255,249,255,251,0,0,0,0,0,0,255,251,255,251,
  0,0,0,0,0,1,255,251,223,253,0,0,0,0,0,3,255,249,175,255,
  48,0,0,0,0,7,255,246,111,255,160,0,0,0,0,29,255,242,30,255,
  246,0,0,0,0,175,255,160,6,255,255,145,0,0,59,255,254,32,0,175,
  255,255,185,172,255,255,246,0,0,10,255,255,255,255,255,255,96,0,0,0,
  110,255,255,255,255,212,0,0,0,0,1,123,239,253,165,0,0,0,0,0,
  0,0,1,0,0,0,0,0,0,0,108,16,0,0,0,0,0,3,255,176,
  0,0,0,0,0,29,255,249,0,0,0,0,0,159,255,193,0,0,0,0,
  5,255,251,16,0,0,0,0,46,255,160,0,0,0,0,0,143,248,0,0,
  0,0,0,0,11,112,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,255,251,0,0,0,0,
  191,255,255,251,0,0,0,0,191,255,255,251,0,0,0,0,191,255,255,251,
  0,0,0,0,191,255,255,251,0,0,0,0,191,255,255,251,0,0,0,0,
  191,255,255,251,0,0,0,0,191,255,255,251,0,0,0,0,191,255,255,251,
  0,0,0,0,191,255,255,251,0,0,0,0,191,255,255,251,0,0,0,0,
  191,255,255,251,0,0,0,0,191,255,239,252,0,0,0,0,207,254,207,255,
  16,0,0,1,255,252,143,255,128,0,0,8,255,248,63,255,246,0,0,111,
  255,243,9,255,255,218,157,255,255,144,1,207,255,255,255,255,252,16,0,26,
  255,255,255,255,161,0,0,0,73,223,253,164,0,0,0,44,252,32,0,44,
  252,32,0,207,255,192,0,207,255,192,0,255,255,240,0,255,255,240,0,207,
  255,192,0,207,255,192,0,44,252,32,0,44,252,32,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,255,251,
  0,0,0,0,191,255,255,251,0,0,0,0,191,255,255,251,0,0,0,0,
  191,255,255,251,0,0,0,0,191,255,255,251,0,0,0,0,191,255,255,251,
  0,0,0,0,191,255,255,251,0,0,0,0,191,255,255,251,0,0,0,0,
  191,255,255,251,0,0,0,0,191,255,255,251,0,0,0,0,191,255,255,251,
  0,0,0,0,191,255,255,251,0,0,0,0,191,255,239,252,0,0,0,0,
  207,254,207,255,16,0,0,1,255,252,143,255,128,0,0,8,255,248,63,255,
  246,0,0,111,255,243,9,255,255,218,157,255,255,144,1,207,255,255,255,255,
  252,16,0,26,255,255,255,255,161,0,0,0,73,223,253,164,0,0,0,0,
  1,16,0,0,0,0,0,0,42,255,251,48,0,5,32,0,3,239,255,255,
  250,68,143,210,0,13,255,255,255,255,255,255,248,0,4,237,66,57,255,255,
  255,144,0,0,49,0,0,58,239,198,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,255,
  251,0,90,239,235,96,0,0,255,251,27,255,255,255,252,32,0,255,252,223,
  255,255,255,255,209,0,255,255,255,235,154,239,255,251,0,255,255,250,16,0,
  25,255,255,64,255,255,176,0,0,0,175,255,160,255,255,32,0,0,0,47,
  255,208,255,253,0,0,0,0,13,255,240,255,251,0,0,0,0,11,255,240,
  255,251,0,0,0,0,11,255,240,255,251,0,0,0,0,11,255,240,255,251,
  0,0,0,0,11,255,240,255,251,0,0,0,0,11,255,240,255,251,0,0,
  0,0,11,255,240,255,251,0,0,0,0,11,255,240,255,251,0,0,0,0,
  11,255,240,255,251,0,0,0,0,11,255,240,255,251,0,0,0,0,11,255,
  240,255,251,0,0,0,0,11,255,240,255,251,0,0,0,0,11,255,240,0,
  0,0,0,0,0,1,214,0,0,0,0,0,0,0,0,0,0,0,0,11,
  255,96,0,0,0,0,0,0,0,0,0,0,0,143,255,245,0,0,0,0,
  0,0,0,0,0,0,5,255,255,144,0,0,0,0,0,0,0,0,0,0,
  62,255,246,0,0,0,0,0,0,0,0,0,0,1,223,254,64,0,0,0,
  0,0,0,0,0,0,0,7,255,194,0,0,0,0,0,0,0,0,0,0,
  0,0,154,16,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,255,
  242,0,0,0,0,0,0,0,0,0,0,0,7,255,248,0,0,0,0,0,
  0,0,0,0,0,0,13,255,253,0,0,0,0,0,0,0,0,0,0,0,
  79,255,255,80,0,0,0,0,0,0,0,0,0,0,175,255,255,176,0,0,
  0,0,0,0,0,0,0,1,255,255,255,242,0,0,0,0,0,0,0,0,
  0,7,255,252,255,247,0,0,0,0,0,0,0,0,0,13,255,226,255,253,
  0,0,0,0,0,0,0,0,0,79,255,160,175,255,64,0,0,0,0,0,
  0,0,0,175,255,64,79,255,160,0,0,0,0,0,0,0,1,255,253,0,
  13,255,241,0,0,0,0,0,0,0,7,255,247,0,7,255,247,0,0,0,
  0,0,0,0,13,255,241,0,2,255,253,0,0,0,0,0,0,0,79,255,
  160,0,0,191,255,64,0,0,0,0,0,0,175,255,64,0,0,95,255,160,
  0,0,0,0,0,1,255,253,0,0,0,14,255,241,0,0,0,0,0,7,
  255,247,0,0,0,8,255,247,0,0,0,0,0,13,255,242,0,0,0,2,
  255,253,0,0,0,0,0,63,255,176,0,0,0,0,191,255,64,0,0,0,
  0,159,255,255,255,255,255,255,255,255,160,0,0,0,1,239,255,255,255,255,
  255,255,255,255,241,0,0,0,6,255,255,255,255,255,255,255,255,255,247,0,
  0,0,12,255,249,153,153,153,153,153,154,255,252,0,0,0,63,255,176,0,
  0,0,0,0,0,207,255,48,0,0,159,255,80,0,0,0,0,0,0,111,
  255,144,0,1,239,254,0,0,0,0,0,0,0,31,255,225,0,6,255,249,
  0,0,0,0,0,0,0,10,255,246,0,12,255,243,0,0,0,0,0,0,
  0,4,255,252,0,63,255,192,0,0,0,0,0,0,0,0,223,255,48,0,
  0,0,0,3,227,0,0,0,0,0,0,0,0,29,254,48,0,0,0,0,
  0,0,0,191,255,225,0,0,0,0,0,0,7,255,254,80,0,0,0,0,
  0,0,79,255,211,0,0,0,0,0,0,2,239,252,32,0,0,0,0,0,
  0,8,255,161,0,0,0,0,0,0,0,0,168,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,255,255,255,255,255,255,255,255,255,128,255,
  255,255,255,255,255,255,255,255,128,255,255,255,255,255,255,255,255,255,128,255,
  253,153,153,153,153,153,153,153,64,255,251,0,0,0,0,0,0,0,0,255,
  251,0,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,255,
  251,0,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,255,
  251,0,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,255,
  251,0,0,0,0,0,0,0,0,255,253,153,153,153,153,153,153,144,0,255,
  255,255,255,255,255,255,255,240,0,255,255,255,255,255,255,255,255,240,0,255,
  255,255,255,255,255,255,255,240,0,255,251,0,0,0,0,0,0,0,0,255,
  251,0,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,255,
  251,0,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,255,
  251,0,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,255,
  251,0,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,255,
  253,153,153,153,153,153,153,153,64,255,255,255,255,255,255,255,255,255,128,255,
  255,255,255,255,255,255,255,255,128,255,255,255,255,255,255,255,255,255,128,0,
  0,8,177,0,0,0,95,251,0,0,2,239,255,144,0,28,255,252,32,0,
  175,255,161,0,6,255,248,0,0,13,255,96,0,0,2,212,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,15,255,176,0,0,
  15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,
  15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,
  15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,
  15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,
  15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,
  15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,
  15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,
  0,0,0,0,0,0,39,0,0,0,0,0,0,0,0,0,0,0,0,1,
  223,128,0,0,0,0,0,0,0,0,0,0,0,10,255,247,0,0,0,0,
  0,0,0,0,0,0,0,127,255,248,0,0,0,0,0,0,0,0,0,0,
  4,255,255,96,0,0,0,0,0,0,0,0,0,0,46,255,228,0,0,0,
  0,0,0,0,0,0,0,0,207,252,32,0,0,0,0,0,0,0,0,0,
  0,0,111,161,0,0,0,0,0,0,0,0,0,0,0,0,4,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,4,156,239,
  254,200,64,0,0,0,0,0,0,0,7,223,255,255,255,255,253,96,0,0,
  0,0,0,3,207,255,255,255,255,255,255,252,32,0,0,0,0,78,255,255,
  253,169,155,223,255,255,228,0,0,0,4,255,255,232,32,0,0,3,159,255,
  255,64,0,0,46,255,252,32,0,0,0,0,2,207,255,226,0,0,191,255,
  176,0,0,0,0,0,0,28,255,251,0,5,255,253,16,0,0,0,0,0,
  0,1,223,255,80,12,255,243,0,0,0,0,0,0,0,0,79,255,192,63,
  255,176,0,0,0,0,0,0,0,0,11,255,243,143,255,80,0,0,0,0,
  0,0,0,0,5,255,247,191,255,16,0,0,0,0,0,0,0,0,1,255,
  251,223,253,0,0,0,0,0,0,0,0,0,0,223,253,255,251,0,0,0,
  0,0,0,0,0,0,0,207,254,255,251,0,0,0,0,0,0,0,0,0,
  0,191,255,239,252,0,0,0,0,0,0,0,0,0,0,191,254,223,253,0,
  0,0,0,0,0,0,0,0,0,223,253,191,255,16,0,0,0,0,0,0,
  0,0,1,255,251,127,255,80,0,0,0,0,0,0,0,0,5,255,247,47,
  255,176,0,0,0,0,0,0,0,0,11,255,243,11,255,244,0,0,0,0,
  0,0,0,0,79,255,192,4,255,253,16,0,0,0,0,0,0,1,223,255,
  64,0,191,255,177,0,0,0,0,0,0,28,255,251,0,0,30,255,252,32,
  0,0,0,0,2,207,255,226,0,0,3,239,255,249,32,0,0,2,159,255,
  254,48,0,0,0,62,255,255,253,169,154,223,255,255,228,0,0,0,0,2,
  207,255,255,255,255,255,255,252,32,0,0,0,0,0,6,223,255,255,255,255,
  253,96,0,0,0,0,0,0,0,4,140,239,254,201,64,0,0,0,0,0,
  0,0,0,0,8,193,0,0,0,0,0,0,0,0,0,95,252,16,0,0,
  0,0,0,0,0,3,239,255,176,0,0,0,0,0,0,0,29,255,253,32,
  0,0,0,0,0,0,0,191,255,177,0,0,0,0,0,0,0,8,255,248,
  0,0,0,0,0,0,0,0,30,255,96,0,0,0,0,0,0,0,0,3,
  211,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  255,251,0,0,0,0,0,0,0,255,251,255,251,0,0,0,0,0,0,0,
  255,251,255,251,0,0,0,0,0,0,0,255,251,255,251,0,0,0,0,0,
  0,0,255,251,255,251,0,0,0,0,0,0,0,255,251,255,251,0,0,0,
  0,0,0,0,255,251,255,251,0,0,0,0,0,0,0,255,251,255,251,0,
  0,0,0,0,0,0,255,251,255,251,0,0,0,0,0,0,0,255,251,255,
  251,0,0,0,0,0,0,0,255,251,255,251,0,0,0,0,0,0,0,255,
  251,255,251,0,0,0,0,0,0,0,255,251,255,251,0,0,0,0,0,0,
  0,255,251,255,251,0,0,0,0,0,0,0,255,251,255,251,0,0,0,0,
  0,0,0,255,251,255,251,0,0,0,0,0,0,0,255,251,255,251,0,0,
  0,0,0,0,0,255,251,255,251,0,0,0,0,0,0,0,255,251,255,251,
  0,0,0,0,0,0,0,255,251,239,253,0,0,0,0,0,0,2,255,250,
  191,255,16,0,0,0,0,0,5,255,247,127,255,112,0,0,0,0,0,11,
  255,243,47,255,226,0,0,0,0,0,79,255,208,10,255,252,16,0,0,0,
  3,239,255,96,2,239,255,230,0,0,1,127,255,251,0,0,79,255,255,235,
  154,191,255,255,210,0,0,4,239,255,255,255,255,255,252,32,0,0,0,42,
  255,255,255,255,255,129,0,0,0,0,0,56,206,255,219,113,0,0,0,0,
  0,44,252,32,0,44,252,32,0,0,0,0,207,255,192,0,207,255,192,0,
  0,0,0,255,255,240,0,255,255,240,0,0,0,0,207,255,192,0,207,255,
  192,0,0,0,0,44,252,32,0,44,252,32,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,255,251,255,251,
  0,0,0,0,0,0,0,255,251,255,251,0,0,0,0,0,0,0,255,251,
  255,251,0,0,0,0,0,0,0,255,251,255,251,0,0,0,0,0,0,0,
  255,251,255,251,0,0,0,0,0,0,0,255,251,255,251,0,0,0,0,0,
  0,0,255,251,255,251,0,0,0,0,0,0,0,255,251,255,251,0,0,0,
  0,0,0,0,255,251,255,251,0,0,0,0,0,0,0,255,251,255,251,0,
  0,0,0,0,0,0,255,251,255,251,0,0,0,0,0,0,0,255,251,255,
  251,0,0,0,0,0,0,0,255,251,255,251,0,0,0,0,0,0,0,255,
  251,255,251,0,0,0,0,0,0,0,255,251,255,251,0,0,0,0,0,0,
  0,255,251,255,251,0,0,0,0,0,0,0,255,251,255,251,0,0,0,0,
  0,0,0,255,251,255,251,0,0,0,0,0,0,0,255,251,239,253,0,0,
  0,0,0,0,2,255,250,191,255,16,0,0,0,0,0,5,255,247,127,255,
  112,0,0,0,0,0,11,255,243,47,255,226,0,0,0,0,0,79,255,208,
  10,255,252,16,0,0,0,3,239,255,96,2,239,255,230,0,0,1,127,255,
  251,0,0,79,255,255,235,154,191,255,255,210,0,0,4,239,255,255,255,255,
  255,252,32,0,0,0,42,255,255,255,255,255,129,0,0,0,0,0,56,206,
  255,219,113,0,0,0,0,0,0,1,16,0,0,0,0,0,0,0,0,5,
  223,254,129,0,0,112,0,0,0,0,143,255,255,254,115,75,250,0,0,0,
  5,255,255,255,255,255,255,254,32,0,0,0,175,131,38,223,255,255,228,0,
  0,0,0,4,0,0,7,223,234,32,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,255,225,0,0,0,0,0,0,0,255,251,255,251,0,0,
  0,0,0,0,0,255,251,255,255,96,0,0,0,0,0,0,255,251,255,255,
  243,0,0,0,0,0,0,255,251,255,255,253,16,0,0,0,0,0,255,251,
  255,255,255,144,0,0,0,0,0,255,251,255,255,255,245,0,0,0,0,0,
  255,251,255,253,255,254,32,0,0,0,0,255,251,255,251,127,255,192,0,0,
  0,0,255,251,255,251,11,255,248,0,0,0,0,255,251,255,251,1,239,255,
  64,0,0,0,255,251,255,251,0,79,255,225,0,0,0,255,251,255,251,0,
  8,255,251,0,0,0,255,251,255,251,0,0,207,255,112,0,0,255,251,255,
  251,0,0,46,255,243,0,0,255,251,255,251,0,0,5,255,253,16,0,255,
  251,255,251,0,0,0,175,255,160,0,255,251,255,251,0,0,0,29,255,245,
  0,255,251,255,251,0,0,0,3,255,254,32,255,251,255,251,0,0,0,0,
  127,255,192,255,251,255,251,0,0,0,0,11,255,248,255,251,255,251,0,0,
  0,0,1,239,255,255,251,255,251,0,0,0,0,0,79,255,255,251,255,251,
  0,0,0,0,0,8,255,255,251,255,251,0,0,0,0,0,0,207,255,251,
  255,251,0,0,0,0,0,0,46,255,251,255,251,0,0,0,0,0,0,5,
  255,251,255,251,0,0,0,0,0,0,0,175,251,255,251,0,0,0,0,0,
  0,0,31,251,0,0,0,1,48,0,0,0,0,0,0,0,0,11,209,0,
  0,0,0,0,0,0,0,191,251,0,0,0,0,0,0,0,4,255,255,112,
  0,0,0,0,0,0,0,94,255,244,0,0,0,0,0,0,0,3,239,254,
  32,0,0,0,0,0,0,0,45,255,176,0,0,0,0,0,0,0,1,191,
  193,0,0,0,0,0,0,0,0,8,16,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,3,140,239,235,
  96,11,255,240,0,1,159,255,255,255,253,59,255,240,0,28,255,255,255,255,
  255,253,255,240,0,207,255,255,185,172,255,255,255,240,8,255,255,129,0,0,
  42,255,255,240,30,255,246,0,0,0,0,143,255,240,127,255,160,0,0,0,
  0,13,255,240,191,255,32,0,0,0,0,7,255,240,239,253,0,0,0,0,
  0,4,255,240,255,251,0,0,0,0,0,2,255,240,255,251,0,0,0,0,
  0,3,255,240,239,253,0,0,0,0,0,5,255,240,191,255,32,0,0,0,
  0,8,255,240,127,255,160,0,0,0,0,13,255,240,31,255,246,0,0,0,
  0,159,255,240,8,255,255,145,0,0,42,255,255,240,1,207,255,255,185,156,
  255,255,255,240,0,44,255,255,255,255,255,253,255,240,0,1,159,255,255,255,
  253,75,255,240,0,0,3,156,239,235,97,11,255,240,0,0,0,2,16,0,
  0,0,0,0,0,0,0,62,160,0,0,0,0,0,0,0,2,239,247,0,
  0,0,0,0,0,0,9,255,255,48,0,0,0,0,0,0,0,159,255,209,
  0,0,0,0,0,0,0,7,255,251,0,0,0,0,0,0,0,0,94,255,
  128,0,0,0,0,0,0,0,3,239,144,0,0,0,0,0,0,0,0,40,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,1,107,239,253,165,0,0,0,0,0,110,255,255,255,
  255,195,0,0,0,10,255,255,255,255,255,254,64,0,0,175,255,254,185,172,
  255,255,226,0,6,255,255,113,0,0,60,255,251,0,30,255,244,0,0,0,
  1,223,255,48,111,255,128,0,0,0,0,95,255,144,191,255,16,0,0,0,
  0,14,255,192,239,255,255,255,255,255,255,255,255,224,255,255,255,255,255,255,
  255,255,255,240,255,255,255,255,255,255,255,255,255,224,223,255,153,153,153,153,
  153,153,153,128,191,255,32,0,0,0,0,0,0,0,111,255,144,0,0,0,
  0,0,0,0,30,255,246,0,0,0,0,38,0,0,6,255,255,145,0,0,
  6,239,144,0,0,175,255,255,201,155,239,255,247,0,0,10,255,255,255,255,
  255,255,177,0,0,0,110,255,255,255,255,232,0,0,0,0,1,106,223,254,
  183,16,0,0,0,131,0,0,0,8,253,16,0,0,143,255,192,0,0,111,
  255,249,0,0,3,239,255,96,0,0,44,255,244,0,0,1,175,254,0,0,
  0,8,246,0,0,0,0,32,0,0,0,0,0,0,0,0,0,0,0,0,
  15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,
  15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,
  15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,
  15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,
  15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,
  0,0,2,16,0,0,0,0,0,0,0,0,46,160,0,0,0,0,0,0,
  0,2,223,247,0,0,0,0,0,0,0,9,255,255,64,0,0,0,0,0,
  0,0,159,255,226,0,0,0,0,0,0,0,6,255,252,0,0,0,0,0,
  0,0,0,78,255,144,0,0,0,0,0,0,0,3,223,160,0,0,0,0,
  0,0,0,0,40,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,1,123,239,253,165,0,0,0,0,
  0,110,255,255,255,255,212,0,0,0,10,255,255,255,255,255,255,112,0,0,
  175,255,255,185,172,255,255,246,0,6,255,255,129,0,0,59,255,255,48,30,
  255,246,0,0,0,0,175,255,176,111,255,144,0,0,0,0,29,255,242,191,
  255,32,0,0,0,0,7,255,247,239,253,0,0,0,0,0,2,255,249,255,
  251,0,0,0,0,0,0,255,251,255,251,0,0,0,0,0,1,255,251,223,
  253,0,0,0,0,0,3,255,249,175,255,48,0,0,0,0,7,255,246,111,
  255,160,0,0,0,0,29,255,242,30,255,246,0,0,0,0,175,255,160,6,
  255,255,145,0,0,59,255,254,32,0,175,255,255,185,172,255,255,246,0,0,
  10,255,255,255,255,255,255,96,0,0,0,110,255,255,255,255,212,0,0,0,
  0,1,123,239,253,165,0,0,0,0,0,4,112,0,0,0,0,0,0,62,
  244,0,0,0,0,0,3,239,253,16,0,0,0,0,2,207,255,176,0,0,
  0,0,0,27,255,247,0,0,0,0,0,0,175,255,48,0,0,0,0,0,
  8,255,208,0,0,0,0,0,0,127,80,0,0,0,0,0,0,2,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,255,251,0,
  0,0,0,191,255,255,251,0,0,0,0,191,255,255,251,0,0,0,0,191,
  255,255,251,0,0,0,0,191,255,255,251,0,0,0,0,191,255,255,251,0,
  0,0,0,191,255,255,251,0,0,0,0,191,255,255,251,0,0,0,0,191,
  255,255,251,0,0,0,0,191,255,255,251,0,0,0,0,191,255,255,251,0,
  0,0,0,191,255,255,251,0,0,0,0,191,255,239,252,0,0,0,0,207,
  254,207,255,16,0,0,1,255,252,143,255,128,0,0,8,255,248,63,255,246,
  0,0,111,255,243,9,255,255,218,157,255,255,144,1,207,255,255,255,255,252,
  16,0,26,255,255,255,255,161,0,0,0,73,223,253,164,0,0,0,0,0,
  0,175,253,16,0,0,0,0,0,0,7,255,255,176,0,0,0,0,0,0,
  79,255,255,249,0,0,0,0,0,2,239,254,223,255,112,0,0,0,0,29,
  255,227,28,255,245,0,0,0,0,175,253,32,1,207,255,64,0,0,5,255,
  194,0,0,27,255,208,0,0,0,172,16,0,0,0,174,48,0,0,0,1,
  0,0,0,0,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,3,140,239,235,96,11,255,240,0,1,159,
  255,255,255,253,59,255,240,0,28,255,255,255,255,255,253,255,240,0,207,255,
  255,185,172,255,255,255,240,8,255,255,129,0,0,42,255,255,240,30,255,246,
  0,0,0,0,143,255,240,127,255,160,0,0,0,0,13,255,240,191,255,32,
  0,0,0,0,7,255,240,239,253,0,0,0,0,0,4,255,240,255,251,0,
  0,0,0,0,2,255,240,255,251,0,0,0,0,0,3,255,240,239,253,0,
  0,0,0,0,5,255,240,191,255,32,0,0,0,0,8,255,240,127,255,160,
  0,0,0,0,13,255,240,31,255,246,0,0,0,0,159,255,240,8,255,255,
  145,0,0,42,255,255,240,1,207,255,255,185,156,255,255,255,240,0,44,255,
  255,255,255,255,253,255,240,0,1,159,255,255,255,253,75,255,240,0,0,3,
  156,239,235,97,11,255,240,0,0,0,1,223,249,0,0,0,0,0,0,0,
  12,255,255,112,0,0,0,0,0,0,159,255,255,244,0,0,0,0,0,6,
  255,253,255,254,32,0,0,0,0,79,255,177,78,255,209,0,0,0,2,239,
  250,0,3,239,251,0,0,0,11,255,144,0,0,45,255,112,0,0,2,215,
  0,0,0,2,219,16,0,0,0,16,0,0,0,0,17,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,
  107,239,253,165,0,0,0,0,0,110,255,255,255,255,195,0,0,0,10,255,
  255,255,255,255,254,64,0,0,175,255,254,185,172,255,255,226,0,6,255,255,
  113,0,0,60,255,251,0,30,255,244,0,0,0,1,223,255,48,111,255,128,
  0,0,0,0,95,255,144,191,255,16,0,0,0,0,14,255,192,239,255,255,
  255,255,255,255,255,255,224,255,255,255,255,255,255,255,255,255,240,255,255,255,
  255,255,255,255,255,255,224,223,255,153,153,153,153,153,153,153,128,191,255,32,
  0,0,0,0,0,0,0,111,255,144,0,0,0,0,0,0,0,30,255,246,
  0,0,0,0,38,0,0,6,255,255,145,0,0,6,239,144,0,0,175,255,
  255,201,155,239,255,247,0,0,10,255,255,255,255,255,255,177,0,0,0,110,
  255,255,255,255,232,0,0,0,0,1,106,223,254,183,16,0,0,0,0,0,
  104,132,0,0,0,0,0,7,255,255,64,0,0,0,0,95,255,255,226,0,
  0,0,4,255,255,255,253,16,0,0,62,255,229,143,255,193,0,2,223,253,
  48,5,239,251,0,12,255,177,0,0,45,255,128,4,233,0,0,0,1,172,
  16,0,32,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,255,251,0,0,
  0,0,0,0,255,251,0,0,0,0,0,0,255,251,0,0,0,0,0,0,
  255,251,0,0,0,0,0,0,255,251,0,0,0,0,0,0,255,251,0,0,
  0,0,0,0,255,251,0,0,0,0,0,0,255,251,0,0,0,0,0,0,
  255,251,0,0,0,0,0,0,255,251,0,0,0,0,0,0,255,251,0,0,
  0,0,0,0,255,251,0,0,0,0,0,0,255,251,0,0,0,0,0,0,
  255,251,0,0,0,0,0,0,255,251,0,0,0,0,0,0,255,251,0,0,
  0,0,0,0,255,251,0,0,0,0,0,0,255,251,0,0,0,0,0,0,
  255,251,0,0,0,0,0,0,1,223,250,0,0,0,0,0,0,0,11,255,
  255,112,0,0,0,0,0,0,159,255,255,244,0,0,0,0,0,6,255,253,
  239,254,48,0,0,0,0,79,255,193,62,255,209,0,0,0,2,239,251,16,
  3,223,251,0,0,0,10,255,144,0,0,45,255,96,0,0,2,216,0,0,
  0,1,204,16,0,0,0,16,0,0,0,0,17,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,123,239,
  253,165,0,0,0,0,0,110,255,255,255,255,212,0,0,0,10,255,255,255,
  255,255,255,112,0,0,175,255,255,185,172,255,255,246,0,6,255,255,129,0,
  0,59,255,255,48,30,255,246,0,0,0,0,175,255,176,111,255,144,0,0,
  0,0,29,255,242,191,255,32,0,0,0,0,7,255,247,239,253,0,0,0,
  0,0,2,255,249,255,251,0,0,0,0,0,0,255,251,255,251,0,0,0,
  0,0,1,255,251,223,253,0,0,0,0,0,3,255,249,175,255,48,0,0,
  0,0,7,255,246,111,255,160,0,0,0,0,29,255,242,30,255,246,0,0,
  0,0,175,255,160,6,255,255,145,0,0,59,255,254,32,0,175,255,255,185,
  172,255,255,246,0,0,10,255,255,255,255,255,255,96,0,0,0,110,255,255,
  255,255,212,0,0,0,0,1,123,239,253,165,0,0,0,0,0,0,191,251,
  0,0,0,0,0,8,255,255,144,0,0,0,0,111,255,255,246,0,0,0,
  4,255,253,223,255,64,0,0,46,255,194,28,255,226,0,1,223,251,16,1,
  191,253,16,6,255,160,0,0,10,255,96,0,136,0,0,0,0,153,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,255,251,0,0,0,0,191,255,255,251,0,0,0,0,191,255,255,
  251,0,0,0,0,191,255,255,251,0,0,0,0,191,255,255,251,0,0,0,
  0,191,255,255,251,0,0,0,0,191,255,255,251,0,0,0,0,191,255,255,
  251,0,0,0,0,191,255,255,251,0,0,0,0,191,255,255,251,0,0,0,
  0,191,255,255,251,0,0,0,0,191,255,255,251,0,0,0,0,191,255,239,
  252,0,0,0,0,207,254,207,255,16,0,0,1,255,252,143,255,128,0,0,
  8,255,248,63,255,246,0,0,111,255,243,9,255,255,218,157,255,255,144,1,
  207,255,255,255,255,252,16,0,26,255,255,255,255,161,0,0,0,73,223,253,
  164,0,0,0,0,0,1,16,0,0,0,0,0,0,0,42,255,252,64,0,
  3,64,0,0,3,239,255,255,251,84,126,227,0,0,30,255,255,255,255,255,
  255,247,0,0,5,251,66,74,255,255,255,128,0,0,0,64,0,0,75,239,
  197,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,3,140,239,235,96,
  11,255,240,0,1,159,255,255,255,253,59,255,240,0,28,255,255,255,255,255,
  253,255,240,0,207,255,255,185,172,255,255,255,240,8,255,255,129,0,0,42,
  255,255,240,30,255,246,0,0,0,0,143,255,240,127,255,160,0,0,0,0,
  13,255,240,191,255,32,0,0,0,0,7,255,240,239,253,0,0,0,0,0,
  4,255,240,255,251,0,0,0,0,0,2,255,240,255,251,0,0,0,0,0,
  3,255,240,239,253,0,0,0,0,0,5,255,240,191,255,32,0,0,0,0,
  8,255,240,127,255,160,0,0,0,0,13,255,240,31,255,246,0,0,0,0,
  159,255,240,8,255,255,145,0,0,42,255,255,240,1,207,255,255,185,156,255,
  255,255,240,0,44,255,255,255,255,255,253,255,240,0,1,159,255,255,255,253,
  75,255,240,0,0,3,156,239,235,97,11,255,240,0,0,0,17,0,0,0,
  0,0,0,0,0,93,255,232,16,0,7,0,0,0,8,255,255,255,231,69,
  191,144,0,0,95,255,255,255,255,255,255,226,0,0,10,248,50,109,255,255,
  254,64,0,0,0,80,0,0,125,254,162,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,1,123,239,253,165,0,0,0,0,0,110,255,255,255,255,
  212,0,0,0,10,255,255,255,255,255,255,112,0,0,175,255,255,185,172,255,
  255,246,0,6,255,255,129,0,0,59,255,255,48,30,255,246,0,0,0,0,
  175,255,176,111,255,144,0,0,0,0,29,255,242,191,255,32,0,0,0,0,
  7,255,247,239,253,0,0,0,0,0,2,255,249,255,251,0,0,0,0,0,
  0,255,251,255,251,0,0,0,0,0,1,255,251,223,253,0,0,0,0,0,
  3,255,249,175,255,48,0,0,0,0,7,255,246,111,255,160,0,0,0,0,
  29,255,242,30,255,246,0,0,0,0,175,255,160,6,255,255,145,0,0,59,
  255,254,32,0,175,255,255,185,172,255,255,246,0,0,10,255,255,255,255,255,
  255,96,0,0,0,110,255,255,255,255,212,0,0,0,0,1,123,239,253,165,
  0,0,0,0,0,1,107,223,253,182,16,0,0,0,110,255,255,255,255,230,
  0,0,10,255,255,255,255,255,255,144,0,175,255,255,201,155,239,255,243,6,
  255,255,145,0,0,23,255,64,30,255,246,0,0,0,0,52,0,111,255,160,
  0,0,0,0,0,0,191,255,32,0,0,0,0,0,0,239,253,0,0,0,
  0,0,0,0,255,251,0,0,0,0,0,0,0,255,251,0,0,0,0,0,
  0,0,223,253,0,0,0,0,0,0,0,191,255,48,0,0,0,0,0,0,
  111,255,160,0,0,0,0,0,0,30,255,247,0,0,0,0,52,0,6,255,
  255,146,0,0,23,239,64,0,175,255,255,201,155,239,255,243,0,10,255,255,
  255,255,255,255,160,0,0,110,255,255,255,255,230,0,0,0,1,107,255,253,
  182,16,0,0,0,0,5,255,128,0,0,0,0,0,0,10,255,233,32,0,
  0,0,0,0,14,255,255,227,0,0,0,0,0,3,52,175,252,0,0,0,
  0,0,0,0,31,255,0,0,0,0,28,130,1,143,253,0,0,0,0,191,
  255,255,255,244,0,0,0,0,41,223,255,251,48,0,0,0,0,0,1,34,
  0,0,0,0,0,0,0,0,4,140,239,254,218,97,0,0,0,0,0,0,
  6,223,255,255,255,255,255,146,0,0,0,0,2,207,255,255,255,255,255,255,
  254,96,0,0,0,78,255,255,253,185,154,223,255,255,250,0,0,4,255,255,
  249,48,0,0,1,126,255,254,32,0,46,255,252,32,0,0,0,0,1,175,
  227,0,0,191,255,177,0,0,0,0,0,0,7,48,0,5,255,253,16,0,
  0,0,0,0,0,0,0,0,12,255,244,0,0,0,0,0,0,0,0,0,
  0,63,255,176,0,0,0,0,0,0,0,0,0,0,127,255,80,0,0,0,
  0,0,0,0,0,0,0,191,255,16,0,0,0,0,0,0,0,0,0,0,
  223,253,0,0,0,0,0,0,0,0,0,0,0,255,252,0,0,0,0,0,
  0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,0,0,0,239,
  252,0,0,0,0,0,0,0,0,0,0,0,223,253,0,0,0,0,0,0,
  0,0,0,0,0,191,255,16,0,0,0,0,0,0,0,0,0,0,127,255,
  80,0,0,0,0,0,0,0,0,0,0,63,255,176,0,0,0,0,0,0,
  0,0,0,0,12,255,244,0,0,0,0,0,0,0,0,0,0,5,255,253,
  16,0,0,0,0,0,0,0,0,0,0,191,255,177,0,0,0,0,0,0,
  6,96,0,0,46,255,252,32,0,0,0,0,0,143,246,0,0,4,255,255,
  249,48,0,0,1,109,255,255,80,0,0,78,255,255,253,185,154,207,255,255,
  251,16,0,0,2,207,255,255,255,255,255,255,255,128,0,0,0,0,6,223,
  255,255,255,255,255,163,0,0,0,0,0,0,4,140,255,254,218,98,0,0,
  0,0,0,0,0,0,5,255,128,0,0,0,0,0,0,0,0,0,0,10,
  255,233,32,0,0,0,0,0,0,0,0,0,14,255,255,227,0,0,0,0,
  0,0,0,0,0,3,52,175,252,0,0,0,0,0,0,0,0,0,0,0,
  31,255,0,0,0,0,0,0,0,0,28,114,1,143,253,0,0,0,0,0,
  0,0,0,191,255,255,255,244,0,0,0,0,0,0,0,0,41,239,255,250,
  48,0,0,0,0,0,0,0,0,0,1,34,0,0,0,0,0,0,0,0,
  2,207,194,0,0,0,0,0,12,255,252,0,0,0,0,0,15,255,255,0,
  0,0,0,0,12,255,252,0,0,0,0,0,2,207,194,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,1,255,240,0,0,0,0,0,
  2,255,240,0,0,0,0,0,3,255,240,0,0,0,0,0,92,255,240,0,
  0,0,0,44,255,255,240,0,0,0,2,223,255,255,240,0,0,0,11,255,
  252,65,0,0,0,0,79,255,176,0,0,0,0,0,175,255,32,0,0,0,
  0,0,223,253,0,0,0,0,0,0,255,251,0,0,0,0,0,0,255,252,
  0,0,0,0,2,0,223,255,16,0,0,0,63,96,175,255,112,0,0,1,
  223,248,95,255,246,0,0,60,255,249,11,255,255,218,156,255,255,209,2,223,
  255,255,255,255,254,48,0,43,255,255,255,255,178,0,0,0,90,239,253,164,
  0,0,44,252,32,207,255,192,255,255,240,207,255,192,44,252,32,0,0,0,
  0,0,0,0,0,0,0,0,0,15,255,176,15,255,176,15,255,176,15,255,
  176,15,255,176,15,255,176,15,255,176,15,255,176,15,255,176,15,255,176,15,
  255,176,15,255,176,15,255,176,15,255,176,15,255,176,15,255,176,15,255,176,
  15,255,176,15,255,176,0,6,206,236,96,0,1,207,255,255,252,16,12,255,
  255,255,255,176,95,255,113,23,255,245,191,248,0,0,143,251,239,242,0,0,
  47,254,255,240,0,0,15,255,239,242,0,0,47,254,191,248,0,0,143,251,
  111,255,96,6,255,246,12,255,255,255,255,192,2,207,255,255,252,32,0,23,
  207,252,113,0,44,252,32,207,255,192,255,255,240,207,255,192,44,252,32,
};
static int fontIdx(uint32_t cp){
  if(cp>=0x20 && cp<=0x7E) return cp-0x20;
  switch(cp){
    case 0xE1: return 95;
    case 0xE9: return 96;
    case 0xED: return 97;
    case 0xF3: return 98;
    case 0xFA: return 99;
    case 0xFC: return 100;
    case 0xF1: return 101;
    case 0xC1: return 102;
    case 0xC9: return 103;
    case 0xCD: return 104;
    case 0xD3: return 105;
    case 0xDA: return 106;
    case 0xDC: return 107;
    case 0xD1: return 108;
    case 0xE0: return 109;
    case 0xE8: return 110;
    case 0xEC: return 111;
    case 0xF2: return 112;
    case 0xF9: return 113;
    case 0xE2: return 114;
    case 0xEA: return 115;
    case 0xEE: return 116;
    case 0xF4: return 117;
    case 0xFB: return 118;
    case 0xE3: return 119;
    case 0xF5: return 120;
    case 0xE7: return 121;
    case 0xC7: return 122;
    case 0xBF: return 123;
    case 0xA1: return 124;
    case 0xB0: return 125;
    case 0xB7: return 126;
    default: return 0x3F-0x20;
  }
}
#define FONT_HPS 8    // px de alto por unidad de 'size' logico
#define FONT_CAPOFF 11 // alinea el tope de mayusculas/digitos con y (como el 5x7)
static inline uint8_t fgPix(const FGlyph* g, int x, int y){
  if(x < 0 || y < 0 || x >= g->w || y >= g->h) return 0;
  int bpr = (g->w + 1) >> 1;
  uint8_t b = FBM[g->off + (uint32_t)y * bpr + (x >> 1)];
  return (x & 1) ? (b & 0x0F) : (b >> 4);
}
static inline float fontSc(int size){ return (float)(size * FONT_HPS) / (float)FONT_LINEH; }
static uint32_t nextCP(const char** ps){
  const char* s = *ps; uint8_t b = (uint8_t)*s++; uint32_t cp;
  if(b < 0x80) cp = b;
  else if((b & 0xE0) == 0xC0){ uint8_t b1 = *s ? (uint8_t)*s++ : 0; cp = ((b & 0x1F) << 6) | (b1 & 0x3F); }
  else if((b & 0xF0) == 0xE0){ if(*s) s++; if(*s) s++; cp = 0x3F; }
  else cp = 0x3F;
  *ps = s; return cp;
}
// Dibuja un glifo escalado (bilineal desde el master 4bpp)
static void drawGlyphScaled(int px0, int py0, const FGlyph* g, float sc, uint16_t col, uint8_t alpha){
  if(g->w == 0) return;
  int tw = (int)(g->w * sc + 0.999f), th = (int)(g->h * sc + 0.999f);
  for(int ty = 0; ty < th; ty++){
    float fy = ty / sc; int y0 = (int)fy; float dyf = fy - y0;
    for(int tx = 0; tx < tw; tx++){
      float fx = tx / sc; int x0 = (int)fx; float dxf = fx - x0;
      float a00 = fgPix(g, x0, y0),     a10 = fgPix(g, x0 + 1, y0);
      float a01 = fgPix(g, x0, y0 + 1), a11 = fgPix(g, x0 + 1, y0 + 1);
      float top = a00 * (1 - dxf) + a10 * dxf;
      float bot = a01 * (1 - dxf) + a11 * dxf;
      float cov = (top * (1 - dyf) + bot * dyf) * 17.0f;   // 0..255
      if(cov > 4.0f){
        int a = (int)(cov * alpha / 255.0f); if(a > 255) a = 255;
        pxA(px0 + tx, py0 + ty, col, (uint8_t)a);
      }
    }
  }
}

static int textW(const char* s, int size){
  if(size <= 1){                 // texto minusculo: bitmap 5x7 nitido (6px monoespaciado)
    int n = 0;
    while(*s){ uint8_t b = (uint8_t)*s++;
      if(b >= 0x80){ if((b & 0xE0) == 0xC0){ if(*s) s++; } else if((b & 0xF0) == 0xE0){ if(*s) s++; if(*s) s++; } }
      n++;
    }
    return n > 0 ? n * 6 - 1 : 0;
  }
  float sc = fontSc(size), w = 0;
  while(*s){ uint32_t cp = nextCP(&s); w += FG[fontIdx(cp)].adv * sc; }
  return (int)(w + 0.5f);
}
// Texto con alpha (workhorse). REGLA: vectorial Outfit para tamano >=2
// (curvas suaves), bitmap 5x7 NITIDO para size 1 (evita el emborronado
// de encoger demasiado una vectorial fina).
static int drawTextA(int x, int y, const char* s, int size, uint16_t col, uint8_t alpha){
  if(size <= 1){
    while(*s){
      uint8_t b = (uint8_t)*s++; uint32_t cp;
      if(b < 0x80) cp = b;
      else if((b & 0xE0) == 0xC0){ uint8_t b1 = *s ? (uint8_t)*s++ : 0; cp = ((b & 0x1F) << 6) | (b1 & 0x3F); }
      else if((b & 0xF0) == 0xE0){ if(*s) s++; if(*s) s++; cp = 0x3F; }
      else cp = 0x3F;
      uint8_t base, acc; mapCP(cp, base, acc);
      drawGlyphSmooth(x, y, base, 1, col, alpha);     // 1:1 = nitido
      if(acc) drawAccent(x, y, 1, acc, col);
      x += 6;
    }
    return x;
  }
  float sc = fontSc(size), penx = x;
  while(*s){
    uint32_t cp = nextCP(&s);
    const FGlyph* g = &FG[fontIdx(cp)];
    drawGlyphScaled((int)(penx + g->bx * sc + 0.5f), y + (int)((g->topoff - FONT_CAPOFF) * sc + 0.5f), g, sc, col, alpha);
    penx += g->adv * sc;
  }
  return (int)(penx + 0.5f);
}
static int  drawText(int x, int y, const char* s, int size, uint16_t col){ return drawTextA(x, y, s, size, col, 255); }
static void drawTextC(int cx, int y, const char* s, int size, uint16_t col){ drawTextA(cx - textW(s, size) / 2, y, s, size, col, 255); }
static void drawTextCA(int cx, int y, const char* s, int size, uint16_t col, uint8_t a){ drawTextA(cx - textW(s, size) / 2, y, s, size, col, a); }
static void drawTextR(int rx, int y, const char* s, int size, uint16_t col){ drawTextA(rx - textW(s, size), y, s, size, col, 255); }

// ---------------- Triangulo relleno (baricentrico) ----------------
static void fillTriangle(int x0,int y0,int x1,int y1,int x2,int y2,uint16_t c){
  int minx = min(x0, min(x1, x2)), maxx = max(x0, max(x1, x2));
  int miny = min(y0, min(y1, y2)), maxy = max(y0, max(y1, y2));
  // En modo landscape (gLand) el lienzo LOGICO es 800x533, no 533x800: recortar
  // contra los limites correctos para no perder los triangulos con x logica >=480
  // (picos/naves/wave del juego Geo Dash). En portrait no cambia nada.
  int bcw = gLand ? SCR_H : SCR_W, bch = gLand ? SCR_W : SCR_H;
  if(minx < 0) minx = 0; if(miny < 0) miny = 0;
  if(maxx >= bcw) maxx = bcw - 1; if(maxy >= bch) maxy = bch - 1;
  for(int y = miny; y <= maxy; y++){
    for(int x = minx; x <= maxx; x++){
      long e0 = (long)(x - x0) * (y1 - y0) - (long)(y - y0) * (x1 - x0);
      long e1 = (long)(x - x1) * (y2 - y1) - (long)(y - y1) * (x2 - x1);
      long e2 = (long)(x - x2) * (y0 - y2) - (long)(y - y2) * (x0 - x2);
      bool neg = (e0 < 0) || (e1 < 0) || (e2 < 0);
      bool pos = (e0 > 0) || (e1 > 0) || (e2 > 0);
      if(!(neg && pos)) px(x, y, c);
    }
  }
}
static void fillQuad(int x0,int y0,int x1,int y1,int x2,int y2,int x3,int y3,uint16_t c){
  fillTriangle(x0,y0,x1,y1,x2,y2,c);
  fillTriangle(x0,y0,x2,y2,x3,y3,c);
}

// ---------------- Reloj vectorial (trazo grueso redondeado) -------
static float bigCharAdvance(char ch, int capH){
  if(ch == ':') return capH * 0.34f;
  return capH * 0.60f + capH * 0.12f;   // ancho + hueco
}
static void drawBigChar(char ch, int ox, int oy, int capH, int thick, uint16_t col){
  float DW = capH * 0.60f, DH = capH;
  float m = thick * 0.5f;
  float L = ox + m, R = ox + DW - m, T = oy + m, B = oy + DH - m;
  float MX = ox + DW * 0.5f, MY = oy + DH * 0.5f;
  int rad = thick / 2; if(rad < 1) rad = 1;
  float pr = 0.0174532925f;
  auto arc = [&](float cxx, float cyy, float rx, float ry, float a0, float a1){
    int steps = (int)(fabsf(a1 - a0) / 7.0f) + 2;
    float pxp = 0, pyp = 0; bool first = true;
    for(int i = 0; i <= steps; i++){
      float a = (a0 + (a1 - a0) * i / steps) * pr;
      float xx = cxx + rx * cosf(a), yy = cyy + ry * sinf(a);
      if(!first) strokeSegAA(pxp, pyp, xx, yy, (float)rad, col);
      pxp = xx; pyp = yy; first = false;
    }
  };
  auto seg = [&](float x0, float y0, float x1, float y1){ strokeSegAA(x0, y0, x1, y1, (float)rad, col); };
  float sx;
  switch(ch){
    case '0': arc(MX, MY, DW * 0.5f - m, DH * 0.5f - m, 0, 360); break;
    case '1':
      sx = MX + DW * 0.06f;
      seg(sx, T, sx, B);
      seg(MX - DW * 0.26f, T + DH * 0.16f, sx, T);
      seg(MX - DW * 0.30f, B, MX + DW * 0.34f, B);
      break;
    case '2': {
      arc(MX, T + DH * 0.24f, DW * 0.5f - m, DH * 0.24f, 180, 380);
      float ex = MX + (DW * 0.5f - m) * cosf(20 * pr);
      float ey = (T + DH * 0.24f) + (DH * 0.24f) * sinf(20 * pr);
      seg(ex, ey, L, B);
      seg(L, B, R, B);
    } break;
    case '3':
      // FIX: los angulos originales (200-430 / 290-520) hacian que ambos arcos
      // se pasaran ~50-70 grados mas alla del centro vertical del glifo, asi
      // que se superponian en una franja ancha del lado derecho -- eso era la
      // "raya horizontal que sobresale". Los angulos de abajo se resolvieron
      // para que el arco superior TERMINE exactamente donde el arco inferior
      // EMPIEZA (mismo punto, sin solape), verificado por simulacion en Python
      // (distancia entre los dos extremos = 0px). El resto del barrido (el
      // rizo de la izquierda en cada extremo) se dejo igual que el original.
      arc(MX - DW * 0.05f, T + DH * 0.27f, DW * 0.40f, DH * 0.27f, 200, 398);
      arc(MX - DW * 0.05f, B - DH * 0.27f, DW * 0.40f, DH * 0.27f, 322, 520);
      break;
    case '4':
      sx = MX + DW * 0.16f;
      seg(sx, T, L, T + DH * 0.64f);
      seg(L, T + DH * 0.64f, R, T + DH * 0.64f);
      seg(sx, T, sx, B);
      break;
    case '5':
      seg(R, T, L + DW * 0.04f, T);
      seg(L + DW * 0.04f, T, L + DW * 0.04f, T + DH * 0.40f);
      arc(MX - DW * 0.02f, B - DH * 0.28f, DW * 0.44f, DH * 0.28f, 190, 470);
      break;
    case '6':
      arc(MX, MY + DH * 0.02f, DW * 0.42f, DH * 0.44f, 300, 120);
      arc(MX, B - DH * 0.26f, DW * 0.42f, DH * 0.26f, 0, 360);
      break;
    case '7':
      seg(L, T, R, T);
      seg(R, T, MX - DW * 0.06f, B);
      break;
    case '8':
      arc(MX, T + DH * 0.25f, DW * 0.42f, DH * 0.25f, 0, 360);
      arc(MX, B - DH * 0.27f, DW * 0.46f, DH * 0.27f, 0, 360);
      break;
    case '9':
      arc(MX, T + DH * 0.26f, DW * 0.42f, DH * 0.26f, 0, 360);
      arc(MX, MY, DW * 0.42f, DH * 0.44f, 40, 200);
      break;
    case ':': {
      int dr = thick; if(dr < 2) dr = 2;
      fillCircleAA(ox + DW * 0.16f, T + DH * 0.32f, (float)dr, col);
      fillCircleAA(ox + DW * 0.16f, B - DH * 0.28f, (float)dr, col);
    } break;
  }
}
// Dibuja "H:MM" centrado horizontalmente en cx
static void drawBigClock(const char* s, int cx, int y, int capH, int thick, uint16_t col){
  float total = 0;
  for(const char* p = s; *p; p++) total += bigCharAdvance(*p, capH);
  float x = cx - total / 2;
  for(const char* p = s; *p; p++){
    drawBigChar(*p, (int)x, y, capH, thick, col);
    x += bigCharAdvance(*p, capH);
  }
}

// #############################################################
// ##  ICONOS VECTORIALES  (originales, basados en tus imagenes)
// #############################################################

// arco con trazo grueso (para wifi y detalles)
static void arcStroke(float cx, float cy, float r, float a0, float a1, int thick, uint16_t col){
  float pr = 0.0174532925f;
  int steps = (int)(fabsf(a1 - a0) / 8.0f) + 2;
  float pxp = 0, pyp = 0; bool first = true;
  int rad = thick / 2; if(rad < 1) rad = 1;
  for(int i = 0; i <= steps; i++){
    float a = (a0 + (a1 - a0) * i / steps) * pr;
    float xx = cx + r * cosf(a), yy = cy + r * sinf(a);
    if(!first) strokeSeg(pxp, pyp, xx, yy, rad, col);
    pxp = xx; pyp = yy; first = false;
  }
}

enum { IC_RELOJ, IC_GALERIA, IC_MULTIMEDIA, IC_ALMACEN, IC_MODOPC, IC_NOTAS,
       IC_EDU, IC_NAV, IC_CODE, IC_BIEN, IC_PAINT, IC_JUEGOS,
       IC_AJUSTES, IC_CALC, IC_CALEND, IC_CAMARA };

static void iconBase(int x, int y, int S, uint16_t bg, int rf100){
  int r = S * rf100 / 100;
  if(gIconStyle == 1){                     // estilo "Vidrio": fondo Liquid Glass (ver drawAppIcon)
    drawLiquidGlassPanel(x, y, S, S, r, bg);
  } else {                                 // estilo "Plano" (original)
    fillRoundRect(x, y, S, S, r, bg);
    // sutil brillo superior
    fillRoundRectA(x, y, S, S / 2, r, rgb565(255,255,255), 22);
  }
}

static void drawAppIcon(int id, int x, int y, int S){
  int cx = x + S / 2, cy = y + S / 2;
  int tk = S / 12; if(tk < 2) tk = 2;               // grosor de trazo generico
  uint16_t WHITE = rgb565(255,255,255);
  switch(id){
    case IC_RELOJ: {
      iconBase(x, y, S, rgb565(245,245,247), 22);
      fillRing(cx, cy, (int)(S * 0.36f), 2, rgb565(70,70,74));
      // ticks 12/3/6/9
      fillRect(cx - 1, y + (int)(S * 0.16f), 2, S / 12, rgb565(70,70,74));
      fillRect(cx - 1, y + S - (int)(S * 0.16f) - S / 12, 2, S / 12, rgb565(70,70,74));
      fillRect(x + (int)(S * 0.16f), cy - 1, S / 12, 2, rgb565(70,70,74));
      fillRect(x + S - (int)(S * 0.16f) - S / 12, cy - 1, S / 12, 2, rgb565(70,70,74));
      strokeSeg(cx, cy, cx - S * 0.14f, cy - S * 0.10f, tk / 2 + 1, rgb565(30,30,30)); // hora
      strokeSeg(cx, cy, cx + S * 0.12f, cy - S * 0.20f, tk / 2, rgb565(245,140,30));   // min
      fillCircle(cx, cy, tk / 2 + 1, rgb565(30,30,30));
    } break;
    case IC_GALERIA: {
      iconBase(x, y, S, WHITE, 22);
      uint16_t cols[8] = { rgb565(233,64,64), rgb565(240,150,40), rgb565(240,210,50),
                           rgb565(90,200,90), rgb565(50,190,190), rgb565(60,120,235),
                           rgb565(120,80,220), rgb565(220,80,200) };
      float d = S * 0.17f, pr = S * 0.135f;
      for(int k = 0; k < 8; k++){
        float a = k * 45 * 0.0174532925f;
        fillCircle((int)(cx + d * cosf(a)), (int)(cy + d * sinf(a)), (int)pr, cols[k]);
      }
      fillCircle(cx, cy, (int)(S * 0.11f), WHITE);
    } break;
    case IC_MULTIMEDIA: {
      iconBase(x, y, S, rgb565(27,95,217), 22);
      fillTriangle(cx - (int)(S * 0.12f), cy - (int)(S * 0.18f),
                   cx - (int)(S * 0.12f), cy + (int)(S * 0.18f),
                   cx + (int)(S * 0.22f), cy, WHITE);
    } break;
    case IC_ALMACEN: {
      iconBase(x, y, S, rgb565(59,123,217), 22);
      uint16_t fol = rgb565(225,236,250);
      fillRoundRect(x + (int)(S * 0.20f), y + (int)(S * 0.28f), (int)(S * 0.30f), (int)(S * 0.12f), 3, fol);
      fillRoundRect(x + (int)(S * 0.18f), y + (int)(S * 0.36f), (int)(S * 0.64f), (int)(S * 0.34f), 5, fol);
      // nubecita
      uint16_t cl = rgb565(205,222,245);
      fillCircle(x + (int)(S * 0.60f), y + (int)(S * 0.34f), (int)(S * 0.10f), cl);
      fillCircle(x + (int)(S * 0.72f), y + (int)(S * 0.36f), (int)(S * 0.08f), cl);
      fillRect(x + (int)(S * 0.58f), y + (int)(S * 0.36f), (int)(S * 0.18f), (int)(S * 0.07f), cl);
    } break;
    case IC_MODOPC: {
      iconBase(x, y, S, rgb565(30,58,110), 22);
      fillRoundRect(x + (int)(S * 0.16f), y + (int)(S * 0.24f), (int)(S * 0.68f), (int)(S * 0.40f), 4, rgb565(235,240,250));
      fillRect(x + (int)(S * 0.22f), y + (int)(S * 0.30f), (int)(S * 0.56f), (int)(S * 0.28f), rgb565(45,95,205));
      fillRect(cx - (int)(S * 0.05f), y + (int)(S * 0.64f), (int)(S * 0.10f), (int)(S * 0.08f), rgb565(200,210,225));
      fillRoundRect(cx - (int)(S * 0.16f), y + (int)(S * 0.72f), (int)(S * 0.32f), (int)(S * 0.06f), 2, rgb565(200,210,225));
    } break;
    case IC_NOTAS: {
      iconBase(x, y, S, rgb565(232,167,90), 22);
      fillRoundRect(x + (int)(S * 0.22f), y + (int)(S * 0.18f), (int)(S * 0.48f), (int)(S * 0.62f), 5, WHITE);
      for(int i = 0; i < 3; i++)
        fillRect(x + (int)(S * 0.30f), y + (int)(S * 0.30f) + i * (int)(S * 0.12f), (int)(S * 0.32f), 2, rgb565(180,180,185));
      strokeSeg(x + S * 0.56f, y + S * 0.66f, x + S * 0.78f, y + S * 0.40f, tk / 2 + 1, rgb565(120,90,40)); // lapiz
      fillCircle((int)(x + S * 0.78f), (int)(y + S * 0.40f), tk / 2 + 1, rgb565(245,210,90));
    } break;
    case IC_EDU: {
      iconBase(x, y, S, rgb565(79,179,196), 22);
      uint16_t cap = rgb565(28,52,96);
      fillQuad(cx, cy - (int)(S * 0.24f), cx + (int)(S * 0.28f), cy - (int)(S * 0.06f),
               cx, cy + (int)(S * 0.12f), cx - (int)(S * 0.28f), cy - (int)(S * 0.06f), cap);
      fillRoundRect(cx - (int)(S * 0.18f), cy + (int)(S * 0.08f), (int)(S * 0.36f), (int)(S * 0.16f), 3, WHITE);
      strokeSeg(cx + S * 0.26f, cy - S * 0.06f, cx + S * 0.26f, cy + S * 0.14f, 1, cap);
    } break;
    case IC_NAV: {
      iconBase(x, y, S, rgb565(46,155,230), 22);
      fillRing(cx, cy, (int)(S * 0.30f), 2, WHITE);
      vLine(cx, cy - (int)(S * 0.30f), (int)(S * 0.60f), WHITE);
      hLine(cx - (int)(S * 0.30f), cy, (int)(S * 0.60f), WHITE);
      arcStroke(cx, cy, S * 0.18f, 90, 270, 2, WHITE);   // meridiano
      arcStroke(cx, cy, S * 0.18f, -90, 90, 2, WHITE);
    } break;
    case IC_CODE: {
      iconBase(x, y, S, rgb565(154,160,166), 22);
      uint16_t dk = rgb565(55,58,66);
      strokeSeg(cx - S * 0.10f, cy - S * 0.15f, cx - S * 0.26f, cy, tk / 2 + 1, dk);
      strokeSeg(cx - S * 0.26f, cy, cx - S * 0.10f, cy + S * 0.15f, tk / 2 + 1, dk);
      strokeSeg(cx + S * 0.10f, cy - S * 0.15f, cx + S * 0.26f, cy, tk / 2 + 1, dk);
      strokeSeg(cx + S * 0.26f, cy, cx + S * 0.10f, cy + S * 0.15f, tk / 2 + 1, dk);
      strokeSeg(cx + S * 0.04f, cy - S * 0.17f, cx - S * 0.04f, cy + S * 0.17f, tk / 2, dk);
    } break;
    case IC_BIEN: {
      iconBase(x, y, S, rgb565(92,193,90), 30);
      strokeSeg(cx - S * 0.16f, cy + S * 0.02f, cx - S * 0.02f, cy + S * 0.16f, tk / 2 + 1, WHITE);
      strokeSeg(cx - S * 0.02f, cy + S * 0.16f, cx + S * 0.20f, cy - S * 0.14f, tk / 2 + 1, WHITE);
    } break;
    case IC_PAINT: {
      iconBase(x, y, S, rgb565(241,231,210), 22);
      fillCircle(cx - (int)(S * 0.04f), cy + (int)(S * 0.02f), (int)(S * 0.27f), rgb565(236,226,205));
      fillCircle(cx + (int)(S * 0.11f), cy + (int)(S * 0.11f), (int)(S * 0.06f), rgb565(241,231,210)); // hueco
      fillCircle(cx - (int)(S * 0.14f), cy - (int)(S * 0.06f), (int)(S * 0.045f), rgb565(230,70,70));
      fillCircle(cx - (int)(S * 0.02f), cy - (int)(S * 0.13f), (int)(S * 0.045f), rgb565(240,200,60));
      fillCircle(cx + (int)(S * 0.10f), cy - (int)(S * 0.07f), (int)(S * 0.045f), rgb565(70,130,235));
      fillCircle(cx - (int)(S * 0.16f), cy + (int)(S * 0.09f), (int)(S * 0.045f), rgb565(80,190,90));
      strokeSeg(cx + S * 0.02f, cy - S * 0.16f, cx + S * 0.26f, cy - S * 0.30f, tk / 2 + 1, rgb565(140,100,60));
    } break;
    case IC_JUEGOS: {
      iconBase(x, y, S, rgb565(142,30,30), 22);
      drawRoundRect(x + (int)(S * 0.14f), y + (int)(S * 0.34f), (int)(S * 0.72f), (int)(S * 0.30f), (int)(S * 0.13f), WHITE);
      drawRoundRect(x + (int)(S * 0.14f) + 1, y + (int)(S * 0.34f) + 1, (int)(S * 0.72f) - 2, (int)(S * 0.30f) - 2, (int)(S * 0.12f), WHITE);
      // dpad
      fillRect(cx - (int)(S * 0.22f) - 1, cy + (int)(S * 0.02f), (int)(S * 0.12f), 3, WHITE);
      fillRect(cx - (int)(S * 0.16f) - 1, cy - (int)(S * 0.04f), 3, (int)(S * 0.12f), WHITE);
      // botones
      fillCircle(cx + (int)(S * 0.14f), cy - (int)(S * 0.01f), 3, WHITE);
      fillCircle(cx + (int)(S * 0.22f), cy + (int)(S * 0.05f), 3, WHITE);
    } break;
    case IC_AJUSTES: {
      iconBase(x, y, S, rgb565(138,143,152), 22);
      uint16_t g = rgb565(70,74,84);
      fillCircle(cx, cy, (int)(S * 0.26f), g);
      for(int k = 0; k < 8; k++){
        float a = k * 45 * 0.0174532925f;
        fillCircle((int)(cx + S * 0.30f * cosf(a)), (int)(cy + S * 0.30f * sinf(a)), (int)(S * 0.075f), g);
      }
      fillCircle(cx, cy, (int)(S * 0.10f), rgb565(138,143,152));
    } break;
    case IC_CALC: {
      iconBase(x, y, S, rgb565(58,58,60), 22);
      fillRoundRect(x + (int)(S * 0.18f), y + (int)(S * 0.16f), (int)(S * 0.64f), (int)(S * 0.16f), 3, rgb565(210,210,215));
      for(int rr = 0; rr < 3; rr++) for(int cc = 0; cc < 4; cc++){
        uint16_t bc = (cc == 3) ? rgb565(245,150,30) : rgb565(150,150,155);
        fillRoundRect(x + (int)(S * 0.18f) + cc * (int)(S * 0.17f), y + (int)(S * 0.40f) + rr * (int)(S * 0.15f),
                      (int)(S * 0.12f), (int)(S * 0.10f), 2, bc);
      }
    } break;
    case IC_CALEND: {
      iconBase(x, y, S, WHITE, 22);
      fillRect(x + (int)(S * 0.16f), y + (int)(S * 0.18f), (int)(S * 0.68f), (int)(S * 0.16f), rgb565(232,70,70));
      drawBigChar('1', cx - (int)(S * 0.60f * 0.30f), y + (int)(S * 0.36f), (int)(S * 0.44f), 3, rgb565(60,60,64));
    } break;
    case IC_CAMARA: {
      iconBase(x, y, S, rgb565(74,74,78), 22);
      fillCircle(cx, cy, (int)(S * 0.27f), rgb565(30,30,32));
      fillRing(cx, cy, (int)(S * 0.27f), 3, rgb565(120,120,128));
      fillCircle(cx, cy, (int)(S * 0.16f), rgb565(60,72,95));
      fillCircle(cx - (int)(S * 0.06f), cy - (int)(S * 0.06f), (int)(S * 0.05f), rgb565(150,175,205));
      fillRoundRect(x + (int)(S * 0.66f), y + (int)(S * 0.18f), (int)(S * 0.10f), (int)(S * 0.06f), 2, rgb565(190,190,195));
    } break;
  }
}

// ---------------- Iconos de barra de estado ----------------
static void drawWifi(int cx, int by, int R, uint16_t col){
  arcStroke(cx, by, R,        225, 315, 2, col);
  arcStroke(cx, by, R * 0.66f, 225, 315, 2, col);
  arcStroke(cx, by, R * 0.33f, 225, 315, 2, col);
  fillCircle(cx, by, 2, col);
}
static void drawBattery(int x, int y, int w, int h, int level, uint16_t col){
  drawRoundRect(x, y, w, h, 2, col);
  drawRoundRect(x + 1, y + 1, w - 2, h - 2, 2, col);
  fillRect(x + w, y + h / 3, 2, h / 3, col);       // pin +
  int fw = (w - 6) * level / 100;
  if(fw > 0) fillRect(x + 3, y + 3, fw, h - 6, col);
}

// #############################################################
// ##  TACTIL DE ALTO NIVEL (gestos)  Â·  original
// #############################################################
static Touch T;

// ---- FASE 4: Modo Kiosco (prestamo seguro) -------------------------------
// El estado vive AQUI ARRIBA, antes de flexPollTouch(), porque el filtro del
// area excluida tiene que actuar en el punto MAS ALTO del pipeline tactil: si
// se filtrara mas abajo, una app hospedada podria llegar a ver el toque.
#define KIOSK_ON 1                     // interruptor de toda la Fase 4
static bool kioskOn   = false;         // true = modo kiosco activo (NVS "kioskon")
static int  kioskApp  = -1;            // app "clavada" (indice de APP_REG, NVS "kioskapp")
static int  kioskExX  = 0, kioskExY = 0, kioskExW = 0, kioskExH = 0;  // area excluida (W=0 -> ninguna)
// Candado discreto + zona del gesto de salida. Coordenadas FISICAS del panel,
// no logicas: asi el gesto de salida funciona igual aunque la app corra en
// landscape (gLand).
//
// Va en la esquina SUPERIOR DERECHA, justo DEBAJO de la barra de estado. Antes
// estaba en (8,8) y se comia la hora, que renderHome() dibuja en (20,16). El
// hueco de aqui esta libre en todas las pantallas: el wifi ocupa x 403..425 y
// llega hasta y=28, la bateria x 434..466 e y 20..35, y los widgets del Home no
// empiezan hasta y=72. Con el candado en (448,44) quedan 9 px de margen bajo la
// bateria y 7 px hasta el borde derecho.
#define KIOSK_BADGE_X 448
#define KIOSK_BADGE_Y 44
#define KIOSK_BADGE_S 24
#define KIOSK_EXIT_PAD 14              // el hitbox del gesto es mas generoso que el dibujo
static bool kioskInExcluded(int px, int py){
  if(kioskExW <= 0 || kioskExH <= 0) return false;
  return px >= kioskExX && px < kioskExX + kioskExW && py >= kioskExY && py < kioskExY + kioskExH;
}
// Decide si un toque se descarta. Se define abajo del todo porque necesita
// gState, que se declara mas adelante; flexPollTouch la llama por prototipo.
static bool kioskTouchBlocked(int px, int py);
// El hitbox de salida SIEMPRE gana sobre el area excluida: si el usuario dibuja
// un rectangulo encima del candado, el dueno del telefono seguiria pudiendo
// salir. Sin esto, el propio modo kiosco se podria convertir en un ladrillo.
static bool kioskInExit(int px, int py){
  return px >= KIOSK_BADGE_X - KIOSK_EXIT_PAD && px <= KIOSK_BADGE_X + KIOSK_BADGE_S + KIOSK_EXIT_PAD &&
         py >= KIOSK_BADGE_Y - KIOSK_EXIT_PAD && py <= KIOSK_BADGE_Y + KIOSK_BADGE_S + KIOSK_EXIT_PAD;
}

// #############################################################
// ##  SUSPENSION (apagado NORMAL de pantalla, sin deep sleep)
// ##  ------------------------------------------------------
// ##  Entrar: doble-tap con DOS dedos en cualquier parte.
// ##  Salir : doble-tap con UN dedo en cualquier parte.
// ##
// ##  Que es y que NO es:
// ##   Â· El ESP32 NO se duerme. loop() sigue corriendo igual (WiFi,
// ##     reloj, animaciones internas). Lo unico que se apaga es la
// ##     SALIDA VISUAL: backlight a 0 por PWM + DISPOFF del panel.
// ##   Â· NO se toca gState. El sistema sigue "siendo" lo que era
// ##     (Home, App, Geo Dash, Modo PC...). La suspension es una capa
// ##     de driver superpuesta, no una pantalla de la aplicacion.
// ##   Â· NO se toca fb ni bbuf. La UI queda intacta en memoria, asi
// ##     que al despertar reaparece sola: el propio fundido del
// ##     backlight YA produce visualmente un fade-in desde negro
// ##     sobre lo que sigue estando en el framebuffer. Por eso NO se
// ##     compone ningun overlay negro de aparicion -- seria pintar
// ##     encima de la UI para conseguir un efecto que el backlight
// ##     regala gratis, y ademas obligaria a redibujar para limpiarlo.
// ##
// ##  El detector NO usa struct Touch: trabaja directamente sobre
// ##  gtFingers (cuenta de contactos del GT911). Asi es inmune a que
// ##  el filtro de abajo anule los flags de T, y el contrato de T
// ##  para el resto del sistema no cambia en absoluto.
// #############################################################
static bool gSuspOn      = false;   // suspension activa (desde el gesto hasta que termina el fundido de vuelta)
static bool gSuspDark    = false;   // backlight ya en 0 y DISPOFF enviado
static int  gSuspBright  = 80;      // brillo del usuario al suspender (para restaurarlo exacto)
static int  gSuspFade    = -1;      // -1 = sin fundido en curso; si no, brillo actual del fundido (0..100)
static int  gSuspFadeTo  = 0;       // destino del fundido
static uint32_t gSuspFadeMs = 0;    // millis() del ultimo paso del fundido
static bool gSuspSwallow = false;   // este poll pertenece al gesto -> no lo ve nadie mas

// ---- Estado del detector de doble-tap ----------------------------------
// Un "toque" (episodio) va desde que baja el primer dedo hasta que se levantan
// todos. Durante el episodio se anota cuantos dedos llego a haber.
static bool     gEpAct   = false;   // hay un episodio de toque en curso
static uint32_t gEpT0    = 0;       // millis() del inicio del episodio
static uint8_t  gEpRun2  = 0;       // polls CONSECUTIVOS con n>=2 dentro del episodio
static bool     gEpHad2  = false;   // el episodio quedo confirmado como "de 2 dedos"
static bool     gEpHad3  = false;   // llego a 3+ dedos -> no cuenta ni como 1 ni como 2
static uint32_t gTap2Ms  = 0;       // fin del ultimo toque valido de 2 dedos (0 = cadena vacia)
static uint32_t gTap1Ms  = 0;       // fin del ultimo toque valido de 1 dedo

// ---- Vuelta a donde estabas tras desbloquear ---------------------------
// Al despertar con clave configurada se cae en la pantalla de Bloqueo, pero el
// sitio donde estaba el usuario NO se pierde: se anota aqui y, al acertar el
// PIN, se restaura. -1 = nada pendiente.
static int  gSuspRetState = -1;     // gState que habia al suspender
static int  gSuspRetApp   = -1;     // gAppId que habia al suspender (si era ST_APP)
// true mientras corre una verificacion que SALIO de la pantalla de Bloqueo. Sin
// esto, cancelar el teclado de PIN caeria por la rama de lsuExit que lleva a
// ST_HOME, y el escritorio quedaria a la vista SIN haber desbloqueado -- justo
// el agujero que este cambio viene a cerrar.
static bool gLockVerifyLocked = false;
// Se define mucho mas abajo (necesita gState, renderLock y showLock, que aun no
// existen aqui). Mismo patron que kioskTouchBlocked: prototipo arriba, cuerpo
// abajo. Solo primitivos en la firma, como exige el auto-prototipado de Arduino.
static void suspWakeLockScreen();
// true mientras hay dedos sobre la rejilla del teclado. Se define abajo, con el
// teclado; aqui solo el prototipo (primitivos en la firma). Lo necesita el
// gesto de suspension: ver el VETO AL TECLEAR en suspGestureUpdate.
static bool kbTypingNow();

// Arranca un fundido de backlight NO bloqueante hacia 'to' (0..100).
static void suspFadeTo(int to){
  // Si ya habia un fundido en curso, sigue desde donde iba; si no, arranca desde
  // el brillo REAL del PWM (gBlPct), NO desde gBright. Al despertar gBright
  // sigue valiendo lo de siempre (p.ej. 80) mientras el PWM esta a 0: arrancar
  // desde gBright dejaria el fundido ya en su destino y la pantalla se
  // encenderia de golpe -- justo el fogonazo que hay que evitar.
  if(gSuspFade < 0) gSuspFade = gBlPct;
  gSuspFadeTo = to;
  gSuspFadeMs = millis();
}
static void suspEnter(){
  if(gSuspOn) return;
  gSuspBright = gBright;             // brillo del usuario, intacto (blWritePct no lo toca)
  gSuspOn = true; gSuspDark = false;
  suspFadeTo(0);
}
static void suspWake(){
  if(!gSuspOn) return;
  // ORDEN IMPORTANTE (privacidad): la pantalla de Bloqueo se compone MIENTRAS
  // sigue todo a oscuras -- backlight a 0 y el panel aun en DISPOFF. Asi lo que
  // habia antes de suspender no llega a verse ni un frame: cuando el backlight
  // empieza a subir, lo que hay en el framebuffer YA es el bloqueo.
  suspWakeLockScreen();
  if(gSuspDark){ panelDisplayOn(); gSuspDark = false; }   // revertir el DCS ANTES de subir el backlight
  suspFadeTo(gSuspBright);
}
// Un paso del fundido por vuelta de loop(). Sin delay(), igual de suave que el
// resto de animaciones del sistema (interpolado en varios ticks).
static void suspFadeTick(){
  if(gSuspFade < 0) return;
  uint32_t now = millis();
  if(now - gSuspFadeMs < SUSP_FADE_STEP_MS) return;
  gSuspFadeMs = now;
  if(gSuspFade > gSuspFadeTo){ gSuspFade -= SUSP_FADE_STEP; if(gSuspFade < gSuspFadeTo) gSuspFade = gSuspFadeTo; }
  else if(gSuspFade < gSuspFadeTo){ gSuspFade += SUSP_FADE_STEP; if(gSuspFade > gSuspFadeTo) gSuspFade = gSuspFadeTo; }
  blWritePct(gSuspFade);
  if(gSuspFade != gSuspFadeTo) return;
  gSuspFade = -1;                                   // fundido terminado
  if(gSuspFadeTo == 0){
    if(gSuspOn && !gSuspDark){ gSuspDark = true; panelDisplayOff(); }   // DCS DESPUES del negro
  } else {
    setBacklight(gSuspBright);      // deja gBright coherente y el PWM en su valor exacto
    gSuspOn = false;                // a partir de aqui el tactil vuelve a fluir normal
  }
}
// Detector de doble-tap. Se llama DESDE flexPollTouch(), al final, con gtFingers
// ya actualizado por gtPoll(). Decide ademas si este poll se lo traga el gesto.
static void suspGestureUpdate(){
#if SUSPEND_ON
  uint32_t now = millis();
  // Cuenta de dedos con la MISMA red de seguridad de 90 ms que usa flexPollTouch:
  // si el GT911 deja de reportar sin mandar el frame de "0 dedos", el episodio
  // no se queda colgado para siempre.
  int n = (now - gtFingersMs > 90) ? 0 : (int)gtFingers;

  // Caducidad de las cadenas de doble-tap (el 2o toque llego tarde -> se olvida).
  if(gTap2Ms && now - gTap2Ms > SUSP_TAP_WINDOW_MS) gTap2Ms = 0;
  if(gTap1Ms && now - gTap1Ms > SUSP_TAP_WINDOW_MS) gTap1Ms = 0;

  if(n > 0){
    if(!gEpAct){ gEpAct = true; gEpT0 = now; gEpRun2 = 0; gEpHad2 = false; gEpHad3 = false; }
    if(n >= 3) gEpHad3 = true;
    // ANTI FALSO-POSITIVO: n>=2 tiene que sostenerse SUSP_TAP_FRAMES polls
    // consecutivos. El instante en que el segundo dedo esta aterrizando es
    // ruidoso y puede dar un unico frame con n=2 espurio.
    // TOLERANCIA A DESINCRONIZACION: no se exige que los dos dedos bajen en el
    // mismo poll; basta con que en ALGUN momento del episodio n llegue a 2 el
    // numero de frames pedido, y la marca gEpHad2 ya no se pierde aunque luego
    // uno de los dedos se levante antes que el otro.
    if(n >= 2){ if(gEpRun2 < 255) gEpRun2++; if(gEpRun2 >= SUSP_TAP_FRAMES) gEpHad2 = true; }
    else gEpRun2 = 0;
  } else if(gEpAct){
    gEpAct = false;
    uint32_t dur = now - gEpT0;
    bool tooLong = (dur > SUSP_TAP_MAX_MS);          // long-press: no es un tap
    if(tooLong || gEpHad3){ gTap1Ms = 0; gTap2Ms = 0; }
    else if(gEpHad2){                                 // ---- toque valido de 2 dedos ----
      // VETO EN KIOSCO: mismo criterio que autoLockTick. En modo kiosco el
      // telefono esta prestado y quien lo tiene en la mano no sabe que existe el
      // gesto de despertar; una pantalla que se queda negra de golpe se lee como
      // "se ha roto" y el dueno acaba teniendo que rescatarlo. Ademas la
      // suspension no aporta nada ahi: el kiosco es para uso activo, no para
      // guardar el aparato.
      //
      // VETO AL TECLEAR (Fase B): escribir con los dos pulgares produce
      // continuamente episodios de 2 dedos. Sin esto pasaban dos cosas, las dos
      // malas: teclear a dos manos apagaba la pantalla sola, y -- peor -- el
      // gSuspSwallow de mas abajo anulaba TODOS los eventos de T mientras habia
      // dos dedos, asi que el teclado se quedaba mudo justo cuando se escribia
      // rapido. El gesto de suspender sigue existiendo igual en todas partes;
      // solo se calla mientras hay dedos sobre las teclas.
      bool veto = (KIOSK_ON && kioskOn) || kbTypingNow();
      if(gTap2Ms && (now - gTap2Ms) >= SUSP_TAP_GAP_MS){ gTap2Ms = 0; if(!gSuspOn && !veto) suspEnter(); }
      else gTap2Ms = now;
      gTap1Ms = 0;                                    // las dos cadenas son excluyentes
    } else {                                          // ---- toque valido de 1 dedo ----
      // El doble-tap de 1 dedo SOLO se escucha con la pantalla suspendida. Con la
      // pantalla encendida un doble-tap normal es un gesto legitimo de la UI y no
      // debe significar nada aqui.
      if(gSuspOn){
        if(gTap1Ms && (now - gTap1Ms) >= SUSP_TAP_GAP_MS){ gTap1Ms = 0; suspWake(); }
        else gTap1Ms = now;
      } else gTap1Ms = 0;
      gTap2Ms = 0;
    }
  }
  // Que toques se traga el gesto:
  //  Â· Suspendido: TODOS. Mientras la pantalla esta apagada, el doble-tap de 1
  //    dedo es lo unico que se lee; nadie mas debe ver el tactil, para que el
  //    toque de despertar no dispare de rebote algo de la UI que hay debajo.
  //  Â· Despierto: solo los episodios ya confirmados como de 2 dedos, para que el
  //    gesto de suspension no abra una app ni dispare el long-press de ST_CTX.
  //  Â· Tecleando: NUNCA. Ver el veto de arriba -- si el episodio de 2 dedos es
  //    de alguien escribiendo, T tiene que seguir llegando al teclado entero.
  gSuspSwallow = gSuspOn || (gEpHad2 && !kbTypingNow());
#else
  gSuspSwallow = false;
#endif
}

static void tDoRelease(unsigned long now){
  T.down = false; T.released = true;
  T.dx = T.x - T.startX; T.dy = T.y - T.startY;
  unsigned long dur = now - T.downMs;
  int adx = abs(T.dx), ady = abs(T.dy);
  if(adx < 16 && ady < 16 && dur < 550) T.tap = true;
  else if(ady > 55 && ady >= adx){ if(T.dy < 0) T.swipeUp = true; else T.swipeDown = true; }
  else if(adx > 55 && adx > ady){ if(T.dx < 0) T.swipeLeft = true; else T.swipeRight = true; }
}
// Ultima posicion PUBLICADA del dedo (no la ultima leida). Es el ancla
// de la histeresis de arriba; se reinicia en cada nuevo contacto.
static int tsHoldX = 0, tsHoldY = 0;
static void flexPollTouch(){
  T.pressed = T.released = T.tap = false;
  T.swipeUp = T.swipeDown = T.swipeLeft = T.swipeRight = false;
  uint16_t gx = 0, gy = 0;
  int8_t ev = gtPoll(gx, gy);
  // FASE 4: descarte SILENCIOSO del area excluida, en el punto mas alto del
  // pipeline. Se convierte en "sin dato" (-1), no en "soltado" (0): asi, si el
  // dedo entro arrastrando desde fuera, el gesto muere por el timeout normal de
  // 90 ms que ya existe abajo, y ninguna capa de arriba -ni el framework, ni una
  // app hospedada- llega a ver nunca esas coordenadas.
  if(ev == 1 && kioskTouchBlocked((int)gx, (int)gy)) ev = -1;
  unsigned long now = millis();
  bool wasDown = T.down;
  if(ev == 1){
    // ---- ZONA MUERTA / HISTERESIS DEL ARRASTRE ----------------------
    // POR QUE HACE FALTA (y por que NO bastaba con el umbral de 12 px de
    // T.moved): ese umbral solo decide SI hubo arrastre. Pero el scroll y
    // el drag no leen T.moved para moverse: leen T.x / T.y directamente y
    // van siguiendo el dedo. El XPT2046 es un ADC resistivo sin ningun
    // filtro interno, asi que con el dedo COMPLETAMENTE QUIETO sus
    // lecturas bailan +-1..2 px logicos entre sondeo y sondeo. Ese baile
    // entraba tal cual en la posicion de la lista: la lista avanzaba 2 px,
    // retrocedia 1, avanzaba 2... y eso es EXACTAMENTE la sensacion de que
    // "el scroll se traba" o "vibra". No era lentitud: era ruido.
    //
    // La solucion es histeresis: la posicion publicada solo se actualiza
    // cuando la nueva lectura se ha ido de verdad a mas de
    // TS_DRAG_DEADZONE px de la ultima publicada. Con el dedo quieto la
    // posicion queda clavada; en cuanto el dedo se mueve de verdad, se
    // salta al valor nuevo SIN suavizar, asi que no se anade ni un ms de
    // retardo al arrastre real (que es lo que estropearia un filtro de
    // media movil).
    if(!wasDown){
      tsHoldX = (int)gx; tsHoldY = (int)gy;
    } else {
      if(abs((int)gx - tsHoldX) >= TS_DRAG_DEADZONE) tsHoldX = (int)gx;
      if(abs((int)gy - tsHoldY) >= TS_DRAG_DEADZONE) tsHoldY = (int)gy;
    }
    gx = (uint16_t)tsHoldX; gy = (uint16_t)tsHoldY;

    T.x = gx; T.y = gy; T.lastMs = now;
    if(!wasDown){ T.down = true; T.pressed = true; T.startX = gx; T.startY = gy; T.downMs = now; T.moved = false; }
    else if(abs((int)gx - T.startX) > 12 || abs((int)gy - T.startY) > 12) T.moved = true;
  } else if(ev == 0){
    if(wasDown) tDoRelease(now);
  } else {
    if(wasDown && now - T.lastMs > 90) tDoRelease(now);
  }
  // SUSPENSION: el detector de doble-tap va AQUI, en el mismo punto alto del
  // pipeline que el filtro del kiosco y por el mismo motivo -- si filtrara mas
  // abajo, alguna capa ya habria visto el toque. Trabaja sobre gtFingers, no
  // sobre T, asi que anular los flags de T de abajo no le afecta.
  suspGestureUpdate();
  if(gSuspSwallow){
    // Se anulan TODOS los eventos, incluido T.down. Anular T.down tambien mata
    // el long-press del menu contextual (ST_CTX), que es justo lo que hace que
    // el gesto de 2 dedos y el long-press no puedan dispararse mutuamente: en
    // cuanto el episodio queda confirmado como de 2 dedos, para el resto del
    // sistema el dedo ya no esta abajo y el temporizador del long-press se
    // reinicia solo. Vale igual para el drag de la cortina de Ajustes rapidos
    // (necesita T.pressed) y para Geo Dash (lee T.down por flanco).
    T.pressed = T.released = T.tap = false;
    T.swipeUp = T.swipeDown = T.swipeLeft = T.swipeRight = false;
    T.down = false; T.moved = false;
  }
}

// #############################################################
// ##  ALMACENAMIENTO (NVS)  Â·  reloj interno  Â·  idiomas
// #############################################################
static Preferences prefs;
static bool  cfgOobeDone = false;
static int   cfgLang     = 0;                 // 0=ES 1=EN 2=FR 3=PT 4=IT 5=ZH
static bool  g24h        = false;             // formato de hora 24h
static int   gLockType   = 0;                 // 0 ninguno, 1 PIN, 2 contraseÃ±a
// ---- FASE 1: bloqueo global reforzado ------------------------------------
// Tres funciones INDEPENDIENTES, cada una con su interruptor. Poner un
// interruptor a 0 desactiva SOLO esa funcion y deja el resto del sistema de
// PIN/contrasena exactamente como estaba (mismo patron que GLASS_*_ON).
#define LOCK_FAILS_ON 1               // contador persistente + espera progresiva
#define LOCK_SHAKE_ON 1               // sacudida horizontal amortiguada al fallar
#define AUTOLOCK_ON   1               // bloqueo automatico por inactividad
// Umbrales de la espera progresiva. 1-3 fallos no cuestan nada; del 4 al 5 se
// cobran 30 s antes de poder reintentar; a partir del 6 son 5 min con mensaje
// explicito en pantalla.
#define LOCK_FAILS_SOFT     4
#define LOCK_FAILS_HARD     6
#define LOCK_WAIT_SOFT_MS   30000UL
#define LOCK_WAIT_HARD_MS   300000UL
// Opciones que ofrece Ajustes -> Seguridad -> Bloqueo de inactividad. El valor
// vivo es gAutoLockMs (NVS "autolockms"); 0 = nunca se bloquea solo.
#define AUTOLOCK_NOPT 6
static const uint32_t AUTOLOCK_OPTS[AUTOLOCK_NOPT]  = { 30000UL, 60000UL, 300000UL, 600000UL, 1800000UL, 0UL };
static const char*    AUTOLOCK_NAMES[AUTOLOCK_NOPT] = { "30 segundos", "1 minuto", "5 minutos", "10 minutos", "30 minutos", "Nunca" };
#define AUTOLOCK_DEFAULT_IDX 1                        // 1 minuto
#define AUTOLOCK_DEFAULT_MS  60000UL
static int      lockFails    = 0;                     // fallos acumulados (NVS "lockfails")
static uint32_t gAutoLockMs  = AUTOLOCK_DEFAULT_MS;   // ventana de inactividad
static int  autoLockIdx(){
  for(int i = 0; i < AUTOLOCK_NOPT; i++) if(AUTOLOCK_OPTS[i] == gAutoLockMs) return i;
  return AUTOLOCK_DEFAULT_IDX;
}
static const char* autoLockName(){ return AUTOLOCK_NAMES[autoLockIdx()]; }
// Ajusta a una de las opciones ofrecidas. Hace falta porque el valor anterior
// por defecto eran 2 minutos, que ya no esta en la lista: sin esto, una placa
// que ya tenga ese valor guardado mostraria "1 minuto" en Ajustes mientras se
// sigue bloqueando a los 2.
static void autoLockNormalize(){
  for(int i = 0; i < AUTOLOCK_NOPT; i++) if(AUTOLOCK_OPTS[i] == gAutoLockMs) return;
  gAutoLockMs = AUTOLOCK_OPTS[AUTOLOCK_DEFAULT_IDX];
}
static uint32_t gLastTouchMs = 0;                     // millis del ultimo contacto real
// ---- APAGADO SEGURO: preferencia de usuario (Ajustes -> Seguridad) -------
// IMPORTANTE -- no confundir dos cosas distintas:
//
//  Â· gPoffPin (esto) es la confirmacion por clave del APAGADO COMPLETO: evita
//    que alguien apague el aparato de un deslizamiento. Se aplica UNICA Y
//    EXCLUSIVAMENTE ahi. Ni suspEnter() ni suspWake() lo consultan jamas:
//    SUSPENDER no pide nada, es un gesto de un segundo.
//
//  Â· El BLOQUEO DE PANTALLA al despertar de una suspension (SUSPEND_LOCK_ON) es
//    otra cosa y depende de gLockType, la clave del dispositivo de toda la
//    vida. Ese si protege la privacidad: si alguien coge el aparato suspendido
//    y lo enciende, se encuentra el bloqueo, no lo que estabas haciendo.
static bool gPoffPin = false;                         // NVS "poffpin"
static bool gBootCleanOff = false;                    // este arranque viene de un apagado limpio (NVS "cleanoff")
// ---- FASES 2 y 3: menu contextual + bloqueo por app ----------------------
#define CTXMENU_ON 1                  // menu de long-press (0 = long-press va directo a Modo Edicion)
#define APPLOCK_ON 1                  // candado por app + verificacion al abrirla
// Una sola clave NVS con un bitmask en vez de 16 claves "applock_<i>": el
// IDENTIFICADOR UNICO de cada FlexApp ya es su indice en APP_REG (0..15, el
// mismo que usan homeOrder[], drawAppIcon() y enterApp()), y 16 apps caben
// exactas en un uint16_t. Un solo getInt/putInt, cero snprintf de claves.
static uint16_t gAppLock = 0;                         // bit i = app i bloqueada (NVS "applockm")
// Que hacer cuando la verificacion de PIN/contrasena ACIERTA. Es lo que permite
// reutilizar lsuStartVerify() para todo (pantalla, apps, kiosco) sin crear una
// segunda ruta de verificacion.
#define LSU_AFTER_UNLOCK    0         // desbloquear la pantalla (comportamiento de siempre)
#define LSU_AFTER_OPENAPP   1         // abrir la app bloqueada que se toco
#define LSU_AFTER_LOCKAPP   2         // confirmar que se pone el candado a una app
#define LSU_AFTER_UNLOCKAPP 3         // confirmar que se quita el candado a una app
#define LSU_AFTER_KIOSKOUT  4         // salir del Modo Kiosco
#define LSU_AFTER_POWEROFF  5         // apagar del todo (solo si el usuario activo "Apagado seguro")
static int lsuAfter    = LSU_AFTER_UNLOCK;
static int lsuAfterApp = -1;
#define LW_CLOCK   0x01   // reloj grande + fecha
#define LW_WEATHER 0x02   // clima (mock, sin datos reales aun)
#define LW_CAL     0x04   // calendario (mock, sin eventos reales aun)
#define LW_NOTIF   0x08   // notificaciones (datos reales: gNotifs[])
static uint8_t gLockWidgets = LW_CLOCK;       // widgets activos en Bloqueo (por defecto: solo el reloj, igual que hoy)
static int   gNavMode    = 0;                 // 0 = botones clasicos, 1 = gestos iOS
// Widgets redimensionables del Home (Fase 1): solo 2 tamanos, no continuo.
// bit0 = clima ancho (ocupa toda la fila), bit1 = noticias ancho. Mutuamente
// excluyentes -- si uno se ensancha, el otro se oculta para no tener que
// mover la rejilla de apps (que empieza justo debajo, a altura fija).
#define WW_CLIMA  0x01
#define WW_NOTICIAS 0x02
static uint8_t gWidgetWide = 0;               // por defecto: los dos normales, lado a lado (igual que hoy)
static int   gAnimStyle  = 0;                 // transicion al abrir/cerrar apps: 0=zoom, 1=fundido, 2=deslizar
static char  cfgName[24]  = "FlexOS Ultra";

// #############################################################
// ##  PREFERENCIAS DEL TECLADO (Fases A-G)  Â·  todas en NVS
// ##  ------------------------------------------------------
// ##  Viven aqui, con el resto de la configuracion, para que
// ##  cfgLoad()/cfgSavePrefs() las traten EXACTAMENTE igual que
// ##  gDark o gAnimStyle: mismo sitio, mismo momento, misma
// ##  namespace "flexos". Los valores por defecto son los del
// ##  teclado de siempre, asi que una placa que actualice no
// ##  nota ningun cambio hasta que el usuario toque Ajustes.
// #############################################################
#define KB_SIZE_COMPACT 0
#define KB_SIZE_NORMAL  1
#define KB_SIZE_BIG     2
static int  gKbSize     = KB_SIZE_NORMAL;   // NVS "kbsize"   Fase A
static bool gKbFastType = true;             // NVS "kbfast"   Fase B (escritura rapida / multitoque)
static bool gKbToolbar  = true;             // NVS "kbtool"   Fase C (barra superior)
static bool gKbPredict  = true;             // NVS "kbpred"   Fase F (texto predictivo)
static bool gKbSpell    = false;            // NVS "kbspell"  Fase F (revision ortografica basica)
static bool gKbEmojiSug = false;            // NVS "kbemoji"  Fase F (sugerir emojis)
static bool gKbHiCon    = false;            // NVS "kbhicon"  Fase E (teclado de contraste alto)
static int  gKbOpacity  = 100;              // NVS "kbopa"    Fase E (opacidad del panel, 40..100)
static int  gKbStyle    = 0;                // NVS "kbstyle"  Fase E (0 redondeada, 1 cuadrada, 2 contorno)
static int  gKbFontSc   = 1;                // NVS "kbfont"   Fase E (0 pequena, 1 normal, 2 grande)
static int  gKbLpMs     = 500;              // NVS "kblp"     Fase E (umbral de long-press: 350/500/700)
static int  gKbFxMs     = 100;              // NVS "kbfx"     Fase E/G (duracion del destello de tecla)
// Fila de simbolos personalizados (Fase E). Se guardan como INDICES dentro de
// KB_SYM_POOL (ver mas abajo) y no como texto libre: asi es imposible acabar
// con un caracter que la fuente 5x7 no sepa dibujar.
#define KB_SYMS 4
static int gKbSym[KB_SYMS] = { 0, 1, 2, 3 };   // NVS "kbsyms" (4 bytes)
// Atajos de texto (Fase E, los usa la Fase F al completar palabra). Sin struct
// a proposito: dos matrices de char[] planas se guardan en NVS con un solo
// putBytes cada una y no obligan a declarar un tipo nuevo.
#define KB_SC_MAX  8
#define KB_SC_ABR  10
#define KB_SC_EXP  24
static char gKbScAbr[KB_SC_MAX][KB_SC_ABR];
static char gKbScExp[KB_SC_MAX][KB_SC_EXP];
// Atajos de fabrica: se escriben la primera vez que arranca (o al restablecer).
static void kbShortcutsDefaults(){
  memset(gKbScAbr, 0, sizeof(gKbScAbr));
  memset(gKbScExp, 0, sizeof(gKbScExp));
  snprintf(gKbScAbr[0], KB_SC_ABR, "xq");  snprintf(gKbScExp[0], KB_SC_EXP, "porque");
  snprintf(gKbScAbr[1], KB_SC_ABR, "q");   snprintf(gKbScExp[1], KB_SC_EXP, "que");
  snprintf(gKbScAbr[2], KB_SC_ABR, "tb");  snprintf(gKbScExp[2], KB_SC_EXP, "tambi\xC3\xA9n");
  snprintf(gKbScAbr[3], KB_SC_ABR, "pf");  snprintf(gKbScExp[3], KB_SC_EXP, "por favor");
}
static void kbPrefsNormalize(){
  if(gKbSize < 0 || gKbSize > 2) gKbSize = KB_SIZE_NORMAL;
  if(gKbStyle < 0 || gKbStyle > 2) gKbStyle = 0;
  if(gKbFontSc < 0 || gKbFontSc > 2) gKbFontSc = 1;
  if(gKbOpacity < 40) gKbOpacity = 40; if(gKbOpacity > 100) gKbOpacity = 100;
  if(gKbLpMs != 350 && gKbLpMs != 500 && gKbLpMs != 700) gKbLpMs = 500;
  if(gKbFxMs != 60 && gKbFxMs != 100 && gKbFxMs != 160) gKbFxMs = 100;
  for(int i = 0; i < KB_SYMS; i++) if(gKbSym[i] < 0 || gKbSym[i] > 15) gKbSym[i] = i;
  for(int i = 0; i < KB_SC_MAX; i++){ gKbScAbr[i][KB_SC_ABR - 1] = 0; gKbScExp[i][KB_SC_EXP - 1] = 0; }
}
static void kbPrefsLoad(){
  gKbSize     = prefs.getInt("kbsize", KB_SIZE_NORMAL);
  gKbFastType = prefs.getBool("kbfast", true);
  gKbToolbar  = prefs.getBool("kbtool", true);
  gKbPredict  = prefs.getBool("kbpred", true);
  gKbSpell    = prefs.getBool("kbspell", false);
  gKbEmojiSug = prefs.getBool("kbemoji", false);
  gKbHiCon    = prefs.getBool("kbhicon", false);
  gKbOpacity  = prefs.getInt("kbopa", 100);
  gKbStyle    = prefs.getInt("kbstyle", 0);
  gKbFontSc   = prefs.getInt("kbfont", 1);
  gKbLpMs     = prefs.getInt("kblp", 500);
  gKbFxMs     = prefs.getInt("kbfx", 100);
  { uint8_t sb[KB_SYMS]; size_t n = prefs.getBytes("kbsyms", sb, KB_SYMS);
    if(n == KB_SYMS) for(int i = 0; i < KB_SYMS; i++) gKbSym[i] = (int)sb[i]; }
  size_t na = prefs.getBytes("kbscabr", gKbScAbr, sizeof(gKbScAbr));
  size_t ne = prefs.getBytes("kbscexp", gKbScExp, sizeof(gKbScExp));
  if(na != sizeof(gKbScAbr) || ne != sizeof(gKbScExp)) kbShortcutsDefaults();
  kbPrefsNormalize();
}
static void kbPrefsSaveOpen(){        // se llama con prefs YA abierto en escritura
  prefs.putInt("kbsize", gKbSize);
  prefs.putBool("kbfast", gKbFastType);
  prefs.putBool("kbtool", gKbToolbar);
  prefs.putBool("kbpred", gKbPredict);
  prefs.putBool("kbspell", gKbSpell);
  prefs.putBool("kbemoji", gKbEmojiSug);
  prefs.putBool("kbhicon", gKbHiCon);
  prefs.putInt("kbopa", gKbOpacity);
  prefs.putInt("kbstyle", gKbStyle);
  prefs.putInt("kbfont", gKbFontSc);
  prefs.putInt("kblp", gKbLpMs);
  prefs.putInt("kbfx", gKbFxMs);
  { uint8_t sb[KB_SYMS]; for(int i = 0; i < KB_SYMS; i++) sb[i] = (uint8_t)gKbSym[i];
    prefs.putBytes("kbsyms", sb, KB_SYMS); }
  prefs.putBytes("kbscabr", gKbScAbr, sizeof(gKbScAbr));
  prefs.putBytes("kbscexp", gKbScExp, sizeof(gKbScExp));
}
// Guarda SOLO las preferencias del teclado (abre y cierra por su cuenta). Lo
// usan las filas de la pantalla de Ajustes del teclado, que cambian una cosa
// cada vez y no tienen por que reescribir las doce claves del sistema.
static void kbPrefsSave(){
  prefs.begin("flexos", false);
  kbPrefsSaveOpen();
  prefs.end();
}

static void cfgLoad(){
  prefs.begin("flexos", true);
  cfgOobeDone = prefs.getBool("oobe", false);
  cfgLang     = prefs.getInt("lang", 0);
  g24h        = prefs.getBool("h24", false);
  uiGlass     = prefs.getBool("glass", false);
  gDark       = prefs.getBool("dark", true);
  gIconStyle  = prefs.getInt("iconstyle", 0);
  gBright     = prefs.getInt("bright", 80);
  gLockType   = prefs.getInt("locktype", 0);
  // FASE 1: el contador de fallos vive en NVS a proposito -- reiniciar la placa
  // NO es una via de escape para saltarse la espera progresiva.
  lockFails   = prefs.getInt("lockfails", 0);
  if(lockFails < 0) lockFails = 0;                                        // prefs corruptas
  gAutoLockMs = (uint32_t)prefs.getInt("autolockms", (int)AUTOLOCK_DEFAULT_MS);
  autoLockNormalize();
  // APAGADO SEGURO: preferencia de usuario (Ajustes -> Seguridad). Por defecto
  // DESACTIVADA para no cambiar el comportamiento de nadie al actualizar.
  gPoffPin    = prefs.getBool("poffpin", false);
  gAppLock    = (uint16_t)prefs.getInt("applockm", 0);                    // FASE 2: candados por app
  // FASE 4: el kiosco sobrevive al reinicio A PROPOSITO -- apagar el telefono no
  // puede ser la via facil de escape. Al arrancar se vuelve a entrar en la misma
  // app y solo el PIN/contrasena permite salir.
  kioskOn     = prefs.getBool("kioskon", false);
  kioskApp    = prefs.getInt("kioskapp", -1);
  kioskExX    = prefs.getInt("kioskx", 0);
  kioskExY    = prefs.getInt("kiosky", 0);
  kioskExW    = prefs.getInt("kioskw", 0);
  kioskExH    = prefs.getInt("kioskh", 0);
  if(kioskApp < 0 || kioskApp > 15) { kioskOn = false; kioskApp = -1; }   // prefs corruptas
  if(gLockType == 0) kioskOn = false;   // sin clave configurada no habria forma de salir: no se activa
  gLockWidgets = (uint8_t)prefs.getInt("lockwidgets", LW_CLOCK);
  gNavMode    = prefs.getInt("navmode", 0);
  gWidgetWide = (uint8_t)prefs.getInt("widgetwide", 0);
  if((gWidgetWide & WW_CLIMA) && (gWidgetWide & WW_NOTICIAS)) gWidgetWide = 0;  // combinacion invalida (prefs corruptas): normaliza
  gAnimStyle  = prefs.getInt("animstyle", 0);
  kbPrefsLoad();                     // Fases A-G: preferencias del teclado
  String n = prefs.getString("name", "FlexOS Ultra");
  n.toCharArray(cfgName, sizeof(cfgName));
  prefs.end();
}
// Guarda las preferencias personalizables (idioma, formato, estilo, brillo)
static void cfgSavePrefs(){
  prefs.begin("flexos", false);
  prefs.putInt("lang", cfgLang);
  prefs.putBool("h24", g24h);
  prefs.putBool("glass", uiGlass);
  prefs.putBool("dark", gDark);
  prefs.putInt("iconstyle", gIconStyle);
  prefs.putInt("bright", gBright);
  prefs.putInt("lockwidgets", gLockWidgets);
  prefs.putInt("navmode", gNavMode);
  prefs.putInt("widgetwide", gWidgetWide);
  prefs.putInt("animstyle", gAnimStyle);
  prefs.putInt("autolockms", (int)gAutoLockMs);
  prefs.putBool("poffpin", gPoffPin);
  kbPrefsSaveOpen();                 // Fases A-G: preferencias del teclado
  prefs.end();
}
// FASE 1: se guarda SOLO el contador de fallos. Aparte de cfgSavePrefs porque se
// escribe en momentos muy distintos (cada fallo / cada acierto) y no queremos
// reescribir doce claves por cada digito equivocado.
static void lockFailsSave(){
  prefs.begin("flexos", false);
  prefs.putInt("lockfails", lockFails);
  prefs.end();
}
// ---- FASE 2: candado por app (bitmask indexado por indice de APP_REG) ----
static bool appLockGet(int id){
  if(!APPLOCK_ON || id < 0 || id > 15) return false;
  return (gAppLock & (uint16_t)(1u << id)) != 0;
}
static void appLockSet(int id, bool on){
  if(id < 0 || id > 15) return;
  if(on) gAppLock |=  (uint16_t)(1u << id);
  else   gAppLock &= (uint16_t)~(1u << id);
  prefs.begin("flexos", false);
  prefs.putInt("applockm", (int)gAppLock);
  prefs.end();
}
// ---- FASE 4: persistencia del Modo Kiosco --------------------------------
static void kioskSave(){
  prefs.begin("flexos", false);
  prefs.putBool("kioskon", kioskOn);
  prefs.putInt("kioskapp", kioskApp);
  prefs.putInt("kioskx", kioskExX);
  prefs.putInt("kiosky", kioskExY);
  prefs.putInt("kioskw", kioskExW);
  prefs.putInt("kioskh", kioskExH);
  prefs.end();
}
static void cfgSaveOobe(){
  prefs.begin("flexos", false);
  prefs.putBool("oobe", true);
  prefs.putInt("lang", cfgLang);
  prefs.putString("name", cfgName);
  prefs.end();
  cfgOobeDone = true;
}

// -------- Idiomas --------
#define NLANG 6
static const char* LANG_ENDONYM[NLANG] = {
  "Espa\xC3\xB1ol", "English", "Fran\xC3\xA7" "ais", "Portugu\xC3\xAas", "Italiano", "\xE4\xB8\xAD\xE6\x96\x87" };
// idx de arrays de fecha (ZH no tiene glifos -> usa EN)
static inline int LI(){ return (cfgLang == 5) ? 1 : cfgLang; }

// Cadenas de interfaz. Columnas: ES,EN,FR,PT,IT. ZH usa EN.
enum { S_SELLANG, S_CONTINUE, S_YOURNAME, S_NAMEHINT, S_START, S_SWIPE,
       S_WEATHER, S_NEWS, S_NONEWS, S_NOEVENTS, S_NOTIFS, S_NONOTIFS, S_SOON, S_M2, S_BACK, S_WELCOME, S_NSTR };
static const char* CH[S_NSTR][5] = {
  {"Selecciona tu idioma","Select your language","Choisis ta langue","Selecione o idioma","Seleziona la lingua"},
  {"Continuar","Continue","Continuer","Continuar","Continua"},
  {"\xC2\xBF" "C\xC3\xB3mo se llama el equipo?","Name your device","Nomme ton appareil","Nomeie o dispositivo","Nomina il dispositivo"},
  {"Toca para escribir","Tap to type","Touche pour \xC3\xA9" "crire","Toque para escrever","Tocca per scrivere"},
  {"Comenzar","Get started","Commencer","Come\xC3\xA7" "ar","Inizia"},
  {"Desliza arriba para desbloquear","Swipe up to unlock","Glisse vers le haut","Deslize para desbloquear","Scorri per sbloccare"},
  {"Clima","Weather","M\xC3\xA9t\xC3\xA9o","Clima","Meteo"},
  {"Noticias","News","Actualit\xC3\xA9s","Not\xC3\xAD" "cias","Notizie"},
  {"(No hay noticias que mostrar)","(No news to show)","(Aucune actualit\xC3\xA9)","(Sem not\xC3\xAD" "cias)","(Nessuna notizia)"},
  {"(Sin eventos)","(No events)","(Aucun \xC3\xA9v\xC3\xA9nement)","(Sem eventos)","(Nessun evento)"},
  {"Notificaciones","Notifications","Notifications","Notifica\xC3\xA7\xC3\xB5" "es","Notifiche"},
  {"(Sin notificaciones)","(No notifications)","(Aucune notification)","(Sem notifica\xC3\xA7\xC3\xB5" "es)","(Nessuna notifica)"},
  {"En construcci\xC3\xB3n","Coming soon","Bient\xC3\xB4t disponible","Em breve","Prossimamente"},
  {"Llega en el Milestone 2","Arrives in Milestone 2","Arrive au Milestone 2","Chega no Milestone 2","Arriva nel Milestone 2"},
  {"Volver","Back","Retour","Voltar","Indietro"},
  {"Bienvenido a","Welcome to","Bienvenue sur","Bem-vindo ao","Benvenuto in"},
};
static const char* t(int id){ return CH[id][LI()]; }

// Etiquetas de apps. Mismo orden que el enum IC_*.
static const char* APP[16][5] = {
  {"Reloj","Clock","Horloge","Rel\xC3\xB3gio","Orologio"},
  {"Galer\xC3\xAD" "a","Gallery","Galerie","Galeria","Galleria"},
  {"Multimedia","Media","Multim\xC3\xA9" "dia","Multim\xC3\xAD" "dia","Multimedia"},
  {"Almacenamiento","Storage","Stockage","Armazenamento","Archivi"},
  {"Modo PC","PC Mode","Mode PC","Modo PC","Modo PC"},
  {"Notas","Notes","Notes","Notas","Note"},
  {"Educaci\xC3\xB3n","Education","\xC3\x89" "ducation","Educa\xC3\xA7\xC3\xA3o","Istruzione"},
  {"Navegador","Browser","Navigateur","Navegador","Browser"},
  {"Code IDE","Code IDE","Code IDE","Code IDE","Code IDE"},
  {"Bienestar","Wellbeing","Bien-\xC3\xAatre","Bem-estar","Benessere"},
  {"Paint","Paint","Dessin","Paint","Disegno"},
  {"Juegos","Games","Jeux","Jogos","Giochi"},
  {"Ajustes","Settings","R\xC3\xA9glages","Ajustes","Impostazioni"},
  {"Calculadora","Calculator","Calculatrice","Calculadora","Calcolatrice"},
  {"Calendario","Calendar","Calendrier","Calend\xC3\xA1rio","Calendario"},
  {"C\xC3\xA1mara","Camera","Appareil","C\xC3\xA2mera","Fotocamera"},
};
static const char* appName(int id){ return APP[id][LI()]; }

// -------- Nombres de dias/meses --------
static const char* WD_FULL[5][7] = {
  {"Domingo","Lunes","Martes","Mi\xC3\xA9rcoles","Jueves","Viernes","S\xC3\xA1" "bado"},
  {"Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"},
  {"Dimanche","Lundi","Mardi","Mercredi","Jeudi","Vendredi","Samedi"},
  {"Domingo","Segunda","Ter\xC3\xA7" "a","Quarta","Quinta","Sexta","S\xC3\xA1" "bado"},
  {"Domenica","Luned\xC3\xAC","Marted\xC3\xAC","Mercoled\xC3\xAC","Gioved\xC3\xAC","Venerd\xC3\xAC","Sabato"},
};
static const char* WD_SHORT[5][7] = {
  {"dom","lun","mar","mi\xC3\xA9","jue","vie","s\xC3\xA1" "b"},
  {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"},
  {"dim","lun","mar","mer","jeu","ven","sam"},
  {"dom","seg","ter","qua","qui","sex","s\xC3\xA1" "b"},
  {"dom","lun","mar","mer","gio","ven","sab"},
};
static const char* MO_FULL[5][12] = {
  {"enero","febrero","marzo","abril","mayo","junio","julio","agosto","septiembre","octubre","noviembre","diciembre"},
  {"January","February","March","April","May","June","July","August","September","October","November","December"},
  {"janvier","f\xC3\xA9vrier","mars","avril","mai","juin","juillet","ao\xC3\xBBt","septembre","octobre","novembre","d\xC3\xA9" "cembre"},
  {"janeiro","fevereiro","mar\xC3\xA7" "o","abril","maio","junho","julho","agosto","setembro","outubro","novembro","dezembro"},
  {"gennaio","febbraio","marzo","aprile","maggio","giugno","luglio","agosto","settembre","ottobre","novembre","dicembre"},
};
static const char* MO_SHORT[5][12] = {
  {"ene","feb","mar","abr","may","jun","jul","ago","sep","oct","nov","dic"},
  {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"},
  {"jan","f\xC3\xA9v","mar","avr","mai","jui","jul","ao\xC3\xBB","sep","oct","nov","d\xC3\xA9" "c"},
  {"jan","fev","mar","abr","mai","jun","jul","ago","set","out","nov","dez"},
  {"gen","feb","mar","apr","mag","giu","lug","ago","set","ott","nov","dic"},
};

// -------- Reloj interno (sin NTP; se siembra a sab, 4 jul 13:23) --------
static int rtcY = 2026, rtcMo = 7, rtcD = 4, rtcWd = 6, rtcH = 13, rtcMin = 23;
static long          seedMinOfDay = 13 * 60 + 23;
static unsigned long clkBootMs = 0;
static long          clkLastMin = -1;

static int daysInMonth(int y, int m){
  static const int dm[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
  if(m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
  return dm[m - 1];
}
static void clkSetDate(long addDays){
  int Y = 2026, Mo = 7, D = 4, Wd = 6;
  for(long i = 0; i < addDays; i++){
    D++; Wd = (Wd + 1) % 7;
    if(D > daysInMonth(Y, Mo)){ D = 1; Mo++; if(Mo > 12){ Mo = 1; Y++; } }
  }
  rtcY = Y; rtcMo = Mo; rtcD = D; rtcWd = Wd;
}
// devuelve true si cambio el minuto (para repintar el reloj)
static bool clkUpdate(){
  long mins = seedMinOfDay + (long)((millis() - clkBootMs) / 60000UL);
  long mod = mins % 1440; if(mod < 0) mod += 1440;
  int h = (int)(mod / 60), mi = (int)(mod % 60);
  if(mins == clkLastMin) return false;
  clkLastMin = mins;
  rtcH = h; rtcMin = mi;
  clkSetDate(mins / 1440);
  return true;
}
// Reloj "H:MM" pelado. Lo usa el RELOJ GIGANTE vectorial (bloqueo y app Reloj),
// que solo sabe dibujar digitos y ':' -> aqui NO puede entrar el AM/PM.
static void clkStr12(char* out, size_t n){
  if(g24h){ snprintf(out, n, "%d:%02d", rtcH, rtcMin); return; }
  int h12 = rtcH % 12; if(h12 == 0) h12 = 12;
  snprintf(out, n, "%d:%02d", h12, rtcMin);
}
// Reloj para las BARRAS DE ESTADO (fuente normal, sÃ­ admite letras).
// Antes todas las barras llamaban a clkStr12, y como en 12 h no se anadia
// AM/PM, el ajuste "Formato de hora" no cambiaba NADA visible entre las 00:00
// y las 12:59, y a partir de esa hora "1:23" podia ser la una de la tarde o de
// la madrugada. Ahora 24 h -> "13:23" y 12 h -> "1:23 PM".
// Reserva al menos 12 bytes: "12:34 PM" son 9 con el terminador.
static void clkStrBar(char* out, size_t n){
  if(g24h){ snprintf(out, n, "%d:%02d", rtcH, rtcMin); return; }
  int h12 = rtcH % 12; if(h12 == 0) h12 = 12;
  snprintf(out, n, "%d:%02d %s", h12, rtcMin, (rtcH < 12) ? "AM" : "PM");
}
// #############################################################
// ##  PANTALLAS + MAQUINA DE ESTADOS + ARRANQUE  (original)
// #############################################################

// ---- Declaraciones adelantadas ----
static void blitToFb(uint16_t* src);
static const char* resetReasonStr();
static void showBootBanner();
static void splashFrame(uint8_t a);
static void splashTick();
static void enterOobeLang();  static void renderOobeLang();  static void oobeLangTick();
static void enterOobeName();  static void drawKeyboard();    static void drawNameField();
static bool hitKey(int px, int py, int &code); static void oobeNameTick();
static void buildLongDate(char* out, size_t n); static void buildShortDate(char* out, size_t n);
static void renderLock(); static void showLock();
static void renderHome(); static void showHome(); static bool hitHomeIcon(int px, int py, int &id);
static void enterHome();  static void enterApp(int id); static void appTick();
static void swPushAndCapture(uint8_t id); static void activarMultitarea(); static void swTick();  // App Switcher
static void swPushNoThumb(uint8_t id);   // apps landscape: sin miniatura (ver appClose)
static void lsuEnter(); static void lsuTick();             // Seguridad -> Bloqueo (PIN/ContraseÃ±a)
static void lsuStartVerify();                              // pedir PIN/contraseÃ±a al desbloquear
static void composeUnlock(int off); static void animateTo(int from, int to);
static void lockTick(); static void homeTick();
static bool handleiOSGestures();                          // gestos de la barra inferior (modo iOS)
static void lsuStartVerifyFor(int what, int id);          // FASE 3: verificar y luego hacer algo (abrir app, kiosco...)
static void ctxOpen(int slot); static void ctxTick();     // FASE 2: menu contextual de long-press
static void kioskShowBadge();                             // FASE 4: candado discreto de "kiosco activo"
static void kioskSetTick();                               // FASE 4: pantalla de definir el area excluida
static void kioskExitNow();                               // FASE 4: salida ya verificada
static void poffEnter();                                  // APAGADO: pantalla de confirmacion ("desliza para apagar")
static void poffTick();                                   // APAGADO: arrastre del slider + cancelar
static void poffBeginAnim();                              // APAGADO: confirmado -> arranca la animacion final
static void poffAnimTick();                               // APAGADO: un paso de la animacion (fundido + "Flex OS" + deep sleep)
static void kbsEnter();                                   // FASE E: Ajustes -> Teclado (pantalla propia)
static void kbsTick();                                    // FASE E: su tick, desde loop()
static void tcalEnter(bool fromSettings);                 // PORT S3: calibracion tactil (pantalla propia, no bloqueante)
static void tcalTick();                                   // PORT S3: su tick, desde loop()
static void noteRenderAll();                              // Notas: repintado completo (lo llaman el portapapeles y los chips)
static void noteInsert(const char* s);                    // Notas: insercion en el cursor (la usa el portapapeles)

// ---- Estado global ----
// ST_CTX y ST_KIOSKSET son de las Fases 2 y 4. Se anaden al FINAL del enum a
// proposito: los valores de los estados anteriores no se mueven. Lo mismo vale
// para los dos estados del apagado, anadidos al final por el mismo motivo.
// ST_KBSET (Fase E del teclado) se anade tambien al final, por identico motivo.
// ST_TOUCHCAL (calibracion tactil del port S3) va el ultimo, por lo mismo.
enum { ST_SPLASH = 0, ST_OOBE_LANG, ST_OOBE_NAME, ST_LOCK, ST_HOME, ST_APP, ST_SWITCHER, ST_LOCKSETUP, ST_WIFI, ST_CTX, ST_KIOSKSET,
       ST_POWEROFF_CONFIRM, ST_POWEROFF_ANIM, ST_KBSET, ST_TOUCHCAL };
static int  gState = ST_SPLASH;
static unsigned long splashStart = 0;
static int  lockOff = 0, lastLockOff = -1;
static int  oobeSel = 0;
static int  gAppId  = 0;
static bool editMode = false;                                   // Modo Edicion del Home
static uint8_t homeOrder[12] = { 0,1,2,3,4,5,6,7,8,9,10,11 };   // app id por slot (reordenable)
static bool gMinChanged = false;   // lo pone loop(): true cuando cambia el minuto

// ---- Invalidacion de caches de pantalla ----
// homeBuf (y la cortina qsBuf, que se compone de el) son escritorios YA
// pintados. Si cambia un ajuste que altera su aspecto -idioma, formato de
// hora, Liquid Glass, estilo de iconos- hay que volver a componerlos, o al
// salir de Ajustes se ve el escritorio VIEJO hasta que cambie el minuto.
// Ese era el motivo de que "algo no coincidiera" tras tocar un ajuste.
static bool gHomeDirty = false;    // homeBuf no refleja los ajustes actuales
static bool qsDirty    = true;     // qsBuf debe recomponerse
// teclado
static int  keyN = 0;
static int  kX[48], kY[48], kW[48], kH[48], kCode[48];

static void blitToFb(uint16_t* src){ fbCopyBand(src, 0, SCR_H - 1); }

// ---------------- Banda forense de arranque ----------------
static const char* resetReasonStr(){
  switch(esp_reset_reason()){
    case ESP_RST_POWERON:   return "POWERON";
    case ESP_RST_SW:        return "SW";
    case ESP_RST_PANIC:     return "PANIC (crash)";
    case ESP_RST_INT_WDT:   return "INT_WDT";
    case ESP_RST_TASK_WDT:  return "TASK_WDT";
    case ESP_RST_WDT:       return "WDT";
    case ESP_RST_BROWNOUT:  return "BROWNOUT (voltaje)";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    default:                return "OTRO";
  }
}
// Solo se muestra tras un reinicio ANORMAL (crash/watchdog/brownout),
// para que puedas leer el motivo sin monitor serie. En un encendido
// normal NO aparece: el arranque va directo al splash limpio.
static void showBootBanner(){
  setBuf(fb);
  fillRect(0, 0, SCR_W, SCR_H, rgb565(0,0,0));
  drawTextC(SCR_W / 2, SCR_H / 2 - 24, "FlexOS Ultra", 3, rgb565(235,238,245));
  char b[72];
  snprintf(b, sizeof(b), "ultimo reinicio: %s", resetReasonStr());
  drawTextC(SCR_W / 2, SCR_H / 2 + 22, b, 1, rgb565(240,185,90));
  drawTextC(SCR_W / 2, SCR_H / 2 + 42, "ESP32-S3 - interfaz proporcional 533x800 -> 320x480", 1, rgb565(140,150,170));
  flxFlushAll();
  delay(2200);
}

// ---------------- SPLASH (fundido sobre NEGRO ABSOLUTO) ----------------
static void splashFrame(uint8_t a){
  int size = 6;
  int ty = SCR_H / 2 - 40;
  int ss = 2, sty = ty + size * 7 + 20;
  int y0 = ty - 10, y1 = sty + ss * 7 + 8;
  setBuf(fb);
  fillRect(0, y0, SCR_W, y1 - y0 + 1, rgb565(0,0,0));                  // banda negra
  drawTextCA(SCR_W / 2, ty, "FlexOS Ultra", size, rgb565(255,255,255), a);
  drawTextCA(SCR_W / 2, sty, "ESP32-S3", ss, rgb565(170,182,200), (uint8_t)(a * 7 / 10));
  flxFlush(y0, y1);
  // puntos de carga (estilo movil): uno se ilumina en secuencia
  int dy = SCR_H - 128, phase = (int)((millis() / 320) % 3);
  fillRect(0, dy - 7, SCR_W, 15, rgb565(0,0,0));
  for(int i = 0; i < 3; i++)
    fillCircleAA(SCR_W / 2 - 16 + i * 16, dy, 4.0f,
                 (i == phase) ? rgb565(255,255,255) : rgb565(70,74,82));
  flxFlush(dy - 8, dy + 8);
}
static void splashTick(){
  unsigned long e = millis() - splashStart;
  uint8_t a;
  if(e < 600)       a = (uint8_t)(e * 255 / 600);
  else if(e < 2000) a = 255;
  else if(e < 2600) a = (uint8_t)(255 - (e - 2000) * 255 / 600);
  else {
    if(!cfgOobeDone) enterOobeLang();
    // FASE 4: si el telefono se apago con el kiosco puesto, se vuelve a entrar en
    // la misma app SIN pasar por el escritorio. La unica salida sigue siendo el
    // gesto del candado + PIN/contrasena.
    else if(KIOSK_ON && kioskOn && kioskApp >= 0){
      renderHome();                      // winRevealAnim compone sobre homeBuf
      enterApp(kioskApp);
      kioskShowBadge();
    }
    else { renderHome(); renderLock(); showLock(); gState = ST_LOCK; lockOff = 0; lastLockOff = -1; }
    return;
  }
  splashFrame(a);
  delay(16);
}

// ---------------- OOBE: idioma ----------------
static void renderOobeLang(){
  drawWallpaper(fb, false); setBuf(fb);
  drawTextC(SCR_W / 2, 78, t(S_SELLANG), 3, rgb565(255,255,255));
  int rowH = 74, gap = 14, x = 44, w = SCR_W - 88, y0 = 158;
  for(int i = 0; i < NLANG; i++){
    int y = y0 + i * (rowH + gap);
    bool sel = (i == oobeSel);
    fillRoundRectA(x, y, w, rowH, 18, rgb565(255,255,255), sel ? 235 : 55);
    uint16_t tc = sel ? rgb565(28,28,38) : rgb565(255,255,255);
    const char* lbl = (i == 5) ? "Chinese" : LANG_ENDONYM[i];
    drawText(x + 28, y + rowH / 2 - 10, lbl, 3, tc);
    if(sel){
      int chx = x + w - 48, chy = y + rowH / 2;
      strokeSeg(chx - 8, chy, chx - 2, chy + 8, 2, rgb565(40,160,90));
      strokeSeg(chx - 2, chy + 8, chx + 12, chy - 10, 2, rgb565(40,160,90));
    }
  }
  int by = SCR_H - 96, bw = SCR_W - 88, bx = 44;
  fillRoundRect(bx, by, bw, 60, 30, rgb565(255,255,255));
  drawTextC(SCR_W / 2, by + 21, t(S_CONTINUE), 3, rgb565(40,80,200));
  flxFlushAll();
}
static void enterOobeLang(){ cfgLang = 0; oobeSel = 0; gState = ST_OOBE_LANG; renderOobeLang(); }
static void oobeLangTick(){
  if(!T.tap) return;
  int rowH = 74, gap = 14, x = 44, w = SCR_W - 88, y0 = 158;
  for(int i = 0; i < NLANG; i++){
    int y = y0 + i * (rowH + gap);
    if(T.x >= x && T.x <= x + w && T.y >= y && T.y <= y + rowH){
      oobeSel = i; cfgLang = i; renderOobeLang(); return;
    }
  }
  int by = SCR_H - 96, bw = SCR_W - 88, bx = 44;
  if(T.x >= bx && T.x <= bx + bw && T.y >= by && T.y <= by + 60){ enterOobeName(); return; }
}

// ---------------- OOBE: nombre (teclado QWERTY) ----------------
static void kbAdd(int x, int y, int w, int h, int code, const char* cap){
  kX[keyN] = x; kY[keyN] = y; kW[keyN] = w; kH[keyN] = h; kCode[keyN] = code; keyN++;
  fillRoundRect(x, y, w, h, 8, rgb565(250,250,252));
  if(cap) drawTextC(x + w / 2, y + h / 2 - 7, cap, 2, rgb565(28,28,38));
  else if(code == -2) fillRoundRect(x + w / 2 - 30, y + h / 2 - 2, 60, 4, 2, rgb565(90,90,100)); // barra espacio
}
static void drawKeyboard(){
  keyN = 0;
  const char* r1 = "QWERTYUIOP";
  const char* r2 = "ASDFGHJKL";
  const char* r3 = "ZXCVBNM";
  int kw = 40, kh = 52, g = 6, step = 60, kbTop = 544;
  { int n = 10; int sx = (SCR_W - (n * kw + (n - 1) * g)) / 2;
    for(int i = 0; i < n; i++){ char c[2] = { r1[i], 0 }; kbAdd(sx + i * (kw + g), kbTop, kw, kh, r1[i], c); } }
  { int n = 9; int sx = (SCR_W - (n * kw + (n - 1) * g)) / 2;
    for(int i = 0; i < n; i++){ char c[2] = { r2[i], 0 }; kbAdd(sx + i * (kw + g), kbTop + step, kw, kh, r2[i], c); } }
  { int n = 7; int backW = 2 * kw + g;
    int totalW = n * kw + (n - 1) * g + g + backW;
    int sx = (SCR_W - totalW) / 2;
    for(int i = 0; i < n; i++){ char c[2] = { r3[i], 0 }; kbAdd(sx + i * (kw + g), kbTop + 2 * step, kw, kh, r3[i], c); }
    kbAdd(sx + n * (kw + g), kbTop + 2 * step, backW, kh, -1, "<-"); }
  { int okW = 120, y = kbTop + 3 * step;
    int spX = 8 + kw, spW = SCR_W - spX - (okW + g) - 8;
    kbAdd(spX, y, spW, kh, -2, NULL);
    kbAdd(spX + spW + g, y, okW, kh, -3, "OK"); }
}
static void drawNameField(){
  int fx = 44, fy = 150, fw = SCR_W - 88, fh = 64;
  fillRoundRect(fx, fy, fw, fh, 16, rgb565(255,255,255));
  if(strlen(cfgName) == 0)
    drawText(fx + 20, fy + fh / 2 - 8, t(S_NAMEHINT), 2, rgb565(150,150,158));
  else {
    int ex = drawText(fx + 20, fy + fh / 2 - 10, cfgName, 3, rgb565(24,24,30));
    fillRect(ex + 2, fy + 16, 3, fh - 32, rgb565(55,120,240));
  }
  flxFlush(fy - 2, fy + fh + 2);
}
static void enterOobeName(){
  gState = ST_OOBE_NAME;
  cfgName[0] = 0;
  drawWallpaper(fb, false); setBuf(fb);
  drawTextC(SCR_W / 2, 70, t(S_YOURNAME), 3, rgb565(255,255,255));
  drawKeyboard();
  drawNameField();
  flxFlushAll();
}
static bool hitKey(int px, int py, int &code){
  for(int i = 0; i < keyN; i++)
    if(px >= kX[i] && px <= kX[i] + kW[i] && py >= kY[i] && py <= kY[i] + kH[i]){ code = kCode[i]; return true; }
  return false;
}
static void oobeNameTick(){
  if(!T.tap) return;
  int code;
  if(!hitKey(T.x, T.y, code)) return;
  int L = strlen(cfgName);
  if(code >= 32){ if(L < 20){ cfgName[L] = (char)code; cfgName[L + 1] = 0; drawNameField(); } }
  else if(code == -1){ if(L > 0){ cfgName[L - 1] = 0; drawNameField(); } }
  else if(code == -2){ if(L > 0 && L < 20){ cfgName[L] = ' '; cfgName[L + 1] = 0; drawNameField(); } }
  else if(code == -3){
    if(strlen(cfgName) == 0) strcpy(cfgName, "FlexOS Ultra");
    cfgSaveOobe();
    renderHome(); renderLock(); showLock();
    gState = ST_LOCK; lockOff = 0; lastLockOff = -1;
  }
}

// ---------------- Fechas localizadas ----------------
static void buildLongDate(char* out, size_t n){
  int li = LI();
  const char* wd = WD_FULL[li][rtcWd];
  const char* mo = MO_FULL[li][rtcMo - 1];
  switch(cfgLang){
    case 0: case 3: snprintf(out, n, "%s, %d de %s", wd, rtcD, mo); break; // ES, PT
    case 2: case 4: snprintf(out, n, "%s %d %s", wd, rtcD, mo); break;     // FR, IT
    default:        snprintf(out, n, "%s, %s %d", wd, mo, rtcD); break;    // EN / ZH
  }
}
static void buildShortDate(char* out, size_t n){
  int li = LI();
  snprintf(out, n, "%s, %d %s", WD_SHORT[li][rtcWd], rtcD, MO_SHORT[li][rtcMo - 1]);
}

// ---------------- LOCK ----------------
// Barra de gestos (estilo iOS): pildora fina centrada cerca del borde inferior.
// yBottom = borde inferior de referencia (normalmente SCR_H). col por defecto blanca.
static void drawHomeIndicator(int yBottom, uint8_t alpha, uint16_t col = rgb565(255,255,255)){
  int barW = 130, barH = 5, radius = 2;
  int x = (SCR_W - barW) / 2;
  int y = yBottom - 20 - barH;          // 20 px de margen desde el borde
  fillRoundRectA(x, y, barW, barH, radius, col, alpha);
}


// Glifos pequeÃ±os para las tarjetas de widgets del bloqueo. Copia minima e
// independiente de los de RI_* (Ajustes -> drawRowGlyph), que se definen
// mas abajo en el archivo; asi esta funcion no depende de nada definido
// despues de ella (evita sorpresas con el auto-prototipado de Arduino).
// kind: 0 = nube (clima), 1 = calendario, cualquier otro = campana (notif).
static void lockGlyph(int kind, int cx, int cy, uint16_t col){
  switch(kind){
    case 0:
      fillCircle(cx - 4, cy + 2, 5, col); fillCircle(cx + 4, cy + 2, 6, col);
      fillCircle(cx, cy - 2, 6, col); fillRect(cx - 8, cy + 2, 16, 5, col); break;
    case 1:
      drawRoundRect(cx - 9, cy - 8, 18, 17, 3, col); fillRect(cx - 9, cy - 8, 18, 5, col);
      fillCircle(cx - 4, cy + 2, 1, col); fillCircle(cx + 3, cy + 2, 1, col); break;
    default:   // campana (notificaciones) -- domo + cuerpo conico + reborde + badajo
      fillCircle(cx, cy - 6, 5, col);
      fillTriangle(cx - 8, cy + 4, cx + 8, cy + 4, cx, cy - 6, col);
      fillRect(cx - 9, cy + 3, 18, 3, col);
      fillCircle(cx, cy + 9, 2, col); break;
  }
}
// Tarjeta compacta para un widget opcional del bloqueo (clima/calendario/
// notificaciones). Estilo Vidrio u overlay plano segun uiGlass, igual que
// el resto de superficies de la app.
static void lockWidgetCard(int y, int kind, const char* title, const char* val, uint16_t accent){
  int x = 28, w = SCR_W - 56, h = 50;
  if(uiGlass){ drawLiquidGlassPanel(x, y, w, h, 16, rgb565(40,50,90)); }
  else fillRoundRectA(x, y, w, h, 16, rgb565(255,255,255), 45);
  lockGlyph(kind, x + 30, y + h / 2, accent);
  drawText(x + 54, y + 9, title, 2, rgb565(255,255,255));
  drawText(x + 54, y + 30, val, 1, rgb565(205,214,232));
}

static void renderLock(){
  drawWallpaper(lockBuf, false); setBuf(lockBuf);
  drawWifi(SCR_W - 66, 40, 12, rgb565(255,255,255));
  drawBattery(SCR_W - 46, 31, 30, 15, 82, rgb565(255,255,255));
  if(gLockWidgets & LW_CLOCK){
    if(uiGlass){ drawLiquidGlassPanel(28, 198, SCR_W - 56, 252, 28, rgb565(40,62,128)); }  // vidrio tras el reloj
    char cs[8]; clkStr12(cs, sizeof(cs));
    drawBigClock(cs, SCR_W / 2, 242, 140, 18, rgb565(255,255,255));
    char ds[64]; buildLongDate(ds, sizeof(ds));
    drawTextC(SCR_W / 2, 242 + 140 + 36, ds, 3, rgb565(255,255,255));
  }
  // widgets opcionales (clima/calendario/notificaciones), apilados debajo
  // del reloj -- o mas arriba si el reloj esta desactivado, para no dejar
  // media pantalla vacia. El panel del reloj (y=198,h=252) termina en
  // y=450 -- el primer widget tiene que empezar DESPUES de eso, con margen
  // (antes empezaba en 448 y se solapaba 2px con el panel: el corte raro
  // que se ve en la foto justo debajo de la fecha).
  int wy = (gLockWidgets & LW_CLOCK) ? 462 : 200;
  if(gLockWidgets & LW_WEATHER){
    lockWidgetCard(wy, 0, t(S_WEATHER), "21C, Lima, Peru", rgb565(90,170,235));  // mock: sin fuente de datos real aun
    wy += 60;
  }
  if(gLockWidgets & LW_CAL){
    lockWidgetCard(wy, 1, appName(IC_CALEND), t(S_NOEVENTS), rgb565(235,110,90));  // mock: sin eventos reales aun
    wy += 60;
  }
  if(gLockWidgets & LW_NOTIF){
    const char* val = gNotifCount > 0 ? gNotifs[gNotifCount - 1].mod.name : t(S_NONOTIFS);  // dato real
    lockWidgetCard(wy, 2, t(S_NOTIFS), val, rgb565(230,180,90));
    wy += 60;
  }
  fillRoundRect(SCR_W / 2 - 70, SCR_H - 150, 140, 10, 5, rgb565(255,255,255));
  drawTextC(SCR_W / 2, SCR_H - 118, t(S_SWIPE), 2, rgb565(255,255,255));
  setBuf(fb);
}
static void showLock(){ blitToFb(lockBuf); flxFlushAll(); }

// ---------------- HOME ----------------
// Widgets del escritorio (clima, noticias, dock) en estilo Liquid Glass
// o plano segun uiGlass.
// Calcula el rectangulo de cada widget del Home segun gWidgetWide. Mutuamente
// excluyente: si uno esta "ancho" ocupa toda la fila (mismo alto de siempre,
// 120px) y el otro no se dibuja ese frame -- asi la rejilla de apps de abajo
// (a altura fija, gy0=212) nunca se tiene que mover. Compartida entre
// drawHomeWidgets() (para pintar) y edTick()/edRender() (para el asa de
// resize en Modo Edicion), asi no hay dos copias de esta geometria.
static void widgetLayout(int &cx, int &cy, int &cw, int &ch, bool &climaVis,
                          int &nx, int &ny, int &nw, int &nh, bool &newsVis){
  cy = ny = 72; ch = nh = 120;
  if(gWidgetWide & WW_CLIMA){        cx = 24; cw = 432; climaVis = true;  newsVis = false; nx = ny = nw = nh = 0; }
  else if(gWidgetWide & WW_NOTICIAS){ nx = 24; nw = 432; newsVis = true;  climaVis = false; cx = cy = cw = ch = 0; }
  else { cx = 24; cw = 208; nx = 24 + 208 + 16; nw = 208; climaVis = newsVis = true; }
}
static void drawHomeWidgets(uint32_t tm){
  int wx, wy, cw, ch, nx, ny, nw, nh; bool climaVis, newsVis;
  widgetLayout(wx, wy, cw, ch, climaVis, nx, ny, nw, nh, newsVis);
  uint16_t W = rgb565(255,255,255);
  if(climaVis){
    if(uiGlass) drawLiquidGlassPanel(wx, wy, cw, ch, 20, rgb565(30,72,150));
    else fillRoundRect(wx, wy, cw, ch, 20, rgb565(28,58,120));
    drawText(wx + 16, wy + 16, t(S_WEATHER), 2, W);
    fillCircle(wx + cw - 42, wy + 34, 13, rgb565(250,205,60));
    fillCircle(wx + cw - 58, wy + 52, 11, rgb565(236,240,248));
    fillCircle(wx + cw - 42, wy + 55, 13, rgb565(236,240,248));
    fillRect(wx + cw - 58, wy + 53, 28, 10, rgb565(236,240,248));
    { int ex = drawText(wx + 16, wy + 48, "21", 4, W);
      drawCircle(ex + 7, wy + 52, 4, W); drawCircle(ex + 7, wy + 52, 3, W); }
    drawText(wx + 16, wy + ch - 22, "Lima, Peru", 1, rgb565(215,224,240));
  }
  if(newsVis){
    if(uiGlass) drawLiquidGlassPanel(nx, ny, nw, nh, 20, rgb565(205,212,226));
    else fillRoundRect(nx, ny, nw, nh, 20, rgb565(240,242,246));
    uint16_t ntxt = uiGlass ? W : rgb565(40,40,50), nsub = uiGlass ? rgb565(238,240,248) : rgb565(120,120,132);
    drawText(nx + 16, ny + 16, t(S_NEWS), 2, ntxt);
    drawTextC(nx + nw / 2, ny + nh / 2, t(S_NONEWS), 1, nsub);
  }
  int dkx = 24, dky = SCR_H - 176, dkw = SCR_W - 48, dkh = 96;
  if(uiGlass) drawLiquidGlassPanel(dkx, dky, dkw, dkh, 28, rgb565(180,186,206));
  else fillRoundRectA(dkx, dky, dkw, dkh, 28, W, 45);
  int dS = 64, inner = dkw - 32, dgap = (inner - 4 * dS) / 3;
  for(int i = 0; i < 4; i++){ int ix = dkx + 16 + i * (dS + dgap), iy = dky + (dkh - dS) / 2; drawAppIcon(12 + i, ix, iy, dS); }
}

static void renderHome(){
  drawWallpaper(homeBuf, true); setBuf(homeBuf);
  gHomeDirty = false;                      // homeBuf ya refleja los ajustes actuales
  qsDirty    = true;                       // la cortina se compone de homeBuf: invalidar su cache
  // barra de estado
  char cs[12]; clkStrBar(cs, sizeof(cs));
  drawText(20, 16, cs, 2, rgb565(255,255,255));
  char sd[48]; buildShortDate(sd, sizeof(sd));
  drawText(20, 40, sd, 1, rgb565(238,240,246));
  drawWifi(SCR_W - 66, 28, 11, rgb565(255,255,255));
  drawBattery(SCR_W - 46, 20, 30, 15, 82, rgb565(255,255,255));
  // widgets (clima, noticias) + dock: estilo Liquid Glass o plano
  drawHomeWidgets(millis());
  // rejilla de apps 4x3
  int S = 72, gx0 = 24, gy0 = 212, rowStep = 112;
  if(!editMode) for(int i = 0; i < 12; i++){                    // en Modo Edicion los pinta edRender()
    int c = i % 4, r = i / 4;
    int ix = gx0 + c * 120, iy = gy0 + r * rowStep;
    drawAppIcon(homeOrder[i], ix, iy, S);
    drawTextC(ix + S / 2, iy + S + 6, appName(homeOrder[i]), 2, rgb565(255,255,255));
  }
  // puntos de pagina
  int dotsY = gy0 + 2 * rowStep + S + 34;
  for(int i = 0; i < 3; i++)
    fillCircleA(SCR_W / 2 - 18 + i * 18, dotsY, 4, rgb565(255,255,255), i == 0 ? 255 : 110);
  // barra de navegacion: botones clasicos o barra de gestos (modo iOS)
  if(gNavMode == 0){
    int ny = SCR_H - 52; uint16_t nv = rgb565(255,255,255);
    int bx = SCR_W / 6;
    fillTriangle(bx - 10, ny + 8, bx + 8, ny - 2, bx + 8, ny + 18, nv);   // atras
    drawCircle(SCR_W / 2, ny + 8, 12, nv); drawCircle(SCR_W / 2, ny + 8, 11, nv); // inicio
    int rx = SCR_W * 5 / 6;
    drawRoundRect(rx - 11, ny - 3, 22, 22, 4, nv);                        // recientes
  } else {
    drawHomeIndicator(SCR_H, 220);                                        // barra de gestos
  }
  setBuf(fb);
}
static void showHome(){ blitToFb(homeBuf); flxFlushAll(); }
static bool hitHomeIcon(int px, int py, int &id){
  int S = 72, gx0 = 24, gy0 = 212, rowStep = 112;
  for(int i = 0; i < 12; i++){
    int c = i % 4, r = i / 4;
    int ix = gx0 + c * 120, iy = gy0 + r * rowStep;
    if(px >= ix - 6 && px <= ix + S + 6 && py >= iy && py <= iy + S + 16){ id = homeOrder[i]; return true; }
  }
  int dkx = 24, dky = SCR_H - 176, dkw = SCR_W - 48, dkh = 96, dS = 64, inner = dkw - 32, dgap = (inner - 4 * dS) / 3;
  for(int i = 0; i < 4; i++){
    int ix = dkx + 16 + i * (dS + dgap), iy = dky + (dkh - dS) / 2;
    if(px >= ix && px <= ix + dS && py >= iy && py <= iy + dS){ id = 12 + i; return true; }
  }
  return false;
}

// ---------------- Desbloqueo con fisica (composicion) ----------------
static void composeUnlock(int off){
  if(off < 0) off = 0; if(off > SCR_H) off = SCR_H;
  for(int y = 0; y < SCR_H; y++){
    if(y < SCR_H - off)
      memcpy(fb + (size_t)y * SCR_W, lockBuf + (size_t)(y + off) * SCR_W, SCR_W * 2);
    else
      memcpy(fb + (size_t)y * SCR_W, homeBuf + (size_t)y * SCR_W, SCR_W * 2);
  }
  flxFlushAll();
}
// Deslizamiento del bloqueo. Antes eran 14 pasos fijos con delay(14) en medio:
// 14 frames en ~200 ms pasara lo que pasara, con el procesador parado la mitad
// del tiempo. Ahora es la MISMA duracion pero basada en tiempo y sin delay, asi
// que el bucle mete todos los frames que el compositor sea capaz de dar. Mismo
// recorrido y mismo ease-out; solo cambia la cadencia.
#define UNLOCK_ANIM_MS 200
static void animateTo(int from, int to){
  uint32_t t0 = millis();
  for(;;){
    uint32_t e = millis() - t0; if(e > (uint32_t)UNLOCK_ANIM_MS) e = UNLOCK_ANIM_MS;
    float p = (float)e / (float)UNLOCK_ANIM_MS;
    p = 1 - (1 - p) * (1 - p);                               // ease-out
    composeUnlock(from + (int)((to - from) * p));
    if(e >= (uint32_t)UNLOCK_ANIM_MS) break;
  }
  composeUnlock(to);
}
// Arranca la verificacion DESDE la pantalla de Bloqueo.
// Si el bloqueo lo puso el despertar de una suspension y antes habia una app
// abierta, se pide la verificacion con destino "abrir esa app": al acertar el
// PIN se vuelve exactamente donde estaba el usuario, no al escritorio. Se
// reutiliza LSU_AFTER_OPENAPP tal cual, sin inventar una ruta nueva.
static void lockStartVerify(){
#if SUSPEND_ON && SUSPEND_LOCK_ON
  int ret = gSuspRetState, app = gSuspRetApp;
  gSuspRetState = -1; gSuspRetApp = -1;      // se consume: solo vale para este desbloqueo
  if(ret == ST_APP && app >= 0){
    lsuStartVerifyFor(LSU_AFTER_OPENAPP, app);
    gLockVerifyLocked = true;                // (va DESPUES: lsuStartVerify resetea estado)
    return;
  }
#endif
  lsuStartVerify();
  gLockVerifyLocked = true;
}
static void lockTick(){
  if(gLockType > 0){
    // Con bloqueo: deslizar arriba lleva DIRECTO a verificar (nunca se revela el escritorio)
    if(T.down && (T.startY - T.y) > 60){ lockStartVerify(); return; }
    if(T.released && T.swipeUp){ lockStartVerify(); return; }
    return;
  }
  if(T.down){
    int off = T.startY - T.y; if(off < 0) off = 0; if(off > SCR_H) off = SCR_H;
    if(off != lastLockOff){ composeUnlock(off); lastLockOff = off; }
    lockOff = off;
  } else if(T.released){
    if(lockOff > SCR_H / 3 || T.swipeUp){ animateTo(lockOff, SCR_H); enterHome(); }
    else { animateTo(lockOff, 0); lockOff = 0; lastLockOff = -1; showLock(); }
  }
}
// #############################################################
// ##  MODO EDICION del Home (long-press, jiggle, drag & drop)
// #############################################################
static float edCurX[12], edCurY[12];        // posiciones animadas (resorte)
static int   edDrag = -1, edHoverSlot = -1; // icono arrastrado / slot bajo el dedo
static float edDragX = 0, edDragY = 0;
// Posicion del icono arrastrado, acotada al area de rejilla. Antes este limite
// solo se aplicaba en los frames de MOVIMIENTO: en el frame del agarre se
// escribia T.x-36 en crudo y el icono podia dibujarse hasta 36 px fuera.
static void edSetDrag(int tx, int ty){
  float dx = (float)(tx - 36), dy = (float)(ty - 36);
  if(dx < 8) dx = 8;
  if(dx > SCR_W - 80) dx = SCR_W - 80;
  if(dy < 140) dy = 140;
  if(dy > 500) dy = 500;
  edDragX = dx; edDragY = dy;
}
static unsigned long edHoverMs = 0, edMs = 0;

static void homeOrderSave(){ prefs.begin("flexos", false); prefs.putBytes("hord", homeOrder, 12); prefs.end(); }
static void homeOrderLoad(){
  prefs.begin("flexos", true); size_t n = prefs.getBytes("hord", homeOrder, 12); prefs.end();
  if(n != 12){ for(int i = 0; i < 12; i++) homeOrder[i] = i; return; }
  bool seen[12] = { false };                 // valida: ids unicos 0..11 (por si prefs corruptas)
  for(int i = 0; i < 12; i++){ if(homeOrder[i] >= 12 || seen[homeOrder[i]]){ for(int j = 0; j < 12; j++) homeOrder[j] = j; return; } seen[homeOrder[i]] = true; }
}
static void edSlotXY(int slot, int &x, int &y){ int c = slot % 4, r = slot / 4; x = 24 + c * 120; y = 212 + r * 112; }
static int  edSlotAt(int px, int py){
  if(px < 24 || py < 212) return -1;
  int c = (px - 24) / 120, r = (py - 212) / 112;
  if(c < 0 || c > 3 || r < 0 || r > 2) return -1;
  int slot = r * 4 + c; return slot < 12 ? slot : -1;
}
static void edMove(int from, int to){        // reinserta el icono (desplaza los demas)
  if(from == to || from < 0 || to < 0 || from >= 12 || to >= 12) return;
  uint8_t v = homeOrder[from];
  if(from < to) for(int i = from; i < to; i++) homeOrder[i] = homeOrder[i + 1];
  else          for(int i = from; i > to; i--) homeOrder[i] = homeOrder[i - 1];
  homeOrder[to] = v;
}
// Asa de resize de los widgets del Home (Fase 1: alterna 2 tamanos --
// normal/ancho -- no arrastre continuo. Con solo 2 estados posibles, un
// toque en el asa es mas confiable que afinar un umbral de distancia de
// arrastre, y evita tocar la maquina de estados de edDrag/dwell de abajo).
static void drawWidgetHandle(int x, int y){
  fillRoundRect(x, y, 24, 24, 8, rgb565(255,255,255));
  strokeSegAA(x + 6, y + 16, x + 16, y + 6, 2.0f, rgb565(60,80,140));
  strokeSegAA(x + 6, y + 10, x + 10, y + 6, 2.0f, rgb565(60,80,140));
  strokeSegAA(x + 14, y + 18, x + 18, y + 14, 2.0f, rgb565(60,80,140));
}
static bool widgetHandleAt(int px, int py, int &which){
  int cx, cy, cw, ch, nx, ny, nw, nh; bool cv, nv;
  widgetLayout(cx, cy, cw, ch, cv, nx, ny, nw, nh, nv);
  if(cv && px >= cx + cw - 40 && px <= cx + cw - 4 && py >= cy + ch - 40 && py <= cy + ch - 4){ which = 0; return true; }
  if(nv && px >= nx + nw - 40 && px <= nx + nw - 4 && py >= ny + nh - 40 && py <= ny + nh - 4){ which = 1; return true; }
  return false;
}
static void widgetToggleSize(int which){
  if(which == 0) gWidgetWide = (gWidgetWide & WW_CLIMA)    ? 0 : WW_CLIMA;
  else           gWidgetWide = (gWidgetWide & WW_NOTICIAS) ? 0 : WW_NOTICIAS;
  cfgSavePrefs();
  renderHome();          // homeBuf tiene que reflejar el nuevo layout antes de que edRender() lo recomponga
}
static void edRender(){
  // Los iconos en Modo Edicion ahora usan el gIconStyle REAL (Vidrio si esta
  // activo en Ajustes) en vez de forzarse a Plano. Cada icono Vidrio pasa por
  // drawLiquidGlassPanel() -- un blur real, no gratis -- y aqui se dibujan
  // hasta 12 por frame. Para no trompicar el jiggle/arrastre, si el
  // estilo es Vidrio se limita el refresco de ESTA funcion a ~20 fps (50 ms).
  // Ojo: esto es un throttle LOCAL (reutiliza edMs, declarada mas arriba y
  // hasta ahora sin usar) -- a proposito NO se toca uiAnimMs, que es el
  // throttle compartido de qsPanel/ripple y no debe frenarse por esto.
  // En estilo Plano no hay throttle: se conserva el mismo refresco fluido de
  // siempre.
  if(gIconStyle == 1){
    unsigned long now = millis();
    if(now - edMs < 50) return;
    edMs = now;
  }
  setBuf(bbuf);
  for(int j = 120; j < 580; j++) memcpy(bbuf + (size_t)j * SCR_W, homeBuf + (size_t)j * SCR_W, SCR_W * 2);  // fondo (sin rejilla)
  uint32_t t = millis();
  { int cx, cy, cw, ch, nx, ny, nw, nh; bool cv, nv;                          // asas de resize de los widgets
    widgetLayout(cx, cy, cw, ch, cv, nx, ny, nw, nh, nv);
    if(cv) drawWidgetHandle(cx + cw - 30, cy + ch - 30);
    if(nv) drawWidgetHandle(nx + nw - 30, ny + nh - 30);
  }
  for(int i = 0; i < 12; i++){
    if(i == edDrag) continue;
    int tx, ty; edSlotXY(i, tx, ty);
    edCurX[i] += (tx - edCurX[i]) * 0.2f; edCurY[i] += (ty - edCurY[i]) * 0.2f;   // resorte
    float ph = i * 0.6f;
    int ox = (int)(2 * sinf(t * 0.02f + ph)), oy = (int)(2 * cosf(t * 0.017f + ph));  // temblor +-2px
    int s = 64, off = (72 - s) / 2;                                              // escala ~90%
    drawAppIcon(homeOrder[i], (int)edCurX[i] + off + ox, (int)edCurY[i] + off + oy, s);
  }
  if(edDrag >= 0){                                                              // icono arrastrado (translucido)
    int dx = (int)edDragX, dy = (int)edDragY, s = 72;
    if(uiGlass) drawLiquidGlassPanel(dx - 6, dy - 6, s + 12, s + 12, 16, rgb565(120,140,205));
    else fillRoundRectA(dx - 6, dy - 6, s + 12, s + 12, 16, rgb565(60,80,140), 150);
    drawAppIcon(homeOrder[edDrag], dx, dy, s);
  }
  drawTextC(SCR_W / 2, 176, "Arrastra los iconos - Inicio para salir", 1, rgb565(230,234,244));
  present(120, 580);
}
static void edEnter(){
  editMode = true;
  renderHome();                              // homeBuf sin rejilla (editMode salta el grid)
  for(int i = 0; i < 12; i++){ int x, y; edSlotXY(i, x, y); edCurX[i] = x; edCurY[i] = y; }
  edDrag = -1; edHoverSlot = -1;
}
static void edExit(){
  editMode = false; edDrag = -1;
  homeOrderSave();
  renderHome(); showHome();
}
static void edTick(){
  if(T.pressed){
    int which;
    if(widgetHandleAt(T.x, T.y, which)){ widgetToggleSize(which); edRender(); return; }
    edDrag = edSlotAt(T.x, T.y); edSetDrag(T.x, T.y); edHoverSlot = -1; edRender(); return;
  }
  if(T.down && edDrag >= 0){
    edSetDrag(T.x, T.y);
    int over = edSlotAt((int)edDragX + 36, (int)edDragY + 36);                  // slot bajo el centro
    if(over >= 0 && over != edDrag){
      if(over != edHoverSlot){ edHoverSlot = over; edHoverMs = millis(); }
      else if(millis() - edHoverMs > 400){ edMove(edDrag, over); edDrag = over; edHoverSlot = -1; }  // dwell 400ms
    } else edHoverSlot = -1;
    edRender(); return;
  }
  if(T.released){
    if(edDrag >= 0){ edDrag = -1; homeOrderSave(); edRender(); }                // soltar -> fija
    else if(T.tap) edExit();                                                    // toque en vacio/Inicio -> salir
    return;
  }
  // reposo: el jiggle continuo lo mueve uiTick()
}

// ---- Destello de reflejo al tocar un icono (estilo "Vidrio") ----
// Circulo blanco que crece y se desvanece (~0.5 s) desde el punto exacto
// donde se toco. Se activa en homeTick() (T.pressed sobre un icono) y se
// anima aqui; se llama desde uiTick() solo mientras gState==ST_HOME, asi
// que si se abre otra pantalla (enterApp) el destello deja de dibujarse
// de inmediato aunque el temporizador no haya terminado.
static bool     gRippleActive = false;
static int      gRippleX = 0, gRippleY = 0;
static uint32_t gRippleStart = 0;
static const uint32_t RIPPLE_DUR_MS = 500;
static const int      RIPPLE_MAX_R  = 70;
static void animateIconRipple(){
  if(gIconStyle != 1 || !bbuf || !homeBuf){ gRippleActive = false; return; }
  setBuf(bbuf);
  gClipX0 = 0; gClipX1 = SCR_W - 1; gClipY0 = 0; gClipY1 = SCR_H - 1;   // recorte completo
  // REPINTADO PARCIAL. El destello es un circulo de radio <= RIPPLE_MAX_R
  // centrado en el punto tocado, que NO se mueve durante la animacion. Antes se
  // recopiaba/volcaba la banda entera 64..726; ahora solo la franja vertical que
  // el circulo puede alcanzar (centro +- radio maximo, acotada a la pantalla).
  // El resto de fb ya es correcto. Salida byte-identica, mucho menos memcpy por
  // frame durante el ~medio segundo del efecto.
  int y0 = gRippleY - RIPPLE_MAX_R; if(y0 < 64)  y0 = 64;
  int y1 = gRippleY + RIPPLE_MAX_R; if(y1 > 726) y1 = 726;
  for(int j = y0; j <= y1; j++) memcpy(bbuf + (size_t)j * SCR_W, homeBuf + (size_t)j * SCR_W, SCR_W * 2);
  uint32_t e = millis() - gRippleStart;
  if(e < RIPPLE_DUR_MS){
    float   p = (float)e / RIPPLE_DUR_MS;                // 0..1
    int     r = (int)(RIPPLE_MAX_R * p);                  // crece
    uint8_t a = (uint8_t)(160 * (1.0f - p));              // se desvanece
    if(r > 0 && a > 0) fillCircleA(gRippleX, gRippleY, r, rgb565(255,255,255), a);
  } else {
    gRippleActive = false;                                // termino: este frame sale limpio (sin circulo)
  }
  present(y0, y1);
}
static void homeTick(){
  if(editMode){ edTick(); return; }
  if(gNavMode == 1 && handleiOSGestures()) return;   // gestos iOS antes que los toques normales
  // destello Liquid Glass al posar el dedo sobre un icono (solo estilo "Vidrio")
  if(T.pressed && gIconStyle == 1){
    int rid;
    if(hitHomeIcon(T.x, T.y, rid)){ gRippleActive = true; gRippleX = T.x; gRippleY = T.y; gRippleStart = millis(); }
  }
  // pulsacion larga (>1000 ms sin mover) sobre un icono de la rejilla.
  // FASE 2: ya no salta directo a Modo Edicion -- abre primero el menu
  // contextual. Con CTXMENU_ON en 0 se recupera exactamente el comportamiento
  // anterior (jiggle + agarre del icono bajo el dedo).
  if(T.down && edSlotAt(T.startX, T.startY) >= 0 && (millis() - T.downMs) > 1000
     && abs(T.x - T.startX) < 12 && abs(T.y - T.startY) < 12){
    int slot = edSlotAt(T.startX, T.startY);
    if(CTXMENU_ON){ ctxOpen(slot); return; }
    edEnter(); edDrag = slot; edSetDrag(T.x, T.y); return;
  }
  if(T.tap){
    if(T.x > SCR_W * 2 / 3 && T.y > SCR_H - 72){ activarMultitarea(); return; }   // boton Recientes
    int id;
    if(hitHomeIcon(T.x, T.y, id)){
      // FASE 3: si la app tiene candado, la verificacion va ANTES de abrirla.
      // Se reutiliza lsuStartVerify (misma UI, mismo contador de fallos, misma
      // espera progresiva de la Fase 1); al acertar, lsuFinishAfter abre la app
      // por el camino normal (enterApp).
      if(APPLOCK_ON && appLockGet(id) && gLockType > 0){ lsuStartVerifyFor(LSU_AFTER_OPENAPP, id); return; }
      enterApp(id);
    }
  }
}

// #############################################################
// ##  FRAMEWORK DE VENTANAS / APPS  (Milestone 2 - base)
// #############################################################
// Cada app expone dos callbacks: enter() dibuja su contenido inicial en
// el AREA DE VENTANA, y tick() (opcional) actualiza por frame. El marco
// (barra de estado + cabecera con "atras" + barra de navegacion) y los
// gestos de cierre los gestiona el framework: las apps solo pintan su
// contenido. Para rellenar una app, se reemplaza su entrada en APP_REG.

// Area de contenido. Embebida en DeX no hay barra de estado ni barra de
// navegacion que esquivar, asi que el contenido ocupa TODO el lienzo: es lo que
// elimina las franjas vacias de arriba y abajo dentro de la ventana.
// Son macros que se expanden en el punto de uso, y todos los usos quedan por
// debajo de donde se declaran gHosted y gAppH (justo aqui abajo, con los flags
// de APP_REG), asi que la expansion siempre las conoce.
#define WIN_TOP (gHosted ? 0 : 96)
#define WIN_BOT (gHosted ? gAppH : (SCR_H - 64))
#define WIN_BG  rgb565(18, 20, 28)  // fondo de ventana (oscuro, profesional)

typedef struct { void (*enter)(); void (*tick)(); uint8_t flags; } FlexApp;
#define APP_CUSTOM_HEADER 1   // la app pinta su propia cabecera (no la centrada)
#define APP_OWN_TOUCH     2   // la app gestiona TODOS sus toques (solo swipe-derecha cierra)
#define APP_LAND          4   // la app dibuja en LANDSCAPE (pone gLand por su cuenta)
// true mientras se re-ejecuta enter() SOLO para volver a maquetar tras un
// cambio de tamano. Una app cuyo enter() tambien inicializa estado (la
// Calculadora pone el display a "0", Ajustes reinicia scroll y seleccion) debe
// saltarse esa parte: al redimensionar se re-dibuja, no se reinicia.
static bool gRelayout = false;
#define APP_FLEX          8   // la app maqueta contra gAppW/gAppH -> se le da un
                              // lienzo del TAMANO REAL de la ventana y se dibuja
                              // 1:1, sin escalar ni barras de letterbox.
// Lienzo LOGICO de la app en curso. A pantalla completa es la pantalla entera;
// dentro de una ventana de DeX, para una app APP_FLEX, es el area de cliente.
// Las apps adaptativas maquetan contra esto en vez de contra SCR_W/SCR_H.
static int gAppW = SCR_W, gAppH = SCR_H;
// Hosting en Modo PC: una app corriendo dentro de una ventana de DeX NO puede
// navegar por el sistema (cerrarse a Home, abrir otra app a pantalla completa o
// saltar al selector) -- eso desmontaria el escritorio que la contiene. Cuando
// gHosted esta activo, esas tres salidas se capturan como una PETICION que DeX
// atiende luego a su manera: cerrar la ventana, abrir otra ventana, o abrir
// Recientes de DeX. Es el unico punto donde el sistema y el hosting se tocan.
static bool gHosted    = false;
static int  gHostReq   = 0;      // 0 nada Â· 1 cerrar ventana Â· 2 abrir app Â· 3 recientes
static int  gHostReqApp = -1;
static void settingsEnter(); static void settingsTick();   // Ajustes (M3), abajo
// Navegacion interna de Ajustes: devuelve true si "atras" tenia una pantalla
// de categoria que cerrar (y la cierra). Lo consulta appTick para que el boton
// atras del sistema no salte directo al escritorio desde una subpantalla.
static bool settingsHandleBack();
static void wifiSettingsEnter(); static void wifiTick();    // Ajustes -> Red e Internet -> Wi-Fi, abajo
static void calcEnter(); static void calcTick();           // Calculadora (M2), abajo
static void pcEnter(); static void pcTick();               // Modo PC (M4), abajo
static void galEnter();                                    // Galeria (M2)
static void bienEnter(); static void bienTick();           // Bienestar (M2)
static void calEnter(); static void calTick();             // Calendario (M2)
static void vidEnter(); static void vidTick();             // Multimedia (esqueleto)
static void camEnter(); static void camTick();             // Camara (esqueleto)
static void noteEnter(); static void noteTick();           // Notas + teclado 4 capas
static void almEnter(); static void eduEnter(); static void navEnter();  // apps simples
static void ideEnter(); static void ideTick(); static void paintEnter(); static void paintTick();
static void geoEnter(); static void geoTick();   // Juegos -> Geo Dash (clon de Geometry Dash)

// Rect del icono en el escritorio (para animar la apertura desde el)
static void getIconRect(int id, int &rx, int &ry, int &rs){
  if(id < 12){
    // La rejilla se dibuja por SLOT (homeOrder[slot] = id de app), no por id.
    // Antes esto calculaba la casilla con id%4 e id/4, o sea daba por hecho que
    // cada app sigue en su casilla original. En cuanto se reordenaban los iconos
    // en Modo Edicion, la animacion de apertura crecia desde donde ESTABA la app
    // antes: mover Notas al hueco de la Calculadora hacia que Notas se abriera
    // desde el sitio de la Calculadora. Hay que buscar en que slot esta hoy.
    int slot = id;
    for(int s = 0; s < 12; s++) if(homeOrder[s] == id){ slot = s; break; }
    int S = 72, gx0 = 24, gy0 = 212, rowStep = 112;
    rx = gx0 + (slot % 4) * 120; ry = gy0 + (slot / 4) * rowStep; rs = S;
  } else {
    int dkx = 24, dky = SCR_H - 176, dkw = SCR_W - 48, dkh = 96, dS = 64;
    int inner = dkw - 32, dgap = (inner - 4 * dS) / 3, i = id - 12;
    rx = dkx + 16 + i * (dS + dgap); ry = dky + (dkh - dS) / 2; rs = dS;
  }
}

// Marco estandar de ventana (barra de estado + cabecera + nav bar)
static void appDrawChrome(int id){
  if(gHosted){ (void)id; return; }   // embebida: la ventana ya tiene su barra de titulo
  setBuf(fb);
  uint16_t W = rgb565(255,255,255);
  char cs[12]; clkStrBar(cs, sizeof(cs));
  drawText(20, 16, cs, 2, W);
  drawWifi(SCR_W - 66, 28, 11, W);
  drawBattery(SCR_W - 46, 20, 30, 15, 82, W);
  if(gNavMode == 0){
    int ny = SCR_H - 52;
    fillTriangle(SCR_W / 6 - 10, ny + 8, SCR_W / 6 + 8, ny - 2, SCR_W / 6 + 8, ny + 18, W);
    drawCircle(SCR_W / 2, ny + 8, 12, W); drawCircle(SCR_W / 2, ny + 8, 11, W);
    drawRoundRect(SCR_W * 5 / 6 - 11, ny - 3, 22, 22, 4, W);
  } else {
    drawHomeIndicator(SCR_H, 180);
  }
  (void)id;
}
// Cabecera estandar (chevron "atras" + titulo centrado). Las apps con
// APP_CUSTOM_HEADER se saltan esto y pintan su propia cabecera.
static void appDrawHeader(int id){
  if(gHosted) return;                // el nombre de la app lo pone la barra de titulo
  uint16_t W = rgb565(255,255,255);
  int hy = 50;
  strokeSegAA(30, hy + 16, 18, hy + 8, 2.4f, W);
  strokeSegAA(18, hy + 8, 30, hy, 2.4f, W);
  drawTextC(SCR_W / 2, hy + 3, appName(id), 3, W);
}


// #############################################################
// ##  LAYOUT RESPONSIVO  Â·  toolkit compartido por las apps
// #############################################################
// Toda app APP_FLEX maqueta contra el LIENZO REAL: gAppW de ancho y
// [WIN_TOP..WIN_BOT] de alto. A pantalla completa eso es la pantalla menos su
// chrome; dentro de una ventana de DeX es EXACTAMENTE el area de cliente, y se
// dibuja 1:1. Ahi esta la diferencia con el camino antiguo: nada se dibuja a
// 480x800 para luego remuestrearlo al tamano de la ventana, que es lo que
// dejaba el texto emborronado ("como una imagen ampliada"). Al maquetar al
// tamano final, la fuente vectorial se rasteriza a su tamano real y sale
// nitida en cualquier ventana.

static void uiBox(int &x, int &y, int &w, int &h){
  x = 0; y = WIN_TOP; w = gAppW; h = WIN_BOT - WIN_TOP;
  if(w < 32) w = 32;
  if(h < 32) h = 32;
}
static inline int uiW(){ int x, y, w, h; uiBox(x, y, w, h); return w; }
static inline int uiH(){ int x, y, w, h; uiBox(x, y, w, h); return h; }
static inline int uiTop(){ int x, y, w, h; uiBox(x, y, w, h); return y; }
// Margen y separacion proporcionales, acotados para que no se coman la caja.
static int uiPad(){
  int w = uiW(), h = uiH(), m = (w < h ? w : h) / 24;
  if(m < 5) m = 5;
  if(m > 22) m = 22;
  return m;
}
static inline int uiGap(){ int g = uiPad() * 3 / 4; return g < 4 ? 4 : g; }
// Tamano de fuente mas grande que cabe en `maxw`, sin pasar de `maxSize`.
static int uiFontFit(const char* t, int maxw, int maxSize){
  int fs = maxSize; if(fs > 5) fs = 5; if(fs < 1) fs = 1;
  while(fs > 1 && textW(t, fs) > maxw) fs--;
  return fs;
}
// Tamano de fuente adecuado a una altura de linea.
static int uiFontH(int lineH){
  if(lineH >= 44) return 5;
  if(lineH >= 33) return 4;
  if(lineH >= 23) return 3;
  if(lineH >= 14) return 2;
  return 1;
}
// Altura aproximada de una linea de texto de tamano fs (para reservar sitio).
static inline int uiLineH(int fs){ return fs <= 1 ? 8 : fs * 9; }
// Titulo de seccion: se dibuja solo si cabe, y devuelve la Y siguiente.
static int uiTitle(int x, int y, int w, const char* t, uint16_t col, int maxSize){
  int fs = uiFontFit(t, w, maxSize);
  drawTextC(x + w / 2, y, t, fs, col);
  return y + uiLineH(fs) + uiGap() / 2;
}

// ---- Secciones opcionales con breakpoint y fundido ----
// Una seccion opcional aparece ENTERA cuando el lienzo da de si y desaparece
// ENTERA por debajo del umbral: nunca a medias ni interpolada de tamano. El
// umbral lo decide cada app segun lo que su seccion necesita para verse bien,
// no un numero arbitrario. Al cruzarlo en vivo (arrastrando el borde) se
// aplica un fundido corto para que no salte de golpe.
#define UI_FADE_MS 130
#define UI_SEC_MAX 12
struct UiSec { uint8_t app; uint8_t id; bool on; uint32_t t0; };
static UiSec  uiSecs[UI_SEC_MAX];
static uint8_t uiSecN = 0;
static bool    uiFading = false;      // hay algun fundido en curso (lo consulta DeX)

// Devuelve el alfa 0..255 con el que dibujar la seccion `id` de la app en
// curso. 0 = no dibujarla. `want` es el breakpoint ya evaluado por la app.
static uint8_t uiSection(uint8_t id, bool want){
  uint8_t app = (uint8_t)gAppId;
  UiSec* sc = NULL;
  for(uint8_t i = 0; i < uiSecN; i++) if(uiSecs[i].app == app && uiSecs[i].id == id){ sc = &uiSecs[i]; break; }
  if(!sc){
    if(uiSecN >= UI_SEC_MAX) return want ? 255 : 0;    // sin ranura: sin fundido, pero correcto
    sc = &uiSecs[uiSecN++];
    sc->app = app; sc->id = id; sc->on = want; sc->t0 = 0;
    return want ? 255 : 0;
  }
  if(want != sc->on){ sc->on = want; sc->t0 = millis(); }
  if(sc->t0 == 0) return sc->on ? 255 : 0;
  uint32_t e = millis() - sc->t0;
  if(e >= UI_FADE_MS){ sc->t0 = 0; return sc->on ? 255 : 0; }
  uiFading = true;
  uint32_t a = (uint32_t)255 * e / UI_FADE_MS;
  return (uint8_t)(sc->on ? a : 255 - a);
}
// Texto y relleno con alfa, para que las secciones opcionales puedan fundirse.
static inline void uiRectA(int x, int y, int w, int h, int r, uint16_t c, uint8_t a){
  if(a == 0) return;
  if(a >= 255) fillRoundRect(x, y, w, h, r, c);
  else fillRoundRectA(x, y, w, h, r, c, a);
}
static inline void uiText(int x, int y, const char* t, int fs, uint16_t c, uint8_t a){
  if(a == 0) return;
  drawTextA(x, y, t, fs, c, a);
}
static inline void uiTextC(int cx, int y, const char* t, int fs, uint16_t c, uint8_t a){
  if(a == 0) return;
  drawTextCA(cx, y, t, fs, c, a);
}
static inline void uiTextR(int rx, int y, const char* t, int fs, uint16_t c, uint8_t a){
  if(a == 0) return;
  drawTextA(rx - textW(t, fs), y, t, fs, c, a);
}

// ---- Contenido de apps ----
// (1) Placeholder para apps aun no implementadas (dentro de la ventana)
static void appPlaceholderEnter(){
  setBuf(fb);
  int cy = (WIN_TOP + WIN_BOT) / 2;
  if(uiGlass) drawLiquidGlassPanel(36, cy - 168, SCR_W - 72, 268, 26, rgb565(50,72,146));  // modal glass
  drawAppIcon(gAppId, SCR_W / 2 - 44, cy - 130, 88);
  drawTextC(SCR_W / 2, cy + 6, t(S_SOON), 3, rgb565(232,234,240));
  drawTextC(SCR_W / 2, cy + 48, t(S_M2), 2, rgb565(140,150,166));
}
// (2) App REAL de referencia: Reloj (prueba el patron completo)
// RELOJ Â· adaptativo.
//   Esencial   : reloj gigante, centrado, con el trazo escalado al lienzo.
//   Opcional 1 : fecha larga -- aparece cuando quedan >= 26 px bajo el reloj.
//   Opcional 2 : tarjetas de fecha corta / dia del ano -- aparecen cuando el
//                lienzo tiene >= 250 px de ancho Y >= 90 px libres debajo, que
//                es lo que necesitan para no quedar apretadas.
static void appRelojRender(){
  setBuf(fb);
  int bx, by, bw, bh; uiBox(bx, by, bw, bh);
  fillRect(bx, by, bw, bh, WIN_BG);
  int pad = uiPad();
  char cs[8]; clkStr12(cs, sizeof(cs));
  // El reloj se lleva ~40% del alto, y nunca mas ancho de lo que cabe.
  int capH = bh * 2 / 5;
  int maxByW = (bw - 2 * pad) * 10 / (int)(strlen(cs) * 7 + 2);
  if(capH > maxByW) capH = maxByW;
  if(capH > 150) capH = 150;
  if(capH < 22) capH = 22;
  int thick = capH / 8; if(thick < 3) thick = 3;
  int cy = by + pad + bh / 12;
  drawBigClock(cs, bx + bw / 2, cy, capH, thick, rgb565(255,255,255));
  int y = cy + capH + pad;
  int rest = (by + bh) - y - pad;
  char ds[64]; buildLongDate(ds, sizeof(ds));
  uint8_t aDate = uiSection(0, rest >= 26);
  if(aDate){
    int fs = uiFontFit(ds, bw - 2 * pad, uiFontH(rest / 3 > 30 ? 30 : rest));
    uiTextC(bx + bw / 2, y, ds, fs, rgb565(200,210,230), aDate);
    y += uiLineH(fs) + pad;
    rest = (by + bh) - y - pad;
  }
  // Panel opcional de tarjetas: solo con ancho y alto suficientes.
  uint8_t aCards = uiSection(1, bw >= 250 && rest >= 90);
  if(aCards){
    int n = 2, g = uiGap();
    int cw = (bw - 2 * pad - g) / n, chh = rest > 130 ? 130 : rest;
    char sd[40]; buildShortDate(sd, sizeof(sd));
    char doy[24]; snprintf(doy, sizeof(doy), "%s", g24h ? "24 h" : "12 h");
    const char* lbl[2] = { "Fecha", "Formato" };
    const char* val[2] = { sd, doy };
    for(int i = 0; i < n; i++){
      int x = bx + pad + i * (cw + g);
      uiRectA(x, y, cw, chh, uiPad(), rgb565(30,34,46), aCards);
      int fl = uiFontFit(lbl[i], cw - 16, 2);
      uiTextC(x + cw / 2, y + chh / 2 - uiLineH(fl) - 6, lbl[i], fl, rgb565(150,160,185), aCards);
      int fv = uiFontFit(val[i], cw - 16, 3);
      uiTextC(x + cw / 2, y + chh / 2 + 2, val[i], fv, rgb565(225,232,245), aCards);
    }
    y += chh + pad;
  }
  int fy = by + bh - pad - uiLineH(2);
  if(fy > y) drawTextC(bx + bw / 2, fy, "Reloj de FlexOS", uiFontFit("Reloj de FlexOS", bw - 2 * pad, 2), rgb565(120,132,152));
  flxFlush(WIN_TOP, WIN_BOT);
}
static void appRelojEnter(){ appRelojRender(); }
static void appRelojTick(){ if(gMinChanged) appRelojRender(); }

// ---- Registro de apps (indices = enum IC_*) ----
static FlexApp APP_REG[16] = {
  { appRelojEnter, appRelojTick, APP_FLEX },              // 0  Reloj  (REAL)
  { galEnter, NULL, APP_FLEX },                           // 1  Galeria (REAL, M2)
  { vidEnter, vidTick, APP_CUSTOM_HEADER | APP_OWN_TOUCH },  // 2  Multimedia (esqueleto)
  { almEnter, NULL, APP_FLEX },                           // 3  Almacenamiento (REAL)
  { pcEnter, pcTick, APP_CUSTOM_HEADER },                  // 4  Modo PC (REAL, M4) -- usa render landscape (gLand)
  { noteEnter, noteTick, APP_CUSTOM_HEADER | APP_OWN_TOUCH },// 5  Notas + teclado (REAL)
  { eduEnter, NULL, APP_FLEX },                           // 6  Educacion (REAL)
  { navEnter, NULL, APP_FLEX },                           // 7  Navegador (REAL)
  { ideEnter, ideTick, APP_FLEX },                        // 8  Code IDE (REAL + Asistente de Hardware)
  { bienEnter, bienTick, APP_FLEX },                      // 9  Bienestar (REAL, M2)
  { paintEnter, paintTick, APP_CUSTOM_HEADER | APP_OWN_TOUCH },// 10 Paint (REAL)
  { geoEnter, geoTick, APP_OWN_TOUCH | APP_CUSTOM_HEADER | APP_LAND }, // 11 Juegos (Geo Dash, REAL)
  { settingsEnter, settingsTick, APP_CUSTOM_HEADER },      // 12 Ajustes (REAL, M3) -- Wi-Fi/PIN cambian gState a pantalla completa
  { calcEnter, calcTick, APP_FLEX },               // 13 Calculadora (REAL, M2) -- app de referencia del modo embebido
  { calEnter, calTick, APP_FLEX },                        // 14 Calendario (REAL, M2)
  { camEnter, camTick, APP_CUSTOM_HEADER | APP_OWN_TOUCH },  // 15 Camara (esqueleto)
};

// Animacion de apertura/cierre: la ventana crece/encoge desde el icono
#define WIN_ANIM_MS 100                              // 0.1 s exactos, basado en tiempo
static void winRevealAnim(int id, bool opening){
  int ix, iy, is; getIconRect(id, ix, iy, is);
  uint16_t bg = (APP_REG[id].flags & APP_CUSTOM_HEADER) ? rgb565(244,247,251) : WIN_BG;
  uint32_t t0 = millis(), dur = WIN_ANIM_MS;
  // FLUIDEZ: antes cada frame recopiaba homeBuf ENTERO (768 KB de PSRAM leidos y
  // escritos, ~10 ms) y volcaba la pantalla completa, aunque la forma solo
  // ocupara una franja. A 0,2 s eso daba unos 20 frames; a 0,1 s habrian sido 10
  // y se veria a saltos. Ahora se recompone y se vuelca SOLO la union de la
  // franja del frame anterior y la de este -- lo unico que puede haber cambiado.
  // Los primeros frames del zoom son un rectangulo pequeno junto al icono y
  // cuestan casi nada, asi que el numero de frames sube mucho: la animacion es
  // mas corta Y mas suave a la vez. Al no haber delay(), el bucle va al maximo
  // que de el compositor.
  int prevY0 = 0, prevY1 = SCR_H - 1;
  bool first = true;
  for(;;){
    uint32_t e = millis() - t0; if(e > dur) e = dur;
    float tt = (float)e / dur;
    float p = opening ? tt : (1.0f - tt);
    p = 1 - (1 - p) * (1 - p) * (1 - p);              // ease-out cubico (mas suave)
    // Geometria de la forma de este frame y la franja vertical que ocupa.
    int x0 = 0, y0 = 0, x1 = SCR_W, y1 = SCR_H, rad = 0;
    if(gAnimStyle == 1){                               // fundido: cubre siempre toda la pantalla
      y0 = 0; y1 = SCR_H;
    } else if(gAnimStyle == 2){                        // deslizar: sube desde el borde inferior
      y0 = (int)(SCR_H * (1 - p)); y1 = SCR_H;
    } else {                                           // zoom: crece desde el icono
      x0  = (int)(ix * (1 - p));
      y0  = (int)(iy * (1 - p));
      x1  = (int)((ix + is) * (1 - p) + SCR_W * p);
      y1  = (int)((iy + is) * (1 - p) + SCR_H * p);
      rad = (int)(18 * (1 - p));
    }
    int by0 = y0 < prevY0 ? y0 : prevY0;
    int by1 = y1 > prevY1 ? y1 : prevY1;
    if(first){ by0 = 0; by1 = SCR_H - 1; first = false; }
    if(by0 < 0) by0 = 0; if(by1 > SCR_H - 1) by1 = SCR_H - 1;
    setBuf(bbuf);
    for(int j = by0; j <= by1; j++)                    // fondo: solo las filas que se tocan
      memcpy(bbuf + (size_t)j * SCR_W, homeBuf + (size_t)j * SCR_W, SCR_W * 2);
    if(gAnimStyle == 1)      fillRectA(0, 0, SCR_W, SCR_H, bg, (uint8_t)(255 * p));
    else if(gAnimStyle == 2) fillRect(0, y0, SCR_W, SCR_H - y0, bg);
    else                     fillRoundRect(x0, y0, x1 - x0, y1 - y0, rad, bg);
    present(by0, by1);                                 // vuelca de una vez (sin parpadeo)
    prevY0 = y0; prevY1 = y1;
    if(e >= dur) break;
  }
}

static void camStreamStop();   // definida con la app Camara (para el appClose de abajo)
static void appClose(){
  // La captura de la camara vive en su propia tarea; hay que pararla al salir
  // de la app o seguiria comiendo CPU del core 0 con el sistema en el Home.
  // Es incondicional a proposito: si la app que se cierra no era la Camara,
  // el flag ya estaba en false y esto no hace nada.
  camStreamStop();
  // FASE 4: un unico candado cierra TODAS las salidas de la app -- boton atras,
  // chevron de la cabecera, gesto rapido de la barra iOS, y cualquier app que
  // llame a appClose desde su propio tick. Poniendolo aqui no hay que ir
  // parcheando cada camino por separado (y ninguno nuevo se escapa).
  if(KIOSK_ON && kioskOn) return;
  if(gHosted){ gHostReq = 1; return; }        // dentro de una ventana: cierra la VENTANA
  // El FRAMEWORK devuelve el motor a portrait, no la app. Antes cada app
  // landscape tenia que acordarse de hacer gLand=false por su cuenta (pcExit,
  // gamesExitApp); si se salia por cualquier otra via -- gesto de la barra,
  // boton de atras, o una app que llamara a appClose desde dentro de su propio
  // tick -- gLand se quedaba en true y TODO lo que se pintara despues pasaba por
  // putPhys: el escritorio salia girado 90 y recortado (ly solo llega a 479, asi
  // que la mitad inferior desaparecia), y de ahi ya no se recuperaba sin
  // reiniciar. Resetear aqui cierra esa clase entera de fallo de una vez.
  bool wasLand = gLand;
  gLand = false;
  gClipY0 = 0; gClipY1 = SCR_H - 1;
  gClipX0 = 0; gClipX1 = SCR_W - 1;
  setBuf(fb);
  if(wasLand) swPushNoThumb(gAppId);   // miniatura girada: mejor ninguna (ver swPushNoThumb)
  else        swPushAndCapture(gAppId);
  // winRevealAnim compone la animacion SOBRE homeBuf: si Ajustes lo dejo sucio,
  // hay que recomponerlo ANTES, o la animacion de cierre encoge hacia el
  // escritorio viejo y este cambia de golpe al terminar.
  if(gHomeDirty) renderHome();
  winRevealAnim(gAppId, false);
  enterHome();
}
static void enterApp(int id){
  // FASE 4: en kiosco solo se puede estar en la app clavada. Esto bloquea que
  // una app abra otra a pantalla completa y deje kioskApp apuntando a otro sitio.
  if(KIOSK_ON && kioskOn && id != kioskApp) return;
  if(gHosted){ gHostReq = 2; gHostReqApp = id; return; }   // -> otra ventana de DeX
  gAppId = id; gState = ST_APP;
  winRevealAnim(id, true);                        // crece desde el icono
  if(!(APP_REG[id].flags & APP_CUSTOM_HEADER)){   // apps normales: marco blanco
    appDrawChrome(id);
    appDrawHeader(id);
  }
  if(APP_REG[id].enter) APP_REG[id].enter();      // la app pinta su contenido (y su marco si es custom)
  flxFlushAll();
}
static void appTick(){
  if(gLand){ if(APP_REG[gAppId].tick) APP_REG[gAppId].tick(); return; }  // Modo PC: gestiona todo por su cuenta
  if(gNavMode == 1 && handleiOSGestures()) return;   // gestos iOS: swipe-arriba -> Home/multitarea
  // Cierre universal: tocar "atras" (nav; y cabecera en apps normales). Gesto swipe-to-close eliminado.
  bool back = false;  // antes: T.swipeRight -> deshabilitado a peticion, ya no cierra la app
  if(T.tap){
    int ny = SCR_H - 52;
    if(!(APP_REG[gAppId].flags & APP_OWN_TOUCH) && T.y >= ny - 10 && T.y <= ny + 22 && T.x < SCR_W / 3) back = true;               // nav atras
    if(!(APP_REG[gAppId].flags & APP_CUSTOM_HEADER) && T.y <= WIN_TOP && T.x < 72) back = true; // chevron
  }
  if(back){
    // Ajustes tiene navegacion propia (lista -> categoria): "atras" cierra
    // primero la pantalla de categoria y solo cierra la app desde la lista.
    if(gAppId == IC_AJUSTES && settingsHandleBack()) return;
    appClose(); return;
  }
  if(APP_REG[gAppId].tick) APP_REG[gAppId].tick();
}

static void enterHome(){
  gState = ST_HOME; lockOff = 0; lastLockOff = -1;
  // Antes se volcaba homeBuf tal cual. Si venias de Ajustes de cambiar idioma,
  // formato de hora, Liquid Glass o estilo de iconos, homeBuf seguia siendo el
  // ANTERIOR y el escritorio contradecia al ajuste que acababas de tocar,
  // hasta que el reloj cambiaba de minuto y lo repintaba por su cuenta.
  if(gHomeDirty) renderHome();
  blitToFb(homeBuf); flxFlushAll();
}

// Gestos de la barra inferior (modo iOS). Solo actua si el toque EMPEZO en los
// ultimos ~44 px de la pantalla (la zona de la barra). Al soltar:
//   Â· deslizamiento hacia arriba rapido (<300 ms) -> Home
//   Â· deslizamiento hacia arriba mantenido (>=300 ms) -> App Switcher
// Devuelve true si consumio el gesto (para que el tick no siga procesando).
static bool handleiOSGestures(){
  if(gNavMode != 1) return false;
  // FASE 4: en kiosco los gestos de la barra (Home y switcher) se ignoran. Se
  // devuelve false, no true: asi appTick sigue llamando al tick de la app y esta
  // no se congela -- solo pierde la via de escape.
  if(KIOSK_ON && kioskOn) return false;
  if(T.released && T.startY > SCR_H - 44){
    int dy = T.startY - T.y;                    // positivo si el dedo subio
    unsigned long dur = millis() - T.downMs;
    if(dy > 30){
      if(dur >= 300)            activarMultitarea();  // mantener -> multitarea
      else if(gState == ST_APP) appClose();           // rapido en app -> Home (guarda miniatura)
      else                      enterHome();          // rapido en Home -> refresca
      return true;
    }
  }
  return false;
}

// #############################################################
// ##  APP AJUSTES  Â·  navegacion de telefono moderno
// ##  ------------------------------------------------------
// ##  Antes eran DOS paneles a la vez: barra lateral de categorias a la
// ##  izquierda y detalle a la derecha, con el detalle encajado en 232 px de
// ##  ancho. Ahora funciona como en Android/One UI/iOS:
// ##    Â· la pantalla principal es SOLO la lista de categorias, a todo lo ancho;
// ##    Â· tocar una categoria ABRE su propia pantalla, dedicada, tambien a todo
// ##      lo ancho (mas sitio para cada fila y para su valor);
// ##    Â· la transicion es un empuje horizontal con paralaje (la pantalla que
// ##      sale se va mas despacio que la que entra), interpolado por TIEMPO;
// ##    Â· para volver: el chevron de la cabecera, el boton "atras" de la barra
// ##      de navegacion o un arrastre desde el borde izquierdo.
// ##  COMPATIBILIDAD CON DeX: la app sigue siendo la MISMA -- mismo enter(),
// ##  mismo tick(), mismo lienzo 480x800, y la navegacion vive en una variable
// ##  propia (setView), no en gState. Por eso dentro de una ventana de Modo PC
// ##  se comporta exactamente igual que a pantalla completa. Lo unico que se
// ##  omite hospedada es la ANIMACION (un tick hospedado solo produce el ultimo
// ##  cuadro: animar ahi seria trabajo tirado), que es justo lo que hacen ya
// ##  las demas animaciones bloqueantes del sistema.
// #############################################################
#define SET_CARD_X   14
#define SET_CARD_W   (SCR_W - 28)           // tarjeta a todo el ancho (452)
#define DP_X         SET_CARD_X             // el contenido de detalle usa DP_X/DP_W
#define DP_W         SET_CARD_W
#define SET_LIST_TOP 104                    // primera tarjeta de la lista de categorias
#define SET_LIST_BOT (SCR_H - 70)
#define DLIST_TOP    128                    // primera fila de la pantalla de categoria
#define DLIST_BOT    (SCR_H - 70)           // 730
#define SET_NAV_MS   270                    // duracion de la transicion entre pantallas
#define SET_ANIM_Y0  40                     // banda que se desplaza (la barra de estado
#define SET_ANIM_Y1  (SCR_H - 60)           //  y la de navegacion se quedan quietas)
// PAGE_BG y la paleta de abajo dependen de gDark (Ajustes -> Pantalla ->
// Modo de apariencia). Colores de MARCA/acento (los puntitos de colores de
// cada fila, iconos como Wi-Fi o el reloj) se dejan igual en ambos modos a
// proposito -- es el fondo y el texto lo que define si algo "se ve" claro
// u oscuro, y es lo unico que cambia aqui.
#define PAGE_BG        (gDark ? rgb565(18,20,28)     : rgb565(244,247,251))   // fondo de pagina
#define SET_CARD_BG    (gDark ? rgb565(34,38,50)     : rgb565(255,255,255))   // fondo de tarjeta (fila/sidebar)
#define SET_CARD_GLASS (gDark ? rgb565(48,54,72)     : rgb565(246,248,252))   // tinte de vidrio de la tarjeta
#define SET_TXT_HI     (gDark ? rgb565(240,242,248)  : rgb565(20,22,30))      // texto principal
#define SET_TXT_LO     (gDark ? rgb565(160,166,182)  : rgb565(120,126,140))   // texto secundario / valor
#define SET_TXT_MUTE   (gDark ? rgb565(120,126,142)  : rgb565(140,146,160))   // texto de ayuda / pie
#define SET_CHEV       (gDark ? rgb565(110,116,132)  : rgb565(160,165,178))   // chevron
#define SET_SIDE_SUB   (gDark ? rgb565(150,156,172)  : rgb565(140,145,158))   // subtitulo de la barra lateral
#define SET_NAVPILL    (gDark ? rgb565(200,204,214)  : rgb565(60,64,74))      // pildora de gestos (modo iOS)

static const char* SET_CAT[12] = {
  "General","Pantalla","Sonido","Red e Internet","Dispositivos",
  "Personalizaci\xC3\xB3n","Seguridad","Bater\xC3\xAD" "a","Almacenamiento",
  "Desarrollador","Sistema","Acerca de" };
static const char* SET_SUB[12] = {
  "Idioma, fecha, hora","Brillo, fondo, tema","Volumen, tonos","WiFi, Bluetooth",
  "GPIO, perifericos","Temas, iconos","Bloqueo, permisos","Ahorro de energia",
  "Interna, SD","Opciones dev","Sistema, logs","Version, creditos" };
static const char* SET_DESC[12] = {
  "Configura las opciones basicas del sistema.","Brillo, fondo de pantalla y modo oscuro.",
  "Volumen, tonos y notificaciones.","Conexiones de red (offline por ahora).",
  "GPIO, modulos y perifericos.","Temas, iconos y estilo del sistema.",
  "Bloqueo, permisos y privacidad.","Estado de la bateria y ahorro de energia.",
  "Memoria interna y tarjeta SD.","Herramientas y diagnostico de desarrollo.",
  "Informacion del sistema y registros.","Version, hardware y creditos de FlexOS." };

// setView es TODA la maquina de navegacion de la app: 0 = lista de categorias,
// 1 = pantalla de una categoria. A proposito NO es un gState nuevo -- asi la
// navegacion interna sobrevive intacta dentro de una ventana de Modo PC, donde
// dexHostRun descarta cualquier cambio de gState que haga la app hospedada.
static int setView = 0;
static int setSel = 0, setScroll = 0, setContentH = 0;
static int setListScroll = 0, setListH = 0;    // scroll propio de la lista de categorias
static int  setDragY0 = 0, setDragS0 = 0;      // arrastre de la lista activa
static bool setDragging = false;
static bool setBackSwipe = false;              // arrastre desde el borde izquierdo (volver)

// Texto recortado por la derecha (evita que se salga del panel)
static int drawTextClip(int x, int y, const char* s, int size, uint16_t col, int maxRight){
  if(size <= 1){
    while(*s){
      if(x + 6 > maxRight) break;
      uint8_t b = (uint8_t)*s++; uint32_t cp;
      if(b < 0x80) cp = b;
      else if((b & 0xE0) == 0xC0){ uint8_t b1 = *s ? (uint8_t)*s++ : 0; cp = ((b & 0x1F) << 6) | (b1 & 0x3F); }
      else if((b & 0xF0) == 0xE0){ if(*s) s++; if(*s) s++; cp = 0x3F; }
      else cp = 0x3F;
      uint8_t base, acc; mapCP(cp, base, acc);
      drawGlyphSmooth(x, y, base, 1, col, 255);
      if(acc) drawAccent(x, y, 1, acc, col);
      x += 6;
    }
    return x;
  }
  float sc = fontSc(size), penx = x;
  while(*s){
    uint32_t cp = nextCP(&s);
    const FGlyph* g = &FG[fontIdx(cp)];
    if(penx + g->adv * sc > maxRight) break;
    drawGlyphScaled((int)(penx + g->bx * sc + 0.5f), y + (int)((g->topoff - FONT_CAPOFF) * sc + 0.5f), g, sc, col, 255);
    penx += g->adv * sc;
  }
  return (int)(penx + 0.5f);
}

static const char* langNameCur(){ return (cfgLang == 5) ? "Chinese" : LANG_ENDONYM[cfgLang]; }
static void settingsDateTimeStr(char* out, size_t n){
  int h12 = rtcH % 12; if(h12 == 0) h12 = 12;
  snprintf(out, n, "%02d/%02d/%04d  %d:%02d %s", rtcD, rtcMo, rtcY, h12, rtcMin, rtcH < 12 ? "AM" : "PM");
}
static void buildUptime(char* out, size_t n){
  unsigned long s = millis() / 1000UL;
  unsigned long d = s / 86400; s %= 86400;
  unsigned long h = s / 3600;  s %= 3600;
  unsigned long m = s / 60;
  if(d > 0)      snprintf(out, n, "%lud %luh %lum", d, h, m);
  else if(h > 0) snprintf(out, n, "%luh %lum", h, m);
  else           snprintf(out, n, "%lum", m);
}

// ---- iconos de fila (panel General) ----
enum { RI_GLOBE, RI_CAL, RI_CLOCK, RI_PIN, RI_REFRESH, RI_CLOUD, RI_RESET, RI_DOT };
static void drawRowGlyph(int k, int cx, int cy, uint16_t col){
  switch(k){
    case RI_GLOBE:
      drawCircle(cx, cy, 10, col); vLine(cx, cy - 10, 21, col); hLine(cx - 10, cy, 21, col);
      arcStroke(cx, cy, 5, 90, 270, 1, col); arcStroke(cx, cy, 5, -90, 90, 1, col); break;
    case RI_CAL:
      drawRoundRect(cx - 9, cy - 8, 18, 17, 3, col); fillRect(cx - 9, cy - 8, 18, 5, col);
      fillCircle(cx - 4, cy + 2, 1, col); fillCircle(cx + 3, cy + 2, 1, col); break;
    case RI_CLOCK:
      drawCircle(cx, cy, 10, col); strokeSegAA(cx, cy, cx, cy - 6, 1.4f, col);
      strokeSegAA(cx, cy, cx + 4, cy, 1.4f, col); break;
    case RI_PIN:
      fillCircle(cx, cy - 3, 7, col); fillTriangle(cx - 6, cy, cx + 6, cy, cx, cy + 9, col);
      fillCircle(cx, cy - 3, 3, rgb565(255,255,255)); break;
    case RI_REFRESH:
      arcStroke(cx, cy, 9, 30, 300, 2, col);
      fillTriangle(cx + 8, cy - 7, cx + 14, cy - 4, cx + 7, cy - 1, col); break;
    case RI_CLOUD:
      fillCircle(cx - 4, cy + 2, 5, col); fillCircle(cx + 4, cy + 2, 6, col);
      fillCircle(cx, cy - 2, 6, col); fillRect(cx - 8, cy + 2, 16, 5, col); break;
    case RI_RESET:
      arcStroke(cx, cy, 9, 40, 320, 2, col);
      fillTriangle(cx + 6, cy - 8, cx + 12, cy - 9, cx + 8, cy - 2, col); break;
    default: fillCircle(cx, cy, 4, col); break;
  }
}

// ---- iconos de categoria (barra lateral) ----
static void drawSetCatIcon(int cat, int x, int y, int S, uint16_t col){
  int cx = x + S / 2, cy = y + S / 2;
  switch(cat){
    case 0: // engranaje
      fillCircleAA(cx, cy, S * 0.18f, col);
      for(int k = 0; k < 8; k++){ float a = k * 0.7853982f;
        fillCircleAA(cx + cosf(a) * S * 0.30f, cy + sinf(a) * S * 0.30f, S * 0.065f, col); }
      fillCircleAA(cx, cy, S * 0.08f, rgb565(255,255,255)); break;
    case 1: // sol
      fillCircleAA(cx, cy, S * 0.15f, col);
      for(int k = 0; k < 8; k++){ float a = k * 0.7853982f;
        strokeSegAA(cx + cosf(a) * S * 0.24f, cy + sinf(a) * S * 0.24f,
                    cx + cosf(a) * S * 0.34f, cy + sinf(a) * S * 0.34f, 1.4f, col); } break;
    case 2: // altavoz
      fillRect((int)(cx - S * 0.22f), (int)(cy - S * 0.06f), (int)(S * 0.10f), (int)(S * 0.12f), col);
      fillTriangle((int)(cx - S * 0.12f), (int)(cy - S * 0.14f), (int)(cx - S * 0.12f), (int)(cy + S * 0.14f), (int)(cx + S * 0.02f), cy, col);
      arcStroke(cx - S * 0.02f, cy, S * 0.14f, -55, 55, 2, col); break;
    case 3: drawWifi(cx, (int)(cy + S * 0.14f), (int)(S * 0.28f), col); break;
    case 4: // cubo
      fillQuad(cx, (int)(cy - S * 0.22f), (int)(cx + S * 0.20f), (int)(cy - S * 0.10f), cx, (int)(cy + S * 0.02f), (int)(cx - S * 0.20f), (int)(cy - S * 0.10f), col);
      fillQuad(cx, (int)(cy + S * 0.02f), (int)(cx + S * 0.20f), (int)(cy - S * 0.10f), (int)(cx + S * 0.20f), (int)(cy + S * 0.14f), cx, (int)(cy + S * 0.26f), mix565(col, rgb565(0,0,0), 70));
      fillQuad(cx, (int)(cy + S * 0.02f), (int)(cx - S * 0.20f), (int)(cy - S * 0.10f), (int)(cx - S * 0.20f), (int)(cy + S * 0.14f), cx, (int)(cy + S * 0.26f), mix565(col, rgb565(0,0,0), 120)); break;
    case 5: // pincel
      strokeSegAA(cx - S * 0.16f, cy + S * 0.18f, cx + S * 0.10f, cy - S * 0.16f, 2.4f, col);
      fillCircleAA(cx - S * 0.18f, cy + S * 0.20f, S * 0.08f, col); break;
    case 6: // candado
      fillRoundRect((int)(cx - S * 0.16f), (int)(cy - S * 0.02f), (int)(S * 0.32f), (int)(S * 0.24f), 3, col);
      arcStroke(cx, cy - S * 0.02f, S * 0.12f, 180, 360, 2, col); break;
    case 7: drawBattery((int)(x + S * 0.24f), (int)(y + S * 0.34f), (int)(S * 0.5f), (int)(S * 0.3f), 80, col); break;
    case 8: // discos apilados
      for(int i = 0; i < 3; i++)
        fillRoundRect((int)(cx - S * 0.22f), (int)(cy - S * 0.16f + i * S * 0.14f), (int)(S * 0.44f), (int)(S * 0.09f), 2, col); break;
    case 9: // </>
      strokeSegAA(cx - S * 0.05f, cy - S * 0.14f, cx - S * 0.20f, cy, 2.0f, col);
      strokeSegAA(cx - S * 0.20f, cy, cx - S * 0.05f, cy + S * 0.14f, 2.0f, col);
      strokeSegAA(cx + S * 0.05f, cy - S * 0.14f, cx + S * 0.20f, cy, 2.0f, col);
      strokeSegAA(cx + S * 0.20f, cy, cx + S * 0.05f, cy + S * 0.14f, 2.0f, col); break;
    default: // info (i)
      drawCircle(cx, cy, (int)(S * 0.30f), col); drawCircle(cx, cy, (int)(S * 0.30f) - 1, col);
      fillCircle(cx, (int)(cy - S * 0.13f), 2, col);
      fillRect(cx - 1, (int)(cy - S * 0.03f), 3, (int)(S * 0.18f), col); break;
  }
}

// Registro de las filas realmente dibujadas en el panel de detalle de
// Ajustes (se resetea en settingsDetailContent() y lo llena cada
// setRowCard()). El tap-handler de settingsTick() lo consulta en vez de
// asumir que todas las filas miden 60px exactos y estan pegadas -- ese
// supuesto se rompia en cuanto habia un titulo de seccion o un texto de
// ayuda entre filas (bug: Pantalla->Bloqueo detectaba la fila equivocada).
#define SET_ROW_MAX 16
static int setRowY0[SET_ROW_MAX], setRowY1[SET_ROW_MAX], setRowN = 0;
// Tarjeta de fila (icono + titulo + valor + chevron). Devuelve la y siguiente.
// EL VIDRIO YA NO SE APAGA AL ARRASTRAR. Antes, mientras el dedo movia la lista,
// las tarjetas se pintaban planas porque drawLiquidGlassPanel (copiar + desenfocar
// + mezclar, por tarjeta y por cuadro) no daba para seguir al dedo: por eso "al
// hacer scroll se perdia el desenfoque y las transparencias". drawGlassCardFlat
// resuelve la tarjeta UNA vez y luego la vuelca por filas, asi que el material se
// mantiene durante todo el desplazamiento y el cuadro sigue siendo barato. Si el
// usuario tiene el Liquid Glass desactivado, la rama de abajo respeta su ajuste.
static int setRowCard(int y, int rIcon, uint16_t iCol, const char* title, const char* val, bool chevron){
  int rh = 64, mr = DP_X + DP_W - 30;
  if(setRowN < SET_ROW_MAX){ setRowY0[setRowN] = y; setRowY1[setRowN] = y + rh; setRowN++; }  // registra el rango real de esta fila
  if(uiGlass) drawGlassCardFlat(DP_X, y, DP_W, rh - 8, 14, SET_CARD_GLASS, PAGE_BG);          // tarjeta vidrio (cacheada)
  else fillRoundRect(DP_X, y, DP_W, rh - 8, 14, SET_CARD_BG);
  drawRowGlyph(rIcon, DP_X + 26, y + (rh - 8) / 2, iCol);
  drawTextClip(DP_X + 52, y + 10, title, 2, SET_TXT_HI, mr);
  if(val) drawTextClip(DP_X + 52, y + 34, val, 1, SET_TXT_LO, mr);
  if(chevron){ int chx = DP_X + DP_W - 20, chy = y + (rh - 8) / 2;
    strokeSegAA(chx - 3, chy - 6, chx + 3, chy, 2.0f, SET_CHEV);
    strokeSegAA(chx + 3, chy, chx - 3, chy + 6, 2.0f, SET_CHEV); }
  return y + rh;
}
static int drawInfoLine(int y, const char* label, const char* val){
  drawText(DP_X, y, label, 1, SET_TXT_LO);
  drawTextR(SCR_W - 12, y, val, 1, SET_TXT_HI);
  return y + 24;
}
static int drawDeviceInfo(int y){
  drawText(DP_X, y, "Dispositivo", 2, SET_TXT_HI); y += 30;
  char v[48];
  y = drawInfoLine(y, "Nombre", cfgName);
  y = drawInfoLine(y, "Modelo", "ESP32-S3 N16R8 CAM");
  y = drawInfoLine(y, "Version", "FlexOS Ultra 1.0");
  buildUptime(v, sizeof(v)); y = drawInfoLine(y, "Actividad", v);
  snprintf(v, sizeof(v), "%u KB libre", (unsigned)(esp_get_free_heap_size() / 1024)); y = drawInfoLine(y, "RAM", v);
  snprintf(v, sizeof(v), "%u / %u MB", (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1048576),
           (unsigned)(heap_caps_get_total_size(MALLOC_CAP_SPIRAM) / 1048576)); y = drawInfoLine(y, "PSRAM", v);
  y = drawInfoLine(y, "Flash", "16 MB");
  y = drawInfoLine(y, "CPU", "Xtensa LX7 dual 240 MHz");
  return y;
}

// Contenido del panel de detalle (con clip vertical activo)
static void settingsDetailContent(int cat){
  setRowN = 0;                       // reinicia el registro de filas (ver setRowCard)
  int base = DLIST_TOP - setScroll;
  int y = base + 6;
  char v[48];
  uint16_t ic = rgb565(70,120,225);
  if(cat == 0){
    y = setRowCard(y, RI_GLOBE,   rgb565(60,140,235), "Idioma", langNameCur(), true);
    settingsDateTimeStr(v, sizeof(v));
    y = setRowCard(y, RI_CAL,     rgb565(235,90,90),  "Fecha y hora", v, true);
    y = setRowCard(y, RI_CLOCK,   rgb565(90,120,230), "Formato de hora", g24h ? "24 horas" : "12 horas", true);
    y = setRowCard(y, RI_PIN,     rgb565(230,80,80),  "Zona horaria", "GMT-05:00 Lima", true);
    y = setRowCard(y, RI_REFRESH, rgb565(60,160,230), "Actualizaciones", flexOtaStatusText(), true);
    y = setRowCard(y, RI_CLOUD,   rgb565(120,160,230),"Copias de seguridad", "Proximamente", true);
    y = setRowCard(y, RI_RESET,   rgb565(220,80,80),  "Restablecer", "Opciones de fabrica", true);
    y += 12; y = drawDeviceInfo(y);
  } else if(cat == 11){
    y = drawDeviceInfo(y);
    y += 8; drawText(DP_X, y, "FlexOS Ultra - desde cero", 1, SET_TXT_MUTE); y += 22;
    drawText(DP_X, y, "para ESP32-S3 N16R8 - 2026", 1, SET_TXT_MUTE); y += 22;
  } else if(cat == 1){                     // Pantalla (funcional)
    char bv[16]; snprintf(bv, sizeof(bv), "%d%%", gBright);
    y = setRowCard(y, RI_DOT, rgb565(240,170,50), "Brillo", bv, true);
    y = setRowCard(y, RI_DOT, rgb565(90,110,235), "Estilo", uiGlass ? "Liquid Glass" : "Plano", true);
    y = setRowCard(y, RI_DOT, gDark ? rgb565(120,130,220) : rgb565(240,170,50), "Modo de apariencia", gDark ? "Oscuro" : "Claro", true);
    y = setRowCard(y, RI_DOT, rgb565(100,180,240), "Barra de navegacion", gNavMode == 0 ? "Botones" : "Gestos iOS", true);
    y += 8; drawText(DP_X, y, "Toca una fila para cambiarla", 1, SET_TXT_MUTE); y += 24;
    y += 8; drawText(DP_X, y, "Bloqueo", 2, SET_TXT_HI); y += 30;
    y = setRowCard(y, RI_CLOCK, rgb565(90,120,230), "Reloj grande", (gLockWidgets & LW_CLOCK) ? "Activado" : "Desactivado", true);
    y = setRowCard(y, RI_CLOUD, rgb565(90,170,235), "Clima", (gLockWidgets & LW_WEATHER) ? "Activado" : "Desactivado", true);
    y = setRowCard(y, RI_CAL, rgb565(235,110,90), "Calendario", (gLockWidgets & LW_CAL) ? "Activado" : "Desactivado", true);
    y = setRowCard(y, RI_DOT, rgb565(230,180,90), "Notificaciones", (gLockWidgets & LW_NOTIF) ? "Activado" : "Desactivado", true);
    y += 8; drawText(DP_X, y, "Elige los widgets de la pantalla de bloqueo", 1, SET_TXT_MUTE); y += 24;
    // PORT S3: recalibracion del panel tactil resistivo. Va en Pantalla porque
    // es donde el usuario busca "lo que tiene que ver con tocar la pantalla".
    y += 8; drawText(DP_X, y, "Panel t\xC3\xA1" "ctil", 2, SET_TXT_HI); y += 30;
    y = setRowCard(y, RI_DOT, rgb565(90,200,160), "Calibraci\xC3\xB3n t\xC3\xA1" "ctil", "Calibrar t\xC3\xA1" "ctil", true);
    y += 8; drawText(DP_X, y, "Hazlo si los toques no caen donde tocas", 1, SET_TXT_MUTE); y += 24;
  } else if(cat == 5){                      // Personalizacion (funcional)
    y = setRowCard(y, RI_DOT, rgb565(90,110,235), "Personalizar UI", uiGlass ? "Liquid Glass" : "Plano", true);
    y = setRowCard(y, RI_DOT, rgb565(150,90,210), "Iconos", gIconStyle == 1 ? "Vidrio" : "Plano", true);
    const char* av = gAnimStyle == 1 ? "Fundido" : gAnimStyle == 2 ? "Deslizar" : "Zoom";
    y = setRowCard(y, RI_DOT, rgb565(90,200,160), "Transiciones", av, true);
#if KB_SETTINGS_ON
    // FASE E: puerta de entrada a los Ajustes del teclado (la otra es el
    // engranaje de la barra superior del propio teclado).
    y = setRowCard(y, RI_DOT, rgb565(235,150,60), "Teclado",
                   gKbSize == KB_SIZE_COMPACT ? "Compacto" : gKbSize == KB_SIZE_BIG ? "Grande" : "Normal", true);
#endif
    y += 8; drawText(DP_X, y, "Toca una fila para cambiar su estilo", 1, SET_TXT_MUTE); y += 24;
  } else if(cat == 6){                      // Seguridad (funcional)
    const char* lt = gLockType == 1 ? "PIN configurado" : gLockType == 2 ? "Contrase\xC3\xB1" "a configurada" : "Deslizar";
    y = setRowCard(y, RI_DOT, rgb565(220,120,120), "Bloqueo", lt, true);
    y = setRowCard(y, RI_CLOCK, rgb565(120,150,235), "Bloqueo de inactividad", autoLockName(), true);
#if POWEROFF_ON && POWEROFF_PIN_ON
    // Apagado seguro: pide el PIN/contrasena antes de apagar del todo. NO afecta
    // a la suspension (doble-tap de 2 dedos), que nunca pide clave.
    y = setRowCard(y, RI_DOT, rgb565(220,120,120), "Apagado seguro",
                   gLockType == 0 ? "Configura antes un PIN" : (gPoffPin ? "Activado" : "Desactivado"), true);
#endif
    y += 8; drawText(DP_X, y, "Toca para configurar PIN o contrase\xC3\xB1" "a", 1, SET_TXT_MUTE); y += 24;
  } else {
    const char* rt[3] = {0,0,0}; const char* rv[3] = {0,0,0}; int rn = 0;
    switch(cat){
      case 2: rt[0]="Volumen";rv[0]="70%"; rt[1]="Tono";rv[1]="Predeterminado"; rn=2; break;
      case 3:
        rt[0]="Wi-Fi";
#if FLEXOS_ENABLE_WIFI
        rv[0] = gNetOnline ? "Conectado" : "Desactivado";
#else
        rv[0] = "No disponible";
#endif
        rt[1]="Bluetooth";rv[1]="No disponible"; rn=2; break;
      case 4: rt[0]="GPIO";rv[0]="Configurable"; rt[1]="Perifericos";rv[1]="Ninguno"; rn=2; break;
      case 6: rt[0]="Bloqueo";rv[0]="Deslizar"; rt[1]="PIN";rv[1]="No configurado"; rn=2; break;
      case 7: rt[0]="Nivel";rv[0]="82%"; rt[1]="Ahorro";rv[1]="Desactivado"; rn=2; break;
      case 8: rt[0]="Interna";rv[0]="6.2 / 16 GB"; rt[1]="Sistema";rv[1]="Disponible"; rn=2; break;
      case 9: rt[0]="Depuracion";rv[0]="En pantalla"; rt[1]="Banda reinicio";rv[1]="Solo crash"; rn=2; break;
      case 10: rt[0]="Version";rv[0]="FlexOS 1.0"; rt[1]="Logs";rv[1]="Puerto serie"; rn=2; break;
      default: rn=0; break;
    }
    for(int i = 0; i < rn; i++) y = setRowCard(y, RI_DOT, ic, rt[i], rv[i], true);
    y += 8; drawText(DP_X, y, "Mas opciones proximamente", 1, SET_TXT_MUTE); y += 24;
  }
  setContentH = (y - base) + 10;
}

// Acentos de cada categoria (los mismos de siempre, solo que ahora viven en la
// tarjeta de la lista principal en vez de en la barra lateral).
static const uint16_t SET_ACCENT[12] = {
  rgb565(70,120,235), rgb565(240,170,50), rgb565(70,120,235), rgb565(60,150,235),
  rgb565(80,180,120), rgb565(90,110,235), rgb565(90,95,110), rgb565(80,190,110),
  rgb565(150,90,210), rgb565(70,75,90), rgb565(70,120,235), rgb565(70,120,235) };

// ---- PANTALLA 1: lista de categorias (a todo el ancho, con scroll) ----
#define SET_CATCARD_H  66
#define SET_CATCARD_GAP 8
static int setCatY0[12], setCatY1[12];      // rango real de cada tarjeta (para el tap)
static void settingsDrawListHead(){
  drawText(16, 40, "Ajustes", 4, SET_TXT_HI);
}
static void settingsListContent(){
  int base = SET_LIST_TOP - setListScroll;
  for(int i = 0; i < 12; i++){
    int y = base + i * (SET_CATCARD_H + SET_CATCARD_GAP);
    setCatY0[i] = y; setCatY1[i] = y + SET_CATCARD_H;
    if(y > SET_LIST_BOT || y + SET_CATCARD_H < SET_LIST_TOP) continue;   // fuera de la ventana: ni se dibuja
    if(uiGlass) drawGlassCardFlat(SET_CARD_X, y, SET_CARD_W, SET_CATCARD_H, 16, SET_CARD_GLASS, PAGE_BG);
    else fillRoundRect(SET_CARD_X, y, SET_CARD_W, SET_CATCARD_H, 16, SET_CARD_BG);
    drawSetCatIcon(i, SET_CARD_X + 16, y + (SET_CATCARD_H - 32) / 2, 32, SET_ACCENT[i]);
    int tx = SET_CARD_X + 62, mr = SET_CARD_X + SET_CARD_W - 30;
    drawTextClip(tx, y + 14, SET_CAT[i], 2, SET_TXT_HI, mr);
    drawTextClip(tx, y + 38, SET_SUB[i], 1, SET_SIDE_SUB, mr);
    int chx = SET_CARD_X + SET_CARD_W - 20, chy = y + SET_CATCARD_H / 2;
    strokeSegAA(chx - 3, chy - 6, chx + 3, chy, 2.0f, SET_CHEV);
    strokeSegAA(chx + 3, chy, chx - 3, chy + 6, 2.0f, SET_CHEV);
  }
  setListH = 12 * (SET_CATCARD_H + SET_CATCARD_GAP) + 10;
}

// ---- PANTALLA 2: cabecera de la categoria abierta (con "atras") ----
#define SET_BACK_W 52                       // zona tactil del chevron de volver
static void settingsDrawDetailHead(){
  uint16_t c = SET_TXT_HI;
  strokeSegAA(30, 46, 18, 58, 2.6f, c);     // chevron "<" (centrado con el titulo)
  strokeSegAA(18, 58, 30, 70, 2.6f, c);
  drawTextClip(SET_BACK_W, 42, SET_CAT[setSel], 4, SET_TXT_HI, SCR_W - 12);
  drawTextClip(SET_BACK_W, 94, SET_DESC[setSel], 1, SET_TXT_LO, SCR_W - 12);
}
static void settingsDrawChromeDark(){
  if(gHosted) return;                // idem: hora, wifi, bateria y gestos son del UI principal
  uint16_t D = SET_TXT_HI;
  char cs[12]; clkStrBar(cs, sizeof(cs));
  drawText(16, 16, cs, 2, D);
  char sd[40]; buildShortDate(sd, sizeof(sd));
  drawText(16 + textW(cs, 2) + 14, 20, sd, 1, D);
  drawWifi(SCR_W - 66, 28, 11, D);
  drawBattery(SCR_W - 46, 20, 30, 15, 82, D);
  if(gNavMode == 0){
    int ny = SCR_H - 52;
    fillTriangle(SCR_W / 6 - 10, ny + 8, SCR_W / 6 + 8, ny - 2, SCR_W / 6 + 8, ny + 18, D);
    drawCircle(SCR_W / 2, ny + 8, 12, D); drawCircle(SCR_W / 2, ny + 8, 11, D);
    drawRoundRect(SCR_W * 5 / 6 - 11, ny - 3, 22, 22, 4, D);
  } else {
    drawHomeIndicator(SCR_H, 210, SET_NAVPILL);   // pildora de gestos, a juego con el fondo de la pagina
  }
}
// Ventana con scroll de la vista activa.
static inline int setBandTop(){ return setView == 0 ? SET_LIST_TOP : DLIST_TOP; }
static inline int setBandBot(){ return setView == 0 ? SET_LIST_BOT : DLIST_BOT; }
static inline int setMaxScroll(){
  int h = (setView == 0) ? setListH : setContentH;
  int m = h - (setBandBot() - setBandTop());
  return m > 0 ? m : 0;
}
// Pinta la PAGINA COMPLETA de una vista en el gBuf actual. Es la unica fuente de
// verdad del aspecto de la app: la usan el repintado normal y los dos lienzos de
// la transicion, asi que lo que se anima es exactamente lo que luego se ve.
static void settingsPaintPage(int view){
  fillRect(0, 0, SCR_W, SCR_H, PAGE_BG);
  settingsDrawChromeDark();
  int top, bot;
  if(view == 0){ settingsDrawListHead();   top = SET_LIST_TOP; bot = SET_LIST_BOT; }
  else         { settingsDrawDetailHead(); top = DLIST_TOP;    bot = DLIST_BOT;    }
  int c0 = gClipY0, c1 = gClipY1;
  gClipY0 = top; gClipY1 = bot - 1;
  if(view == 0) settingsListContent(); else settingsDetailContent(setSel);
  gClipY0 = c0; gClipY1 = c1;
}
// Repintado completo. Se compone en bbuf y se publica de una sola vez con
// present(): un cuadro entero o ninguno. Antes se dibujaba fila a fila DIRECTO
// sobre fb mientras el presenter podia estar subiendolo, que es de donde salian
// los parpadeos y las costuras al repintar.
static void settingsRender(){
  setBuf(bbuf);
  settingsPaintPage(setView);
  present(0, SCR_H - 1);
  setBuf(fb);
}
// Repintado de SOLO la banda con scroll (cada cuadro de un arrastre). Tambien
// via bbuf + present: el vidrio de las tarjetas se mantiene y no hay costuras.
static void settingsRenderBandOnly(){
  int top = setBandTop(), bot = setBandBot();
  setBuf(bbuf);
  fillRect(0, top, SCR_W, bot - top, PAGE_BG);
  int c0 = gClipY0, c1 = gClipY1;
  gClipY0 = top; gClipY1 = bot - 1;
  if(setView == 0) settingsListContent(); else settingsDetailContent(setSel);
  gClipY0 = c0; gClipY1 = c1;
  present(top, bot - 1);
  setBuf(fb);
}
// Nombre historico: lo siguen llamando las acciones de fila (settingsRowAction).
static void settingsRenderDetailOnly(){ settingsRenderBandOnly(); }

// ---- TRANSICION ENTRE PANTALLAS (empuje horizontal con paralaje) ----
// Dos lienzos de pagina en PSRAM. Se reservan la primera vez que se navega y se
// conservan: reservarlos y liberarlos en cada transicion solo fragmentaria el
// monton. Si no hay PSRAM, la navegacion sigue funcionando -- solo se pierde la
// animacion, nunca la funcion.
static uint16_t *setPgOut = NULL, *setPgIn = NULL;
static bool settingsPagesReady(){
  if(!setPgOut) setPgOut = (uint16_t*)heap_caps_malloc((size_t)SCR_W * SCR_H * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if(!setPgIn)  setPgIn  = (uint16_t*)heap_caps_malloc((size_t)SCR_W * SCR_H * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return setPgOut && setPgIn;
}
// Una fila desplazada 'off' px, recortada a la pantalla.
static inline void setRowShift(uint16_t* dst, const uint16_t* src, int off){
  int ds = off, ss = 0, n = SCR_W;
  if(ds < 0){ ss = -ds; n = SCR_W + ds; ds = 0; }
  else if(ds > 0) n = SCR_W - ds;
  if(n > 0) memcpy(dst + ds, src + ss, (size_t)n * 2);
}
// dir = +1 al ABRIR una categoria (la nueva entra desde la derecha),
// dir = -1 al VOLVER (la de detalle se va por la derecha y descubre la lista).
static void settingsAnimate(int fromView, int toView, int dir){
  // Hospedada en una ventana de Modo PC no se anima: de un tick hospedado solo
  // se ve el ultimo cuadro. El cambio de pantalla es instantaneo, como antes.
  if(gHosted || !settingsPagesReady()){ setView = toView; settingsRender(); return; }
  int keep = setView;
  setView = fromView; setBuf(setPgOut); settingsPaintPage(fromView);
  setView = toView;   setBuf(setPgIn);  settingsPaintPage(toView);
  setView = keep;
  setBuf(fb);
  const int y0 = SET_ANIM_Y0, y1 = SET_ANIM_Y1 - 1;
  const int PARA = (SCR_W * 28) / 100;               // paralaje: la que sale recorre un 28%
  int lastO = 0x7FFF, lastI = 0x7FFF;                // ultimo par de desplazamientos ya pintado
  uint32_t t0 = millis();
  for(;;){
    uint32_t e = millis() - t0; if(e > (uint32_t)SET_NAV_MS) e = SET_NAV_MS;
    float p = (float)e / (float)SET_NAV_MS;
    float ip = 1.0f - p;
    p = (p < 0.5f) ? (4.0f * p * p * p) : (1.0f - 4.0f * ip * ip * ip);   // ease-in-out cubica
    int oOff, iOff;
    if(dir > 0){ oOff = (int)(-PARA * p);            // sale hacia la izquierda, despacio
                 iOff = (int)(SCR_W * (1.0f - p)); }  // entra desde la derecha
    else       { oOff = (int)(SCR_W * p);            // sale hacia la derecha
                 iOff = (int)(-PARA * (1.0f - p)); }  // la de debajo vuelve a su sitio
    if(oOff == lastO && iOff == lastI){          // el reloj aun no ha movido nada
      if(e < (uint32_t)SET_NAV_MS){ delay(1); continue; }
      break;
    }
    lastO = oOff; lastI = iOff;
    for(int j = y0; j <= y1; j++){
      uint16_t* d = bbuf + (size_t)j * SCR_W;
      // Orden de pintado = orden de profundidad. Al abrir, la nueva va ENCIMA;
      // al volver, la que se va es la de encima y descubre a la de abajo.
      if(dir > 0){ setRowShift(d, setPgOut + (size_t)j * SCR_W, oOff);
                   setRowShift(d, setPgIn  + (size_t)j * SCR_W, iOff); }
      else       { setRowShift(d, setPgIn  + (size_t)j * SCR_W, iOff);
                   setRowShift(d, setPgOut + (size_t)j * SCR_W, oOff); }
      // Sombra de 8 px en el borde de la pagina de encima: da profundidad y
      // tapa la costura entre las dos capas.
      int edge = (dir > 0) ? iOff : oOff;
      if(edge > 0 && edge <= SCR_W){
        for(int k = 1; k <= 8; k++){
          int x = edge - k; if(x < 0) break;
          d[x] = mix565(d[x], rgb565(0,0,0), (uint8_t)(70 - k * 8));
        }
      }
    }
    present(y0, y1);
    if(e >= (uint32_t)SET_NAV_MS) break;
  }
  setView = toView;
  settingsRender();                                   // estado final limpio, sin restos del paralaje
}
static void settingsOpenCat(int cat){
  if(cat < 0 || cat > 11) return;
  setSel = cat; setScroll = 0; setDragging = false;
  settingsAnimate(0, 1, +1);
}
// Volver a la lista. Devuelve false si ya estabamos en la lista: asi el boton
// "atras" del sistema cierra la app solo cuando no queda pantalla que cerrar
// (igual que en Android).
static bool settingsHandleBack(){
  if(setView != 1) return false;
  setDragging = false; setBackSwipe = false;
  settingsAnimate(1, 0, -1);
  return true;
}
static void settingsEnter(){
  setView = 0; setSel = 0; setScroll = 0; setListScroll = 0;
  setDragging = false; setBackSwipe = false;
  settingsRender();
}
// Accion al tocar una fila del panel de detalle (ajustes funcionales)
// Todo ajuste que cambie el ASPECTO del escritorio marca gHomeDirty: homeBuf es
// una cache ya pintada y hay que recomponerla antes de volver a mostrarla.
// (gBright no lo hace: es PWM del backlight, no repinta nada.)
static void settingsRowAction(int cat, int idx){
  if(cat == 0){
    if(idx == 0){ cfgLang = (cfgLang + 1) % 6; cfgSavePrefs(); gHomeDirty = true; settingsRender(); }         // idioma (cicla)
    else if(idx == 2){ g24h = !g24h; cfgSavePrefs(); gHomeDirty = true; settingsRenderDetailOnly(); }         // formato 12/24
    else if(idx == 4) flexOtaOpenSettings();                                                                 // Actualizaciones -> pantalla OTA
  } else if(cat == 1){
    if(idx == 0){ gBright += 25; if(gBright > 100) gBright = 25; setBacklight(gBright); cfgSavePrefs(); settingsRenderDetailOnly(); }  // brillo real
    else if(idx == 1){ uiGlass = !uiGlass; cfgSavePrefs(); gHomeDirty = true; settingsRenderDetailOnly(); }    // estilo Liquid Glass
    else if(idx == 2){ gDark = !gDark; cfgSavePrefs(); settingsRender(); }  // Modo de apariencia: oscuro <-> claro (aplica ya, sin reiniciar)
    else if(idx == 3){ gNavMode = (gNavMode == 0) ? 1 : 0; cfgSavePrefs(); gHomeDirty = true; settingsRender(); } // barra: botones <-> gestos (redibuja tambien la barra inferior)
    else if(idx == 4){ gLockWidgets ^= LW_CLOCK;   cfgSavePrefs(); settingsRenderDetailOnly(); }  // Bloqueo: reloj grande
    else if(idx == 5){ gLockWidgets ^= LW_WEATHER; cfgSavePrefs(); settingsRenderDetailOnly(); }  // Bloqueo: clima
    else if(idx == 6){ gLockWidgets ^= LW_CAL;     cfgSavePrefs(); settingsRenderDetailOnly(); }  // Bloqueo: calendario
    else if(idx == 7){ gLockWidgets ^= LW_NOTIF;   cfgSavePrefs(); settingsRenderDetailOnly(); }  // Bloqueo: notificaciones
    else if(idx == 8) tcalEnter(true);   // PORT S3: calibracion tactil (vuelve aqui al terminar)
  } else if(cat == 5){
    if(idx == 0){ uiGlass = !uiGlass; cfgSavePrefs(); gHomeDirty = true; settingsRenderDetailOnly(); }         // Personalizar UI
    else if(idx == 1){ gIconStyle = (gIconStyle == 0) ? 1 : 0; cfgSavePrefs(); gHomeDirty = true; settingsRenderDetailOnly(); }  // estilo de iconos: Plano <-> Vidrio
    else if(idx == 2){ gAnimStyle = (gAnimStyle + 1) % 3; cfgSavePrefs(); settingsRenderDetailOnly(); }  // transiciones: zoom -> fundido -> deslizar -> zoom
#if KB_SETTINGS_ON
    else if(idx == 3) kbsEnter();                                                                       // FASE E: Ajustes del teclado
#endif
  } else if(cat == 3){
    if(idx == 0) wifiSettingsEnter();                                                       // Red e Internet -> Wi-Fi
  } else if(cat == 6){
    if(idx == 0) lsuEnter();                                                                 // Seguridad -> Bloqueo (PIN/ContraseÃ±a)
    else if(idx == 1){                                                                       // Seguridad -> Bloqueo de inactividad
      // Cicla 30 s -> 1 min -> 5 min -> 10 min -> 30 min -> Nunca -> 30 s, igual
      // que hacen las demas filas de opcion de Ajustes (Transiciones, Iconos...).
      gAutoLockMs = AUTOLOCK_OPTS[(autoLockIdx() + 1) % AUTOLOCK_NOPT];
      gLastTouchMs = millis();                    // el temporizador nuevo cuenta desde ahora
      cfgSavePrefs();
      settingsRenderDetailOnly();
    }
#if POWEROFF_ON && POWEROFF_PIN_ON
    else if(idx == 2){                                                                   // Seguridad -> Apagado seguro
      // Sin PIN/contrasena configurada no hay nada que pedir: activarlo seria una
      // proteccion de mentira. Se deja tal cual y la fila ya avisa ("Configura
      // antes un PIN").
      if(gLockType == 0) return;
      gPoffPin = !gPoffPin;
      cfgSavePrefs();
      settingsRenderDetailOnly();
    }
#endif
  }
}
static void settingsTick(){
  int top = setBandTop(), bot = setBandBot(), maxS = setMaxScroll();

  // ---- Volver: chevron de la cabecera o arrastre desde el borde izquierdo ----
  if(setView == 1){
    if(T.tap && T.x < SET_BACK_W && T.y >= 20 && T.y <= 84){ settingsHandleBack(); return; }
    // Gesto de retroceso: empieza pegado al borde izquierdo y se lleva el dedo a
    // la derecha. Se decide al soltar (no a media pulsacion) para que un scroll
    // que empiece cerca del borde no lo dispare por accidente.
    if(T.pressed && T.startX < 28) setBackSwipe = true;
    if(setBackSwipe && T.released){
      setBackSwipe = false;
      if((T.x - T.startX) > 70 && abs(T.y - T.startY) < 90){ settingsHandleBack(); return; }
    }
    if(!T.down) setBackSwipe = false;
  }

  // ---- Tap ----
  if(T.tap && !setDragging && T.y >= top && T.y <= bot){
    if(setView == 0){
      for(int i = 0; i < 12; i++)
        if(T.y >= setCatY0[i] && T.y < setCatY1[i]){ settingsOpenCat(i); return; }
    } else {
      for(int i = 0; i < setRowN; i++)
        if(T.y >= setRowY0[i] && T.y < setRowY1[i]){ settingsRowAction(setSel, i); return; }
    }
  }

  // ---- Scroll de la vista activa ----
  // Arrastre real: el contenido va pegado al dedo cuadro a cuadro, con umbral de
  // 6 px para no confundir un toque con un arrastre. Ahora vale para las DOS
  // pantallas (la lista de categorias tambien se desplaza) y ya no apaga el
  // vidrio de las tarjetas mientras dura.
  int* scroll = (setView == 0) ? &setListScroll : &setScroll;
  if(T.pressed && T.y >= top - 24 && T.y <= bot){ setDragY0 = T.y; setDragS0 = *scroll; setDragging = false; }
  if(T.down && maxS > 0 && T.startY >= top - 24 && T.startY <= bot){
    int dy = setDragY0 - T.y;
    if(!setDragging && abs(dy) > 6) setDragging = true;
    if(setDragging){
      int ns = setDragS0 + dy;
      if(ns < 0) ns = 0; if(ns > maxS) ns = maxS;
      if(ns != *scroll){ *scroll = ns; settingsRenderBandOnly(); }
      return;
    }
  }
  if(T.released && setDragging){ setDragging = false; return; }
}

// #############################################################
// ##  APP CALCULADORA  (Milestone 2)  Â·  app normal (marco estandar)
// #############################################################
static char   calcDisp[24] = "0";
static double calcAcc = 0;
static char   calcOp = 0;          // 0,'+','-','x','/'
static bool   calcFresh = true;    // el proximo digito empieza entrada nueva

static const char* CALC_LBL[5][4] = {
  {"C","+/-","%","/"}, {"7","8","9","x"}, {"4","5","6","-"},
  {"1","2","3","+"},   {"0",".","=","DEL"} };

static bool calcErr = false;       // el display muestra "Error" (division por cero / desbordamiento)

// Finito sin depender de macros de math.h (a prueba de .ino):
// NaN falla (v == v); los infinitos fallan el rango.
static inline bool calcFinite(double v){ return (v == v) && (v > -1.0e308) && (v < 1.0e308); }

static void calcFmt(double v){
  if(!calcFinite(v)){ snprintf(calcDisp, sizeof(calcDisp), "Error"); calcErr = true; return; }
  calcErr = false;
  if(v == 0) v = 0;                 // evita "-0"
  snprintf(calcDisp, sizeof(calcDisp), "%g", v);
}
static double calcCompute(double a, double b, char op){
  // Antes 5/0 devolvia 0 en silencio: una respuesta falsa presentada como buena.
  // Ahora se propaga un NaN y calcFmt lo convierte en "Error".
  switch(op){ case '+': return a + b; case '-': return a - b;
              case 'x': return a * b; case '/': return (b != 0) ? (a / b) : (double)NAN; }
  return b;
}
static void calcKey(char k){
  // Con "Error" en pantalla, el estado numerico no vale: cualquier entrada de
  // numero limpia primero. Los operadores se ignoran (no hay operando valido).
  if(calcErr){
    if(k == 'c' || (k >= '0' && k <= '9') || k == '.'){
      strcpy(calcDisp, "0"); calcAcc = 0; calcOp = 0; calcFresh = true; calcErr = false;
      if(k == 'c') return;
    } else return;
  }
  int L = strlen(calcDisp);
  if(k >= '0' && k <= '9'){
    if(calcFresh || (L == 1 && calcDisp[0] == '0')){ calcDisp[0] = k; calcDisp[1] = 0; }
    else if(L < 16){ calcDisp[L] = k; calcDisp[L + 1] = 0; }
    calcFresh = false;
  } else if(k == '.'){
    if(calcFresh){ strcpy(calcDisp, "0."); calcFresh = false; }
    else if(!strchr(calcDisp, '.') && L < 15){ calcDisp[L] = '.'; calcDisp[L + 1] = 0; }
  } else if(k == 'c'){ strcpy(calcDisp, "0"); calcAcc = 0; calcOp = 0; calcFresh = true; }
  else if(k == '\b'){ if(!calcFresh && L > 0){ calcDisp[L - 1] = 0; if(calcDisp[0] == 0) strcpy(calcDisp, "0"); } }
  else if(k == 'n'){
    if(strcmp(calcDisp, "0") != 0){
      if(calcDisp[0] == '-') memmove(calcDisp, calcDisp + 1, strlen(calcDisp));
      else { memmove(calcDisp + 1, calcDisp, strlen(calcDisp) + 1); calcDisp[0] = '-'; }
    }
  } else if(k == '%'){ calcFmt(atof(calcDisp) / 100.0); calcFresh = true; }
  else if(k == '+' || k == '-' || k == 'x' || k == '/'){
    double cur = atof(calcDisp);
    calcAcc = (calcOp && !calcFresh) ? calcCompute(calcAcc, cur, calcOp) : cur;
    calcOp = k; calcFresh = true; calcFmt(calcAcc);
  } else if(k == '='){
    if(calcOp){ calcAcc = calcCompute(calcAcc, atof(calcDisp), calcOp); calcFmt(calcAcc); calcOp = 0; }
    calcFresh = true;
  }
}
static void calcKeyFromLabel(const char* t){
  char k;
  if(!strcmp(t, "C")) k = 'c';
  else if(!strcmp(t, "+/-")) k = 'n';
  else if(!strcmp(t, "DEL")) k = '\b';
  else k = t[0];   // digitos, '.', '%', '/', 'x', '-', '+', '='
  calcKey(k);
}
// ---- Layout ADAPTATIVO (app de referencia del modo embebido) ----
// Nada de constantes de pantalla completa: todo sale del lienzo logico
// (gAppW x [WIN_TOP..WIN_BOT]), que a pantalla completa es la pantalla menos su
// chrome y dentro de una ventana de DeX es el area de cliente. La misma
// funcion sirve para las dos situaciones y para cualquier tamano intermedio.
// CALCULADORA Â· adaptativa (app de referencia).
//   Esencial   : rejilla 5x4 de teclas -- tiene PRIORIDAD sobre el display.
//   Opcional 1 : display -- cede alto a la rejilla y llega a omitirse si el
//                lienzo no da para las 5 filas.
//   Opcional 2 : panel lateral de memoria e historial -- aparece cuando el
//                ancho da para la rejilla con teclas de >= 44 px MAS 150 px de
//                panel. Por debajo de ese umbral desaparece entero, nunca
//                encogido a medias.
#define CALC_SIDE_MIN 150
#define CALC_KEY_MIN   44
// Ancho que reserva el panel lateral (0 si no toca mostrarlo).
static int calcSideW(){
  int w = gAppW, pad = w / 30; if(pad < 4) pad = 4; if(pad > 16) pad = 16;
  int gap = w / 40; if(gap < 3) gap = 3; if(gap > 12) gap = 12;
  int need = 4 * CALC_KEY_MIN + 3 * gap + 2 * pad + CALC_SIDE_MIN + gap;
  if(w < need) return 0;
  int sw = w / 4; if(sw < CALC_SIDE_MIN) sw = CALC_SIDE_MIN; if(sw > 260) sw = 260;
  return sw;
}
static void calcBox(int &bx, int &by, int &bw, int &bh){
  bx = 0; by = WIN_TOP; bw = gAppW; bh = WIN_BOT - WIN_TOP;
  int sw = calcSideW();
  if(sw > 0) bw -= sw;                          // la calculadora cede sitio al panel
  if(bw < 40) bw = 40;
  if(bh < 60) bh = 60;
}
// Reparto vertical. La rejilla tiene PRIORIDAD: primero se asegura de que las 5
// filas caben, y el display se queda con lo que sobre (hasta desaparecer en
// lienzos absurdamente bajos). Al reves -- display con alto minimo fijo -- la
// rejilla se salia por debajo del marco en ventanas achatadas.
static void calcLayout(int &m, int &gap, int &dh, int &bwv, int &bhv){
  int bx, by, bw, bh; calcBox(bx, by, bw, bh);
  (void)bx; (void)by;
  m = gAppW / 30; if(m < 4) m = 4; if(m > 16) m = 16;
  if(4 * m > bh){ m = bh / 8; if(m < 2) m = 2; }
  gap = gAppW / 40; if(gap < 3) gap = 3; if(gap > 12) gap = 12;
  int avail = bh - 2 * m; if(avail < 20) avail = 20;
  int need = 5 * 6 + 4 * gap;                   // minimo vital de la rejilla
  while(gap > 2 && need > avail * 3 / 4){ gap--; need = 5 * 6 + 4 * gap; }
  dh = avail / 5;                               // el display aspira a ~1/5
  if(dh > 120) dh = 120;
  int maxDh = avail - m - need;                 // ...pero nunca a costa de la rejilla
  if(dh > maxDh) dh = maxDh;
  if(dh < 0) dh = 0;
  int gridH = avail - dh - (dh > 0 ? m : 0);
  bhv = (gridH - 4 * gap) / 5; if(bhv < 6) bhv = 6;
  bwv = (bw - 2 * m - 3 * gap) / 4; if(bwv < 6) bwv = 6;
}
static void calcDispRect(int &x, int &y, int &w, int &h){
  int bx, by, bw, bh; calcBox(bx, by, bw, bh);
  int m, gap, dh, bwv, bhv; calcLayout(m, gap, dh, bwv, bhv);
  (void)bh;
  x = bx + m; y = by + m; w = bw - 2 * m; h = dh;
}
static void calcGrid(int &gx, int &gy, int &bw, int &bh, int &gap){
  int bx, by, bwx, bhx; calcBox(bx, by, bwx, bhx);
  (void)bwx; (void)bhx;
  int m, dh, bwv, bhv; calcLayout(m, gap, dh, bwv, bhv);
  gx = bx + m;
  gy = by + m + dh + (dh > 0 ? m : 0);
  bw = bwv; bh = bhv;
}
// Tamano de fuente segun el boton, para que la etiqueta nunca se salga.
static int calcFontFor(int bw, int bh, const char* t){
  int lim = (bw < bh) ? bw : bh;
  int fs = lim >= 56 ? 4 : lim >= 38 ? 3 : lim >= 24 ? 2 : 1;
  if(strlen(t) > 1 && fs > 1) fs--;
  while(fs > 1 && textW(t, fs) > bw - 6) fs--;
  return fs;
}
static int calcKeyY0 = 0, calcKeyY1 = 0;
static void calcRender(){
  // A pantalla completa se sigue componiendo en lockBuf y volcando de una
  // pasada (anti-parpadeo, igual que antes). Embebida NO: el lienzo de la
  // ventana ya se compone entero fuera de pantalla y se vuelca de golpe, y
  // ademas fbCopyBand copia FILAS FISICAS, que con el lienzo rotado de una
  // ventana apaisada no corresponden a las filas logicas.
  bool host = gHosted;
  setBuf(host ? fb : lockBuf);                 // hospedada, fb ya apunta al lienzo
  // Se limpia el LIENZO ENTERO, no solo la caja de la calculadora. calcBox le
  // resta el ancho del panel lateral, asi que limpiar solo esa caja dejaba sin
  // tocar la franja del panel: al cruzar el breakpoint quedaban ahi las
  // tarjetas del frame anterior, y el fundido se mezclaba contra esa basura en
  // vez de contra el fondo. Eso era el ghosting.
  int fx, fy, fw, fh; uiBox(fx, fy, fw, fh);
  fillRect(fx, fy, fw, fh, WIN_BG);
  int bx, by, bw0, bh0; calcBox(bx, by, bw0, bh0);
  int dx, dy, dw, dh; calcDispRect(dx, dy, dw, dh);
  if(dh > 0){
    int drad = dh / 5; if(drad > 14) drad = 14; if(drad < 2) drad = 2;
    fillRoundRect(dx, dy, dw, dh, drad, rgb565(28,31,40));
    int dfs = dh >= 90 ? 5 : dh >= 64 ? 4 : dh >= 40 ? 3 : 2;
    while(dfs > 1 && textW(calcDisp, dfs) > dw - 20) dfs--;
    drawTextR(dx + dw - 10, dy + dh / 2 - dfs * 4, calcDisp, dfs, rgb565(255,255,255));
  }
  int gx, gy, bw, bh, gap; calcGrid(gx, gy, bw, bh, gap);
  calcKeyY0 = gy - 4; calcKeyY1 = gy + 5 * (bh + gap) + 4;
  if(calcKeyY1 > gAppH) calcKeyY1 = gAppH;
  int rad = bw / 6; if(rad > 14) rad = 14; if(rad < 3) rad = 3;
  for(int r = 0; r < 5; r++) for(int c = 0; c < 4; c++){
    int x = gx + c * (bw + gap), y = gy + r * (bh + gap);
    const char* tl = CALC_LBL[r][c];
    uint16_t bg;
    if(c == 3 || (r == 4 && c == 2)) bg = rgb565(245,150,40);
    else if(r == 0)                  bg = rgb565(70,74,86);
    else                             bg = rgb565(92,96,110);
    // drawLiquidGlassPanel solo es correcto en portrait sin rotar; con lienzo
    // apaisado (ventana ancha) se usa el relleno plano.
    if(uiGlass && !gLand) drawLiquidGlassPanel(x, y, bw, bh, rad, bg);
    else fillRoundRect(x, y, bw, bh, rad, bg);
    int fs = calcFontFor(bw, bh, tl);
    drawTextC(x + bw / 2, y + bh / 2 - fs * 4 + 1, tl, fs, rgb565(248,248,252));
  }
  // Panel lateral opcional: memoria e historial. Aparece/desaparece entero.
  int sw = calcSideW();
  uint8_t aSide = uiSection(0, sw > 0);
  if(aSide && sw > 0){
    int pad, gapL, dhL, bwL, bhL; calcLayout(pad, gapL, dhL, bwL, bhL);
    (void)gapL; (void)dhL; (void)bwL; (void)bhL;
    int sx = bx + bw0, sy = by, shh = bh0;
    uiRectA(sx + pad / 2, sy + pad, sw - pad, shh - 2 * pad, pad, rgb565(30,34,46), aSide);
    int ix = sx + pad, iw = sw - 2 * pad, iy = sy + pad * 2;
    uiTextC(sx + sw / 2, iy, "Memoria", uiFontFit("Memoria", iw, 3), rgb565(150,160,190), aSide);
    iy += uiLineH(3) + pad;
    const char* mk[3] = { "MC", "MR", "M+" };
    int mh = (shh / 8) < 30 ? 30 : (shh / 8);
    for(int i = 0; i < 3; i++){
      uiRectA(ix, iy, iw, mh, mh / 4, rgb565(70,74,86), aSide);
      uiTextC(sx + sw / 2, iy + mh / 2 - uiLineH(2), mk[i], uiFontFit(mk[i], iw - 8, 3), rgb565(240,244,252), aSide);
      iy += mh + pad / 2;
    }
    iy += pad;
    uiTextC(sx + sw / 2, iy, "Resultado", uiFontFit("Resultado", iw, 2), rgb565(150,160,190), aSide);
    iy += uiLineH(2) + 4;
    uiTextC(sx + sw / 2, iy, calcDisp, uiFontFit(calcDisp, iw, 3), rgb565(200,230,255), aSide);
  }
  if(!host){ setBuf(fb); fbCopyBand(lockBuf, WIN_TOP, WIN_BOT - 1); }
  flxFlush(WIN_TOP, WIN_BOT);
}
static void calcRenderDisplay(){                       // solo el display (al teclear) -> responsivo
  // Dos dibujos SEPARADOS directo en
  // fb (fillRoundRect + drawTextR) antes de un solo Û^ºë†òµë(š+myÕô‚ÒSÂ§rÒ45%õrÒƒ²òò¦ööÒÖçVÂ††÷&—¦öçFÂ¢f–ÆÅ&÷VæE&V7B‡§‚Â§’Â§rÂbÂ2Â&v#ScRƒsÃsBÃƒ‚’“°¢f–ÆÄ6—&6ÆR‡§‚²†–çB’‚†6Õ¦ööÒÒ’òC’ãb¢§r’Â§’²2Â’Âr“²Ð¢²6öç7B6†"¢ÖE³UÒÒ²$dõDò"Â%d”DTò"Â$4”äR"Â$44”ôâ"Â%$õ$U2"Ó²–çB’Ò45%ô‚Ò3Â7s"Ò45%õròS°¢f÷"†–çB’Ò²’ÂS²’²²’G&uFW‡D2†7s"¢’²7s"ò"Â’ÂÖE¶•ÒÂ"Â’ÓÒ6ÔÖöFRò&v#ScRƒ#SRÃ##Ãc’¢&v#ScRƒsÃsbÃ“’“²Ð¢²–çB6'‚Ò45%õrò"Â6'’Ò45%ô‚ÒƒC²&ööÂf–FÖöFRÒ†6ÔÖöFRãÒ“²òò&÷Föâ6GW&¢G&t6—&6ÆR†6'‚Â6'’Â3BÂr“²G&t6—&6ÆR†6'‚Â6'’Â32Âr“°¢–b†6Õ&V2bbf–FÖöFR’f–ÆÅ&÷VæE&V7B†6'‚Ò"Â6'’Ò"Â#BÂ#BÂRÂ&v#ScRƒ#3ÃcÃc’“°¢VÇ6Rf–ÆÄ6—&6ÆR†6'‚Â6'’Â#rÂf–FÖöFRò&v#ScRƒ#3ÃcÃc’¢r“²Ð¢–b†6Õ&V2—²f–ÆÄ6—&6ÆR…45%õrò"ÒC"ÂcÂbÂ&v#ScRƒ#3ÃcÃc’“²G&uFW‡B…45%õrò"Ò3ÂSBÂ%$T2"Â"Â&v#ScRƒ#3ÃcÃc’“²Ð§Ð§7FF–2fö–B6Õ&VæFW$ÆÂ‚—²t6Æ—“Ò²t6Æ—“Ò45%ô‚Ò²6Õ&VæFW%&Wf–Wr‚“²6ÔG&uT’‚“²fÇ„fÇW6„ÆÂ‚“²Ð ¢òò2222222222222222222222222222222222222222222222222222222222220¢òò22D$TDR4EU$†6÷&RÂ&–÷&–FB"¢òò22ÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÐ¢òò22õ"TRTâTÂ4õ$R’äòTâTÂ¢Æö÷‚’×’6öâVÂFöFòVÀ¢òò226öæFVòFVÂF7F–ÂÂÆ÷2vW7F÷2’ÆÆöv–6FRÆ22Ò6÷'&P¢òò22VâVÂ6÷&RâFV¦&Æò4ôÕÄUDÔTåDRÆ–'&RW2§W7FòVÂö&¦WF—fð¢òò22FR66"Æ6GW&FR6ÕF–6²‚’âVâVÂ6÷&R–f—fRVÀ¢òò22&W6VçFW"ÂW&ò6öâ&–÷&–FB2g&VçFRÆ"FRW7FF&V ¢òò22VÂ&W6VçFW"4”TÕ$RÆFW6Æö¦Â6’VRÆ6Ö&6öÆòW6¢òò22Æ÷2‡VV6÷2VâVRVÂ&W6VçFW"W7FW7W&æFòÂDÔÒÒVP¢òò226öâÆÖ–÷"'FRFR7RF–V×òà¢òò20¢òò22W7ö6ÖW&öf%övWB‚’6R$ÄõTTW7W&æFòVÂf÷Föw&ÖâV’W6ð¢òò22–æò–×÷'F¢&Æ÷VV"W7FF&Væò&æFÖ2à¢òò2222222222222222222222222222222222222222222222222222222222220§7FF–2fö–B6ÕF6´fâ‡fö–B¢—°¢f÷"ƒ³²—°¢–b‚6Õ7G&VÔöâÇÂ6Õ6Vç6÷$ö²—²eF6´FVÆ’‡DÕ5õDõõD”4µ2ƒC’“²6öçF–çVS²Ð¢–çBrÒ6Õw&—FT–Gƒ°¢V–çCe÷B¢G7BÒ6Õ66VæT'Ve·uÓ°¢–b‚G7B—²eF6´FVÆ’‡DÕ5õDõõD”4µ2ƒC’“²6öçF–çVS²Ð¢&ööÂö³°¢–b†6Õ6–ævÆT'Vb—°¢òòVâ6öÆòÆ–Vç¦ó¢†’VRW67&–&—&Æò6öâVÂ6æFFòFöÖFòÂòÆT¢òòöG&––çF"Vâf÷Föw&ÖÖVF–òW67&–&—"à¢–b†6Ô×W‚’…6VÖ†÷&UF¶R†6Ô×W‚Â÷'DÔ…ôDTÄ’“°¢ö²Ò6Ô6GW&R†G7B“°¢–b†ö²’6Ôg&ÖU6W²³°¢–b†6Ô×W‚’…6VÖ†÷&Tv—fR†6Ô×W‚“°¢ÒVÇ6R°¢ö²Ò6Ô6GW&R†G7B“°¢–b†ö²—°¢òòV&Æ–66–öâFöÖ–6¢6R–çFW&6Ö&–âÆ÷2–æF–6W2âÆT’VP¢òòW7GWf–W&ÆW–VæFò&WF–VæR6Ô×W‚†7FFW&Ö–æ"7R6FÂ6¢òòVRW7FR–çFW&6Ö&–òçVæ6Æ–ÆÆÖVF–2à¢–b†6Ô×W‚’…6VÖ†÷&UF¶R†6Ô×W‚Â÷'DÔ…ôDTÄ’“°¢6Õ&VD–G‚Òs°¢6Õw&—FT–G‚ÒÒs°¢6Ôg&ÖU6W²³°¢–b†6Ô×W‚’…6VÖ†÷&Tv—fR†6Ô×W‚“°¢Ð¢Ð¢–b‚ö²’eF6´FVÆ’‡DÕ5õDõõD”4µ2ƒ3’“²òò6Vç6÷"×VFó¢æòVVÖ"5P¢eF6´FVÆ’ƒ“²òò6VFRVÂ6÷&RVçVRf–6ö'&Fð¢Ð§Ð¢òòFWF–VæRVÂ7G&VÖ–ærâÆÆÆÖ6Æ÷6R‚’&5TÅT”U"¢6’ÆVP¢òò6R6–W'&æòW&Æ6Ö&ÂW7Fò–W7F&VâfÇ6R’æò†6RæFà§7FF–2fö–B6Õ7G&VÕ7F÷‚—²6Õ7G&VÔöâÒfÇ6S²Ð §7FF–2fö–B6ÔVçFW"‚—°¢–b‚6Õ66VæT'Ve³Ò—°¢f÷"†–çB’Ò²’Â#²’²²¢6Õ66VæT'Ve¶•ÒÒ‡V–çCe÷B¢–†Vö65öÖÆÆö2‚‡6—¦U÷B”4Õõ5r¢4Õõ4‚¢"ÂÔÄÄô5ô4õ5•$ÒÂÔÄÄô5ô4ó„$•B“°¢6Õ66VæRÒ6Õ66VæT'Ve³Ó²òòÆ–2†—7F÷&–6ò‚&Æ–'&æ6ò"¢–b‚6Ô×W‚’6Ô×W‚Ò…6VÖ†÷&T7&VFT×WFW‚‚“°¢òò6–â6—F–ò&VÂ6VwVæFòÆ–Vç¦òÓâÖöFòFVw&FFò‡fW"6Õ6–ævÆT'Vb’à¢6Õ6–ævÆT'VbÒ†6Õ66VæT'Ve³ÒÒåTÄÂbb6Õ66VæT'Ve³ÒÓÒåTÄÂ“°¢–b†6Õ6–ævÆT'Vb—°¢6Õ66VæT'Ve³ÒÒ6Õ66VæT'Ve³Ó²òòÆ÷2F÷2–æF–6W2VçFâÂÖ—6Öð¢6W&–Âç&–çFb‚%´4ÕÒ5$Ò§W7F‚WR´"Æ–'&W2“¢Vâ6öÆòÆ–Vç¦òFRW66VæÆâ"À¢‡Vç6–væVB’††Vö65övWEög&VU÷6—¦R„ÔÄÄô5ô4õ5•$Ò’ò#B’“°¢Ð¢òò&–ÖW"6öçFVæ–Fó¢Vâf÷Föw&Ö&VÂ6’†’6Vç6÷#²6’æòÂVÂG&öà¢òòFR'VV&TâÄõ2Dõ2Æ–Vç¦÷2‡&VRVÂ–ær×öæræòVç6VæR&7W&¢òòVâ7R&–ÖW"–çFW&6Ö&–ò’à¢–b†6Õ66VæT'Ve³Ò—°¢–b‚6Ô6GW&R†6Õ66VæT'Ve³Ò’—°¢6ÔvVå66VæR†6Õ66VæT'Ve³Ò“°¢–b‚6Õ6–ævÆT'Vb’6ÔvVå66VæR†6Õ66VæT'Ve³Ò“°¢ÒVÇ6R–b‚6Õ6–ævÆT'Vb—°¢ÖVÖ7’†6Õ66VæT'Ve³ÒÂ6Õ66VæT'Ve³ÒÂ‡6—¦U÷B”4Õõ5r¢4Õõ4‚¢"“°¢Ð¢Ð¢6Õ&VD–G‚Ò²6Õw&—FT–G‚Ò6Õ6–ævÆT'Vbò¢°¢–b‚6ÕF6´‚¢…F6´7&VFU–ææVEFô6÷&R†6ÕF6´fâÂ&6Ô6GW&R"ÂC“bÂåTÄÂÂ"Âf6ÕF6´‚Â“°¢Ð¢6Õ¦ööÒÒãc²6Õ&V2ÒfÇ6S²6ÔÖöFRÒ²6Ôæ–v‡BÒfÇ6S²6Õ&rÒfÇ6S²6ÔW‡òÒS²6ÔV—5‚Ò²6ÔV—5’Ò°¢6ÔÆ7E6WÒ6Ôg&ÖU6W°¢6Õ7G&VÔöâÒG'VS²òò'F—"FRV’ÆF&V6GW&¢–b†6Õ66VæT'Ve³Ò’6Õ&VæFW$ÆÂ‚“°§Ð§7FF–2fö–B6ÕF–6²‚—°¢–b‚6Õ66VæT'Ve³Ò’&WGW&ã°¢6–b4Õô„5õ4Tå4õ ¢òò–äò6R6GW&V“¢6öÆò6R6ö×'VV&6’ÆF&V†V&Æ–6FòVà¢òòf÷Föw&ÖçVWfòâW2VæÆV7GW&FRVâ6öçFF÷"Â7VW7Fææ÷6VwVæF÷2À¢òò’6’æò†’æFçVWfò6ÕF–6²‚’6–wVRFRÆ&vò6–â&Æ÷VV"VÂÆö÷à¢–b†6Ôg&ÖU6WÒ6ÔÆ7E6W—²6ÔÆ7E6WÒ6Ôg&ÖU6W²6Õ&VæFW$ÆÂ‚“²Ð¢6VæF–`¢–çBW‚Ò45%õrÒ#bÂW’Ò3ÂV‚Ò#ƒ°¢–b…BæF÷vâbbBç‚ãÒW‚Ò‚bbBç‚ÃÒW‚²#bbbBç’ãÒW’Ò"bbBç’ÃÒW’²V‚²"—°¢–çBbÒ†W’²V‚ÒBç’’¢òVƒ²–b‡bÂ’bÒ²–b‡bâ’bÒ²6ÔW‡òÒc²6Õ&VæFW$ÆÂ‚“²&WGW&ã°¢Ð¢–çB§‚ÒCÂ§’Ò45%ô‚ÒSÂ§rÒ45%õrÒƒ°¢–b…BæF÷vâbbBç’ãÒ§’ÒBbbBç’ÃÒ§’²‚bbBç‚ãÒ§‚ÒBbbBç‚ÃÒ§‚²§r²B—°¢fÆöB¢Ò²†fÆöB’…Bç‚Ò§‚’ò§r¢C’ãc²–b‡¢Â’¢Ò²–b‡¢âS’¢ÒS²6Õ¦ööÒÒ£²6Õ&VæFW$ÆÂ‚“²&WGW&ã°¢Ð¢–b‚BçF’&WGW&ã°¢–b…Bç‚ÂC‚bbBç’ÂC‚—²6Æ÷6R‚“²&WGW&ã²Ð¢–b…Bç‚ãÒcbbBç‚ÃÒbbBç’ãÒ‚bbBç’ÃÒCB—²6Ôæ–v‡BÒ6Ôæ–v‡C²6Õ&VæFW$ÆÂ‚“²&WGW&ã²Ð¢–b…Bç‚ãÒ‚bbBç‚ÃÒS‚bbBç’ãÒ‚bbBç’ÃÒCB—²6Õ&rÒ6Õ&s²6Õ&VæFW$ÆÂ‚“²&WGW&ã²Ð¢²fÆöBÇe³UÒÒ²ãVbÂÂ"ÂRÂSÓ²–çB'rÒSbÂrÒ‚ÂF÷BÒR¢'r²B¢rÂ7‚Ò…45%õrÒF÷B’ò"Â’Ò45%ô‚Ò“#°¢f÷"†–çB’Ò²’ÂS²’²²—²–çB‚Ò7‚²’¢†'r²r“²–b…Bç‚ãÒ‚bbBç‚ÃÒ‚²'rbbBç’ãÒ’bbBç’ÃÒ’²3B—²6Õ¦ööÒÒÇe¶•Ó²6Õ&VæFW$ÆÂ‚“²&WGW&ã²ÒÒÐ¢²–çB’Ò45%ô‚Ò3Â7s"Ò45%õròS²–b…Bç’ãÒ’Ò‚bbBç’ÃÒ’²#"—²–çBÒÒBç‚ò7s#²–b†ÒãÒbbÒÂR—²6ÔÖöFRÒÓ²–b†6ÔÖöFRÓÒ’6Õ&V2ÒfÇ6S²6Õ&VæFW$ÆÂ‚“²&WGW&ã²ÒÒÐ¢²–çB6'‚Ò45%õrò"Â6'’Ò45%ô‚ÒƒC²–b…Bç‚ãÒ6'‚Ò3BbbBç‚ÃÒ6'‚²3BbbBç’ãÒ6'’Ò3BbbBç’ÃÒ6'’²3B—°¢–b†6ÔÖöFRãÒ’6Õ&V2Ò6Õ&V3²6Õ&VæFW$ÆÂ‚“²&WGW&ã²ÒÐ§Ð ¢òò2222222222222222222222222222222222222222222222222222222222220¢òò22äõD2²DT4ÄDòB42„U2ôTâôåTÒôTÔô¤’¢òò22VçFW&÷2F–æÖ–6÷2†Ö7F—fò’Â'VffW"UDbÓ‚6VwW&òÀ¢òò22Æöær×&W72&6VçF÷2’ÖVò÷"7VG&–7VÆF7F–Âà¢òò2222222222222222222222222222222222222222222222222222222222220¢6FVf–æR´%ô4ôÅ2 ¢6FVf–æR´%õ$õu20 ¢òòBÖG&–6W2–æFWVæF–VçFW2FR6FVæ2†6öç7B6†"¢’âÆâ6öâ%Ç„35Ç„#"à¢òò7V&VâV’'&–&†çFW2W7F&âFV&¦òFRÆvVöÖWG&–’÷'VRÆÇGW&FP¢òòÆg&æ¦FR6†—2FWVæFRFRTR6W7F7F—f¢VâÆ6çVÖW&–6W6¢òòg&æ¦×VW7G&Æ÷26–Ö&öÆ÷2W'6öæÆ—¦F÷2FRÆf6RRà§7FF–26öç7B6†"¢Ä”õUEôU5´´%õ$õu5Õ´´%ô4ôÅ5ÒÒ°¢²'"Â'r"Â&R"Â'""Â'B"Â'’"Â'R"Â&’"Â&ò"Â''ÒÀ¢²&"Â'2"Â&B"Â&b"Â&r"Â&‚"Â&¢"Â&²"Â&Â"Â%Ç„35Ç„#'ÒÀ¢²'¢"Â'‚"Â&2"Â'b"Â&""Â&â"Â&Ò"Â"Â"Â"â"Â#ò'ÒÓ°§7FF–26öç7B6†"¢Ä”õUEôTå´´%õ$õu5Õ´´%ô4ôÅ5ÒÒ°¢²'"Â'r"Â&R"Â'""Â'B"Â'’"Â'R"Â&’"Â&ò"Â''ÒÀ¢²&"Â'2"Â&B"Â&b"Â&r"Â&‚"Â&¢"Â&²"Â&Â"Â#²'ÒÀ¢²'¢"Â'‚"Â&2"Â'b"Â&""Â&â"Â&Ò"Â"Â"Â"â"Â#ò'ÒÓ°§7FF–26öç7B6†"¢Ä”õUEôåTÕ´´%õ$õu5Õ´´%ô4ôÅ5ÒÒ°¢²#"Â#""Â#2"Â#B"Â#R"Â#b"Â#r"Â#‚"Â#’"Â#'ÒÀ¢²$"Â"2"Â"B"Â"R"Â"b"Â"Ò"Â%ò"Â"‚"Â"’"Â"ò'ÒÀ¢²"¢"Â%Â""Â"r"Â#¢"Â#²"Â""Â#ò"Â"²"Â#Ò"Â"â'ÒÓ°§7FF–26öç7B6†"¢Ä”õUEôTÔô¤•´´%õ$õu5Õ´´%ô4ôÅ5ÒÒ²òòVÖ÷F–6öæW2FRFW‡Fò†ÆgVVçFRÆ÷2F–'V¦¢²#¢’"Â#¤B"Â#¢‚"Â#²’"Â#¥"Â'„B"Â#¦ò"Â#§Â"Â#Ã2"Â#£2'ÒÀ¢²%åâ"Â&õöò"Â#ã¢‚"Â#¢r‚"Â$"’"Â"ÕòÒ"Â#Ò’"Â$C¢"Â#§b"Â#¦2'ÒÀ¢²'WwR"Â#¢¢"Â#ÅóÂ"Â#åóâ"Â"‡’’"Â""Â#¥Ò"Â%³¢"Â%EõB"Â&òò'ÒÓ° §7FF–26öç7B6†"¢‚¦Ö7F—fò•´´%ô4ôÅ5ÒÒÄ”õUEôU3²òòÃÃÂVçFW&òÖW7G&ð§7FF–2&ööÂ¶%6†–gBÒfÇ6RÂ¶$ÆætW2ÒG'VS° ¢òò2222222222222222222222222222222222222222222222222222222222220¢òò22d4R+rtTôÔUE$”DTÂDT4ÄDòTâd$”$ÄU0¢òò22ÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÐ¢òò22çFW2´%õ‚ô´%ôµrô´%ô´‚ô´%ôtW&â6FVf–æRf–¦÷3¢æò†&–¢òò22f÷&ÖFR6Ö&–"VÂFÖæò6–â&V6ö×–Æ"â†÷&6öà¢òò22d$”$ÄU2VR6ÆVâFRt¶%6—¦RÂ’Æ÷2æöÖ'&W2´%ò¢6P¢òò226öç6W'fâ6öÖòÖ7&÷2&VRÆ2ãƒ&VfW&Væ6–2VR–¢òò22W†—7F–âVâæ÷F2Â&Æ÷VVò’v’Ôf’6–vâÆW–VæF÷6R–wVÂà¢òò20¢òò22Æ÷2G&W2FÖæ÷2†æ6†òFRFV6ÆòÇFòò6W&6–öâòÖ&vVâ“ ¢òò226ö×7Fò3’òC"òBò#rÓâ£3’²’£BÒC#b‚FR&V¦–ÆÆ¢òò22æ÷&ÖÂC2òC‚òBòbÓâCcb‚„U„5DÔTåDRÆòFR6–V×&R¢òò22w&æFRCRòSbò"òbÓâCc‚€¢òò22Æ÷2G&W26&VâVâ45%õsÓCƒ6öâÖ&vVâÆ÷2F÷2ÆF÷2Â’Æ¢òò227V'Ff–Æ†gVæ6–öæW2’FW&Ö–æ6–V×&R÷"Væ6–ÖFVÂ&÷&FP¢òò22–æfW&–÷"âfW"¶%6—¦T6†V6²‚’à¢òò2222222222222222222222222222222222222222222222222222222222220¢6–b´%õ4•¤Uô4ôäd”uôôà§7FF–2–çB¶$µrÒCRÂ¶$´‚ÒcÂ¶$vÒ"Â¶%‚Òc°¢òò&V6Æ7VÆÆvVöÖWG&–'F—"FRt¶%6—¦Râ6RÆÆÖÂ6&v"Æ0¢òò&VfW&Væ6–2’6FfW¢VRVÂW7V&–ò6Ö&–VÂFÖæòVâ§W7FW3¢Æ–6¢òò”Â6–â&V–æ–6–"Â÷'VRFöFÆvVöÖWG&–6R6öç7VÇFVâ6F&W–çFFòà¢òð¢òòõ"TRU5Dõ2åTÔU$õ2‡&Wf—6F÷2G&2&ö&"VâÆÆ6¢VÂFV6ÆFð¢òòçFW&–÷"6Æ–WVVæòR–æ6öÖöFò“ ¢òò+rVÂä4„òW7FF÷Fò÷"Æ26öÇVÖæ2â6öâCƒ‚FRçFÆÆÂVÀ¢òòÖ†–Öò&¦öæ&ÆRW2CR‚FRFV6Æ’"‚FR6W&6–öâƒCS³ƒÓCc‚Âð¢òò6Vb‚FRÖ&vVâ6FÆFò’â÷"†’–æò6RVVFR7&V6W"Ö2à¢òò+rVÂÅDòW2FöæFR4’†’6—F–òÂ’W2ÆòVRFRfW&FB6Ræ÷F6öâVÂFVFó ¢òòÆçFÆÆF–VæRƒ‚FRÇFò’Æ2Bf–Æ2ö7Wâ6öÖò×V6†òã3à¢òò÷"W6òVÂFÖæò7&V6R6ö'&RFöFò†6–&¦òà¢òò6ö×7FòC2‚S†ÆòVRçFW2W&$æ÷&ÖÂ"¢òòæ÷&ÖÂCR‚c‡÷"FVfV7Fó¢#RRÖ2ÇFòVRçFW2¢òòw&æFRCR‚s"ƒSRÖ2ÇFòVRçFW2§7FF–2fö–B¶$Ç•6—¦R‚—°¢–b†t¶%6—¦RÓÒ´%õ4•¤Uô4ôÕ5B—²¶$µrÒC3²¶$´‚ÒS²¶$vÒC²Ð¢VÇ6R–b†t¶%6—¦RÓÒ´%õ4•¤Uô$”r—²¶$µrÒCS²¶$´‚Òs#²¶$vÒ#²Ð¢VÇ6R²¶$µrÒCS²¶$´‚Òc²¶$vÒ#²Ð¢–çBwrÒ´%ô4ôÅ2¢¶$µr²„´%ô4ôÅ2Ò’¢¶$v²òòæ6†ò&VÂFRÆ&V¦–ÆÆ¢¶%‚Ò…45%õrÒwr’ò#²–b†¶%‚Â"’¶%‚Ò#²òò6VçG&FÂçVæ6VvFÂ&÷&FP§Ð¢6VÇ6P¢òò–çFW''WF÷"¢W†7FÖVçFRÆ÷2çVÖW&÷2FR6–V×&RÂ’¶$Ç•6—¦Ræò†6RæFà§7FF–26öç7B–çB¶$µrÒC2Â¶$´‚ÒC‚Â¶$vÒBÂ¶%‚Òc°§7FF–2fö–B¶$Ç•6—¦R‚—·Ð¢6VæF–`¢6FVf–æR´%ôµr¶$µp¢6FVf–æR´%ô´‚¶$´€¢6FVf–æR´%ôt¶$v ¢6FVf–æR´%õ‚¶%€ ¢òòW‡G&2VR6RF–'V¦âTä4”ÔFRÆ2FV6Æ2†&'&7WW&–÷"FRÆf6R2¢òòg&æ¦FR6†—2FRÆf6Rb’â6öÆòÆ÷2×VW7G&Æ7WW&f–6–RVRÆ÷2–FP¢òò††÷“¢æ÷F2’âVÂ&Æ÷VVò’VÂv’Ôf’öæVâ¶$W‡G&4öãÖfÇ6R&÷÷6—FòÒÐ¢òòVâ&÷FöâFR'÷'FVÆW2"òFR&§W7FW2"66W6–&ÆRFW6FRÆçFÆÆFP¢òò6öçG&6Væ6W&–VâwV¦W&òFR6VwW&–FBÂæòVæ6öÖöF–FBà§7FF–2&ööÂ¶$W‡G&4öâÒfÇ6S°§7FF–2–çB¶%FööÆ&$‚‚—²&WGW&â„´%õDôôÄ$%ôôâbb¶$W‡G&4öâbbt¶%FööÆ&"’òSb¢²Ð¢òòÆg&æ¦f–æFR'&–&F–VæRDõ2–çV–Æ–æ÷2VRçVæ66ö–æ6–FVã¢Æ÷26†—0¢òòFRWFö6ö×ÆWFFò†62FRÆWG&2’’Æ÷26–Ö&öÆ÷2W'6öæÆ—¦F÷2FRÆf6RP¢òò†6çVÖW&–6’â7VvW&—"Æ'&2Ö–VçG&26RFV6ÆVâçVÖW&÷2æòFVæG&–¢òò6VçF–FòÂ6’VR6R&W'FVâÆÖ—6Ög&æ¦VâfW¢FR7VÖ"F÷2à§7FF–2&ööÂ¶$6†—5vçB‚—°¢–b‚¶$W‡G&4öâ’&WGW&âfÇ6S°¢–b†Ö7F—fòÓÒÄ”õUEôåTÒ’&WGW&â´%õ4UED”äu5ôôâòG'VR¢fÇ6S°¢&WGW&â´%ôUDô4ôÕÄUDUôôâbbt¶%&VF–7C°§Ð§7FF–2–çB¶$6†—4‚‚—²&WGW&â¶$6†—5vçB‚’ò3"¢²Ð§7FF–2–çB¶%F÷‚‚—²&WGW&â¶%FööÆ&$‚‚’²¶$6†—4‚‚“²Ð¢òò’FRÆ$”ÔU$d”ÄDRDT4Ä2âW2ÆòVR6–V×&R6–væ–f–6ò´%õ’Â’6–wVP¢òò6–væ–f–6æFòÆòÖ—6Öó¢Æ÷2W‡G&27&V6Vâ†6–%$”$ÂæòV×V¦âÆ2FV6Æ2à§7FF–2–çB¶%&÷w5F÷‚—²&WGW&â45%ô‚ÒB¢„´%ô´‚²´%ôt’Òc²Ð¢6FVf–æR´%õ’¶%&÷w5F÷‚¢òò’FöæFRV×–W¦VÂäTÂVçFW&ò†6öâ&'&’6†—2–æ6ÇV–F÷2’âW2VÂÆ–Ö—FP¢òòFR&¦òFVÂ&VFRFW‡Fò’VÂ&÷&FR7WW&–÷"FRÆ&æFföÆ6"à§7FF–2–çB¶%æVÅF÷‚—²&WGW&â¶%&÷w5F÷‚’ÒBÒ¶%F÷‚‚“²Ð§7FF–2–çB¶%FööÆ&%’‚—²&WGW&â¶%æVÅF÷‚’²C²Ð§7FF–2–çB¶$6†—5’‚—²&WGW&â¶%FööÆ&%’‚’²¶%FööÆ&$‚‚“²Ð¢òò’FRÆf–ÆFRgVæ6–öæW2‡6†–gBÂ6Â–F–öÖÂW76–òÂ&÷'&"ÂVçFW"’à§7FF–2–çB¶$gVæ5’‚—²&WGW&â¶%&÷w5F÷‚’²2¢„´%ô´‚²´%ôt“²Ð ¢òòÒÒÒÒf–ÆFRgVæ6–öæW3¢vVöÖWG&–&÷÷&6–öæÂÆ&V¦–ÆÆÒÒÒÐ¢òòçFW2Æ2bFV6Æ2FVæ–â‚’æ6†ò„$D4ôDTDõ2ƒbÂc‚Â#bÂs‚Â3s"ÂC#¢òòVâ7VG&ò6—F–÷2F—7F–çF÷2Â’VÂ†—B×FW7BÆ÷2&WWF–6öÖòçVÖW&÷27VVÇF÷0¢òò‚&–b‡‚ÂcB’âââVÇ6R–b‡‚Â#"’"’â6öâÆ&V¦–ÆÆ–æòf–¦†'&–VP¢òòFö6&Æ÷2VâFöF÷2ÆF÷3²V÷#¢6Ö&–"Væò’öÇf–F"÷G&òFV¦FV6Æ2VR6P¢òòfVâVâVâ6—F–ò’&W7öæFVâVâ÷G&òâ†÷&6ÆVâFRVæ6öÆF&ÆFRW6÷2à¢6FVf–æR´%ôd´U•2`§7FF–26öç7BfÆöB´%ôeu´´%ôd´U•5ÒÒ²ã3VbÂã#VbÂãbÂãC#bÂãbÂãbÓ°§7FF–2–çB¶$d¶W•r†–çB’—°¢–b†’ÂÇÂ’ãÒ´%ôd´U•2’&WGW&â°¢–çBwrÒ´%ô4ôÅ2¢´%ôµr²„´%ô4ôÅ2Ò’¢´%ôt°¢–çBW6&ÆRÒwrÒ„´%ôd´U•2Ò’¢´%ôt°¢–çBrÒ†–çB’‡W6&ÆR¢´%ôeu¶•Ò²ãVb“°¢&WGW&ârÂ#ò#¢s°§Ð§7FF–2–çB¶$d¶W•‚†–çB’—°¢–çB‚Ò´%õƒ°¢f÷"†–çB²Ò²²Â“²²²²’‚³Ò¶$d¶W•r†²’²´%ôt°¢&WGW&âƒ°§Ð¢òòFWgVVÇfRâãR‡6†–gBÂ6Â–F–öÖÂW76–òÂ&÷'&"ÂVçFW"’òÓà¢òòÖ—6Öò7&—FW&–òVR¶$6VÆÄC¢6FFV6Æ6RVVF6öâÆ6W&6–öâVRF–VæP¢òò7RFW&V6†Â6’VRæò†’g&æ¦2×VW'F2VçG&R&÷FöæW2à§7FF–2–çB¶$e&÷t†—B†–çB‚Â–çB’—°¢–çBg’Ò¶$gVæ5’‚“°¢–b‡’Âg’Ò´%ôtÇÂ’âg’²´%ô´‚²´%ôt’&WGW&âÓ°¢–b‡‚Â´%õ‚Ò´%ôt’&WGW&âÓ°¢f÷"†–çB’Ò²’Â´%ôd´U•3²’²²—°¢–çB‚Ò¶$d¶W•‚†’“°¢–b‡‚ÃÒ‚²¶$d¶W•r†’’²´%ôt’&WGW&â“°¢Ð¢&WGW&âÓ°§Ð¢òò6ö×&ö&6–öâFRVRä”äuTâFÖæò6R6ÆRFRÆçFÆÆâæòF–'V¦æF¢W0¢òòVæ6W&6–öâ&&FVR6÷'&RVæfW¢Â'&æ6"’FV¦&7G&òVâVÂÆörà§7FF–2&ööÂ¶%6—¦T6†V6²‚—°¢–çBwrÒ´%ô4ôÅ2¢´%ôµr²„´%ô4ôÅ2Ò’¢´%ôt°¢–çBgrÒ¶$d¶W•‚„´%ôd´U•2Ò’²¶$d¶W•r„´%ôd´U•2Ò“°¢–çB&÷BÒ¶$gVæ5’‚’²´%ô´ƒ°¢&WGW&â„´%õ‚ãÒ’bb„´%õ‚²wrÃÒ45%õr’bb†grÃÒ45%õr’bb†&÷BÃÒ45%ô‚ÒB’bb†¶%æVÅF÷‚’â#“°§Ð ¢òòÒÒÒÒ6öÆ÷&W2FVÂFV6ÆFò„f6RS¢6öçG&7FRÇFò²÷6–FB²W7F–Æò’ÒÒÒÐ¢òò6R&W7VVÇfVâVâgVæ6–öâFRt¶$†”6öâVâfW¢FRW7F"W67&—F÷2ÖæòVâ6F¢òògVæ6–öâFRF–'V¦òÂVRW&ÆòVR†6––×÷6–&ÆRæF—"VâFVÖà§7FF–2V–çCe÷B¶$6öÄ¶W’‚—²&WGW&ât¶$†”6öâò&v#ScRƒÃÃ’¢&v#ScRƒS"ÃSbÃs“²Ð§7FF–2V–çCe÷B¶$6öÄ¶W•G‡B‚—²&WGW&ât¶$†”6öâò&v#ScRƒ#SRÃ#SRÃ#SR’¢&v#ScRƒ#CÃ#C"Ã#C‚“²Ð§7FF–2V–çCe÷B¶$6öÄfâ‚—²&WGW&ât¶$†”6öâò&v#ScRƒ#BÃ#BÃ#B’¢&v#ScRƒcbÃsÃƒb“²Ð§7FF–2V–çCe÷B¶$6öÄfäöâ‚—²&WGW&ât¶$†”6öâò&v#ScRƒ#SRÃ#Ã’¢&v#ScRƒcÃÃ#3R“²Ð§7FF–2V–çCe÷B¶$6öÄfäöåG‡B‚—²&WGW&ât¶$†”6öâò&v#ScRƒÃÃ’¢&v#ScRƒ#CÃ#C"Ã#C‚“²Ð§7FF–2V–çCe÷B¶$6öÅæVÂ‚—²&WGW&ât¶$†”6öâò&v#ScRƒÃÃ’¢&v#ScRƒ3bÃCÃS‚“²Ð§7FF–2V–çCe÷B¶$6öÄVFvR‚—²&WGW&ât¶$†”6öâò&v#ScRƒ#SRÃ#SRÃ#SR’¢&v#ScRƒ“bÃ"Ã#B“²Ð§7FF–2V–çCe÷B¶$6öÅ&W72‚—²&WGW&ât¶$†”6öâò&v#ScRƒ#SRÃ#Ã’¢&v#ScRƒ“bÃ3"Ã#3R“²Ð¢òòFÖæòFRÆWG&FRÆ2FV6Æ2„f6RR’âVÂ6—7FVÖFRFÆÆ2FRG&uFW‡BW0¢òòVçFW&òƒÃ"Ã2âââ’Â6’VR%FÖæòFRgVVçFR"×VWfRW6FÆÆÂæòVâf7F÷"à§7FF–2–çB¶$föçE6—¦R‚—²&WGW&ât¶$föçE62ÓÒò¢t¶$föçE62ÓÒ"ò2¢#²Ð¢òòÇFòFRÆ–æV&÷†–ÖFòFRW6FÆÆÂ&6VçG&"VÂvÆ–fòVâÆFV6Æà§7FF–2–çB¶$föçDG’‚—²&WGW&ât¶$föçE62ÓÒòB¢t¶$föçE62ÓÒ"ò"¢ƒ²Ð§7FF–2–çB¶%&F—W2‚—²&WGW&ât¶%7G–ÆRÓÒò¢c²ÒòòÒ$7VG&F  ¢òò–çFTäFV6Æ6öâVÂW7F–ÆòVÆVv–Fò„f6RR’’VÂFW7FVÆÆòFRVÇ6F¢òò„f6Rr’â6öÆò&–Ö—F—f÷2VâÆf—&ÖÂ6öÖòÖæFVÂ&÷–V7Fòà§7FF–2fö–B¶%–çD¶W’†–çB‚Â–çB’Â–çBrÂ–çB‚Â6öç7B6†"¢Æ&VÂÂ–çBföçE6—¦RÂV–çCe÷B&rÂV–çCe÷BG‡BÂ&ööÂ&W76VB—°¢–çB"Ò¶%&F—W2‚“°¢V–çCe÷Bf–ÆÂÒ&W76VBò¶$6öÅ&W72‚’¢&s°¢–b†t¶%7G–ÆRÓÒ"—²òò$6öçF÷&æò#¢6–â&VÆÆVæòÂ6öÆò&÷&FP¢–b‡&W76VB’f–ÆÅ&÷VæE&V7B‡‚Â’ÂrÂ‚Â"Âf–ÆÂ“°¢G&u&÷VæE&V7B‡‚Â’ÂrÂ‚Â"Â¶$6öÄVFvR‚’“°¢ÒVÇ6R°¢f–ÆÅ&÷VæE&V7B‡‚Â’ÂrÂ‚Â"Âf–ÆÂ“°¢Ð¢–b†Æ&VÂbbÆ&VÅ³Ò’G&uFW‡D2‡‚²rò"Â’²‚ò"Ò¶$föçDG’‚’ÂÆ&VÂÂföçE6—¦RÂG‡B“°§Ð ¢òòföæFòFVÂæVÂFVÂFV6ÆFòâVâ6öÆò6—F–ò&Æ2G&W27WW&f–6–W2Â6öâÆ¢òò÷6–FBFRÆf6RRÆ–6F¢ÂRW2VÂf–G&–ò÷&VÆÆVæòFR6–V×&S²÷ ¢òòFV&¦ò6RW6Vâ&VÆÆVæò6öâÆf&VR6RG&ç7&VçFRÆòVR†’FWG&0¢òò†G&tÆ—V–DvÆ75æVÂæòFÖ—FRÆfÂ6’VR÷6–FB&6–Â6R6Ö&–¢òòÆ'WFÆæ6öâÆfÒÒFö7VÖVçFFòÂæòW2VâöÇf–Fò’à§7FF–2fö–B¶%–çEæVÂ†–çB“ÂV–çCe÷BF–çB—°¢–çB‚Ò45%ô‚Ò“°¢–b†‚ÃÒ’&WGW&ã°¢V–çC…÷BÒ‡V–çC…÷B’†t¶$÷6—G’¢#SRò“°¢–b‡V”vÆ72bbt¶$÷6—G’ãÒ—²G&tÆ—V–DvÆ75æVÂƒÂ“Â45%õrÂ‚ÂÂF–çB“²&WGW&ã²Ð¢–b†t¶$÷6—G’ãÒ’f–ÆÅ&V7BƒÂ“Â45%õrÂ‚ÂF–çB“°¢VÇ6Rf–ÆÅ&V7DƒÂ“Â45%õrÂ‚ÂF–çBÂ“°§Ð ¢òòÒÒÒÒÖVò÷"7VG&–7VÆ¢‡‚Ç’’Óâ6VÆF†f–Æ¤4ôÅ2¶6öÂ’òÓÒÒÒÐ¢òò7V&RV’†çFW2f—f–FVçG&òFVÂ&Æ÷VRFRæ÷F2’÷'VR†÷&ÆW6à¢òòFÖ&–VâÆ'WF×VÇF—F÷VRFRÆf6R"’VÂVF—F÷"FRF¦÷2FRÆf6RRÀ¢òòVR6RFVf–æVâçFW2VRÆà¢òòVÂ&V6Vç6–&ÆRFR6FFV6ÆW27R4ò4ôÕÄUDò‡FV6Æ²6W&6–öâ’Âæð¢òò6öÆòVÂ&V7FæwVÆò–çFFòâçFW2Â6W"VâÆ÷2"ÓB‚FR6W&6–öâæòW&¢òòæ–æwVæFV6Æ¢VÂF÷VR6RW&F–Vâ6–ÆVæ6–ò’Æ6Vç66–öâW&ÆFRVà¢òòFV6ÆFòVR&æò&W7öæFR&–Vâ"â†÷&æò†’‡VV6÷2×VW'F÷3¢6FVçFòFRÆ¢òò&V¦–ÆÆW'FVæV6RÆwVæFV6ÆâÆòVR6RfR6–wVR6–VæFò–wVÃ²ÆòVP¢òò6Ö&–W2ÆòVR6RVVFRFö6"à§7FF–2–çB¶$6VÆÄB†–çB‚Â–çB’—°¢–çB—F6…‚Ò´%ôµr²´%ôtÂ—F6…’Ò´%ô´‚²´%ôt°¢–çBG‚Ò‚Ò´%õ‚ÂG’Ò’Ò´%õ“°¢–b†G‚ÂÇÂG’Â’&WGW&âÓ°¢–çB2ÒG‚ò—F6…‚Â"ÒG’ò—F6…“°¢–b†2ÂÇÂ2ãÒ´%ô4ôÅ2ÇÂ"ÂÇÂ"ãÒ´%õ$õu2’&WGW&âÓ°¢&WGW&â"¢´%ô4ôÅ2²3°§Ð ¢òòÒÒÒÒd4RS¢ÆWFFR6–Ö&öÆ÷2W'6öæÆ—¦&ÆW2ÒÒÒÐ¢òòVÂW7V&–òVÆ–vRBFRW7F÷2bâ6RwV&Fâ6öÖò–æF–6RÂæò6öÖòFW‡FòÂ&¢òòVR6V”Õõ4”$ÄR6&"6öâVâ6&7FW"VRÆgVVçFRWƒræòF–'V¦Rà¢6FVf–æR´%õ5”ÕõôôÅôâ`§7FF–26öç7B6†"¢´%õ5”ÕõôôÅ´´%õ5”ÕõôôÅôåÒÒ°¢$"Â"2"Â"B"Â"R"Â"b"Â"¢"Â"²"Â#Ò"Â"ò"Â%ÅÂ"Â"‚"Â"’"Â%²"Â%Ò"Â#Â"Â#â"Ó°§7FF–26öç7B6†"¢¶%7–ÔB†–çB’—°¢–b†’ÂÇÂ’ãÒ´%õ5”Õ2’&WGW&â"#°¢–çB²Òt¶%7–Õ¶•Ó²–b†²ÂÇÂ²ãÒ´%õ5”ÕõôôÅôâ’²Ò°¢&WGW&â´%õ5”ÕõôôÅ¶µÓ°§Ð ¢òò2222222222222222222222222222222222222222222222222222222222220¢òò22d4R"+r4TuT”Ô”TåDòDR4ôåD5Dõ2DTÂDT4ÄDð¢òò22ÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÐ¢òò22W7Fòäò7W7F—GW–R7G'V7BF÷V6ƒ¢W2Væf–&–FVP¢òò226÷'&RTâ$ÄTÄò’6öÆòFVçG&òFRÆ27WW&f–6–W2FP¢òò22FV6ÆFòâFöFòÆòFVÖ2†'&7G&RFRÖæ–¦2ÂÆöær×&W70¢òò22FR6VçF÷2ÂÖVçR6öçFW‡GVÂÂvW7F÷2’6–wVRÆW–VæFòBà¢òò20¢òò22–FV¢6FFVFòVRVÂuC“&W÷'FG&R7RE$4²”BâVà¢òò22–BVR&V6R÷"&–ÖW&fW¢6ö'&RVæFV6ÆÒ&¶W¢òò22F÷vâ"ÓâÆFV6Æ6RW67&–&R”Â6–âW7W&"VRW6P¢òò22FVFò6RÆWfçFRâ7VæFòVÂ–BFW6&V6R6RÆ–&W&7P¢òò22‡VV6ò’æò6RW67&–&RæF÷G&fW¢â6’Â÷–"Æ¢òò226–wV–VçFRFV6ÆÖ–VçG&2ÆçFW&–÷"Vâ6RW7F6öÇFæFð¢òò22æò6RG&&æ’–W&FRVÇ66–öæW2‡6Vç66–öâFR&öÆÆ÷fW"’à¢òò2222222222222222222222222222222222222222222222222222222222220¢6FVf–æR´%õE$4µôÔ‚´%ôÔ…ô”åE0¢6FVf–æR´%ôUeôÔ‚€§7FF–2–çB¶%G&´–E´´%õE$4µôÔ…ÒÒ²ÓÂÓÂÓÂÓÂÓÓ°§7FF–2–çB¶%G&´6VÆÅ´´%õE$4µôÔ…ÒÒ²ÓÂÓÂÓÂÓÂÓÓ°§7FF–2–çB¶$Wd6VÆÅ´´%ôUeôÔ…Ó²òò6VÆFFRÆ&V¦–ÆÆFVÂWfVçFò’‚Ó6’æòÆ†’§7FF–2–çB¶$Wdfå´´%ôUeôÔ…Ó²òòFV6ÆFRgVæ6–öâFVÂWfVçFò’‚Ó6’æòÆ†’§7FF–2–çB¶$WdâÒ°¢òò¶$×Dö³¢Æf–&–F†FVÖ÷7G&FòVReTä4”ôäVâW7F7WW&f–6–R††¢òòF—7&FòÂÖVæ÷2VæFV6ÆFW6FRVR6RVçG&ò’âW2ÆòVRFV6–FRV–Và¢òòW67&–&S¢Ö–VçG&26VfÇ6RÖæFÆ'WF6Æ6–6FR6öÇF"Â’Vâ7VçFò6P¢òòöæRG'VRÆ'WFFR6öÇF"FV¦FRW67&–&—"FVÂFöFòâ6’W2”Õõ4”$ÄRVP¢òòÆ2F÷2W67&–&âÆÖ—6ÖFV6Æ†ÆWG&2GWÆ–6F2VâÆ†ö¦’Â’6’VÂæVÀ¢òòæòF–W&VçF÷2×VÇF—ÆW2VÂFV6ÆFò6VwV—&–gVæ6–öææFò–wVÂVR6–V×&Rà§7FF–2&ööÂ¶$×Dö²ÒfÇ6S°§7FF–2–çB¶$×DÖ…G2Ò²òòÖ†–ÖòFRFVF÷2f—7F÷2ÆfW¢†F–væ÷7F–6òFR§W7FW2§7FF–2V–çC3%÷Bt¶%G—–æt×2Ò²òòÖ–ÆÆ—2FVÂVÇF–Öòg&ÖR6öâFVF÷26ö'&RVÂFV6ÆFð §7FF–2fö–B¶$×E&W6WB‚—°¢f÷"†–çBBÒ²BÂ´%õE$4µôÔƒ²B²²—²¶%G&´–E·EÒÒÓ²¶%G&´6VÆÅ·EÒÒÓ²Ð¢¶$WdâÒ°§Ð¢òò&V–æ–6–ò4ôÕÄUDòÂVçG&"VâVæ7WW&f–6–RFRFV6ÆFó¢FVÖ2FRöÇf–F ¢òòÆ÷26öçF7F÷2Â6RgVVÇfRöæW"VâGVF6’Æf–&–FgVæ6–öæâ6’6F¢òòçFÆÆFV6–FR÷"6’Ö—6ÖV–VâW67&–&RÂ’VæÆ67W–òæVÂæòF–W&¢òòVçF÷2×VÇF—ÆW26R6öÆVâVÂ6ö×÷'FÖ–VçFòFR6–V×&Rà§7FF–2fö–B¶$×E7W&f6U&W6WB‚—²¶$×E&W6WB‚“²¶$×Dö²ÒfÇ6S²Ð¢òòFWgVVÇfR7VçF2FV6Æ2åTUd26R†âFö6FòVâW7FRg&ÖRƒ6’æ–æwVæ’à¢òð¢òòõ$DTâDRU45$•EU$†W7FòW2ÆòVR6R–F–ó¢V–VâFö6&–ÖW&òÂW67&–&P¢òò&–ÖW&ò’â6R&W7VVÇfRVâF÷2æ—fVÆW3 ¢òò’VçG&Re$ÔU3¢ÆFV6Æ6RF—7&VâVÂÖ—6Öòg&ÖRVâVR&V6R7P¢òòFVFòÂ’Æ÷2g&ÖW26R&ö6W6âVâ÷&FVââVÂuC“V&Æ–66Fã×2Âð¢òò6VVRF÷2F÷VW26W&F÷2÷"#×26VâVâg&ÖW2F—7F–çF÷2’6ÆVà¢òò4”TÕ$RVâVÂ÷&FVâ6÷'&V7Fòà¢òò"’FVçG&òFRVâÔ•4Ôòg&ÖR†F÷2FVF÷2ÖVæ÷2FRã×2“¢VÂ–ç7FçFR&VÀ¢òò–æò6RVVFR6&W"ÒÒVÂ6†—Æ÷2&W÷'F§VçF÷2Â6–âÖ&6FRF–V×òà¢òò6R&W7WFVÂ÷&FVâVâVRÆ÷2V&Æ–6VÂ&÷–òuC“ÂVRW2VÂ÷&FVà¢òòVâVR7Rf—&×v&RÆ÷2FWFV7Fòâæò6R–çfVçFæFÖV¦÷"VRW6òà§7FF–2–çB¶$×EöÆÂ‚—°¢¶$WdâÒ°¢–b‚´%ôÕTÅD•DõT4…ôôâÇÂt¶$f7EG—R’&WGW&â°¢–çBâÒwEöÆÄ×VÇF’‚“°¢–b†âÂ’&WGW&â²òò6–âFFòWF–Æ—¦&ÆS¢æò6RFö6VÂ6VwV–Ö–VçFð¢–çBÆ—fRÒ°¢f÷"†–çBÒ²Â´%ôÔ…ô”åE3²²²’–b†t¶%ö–çG5·Òæ7F—fR’Æ—fR²³°¢–b†Æ—fRâ¶$×DÖ…G2’¶$×DÖ…G2ÒÆ—fS²òòF–væ÷7F–6ó¢7VçF÷2VçF÷2FFRfW&FBW7FRæVÀ¢&ööÂ6VVå´´%õE$4µôÔ…Ó°¢f÷"†–çBBÒ²BÂ´%õE$4µôÔƒ²B²²’6VVå·EÒÒfÇ6S°¢f÷"†–çBÒ²Â´%ôÔ…ô”åE3²²²—°¢–b‚t¶%ö–çG5·Òæ7F—fR’6öçF–çVS°¢–çB–BÒt¶%ö–çG5·Òæ–BÂ6Æ÷BÒÓ°¢f÷"†–çBBÒ²BÂ´%õE$4µôÔƒ²B²²’–b†¶%G&´–E·EÒÓÒ–B—²6Æ÷BÒC²'&V³²Ð¢–b‡6Æ÷BãÒ—²6VVå·6Æ÷EÒÒG'VS²t¶%G—–æt×2ÒÖ–ÆÆ—2‚“²6öçF–çVS²ÒòòFVFò–6öæö6–Fð¢–çB6VÆÂÒ¶$6VÆÄB†t¶%ö–çG5·Òç‚Ât¶%ö–çG5·Òç’“°¢–çBfâÒ¶$e&÷t†—B†t¶%ö–çG5·Òç‚Ât¶%ö–çG5·Òç’“°¢–b†6VÆÂÂbbfâÂ’6öçF–çVS²òògVW&FVÂFV6ÆFó¢W6òW26÷6FR@¢f÷"†–çBBÒ²BÂ´%õE$4µôÔƒ²B²²’–b†¶%G&´–E·EÒÂ—²6Æ÷BÒC²'&V³²Ð¢–b‡6Æ÷BÂ’6öçF–çVS²òòÆ÷2R‡VV6÷2ö7WF÷0¢¶%G&´–E·6Æ÷EÒÒ–C²¶%G&´6VÆÅ·6Æ÷EÒÒ6VÆÃ²6VVå·6Æ÷EÒÒG'VS°¢t¶%G—–æt×2ÒÖ–ÆÆ—2‚“°¢–b†¶$WdâÂ´%ôUeôÔ‚—²¶$Wd6VÆÅ¶¶$WdåÒÒ6VÆÃ²¶$Wdfå¶¶$WdåÒÒfã²¶$Wdâ²³²Ð¢Ð¢f÷"†–çBBÒ²BÂ´%õE$4µôÔƒ²B²²’–b†¶%G&´–E·EÒãÒbb6VVå·EÒ—²¶%G&´–E·EÒÒÓ²¶%G&´6VÆÅ·EÒÒÓ²Ð¢–b†¶$Wdââ’¶$×Dö²ÒG'VS°¢&WGW&â¶$Wdã°§Ð¢òòG'VRÖ–VçG&26RW7FRFV6ÆVæFòFRfW&FB††’FVF÷26ö'&RÆ&V¦–ÆÆ’âÆð¢òò6öç7VÇFVÂvW7FòFR7W7Vç6–öã¢fW"7W7vW7GW&UWFFRâÆÖ&6ÆöæVà¢òòFçFòÆf–&–F6öÖòÆ÷2F–6·2FRæ÷F2’&Æ÷VVòÂ6’VRfÆRFÖ&–Và¢òò6öâÆW67&—GW&&–FFW67F—fFà§7FF–2&ööÂ¶%G—–ætæ÷r‚—²&WGW&â†Ö–ÆÆ—2‚’Òt¶%G—–æt×2’ÂS²Ð§7FF–2fö–B¶%G—–ætÖ&²‚—²t¶%G—–æt×2ÒÖ–ÆÆ—2‚“²Ð¢òòT”TâU45$”$RâG'VRÒÖæFÆf–&–F†ÂFö6"’’Æ'WFFR6öÇF"äð¢òòW67&–&Ræ’VæÆWG&âW7FW2Æ&VvÆVR†6R–×÷6–&ÆRVR6ÆvâÆWG&0¢òòGWÆ–6F2VâÆ†ö¦¢çVæ6†’F÷26Ö–æ÷27F—f÷2ÆfW¢à§7FF–2&ööÂ¶$f7D7F—fR‚—²&WGW&â´%ôÕTÅD•DõT4…ôôâbbt¶$f7EG—Rbb¶$×Dö³²Ð¢òòG'VR6’W66VÆFF–VæR†÷&Ö—6ÖòVâFVFòVæ6–Ö‡&–çF&Æ‡VæF–F’à§7FF–2&ööÂ¶$6VÆÄ†VÆB†–çB6VÆÂ—°¢–b†6VÆÂÂ’&WGW&âfÇ6S°¢f÷"†–çBBÒ²BÂ´%õE$4µôÔƒ²B²²’–b†¶%G&´–E·EÒãÒbb¶%G&´6VÆÅ·EÒÓÒ6VÆÂ’&WGW&âG'VS°¢&WGW&âfÇ6S°§Ð ¢òò2222222222222222222222222222222222222222222222222222222222220¢òò22d4Rr+rDU5DTÄÄòDRDT4Ä$U4”ôäD¢òò226R&W–çF4ôÄòVÂ&V7FæwVÆòFRW6FV6Æ’6RgVVÆ67P¢òò22&æF¢æ’çFÆÆ6ö×ÆWFÂæ’f–ÆÅ67&VVâÂæ’'FVòà¢òò2222222222222222222222222222222222222222222222222222222222220§7FF–2–çB¶$g„6VÆÂÒÓ²òò6VÆF6öâFW7FVÆÆòf—fð§7FF–2V–çC3%÷B¶$g…CÒ°§7FF–2fö–B¶$g…7F'B†–çB6VÆÂ—°¢–b‚´%ôä”ÕõôÄ•4…ôôâÇÂ6VÆÂÂ’&WGW&ã°¢¶$g„6VÆÂÒ6VÆÃ²¶$g…CÒÖ–ÆÆ—2‚“°§Ð§7FF–2&ööÂ¶$g„7F—fR‚—²&WGW&â´%ôä”ÕõôÄ•4…ôôâbb¶$g„6VÆÂãÒ²Ð¢òò&öw&W6òâã#SRFVÂFW7FVÆÆòƒ#SRÒ&V6–VâFö6FÂÒvFò’à§7FF–2–çB¶$g„ÆWfVÂ†–çB6VÆÂ—°¢–b‚¶$g„7F—fR‚’ÇÂ6VÆÂÒ¶$g„6VÆÂ’&WGW&â°¢V–çC3%÷BGBÒÖ–ÆÆ—2‚’Ò¶$g…C°¢–b‚†–çB–GBãÒt¶$g„×2’&WGW&â°¢&WGW&â#SRÒ†–çB’†GB¢#SRò‡V–çC3%÷B–t¶$g„×2“°§Ð¢òò&W–çFTä6VÆF–ÂVâ7R6—F–òÂ6–âFö6"VÂ&W7FòFVÂFV6ÆFòâW2Æ¢òò–W¦VR†6RVRVÂFW7FVÆÆòæò7VW7FRVâ&W–çFFòVçFW&òà§7FF–2fö–B¶%–çD6VÆÄæ÷r†–çB6VÆÂÂ&ööÂ&W76VBÂV–çCe÷B&rÂV–çCe÷BG‡B—°¢–b†6VÆÂÂ’&WGW&ã°¢–çB"Ò6VÆÂò´%ô4ôÅ2Â2Ò6VÆÂR´%ô4ôÅ3°¢–çB‚Ò´%õ‚²2¢„´%ôµr²´%ôt’Â’Ò´%õ’²"¢„´%ô´‚²´%ôt“°¢6öç7B6†"¢²ÒÖ7F—fõ·%Õ¶5Ó°¢6†"W³eÓ°¢–b†¶%6†–gBbbµ³ÒÓÒbbµ³ÒãÒvrbbµ³ÒÃÒw¢r—²W³ÒÒ†6†"’†µ³ÒÒ3"“²W³ÒÒ²²ÒW²Ð¢6WD'Vb†f"“°¢¶%–çD¶W’‡‚Â’Â´%ôµrÂ´%ô´‚Â²Â¶$föçE6—¦R‚’Â&rÂG‡BÂ&W76VB“°¢fÇ„fÇW6‚‡’ÒÂ’²´%ô´‚²“°§Ð¢òòVæ6–VæFRVÂFW7FVÆÆò’Æò–çFVâVÂ7FòâW2Æ'WF&7VæFòÆ¢òòW67&—GW&&–FW7FvF¢†’VÂfVVF&6²F–VæRVR6Æ—"VâBç&W76VBÀ¢òò÷'VRÆFV6Ææò6RW67&–&R†7F6öÇF"’6–âW7Fòæò†'&–æ–æwVæ¢òò6VæÂFRVRVÂF÷VRÆÆVvòà§7FF–2fö–B¶$g…&W72†–çB6VÆÂÂV–çCe÷B&rÂV–çCe÷BG‡B—°¢–b‚´%ôä”ÕõôÄ•4…ôôâÇÂ6VÆÂÂ’&WGW&ã°¢¶$g…7F'B†6VÆÂ“°¢¶%–çD6VÆÄæ÷r†6VÆÂÂG'VRÂ&rÂG‡B“°§Ð¢òò&W–çFÆFV6ÆFVÂFW7FVÆÆò7VæFò6RvâÆÆÆÖâÆ÷2F–6·2FRÆ0¢òò7WW&f–6–W26öâ5U26öÆ÷&W2‡&–Ö—F—f÷2VâÆf—&ÖÂ6öÖò–FRVÂ&÷–V7Fò’à§7FF–2&ööÂ¶$g…F–6²‡V–çCe÷B&rÂV–çCe÷BG‡B—°¢–b‚¶$g„7F—fR‚’’&WGW&âfÇ6S°¢–b‚†–çB’†Ö–ÆÆ—2‚’Ò¶$g…C’Ât¶$g„×2’&WGW&âfÇ6S°¢–çB6VÆÂÒ¶$g„6VÆÃ²¶$g„6VÆÂÒÓ°¢¶%–çD6VÆÄæ÷r†6VÆÂÂfÇ6RÂ&rÂG‡B“°¢&WGW&âG'VS°§Ð ¢òò2222222222222222222222222222222222222222222222222222222222220¢òò22d4RB+rõ%DTÄU2DRd$”2$åU$0¢òò22ÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÐ¢òò22U%4•5DTä4”†FV6—6–öâW‡Æ–6—F“¢6öÆò6ö'&Wf—fVâÀ¢òò22&V–æ–6–òÆ2&çW&2d”¤D2âVÂ&W7Fò6öâFR6W6–öââ6P¢òò22wV&FâVâåe26öâVæ6ÆfR÷"&çW&‚&6Æ—"ââ&6Æ—"¢òò22FVçG&òFRÆÖ—6ÖæÖW76R&fÆW†÷2"FVÂ&W7FòFVÀ¢òò226—7FVÖâÖ÷F—fó¢VÂ÷'FVÆW2FRG&&¦ò6Ö&–¢òò226öç7FçFVÖVçFR’W67&–&—"fÆ6‚Vâ6F6÷–v7F&–¢òò226–6Æ÷2FRåe2&æF²ÆòVRVÂW7V&–òÖ&66öâVÀ¢òò22–âW2§W7FòÆòVRF–6R&W7FòV–W&ò6öç6W'f&Æò"à¢òò2222222222222222222222222222222222222222222222222222222222220§7FF–26†"6Æ—&ö&E³S%ÒÒ"#²òòÃÃÂ'VffW"6Æ6–6ó¢6–wVRW†—7F–VæFò„´%ô4Ä•$ô$EôÕTÅD•ôôâ’ããà §7FF–2fö–B6Æ—6fU–ææVB‚—°¢–b‚´%ô4Ä•$ô$EôÕTÅD•ôôâ’&WGW&ã°¢&Vg2æ&Vv–â‚&fÆW†÷2"ÂfÇ6R“°¢6†"¶W•³Ó°¢f÷"†–çB’Ò²’Â4Ä•õ4ÄõE3²’²²—°¢6ç&–çFb†¶W’Â6—¦Vöb†¶W’’Â&6Æ—VB"Â’“°¢–b†t6Æ—¶•ÒçW6VBbbt6Æ—¶•Òç–ææVB’&Vg2çWE7G&–ær†¶W’Ât6Æ—¶•ÒçFW‡B“°¢VÇ6R&Vg2ç&VÖ÷fR†¶W’“°¢Ð¢&Vg2æVæB‚“°§Ð§7FF–2fö–B6Æ—ÆöE–ææVB‚—°¢–b‚´%ô4Ä•$ô$EôÕTÅD•ôôâ’&WGW&ã°¢&Vg2æ&Vv–â‚&fÆW†÷2"ÂG'VR“°¢6†"¶W•³Ó°¢f÷"†–çB’Ò²’Â4Ä•õ4ÄõE3²’²²—°¢6ç&–çFb†¶W’Â6—¦Vöb†¶W’’Â&6Æ—VB"Â’“°¢7G&–ær2Ò&Vg2ævWE7G&–ær†¶W’Â""“°¢–b‡2æÆVæwF‚‚’â—°¢2çFô6†$'&’†t6Æ—¶•ÒçFW‡BÂ4Ä•õE…EôÔ‚“°¢t6Æ—¶•ÒçW6VBÒG'VS²t6Æ—¶•Òç–ææVBÒG'VS²t6Æ—¶•ÒçG2Ò°¢Ð¢Ð¢&Vg2æVæB‚“°§Ð§7FF–2–çB6Æ—6÷VçB‚—°¢–çBâÒ°¢f÷"†–çB’Ò²’Â4Ä•õ4ÄõE3²’²²’–b†t6Æ—¶•ÒçW6VB’â²³°¢&WGW&âã°§Ð¢òò–ç6W'FVâFW‡FòçVWfòâ6’æò†’‡VV6òÆ–'&RÂ6ö'&W67&–&RÆ&çW&äð¢òòd”¤DÖ2çF–wVÒÒVæf–¦Fæò6RFW66'F¦Ö2à§7FF–2fö–B6Æ—W6‚†6öç7B6†"¢2—°¢–b‚2ÇÂ5³Ò’&WGW&ã°¢–b‡2Ò6Æ—&ö&B—²7G&æ7’†6Æ—&ö&BÂ2Â6—¦Vöb†6Æ—&ö&B’Ò“²6Æ—&ö&E·6—¦Vöb†6Æ—&ö&B’ÒÒÒ²Ð¢–b‚´%ô4Ä•$ô$EôÕTÅD•ôôâ’&WGW&ã°¢f÷"†–çB’Ò²’Â4Ä•õ4ÄõE3²’²²’òò&WWF–Fó¢6öÆò6R&Vg&W66ÆfV6†¢–b†t6Æ—¶•ÒçW6VBbb7G&æ6×†t6Æ—¶•ÒçFW‡BÂ2Â4Ä•õE…EôÔ‚Ò’—²t6Æ—¶•ÒçG2ÒÖ–ÆÆ—2‚“²&WGW&ã²Ð¢–çB6Æ÷BÒÓ°¢f÷"†–çB’Ò²’Â4Ä•õ4ÄõE3²’²²’–b‚t6Æ—¶•ÒçW6VB—²6Æ÷BÒ“²'&V³²Ð¢–b‡6Æ÷BÂ—°¢V–çC3%÷BöÆFW7BÒ„dddddddgS°¢f÷"†–çB’Ò²’Â4Ä•õ4ÄõE3²’²²’–b‚t6Æ—¶•Òç–ææVBbbt6Æ—¶•ÒçG2ÃÒöÆFW7B—²öÆFW7BÒt6Æ—¶•ÒçG3²6Æ÷BÒ“²Ð¢–b‡6Æ÷BÂ’&WGW&ã²òòÆ2"W7Fâf–¦F3¢æò6RFö6æ–æwVæ¢Ð¢7G&æ7’†t6Æ—·6Æ÷EÒçFW‡BÂ2Â4Ä•õE…EôÔ‚Ò“°¢t6Æ—·6Æ÷EÒçFW‡E´4Ä•õE…EôÔ‚ÒÒÒ°¢t6Æ—·6Æ÷EÒçW6VBÒG'VS²t6Æ—·6Æ÷EÒç–ææVBÒfÇ6S²t6Æ—·6Æ÷EÒçG2ÒÖ–ÆÆ—2‚“°§Ð§7FF–2fö–B6Æ—FVÂ†–çB’—°¢–b†’ÂÇÂ’ãÒ4Ä•õ4ÄõE2’&WGW&ã°¢&ööÂv5–ææVBÒt6Æ—¶•Òç–ææVC°¢t6Æ—¶•ÒçW6VBÒfÇ6S²t6Æ—¶•Òç–ææVBÒfÇ6S²t6Æ—¶•ÒçFW‡E³ÒÒ²t6Æ—¶•ÒçG2Ò°¢–b‡v5–ææVB’6Æ—6fU–ææVB‚“°§Ð§7FF–2fö–B6Æ—FövvÆU–â†–çB’—°¢–b†’ÂÇÂ’ãÒ4Ä•õ4ÄõE2ÇÂt6Æ—¶•ÒçW6VB’&WGW&ã°¢t6Æ—¶•Òç–ææVBÒt6Æ—¶•Òç–ææVC°¢6Æ—6fU–ææVB‚“°§Ð§7FF–2fö–B6Æ—6ÆV%Vç–ææVB‚—°¢f÷"†–çB’Ò²’Â4Ä•õ4ÄõE3²’²²’–b†t6Æ—¶•ÒçW6VBbbt6Æ—¶•Òç–ææVB—²t6Æ—¶•ÒçW6VBÒfÇ6S²t6Æ—¶•ÒçFW‡E³ÒÒ²t6Æ—¶•ÒçG2Ò²Ð§Ð ¢òò2222222222222222222222222222222222222222222222222222222222220¢òò22d4Rb+rUDô4ôÕÄUDDò4”ÕTÄDò„Ä•5DÄô4Âd”¤¢òò22ÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÐ¢òò22TRU3¢F÷2Æ—7F2FRÆ'&2g&V7VVçFW2ÂVæ÷"–F–öÖÀ¢òò22W67&—F2ÖæòVâVÂ&÷–òæ–æòâ6R'W66÷"$Td”¤ò¢òò226Rög&V6Vâ†7F26ö–æ6–FVæ6–2à¢òò22TRäòU3¢VâÖöFVÆòFRÆVæwV¦Râæò&VæFRÂæòVçF–VæFP¢òò22VÂ6öçFW‡Fò’æò&VF–6RÆÆ'&6–wV–VçFRâÆçFÆÆ¢òò22%6ö'&RFV6ÆFò"ÆòF–6R6öâW62Ö—6Ö2Æ'&2ÒÒfVæFW ¢òò22W7Fò6öÖò$”"6W&–ÖVçF—"à¢òò224õ5DS¢ã#SVçG&F2÷"–F–öÖÂVæ÷22´"FRfÆ6‚6F¢òò22Æ—7FâÆ'W7VVFW2Vâ&V6÷'&–FòÆ–æVÂVR6÷'FVà¢òò227VçFò§VçF2&W7VÇFF÷3¢Ö–7&÷6VwVæF÷2÷"FV6Æà¢òò2222222222222222222222222222222222222222222222222222222222220§7FF–26öç7B6†"¢´%ôD”5EôU5µÒÒ°¢&"Â&&¦ò"Â&'&—""Â&66ò"Â&6WF""Â&7VW&Fò"Â&FVÆçFR"Â&FVÕÇ„35Ç„2"Â&wV"Â&†÷&"À¢&Ævò"Â&ÆwV–Vâ"Â&ÆwVæò"Â&ÆÆ’"Â&ÇFò"Â&Ö–vò"Â&Ö÷""Â&çFW2"Â&Ç„35Ç„#ò"Â&v""À¢&Æ–66–öâ"Â&&VæFW""Â&V’"Â&&6†—fò"Â&'&–&"Â&6’"Â&–W""Â&—VF"Â&&¦""Â&&7FçFR"À¢&&–Vâ"Â&&÷'&""Â&'&–ÆÆò"Â&'VVæ2"Â&'VVæò"Â&'W66""Â&6¦"Â&6ÆÆR"Â&6Ö&–""Â&6Ö–æò"À¢&6&v""Â&66"Â&66ò"Â&6VÇVÆ""Â&6W&6"Â&6W'&""Â&6–VÆò"Â&6—VFB"Â&6Æ&ò"Â&6öF–vò"À¢&6öÆ÷""Â&6öÖVç¦""Â&6öÖ–F"Â&6öÖò"Â&6ö×ÆWFò"Â&6ö×'F—""Â&6ö×&""Â&6öâ"Â&6öæV7F""Â&6öæö6W""À¢&6öçF7Fò"Â&6öçG&"Â&6÷–""Â&6÷'&Vò"Â&6÷6"Â&7&V""Â&7VæFò"Â&7VVçF"Â&F""Â&FF÷2"À¢&FV&W""Â&FV6—""Â&FV¦""Â&FVÆçFR"Â&FVçG&ò"Â&FW6FR"Â&FW7UÇ„35Ç„—2"Â&F–"Â&F–æW&ò"Â&F—7÷6—F—fò"À¢&FöæFR"Â&F÷&Ö—""Â&GW&çFR"Â&VÂ"Â&VÆÆ"Â&V×W¦""Â&Væ6VæFW""Â&Væ6öçG&""Â&VçFVæFW""Â&VçFöæ6W2"À¢&VçG&""Â&Vçf–""Â&W'&÷""Â&W67&–&—""Â&W67V6†""Â&W76–ò"Â&W7W&""Â&W7EÇ„35Ç„"Â&W7FR"Â&W7F""À¢&fÇF"Â&fÖ–Æ–"Â&ff÷""Â&fV6†"Â&f–æÂ"Â&f÷&Ö"Â&f÷Fò"Â&gVW&"Â&gVW'FR"Â&gVæ6–öæ""À¢&vVçFR"Â&w&æFR"Â&w&6–2"Â&wV&F""Â&wW7F""Â&†&W""Â&†&Æ""Â&†6W""Â&†7F"Â&†V6†ò"À¢&†öÆ"Â&†öÖ'&R"Â&†÷&"Â&†÷’"Â&–FV"Â&–F–öÖ"Â&–ÖvVâ"Â&–×÷'FçFR"Â&–æf÷&Ö6–öâ"Â&–ç7FÆ""À¢&–çFW&æWB"Â&—""Â&§VVvò"Â&§Vv""Â&§VçFò"Â&ÆFò"Â&Æ&vò"Â&ÆVW""Â&ÆVçFò"Â&ÆWG&"À¢&Æ–'&ò"Â&Æ–×–""Â&ÆÆÖ""Â&ÆÆVv""Â&ÆÆWf""Â&ÇVVvò"Â&ÇVv""Â&ÇW¢"Â&ÖG&R"Â&ÖÂ"À¢&ÖæF""Â&ÖæW&"Â&ÖÇ„35Ç„#"&æ"Â&Öæò"Â&Ö2"Â&Ö–÷""Â&ÖV¦÷""Â&ÖVÖ÷&–"Â&ÖVæ÷2"Â&ÖVç6¦R"À¢&ÖW2"Â&Ö–VçG&2"Â&Ö–çWFò"Â&Ö—&""Â&Ö—6Öò"Â&ÖöFò"Â&ÖöÖVçFò"Â&Ö÷7G&""Â&Ö÷fW""Â&×V6†ò"À¢&×V¦W""Â&×VæFò"Â&×W6–6"Â&×W’"Â&æF"Â&æV6W6—F""Â&æ•Ç„35Ç„#"&ò"Â&æö6†R"Â&æöÖ'&R"Â&æ÷&ÖÂ"À¢&æ÷6÷G&÷2"Â&æ÷F–6–"Â&çVWfò"Â&çVÖW&ò"Â&çVæ6"Â&ö7W'&—""Â&ö—""Â&÷6–öâ"Â&÷&FVâ"Â&÷G&ò"À¢'G&R"Â'v–æ"Â'Æ'&"Â'çFÆÆ"Â'VÂ"Â'&"Â'&V6W""Â''FR"Â'6""Â'VF—""À¢'VÆ–7VÆ"Â'Vç6""Â'WVUÇ„35Ç„#ò"Â'W&FW""Â'W&ò"Â'W'6öæ"Â'ö6ò"Â'öFW""Â'öæW""Â'÷'VR"À¢'÷6–&ÆR"Â'&–ÖW&ò"Â'&ö&""Â'&ö&ÆVÖ"Â'&öçFò"Â'&÷–ò"Â'VçFò"Â'VVF""Â'VW&W""Â'V•Ç„35Ç„–â"À¢'V—F""Â'&–Fò"Â'&¦öâ"Â'&V6–&—""Â'&V–æ–6–""Â'&W7VW7F"Â'&W7VÇFFò"Â'6&W""Â'6Æ—""Â'6VwV—""À¢'6VwVæFò"Â'6VwW&ò"Â'6VÆV66–öæ""Â'6VÖæ"Â'6VçF—""Â'6UÇ„35Ç„#"&Â"Â'6W""Â'6W'f–6–ò"Â'6–V×&R"Â'6–wV–VçFR"À¢'6–ÆVæ6–ò"Â'6—7FVÖ"Â'6—F–ò"Â'6ö'&R"Â'6öÆò"Â'6öæ–Fò"Â'FÖÇ„35Ç„#ò"Â'FÖ&•Ç„35Ç„–â"Â'F&FR"Â'FV6ÆFò"À¢'FVÆVföæò"Â'FVÖ"Â'FVæW""Â'FW‡Fò"Â'F–V×ò"Â'F—ò"Â'Fö6""Â'FöFò"Â'FöÖ""Â'G&&¦ò"À¢'G&W""Â'G&F""Â'VÇF–Öò"Â'W6""Â'W7V&–ò"Â'fÆ÷""Â'fVæ—""Â'fVçFæ"Â'fW""Â'fW&FB"À¢'fW¢"Â'f–¦R"Â'f–F"Â'föÇfW""Â'f÷¢"Â'–"Â'¦öæ"Ó°§7FF–26öç7B6†"¢´%ôD”5EôTåµÒÒ°¢&&÷WB"Â&&÷fR"Â&66WB"Â&66÷VçB"Â&FB"Â&gFW""Â&v–â"Â&v–ç7B"Â&ÆÂ"Â&ÆÆ÷r"À¢&ÆÖ÷7B"Â&Ç6ò"Â&Çv—2"Â&æB"Â&æ÷F†W""Â&ç7vW""Â&ç’"Â&"Â&Ç’"Â&&R"À¢&&÷VæB"Â&6²"Â&v’"Â&&6²"Â&&GFW'’"Â&&V6W6R"Â&&V6öÖR"Â&&VVâ"Â&&Vf÷&R"Â&&Vv–â"À¢&&V†–æB"Â&&V–ær"Â&&VÆ–WfR"Â&&VÆ÷r"Â&&W7B"Â&&WGFW""Â&&WGvVVâ"Â&&–r"Â&&—B"Â&&öö²"À¢&&÷F‚"Â&'&–ær"Â&'V–ÆB"Â&'WGFöâ"Â&'W’"Â&6ÆÂ"Â&6ÖW&"Â&6â"Â&6æ6VÂ"Â&6""À¢&6&R"Â&6''’"Â&66R"Â&6†ævR"Â&6†V6²"Â&6†–ÆB"Â&6†ö÷6R"Â&6—G’"Â&6ÆVâ"Â&6ÆV""À¢&6Æ–6²"Â&6Æ÷6R"Â&6öFR"Â&6öÆB"Â&6öÆ÷""Â&6öÖR"Â&6ö×ç’"Â&6ö×WFW""Â&6öææV7B"Â&6öçF7B"À¢&6öçF–çVR"Â&6÷’"Â&6÷VÆB"Â&6÷VçG'’"Â&7&VFR"Â&7WB"Â&F&²"Â&FF"Â&F’"Â&FVÆWFR"À¢&FWf–6R"Â&F–ffW&VçB"Â&F—7Æ’"Â&FöW2"Â&FöæR"Â&Fö÷""Â&F÷vâ"Â&F÷væÆöB"Â&G&r"Â&G&—fR"À¢&GW&–ær"Â&V6‚"Â&V&Ç’"Â&V7’"Â&VF—B"Â&VÖ–Â"Â&VæB"Â&Væ÷Vv‚"Â&VçFW""Â&W'&÷""À¢&WfVâ"Â&WfW""Â&WfW'’"Â&W†×ÆR"Â&W†—B"Â&f6R"Â&f7B"Â&f–Â"Â&fÖ–Ç’"Â&f""À¢&f7B"Â&fVVÂ"Â&f–VÆB"Â&f–ÆR"Â&f–ÆÂ"Â&f–æB"Â&f–æR"Â&f—'7B"Â&föÆFW""Â&föÆÆ÷r"À¢&föçB"Â&fööB"Â&f÷""Â&f÷&6R"Â&f÷&Ò"Â&g&VR"Â&g&–VæB"Â&g&öÒ"Â&gVÆÂ"Â&vÖR"À¢&vWB"Â&v—fR"Â&vööB"Â&w&VB"Â&w&÷W"Â&†æB"Â&†Vâ"Â&†’"Â&†&B"Â&†fR"À¢&†VB"Â&†V""Â&†VÇ"Â&†W&R"Â&†–v‚"Â&†öÆB"Â&†öÖR"Â&†÷R"Â&†÷W""Â&†÷W6R"À¢&†÷r"Â&–FV"Â&–ÖvR"Â&–×÷'B"Â&–ç6–FR"Â&–ç7FÆÂ"Â&§W7B"Â&¶VW"Â&¶W’"Â&¶W–&ö&B"À¢&¶–æB"Â&¶æ÷r"Â&ÆæwVvR"Â&Æ&vR"Â&Æ7B"Â&ÆFR"Â&ÆV&â"Â&ÆVfR"Â&ÆVgB"Â&ÆW72"À¢&ÆWB"Â&ÆWGFW""Â&ÆWfVÂ"Â&Æ–fR"Â&Æ–v‡B"Â&Æ–¶R"Â&Æ–æR"Â&Æ—7B"Â&Æ—GFÆR"Â&Æ—fR"À¢&ÆöB"Â&Æö6Â"Â&Æö6²"Â&Æöær"Â&Æöö²"Â&Æ÷fR"Â&ÖFR"Â&Ö¶R"Â&Öç’"Â&Ö&²"À¢&Ö’"Â&ÖVâ"Â&ÖVÖ÷'’"Â&ÖVçR"Â&ÖW76vR"Â&Ö–v‡B"Â&Ö–æB"Â&Ö–çWFR"Â&Ö—72"Â&ÖöFR"À¢&ÖöæW’"Â&ÖöçF‚"Â&Ö÷&R"Â&Ö÷&æ–ær"Â&Ö÷7B"Â&Ö÷fR"Â&×V6‚"Â&×W6–2"Â&×W7B"Â&æÖR"À¢&æV""Â&æVVB"Â&æWGv÷&²"Â&æWfW""Â&æWr"Â&æWw2"Â&æW‡B"Â&æ–6R"Â&æ–v‡B"Â&æöæR"À¢&æ÷FR"Â&æ÷F†–ær"Â&æ÷r"Â&çVÖ&W""Â&öfb"Â&öffW""Â&ögFVâ"Â&öæ6R"Â&öæÇ’"Â&÷Vâ"À¢&÷F–öâ"Â&÷&FW""Â&÷F†W""Â&÷fW""Â'vR"Â'W""Â''B"Â'77v÷&B"Â'V÷ÆR"Â'†öæR"À¢'†÷Fò"Â'–6²"Â'Æ6R"Â'Æ’"Â'ÆV6R"Â'ö–çB"Â'÷vW""Â'&W72"Â'&–çB"Â'VW7F–öâ"À¢'V–6²"Â'V—B"Â'&VB"Â'&VG’"Â'&VÂ"Â'&V6öâ"Â'&V6÷&B"Â'&VÖ÷fR"Â'&WVB"Â'&WÇ’"À¢'&W÷'B"Â'&W6WB"Â'&W7B"Â'&W7VÇB"Â'&WGW&â"Â'&–v‡B"Â'&ööÒ"Â''Vâ"Â'6ÖR"Â'6fR"À¢'6’"Â'67&VVâ"Â'6V&6‚"Â'6V6öæB"Â'6VR"Â'6VÆV7B"Â'6VæB"Â'6W'fW""Â'6W'f–6R"Â'6WB"À¢'6WGF–æw2"Â'6†&R"Â'6†÷'B"Â'6†÷VÆB"Â'6†÷r"Â'6–FR"Â'6–vâ"Â'6–æ6R"Â'6—¦R"Â'6ÖÆÂ"À¢'6öÖR"Â'6ööâ"Â'6÷VæB"Â'76R"Â'7V²"Â'7F'B"Â'7FFR"Â'7F’"Â'7FW"Â'7F–ÆÂ"À¢'7F÷"Â'7F÷&R"Â'7F÷'’"Â'7GVG’"Â'7V6‚"Â'7W÷'B"Â'7W&R"Â'7—7FVÒ"Â'F&ÆR"Â'F¶R"À¢'FÆ²"Â'FVÆÂ"Â'FW7B"Â'FW‡B"Â'F†â"Â'F†æ²"Â'F†B"Â'F†V—""Â'F†VÒ"Â'F†Vâ"À¢'F†W&R"Â'F†W6R"Â'F†W’"Â'F†–ær"Â'F†–æ²"Â'F†—2"Â'F–ÖR"Â'FöF’"Â'FövWF†W""Â'Föò"À¢'FööÂ"Â'F÷V6‚"Â'G'’"Â'GW&â"Â'G—R"Â'VæFW""Â'VçF–Â"Â'WFFR"Â'WÆöB"Â'W6R"À¢'W6W""Â'fW'’"Â'f–Wr"Â'v—B"Â'vÆ²"Â'vçB"Â'vF6‚"Â'vFW""Â'v’"Â'vVV²"À¢'vVÆÂ"Â'v†B"Â'v†Vâ"Â'v†W&R"Â'v†–6‚"Â'v†–ÆR"Â'v†—FR"Â'v†ò"Â'v‡’"Â'v–ÆÂ"À¢'v–æF÷r"Â'v—F‚"Â'v÷&B"Â'v÷&²"Â'v÷&ÆB"Â'v÷VÆB"Â'w&—FR"Â'–V""Â'–W2"Â'–÷W""Ó°¢6FVf–æR´%ôD”5EôU5ôâ‚†–çB’‡6—¦Vöb„´%ôD”5EôU2’ò6—¦Vöb„´%ôD”5EôU5³Ò’’¢6FVf–æR´%ôD”5EôTåôâ‚†–çB’‡6—¦Vöb„´%ôD”5EôTâ’ò6—¦Vöb„´%ôD”5EôTå³Ò’’ ¢òòFWgVVÇfRVÂ6–wV–VçFR6&7FW"'ÆVvFò#¢Ö–çW67VÆ’6–â6VçFòâ6¢òòW67&–&—"&Ö2"Væ7VVçG&&ÕÇ„35Ç„2"’&Öâ"Væ7VVçG&&ÖÇ„35Ç„#"&æ"ÂVRW2ÆòVP¢òòW7W&7VÇV–W&VRW67&–&&–Fò6–â&'6RöæW"F–ÆFW2à§7FF–26†"¶$föÆD6‚†6öç7B6†"¢¢2—°¢6öç7B6†"¢2Ò§3°¢Vç6–væVB6†"2Ò‡Vç6–væVB6†"—5³Ó°¢–b†2ÓÒ—²&WGW&â²Ð¢–b†2Âƒƒ—²§2Ò2²²&WGW&â†6†"’‚†2ãÒtrbb2ÃÒu¢r’ò2²3"¢2“²Ð¢–b†2ÓÒ„32bb5³Ò—°¢Vç6–væVB6†"BÒ‡Vç6–væVB6†"—5³Ó°¢§2Ò2²#°¢–b‚†BãÒƒƒbbBÃÒƒƒR’ÇÂ†BãÒ„bbBÃÒ„R’’&WGW&âvs°¢–b‚†BãÒƒƒ‚bbBÃÒƒ„"’ÇÂ†BãÒ„‚bbBÃÒ„"’’&WGW&âvRs°¢–b‚†BãÒƒ„2bbBÃÒƒ„b’ÇÂ†BãÒ„2bbBÃÒ„b’’&WGW&âv’s°¢–b‚†BãÒƒ“"bbBÃÒƒ“b’ÇÂ†BãÒ„#"bbBÃÒ„#b’’&WGW&âvòs°¢–b‚†BãÒƒ“’bbBÃÒƒ”2’ÇÂ†BãÒ„#’bbBÃÒ„$2’’&WGW&âwRs°¢–b†BÓÒƒ“ÇÂBÓÒ„#’&WGW&âvâs²òòâöâ6öâf—&wVÆ–ÆÆ¢&WGW&âsòs°¢Ð¢2²³²v†–ÆR‚‚‡Vç6–væVB6†"’§2b„3’ÓÒƒƒ’2²³²òò÷G&ò×VÇF–'—FS¢6R6ÇFVçFW&ð¢§2Ò3²&WGW&âsòs°§Ð§7FF–2&ööÂ¶%7F'G5v—F‚†6öç7B6†"¢v÷&BÂ6öç7B6†"¢&Vb—°¢6öç7B6†"¢rÒv÷&C²6öç7B6†"¢Ò&Vc°¢f÷"ƒ³²—°¢6öç7B6†"¢Ò²6†"2Ò¶$föÆD6‚‚g“°¢–b‡2ÓÒ’&WGW&âG'VS²òòVÂ&Vf–¦ò6R6&ó¢Væ6¦¢6öç7B6†"¢wrÒs²6†"v2Ò¶$föÆD6‚‚gwr“°¢–b‡v2ÓÒÇÂv2Ò2’&WGW&âfÇ6S°¢Ò²rÒws°¢Ð§Ð§7FF–2&ööÂ¶%6ÖUv÷&B†6öç7B6†"¢Â6öç7B6†"¢"—°¢6öç7B6†"¢‚Ò²6öç7B6†"¢’Ò#°¢f÷"ƒ³²—°¢6öç7B6†"¢‡‚Òƒ²6†"2Ò¶$föÆD6‚‚g‡‚“°¢6öç7B6†"¢—’Ò“²6†"&2Ò¶$föÆD6‚‚g—’“°¢–b†2Ò&2’&WGW&âfÇ6S°¢–b†2ÓÒ’&WGW&âG'VS°¢‚Ò‡ƒ²’Ò—“°¢Ð§Ð¢òò&Wf—6–öâ÷'Föw&f–6&6–6„f6RRôb“¢&6öæö6–F"ÒW7FVâVÂF–66–öæ&–ð¢òòFVÂ–F–öÖ7F—fòòW2VæòFRÆ÷2F¦÷2FVÂW7V&–òâW2U„5DÔTåDRW6òÂæð¢òòVâ6÷'&V7F÷"w&ÖF–6Âà§7FF–2&ööÂ¶$F–7D†2†6öç7B6†"¢r—°¢–b‚rÇÂu³Ò’&WGW&âG'VS°¢6öç7B6†"¢6öç7B¢BÒ¶$ÆætW2ò´%ôD”5EôU2¢´%ôD”5EôTã°¢–çBâÒ¶$ÆætW2ò´%ôD”5EôU5ôâ¢´%ôD”5EôTåôã°¢f÷"†–çB’Ò²’Âã²’²²’–b†¶%6ÖUv÷&B†E¶•ÒÂr’’&WGW&âG'VS°¢f÷"†–çB’Ò²’Â´%õ45ôÔƒ²’²²’–b†t¶%64'%¶•Õ³Òbb¶%6ÖUv÷&B†t¶%64'%¶•ÒÂr’’&WGW&âG'VS°¢&WGW&âfÇ6S°§Ð¢òòVÖö¦—27VvW&–F÷2‡6öÆòvÆ–f÷2VRÆgVVçFR–F–'V¦ÂÆ÷2Ö—6Ö÷2FP¢òòÄ”õUEôTÔô¤’’âW2VæF&ÆF—7&F÷"ÓæVÖ÷F–6öâÂæòVâ6Æ6–f–6F÷"à¢6FVf–æR´%ôTÔõ5Tuôâ §7FF–26öç7B6†"¢´%ôTÔõ5Tuõu´´%ôTÔõ5TuôåÒÒ²'&—6"Â&Ö÷""Â'G&—7FR"Â&wV–æò"Â&'&¦ò"Â&ÆVv‚"Â&Æ÷fR"Â'6B"Â'v–æ²"Â&‡Vr"Ó°§7FF–26öç7B6†"¢´%ôTÔõ5TuôU´´%ôTÔõ5TuôåÒÒ²#¤B"Â#Ã2"Â#¢‚"Â#²’"Â"‡’’"Â#¤B"Â#Ã2"Â#¢‚"Â#²’"Â"‡’’"Ó° ¢òò'W66†7FÖ†â6ö–æ6–FVæ6–2÷"&Vf–¦òâ÷&FVã¢&–ÖW&òÆ÷2F¦÷2FP¢òòFW‡FòFVÂW7V&–ò‡Væ'&Wf–6–öâW67&—FVçFW&væ7VÇV–W"Æ'&’À¢òòÇVVvòVÂF–66–öæ&–òÂÇVVvòVÂVÖö¦’7VvW&–Fò6’Fö6à§7FF–2–çB¶%7VvvW7B†6öç7B6†"¢&VbÂ6öç7B6†"¢¢÷WBÂ–çBÖ†â—°¢–çBâÒ°¢–b‚´%ôUDô4ôÕÄUDUôôâÇÂt¶%&VF–7BÇÂ&VbÇÂ&Ve³ÒÇÂÖ†âÃÒ’&WGW&â°¢f÷"†–çB’Ò²’Â´%õ45ôÔ‚bbâÂÖ†ã²’²²¢–b†t¶%64'%¶•Õ³Òbbt¶%64W‡¶•Õ³Òbb¶%6ÖUv÷&B†t¶%64'%¶•ÒÂ&Vb’’÷WE¶â²µÒÒt¶%64W‡¶•Ó°¢6öç7B6†"¢6öç7B¢BÒ¶$ÆætW2ò´%ôD”5EôU2¢´%ôD”5EôTã°¢–çBFâÒ¶$ÆætW2ò´%ôD”5EôU5ôâ¢´%ôD”5EôTåôã°¢f÷"†–çB’Ò²’ÂFâbbâÂÖ†ã²’²²—°¢–b‚¶%7F'G5v—F‚†E¶•ÒÂ&Vb’’6öçF–çVS°¢&ööÂGWÒfÇ6S°¢f÷"†–çB²Ò²²Âã²²²²’–b†¶%6ÖUv÷&B†÷WE¶µÒÂE¶•Ò’’GWÒG'VS°¢–b‚GW’÷WE¶â²µÒÒE¶•Ó°¢Ð¢–b†t¶$VÖö¦•7VrbbâÂÖ†â¢f÷"†–çB’Ò²’Â´%ôTÔõ5TuôâbbâÂÖ†ã²’²²¢–b†¶%7F'G5v—F‚„´%ôTÔõ5Tuõu¶•ÒÂ&Vb’—²÷WE¶â²µÒÒ´%ôTÔõ5TuôU¶•Ó²'&V³²Ð¢&WGW&âã°§Ð¢òòÆ'&Vâ6öç7G'V66–öã¢FVÂVÇF–ÖòW76–ò÷6ÇFò†7FVÂ7W'6÷"â6÷–¢òò÷WB‡&–Ö—F—f÷2VâÆf—&Ö’’FWgVVÇfR7RÆöæv—GVBVâ'—FW2à§7FF–2–çB¶$7W'&VçEv÷&B†6öç7B6†"¢'VbÂ–çB7W"Â6†"¢÷WBÂ–çB÷WG7¢—°¢÷WE³ÒÒ°¢–b‚'VbÇÂ7W"ÃÒÇÂ÷WG7¢ÃÒ’&WGW&â°¢–çBÒ7W#°¢v†–ÆR†â—°¢Vç6–væVB6†"2Ò‡Vç6–væVB6†"–'Ve¶ÒÓ°¢–b†2ÓÒrrÇÂ2ÓÒuÆârÇÂ2ÓÒuÇBr’'&V³°¢ÒÓ°¢Ð¢–çBâÒ7W"Ò²–b†ââ÷WG7¢Ò’âÒ÷WG7¢Ò°¢ÖVÖ7’†÷WBÂ'Vb²Ââ“²÷WE¶åÒÒ°¢&WGW&âã°§Ð §7FF–26†"æ÷FT'VffW%³S%ÒÒ"#°¢òòW7FFòFVÂ÷×WFR6VçF÷0§7FF–2–çB¶$Ç¶W’ÒÓÂ¶%÷‚ÒÂ¶%÷’ÒÂ¶%÷âÒÂ¶%÷rÒCÂ¶%÷rÒC°§7FF–2&ööÂ¶%÷WÒfÇ6S° ¢òòÒÒÒÒ–ç6W&6–öâö&÷'&FòUDbÓ‚6VwW&÷2ÒÒÒÐ¢òòÒÒÒÒÖöFVÆòFRFW‡FòVF—F&ÆR†7W'6÷"²6VÆV66–öâ’²õ%DTÄU2tÄô$ÂÒÒÒÐ§7FF–2–çBæ÷FT7W"Ò²òò7W'6÷"†–æF–6RVâ'—FW2§7FF–2–çBæ÷FU6VÄÒÓÂæ÷FU6VÄ"ÒÓ²òò6VÆV66–öââä"Vâ'—FW2‚ÓÒæ–æwVæ§7FF–2&ööÂæ÷FTÖVçRÒfÇ6S²òòÖVçR6öçFW‡GVÂf—6–&ÆP§7FF–2–çBæ÷FT†æFÆTG&rÒ²òòæòÂÖæ–¦—§Â"FW ¢òò†VÂ÷'FVÆW2f—fR†÷&'&–&¢'VffW"6Æ6–6ò²Æ2"&çW&2FRÆf6RB §7FF–2–çBWFc…&Wb†6öç7B6†"¢2Â–çB’—²–b†’ÃÒ’&WGW&â²’ÒÓ²v†–ÆR†’âbb‡5¶•Òb„3’ÓÒƒƒ’’ÒÓ²&WGW&â“²Ð§7FF–2–çBWFc„æW‡B†6öç7B6†"¢2Â–çB’—²–çBÂÒ7G&ÆVâ‡2“²–b†’ãÒÂ’&WGW&âÃ²’²³²v†–ÆR†’ÂÂbb‡5¶•Òb„3’ÓÒƒƒ’’²³²&WGW&â“²Ð§7FF–2&ööÂ—5v÷&D'—FR‡Vç6–væVB6†"2—²&WGW&â2ãÒƒƒÇÂ†2ãÒvrbb2ÃÒw¢r’ÇÂ†2ãÒtrbb2ÃÒu¢r’ÇÂ†2ãÒsrbb2ÃÒs’r“²Ð§7FF–2&ööÂæ÷FT†56VÂ‚—²&WGW&âæ÷FU6VÄãÒbbæ÷FU6VÄ"âæ÷FU6VÄ²Ð§7FF–2fö–Bæ÷FT6ÆV%6VÂ‚—²æ÷FU6VÄÒæ÷FU6VÄ"ÒÓ²æ÷FTÖVçRÒfÇ6S²Ð§7FF–2fö–Bæ÷FTFVÆWFU6VÂ‚—°¢–b‚æ÷FT†56VÂ‚’’&WGW&ã°¢–çBÒæ÷FU6VÄÂ"Òæ÷FU6VÄ"ÂÂÒ7G&ÆVâ†æ÷FT'VffW"“°¢ÖVÖÖ÷fR†æ÷FT'VffW"²Âæ÷FT'VffW"²"ÂÂÒ"²“°¢æ÷FT7W"Ò²æ÷FT6ÆV%6VÂ‚“°§Ð§7FF–2fö–Bæ÷FT–ç6W'B†6öç7B6†"¢2—²òò–ç6W'FVâVÂ7W'6÷"‡&VV×Æ¦6VÆV66–öâ6’†’¢–b†æ÷FT†56VÂ‚’’æ÷FTFVÆWFU6VÂ‚“°¢–çBÂÒ7G&ÆVâ†æ÷FT'VffW"’Â6ÂÒ7G&ÆVâ‡2“°¢–b„Â²6ÂãÒ†–çB—6—¦Vöb†æ÷FT'VffW"’Ò’&WGW&ã°¢–b†æ÷FT7W"Â’æ÷FT7W"Ò²–b†æ÷FT7W"âÂ’æ÷FT7W"ÒÃ°¢ÖVÖÖ÷fR†æ÷FT'VffW"²æ÷FT7W"²6ÂÂæ÷FT'VffW"²æ÷FT7W"ÂÂÒæ÷FT7W"²“°¢ÖVÖ7’†æ÷FT'VffW"²æ÷FT7W"Â2Â6Â“°¢æ÷FT7W"³Ò6Ã²æ÷FTÖVçRÒfÇ6S°§Ð§7FF–2fö–Bæ÷FT&6·76R‚—²òò&÷'&çFW2FVÂ7W'6÷"†×VÇF–'—FR’òÆ6VÆV66–öà¢–b†æ÷FT†56VÂ‚’—²æ÷FTFVÆWFU6VÂ‚“²&WGW&ã²Ð¢–b†æ÷FT7W"ÃÒ’&WGW&ã°¢–çBÒWFc…&Wb†æ÷FT'VffW"Âæ÷FT7W"’ÂÂÒ7G&ÆVâ†æ÷FT'VffW"“°¢ÖVÖÖ÷fR†æ÷FT'VffW"²Âæ÷FT'VffW"²æ÷FT7W"ÂÂÒæ÷FT7W"²“°¢æ÷FT7W"Ò²æ÷FTÖVçRÒfÇ6S°§Ð¢òòd4Rs¢6†—fÆ÷FçFR$6÷–Fò"âæò&Æ÷VVæFÒÒW2VæÖ&6FRF–V×ð¢òòVRVÂF–6²FRæ÷F2Ö—&’&÷'&6öÆÆ÷2ãã"2à§7FF–2V–çC3%÷B¶%Fö7D×2Ò°§7FF–26†"¶%Fö7EG‡E³#EÒÒ"#°§7FF–2fö–B¶%Fö7B†6öç7B6†"¢B—°¢6ç&–çFb†¶%Fö7EG‡BÂ6—¦Vöb†¶%Fö7EG‡B’Â"W2"ÂB“°¢¶%Fö7D×2ÒÖ–ÆÆ—2‚“°¢–b‚´%ôä”ÕõôÄ•4…ôôâ’¶%Fö7D×2Ò²òò6–âæ–Ö6–öæW3¢æ’Fö7Bæ’6÷'FRÂ6–×ÆVÖVçFRæò6ÆP§Ð¢òòd4RC¢6÷–"†÷&Ä”ÔTåDÆ2"&çW&2FVÖ2FVÂ'VffW"6Æ6–6òÂ6¢òòVR6Æ—&ö&EµÒ6–wVR6–VæFòfÆ–FòVçVRVÂ–çFW''WF÷"W7FRà§7FF–2fö–B6Æ—6÷’‚—°¢–b‚æ÷FT†56VÂ‚’’&WGW&ã°¢–çBÒæ÷FU6VÄÂ"Òæ÷FU6VÄ"ÂâÒ"Ò°¢–b†âãÒ†–çB—6—¦Vöb†6Æ—&ö&B’’âÒ6—¦Vöb†6Æ—&ö&B’Ò°¢ÖVÖ7’†6Æ—&ö&BÂæ÷FT'VffW"²Ââ“²6Æ—&ö&E¶åÒÒ°¢6Æ—W6‚†6Æ—&ö&B“°¢¶%Fö7B‚$6÷–Fò"“°§Ð§7FF–2fö–B6Æ—7WB‚—²6Æ—6÷’‚“²æ÷FTFVÆWFU6VÂ‚“²Ð§7FF–2fö–B6Æ—7FR‚—²–b†6Æ—&ö&E³Ò’æ÷FT–ç6W'B†6Æ—&ö&B“²Ð§7FF–2fö–B6VÆV7DÆÅG‡B‚—²æ÷FU6VÄÒ²æ÷FU6VÄ"Ò7G&ÆVâ†æ÷FT'VffW"“²æ÷FT7W"Òæ÷FU6VÄ#²æ÷FTÖVçRÒæ÷FT†56VÂ‚“²Ð§7FF–2fö–B6VÆV7Ev÷&DB†–çB&’—°¢–çBÂÒ7G&ÆVâ†æ÷FT'VffW"“²–b„ÂÓÒ’&WGW&ã°¢–b†&’ãÒÂ’&’ÒWFc…&Wb†æ÷FT'VffW"ÂÂ“°¢–çBÒ&“²v†–ÆR†â—²–çBÒWFc…&Wb†æ÷FT'VffW"Â“²–b‚—5v÷&D'—FR‚‡Vç6–væVB6†"–æ÷FT'VffW%·Ò’’'&V³²Ò²Ð¢–çB"Ò&“²v†–ÆR†"ÂÂ—²–b‚—5v÷&D'—FR‚‡Vç6–væVB6†"–æ÷FT'VffW%¶%Ò’’'&V³²"ÒWFc„æW‡B†æ÷FT'VffW"Â"“²Ð¢–b†"â—²æ÷FU6VÄÒ²æ÷FU6VÄ"Ò#²æ÷FT7W"Ò#²æ÷FTÖVçRÒG'VS²Ð§Ð§7FF–2fö–B¶%&W746†"†6öç7B6†"¢2—°¢–b†¶%6†–gBbb5³ÒÓÒbb5³ÒãÒvrbb5³ÒÃÒw¢r—²6†"U³%ÒÒ²†6†"’‡5³ÒÒ3"’ÂÓ²æ÷FT–ç6W'B‡R“²¶%6†–gBÒfÇ6S²Ð¢VÇ6R–b†¶%6†–gBbb7G&6×‡2Â%Ç„35Ç„#"’—²æ÷FT–ç6W'B‚%Ç„35Çƒ“"“²¶%6†–gBÒfÇ6S²Ð¢VÇ6Ræ÷FT–ç6W'B‡2“°§Ð¢òòf&–çFW26VçGVF2‡6öÆòÆ2VRF–VæRÆgVVçFR’âFWgVVÇfRVÂçVÖW&òà§7FF–2–çB¶$vWEf&–çG2†6†""Â6öç7B6†"¢f%³EÒ—°¢7v—F6‚†"—°¢66Rvs¢f%³ÓÒ%Ç„35Ç„#²f%³ÓÒ%Ç„35Ç„#²f%³%ÓÒ%Ç„35Ç„"#²f%³5ÓÒ%Ç„35Ç„2#²&WGW&âC°¢66RvRs¢f%³ÓÒ%Ç„35Ç„’#²f%³ÓÒ%Ç„35Ç„‚#²f%³%ÓÒ%Ç„35Ç„#²&WGW&â3°¢66Rv’s¢f%³ÓÒ%Ç„35Ç„B#²f%³ÓÒ%Ç„35Ç„2#²f%³%ÓÒ%Ç„35Ç„R#²&WGW&â3°¢66Rvòs¢f%³ÓÒ%Ç„35Ç„#2#²f%³ÓÒ%Ç„35Ç„#"#²f%³%ÓÒ%Ç„35Ç„#B#²f%³5ÓÒ%Ç„35Ç„#R#²&WGW&âC°¢66RwRs¢f%³ÓÒ%Ç„35Ç„$#²f%³ÓÒ%Ç„35Ç„#’#²f%³%ÓÒ%Ç„35Ç„$"#²f%³5ÓÒ%Ç„35Ç„$2#²&WGW&âC°¢Ð¢&WGW&â°§Ð§7FF–2&ööÂ¶$—5f÷vVÄ6VÆÂ†–çB6VÆÂ—°¢–b†6VÆÂÂÇÂ†Ö7F—fòÓÒÄ”õUEôU2ÇÂÖ7F—fòÓÒÄ”õUEôTâ’’&WGW&âfÇ6S°¢6öç7B6†"¢²ÒÖ7F—fõ¶6VÆÂò´%ô4ôÅ5Õ¶6VÆÂR´%ô4ôÅ5Ó°¢&WGW&âµ³ÒÓÒbb†µ³ÓÓÒvwÇÆµ³ÓÓÒvRwÇÆµ³ÓÓÒv’wÇÆµ³ÓÓÒvòwÇÆµ³ÓÓÒwRr“°§Ð ¢òòÆ–Ö—FRFR&¦òFVÂ$TDRDU…DòâçFW2W&´%õ’Ó‚W67&—FòÖæòVâ6–æ6ð¢òò6—F–÷3²†÷&6ÆRFVÂæVÂFVÂFV6ÆFòÂVRVVFR7&V6W"†6–'&–&6¢òòW7FâÆ&'&FRÆf6R2òÆ÷26†—2FRÆf6Rbà§7FF–2–çBæ÷FUG‡D&÷B‚—²&WGW&â¶%æVÅF÷‚’Òƒ²Ð§7FF–2–çB7W%5‚Â7W%5’Â„5‚Â„5’Â„%5‚Â„%5’Âæ÷FTÖVçU‚Âæ÷FTÖVçU“°§7FF–2fö–Bæ÷FTG&uFW‡B‚—°¢6WD'Vb†f"“°¢f–ÆÅ&V7Bƒ‚ÂC‚Â45%õrÒbÂæ÷FUG‡D&÷B‚’ÒC‚Â&v#ScRƒ#BÃ#bÃ3B’“°¢–çB‚Ò‚Â’ÒcÂÖ…‚Ò45%õrÒ‚ÂÆ‚Ò#bÂ6—¦RÒ#°¢fÆöB62ÒföçE62‡6—¦R“°¢6öç7B6†"¢2Òæ÷FT'VffW#²–çB&’Ò°¢&ööÂ†56VÂÒæ÷FT†56VÂ‚“°¢–çB”'&V²Òæ÷FUG‡D&÷B‚’Ò##°¢7W%5‚Òƒ²7W%5’Òc²„5‚Ò„%5‚Òƒ²„5’Ò„%5’Òc°¢òòd4RbÒ&Wf—6–öâ÷'Föw&f–6&6–6¢6R6–wVRÆÆ'&Vâ7W'6ò†FöæFP¢òòV×W¦ò’VâVRÆ–æV’’Â6W'&&Æ6R7V'&–6öâVçF—F÷26’äòW7FVà¢òòVÂF–66–öæ&–òFVÂ–F–öÖ7F—fòâW2W6ò’æFÖ3¢6ö×&6–öâ6öçG&Væ¢òòÆ—7FÆö6ÂÂ6–âw&ÖF–6æ’7VvW&Væ6–2FR6÷'&V66–öâà¢&ööÂ7VÆÄöâÒ„´%ôUDô4ôÕÄUDUôôâbbt¶%7VÆÂ“°¢–çBw5‚Ò‚Âw5’Ò’Âw4&’Ò²&ööÂ–åv÷&BÒfÇ6S°¢v†–ÆR‚§2—°¢–b‚§2ÓÒuÆâr—°¢–b‡7VÆÄöâbb–åv÷&B—°¢6†"wF×³CÓ²–çBvâÒ&’Òw4&“²–b‡vââ3’’vâÒ3“°¢ÖVÖ7’‡wF×Âæ÷FT'VffW"²w4&’Âvâ“²wF×·våÒÒ°¢–b‡w5’ÓÒ’bbvâãÒ2bb¶$F–7D†2‡wF×’’f÷"†–çB‚Òw5ƒ²‚Âƒ²‚³Ò2’f–ÆÅ&V7B‡‚Â’²Æ‚ÒrÂ"Â"Â&v#ScRƒ##bÃ“bÃ“b’“°¢–åv÷&BÒfÇ6S°¢Ð¢–b†&’ÓÒæ÷FT7W"—²7W%5‚Òƒ²7W%5’Ò“²Ò–b†&’ÓÒæ÷FU6VÄ—²„5‚Òƒ²„5’Ò“²Ò–b†&’ÓÒæ÷FU6VÄ"—²„%5‚Òƒ²„%5’Ò“²Ð¢2²³²&’²³²‚Òƒ²’³ÒÆƒ²–b‡’â”'&V²’'&V³²6öçF–çVS°¢Ð¢6öç7B6†"¢6fRÒ3²V–çC3%÷B7ÒæW‡D5‚g2“²–çBæ"Ò2Ò6fS°¢–çBrÒ†–çB’„du¶föçD–G‚†7•ÒæGb¢62²ãVb“°¢–b‡‚²râÖ…‚—²‚Òƒ²’³ÒÆƒ²–b‡’â”'&V²’'&V³²Ð¢–b‡7VÆÄöâ—°¢&ööÂv"Ò—5v÷&D'—FR‚‡Vç6–væVB6†"’§6fR“°¢–b‡v"bb–åv÷&B—²–åv÷&BÒG'VS²w5‚Òƒ²w5’Ò“²w4&’Ò&“²Ð¢VÇ6R–b‚v"bb–åv÷&B—°¢6†"wF×³CÓ²–çBvâÒ&’Òw4&“²–b‡vââ3’’vâÒ3“°¢ÖVÖ7’‡wF×Âæ÷FT'VffW"²w4&’Âvâ“²wF×·våÒÒ°¢–b‡w5’ÓÒ’bbvâãÒ2bb¶$F–7D†2‡wF×’’f÷"†–çB‚Òw5ƒ²‚Âƒ²‚³Ò2’f–ÆÅ&V7B‡‚Â’²Æ‚ÒrÂ"Â"Â&v#ScRƒ##bÃ“bÃ“b’“°¢–åv÷&BÒfÇ6S°¢Ð¢Ð¢–b†&’ÓÒæ÷FT7W"—²7W%5‚Òƒ²7W%5’Ò“²Ò–b†&’ÓÒæ÷FU6VÄ—²„5‚Òƒ²„5’Ò“²Ò–b†&’ÓÒæ÷FU6VÄ"—²„%5‚Òƒ²„%5’Ò“²Ð¢–b††56VÂbb&’ãÒæ÷FU6VÄbb&’Âæ÷FU6VÄ"’f–ÆÅ&V7B‡‚ÒÂ’Ò"Âr²ÂÆ‚ÒBÂ&v#ScRƒC‚Ã“"Ãc‚’“²òò&W6ÇFFð¢6†"öæU³eÓ²–çBâÒæ#²–b†ââR’âÒS²f÷"†–çB’Ò²’Âã²’²²’öæU¶•ÒÒ6fU¶•Ó²öæU¶åÒÒ°¢G&uFW‡B‡‚Â’ÂöæRÂ6—¦RÂ&v#ScRƒ#3RÃ#3‚Ã#Cb’“°¢‚³Òs²&’³Òæ#°¢Ð¢òòVÇF–ÖÆ'&FVÂFW‡Fòâ6’VÂ7W'6÷"W7F§W7Fò†’W2VR6RW7F¢òòW67&–&–VæFòDôDd”¢Ö&6&ÆVâ&ö¦òÖ–VçG&26RFV6ÆV6W&–'V–FòW&òÀ¢òò6’VRW66RFV¦Vâ¢†7FVR6R6–W'&R6öâVâW76–òà¢–b‡7VÆÄöâbb–åv÷&Bbbæ÷FT7W"Ò&’—°¢6†"wF×³CÓ²–çBvâÒ&’Òw4&“²–b‡vââ3’’vâÒ3“°¢ÖVÖ7’‡wF×Âæ÷FT'VffW"²w4&’Âvâ“²wF×·våÒÒ°¢–b‡w5’ÓÒ’bbvâãÒ2bb¶$F–7D†2‡wF×’’f÷"†–çB‚Òw5ƒ²‚Âƒ²‚³Ò2’f–ÆÅ&V7B‡‚Â’²Æ‚ÒrÂ"Â"Â&v#ScRƒ##bÃ“bÃ“b’“°¢Ð¢–b†&’ÓÒæ÷FT7W"—²7W%5‚Òƒ²7W%5’Ò“²Ò–b†&’ÓÒæ÷FU6VÄ—²„5‚Òƒ²„5’Ò“²Ò–b†&’ÓÒæ÷FU6VÄ"—²„%5‚Òƒ²„%5’Ò“²Ð¢–b††56VÂ—²òòÖæ–¦2†v÷F2'&7G&&ÆW2¢dÆ–æR†„5‚Â„5’Ò"Â#BÂ&v#ScRƒ“ÃSÃ#C’“²f–ÆÄ6—&6ÆR†„5‚Â„5’²#BÂrÂ&v#ScRƒ“ÃSÃ#C’“°¢dÆ–æR†„%5‚Â„%5’Ò"Â#BÂ&v#ScRƒ“ÃSÃ#C’“²f–ÆÄ6—&6ÆR†„%5‚Â„%5’²#BÂrÂ&v#ScRƒ“ÃSÃ#C’“°¢ÒVÇ6R°¢f–ÆÅ&V7B†7W%5‚²Â7W%5’Ò"Â"Â#"Â&v#ScRƒ“ÃSÃ#C’“²òò7W'6÷ ¢Ð¢–b†æ÷FTÖVçR—²òòÖVçR6öçFW‡GVÂfÆ÷FçFP¢6öç7B6†"¢—E³EÒÒ²$6÷'F""Â$6÷–""Â%Vv""Â%FöFò"Ó°¢–çB'rÒ“"ÂvÒBÂF÷BÒB¢'r²2¢vÂ×‚Ò…45%õrÒF÷B’ò"Â×’Ò„5’ÒCC²–b†×’ÂS’×’ÒS°¢æ÷FTÖVçU‚Ò×ƒ²æ÷FTÖVçU’Ò×“°¢f–ÆÅ&÷VæE&V7B†×‚ÒbÂ×’ÒbÂF÷B²"ÂCÂ‚Â&v#ScRƒ3‚ÃC"ÃSb’“°¢f÷"†–çB’Ò²’ÂC²’²²—²–çB'‚Ò×‚²’¢†'r²v“²f–ÆÅ&÷VæE&V7B†'‚Â×’Â'rÂ#‚ÂbÂ&v#ScRƒS‚ÃcBÃƒB’“²G&uFW‡D2†'‚²'rò"Â×’²rÂ—E¶•ÒÂ"Â&v#ScRƒ#CÃ#CBÃ#S"’“²Ð¢Ð¢òòd4RrÒ6†—fÆ÷FçFR$6÷–Fò#¢f—fRDTåE$òFVÂ&VFRFW‡FòÂ6’VR6P¢òò&÷'&6öÆò6öâVÂ6–wV–VçFR&W–çFFòFRW7FÖ—6Ö&æFâ6RFW7fæV6RVà¢òòVÂVÇF–ÖòFW&6–òVâfW¢FRFW6&V6W"FRvöÇRà¢–b„´%ôä”ÕõôÄ•4…ôôâbb¶%Fö7D×2—°¢V–çC3%÷BGBÒÖ–ÆÆ—2‚’Ò¶%Fö7D×3°¢–b†GBÂ#—°¢V–çC…÷BÒ†GBÂƒ’ò#3¢‡V–çC…÷B’ƒ#3Ò†GBÒƒ’¢#3òC“°¢–çBGrÒFW‡Er†¶%Fö7EG‡BÂ"’²3BÂG‚Ò…45%õrÒGr’ò"ÂG’Òæ÷FUG‡D&÷B‚’ÒCc°¢f–ÆÅ&÷VæE&V7D‡G‚ÂG’ÂGrÂ3"ÂbÂ&v#ScRƒ#‚Ã3"ÃCB’Â“°¢G&uFW‡D4…45%õrò"ÂG’²’Â¶%Fö7EG‡BÂ"Â&v#ScRƒ#3RÃ#CÃ#S’Â“°¢Ð¢Ð¢òò6RgVVÆ6†7FVÂ&÷&FRÖ—6ÖòFVÂæVÂFVÂFV6ÆFó¢VçG&RVÂf–æÂFVÀ¢òò&VFRFW‡Fò’¶%æVÅF÷‚’†’Væ÷2—†VÆW2FRföæFòVR6’æòVVF&–à¢òòVâF–W'&FRæF–R†æ’W7F&æFæ’ÆFVÂFV6ÆFòÆ÷2V&Æ–6&–’à¢fÇ„fÇW6‚ƒCBÂ¶%æVÅF÷‚’Ò“°§Ð¢òòÖVVâF÷VR‡‚Ç’’Â–æF–6RFR'—FRÖ26W&6æòVâVÂFW‡Fð§7FF–2–çBæ÷FTÆ–÷WD†—B†–çB‚Â–çB’—°¢–çB‚Ò‚Â’ÒcÂÖ…‚Ò45%õrÒ‚ÂÆ‚Ò#bÂ6—¦RÒ#²fÆöB62ÒföçE62‡6—¦R“°¢6öç7B6†"¢2Òæ÷FT'VffW#²–çB&’ÒÂ&W7BÒ²Æöær&W7FBÒÂÃÂ3°¢v†–ÆR‚§2—°¢–b‚§2ÓÒuÆâr—²ÆöærBÒ†Æöær–'2‡‚Ò‚’²†Æöær–'2‡’Ò‡’²‚’’¢#²–b†BÂ&W7FB—²&W7FBÒC²&W7BÒ&“²Ò2²³²&’²³²‚Òƒ²’³ÒÆƒ²6öçF–çVS²Ð¢6öç7B6†"¢6fRÒ3²V–çC3%÷B7ÒæW‡D5‚g2“²–çBæ"Ò2Ò6fS°¢–çBrÒ†–çB’„du¶föçD–G‚†7•ÒæGb¢62²ãVb“°¢–b‡‚²râÖ…‚—²‚Òƒ²’³ÒÆƒ²Ð¢ÆöærCÒ†Æöær–'2‡‚Ò‚’²†Æöær–'2‡’Ò‡’²‚’’¢#²–b†CÂ&W7FB—²&W7FBÒC²&W7BÒ&“²Ð¢ÆöærCÒ†Æöær–'2‡‚Ò‡‚²r’’²†Æöær–'2‡’Ò‡’²‚’’¢#²–b†CÂ&W7FB—²&W7FBÒC²&W7BÒ&’²æ#²Ð¢‚³Òs²&’³Òæ#°¢Ð¢ÆöærBÒ†Æöær–'2‡‚Ò‚’²†Æöær–'2‡’Ò‡’²‚’’¢#²–b†BÂ&W7FB—²&W7FBÒC²&W7BÒ&“²Ð¢&WGW&â&W7C°§Ð§7FF–2–çBæ÷FTÖVçT†—B†–çB‚Â–çB’—°¢–b‚æ÷FTÖVçRÇÂ’Âæ÷FTÖVçU’ÇÂ’âæ÷FTÖVçU’²#‚’&WGW&âÓ°¢–çB'rÒ“"ÂvÒC°¢f÷"†–çB’Ò²’ÂC²’²²—²–çB'‚Òæ÷FTÖVçU‚²’¢†'r²v“²–b‡‚ãÒ'‚bb‚ÃÒ'‚²'r’&WGW&â“²Ð¢&WGW&âÓ°§Ð¢òòFV6ÆFRgVæ6–öââÖ—6Öf—&ÖFR6–V×&R†ÆW6â&Æ÷VVò’v’Ôf’’ÂW&ò÷ ¢òòFVçG&ò–6÷"¶%–çD¶W“¢†W&VFW7F–ÆòÂ6öçG&7FRÇFò’FÖæòFP¢òògVVçFRFRÆf6RR6–âVRW62G&W27WW&f–6–W2FVævâVRVçFW&'6Rà§7FF–2fö–B¶$d¶W’†–çB‚Â–çBg’Â–çBrÂ6öç7B6†"¢Æ&VÂÂ&ööÂöâ—°¢¶%–çD¶W’‡‚Âg’ÂrÂ´%ô´‚ÂÆ&VÂÂ¶$föçE6—¦R‚’â"ò"¢¶$föçE6—¦R‚’À¢öâò¶$6öÄfäöâ‚’¢¶$6öÄfâ‚’Âöâò¶$6öÄfäöåG‡B‚’¢¶$6öÄ¶W•G‡B‚’ÂfÇ6R“°§Ð¢òòWF—VWFFRÆFV6ÆFR6ƒó#2òVÖö¦’ò$2’ÂVâVâ6öÆò6—F–òà§7FF–26öç7B6†"¢¶$Æ–W$Æ&VÂ‚—°¢&WGW&â†Ö7F—fòÓÒÄ”õUEôåTÒ’ò&VÖö¦’"¢†Ö7F—fòÓÒÄ”õUEôTÔô¤’’ò$$2"¢#ó#2#°§Ð§7FF–2fö–Bæ÷FTG&tgVæ5&÷r†–çB–öfb—°¢–çBg’Ò¶$gVæ5’‚’²–öfc°¢6öç7B6†"¢Æ%´´%ôd´U•5ÒÒ²'6†–gB"Â¶$Æ–W$Æ&VÂ‚’Â¶$ÆætW2ò$U2"¢$Tâ"Â&W76–ò"Â#ÂÒ"Â&VçB"Ó°¢f÷"†–çB’Ò²’Â´%ôd´U•3²’²²’¶$d¶W’†¶$d¶W•‚†’’Âg’Â¶$d¶W•r†’’ÂÆ%¶•ÒÂ†’ÓÒ’bb¶%6†–gB“°§Ð ¢òò2222222222222222222222222222222222222222222222222222222222220¢òò22d4R2+r$%$5UU$”õ"DR44U4õ2$”Dõ0¢òò22VÖö¦’+r–F–öÖ+r÷'FVÆW2+r§W7FW2+rÖ2÷6–öæW0¢òò22ÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÐ¢òò22–6öæ÷2dT5Dõ$”ÄU26öâÆ2&–Ö—F—f2FVÂ&÷–òÖ÷F÷ ¢òò22†æFFR76WG2çVWf÷2’â6öÆò6RF–'V¦Vâæ÷F3¢VâÆ¢òò22çFÆÆFR6öçG&6VæVâ&÷FöâFR÷'FVÆW2òFP¢òò22§W7FW26W&–Væf–FRW66RÂæòVæ6öÖöF–FBà¢òò2222222222222222222222222222222222222222222222222222222222220¢6FVf–æR´%õD%ôTÔô¤’ ¢6FVf–æR´%õD%ôÄär¢6FVf–æR´%õD%ô4Ä• ¢6FVf–æR´%õD%õ4UB0¢6FVf–æR´%õD%ôÔõ$R@¢6FVf–æR´%õD%ôâP§7FF–2–çB¶%FööÅ‚†–çB’—²òò6VçG&ò‚FVÂ&÷Föâ¢–çB7FWÒ45%õrò´%õD%ôã°¢&WGW&â7FWò"²’¢7FW°§Ð§7FF–2–çB¶%FööÄ†—B†–çB‚Â–çB’—°¢–b†¶%FööÆ&$‚‚’ÓÒ’&WGW&âÓ°¢–çB“Ò¶%FööÆ&%’‚“°¢–b‡’Â“ÇÂ’â“²S"’&WGW&âÓ°¢f÷"†–çB’Ò²’Â´%õD%ôã²’²²’–b†'2‡‚Ò¶%FööÅ‚†’’’ÃÒ#b’&WGW&â“°¢&WGW&âÓ°§Ð§7FF–2fö–B¶%FööÄ–6öâ†–çB–G‚Â–çB7‚Â–çB7’ÂV–çCe÷B6öÂ—°¢7v—F6‚†–G‚—°¢66R´%õD%ôTÔô¤“¢òò6&—F¢G&t6—&6ÆR†7‚Â7’ÂÂ6öÂ“²G&t6—&6ÆR†7‚Â7’ÂÂ6öÂ“°¢f–ÆÄ6—&6ÆR†7‚ÒBÂ7’Ò2Â"Â6öÂ“²f–ÆÄ6—&6ÆR†7‚²BÂ7’Ò2Â"Â6öÂ“°¢&57G&ö¶R†7‚Â7’²ÂbÂ#ÂcÂ"Â6öÂ“²'&V³°¢66R´%õD%ôÄäs¢òòvÆö&ò†6Ö&–"–F–öÖ¢G&t6—&6ÆR†7‚Â7’ÂÂ6öÂ“°¢„Æ–æR†7‚ÒÂ7’Â#"Â6öÂ“°¢&57G&ö¶R†7‚Â7’ÂÂ#SÂ#“Â"Â6öÂ“²&57G&ö¶R†7‚Â7’ÂÂsÂÂ"Â6öÂ“°¢dÆ–æR†7‚Â7’ÒÂ#"Â6öÂ“²'&V³°¢66R´%õD%ô4Ä•¢òò÷'FVÆW0¢G&u&÷VæE&V7B†7‚Ò‚Â7’ÒÂbÂ#Â2Â6öÂ“°¢f–ÆÅ&÷VæE&V7B†7‚ÒRÂ7’Ò2ÂÂRÂ"Â6öÂ“°¢„Æ–æR†7‚ÒBÂ7’ÒÂ‚Â6öÂ“²„Æ–æR†7‚ÒBÂ7’²BÂ‚Â6öÂ“²'&V³°¢66R´%õD%õ4UC¢òòVæw&æ¦P¢G&t6—&6ÆR†7‚Â7’ÂRÂ6öÂ“°¢f÷"†–çB²Ò²²Âc²²²²—²fÆöBÒ²¢ãCs“sfc°¢7G&ö¶U6Vt†7‚²6÷6b†’¢rÂ7’²6–æb†’¢rÂ7‚²6÷6b†’¢Â7’²6–æb†’¢Â"ã&bÂ6öÂ“²Ð¢'&V³°¢FVfVÇC¢òòÖ2÷6–öæW0¢f–ÆÄ6—&6ÆR†7‚Ò‚Â7’Â"Â6öÂ“²f–ÆÄ6—&6ÆR†7‚Â7’Â"Â6öÂ“²f–ÆÄ6—&6ÆR†7‚²‚Â7’Â"Â6öÂ“²'&V³°¢Ð§Ð§7FF–2fö–B¶$G&uFööÆ&"†–çB–öfb—°¢–b†¶%FööÆ&$‚‚’ÓÒ’&WGW&ã°¢–çB“Ò¶%FööÆ&%’‚’²–öfbÂ7’Ò“²#c°¢f÷"†–çB’Ò²’Â´%õD%ôã²’²²—°¢–çB7‚Ò¶%FööÅ‚†’“°¢f–ÆÄ6—&6ÆR†7‚Â7’Â#Ât¶$†”6öâò&v#ScRƒ#BÃ#BÃ#B’¢&v#ScRƒS"ÃS‚Ãsb’“°¢¶%FööÄ–6öâ†’Â7‚Â7’Â¶$6öÄ¶W•G‡B‚’“°¢Ð§Ð ¢òò2222222222222222222222222222222222222222222222222222222222220¢òò22d4Rb+re$ä¤DR4„•2‡7VvW&Væ6–2ò6–Ö&öÆ÷2¢òò22Vâ62FRÆWG&3¢†7F2Æ'&2FVÂF–66–öæ&–òÆö6Âà¢òò22VâÆ6çVÖW&–6¢Æ÷2B6–Ö&öÆ÷2W'6öæÆ—¦F÷2„f6RR’À¢òò22ò6V'VâFW‡G&"FW6FRó#2Â6öÖò6R–F–òà¢òò2222222222222222222222222222222222222222222222222222222222220§7FF–26öç7B6†"¢¶$6†—G‡E³EÓ°§7FF–2–çB¶$6†—âÒ°§7FF–2V–çC3%÷B¶$6†—×2Ò²òò7VæFò6Ö&–òÆÆ—7F‡&VÂgVæF–FòFRÆf6Rr§7FF–2fö–B¶$6†—4'V–ÆB‚—°¢6öç7B6†"¢&We³EÓ²–çB&WdâÒ¶$6†—ã°¢f÷"†–çB’Ò²’Â&Wdâbb’ÂC²’²²’&We¶•ÒÒ¶$6†—G‡E¶•Ó°¢¶$6†—âÒ°¢–b‚¶$6†—5vçB‚’’&WGW&ã°¢–b†Ö7F—fòÓÒÄ”õUEôåTÒ—°¢f÷"†–çB’Ò²’Â´%õ5”Õ3²’²²’¶$6†—G‡E¶¶$6†—â²µÒÒ¶%7–ÔB†’“°¢ÒVÇ6R°¢6†"u³CÓ°¢–b†¶$7W'&VçEv÷&B†æ÷FT'VffW"Âæ÷FT7W"ÂrÂ6—¦Vöb‡r’’â’¶$6†—âÒ¶%7VvvW7B‡rÂ¶$6†—G‡BÂ2“°¢Ð¢&ööÂ6†ævVBÒ†¶$6†—âÒ&Wdâ“°¢f÷"†–çB’Ò²’Â¶$6†—âbb6†ævVC²’²²’–b†¶$6†—G‡E¶•ÒÒ&We¶•Ò’6†ævVBÒG'VS°¢–b†6†ævVB’¶$6†—×2ÒÖ–ÆÆ—2‚“°§Ð§7FF–2–çB¶$6†—†—B†–çB‚Â–çB’—°¢–b†¶$6†—4‚‚’ÓÒÇÂ¶$6†—âÃÒ’&WGW&âÓ°¢–çB“Ò¶$6†—5’‚“°¢–b‡’Â“ÇÂ’â“²3"’&WGW&âÓ°¢–çB7rÒ…45%õrÒ"’ò¶$6†—ã°¢f÷"†–çB’Ò²’Â¶$6†—ã²’²²—²–çB‚Òb²’¢7s²–b‡‚ãÒ‚bb‚ÃÒ‚²7r’&WGW&â“²Ð¢&WGW&âÓ°§Ð§7FF–2fö–B¶$G&t6†—2†–çB–öfb—°¢–b†¶$6†—4‚‚’ÓÒ’&WGW&ã°¢–çB“Ò¶$6†—5’‚’²–öfc°¢òòd4Rs¢Æ÷26†—2æò&V6VâFRvöÇRÂVçG&â6öâVâgVæF–Fò6÷'Fòà¢V–çC…÷BÒ#SS°¢–b„´%ôä”ÕõôÄ•4…ôôâbb¶$6†—×2—°¢V–çC3%÷BGBÒÖ–ÆÆ—2‚’Ò¶$6†—×3°¢–b†GBÂC’Ò‡V–çC…÷B’ƒc²GB¢“RòC“°¢Ð¢–b†¶$6†—âÃÒ’&WGW&ã°¢–çB7rÒ…45%õrÒ"’ò¶$6†—ã°¢f÷"†–çB’Ò²’Â¶$6†—ã²’²²—°¢–çB‚Òb²’¢7s°¢–b†’â’f–ÆÅ&V7D‡‚Â“²‚ÂÂbÂ¶$6öÄVFvR‚’Â#“°¢G&uFW‡D4‡‚²7rò"Â“²‚Â¶$6†—G‡E¶•ÒÂ"Â¶$6öÄ¶W•G‡B‚’Â“°¢Ð§Ð ¢òòF–'V¦VÂFV6ÆFò6ö×ÆWFò‡æVÂ²W‡G&2²FV6Æ2’FW7Æ¦Fò–öfb—†VÆW0¢òò†6–&¦òâ–öfbÒ6öÆòGW&çFRÆæ–Ö6–öâFRW'GW&FRÆf6Rrà§7FF–2fö–Bæ÷FU&VæFW$¶W–&ö&B†–çB–öfb—°¢6WD'Vb†f"“°¢–çBF÷Ò¶%æVÅF÷‚’Â’ÒF÷²–öfc°¢òò$õ%$Dòô$Ä”tDõ$”òDRÄ$äDÂ4”TÕ$RâW7Fò'&VvÆVÂ'FV6ÆFòfçF6Ö¢òò&÷'&÷6òFWG&2FVÂFV6ÆFò"’Æ÷26†—2VR6RVÖ&÷'&öæ&âÖ26öâ6F¢òòFV6Æ ¢òòG&tÆ—V–DvÆ75æVÂ4õ”ÆòVR†’FV&¦òFVÂæVÂÂÆòFW6Væfö6’Æð¢òòÖW¦6Æâ6öÖòV’6RF–'V¦F—&V7FÖVçFR6ö'&Rf"Â&ÆòVR†’FV&¦ò"W&¢òòVÂFV6ÆFòFVÂ5TE$òåDU$”õ"â&W7VÇFFó¢6F&W–çFFòFW6Væfö6&VÀ¢òòFV6ÆFòçFW&–÷"’ÆòFV¦&VvFòÂföæFòÂ’Â&W–çF"÷G&fW ¢òòFW6Væfö6&VÂFW6Væf÷VRâââ÷"W6òV×V÷&&FV6ÆFV6Æà¢òò6öâVÂföæFòÆæòFV&¦òÂVÂf–G&–ò×VW7G&V6–V×&RÆòÖ—6ÖòVR7VæFð¢òò6R&W–çFÆçFÆÆVçFW&¢–FVçF–6ò7V7FòÂ6W&ò7V×VÆ6–öâà¢f–ÆÅ&V7BƒÂF÷Ò"Â45%õrÂ45%ô‚Ò‡F÷Ò"’Â&v#ScRƒ"ÃBÃ#’“°¢¶%–çEæVÂ‡’ÂV”vÆ72ò¶$6öÅæVÂ‚’¢†t¶$†”6öâò&v#ScRƒÃÃ’¢&v#ScRƒ‚Ã#Ã#‚’’“°¢¶$G&uFööÆ&"‡–öfb“°¢¶$G&t6†—2‡–öfb“°¢–çBg2Ò¶$föçE6—¦R‚“°¢f÷"†–çB"Ò²"Â´%õ$õu3²"²²’f÷"†–çB2Ò²2Â´%ô4ôÅ3²2²²—°¢–çB‚Ò´%õ‚²2¢„´%ôµr²´%ôt’Â’Ò´%õ’²"¢„´%ô´‚²´%ôt’²–öfc°¢6öç7B6†"¢²ÒÖ7F—fõ·%Õ¶5Ó°¢6†"U³eÓ°¢–b†¶%6†–gBbbµ³ÒÓÒbbµ³ÒãÒvrbbµ³ÒÃÒw¢r—²U³ÒÒ†6†"’†µ³ÒÒ3"“²U³ÒÒ²²ÒS²Ð¢–çB6VÆÂÒ"¢´%ô4ôÅ2²3°¢&ööÂ†÷BÒ¶$6VÆÄ†VÆB†6VÆÂ’ÇÂ¶$g„ÆWfVÂ†6VÆÂ’â°¢¶%–çD¶W’‡‚Â’Â´%ôµrÂ´%ô´‚Â²Âg2Â¶$6öÄ¶W’‚’Â¶$6öÄ¶W•G‡B‚’Â†÷B“°¢Ð¢æ÷FTG&tgVæ5&÷r‡–öfb“°¢fÇ„fÇW6‚‡F÷Ò"Â45%ô‚Ò“°§Ð§7FF–2fö–Bæ÷FU&VæFW$ÆÂ‚—°¢6WD'Vb†f"“°¢f–ÆÅ&V7BƒÂÂ45%õrÂ45%ô‚Â&v#ScRƒ"ÃBÃ#’“°¢7G&ö¶U6Vtƒ3Â#bÂ‚Â‚Â"ãFbÂ&v#ScRƒ#SRÃ#SRÃ#SR’“°¢7G&ö¶U6Vtƒ‚Â‚Â3ÂÂ"ãFbÂ&v#ScRƒ#SRÃ#SRÃ#SR’“°¢G&uFW‡D2…45%õrò"ÂBÂ$æ÷F2"Â2Â&v#ScRƒ#SRÃ#SRÃ#SR’“°¢¶$6†—4'V–ÆB‚“°¢æ÷FTG&uFW‡B‚“°¢æ÷FU&VæFW$¶W–&ö&Bƒ“°¢fÇ„fÇW6„ÆÂ‚“°§Ð§7FF–2fö–B¶%&VæFW%÷W†–çB6VÆÂ—°¢–çB"Ò6VÆÂò´%ô4ôÅ2Â2Ò6VÆÂR´%ô4ôÅ3°¢–çB·‚Ò´%õ‚²2¢„´%ôµr²´%ôt’Â·’Ò´%õ’²"¢„´%ô´‚²´%ôt“°¢6öç7B6†"¢f%³EÓ²–çBâÒ¶$vWEf&–çG2†Ö7F—fõ·%Õ¶5Õ³ÒÂf"“°¢–b†âÓÒ’&WGW&ã°¢–çBrÒCÂ‚ÒCbÂvÒBÂF÷GrÒâ¢r²†âÒ’¢v°¢–çBƒÒ·‚²´%ôµrò"ÒF÷Grò#²–b‡ƒÂB’ƒÒC²–b‡ƒ²F÷Grâ45%õrÒB’ƒÒ45%õrÒBÒF÷Gs°¢–çB“Ò·’Ò‚Ò°¢¶%÷‚Òƒ²¶%÷’Ò“²¶%÷âÒã²¶%÷rÒs²¶%÷rÒv°¢6WD'Vb†f"“°¢f–ÆÅ&÷VæE&V7B‡ƒÒbÂ“ÒbÂF÷Gr²"Â‚²"ÂÂ&v#ScRƒCÃCBÃS‚’“°¢f÷"†–çB’Ò²’Âã²’²²—°¢–çB‚Òƒ²’¢‡r²v“°¢f–ÆÅ&÷VæE&V7B‡‚Â“ÂrÂ‚Â‚Â&v#ScRƒcBÃc‚Ãƒb’“°¢G&uFW‡D2‡‚²rò"Â“²‚ò"Ò"Âf%¶•ÒÂ2Â&v#ScRƒ#SRÃ#SRÃ#SR’“°¢Ð¢fÇ„fÇW6‚‡“Ò‚Â·’²´%ô´‚“°§Ð§7FF–2–çB¶%÷W†—B†–çB‚Â–çB’—°¢f÷"†–çB’Ò²’Â¶%÷ã²’²²—²–çB‚Ò¶%÷‚²’¢†¶%÷r²¶%÷r“²–b‡‚ãÒ‚bb‚ÃÒ‚²¶%÷rbb’ãÒ¶%÷’bb’ÃÒ¶%÷’²Cb’&WGW&â“²Ð¢&WGW&âÓ°§Ð¢òò66–öâFRVæFV6ÆFReTä4”ôâƒâãR’âVâ6öÆò6—F–ò&Æ2F÷2'WF2FP¢òòVçG&F¢VÂF—7&ò&–FòFRÆf6R"’VÂF—7&ò6Æ6–6òÂ6öÇF"à§7FF–2fö–Bæ÷FTgVæ4¶W’†–çB’—°¢–b†’ÓÒ’¶%6†–gBÒ¶%6†–gC°¢VÇ6R–b†’ÓÒ—²òò6–6Æ$2ÓâåTÒÓâTÔô¤¢–b†Ö7F—fòÓÒÄ”õUEôåTÒ’Ö7F—fòÒÄ”õUEôTÔô¤“°¢VÇ6R–b†Ö7F—fòÓÒÄ”õUEôTÔô¤’’Ö7F—fòÒ¶$ÆætW2òÄ”õUEôU2¢Ä”õUEôTã°¢VÇ6RÖ7F—fòÒÄ”õUEôåTÓ°¢Ð¢VÇ6R–b†’ÓÒ"—²¶$ÆætW2Ò¶$ÆætW3²–b†Ö7F—fòÓÒÄ”õUEôU2ÇÂÖ7F—fòÓÒÄ”õUEôTâ’Ö7F—fòÒ¶$ÆætW2òÄ”õUEôU2¢Ä”õUEôTã²Ð¢VÇ6R–b†’ÓÒ2’æ÷FT–ç6W'B‚""“°¢VÇ6R–b†’ÓÒB’æ÷FT&6·76R‚“°¢VÇ6R–b†’ÓÒR’æ÷FT–ç6W'B‚%Æâ"“°§Ð¢òòd4Rc¢6WF"Vâ6†—âVâÆ6çVÖW&–6VÂ6†—W2Vâ6–Ö&öÆò’6P¢òò–ç6W'FFÂ7VÃ²VâÆ2FRÆWG&25U5D•EU”RÆÆ'&Vâ6öç7G'V66–öâ¢òòFV¦VâW76–òFWG&2à§7FF–2fö–B¶$Ç”6†—†–çB’—°¢–b†’ÂÇÂ’ãÒ¶$6†—â’&WGW&ã°¢–b†Ö7F—fòÓÒÄ”õUEôåTÒ—²æ÷FT–ç6W'B†¶$6†—G‡E¶•Ò“²&WGW&ã²Ð¢6öç7B6†"¢rÒ¶$6†—G‡E¶•Ó°¢6†"F×³CÓ°¢v†–ÆR†¶$7W'&VçEv÷&B†æ÷FT'VffW"Âæ÷FT7W"ÂF×Â6—¦Vöb‡F×’’â’æ÷FT&6·76R‚“°¢æ÷FT–ç6W'B‡r“²æ÷FT–ç6W'B‚""“°§Ð ¢òò2222222222222222222222222222222222222222222222222222222222220¢òò22d4RB+räTÂDRõ%DTÄU2‡&V¦–ÆÆFR"6öÇVÖæ2¢òò22FÒVv"+r–âÒf–¦"÷6öÇF"+r‚Ò&÷'&"W6f–6†¢òò226&V6W&¢föÇfW"+rf–ÇG&òFRf–¦F÷2+rf6–"†6öâf—6ò¢òò2222222222222222222222222222222222222222222222222222222222220§7FF–2&ööÂ6Æ—æVÄöâÒfÇ6S°§7FF–2&ööÂ6Æ—f–ÇFW%–âÒfÇ6S°§7FF–2&ööÂ6Æ—6´6ÆV"ÒfÇ6S²òò6R–F–òf6–#¢Æ6&V6W&–FR6öæf—&Ö6–öà§7FF–2–çB6Æ—f—5´4Ä•õ4ÄõE5ÒÂ6Æ—f—4âÒ°§7FF–2fö–B6Æ—'V–ÆEf—2‚—°¢6Æ—f—4âÒ°¢f÷"†–çB’Ò²’Â4Ä•õ4ÄõE3²’²²—°¢–b‚t6Æ—¶•ÒçW6VB’6öçF–çVS°¢–b†6Æ—f–ÇFW%–âbbt6Æ—¶•Òç–ææVB’6öçF–çVS°¢6Æ—f—5¶6Æ—f—4â²µÒÒ“°¢Ð§Ð§7FF–2fö–B6Æ—6&E&V7B†–çB²Â–çBg‚Â–çBg’Â–çBgrÂ–çBf‚—°¢–çB6öÂÒ²R"Â&÷rÒ²ò#°¢rÒ…45%õrÒ2¢"’ò#²‚Òc°¢‚Ò"²6öÂ¢‡r²"“°¢’Ò‚²&÷r¢†‚²"“°§Ð§7FF–2fö–B6Æ—&VæFW%æVÂ‚—°¢6Æ—'V–ÆEf—2‚“°¢6WD'Vb†f"“°¢f–ÆÅ&V7BƒÂÂ45%õrÂ45%ô‚Â&v#ScRƒ"ÃBÃ#’“°¢òò6&V6W&ÂÂW7—&—GRFRÆ6GW&¢–6öæòFRFV6ÆFòÆ—§V–W&FÂ–â¢òòVÆW&ÆFW&V6†à¢f–ÆÅ&V7BƒÂÂ45%õrÂcBÂ&v#ScRƒ#Ã#2Ã3"’“°¢G&u&÷VæE&V7BƒBÂ#"Â#bÂ#ÂBÂ&v#ScRƒ#3RÃ#3‚Ã#Cb’“°¢f÷"†–çB’Ò²’Â3²’²²’f–ÆÅ&V7Bƒ’²’¢rÂ#‚ÂBÂ2Â&v#ScRƒ#3RÃ#3‚Ã#Cb’“°¢„Æ–æRƒ#Â3bÂBÂ&v#ScRƒ#3RÃ#3‚Ã#Cb’“°¢G&uFW‡BƒSBÂ#Â%÷'FVÆW2"Â2Â&v#ScRƒ#SRÃ#SRÃ#SR’“°¢²–çB7‚Ò45%õrÒ“bÂ7’Ò3#²òò–â†f–ÇG&f–¦F÷2¢f–ÆÄ6—&6ÆR†7‚Â7’Â#Â6Æ—f–ÇFW%–âò&v#ScRƒcÃÃ#3R’¢&v#ScRƒCÃCRÃc’“°¢f–ÆÅ&V7B†7‚Ò"Â7’Ò"ÂBÂ"Â&v#ScRƒ#CÃ#C"Ã#C‚’“°¢f–ÆÅ&÷VæE&V7B†7‚ÒrÂ7’ÒÂBÂ’Â2Â&v#ScRƒ#CÃ#C"Ã#C‚’“²Ð¢²–çB7‚Ò45%õrÒC"Â7’Ò3#²òòVÆW&‡f6–"æòf–¦F÷2¢f–ÆÄ6—&6ÆR†7‚Â7’Â#Â6Æ—6´6ÆV"ò&v#ScRƒ#ÃsÃs’¢&v#ScRƒCÃCRÃc’“°¢f–ÆÅ&÷VæE&V7B†7‚Ò‚Â7’ÒbÂbÂRÂ2Â&v#ScRƒ#CÃ#C"Ã#C‚’“°¢f–ÆÅ&V7B†7‚ÒÂ7’Ò’Â#Â2Â&v#ScRƒ#CÃ#C"Ã#C‚’“°¢f–ÆÅ&V7B†7‚Ò2Â7’Ò"ÂbÂ2Â&v#ScRƒ#CÃ#C"Ã#C‚’“²Ð¢–b†6Æ—6´6ÆV"—°¢G&uFW‡D2…45%õrò"ÂsBÂ%Fö6÷G&fW¢ÆVÆW&&f6–"†Æ÷2f–¦F÷26RVVFâ’"ÂÂ&v#ScRƒ#CÃƒÃ#’“°¢ÒVÇ6R°¢6†"7V%³cEÓ²6ç&–çFb‡7V"Â6—¦Vöb‡7V"’Â"VBf—6–&ÆW2ÒVBFRVB&çW&2VâW6òW2"À¢6Æ—f—4âÂ6Æ—6÷VçB‚’Â4Ä•õ4ÄõE2Â6Æ—f–ÇFW%–âò"Òf–ÇG&ó¢f–¦F2"¢""“°¢G&uFW‡D2…45%õrò"ÂsbÂ7V"ÂÂ&v#ScRƒSÃS‚Ãs‚’“°¢Ð¢–b†6Æ—f—4âÓÒ—°¢G&uFW‡D2…45%õrò"Â3Â$æF6÷–FòFöFf–"Â"Â&v#ScRƒSÃS‚Ãs‚’“°¢G&uFW‡D2…45%õrò"Â33Â%6VÆV66–öæFW‡Fò’Fö66÷–""ÂÂ&v#ScRƒÃ‚Ã3‚’“°¢Ð¢f÷"†–çB²Ò²²Â6Æ—f—4âbb²Âƒ²²²²—°¢–çB‚Â’ÂrÂƒ²6Æ—6&E&V7B†²Â‚Â’ÂrÂ‚“°¢–çB’Ò6Æ—f—5¶µÓ°¢–b‡V”vÆ72’G&tÆ—V–DvÆ75æVÂ‡‚Â’ÂrÂ‚Â"Â&v#ScRƒCBÃSÃc‚’“°¢VÇ6Rf–ÆÅ&÷VæE&V7B‡‚Â’ÂrÂ‚Â"Â&v#ScRƒ3BÃ3‚ÃS’“°¢òòFW‡Fò&V6÷'FFò2Æ–æV26öâVÆ—6—2†Æf–6†æò7&V6S¢Æ&V¦–ÆÆW2f–¦¢6öç7B6†"¢2Òt6Æ—¶•ÒçFW‡C°¢–çBG’Ò’²ÂÇ‚Ò‚²ÂÖ‡"Ò‚²rÒ3BÂÆ–æW2Ò°¢6†"Æå³CÓ²–çBÇÒ°¢f÷"†–çBÒ²5·ÒbbÆ–æW2Â3²²²—°¢Æå¶Ç²µÒÒ5·Ó²Æå¶ÇÒÒ°¢&ööÂÆ7BÒ‡5·²ÒÓÒ“°¢–b‡FW‡Er†ÆâÂ’âÖ‡"ÒÇ‚ÒbÇÂÇãÒ3‚ÇÂ5·ÒÓÒuÆârÇÂÆ7B—°¢–b‚Æ7BbbÆ–æW2ÓÒ"—²Æå¶Çâ"òÇÒ"¢ÒÒ²7G&æ6B†ÆâÂ"âââ"Â6—¦Vöb†Æâ’Ò7G&ÆVâ†Æâ’Ò“²Ð¢G&uFW‡D6Æ—†Ç‚ÂG’ÂÆâÂÂ&v#ScRƒ##‚Ã#3"Ã#C"’ÂÖ‡"“°¢G’³Òc²Æ–æW2²³²ÇÒ²Æå³ÒÒ°¢Ð¢Ð¢²–çBƒ"Ò‚²rÒ#Â“"Ò’²c²òò–âFRÆf–6†¢f–ÆÅ&V7B‡ƒ"Ò"Â“"ÒÂBÂ’Ât6Æ—¶•Òç–ææVBò&v#ScRƒ“ÃsÃ#SR’¢&v#ScRƒ“bÃ"Ã#’“°¢f–ÆÅ&÷VæE&V7B‡ƒ"ÒbÂ“"Ò’Â"Â‚Â"Ât6Æ—¶•Òç–ææVBò&v#ScRƒ“ÃsÃ#SR’¢&v#ScRƒ“bÃ"Ã#’“²Ð¢²–çBƒ"Ò‚²rÒ#Â“"Ò’²‚Òƒ²òò&÷'&"W7Ff–6†¢7G&ö¶U6Vt‡ƒ"ÒRÂ“"ÒRÂƒ"²RÂ“"²RÂ"ãbÂ&v#ScRƒ#ÃÃ’“°¢7G&ö¶U6Vt‡ƒ"²RÂ“"ÒRÂƒ"ÒRÂ“"²RÂ"ãbÂ&v#ScRƒ#ÃÃ’“²Ð¢Ð¢G&uFW‡D2…45%õrò"Â45%ô‚ÒCBÂ%Fö6Væf–6†&Vv&ÆVâVÂ7W'6÷""ÂÂ&v#ScRƒ#Ã#‚ÃC‚’“°¢fÇ„fÇW6„ÆÂ‚“°§Ð¢òòFWgVVÇfRG'VR6’VÂF÷VRW&&VÂæVÂ‡6–V×&RVRW7FR&–W'Fò’à§7FF–2&ööÂ6Æ—æVÅF–6²‚—°¢–b‚6Æ—æVÄöâ’&WGW&âfÇ6S°¢–b‚BçF’&WGW&âG'VS°¢–b…Bç’ÂcB—°¢–b…Bç‚ÂS—²6Æ—æVÄöâÒfÇ6S²6Æ—6´6ÆV"ÒfÇ6S²æ÷FU&VæFW$ÆÂ‚“²&WGW&âG'VS²ÒòòföÇfW ¢–b†'2…Bç‚Ò…45%õrÒ“b’’ÃÒ#"—²6Æ—f–ÇFW%–âÒ6Æ—f–ÇFW%–ã²6Æ—6´6ÆV"ÒfÇ6S²6Æ—&VæFW%æVÂ‚“²&WGW&âG'VS²Ð¢–b†'2…Bç‚Ò…45%õrÒC"’’ÃÒ#"—²òòf6–"ƒ"F÷VW2¢–b†6Æ—6´6ÆV"—²6Æ—6ÆV%Vç–ææVB‚“²6Æ—6´6ÆV"ÒfÇ6S²Ð¢VÇ6R6Æ—6´6ÆV"ÒG'VS°¢6Æ—&VæFW%æVÂ‚“²&WGW&âG'VS°¢Ð¢&WGW&âG'VS°¢Ð¢6Æ—6´6ÆV"ÒfÇ6S°¢f÷"†–çB²Ò²²Â6Æ—f—4âbb²Âƒ²²²²—°¢–çB‚Â’ÂrÂƒ²6Æ—6&E&V7B†²Â‚Â’ÂrÂ‚“°¢–b…Bç‚Â‚ÇÂBç‚â‚²rÇÂBç’Â’ÇÂBç’â’²‚’6öçF–çVS°¢–çB’Ò6Æ—f—5¶µÓ°¢–b…Bç‚â‚²rÒ3BbbBç’Â’²3B—²6Æ—FövvÆU–â†’“²6Æ—&VæFW%æVÂ‚“²&WGW&âG'VS²Òòò–à¢–b…Bç‚â‚²rÒ3BbbBç’â’²‚Ò3B—²6Æ—FVÂ†’“²6Æ—&VæFW%æVÂ‚“²&WGW&âG'VS²Òòò&÷'& ¢æ÷FT–ç6W'B†t6Æ—¶•ÒçFW‡B“²òòVv ¢6Æ—æVÄöâÒfÇ6S²æ÷FU&VæFW$ÆÂ‚“²&WGW&âG'VS°¢Ð¢&WGW&âG'VS°§Ð ¢òòÒÒÒÒd4R3¢ÖVçR&Ö2÷6–öæW2"ƒ"66–öæW2&VÆW2Â6–â&VÆÆVæò’ÒÒÒÐ§7FF–2&ööÂ¶$Ö÷&TöâÒfÇ6S°§7FF–2–çB¶$Ö÷&U‚ÒÂ¶$Ö÷&U’Ò°¢6FVf–æR´%ôÔõ$Uôâ §7FF–26öç7B6†"¢´%ôÔõ$UôÄ$Å´´%ôÔõ$UôåÒÒ²%6VÆV66–öæ"FöFò"Â$–ç6W'F"fV6†’†÷&"Ó°§7FF–2fö–B¶$G&tÖ÷&R‚—°¢–b‚¶$Ö÷&Töâ’&WGW&ã°¢–çBrÒ#SÂ‚Ò´%ôÔõ$Uôâ¢3‚²#°¢¶$Ö÷&U‚Ò45%õrÒrÒ²¶$Ö÷&U’Ò¶%FööÆ&%’‚’Ò‚Òc°¢–b†¶$Ö÷&U’Âc’¶$Ö÷&U’Òc°¢6WD'Vb†f"“°¢f–ÆÅ&÷VæE&V7B†¶$Ö÷&U‚Â¶$Ö÷&U’ÂrÂ‚Â"Â&v#ScRƒ3‚ÃC"ÃSb’“°¢f÷"†–çB’Ò²’Â´%ôÔõ$Uôã²’²²¢G&uFW‡B†¶$Ö÷&U‚²BÂ¶$Ö÷&U’²²’¢3‚²‚Â´%ôÔõ$UôÄ$Å¶•ÒÂ"Â&v#ScRƒ#3‚Ã#C"Ã#S’“°¢fÇ„fÇW6‚†¶$Ö÷&U’Ò"Â¶$Ö÷&U’²‚²"“°§Ð§7FF–2–çB¶$Ö÷&T†—B†–çB‚Â–çB’—°¢–b‚¶$Ö÷&Töâ’&WGW&âÓ°¢–çBrÒ#S°¢–b‡‚Â¶$Ö÷&U‚ÇÂ‚â¶$Ö÷&U‚²r’&WGW&âÓ°¢f÷"†–çB’Ò²’Â´%ôÔõ$Uôã²’²²—²–çB’Ò¶$Ö÷&U’²²’¢3ƒ²–b‡’ãÒ’bb’Â’²3‚’&WGW&â“²Ð¢&WGW&âÓ°§Ð ¢òòG'VR6’VÂ6&7FW"VR†’¥U5DòåDU2FVÂ7W'6÷"W2†–væ÷&æFòÖ—W67VÆ2¢òòF–ÆFW2’VÂÖ—6ÖòVR2â6RW6&æò&÷'&"FRÖ2ÂVÆVv—"Vâ6VçFòà§7FF–2&ööÂ¶$Æ7D6†$—2†6öç7B6†"¢2—°¢–b†æ÷FT7W"ÃÒÇÂæ÷FT7W"â†–çB—7G&ÆVâ†æ÷FT'VffW"’’&WGW&âfÇ6S°¢6öç7B6†"¢Òæ÷FT'VffW"²WFc…&Wb†æ÷FT'VffW"Âæ÷FT7W"“°¢6öç7B6†"¢"Ò3°¢&WGW&â¶$föÆD6‚‚f’ÓÒ¶$föÆD6‚‚f"“°§Ð§7FF–2fö–B†æFÆT¶W•&VÆV6R†–çB‚Â–çB’—°¢–b‡‚ÂC‚bb’ÂC‚—²6Æ÷6R‚“²&WGW&ã²Ð¢òò6öâÆf–&–F6öæf—&ÖFÂU5D'WF–æòW67&–&S¢ÆFV6Æ6RW67&–&–ð¢òòÂFö6"â6–âW7FÆ–æVÆÖ—6ÖVÇ66–öâVçG&&–F÷2fV6W2à¢–b†¶$f7D7F—fR‚’’&WGW&ã°¢–çBf’Ò¶$e&÷t†—B‡‚Â’“°¢–b†f’ãÒ—²æ÷FTgVæ4¶W’†f’“²æ÷FU&VæFW$ÆÂ‚“²&WGW&ã²Ð¢–çB6VÆÂÒ¶$6VÆÄB‡‚Â’“°¢–b†6VÆÂãÒ—²¶$g…7F'B†6VÆÂ“²¶%&W746†"†Ö7F—fõ¶6VÆÂò´%ô4ôÅ5Õ¶6VÆÂR´%ô4ôÅ5Ò“²æ÷FU&VæFW$ÆÂ‚“²&WGW&ã²Ð§Ð¢òòd4Rs¢æ–Ö6–öâFRW'GW&FVÂFV6ÆFòVâæ÷F2ƒã22Â–çFW'öÆFÀ¢òòÖ—6ÖòW7—&—GRVRÇ7T¶$æ–ÒFVÂ&Æ÷VVòÂVR–ÆFVæ–’à§7FF–2V–çC3%÷Bæ÷FT¶$æ–ÒÒ°§7FF–2fö–Bæ÷FTVçFW"‚—°¢Ö7F—fòÒÄ”õUEôU3²¶$ÆætW2ÒG'VS²¶%6†–gBÒfÇ6S²¶$Ç¶W’ÒÓ²¶%÷WÒfÇ6S°¢æ÷FT7W"Ò7G&ÆVâ†æ÷FT'VffW"“²æ÷FT6ÆV%6VÂ‚“²æ÷FT†æFÆTG&rÒ°¢¶$W‡G&4öâÒG'VS²òòæ÷F24’×VW7G&&'&’6†—0¢¶$Ç•6—¦R‚“²¶$×E7W&f6U&W6WB‚“°¢6Æ—æVÄöâÒfÇ6S²¶$Ö÷&TöâÒfÇ6S²6Æ—6´6ÆV"ÒfÇ6S²¶%Fö7D×2Ò°¢¶$6†—4'V–ÆB‚“°¢æ÷FT¶$æ–ÒÒ´%ôä”ÕõôÄ•4…ôôâòÖ–ÆÆ—2‚’¢°¢æ÷FU&VæFW$ÆÂ‚“°§Ð§7FF–2fö–Bæ÷FUF–6²‚—°¢–çBG…F÷ÒC‚ÂG„&÷BÒæ÷FUG‡D&÷B‚“° ¢òò’d4RrÒW'GW&FVÂFV6ÆFó¢6R–çFW'öÆVÂFW7Æ¦Ö–VçFòfW'F–6Â¢òò6RgVVÆ64ôÄòÆ&æFFVÂFV6ÆFòâÖ–VçG&2GW&Âæò6RÆVRVçG&Fà¢–b†æ÷FT¶$æ–Ò—°¢fÆöBÒ†Ö–ÆÆ—2‚’Òæ÷FT¶$æ–Ò’ò3ãc°¢–b‡ãÒ—²Ò²æ÷FT¶$æ–ÒÒ²Ð¢æ÷FU&VæFW$¶W–&ö&B‚†–çB’‚ƒãbÒ’¢…45%ô‚Ò¶%æVÅF÷‚’’’“°¢&WGW&ã°¢Ð ¢òòæVÂFR÷'FVÆW2&–W'Fó¢6RÆò6öÖRFöFò†7FVR6R6–W'&Rà¢–b†6Æ—æVÄöâ—²6Æ—æVÅF–6²‚“²&WGW&ã²Ð ¢òòd4RrÒv"VÂFW7FVÆÆòFRÆVÇF–ÖFV6Æ7VæFò7V×ÆR7RF–V×òà¢¶$g…F–6²†¶$6öÄ¶W’‚’Â¶$6öÄ¶W•G‡B‚’“°¢òòd4RrÒVÂ6†—$6÷–Fò"6R&÷'&6öÆò&W–çFæFò7R&æFÂ6GV6"à¢–b„´%ôä”ÕõôÄ•4…ôôâbb¶%Fö7D×2bbÖ–ÆÆ—2‚’Ò¶%Fö7D×2ãÒ#—²¶%Fö7D×2Ò²æ÷FTG&uFW‡B‚“²Ð ¢òòÖ&—2’d4R"Òd”$”D¢ÆFV6Æ6RW67&–&RÂDô4"Â÷"”BFP¢òò6öçF7FòÂ6–âW7W&"VRVÂFVFò6RÆWfçFRâ6öçf—fR6öâC¢FöFòÆòFP¢òò&¦ò†Öæ–¦2ÂÆöær×&W72ÂÖVçR’6–wVR6–VæFòW†7FÖVçFR–wVÂVRçFW2à¢–b…BæF÷vâbbBç’ãÒ¶%æVÅF÷‚’’¶%G—–ætÖ&²‚“²òòfWFòFVÂvW7FòFR7W7Vç6–öâÖ–VçG&26RFV6ÆV¢–b„´%ôÕTÅD•DõT4…ôôâbbt¶$f7EG—Rbbæ÷FTÖVçRbb¶%÷Wbb¶$Ö÷&Töâ—°¢–çBâÒ¶$×EöÆÂ‚“°¢f÷"†–çBRÒ²RÂã²R²²—°¢–b†¶$Wd6VÆÅ¶UÒãÒ—²¶$g…7F'B†¶$Wd6VÆÅ¶UÒ“²¶%&W746†"†Ö7F—fõ¶¶$Wd6VÆÅ¶UÒò´%ô4ôÅ5Õ¶¶$Wd6VÆÅ¶UÒR´%ô4ôÅ5Ò“²Ð¢VÇ6R–b†¶$Wdfå¶UÒãÒ’æ÷FTgVæ4¶W’†¶$Wdfå¶UÒ“°¢Ð¢–b†ââ—²¶$6†—4'V–ÆB‚“²æ÷FTG&uFW‡B‚“²æ÷FU&VæFW$¶W–&ö&Bƒ“²Ð¢ÒVÇ6R°¢òò6öâÆf–&–FVâW6†ÖVçR&–W'FòÂ÷WFR6VçF÷2ÂÖVçR&Ö2"¢òò6RôÅd”DâÆ÷26öçF7F÷26VwV–F÷2â6’æòÂÂ&VçVF"VVF&–â–G0¢òò'f—f÷2"FRFVF÷2VR–6RÆWfçF&öâ’Æ6–wV–VçFRVÇ66–öâFRW6P¢òòÖ—6Öò–BæòF—7&&–æFâÖ–VçG&2FçFò6–wVRgVæ6–öææFòÆ'WF¢òò6Æ6–6FR6öÇF"Â6’VRæò6R–W&FRæ–æwVæFV6Æà¢¶$×E&W6WB‚“°¢Ð ¢òò’ÖVçR6öçFW‡GVÃ¢–çFW&6WFF÷VW2VâVÂ&VFRFW‡Fð¢–b†æ÷FTÖVçRbbBç&VÆV6VBbbBçFbbBç’Â¶%æVÅF÷‚’—°¢–çBÖ’Òæ÷FTÖVçT†—B…Bç‚ÂBç’“°¢–b†Ö’ãÒ—°¢–b†Ö’ÓÒ’6Æ—7WB‚“²VÇ6R–b†Ö’ÓÒ’6Æ—6÷’‚“²VÇ6R–b†Ö’ÓÒ"’6Æ—7FR‚“²VÇ6R6VÆV7DÆÅG‡B‚“°¢–b†Ö’Ò2’æ÷FTÖVçRÒfÇ6S°¢æ÷FU&VæFW$ÆÂ‚“²&WGW&ã°¢Ð¢æ÷FTÖVçRÒfÇ6S²æ÷FT6ÆV%6VÂ‚“²æ÷FT7W"Òæ÷FTÆ–÷WD†—B…Bç‚ÂBç’“²æ÷FU&VæFW$ÆÂ‚“²&WGW&ã°¢Ð ¢òò"’–æ–6–òFRvW7Fó¢Öæ–¦FR6VÆV66–öâòÆöær×&W72FRFV6Æ¢–b…Bç&W76VB—°¢æ÷FT†æFÆTG&rÒ²¶$Ç¶W’ÒÓ²¶%÷WÒfÇ6S°¢–b†æ÷FT†56VÂ‚’bbBç’ãÒG…F÷bbBç’ÂG„&÷B²3—°¢–b†'2…Bç‚Ò„5‚’Â#Bbb'2…Bç’Ò†„5’²#’’Â#‚’æ÷FT†æFÆTG&rÒ°¢VÇ6R–b†'2…Bç‚Ò„%5‚’Â#Bbb'2…Bç’Ò†„%5’²#’’Â#‚’æ÷FT†æFÆTG&rÒ#°¢Ð¢–b‚æ÷FT†æFÆTG&rbbBç’ãÒ´%õ’—°¢–çB6VÆÂÒ¶$6VÆÄB…Bç‚ÂBç’“°¢¶$Ç¶W’Ò¶$—5f÷vVÄ6VÆÂ†6VÆÂ’ò6VÆÂ¢Ó°¢òòd4Rs¢6öâÆW67&—GW&&–FtDÆFV6Ææò6RW67&–&R†7F¢òò6öÇF"Â6’VRVÂFW7FVÆÆòW2ÆVæ–66VæÂFRVRVÂF÷VRVçG&òà¢–b‚„´%ôÕTÅD•DõT4…ôôâbbt¶$f7EG—R’’¶$g…&W72†6VÆÂÂ¶$6öÄ¶W’‚’Â¶$6öÄ¶W•G‡B‚’“°¢Ð¢&WGW&ã°¢Ð ¢òò2’'&7G&RFRÖæ–¦ÓâW‡FVæFW"6VÆV66–öà¢–b†æ÷FT†æFÆTG&rbbBæF÷vâ—°¢–çB&’Òæ÷FTÆ–÷WD†—B…Bç‚ÂBç’“°¢–b†æ÷FT†æFÆTG&rÓÒ—²–b†&’ãÒbb&’Âæ÷FU6VÄ"’æ÷FU6VÄÒ&“²Ð¢VÇ6R²–b†&’âæ÷FU6VÄ’æ÷FU6VÄ"Ò&“²Ð¢æ÷FT7W"Ò†æ÷FT†æFÆTG&rÓÒ’òæ÷FU6VÄ¢æ÷FU6VÄ#°¢æ÷FU&VæFW$ÆÂ‚“²&WGW&ã°¢Ð ¢òòB’Æöær×&W72VâFW‡FòÓâ6VÆV66–öæ"Æ'&¢–b‚æ÷FT†æFÆTG&rbb¶$Ç¶W’ÂbbBæF÷vâbbæ÷FT†56VÂ‚¢bbBç7F'E’ãÒG…F÷bbBç7F'E’ÂG„&÷Bbb†Ö–ÆÆ—2‚’ÒBæF÷vä×2’âS ¢bb'2…Bç‚ÒBç7F'E‚’Â"bb'2…Bç’ÒBç7F'E’’Â"—°¢6VÆV7Ev÷&DB†æ÷FTÆ–÷WD†—B…Bç7F'E‚ÂBç7F'E’’“²æ÷FU&VæFW$ÆÂ‚“²&WGW&ã°¢Ð ¢òòR’Æöær×&W72VâFV6ÆÓâ÷WFR6VçF÷2‡VÖ'&Â6öæf–wW&&ÆRÂf6RR¢–b†¶$Ç¶W’ãÒbbBæF÷vâbb¶%÷Wbb†Ö–ÆÆ—2‚’ÒBæF÷vä×2’â‡Vç6–væVBÆöær–t¶$Ç×2—²¶%÷WÒG'VS²¶%&VæFW%÷W†¶$Ç¶W’“²Ð ¢òòb’6öÇF ¢–b…Bç&VÆV6VB—°¢–b†æ÷FT†æFÆTG&r—²æ÷FT†æFÆTG&rÒ²æ÷FTÖVçRÒG'VS²æ÷FU&VæFW$ÆÂ‚“²&WGW&ã²Ð¢–b†¶%÷W—°¢–çBbÒ¶%÷W†—B…Bç‚ÂBç’“²6öç7B6†"¢f%³EÓ°¢6öç7B6†"¢&6RÒÖ7F—fõ¶¶$Ç¶W’ò´%ô4ôÅ5Õ¶¶$Ç¶W’R´%ô4ôÅ5Ó°¢òò6öâÆf–&–FÂÆÆWG&&6R”6RW67&–&–òÂFö6"â6R6ö×'VV&¢òòVRVÂ6&7FW"çFW&–÷"Â7W'6÷"6VU„5DÔTåDRW6ÆWG&çFW2FP¢òò&÷'&"æF¢6’Â6’÷"ÆòVRgVW&æòÆÆVvòW67&–&—'6RÂæò6R6öÖP¢òòVÂ6&7FW"FRÂÆFòà¢&ööÂ&6U–W67&—FÒ¶$f7D7F—fR‚’bb¶$Æ7D6†$—2†&6R“°¢–b‡bãÒ—°¢–b†&6U–W67&—F’æ÷FT&6·76R‚“°¢¶$vWEf&–çG2†&6U³ÒÂf"“²æ÷FT–ç6W'B‡f%·eÒ“°¢Ð¢VÇ6R–b‚&6U–W67&—F’¶%&W746†"†&6R“°¢¶%÷WÒfÇ6S²¶$Ç¶W’ÒÓ²æ÷FU&VæFW$ÆÂ‚“²&WGW&ã°¢Ð¢–b…BçF—°¢òòd4R2Ò&'&7WW&–÷"‡6öÆò6’W7Ff—6–&ÆR¢–b†¶$Ö÷&Töâ—°¢–çBÖ’Ò¶$Ö÷&T†—B…Bç‚ÂBç’“°¢¶$Ö÷&TöâÒfÇ6S°¢–b†Ö’ÓÒ—²6VÆV7DÆÅG‡B‚“²æ÷FU&VæFW$ÆÂ‚“²&WGW&ã²Ð¢–b†Ö’ÓÒ—²6†"E³C…ÒÂ…³%Ó²'V–ÆE6†÷'DFFR†BÂ6—¦Vöb†B’“²6Æµ7G$&"†‚Â6—¦Vöb†‚’“°¢6†"Æå³cEÓ²6ç&–çFb†ÆâÂ6—¦Vöb†Æâ’Â"W2W2"ÂBÂ‚“²æ÷FT–ç6W'B†Æâ“²æ÷FU&VæFW$ÆÂ‚“²&WGW&ã²Ð¢æ÷FU&VæFW$ÆÂ‚“²&WGW&ã°¢Ð¢–çBF’Ò¶%FööÄ†—B…Bç‚ÂBç’“°¢–b‡F’ãÒ—°¢–b‡F’ÓÒ´%õD%ôTÔô¤’—²Ö7F—fòÒÄ”õUEôTÔô¤“²æ÷FU&VæFW$ÆÂ‚“²Ð¢VÇ6R–b‡F’ÓÒ´%õD%ôÄär—²æ÷FTgVæ4¶W’ƒ"“²æ÷FU&VæFW$ÆÂ‚“²ÒòòÖ—6ÖÆöv–6VRÆFV6ÆU2ôTà¢VÇ6R–b‡F’ÓÒ´%õD%ô4Ä•—²–b„´%ô4Ä•$ô$EôÕTÅD•ôôâ—²6Æ—æVÄöâÒG'VS²6Æ—6´6ÆV"ÒfÇ6S²6Æ—&VæFW%æVÂ‚“²ÒVÇ6R²6Æ—7FR‚“²æ÷FU&VæFW$ÆÂ‚“²ÒÐ¢VÇ6R–b‡F’ÓÒ´%õD%õ4UB—²–b„´%õ4UED”äu5ôôâ’¶'4VçFW"‚“²Ð¢VÇ6R²¶$Ö÷&TöâÒG'VS²¶$G&tÖ÷&R‚“²Ð¢&WGW&ã°¢Ð¢–çB6’Ò¶$6†—†—B…Bç‚ÂBç’“°¢–b†6’ãÒ—²¶$Ç”6†—†6’“²æ÷FU&VæFW$ÆÂ‚“²&WGW&ã²Ð¢–b…Bç’ãÒG…F÷bbBç’ÂG„&÷B—²òòFVâFW‡FòÓâ÷6–6–öæ"7W'6÷ ¢æ÷FT6ÆV%6VÂ‚“²æ÷FT7W"Òæ÷FTÆ–÷WD†—B…Bç‚ÂBç’“²æ÷FU&VæFW$ÆÂ‚“²&WGW&ã°¢Ð¢Ð¢†æFÆT¶W•&VÆV6R…Bç‚ÂBç’“²¶$Ç¶W’ÒÓ²òòFV6ÆFð¢Ð§Ð ¢òò2222222222222222222222222222222222222222222222222222222222220¢òò22d4RR+r¥U5DU2DTÂDT4ÄDò‡çFÆÆ&÷–ÂæfVv&ÆR¢òò22ÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÐ¢òò226RÆÆVvFW6FR§W7FW2ÓâW'6öæÆ—¦6–öâÓâFV6ÆFò¢òò22FW6FRVÂVæw&æ¦RFRÆ&'&FRÆf6R2à¢òò20¢òò22õ"TRTäåDÄÄ$õ”’äòTä64DTtõ$”TâÄ¢òò22$%$ÄDU$ÂDR¥U5DU3¢W6&'&F–VæR"F&¦WF2FP¢òò22S"‚FW6FR“ÓÂò6VVRFW&Ö–æVâ“Ós#BâVæ6¢òò226W&–Væ6–ÖFRÆ&'&FRæfVv6–öâ‡“ÓsC‚’âÖWFW&Æ¢òò22†’ö&Æ–v&&V†6W"VÂÆ–÷WBFR§W7FW2VçFW&òÂVRæð¢òò22W2ÆòVR6R–F–òâÆ2F&¦WF2Â6öÆ÷&W2’VÂG&öâFP¢òò22f–Æ26öâÆ÷2Ö—6Ö÷2FR6WE&÷t6&BÂ6’VR6RfR6öÖòVæ¢òò226V66–öâÖ2FR§W7FW2à¢òò20¢òò22ÄòTRäò4R”ÕÄTÔTåD’õ"TR‡6R×VW7G&ÂFVçVFòÀ¢òò22VâfW¢FRW66öæFW&Æò“ ¢òò22+rVçG&FFRf÷¢ÓâæV6W6—F&V6öæö6–Ö–VçFòVâÆçV&Rà¢òò22+rwV&F"6GW&2Vâ÷'FVÆW2ÓâW7FRfÆW„õ2æòF–VæP¢òò22gVæ6–öâFR6GW&FRçFÆÆFRW7V&–òà¢òò22+r&W7VW7F†F–62ÓâÆÆ6æòÆÆWfÖ÷F÷"f–'&F÷"à¢òò22+rF—f–F—"FV6ÆFòÓâ6öâCƒ‚FRæ6†òæò6&VâF÷0¢òò22Ö—FFW2W6&ÆW3²Vâ7RÇVv"W7FVÂ6VÆV7F÷"FRFÖæòà¢òò2222222222222222222222222222222222222222222222222222222222220¢6FVf–æR´%5ôÔ”â ¢6FVf–æR´%5ôDU4”tâ¢6FVf–æR´%5õDõT4‚ ¢6FVf–æR´%5õ5”Õ20¢6FVf–æR´%5õ4„õ%B@¢6FVf–æR´%5õ44TD•BP¢6FVf–æR´%5ô$õUB`¢6FVf–æR´%5ôÄärp¢6FVf–æR´%5õ$õuôÔ‚# §7FF–2–çB¶'5vRÒ´%5ôÔ”ã°§7FF–2–çB¶'567&öÆÂÒÂ¶'46öçFVçD‚Ò°§7FF–2–çB¶'5&÷u“´´%5õ$õuôÔ…ÒÂ¶'5&÷u“´´%5õ$õuôÔ…ÒÂ¶'5&÷tâÒ°§7FF–2–çB¶'5&WBÒ²òòÒföÇfW"æ÷F2ÂÒföÇfW"§W7FW0§7FF–2–çB¶'5656VÂÒ²òòF¦òVR6RW7FVF—FæFð§7FF–2–çB¶'564f–VÆBÒ²òòÒ'&Wf–6–öâÂÒW‡ç6–öà§7FF–26†"¶'564´´%õ45ô%%ÒÂ¶'564U´´%õ45ôU…Ó°§7FF–2–çB¶'4G&u“ÒÂ¶'4G&u3Ò²òò'&7G&RFRÆÆ—7F§7FF–2&ööÂ¶'4G&vv–ærÒfÇ6S°¢6FVf–æR´%5õDõ ¢6FVf–æR´%5ô$õB…45%ô‚Ò#B §7FF–2V–çCe÷B¶'4&r‚—²&WGW&âtUô$s²Ð§7FF–2V–çCe÷B¶'46&B‚—²&WGW&âV”vÆ72ò4UEô4$EôtÄ52¢4UEô4$Eô$s²Ð¢òòf–ÆFöFòVÂæ6†òÂ6öâVÂÖ—6ÖòÆVæwV¦Rf—7VÂVR6WE&÷t6&B‡F&¦WFÀ¢òòF—GVÆòÂfÆ÷"’6†Wg&öâ’â&öâ"FVçVVÂFW‡Fò7VæFòÆf–ÆW7FvF¢òò÷"†&Gv&R‡f÷¢Â6GW&2Â†F–6’à§7FF–2–çB¶'5&÷r†–çB’Â6öç7B6†"¢F—FÆRÂ6öç7B6†"¢fÂÂ&ööÂ6†Wg&öâÂ&ööÂVæ&ÆVB—°¢–çB&‚Òc"Â‚Ò"ÂrÒ45%õrÒ#C°¢–b†¶'5&÷tâÂ´%5õ$õuôÔ‚—²¶'5&÷u“¶¶'5&÷tåÒÒ“²¶'5&÷u“¶¶'5&÷tåÒÒ’²&ƒ²¶'5&÷tâ²³²Ð¢òòVÂf–G&–ò–äò6RvGW&çFRVÂ'&7G&R†G&tvÆ746&DfÆBÆò&W7VVÇfP¢òòVæfW¢’ÆògVVÆ6÷"f–Æ2“¢VÂÖFW&–Â6RÖçF–VæRÖ–VçG&26R†6P¢òò67&öÆÂÂFÂ7VÂW7F6öæf–wW&Fòà¢–b‡V”vÆ72’G&tvÆ746&DfÆB‡‚Â’ÂrÂ&‚Ò‚Â"Â¶'46&B‚’Â¶'4&r‚’“°¢VÇ6Rf–ÆÅ&÷VæE&V7B‡‚Â’ÂrÂ&‚Ò‚Â"Â¶'46&B‚’“°¢V–çCe÷BF2ÒVæ&ÆVBò4UEõE…Eô„’¢4UEõE…EôÕUDS°¢V–çCe÷Bf2ÒVæ&ÆVBò4UEõE…EôÄò¢4UEõE…EôÕUDS°¢G&uFW‡D6Æ—‡‚²bÂ’²‚ÂF—FÆRÂ"ÂF2Â‚²rÒ#‚“°¢–b‡fÂ’G&uFW‡D6Æ—‡‚²bÂ’²3"ÂfÂÂÂf2Â‚²rÒ#‚“°¢–b†6†Wg&öâbbVæ&ÆVB—°¢–çB6‡‚Ò‚²rÒ‚Â6‡’Ò’²‡&‚Ò‚’ò#°¢7G&ö¶U6Vt†6‡‚Ò2Â6‡’ÒbÂ6‡‚²2Â6‡’Â"ãbÂ4UEô4„Ub“°¢7G&ö¶U6Vt†6‡‚²2Â6‡’Â6‡‚Ò2Â6‡’²bÂ"ãbÂ4UEô4„Ub“°¢Ð¢&WGW&â’²&ƒ°§Ð§7FF–2–çB¶'56V7F–öâ†–çB’Â6öç7B6†"¢B—°¢G&uFW‡BƒbÂ’ÂBÂ"Â4UEõE…Eô„’“°¢&WGW&â’²3°§Ð§7FF–26öç7B6†"¢¶'56—¦TæÖR‚—²&WGW&ât¶%6—¦RÓÒ´%õ4•¤Uô4ôÕ5Bò$6ö×7Fò"¢t¶%6—¦RÓÒ´%õ4•¤Uô$”rò$w&æFR"¢$æ÷&ÖÂ#²Ð§7FF–26öç7B6†"¢¶'57G–ÆTæÖR‚—²&WGW&ât¶%7G–ÆRÓÒò$7VG&F"¢t¶%7G–ÆRÓÒ"ò$6öçF÷&æò"¢%&VFöæFVF#²Ð§7FF–26öç7B6†"¢¶'4föçDæÖR‚—²&WGW&ât¶$föçE62ÓÒò%WVUÇ„35Ç„#"&"¢t¶$föçE62ÓÒ"ò$w&æFR"¢$æ÷&ÖÂ#²Ð§7FF–26öç7B6†"¢¶'4g„æÖR‚—²&WGW&ât¶$g„×2ÓÒcò$6÷'F"¢t¶$g„×2ÓÒcò$Æ&v"¢$æ÷&ÖÂ#²Ð§7FF–26öç7B6†"¢¶'4öäöfb†&ööÂ"—²&WGW&â"ò$7F—fFò"¢$FW67F—fFò#²Ð ¢òòÒÒÒÒFV6ÆFò–æ7'W7FFòFVÂVF—F÷"FRF¦÷2ÒÒÒÐ¢òòW2VÂÔ•4ÔòFV6ÆFò†Ö—6ÖvVöÖWG&–ÂÖ—6Ö÷26öÆ÷&W2ÂÖ—6Öò¶$6VÆÄB’Â6öÆð¢òòVRW67&–&RVâVÂ6×òVæfö6FòVâfW¢FRVâVææ÷Fà§7FF–2fö–B¶'4VF—F÷$¶"‚—°¢¶%–çEæVÂ„´%õ’ÒBÂV”vÆ72ò¶$6öÅæVÂ‚’¢&v#ScRƒ‚Ã#Ã#‚’“°¢–çBg2Ò¶$föçE6—¦R‚“°¢f÷"†–çB"Ò²"Â´%õ$õu3²"²²’f÷"†–çB2Ò²2Â´%ô4ôÅ3²2²²—°¢–çB‚Ò´%õ‚²2¢„´%ôµr²´%ôt’Â’Ò´%õ’²"¢„´%ô´‚²´%ôt“°¢6öç7B6†"¢²ÒÖ7F—fõ·%Õ¶5Ó°¢6†"U³eÓ°¢–b†¶%6†–gBbbµ³ÒÓÒbbµ³ÒãÒvrbbµ³ÒÃÒw¢r—²U³ÒÒ†6†"’†µ³ÒÒ3"“²U³ÒÒ²²ÒS²Ð¢¶%–çD¶W’‡‚Â’Â´%ôµrÂ´%ô´‚Â²Âg2Â¶$6öÄ¶W’‚’Â¶$6öÄ¶W•G‡B‚’ÂfÇ6R“°¢Ð¢–çBg’Ò¶$gVæ5’‚“°¢6öç7B6†"¢Æ%´´%ôd´U•5ÒÒ²'6†–gB"Â¶$Æ–W$Æ&VÂ‚’Â¶$ÆætW2ò$U2"¢$Tâ"Â&W76–ò"Â#ÂÒ"Â$ô²"Ó°¢f÷"†–çB’Ò²’Â´%ôd´U•3²’²²’¶$d¶W’†¶$d¶W•‚†’’Âg’Â¶$d¶W•r†’’ÂÆ%¶•ÒÂ†’ÓÒ’bb¶%6†–gB“°§Ð§7FF–2fö–B¶'4VæDf–VÆB†6öç7B6†"¢2—°¢6†"¢G7BÒ¶'564f–VÆBÓÒò¶'564¢¶'564S°¢–çB6Ò¶'564f–VÆBÓÒò´%õ45ô%"¢´%õ45ôU…°¢–çBÂÒ7G&ÆVâ†G7B’Â6ÂÒ7G&ÆVâ‡2“°¢–b„Â²6ÂãÒ6Ò’&WGW&ã°¢ÖVÖ7’†G7B²ÂÂ2Â6Â“²G7E´Â²6ÅÒÒ°§Ð§7FF–2fö–B¶'4&6´f–VÆB‚—°¢6†"¢G7BÒ¶'564f–VÆBÓÒò¶'564¢¶'564S°¢–çBÂÒ7G&ÆVâ†G7B“°¢–b„ÂÃÒ’&WGW&ã°¢–çBÒÂÒ²v†–ÆR‡âbb†G7E·Òb„3’ÓÒƒƒ’ÒÓ°¢G7E·ÒÒ°§Ð ¢òòÒÒÒÒ6öçFVæ–FòFR6Fv–æÒÒÒÐ§7FF–2fö–B¶'46öçFVçB‚—°¢¶'5&÷tâÒ°¢–çB’Ò´%5õDõÒ¶'567&öÆÃ°¢6†"e³cEÓ°¢–b†¶'5vRÓÒ´%5ôÔ”â—°¢’Ò¶'5&÷r‡’Â$–F–öÖ2’F—÷2"Â¶$ÆætW2ò$W7Ç„35Ç„#öÂ„U2’Ò7F—fò"¢$VævÆ—6‚„Tâ’Ò7F—fò"ÂG'VRÂG'VR“°¢’Ò¶'5&÷r‡’Â%FW‡Fò&VF–7F—fò"Â¶'4öäöfb†t¶%&VF–7B’ÂfÇ6RÂG'VR“°¢’Ò¶'5&÷r‡’Â%&Wf—6•Ç„35Ç„#6â÷'Föw%Ç„35Ç„"&f–6%Ç„35Ç„6–6"Â¶'4öäöfb†t¶%7VÆÂ’ÂfÇ6RÂG'VR“°¢’Ò¶'5&÷r‡’Â%7VvW&—"VÖö¦—2"Â¶'4öäöfb†t¶$VÖö¦•7Vr’ÂfÇ6RÂG'VR“°¢’Ò¶'5&÷r‡’Â$F¦÷2FRFW‡Fò"Â$'&Wf–6•Ç„35Ç„#6âÓâW‡ç6•Ç„35Ç„#6â"ÂG'VRÂG'VR“°¢’³Òc²’Ò¶'56V7F–öâ‡’Â%FV6ÆFò"“°¢’Ò¶'5&÷r‡’Â$&'&FR†W'&Ö–VçF2FVÂFV6ÆFò"Â¶'4öäöfb†t¶%FööÆ&"’ÂfÇ6RÂG'VR“°¢’Ò¶'5&÷r‡’Â%FV6ÆFòFR6öçG&7FRÇFò"Â¶'4öäöfb†t¶$†”6öâ’ÂfÇ6RÂG'VR“°¢òòF–væ÷7F–6ò†öæW7FòÂÆFòFVÂ–çFW''WF÷#¢7VçF÷2FVF÷2ÆfW¢†¢òòÆÆVvFò&W÷'F"VÂæVÂFW6FRVR'&æ6òÆÆ6à¢òòTâU5DÄ4dôäU"4”TÕ$R#FVFò"Â’W26÷'&V7Fó¢VÂæVÂFP¢òòBã"W2$U4•5D•dòFRB†–Æ÷2’÷"6öç7G'V66–öâæòVVFRF—7F–æwV—"F÷0¢òò6öçF7F÷2†Æ÷2ÆVR6öÖòVæò6öÆòVâVÂVçFòÖVF–ò’âÆW67&—GW&&–F¢òò6–wVRgVæ6–öææFòÖF—7&ÂDô4"VâfW¢FRÂ6öÇF"ÂVRW2ÆòVRÆ¢òò†6R&–FÒÂÆòVRæò†’W2F÷2FV6Æ2ÆfW¢âÆf–ÆÆòF–6RFÀ¢òò7VÂVâfW¢FRf–æv—"Vâ×VÇF—F÷VRVRVÂ†&Gv&RæòFà¢–b†t¶$f7EG—R’6ç&–çFb‡bÂ6—¦Vöb‡b’Â$7F—fFòÒVÂæVÂ†FFòVBFVFòW2ÆfW¢"À¢¶$×DÖ…G2Â¶$×DÖ…G2ÓÒò""¢'2"“°¢VÇ6R6ç&–çFb‡bÂ6—¦Vöb‡b’Â$FW67F—fFòÒ6RW67&–&RÂ6öÇF""“°¢’Ò¶'5&÷r‡’Â$W67&—GW&%Ç„35Ç„–F†×VÇF—F÷VR’"ÂbÂfÇ6RÂG'VR“°¢’Ò¶'5&÷r‡’Â$F—6UÇ„35Ç„#ò’FÖÇ„35Ç„#ò"Â´%õ4•¤Uô4ôäd”uôôâò¶'56—¦TæÖR‚’¢¶'57G–ÆTæÖR‚’ÂG'VRÂG'VR“°¢’Ò¶'5&÷r‡’Â$FW6Æ—¦"ÂFö6"’&W7VW7FEÇ„35Ç„"&7F–Â"Â¶'4g„æÖR‚’ÂG'VRÂG'VR“°¢’³Òc²’Ò¶'56V7F–öâ‡’Â$æòF—7öæ–&ÆRVâW7FR†&Gv&R"“°¢’Ò¶'5&÷r‡’Â$VçG&FFRf÷¢"Â$æV6W6—F&V6öæö6–Ö–VçFòVâÆçV&R"ÂfÇ6RÂfÇ6R“°¢’Ò¶'5&÷r‡’Â$wV&F"6GW&2Vâ÷'FVÆW2"Â$fÆW„õ2æòF–VæR6GW&FRçFÆÆ"ÂfÇ6RÂfÇ6R“°¢’³Òc²’Ò¶'56V7F–öâ‡’Â$÷G&÷2"“°¢’Ò¶'5&÷r‡’Â%&W7F&ÆV6W"§W7FW2FVÂFV6ÆFò"Â%gVVÇfRÆ÷2fÆ÷&W2÷"FVfV7Fò"ÂfÇ6RÂG'VR“°¢’Ò¶'5&÷r‡’Â%6ö'&RFV6ÆFò"Â%FV6ÆFòfÆW„õ2"ÂG'VRÂG'VR“°¢ÒVÇ6R–b†¶'5vRÓÒ´%5ôÄär—°¢’Ò¶'5&÷r‡’Â$W7Ç„35Ç„#öÂ„U2’"Â¶$ÆætW2ò$7F—fò"¢%Fö6&7F—f""ÂfÇ6RÂG'VR“°¢’Ò¶'5&÷r‡’Â$VævÆ—6‚„Tâ’"Â¶$ÆætW2ò%Fö6&7F—f""¢$7F—fò"ÂfÇ6RÂG'VR“°¢’Ò¶'5&÷r‡’Â$6çVÕÇ„35Ç„—&–6’FR5Ç„35Ç„FÖ&öÆ÷2"Â%6–V×&RF—7öæ–&ÆRƒó#2’"ÂfÇ6RÂfÇ6R“°¢’Ò¶'5&÷r‡’Â$VÖ÷F–6öæ÷2FRFW‡Fò"Â$6VÖö¦’ÂvÆ–f÷2FRÆgVVçFR"ÂfÇ6RÂfÇ6R“°¢’³Òƒ°¢G&uFW‡D6Æ—ƒbÂ’Â$VÂFV6ÆFò6÷÷'FW7F÷2F÷2–F–öÖ3¢6öâÆ÷2VR"ÂÂ4UEõE…EôÕUDRÂ45%õrÒb“²’³Òƒ°¢G&uFW‡D6Æ—ƒbÂ’Â'F–VæVâÖFRFV6Æ2’F–66–öæ&–ò&÷–÷2â"ÂÂ4UEõE…EôÕUDRÂ45%õrÒb“²’³Ò##°¢ÒVÇ6R–b†¶'5vRÓÒ´%5ôDU4”tâ—°¢6–b´%õ4•¤Uô4ôäd”uôôà¢òò6öâ´%õ4•¤Uô4ôäd”uôôâW7Ff–Æä’4RD”%T¤¢VÂFV6ÆFòW6Æ÷0¢òòfÆ÷&W2f–¦÷2FR6–V×&R’ög&V6W"Vâ6VÆV7F÷"VRæò†6RæF6W&–¢òòÖVçF—"âÆ2f–Æ2FR&¦ò6R&V6öÆö6â6öÆ2‡fW"¶'5&÷t7F–öâ’à¢’Ò¶'5&÷r‡’Â%FÖÇ„35Ç„#òFRFV6ÆFò"Â¶'56—¦TæÖR‚’ÂfÇ6RÂG'VR“°¢6VæF–`¢6ç&–çFb‡bÂ6—¦Vöb‡b’Â"VBRRFR÷6–FB"Ât¶$÷6—G’“°¢’Ò¶'5&÷r‡’Â%FÖÇ„35Ç„#ò’G&ç7&Væ6–"ÂbÂfÇ6RÂG'VR“°¢’Ò¶'5&÷r‡’Â$F—6UÇ„35Ç„#ò"Â¶'57G–ÆTæÖR‚’ÂfÇ6RÂG'VR“°¢’Ò¶'5&÷r‡’Â%FÖÇ„35Ç„#òFRgVVçFR"Â¶'4föçDæÖR‚’ÂfÇ6RÂG'VR“°¢6ç&–çFb‡bÂ6—¦Vöb‡b’Â"W2W2W2W2"Â¶%7–ÔBƒ’Â¶%7–ÔBƒ’Â¶%7–ÔBƒ"’Â¶%7–ÔBƒ2’“°¢’Ò¶'5&÷r‡’Â%5Ç„35Ç„FÖ&öÆ÷2W'6öæÆ—¦F÷2"ÂbÂG'VRÂG'VR“°¢’³Òƒ°¢G&uFW‡D6Æ—ƒbÂ’Â%6–âÂ&F—f–F—"FV6ÆFõÂ#¢6öâCƒ‚FRæ6†òæò6&Vâ"ÂÂ4UEõE…EôÕUDRÂ45%õrÒb“²’³Òƒ°¢G&uFW‡D6Æ—ƒbÂ’Â&F÷2Ö—FFW2W6&ÆW2âVâ7RÇVv"ÂÆ÷2G&W2FÖæ÷2â"ÂÂ4UEõE…EôÕUDRÂ45%õrÒb“²’³Ò##°¢ÒVÇ6R–b†¶'5vRÓÒ´%5õDõT4‚—°¢’Ò¶'5&÷r‡’Â$æ–Ö6•Ç„35Ç„#6âFRFV6Æ&W6–öæF"Â¶'4g„æÖR‚’ÂfÇ6RÂG'VR“°¢6ç&–çFb‡bÂ6—¦Vöb‡b’Â"VB×2"Ât¶$Ç×2“°¢’Ò¶'5&÷r‡’Â%VÇ66•Ç„35Ç„#6âÆ&v†6VçF÷2’"ÂbÂfÇ6RÂG'VR“°¢’Ò¶'5&÷r‡’Â%&W7VW7F…Ç„35Ç„F–6"Â$ÆÆ6æòF–VæRÖ÷F÷"f–'&F÷""ÂfÇ6RÂfÇ6R“°¢’³Òƒ°¢G&uFW‡D6Æ—ƒbÂ’Â%FöFòVÂ&WF÷&æòFRW7FçFÆÆW2d•5TÂâ"ÂÂ4UEõE…EôÕUDRÂ45%õrÒb“²’³Ò##°¢ÒVÇ6R–b†¶'5vRÓÒ´%5õ5”Õ2—°¢f÷"†–çB’Ò²’Â´%õ5”Õ3²’²²—°¢6†"E³#EÓ²6ç&–çFb‡BÂ6—¦Vöb‡B’Â%5Ç„35Ç„FÖ&öÆòVB"Â’²“°¢’Ò¶'5&÷r‡’ÂBÂ¶%7–ÔB†’’ÂfÇ6RÂG'VR“°¢Ð¢’³Òƒ°¢G&uFW‡D6Æ—ƒbÂ’Â%Fö6Væf–Æ&6Ö&–"VÂ6–Ö&öÆòâ6ÆVâVâÆ"ÂÂ4UEõE…EôÕUDRÂ45%õrÒb“²’³Òƒ°¢G&uFW‡D6Æ—ƒbÂ’Â&g&æ¦FR'&–&ÂVçG&"VâÆ6ó#2â"ÂÂ4UEõE…EôÕUDRÂ45%õrÒb“²’³Ò##°¢ÒVÇ6R–b†¶'5vRÓÒ´%5õ4„õ%B—°¢f÷"†–çB’Ò²’Â´%õ45ôÔƒ²’²²—°¢–b†t¶%64'%¶•Õ³Òbbt¶%64W‡¶•Õ³Ò’6ç&–çFb‡bÂ6—¦Vöb‡b’Â"W2ÓâW2"Ât¶%64'%¶•ÒÂt¶%64W‡¶•Ò“°¢VÇ6R6ç&–çFb‡bÂ6—¦Vöb‡b’Â%f6–òÒFö6&7&V""“°¢6†"E³#EÓ²6ç&–çFb‡BÂ6—¦Vöb‡B’Â$F¦òVB"Â’²“°¢’Ò¶'5&÷r‡’ÂBÂbÂG'VRÂG'VR“°¢Ð¢’³Òƒ°¢G&uFW‡D6Æ—ƒbÂ’Â$ÂW67&–&—"Æ'&Wf–6–öâÂVÂ6†—FR7VvW&Væ6–"ÂÂ4UEõE…EôÕUDRÂ45%õrÒb“²’³Òƒ°¢G&uFW‡D6Æ—ƒbÂ’Â&ög&V6RÆW‡ç6–öâ6ö×ÆWFâ"ÂÂ4UEõE…EôÕUDRÂ45%õrÒb“²’³Ò##°¢ÒVÇ6R–b†¶'5vRÓÒ´%5ô$õUB—°¢’Ò¶'5&÷r‡’Â%FV6ÆFòfÆW„õ2"Â%fW'6–öâã"ÂfÇ6RÂfÇ6R“°¢6ç&–çFb‡bÂ6—¦Vöb‡b’Â"VBÆ'&2U2òVBTâ"Â´%ôD”5EôU5ôâÂ´%ôD”5EôTåôâ“°¢’Ò¶'5&÷r‡’Â$F–66–öæ&–òÆö6Â"ÂbÂfÇ6RÂfÇ6R“°¢’Ò¶'5&÷r‡’Â$–F–öÖ2"Â$W7Ç„35Ç„#öÂÂVævÆ—6‚"ÂfÇ6RÂfÇ6R“°¢’³Ò°¢G&uFW‡D6Æ—ƒbÂ’Â$VÂWFö6ö×ÆWFFòW2VæÄ•5DÄô4Âd”¤W67&—F"ÂÂ4UEõE…EôÕUDRÂ45%õrÒb“²’³Òƒ°¢G&uFW‡D6Æ—ƒbÂ’Â&VâVÂ&÷–òf—&×v&RâæòW2VâÖöFVÆòFR”¢æò"ÂÂ4UEõE…EôÕUDRÂ45%õrÒb“²’³Òƒ°¢G&uFW‡D6Æ—ƒbÂ’Â&&VæFRÂæòVçF–VæFRVÂ6öçFW‡Fò’æò&VF–6RÆ"ÂÂ4UEõE…EôÕUDRÂ45%õrÒb“²’³Òƒ°¢G&uFW‡D6Æ—ƒbÂ’Â'Æ'&6–wV–VçFRâ6öÆò'W66÷"&Vf–¦òâ"ÂÂ4UEõE…EôÕUDRÂ45%õrÒb“²’³Ò#C°¢G&uFW‡D6Æ—ƒbÂ’Â$Æ&Wf—6–öâ÷'Föw&f–66ö×&6öçG&W6Ö—6Ö"ÂÂ4UEõE…EôÕUDRÂ45%õrÒb“²’³Òƒ°¢G&uFW‡D6Æ—ƒbÂ’Â&Æ—7F¢7V'&–ÆòVRæòVæ7VVçG&ÂæFÖ2â"ÂÂ4UEõE…EôÕUDRÂ45%õrÒb“²’³Ò##°¢ÒVÇ6R–b†¶'5vRÓÒ´%5õ44TD•B—°¢òòF÷26×÷2²FV6ÆFò&VÂFV&¦òâVÂ6×òVæfö6FòÆÆWf&÷&FR§VÂà¢f÷"†–çBbÒ²bÂ#²b²²—°¢–çBg’Ò´%5õDõ²b¢sc°¢6öç7B6†"¢Æ&ÂÒbÓÒò$'&Wf–6•Ç„35Ç„#6â†ÆòVRW67&–&W2’"¢$W‡ç6•Ç„35Ç„#6â†ÆòVR&V6R’#°¢G&uFW‡BƒbÂg’ÂÆ&ÂÂÂ4UEõE…EôÄò“°¢f–ÆÅ&÷VæE&V7Bƒ"Âg’²‚Â45%õrÒ#BÂC"ÂÂV”vÆ72ò4UEô4$EôtÄ52¢4UEô4$Eô$r“°¢–b†¶'564f–VÆBÓÒb’G&u&÷VæE&V7Bƒ"Âg’²‚Â45%õrÒ#BÂC"ÂÂ&v#ScRƒsÃ3Ã#C’“°¢G&uFW‡D6Æ—ƒ#BÂg’²3ÂbÓÒò¶'564¢¶'564RÂ"Â4UEõE…Eô„’Â45%õrÒ3“°¢Ð¢²–çB'’Ò´%5õDõ²c°¢f–ÆÅ&÷VæE&V7Bƒ"Â'’Â…45%õrÒ3b’ò"ÂCbÂ"Â&v#ScRƒcÃÃ#3R’“°¢G&uFW‡D2ƒ"²…45%õrÒ3b’òBÂ'’²BÂ$wV&F""Â"Â&v#ScRƒ#SRÃ#SRÃ#SR’“°¢f–ÆÅ&÷VæE&V7Bƒ#B²…45%õrÒ3b’ò"Â'’Â…45%õrÒ3b’ò"ÂCbÂ"Â&v#ScRƒSÃcÃc’“°¢G&uFW‡D2ƒ#B²…45%õrÒ3b’ò"²…45%õrÒ3b’òBÂ'’²BÂ$&÷'&""Â"Â&v#ScRƒ#SRÃ#SRÃ#SR’“²Ð¢¶'4VF—F÷$¶"‚“°¢Ð¢¶'46öçFVçD‚Ò‡’²¶'567&öÆÂ’Ò´%5õDõ²#°§Ð§7FF–26öç7B6†"¢¶'5F—FÆR‚—°¢7v—F6‚†¶'5vR—°¢66R´%5ôDU4”tã¢&WGW&â$F—6UÇ„35Ç„#ò’FÖÇ„35Ç„#ò#°¢66R´%5õDõT4ƒ¢&WGW&â$FW6Æ—¦"’Fö6"#°¢66R´%5õ5”Õ3¢&WGW&â%5Ç„35Ç„FÖ&öÆ÷2#°¢66R´%5õ4„õ%C¢&WGW&â$F¦÷2FRFW‡Fò#°¢66R´%5õ44TD•C¢&WGW&â$VF—F"F¦ò#°¢66R´%5ô$õUC¢&WGW&â%6ö'&RFV6ÆFò#°¢66R´%5ôÄäs¢&WGW&â$–F–öÖ2’F—÷2#°¢FVfVÇC¢&WGW&â%FV6ÆFò#°¢Ð§Ð¢òò–çFÆçFÆÆ6ö×ÆWFVâVÂ'VffW"VRF÷VR†f"F—&V7FòÂò&'Vb7VæFð¢òò6Rfæ–Ö"ÆG&ç6–6–öâ’à§7FF–2fö–B¶'5–çB‚—°¢f–ÆÅ&V7BƒÂÂ45%õrÂ45%ô‚Â¶'4&r‚’“°¢7G&ö¶U6Vtƒ3ÂCÂ‚Â3"Â"ãFbÂ4UEõE…Eô„’“²òòfÆV6†FRföÇfW ¢7G&ö¶U6Vtƒ‚Â3"Â3Â#BÂ"ãFbÂ4UEõE…Eô„’“°¢G&uFW‡BƒS"Â#Â¶'5F—FÆR‚’Â2Â4UEõE…Eô„’“°¢–çB3Òt6Æ—“Â3Òt6Æ—“°¢t6Æ—“Ò´%5õDõÒƒ²t6Æ—“Ò´%5ô$õC°¢¶'46öçFVçB‚“°¢t6Æ—“Ò3²t6Æ—“Ò3°§Ð¢òò6R6ö×öæRVâ&'Vb’6RV&Æ–6FRVæfW£¢VÂ7VG&òÆÆVvVçFW&òÂæVÂÀ¢òòçVæ6ÖVF–2‡fW"VÂ6æFFòFR6ö×÷6–6–öâVâfÇ…&W6VçFW"’à§7FF–2fö–B¶'5&VæFW"‚—°¢6WD'Vb†&'Vb“°¢¶'5–çB‚“°¢&W6VçBƒÂ45%ô‚Ò“°¢6WD'Vb†f"“°§Ð¢òò&W–çFFò4ôÄòFRÆ&æFFRÆÆ—7FâW2ÆòVR6RW67VG&ò7VG&ð¢òòÖ–VçG&2VÂFVFò'&7G&¢æ’6&V6W&æ’föæFò6ö×ÆWFòÂ6öÆòÆ&æFâVÀ¢òòf–G&–òFRÆ2F&¦WF24’6RÖçF–VæR†G&tvÆ746&DfÆBÆò&W7VVÇfRVæfW ¢òò’ÆògVVÆ6÷"f–Æ2’Â6’VRVÂ67&öÆÂfVvFòÂFVFò6–âW&FW"VÀ¢òòÖFW&–Âæ’'FV"à§7FF–2fö–B¶'5&VæFW$Æ—7B‚—°¢6WD'Vb†&'Vb“°¢f–ÆÅ&V7BƒÂ´%5õDõÒÂ45%õrÂ´%5ô$õBÒ„´%5õDõÒ’Â¶'4&r‚’“°¢–çB3Òt6Æ—“Â3Òt6Æ—“°¢t6Æ—“Ò´%5õDõÒ²t6Æ—“Ò´%5ô$õC°¢¶'46öçFVçB‚“°¢t6Æ—“Ò3²t6Æ—“Ò3°¢&W6VçB„´%5õDõÒÂ´%5ô$õB“°¢6WD'Vb†f"“°§Ð¢òòd4RrÒG&ç6–6–öâVçG&RçFÆÆ2FVÂFV6ÆFòâäò6R–çfVçFVâW7F–Æð¢òòçVWfó¢6R&WW6tæ–Õ7G–ÆRƒ¦ööÒÂgVæF–FòÂ"FW6Æ—¦"’ÂVÂÖ—6Öò§W7FP¢òòVR–vö&–W&æÆW'GW&FR2â6R6ö×öæRVâ&'Vb’6RgVVÆ6²çVæ6¢òò6RF–'V¦ÖVF–2VâçFÆÆà§7FF–2fö–B¶'5&VæFW$æ–Ò‚—°¢–b‚´%ôä”ÕõôÄ•4…ôôâÇÂ&'Vb—²¶'5&VæFW"‚“²&WGW&ã²Ð¢6WD'Vb†&'Vb“°¢¶'5–çB‚“°¢6WD'Vb†f"“°¢6öç7B–çB7FW2Òc°¢f÷"†–çB2Ò²2ÃÒ7FW3²2²²—°¢fÆöBÒ†fÆöB—2ò7FW3°¢–b†tæ–Õ7G–ÆRÓÒ"—²òòFW6Æ—¦"FW6FRÆFW&V6†¢–çBöfbÒ†–çB’‚ƒãbÒ’¢45%õr“°¢f÷"†–çB’Ò²’Â45%ôƒ²’²²—°¢V–çCe÷B¢BÒf"²‡6—¦U÷B—’¢45%õs°¢6öç7BV–çCe÷B¢7Ò&'Vb²‡6—¦U÷B—’¢45%õs°¢f÷"†–çB‚Ò²‚Âöfc²‚²²’E·…ÒÒ¶'4&r‚“°¢ÖVÖ7’†B²öfbÂ7Â‡6—¦U÷B’…45%õrÒöfb’¢"“°¢Ð¢ÒVÇ6R–b†tæ–Õ7G–ÆRÓÒ—²òògVæF–Fð¢V–çC…÷BÒ‡V–çC…÷B’‡¢#SR“°¢f÷"†–çB’Ò²’Â45%ôƒ²’²²—°¢V–çCe÷B¢BÒf"²‡6—¦U÷B—’¢45%õs°¢6öç7BV–çCe÷B¢7Ò&'Vb²‡6—¦U÷B—’¢45%õs°¢f÷"†–çB‚Ò²‚Â45%õs²‚²²’E·…ÒÒÖ—ƒScR†E·…ÒÂ7·…ÒÂ“°¢Ð¢ÒVÇ6R²òò¦ööÒ†FVÂƒ‚RÂR¢fÆöB²Òãƒ†b²ã&b¢°¢–çB7rÒ†–çB’…45%õr¢²’Â6‚Ò†–çB’…45%ô‚¢²“°¢–çB÷‚Ò…45%õrÒ7r’ò"Â÷’Ò…45%ô‚Ò6‚’ò#°¢f÷"†–çB’Ò²’Â45%ôƒ²’²²—°¢V–çCe÷B¢BÒf"²‡6—¦U÷B—’¢45%õs°¢–çB7’Ò‡’Ò÷’’¢45%ô‚ò†6‚âò6‚¢“°¢–b‡’Â÷’ÇÂ’ãÒ÷’²6‚ÇÂ7’ÂÇÂ7’ãÒ45%ô‚—²f÷"†–çB‚Ò²‚Â45%õs²‚²²’E·…ÒÒ¶'4&r‚“²6öçF–çVS²Ð¢6öç7BV–çCe÷B¢7Ò&'Vb²‡6—¦U÷B—7’¢45%õs°¢f÷"†–çB‚Ò²‚Â45%õs²‚²²—°¢–çB7‚Ò‡‚Ò÷‚’¢45%õrò†7râò7r¢“°¢E·…ÒÒ‡‚Â÷‚ÇÂ‚ãÒ÷‚²7rÇÂ7‚ÂÇÂ7‚ãÒ45%õr’ò¶'4&r‚’¢7·7…Ó°¢Ð¢Ð¢Ð¢fÇ„fÇW6„ÆÂ‚“°¢FVÆ’ƒ"“°¢Ð¢òòf÷Föw&Öf–æÂU„5Dò†Æ2–çFW'öÆ6–öæW2FV¦â&VFöæFV÷2“¢6R6÷–VÀ¢òò'VffW"'VVæòFÂ7VÂÂ&VRÆçFÆÆVRVVFæò6VÆ&÷†–ÖFà¢f$6÷”&æB†&'VbÂÂ45%ô‚Ò“°¢fÇ„fÇW6„ÆÂ‚“°§Ð§7FF–2fö–B¶'4vò†–çBvR—°¢¶'5vRÒvS²¶'567&öÆÂÒ°¢¶'4G&vv–ærÒfÇ6S²òò6Ö&–"FRv–æ6æ6VÆ7VÇV–W"'&7G&Rf—fð¢¶'5&VæFW$æ–Ò‚“°§Ð§7FF–2fö–B¶'4VçFW"‚—°¢–b‚´%õ4UED”äu5ôôâ’&WGW&ã°¢¶'5&WBÒ†u7FFRÓÒ5Eôbbt–BÓÒ"’ò¢²òò"Ò§W7FW0¢¶$W‡G&4öâÒfÇ6S²òòV’VÂFV6ÆFòæòÆÆWf&'&æ’6†—0¢¶$Ç•6—¦R‚“²¶$×E7W&f6U&W6WB‚“°¢u7FFRÒ5Eô´%4UC²¶'5vRÒ´%5ôÔ”ã²¶'567&öÆÂÒ°¢¶'4G&vv–ærÒfÇ6S°¢¶'5&VæFW"‚“°§Ð§7FF–2fö–B¶'4W†—B‚—°¢–b†¶'5&WBÓÒ—²u7FFRÒ5Eô²6WGF–æw5&VæFW"‚“²&WGW&ã²Ð¢u7FFRÒ5Eô²¶$W‡G&4öâÒG'VS²¶$Ç•6—¦R‚“²¶$6†—4'V–ÆB‚“²æ÷FU&VæFW$ÆÂ‚“°§Ð§7FF–2fö–B¶'5&W6WDFVfVÇG2‚—°¢t¶%6—¦RÒ´%õ4•¤Uôäõ$ÔÃ²t¶$f7EG—RÒG'VS²t¶%FööÆ&"ÒG'VS²t¶%&VF–7BÒG'VS°¢t¶%7VÆÂÒfÇ6S²t¶$VÖö¦•7VrÒfÇ6S²t¶$†”6öâÒfÇ6S²t¶$÷6—G’Ò°¢t¶%7G–ÆRÒ²t¶$föçE62Ò²t¶$Ç×2ÒS²t¶$g„×2Ò°¢f÷"†–çB’Ò²’Â´%õ5”Õ3²’²²’t¶%7–Õ¶•ÒÒ“°¢¶%6†÷'F7WG4FVfVÇG2‚“°¢¶$Ç•6—¦R‚“²¶%&Vg56fR‚“°§Ð¢òò66–öâÂFö6"Æf–Æ–G‚FRÆv–æ7GVÂà§7FF–2fö–B¶'5&÷t7F–öâ†–çB–G‚—°¢–b†¶'5vRÓÒ´%5ôÔ”â—°¢7v—F6‚†–G‚—°¢66R¢¶'4vò„´%5ôÄär“²&WGW&ã°¢66R¢t¶%&VF–7BÒt¶%&VF–7C²'&V³°¢66R#¢t¶%7VÆÂÒt¶%7VÆÃ²'&V³°¢66R3¢t¶$VÖö¦•7VrÒt¶$VÖö¦•7Vs²'&V³°¢66RC¢¶'4vò„´%5õ4„õ%B“²&WGW&ã°¢66RS¢t¶%FööÆ&"Òt¶%FööÆ&#²'&V³°¢66Rc¢t¶$†”6öâÒt¶$†”6öã²'&V³°¢66Rs¢t¶$f7EG—RÒt¶$f7EG—S²¶$×E7W&f6U&W6WB‚“²'&V³°¢66Rƒ¢¶'4vò„´%5ôDU4”tâ“²&WGW&ã°¢66R“¢¶'4vò„´%5õDõT4‚“²&WGW&ã°¢66R¢66R¢&WGW&ã²òòf–Æ2–æf÷&ÖF—f2††&Gv&R¢66R#¢¶'5&W6WDFVfVÇG2‚“²¶'5&VæFW"‚“²&WGW&ã°¢66R3¢¶'4vò„´%5ô$õUB“²&WGW&ã°¢FVfVÇC¢&WGW&ã°¢Ð¢¶%&Vg56fR‚“²¶'5&VæFW"‚“²&WGW&ã°¢Ð¢–b†¶'5vRÓÒ´%5ôÄär—°¢–b†–G‚ÓÒÇÂ–G‚ÓÒ—°¢¶$ÆætW2Ò†–G‚ÓÒ“°¢–b†Ö7F—fòÓÒÄ”õUEôU2ÇÂÖ7F—fòÓÒÄ”õUEôTâ’Ö7F—fòÒ¶$ÆætW2òÄ”õUEôU2¢Ä”õUEôTã°¢¶'5&VæFW"‚“°¢Ð¢&WGW&ã°¢Ð¢–b†¶'5vRÓÒ´%5ôDU4”tâ—°¢–çB²Ò–G‚²„´%õ4•¤Uô4ôäd”uôôâò¢“²òò6–âf–ÆFRFÖæòÂFöFò7V&RVâVW7Fð¢–b†²ÓÒ—²t¶%6—¦RÒ†t¶%6—¦R²’R3²¶$Ç•6—¦R‚“²Ð¢VÇ6R–b†²ÓÒ—²t¶$÷6—G’ÓÒS²–b†t¶$÷6—G’ÂC’t¶$÷6—G’Ò²Ð¢VÇ6R–b†²ÓÒ"—²t¶%7G–ÆRÒ†t¶%7G–ÆR²’R3²Ð¢VÇ6R–b†²ÓÒ2—²t¶$föçE62Ò†t¶$föçE62²’R3²Ð¢VÇ6R–b†²ÓÒB—²¶'4vò„´%5õ5”Õ2“²&WGW&ã²Ð¢VÇ6R&WGW&ã°¢¶%&Vg56fR‚“²¶'5&VæFW"‚“²&WGW&ã°¢Ð¢–b†¶'5vRÓÒ´%5õDõT4‚—°¢–b†–G‚ÓÒ—²t¶$g„×2Ò†t¶$g„×2ÓÒc’ò¢†t¶$g„×2ÓÒ’òc¢c²Ð¢VÇ6R–b†–G‚ÓÒ—²t¶$Ç×2Ò†t¶$Ç×2ÓÒ3S’òS¢†t¶$Ç×2ÓÒS’òs¢3S²Ð¢VÇ6R&WGW&ã°¢¶%&Vg56fR‚“²¶'5&VæFW"‚“²&WGW&ã°¢Ð¢–b†¶'5vRÓÒ´%5õ5”Õ2—°¢–b†–G‚ãÒbb–G‚Â´%õ5”Õ2—°¢t¶%7–Õ¶–G…ÒÒ†t¶%7–Õ¶–G…Ò²’R´%õ5”ÕõôôÅôã°¢¶%&Vg56fR‚“²¶'5&VæFW"‚“°¢Ð¢&WGW&ã°¢Ð¢–b†¶'5vRÓÒ´%5õ4„õ%B—°¢–b†–G‚ãÒbb–G‚Â´%õ45ôÔ‚—°¢¶'5656VÂÒ–Gƒ²¶'564f–VÆBÒ°¢6ç&–çFb†¶'564Â6—¦Vöb†¶'564’Â"W2"Ât¶%64'%¶–G…Ò“°¢6ç&–çFb†¶'564RÂ6—¦Vöb†¶'564R’Â"W2"Ât¶%64W‡¶–G…Ò“°¢¶'4vò„´%5õ44TD•B“°¢Ð¢&WGW&ã°¢Ð§Ð§7FF–2fö–B¶'5F–6²‚—°¢–b‚´%õ4UED”äu5ôôâ—²u7FFRÒ5Eô²&WGW&ã²Ð¢–b…BçFbbBç‚ÂC‚bbBç’ÂS"—²òòföÇfW ¢–b†¶'5vRÓÒ´%5ôÔ”â’¶'4W†—B‚“°¢VÇ6R–b†¶'5vRÓÒ´%5õ44TD•B’¶'4vò„´%5õ4„õ%B“°¢VÇ6R–b†¶'5vRÓÒ´%5õ5”Õ2’¶'4vò„´%5ôDU4”tâ“°¢VÇ6R¶'4vò„´%5ôÔ”â“°¢&WGW&ã°¢Ð¢–b†¶'5vRÓÒ´%5õ44TD•B—°¢–b‚BçF’&WGW&ã°¢f÷"†–çBbÒ²bÂ#²b²²—°¢–çBg’Ò´%5õDõ²b¢sc°¢–b…Bç’ãÒg’²‚bbBç’ÃÒg’²c—²¶'564f–VÆBÒc²¶'5&VæFW"‚“²&WGW&ã²Ð¢Ð¢²–çB'’Ò´%5õDõ²cÂ‡rÒ…45%õrÒ3b’ò#°¢–b…Bç’ãÒ'’bbBç’ÃÒ'’²Cb—°¢–b…Bç‚ãÒ"bbBç‚ÃÒ"²‡r—²òòwV&F ¢–b†¶'564³Òbb¶'564U³Ò—²6ç&–çFb†t¶%64'%¶¶'5656VÅÒÂ´%õ45ô%"Â"W2"Â¶'564“²6ç&–çFb†t¶%64W‡¶¶'5656VÅÒÂ´%õ45ôU…Â"W2"Â¶'564R“²Ð¢¶%&Vg56fR‚“²¶'4vò„´%5õ4„õ%B“²&WGW&ã°¢Ð¢–b…Bç‚ãÒ#B²‡r—²òò&÷'&"VÂF¦ð¢t¶%64'%¶¶'5656VÅÕ³ÒÒ²t¶%64W‡¶¶'5656VÅÕ³ÒÒ°¢¶%&Vg56fR‚“²¶'4vò„´%5õ4„õ%B“²&WGW&ã°¢Ð¢ÒÐ¢–çBf’Ò¶$e&÷t†—B…Bç‚ÂBç’“°¢–b†f’ãÒ—°¢–b†f’ÓÒ’¶%6†–gBÒ¶%6†–gC°¢VÇ6R–b†f’ÓÒ’Ö7F—fòÒ†Ö7F—fòÓÒÄ”õUEôåTÒ’òÄ”õUEôTÔô¤’¢†Ö7F—fòÓÒÄ”õUEôTÔô¤’’ò†¶$ÆætW2òÄ”õUEôU2¢Ä”õUEôTâ’¢Ä”õUEôåTÓ°¢VÇ6R–b†f’ÓÒ"—²¶$ÆætW2Ò¶$ÆætW3²–b†Ö7F—fòÓÒÄ”õUEôU2ÇÂÖ7F—fòÓÒÄ”õUEôTâ’Ö7F—fòÒ¶$ÆætW2òÄ”õUEôU2¢Ä”õUEôTã²Ð¢VÇ6R–b†f’ÓÒ2’¶'4VæDf–VÆB‚""“°¢VÇ6R–b†f’ÓÒB’¶'4&6´f–VÆB‚“°¢VÇ6R²–b†¶'564³Òbb¶'564U³Ò—²6ç&–çFb†t¶%64'%¶¶'5656VÅÒÂ´%õ45ô%"Â"W2"Â¶'564“²6ç&–çFb†t¶%64W‡¶¶'5656VÅÒÂ´%õ45ôU…Â"W2"Â¶'564R“²¶%&Vg56fR‚“²¶'4vò„´%5õ4„õ%B“²&WGW&ã²ÒÐ¢¶'5&VæFW"‚“²&WGW&ã°¢Ð¢–çB6VÆÂÒ¶$6VÆÄB…Bç‚ÂBç’“°¢–b†6VÆÂãÒ—°¢6öç7B6†"¢²ÒÖ7F—fõ¶6VÆÂò´%ô4ôÅ5Õ¶6VÆÂR´%ô4ôÅ5Ó°¢–b†¶%6†–gBbbµ³ÒÓÒbbµ³ÒãÒvrbbµ³ÒÃÒw¢r—²6†"U³%ÒÒ²†6†"’†µ³ÒÒ3"’ÂÓ²¶'4VæDf–VÆB‡R“²¶%6†–gBÒfÇ6S²Ð¢VÇ6R¶'4VæDf–VÆB†²“°¢¶'5&VæFW"‚“°¢Ð¢&WGW&ã°¢Ð¢òò%$5E$R$TÂFRÆÆ—7F†çFW2W&â6ÇF÷2FRC‚Â6öÇF"’âVÀ¢òò6öçFVæ–Fò6–wVRÂFVFò7VG&ò7VG&òÂ&W–çFæFò6öÆò7R&æF’6öâÆ0¢òòF&¦WF2Ææ2Ö–VçG&2GW&²Â6öÇF"ÂVâ&W–çFFò'VVæò6öâf–G&–òà¢–çBgÒ´%5ô$õBÒ´%5õDõÂÖ…2Ò¶'46öçFVçD‚Òg²–b†Ö…2Â’Ö…2Ò°¢–b…Bç&W76VB—²¶'4G&u“ÒBç“²¶'4G&u3Ò¶'567&öÆÃ²¶'4G&vv–ærÒfÇ6S²Ð¢–b…BæF÷vâbbÖ…2â—°¢–çBG’Ò¶'4G&u“ÒBç“°¢–b‚¶'4G&vv–ærbb'2†G’’âb—²¶'4G&vv–ærÒG'VS²Ð¢–b†¶'4G&vv–ær—°¢–çBç2Ò¶'4G&u3²G“°¢–b†ç2Â’ç2Ò²–b†ç2âÖ…2’ç2ÒÖ…3°¢–b†ç2Ò¶'567&öÆÂ—²¶'567&öÆÂÒç3²¶'5&VæFW$Æ—7B‚“²Ð¢&WGW&ã°¢Ð¢Ð¢–b…Bç&VÆV6VBbb¶'4G&vv–ær—²¶'4G&vv–ærÒfÇ6S²¶'5&VæFW"‚“²&WGW&ã²Ð¢–b…BçFbb¶'4G&vv–ærbbBç’ãÒ´%5õDõÒ‚bbBç’ÃÒ´%5ô$õB—°¢f÷"†–çB’Ò²’Â¶'5&÷tã²’²²’–b…Bç’ãÒ¶'5&÷u“¶•ÒbbBç’Â¶'5&÷u“¶•Ò—²¶'5&÷t7F–öâ†’“²&WGW&ã²Ð¢Ð§Ð ¢òò2222222222222222222222222222222222222222222222222222222222220¢òò2224”ÕÄU3¢ÆÖ6VæÖ–VçFòÂVGV66–öâÂæfVvF÷"À¢òò226öFR”DRÂ–çB†gVæ6–öæÂ’Â§VVv÷0¢òò2222222222222222222222222222222222222222222222222222222222220¢òò&'&WF—VWF·fÆ÷"·&öw&W6òÂF–ÖVç6–öæFÂÆ–Vç¦ò†ÆW6âÆÖ6VæÖ–VçFð¢òò’7VÇV–W"VRV–W&Væf–ÆFRÖVF–F÷"’à§7FF–2–çB6–×&"†–çB’Â6öç7B6†"¢Æ&VÂÂ6öç7B6†"¢fÂÂ–çB7BÂV–çCe÷B6öÂ—°¢–çB'‡‚Â'—’Â'wrÂ&†ƒ²V”&÷‚†'‡‚Â'—’Â'wrÂ&†‚“°¢–çBBÒV•B‚“°¢–çB'‚Ò'‡‚²B¢"Â'rÒ'wrÒB¢C°¢–b†'rÂc—²'‚Ò'‡‚²C²'rÒ'wrÒ"¢C²Ð¢–çBg2ÒV”föçDf—B†Æ&VÂÂ'rò"Â"“°¢G&uFW‡B†'‚Â’ÂÆ&VÂÂg2Â&v#ScRƒ##RÃ##’Ã#C’“°¢G&uFW‡E"†'‚²'rÂ’ÂfÂÂV”föçDf—B‡fÂÂ'rò"Â"’Â&v#ScRƒƒÃƒ‚Ã#R’“°¢–çB&$‚Ò&†‚ò##²–b†&$‚Â’’&$‚Ò“²–b†&$‚â#’&$‚Ò#°¢–çB'—"Ò’²V”Æ–æT‚†g2’²c°¢f–ÆÅ&÷VæE&V7B†'‚Â'—"Â'rÂ&$‚Â&$‚ò"Â&v#ScRƒC‚ÃS"Ãcb’“°¢–b‡7Bâ’f–ÆÅ&÷VæE&V7B†'‚Â'—"Â'r¢7BòÂ&$‚Â&$‚ò"Â6öÂ“°¢&WGW&â'—"²&$‚²V•B‚“°§Ð¢òòÄÔ4TäÔ”TåDò+rFFF—fà¢òòW6Væ6–Â¢Æ2&'&2FRfÆ6‚’5$Òà¢òò÷6–öæÂ¢&W7VÖVâVâF&¦WF26öâF÷FÆW2ÒÒ&V6R7VæFòVVFà¢òòãÒƒ‚Æ–'&W2&¦òÆ2&'&2‡6’æòÂ6RöÖ—FRVçFW&ò’à¢òò÷6–öæÂ"¢–RFR—VFÒÒ&V6R6’Vâ6ö'&âãÒ‚‚à§7FF–2fö–BÆÔVçFW"‚—°¢6WD'Vb†f"“°¢–çB'‚Â'’Â'rÂ&ƒ²V”&÷‚†'‚Â'’Â'rÂ&‚“°¢f–ÆÅ&V7B†'‚Â'’Â'rÂ&‚Ât”åô$r“°¢–çBBÒV•B‚’ÂvÒV”v‚“°¢–çB’Ò'’²C°¢’ÒV•F—FÆR†'‚Â’Â'rÂ$ÆÖ6VæÖ–VçFò"Â&v#ScRƒ#SRÃ#SRÃ#SR’ÂV”föçD‚†&‚ò"’“°¢’³Òvò#°¢’Ò6–×&"‡’Â$fÆ6‚‡6—7FVÖ’"Â'ã"òbÔ""Â2Â&v#ScRƒ“ÃcÃ#C’“°¢6—¦U÷BbÒ†Vö65övWEög&VU÷6—¦R„ÔÄÄô5ô4õ5•$Ò’ÂBÒ†Vö65övWE÷F÷FÅ÷6—¦R„ÔÄÄô5ô4õ5•$Ò“°¢–çBWÒBâò†–çB’ƒÒ‡V–çCcE÷B—b¢òB’¢°¢6†"e³CÓ²6ç&–çFb‡bÂ6—¦Vöb‡b’Â"VBRRVâW6ò"ÂW“°¢’Ò6–×&"‡’Â%5$Ò"ÂbÂWÂ&v#ScRƒ“ÃƒÃ#’“°¢–çB&W7BÒ†'’²&‚’Ò’ÒC°¢V–çC…÷B7VÒÒV•6V7F–öâƒÂ&W7BãÒƒbb'rãÒ#C“°¢–b†7VÒ—°¢–çBâÒ2ÂrÒvÂ7rÒ†'rÒ"¢BÒ†âÒ’¢r’òã°¢–çB6†‚Ò&W7Bâ#ò#¢&W7C°¢6†"C³#EÒÂC%³#EÒÂC5³#EÓ°¢6ç&–çFb‡CÂ6—¦Vöb‡C’Â"WRÔ""Â‡Vç6–væVB’‡Bòƒ#B¢#B’’“°¢6ç&–çFb‡C"Â6—¦Vöb‡C"’Â"WRÔ""Â‡Vç6–væVB’‡bòƒ#B¢#B’’“°¢6ç&–çFb‡C2Â6—¦Vöb‡C2’Â"WR´""Â‡Vç6–væVB’†W7övWEög&VUö†V÷6—¦R‚’ò#B’“°¢6öç7B6†"¢Æ%³5ÒÒ²%5$ÒF÷FÂ"Â%5$ÒÆ–'&R"Â%$ÒÆ–'&R"Ó°¢6öç7B6†"¢fÅ³5ÒÒ²CÂC"ÂC2Ó°¢f÷"†–çB’Ò²’Âã²’²²—°¢–çB‚Ò'‚²B²’¢†7r²r“°¢V•&V7D‡‚Â’Â7rÂ6†‚ÂV•B‚’Â&v#ScRƒ3Ã3BÃCb’Â7VÒ“°¢V•FW‡D2‡‚²7rò"Â’²6†‚ò"ÒV”Æ–æT‚ƒ"’Ò‚ÂÆ%¶•ÒÂV”föçDf—B†Æ%¶•ÒÂ7rÒ"Â"’Â&v#ScRƒSÃcÃƒR’Â7VÒ“°¢V•FW‡D2‡‚²7rò"Â’²6†‚ò"²"ÂfÅ¶•ÒÂV”föçDf—B‡fÅ¶•ÒÂ7rÒ"Â2’Â&v#ScRƒ##RÃ#3"Ã#CR’Â7VÒ“°¢Ð¢’³Ò6†‚²v°¢Ð¢fÇ„fÇW6‚…t”åõDõÂt”åô$õB“°§Ð¢òòTET44”ôâ‡’7VÇV–W"FRÆ—7FFRF&¦WF2’+rFFF—fà¢òòW6Væ6–Â¢ÆÆ—7FFRF&¦WF2Â&W'F–FVâ4ôÅTÔä26VwVâVÂæ6†ð¢òò‡Væ6öÇVÖææV6W6—FãÒ##‚’ÂFRÖöFòVRÂVç6æ6†"Æ¢òòfVçFææòVVFÖVF–òÆ–Vç¦òVâ&Ææ6ó¢6"ò26öÇVÖæ2à¢òò÷6–öæÂ¢7V'F—GVÆò%&÷†–ÖÖVçFR"FVçG&òFR6FF&¦WFÒÒ&V6P¢òò7VæFòÆF&¦WFF–VæRãÒSb‚FRÇFò‡6’æòÂ6öÆòVÀ¢òòF—GVÆòÂVRW2ÆòW6Væ6–Â’à§7FF–2fö–B6–×6&G2†6öç7B6†"¢F—FÆRÂ6öç7B6†"¢—FV×5µÒÂ–çBâ—°¢6WD'Vb†f"“°¢–çB'‚Â'’Â'rÂ&ƒ²V”&÷‚†'‚Â'’Â'rÂ&‚“°¢f–ÆÅ&V7B†'‚Â'’Â'rÂ&‚Ât”åô$r“°¢–çBBÒV•B‚’ÂvÒV”v‚“°¢–çB“Ò'’²C°¢–çBg5BÒV”föçDf—B‡F—FÆRÂ'rÒ"¢BÂV”föçD‚†&‚ò"’“°¢G&uFW‡D2†'‚²'rò"Â“ÂF—FÆRÂg5BÂ&v#ScRƒ#SRÃ#SRÃ#SR’“°¢“³ÒV”Æ–æT‚†g5B’²v°¢–çB6öÇ2Ò†'rÒ"¢B²v’òƒ##²v“²–b†6öÇ2Â’6öÇ2Ò²–b†6öÇ2â2’6öÇ2Ò3°¢–çB&÷w2Ò†â²6öÇ2Ò’ò6öÇ3°¢–çB7rÒ†'rÒ"¢BÒ†6öÇ2Ò’¢v’ò6öÇ3°¢–çBf–Ä‚Ò†'’²&‚’Ò“ÒC°¢–çB6†‚Ò†f–Ä‚Ò‡&÷w2Ò’¢v’ò‡&÷w2âò&÷w2¢“°¢–b†6†‚â’6†‚Ò°¢–b†6†‚Â#b’6†‚Ò#c°¢V–çC…÷B7V"ÒV•6V7F–öâƒÂ6†‚ãÒSb“°¢–çB&BÒV•B‚“°¢f÷"†–çB’Ò²’Âã²’²²—°¢–çB2Ò’R6öÇ2Â"Ò’ò6öÇ3°¢–çB‚Ò'‚²B²2¢†7r²v’Â’Ò“²"¢†6†‚²v“°¢–b‡’²6†‚â'’²&‚’'&V³²òòçVæ6gVW&FVÂÖ&6ð¢–b‡V”vÆ72bbtÆæB’G&tÆ—V–DvÆ75æVÂ‡‚Â’Â7rÂ6†‚Â&BÂ&v#ScRƒcÃƒÃS’“°¢VÇ6Rf–ÆÅ&÷VæE&V7B‡‚Â’Â7rÂ6†‚Â&BÂ&v#ScRƒCÃCBÃS‚’“°¢–çBg4’ÒV”föçDf—B†—FV×5¶•ÒÂ7rÒ"¢BÂV”föçD‚†6†‚ò"’“°¢–çBG’Ò7V"ò‡’²6†‚ò"ÒV”Æ–æT‚†g4’’’¢‡’²6†‚ò"ÒV”Æ–æT‚†g4’’ò"“°¢G&uFW‡B‡‚²BÂG’Â—FV×5¶•ÒÂg4’Â&v#ScRƒ#SRÃ#SRÃ#SR’“°¢–b†7V"’V•FW‡B‡‚²BÂG’²V”Æ–æT‚†g4’’²BÂ%&÷†–ÖÖVçFR"ÂÂ&v#ScRƒSÃS‚Ãƒ’Â7V"“°¢Ð¢fÇ„fÇW6‚…t”åõDõÂt”åô$õB“°§Ð§7FF–2fö–BVGTVçFW"‚—²6öç7B6†"¢—E³EÒÒ²$VÆV7G%Ç„35Ç„#6æ–6%Ç„35Ç„6–6"Â%&öw&Ö6•Ç„35Ç„#6â2²²"Â%&VFW2’v”f’"Â%6Vç6÷&W2“$2"Ó²6–×6&G2‚$VGV66•Ç„35Ç„#6â"Â—BÂB“²Ð¢òò2222222222222222222222222222222222222222222222222222222222220¢òò22¥TTtó¢$vVòF6‚"ÒÒ6VÆV7F÷"FRæ—fVÆW2²Bæ—fVÆW2$TÄU0¢òò22…7FW&VòÖFæW72Â6ÇWGFW&gVæ²Â6âwBÆWBvòÂ&Æ7B&ö6W76–ær¢òò226öâRf÷&Ö2„7V&òõ6†—ô&ÆÂõvfRõTdò’Â÷'FÆW2FRf÷&Öð¢òò22FÖæòòw&fVFBòfVÆö6–FBÂÖöFò&7F–66öâ6†V6·ö–çG0¢òò22’&öw&W6òW'6—7FVçFR÷"æ—fVÂVâ&VfW&Væ6W2‚&fÆW†÷2"’à¢òò20¢òò22õ$”TåD4”ôâ„õ$•¤ôåDÃ¢6RF–'V¦&÷FFò“6ö'&RVÂæVÀ¢òò22÷'G&—BW6æFòVÂÖöFòÆæG66R–W†—7FVçFR†tÆæB’âVÀ¢òò22Æ–Vç¦òÄôt”4òW2ƒƒCƒ„År‚Ä‚’â–wVÂVRVÂÖöFò2ÂÆ¢òò22öæRtÆæC×G'VR’W2ÆVæ–6VRvW7F–öæÆçFÆÆ°¢òò22Â6Æ—"6R&W7FW&tÆæCÖfÇ6Rà¢òò20¢òò22G&öã¢ôõtåõDõT4‚Âô5U5DôÕô„TDU"ÒÒ÷6VRDôDÆ¢òò22çFÆÆ’DôDõ2Æ÷2F÷VW2Â6ö×öæRVâ&'Vb’gVVÆ66öà¢òò22&W6VçB‚’†çF’ÖfÆ–6¶W"’Â’G&R7R&÷–ò&÷FöâFR6Æ—"à¢òò20¢òò22FöFòVÂW7FFòW27FF–2æ—fVÂFR&6†—fò‡6–â†V’à¢òò22ä”äuTäf—&ÖFRgVæ6–öâW6Æ÷2F—÷2vVôö'7F6ÆRôvVõG&–Âð¢òò22vVõ'BôvVôÆWfVÂÂ6’Æ÷2&÷F÷F—÷2VRWFòÖvVæW&VÂ”DP¢òò22†–ç6W'FF÷2%$”$FVÂ&6†—fòÂçFW2FRW7F2FVf–æ–6–öæW2¢òò22çVæ6&VfW&Væ6–âVâF—òFW66öæö6–FòÓâ6ö×–ÆÆ–×–òà¢òò2222222222222222222222222222222222222222222222222222222222220 ¢òòÒÒÒÒvVöÖWG&–FVÂW66Væ&–ò†6ö÷&G2Äôt”42ÆæG66S¢æ6†òÅsÓƒÂÇFòÄƒÓCƒ’ÒÒÒÐ¢6FVf–æRtTõõE23BòòFÖæòFR&ÆF÷6‡–æ6†òö&Æ÷VR¢6FVf–æRtTõõÂ3BòòÆFòFVÂ§VvF÷"FÖæòäõ$ÔÂ†Ö–æ’ÒÆÖ—FB¢6FVf–æRtTõõÅ‚Còò‚f–¦FVÂ§VvF÷ ¢6FVf–æRtTõô…TEô‚Còò&æF7WW&–÷"‡6Æ—"²&öw&W6ò¢6FVf–æRtTõôdÄôõ%õ’Còò’FVÂ7VVÆò‡7WW&f–6–R7WW&–÷"FRÆg&æ¦FR7VVÆò¢6FVf–æRtTõô4T”Åõ’ƒBòò’FVÂFV6†ò‡7WW&f–6–RVâw&fVFB–çfW'F–Fò6÷'&VF÷&W2 ¢òòÒÒÒÒf—6–6‡‚÷6Vs²GB&VÂf–Ö–ÆÆ—2‚’’ÒÒÒÐ¢6FVf–æRtTõôu$b#cãbòòw&fVFB†7V&òò&ÆÂ¢6FVf–æRtTõô¥TÕs#ãbòò–×VÇ6òFR6ÇFòFVÂ7V&ò†–6Rã‚¢6FVf–æRtTõôdÄÄ43ãbòòF÷RFRfVÆö6–FBFR6–F†7V&òò&ÆÂ¢6FVf–æRtTõõ5TTB#3ãbòòfVÆö6–FB&6RFR67&öÆÂFVÂ×VæFð¢6FVf–æRtTõõ$õB2ãfbòòfVÂâFRv—&ò6÷6ÖWF–6ò‡&B÷2¢6FVf–æRtTõõ4„•ôu$bSãbòòw&fVFB&VGV6–FFVÂ6†— ¢6FVf–æRtTõõ4„•ô423#ãbòòV×V¦RFVÂ6†—ÂÖçFVæW"Fö6Fð¢6FVf–æRtTõõ4„•õdÔ‚S#ãbòòF÷RFRfVÆö6–FBfW'F–6ÂFVÂ6†— ¢6FVf–æRtTõõTdõôu$bSãbòòw&fVFBFVÂVfò†fÆ’¢6FVf–æRtTõõTdõô”ÕCsãbòò–×VÇ6ò6÷'FòFVÂVfò÷"6FF ¢6FVf–æRtTõõTdõõdÔ‚ccãbòòF÷RFRfVÆö6–FBfW'F–6ÂFVÂVfð¢6FVf–æRtTõôÄäEDôÂ3òò†öÆwW&FR&FW'&—¦"FW6FR'&–&"6ö'&RVâ&Æ÷VP¢6FVf–æRtTõôe$ÔUôÕ232òòF‡&÷GFÆRFR&VæFW"‡ã3e2W7F&ÆW2¢6FVf–æRtTõôEDÔ‚ãVbòòGBÖ†–Öò÷"g&ÖR‡F÷R6’VÂÆö÷6RG&&¢6FVf–æRtTõõ5T%5DUã&bòò6òf–¦òFRÆf—6–6†Wf—FGVæVÆW27VÇV–W"e2¢6FVf–æRtTõõ$U5tåôÕ2S#òòW7W&G&2Ö÷&—"çFW2FR&V&V6W  ¢òòÒÒÒÒW7FVÆ’'F–7VÆ2ÒÒÒÐ¢6FVf–æRtTõõE$”Åôâ# ¢6FVf–æRtTõõE$”ÅôdDR#ãbòòF—7Fæ6–‡‚FR67&öÆÂ’G&2ÆVRÆW7FVÆ6Rv¢6FVf–æRtTõõ%Eôâ€ ¢òòÒÒÒÒÆWF&6R†Æ÷26öÆ÷&W2FR–FVçF–FBFR6Fæ—fVÂ6RÆ–6â'FR’ÒÒÒÐ¢6FVf–æRtTõô”ä²&v#ScRƒÃ"Ã#b’òò&VÆÆVæò÷67W&òFRö'7F7VÆ÷0¢6FVf–æRtTõõE…B&v#ScRƒ#3"Ã#3bÃ#C‚¢6FVf–æRtTõôÔ$U"&v#ScRƒ#SÃ#Ãs’òò÷'FÂFRw&fVF@¢6FVf–æRtTõô5”â&v#ScRƒ“Ã##Ã#C’òò÷'FÂFRfVÆö6–F@¢6FVf–æRtTõõU%&v#ScRƒ#Ã#Ã#SR’òò÷'FÂFRf÷&Ö¢6FVf–æRtTõôÄ”ÔR&v#ScRƒSÃ#CÃ#’òò÷'FÂFRFÖæð ¢òòÒÒÒÒF—÷2FRö'7F7VÆò†Æ÷2&–ÖW&÷2b6öç6W'fâ7RfÆ÷"÷&–v–æÂ’ÒÒÒÐ¦VçVÒ²ô%5õ”4òÒÂô%5ô$ÄõTRÂô%5ô…TT4òÂô%5õ”4õõBÀ¢ô%5õõ%DÅôu$bÂô%5õõ%DÅõdTÂÀ¢ô%5õô5T$òÂô%5õõ4„•Âô%5õô$ÄÂÂô%5õõtdRÂô%5õõTdòÀ¢ô%5õôÔ”ä’Âô%5õôäõ$ÔÂÂô%5ô$ÄõTUôbÓ° ¢òò‚Ò÷6–6–öâVâVÂÕTäDò‡‚“²F—ó²&ÒÒÇGW&Vâ&ÆF÷62†&Æ÷VR’À¢òòæ6†òVâ&ÆF÷62†‡VV6ò’ò7V'F—ò‡÷'FÂFRfVÆö6–FB’à§7G'V7BvVôö'7F6ÆR²–çCe÷Bƒ²V–çC…÷BF—ó²V–çC…÷B&Ó²Ó° ¢òòÒÒÒÒf÷&Ö2FVÂ§VvF÷"ÒÒÒÐ¦VçVÒ²e$Õô5T$òÒÂe$Õõ4„•Âe$Õô$ÄÂÂe$ÕõtdRÂe$ÕõTdòÓ° ¢òò2222222222222222222222222222222222222222222222222222222222220¢òò22DDõ2DRÄõ2Bä•dTÄU2†æ—fVÂÒDDÂæòF–'V¦òÖæò¢òò2222222222222222222222222222222222222222222222222222222222220 ¢òòä•dTÂÒÒ5DU$TòÔDäU52ƒ¢“¢7V&òGWF÷&–Â²VâG&ÖòFR6†—ÆÖ—F@¢òò’÷G&ò6÷'Fò6W&6FVÂf–æÂâ&öw&W6–öâÕU’w&GVÂÂv2vVæW&÷6÷2à§7FF–26öç7BvVôö'7F6ÆRÅdÃµÒÒ°¢²ScÂô%5õ”4òÂÒÂòò—7FFR'&çVRÆ&v¢²sƒÂô%5õ”4òÂÒÀ¢²Âô%5ô$ÄõTRÂÒÂòò&Æ÷VR6–×ÆR‡6ÇF"÷"Væ6–ÖöVæ6–Ö¢²#CÂô%5õ”4òÂÒÀ¢²CƒÂô%5ô$ÄõTRÂ"ÒÂòòÆFf÷&ÖÇF¢²sCÂô%5ô…TT4òÂ"ÒÂòò‡VV6ò‡6ÇF"÷"Væ6–Ö¢²#Âô%5õ”4òÂÒÀ¢²###Âô%5õõ4„•ÂÒÂòòÒÓâ4„•‡¦öæW76–÷6¢²#CƒÂô%5õ”4õõBÂÒÀ¢²#cÂô%5õ”4òÂÒÀ¢²#ƒ#Âô%5õ”4õõBÂÒÀ¢²3#Âô%5õô5T$òÂÒÂòòÒÓâföÇfW"5T$ð¢²3##Âô%5õ”4òÂÒÀ¢²3C#Âô%5ô$ÄõTRÂÒÀ¢²3c#Âô%5õ”4òÂÒÀ¢²3ƒÂô%5õõ4„•ÂÒÂòòÒÓâ4„•6÷'Fòf–æÀ¢²C#Âô%5õ”4õõBÂÒÀ¢²CCÂô%5õ”4òÂÒÀ¢²C3CÂô%5õô5T$òÂÒÀ¢²CS#Âô%5õ”4òÂÒÀ§Ó° ¢òòä•dTÂ"ÒÒ4ÅUEDU$eTä²ƒ¢ÂVÂÖ2F–f–6–Â“¢÷'FÂFRDÔäò†Ö–æ’öæ÷&ÖÂ’À¢òò–çfW'6–öâFRw&fVFBg&V7VVçFRÂ6†—æv÷7Fò’VâG&ÖòFR&ÆÂâFVç6òà§7FF–26öç7BvVôö'7F6ÆRÅdÃµÒÒ°¢²CƒÂô%5õ”4òÂÒÀ¢²cCÂô%5õ”4òÂÒÀ¢²sƒÂô%5ô$ÄõTRÂÒÀ¢²“#Âô%5õ”4òÂÒÀ¢²cÂô%5õõ%DÅôu$bÂÒÂòò–çfW'F—"ÓâÂFV6†ð¢²#CÂô%5õ”4õõBÂÒÀ¢²3ƒÂô%5õ”4õõBÂÒÀ¢²S#Âô%5õõ%DÅôu$bÂÒÂòòföÇfW"æ÷&ÖÀ¢²ccÂô%5õ”4òÂÒÀ¢²ƒÂô%5õôÔ”ä’ÂÒÂòòÒÓâÔ”ä’††—F&÷‚ÆÖ—FB¢²“#Âô%5õ”4òÂÒÂòòö'7F7VÆ÷2Ö2§VçF÷2VâÖ–æ¢²##Âô%5õ”4òÂÒÀ¢²#3Âô%5ô$ÄõTRÂÒÀ¢²##SÂô%5õ”4òÂÒÀ¢²#3ƒÂô%5õôäõ$ÔÂÂÒÂòòÒÓâFÖæòäõ$ÔÀ¢²#SCÂô%5õõ4„•ÂÒÂòòÒÓâ4„•æv÷7Fð¢²#sCÂô%5õ”4õõBÂÒÀ¢²#ƒCÂô%5õ”4òÂÒÀ¢²3Âô%5õ”4õõBÂÒÀ¢²3Âô%5õ”4òÂÒÀ¢²3#cÂô%5õô$ÄÂÂÒÂòòÒÓâ$ÄÂ‡F–çf–W'FRw&fVFB¢²3CCÂô%5õ”4òÂÒÀ¢²3SƒÂô%5õ”4õõBÂÒÀ¢²3sCÂô%5õ”4òÂÒÀ¢²3“Âô%5õõ%DÅõdTÂÂ"ÒÂòò6VÆW&"‡G&Öòf–æÂ¢²CcÂô%5õô5T$òÂÒÀ¢²C#Âô%5õ”4òÂÒÀ¢²C3CÂô%5ô$ÄõTRÂ"ÒÀ¢²CSÂô%5õ”4òÂÒÀ§Ó° ¢òòä•dTÂ2ÒÒ4âuBÄUBtòƒb¢“¢–çG&öGV6RVÂÖöFòtdR‡¦–w¦rF–vöæÂVà¢òò6÷'&VF÷&W26öâ–6÷2'&–&’&¦ò’â7V&ò’6†—Ö2§W7FF÷2VR7FW&Vòà§7FF–26öç7BvVôö'7F6ÆRÅdÃ%µÒÒ°¢²SÂô%5õ”4òÂÒÀ¢²sÂô%5õ”4òÂÒÀ¢²ƒcÂô%5ô$ÄõTRÂÒÀ¢²CÂô%5õ”4òÂÒÀ¢²#Âô%5õ”4òÂÒÂòò–6÷2Ö26VwV–F÷0¢²3ƒÂô%5õõtdRÂÒÂòòÒÓâtdR†6÷'&VF÷"¢²SCÂô%5õ”4õõBÂÒÀ¢²cCÂô%5õ”4òÂÒÀ¢²scÂô%5õ”4õõBÂÒÀ¢²ƒcÂô%5õ”4òÂÒÀ¢²#Âô%5õô5T$òÂÒÂòòÒÓâ5T$ð¢²#ƒÂô%5õ”4òÂÒÀ¢²#3CÂô%5õõ4„•ÂÒÂòòÒÓâ4„•§W7FFð¢²#SCÂô%5õ”4õõBÂÒÀ¢²#cCÂô%5õ”4òÂÒÀ¢²#ƒ#Âô%5õô5T$òÂÒÂòòÒÓâ5T$ð¢²#“ƒÂô%5õ”4òÂÒÀ¢²3#Âô%5ô$ÄõTRÂ"ÒÀ¢²3#ƒÂô%5õõtdRÂÒÂòòÒÓâtdR†6÷'&VF÷""¢²3CCÂô%5õ”4õõBÂÒÀ¢²3SCÂô%5õ”4òÂÒÀ¢²3ccÂô%5õ”4õõBÂÒÀ¢²3sƒÂô%5õô5T$òÂÒÂòòÒÓâ5T$ð¢²3“cÂô%5õ”4òÂÒÀ¢²CCÂô%5õ”4òÂÒÀ§Ó° ¢òòä•dTÂBÒÒ$Ä5B$ô4U54”ärƒ¢“¢W6Æ2Rf÷&Ö2„7V&òÓåvfRÓå6†—Óä&ÆÂÓà¢òòTdò’ÂVâG&ÖòFRvfRÆ&vò’f–æò†ÆòÖ2F–f–6–Â’’Vâf–æÂG&–6–öæW&ð¢òò6öâ&Æ÷VW2&VÆW2öfÇ6÷2‡f—7VÆÖVçFR–wVÆW2Â6öÆòÆwVæ÷26öÆ–F÷2’à§7FF–26öç7BvVôö'7F6ÆRÅdÃ5µÒÒ°¢²CcÂô%5õ”4òÂÒÀ¢²ccÂô%5ô$ÄõTRÂÒÀ¢²ƒCÂô%5õ”4òÂÒÀ¢²Âô%5õõtdRÂÒÂòòÒÓâtdRÆ&vò‡6V66–öâÖ2F–f–6–Â¢²CÂô%5õ”4õõBÂÒÀ¢²#CÂô%5õ”4òÂÒÀ¢²3CÂô%5õ”4õõBÂÒÀ¢²CCÂô%5õ”4òÂÒÀ¢²SCÂô%5õ”4õõBÂÒÀ¢²cCÂô%5õ”4òÂÒÀ¢²sƒÂô%5õõ4„•ÂÒÂòòÒÓâ4„• ¢²“ƒÂô%5õ”4õõBÂÒÀ¢²#ƒÂô%5õ”4òÂÒÀ¢²##cÂô%5õô$ÄÂÂÒÂòòÒÓâ$ÄÀ¢²#CCÂô%5õ”4òÂÒÀ¢²#SƒÂô%5õ”4õõBÂÒÀ¢²#scÂô%5õõTdòÂÒÂòòÒÓâTdò‡FÒ6ÇFò6÷'Fò¢²#“CÂô%5õ”4õõBÂÒÀ¢²3CÂô%5õ”4òÂÒÀ¢²3##Âô%5õ”4õõBÂÒÀ¢²3CÂô%5õô5T$òÂÒÂòòÒÓâ5T$ò†f–æÂ¢²3ScÂô%5ô$ÄõTRÂÒÂòò&Æ÷VR&VÀ¢²3sÂô%5ô$ÄõTUôbÂÒÂòò&Æ÷VRdÅ4ò†æò6öÆ–Fò¢²3ƒ#Âô%5õ”4òÂÒÀ¢²3“cÂô%5ô$ÄõTUôbÂÒÂòòfÇ6ð¢²CƒÂô%5ô$ÄõTRÂÒÂòò&VÀ¢²C##Âô%5õ”4òÂÒÀ¢²C3cÂô%5ô$ÄõTRÂÒÀ§Ó° ¢6FVf–æRÅdÄâ†’‚‡V–çC…÷B’‡6—¦Vöb†’ò6—¦Vöb†³Ò’’¢6FVf–æRtTõõE$”uôÔ‚C‚òòãÒÖ‚FRö'7F7VÆ÷2FR7VÇV–W"æ—fVÀ ¢òòÖWFFF÷2²ÆWFFR–FVçF–FBFR6Fæ—fVÂ†æÖR÷7F'2÷6¶–â÷6·’öfÆö÷"öæVöâ’à§7G'V7BvVôÆWfVÂ°¢6öç7B6†"¢æÖS²V–çC…÷B7F'3°¢V–çCe÷B6¶–âÂ6·’ÂfÆö÷&2ÂæVöã°¢6öç7BvVôö'7F6ÆR¢ö'3²V–çC…÷Bö'4ã²V–çCe÷BÆVã°§Ó°§7FF–26öç7BvVôÆWfVÂtTõôÄUdTÅ5³EÒÒ°¢²%7FW&VòÖFæW72"ÂÂ&v#ScRƒsÃSÃ#C’Â&v#ScRƒ#bÃSBÃc‚’Â&v#ScRƒÃbÃcB’Â&v#ScRƒ#Ã#Ã#SR’ÂÅdÃÂÅdÄâ„ÅdÃ’ÂCcÒÀ¢²$6ÇWGFW&gVæ²"ÂÂ&v#ScRƒ#3"ÃcbÃ#B’Â&v#ScRƒsbÃCÃc‚’Â&v#ScRƒsÃÃs"’Â&v#ScRƒ#SRÃ3Ã#C’ÂÅdÃÂÅdÄâ„ÅdÃ’ÂCcÒÀ¢²$6âwBÆWBvò"ÂbÂ&v#ScRƒ#BÃ#BÃƒb’Â&v#ScRƒSÃS‚Ã3"’Â&v#ScRƒS‚ÃS‚Ã"’Â&v#ScRƒ#3"Ã#CÃ#’ÂÅdÃ"ÂÅdÄâ„ÅdÃ"’ÂC#CÒÀ¢²$&Æ7B&ö6W76–ær"ÃÂ&v#ScRƒ#CBÃ"ÃS‚’Â&v#ScRƒ#BÃSÃS’Â&v#ScRƒ‚ÃS‚Ãc’Â&v#ScRƒ#Ã#CÃ#3"’ÂÅdÃ2ÂÅdÄâ„ÅdÃ2’ÂCSÒÀ§Ó° ¢òò×VÇF—Æ–6F÷&W2FRÆ÷2÷'FÆW2FRfVÆö6–FB†–æFW†F÷2÷"&Ò§7FF–26öç7BfÆöBtTõõdTÄÕTÅ³EÒÒ²ãsVbÂãbÂã3VbÂãvbÓ° ¢òò2222222222222222222222222222222222222222222222222222222222220¢òò22U5DDòDRÄ%D”D‡FöFò7FF–2Â6–â†V¢òò2222222222222222222222222222222222222222222222222222222222220¦VçVÒ²tTõõÄ’ÒÂtTõôDTBÂtTõõt”âÓ²òòW7FFòFVçG&òFRVæ'F–F¦VçVÒ²u5õ4TÄT5BÒÂu5ôtÔRÓ²òòçFÆÆ7F—fFRÆ §7FF–2–çBu67&VVâÒu5õ4TÄT5C²òò6VÆV7F÷"ÂÓâ§VVvð§7FF–2–çBt7W$ÆWfVÂÒ²òòæ—fVÂVÆVv–FòVâVÂ6''W6VÀ §7FF–2–çBvVõ7FFRÒtTõõÄ“°§7FF–2fÆöBvVõ67&öÆÂÒ²òòFW7Æ¦Ö–VçFòFVÂ×VæFò†6–Æ—§V–W&F§7FF–2fÆöBvVõÆ–W%’Ò²òò’FVÂ&÷&FR7WW&–÷"FVÂ§VvF÷ §7FF–2fÆöBvVõ&Wd&÷BÒ²òò&÷&FR–æfW&–÷"FVÂg&ÖRçFW&–÷"†FW'&—¦¦R§7FF–2fÆöBvVõfVÅ’Ò°§7FF–2fÆöBvVôævÆRÒ²òòv—&ò6÷6ÖWF–6ò†7V&òö&ÆÂ§7FF–2–çBvVôw&dF—"Ò²òò³æ÷&ÖÂÂÓ–çfW'F–F§7FF–2fÆöBvVõ7VVD×VÂÒãc°§7FF–2&ööÂvVôw&÷VæFVBÒG'VS°§7FF–2–çBvVôGFV×G2Ò°§7FF–2–çBtf÷&ÖÒe$Õô5T$ó²òòf÷&Ö7GVÂFVÂ§VvF÷ §7FF–2&ööÂtÖ–æ’ÒfÇ6S²òòFÖæòÖ–æ’7F—fð§7FF–2&ööÂu&7F–6RÒfÇ6S²òòÖöFò&7F–67F—fð§7FF–2&ööÂvVôF÷vå&WbÒfÇ6S²òòW7FFòFRBæF÷vâFVÂF–6²çFW&–÷"†fÆæ6ò§7FF–2&ööÂvVô†VÆDÆF6‚ÒfÇ6S²òò‡V&òFVFò÷–FòFW6FRVÂVÇF–ÖòWFFRf—6–6ð§7FF–2&ööÂvVõFÆF6‚ÒfÇ6S²òò‡V&òVâfÆæ6òFRçVWfòF÷VR†&ÆÂ÷Vfò§7FF–2V–çC3%÷BvVôg&ÖT×2Ò²òòÖ–ÆÆ—2‚’FVÂg&ÖRçFW&–÷"‡&GB§7FF–2V–çC3%÷BvVôFVD×2Ò°§7FF–2&ööÂvVõG&–u´tTõõE$”uôÔ…Ó²òò÷'FÆW2–F—7&F÷2†Wf—F&RÖF—7&ò ¢òòVçFW&÷2öW7FFòFVÂæ—fVÂ7F—fò‡6Rf–¦âÂV×W¦#²6’æ–æwVæf—&ÖFP¢òògVæ6–öâæV6W6—FVÂF—òvVôö'7F6ÆRôvVôÆWfVÂÓâ6ö×–ÆÆ–×–ò’à§7FF–26öç7BvVôö'7F6ÆR¢tö'2Ò°§7FF–2–çBtö'4âÒ°§7FF–2–çBtÆVâÒCS°§7FF–2V–çCe÷Bu6·’ÂtfÆö÷$2ÂtæVöâÂu6¶–ã° ¢òò&öw&W6òW'6—7FVçFR†66†RVâ$Ó²6RÆVRFR&VfW&Væ6W2ÂVçG&"’à§7FF–2–çBvVô&W7E³EÒÒ²ÂÂÂÓ°§7FF–2&ööÂvVôFöæU³EÒÒ²fÇ6RÂfÇ6RÂfÇ6RÂfÇ6RÓ° ¢òò6†V6·ö–çBFRÖöFò&7F–6‡6öÆòVÂVÇF–ÖòÆ6ç¦Fó²äò6RwV&FVâåe2’à§7FF–2&ööÂvVô56WBÒfÇ6S°§7FF–2fÆöBvVô567&öÆÂÒÂvVô5’Ò°§7FF–2V–çC…÷BvVô5f÷&ÒÒe$Õô5T$òÂvVô5Ö–æ’Ò°§7FF–2–çC…÷BvVô5w&bÒ°§7FF–2fÆöBvVô57VVBÒãc°§7FF–2fÆöBvVôæW‡D5Ò²òòVÖ'&ÂFR67&öÆÂ&VÂ&÷†–Öò6†V6·ö–ç@ ¢òòW7FVÆ¢'VffW"6—&7VÆ"FR÷6–6–öæW2‡6RvâÂÆV¦'6R÷"VÂ67&öÆÂ§7G'V7BvVõG&–Â²fÆöB67&öÆÄC²–çCe÷B“²&ööÂW6VC²Ó°§7FF–2vVõG&–ÂvVõG&–Å´tTõõE$”ÅôåÓ°§7FF–2–çBvVõG&–Ä†VBÒ° ¢òò'F–7VÆ2FRÆW‡Æ÷6–öâFR×VW'FP§7G'V7BvVõ'B²fÆöB‚Â’Âg‚Âg’ÂÆ–fS²&ööÂW6VC²Ó°§7FF–2vVõ'BvVõ'E´tTõõ%EôåÓ° ¢òò$är&÷–ò‡†÷'6†–gC3"’ÒÒ6–âFWVæFVæ6–2Â6VÖ–ÆÆFW6FRÖ–ÆÆ—2‚§7FF–2V–çC3%÷BvVõ&æu7FFRÒƒ–S3ss–#—S°§7FF–2–æÆ–æRV–çC3%÷BvVõ&æB‚—°¢V–çC3%÷B‚ÒvVõ&æu7FFS²‚ãÒ‚ÃÂ3²‚ãÒ‚ãâs²‚ãÒ‚ÃÂS°¢&WGW&â†vVõ&æu7FFRÒ‚“°§Ð§7FF–2–æÆ–æRfÆöBvVõ&æFb‚—²&WGW&â†vVõ&æB‚’b„dddb’òcSS3Rãc²Ð ¢òò$"‡&V7FæwVÆò6öçG&&V7FæwVÆò§7FF–2–æÆ–æR&ööÂvVô$"†–çB‚Â–çB’Â–çBrÂ–çB‚Â–çB'‚Â–çB'’Â–çB'rÂ–çB&‚—°¢&WGW&â‚Â'‚²'rbb‚²râ'‚bb’Â'’²&‚bb’²‚â'“°§Ð ¢òòF÷V6‚f—6–6ò‡÷'G&—B’Óâ6ö÷&G2Äôt”42ÆæG66R†–wVÂVR†6RVÂÖöFò2’à§7FF–2–æÆ–æR–çBvVôÅ‚‚—²&WGW&âBç“²ÒòòÆöv–6‚âãs“§7FF–2–æÆ–æR–çBvVôÅ’‚—²&WGW&â…45%õrÒ’ÒBçƒ²ÒòòÆöv–6’âãCs ¢òò2222222222222222222222222222222222222222222222222222222222220¢òò22d5BÔd”ÄÂÆæG66S¢&VÆÆVæVâ&V7BÄôt”4òW67&–&–VæFò'Vç0¢òò224ôåD”uTõ2VâÖVÖ÷&–f—6–6‡Væ„Æ–æRÆöv–66W&–F—7W'6À¢òò22W&òVæ6öÇVÖæÆöv–6W2Væf–Æf—6–66öçF–wV’âW7FòW0¢òò22ÆòVRÖçF–VæRVÂe2W7F&ÆRW6RÆ&÷F6–öã¢VÂföæFòÀ¢òò227VVÆò’…TB6öâVÂw'VW6òFVÂ–çFFò’6ÆVâ66’6÷7Fð¢òò22÷'G&—BâW67&–&RD•$T5DòÂ'VffW"FW7F–æò‡6–ât6Æ—¢VÀ¢òò22§VVvòö7WFöFÆçFÆÆ’Â&W7WFæFòVÂÖ—6ÖòÖVòVP¢òò22WE‡—2ÓâVæ6¦—†VÂ—†VÂ6öâVÂ&W7FòFR&–Ö—F—f2tÆæBà¢òò2222222222222222222222222222222222222222222222222222222222220§7FF–2fö–BvVôf–ÆÄÂ‡V–çCe÷B¢'VbÂ–çBÇ‚Â–çBÇ’Â–çBrÂ–çB‚ÂV–çCe÷B6öÂ—°¢–b†Ç‚Â—²r³ÒÇƒ²Ç‚Ò²Ð¢–b†Ç’Â—²‚³ÒÇ“²Ç’Ò²Ð¢–b†Ç‚²râÅr’rÒÅrÒÇƒ°¢–b†Ç’²‚âÄ‚’‚ÒÄ‚ÒÇ“°¢–b‡rÃÒÇÂ‚ÃÒ’&WGW&ã°¢–çBƒÒ…45%õrÒ’Ò†Ç’²‚Ò“²òò6öÇVÖæf—6–6Ö–æ–ÖFVÂ'Và¢f÷"†–çB’Ò²’Âs²’²²—°¢V–çCe÷B¢Ò'Vb²‡6—¦U÷B’†Ç‚²’’¢45%õr²ƒ°¢f÷"†–çB²Ò²²Âƒ²²²²’¶µÒÒ6öÃ²òò'Vâ†÷&—¦öçFÂf—6–6òÒ6öçF–wVð¢Ð§Ð ¢òò2222222222222222222222222222222222222222222222222222222222220¢òò22$ôu$U4òU%4•5DTåDR…&VfW&Væ6W2ÂæÖW76R&fÆW†÷2"¢òò22Ö—6ÖòÖV6æ—6ÖòVRVÂ&W7FòFVÂ6—7FVÖâ6ÆfW2÷"æ—fVÃ ¢òò22&sö&W7B"ââ&s5ö&W7B"†–çBR’Â&söFöæR"ââ&s5öFöæR"†&ööÂ’à¢òò2222222222222222222222222222222222222222222222222222222222220§7FF–2fö–BvVôÆöE&öw&W72‚—°¢&Vg2æ&Vv–â‚&fÆW†÷2"ÂG'VR“°¢6†"µ³%Ó°¢f÷"†–çB’Ò²’ÂC²’²²—°¢6ç&–çFb†²Â6—¦Vöb†²’Â&rVEö&W7B"Â’“²vVô&W7E¶•ÒÒ&Vg2ævWD–çB†²Â“°¢6ç&–çFb†²Â6—¦Vöb†²’Â&rVEöFöæR"Â’“²vVôFöæU¶•ÒÒ&Vg2ævWD&ööÂ†²ÂfÇ6R“°¢Ð¢&Vg2æVæB‚“°§Ð§7FF–2fö–BvVõ6fU&öw&W72†–çBÇfÂÂ–çB7BÂ&ööÂFöæR—°¢–b‡7BÂ’7BÒ²–b‡7Bâ’7BÒ°¢&ööÂ6†ævVBÒfÇ6S°¢–b‡7BâvVô&W7E¶ÇfÅÒ—²vVô&W7E¶ÇfÅÒÒ7C²6†ævVBÒG'VS²Ð¢–b†FöæRbbvVôFöæU¶ÇfÅÒ—²vVôFöæU¶ÇfÅÒÒG'VS²6†ævVBÒG'VS²Ð¢–b‚6†ævVB’&WGW&ã²òòæòW67&–&—"åe26’æFÖV¦÷&ð¢&Vg2æ&Vv–â‚&fÆW†÷2"ÂfÇ6R“°¢6†"µ³%Ó°¢6ç&–çFb†²Â6—¦Vöb†²’Â&rVEö&W7B"ÂÇfÂ“²&Vg2çWD–çB†²ÂvVô&W7E¶ÇfÅÒ“°¢–b†vVôFöæU¶ÇfÅÒ—²6ç&–çFb†²Â6—¦Vöb†²’Â&rVEöFöæR"ÂÇfÂ“²&Vg2çWD&ööÂ†²ÂG'VR“²Ð¢&Vg2æVæB‚“°§Ð ¢òòf–¦Æ÷2VçFW&÷2÷ÆWFFVÂæ—fVÂ7F—fòFW6FRÆF&Æ‡÷"–æF–6R’à§7FF–2fö–BvVô&–æDÆWfVÂ†–çB–G‚—°¢t7W$ÆWfVÂÒ–Gƒ°¢tö'2ÒtTõôÄUdTÅ5¶–G…Òæö'3°¢tö'4âÒtTõôÄUdTÅ5¶–G…Òæö'4ã°¢tÆVâÒtTõôÄUdTÅ5¶–G…ÒæÆVã°¢u6¶–âÒtTõôÄUdTÅ5¶–G…Òç6¶–ã°¢u6·’ÒtTõôÄUdTÅ5¶–G…Òç6·“°¢tfÆö÷$3ÒtTõôÄUdTÅ5¶–G…ÒæfÆö÷&3°¢tæVöâÒtTõôÄUdTÅ5¶–G…ÒææVöã°§Ð ¢òòÒÒÒÒ&V–æ–6–òFVÂæ—fVÂÂW7FFò–æ–6–Â†æòFö6–çFVçF÷2’ÒÒÒÐ§7FF–2fö–BvVõ&W6WDÆWfVÂ‚—°¢vVõ67&öÆÂÒ²vVõfVÅ’Ò²vVôævÆRÒ°¢vVôw&dF—"Ò²vVõ7VVD×VÂÒãc°¢tf÷&ÖÒe$Õô5T$ó²tÖ–æ’ÒfÇ6S°¢vVõÆ–W%’ÒtTõôdÄôõ%õ’ÒtTõõÃ²vVõ&Wd&÷BÒvVõÆ–W%’²tTõõÃ°¢vVôw&÷VæFVBÒG'VS°¢f÷"†–çB’Ò²’ÂtTõõE$”uôÔƒ²’²²’vVõG&–u¶•ÒÒfÇ6S°¢f÷"†–çB’Ò²’ÂtTõõE$”Åôã²’²²’vVõG&–Å¶•ÒçW6VBÒfÇ6S°¢f÷"†–çB’Ò²’ÂtTõõ%Eôã²’²²’vVõ'E¶•ÒçW6VBÒfÇ6S°¢vVõG&–Ä†VBÒ°¢vVôg&ÖT×2ÒÖ–ÆÆ—2‚“°§Ð ¢òòÒÒÒÒ6Ö&–òFRf÷&Ö‡÷'FÂ“¢6öç6W'f“²Æ2f÷&Ö2FRgVVÆò'&æ6âVâVÂ—&RÒÒÒÐ§7FF–2fö–BvVõ6WDf÷&Ö†–çBb—°¢tf÷&ÖÒc°¢vVôævÆRÒ°¢–b†bÓÒe$Õõ4„•ÇÂbÓÒe$ÕõtdRÇÂbÓÒe$ÕõTdò’vVôw&÷VæFVBÒfÇ6S°§Ð ¢òòÒÒÒÒ6Ö&–òFRFÖæò‡÷'FÂÖ–æ’öæ÷&ÖÂ“¢ÖçF–VæRVÂ&÷&FR–æfW&–÷"ÒÒÒÐ§7FF–2fö–BvVõ6WDÖ–æ’†&ööÂÒ—°¢–b†tÖ–æ’ÓÒÒ’&WGW&ã°¢–çBöÆEÂÒtÖ–æ’ò„tTõõÂò"’¢tTõõÃ°¢tÖ–æ’ÒÓ°¢–çBæWuÂÒtÖ–æ’ò„tTõõÂò"’¢tTõõÃ°¢vVõÆ–W%’³Ò†öÆEÂÒæWuÂ“²òòVÂ&÷&FR–æfW&–÷"æò6R×VWfP¢–b†vVõÆ–W%’ÂtTõô4T”Åõ’’vVõÆ–W%’ÒtTõô4T”Åõ“°§Ð ¢òòÒÒÒÒ6†V6·ö–çG2FR&7F–6¢wV&F"ò&W7FW&"ò&RÖ&Ö"÷'FÆW2ÒÒÒÐ§7FF–2fö–BvVõ6fT5‚—°¢vVô56WBÒG'VS°¢vVô567&öÆÂÒvVõ67&öÆÃ²vVô5’ÒvVõÆ–W%“°¢vVô5f÷&ÒÒ‡V–çC…÷B–tf÷&Ö²vVô5Ö–æ’ÒtÖ–æ’ò¢°¢vVô5w&bÒ†–çC…÷B–vVôw&dF—#²vVô57VVBÒvVõ7VVD×VÃ°§Ð§7FF–2fö–BvVõ&W7F÷&T5‚—°¢vVõ67&öÆÂÒvVô567&öÆÃ²vVõÆ–W%’ÒvVô5“²vVõfVÅ’Ò°¢tf÷&ÖÒvVô5f÷&Ó²tÖ–æ’Ò†vVô5Ö–æ’Ò“°¢vVôw&dF—"ÒvVô5w&c²vVõ7VVD×VÂÒvVô57VVC²vVôw&÷VæFVBÒfÇ6S°¢vVôævÆRÒ°¢òò&RÖ&Ö"÷'FÆW3¢Æ÷2VRVVF&öâåDU2FVÂ6†V6·ö–çBæòFV&Vâ&RÖF—7& ¢òò‡7RVfV7Fò–f–VæR&VfÆV¦FòVâVÂW7FFòwV&FFò“²Æ÷2FRFW7VW2Â6’à¢–çB7Ò†–çB–vVô567&öÆÃ°¢f÷"†–çB’Ò²’Âtö'4ã²’²²—°¢–çB7‚Òtö'5¶•Òç‚Ò7²òò‚VâçFÆÆÂÖöÖVçFòFVÂ6†V6·ö–ç@¢vVõG&–u¶•ÒÒ‡7‚ÂtTõõÅ‚²b“²òò–G&fW6FòÓâÖ&6Fò6öÖòF—7&Fð¢Ð¢f÷"†–çB’Ò²’ÂtTõõE$”Åôã²’²²’vVõG&–Å¶•ÒçW6VBÒfÇ6S°¢vVõG&–Ä†VBÒ°§Ð ¢òòÒÒÒÒW‡Æ÷6–öâFR'F–7VÆ2VâÆ÷6–6–öâFVÂ§VvF÷"ÒÒÒÐ§7FF–2fö–BvVõ7vå'F–6ÆW2‚—°¢–çBuÂÒtÖ–æ’ò„tTõõÂò"’¢tTõõÃ°¢fÆöB7‚ÒtTõõÅ‚²uÂò"ãbÂ7’ÒvVõÆ–W%’²uÂò"ãc°¢f÷"†–çB’Ò²’ÂtTõõ%Eôã²’²²—°¢fÆöBærÒvVõ&æFb‚’¢bã#ƒ3ƒS6c°¢fÆöB7Ò“ãb²vVõ&æFb‚’¢#cãc°¢vVõ'E¶•Òç‚Ò7ƒ²vVõ'E¶•Òç’Ò7“°¢vVõ'E¶•Òçg‚Ò6÷6b†ær’¢7°¢vVõ'E¶•Òçg’Ò6–æb†ær’¢7Òsãc°¢vVõ'E¶•ÒæÆ–fRÒãCVb²vVõ&æFb‚’¢ã3Vc°¢vVõ'E¶•ÒçW6VBÒG'VS°¢Ð§Ð§7FF–2fö–BvVôF–R‚—°¢–b†vVõ7FFRÒtTõõÄ’’&WGW&ã°¢vVõ7FFRÒtTõôDTC²vVôFVD×2ÒÖ–ÆÆ—2‚“°¢vVõfVÅ’Ò°¢vVõ7vå'F–6ÆW2‚“°¢òòVÂRwV&FFòFVÂæ÷&ÖÂÖöFR6R7GVÆ—¦ÂÖ÷&—"†çVæ6&¦’à¢–b‚u&7F–6R—°¢–çB7BÒ†–çB’†vVõ67&öÆÂò†fÆöB–tÆVâ¢ãb“°¢vVõ6fU&öw&W72†t7W$ÆWfVÂÂ7BÂfÇ6R“°¢Ð§Ð§7FF–2fö–BvVõWFFU'F–6ÆW2†fÆöBGB—°¢f÷"†–çB’Ò²’ÂtTõõ%Eôã²’²²—°¢–b‚vVõ'E¶•ÒçW6VB’6öçF–çVS°¢vVõ'E¶•Òç‚³ÒvVõ'E¶•Òçg‚¢GC°¢vVõ'E¶•Òç’³ÒvVõ'E¶•Òçg’¢GC°¢vVõ'E¶•Òçg’³Ò“ãb¢GC°¢vVõ'E¶•ÒæÆ–fRÓÒGC°¢–b†vVõ'E¶•ÒæÆ–fRÃÒ’vVõ'E¶•ÒçW6VBÒfÇ6S°¢Ð§Ð ¢òò2222222222222222222222222222222222222222222222222222222222220¢òò22d•4”4²4ôÄ•4”ôâ²õ%DÄU2²ÕTU%DR‡Vâ7V'7FW¢òò22†VÆC¢FVFò÷–FòVâÆ¦öæFR§VVvò†ÆF6†VFòVâvVõF–6²’à¢òò22Æ÷2VfV7F÷2FR'Vâ6öÆòF"†&ÆÂ÷Vfò’6RÆ–6âeTU$ÂVà¢òò22vVõF–6²Â&æò&WWF—'6RVâ6F7V'7FWà¢òò2222222222222222222222222222222222222222222222222222222222220§7FF–2fö–BvVõWFFR†fÆöBGBÂ&ööÂ†VÆB—°¢–çBuÂÒtÖ–æ’ò„tTõõÂò"’¢tTõõÃ° ¢òòÒÒÒ–×VÇ6ò6öçF–çVòFVÂ7V&ò‡6ÇFÂÖçFVæW"6’W7FVâVÂ—6ò’ÒÒÐ¢–b†tf÷&ÖÓÒe$Õô5T$òbbvVôw&÷VæFVBbb†VÆB—°¢vVõfVÅ’ÒÖvVôw&dF—"¢tTõô¥TÕ°¢vVôw&÷VæFVBÒfÇ6S°¢Ð¢&ööÂ&Wdw&÷VæFVBÒvVôw&÷VæFVC° ¢òòÒÒÒ67&öÆÂFVÂ×VæFò²W7FVÆÒÒÐ¢vVõ67&öÆÂ³ÒtTõõ5TTB¢vVõ7VVD×VÂ¢GC°¢vVõG&–Å¶vVõG&–Ä†VEÒç67&öÆÄBÒvVõ67&öÆÃ°¢vVõG&–Å¶vVõG&–Ä†VEÒç’Ò†–çCe÷B’†vVõÆ–W%’²uÂò"“°¢vVõG&–Å¶vVõG&–Ä†VEÒçW6VBÒG'VS°¢vVõG&–Ä†VBÒ†vVõG&–Ä†VB²’RtTõõE$”Åôã° ¢òòÒÒÒ–çFVw&6–öâfW'F–6Â†FWVæFRFRÆf÷&Ö’ÒÒÐ¢vVõ&Wd&÷BÒvVõÆ–W%’²uÃ°¢–b†tf÷&ÖÓÒe$Õô5T$òÇÂtf÷&ÖÓÒe$Õô$ÄÂ—°¢vVõfVÅ’³ÒvVôw&dF—"¢tTõôu$b¢GC°¢–b†vVõfVÅ’âtTõôdÄÄ4’vVõfVÅ’ÒtTõôdÄÄ4°¢–b†vVõfVÅ’ÂÔtTõôdÄÄ4’vVõfVÅ’ÒÔtTõôdÄÄ4°¢ÒVÇ6R–b†tf÷&ÖÓÒe$Õõ4„•—°¢vVõfVÅ’³ÒvVôw&dF—"¢tTõõ4„•ôu$b¢GC°¢–b††VÆB’vVõfVÅ’ÓÒvVôw&dF—"¢tTõõ4„•ô42¢GC²òòV×V¦R6÷7FVæ–Fð¢–b†vVõfVÅ’âtTõõ4„•õdÔ‚’vVõfVÅ’ÒtTõõ4„•õdÔƒ°¢–b†vVõfVÅ’ÂÔtTõõ4„•õdÔ‚’vVõfVÅ’ÒÔtTõõ4„•õdÔƒ°¢ÒVÇ6R–b†tf÷&ÖÓÒe$ÕõTdò—°¢vVõfVÅ’³ÒvVôw&dF—"¢tTõõTdõôu$b¢GC°¢–b†vVõfVÅ’âtTõõTdõõdÔ‚’vVõfVÅ’ÒtTõõTdõõdÔƒ°¢–b†vVõfVÅ’ÂÔtTõõTdõõdÔ‚’vVõfVÅ’ÒÔtTõõTdõõdÔƒ°¢ÒVÇ6R²òòe$ÕõtdS¢F–vöæÂ–ç7FçFæVÂ6–â–æW&6–ƒCRw&F÷26öâVÂ67&öÆÂ¢fÆöBwbÒtTõõ5TTB¢vVõ7VVD×VÃ°¢vVõfVÅ’Ò†VÆBò×wb¢wc°¢Ð¢vVõÆ–W%’³ÒvVõfVÅ’¢GC° ¢–çB67&öÆÂÒ†–çB–vVõ67&öÆÃ°¢–çBÂÒtTõõÅ‚Â"ÒtTõõÅ‚²uÃ° ¢òòÒÒÒF—7&F÷&W2FR÷'FÂ†6öÆ—6–öâFR&G&fW6""ÂVæ6öÆfW¢’ÒÒÐ¢f÷"†–çB’Ò²’Âtö'4ã²’²²—°¢V–çC…÷BGÒtö'5¶•ÒçF—ó°¢–b‡GÂô%5õõ%DÅôu$bÇÂGâô%5õôäõ$ÔÂ’6öçF–çVS²òòæòW2÷'FÀ¢–b†vVõG&–u¶•Ò’6öçF–çVS°¢–çB7‚Òtö'5¶•Òç‚Ò67&öÆÃ°¢–çB'‚Ò7‚²tTõõE2ò"ÒS°¢–b†vVô$"‡ÂÂ†–çB–vVõÆ–W%’ÂuÂÂuÂÂ'‚ÂtTõô4T”Åõ’Â3ÂtTõôdÄôõ%õ’ÒtTõô4T”Åõ’’—°¢vVõG&–u¶•ÒÒG'VS°¢7v—F6‚‡G—°¢66Rô%5õõ%DÅôu$c¢vVôw&dF—"ÒÖvVôw&dF—#²'&V³°¢66Rô%5õõ%DÅõdTÃ¢vVõ7VVD×VÂÒtTõõdTÄÕTÅ¶tö'5¶•Òç&Òb5Ó²'&V³°¢66Rô%5õô5T$ó¢vVõ6WDf÷&Ö„e$Õô5T$ò“²'&V³°¢66Rô%5õõ4„•¢vVõ6WDf÷&Ö„e$Õõ4„•“²'&V³°¢66Rô%5õô$ÄÃ¢vVõ6WDf÷&Ö„e$Õô$ÄÂ“²'&V³°¢66Rô%5õõtdS¢vVõ6WDf÷&Ö„e$ÕõtdR“²'&V³°¢66Rô%5õõTdó¢vVõ6WDf÷&Ö„e$ÕõTdò“²'&V³°¢66Rô%5õôÔ”ä“¢vVõ6WDÖ–æ’‡G'VR“²'&V³°¢66Rô%5õôäõ$ÔÃ¢vVõ6WDÖ–æ’†fÇ6R“²'&V³°¢Ð¢Ð¢Ð ¢òòÒÒÒ&W6öÇV6–öâfW'F–6Â6VwVâf÷&ÖÒÒÐ¢vVôw&÷VæFVBÒfÇ6S°¢–b†tf÷&ÖÓÒe$Õô5T$ò—°¢–b†vVõfVÅ’ãÒ—²òò6–VæFó¢7VVÆòòFV6†òFR&Æ÷VP¢&ööÂ÷fW$vÒfÇ6S°¢f÷"†–çB’Ò²’Âtö'4ã²’²²—°¢–b†tö'5¶•ÒçF—òÒô%5ô…TT4ò’6öçF–çVS°¢–çBwƒÒtö'5¶•Òç‚Ò67&öÆÂÂwƒÒwƒ²tö'5¶•Òç&Ò¢tTõõE3°¢–b‡"âwƒbbÂÂwƒ—²÷fW$vÒG'VS²'&V³²Ð¢Ð¢fÆöBF÷Ò÷fW$vòãb¢†fÆöB”tTõôdÄôõ%õ“°¢f÷"†–çB’Ò²’Âtö'4ã²’²²—°¢–b†tö'5¶•ÒçF—òÒô%5ô$ÄõTR’6öçF–çVS°¢–çB'‚Òtö'5¶•Òç‚Ò67&öÆÂÂ'’ÒtTõôdÄôõ%õ’Òtö'5¶•Òç&Ò¢tTõõE3°¢–b‡"â'‚bbÂÂ'‚²tTõõE2bbvVõ&Wd&÷BÃÒ'’²tTõôÄäEDôÂbb'’ÂF÷’F÷Ò'“°¢Ð¢–b†vVõÆ–W%’²uÂãÒF÷—²vVõÆ–W%’ÒF÷ÒuÃ²vVõfVÅ’Ò²vVôw&÷VæFVBÒG'VS²Ð¢Ð¢–b†vVõfVÅ’ÃÒ—²òò7V&–VæFó¢FV6†ò†w&b–çfW'F–F¢–b†vVõÆ–W%’ÃÒtTõô4T”Åõ’—²vVõÆ–W%’ÒtTõô4T”Åõ“²vVõfVÅ’Ò²vVôw&÷VæFVBÒG'VS²Ð¢Ð¢ÒVÇ6R–b†tf÷&ÖÓÒe$Õô$ÄÂ—²òò6RVv—6òòFV6†ð¢–b†vVõÆ–W%’²uÂãÒtTõôdÄôõ%õ’—²vVõÆ–W%’ÒtTõôdÄôõ%õ’ÒuÃ²–b†vVõfVÅ’â’vVõfVÅ’Ò²vVôw&÷VæFVBÒG'VS²Ð¢–b†vVõÆ–W%’ÃÒtTõô4T”Åõ’—²vVõÆ–W%’ÒtTõô4T”Åõ“²–b†vVõfVÅ’Â’vVõfVÅ’Ò²vVôw&÷VæFVBÒG'VS²Ð¢ÒVÇ6R²òò6†—÷vfR÷Vfó¢FW6Æ—¦âVâÆ÷2&÷&FW0¢–b†vVõÆ–W%’ÂtTõô4T”Åõ’—²vVõÆ–W%’ÒtTõô4T”Åõ“²–b†vVõfVÅ’Â’vVõfVÅ’Ò²Ð¢–b†vVõÆ–W%’âtTõôdÄôõ%õ’ÒuÂ—²vVõÆ–W%’ÒtTõôdÄôõ%õ’ÒuÃ²–b†vVõfVÅ’â’vVõfVÅ’Ò²Ð¢Ð ¢òòÒÒÒv—&ò6÷6ÖWF–6òÒÒÐ¢–b†tf÷&ÖÓÒe$Õô5T$ò—°¢–b†vVôw&÷VæFVBbb&Wdw&÷VæFVB—²fÆöBÒãSss“c6c²vVôævÆRÒ¢&÷VæFb†vVôævÆRò“²Ð¢–b‚vVôw&÷VæFVB’vVôævÆR³ÒtTõõ$õB¢GC°¢ÒVÇ6R–b†tf÷&ÖÓÒe$Õô$ÄÂ—°¢vVôævÆR³Ò†vVôw&dF—"âòtTõõ$õB¢ÔtTõõ$õB’¢GB¢ãVc²òò'VVF¢ÒVÇ6R°¢vVôævÆRÒ°¢Ð¢v†–ÆR†vVôævÆRãÒbã#ƒ3ƒS6b’vVôævÆRÓÒbã#ƒ3ƒS6c°¢v†–ÆR†vVôævÆRÂ’vVôævÆR³Òbã#ƒ3ƒS6c° ¢òòÒÒÒ×VW'FS¢–6÷2‡—6ò÷FV6†ò’Â–çFW&–÷"FR&Æ÷VR4ôÄ”DòÂ6W"Vâ‡VV6òÒÒÐ¢–çB‡‚ÒtÖ–æ’òR¢‚Â‡’ÒtÖ–æ’ò"¢#"Â‡rÒtÖ–æ’ò"¢ƒ²òò†—F&÷‚FR–6ò6VwVâFÖæð¢f÷"†–çB’Ò²’Âtö'4ã²’²²—°¢–çB7‚Òtö'5¶•Òç‚Ò67&öÆÃ°¢V–çC…÷BGÒtö'5¶•ÒçF—ó°¢–b‡GÓÒô%5õ”4ò—°¢–b†vVô$"‡ÂÂ†–çB–vVõÆ–W%’ÂuÂÂuÂÂ7‚²‡‚ÂtTõôdÄôõ%õ’Ò‡’Ò"Â‡rÂ‡’’—²vVôF–R‚“²&WGW&ã²Ð¢ÒVÇ6R–b‡GÓÒô%5õ”4õõB—°¢–b†vVô$"‡ÂÂ†–çB–vVõÆ–W%’ÂuÂÂuÂÂ7‚²‡‚ÂtTõô4T”Åõ’²"Â‡rÂ‡’’—²vVôF–R‚“²&WGW&ã²Ð¢ÒVÇ6R–b‡GÓÒô%5ô$ÄõTR—°¢–çB'’ÒtTõôdÄôõ%õ’Òtö'5¶•Òç&Ò¢tTõõE3°¢–b‡"â7‚²"bbÂÂ7‚²tTõõE2Ò"bbvVõÆ–W%’²uÂâ'’²bbbvVõÆ–W%’ÂtTõôdÄôõ%õ’—²vVôF–R‚“²&WGW&ã²Ð¢Ð¢òòô%5ô$ÄõTUôc¢&Æ÷VRdÅ4òÓâ6RF–'V¦–wVÂW&òäò6öÆ—6–öææ’6÷7F–VæRà¢Ð¢–b‚†tf÷&ÖÓÒe$Õô5T$òÇÂtf÷&ÖÓÒe$Õô$ÄÂ’bbvVõÆ–W%’âtTõôdÄôõ%õ’²3—²vVôF–R‚“²&WGW&ã²Ð ¢òòÒÒÒf–âFVÂæ—fVÂÒÒÐ¢–b†vVõ67&öÆÂãÒtÆVâ—°¢vVõ7FFRÒtTõõt”ã°¢–b‚u&7F–6R’vVõ6fU&öw&W72†t7W$ÆWfVÂÂÂG'VR“°¢Ð§Ð ¢òò2222222222222222222222222222222222222222222222222222222222220¢òò22D”%T¤òDTÂ¥TTtò†6ö÷&G2Äôt”42ƒƒCƒ²W67&–&RVâ&'Vb¢òò22Æ÷2w&æFW2&VÆÆVæ÷2W6âvVôf–ÆÄÂ‡'Vç26öçF–wV÷2’&æð¢òò22W&FW"e2÷"Æ&÷F6–öââö'7F7VÆ÷2’§VvF÷"W6âÆ0¢òò22&–Ö—F—f2tÆæBæ÷&ÖÆW2‡G&–æwVÆ÷2÷VG2ö6—&7VÆ÷2÷FW‡Fò’à¢òò2222222222222222222222222222222222222222222222222222222222220 ¢òòÒÒÒÒföæFó¢6–VÆò²&ÆÆ‚²FV6†ò6öÆ–Fò6’Æw&fVFBW7F–çfW'F–FÒÒÒÐ§7FF–2fö–BvVôG&t&6¶w&÷VæB‚—°¢vVôf–ÆÄÂ†&'VbÂÂtTõô…TEô‚ÂÅrÂtTõôdÄôõ%õ’ÒtTõô…TEô‚Âu6·’“°¢òò&ÆÆƒ¢æVÆW2fW'F–6ÆW2VRFW&—fâã3‚FVÂ67&öÆÂ‡&ögVæF–FB’à¢V–çCe÷BæRÒÖ—ƒScR†u6·’ÂtTõô”ä²Âc“°¢–çBöfbÒ†–çB’†vVõ67&öÆÂ¢ã3b’R3²–b†öfbÂ’öfb³Ò3°¢f÷"†–çB‚ÒÖöfc²‚ÂÅs²‚³Ò3—°¢vVôf–ÆÄÂ†&'VbÂ‚²BÂtTõô…TEô‚²#BÂƒBÂtTõôdÄôõ%õ’ÒtTõô…TEô‚ÒcÂæR“°¢Ð¢–b†vVôw&dF—"Â—²òò7WW&f–6–RFRFV6†ð¢vVôf–ÆÄÂ†&'VbÂÂtTõô…TEô‚ÂÅrÂtTõô4T”Åõ’ÒtTõô…TEô‚ÂtfÆö÷$2“°¢„Æ–æRƒÂtTõô4T”Åõ’ÂÅrÂtæVöâ“°¢„Æ–æRƒÂtTõô4T”Åõ’²ÂÅrÂÖ—ƒScR†tæVöâÂtTõô”ä²Â“’“°¢Ð§Ð ¢òòÒÒÒÒ7VVÆò6öâFW‡GW&FRfVÆö6–FB²‡VV6÷2ÒÒÒÐ§7FF–2fö–BvVôG&tfÆö÷"‚—°¢vVôf–ÆÄÂ†&'VbÂÂtTõôdÄôõ%õ’ÂÅrÂÄ‚ÒtTõôdÄôõ%õ’ÂtfÆö÷$2“°¢V–çCe÷Bfö–F2ÒÖ—ƒScR†tfÆö÷$2Â&v#ScRƒÃÃ’Â#“°¢–çBöfbÒ†–çB–vVõ67&öÆÂRtTõõE3²–b†öfbÂ’öfb³ÒtTõõE3°¢f÷"†–çB‚ÒÖöfc²‚ÂÅs²‚³ÒtTõõE2’dÆ–æR‡‚ÂtTõôdÄôõ%õ’ÂÄ‚ÒtTõôdÄôõ%õ’Âfö–F2“°¢„Æ–æRƒÂtTõôdÄôõ%õ’ÂÅrÂtæVöâ“°¢„Æ–æRƒÂtTõôdÄôõ%õ’ÒÂÅrÂÖ—ƒScR†tæVöâÂtTõô”ä²Â“’“°¢òò‡VV6÷3¢&V6÷'F"VÂ7VVÆò‡÷¦ò’’&VÖ&6"&÷&FW2à¢–çB67&öÆÂÒ†–çB–vVõ67&öÆÃ°¢f÷"†–çB’Ò²’Âtö'4ã²’²²—°¢–b†tö'5¶•ÒçF—òÒô%5ô…TT4ò’6öçF–çVS°¢–çBwƒÒtö'5¶•Òç‚Ò67&öÆÂÂrÒtö'5¶•Òç&Ò¢tTõõE3°¢–b†wƒâÅrÇÂwƒ²rÂ’6öçF–çVS°¢vVôf–ÆÄÂ†&'VbÂwƒÂtTõôdÄôõ%õ’ÒÂrÂÄ‚ÒtTõôdÄôõ%õ’²Âfö–F2“°¢dÆ–æR†wƒÂtTõôdÄôõ%õ’Â3bÂtæVöâ“°¢dÆ–æR†wƒ²rÂtTõôdÄôõ%õ’Â3bÂtæVöâ“°¢Ð§Ð ¢òòÒÒÒÒVâ&Æ÷VR‡&VÂòfÇ6ó¢6RF–'V¦â”uTÂ’ÒÒÒÐ§7FF–2fö–BvVôG&t&Æö6²†–çB7‚Â–çBçF–ÆW2—°¢–çB&‚ÒçF–ÆW2¢tTõõE2Â'’ÒtTõôdÄôõ%õ’Ò&ƒ°¢f–ÆÅ&V7B‡7‚Â'’ÂtTõõE2Â&‚ÂtTõô”ä²“°¢G&u&V7B‡7‚Â'’ÂtTõõE2Â&‚ÂÖ—ƒScR†tæVöâÂtTõô”ä²Â“’“°¢„Æ–æR‡7‚Â'’ÂtTõõE2ÂtæVöâ“°¢f÷"†–çB²Ò²²ÂçF–ÆW3²²²²’„Æ–æR‡7‚Â'’²²¢tTõõE2ÂtTõõE2ÂÖ—ƒScR†tæVöâÂtTõô”ä²Â“’“°¢dÆ–æR‡7‚²tTõõE2ò"Â'’Â&‚ÂÖ—ƒScR†tæVöâÂtTõô”ä²Â“’“°§Ð ¢òòÒÒÒÒö'7F7VÆ÷2f—6–&ÆW2‡–6÷2Â&Æ÷VW2Â÷'FÆW2’ÒÒÒÐ§7FF–2fö–BvVôG&tö'7F6ÆW2‚—°¢–çB67&öÆÂÒ†–çB–vVõ67&öÆÃ°¢f÷"†–çB’Ò²’Âtö'4ã²’²²—°¢–çB7‚Òtö'5¶•Òç‚Ò67&öÆÃ°¢–b‡7‚ÂÔtTõõE2¢2ÇÂ7‚âÅr²tTõõE2’6öçF–çVS²òò&V6÷'FP¢V–çC…÷BGÒtö'5¶•ÒçF—ó°¢–b‡GÓÒô%5õ”4ò—°¢f–ÆÅG&–ævÆR‡7‚ÂtTõôdÄôõ%õ’Â7‚²tTõõE2ÂtTõôdÄôõ%õ’Â7‚²tTõõE2ò"ÂtTõôdÄôõ%õ’ÒtTõõE2ÂtæVöâ“°¢f–ÆÅG&–ævÆR‡7‚²2ÂtTõôdÄôõ%õ’ÒÂ7‚²tTõõE2Ò2ÂtTõôdÄôõ%õ’ÒÂ7‚²tTõõE2ò"ÂtTõôdÄôõ%õ’ÒtTõõE2²bÂtTõô”ä²“°¢ÒVÇ6R–b‡GÓÒô%5õ”4õõB—°¢f–ÆÅG&–ævÆR‡7‚ÂtTõô4T”Åõ’Â7‚²tTõõE2ÂtTõô4T”Åõ’Â7‚²tTõõE2ò"ÂtTõô4T”Åõ’²tTõõE2ÂtæVöâ“°¢f–ÆÅG&–ævÆR‡7‚²2ÂtTõô4T”Åõ’²Â7‚²tTõõE2Ò2ÂtTõô4T”Åõ’²Â7‚²tTõõE2ò"ÂtTõô4T”Åõ’²tTõõE2ÒbÂtTõô”ä²“°¢ÒVÇ6R–b‡GÓÒô%5ô$ÄõTRÇÂGÓÒô%5ô$ÄõTUôb—°¢vVôG&t&Æö6²‡7‚Âtö'5¶•Òç&Ò“²òò&VÂ’fÇ6ó¢–FVçF–6÷0¢ÒVÇ6R–b‡GÓÒô%5õõ%DÅôu$bÇÂGÓÒô%5õõ%DÅõdTÂÇÀ¢‡GãÒô%5õô5T$òbbGÃÒô%5õôäõ$ÔÂ’—°¢V–çCe÷B6öÃ°¢–b‡GÓÒô%5õõ%DÅôu$b’6öÂÒtTõôÔ$U#°¢VÇ6R–b‡GÓÒô%5õõ%DÅõdTÂ’6öÂÒtTõô5”ã°¢VÇ6R–b‡GÓÒô%5õôÔ”ä’ÇÂGÓÒô%5õôäõ$ÔÂ’6öÂÒtTõôÄ”ÔS°¢VÇ6R6öÂÒtTõõU%²òò÷'FÆW2FRf÷&Ö¢–çB…òÒ7‚²tTõõE2ò#°¢f–ÆÅ&V7D‡…òÒRÂtTõô4T”Åõ’Â3ÂtTõôdÄôõ%õ’ÒtTõô4T”Åõ’Â6öÂÂs“°¢G&u&V7B‡…òÒRÂtTõô4T”Åõ’Â3ÂtTõôdÄôõ%õ’ÒtTõô4T”Åõ’Â6öÂ“°¢G&u&V7B‡…òÒBÂtTõô4T”Åõ’²Â#‚ÂtTõôdÄôõ%õ’ÒtTõô4T”Åõ’Ò"Â6öÂ“°¢–çB7’Ò„tTõô4T”Åõ’²tTõôdÄôõ%õ’’ò#°¢–b‡GÓÒô%5õõ%DÅôu$b—²òòfÆV6†2'&–&ö&¦ð¢f–ÆÅG&–ævÆR‡…òÂ7’Ò#bÂ…òÒ’Â7’Ò"Â…ò²’Â7’Ò"Â6öÂ“°¢f–ÆÅG&–ævÆR‡…òÂ7’²#bÂ…òÒ’Â7’²"Â…ò²’Â7’²"Â6öÂ“°¢ÒVÇ6R–b‡GÓÒô%5õõ%DÅõdTÂ—²òò6†Wg&öæW2‡fVÆö6–FB¢f–ÆÅG&–ævÆR‡…òÒÂ7’ÒBÂ…òÒÂ7’²BÂ…ò²BÂ7’Â6öÂ“°¢f–ÆÅG&–ævÆR‡…ò²"Â7’ÒBÂ…ò²"Â7’²BÂ…ò²bÂ7’Â6öÂ“°¢ÒVÇ6R–b‡GÓÒô%5õôÔ”ä’ÇÂGÓÒô%5õôäõ$ÔÂ—²òò6–Ö&öÆòFRFÖæð¢–çB2Ò‡GÓÒô%5õôÔ”ä’’òb¢#°¢G&u&V7B‡…òÒ2Â7’Ò2Â"¢2Â"¢2Â6öÂ“°¢f–ÆÅ&V7B‡…òÒ2²"Â7’Ò2²"Â"¢2ÒBÂ"¢2ÒBÂÖ—ƒScR†6öÂÂtTõô”ä²Â#’“°¢ÒVÇ6R²òòÆWG&FRÆf÷&Ö¢6öç7B6†"¢rÒ$2#°¢–b‡GÓÒô%5õõ4„•’rÒ%2#²VÇ6R–b‡GÓÒô%5õô$ÄÂ’rÒ$"#°¢VÇ6R–b‡GÓÒô%5õõtdR’rÒ%r#²VÇ6R–b‡GÓÒô%5õõTdò’rÒ%R#°¢G&uFW‡D2‡…òÂ7’Ò‚ÂrÂ"Â6öÂ“°¢Ð¢Ð¢Ð§Ð ¢òòÒÒÒÒW7FVÆ†7VG&F—F÷2VR6RFW7fæV6Vâ†6–G&2’ÒÒÒÐ§7FF–2fö–BvVôG&uG&–Â‚—°¢–çBuÂÒtÖ–æ’ò„tTõõÂò"’¢tTõõÃ°¢f÷"†–çB’Ò²’ÂtTõõE$”Åôã²’²²—°¢–b‚vVõG&–Å¶•ÒçW6VB’6öçF–çVS°¢fÆöBF—7BÒvVõ67&öÆÂÒvVõG&–Å¶•Òç67&öÆÄC°¢–b†F—7BãÒtTõõE$”ÅôdDR—²vVõG&–Å¶•ÒçW6VBÒfÇ6S²6öçF–çVS²Ð¢–çB7‚ÒtTõõÅ‚²uÂò"Ò†–çB–F—7C°¢–b‡7‚ÂÓ—²vVõG&–Å¶•ÒçW6VBÒfÇ6S²6öçF–çVS²Ð¢fÆöBbÒãbÒF—7BòtTõõE$”ÅôdDS°¢–çB7¢Ò†–çB’ƒ2²b¢“°¢V–çC…÷BÒ‡V–çC…÷B’†b¢S“°¢f–ÆÅ&V7D‡7‚Ò7¢ò"ÂvVõG&–Å¶•Òç’Ò7¢ò"Â7¢Â7¢Âu6¶–âÂ“°¢Ð§Ð ¢òòÒÒÒÒ7VG&Fò&÷FFò÷"7W2BW7V–æ2†7V&ò’ÒÒÒÐ§7FF–2fö–BvVõVE&÷B†–çB7‚Â–çB7’ÂfÆöB‚ÂfÆöB2ÂfÆöB2ÂV–çCe÷B6öÂ—°¢–çBƒÒ7‚²†–çB’‚Ö‚¢2²‚¢2’Â“Ò7’²†–çB’‚Ö‚¢2Ò‚¢2“°¢–çBƒÒ7‚²†–çB’‚‚¢2²‚¢2’Â“Ò7’²†–çB’‚‚¢2Ò‚¢2“°¢–çBƒ"Ò7‚²†–çB’‚‚¢2Ò‚¢2’Â“"Ò7’²†–çB’‚‚¢2²‚¢2“°¢–çBƒ2Ò7‚²†–çB’‚Ö‚¢2Ò‚¢2’Â“2Ò7’²†–çB’‚Ö‚¢2²‚¢2“°¢f–ÆÅVB‡ƒÂ“ÂƒÂ“Âƒ"Â“"Âƒ2Â“2Â6öÂ“°§Ð ¢òòÒÒÒÒ§VvF÷#¢F–'V¦ò÷"f÷&Ö†7‚Æ7’Ò6VçG&ó²"Ò&F–ò6VwVâFÖæò’ÒÒÒÐ§7FF–2fö–BvVôG&uÆ–W"†–çB7‚Â–çB7’Â–çB"—°¢V–çCe÷B†’Òu6¶–âÂF²ÒÖ—ƒScR†u6¶–âÂtTõô”ä²ÂS’ÂW–RÒ&v#ScRƒ#CÃ#CbÃ#SR“°¢–b†tf÷&ÖÓÒe$Õô5T$ò—°¢fÆöB2Ò6–æb†vVôævÆR’Â2Ò6÷6b†vVôævÆR“°¢vVõVE&÷B†7‚Â7’Â"Â2Â2Â†’“°¢vVõVE&÷B†7‚Â7’Â"¢ãc&bÂ2Â2ÂF²“°¢vVõVE&÷B†7‚Â7’Â"¢ã#†bÂ2Â2ÂW–R“°¢ÒVÇ6R–b†tf÷&ÖÓÒe$Õô$ÄÂ—°¢f–ÆÄ6—&6ÆR†7‚Â7’Â"Â†’“°¢f–ÆÄ6—&6ÆR†7‚Â7’Â†–çB’‡"¢ãcb’ÂF²“°¢òòÖ&6VRv—&‡6Vç66–öâFR&öF"¢–çB×‚Ò7‚²†–çB’†6÷6b†vVôævÆR’¢"¢ãSVb“°¢–çB×’Ò7’²†–çB’‡6–æb†vVôævÆR’¢"¢ãSVb“°¢f–ÆÄ6—&6ÆR†×‚Â×’Â"â"òB¢"ÂW–R“°¢G&t6—&6ÆR†7‚Â7’Â"ÂÖ—ƒScR††’Â&v#ScRƒ#SRÃ#SRÃ#SR’Â“’“°¢ÒVÇ6R–b†tf÷&ÖÓÒe$Õõ4„•—°¢òò–æ6Æ–æ6–öâ6VwVâfVÆö6–FBfW'F–6À¢fÆöBF–ÇBÒvVõfVÅ’òtTõõ4„•õdÔƒ²–b‡F–ÇBâ’F–ÇBÒ²–b‡F–ÇBÂÓ’F–ÇBÒÓ°¢–çBG’Ò†–çB’‡F–ÇB¢"¢ãVb“°¢f–ÆÅG&–ævÆR†7‚Ò"Â7’Ò"²G’Â7‚Ò"Â7’²"ÒG’Â7‚²"²BÂ7’Â†’“²òò666ð¢f–ÆÅG&–ævÆR†7‚Ò"²2Â7’Ò"²2²G’Â7‚Ò"²2Â7’²"Ò2ÒG’Â7‚²"Â7’ÂF²“°¢f–ÆÄ6—&6ÆR†7‚Ò"Â7’Ò"²bÂ"â"òb¢2ÂW–R“²òò6&–æ¢f–ÆÄ6—&6ÆR†7‚Ò"Â7’Ò"²bÂ"â"ò2¢"Âu6¶–â“°¢ÒVÇ6R–b†tf÷&ÖÓÒe$ÕõtdR—°¢òò&öÖ&òVRVçFVâÆF—&V66–öâFVÂÖ÷f–Ö–VçFð¢–çBG’Ò†vVõfVÅ’Â’ò×"¢#°¢f–ÆÅG&–ævÆR†7‚Ò"Â7’Â7‚²"Â7’Â7‚Â7’²G’Â†’“°¢f–ÆÅG&–ævÆR†7‚Ò"Â7’Â7‚²"Â7’Â7‚Â7’ÒG’ò"ÂF²“°¢ÒVÇ6R²òòe$ÕõTdð¢f–ÆÄ6—&6ÆR†7‚Â7’ÒÂ"Â†’“²òò7WVÆ¢vVôf–ÆÄÂ†&'VbÂ7‚Ò"Â7’Â"¢"Â"ò"²"ÂF²“²òò&6P¢f–ÆÄ6—&6ÆR†7‚Â7’Ò"ò"Â"â"òB¢"ÂW–R“°¢G&t6—&6ÆR†7‚Â7’ÒÂ"ÂÖ—ƒScR††’Â&v#ScRƒ#SRÃ#SRÃ#SR’Â“’“°¢Ð§Ð §7FF–2fö–BvVôG&u'F–6ÆW2‚—°¢f÷"†–çB’Ò²’ÂtTõõ%Eôã²’²²—°¢–b‚vVõ'E¶•ÒçW6VB’6öçF–çVS°¢fÆöBbÒvVõ'E¶•ÒæÆ–fRòã†c²–b†bâ’bÒ²–b†bÂ’bÒ°¢–çB7¢Ò"²†–çB’†b¢b“°¢V–çC…÷BÒ‡V–çC…÷B’†b¢#3R“°¢V–çCe÷B6öÂÒ†’b’òu6¶–â¢&v#ScRƒ#CÃ#CbÃ#SR“°¢f–ÆÅ&V7D‚†–çB–vVõ'E¶•Òç‚Ò7¢ò"Â†–çB–vVõ'E¶•Òç’Ò7¢ò"Â7¢Â7¢Â6öÂÂ“°¢Ð§Ð ¢òòÒÒÒÒ&æFW&—FFVÂVÇF–Öò6†V6·ö–çB‡6öÆò&7F–6Â6’W7FVâçFÆÆ’ÒÒÒÐ§7FF–2fö–BvVôG&t6†V6·ö–çB‚—°¢–b‚u&7F–6RÇÂvVô56WB’&WGW&ã°¢–çB7‚ÒtTõõÅ‚²†–çB’†vVô567&öÆÂÒvVõ67&öÆÂ“²òòFW&—f†6–Æ—§V–W&F¢–b‡7‚ÂÓbÇÂ7‚âÅr’&WGW&ã°¢dÆ–æR‡7‚ÂtTõôdÄôõ%õ’Ò3BÂ3BÂ&v#ScRƒ#SRÃ#SRÃ#SR’“°¢f–ÆÅG&–ævÆR‡7‚ÂtTõôdÄôõ%õ’Ò3BÂ7‚²bÂtTõôdÄôõ%õ’Ò#‚Â7‚ÂtTõôdÄôõ%õ’Ò#"Â&v#ScRƒ#Ã#SRÃC’“°§Ð ¢òòÒÒÒÒ…TB‡6Æ—"²&öw&W6ò²–çFVçF÷2²&÷Föâ&V–æ–6–"5Vâ&7F–6’ÒÒÒÐ§7FF–2fö–BvVôG&t…TB‚—°¢vVôf–ÆÄÂ†&'VbÂÂÂÅrÂtTõô…TEô‚Â&v#ScRƒ‚Ã‚Ã3B’“°¢òò&÷Föâ6Æ—"‡gVVÇfRÂ6VÆV7F÷"’ÒÒW7V–æ7WW&–÷"—§V–W&Fà¢f–ÆÅ&÷VæE&V7BƒÂbÂCbÂ#‚Â‚Â&v#ScRƒS‚ÃS‚Ã“b’“°¢7G&ö¶U6Vrƒ3bÂ"Â#bÂ#Â"ÂtTõõE…B“°¢7G&ö¶U6Vrƒ#bÂ#Â3bÂ#‚Â"ÂtTõõE…B“°¢òò&'&FR&öw&W6òà¢–çB'‚ÒcbÂ'‡"Òu&7F–6Rò„ÅrÒƒ’¢„ÅrÒ““°¢–çB'rÒ'‡"Ò'‚Â'’Ò2Â&‚Ò#°¢f–ÆÅ&÷VæE&V7B†'‚Â'’Â'rÂ&‚ÂbÂ&v#ScRƒ3‚Ã3‚ÃcB’“°¢fÆöB"ÒvVõ67&öÆÂò†fÆöB–tÆVã²–b‡"Â’"Ò²–b‡"â’"Ò°¢–b‡"âãb’f–ÆÅ&÷VæE&V7B†'‚Â'’Â†–çB’†'r¢"’Â&‚ÂbÂtæVöâ“°¢6†"³Ó²6ç&–çFb‡Â6—¦Vöb‡’Â"VBRR"Â†–çB’‡"¢’“°¢G&uFW‡E"†'‡"²†u&7F–6RòC¢s‚’Â'’²ÂÂÂtTõõE…B“°¢òòÖöFò²–çFVçF÷2à¢6†"%³#eÓ²6ç&–çFb†"Â6—¦Vöb†"’Â"W22VB"Âu&7F–6Rò%&7F–6"¢$æ÷&ÖÂ"ÂvVôGFV×G2“°¢G&uFW‡E"„ÅrÒ‚Â#‚Â"ÂÂÖ—ƒScR„tTõõE…BÂu6·’Âc’“°¢òò&÷Föâ%&V–æ–6–"5"‡6öÆò&7F–6’à¢–b†u&7F–6R—°¢f–ÆÅ&÷VæE&V7B„ÅrÒ3"ÂbÂ#"Â#‚Â‚Â&v#ScRƒsÃSBÃ’“°¢G&uFW‡D2„ÅrÒsÂ2Â%&V–æ–6–"5"ÂÂtTõõE…B“°¢Ð§Ð ¢òòÒÒÒÒ6ö×÷6–6–öâFRVâg&ÖRFR¥TTtò†Vâ&'Vb’’föÆ6FòFöÖ–6òÒÒÒÐ§7FF–2fö–BvVõ&VæFW$vÖR‚—°¢tÆæBÒG'VS²6WD'Vb†&'Vb“°¢t6Æ—ƒÒ²t6Æ—ƒÒ45%õrÒ²t6Æ—“Ò²t6Æ—“Ò45%ô‚Ò°¢–çBuÂÒtÖ–æ’ò„tTõõÂò"’¢tTõõÃ°¢vVôG&t&6¶w&÷VæB‚“°¢vVôG&tfÆö÷"‚“°¢vVôG&tö'7F6ÆW2‚“°¢vVôG&t6†V6·ö–çB‚“°¢–b†vVõ7FFRÒtTõôDTB—°¢vVôG&uG&–Â‚“°¢vVôG&uÆ–W"„tTõõÅ‚²uÂò"Â†–çB–vVõÆ–W%’²uÂò"ÂuÂò"“°¢ÒVÇ6R°¢vVôG&u'F–6ÆW2‚“°¢Ð¢vVôG&t…TB‚“°¢–b†vVõ7FFRÓÒtTõôDTB—°¢G&uFW‡D2„Årò"ÂtTõôdÄôõ%õ’Ò“bÂ$fÆÆ7FR"ÂBÂtTõõE…B“°¢ÒVÇ6R–b†vVõ7FFRÓÒtTõõt”â—°¢G&uFW‡D2„Årò"ÂtTõôdÄôõ%õ’Ò‚Â%Ç„3%Ç„æ—fVÂ6ö×ÆWFFò"ÂBÂtæVöâ“°¢G&uFW‡D2„Årò"ÂtTõôdÄôõ%õ’Òs‚Â%Fö6&föÇfW"Â6VÆV7F÷""Â"ÂtTõõE…B“°¢Ð¢&W6VçBƒÂ45%ô‚Ò“°§Ð ¢òò2222222222222222222222222222222222222222222222222222222222220¢òò224TÄT5Dõ"DRä•dTÂ†6''W6VÂÂ6öÖòVÂ6VÆV7F÷"&VÂFRtB¢òò2222222222222222222222222222222222222222222222222222222222220 ¢òòW7G&VÆÆ—FFRBVçF2†F–f–7VÇFB’ÒÒ&&FÂ6–âFWVæFW"FRÆgVVçFRà§7FF–2fö–BvVõ7F"†–çB7‚Â–çB7’Â–çB"ÂV–çCe÷B6öÂ—°¢–çBBÒ"ò3°¢f–ÆÅG&–ævÆR†7‚Â7’Ò"Â7‚ÒBÂ7’Â7‚²BÂ7’Â6öÂ“°¢f–ÆÅG&–ævÆR†7‚Â7’²"Â7‚ÒBÂ7’Â7‚²BÂ7’Â6öÂ“°¢f–ÆÅG&–ævÆR†7‚Ò"Â7’Â7‚Â7’ÒBÂ7‚Â7’²BÂ6öÂ“°¢f–ÆÅG&–ævÆR†7‚²"Â7’Â7‚Â7’ÒBÂ7‚Â7’²BÂ6öÂ“°§Ð ¢òò&'&FR&öw&W6òFVÂ6VÆV7F÷"‡fW&FRæ÷&ÖÂò6VÆW7FR&7F–6’à§7FF–2fö–BvVõ6VÄ&"†–çB‚Â–çB’Â–çBrÂ–çB‚ÂV–çCe÷Bf–ÆÆ2Â–çB7BÂ&ööÂFöæRÂ6öç7B6†"¢f—†VB—°¢f–ÆÅ&÷VæE&V7B‡‚Â’ÂrÂ‚Â‚ò"Â&v#ScRƒ#‚Ã3ÃCB’“°¢G&u&÷VæE&V7B‡‚Â’ÂrÂ‚Â‚ò"ÂÖ—ƒScR†f–ÆÆ2Â&v#ScRƒ#SRÃ#SRÃ#SR’Âc’“°¢–b‡7BÂ’7BÒ²–b‡7Bâ’7BÒ°¢–çBgrÒ†FöæRòr¢‡r¢7Bò’“°¢–b†grâ‚’f–ÆÅ&÷VæE&V7B‡‚Â’ÂgrÂ‚Â‚ò"Âf–ÆÆ2“°¢VÇ6R–b†grâ’f–ÆÅ&V7B‡‚²‚ò"Â’ÂgrÂ‚Âf–ÆÆ2“°¢6†"E³eÓ°¢–b†f—†VB’6ç&–çFb‡BÂ6—¦Vöb‡B’Â"W2"Âf—†VB“°¢VÇ6R–b†FöæR’6ç&–çFb‡BÂ6—¦Vöb‡B’Â$4ôÕÄUDDò"“°¢VÇ6R6ç&–çFb‡BÂ6—¦Vöb‡B’Â"VBRR"Â7B“°¢G&uFW‡D2‡‚²rò"Â’²‚ò"ÒrÂBÂ"Â&v#ScRƒ#SRÃ#SRÃ#SR’“°§Ð §7FF–2fö–BvVõ&VæFW%6VÆV7B‚—°¢tÆæBÒG'VS²6WD'Vb†&'Vb“°¢t6Æ—ƒÒ²t6Æ—ƒÒ45%õrÒ²t6Æ—“Ò²t6Æ—“Ò45%ô‚Ò°¢V–çCe÷B6·’ÒtTõôÄUdTÅ5¶t7W$ÆWfVÅÒç6·“°¢V–çCe÷BæVöâÒtTõôÄUdTÅ5¶t7W$ÆWfVÅÒææVöã°¢V–çCe÷B6¶–âÒtTõôÄUdTÅ5¶t7W$ÆWfVÅÒç6¶–ã°¢vVôf–ÆÄÂ†&'VbÂÂÂÅrÂÄ‚Â6·’“°¢òòg&æ¦FV6÷&F—f7WW&–÷"†&Æ÷VW2W7F–ÆòÖVçRtB’à¢vVôf–ÆÄÂ†&'VbÂÂÂÅrÂ"ÂÖ—ƒScR‡6·’Â&v#ScRƒ#SRÃ#SRÃ#SR’ÂC’“°¢f÷"†–çB‚Ò²‚ÂÅs²‚³Òc’vVôf–ÆÄÂ†&'VbÂ‚²bÂ"ÂC"Â#ÂÖ—ƒScR†æVöâÂ6·’Â#’“° ¢òò&÷Föâ6Æ—"†6–W'&Æ’ÒÒW7V–æ7WW&–÷"—§V–W&Fà¢f–ÆÅ&÷VæE&V7BƒÂ‚ÂCbÂ#‚Â‚ÂÖ—ƒScR‡6·’ÂtTõô”ä²Â’“°¢7G&ö¶U6Vrƒ3bÂBÂ#bÂ#"Â"ÂtTõõE…B“°¢7G&ö¶U6Vrƒ#bÂ#"Â3bÂ3Â"ÂtTõõE…B“° ¢òòfÆV6†2FVÂ6''W6VÂà¢f–ÆÅG&–ævÆRƒCBÂ#CÂƒBÂ#ÂƒBÂ#sÂ&v#ScRƒ#SRÃ#SRÃ#SR’“°¢f–ÆÅG&–ævÆRƒS"Â#CÂƒ"Â#bÂƒ"Â#cBÂÖ—ƒScR‡6·’ÂtTõô”ä²Â“’“°¢f–ÆÅG&–ævÆRƒsSbÂ#CÂsbÂ#ÂsbÂ#sÂ&v#ScRƒ#SRÃ#SRÃ#SR’“°¢f–ÆÅG&–ævÆRƒsC‚Â#CÂs‚Â#bÂs‚Â#cBÂÖ—ƒScR‡6·’ÂtTõô”ä²Â“’“° ¢òòF&¦WF6VçG&Âà¢–çBƒÒc‚Â“ÒcÂrÒCcBÂ‚Ò3c°¢f–ÆÅ&÷VæE&V7B‡ƒÂ“ÂrÂ‚Â‚ÂÖ—ƒScR‡6·’ÂtTõô”ä²Â#’“°¢G&u&÷VæE&V7B‡ƒÂ“ÂrÂ‚Â‚ÂÖ—ƒScR†æVöâÂ6·’Â’“° ¢òò$6&FVÂ7V&ò"†7VG&FòFR6öÆ÷"’²ö¦÷2Â6öÖòVÂ–6öæòFVÂæ—fVÂà¢–çBg‚Òƒ²#bÂg’Ò“²#BÂg2ÒcC°¢f–ÆÅ&÷VæE&V7B†g‚Âg’Âg2Âg2Â"Â6¶–â“°¢G&u&÷VæE&V7B†g‚Âg’Âg2Âg2Â"ÂÖ—ƒScR‡6¶–âÂ&v#ScRƒ#SRÃ#SRÃ#SR’Âƒ’“°¢f–ÆÅ&V7B†g‚²bÂg’²#BÂÂ"Â&v#ScRƒ#Ã#"Ã3B’“°¢f–ÆÅ&V7B†g‚²g2Ò#bÂg’²#BÂÂ"Â&v#ScRƒ#Ã#"Ã3B’“° ¢òòæöÖ'&R²F–f–7VÇFBà¢G&uFW‡B‡ƒ²BÂ“²3BÂtTõôÄUdTÅ5¶t7W$ÆWfVÅÒææÖRÂ2Â&v#ScRƒ#SRÃ#SRÃ#SR’“°¢6†"7E³eÓ²6ç&–çFb‡7BÂ6—¦Vöb‡7B’Â"VB"ÂtTõôÄUdTÅ5¶t7W$ÆWfVÅÒç7F'2“°¢–çB7rÒFW‡Er‡7BÂ"“°¢G&uFW‡B‡ƒ²rÒ#BÒ7rÒ‚Â“²#Â7BÂ"Â&v#ScRƒ#SÃ#Ãƒ’“°¢vVõ7F"‡ƒ²rÒ#"Â“²#bÂ’Â&v#ScRƒ#SÃ#Ãƒ’“° ¢òò&'&2æ÷&ÖÂò&7F–6à¢G&uFW‡D2‡ƒ²rò"Â“²‚Â$æ÷&ÖÂÖöFR"Â"Â&v#ScRƒ#3Ã#3bÃ#S’“°¢vVõ6VÄ&"‡ƒ²CBÂ“²C"ÂrÒƒ‚Â3BÂ&v#ScRƒsÃ##Ã“’À¢vVô&W7E¶t7W$ÆWfVÅÒÂvVôFöæU¶t7W$ÆWfVÅÒÂåTÄÂ“°¢G&uFW‡D2‡ƒ²rò"Â“²#"Â%&7F–6RÖöFR"Â"Â&v#ScRƒ#3Ã#3bÃ#S’“°¢vVõ6VÄ&"‡ƒ²CBÂ“²##bÂrÒƒ‚Â3BÂ&v#ScRƒ“Ã#RÃ#C’ÂÂfÇ6RÂ%&7F–6""“° ¢òòVçF÷2–æF–6F÷&W2FVÂ6''W6VÂà¢f÷"†–çB’Ò²’ÂC²’²²—°¢–çBG‚ÒÅrò"Ò2¢#"ò"²’¢##°¢–b†’ÓÒt7W$ÆWfVÂ’f–ÆÄ6—&6ÆR†G‚ÂC3"ÂbÂ&v#ScRƒ#SRÃ#SRÃ#SR’“°¢VÇ6Rf–ÆÄ6—&6ÆR†G‚ÂC3"ÂBÂÖ—ƒScR‡6·’Â&v#ScRƒ#SRÃ#SRÃ#SR’Â#’“°¢Ð¢G&uFW‡D2„Årò"ÂCC‚Â%Fö6Væ&'&&§Vv""ÂÂÖ—ƒScR‡&v#ScRƒ#SRÃ#SRÃ#SR’Â6·’Â“’“° ¢&W6VçBƒÂ45%ô‚Ò“°§Ð ¢òò2222222222222222222222222222222222222222222222222222222222220¢òò22ÔTåRDR¥TTtõ2†6÷"Væ6–ÖFRvVòF6‚ògWF&öÂ¢òò22ÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÐ¢òò22Æ$§VVv÷2"‡6Æ÷BFRõ$Tr’–äòVçG&F—&V7F¢òò22vVòF6ƒ¢vVôVçFW"‚’'&R$”ÔU$òW7FRÖVçRÂVRFV¦VÆVv— ¢òò22$vVöÖWG'’F6‚"†Ö÷F÷"÷&–v–æÂÂ–çF7Fò’ò$gWF&öÂ"âVÀ¢òò22&÷WF–ærW7FVâvVõF–6²‚’†Ö2&¦ò’âVÂÖ÷F÷"FRvVòF6€¢òò22æò6RFö6Óâ4U$ò&Vw&W6–öæW3²6öÆò6Ö&–VÂDU5D”äòFVÀ¢òò22&÷Föâ6Æ—"FR7R6VÆV7F÷"†çFW26W'&&Æ²†÷&¢òò22gVVÇfRW7FRÖVçR’âFöFòÆòçVWfòÆÆWf&Vf–¦òvÖW2¢ögWB¢à¢òò2222222222222222222222222222222222222222222222222222222222220¦VçVÒ²tÕôÔTåRÒÂtÕôtTòÂtÕôeUBÓ°§7FF–2–çBtvÖTÖöFRÒtÕôÔTåS° ¢òò2222222222222222222222222222222222222222222222222222222222220¢òò22eUD$ôÂ$4DR†ÖöFòÆæG66RÂ6Ö&ÇF6öâ67&öÆÂ¢òò22ÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÐ¢òò22Ö—6ÖòG&öâVRvVòF6ƒ¢Æ–Vç¦òÄôt”4òƒƒCƒ„År‚Ä‚’À¢òò226ö×öæRVâ&'Vb’gVVÆ66öâ&W6VçB‚’†çF’ÖfÆ–6¶W"’Âã3 ¢òò22g26öâF‡&÷GFÆR÷"Ö–ÆÆ—2‚’„tTõôe$ÔUôÕ2’’f—6–66öà¢òò227V'7FWf–¦ò„tTõõ5T%5DU’ÂvF6†För6VwW&ò‡6–âFVÆ—2’à¢òò22DôDòVÂW7FFòW27FF–2‡6–â†V“¢æò6RÆö6æ–æwVà¢òò22g&ÖV'VffW"çVWfòÒÒ6RF–'V¦F—&V7Fò6ö'&R&'VbÂ–wVÂVP¢òò22vVõ&VæFW$vÖR‚’âä”äuTâ7G'V7BçVWfò„gWE’6RW66öÖð¢òò22$ÔUE$òFRgVæ6–öã¢6R–æFW†÷"–çBÂ6’Æ÷2&÷F÷F—÷0¢òò22VRWFòÖvVæW&VÂ”DRçVæ6&VfW&Væ6–âVâF—òFW66öæö6–Fòà¢òò20¢òò22„$Et$S¢–çWBF7F–ÂFRTâ4ôÄòVçFò‡7G'V7BF÷V6‚B’âæð¢òò22†’¦÷—7F–6²æ’&÷FöæW2f—6–6÷2Â6’VRÆ÷26öçG&öÆW26öà¢òò22¤ôä2F7F–ÆW2F–'V¦F3¢Vâ7F–6²æÆöv–6òf—'GVÂ†×VWfP¢òò22Â§VvF÷"6öâ&ÆöâòÖ26W&6æò’’&÷FöæW27&–çBõ6Rð¢òò22F—&òô6Ö&–òâÂ6W"Vâ6öÆòFVFòÂÖ÷fW"’VÇ6"&÷Föâ6öà¢òò22W†6ÇW–VçFW2÷"F—6Væó¢VÂ7F–6²f—fRVâÆÖ—FB—§V–W&F¢òò22’Æ÷2&÷FöæW2VâÆFW&V6†²7&–çBW2VâDôttÄR’6RõF—&òð¢òò226Ö&–òW6âÆTÅD”ÔF—&V66–öâFVÂ7F–6²†gWDf6R’6öÖð¢òò22÷&–VçF6–öââ6WVFòÓ4B&&Fó¢W66ÆFò÷"&ögVæF–FB…¢òò22Vâ6æ6†’6öâF&Æ2‚&V6Æ7VÆF2Â6–âG&–r÷"7&—FRà¢òò2222222222222222222222222222222222222222222222222222222222220 ¢òòÒÒÒÒvVöÖWG&–FVÂ6×ò†6ö÷&G2FRÕTäDò’ÒÒÒÐ¢6FVf–æReUEôÄTâCòòÆ&vòFVÂ6×ò†V¦R‚×VæFó²÷'FW&–Vâ’VâeUEôÄTâ¢6FVf–æReUEõt”B3òòæ6†òFVÂ6×ò†V¦R’×VæFó²ÖföæFòÆV¦æòÂeUEõt”CÖ6W&6¢6FVf–æReUEôÔ”E’Sòò6VçG&òFVÂæ6†ò„eUEõt”Bó"¢6FVf–æReUEôt„ÄbCbòòÖVF–òÖæ6†òFRÆ÷'FW&–†Vâ’FR×VæFòÂ6VçG&FVâeUEôÔ”E’¢6FVf–æReUEô5‚Còò‚FRçFÆÆVR6–wVRÆ6Ö&„Åró"¢6FVf–æReUEô„õ$•¤ôâbòò’FRçFÆÆFRÆF÷V6†Æ–æRÄT¤ä†föæFòFVÂ6×ò¢6FVf–æReUEô$õEDôÒCS"òò’FRçFÆÆFRÆF÷V6†Æ–æR4U$4ä ¢òòÒÒÒÒf—6–6‡Væ–FFW2FR×VæFò÷6Vs²GB&VÂf–Ö–ÆÆ—2‚’’ÒÒÒÐ¢6FVf–æReUEôÔD4…õ4T2Sãbòò6VwVæF÷2&VÆW2FRVâ'F–Fò6ö×ÆWFò‚Óâ“rFR§VVvò¢6FVf–æReUEô$4Uõ5B‚ãbòòfVÆö6–FB&6RFR6'&W&FRVâ§VvF÷ ¢6FVf–æReUEõ5$”åEôÕTÂãc&bòò×VÇF—Æ–6F÷"FR7&–ç@¢6FVf–æReUEô4%%•õ5B‚ãbòòfVÆö6–FBFVÂ÷'FF÷"†ÆvòÖ2ÆVçFòVR6÷'&–VæFò¢6FVf–æReUEôtµõ5B“bãbòòfVÆö6–FBFVÂ÷'FW&ð¢6FVf–æReUEõ55õ5B3ƒãbòòfVÆö6–FBFVÂ&ÆöâVâVâ6P¢6FVf–æReUEõ4„ôõEõ5BSCãbòòfVÆö6–FBFVÂ&ÆöâVâVâF—&ð¢6FVf–æReUEôE$”$$ÄRRãbòòF—7Fæ6–FVÂ&Æöâ÷"FVÆçFRFVÂ÷'FF÷ ¢6FVf–æReUEô5E$Åõ"Rãbòò&F–òFR6öçG&öÂFVÂ&Æöâ7VVÇFò†§VvF÷"FR6×ò¢6FVf–æReUEôtµô5E$Åõ"#bãbòò&F–òFR6öçG&öÂöF¦FFVÂ÷'FW&ò†Ö2×Æ–ò¢6FVf–æReUEõD4´ÄUõ"Rãbòò&F–ò&–çFVçF"V—F"VÂ&Æöà¢6FVf–æReUEõ4„ôõEõ$ätR3ƒãbòòF—7Fæ6–÷'FW&–ÆVRÆ”6RÆçFVF—& ¢6FVf–æReUEô$ÄÅôrCcãbòòw&fVFBFVÂ&ÆöâVâVÂ—&R†ÇGW&¢¢6FVf–æReUEô$ÄÅôe$”2ãvbòò&÷¦Ö–VçFòFVÂ&Æöâ7VVÇFòVâVÂ7VVÆò‡÷"6Vr ¢òòÒÒÒÒ6öçG&öÆW2f—'GVÆW2†6ö÷&G2Äôt”42ÆæG66R’ÒÒÒÐ¢6FVf–æReUEõ5Dµô5‚‚òò7F–6²æÆöv–6ó¢6VçG&ò€¢6FVf–æReUEõ5Dµô5’3s"òò7F–6²æÆöv–6ó¢6VçG&ò¢6FVf–æReUEõ5Dµõ"cbòò&F–òFVÂ7F–6°¢6FVf–æReUEõ5DµôDTB"òò¦öæ×VW'F¢6FVf–æReUEô%Dåõ"3òò&F–òFRÆ÷2&÷FöæW0¦VçVÒ²d%Eõ5"ÒÂd%Eõ2Âd%EõD•"Âd%Eô4ÒÓ°§7FF–26öç7B–çCe÷BeUEô%Då…³EÒÒ²c“bÂc“bÂscBÂc#‚Ó²òò5"'&–&Â2&¦òÂD•"FW"Â4Ò—§§7FF–26öç7B–çCe÷BeUEô%Då•³EÒÒ²3bÂC3‚Â3s"Â3s"Ó° ¢òòÒÒÒÒ&öÆW2’W7FF÷2FR”†ÖV–æFRW7FF÷2&&F’ÒÒÒÐ¦VçVÒ²e%ôt²ÒÂe%ôDTbÂe%ôÔ”BÂe%ôetBÓ°¦VçVÒ²dô„ôÄBÒÂdô4„4RÂdõ5Uõ%BÂdôÔ$²Âdôt²Âdô4%%’Ó° ¢òòÒÒÒÒf÷&Ö6–öâ&6RBÓ2Ó2FVÂWV—ò†F6µ‚’âVÂWV—òW0¢òòVÂW7V¦ò‡‚rÒeUEôÄTâÒ‚’â§VvF÷&W2÷"WV—òâÒÒÒÐ§7FF–26öç7B–çCe÷BeUEôdõ$Õõ…³ÒÒ²cÂ3Ã3Ã3Ã3ÂScÃcÃScÂƒcÃ“CÃƒcÓ°§7FF–26öç7B–çCe÷BeUEôdõ$Õõ•³ÒÒ²SÂcÃÃ“Ã#CÂ“ÃSÃ#ÂƒÃSÃ##Ó°§7FF–26öç7BV–çC…÷BeUEôdõ$Õõ%³ÒÒ²e%ôt²Âe%ôDTbÄe%ôDTbÄe%ôDTbÄe%ôDTbÂe%ôÔ”BÄe%ôÔ”BÄe%ôÔ”BÂe%ôetBÄe%ôetBÄe%ôetBÓ° ¢òò2222222222222222222222222222222222222222222222222222222222220¢òò22UT•õ2ƒ#BÓ3"’TâdÄ4‚‚ç&öFF’ÒÒäòVâ$Òõ5$Òà¢òò227G'V7B6ö×7FòVF–Fó¢²6†"æöÖ'&U³eÓ²V–çC…÷B&F–æs°¢òò22V–çCe÷B6öÆ÷$²V–çCe÷B6öÆ÷$#²Òâ6–âW67VF÷2öÆöv÷26öà¢òò22FW&V6†÷3¢VÂW67VFò6RvVæW&Vâ'VçF–ÖR†f÷&Ö²6öÆ÷&W2’à¢òò22–æ6ÇW–R6VÆV66–öæW2Â6ÇV&W2F÷’6ÇV&W2W'Væ÷2à¢òò2222222222222222222222222222222222222222222222222222222222220§7G'V7BgWEFVÒ²6†"æöÖ'&U³eÓ²V–çC…÷B&F–æs²V–çCe÷B6öÆ÷$²V–çCe÷B6öÆ÷$#²Ó°§7FF–26öç7BgWEFVÒeUEõDTÕ5µÒÒ°¢²%W'R"Âs‚Â&v#ScRƒ##ÃCÃc’Â&v#ScRƒ#CRÃ#CRÃ#CR’ÒÂòò‡÷"FVfV7Fò¢²$&vVçF–æ"Â“Â&v#ScRƒÃ“Ã#3R’Â&v#ScRƒ#CRÃ#CRÃ#CR’ÒÀ¢²$'&6–Â"Â“Â&v#ScRƒ#CRÃ#RÃC’Â&v#ScRƒ#ÃÃc’ÒÀ¢²%W'VwV’"ÂƒBÂ&v#ScRƒ“ÃsÃ##R’Â&v#ScRƒ#Ã#BÃC’ÒÀ¢²$6†–ÆR"Âs’Â&v#ScRƒ#ÃCÃS’Â&v#ScRƒ3ÃcÃC’ÒÀ¢²$6öÆöÖ&–"Âƒ2Â&v#ScRƒ#CRÃ#ÃC’Â&v#ScRƒ3ÃsÃc’ÒÀ¢²$ÖW†–6ò"ÂƒÂ&v#ScRƒ#Ã3Ãs’Â&v#ScRƒ#CRÃ#CRÃ#CR’ÒÀ¢²$W7æ"Â“Â&v#ScRƒ#Ã3ÃC’Â&v#ScRƒ3ÃCÃ#’ÒÀ¢²$g&æ6–"Â“"Â&v#ScRƒCÃsÃs’Â&v#ScRƒ#CRÃ#CRÃ#CR’ÒÀ¢²$ÆVÖæ–"Â“Â&v#ScRƒ#CÃ#CÃ#CR’Â&v#ScRƒ#Ã#BÃ3B’ÒÀ¢²$–ævÆFW'&"Âƒ‚Â&v#ScRƒ#CRÃ#CRÃ#CR’Â&v#ScRƒ3ÃCÃ#’ÒÀ¢²$—FÆ–"Âƒ‚Â&v#ScRƒCÃ“Ã“’Â&v#ScRƒ#CRÃ#CRÃ#CR’ÒÀ¢²%÷'GVvÂ"Âƒ’Â&v#ScRƒ“Ã3ÃS’Â&v#ScRƒ#ÃÃs’ÒÀ¢²%—6W2&¦÷2"ÂƒbÂ&v#ScRƒ#CÃ3Ã3’Â&v#ScRƒ#CRÃ#CRÃ#CR’ÒÀ¢²$Æ–ç¦Æ–Ö"ÂsbÂ&v#ScRƒ3ÃcÃS’Â&v#ScRƒ#CRÃ#CRÃ#CR’ÒÂòòBW'Væð¢²%Væ—fW'6—F&–ò"ÃsbÂ&v#ScRƒ#3RÃ#3Ã#R’Â&v#ScRƒCÃ3ÃS’ÒÂòòRW'Væò†7&VÖ¢²%2â7&—7FÂ"ÂsRÂ&v#ScRƒ“ÃƒÃ##R’Â&v#ScRƒ#CRÃ#CRÃ#CR’ÒÂòòbW'Væò†6VÆW7FR¢²$&&6VÆöæ"Â“Â&v#ScRƒCÃ3Ãc’Â&v#ScRƒ3ÃSÃ#’ÒÂòòr†w&æ²§VÂ¢²%&VÂÖG&–B"Â“Â&v#ScRƒ#CRÃ#CRÃ#CR’Â&v#ScRƒ#ÃƒÃc’ÒÀ¢²$Öâ6—G’"Â“Â&v#ScRƒ#Ã“RÃ##R’Â&v#ScRƒ#CRÃ#CRÃ#CR’ÒÀ¢²$Æ—fW'ööÂ"Âƒ‚Â&v#ScRƒ#ÃCÃS’Â&v#ScRƒ#CRÃ#CRÃ#CR’ÒÀ¢²$&–W&â"Â“Â&v#ScRƒ#Ã3ÃS’Â&v#ScRƒ#CRÃ#CRÃ#CR’ÒÀ¢²%4r"Âƒ‚Â&v#ScRƒ3ÃCÃ“’Â&v#ScRƒ#ÃCÃc’ÒÀ¢²$§WfVçGW2"ÂƒbÂ&v#ScRƒ#CÃ#CÃ#CR’Â&v#ScRƒ#Ã#"Ã3’ÒÀ¢²$Ö–Æâ"ÂƒRÂ&v#ScRƒ#Ã3ÃCR’Â&v#ScRƒ#Ã#"Ã3’ÒÀ¢²$&ö6§Væ–÷'2"ÂƒÂ&v#ScRƒ3ÃSÃ3’Â&v#ScRƒ#3Ã“Ãc’ÒÀ¢²%&—fW"ÆFR"ÂƒÂ&v#ScRƒ#CÃ#CÃ#CR’Â&v#ScRƒ#ÃCÃc’ÒÀ¢²$fÆÖVævò"Âƒ"Â&v#ScRƒ#ÃCÃS’Â&v#ScRƒ#Ã#"Ã3’ÒÀ§Ó°¢6FVf–æReUEôåDTÕ2‚†–çB’‡6—¦Vöb„eUEõDTÕ2’ò6—¦Vöb„eUEõDTÕ5³Ò’’ ¢òò2222222222222222222222222222222222222222222222222222222222220¢òò22U5DDòDTÂ%D”Dò‡FöFò7FF–2Â6–â†V¢òò2222222222222222222222222222222222222222222222222222222222220¢6FVf–æReUEôå# §7G'V7BgWE²fÆöB‚Â’Âg‚Âg“²V–çC…÷BFVÓ²V–çC…÷B&öÆS²V–çC…÷B7C²fÆöBF6C²Ó°§7FF–2gWEgWE´eUEôåÓ° ¦VçVÒ²e5õDTÒÒÂe5ôõÂe5õÄ’Âe5ôTäBÓ²òòçFÆÆ7F—fFVÂÖöFògWF&öÀ§7FF–2–çBgWE67&VVâÒe5õDTÓ°§7FF–2–çBgWEFVÕ6VÅ³%ÒÒ²ÂÓ²òò³Ó×GRWV—òÂ³Ó×&—fÂ†–æF–6W2VâeUEõDTÕ2§7FF–2–çBgWE66÷&TÒÂgWE66÷&T"Ò°§7FF–2fÆöBgWEÆ•6V2Ò²òòF–V×òFR¥TTtò7V×VÆFòƒâãSCÒrâã“r §7FF–2fÆöBgWD'‚ÂgWD'’ÂgWD'£²òò&Æöã¢÷6–6–öâ×VæFò²ÇGW& §7FF–2fÆöBgWD'g‚ÂgWD'g’ÂgWD'g£²òò&Æöã¢fVÆö6–F@§7FF–2–çBgWD÷væW"ÒÓ²òòÓÒ7VVÇFòÂVÇ6R–æF–6RFVÂ÷'FF÷ §7FF–2–çBgWD7G&ÂÒ²òò§VvF÷"6öçG&öÆFò‡6–V×&RFVÂWV—ò§7FF–2–çBgWDÆ7EF÷V6‚ÒÓ²òòVÇF–ÖòVRFö6òVÂ&Æöâ‡&6ööÆF÷vâFR6R§7FF–2fÆöBgWEF÷V6„6BÒ²òò6ööÆF÷vâ&VRVÂ6F÷"æò&V6–&7R&÷–ò6P§7FF–2fÆöBgWD6ÒÒeUEô5ƒ²òò‚FR×VæFò&¦òÆ6Ö&‡6–wVRÂ&Æöâ §7FF–2fÆöBgWDÖ÷fU‚ÒÂgWDÖ÷fU’Ò²òòfV7F÷"FVÂ7F–6²‚Óâã§7FF–2fÆöBgWDf6U‚ÒÂgWDf6U’Ò²òòVÇF–ÖF—&V66–öâ†÷&–VçF6–öâ&6R÷F—&ò§7FF–2–çBgWE7F´¶æö%‚ÒÂgWE7F´¶æö%’Ò²òòFW7Æ¦Ö–VçFòf—7VÂFRÆW&–ÆÆ§7FF–2&ööÂgWE7&–çBÒfÇ6S²òòFövvÆRFR7&–ç@§7FF–2&ööÂgWE&W72ÒfÇ6RÂgWE&W6†ö÷BÒfÇ6RÂgWE&W7v—F6‚ÒfÇ6RÂgWE&W7&–çBÒfÇ6S° §7FF–2V–çC3%÷BgWDg&ÖT×2Ò²òòÖ–ÆÆ—2‚’FVÂg&ÖRçFW&–÷"†GB§7FF–2fÆöBgWDGBÒ²òòGBFVÂg&ÖR7GVÂ‡&Æ”§7FF–2V–çC3%÷BgWDg&VW¦UVçF–ÂÒ²òò6öævVÆ§VVvò‡6VRò6VÆV'&6–öâFRvöÂ§7FF–2&ööÂgWEVæF–ærÒfÇ6S²òò†’Vâ6VRVæF–VçFRG&2VâvöÀ§7FF–2–çBgWD¶–6´æW‡BÒ²òòWV—òVR66G&2VÂvöÀ§7FF–26†"gWDfÆ6…³eÒÒ"#²òò&÷GVÆòw&æFRFV×÷&Â‚$tôÂ" ¢òòÒÒÒÒF&Æ2‚FRW'7V7F—f‡6WVFòÓ4B“¢&ögVæF–FBC×w’ôeUEõt”Bà¢òò6R6Æ7VÆâTäfW¢‡÷vbö–çFW'’Óâ÷"7&—FR6öÆò†’Æöö·W2âÒÒÒÐ¢6FVf–æReUEôE5DU2c@§7FF–2V–çCe÷BgWE7”ÅUE´eUEôE5DU5Ó²òò’FRçFÆÆFRÆ÷2”U26F&ögVæF–F@§7FF–2V–çC…÷BgWE‡B´eUEôE5DU5Ó²òòÇFòFVÂ7&—FR‡‚’6F&ögVæF–F@§7FF–2V–çC…÷BgWE‡4ÅUE´eUEôE5DU5Ó²òòƒ¢6ö×&W6–öâ†÷&—¦öçFÂ‡G&V6–ò’¢#S`§7FF–2&ööÂgWDÅUG&VG’ÒfÇ6S° §7FF–2fö–BgWD'V–ÆDÅUB‚—°¢f÷"†–çB’Ò²’ÂeUEôE5DU3²’²²—°¢fÆöBBÒ†fÆöB–’ò„eUEôE5DU2Ò“²òòÖÆV¦÷2†'&–&’ââÖ6W&6†&¦ò¢fÆöBG’Ò÷vb‡BÂã#&b“²òòÆWfR7W'fFRW'7V7F—fVâ¢gWE7”ÅUE¶•ÒÒ‡V–çCe÷B’„eUEô„õ$•¤ôâ²G’¢„eUEô$õEDôÒÒeUEô„õ$•¤ôâ’“°¢gWE‡E¶•ÒÒ‡V–çC…÷B’ƒ"²B¢3B“²òò"‚†ÆV¦÷2’ââCb‚†6W&6¢fÆöB‡2Òãcb²B¢ãCc²òòãc†ÆV¦÷2’ââã†6W&6’ÓâG&V6–ð¢gWE‡4ÅUE¶•ÒÒ‡V–çC…÷B’‡‡2¢#Sbãb²ãVb“°¢Ð¢gWDÅUG&VG’ÒG'VS°§Ð ¢òòÒÒÒÒ$äs¢&WWF–Æ—¦VÂ†÷'6†–gBFRvVòF6‚†vVõ&æBövVõ&æFb’ÒÒÒÐ§7FF–2–æÆ–æRfÆöBgWDW'&b‚—²&WGW&âvVõ&æFb‚’¢"ãbÒãc²Òòò²ÓâãÐ ¢òò2222222222222222222222222222222222222222222222222222222222220¢òò22E$å4dõ$Ô4”ôäU2ÕTäDòÓâåDÄÄ†f–æW2²ÅUBÂ6–âG&–r¢òò2222222222222222222222222222222222222222222222222222222222220§7FF–2–æÆ–æR–çBgWDD–G‚†fÆöBw’—°¢–çBBÒ†–çB’‡w’¢„eUEôE5DU2Ò’òeUEõt”B“°¢–b†BÂ’BÒ²–b†BâeUEôE5DU2Ò’BÒeUEôE5DU2Ò°¢&WGW&âC°§Ð§7FF–2–æÆ–æR–çBgWDu‚†fÆöBw‚ÂfÆöBw’—²òò‚FRçFÆÆVâVÂ7VVÆð¢–çBBÒgWDD–G‚‡w’“°¢&WGW&âeUEô5‚²†–çB’‚‚†Æöær’‚†–çB’‡w‚ÒgWD6Ò’’¢gWE‡4ÅUE¶EÒ’ãâ‚“°§Ð§7FF–2–æÆ–æR–çBgWDu’†fÆöBw’—²&WGW&âgWE7”ÅUE¶gWDD–G‚‡w’•Ó²Òòò’FRçFÆÆ‡–W2 §7FF–2–æÆ–æRfÆöBgWDF—7C"†–çB’Â–çB¢—°¢fÆöBG‚ÒgWE¶•Òç‚ÒgWE¶¥Òç‚ÂG’ÒgWE¶•Òç’ÒgWE¶¥Òç“°¢&WGW&âG‚¢G‚²G’¢G“°§Ð§7FF–2–æÆ–æRfÆöBgWD†öÖU‚†–çB’—°¢–çB²Ò’R²fÆöB‚ÒeUEôdõ$Õõ…¶µÓ°¢&WGW&â†’ãÒ’ò„eUEôÄTâÒ‚’¢ƒ°§Ð§7FF–2–æÆ–æRfÆöBgWD†öÖU’†–çB’—²&WGW&âeUEôdõ$Õõ•¶’RÓ²Ð§7FF–2–æÆ–æRfÆöBgWDvöÅ‚†–çBFVÒ—²&WGW&â‡FVÒÓÒ’ò†fÆöB”eUEôÄTâ¢ãc²Òòò÷'FW&–VRD4§7FF–2–æÆ–æRfÆöBgWD÷vå‚†–çBFVÒ—²&WGW&â‡FVÒÓÒ’òãb¢†fÆöB”eUEôÄTã²Òòò÷'FW&–VRDTd”TäDP§7FF–2–æÆ–æRfÆöBgWE&F–ær†–çBFVÒ—²&WGW&âeUEõDTÕ5¶gWEFVÕ6VÅ·FVÕÕÒç&F–ærò“’ãc²Òòòâã ¢òò2222222222222222222222222222222222222222222222222222222222220¢òò22$”Ô•D•d2DRD”%T¤ò$õ”2DTÂeUD$ôÀ¢òò2222222222222222222222222222222222222222222222222222222222220¢òòVÆ—6R&VÆÆVæ’Æ7FF‡6öÖ'&&¦ò–W2ò&Æöâ§7FF–2fö–BgWE6†F÷r†–çB7‚Â–çB7’Â–çB'‚Â–çB'’ÂV–çCe÷B6öÂÂV–çC…÷B—°¢–b‡'‚Â’'‚Ò²–b‡'’Â’'’Ò°¢f÷"†–çBG’Ò×'“²G’ÃÒ'“²G’²²—°¢–çBG‚Ò†–çB’‡'‚¢7'FbƒãbÒ†fÆöB–G’¢G’ò‚†fÆöB—'’¢'’’’“°¢„Æ–æT†7‚ÒG‚Â7’²G’Â"¢G‚²Â6öÂÂ“°¢Ð§Ð¢òòVÆ—6R†6öçF÷&æò’&VÂ6—&7VÆò6VçG&À§7FF–2fö–BgWDVÆÆ—6R†–çB7‚Â–çB7’Â–çB'‚Â–çB'’ÂV–çCe÷B6öÂ—°¢–b‡'‚Â"’'‚Ò#²–b‡'’Â’'’Ò°¢–çB‡Ò7‚²'‚Â—Ò7“°¢f÷"†–çBÒ²ÃÒC²²²—°¢fÆöBF‚Ò¢ƒbã#ƒ3ƒS6bòC“°¢–çB‚Ò7‚²†–çB’‡'‚¢6÷6b‡F‚’’Â’Ò7’²†–çB’‡'’¢6–æb‡F‚’“°¢7G&ö¶U6Vr‡‡Â—Â‚Â’ÂÂ6öÂ“²‡Òƒ²—Ò“°¢Ð§Ð¢òòW67VFòvVæW&FòVâ'VçF–ÖR†f÷&ÖVçFvöæÂ²g&æ¦’6öâÆ÷26öÆ÷&W2FVÂWV—ð§7FF–2fö–BgWD7&W7B†–çB7‚Â–çB7’Â–çBrÂ–çB‚ÂV–çCe÷B6öÄÂV–çCe÷B6öÄ"—°¢–çBƒÒ7‚Òrò"Â“Ò7’Ò‚ò"Â‡"Ò‚¢2òS°¢f–ÆÅ&V7B‡ƒÂ“ÂrÂ‡"Â6öÄ“°¢f–ÆÅG&–ævÆR‡ƒÂ“²‡"Âƒ²rÂ“²‡"Â7‚Â“²‚Â6öÄ“°¢–çB7rÒrò3²–b‡7rÂ2’7rÒ3°¢f–ÆÅ&V7B†7‚Ò7rò"Â“Â7rÂ‡"Â6öÄ"“°¢f–ÆÅG&–ævÆR†7‚Ò7rò"Â“²‡"Â7‚²7rò"Â“²‡"Â7‚Â“²‚Ò2Â6öÄ"“°¢V–çCe÷BVFvRÒ&v#ScRƒ#3‚Ã#CÃ#Cb“°¢G&u&V7B‡ƒÂ“ÂrÂ‡"ÂVFvR“°¢7G&ö¶U6Vr‡ƒÂ“²‡"Â7‚Â“²‚ÂÂVFvR“°¢7G&ö¶U6Vr‡ƒ²rÂ“²‡"Â7‚Â“²‚ÂÂVFvR“°§Ð ¢òò2222222222222222222222222222222222222222222222222222222222220¢òò22%U5TTD2U„”Ä”$U2†æV&W7BÂWF2â¢òò2222222222222222222222222222222222222222222222222222222222220§7FF–2–çBgWDæV&W7D÷WFf–VÆB†–çBFVÒÂfÆöB‚ÂfÆöB’—²òòVÂÖ26W&6æò‡6–â÷'FW&ò¢–çB&W7BÒÓ²fÆöB&BÒS†c°¢f÷"†–çB²Ò²²Â²²²²—°¢–çB’ÒFVÒ¢²³°¢–b†gWE¶•Òç&öÆRÓÒe%ôt²’6öçF–çVS°¢fÆöBG‚ÒgWE¶•Òç‚Ò‚ÂG’ÒgWE¶•Òç’Ò’ÂBÒG‚¢G‚²G’¢G“°¢–b†BÂ&B—²&BÒC²&W7BÒ“²Ð¢Ð¢&WGW&â&W7C°§Ð§7FF–2–çBgWDæV&W7EFô&ÆÂ†–çBFVÒ—²&WGW&âgWDæV&W7D÷WFf–VÆB‡FVÒÂgWD'‚ÂgWD'’“²Ð§7FF–2–çBgWDæV&W7D÷†–çB’—°¢–çB÷Ò†gWE¶•ÒçFVÒÓÒ’ò¢Â&W7BÒÓ²fÆöB&BÒS†c°¢f÷"†–çB²Ò²²Â²²²²—°¢–çB¢Ò÷¢²³²fÆöBBÒgWDF—7C"†’Â¢“°¢–b†BÂ&B—²&BÒC²&W7BÒ£²Ð¢Ð¢&WGW&â&W7C°§Ð ¢òò2222222222222222222222222222222222222222222222222222222222220¢òò224RòD•$òò4Ô$”òòT•DR†66–öæW2¢òò2222222222222222222222222222222222222222222222222222222222220§7FF–2fö–BgWE6WDÆö÷6R†–çBg&öÒÂfÆöBF—'‚ÂfÆöBF—'’ÂfÆöB7VVBÂfÆöBW¢—°¢fÆöBBÒ7'Fb†F—'‚¢F—'‚²F—'’¢F—'’“²–b†BÂãb—²F—'‚ÒgWDf6Uƒ²F—'’ÒgWDf6U“²BÒ²Ð¢gWD'g‚ÒF—'‚òB¢7VVC²gWD'g’ÒF—'’òB¢7VVC²gWD'g¢ÒW£°¢gWD÷væW"ÒÓ²gWDÆ7EF÷V6‚Òg&öÓ²gWEF÷V6„6BÒã#&c²gWD'¢Ò°§Ð¢òò6RFVÂ§VvF÷"‡VÖæò‡6öÆò6’7RWV—òF–VæRVÂ&Æöâ§7FF–2fö–BgWDFõ72‚—°¢–b†gWD÷væW"ÂÇÂgWE¶gWD÷væW%ÒçFVÒÒ’&WGW&ã°¢–çBòÒgWD÷væW"Â&W7BÒÓ²fÆöB&W7E62ÒÓS†c°¢f÷"†–çB²Ò²²Â²²²²—°¢–çB"Ò³²–b‡"ÓÒòÇÂgWE·%Òç&öÆRÓÒe%ôt²’6öçF–çVS°¢fÆöBG‚ÒgWE·%Òç‚ÒgWE¶õÒç‚ÂG’ÒgWE·%Òç’ÒgWE¶õÒç“°¢fÆöBF—7BÒ7'Fb†G‚¢G‚²G’¢G’“°¢–b†F—7BÂ#‚ÇÂF—7Bâ3ƒ’6öçF–çVS°¢fÆöBF÷BÒ†G‚¢gWDf6U‚²G’¢gWDf6U’’òF—7C²òòÆ–æVFò6öâÆF—&V66–öâVçFF¢fÆöBgvBÒ†gWDvöÅ‚ƒ’âgWE¶õÒç‚’ò†G‚âòãFb¢Óã6b’¢°¢fÆöB62ÒF÷B²gvBÒF—7B¢ã&c°¢–b‡62â&W7E62—²&W7E62Ò63²&W7BÒ#²Ð¢Ð¢–b†&W7BÂ’&WGW&ã°¢òò&V6—6–öâFVÂ6S¢W66Æ6öâVÂ&F–ær&÷–òÂ6öâÖ&vVâFRW'&÷"Ô”ä”ÔòƒãÓR’à¢fÆöBW'"Òãb²ƒãbÒgWE&F–ærƒ’’¢ãfc°¢fÆöBÆVBÒã&c°¢fÆöBG‚ÒgWE¶&W7EÒç‚²gWE¶&W7EÒçg‚¢ÆVBÂG’ÒgWE¶&W7EÒç’²gWE¶&W7EÒçg’¢ÆVC°¢fÆöBG‚ÒG‚ÒgWD'‚²gWDW'&b‚’¢W'"¢“ÂG’ÒG’ÒgWD'’²gWDW'&b‚’¢W'"¢“°¢gWE6WDÆö÷6R†òÂG‚ÂG’ÂeUEõ55õ5BÂ“°§Ð¢òòF—&òFVÂ§VvF÷"‡VÖæð§7FF–2fö–BgWDFõ6†ö÷B‚—°¢–b†gWD÷væW"ÂÇÂgWE¶gWD÷væW%ÒçFVÒÒ’&WGW&ã°¢–çBòÒgWD÷væW#°¢fÆöBw‚ÒgWDvöÅ‚ƒ’Âw’ÒeUEôÔ”E“°¢fÆöBG‚Òw‚ÒgWD'‚ÂG’Òw’ÒgWD'“°¢G‚³ÒgWDf6U‚¢c²G’³ÒgWDf6U’¢C²òò6W6v6öâÆòVçFFð¢fÆöBW'"Òãb²ƒãbÒgWE&F–ærƒ’’¢ãFc°¢G’³ÒgWDW'&b‚’¢W'"¢#°¢gWE6WDÆö÷6R†òÂG‚ÂG’ÂeUEõ4„ôõEõ5BÂs²vVõ&æFb‚’¢C“²òò6öâÆvòFRÇGW&¢gWDfÆ6…³ÒÒ°§Ð¢òò6Ö&–"FR§VvF÷"6öçG&öÆFò‡WF–Â7VæFòäòF–VæW2VÂ&Æöâ§7FF–2fö–BgWDFõ7v—F6‚‚—°¢–çB7F'BÒgWD7G&ÂÂ&W7BÒÓ²fÆöB&BÒS†c°¢f÷"†–çB²Ò²²Â²²²²—°¢–çB’Ò³²–b†’ÓÒ7F'BÇÂgWE¶•Òç&öÆRÓÒe%ôt²’6öçF–çVS°¢fÆöBG‚ÒgWE¶•Òç‚ÒgWD'‚ÂG’ÒgWE¶•Òç’ÒgWD'’ÂBÒG‚¢G‚²G’¢G“°¢–b†BÂ&B—²&BÒC²&W7BÒ“²Ð¢Ð¢–b†&W7BãÒ’gWD7G&ÂÒ&W7C°§Ð ¢òò2222222222222222222222222222222222222222222222222222222222220¢òò22”†ÖV–æFRW7FF÷2&&F²6–âF†f–æF–ær¢òò2222222222222222222222222222222222222222222222222222222222220§7FF–2fö–BgWE7FVW"†–çB’ÂfÆöBG‚ÂfÆöBG’ÂfÆöBÖ‡7B—°¢fÆöBG‚ÒG‚ÒgWE¶•Òç‚ÂG’ÒG’ÒgWE¶•Òç’ÂBÒ7'Fb†G‚¢G‚²G’¢G’“°¢–b†BâãVb—²gWE¶•Òçg‚ÒG‚òB¢Ö‡7C²gWE¶•Òçg’ÒG’òB¢Ö‡7C²Ð¢VÇ6R²gWE¶•Òçg‚Ò²gWE¶•Òçg’Ò²Ð§Ð¢òòF—&ò÷6RFRVâ÷'FF÷"6öçG&öÆFò÷"Æ”†WV—òFVÂ÷'FF÷"§7FF–2fö–BgWD•6†ö÷B†–çB’—°¢–çBBÒgWE¶•ÒçFVÓ²fÆöBw‚ÒgWDvöÅ‚‡B“°¢fÆöBG‚Òw‚ÒgWD'‚ÂG’ÒeUEôÔ”E’ÒgWD'“°¢fÆöBW'"Òãb²ƒãbÒgWE&F–ær‡B’’¢ãfc²òòÖ&vVâÔ”ä”ÔòRVâVâF÷ ¢G’³ÒgWDW'&b‚’¢W'"¢3°¢gWE6WDÆö÷6R†’ÂG‚ÂG’ÂeUEõ4„ôõEõ5BÂc²vVõ&æFb‚’¢C“°§Ð§7FF–2fö–BgWD•72†–çB’—°¢–çBBÒgWE¶•ÒçFVÒÂ&W7BÒÓ²fÆöB&W7E62ÒÓS†c°¢f÷"†–çB²Ò²²Â²²²²—°¢–çB"ÒB¢²³²–b‡"ÓÒ’ÇÂgWE·%Òç&öÆRÓÒe%ôt²’6öçF–çVS°¢fÆöBG‚ÒgWE·%Òç‚ÒgWE¶•Òç‚ÂG’ÒgWE·%Òç’ÒgWE¶•Òç’ÂF—7BÒ7'Fb†G‚¢G‚²G’¢G’“°¢–b†F—7BÂ3ÇÂF—7Bâ3C’6öçF–çVS°¢fÆöBF÷v&BÒ†gWDvöÅ‚‡B’ÒgWE¶•Òç‚“²òò&VfW&—"†6–VÂFVP¢fÆöBgvBÒ‚†G‚â’ÓÒ‡F÷v&Bâ’’òãVb¢Óã&c°¢òòVæÆ—¦6’†’Vâ&—fÂ6W&6FVÂ&V6WF÷ ¢–çB÷ÒgWDæV&W7D÷‡"“²fÆöB÷BÒ÷ãÒò7'Fb†gWDF—7C"‡"Â÷’’¢“““°¢fÆöB62ÒgvB²÷B¢ãFbÒF—7B¢ãc°¢–b‡62â&W7E62—²&W7E62Ò63²&W7BÒ#²Ð¢Ð¢–b†&W7BÂ—²gWD•6†ö÷B†’“²&WGW&ã²Ð¢fÆöBW'"Òãb²ƒãbÒgWE&F–ær‡B’’¢ãVc°¢fÆöBG‚ÒgWE¶&W7EÒç‚ÒgWD'‚²gWDW'&b‚’¢W'"¢“°¢fÆöBG’ÒgWE¶&W7EÒç’ÒgWD'’²gWDW'&b‚’¢W'"¢“°¢gWE6WDÆö÷6R†’ÂG‚ÂG’ÂeUEõ55õ5BÂ“°§Ð¢òòFV6—6–öâFVÂ÷'FF÷"”¢&VvFV"òF—&"ò6 §7FF–2fö–BgWD”6''’†–çB’—°¢–çBBÒgWE¶•ÒçFVÓ²fÆöB6¶–ÆÂÒgWE&F–ær‡B“°¢fÆöBw‚ÒgWDvöÅ‚‡B’Âw’ÒeUEôÔ”E“°¢fÆöBFw‚Òw‚ÒgWE¶•Òç‚ÂFw’Òw’ÒgWE¶•Òç’ÂFrÒ7'Fb†Fw‚¢Fw‚²Fw’¢Fw’“°¢–çB÷ÒgWDæV&W7D÷†’“²fÆöBBÒ÷ãÒò7'Fb†gWDF—7C"†’Â÷’’¢“““°¢gWE¶•ÒæF6BÓÒgWDGC°¢–b†gWE¶•ÒæF6BÃÒ—°¢&ööÂ–å&ævRÒ†FrÂeUEõ4„ôõEõ$ätR“°¢–b†–å&ævRbbvVõ&æFb‚’Âƒã#†b²ãSVb¢6¶–ÆÂ’—²gWD•6†ö÷B†’“²gWE¶•ÒæF6BÒãvc²&WGW&ã²Ð¢–b‡BÂ#‚bbvVõ&æFb‚’Âƒã3Vb²ãCVb¢6¶–ÆÂ’—²gWD•72†’“²gWE¶•ÒæF6BÒãfc²&WGW&ã²Ð¢gWE¶•ÒæF6BÒãFb²ƒãbÒ6¶–ÆÂ’¢ã3Fc²òò&V66–öã¢ÖVæ÷2&F–ærÓâ&W–Vç6Ö2ÆVçFð¢Ð¢òò&VvFR†6–÷'FW&–ÂW7V—fæFòÆWfVÖVçFRÆ&W6–öà¢fÆöBG‚Òw‚ÂG’Òw“°¢–b‡BÂCbb÷ãÒ—²G’³Ò†gWE¶•Òç’ÂgWE¶÷Òç’’òÓS¢S²Ð¢gWE7FVW"†’ÂG‚ÂG’ÂeUEô4%%•õ5B¢ƒã–b²ã&b¢6¶–ÆÂ’“°¢gWE¶•Òç7BÒdô4%%“°§Ð§7FF–2fö–BgWDtµF&vWB†–çB’ÂfÆöB¢G‚ÂfÆöB¢G’—°¢–çBBÒgWE¶•ÒçFVÓ²fÆöBöw‚ÒgWD÷vå‚‡B“°¢§G‚Òöw‚²‡BÓÒòC"¢ÓC"“°¢fÆöB’ÒgWD'“²–b‡’ÂeUEôÔ”E’Òs"’’ÒeUEôÔ”E’Òs#²–b‡’âeUEôÔ”E’²s"’’ÒeUEôÔ”E’²s#°¢§G’Ò“°§Ð§7FF–2fö–BgWD’‚—°¢–çB÷72Ò†gWD÷væW"ãÒ’ògWE¶gWD÷væW%ÒçFVÒ¢Ó°¢–b†gWD÷væW"ãÒbbgWE¶gWD÷væW%ÒçFVÒÓÒ’gWD7G&ÂÒgWD÷væW#²òòGRWV—ò6öâ&ÆöâÓâ6öçG&öÆ2Â÷'FF÷ ¢–çB6†6W#ÒgWDæV&W7EFô&ÆÂƒ’Â6†6W#ÒgWDæV&W7EFô&ÆÂƒ“°¢f÷"†–çB’Ò²’ÂeUEôå²’²²—°¢–b†’ÓÒgWD7G&ÂbbgWE¶•ÒçFVÒÓÒ’6öçF–çVS²òòÂ6öçG&öÆFòÆò×VWfRVÂ‡VÖæð¢–çBBÒgWE¶•ÒçFVÒÂ&öÆRÒgWE¶•Òç&öÆS°¢–b‡&öÆRÓÒe%ôt²—²fÆöBG‚ÂG“²gWDtµF&vWB†’ÂgG‚ÂgG’“²gWE7FVW"†’ÂG‚ÂG’ÂeUEôtµõ5B“²gWE¶•Òç7BÒdôt³²6öçF–çVS²Ð¢fÆöB7BÒ‡&öÆRÓÒe%ôetB’ò#b¢‡&öÆRÓÒe%ôÔ”Bò‚¢"“°¢–b‡÷72ÓÒB—°¢–b†gWD÷væW"ÓÒ’—²gWD”6''’†’“²6öçF–çVS²Òòò÷'FF÷"”¢òò÷–ó¢V×V¦†6–VÂFVR’'&RW76–÷0¢fÆöBG‚ÒgWD†öÖU‚†’’¢ãCb²gWD'‚¢ã#†b²gWDvöÅ‚‡B’¢ã3&c°¢fÆöBG’ÒgWD†öÖU’†’’¢ãCVb²gWD'’¢ãSVc°¢gWE7FVW"†’ÂG‚ÂG’Â7B“²gWE¶•Òç7BÒdõ5Uõ%C°¢ÒVÇ6R–b‡÷72ÓÒƒÒB’—°¢–çB6†6W"Ò‡BÓÒ’ò6†6W#¢6†6W#°¢–b†’ÓÒ6†6W"—²gWE7FVW"†’ÂgWD'‚ÂgWD'’Â7B¢ãFb“²gWE¶•Òç7BÒdô4„4S²Ð¢VÇ6R²òòÖ&6¦öæÃ¢÷6–6–öâ&6RFW7Æ¦F†6–VÂ&Æöà¢fÆöBG‚ÒgWD†öÖU‚†’’¢ãc&b²gWD'‚¢ã#Fb²gWD÷vå‚‡B’¢ãFc°¢fÆöBG’ÒgWD†öÖU’†’’¢ãSb²gWD'’¢ãSc°¢gWE7FVW"†’ÂG‚ÂG’Â7B¢ã“fb“²gWE¶•Òç7BÒdôÔ$³°¢Ð¢ÒVÇ6R²òò&Æöâ7VVÇFó¢VÂÖ26W&6æòFR6FWV—òf÷"VÀ¢–çB6†6W"Ò‡BÓÒ’ò6†6W#¢6†6W#°¢–b†’ÓÒ6†6W"—²gWE7FVW"†’ÂgWD'‚ÂgWD'’Â7B¢ã†b“²gWE¶•Òç7BÒdô4„4S²Ð¢VÇ6R²fÆöBG‚ÒgWD†öÖU‚†’’¢ãcfb²gWD'‚¢ã#c²fÆöBG’ÒgWD†öÖU’†’’¢ãSVb²gWD'’¢ãCVc°¢gWE7FVW"†’ÂG‚ÂG’Â7B¢ã–b“²gWE¶•Òç7BÒdô„ôÄC²Ð¢Ð¢Ð§Ð ¢òò2222222222222222222222222222222222222222222222222222222222220¢òò22d•4”4²4ôÄ•4”ôâ‡Vâ7V'7FW’+rvF6†För×6fR‡6–âFVÆ’¢òò2222222222222222222222222222222222222222222222222222222222220§7FF–2–çBgWD&ÆÄ6öçG&öÆÆW"‚—°¢–çB&W7BÒÓ²fÆöB&BÒS†c°¢f÷"†–çB’Ò²’ÂeUEôå²’²²—°¢–b†’ÓÒgWDÆ7EF÷V6‚bbgWEF÷V6„6Bâ’6öçF–çVS²òòVÂ6F÷"æò&V6–&R7R&÷–ò6P¢fÆöB"Ò†gWE¶•Òç&öÆRÓÒe%ôt²’òeUEôtµô5E$Åõ"¢eUEô5E$Åõ#°¢fÆöBG‚ÒgWE¶•Òç‚ÒgWD'‚ÂG’ÒgWE¶•Òç’ÒgWD'’ÂC"ÒG‚¢G‚²G’¢G“°¢–b†C"ÃÒ"¢"bbC"Â&B—²&BÒC#²&W7BÒ“²Ð¢Ð¢&WGW&â&W7C°§Ð§7FF–2fö–BgWDvöÄ¶–6²†–çBFVeFVÒ—²òò6VRFR÷'FW&–¢–çBv²ÒFVeFVÒ¢²°¢gWD'‚ÒgWE¶vµÒçƒ²gWD'’ÒgWE¶vµÒç“²gWD'¢Ò°¢gWD'g‚ÒgWD'g’ÒgWD'g¢Ò²gWD÷væW"Òv³²gWDÆ7EF÷V6‚Òv³²gWEF÷V6„6BÒ°¢–b†FVeFVÒÓÒ’gWD7G&ÂÒv³°§Ð§7FF–2fö–BgWDvöÂ†–çBFVÒ—°¢–b‡FVÒÓÒ’gWE66÷&T²³²VÇ6RgWE66÷&T"²³°¢6ç&–çFb†gWDfÆ6‚Â6—¦Vöb†gWDfÆ6‚’Â$tôÂ"“°¢gWDg&VW¦UVçF–ÂÒÖ–ÆÆ—2‚’²S°¢gWEVæF–ærÒG'VS²gWD¶–6´æW‡BÒÒFVÓ²òò66VÂVRVæ6¦ð¢gWD÷væW"ÒÓ°§Ð§7FF–2fö–BgWE7FW†fÆöBGB—°¢òòÒÒÒV—FS¢&—fÆW26W&6FVÂ÷'FF÷"–çFVçFâ&ö&"ÒÒÐ¢–b†gWD÷væW"ãÒ—°¢–çBòÒgWD÷væW"Â÷BÒgWE¶õÒçFVÓ°¢f÷"†–çB²Ò²²Â²²²²—°¢–çB¢ÒƒÒ÷B’¢²³°¢–b†gWE¶¥Òç&öÆRÓÒe%ôt²’6öçF–çVS°¢–b†gWDF—7C"†òÂ¢’ÃÒeUEõD4´ÄUõ"¢eUEõD4´ÄUõ"—°¢fÆöB6²ÒgWE&F–ær†gWE¶¥ÒçFVÒ“°¢fÆöB&ö"ÒƒãFb²"ã&b¢6²’¢GC²òò÷"6VwVæFòÂW66ÆFò÷"&F–æp¢–b†vVõ&æFb‚’Â&ö"—°¢gWD÷væW"Ò£²gWDÆ7EF÷V6‚Ò£²gWEF÷V6„6BÒãFc°¢–b†gWE¶¥ÒçFVÒÓÒ’gWD7G&ÂÒ£°¢'&V³°¢Ð¢Ð¢Ð¢Ð¢òòÒÒÒ–çFVw&6–öâFR§VvF÷&W2ÒÒÐ¢f÷"†–çB’Ò²’ÂeUEôå²’²²—°¢gWE¶•Òç‚³ÒgWE¶•Òçg‚¢GC²gWE¶•Òç’³ÒgWE¶•Òçg’¢GC°¢–b†gWE¶•Òç‚ÂB’gWE¶•Òç‚ÒC²–b†gWE¶•Òç‚âeUEôÄTâÒB’gWE¶•Òç‚ÒeUEôÄTâÒC°¢–b†gWE¶•Òç’Âb’gWE¶•Òç’Òc²–b†gWE¶•Òç’âeUEõt”BÒb’gWE¶•Òç’ÒeUEõt”BÒc°¢Ð¢–b†gWEF÷V6„6Bâ’gWEF÷V6„6BÓÒGC°¢òòÒÒÒ&ÆöâÒÒÐ¢–b†gWD÷væW"ãÒ—°¢–çBòÒgWD÷væW#²fÆöBfG‚ÂfG“°¢–b†òÓÒgWD7G&ÂbbgWE¶õÒçFVÒÓÒ—²fG‚ÒgWDf6Uƒ²fG’ÒgWDf6U“²Ð¢VÇ6R²fÆöBBÒ7'Fb†gWE¶õÒçg‚¢gWE¶õÒçg‚²gWE¶õÒçg’¢gWE¶õÒçg’“°¢–b†BâB—²fG‚ÒgWE¶õÒçg‚òC²fG’ÒgWE¶õÒçg’òC²Ð¢VÇ6R²fG‚Ò†gWDvöÅ‚†gWE¶õÒçFVÒ’âgWE¶õÒç‚’ò¢Ó²fG’Ò²ÒÐ¢fÆöBfâÒ7'Fb†fG‚¢fG‚²fG’¢fG’“²–b†fâÂãb—²fG‚Ò²fG’Ò²fâÒ²Ð¢gWD'‚ÒgWE¶õÒç‚²fG‚òfâ¢eUEôE$”$$ÄS°¢gWD'’ÒgWE¶õÒç’²fG’òfâ¢eUEôE$”$$ÄS°¢òòÖçFVæW"VÂ&ÆöâFVçG&òFVÂ6×òVçVRVÂ÷'FF÷"W7FRVvFòVæ¢òòÆ–æV¢6’æòÂVâF—&ò6Æ7VÆFòFW6FRVæ‚gVW&FR&ævò6ÆG&––çfW'F–Fòà¢–b†gWD'‚Â"’gWD'‚Ò#²–b†gWD'‚âeUEôÄTâÒ"’gWD'‚ÒeUEôÄTâÒ#°¢–b†gWD'’Â"’gWD'’Ò#²–b†gWD'’âeUEõt”BÒ"’gWD'’ÒeUEõt”BÒ#°¢gWD'¢Ò²gWD'g‚ÒgWD'g’ÒgWD'g¢Ò°¢ÒVÇ6R°¢gWD'‚³ÒgWD'g‚¢GC²gWD'’³ÒgWD'g’¢GC°¢fÆöBg"ÒãbÒeUEô$ÄÅôe$”2¢GC²–b†g"Â’g"Ò°¢gWD'g‚£Òg#²gWD'g’£Òg#°¢–b†gWD'¢âÇÂgWD'g¢Ò—°¢gWD'¢³ÒgWD'g¢¢GC²gWD'g¢ÓÒeUEô$ÄÅôr¢GC°¢–b†gWD'¢ÃÒ—²gWD'¢Ò²–b†gWD'g¢Â—²gWD'g¢ÒÖgWD'g¢¢ãC&c²–b†gWD'g¢Â#B’gWD'g¢Ò²ÒÐ¢Ð¢–b†gWD'’ÂB—²gWD'’ÒC²gWD'g’ÒÖgWD'g’¢ãVc²Òòò&V&÷FRVâÆ2&æF0¢–b†gWD'’âeUEõt”BÒB—²gWD'’ÒeUEõt”BÒC²gWD'g’ÒÖgWD'g’¢ãVc²Ð¢òòÒÒÒÆ–æVFRvöÂò÷'FW&–ÒÒÐ¢&ööÂ–äÖ÷WF‚Ò†gWD'’âeUEôÔ”E’ÒeUEôt„ÄbbbgWD'’ÂeUEôÔ”E’²eUEôt„ÄbbbgWD'¢ÂCb“°¢–b†gWD'‚ãÒeUEôÄTâÒ"—°¢–b†–äÖ÷WF‚—²gWDvöÂƒ“²&WGW&ã²ÒVÇ6R²gWDvöÄ¶–6²ƒ“²&WGW&ã²Ð¢ÒVÇ6R–b†gWD'‚ÃÒ"—°¢–b†–äÖ÷WF‚—²gWDvöÂƒ“²&WGW&ã²ÒVÇ6R²gWDvöÄ¶–6²ƒ“²&WGW&ã²Ð¢Ð¢–çBrÒgWD&ÆÄ6öçG&öÆÆW"‚“°¢–b†rãÒ—²gWD÷væW"Òs²gWD'g‚ÒgWD'g’ÒgWD'g¢Ò²gWD'¢Ò²–b†gWE¶uÒçFVÒÓÒ’gWD7G&ÂÒs²Ð¢Ð§Ð ¢òòÒÒÒÒ6öÆö6"Æf÷&Ö6–öâ‡6VR’â¶–6µFVÒöæRVâ§VvF÷"Â6VçG&òâÒÒÒÐ§7FF–2fö–BgWE&W6WDf÷&ÖF–öâ†–çB¶–6µFVÒ—°¢f÷"†–çBBÒ²BÂ#²B²²—°¢f÷"†–çB²Ò²²Â²²²²—°¢–çB’ÒB¢²³°¢fÆöB‚ÒeUEôdõ$Õõ…¶µÒÂ’ÒeUEôdõ$Õõ•¶µÓ°¢–b‡BÓÒ’‚ÒeUEôÄTâÒƒ°¢–b‡BÓÒbb‚âcƒ‚’‚Òcƒƒ²òòVâVÂ6VRÂ6FWV—òVâ7R6×ð¢–b‡BÓÒbb‚Âs"’‚Òs#°¢gWE¶•Òç‚Òƒ²gWE¶•Òç’Ò“²gWE¶•Òçg‚Ò²gWE¶•Òçg’Ò°¢gWE¶•ÒçFVÒÒ‡V–çC…÷B—C²gWE¶•Òç&öÆRÒeUEôdõ$Õõ%¶µÓ²gWE¶•Òç7BÒdô„ôÄC²gWE¶•ÒæF6BÒ°¢Ð¢Ð¢–çB¶–6²Ò¶–6µFVÒ¢²c²òòVÂÖVF–ö6VçG&ò66¢gWE¶¶–6µÒç‚Ò†¶–6µFVÒÓÒ’òcƒb¢sC²gWE¶¶–6µÒç’ÒeUEôÔ”E“°¢gWD'‚Òs²gWD'’ÒeUEôÔ”E“²gWD'¢Ò²gWD'g‚ÒgWD'g’ÒgWD'g¢Ò°¢gWD÷væW"ÒÓ²gWDÆ7EF÷V6‚ÒÓ²gWEF÷V6„6BÒ²gWD6ÒÒs°¢gWDf6U‚Ò†¶–6µFVÒÓÒ’ò¢Ó²gWDf6U’Ò°¢gWD7G&ÂÒgWDæV&W7EFô&ÆÂƒ“°§Ð ¢òò2222222222222222222222222222222222222222222222222222222222220¢òò22D”%T¤òDTÂ4Õò†6ö÷&G2Äôt”42ƒƒCƒ²W67&–&RVâ&'Vb¢òò2222222222222222222222222222222222222222222222222222222222220§7FF–2fö–BgWDG&tÖ&¶–æw2‚—°¢V–çCe÷BÆâÒ&v#ScRƒ#3"Ã#CÃ#3b“°¢–çB3‚ÒgWDu‚ƒÃ’Â3’ÒgWDu’ƒ“°¢–çB3‚ÒgWDu‚„eUEôÄTâÃ’Â3’ÒgWDu’ƒ“°¢–çB3‚ÒgWDu‚ƒÄeUEõt”B’Â3’ÒgWDu’„eUEõt”B“°¢–çB3‚ÒgWDu‚„eUEôÄTâÄeUEõt”B’Â3’ÒgWDu’„eUEõt”B“°¢7G&ö¶U6Vr†3‚Â3’Â3‚Â3’ÂÂÆâ“²òòF÷V6†Æ–æRÆV¦æ¢7G&ö¶U6Vr†3‚Â3’Â3‚Â3’ÂÂÆâ“²òòF÷V6†Æ–æR6W&6æ¢7G&ö¶U6Vr†3‚Â3’Â3‚Â3’ÂÂÆâ“²òòföæFò—§†Æ–æVFRvöÂ¢7G&ö¶U6Vr†3‚Â3’Â3‚Â3’ÂÂÆâ“²òòföæFòFW ¢òòÆ–æVFRÖVF–ò6×ò²6—&7VÆò6VçG&À¢–çBÖ–BÒeUEôÄTâò#°¢7G&ö¶U6Vr†gWDu‚†Ö–BÃ’ÂgWDu’ƒ’ÂgWDu‚†Ö–BÄeUEõt”B’ÂgWDu’„eUEõt”B’ÂÂÆâ“°¢–çB67‚ÒgWDu‚†Ö–BÂeUEôÔ”E’’Â67’ÒgWDu’„eUEôÔ”E’“°¢–çB7'‚Ò†gWDu‚†Ö–B²sÂeUEôÔ”E’’ÒgWDu‚†Ö–BÒsÂeUEôÔ”E’’’ò#°¢–çB7'’Ò†gWDu’„eUEôÔ”E’²C"’ÒgWDu’„eUEôÔ”E’ÒC"’’ò#°¢gWDVÆÆ—6R†67‚Â67’Â7'‚Â7'’ÂÆâ“°¢f–ÆÄ6—&6ÆR†67‚Â67’Â"ÂÆâ“°¢òò&V2‡VæVâ6F÷'FW&–“¢G&W2Æ–æV2FVÂ&V7FæwVÆð¢f÷"†–çB2Ò²2Â#²2²²—°¢fÆöB'‚Ò‡2ÓÒ’ò¢„eUEôÄTâÒc“°¢fÆöB'ƒ"Ò‡2ÓÒ’òc¢eUEôÄTã°¢fÆöBVFvRÒ‡2ÓÒ’ò'ƒ"¢'ƒ²òòÆ–æVfW'F–6Â–çFW&–÷"FVÂ&V¢7G&ö¶U6Vr†gWDu‚†VFvRÂeUEôÔ”E’Ò’ÂgWDu’„eUEôÔ”E’Ò’À¢gWDu‚†VFvRÂeUEôÔ”E’²’ÂgWDu’„eUEôÔ”E’²’ÂÂÆâ“°¢7G&ö¶U6Vr†gWDu‚†'‚ÂeUEôÔ”E’Ò’ÂgWDu’„eUEôÔ”E’Ò’À¢gWDu‚†'ƒ"ÂeUEôÔ”E’Ò’ÂgWDu’„eUEôÔ”E’Ò’ÂÂÆâ“°¢7G&ö¶U6Vr†gWDu‚†'‚ÂeUEôÔ”E’²’ÂgWDu’„eUEôÔ”E’²’À¢gWDu‚†'ƒ"ÂeUEôÔ”E’²’ÂgWDu’„eUEôÔ”E’²’ÂÂÆâ“°¢Ð¢òò÷'FW&–2‡÷7FW2²Æ&wVW&ò²&VB6–×ÆR¢f÷"†–çB2Ò²2Â#²2²²—°¢fÆöBw‚Ò‡2ÓÒ’ò¢eUEôÄTã°¢–çBBÒgWDD–G‚„eUEôÔ”E’“²–çBv‚ÒgWE‡E¶EÒ²ƒ²òòÇFòVâçFÆÆ¢–çB‚ÒgWDu‚†w‚ÂeUEôÔ”E’ÒeUEôt„Äb’Â’ÒgWDu’„eUEôÔ”E’ÒeUEôt„Äb“°¢–çB‚ÒgWDu‚†w‚ÂeUEôÔ”E’²eUEôt„Äb’Â’ÒgWDu’„eUEôÔ”E’²eUEôt„Äb“°¢V–çCe÷BæWBÒ&v#ScRƒ##Ã##bÃ#3“°¢òò&V@¢f÷"†–çBâÒ²âÂS²â²²—°¢–çB—“Ò’Òv‚¢âòRÂ—“Ò’Òv‚¢âòS°¢7G&ö¶U6Vr‡‚Â—“Â‚Â—“ÂÂÖ—ƒScR†æWBÂ&v#ScRƒcÃÃs’ÂS’“°¢Ð¢7G&ö¶U6Vr‡‚Â’Â‚Â’Òv‚ÂÂ&v#ScRƒ#CRÃ#CRÃ#CR’“²òò÷7FR¢7G&ö¶U6Vr‡‚Â’Â‚Â’Òv‚ÂÂ&v#ScRƒ#CRÃ#CRÃ#CR’“²òò÷7FR ¢7G&ö¶U6Vr‡‚Â’Òv‚Â‚Â’Òv‚ÂÂ&v#ScRƒ#CRÃ#CRÃ#CR’“²òòÆ&wVW&ð¢Ð§Ð§7FF–2fö–BgWDG&tf–VÆB‚—°¢òò6–VÆò²G&–'Væ6ö'&RVÂ†÷&—¦öçFP¢vVôf–ÆÄÂ†&'VbÂÂÂÅrÂeUEô„õ$•¤ôâÂ&v#ScRƒS‚Ãc‚ÃB’“°¢vVôf–ÆÄÂ†&'VbÂÂeUEô„õ$•¤ôâÒ3ÂÅrÂ3Â&v#ScRƒ3‚ÃC"ÃS‚’“°¢–çB6öfbÒ‚†–çB–gWD6Ò’Rc²–b‡6öfbÂ’6öfb³Òc°¢f÷"†–çB‚Ò×6öfc²‚ÂÅs²‚³Òb’òòV&Æ–6ò†Ö÷F2¢vVôf–ÆÄÂ†&'VbÂ‚²2ÂeUEô„õ$•¤ôâÒ#bÂbÂ#Â&v#ScRƒsÃs‚ÃB’“°¢òò6W7VB&6P¢vVôf–ÆÄÂ†&'VbÂÂeUEô„õ$•¤ôâÂÅrÂÄ‚ÒeUEô„õ$•¤ôâÂ&v#ScRƒ3‚ÃSÃSB’“°¢òòg&æ¦2FR6÷'FFò†æ6ÆF2Â×VæFòÓâ†6Vâ67&öÆÂ6öâÆ6Ö&¢–çB7rÒCƒ°¢–çBãÒ‚‚†–çB–gWD6ÒÒÅrò"’ò7r’Ò#°¢f÷"†–çBâÒã²²â²²—°¢–çB7ƒÒeUEô5‚²†â¢7rÒ†–çB–gWD6Ò“°¢–b‡7ƒâÅr’'&V³°¢–çBÒ7ƒÂò¢7ƒÂ"Ò7ƒ²7s²–b†"âÅr’"ÒÅs°¢–b†"â—²V–çCe÷BrÒ†âb’ò&v#ScRƒCBÃcBÃc’¢&v#ScRƒ32Ã3‚ÃC‚“°¢vVôf–ÆÄÂ†&'VbÂÂeUEô„õ$•¤ôâÂ"ÒÂÄ‚ÒeUEô„õ$•¤ôâÂr“²Ð¢Ð¢gWDG&tÖ&¶–æw2‚“°§Ð ¢òòÒÒÒÒVâ§VvF÷"‡÷"–æF–6S²6–â6"VÂ7G'V7B6öÖò&ÖWG&ò’ÒÒÒÐ§7FF–2fö–BgWDG&uÆ–W"†–çB’—°¢–çBBÒgWDD–G‚†gWE¶•Òç’“°¢–çB7‚ÒgWDu‚†gWE¶•Òç‚ÂgWE¶•Òç’“°¢–çBg’ÒgWE7”ÅUE¶EÒÂ‚ÒgWE‡E¶EÓ°¢–b‡7‚ÂÓ3ÇÂ7‚âÅr²3’&WGW&ã°¢–çBF–BÒgWEFVÕ6VÅ¶gWE¶•ÒçFVÕÓ°¢V–çCe÷B6öÄÒeUEõDTÕ5·F–EÒæ6öÆ÷$Â6öÄ"ÒeUEõDTÕ5·F–EÒæ6öÆ÷$#°¢–b†gWE¶•Òç&öÆRÓÒe%ôt²—²6öÄÒ&v#ScRƒCÃ##Ã3“²6öÄ"Ò&v#ScRƒ‚ÃCÃ3“²Òòò÷'FW&òF—7F–çFð¢gWE6†F÷r‡7‚Âg’²Â‚¢C"ò²2Â‚¢Rò²"Â&v#ScRƒ"Ã3"ÃB’Â“R“°¢–çBÆVt‚Ò‚¢3òÂ&öG”‚Ò‚¢3òÂ6†÷'D‚Ò‚¢#"ò°¢–çB'rÒ‚¢Cbò²–b†'rÂB’'rÒC°¢–çBÆVurÒ‚¢"ò²–b†ÆVurÂ’ÆVurÒ°¢–çB†VE"Ò‚¢bò²–b††VE"Â"’†VE"Ò#°¢–çBF÷&öG’Òg’ÒÆVt‚Ò6†÷'D‚Ò&öG”ƒ°¢f–ÆÅ&V7B‡7‚ÒÆVurÒÂg’ÒÆVt‚ÂÆVurÂÆVt‚Â&v#ScRƒ#‚Ã3ÃC’“²òò–W&æ0¢f–ÆÅ&V7B‡7‚²Âg’ÒÆVt‚ÂÆVurÂÆVt‚Â&v#ScRƒ#‚Ã3ÃC’“°¢f–ÆÅ&V7B‡7‚Ò'rò"Âg’ÒÆVt‚Ò6†÷'D‚Â'rÂ6†÷'D‚Â6öÄ"“²òò6†÷'@¢f–ÆÅ&V7B‡7‚Ò'rò"ÂF÷&öG’Â'rÂ&öG”‚Â6öÄ“²òò6Ö—6WF¢G&u&V7B‡7‚Ò'rò"ÂF÷&öG’Â'rÂ&öG”‚ÂÖ—ƒScR†6öÄÂ&v#ScRƒÃÃ’Â“’“°¢f–ÆÄ6—&6ÆR‡7‚ÂF÷&öG’Ò†VE"²Â†VE"Â&v#ScRƒ##bÃƒ"ÃC"’“²òò6&W¦¢–b†’ÓÒgWD7G&ÂbbgWE¶•ÒçFVÒÓÒ—²òò–æF–6F÷"#U"†fÆV6†¢–çB’ÒF÷&öG’Ò†VE"¢"ÒbÒ‚†Ö–ÆÆ—2‚’ò3’R"’¢3°¢f–ÆÅG&–ævÆR‡7‚ÒrÂ’Ò‚Â7‚²rÂ’Ò‚Â7‚Â’Â&v#ScRƒ#CRÃcÃc’“°¢G&uFW‡D2‡7‚Â’Ò#Â#U"ÂÂ&v#ScRƒ#CRÃ#CÃ#’“°¢Ð§Ð§7FF–2fö–BgWDG&t&ÆÂ‚—°¢–çBBÒgWDD–G‚†gWD'’“°¢–çB7‚ÒgWDu‚†gWD'‚ÂgWD'’’Âg’ÒgWE7”ÅUE¶EÓ°¢–çB"ÒgWE‡E¶EÒ¢2ò²–b‡"Â"’"Ò#°¢–çB¦öfbÒ†–çB’†gWD'¢¢†gWE‡E¶EÒòC"ãb’“°¢gWE6†F÷r‡7‚Âg’²Â"²Â"ò"²Â&v#ScRƒ"Ã3"ÃB’Âƒ“°¢f–ÆÄ6—&6ÆR‡7‚Âg’Ò"Ò¦öfbÂ"Â&v#ScRƒ#CRÃ#CRÃ#C‚’“°¢G&t6—&6ÆR‡7‚Âg’Ò"Ò¦öfbÂ"Â&v#ScRƒcÃcBÃs’“°§Ð ¢òòÒÒÒÒ…TB&6FS¢Ö&6F÷"'&–&ÂF–V×òÂ6VçG&òÂæöÖ'&W2ÒÒÒÐ§7FF–2fö–BgWDf×D6Æö6²†6†"¢÷WBÂ–çBâ—°¢–çBw2Ò†–çB–gWEÆ•6V3²–b†w2âSC’w2ÒSC°¢6ç&–çFb†÷WBÂâÂ"S&C¢S&B"Âw2òcÂw2Rc“°§Ð§7FF–2fö–BgWDG&t…TB‚—°¢f–ÆÅ&V7DƒÂÂÅrÂCbÂ&v#ScRƒ"ÃBÃ#b’Â#B“°¢„Æ–æTƒÂCbÂÅrÂ&v#ScRƒÃÃ’Â#“°¢òò&÷Föâ6Æ—"òW6†W7V–æ7WW&–÷"—§V–W&F¢f–ÆÅ&÷VæE&V7BƒbÂbÂ3BÂ#"ÂbÂ&v#ScRƒs‚ÃƒbÃ#b’“°¢f–ÆÅ&V7BƒRÂÂBÂBÂ&v#ScRƒ#CÃ#C"Ã#C‚’“²f–ÆÅ&V7Bƒ#2ÂÂBÂBÂ&v#ScRƒ#CÃ#C"Ã#C‚’“°¢–çB–DÒgWEFVÕ6VÅ³ÒÂ–D"ÒgWEFVÕ6VÅ³Ó°¢V–çCe÷BvöÆBÒ&v#ScRƒ#SÃ#BÃ“’ÂG‡BÒ&v#ScRƒ#3‚Ã#C"Ã#S“°¢òò—§V–W&F¢W67VFò²æöÖ'&R²Ö&6F÷"¢gWD7&W7Bƒc"Â#BÂ#BÂ#‚ÂeUEõDTÕ5¶–DÒæ6öÆ÷$ÂeUEõDTÕ5¶–DÒæ6öÆ÷$"“°¢G&uFW‡BƒƒÂbÂeUEõDTÕ5¶–DÒææöÖ'&RÂÂG‡B“°¢6†"5³…Ó²6ç&–çFb‡2Â6—¦Vöb‡2’Â"VB"ÂgWE66÷&T“²G&uFW‡BƒƒÂ‚Â2Â2ÂvöÆB“°¢òòFW&V6†¢W67VFò²æöÖ'&R²Ö&6F÷" ¢gWD7&W7B„ÅrÒc"Â#BÂ#BÂ#‚ÂeUEõDTÕ5¶–D%Òæ6öÆ÷$ÂeUEõDTÕ5¶–D%Òæ6öÆ÷$"“°¢G&uFW‡E"„ÅrÒƒÂbÂeUEõDTÕ5¶–D%ÒææöÖ'&RÂÂG‡B“°¢6ç&–çFb‡2Â6—¦Vöb‡2’Â"VB"ÂgWE66÷&T"“²G&uFW‡E"„ÅrÒƒÂ‚Â2Â2ÂvöÆB“°¢òò6VçG&ó¢7&öæöÖWG&ò‡F–V×òFR§VVvòrâã“r¢6†"FÕ³…Ó²gWDf×D6Æö6²‡FÒÂ6—¦Vöb‡FÒ’“°¢G&uFW‡D2„Årò"ÂbÂFÒÂ2ÂvöÆB“°§Ð ¢òòÒÒÒÒ6öçG&öÆW2f—'GVÆW2‡7F–6²²&÷FöæW2’ÂW7F–ÆòFVÂ6—7FVÖÒÒÒÐ§7FF–2fö–BgWDG&t'Fâ†–çB–BÂ6öç7B6†"¢Æ&VÂÂ&ööÂ7F—fRÂV–çCe÷B6öÂ—°¢–çB7‚ÒeUEô%Då…¶–EÒÂ7’ÒeUEô%Då•¶–EÓ°¢f–ÆÄ6—&6ÆT†7‚Â7’ÂeUEô%Dåõ"Â6öÂÂ7F—fRò#3b¢S“°¢–b‡V”vÆ72’f–ÆÄ6—&6ÆT†7‚Â7’ÒeUEô%Dåõ"ò2ÂeUEô%Dåõ"ò"Â&v#ScRƒ#SRÃ#SRÃ#SR’ÂCb“²òò'&–ÆÆòÆ—V–BvÆ70¢G&t6—&6ÆR†7‚Â7’ÂeUEô%Dåõ"ÂÖ—ƒScR†6öÂÂ&v#ScRƒ#SRÃ#SRÃ#SR’Â#’“°¢G&uFW‡D2†7‚Â7’ÒbÂÆ&VÂÂÂ&v#ScRƒ#SRÃ#SRÃ#SR’“°§Ð§7FF–2fö–BgWDG&t6öçG&öÇ2‚—°¢òò7F–6°¢f–ÆÄ6—&6ÆT„eUEõ5Dµô5‚ÂeUEõ5Dµô5’ÂeUEõ5Dµõ"Â&v#ScRƒ#Ã#BÃC"’Â#“°¢G&t6—&6ÆR„eUEõ5Dµô5‚ÂeUEõ5Dµô5’ÂeUEõ5Dµõ"Â&v#ScRƒSÃc‚Ã#b’“°¢–b‡V”vÆ72’f–ÆÄ6—&6ÆT„eUEõ5Dµô5‚ÂeUEõ5Dµô5’ÒeUEõ5Dµõ"ò2ÂeUEõ5Dµõ"ò"Â&v#ScRƒ#SRÃ#SRÃ#SR’Â3“°¢–çB·‚ÒeUEõ5Dµô5‚²gWE7F´¶æö%‚Â·’ÒeUEõ5Dµô5’²gWE7F´¶æö%“°¢f–ÆÄ6—&6ÆT†·‚Â·’Â#bÂ&v#ScRƒƒBÃ#BÃ#CB’Â#“°¢G&t6—&6ÆR†·‚Â·’Â#bÂ&v#ScRƒ#3"Ã#CÃ#SR’“°¢òò&÷FöæW0¢gWDG&t'Fâ„d%Eõ5"Â%5""ÂgWE7&–çBÂ&v#ScRƒ“Ã#Ã#’“°¢gWDG&t'Fâ„d%Eõ2Â%2"ÂfÇ6RÂ&v#ScRƒ“ÃSÃ#C’“°¢gWDG&t'Fâ„d%EõD•"Â%D•""ÂfÇ6RÂ&v#ScRƒ#CÃ“"Ã“"’“°¢gWDG&t'Fâ„d%Eô4ÒÂ$4Ò"ÂfÇ6RÂ&v#ScRƒ#CÃ#ÃƒB’“°§Ð ¢òòÒÒÒÒ6ö×÷6–6–öâFRVâg&ÖRFR%D”Dò†Vâ&'Vb’²föÆ6FòÒÒÒÐ§7FF–2fö–BgWE&VæFW%Æ’‚—°¢tÆæBÒG'VS²6WD'Vb†&'Vb“°¢t6Æ—ƒÒ²t6Æ—ƒÒ45%õrÒ²t6Æ—“Ò²t6Æ—“Ò45%ô‚Ò°¢gWDG&tf–VÆB‚“°¢òò÷&FVâ÷"&ögVæF–FB‡w’66VæFVçFRÒÆV¦÷2&–ÖW&ò’&VRÆ÷0¢òò6W&6æ÷2FVâÆ÷2ÆV¦æ÷2â–ç6W&6–öâ6ö'&R#"–æF–6W3¢&&Fòà¢–çB÷&FW%´eUEôåÓ°¢f÷"†–çB’Ò²’ÂeUEôå²’²²’÷&FW%¶•ÒÒ“°¢f÷"†–çBÒ²ÂeUEôå²²²—°¢–çBbÒ÷&FW%¶Ó²fÆöBg’ÒgWE·eÒç“²–çB"ÒÒ°¢v†–ÆR†"ãÒbbgWE¶÷&FW%¶%ÕÒç’âg’—²÷&FW%¶"²ÒÒ÷&FW%¶%Ó²"ÒÓ²Ð¢÷&FW%¶"²ÒÒc°¢Ð¢&ööÂ&ÆÄG&vâÒfÇ6S°¢f÷"†–çBÒ²ÂeUEôå²²²—°¢–b‚&ÆÄG&vâbbgWD'’ÃÒgWE¶÷&FW%¶ÕÒç’—²gWDG&t&ÆÂ‚“²&ÆÄG&vâÒG'VS²Ð¢gWDG&uÆ–W"†÷&FW%¶Ò“°¢Ð¢–b‚&ÆÄG&vâ’gWDG&t&ÆÂ‚“°¢gWDG&t…TB‚“°¢gWDG&t6öçG&öÇ2‚“°¢–b†Ö–ÆÆ—2‚’ÂgWDg&VW¦UVçF–ÂbbgWDfÆ6…³Ò—°¢G&uFW‡D2„Årò"ÂeUEô„õ$•¤ôâ²3ÂgWDfÆ6‚ÂbÂ&v#ScRƒ#SÃ#3Ã“’“°¢–çB62Ò†gWD¶–6´æW‡BÓÒ’ò¢²òòÖ&6òV–Vâ6&FRÖ&6 ¢G&uFW‡D2„Årò"ÂeUEô„õ$•¤ôâ²“"ÂeUEõDTÕ5¶gWEFVÕ6VÅ·65ÕÒææöÖ'&RÂ2Â&v#ScRƒ#CÃ#CBÃ#S"’“°¢Ð¢&W6VçBƒÂ45%ô‚Ò“°§Ð ¢òòÒÒÒÒ&÷F÷F—÷2FVÆçFF÷2„gWF&öÂò§VVv÷2òvVòF6‚’ÒÒÒÐ¢òòW7F26–æ6ògVæ6–öæW26RU4âVæ2Æ–æV2Ö2'&–&FRFöæFR6RDTd”äTâà¢òòVâVÂ”DRFR&GV–æòW6ò7VVÆ÷'VR7Fw2WFövVæW&Æ÷2&÷F÷F—÷2À¢òò&–æ6—–òFVÂ6¶WF6ƒ²FV6Æ&&Æ÷2V’FRf÷&ÖW‡Æ–6—F†6RVRVÀ¢òò&6†—fò6ö×–ÆRFÖ&–Vâ6–âW6R6ò†&GV–æòÖ6Æ’6–â7Fw2ÂÆFf÷&Ô”òÀ¢òòò6’ÆwVæfW¢6R'FRVÂ&÷–V7FòVâf&–÷2æ7’âæò6Ö&–VÀ¢òò6ö×÷'FÖ–VçFó¢6öÆòVÆ–Ö–æVæFWVæFVæ6–ö7VÇFFVÂ&W&ö6W6Fòà§7FF–2fö–BgWE&VæFW%FVÕ6VÂ†&ööÂ÷“°§7FF–2fö–BgWE&VæFW$VæB‚“°§7FF–2fö–BgWE6fUFVÒ†–çB–G‚“°§7FF–2fö–BvÖW5&VæFW$ÖVçR‚“°§7FF–2fö–BvVôVçFW%6VÆV7B‚“° ¢òòÒÒÒÒÆV7GW&FVÂ7F–6²²66–öæW2‡Vâ6öÆòFVFò’ÒÒÒÐ§7FF–2fö–BgWE&VE7F–6²†–çBÇ‚Â–çBÇ’—°¢&ööÂ7F–6²ÒBæF÷vâbbÇ‚Â3ƒbbÇ’â“°¢–b‡7F–6²—°¢fÆöB÷‚ÒÇ‚ÒeUEõ5Dµô5‚Â÷’ÒÇ’ÒeUEõ5Dµô5’ÂBÒ7'Fb†÷‚¢÷‚²÷’¢÷’“°¢–b†BâeUEõ5DµôDTB—°¢fÆöBFBÒ†BâeUEõ5Dµõ"’òeUEõ5Dµõ"¢C°¢fÆöB×‚Ò÷‚òBÂ×’Ò÷’òC°¢fÆöBÖrÒ†FBÒeUEõ5DµôDTB’ò„eUEõ5Dµõ"ÒeUEõ5DµôDTB“°¢gWDÖ÷fU‚Ò×‚¢Ös²gWDÖ÷fU’Ò×’¢Ös°¢gWDf6U‚Ò×ƒ²gWDf6U’Ò×“°¢gWE7F´¶æö%‚Ò†–çB’†×‚¢FB“²gWE7F´¶æö%’Ò†–çB’†×’¢FB“°¢&WGW&ã°¢Ð¢Ð¢gWDÖ÷fU‚ÒgWDÖ÷fU’Ò²gWE7F´¶æö%‚Ò²gWE7F´¶æö%’Ò°§Ð ¢òòÒÒÒÒF–6²FVÂ'F–Fó¢F‡&÷GFÆR²f—6–66öâ7V'7FW²&VæFW"ÒÒÒÐ§7FF–2fö–BgWEÆ•F–6²‚—°¢V–çC3%÷Bæ÷rÒÖ–ÆÆ—2‚“°¢–çBÇ‚ÒvVôÅ‚‚’ÂÇ’ÒvVôÅ’‚“°¢òò6Æ—"Æ6VÆV66–öâFRWV—òÒÒVâDôDõ2Æ÷2F–6·2†çFW2FVÂF‡&÷GFÆR’à¢–b…BçFbbÇ‚ÂCbbbÇ’Â3—²gWE67&VVâÒe5õDTÓ²gWE&VæFW%FVÕ6VÂ†fÇ6R“²&WGW&ã²Ð¢&ööÂg&÷¦VâÒ†æ÷rÂgWDg&VW¦UVçF–Â“°¢òòÆF6†VòFRF2FR&÷Föâ‡G&ç6—F÷&–÷2’çFW2FVÂF‡&÷GFÆRÂ&æòW&FW&Æ÷2à¢–b…BçFbbg&÷¦Vâ—°¢f÷"†–çB"Ò²"ÂC²"²²—°¢–çBG‚ÒÇ‚ÒeUEô%Då…¶%ÒÂG’ÒÇ’ÒeUEô%Då•¶%Ó°¢–b†G‚¢G‚²G’¢G’ÃÒ„eUEô%Dåõ"²r’¢„eUEô%Dåõ"²r’—°¢–b†"ÓÒd%Eõ5"’gWE&W7&–çBÒG'VS²VÇ6R–b†"ÓÒd%Eõ2’gWE&W72ÒG'VS°¢VÇ6R–b†"ÓÒd%EõD•"’gWE&W6†ö÷BÒG'VS²VÇ6R–b†"ÓÒd%Eô4Ò’gWE&W7v—F6‚ÒG'VS°¢'&V³°¢Ð¢Ð¢Ð¢–b†æ÷rÒgWDg&ÖT×2ÂtTõôe$ÔUôÕ2’&WGW&ã²òòã3g0¢fÆöBGBÒ†æ÷rÒgWDg&ÖT×2’òãc²gWDg&ÖT×2Òæ÷s°¢–b†GBâtTõôEDÔ‚’GBÒtTõôEDÔƒ°¢gWDGBÒGC°¢–b‚g&÷¦VâbbgWEVæF–ærbbæ÷rãÒgWDg&VW¦UVçF–Â—²gWEVæF–ærÒfÇ6S²gWE&W6WDf÷&ÖF–öâ†gWD¶–6´æW‡B“²gWDfÆ6…³ÒÒ²Ð¢–b‚g&÷¦Vâ—°¢gWE&VE7F–6²†Ç‚ÂÇ’“°¢–b†gWE&W7&–çB—²gWE7&–çBÒgWE7&–çC²gWE&W7&–çBÒfÇ6S²Ð¢–b†gWE&W72—²gWDFõ72‚“²gWE&W72ÒfÇ6S²Ð¢–b†gWE&W6†ö÷B—²gWDFõ6†ö÷B‚“²gWE&W6†ö÷BÒfÇ6S²Ð¢–b†gWE&W7v—F6‚—²gWDFõ7v—F6‚‚“²gWE&W7v—F6‚ÒfÇ6S²Ð¢òòfVÆö6–FBFVÂ§VvF÷"6öçG&öÆFòFW6FRVÂ7F–6°¢fÆöB7BÒeUEô$4Uõ5B¢†gWE7&–çBòeUEõ5$”åEôÕTÂ¢ãb“°¢gWE¶gWD7G&ÅÒçg‚ÒgWDÖ÷fU‚¢7C²gWE¶gWD7G&ÅÒçg’ÒgWDÖ÷fU’¢7C°¢gWD’‚“°¢fÆöB&VÖ–âÒGC°¢v†–ÆR‡&VÖ–ââãb—²fÆöB2Ò‡&VÖ–ââtTõõ5T%5DU’òtTõõ5T%5DU¢&VÖ–ã²gWE7FW‡2“²&VÖ–âÓÒ3²Ð¢gWD6Ò³Ò†gWD'‚ÒgWD6Ò’¢†GB¢2ãbâò¢GB¢2ãb“°¢–b†gWD6ÒÂeUEô5‚’gWD6ÒÒeUEô5ƒ²–b†gWD6ÒâeUEôÄTâÒeUEô5‚’gWD6ÒÒeUEôÄTâÒeUEô5ƒ°¢gWEÆ•6V2³ÒGB¢ƒSCãbòeUEôÔD4…õ4T2“°¢–b†gWEÆ•6V2ãÒSC—²gWEÆ•6V2ÒSC²gWE67&VVâÒe5ôTäC²gWE&VæFW$VæB‚“²&WGW&ã²Ð¢ÒVÇ6R°¢gWDÖ÷fU‚ÒgWDÖ÷fU’Ò²gWE7F´¶æö%‚ÒgWE7F´¶æö%’Ò°¢Ð¢gWE&VæFW%Æ’‚“°§Ð ¢òò2222222222222222222222222222222222222222222222222222222222220¢òò224TÄT5Dõ"DRUT•ò‡GRWV—òÓâ&—fÂÓâ6VR¢òò2222222222222222222222222222222222222222222222222222222222220§7FF–2fö–BgWDG&uFVÔ6VÆÂ†–çB‚Â–çB’Â–çBrÂ–çB‚Â–çB–G‚Â&ööÂ†Â—°¢V–çCe÷BÒeUEõDTÕ5¶–G…Òæ6öÆ÷$Â"ÒeUEõDTÕ5¶–G…Òæ6öÆ÷$#°¢f–ÆÅ&÷VæE&V7B‡‚Â’ÂrÂ‚Â‚ÂÖ—ƒScR‡&v#ScRƒ#Ã#BÃC"’ÂÂSR’“°¢f–ÆÅ&V7B‡‚²‚Â’²‚ÂbÂ‚Â“²f–ÆÅ&V7B‡‚²#BÂ’²‚Â’Â‚Â"“°¢G&u&V7B‡‚²‚Â’²‚Â#RÂ‚Â&v#ScRƒÃ"Ã#’“°¢gWD7&W7B‡‚²rÒ#Â’²#Â#Â#BÂÂ"“°¢G&uFW‡D2‡‚²rò"Â’²‚Ò3ÂeUEõDTÕ5¶–G…ÒææöÖ'&RÂÂ&v#ScRƒ#3bÃ#CÃ#S’“°¢6†"'E³eÓ²6ç&–çFb‡'BÂ6—¦Vöb‡'B’Â"VB"ÂeUEõDTÕ5¶–G…Òç&F–ær“°¢G&uFW‡B‡‚²‚Â’²‚ÒbÂ'BÂÂ&v#ScRƒ#SÃ#BÃ“"’“°¢G&u&÷VæE&V7B‡‚Â’ÂrÂ‚Â‚Â†Âò&v#ScRƒ#SÃ#3Ã“’¢Ö—ƒScR†Â&v#ScRƒ#SRÃ#SRÃ#SR’ÂCb’“°§Ð§7FF–2fö–BgWE&VæFW%FVÕ6VÂ†&ööÂ÷—°¢tÆæBÒG'VS²6WD'Vb†&'Vb“°¢t6Æ—ƒÒ²t6Æ—ƒÒ45%õrÒ²t6Æ—“Ò²t6Æ—“Ò45%ô‚Ò°¢vVôf–ÆÄÂ†&'VbÂÂÂÅrÂÄ‚Â&v#ScRƒbÃ#Ã3b’“°¢vVôf–ÆÄÂ†&'VbÂÂÂÅrÂSBÂ&v#ScRƒ#BÃ3ÃSB’“°¢G&uFW‡D2„Årò"ÂBÂ÷ò$TÄ”tR$•dÂ"¢$TÄ”tRERUT•ò"Â2Â&v#ScRƒ#CÃ#CBÃ#S"’“°¢f–ÆÅ&÷VæE&V7BƒÂ"ÂCBÂ#‚Â‚Â&v#ScRƒcÃcbÃ’“°¢–b†÷—²7G&ö¶U6Vrƒ3‚Â‚Â#bÂ#bÂ"Â&v#ScRƒ#CÃ#CÃ#C’“²7G&ö¶U6Vrƒ#bÂ#bÂ3‚Â3BÂ"Â&v#ScRƒ#CÃ#CÃ#C’“²ÒòòÂÒföÇfW ¢VÇ6R²7G&ö¶U6Vrƒ#BÂ‚ÂCÂ3BÂ"Â&v#ScRƒ#CÃ#CÃ#C’“²7G&ö¶U6VrƒCÂ‚Â#BÂ3BÂ"Â&v#ScRƒ#CÃ#CÃ#C’“²Òòò‚6Æ— ¢–çB6öÇ2ÒrÂ7rÒbÂ6‚ÒsbÂƒÒ‚Â“ÒsÂw‚Ò"Âw’Òc°¢f÷"†–çB’Ò²’ÂeUEôåDTÕ3²’²²—°¢–çB2Ò’R6öÇ2Â"Ò’ò6öÇ3°¢–çB‚Òƒ²2¢†7r²w‚’Â’Ò“²"¢†6‚²w’“°¢&ööÂ†ÂÒ÷ò†’ÓÒgWEFVÕ6VÅ³Ò’¢†’ÓÒgWEFVÕ6VÅ³Ò“°¢gWDG&uFVÔ6VÆÂ‡‚Â’Â7rÒBÂ6‚ÒBÂ’Â†Â“°¢Ð¢G&uFW‡D2„Årò"ÂÄ‚ÒbÂ÷ò%Fö6VÂWV—ò&—fÂ"¢%Fö6GRWV—ò‡6RwV&F’"ÂÂ&v#ScRƒcÃsÃ“’“°¢&W6VçBƒÂ45%ô‚Ò“°§Ð§7FF–2fö–BgWE7F'DÖF6‚‚—°¢gWE66÷&TÒ²gWE66÷&T"Ò²gWEÆ•6V2Ò²gWE7&–çBÒfÇ6S°¢gWE&W72ÒgWE&W6†ö÷BÒgWE&W7v—F6‚ÒgWE&W7&–çBÒfÇ6S°¢gWEVæF–ærÒfÇ6S²gWDfÆ6…³ÒÒ°¢gWE&W6WDf÷&ÖF–öâƒ“²òò66VÂ§VvF÷"†WV—ò¢gWDg&VW¦UVçF–ÂÒÖ–ÆÆ—2‚’²ƒ²òò'&WfRW6FR6VP¢gWDg&ÖT×2ÒÖ–ÆÆ—2‚“°¢gWE67&VVâÒe5õÄ“°¢gWE&VæFW%Æ’‚“°§Ð§7FF–2fö–BgWEFVÕ6VÅF–6²†&ööÂ÷—°¢–b‚BçF’&WGW&ã°¢–çBÇ‚ÒvVôÅ‚‚’ÂÇ’ÒvVôÅ’‚“°¢–b†Ç‚ãÒbbÇ‚ÃÒSBbbÇ’ãÒ"bbÇ’ÃÒC—°¢–b†÷—²gWE67&VVâÒe5õDTÓ²gWE&VæFW%FVÕ6VÂ†fÇ6R“²Ð¢VÇ6R²tvÖTÖöFRÒtÕôÔTåS²vÖW5&VæFW$ÖVçR‚“²Òòò6Æ—"FVÂgWF&öÂÂÖVçRFR§VVv÷0¢&WGW&ã°¢Ð¢–çB6öÇ2ÒrÂ7rÒbÂ6‚ÒsbÂƒÒ‚Â“ÒsÂw‚Ò"Âw’Òc°¢–b†Ç’Â“ÇÂÇ‚Âƒ’&WGW&ã°¢–çB2Ò†Ç‚Òƒ’ò†7r²w‚’Â"Ò†Ç’Ò“’ò†6‚²w’“°¢–b†2ÂÇÂ2ãÒ6öÇ2ÇÂ"Â’&WGW&ã°¢–çB–G‚Ò"¢6öÇ2²3²–b†–G‚ãÒeUEôåDTÕ2’&WGW&ã°¢–çB7‚Òƒ²2¢†7r²w‚’Â7’Ò“²"¢†6‚²w’“°¢–b†Ç‚â7‚²7rÒBÇÂÇ’â7’²6‚ÒB’&WGW&ã²òòVâÆ6W&6–öà¢–b‚÷—²gWEFVÕ6VÅ³ÒÒ–Gƒ²gWE6fUFVÒ†–G‚“²gWE67&VVâÒe5ôõ²gWE&VæFW%FVÕ6VÂ‡G'VR“²Ð¢VÇ6R²gWEFVÕ6VÅ³ÒÒ–Gƒ²gWE7F'DÖF6‚‚“²Ð§Ð ¢òòÒÒÒÒçFÆÆf–æÂFVÂ'F–FòÒÒÒÐ§7FF–2fö–BgWE&VæFW$VæB‚—°¢tÆæBÒG'VS²6WD'Vb†&'Vb“°¢t6Æ—ƒÒ²t6Æ—ƒÒ45%õrÒ²t6Æ—“Ò²t6Æ—“Ò45%ô‚Ò°¢vVôf–ÆÄÂ†&'VbÂÂÂÅrÂÄ‚Â&v#ScRƒÃBÃ#‚’“°¢G&uFW‡D2„Årò"Âs‚Â$d”äÂ"ÂRÂ&v#ScRƒ#SÃ#3Ã“’“°¢6†"65³C…Ó°¢6ç&–çFb‡62Â6—¦Vöb‡62’Â"W2VBÒVBW2"À¢eUEõDTÕ5¶gWEFVÕ6VÅ³ÕÒææöÖ'&RÂgWE66÷&TÂgWE66÷&T"ÂeUEõDTÕ5¶gWEFVÕ6VÅ³ÕÒææöÖ'&R“°¢G&uFW‡D2„Årò"Â“Â62Â2Â&v#ScRƒ#CÃ#CBÃ#S"’“°¢6öç7B6†"¢&W2Ò†gWE66÷&TâgWE66÷&T"’ò%Ç„3%Ç„væ7FR"¢†gWE66÷&TÂgWE66÷&T"ò%W&F—7FR"¢$V×FR"“°¢G&uFW‡D2„Årò"Â#SbÂ&W2Â2Â†gWE66÷&TâgWE66÷&T"’ò&v#ScRƒ#Ã#CÃC’¢&v#ScRƒ#CÃƒÃƒ’“°¢G&uFW‡D2„Årò"Â3cbÂ%Fö6&6öçF–çV""Â"Â&v#ScRƒsÃƒÃ#’“°¢&W6VçBƒÂ45%ô‚Ò“°§Ð§7FF–2fö–BgWDVæEF–6²‚—°¢–b…BçF—²gWE67&VVâÒe5õDTÓ²gWE&VæFW%FVÕ6VÂ†fÇ6R“²Ð§Ð ¢òòÒÒÒÒåe3¢VÇF–ÖòWV—òVÆVv–Fò†æÖW76R&fÆW†÷2"Â6ÆfR&gWE÷FVÒ"’ÒÒÒÐ§7FF–2fö–BgWDÆöEFVÒ‚—°¢&Vg2æ&Vv–â‚&fÆW†÷2"ÂG'VR“°¢–çBbÒ&Vg2ævWD–çB‚&gWE÷FVÒ"Â“°¢&Vg2æVæB‚“°¢–b‡bÂÇÂbãÒeUEôåDTÕ2’bÒ°¢gWEFVÕ6VÅ³ÒÒc°§Ð§7FF–2fö–BgWE6fUFVÒ†–çB–G‚—°¢–b†–G‚ÂÇÂ–G‚ãÒeUEôåDTÕ2’&WGW&ã°¢&Vg2æ&Vv–â‚&fÆW†÷2"ÂfÇ6R“°¢&Vg2çWD–çB‚&gWE÷FVÒ"Â–G‚“°¢&Vg2æVæB‚“°§Ð ¢òòÒÒÒÒVçG&FÂÖöFògWF&öÂ†FW6FRVÂÖVçRFR§VVv÷2’ÒÒÒÐ§7FF–2fö–BgWDVçFW"‚—°¢tvÖTÖöFRÒtÕôeUC°¢–b‚gWDÅUG&VG’’gWD'V–ÆDÅUB‚“°¢vVõ&æu7FFRÒƒ–S3ss–#—RâÖ–ÆÆ—2‚“°¢gWDÆöEFVÒ‚“°¢–b†gWEFVÕ6VÅ³ÒÓÒgWEFVÕ6VÅ³Ò’gWEFVÕ6VÅ³ÒÒ†gWEFVÕ6VÅ³Ò²’ReUEôåDTÕ3°¢gWE67&VVâÒe5õDTÓ°¢gWE&VæFW%FVÕ6VÂ†fÇ6R“°§Ð¢òòÒÒÒÒ&÷WFW"FVÂÖöFògWF&öÂÒÒÒÐ§7FF–2fö–BgWEF–6²‚—°¢–b†gWE67&VVâÓÒe5õDTÒ—²gWEFVÕ6VÅF–6²†fÇ6R“²&WGW&ã²Ð¢–b†gWE67&VVâÓÒe5ôõ—²gWEFVÕ6VÅF–6²‡G'VR“²&WGW&ã²Ð¢–b†gWE67&VVâÓÒe5ôTäB—²gWDVæEF–6²‚“²&WGW&ã²Ð¢gWEÆ•F–6²‚“°§Ð ¢òò2222222222222222222222222222222222222222222222222222222222220¢òò22ÔTåRDR¥TTtõ2„vVöÖWG'’F6‚ògWF&öÂ¢òò2222222222222222222222222222222222222222222222222222222222220§7FF–2fö–BvÖW4ÖVçT6&B†–çB‚Â–çB’Â–çBrÂ–çB‚ÂV–çCe÷B66VçBÂ6öç7B6†"¢Æ&VÂÂ–çB¶–æB—°¢f–ÆÅ&÷VæE&V7D‡‚Â’ÂrÂ‚Â#ÂÖ—ƒScR‡&v#ScRƒ‚Ã#"ÃC’Â66VçBÂs’Â#3R“°¢–b‡V”vÆ72’f–ÆÅ&÷VæE&V7D‡‚²bÂ’²bÂrÒ"Â‚ò2ÂbÂ&v#ScRƒ#SRÃ#SRÃ#SR’Â#b“°¢G&u&÷VæE&V7B‡‚Â’ÂrÂ‚Â#ÂÖ—ƒScR†66VçBÂ&v#ScRƒ#SRÃ#SRÃ#SR’Â“’“°¢–çB–7‚Ò‚²rò"Â–7’Ò’²‚ò"Ò#C°¢–b†¶–æBÓÒ—²òò–6öæòvVòF6ƒ¢7V&ò6öâ&ö¦÷2 ¢–çB2ÒCc°¢f–ÆÅ&÷VæE&V7B†–7‚Ò2Â–7’Ò2Â"¢2Â"¢2ÂÂ66VçB“°¢f–ÆÅ&÷VæE&V7B†–7‚Ò2²bÂ–7’Ò2²bÂ"¢2Ò"Â"¢2Ò"Â‚ÂÖ—ƒScR†66VçBÂ&v#ScRƒÃÃ’Â“’“°¢f–ÆÅ&V7B†–7‚Ò#Â–7’Ò‚Â"ÂbÂ&v#ScRƒ#CÃ#CBÃ#S"’“°¢f–ÆÅ&V7B†–7‚²‚Â–7’Ò‚Â"ÂbÂ&v#ScRƒ#CÃ#CBÃ#S"’“°¢ÒVÇ6R²òò–6öæògWF&öÃ¢&Æöâ²6W7V@¢f–ÆÅ&÷VæE&V7B†–7‚ÒS"Â–7’²3ÂBÂbÂbÂ&v#ScRƒCÃcÃc’“°¢f–ÆÄ6—&6ÆR†–7‚Â–7’ÂCÂ&v#ScRƒ#CRÃ#CRÃ#C‚’“°¢G&t6—&6ÆR†–7‚Â–7’ÂCÂ&v#ScRƒSÃSBÃc’“°¢f–ÆÅG&–ævÆR†–7‚Â–7’ÒbÂ–7‚ÒRÂ–7’²bÂ–7‚²RÂ–7’²bÂ&v#ScRƒ3Ã3"ÃC’“²òòVçFvöæð¢f–ÆÄ6—&6ÆR†–7‚Â–7’ÂBÂ&v#ScRƒ3Ã3"ÃC’“°¢Ð¢G&uFW‡D2†–7‚Â’²‚ÒS"ÂÆ&VÂÂ2Â&v#ScRƒ#CBÃ#CrÃ#S"’“°§Ð§7FF–2fö–BvÖW5&VæFW$ÖVçR‚—°¢tÆæBÒG'VS²6WD'Vb†&'Vb“°¢t6Æ—ƒÒ²t6Æ—ƒÒ45%õrÒ²t6Æ—“Ò²t6Æ—“Ò45%ô‚Ò°¢vVôf–ÆÄÂ†&'VbÂÂÂÅrÂÄ‚Â&v#ScRƒ‚Ã#"ÃC’“°¢vVôf–ÆÄÂ†&'VbÂÂÂÅrÂS‚Â&v#ScRƒ#bÃ3"ÃS‚’“°¢G&uFW‡D2„Årò"ÂbÂ$¥TTtõ2"ÂBÂ&v#ScRƒ#CÃ#CBÃ#S"’“°¢f–ÆÅ&÷VæE&V7Bƒ"ÂBÂCbÂ3Â‚Â&v#ScRƒS‚ÃcBÃ“‚’“²òò‚6Æ—"FRÆ ¢7G&ö¶U6Vrƒ#BÂ#ÂCBÂ3‚Â"Â&v#ScRƒ#CÃ#CÃ#C’“²7G&ö¶U6VrƒCBÂ#Â#BÂ3‚Â"Â&v#ScRƒ#CÃ#CÃ#C’“°¢–çB7rÒ3#Â6‚Ò3ÂvÒCÂ“Ò#°¢–çBƒÒ„ÅrÒƒ"¢7r²v’’ò"Âƒ"Òƒ²7r²v°¢vÖW4ÖVçT6&B‡ƒÂ“Â7rÂ6‚Â&v#ScRƒsÃSÃ#C’Â$vVöÖWG'’F6‚"Â“°¢vÖW4ÖVçT6&B‡ƒ"Â“Â7rÂ6‚Â&v#ScRƒCÃsÃƒ’Â$gWF&öÂ"Â“°¢G&uFW‡D2„Årò"ÂÄ‚Ò#"Â%Fö6Vâ§VVvò&V×W¦""Â"Â&v#ScRƒcÃsÃ“’“°¢&W6VçBƒÂ45%ô‚Ò“°§Ð¢òò6–W'&Æ$§VVv÷2"Óâ†öÖRâäòFö6tÆæC¢Æò†6R6Æ÷6R‚’ÂVRW0¢òòV–VâÆòF–VæRFö7VÖVçFFò6öÖò&W7öç6&–Æ–FBFVÂg&ÖWv÷&²âöæW&ÆòfÇ6P¢òòV’çFW2FRÆÆÖ"†6–VR6Æ÷6Rf–W&v4ÆæCÖfÇ6R’wV&F&Væ¢òòÖ–æ–GW&t•$DVâ&V6–VçFW2†VÂ66òW†7FòVRf—67R6öÖVçF&–ò’Â’Và¢òòÖöFò¶–÷66òÂFöæFR6Æ÷6R6Ræ–Vv6W'&"ÂFV¦&VÂÖ÷F÷"Vâ÷'G&—@¢òòÖ–VçG&2VÂ§VVvò6VwV–F–'V¦æFòVâÆæG66Rà§7FF–2fö–BvÖW4W†—D‚—²6Æ÷6R‚“²Ð§7FF–2fö–BvÖW4VçFW$vVò‚—²òò'&RvVòF6‚†Ö÷F÷"÷&–v–æÂÂ–çF7Fò¢tvÖTÖöFRÒtÕôtTó°¢vVôÆöE&öw&W72‚“°¢t7W$ÆWfVÂÒ°¢vVôVçFW%6VÆV7B‚“°§Ð§7FF–2fö–BvÖW4ÖVçUF–6²‚—°¢–b‚BçF’&WGW&ã°¢–çBÇ‚ÒvVôÅ‚‚’ÂÇ’ÒvVôÅ’‚“°¢–b†Ç‚ãÒ"bbÇ‚ÃÒS‚bbÇ’ãÒBbbÇ’ÃÒCB—²vÖW4W†—D‚“²&WGW&ã²Ð¢–çB7rÒ3#Â6‚Ò3ÂvÒCÂ“Ò#°¢–çBƒÒ„ÅrÒƒ"¢7r²v’’ò"Âƒ"Òƒ²7r²v°¢–b†Ç’ãÒ“bbÇ’ÃÒ“²6‚—°¢–b†Ç‚ãÒƒbbÇ‚ÃÒƒ²7r—²vÖW4VçFW$vVò‚“²&WGW&ã²Ð¢–b†Ç‚ãÒƒ"bbÇ‚ÃÒƒ"²7r—²gWDVçFW"‚“²&WGW&ã²Ð¢Ð§Ð ¢òòÒÒÒÒ6Æ—"FVÂ6VÆV7F÷"FRvVòF6ƒ¢gVVÇfRÂÖVçRFR§VVv÷2ÒÒÒÐ§7FF–2fö–BvVôW†—B‚—²tvÖTÖöFRÒtÕôÔTåS²vÖW5&VæFW$ÖVçR‚“²Ð ¢òòÒÒÒÒV×W¦"Vâæ—fVÂ„æ÷&ÖÂò&7F–6’ÒÒÒÐ§7FF–2fö–BvVõ7F'DÆWfVÂ†–çB–G‚Â&ööÂ&7F–6R—°¢vVô&–æDÆWfVÂ†–G‚“°¢u&7F–6RÒ&7F–6S°¢vVõ&æu7FFRÒƒ–S3ss–#—RâÖ–ÆÆ—2‚“°¢vVôGFV×G2Ò°¢vVõ7FFRÒtTõõÄ“°¢vVõ&W6WDÆWfVÂ‚“°¢vVô56WBÒfÇ6S°¢vVôæW‡D5ÒtÆVâ¢ã#&c²òò&–ÖW"6†V6·ö–çBã#"P¢vVôF÷vå&WbÒfÇ6S²vVô†VÆDÆF6‚ÒfÇ6S²vVõFÆF6‚ÒfÇ6S°¢u67&VVâÒu5ôtÔS°¢vVõ&VæFW$vÖR‚“°§Ð ¢òòÒÒÒÒföÇfW"Â6VÆV7F÷"‡&V6&vVÂ&öw&W6òÖ÷7G&Fò’ÒÒÒÐ§7FF–2fö–BvVôVçFW%6VÆV7B‚—°¢u67&VVâÒu5õ4TÄT5C°¢vVô&–æDÆWfVÂ†t7W$ÆWfVÂ“°¢vVõ&VæFW%6VÆV7B‚“°§Ð ¢òòÒÒÒÒF÷VW2FVÂ6VÆV7F÷"ÒÒÒÐ§7FF–2fö–BvVõ6VÆV7EF–6²‚—°¢–b‚BçF’&WGW&ã°¢–çBÇ‚ÒvVôÅ‚‚’ÂÇ’ÒvVôÅ’‚“°¢–çBƒÒc‚Â“ÒcÂrÒCcC°¢–b†Ç‚ãÒbbÇ‚ÃÒSbbbÇ’ãÒ‚bbÇ’ÃÒC—²vVôW†—B‚“²&WGW&ã²Òòò6Æ—"FRÆ ¢–b†Ç‚ãÒ#‚bbÇ‚ÃÒ“bbbÇ’ãÒ“bbbÇ’ÃÒ#ƒ‚—²t7W$ÆWfVÂÒ†t7W$ÆWfVÂ²2’RC²vVõ&VæFW%6VÆV7B‚“²&WGW&ã²ÒòòÀ¢–b†Ç‚ãÒsBbbÇ‚ÃÒss"bbÇ’ãÒ“bbbÇ’ÃÒ#ƒ‚—²t7W$ÆWfVÂÒ†t7W$ÆWfVÂ²’RC²vVõ&VæFW%6VÆV7B‚“²&WGW&ã²Òòòà¢òò&'&æ÷&ÖÂà¢–b†Ç‚ãÒƒ²CBbbÇ‚ÃÒƒ²rÒCBbbÇ’ãÒ“²C"bbÇ’ÃÒ“²sb—²vVõ7F'DÆWfVÂ†t7W$ÆWfVÂÂfÇ6R“²&WGW&ã²Ð¢òò&'&&7F–6à¢–b†Ç‚ãÒƒ²CBbbÇ‚ÃÒƒ²rÒCBbbÇ’ãÒ“²##bbbÇ’ÃÒ“²#c—²vVõ7F'DÆWfVÂ†t7W$ÆWfVÂÂG'VR“²&WGW&ã²Ð§Ð ¢òòÒÒÒÒF–6²FR¥TTtò‡F‡&÷GFÆR²f—6–66öâ7V'7FW²&VæFW"’ÒÒÒÐ§7FF–2fö–BvVôvÖUF–6²‚—°¢V–çC3%÷Bæ÷rÒÖ–ÆÆ—2‚“°¢–çBÇ‚ÒvVôÅ‚‚’ÂÇ’ÒvVôÅ’‚“°¢òò6Æ—"Â6VÆV7F÷"ÒÒVâDôDõ2Æ÷2F–6·2†çFW2FVÂF‡&÷GFÆR’&æòW&FW&Æòà¢–b…BçFbbÇ‚ãÒbbÇ‚ÃÒSbbbÇ’ãÒbbbÇ’ÃÒ3b—²vVôVçFW%6VÆV7B‚“²&WGW&ã²Ð¢òò&V–æ–6–"6†V6·ö–çG2‡6öÆò&7F–6“¢&V–æ–6–VÂæ—fVÂFW6FRRà¢–b†u&7F–6RbbBçFbbÇ‚ãÒÅrÒ3"bbÇ‚ÃÒÅrÒbbÇ’ãÒbbbÇ’ÃÒ3b—°¢vVô56WBÒfÇ6S²vVôæW‡D5ÒtÆVâ¢ã#&c°¢vVôGFV×G2Ò²vVõ&W6WDÆWfVÂ‚“²vVõ7FFRÒtTõõÄ“°¢vVõ&VæFW$vÖR‚“²&WGW&ã°¢Ð¢òòÆF6†VòFR–çWBFR§VVvò‡¦öæ&¦òVÂ…TB“¢†VÆB6÷7FVæ–Fò²fÆæ6òFRFà¢&ööÂ–åÆ’ÒBæF÷vâbbÇ’âtTõô…TEôƒ°¢–b†–åÆ’’vVô†VÆDÆF6‚ÒG'VS°¢–b†–åÆ’bbvVôF÷vå&Wb’vVõFÆF6‚ÒG'VS°¢vVôF÷vå&WbÒBæF÷vã° ¢òòF‡&÷GFÆS¢æòfç¦"f—6–6÷&VæFW"Ö2&–FòVRätTõôe$ÔUôÕ2à¢–b†æ÷rÒvVôg&ÖT×2ÂtTõôe$ÔUôÕ2’&WGW&ã°¢fÆöBGBÒ†æ÷rÒvVôg&ÖT×2’òãc°¢vVôg&ÖT×2Òæ÷s°¢–b†GBâtTõôEDÔ‚’GBÒtTõôEDÔƒ° ¢&ööÂ†VÆBÒvVô†VÆDÆF6‚ÂFVFvRÒvVõFÆF6ƒ°¢vVô†VÆDÆF6‚ÒfÇ6S²vVõFÆF6‚ÒfÇ6S° ¢–b†vVõ7FFRÓÒtTõõÄ’—°¢òòVfV7F÷2FR'Vâ6öÆòF"‡VæfW¢÷"g&ÖRÂgVW&FVÂ7V'7FW’à¢–b‡FVFvR—°¢–b†tf÷&ÖÓÒe$Õô$ÄÂ’vVôw&dF—"ÒÖvVôw&dF—#²òò–çfW'F—"w&fVF@¢VÇ6R–b†tf÷&ÖÓÒe$ÕõTdò—²vVõfVÅ’ÒÖvVôw&dF—"¢tTõõTdõô”Õ²vVôw&÷VæFVBÒfÇ6S²Òòò6ÇFò6÷'Fð¢Ð¢òò7V'7FW–æs¢f—6–6Vâ6÷2f–¦÷2ÃÒtTõõ5T%5DU‡6–âGVæVÆW27VÇV–W"e2’à¢fÆöB&VÖ–âÒGC°¢v†–ÆR‡&VÖ–ââãbbbvVõ7FFRÓÒtTõõÄ’—°¢fÆöB7FWÒ&VÖ–ââtTõõ5T%5DUòtTõõ5T%5DU¢&VÖ–ã°¢vVõWFFR‡7FWÂ†VÆB“°¢&VÖ–âÓÒ7FW°¢Ð¢òò6†V6·ö–çBWFöÖF–6òVâ&7F–6‡æ6F#"RFRfæ6R’à¢–b†u&7F–6RbbvVõ7FFRÓÒtTõõÄ’bbvVõ67&öÆÂãÒvVôæW‡D5—°¢vVõ6fT5‚“°¢vVôæW‡D5³ÒtÆVâ¢ã#&c°¢Ð¢ÒVÇ6R–b†vVõ7FFRÓÒtTõôDTB—°¢vVõWFFU'F–6ÆW2†GB“°¢–b†æ÷rÒvVôFVD×2âtTõõ$U5tåôÕ2—°¢vVôGFV×G2²³°¢–b†u&7F–6RbbvVô56WB—²vVõ&W7F÷&T5‚“²vVõ7FFRÒtTõõÄ“²Òòò&V&V6RVâVÂ6†V6·ö–ç@¢VÇ6R²vVõ&W6WDÆWfVÂ‚“²vVõ7FFRÒtTõõÄ“²Òòòæ÷&ÖÃ¢FW6FRP¢Ð¢ÒVÇ6R²òòtTõõt”à¢–b…BçFbbÇ’âtTõô…TEô‚—²vVôVçFW%6VÆV7B‚“²&WGW&ã²Ð¢Ð¢vVõ&VæFW$vÖR‚“°§Ð ¢òòÒÒÒÒVçG&FFRÆ$§VVv÷2#¢'&RVÂÖVçRFR6VÆV66–öâFR§VVvòÒÒÒÐ§7FF–2fö–BvVôVçFW"‚—°¢tÆæBÒG'VS°¢tvÖTÖöFRÒtÕôÔTåS°¢vÖW5&VæFW$ÖVçR‚“°§Ð ¢òòÒÒÒÒF–6²FRÆ¢Vç'WFÖVçRòvVòF6‚ògWF&öÂÒÒÒÐ§7FF–2fö–BvVõF–6²‚—°¢–b†tvÖTÖöFRÓÒtÕôÔTåR—²vÖW4ÖVçUF–6²‚“²&WGW&ã²Ð¢–b†tvÖTÖöFRÓÒtÕôeUB—²gWEF–6²‚“²&WGW&ã²Ð¢òòtÕôtTòÓâÖ÷F÷"÷&–v–æÂFRvVöÖWG'’F6‚†–çF7Fò¢–b†u67&VVâÓÒu5õ4TÄT5B—²vVõ6VÆV7EF–6²‚“²&WGW&ã²Ð¢vVôvÖUF–6²‚“°§Ð¢òòädTtDõ"+rFFF—fòà¢òòW6Væ6–Â¢&'&FRF—&V66–öæW2²W7FFòFR6öæW†–öâà¢òò÷6–öæÂ¢&'&FRW7Fæ2ÒÒ&V6R7VæFòVÂÆ–Vç¦ò6FR3c€¢òòFRæ6†ò‡VæW7FæÆVv–&ÆRæV6W6—FãÒ‚’VW&VÖ÷0¢òòÂÖVæ÷2G&W2’à¢òò÷6–öæÂ"¢vÆö&òö–ÇW7G&6–öâÒÒ&V6R6öÆò6’VVFâãÒ#‚Æ–'&W0¢òò&¦òÆ&'&²÷"FV&¦òFRW6ò6RöÖ—FRVçFW&VâfW¢FP¢òòVæ6övW&Æ†7FæòF—7F–æwV—'6Rà§7FF–2fö–BædVçFW"‚—°¢6WD'Vb†f"“°¢–çB'‚Â'’Â'rÂ&ƒ²V”&÷‚†'‚Â'’Â'rÂ&‚“°¢f–ÆÅ&V7B†'‚Â'’Â'rÂ&‚Ât”åô$r“°¢–çBBÒV•B‚’ÂvÒV”v‚“°¢–çB’Ò'’²C°¢–çBg5BÒV”föçDf—B‚$æfVvF÷""Â'rÒ"¢BÂV”föçD‚†&‚ò"’“°¢G&uFW‡D2†'‚²'rò"Â’Â$æfVvF÷""Âg5BÂ&v#ScRƒ#SRÃ#SRÃ#SR’“°¢’³ÒV”Æ–æT‚†g5B’²v°¢V–çC…÷BF'2ÒV•6V7F–öâƒÂ'rãÒ3c“°¢–b†F'2—°¢–çBçBÒ2ÂGrÒ†'rÒ"¢BÒ†çBÒ’¢v’òçC°¢–çBF†‚Ò&‚òC²–b‡F†‚Â#’F†‚Ò#²–b‡F†‚â3B’F†‚Ò3C°¢6öç7B6†"¢F'5³5ÒÒ²$–æ–6–ò"Â$Ö&6F÷&W2"Â$†—7F÷&–Â"Ó°¢f÷"†–çB’Ò²’ÂçC²’²²—°¢–çB‚Ò'‚²B²’¢‡Gr²v“°¢V•&V7D‡‚Â’ÂGrÂF†‚ÂF†‚ò2Â’ÓÒò&v#ScRƒS‚ÃƒbÃS’¢&v#ScRƒ3BÃ3‚ÃS’ÂF'2“°¢V•FW‡D2‡‚²Grò"Â’²F†‚ò"ÒV”Æ–æT‚ƒ"’ò"ÂF'5¶•ÒÀ¢V”föçDf—B‡F'5¶•ÒÂGrÒÂ"’Â&v#ScRƒ##RÃ#3"Ã#CR’ÂF'2“°¢Ð¢’³ÒF†‚²v°¢Ð¢–çB&$‚Ò&‚ò²–b†&$‚Â#b’&$‚Ò#c²–b†&$‚âC‚’&$‚ÒCƒ°¢f–ÆÅ&÷VæE&V7B†'‚²BÂ’Â'rÒ"¢BÂ&$‚Â&$‚ò2Â&v#ScRƒ#CÃ#C"Ã#C‚’“°¢G&uFW‡B†'‚²B¢"Â’²&$‚ò"ÒV”Æ–æT‚ƒ"’ò"Â&‡GG3¢òò"À¢V”föçDf—B‚&‡GG3¢òò"Â'rÒB¢BÂ"’Â&v#ScRƒ#Ã#bÃC’“°¢’³Ò&$‚²v°¢–çB&W7BÒ†'’²&‚’Ò’ÒC°¢V–çC…÷BvÆö&RÒV•6V7F–öâƒÂ&W7BãÒ#“°¢–çB7“°¢–b†vÆö&R—°¢–çB'"Ò‡&W7BÒC’ò#²–b‡'"â'ròB’'"Ò'ròC²–b‡'"âs’'"Òs°¢7’Ò’²'"²c°¢V–çCe÷Bv2ÒÖ—ƒScR…t”åô$rÂ&v#ScRƒƒÃ#Ã#’ÂvÆö&R“°¢G&t6—&6ÆR†'‚²'rò"Â7’Â'"Âv2“²G&t6—&6ÆR†'‚²'rò"Â7’Â'"ÒÂv2“°¢dÆ–æR†'‚²'rò"Â7’Ò'"Â"¢'"Âv2“°¢„Æ–æR†'‚²'rò"Ò'"Â7’Â"¢'"Âv2“°¢’Ò7’²'"²v°¢Ð¢6öç7B6†"¢7BÒ%6–â6öæW†•Ç„35Ç„#6âÒÖöFòöffÆ–æR#°¢–çBg’Ò'’²&‚ÒBÒV”Æ–æT‚ƒ"“°¢–b†g’Â’’g’Ò“°¢G&uFW‡D2†'‚²'rò"Âg’Â7BÂV”föçDf—B‡7BÂ'rÒ"¢BÂ"’Â&v#ScRƒcÃc‚Ãƒ‚’“°¢fÇ„fÇW6‚…t”åõDõÂt”åô$õB“°§Ð¢òò2222222222222222222222222222222222222222222222222222222222220¢òò224•5DTåDRDR„$Et$R„d4R2ÂFVçG&òFR6öFR”DR¢òò22ÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÐ¢òò22æVÂFR4ôÄòÄT5EU$†æò6R&Vf7F÷&—¦VÂVF—F÷"ÂVP¢òò22†÷’W2VæFVÖò’âÆ—7FÆ÷2ÖöGVÆ÷2“$2FWFV7FF÷2VâÆ¢òò22f6R"†FWFV7FVDÖöGVÆW5µÒ’’ÂÂVÆVv—"VæòÂvVæW&VÀ¢òò226öF–vòFR–æ–6–Æ—¦6–öâ’Æò×VW7G&6öÖòFW‡Fò&¢òò226÷–"Â÷'FVÆW2vÆö&Â†6Æ—&ö&EµÒ’â6RF–'V¦¢òò22f"6öâfÇ„fÇW6‚Væ6öÆfW¢÷"6Ö&–òFRW7FFð¢òò22†æFÆò&VF–'V¦÷"g&ÖRGW&çFR5Eô6öâVÂ”DR’À¢òò226’VRæò†’'FVòæ’6öæfÆ–7Fò6öâVÂ&W6VçFW"à¢òò2222222222222222222222222222222222222222222222222222222222220 ¢òòW7FFòFVÂ6—7FVçFP§7FF–2&ööÂ‡uv—¦&D7F—fRÒfÇ6S²òòæVÂ&–W'Fð§7FF–2–çB‡u6VÄÖöGVÆRÒÓ²òòÓÒÆ—7F²ãÓÒ–æF–6RVâFWFV7FVDÖöGVÆW2‡f—7F6öF–vò§7FF–2&ööÂ‡t6÷–VBÒfÇ6S²òòfVVF&6²FVÂ&÷Föâ$6÷–" §7FF–26†"‡t6öFU³S%ÒÒ"#²òò6öF–vòvVæW&Fò‡FÖ&–VâfÂ÷'FVÆW2 ¢òòvVöÖWG&–¢&÷FöâFVÂVF—F÷ ¢6FVf–æR…uô%Dåõ‚…45%õrò"ÒC¢6FVf–æR…uô%Dåõ’…t”åô$õBÒB¢6FVf–æR…uô%Dåõr#ƒ ¢6FVf–æR…uô%Dåô‚C`¢òòvVöÖWG&–¢Æ—7FFRÖöGVÆ÷0¢6FVf–æR…uôÄ•5Eõ“…t”åõDõ²Sb¢6FVf–æR…uõ$õuô‚S`¢6FVf–æR…uô4$Eõ‚#@¢6FVf–æR…uô4$Eõr…45%õrÒC‚¢6FVf–æR…uô4$Eô‚C€¢òòvVöÖWG&–¢&÷Föâ$6W'&""‡f—7FÆ—7F¢6FVf–æR…uô4Äõ4Uõ‚…45%õrò"Òƒ¢6FVf–æR…uô4Äõ4Uõ’…t”åô$õBÒc¢6FVf–æR…uô4Äõ4Uõrc ¢6FVf–æR…uô4Äõ4Uô‚C@¢òòvVöÖWG&–¢&÷FöæW2%föÇfW""ò$6÷–""‡f—7F6öF–vò¢6FVf–æR…uô$4µõ‚#@¢6FVf–æR…uô4õ•õ‚…45%õrÒ#BÒS¢6FVf–æR…uô5Eõ’…t”åô$õBÒc¢6FVf–æR…uô5EõrS ¢6FVf–æR…uô5Eô‚C@ ¢òòG&tÖöGVÆT–6öâ‚’6RFVf–æRÖ2&¦ò†&Æ÷VRFRÆ—6Æ“²f÷'v&BÖFV6Â&¢òòöFW"&WWF–Æ—¦"VÂÖ—6ÖòÖVòF—òÓæ–6öæòV’à§7FF–2fö–BG&tÖöGVÆT–6öâ„ÖöGVÆUG—RG—RÂ–çB‚Â–çB’Â–çB2“° ¢òò4ôDR”DR+rFFF—fòà¢òòW6Væ6–Â¢Æ—7FFòFR6öF–vò6öâçVÖW&÷2FRÆ–æV²&÷FöâFVÂ6—7FVçFRà¢òò÷6–öæÂ¢Ö–æ–ÖòæVÂFR6–Ö&öÆ÷2ÆFW&V6†ÒÒ&V6R7VæFòVÀ¢òòÆ–Vç¦ò6FRCs‚FRæ6†ò†VÂ6öF–vòæV6W6—FãÒ3€¢òò&æò6÷'F'6R’VÂæVÂãÒC‚&ÆVW'6R’à¢òò÷6–öæÂ"¢–RFR6Æ&6–öâÒÒ&V6R6’6ö'&âãÒb‚à§7FF–2fö–B–FTVçFW"‚—°¢6WD'Vb†f"“°¢–çB'‚Â'’Â'rÂ&ƒ²V”&÷‚†'‚Â'’Â'rÂ&‚“°¢f–ÆÅ&V7B†'‚Â'’Â'rÂ&‚Â&v#ScRƒ#Ã#"Ã3’“°¢–çBBÒV•B‚’ÂvÒV”v‚“°¢–çB’Ò'’²C°¢–çBg5BÒV”föçDf—B‚$6öFR”DR"Â'rÒ"¢BÂV”föçD‚†&‚ò"’“°¢G&uFW‡D2†'‚²'rò"Â’Â$6öFR”DR"Âg5BÂ&v#ScRƒ#SRÃ#SRÃ#SR’“°¢’³ÒV”Æ–æT‚†g5B’²v°¢–çB6–FUrÒ'ròC²–b‡6–FUrâ“’6–FUrÒ“°¢V–çC…÷B6–FRÒV•6V7F–öâƒÂ'rãÒCs“°¢–çB6öFUrÒ6–FRò†'rÒ"¢BÒ6–FUrÒv’¢†'rÒ"¢B“°¢6öç7B6†"¢6öFU³…ÒÒ°¢"6–æ6ÇVFRÄfÆW„õ2æƒâ"Â""Â'fö–B6WGW‚’²"Â"67&VVâæ&Vv–â‚“²"À¢"V’æG&t†öÖR‚“²"Â'Ò"Â""Â'fö–BÆö÷‚’²V’çF–6²‚“²Ò"Ó°¢–çB'Fä‚Ò&‚ò“²–b†'Fä‚Â3’'Fä‚Ò3²–b†'Fä‚âS’'Fä‚ÒS°¢–çB6öFT&÷BÒ'’²&‚ÒBÒ'Fä‚Òv°¢–çBÆ–æT‚Ò†6öFT&÷BÒ’’òƒ²–b†Æ–æT‚Â’’Æ–æT‚Ò“°¢–çBg42ÒÆ–æT‚ãÒ#ò"¢°¢–çBwWBÒFW‡Er‚#ƒ‚"Âg42’²ƒ°¢f÷"†–çB’Ò²’Âƒ²’²²—°¢–çBÇ’Ò’²’¢Æ–æTƒ°¢–b†Ç’²Æ–æT‚â6öFT&÷B’'&V³°¢6†"Æå³…Ó²6ç&–çFb†ÆâÂ6—¦Vöb†Æâ’Â"S&B"Â’²“°¢G&uFW‡B†'‚²BÂÇ’ÂÆâÂg42Â&v#ScRƒ“Ã“bÃ"’“°¢G&uFW‡B†'‚²B²wWBÂÇ’Â6öFU¶•ÒÂV”föçDf—B†6öFU¶•ÒÂ6öFUrÒwWBÂg42’Â&v#ScRƒSÃ##Ãƒ’“°¢Ð¢–b†6–FR—°¢–çB7‚Ò'‚²B²6öFUr²v°¢V•&V7D‡7‚Â’Â6–FUrÂ6öFT&÷BÒ’ÂBÂ&v#ScRƒ#bÃ#’ÃC’Â6–FR“°¢V•FW‡D2‡7‚²6–FUrò"Â’²BÂ%6–Ö&öÆ÷2"ÂV”föçDf—B‚%6–Ö&öÆ÷2"Â6–FUrÒ"Â"’Â&v#ScRƒSÃcÃ“’Â6–FR“°¢6öç7B6†"¢7–Õ³5ÒÒ²'6WGW‚’"Â&Æö÷‚’"Â$fÆW„õ2æ‚"Ó°¢–çB7—’Ò’²B²V”Æ–æT‚ƒ"’²v°¢f÷"†–çB’Ò²’Â3²’²²—°¢V•FW‡B‡7‚²BÂ7—’Â7–Õ¶•ÒÂV”föçDf—B‡7–Õ¶•ÒÂ6–FUrÒ"¢BÂ"’Â&v#ScRƒ#Ã#RÃ#3R’Â6–FR“°¢7—’³ÒV”Æ–æT‚ƒ"’²c°¢Ð¢Ð¢‡uv—¦&D7F—fRÒfÇ6S²‡u6VÄÖöGVÆRÒÓ²‡t6÷–VBÒfÇ6S°¢–çB'vâÒ'rÒ"¢C²–b†'vââ3#’'vâÒ3#°¢–çB'†âÒ'‚²†'rÒ'vâ’ò"Â'–âÒ'’²&‚ÒBÒ'Fäƒ°¢f–ÆÅ&÷VæE&V7B†'†âÂ'–âÂ'vâÂ'Fä‚Â'Fä‚òBÂ&v#ScRƒcÃÃ#3R’“°¢G&uFW‡D2†'‚²'rò"Â'–â²'Fä‚ò"ÒV”Æ–æT‚ƒ"’Â$6—7FVçFRFR†&Gv&R"À¢V”föçDf—B‚$6—7FVçFRFR†&Gv&R"Â'vâÒ"Â2’Â&v#ScRƒ#SRÃ#SRÃ#SR’“°¢fÇ„fÇW6‚…t”åõDõÂt”åô$õB“°§Ð ¢òò–æF–6W2FRÆ÷2ÖöGVÆ÷27F—f÷2‡&ÖV"f–Æ2FRÆÆ—7FÂÓâFWFV7FVDÖöGVÆW2§7FF–2–çB‡t7F—fTÆ—7B†–çB¢÷WBÂ–çBÖ†â—°¢–çB2Ò°¢f÷"†–çB’Ò²’ÂFWFV7FVD6÷VçBbb2ÂÖ†ã²’²²¢–b†FWFV7FVDÖöGVÆW5¶•Òæ7F—fR’÷WE¶2²µÒÒ“°¢&WGW&â3°§Ð ¢òòvVæW&VÂ6öF–vòFR–æ–6–Æ—¦6–öâFVÂÖöGVÆòVâ‡t6öFUµÒ†6†%µÒ²6ç&–çFb§7FF–2fö–B‡tvVä6öFR†6öç7BFWFV7FVDÖöGVÆR¢Ò—°¢6—¦U÷BâÒ°¢‡t6öFU³ÒÒ°¢6FVf–æR…t4B‚âââ’F÷²–çB÷rÒ6ç&–çFb†‡t6öFR²âÂ6—¦Vöb†‡t6öFR’ÒâÂõõdô$u5õò“²À¢–b…÷râ—²â³Ò‡6—¦U÷B•÷s²–b†âãÒ6—¦Vöb†‡t6öFR’’âÒ6—¦Vöb†‡t6öFR’Ò²Ò×v†–ÆRƒ¢…t4B‚"òò–æ–6–Æ—¦6–öâWFöÖF–6Æâ"“°¢…t4B‚"òòÖöGVÆó¢W2"ÂÒÓææÖR“°¢–b†ÒÓæ“&4FG"’…t4B‚"ƒ‚S%‚’"ÂÒÓæ“&4FG"“°¢…t4B‚%Æâ6–æ6ÇVFRÅv—&RæƒåÆåÆâ"“°¢…t4B‚'fö–B6WGW‚’µÆâ"“°¢…t4B‚"v—&Ræ&Vv–â‚VBÂVB“²òò4DÒVB44ÃÒVEÆâ"Â”åô“$5õ4DÂ”åô“$5õ44ÂÂ”åô“$5õ4DÂ”åô“$5õ44Â“°¢–b†ÒÓæ“&4FG"—°¢…t4B‚"v—&Ræ&Vv–åG&ç6Ö—76–öâƒ‚S%‚“µÆâ"ÂÒÓæ“&4FG"“°¢…t4B‚"&ööÂö²Ò…v—&RæVæEG&ç6Ö—76–öâ‚’ÓÒ“µÆâ"“°¢Ð¢7v—F6‚†ÒÓçG—R—°¢66RÔôEô$ÔS#ƒ¢…t4B‚"òòÆ–"7VvW&–F¢Fg'V—Eô$ÔS#ƒÆâ"“²'&V³°¢66RÔôEôÕScS¢…t4B‚"òòÆ–"7VvW&–F¢ÕScS„“$6FWb•Æâ"“²'&V³°¢FVfVÇC¢'&V³°¢Ð¢…t4B‚'ÕÆâ"“°¢7VæFVb…t4@§Ð ¢òòF–'V¦VÂæVÂFVÂ6—7FVçFR‡Væ6öÆfW¢÷"6Ö&–òFRW7FFò§7FF–2fö–B‡tG&uv—¦&B‚—°¢6WD'Vb†f"“°¢f–ÆÅ&V7BƒÂt”åõDõÂ45%õrÂt”åô$õBÒt”åõDõÂt”åô$r“° ¢–b†‡u6VÄÖöGVÆRÂ—°¢òòÒÒÒÒf—7FÄ•5DÒÒÒÐ¢G&uFW‡D2…45%õrò"Ât”åõDõ²bÂ$6—7FVçFRFR†&Gv&R"Â2Â&v#ScRƒ#SRÂ#SRÂ#SR’“°¢–çB–G‡5´Ô…ôÔôETÄU5ôDUDT5DTEÓ°¢–çBæ2Ò‡t7F—fTÆ—7B†–G‡2ÂÔ…ôÔôETÄU5ôDUDT5DTB“°¢–b†æ2ÓÒ—°¢G&uFW‡D2…45%õrò"Ât”åõDõ²#Â$æò†’ÖöGVÆ÷2“$2FWFV7FF÷2"Â"Â&v#ScRƒsÂs‚Â“b’“°¢G&uFW‡D2…45%õrò"Ât”åõDõ²SÂ$6öæV7FVâ6Vç6÷"Â'W2…4DÓrÂ44ÃÓ‚’"ÂÂ&v#ScRƒ3Â3‚ÂS‚’“°¢ÒVÇ6R°¢f÷"†–çB"Ò²"Âæ3²"²²—°¢FWFV7FVDÖöGVÆR¢ÒÒfFWFV7FVDÖöGVÆW5¶–G‡5·%ÕÓ°¢–çB7’Ò…uôÄ•5Eõ“²"¢…uõ$õuôƒ°¢G&tÆ—V–DvÆ75æVÂ„…uô4$Eõ‚Â7’Â…uô4$EõrÂ…uô4$Eô‚Â"Â&v#ScRƒCÂcÂ3’“°¢G&tÖöGVÆT–6öâ†ÒÓçG—RÂ…uô4$Eõ‚²‚Â7’²‚Â3"“°¢6†"Æ&VÅ³C…Ó°¢–b†ÒÓæ“&4FG"’6ç&–çFb†Æ&VÂÂ6—¦Vöb†Æ&VÂ’Â"W2ƒ‚S%‚’"ÂÒÓææÖRÂÒÓæ“&4FG"“°¢VÇ6R6ç&–çFb†Æ&VÂÂ6—¦Vöb†Æ&VÂ’Â"W2"ÂÒÓææÖR“°¢G&uFW‡B„…uô4$Eõ‚²SÂ7’²bÂÆ&VÂÂ"Â&v#ScRƒ#CÂ#C"Â#C‚’“°¢G&uFW‡D2„…uô4$Eõ‚²…uô4$EõrÒCbÂ7’²‚Â$6öæf–r"ÂÂ&v#ScRƒƒÂ#Â#SR’“°¢Ð¢Ð¢f–ÆÅ&÷VæE&V7B„…uô4Äõ4Uõ‚Â…uô4Äõ4Uõ’Â…uô4Äõ4UõrÂ…uô4Äõ4Uô‚ÂBÂ&v#ScRƒsÂsBÂ“’“°¢G&uFW‡D2…45%õrò"Â…uô4Äõ4Uõ’²BÂ$6W'&""Â"Â&v#ScRƒ#SRÂ#SRÂ#SR’“°¢ÒVÇ6R°¢òòÒÒÒÒf—7F4ôD”tò‡6öÆòÆV7GW&’ÒÒÒÐ¢FWFV7FVDÖöGVÆR¢ÒÒfFWFV7FVDÖöGVÆW5¶‡u6VÄÖöGVÆUÓ°¢6†"E³CÓ²6ç&–çFb‡BÂ6—¦Vöb‡B’Â$6öF–vó¢W2"ÂÒÓææÖR“°¢G&uFW‡D2…45%õrò"Ât”åõDõ²bÂBÂ"Â&v#ScRƒ#SRÂ#SRÂ#SR’“° ¢–çB‚ÒbÂ’Òt”åõDõ²S"ÂrÒ45%õrÒ3"Â‚Ò„…uô5Eõ’Ò"’Ò…t”åõDõ²S"“°¢G&tÆ—V–DvÆ75æVÂ‡‚Â’ÂrÂ‚Â"Â&v#ScRƒ#BÂCÂƒ’“° ¢òòföÆ6"‡t6öFRÆ–æVÆ–æV‡7Æ—B÷"uÆâr¢–çBÇ’Ò’²"ÂÆ–æTæòÒ°¢6öç7B6†"¢Ò‡t6öFS°¢6†"Æ–æU³ƒÓ°¢v†–ÆR‚§—°¢–çBÆ’Ò°¢v†–ÆR‚§bb§ÒuÆârbbÆ’Âs‚—²Æ–æU¶Æ’²µÒÒ§²³²Ð¢Æ–æU¶Æ•ÒÒ°¢–b‚§ÓÒuÆâr’²³°¢6†"çVÕ³eÓ²6ç&–çFb†çVÒÂ6—¦Vöb†çVÒ’Â"S&B"ÂÆ–æTæò²²“°¢G&uFW‡B‡‚²ÂÇ’ÂçVÒÂÂ&v#ScRƒ“Â“bÂ"’“°¢G&uFW‡B‡‚²3‚ÂÇ’ÂÆ–æRÂÂ&v#ScRƒSÂ##Âƒ’“°¢Ç’³Òƒ°¢–b†Ç’â’²‚Òb’'&V³°¢Ð ¢f–ÆÅ&÷VæE&V7B„…uô$4µõ‚Â…uô5Eõ’Â…uô5EõrÂ…uô5Eô‚Â"Â&v#ScRƒsÂsBÂ“’“°¢G&uFW‡D2„…uô$4µõ‚²…uô5Eõrò"Â…uô5Eõ’²BÂ%föÇfW""Â"Â&v#ScRƒ#SRÂ#SRÂ#SR’“°¢f–ÆÅ&÷VæE&V7B„…uô4õ•õ‚Â…uô5Eõ’Â…uô5EõrÂ…uô5Eô‚Â"Â‡t6÷–VBò&v#ScRƒCbÂcÂ“’¢&v#ScRƒcÂÂ#3R’“°¢G&uFW‡D2„…uô4õ•õ‚²…uô5Eõrò"Â…uô5Eõ’²BÂ‡t6÷–VBò$6÷–Fò"¢$6÷–""Â"Â&v#ScRƒ#SRÂ#SRÂ#SR’“°¢Ð¢fÇ„fÇW6‚…t”åõDõÂt”åô$õB“°§Ð ¢òòF–6²FVÂ6öFR”DS¢vW7F–öæVÂ&÷FöâFVÂVF—F÷"’Æ÷2F÷VW2FVÂ6—7FVçFRà¢òòVÂÖ&6ò†6†Wg&öâöG&2öæb’Æò6–wVR6W'&æFòVÂg&ÖWv÷&²Óâ6–W'&Æà§7FF–2fö–B–FUF–6²‚—°¢–b†‡uv—¦&D7F—fR—°¢–b‚BçF’&WGW&ã°¢–b†‡u6VÄÖöGVÆRÂ—°¢òòf—7FÆ—7F¢FVâVæF&¦WFÓâvVæW&"6öF–vò’6"f—7F6öF–vð¢–çB–G‡5´Ô…ôÔôETÄU5ôDUDT5DTEÓ°¢–çBæ2Ò‡t7F—fTÆ—7B†–G‡2ÂÔ…ôÔôETÄU5ôDUDT5DTB“°¢f÷"†–çB"Ò²"Âæ3²"²²—°¢–çB7’Ò…uôÄ•5Eõ“²"¢…uõ$õuôƒ°¢–b…Bç‚ãÒ…uô4$Eõ‚bbBç‚ÃÒ…uô4$Eõ‚²…uô4$EõrbbBç’ãÒ7’bbBç’ÃÒ7’²…uô4$Eô‚—°¢‡u6VÄÖöGVÆRÒ–G‡5·%Ó²‡t6÷–VBÒfÇ6S°¢‡tvVä6öFR‚fFWFV7FVDÖöGVÆW5¶‡u6VÄÖöGVÆUÒ“°¢‡tG&uv—¦&B‚“°¢&WGW&ã°¢Ð¢Ð¢òò6W'&"ÓâföÇfW"ÂVF—F÷ ¢–b…Bç‚ãÒ…uô4Äõ4Uõ‚bbBç‚ÃÒ…uô4Äõ4Uõ‚²…uô4Äõ4UõrbbBç’ãÒ…uô4Äõ4Uõ’bbBç’ÃÒ…uô4Äõ4Uõ’²…uô4Äõ4Uô‚—°¢‡uv—¦&D7F—fRÒfÇ6S°¢–FTVçFW"‚“°¢&WGW&ã°¢Ð¢ÒVÇ6R°¢òòf—7F6öF–vó¢föÇfW ¢–b…Bç‚ãÒ…uô$4µõ‚bbBç‚ÃÒ…uô$4µõ‚²…uô5EõrbbBç’ãÒ…uô5Eõ’bbBç’ÃÒ…uô5Eõ’²…uô5Eô‚—°¢‡u6VÄÖöGVÆRÒÓ²‡t6÷–VBÒfÇ6S²‡tG&uv—¦&B‚“°¢&WGW&ã°¢Ð¢òò6÷–"Â÷'FVÆW2vÆö&À¢–b…Bç‚ãÒ…uô4õ•õ‚bbBç‚ÃÒ…uô4õ•õ‚²…uô5EõrbbBç’ãÒ…uô5Eõ’bbBç’ÃÒ…uô5Eõ’²…uô5Eô‚—°¢7G&æ7’†6Æ—&ö&BÂ‡t6öFRÂ6—¦Vöb†6Æ—&ö&B’Ò“°¢6Æ—&ö&E·6—¦Vöb†6Æ—&ö&B’ÒÒÒ°¢‡t6÷–VBÒG'VS²‡tG&uv—¦&B‚“°¢&WGW&ã°¢Ð¢Ð¢&WGW&ã°¢Ð¢òòVF—F÷#¢'&—"VÂ6—7FVçFP¢–b…BçFbbBç‚ãÒ…uô%Dåõ‚bbBç‚ÃÒ…uô%Dåõ‚²…uô%DåõrbbBç’ãÒ…uô%Dåõ’bbBç’ÃÒ…uô%Dåõ’²…uô%Dåô‚—°¢‡uv—¦&D7F—fRÒG'VS²‡u6VÄÖöGVÆRÒÓ²‡t6÷–VBÒfÇ6S°¢‡tG&uv—¦&B‚“°¢Ð§Ð  ¢òòÒÒÒÒ–çC¢Æ–Vç¦ò6öâF–'V¦òF7F–Â&VÂÒÒÒÐ¢6FVf–æRõDõ“`¢6FVf–æRô$õB…45%ô‚Òcb§7FF–2V–çCe÷B6öÆ÷"Ò°§7FF–2–çB&We‚ÒÓÂ&We’ÒÓÂ6—¦RÒC°§7FF–26öç7BV–çCe÷BõÅ³eÒÒ²&v#ScRƒ3Ã3ÃC’Â&v#ScRƒ#3ÃcÃc’Â&v#ScRƒ#CÃSÃC’À¢&v#ScRƒ#CÃ#ÃS’Â&v#ScRƒƒÃƒÃ#’Â&v#ScRƒcÃ#Ã#3R’Ó°§7FF–2fö–B–çEFööÇ2‚—°¢6WD'Vb†f"“°¢–çB’Ò45%ô‚ÒSbÂ7rÒC"ÂvÒ‚Â‚Òc°¢f–ÆÅ&V7BƒÂô$õBÂ45%õrÂ45%ô‚Òô$õBÂ&v#ScRƒ‚Ã#Ã#‚’“°¢f÷"†–çB’Ò²’Âc²’²²—°¢–çB7‚Ò‚²’¢‡7r²v’²7rò#°¢f–ÆÄ6—&6ÆR†7‚Â’²7rò"Â7rò"Ò"ÂõÅ¶•Ò“°¢–b…õÅ¶•ÒÓÒ6öÆ÷"—²G&t6—&6ÆR†7‚Â’²7rò"Â7rò"Â&v#ScRƒ#SRÃ#SRÃ#SR’“²G&t6—&6ÆR†7‚Â’²7rò"Â7rò"ÒÂ&v#ScRƒ#SRÃ#SRÃ#SR’“²Ð¢Ð¢f–ÆÅ&÷VæE&V7B…45%õrÒ“"Â’²"Âs‚ÂCÂÂ&v#ScRƒcÃcBÃs‚’“°¢G&uFW‡D2…45%õrÒS2Â’²"Â$Æ–×–""Â"Â&v#ScRƒ#CÃ#C"Ã#C‚’“°¢fÇ„fÇW6‚…ô$õBÂ45%ô‚Ò“°§Ð§7FF–2fö–B–çDVçFW"‚—°¢6WD'Vb†f"“²f–ÆÅ&V7BƒÂÂ45%õrÂ45%ô‚Â&v#ScRƒbÃ‚Ã#b’“°¢7G&ö¶U6Vtƒ3Â#bÂ‚Â‚Â"ãFbÂ&v#ScRƒ#SRÃ#SRÃ#SR’“°¢7G&ö¶U6Vtƒ‚Â‚Â3ÂÂ"ãFbÂ&v#ScRƒ#SRÃ#SRÃ#SR’“°¢G&uFW‡D2…45%õrò"ÂBÂ%–çB"Â2Â&v#ScRƒ#SRÃ#SRÃ#SR’“°¢f–ÆÅ&V7Bƒ‚ÂõDõÂ45%õrÒbÂô$õBÒõDõÂ&v#ScRƒ#SÃ#SÃ#S"’“²òòÆ–Vç¦ð¢6öÆ÷"ÒõÅ³Ó²&We‚Ò&We’ÒÓ°¢–çEFööÇ2‚“°¢fÇ„fÇW6„ÆÂ‚“°§Ð§7FF–2fö–B–çEF–6²‚—°¢–b…BæF÷vâbbBç’ãÒõDõbbBç’ÃÒô$õBbbBç‚ãÒ‚bbBç‚ÃÒ45%õrÒ‚—°¢6WD'Vb†f"“°¢–çB“Â“°¢–b‡&We‚ãÒ—²7G&ö¶U6Vr‡&We‚Â&We’ÂBç‚ÂBç’Â6—¦RÂ6öÆ÷"“²“ÒÖ–â‡&We’ÂBç’“²“ÒÖ‚‡&We’ÂBç’“²Ð¢VÇ6R²f–ÆÄ6—&6ÆR…Bç‚ÂBç’Â6—¦RÂ6öÆ÷"“²“Ò“ÒBç“²Ð¢&We‚ÒBçƒ²&We’ÒBç“°¢fÇ„fÇW6‚‡“Ò6—¦RÒÂ“²6—¦R²“°¢&WGW&ã°¢Ð¢–b‚BæF÷vâ—²&We‚Ò&We’ÒÓ²Ð¢–b…BçF—°¢–b…Bç‚ÂC‚bbBç’ÂC‚—²6Æ÷6R‚“²&WGW&ã²Ð¢–çB’Ò45%ô‚ÒSbÂ7rÒC"ÂvÒ‚Â‚Òc°¢f÷"†–çB’Ò²’Âc²’²²—²–çB7‚Ò‚²’¢‡7r²v’²7rò#²–b…Bç‚ãÒ7‚Ò7rò"bbBç‚ÃÒ7‚²7rò"bbBç’ãÒ’bbBç’ÃÒ’²7r—²6öÆ÷"ÒõÅ¶•Ó²–çEFööÇ2‚“²&WGW&ã²ÒÐ¢–b…Bç‚ãÒ45%õrÒ“"bbBç’ãÒ’—²6WD'Vb†f"“²f–ÆÅ&V7Bƒ‚ÂõDõÂ45%õrÒbÂô$õBÒõDõÂ&v#ScRƒ#SÃ#SÃ#S"’“²fÇ„fÇW6‚…õDõÂô$õB“²&WGW&ã²Ð¢Ð§Ð ¢òò2222222222222222222222222222222222222222222222222222222222220¢òò225t•D4„U"„×VÇF—F&V’+r6''W6VÂ†÷&—¦öçFÂW7F–Æò”õ0¢òò22F&¦WF26öâÖ–æ’Ö6GW&Vâ5$Òâ'&7G&R²–æW&6–À¢òò227v—RÖ'&–&&6W'&"†g&VR’ÂF÷VR&Ö†–Ö—¦"à¢òò2222222222222222222222222222222222222222222222222222222222220¢6FVf–æR5uôÔ‚`¢6FVf–æRD…õrcp¢6FVf–æRD…ô‚#S ¢6FVf–æR5uô5r#c€¢6FVf–æR5uô4‚C3 ¢6FVf–æR5uõ5DU#ƒ€¢6FVf–æR5uõDõS  §7G'V7BF6²²V–çC…÷B”C²&ööÂW6VC²V–çCe÷B¢F‡VÖ#²Ó²òòW7FFò7W7VæF–Fò²Ö–æ–GW&§7FF–2F6²7uF6·5µ5uôÔ…Ó°§7FF–2–çB7t6÷VçBÒ°§7FF–2fÆöB7u67&öÆÅ‚ÒÂ7ufVÂÒÂ7tÆ–gE’Ò°§7FF–2–çB7tÆ–gD6&BÒÓÂ7tvW7GW&RÒ²òòæFÂ†÷&—¦öçFÂÂ"fW'F–6À§7FF–2fÆöB7u7F'E‚Â7u7F'E’Â7tÆ7Eƒ"Â7tÆ7E“#° §7FF–2V–çCe÷B¢7tÆÆö5F‡VÖ"‚—²&WGW&â‡V–çCe÷B¢–†Vö65öÖÆÆö2‚‡6—¦U÷B•D…õr¢D…ô‚¢"ÂÔÄÄô5ô4õ5•$ÒÂÔÄÄô5ô4ó„$•B“²Ð§7FF–2fö–B6GW&UF‡VÖ"‡V–çCe÷B¢G7B—²òò&VGV6Rf"S37ƒƒÓâcwƒ#S6–âFVf÷&Ö ¢f÷"†–çB¢Ò²¢ÂD…ôƒ²¢²²—°¢–çB7’Ò¢¢45%ô‚òD…ôƒ²V–çCe÷B¢BÒG7B²‡6—¦U÷B–¢¢D…õs°¢f÷"†–çB’Ò²’ÂD…õs²’²²’E¶•ÒÒf%²‡6—¦U÷B—7’¢45%õr²’¢45%õròD…õuÓ°¢Ð§Ð§7FF–2fö–B7uW6‚‡V–çC…÷B–B—²òò×VWfRÂg&VçFR†ò–ç6W'F¢–çBBÒÓ²f÷"†–çB’Ò²’Â7t6÷VçC²’²²’–b‡7uF6·5¶•Òæ”BÓÒ–B—²BÒ“²'&V³²Ð¢–b†BãÒ—²F6²F×Ò7uF6·5¶EÓ²f÷"†–çB’ÒC²’â²’ÒÒ’7uF6·5¶•ÒÒ7uF6·5¶’ÒÓ²7uF6·5³ÒÒF×²&WGW&ã²Ð¢–b‡7t6÷VçBÂ5uôÔ‚—²f÷"†–çB’Ò7t6÷VçC²’â²’ÒÒ’7uF6·5¶•ÒÒ7uF6·5¶’ÒÓ²7t6÷VçB²³²Ð¢VÇ6R²–b‡7uF6·5µ5uôÔ‚ÒÒçF‡VÖ"’g&VR‡7uF6·5µ5uôÔ‚ÒÒçF‡VÖ"“²f÷"†–çB’Ò5uôÔ‚Ò²’â²’ÒÒ’7uF6·5¶•ÒÒ7uF6·5¶’ÒÓ²Ð¢7uF6·5³Òæ”BÒ–C²7uF6·5³ÒçW6VBÒG'VS²7uF6·5³ÒçF‡VÖ"ÒåTÄÃ°§Ð§7FF–2fö–B7uW6„æD6GW&R‡V–çC…÷B–B—°¢7uW6‚†–B“°¢–b‚7uF6·5³ÒçF‡VÖ"’7uF6·5³ÒçF‡VÖ"Ò7tÆÆö5F‡VÖ"‚“°¢–b‡7uF6·5³ÒçF‡VÖ"’6GW&UF‡VÖ"‡7uF6·5³ÒçF‡VÖ"“°§Ð¢òò&2VRF–'V¦âVâÄäE44R„ÖöFò2Â§VVv÷2“¢6GW&UF‡VÖ"&VGV6Rf ¢òòFÂ7VÂÂ’f"6öçF–VæRVÂg&ÖR–$õDDòâwV&F&ÆòF&–VæÖ–æ–GW&¢òòv—&F“FVçG&òFRVæF&¦WFfW'F–6ÂÒÒVRW2§W7FòÆòVR6RfV–VâVÀ¢òò6VÆV7F÷"FR&V6–VçFW2âV’6RVçG&VâÆÆ—7F4”âÖ–æ–GW&‡’6RF—&Æ¢òòVR‡V&–W&ÂVR6W&–FRVæ6W6–öâçFW&–÷"’Â6’7u&VæFW$6&G26R7P¢òò&W7ÆFó¢F&¦WF²–6öæòFRÆÂVR6’W7F&–Vâ÷&–VçFFòà§7FF–2fö–B7uW6„æõF‡VÖ"‡V–çC…÷B–B—°¢7uW6‚†–B“°¢–b‡7uF6·5³ÒçF‡VÖ"—²g&VR‡7uF6·5³ÒçF‡VÖ"“²7uF6·5³ÒçF‡VÖ"ÒåTÄÃ²Ð§Ð§7FF–2fö–B7t6Æ÷6T6&B†–çB–G‚—²òòÆ–&W&5$Ò’&V÷&FVæ¢–b†–G‚ÂÇÂ–G‚ãÒ7t6÷VçB’&WGW&ã°¢–b‡7uF6·5¶–G…ÒçF‡VÖ"—²g&VR‡7uF6·5¶–G…ÒçF‡VÖ"“²7uF6·5¶–G…ÒçF‡VÖ"ÒåTÄÃ²Ð¢f÷"†–çB’Ò–Gƒ²’Â7t6÷VçBÒ²’²²’7uF6·5¶•ÒÒ7uF6·5¶’²Ó°¢7t6÷VçBÒÓ°§Ð§7FF–2V–çCe÷B7u7„ÅUEµ5uô5uÒÂ7u7”ÅUEµ5uô4…Ó²7FF–2&ööÂ7tÅUFFöæRÒfÇ6S°§7FF–2fö–B7t'V–ÆDÅUB‚—°¢–çBGrÒ5uô5rÒbÂF‚Ò5uô4‚ÒS#°¢f÷"†–çB’Ò²’ÂGs²’²²’7u7„ÅUE¶•ÒÒ‡V–çCe÷B’†’¢D…õròGr“°¢f÷"†–çB¢Ò²¢ÂFƒ²¢²²’7u7”ÅUE¶¥ÒÒ‡V–çCe÷B’†¢¢D…ô‚òF‚“°¢7tÅUFFöæRÒG'VS°§Ð§7FF–2fö–B&Æ—EF‡VÖ%66ÆVB‡V–çCe÷B¢F‚Â–çBG‚Â–çBG’Â–çBGrÂ–çBF‚—°¢&ööÂÇWBÒ†GrÓÒ5uô5rÒbbbF‚ÓÒ5uô4‚ÒS"“²òò'WF&–F‡FÖæòf–¦ò¢–b†ÇWBbb7tÅUFFöæR’7t'V–ÆDÅUB‚“°¢f÷"†–çB¢Ò²¢ÂFƒ²¢²²—²–çB—’ÒG’²£²–b‚‡Vç6–væVB——’ãÒ45%ô‚’6öçF–çVS°¢–çB7’ÒÇWBò7u7”ÅUE¶¥Ò¢¢¢D…ô‚òFƒ°¢V–çCe÷B¢2ÒF‚²‡6—¦U÷B—7’¢D…õs²V–çCe÷B¢BÒt'Vb²‡6—¦U÷B——’¢45%õs°¢–çBƒÒG‚Âò¢G‚ÂƒÒG‚²Grâ45%õrò45%õr¢G‚²Gs°¢f÷"†–çB‡‚Òƒ²‡‚Âƒ²‡‚²²’E·‡…ÒÒ5¶ÇWBò7u7„ÅUE·‡‚ÒG…Ò¢‡‡‚ÒG‚’¢D…õròGuÓ°¢Ð§Ð¢òòÖ&6òF—òf–G&–ò†&&Fó¢6ö'&RVÂföæFò÷67W&òVæ–f÷&ÖRVÂ&ÇW"æò÷'FÀ¢òò6’VÂ6''W6VÂ6÷'&RfÇV–Fò’âG&tÆ—V–DvÆ75æVÂ6R&W6W'f&7WW&f–6–W26öâ6öçFVæ–FòFWG&2à§7FF–2fö–B7t6&Dg&ÖR†–çB‚Â–çB’Â–çBrÂ–çB‚Â–çB&B—°¢f–ÆÅ&÷VæE&V7B‡‚Â’ÂrÂ‚Â&BÂ&v#ScRƒ#bÃ3ÃCB’“°¢G&u&÷VæE&V7B‡‚Â’ÂrÂ‚Â&BÂ&v#ScRƒ“RÃRÃ3‚’“°§Ð§7FF–2fö–B7u&VæFW"†fÆöB66ÆR—²òò6ö×ÆWFò‡6öÆòæ–Ö6–öâFRVçG&F¢6WD'Vb†&'Vb“°¢–b†&ÇW$&r’ÖVÖ7’†&'VbÂ&ÇW$&rÂ‡6—¦U÷B•45%õr¢45%ô‚¢"“²VÇ6Rf–ÆÅ&V7BƒÂÂ45%õrÂ45%ô‚Â&v#ScRƒ‚ÃÃb’“°¢G&uFW‡D2…45%õrò"ÂbÂ%&V6–VçFW2"Â2Â&v#ScRƒ#CÃ#CBÃ#S"’“°¢–b‡7t6÷VçBÓÒ’G&uFW‡D2…45%õrò"Â45%ô‚ò"Â%6–â2&V6–VçFW2"Â"Â&v#ScRƒSÃS‚Ãƒ’“°¢–çB7rÒ†–çB’…5uô5r¢66ÆR’Â6‚Ò†–çB’…5uô4‚¢66ÆR“°¢f÷"†–çB’Ò²’Â7t6÷VçC²’²²—°¢–çB7‚Ò45%õrò"²’¢5uõ5DUÒ†–çB—7u67&öÆÅƒ°¢–b†7‚ÂÕ5uô5rÇÂ7‚â45%õr²5uô5r’6öçF–çVS°¢–çB‚Ò7‚Ò7rò"Â’Ò5uõDõ²…5uô4‚Ò6‚’ò#°¢–b†’ÓÒ7tÆ–gD6&B’’ÓÒ†–çB—7tÆ–gE“°¢7t6&Dg&ÖR‡‚Â’Â7rÂ6‚Â#"“°¢–b‡7uF6·5¶•ÒçF‡VÖ"’&Æ—EF‡VÖ%66ÆVB‡7uF6·5¶•ÒçF‡VÖ"Â‚²‚Â’²‚Â7rÒbÂ6‚ÒS"“°¢VÇ6R²f–ÆÅ&÷VæE&V7B‡‚²‚Â’²‚Â7rÒbÂ6‚ÒS"ÂBÂ&v#ScRƒ#‚Ã3"ÃCB’“²G&t–6öâ‡7uF6·5¶•Òæ”BÂ‚²7rò"Ò3Â’²6‚ò"ÒsÂc“²Ð¢G&uFW‡D2‡‚²7rò"Â’²6‚Ò3"ÂæÖR‡7uF6·5¶•Òæ”B’Â"Â&v#ScRƒ#SRÃ#SRÃ#SR’“°¢Ð¢G&uFW‡D2…45%õrò"Â45%ô‚Ò#‚Â$FW6Æ—¦VæF&¦WF'&–&&6W'&""ÂÂ&v#ScRƒ3Ã3‚ÃS‚’“°¢&W6VçBƒÂ45%ô‚Ò“°§Ð¢òò÷"Ög&ÖS¢4ôÄò&W–çF’gVVÆ6Æ&æFFRÆ2F&¦WF2†×V6†òÖ2Æ–vW&ò§7FF–2fö–B7u&VæFW$6&G2‚—°¢6WD'Vb†&'Vb“°¢–b†&ÇW$&r—²f÷"†–çB¢Ò3#²¢ÂcC²¢²²’ÖVÖ7’†&'Vb²‡6—¦U÷B–¢¢45%õrÂ&ÇW$&r²‡6—¦U÷B–¢¢45%õrÂ45%õr¢"“²Ð¢VÇ6Rf–ÆÅ&V7BƒÂ3"Â45%õrÂSs"Â&v#ScRƒ‚ÃÃb’“°¢–b‡7t6÷VçBÓÒ’G&uFW‡D2…45%õrò"Â45%ô‚ò"Â%6–â2&V6–VçFW2"Â"Â&v#ScRƒSÃS‚Ãƒ’“°¢f÷"†–çB’Ò²’Â7t6÷VçC²’²²—°¢–çB7‚Ò45%õrò"²’¢5uõ5DUÒ†–çB—7u67&öÆÅƒ°¢–b†7‚ÂÕ5uô5rÇÂ7‚â45%õr²5uô5r’6öçF–çVS°¢–çB‚Ò7‚Ò5uô5rò"Â’Ò5uõDõ°¢–b†’ÓÒ7tÆ–gD6&B’’ÓÒ†–çB—7tÆ–gE“°¢7t6&Dg&ÖR‡‚Â’Â5uô5rÂ5uô4‚Â#"“°¢–b‡7uF6·5¶•ÒçF‡VÖ"’&Æ—EF‡VÖ%66ÆVB‡7uF6·5¶•ÒçF‡VÖ"Â‚²‚Â’²‚Â5uô5rÒbÂ5uô4‚ÒS"“°¢VÇ6R²f–ÆÅ&÷VæE&V7B‡‚²‚Â’²‚Â5uô5rÒbÂ5uô4‚ÒS"ÂBÂ&v#ScRƒ#‚Ã3"ÃCB’“²G&t–6öâ‡7uF6·5¶•Òæ”BÂ‚²5uô5rò"Ò3Â’²5uô4‚ò"ÒsÂc“²Ð¢G&uFW‡D2‡‚²5uô5rò"Â’²5uô4‚Ò3"ÂæÖR‡7uF6·5¶•Òæ”B’Â"Â&v#ScRƒ#SRÃ#SRÃ#SR’“°¢Ð¢&W6VçBƒ3"ÂcB“°§Ð§7FF–2–çB7t6&D–æFW„B†–çB‚—°¢f÷"†–çB’Ò²’Â7t6÷VçC²’²²—²–çB7‚Ò45%õrò"²’¢5uõ5DUÒ†–çB—7u67&öÆÅƒ²–b‡‚ãÒ7‚Ò5uô5rò"bb‚ÃÒ7‚²5uô5rò"’&WGW&â“²Ð¢&WGW&âÓ°§Ð§7FF–2–çB7t6VçFW$–æFW‚‚—²–çB’Ò†–çB—&÷VæFb‡7u67&öÆÅ‚ò5uõ5DU“²–b†’Â’’Ò²–b†’ãÒ7t6÷VçB’’Ò7t6÷VçBÒ²&WGW&â“²Ð§7FF–2fö–B7tW†—EFô†öÖR‚—²u7FFRÒ5Eô„ôÔS²&VæFW$†öÖR‚“²6†÷t†öÖR‚“²Ð§7FF–2fö–B7tÖ†–Ö—¦R†–çB–G‚—²–b†–G‚ãÒbb–G‚Â7t6÷VçB’VçFW$‡7uF6·5¶–G…Òæ”B“²Òòò&W7FW&çFÆÆ6ö×ÆWF ¢òò6öævVÆÆ7F—fÂ6Ö&–ÔôDõôÕTÅD•D$T’†6RÆæ–Ö6–öâVÆ7F–6FRVçG&Fà§7FF–2fö–B7F—f$×VÇF—F&V‚—°¢–b„´”õ4µôôâbb¶–÷6´öâ’&WGW&ã²òòd4RC¢6–â6VÆV7F÷"FR2Vâ¶–÷66ð¢–b†t†÷7FVB—²t†÷7E&WÒ3²&WGW&ã²ÒòòÓâ&V6–VçFW2FRFU€¢òò&VBFR6VwW&–FB–wVÂVRVâ6Æ÷6S¢VÂ6VÆV7F÷"6RF–'V¦Vâ÷'G&—Bà¢òò6’6RÆÆVv&V’6öâtÆæC×G'VRÂÆ2F&¦WF26ÆG&–â&÷FF2’ÖVF–2à¢tÆæBÒfÇ6S°¢t6Æ—“Ò²t6Æ—“Ò45%ô‚Ò°¢t6Æ—ƒÒ²t6Æ—ƒÒ45%õrÒ°¢6WD'Vb†f"“°¢Vç7W&T&ÇW$&r‚“°¢u7FFRÒ5Eõ5t•D4„U#°¢7u67&öÆÅ‚Ò²7ufVÂÒ²7tÆ–gD6&BÒÓ²7tÆ–gE’Ò²7tvW7GW&RÒ°¢f÷"†–çB2Ò²2ÃÒ²2²²—²òò7&–ær66ÆRÖ–â†V6RÖ÷WBÖ&6²¢fÆöBÒ2òãbÂÒÒÒãbÂVö"Òãb²"ãfb¢Ò¢Ò¢Ò²ãfb¢Ò¢Ó°¢7u&VæFW"ƒãfb²ãFb¢Vö"“²FVÆ’ƒB“°¢Ð¢7u&VæFW"ƒãb“°§Ð§7FF–2fö–B7uF–6²‚—°¢–b…Bç&W76VB—°¢7u7F'E‚ÒBçƒ²7u7F'E’ÒBç“²7tÆ7Eƒ"ÒBçƒ²7tÆ7E“"ÒBç“²7ufVÂÒ²7tvW7GW&RÒ°¢7tÆ–gD6&BÒ7t6&D–æFW„B…Bç‚“²7tÆ–gE’Ò°¢&WGW&ã°¢Ð¢–b…BæF÷vâ—°¢fÆöBG‚ÒBç‚Ò7tÆ7Eƒ"ÂG’ÒBç’Ò7tÆ7E“#²‡fö–B–G“°¢–b‡7tvW7GW&RÓÒ—°¢–b†f'6b…Bç‚Ò7u7F'E‚’â"’7tvW7GW&RÒ°¢VÇ6R–b†f'6b…Bç’Ò7u7F'E’’âB’7tvW7GW&RÒ#°¢Ð¢–b‡7tvW7GW&RÓÒ—²òò67&öÆÂ†÷&—¦öçFÂ²fVÆö6–F@¢7u67&öÆÅ‚ÓÒGƒ²7ufVÂÒÖGƒ°¢fÆöBÖâÒÓ“Â×‚Ò‡7t6÷VçBÒ’¢5uõ5DU²“²–b‡7u67&öÆÅ‚ÂÖâ’7u67&öÆÅ‚ÒÖã²–b‡7u67&öÆÅ‚â×‚’7u67&öÆÅ‚Ò×ƒ°¢7tÆ–gE’Ò²7u&VæFW$6&G2‚“°¢ÒVÇ6R–b‡7tvW7GW&RÓÒ"bb7tÆ–gD6&BãÒ—²òòÆWfçF"F&¦WF†6W'&"¢7tÆ–gE’Ò7u7F'E’ÒBç“²–b‡7tÆ–gE’Â’7tÆ–gE’Ò²–b‡7tÆ–gE’âb’7tÆ–gE’Òc²7u&VæFW$6&G2‚“°¢Ð¢7tÆ7Eƒ"ÒBçƒ²7tÆ7E“"ÒBç“°¢&WGW&ã°¢Ð¢–b…Bç&VÆV6VB—°¢–b‡7tvW7GW&RÓÒ"bb7tÆ–gD6&BãÒbb7tÆ–gE’â—²òò7v—RÖ'&–&Óâ6W'&"†g&VR¢7t6Æ÷6T6&B‡7tÆ–gD6&B“²7tÆ–gD6&BÒÓ²7tÆ–gE’Ò°¢fÆöB×‚Ò‡7t6÷VçBâò‡7t6÷VçBÒ’¢5uõ5DU¢“²–b‡7u67&öÆÅ‚â×‚’7u67&öÆÅ‚Ò×ƒ²–b‡7u67&öÆÅ‚Â’7u67&öÆÅ‚Ò°¢7u&VæFW$6&G2‚“²&WGW&ã°¢Ð¢–b‡7tvW7GW&RÓÒ—²òòF÷VP¢–b…Bç’â45%ô‚Òc—²7tW†—EFô†öÖR‚“²&WGW&ã²Ð¢–çB–G‚Ò7t6&D–æFW„B…Bç‚“°¢–b†–G‚ãÒ—²–b†–G‚ÓÒ7t6VçFW$–æFW‚‚’’7tÖ†–Ö—¦R†–G‚“²VÇ6R²7u67&öÆÅ‚Ò–G‚¢5uõ5DU²7u&VæFW$6&G2‚“²Ò&WGW&ã²Ð¢7tW†—EFô†öÖR‚“²&WGW&ã°¢Ð¢7tÆ–gD6&BÒÓ²7tÆ–gE’Ò°¢&WGW&ã°¢Ð¢òò&W÷6ó¢–æW&6–²Vævæ6†RVÆ7F–6òÆF&¦WFÖ26W&6æ¢–b†f'6b‡7ufVÂ’âãFb—°¢7u67&öÆÅ‚³Ò7ufVÃ²7ufVÂ£Òã“c°¢fÆöB×‚Ò‡7t6÷VçBâò‡7t6÷VçBÒ’¢5uõ5DU¢“°¢–b‡7u67&öÆÅ‚Â—²7u67&öÆÅ‚Ò²7ufVÂÒ²Ò–b‡7u67&öÆÅ‚â×‚—²7u67&öÆÅ‚Ò×ƒ²7ufVÂÒ²Ð¢7u&VæFW$6&G2‚“°¢ÒVÇ6R°¢–çBFwBÒ7t6VçFW$–æFW‚‚’¢5uõ5DU°¢–b‚†–çB—7u67&öÆÅ‚ÒFwB—²7u67&öÆÅ‚³Ò‡FwBÒ7u67&öÆÅ‚’¢ã#Vc²–b†f'6b‡FwBÒ7u67&öÆÅ‚’Â’7u67&öÆÅ‚ÒFwC²7u&VæFW$6&G2‚“²Ð¢Ð§Ð ¢òò2222222222222222222222222222222222222222222222222222222222220¢òò224TuU$”DBÓâ$ÄõTTò…”âò6öçG&6\;¢òò22FöFò6ö×öæRVâ&'Vb’&W6VçFFRVæfW¢†çF’ÖfÆ–6¶W"’à¢òò2222222222222222222222222222222222222222222222222222222222220¦VçVÒ²Å5Uõ4TÂÒÂÅ5Uõ”âÂÅ5Uõ52Ó°§7FF–2–çBÇ7TÖöFRÒÅ5Uõ4TÃ°§7FF–26†"Ç7U–å³%ÒÒ""ÂÇ7U75³cEÒÒ"#°§7FF–2–çBÇ7U&W72ÒÓ²7FF–2V–çC3%÷BÇ7U&W74×2Ò°§7FF–2V–çC3%÷BÇ7T¶$æ–ÒÒ²òòÖ–ÆÆ—2Â'&—"VÂFV6ÆFò‡&VÂ6Æ–FRFRã72§7FF–2V–çC3%÷BÇ7Tæ–Ô×2Ò°§7FF–26öç7B6†"¢”åô´U•5³%ÒÒ²#"Â#""Â#2"Â#B"Â#R"Â#b"Â#r"Â#‚"Â#’"Â#Â"Â#"Â$ô²"Ó°§7FF–2&ööÂÇ7UfW&–g’ÒfÇ6S²òòG'VRÒFW6&Æ÷VV"‡fW&–f–6"’ÂfÇ6RÒ7&V §7FF–26†"Ç7U6fVE³cEÒÒ"#²òò6ÆfRwV&FF6ö×& §7FF–2V–çC3%÷BÇ7Uw&öærÒ²òòÖ–ÆÆ—2FVÂVÇF–ÖòW'&÷"‡&VÂfÆ6‚&ö¦ò¢òòÒÒÒÒFVÖFRÆ2çFÆÆ2FR6ÆfR‡&WWF–Æ—¦ÆÆWFFR§W7FW2’ÒÒÒÐ¢òò$TtÄ¢tF&²6öÆòÖæF7VæFòVÂföæFòW2VÂ6öÆ÷"4ôÄ”DòFRv–æÂò6V¢òòÂ5$T"Æ6ÆfRâÂdU$”d”4"ÂVÂföæFòW2VÂvÆÇW"FW6Væfö6FòÒÒVæ¢òòf÷FòÒÒ’†’VÂÖöFò6Æ&òöæG&–FW‡Fò÷67W&ò’F&¦WF2&Ææ62Væ6–ÖVP¢òòæò6RÆVW&–ââVâW6R66ò6R6öç6W'fâW†7FÖVçFRÆ÷26öÆ÷&W2FR6–V×&Rà¢òòVÂV”vÆ72ÂVâ6Ö&–òÂ–6R&W7WF&’6R6–wVR&W7WFæFòVâÆ÷2F÷266÷2à§7FF–2V–çCe÷BÇ7T&t6öÂ‚’²&WGW&âÇ7UfW&–g’ò&v#ScRƒ"ÃBÃ#"’¢tUô$s²Ð§7FF–2V–çCe÷BÇ7T6&D6öÂ‚’²&WGW&âÇ7UfW&–g’ò&v#ScRƒCBÃSBÃ“"’¢4UEô4$Eô$s²Ð§7FF–2V–çCe÷BÇ7TvÆ746öÂ‚—²&WGW&âÇ7UfW&–g’ò&v#ScRƒC‚ÃcÃ’¢4UEô4$EôtÄ53²Ð§7FF–2V–çCe÷BÇ7T¶$&t6öÂ‚’²&WGW&âÇ7UfW&–g’ò&v#ScRƒ‚Ã#Ã#‚’¢tUô$s²Ð§7FF–2V–çCe÷BÇ7T¶$vÆ72‚’²&WGW&âÇ7UfW&–g’ò&v#ScRƒ3bÃCÃS‚’¢4UEô4$EôtÄ53²Ð§7FF–2V–çCe÷BÇ7T¶W”6öÂ‚’²&WGW&âÇ7UfW&–g’ò&v#ScRƒS"ÃSbÃs’¢4UEô4$Eô$s²Ð§7FF–2V–çCe÷BÇ7UG‡D†’‚’²&WGW&âÇ7UfW&–g’ò&v#ScRƒ#SRÃ#SRÃ#SR’¢4UEõE…Eô„“²Ð§7FF–2V–çCe÷BÇ7UG‡DÆò‚’²&WGW&âÇ7UfW&–g’ò&v#ScRƒSÃS‚Ãƒ’¢4UEõE…EôÄó²Ð§7FF–2V–çCe÷BÇ7T¶W•G‡B‚’²&WGW&âÇ7UfW&–g’ò&v#ScRƒ#CÃ#C"Ã#C‚’¢4UEõE…Eô„“²Ð §7FF–2fö–BÇ7T&r‚—°¢–b†Ç7UfW&–g’bb&ÇW$&r’ÖVÖ7’†t'VbÂ&ÇW$&rÂ‡6—¦U÷B•45%õr¢45%ô‚¢"“²òòvÆÇW"&÷'&÷6ð¢VÇ6Rf–ÆÅ&V7BƒÂÂ45%õrÂ45%ô‚ÂÇ7T&t6öÂ‚’“°§Ð ¢òò2222222222222222222222222222222222222222222222222222222222220¢òò22E$å4”4”ôâ$Ud”Â$ÄõTTòDR4TuU$”D@¢òò22ÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÐ¢òò22çFW2ÂVF—"Æ6ÆfRW&Vâ4õ%DR4T4ó¢VÂ7VG&òVR‡V&–W&Và¢òò22çFÆÆ†VÂW67&—F÷&–òÂÆVR6RW7F&&Æ÷VVæFòÂÆçFÆÆFP¢òò22&Æ÷VVò’6R7W7F—GV–FRvöÇR÷"VÂFV6ÆFòFRÆ6ÆfRà¢òò22†÷&6öâF÷2F–V×÷2Â6öÖòVâöæRT’ò”õ3 ¢òò22’Æ–çFW&f¢7GVÂ6RDU5däT4R†6–VÂföæFòFRÆçFÆÆFP¢òò22WFVçF–66–öâ†fFR÷WB“°¢òò22"’VÂÖWFöFòFR6VwW&–FBVRVÂW7V&–òFVæv6öæf–wW&FòÒÒ”âÀ¢òò226öçG&6VæòVÂVR6RæFFW7VW2ÒÒ$T4RVæ6–Ö†fFR–â’à¢òò22f—fRVâVÂ6Ö–æò6ö×Vâ†Ç7U7F'EfW&–g’²Æ2F÷2'WF–æ2VR–çFâVÀ¢òò22&–ÖW"7VG&òFR6FÖWFöFò’Â6’VRDôD2Æ2'WF2VR–FVâ6ÆfRÆ¢òò22†W&VFã¢&Æ÷VV"öFW6&Æ÷VV"VæÂ6Æ—"FVÂ¶–÷66òÂvFò6VwW&ò¢òò22VÂFW6&Æ÷VVòFRÆçFÆÆà¢òò22Æ÷2F÷2gVæF–F÷26öâ÷"D”TÕò†æò÷"çVÖW&òFR6÷2“¢GW&âÆòÖ—6Öð¢òò22f–VÂ6—7FVÖ&–FòòÆVçFòÂ’ÖWFVâFçF÷27VG&÷26öÖòVVFF"VÀ¢òò226ö×÷6—F÷"â6F7VG&ò6RV&Æ–6VçFW&ò6öâ&W6VçB‚’Âò6VVRæð¢òò22VVFR&V6W"ÖVF–2æ’'F–Fòà¢òò2222222222222222222222222222222222222222222222222222222222220¢6FVf–æRUD…ôdDUôõUEôÕ2“ ¢6FVf–æRUD…ôdDUô”åôÕ2#3 §7FF–2V–çCe÷B¢WF…6æÒåTÄÃ²òò–ç7FçFæVFRÆ–çFW&f¢VR6Rf§7FF–2&ööÂWF„fFUVæF–ærÒfÇ6S²òòVÂfFR÷WB–6÷'&–ó¢Fö6VÂfFR–à ¢òògVæF–FòFR6Æ–F†6–VÂföæFòFRÆçFÆÆFR6ÆfRâFV¦&W&FòVÀ¢òòfFR–ââ6’ÆvòæòW7FF—7öæ–&ÆR…5$ÒÂÆæG66RÂ†÷7VFF’æò6P¢òòæ–Ö’ÆfW&–f–66–öâ6–wVR–wVÂVR6–V×&S¢ÆG&ç6–6–öâW2VâF÷&æòÀ¢òòçVæ6Vâ&WV—6—Fò&öFW"–çG&öGV6—"Æ6ÆfRà§7FF–2fö–BWF„fFT÷WB‚—°¢WF„fFUVæF–ærÒfÇ6S°¢–b†t†÷7FVBÇÂtÆæB’&WGW&ã°¢Vç7W&T&ÇW$&r‚“°¢–b‚&ÇW$&r’&WGW&ã°¢–b‚WF…6æ’WF…6æÒ‡V–çCe÷B¢–†Vö65öÖÆÆö2‚‡6—¦U÷B•45%õr¢45%ô‚¢"ÂÔÄÄô5ô4õ5•$ÒÂÔÄÄô5ô4ó„$•B“°¢–b‚WF…6æ’&WGW&ã°¢ÖVÖ7’†WF…6æÂf"Â‡6—¦U÷B•45%õr¢45%ô‚¢"“°¢V–çC3%÷BCÒÖ–ÆÆ—2‚“°¢–çBÆ7BÒÓ°¢f÷"ƒ³²—°¢V–çC3%÷BRÒÖ–ÆÆ—2‚’ÒC²–b†Râ‡V–çC3%÷B”UD…ôdDUôõUEôÕ2’RÒUD…ôdDUôõUEôÕ3°¢fÆöBÒ†fÆöB–Rò†fÆöB”UD…ôdDUôõUEôÕ3°¢Ò¢¢ƒ2ãbÒ"ãb¢“²òò7Vf—¦FòVâÆ2F÷2VçF0¢V–çC…÷BÒ‡V–çC…÷B’‡¢#SRãb“°¢–b‚†–çB–ÓÒÆ7B—²òòVÂ&VÆö¢Vâæò†Ö÷f–FòVÂgVæF–Fð¢–b†RÂ‡V–çC3%÷B”UD…ôdDUôõUEôÕ2—²FVÆ’ƒ“²6öçF–çVS²Ð¢'&V³°¢Ð¢Æ7BÒ°¢f÷"†–çB¢Ò²¢Â45%ôƒ²¢²²—°¢V–çCe÷B¢BÒ&'Vb²‡6—¦U÷B–¢¢45%õs°¢6öç7BV–çCe÷B¢3ÒWF…6æ²‡6—¦U÷B–¢¢45%õs°¢6öç7BV–çCe÷B¢3Ò&ÇW$&r²‡6—¦U÷B–¢¢45%õs°¢f÷"†–çB’Ò²’Â45%õs²’²²’E¶•ÒÒÖ—ƒScR‡3¶•ÒÂ3¶•ÒÂ“°¢Ð¢&W6VçBƒÂ45%ô‚Ò“°¢–b†RãÒ‡V–çC3%÷B”UD…ôdDUôõUEôÕ2’'&V³°¢Ð¢WF„fFUVæF–ærÒG'VS°§Ð¢òògVæF–FòFRVçG&FFVÂÖWFöFòFR6VwW&–FB–6ö×VW7FòVâwF&vWBrà¢òòFWgVVÇfRfÇ6R6’æò†&–G&ç6–6–öâVâ7W'6òÂ&VRVÂÆÆÖçFRV&Æ—VP¢òò7R7VG&ò6öÖò6–V×&Rà§7FF–2&ööÂWF„fFT–â†6öç7BV–çCe÷B¢F&vWB—°¢–b‚WF„fFUVæF–ær’&WGW&âfÇ6S°¢WF„fFUVæF–ærÒfÇ6S°¢–b‚F&vWBÇÂ&ÇW$&r’&WGW&âfÇ6S°¢V–çC3%÷BCÒÖ–ÆÆ—2‚“°¢–çBÆ7BÒÓ°¢f÷"ƒ³²—°¢V–çC3%÷BRÒÖ–ÆÆ—2‚’ÒC²–b†Râ‡V–çC3%÷B”UD…ôdDUô”åôÕ2’RÒUD…ôdDUô”åôÕ3°¢fÆöBÒ†fÆöB–Rò†fÆöB”UD…ôdDUô”åôÕ3°¢Ò¢¢ƒ2ãbÒ"ãb¢“°¢V–çC…÷BÒ‡V–çC…÷B’‡¢#SRãb“°¢–b‚†–çB–ÓÒÆ7B—°¢–b†RÂ‡V–çC3%÷B”UD…ôdDUô”åôÕ2—²FVÆ’ƒ“²6öçF–çVS²Ð¢'&V³°¢Ð¢Æ7BÒ°¢f÷"†–çB¢Ò²¢Â45%ôƒ²¢²²—°¢V–çCe÷B¢BÒ&'Vb²‡6—¦U÷B–¢¢45%õs°¢6öç7BV–çCe÷B¢3Ò&ÇW$&r²‡6—¦U÷B–¢¢45%õs°¢6öç7BV–çCe÷B¢3ÒF&vWB²‡6—¦U÷B–¢¢45%õs°¢f÷"†–çB’Ò²’Â45%õs²’²²’E¶•ÒÒÖ—ƒScR‡3¶•ÒÂ3¶•ÒÂ“°¢Ð¢&W6VçBƒÂ45%ô‚Ò“°¢–b†RãÒ‡V–çC3%÷B”UD…ôdDUô”åôÕ2’'&V³°¢Ð¢ÖVÖ7’†&'VbÂF&vWBÂ‡6—¦U÷B•45%õr¢45%ô‚¢"“²òò7VG&òf–æÂW†7Fò‡6–â&VFöæFV÷2FVÂgVæF–Fò¢&W6VçBƒÂ45%ô‚Ò“°¢&WGW&âG'VS°§Ð ¢òò2222222222222222222222222222222222222222222222222222222222220¢òò22d4RÒ$ÄõTTòtÄô$Â$Tdõ%¤Dð¢òò22†’67VF–FÖ÷'F–wVFÂfÆÆ"Äô4µõ4„´Uôôà¢òò22†"’6öçFF÷"W'6—7FVçFR²W7W&&öw&W6—fÄô4µôd”Å5ôôà¢òò22†2’WFòÖ&Æ÷VVò÷"–æ7F—f–FBUDôÄô4µôôà¢òò22fV’ÂVçG&RÇ7T&r‚’’VÂ&W7FòFVÂÅ5RÂ÷'VRæV6W6—F6öæö6W ¢òò22Ç7TÖöFRöÇ7UfW&–g’†FV6Æ&F÷2§W7Fò'&–&’’ÆòW6âÇ7UF–6²ð¢òò22Ç7UVæÆö6²òÇ7U7F'EfW&–g’†§W7Fò&¦ò’à¢òò2222222222222222222222222222222222222222222222222222222222220 ¢òòÒÒÒÒ†’67VF–F†÷&—¦öçFÂÖ÷'F–wVFÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÐ¢òòVâfÆÆòåTä46R6ö×Væ–66öâVâ'FVòæ’6öâVâ6Ö&–ò''W66òFR6öÆ÷# ¢òò6R6ö×Væ–6Ö÷f–VæFòâVÂöfg6WBW2Væ6Væö–FÂFR26–6Æ÷27W–×Æ—GV@¢òòFV6RÆ–æVÆÖVçFR†7FÂWfÇVFã3×2÷"g&ÖRÓâVæ÷2bg&ÖW2à¢òòÂW‡—&"FWgVVÇfRW†7FÖVçFRÂ6’VRVÂVÇF–Öòg&ÖRFRÆæ–Ö6–öà¢òòFV¦ÆçFÆÆVâ7R6—F–ò6–âæ–æwVâ6ÇFòà¢6FVf–æRÅ5Uõ4„´UôÕ2ƒ ¢6FVf–æRÅ5Uõ4„´UôÕ"ã`¢6FVf–æRÅ5Uõ4„´Uô5”22ã`§7FF–2V–çC3%÷BÇ7U6†¶T×2Ò²òòÖ–ÆÆ—2FR–æ–6–òƒÒV–WFò§7FF–2fö–BÇ7U6†¶U7F'B‚—°¢–b‚Äô4µõ4„´Uôôâ’&WGW&ã°¢Ç7U6†¶T×2ÒÖ–ÆÆ—2‚“°¢–b‚Ç7U6†¶T×2’Ç7U6†¶T×2Ò²òòW2VÂ6VçF–æVÆFR'V–WFò §Ð§7FF–2–çBÇ7U6†¶Töfb‚—°¢–b‚Äô4µõ4„´UôôâÇÂÇ7U6†¶T×2’&WGW&â°¢V–çC3%÷BRÒÖ–ÆÆ—2‚’ÒÇ7U6†¶T×3°¢–b†RãÒ‡V–çC3%÷B”Å5Uõ4„´UôÕ2—²Ç7U6†¶T×2Ò²&WGW&â²Ð¢fÆöBÒ†fÆöB–Rò†fÆöB”Å5Uõ4„´UôÕ3²òòâã¢&WGW&â†–çB’„Å5Uõ4„´UôÕ¢ƒãbÒ’¢6–æb‡¢Å5Uõ4„´Uô5”2¢bã#ƒ3ƒS6b’“°§Ð ¢òòÒÒÒÒ†"’W7W&&öw&W6—fG&2–çFVçF÷2fÆÆ–F÷2ÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÐ¢òòvVöÖWG&–FRÆ¦öæFVÂ6öçFF÷"â6RVâVÂ‡VV6òVR–W†—7FRVçG&RÆ÷0¢òòVçF÷2FVÂ”â‡“ÓS’’Æ&–ÖW&f–ÆFVÂFV6ÆFòçVÖW&–6ò‡“Ó3’Â6¢òòVRæòFæF’6—'fR–wVÂVâÆçFÆÆFR6öçG&6Væà¢6FVf–æRÅuô$äEõ“ƒ ¢6FVf–æRÅuô$äEõ“#s`¢6FVf–æRÅuôÕ4uõ’ƒ`¢6FVf–æRÅuôÕ4s%õ’#`¢6FVf–æRÅuôåTÕõ“##bòò&æFVR6R&W–çF÷"F–ff–æp¢6FVf–æRÅuôåTÕõ“#s@¢6FVf–æRÅuôåTÕõ’#3 §7FF–2V–çC3%÷BÆö6µv—EVçF–ÂÒ²òòÖ–ÆÆ—2VâVRW‡—&ÆW7W&ƒÒæ–æwVæ§7FF–2&ööÂÆö6µVæÇG•6W'fVBÒfÇ6S²òò–6R6ö'&òÆW7W&FVÂ6öçFF÷"7GVÀ§7FF–2&ööÂÆö6µv—E–çFVBÒfÇ6S²òòVÂÖVç6¦Rf–¦ò–W7FVâçFÆÆ§7FF–2–çBÆö6µv—DÆ7E6V2ÒÓ²òòVÇF–ÖòfÆ÷"–çFFò†F–ff–ær §7FF–2V–çC3%÷BÆö6µVæÇG”×2‚—°¢–b†Æö6´f–Ç2ãÒÄô4µôd”Å5ô„$B’&WGW&âÄô4µõt•Eô„$EôÕ3²òòb²ÓâRÖ–à¢–b†Æö6´f–Ç2ãÒÄô4µôd”Å5õ4ôeB’&WGW&âÄô4µõt•Eõ4ôeEôÕ3²òòBÓRÓâ30¢&WGW&â²òòÓ2Óâ6–âW7W&§Ð¢òòÆ&W7F6R†6RVâ–çC3"6öâ6–væò&÷÷6—Fó¢6’6–wVR6–VæFò6÷'&V7F¢òò7VæFòÖ–ÆÆ—2‚’FÆgVVÇFÆ÷2ãC’F–2à§7FF–2&ööÂÆö6µv—D7F—fR‚—°¢–b‚Äô4µôd”Å5ôôâÇÂÆö6µv—EVçF–Â’&WGW&âfÇ6S°¢–b‚†–çC3%÷B’†Ö–ÆÆ—2‚’ÒÆö6µv—EVçF–Â’ãÒ—°¢Æö6µv—EVçF–ÂÒ²Æö6µVæÇG•6W'fVBÒG'VS²òò7V×Æ–F¢&WGW&âfÇ6S°¢Ð¢&WGW&âG'VS°§Ð¢òò&W7FW&Æ&æF·“Ç“ÒVâ&'VbFW6FRÆ6VR6÷'&W7öæF¢Æ&6P¢òòW7FF–6FVÂFV6ÆFòçVÖW&–6ò†Æö6´'VbÂVR–ÆF–VæR6ö×VW7F’òVÀ¢òòvÆÇW"&÷'&÷6òFRÆçFÆÆFR6öçG&6VæâåTä4&V6ö×öæRÆçFÆÆ¢òòVçFW&¢6öÆòW7F2f–Æ2à§7FF–2fö–BÆö6µv—D&6R†–çB“Â–çB“—°¢6WD'Vb†&'Vb“°¢òò&V6÷'FR6ö×ÆWFó¢6’fVæ–Ö÷2FRVæÆ—7F6öâ67&öÆÂFR§W7FW2Ât6Æ— ¢òòöG&–W7F"W7G&V6†Fò’VÂ6öçFF÷"æò6RF–'V¦&–ÒÒ’6–âÖöæ—F÷ ¢òò6W&–RW6ò6öÆò6RfR6öÖò&Æ7VVçFG&2æò&V6R"à¢t6Æ—ƒÒ²t6Æ—ƒÒ45%õrÒ²t6Æ—“Ò²t6Æ—“Ò45%ô‚Ò°¢–b†Ç7TÖöFRÓÒÅ5Uõ”â—°¢f÷"†–çB¢Ò“²¢ÃÒ“²¢²²’ÖVÖ7’†&'Vb²‡6—¦U÷B–¢¢45%õrÂÆö6´'Vb²‡6—¦U÷B–¢¢45%õrÂ45%õr¢"“°¢ÒVÇ6R–b†Ç7UfW&–g’bb&ÇW$&r—°¢f÷"†–çB¢Ò“²¢ÃÒ“²¢²²’ÖVÖ7’†&'Vb²‡6—¦U÷B–¢¢45%õrÂ&ÇW$&r²‡6—¦U÷B–¢¢45%õrÂ45%õr¢"“°¢ÒVÇ6R°¢f–ÆÅ&V7BƒÂ“Â45%õrÂ“Ò“²ÂÇ7T&t6öÂ‚’“°¢Ð§Ð¢òò6öçFF÷"&Vw&W6—fò6öâD”dd”äs¢6’VÂ6VwVæFòVRFö6Ö÷7G&"W2VÂÖ—6Öð¢òòVR–W7F–çFFòÂW7FgVæ6–öâæòFö6æ’Vâ—†VÂâ7VæFò6Ö&–À¢òò&W–çF6öÆòÆg&æ¦FVÂçVÖW&ò„ÅuôåTÕò¢’ÂæòÆçFÆÆà§7FF–2fö–BÆö6µv—EF–6²‚—°¢V–çC3%÷B&VÒÒÆö6µv—EVçF–ÂÒÖ–ÆÆ—2‚“²òòã¢Æòv&çF—¦Æö6µv—D7F—fR‚¢–çB6V72Ò†–çB’‚‡&VÒ²““’’ò“²òò&VFöæFV†6–'&–&¢–b†Æö6µv—E–çFVBbb6V72ÓÒÆö6µv—DÆ7E6V2’&WGW&ã²òòæF6Ö&–ð¢&ööÂf—'7BÒÆö6µv—E–çFVC°¢–çB“Òf—'7BòÅuô$äEõ“¢ÅuôåTÕõ“°¢–çB“Òf—'7BòÅuô$äEõ“¢ÅuôåTÕõ“°¢Æö6µv—D&6R‡“Â““°¢–b†f—'7B—°¢6öç7B6†"¢ÓÒ$FVÖ6–F÷2–çFVçF÷2fÆÆ–F÷2#°¢G&uFW‡D2…45%õrò"ÂÅuôÕ4uõ’ÂÓÂV”föçDf—B†ÓÂ45%õrÒCÂ"’Â&v#ScRƒ#CÃcÃS’“°¢–b†Æö6´f–Ç2ãÒÄô4µôd”Å5ô„$B—²òòÖVç6¦RW‡Æ–6—FòFVÂG&ÖòÆ&vð¢6öç7B6†"¢Ó"Ò$&Æ÷VVòFV×÷&ÂFRRÖ–çWF÷2#°¢G&uFW‡D2…45%õrò"ÂÅuôÕ4s%õ’ÂÓ"ÂV”föçDf—B†Ó"Â45%õrÒCÂ"’Â&v#ScRƒ#RÃSÃCR’“°¢Ð¢Ð¢6†"6E³eÓ°¢–b‡6V72ãÒc’6ç&–çFb†6BÂ6—¦Vöb†6B’Â"VC¢S&B"Â6V72òcÂ6V72Rc“°¢VÇ6R6ç&–çFb†6BÂ6—¦Vöb†6B’Â"VB2"Â6V72“°¢G&uFW‡D2…45%õrò"ÂÅuôåTÕõ’Â6BÂBÂ&v#ScRƒ#SRÃ#SRÃ#SR’“°¢&W6VçB‡“Â““°¢6WD'Vb†f"“°¢Æö6µv—DÆ7E6V2Ò6V73²Æö6µv—E–çFVBÒG'VS°§Ð¢òòöÇf–FÆòVR†’–çFFòFVÂ6öçFF÷"4”âF–'V¦"æF¢V–VâÆÆÖW7Fð¢òòf&V6ö×öæW"Æ¦öæ–wVÆÖVçFR†Ç7Tæ–Õ–âÂÇ7U&VæFW%72òÇ7TW†—B’Â¢òò6’VÂ&÷'&Fòf–¦VâW6RÖ—6Öò&W6VçB‚’VâfW¢FRVâVâföÆ6Fò&÷–òà¢òòæòFö6Æö6µv—EVçF–Ã¢6Æ—"FRÆçFÆÆæòW&FöæÆW7W&à§7FF–2fö–BÆö6µv—E&W6WB‚—°¢Æö6µv—E–çFVBÒfÇ6S²Æö6µv—DÆ7E6V2ÒÓ°§Ð§7FF–2fö–BÆö6´öäf–Â‚—°¢–b‚Äô4µôd”Å5ôôâ’&WGW&ã°¢–b†Æö6´f–Ç2Â“““’’Æö6´f–Ç2²³°¢Æö6´f–Ç56fR‚“²òòW'6—7FRåDU2FR7VÇV–W"W7W&¢Æö6µVæÇG•6W'fVBÒfÇ6S°¢V–çC3%÷BVâÒÆö6µVæÇG”×2‚“°¢Æö6µv—EVçF–ÂÒVâò†Ö–ÆÆ—2‚’²Vâ’¢°¢Æö6µv—E–çFVBÒfÇ6S²Æö6µv—DÆ7E6V2ÒÓ°§Ð§7FF–2fö–BÆö6´öå7V66W72‚—°¢Æö6µv—EVçF–ÂÒ²Æö6µv—E–çFVBÒfÇ6S²Æö6µv—DÆ7E6V2ÒÓ°¢Æö6µVæÇG•6W'fVBÒG'VS°¢–b‚Äô4µôd”Å5ôôâ’&WGW&ã°¢–b†Æö6´f–Ç2Ò—²Æö6´f–Ç2Ò²Æö6´f–Ç56fR‚“²Òòò6–W'FòÓâ6öçFF÷"6W&ð§Ð¢òò6RÆÆÖÂ%$•"ÆçFÆÆFRfW&–f–66–öââ6’VÂ6öçFF÷"–fVæ–ÇFð¢òò×÷"V¦V×Æò÷'VR6R&V–æ–6–òÆÆ6&–çFVçF"6ÇF'6RÆW7W&Ð¢òòÆW7W&6R6ö'&V’ÂçFW2FVÂ&–ÖW"–çFVçFòâÆö6µVæÇG•6W'fVB'&æ6¢òòVâfÇ6RVâ6F'&çVRÂ6’VRVÂ67F–vò6RÆ–6VæfW¢G&2VÀ¢òò&V–æ–6–ò’æò6R&W—FR6FfW¢VR6RVçG&’6R6ÆRFRÆçFÆÆà§7FF–2fö–BÆö6´&ÕVæF–æuVæÇG’‚—°¢–b‚Äô4µôd”Å5ôôâÇÂÆö6µv—EVçF–ÂÇÂÆö6µVæÇG•6W'fVB’&WGW&ã°¢V–çC3%÷BVâÒÆö6µVæÇG”×2‚“°¢–b‚Vâ’&WGW&ã°¢Æö6µv—EVçF–ÂÒÖ–ÆÆ—2‚’²Vã°¢Æö6µv—E–çFVBÒfÇ6S²Æö6µv—DÆ7E6V2ÒÓ°§Ð ¢òòÒÒÒÒ†2’WFòÖ&Æ÷VVò÷"–æ7F—f–FBÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÐ¢òòæòGWÆ–6äDFRÆÆöv–6FR&Æ÷VVó¢6–W'&Æ6öâ6Æ÷6R‚’‡7P¢òòæ–Ö6–öâæ÷&ÖÂÂVR–FV¦Ö–æ–GW&Vâ&V6–VçFW2’FWgVVÇfR5Eô„ôÔR’¢òòÇVVvòFV¦6W"VÂ&Æ÷VVò6öâæ–ÖFUFò‚’ö6ö×÷6UVæÆö6²‚’ÂVÂÖ—6Öð¢òòÖV6æ—6Öò–çFW'öÆFòFVÂFW6&Æ÷VVò÷"vW7FòW&òÂ&WfW2à§7FF–2fö–BWFôÆö6´æ÷r‚—°¢–b†VF—DÖöFR’VDW†—B‚“²òòwV&FVÂ÷&FVâFR–6öæ÷2’&W–çFVÂ†öÖP¢–b†u7FFRÓÒ5Eô’6Æ÷6R‚“²òòÓâ5Eô„ôÔRÂ6öâ7Ræ–Ö6–öâFR6–W'&P¢u&—ÆT7F—fRÒfÇ6S°¢&VæFW$†öÖR‚“²òò6FR&¦òFR6ö×÷6UVæÆö6²ÂÂF–¢&VæFW$Æö6²‚“²òò6FR'&–&Â6öâÆ†÷&7GVÀ¢u7FFRÒ5EôÄô4³°¢æ–ÖFUFò…45%ô‚Â“²òòVÂ&Æ÷VVò&¦–çFW'öÆFò†6W&ò'FVò¢Æö6´öfbÒ²Æ7DÆö6´öfbÒÓ°¢6†÷tÆö6²‚“°¢tÆ7EF÷V6„×2ÒÖ–ÆÆ—2‚“°§Ð§7FF–2fö–BWFôÆö6µF–6²‚—°¢–b‚UDôÄô4µôôâ’&WGW&ã°¢òòd4RC¢Vâ¶–÷66òäò6RWFòÖ&Æ÷VVâVÂFVÆVföæòW7F&W7FFò’VâW6ó²¢òòFVÖ2FV¦"6W"VÂ&Æ÷VVò6ö'&RVÂ¶–÷66òÖW¦6Æ&–F÷2ÖöF÷2VR6P¢òò—6â†6Æ÷6RW7FfWFFòVâ¶–÷66òÂ6’VRVÂ6–W'&RVVF&–ÖVF–2’à¢–b„´”õ4µôôâbb¶–÷6´öâ’&WGW&ã°¢òò5U5Tå4”ôâ’UDòÔ$ÄõTTò4ôâ”äDUTäD”TåDU2Âäò4R•4âà¢òòÖ–VçG&2ÆçFÆÆW7F7W7VæF–Fæò6RFV¦6W"VÂ&Æ÷VVòFW6FRV“ ¢òò†6W&Æò6öçG&VæçFÆÆvF6W&–G&&¦ò–çf—6–&ÆR†æ–ÖFUFð¢òò6ö×öæR’gVVÆ6Vâg&ÖRVçFW&òVRæF–RfR’’FV¦&–Æö6´öf`¢òòFW6–æ7&öæ—¦Fò6öâÆòVR†’VâVÂg&ÖV'VffW"à¢òòæò†6RfÇF¢FVÂ&Æ÷VVòÂFW7W'F"–6RVæ6&v7W7v¶TÆö6µ67&VVâ‚’À¢òòVRÆòöæR4”TÕ$RVR†–6ÆfR6öæf–wW&FÒÒ6–âW7W&"VRfVç¦¢òòæ–æwVæfVçFæFR–æ7F—f–FBâ6’VR7W7VæFW"W2ÂFR†V6†òÂÖ2W7G&–7Fð¢òòVRVÂWFòÖ&Æ÷VVòÂæòÖVæ÷2à¢òòFÖ&–Vâ6RÆ¦GW&çFRVÂvFò6ö×ÆWFó¢†’VÂFW7F–æò–W7F¢òòFV6–F–Fò’&Æ÷VV"ÖVF–26öÆòVVFR&ö×W"Ææ–Ö6–öâà¢–b…5U5TäEôôâbbu7W7öâ’&WGW&ã°¢–b…õtU$ôdeôôâbb†u7FFRÓÒ5EõõtU$ôdeô4ôäd•$ÒÇÂu7FFRÓÒ5EõõtU$ôdeôä”Ò’’&WGW&ã°¢òòåTä4WFòÖ&Æ÷VV"6öâVæ7GVÆ—¦6–öâVâÖ&6†âVæFW66&væð¢òòvVæW&F÷VW2Â6’VRVÂFV×÷&—¦F÷"FR–æ7F—f–FBfVæ6–Ö—F@¢òòFRÆõDƒ32÷"FVfV7Fò“¢6–ÆçFÆÆFR&Æ÷VVòÂ6P¢òò&W–çF&çFÆÆ6ö×ÆWFVÆVæF÷6R6öâÆFR&öw&W6òÂ’VÀ¢òòW7V&–ò6RVæ6öçG&&–F–VæFòVÂ”âÖ–VçG&26RW7F&fÆ6†VæFòà¢òòFÖ&–Vâ6R&W7WF7VÇV–W"6õDçFÆÆ6ö×ÆWFà¢–b†fÆW„÷F'W7’‚’ÇÂfÆW„÷F÷vç567&VVâ‚’—²tÆ7EF÷V6„×2ÒÖ–ÆÆ—2‚“²&WGW&ã²Ð¢òòæ’Væ6–ÖFRÆ6Æ–'&6–öâF7F–Ã¢VÂW7V&–òVVFRF&F"VâVçF ¢òò&–VâÆ27'V6W2Â’&Æ÷VV"Ö—FBFV¦&–Æ6Æ–'&6–öâ6–à¢òòFW&Ö–æ"’6–â&W7FW&"VÂ&W7ÆFòà¢–b†u7FFRÓÒ5EõDõT4„4Â’&WGW&ã°¢òò7VÇV–W"6öçF7FòÂVâ5TÅT”U"çFÆÆÂ&V&ÖVÂFV×÷&—¦F÷"âfçFW0¢òòFVÂf–ÇG&òFRW7FF÷2&VRÂföÇfW"FVÂ”âÂW67&—F÷&–òVÂ6öçFF÷ ¢òòæò'&çVR–fVæ6–Fò’gVVÇf&Æ÷VV"Â–ç7FçFRà¢–b…BæF÷vâÇÂBç&W76VBÇÂBç&VÆV6VB—²tÆ7EF÷V6„×2ÒÖ–ÆÆ—2‚“²&WGW&ã²Ð¢–b‚tWFôÆö6´×2’&WGW&ã°¢–b†u7FFRÒ5Eô„ôÔRbbu7FFRÒ5Eô’&WGW&ã°¢–b†tÆæBÇÂt†÷7FVB’&WGW&ã²òòÖöFò2ò†÷7VFF¢æò6RFö6¢–b‡5æVÅ’Ò’&WGW&ã²òò6÷'F–æ&–W'F¢æò&Æ÷VV"ÖVF––çFW&66–öà¢–b‚tÆ7EF÷V6„×2—²tÆ7EF÷V6„×2ÒÖ–ÆÆ—2‚“²&WGW&ã²Ð¢–b†Ö–ÆÆ—2‚’ÒtÆ7EF÷V6„×2ÂtWFôÆö6´×2’&WGW&ã°¢WFôÆö6´æ÷r‚“°§Ð ¢òò2222222222222222222222222222222222222222222222222222222222220¢òò22d4RBÒÔôDò´”õ44ò‡&W7FÖò6VwW&ò¢òò22VÂW7FFò’VÂf–ÇG&òF7F–ÂW7Fâ'&–&Â§VçFòfÆW…öÆÅF÷V6‚à¢òò22V’fFöFòÆòVRæV6W6—FF–'V¦"òæfVv"à¢òò2222222222222222222222222222222222222222222222222222222222220 ¢òòÒÒÒÒ6æFFòF—67&WFòFR&¶–÷66ò7F—fò"ÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÐ¢òòVÂ6æFFòäò6R&Vg&W66÷"FV×÷&—¦F÷"â6RU5DÕFVçG&òFRfÇ„fÇW6‚‚’À¢òòVRW2VÂVæ–6òVçFò÷"VÂVR'6öÇWFÖVçFRFöFò6&ÆÆVvæFòÂæVÂÀ¢òò§W7FòçFW2FRV&Æ–6"Æ&æF7V6–â6’VÂ6æFFòW7FVâf"õ ¢òò4ôå5E%T44”ôâçFW2FR7VÇV–W"7V&–FDÔà¢òð¢òò÷"VRæò&7F6öâ&W–çF&ÆòFW6FR¶–÷6µF–6³¢vVõ&VæFW$vÖR„vVòF6‚¢òò6ö×öæRVâ&'Vb’†6R&W6VçBƒÂ45%ô‚Ó’Vâ6Fg&ÖRÂ’VÂ&W6VçFW"7V&P¢òòf"FRf÷&Ö4”ä5$ôäFW6FRVÂ6÷&Râ&W–çFæFòFW7VW2FVÂ&W6VçB†’Væ¢òòfVçFæVâÆVRVÂ&W6VçFW"–V×W¦ò7V&—"Æ&æF6–âVÂ6æFFòÒÒ6P¢òòfVÖ2òÖVæ÷26VwV–FòÂ6–wVR'FVæFòâW7F×æFòFVçG&òFRfÇ„fÇW6‚W6¢òòfVçFææòW†—7FS¢Æ&æFçVæ66RV&Æ–66–âVÂ6æFFòFVçG&òà¢òò&V7VG&òVRVçgVVÇfRÂ6æFFò6öâÖ&vVââW2Æ&æFVR6R6ö×'VV&Và¢òòfÇ„fÇW6‚’ÆVRV&Æ–6¶–÷6µ6†÷t&FvRÂ6’VRF–VæRVR6VwV—"¢òò´”õ4µô$DtUõ‚õ“¢6æFFòVâCC‚âãCs‚CBâãcrÂ&V7VG&òCCBâãCs’‚Câãsà¢6FVf–æR´”õ4µô$õ…õ‚CC@¢6FVf–æR´”õ4µô$õ…õ’C ¢6FVf–æR´”õ4µô$õ…õr3`¢6FVf–æR´”õ4µô$õ…ô‚3 §7FF–2fö–B¶–÷6´&FvU–çB‚—°¢–çB'‚Ò´”õ4µô$DtUõ‚Â'’Ò´”õ4µô$DtUõ’Â'2Ò´”õ4µô$DtUõ3°¢f–ÆÅ&÷VæE&V7D†'‚Â'’Â'2Â'2ÂrÂ&v#ScRƒbÃ‚Ã#b’Â#“°¢f–ÆÄ6—&6ÆT†'‚²'2ò"Â'’²’ÂRÂ&v#ScRƒ#3‚Ã#CÃ#C‚’Â#SR“²òò&6òFVÂ6æFFð¢f–ÆÄ6—&6ÆT†'‚²'2ò"Â'’²’Â2Â&v#ScRƒbÃ‚Ã#b’Â#SR“°¢f–ÆÅ&÷VæE&V7D†'‚²RÂ'’²Â'2ÒÂ’Â"Â&v#ScRƒ#3‚Ã#CÃ#C‚’Â#SR“²òò7VW'ð§Ð¢òòÆÆÖFFW6FRfÇ„fÇW6‚6öâÆ&æFVR6RfV&Æ–6"â6ÆRVç6VwV–FVâVÀ¢òò66òæ÷&ÖÂ†¶–÷66òvFòÂò&æFVRæòFö6ÆW7V–æFVÂ6æFFò’Â6¢òòVRæòVæ6&V6RVÂ6Ö–æòFRF–'V¦òFVÂ&W7FòFVÂ6—7FVÖâF–'V¦’–¢äð¢òòÆÆÖfÇ„fÇW6‚ÂFRÖöFòVRæò†’&V7W'6–öâà§7FF–2fö–B¶–÷6µ7F×&FvR†–çB“Â–çB“—°¢–b‚´”õ4µôôâÇÂ¶–÷6´öâ’&WGW&ã°¢–b†u7FFRÒ5Eô’&WGW&ã²òò6öÆò6ö'&RÆ6ÆfF¢–b‡“Â´”õ4µô$õ…õ’ÇÂ“â´”õ4µô$õ…õ’²´”õ4µô$õ…ô‚Ò’&WGW&ã°¢òòtÆæBgVW&’&V6÷'FR6ö×ÆWFó¢VÂ6æFFòfVâÆW7V–æd•4”4FVÂæVÂÀ¢òò6RÆòVR6R6öâÆ÷&–VçF6–öâÆöv–6FRÆ„§VVv÷2F–'V¦Và¢òòÆæG66R’öæRtÆæC×G'VRVâ6Fg&ÖR’à¢V–çCe÷B¢ö"Òt'Vc²&ööÂvÂÒtÆæC°¢–çB7ƒÒt6Æ—ƒÂ7ƒÒt6Æ—ƒÂ7“Òt6Æ—“Â7“Òt6Æ—“°¢tÆæBÒfÇ6S°¢t6Æ—ƒÒ²t6Æ—ƒÒ45%õrÒ²t6Æ—“Ò²t6Æ—“Ò45%ô‚Ò°¢6WD'Vb†f"“°¢¶–÷6´&FvU–çB‚“°¢6WD'Vb†ö"“²tÆæBÒvÃ°¢t6Æ—ƒÒ7ƒ²t6Æ—ƒÒ7ƒ²t6Æ—“Ò7“²t6Æ—“Ò7“°§Ð¢òòV&Æ–6Æ&æFFVÂ6æFFòÂTåE$"Vâ¶–÷66òÂ&VR&W¦6FP¢òò–æÖVF–FòVçVRÆæògVVÇfF–'V¦"÷"7R7VVçFâVÂW7F×FòVâ6¢òòÆò†6RfÇ„fÇW6‚â'F—"FR†’VÂ6æFFò6RÖçF–VæR6öÆòà§7FF–2fö–B¶–÷6µ6†÷t&FvR‚—°¢–b‚´”õ4µôôâÇÂ¶–÷6´öâ’&WGW&ã°¢fÇ„fÇW6‚„´”õ4µô$õ…õ’Â´”õ4µô$õ…õ’²´”õ4µô$õ…ô‚Ò“°§Ð ¢òòÒÒÒÒVçG&"ò6Æ—"ÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÐ§7FF–2fö–B¶–÷6µ7F'B†–çB–BÂ–çBW‚Â–çBW’Â–çBWrÂ–çBV‚—°¢–b‚´”õ4µôôâÇÂtÆö6µG—RÓÒÇÂ–BÂÇÂ–BâR’&WGW&ã²òò6–â6ÆfRæò†'&–6Æ–F¢æò6R7F—f¢¶–÷6´öâÒG'VS²¶–÷6´Ò–C°¢¶–÷6´W…‚ÒWƒ²¶–÷6´W…’ÒW“²¶–÷6´W…rÒWs²¶–÷6´W„‚ÒVƒ°¢¶–÷6µ6fR‚“°¢&VæFW$†öÖR‚“²òòv–å&WfVÄæ–Ò6ö×öæR6ö'&R†öÖT'V`¢VçFW$†–B“²òòW'GW&6öâÆæ–Ö6–öâæ÷&ÖÂFVÂ6—7FVÖ¢¶–÷6µ6†÷t&FvR‚“°§Ð§7FF–2fö–B¶–÷6´W†—Dæ÷r‚—°¢¶–÷6´öâÒfÇ6S²¶–÷6´ÒÓ°¢¶–÷6´W…‚Ò¶–÷6´W…’Ò¶–÷6´W…rÒ¶–÷6´W„‚Ò°¢¶–÷6µ6fR‚“°¢tÆæBÒfÇ6S²òòÖ—6Ö&VBFR6VwW&–FBVR6Æ÷6P¢t6Æ—ƒÒ²t6Æ—ƒÒ45%õrÒ²t6Æ—“Ò²t6Æ—“Ò45%ô‚Ò°¢6WD'Vb†f"“°¢&VæFW$†öÖR‚“°¢VçFW$†öÖR‚“²òòFV¦u7FFRVâ5Eô„ôÔR’gVVÆ6VÂW67&—F÷&–ð¢tÆ7EF÷V6„×2ÒÖ–ÆÆ—2‚“°§Ð¢òòvW7FòFR6Æ–F¢ÖçFVæW"VÇ6FòVÂ6æFFòâ2âW2VÂÖ—6ÖòvW7FòFP¢òòÆöær×&W726öâVÂVR6RVçG&ò†FW6FRVÂÖVçR6öçFW‡GVÂ’Â’6öÆò'&RÆ¢òòfW&–f–66–öã¢6–â”âö6öçG&6Væ6÷'&V7Fæò6R6ÆRà¢òòVÂ&VW†6ÇV–F4ôÄòf–ÇG&Ö–VçG&2Æ6ÆfFW7FVâçFÆÆà¢òð¢òòçFW2f–ÇG&&Vâ7VÇV–W"W7FFòÂ’W6ò6öçfW'F–VÂFVÆVföæòVâVâÆG&–ÆÆð¢òò—'&V7WW&&ÆS¢6’VÂW7V&–òF–'V¦&VÂ&VVæ6–ÖFVÂFV6ÆFòFVÂ”â†ð¢òò6–×ÆVÖVçFRw&æFR’ÂÂVF—"Æ6Æ–FFVÂ¶–÷66òÆçFÆÆFRfW&–f–66–öà¢òò&V6–W&òVÂFV6ÆFòæò&W7öæF–ÒÒ’6öÖòVÂ¶–÷66òW'6—7FRVâåe2À¢òò&V–æ–6–"föÇf–VçG&"VâVÂÖ—6Öò6—F–òâæò†&–æ–æwVæ6Æ–Fà¢òð¢òòVÂ&VW†6ÇV–FW†—7FR&VRÆæò&V6–&W6÷2F÷VW3²ÆT’FVÀ¢òò&÷–ò6—7FVÖ‡fW&–f–66–öâÂÖVçW2’çVæ6gVR7Rö&¦WF—fòà§7FF–2&ööÂ¶–÷6µF÷V6„&Æö6¶VB†–çB‚Â–çB’—°¢–b‚´”õ4µôôâÇÂ¶–÷6´öâ’&WGW&âfÇ6S°¢–b†u7FFRÒ5Eô’&WGW&âfÇ6S²òòfW&–f–66–öâFVÂ”âÂÖVçW2ÂWF3¢6–âf–ÇG&ð¢–b‚¶–÷6´–äW†6ÇVFVB‡‚Â’’’&WGW&âfÇ6S°¢&WGW&â¶–÷6´–äW†—B‡‚Â’“²òòVÂ6æFFòFR6Æ–F6–V×&Rvæ§Ð§7FF–2&ööÂ¶–÷6´W†—Df—&VBÒfÇ6S²òò–6RF—7&ò6öâU5DR6öçF7Fð§7FF–2fö–B¶–÷6µF–6²‚—°¢–b‚´”õ4µôôâÇÂ¶–÷6´öâ—²¶–÷6´W†—Df—&VBÒfÇ6S²&WGW&ã²Ð¢òòVÂ&V&ÖRfåDU2FVÂf–ÇG&òFRW7FFó¢Ö–VçG&26RW67&–&RÆ6ÆfP¢òò6VwV–Ö÷2Vâ5EôÄô4µ4UEUÂ’6’VÂFVFò6RÆWfçF†’†’VRöFW"föÇfW"¢òò–çFVçF&Æòâ6–âW7FòÂVâFVFòVR6–wV–W&÷–FòÂ6æ6VÆ"Æ6Æ–F¢òò&V'&—&–ÆfW&–f–66–öâVâ'V6ÆRà¢–b‚BæF÷vâ’¶–÷6´W†—Df—&VBÒfÇ6S°¢–b†u7FFRÒ5Eô’&WGW&ã°¢òò–æò&W–çFVÂ6æFFó¢FRW6ò6RVæ6&v¶–÷6µ7F×&FvRFW6FRfÇ„fÇW6‚à¢òòV’6öÆòVVFW67V6†"VÂvW7FòFR6Æ–Fà¢–b‚¶–÷6´W†—Df—&VBbbBæF÷vâbb¶–÷6´–äW†—B…Bç7F'E‚ÂBç7F'E’’bb†Ö–ÆÆ—2‚’ÒBæF÷vä×2’â ¢bb'2…Bç‚ÒBç7F'E‚’Â"bb'2…Bç’ÒBç7F'E’’Â"—°¢¶–÷6´W†—Df—&VBÒG'VS°¢Ç7U7F'EfW&–g”f÷"„Å5UôeDU%ô´”õ4´õUBÂ¶–÷6´“°¢Ð§Ð ¢òòÒÒÒÒçFÆÆ&FVf–æ—"VÂ&VW†6ÇV–F…5Eô´”õ4µ4UB’ÒÒÒÒÒÒÒÒÒÒÒÒÒÐ¢òòVÂföæFò‡F—GVÆòÂ–6öæòÂFW‡F÷2’&÷FöæW2’W2U5DD”4ò’6R6ö×öæRVæ6öÆ¢òòfW¢VâÆö6´'VbÒÒVÂÖ—6ÖòW6òFR67&F6‚VR–†6RÇ7T6ö×÷6U–ââ6F¢òòg&ÖRFVÂ'&7G&R6öÆò6÷–W6&6R’F–'V¦VÂ&V7FæwVÆòVæ6–Ö¢Và¢òòVæ–6ò&W6VçB‚’÷"g&ÖRÂ6W&ò'FVòà¢6FVf–æRµ5ô%Dåõ’…45%ô‚Ò“b¢6FVf–æRµ5ô%Dåô‚c@¢6FVf–æRµ5ô%Dåõrƒ §7FF–2–çB¶–÷6µ6WDÒÓ°§7FF–2–çB¶–÷6µ6WEƒÒÂ¶–÷6µ6WE“ÒÂ¶–÷6µ6WEƒÒÂ¶–÷6µ6WE“Ò°§7FF–2&ööÂ¶–÷6µ6WD†2ÒfÇ6S°§7FF–2V–çC3%÷B¶–÷6µ6WD×2Ò°§7FF–2fö–B¶–÷6µ6WD&6R†–çB–B—°¢ÖVÖ7’†Æö6´'VbÂ†öÖT'VbÂ‡6—¦U÷B•45%õr¢45%ô‚¢"“°¢6WD'Vb†Æö6´'Vb“°¢t6Æ—ƒÒ²t6Æ—ƒÒ45%õrÒ²t6Æ—“Ò²t6Æ—“Ò45%ô‚Ò°¢òòfVÆòFVÂ6öÆ÷"FRv–æ†tF&²’²D$¤UDFRf–G&–ò6öâVÂ6öçFVæ–FòÂVP¢òòW26öÖòW6VÂf–G&–òVÂ&W7FòFVÂ6—7FVÖâçFW2VÂfVÆòW&Vâw&—0¢òò§VÆFòf–¦òÂVRW2§W7Fò÷"ÆòVR&V6–VRV’'6RV—F&"VÀ¢òòÆ—V–BvÆ72âVÂf–G&–òfVâÆF&¦WF’æòçFÆÆ6ö×ÆWF¢òò&÷÷6—Fó¢G&tÆ—V–DvÆ75æVÄW‚&W'FR7RFVw&FFòFRÇW¢6ö'&RDôDÆ¢òòÇGW&FVÂæVÂÂ’ƒ‚W6ò6W&–Væ&æF&Ææ6ÖÖæVw&Væ÷&ÖRVà¢òòfW¢FRVâ7&—7FÂà¢f–ÆÅ&V7DƒÂÂ45%õrÂ45%ô‚ÂtUô$rÂ#‚“°¢–b‡V”vÆ72’G&tÆ—V–DvÆ75æVÂƒ#BÂCÂ45%õrÒC‚Â3SÂ#BÂ4UEô4$EôtÄ52“°¢VÇ6Rf–ÆÅ&÷VæE&V7Dƒ#BÂCÂ45%õrÒC‚Â3SÂ#BÂ4UEô4$Eô$rÂ#3R“°¢G&uFW‡D2…45%õrò"ÂcBÂ$ÖöFò¶–÷66ò"ÂBÂ4UEõE…Eô„’“°¢6öç7B6†"¢3Ò$'&7G&&W†6ÇV—"Væ¦öæFVÂF7F–Â#°¢G&uFW‡D2…45%õrò"Â#"Â3ÂV”föçDf—B‡3Â45%õrÒCÂ"’Â4UEõE…EôÄò“°¢6öç7B6†"¢3"Ò%6’æò'&7G&2ÂFöFÆçFÆÆVVF7F—f#°¢G&uFW‡D2…45%õrò"ÂCbÂ3"ÂV”föçDf—B‡3"Â45%õrÒCÂ"’Â4UEõE…EôÕUDR“°¢G&t–6öâ†–BÂ45%õrò"Ò3bÂ“bÂs"“°¢G&uFW‡D2…45%õrò"Â#ƒ"ÂæÖR†–B’Â2Â4UEõE…Eô„’“°¢6öç7B6†"¢32Ò%&6Æ—#¢ÖçFVâVÇ6FòVÂ6æFFòFRÆ#°¢G&uFW‡D2…45%õrò"Â3CBÂ32ÂV”föçDf—B‡32Â45%õrÒCÂ"’Â4UEõE…EôÄò“°¢6öç7B6†"¢3BÒ&W7V–æ’W67&–&RGR6ÆfRFVÂ6—7FVÖ#°¢G&uFW‡D2…45%õrò"Â3cbÂ3BÂV”föçDf—B‡3BÂ45%õrÒCÂ"’Â4UEõE…EôÄò“°¢òò$6æ6VÆ""W2VæF&¦WFæ÷&ÖÂ‡FVÖ’Â$–æ–6–""6öç6W'fVÂ§VÂFP¢òò6VçFó¢Æ÷26öÆ÷&W2FRÔ$4æò6Ö&–â6öâtF&²Â–wVÂVRVâ§W7FW2à¢–b‡V”vÆ72’G&tÆ—V–DvÆ75æVÂƒ3Âµ5ô%Dåõ’Âµ5ô%DåõrÂµ5ô%Dåô‚Â#Â4UEô4$EôtÄ52“°¢VÇ6Rf–ÆÅ&÷VæE&V7Bƒ3Âµ5ô%Dåõ’Âµ5ô%DåõrÂµ5ô%Dåô‚Â#Â4UEô4$Eô$r“°¢G&uFW‡D2ƒ3²µ5ô%Dåõrò"Âµ5ô%Dåõ’²µ5ô%Dåô‚ò"Ò’Â$6æ6VÆ""Â"Â4UEõE…Eô„’“°¢f–ÆÅ&÷VæE&V7B…45%õrÒ3Òµ5ô%DåõrÂµ5ô%Dåõ’Âµ5ô%DåõrÂµ5ô%Dåô‚Â#Â&v#ScRƒCbÃƒ"Ãƒ"’“°¢G&uFW‡D2…45%õrÒ3Òµ5ô%Dåõrò"Âµ5ô%Dåõ’²µ5ô%Dåô‚ò"Ò’Â$–æ–6–""Â"Â&v#ScRƒ#SRÃ#SRÃ#SR’“°¢6WD'Vb†f"“°§Ð§7FF–2fö–B¶–÷6µ6WE&VæFW"‚—°¢ÖVÖ7’†&'VbÂÆö6´'VbÂ‡6—¦U÷B•45%õr¢45%ô‚¢"“°¢6WD'Vb†&'Vb“°¢t6Æ—ƒÒ²t6Æ—ƒÒ45%õrÒ²t6Æ—“Ò²t6Æ—“Ò45%ô‚Ò°¢–b†¶–÷6µ6WD†2—°¢–çB‚Ò¶–÷6µ6WEƒÂ¶–÷6µ6WEƒò¶–÷6µ6WEƒ¢¶–÷6µ6WEƒ°¢–çB’Ò¶–÷6µ6WE“Â¶–÷6µ6WE“ò¶–÷6µ6WE“¢¶–÷6µ6WE“°¢–çBrÒ¶–÷6µ6WEƒÒ¶–÷6µ6WEƒ²–b‡rÂ’rÒ×s°¢–çB‚Ò¶–÷6µ6WE“Ò¶–÷6µ6WE“²–b†‚Â’‚ÒÖƒ°¢f–ÆÅ&V7D‡‚Â’ÂrÂ‚Â&v#ScRƒ#3RÃ“Ãƒ’Â“R“°¢G&u&÷VæE&V7B‡‚Â’ÂrÂ‚ÂBÂ&v#ScRƒ#CbÃSÃC’“°¢6†"%³#EÓ²6ç&–çFb†"Â6—¦Vöb†"’Â"VB‚VB"ÂrÂ‚“°¢G&uFW‡D2‡‚²rò"Â’²‚ò"Ò’Â"Â"Â&v#ScRƒ#SRÃ#SRÃ#SR’“°¢Ð¢&W6VçBƒÂ45%ô‚Ò“°¢6WD'Vb†f"“°§Ð§7FF–2fö–B¶–÷6µ6WDVçFW"†–çB–B—°¢¶–÷6µ6WDÒ–C²¶–÷6µ6WD†2ÒfÇ6S²¶–÷6µ6WD×2Ò°¢¶–÷6µ6WEƒÒ¶–÷6µ6WE“Ò¶–÷6µ6WEƒÒ¶–÷6µ6WE“Ò°¢u7FFRÒ5Eô´”õ4µ4UC°¢¶–÷6µ6WD&6R†–B“°¢¶–÷6µ6WE&VæFW"‚“°§Ð§7FF–2fö–B¶–÷6µ6WEF–6²‚—°¢òòVÂ'&7G&R6öÆò7VVçF6’TÕ”U¤÷"Væ6–ÖFRÆf–ÆFR&÷FöæW2Â6’VP¢òòVÇ6"$–æ–6–""ò$6æ6VÆ""çVæ66R–çFW'&WF6öÖòF–'V¦"à¢&ööÂG&u¦öæRÒ…Bç7F'E’Âµ5ô%Dåõ’Ò‚“°¢–b…Bç&W76VBbbG&u¦öæR—°¢¶–÷6µ6WEƒÒ¶–÷6µ6WEƒÒBçƒ²¶–÷6µ6WE“Ò¶–÷6µ6WE“ÒBç“°¢¶–÷6µ6WD†2ÒfÇ6S²&WGW&ã°¢Ð¢–b…BæF÷vâbbG&u¦öæR—°¢¶–÷6µ6WEƒÒBçƒ²¶–÷6µ6WE“ÒBç“°¢–çBrÒ¶–÷6µ6WEƒÒ¶–÷6µ6WEƒ²–b‡rÂ’rÒ×s°¢–çB‚Ò¶–÷6µ6WE“Ò¶–÷6µ6WE“²–b†‚Â’‚ÒÖƒ°¢¶–÷6µ6WD†2Ò‡rãÒ#bb‚ãÒ#“²òò÷"FV&¦òFRW6òW2VâF÷VRÂæòVâ&V¢–b†Ö–ÆÆ—2‚’Ò¶–÷6µ6WD×2â3—²¶–÷6µ6WD×2ÒÖ–ÆÆ—2‚“²¶–÷6µ6WE&VæFW"‚“²Ð¢&WGW&ã°¢Ð¢–b…Bç&VÆV6VBbbG&u¦öæR—²¶–÷6µ6WE&VæFW"‚“²&WGW&ã²Òòòf–¦VÂ&V7FæwVÆòF–'V¦Fð¢–b…BçFbbBç’ãÒµ5ô%Dåõ’bbBç’ÃÒµ5ô%Dåõ’²µ5ô%Dåô‚—°¢–b…Bç‚ãÒ3bbBç‚ÃÒ3²µ5ô%Dåõr—²òò6æ6VÆ ¢u7FFRÒ5Eô„ôÔS²&VæFW$†öÖR‚“²6†÷t†öÖR‚“²&WGW&ã°¢Ð¢–b…Bç‚ãÒ45%õrÒ3Òµ5ô%DåõrbbBç‚ÃÒ45%õrÒ3—²òò–æ–6– ¢–çBW‚ÒÂW’ÒÂWrÒÂV‚Ò°¢–b†¶–÷6µ6WD†2—°¢W‚Ò¶–÷6µ6WEƒÂ¶–÷6µ6WEƒò¶–÷6µ6WEƒ¢¶–÷6µ6WEƒ°¢W’Ò¶–÷6µ6WE“Â¶–÷6µ6WE“ò¶–÷6µ6WE“¢¶–÷6µ6WE“°¢WrÒ¶–÷6µ6WEƒÒ¶–÷6µ6WEƒ²–b†WrÂ’WrÒÖWs°¢V‚Ò¶–÷6µ6WE“Ò¶–÷6µ6WE“²–b†V‚Â’V‚ÒÖVƒ°¢Ð¢¶–÷6µ7F'B†¶–÷6µ6WDÂW‚ÂW’ÂWrÂV‚“°¢&WGW&ã°¢Ð¢Ð§Ð ¢òò2222222222222222222222222222222222222222222222222222222222220¢òò22d4R"ÒÔTåR4ôåDU…ETÂDRÄôärÕ$U52†W7F–Æò7F–öâ6†VWB¢òò22&V6R6öâW66Æ¶gVæF–Fò–çFW'öÆF÷26ö'&RVÂW67&—F÷&–òâäð¢òò22&V6ö×öæRÆçFÆÆVçFW&¢6öÆòÆ&æF–æfW&–÷"FöæFRf—fRà¢òò2222222222222222222222222222222222222222222222222222222222220¢òòæVÂVæ–6òæ6ÆFòÂ–6öæòÂW7F–Æò†ö¦FR66–öæW2FR”õ3¢Væ6öÆ¢òòF&¦WF&VFöæFVFÂFW‡FòÆ•¥T”U$DÂvÆ–fòÆDU$T4„’f–Æ26W&F0¢òò÷"VæÆ–æVFR‚âæòÆÆWff–Æ$6æ6VÆ"#¢6R6–W'&Fö6æFògVW&à¢6FVf–æR5E…õ$õu20¢6FVf–æR5E…õ$õuô‚S€¢6FVf–æR5E…õr#C@¢6FVf–æR5E…õ$B# ¢6FVf–æR5E…ôtÅ•…õ2#`¢6FVf–æR5E…õEôÂ‚òòÖ&vVâFVÂFW‡Fð¢6FVf–æR5E…õEõ"BòòÖ&vVâFVÂvÆ–fð¢6FVf–æR5E…ôÔ$t”â‚òò—&RÖ–æ–Öò6öçG&7VÇV–W"&÷&FP¢6FVf–æR5E…ôt‚"òò6W&6–öâVçG&RVÂ–6öæò’VÂæVÀ¢6FVf–æR5E…ô”4ôåõ2s"òòÆFòFVÂ–6öæòVâÆ&V¦–ÆÆFVÂ†öÖP¢6FVf–æR5E…ôä”ÕôÕ2S ¢6FVf–æR5E…õäTÅô‚„5E…õ$õu2¢5E…õ$õuô‚§7FF–2–çB7G„ÒÓÂ7G„7F–öâÒÓ°§7FF–2&ööÂ7G„6Æ÷6–ærÒfÇ6S°§7FF–2V–çC3%÷B7G„æ–Ô×2Ò°§7FF–2–çB7G…‚ÒÂ7G…’Ò²òòW7V–æFVÂæVÂ–&V6÷'FF§7FF–2–çB7G„&æE“ÒÂ7G„&æE“Ò²òò&æFVR6R&V6ö×öæR÷"g&ÖP§7FF–2V–çCe÷B7G…æVÄ6öÂ‚—²&WGW&âV”vÆ72ò4UEô4$EôtÄ52¢4UEô4$Eô$s²Ð¢òòf–ÆÒ6æFFòFRÂÒÖöFòVF–6–öâÂ"ÒÖöFò¶–÷66òâÆ2F÷2VP¢òòæV6W6—FâVæ6ÆfRFVÂ6—7FVÖ6öâÆVRfW&–f–6"6RF–'V¦âFVçVF2¢òò6öâ–æW'FW26’æò†’æ–æwVæ6öæf–wW&F¢6RfR÷"VRæò6RVVFVâW6"À¢òòVâfW¢FRæò†6W"æFÂFö6&Æ2à§7FF–2&ööÂ7G…&÷tVæ&ÆVB†–çB’—°¢–b†’ÓÒ’&WGW&âÄô4µôôâbbtÆö6µG—Râ°¢–b†’ÓÒ"’&WGW&â´”õ4µôôâbbtÆö6µG—Râ°¢&WGW&âG'VS°§Ð§7FF–26öç7B6†"¢7G„Æ&VÂ†–çB’—°¢7v—F6‚†’—°¢66R¢&WGW&âÆö6´vWB†7G„’ò$FW6&Æ÷VV""¢$&Æ÷VV"#°¢66R¢&WGW&â$ÖöFòVF–6•Ç„35Ç„#2"&â#°¢FVfVÇC¢&WGW&â$ÖöFò¶–÷6¶ò#°¢Ð§Ð¢òòvÆ–f÷2fV7F÷&–ÆW2FR#gƒ#bÂF–'V¦F÷26öâÆ2&–Ö—F—f2VR–W†—7FVã¢æð¢òò†6VâfÇF&—FÖ2æ’VægVVçFRFR–6öæ÷2âVÂ&‡VV6ò"FR6FVæò6R–çF¢òòFVÂ6öÆ÷"FVÂæVÂÂ6’VR6–wVVâÂFVÖ6–âÆöv–6'FRà§7FF–2fö–B7G„vÇ—‚†–çB¶–æBÂ–çB‚Â–çB’Â–çB2ÂV–çC…÷B—°¢V–çCe÷B†öÆRÒ7G…æVÄ6öÂ‚“°¢–b†¶–æBÓÒ—²òò6æFFð¢V–çCe÷B2Ò&v#ScRƒ#3RÃƒÃƒ“°¢f–ÆÄ6—&6ÆT‡‚²2ò"Â’²2ò2Â2òBÂ2Â“²òò&6ð¢f–ÆÄ6—&6ÆT‡‚²2ò"Â’²2ò2Â2òbÂ†öÆRÂ“°¢f–ÆÅ&÷VæE&V7D‡‚²2Â’²2ò"Ò"Â2ÒbÂ2ò"²Â2Â2Â“²òò7VW'ð¢ÒVÇ6R–b†¶–æBÓÒ—²òò&V¦–ÆÆFR–6öæ÷2„ÖöFòVF–6–öâ¢V–çCe÷B2Ò&v#ScRƒCÃSÃs"“°¢–çBÒ‡2ÒR’ò#°¢f–ÆÅ&÷VæE&V7D‡‚Â’ÂÂÂ"Â2Â“°¢f–ÆÅ&÷VæE&V7D‡‚²²RÂ’ÂÂÂ"Â2Â“°¢f–ÆÅ&÷VæE&V7D‡‚Â’²²RÂÂÂ"Â2Â“°¢f–ÆÅ&÷VæE&V7D‡‚²²RÂ’²²RÂÂÂ"Â2Â“°¢ÒVÇ6R²òòçFÆÆ6öâ6æFFò„ÖöFò¶–÷66ò¢V–çCe÷B2Ò&v#ScRƒÃ#ÃSR“°¢f–ÆÅ&÷VæE&V7D‡‚Â’²Â2Â2ÒrÂ2Â2Â“²òòÖ&6ð¢f–ÆÅ&÷VæE&V7D‡‚²2Â’²BÂ2ÒbÂ2Ò2Â"Â†öÆRÂ“°¢f–ÆÅ&÷VæE&V7D‡‚²2ò"Ò2Â’²2ò"ÒRÂbÂrÂÂ2Â“²òò6æFFòFVçG&ð¢f–ÆÅ&÷VæE&V7D‡‚²2ò2Â’²2ÒRÂ2ò2Â2ÂÂ2Â“²òò–P¢Ð§Ð§7FF–2fö–B7G…&VæFW"†fÆöB—°¢–b‡Â’Ò²–b‡â’Ò°¢fÆöBV6RÒÒƒÒ’¢ƒÒ“²òòV6RÖ÷W@¢fÆöB62Òãƒ†b²ã&b¢V6S²òòW66Æ¢V–çC…÷BÒ‡V–çC…÷B’ƒ#SRãb¢V6R“²òògVæF–Fð¢6WD'Vb†&'Vb“°¢t6Æ—ƒÒ²t6Æ—ƒÒ45%õrÒ²t6Æ—“Ò²t6Æ—“Ò45%ô‚Ò°¢òò6öÆòÆ&æFVRö7WVÂæVÂÂæòÆçFÆÆVçFW&âÆ&æF6R6Æ7VÆ¢òòVâ7G„÷Vâ6öçG&Æ÷6–6–öâd”äÂ†W66Æ“²6öÖòÆW66ÆVæ6övRVÀ¢òòæVÂ†6–7R6VçG&òÂæ–æwVâg&ÖR–çFW&ÖVF–ò6R6ÆRFRVÆÆà¢f÷"†–çB¢Ò7G„&æE“²¢ÃÒ7G„&æE“²¢²²¢ÖVÖ7’†&'Vb²‡6—¦U÷B–¢¢45%õrÂ†öÖT'Vb²‡6—¦U÷B–¢¢45%õrÂ45%õr¢"“°¢fÆöB67‚Ò†fÆöB–7G…‚²5E…õrò"ãc°¢fÆöB67’Ò†fÆöB–7G…’²5E…õäTÅô‚ò"ãc°¢–çB‚Ò†–çB’†67‚²‚†fÆöB–7G…‚Ò67‚’¢62“°¢–çB’Ò†–çB’†67’²‚†fÆöB–7G…’Ò67’’¢62“°¢–çBrÒ†–çB’„5E…õr¢62’Â‚Ò†–çB’„5E…õäTÅô‚¢62“°¢–çB&BÒ†–çB’„5E…õ$B¢62“°¢òòTä6öÆF&¦WF&Æ2G&W2f–Æ2âG&tÆ—V–DvÆ75æVÂæò6WFÇ†À¢òò6’VRÖ–VçG&27&V6R6RW67RÔ•4ÔòF–çFR6öÖò6öÆ÷"Ææò’6öÆòVÀ¢òòg&ÖRf–æÂ6Âf–G&–ò&VÃ¢ÆòVæ–6òVR&V6RVçFöæ6W2W2VÀ¢òòFW6Væf÷VRÂæòVâ6ÇFòFR6öÆ÷"à¢f–ÆÅ&÷VæE&V7D‡‚Â’ÂrÂ‚Â&BÂ7G…æVÄ6öÂ‚’Â‡V–çC…÷B’ƒ#3‚¢†–çB–ò#SR’“°¢–b‡V”vÆ72bbãÒãb’G&tÆ—V–DvÆ75æVÂ‡‚Â’ÂrÂ‚Â&BÂ4UEô4$EôtÄ52“°¢–çB&‚Ò‚ò5E…õ$õu3°¢–çBFW‡DÖ‚Ò5E…õrÒ5E…õEôÂÒ5E…ôtÅ•…õ2Ò5E…õEõ"Ò°¢f÷"†–çB’Ò²’Â5E…õ$õu3²’²²—°¢–çB'’Ò’²’¢&ƒ°¢–b†’â’f–ÆÅ&V7D‡‚²BÂ'’ÂrÒ#‚ÂÂ4UEõE…EôÕUDRÂ‡V–çC…÷B’ƒ“R¢†–çB–ò#SR’“²òò6W&F÷ ¢&ööÂVâÒ7G…&÷tVæ&ÆVB†’“°¢6öç7B6†"¢Æ"Ò7G„Æ&VÂ†’“°¢òòVÂFÖæò6R6Æ7VÆ6öçG&VÂæ6†òd”äÂÂæòVÂW66ÆFó¢6’6P¢òò&V6Æ7VÆ&÷"g&ÖRöG&–6ÇF"Ö—FBFRÆæ–Ö6–öâà¢–çBg2ÒV”föçDf—B†Æ"ÂFW‡DÖ‚Â2“°¢G&uFW‡D‡‚²5E…õEôÂÂ'’²&‚ò"ÒV”Æ–æT‚†g2’ò"ÂÆ"Âg2À¢Vâò4UEõE…Eô„’¢4UEõE…EôÕUDRÂ“°¢7G„vÇ—‚†’Â‚²rÒ5E…õEõ"Ò5E…ôtÅ•…õ2Â'’²&‚ò"Ò5E…ôtÅ•…õ2ò"À¢5E…ôtÅ•…õ2ÂVâò¢‡V–çC…÷B’‚†–çB–¢ò#SR’“°¢Ð¢&W6VçB†7G„&æE“Â7G„&æE““°¢6WD'Vb†f"“°§Ð§7FF–2fö–B7G„÷Vâ†–çB6Æ÷B—°¢–b‚5E„ÔTåUôôâÇÂ6Æ÷BÂÇÂ6Æ÷Bâ’&WGW&ã°¢7G„Ò†öÖT÷&FW%·6Æ÷EÓ°¢òòvVöÖWG&–$TÂFVÂ–6öæòVÇ6Fó¢ÆÖ—6Ö&V¦–ÆÆVR–çF&VæFW$†öÖR‚¢òò†wƒÓ#BÂw“Ó#"Â6òFR6öÇVÖæ#Â6òFRf–Æ"Â–6öæòFRs"’à¢–çB—‚Ò#B²‡6Æ÷BRB’¢#°¢–çB—’Ò#"²‡6Æ÷BòB’¢#°¢òòÆFó¢6R&Vf–W&RÆDU$T4„FVÂ–6öæòÂW&ò6öÆò6’VÂæVÂ6&RVçFW&ð¢òò†“²6’æòÂÆ—§V–W&Fâ6’æò6&RVâæ–æwVæòFRÆ÷2F÷2‡æVÂÖ2æ6†ð¢òòFRÆ7VVçF’Â6RVÆ–vRVÂÆFò6öâÔ26—F–òçFW2FR&V6÷'F"ÒÒ6’VÀ¢òò&V6÷'FRFR&¦òçVæ66&FV¦æFòVÂæVÂVæ6–ÖFVÂ–6öæòVÇ6FòÂVP¢òòW2§W7FòVÂVRVÂW7V&–òæV6W6—F6VwV—"f–VæFòà¢–çB&ööÕ"Ò…45%õrÒ5E…ôÔ$t”â’Ò†—‚²5E…ô”4ôåõ2²5E…ôt‚“°¢–çB&ööÔÂÒ†—‚Ò5E…ôt‚’Ò5E…ôÔ$t”ã°¢&ööÂFõ&–v‡BÒ‡&ööÕ"ãÒ5E…õr’òG'VR¢‡&ööÔÂãÒ5E…õròfÇ6R¢‡&ööÕ"ãÒ&ööÔÂ’“°¢–çB‚ÒFõ&–v‡Bò†—‚²5E…ô”4ôåõ2²5E…ôt‚’¢†—‚Ò5E…ôt‚Ò5E…õr“°¢–çB’Ò—“²òòÆ–æVFò6öâVÂ&÷&FR7WW&–÷"FVÂ–6öæð¢òò$T4õ%DRd”äÂÂ–æ6öæF–6–öæÃ¢6RÆòVR6R6öâVÂÆFòVÆVv–FòÂVÂæVÀ¢òòVçFW&òVVFFVçG&òFRçFÆÆâW2ÆòVRv&çF—¦VRæ–æwVâ–6öæòFRÆ¢òòVÇF–Öf–Æò6öÇVÖæFV¦RVÂÖVçRÖVF–2ògVW&FRÆ6æ6Rà¢–b‡‚Â5E…ôÔ$t”â’‚Ò5E…ôÔ$t”ã°¢–b‡‚â45%õrÒ5E…ôÔ$t”âÒ5E…õr’‚Ò45%õrÒ5E…ôÔ$t”âÒ5E…õs°¢–b‡’Â5E…ôÔ$t”â’’Ò5E…ôÔ$t”ã°¢–b‡’â45%ô‚Ò5E…ôÔ$t”âÒ5E…õäTÅô‚’’Ò45%ô‚Ò5E…ôÔ$t”âÒ5E…õäTÅôƒ°¢7G…‚Òƒ²7G…’Ò“°¢7G„&æE“Ò’Ò#²–b†7G„&æE“Â’7G„&æE“Ò°¢7G„&æE“Ò’²5E…õäTÅô‚²#²–b†7G„&æE“â45%ô‚Ò’7G„&æE“Ò45%ô‚Ò°¢7G„7F–öâÒÓ²7G„6Æ÷6–ærÒfÇ6S°¢7G„æ–Ô×2ÒÖ–ÆÆ—2‚“²–b‚7G„æ–Ô×2’7G„æ–Ô×2Ò°¢u&—ÆT7F—fRÒfÇ6S°¢u7FFRÒ5Eô5Eƒ°§Ð¢òòÆ66–öâäò6RV¦V7WFÂFö6#¢6RwV&F’6RV¦V7WF7VæFòFW&Ö–æÆ¢òòæ–Ö6–öâFR6–W'&RÂ&VRVÂæVÂçVæ6FW6&W¦6FRvöÇRà§7FF–2fö–B7G„6Æ÷6R†–çB7F–öâ—°¢7G„7F–öâÒ7F–öã²7G„6Æ÷6–ærÒG'VS°¢7G„æ–Ô×2ÒÖ–ÆÆ—2‚“²–b‚7G„æ–Ô×2’7G„æ–Ô×2Ò°§Ð§7FF–2fö–B7G„f–æ—6‚‚—°¢–çBÒ7G„7F–öâÂÒ7G„°¢7G„7F–öâÒÓ²7G„6Æ÷6–ærÒfÇ6S²7G„æ–Ô×2Ò²7G„ÒÓ°¢u7FFRÒ5Eô„ôÔS°¢6†÷t†öÖR‚“²òòW67&—F÷&–òÆ–×–òVâVâ6öÆòföÆ6Fð¢òòÂÒ6æ6VÆFò‡F÷VRgVW&FVÂæVÂ“¢æò†’æFVR†6W"à¢–b†ÓÒbb7G…&÷tVæ&ÆVBƒ’—°¢òòöæW"’V—F"VÂ6æFFòW†–vVâ6ÆfS¢6’æF–RFW6&Æ÷VVÆFP¢òò÷G&ò6öâ6öÆòFö6"VÂ–6öæòâÖ—6Ö'WFFRfW&–f–66–öâVRFöFòÆòFVÖ2à¢Ç7U7F'EfW&–g”f÷"†Æö6´vWB†’òÅ5UôeDU%õTäÄô4´¢Å5UôeDU%ôÄô4´Â“°¢ÒVÇ6R–b†ÓÒ—°¢òòÖöFòVF–6–öã¢VÂ6ö×÷'FÖ–VçFòFR6–V×&RÂW&ò4”âv'&"VÂ–6öæòÒÐ¢òò7VæFò6RVÆ–vRW7Ff–ÆVÂFVFò–6RÆWfçFòFVÂ–6öæò†6R&Fòà¢VDVçFW"‚“°¢ÒVÇ6R–b†ÓÒ"bb7G…&÷tVæ&ÆVBƒ"’—°¢¶–÷6µ6WDVçFW"†“°¢Ð§Ð§7FF–2fö–B7G…F–6²‚—°¢–b†7G„æ–Ô×2—°¢V–çC3%÷BRÒÖ–ÆÆ—2‚’Ò7G„æ–Ô×3°¢fÆöBÒ†fÆöB–Rò†fÆöB”5E…ôä”ÕôÕ3²–b‡âãb’Òãc°¢7G…&VæFW"†7G„6Æ÷6–æròƒãbÒ’¢“°¢–b†RãÒ‡V–çC3%÷B”5E…ôä”ÕôÕ2—°¢7G„æ–Ô×2Ò°¢–b†7G„6Æ÷6–ær’7G„f–æ—6‚‚“°¢Ð¢&WGW&ã°¢Ð¢–b…BçF—°¢f÷"†–çB’Ò²’Â5E…õ$õu3²’²²—°¢–çB’Ò7G…’²’¢5E…õ$õuôƒ°¢–b…Bç‚ãÒ7G…‚bbBç‚ÃÒ7G…‚²5E…õrbbBç’ãÒ’bbBç’ÃÒ’²5E…õ$õuô‚—°¢–b‚7G…&÷tVæ&ÆVB†’’’&WGW&ã²òò–æW'FS¢æ’6—V–W&6–W'&VÂÖVçP¢7G„6Æ÷6R†’“²&WGW&ã°¢Ð¢Ð¢7G„6Æ÷6R‚Ó“²òògVW&FVÂæVÂÓâ6æ6VÆ ¢Ð§Ð ¢òò2222222222222222222222222222222222222222222222222222222222220¢òò22d4R2ÒTR„4U"E$2TädU$”d”44”ôâ4õ%$T5D¢òò22VâVæ–6òVçFòFR6Æ–F&Æ27VG&ò'WF2çVWf2Â6¢òò22Ç7U7F'EfW&–g’6–wVR6–VæFòÆTä”4'WFFRfW&–f–66–öâà¢òò2222222222222222222222222222222222222222222222222222222222220§7FF–2fö–BÇ7Tf–æ—6„gFW"‚—°¢–çBv†BÒÇ7TgFW"Â–BÒÇ7TgFW$°¢Ç7TgFW"ÒÅ5UôeDU%õTäÄô4³²Ç7TgFW$ÒÓ°¢–b‡v†BÓÒÅ5UôeDU%ô´”õ4´õUB—²¶–÷6´W†—Dæ÷r‚“²&WGW&ã²Ð¢òòvFò6VwW&ó¢”â6÷'&V7FòÓâ6R6öçF–çV6öâÆæ–Ö6–öâFRvFòâæð¢òò6R&W–çFVÂW67&—F÷&–òFR&¦ò&÷÷6—Fó¢Ææ–Ö6–öâ'&æ6†6–VæFð¢òòVâgVæF–FòæVw&òDU4DRÆòVR–†’VâçFÆÆà¢–b‡v†BÓÒÅ5UôeDU%õõtU$ôdb—²öfd&Vv–äæ–Ò‚“²&WGW&ã²Ð¢u7FFRÒ5Eô„ôÔS°¢–b‡v†BÓÒÅ5UôeDU%ôÄô4´’Æö6µ6WB†–BÂG'VR“°¢VÇ6R–b‡v†BÓÒÅ5UôeDU%õTäÄô4´’Æö6µ6WB†–BÂfÇ6R“°¢&VæFW$†öÖR‚“²6†÷t†öÖR‚“²òò&÷'&ÆçFÆÆFRfW&–f–66–öà¢–b‡v†BÓÒÅ5UôeDU%ôõTäbb–BãÒ’VçFW$†–B“°§Ð§7FF–2fö–BÇ7U7F'EfW&–g”f÷"†–çBv†BÂ–çB–B—°¢–b†tÆö6µG—RÓÒ’&WGW&ã²òò6–â6ÆfRæò†’æFVRfW&–f–6 ¢Ç7U7F'EfW&–g’‚“²òòFV¦Ç7TgFW"VâTäÄô4³²6R§W7F§W7FòV¢Ç7TgFW"Òv†C²Ç7TgFW$Ò–C°§Ð §7FF–2–çBWFc„6÷VçB†6öç7B6†"¢2—²–çBâÒ²v†–ÆR‚§2—²–b‚‚§2b„3’Òƒƒ’â²³²2²³²Ò&WGW&âã²Ð§7FF–2fö–BÇ7U74VæB†6öç7B6†"¢2—²–çBÂÒ7G&ÆVâ†Ç7U72’Â6ÂÒ7G&ÆVâ‡2“²–b„Â²6ÂÂ†–çB—6—¦Vöb†Ç7U72’Ò—²ÖVÖ7’†Ç7U72²ÂÂ2Â6Â“²Ç7U75´Â²6ÅÒÒ²ÒÐ§7FF–2fö–BÇ7TW†—B‚—°¢òòd4U22’C¢6æ6VÆ"VæfW&–f–66–öâFRòFR¶–÷66òäòFV&R&Æ÷VV"Æ¢òòçFÆÆ‡VRW2ÆòVR†6–Æ&ÖFR&¦ò’â’6ö'&RFöFó¢6æ6VÆ"Æ¢òò6Æ–FFVÂ¶–÷66òF–VæRVRFWföÇfW"ÄÂçVæ6ÂW67&—F÷&–òÒÒ6’æòÀ¢òòÆ&÷–fÆV6†FR&G&2"6W&–Æf–FRW66RFVÂÖöFò¶–÷66òà¢òò4TuU$”DC¢6’ÆfW&–f–66–öâ6Æ–òFRÆçFÆÆFR&Æ÷VVòÂ6æ6VÆ ¢òògVVÇfRÂ$ÄõTTòÂçVæ6ÂW67&—F÷&–òâfÆò$”ÔU$ò÷'VRVÂFW7W'F"FP¢òòVæ7W7Vç6–öâW6Å5UôeDU%ôõTä‡&föÇfW"ÆFöæFRW7F&2’Â¢òò6–âW7F&Ö6W&–÷"ÆFR&¦òÂVRFW&Ö–æVâ5Eô„ôÔR²6†÷t†öÖR‚“ ¢òòVÂW67&—F÷&–òÆf—7F6–â†&W"–çG&öGV6–FòÆ6ÆfRà¢–b†Ç7UfW&–g’bbtÆö6µfW&–g”Æö6¶VB—°¢tÆö6µfW&–g”Æö6¶VBÒfÇ6S°¢Ç7UfW&–g’ÒfÇ6S²Ç7TgFW"ÒÅ5UôeDU%õTäÄô4³²Ç7TgFW$ÒÓ°¢Ç7U6†¶T×2Ò²Æö6µv—E&W6WB‚“°¢u7FFRÒ5EôÄô4³²Æö6´öfbÒ²Æ7DÆö6´öfbÒÓ°¢&VæFW$Æö6²‚“²6†÷tÆö6²‚“°¢&WGW&ã°¢Ð¢–b†Ç7UfW&–g’bbÇ7TgFW"ÒÅ5UôeDU%õTäÄô4²—°¢&ööÂv4¶–÷6²Ò†Ç7TgFW"ÓÒÅ5UôeDU%ô´”õ4´õUB“°¢&ööÂv5öfbÒ†Ç7TgFW"ÓÒÅ5UôeDU%õõtU$ôdb“°¢Ç7UfW&–g’ÒfÇ6S²Ç7TgFW"ÒÅ5UôeDU%õTäÄô4³²Ç7TgFW$ÒÓ°¢Ç7U6†¶T×2Ò²Æö6µv—E&W6WB‚“°¢òò6æ6VÆ"ÆfW&–f–66–öâFVÂvFòäòv’äò6RfÂW67&—F÷&–ó ¢òòFWgVVÇfRÆçFÆÆFR6öæf—&Ö6–öâÂ6öâVÂ6Æ–FW"÷G&fW¢Vâ&W÷6òà¢–b‡v5öfbbbõtU$ôdeôôâ—²öfdVçFW"‚“²&WGW&ã²Ð¢–b‡v4¶–÷6²bb´”õ4µôôâbb¶–÷6´öâbb¶–÷6´ãÒ—°¢&VæFW$†öÖR‚“²òòv–å&WfVÄæ–Ò6ö×öæR6ö'&R†öÖT'V`¢VçFW$†¶–÷6´“°¢¶–÷6µ6†÷t&FvR‚“°¢ÒVÇ6R°¢u7FFRÒ5Eô„ôÔS²&VæFW$†öÖR‚“²6†÷t†öÖR‚“°¢Ð¢&WGW&ã°¢Ð¢–b†Ç7UfW&–g’—²Ç7UfW&–g’ÒfÇ6S²u7FFRÒ5EôÄô4³²Æö6´öfbÒ²Æ7DÆö6´öfbÒÓ²&VæFW$Æö6²‚“²6†÷tÆö6²‚“²Ð¢VÇ6R²u7FFRÒ5Eô²6WGF–æw5&VæFW"‚“²Ð§Ð§7FF–2fö–BÇ7UVæÆö6²‚—°¢Æö6´öå7V66W72‚“²òòd4R¢6–W'FòÓâ6öçFF÷"FRfÆÆ÷26W&ð¢Ç7U6†¶T×2Ò°¢tÆö6µfW&–g”Æö6¶VBÒfÇ6S²òò6ÆfR6÷'&V7F¢–æòW7FÖ÷2&FWG&2FVÂ&Æ÷VVò ¢Ç7UfW&–g’ÒfÇ6S²Ç7Uw&öærÒ²Æö6´öfbÒ²Æ7DÆö6´öfbÒÓ°¢òòd4U22’C¢6’ÆfW&–f–66–öâæòW&&FW6&Æ÷VV"ÆåDÄÄÂVÀ¢òòFW7F–æòÆòFV6–FRÇ7Tf–æ—6„gFW"†'&—"ÂöæW"÷V—F"6æFFòÂ6Æ—"FVÀ¢òò¶–÷66ò’âÆæ–Ö6–öâFR&WfVÆFòFVÂW67&—F÷&–òFR&¦òæòÆ–6†’à¢–b†Ç7TgFW"ÒÅ5UôeDU%õTäÄô4²—²Ç7Tf–æ—6„gFW"‚“²&WGW&ã²Ð¢u7FFRÒ5Eô„ôÔS°¢&VæFW$†öÖR‚“²òò6ö×öæRVÂ†öÖRVâ†öÖT'V`¢V–çC3%÷BCÒÖ–ÆÆ—2‚’ÂGW"ÒC²òòãG3¢&V6W"FW7fæV6–Fò²ÆWfRFVÖ&Æ÷ ¢f÷"ƒ³²—°¢V–çC3%÷BRÒÖ–ÆÆ—2‚’ÒC²–b†RâGW"’RÒGW#°¢fÆöBÒ†fÆöB–RòGW#°¢V–çC…÷BÒ‡V–çC…÷B’‡¢#SR“°¢–çB6‚Ò†–çB’‚ƒãbÒ’¢bãb¢6–æb†R¢ãVb’“²òòFVÖ&Æ÷"VRFV6P¢f÷"†–çB¢Ò²¢Â45%ôƒ²¢²²—°¢V–çCe÷B¢BÒ&'Vb²‡6—¦U÷B–¢¢45%õs°¢V–çCe÷B¢&rÒ†&ÇW$&rò&ÇW$&r¢†öÖT'Vb’²‡6—¦U÷B–¢¢45%õs°¢V–çCe÷B¢†ÒÒ†öÖT'Vb²‡6—¦U÷B–¢¢45%õs°¢f÷"†–çB’Ò²’Â45%õs²’²²—°¢–çB6’Ò’Ò6ƒ²–b‡6’Â’6’Ò²–b‡6’ãÒ45%õr’6’Ò45%õrÒ°¢E¶•ÒÒÖ—ƒScR†&u¶•ÒÂ†Õ·6•ÒÂ“°¢Ð¢Ð¢&W6VçBƒÂ45%ô‚Ò“°¢–b†RãÒGW"’'&V³°¢Ð¢6†÷t†öÖR‚“°§Ð§7FF–2fö–BÇ7U6fU–â‚—²&Vg2æ&Vv–â‚&fÆW†÷2"ÂfÇ6R“²&Vg2çWE7G&–ær‚&Æö6·–â"ÂÇ7U–â“²&Vg2çWD–çB‚&Æö6·G—R"Â“²&Vg2æVæB‚“²tÆö6µG—RÒ²Ç7TW†—B‚“²Ð§7FF–2fö–BÇ7U6fU72‚—²&Vg2æ&Vv–â‚&fÆW†÷2"ÂfÇ6R“²&Vg2çWE7G&–ær‚&Æö6·72"ÂÇ7U72“²&Vg2çWD–çB‚&Æö6·G—R"Â"“²&Vg2æVæB‚“²tÆö6µG—RÒ#²Ç7TW†—B‚“²Ð§7FF–2fö–BÇ7T&6²‚—²V–çCe÷B2ÒÇ7UG‡D†’‚“²7G&ö¶U6Vtƒ3Â#bÂ‚Â‚Â"ãFbÂ2“²7G&ö¶U6Vtƒ‚Â‚Â3ÂÂ"ãFbÂ2“²Ð ¢òòÒÒÒÒ6VÆV7F÷"”âò6öçG&6\;ÒÒÒÐ§7FF–2fö–BÇ7U&VæFW%6VÂ‚—°¢6WD'Vb†&'Vb“°¢f–ÆÅ&V7BƒÂÂ45%õrÂ45%ô‚ÂÇ7T&t6öÂ‚’“°¢Ç7T&6²‚“°¢G&uFW‡D2…45%õrò"ÂsBÂ$&Æ÷VVòFRçFÆÆ"Â2ÂÇ7UG‡D†’‚’“°¢G&uFW‡D2…45%õrò"Â‚Â$VÆ–vRVâÖWFöFò"Â"ÂÇ7UG‡DÆò‚’“°¢–çB'rÒ45%õrÒƒÂ&‚Ò#Â“Ò##Â“"Ò“²&‚²3°¢6öç7B6†"¢Æ&Å³%ÒÒ²%”â"Â$6öçG&6UÇ„35Ç„#"&"Ó²–çB—5³%ÒÒ²“Â“"Ó°¢f÷"†–çB²Ò²²Â#²²²²—°¢–b‡V”vÆ72—²G&tÆ—V–DvÆ75æVÂƒCÂ—5¶µÒÂ'rÂ&‚Â#"Â&v#ScRƒSÃ“Ã#’“²Ð¢VÇ6Rf–ÆÅ&÷VæE&V7BƒCÂ—5¶µÒÂ'rÂ&‚Â#"Â&v#ScRƒCbÃƒ"Ãƒ"’“°¢G&uFW‡D2…45%õrò"Â—5¶µÒ²&‚ò"Ò‚ÂÆ&Å¶µÒÂBÂ&v#ScRƒ#SRÃ#SRÃ#SR’“°¢Ð¢&W6VçBƒÂ45%ô‚Ò“°§Ð ¢òòÒÒÒÒçFÆÆ”â‡FV6ÆFòçVÖW&–6òÆ—V–BvÆ72²fVVF&6²FRFV6ÆVò’ÒÒÒÐ§7FF–2fö–BÇ7U–å&V7B†–çB’Â–çBg‚Â–çBg’Â–çBgrÂ–çBf‚—°¢–çB2Ò’R2Â"Ò’ò2Â'rÒ3"Â&‚Òƒ"ÂvÒ#°¢–çBF÷BÒ2¢'r²"¢vÂƒÒ…45%õrÒF÷B’ò"Â“Ò3°¢‚Òƒ²2¢†'r²v“²’Ò“²"¢†&‚²v“²rÒ's²‚Ò&ƒ°§Ð§7FF–2fö–BÇ7T6ö×÷6U–â‚—²òò&6RFRf–G&–òVâÆö6´'Vb…4ôÄòVæfW¢¢6WD'Vb†Æö6´'Vb“°¢Ç7T&r‚“°¢Ç7T&6²‚“°¢G&uFW‡D2…45%õrò"ÂcÂÇ7UfW&–g’ò$–çG&öGV6RVÂ”â"¢$7&V"”â"ÂÇ7UfW&–g’ò2¢BÂÇ7UG‡D†’‚’“°¢f÷"†–çB’Ò²’Â#²’²²—°¢–çB‚Â’ÂrÂƒ²Ç7U–å&V7B†’Â‚Â’ÂrÂ‚“°¢–b‡V”vÆ72—²G&tÆ—V–DvÆ75æVÂ‡‚Â’ÂrÂ‚ÂbÂÇ7TvÆ746öÂ‚’“²Ð¢VÇ6Rf–ÆÅ&÷VæE&V7B‡‚Â’ÂrÂ‚ÂbÂÇ7T6&D6öÂ‚’“°¢V–çCe÷B6öÂÒ†’ÓÒ’’ò&v#ScRƒ#3ÃƒÃ“’¢†’ÓÒ’ò&v#ScRƒ#Ã##ÃS’¢Ç7UG‡D†’‚“°¢G&uFW‡D2‡‚²rò"Â’²‚ò"Ò"Â”åô´U•5¶•ÒÂ2Â6öÂ“°¢Ð¢6WD'Vb†&'Vb“°§Ð¢òòVÂ&–ÖW"7VG&òFVÂ”âVçG&6öâVÂgVæF–FòFRÆG&ç6–6–öâFR6VwW&–F@¢òò‡6’Æ‡V&ò’âWF„fFT–âFWgVVÇfRfÇ6R7VæFòæò†’G&ç6–6–öâVæF–VçFP¢òòÒÒæV¢âÂ5$T"VÂ”âFW6FR§W7FW2ÒÒ’VçFöæ6W26RV&Æ–6FRVæfW¢À¢òòW†7FÖVçFR6öÖòçFW2à§7FF–2fö–BÇ7U6†÷u–â‚—°¢Ç7T6ö×÷6U–â‚“°¢–b†WF„fFT–â†Æö6´'Vb’’&WGW&ã°¢ÖVÖ7’†&'VbÂÆö6´'VbÂ‡6—¦U÷B•45%õr¢45%ô‚¢"“²&W6VçBƒÂ45%ô‚Ò“°§Ð§7FF–2fö–BÇ7Tæ–Õ–â‚—²òòVçF÷2F–æÖ–6÷2²FW7FVÆÆò²fÆ6‚„äò&RÖFW6Væfö6¢6WD'Vb†&'Vb“°¢òòd4R¢6‚Ò6öÆòGW&çFRÆ÷2ãbg&ÖW2FRÆ67VF–FâÆ&æF6P¢òò6÷–DU5Ä¤DVâ†÷&—¦öçFÂÂ6’VRVçF÷2’FV6ÆFò6R×VWfVâ§VçF÷0¢òò6–âföÇfW"F–'V¦"æ’VâæVÂFRf–G&–ó²Æ÷2&÷&FW26R&VÆÆVæà¢òò&W—F–VæFòÆ6öÇVÖæW‡G&VÖ†6Æ×’ÂçVæ66öâæVw&òà¢–çB6‚ÒÇ7U6†¶Töfb‚“°¢–b‡6‚ÓÒ—°¢f÷"†–çB¢Ò#²¢Âs²¢²²’ÖVÖ7’†&'Vb²‡6—¦U÷B–¢¢45%õrÂÆö6´'Vb²‡6—¦U÷B–¢¢45%õrÂ45%õr¢"“°¢ÒVÇ6R°¢f÷"†–çB¢Ò#²¢Âs²¢²²—°¢V–çCe÷B¢BÒ&'Vb²‡6—¦U÷B–¢¢45%õs°¢6öç7BV–çCe÷B¢2ÒÆö6´'Vb²‡6—¦U÷B–¢¢45%õs°¢f÷"†–çB’Ò²’Â45%õs²’²²—°¢–çB6’Ò’Ò6ƒ²–b‡6’Â’6’Ò²–b‡6’ãÒ45%õr’6’Ò45%õrÒ°¢E¶•ÒÒ5·6•Ó°¢Ð¢Ð¢Ð¢–çBâÒ7G&ÆVâ†Ç7U–â“°¢V–çCe÷BF2Ò†Ç7Uw&öærbbÖ–ÆÆ—2‚’ÒÇ7Uw&öærÂS’ò&v#ScRƒ#3RÃsÃs’¢&v#ScRƒ“ÃSÃ#C“°¢f÷"†–çB’Ò²’Âƒ²’²²—²–çB7‚Ò45%õrò"ÒB¢#‚²B²’¢#‚²6ƒ²–b†’Ââ’f–ÆÄ6—&6ÆR†7‚ÂSÂ‚ÂF2“²VÇ6RG&t6—&6ÆR†7‚ÂSÂ‚Â&v#ScRƒ“ÃÃ3’“²Ð¢f÷"†–çB’Ò²’Â#²’²²—°¢–çB‚Â’ÂrÂƒ²Ç7U–å&V7B†’Â‚Â’ÂrÂ‚“°¢–b†’ÓÒÇ7U&W72—²fÆöBÒ†Ö–ÆÆ—2‚’ÒÇ7U&W74×2’ò#ãc²–b‡Â’f–ÆÅ&÷VæE&V7D‡‚²6‚Â’ÂrÂ‚ÂbÂ&v#ScRƒ#SRÃ#SRÃ#SR’Â‡V–çC…÷B’‚ƒÒ’¢“’“²VÇ6RÇ7U&W72ÒÓ²Ð¢Ð¢&W6VçBƒ#Âs“°§Ð ¢òòÒÒÒÒFV6ÆFòÆfçVÖW&–6ò&6öçG&6\;†6öâöfg6WB&VÂ6Æ–FR’ÒÒÒÐ¢òò†öfbÒ67VF–F†÷&—¦öçFÂFRÆd4RâVÂæVÂFRföæFò6RF–'V¦4”à¢òòFW7Æ¦"’6öÆòÆ2FV6Æ26R×VWfVâFVçG&òFRVÂÂ6’VRÆ67VF–FçVæ6¢òòFV¦Væg&æ¦f6–VâÆ÷2&÷&FW2FRÆçFÆÆà§7FF–2fö–BÇ7TG&t¶"†–çB–öfbÂ–çB†öfb—°¢–çB·’Ò´%õ’²–öfc°¢òòV’äò†’&'&7WW&–÷"æ’6†—2†¶$W‡G&4öâVVFVâfÇ6RVâW7F¢òòçFÆÆ’Â6’VRVÂæVÂV×–W¦§W7FòVæ6–ÖFRÆ2FV6Æ2Â–wVÂVP¢òò6–V×&RâÆòVæ–6òVR6Ö&–6öâÆf6RW2VRVÂFÖæò–æòW2f–¦òà¢–b‡V”vÆ72—²G&tÆ—V–DvÆ75æVÂƒÂ·’ÒBÂ45%õrÂ45%ô‚Ò†·’ÒB’ÂÂÇ7T¶$vÆ72‚’“²Ð¢VÇ6Rf–ÆÅ&V7BƒÂ·’ÒBÂ45%õrÂ45%ô‚Ò†·’ÒB’ÂÇ7T¶$&t6öÂ‚’“°¢–çBg2Ò¶$föçE6—¦R‚“°¢f÷"†–çB"Ò²"Â´%õ$õu3²"²²’f÷"†–çB2Ò²2Â´%ô4ôÅ3²2²²—°¢–çB‚Ò´%õ‚²2¢„´%ôµr²´%ôt’²†öfbÂ’Ò·’²"¢„´%ô´‚²´%ôt“°¢6öç7B6†"¢²ÒÖ7F—fõ·%Õ¶5Ó°¢6†"U³eÓ°¢–b†¶%6†–gBbbµ³ÒÓÒbbµ³ÒãÒvrbbµ³ÒÃÒw¢r—²U³ÒÒ†6†"’†µ³ÒÒ3"“²U³ÒÒ²²ÒS²Ð¢–çB6VÆÂÒ"¢´%ô4ôÅ2²3°¢¶%–çD¶W’‡‚Â’Â´%ôµrÂ´%ô´‚Â²Âg2ÂÇ7T¶W”6öÂ‚’ÂÇ7T¶W•G‡B‚’Â¶$6VÆÄ†VÆB†6VÆÂ’ÇÂ¶$g„ÆWfVÂ†6VÆÂ’â“°¢Ð¢–çBg’Ò·’²2¢„´%ô´‚²´%ôt“°¢6öç7B6†"¢Æ%´´%ôd´U•5ÒÒ²'6†–gB"Â¶$Æ–W$Æ&VÂ‚’Â¶$ÆætW2ò$U2"¢$Tâ"Â&W76–ò"Â#ÂÒ"Â$ô²"Ó°¢f÷"†–çB’Ò²’Â´%ôd´U•3²’²²’¶$d¶W’†¶$d¶W•‚†’’²†öfbÂg’Â¶$d¶W•r†’’ÂÆ%¶•ÒÂ†’ÓÒ’bb¶%6†–gB“°§Ð¢òò–çFFòFRÆçFÆÆFR6öçG&6VæVâVÂt'Vb5ETÂ‡6–âV&Æ–6"’â6P¢òò6W&òFVÂföÆ6Fò&öFW"6ö×öæW&ÆgVW&FRçFÆÆ’W6&Æ6öÖð¢òòFW7F–æòFVÂgVæF–FòFRVçG&FFRÆG&ç6–6–öâFR6VwW&–FBà§7FF–2fö–BÇ7U–çE72†–çB–öfbÂ–çB†öfb—°¢Ç7T&r‚“°¢Ç7T&6²‚“°¢G&uFW‡D2…45%õrò"ÂSÂÇ7UfW&–g’ò$–çG&öGV6R6öçG&6UÇ„35Ç„#"&"¢$7&V"6öçG&6UÇ„35Ç„#"&"Â2ÂÇ7UG‡D†’‚’“°¢–çB6çBÒWFc„6÷VçB†Ç7U72“°¢f÷"†–çB’Ò²’Â6çBbb’Âƒ²’²²’f–ÆÄ6—&6ÆRƒ3²’¢#B²†öfbÂ#ÂrÂ&v#ScRƒ“ÃSÃ#C’“°¢Ç7TG&t¶"‡–öfbÂ†öfb“°§Ð§7FF–2fö–BÇ7U&VæFW%72†–çB–öfbÂ–çB†öfb—°¢6WD'Vb†&'Vb“°¢Ç7U–çE72‡–öfbÂ†öfb“°¢&W6VçBƒÂ45%ô‚Ò“°§Ð¢òò&–ÖW"7VG&òFRÆ6öçG&6Væ¢6R6ö×öæR6öâVÂFV6ÆFòFöFf–eTU$FP¢òòçFÆÆÂ6RgVæFRFW6FRVÂföæFò’'F—"FR†’'&æ6VÂFW6Æ—¦Ö–VçFð¢òòFVÂFV6ÆFòFR6–V×&Râ6’Æ&–6–öâW2gVæF–Fò²FW6Æ—¦Ö–VçFòÂæòVà¢òò6ÇFò6V6òà§7FF–2fö–BÇ7U6†÷u74f—'7B‚—°¢–b†WF„fFUVæF–ærbbWF…6æ—°¢V–çCe÷B¢öÆBÒt'Vc°¢t'VbÒWF…6æ²òòÆ–Vç¦ògVW&FRçFÆÆ‡–æò†6RfÇFÆ–ç7FçFæV¢Ç7U–çE72…45%ô‚Ò´%õ’Â“°¢t'VbÒöÆC°¢WF„fFT–â†WF…6æ“°¢Ð¢Ç7T¶$æ–ÒÒÖ–ÆÆ—2‚“°§Ð §7FF–2fö–BÇ7TVçFW"‚—°¢òòd4RC¢Vâ¶–÷66òäò6RVVFR6Ö&–"Æ6ÆfRFVÂ6—7FVÖâ6’Æ6ÆfF¢òògVW&§W7FW2ÂV–VâFVævVÂFVÆVföæòVçG&&–Vâ6VwW&–FBÓâ&Æ÷VVòÂ6P¢òòöæG&–Vâ”âçVWfò’6ÆG&–6öâVÃ¢ÆVæ–6ÆÆfRFVÂ¶–÷66òW2Æ6ÆfP¢òòVR–W7F&VW7FçFW2FR&W7F&Æòà¢–b„´”õ4µôôâbb¶–÷6´öâ’&WGW&ã°¢u7FFRÒ5EôÄô4µ4UEU²Ç7TÖöFRÒÅ5Uõ4TÃ²Ç7U–å³ÒÒ²Ç7U75³ÒÒ²Ç7U&W72ÒÓ²Ç7T¶$æ–ÒÒ°¢Ö7F—fòÒÄ”õUEôU3²¶$ÆætW2ÒG'VS²¶%6†–gBÒfÇ6S°¢¶$W‡G&4öâÒfÇ6S²¶$Ç•6—¦R‚“²¶$×E7W&f6U&W6WB‚“²òò6–â&'&æ’6†—2VâÆçFÆÆFR6ÆfP¢Ç7U&VæFW%6VÂ‚“°§Ð§7FF–2fö–BÇ7UF–6²‚—°¢–b†Ç7TÖöFRÓÒÅ5Uõ4TÂ—°¢òòVÂ6VÆV7F÷"W2U5DD”4ò†Æò–çFÇ7TVçFW"“¢æò†’æFVRæ–Ö"V’à¢–b…BçF—°¢–b…Bç‚ÂC‚bbBç’ÂC‚—²Ç7TW†—B‚“²&WGW&ã²Ð¢–çB'rÒ45%õrÒƒÂ“Ò##Â&‚Ò#Â“"Ò“²&‚²3°¢–b…Bç‚ãÒCbbBç‚ÃÒC²'rbbBç’ãÒ“bbBç’ÃÒ“²&‚—²Ç7TÖöFRÒÅ5Uõ”ã²Ç7U6†÷u–â‚“²&WGW&ã²Ð¢–b…Bç‚ãÒCbbBç‚ÃÒC²'rbbBç’ãÒ“"bbBç’ÃÒ“"²&‚—²Ç7TÖöFRÒÅ5Uõ53²Ç7T¶$æ–ÒÒÖ–ÆÆ—2‚“²&WGW&ã²Ð¢Ð¢&WGW&ã°¢Ð¢–b†Ç7TÖöFRÓÒÅ5Uõ”â—°¢òòd4RÒÆ67VF–Ff$”ÔU$ó¢Ö–VçG&2GW&‡ãbg&ÖW2’VÂFV6ÆFòæð¢òò6WFVÇ66–öæW2Â’6öÆò7VæFòFW&Ö–æ&V6RVÂ6öçFF÷"FRW7W&à¢–b†Ç7U6†¶T×2—°¢–b†Ö–ÆÆ—2‚’ÒÇ7Tæ–Ô×2â3—²Ç7Tæ–Ô×2ÒÖ–ÆÆ—2‚“²Ç7Tæ–Õ–â‚“²Ð¢&WGW&ã°¢Ð¢òòd4RÒW7W&f÷'¦F¢VÂFV6ÆFòW7F–æW'FR’6öÆò6Ræ–ÖVÀ¢òò6öçFF÷"&Vw&W6—fò÷"F–ff–ærâ6–âFVÆ’‚’&Æ÷VVçFS¢W7FòW2Và¢òòW7FFò6öâÖ&6FRF–V×òVR6RWfÇVVâ6FgVVÇFFVÂÆö÷à¢–b†Æö6µv—D7F—fR‚’—°¢òò6RW&Ö—FR6Æ—"6öâÆfÆV6†¢ÆW7W&äò6R–W&FRÂ6Æ—"‡6–wVP¢òòf—fVâÆö6µv—EVçF–Â’Â6’VRW7FòæòW2Væf–FRW66RÒÒ6öÆð¢òòWf—FVVF'6R6–æ6òÖ–çWF÷2G&FòVâVæçFÆÆ–æW'FRà¢–b…BçFbbBç‚ÂC‚bbBç’ÂC‚—²Æö6µv—E&W6WB‚“²Ç7TW†—B‚“²&WGW&ã²Ð¢Æö6µv—EF–6²‚“°¢&WGW&ã°¢Ð¢òò6R7V×Æ–òÆW7W&¢VÂ6öçFF÷"6R&÷'&VâVÂÔ•4Ôò&W6VçB‚’6öâVÀ¢òòVRÇ7Tæ–Õ–â&W–çF7R&æFƒ#âãsÂVR–6öçF–VæRW7F¦öæ’À¢òòæòVâVâföÆ6Fò'FRà¢–b†Æö6µv—E–çFVB—²Æö6µv—E&W6WB‚“²Ç7Tæ–Ô×2Ò²Ð¢–b…BçF—°¢–b…Bç‚ÂC‚bbBç’ÂC‚—²Ç7TW†—B‚“²&WGW&ã²Ð¢f÷"†–çB’Ò²’Â#²’²²—²–çB‚Â’ÂrÂƒ²Ç7U–å&V7B†’Â‚Â’ÂrÂ‚“°¢–b…Bç‚ãÒ‚bbBç‚ÃÒ‚²rbbBç’ãÒ’bbBç’ÃÒ’²‚—°¢Ç7U&W72Ò“²Ç7U&W74×2ÒÖ–ÆÆ—2‚“°¢–b†’ÓÒ’—²–çBÂÒ7G&ÆVâ†Ç7U–â“²–b„Ââ’Ç7U–å´ÂÒÒÒ²Òòò&÷'& ¢VÇ6R–b†’ÓÒ—²òòô°¢–b†Ç7UfW&–g’—²–b‚7G&6×†Ç7U–âÂÇ7U6fVB’’Ç7UVæÆö6²‚“²VÇ6R²Ç7Uw&öærÒÖ–ÆÆ—2‚“²Ç7U–å³ÒÒ²Æö6´öäf–Â‚“²Ç7U6†¶U7F'B‚“²Ò&WGW&ã²Ð¢VÇ6R–b‡7G&ÆVâ†Ç7U–â’ãÒB—²Ç7U6fU–â‚“²&WGW&ã²Ð¢Ð¢VÇ6R–b‡7G&ÆVâ†Ç7U–â’Â‚—²òòF–v—Fð¢–çBÂÒ7G&ÆVâ†Ç7U–â“²Ç7U–å´ÅÒÒ”åô´U•5¶•Õ³Ó²Ç7U–å´Â²ÒÒ°¢–b†Ç7UfW&–g’bb†–çB—7G&ÆVâ†Ç7U–â’ÓÒ†–çB—7G&ÆVâ†Ç7U6fVB’bb7G&ÆVâ†Ç7U6fVB’â—°¢–b‚7G&6×†Ç7U–âÂÇ7U6fVB’’Ç7UVæÆö6²‚“²VÇ6R²Ç7Uw&öærÒÖ–ÆÆ—2‚“²Ç7U–å³ÒÒ²Æö6´öäf–Â‚“²Ç7U6†¶U7F'B‚“²Ð¢&WGW&ã°¢Ð¢Ð¢'&V³°¢Ð¢Ð¢Ð¢–b†Ö–ÆÆ—2‚’ÒÇ7Tæ–Ô×2â3—²Ç7Tæ–Ô×2ÒÖ–ÆÆ—2‚“²Ç7Tæ–Õ–â‚“²Òòòæ–Ò6öâF‡&÷GFÆR‡&W7öç6—fò¢&WGW&ã°¢Ð¢òòÅ5Uõ50¢–b†Ç7T¶$æ–Ò—²òòæ–Ö6–öâFRW'GW&FVÂFV6ÆFòÒã72U„5Dõ0¢fÆöBÒ†Ö–ÆÆ—2‚’ÒÇ7T¶$æ–Ò’ò3ãc²–b‡ãÒ—²Ò²Ç7T¶$æ–ÒÒ²Ð¢–çB¶&‚Ò45%ô‚Ò´%õ“°¢Ç7U&VæFW%72‚†–çB’‚ƒãbÒ’¢¶&‚’Â“°¢&WGW&ã°¢Ð¢òòd4RÒÖ—6Ö6V7VVæ6–VRVâVÂ”ã¢&–ÖW&ò67VF—"ÂÇVVvòW7W&"à¢òòV’ÆòVR6R×VWfR6öâÆ2DT4Ä2FVçG&òFR7RæVÂ†VÂ6×òFRVçF÷0¢òòVVFf6–òÂfÆÆ"Â6’VR67VF—&Æòæò6RfW&–’à¢–b†Ç7U6†¶T×2—°¢–b†Ö–ÆÆ—2‚’ÒÇ7Tæ–Ô×2â3—²Ç7Tæ–Ô×2ÒÖ–ÆÆ—2‚“²Ç7U&VæFW%72ƒÂÇ7U6†¶Töfb‚’“²Ð¢&WGW&ã°¢Ð¢–b†Æö6µv—D7F—fR‚’—°¢–b…BçFbbBç‚ÂC‚bbBç’ÂC‚—²Æö6µv—E&W6WB‚“²Ç7TW†—B‚“²&WGW&ã²Ð¢Æö6µv—EF–6²‚“°¢&WGW&ã°¢Ð¢òòV’VÂ&÷'&Fò6ÆRw&F—2VâVÂÖ—6ÖòföÆ6Fó¢Ç7U&VæFW%72&V6ö×öæRÆ¢òòçFÆÆ6ö×ÆWFFRVæfW¢à¢–b†Æö6µv—E–çFVB—²Æö6µv—E&W6WB‚“²Ç7U&VæFW%72ƒÂ“²Ð¢òòd4RrÒv"VÂFW7FVÆÆòFRÆVÇF–ÖFV6ÆÂ7V×Æ—"7RF–V×òà¢¶$g…F–6²†Ç7T¶W”6öÂ‚’ÂÇ7T¶W•G‡B‚’“°¢òòd4R"Òf–&–FFÖ&–VâV“¢W67&–&—"Æ6öçG&6Væ&–FòæòFV&W&–¢òòW&FW"ÆWG&2âÆ2FV6Æ2FReTä4”ôâVR6öæf—&Öâò&÷'&â„ô²ÂÂÒ’äð¢òòVçG&â÷"W7Ff–¢Væ6öæf—&Ö6–öâF–VæRVR6Æ—"FRVâF÷VP¢òòFVÆ–&W&FòÂæòFRVâ&ö6RÖ–VçG&2VÂFVFòçFW&–÷"6RÆWfçFà¢–b…BæF÷vâbbBç’ãÒ´%õ’Ò‚’¶%G—–ætÖ&²‚“²òòfWFòFVÂvW7FòFR7W7Vç6–öâÖ–VçG&26RFV6ÆV¢–b„´%ôÕTÅD•DõT4…ôôâbbt¶$f7EG—R—°¢–çBâÒ¶$×EöÆÂ‚“°¢&ööÂw&÷FRÒfÇ6S°¢f÷"†–çBRÒ²RÂã²R²²—°¢–çB6VÆÂÒ¶$Wd6VÆÅ¶UÓ°¢–b†6VÆÂÂ’6öçF–çVS°¢6öç7B6†"¢²ÒÖ7F—fõ¶6VÆÂò´%ô4ôÅ5Õ¶6VÆÂR´%ô4ôÅ5Ó°¢–b†¶%6†–gBbbµ³ÒÓÒbbµ³ÒãÒvrbbµ³ÒÃÒw¢r—²6†"U³%ÒÒ²†6†"’†µ³ÒÒ3"’ÂÓ²Ç7U74VæB‡R“²¶%6†–gBÒfÇ6S²Ð¢VÇ6RÇ7U74VæB†²“°¢¶$g…7F'B†6VÆÂ“²w&÷FRÒG'VS°¢Ð¢–b‡w&÷FR’Ç7U&VæFW%72ƒÂ“°¢Ð¢òòd4Rs¢Ö—6Öò7&—FW&–òVRVâæ÷F2ÒÒ6–âW67&—GW&&–FÂVÂFW7FVÆÆòf¢òòVâVÂfÆæ6òFR$U4”ôä"ÂVR6’æòÆFV6ÆæòFæ–æwVæ6VæÂà¢–b…Bç&W76VBbb„´%ôÕTÅD•DõT4…ôôâbbt¶$f7EG—R’’¶$g…&W72†¶$6VÆÄB…Bç‚ÂBç’’ÂÇ7T¶W”6öÂ‚’ÂÇ7T¶W•G‡B‚’“°¢–b…BçF—°¢–b…Bç‚ÂC‚bbBç’ÂC‚—²Ç7TW†—B‚“²&WGW&ã²Ð¢–çBf’Ò¶$e&÷t†—B…Bç‚ÂBç’“°¢–b†f’ãÒ—°¢–b†f’ÓÒ’¶%6†–gBÒ¶%6†–gC°¢VÇ6R–b†f’ÓÒ’Ö7F—fòÒ†Ö7F—fòÓÒÄ”õUEôåTÒ’òÄ”õUEôTÔô¤’¢†Ö7F—fòÓÒÄ”õUEôTÔô¤’’ò†¶$ÆætW2òÄ”õUEôU2¢Ä”õUEôTâ’¢Ä”õUEôåTÓ°¢VÇ6R–b†f’ÓÒ"—²¶$ÆætW2Ò¶$ÆætW3²–b†Ö7F—fòÓÒÄ”õUEôU2ÇÂÖ7F—fòÓÒÄ”õUEôTâ’Ö7F—fòÒ¶$ÆætW2òÄ”õUEôU2¢Ä”õUEôTã²Ð¢VÇ6R–b†f’ÓÒ2’Ç7U74VæB‚""“°¢VÇ6R–b†f’ÓÒB—²–çBÂÒ7G&ÆVâ†Ç7U72“²–b„Ââ—²–çBÒÂÒ²v†–ÆR‡âbb†Ç7U75·Òb„3’ÓÒƒƒ’ÒÓ²Ç7U75·ÒÒ²ÒÐ¢VÇ6R²–b†Ç7UfW&–g’—²–b‚7G&6×†Ç7U72ÂÇ7U6fVB’’Ç7UVæÆö6²‚“²VÇ6R²Ç7Uw&öærÒÖ–ÆÆ—2‚“²Ç7U75³ÒÒ²Æö6´öäf–Â‚“²Ç7U6†¶U7F'B‚“²Ç7U&VæFW%72ƒÂ“²Ò&WGW&ã²ÒVÇ6R–b‡7G&ÆVâ†Ç7U72’ãÒB—²Ç7U6fU72‚“²&WGW&ã²ÒÐ¢Ç7U&VæFW%72ƒÂ“²&WGW&ã°¢Ð¢–b†¶$f7D7F—fR‚’’&WGW&ã²òò–ÆW67&–&–òÆf–&–F¢–çB6VÆÂÒ¶$6VÆÄB…Bç‚ÂBç’“°¢–b†6VÆÂãÒ—°¢6öç7B6†"¢²ÒÖ7F—fõ¶6VÆÂò´%ô4ôÅ5Õ¶6VÆÂR´%ô4ôÅ5Ó°¢–b†¶%6†–gBbbµ³ÒÓÒbbµ³ÒãÒvrbbµ³ÒÃÒw¢r—²6†"U³%ÒÒ²†6†"’†µ³ÒÒ3"’ÂÓ²Ç7U74VæB‡R“²¶%6†–gBÒfÇ6S²Ð¢VÇ6RÇ7U74VæB†²“°¢¶$g…7F'B†6VÆÂ“°¢Ç7U&VæFW%72ƒÂ“°¢Ð¢Ð§Ð §7FF–2fö–BÇ7U7F'EfW&–g’‚—°¢òò$TBDR4TuU$”DB†Ö—6ÖVR6Æ÷6Rö7F—f$×VÇF—F&V“¢ÆT’FP¢òòfW&–f–66–öâ4”TÕ$R6R6ö×öæRVâ÷'G&—B’çFÆÆ6ö×ÆWFâ6–âW7FòÀ¢òòÆÆVv"V’FW6FRVæÆæG66RÔ§VVv÷2W2ÆVæ–66öâôÄäBÂ¢òòvVõ&VæFW$vÖRöæRtÆæC×G'VRVâ4Dg&ÖRÒ–çF&VÂFV6ÆFòv—&Fò¢òò&V6÷'FFó²’6öÖòVÂ&VÖVòFVÂF7F–ÂFÖ&–VâFWVæFRFRtÆæBÂÆ÷0¢òòF–v—F÷2æò6–âFöæFR6RfV–ã¢æò†&–f÷&ÖFRW67&–&—"VÂ”âæ’FP¢òò6Æ—"FRW6çFÆÆâæò6R&W7FW&ÂFW&Ö–æ"$õõ4•Dó¢V–VâgVVÇf¢òòVæÆæG66RÆò†6Rf–VçFW$ÓâvVôVçFW"ÂVRöæR7RtÆæC×G'VP¢òò÷"7R7VVçFÂVâfW¢FR†W&VF&ÆòFRÆçFÆÆFRfW&–f–66–öâà¢tÆæBÒfÇ6S°¢t6Æ—ƒÒ²t6Æ—ƒÒ45%õrÒ²t6Æ—“Ò²t6Æ—“Ò45%ô‚Ò°¢6WD'Vb†f"“°¢&Vg2æ&Vv–â‚&fÆW†÷2"ÂG'VR“°¢7G&–ær2Ò†tÆö6µG—RÓÒ’ò&Vg2ævWE7G&–ær‚&Æö6·–â"Â""’¢&Vg2ævWE7G&–ær‚&Æö6·72"Â""“°¢&Vg2æVæB‚“°¢2çFô6†$'&’†Ç7U6fVBÂ6—¦Vöb†Ç7U6fVB’“°¢Ç7UfW&–g’ÒG'VS²Ç7Uw&öærÒ²Ç7U–å³ÒÒ²Ç7U75³ÒÒ²Ç7U&W72ÒÓ°¢Ö7F—fòÒÄ”õUEôU3²¶$ÆætW2ÒG'VS²¶%6†–gBÒfÇ6S°¢¶$W‡G&4öâÒfÇ6S²¶$Ç•6—¦R‚“²¶$×E7W&f6U&W6WB‚“²òò6–â&'&æ’6†—2VâÆfW&–f–66–öà¢Vç7W&T&ÇW$&r‚“°¢u7FFRÒ5EôÄô4µ4UEU°¢òò÷"FVfV7FòW7FfW&–f–66–öâW2ÆFRÆåDÄÄâÇ7U7F'EfW&–g”f÷"Æð¢òò&V§W7FFW7VW2FRÆÆÖ"V’Â6’VRæ–æwVæ'WFVVFR†W&VF"÷ ¢òò66–FVçFRVÂFW7F–æòFRVæfW&–f–66–öâçFW&–÷"à¢Ç7TgFW"ÒÅ5UôeDU%õTäÄô4³²Ç7TgFW$ÒÓ°¢Ç7U6†¶T×2Ò°¢Æö6µv—E–çFVBÒfÇ6S²Æö6µv—DÆ7E6V2ÒÓ°¢Æö6´&ÕVæF–æuVæÇG’‚“²òòd4R¢6öçFF÷"–ÇFòÓâ6R6ö'&çFW2FVÂ&–ÖW"–çFVçFð¢òòE$å4”4”ôã¢&–ÖW&ò6RfÆ–çFW&f¢7GVÂÂÇVVvòVçG&VÂÖWFöFòFP¢òò6VwW&–FB6öæf–wW&Fòâf§W7FòV’ÂFW7VW2FRFV¦"Æ—7FòVÂW7FFò¢òòåDU2FR–çF"VÂ&–ÖW"7VG&òFRÆ6ÆfRà¢WF„fFT÷WB‚“°¢–b†tÆö6µG—RÓÒ—²Ç7TÖöFRÒÅ5Uõ”ã²Ç7U6†÷u–â‚“²Ð¢VÇ6R²Ç7TÖöFRÒÅ5Uõ53²Ç7U6†÷u74f—'7B‚“²Ð§Ð ¢òò2222222222222222222222222222222222222222222222222222222222220¢òò225U5Tå4”ôâ²$ÄõTTð¢òò22ÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÐ¢òò227VW'òFR7W7v¶TÆö6µ67&VVâ‚’Â7W–ò&÷F÷F—òW7F'&–&¢òò22§VçFòÂFWFV7F÷"FRFö&ÆR×FâfT’$¤ò÷'VRæV6W6—F¢òò22u7FFRÂVF—DÖöFRÂu&—ÆT7F—fRÂ&VæFW$Æö6²’6†÷tÆö6²ÂVP¢òò22FöFf–æòW†—7FVâÆÆ'&–&à¢òò20¢òò22ÆÆÆÖ7W7v¶R‚’6öâÆçFÆÆDôDd”÷67W&2ÂçFW0¢òò22FVÂD•5ôâ’çFW2FRVRVÂ&6¶Æ–v‡BV×–V6R7V&—"â÷ ¢òò22W6òVÂ6öçFVæ–FòçFW&–÷"çVæ6ÆÆVvfW'6S¢7VæFò†’ÇW¢À¢òò22ÆòVR†’VâVÂg&ÖV'VffW"–W2VÂ&Æ÷VVòà¢òò2222222222222222222222222222222222222222222222222222222222220§7FF–2fö–B7W7v¶TÆö6µ67&VVâ‚—°¢6–b5U5TäEôôâbb5U5TäEôÄô4µôôà¢òò6–â”âö6öçG&6Væ6öæf–wW&Fäò6R&Æ÷VVæF¢6W&–VF—&ÆRÂW7V&–ð¢òòVR&FW6&Æ÷VVR"6öâVæ6ÆfRVRæòW†—7FRâ6RFW7–W'FFöæFRW7F&À¢òòVRW2VÂ6ö×÷'FÖ–VçFòFR6–V×&Rà¢–b†tÆö6µG—RÓÒ’&WGW&ã°¢òò–W7F&VâVÂ&Æ÷VVò†òÖWF–VæFòÆ6ÆfR’Â7W7VæFW#¢æFVR†6W"¢òòæFVR&W7FW&"à¢–b†u7FFRÓÒ5EôÄô4²ÇÂu7FFRÓÒ5EôÄô4µ4UEU—²u7W7&WE7FFRÒÓ²u7W7&WDÒÓ²&WGW&ã²Ð¢òòFöæFRföÇfW"7VæFò6–W'FRÆ6ÆfR†Æò6öç7VÖRÆö6µ7F'EfW&–g’’à¢u7W7&WE7FFRÒu7FFS°¢u7W7&WDÒt–C°¢òòFV¦"VÂ6—7FVÖVâVâW7FFòÆ–×–òçFW2FRF"6öâVÂ&Æ÷VVòâäò6P¢òòÆÆÖ6Æ÷6R‚“¢6W'&"ÆV’W2§W7FòÆòVR†&–W&FW"VÂ6—F–ð¢òòÒÒÆ6RFV¦f—f’6RgVVÇfRVÆÆ6öâVçFW$‚’G&2FW6&Æ÷VV"à¢–b†VF—DÖöFR’VDW†—B‚“°¢u&—ÆT7F—fRÒfÇ6S°¢5æVÅ’Ò²òòÆ6÷'F–ææòVVFRVVF"ÖVF–ò'&—"&¦òVÂ&Æ÷VVð¢tÆæBÒfÇ6S²òòVÂ&Æ÷VVò4”TÕ$R6R6ö×öæRVâ÷'G&—B†2ÆæG66R¢t6Æ—ƒÒ²t6Æ—ƒÒ45%õrÒ²t6Æ—“Ò²t6Æ—“Ò45%ô‚Ò°¢u7FFRÒ5EôÄô4³²Æö6´öfbÒ²Æ7DÆö6´öfbÒÓ°¢&VæFW$Æö6²‚“°¢6†÷tÆö6²‚“°¢6VæF–`§Ð ¢òò2222222222222222222222222222222222222222222222222222222222220¢òò22tDò4ôÕÄUDò†FVW6ÆVW&VÂ¢òò22ÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÐ¢òò22'WF¢æVÂ&–FòÓâ6—&7VÆò$v""Óâ5EõõtU$ôdeô4ôäd•$Ð¢òò22‡6Æ–FW"&FW6Æ—¦&v""²6æ6VÆ"’Óâµ”â÷6–öæÅÐ¢òò22Óâ5EõõtU$ôdeôä”Ò†gVæF–FòæVw&ò²$fÆW‚õ2"²gVæF–FòFVÀ¢òò22FW‡Fò’Óâ&6¶Æ–v‡BÓâD52FR&¦ò6öç7VÖòÓâFVW6ÆVWà¢òò20¢òò22FöFò–çFW'öÆFò÷"Ö–ÆÆ—2‚’Â6W&òFVÆ’‚’&Æ÷VVçFRÂ6W&ð¢òò22f–ÆÅ67&VVâ‚’FRçFÆÆ6ö×ÆWF6öÖòÖV6æ—6ÖòFRvFòÂ¢òò22FöF6ö×÷6–6–öâ6÷"&'Vb²&W6VçB‚’à¢òò2222222222222222222222222222222222222222222222222222222222220 ¢òòÒÒÒÒvVöÖWG&–FRÆçFÆÆFR6öæf—&Ö6–öâÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÐ¢6FVf–æRôdeõE$4µõ‚Còò—7FFVÂ6Æ–FW ¢6FVf–æRôdeõE$4µõ’3S ¢6FVf–æRôdeõE$4µõr…45%õrÒƒ’òòC ¢6FVf–æRôdeõE$4µô‚“`¢6FVf–æRôdeõE$4µõ"…ôdeõE$4µô‚ò"¢6FVf–æRôdeô´äô%õBbòòÖ&vVâFVÂöÖòFVçG&òFRÆ—7F¢6FVf–æRôdeô´äô%ôB…ôdeõE$4µô‚Ò"¢ôdeô´äô%õB’òòƒ@¢6FVf–æRôdeô´äô%õ"…ôdeô´äô%ôBò"¢6FVf–æRôdeõ%Tâ…ôdeõE$4µõrÒ"¢ôdeô´äô%õBÒôdeô´äô%ôB’òò&V6÷'&–FòWF–ÂFVÂöÖð¢6FVf–æRôdeôDôäUõ5B“"òòRFVÂ&V6÷'&–FòVR7VVçF6öÖò&6ö×ÆWFFò ¢6FVf–æRôdeô$äEõ“…ôdeõE$4µõ’Ò‚’òò&æFVR6R&W–çF6Fg&ÖP¢6FVf–æRôdeô$äEõ“…ôdeõE$4µõ’²ôdeõE$4µô‚²‚¢6FVf–æRôdeô$äEô‚…ôdeô$äEõ“Òôdeô$äEõ“²¢6FVf–æRôdeô4åõ‚…45%õrò"Ò’òò&÷Föâ6æ6VÆ ¢6FVf–æRôdeô4åõ’Sc ¢6FVf–æRôdeô4åõr## ¢6FVf–æRôdeô4åô‚s ¢6FVf–æRôdeô4åõ"…ôdeô4åô‚ò" ¢òòÒÒÒÒF–V×÷2FRÆæ–Ö6–öâFRvFò‡FöFò–çFW'öÆFò’ÒÒÒÒÒÒÒÒÒÒÒÒÒÐ¢6FVf–æRôdeôdDUôÕ2S#òò’gVæF–FòæVw&òFRÆçFÆÆFR6öæf—&Ö6–öà¢6FVf–æRôdeõD”åôÕ2#còò"’&–6–öâFVÂFW‡Fò$fÆW‚õ2 ¢6FVf–æRôdeô„ôÄEôÕ2sòòâââ’7VçFò6RVVFV–WFð¢6FVf–æRôdeõDõUEôÕ2c#òò2’F—6öÇV6–öâFVÂFW‡Fð¢6FVf–æRôdeõE…Eõ’…45%ô‚ò"Ò#b’òòÆ–æV&6RFVÂFW‡Fò6VçG&Fð¢6FVf–æRôdeõE…Eõ5¢P¢òò&æFFVÂFW‡Fó¢ÆòTä”4òVR6Ö&–VâÆ2f6W2"’2â6—¦RRÆ6¦FP¢òòÆ–æVÖ–FRdôåEôÄ”äT‚¢föçE62ƒR’ÒS¢‚£RóSÒCƒ²6RFV¦Ö&vVâFP¢òò6ö'&÷"'&–&†66VæFVçFW2’’÷"&¦ò†FW66VæFVçFW2’&VRæ–æwVà¢òòvÆ–fòVVFVVF"&V6÷'FFò÷"VÂ6Æ—à¢6FVf–æRôdeõE…Eô%“…ôdeõE…Eõ’Ò#B¢6FVf–æRôdeõE…Eô%“…ôdeõE…Eõ’²ƒ ¢òòÒÒÒÒW7FFòÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÐ§7FF–2V–çCe÷B¢öfd&æBÒåTÄÃ²òò66†RFRÆ&æFU5DD”4FVÂ6Æ–FW"‡—7Ff6–²f–G&–ò§7FF–2–çBöfd¶æö"Ò²òò÷6–6–öâ7GVÂFVÂöÖòƒâåôdeõ%Tâ’Â–çFW'öÆF§7FF–2–çBöfeF&vWBÒ²òòö&¦WF—fòFVÂöÖð§7FF–2&ööÂöfdG&rÒfÇ6S²òò'&7G&RVâ7W'6ð§7FF–2–çBöfdw&"Ò²òòöfg6WBFVFò×öÖòÂv'&"†Wf—FVÂ6ÇFò–æ–6–Â§7FF–2–çBöfdÆ7D¶æö"ÒÓ²òòVÇF–ÖòöÖòF–'V¦Fò‡&æò&W–çF"FR&ÆFR§7FF–2V–çC…÷Böfe†6RÒ²òòÖgVæF–Fò×FW‡Fò–â#Ö†öÆB3×FW‡Fò÷WBCÖ&6¶Æ–v‡BSÖF÷&Ö— §7FF–2V–çC3%÷Böfe†6T×2Ò²òòÖ–ÆÆ—2‚’FVÂ–æ–6–òFRÆf6R7GVÀ ¢òòÆ&÷FV66–öâ÷"”âÆ–64ôÄòV’âfW"VÂ6öÖVçF&–òFRuöfe–ã¢Æ¢òò5U5Tå4”ôâçVæ6Æ6öç7VÇFà§7FF–2&ööÂöfe–å&WV—&VB‚—°¢6–bõtU$ôdeõ”åôôà¢&WGW&âuöfe–âbbtÆö6µG—Râ°¢6VÇ6P¢&WGW&âfÇ6S°¢6VæF–`§Ð ¢òòÒÒÒÒ&VæFW"ÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÐ¢òòF–'V¦Æ'FRU5DD”4†föæFòFRf–G&–òÂF—GVÆòÂ—7Ff6–Â6æ6VÆ"’Và¢òòVÂ'VffW"7F—fòâ6RÆÆÖVæ6öÆfW¢÷"VçG&FÆçFÆÆà§7FF–2fö–BöfdG&u7FF–2‚—°¢òòföæFó¢vÆÇW"&÷'&÷6ò–66†VFò†VÂÖ—6ÖòVRW6ÆfW&–f–66–öâFP¢òò”â’²VâfVÆò÷67W&ò&VRVÂæVÂFRf–G&–òFVæv6öçG&VRFW7F6"à¢–b†&ÇW$&r’ÖVÖ7’†t'VbÂ&ÇW$&rÂ‡6—¦U÷B•45%õr¢45%ô‚¢"“°¢VÇ6Rf–ÆÅ&V7BƒÂÂ45%õrÂ45%ô‚Â&v#ScRƒÃ"Ã#’“°¢f–ÆÅ&V7DƒÂÂ45%õrÂ45%ô‚Â&v#ScRƒ‚ÃÃ‚’ÂS“° ¢òòæVÂÆ—V–BvÆ72VçföÇfVçFS¢6R$UUD”Ä•¤G&tÆ—V–DvÆ75æVÄW‚FÂ7VÀ¢òò†6öâ7R6öÖ'&ö&ÇW"f&–&ÆRöW7V7VÆ"÷&Vg&66–öâ6VwVâÆ2fÆw2tÄ55ò¢’À¢òòæò6R&V–çfVçFæ–æwVâ6—7FVÖFRf–G&–òçVWfòà¢G&tÆ—V–DvÆ75æVÄW‚ƒ#‚Â#3"Â45%õrÒSbÂC#BÂCBÂ&v#ScRƒCÃ3ÃC"’Â’“° ¢G&uFW‡D2…45%õrò"Â#c‚Â%Ç„3%Ç„$b"$v"fÆW„õ3ò"Â2Â&v#ScRƒ#CRÃ#3‚Ã#C’“°¢G&uFW‡D2…45%õrò"Â3bÂ$VÂ6—7FVÖVçG&%Ç„35Ç„Vâ&W÷6ò&ögVæFò"ÂÂ&v#ScRƒ“‚Ã“Ã“b’“° ¢òò—7FFVÂ6Æ–FW"‡f6–’âVÂ&VÆÆVæò’VÂöÖò6öâF–æÖ–6÷2à¢f–ÆÅ&÷VæE&V7D…ôdeõE$4µõ‚²"ÂôdeõE$4µõ’²BÂôdeõE$4µõrÂôdeõE$4µô‚ÂôdeõE$4µõ"Â&v#ScRƒÃÃ’Âc“°¢G&tÆ—V–DvÆ75æVÄW‚…ôdeõE$4µõ‚ÂôdeõE$4µõ’ÂôdeõE$4µõrÂôdeõE$4µô‚ÂôdeõE$4µõ"Â&v#ScRƒS"ÃsBÃs’Âr“°¢G&u&÷VæE&V7B…ôdeõE$4µõ‚ÂôdeõE$4µõ’ÂôdeõE$4µõrÂôdeõE$4µô‚ÂôdeõE$4µõ"Â&v#ScRƒ#ÃSÃCB’“° ¢òò&÷Föâ6æ6VÆ"‡gVVÇfRÂW7FFò&Wf–ò6–âæ–æwVâVfV7Fò6V7VæF&–ò’à¢f–ÆÅ&÷VæE&V7D…ôdeô4åõ‚²"Âôdeô4åõ’²BÂôdeô4åõrÂôdeô4åô‚Âôdeô4åõ"Â&v#ScRƒÃÃ’Âc“°¢G&tÆ—V–DvÆ75æVÄW‚…ôdeô4åõ‚Âôdeô4åõ’Âôdeô4åõrÂôdeô4åô‚Âôdeô4åõ"Â&v#ScRƒS"ÃS‚Ãƒ’Âr“°¢G&u&÷VæE&V7B…ôdeô4åõ‚Âôdeô4åõ’Âôdeô4åõrÂôdeô4åô‚Âôdeô4åõ"Â&v#ScRƒ#Ã#‚ÃS’“°¢G&uFW‡D2…45%õrò"Âôdeô4åõ’²ôdeô4åô‚ò"Ò’Â$6æ6VÆ""Â"Â&v#ScRƒ#3RÃ#3‚Ã#C‚’“°§Ð¢òò–çFöÖò²&VÆÆVæò²&÷GVÆò6ö'&RÆ&æF–&W7FW&FFVÂ'VffW"7F—fòà§7FF–2fö–BöfdG&t¶æö"‚—°¢–çB·‚ÒôdeõE$4µõ‚²ôdeô´äô%õB²öfd¶æö#²òòW7V–æ—§âFVÂöÖð¢–çBÒôdeõ%Tââò‡öfd¶æö"¢òôdeõ%Tâ’¢²òòRFVÂ&V6÷'&–Fð ¢òòW7FVÆ¢VÂG&÷¦òFR—7F–&V6÷'&–Fò6RF–æRFR&ö¦òÂ6FfW¢Ö26öÆ–Fòà¢–çBgrÒöfd¶æö"²ôdeô´äô%ôB²"¢ôdeô´äô%õC°¢–b†grâôdeõE$4µõr’grÒôdeõE$4µõs°¢–b‡öfd¶æö"â¢f–ÆÅ&÷VæE&V7D…ôdeõE$4µõ‚ÂôdeõE$4µõ’ÂgrÂôdeõE$4µô‚ÂôdeõE$4µõ"À¢&v#ScRƒ#ÃcÃSR’Â‡V–çC…÷B’ƒC²¢3ò’“° ¢òò&÷GVÆò&FW6Æ—¦&v"#¢6RFW7fæV6R6öæf÷&ÖRVÂöÖòfç¦†Æ÷0¢òòR–æòW7F÷&&ÂöÖòÂVRW7F§W7FòVæ6–Ö’à¢–çBÆ&ÄÒ#3RÒ¢#°¢–b†Æ&Äâ¢G&uFW‡D4…ôdeõE$4µõ‚²ôdeõE$4µõrò"²‚ÂôdeõE$4µõ’²ôdeõE$4µô‚ò"Ò’À¢&FW6Æ—¦&v""Â"Â&v#ScRƒ#3bÃ#CÃ#3‚’Â‡V–çC…÷B–Æ&Ä“° ¢òòöÖò&Ææ6ò6öâVÂ6–Ö&öÆòFRvFòVâ&ö¦ò‡&VfW&Væ6–¢”õ2’à¢f–ÆÄ6—&6ÆT†·‚²ôdeô´äô%õ"²ÂôdeõE$4µõ’²ôdeõE$4µô‚ò"²"Âôdeô´äô%õ"Â&v#ScRƒÃÃ’Âs“°¢f–ÆÄ6—&6ÆR†·‚²ôdeô´äô%õ"ÂôdeõE$4µõ’²ôdeõE$4µô‚ò"Âôdeô´äô%õ"Â&v#ScRƒ#S"Ã#S"Ã#S"’“°¢5F–ÆT–6öâƒbÂ·‚²ôdeô´äô%õ"ÂôdeõE$4µõ’²ôdeõE$4µô‚ò"Â&v#ScRƒ##bÃCbÃC’“°§Ð¢òòVâg&ÖRFRÆçFÆÆFR6öæf—&Ö6–öââ4ôÄò6R&V6ö×öæRÆ&æFFVÀ¢òò6Æ–FW#¢VÂ&W7Fò‡F—GVÆòÂf–G&–òÂ6æ6VÆ"’W2W7FF–6ò’–W7FVâf"FW6FP¢òòöfdVçFW"‚’Â6’VRæò†’æFVR&W–çF"æ’æFVRVVF'FV"à§7FF–2fö–Böfe&VæFW$&æB‚—°¢òò6–âÆ66†Ræò6RVVFR$õ%$"VÂöÖòçFW&–÷"Â6’VR6R&Vf–W&Ræð¢òòæ–Ö"çFW2VRFV¦"Vâ&VwVW&òFRöÖ÷2VvF÷2âÆçFÆÆ6–wVP¢òò6–VæFòW6&ÆS¢VÂ&÷Föâ6æ6VÆ"6RF–VæFRVâöfeF–6²ÂçFW2FRÆÆVv ¢òòV’â…6öÆò6&–6’5$Ò6RVVF&6–âÆ÷2ã3R´"FVÂ'VffW"â¢–b‚öfd&æB’&WGW&ã°¢–b‡öfd¶æö"ÓÒöfdÆ7D¶æö"’&WGW&ã²òòæFVR†6W"W7FRg&ÖP¢öfdÆ7D¶æö"Òöfd¶æö#°¢ÖVÖ7’†&'Vb²‡6—¦U÷B•ôdeô$äEõ“¢45%õrÂöfd&æBÂ‡6—¦U÷B•ôdeô$äEô‚¢45%õr¢"“°¢V–çCe÷B¢öÆBÒt'Vc²6WD'Vb†&'Vb“°¢–çB3Òt6Æ—“Â3Òt6Æ—“°¢t6Æ—“Òôdeô$äEõ“²t6Æ—“Òôdeô$äEõ“²òòæFVVFR6Æ—'6RFRÆ&æF¢öfdG&t¶æö"‚“°¢t6Æ—“Ò3²t6Æ—“Ò3°¢6WD'Vb†öÆB“°¢&W6VçB…ôdeô$äEõ“Âôdeô$äEõ““°§Ð §7FF–2fö–BöfdVçFW"‚—°¢6–bõtU$ôdeôôà¢&WGW&ã°¢6VÇ6P¢òòÖ—6Ö&VBFR6VwW&–FBVRÇ7U7F'EfW&–g“¢W7FçFÆÆ4”TÕ$R6R6ö×öæP¢òòVâ÷'G&—B’çFÆÆ6ö×ÆWFÂfVævFRFöæFRfVæv„ÖöFò2òVæ ¢òòÆæG66RFV¦âtÆæC×G'VR’VÂF7F–Â&VÖVFò’à¢tÆæBÒfÇ6S°¢t6Æ—ƒÒ²t6Æ—ƒÒ45%õrÒ²t6Æ—“Ò²t6Æ—“Ò45%ô‚Ò°¢Vç7W&T&ÇW$&r‚“°¢öfd¶æö"ÒöfeF&vWBÒ²öfdG&rÒfÇ6S²öfdw&"Ò²öfdÆ7D¶æö"ÒÓ° ¢6WD'Vb†&'Vb“°¢öfdG&u7FF–2‚“°¢òò66†RFRÆ&æFFVÂ6Æ–FW"DÂ5TÂVVFFRf'&–6‡—7Ff6–’â6P¢òò&W6W'fTä6öÆfW¢VâFöFÆ6W6–öâÂgVW&FVÂÆö÷FR&VæFW"ÂVâ5$Ð¢òòÒÒ–wVÂVR4'Vbâã3R´"ƒCƒ‚"‚"’ÂæòVâg&ÖV'VffW"VçFW&òà¢–b‚öfd&æB’öfd&æBÒ‡V–çCe÷B¢–†Vö65öÖÆÆö2‚‡6—¦U÷B•ôdeô$äEô‚¢45%õr¢"ÂÔÄÄô5ô4õ5•$ÒÂÔÄÄô5ô4ó„$•B“°¢–b‡öfd&æB’ÖVÖ7’‡öfd&æBÂ&'Vb²‡6—¦U÷B•ôdeô$äEõ“¢45%õrÂ‡6—¦U÷B•ôdeô$äEô‚¢45%õr¢"“°¢öfdG&t¶æö"‚“²òòöÖòVâ&W÷6òÂFVçG&òFVÂÖ—6Öòg&ÖP¢6WD'Vb†f"“°¢&W6VçBƒÂ45%ô‚Ò“²òòVâVæ–6òföÆ6FòFöÖ–6ó¢6W&ò'FVð¢öfdÆ7D¶æö"Ò°¢u7FFRÒ5EõõtU$ôdeô4ôäd•$Ó°¢6VæF–`§Ð ¢òò'&7G&RFVÂ6Æ–FW"âÖ—6ÖòVæf÷VRVRVÂG&rFRÆ6÷'F–æFR§W7FW0¢òò&–F÷2‡4†æFÆR“¢Ö–VçG&2VÂFVFòW7F&¦òVÂöÖò4”uTRÂFVFò£Â¢òòÂ6öÇF"6–â6ö×ÆWF"VÂ&V6÷'&–FògVVÇfR–çFW'öÆFòà§7FF–2fö–BöfeF–6²‚—°¢6–bõtU$ôdeôôà¢–çB·‚ÒôdeõE$4µõ‚²ôdeô´äô%õB²öfd¶æö#°¢–b…Bç&W76VBbböfdG&r—°¢òòv'&RvVæW&÷6ó¢FöFòVÂÇFòFRÆ—7FÂ’Vâ‚FW6FRVÂöÖò†6–Æ¢òò—§V–W&FVâö6òÂ&VRV×W¦"VÂvW7FòæòW†–¦&V6—6–öâà¢–b…Bç’ãÒôdeõE$4µõ’bbBç’ÃÒôdeõE$4µõ’²ôdeõE$4µô‚b`¢Bç‚ãÒ·‚Ò#BbbBç‚ÃÒ·‚²ôdeô´äô%ôB²#B—°¢öfdG&rÒG'VS²öfdw&"ÒBç‚Ò·ƒ°¢Ð¢Ð¢–b‡öfdG&r—°¢–b…BæF÷vâ—°¢–çBbÒBç‚Òöfdw&"Ò…ôdeõE$4µõ‚²ôdeô´äô%õB“°¢–b‡bÂ’bÒ²–b‡bâôdeõ%Tâ’bÒôdeõ%Tã°¢öfeF&vWBÒöfd¶æö"Òc²òò£6öâVÂFVFð¢ÒVÇ6R°¢öfdG&rÒfÇ6S°¢–b…ôdeõ%Tââbböfd¶æö"¢òôdeõ%TâãÒôdeôDôäUõ5B—°¢öfd¶æö"ÒöfeF&vWBÒôdeõ%Tã²òò6ö×ÆWFFð¢öfe&VæFW$&æB‚“°¢òò”â÷6–öæÂåDU2FR&ö6VFW"â6’fÆÆò6R6æ6VÆÂ6RgVVÇfRV¢òò‡fW"Ç7TW†—B’’æò6RvæFà¢–b‡öfe–å&WV—&VB‚’—²Ç7U7F'EfW&–g”f÷"„Å5UôeDU%õõtU$ôdbÂÓ“²&WGW&ã²Ð¢öfd&Vv–äæ–Ò‚“²&WGW&ã°¢Ð¢öfeF&vWBÒ²òòæòÆÆVvó¢gVVÇfR6öÆð¢Ð¢ÒVÇ6R–b…BçFbbBç‚ãÒôdeô4åõ‚bbBç‚ÃÒôdeô4åõ‚²ôdeô4åõrb`¢Bç’ãÒôdeô4åõ’bbBç’ÃÒôdeô4åõ’²ôdeô4åô‚—°¢òò6æ6VÆ#¢6–âæ–æwVâVfV7Fò6V7VæF&–òâ6RgVVÇfRÂW67&—F÷&–òÂVRW2FP¢òòFöæFR6RÆÆVv6–V×&R†VÂ–6öæòf—fRVâVÂæVÂ&–FòÂVR6öÆò6R'&P¢òòFW6FR5Eô„ôÔR’à¢u7FFRÒ5Eô„ôÔS²&VæFW$†öÖR‚“²6†÷t†öÖR‚“²&WGW&ã°¢Ð¢òò–çFW'öÆ6–öâFRgVVÇF†Ö—6ÖòW7F–ÆòVRVÂ&W7FòFR&W6÷'FW2FVÂ6—7FVÖ ¢òò6W&6Ö–VçFò&÷÷&6–öæÂ²Vævæ6†Rf–æÂ&VRFW&Ö–æRFRfW&FB’à¢–b‚öfdG&rbböfd¶æö"ÒöfeF&vWB—°¢–çBBÒöfeF&vWBÒöfd¶æö#°¢öfd¶æö"³Ò†Bâò†B²2’òB¢†BÒ2’òB“°¢–b†'2‡öfeF&vWBÒöfd¶æö"’Â2’öfd¶æö"ÒöfeF&vWC°¢Ð¢öfe&VæFW$&æB‚“°¢6VæF–`§Ð ¢òòÒÒÒÒFVW6ÆVWÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÐ¢òò&ÖÆgVVçFRFRFW7W'F"’VçG&VâFVW6ÆVWâäò&WF÷&æçVæ6à¢òð¢òòeTTåDRDRDU5U%D"ÒÒfW"VÂ&Æ÷VRôdeõt´Uôu”òFR'&–&FVÂ&6†—fó ¢òò+r'WF'VVæ…Tä•%FVÂ…C#CbÂu”ò%D2âã#“¢W‡CâVâVÂU53"Õ30¢òòW‡CW2Æf–6÷÷'FF&FW7W'F"÷"–âÂ’VÂTä•%W2W&fV7Fð¢òò&VÆÆ÷'VRW2Vâä•dTÂ6÷7FVæ–FòÖ–VçG&2†’VâFVFòVæ6–Ö†VÀ¢òò”åBFVÂuC“FVÂBW&VâVÇ6òÂ’÷"W6òÆÆ’W7F'WFæò6W'f–’à¢òò+r'WFÇFW&æF—f‡–âgVW&FR&ævò%D2“¢FV×÷&—¦F÷"âVÂ6†—FW7–W'F¢òò6Fôdeõt´UõôÄÅôÕ2ÂÖ—&VÂF7F–Â’6RgVVÇfRF÷&Ö—"6’æò†¢òòFVFòâgVæ6–öæVâ7VÇV–W"Æ66–â6&W"æ–æwVâ–âÂW&ò6öç7VÖP¢òò&7FçFRÖ2VRVÂFVW6ÆVWFRfW&FB÷'VRVÂ6†—'&æ66F¢òò&FòâW2VâÖöFòDTu$DDòÂæòVÂö&¦WF—fòà§7FF–2fö–BöfdVçFW$FVW6ÆVW‚—°¢òò$4´Ä”t…B4ôätTÄDòTâ$¤òâVâVÂBÆòVR†&–VR6öævVÆ"W&VÀ¢òò&W6WBFVÂuC“‡&VRVÂF7F–Â6–wV–W&W66æVæFòGW&çFRVÂ7VVæò’à¢òòVÂ…C#CbæòF–VæRÆ–æVFR&W6WB×7RTä•%W26—fò’gVæ6–öæ6–à¢òòÆ–ÖVçF6–öâÆöv–6FVÂU53"ÒÂ6’VRÆòVR–çFW&W66öævVÆ"V’W0¢òò÷G&6÷6¢VÂ–âFVÂ&6¶Æ–v‡Bâ6–âVÂ†öÆBÂÂVçG&"VâFVW6ÆVWVÀ¢òòBgVVÇfR7RW7FFò÷"FVfV7Fò†VçG&F6öâVÆÂ×W’’VÂG&—fW"FVÀ¢òò&6¶Æ–v‡BFRÆwVæ÷2ÖöGVÆ÷2Æò–çFW'&WF6öÖò&Væ6VæFW""ÓâÆçFÆÆ¢òò6RVVF&––ÇVÖ–æFVâ&Ææ6ò6öâVÂ6—7FVÖvFòà¢òð¢òòF–fW&Væ6–FVÂBÂVÂU53"Õ324’FV6Æ&w–õöFVW÷6ÆVWö†öÆEöVâ‚’†æð¢òòFVf–æR4ô5ôu”õõ5Uõ%Eô„ôÄEõ4”ätÄUô”õô”åôE4Å’Â’VâW7FR6†—†6RfÇF¢òòÆÆÖ&ÆDTÔ2FRw–õö†öÆEöVâ‚’&VRVÂ†öÆB6ö'&Wf—fÂ7VVæòà¢–äÖöFR…”åôÄ4Eô$ÂÂõUEUB“°¢F–v—FÅw&—FR…”åôÄ4Eô$ÂÂÄõr“°¢w–õö†öÆEöVâ‚†w–õöçVÕ÷B•”åôÄ4Eô$Â“°¢w–õöFVW÷6ÆVWö†öÆEöVâ‚“° ¢6–b…ôdeõt´Uôu”òãÒ’bb…ôdeõt´Uôu”òÃÒ#¢òòVÂTä•%æV6W6—F7RVÆÂ×WFÖ&–VâGW&çFRVÂ7VVæòÂòfÆ÷F&–¢òòFW7W'F&–ÆÆ66öÆâ'F5öw–õ÷VÆÇWöVâæò†6RfÇF¢VâVÂ32VÀ¢òòVÆÂ×WFVÂB6RÖçF–VæR6’6R–FR†öÆB6ö'&RVÂà¢W7÷6ÆVWöVæ&ÆUöW‡C÷v¶WWö–òƒTÄÂÃÂôdeõt´Uôu”òÀ¢ôdeõt´UôÄUdTÂòU5ôU…Cõt´UUôå•ô„”t‚¢U5ôU…Cõt´UUôå•ôÄõr“°¢6VÇ6P¢W7÷6ÆVWöVæ&ÆU÷F–ÖW%÷v¶WW‚‡V–çCcE÷B•ôdeõt´UõôÄÅôÕ2¢TÄÂ“°¢6VæF–`¢6W&–Âç&–çFÆâ„b‚%µu%ÒFVW6ÆVW"’“°¢6W&–ÂæfÇW6‚‚“°¢W7öFVW÷6ÆVW÷7F'B‚“²òòæò&WF÷&æ§Ð¢òòÖ&6Vâåe2VRW7FRvFògVRÄ”Õ”ò†Æò–F–òVÂW7V&–ò’Â&VP¢òò6WGW‚’VVFF—7F–æwV—"Vâ'&çVRæ÷&ÖÂFRVâFW7W'F"FRFVW6ÆVW6–à¢òòFWVæFW"6öÆòFRW7÷&W6WE÷&V6öâ‚’à§7FF–2fö–Böfe6fT6ÆVäfÆr‚—°¢&Vg2æ&Vv–â‚&fÆW†÷2"ÂfÇ6R“°¢&Vg2çWD&ööÂ‚&6ÆVæöfb"ÂG'VR“°¢&Vg2çWD–çB‚&'&–v‡B"Ât'&–v‡B“²òòVÂ'&–ÆÆòFVÂW7V&–òÂ&&W7FW&&ÆòÂVæ6VæFW ¢&Vg2æVæB‚“°§Ð ¢òòÒÒÒÒæ–Ö6–öâFRvFòÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÐ§7FF–2fö–Böfd&Vv–äæ–Ò‚—°¢6–bõtU$ôdeôôà¢tÆæBÒfÇ6S°¢t6Æ—ƒÒ²t6Æ—ƒÒ45%õrÒ²t6Æ—“Ò²t6Æ—“Ò45%ô‚Ò°¢òò–ç7FçFæVFVÂg&ÖR7GVÂ&VÂgVæF–FòæVw&òâ6RW6Æö6´'Vb†Æ¢òò66†RFRÆçFÆÆFR&Æ÷VVò’VâfW¢FR&W6W'f"sS´"Ö3¢&VæFW$Æö6²‚¢òòÆ&VvVæW&VçFW&6FfW¢VR†6RfÇFÂ’FW6FRV’–æò6RgVVÇfR¢òòæ–æwVæçFÆÆÒÒVÂFW7F–æòW2VÂFVW6ÆVWà¢–b†Æö6´'Vb’ÖVÖ7’†Æö6´'VbÂf"Â‡6—¦U÷B•45%õr¢45%ô‚¢"“°¢öfe†6RÒ²öfe†6T×2ÒÖ–ÆÆ—2‚“°¢u7FFRÒ5EõõtU$ôdeôä”Ó°¢6VæF–`§Ð§7FF–2fö–Böfdæ–ÕF–6²‚—°¢6–bõtU$ôdeôôà¢V–çC3%÷BRÒÖ–ÆÆ—2‚’Òöfe†6T×3°¢7v—F6‚‡öfe†6R—°¢66R¢²òò’gVæF–FòæVw&ò†÷fW&Æ’6ö×VW7FòVâ&'VbÂåTä4f–ÆÅ67&VVâ¢–b†RâôdeôdDUôÕ2’RÒôdeôdDUôÕ3°¢V–çC…÷BÒ‡V–çC…÷B’†R¢#SRòôdeôdDUôÕ2“°¢6öç7BV–çCe÷B¢7&2ÒÆö6´'VbòÆö6´'Vb¢f#°¢f÷"†–çB¢Ò²¢Â45%ôƒ²¢²²—°¢6öç7BV–çCe÷B¢2Ò7&2²‡6—¦U÷B–¢¢45%õs°¢V–çCe÷B¢BÒ&'Vb²‡6—¦U÷B–¢¢45%õs°¢f÷"†–çB’Ò²’Â45%õs²’²²’E¶•ÒÒÖ—ƒScR‡5¶•ÒÂÂ“²òòÒæVw&òVâ$t#ScP¢Ð¢&W6VçBƒÂ45%ô‚Ò“°¢–b†RãÒôdeôdDUôÕ2—²öfe†6RÒ²öfe†6T×2ÒÖ–ÆÆ—2‚“²Ð¢'&V³°¢Ð¢66R¢òò"’VÂFW‡Fò$fÆW‚õ2"&V6R6ö'&RVÂæVw&ð¢66R#¢òòâââ6RVVFV–WFð¢66R3¢²òò2’âââ’6RF—7VVÇfR†Ç†FV7&V6–VçFR¢V–çC…÷BÒ#SS°¢–b‡öfe†6RÓÒ—²–b†RâôdeõD”åôÕ2’RÒôdeõD”åôÕ3²Ò‡V–çC…÷B’†R¢#SRòôdeõD”åôÕ2“²Ð¢VÇ6R–b‡öfe†6RÓÒ2—²–b†RâôdeõDõUEôÕ2’RÒôdeõDõUEôÕ3²Ò‡V–çC…÷B’ƒ#SRÒR¢#SRòôdeõDõUEôÕ2“²Ð¢òò6öÆò6R&V6ö×öæRÆ&æFFVÂFW‡Fó¢VÂ&W7FòFRÆçFÆÆ–W0¢òòæVw&ò6öÆ–FòVâf"FW6FRÆf6R’æò6Ö&–à¢f÷"†–çB¢ÒôdeõE…Eô%“²¢ÃÒôdeõE…Eô%“²¢²²¢ÖV×6WB†&'Vb²‡6—¦U÷B–¢¢45%õrÂÂ45%õr¢"“°¢V–çCe÷B¢öÆBÒt'Vc²6WD'Vb†&'Vb“°¢–çB3Òt6Æ—“Â3Òt6Æ—“°¢t6Æ—“ÒôdeõE…Eô%“²t6Æ—“ÒôdeõE…Eô%“°¢–b†’G&uFW‡D4…45%õrò"ÂôdeõE…Eõ’Â$fÆW‚õ2"ÂôdeõE…Eõ5¢Â&v#ScRƒ#CÃ#C"Ã#S’Â“°¢t6Æ—“Ò3²t6Æ—“Ò3°¢6WD'Vb†öÆB“°¢&W6VçB…ôdeõE…Eô%“ÂôdeõE…Eô%““°¢V–çC3%÷BGW"Òöfe†6RÓÒòôdeõD”åôÕ2¢öfe†6RÓÒ"òôdeô„ôÄEôÕ2¢ôdeõDõUEôÕ3°¢–b†Ö–ÆÆ—2‚’Òöfe†6T×2ãÒGW"—²öfe†6R²³²öfe†6T×2ÒÖ–ÆÆ—2‚“²Ð¢'&V³°¢Ð¢66RC¢òòB’gVæF–FòFVÂ&6¶Æ–v‡B†Ö—6ÖòÖV6æ—6ÖòVRÆ7W7Vç6–öâ¢–b†u7W7fFRÂbbu7W7öâ—²u7W7öâÒG'VS²u7W7F&²ÒfÇ6S²u7W7'&–v‡BÒt'&–v‡C²7W7fFUFòƒ“²Ð¢–b†u7W7F&²—²öfe†6RÒS²öfe†6T×2ÒÖ–ÆÆ—2‚“²Ð¢'&V³°¢66RS¢òòR’D52FR&¦ò6öç7VÖòÓâåe2ÓâFVW6ÆVW ¢æVÅ6ÆVW–â‚“°¢öfe6fT6ÆVäfÆr‚“°¢öfdVçFW$FVW6ÆVW‚“²òòæò&WF÷&æ¢'&V³°¢Ð¢6VæF–`§Ð ¢òòÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÐ¢òòÄ”ÔTåD4”ôâDTdTå4•dDTÂD4²tD4„Dôp¢òòÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÐ¢òòW7÷F6µ÷vGE÷&W6WB‚’6öÆòW2fÆ–Fò6’ÆF&VVRÆÆÖW7F¢òò5U45$•DÂEtEBâ6’æòÆòW7FFWgVVÇfRU5ôU%%ôäõEôdõTäB’VÀ¢òò6ö×öæVçFRF6µ÷vGB–×&–ÖRVâU%$õ"÷"4DÆÆÖF ¢òð¢òòRƒs33b’F6µ÷vGC¢W7÷F6µ÷vGE÷&W6WBƒsR“¢F6²æ÷Bf÷Væ@¢òð¢òòFW6FRÆö÷‚’ÂVRFVæ2#gVVÇF2÷"6VwVæFòÂW6ò6GW&VÀ¢òòVW'Fò6W&–R’FV¦VÂÆör–ç6W'f–&ÆR&F–væ÷7F–6"æFÖ2à¢òð¢òò÷"VR6RVVFRW&FW"Æ7W67&—6–öã¢ÆWfçF"Æ–Æv”f’†VâVÀ¢òòBÂVÂVæÆ6RW7Ö†÷7FVBõ4D”ò6öâVÂ3b’&V–æ–6–Æ—¦VÂEtEB’6öà¢òòVÂ6RfÆÆ—7FFR7W67&—F÷&W2âçFW2æò6Ræ÷F&÷'VRÆ¢òò&F–òæò6RVæ6VæF–çVæ66öÆ²Vâ7VçFòVÂ'&çVRV×W¦ò¢òòÆWfçF&ÆÂVÂ'V6ÆR&V6–òVâ6F&ö÷Bà¢òð¢òòF—6\;ó¢VâVÂ66òæ÷&ÖÂW7FòW2Tä6öÆÆÆÖF÷"gVVÇFÂ–wVÀ¢òòFR&&FòVRçFW2âVâ7VçFòfÆÆTäfW¢6RFV¦FRÆÆÖ"’6P¢òò6&V–çFVçF"Æ7W67&—6–öâ6FR2ÒÒ6’VÂÆörfRVæÆ–æV¢òòVâÆG&ç6–6–öâ’6öÖò×V6†òVæ6FR2ÂçVæ6Vâ'V6ÆRâ’6’VÀ¢òòEtEBgVVÇfRW7F"F—7öæ–&ÆRÂÆf–v–Ææ6–6R&V7WW&6öÆ¢æð¢òò6R&æFöæçVæ6FRf÷&ÖW&ÖæVçFRà¢òòÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÐ§7FF–2fö–BfÆW„fVVEvGB‚—°¢7FF–2&ööÂ7V'67&–&VBÒG'VS²òò&GV–æò7W67&–&RVÂÆö÷F6²Â'&æ6 ¢7FF–2V–çC3%÷BæW‡EG'’Ò°¢–b‡7V'67&–&VB—°¢–b†W7÷F6µ÷vGE÷&W6WB‚’ÓÒU5ôô²’&WGW&ã²òò66òæ÷&ÖÀ¢7V'67&–&VBÒfÇ6S²òò6RW&F–òÆ7W67&—6–öà¢æW‡EG'’ÒÖ–ÆÆ—2‚’²S°¢6W&–Âç&–çFÆâ„b‚%µtEEÒÆö÷F6²–æòW7F7W67&—FòÂF6²vF6†Fös²&V–çFVçFæFò6FR2"’“°¢&WGW&ã°¢Ð¢V–çC3%÷Bæ÷rÒÖ–ÆÆ—2‚“°¢–b†æ÷rÂæW‡EG'’’&WGW&ã²òò6–â–ç6—7F—#¢W6òW2ÆòVR–çVæF&¢æW‡EG'’Òæ÷r²S°¢–b†W7÷F6µ÷vGEöFB„åTÄÂ’ÓÒU5ôô²—°¢7V'67&–&VBÒG'VS°¢6W&–Âç&–çFÆâ„b‚%µtEEÒÆö÷F6²&W7W67&—FòÂF6²vF6†För"’“°¢W7÷F6µ÷vGE÷&W6WB‚“°¢Ð§Ð ¢òòÒÒÒÒf–ÇG&òFR'&çVS¢26VwVæF÷2FR&W6–öâ6÷7FVæ–FÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÐ¢òòõ"TR4’†W2VÂVçFòÖ2FVÆ–6FòFRFöFòVÂvFò“ ¢òòVâFVW6ÆVWVÂÆö÷‚’æòW†—7FRÂ6’VRäò†’f÷&ÖFR7&öæöÖWG&"Æ÷20¢òò6VwVæF÷26öâÆÆöv–6†&—GVÂFRÖ–ÆÆ—2‚’ÒÒVÂ6†—W7FvFòâW‡C6öÆð¢òòVVFRFV6—"&ÆwV–Vâ†Fö6Fò"ÂçVæ6&ÆÆWf22Fö6æFò"à¢òò6öÇV6–öâ†ÆÖ—6ÖFR6–V×&RÂ’ÆVæ–66÷÷'FFFRfW&FB“ ¢òòFW7W'F"Â$”ÔU"6öçF7FòÓâfW&–f–6"ÆGW&6–öâ”DU5”U%DòÂÆW–VæFð¢òòVÂ…C#Cb÷"5’ÂVâÆ÷2&–ÖW&÷2–ç7FçFW2FR6WGW‚’Â6öâVÂæVÂ’VÀ¢òò&6¶Æ–v‡BFöFf–vF÷2Óâ6’æò6R6÷7F–VæRÂföÇfW"F÷&Ö—"6–âÆÆVv ¢òò'&æ6"à¢òòVÂW7V&–òçVæ6fRVâFW7FVÆÆó¢æFFRW7Fòö7W'&RFW7VW2FRfÆW…æVÄ–æ—Bà§7FF–2fö–Böfev¶TvFR‚—°¢6–bõtU$ôdeôôà¢–b†W7÷&W6WE÷&V6öâ‚’ÒU5õ%5EôDTU4ÄTU’&WGW&ã²òòæòfVæ–Ö÷2FRFVW6ÆVW ¢òòÆ–&W&"VÂ†öÆBFVÂ&6¶Æ–v‡BâÆ’–FRFV¦"VÂ–â–6öæf–wW&FòVà¢òòVÂÖ—6Öòæ—fVÂåDU2FR6öÇF"VÂ†öÆBÂòVÂBF&–VâvÆ—F6‚Â6"¢òò7RW7FFò÷"FVfV7Fò‡’W6òVæ6VæFW&–ÆçFÆÆVâ–ç7FçFRÂVRW0¢òò§W7FòÆòVRW7FRf–ÇG&òW†—7FR&Wf—F"’âVâVÂ32†’VR6öÇF ¢òòFVÖ2VÂ†öÆBvÆö&ÂFRFVW6ÆVWà¢–äÖöFR…”åôÄ4Eô$ÂÂõUEUB“°¢F–v—FÅw&—FR…”åôÄ4Eô$ÂÂÄõr“°¢w–õö†öÆEöF—2‚†w–õöçVÕ÷B•”åôÄ4Eô$Â“°¢w–õöFVW÷6ÆVWö†öÆEöF—2‚“°¢òò6öÆòVÂ'W25’²VÂF7F–ÂâVÂæVÂäò6RVæ6–VæFRV’&÷÷6—Fòà¢–b‚fÇ…7”'W4–æ—B‚’’&WGW&ã°¢fÆW…F÷V6„–æ—B‚“²òò…C#Cb†VÂæVÂ6–wVRvFò¢–b‚wDö²’&WGW&ã²òò6–âF7F–Âæò†’f÷&ÖFR6öæf—&Ö#¢6R'&æ6¢V–çC3%÷BCÒÖ–ÆÆ—2‚’Â†VÆDg&öÒÒ°¢v†–ÆR†Ö–ÆÆ—2‚’ÒCÂôdeõt´UôtDUôÕ2—°¢fÆW„fVVEvGB‚“²òòVÂEtEB6RÆ–ÖVçF–wVÂVRVâÆö÷‚’†FVfVç6—fò¢V–çCe÷Bw‚ÒÂw’Ò°¢wEöÆÂ†w‚Âw’“°¢–çBâÒ†Ö–ÆÆ—2‚’ÒwDf–ævW'4×2â#’ò¢†–çB–wDf–ævW'3°¢–b†âãÒ—°¢–b‚†VÆDg&öÒ’†VÆDg&öÒÒÖ–ÆÆ—2‚“°¢–b†Ö–ÆÆ—2‚’Ò†VÆDg&öÒãÒôdeõt´Uô„ôÄEôÕ2’&WGW&ã²òò226÷7FVæ–F÷2Óâ'&çVR6ö×ÆWFð¢ÒVÇ6R†VÆDg&öÒÒ°¢FVÆ’ƒ“°¢Ð¢öfdVçFW$FVW6ÆVW‚“²òòæò6R6÷7GWfó¢FRgVVÇFF÷&Ö— ¢6VæF–`§Ð ¢òò2222222222222222222222222222222222222222222222222222222222220¢òò226WGWòÆö÷ ¢òò2222222222222222222222222222222222222222222222222222222222220¢òòÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÐ¢òò&F–ò…v”f’äD•dòFVÂU53"Õ32¢òð¢òòU5DRU2TÂ%DDòTRÔ2tä4ôâTÂõ%BâVâVÂU53"ÕBæð¢òò†’&F–ó¢VÂv”f’FVæ–VR—"÷"Vâ6ò×&ö6W6F÷"U53"Ô3b¢òòG&fW2FRW7Ö†÷7FVBõ4D”òÂ6öâf—&×v&R'6ÆfR"VR†&–VP¢òòfÆ6†V"'FRÂ’6’W6RVæÆ6RfÆÆ&ÆÆ6VçG&&VâVà¢òò'V6ÆRFRä”2Vâ4D'&çVRâ÷"W6òÆ&F–ò6RFV¦òVâÖöFð¢òò&¦òFVÖæF¢W&ÆVæ–6f÷&ÖFRVRVâfÆÆòFVÂ3bæò6P¢òòÆÆWf&÷"FVÆçFRVÂ'&çVRVçFW&òà¢òð¢òòVÂU53"Õ32ÆÆWfv”f’ƒ"ã"öröâ’&ÇVWFö÷F‚ÄRRDTåE$òFVÀ¢òò&÷–ò6†—âv”f’æ&Vv–â‚’òv”f’ç66äæWGv÷&·2‚’†&ÆâF—&V7FÐ¢òòÖVçFR6öâVÂG&—fW"Æö6Ã¢6–â4D”òÂ6–âf—&×v&RW‡FW&æòÂ6–à¢òòG&ç7÷'FRVR6RVVF6W"â–æòW†—7FRæ–æwVæòFRVVÆÆ÷0¢òòÖöF÷2FRfÆÆòà¢òð¢òòTâ4’4R4ôå4U%dTÂ%$åTR$¤òDTÔäDÂ’&÷÷6—Fó ¢òò+rW2VÂ6ö×÷'FÖ–VçFòVRVÂW7V&–ò–6öæö6R†Æ&F–ò6P¢òòVæ6–VæFRFW6FR§W7FW2â&VBR–çFW&æWBâv’Ôf’’à¢òò+rFV¦Æ÷2ãS´"FR$Ò–çFW&æFRÆ–Æv”f’Æ–'&W2Ö–VçG&0¢òòæò6RW6ÂVRVâW7FÆ66R&÷fV6†â&Æ÷2'VffW'0¢òòDÔFVÂæVÂ’FRÆ6Ö&à¢òò+ræòv7F&FW&–W66æVæFò7VæFòæF–RÆò†VF–Fòà¢òòVæ6VæFW&ÆÂ'&æ6"6W&–†÷&W&fV7FÖVçFR6VwW&ó¢&7F&–¢òò6öâÆÆÖ"V’v”f’æ&Vv–â‡76–BÂ72’à¢òð¢òòdÄU„õ5ôTä$ÄUõt”d’’tæWDöæÆ–æRW7FâFV6Æ&F÷2'&–&FVÂFöFð¢òòFVÂ&6†—fò†§VçFòÆ÷2”äU2’÷'VR§W7FW2Æ÷2æV6W6—FçFW0¢òòFRÆÆVv"V’à¢òð¢òòäõD4ô%$RÄõ2”äU3¢Æ&F–òFVÂ32W2–çFW&æ’äò6öç7VÖP¢òòæ–æwVâu”òÂ6’VRæòVçG&VâVÂ&W'Fò6öâÆ6Ö&æ’6öà¢òòÆçFÆÆâÆòVæ–6òFVæW"Vâ7VVçFW2VRVÂD3"æò6RVVFP¢òòW6"6öâVÂv”f’7F—fòÒÒW7FR&÷–V7FòæòW6D3"à¢òòÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÐ¢6FVf–æRdÄU„õ5õt”d•õD”ÔTõUEôÕ2S  ¢òòFVf–æ–FòÖ2&¦ò†æV6W6—Fv–f•6fVE54”B’VÂ&W7FòFVÂ&Æ÷VRv’Ôf’’à§7FF–2&ööÂv–f”7&VG4ÆöB‚“° ¢òò7VçFò6RW7W&FW6FRVÂ'&çVRçFW2FVÂ$”ÔU"–çFVçFòFP¢òò&V6öæW†–öâWFöÖF–6âæòW2VâF÷&æó¢FF–V×òVRVÂæVÂÂVÀ¢òòF7F–Â’VÂW67&—F÷&–òW7FVâVâÖ&6†ÂFRÖöFòVRVâfÆÆòÀ¢òòÆWfçF"Æ&F–ò6RfV6öâVÂ6—7FVÖ–f—fò’æòÖ—FBFVÀ¢òò'&çVRà¢6FVf–æRdÄU„õ5õt”d•ôUDô4ôäåôDTÄ•ôÕ2c  ¢òòÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÐ¢òò6WGW‚’äòDô4Ä$D”òâä’Tä4ôÄdU¢à¢òòÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÐ¢òòW7FÆ6ÆÆWf&F–òæF—fÂ6’VRV’æò†’VÂVæÆ6R4D”ð¢òòg&v–ÂFVÂC²Vâ6’6RÖçF–VæRTÂÔ•4Ôò7&—FW&–òVâÆ2G&W0¢òòf&–çFW2FVÂ6—7FVÖâÆWfçF"Æ–Æv”f’FVçG&òFR6WGW‚’FV¦ð¢òòFR6W"6WF&ÆR7VæFò6R6ö×&ö&òVRVVFR&V–æ–6–Æ—¦"VÂF6°¢òòvF6†För’GVÖ&"Æ7W67&—6–öâFVÂÆö÷F6²ÂÆòVR–çVæFVÂÆöp¢òò6öâ'F6µ÷vGC¢W7÷F6µ÷vGE÷&W6WBƒsR“¢F6²æ÷Bf÷VæB"FW6FRVÀ¢òò&–ÖW"'&çVRâVæ6öÆ&VvÆ&Æ2G&W2Æ62W2FVÖ2Ö0¢òòf6–ÂFR&¦öæ"VRVæW†6W6–öâ÷"f&–çFRà¢òð¢òòV’6öÆò6RÆVVâÆ27&VFVæ6–ÆW2FRåe2ÒÒW6òW2fÆ6‚Âæò&F–òÀ¢òò’W26VwW&òâVÂ–çFVçFò&VÂÆòF—7&v–f”WFõ&V6öææV7EF–6²‚¢òòFW6FRÆö÷‚’Â–6öâVÂ6—7FVÖ'&æ6Fòà¢òòÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÐ§7FF–2fö–B&ö÷D–æ—E&F–õ6fR‚—°¢&ööÂ6fVBÒv–f”7&VG4ÆöB‚“²òò6öÆòåe3¢æòÆWfçFÆ&F–ð¢–b‡6fVB’6W&–Âç&–çFÆâ„b‚%´äUEÒ†’&VBwV&FFÓâ6R–çFVçF&&V6öæV7F"G&2VÂ'&çVR†çVæ6FVçG&òFR6WGW’"’“°¢VÇ6R6W&–Âç&–çFÆâ„b‚%´äUEÒv”f’æF—fòFVÂ32VâÖöFò&¦òFVÖæFÓâ§W7FW2â&VBR–çFW&æWBâv’Ôf’"’“°§Ð ¢òòÒÒÒÒ$ÄR÷6–öæÂ††÷&äD•dò’ÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÐ¢òòVÂU53"Õ324’F–VæR6öçG&öÆF÷"&ÇVWFö÷F‚ÄRR&÷–òÂ6’VRW7Fò–¢òòæòFWVæFRFRæ–æwVâ6ò×&ö6W6F÷"æ’FRf—&×v&RW‡FW&æó¢&7F6öà¢òò–ç7FÆ"æ–Ô$ÄRÔ&GV–æò’öæW"VÂ6–bFR&¦òà¢òòäõD¢F×ö6òV’6RÆÆÖW7ö'Eö6öçG&öÆÆW%öÖVÕ÷&VÆV6R‚’âW6¢òòÆ–&W&Æ$ÒFVÂ&ÇVWFö÷F‚4Ä4”4òÂ’VÂ32æòÆòF–VæR‡6öÆò$ÄR’Â6¢òòVRæò†’æFVRÆ–&W&"âVÂ–çFW''WF÷"6–wVRVâ÷"FVfV7Fò&¢òòæòv7F"Æ÷2ã3´"FR$ÒFRÆ–Æ$ÄR6’æò6RW6à¢6–b ¢6–æ6ÇVFRÄæ–Ô$ÄTFWf–6Ræƒà§7FF–2fö–B&ö÷D–æ—D&ÆU6fR‚—°¢æ–Ô$ÄTFWf–6S£¦–æ—B‚$fÆW„õ2"“°¢òòâââGRÆöv–6FRGfW'F—6–æròtEBV§Ð¢6VæF–` ¢òò2222222222222222222222222222222222222222222222222222222222220¢òò22¥U5DU2Óâ$TBR”åDU$äUBÓât’Ôd¢òò22W66æVò’6öæW†–öâ6÷'&VâVâ7R&÷–F&VFVÂ6÷&R¢òò22†–wVÂG&öâVR'&–&“¢Æö÷‚’çVæ66R&Æ÷VVÂ’Và¢òò22fÆÆòFVÂ3b6RVVF6öçFVæ–FòVâW7FçFÆÆà¢òò2222222222222222222222222222222222222222222222222222222222220¢6FVf–æRt”d•ôÔ…ôäUE2`§7G'V7Bv–f”æWB²6†"76–E³35Ó²–çC…÷B'76“²&ööÂ6V7W&S²Ó°§7FF–2v–f”æWBv–f”æWG5µt”d•ôÔ…ôäUE5Ó°§7FF–2föÆF–ÆR–çBv–f”æWD6÷VçBÒ°§7FF–2÷'DÕU…õE•Rv–f”×W‚Ò÷'DÕU…ô”ä•D”Ä•¤U%õTäÄô4´TC° ¦VçVÒ²uT•ôÄ•5BÒÂuT•õ44ää”ärÂuT•õ52ÂuT•ô4ôääT5D”ärÂuT•ôô²ÂuT•ôd”ÂÓ°§7FF–2föÆF–ÆR–çBv–f•T•7FFRÒuT•ôÄ•5C°§7FF–2–çBv–f•6VÂÒÓ°§7FF–26†"v–f•75³cEÒÒ"#°§7FF–2V–çC3%÷Bv–f”¶$æ–ÒÒ°§7FF–26†"v–f”6öæå54”E³35ÒÒ"#°§7FF–26†"v–f”6öæå75³cEÒÒ"#°§7FF–26†"v–f”6öæä•³#EÒÒ"#° ¢òò2222222222222222222222222222222222222222222222222222222222220¢òò225$TDTä4”ÄU2uT$DD2„åe2’²$T4ôäU„”ôâUDôÔD”4¢òò22ÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÐ¢òò22çFW2ÂÆ&VBVÆVv–FÖæò6öÆòf—f–Vâ$Ó¢6F¢òò22vFòö&Æ–v&&WWF—"W66æVò²6VÆV66–öâ²6ÆfRà¢òò22†÷&ÂÆ$”ÔU$6öæW†–öâ6÷'&V7FwV&F54”B’6ÆfRVà¢òò22åe2†æÖW76R&÷–ò&fÆW†÷5÷v–f’"Â6W&FòFR&fÆW†÷2 ¢òò22&VRVâ&÷'&FòFR§W7FW2æò'&7G&RÆ&VB’À¢òò22&WfW2’Â’VÂ'&çVRÆ2&WWF–Æ—¦Vâ6VwVæFòÆæòà¢òò2222222222222222222222222222222222222222222222222222222222220¢6FVf–æRt”d•ôåe5ôå2&fÆW†÷5÷v–f’ ¢6FVf–æRt”d•ôåe5õ54”B'76–B ¢6FVf–æRt”d•ôåe5õ52'72  §7FF–26†"v–f•6fVE54”E³35ÒÒ"#°§7FF–26†"v–f•6fVE75³cEÒÒ"#°§7FF–2föÆF–ÆR&ööÂuv–f”WFô'W7’ÒfÇ6S²òò–çFVçFòWFöÖF–6òVâ7W'6ð§7FF–2föÆF–ÆR&ööÂuv–f”WFôFöæRÒfÇ6S²òò–6R–çFVçFò†6öâW†—Fòòæò §7FF–2&ööÂv–f”7&VG4W†—7B‚—²&WGW&âv–f•6fVE54”E³ÒÒ²Ð ¢òò6&vÆ27&VFVæ6–ÆW2FRåe2$Òâ6RÆÆÖVæfW¢VâVÂ'&çVRà§7FF–2&ööÂv–f”7&VG4ÆöB‚—°¢&VfW&Væ6W2°¢–b‚æ&Vv–â…t”d•ôåe5ôå2ÂG'VR’—²v–f•6fVE54”E³ÒÒ²v–f•6fVE75³ÒÒ²&WGW&âfÇ6S²Ð¢7G&–ær2ÒævWE7G&–ær…t”d•ôåe5õ54”BÂ""“°¢7G&–ærrÒævWE7G&–ær…t”d•ôåe5õ52Â""“°¢æVæB‚“°¢2çFô6†$'&’‡v–f•6fVE54”BÂ6—¦Vöb‡v–f•6fVE54”B’“°¢rçFô6†$'&’‡v–f•6fVE72Â6—¦Vöb‡v–f•6fVE72’“°¢&WGW&âv–f”7&VG4W†—7B‚“°§Ð ¢òòwV&F‡6öÆò6’Ævò6Ö&–ó¢W67&–&—"åe26–âæV6W6–FBFW6v7FÆfÆ6‚’à§7FF–2fö–Bv–f”7&VG56fR†6öç7B6†"¢76–BÂ6öç7B6†"¢72—°¢–b‚76–BÇÂ76–E³Ò’&WGW&ã°¢–b‚7G&6×‡v–f•6fVE54”BÂ76–B’bb7G&6×‡v–f•6fVE72Â72ò72¢""’’&WGW&ã°¢&VfW&Væ6W2°¢–b‚æ&Vv–â…t”d•ôåe5ôå2ÂfÇ6R’’&WGW&ã°¢çWE7G&–ær…t”d•ôåe5õ54”BÂ76–B“°¢çWE7G&–ær…t”d•ôåe5õ52Â72ò72¢""“°¢æVæB‚“°¢7G&æ7’‡v–f•6fVE54”BÂ76–BÂ6—¦Vöb‡v–f•6fVE54”B’Ò“²v–f•6fVE54”E·6—¦Vöb‡v–f•6fVE54”B’ÒÒÒ°¢7G&æ7’‡v–f•6fVE72Â72ò72¢""Â6—¦Vöb‡v–f•6fVE72’Ò“²v–f•6fVE75·6—¦Vöb‡v–f•6fVE72’ÒÒÒ°¢6W&–Âç&–çFb‚%µv”f•Ò&VBwV&FF¢W5Æâ"Âv–f•6fVE54”B“°§Ð ¢òòöÇf–FÆ&VB†6Ö&–òFR&÷WFW"’â&÷'&åe2’Æ6÷–Vâ$Òà§7FF–2fö–Bv–f”7&VG4f÷&vWB‚—°¢&VfW&Væ6W2°¢–b‡æ&Vv–â…t”d•ôåe5ôå2ÂfÇ6R’—²æ6ÆV"‚“²æVæB‚“²Ð¢v–f•6fVE54”E³ÒÒ²v–f•6fVE75³ÒÒ°¢uv–f”WFôFöæRÒG'VS²òòæò&V–çFVçF"VâW7F6W6–öà¢6W&–Âç&–çFÆâ„b‚%µv”f•Ò&VBwV&FF&÷'&F"’“°§Ð ¢òòuT$BDRÔôDòâFöFò–çFVçFòFRW66æVòò6öæW†–öâ6÷"V“¢VÀ¢òòG&—fW"FV&RW7F"Vâ5DçFW2FRFö6&ÆòâÆÆÖ"v”f’æÖöFR‚’7VæFð¢òò–6RW7FVâVÂÖöFòVF–FòW2–ææV6W6&–ò’VâVÂG&ç7÷'FR†÷7FV@¢òòFVÂ3b&V–æ–6–Æ–çFW&f¢6–âÖ÷F—fòÂ6’VR6öÆò6R6Ö&–6’†6P¢òòfÇFFRfW&FBà§7FF–2fö–Bv–f”Vç7W&U7FÖöFR‚—°¢–b…v”f’ævWDÖöFR‚’Òt”d•õ5D’v”f’æÖöFR…t”d•õ5D“°§Ð ¢6–bdÄU„õ5ôTä$ÄUõt”d§7FF–2fö–Bv–f•66åF6²‡fö–B¢—°¢v–f”Vç7W&U7FÖöFR‚“°¢–çBâÒv”f’ç66äæWGv÷&·2‚“²òò&Æ÷VVçFRÂW&òVâ7R$õ”F&V¢Æö÷‚’6–wVRf—fð¢òòåD’Ô5$4ƒ¢6öç7G'V—"ÆÆ—7FeTU$FRFöF6V66–öâ7&—F–6âÆfW'6–öà¢òòçFW&–÷"6÷–&FVçG&òFR÷'DTåDU%ô5$•D”4Â‚gv–f”×W‚’ÂW&òv”f’å54”B‚¢òòFWgVVÇfRVâ7G&–ær†ÖÆÆö2’’FVÖ2Fö6VÂG&—fW"v”f’â†6W"ÖÆÆö26öà¢òòÆ2–çFW''W6–öæW2FW6†&–Æ—FF2’Vâ7–æÆö6²FöÖFòVVFR†’&Æ÷VV ¢òòVÂÆö6²–çFW&æòFVÂ†VÓâFVFÆö6²Âò†"’ÖçFVæW"Æ2•%vF0¢òòFVÖ6–FòF–V×òÓâF—7&"VÂvF6†FörFR–çFW''W6–öæW2„”åEõtEB’¢òò&V–æ–6–"VÂU53"âv–f”æWG5µÒ4ôÄòÆòW67&–&RW7FF&V²ÆT’ÆVRVæ–6Ð¢òòÖVçFR–æF–6W2³Âv–f”æWD6÷VçB’â÷"W6ò&7F6öâV&Æ–6"v–f”æWD6÷VçBÀ¢òòd”äÂÂ&¦òVæ6V66–öâ7&—F–6Ö–æ–Ö†&'&W&FRÖVÖ÷&–“¢ÆT’çVæ6fP¢òòVæVçG&FÖVF–òW67&–&—"’æò†’æ–æwVæ6–væ6–öâ&¦òVÂ7–æÆö6²à¢–çB6çBÒ°¢–b†ââ—°¢f÷"†–çB’Ò²’Ââbb6çBÂt”d•ôÔ…ôäUE3²’²²—°¢7G&–ær72Òv”f’å54”B†’“°¢–b‡72æÆVæwF‚‚’ÓÒ’6öçF–çVS²òòö7VÇF&VFW26–âæöÖ'&P¢&ööÂGWÒfÇ6S°¢f÷"†–çB²Ò²²Â6çC²²²²’–b‚7G&6×‡v–f”æWG5¶µÒç76–BÂ72æ5÷7G"‚’’—²GWÒG'VS²'&V³²Ð¢–b†GW’6öçF–çVS²òòÖ—6Öò54”Bf—7FòVâf&–÷26æÆW0¢72çFô6†$'&’‡v–f”æWG5¶6çEÒç76–BÂ6—¦Vöb‡v–f”æWG5¶6çEÒç76–B’“°¢v–f”æWG5¶6çEÒç'76’Ò†–çC…÷B•v”f’å%54’†’“°¢v–f”æWG5¶6çEÒç6V7W&RÒ…v”f’æVæ7'—F–öåG—R†’’Òt”d•ôUD…ôõTâ“°¢6çB²³°¢Ð¢Ð¢÷'DTåDU%ô5$•D”4Â‚gv–f”×W‚“²v–f”æWD6÷VçBÒ6çC²÷'DU„•Eô5$•D”4Â‚gv–f”×W‚“²òòV&Æ–66–öâFöÖ–6FVÂ6öçFF÷ ¢v”f’ç66äFVÆWFR‚“°¢v–f•T•7FFRÒuT•ôÄ•5C²òòãÃÓÓâÆ—7Ff6–†ÖVç6¦R'6–â&VFW2"’ÂæòW2VâW'&÷"fFÀ¢eF6´FVÆWFR„åTÄÂ“°§Ð§7FF–2fö–Bv–f”6öæåF6²‡fö–B¢—°¢v–f”Vç7W&U7FÖöFR‚“°¢v”f’æ&Vv–â‡v–f”6öæå54”BÂv–f”6öæå72“°¢V–çC3%÷BCÒÖ–ÆÆ—2‚“°¢v†–ÆR…v”f’ç7FGW2‚’ÒtÅô4ôääT5DTBbbÖ–ÆÆ—2‚’ÒCÂdÄU„õ5õt”d•õD”ÔTõUEôÕ2—°¢eF6´FVÆ’‡DÕ5õDõõD”4µ2ƒ#’“²òò6VFR5RÓâæòÖöÆW7FæF–RÂæòF—7&EtE@¢Ð¢–b…v”f’ç7FGW2‚’ÓÒtÅô4ôääT5DTB—°¢tæWDöæÆ–æRÒG'VS°¢•FG&W72—Òv”f’æÆö6Ä•‚“°¢7G&–ær—2Ò—çFõ7G&–ær‚“°¢—2çFô6†$'&’‡v–f”6öæä•Â6—¦Vöb‡v–f”6öæä•’“°¢òòU%4•5DTä4”¢6öÆò6RwV&FÆòVR”6R†6ö×&ö&FòVP¢òògVæ6–öæâwV&F"çFW2FR6öæf—&Ö"FV¦&–Væ6ÆfRW'&öæV¢òòf–¦Vâåe2’VÂWV—ò&V–çFVçF&–6öâVÆÆVâ6F'&çVRà¢v–f”7&VG56fR‡v–f”6öæå54”BÂv–f”6öæå72“°¢v–f•T•7FFRÒuT•ôô³°¢ÒVÇ6R°¢tæWDöæÆ–æRÒfÇ6S°¢v”f’æF—66öææV7B‡G'VRÂG'VR“²òòÆ–&W&VÂ–çFVçFòfÆÆ–FòÂæòFV¦VÂ3bÖVF–0¢v–f•T•7FFRÒuT•ôd”Ã°¢Ð¢eF6´FVÆWFR„åTÄÂ“°§Ð ¢òòÒÒÒÒ$T4ôäU„”ôâUDôÔD”4Â%$ä4"ÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÐ¢òòÖ—6Öf÷&ÖVRv–f”6öæåF6²‡F&V&÷–VâVÂ6÷&RÂ‚´"FR–ÆÀ¢òò6W6–öæW26öâeF6´FVÆ’’ÂW&òäòFö6v–f•T•7FFS¢6÷'&RFRföæFò6öà¢òòÆçFÆÆFRv’Ôf’6W'&FÂ’W6f&–&ÆRW'FVæV6RW6çFÆÆà¢òò6’Fö6&v–f•T•7FFRÂ'&—"§W7FW2âv’Ôf’Ö—FBFVÂ–çFVçFð¢òòÖ÷7G&&–Vâ$6öæV7FæFòâââ"VRVÂW7V&–òæò†VF–Fòà§7FF–2fö–Bv–f”WFô6öæåF6²‡fö–B¢—°¢6W&–Âç&–çFb‚%µv”f•Ò&V6öæW†–öâWFöÖF–6Â"W5Â"ââåÆâ"Âv–f•6fVE54”B“°¢v–f”Vç7W&U7FÖöFR‚“°¢v”f’æ&Vv–â‡v–f•6fVE54”BÂv–f•6fVE72“°¢V–çC3%÷BCÒÖ–ÆÆ—2‚“°¢v†–ÆR…v”f’ç7FGW2‚’ÒtÅô4ôääT5DTBbbÖ–ÆÆ—2‚’ÒCÂdÄU„õ5õt”d•õD”ÔTõUEôÕ2—°¢eF6´FVÆ’‡DÕ5õDõõD”4µ2ƒ#’“°¢Ð¢–b…v”f’ç7FGW2‚’ÓÒtÅô4ôääT5DTB—°¢tæWDöæÆ–æRÒG'VS°¢•FG&W72—Òv”f’æÆö6Ä•‚“°¢7G&–ær—2Ò—çFõ7G&–ær‚“°¢—2çFô6†$'&’‡v–f”6öæä•Â6—¦Vöb‡v–f”6öæä•’“°¢7G&æ7’‡v–f”6öæå54”BÂv–f•6fVE54”BÂ6—¦Vöb‡v–f”6öæå54”B’Ò“°¢v–f”6öæå54”E·6—¦Vöb‡v–f”6öæå54”B’ÒÒÒ°¢6W&–Âç&–çFb‚%µv”f•Ò&V6öæV7FFòâ•W5Æâ"Âv–f”6öæä•“°¢ÒVÇ6R°¢òòfÆÆò‡&÷WFW"vFòÂ6ÆfR6Ö&–FÂgVW&FRÆ6æ6R“¢6R7VVÇF¢òòÆ&F–ò’äò6R&÷'&æFâÆ27&VFVæ6–ÆW26–wVVâwV&FF2&¢òòVÂ&÷†–Öò'&çVS²VÂW7V&–òVVFRVçG&"§W7FW2âv’Ôf’¢òò6öæf–wW&"ÖæòÂVRW2VÂ6Ö–æòFR6–V×&Rà¢tæWDöæÆ–æRÒfÇ6S°¢v”f’æF—66öææV7B‡G'VRÂG'VR“°¢6W&–Âç&–çFÆâ„b‚%µv”f•Ò&V6öæW†–öâWFöÖF–6fÆÆ–FÓâVVFÆ6öæf–wW&6–öâÖçVÂ"’“°¢Ð¢uv–f”WFô'W7’ÒfÇ6S°¢uv–f”WFôFöæRÒG'VS°¢eF6´FVÆWFR„åTÄÂ“°§Ð ¢òòÆç¦VÂ–çFVçFòWFöÖF–6ò6’†’ÆvòwV&FFòâæò&Æ÷VVVÂ'&çVRà§7FF–2fö–Bv–f•G'”WFô6öææV7B‚—°¢–b†uv–f”WFô'W7’ÇÂuv–f”WFôFöæR’&WGW&ã°¢–b‚v–f”7&VG4W†—7B‚’—°¢uv–f”WFôFöæRÒG'VS°¢6W&–Âç&–çFÆâ„b‚%µv”f•Ò6–â&VBwV&FFÓâ6öæf–wW&6–öâÖçVÂ"’“°¢&WGW&ã°¢Ð¢uv–f”WFô'W7’ÒG'VS°¢…F6´7&VFU–ææVEFô6÷&R‡v–f”WFô6öæåF6²Â'v–f”WFò"Âƒ“"ÂåTÄÂÂÂåTÄÂÂ“°§Ð¢6VæF–bòòdÄU„õ5ôTä$ÄUõt”d ¢òòÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÐ¢òò$T4ôäU„”ôâUDôÔD”4D”dU$”D‡6RÆÆÖFW6FRÆö÷‚’¢òòÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÐ¢òò6RV¦V7WFVâ6FgVVÇFW&ò6öÆò†6RÆvòTäfW¢Â’çVæ6¢òòçFW2FRVRVÂ6—7FVÖW7FR÷W&F—fó¢W67&—F÷&–òòçFÆÆFP¢òò&Æ÷VVòÂ’Væ÷26VwVæF÷2FW7VW2FVÂVæ6VæF–Fòà¢òð¢òòVÂ&WG&6òäòW26÷6ÖWF–6òâÆ&F–òFRW7FÆ67VVÆvFRVà¢òò6ò×&ö6W6F÷"3b÷"4D”òÂ’ÆWfçF&ÆFVçG&òFR6WGW‚’W2ÆòVP¢òò&öGV6–VÂ'V6ÆRFR%ä”2†7&6‚’"VRFö7VÖVçFVÂ&Æ÷VR$TÀ¢òò%$åTR”åTä4Dô4Ä$D”ò"âF—7&æFöÆFW6FRV’ÂVÀ¢òòVæÆ6R6R'&RVâÆÖ—6ÖfVçFæ6öçFVæ–F’ö'6W'f&ÆRVRÆ¢òò'WFÖçVÃ¢6’VÂ3bfÆÆÂVÂ6—7FVÖ–W7Ff—fò’VÂfÆÆò6P¢òòfRÂVâfW¢FRÆÆWf'6R÷"FVÆçFRVÂ'&çVRVçFW&òà¢òòÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÐ§7FF–2fö–Bv–f”WFõ&V6öææV7EF–6²‚—°¢6–bdÄU„õ5ôTä$ÄUõt”d¢–b†uv–f”WFôFöæRÇÂuv–f”WFô'W7’’&WGW&ã°¢–b†u7FFRÒ5Eô„ôÔRbbu7FFRÒ5EôÄô4²’&WGW&ã²òòVâ'&æ6æFó¢W7W& ¢–b†Ö–ÆÆ—2‚’ÂdÄU„õ5õt”d•ôUDô4ôäåôDTÄ•ôÕ2’&WGW&ã°¢–b‚v–f”7&VG4W†—7B‚’—²uv–f”WFôFöæRÒG'VS²&WGW&ã²Ð¢6W&–Âç&–çFÆâ„b‚%µv”f•Ò6—7FVÖÆ—7FòÓâÆç¦æFò&V6öæW†–öâWFöÖF–6"’“°¢v–f•G'”WFô6öææV7B‚“°¢6VæF–`§Ð §7FF–2fö–Bv–f•7F'E66â‚—°¢6–bdÄU„õ5ôTä$ÄUõt”d¢v–f”Vç7W&U7FÖöFR‚“°¢v–f•T•7FFRÒuT•õ44ää”äs°¢÷'DTåDU%ô5$•D”4Â‚gv–f”×W‚“²v–f”æWD6÷VçBÒ²÷'DU„•Eô5$•D”4Â‚gv–f”×W‚“°¢…F6´7&VFU–ææVEFô6÷&R‡v–f•66åF6²Â'v–f•66â"Âƒ“"ÂåTÄÂÂÂåTÄÂÂ“²òò„´#¢66äæWGv÷&·2‚’²7G&–æræV6W6—FâÖ2VRd´"†Wf—F7F6²÷fW&fÆ÷r¢6VæF–`§Ð§7FF–2fö–Bv–f•7F'D6öææV7B‚—°¢6–bdÄU„õ5ôTä$ÄUõt”d¢–b‡v–f•6VÂÂÇÂv–f•6VÂãÒt”d•ôÔ…ôäUE2—²v–f•T•7FFRÒuT•ôÄ•5C²&WGW&ã²Òòò–æF–6R–çfÆ–FòÓâçVæ6ÆVW"v–f”æWG5µÒgVW&FR&ævð¢v–f”Vç7W&U7FÖöFR‚“°¢7G&æ7’‡v–f”6öæå54”BÂv–f”æWG5·v–f•6VÅÒç76–BÂ6—¦Vöb‡v–f”6öæå54”B’Ò“²v–f”6öæå54”E·6—¦Vöb‡v–f”6öæå54”B’ÒÒÒ°¢7G&æ7’‡v–f”6öæå72Âv–f•72Â6—¦Vöb‡v–f”6öæå72’Ò“²v–f”6öæå75·6—¦Vöb‡v–f”6öæå72’ÒÒÒ°¢v–f•T•7FFRÒuT•ô4ôääT5D”äs°¢…F6´7&VFU–ææVEFô6÷&R‡v–f”6öæåF6²Â'v–f”6öæâ"Âƒ“"ÂåTÄÂÂÂåTÄÂÂ“²òò„´#¢v”f’æ&Vv–â‚’²–ÆFVÂG&—fW"æV6W6—FâÖ2VRd´ ¢6VæF–`§Ð§7FF–2fö–Bv–f•74VæB†6öç7B6†"¢2—²–çBÂÒ7G&ÆVâ‡v–f•72’Â6ÂÒ7G&ÆVâ‡2“²–b„Â²6ÂÂ†–çB—6—¦Vöb‡v–f•72’Ò—²ÖVÖ7’‡v–f•72²ÂÂ2Â6Â“²v–f•75´Â²6ÅÒÒ²ÒÐ§7FF–2fö–Bv–f•74&6·76R‚—²–çBÂÒ7G&ÆVâ‡v–f•72“²–b„Ââ—²–çBÒÂÒ²v†–ÆR‡âbb‡v–f•75·Òb„3’ÓÒƒƒ’ÒÓ²v–f•75·ÒÒ²ÒÐ§7FF–2fö–Bv–f”&6²‚—²7G&ö¶U6Vtƒ3Â#bÂ‚Â‚Â"ãFbÂ&v#ScRƒ#SRÃ#SRÃ#SR’“²7G&ö¶U6Vtƒ‚Â‚Â3ÂÂ"ãFbÂ&v#ScRƒ#SRÃ#SRÃ#SR’“²Ð §7FF–2–çBv–f•&÷u’†–çB’—²&WGW&âS²’¢cc²Ð ¢òòvVöÖWG&–FRÆ÷2&÷FöæW2FVÂ–RâTä6öÆgVVçFR&VÂF–'V¦ò’&¢òòVÂF¢6’VÂ$öÇf–F"&VB"&V6RòFW6&V6R6VwVâ†–&VBwV&FFÀ¢òòÆ2F÷2'WF26Ö&–âÆfW¢’Æ¦öæVÇ6&ÆRæòVVFRFW6Æ–æV'6P¢òòFRÆò–çFFò†Ö—6Öò7&—FW&–òVR6WE&÷t6&B÷6WE&÷u“Vâ§W7FW2’à§7FF–2fö–Bv–f”'Få&V7G2†–çBb'’Â–çBb7‚Â–çBb7rÂ–çBbg‚Â–çBbgr—°¢'’Ò45%ô‚Ò“°¢–b‡v–f”7&VG4W†—7B‚’—²òòF÷2&÷FöæW2ÆFòÆFð¢–çBrÒ…45%õrÒC‚Ò"’ò#°¢7‚Ò#C²7rÒs°¢g‚Ò#B²r²#²grÒs°¢ÒVÇ6R²òò6öÆò$'W66"&VFW2"Â6VçG&Fð¢7‚Ò45%õrò"Ò²7rÒ##°¢g‚Ò²grÒ°¢Ð§Ð §7FF–2fö–Bv–f•&VæFW$Æ—7B‚—°¢6WD'Vb†f"“°¢f–ÆÅ&V7BƒÂÂ45%õrÂ45%ô‚Â&v#ScRƒBÃbÃ#B’“°¢v–f”&6²‚“°¢G&uFW‡D2…45%õrò"ÂcÂ%v’Ôf’"ÂBÂ&v#ScRƒ#SRÃ#SRÃ#SR’“°¢òòW7FFòFRÆ&VB&V6÷&FFÂ§W7Fò&¦òVÂF—GVÆòà¢–b‡v–f”7&VG4W†—7B‚’—°¢6†"7e³cEÓ°¢&ööÂöâÒ…v”f’ç7FGW2‚’ÓÒtÅô4ôääT5DTB“°¢6ç&–çFb‡7bÂ6—¦Vöb‡7b’Â"W2W2"Âöâò$6öæV7FFò"¢%&VBwV&FF¢"Âv–f•6fVE54”B“°¢G&uFW‡D2…45%õrò"Â"Â7bÂÂöâò&v#ScRƒ#Ã##Ãc’¢&v#ScRƒSÃS‚Ãs‚’“°¢ÒVÇ6R–b†uv–f”WFô'W7’—°¢G&uFW‡D2…45%õrò"Â"Â%&V6öæV7FæFòâââ"ÂÂ&v#ScRƒSÃS‚Ãs‚’“°¢Ð¢–çB6çC²÷'DTåDU%ô5$•D”4Â‚gv–f”×W‚“²6çBÒv–f”æWD6÷VçC²÷'DU„•Eô5$•D”4Â‚gv–f”×W‚“°¢–b‡v–f•T•7FFRÓÒuT•õ44ää”är—°¢G&uFW‡D2…45%õrò"Â3Â$'W66æFò&VFW2âââ"Â"Â&v#ScRƒcÃsÃ“b’“°¢ÒVÇ6R–b†6çBÓÒ—°¢G&uFW‡D2…45%õrò"Â3Â$æò6RVæ6öçG&&öâ&VFW2"Â"Â&v#ScRƒcÃsÃ“b’“°¢ÒVÇ6R°¢f÷"†–çB’Ò²’Â6çC²’²²—°¢–çB’Òv–f•&÷u’†’“²–b‡’â45%ô‚Òc’'&V³°¢f–ÆÅ&÷VæE&V7Bƒ#BÂ’Â45%õrÒC‚ÂSbÂBÂ&v#ScRƒ3Ã3BÃC‚’“°¢G&uv–f’ƒSbÂ’²#‚Â2Â&v#ScRƒ#SRÃ#SRÃ#SR’“°¢G&uFW‡D6Æ—ƒƒ‚Â’²"Âv–f”æWG5¶•Òç76–BÂ"Â&v#ScRƒ#CÃ#C"Ã#C‚’Â45%õrÒ“°¢–b‡v–f”æWG5¶•Òç6V7W&R—°¢f–ÆÅ&÷VæE&V7B…45%õrÒƒÂ’²#ÂbÂBÂ2Â&v#ScRƒƒÃƒbÃ#B’“°¢&57G&ö¶R…45%õrÒs"Â’²#ÂbÂƒÂ3cÂ"Â&v#ScRƒƒÃƒbÃ#B’“°¢Ð¢Ð¢Ð¢–çB'’Â7‚Â7rÂg‚Âgs°¢v–f”'Få&V7G2†'’Â7‚Â7rÂg‚Âgr“°¢f–ÆÅ&÷VæE&V7B‡7‚Â'’Â7rÂSbÂbÂ&v#ScRƒcÃÃ#3R’“²òò$'W66"&VFW2"‡&VW66æV"¢G&uFW‡D2‡7‚²7rò"Â'’²‚Âv–f•T•7FFRÓÒuT•õ44ää”ärò$'W66æFòâââ"¢$'W66"&VFW2"Â"Â&v#ScRƒ#SRÃ#SRÃ#SR’“°¢–b†grâ—²òò$öÇf–F"&VB"†6Ö&–òFR&÷WFW"¢f–ÆÅ&÷VæE&V7B†g‚Â'’ÂgrÂSbÂbÂ&v#ScRƒS‚Ãc"Ãƒ’“°¢G&uFW‡D2†g‚²grò"Â'’²‚Â$öÇf–F"&VB"Â"Â&v#ScRƒ#CÃƒÃƒ’“°¢Ð¢fÇ„fÇW6„ÆÂ‚“°§Ð§7FF–2fö–Bv–f•&VæFW%72†–çB–öfb—°¢6WD'Vb†&'Vb“°¢f–ÆÅ&V7BƒÂÂ45%õrÂ45%ô‚Â&v#ScRƒBÃbÃ#B’“°¢v–f”&6²‚“°¢6†"F—FÆU³C…Ó²6ç&–çFb‡F—FÆRÂ6—¦Vöb‡F—FÆR’Â$6öçG&6UÇ„35Ç„#"&FRW2"Âv–f”æWG5·v–f•6VÅÒç76–B“°¢G&uFW‡D2…45%õrò"ÂSÂF—FÆRÂ"Â&v#ScRƒ#SRÃ#SRÃ#SR’“°¢–çB6çBÒWFc„6÷VçB‡v–f•72“°¢f÷"†–çB’Ò²’Â6çBbb’Âƒ²’²²’f–ÆÄ6—&6ÆRƒ3²’¢#BÂ#ÂrÂ&v#ScRƒ“ÃSÃ#C’“°¢–çB·’Ò´%õ’²–öfc°¢–b‡V”vÆ72—²G&tÆ—V–DvÆ75æVÂƒÂ·’ÒBÂ45%õrÂ45%ô‚Ò†·’ÒB’ÂÂ&v#ScRƒ3bÃCÃS‚’“²Ð¢VÇ6Rf–ÆÅ&V7BƒÂ·’ÒBÂ45%õrÂ45%ô‚Ò†·’ÒB’Â&v#ScRƒ‚Ã#Ã#‚’“°¢–çBg2Ò¶$föçE6—¦R‚“°¢f÷"†–çB"Ò²"Â´%õ$õu3²"²²’f÷"†–çB2Ò²2Â´%ô4ôÅ3²2²²—°¢–çB‚Ò´%õ‚²2¢„´%ôµr²´%ôt’Â’Ò·’²"¢„´%ô´‚²´%ôt“°¢6öç7B6†"¢²ÒÖ7F—fõ·%Õ¶5Ó°¢6†"U³eÓ°¢–b†¶%6†–gBbbµ³ÒÓÒbbµ³ÒãÒvrbbµ³ÒÃÒw¢r—²U³ÒÒ†6†"’†µ³ÒÒ3"“²U³ÒÒ²²ÒS²Ð¢¶%–çD¶W’‡‚Â’Â´%ôµrÂ´%ô´‚Â²Âg2Â¶$6öÄ¶W’‚’Â¶$6öÄ¶W•G‡B‚’ÂfÇ6R“°¢Ð¢–çBg’Ò·’²2¢„´%ô´‚²´%ôt“°¢6öç7B6†"¢Æ%´´%ôd´U•5ÒÒ²'6†–gB"Â¶$Æ–W$Æ&VÂ‚’Â¶$ÆætW2ò$U2"¢$Tâ"Â&W76–ò"Â#ÂÒ"Â$6öæV7F""Ó°¢f÷"†–çB’Ò²’Â´%ôd´U•3²’²²’¶$d¶W’†¶$d¶W•‚†’’Âg’Â¶$d¶W•r†’’ÂÆ%¶•ÒÂ†’ÓÒ’bb¶%6†–gB“°¢&W6VçBƒÂ45%ô‚Ò“°§Ð§7FF–2fö–Bv–f•&VæFW%7FGW2‚—°¢6WD'Vb†f"“°¢f–ÆÅ&V7BƒÂÂ45%õrÂ45%ô‚Â&v#ScRƒBÃbÃ#B’“°¢v–f”&6²‚“°¢–b‡v–f•T•7FFRÓÒuT•ôô²—°¢G&t6—&6ÆR…45%õrò"Â#ƒÂCbÂ&v#ScRƒ“Ã##ÃC’“²G&t6—&6ÆR…45%õrò"Â#ƒÂCRÂ&v#ScRƒ“Ã##ÃC’“°¢7G&ö¶U6Vt…45%õrò"Ò#Â#ƒÂ45%õrò"ÒBÂ3ÂBãbÂ&v#ScRƒ“Ã##ÃC’“°¢7G&ö¶U6Vt…45%õrò"ÒBÂ3Â45%õrò"²#bÂ#cÂBãbÂ&v#ScRƒ“Ã##ÃC’“°¢G&uFW‡D2…45%õrò"Â3SÂ$6öæV7FFò"Â2Â&v#ScRƒ#SRÃ#SRÃ#SR’“°¢6†"—Å³CÓ²6ç&–çFb†—ÂÂ6—¦Vöb†—Â’Â$•¢W2"Âv–f”6öæä•“°¢G&uFW‡D2…45%õrò"Â3“Â—ÂÂ"Â&v#ScRƒcÃsÃ“b’“°¢f–ÆÅ&÷VæE&V7B…45%õrò"ÒÂ45%ô‚Ò#Â#ÂSbÂbÂ&v#ScRƒcÃÃ#3R’“°¢G&uFW‡D2…45%õrò"Â45%ô‚Ò"Â$Æ—7Fò"Â"Â&v#ScRƒ#SRÃ#SRÃ#SR’“°¢ÒVÇ6R–b‡v–f•T•7FFRÓÒuT•ô4ôääT5D”är—°¢G&uFW‡D2…45%õrò"Â#ƒÂ$6öæV7FæFòâââ"Â2Â&v#ScRƒ#SRÃ#SRÃ#SR’“°¢6†"7V%³C…Ó²6ç&–çFb‡7V"Â6—¦Vöb‡7V"’Â&W2"Âv–f”6öæå54”B“°¢G&uFW‡D2…45%õrò"Â3#Â7V"Â"Â&v#ScRƒcÃsÃ“b’“°¢ÒVÇ6R²òòuT•ôd”À¢G&t6—&6ÆR…45%õrò"Â#ƒÂCbÂ&v#ScRƒ#3Ã“Ã“’“²G&t6—&6ÆR…45%õrò"Â#ƒÂCRÂ&v#ScRƒ#3Ã“Ã“’“°¢7G&ö¶U6Vt…45%õrò"ÒBÂ#cBÂ45%õrò"²BÂ#“bÂBãbÂ&v#ScRƒ#3Ã“Ã“’“°¢7G&ö¶U6Vt…45%õrò"²BÂ#cBÂ45%õrò"ÒBÂ#“bÂBãbÂ&v#ScRƒ#3Ã“Ã“’“°¢G&uFW‡D2…45%õrò"Â3SÂ$æò6RVFò6öæV7F""Â2Â&v#ScRƒ#SRÃ#SRÃ#SR’“°¢G&uFW‡D2…45%õrò"Â3ƒ‚Â$6öçG&6UÇ„35Ç„#"&–æ6÷'&V7Fò&VBgVW&FR&ævò"ÂÂ&v#ScRƒcÃsÃ“b’“°¢f–ÆÅ&÷VæE&V7B…45%õrò"Ò#Â45%ô‚Ò#Â#ÂSbÂbÂ&v#ScRƒsÃsBÃ“’“°¢G&uFW‡D2…45%õrò"ÒÂ45%ô‚Ò"Â$6æ6VÆ""Â"Â&v#ScRƒ#SRÃ#SRÃ#SR’“°¢f–ÆÅ&÷VæE&V7B…45%õrò"²Â45%ô‚Ò#Â#ÂSbÂbÂ&v#ScRƒcÃÃ#3R’“°¢G&uFW‡D2…45%õrò"²Â45%ô‚Ò"Â%&V–çFVçF""Â"Â&v#ScRƒ#SRÃ#SRÃ#SR’“°¢Ð¢fÇ„fÇW6„ÆÂ‚“°§Ð§7FF–2fö–Bv–f•&VæFW%Væf–Â‚—°¢6WD'Vb†f"“°¢f–ÆÅ&V7BƒÂÂ45%õrÂ45%ô‚Â&v#ScRƒBÃbÃ#B’“°¢v–f”&6²‚“°¢G&uFW‡D2…45%õrò"ÂcÂ%v’Ôf’"ÂBÂ&v#ScRƒ#SRÃ#SRÃ#SR’“°¢G&uFW‡D2…45%õrò"Â3Â%v’Ôf’FW67F—fFòVâW7FR'V–ÆB"Â"Â&v#ScRƒcÃsÃ“b’“°¢G&uFW‡D2…45%õrò"Â33BÂ"„dÄU„õ5ôTä$ÄUõt”d’ÒVâVÂæ–æò’"ÂÂ&v#ScRƒ#Ã#‚ÃS’“°¢fÇ„fÇW6„ÆÂ‚“°§Ð§7FF–2fö–Bv–f”W†—B‚—²u7FFRÒ5Eô²6WGF–æw5&VæFW"‚“²Ð §7FF–2fö–Bv–f•6WGF–æw4VçFW"‚—°¢u7FFRÒ5Eõt”d“°¢6–bdÄU„õ5ôTä$ÄUõt”d¢v–f•6VÂÒÓ²v–f•75³ÒÒ°¢Ö7F—fòÒÄ”õUEôU3²¶$ÆætW2ÒG'VS²¶%6†–gBÒfÇ6S°¢¶$W‡G&4öâÒfÇ6S²¶$Ç•6—¦R‚“²¶$×E7W&f6U&W6WB‚“²òòVÂFV6ÆFòFRv’Ôf’6öÆò†W&VFVÂDÔäò„f6R¢v–f•7F'E66â‚“°¢v–f•&VæFW$Æ—7B‚“°¢6VÇ6P¢v–f•&VæFW%Væf–Â‚“°¢6VæF–`§Ð§7FF–2fö–Bv–f•F–6²‚—°¢6–bdÄU„õ5ôTä$ÄUõt”d¢–b…BçFbbBç‚ÂC‚bbBç’ÂC‚’v–f”W†—B‚“°¢&WGW&ã°¢6VÇ6P¢òò&W–çFTäfW¢7VæFòVÂW7FFò6Ö&–÷"6W62W‡FW&æ2†Æ¢òòF&VFRW66æVòö6öæW†–öâVâ6÷&RFW&Ö–æò’âÆ2G&ç6–6–öæW2VP¢òòF—7&VÂ&÷–òF––çFâFR–æÖVF–FòÖ2&¦òà¢7FF–2–çBv–f•T•7FFU6†÷vâÒÓ°¢–b‡v–f•T•7FFRÒv–f•T•7FFU6†÷vâ—°¢v–f•T•7FFU6†÷vâÒv–f•T•7FFS°¢–b‡v–f•T•7FFRÓÒuT•ôÄ•5BÇÂv–f•T•7FFRÓÒuT•õ44ää”är’v–f•&VæFW$Æ—7B‚“°¢VÇ6R–b‡v–f•T•7FFRÓÒuT•ô4ôääT5D”ärÇÂv–f•T•7FFRÓÒuT•ôô²ÇÂv–f•T•7FFRÓÒuT•ôd”Â’v–f•&VæFW%7FGW2‚“°¢Ð¢7v—F6‚‡v–f•T•7FFR—°¢66RuT•ôÄ•5C¢°¢–b…BçF—°¢–b…Bç‚ÂC‚bbBç’ÂC‚—²v–f”W†—B‚“²&WGW&ã²Ð¢–çB'’Â7‚Â7rÂg‚Âgs°¢v–f”'Få&V7G2†'’Â7‚Â7rÂg‚Âgr“°¢–b…Bç’ãÒ'’bbBç’ÃÒ'’²Sb—°¢–b…Bç‚ãÒ7‚bbBç‚ÃÒ7‚²7r—²v–f•7F'E66â‚“²&WGW&ã²Ð¢–b†grâbbBç‚ãÒg‚bbBç‚ÃÒg‚²gr—²òòöÇf–F"&VBwV&FF¢v–f”7&VG4f÷&vWB‚“°¢v”f’æF—66öææV7B‡G'VRÂG'VR“²òò7VVÇFFÖ&–VâÆ6W6–öâVâ7W'6ð¢tæWDöæÆ–æRÒfÇ6S°¢v–f•&VæFW$Æ—7B‚“²òòVÂ&÷FöâFW6&V6RÂæò†&W"&V@¢&WGW&ã°¢Ð¢Ð¢–çB6çC²÷'DTåDU%ô5$•D”4Â‚gv–f”×W‚“²6çBÒv–f”æWD6÷VçC²÷'DU„•Eô5$•D”4Â‚gv–f”×W‚“°¢f÷"†–çB’Ò²’Â6çC²’²²—°¢–çB’Òv–f•&÷u’†’“²–b‡’â45%ô‚Òc’'&V³°¢–b…Bç’ãÒ’bbBç’ÃÒ’²SbbbBç‚ãÒ#BbbBç‚ÃÒ45%õrÒ#B—°¢v–f•6VÂÒ“°¢–b‚v–f”æWG5¶•Òç6V7W&R’v–f•7F'D6öææV7B‚“²òò&VB&–W'F¢6öæV7FF—&V7Fð¢VÇ6R²v–f•75³ÒÒ²v–f•T•7FFRÒuT•õ53²v–f”¶$æ–ÒÒÖ–ÆÆ—2‚“²Ð¢&WGW&ã°¢Ð¢Ð¢Ð¢'&V³°¢Ð¢66RuT•õ44ää”äs¢'&V³²òòçFÆÆW7FF–6$'W66æFòâââ#²VÂ&W–çFFòFR'&–&6Ö&–Ä•5B6öÆð¢66RuT•õ53¢°¢–b‡v–f”¶$æ–Ò—°¢fÆöBÒ†Ö–ÆÆ—2‚’Òv–f”¶$æ–Ò’ò3ãc²–b‡ãÒ—²Ò²v–f”¶$æ–ÒÒ²Ð¢v–f•&VæFW%72‚†–çB’‚ƒãbÒ’¢…45%ô‚Ò´%õ’’’“°¢&WGW&ã°¢Ð¢–b…BçF—°¢–b…Bç‚ÂC‚bbBç’ÂC‚—²v–f•T•7FFRÒuT•ôÄ•5C²v–f•&VæFW$Æ—7B‚“²&WGW&ã²Ð¢–çBf’Ò¶$e&÷t†—B…Bç‚ÂBç’“°¢–b†f’ãÒ—°¢–b†f’ÓÒ’¶%6†–gBÒ¶%6†–gC°¢VÇ6R–b†f’ÓÒ’Ö7F—fòÒ†Ö7F—fòÓÒÄ”õUEôåTÒ’òÄ”õUEôTÔô¤’¢†Ö7F—fòÓÒÄ”õUEôTÔô¤’’ò†¶$ÆætW2òÄ”õUEôU2¢Ä”õUEôTâ’¢Ä”õUEôåTÓ°¢VÇ6R–b†f’ÓÒ"—²¶$ÆætW2Ò¶$ÆætW3²–b†Ö7F—fòÓÒÄ”õUEôU2ÇÂÖ7F—fòÓÒÄ”õUEôTâ’Ö7F—fòÒ¶$ÆætW2òÄ”õUEôU2¢Ä”õUEôTã²Ð¢VÇ6R–b†f’ÓÒ2’v–f•74VæB‚""“°¢VÇ6R–b†f’ÓÒB’v–f•74&6·76R‚“°¢VÇ6R–b‡7G&ÆVâ‡v–f•72’â—²v–f•7F'D6öææV7B‚“²&WGW&ã²Ð¢v–f•&VæFW%72ƒ“²&WGW&ã°¢Ð¢–çB6VÆÂÒ¶$6VÆÄB…Bç‚ÂBç’“°¢–b†6VÆÂãÒ—°¢6öç7B6†"¢²ÒÖ7F—fõ¶6VÆÂò´%ô4ôÅ5Õ¶6VÆÂR´%ô4ôÅ5Ó°¢–b†¶%6†–gBbbµ³ÒÓÒbbµ³ÒãÒvrbbµ³ÒÃÒw¢r—²6†"U³%ÒÒ²†6†"’†µ³ÒÒ3"’ÂÓ²v–f•74VæB‡R“²¶%6†–gBÒfÇ6S²Ð¢VÇ6Rv–f•74VæB†²“°¢v–f•&VæFW%72ƒ“°¢Ð¢Ð¢'&V³°¢Ð¢66RuT•ô4ôääT5D”äs¢'&V³²òòçFÆÆW7FF–6$6öæV7FæFòâââ#²VÂ&W–çFFòFR'&–&6Ö&–ô²ôd”Â6öÆð¢66RuT•ôô³¢°¢–b…BçF—°¢–b…Bç‚ÂC‚bbBç’ÂC‚—²v–f”W†—B‚“²&WGW&ã²Ð¢–b…Bç’ãÒ45%ô‚Ò#bbBç’ÃÒ45%ô‚ÒcBbbBç‚ãÒ45%õró"ÒbbBç‚ÃÒ45%õró"²—²v–f”W†—B‚“²&WGW&ã²Ð¢Ð¢'&V³°¢Ð¢66RuT•ôd”Ã¢°¢–b…BçF—°¢–b…Bç‚ÂC‚bbBç’ÂC‚—²v–f•T•7FFRÒuT•ôÄ•5C²v–f•&VæFW$Æ—7B‚“²&WGW&ã²Ð¢–b…Bç’ãÒ45%ô‚Ò#bbBç’ÃÒ45%ô‚ÒcB—°¢–b…Bç‚ãÒ45%õró"Ò#bbBç‚ÃÒ45%õró"Ò—²v–f•T•7FFRÒuT•ôÄ•5C²v–f•&VæFW$Æ—7B‚“²&WGW&ã²Òòò6æ6VÆ ¢–b…Bç‚ãÒ45%õró"²bbBç‚ÃÒ45%õró"²#—²v–f•75³ÒÒ²v–f•T•7FFRÒuT•õ53²v–f”¶$æ–ÒÒÖ–ÆÆ—2‚“²&WGW&ã²Òòò&V–çFVçF ¢Ð¢Ð¢'&V³°¢Ð¢Ð¢6VæF–`§Ð ¢òò2222222222222222222222222222222222222222222222222222222222220¢òò22•4ÄD”äÔ”4+rÆöv–6’&VæFW"„d4RÂ&6†RçF’ÖfÆ–6¶W"¢òò22ÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÐ¢òò22d•‚Æ–6FòG&2VÂ'VrFR'FVò²F&¦WF2VvF3 ¢òò22Æ—6Æ–äò6ö×öæR6ö'&Rf"†VÂ'VffW"VRfÇ…&W6VçFW ¢òò22ÆVR÷"DÔVâ÷G&ò6÷&R’â†÷&&W7FW&Æ&æFÆ–×–¢òò226÷–æFòFW6FR†öÖT'Vb†6–&'VbÂF–'V¦Æ2F&¦WF26ö'&R&'VbÂ’7'W¦¢òò22f"6öâVâVæ–6ò&W6VçB‚’FöÖ–6òâ&'VbW2FRVâ6öÆð¢òò22W67&—F÷"†VÂÆö÷F6²“²æF–RÖ2ÆòÆVRÂ6’VRVÀ¢òò22&W6VçFW"çVæ6VVFR6GW&"Vâg&ÖRÖVF–ò–çF"à¢òò22÷"W6òVÂ6ö×÷6R‡&W7F÷&R¶F–'V¦"·&W6VçB’6öÆò6÷'&R6öà¢òò22u7FFSÓÕ5Eô„ôÔS¢†öÖT'Vb6öÆòW2VâföæFòfÆ–Fò†’âVÀ¢òò22fæ6RFRf6W26–wVR6–â6öæF–6–öâ†W2&—FÖWF–6W&’à¢òò20¢òò22Vâf6Räò†’FWFV66–öâ“$2&VÃ¢Æ2æ÷F–f–66–öæW26P¢òò22F—7&â6öâVâG&–vvW"FR'VV&†FVÖòÂ&–ÖW"†öÖR²F ¢òò22'&–&ÖFW&V6†’&fÆ–F"&VæFW"öæ–Ö6–öâöFW66'FRFP¢òò22f÷&Ö•4ÄDà¢òò20¢òò22DU5d”4”ôâDTÄ”$U$D&W7V7FòÂÆâ÷&–v–æÃ¢VÂf–G&–ð¢òò226R$RÔ„õ$äT6Fg&ÖR†G&tÆ—V–DvÆ75æVÂ’VâfW¢FP¢òò22&†÷&æV"VæfW¢"â6R†6R6’6öÆò÷'VR6öâÃÓ2F&¦WF0¢òò22WVVæ2ƒCC‡ƒcB’’VÂ6÷7FRW2&¦ó²W&Ö—FRVRVÀ¢òò22FW6Æ—¦Ö–VçFò&WWF–Æ–6RVÂÖ—6Öò6Ö–æò6–â66†V"Vâ'VffW ¢òò22÷"F&¦WFâVÂ&ÇW"6÷7F÷6ò‡çFÆÆ6ö×ÆWF’6R6–wVRWf—FæFòà¢òò2222222222222222222222222222222222222222222222222222222222220 ¢òò–6öæòFVÂÖöGVÆó¢&WWF–Æ—¦Æ÷2–6öæ÷2FRW†—7FVçFW2†ÖVò6–×ÆR§7FF–2fö–BG&tÖöGVÆT–6öâ„ÖöGVÆUG—RG—RÂ–çB‚Â–çB’Â–çB2—°¢–çB–BÒ”5ô¥U5DU3°¢7v—F6‚‡G—R—°¢66RÔôEõTÅE$4ôä”3¢–BÒ”5ôäc²'&V³°¢66RÔôEô$ÔS#ƒ¢–BÒ”5ô$”Tã²'&V³°¢66RÔôEôÕScS¢–BÒ”5ô¥TTtõ3²'&V³°¢66RÔôEôÄTC¢–BÒ”5ô4Ä3²'&V³°¢66RÔôEô%UEDôã¢–BÒ”5ôäõD3²'&V³°¢66RÔôEõ4U%dó¢–BÒ”5ôÔôDõ3²'&V³°¢66RÔôEô“$5ôtTäU$”3¢–BÒ”5ôÄÔ4Tã²'&V³°¢FVfVÇC¢–BÒ”5ô¥U5DU3²'&V³°¢Ð¢G&t–6öâ†–BÂ‚Â’Â2“°§Ð ¢òò&W7FW&Æ&æFÆ–×–Tâ&'VbÂ6÷–æFòFW6FR†öÖT'Vb‡6–V×&RÂF– ¢òò6R&V6ö×öæR6öÆòVâ6F6Ö&–òFRÖ–çWFòÂÂ6Æ—"FRVF–6–öâÂWF2â’à¢òò†öÖT'VbW2ÆgVVçFR’&'VbVÂÆ–Vç¦òFRG&&¦òâ–æò†6RfÇF¢òò6æ6†÷BÖçVÂ†æ÷F–e6æ6†÷D&rFW6&V6R’à§7FF–2fö–Bæ÷F–e&W7F÷&T&r‚—°¢–b‚†öÖT'VbÇÂ&'Vb’&WGW&ã°¢ÖVÖ7’†&'Vb²‡6—¦U÷B”äõD”eô$äEõDõ¢45%õrÂ†öÖT'Vb²‡6—¦U÷B”äõD”eô$äEõDõ¢45%õrÀ¢‡6—¦U÷B•45%õr¢äõD”eô$äEô‚¢"“°§Ð ¢òòVæ6öÆVææ÷F–f–66–öâ'F—"FRVâÖöGVÆð§7FF–2fö–Bæ÷F–eW6‚†6öç7BFWFV7FVDÖöGVÆR¢Ò—°¢–b†tæ÷F–d6÷VçBãÒäõD”eôÔ‚’&WGW&ã²òò6öÆÆÆVæ¢6RFW66'F„f6R¢æ÷F–f–6F–öâ¢âÒftæ÷F–g5¶tæ÷F–d6÷VçB²µÓ°¢âÓæÖöBÒ¦Ó°¢âÓæ7F—fRÒG'VS°¢âÓç†6RÒåô”ã°¢âÓæ&÷&ä×2ÒÖ–ÆÆ—2‚“°¢âÓç6Æ–FU‚Òãc°¢âÓæ&ÖVBÒfÇ6S²òò6R&Ö†VçG&F²R2’Â†6W'6Rf—6–&ÆRVâ†öÖP§Ð ¢òòVÆ–Ö–æÆ&çW&–G‚’6ö×7FÆ6öÆ§7FF–2fö–Bæ÷F–e&VÖ÷fR†–çB–G‚—°¢–b†–G‚ÂÇÂ–G‚ãÒtæ÷F–d6÷VçB’&WGW&ã°¢f÷"†–çB¢Ò–Gƒ²¢Âtæ÷F–d6÷VçBÒ²¢²²’tæ÷F–g5¶¥ÒÒtæ÷F–g5¶¢²Ó°¢tæ÷F–d6÷VçBÒÓ°¢tæ÷F–g5¶tæ÷F–d6÷VçEÒæ7F—fRÒfÇ6S°¢–b†æ÷F–dG&t–G‚ÓÒ–G‚’æ÷F–dG&t–G‚ÒÓ°¢VÇ6R–b†æ÷F–dG&t–G‚â–G‚’æ÷F–dG&t–G‚ÒÓ°§Ð ¢òòV6RÖ÷WB7V&–6ƒâã§7FF–2–æÆ–æRfÆöBæ÷F–dV6T÷WB†fÆöB—²fÆöBÒãbÒ²&WGW&âãbÒ¢¢²Ð ¢òò6öÆFR'W&'V¦FR6†C¢VâG&–æwVÆòVçFæFò†6–%$”$Â÷'VRÆ0¢òòF&¦WF2FRæ÷F–f–66–öâ6VâFW6FRVÂ&÷&FR7WW&–÷"FRÆçFÆÆ†æð¢òò†’Vâ–6öæòFRVâVÂ†öÖRÂVRVçF"ÒÒW7F26öâFWFV66–öæW2FP¢òò†&Gv&R“$2f–‡tFWFV7EF–6²‚’Âæòæ÷F–f–66–öæW2VRfVævâFRVæ ¢òò&–W'F’â6öÆ–FòÂæòf–G&–ó¢W2FVÖ6–FòWV\;&VRVÂ&ÇW"6P¢òòæ÷FRÂ’w&æF"VÂæVÂ6öÆò&Æ6öÆæòfÆRÆVæà§7FF–2fö–Bæ÷F–dG&uF–Â†–çB7‚Â–çBF÷’ÂV–çCe÷B6öÂ—°¢f–ÆÅG&–ævÆR†7‚Ò‚ÂF÷’Â7‚²‚ÂF÷’Â7‚ÂF÷’Ò’Â6öÂ“°§Ð¢òòF–'V¦VæF&¦WFVâÆ6ö÷&FVæF’FF†Æ–67R6Æ–FU‚†÷&—¦öçFÂ§7FF–2fö–Bæ÷F–dG&t6&B„æ÷F–f–6F–öâ¢âÂ–çB6&E’—°¢–çB‚ÒäõD”eôÔ$t”åõ‚²†–çB–âÓç6Æ–FUƒ²òòÂFW6Æ—¦"Æ—§Â‚6RgVVÇfRæVvF—fð¢–çB’Ò6&E’ÂrÒäõD”eô4$EõrÂ‚ÒäõD”eô4$Eôƒ°¢æ÷F–dG&uF–Â‡‚²rò"Â’Â&v#ScRƒ“Â#Â#’“²òò&–ÖW&ó¢ÆF&¦WF6RF–'V¦§W7FòFV&¦òÂ6–â6öÆ&Æ¢òòf–G&–ò&6R†&ÇW"’âG&tÆ—V–DvÆ75æVÂ&V6÷'FƒÃ ¢òò6öç6W'fæFòVÂ&÷&FRFW&V6†òÓâVÂFW6Æ—¦Ö–VçFòÆ—§V–W&F6ÆRæGW&Âà¢G&tÆ—V–DvÆ75æVÂ‡‚Â’ÂrÂ‚ÂäõD”eõ$BÂ&v#ScRƒCÂcÂ3’“°¢òòFVw&FFòW‡G&W7F–Æò'W&'V¦†Ö26Æ&ò'&–&ÂÖ2÷67W&ò&¦ò’à¢òòf–Æf–Æ6öâvÄ–ç6WB‚’ÒÒ–wVÂVRG&tÆ—V–DvÆ75æVÂÒÒ&æð¢òò6Æ—'6RFRÆ2W7V–æ2&VFöæFVF2‡Væf–ÆÅ&V7DÆæ<:Ò6R6ÆG,:Ö’à¢f÷"†–çB¢Ò²¢Âƒ²¢²²—°¢–çB–ç2ÒvÄ–ç6WB†¢Â‚ÂäõD”eõ$B“°¢V–çC…÷BÒ‡V–çC…÷B’ƒC"ÒC"¢¢ò‚“°¢–b†â’„Æ–æT‡‚²–ç2Â’²¢ÂrÒ"¢–ç2Â&v#ScRƒ#SRÃ#SRÃ#SR’Â“°¢Ð¢G&u&÷VæE&V7B‡‚Â’ÂrÂ‚ÂäõD”eõ$BÂ&v#ScRƒ#Â#Â#3’“°¢òò–6öæòCƒC†Æ2&–Ö—F—f26÷Fâ6ö÷&G2æVvF—f3¢6VwW&ògVW&FRçFÆÆ¢G&tÖöGVÆT–6öâ†âÓæÖöBçG—RÂ‚²"Â’²†‚ÒC’ò"ÂC“°¢òòFW‡F÷0¢G&uFW‡B‡‚²c"Â’²BÂâÓæÖöBææÖRÂ"Â&v#ScRƒ#SRÂ#SRÂ#SR’“°¢G&uFW‡B‡‚²c"Â’²3‚ÂâÓæÖöBç7V"ÂÂ&v#ScRƒ#RÂ#BÂ#3"’“°¢òò&÷Föâ6W'&"…‚¢–çB7‚Ò‚²rÒ#"Â7’Ò’²#°¢7G&ö¶U6Vt†7‚ÒRÂ7’ÒRÂ7‚²RÂ7’²RÂã†bÂ&v#ScRƒ#SRÂ#SRÂ#SR’“°¢7G&ö¶U6Vt†7‚ÒRÂ7’²RÂ7‚²RÂ7’ÒRÂã†bÂ&v#ScRƒ#SRÂ#SRÂ#SR’“°§Ð ¢òòÒÒÒÒG&–vvW"FR%TT$‡6öÆòf6R²6R&WF—&÷&VV×Æ¦Vâf6R"’ÒÒÒÐ¢òò6ö×–ÆFò4ôÄò6’äõD”eôDTÔõõE$”ttU%2Òâ7W2F÷2Væ–6÷2ÆÆÖF÷&W2W7Fà¢òò&¦òVÂÖ—6Öò6–bÂ6’VRFV¦&Æò6–V×&R&W6VçFRF&Vâf—6òFRgVæ6–öà¢òòW7FF–66–âW6"Vâ6F6ö×–Æ6–öâà¢6–bäõD”eôDTÔõõE$”ttU%0¢òòV×V¦Vææ÷F–f–66–öâFVÖòÂ&÷FæFò÷"F—÷2&fW"FöF÷2Æ÷2–6öæ÷2à§7FF–2fö–Bæ÷F–eW6„FVÖò‚—°¢7FF–2V–çC…÷B²Ò°¢FWFV7FVDÖöGVÆRÓ°¢ÖV×6WB‚fÒÂÂ6—¦Vöb†Ò’“°¢Òæ7F—fRÒG'VS²ÒæFWFV7FVDBÒÖ–ÆÆ—2‚“°¢7v—F6‚†²RR—°¢66R¢ÒçG—RÒÔôEô$ÔS#ƒ²Òæ“&4FG"Òƒsc°¢6ç&–çFb†ÒææÖRÂ6—¦Vöb†ÒææÖR’Â%6Vç6÷"$ÔS#ƒ"“°¢6ç&–çFb†Òç7V"Â6—¦Vöb†Òç7V"’Â$“$2ƒsbFWFV7FFò"“²'&V³°¢66R¢ÒçG—RÒÔôEôÕScS²Òæ“&4FG"Òƒcƒ°¢6ç&–çFb†ÒææÖRÂ6—¦Vöb†ÒææÖR’Â$ÕScS"“°¢6ç&–çFb†Òç7V"Â6—¦Vöb†Òç7V"’Â$”ÕRÒ“$2ƒc‚"“²'&V³°¢66R#¢ÒçG—RÒÔôEõTÅE$4ôä”3°¢6ç&–çFb†ÒææÖRÂ6—¦Vöb†ÒææÖR’Â%VÇG&6öæ–Fò"“°¢6ç&–çFb†Òç7V"Â6—¦Vöb†Òç7V"’Â$„2Õ5#BFWFV7FFò"“²'&V³°¢66R3¢ÒçG—RÒÔôEõ4U%dó°¢6ç&–çFb†ÒææÖRÂ6—¦Vöb†ÒææÖR’Â%6W'fò"“°¢6ç&–çFb†Òç7V"Â6—¦Vöb†Òç7V"’Â$7GVF÷"tÒ"“²'&V³°¢FVfVÇC¦ÒçG—RÒÔôEô“$5ôtTäU$”3²Òæ“&4FG"Òƒ43°¢6ç&–çFb†ÒææÖRÂ6—¦Vöb†ÒææÖR’Â$F—7÷6—F—fò“$2"“°¢6ç&–çFb†Òç7V"Â6—¦Vöb†Òç7V"’Â#ƒ42FWFV7FFò"“²'&V³°¢Ð¢²²³°¢æ÷F–eW6‚‚fÒ“°§Ð¢6VæF–bòòäõD”eôDTÔõõE$”ttU%0 ¢òòÒÒÒÒF÷VRFRÆ—6Æ¢–çFW&6WF4ôÄòFVçG&òFRÆ2F&¦WF2ÒÒÒÐ¢òò6RÆÆÖVâÆö÷‚’§W7FòFW7VW2FRfÆW…öÆÅF÷V6‚‚’’çFW2FVÂ7v—F6‚FP¢òòW7FFòâ6öç7VÖRVæ–6ÖVçFRÆ÷2fÆw2FRWfVçFòVRW6‡F÷&W76VB÷&VÆV6VBð¢òò7v—TÆVgB“²åTä4Fö6BæF÷vâ†ÆòvW7F–öæfÆW…öÆÅF÷V6‚’&æò6÷'&ö×W"Æ¢òòÖV–æFRW7FF÷2FVÂF7F–Âà§7FF–2fö–Bæ÷F–d†æFÆUF÷V6‚‚—°¢òòÆ—6Æ6öÆò&V6–&RF÷VW27VæFòW2f—6–&ÆR„†öÖR&–æ6—ÂFW6&Æ÷VVFò’à¢–b†u7FFRÒ5Eô„ôÔRÇÂ5æVÅ’ÒÇÂVF—DÖöFR—²æ÷F–dG&t–G‚ÒÓ²&WGW&ã²Ð¢òòF÷VW2VâF&¦WF2†6W'&"ÂfÆ–6²Â–æ–6–"'&7G&R¢f÷"†–çB’Ò²’Âtæ÷F–d6÷VçC²’²²—°¢–b‚tæ÷F–g5¶•Òæ7F—fRÇÂtæ÷F–g5¶•Òç†6RÓÒåôõUB’6öçF–çVS°¢–çB6&E’ÒäõD”eõ“²’¢„äõD”eô4$Eô‚²äõD”eôt“°¢–çBƒÒäõD”eôÔ$t”åõ‚ÂƒÒäõD”eôÔ$t”åõ‚²äõD”eô4$Eõs°¢–çB“Ò6&E’Â“Ò6&E’²äõD”eô4$Eôƒ°¢òò&÷Föâ6W'&"…‚’'&–&ÖFW&V6†¢–çB7‚ÒäõD”eôÔ$t”åõ‚²äõD”eô4$EõrÒ#"Â7’Ò6&E’²#°¢–b…BçFbb'2…Bç‚Ò7‚’Âbbb'2…Bç’Ò7’’Âb—°¢tæ÷F–g5¶•Òç†6RÒåôõUC²BçFÒfÇ6S²Bç&W76VBÒfÇ6S²&WGW&ã°¢Ð¢òòfÆ–6²&–FòÆ—§V–W&F6ö'&RÆF&¦WF¢–b…Bç7v—TÆVgBbbBç7F'E’ãÒ“bbBç7F'E’ÃÒ“—°¢tæ÷F–g5¶•Òç†6RÒåôõUC²Bç7v—TÆVgBÒfÇ6S²BçFÒfÇ6S²&WGW&ã°¢Ð¢òò–æ–6–"'&7G&R†FVFòFVçG&òFRÆF&¦WF¢–b…Bç&W76VBbbBç‚ãÒƒbbBç‚ÃÒƒbbBç’ãÒ“bbBç’ÃÒ“—°¢æ÷F–dG&t–G‚Ò“²Bç&W76VBÒfÇ6S°¢Ð¢Ð¢òò'&7G&RVâ7W'6ð¢–b†æ÷F–dG&t–G‚ãÒbbæ÷F–dG&t–G‚Âtæ÷F–d6÷VçB—°¢æ÷F–f–6F–öâ¢âÒftæ÷F–g5¶æ÷F–dG&t–G…Ó°¢–b…BæF÷vâ—°¢fÆöBG‚Ò†fÆöB’…Bç‚ÒBç7F'E‚“°¢–b†G‚â’G‚Ò²òò6öÆò†6–Æ—§V–W&F¢–b†G‚ÂÒ„äõD”eô4$Eõr²C’’G‚ÒÒ„äõD”eô4$Eõr²C“°¢âÓç6Æ–FU‚ÒGƒ²âÓç†6RÒåôE$s°¢BçFÒfÇ6S²Bç7v—TÆVgBÒfÇ6S²òòæò&÷v"ÆçFÆÆ¢ÒVÇ6R°¢òò6öÇF#¢FW66'F"6’6òVÂVÖ'&ÂÂ6’æòföÇfW"7R6—F–ð¢–b†âÓç6Æ–FU‚ÂÔäõD”eô4$EõròB’âÓç†6RÒåôõUC°¢VÇ6RâÓç†6RÒåõ5$”äs°¢æ÷F–dG&t–G‚ÒÓ°¢BçFÒfÇ6S²Bç&VÆV6VBÒfÇ6S°¢Ð¢Ð¢6–bäõD”eôDTÔõõE$”ttU%0¢òò&R×G&–vvW"FR%TT$¢F'&–&ÖFW&V6†ÂgVW&FRÆ¦öæ6Æ–VçFRFRÆ¢òò6÷'F–æ‡VR6GW&7F'E“Ã3’’6öÆòVâ†öÖRâvVæW&Æ6–wV–VçFRFVÖòà¢–b…BçFbbu7FFRÓÒ5Eô„ôÔRbb5æVÅ’ÓÒbbVF—DÖöFRb`¢Bç‚ãÒ45%õrÒS"bbBç’ãÒ3bbbBç’ÃÒSb—°¢æ÷F–eW6„FVÖò‚“°¢BçFÒfÇ6S²Bç&W76VBÒfÇ6S°¢Ð¢6VæF–`§Ð ¢òòÒÒÒÒF–6²FRÆ—6Æ¢æ–Ö’6ö×öæR‡6RÆÆÖÂf–æÂFRÆö÷’ÒÒÒÐ§7FF–2fö–Bæ÷F–eF–6²‚—°¢òòG&–vvW"FR'VV&¢&–ÖW&FVÖòÂÆÆVv"†öÖP¢6–bäõD”eôDTÔõõE$”ttU%0¢7FF–2&ööÂ&ö÷DFVÖòÒfÇ6S°¢–b‚&ö÷DFVÖòbbu7FFRÓÒ5Eô„ôÔRbbÖ–ÆÆ—2‚’â#—²&ö÷DFVÖòÒG'VS²æ÷F–eW6„FVÖò‚“²Ð¢6VæF–` ¢òòF‡&÷GFÆRã3g0¢–b†Ö–ÆÆ—2‚’Òæ÷F–dÆ7D×2Â32’&WGW&ã°¢æ÷F–dÆ7D×2ÒÖ–ÆÆ—2‚“° ¢òòæFVRÖ÷7G&"’&æF–Æ–×–Óâ6Æ–F&&F¢–b†tæ÷F–d6÷VçBÓÒbbæ÷F–d&æDöâ’&WGW&ã° ¢òòÆ—6Æ4ôÄòf—fRVâVÂ†öÖR&–æ6—ÂFW6&Æ÷VVFò‡6–â6÷'F–ææ’VF–6–öâ’à¢òògVW&FR†’æòfç¦Ö÷2f6W2æ’F–'V¦Ö÷3¢Æ2æ÷F–f–66–öæW2FWFV7FF0¢òòGW&çFRVÂ&Æ÷VVòW7W&â6öævVÆF2’7Ræ–Ö6–öâFRVçG&F²Æ÷2R0¢òò'&æ6âÂÆÆVv"V’â6’FÖ&–VâWf—FÖ÷2VÂ6öæfÆ–7FòFRF–'V¦ò6öà¢òò÷G&2çFÆÆ2‡VR6öâV–VæW2FV&Vâ÷6VW"VÂf"VâW6RÖöÖVçFò’à¢–b†u7FFRÒ5Eô„ôÔRÇÂ5æVÅ’ÒÇÂVF—DÖöFR—°¢–b‚æ÷F–eW6VB—²æ÷F–eW6VBÒG'VS²æ÷F–eW6UCÒÖ–ÆÆ—2‚“²ÒòòÖ&6VÂ–æ–6–òFRÆW6‡æV¢â6R'&–òVæ¢&WGW&ã°¢Ð¢–b†æ÷F–eW6VB—°¢òò&VçVFæFòG&2VæW6‡æV¢â6R6W'&òÆVR6R'&–òVæ6–Ö“ ¢òò7VÖ"VÂF–V×òW6Fò&÷&ä×2FR6FF&¦WF7F—f&VP¢òò6öç6W'fVâVÂF–V×òVRÆW2VVF&ÂVâfW¢FRVRÖ–ÆÆ—2‚’Ö&÷&ä×26P¢òòF—7&RFRvöÇR’FöF26VâFRf6R‡’6R&V–æFW†Vâ’VâVÂÖ—6Öð¢òòg&ÖRÒÒW6òW&VÂ'FVòò'6RVVF'VvVFò"ÂföÇfW"FRVæà¢V–çC3%÷BW6VBÒÖ–ÆÆ—2‚’Òæ÷F–eW6UC°¢f÷"†–çB’Ò²’Âtæ÷F–d6÷VçC²’²²’tæ÷F–g5¶•Òæ&÷&ä×2³ÒW6VC°¢æ÷F–eW6VBÒfÇ6S°¢Ð¢–b†tæ÷F–d6÷VçBâ’æ÷F–d&æDöâÒG'VS° ¢òò&Ö"ÆVçG&FFRÆ2æ÷F–f–66–öæW2VâæòÖ÷7G&F0¢f÷"†–çB’Ò²’Âtæ÷F–d6÷VçC²’²²—°¢–b‚tæ÷F–g5¶•Òæ&ÖVB—°¢tæ÷F–g5¶•Òæ&ÖVBÒG'VS°¢tæ÷F–g5¶•Òç†6RÒåô”ã°¢tæ÷F–g5¶•Òæ&÷&ä×2ÒÖ–ÆÆ—2‚“°¢tæ÷F–g5¶•Òç6Æ–FU‚Òãc°¢Ð¢Ð ¢òòfç¦"f6W2FRæ–Ö6–öà¢f÷"†–çB’Ò²’Âtæ÷F–d6÷VçC²’²²—°¢æ÷F–f–6F–öâ¢âÒftæ÷F–g5¶•Ó°¢7v—F6‚†âÓç†6R—°¢66Råô”ã ¢–b†Ö–ÆÆ—2‚’ÒâÓæ&÷&ä×2ãÒ#ƒ’âÓç†6RÒåô”DÄS°¢'&V³°¢66Råô”DÄS ¢–b†Ö–ÆÆ—2‚’ÒâÓæ&÷&ä×2ãÒäõD”eô„ôÄEôÕ2’âÓç†6RÒåôõUC²òòWFòÖFW66'FRÆ÷2R0¢'&V³°¢66Råõ5$”äs ¢âÓç6Æ–FU‚³ÒƒãbÒâÓç6Æ–FU‚’¢ã3Vc²òò×VVÆÆRFRgVVÇF¢–b†âÓç6Æ–FU‚âÓãVb—²âÓç6Æ–FU‚Òãc²âÓç†6RÒåô”DÄS²Ð¢'&V³°¢66RåôõUC ¢âÓç6Æ–FU‚ÓÒ„äõD”eô4$Eõr²äõD”eôÔ$t”åõ‚’¢ã†b²bãc²òò6ÆR÷"Æ—§V–W&F¢–b†âÓç6Æ–FU‚ÂÒ„äõD”eô4$Eõr²äõD”eôÔ$t”åõ‚²B’—²æ÷F–e&VÖ÷fR†’“²’ÒÓ²6öçF–çVS²Ð¢'&V³°¢FVfVÇC¢'&V³°¢Ð¢Ð ¢òò&V6÷'FR6ö×ÆWFò‡÷"6’VæÆòFV¦òW7G&V6†ò’çFW2FR6ö×öæW ¢t6Æ—“Ò²t6Æ—“Ò45%ô‚Ò²t6Æ—ƒÒ²t6Æ—ƒÒ45%õrÒ° ¢òò6ö×öæW"Vâ&'Vb†æF–RÖ2ÆòÆVR“¢&W7FW&"föæFòÆ–×–ò’F–'V¦"Æ0¢òòF&¦WF2Væ6–ÖâæF–RÖ2&W6VçFW7F&æFÓâ6–â'FVòà¢6WD'Vb†&'Vb“°¢æ÷F–e&W7F÷&T&r‚“°¢f÷"†–çB’Ò²’Âtæ÷F–d6÷VçC²’²²—°¢–b‚tæ÷F–g5¶•Òæ7F—fR’6öçF–çVS°¢–çB6&E’ÒäõD”eõ“²’¢„äõD”eô4$Eô‚²äõD”eôt“°¢–b†tæ÷F–g5¶•Òç†6RÓÒåô”â—°¢fÆöBÒ†Ö–ÆÆ—2‚’Òtæ÷F–g5¶•Òæ&÷&ä×2’ò#ƒãc²–b‡âãb’Òãc°¢6&E’ÓÒ†–çB’‚ƒãbÒæ÷F–dV6T÷WB‡’’¢äõD”eôTåDU%ôE$õ“²òòVçG&F¢6RFW6FR'&–&¢Ð¢æ÷F–dG&t6&B‚ftæ÷F–g5¶•ÒÂ6&E’“°¢Ð¢òòföÆ6FòFöÖ–6ò&'VbÓæf"FRVæ&æF–FW&Ö–æFâVÂ&W6VçFW"çVæ6fP¢òòVâf"ÖVF–ò–çF"à¢&W6VçB„äõD”eô$äEõDõÂäõD”eô$äEô$õBÒ“° ¢òò&æFf6–F¢VÂg&ÖRFRÆ–×–W¦–6R6ö×W6ò’föÆ6ò'&–&à¢–b†tæ÷F–d6÷VçBÓÒ’æ÷F–d&æDöâÒfÇ6S°§Ð  ¢òò2222222222222222222222222222222222222222222222222222222222220¢òò22DUDT44”ôâDR„$Et$R“$2„d4R"¢òò22ÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÐ¢òò224ÄdRDR4TuU$”DC¢VÂW66æVò6÷'&RVâVÂÔ•4Ôò6öçFW‡Fð¢òò22VRfÆW…öÆÅF÷V6‚‚’ÖVÂÆö÷F6²Â6÷&RÒÆÆÖæFò¢òò22‡tFWFV7EF–6²‚’Vâ6FgVVÇFÂ’æò†’æ–æwVæ÷G&F&V¢òò22Fö6æFòv—&RÂ6’VRÆ2G&ç666–öæW2åTä46R6öÆâ¢òò22æò†6RfÇF×WFW‚â„W7FòW2&÷÷6—FòÆò6öçG&&–òFVÀ¢òò22Æâ÷&–v–æÂÂVRöæ–VæF&VFRW66æVò'FS¢W6ð¢òò226ö×'F–v—&R6–â&÷FV66–öâÓâ6÷''W6–öâFVÂ'W2ö7&6‚â¢òò20¢òò22TâTÂõ%BÂ32VÂ'W2VVFFVÖ24ôÕÄUDÔTåDRÄ”%$S¢VÀ¢òò22F7F–Â–æòW2“$2†VÂ…C#CbW25’’’Æ6Ö&W67P¢òò22&÷–ò444"Vâu”òBóRâW7FRv—&R„u”ò3‚ó3’’W2W†6ÇW6—fð¢òò22FRÆ÷2ÖöGVÆ÷2VR6öæV7FRVÂW7V&–òà¢òò20¢òò22FVÖ2VÂ&'&–FòW2”ä5$TÔTåDÃ¢6öæFV“$5õ44åõU%õD”4°¢òò22F—&V66–öæW2÷"gVVÇFÂ&æòæF—"ÆFVæ6–W&6WF–&ÆP¢òò22ÂF7F–Âæ’f÷'¦"VÂvF6†FörâÆ÷2F—7÷6—F—f÷2çVWf÷0¢òò22f—6â÷"Æ—6ÆF–æÖ–6FRÆf6R†æ÷F–eW6‚’à¢òò20¢òò22Ä4ä4R„ôäU5Dó¢6öÆò“$2ÂVRW2f–&ÆRâÆFWFV66–öâFP¢òò22ÖöGVÆ÷2÷"u”ò‡VÇ6F÷&W2Â„2Õ5#BÂ6W'f÷2’äò6R†6P¢òò22V’÷'VRæòW2F—7F–æwV–&ÆR6–âfÇ6÷2÷6—F—f÷3²W6÷0¢òò22ÆÆVv&â÷"6–væ6–öâÖçVÂFR–æW2VâVÂ6—7FVçFP¢òò22„f6R2’Âæò÷"WFòÖFWFV66–öâà¢òò2222222222222222222222222222222222222222222222222222222222220 ¢òòÖVVæF—&V66–öâ“$2VâF—òFRÖöGVÆò6öæö6–Fð§7FF–2ÖöGVÆUG—R–FVçF–g”“$4FWf–6R‡V–çC…÷BFG"—°¢7v—F6‚†FG"—°¢66Rƒsc¢66Rƒss¢&WGW&âÔôEô$ÔS#ƒ²òò$ÔS#ƒò$Õ#ƒ ¢66Rƒcƒ¢66Rƒc“¢&WGW&âÔôEôÕScS²òòÕScSòÕS“#S ¢FVfVÇC¢&WGW&âÔôEô“$5ôtTäU$”3°¢Ð§Ð ¢òò&VÆÆVææÖR÷7V"FW67&—F—f÷2FRVâÖöGVÆò“$0§7FF–2fö–B“&4FW67&–&R„FWFV7FVDÖöGVÆR¢Ò—°¢7v—F6‚†ÒÓçG—R—°¢66RÔôEô$ÔS#ƒ ¢6ç&–çFb†ÒÓææÖRÂ6—¦Vöb†ÒÓææÖR’Â%6Vç6÷"$ÔS#ƒ"“°¢6ç&–çFb†ÒÓç7V"Â6—¦Vöb†ÒÓç7V"’Â$“$2‚S%‚FWFV7FFò"ÂÒÓæ“&4FG"“°¢'&V³°¢66RÔôEôÕScS ¢6ç&–çFb†ÒÓææÖRÂ6—¦Vöb†ÒÓææÖR’Â$ÕScS"“°¢6ç&–çFb†ÒÓç7V"Â6—¦Vöb†ÒÓç7V"’Â$”ÕRÒ“$2‚S%‚"ÂÒÓæ“&4FG"“°¢'&V³°¢FVfVÇC ¢6ç&–çFb†ÒÓææÖRÂ6—¦Vöb†ÒÓææÖR’Â$F—7÷6—F—fò“$2"“°¢6ç&–çFb†ÒÓç7V"Â6—¦Vöb†ÒÓç7V"’Â#‚S%‚FWFV7FFò"ÂÒÓæ“&4FG"“°¢'&V³°¢Ð§Ð ¢òòF—&V66–öæW2VRäò6RçVæ6–â6öÖò&ÖöGVÆòçVWfò"âVâVÂ32VÂF7F–Â–æð¢òòW7FVâ“$2†VÂ…C#CbW25’’’Æ6Ö&F–VæR7R&÷–ò'W2444"Â6’VP¢òòW7FRf–ÇG&ò–æòFæF&VÃ²6R6öç6W'f÷"6ö×F–&–Æ–FB’÷'VP¢òò6–wVR6–VæFò6÷'&V7Fò6’ÆwV–VâVæ6‡VfVâæVÂ66—F—fòW7FR'W2à§7FF–2–æÆ–æR&ööÂ“&4—5F÷V6‚‡V–çC…÷BFG"—²&WGW&âFG"ÓÒwDFG"ÇÂFG"ÓÒƒTBÇÂFG"ÓÒƒBÇÂFG"ÓÒƒ3²Ð ¢òò–æF–6RFRVâÖöGVÆò÷"F—&V66–öâ†òÓ§7FF–2–çB“&4f–æD'”FG"‡V–çC…÷BFG"—°¢f÷"†–çB’Ò²’ÂFWFV7FVD6÷VçC²’²²¢–b†FWFV7FVDÖöGVÆW5¶•Òæ“&4FG"ÓÒFG"’&WGW&â“°¢&WGW&âÓ°§Ð ¢òòÖ&6&W6Væ6–FRVæF—&V66–öã²6’W2åTUdÆ&Vv—7G&’f—6÷"Æ—6Æ§7FF–2fö–B“&4öäFWf–6U&W6VçB‡V–çC…÷BFG"—°¢–b†“&4—5F÷V6‚†FG"’’&WGW&ã°¢–çB–G‚Ò“&4f–æD'”FG"†FG"“°¢–b†–G‚ãÒ—°¢ÖöE7vVW–E¶–G…ÒÒ“&57vVW–C²òò6–wVR&W6VçFRVâW7FR&'&–Fð¢–b‚FWFV7FVDÖöGVÆW5¶–G…Òæ7F—fR—²òò&V&V6–òG&2†&W'6RFW66öæV7FFð¢FWFV7FVDÖöGVÆW5¶–G…Òæ7F—fRÒG'VS°¢FWFV7FVDÖöGVÆW5¶–G…ÒæFWFV7FVDBÒÖ–ÆÆ—2‚“°¢æ÷F–eW6‚‚fFWFV7FVDÖöGVÆW5¶–G…Ò“°¢Ð¢&WGW&ã°¢Ð¢–b†FWFV7FVD6÷VçBãÒÔ…ôÔôETÄU5ôDUDT5DTB’&WGW&ã°¢FWFV7FVDÖöGVÆRÓ°¢ÖV×6WB‚fÒÂÂ6—¦Vöb†Ò’“°¢Òæ“&4FG"ÒFG#°¢ÒçG—RÒ–FVçF–g”“$4FWf–6R†FG"“°¢Òæ7F—fRÒG'VS°¢ÒæçVÕ–ç2Ò°¢ÒæFWFV7FVDBÒÖ–ÆÆ—2‚“°¢“&4FW67&–&R‚fÒ“°¢–çB6Æ÷BÒFWFV7FVD6÷VçB²³°¢FWFV7FVDÖöGVÆW5·6Æ÷EÒÒÓ°¢ÖöE7vVW–E·6Æ÷EÒÒ“&57vVW–C°¢æ÷F–eW6‚‚fFWFV7FVDÖöGVÆW5·6Æ÷EÒ“°§Ð ¢òò6–W'&Vâ&'&–Fò6ö×ÆWFó¢Æòæòf—7FòÓâ–æ7F—fò‡W&Ö—FR&RÖf—6òÂ&V6öæV7F"§7FF–2fö–B“&4VæE7vVW‚—°¢f÷"†–çB’Ò²’ÂFWFV7FVD6÷VçC²’²²¢–b†FWFV7FVDÖöGVÆW5¶•Òæ7F—fRbbÖöE7vVW–E¶•ÒÒ“&57vVW–B¢FWFV7FVDÖöGVÆW5¶•Òæ7F—fRÒfÇ6S°¢“&57vVW–B²³°¢“&4Æ7E7vVWÒÖ–ÆÆ—2‚“°§Ð ¢òòF–6²FRFWFV66–öâ“$2âÆÆÖ"VâÆö÷‚’VâVÂÖ—6Öò6öçFW‡FòVRfÆW…öÆÅF÷V6‚à§7FF–2fö–B‡tFWFV7EF–6²‚—°¢–b‚t“&4ö²’&WGW&ã²òò6–â“$2–æ–6–Æ—¦FòÂæF¢–b‚“&57vVW–ær—°¢–b†Ö–ÆÆ—2‚’Ò“&4Æ7E7vVWÂ“$5õ5tTUô”åDU%dÂ’&WGW&ã²òòW7W&VçG&R&'&–F÷0¢“&57vVW–ærÒG'VS°¢“&566ä7W'6÷"Ò“$5õ44åôÄó°¢Ð¢–çB&ö&W2Ò°¢v†–ÆR†“&57vVW–ærbb&ö&W2Â“$5õ44åõU%õD”4²—°¢V–çC…÷BFG"Ò“&566ä7W'6÷#°¢–b‚“&4—5F÷V6‚†FG"’—°¢v—&Ræ&Vv–åG&ç6Ö—76–öâ†FG"“°¢–b…v—&RæVæEG&ç6Ö—76–öâ‚’ÓÒ’“&4öäFWf–6U&W6VçB†FG"“²òò4²Óâ†’F—7÷6—F—fð¢Ð¢&ö&W2²³°¢–b†“&566ä7W'6÷"ãÒ“$5õ44åô„’—²“&57vVW–ærÒfÇ6S²“&4VæE7vVW‚“²Ð¢VÇ6R“&566ä7W'6÷"²³°¢Ð§Ð  ¢òò2222222222222222222222222222222222222222222222222222222222220¢òò224Ä”%$4”ôâDTÂD5D”Â†ÖFVÖF–6FR&GTõ2c2ã3¢òò22ÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÐ¢òò22VâæVÂ$U4•5D•dòæòf–VæR6Æ–'&FòFRf'&–6ÂÂ&WfW0¢òò22VRVÂuC“66—F—fòFVÂB‡VRF&6ö÷&FVæF0¢òò22'6öÇWF2Æ—7F2&W6"’â÷"W6ò&GTõ2–æ6ÇV–Væ¢òò226Æ–'&6–öâ–âÖFWf–6R’÷"W6òf–VæR6öâVÂG&ç7ÆçFS ¢òò226–âVÆÆVÂF7F–Âæò6RFöæFRFö62à¢òò20¢òò22ÄÔDTÔD”4U2ÄDR&GTõ2ÂÄ•DU$Ã¢7VG&ò7'V6W26öà¢òò22Ö&vVâ6öæö6–Fò&W7V7FòÆ2W7V–æ2Â6GW&FRÆ÷0¢òò22fÆ÷&W2$r†æòFRÆ÷2ÖVF÷2Â&VRgVæ6–öæRVçVRÆ¢òò226Æ–'&6–öâ&Wf–W7FR6ö×ÆWFÖVçFR&÷F’Â&öÖVF–FòFP¢òò22Æ÷2&W2—§V–W&FöFW&V6†’'&–&ö&¦òÂW‡G&öÆ6–öâ¢òò22Æ÷2&÷&FW2’wV&FFòVâÆ2Ö—6Ö26ÆfW2FRåe2à¢òò20¢òò22dÅT¤ò4ôÕÄUDó ¢òò22B7'V6W2Óâ6R6Æ7VÆÓâ4RuT$DÓâ'VV&f—7VÀ¢òò22‡6VÖf÷&ò¢òò22Óâ6öç6W'f"ò&WWF— ¢òò20¢òò22õ"TR4RuT$DåDU2DRÄ%TT$¢÷'VRÆ'VV&W6Æ¢òò226Æ–'&6–öâçVWfFRfW&FBâwV&F&Æ&–ÖW&ò†6RVRÆòVP¢òò22VÂW7V&–òfRVâÆçFÆÆFR&W7VÇFFò6VU„5DÔTåDRÆð¢òò22VRfFVæW"FW7VW2ÒÒæòVæ6–×VÆ6–öââ6’FV6–FP¢òò22&WWF—"ÂÆ6–wV–VçFR&öæF6–×ÆVÖVçFRÆ6ö'&VW67&–&Rà¢òò20¢òò22TRU2åTUdòTâU5DdU%4”ôâ‡’÷"VR“ ¢òò20¢òò22’”äò$ÄõTTâçFW2W&VægVæ6–öâ6–æ7&öæ6öâ'V6ÆW0¢òò22v†–ÆR‚’FVçG&òÂ–çfö6&ÆR6öÆòFW6FR6WGW‚’âÖ–VçG&0¢òò226÷'&–ÂVÂ6—7FVÖVçFW&òW7F&6öævVÆFó¢æò†&–¢òò22f÷&ÖFRÆÆÖ&ÆVâ6Æ–VçFRFW6FR§W7FW2â†÷&W2Và¢òò22U5DDòÖ2FVÂ6—7FVÖ…5EõDõT4„4Â’6öâ7RF6ÅF–6²‚¢òò22ÆÆÖFòFW6FRÆö÷‚’Â–wVÂVR5Eô´%4UBò5Eõt”d’à¢òò20¢òò22"’Ô$tTäU24ôâäôÔ%$R’Ô2UTTäõ2âçFW2Æ27'V6W0¢òò22W7F&â3‚Vâ‚’c‚Vâ’FRÆ÷2&÷&FW2Â6öâW6÷0¢òò22çVÖW&÷2&WWF–F÷2ÖæòVâG&W26—F–÷2â†÷&6öâDõ0¢òò226öç7FçFW26öâæöÖ'&RÂÖ2WVVæ2Â’Æ2Ô•4Ô26RW6à¢òò22&F–'V¦"’&W‡G&öÆ"à¢òò20¢òò222’$U5ÄDò’$U5DU$4”ôââÂVçG&"6RwV&FÆ¢òò226Æ–'&6–öâf–vVçFRVâ$Òâ6’VÂW7V&–ò6æ6VÆÂ6P¢òò22&W7FW&ÒÒ–æ6ÇV–FÆåe2Â÷'VRW62ÇGW&2Æ÷0¢òò22fÆ÷&W2çVWf÷2–VVFVâW7F"W67&—F÷2à¢òò20¢òò22B’%TT$d•5TÂ4ôâ4TÔdõ$òÂäòTâU„ÔTââfW"VÂ&Æ÷VP¢òò22FR&¦ó¢W2VÂ6Ö&–òÖ2–×÷'FçFRFR6&ÂW7V&–òà¢òò2222222222222222222222222222222222222222222222222222222222220 ¢òòÒÒÒÒÖ&vVæW2FRÆ27'V6W2†Vâ—†VÆW2d•4”4õ2FVÂæVÂ’ÒÒÒÐ¢òò6öâVÂ6ö×&öÖ—6òVçG&RF÷26÷62÷VW7F3 ¢òò+r7VçFòÔ2UTTäõ2ÂÖV¦÷#¢Æ÷2VçF÷2ÖVF–F÷2VVFâÖ26W&6¢òòFRÆ÷2&÷&FW2&VÆW2Â†’VRW‡G&öÆ"ÖVæ÷2’Æ6Æ–'&6–öà¢òò6ÆRÖ2&V6—6âFVÖ2Æ7'W¢'&V6R"ÆW7V–æà¢òò+ræòVVFVâ6W"4U$ó¢Æ7'W¢Ö–FR²Ó#‚Æöv–6÷2FR'&¦òÖ2Và¢òò6—&7VÆòFR&F–òÂ’6’6R6ÆRFVÂæVÂVÂW7V&–òæòVVFP¢òòVçF"7R6VçG&ò†VçF&–Væ7'W¢&V6÷'FFÂ’W6R6W6vð¢òò6RG&6ÆFVçFW&òÆ6Æ–'&6–öâ’à¢òòVâ'&¦òFR#‚Äôt”4õ26öâ#£"ó2Ò2‚d•4”4õ2VâVÂÖöFð¢òòU5D•$"â6öâb‚FRÖ&vVâÆ7'W¢VçG&6ö×ÆWF’6ö'&‚à¢6FVf–æRE5ô4ÅôÔ$t”åõ‚`¢6FVf–æRE5ô4ÅôÔ$t”åõ’` ¢òò2222222222222222222222222222222222222222222222222222222222220¢òò22%TT$DR$T4•4”ôâ+r4TÔdõ$òõ"D•5Dä4”¢òò22ÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÐ¢òò22TÂ$ô$ÄTÔTR$U5TTÅdS¢ÆfW'6–öâçFW&–÷"†6–Và¢òò22U„ÔTâFR&ö&Fò÷7W7Vç6òâ6ö×&&VÂW'&÷"Vâ‚’Vâ¢òò22÷"6W&Fò6öçG&VæFöÆW&æ6–Væ–6’Â6’5TÅT”U$FP¢òò22Æ÷2F÷26R6&VçVRgVW&÷"Vâ—†VÂÂF—&&Æ¢òò226Æ–'&6–öâVçFW&’ö&Æ–v&&WWF—"Æ2B7'V6W2â6öâVà¢òò22æVÂ&W6—7F—fò’VâFVFò‡VÖæòW6òW266’–×÷6–&ÆRFP¢òò227WW&#¢æF–R6–W'FÂ—†VÂÂ’FVÖ2Æ–VÖFVÀ¢òò22VçFò§W7Fò7VæFò†’VRVçF&ÆòâVÂW7V&–òVVF&¢òò22G&FòVâVâ'V6ÆR6–âVçFVæFW"÷"VRà¢òò20¢òò22ÄòTR„4R„õ$¢VâVæ–6òVçFòVâVÂ6VçG&òÂ6RÖ–FRÆ¢òò22D•5Dä4”$TÂ†WV6Æ–FVÂæò%‚R’÷"6W&Fò"’’6P¢òò226Æ6–f–6VâG&W2G&Ö÷2âÆ6Æ–'&6–öâ–W7FwV&FFÂ6¢òò22VRVÂ&W7VÇFFòW2–æf÷&ÖF—fó¢VÂW7V&–òDT4”DRà¢òò20¢òò22õ"TRD•5Dä4”UT4Ä”DT’äòÆG‡Â’ÆG—Âõ"4U$Dó ¢òò226öâVÖ'&ÆW2÷"V¦RÂVâW'&÷"FR#‚Vâ‚’#Vâ’6P¢òò226WF&ÂW&òVæòFR#RVâ‚’Vâ’6R&V6†¦&ÒÒ’VÀ¢òò226VwVæFòW2ÔT¤õ"ƒ#‚‚FRFW7f–6–öâ&VÂg&VçFR#R’âVÀ¢òò227&—FW&–ò÷"V¦W267F–vW'&÷&W2VRVâÆ&7F–66Ræ÷Fà¢òò22ÖVæ÷2âÆF—7Fæ6–Ö–FRÆòVRFRfW&FB–×÷'F¢7VçFò6P¢òò22ÆV¦VÂFVFòFVÂ6—F–òà¢òò20¢òò22Äõ2E$U2E$Ôõ2†Vâ—†VÆW2Äôt”4õ3²Ö&÷2V¦W2FVÂæVÂ6P¢òò22&W6VçFâ&÷†–ÖFÖVçFR2óRÂ6’VR‚f—6–6òâÃcrÆöv–6÷2“ ¢òò22dU$DRÃÒ#Æöv–6÷2‡ã2f—6–6÷2Âã"ÖÒ’W†6VÆVçFP¢òò22Ô$”ÄÄòÃÒCRÆöv–6÷2‡ã3f—6–6÷2ÂãBÖÒ’6WF&ÆP¢òò22$ô¤òâCRö6ò&V6—6¢òò20¢òò22VÂG&ÖòÔ$”ÄÄòW2FVÆ–&W&FÖVçFRæ6†òâBÖÒFP¢òò22FW7f–6–öâ7VVæâ×V6†òW67&—F÷2V’ÂW&òVâÆ&7F–6¢òò22æòÖöÆW7Fã¢Æ÷2&÷FöæW2Ö2WVVæ÷2FVÂ6—7FVÖ†Æ÷2–6öæ÷0¢òò22FVÂ†öÖR’F–VæVâ¦öæ2F7F–ÆW2FRCGƒCB‚Æöv–6÷2Â6’VP¢òò226öâCR‚FRW'&÷"VÂV÷"66ò6–wVR6–VæFòFVçG&òFVÀ¢òò22&÷FöâVR6RVçFâ÷"W6òVÂÖ&–ÆÆò6R&W6VçF6öÖð¢òò22'VVFW26VwV—""’æò6öÖòVæGfW'FVæ6–à¢òò2222222222222222222222222222222222222222222222222222222222220¢6FVf–æRE5õDU5Eôu$TTâ#òòÃÒW7Fó¢W†6VÆVçFP¢6FVf–æRE5õDU5Eõ”TÄÄõrCRòòÃÒW7Fó¢6WF&ÆS²÷"Væ6–ÖÂö6ò&V6—6 ¢òòÒÒÒÒW7FFòFRÆçFÆÆ†æò&Æ÷VVçFR’ÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÐ¦VçVÒ²D4Åô5$õ52ÒÂD4ÅõDU5BÂD4Åõ$U5TÅBÓ°¦VçVÒ²D4Åôu$DUôu$TTâÒÂD4Åôu$DUõ”TÄÄõrÂD4Åôu$DUõ$TBÓ°§7FF–2–çBF6Å†6RÒD4Åô5$õ53°§7FF–2–çBF6Å7FWÒ²òò7'W¢7GVÂƒâã2§7FF–2&ööÂF6Äg&öÕ6WGF–æw2ÒfÇ6S²òòG'VRÓâÂ6Æ—"6RgVVÇfR§W7FW0§7FF–2&ööÂF6Åv—DÆ–gBÒG'VS²òò†’VRW7W&"VR6RÆWfçFRVÂFVFð§7FF–2V–çC3%÷BF6Å†6T×2Ò°§7FF–2V–çC…÷BF6Å'VâÒ²òòÆV7GW&2fÆ–F26öç6V7WF—f2FVÂVçFò7GVÀ§7FF–2V–çC3%÷BF6Ä65‚ÒÂF6Ä65’Ò°§7FF–2V–çCe÷BF6Å&u…³EÒÂF6Å&u•³EÓ°§7FF–2–çBF6ÅG&–W2Ò²òò&öæF2FRB7'V6W2–†V6†0§7FF–2&ööÂF6Å6fVBÒfÇ6S²òòÆ÷2fÆ÷&W2çVWf÷2–W7FâVâåe0§7FF–2&ööÂF6Ä†E&WbÒfÇ6S²òò†&–Væ6Æ–'&6–öâçFW2FRVçG& ¢òò&W7VÇFFòFRÆ'VV&§7FF–2–çBF6Äw&FRÒD4Åôu$DUôu$TTã°§7FF–2–çBF6ÄF—7BÒ²òòFW7f–6–öâÖVF–FÂVâ‚Æöv–6÷0§7FF–2–çBF6Ä†—E‚ÒÂF6Ä†—E’Ò²òòFöæFR6–òVÂF÷VR†Æöv–62§7FF–2–çBF6ÄW‡‚ÒÂF6ÄW‡’Ò²òòFöæFRW7F&VÂVçFò†Æöv–62¢òò&W7ÆFòFRÆ6Æ–'&6–öâf–vVçFRÂTåE$"‡&öFW"6æ6VÆ"§7FF–2–çCe÷BF6Ä&µ„Ô”âÂF6Ä&µ„Ô‚ÂF6Ä&µ”Ô”âÂF6Ä&µ”Ôƒ° ¢òòÆV7GW&26öç6V7WF—f2VR6öæf—&ÖâVâVçFòFR6Æ–'&6–öââÖ2ÇFð¢òòVRVÂE5ôDT$õTä4Uõ$TE2æ÷&ÖÂ&÷÷6—Fó¢V’Vâ6öÆòVçFòÖÀ¢òò6GW&FòW7G&÷VÆ6Æ–'&6–öâVçFW&Â6’VR6ö×Vç6W†–v—"Ö2à¢6FVf–æRD4Åô4ôäd•$Õõ$TE2P §7FF–26öç7B–çCe÷BE5ô4Åõ…³EÒÒ²E5ô4ÅôÔ$t”åõ‚Â…õrÒE5ô4ÅôÔ$t”åõ‚À¢E5ô4ÅôÔ$t”åõ‚Â…õrÒE5ô4ÅôÔ$t”åõ‚Ó°§7FF–26öç7B–çCe÷BE5ô4Åõ•³EÒÒ²E5ô4ÅôÔ$t”åõ’ÂE5ô4ÅôÔ$t”åõ’À¢…ô‚ÒE5ô4ÅôÔ$t”åõ’Â…ô‚ÒE5ô4ÅôÔ$t”åõ’Ó° ¢òò6öçf–W'FRVæ6ö÷&FVæFd•4”4FVÂæVÂÆÄôt”4WV—fÆVçFRÂ6öà¢òòÆÔ•4Ô&¦öâVçFW&VRW6VÂ&W6VçFW"&F–'V¦"â6’VâVçFð¢òòF–'V¦FòV’&V6RW†7FÖVçFR6ö'&RVÂ—†VÂf—6–6òVR6RV–W&P¢òòÖVF—"Â’Æ6ö×&6–öâ6öâVÂF÷VRW2§W7Fà§7FF–2–æÆ–æR–çBF6Å‡—5FôÆöu‚†–çBg‚—²&WGW&â†–çB’‚‚†–çCcE÷B’†g‚Ò…õƒ’¢…45%õrÒ’²……ô5rÒ’ò"’ò……ô5rÒ’“²Ð§7FF–2–æÆ–æR–çBF6Å‡—5FôÆöu’†–çBg’—²&WGW&â†g’¢…õ5•ôåTÒ’ò…õ5•ôDTã²Ð §7FF–2fö–BF6Ä7&÷72†–çBg‚Â–çBg’ÂV–çCe÷B6öÂ—°¢–çBÇ‚ÒF6Å‡—5FôÆöu‚†g‚’ÂÇ’ÒF6Å‡—5FôÆöu’†g’“°¢–b†Ç‚Â’Ç‚Ò²–b†Ç‚â45%õrÒ’Ç‚Ò45%õrÒ°¢–b†Ç’Â’Ç’Ò²–b†Ç’â45%ô‚Ò’Ç’Ò45%ô‚Ò°¢f÷"†–çB’ÒÓ#²’ÃÒ#²’²²—°¢–çB‚ÒÇ‚²’Â’ÒÇ’²“°¢–b‡‚ãÒbb‚Â45%õr’f%²‡6—¦U÷B–Ç’¢45%õr²…ÒÒ6öÃ°¢–b‡’ãÒbb’Â45%ô‚’f%²‡6—¦U÷B—’¢45%õr²Ç…ÒÒ6öÃ°¢Ð¢G&t6—&6ÆR†Ç‚ÂÇ’ÂÂ&v#ScRƒ#SRÃ##RÃc’“°§Ð ¢òòF–æFRÆ'VV&¢Vâ6öÆòVçFòÂw&æFR’6öâæ–ÆÆ÷26öæ6VçG&–6÷0¢òò&VR6RfV&–VâFöæFRW7FVÂ6VçG&òW†7FòVçVRVÂFVFòÆòFRà§7FF–2fö–BF6ÅF&vWB†–çBÇ‚Â–çBÇ’ÂV–çCe÷B6öÂ—°¢G&t6—&6ÆR†Ç‚ÂÇ’Â#bÂ6öÂ“°¢G&t6—&6ÆR†Ç‚ÂÇ’Â#RÂ6öÂ“°¢G&t6—&6ÆR†Ç‚ÂÇ’ÂBÂ6öÂ“°¢f–ÆÄ6—&6ÆR†Ç‚ÂÇ’ÂBÂ6öÂ“°¢f÷"†–çB’ÒÓ3C²’ÃÒ3C²’²²—°¢–b†'2†’’Â‚’6öçF–çVS²òò‡VV6ò6VçG&Ã¢æòFÆF–æ¢–çB‚ÒÇ‚²’Â’ÒÇ’²“°¢–b‡‚ãÒbb‚Â45%õr’f%²‡6—¦U÷B–Ç’¢45%õr²…ÒÒ6öÃ°¢–b‡’ãÒbb’Â45%ô‚’f%²‡6—¦U÷B—’¢45%õr²Ç…ÒÒ6öÃ°¢Ð§Ð ¢òòÒÒÒÒ&÷FöæW2ÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÐ¢òòVâ5%T4U2’%TT$¢Vâ6öÆò&÷Föâ$6æ6VÆ""‡6’fVæ–Ö÷2FR§W7FW2’à¢òòVâ$U5TÅDDó¢F÷2&÷FöæW2Â%&WWF—""’$6öç6W'f""à¢6FVf–æRD4Åô4ä4TÅõ‚…45%õrò"Ò“¢6FVf–æRD4Åô4ä4TÅõ’…45%ô‚ò"²S¢6FVf–æRD4Åô4ä4TÅõrƒ ¢6FVf–æRD4Åô4ä4TÅô‚C` ¢6FVf–æRD4Åô%Dåõ’…45%ô‚Ò‚¢6FVf–æRD4Åô%Dåô‚S€¢6FVf–æRD4Åô%Dåõr# ¢6FVf–æRD4Åô%DåôÅõ‚#€¢6FVf–æRD4Åô%Dåõ%õ‚…45%õrÒ#‚ÒD4Åô%Dåõr §7FF–2fö–BF6Ä'WGFöâ†–çB‚Â–çB’Â–çBrÂ–çB‚Â6öç7B6†"¢Æ&VÂÂV–çCe÷B&rÂV–çCe÷BfrÂ&ööÂ&–Ö'’—°¢f–ÆÅ&÷VæE&V7B‡‚Â’ÂrÂ‚ÂBÂ&r“°¢–b‡&–Ö'’—²òò&÷&FR6Æ&òÒ66–öâ&V6öÖVæFF¢G&u&÷VæE&V7B‡‚Â’ÂrÂ‚ÂBÂ&v#ScRƒ#SRÃ#SRÃ#SR’“°¢G&u&÷VæE&V7B‡‚²Â’²ÂrÒ"Â‚Ò"Â2Â&v#ScRƒ#SRÃ#SRÃ#SR’“°¢Ð¢G&uFW‡D2‡‚²rò"Â’²‚ò"ÒrÂÆ&VÂÂ"Âfr“°§Ð ¢òò6öÆ÷&W2FVÂ6VÖf÷&ð§7FF–2–æÆ–æRV–çCe÷BF6Äw&FT6öÆ÷"†–çBr—°¢&WGW&ârÓÒD4Åôu$DUôu$TTâò&v#ScRƒcÃ#RÃ#¢¢rÓÒD4Åôu$DUõ”TÄÄõrò&v#ScRƒ#CÃ“Ãc¢¢&v#ScRƒ#CÃ“RÃƒ“°§Ð ¢òò–ç6–væ–6—&7VÆ"6öâVÂ6–Ö&öÆòFVÂ&W7VÇFFòÂF–'V¦FfV7F÷&–ÆÖVçFP¢òò†æFFRgVVçFW2FR–6öæ÷2“¢6†V6²ÂFÖ—&6–öâò7à§7FF–2fö–BF6Ä&FvR†–çB7‚Â–çB7’Â–çBr—°¢V–çCe÷B2ÒF6Äw&FT6öÆ÷"†r“°¢f–ÆÄ6—&6ÆR†7‚Â7’ÂCBÂ2“°¢f–ÆÄ6—&6ÆR†7‚Â7’Â3rÂ&v#ScRƒ"ÃBÃ#’“°¢–b†rÓÒD4Åôu$DUôu$TTâ—°¢7G&ö¶U6Vt†7‚ÒrÂ7’²Â7‚ÒRÂ7’²BÂBãbÂ2“²òò6†V6°¢7G&ö¶U6Vt†7‚ÒRÂ7’²BÂ7‚²‚Â7’Ò2ÂBãbÂ2“°¢ÒVÇ6R–b†rÓÒD4Åôu$DUõ”TÄÄõr—°¢7G&ö¶U6Vt†7‚Â7’Ò‚Â7‚Â7’²bÂBãVbÂ2“²òòFÖ—&6–öà¢f–ÆÄ6—&6ÆR†7‚Â7’²bÂ2Â2“°¢ÒVÇ6R°¢7G&ö¶U6Vt†7‚Ò2Â7’Ò2Â7‚²2Â7’²2ÂBãbÂ2“²òò7¢7G&ö¶U6Vt†7‚²2Â7’Ò2Â7‚Ò2Â7’²2ÂBãbÂ2“°¢Ð§Ð ¢òòF–w&Ö£FRÆFW7f–6–öã¢Vâ&V7VG&ò6öâVÂVçFòU5U$DòVâ7P¢òò6VçG&ò’VÂVçFòDUDT5DDòFW7Æ¦FòW†7FÖVçFRÆòVR6RFW7f–òVÀ¢òòFVFòâ6–â×Æ–"æ’ÖV–ÆÆ"ÒÒ6’Æ÷2F÷26—&7VÆ÷26R6öÆâW2VP¢òòÆ6Æ–'&6–öâW2'VVæÂ’W6ò6RVçF–VæFR6–âÆVW"æ–æwVâçVÖW&òà¢6FVf–æRD4ÅôD”uõr# ¢6FVf–æRD4ÅôD”uô‚S §7FF–2fö–BF6ÄF–w&Ò†–çB7‚Â–çB7’—°¢–çBƒÒ7‚ÒD4ÅôD”uõrò"Â“Ò7’ÒD4ÅôD”uô‚ò#°¢f–ÆÅ&÷VæE&V7B‡ƒÂ“ÂD4ÅôD”uõrÂD4ÅôD”uô‚Â"Â&v#ScRƒ#"Ã#RÃ3B’“°¢G&u&÷VæE&V7B‡ƒÂ“ÂD4ÅôD”uõrÂD4ÅôD”uô‚Â"Â&v#ScRƒS"ÃS‚ÃsB’“°¢òòVçFòW7W&Fò†æ–ÆÆò&Ææ6ò6öâ7'W¢f–æ¢G&t6—&6ÆR†7‚Â7’Â2Â&v#ScRƒ#RÃ##Ã#3R’“°¢„Æ–æR†7‚Ò#Â7’ÂCÂ&v#ScRƒ“Ã“bÃ‚’“°¢dÆ–æR†7‚Â7’Ò#ÂCÂ&v#ScRƒ“Ã“bÃ‚’“°¢òòVçFòFWFV7FFòÂFW7Æ¦FòÆòVRÖ&6òVÂFVFò‡&V6÷'FFòÂ&V7VG&ò¢–çBG‚ÒF6Ä†—E‚ÒF6ÄW‡‚ÂG’ÒF6Ä†—E’ÒF6ÄW‡“°¢–çBÆ–ÒÒD4ÅôD”uõrò"ÒbÂÆ–×’ÒD4ÅôD”uô‚ò"Òc°¢–b†G‚âÆ–Ò’G‚ÒÆ–Ó²–b†G‚ÂÖÆ–Ò’G‚ÒÖÆ–Ó°¢–b†G’âÆ–×’’G’ÒÆ–×“²–b†G’ÂÖÆ–×’’G’ÒÖÆ–×“°¢V–çCe÷B2ÒF6Äw&FT6öÆ÷"‡F6Äw&FR“°¢–b†'2†G‚’â"ÇÂ'2†G’’â"’7G&ö¶U6Vt†7‚Â7’Â7‚²G‚Â7’²G’Â"ãbÂ2“°¢f–ÆÄ6—&6ÆR†7‚²G‚Â7’²G’ÂrÂ2“°§Ð §7FF–2fö–BF6Å&VæFW"‚—°¢–b‚f"’&WGW&ã°¢6WD'Vb†f"“°¢t6Æ—“Ò²t6Æ—“Ò45%ô‚Ò²t6Æ—ƒÒ²t6Æ—ƒÒ45%õrÒ°¢f–ÆÅ&V7BƒÂÂ45%õrÂ45%ô‚Â&v#ScRƒÃÃ’“°¢6†"Õ³s%Ó° ¢–b‡F6Å†6RÓÒD4Åô5$õ52—°¢G&uFW‡D2…45%õrò"Â45%ô‚ò"ÒÂ$6Æ–'&6•Ç„35Ç„#6âEÇ„35Ç„"&7F–Â"Â2Â&v#ScRƒ#3RÃ#3‚Ã#CR’“°¢6ç&–çFb†ÒÂ6—¦Vöb†Ò’Â%Fö6VÂVçFòVBFRB"ÂF6Å7FW²“°¢G&uFW‡D2…45%õrò"Â45%ô‚ò"ÒS"ÂÒÂ"Â&v#ScRƒ#Ã#bÃ##’“°¢G&uFW‡D2…45%õrò"Â45%ô‚ò"Ò#"Â$§W7FòVâVÂ6VçG&òFRÆ7'W¢"ÂÂ&v#ScRƒCÃSÃs’“°¢–b‡F6ÅG&–W2â—°¢6ç&–çFb†ÒÂ6—¦Vöb†Ò’Â$–çFVçFòVB"ÂF6ÅG&–W2²“°¢G&uFW‡D2…45%õrò"Â45%ô‚ò"²"ÂÒÂÂ&v#ScRƒSÃcÃƒ’“°¢Ð¢f÷"†–çB’Ò²’ÂC²’²²¢f–ÆÅ&V7B…45%õrò"ÒCb²’¢#BÂ45%ô‚ò"²3BÂbÂbÀ¢’ÂF6Å7FWò&v#ScRƒcÃ#Ã’¢&v#ScRƒcÃcBÃs‚’“°¢F6Ä7&÷72…E5ô4Åõ…·F6Å7FWÒÂE5ô4Åõ•·F6Å7FWÒÂ&v#ScRƒ#CÃsÃs’“° ¢ÒVÇ6R–b‡F6Å†6RÓÒD4ÅõDU5B—°¢G&uFW‡D2…45%õrò"ÂSÂ%fÖ÷2&ö&&Æ"Â2Â&v#ScRƒ#3RÃ#3‚Ã#CR’“°¢G&uFW‡D2…45%õrò"Â“bÂ%Fö6VÂ6VçG&òFRÆF–æ"Â"Â&v#ScRƒ#Ã#bÃ##’“°¢G&uFW‡D2…45%õrò"Â#3"Â$6öâÆ–VÖÂ6öÖòW62VÂ&Fòæ÷&ÖÆÖVçFR"ÂÂ&v#ScRƒCÃSÃs’“°¢F6ÅF&vWB‡F6ÄW‡‚ÂF6ÄW‡’Â&v#ScRƒ“Ã“Ã#CR’“°¢G&uFW‡D2…45%õrò"Â45%ô‚Ò“Â%GR6Æ–'&6•Ç„35Ç„#6â–W7EÇ„35Ç„wV&FF"ÂÂ&v#ScRƒ#Ã#‚ÃC‚’“° ¢ÒVÇ6R²òòD4Åõ$U5TÅ@¢V–çCe÷B2ÒF6Äw&FT6öÆ÷"‡F6Äw&FR“°¢G&uFW‡D2…45%õrò"Â“"Â%&W7VÇFFò"Â"Â&v#ScRƒSÃS‚Ãs‚’“°¢F6Ä&FvR…45%õrò"Â“ÂF6Äw&FR“°¢6öç7B6†"¢F—FÆRÒF6Äw&FRÓÒD4Åôu$DUôu$TTâò$W†6VÆVçFR ¢¢F6Äw&FRÓÒD4Åôu$DUõ”TÄÄõrò$6WF&ÆR ¢¢%ö6ò&V6—6#°¢G&uFW‡D2…45%õrò"Â#SbÂF—FÆRÂBÂ2“°¢6ç&–çFb†ÒÂ6—¦Vöb†Ò’Â$FW7f–6•Ç„35Ç„#6ã¢VB‚"ÂF6ÄF—7B“°¢G&uFW‡D2…45%õrò"Â3"ÂÒÂ"Â&v#ScRƒ#Ã#bÃ##’“° ¢–b‡F6Äw&FRÓÒD4Åôu$DUôu$TTâ—°¢G&uFW‡D2…45%õrò"Â3C‚Â$VÂEÇ„35Ç„"&7F–Â&W7öæFR§W7FòFöæFRFö62â"ÂÂ&v#ScRƒSÃS‚Ãs‚’“°¢G&uFW‡D2…45%õrò"Â3cbÂ%VVFW2W6&Æ6–â&ö&ÆVÖ2â"ÂÂ&v#ScRƒSÃS‚Ãs‚’“°¢ÒVÇ6R–b‡F6Äw&FRÓÒD4Åôu$DUõ”TÄÄõr—°¢G&uFW‡D2…45%õrò"Â3C‚Â$†’VæWVUÇ„35Ç„#"&FW7f–6•Ç„35Ç„#6âÂW&òFöFò"ÂÂ&v#ScRƒSÃS‚Ãs‚’“°¢G&uFW‡D2…45%õrò"Â3cbÂ'6R6–wVRFö6æFò&–VââEÇ„35Ç„$FV6–FW2â"ÂÂ&v#ScRƒSÃS‚Ãs‚’“°¢ÒVÇ6R°¢G&uFW‡D2…45%õrò"Â3C‚Â$ÆFW7f–6•Ç„35Ç„#6âW2w&æFR’6Ræ÷F%Ç„35Ç„"ÂÂ&v#ScRƒ#3ÃSÃC’“°¢G&uFW‡D2…45%õrò"Â3cbÂ&ÂW6&ÆòâÖV¦÷"&W—FRÆ6Æ–'&6•Ç„35Ç„#6ââ"ÂÂ&v#ScRƒ#3ÃSÃC’“°¢Ð¢F6ÄF–w&Ò…45%õrò"ÂCƒ“°¢G&uFW‡D2…45%õrò"ÂScbÂ&æ–ÆÆò&Ææ6òÒÆF–æ+rVçFòFR6öÆ÷"ÒGRF÷VR"ÂÂ&v#ScRƒÃ‚Ã3‚’“° ¢òòVÂ&÷Föâ$T4ôÔTäDDò6Ö&–6VwVâVÂ&W7VÇFFó¢VâfW&FR’Ö&–ÆÆð¢òòÆò6Vç6FòW26VwV—#²Vâ&ö¦òÂ&WWF—"âÆ÷2F÷26–wVVâF—7öæ–&ÆW0¢òò6–V×&RÒÒVÂW7V&–òÖæFÂVÂ6—7FVÖ6öÆò6öç6V¦à¢&ööÂ¶VW—5&–Ö'’Ò‡F6Äw&FRÒD4Åôu$DUõ$TB“°¢F6Ä'WGFöâ…D4Åô%DåôÅõ‚ÂD4Åô%Dåõ’ÂD4Åô%DåõrÂD4Åô%Dåô‚Â%&WWF—""À¢¶VW—5&–Ö'’ò&v#ScRƒCBÃC‚Ãc’¢&v#ScRƒ“bÃs"Ãc’À¢&v#ScRƒ#3‚Ã#CÃ#C‚’Â¶VW—5&–Ö'’“°¢F6Ä'WGFöâ…D4Åô%Dåõ%õ‚ÂD4Åô%Dåõ’ÂD4Åô%DåõrÂD4Åô%Dåô‚Â$6öç6W'f""À¢¶VW—5&–Ö'’ò&v#ScRƒCbÃ3"Ãƒb’¢&v#ScRƒCBÃC‚Ãc’À¢&v#ScRƒ#3‚Ã#CÃ#C‚’Â¶VW—5&–Ö'’“°¢Ð ¢–b‡F6Äg&öÕ6WGF–æw2bb‡F6Å†6RÓÒD4Åô5$õ52ÇÂF6Å†6RÓÒD4ÅõDU5B’—°¢f–ÆÅ&÷VæE&V7B…D4Åô4ä4TÅõ‚ÂD4Åô4ä4TÅõ’ÂD4Åô4ä4TÅõrÂD4Åô4ä4TÅô‚Â"Â&v#ScRƒC‚ÃS"ÃcB’“°¢G&uFW‡D2…45%õrò"ÂD4Åô4ä4TÅõ’²RÂ$6æ6VÆ""Â"Â&v#ScRƒ##RÃ##‚Ã#3‚’“°¢Ð¢fÇ„fÇW6„ÆÂ‚“°§Ð ¢òòVçG&FÂW7FFòâg&öÕ6WGF–æw2FV6–FR6’†’&÷FöâFR6æ6VÆ"¢òòFöæFR6RgVVÇfRÂFW&Ö–æ"à§7FF–2fö–BF6ÄVçFW"†&ööÂg&öÕ6WGF–æw2—°¢F6Äg&öÕ6WGF–æw2Òg&öÕ6WGF–æw3°¢F6Å†6RÒD4Åô5$õ53°¢F6Å7FWÒ°¢F6ÅG&–W2Ò°¢F6Å'VâÒ°¢F6Å6fVBÒfÇ6S°¢F6Ä†E&WbÒG46Æ–$FöæR‚“°¢F6Åv—DÆ–gBÒG'VS°¢F6Å†6T×2ÒÖ–ÆÆ—2‚“°¢F6ÄW‡‚ÒF6Å‡—5FôÆöu‚……õrò"“°¢F6ÄW‡’ÒF6Å‡—5FôÆöu’……ô‚ò"“°¢òò$U5ÄDòFRÆ6Æ–'&6–öâf–vVçFRâ6’VÂW7V&–ò6æ6VÆÂ6RgVVÇfR¢òòW7FòÒÒ’†÷&FÖ&–VâVâÆåe2Â÷'VRÆ÷2fÆ÷&W2çVWf÷26P¢òòW67&–&VâçFW2FRÆ'VV&à¢F6Ä&µ„Ô”âÒEõ„Ô”ã²F6Ä&µ„Ô‚ÒEõ„Ôƒ°¢F6Ä&µ”Ô”âÒEõ”Ô”ã²F6Ä&µ”Ô‚ÒEõ”Ôƒ°¢òò’&W6WFVòÆ÷2fÆ÷&W2äUUE$õ2FRf'&–6âÆ6GW&W6fÆ÷&W0¢òò$r’æòFWVæFRFRÆ6Æ–'&6–öâÂW&òFV¦&ÆÖVF–26W&–¢òòÖ&–wVó¢6’VÂW7V&–ò6æ6VÆÖ—FBÂVÂ6—7FVÖFV&RW7F"òVà¢òòÆ6Æ–'&6–öâf–V¦‡&W7FW&F’òVâÆFRf'&–6ÂçVæ6VâVæ¢òòÖW¦6ÆFRÆf–V¦6öâF÷2VçF÷2çVWf÷2à¢Eõ„Ô”âÒE5ôDTeõ„Ô”ã²Eõ„Ô‚ÒE5ôDTeõ„Ôƒ°¢Eõ”Ô”âÒE5ôDTeõ”Ô”ã²Eõ”Ô‚ÒE5ôDTeõ”Ôƒ°¢u7FFRÒ5EõDõT4„4Ã°¢F6Å&VæFW"‚“°§Ð §7FF–2fö–BF6ÄW†—B‚—°¢–b‡F6Äg&öÕ6WGF–æw2’6WGF–æw4VçFW"‚“²VÇ6RVçFW$†öÖR‚“°§Ð ¢òò&W7FW&VÂ&W7ÆFò’6ÆRâ6’–6R†&–W67&—FòÆ6Æ–'&6–öâçVWf¢òòVâÆåe2Â6RFW6†6RFÖ&–VâÆÆ’à§7FF–2fö–BF6Ä6æ6VÂ‚—°¢Eõ„Ô”âÒF6Ä&µ„Ô”ã²Eõ„Ô‚ÒF6Ä&µ„Ôƒ°¢Eõ”Ô”âÒF6Ä&µ”Ô”ã²Eõ”Ô‚ÒF6Ä&µ”Ôƒ°¢–b‡F6Å6fVB—°¢òò†&–6Æ–'&6–öâ&Wf–Óâ6RgVVÇfRW67&–&—"FÂ7VÂà¢òòäòÆ†&–Óâ6RÆ–×–ÆÖ&6ÂòVÂ&FòF&–÷"'VVæVæ¢òò6Æ–'&6–öâVRVÂW7V&–ò6&FR&V6†¦"’æòföÇfW&–¢òòög&V6W&ÆçVæ6Â'&æ6"à¢–b‡F6Ä†E&Wb’G46Æ–%6fR‚“°¢VÇ6RG46Æ–$6ÆV"‚“°¢Ð¢6W&–Âç&–çFÆâ„b‚%´…uÒ6Æ–'&6–öâ6æ6VÆFÓâ&W7FW&FÆçFW&–÷""’“°¢F6ÄW†—B‚“°§Ð ¢òòÆ–6ÆÖFVÖF–6FR&GTõ26ö'&RÆ2B6GW&2à§7FF–2fö–BF6Ä6ö×WFTg&öÔ7&÷76W2‚—°¢òò‚ÒÖ†fu’ÂEõ„Ô”âÂEõ„Ô‚ÂÂ…õr’ÓâEõ„Ô”âÆ•¥T”U$D‡VçF÷2’"¢òò’ÒÖ†fu‚ÂEõ”Ô”âÂEõ”Ô‚ÂÂ…ô‚’ÓâEõ”Ô”â%$”$‡VçF÷2’¢òòÆ÷2ö&¦WF—f÷2æòW7FâVâÆ÷2&÷&FW2W†7F÷26–æò6öâE5ô4ÅôÔ$t”åò¢À¢òò6’VR6RW‡G&öÆ¢VÂÖ&vVâ6ö'&RVÂfæòÖVF–FòVçG&RÆ27'V6W2à¢–çC3%÷Bfu•ö—§Ò‚†–çC3%÷B—F6Å&u•³Ò²F6Å&u•³%Ò’ò#°¢–çC3%÷Bfu•öFW"Ò‚†–çC3%÷B—F6Å&u•³Ò²F6Å&u•³5Ò’ò#°¢–çC3%÷Bfu…ö'"Ò‚†–çC3%÷B—F6Å&u…³Ò²F6Å&u…³Ò’ò#°¢–çC3%÷Bfu…ö&Ò‚†–çC3%÷B—F6Å&u…³%Ò²F6Å&u…³5Ò’ò#°¢–çC3%÷BE’Òfu•öFW"Òfu•ö—§°¢–çC3%÷BE‚Òfu…ö&Òfu…ö'#°¢–çC3%÷B7å‚Ò…õrÒ"¢E5ô4ÅôÔ$t”åõƒ²òòfæò$TÂVçG&RÆ27'V6W0¢–çC3%÷B7å’Ò…ô‚Ò"¢E5ô4ÅôÔ$t”åõ“°¢–b‡7å‚Â’7å‚Ò°¢–b‡7å’Â’7å’Ò°¢Eõ„Ô”âÒ†–çCe÷B’†fu•ö—§Ò†E’¢E5ô4ÅôÔ$t”åõ‚’ò7å‚“°¢Eõ„Ô‚Ò†–çCe÷B’†fu•öFW"²†E’¢E5ô4ÅôÔ$t”åõ‚’ò7å‚“°¢Eõ”Ô”âÒ†–çCe÷B’†fu…ö'"Ò†E‚¢E5ô4ÅôÔ$t”åõ’’ò7å’“°¢Eõ”Ô‚Ò†–çCe÷B’†fu…ö&²†E‚¢E5ô4ÅôÔ$t”åõ’’ò7å’“°§Ð ¢òò&V–æ–6–Æ&öæFFRB7'V6W2†VÂW7V&–ò†VÇ6Fò%&WWF—""’à§7FF–2fö–BF6Å&W7F'D7&÷76W2‚—°¢F6ÅG&–W2²³°¢F6Å†6RÒD4Åô5$õ53°¢F6Å7FWÒ°¢F6Å'VâÒ°¢F6Åv—DÆ–gBÒG'VS°¢F6Å†6T×2ÒÖ–ÆÆ—2‚“°¢Eõ„Ô”âÒE5ôDTeõ„Ô”ã²Eõ„Ô‚ÒE5ôDTeõ„Ôƒ°¢Eõ”Ô”âÒE5ôDTeõ”Ô”ã²Eõ”Ô‚ÒE5ôDTeõ”Ôƒ°¢F6Å&VæFW"‚“°§Ð ¢òòVâ6òFRÆÖV–æâ6RÆÆÖFW6FRÆö÷‚’Âäò&Æ÷VVà§7FF–2fö–BF6ÅF–6²‚—°¢–b‚wDö²—²F6ÄW†—B‚“²&WGW&ã²Ð ¢òòÒÒÒÒçFÆÆFR&W7VÇFFó¢6öÆò&÷FöæW2ÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÐ¢–b‡F6Å†6RÓÒD4Åõ$U5TÅB—°¢–b‚BçF’&WGW&ã°¢–b…Bç’ãÒD4Åô%Dåõ’bbBç’ÃÒD4Åô%Dåõ’²D4Åô%Dåô‚—°¢–b…Bç‚ãÒD4Åô%DåôÅõ‚bbBç‚ÃÒD4Åô%DåôÅõ‚²D4Åô%Dåõr—²F6Å&W7F'D7&÷76W2‚“²&WGW&ã²Ð¢–b…Bç‚ãÒD4Åô%Dåõ%õ‚bbBç‚ÃÒD4Åô%Dåõ%õ‚²D4Åô%Dåõr—°¢6W&–Âç&–çFb‚%´…uÒ6Æ–'&6–öâ6öç6W'fF†FW7f–6–öâVB‚•Æâ"ÂF6ÄF—7B“°¢F6ÄW†—B‚“²&WGW&ã°¢Ð¢Ð¢&WGW&ã°¢Ð ¢òò&÷Föâ6æ6VÆ"‡6öÆò6’fVæ–Ö÷2FR§W7FW2’â6RÆVRFRBÂVR–G&P¢òòVÂF÷VR&ö6W6Fò÷"VÂ—VÆ–æRæ÷&ÖÂà¢–b‡F6Äg&öÕ6WGF–æw2bbBçFb`¢Bç‚ãÒD4Åô4ä4TÅõ‚bbBç‚ÃÒD4Åô4ä4TÅõ‚²D4Åô4ä4TÅõrb`¢Bç’ãÒD4Åô4ä4TÅõ’bbBç’ÃÒD4Åô4ä4TÅõ’²D4Åô4ä4TÅô‚—°¢F6Ä6æ6VÂ‚“²&WGW&ã°¢Ð ¢òò†’VRfW"VÂFVFòÄUdåDDòçFW2FR6WF"VÂ6–wV–VçFRVçFó²6¢òòæòÂVâ6öÆòF÷VR6÷7FVæ–Fò6R6öÖW&–Æ27VG&ò7'V6W2FRVæfW¢à¢–b‡F6Åv—DÆ–gB—°¢–b†F–v—FÅ&VB…”åõEô•%’ÒÄõrbbÖ–ÆÆ—2‚’ÒF6Å†6T×2âS’F6Åv—DÆ–gBÒfÇ6S°¢–b†F–v—FÅ&VB…”åõEô•%’ÓÒÄõr’F6Å†6T×2ÒÖ–ÆÆ—2‚“°¢&WGW&ã°¢Ð ¢òò6GW&F—&V7FFVÂD2…$r’Â6–â6"÷"Æ6Æ–'&6–öã¢W2ÆòVP¢òòW&Ö—FR&V6Æ–'&"VçVRÆ6Æ–'&6–öâf–vVçFRW7FR6ö×ÆWFÖVçFP¢òò&÷Fâ6RW†–vVâD4Åô4ôäd•$Õõ$TE2ÆV7GW&26VwV–F2’6R&öÖVF–âà¢G56×ÆR2ÒG56×ÆR‚“°¢òòv'W7’rÒVÂ&W6VçFW"FVæ–VÂ'W25’âäòW2&VÂFVFò6R†–Fò"Â6’VP¢òòæòVVFR&V–æ–6–"Æ7VVçFFR6öæf—&Ö6–öã¢6’Æò†–6–W&ÂVâçFÆÆ0¢òòVR6R&W–çFâ6W&–66’–×÷6–&ÆR§VçF"RÆV7GW&26VwV–F2à¢–b‡2æ'W7’’&WGW&ã°¢–b‚2çfÆ–B—²F6Å'VâÒ²F6Ä65‚ÒF6Ä65’Ò²&WGW&ã²Ð¢F6Ä65‚³Ò2çƒ²F6Ä65’³Ò2ç“²F6Å'Vâ²³°¢–b‡F6Å'VâÂD4Åô4ôäd•$Õõ$TE2’&WGW&ã° ¢V–çCe÷B'‚Ò‡V–çCe÷B’‡F6Ä65‚òF6Å'Vâ“°¢V–çCe÷B'’Ò‡V–çCe÷B’‡F6Ä65’òF6Å'Vâ“°¢F6Å'VâÒ²F6Ä65‚ÒF6Ä65’Ò°¢F6Åv—DÆ–gBÒG'VS°¢F6Å†6T×2ÒÖ–ÆÆ—2‚“° ¢òòÒÒÒÒf6RFRÆ2B7'V6W2ÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÐ¢–b‡F6Å†6RÓÒD4Åô5$õ52—°¢F6Å&u…·F6Å7FWÒÒ'ƒ°¢F6Å&u•·F6Å7FWÒÒ'“°¢F6Å7FW²³°¢–b‡F6Å7FWÂB—²F6Å&VæFW"‚“²&WGW&ã²Ð¢òòÆ2B7'V6W2W7Fã¢6R6Æ7VÆ’4RuT$D”â'F—"FRV’Æ¢òò'VV&6R†6R6öâÆ6Æ–'&6–öâFRfW&FBÂÆÖ—6ÖVRVVF&¢òòVâVÂ&Fò6’VÂW7V&–òÆ6öç6W'fà¢F6Ä6ö×WFTg&öÔ7&÷76W2‚“°¢G46Æ–%6fR‚“°¢F6Å6fVBÒG'VS°¢6W&–Âç&–çFb‚%´…uÒ6Æ–'&6–öâwV&FF…²VBââVEÒ•²VBââVEÒÓâ&ö&æFõÆâ"À¢Eõ„Ô”âÂEõ„Ô‚ÂEõ”Ô”âÂEõ”Ô‚“°¢F6Å†6RÒD4ÅõDU5C°¢F6Å&VæFW"‚“°¢&WGW&ã°¢Ð ¢òòÒÒÒÒf6RFR'VV&¢VâVçFòÂ’6RÖ–FRÆD•5Dä4”ÒÒÒÒÒÒÒÒÒÒÒÐ¢V–çCe÷BÇ‚ÒÂÇ’Ò°¢G5FôÆöv–6Â‡'‚Â'’ÂÇ‚ÂÇ’“°¢F6Ä†—E‚Ò†–çB–Çƒ²F6Ä†—E’Ò†–çB–Ç“°¢–çBG‚ÒF6Ä†—E‚ÒF6ÄW‡‚ÂG’ÒF6Ä†—E’ÒF6ÄW‡“°¢òòF—7Fæ6–WV6Æ–FV6öâÆ&—¢VçFW&VR–W6VÂÖ÷F÷"w&f–6ð¢òò†—7'C3"“¢6–â6öÖfÆ÷FçFR’6–âFW6&÷&F"ÒÒVÂV÷"66òV’W0¢òòCƒã"²ƒã"ÒƒsãCÂ×W’÷"FV&¦òFVÂÆ–Ö—FRFR3"&—G2à¢F6ÄF—7BÒ—7'C3"†G‚¢G‚²G’¢G’“°¢F6Äw&FRÒ‡F6ÄF—7BÃÒE5õDU5Eôu$TTâ’òD4Åôu$DUôu$TTà¢¢‡F6ÄF—7BÃÒE5õDU5Eõ”TÄÄõr’òD4Åôu$DUõ”TÄÄõp¢¢D4Åôu$DUõ$TC°¢6W&–Âç&–çFb‚%´…uÒ'VV&¢W7W&Fò‚VBÂVB’ÆV–Fò‚VBÂVB’FW7f–6–öâVB‚ÓâW5Æâ"À¢F6ÄW‡‚ÂF6ÄW‡’ÂF6Ä†—E‚ÂF6Ä†—E’ÂF6ÄF—7BÀ¢F6Äw&FRÓÒD4Åôu$DUôu$TTâò%dU$DR"¢F6Äw&FRÓÒD4Åôu$DUõ”TÄÄõrò$Ô$”ÄÄò"¢%$ô¤ò"“°¢F6Å†6RÒD4Åõ$U5TÅC°¢F6Å&VæFW"‚“°§Ð ¢òòVVçFRFVÂÖöGVÆòõDâfT’Â’æò'&–&Â&÷÷6—Fó¢–×ÆVÖVçF¢òòÆ2gVæ6–öæW2÷F†÷7B¢ÆÆÖæFòÆ2&–Ö—F—f2w&f–62FVÂ6—7FVÖ¢òò†f–ÆÅ&V7BÂG&uFW‡BÂ&W6VçBÂ6WD'Vbâââ’ÂVR6öâ7FF–6’÷"FçFð¢òò6öÆòW†—7FVâ'F—"FVÂVçFòFVÂf–6†W&òVâVR6RFVf–æ–W&öâà¢6–æ6ÇVFR$fÆW„õ5ôõDô'&–FvRæ‚  §fö–B6WGW‚—°¢6W&–Âæ&Vv–âƒS#“°¢FVÆ’ƒc“°¢6W&–Âç&–çFÆâ„b‚%ÆãÓÓÒfÆW„õ2VÇG&„U53"Õ32ãe#‚’'&æ6æFòÓÓÒ"’“° ¢òòÔ„”ÔdTÄô4”DBDR5RFW6FRVÂ&–ÖW"–ç7FçFRâVÂ32'&æ6Æ¢òòg&V7VVæ6–FVÂ&ö÷FÆöFW#²f–¦&ÆV’#CÔ‡¢†6RVRVÂ&÷–ð¢òò'&çVR‡F&ÆFVÂæVÂÂ&÷'&FòFR2Ô"FR5$ÒÂ–æ—BFRÆ6Ö&¢òòf–ÂÖ†–ÖòâVÂæVÂ&–FòVVFR&¦&ÆÇVVvòc‡4Ç•÷vW"’à¢6WD7Tg&WVVæ7”Ö‡¢ƒ#C“° ¢òò…–äò6RFö6VÂEtEBV’â’ÆfW'6–öâçFW&–÷"ÆÆÖ&¢òòW7÷F6µ÷vGE÷&V6öæf–wW&R‚’Vâ6F'&çVR&F"Ö&vVâÀ¢òò'&–ær×WFVÂæVÂâW&–ææV6W6&–òÖæFVâ6WGW‚’&Æ÷VVÖ0¢òòFRVæg&66–öâFR6VwVæFòÂ’Æ&F–ò–æò6÷'&RV’Ò’W0¢òò6öF–vòçVWfòæòfW&–f–6Fò6öçG&VÂW7FFò&VÂFVÂEtEBVâW7F¢òòÆ6Â6’VR6R&WF—&¢ÖVæ÷27WW&f–6–R&Vâ7&6‚Vâ6F¢òò&ö÷Bâ6–wVRVâ–RW7÷F6µ÷vGE÷&W6WB‚’VâÆö÷‚’‡fW"Ö2&¦ò’à ¢òòd”ÅE$òDRTä4TäD”DòFW6FRvFò6ö×ÆWFòâfåDU2FRfÆW…æVÄ–æ—B‚’¢òò&÷÷6—Fó¢6’VÂF÷VRæò6R6÷7F–VæR22ÂVÂ6†—6RgVVÇfRF÷&Ö—"6–à¢òò†&W"Væ6VæF–FòçVæ6æ’VÂæVÂæ’VÂ&6¶Æ–v‡BÂ6’VRVâ&ö6P¢òò66–FVçFÂVâVÂ&öÇ6–ÆÆòæò&öGV6Ræ’VâFW7FVÆÆòâfW"öfev¶TvFR‚’à¢öfev¶TvFR‚“° ¢òòæVÃ¢&V–çFVçFò6÷FFòâ6’FRfW&FBæòVæ6–VæFR†6&ÆVFò5’’À¢òò'FVòFVÂ&6¶Æ–v‡B6öÖò4õ2ÓâÆÆ66–wVRf—fÂæò×VW'Fà¢&ööÂæVÄö²ÒfÆW…æVÄ–æ—B‚“°¢–b‚æVÄö²—²FVÆ’ƒS“²æVÄö²ÒfÆW…æVÄ–æ—B‚“²Ð¢–b‚æVÄö²—°¢6W&–Âç&–çFÆâ„b‚%´dDÅÒVÂæVÂ5’æò&W7öæFR‡&Wf—652ôD2õ%5Bõ44²ôÔõ4’’"’“°¢–äÖöFR…”åôÄ4Eô$ÂÂõUEUB“°¢f÷"ƒ³²—²F–v—FÅw&—FR…”åôÄ4Eô$ÂÂ„”t‚“²FVÆ’ƒS“²F–v—FÅw&—FR…”åôÄ4Eô$ÂÂÄõr“²FVÆ’ƒS“²Ð¢Ð¢òòf—6òFV×&æò’W‡Æ–6—Fò6’Æ5$ÒæòW7F7F—fF¢6–âVÆÆæò†¢òòæ’Vâ6öÆòÆ–Vç¦ò’VÂ6—7FVÖæòVVFR'&æ6"âW2VÂW'&÷"FP¢òò6öæf–wW&6–öâÖ26ö×VâÂ6ö×–Æ"&VæÆ6ãe#‚à¢–b‚7&Ôf÷VæB‚’—°¢6W&–Âç&–çFÆâ„b‚%´dDÅÒ5$ÒæòFWFV7FFâVâVÂ”DS¢FööÇ2â5$Òâtõ’5$Òr"’“°¢Ð¢–b‚fÇ„vg„–æ—B‚’—°¢6W&–Âç&–çFÆâ„b‚%´dDÅÒ6–â5$Ò†7F—fu5$Ó¢õ’5$ÒrVâVÂ”DR’"’“°¢f÷"ƒ³²’FVÆ’ƒ“°¢Ð ¢fÆW…F÷V6„–æ—B‚“²òò…C#Cc¢fÆÆò7VfR‡6’æò&W7öæFRÂ6R6–wVR6–âF7F–Â¢òò4Ä”%$4”ôã¢6RÖ&66öÖòTäD”TåDR6’W2Æ&–ÖW&fW¢Âò6’VÂW7V&–ð¢òòÖçF–VæRVÂFVFò÷–FòGW&çFRVÂ'&çVR‡f–FRW66R&&V6Æ–'& ¢òò6–â&÷'&"Æåe2’â–æò6RV¦V7WFV’Ö&Æ÷VV&–VÂ'&çVRÓ¢6P¢òòVçG&VâVÂW7FFò5EõDõT4„4ÂÂf–æÂFR6WGW‚’ÂVâfW¢FVÂ7Æ6‚à¢&ööÂæVVD6ÂÒ†wDö²bb‚G46Æ–$FöæR‚’ÇÂF–v—FÅ&VB…”åõEô•%’ÓÒÄõr’“°¢òò4Ô$õc#cCâfDU5TU2FRÆ÷2Æ–Vç¦÷2&÷÷6—Fó¢6’Â6’Æ5$Ð¢òòæGWf–W&§W7FÂV–Vâ6RVVF6–âÖVÖ÷&–W2Æ6Ö&‡Væ’’æð¢òòVÂ6—7FVÖVçFW&òâ7RfÆÆòW27VfS¢Æ6Ö&6RÂG&öâFP¢òò'VV&’FöFòÆòFVÖ26–wVR–wVÂà¢òð¢òò’fåDU2FRfÆW„“$4–æ—B‚’FÖ&–Vâ&÷÷6—Fó¢VÂG&—fW"444"FRÆ¢òò6Ö&&W6W'f7R&÷–òVW'Fò“$2FVÂ6†—â'&æ6æFò&–ÖW&òÆ¢òò6Ö&Â6’÷"Æ6öæf–wW&6–öâFVÂ6÷&RÆRFö6&VÂÖ—6ÖòVW'FòVP¢òòv—&RÂV–Vâ6RVVF6–â'W2W2VÂW66æW"FRÖöGVÆ÷2‡VægVæ6–öà¢òò66W6÷&–ÂVR6Rv6öÆ6öât“&4ö³ÖfÇ6R’’çVæ6Æ6Ö&à¢6Õ6Vç6÷$–æ—B‚“°¢fÆW„“$4–æ—B‚“²òò'W2“$2FRÖöGVÆ÷2W‡FW&æ÷2„d4R"¢&ö÷D–æ—E&F–õ6fR‚“²òòv”f“¢çVæ6&Æ÷VVVÂ'&çVP¢fÆW„÷F&Vv–â‚“²òòõD¢7&VÆF&VFRföæFò‡&–÷&–FB&¦’âæò6öæV7Fæ’FW66&væFV’à¢6ftÆöB‚“°¢6WD&6¶Æ–v‡B†t'&–v‡B“²òòÆ–6VÂ'&–ÆÆòwV&FFð¢†öÖT÷&FW$ÆöB‚“²òò÷&FVâFR–6öæ÷2FVÂ†öÖP¢òòDT4ÄDò„f6W2ÔB“¢vVöÖWG&–FVÂFÖæòwV&FFòÂ&çW&2f–¦F2FVÀ¢òò÷'FVÆW2’Væ6ö×&ö&6–öâ&&FFRVRW6RFÖæò6&RVâçFÆÆà¢¶$Ç•6—¦R‚“°¢¶$×E7W&f6U&W6WB‚“°¢6Æ—ÆöE–ææVB‚“°¢6W&–Âç&–çFb‚%´´%ÒFÖæóÒVB‚VG‚VBvÒVBƒÒVB’6&SÒW5Æâ"À¢t¶%6—¦RÂ´%ôµrÂ´%ô´‚Â´%ôtÂ´%õ‚Â¶%6—¦T6†V6²‚’ò'6’"¢$äò"“° ¢6Æ´&ö÷D×2ÒÖ–ÆÆ—2‚“°¢6VVDÖ–äödF’Ò2¢c²#3²òò6–VÖ'&¢6"B§VÂÂ3£#2†6öÖòGW2–ÖvVæW2¢6Æ´Æ7DÖ–âÒÓ°¢6ÆµWFFR‚“° ¢òòçFÆÆFRF–væ÷7F–6ò4ôÄò6’VÂ&V–æ–6–ògVRäõ$ÔÀ¢òò†7&6‚òvF6†Förò'&÷væ÷WB’âVâVæ6VæF–Fòæ÷&ÖÂÂ'&çVRÆ–×–òà¢W7÷&W6WE÷&V6öå÷B'"ÒW7÷&W6WE÷&V6öâ‚“°¢òòFW7W'F"FRVâvFò6ö×ÆWFòW2Vâ'&çVRäõ$ÔÂÂæòVâ7&6ƒ¢6–à¢òòW7F&ÖÆ&æFf÷&Vç6R6ÆG&–Vâ6FVæ6VæF–FòFW6FRFVW6ÆVWà¢òòt&ö÷D6ÆVäöfbF—7F–æwVR&VÂW7V&–òvò&÷÷6—Fò"FRVâFVW6ÆVWVP¢òòæò6Æ–òFRV“²Æ&æFW&6R6öç7VÖR‡6R&÷'&’&VR6öÆòfÆv&¢òòW7FR'&çVRà¢&ööÂg&öÔFVWÒ‡'"ÓÒU5õ%5EôDTU4ÄTU“°¢–b†g&öÔFVW—°¢&Vg2æ&Vv–â‚&fÆW†÷2"ÂfÇ6R“°¢t&ö÷D6ÆVäöfbÒ&Vg2ævWD&ööÂ‚&6ÆVæöfb"ÂfÇ6R“°¢–b†t&ö÷D6ÆVäöfb’&Vg2çWD&ööÂ‚&6ÆVæöfb"ÂfÇ6R“°¢&Vg2æVæB‚“°¢6W&–Âç&–çFb‚%µu%Ò'&çVRFW6FRFVW6ÆVW†vFòÆ–×–ó¢W2•Æâ"Ât&ö÷D6ÆVäöfbò'6’"¢&æò"“°¢Ð¢&ööÂ&æ÷&ÖÂÒ‡'"ÓÒU5õ%5EõõtU$ôâÇÂ'"ÓÒU5õ%5Eõ5rÇÂg&öÔFVW“°¢–b†&æ÷&ÖÂ’6†÷t&ö÷D&ææW"‚“° ¢òòföæFòäTu$ò%4ôÅUDò&VÂ7Æ6‚†6öÖòVâÖ÷f–Â6öÖW&6–Â¢6WD'Vb†f"“°¢f–ÆÅ&V7BƒÂÂ45%õrÂ45%ô‚Â&v#ScRƒÃÃ’“°¢fÇ„fÇW6„ÆÂ‚“° ¢7Æ6…7F'BÒÖ–ÆÆ—2‚“°¢u7FFRÒ5Eõ5Ä4ƒ° ¢òò4Ä”%$4”ôâTäD”TåDS¢6RVçG&T’ÂÂf–æÂFR6WGW‚’ÂVâfW¢FVÀ¢òò7Æ6‚âW2ÆòVÇF–ÖòVR†6RVÂ'&çVRÂ6’VR7VæFòÆçFÆÆ¢òòFR6Æ–'&6–öâ&V6RVÂ6—7FVÖ–W7FVçFW&òVâ–R’7RF–6²6÷'&P¢òòFW6FRÆö÷‚’6öÖòVÂFR7VÇV–W"÷G&çFÆÆÒÒæF&Æ÷VVà¢òòÂFW&Ö–æ"†ò6æ6VÆ"’6RÂ†öÖR÷"Æf–æ÷&ÖÂà¢–b†æVVD6Â’F6ÄVçFW"†fÇ6R“°§Ð ¢òò'V6ÆRFRæ–Ö6–öâ6öçF–çVòFRÆT’(	B6÷'&R6RÆòVR6RÂæò6öÆð¢òò7VæFò†’VâFâ6ö×öæRöfb×67&VVâ†çF’ÖfÆ–6¶W"’f–6F&VæFW"à§7FF–2Vç6–væVBÆöærV”æ–Ô×2Ò°§7FF–2fö–BV•F–6²‚—°¢òòVÂ&—ÆRFVÂ–6öæòW2Vææ–Ö6–öâW&ÖVçFRDTÕõ$Â‡7R÷6–6–öâW0¢òògVæ6–öâFRÖ–ÆÆ—2‚’ÂæòVâ6òf–¦ò÷"g&ÖR’’ÂG&2VÂ&W–çFFð¢òò&6–ÂFRæ–ÖFT–6öå&—ÆRÂ7VW7F×W’ö6ò÷"g&ÖRâ÷"W6òU4'WF¢òò6R&Vg&W66ãcg3¢&W7FW&W†7FÖVçFRÆg&æ¦VRF–'V¦Â6’VP¢òòW26÷'&V7F÷"6öç7G'V66–öâ’×W’&&FâVÂ&W7Fò†VF–6–öâÂ6÷'F–æ¢òò6öç6W'f7R6FVæ6–FRã#bg2ÒÒÆwVæ2ÆÆWfâ6÷2÷"Ög&ÖR‡æV¢à¢òòVÂ&W6÷'FRFR–6öæ÷2VâÖöFòVF–6–öâ’’6VÆW&&Æ26Ö&–&–7P¢òòdTÄô4”DBÂæò6öÆò7R7Vf–FC²÷"W6òæò6RFö6âà¢òòÄ4õ%D”ä4’4Äd”$”Dâ–æòF–VæRæ–æwVâ6ò÷"Ö7VG&ó¢7P¢òò÷6–6–öâ6ÆRFVÂFVFò†6öâ7Vf—¦Fò÷"6öç7FçFRFRD”TÕò’òFVÂ&VÆö ¢òò‡4æ–ÕFò’Â’VÂFW7FVÆÆòFR7W2&÷FöæW2FÖ&–VâW2gVæ6–öâFRÖ–ÆÆ—2‚’à¢òò7V&—"7R6FVæ6–ãcg26öÆòÆ†6RÖ27VfRÂæòÖ2&–FÒÒ’#`¢òòg2VâvW7Fò&–Fòfç¦&FçFòVçG&R7VG&ò’7VG&òVRVÂÖ÷f–Ö–VçFð¢òò6RfV–6ÇF÷2à¢&ööÂf7EF‚Ò†u7FFRÓÒ5Eô„ôÔRbbVF—DÖöFRbb†u&—ÆT7F—fRÇÂ5æVÅ’â’“°¢Vç6–væVBÆöær–çFW'fÂÒf7EF‚òb¢3ƒ°¢–b†Ö–ÆÆ—2‚’ÒV”æ–Ô×2Â–çFW'fÂ’&WGW&ã°¢V”æ–Ô×2ÒÖ–ÆÆ—2‚“°¢–b†u7FFRÓÒ5Eô„ôÔR—°¢–b‡5æVÅ’â’5&VæFW"‚“²òò6÷'F–æf—6–&ÆS¢æ–ÖVÂFW7FVÆÆòFRF÷VR†–æ6ÇW6òGW&çFRVÂG&r¢VÇ6R–b†VF—DÖöFR’VE&VæFW"‚“²òò¦–vvÆR6öçF–çVð¢VÇ6R–b†u&—ÆT7F—fR’æ–ÖFT–6öå&—ÆR‚“²òòFW7FVÆÆòFVÂ–6öæòFö6Fò…f–G&–ò’ÂããW0¢Ð§Ð §fö–BÆö÷‚—°¢fÆW„fVVEvGB‚“²òòÆ–ÖVçFVÂEtEB6öÆò6’Æö÷F6²6–wVR7W67&—Fò‡fW"'&–&¢fÆW…öÆÅF÷V6‚‚“²òò†V’FVçG&ò6÷'&RFÖ&–VâVÂFWFV7F÷"FRFö&ÆR×FFRÆ7W7Vç6–öâ¢7W7fFUF–6²‚“²òò5U5Tå4”ôâôtDó¢Vâ6òFVÂgVæF–FòFR&6¶Æ–v‡B†æò&Æ÷VVçFR¢WFôÆö6µF–6²‚“²òòd4R¢&Æ÷VVò÷"–æ7F—f–FB†ÆVRB6–âf–ÇG&"ÂçFW2FRVRæF–R6öç7VÖVÂF÷VR¢æ÷F–d†æFÆUF÷V6‚‚“²òòÆ—6Æ–çFW&6WFF÷VW2FVçG&òFR7W2F&¦WF2„f6R¢fÆW„÷FF÷V6„'&–FvR‚“²òòõD¢6’†’÷fW&Æ’f—6–&ÆRÂ6RVVFVÂF÷VRçFW2VRæF–P¢‡tFWFV7EF–6²‚“²òòFWFV66–öâ“$2–æ7&VÖVçFÂÂÖ—6Öò6öçFW‡FòVRVÂF7F–Â„f6R"¢v–f”WFõ&V6öææV7EF–6²‚“²òò&V6öæW†–öâv”f’F–fW&–F†Æ&F–òåTä46RFö6Vâ6WGW‚“²fW"&ö÷D–æ—E&F–õ6fR¢&ööÂÖ–ä6†ævVBÒ6ÆµWFFR‚“°¢tÖ–ä6†ævVBÒÖ–ä6†ævVC° ¢òòÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÐ¢òòåDÄÄTâU„4ÅU4•d$TÂõD¢òòÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÐ¢òò6öâVæ6õDçFÆÆ6ö×ÆWF†6†ævVÆörÂ&öw&W6òð¢òò§W7FW2FR7GVÆ—¦6–öâ’Âä”äuTâ÷G&ò7V'6—7FVÖF–'V¦à¢òð¢òò÷"VS¢VÂ÷fW&Æ’æò6Ö&–u7FFRÒÒGW&çFRVæFW66&v¢òò6VwV–Ö÷2Vâ5Eô„ôÔRâ6–âW7FR6÷'FRÂVâ6FgVVÇF6P¢òòV¦V7WF&â–wVÆÖVçFR†öÖUF–6²‚’Â¶–÷6µF–6²‚’ÂV•F–6²‚’¢òòæ÷F–eF–6²‚’Â’FöF÷2VÆÆ÷26ö×öæVâVâVÂÔ•4Ôò&'Vb¢òòV&Æ–6â7R&æF6öâ&W6VçB‚’âVçG&RF÷2&W–çFF÷2FVÂõD¢òò6R6öÆ&Væ&æF6öâVÂföæFòFVÂW67&—F÷&–òÒÒVâFVw&FFð¢òò§VÂ÷fW&FRÒÒ§W7FòVæ6–ÖFRÆçFÆÆFR&öw&W6ó¢W6RW&¢òòVÂ''FVò6–â"Vâ6FRâæòW&Væ6'&W&VçG&P¢òòçV6ÆV÷2†VÂ&W6VçFW"W2VÂVæ–6òVR†&Æ6öâVÂæVÂ’–¢òòW7F&&÷FVv–Fò÷"fÇ„f$×W‚“¢W&VRæF–RFVæ–Æ¢òò&÷–VFBW†6ÇW6—fFRÆçFÆÆà¢òð¢òòVÂF7F–ÂÂVÂEtEBÂÆFWFV66–öâ“$2’Æ&F–ò4”uTTà¢òò6÷'&–VæFò'&–&¢V’6öÆò6R6÷'FVÂD”%T¤òà¢òòÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÒÐ¢–b†fÆW„÷F÷vç567&VVâ‚’—°¢fÆW„÷F&VæFW"‚“°¢FVÆ’ƒR“°¢&WGW&ã°¢Ð ¢7v—F6‚†u7FFR—°¢66R5Eõ5Ä4ƒ¢7Æ6…F–6²‚“²'&V³°¢66R5Eôôô$UôÄäs¢öö&TÆæuF–6²‚“²'&V³°¢66R5Eôôô$UôäÔS¢öö&TæÖUF–6²‚“²'&V³°¢66R5EôÄô4³ ¢–b†Ö–ä6†ævVB—²&VæFW$Æö6²‚“²–b†Æö6´öfbÓÒ’6†÷tÆö6²‚“²Ð¢Æö6µF–6²‚“°¢'&V³°¢66R5Eô„ôÔS ¢–b†Ö–ä6†ævVBbb5æVÅ’ÓÒbbVF—DÖöFR—°¢&VæFW$†öÖR‚“²òò&Vg&W66VÂ66†R†öÖT'Vb†öfg67&VVã¢6WD'Vb††öÖT'Vb’ââç6WD'Vb†f"’Â6–âFö6"çFÆÆ¢6†÷t†öÖR‚“°¢Ð¢–b†VF—DÖöFRÇÂ4†æFÆR‚’’†öÖUF–6²‚“²òòVâVF–6–öâÂ6ÇF"Æ6÷'F–æ¢'&V³°¢66R5Eô¢F–6²‚“²'&V³°¢66R5Eõ5t•D4„U#¢7uF–6²‚“²'&V³°¢66R5EôÄô4µ4UEU¢Ç7UF–6²‚“²'&V³°¢66R5Eõt”d“¢v–f•F–6²‚“²'&V³°¢66R5Eô5Eƒ¢7G…F–6²‚“²'&V³²òòd4R#¢ÖVçR6öçFW‡GVÂFRÆöær×&W70¢66R5Eô´”õ4µ4UC¢¶–÷6µ6WEF–6²‚“²'&V³²òòd4RC¢FVf–æ—"VÂ&VW†6ÇV–F¢66R5EõõtU$ôdeô4ôäd•$Ó¢öfeF–6²‚“²'&V³²òòtDó¢6Æ–FW"&FW6Æ—¦&v" ¢66R5EõõtU$ôdeôä”Ó¢öfdæ–ÕF–6²‚“²'&V³²òòtDó¢æ–Ö6–öâf–æÂ†æògVVÇfR¢66R5Eô´%4UC¢¶'5F–6²‚“²'&V³²òòd4RS¢§W7FW2FVÂFV6ÆFð¢66R5EõDõT4„4Ã¢F6ÅF–6²‚“²'&V³²òòõ%B33¢6Æ–'&6–öâF7F–Â†æò&Æ÷VVçFR¢Ð¢¶–÷6µF–6²‚“²òòd4RC¢&Vg&W66VÂ6æFFò’W67V6†VÂvW7FòFR6Æ–F¢V•F–6²‚“²òòæ–Ö6–öâ6öçF–çVFVÂf–G&–ð¢æ÷F–eF–6²‚“²òò—6ÆF–æÖ–6¢æ–Ö’6ö×öæR6ö'&RÆçFÆÆ7F—f„f6R¢fÆW„÷F&VæFW"‚“²òòõD¢TÅD”Ô6FVÂ—VÆ–æRw&f–6ò†çVæ6Fö6VÂf"FRVæ¢FVÆ’ƒR“°§Ð ¢òò2222222222222222222222222222222222222222222222222222222222220¢òò22„ô¤DR%UD†ÆòVRÆÆVvFW7VW2FVÂÖ–ÆW7FöæR¢òò2222222222222222222222222222222222222222222222222222222222220¢òð¢òòÖ–ÆW7FöæR„U5DR&6†—fò’(	B4ôÕÄUDó ¢òò+r6…s¢æVÂ5’5Css“bô”Ä““Cƒ‚²F7F–Â…C#Cb†6öæf–rFR&GTõ2¢òò+rÖ÷F÷"w&f–6ò&÷–ò†g&ÖV'VffW'25$Ò²&W6VçFW"6÷&R¢òò+rgVVçFRWƒr6öâ6VçF÷2UDbÓ‚†W2ög"÷Bö—B’²&VÆö¢fV7F÷&–À¢òò+r7Æ6‚6öâgVæF–Fò+rôô$Rƒb–F–öÖ2²FV6ÆFòtU%E’¢òò+r&Æ÷VVò6öâ&VÆö¢v–vçFR’FW6&Æ÷VVò6öâf—6–6‡7v—R×W¢òò+rW67&—F÷&–ó¢&'&FRW7FFòÂ"v–FvWG2Â&V¦–ÆÆGƒ2ÂFö6²Âæ`¢òò+r&æFf÷&Vç6RFR&V–æ–6–ò†FWW&6–öâ6–â2¢òð¢òòÖ–ÆW7FöæR"(	Bg&ÖWv÷&²FR2²2&VÆW3 ¢òò´„T4„õÒ6—7FVÖFRfVçFæ3¢W'GW&ö6–W'&Ræ–ÖFòFW6FRVÂ–6öæòÀ¢òòÖ&6òW7FæF"†W7FFò²6&V6W&&G&2"²æb’Â&Vv—7G&ð¢òòõ$TrVæ6‡Vf&ÆRÂvW7F÷2FR6–W'&RâFR&VfW&Væ6–¢&VÆö¢à¢òòµTäD”TåDUÒ&VÆÆVæ"VÂ&W7Fò‡&VV×Æ¦"VçG&F2FRõ$Tr“ ¢òòvÆW&–Â×VÇF–ÖVF–ÂÆÖ6VæÖ–VçFòÂÖöFò2Âæ÷F2ÂVGV66–öâÀ¢òòæfVvF÷"Â6öFR”DRÂ&–VæW7F"Â–çBÂ§VVv÷2Â6Æ7VÆF÷&À¢òò6ÆVæF&–òÂ6Ö&à¢òð¢òòÖ–ÆW7FöæR2(	B§W7FW2†–ÖvVâ2“¢´„T4„õÒF÷2æVÆW2†&'&ÆFW&À¢òòFR"6FVv÷&–2²æVÂFRFWFÆÆR6öâ67&öÆÂ’âvVæW&Â’6W&6FP¢òò6öâFF÷2&VÆW2FVÂF—7÷6—F—fó²&W7Fò6öâf–Æ2&W&W6VçFF—f2à¢òòÖ÷F÷#¢6RæF–ò&V6÷'FRfW'F–6Â†6Æ—’&Æ—7F26öâ67&öÆÂà¢òð¢òòÖ–ÆW7FöæRB(	BÖöFò2W7F–Æòv–æF÷w2†–ÖvVâB“¢&'&FP¢òòF&V2ÂfVçFæ2fÆ÷FçFW2ÂW67&—F÷&–ò†÷&—¦öçFÂà¢òð¢òòVæF–VçFW2FRÆFf÷&Ö†7VæFòF÷VR“ ¢òò+rgVVçFR4¤²†&6†—fòFRgVVçFRVâ5”de2’&6†–æò&VÂà¢òò+rv”f’ôåE¢Æ&F–òFVÂ32W2æF—f’f–&ÆS²†÷’6–wVRVâÖöFð¢òò&¦òFVÖæF6öÆò&†÷'&"$Ò’&FW&–‡fW"&ö÷D–æ—E&F–õ6fR’à¢òò+r&FW&–&VÂ÷"D2‡W6"D3¢VÂD3"æò6öçf—fR6öâVÂv”f’’à¢òòVÂ'&–ÆÆò÷"tÒFVÂ&6¶Æ–v‡B”W7F†V6†òVâW7FR÷'B„ÄTD2’à¢òò+rÆÖ6VæÖ–VçFòVâ4Bò5”de2&föæF÷2’§W7FW2à¢òò2222222222222222222222222222222222222222222222222222222222220