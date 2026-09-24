#pragma once
// #############################################################
//  FLEX OS · SERVIDOR HTTP LOCAL  ·  protocolo  (FlexOS_HttpShare.h/.cpp)
//  ------------------------------------------------------------
//  QUE ES. La mitad PORTABLE del servidor HTTP del sistema: analizar
//  una peticion HTTP/1.1, decidir a que recurso se refiere y componer
//  la respuesta. Los sockets y la tarea de FreeRTOS viven en la mitad
//  de placa (FlexOS_Ultra_WebServer.h).
//
//  DE DONDE SALE. Se escribio para Flex Vector Pro (compartir un SVG
//  por Wi-Fi) y quedo archivado con esa app en futuras-versiones-BETA
//  sin un solo consumidor. Flex Web Server lo PROMUEVE al firmware
//  activo en vez de escribir un segundo analizador: la API de entonces
//  (flexHttpParse, flexHttpMatchShare, flexHttpIndexPage...) sigue aqui
//  intacta y con sus pruebas, y se AMPLIA con lo que necesita una
//  biblioteca de medios: POST con Content-Length, cadena de consulta,
//  la cookie de sesion, Range, cabeceras extra, nombres UTF-8 en
//  Content-Disposition y transferencia por trozos.
//
//  POR QUE VIVE FUERA DEL SKETCH. Porque analiza bytes que vienen de la
//  red -- de cualquiera que este en la misma Wi-Fi -- y eso en este
//  proyecto se prueba en el PC con sanitizers, sin excepcion (mismo
//  criterio que FlexOS_Browser.cpp y FlexOS_FlexLink.cpp). Una linea de
//  peticion sin '\0', una cabecera de 40 KB o un %-escape truncado no
//  pueden quedarse en "parece que funciona".
//
//  QUE DECIDE ESTE MODULO Y QUE NO. Aqui solo hay protocolo. Quien puede
//  ver que (sesiones, codigo de acceso, elementos bloqueados) lo decide
//  FlexOS_MediaWeb, que es la aplicacion del servidor.
// #############################################################
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FLEXHTTP_PATH_MAX    160
#define FLEXHTTP_TOKEN_LEN    16     // caracteres hex del testigo de sesion
#define FLEXHTTP_NAME_MAX     64
#define FLEXHTTP_MAX_REQUEST 4096    // techo de la peticion que se acepta

enum { FLEXHTTP_M_UNKNOWN = 0, FLEXHTTP_M_GET, FLEXHTTP_M_HEAD };

typedef struct {
  int  method;                       // FLEXHTTP_M_*
  int  keepAlive;
  int  complete;                     // 1 = se leyeron las cabeceras enteras
  char path[FLEXHTTP_PATH_MAX];      // ya des-escapado y sin la cadena de consulta
} FlexHttpReq;

// Analiza el bloque recibido. Devuelve:
//   1  peticion completa y valida        -> 'out' relleno
//   0  aun faltan bytes (sin \r\n\r\n)   -> hay que seguir leyendo
//  -1  peticion invalida o demasiado grande -> cerrar con 400
int flexHttpParse(const char* buf, size_t len, FlexHttpReq* out);

// Des-escapa %XX y '+' sobre una ruta. Nunca escribe fuera de 'out' y
// SIEMPRE termina en cero. Devuelve la longitud escrita.
size_t flexHttpUnescape(const char* in, size_t len, char* out, size_t cap);

// true si la ruta es segura: absoluta, sin "..", sin '\\' y sin bytes de
// control. Se comprueba aunque el recurso no se busque por ruta: una
// ruta con ".." en el registro ya es senal de que alguien esta probando.
int flexHttpPathSafe(const char* path);

// Compara la ruta con "/<token>/<nombre>". Devuelve 1 si encaja.
int flexHttpMatchShare(const char* path, const char* token, const char* name);

// Tipo de contenido por extension. Devuelve un literal estatico.
const char* flexHttpMime(const char* name);

// Cabecera de respuesta. Devuelve los bytes escritos, o 0 si no cabe.
size_t flexHttpHeader(char* out, size_t cap, int status, const char* mime,
                      size_t bodyLen, const char* filename, int keepAlive);

// Pagina de bienvenida: un enlace al archivo publicado. Es lo que se ve
// al abrir "http://<ip>:8080/" a mano, sin escanear el QR. Devuelve los
// bytes escritos (0 si no cabe).
size_t flexHttpIndexPage(char* out, size_t cap, const char* token,
                         const char* name, size_t bytes);

