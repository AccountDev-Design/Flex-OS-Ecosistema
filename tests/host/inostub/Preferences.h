#pragma once
#include "Arduino.h"
class Preferences {
public:
  bool   begin(const char* ns, bool ro = false){ (void)ns;(void)ro; return true; }
  void   end(){}
  String getString(const char* k, const char* d = ""){ (void)k; return String(d); }
  size_t putString(const char* k, const char* v){ (void)k; return v?strlen(v):0; }
  int    getInt(const char* k, int d = 0){ (void)k; return d; }
  size_t putInt(const char* k, int v){ (void)k;(void)v; return 4; }
  unsigned int getUInt(const char* k, unsigned int d = 0){ (void)k; return d; }
  size_t putUInt(const char* k, unsigned int v){ (void)k;(void)v; return 4; }
  long long getLong64(const char* k, long long d = 0){ (void)k; return d; }
  size_t putLong64(const char* k, long long v){ (void)k;(void)v; return 8; }
  unsigned long long getULong64(const char* k, unsigned long long d = 0){ (void)k; return d; }
  size_t putULong64(const char* k, unsigned long long v){ (void)k;(void)v; return 8; }
  bool   getBool(const char* k, bool d = false){ (void)k; return d; }
  size_t putBool(const char* k, bool v){ (void)k;(void)v; return 1; }
  size_t getBytes(const char* k, void* b, size_t n){ (void)k;(void)b;(void)n; return 0; }
  size_t putBytes(const char* k, const void* b, size_t n){ (void)k;(void)b; return n; }
  bool   isKey(const char* k){ (void)k; return false; }
  bool   remove(const char* k){ (void)k; return true; }
  bool   clear(){ return true; }
  size_t freeEntries(){ return 100; }
};
