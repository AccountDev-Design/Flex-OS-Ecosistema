// #############################################################
//  test_httpshare.cpp  ·  pruebas de host de FlexOS_HttpShare.cpp
// #############################################################
//
//  Este modulo analiza bytes que llegan por la red: cualquiera que
//  este en la misma Wi-Fi puede mandarle lo que quiera. En este
//  proyecto eso significa prueba de host CON sanitizers, sin
//  excepcion -- el mismo criterio que FlexOS_Browser.cpp.
//
//  Lo que se ejercita, y por que cada cosa:
//    1) Peticiones a medias. Un GET partido en dos paquetes tiene que
//       devolver "aun no", no "invalida": cerrarle la conexion a un
//       navegador porque el TCP troceo la cabecera es un fallo que en
//       la placa se ve como "a veces no descarga".
//    2) Rutas. %-escapes truncados, "..", barras invertidas, bytes de
//       control. El servidor no abre ficheros por ruta, pero una ruta
//       con ".." ya dice que alguien esta probando, y se rechaza.
//    3) El testigo de sesion. La ruta compartida solo encaja con SU
//       testigo y SU nombre: ni un prefijo, ni un sufijo, ni el nombre
//       a secas.
//    4) Cabeceras y paginas. Nunca se escribe fuera del buffer, y un
//       buffer corto devuelve 0 en vez de una cabecera cortada -- una
//       cabecera cortada es una respuesta que el navegador no entiende.
//    5) Escapado. El nombre del archivo lo escribe el usuario en el
//       teclado del dispositivo: puede llevar comillas y '<'.

#include "../../FlexOS_Ultra/FlexOS_HttpShare.h"
#include <cstdio>
#include <cstring>
#include <string>

