#pragma once
// #############################################################
//  FLEX OS · SERVIDOR HTTP LOCAL  ·  protocolo  (FlexOS_HttpShare.h/.cpp)
//  ------------------------------------------------------------
//  QUE ES. La mitad PORTABLE del servidor HTTP del sistema: analizar
//  una peticion HTTP/1.1, decidir a que recurso se refiere y componer
//  la respuesta. Los sockets y la tarea de FreeRTOS viven en
//  FlexOS_Ultra_HttpShare.h, que es la mitad que necesita la placa.
//
//  ES UN MODULO DEL SISTEMA, NO DE UNA APP. Flex Vector Pro es el
//  primero que lo usa (para dejar descargar el SVG exportado desde el
//  movil), pero el servidor no sabe nada de vectores: publica un blob
//  con un nombre y un tipo. Cualquier app futura -- Notas, Galeria,
//  Almacenamiento -- comparte por aqui sin duplicar ni una linea.
//
//  POR QUE EXISTE. Se reviso todo el repositorio: Flex OS tiene
//  CLIENTES de red (el navegador, el OTA, Flex Store, el clima) y no
//  tenia ningun servidor.
//
//  POR QUE VIVE FUERA DEL SKETCH. Porque analiza bytes que vienen de la
//  red -- de cualquiera que este en la misma Wi-Fi -- y eso en este
//  proyecto se prueba en el PC con sanitizers, sin excepcion (mismo
//  criterio que FlexOS_Browser.cpp y FlexOS_FlexLink.cpp). Una linea de
//  peticion sin '\0', una cabecera de 40 KB o un %-escape truncado no
//  pueden quedarse en "parece que funciona".
//
//  SEGURIDAD, Y ES DELIBERADAMENTE POCA. Esto sirve UN archivo, en la
//  red local, tras una ruta con un testigo aleatorio de sesion, y solo
//  mientras la app que lo publico esta abierta. No hay listado de
//  directorios, no se abre ningun archivo por su ruta y no se acepta
//  ningun metodo que escriba. Lo que el servidor puede entregar es
//  EXACTAMENTE el bloque de memoria que se le registro.
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

#ifdef __cplusplus
}
#endif
