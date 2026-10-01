#pragma once
// HTTPClient simulado (arduino-esp32 3.2.1): cada peticion la contesta el
// manejador de la prueba (netstub.h). Mismas firmas que el real.
#include "WiFi.h"
#include "WiFiClientSecure.h"
#include <map>
#include <vector>

#define HTTP_CODE_OK                 200
#define HTTP_CODE_CREATED            201
#define HTTP_CODE_NO_CONTENT         204
#define HTTP_CODE_PARTIAL_CONTENT    206
#define HTTP_CODE_BAD_REQUEST        400
#define HTTP_CODE_UNAUTHORIZED       401
#define HTTP_CODE_FORBIDDEN          403
#define HTTP_CODE_NOT_FOUND          404
#define HTTP_CODE_CONFLICT           409
#define HTTP_CODE_GONE               410
#define HTTP_CODE_PRECONDITION_FAILED 412
#define HTTP_CODE_RANGE_NOT_SATISFIABLE 416
#define HTTP_CODE_UNPROCESSABLE_ENTITY 422
#define HTTP_CODE_TOO_MANY_REQUESTS  429
#define HTTP_CODE_INTERNAL_SERVER_ERROR 500
#define HTTP_CODE_SERVICE_UNAVAILABLE 503
#define HTTP_CODE_INSUFFICIENT_STORAGE 507

#define HTTPC_ERROR_CONNECTION_REFUSED  (-1)
#define HTTPC_ERROR_SEND_HEADER_FAILED  (-2)
#define HTTPC_ERROR_SEND_PAYLOAD_FAILED (-3)
#define HTTPC_ERROR_NOT_CONNECTED       (-4)
#define HTTPC_ERROR_CONNECTION_LOST     (-5)
#define HTTPC_ERROR_NO_STREAM           (-6)
#define HTTPC_ERROR_NO_HTTP_SERVER      (-7)
#define HTTPC_ERROR_TOO_LESS_RAM        (-8)
#define HTTPC_ERROR_ENCODING            (-9)
#define HTTPC_ERROR_STREAM_WRITE        (-10)
#define HTTPC_ERROR_READ_TIMEOUT        (-11)

typedef enum { HTTPC_DISABLE_FOLLOW_REDIRECTS, HTTPC_STRICT_FOLLOW_REDIRECTS, HTTPC_FORCE_FOLLOW_REDIRECTS } followRedirects_t;

class NetBodyClient : public WiFiClient {
public:
  void reset(const std::string& body, size_t cutAt){ body_ = body; pos_ = 0; cut_ = cutAt; }
  uint8_t connected() override { return pos_ < limit() ? 1 : 0; }
  int available() override { return (int)(limit() - pos_); }
  int read() override { return pos_ < limit() ? (uint8_t)body_[pos_++] : -1; }
  size_t readBytes(uint8_t* buf, size_t n) override {
    size_t k = limit() - pos_; if(k > n) k = n;
    memcpy(buf, body_.data() + pos_, k); pos_ += k; return k;
  }
  size_t readBytes(char* buf, size_t n) override { return readBytes((uint8_t*)buf, n); }
  bool cut() const { return cut_ < body_.size(); }
  int read(uint8_t* buf, size_t n) override { return (int)readBytes(buf, n); }
  size_t delivered() const { return pos_; }
private:
  size_t limit() const { return cut_ < body_.size() ? cut_ : body_.size(); }
  std::string body_; size_t pos_ = 0, cut_ = (size_t)-1;
};

class HTTPClient {
public:
  bool begin(WiFiClient& client, const char* url);
  bool begin(WiFiClient& client, const String& url){ return begin(client, url.c_str()); }
  void end();
  void setTimeout(uint16_t) {}
  void setConnectTimeout(int32_t) {}
  void setReuse(bool r){ reuse_ = r; }
  void useHTTP10(bool) {}
  void setFollowRedirects(followRedirects_t) {}
  void setUserAgent(const String&) {}
  void addHeader(const String& name, const String& value, bool first = false, bool replace = true);
  void collectHeaders(const char* keys[], const size_t n){ collect_.assign(keys, keys + n); }
  String header(const char* name);
  bool hasHeader(const char* name);
  int GET(){ return sendRequest("GET"); }
  int POST(uint8_t* payload, size_t size){ return sendRequest("POST", payload, size); }
  int POST(const String& p){ return sendRequest("POST", (uint8_t*)p.c_str(), p.length()); }
  int PUT(uint8_t* payload, size_t size){ return sendRequest("PUT", payload, size); }
  int sendRequest(const char* type, uint8_t* payload = nullptr, size_t size = 0);
  int sendRequest(const char* type, const String& p){ return sendRequest(type, (uint8_t*)p.c_str(), p.length()); }
  int sendRequest(const char* type, Stream* stream, size_t size = 0);
  int getSize(){ return size_; }
  WiFiClient* getStreamPtr(){ return active_ ? &body_ : nullptr; }
  WiFiClient& getStream(){ return body_; }
  String getString();
  // Copia el cuerpo a `stream`. Como el real: bytes escritos, o
  // HTTPC_ERROR_STREAM_WRITE si el destino no admite todo o el cuerpo llega
  // mas corto que su Content-Length (o la conexion se corta a mitad).
  int writeToStream(Stream* stream);
  bool connected(){ return active_ && body_.connected(); }
  static String errorToString(int e){ return String(std::string("error ") + std::to_string(e)); }
private:
  int dispatch(const char* type, const std::string& body);
  WiFiClient* client_ = nullptr;
  std::string url_;
  std::vector<std::pair<std::string, std::string>> reqHeaders_;
  std::vector<std::string> collect_;
  std::map<std::string, std::string> respHeaders_;
  NetBodyClient body_;
  int size_ = -1;
  bool active_ = false, reuse_ = false;
};
