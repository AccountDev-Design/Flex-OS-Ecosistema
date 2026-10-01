// #############################################################
//  FLEX ACCOUNT / FLEX COMMUNITY · PERSISTENCIA DEL VINCULO
//  ------------------------------------------------------------
//  Compila y EJECUTA el FlexOS_Account.cpp REAL contra netstub/: una NVS
//  con la semantica exacta de arduino-esp32 3.2.1 y un servidor simulado
//  que firma sus respuestas con una clave P-256 efimera.
//
//  EXISTE POR UN FALLO REAL: "vinculo la cuenta, apago el P4, lo enciendo y
//  aparece desvinculada". La cuenta se guardaba bien; al arrancar se leia con
//  Preferences::getString(clave, char*, cap), que devuelve la longitud CON el
//  terminador (44 para el token de 43), y el modulo exigia 43. Este modulo no
//  lo compilaba ninguna prueba, y el doble de NVS de las demas no tenia esa
//  sobrecarga: nada podia verlo.
//
//  Ademas fija que "sin red" no es "desvinculado": arrancar sin Wi-Fi, un
//  servidor caido, un 401 de una pagina de error del alojamiento o un
//  certificado que no valida dejan la cuenta VINCULADA.
// #############################################################
#include "netstub.h"
#include "FlexOS_Account.h"
#include "pkgbuild.h"
#include <cJSON.h>
#include <string>

static int gChecks = 0, gFails = 0;
#define CHECK(cond, msg) do { gChecks++; if(!(cond)){ gFails++; printf("  FALLO: %s  (%s:%d)\n", msg, __FILE__, __LINE__); } } while(0)

// ------------------------------ servidor simulado ------------------------------
static pkgb::Key gKey;
static const char* KEY_ID = "69b91cf7a9246cc2084dda73ccef77b2dc1a372b18fec19b49cf980efd68af69";
static const char* CODE_URL = "https://flex-developer-studio.ralvarezsantos980.chatgpt.site/api/devices/code";
static const char* SESSION_URL = "https://flex-developer-studio.ralvarezsantos980.chatgpt.site/api/cloud/me";

enum SessionMode { SM_OK, SM_DOWN, SM_500, SM_EXPIRED, SM_REVOKED, SM_HTML401, SM_BADBODY, SM_TLSFAIL };
struct Server {
  std::string registeredHash;      // SHA-256 de la credencial aprobada
  std::string pendingHash;
  std::string code = "FLX7Q2";
  int pendingPolls = 2;            // respuestas "pending" antes de aprobar
  bool expireCode = false;
  SessionMode session = SM_OK;
  std::string address = "ana.p4@flex";
  std::string display = "Ana \xC3\x91" "u\xC3\xB1" "ez";
  int sessionCalls = 0, codeCalls = 0;
} S;

static std::string b64url(const pkgb::Bytes& b){
  static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  std::string out; uint32_t acc = 0; int bits = 0;
  for(uint8_t c : b){ acc = (acc << 8) | c; bits += 8; while(bits >= 6){ bits -= 6; out += t[(acc >> bits) & 63]; } }
  if(bits) out += t[(acc << (6 - bits)) & 63];
  return out;
}
static std::string envelope(const std::string& payload){
  pkgb::Bytes p(payload.begin(), payload.end());
  pkgb::Bytes sig = gKey.sign(pkgb::sha256(p));
  return std::string("{\"schema\":2,\"algorithm\":\"ES256\",\"keyId\":\"") + KEY_ID + "\",\"payload\":\"" + b64url(p) +
         "\",\"signature\":\"" + b64url(sig) + "\"}";
}
static std::string sha256hex(const std::string& s){ return pkgb::hex(pkgb::sha256((const uint8_t*)s.data(), s.size())); }
static std::string queryParam(const std::string& url, const char* k){
  std::string key = std::string(k) + "=";
  size_t p = url.find(key); if(p == std::string::npos) return "";
  size_t e = url.find('&', p); return url.substr(p + key.size(), e == std::string::npos ? std::string::npos : e - p - key.size());
}

