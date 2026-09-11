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
    case 400: return "Bad Request";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 503: return "Service Unavailable";
  }
  return "Error";
}

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
