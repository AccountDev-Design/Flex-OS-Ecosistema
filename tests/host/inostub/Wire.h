#pragma once
#include "Arduino.h"
// #############################################################
//  Doble del TwoWire de Arduino-ESP32 para la bateria del sketch.
//  ------------------------------------------------------------
//  Tiene lo que el sketch usa de verdad y con la MISMA firma que el
//  core: begin/end (bool en los dos), setClock, setTimeOut(uint16_t) y
//  el transporte.
//
//  Y modela una cosa mas, porque hay una prueba que la necesita: que el
//  bus se TRABE. Cuando un modulo del bus compartido pierde la
//  alimentacion en mitad de una transaccion -- retirar el GY-BNO085 en
//  caliente -- se queda tirando de SDA a masa y a partir de ahi NINGUNA
//  transaccion sale adelante, tampoco las del tactil. Con gWireWedged
//  puesto, este doble se comporta exactamente asi, que es lo que permite
//  comprobar de verdad que el tactil se recupera.
// #############################################################

// 1 = SDA a masa: ninguna transaccion sale adelante.
inline int           gWireWedged   = 0;
// Transacciones desde el ultimo reinicio del contador (las cuenta la prueba).
inline unsigned long gWireTxN      = 0;
// Byte que devuelve cualquier lectura con el bus sano. Es lo unico que hace
// falta para que gtPoll() recorra su camino completo.
inline unsigned char gWireReadByte = 0;

class TwoWire {
public:
  bool begin(int sda=-1, int scl=-1, uint32_t freq=0){ (void)sda;(void)scl;(void)freq; return true; }
  bool end(){ return true; }
  void setClock(uint32_t){}
  void setTimeOut(uint16_t ms){ _timeOut = ms; }
  uint16_t getTimeOut(){ return _timeOut; }
  void beginTransmission(uint8_t){}
  size_t write(uint8_t){ return 1; }
  size_t write(const uint8_t*, size_t n){ return n; }
  uint8_t endTransmission(bool stop = true){ (void)stop; gWireTxN++; return gWireWedged ? 2 : 0; }
  uint8_t requestFrom(uint8_t a, uint8_t n){ return (uint8_t)requestFrom((int)a, (int)n); }
  int requestFrom(int, int n){
    gWireTxN++;
    if(gWireWedged){ _avail = 0; return 0; }
    _avail = n < 0 ? 0 : n;
    return _avail;
  }
  int available(){ return _avail; }
  int read(){ if(_avail > 0) _avail--; return (int)gWireReadByte; }
private:
  uint16_t _timeOut = 50;
  int      _avail   = 0;
};
extern TwoWire Wire;
