#pragma once
#include "WiFi.h"
class WiFiClientSecure : public WiFiClient {
public:
  void setInsecure(){ insecure = true; }
  void setCACert(const char* ca){ caCert = ca; }
  void setHandshakeTimeout(unsigned long s){ (void)s; }
  void setTimeout(uint32_t s){ (void)s; }
  bool insecure = false;
  const char* caCert = nullptr;
};
