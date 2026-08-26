// #############################################################
// ##  FlexOS_Browser.cpp  ·  NUCLEO PORTABLE DEL NAVEGADOR
// ##  ------------------------------------------------------
// ##  Aqui NO hay red, NO hay dibujo y NO hay estado global de la
// ##  app: solo funciones puras. Es lo que permite compilar este
// ##  mismo fichero en el PC y probar de verdad las dos piezas que
// ##  mas facil es equivocar y mas caro es equivocar:
// ##
// ##    · el analizador del omnibox (decide si algo es una URL, una
// ##      busqueda, una pagina interna o un destino prohibido);
// ##    · el codec del protocolo FBP/1 (bytes que vienen de la red y
// ##      por tanto NO son de fiar).
// ##
// ##  Ver tests/host/test_browser.cpp.
// #############################################################

#include "FlexOS_Browser.h"

// Guardia de version (ver el bloque 0 de FlexOS_Browser.h).
static_assert(FLEXBR_BUILD == 4,
  "FlexOS_Browser.cpp y FlexOS_Browser.h son de versiones distintas: "
  "copia otra vez LOS CUATRO ficheros del navegador a la carpeta del sketch.");

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// =============================================================
//  Utilidades de cadena (sin String, sin heap)
// =============================================================
static inline char brLower(char c){ return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }
static inline bool brIsDigit(char c){ return c >= '0' && c <= '9'; }
static inline bool brIsAlpha(char c){ c = brLower(c); return c >= 'a' && c <= 'z'; }
static inline bool brIsAlnum(char c){ return brIsAlpha(c) || brIsDigit(c); }

static bool brStartsWithCI(const char* s, const char* pre){
  while(*pre){ if(brLower(*s) != brLower(*pre)) return false; s++; pre++; }
  return true;
}

static void brCopy(char* dst, size_t n, const char* src){
  if(!n) return;
  size_t i = 0;
  if(src) for(; src[i] && i + 1 < n; i++) dst[i] = src[i];
  dst[i] = 0;
}

// Copia recortando espacios por los dos lados. Devuelve la longitud.
static size_t brTrimCopy(char* dst, size_t n, const char* src){
  if(!n) return 0;
  if(!src){ dst[0] = 0; return 0; }
  const char* a = src;
  while(*a == ' ' || *a == '\t' || *a == '\r' || *a == '\n') a++;
  const char* b = a + strlen(a);
  while(b > a && (b[-1] == ' ' || b[-1] == '\t' || b[-1] == '\r' || b[-1] == '\n')) b--;
  size_t len = (size_t)(b - a);
  if(len > n - 1) len = n - 1;
  memcpy(dst, a, len);
  dst[len] = 0;
  return len;
}

// =============================================================
//  Ajustes por defecto
// =============================================================
void flexBrSettingsDefaults(BrSettings* st){
  if(!st) return;
  memset(st, 0, sizeof(*st));
  st->server[0] = 0;                 // sin servidor: la app lo pide en flex://settings
  st->token[0] = 0;
  st->searchEngine = 0;              // DuckDuckGo: no exige cuenta ni cookies
  st->searchCustom[0] = 0;
  st->profile = BRQ_BALANCED;
  st->quality = 62;
  // ESCALA POR DEFECTO. En Flex OS Pro el lienzo logico mide el doble
  // que el panel fisico (480 logicos sobre 240 reales), asi que pedir el
  // 50 % envia EXACTAMENTE los pixeles que la pantalla puede mostrar:
  // se ve igual y cuesta la mitad de bytes y de decodificacion. En las
  // demas placas el lienzo es 1:1 y no hay nada que ahorrar.
#if FLEXBR_PLAT_PRO
  st->scalePct = 50;
#else
  st->scalePct = 100;
#endif
  st->source = BRSRC_AUTO;           // de fabrica: el que este disponible
  st->cloudServer[0] = 0;            // sin nube configurada
  st->allowLocal = false;            // por seguridad, apagado
  st->allowInsecure = false;
  st->saveHistory = true;
  st->telemetry = false;
  st->mediaNative = true;
}

// =============================================================
//  DE DONDE SALE EL NAVEGADOR
// =============================================================
// Funcion PURA: recibe las disponibilidades ya medidas y decide. No
// consulta la red, no abre sockets y no adivina -- justamente para
// que quien la llame tenga que haber comprobado /v1/health antes de
// decir que una fuente esta disponible.
const char* flexBrSourceName(int src){
  switch(src){
    case BRSRC_AUTO:   return "Automatico";
    case BRSRC_PHONE:  return "Flex Phone";
    case BRSRC_CLOUD:  return "Nube";
    case BRSRC_MANUAL: return "PC/Ubuntu manual";
    default:           return "Sin fuente";
  }
}

int flexBrSourceResolve(const BrSettings* st, bool phoneUp, bool cloudCfg, bool manualCfg){
  if(!st) return BRSRC_NONE;
  switch(st->source){
    // Modos EXCLUSIVOS: si el elegido no esta, no se cae a otro por
    // detras. Un "Flex Phone" que en realidad tira del PC seria
    // exactamente el tipo de mentira que este selector evita.
    case BRSRC_PHONE:  return phoneUp   ? BRSRC_PHONE  : BRSRC_NONE;
    case BRSRC_CLOUD:  return cloudCfg  ? BRSRC_CLOUD  : BRSRC_NONE;
    case BRSRC_MANUAL: return manualCfg ? BRSRC_MANUAL : BRSRC_NONE;
    default: break;
  }
  // Automatico: telefono, luego nube, luego manual.
  if(phoneUp)   return BRSRC_PHONE;
  if(cloudCfg)  return BRSRC_CLOUD;
  if(manualCfg) return BRSRC_MANUAL;
  return BRSRC_NONE;
}

