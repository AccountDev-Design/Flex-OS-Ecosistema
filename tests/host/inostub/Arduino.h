#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

unsigned long millis();
unsigned long micros();
void delay(unsigned long ms);
void delayMicroseconds(unsigned long us);
void pinMode(int, int);
void digitalWrite(int, int);
int  digitalRead(int);
void yield();
long random(long);
long random(long, long);
uint32_t esp_random();
bool setCpuFrequencyMhz(uint32_t);
uint32_t getCpuFrequencyMhz();

#define HIGH 1
#define LOW  0
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2

class __FlexSerial {
public:
  void begin(unsigned long) {}
  void end() {}
  operator bool() const { return true; }
  template <typename T> void print(T)   {}
  template <typename T> void println(T) {}
  void println() {}
  int  printf(const char*, ...) { return 0; }
  int  available() { return 0; }
  int  read() { return -1; }
  void flush() {}
};
extern __FlexSerial Serial;

#define F(x) (x)
#define PROGMEM
#define pgm_read_byte(a) (*(const uint8_t*)(a))

class String {
public:
  String(){ buf[0]=0; }
  String(const char* s){ set(s); }
  String(int v){ snprintf(buf,sizeof(buf),"%d",v); }
  String(unsigned v){ snprintf(buf,sizeof(buf),"%u",v); }
  const char* c_str() const { return buf; }
  size_t length() const { return strlen(buf); }
  void toCharArray(char* out, size_t n) const { snprintf(out, n, "%s", buf); }
  bool operator==(const char* o) const { return o && !strcmp(buf,o); }
private:
  void set(const char* s){ buf[0]=0; if(s) snprintf(buf,sizeof(buf),"%s",s); }
  char buf[256];
};

class IPAddress {
public:
  IPAddress(){}
  String toString() const { return String("0.0.0.0"); }
};

// esp32-hal-ledc / esp32-hal-misc
bool     ledcAttach(uint8_t pin, uint32_t freq, uint8_t res);
bool     ledcAttachChannel(uint8_t pin, uint32_t freq, uint8_t res, uint8_t ch);
bool     ledcWrite(uint8_t pin, uint32_t duty);
bool     ledcDetach(uint8_t pin);
long     map(long, long, long, long, long);
#define constrain(a,l,h) ((a)<(l)?(l):((a)>(h)?(h):(a)))

// min/max: en arduino-esp32 son MACROS de Arduino.h. El sketch las usa
// asi (min(x0, min(x1,x2))), por eso aqui tambien son macros y no
// std::min/std::max.
#undef min
#undef max
#define min(a,b) ((a)<(b)?(a):(b))
#define max(a,b) ((a)>(b)?(a):(b))

// EspClass: en arduino-esp32 es el objeto global 'ESP'. Aqui se declara lo
// unico que usa el sketch -- el tamano de la imagen de firmware, que
// Almacenamiento ensena en su pantalla de detalle.
class EspClass {
public:
  uint32_t getSketchSize();
  uint32_t getFreeSketchSpace();
};
extern EspClass ESP;
