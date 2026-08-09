#pragma once
#include <stdint.h>
#include <stddef.h>

// Superficie MINIMA de WiFiClient: exactamente los metodos que usa
// FlexOS_BrowserApp.cpp, con las mismas firmas que arduino-esp32.
class WiFiClient {
public:
  virtual ~WiFiClient() {}
  virtual int  connect(const char* host, uint16_t port);
  virtual void stop();
  virtual uint8_t connected();
  virtual int  available();
  virtual int  read(uint8_t* buf, size_t size);
  virtual size_t write(const uint8_t* buf, size_t size);
  void setTimeout(uint32_t s) { (void)s; }
protected:
  // Descriptor del socket. En arduino-esp32 esto es privado y no existe
  // con este nombre; aqui hace falta porque test_net.cpp implementa
  // estos metodos sobre sockets POSIX de verdad. Los demas dobles lo
  // ignoran.
  int m_fd = -1;
};
