#pragma once
#include "WiFi.h"
class WiFiUDP {
public:
  uint8_t begin(uint16_t port){ (void)port; return 1; }
  void    stop(){}
  int     beginPacket(const char* host, uint16_t port){ (void)host;(void)port; return 1; }
  int     endPacket(){ return 1; }
  size_t  write(const uint8_t*, size_t n){ return n; }
  int     parsePacket(){ return 0; }
  int     read(uint8_t*, size_t){ return 0; }
  int     available(){ return 0; }
};