const char* flexBrSourceWhyNone(const BrSettings* st, bool phoneUp, bool cloudCfg, bool manualCfg){
  if(!st) return "Sin ajustes de navegador";
  switch(st->source){
    case BRSRC_PHONE:
      return phoneUp ? "" : "Flex Phone no tiene el Browser Relay activo";
    case BRSRC_CLOUD:
      return cloudCfg ? "" : "Falta configurar el servidor en la nube";
    case BRSRC_MANUAL:
      return manualCfg ? "" : "Falta configurar el servidor en Ajustes";
    default: break;
  }
  if(phoneUp || cloudCfg || manualCfg) return "";
  return "Sin fuente: activa el relay del telefono, configura la nube "
         "o pon el servidor del PC en Ajustes";
}

// Plantillas de buscador. Se guardan aqui y no en el .ino para que las
// tres placas usen exactamente las mismas.
static const char* kEngineTpl[3] = {
  "https://duckduckgo.com/?q=%s",
  "https://www.google.com/search?q=%s",
  "https://www.bing.com/search?q=%s"
};

// =============================================================
//  Codificacion porcentual (RFC 3986)
//  -------------------------------------------------------------
//  Se codifica TODO lo que no sea "unreserved". En particular el
//  espacio pasa a %20 y no a '+': '+' solo significa espacio dentro
//  de application/x-www-form-urlencoded, y usarlo en una ruta seria
//  incorrecto. Los bytes UTF-8 se codifican byte a byte, que es lo
//  que esperan todos los buscadores.
// =============================================================
static inline bool brUnreserved(unsigned char c){
  return brIsAlnum((char)c) || c == '-' || c == '_' || c == '.' || c == '~';
}

int flexBrPercentEncode(const char* in, char* out, size_t outN){
  if(!in || !out || outN == 0) return -1;
  static const char hex[] = "0123456789ABCDEF";
  size_t o = 0;
  for(const unsigned char* p = (const unsigned char*)in; *p; p++){
    if(brUnreserved(*p)){
      if(o + 1 >= outN){ out[0] = 0; return -1; }
      out[o++] = (char)*p;
    } else {
      if(o + 3 >= outN){ out[0] = 0; return -1; }
      out[o++] = '%';
      out[o++] = hex[*p >> 4];
      out[o++] = hex[*p & 0x0F];
    }
  }
  out[o] = 0;
  return (int)o;
}

// =============================================================
//  Paginas internas
// =============================================================
static const char* kPageName[BRP_N] = {
  "newtab", "history", "bookmarks", "settings", "downloads", "offline", "about"
};
static const char* kPageTitle[BRP_N] = {
  "Nueva pesta\xC3\xB1" "a", "Historial", "Favoritos", "Ajustes del navegador",
  "Descargas", "Sin conexi\xC3\xB3n", "Acerca de"
};

const char* flexBrPageName(int page){
  return (page >= 0 && page < BRP_N) ? kPageName[page] : "";
}
const char* flexBrPageTitle(int page){
  return (page >= 0 && page < BRP_N) ? kPageTitle[page] : "";
}

int flexBrInternalPage(const char* url){
  if(!url) return BRP_NONE;
  if(!brStartsWithCI(url, "flex://")) return BRP_NONE;
  const char* p = url + 7;
  // Se admite una barra final y nada mas: "flex://history/" vale,
  // "flex://history/algo" no. Asi una pagina interna nunca puede
  // llevar una ruta que el resto del sistema tenga que interpretar.
  char name[24]; size_t i = 0;
  while(p[i] && p[i] != '/' && p[i] != '?' && p[i] != '#' && i + 1 < sizeof(name)){
    name[i] = brLower(p[i]); i++;
  }
  name[i] = 0;
  if(p[i] == '/' && p[i + 1] != 0) return BRP_NONE;
  if(p[i] == '?' || p[i] == '#') return BRP_NONE;
  if(!name[0]) return BRP_NEWTAB;                  // "flex://" a secas
  for(int k = 0; k < BRP_N; k++) if(!strcmp(name, kPageName[k])) return k;
  return BRP_NONE;
}

// =============================================================
//  Analisis de host
//  -------------------------------------------------------------
//  POLITICA (deliberadamente estricta, y esta es la razon):
//  el servicio remoto ejecuta un Chromium DE VERDAD dentro de una red
//  que puede tener otras cosas. Si el dispositivo le pidiera
//  "http://169.254.169.254/" o "http://10.0.0.5/admin", el navegador
//  remoto lo cargaria desde DENTRO de esa red. Por eso el
//  dispositivo bloquea de entrada todo lo que huela a destino
//  interno, y el servicio vuelve a comprobarlo por su cuenta
//  (incluida la resolucion DNS y CADA redireccion). Dos barreras
//  independientes: si una falla, la otra sigue.
//
//  Con `allowLocal` (interruptor explicito de desarrollo) el
//  dispositivo deja pasar IP y nombres de una sola etiqueta, pero el
//  servicio SIGUE decidiendo: alli hace falta ademas su propia lista
//  blanca. Nunca basta con tocar el ajuste del dispositivo.
// =============================================================

// Sufijos que nunca salen de la red local.
static const char* kLocalSuffix[] = {
  ".local", ".internal", ".localdomain", ".lan", ".home", ".home.arpa",
  ".intranet", ".corp", ".private", ".test", ".invalid", ".localhost", 0
};

static bool brHostIsLocalName(const char* host){
  if(!host || !host[0]) return true;
  char h[FLEXBR_HOST_MAX];
  size_t n = 0;
  for(; host[n] && n + 1 < sizeof(h); n++) h[n] = brLower(host[n]);
  h[n] = 0;
  if(!strcmp(h, "localhost")) return true;
  for(int i = 0; kLocalSuffix[i]; i++){
    size_t sl = strlen(kLocalSuffix[i]);
    if(n > sl && !strcmp(h + n - sl, kLocalSuffix[i])) return true;
  }
  return false;
}

