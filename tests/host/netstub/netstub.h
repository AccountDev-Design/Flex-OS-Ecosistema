#pragma once
// Control de la red y la NVS simuladas desde la prueba.
#include "HTTPClient.h"
#include "Preferences.h"
#include <functional>

struct NetRequest {
  std::string method, url, body;
  std::map<std::string, std::string> headers;   // nombres en minusculas
  bool tlsInsecure = false;                      // se llamo a setInsecure()
  const char* tlsCa = nullptr;                   // setCACert()
  bool https = false;
  unsigned long at = 0;                          // millis() al enviarla
};
struct NetResponse {
  int status = 200;                              // < 0 = fallo de transporte (HTTPC_ERROR_*)
  std::string body;
  std::map<std::string, std::string> headers;    // nombres en minusculas
  size_t cutAt = (size_t)-1;                     // la conexion se corta tras N bytes del cuerpo
  bool chunked = false;                          // sin Content-Length (getSize() == -1)
};

extern std::function<NetResponse(const NetRequest&)> gNetHandler;
extern std::vector<NetRequest> gNetLog;
extern bool gNetWifi;
extern unsigned long gNetNowMs;
extern unsigned gNetTaskCreates;
extern size_t gNetPsNow, gNetPsPeak;    // PSRAM reservada ahora / pico (heap_caps_malloc)
extern size_t gNetPsFailAbove;          // >0: heap_caps_malloc falla por encima de esto

void netstubReset();            // red, registro y reloj a cero (la NVS NO se toca)
void netstubNvsWipe();          // NVS de fabrica
void netstubAdvance(unsigned long ms);