static NetResponse serve(const NetRequest& rq){
  NetResponse rs;
  if(!rq.url.compare(0, strlen(CODE_URL), CODE_URL)){
    S.codeCalls++;
    if(rq.method == "POST"){
      cJSON* j = cJSON_Parse(rq.body.c_str());
      cJSON* h = cJSON_GetObjectItem(j, "tokenHash");
      S.pendingHash = cJSON_IsString(h) ? h->valuestring : "";
      cJSON_Delete(j);
      rs.status = 201;
      rs.body = envelope("{\"schema\":1,\"purpose\":\"flex-device-code\",\"code\":\"" + S.code + "\",\"tokenHash\":\"" + S.pendingHash +
                         "\",\"activationUrl\":\"https://flex-developer-studio.ralvarezsantos980.chatgpt.site/activate?code=" + S.code + "\"}");
      return rs;
    }
    if(queryParam(rq.url, "tokenHash") != S.pendingHash){ rs.status = 404; return rs; }
    std::string st = "pending";
    if(S.expireCode) st = "expired";
    else if(S.pendingPolls > 0) S.pendingPolls--;
    else { st = "approved"; S.registeredHash = S.pendingHash; }
    std::string extra = st == "approved" ? ",\"flexAddress\":\"" + S.address + "\",\"displayName\":\"" + S.display + "\"" : "";
    rs.body = envelope("{\"schema\":1,\"purpose\":\"flex-device-status\",\"state\":\"" + st + "\"" + extra + "}");
    return rs;
  }
  if(rq.url == SESSION_URL){
    S.sessionCalls++;
    switch(S.session){
      case SM_DOWN:     rs.status = HTTPC_ERROR_CONNECTION_REFUSED; return rs;
      case SM_TLSFAIL:  rs.status = HTTPC_ERROR_CONNECTION_REFUSED; return rs;
      case SM_500:      rs.status = 503; rs.body = "<html>bad gateway</html>"; return rs;
      case SM_HTML401:  rs.status = 401; rs.body = "<html>Access denied</html>"; return rs;
      case SM_EXPIRED:  rs.status = 401; rs.body = "{\"ok\":false,\"error\":{\"code\":\"token_expired\",\"message\":\"x\"}}"; return rs;
      case SM_REVOKED:  rs.status = 401; rs.body = "{\"ok\":false,\"error\":{\"code\":\"device_revoked\",\"message\":\"x\"}}"; return rs;
      case SM_BADBODY:  rs.status = 200; rs.body = "{\"ok\":true}"; return rs;
      default: break;
    }
    auto it = rq.headers.find("authorization");
    std::string bearer = (it != rq.headers.end() && it->second.size() > 7) ? it->second.substr(7) : "";
    if(sha256hex(bearer) != S.registeredHash){
      rs.status = 401; rs.body = "{\"ok\":false,\"error\":{\"code\":\"device_revoked\",\"message\":\"x\"}}"; return rs;
    }
    rs.body = "{\"ok\":true,\"account\":{\"id\":\"acc_1\",\"flexAddress\":\"" + S.address + "\",\"displayName\":\"" + S.display + "\"}}";
    return rs;
  }
  rs.status = 404;
  return rs;
}

// ------------------------------ utilidades ------------------------------
static FlexAccountSnapshot snap(){ FlexAccountSnapshot s; flexAccountSnapshot(&s); return s; }
static void steps(int n, unsigned long msEach = 100){ for(int i = 0; i < n; i++){ flexAccountTestStep(); netstubAdvance(msEach); } }
static int countReq(const char* url){ int n = 0; for(auto& r : gNetLog) if(r.url.rfind(url, 0) == 0) n++; return n; }
static bool bearerLeaked(){
  // La credencial solo puede viajar en la cabecera Authorization hacia el
  // servicio de sesion, y nunca por un canal sin certificado validado.
  for(auto& r : gNetLog){
    auto it = r.headers.find("authorization");
    if(it != r.headers.end()){
      if(r.tlsInsecure || !r.tlsCa || !r.https) return true;
      if(r.url != SESSION_URL) return true;
    }
  }
  return false;
}
static void linkNow(){
  S.pendingPolls = 2; S.expireCode = false;
  CHECK(flexAccountRequestCode("FlexOS Ultra de Ana"), "se acepta la peticion de enlace");
  flexAccountTestStep();                         // ejecuta el flujo entero (sondea con vTaskDelay)
}