// "Parece una IP literal" en cualquiera de las formas que aceptan los
// navegadores: cuatro octetos con puntos, decimal a secas, hexadecimal
// y IPv6 (con o sin corchetes). NO se intenta averiguar QUE direccion
// es: todas se bloquean igual salvo con el permiso de desarrollo, asi
// que no hace falta acertar con las variantes raras (010.0.0.1 en
// octal, 2130706433 en decimal, 0x7f000001 en hexadecimal...).
//
// La deteccion es deliberadamente ESTRECHA en el caso "digitos y
// puntos": exige CUATRO etiquetas. Sin eso, "3.14159" o "version 1.2"
// se bloquearian como si fueran direcciones IP en vez de irse al
// buscador, que es lo que el usuario quiere. Lo que no sea ni IP ni
// dominio publico plausible cae igualmente por el otro filtro.
static bool brHostIsIpLiteral(const char* host){
  if(!host || !host[0]) return false;
  if(host[0] == '[') return true;                       // IPv6 entre corchetes
  if(strchr(host, ':')) return true;                    // IPv6 sin corchetes
  if(brStartsWithCI(host, "0x")){                       // 0x7f000001
    bool onlyHex = true;
    for(const char* p = host + 2; *p; p++){
      char c = brLower(*p);
      if(!(brIsDigit(c) || (c >= 'a' && c <= 'f'))){ onlyHex = false; break; }
    }
    if(onlyHex && host[2]) return true;
  }
  bool onlyDigitDot = true;
  int labels = 1;
  for(const char* p = host; *p; p++){
    if(*p == '.') labels++;
    else if(!brIsDigit(*p)){ onlyDigitDot = false; break; }
  }
  if(!onlyDigitDot) return false;
  if(labels == 1) return true;                          // 2130706433 (decimal)
  return labels == 4;                                   // 1.2.3.4 · 010.0.0.1
}

// Comprueba que el host es un nombre de dominio publico plausible:
// etiquetas alfanumericas separadas por puntos, con un TLD de al menos
// dos letras. Todo ASCII: un host con bytes >= 0x80 necesitaria IDNA
// (punycode) para ser correcto, y adivinar la conversion seria peor que
// mandarlo al buscador -- que es lo que se hace.
static bool brHostLooksPublicDomain(const char* host){
  if(!host || !host[0]) return false;
  size_t total = strlen(host);
  if(total > 253) return false;
  int labels = 0;
  const char* p = host;
  const char* lastLabel = host;
  while(*p){
    const char* start = p;
    while(*p && *p != '.') p++;
    size_t len = (size_t)(p - start);
    if(len == 0 || len > 63) return false;
    if(start[0] == '-' || start[len - 1] == '-') return false;
    for(size_t i = 0; i < len; i++){
      unsigned char c = (unsigned char)start[i];
      if(c >= 0x80) return false;                       // sin IDNA: al buscador
      if(!brIsAlnum((char)c) && c != '-') return false;
    }
    lastLabel = start;
    labels++;
    if(*p == '.') p++;
    else break;
  }
  if(labels < 2) return false;                          // hace falta al menos "algo.tld"
  size_t tl = strlen(lastLabel);
  if(tl < 2) return false;
  for(size_t i = 0; i < tl; i++) if(!brIsAlpha(lastLabel[i])) return false;
  return true;
}

// Separa el host (y valida el puerto) de una autoridad "host:puerto".
// Devuelve BRB_* .
static int brSplitAuthority(const char* auth, char* host, size_t hostN){
  if(!auth || !auth[0]) return BRB_NOHOST;
  if(strchr(auth, '@')) return BRB_USERINFO;
  const char* portPart = NULL;
  size_t hlen;
  if(auth[0] == '['){                                   // IPv6 literal
    const char* close = strchr(auth, ']');
    if(!close) return BRB_NOHOST;
    hlen = (size_t)(close - auth) + 1;
    if(close[1] == ':') portPart = close + 2;
    else if(close[1] != 0) return BRB_NOHOST;
  } else {
    const char* colon = strrchr(auth, ':');
    hlen = colon ? (size_t)(colon - auth) : strlen(auth);
    if(colon) portPart = colon + 1;
  }
  if(hlen == 0 || hlen >= hostN) return BRB_NOHOST;
  memcpy(host, auth, hlen); host[hlen] = 0;
  if(portPart){
    if(!portPart[0]) return BRB_NOHOST;
    long v = 0;
    for(const char* p = portPart; *p; p++){
      if(!brIsDigit(*p)) return BRB_NOHOST;
      v = v * 10 + (*p - '0');
      if(v > 65535) return BRB_NOHOST;
    }
    if(v == 0) return BRB_NOHOST;
  }
  return BRB_NONE;
}

// =============================================================
//  Validacion de una URL http(s) completa
// =============================================================
int flexBrCheckUrl(const char* url, const BrSettings* st){
  if(!url || !url[0]) return BRB_NOHOST;
  size_t len = strlen(url);
  if(len >= FLEXBR_URL_MAX) return BRB_TOOLONG;

  // Caracteres de control y espacios en crudo: una URL correcta los
  // lleva codificados. Dejarlos pasar es justo por donde se cuelan las
  // inyecciones de cabecera y las URL que "parecen" otra cosa.
  for(size_t i = 0; i < len; i++){
    unsigned char c = (unsigned char)url[i];
    if(c < 0x21 || c == 0x7F) return BRB_BADCHARS;
    if(c == '"' || c == '<' || c == '>' || c == '\\' || c == '^' || c == '`' ||
       c == '{' || c == '}' || c == '|') return BRB_BADCHARS;
  }

  const char* rest;
  if(brStartsWithCI(url, "https://")) rest = url + 8;
  else if(brStartsWithCI(url, "http://")) rest = url + 7;
  else return BRB_SCHEME;

  char auth[FLEXBR_HOST_MAX + 16];
  size_t a = 0;
  while(rest[a] && rest[a] != '/' && rest[a] != '?' && rest[a] != '#'){
    if(a + 1 >= sizeof(auth)) return BRB_NOHOST;
    auth[a] = rest[a]; a++;
  }
  auth[a] = 0;

  char host[FLEXBR_HOST_MAX];
  int r = brSplitAuthority(auth, host, sizeof(host));
  if(r != BRB_NONE) return r;

  bool allowLocal = st && st->allowLocal;
  if(brHostIsIpLiteral(host)) return allowLocal ? BRB_NONE : BRB_PRIVATE_IP;
  if(brHostIsLocalName(host)) return allowLocal ? BRB_NONE : BRB_LOCALHOST;
  if(!brHostLooksPublicDomain(host)) return allowLocal ? BRB_NONE : BRB_LOCALHOST;
  return BRB_NONE;
}

