// #############################################################
//  test_browser.cpp  ·  pruebas de host del nucleo del navegador
//  (FlexOS_Browser.cpp: omnibox, validacion de URL y codec FBP/1)
// #############################################################
//
//  Se compila el MISMO fichero que usa el firmware. Todo lo que se
//  prueba aqui es codigo que en la placa recibe datos que no son de
//  fiar: texto tecleado por el usuario y bytes venidos de la red.

#include "../../FlexOS_Browser.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>

static int g_fail = 0, g_run = 0;
#define CHECK(cond, ...) do { g_run++; if(!(cond)){ g_fail++; \
  std::printf("  FALLO %s:%d  ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while(0)

static const char* kindName(int k){
  switch(k){
    case BRI_EMPTY: return "VACIO";
    case BRI_INTERNAL: return "INTERNA";
    case BRI_URL: return "URL";
    case BRI_SEARCH: return "BUSQUEDA";
    case BRI_BLOCKED: return "BLOQUEADO";
    default: return "?";
  }
}

// -------------------------------------------------------------
//  1) Omnibox
// -------------------------------------------------------------
static void expectKind(const BrSettings& st, const char* in, int kind, const char* url = nullptr){
  BrResolved r;
  int k = flexBrResolveInput(in, &st, &r);
  if(k != kind){
    g_run++; g_fail++;
    std::printf("  FALLO  \"%s\" -> %s (esperado %s)%s%s\n", in, kindName(k), kindName(kind),
                r.block ? "  motivo: " : "", r.block ? flexBrBlockReason(r.block) : "");
    return;
  }
  g_run++;
  if(url){
    g_run++;
    if(std::strcmp(r.url, url) != 0){
      g_fail++;
      std::printf("  FALLO  \"%s\" -> url \"%s\"\n         esperado \"%s\"\n", in, r.url, url);
    }
  }
}

static void expectBlock(const BrSettings& st, const char* in, int reason){
  BrResolved r;
  int k = flexBrResolveInput(in, &st, &r);
  g_run++;
  if(k != BRI_BLOCKED || r.block != reason){
    g_fail++;
    std::printf("  FALLO  \"%s\" -> %s/%d (esperado BLOQUEADO/%d)\n", in, kindName(k), r.block, reason);
  }
}

static void testOmnibox(){
  std::printf("[omnibox] resolucion de entradas\n");
  BrSettings st; flexBrSettingsDefaults(&st);

  // --- 1) vacio ---
  expectKind(st, "", BRI_EMPTY);
  expectKind(st, "    ", BRI_EMPTY);
  expectKind(st, "\t\r\n", BRI_EMPTY);
  { BrResolved r; CHECK(flexBrResolveInput(nullptr, &st, &r) == BRI_EMPTY, "nullptr no dio VACIO"); }

  // --- 2) paginas internas ---
  expectKind(st, "flex://newtab", BRI_INTERNAL, "flex://newtab");
  expectKind(st, "flex://history", BRI_INTERNAL, "flex://history");
  expectKind(st, "flex://bookmarks", BRI_INTERNAL, "flex://bookmarks");
  expectKind(st, "flex://settings", BRI_INTERNAL, "flex://settings");
  expectKind(st, "flex://offline", BRI_INTERNAL, "flex://offline");
  expectKind(st, "flex://about", BRI_INTERNAL, "flex://about");
  expectKind(st, "flex://downloads", BRI_INTERNAL, "flex://downloads");
  expectKind(st, "FLEX://HISTORY", BRI_INTERNAL, "flex://history");
  expectKind(st, "flex://history/", BRI_INTERNAL, "flex://history");
  expectKind(st, "  flex://about  ", BRI_INTERNAL, "flex://about");
  expectKind(st, "flex://", BRI_INTERNAL, "flex://newtab");
  // Una pagina interna que no existe NO se convierte en busqueda: se
  // bloquea, para que "flex://" no sea una via de escape hacia nada.
  expectBlock(st, "flex://noexiste", BRB_SCHEME);
  expectBlock(st, "flex://history/../../etc", BRB_SCHEME);
  expectBlock(st, "flex://settings?x=1", BRB_SCHEME);
  expectBlock(st, "flex:algo", BRB_SCHEME);

  // --- 3) http/https validos ---
  expectKind(st, "https://example.com", BRI_URL, "https://example.com");
  expectKind(st, "http://example.com/a/b?c=d#e", BRI_URL, "http://example.com/a/b?c=d#e");
  expectKind(st, "https://sub.dominio.co.uk:8443/ruta", BRI_URL);
  expectKind(st, "HTTPS://Example.COM/X", BRI_URL, "HTTPS://Example.COM/X");

  // --- 4) otros esquemas: todos bloqueados ---
  const char* badSchemes[] = {
    "file:///etc/passwd", "file://server/share", "javascript:alert(1)",
    "data:text/html,<h1>x", "ftp://ftp.example.com/f", "about:blank",
    "chrome://settings", "view-source:https://example.com", "blob:https://x/y",
    "ws://example.com/s", "wss://example.com/s", "mailto:a@b.com",
    "tel:+34600000000", "intent://x", "content://media/1", "jar:file:///a!/b", 0
  };
  for(int i = 0; badSchemes[i]; i++) expectBlock(st, badSchemes[i], BRB_SCHEME);

  // --- 5) dominio sin esquema -> https ---
  expectKind(st, "example.com", BRI_URL, "https://example.com");
  expectKind(st, "example.com/ruta?q=1", BRI_URL, "https://example.com/ruta?q=1");
  expectKind(st, "www.rtve.es", BRI_URL, "https://www.rtve.es");
  expectKind(st, "sub.dominio.org:8080/x", BRI_URL, "https://sub.dominio.org:8080/x");
  expectKind(st, "  wikipedia.org  ", BRI_URL, "https://wikipedia.org");

  // --- 6) IP y hosts locales: bloqueados por defecto ---
  expectBlock(st, "127.0.0.1", BRB_PRIVATE_IP);
  expectBlock(st, "http://127.0.0.1:8080/", BRB_PRIVATE_IP);
  expectBlock(st, "https://10.0.0.5/admin", BRB_PRIVATE_IP);
  expectBlock(st, "192.168.1.1", BRB_PRIVATE_IP);
  expectBlock(st, "172.16.0.1", BRB_PRIVATE_IP);
  expectBlock(st, "169.254.169.254", BRB_PRIVATE_IP);          // metadatos de nube
  expectBlock(st, "http://169.254.169.254/latest/meta-data/", BRB_PRIVATE_IP);
  expectBlock(st, "http://[::1]:80/", BRB_PRIVATE_IP);
  expectBlock(st, "http://[fd00::1]/", BRB_PRIVATE_IP);
  expectBlock(st, "0.0.0.0", BRB_PRIVATE_IP);
  expectBlock(st, "2130706433", BRB_PRIVATE_IP);               // 127.0.0.1 en decimal
  expectBlock(st, "0x7f000001", BRB_PRIVATE_IP);               // 127.0.0.1 en hexadecimal
  expectBlock(st, "010.0.0.1", BRB_PRIVATE_IP);                // octal
  expectBlock(st, "http://93.184.216.34/", BRB_PRIVATE_IP);    // IP publica: tambien fuera por defecto
  expectBlock(st, "localhost", BRB_LOCALHOST);
  expectBlock(st, "http://localhost:3000/", BRB_LOCALHOST);
  expectBlock(st, "http://impresora.local/", BRB_LOCALHOST);
  expectBlock(st, "http://nas.internal/", BRB_LOCALHOST);
  expectBlock(st, "http://router.lan/", BRB_LOCALHOST);
  expectBlock(st, "http://intranet/", BRB_LOCALHOST);          // una sola etiqueta
  expectBlock(st, "http://servidor/recurso", BRB_LOCALHOST);

  // --- usuario:clave incrustados ---
  expectBlock(st, "https://admin:secreto@banco.com/", BRB_USERINFO);
  expectBlock(st, "http://a@b.example.com/", BRB_USERINFO);

  // --- caracteres de control e inyeccion ---
  expectBlock(st, "https://example.com/\nHost: otro", BRB_BADCHARS);
  expectBlock(st, "https://example.com/\r\nX", BRB_BADCHARS);
  expectBlock(st, "https://example.com/<script>", BRB_BADCHARS);
  expectBlock(st, "https://example.com/a\\b", BRB_BADCHARS);

  // --- longitud ---
  {
    std::string tooLong = "https://example.com/";
    tooLong.append(FLEXBR_INPUT_MAX + 40, 'a');
    expectBlock(st, tooLong.c_str(), BRB_TOOLONG);
    std::string longButOk = "https://example.com/";
    longButOk.append(150, 'b');
    expectKind(st, longButOk.c_str(), BRI_URL);
  }

  // --- 7) busqueda ---
  expectKind(st, "gatos", BRI_SEARCH, "https://duckduckgo.com/?q=gatos");
  expectKind(st, "como hacer pan", BRI_SEARCH, "https://duckduckgo.com/?q=como%20hacer%20pan");
  expectKind(st, "que es el ma\xC3\xB1" "ana", BRI_SEARCH,
             "https://duckduckgo.com/?q=que%20es%20el%20ma%C3%B1ana");
  expectKind(st, "c++ std::vector", BRI_SEARCH,
             "https://duckduckgo.com/?q=c%2B%2B%20std%3A%3Avector");
  expectKind(st, "100% & m\xC3\xA1s", BRI_SEARCH,
             "https://duckduckgo.com/?q=100%25%20%26%20m%C3%A1s");
  // Una sola palabra con punto pero sin TLD valido -> busqueda, no URL.
  expectKind(st, "version 1.2", BRI_SEARCH);
  expectKind(st, "archivo.7z", BRI_SEARCH);        // "7z" no es un TLD alfabetico
  expectKind(st, "3.14159", BRI_SEARCH);
  expectKind(st, "a@b.com", BRI_SEARCH);           // parece un correo: al buscador
  // Dominio con acentos: sin IDNA no se puede construir la URL correcta,
  // asi que se busca en vez de inventar la conversion.
  expectKind(st, "espa\xC3\xB1" "a.es", BRI_SEARCH);

  // --- permiso de desarrollo ---
  {
    BrSettings dev = st; dev.allowLocal = true;
    expectKind(dev, "http://192.168.1.50:8080/panel", BRI_URL, "http://192.168.1.50:8080/panel");
    expectKind(dev, "localhost:3000", BRI_URL, "https://localhost:3000");
    expectKind(dev, "127.0.0.1", BRI_URL, "https://127.0.0.1");
    // Ni con el permiso se acepta otro esquema ni usuario incrustado.
    expectBlock(dev, "file:///etc/passwd", BRB_SCHEME);
    expectBlock(dev, "https://a:b@10.0.0.1/", BRB_USERINFO);
  }

  // --- otros buscadores ---
  {
    BrSettings g = st; g.searchEngine = 1;
    expectKind(g, "hola mundo", BRI_SEARCH, "https://www.google.com/search?q=hola%20mundo");
    BrSettings b = st; b.searchEngine = 2;
    expectKind(b, "hola", BRI_SEARCH, "https://www.bing.com/search?q=hola");
    BrSettings c = st; c.searchEngine = 3;
    std::strcpy(c.searchCustom, "https://buscador.example.org/s?query=%s&lang=es");
    expectKind(c, "una cosa", BRI_SEARCH,
               "https://buscador.example.org/s?query=una%20cosa&lang=es");
    // Plantilla personalizada que apunta a un destino interno: se
    // bloquea igual que cualquier otra URL.
    BrSettings bad = st; bad.searchEngine = 3;
    std::strcpy(bad.searchCustom, "http://192.168.0.1/s?q=%s");
    expectBlock(bad, "algo", BRB_PRIVATE_IP);
    // Plantilla sin %s: no se puede usar.
    BrSettings nop = st; nop.searchEngine = 3;
    std::strcpy(nop.searchCustom, "https://buscador.example.org/s");
    expectBlock(nop, "algo", BRB_BADCHARS);
    // Plantilla con especificadores de printf: NO se interpretan.
    BrSettings pf = st; pf.searchEngine = 3;
    std::strcpy(pf.searchCustom, "https://buscador.example.org/s?q=%s&n=%n%d");
    BrResolved r;
    int k = flexBrResolveInput("x", &pf, &r);
    CHECK(k == BRI_SEARCH && std::strstr(r.url, "%n%d") != nullptr,
          "la plantilla personalizada se paso por printf");
  }

  // --- la salida SIEMPRE cabe ---
  {
    BrResolved r;
    std::string q(FLEXBR_INPUT_MAX - 2, 'z');
    flexBrResolveInput(q.c_str(), &st, &r);
    CHECK(std::strlen(r.url) < FLEXBR_URL_MAX, "la URL resuelta desborda el buffer");
  }
}

// -------------------------------------------------------------
//  2) Utilidades de URL
// -------------------------------------------------------------
static void testUrlHelpers(){
  std::printf("[url] utilidades\n");
  char buf[FLEXBR_URL_MAX];

  CHECK(flexBrPercentEncode("abc-_.~", buf, sizeof(buf)) == 7 && !std::strcmp(buf, "abc-_.~"),
        "los caracteres no reservados no deben codificarse: \"%s\"", buf);
  CHECK(flexBrPercentEncode(" ", buf, sizeof(buf)) == 3 && !std::strcmp(buf, "%20"),
        "el espacio debe ser %%20 y no '+': \"%s\"", buf);
  CHECK(flexBrPercentEncode("/?&=#", buf, sizeof(buf)) > 0 && !std::strcmp(buf, "%2F%3F%26%3D%23"),
        "reservados mal codificados: \"%s\"", buf);
  { char tiny[4];
    CHECK(flexBrPercentEncode("abcdef", tiny, sizeof(tiny)) == -1 && tiny[0] == 0,
          "el desbordamiento debe fallar y dejar la salida vacia"); }

  CHECK(flexBrUrlHost("https://www.example.com:8443/x", buf, sizeof(buf)) &&
        !std::strcmp(buf, "www.example.com"), "host mal extraido: \"%s\"", buf);
  CHECK(!flexBrUrlHost("flex://about", buf, sizeof(buf)), "flex:// no tiene host");

  flexBrUrlLabel("https://www.example.com/a/b", buf, sizeof(buf));
  CHECK(!std::strcmp(buf, "example.com"), "etiqueta \"%s\", esperado \"example.com\"", buf);
  flexBrUrlLabel("flex://history", buf, sizeof(buf));
  CHECK(!std::strcmp(buf, "Historial"), "etiqueta de pagina interna \"%s\"", buf);

  CHECK(flexBrInternalPage("flex://about") == BRP_ABOUT, "flex://about mal reconocida");
  CHECK(flexBrInternalPage("https://example.com") == BRP_NONE, "una URL no es pagina interna");
  CHECK(flexBrInternalPage(nullptr) == BRP_NONE, "nullptr no es pagina interna");
  for(int p = 0; p < BRP_N; p++){
    char u[64]; std::snprintf(u, sizeof(u), "flex://%s", flexBrPageName(p));
    CHECK(flexBrInternalPage(u) == p, "ida y vuelta de la pagina %d", p);
    CHECK(flexBrPageTitle(p)[0] != 0, "la pagina %d no tiene titulo", p);
  }
}

// -------------------------------------------------------------
//  3) Protocolo FBP/1
// -------------------------------------------------------------
static void testProtocol(){
  std::printf("[fbp] codec del protocolo\n");
  uint8_t buf[4096];
  FbpHeader h;

  // --- cabecera ---
  fbpWriteHeader(buf, FBP_C_PING, 0x03, 2, 0x1234, 7);
  CHECK(fbpReadHeader(buf, FBP_HDR_SIZE, &h), "cabecera valida rechazada");
  CHECK(h.type == FBP_C_PING && h.flags == 3 && h.channel == 2 && h.seq == 0x1234 && h.len == 7,
        "cabecera mal leida");
  { uint8_t b2[FBP_HDR_SIZE]; std::memcpy(b2, buf, sizeof(b2)); b2[0] = 'X';
    CHECK(!fbpReadHeader(b2, sizeof(b2), &h), "magia incorrecta aceptada"); }
  { uint8_t b2[FBP_HDR_SIZE]; std::memcpy(b2, buf, sizeof(b2)); b2[2] = 99;
    CHECK(!fbpReadHeader(b2, sizeof(b2), &h), "version incorrecta aceptada"); }
  { uint8_t b2[FBP_HDR_SIZE]; std::memcpy(b2, buf, sizeof(b2));
    b2[8] = 0xFF; b2[9] = 0xFF; b2[10] = 0xFF; b2[11] = 0xFF;
    CHECK(!fbpReadHeader(b2, sizeof(b2), &h), "longitud absurda aceptada"); }
  CHECK(!fbpReadHeader(buf, FBP_HDR_SIZE - 1, &h), "cabecera corta aceptada");

  // --- constructores del cliente ---
  int n = fbpBuildHello(buf, sizeof(buf), 1, "token-secreto", "flexos-abc", 480, 800, 60, BRQ_BALANCED, 0x0F);
  CHECK(n > FBP_HDR_SIZE, "HELLO no se construyo");
  CHECK(fbpReadHeader(buf, (size_t)n, &h) && h.type == FBP_C_HELLO &&
        h.len == (uint32_t)(n - FBP_HDR_SIZE), "cabecera de HELLO incoherente");

  n = fbpBuildNavigate(buf, sizeof(buf), 2, 1, "https://example.com/x");
  CHECK(n == FBP_HDR_SIZE + 2 + 21, "tamano de NAVIGATE: %d", n);
  CHECK(fbpReadHeader(buf, (size_t)n, &h) && h.type == FBP_C_NAVIGATE && h.channel == 1,
        "NAVIGATE mal formado");

  n = fbpBuildPointer(buf, sizeof(buf), 3, 1, FBP_PTR_TAP, 240, 400, 0);
  CHECK(n == FBP_HDR_SIZE + 6, "tamano de POINTER");
  CHECK(buf[FBP_HDR_SIZE] == FBP_PTR_TAP &&
        (buf[FBP_HDR_SIZE + 1] | (buf[FBP_HDR_SIZE + 2] << 8)) == 240 &&
        (buf[FBP_HDR_SIZE + 3] | (buf[FBP_HDR_SIZE + 4] << 8)) == 400, "POINTER mal codificado");

  n = fbpBuildScroll(buf, sizeof(buf), 4, 1, -120, 340);
  CHECK(n == FBP_HDR_SIZE + 4, "tamano de SCROLL");
  CHECK((int16_t)(buf[FBP_HDR_SIZE] | (buf[FBP_HDR_SIZE + 1] << 8)) == -120,
        "SCROLL no conserva el signo");

  n = fbpBuildKey(buf, sizeof(buf), 5, 1, FBP_KEY_TEXT, 0, 0, "\xC3\xB1");
  CHECK(n == FBP_HDR_SIZE + 3 + 2 + 2, "tamano de KEY con UTF-8: %d", n);

  n = fbpBuildViewport(buf, sizeof(buf), 6, 1, 480, 736, 70, BRQ_LOWLAT, 100);
  CHECK(n == FBP_HDR_SIZE + 7, "tamano de VIEWPORT");

  n = fbpBuildAck(buf, sizeof(buf), 7, 1000, 2);
  CHECK(n == FBP_HDR_SIZE + 3, "tamano de ACK");

  n = fbpBuildMedia(buf, sizeof(buf), 8, 1, FBP_MED_SEEK, 45000);
  CHECK(n == FBP_HDR_SIZE + 5, "tamano de MEDIA");

  // Buffer insuficiente: se rechaza, nunca se escribe de mas.
  { uint8_t tiny[8];
    CHECK(fbpBuildNavigate(tiny, sizeof(tiny), 1, 0, "https://example.com") == -1,
          "NAVIGATE escribio en un buffer pequeno");
    CHECK(fbpBuildHello(tiny, sizeof(tiny), 1, "t", "d", 1, 1, 1, 1, 0) == -1,
          "HELLO escribio en un buffer pequeno");
    CHECK(fbpBuildPointer(tiny, sizeof(tiny), 1, 0, 0, 0, 0, 0) == -1,
          "POINTER escribio en un buffer pequeno"); }
  // Una URL mas larga que el buffer no se trunca: falla.
  { std::string huge(3000, 'u');
    std::string url = "https://example.com/" + huge;
    uint8_t small[512];
    CHECK(fbpBuildNavigate(small, sizeof(small), 1, 0, url.c_str()) == -1,
          "NAVIGATE trunco una URL larga"); }

  // --- lectores del servidor ---
  {
    uint8_t p[64]; uint32_t o = 0;
    p[o++] = FBP_VERSION;
    p[o++] = 0xE0; p[o++] = 0x01;              // 480
    p[o++] = 0x20; p[o++] = 0x03;              // 800
    p[o++] = 0x0F; p[o++] = 0; p[o++] = 0; p[o++] = 0;
    p[o++] = 4; p[o++] = 0;
    p[o++] = 0x00; p[o++] = 0x00; p[o++] = 0x03; p[o++] = 0x00;
    const char* sid = "sess-123";
    p[o++] = (uint8_t)std::strlen(sid); p[o++] = 0;
    std::memcpy(p + o, sid, std::strlen(sid)); o += (uint32_t)std::strlen(sid);
    FbpWelcome w;
    CHECK(fbpParseWelcome(p, o, &w), "WELCOME valido rechazado");
    CHECK(w.viewW == 480 && w.viewH == 800 && w.caps == 0x0F && w.maxTabs == 4 &&
          w.maxFrameBytes == 0x30000 && !std::strcmp(w.sessionId, "sess-123"),
          "WELCOME mal leido");
    // Truncado en cada posicion: nunca debe aceptarlo ni leer de mas.
    for(uint32_t cut = 0; cut < o; cut++){
      FbpWelcome bad;
      CHECK(!fbpParseWelcome(p, cut, &bad), "WELCOME truncado a %u aceptado", cut);
    }
    // Version distinta.
    { uint8_t q[64]; std::memcpy(q, p, o); q[0] = 2;
      FbpWelcome bad; CHECK(!fbpParseWelcome(q, o, &bad), "WELCOME de otra version aceptado"); }
    // Viewport imposible.
    { uint8_t q[64]; std::memcpy(q, p, o); q[1] = 0; q[2] = 0;
      FbpWelcome bad; CHECK(!fbpParseWelcome(q, o, &bad), "WELCOME con ancho 0 aceptado"); }
  }
  {
    uint8_t p[256]; uint32_t o = 0;
    p[o++] = FBP_ST_LOADING | FBP_ST_SECURE | FBP_ST_CAN_BACK;
    p[o++] = 55;
    const char* t = "Un t\xC3\xADtulo";
    p[o++] = (uint8_t)std::strlen(t); p[o++] = 0;
    std::memcpy(p + o, t, std::strlen(t)); o += (uint32_t)std::strlen(t);
    const char* u = "https://example.com/";
    p[o++] = (uint8_t)std::strlen(u); p[o++] = 0;
    std::memcpy(p + o, u, std::strlen(u)); o += (uint32_t)std::strlen(u);
    FbpState s;
    CHECK(fbpParseState(p, o, &s), "STATE valido rechazado");
    CHECK(s.progress == 55 && (s.flags & FBP_ST_SECURE) && !std::strcmp(s.url, u),
          "STATE mal leido");
    for(uint32_t cut = 0; cut < o; cut++){
      FbpState bad; CHECK(!fbpParseState(p, cut, &bad), "STATE truncado a %u aceptado", cut);
    }
    // Un titulo con saltos de linea se limpia: una pagina externa no
    // puede partir la barra del navegador con su propio texto.
    { uint8_t q[256]; std::memcpy(q, p, o);
      q[4] = '\n'; q[5] = '\r';
      FbpState s2;
      CHECK(fbpParseState(q, o, &s2), "STATE con controles rechazado");
      for(const char* c = s2.title; *c; c++)
        CHECK((unsigned char)*c >= 0x20, "quedo un caracter de control en el titulo"); }
    // Progreso fuera de rango se acota.
    { uint8_t q[256]; std::memcpy(q, p, o); q[1] = 240;
      FbpState s2; CHECK(fbpParseState(q, o, &s2) && s2.progress == 100,
                         "el progreso no se acoto a 100"); }
  }
  {
    uint8_t p[128]; uint32_t o = 0;
    auto w16 = [&](uint16_t v){ p[o++] = (uint8_t)(v & 0xFF); p[o++] = (uint8_t)(v >> 8); };
    auto w32 = [&](uint32_t v){ p[o++] = (uint8_t)v; p[o++] = (uint8_t)(v >> 8);
                                p[o++] = (uint8_t)(v >> 16); p[o++] = (uint8_t)(v >> 24); };
    w16(0); w16(64); w16(480); w16(32);
    p[o++] = FBP_IMG_JPEG; p[o++] = FBP_FR_KEYFRAME | FBP_FR_LAST;
    w32(77);
    const uint8_t img[10] = { 0xFF, 0xD8, 1, 2, 3, 4, 5, 6, 7, 8 };
    w32(sizeof(img));
    std::memcpy(p + o, img, sizeof(img)); o += (uint32_t)sizeof(img);
    FbpFrame f;
    CHECK(fbpParseFrame(p, o, &f), "FRAME valido rechazado");
    CHECK(f.y == 64 && f.w == 480 && f.h == 32 && f.frameId == 77 &&
          f.imgLen == sizeof(img) && f.img[0] == 0xFF, "FRAME mal leido");
    CHECK((f.flags & FBP_FR_KEYFRAME) != 0, "no se leyo el flag de keyframe");
    // La longitud declarada tiene que cuadrar EXACTA con los bytes que hay:
    // ni de mas (leeria fuera) ni de menos (dejaria basura pegada).
    CHECK(!fbpParseFrame(p, o - 1, &f), "FRAME con un byte de menos aceptado");
    CHECK(!fbpParseFrame(p, o + 1, &f), "FRAME con un byte de mas aceptado");
    { uint8_t q[128]; std::memcpy(q, p, o); q[8] = 99;
      CHECK(!fbpParseFrame(q, o, &f), "FRAME con formato desconocido aceptado"); }
    { uint8_t q[128]; std::memcpy(q, p, o); q[4] = 0; q[5] = 0;
      CHECK(!fbpParseFrame(q, o, &f), "FRAME de ancho 0 aceptado"); }
    { uint8_t q[128]; std::memcpy(q, p, o);
      q[14] = 0; q[15] = 0; q[16] = 0; q[17] = 0;
      CHECK(!fbpParseFrame(q, o, &f), "FRAME sin imagen aceptado"); }
    for(uint32_t cut = 0; cut < o; cut++)
      CHECK(!fbpParseFrame(p, cut, &f), "FRAME truncado a %u aceptado", cut);
  }
  {
    uint8_t p[256]; uint32_t o = 0;
    p[o++] = 1;                                   // video
    p[o++] = 0xE8; p[o++] = 0x03; p[o++] = 0; p[o++] = 0;   // 1000 ms
    p[o++] = 0x80; p[o++] = 0x02;                 // 640
    p[o++] = 0xE0; p[o++] = 0x01;                 // 480
    const char* mime = "video/mp4";
    p[o++] = (uint8_t)std::strlen(mime); p[o++] = 0;
    std::memcpy(p + o, mime, std::strlen(mime)); o += (uint32_t)std::strlen(mime);
    const char* url = "https://cdn.example.com/v.mp4";
    p[o++] = (uint8_t)std::strlen(url); p[o++] = 0;
    std::memcpy(p + o, url, std::strlen(url)); o += (uint32_t)std::strlen(url);
    p[o++] = 0; p[o++] = 0;                       // titulo vacio
    FbpMedia m;
    CHECK(fbpParseMedia(p, o, &m), "MEDIA valido rechazado");
    CHECK(m.kind == 1 && m.durationMs == 1000 && m.w == 640 && m.h == 480 &&
          !std::strcmp(m.mime, "video/mp4"), "MEDIA mal leido");
    for(uint32_t cut = 0; cut < o; cut++){
      FbpMedia bad; CHECK(!fbpParseMedia(p, cut, &bad), "MEDIA truncado a %u aceptado", cut);
    }
  }
  {
    uint8_t p[64]; uint32_t o = 0;
    p[o++] = FBP_ERR_BLOCKED; p[o++] = 0;
    const char* m = "destino interno";
    p[o++] = (uint8_t)std::strlen(m); p[o++] = 0;
    std::memcpy(p + o, m, std::strlen(m)); o += (uint32_t)std::strlen(m);
    FbpError e;
    CHECK(fbpParseError(p, o, &e), "ERROR valido rechazado");
    CHECK(e.code == FBP_ERR_BLOCKED && !std::strcmp(e.msg, m), "ERROR mal leido");
    for(int c = 0; c <= FBP_ERR_EXPIRED; c++) CHECK(flexBrErrorText(c)[0] != 0, "error %d sin texto", c);
  }

  // Una cadena que dice medir mas de lo que hay: rechazada siempre.
  {
    uint8_t p[32] = { 0, 0, 0xFF, 0x7F };      // flags, progreso, len=0x7FFF
    FbpState s;
    CHECK(!fbpParseState(p, sizeof(p), &s), "cadena con longitud mentirosa aceptada");
  }
  // Una cadena mas larga que el buffer de destino se recorta pero no
  // desborda (los titulos de las paginas reales son largos de verdad).
  {
    std::vector<uint8_t> p;
    p.push_back(0); p.push_back(0);
    std::string big(500, 'T');
    p.push_back((uint8_t)(big.size() & 0xFF)); p.push_back((uint8_t)(big.size() >> 8));
    p.insert(p.end(), big.begin(), big.end());
    p.push_back(0); p.push_back(0);
    FbpState s;
    CHECK(fbpParseState(p.data(), (uint32_t)p.size(), &s), "titulo largo rechazado");
    CHECK(std::strlen(s.title) == FLEXBR_TITLE_MAX - 1, "titulo largo mal recortado: %zu",
          std::strlen(s.title));
  }
}

// -------------------------------------------------------------
//  4) Apreton de manos WebSocket
// -------------------------------------------------------------
static void testWebsocket(){
  std::printf("[ws] apreton de manos\n");
  // Vectores de SHA-1 del FIPS 180-1 / RFC 3174.
  struct { const char* in; const char* hex; } sv[] = {
    { "", "da39a3ee5e6b4b0d3255bfef95601890afd80709" },
    { "abc", "a9993e364706816aba3e25717850c26c9cd0d89d" },
    { "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
      "84983e441c3bd26ebaae4aa1f95129e5e54670f1" },
    { 0, 0 }
  };
  for(int i = 0; sv[i].in; i++){
    uint8_t d[20]; char hex[41];
    flexBrSha1((const uint8_t*)sv[i].in, std::strlen(sv[i].in), d);
    for(int k = 0; k < 20; k++) std::sprintf(hex + k * 2, "%02x", d[k]);
    CHECK(!std::strcmp(hex, sv[i].hex), "SHA-1(\"%.20s\") = %s", sv[i].in, hex);
  }
  // Un millon de 'a': comprueba el relleno multibloque.
  {
    std::string big(1000000, 'a');
    uint8_t d[20]; char hex[41];
    flexBrSha1((const uint8_t*)big.data(), big.size(), d);
    for(int k = 0; k < 20; k++) std::sprintf(hex + k * 2, "%02x", d[k]);
    CHECK(!std::strcmp(hex, "34aa973cd4c4daa4f61eeb2bdbad27316534016f"), "SHA-1(1e6 x 'a') = %s", hex);
  }
  // Base64.
  { char b[32];
    CHECK(flexBrBase64((const uint8_t*)"", 0, b, sizeof(b)) == 0 && b[0] == 0, "base64 vacio");
    CHECK(flexBrBase64((const uint8_t*)"f", 1, b, sizeof(b)) == 4 && !std::strcmp(b, "Zg=="), "base64 \"f\" = %s", b);
    CHECK(flexBrBase64((const uint8_t*)"fo", 2, b, sizeof(b)) == 4 && !std::strcmp(b, "Zm8="), "base64 \"fo\" = %s", b);
    CHECK(flexBrBase64((const uint8_t*)"foobar", 6, b, sizeof(b)) == 8 && !std::strcmp(b, "Zm9vYmFy"), "base64 \"foobar\" = %s", b);
    char tiny[4];
    CHECK(flexBrBase64((const uint8_t*)"foobar", 6, tiny, sizeof(tiny)) == -1, "base64 desbordo"); }
  // Vector del propio RFC 6455 (seccion 1.3).
  { char acc[40];
    CHECK(flexBrWsAccept("dGhlIHNhbXBsZSBub25jZQ==", acc, sizeof(acc)) &&
          !std::strcmp(acc, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo="),
          "Sec-WebSocket-Accept = %s", acc); }

  // Troceo de la URL del servicio.
  {
    bool tls; char host[128]; uint16_t port; char path[128];
    CHECK(flexBrParseWsUrl("wss://nav.example.com/v1/session", &tls, host, sizeof(host), &port, path, sizeof(path)) &&
          tls && !std::strcmp(host, "nav.example.com") && port == 443 && !std::strcmp(path, "/v1/session"),
          "wss por defecto mal troceada");
    CHECK(flexBrParseWsUrl("ws://192.168.1.10:8080/s", &tls, host, sizeof(host), &port, path, sizeof(path)) &&
          !tls && port == 8080 && !std::strcmp(host, "192.168.1.10") && !std::strcmp(path, "/s"),
          "ws con puerto mal troceada");
    CHECK(flexBrParseWsUrl("wss://nav.example.com", &tls, host, sizeof(host), &port, path, sizeof(path)) &&
          !std::strcmp(path, "/"), "ruta vacia debe ser /");
    CHECK(!flexBrParseWsUrl("https://nav.example.com/", &tls, host, sizeof(host), &port, path, sizeof(path)),
          "https no es una URL de WebSocket");
    CHECK(!flexBrParseWsUrl("wss://user:clave@nav.example.com/", &tls, host, sizeof(host), &port, path, sizeof(path)),
          "credenciales en la URL: deben rechazarse");
    CHECK(!flexBrParseWsUrl("wss://nav.example.com:0/", &tls, host, sizeof(host), &port, path, sizeof(path)),
          "puerto 0 aceptado");
    CHECK(!flexBrParseWsUrl("wss://nav.example.com:99999/", &tls, host, sizeof(host), &port, path, sizeof(path)),
          "puerto fuera de rango aceptado");
    CHECK(!flexBrParseWsUrl("", &tls, host, sizeof(host), &port, path, sizeof(path)), "URL vacia aceptada");
  }
}

// -------------------------------------------------------------
//  5) Ajustes por defecto
// -------------------------------------------------------------
static void testDefaults(){
  std::printf("[ajustes] valores por defecto\n");
  BrSettings st;
  flexBrSettingsDefaults(&st);
  CHECK(!st.allowLocal, "allowLocal debe venir apagado");
  CHECK(!st.allowInsecure, "allowInsecure debe venir apagado");
  CHECK(!st.telemetry, "la telemetria debe venir apagada");
  CHECK(st.server[0] == 0 && st.token[0] == 0, "no debe haber servidor ni credencial de fabrica");
  CHECK(st.quality >= 20 && st.quality <= 90, "calidad por defecto fuera de rango");
  CHECK(st.scalePct >= 50 && st.scalePct <= 100, "escala por defecto fuera de rango");
  CHECK(st.profile < BRQ_N, "perfil por defecto invalido");
}

int main(){
  std::printf("=== FlexOS · nucleo del navegador ===\n");
  testOmnibox();
  testUrlHelpers();
  testProtocol();
  testWebsocket();
  testDefaults();
  std::printf("=== %d comprobaciones, %d fallos ===\n", g_run, g_fail);
  return g_fail ? 1 : 0;
}
