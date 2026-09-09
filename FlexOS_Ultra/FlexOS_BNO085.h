// #############################################################
// ##  FlexOS · IMU  ·  TENSTAR GY-BNO085 (BNO085 9-DOF)  ·  ESP32-P4
// ##  Placa GUITION JC4880P443C_I_W
// #############################################################
//
//  CABLEADO REAL (cabecera de expansion del P4, ver "PIN DEFINITIONS")
//  ------------------------------------------------------------
//    GY-BNO085  VCC  -> 3V3
//    GY-BNO085  GND  -> GND
//    GY-BNO085  SCL  -> I2C_SCL   (GPIO8, PIN_TP_SCL)
//    GY-BNO085  SDA  -> I2C_SDA   (GPIO7, PIN_TP_SDA)
//  Los pads PS1/PS0 del modulo eligen el protocolo: los dos a masa
//  (valor de fabrica) es I2C, que es el unico modo que soporta este
//  driver. La direccion es 0x4A con ADR/AD0 a masa y 0x4B con AD0 a
//  3V3; se prueban las dos, en ese orden.
//
//  EL BUS I2C ES EL MISMO QUE EL DEL TACTIL
//  ----------------------------------------
//  GPIO7/GPIO8 son los pines del GT911 y del codec ES8311. Por eso
//  este modulo sigue la MISMA regla que FlexOS_Audio:
//    · no crea su propio bus ni llama a Wire.begin(): usa el Wire que
//      ya inicializo flexTouchInit() para el tactil;
//    · no habla con el sensor desde otra tarea. Todas sus llamadas
//      salen del hilo que sondea el tactil (loopTask), igual que la
//      deteccion I2C incremental del sistema. Compartir un bus I2C
//      entre dos tareas sin proteccion es exactamente lo que corrompe
//      el bus y cuelga el panel.
//
//  POR QUE NO SE USA UNA LIBRERIA
//  ------------------------------
//  El proyecto no lleva ninguna libreria de BNO08x, y las disponibles
//  arrastran su propia capa de transporte y su propio bus. Aqui hace
//  falta justo lo contrario: compartir el Wire del sistema y no
//  bloquear nunca el bucle. Lo que se implementa es el minimo de SHTP
//  (transporte) y SH-2 (control) que hacen falta -- identificacion,
//  activacion de cuatro informes y lectura -- con el mismo criterio
//  que el codec de audio: registros justificados, nada copiado a
//  ciegas.
//
//  HONESTIDAD SOBRE LA DISPONIBILIDAD
//  ----------------------------------
//  flexBnoAvailable() NO devuelve true "porque la placa podria llevar
//  IMU". Devuelve true solo si:
//    1) alguien contesta en 0x4A o 0x4B, Y
//    2) el sensor responde al Product ID (reporte 0xF1) -- o sea, es
//       un BNO08x y el bus funciona en los dos sentidos, Y
//    3) esta entregando informes de verdad ahora mismo.
//  Y cada sensor por separado (acelerometro, giroscopio, magnetometro
//  y sensor fusion) solo se marca como comprobado cuando ha llegado un
//  informe SUYO con estado valido. La interfaz no dibuja un tick sin
//  eso detras.
// #############################################################
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Direcciones posibles del modulo (AD0/ADR a masa o a 3V3).
#define FLEXBNO_ADDR_LOW    0x4A
#define FLEXBNO_ADDR_HIGH   0x4B

// Estado del enlace con el sensor. Es lo unico que la interfaz
// necesita saber para decidir que pantalla ensena.
enum {
  FLEXBNO_ST_ABSENT = 0,   // nadie contesta en el bus
  FLEXBNO_ST_PROBING,      // contesta: reset enviado, esperando al sensor
  FLEXBNO_ST_IDENT,        // Product ID pedido, esperando el reporte 0xF1
  FLEXBNO_ST_CONFIG,       // identificado: activando los cuatro informes
  FLEXBNO_ST_READY,        // entregando informes
  FLEXBNO_ST_LOST          // estuvo listo y dejo de responder
};