bool flexBrUrlHost(const char* url, char* out, size_t outN){
  if(!url || !out || outN == 0) return false;
  out[0] = 0;
  const char* rest;
  if(brStartsWithCI(url, "https://")) rest = url + 8;
  else if(brStartsWithCI(url, "http://")) rest = url + 7;
  else return false;
  char auth[FLEXBR_HOST_MAX + 16];
  size_t a = 0;
  while(rest[a] && rest[a] != '/' && rest[a] != '?' && rest[a] != '#'){
    if(a + 1 >= sizeof(auth)) return false;
    auth[a] = rest[a]; a++;
  }
  auth[a] = 0;
  char host[FLEXBR_HOST_MAX];
  if(brSplitAuthority(auth, host, sizeof(host)) != BRB_NONE) return false;
  brCopy(out, outN, host);
  return true;
}

void flexBrUrlLabel(const char* url, char* out, size_t outN){
  if(!out || outN == 0) return;
  out[0] = 0;
  if(!url || !url[0]) return;
  int page = flexBrInternalPage(url);
  if(page != BRP_NONE){ brCopy(out, outN, flexBrPageTitle(page)); return; }
  char host[FLEXBR_HOST_MAX];
  if(!flexBrUrlHost(url, host, sizeof(host))){ brCopy(out, outN, url); return; }
  const char* h = host;
  if(brStartsWithCI(h, "www.")) h += 4;
  brCopy(out, outN, h);
}

// =============================================================
//  RESOLUCION DEL OMNIBOX
//  -------------------------------------------------------------
//  El orden es exactamente el que pide la especificacion, y cada paso
//  esta antes que el siguiente por un motivo:
//
//   1. Vacio            -> no se navega (la interfaz decide si abre
//                          una pestana nueva o se queda donde esta).
//   2. flex://          -> pagina interna. Se resuelve AQUI, antes que
//                          nada, para que jamas salga hacia el
//                          servidor: una pagina externa no puede
//                          provocar un flex:// y el servicio no
//                          necesita conocer el esquema.
//   3. http:// https:// -> se valida y se navega.
//   4. Otro esquema     -> BLOQUEADO. file://, javascript:, data:,
//                          ftp://, about:, chrome:... ninguno tiene
//                          sentido aqui y varios son peligrosos.
//   5. Dominio sin esquema -> se completa con https:// (nunca http:
//                          si el sitio no habla https, el servicio
//                          informara; degradar solos seria peor).
//   6. IP o host local  -> BLOQUEADO salvo permiso de desarrollo.
//   7. Todo lo demas    -> busqueda, con el texto codificado entero.
// =============================================================
int flexBrResolveInput(const char* text, const BrSettings* st, BrResolved* out){
  if(!out) return BRI_EMPTY;
  memset(out, 0, sizeof(*out));
  out->page = BRP_NONE;
  out->block = BRB_NONE;

  BrSettings defs;
  if(!st){ flexBrSettingsDefaults(&defs); st = &defs; }

  char in[FLEXBR_INPUT_MAX];
  // El recorte se hace sobre un buffer acotado: una entrada mas larga
  // que FLEXBR_INPUT_MAX no se trunca en silencio, se rechaza.
  if(text && strlen(text) >= FLEXBR_INPUT_MAX){
    out->kind = BRI_BLOCKED; out->block = BRB_TOOLONG; return out->kind;
  }
  size_t n = brTrimCopy(in, sizeof(in), text);
  if(n == 0){ out->kind = BRI_EMPTY; return out->kind; }

  // Caracteres de control en cualquier posicion: fuera. Un \r o un \n
  // en medio de una URL es siempre un intento de inyeccion.
  for(size_t i = 0; i < n; i++){
    unsigned char c = (unsigned char)in[i];
    if(c < 0x20 || c == 0x7F){ out->kind = BRI_BLOCKED; out->block = BRB_BADCHARS; return out->kind; }
  }

  // ---- 2) Esquema interno ----
  if(brStartsWithCI(in, "flex:")){
    int page = flexBrInternalPage(in);
    if(page == BRP_NONE){ out->kind = BRI_BLOCKED; out->block = BRB_SCHEME; return out->kind; }
    out->kind = BRI_INTERNAL; out->page = page;
    snprintf(out->url, sizeof(out->url), "flex://%s", flexBrPageName(page));
    return out->kind;
  }

  // ---- Deteccion de esquema generico ----
  // Dos matices que parecen detalles y no lo son:
  //  · el PUNTO no cuenta como caracter de esquema. RFC 3986 lo
  //    permite, pero en un omnibox "sub.dominio.org:8080/x" es un
  //    host con puerto, no un esquema llamado "sub.dominio.org".
  //  · si detras de los dos puntos vienen SOLO digitos, es un puerto:
  //    "localhost:3000" no es el esquema "localhost".
  size_t sl = 0;
  while(sl < n && (brIsAlnum(in[sl]) || in[sl] == '+' || in[sl] == '-')) sl++;
  bool hasScheme = (sl > 0 && sl < n && in[sl] == ':' && brIsAlpha(in[0]));
  if(hasScheme){
    size_t d = sl + 1;
    while(d < n && brIsDigit(in[d])) d++;
    bool allDigits = (d > sl + 1) && (d == n || in[d] == '/' || in[d] == '?' || in[d] == '#');
    if(allDigits) hasScheme = false;                    // era host:puerto
  }

  if(hasScheme){
    if(brStartsWithCI(in, "http://") || brStartsWithCI(in, "https://")){
      int b = flexBrCheckUrl(in, st);
      if(b != BRB_NONE){ out->kind = BRI_BLOCKED; out->block = b; return out->kind; }
      out->kind = BRI_URL; brCopy(out->url, sizeof(out->url), in);
      return out->kind;
    }
    // ---- 4) Cualquier otro esquema ----
    out->kind = BRI_BLOCKED; out->block = BRB_SCHEME;
    return out->kind;
  }

  // ---- 5/6) Sin esquema: puede ser un dominio ----
  // Un espacio en cualquier parte descarta que sea un host y lo manda
  // directo a la busqueda; asi "hola mundo.com" no se convierte en una
  // URL rara.
  bool hasSpace = (strchr(in, ' ') != NULL);
  if(!hasSpace){
    char auth[FLEXBR_HOST_MAX + 16];
    size_t a = 0;
    while(a < n && in[a] != '/' && in[a] != '?' && in[a] != '#' && a + 1 < sizeof(auth)){
      auth[a] = in[a]; a++;
    }
    auth[a] = 0;
    if(auth[0] && !strchr(auth, '@')){
      char host[FLEXBR_HOST_MAX];
      int sp = brSplitAuthority(auth, host, sizeof(host));
      if(sp == BRB_NONE){
        bool ip    = brHostIsIpLiteral(host);
        bool local = brHostIsLocalName(host);
        bool pub   = brHostLooksPublicDomain(host);
        if(ip || local){
          if(!st->allowLocal){
            out->kind = BRI_BLOCKED;
            out->block = ip ? BRB_PRIVATE_IP : BRB_LOCALHOST;
            return out->kind;
          }
          pub = true;                    // con permiso de desarrollo se deja pasar
        }
        if(pub){
          char full[FLEXBR_URL_MAX];
          int w = snprintf(full, sizeof(full), "https://%s", in);
          if(w < 0 || (size_t)w >= sizeof(full)){
            out->kind = BRI_BLOCKED; out->block = BRB_TOOLONG; return out->kind;
          }
          int b = flexBrCheckUrl(full, st);
          if(b != BRB_NONE){ out->kind = BRI_BLOCKED; out->block = b; return out->kind; }
          out->kind = BRI_URL; brCopy(out->url, sizeof(out->url), full);
          return out->kind;
        }
      }
    }
  }

  // ---- 7) Busqueda ----
  {
    char enc[FLEXBR_URL_MAX];
    if(flexBrPercentEncode(in, enc, sizeof(enc)) < 0){
      out->kind = BRI_BLOCKED; out->block = BRB_TOOLONG; return out->kind;
    }
    const char* tpl;
    if(st->searchEngine == 3 && st->searchCustom[0]) tpl = st->searchCustom;
    else tpl = kEngineTpl[st->searchEngine < 3 ? st->searchEngine : 0];

    // La plantilla se expande A MANO buscando "%s": no se pasa a
    // snprintf con formato variable, que es como se cuela un "%n" en
    // una plantilla personalizada.
    const char* pct = strstr(tpl, "%s");
    if(!pct){ out->kind = BRI_BLOCKED; out->block = BRB_BADCHARS; return out->kind; }
    size_t pre = (size_t)(pct - tpl), post = strlen(pct + 2), el = strlen(enc);
    if(pre + el + post >= sizeof(out->url)){
      out->kind = BRI_BLOCKED; out->block = BRB_TOOLONG; return out->kind;
    }
    memcpy(out->url, tpl, pre);
    memcpy(out->url + pre, enc, el);
    memcpy(out->url + pre + el, pct + 2, post);
    out->url[pre + el + post] = 0;

    // Una plantilla personalizada tambien se valida: si apunta a un
    // destino interno, se bloquea igual que cualquier otra URL.
    int b = flexBrCheckUrl(out->url, st);
    if(b != BRB_NONE){ out->kind = BRI_BLOCKED; out->block = b; out->url[0] = 0; return out->kind; }
    out->kind = BRI_SEARCH;
    return out->kind;
  }
}

