// #############################################################
//  FLEX OS · SERVIDOR HTTP LOCAL  ·  protocolo (implementacion)
//  ------------------------------------------------------------
//  Ver FlexOS_HttpShare.h. Regla de este archivo: NADA que venga de la
//  red se copia sin un limite explicito, y toda cadena de salida acaba
//  en cero aunque se haya truncado.
// #############################################################
#include "FlexOS_HttpShare.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

// Comparacion sin distinguir mayusculas, escrita aqui. strcasecmp es
// POSIX y en el toolchain de la placa esta detras de una macro de
// visibilidad: depender de ella seria depender de como este configurado
// newlib ese dia. Son ocho lineas y quitan esa dependencia entera.
static int fhNCaseCmp(const char* a, const char* b, size_t n){
  for(size_t i = 0; i < n; i++){
    unsigned char x = (unsigned char)a[i], y = (unsigned char)b[i];
    if(x >= 'A' && x <= 'Z') x = (unsigned char)(x + 32);
    if(y >= 'A' && y <= 'Z') y = (unsigned char)(y + 32);
    if(x != y) return (int)x - (int)y;
    if(!x) return 0;
  }
  return 0;
}
static int fhCaseCmp(const char* a, const char* b){
  return fhNCaseCmp(a, b, strlen(a) > strlen(b) ? strlen(a) + 1 : strlen(b) + 1);
}

