// #############################################################
//  FLEX CLOUD · NUCLEO PORTABLE (FlexOS_CloudCore.cpp)
//  ------------------------------------------------------------
//  Lo que decide el P4 con bytes que llegan de internet: respuestas de la
//  API, el diario de subidas que sobrevive a un reinicio, la cache de
//  bloques del streaming y los nombres que caben en LittleFS. Con
//  sanitizers: un desbordamiento aqui no puede quedarse en "parece que
//  funciona".
// #############################################################
#include "FlexOS_CloudCore.h"
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include <random>
#include <openssl/sha.h>

static int gChecks = 0, gFails = 0;
#define CHECK(c, m) do { gChecks++; if(!(c)){ gFails++; printf("  FALLO: %s (%s:%d)\n", m, __FILE__, __LINE__); } } while(0)

static bool validUtf8(const char* s){
  const unsigned char* p = (const unsigned char*)s;
  while(*p){
    int n = *p < 0x80 ? 0 : (*p >> 5) == 6 ? 1 : (*p >> 4) == 14 ? 2 : (*p >> 3) == 30 ? 3 : -1;
    if(n < 0) return false;
    p++;
    for(int i = 0; i < n; i++, p++) if((*p & 0xC0) != 0x80) return false;
  }
  return true;
}

static void testJson(){
  printf("-- respuestas de la API --\n");
  std::string list = R"({"ok":true,"items":[
    {"type":"folder","id":"fld_aaaaaaaaaaaaaaaaaaaaaaaa","name":"Fotos de Año Nuevo 🎆","parentId":null,"createdAt":1,"updatedAt":1790000000000},
    {"type":"file","id":"fil_bbbbbbbbbbbbbbbbbbbbbbbb","name":"Vídeo ñandú 🎬.avi","parentId":"fld_aaaaaaaaaaaaaaaaaaaaaaaa","size":167772160,
     "mime":"video/x-msvideo","kind":"video","sha256":"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
     "hasThumbnail":true,"source":"device","metadata":{"width":800,"height":480,"durationMs":61000},"updatedAt":1790000001000},
    {"type":"file","id":"../../etc/passwd","name":"x"},
    {"type":"raro","id":"fil_cccccccccccccccccccccccc","name":"y"},
    {"type":"file","id":"fil_dddddddddddddddddddddddd","name":"sin_sha.bin","size":-5,"sha256":"NOHEX"}
  ],"nextCursor":"eyJvIjo0MH0","folder":{"id":"fld_x","name":"x","path":[{"id":"fld_aaaaaaaaaaaaaaaaaaaaaaaa","name":"Fotos de Año Nuevo 🎆"}]}})";
  FclItem items[8]; char cursor[FCL_CURSOR_MAX]; bool more = false; FclCrumb crumbs[4]; int nc = 0;
  int n = fclParseList(list.data(), list.size(), items, 8, cursor, sizeof(cursor), &more, crumbs, 4, &nc);
  CHECK(n == 3, "3 elementos validos (id con ruta y tipo raro descartados)");
  CHECK(items[0].isFolder && !strcmp(items[0].name, "Fotos de Año Nuevo 🎆"), "carpeta con ñ y emoji");
  CHECK(!items[1].isFolder && items[1].kind == FCL_K_VIDEO && items[1].size == 167772160ull, "video de 160 MB");
  CHECK(items[1].hasThumb && items[1].fromDevice && items[1].durationMs == 61000 && items[1].width == 800, "metadatos");
  CHECK(!strcmp(items[1].parentId, "fld_aaaaaaaaaaaaaaaaaaaaaaaa") && strlen(items[1].sha256) == 64, "padre y sha");
  CHECK(items[2].size == 0 && items[2].sha256[0] == 0, "tamano negativo y sha no hexadecimal: se ignoran");
  CHECK(more && !strcmp(cursor, "eyJvIjo0MH0"), "cursor de la pagina siguiente");
  CHECK(nc == 1 && !strcmp(crumbs[0].name, "Fotos de Año Nuevo 🎆"), "ruta de la carpeta");

  // Capacidad menor que la pagina: no se escribe fuera.
  FclItem one[1];
  CHECK(fclParseList(list.data(), list.size(), one, 1, nullptr, 0, nullptr, nullptr, 0, nullptr) == 1, "respeta la capacidad");

  // Malformado / truncado / vacio.
  for(size_t cut = 0; cut < list.size(); cut += 37){
    FclItem t[8];
    int r = fclParseList(list.data(), cut, t, 8, cursor, sizeof(cursor), &more, crumbs, 4, &nc);
    if(r > 3){ CHECK(false, "un JSON truncado no puede dar mas elementos"); break; }
  }
  CHECK(fclParseList("{}", 2, items, 8, cursor, sizeof(cursor), &more, crumbs, 4, &nc) == -1, "sin items: -1");
  CHECK(fclParseList("", 0, items, 8, cursor, sizeof(cursor), &more, crumbs, 4, &nc) == -1, "vacio: -1");

  // Nombre de 255 bytes con emojis: se recorta sin partir un caracter.
  std::string longName;
  while(longName.size() < 250) longName += "🎬a";
  std::string one1 = "{\"items\":[{\"type\":\"file\",\"id\":\"fil_eeeeeeeeeeeeeeeeeeeeeeee\",\"name\":\"" + longName + "\"}]}";
  CHECK(fclParseList(one1.data(), one1.size(), items, 1, nullptr, 0, nullptr, nullptr, 0, nullptr) == 1, "nombre largo");
  CHECK(validUtf8(items[0].name) && strlen(items[0].name) < FCL_NAME_MAX, "nombre largo: UTF-8 valido");

  // Cuota.
  std::string me = R"({"ok":true,"account":{"flexAddress":"ana@flex","displayName":"Ana Núñez"},"quota":{"plan":"free","totalBytes":5368709120,"usedBytes":4900000000,"reservedBytes":100000000,"trashBytes":1000,"availableBytes":368709120,"state":"low"}})";
  char addr[48], dn[64]; FclQuota q;
  CHECK(fclParseMe(me.data(), me.size(), addr, sizeof(addr), dn, sizeof(dn), &q), "me");
  CHECK(!strcmp(addr, "ana@flex") && !strcmp(dn, "Ana Núñez"), "cuenta");
  CHECK(q.totalBytes == 5368709120ull && q.state == FCL_Q_LOW && q.permille == 932, "cuota (93,2 %)");
  FclQuota q2;
  CHECK(!fclParseQuota("{\"quota\":{\"totalBytes\":0}}", 24, &q2), "cuota sin total no vale");

  // Subida con bitmap de partes.
  std::string up = R"({"ok":true,"upload":{"uploadId":"upl_ffffffffffffffffffffffff","state":"active","size":1000000,"chunkSize":262144,"totalParts":4,"receivedParts":[1,3,3,99,0,-1,2.5],"receivedBytes":524288,"resumed":true}})";
  FclUpload u;
  CHECK(fclParseUpload(up.data(), up.size(), &u), "subida");
  CHECK(fclUploadHasPart(&u, 1) && !fclUploadHasPart(&u, 2) && fclUploadHasPart(&u, 3) && !fclUploadHasPart(&u, 4), "bitmap");
  CHECK(u.receivedCount == 3 && u.resumed, "partes contadas una vez (99 cuenta como dato: se valida contra el total)");
  std::string liar = R"({"upload":{"uploadId":"upl_ffffffffffffffffffffffff","size":10,"chunkSize":262144,"totalParts":900}})";
  CHECK(!fclParseUpload(liar.data(), liar.size(), &u), "una subida con un plan imposible se rechaza");
  std::string done = R"({"upload":{"uploadId":"upl_ffffffffffffffffffffffff","state":"completed","size":5,"chunkSize":65536,"totalParts":1,"receivedParts":[1],"fileId":"fil_gggggggggggggggggggggggg","file":{"type":"file","id":"fil_gggggggggggggggggggggggg","name":"a","sha256":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"}}})";
  CHECK(fclParseUpload(done.data(), done.size(), &u) && !strcmp(u.fileId, "fil_gggggggggggggggggggggggg") && u.sha256[0] == 'a', "subida terminada con archivo");
  FclItem fi;
  CHECK(fclParseItem(done.data(), done.size(), &fi) && !strcmp(fi.id, "fil_gggggggggggggggggggggggg"), "archivo dentro de la subida");

  char code[FCL_CODE_MAX], msg[FCL_MSG_MAX];
  std::string err = R"({"ok":false,"error":{"code":"quota_exceeded","message":"No queda espacio suficiente en tu Flex Cloud."}})";
  CHECK(fclParseError(err.data(), err.size(), code, sizeof(code), msg, sizeof(msg)) && !strcmp(code, "quota_exceeded"), "error");
  CHECK(!fclParseError("<html>502</html>", 16, code, sizeof(code), msg, sizeof(msg)) && !code[0], "pagina de error HTML: sin codigo");
  CHECK(strstr(fclErrorText("quota_exceeded"), "espacio") != nullptr, "texto legible");
  CHECK(fclErrorText("codigo_que_no_existe")[0] != 0 && fclErrorText(nullptr)[0] != 0, "siempre hay texto");
}

