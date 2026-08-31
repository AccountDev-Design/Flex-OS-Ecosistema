#pragma once
#include "Arduino.h"

typedef enum { WIFI_MODE_NULL=0, WIFI_STA, WIFI_AP, WIFI_AP_STA, WIFI_OFF=WIFI_MODE_NULL } wifi_mode_t;
#define WIFI_AUTH_OPEN 0
typedef enum { WL_IDLE_STATUS=0, WL_NO_SSID_AVAIL, WL_SCAN_COMPLETED, WL_CONNECTED, WL_CONNECT_FAILED, WL_CONNECTION_LOST, WL_DISCONNECTED } wl_status_t;

class WiFiClient {
public:
  virtual ~WiFiClient(){}
  virtual int connect(const char*, uint16_t){ return 0; }
  virtual void stop(){}
  virtual uint8_t connected(){ return 0; }
  virtual int available(){ return 0; }
  virtual int read(uint8_t*, size_t){ return 0; }
  virtual size_t write(const uint8_t*, size_t n){ return n; }
  void setTimeout(uint32_t){}
};

class __FlexWiFi {
public:
  bool setPins(int8_t, int8_t, int8_t, int8_t, int8_t, int8_t, int8_t){ return true; }
  bool mode(wifi_mode_t){ return true; }
  wifi_mode_t getMode(){ return WIFI_STA; }
  int begin(const char*, const char* = nullptr){ return 0; }
  bool disconnect(bool a=false, bool b=false){ (void)a;(void)b; return true; }
  wl_status_t status(){ return WL_DISCONNECTED; }
  IPAddress localIP(){ return IPAddress(); }
  String SSID(){ return String(""); }
  String SSID(int){ return String(""); }
  int RSSI(){ return -70; }
  int RSSI(int){ return -70; }
  int encryptionType(int){ return 3; }
  int scanNetworks(bool async=false){ (void)async; return 0; }
  void scanDelete(){}
  void setSleep(bool){}
  void setAutoReconnect(bool){}
  String macAddress(){ return String("00:00:00:00:00:00"); }
};
extern __FlexWiFi WiFi;