// ------------------------------ pruebas ------------------------------
static void testNvsSemantics(){
  printf("-- NVS: la semantica que escondia el fallo --\n");
  netstubNvsWipe();
  Preferences p; p.begin("nvstest", false);
  const char* tok = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQ";   // 43
  CHECK(p.putString("token", tok) == 43, "putString devuelve strlen (43)");
  char buf[48]; memset(buf, 0x7F, sizeof(buf));
  size_t got = p.getString("token", buf, sizeof(buf));
  CHECK(got == 44, "getString(char*) devuelve la longitud CON terminador (44)");
  // Asi decidia el arranque ANTES del arreglo: siempre falso.
  bool oldCheck = (got == 43);
  CHECK(!oldCheck, "la comprobacion antigua (== 43) rechazaba SIEMPRE la cuenta guardada");
  CHECK(flexNvsStrLen(got, buf, sizeof(buf)) == 43, "flexNvsStrLen recupera la longitud util");
  char small[10];
  CHECK(p.getString("token", small, sizeof(small)) == 0, "sin sitio: 0 y el buffer no es fiable");
  CHECK(flexNvsStrLen(0, small, sizeof(small)) == 0, "flexNvsStrLen(0) = 0");
  char nonul[4] = {'a','b','c','d'};
  CHECK(flexNvsStrLen(5, nonul, sizeof(nonul)) == 0, "sin terminador no se acepta");
  CHECK(flexNvsStrLen(9, "abc", 4) == 0, "longitud incoherente no se acepta");
  CHECK(flexNvsStrLen(3, "abc", 4) == 3, "si el core devolviera strlen tambien vale");
  p.end();
}

static void testPureCore(){
  printf("-- nucleo: clasificacion y espera --\n");
  CHECK(flexAccountClassify(200, nullptr) == FLEX_SESSION_OK, "200 = ok");
  CHECK(flexAccountClassify(401, "token_expired") == FLEX_SESSION_TOKEN_EXPIRED, "401 token_expired");
  CHECK(flexAccountClassify(401, "device_revoked") == FLEX_SESSION_AUTH_REQUIRED, "401 device_revoked");
  CHECK(flexAccountClassify(403, "auth_required") == FLEX_SESSION_AUTH_REQUIRED, "403 auth_required");
  CHECK(flexAccountClassify(401, nullptr) == FLEX_SESSION_UNAVAILABLE, "401 sin codigo (pagina de un proxy) NO desvincula");
  CHECK(flexAccountClassify(401, "") == FLEX_SESSION_UNAVAILABLE, "401 con codigo vacio NO desvincula");
  CHECK(flexAccountClassify(404, nullptr) == FLEX_SESSION_UNAVAILABLE, "404 = servicio no disponible");
  CHECK(flexAccountClassify(503, nullptr) == FLEX_SESSION_UNAVAILABLE, "503 = servicio no disponible");
  CHECK(flexAccountClassify(-1, nullptr) == FLEX_SESSION_UNAVAILABLE, "fallo de transporte = no disponible");
  CHECK(flexAccountBackoffMs(1) == 30000u, "primer reintento a 30 s");
  CHECK(flexAccountBackoffMs(2) == 60000u, "segundo a 60 s");
  CHECK(flexAccountBackoffMs(5) == 480000u, "quinto a 8 min");
  CHECK(flexAccountBackoffMs(6) == 900000u && flexAccountBackoffMs(200) == 900000u, "tope de 15 min");
}

