// #############################################################
//  test_flexlink_vectors.cpp  ·  VECTORES DORADOS de Flex Link
// #############################################################
//
//  POR QUE EXISTE ESTE FICHERO
//  ---------------------------
//  El firmware (C++) y la app Android (Kotlin) implementan el MISMO
//  protocolo por separado. Si alguien mueve un campo, cambia un
//  desplazamiento o toca el CRC en UN solo lado, no salta ningun
//  error de compilacion en ninguno de los dos: simplemente el P4
//  empieza a descartar tramas por CRC y el usuario ve "el telefono
//  no conecta", sin ninguna pista de por que.
//
//  Aqui se fijan los bytes EXACTOS de varios mensajes. El mismo
//  vector, byte a byte, esta en:
//      android/FlexPhone/protocol/src/test/kotlin/
//          com/flexos/flexphone/protocol/FlexLinkTest.kt
//
//  Si los dos lados dejan de coincidir, una de las dos baterias
//  falla ANTES de que nadie flashee nada.

#include "../../FlexOS_Ultra/FlexOS_FlexLink.h"
#include "../../FlexOS_Ultra/FlexOS_FlexPhone.h"
#include "../../FlexOS_Ultra/FlexOS_FlexAuth.h"
#include <cstdio>
#include <cstring>

static int g_fail = 0, g_run = 0;

// Compara `n` bytes contra una cadena hexadecimal en mayusculas.
static void vec(const char* name, const uint8_t* got, size_t n, const char* wantHex){
  g_run++;
  char hex[512];
  size_t at = 0;
  for(size_t i = 0; i < n && at + 2 < sizeof(hex); i++){
    static const char* D = "0123456789ABCDEF";
    hex[at++] = D[(got[i] >> 4) & 0xF];
    hex[at++] = D[got[i] & 0xF];
  }
  hex[at] = 0;
  if(std::strcmp(hex, wantHex) != 0){
    g_fail++;
    std::printf("  FALLO  %s\n         obtenido %s\n         esperado %s\n", name, hex, wantHex);
  } else {
    std::printf("   %-26s %s\n", name, hex);
  }
}

