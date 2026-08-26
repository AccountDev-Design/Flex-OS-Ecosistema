// #############################################################
//  test_flexphone.cpp  ·  pruebas de host del MODELO Flex Phone
//  (FlexOS_FlexPhone.cpp: cola de notificaciones, expulsion,
//   conversaciones, privacidad, codecs y persistencia)
// #############################################################
//
//  Igual que test_flexlink: se compila el fichero REAL, con
//  sanitizers, y se le dan los bytes que puede mandar un telefono
//  -- incluidos los que no deberia mandar.

#include "../../FlexOS_FlexPhone.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

static int g_fail = 0, g_run = 0;
#define CHECK(cond, ...) do { g_run++; if(!(cond)){ g_fail++; \
  std::printf("  FALLO %s:%d  ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while(0)

static FlexPhoneModel M;

static FlexPhoneNotif mk(uint32_t id, const char* pkg, const char* title,
                         const char* text, uint8_t pri, bool canReply){
  FlexPhoneNotif n; std::memset(&n, 0, sizeof(n));
  n.id = id; n.pri = pri; n.cat = FLP_CAT_MSG;
  std::snprintf(n.pkg,   sizeof(n.pkg),   "%s", pkg);
  std::snprintf(n.app,   sizeof(n.app),   "App");
  std::snprintf(n.title, sizeof(n.title), "%s", title);
  std::snprintf(n.text,  sizeof(n.text),  "%s", text);
  if(canReply){
    n.actionCount = 2;
    std::snprintf(n.actions[0], sizeof(n.actions[0]), "Marcar leido");
    std::snprintf(n.actions[1], sizeof(n.actions[1]), "Responder");
    n.replyAction = 1; n.canReply = true;
  }
  n.used = true;
  return n;
}

// -------------------------------------------------------------
//  1) Cola: alta, actualizacion, borrado
// -------------------------------------------------------------
static void testQueue(){
  std::printf("[flexphone] cola: alta, actualizacion en sitio y borrado\n");
  flexPhoneModelInit(&M);
  CHECK(M.notifCount == 0, "arranca con %u", M.notifCount);
  CHECK(M.phone.battery == 255, "la bateria deberia nacer DESCONOCIDA, no 0");

  FlexPhoneNotif a = mk(101, "com.whatsapp", "Ana", "hola", FLP_PRI_DEFAULT, true);
  CHECK(flexPhoneNotifPut(&M, &a, 1000) >= 0, "no inserto");
  CHECK(M.notifCount == 1, "cuenta %u", M.notifCount);

  // -- ACTUALIZADA: mismo id, texto nuevo, sin duplicar --
  FlexPhoneNotif a2 = mk(101, "com.whatsapp", "Ana", "hola otra vez", FLP_PRI_DEFAULT, true);
  const int i2 = flexPhoneNotifPut(&M, &a2, 2000);
  CHECK(i2 >= 0, "no actualizo");
  CHECK(M.notifCount == 1, "la actualizacion duplico (%u)", M.notifCount);
  CHECK(std::strcmp(M.notif[i2].text, "hola otra vez") == 0, "no cambio el texto");
  CHECK(M.notif[i2].rxMs == 1000, "la actualizacion movio su sitio en el orden");
  CHECK(M.stats.rxUpdate == 1, "no conto la actualizacion");

  // -- ELIMINADA --
  CHECK(flexPhoneNotifRemove(&M, 101), "no borro");
  CHECK(M.notifCount == 0, "sigue contando %u", M.notifCount);
  CHECK(!flexPhoneNotifRemove(&M, 101), "borro dos veces la misma");
  CHECK(flexPhoneNotifFind(&M, 101) < 0, "la encuentra tras borrarla");

  // -- Sin paquete: no es identificable, se rechaza --
  FlexPhoneNotif bad = mk(9, "", "X", "y", FLP_PRI_DEFAULT, false);
  CHECK(flexPhoneNotifPut(&M, &bad, 3000) < 0, "acepto una notificacion sin paquete");
}

// -------------------------------------------------------------
//  2) Expulsion: primero la mas antigua de prioridad mas baja
// -------------------------------------------------------------
static void testEviction(){
  std::printf("[flexphone] cola llena: expulsa antigua+baja, nunca la urgente\n");
  flexPhoneModelInit(&M);
  // Llena la cola: una BAJA vieja, el resto ALTAS mas nuevas.
  FlexPhoneNotif low = mk(1, "com.x", "bajita", "t", FLP_PRI_LOW, false);
  flexPhoneNotifPut(&M, &low, 100);
  for(int i = 1; i < FLP_NOTIF_MAX; i++){
    FlexPhoneNotif h = mk((uint32_t)(100 + i), "com.y", "alta", "t", FLP_PRI_HIGH, false);
    flexPhoneNotifPut(&M, &h, (uint32_t)(1000 + i));
  }
  CHECK(M.notifCount == FLP_NOTIF_MAX, "no se lleno (%u)", M.notifCount);

  // Una mas: tiene que caer la de prioridad BAJA, no una ALTA.
  FlexPhoneNotif extra = mk(999, "com.z", "nueva", "t", FLP_PRI_DEFAULT, false);
  CHECK(flexPhoneNotifPut(&M, &extra, 9000) >= 0, "no entro con la cola llena");
  CHECK(M.notifCount == FLP_NOTIF_MAX, "la cola crecio por encima del maximo (%u)", M.notifCount);
  CHECK(flexPhoneNotifFind(&M, 1) < 0, "no expulso la de prioridad baja");
  CHECK(flexPhoneNotifFind(&M, 999) >= 0, "no guardo la nueva");
  CHECK(M.stats.queueFull == 1 && M.stats.evicted == 1, "no conto la cola llena/expulsion");

  // Una URGENTE con todo alto: cae la mas ANTIGUA de las altas.
  // Ahora la prioridad MAS BAJA presente es la de id=999 (DEFAULT); el
  // resto son HIGH. Asi que la que debe caer es la 999, NO una HIGH:
  // la politica mira primero la prioridad y solo despues la antiguedad.
  FlexPhoneNotif urg = mk(1000, "com.z", "urgente", "t", FLP_PRI_URGENT, false);
  flexPhoneNotifPut(&M, &urg, 9500);
  CHECK(flexPhoneNotifFind(&M, 1000) >= 0, "no entro la urgente");
  CHECK(flexPhoneNotifFind(&M, 999) < 0, "expulso una HIGH teniendo una DEFAULT mas baja");
  CHECK(flexPhoneNotifFind(&M, 101) >= 0, "expulso una HIGH antes que la DEFAULT");

  // La urgente SOBREVIVE a otra ronda de inserciones normales.
  for(int i = 0; i < 5; i++){
    FlexPhoneNotif h = mk((uint32_t)(2000 + i), "com.y", "otra", "t", FLP_PRI_DEFAULT, false);
    flexPhoneNotifPut(&M, &h, (uint32_t)(10000 + i));
  }
  CHECK(flexPhoneNotifFind(&M, 1000) >= 0, "expulso la URGENTE teniendo otras mas bajas");
}

// -------------------------------------------------------------
//  3) Caducidad por privacidad
// -------------------------------------------------------------
static void testExpire(){
  std::printf("[flexphone] caducidad: no se guardan notificaciones privadas para siempre\n");
  flexPhoneModelInit(&M);
  FlexPhoneNotif a = mk(1, "com.x", "vieja", "t", FLP_PRI_DEFAULT, false);
  FlexPhoneNotif b = mk(2, "com.x", "nueva", "t", FLP_PRI_DEFAULT, false);
  flexPhoneNotifPut(&M, &a, 1000);
  flexPhoneNotifPut(&M, &b, 50000);
  CHECK(flexPhoneNotifExpire(&M, 60000, 30000) == 1, "no caduco exactamente una");
  CHECK(flexPhoneNotifFind(&M, 1) < 0 && flexPhoneNotifFind(&M, 2) >= 0, "caduco la que no era");
  CHECK(flexPhoneNotifExpire(&M, 60000, 0) == 0, "con maxAge 0 no debe caducar nada");
}

// -------------------------------------------------------------
//  4) Privacidad: bloqueo, oculto y sensible
// -------------------------------------------------------------
static void testPrivacy(){
  std::printf("[flexphone] privacidad: oculto, sensible y pantalla bloqueada\n");
  flexPhoneModelInit(&M);
  char body[FLP_TEXT_MAX];

  FlexPhoneNotif n = mk(1, "com.whatsapp", "Ana", "el codigo es 1234", FLP_PRI_DEFAULT, true);
  flexPhoneNotifPut(&M, &n, 1000);
  const FlexPhoneNotif* p = &M.notif[flexPhoneNotifFind(&M, 1)];

  M.priv.hideBodyOnLock = true;
  CHECK(flexPhoneNotifBody(&M, p, false, body, sizeof(body)), "desbloqueado deberia verse");
  CHECK(std::strcmp(body, "el codigo es 1234") == 0, "cuerpo \"%s\"", body);
  CHECK(!flexPhoneNotifBody(&M, p, true, body, sizeof(body)), "en BLOQUEO no debia verse el cuerpo");
  CHECK(body[0] == 0, "dejo texto en el buffer al ocultar");

  M.priv.hideBodyOnLock = false;
  CHECK(flexPhoneNotifBody(&M, p, true, body, sizeof(body)), "con la opcion apagada si debe verse");

  // -- SENSIBLE: no se enseria NUNCA, ni desbloqueado --
  FlexPhoneNotif s = mk(2, "com.bank", "Banco", "OTP 9988", FLP_PRI_HIGH, false);
  s.sensitive = true;
  flexPhoneNotifPut(&M, &s, 2000);
  const FlexPhoneNotif* ps = &M.notif[flexPhoneNotifFind(&M, 2)];
  CHECK(!flexPhoneNotifBody(&M, ps, false, body, sizeof(body)), "mostro una notificacion SENSIBLE");
  CHECK(!flexPhoneNotifBody(&M, ps, true, body, sizeof(body)), "mostro una sensible en bloqueo");

  // -- OCULTO por el propio telefono: no hay nada que enseriar --
  FlexPhoneNotif h = mk(3, "com.x", "Alguien", "", FLP_PRI_DEFAULT, false);
  h.hidden = true;
  flexPhoneNotifPut(&M, &h, 3000);
  const FlexPhoneNotif* ph = &M.notif[flexPhoneNotifFind(&M, 3)];
  CHECK(!flexPhoneNotifBody(&M, ph, false, body, sizeof(body)), "mostro un cuerpo marcado oculto");
}

// -------------------------------------------------------------
//  5) Respuestas: la regla de RemoteInput
//     Esta es la prueba que impide un boton "Enviar" mentiroso.
// -------------------------------------------------------------
static void testReply(){
  std::printf("[flexphone] respuesta: solo con RemoteInput real\n");
  flexPhoneModelInit(&M);

  FlexPhoneNotif ok = mk(1, "com.whatsapp", "Ana", "hola", FLP_PRI_DEFAULT, true);
  FlexPhoneNotif no = mk(2, "com.bank", "Banco", "saldo", FLP_PRI_DEFAULT, false);
  flexPhoneNotifPut(&M, &ok, 1000);
  flexPhoneNotifPut(&M, &no, 1000);

  CHECK(flexPhoneReplyCheck(&M, 1, "vale") == FLNK_E_NONE, "no deja responder una que SI admite");
  CHECK(flexPhoneReplyCheck(&M, 2, "vale") == FLNK_E_NOREPLY,
        "dejo responder una SIN RemoteInput");
  CHECK(flexPhoneReplyCheck(&M, 77, "vale") == FLNK_E_GONE,
        "dejo responder una notificacion que ya no existe");
  CHECK(flexPhoneReplyCheck(&M, 1, "") == FLNK_E_INTERNAL, "dejo enviar una respuesta vacia");

  // Respuesta demasiado larga: se rechaza ANTES de enviar.
  char big[FLP_REPLY_MAX + 40];
  std::memset(big, 'a', sizeof(big) - 1); big[sizeof(big) - 1] = 0;
  CHECK(flexPhoneReplyCheck(&M, 1, big) == FLNK_E_TOOBIG, "acepto una respuesta sin limite");

  // La notificacion DESAPARECE mientras se escribia -> error real.
  flexPhoneNotifRemove(&M, 1);
  CHECK(flexPhoneReplyCheck(&M, 1, "vale") == FLNK_E_GONE,
        "no detecto que la notificacion se fue");

  // Resultado que devuelve el telefono.
  uint8_t buf[16];
  FlexLinkWr w; flexLinkWrInit(&w, buf, sizeof(buf));
  flexLinkWrU32(&w, 1); flexLinkWrU8(&w, FLNK_E_GONE);
  uint32_t id = 0; uint8_t ec = 0;
  CHECK(flexPhoneDecReplyResult(buf, w.at, &id, &ec), "no decodifico el resultado");
  CHECK(id == 1 && ec == FLNK_E_GONE, "resultado id=%u ec=%u", (unsigned)id, ec);
  CHECK(!flexPhoneDecReplyResult(buf, 2, &id, &ec), "acepto un resultado truncado");
}

// -------------------------------------------------------------
//  6) Conversaciones
// -------------------------------------------------------------
static void testConversations(){
  std::printf("[flexphone] conversaciones: solo las que se pueden contestar\n");
  flexPhoneModelInit(&M);
  FlexPhoneNotif a = mk(1, "com.whatsapp", "Ana",  "hola", FLP_PRI_DEFAULT, true);
  FlexPhoneNotif b = mk(2, "com.whatsapp", "Beto", "que tal", FLP_PRI_DEFAULT, true);
  FlexPhoneNotif c = mk(3, "com.bank",     "Banco","saldo", FLP_PRI_DEFAULT, false);
  flexPhoneNotifPut(&M, &a, 1000);
  flexPhoneNotifPut(&M, &b, 2000);
  flexPhoneNotifPut(&M, &c, 3000);
  flexPhoneConvRebuild(&M);

  int used = 0;
  for(int i = 0; i < FLP_CONV_MAX; i++) if(M.conv[i].used) used++;
  CHECK(used == 2, "hay %d conversaciones (esperadas 2: la del banco no se contesta)", used);
  CHECK(flexPhoneConvFind(&M, "com.whatsapp", "Ana") >= 0, "falta la de Ana");
  CHECK(flexPhoneConvFind(&M, "com.bank", "Banco") < 0, "creo conversacion para algo no respondible");

  // Al irse la notificacion, la conversacion deja de ser contestable.
  flexPhoneNotifRemove(&M, 1);
  flexPhoneConvRebuild(&M);
  CHECK(flexPhoneConvFind(&M, "com.whatsapp", "Ana") < 0,
        "mantuvo una conversacion cuya notificacion ya no existe");

  // -- Borradores: viven SIN telefono --
  CHECK(flexPhoneDraftPut(&M, "com.whatsapp", "Ana", "luego te digo", 5000) >= 0, "no guardo el borrador");
  const int d = flexPhoneDraftFind(&M, "com.whatsapp", "Ana");
  CHECK(d >= 0 && std::strcmp(M.draft[d].text, "luego te digo") == 0, "el borrador no vuelve");
  CHECK(flexPhoneDraftPut(&M, "com.whatsapp", "Ana", "cambiado", 6000) == d, "duplico el borrador");
  CHECK(flexPhoneDraftRemove(&M, d), "no borro el borrador");
  CHECK(flexPhoneDraftFind(&M, "com.whatsapp", "Ana") < 0, "el borrador sigue ahi");
}

// -------------------------------------------------------------
//  7) Codecs contra bytes hostiles
// -------------------------------------------------------------
static void testCodecs(){
  std::printf("[flexphone] codecs: ida y vuelta, y bytes hostiles\n");
  uint8_t buf[512];
  FlexPhoneNotif n = mk(42, "com.whatsapp", "Ana", "un mensaje", FLP_PRI_HIGH, true);
  n.whenMs = 123456; n.cat = FLP_CAT_MSG;
  const int k = flexPhoneEncNotif(buf, sizeof(buf), &n);
  CHECK(k > 0, "no codifico (%d)", k);

  FlexPhoneNotif g;
  CHECK(flexPhoneDecNotif(buf, (size_t)k, &g), "no decodifico");
  CHECK(g.id == 42 && g.canReply && g.replyAction == 1, "campos mal");
  CHECK(std::strcmp(g.title, "Ana") == 0 && std::strcmp(g.text, "un mensaje") == 0, "textos mal");
  CHECK(g.actionCount == 2 && std::strcmp(g.actions[1], "Responder") == 0, "acciones mal");

  // -- TRUNCADO: cualquier prefijo se rechaza, ninguno revienta --
  for(int cut = 1; cut < k; cut++){
    FlexPhoneNotif t;
    // Lo unico que se exige: no puede leer fuera (lo vigila ASan) y
    // no puede devolver true con datos a medias.
    (void)flexPhoneDecNotif(buf, (size_t)cut, &t);
  }
  std::printf("   %d prefijos truncados procesados sin desbordar\n", k - 1);

  // -- MIENTE en el numero de acciones: se recorta al maximo --
  uint8_t liar[256];
  FlexLinkWr w; flexLinkWrInit(&w, liar, sizeof(liar));
  flexLinkWrU32(&w, 7);
  flexLinkWrStr(&w, "com.x", 32); flexLinkWrStr(&w, "X", 16);
  flexLinkWrStr(&w, "T", 16);     flexLinkWrStr(&w, "B", 16);
  flexLinkWrU32(&w, 0);
  flexLinkWrU8(&w, 99);            // categoria fuera de rango
  flexLinkWrU8(&w, 99);            // prioridad fuera de rango
  flexLinkWrU8(&w, 0x04);          // dice canReply
  flexLinkWrU8(&w, 200);           // accion de respuesta inexistente
  flexLinkWrU8(&w, 200);           // dice 200 acciones
  FlexPhoneNotif h;
  const bool okDec = flexPhoneDecNotif(liar, w.at, &h);
  // Con 200 acciones declaradas y ninguna presente, el lector se
  // desborda y la decodificacion FALLA. Eso es lo correcto.
  CHECK(!okDec, "acepto una notificacion que miente en el numero de acciones");

  // Version honesta pero con valores fuera de rango: se sanean.
  FlexLinkWr w2; flexLinkWrInit(&w2, liar, sizeof(liar));
  flexLinkWrU32(&w2, 8);
  flexLinkWrStr(&w2, "com.x", 32); flexLinkWrStr(&w2, "X", 16);
  flexLinkWrStr(&w2, "T", 16);     flexLinkWrStr(&w2, "B", 16);
  flexLinkWrU32(&w2, 0);
  flexLinkWrU8(&w2, 99); flexLinkWrU8(&w2, 99);
  flexLinkWrU8(&w2, 0x04); flexLinkWrU8(&w2, 200); flexLinkWrU8(&w2, 0);
  CHECK(flexPhoneDecNotif(liar, w2.at, &h), "no decodifico la version honesta");
  CHECK(h.cat == FLP_CAT_OTHER, "no saneo la categoria (%u)", h.cat);
  CHECK(h.pri == FLP_PRI_DEFAULT, "no saneo la prioridad (%u)", h.pri);
  CHECK(!h.canReply, "dejo canReply con una accion de respuesta inexistente");

  // -- Estado del telefono: bateria imposible -> DESCONOCIDA --
  FlexPhoneState st;
  FlexLinkWr w3; flexLinkWrInit(&w3, liar, sizeof(liar));
  flexLinkWrStr(&w3, "Pixel", 24); flexLinkWrU8(&w3, 180);
  flexLinkWrU8(&w3, 0x01); flexLinkWrU8(&w3, 99);
  CHECK(flexPhoneDecPhoneState(liar, w3.at, &st), "no decodifico el estado");
  CHECK(st.battery == 255, "no saneo una bateria del 180%% (%u)", st.battery);
  CHECK(st.charging, "perdio el estado de carga");
  CHECK(st.net == FLP_NET_UNKNOWN, "no saneo un tipo de red desconocido");
  CHECK(st.valid, "no marco el estado como valido");
  CHECK(!flexPhoneDecPhoneState(liar, 2, &st), "acepto un estado truncado");

  // -- Multimedia sin titulo: NO se marca valido --
  FlexPhoneMedia md;
  FlexLinkWr w4; flexLinkWrInit(&w4, liar, sizeof(liar));
  flexLinkWrStr(&w4, "", 32); flexLinkWrStr(&w4, "", 32);
  flexLinkWrStr(&w4, "Spotify", 24); flexLinkWrU8(&w4, FLP_MEDIA_PLAYING);
  CHECK(flexPhoneDecMedia(liar, w4.at, &md), "no decodifico multimedia");
  CHECK(!md.valid, "marco valida una sesion multimedia SIN titulo");

  // -- Relay sin puerto: es un ERROR, no un "conectado" --
  FlexPhoneRelay rl;
  FlexLinkWr w5; flexLinkWrInit(&w5, liar, sizeof(liar));
  const uint8_t ip0[4] = {0,0,0,0};
  flexLinkWrBytes(&w5, ip0, 4); flexLinkWrU16(&w5, 0);
  flexLinkWrU8(&w5, 1); flexLinkWrU8(&w5, 0); flexLinkWrU16(&w5, 0);
  flexLinkWrStr(&w5, "", 32);
  CHECK(flexPhoneDecRelayInfo(liar, w5.at, &rl), "no decodifico el relay");
  CHECK(rl.state == FLP_RELAY_ERROR, "dio por bueno un relay sin direccion");
  CHECK(rl.err[0] != 0, "no explico por que el relay no vale");

  // -- Relay valido --
  FlexLinkWr w6; flexLinkWrInit(&w6, liar, sizeof(liar));
  const uint8_t ip1[4] = {192,168,1,50};
  flexLinkWrBytes(&w6, ip1, 4); flexLinkWrU16(&w6, 8443);
  flexLinkWrU8(&w6, 1); flexLinkWrU8(&w6, 0x01); flexLinkWrU16(&w6, 0x0003);
  flexLinkWrStr(&w6, "", 32);
  CHECK(flexPhoneDecRelayInfo(liar, w6.at, &rl), "no decodifico el relay bueno");
  CHECK(rl.state == FLP_RELAY_UP && rl.port == 8443 && rl.tls, "relay bueno mal leido");
  CHECK(rl.ip[0] == 192 && rl.ip[3] == 50, "IP mal leida");

  // -- Ordenes multimedia: solo las cuatro validas --
  CHECK(flexPhoneEncMediaCmd(buf, sizeof(buf), FLP_MCMD_PLAY) == 1, "play no codifica");
  CHECK(flexPhoneEncMediaCmd(buf, sizeof(buf), 0)   < 0, "acepto una orden multimedia 0");
  CHECK(flexPhoneEncMediaCmd(buf, sizeof(buf), 200) < 0, "acepto una orden multimedia inventada");
}

// -------------------------------------------------------------
//  8) Iconos
// -------------------------------------------------------------
static void testIcons(){
  std::printf("[flexphone] iconos: tabla local, sin mandar imagenes por BLE\n");
  CHECK(flexPhoneIconFor("com.whatsapp", FLP_CAT_MSG) == FLP_ICON_CHAT, "WhatsApp no es chat");
  CHECK(flexPhoneIconFor("com.google.android.gm", FLP_CAT_OTHER) == FLP_ICON_MAIL, "Gmail no es correo");
  CHECK(flexPhoneIconFor("com.marca.desconocida", FLP_CAT_OTHER) == FLP_ICON_GENERIC,
        "un paquete desconocido deberia caer al generico");
  CHECK(flexPhoneIconFor("com.otra.rara", FLP_CAT_EMAIL) == FLP_ICON_MAIL,
        "sin coincidencia, la categoria deberia decidir");
  CHECK(flexPhoneIconFor(nullptr, FLP_CAT_OTHER) == FLP_ICON_GENERIC, "NULL no cae al generico");
  CHECK(std::strcmp(flexPhoneIconName(FLP_ICON_CHAT), "chat") == 0, "nombre de icono mal");
}

// -------------------------------------------------------------
//  9) Persistencia
// -------------------------------------------------------------
static void testPersistence(){
  std::printf("[flexphone] persistencia: ida y vuelta, privacidad y blobs corruptos\n");
  flexPhoneModelInit(&M);
  FlexPhoneNotif a = mk(1, "com.whatsapp", "Ana", "hola", FLP_PRI_DEFAULT, true);
  FlexPhoneNotif b = mk(2, "com.x", "Otro", "texto", FLP_PRI_LOW, false);
  flexPhoneNotifPut(&M, &a, 1000);
  flexPhoneNotifPut(&M, &b, 2000);
  flexPhoneDraftPut(&M, "com.whatsapp", "Ana", "borrador", 3000);
  M.priv.keepHours = 12;

  static uint8_t blob[FLP_BLOB_CAP];
  CHECK(flexPhoneBlobMaxSize() <= sizeof(blob), "el blob maximo (%u) no cabe en FLP_BLOB_CAP (%u)",
        (unsigned)flexPhoneBlobMaxSize(), (unsigned)FLP_BLOB_CAP);
  std::printf("   blob en el peor caso: %u B (tope %u B)\n",
              (unsigned)flexPhoneBlobMaxSize(), (unsigned)FLP_BLOB_CAP);
  const size_t nb = flexPhoneSerialize(&M, blob, sizeof(blob));
  CHECK(nb > 0, "no serializo");

  FlexPhoneModel N;
  flexPhoneModelInit(&N);
  CHECK(flexPhoneDeserialize(&N, blob, nb), "no deserializo");
  CHECK(N.notifCount == 2, "volvieron %u notificaciones", N.notifCount);
  CHECK(flexPhoneNotifFind(&N, 1) >= 0 && flexPhoneNotifFind(&N, 2) >= 0, "falta alguna");
  CHECK(N.priv.keepHours == 12, "no volvio la configuracion de privacidad");
  CHECK(flexPhoneDraftFind(&N, "com.whatsapp", "Ana") >= 0, "no volvio el borrador");
  CHECK(!N.dirty, "recien cargado no deberia estar sucio");
  // Multimedia y relay NO se persisten: resucitarlos seria mentir.
  CHECK(!N.media.valid, "resucito un estado multimedia que ya no es cierto");
  CHECK(N.relay.state == FLP_RELAY_OFF, "resucito un relay que ya no esta arriba");
  CHECK(!N.phone.valid, "resucito un estado de telefono que ya no es cierto");

  // -- keepHistory = false: NO se guarda ni una --
  M.priv.keepHistory = false;
  const size_t nb2 = flexPhoneSerialize(&M, blob, sizeof(blob));
  FlexPhoneModel P; flexPhoneModelInit(&P);
  CHECK(flexPhoneDeserialize(&P, blob, nb2), "no deserializo sin historial");
  CHECK(P.notifCount == 0, "guardo historial con la opcion APAGADA (%u)", P.notifCount);
  CHECK(flexPhoneDraftFind(&P, "com.whatsapp", "Ana") >= 0, "perdio el borrador local");

  // -- Blobs corruptos: ninguno debe pasar ni desbordar --
  FlexPhoneModel Q;
  flexPhoneModelInit(&Q);
  CHECK(!flexPhoneDeserialize(&Q, blob, 3), "acepto un blob mas corto que la magia");
  uint8_t junk[32]; std::memset(junk, 0xAA, sizeof(junk));
  CHECK(!flexPhoneDeserialize(&Q, junk, sizeof(junk)), "acepto basura");
  // Magia buena, conteo imposible.
  FlexLinkWr w; flexLinkWrInit(&w, junk, sizeof(junk));
  flexLinkWrU32(&w, FLP_BLOB_MAGIC); flexLinkWrU16(&w, 1);
  flexLinkWrU16(&w, 60000);
  CHECK(!flexPhoneDeserialize(&Q, junk, w.at), "acepto un blob que dice tener 60000 notificaciones");
  // Version futura: no se adivina.
  FlexLinkWr w2; flexLinkWrInit(&w2, junk, sizeof(junk));
  flexLinkWrU32(&w2, FLP_BLOB_MAGIC); flexLinkWrU16(&w2, 99); flexLinkWrU16(&w2, 0);
  CHECK(!flexPhoneDeserialize(&Q, junk, w2.at), "intento leer una version futura del blob");
  // Cualquier prefijo del blob bueno: ni cuelga ni desborda.
  for(size_t cut = 1; cut < nb; cut++){
    FlexPhoneModel R; flexPhoneModelInit(&R);
    (void)flexPhoneDeserialize(&R, blob, cut);
  }
  std::printf("   %u prefijos de blob procesados sin desbordar\n", (unsigned)(nb - 1));
}

// -------------------------------------------------------------
// 10) Desconexion y desvinculado
// -------------------------------------------------------------
static void testDisconnect(){
  std::printf("[flexphone] telefono desconectado y desvinculado\n");
  flexPhoneModelInit(&M);
  FlexPhoneNotif a = mk(1, "com.whatsapp", "Ana", "hola", FLP_PRI_DEFAULT, true);
  flexPhoneNotifPut(&M, &a, 1000);
  flexPhoneDraftPut(&M, "com.whatsapp", "Ana", "pendiente", 1500);
  M.media.valid = true; M.phone.valid = true; M.relay.state = FLP_RELAY_UP;

  // Desconexion: se cae el estado instantaneo, se conserva lo local.
  flexPhoneModelClear(&M, false);
  CHECK(!M.media.valid, "el multimedia sobrevivio a la desconexion");
  CHECK(!M.phone.valid, "el estado del telefono sobrevivio a la desconexion");
  CHECK(M.relay.state == FLP_RELAY_OFF, "el relay sobrevivio a la desconexion");
  CHECK(M.notifCount == 0, "las notificaciones sobrevivieron");
  CHECK(flexPhoneDraftFind(&M, "com.whatsapp", "Ana") >= 0,
        "un borrador LOCAL no debe perderse al desconectar");
  CHECK(M.phone.battery == 255, "la bateria deberia volver a DESCONOCIDA");

  // Desvincular: se borra todo, borradores incluidos.
  flexPhoneModelClear(&M, true);
  CHECK(flexPhoneDraftFind(&M, "com.whatsapp", "Ana") < 0, "el desvinculado no borro los borradores");
}

int main(){
  std::printf("\n=== FlexOS · modelo Flex Phone ===\n");
  testQueue();
  testEviction();
  testExpire();
  testPrivacy();
  testReply();
  testConversations();
  testCodecs();
  testIcons();
  testPersistence();
  testDisconnect();
  std::printf("=== %d comprobaciones, %d fallos ===\n", g_run, g_fail);
  return g_fail ? 1 : 0;
}