static void testFreshBoot(){
  printf("-- arranque de fabrica --\n");
  netstubNvsWipe(); netstubReset(); gNetHandler = serve;
  flexAccountTestPowerCycle();
  FlexAccountSnapshot s = snap();
  CHECK(s.state == FLEX_ACCOUNT_UNLINKED && !s.linked, "sin cuenta: no vinculada");
  CHECK(s.link == FLEX_LINK_UNLINKED, "estado del vinculo: UNLINKED");
  gNetWifi = true; steps(100);
  CHECK(gNetLog.empty(), "sin cuenta no se habla con nadie");
}

static void testLinkAndPowerCycle(){
  printf("-- vincular, apagar y encender (el fallo reportado) --\n");
  netstubNvsWipe(); netstubReset(); gNetHandler = serve; S = Server();
  flexAccountTestPowerCycle();
  gNetWifi = true;
  linkNow();
  FlexAccountSnapshot s = snap();
  CHECK(s.state == FLEX_ACCOUNT_LINKED && s.linked, "vinculada tras aprobar");
  CHECK(s.link == FLEX_LINK_LINKED, "recien aprobada cuenta como validada");
  CHECK(!strcmp(s.flexAddress, "ana.p4@flex"), "direccion guardada");
  CHECK(!strcmp(s.displayName, S.display.c_str()), "nombre con tildes y enie intacto");
  char bearer[64];
  CHECK(flexAccountCopyBearer(bearer, sizeof(bearer)) && strlen(bearer) == 43, "credencial disponible (43)");
  CHECK(sha256hex(bearer) == S.registeredHash, "el servidor solo conoce la huella de la credencial");
  for(auto& r : gNetLog) CHECK(r.body.find(bearer) == std::string::npos && r.url.find(bearer) == std::string::npos,
                               "la credencial no viaja durante el enlace");

  // APAGAR Y ENCENDER, SIN RED.
  gNetWifi = false; gNetLog.clear();
  unsigned writesBefore = netstubNvsWriteCount();
  flexAccountTestPowerCycle();
  s = snap();
  CHECK(s.linked && s.state == FLEX_ACCOUNT_LINKED, "TRAS APAGAR Y ENCENDER SIGUE VINCULADA");
  CHECK(s.link == FLEX_LINK_LINKED_OFFLINE, "sin Wi-Fi: LINKED_OFFLINE, no UNLINKED");
  CHECK(!strcmp(s.flexAddress, "ana.p4@flex"), "la direccion vuelve de la NVS");
  CHECK(flexAccountLinked() && flexAccountUsable(), "linked() y usable() verdaderos sin red");
  char again[64];
  CHECK(flexAccountCopyBearer(again, sizeof(again)) && !strcmp(again, bearer), "la misma credencial tras reiniciar");
  steps(600);                                     // un minuto sin Wi-Fi
  s = snap();
  CHECK(s.link == FLEX_LINK_LINKED_OFFLINE && s.linked, "un minuto sin Wi-Fi: sigue vinculada");
  CHECK(gNetLog.empty(), "sin Wi-Fi no se intenta la red");
  CHECK(netstubNvsWriteCount() == writesBefore, "arrancar no escribe en la flash");

  // VUELVE EL WI-FI: valida una vez, con TLS verificado.
  gNetWifi = true;
  steps(10);
  CHECK(countReq(SESSION_URL) == 0, "espera a que la red se asiente antes de validar");
  steps(40);
  s = snap();
  CHECK(countReq(SESSION_URL) == 1, "una sola validacion al volver el Wi-Fi");
  CHECK(s.link == FLEX_LINK_LINKED, "validada: LINKED");
  CHECK(s.verifiedAgeS != 0xFFFFFFFFu, "consta cuando se valido");
  CHECK(!bearerLeaked(), "la credencial solo viaja con certificado validado y a la sesion");
  CHECK(!gNetLog.empty() && !gNetLog.back().tlsInsecure && gNetLog.back().tlsCa && strstr(gNetLog.back().tlsCa, "BEGIN CERTIFICATE"),
        "la validacion usa las raices de confianza, nunca setInsecure()");
  CHECK(netstubNvsWriteCount() == writesBefore, "validar con exito no escribe en la flash");
  steps(3000);
  CHECK(countReq(SESSION_URL) == 1, "no se revalida en bucle");

  // Varios ciclos de encendido seguidos.
  for(int i = 0; i < 5; i++){
    gNetWifi = (i % 2) == 0;
    flexAccountTestPowerCycle();
    steps(60);
    s = snap();
    CHECK(s.linked && (s.link == FLEX_LINK_LINKED || s.link == FLEX_LINK_LINKED_OFFLINE), "reinicio repetido: sigue vinculada");
  }
}

