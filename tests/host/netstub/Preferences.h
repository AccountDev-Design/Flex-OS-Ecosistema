#pragma once
// NVS simulada con la SEMANTICA EXACTA de arduino-esp32 3.2.1 (ver README).
#include "Arduino.h"
#include <map>
#include <vector>

struct NetNvsValue {
  int type = 0;                 // 1 = numero, 2 = cadena, 3 = blob (como la NVS: el tipo es parte de la clave)
  unsigned long long num = 0;
  std::string str;
  std::vector<uint8_t> blob;
};
std::map<std::string, NetNvsValue>& netstubNvs();
unsigned& netstubNvsWriteCount();
bool& netstubNvsBroken();

class Preferences {
public:
  bool begin(const char* name, bool readOnly = false);
  void end(){ started_ = false; }
  bool clear();
  bool remove(const char* key);
  bool isKey(const char* key);

  size_t putUChar(const char* key, uint8_t v){ return putNum(key, v, 1); }
  size_t putBool(const char* key, bool v){ return putNum(key, v ? 1 : 0, 1); }
  size_t putUInt(const char* key, uint32_t v){ return putNum(key, v, 4); }
  size_t putInt(const char* key, int32_t v){ return putNum(key, (uint32_t)v, 4); }
  size_t putULong64(const char* key, uint64_t v){ return putNum(key, v, 8); }
  uint8_t  getUChar(const char* key, uint8_t d = 0){ return (uint8_t)getNum(key, d); }
  bool     getBool(const char* key, bool d = false){ return getNum(key, d ? 1 : 0) != 0; }
  uint32_t getUInt(const char* key, uint32_t d = 0){ return (uint32_t)getNum(key, d); }
  int32_t  getInt(const char* key, int32_t d = 0){ return (int32_t)(uint32_t)getNum(key, (uint32_t)d); }
  uint64_t getULong64(const char* key, uint64_t d = 0){ return getNum(key, d); }

  // putString: strlen(value). getString(char*): longitud CON terminador, 0 si
  // no existe o no cabe (y en ese caso el buffer no se toca).
  size_t putString(const char* key, const char* value);
  size_t putString(const char* key, const String& value){ return putString(key, value.c_str()); }
  size_t getString(const char* key, char* value, size_t maxLen);
  String getString(const char* key, const String defaultValue = String());

  size_t putBytes(const char* key, const void* value, size_t len);
  size_t getBytes(const char* key, void* buf, size_t maxLen);
  size_t getBytesLength(const char* key);
private:
  bool valid(const char* key) const;
  std::string full(const char* key) const { return ns_ + "\x1f" + key; }
  size_t putNum(const char* key, unsigned long long v, size_t width);
  unsigned long long getNum(const char* key, unsigned long long d);
  std::string ns_;
  bool started_ = false, ro_ = false;
};
