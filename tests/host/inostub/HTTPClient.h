#pragma once
#include "WiFi.h"
#define HTTP_CODE_OK 200
class HTTPClient {
public:
  bool begin(WiFiClient&, const char*){ return true; }
  bool begin(const char*){ return true; }
  void end(){}
  void setTimeout(uint16_t){}
  void setConnectTimeout(int32_t){}
  void setReuse(bool){}
  void setUserAgent(const String&){}
  void addHeader(const char*, const char*){}
  void useHTTP10(bool){}
  bool setURL(const char*){ return true; }
  const char* errorToString(int){ return "err"; }
  int GET(){ return -1; }
  int getSize(){ return -1; }
  WiFiClient* getStreamPtr(){ return nullptr; }
  WiFiClient& getStream(){ static WiFiClient c; return c; }
  String getString(){ return String(""); }
  String header(const char*){ return String(""); }
  void collectHeaders(const char**, size_t){}
};
