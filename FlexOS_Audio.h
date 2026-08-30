// #############################################################
// ##  FlexOS · AUDIO  ·  codec ES8311 + I2S  (ESP32-P4)
// ##  Placa JC4880P443 V1.0
// #############################################################
//
//  CABLEADO REAL (verificado en el esquema, hoja del modulo
//  JC-ESP32P4-M3 y hoja "ES8311")
//  ------------------------------------------------------------
//    ES8311 pin 2  MCLK   <- CODEC_I2S0_MCLK  -> GPIO13
//    ES8311 pin 6  SCLK   <- CODEC_I2S0_SCLK  -> GPIO12   (BCLK)
//    ES8311 pin 8  LRCK   <- CODEC_I2S0_LRCK  -> GPIO10   (WS)
//    ES8311 pin 9  DSDIN  <- CODEC_I2S0_DSDIN -> GPIO9    (salida del P4)
//    ES8311 pin 7  ASDOUT -> ES7210_SDOUT     -> GPIO48   (entrada al P4)
//    ES8311 pin 1  CCLK   <- ES_I2C_SCL       -> GPIO8
//    ES8311 pin 19 CDATA  <- ES_I2C_SDA       -> GPIO7
//    ES8311 pin 20 CE     -> a masa por R1 10K  => direccion I2C 0x18
//    Amplificador NS4150: STD <- PA_CTRL -> GPIO11, con R19 10K a
//    masa (o sea: apagado por defecto; hay que subirlo para que
//    suene).
//
//  EL BUS I2C ES COMPARTIDO CON EL TACTIL
//  --------------------------------------
//  GPIO7/GPIO8 son los MISMOS pines del GT911 (PIN_TP_SDA/SCL del
//  sketch). Por eso este modulo:
//    · no crea su propio bus: usa el objeto Wire que ya inicializo
//      el sketch para el tactil;
//    · no habla con el codec desde otra tarea. Todas sus llamadas
//      salen del mismo hilo que sondea el tactil, igual que hace la
//      deteccion I2C incremental del sistema. Compartir un bus I2C
//      entre dos tareas sin proteccion es exactamente lo que
//      corrompe el bus y cuelga el panel.
//  Las escrituras al codec son unos pocos bytes y solo ocurren al
//  arrancar y al cambiar el volumen, no en cada bloque de audio: el
//  audio en si va por I2S, que no toca I2C para nada.
//
//  HONESTIDAD SOBRE LA DISPONIBILIDAD
//  ----------------------------------
//  flexAudioAvailable() NO devuelve true "porque la placa deberia
//  tener codec". Devuelve true solo si:
//    1) el ES8311 responde en 0x18, Y
//    2) sus registros de identificacion leen 0x83/0x11 (o sea, es
//       ese chip y el bus funciona en los dos sentidos), Y
//    3) el canal I2S se creo y arranco sin error.
//  Si algo de eso falla, flexAudioError() dice cual y la interfaz NO
//  dibuja ningun control de sonido. Un deslizador de volumen que no
//  mueve nada es peor que no tener deslizador.
//
//  ESTADO DE VERIFICACION (sin ocultarlo): el cableado y la
//  direccion salen del esquema oficial y la secuencia de arranque
//  del codec es la del fabricante, pero ESTE MODULO NO SE HA PODIDO
//  PROBAR CON ALTAVOZ. Por eso todo cuelga de la comprobacion de
//  arriba: si el chip no contesta lo que tiene que contestar, no se
//  anuncia audio.

#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Direccion del codec con CE a masa (es como esta en esta placa).
#define FLEXAUDIO_I2C_ADDR   0x18

// Pines, en un solo sitio.
#define FLEXAUDIO_PIN_MCLK   13
#define FLEXAUDIO_PIN_BCLK   12
#define FLEXAUDIO_PIN_WS     10
#define FLEXAUDIO_PIN_DOUT    9
#define FLEXAUDIO_PIN_DIN    48
#define FLEXAUDIO_PIN_PA     11

// Volumen: 0..100. El tope corresponde a 0 dB en el DAC del codec,
// NO al maximo del registro: por encima de 0 dB el ES8311 aplica
// ganancia digital y satura. Un "100%" que distorsiona no es un
// 100% util.
#define FLEXAUDIO_VOL_MAX   100
#define FLEXAUDIO_VOL_DEF    70

// -------------------------------------------------------------
//  Arranque. Llamar DESPUES de Wire.begin() (el tactil ya lo hace).
//  No bloquea mas que unos milisegundos y es seguro llamarla aunque
//  no haya codec: en ese caso deja el modulo en "no disponible".
// -------------------------------------------------------------
bool        flexAudioBegin();

// true solo si hay codec verificado Y salida I2S lista.
bool        flexAudioAvailable();

// Motivo legible de por que no hay audio (o "Listo" si lo hay).
// Nunca NULL: se puede ensenar tal cual.
const char* flexAudioError();

// -------------------------------------------------------------
//  Reproduccion PCM
//  ------------------------------------------------------------
//  El modulo NO tiene hilo propio ni cola: el llamante le va dando
//  bloques y flexAudioWrite entrega al DMA lo que quepa AHORA,
//  devolviendo cuanto acepto. Nunca bloquea, asi que se puede
//  llamar desde el mismo tick que dibuja.
// -------------------------------------------------------------
// Prepara la salida para ese formato (8 o 16 bits, 1 o 2 canales).
// false si el formato no se admite o no hay codec.
bool        flexAudioStartPcm(uint32_t sampleRate, uint16_t channels, uint16_t bits);

// Entrega bytes. Devuelve cuantos se aceptaron (0 si el buffer del
// DMA esta lleno ahora mismo), o -1 si no hay reproduccion activa.
int         flexAudioWrite(const void* data, size_t n);

// Para, silencia el amplificador y suelta el canal.
void        flexAudioStop();
bool        flexAudioPlaying();

// -------------------------------------------------------------
//  Volumen REAL
//  ------------------------------------------------------------
//  Escribe el registro de volumen del DAC del codec (0x32). No es
//  un numero de la interfaz: cambia la salida analogica de verdad.
//  El valor se guarda en NVS y se restaura al arrancar.
// -------------------------------------------------------------
void        flexAudioSetVolume(uint8_t vol0to100);
uint8_t     flexAudioVolume();
void        flexAudioSetMuted(bool muted);
bool        flexAudioMuted();
