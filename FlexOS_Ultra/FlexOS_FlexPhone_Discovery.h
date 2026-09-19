// #############################################################
// ##  FLEX PHONE · DESCUBRIMIENTO  (nucleo puro)
// ##  ---------------------------------------------------------
// ##  LOS BYTES QUE VAN AL AIRE, y nada mas: ni sockets, ni Wi-Fi,
// ##  ni Arduino. Por eso se compila y se ejecuta EN EL PC
// ##  (tests/host/test_flexphone_discovery.cpp).
// ##
// ##  Existe por un motivo concreto: estos cuatro paquetes estan
// ##  escritos DOS VECES -- aqui y en WifiLinkServer.kt --, y un
// ##  desplazamiento mal contado en cualquiera de los dos lados no
// ##  produce ningun error: produce un telefono y un reloj que no
// ##  se encuentran nunca y nadie sabe por que. Un fallo asi solo
// ##  se ve comparando bytes, que es lo que hace la bateria.
// ##
// ##  EL DESCUBRIMIENTO VA EN LOS DOS SENTIDOS, y no por simetria:
// ##  por fiabilidad.
// ##
// ##    · Que el reloj emita y el telefono RECIBA una difusion es
// ##      la direccion FRAGIL. Android filtra las tramas de
// ##      difusion en el controlador Wi-Fi para ahorrar bateria, y
// ##      el cerrojo de multidifusion ayuda pero no lo cura del
// ##      todo con la pantalla apagada.
// ##
// ##    · Al reves es SOLIDO: emitir desde Android no se filtra
// ##      nunca, y la respuesta del reloj vuelve en unidifusion,
// ##      que tampoco. Ademas, al recibir la pregunta el reloj ya
// ##      sabe todo lo que necesita -- la direccion viene en el
// ##      origen del paquete y el puerto en la carga --, asi que
// ##      puede conectar sin que su propia difusion haya llegado a
// ##      ninguna parte.
// ##
// ##  LOS CUATRO PAQUETES
// ##
// ##    reloj    -> difusion : "FLEXPHONE?" u8 ver
// ##    telefono -> unidifus.: "FLEXPHONE!" u8 ver  u16 puertoTcp  str nombre
// ##    telefono -> difusion : "FLEXOS?"    u8 ver  u16 puertoTcp  str nombre
// ##    reloj    -> unidifus.: "FLEXOS!"    u8 ver  u8 flags       str id  str nombre
// ##
// ##  `str` es un byte de longitud seguido de esos bytes, sin cero
// ##  final. `u16` va en orden de byte menos significativo primero.
// ##
// ##  NINGUNA de estas funciones confia en la longitud que anuncia
// ##  el paquete: se comprueba contra lo que de verdad se recibio
// ##  antes de copiar un solo byte. Lo que llega aqui viene de la
// ##  red y puede estar hecho a mano.
// #############################################################
#pragma once
#include "FlexOS_FlexLink.h"

// =============================================================
//  Constantes del protocolo de red
//  Los MISMOS numeros que WifiLinkServer.kt.
// =============================================================
#define FLPW_TCP_PORT        47820   // Flex Link sobre TCP
#define FLPW_UDP_PORT        47821   // descubrimiento

// Sonda del RELOJ hacia el telefono, y su respuesta.
#define FLPW_PROBE           "FLEXPHONE?"
#define FLPW_REPLY           "FLEXPHONE!"
#define FLPW_PROBE_LEN       10

// Sonda del TELEFONO hacia el reloj, y su respuesta.
#define FLPW_ASK             "FLEXOS?"
#define FLPW_ANS             "FLEXOS!"
#define FLPW_ASK_LEN         7
#define FLPW_FLAG_PAIRING    0x01    // el reloj ensena AHORA un codigo
#define FLPW_NAME_MAX        31      // tope de un `str` en estos paquetes

