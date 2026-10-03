#pragma once
#include "WiFi.h"
// El descubrimiento UDP NO se prueba aqui (lo cubre test_flexphone_discovery):
// el socket UDP simulado no recibe nada ni emite a ninguna parte.
class WiFiUDP {
public:
  uint8_t begin(uint16_t){ return 1; }
  void    stop(){}
  int     beginPacket(IPAddress, uint16_t){ return 1; }
  IPAddress remoteIP(){ return IPAddress(); }
  int     endPacket(){ return 1; }
  size_t  write(const uint8_t*, size_t n){ return n; }
  int     parsePacket(){ return 0; }
  int     read(uint8_t*, size_t){ return 0; }
};
