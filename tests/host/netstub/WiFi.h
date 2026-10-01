#pragma once
#include "Arduino.h"
typedef enum { WL_IDLE_STATUS = 0, WL_NO_SSID_AVAIL = 1, WL_CONNECTED = 3, WL_CONNECT_FAILED = 4,
               WL_CONNECTION_LOST = 5, WL_DISCONNECTED = 6 } wl_status_t;

// Cliente TCP minimo: lo que usan los modulos para leer una respuesta.
class WiFiClient : public Stream {
public:
  virtual uint8_t connected(){ return 0; }
  virtual void stop() {}
  int available() override { return 0; }
  int read() override { return -1; }
  virtual int read(uint8_t* buf, size_t n){ return (int)readBytes(buf, n); }
  size_t write(uint8_t) override { return 1; }
  void setTimeout(uint32_t s){ (void)s; }
};

class __FlexWiFi {
public:
  wl_status_t status();
};
extern __FlexWiFi WiFi;