// Lo mas largo que puede salir de fpdBuildAnswer.
#define FLPW_ANS_MAX  (FLPW_ASK_LEN + 2 + 1 + FLPW_NAME_MAX + 1 + FLPW_NAME_MAX)
// Lo mas largo que puede salir de fpdBuildAsk.
#define FLPW_ASK_MAX  (FLPW_ASK_LEN + 3 + 1 + FLPW_NAME_MAX)

// -------------------------------------------------------------
//  Utilidades internas
// -------------------------------------------------------------
// Escribe `str`: un byte de longitud y los bytes. Devuelve cuanto
// avanzo, o -1 si no cabe. Trunca a FLPW_NAME_MAX.
static int fpdWrStr(uint8_t* out, size_t cap, size_t at, const char* s){
  size_t n = s ? strnlen(s, FLPW_NAME_MAX) : 0;
  if(n > FLPW_NAME_MAX) n = FLPW_NAME_MAX;
  if(at + 1 + n > cap) return -1;
  out[at] = (uint8_t)n;
  if(n) memcpy(out + at + 1, s, n);
  return (int)(1 + n);
}

// Lee `str` dejandolo con cero final. `*at` avanza SIEMPRE lo que
// dijera la longitud (acotada a lo recibido), para que el campo
// siguiente se lea en su sitio aunque este no cupiera.
static bool fpdRdStr(const uint8_t* buf, int got, int* at, char* out, size_t outN){
  if(!buf || !at || !out || outN == 0) return false;
  out[0] = 0;
  if(*at >= got) return false;
  int n = buf[*at];
  (*at)++;
  const int avail = got - *at;
  if(n > avail) n = avail;          // un paquete que miente sobre su propio tamano
  if(n < 0) n = 0;
  int take = n;
  if(take > (int)outN - 1) take = (int)outN - 1;
  if(take > 0){ memcpy(out, buf + *at, (size_t)take); }
  out[take] = 0;
  *at += n;
  return true;
}

// =============================================================
//  1) El reloj pregunta:  "FLEXPHONE?" u8 ver
// =============================================================
static int fpdBuildProbe(uint8_t* out, size_t cap, uint8_t ver){
  if(!out || cap < FLPW_PROBE_LEN + 1) return 0;
  memcpy(out, FLPW_PROBE, FLPW_PROBE_LEN);
  out[FLPW_PROBE_LEN] = ver;
  return FLPW_PROBE_LEN + 1;
}

static bool fpdIsProbe(const uint8_t* buf, int got){
  return buf && got >= FLPW_PROBE_LEN + 1 && memcmp(buf, FLPW_PROBE, FLPW_PROBE_LEN) == 0;
}

// =============================================================
//  2) El telefono contesta: "FLEXPHONE!" u8 ver u16 puerto str nombre
// =============================================================
static bool fpdIsReply(const uint8_t* buf, int got){
  return buf && got >= FLPW_PROBE_LEN + 3 && memcmp(buf, FLPW_REPLY, FLPW_PROBE_LEN) == 0;
}

// Devuelve false si no es una respuesta legible. `name` puede ser
// NULL si no interesa; si viene, sale SIEMPRE con cero final.
static bool fpdParseReply(const uint8_t* buf, int got, uint8_t* ver,
                          uint16_t* port, char* name, size_t nameN){
  if(name && nameN) name[0] = 0;
  if(!fpdIsReply(buf, got)) return false;
  if(ver)  *ver  = buf[FLPW_PROBE_LEN];
  if(port) *port = (uint16_t)(buf[FLPW_PROBE_LEN + 1] |
                              ((uint16_t)buf[FLPW_PROBE_LEN + 2] << 8));
  if(name && nameN){
    int at = FLPW_PROBE_LEN + 3;
    char raw[FLPW_NAME_MAX + 1];
    if(fpdRdStr(buf, got, &at, raw, sizeof(raw)))
      flexLinkUtf8Copy(name, nameN, raw);   // corta por caracter, no por byte
  }
  return true;
}

