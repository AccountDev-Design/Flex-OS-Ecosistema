// #############################################################
//  NVS EN MEMORIA (solo para las pruebas de host de Flex Weather)
//  ------------------------------------------------------------
//  Mismo motivo que vaultstub/: el doble de Preferences del arnes
//  general (inostub) no guarda nada, y aqui hay que comprobar
//  justo lo contrario -- que la CACHE del pronostico y la lista de
//  ubicaciones sobreviven a un "reinicio" (volver a llamar a
//  wxLoadCache() / wxLoadLocs()).
//
//  La diferencia con vaultstub es el TAMANO del valor: un
//  WeatherState completo son ~1,3 KB, muy por encima de los 128 B
//  que necesita la boveda. Se separan en vez de agrandar aquel
//  para no cambiar el consumo de las pruebas que ya existen.
//
//  SIN CONTENEDORES DE LA BIBLIOTECA ESTANDAR, por la misma razon
//  documentada en vaultstub: Arduino.h define min y max como
//  macros y eso rompe la compilacion de <map> y <string>.
//
//  No se compila NUNCA para la placa.
// #############################################################
#pragma once
#include "Arduino.h"

#define __WXPREFS_MAX_KEYS 24
#define __WXPREFS_KEY_LEN  40
#define __WXPREFS_VAL_LEN  2048

struct __WxPrefsEntry {
  char          key[__WXPREFS_KEY_LEN];      // "namespace\x1fclave"
  unsigned char val[__WXPREFS_VAL_LEN];
  unsigned int  len;
  bool          used;
};

inline __WxPrefsEntry* __wxPrefsTable(){
  static __WxPrefsEntry t[__WXPREFS_MAX_KEYS];
  return t;
}
inline void prefsTestClear(){
  __WxPrefsEntry* t = __wxPrefsTable();
  for(int i = 0; i < __WXPREFS_MAX_KEYS; i++) t[i].used = false;
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
    __WxPrefsEntry* e = find(k);
    if(!e) return false;
    e->used = false;
    return true;
  }
  bool clear(){ return true; }

  size_t getBytes(const char* k, void* b, size_t n){
    __WxPrefsEntry* e = find(k);
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
  unsigned char getUChar(const char* k, unsigned char d = 0){
    unsigned char v = d;
    return getBytes(k, &v, 1) == 1 ? v : d;
  }
  size_t putUChar(const char* k, unsigned char v){ put(k, &v, 1); return 1; }
  char getChar(const char* k, char d = 0){
    char v = d;
    return getBytes(k, &v, 1) == 1 ? v : d;
  }
  size_t putChar(const char* k, char v){ put(k, &v, 1); return 1; }
  int getInt(const char* k, int d = 0){
    int v = d;
    return getBytes(k, &v, sizeof(v)) == sizeof(v) ? v : d;
  }
  size_t putInt(const char* k, int v){ put(k, &v, sizeof(v)); return sizeof(v); }

private:
  void keyOf(const char* k, char* out, size_t n){
    snprintf(out, n, "%s\x1f%s", _ns, k ? k : "");
  }
  __WxPrefsEntry* find(const char* k){
    char kk[__WXPREFS_KEY_LEN];
    keyOf(k, kk, sizeof(kk));
    __WxPrefsEntry* t = __wxPrefsTable();
    for(int i = 0; i < __WXPREFS_MAX_KEYS; i++)
      if(t[i].used && !strcmp(t[i].key, kk)) return &t[i];
    return NULL;
  }
  void put(const char* k, const void* b, size_t n){
    if(n > __WXPREFS_VAL_LEN) n = __WXPREFS_VAL_LEN;
    __WxPrefsEntry* e = find(k);
    if(!e){
      __WxPrefsEntry* t = __wxPrefsTable();
      for(int i = 0; i < __WXPREFS_MAX_KEYS; i++)
        if(!t[i].used){ e = &t[i]; e->used = true; keyOf(k, e->key, sizeof(e->key)); break; }
    }
    if(!e) return;                            // tabla llena: la prueba lo vera
    memcpy(e->val, b, n);
    e->len = (unsigned int)n;
  }
  char _ns[16] = {0};
  bool _ro = false;
};
