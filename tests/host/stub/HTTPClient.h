#pragma once
#include "WiFi.h"
class HTTPClient {
public:
  bool begin(WiFiClient& c, const char* url) { (void)c; (void)url; return true; }
  void setTimeout(uint16_t ms) { (void)ms; }
  void setConnectTimeout(int32_t ms) { (void)ms; }
  int  GET() { return -1; }
  void end() {}
};