// Pagina de error, con el mismo aspecto que la de bienvenida.
size_t flexHttpErrorPage(char* out, size_t cap, int status, const char* msg);

// Escribe un testigo hexadecimal de FLEXHTTP_TOKEN_LEN caracteres a
// partir de una semilla de 64 bits. El generador es del anfitrion
// (esp_random en la placa): aqui solo se le da forma, para que la
// prueba de host pueda comprobar el formato sin depender del hardware.
void flexHttpToken(uint64_t seed, char* out, size_t cap);

// #############################################################
//  API AMPLIADA  ·  la que usa Flex Web Server
// #############################################################
enum { FLEXHTTP_M_POST = 3, FLEXHTTP_M_DELETE = 4, FLEXHTTP_M_OPTIONS = 5 };

#define FLEXHTTP_QUERY_MAX   512
#define FLEXHTTP_SESS_MAX     64     // valor de la cookie de sesion
#define FLEXHTTP_HOST_MAX     64
#define FLEXHTTP_CTYPE_MAX    64
#define FLEXHTTP_SESS_COOKIE "fxs"

typedef struct {
  int      method;                       // FLEXHTTP_M_*
  int      keepAlive;
  int      complete;
  size_t   headerLen;                    // bytes de cabecera (incluido \r\n\r\n): el cuerpo empieza aqui
  char     path[FLEXHTTP_PATH_MAX];      // des-escapada y sin la consulta
  char     query[FLEXHTTP_QUERY_MAX];    // consulta CRUDA (sin '?'), aun escapada
  long long contentLength;               // -1 = no vino
  int      chunked;                      // Transfer-Encoding: chunked
  char     session[FLEXHTTP_SESS_MAX];   // valor de la cookie "fxs" ("" si no vino)
  char     host[FLEXHTTP_HOST_MAX];      // cabecera Host ("" si no vino)
  char     ctype[FLEXHTTP_CTYPE_MAX];    // Content-Type
  int      hasRange;                     // vino un Range: bytes=a-b valido
  long long rangeStart, rangeEnd;        // rangeEnd -1 = hasta el final; rangeStart -1 = sufijo
  int      xflex;                        // "X-Flex: 1": la peticion viene de la app (defensa CSRF)
} FlexHttpReqEx;

// Como flexHttpParse, con todo lo anterior. Acepta GET, HEAD, POST,
// DELETE y OPTIONS; cualquier otro metodo queda en FLEXHTTP_M_UNKNOWN
// (el llamante responde 405). Devuelve 1 / 0 / -1 igual que la otra.
int    flexHttpParseEx(const char* buf, size_t len, FlexHttpReqEx* out);

// Busca `key` en una consulta cruda ("a=1&b=%C3%B1") y des-escapa su
// valor en `out`. 1 = estaba (aunque vacia), 0 = no estaba.
int    flexHttpQueryGet(const char* query, const char* key, char* out, size_t cap);
// Decimal sin signo de 32 bits, sin espacios ni signos ni desbordamiento.
int    flexHttpParseU32(const char* s, uint32_t* out);

// Texto del estado HTTP (Created, Partial Content, Payload Too Large...).
const char* flexHttpStatusText(int status);

// Cabecera de respuesta completa. bodyLen < 0 = por trozos (chunked).
// `extra` son lineas ya formadas ("Name: valor\r\n...") o NULL.
// Devuelve bytes escritos o 0 si no cabe.
size_t flexHttpHeaderEx(char* out, size_t cap, int status, const char* mime,
                        long long bodyLen, const char* extra, int keepAlive);

// "Content-Disposition: attachment; filename=\"ascii\"; filename*=UTF-8''..."
// con el nombre UTF-8 del usuario bien escapado (RFC 6266 / 5987).
// inline=1 para abrir en el navegador. Devuelve bytes o 0.
size_t flexHttpDisposition(char* out, size_t cap, const char* utf8Name, int isInline);

// Cabecera de un trozo ("1a2b\r\n"). El cierre de trozo es "\r\n" y el
// final "0\r\n\r\n".
size_t flexHttpChunkHead(char* out, size_t cap, size_t n);

// Host valido para este servidor: vacio (HTTP/1.0), o la IP propia con o
// sin ":puerto". Defensa contra el "DNS rebinding".
int    flexHttpHostOk(const char* host, const char* ownIp, int port);

#ifdef __cplusplus
}
#endif
