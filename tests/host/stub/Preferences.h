#pragma once
#include "Arduino.h"

// Superficie minima de Preferences (NVS) y de String: solo lo que usa
// FlexOS_Browser_Bridge.h. Las firmas son las de arduino-esp32.
class String {
public:
  String() {}
  String(const char* s){ if(s){ size_t n = strlen(s); if(n > sizeof(buf) - 1) n = sizeof(buf) - 1; memcpy(buf, s, n); buf[n] = 0; } }
  const char* c_str() const { return buf; }
  size_t length() const { return strlen(buf); }
  void toCharArray(char* out, size_t n) const { snprintf(out, n, "%s", buf); }
private:
  char buf[256] = {0};
};

class Preferences {
public:
  bool   begin(const char* ns, bool ro = false){ (void)ns; (void)ro; return true; }
  void   end(){}
  String getString(const char* key, const char* def = ""){ (void)key; return String(def); }
  size_t putString(const char* key, const char* val){ (void)key; return val ? strlen(val) : 0; }
  int    getInt(const char* key, int def = 0){ (void)key; return def; }
  size_t putInt(const char* key, int v){ (void)key; (void)v; return 4; }
  bool   clear(){ return true; }
};