// Comprobaciones individuales (mascara de bits). Una comprobacion solo
// se enciende cuando ha llegado un informe REAL de ese sensor.
#define FLEXBNO_CHK_I2C     0x01   // Product ID contestado
#define FLEXBNO_CHK_ACCEL   0x02
#define FLEXBNO_CHK_GYRO    0x04
#define FLEXBNO_CHK_MAG     0x08
#define FLEXBNO_CHK_FUSION  0x10   // rotation vector (sensor fusion)
#define FLEXBNO_CHK_ALL     0x1F

// Cadencia de los informes. 50 Hz es de sobra para detectar una caida
// (el evento entero dura entre 300 y 800 ms) y deja el bus I2C
// practicamente libre para el tactil, que es quien manda.
#define FLEXBNO_REPORT_HZ   50

// -------------------------------------------------------------
//  Arranque. Llamar DESPUES de Wire.begin() (el tactil ya lo hace).
//  NO bloquea: sondea el bus y, si alguien contesta, deja el modulo
//  en PROBING; el resto del dialogo lo lleva flexBnoTick().
//  Es seguro llamarla aunque no haya modulo: se queda en ABSENT.
// -------------------------------------------------------------
bool        flexBnoBegin();

// -------------------------------------------------------------
//  Tick. Llamar desde el MISMO hilo que sondea el tactil, en cada
//  vuelta de loop(). Lee como mucho unos pocos paquetes por vuelta,
//  asi que no anade latencia perceptible ni fuerza el watchdog.
// -------------------------------------------------------------
void        flexBnoTick(uint32_t nowMs);

// Vuelve a sondear el bus desde cero (lo usa la pantalla de Deteccion
// de caidas al entrar, y el reintento manual). No bloquea.
void        flexBnoRescan();

// Suelta el sensor: desactiva los informes si puede y vuelve a ABSENT.
// Se llama al salir de la funcion para no dejar al BNO085 emitiendo a
// 50 Hz sobre el bus del tactil cuando ya no lo lee nadie.
void        flexBnoStop();

// ---- Estado ----
int         flexBnoState();        // FLEXBNO_ST_*
bool        flexBnoPresent();      // alguien contesta en el bus
bool        flexBnoAvailable();    // READY: informes reales llegando
uint8_t     flexBnoChecks();       // mascara FLEXBNO_CHK_*
uint8_t     flexBnoAddr();         // direccion en uso (0 si no hay)
const char* flexBnoError();        // motivo legible; nunca NULL

// Version de firmware del sensor, leida del reporte 0xF1. Solo valida
// con FLEXBNO_CHK_I2C encendido.
uint8_t     flexBnoSwMajor();
uint8_t     flexBnoSwMinor();
uint32_t    flexBnoSwPart();

// ---- Lecturas ----
// Todas escriben en el array de 3 (o 4) y devuelven false si ese
// sensor todavia no ha entregado ningun informe valido: quien llama
// NO recibe ceros que parezcan una medida.
bool        flexBnoAccel(float* xyz);   // m/s^2, con gravedad
bool        flexBnoGyro(float* xyz);    // rad/s
bool        flexBnoMag(float* xyz);     // uT
bool        flexBnoQuat(float* ijkr);   // cuaternion unitario (i,j,k,real)

// Precision declarada por el propio sensor: 0 sin fiar, 1 baja,
// 2 media, 3 alta. 0xFF = todavia sin informe.
uint8_t     flexBnoAccelAcc();
uint8_t     flexBnoGyroAcc();
uint8_t     flexBnoMagAcc();
uint8_t     flexBnoFusionAcc();

// Edad en ms del ultimo informe recibido (cualquiera). 0xFFFFFFFF si
// no ha llegado ninguno.
uint32_t    flexBnoLastReportAge(uint32_t nowMs);

// Numero de informes recibidos desde el arranque del modulo. Lo usa la
// prueba del IMU para decir cuantas muestras respaldan cada tick.
uint32_t    flexBnoReportCount();

// Cabeceo/alabeo/guiñada en grados a partir del cuaternion. Devuelve
// false sin sensor fusion. Es una conversion, no una medida nueva.
bool        flexBnoEuler(float* rollPitchYawDeg);
