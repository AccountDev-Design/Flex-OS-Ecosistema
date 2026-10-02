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
  int tlsError = 0;                              // con status < 0: lo que devolveria WiFiClientSecure::lastError()
  unsigned long latencyMs = 0;                   // lo que tarda en contestar (o en fallar): el reloj avanza esto
  size_t cutAt = (size_t)-1;                     // la conexion se corta tras N bytes del cuerpo
  bool chunked = false;                          // sin Content-Length (getSize() == -1)
};

extern std::function<NetResponse(const NetRequest&)> gNetHandler;
extern std::vector<NetRequest> gNetLog;
extern bool gNetWifi;
extern bool gNetDnsOk;                  // el nombre del servidor se resuelve (flexTlsDnsOk)
extern unsigned long gNetNowMs;
extern unsigned gNetTaskCreates;
extern size_t gNetPsNow, gNetPsPeak;    // PSRAM reservada ahora / pico (heap_caps_malloc)
extern size_t gNetPsFailAbove;          // >0: heap_caps_malloc falla por encima de esto
extern size_t gNetInternalFree;         // SRAM interna libre que dice heap_caps_get_free_size(MALLOC_CAP_INTERNAL)
extern size_t gNetInternalBlock;        // y su mayor bloque contiguo
extern std::function<void()> gNetOnDelay;  // se llama tras CADA vTaskDelay (ya avanzado el reloj): la prueba mira el estado
                                          // y cambia Wi-Fi o memoria en un instante concreto de un flujo que bloquea

void netstubReset();            // red, registro y reloj a cero (la NVS NO se toca)
void netstubNvsWipe();          // NVS de fabrica
void netstubAdvance(unsigned long ms);
