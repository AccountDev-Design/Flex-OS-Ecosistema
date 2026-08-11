// #############################################################
//  NVS EN MEMORIA (solo para las pruebas de host de Flex Vault)
//  ------------------------------------------------------------
//  El doble de Preferences que usa el resto del arnes (inostub) no
//  guarda nada: sirve para comprobar que el sketch COMPILA. Flex
//  Vault necesita otra cosa: que lo que se escribe se pueda volver a
//  leer, porque justo eso es lo que hay que probar -- que el sobre de
//  la clave maestra, el contador de intentos y los ajustes sobreviven
//  a un "reinicio" (llamar otra vez a flexVaultBegin()).
//
//  Guarda de verdad, en una tabla fija por (namespace, clave), y
//  ofrece prefsTestClear() para simular una placa virgen.
//
//  SIN CONTENEDORES DE LA BIBLIOTECA ESTANDAR a proposito: este
//  fichero se incluye DESPUES de Arduino.h, que define min y max
//  como macros (igual que el Arduino de verdad), y esas macros
//  rompen la compilacion de <map> y <string>. Con una tabla de
//  arrays el problema no existe.
//
//  No se compila NUNCA para la placa.
// #############################################################
#pragma once
#include "Arduino.h"

#define __PREFS_MAX_KEYS 64
#define __PREFS_KEY_LEN  40
#define __PREFS_VAL_LEN  128

struct __PrefsEntry {
  char          key[__PREFS_KEY_LEN];      // "namespace\x1fclave"
  unsigned char val[__PREFS_VAL_LEN];
  unsigned int  len;
  bool          used;
};

inline __PrefsEntry* __prefsTable(){
  static __PrefsEntry t[__PREFS_MAX_KEYS];
  return t;
}
inline void prefsTestClear(){
  __PrefsEntry* t = __prefsTable();
  for(int i = 0; i < __PREFS_MAX_KEYS; i++) t[i].used = false;
}

class Preferences {
public:
  bool begin(const char* ns, bool ro = false){
    snprintf(_ns, sizeof(_ns), "%s", ns ? ns : "");
    _ro = ro;
    return true;
  }
  void end(){ _ns[0] = 0; }

  bool isKey(const char* k){ return find(k) != NULL; }
  bool remove(const char* k){
    __PrefsEntry* e = find(k);
    if(!e) return false;
    e->used = false;
    return true;
  }
  bool clear(){ return true; }
  size_t freeEntries(){ return __PREFS_MAX_KEYS; }

  String getString(const char* k, const char* d = ""){
    __PrefsEntry* e = find(k);
    if(!e) return String(d);
    char tmp[__PREFS_VAL_LEN + 1];
    unsigned int n = e->len < __PREFS_VAL_LEN ? e->len : __PREFS_VAL_LEN;
    memcpy(tmp, e->val, n);
    tmp[n] = 0;
    return String(tmp);
  }
  size_t putString(const char* k, const char* v){
    if(_ro) return 0;
    size_t n = v ? strlen(v) : 0;
    put(k, v, n);
    return n;
  }

  int getInt(const char* k, int d = 0){ return (int)getNum(k, (long long)d); }
  size_t putInt(const char* k, int v){ return putNum(k, (long long)v, 4); }
  unsigned int getUInt(const char* k, unsigned int d = 0){ return (unsigned int)getNum(k, (long long)d); }
  size_t putUInt(const char* k, unsigned int v){ return putNum(k, (long long)(unsigned long long)v, 4); }
  long long getLong64(const char* k, long long d = 0){ return getNum(k, d); }
  size_t putLong64(const char* k, long long v){ return putNum(k, v, 8); }
  unsigned long long getULong64(const char* k, unsigned long long d = 0){ return (unsigned long long)getNum(k, (long long)d); }
  size_t putULong64(const char* k, unsigned long long v){ return putNum(k, (long long)v, 8); }
  bool getBool(const char* k, bool d = false){ return getNum(k, d ? 1 : 0) != 0; }
  size_t putBool(const char* k, bool v){ return putNum(k, v ? 1 : 0, 1); }

  size_t getBytes(const char* k, void* b, size_t n){
    __PrefsEntry* e = find(k);
    if(!e) return 0;
    size_t c = e->len < n ? e->len : n;
    memcpy(b, e->val, c);
    return c;                                // como NVS: devuelve lo leido
  }
  size_t putBytes(const char* k, const void* b, size_t n){
    if(_ro) return 0;
    put(k, b, n);
    return n;
  }

private:
  void keyOf(const char* k, char* out, size_t n){
    snprintf(out, n, "%s\x1f%s", _ns, k ? k : "");
  }
  __PrefsEntry* find(const char* k){
    char kk[__PREFS_KEY_LEN];
    keyOf(k, kk, sizeof(kk));
    __PrefsEntry* t = __prefsTable();
    for(int i = 0; i < __PREFS_MAX_KEYS; i++)
      if(t[i].used && !strcmp(t[i].key, kk)) return &t[i];
    return NULL;
  }
  void put(const char* k, const void* b, size_t n){
    if(n > __PREFS_VAL_LEN) n = __PREFS_VAL_LEN;
    __PrefsEntry* e = find(k);
    if(!e){
      __PrefsEntry* t = __prefsTable();
      for(int i = 0; i < __PREFS_MAX_KEYS && !e; i++) if(!t[i].used) e = &t[i];
      if(!e) return;                         // tabla llena: la prueba lo vera
      keyOf(k, e->key, sizeof(e->key));
      e->used = true;
    }
    if(n) memcpy(e->val, b, n);
    e->len = (unsigned int)n;
  }
  long long getNum(const char* k, long long d){
    __PrefsEntry* e = find(k);
    if(!e || e->len > 8) return d;
    long long out = 0;
    memcpy(&out, e->val, e->len);
    // NVS guarda el entero tal cual, asi que un valor negativo de 4
    // bytes hay que extenderlo de signo al leerlo.
    if(e->len == 4 && (out & 0x80000000LL)) out |= ~0xFFFFFFFFLL;
    if(e->len == 1 && (out & 0x80LL))       out |= ~0xFFLL;
    return out;
  }
  size_t putNum(const char* k, long long v, size_t n){
    if(_ro) return 0;
    put(k, &v, n);
    return n;
  }

  char _ns[24] = { 0 };
  bool _ro = false;
};