int main(){
  std::printf("\n=== FlexOS · vectores dorados de Flex Link (C++ <-> Kotlin) ===\n");

  uint8_t f[FLNK_MAX_FRAME];
  FlexLinkHeader h; std::memset(&h, 0, sizeof(h));
  h.version = FLNK_VERSION;
  h.session = 0x1234; h.packet = 7; h.frag = 0; h.fragCount = 1; h.counter = 1;

  // -- 1) PING sin carga: solo cabecera --
  h.type = FLNK_T_PING;
  int n = flexLinkWriteFrame(f, sizeof(f), &h, NULL, 0);
  vec("PING vacio", f, (size_t)n, "F15802033412070000010000010000004D2A");

  // -- 2) Trama con carga ASCII --
  h.type = FLNK_T_NOTIF_ADD;
  const char* body = "hola flex";
  n = flexLinkWriteFrame(f, sizeof(f), &h, (const uint8_t*)body, std::strlen(body));
  vec("NOTIF_ADD 'hola flex'", f, (size_t)n,
      "F15802203412070000010900010000007050686F6C6120666C6578");

  // -- 3) CRC16-CCITT: vectores clasicos --
  g_run++;
  if(flexLinkCrc16((const uint8_t*)"123456789", 9) != 0x29B1){
    g_fail++; std::printf("  FALLO  crc16(\"123456789\") != 0x29B1\n");
  } else std::printf("   %-26s 29B1\n", "crc16(\"123456789\")");
  g_run++;
  if(flexLinkCrc16((const uint8_t*)"", 0) != 0xFFFF){
    g_fail++; std::printf("  FALLO  crc16(vacio) != 0xFFFF\n");
  } else std::printf("   %-26s FFFF\n", "crc16(vacio)");

  // -- 4) Notificacion completa --
  FlexPhoneNotif nt; std::memset(&nt, 0, sizeof(nt));
  nt.id = 42;
  std::snprintf(nt.pkg,   sizeof(nt.pkg),   "com.whatsapp");
  std::snprintf(nt.app,   sizeof(nt.app),   "WhatsApp");
  std::snprintf(nt.title, sizeof(nt.title), "Ana");
  std::snprintf(nt.text,  sizeof(nt.text),  "un mensaje");
  nt.whenMs = 123456; nt.cat = FLP_CAT_MSG; nt.pri = FLP_PRI_HIGH;
  nt.canReply = true; nt.replyAction = 1; nt.actionCount = 2;
  std::snprintf(nt.actions[0], sizeof(nt.actions[0]), "Marcar leido");
  std::snprintf(nt.actions[1], sizeof(nt.actions[1]), "Responder");
  uint8_t nb[512];
  int k = flexPhoneEncNotif(nb, sizeof(nb), &nt);
  vec("notificacion", nb, (size_t)k,
      "2A0000000C636F6D2E776861747361707008576861747341707003416E610A756E206D656E7361"
      "6A6540E2010001030401020C4D6172636172206C6569646F09526573706F6E646572");

  // -- 5) La notificacion del vector vuelve a entrar entera --
  //    (asi el vector no solo fija bytes: comprueba que son LEIBLES)
  g_run++;
  FlexPhoneNotif back;
  if(!flexPhoneDecNotif(nb, (size_t)k, &back) ||
     back.id != 42 || !back.canReply || back.replyAction != 1 ||
     back.actionCount != 2 || std::strcmp(back.title, "Ana") != 0){
    g_fail++; std::printf("  FALLO  el vector de notificacion no vuelve a decodificarse\n");
  } else std::printf("   %-26s ok\n", "vector -> decodifica");

  // -- 6) Capacidades --
  //    Es el mensaje que decide QUE ensena Flex OS. Si los dos lados
  //    lo leyeran distinto, el reloj podria ofrecer funciones que el
  //    telefono no tiene, que es justo lo que este mapa evita.
  FlexPhoneCaps cp; std::memset(&cp, 0, sizeof(cp));
  cp.supported = FLP_CAP_NOTIF | FLP_CAP_REPLY | FLP_CAP_MEDIA |
                 FLP_CAP_RELAY | FLP_CAP_STATE | FLP_CAP_TIME;
  cp.granted   = FLP_CAP_NOTIF | FLP_CAP_STATE | FLP_CAP_TIME;
  cp.protoVer  = FLNK_VERSION;
  std::snprintf(cp.model,  sizeof(cp.model),  "SM-A556B");
  std::snprintf(cp.vendor, sizeof(cp.vendor), "samsung");
  std::snprintf(cp.osver,  sizeof(cp.osver),  "Android 14");
  uint8_t cb[256];
  int ck = flexPhoneEncCaps(cb, sizeof(cb), &cp);
  vec("capacidades", cb, (size_t)ck,
      "6F0061000208534D2D41353536420773616D73756E670A416E64726F6964203134");

  // -- 7) Material del vinculo --
  //    El mismo vector esta en FlexLinkTest.kt. Si la derivacion se
  //    toca en un solo lado, el telefono y el reloj derivan claves
  //    distintas y el emparejamiento falla sin explicacion posible.
  uint8_t salt[FLXA_SALT_SIZE];
  for(size_t i = 0; i < sizeof(salt); i++) salt[i] = (uint8_t)i;
  uint8_t nonce[FLXA_NONCE_SIZE];
  for(size_t i = 0; i < sizeof(nonce); i++) nonce[i] = (uint8_t)(0xA0 + i);
  uint8_t key[FLXA_KEY_SIZE];
  flexAuthDeriveKey("012345", salt, "flexos-1", "phone-1", key);
  vec("clave del vinculo", key, sizeof(key),
      "66840F7C66ABABB9F042D87C18E1F6B28D1BF08DB58316823BB99EDB38954176");
  uint8_t pf[FLXA_PROOF_SIZE];
  flexAuthProof(key, FLXA_ROLE_PHONE, nonce, 0x1234, pf);
  vec("prueba del telefono", pf, sizeof(pf),
      "A259AF02D131F300FD204569B46A9D16EA7754C2D88E62768566CE72F169786A");
  flexAuthProof(key, FLXA_ROLE_HOST, nonce, 0x1234, pf);
  vec("prueba de Flex OS", pf, sizeof(pf),
      "DF2848A174C2210E9AE82E2BB5B0888AA50FBF5CC32476612844AD3359385377");

  std::printf("=== %d vectores, %d fallos ===\n", g_run, g_fail);
  return g_fail ? 1 : 0;
}
