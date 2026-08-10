#pragma once
#include "WiFi.h"
class WiFiClientSecure : public WiFiClient {
public:
  void setInsecure(){}
  void setCACert(const char*){}
  void setHandshakeTimeout(uint32_t){}
};
