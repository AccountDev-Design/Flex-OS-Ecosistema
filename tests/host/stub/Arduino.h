#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
unsigned long millis();
void delay(unsigned long ms);

// Serial: los modulos del proyecto lo usan para trazas de diagnostico
// (igual que FlexOS_OTA.cpp). Aqui es un sumidero silencioso salvo que
// se defina FLEXOS_TEST_VERBOSE, para no ensuciar la salida de los tests.
class __FlexSerial {
public:
  void begin(unsigned long) {}
  template <typename T> void print(T)   {}
  template <typename T> void println(T v){
#ifdef FLEXOS_TEST_VERBOSE
    fputs("[serial] ", stderr); __emit(v); fputc('\n', stderr);
#else
    (void)v;
#endif
  }
  void println() {}
  int printf(const char* fmt, ...) { (void)fmt; return 0; }
private:
#ifdef FLEXOS_TEST_VERBOSE
  static void __emit(const char* s){ fputs(s, stderr); }
  template <typename T> static void __emit(T){}
#endif
};
extern __FlexSerial Serial;

// F(): en Arduino mete la cadena en flash. En el PC no hay flash, asi
// que es la identidad -- misma firma, mismo uso en el codigo.
#define F(x) (x)
