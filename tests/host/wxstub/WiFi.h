#pragma once
#include <stdint.h>
#include <stddef.h>
// Superficie MINIMA de la pila Wi-Fi: solo lo que toca FlexOS_Weather.cpp.
// Las pruebas no abren sockets -- lo que se verifica es el parseo -- asi que
// el doble dice siempre "sin conexion", que es un camino real del modulo.
#define WL_CONNECTED 3
class WiFiClient {
public:
  virtual ~WiFiClient() {}
  int    available(){ return 0; }
  int    readBytes(uint8_t*, size_t){ return 0; }
  bool   connected(){ return false; }
  void   setTimeout(uint32_t){}
};
class __FlexWiFi {
public:
  int status(){ return 0; }        // nunca conectado en el PC
};
extern __FlexWiFi WiFi;
