#pragma once
#include "Arduino.h"
#include <string.h>
#include <stdlib.h>

// #############################################################
//  Preferences (NVS) simulada CON ALMACENAMIENTO REAL EN MEMORIA
//  ------------------------------------------------------------
//  Antes este doble no guardaba nada: cada get devolvia el valor
//  por defecto. Eso hacia imposible probar en el PC lo unico que
//  de verdad importa de la NVS -- que lo guardado se recupera, que
//  una clave vieja se MIGRA a la nueva y que un valor corrupto se
//  descarta --, que es justo donde se rompen los escritorios de los
//  usuarios al actualizar.
//
//  SIN STL A PROPOSITO: Arduino.h define min/max como MACROS (igual
//  que arduino-esp32), y <map>/<vector> no compilan con ellas
//  definidas. Un almacen de tamano fijo sobra para las pruebas.
//
//  flexPrefsWipe() vacia la particion: es lo que usa una prueba
//  para simular un primer arranque de fabrica.
// #############################################################
#define FLEXPREF_MAX_KEYS 128
#define FLEXPREF_KEY_MAX   48
#define FLEXPREF_VAL_MAX  256

struct FlexPrefEntry {
  char          key[FLEXPREF_KEY_MAX];
  bool          used;
  unsigned char blob[FLEXPREF_VAL_MAX];
  unsigned      blobLen;                 // 0 = no es blob
  long long     num;
  bool          hasNum;
  char          str[FLEXPREF_VAL_MAX];
  bool          hasStr;
};
inline FlexPrefEntry* flexPrefsAll(){ static FlexPrefEntry e[FLEXPREF_MAX_KEYS]; return e; }
inline void flexPrefsWipe(){ memset(flexPrefsAll(), 0, sizeof(FlexPrefEntry) * FLEXPREF_MAX_KEYS); }
inline FlexPrefEntry* flexPrefsFind(const char* k, bool create){
  FlexPrefEntry* e = flexPrefsAll();
  for(int i = 0; i < FLEXPREF_MAX_KEYS; i++)
    if(e[i].used && !strcmp(e[i].key, k)) return &e[i];
  if(!create) return 0;
  for(int i = 0; i < FLEXPREF_MAX_KEYS; i++)
    if(!e[i].used){
      memset(&e[i], 0, sizeof(e[i]));
      snprintf(e[i].key, sizeof(e[i].key), "%s", k);
      e[i].used = true;
      return &e[i];
    }
  return 0;
}

class Preferences {
  char ns[24];
  bool ro;
  void mk(const char* k, char* out, size_t n) const { snprintf(out, n, "%s/%s", ns, k ? k : ""); }
public:
  Preferences(){ ns[0] = 0; ro = false; }
  bool begin(const char* n, bool readOnly = false){
    snprintf(ns, sizeof(ns), "%s", n ? n : "");
    ro = readOnly;
    return true;
  }
  void end(){}
  String getString(const char* k, const char* d = ""){
    char q[FLEXPREF_KEY_MAX]; mk(k, q, sizeof(q));
    FlexPrefEntry* e = flexPrefsFind(q, false);
    return String(e && e->hasStr ? e->str : (d ? d : ""));
  }
  size_t putString(const char* k, const char* v){
    if(ro) return 0;
    char q[FLEXPREF_KEY_MAX]; mk(k, q, sizeof(q));
    FlexPrefEntry* e = flexPrefsFind(q, true);
    if(!e) return 0;
    snprintf(e->str, sizeof(e->str), "%s", v ? v : "");
    e->hasStr = true;
    return v ? strlen(v) : 0;
  }
  long long getNum(const char* k, long long d){
    char q[FLEXPREF_KEY_MAX]; mk(k, q, sizeof(q));
    FlexPrefEntry* e = flexPrefsFind(q, false);
    return (e && e->hasNum) ? e->num : d;
  }
  size_t putNum(const char* k, long long v, size_t sz){
    if(ro) return 0;
    char q[FLEXPREF_KEY_MAX]; mk(k, q, sizeof(q));
    FlexPrefEntry* e = flexPrefsFind(q, true);
    if(!e) return 0;
    e->num = v; e->hasNum = true;
    return sz;
  }
  int    getInt(const char* k, int d = 0){ return (int)getNum(k, d); }
  size_t putInt(const char* k, int v){ return putNum(k, v, 4); }
  unsigned int getUInt(const char* k, unsigned int d = 0){ return (unsigned int)getNum(k, (long long)d); }
  size_t putUInt(const char* k, unsigned int v){ return putNum(k, (long long)v, 4); }
  long long getLong64(const char* k, long long d = 0){ return getNum(k, d); }
  size_t putLong64(const char* k, long long v){ return putNum(k, v, 8); }
  unsigned long long getULong64(const char* k, unsigned long long d = 0){ return (unsigned long long)getNum(k, (long long)d); }
  size_t putULong64(const char* k, unsigned long long v){ return putNum(k, (long long)v, 8); }
  bool   getBool(const char* k, bool d = false){ return getNum(k, d ? 1 : 0) != 0; }
  size_t putBool(const char* k, bool v){ return putNum(k, v ? 1 : 0, 1); }
  // Se comporta como la NVS real: si la clave no existe devuelve 0 y NO toca el
  // buffer; si existe con OTRO tamano devuelve ese tamano real, que es como una
  // migracion distingue "no esta" de "esta pero es de otra version".
  size_t getBytes(const char* k, void* b, size_t n){
    char q[FLEXPREF_KEY_MAX]; mk(k, q, sizeof(q));
    FlexPrefEntry* e = flexPrefsFind(q, false);
    if(!e || !e->blobLen) return 0;
    if(e->blobLen > n) return e->blobLen;
    if(b) memcpy(b, e->blob, e->blobLen);
    return e->blobLen;
  }
  size_t putBytes(const char* k, const void* b, size_t n){
    if(ro) return 0;
    if(n > FLEXPREF_VAL_MAX) n = FLEXPREF_VAL_MAX;
    char q[FLEXPREF_KEY_MAX]; mk(k, q, sizeof(q));
    FlexPrefEntry* e = flexPrefsFind(q, true);
    if(!e) return 0;
    memcpy(e->blob, b, n);
    e->blobLen = (unsigned)n;
    return n;
  }
  bool   isKey(const char* k){
    char q[FLEXPREF_KEY_MAX]; mk(k, q, sizeof(q));
    return flexPrefsFind(q, false) != 0;
  }
  bool   remove(const char* k){
    char q[FLEXPREF_KEY_MAX]; mk(k, q, sizeof(q));
    FlexPrefEntry* e = flexPrefsFind(q, false);
    if(e) memset(e, 0, sizeof(*e));
    return true;
  }
  bool   clear(){ flexPrefsWipe(); return true; }
  size_t freeEntries(){ return FLEXPREF_MAX_KEYS; }
};
