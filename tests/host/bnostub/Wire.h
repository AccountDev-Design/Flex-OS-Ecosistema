#pragma once
// #############################################################
//  BUS I2C SIMULADO CON UN GY-BNO085 AL OTRO LADO
//  ------------------------------------------------------------
//  Habla SHTP de verdad -- cabecera de 4 bytes, canales, el reporte
//  0xF1 del Product ID y los informes 0x01/0x02/0x03/0x05 -- porque
//  la prueba tiene que ejercitar el dialogo real del driver, no una
//  maqueta suya.
//
//  Y sobre todo modela los tres estados en los que el bus deja de ser
//  un bus, que son los que provocaban el bloqueo del sistema:
//    · WIRE_ABSENT  : nadie contesta (NACK inmediato, coste ~0)
//    · WIRE_WEDGED  : el modulo se ha retirado y tira de SDA a masa;
//                     CADA transaccion se va al plazo de espera
//    · WIRE_GARBAGE : el modulo contesta pero devuelve 0xFF: las
//                     cabeceras SHTP dicen "vienen 32763 bytes"
//
//  Cada transaccion suma su coste al reloj virtual y al contador, asi
//  que la prueba puede afirmar cosas exactas: "esta vuelta no toco el
//  bus", "esta vuelta no gasto mas de X ms".
// #############################################################
#include "Arduino.h"

//    · WIRE_BADLEN  : el peor caso de todos. El modulo contesta con una
//                     cabecera de canal VALIDO pero longitud absurda, que es
//                     lo que deja pasar cualquier comprobacion de canal: sin
//                     un tope de longitud, vaciar ese paquete son mas de mil
//                     transacciones I2C seguidas dentro de UNA sola llamada.
enum WireMode { WIRE_PRESENT = 0, WIRE_ABSENT, WIRE_WEDGED, WIRE_GARBAGE, WIRE_BADLEN };

extern int      gWireMode;        // WireMode
extern uint32_t gWireTx;          // transacciones I2C desde el ultimo reset
extern uint32_t gWireMsCost;      // ms de reloj virtual gastados en el bus
extern uint16_t gWireTimeoutMs;   // plazo de espera configurado (Wire.setTimeOut)
extern uint8_t  gBnoI2cAddr;      // direccion en la que contesta el modulo simulado

// --- Modelo del sensor (definido en test_imu.cpp) ---
void     fakeBnoReset();               // vuelve al estado de recien alimentado
void     fakeBnoOnWrite(const uint8_t* p, size_t n);   // paquete SHTP escrito por el maestro
size_t   fakeBnoRead(uint8_t* out, size_t want);       // sirve `want` bytes al maestro
bool     fakeBnoQueueReports();        // encola un informe de cada sensor activado

class TwoWire {
public:
  bool begin(int sda = -1, int scl = -1, uint32_t freq = 0){
    (void)sda; (void)scl; (void)freq; return true;
  }
  bool end(){ return true; }
  void setClock(uint32_t){}
  void setTimeOut(uint16_t ms){ gWireTimeoutMs = ms; }
  uint16_t getTimeOut(){ return gWireTimeoutMs; }

  void beginTransmission(uint8_t addr){ _addr = addr; _txn = 0; }
  size_t write(uint8_t b){ if(_txn < sizeof(_tx)) _tx[_txn++] = b; return 1; }
  size_t write(const uint8_t* p, size_t n){ for(size_t i = 0; i < n; i++) write(p[i]); return n; }

  uint8_t endTransmission(bool stop = true){
    (void)stop;
    if(!_charge()) return 2;                       // NACK
    if(_addr == gBnoI2cAddr && _txn) fakeBnoOnWrite(_tx, _txn);
    _txn = 0;
    return 0;
  }

  uint8_t requestFrom(uint8_t addr, uint8_t n){ return (uint8_t)requestFrom((int)addr, (int)n); }
  int requestFrom(int addr, int n){
    _addr = (uint8_t)addr;
    _rxn = _rxi = 0;
    if(!_charge()) return 0;
    if(n < 0) n = 0;
    if((size_t)n > sizeof(_rx)) n = (int)sizeof(_rx);
    if(gWireMode == WIRE_GARBAGE){ memset(_rx, 0xFF, (size_t)n); _rxn = (size_t)n; return n; }
    if(gWireMode == WIRE_BADLEN){
      memset(_rx, 0x5A, (size_t)n);
      if(n >= 4){ _rx[0] = 0xFB; _rx[1] = 0x7F; _rx[2] = 3; _rx[3] = 0; }  // canal 3, 32763 B
      _rxn = (size_t)n;
      return n;
    }
    _rxn = fakeBnoRead(_rx, (size_t)n);
    return (int)_rxn;
  }
  int available(){ return (int)(_rxn - _rxi); }
  int read(){ return _rxi < _rxn ? (int)_rx[_rxi++] : 0; }

private:
  // Toda transaccion cuesta: una sana, practicamente nada; una contra un bus
  // trabado, el plazo de espera entero. Ese coste es el que congelaba el
  // sistema, asi que la prueba tiene que poder medirlo.
  bool _charge(){
    gWireTx++;
    if(gWireMode == WIRE_WEDGED){
      gWireMsCost += gWireTimeoutMs;
      gFakeMs     += gWireTimeoutMs;
      return false;
    }
    if(gWireMode == WIRE_ABSENT) return false;     // NACK inmediato: no cuesta tiempo
    if(_addr != gBnoI2cAddr) return false;         // otra direccion del bus: no es el IMU
    return true;
  }
  uint8_t _addr = 0;
  uint8_t _tx[64]; size_t _txn = 0;
  uint8_t _rx[64]; size_t _rxn = 0, _rxi = 0;
};
extern TwoWire Wire;