static void testSha(){
  printf("-- SHA-256 por partes --\n");
  char h[FCL_SHA_HEX];
  fclShaHex("abc", 3, h);
  CHECK(!strcmp(h, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"), "vector FIPS 180-4 'abc'");
  std::vector<uint8_t> big(3 * 1024 * 1024 + 17);
  std::mt19937 g(5); for(auto& b : big) b = (uint8_t)g();
  FclSha s; fclShaStart(&s);
  for(size_t off = 0; off < big.size(); off += 4093) fclShaUpdate(&s, big.data() + off, std::min<size_t>(4093, big.size() - off));
  char inc[FCL_SHA_HEX]; fclShaFinishHex(&s, inc);
  uint8_t d[32]; SHA256(big.data(), big.size(), d);
  char ref[65]; for(int i = 0; i < 32; i++) snprintf(ref + 2 * i, 3, "%02x", d[i]);
  CHECK(!strcmp(inc, ref), "por trozos = de una vez (OpenSSL)");
}

static void testJournal(){
  printf("-- diario de subidas --\n");
  FclJournal j; fclJournalInit(&j);
  FclJob* a = fclJournalAlloc(&j);
  a->state = FCL_JOB_ACTIVE; a->type = FCL_JOB_UPLOAD; a->flags = FCL_JF_FREE_LOCAL | FCL_JF_FROM_LIBRARY;
  a->mlId = 77; a->size = 9999999;
  snprintf(a->localPath, sizeof(a->localPath), "/Videos/Cumpleaños 🎂.avi");
  snprintf(a->name, sizeof(a->name), "Cumpleaños 🎂.avi");
  snprintf(a->remoteId, sizeof(a->remoteId), "upl_ffffffffffffffffffffffff");
  snprintf(a->sha256, sizeof(a->sha256), "%064d", 0);
  FclJob* b = fclJournalAlloc(&j);
  b->state = FCL_JOB_QUEUED; b->type = FCL_JOB_DOWNLOAD;
  snprintf(b->remoteId, sizeof(b->remoteId), "fil_gggggggggggggggggggggggg");
  std::vector<uint8_t> buf(fclJournalMaxBytes());
  size_t n = fclJournalEncode(&j, buf.data(), buf.size());
  CHECK(n > 0 && n <= fclJournalMaxBytes(), "cabe en el maximo declarado");
  FclJournal k; bool dmg = true;
  CHECK(fclJournalDecode(&k, buf.data(), n, &dmg) && !dmg, "se lee");
  FclJob* ka = fclJournalFind(&k, a->id);
  CHECK(ka && ka->state == FCL_JOB_QUEUED, "un trabajo a medias vuelve a la cola al arrancar");
  CHECK(ka && !strcmp(ka->localPath, "/Videos/Cumpleaños 🎂.avi") && ka->mlId == 77 && ka->size == 9999999 && (ka->flags & FCL_JF_FREE_LOCAL), "campos intactos");
  CHECK(fclJournalNext(&k) == ka, "el mas antiguo primero");
  CHECK(k.nextId > b->id, "los ids no se repiten tras reiniciar");
  // Un byte cambiado en un registro: ese registro se descarta, el resto sigue.
  std::vector<uint8_t> bad(buf.begin(), buf.begin() + n);
  bad[30] ^= 0x5A;
  CHECK(fclJournalDecode(&k, bad.data(), bad.size(), &dmg) && dmg, "CRC: se detecta");
  CHECK(!fclJournalFind(&k, a->id) && fclJournalFind(&k, b->id), "el registro danado se descarta entero; el otro sobrevive");
  for(size_t cut = 0; cut < n; cut += 29){ FclJournal t; fclJournalDecode(&t, buf.data(), cut, &dmg); }
  CHECK(true, "truncado en cualquier punto: sin lecturas fuera");
  std::vector<uint8_t> old(buf.begin(), buf.begin() + n); old[4] = 9;
  CHECK(fclJournalDecode(&k, old.data(), old.size(), &dmg) && dmg && fclJournalCount(&k, FCL_JOB_QUEUED) == 0, "otra version: diario vacio, no basura");
  // Diario lleno: se reutiliza el terminado mas antiguo, nunca uno en curso.
  fclJournalInit(&j);
  for(int i = 0; i < FCL_JOBS_MAX; i++){ FclJob* x = fclJournalAlloc(&j); x->state = FCL_JOB_QUEUED; x->type = FCL_JOB_UPLOAD; }
  CHECK(fclJournalAlloc(&j) == nullptr, "lleno de trabajos en curso: no hay hueco");
  j.jobs[3].state = FCL_JOB_DONE; j.jobs[5].state = FCL_JOB_FAILED;
  FclJob* r = fclJournalAlloc(&j);
  CHECK(r == &j.jobs[3], "se reutiliza el terminado mas antiguo");
}

// Simula la tarea de red: trae bloques de un "archivo" en memoria.
static void fetchSome(FclCache& c, const std::vector<uint8_t>& file, int maxBlocks){
  for(int i = 0; i < maxBlocks; i++){
    uint32_t off, len;
    int s = fclCacheNextFetch(&c, &off, &len);
    if(s < 0) return;
    memcpy(fclCacheSlot(&c, s), file.data() + off, len);
    fclCacheCommit(&c, s);
  }
}

static void testCache(){
  printf("-- cache de bloques del streaming --\n");
  const uint32_t BS = 64 * 1024; const uint16_t NB = 16;
  std::vector<uint8_t> file(5 * 1024 * 1024 + 12345);
  std::mt19937 g(9); for(auto& b : file) b = (uint8_t)g();
  std::vector<uint8_t> arena((size_t)BS * NB);
  FclCache c; fclCacheInit(&c, arena.data(), BS, NB, (uint32_t)file.size());
  uint8_t buf[300000];
  CHECK(fclCacheRead(&c, 0, buf, 12) == -1 && c.wantSet && c.wantOff == 0, "sin datos: no bloquea, anota lo que falta");
  fetchSome(c, file, 100);
  CHECK(fclCacheRead(&c, 0, buf, 12) == 12 && !memcmp(buf, file.data(), 12), "cabecera");
  CHECK(fclCacheContiguous(&c, 0) == (NB - 2) * BS, "lee por delante hasta la ventana");
  // Lectura secuencial con la red al mismo ritmo: nunca falta nada salvo al principio.
  uint32_t pos = 0; int misses = 0;
  while(pos < file.size()){
    uint32_t n = std::min<uint32_t>(48000, (uint32_t)file.size() - pos);
    int r = fclCacheRead(&c, pos, buf, n);
    if(r < 0){ misses++; fetchSome(c, file, 2); continue; }
    if(memcmp(buf, file.data() + pos, n)){ CHECK(false, "datos distintos"); break; }
    pos += n;
    fetchSome(c, file, 1);
  }
  CHECK(pos == file.size(), "se lee el archivo entero a traves de 1 MB de arena");
  CHECK(misses <= 2, "con la red al ritmo del lector casi no hay esperas");
  CHECK(fclCacheRead(&c, (uint32_t)file.size(), buf, 10) == 0, "fin del archivo");
  // Lectura que cruza bloques y el final corto.
  fclCacheSeek(&c, (uint32_t)file.size() - 70000);
  for(int i = 0; i < 40; i++){ if(fclCacheRead(&c, (uint32_t)file.size() - 70000, buf, 70000) == 70000) break; fetchSome(c, file, 3); }
  CHECK(!memcmp(buf, file.data() + file.size() - 70000, 70000), "cruza bloques hasta el ultimo (corto)");
  // Fijar un rango (indice del AVI al final) mientras se lee el principio.
  fclCacheInit(&c, arena.data(), BS, NB, (uint32_t)file.size());
  fclCachePin(&c, (uint32_t)file.size() - 200000, 200000);
  fetchSome(c, file, 4);
  CHECK(fclCacheReady(&c, (uint32_t)file.size() - 200000, 200000), "lo fijado llega primero");
  fetchSome(c, file, 100);
  CHECK(fclCacheReady(&c, (uint32_t)file.size() - 200000, 200000), "y no se desaloja mientras este fijado");
  fclCacheUnpin(&c);
  // Lecturas al azar contra la referencia, como un reproductor que salta:
  // cada lectura ACABA sirviendose (la red trae lo que falta) y nunca con un
  // dato equivocado.
  int ok = 0, bad = 0, stuck = 0, waits = 0;
  for(int i = 0; i < 4000; i++){
    uint32_t off = g() % (uint32_t)file.size();
    uint32_t n = 1 + g() % 200000;
    if(i % 50 == 0) fclCacheSeek(&c, off);
    int r = -1;
    for(int tries = 0; tries < 60 && r < 0; tries++){
      r = fclCacheRead(&c, off, buf, n);
      if(r < 0){ waits++; fetchSome(c, file, 1 + (int)(g() % 3)); }
    }
    if(r < 0){ stuck++; continue; }
    if(memcmp(buf, file.data() + off, (size_t)r)) bad++; else ok++;
  }
  if(bad || stuck) printf("  (correctas %d, incorrectas %d, atascadas %d)\n", ok, bad, stuck);
  CHECK(bad == 0, "4000 lecturas al azar: nunca un byte equivocado");
  CHECK(stuck == 0 && ok == 4000, "y todas acaban sirviendose (sin esperas eternas)");
  printf("  (%d esperas de red en 4000 saltos)\n", waits);
  int loading = 0; for(int i = 0; i < NB; i++) if(c.blocks[i].state == FCL_B_LOADING) loading++;
  CHECK(loading == 0, "ningun bloque se queda a medias");
  // Una lectura mas grande que la arena no puede satisfacerse: debe decirlo
  // (-1) en vez de bloquear o escribir fuera.
  std::vector<uint8_t> huge((size_t)BS * NB + 10);
  CHECK(fclCacheRead(&c, 0, huge.data(), (uint32_t)huge.size()) == -1, "lectura mayor que la arena: -1");
  // Abortar libera el hueco.
  uint32_t off, len; int s = fclCacheNextFetch(&c, &off, &len);
  if(s >= 0){ fclCacheAbort(&c, s); CHECK(c.blocks[s].state == FCL_B_EMPTY, "abortar deja el hueco vacio"); }
  // Sin arena: todo responde sin tocar memoria.
  FclCache z; fclCacheInit(&z, nullptr, BS, NB, 1000);
  CHECK(fclCacheRead(&z, 0, buf, 10) == -1 && fclCacheNextFetch(&z, &off, &len) == -1, "sin arena: nada");
}

static void testNames(){
  printf("-- nombres para LittleFS (48 bytes) --\n");
  char out[48];
  fclLocalName("Vídeo del cumpleaños de la abuela en la playa de Málaga 🎂🎉.avi", out, sizeof(out));
  CHECK(strlen(out) < 48 && validUtf8(out) && strstr(out, ".avi") == out + strlen(out) - 4, "recorta sin partir y conserva la extension");
  fclLocalName("a/b\\c:d*e?f\"g<h>i|j.txt", out, sizeof(out));
  CHECK(!strpbrk(out, "/\\:*?\"<>|") && !strcmp(out, "a_b_c_d_e_f_g_h_i_j.txt"), "caracteres prohibidos");
  fclLocalName("...oculto.jpg", out, sizeof(out));
  CHECK(out[0] != '.', "sin punto inicial");
  fclLocalName("", out, sizeof(out));
  CHECK(!strcmp(out, "archivo"), "vacio");
  fclLocalName("sin_extension", out, sizeof(out));
  CHECK(!strcmp(out, "sin_extension"), "sin extension");
  fclLocalName("🎬🎬🎬🎬🎬🎬🎬🎬🎬🎬🎬🎬🎬🎬🎬🎬🎬🎬.mp4", out, sizeof(out));
  CHECK(validUtf8(out) && strlen(out) < 48, "solo emojis");
  fclLocalName("nombre.con.puntos.y.una.extension.muy.larga.que.no.es.extension", out, sizeof(out));
  CHECK(strlen(out) < 48 && validUtf8(out), "extension larga se trata como nombre");
  char tiny[6]; fclLocalName("abcdefghij.jpg", tiny, sizeof(tiny));
  CHECK(strlen(tiny) < 6, "capacidad minima");
  char enc[64];
  CHECK(fclUrlEncode("Año 🎆/x?y", enc, sizeof(enc)) && !strcmp(enc, "A%C3%B1o%20%F0%9F%8E%86%2Fx%3Fy"), "url");
  CHECK(!fclUrlEncode("xxxxxxxxxx", enc, 5), "url que no cabe");
  char js[64];
  CHECK(fclJsonEscape("a\"b\\c\n\x01", js, sizeof(js)) && !strcmp(js, "a\\\"b\\\\c\\n\\u0001"), "json");
  char sz[24];
  fclFmtBytes(5368709120ull, sz, sizeof(sz)); CHECK(!strcmp(sz, "5 GB"), "5 GB");
  fclFmtBytes(167772160ull, sz, sizeof(sz)); CHECK(!strcmp(sz, "160 MB"), "160 MB");
  fclFmtBytes(1536, sz, sizeof(sz)); CHECK(!strcmp(sz, "1,5 KB"), "1,5 KB");
  CHECK(fclBackoffMs(1) == 2000 && fclBackoffMs(3) == 8000 && fclBackoffMs(30) == 60000, "espera creciente con tope");
  CHECK(fclChunkFor(11 * 1024 * 1024) == 256 * 1024, "partes de 256 KB en el P4");
  CHECK(fclChunkFor(1ull << 30) * (uint64_t)FCL_PARTS_MAX >= (1ull << 30), "archivos enormes caben en el bitmap");
}

int main(){
  printf("=== FlexOS · Flex Cloud: nucleo portable ===\n");
  testJson();
  testSha();
  testJournal();
  testCache();
  testNames();
  printf("=== %d comprobaciones, %d fallos ===\n", gChecks, gFails);
  return gFails ? 1 : 0;
}
