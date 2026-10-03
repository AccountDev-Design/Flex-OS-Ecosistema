#pragma once
#include "Arduino.h"

class IPAddress {
public:
  IPAddress(){ v[0]=v[1]=v[2]=v[3]=0; }
  IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d){ v[0]=a; v[1]=b; v[2]=c; v[3]=d; }
  uint8_t operator[](int i) const { return v[i & 3]; }
private:
  uint8_t v[4];
};

typedef enum { WL_IDLE_STATUS=0, WL_NO_SSID_AVAIL, WL_SCAN_COMPLETED, WL_CONNECTED, WL_CONNECT_FAILED, WL_CONNECTION_LOST, WL_DISCONNECTED } wl_status_t;

// Semantica de NetworkClient (arduino-esp32 3.2.1), sobre un socket POSIX.
class WiFiClient {
public:
  WiFiClient(){}
  WiFiClient(const WiFiClient&) = delete;
  ~WiFiClient(){ stop(); }
  int connect(const char* host, uint16_t port, int32_t timeoutMs);
  void stop();
  uint8_t connected();
  int available();
  int read(uint8_t* buf, size_t size);
  size_t write(const uint8_t* buf, size_t size);
  int fd() const { return m_fd; }
private:
  int  m_fd = -1;
  bool m_connected = false;
};

class __FlexWiFi {
public:
  wl_status_t status();
  IPAddress localIP(){ return IPAddress(127,0,0,1); }
  IPAddress subnetMask(){ return IPAddress(255,0,0,0); }
  void setSleep(bool){}
};
extern __FlexWiFi WiFi;