const char* flexBrBlockReason(int block){
  switch(block){
    case BRB_SCHEME:     return "Ese tipo de direcci\xC3\xB3n no se puede abrir aqu\xC3\xAD";
    case BRB_LOCALHOST:  return "Los nombres de red local est\xC3\xA1n bloqueados";
    case BRB_PRIVATE_IP: return "Las direcciones IP directas est\xC3\xA1n bloqueadas";
    case BRB_USERINFO:   return "La direcci\xC3\xB3n lleva usuario y contrase\xC3\xB1" "a incrustados";
    case BRB_TOOLONG:    return "La direcci\xC3\xB3n es demasiado larga";
    case BRB_BADCHARS:   return "La direcci\xC3\xB3n tiene caracteres no permitidos";
    case BRB_NOHOST:     return "No se reconoce el sitio de destino";
    default:             return "";
  }
}

const char* flexBrErrorText(int code){
  switch(code){
    case FBP_ERR_AUTH:     return "El servicio rechaz\xC3\xB3 este dispositivo";
    case FBP_ERR_BLOCKED:  return "El servicio bloque\xC3\xB3 ese destino";
    case FBP_ERR_NAV:      return "No se pudo cargar la p\xC3\xA1gina";
    case FBP_ERR_TIMEOUT:  return "Se agot\xC3\xB3 el tiempo de espera";
    case FBP_ERR_LIMIT:    return "L\xC3\xADmite del servicio alcanzado";
    case FBP_ERR_CERT:     return "El certificado del sitio no es v\xC3\xA1lido";
    case FBP_ERR_PROTOCOL: return "Respuesta del servicio no v\xC3\xA1lida";
    case FBP_ERR_EXPIRED:  return "La sesi\xC3\xB3n caduc\xC3\xB3 por inactividad";
    case FBP_ERR_INTERNAL: return "Error interno del servicio";
    default:               return "Error desconocido";
  }
}

