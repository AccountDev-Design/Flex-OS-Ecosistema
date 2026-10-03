// #############################################################
//  linkstub/  --  lo MINIMO del core de arduino-esp32 para ejecutar en el PC
//  la tarea de red REAL de Flex Phone (FlexOS_FlexPhone_WiFi.h) sobre
//  sockets POSIX de verdad y con HILOS de verdad.
//
//  A diferencia de stub/ e inostub/ (que no hacen nada), aqui WiFiClient
//  reproduce la semantica de NetworkClient de arduino-esp32 3.2.1 que importa
//  para este fallo: connect(host,port,ms) con plazo, connected() por
//  recv(MSG_PEEK), write() que puede devolver MENOS de lo pedido, y
//  available() por FIONREAD. Ver tests/host/test_flexphone_wifi_e2e.cpp.
// #############################################################
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
unsigned long millis();
void delay(unsigned long ms);

class __FlexSerial {
public:
  void begin(unsigned long){}
  int printf(const char* fmt, ...) __attribute__((format(printf, 2, 3)));
  void println(const char*);
  void println(){}
};
extern __FlexSerial Serial;
#define F(x) (x)