static void testServerTrouble(){
  printf("-- servidor caido, 401 de un proxy, respuesta rara --\n");
  netstubReset(); gNetHandler = serve;
  flexAccountTestPowerCycle();
  S.session = SM_DOWN;
  gNetWifi = true;
  steps(40);
  FlexAccountSnapshot s = snap();
  CHECK(s.linked && s.link == FLEX_LINK_NETWORK_UNAVAILABLE, "servidor caido: NETWORK_UNAVAILABLE y vinculada");
  CHECK(flexAccountUsable(), "sigue usable (sin conexion no es rechazo)");
  steps(2600);                                    // 260 s
  std::vector<unsigned long> at;
  for(auto& r : gNetLog) if(r.url == SESSION_URL) at.push_back(r.at);
  CHECK(at.size() >= 4, "hay reintentos");
  if(at.size() >= 4){
    unsigned long g1 = at[1] - at[0], g2 = at[2] - at[1], g3 = at[3] - at[2];
    CHECK(g1 >= 30000 && g1 <= 30300, "primer reintento a los 30 s");
    CHECK(g2 >= 60000 && g2 <= 60300, "segundo a los 60 s");
    CHECK(g3 >= 120000 && g3 <= 120300, "tercero a los 2 min");
  }
  // Una hora con el servidor caido: pocos intentos, nunca un bucle.
  int before = countReq(SESSION_URL);
  steps(36000);
  int during = countReq(SESSION_URL) - before;
  CHECK(during >= 3 && during <= 8, "una hora de servidor caido: unos pocos reintentos");
  CHECK(snap().linked, "tras una hora sin servidor sigue vinculada");

  S.session = SM_HTML401;
  flexAccountRequestValidation(); steps(5);
  CHECK(snap().link == FLEX_LINK_NETWORK_UNAVAILABLE && snap().linked, "401 de una pagina de error: no desvincula");
  S.session = SM_BADBODY;
  flexAccountRequestValidation(); steps(5);
  CHECK(snap().link == FLEX_LINK_NETWORK_UNAVAILABLE, "200 sin cuenta en el cuerpo: no cuenta como validada");
  S.session = SM_500;
  flexAccountRequestValidation(); steps(5);
  CHECK(snap().link == FLEX_LINK_NETWORK_UNAVAILABLE && snap().linked, "503: servicio no disponible");
  S.session = SM_OK;
  flexAccountRequestValidation(); steps(5);
  CHECK(snap().link == FLEX_LINK_LINKED, "el servidor vuelve: LINKED");

  // Wi-Fi que se cae y vuelve.
  gNetWifi = false; steps(3);
  CHECK(snap().link == FLEX_LINK_LINKED_OFFLINE, "Wi-Fi caido: LINKED_OFFLINE");
  int k = countReq(SESSION_URL);
  for(int i = 0; i < 20; i++){ gNetWifi = !gNetWifi; steps(20); }   // Wi-Fi intermitente 40 s
  gNetWifi = true; steps(400);
  int flaps = countReq(SESSION_URL) - k;
  CHECK(flaps >= 1 && flaps <= 3, "Wi-Fi intermitente: no valida en cada flanco");
  CHECK(snap().link == FLEX_LINK_LINKED, "estable de nuevo: LINKED");
}

