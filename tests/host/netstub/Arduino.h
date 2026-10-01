#pragma once
// Arduino simulado con comportamiento (ver README.md). Sin macros min/max:
// los modulos que se prueban aqui no las usan y estorbarian a la STL.
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string>

unsigned long millis();
unsigned long micros();
void delay(unsigned long ms);
void yield();

#define F(x) (x)
#define PROGMEM

class String {
public:
  String() {}
  String(const char* s) : s_(s ? s : "") {}
  String(const std::string& s) : s_(s) {}
  String(int v) : s_(std::to_string(v)) {}
  String(unsigned v) : s_(std::to_string(v)) {}
  String(long v) : s_(std::to_string(v)) {}
  String(unsigned long v) : s_(std::to_string(v)) {}
  const char* c_str() const { return s_.c_str(); }
  size_t length() const { return s_.size(); }
  bool isEmpty() const { return s_.empty(); }
  bool operator==(const char* o) const { return o && s_ == o; }
  bool operator==(const String& o) const { return s_ == o.s_; }
  String& operator+=(const char* o){ s_ += (o ? o : ""); return *this; }
  String& operator+=(const String& o){ s_ += o.s_; return *this; }
  friend String operator+(const String& a, const char* b){ String r(a); r += b; return r; }
  void toCharArray(char* out, size_t n) const { if(n){ snprintf(out, n, "%s", s_.c_str()); } }
  int toInt() const { return atoi(s_.c_str()); }
  const std::string& str() const { return s_; }
private:
  std::string s_;
};

// Print/Stream minimos, con las firmas de arduino-esp32.
class Print {
public:
  virtual ~Print() {}
  virtual size_t write(uint8_t c) = 0;
  virtual size_t write(const uint8_t* buf, size_t n){ size_t k = 0; while(k < n && write(buf[k])) k++; return k; }
};
class Stream : public Print {
public:
  virtual int available() = 0;
  virtual int read() = 0;
  virtual int peek(){ return -1; }
  virtual void flush() {}
  void setTimeout(unsigned long ms){ timeout_ = ms; }
  virtual size_t readBytes(uint8_t* buf, size_t n){
    size_t k = 0;
    while(k < n){ int c = read(); if(c < 0) break; buf[k++] = (uint8_t)c; }
    return k;
  }
  size_t readBytes(char* buf, size_t n){ return readBytes((uint8_t*)buf, n); }
protected:
  unsigned long timeout_ = 1000;
};

class __FlexSerial {
public:
  void begin(unsigned long) {}
  template <typename T> void print(T) {}
  template <typename T> void println(T v){ emit(v); }
  void println() {}
  int printf(const char* fmt, ...);
private:
  void emit(const char* s);
  void emit(const String& s){ emit(s.c_str()); }
  template <typename T> void emit(T) {}
};
extern __FlexSerial Serial;

class EspClass {
public:
  uint64_t getEfuseMac(){ return 0x0000A1B2C3D4E5F6ull; }
  uint32_t getFreeHeap(){ return 400u * 1024u; }
};
extern EspClass ESP;
