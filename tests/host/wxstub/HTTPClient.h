#pragma once
#include "WiFi.h"
#define HTTPC_ERROR_READ_TIMEOUT       (-11)
#define HTTPC_ERROR_CONNECTION_LOST    (-5)
#define HTTPC_STRICT_FOLLOW_REDIRECTS  1
#define HTTP_CODE_OK                   200
class HTTPClient {
public:
  bool begin(WiFiClient& c, const char* url){ (void)c; (void)url; return false; }
  void setTimeout(uint16_t){}
  void setConnectTimeout(int32_t){}
  void setFollowRedirects(int){}
  void setReuse(bool){}
  void useHTTP10(bool){}
  int  GET(){ return -1; }
  void end(){}
  WiFiClient* getStreamPtr(){ return nullptr; }
};
