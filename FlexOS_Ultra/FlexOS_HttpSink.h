#ifndef FLEXOS_HTTPSINK_H
#define FLEXOS_HTTPSINK_H

// #############################################################
//  CUERPO DE UNA RESPUESTA HTTP A UN BUFFER FIJO
//  ------------------------------------------------------------
//  HTTPClient::writeToStream() descodifica "chunked" y escribe aqui. Lo que
//  no cabe se descarta y overflow() lo dice: una respuesta mas grande de lo
//  esperado es una respuesta que no se acepta, nunca una reserva sin limite
//  (HTTPClient::getString() crece lo que mande el otro lado).
//
//  Lo usan Flex Cloud (FlexOS_Cloud.cpp) y Flex Storage
//  (FlexOS_StorageLink.cpp).
// #############################################################
#include <Arduino.h>
#include <string.h>

class FlexBufSink : public Stream {
 public:
  FlexBufSink(char* b, size_t cap) : b_(b), cap_(cap), n_(0), over_(false) {}
  size_t write(uint8_t c) override { if(n_ + 1 >= cap_){ over_ = true; return 0; } b_[n_++] = (char)c; return 1; }
  size_t write(const uint8_t* p, size_t n) override {
    if(n_ + n >= cap_){ over_ = true; n = n_ + 1 < cap_ ? cap_ - 1 - n_ : 0; }
    memcpy(b_ + n_, p, n); n_ += n; return n;
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}
  size_t len() const { return n_; }
  bool overflow() const { return over_; }
  void terminate(){ b_[n_ < cap_ ? n_ : cap_ - 1] = 0; }
 private:
  char* b_; size_t cap_, n_; bool over_;
};

#endif