static int g_fail = 0, g_run = 0;
#define CHECK(cond, ...) do { g_run++; if(!(cond)){ g_fail++; \
  std::printf("  FALLO %s:%d  ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while(0)

static int parse(const char* req, FlexHttpReq* out){
  return flexHttpParse(req, strlen(req), out);
}

// =============================================================
static void testParse(){
  std::printf("-- linea de peticion y cabeceras --\n");
  FlexHttpReq r;
  CHECK(parse("GET /a/b.svg HTTP/1.1\r\nHost: x\r\n\r\n", &r) == 1, "GET valido");
  CHECK(r.method == FLEXHTTP_M_GET, "metodo GET");
  CHECK(!strcmp(r.path, "/a/b.svg"), "ruta: \"%s\"", r.path);
  CHECK(r.keepAlive == 1, "HTTP/1.1 mantiene la conexion por defecto");

  CHECK(parse("HEAD / HTTP/1.1\r\n\r\n", &r) == 1, "HEAD valido");
  CHECK(r.method == FLEXHTTP_M_HEAD, "metodo HEAD");

  // Un metodo desconocido NO es una peticion invalida: se analiza igual
  // para poder contestar 405 con una pagina, en vez de cortar la conexion.
  CHECK(parse("POST /x HTTP/1.1\r\n\r\n", &r) == 1, "POST se analiza");
  CHECK(r.method == FLEXHTTP_M_UNKNOWN, "POST queda como metodo desconocido");

  CHECK(parse("GET / HTTP/1.0\r\n\r\n", &r) == 1, "HTTP/1.0");
  CHECK(r.keepAlive == 0, "HTTP/1.0 cierra por defecto");
  CHECK(parse("GET / HTTP/1.1\r\nConnection: close\r\n\r\n", &r) == 1, "Connection: close");
  CHECK(r.keepAlive == 0, "Connection: close manda sobre la version");
  CHECK(parse("GET / HTTP/1.0\r\nConnection: Keep-Alive\r\n\r\n", &r) == 1, "keep-alive");
  CHECK(r.keepAlive == 1, "Connection: Keep-Alive (mayusculas incluidas)");

  // La cadena de consulta se descarta ANTES de des-escapar.
  CHECK(parse("GET /f.svg?x=1&y=2 HTTP/1.1\r\n\r\n", &r) == 1, "con consulta");
  CHECK(!strcmp(r.path, "/f.svg"), "la consulta no entra en la ruta: \"%s\"", r.path);

  std::printf("-- peticiones a medias e invalidas --\n");
  CHECK(parse("GET /a HTTP/1.1\r\nHost: x\r\n", &r) == 0, "sin fin de cabeceras -> faltan bytes");
  CHECK(parse("GE", &r) == 0, "dos bytes -> faltan bytes");
  CHECK(parse("", &r) == 0, "vacio -> faltan bytes");
  CHECK(parse("\r\n\r\n", &r) == -1, "sin linea de peticion -> invalida");
  CHECK(parse("GET\r\n\r\n", &r) == -1, "sin ruta -> invalida");
  // Techo de tamano: una peticion enorme se rechaza, no se copia.
  std::string big = "GET /";
  big.append(FLEXHTTP_MAX_REQUEST + 100, 'a');
  big += " HTTP/1.1\r\n\r\n";
  CHECK(flexHttpParse(big.c_str(), big.size(), &r) == -1, "peticion por encima del techo");
  // Ruta larga pero peticion corta: tambien se rechaza, sin desbordar.
  std::string longPath = "GET /";
  longPath.append(FLEXHTTP_PATH_MAX + 20, 'b');
  longPath += " HTTP/1.1\r\n\r\n";
  CHECK(flexHttpParse(longPath.c_str(), longPath.size(), &r) == -1, "ruta mas larga que el buffer");
}

static void testPaths(){
  std::printf("-- rutas: %%-escapes y seguridad --\n");
  char out[64];
  CHECK(flexHttpUnescape("/a%20b", 6, out, sizeof(out)) == 4 && !strcmp(out, "/a b"), "%%20 -> espacio: \"%s\"", out);
  CHECK(flexHttpUnescape("a+b", 3, out, sizeof(out)) == 3 && !strcmp(out, "a b"), "'+' -> espacio");
  // Un '%' sin dos hexadecimales detras se copia TAL CUAL. Saltarselo
  // convertiria "%%2e%2e" en "..", que es justo lo que no puede pasar.
  CHECK(flexHttpUnescape("%zz", 3, out, sizeof(out)) == 3 && !strcmp(out, "%zz"), "%% invalido se copia: \"%s\"", out);
  CHECK(flexHttpUnescape("%2", 2, out, sizeof(out)) == 2 && !strcmp(out, "%2"), "%% truncado al final");
  // Nunca escribe fuera, y siempre cierra en cero.
  char tiny[4];
  memset(tiny, 0x7F, sizeof(tiny));
  size_t n = flexHttpUnescape("abcdefgh", 8, tiny, sizeof(tiny));
  CHECK(n == 3 && tiny[3] == 0, "buffer corto: se trunca y se cierra en cero");

  CHECK(flexHttpPathSafe("/a/b.svg") == 1, "ruta normal");
  CHECK(flexHttpPathSafe("relativa") == 0, "una ruta que no empieza en '/'");
  CHECK(flexHttpPathSafe("/a/../b") == 0, "'..' como segmento");
  CHECK(flexHttpPathSafe("/..") == 0, "'..' al final");
  CHECK(flexHttpPathSafe("/../a") == 0, "'..' al principio");
  CHECK(flexHttpPathSafe("/a/...") == 1, "'...' es un nombre legitimo");
  CHECK(flexHttpPathSafe("/a..b") == 1, "'a..b' es un nombre legitimo");
  CHECK(flexHttpPathSafe("/a\\b") == 0, "barra invertida");
  CHECK(flexHttpPathSafe("/a\nb") == 0, "byte de control");
  CHECK(flexHttpPathSafe(NULL) == 0, "ruta nula");

  // Y por el camino real: una peticion con ".." escapado tampoco pasa.
  FlexHttpReq r;
  CHECK(parse("GET /%2e%2e/secreto HTTP/1.1\r\n\r\n", &r) == -1, "'..' escapado se rechaza");
}

static void testShareMatch(){
  std::printf("-- testigo de sesion: solo encaja el suyo --\n");
  const char* tok = "0123456789abcdef";
  const char* name = "Documento.svg";
  char path[FLEXHTTP_PATH_MAX];
  snprintf(path, sizeof(path), "/%s/%s", tok, name);
  CHECK(flexHttpMatchShare(path, tok, name) == 1, "la ruta buena encaja");
  CHECK(flexHttpMatchShare("/Documento.svg", tok, name) == 0, "el nombre a secas no basta");
  CHECK(flexHttpMatchShare("/0123456789abcdee/Documento.svg", tok, name) == 0, "un testigo distinto no encaja");
  CHECK(flexHttpMatchShare("/0123456789abcdef/Otro.svg", tok, name) == 0, "otro nombre no encaja");
  CHECK(flexHttpMatchShare("/0123456789abcdefX/Documento.svg", tok, name) == 0, "testigo con sufijo");
  CHECK(flexHttpMatchShare("/", tok, name) == 0, "la raiz no es el archivo");
  CHECK(flexHttpMatchShare(NULL, tok, name) == 0, "ruta nula");

  std::printf("-- testigo: formato y longitud --\n");
  char t1[FLEXHTTP_TOKEN_LEN + 1], t2[FLEXHTTP_TOKEN_LEN + 1];
  flexHttpToken(0x0123456789abcdefULL, t1, sizeof(t1));
  flexHttpToken(0xfedcba9876543210ULL, t2, sizeof(t2));
  CHECK(strlen(t1) == FLEXHTTP_TOKEN_LEN, "longitud del testigo: %zu", strlen(t1));
  CHECK(strcmp(t1, t2) != 0, "dos semillas distintas dan testigos distintos");
  for(size_t i = 0; i < strlen(t1); i++)
    CHECK((t1[i] >= '0' && t1[i] <= '9') || (t1[i] >= 'a' && t1[i] <= 'f'), "el testigo es hexadecimal");
  char tsmall[5];
  flexHttpToken(1, tsmall, sizeof(tsmall));
  CHECK(strlen(tsmall) == 4, "con un buffer corto se trunca y se cierra en cero");
}

static void testResponses(){
  std::printf("-- cabeceras y paginas --\n");
  char buf[1024];
  size_t n = flexHttpHeader(buf, sizeof(buf), 200, "image/svg+xml", 1234, "Mi documento.svg", 0);
  CHECK(n > 0, "cabecera 200");
  CHECK(strstr(buf, "HTTP/1.1 200 OK\r\n") == buf, "linea de estado");
  CHECK(strstr(buf, "Content-Length: 1234\r\n") != NULL, "Content-Length");
  CHECK(strstr(buf, "Content-Type: image/svg+xml\r\n") != NULL, "Content-Type");
  CHECK(strstr(buf, "Connection: close\r\n") != NULL, "Connection");
  CHECK(strstr(buf, "X-Content-Type-Options: nosniff") != NULL, "nosniff");
  CHECK(n >= 4 && !strcmp(buf + n - 4, "\r\n\r\n"), "la cabecera acaba en linea en blanco");
  // Una comilla en el nombre partiria la cabecera en dos: se filtra.
  n = flexHttpHeader(buf, sizeof(buf), 200, "image/svg+xml", 1, "ma\"lo\r\n.svg", 0);
  CHECK(n > 0 && strstr(buf, "filename=\"malo.svg\"") != NULL, "el nombre se limpia: %s", buf);
  // Un buffer corto devuelve 0, no una cabecera cortada.
  CHECK(flexHttpHeader(buf, 20, 200, "image/svg+xml", 1234, NULL, 0) == 0, "buffer corto -> 0");

  CHECK(!strcmp(flexHttpMime("a.svg"), "image/svg+xml"), "mime svg");
  CHECK(!strcmp(flexHttpMime("a.SVG"), "image/svg+xml"), "mime svg en mayusculas");
  CHECK(!strcmp(flexHttpMime("a.png"), "image/png"), "mime png");
  CHECK(!strcmp(flexHttpMime("sinpunto"), "application/octet-stream"), "sin extension");
  CHECK(!strcmp(flexHttpMime(NULL), "application/octet-stream"), "nombre nulo");

  n = flexHttpIndexPage(buf, sizeof(buf), "abc", "Documento.svg", 4096);
  CHECK(n > 0, "pagina de bienvenida");
  CHECK(strstr(buf, "href=\"/abc/Documento.svg\"") != NULL, "el enlace lleva el testigo");
  CHECK(strstr(buf, "4 KB") != NULL, "el tamano se ensena en KB");
  // El nombre lo escribe el usuario en el teclado: puede llevar '<'.
  n = flexHttpIndexPage(buf, sizeof(buf), "abc", "<script>", 10);
  CHECK(n > 0 && strstr(buf, "<script>x") == NULL, "el nombre se escapa");
  CHECK(strstr(buf, "&lt;script&gt;") != NULL, "escapado correcto");
  CHECK(flexHttpIndexPage(buf, 40, "abc", "x.svg", 1) == 0, "buffer corto -> 0");

  n = flexHttpErrorPage(buf, sizeof(buf), 404, "Aqui no hay nada");
  CHECK(n > 0 && strstr(buf, "404") != NULL, "pagina de error");
  CHECK(flexHttpErrorPage(buf, 30, 404, "x") == 0, "error con buffer corto -> 0");
}

// Ruido: bytes cualesquiera contra el analizador. No se comprueba QUE
// devuelve -- se comprueba que devuelve, sin desbordar (lo vigilan los
// sanitizers) y sin dejar una ruta sin terminar en cero.
static void testFuzz(){
  std::printf("-- ruido aleatorio contra el analizador --\n");
  uint32_t seed = 0x1234567u;
  char buf[512];
  FlexHttpReq r;
  int ok = 1;
  for(int it = 0; it < 4000; it++){
    size_t len = (size_t)(seed % (sizeof(buf) - 1));
    for(size_t i = 0; i < len; i++){
      seed = seed * 1103515245u + 12345u;
      buf[i] = (char)((seed >> 16) & 0xFF);
    }
    seed = seed * 1103515245u + 12345u;
    int rc = flexHttpParse(buf, len, &r);
    if(rc != -1 && rc != 0 && rc != 1) ok = 0;
    if(rc == 1 && r.path[FLEXHTTP_PATH_MAX - 1] != 0) ok = 0;
  }
  CHECK(ok, "4000 entradas de ruido: siempre un codigo valido y una ruta cerrada");
}

int main(){
  std::printf("=== FlexOS · servidor HTTP local (protocolo) ===\n");
  testParse();
  testPaths();
  testShareMatch();
  testResponses();
  testFuzz();
  std::printf("=== %d comprobaciones, %d fallos ===\n", g_run, g_fail);
  return g_fail ? 1 : 0;
}
