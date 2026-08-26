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

#include "../../FlexOS_FlexLink.h"
#include "../../FlexOS_FlexPhone.h"
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
  vec("PING vacio", f, (size_t)n, "F1580103341207000001000001000000EEA7");

  // -- 2) Trama con carga ASCII --
  h.type = FLNK_T_NOTIF_ADD;
  const char* body = "hola flex";
  n = flexLinkWriteFrame(f, sizeof(f), &h, (const uint8_t*)body, std::strlen(body));
  vec("NOTIF_ADD 'hola flex'", f, (size_t)n,
      "F15801203412070000010900010000006DFF686F6C6120666C6578");

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

  std::printf("=== %d vectores, %d fallos ===\n", g_run, g_fail);
  return g_fail ? 1 : 0;
}