static void testExpiredAndRelink(){
  printf("-- credencial caducada, cancelar y volver a vincular --\n");
  netstubReset(); gNetHandler = serve;
  flexAccountTestPowerCycle();
  S.session = SM_EXPIRED; gNetWifi = true;
  unsigned w0 = netstubNvsWriteCount();
  steps(40);
  FlexAccountSnapshot s = snap();
  CHECK(s.link == FLEX_LINK_TOKEN_EXPIRED, "401 token_expired: TOKEN_EXPIRED");
  CHECK(s.linked && !flexAccountUsable(), "sigue vinculada (hay que renovar), pero no usable");
  CHECK(netstubNvsWriteCount() == w0 + 1, "el cambio de estado se guarda UNA vez");
  steps(600);
  CHECK(netstubNvsWriteCount() == w0 + 1, "y no se reescribe");
  flexAccountTestPowerCycle(); gNetWifi = false; steps(10);
  s = snap();
  CHECK(s.link == FLEX_LINK_TOKEN_EXPIRED && s.linked, "tras reiniciar sin red: sigue TOKEN_EXPIRED, no desvinculada");

  // Volver a vincular y CANCELAR: la cuenta guardada no se toca.
  char oldBearer[64]; flexAccountCopyBearer(oldBearer, sizeof(oldBearer));
  gNetWifi = true;
  S.pendingPolls = 1000;
  CHECK(flexAccountRequestCode("FlexOS"), "se puede pedir un codigo nuevo");
  flexAccountCancel();
  flexAccountTestStep();
  s = snap();
  CHECK(s.linked && s.state == FLEX_ACCOUNT_LINKED, "cancelar el nuevo enlace conserva la cuenta");
  CHECK(s.link == FLEX_LINK_TOKEN_EXPIRED, "y su estado");
  char cur[64]; flexAccountCopyBearer(cur, sizeof(cur));
  CHECK(!strcmp(cur, oldBearer), "la credencial anterior sigue intacta");

  // Codigo que caduca: tampoco toca la cuenta.
  S.expireCode = true;
  CHECK(flexAccountRequestCode("FlexOS"), "otro intento");
  flexAccountTestStep();
  s = snap();
  CHECK(s.linked && s.state == FLEX_ACCOUNT_LINKED && s.error[0], "codigo caducado: cuenta intacta y el motivo a la vista");

  // Volver a vincular de verdad.
  S.session = SM_OK; S.expireCode = false;
  linkNow();
  s = snap();
  char fresh[64]; flexAccountCopyBearer(fresh, sizeof(fresh));
  CHECK(s.link == FLEX_LINK_LINKED && strcmp(fresh, oldBearer), "nueva credencial y LINKED");
  flexAccountTestPowerCycle(); gNetWifi = false; steps(5);
  CHECK(snap().link == FLEX_LINK_LINKED_OFFLINE, "la nueva sesion sobrevive al reinicio (authst limpio)");
}

static void testRevokedForgetRelink(){
  printf("-- revocada, desvincular y volver a vincular --\n");
  netstubReset(); gNetHandler = serve;
  flexAccountTestPowerCycle();
  S.session = SM_REVOKED; gNetWifi = true;
  steps(40);
  CHECK(snap().link == FLEX_LINK_AUTH_REQUIRED && snap().linked, "revocada en la web: AUTH_REQUIRED (no 'desvinculada')");
  S.session = SM_DOWN;
  flexAccountRequestValidation(); steps(5);
  CHECK(snap().link == FLEX_LINK_AUTH_REQUIRED, "un fallo de red no borra el rechazo del servidor");
  S.session = SM_OK;

  flexAccountForgetLocal();
  FlexAccountSnapshot s = snap();
  CHECK(!s.linked && s.link == FLEX_LINK_UNLINKED && s.state == FLEX_ACCOUNT_UNLINKED, "olvidar: UNLINKED");
  char b[64]; CHECK(!flexAccountCopyBearer(b, sizeof(b)), "sin credencial");
  flexAccountTestPowerCycle();
  CHECK(!snap().linked && snap().link == FLEX_LINK_UNLINKED, "desvinculada de verdad tras reiniciar");
  gNetLog.clear(); steps(100);
  CHECK(countReq(SESSION_URL) == 0, "desvinculada: no valida nada");

  linkNow();
  CHECK(snap().link == FLEX_LINK_LINKED, "volver a vincular");
  flexAccountTestPowerCycle(); gNetWifi = true; steps(50);
  CHECK(snap().link == FLEX_LINK_LINKED, "y validada tras reiniciar con Wi-Fi");
}