static inline int fhHexVal(char c){
  if(c >= '0' && c <= '9') return c - '0';
  if(c >= 'a' && c <= 'f') return c - 'a' + 10;
  if(c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

size_t flexHttpUnescape(const char* in, size_t len, char* out, size_t cap){
  if(!out || cap == 0) return 0;
  size_t w = 0;
  for(size_t i = 0; i < len && w + 1 < cap; i++){
    char c = in[i];
    if(c == '%' && i + 2 < len){
      int h = fhHexVal(in[i + 1]), l = fhHexVal(in[i + 2]);
      if(h >= 0 && l >= 0){ out[w++] = (char)((h << 4) | l); i += 2; continue; }
      // Un '%' que no lleva dos hex detras NO se interpreta: se copia tal
      // cual. Saltarselo dejaria pasar "%%2e%2e" como "..".
    }
    if(c == '+') c = ' ';
    out[w++] = c;
  }
  out[w] = 0;
  return w;
}

int flexHttpPathSafe(const char* path){
  if(!path || path[0] != '/') return 0;
  size_t n = strlen(path);
  if(n >= FLEXHTTP_PATH_MAX) return 0;
  for(size_t i = 0; i < n; i++){
    unsigned char c = (unsigned char)path[i];
    if(c < 0x20 || c == 0x7F) return 0;
    if(c == '\\') return 0;
  }
  // ".." como SEGMENTO completo. "..." o "a..b" son nombres legitimos;
  // lo que sube de directorio es el segmento exacto.
  const char* p = path;
  while(*p){
    const char* s = p;
    while(*p && *p != '/') p++;
    size_t seg = (size_t)(p - s);
    if(seg == 2 && s[0] == '.' && s[1] == '.') return 0;
    while(*p == '/') p++;
  }
  return 1;
}

int flexHttpParse(const char* buf, size_t len, FlexHttpReq* out){
  if(!buf || !out) return -1;
  memset(out, 0, sizeof(*out));
  if(len > FLEXHTTP_MAX_REQUEST) return -1;
  // Fin de cabeceras. Sin el, la peticion aun no esta entera.
  const char* end = NULL;
  for(size_t i = 0; i + 3 < len; i++)
    if(buf[i] == '\r' && buf[i+1] == '\n' && buf[i+2] == '\r' && buf[i+3] == '\n'){
      end = buf + i + 4; break;
    }
  if(!end) return 0;
  size_t hlen = (size_t)(end - buf);

  // Linea de peticion: METODO SP RUTA SP VERSION
  size_t i = 0;
  while(i < hlen && buf[i] != ' ' && buf[i] != '\r') i++;
  if(i == 0 || i >= hlen || buf[i] != ' ') return -1;
  if(i == 3 && !strncmp(buf, "GET", 3))       out->method = FLEXHTTP_M_GET;
  else if(i == 4 && !strncmp(buf, "HEAD", 4)) out->method = FLEXHTTP_M_HEAD;
  else out->method = FLEXHTTP_M_UNKNOWN;      // se responde 405, no se cierra a lo bruto
  i++;
  size_t ps = i;
  while(i < hlen && buf[i] != ' ' && buf[i] != '\r' && buf[i] != '\n') i++;
  size_t pe = i;
  if(pe <= ps) return -1;
  // La cadena de consulta no se usa: se corta antes de des-escapar, para
  // que un '?' escapado no pueda colarse como parte de la ruta.
  size_t q = ps;
  while(q < pe && buf[q] != '?') q++;
  if(q - ps >= FLEXHTTP_PATH_MAX) return -1;
  flexHttpUnescape(buf + ps, q - ps, out->path, sizeof(out->path));
  // Version. HTTP/1.1 mantiene la conexion salvo "Connection: close".
  int http11 = 0;
  if(i < hlen && buf[i] == ' '){
    i++;
    if(hlen - i >= 8 && !strncmp(buf + i, "HTTP/1.1", 8)) http11 = 1;
  }
  out->keepAlive = http11;
  // Cabeceras: solo interesa Connection.
  for(size_t k = 0; k + 1 < hlen; k++){
    if(buf[k] != '\n') continue;
    const char* line = buf + k + 1;
    size_t avail = hlen - (k + 1);
    if(avail >= 11 && !fhNCaseCmp(line, "Connection:", 11)){
      const char* v = line + 11;
      size_t va = avail - 11;
      while(va && (*v == ' ' || *v == '\t')){ v++; va--; }
      if(va >= 5 && !fhNCaseCmp(v, "close", 5)) out->keepAlive = 0;
      else if(va >= 10 && !fhNCaseCmp(v, "keep-alive", 10)) out->keepAlive = 1;
    }
  }
  out->complete = 1;
  if(!flexHttpPathSafe(out->path)) return -1;
  return 1;
}

int flexHttpMatchShare(const char* path, const char* token, const char* name){
  if(!path || !token || !name) return 0;
  if(path[0] != '/') return 0;
  size_t tl = strlen(token);
  if(tl == 0 || strncmp(path + 1, token, tl) != 0) return 0;
  if(path[1 + tl] != '/') return 0;
  return strcmp(path + 2 + tl, name) == 0;
}

const char* flexHttpMime(const char* name){
  if(!name) return "application/octet-stream";
  const char* dot = strrchr(name, '.');
  if(!dot) return "application/octet-stream";
  if(!fhCaseCmp(dot, ".svg"))  return "image/svg+xml";
  if(!fhCaseCmp(dot, ".png"))  return "image/png";
  if(!fhCaseCmp(dot, ".jpg") || !fhCaseCmp(dot, ".jpeg")) return "image/jpeg";
  if(!fhCaseCmp(dot, ".txt"))  return "text/plain; charset=utf-8";
  if(!fhCaseCmp(dot, ".html")) return "text/html; charset=utf-8";
  if(!fhCaseCmp(dot, ".json")) return "application/json";
  return "application/octet-stream";
}

static const char* fhStatusText(int status){
  switch(status){
    case 200: return "OK";
    case 201: return "Created";
    case 204: return "No Content";
    case 206: return "Partial Content";
    case 304: return "Not Modified";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 408: return "Request Timeout";
    case 409: return "Conflict";
    case 411: return "Length Required";
    case 413: return "Payload Too Large";
    case 415: return "Unsupported Media Type";
    case 416: return "Range Not Satisfiable";
    case 422: return "Unprocessable Content";
    case 423: return "Locked";
    case 429: return "Too Many Requests";
    case 500: return "Internal Server Error";
    case 503: return "Service Unavailable";
    case 507: return "Insufficient Storage";
  }
  return "Error";
}
const char* flexHttpStatusText(int status){ return fhStatusText(status); }

size_t flexHttpHeader(char* out, size_t cap, int status, const char* mime,
                      size_t bodyLen, const char* filename, int keepAlive){
  if(!out || cap == 0) return 0;
  int n = snprintf(out, cap,
      "HTTP/1.1 %d %s\r\n"
      "Content-Type: %s\r\n"
      "Content-Length: %lu\r\n"
      "Connection: %s\r\n"
      "Cache-Control: no-store\r\n"
      "X-Content-Type-Options: nosniff\r\n",
      status, fhStatusText(status), mime ? mime : "application/octet-stream",
      (unsigned long)bodyLen, keepAlive ? "keep-alive" : "close");
  if(n < 0 || (size_t)n >= cap) return 0;
  size_t w = (size_t)n;
  if(filename && filename[0]){
    // El nombre va entre comillas y sin comillas dentro: una comilla sin
    // escapar en el nombre partiria la cabecera en dos.
    char safe[FLEXHTTP_NAME_MAX];
    size_t k = 0;
    for(const char* p = filename; *p && k + 1 < sizeof(safe); p++){
      unsigned char c = (unsigned char)*p;
      if(c < 0x20 || c == '"' || c == '\\' || c >= 0x7F) continue;
      safe[k++] = (char)c;
    }
    safe[k] = 0;
    int m = snprintf(out + w, cap - w,
                     "Content-Disposition: attachment; filename=\"%s\"\r\n", safe);
    if(m < 0 || (size_t)m >= cap - w) return 0;
    w += (size_t)m;
  }
  if(w + 2 >= cap) return 0;
  out[w++] = '\r'; out[w++] = '\n'; out[w] = 0;
  return w;
}

// Hoja de estilo de las dos paginas. Se escribe una vez y la comparten
// las dos: el aspecto del sistema no se duplica en dos sitios.
static const char FH_CSS[] =
  "<style>body{margin:0;min-height:100vh;display:flex;align-items:center;"
  "justify-content:center;background:#0d1017;color:#e8ecf4;"
  "font:16px/1.5 system-ui,-apple-system,Segoe UI,Roboto,sans-serif}"
  ".c{max-width:34rem;padding:2rem 1.5rem;text-align:center}"
  "h1{font-size:1.4rem;margin:0 0 .5rem}"
  "p{color:#9aa6bd;margin:.4rem 0 1.4rem}"
  "a.b{display:inline-block;padding:.7rem 1.4rem;border-radius:999px;"
  "background:#3c6ef0;color:#fff;text-decoration:none;font-weight:600}"
  "small{color:#66718a;display:block;margin-top:1.4rem}</style>";

// Escapa el texto que se mete en HTML. El nombre del archivo lo elige el
// usuario en el teclado del dispositivo, asi que puede llevar '<'.
static size_t fhEscHtml(const char* in, char* out, size_t cap){
  size_t w = 0;
  for(const char* p = in; p && *p && w + 7 < cap; p++){
    switch(*p){
      case '&': memcpy(out + w, "&amp;", 5);  w += 5; break;
      case '<': memcpy(out + w, "&lt;", 4);   w += 4; break;
      case '>': memcpy(out + w, "&gt;", 4);   w += 4; break;
      case '"': memcpy(out + w, "&quot;", 6); w += 6; break;
      default:  out[w++] = *p; break;
    }
  }
  if(w < cap) out[w] = 0; else if(cap) out[cap - 1] = 0;
  return w;
}

size_t flexHttpIndexPage(char* out, size_t cap, const char* token,
                         const char* name, size_t bytes){
  if(!out || cap == 0 || !token || !name) return 0;
  char esc[FLEXHTTP_NAME_MAX * 6 + 8];
  fhEscHtml(name, esc, sizeof(esc));
  char kb[32];
  if(bytes < 1024) snprintf(kb, sizeof(kb), "%lu B", (unsigned long)bytes);
  else             snprintf(kb, sizeof(kb), "%lu KB", (unsigned long)((bytes + 512) / 1024));
  int n = snprintf(out, cap,
    "<!doctype html><html lang=\"es\"><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>Flex OS</title>%s<div class=\"c\">"
    "<h1>Descarga desde Flex OS</h1>"
    "<p>%s &middot; %s</p>"
    "<a class=\"b\" href=\"/%s/%s\">Descargar</a>"
    "<small>Servido por Flex OS Ultra en tu red local. El enlace deja de "
    "funcionar al cerrar la aplicacion.</small></div>",
    FH_CSS, esc, kb, token, esc);
  if(n < 0 || (size_t)n >= cap) return 0;
  return (size_t)n;
}

size_t flexHttpErrorPage(char* out, size_t cap, int status, const char* msg){
  if(!out || cap == 0) return 0;
  char esc[192];
  fhEscHtml(msg ? msg : fhStatusText(status), esc, sizeof(esc));
  int n = snprintf(out, cap,
    "<!doctype html><html lang=\"es\"><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>Flex OS</title>%s<div class=\"c\"><h1>%d</h1><p>%s</p></div>",
    FH_CSS, status, esc);
  if(n < 0 || (size_t)n >= cap) return 0;
  return (size_t)n;
}

void flexHttpToken(uint64_t seed, char* out, size_t cap){
  static const char H[] = "0123456789abcdef";
  size_t n = FLEXHTTP_TOKEN_LEN;
  if(cap == 0) return;
  if(n > cap - 1) n = cap - 1;
  for(size_t i = 0; i < n; i++){
    out[i] = H[(seed >> ((i % 16) * 4)) & 0xF];
    // Se remueve la semilla cada 16 caracteres para no repetir el patron
    // si algun dia el testigo se alarga.
    if(i % 16 == 15) seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
  }
  out[n] = 0;
}

// #############################################################
//  API AMPLIADA (Flex Web Server)
// #############################################################

// Copia acotada de un valor de cabecera sin espacios alrededor. Devuelve
// -1 si no cabe (el llamante decide si eso invalida la peticion).
static int fhCopyTrim(const char* v, size_t vl, char* out, size_t cap){
  while(vl && (*v == ' ' || *v == '\t')){ v++; vl--; }
  while(vl && (v[vl - 1] == ' ' || v[vl - 1] == '\t' || v[vl - 1] == '\r')) vl--;
  if(vl + 1 > cap) return -1;
  memcpy(out, v, vl);
  out[vl] = 0;
  return (int)vl;
}

static int fhIsHex(char c){ return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }

// Entero decimal no negativo, con tope. -1 si no es un numero limpio.
static long long fhParseLL(const char* s, size_t n, long long max){
  while(n && (*s == ' ' || *s == '\t')){ s++; n--; }
  while(n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r')) n--;
  if(n == 0 || n > 18) return -1;
  long long v = 0;
  for(size_t i = 0; i < n; i++){
    if(s[i] < '0' || s[i] > '9') return -1;
    v = v * 10 + (s[i] - '0');
    if(v > max) return -1;
  }
  return v;
}

// Cookie: "a=1; fxs=abcd; b=2" -> "abcd". Solo hex: un valor raro no se
// copia (y la peticion sigue, sin sesion).
static void fhCookie(const char* v, size_t vl, char* out, size_t cap){
  size_t i = 0;
  while(i < vl){
    while(i < vl && (v[i] == ' ' || v[i] == ';')) i++;
    size_t ks = i;
    while(i < vl && v[i] != '=' && v[i] != ';') i++;
    size_t kl = i - ks;
    if(i < vl && v[i] == '='){
      i++;
      size_t vs = i;
      while(i < vl && v[i] != ';' && v[i] != '\r') i++;
      size_t ln = i - vs;
      if(kl == strlen(FLEXHTTP_SESS_COOKIE) && !memcmp(v + ks, FLEXHTTP_SESS_COOKIE, kl)){
        bool ok = ln > 0 && ln + 1 <= cap;
        for(size_t k = 0; ok && k < ln; k++) if(!fhIsHex(v[vs + k])) ok = false;
        if(ok){ memcpy(out, v + vs, ln); out[ln] = 0; }
        return;
      }
    }
  }
}

// Range: "bytes=10-20", "bytes=10-" o "bytes=-500". Varios rangos o
// cualquier otra unidad se ignoran (se sirve entero, que es lo que el
// RFC permite).
static void fhRange(const char* v, size_t vl, FlexHttpReqEx* r){
  char b[64];
  if(fhCopyTrim(v, vl, b, sizeof(b)) < 0) return;
  if(strncmp(b, "bytes=", 6) || strchr(b, ',')) return;
  const char* p = b + 6;
  const char* dash = strchr(p, '-');
  if(!dash) return;
  long long a = -1, z = -1;
  if(dash > p){ a = fhParseLL(p, (size_t)(dash - p), 0x7FFFFFFFLL); if(a < 0) return; }
  if(dash[1]){ z = fhParseLL(dash + 1, strlen(dash + 1), 0x7FFFFFFFLL); if(z < 0) return; }
  if(a < 0 && z < 0) return;
  if(a >= 0 && z >= 0 && z < a) return;
  r->hasRange = 1;
  r->rangeStart = a;          // -1 = sufijo ("los ultimos z")
  r->rangeEnd = z;            // -1 = hasta el final
}

int flexHttpParseEx(const char* buf, size_t len, FlexHttpReqEx* out){
  if(!buf || !out) return -1;
  memset(out, 0, sizeof(*out));
  out->contentLength = -1;
  out->rangeStart = out->rangeEnd = -1;
  // Fin de cabeceras. Lo que sigue es cuerpo, aunque venga en el mismo
  // paquete: solo se mira hasta aqui.
  const char* end = NULL;
  size_t scan = len > FLEXHTTP_MAX_REQUEST ? FLEXHTTP_MAX_REQUEST : len;
  for(size_t i = 0; i + 3 < scan; i++)
    if(buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' && buf[i + 3] == '\n'){
      end = buf + i + 4; break;
    }
  if(!end) return len >= FLEXHTTP_MAX_REQUEST ? -1 : 0;
  size_t hlen = (size_t)(end - buf);
  out->headerLen = hlen;
  if(memchr(buf, 0, hlen)) return -1;          // un NUL en la cabecera no es HTTP

  // ---- linea de peticion ----
  size_t i = 0;
  while(i < hlen && buf[i] != ' ' && buf[i] != '\r') i++;
  if(i == 0 || i >= hlen || buf[i] != ' ') return -1;
  if(i == 3 && !strncmp(buf, "GET", 3))          out->method = FLEXHTTP_M_GET;
  else if(i == 4 && !strncmp(buf, "HEAD", 4))    out->method = FLEXHTTP_M_HEAD;
  else if(i == 4 && !strncmp(buf, "POST", 4))    out->method = FLEXHTTP_M_POST;
  else if(i == 6 && !strncmp(buf, "DELETE", 6))  out->method = FLEXHTTP_M_DELETE;
  else if(i == 7 && !strncmp(buf, "OPTIONS", 7)) out->method = FLEXHTTP_M_OPTIONS;
  else out->method = FLEXHTTP_M_UNKNOWN;
  i++;
  size_t ps = i;
  while(i < hlen && buf[i] != ' ' && buf[i] != '\r' && buf[i] != '\n') i++;
  size_t pe = i;
  if(pe <= ps) return -1;
  size_t q = ps;
  while(q < pe && buf[q] != '?') q++;
  if(q - ps >= FLEXHTTP_PATH_MAX) return -1;
  flexHttpUnescape(buf + ps, q - ps, out->path, sizeof(out->path));
  if(q < pe){
    size_t ql = pe - q - 1;
    if(ql + 1 > sizeof(out->query)) return -1;
    memcpy(out->query, buf + q + 1, ql);
    out->query[ql] = 0;
  }
  int http11 = 0;
  if(i < hlen && buf[i] == ' '){
    i++;
    if(hlen - i >= 8 && !strncmp(buf + i, "HTTP/1.1", 8)) http11 = 1;
    else if(hlen - i < 8 || strncmp(buf + i, "HTTP/1.0", 8)) return -1;
  } else return -1;
  out->keepAlive = http11;

  // ---- cabeceras ----
  const char* lp = buf;
  while(lp < end && *lp != '\n') lp++;
  lp++;
  while(lp < end - 2){
    const char* le = lp;
    while(le < end && *le != '\n') le++;
    size_t ll = (size_t)(le - lp);
    if(ll && lp[ll - 1] == '\r') ll--;
    if(ll == 0) break;
    const char* colon = (const char*)memchr(lp, ':', ll);
    if(!colon) return -1;                          // linea sin "Nombre:" no es una cabecera
    size_t nl = (size_t)(colon - lp);
    const char* v = colon + 1;
    size_t vl = ll - nl - 1;
    #define FH_IS(name) (nl == sizeof(name) - 1 && !fhNCaseCmp(lp, name, nl))
    if(FH_IS("Connection")){
      char b[24];
      if(fhCopyTrim(v, vl, b, sizeof(b)) >= 0){
        if(!fhCaseCmp(b, "close")) out->keepAlive = 0;
        else if(!fhCaseCmp(b, "keep-alive")) out->keepAlive = 1;
      }
    } else if(FH_IS("Content-Length")){
      long long n = fhParseLL(v, vl, 0x7FFFFFFFLL);
      if(n < 0) return -1;
      // Dos Content-Length distintos es el primer paso del "request
      // smuggling": no se elige uno, se rechaza la peticion.
      if(out->contentLength >= 0 && out->contentLength != n) return -1;
      out->contentLength = n;
    } else if(FH_IS("Transfer-Encoding")){
      char b[32];
      if(fhCopyTrim(v, vl, b, sizeof(b)) < 0 || fhCaseCmp(b, "chunked")) return -1;
      out->chunked = 1;
    } else if(FH_IS("Cookie")){
      fhCookie(v, vl, out->session, sizeof(out->session));
    } else if(FH_IS("Host")){
      if(fhCopyTrim(v, vl, out->host, sizeof(out->host)) < 0) return -1;
    } else if(FH_IS("Content-Type")){
      if(fhCopyTrim(v, vl, out->ctype, sizeof(out->ctype)) < 0) out->ctype[0] = 0;
    } else if(FH_IS("Range")){
      fhRange(v, vl, out);
    } else if(FH_IS("X-Flex")){
      char b[8];
      if(fhCopyTrim(v, vl, b, sizeof(b)) >= 0 && !strcmp(b, "1")) out->xflex = 1;
    }
    #undef FH_IS
    lp = le + 1;
  }
  out->complete = 1;
  if(!flexHttpPathSafe(out->path)) return -1;
  return 1;
}

int flexHttpQueryGet(const char* query, const char* key, char* out, size_t cap){
  if(out && cap) out[0] = 0;
  if(!query || !key || !key[0]) return 0;
  size_t kl = strlen(key);
  const char* p = query;
  while(*p){
    const char* e = p;
    while(*e && *e != '&') e++;
    const char* eq = p;
    while(eq < e && *eq != '=') eq++;
    if((size_t)(eq - p) == kl && !memcmp(p, key, kl)){
      if(out && cap){
        const char* vs = (eq < e) ? eq + 1 : e;
        flexHttpUnescape(vs, (size_t)(e - vs), out, cap);
      }
      return 1;
    }
    p = *e ? e + 1 : e;
  }
  return 0;
}

int flexHttpParseU32(const char* s, uint32_t* out){
  if(!s || !s[0] || !out) return 0;
  size_t n = strlen(s);
  if(n > 10) return 0;
  unsigned long long v = 0;
  for(size_t i = 0; i < n; i++){
    if(s[i] < '0' || s[i] > '9') return 0;
    v = v * 10 + (unsigned)(s[i] - '0');
  }
  if(v > 0xFFFFFFFFull) return 0;
  *out = (uint32_t)v;
  return 1;
}

// Anade texto formateado en out[*w..]. false si no cabe (nada queda a medias
// que importe: el llamante devuelve 0 y no envia esa cabecera).
static bool fhAppend(char* out, size_t cap, size_t* w, const char* fmt, ...){
  va_list ap;
  va_start(ap, fmt);
  int m = vsnprintf(out + *w, cap - *w, fmt, ap);
  va_end(ap);
  if(m < 0 || (size_t)m >= cap - *w) return false;
  *w += (size_t)m;
  return true;
}

size_t flexHttpHeaderEx(char* out, size_t cap, int status, const char* mime,
                        long long bodyLen, const char* extra, int keepAlive){
  if(!out || cap == 0) return 0;
  size_t w = 0;
  bool ok = fhAppend(out, cap, &w, "HTTP/1.1 %d %s\r\n", status, fhStatusText(status));
  if(ok && mime && mime[0]) ok = fhAppend(out, cap, &w, "Content-Type: %s\r\n", mime);
  if(ok) ok = (bodyLen >= 0) ? fhAppend(out, cap, &w, "Content-Length: %lld\r\n", bodyLen)
                             : fhAppend(out, cap, &w, "Transfer-Encoding: chunked\r\n");
  if(ok) ok = fhAppend(out, cap, &w, "Connection: %s\r\n", keepAlive ? "keep-alive" : "close");
  if(ok && (!extra || !strstr(extra, "Cache-Control:"))) ok = fhAppend(out, cap, &w, "Cache-Control: no-store\r\n");
  if(ok) ok = fhAppend(out, cap, &w, "X-Content-Type-Options: nosniff\r\nReferrer-Policy: no-referrer\r\n");
  if(ok && extra && extra[0]) ok = fhAppend(out, cap, &w, "%s", extra);
  if(ok) ok = fhAppend(out, cap, &w, "\r\n");
  return ok ? w : 0;
}

size_t flexHttpDisposition(char* out, size_t cap, const char* utf8Name, int isInline){
  if(!out || cap == 0) return 0;
  const char* nm = (utf8Name && utf8Name[0]) ? utf8Name : "archivo";
  // Respaldo ASCII para quien no entienda filename*.
  char ascii[80];
  size_t k = 0;
  for(const unsigned char* p = (const unsigned char*)nm; *p && k + 1 < sizeof(ascii); p++){
    unsigned char c = *p;
    if(c >= 0x80){ if((c & 0xC0) != 0x80) ascii[k++] = '_'; continue; }   // un '_' por caracter
    if(c < 0x20 || c == 0x7F || c == '"' || c == '\\' || c == '/') c = '_';
    ascii[k++] = (char)c;
  }
  ascii[k] = 0;
  int n = snprintf(out, cap, "Content-Disposition: %s; filename=\"%s\"; filename*=UTF-8''",
                   isInline ? "inline" : "attachment", ascii);
  if(n < 0 || (size_t)n >= cap) return 0;
  size_t w = (size_t)n;
  static const char H[] = "0123456789ABCDEF";
  for(const unsigned char* p = (const unsigned char*)nm; *p; p++){
    unsigned char c = *p;
    bool plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                 strchr("!#$&+-.^_`|~", c) != NULL;
    if(c == 0x7F || c < 0x20) continue;
    if(plain){ if(w + 2 > cap) return 0; out[w++] = (char)c; }
    else { if(w + 4 > cap) return 0; out[w++] = '%'; out[w++] = H[c >> 4]; out[w++] = H[c & 15]; }
  }
  if(w + 3 > cap) return 0;
  out[w++] = '\r'; out[w++] = '\n'; out[w] = 0;
  return w;
}

size_t flexHttpChunkHead(char* out, size_t cap, size_t n){
  if(!out || cap == 0) return 0;
  int m = snprintf(out, cap, "%lx\r\n", (unsigned long)n);
  return (m < 0 || (size_t)m >= cap) ? 0 : (size_t)m;
}

int flexHttpHostOk(const char* host, const char* ownIp, int port){
  if(!host || !host[0]) return 1;                  // HTTP/1.0 sin Host
  if(!ownIp || !ownIp[0]) return 0;
  size_t il = strlen(ownIp);
  if(strncmp(host, ownIp, il)) return 0;
  if(host[il] == 0) return 1;
  if(host[il] != ':') return 0;
  uint32_t p;
  return flexHttpParseU32(host + il + 1, &p) && (int)p == port;
}
