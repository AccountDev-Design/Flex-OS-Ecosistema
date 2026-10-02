#pragma once
#include "WiFi.h"
#include <stdio.h>
class WiFiClientSecure : public WiFiClient {
public:
  void setInsecure(){ insecure = true; }
  void setCACert(const char* ca){ caCert = ca; }
  void setHandshakeTimeout(unsigned long s){ (void)s; }
  void setTimeout(uint32_t s){ (void)s; }
  // Como arduino-esp32 3.2.1: devuelve el codigo de mbedTLS de la ULTIMA conexion
  // fallida (0 = ninguno) y escribe su texto. La prueba lo fija desde la respuesta.
  int lastError(char* buf, const size_t size){
    if(!lastErr){ return 0; }
    if(buf && size){ snprintf(buf, size, "mbedtls error %d", lastErr); }
    return lastErr;
  }
  int lastErr = 0;
  bool insecure = false;
  const char* caCert = nullptr;
};