static void testRejectedByCloud(){
  printf("-- un 401 de Flex Cloud pide revalidar, con freno --\n");
  netstubReset(); gNetHandler = serve; gNetWifi = true;
  flexAccountTestPowerCycle(); steps(50);
  int n0 = countReq(SESSION_URL);
  for(int i = 0; i < 10; i++){ flexAccountReportRejected(); steps(2); }
  CHECK(countReq(SESSION_URL) == n0 + 1, "diez rechazos seguidos: UNA revalidacion");
  steps(610);
  flexAccountReportRejected(); steps(2);
  CHECK(countReq(SESSION_URL) == n0 + 2, "pasado un minuto, otra");
  CHECK(snap().link == FLEX_LINK_LINKED, "con el servidor bien, sigue LINKED");
}

static void testCorruptAndBrokenNvs(){
  printf("-- NVS incompleta o rota: ERROR, nunca 'sin cuenta' en silencio --\n");
  netstubNvsWipe(); netstubReset();
  { Preferences p; p.begin("flexacct", false); p.putBool("linked", true); p.putString("address", "ana@flex"); p.end(); }
  flexAccountTestPowerCycle();
  FlexAccountSnapshot s = snap();
  CHECK(!s.linked && s.link == FLEX_LINK_ERROR, "registro sin token: ERROR con motivo");
  CHECK(strstr(s.linkDetail, "incompleta") != nullptr, "el motivo se dice");
  netstubNvsBroken() = true;
  flexAccountTestPowerCycle();
  CHECK(snap().link == FLEX_LINK_ERROR, "NVS que no abre: ERROR");
  netstubNvsBroken() = false;
}

static void testWearOverTime(){
  printf("-- desgaste de flash y 60 dias encendido (millis() da la vuelta) --\n");
  netstubNvsWipe(); netstubReset(); gNetHandler = serve; S = Server();
  flexAccountTestPowerCycle(); gNetWifi = true; linkNow();
  unsigned w0 = netstubNvsWriteCount();
  int n0 = countReq(SESSION_URL);
  for(int day = 0; day < 30; day++) steps(24 * 60, 60000);   // 30 dias en pasos de 1 min
  int calls = countReq(SESSION_URL) - n0;
  if(!(calls >= 115 && calls <= 121)) printf("  (validaciones en 30 dias: %d)\n", calls);
  CHECK(calls >= 115 && calls <= 121, "revalida cada 6 h (unas 120 veces al mes), tambien pasados los 24,8 dias");
  CHECK(netstubNvsWriteCount() == w0, "y ni una escritura de flash");
  // Pasar la vuelta de millis() (49,7 dias) con el servidor caido a ratos.
  S.session = SM_DOWN;
  for(int day = 0; day < 30; day++) steps(24 * 60, 60000);
  S.session = SM_OK;
  int n1 = countReq(SESSION_URL);
  steps(16 * 60, 60000);                          // como mucho 15 min de espera
  CHECK(countReq(SESSION_URL) > n1 && snap().link == FLEX_LINK_LINKED, "tras la vuelta de millis() se recupera solo");
  CHECK(snap().linked, "60 dias encendido: sigue vinculada");
}

int main(){
  printf("=== FlexOS · Flex Account / Flex Community: persistencia del vinculo ===\n");
  gKey.generate();
  flexAccountTestSetKey(gKey.pub.data());
  netstubReset();
  testNvsSemantics();
  testPureCore();
  testFreshBoot();
  testLinkAndPowerCycle();
  testServerTrouble();
  testExpiredAndRelink();
  testRevokedForgetRelink();
  testRejectedByCloud();
  testCorruptAndBrokenNvs();
  testWearOverTime();
  gKey.free_();
  printf("=== %d comprobaciones, %d fallos ===\n", gChecks, gFails);
  return gFails ? 1 : 0;
}