// =============================================================
//  3) El telefono pregunta: "FLEXOS?" u8 ver u16 puerto str nombre
// =============================================================
static int fpdBuildAsk(uint8_t* out, size_t cap, uint8_t ver,
                       uint16_t tcpPort, const char* name){
  if(!out || cap < (size_t)FLPW_ASK_LEN + 3) return 0;
  size_t at = 0;
  memcpy(out, FLPW_ASK, FLPW_ASK_LEN); at += FLPW_ASK_LEN;
  out[at++] = ver;
  out[at++] = (uint8_t)(tcpPort & 0xFF);
  out[at++] = (uint8_t)((tcpPort >> 8) & 0xFF);
  const int n = fpdWrStr(out, cap, at, name);
  if(n < 0) return (int)at;            // sin nombre, pero valido
  return (int)at + n;
}

static bool fpdIsAsk(const uint8_t* buf, int got){
  return buf && got >= FLPW_ASK_LEN + 3 && memcmp(buf, FLPW_ASK, FLPW_ASK_LEN) == 0;
}

static bool fpdParseAsk(const uint8_t* buf, int got, uint8_t* ver,
                        uint16_t* port, char* name, size_t nameN){
  if(name && nameN) name[0] = 0;
  if(!fpdIsAsk(buf, got)) return false;
  if(ver)  *ver  = buf[FLPW_ASK_LEN];
  if(port) *port = (uint16_t)(buf[FLPW_ASK_LEN + 1] |
                              ((uint16_t)buf[FLPW_ASK_LEN + 2] << 8));
  if(name && nameN){
    int at = FLPW_ASK_LEN + 3;
    char raw[FLPW_NAME_MAX + 1];
    if(fpdRdStr(buf, got, &at, raw, sizeof(raw)))
      flexLinkUtf8Copy(name, nameN, raw);
  }
  return true;
}

// =============================================================
//  4) El reloj contesta: "FLEXOS!" u8 ver u8 flags str id str nombre
// =============================================================
static int fpdBuildAnswer(uint8_t* out, size_t cap, uint8_t ver, uint8_t flags,
                          const char* id, const char* name){
  if(!out || cap < (size_t)FLPW_ASK_LEN + 2) return 0;
  size_t at = 0;
  memcpy(out, FLPW_ANS, FLPW_ASK_LEN); at += FLPW_ASK_LEN;
  out[at++] = ver;
  out[at++] = flags;
  int n = fpdWrStr(out, cap, at, id);
  if(n < 0) return (int)at;
  at += (size_t)n;
  n = fpdWrStr(out, cap, at, name);
  if(n < 0) return (int)at;
  return (int)(at + (size_t)n);
}

static bool fpdIsAnswer(const uint8_t* buf, int got){
  return buf && got >= FLPW_ASK_LEN + 2 && memcmp(buf, FLPW_ANS, FLPW_ASK_LEN) == 0;
}

static bool fpdParseAnswer(const uint8_t* buf, int got, uint8_t* ver, uint8_t* flags,
                           char* id, size_t idN, char* name, size_t nameN){
  if(id && idN) id[0] = 0;
  if(name && nameN) name[0] = 0;
  if(!fpdIsAnswer(buf, got)) return false;
  if(ver)   *ver   = buf[FLPW_ASK_LEN];
  if(flags) *flags = buf[FLPW_ASK_LEN + 1];
  int at = FLPW_ASK_LEN + 2;
  char raw[FLPW_NAME_MAX + 1];
  // El identificador se lee SIEMPRE aunque no interese, porque el
  // nombre viene detras: saltarselo dejaria el nombre descolocado.
  if(!fpdRdStr(buf, got, &at, raw, sizeof(raw))) return true;
  if(id && idN) flexLinkUtf8Copy(id, idN, raw);
  if(fpdRdStr(buf, got, &at, raw, sizeof(raw)))
    if(name && nameN) flexLinkUtf8Copy(name, nameN, raw);
  return true;
}