// =============================================================
//  CODEC DEL PROTOCOLO FBP/1
//  -------------------------------------------------------------
//  Todo lo que se lee viene de la red: cada lector comprueba que hay
//  bytes suficientes ANTES de tocarlos y no confia en ninguna longitud
//  declarada sin contrastarla con los bytes que de verdad hay.
// =============================================================
static inline void wr16(uint8_t* p, uint16_t v){ p[0] = (uint8_t)(v & 0xFF); p[1] = (uint8_t)(v >> 8); }
static inline void wr32(uint8_t* p, uint32_t v){
  p[0] = (uint8_t)(v & 0xFF); p[1] = (uint8_t)((v >> 8) & 0xFF);
  p[2] = (uint8_t)((v >> 16) & 0xFF); p[3] = (uint8_t)((v >> 24) & 0xFF);
}
static inline uint16_t rd16le(const uint8_t* p){ return (uint16_t)(p[0] | (p[1] << 8)); }
static inline uint32_t rd32le(const uint8_t* p){
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int fbpWriteHeader(uint8_t* buf, uint8_t type, uint8_t flags, uint8_t channel,
                   uint16_t seq, uint32_t len){
  if(!buf) return -1;
  buf[0] = FBP_MAGIC0; buf[1] = FBP_MAGIC1; buf[2] = FBP_VERSION;
  buf[3] = type; buf[4] = flags; buf[5] = channel;
  wr16(buf + 6, seq);
  wr32(buf + 8, len);
  return FBP_HDR_SIZE;
}

bool fbpReadHeader(const uint8_t* buf, size_t n, FbpHeader* out){
  if(!buf || !out || n < FBP_HDR_SIZE) return false;
  if(buf[0] != FBP_MAGIC0 || buf[1] != FBP_MAGIC1) return false;
  if(buf[2] != FBP_VERSION) return false;
  uint32_t len = rd32le(buf + 8);
  if(len > FBP_PAYLOAD_MAX) return false;
  out->type = buf[3]; out->flags = buf[4]; out->channel = buf[5];
  out->seq = rd16le(buf + 6); out->len = len;
  return true;
}

// Escribe una cadena con longitud por delante. Devuelve los bytes
// escritos o -1 si no cabe.
static int fbpPutStr(uint8_t* p, size_t room, const char* s){
  size_t l = s ? strlen(s) : 0;
  if(l > 0xFFFF) return -1;
  if(room < l + 2) return -1;
  wr16(p, (uint16_t)l);
  if(l) memcpy(p + 2, s, l);
  return (int)(l + 2);
}

// Lee una cadena con longitud por delante hacia un buffer fijo. Avanza
// `off`. Si la cadena declarada no cabe en el mensaje, falla.
static bool fbpGetStr(const uint8_t* p, uint32_t n, uint32_t* off, char* out, size_t outN){
  if(*off + 2 > n) return false;
  uint16_t l = rd16le(p + *off);
  *off += 2;
  if(*off + l > n) return false;
  if(out && outN){
    size_t c = l < outN - 1 ? l : outN - 1;
    memcpy(out, p + *off, c);
    out[c] = 0;
    // Un titulo o una URL que venga con bytes de control se limpia: si
    // llegara tal cual al motor de texto, un "\n" partiria la linea de
    // la barra y una pagina externa podria falsificar la interfaz.
    for(size_t i = 0; i < c; i++) if((unsigned char)out[i] < 0x20) out[i] = ' ';
  }
  *off += l;
  return true;
}

int fbpBuildHello(uint8_t* buf, size_t n, uint16_t seq, const char* token,
                  const char* deviceId, uint16_t w, uint16_t h,
                  uint8_t quality, uint8_t profile, uint32_t caps){
  if(!buf || n < FBP_HDR_SIZE + 16) return -1;
  uint8_t* p = buf + FBP_HDR_SIZE;
  size_t room = n - FBP_HDR_SIZE, o = 0;
  if(room < 13) return -1;
  p[o++] = FBP_VERSION;
  wr16(p + o, w); o += 2;
  wr16(p + o, h); o += 2;
  p[o++] = quality; p[o++] = profile;
  wr32(p + o, caps); o += 4;
  p[o++] = FBP_IMG_JPEG;                 // formatos que sabemos decodificar
  p[o++] = FLEXBR_MAX_TABS;
  wr32(p + o, (uint32_t)FLEXBR_KEYFRAME_MAX); o += 4;
  int r = fbpPutStr(p + o, room - o, token);    if(r < 0) return -1; o += (size_t)r;
  r = fbpPutStr(p + o, room - o, deviceId);     if(r < 0) return -1; o += (size_t)r;
  fbpWriteHeader(buf, FBP_C_HELLO, 0, 0, seq, (uint32_t)o);
  return (int)(FBP_HDR_SIZE + o);
}

int fbpBuildNavigate(uint8_t* buf, size_t n, uint16_t seq, uint8_t ch, const char* url){
  if(!buf || n < FBP_HDR_SIZE + 2) return -1;
  int r = fbpPutStr(buf + FBP_HDR_SIZE, n - FBP_HDR_SIZE, url);
  if(r < 0) return -1;
  fbpWriteHeader(buf, FBP_C_NAVIGATE, 0, ch, seq, (uint32_t)r);
  return FBP_HDR_SIZE + r;
}

int fbpBuildSimple(uint8_t* buf, size_t n, uint16_t seq, uint8_t ch, uint8_t type){
  if(!buf || n < FBP_HDR_SIZE) return -1;
  fbpWriteHeader(buf, type, 0, ch, seq, 0);
  return FBP_HDR_SIZE;
}

int fbpBuildPointer(uint8_t* buf, size_t n, uint16_t seq, uint8_t ch,
                    uint8_t action, uint16_t x, uint16_t y, uint8_t button){
  if(!buf || n < FBP_HDR_SIZE + 6) return -1;
  uint8_t* p = buf + FBP_HDR_SIZE;
  p[0] = action; wr16(p + 1, x); wr16(p + 3, y); p[5] = button;
  fbpWriteHeader(buf, FBP_C_POINTER, 0, ch, seq, 6);
  return FBP_HDR_SIZE + 6;
}

int fbpBuildScroll(uint8_t* buf, size_t n, uint16_t seq, uint8_t ch, int16_t dx, int16_t dy){
  if(!buf || n < FBP_HDR_SIZE + 4) return -1;
  uint8_t* p = buf + FBP_HDR_SIZE;
  wr16(p, (uint16_t)dx); wr16(p + 2, (uint16_t)dy);
  fbpWriteHeader(buf, FBP_C_SCROLL, 0, ch, seq, 4);
  return FBP_HDR_SIZE + 4;
}

int fbpBuildKey(uint8_t* buf, size_t n, uint16_t seq, uint8_t ch,
                uint8_t action, uint8_t code, uint8_t mods, const char* text){
  if(!buf || n < FBP_HDR_SIZE + 5) return -1;
  uint8_t* p = buf + FBP_HDR_SIZE;
  p[0] = action; p[1] = code; p[2] = mods;
  int r = fbpPutStr(p + 3, n - FBP_HDR_SIZE - 3, text);
  if(r < 0) return -1;
  fbpWriteHeader(buf, FBP_C_KEY, 0, ch, seq, (uint32_t)(3 + r));
  return FBP_HDR_SIZE + 3 + r;
}

int fbpBuildViewport(uint8_t* buf, size_t n, uint16_t seq, uint8_t ch,
                     uint16_t w, uint16_t h, uint8_t quality, uint8_t profile, uint8_t scalePct){
  if(!buf || n < FBP_HDR_SIZE + 7) return -1;
  uint8_t* p = buf + FBP_HDR_SIZE;
  wr16(p, w); wr16(p + 2, h); p[4] = quality; p[5] = profile; p[6] = scalePct;
  fbpWriteHeader(buf, FBP_C_VIEWPORT, 0, ch, seq, 7);
  return FBP_HDR_SIZE + 7;
}

int fbpBuildAck(uint8_t* buf, size_t n, uint16_t seq, uint16_t lastSeq, uint8_t pending){
  if(!buf || n < FBP_HDR_SIZE + 3) return -1;
  uint8_t* p = buf + FBP_HDR_SIZE;
  wr16(p, lastSeq); p[2] = pending;
  fbpWriteHeader(buf, FBP_C_ACK, 0, 0, seq, 3);
  return FBP_HDR_SIZE + 3;
}

int fbpBuildMedia(uint8_t* buf, size_t n, uint16_t seq, uint8_t ch, uint8_t cmd, uint32_t arg){
  if(!buf || n < FBP_HDR_SIZE + 5) return -1;
  uint8_t* p = buf + FBP_HDR_SIZE;
  p[0] = cmd; wr32(p + 1, arg);
  fbpWriteHeader(buf, FBP_C_MEDIA, 0, ch, seq, 5);
  return FBP_HDR_SIZE + 5;
}

// ---- Lectores ----
bool fbpParseWelcome(const uint8_t* p, uint32_t n, FbpWelcome* out){
  if(!p || !out || n < 15) return false;
  FbpWelcome w; memset(&w, 0, sizeof(w));
  uint32_t o = 0;
  w.protoVer = p[o++];
  if(w.protoVer != FBP_VERSION) return false;
  w.viewW = rd16le(p + o); o += 2;
  w.viewH = rd16le(p + o); o += 2;
  w.caps = rd32le(p + o); o += 4;
  w.maxTabs = rd16le(p + o); o += 2;
  w.maxFrameBytes = rd32le(p + o); o += 4;
  if(!fbpGetStr(p, n, &o, w.sessionId, sizeof(w.sessionId))) return false;
  if(w.viewW == 0 || w.viewH == 0 || w.viewW > 4096 || w.viewH > 4096) return false;
  *out = w;
  return true;
}

bool fbpParseState(const uint8_t* p, uint32_t n, FbpState* out){
  if(!p || !out || n < 2) return false;
  FbpState s; memset(&s, 0, sizeof(s));
  uint32_t o = 0;
  s.flags = p[o++];
  s.progress = p[o++];
  if(s.progress > 100) s.progress = 100;
  if(!fbpGetStr(p, n, &o, s.title, sizeof(s.title))) return false;
  if(!fbpGetStr(p, n, &o, s.url, sizeof(s.url))) return false;
  *out = s;
  return true;
}

bool fbpParseFrame(const uint8_t* p, uint32_t n, FbpFrame* out){
  if(!p || !out || n < 18) return false;
  FbpFrame f; memset(&f, 0, sizeof(f));
  uint32_t o = 0;
  f.x = rd16le(p + o); o += 2;
  f.y = rd16le(p + o); o += 2;
  f.w = rd16le(p + o); o += 2;
  f.h = rd16le(p + o); o += 2;
  f.format = p[o++]; f.flags = p[o++];
  f.frameId = rd32le(p + o); o += 4;
  uint32_t imgLen = rd32le(p + o); o += 4;
  if(o + imgLen != n) return false;          // la longitud declarada tiene que cuadrar EXACTA
  if(f.format != FBP_IMG_JPEG) return false;
  if(f.w == 0 || f.h == 0 || f.w > 4096 || f.h > 4096) return false;
  if(imgLen == 0) return false;
  f.img = p + o; f.imgLen = imgLen;
  *out = f;
  return true;
}

bool fbpParseMedia(const uint8_t* p, uint32_t n, FbpMedia* out){
  if(!p || !out || n < 9) return false;
  FbpMedia m; memset(&m, 0, sizeof(m));
  uint32_t o = 0;
  m.kind = p[o++];
  m.durationMs = rd32le(p + o); o += 4;
  m.w = rd16le(p + o); o += 2;
  m.h = rd16le(p + o); o += 2;
  if(!fbpGetStr(p, n, &o, m.mime, sizeof(m.mime))) return false;
  if(!fbpGetStr(p, n, &o, m.url, sizeof(m.url))) return false;
  if(!fbpGetStr(p, n, &o, m.title, sizeof(m.title))) return false;
  *out = m;
  return true;
}

// =============================================================
//  APRETON DE MANOS WEBSOCKET (RFC 6455)
//  -------------------------------------------------------------
//  SHA-1 y Base64 se escriben aqui, y no se toman de mbedTLS, por
//  dos razones practicas: (a) asi este calculo se puede PROBAR en el
//  PC contra el vector del propio RFC, y (b) la API de mbedTLS ha
//  cambiado de firma entre versiones del IDF y una compilacion rota
//  por eso seria un fallo tonto en las tres placas a la vez.
//  SHA-1 se usa SOLO para el apreton de manos del WebSocket, que es
//  exactamente para lo que el estandar lo define; no se usa como
//  primitiva de seguridad en ningun otro sitio.
// =============================================================
static inline uint32_t sha1Rol(uint32_t v, int n){ return (v << n) | (v >> (32 - n)); }

void flexBrSha1(const uint8_t* data, size_t n, uint8_t out[20]){
  uint32_t h[5] = { 0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u };
  uint64_t bits = (uint64_t)n * 8;
  size_t total = n + 1;
  while(total % 64 != 56) total++;
  total += 8;

  uint8_t blk[64];
  for(size_t off = 0; off < total; off += 64){
    for(int i = 0; i < 64; i++){
      size_t idx = off + (size_t)i;
      if(idx < n) blk[i] = data[idx];
      else if(idx == n) blk[i] = 0x80;
      else if(idx < total - 8) blk[i] = 0x00;
      else blk[i] = (uint8_t)(bits >> (8 * (total - 1 - idx)));
    }
    uint32_t w[80];
    for(int i = 0; i < 16; i++)
      w[i] = ((uint32_t)blk[i * 4] << 24) | ((uint32_t)blk[i * 4 + 1] << 16) |
             ((uint32_t)blk[i * 4 + 2] << 8) | (uint32_t)blk[i * 4 + 3];
    for(int i = 16; i < 80; i++) w[i] = sha1Rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for(int i = 0; i < 80; i++){
      uint32_t f, k;
      if(i < 20){      f = (b & c) | ((~b) & d);        k = 0x5A827999u; }
      else if(i < 40){ f = b ^ c ^ d;                   k = 0x6ED9EBA1u; }
      else if(i < 60){ f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDCu; }
      else {           f = b ^ c ^ d;                   k = 0xCA62C1D6u; }
      uint32_t t = sha1Rol(a, 5) + f + e + k + w[i];
      e = d; d = c; c = sha1Rol(b, 30); b = a; a = t;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
  }
  for(int i = 0; i < 5; i++){
    out[i * 4]     = (uint8_t)(h[i] >> 24);
    out[i * 4 + 1] = (uint8_t)(h[i] >> 16);
    out[i * 4 + 2] = (uint8_t)(h[i] >> 8);
    out[i * 4 + 3] = (uint8_t)(h[i]);
  }
}

int flexBrBase64(const uint8_t* in, size_t n, char* out, size_t outN){
  static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t need = ((n + 2) / 3) * 4;
  if(!out || outN < need + 1) return -1;
  size_t o = 0;
  for(size_t i = 0; i < n; i += 3){
    uint32_t v = (uint32_t)in[i] << 16;
    if(i + 1 < n) v |= (uint32_t)in[i + 1] << 8;
    if(i + 2 < n) v |= (uint32_t)in[i + 2];
    out[o++] = T[(v >> 18) & 0x3F];
    out[o++] = T[(v >> 12) & 0x3F];
    out[o++] = (i + 1 < n) ? T[(v >> 6) & 0x3F] : '=';
    out[o++] = (i + 2 < n) ? T[v & 0x3F] : '=';
  }
  out[o] = 0;
  return (int)o;
}

bool flexBrWsAccept(const char* clientKeyB64, char* out, size_t outN){
  static const char* GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
  if(!clientKeyB64 || !out) return false;
  char cat[128];
  size_t kl = strlen(clientKeyB64), gl = strlen(GUID);
  if(kl + gl + 1 > sizeof(cat)) return false;
  memcpy(cat, clientKeyB64, kl);
  memcpy(cat + kl, GUID, gl);
  cat[kl + gl] = 0;
  uint8_t dig[20];
  flexBrSha1((const uint8_t*)cat, kl + gl, dig);
  return flexBrBase64(dig, 20, out, outN) > 0;
}

bool flexBrParseWsUrl(const char* url, bool* tls, char* host, size_t hostN,
                      uint16_t* port, char* path, size_t pathN){
  if(!url || !tls || !host || !port || !path) return false;
  bool secure;
  const char* rest;
  if(brStartsWithCI(url, "wss://")){ secure = true;  rest = url + 6; }
  else if(brStartsWithCI(url, "ws://")){ secure = false; rest = url + 5; }
  else return false;

  char auth[FLEXBR_HOST_MAX + 16];
  size_t a = 0;
  while(rest[a] && rest[a] != '/' && rest[a] != '?' && rest[a] != '#'){
    if(a + 1 >= sizeof(auth)) return false;
    auth[a] = rest[a]; a++;
  }
  auth[a] = 0;
  if(strchr(auth, '@')) return false;          // el token va en el HELLO, no en la URL

  char h[FLEXBR_HOST_MAX];
  uint16_t p = secure ? 443 : 80;
  if(auth[0] == '['){
    const char* close = strchr(auth, ']');
    if(!close) return false;
    size_t hl = (size_t)(close - auth) + 1;
    if(hl >= sizeof(h)) return false;
    memcpy(h, auth, hl); h[hl] = 0;
    if(close[1] == ':'){ long v = atol(close + 2); if(v <= 0 || v > 65535) return false; p = (uint16_t)v; }
    else if(close[1] != 0) return false;
  } else {
    const char* colon = strrchr(auth, ':');
    size_t hl = colon ? (size_t)(colon - auth) : strlen(auth);
    if(hl == 0 || hl >= sizeof(h)) return false;
    memcpy(h, auth, hl); h[hl] = 0;
    if(colon){
      long v = 0;
      for(const char* q = colon + 1; *q; q++){ if(!brIsDigit(*q)) return false; v = v * 10 + (*q - '0'); }
      if(v <= 0 || v > 65535) return false;
      p = (uint16_t)v;
    }
  }
  const char* pp = rest + a;
  if(!pp[0]) pp = "/";
  if(strlen(pp) + 1 > pathN) return false;
  brCopy(path, pathN, pp);
  brCopy(host, hostN, h);
  *tls = secure; *port = p;
  return true;
}

bool fbpParseError(const uint8_t* p, uint32_t n, FbpError* out){
  if(!p || !out || n < 2) return false;
  FbpError e; memset(&e, 0, sizeof(e));
  uint32_t o = 0;
  e.code = rd16le(p + o); o += 2;
  if(!fbpGetStr(p, n, &o, e.msg, sizeof(e.msg))) return false;
  *out = e;
  return true;
}
