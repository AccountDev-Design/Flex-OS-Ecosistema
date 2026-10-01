// #############################################################
//  FLEX CLOUD MANAGER (P4) · el modulo REAL contra un Flex Cloud simulado
//  ------------------------------------------------------------
//  Compila y EJECUTA FlexOS_Cloud.cpp, FlexOS_Account.cpp, FlexOS_FS.cpp,
//  FlexOS_CloudCore.cpp y FlexOS_JPEG.cpp tal cual van a la placa, contra:
//    · netstub/: red, Wi-Fi, NVS y reloj programables (y PSRAM contada);
//    · fsstub/:  LittleFS en memoria que sobrevive a un "apagado";
//    · un servidor que contesta como cloud/ de Flex-Developer-Studio: las
//      mismas rutas, los mismos codigos de error, la misma reanudacion por
//      clientKey, Range/If-Range y la reserva de cuota.
//
//  Lo que tiene que quedar demostrado (no "compila, luego va"):
//    · subir y bajar con verificacion SHA-256 de punta a punta;
//    · reanudar tras corte de Wi-Fi, servidor caido, conexion cortada a
//      mitad, error de una parte y APAGADO del P4 a mitad;
//    · el original nunca se toca y "liberar espacio" solo se ofrece tras la
//      confirmacion de la nube con la misma huella;
//    · cancelar suelta la reserva en la nube (tambien si se cancelo sin red);
//    · un video de 160 MB se reproduce con memoria ACOTADA;
//    · la credencial solo viaja por TLS verificado y nunca se envia un
//      identificador de cuenta elegido por el cliente.
// #############################################################
#include "netstub.h"
#include "FS.h"
#include "FlexOS_Account.h"
#include "FlexOS_Cloud.h"
#include "FlexOS_CloudTLS.h"
#include "FlexOS_FS.h"
#include "FlexOS_JPEGEnc.h"
#include "pkgbuild.h"
#include <cJSON.h>
#include <string>
#include <map>
#include <vector>
#include <functional>

void fsStubReset();

static int gChecks = 0, gFails = 0;
#define CHECK(cond, msg) do { gChecks++; if(!(cond)){ gFails++; printf("  FALLO: %s  (%s:%d)\n", msg, __FILE__, __LINE__); } } while(0)

static const std::string HOST = "https://flex-developer-studio.ralvarezsantos980.chatgpt.site";
static const std::string API = HOST + "/api/cloud";
static const std::string CODE_URL = HOST + "/api/devices/code";

// ============================== cuenta (enlace) ==============================
static pkgb::Key gKey;
static const char* KEY_ID = "69b91cf7a9246cc2084dda73ccef77b2dc1a372b18fec19b49cf980efd68af69";
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

// ============================== Flex Cloud simulado ==============================
// Contenido generado para archivos enormes: no se guardan 160 MB en la prueba.
static uint8_t genByte(uint64_t i){ uint32_t x = (uint32_t)(i * 2654435761u) ^ (uint32_t)(i >> 11); return (uint8_t)(x >> 13); }

struct FFile {
  std::string id, parent, name, mime, kind, sha, data, thumb;
  uint64_t size = 0; bool generated = false; bool trashed = false; int64_t updated = 0; bool fromDevice = false;
  uint8_t at(uint64_t i) const { return generated ? genByte(i) : (uint8_t)data[i]; }
};
struct FFolder { std::string id, parent, name; bool trashed = false; };
struct FUpload {
  std::string id, name, parent, sha, clientKey, state = "active", fileId;
  uint64_t size = 0, received = 0; uint32_t chunk = 0, total = 0;
  std::map<uint32_t, std::string> parts;
};

struct Cloud {
  std::map<std::string, FFile> files;
  std::map<std::string, FFolder> folders;
  std::map<std::string, FUpload> uploads;
  uint64_t total = 5ull << 30, used = 0, reserved = 0;
  std::string tokenHash, pendingHash;
  int pendingPolls = 0;
  int seq = 0;
  int64_t clock = 1767225600000;           // 2026-01-01
  // Fallos a proposito: si devuelve true, `rs` es la respuesta.
  std::function<bool(const NetRequest&, NetResponse&)> fault;
  // Contadores
  int me = 0, creates = 0, resumes = 0, partPuts = 0, completes = 0, aborts = 0, statusGets = 0, downloads = 0, thumbs = 0;
  std::vector<uint32_t> partLog;
  std::vector<std::string> ranges;
  bool corruptDownload = false;
} C;

static std::string newId(const char* pre){ char b[32]; snprintf(b, sizeof(b), "%s_%06d", pre, ++C.seq); return b; }

static std::string jesc(const std::string& s){
  std::string o;
  for(unsigned char c : s){
    if(c == '"' || c == '\\'){ o += '\\'; o += (char)c; }
    else if(c < 0x20){ char b[8]; snprintf(b, sizeof(b), "\\u%04x", c); o += b; }
    else o += (char)c;
  }
  return o;
}
static std::string kindOf(const std::string& name){
  std::string n = name; for(auto& c : n) c = (char)tolower((unsigned char)c);
  auto ends = [&](const char* e){ size_t l = strlen(e); return n.size() >= l && !n.compare(n.size() - l, l, e); };
  if(ends(".jpg") || ends(".jpeg") || ends(".png")) return "photo";
  if(ends(".avi") || ends(".mp4") || ends(".mov")) return "video";
  if(ends(".mp3") || ends(".wav")) return "audio";
  return "other";
}
static std::string quotaJson(){
  uint64_t avail = C.total > C.used + C.reserved ? C.total - C.used - C.reserved : 0;
  double r = (double)(C.used + C.reserved) / (double)C.total;
  const char* st = r >= 1.0 ? "full" : r >= 0.9 ? "low" : "ok";
  char b[300];
  snprintf(b, sizeof(b), "{\"plan\":\"free\",\"totalBytes\":%llu,\"usedBytes\":%llu,\"reservedBytes\":%llu,\"trashBytes\":0,\"availableBytes\":%llu,\"state\":\"%s\"}",
           (unsigned long long)C.total, (unsigned long long)C.used, (unsigned long long)C.reserved, (unsigned long long)avail, st);
  return b;
}
static std::string fileJson(const FFile& f){
  char b[256];
  snprintf(b, sizeof(b), ",\"size\":%llu,\"updatedAt\":%lld,\"deletedAt\":%s", (unsigned long long)f.size, (long long)f.updated, f.trashed ? "1" : "null");
  return "{\"type\":\"file\",\"id\":\"" + f.id + "\",\"parentId\":" + (f.parent.empty() ? std::string("null") : "\"" + f.parent + "\"") +
         ",\"name\":\"" + jesc(f.name) + "\",\"mime\":\"" + f.mime + "\",\"kind\":\"" + f.kind + "\",\"sha256\":\"" + f.sha + "\"" + b +
         ",\"hasThumbnail\":" + (f.thumb.empty() ? "false" : "true") + ",\"source\":\"" + (f.fromDevice ? "device" : "web") + "\"}";
}
static std::string folderJson(const FFolder& d){
  return "{\"type\":\"folder\",\"id\":\"" + d.id + "\",\"parentId\":" + (d.parent.empty() ? std::string("null") : "\"" + d.parent + "\"") +
         ",\"name\":\"" + jesc(d.name) + "\"" + (d.trashed ? ",\"deletedAt\":1" : "") + "}";
}
static std::string uploadJson(const FUpload& u){
  std::string parts;
  for(auto& kv : u.parts){ if(!parts.empty()) parts += ","; parts += std::to_string(kv.first); }
  char b[400];
  snprintf(b, sizeof(b), "{\"uploadId\":\"%s\",\"state\":\"%s\",\"size\":%llu,\"receivedBytes\":%llu,\"chunkSize\":%u,\"totalParts\":%u,\"sha256\":\"%s\"",
           u.id.c_str(), u.state.c_str(), (unsigned long long)u.size, (unsigned long long)u.received, u.chunk, u.total, u.sha.c_str());
  std::string o = std::string(b) + ",\"receivedParts\":[" + parts + "]";
  if(!u.fileId.empty()){ o += ",\"fileId\":\"" + u.fileId + "\""; auto it = C.files.find(u.fileId); if(it != C.files.end()) o += ",\"file\":" + fileJson(it->second); }
  return o + "}";
}
static NetResponse jerr(int st, const char* code){
  NetResponse r; r.status = st;
  r.body = std::string("{\"ok\":false,\"error\":{\"code\":\"") + code + "\",\"message\":\"x\"}}";
  return r;
}
static NetResponse jok(const std::string& inner, int st = 200){ NetResponse r; r.status = st; r.body = "{\"ok\":true," + inner + "}"; return r; }
static std::string qp(const std::string& url, const char* k){
  size_t q = url.find('?'); if(q == std::string::npos) return "";
  std::string key = std::string(k) + "=";
  size_t p = url.find(key, q); if(p == std::string::npos || (url[p - 1] != '?' && url[p - 1] != '&')) return "";
  size_t e = url.find('&', p);
  std::string v = url.substr(p + key.size(), e == std::string::npos ? std::string::npos : e - p - key.size());
  std::string out;                                           // %XX
  for(size_t i = 0; i < v.size(); i++){
    if(v[i] == '%' && i + 2 < v.size()){ out += (char)strtol(v.substr(i + 1, 2).c_str(), nullptr, 16); i += 2; }
    else out += v[i];
  }
  return out;
}
static std::string bodyStr(const NetRequest& rq, const char* k){
  cJSON* j = cJSON_Parse(rq.body.c_str());
  cJSON* v = cJSON_GetObjectItem(j, k);
  std::string s = cJSON_IsString(v) ? v->valuestring : "";
  cJSON_Delete(j);
  return s;
}
static double bodyNum(const NetRequest& rq, const char* k, double d){
  cJSON* j = cJSON_Parse(rq.body.c_str());
  cJSON* v = cJSON_GetObjectItem(j, k);
  double r = cJSON_IsNumber(v) ? v->valuedouble : d;
  cJSON_Delete(j);
  return r;
}
static bool nameTaken(const std::string& parent, const std::string& name){
  for(auto& kv : C.files) if(!kv.second.trashed && kv.second.parent == parent && kv.second.name == name) return true;
  for(auto& kv : C.folders) if(!kv.second.trashed && kv.second.parent == parent && kv.second.name == name) return true;
  return false;
}
static std::string uniqueName(const std::string& parent, const std::string& name){
  if(!nameTaken(parent, name)) return name;
  size_t dot = name.rfind('.');
  std::string stem = dot == std::string::npos ? name : name.substr(0, dot), ext = dot == std::string::npos ? "" : name.substr(dot);
  for(int i = 2;; i++){ std::string n = stem + " (" + std::to_string(i) + ")" + ext; if(!nameTaken(parent, n)) return n; }
}

static NetResponse list(const NetRequest& rq){
  std::string view = qp(rq.url, "view"), parent = qp(rq.url, "parentId"), kind = qp(rq.url, "kind"), q = qp(rq.url, "q");
  int limit = atoi(qp(rq.url, "limit").c_str()); if(limit <= 0 || limit > 200) limit = 50;
  int off = 0; std::string cur = qp(rq.url, "cursor"); if(!cur.empty() && cur[0] == 'o') off = atoi(cur.c_str() + 1);
  if(parent == "root") parent = "";
  std::vector<std::string> rows;
  if(view.empty()){
    for(auto& kv : C.folders) if(!kv.second.trashed && kv.second.parent == parent) rows.push_back(folderJson(kv.second));
    for(auto& kv : C.files) if(!kv.second.trashed && kv.second.parent == parent) rows.push_back(fileJson(kv.second));
  } else if(view == "trash"){
    for(auto& kv : C.folders) if(kv.second.trashed) rows.push_back(folderJson(kv.second));
    for(auto& kv : C.files) if(kv.second.trashed) rows.push_back(fileJson(kv.second));
  } else {
    for(auto& kv : C.files){
      const FFile& f = kv.second;
      if(f.trashed) continue;
      if(kind == "media" && f.kind != "photo" && f.kind != "video") continue;
      if(kind == "video" && f.kind != "video") continue;
      if(view == "search" && f.name.find(q) == std::string::npos) continue;
      rows.push_back(fileJson(f));
    }
  }
  std::string items;
  int end = off + limit < (int)rows.size() ? off + limit : (int)rows.size();
  for(int i = off; i < end; i++){ if(!items.empty()) items += ","; items += rows[i]; }
  std::string inner = "\"items\":[" + items + "]";
  if(end < (int)rows.size()) inner += ",\"nextCursor\":\"o" + std::to_string(end) + "\"";
  if(view.empty() && !parent.empty()){
    std::vector<const FFolder*> chain;
    for(std::string p = parent; !p.empty();){ auto it = C.folders.find(p); if(it == C.folders.end()) break; chain.insert(chain.begin(), &it->second); p = it->second.parent; }
    std::string path;
    for(auto* d : chain){ if(!path.empty()) path += ","; path += "{\"id\":\"" + d->id + "\",\"name\":\"" + jesc(d->name) + "\"}"; }
    inner += ",\"folder\":{\"id\":\"" + parent + "\",\"path\":[" + path + "]}";
  }
  return jok(inner);
}

static void finishUpload(FUpload& u){
  FFile f;
  f.id = newId("fil"); f.parent = u.parent; f.name = uniqueName(u.parent, u.name); f.kind = kindOf(u.name);
  f.mime = f.kind == "photo" ? "image/jpeg" : f.kind == "video" ? "video/x-msvideo" : "application/octet-stream";
  for(auto& kv : u.parts) f.data += kv.second;
  f.size = f.data.size(); f.sha = sha256hex(f.data); f.updated = ++C.clock; f.fromDevice = true;
  C.files[f.id] = f;
  u.fileId = f.id; u.state = "completed"; u.parts.clear();
  C.reserved -= u.size; C.used += u.size;
}

static NetResponse serveCloud(const NetRequest& rq){
  std::string path = rq.url.substr(API.size());
  std::string route = path.substr(0, path.find('?'));
  const std::string& m = rq.method;
  // ---- autenticacion: la huella de la credencial del dispositivo
  auto it = rq.headers.find("authorization");
  std::string bearer = (it != rq.headers.end() && it->second.rfind("Bearer ", 0) == 0) ? it->second.substr(7) : "";
  if(bearer.empty() || sha256hex(bearer) != C.tokenHash) return jerr(401, "device_revoked");
  if(route == "/me"){
    C.me++;
    return jok("\"account\":{\"id\":\"acc_1\",\"flexAddress\":\"ana.p4@flex\",\"displayName\":\"Ana\"},\"quota\":" + quotaJson());
  }
  if(route == "/files" && m == "GET") return list(rq);
  if(route == "/folders" && m == "POST"){
    std::string name = bodyStr(rq, "name"), parent = bodyStr(rq, "parentId");
    if(name.empty() || name.find('/') != std::string::npos) return jerr(400, "name_invalid");
    if(!parent.empty() && !C.folders.count(parent)) return jerr(404, "not_found");
    if(nameTaken(parent, name)) return jerr(409, "name_conflict");
    FFolder d; d.id = newId("fld"); d.parent = parent; d.name = name; C.folders[d.id] = d;
    return jok("\"folder\":" + folderJson(d), 201);
  }
  // /files/:id..., /folders/:id...
  auto seg = [&](int k) -> std::string { std::string r = route; for(int i = 0; i < k; i++){ size_t p = r.find('/', 1); if(p == std::string::npos) return ""; r = r.substr(p); } size_t e = r.find('/', 1); return r.substr(1, e == std::string::npos ? std::string::npos : e - 1); };
  std::string coll = seg(0), id = seg(1), action = seg(2), extra = seg(3);
  if(coll == "files" || coll == "folders"){
    bool isFile = coll == "files";
    if(isFile ? !C.files.count(id) : !C.folders.count(id)) return jerr(404, "not_found");
    if(m == "PATCH"){
      std::string name = bodyStr(rq, "name");
      if(name.empty()) return jerr(400, "name_invalid");
      std::string parent = isFile ? C.files[id].parent : C.folders[id].parent;
      if(nameTaken(parent, name)) return jerr(409, "name_conflict");
      if(isFile){ C.files[id].name = name; return jok("\"file\":" + fileJson(C.files[id])); }
      C.folders[id].name = name; return jok("\"folder\":" + folderJson(C.folders[id]));
    }
    if(m == "DELETE" && action.empty()){ if(isFile) C.files[id].trashed = true; else C.folders[id].trashed = true; return jok("\"trashed\":true"); }
    if(m == "POST" && action == "restore"){ if(isFile) C.files[id].trashed = false; else C.folders[id].trashed = false; return jok("\"restored\":true"); }
    if(m == "POST" && action == "permanent-delete"){
      if(isFile){ C.used -= C.files[id].size; C.files.erase(id); } else C.folders.erase(id);
      return jok("\"deleted\":true,\"quota\":" + quotaJson());
    }
    if(isFile && action == "thumbnail"){
      FFile& f = C.files[id];
      if(m == "PUT"){
        if(rq.headers.count("content-type") == 0 || rq.headers.at("content-type") != "image/jpeg") return jerr(400, "invalid_request");
        f.thumb = rq.body; return jok("\"thumbnail\":true");
      }
      C.thumbs++;
      if(f.thumb.empty()) return jerr(404, "not_found");
      NetResponse r; r.status = 200; r.body = f.thumb; r.headers["content-type"] = "image/jpeg"; return r;
    }
    return jerr(404, "not_found");
  }
  if(coll == "uploads"){
    if(m == "POST" && id.empty()){
      std::string name = bodyStr(rq, "name"), sha = bodyStr(rq, "sha256"), key = bodyStr(rq, "clientKey"), parent = bodyStr(rq, "parentId");
      double size = bodyNum(rq, "size", -1), chunk = bodyNum(rq, "chunkSize", 8 << 20);
      if(size < 0 || name.empty()) return jerr(400, "invalid_request");
      if(chunk < 65536) chunk = 65536;
      for(auto& kv : C.uploads){
        FUpload& u = kv.second;
        if(!key.empty() && u.clientKey == key && u.state == "active" && u.size == (uint64_t)size && u.name == name && u.parent == parent){
          C.resumes++;
          return jok("\"upload\":" + uploadJson(u).substr(0, uploadJson(u).size() - 1) + ",\"resumed\":true}");
        }
      }
      uint64_t avail = C.total > C.used + C.reserved ? C.total - C.used - C.reserved : 0;
      if((uint64_t)size > avail) return jerr(507, "quota_exceeded");
      FUpload u; u.id = newId("upl"); u.name = name; u.parent = parent; u.sha = sha; u.clientKey = key; u.size = (uint64_t)size;
      u.chunk = (uint32_t)chunk; u.total = u.size ? (uint32_t)((u.size + u.chunk - 1) / u.chunk) : 0;
      C.reserved += u.size; C.uploads[u.id] = u; C.creates++;
      return jok("\"upload\":" + uploadJson(u), 201);
    }
    if(!C.uploads.count(id)) return jerr(404, "upload_not_found");
    FUpload& u = C.uploads[id];
    if(m == "GET" && action.empty()){ C.statusGets++; return jok("\"upload\":" + uploadJson(u)); }
    if(m == "DELETE" && action.empty()){
      C.aborts++;
      if(u.state == "active"){ u.state = "aborted"; C.reserved -= u.size; u.parts.clear(); }
      return jok("\"upload\":" + uploadJson(u) + ",\"quota\":" + quotaJson());
    }
    if(m == "PUT" && action == "parts"){
      if(u.state != "active") return jerr(409, "upload_state");
      uint32_t n = (uint32_t)atoi(extra.c_str());
      if(n < 1 || n > u.total) return jerr(400, "part_out_of_range");
      uint64_t want = n < u.total ? u.chunk : u.size - (uint64_t)(n - 1) * u.chunk;
      if(rq.body.size() != want) return jerr(400, "part_size_mismatch");
      auto h = rq.headers.find("x-part-sha256");
      if(h == rq.headers.end() || h->second != sha256hex(rq.body)) return jerr(422, "checksum_mismatch");
      C.partPuts++; C.partLog.push_back(n);
      if(u.parts.count(n)){ if(u.parts[n] != rq.body) return jerr(409, "part_conflict"); }
      else { u.parts[n] = rq.body; u.received += rq.body.size(); }
      return jok("\"partNumber\":" + std::to_string(n) + ",\"receivedBytes\":" + std::to_string(u.received));
    }
    if(m == "POST" && action == "complete"){
      C.completes++;
      if(u.state == "completed") return jok("\"upload\":" + uploadJson(u));
      if(u.state != "active") return jerr(409, "upload_state");
      if(u.parts.size() != u.total) return jerr(409, "incomplete_upload");
      std::string all; for(auto& kv : u.parts) all += kv.second;
      std::string declared = bodyStr(rq, "sha256");
      if(sha256hex(all) != declared){ u.state = "failed"; C.reserved -= u.size; u.parts.clear(); return jerr(422, "checksum_mismatch"); }
      finishUpload(u);
      return jok("\"upload\":" + uploadJson(u) + ",\"quota\":" + quotaJson());
    }
    return jerr(404, "not_found");
  }
  if(coll == "download"){
    if(!C.files.count(id)) return jerr(404, "not_found");
    C.downloads++;
    FFile& f = C.files[id];
    uint64_t start = 0, end = f.size ? f.size - 1 : 0;
    bool partial = false;
    auto rh = rq.headers.find("range");
    auto ir = rq.headers.find("if-range");
    if(rh != rq.headers.end()) C.ranges.push_back(rh->second);
    if(rh != rq.headers.end() && (ir == rq.headers.end() || ir->second == "\"" + f.sha + "\"")){
      unsigned long long a = 0, b = 0;
      int k = sscanf(rh->second.c_str(), "bytes=%llu-%llu", &a, &b);
      if(k >= 1){
        if(a >= f.size) return jerr(416, "range_not_satisfiable");
        start = a; if(k == 2 && b < f.size) end = b; partial = true;
      }
    }
    NetResponse r; r.status = partial ? 206 : 200;
    r.headers["etag"] = "\"" + f.sha + "\"";
    r.headers["accept-ranges"] = "bytes";
    r.body.resize(f.size ? end - start + 1 : 0);
    for(uint64_t i = 0; i < r.body.size(); i++) r.body[i] = (char)f.at(start + i);
    if(C.corruptDownload && r.body.size() > 10) r.body[r.body.size() / 2] ^= 0x5A;
    return r;
  }
  return jerr(404, "not_found");
}

static NetResponse serve(const NetRequest& rq){
  NetResponse rs;
  if(C.fault && C.fault(rq, rs)) return rs;
  if(!rq.url.compare(0, CODE_URL.size(), CODE_URL)){
    if(rq.method == "POST"){
      C.pendingHash = bodyStr(rq, "tokenHash");
      rs.status = 201;
      rs.body = envelope("{\"schema\":1,\"purpose\":\"flex-device-code\",\"code\":\"FLX7Q2\",\"tokenHash\":\"" + C.pendingHash +
                         "\",\"activationUrl\":\"" + HOST + "/activate?code=FLX7Q2\"}");
      return rs;
    }
    std::string st = "pending";
    if(C.pendingPolls > 0) C.pendingPolls--; else { st = "approved"; C.tokenHash = C.pendingHash; }
    std::string extra = st == "approved" ? ",\"flexAddress\":\"ana.p4@flex\",\"displayName\":\"Ana\"" : "";
    rs.body = envelope("{\"schema\":1,\"purpose\":\"flex-device-status\",\"state\":\"" + st + "\"" + extra + "}");
    return rs;
  }
  if(!rq.url.compare(0, API.size(), API)) return serveCloud(rq);
  rs.status = 404;
  return rs;
}

// ============================== utilidades ==============================
static void step(unsigned long ms = 20){ flexAccountTestStep(); flexCloudTestStep(); netstubAdvance(ms); }
static void pump(int n, unsigned long ms = 20){ for(int i = 0; i < n; i++) step(ms); }
static bool pumpUntil(std::function<bool()> cond, int max = 20000, unsigned long ms = 20){
  for(int i = 0; i < max; i++){ if(cond()) return true; step(ms); }
  return cond();
}
static std::vector<FlexCloudEvent> gEv;
static void drain(){ FlexCloudEvent e; while(flexCloudPollEvent(&e)) gEv.push_back(e); }
static const FlexCloudEvent* findEv(uint8_t kind, uint32_t op){ for(auto& e : gEv) if(e.kind == kind && e.opId == op) return &e; return nullptr; }
static std::string evCode(uint8_t kind, uint32_t op){ const FlexCloudEvent* e = findEv(kind, op); return e ? e->code : "<sin aviso>"; }
static std::string evFile(uint8_t kind, uint32_t op){ const FlexCloudEvent* e = findEv(kind, op); return e ? e->fileId : "<sin aviso>"; }
static std::string evPath(uint8_t kind, uint32_t op){ const FlexCloudEvent* e = findEv(kind, op); return e ? e->localPath : "<sin aviso>"; }
static bool waitEv(uint8_t kind, uint32_t op, int max = 40000){ return pumpUntil([&]{ drain(); return findEv(kind, op) != nullptr; }, max); }
static FlexCloudStatus status(){ FlexCloudStatus s; flexCloudStatus(&s); return s; }
static FlexCloudListInfo listInfo(){ FlexCloudListInfo l; flexCloudListInfo(&l); return l; }
static FlexCloudXfer xfer(uint32_t id){
  FlexCloudXfer x[FLEX_CLOUD_XFERS]; memset(x, 0, sizeof(x));
  int n = flexCloudXfers(x, FLEX_CLOUD_XFERS);
  for(int i = 0; i < n; i++) if(x[i].id == id) return x[i];
  FlexCloudXfer none; memset(&none, 0, sizeof(none)); none.phase = 255; return none;
}
static std::string localFile(const std::string& path){ auto it = gFs.find(path); return it == gFs.end() ? std::string("<no>") : std::string(it->second.data.begin(), it->second.data.end()); }
static void putLocal(const char* path, const std::string& data){
  FlexFsStream* f = flexFsOpenWrite(path);
  if(f){ flexFsStreamWrite(f, data.data(), data.size()); flexFsStreamClose(f); }
}
static std::string pattern(size_t n, uint32_t seed){
  std::string s(n, '\0'); uint32_t x = seed * 747796405u + 2891336453u;
  for(size_t i = 0; i < n; i++){ x ^= x << 13; x ^= x >> 17; x ^= x << 5; s[i] = (char)x; }
  return s;
}
static FclItem itemOf(const FFile& f){
  FclItem it; memset(&it, 0, sizeof(it));
  snprintf(it.id, sizeof(it.id), "%s", f.id.c_str()); snprintf(it.name, sizeof(it.name), "%s", f.name.c_str());
  snprintf(it.sha256, sizeof(it.sha256), "%s", f.sha.c_str()); snprintf(it.parentId, sizeof(it.parentId), "%s", f.parent.c_str());
  it.size = f.size; it.kind = f.kind == "video" ? FCL_K_VIDEO : FCL_K_PHOTO;
  return it;
}
static FFile& addCloudFile(const std::string& name, const std::string& data, const std::string& parent = ""){
  FFile f; f.id = newId("fil"); f.name = name; f.parent = parent; f.data = data; f.size = data.size(); f.sha = sha256hex(data);
  f.kind = kindOf(name); f.mime = "application/octet-stream"; f.updated = ++C.clock;
  C.used += f.size;
  return C.files[f.id] = f;
}
static FFile& addGeneratedFile(const std::string& name, uint64_t size){
  FFile f; f.id = newId("fil"); f.name = name; f.size = size; f.generated = true; f.kind = kindOf(name); f.updated = ++C.clock;
  EVP_MD_CTX* c = EVP_MD_CTX_new(); EVP_DigestInit_ex(c, EVP_sha256(), nullptr);
  static uint8_t tmp[65536];
  for(uint64_t o = 0; o < size;){ size_t n = size - o < sizeof(tmp) ? (size_t)(size - o) : sizeof(tmp); for(size_t i = 0; i < n; i++) tmp[i] = genByte(o + i); EVP_DigestUpdate(c, tmp, n); o += n; }
  pkgb::Bytes d(32); unsigned dl = 32; EVP_DigestFinal_ex(c, d.data(), &dl); EVP_MD_CTX_free(c);
  f.sha = pkgb::hex(d);
  C.used += size;
  return C.files[f.id] = f;
}

// Revisa TODO lo que salio por la red hasta ahora.
static void auditNetwork(const char* when){
  char bearer[64] = "";
  flexAccountCopyBearer(bearer, sizeof(bearer));
  int withAuth = 0; bool tlsOk = true, leak = false, accountParam = false;
  for(auto& r : gNetLog){
    auto it = r.headers.find("authorization");
    if(it != r.headers.end()){
      withAuth++;
      if(!r.https || r.tlsInsecure || r.tlsCa != flexCloudRootCA()) tlsOk = false;
    }
    if(bearer[0] && (r.url.find(bearer) != std::string::npos || r.body.find(bearer) != std::string::npos)) leak = true;
    std::string lu = r.url, lb = r.body;
    for(auto& c : lu) c = (char)tolower((unsigned char)c);
    for(auto& c : lb) c = (char)tolower((unsigned char)c);
    if(lu.find("accountid") != std::string::npos || lb.find("accountid") != std::string::npos ||
       lu.find("account_id") != std::string::npos || lb.find("account_id") != std::string::npos) accountParam = true;
  }
  char msg[160];
  snprintf(msg, sizeof(msg), "%s: toda peticion con credencial va por HTTPS con la CA de Flex Cloud (nunca setInsecure)", when);
  CHECK(tlsOk, msg);
  snprintf(msg, sizeof(msg), "%s: la credencial nunca va en la URL ni en un cuerpo", when);
  CHECK(!leak, msg);
  snprintf(msg, sizeof(msg), "%s: el P4 nunca envia un id de cuenta (lo decide el servidor)", when);
  CHECK(!accountParam, msg);
  (void)withAuth;
}

// Arranque limpio: flash y NVS de fabrica, cuenta vinculada, Wi-Fi.
static void boot(bool link = true){
  netstubNvsWipe(); netstubReset(); fsStubReset();
  C = Cloud();
  gNetHandler = serve;
  gFsTotalBytes = 11136u * 1024u;
  flexFsBegin();
  flexFsMkdir("/System");
  flexAccountTestPowerCycle();
  flexCloudTestPowerCycle();
  gEv.clear();
  if(link){
    gNetWifi = true;
    C.pendingPolls = 1;
    CHECK(flexAccountRequestCode("Flex OS Ultra"), "enlace pedido");
    flexAccountTestStep();
    CHECK(flexAccountLinked() && flexAccountUsable(), "cuenta vinculada y utilizable");
  }
}
static void powerCycle(){
  // Se pierde la RAM de TODO; LittleFS (gFs) y la NVS siguen.
  flexAccountTestPowerCycle();
  flexCloudTestPowerCycle();
  gEv.clear();
}

// ============================== pruebas ==============================
static void testStates(){
  printf("-- estados: sin cuenta, sin Wi-Fi, conectado --\n");
  boot(false);
  CHECK(status().net == FCN_NO_ACCOUNT, "sin cuenta: FCN_NO_ACCOUNT");
  CHECK(flexCloudRequestList(FCL_VIEW_FOLDER, "root", nullptr), "pedir lista sin cuenta se acepta (y falla con motivo)");
  pump(5);
  FlexCloudListInfo l = listInfo();
  CHECK(l.state == FCL_LIST_ERROR && strstr(l.error, "Flex Account"), "lista: error que nombra Flex Account");
  CHECK(gNetLog.empty(), "sin cuenta no sale NADA a la red");

  boot(true);
  gNetWifi = false; gNetLog.clear();
  pump(5);
  CHECK(status().net == FCN_OFFLINE, "cuenta pero sin Wi-Fi: FCN_OFFLINE");
  flexCloudRequestList(FCL_VIEW_FOLDER, "root", nullptr);
  pump(3);
  CHECK(listInfo().state == FCL_LIST_ERROR, "lista sin Wi-Fi: error claro, no espera eterna");
  CHECK(gNetLog.empty(), "sin Wi-Fi no se intenta nada");

  gNetWifi = true;
  CHECK(pumpUntil([]{ return status().net == FCN_ONLINE; }, 50), "con Wi-Fi: ONLINE");
  FlexCloudStatus s = status();
  CHECK(s.quotaValid && s.quota.totalBytes == (5ull << 30), "cuota de 5 GB leida del servidor (no fija en el P4)");
  CHECK(!strcmp(s.address, "ana.p4@flex"), "direccion de la cuenta");
  // Plan mayor: el P4 no tiene limites propios.
  C.total = 100ull << 30; C.used = 92ull << 30;
  flexCloudSetActive(true);
  pump(3);
  s = status();
  CHECK(s.quota.totalBytes == (100ull << 30) && s.quota.state == FCL_Q_LOW, "plan de 100 GB al 92 %: espacio bajo");
  CHECK(s.quota.permille == 920, "porcentaje exacto (920 por mil)");
  // Con la nube a la vista se refresca cada minuto; fuera de ella, no.
  int me0 = C.me;
  pump(3100, 20);                                   // ~62 s
  CHECK(C.me - me0 >= 1 && C.me - me0 <= 2, "a la vista: cuota cada ~60 s (sin inundar)");
  flexCloudSetActive(false);
  me0 = C.me;
  pump(9000, 20);                                   // 3 min
  CHECK(C.me == me0, "fuera de la vista no se consulta la cuota");
  auditNetwork("estados");
}

static void testListAndOps(){
  printf("-- listas, paginas, migas y operaciones (nombres Unicode) --\n");
  boot();
  FFolder d; d.id = newId("fld"); d.name = "Fotos de A\xC3\xB1o Nuevo \xF0\x9F\x8E\x89"; C.folders[d.id] = d;
  FFolder sub; sub.id = newId("fld"); sub.name = "Ma\xC3\xB1" "ana"; sub.parent = d.id; C.folders[sub.id] = sub;
  for(int i = 0; i < 45; i++){ char n[32]; snprintf(n, sizeof(n), "foto_%02d.jpg", i); addCloudFile(n, pattern(1000 + i, i)); }
  flexCloudRequestList(FCL_VIEW_FOLDER, "root", nullptr);
  CHECK(listInfo().state == FCL_LIST_LOADING, "pedida: CARGANDO al instante (no bloquea)");
  CHECK(pumpUntil([]{ return listInfo().state == FCL_LIST_READY; }, 100), "lista lista");
  FlexCloudListInfo l = listInfo();
  CHECK(l.count == 40 && l.more, "primera pagina de 40 y hay mas");
  FclItem items[60];
  int n = flexCloudListCopy(items, 0, 60);
  CHECK(n == 40 && items[0].isFolder && !strcmp(items[0].name, d.name.c_str()), "la carpeta con tilde, enie y emoji llega intacta");
  CHECK(flexCloudRequestMore(), "pedir mas");
  CHECK(pumpUntil([]{ return listInfo().count == 46; }, 100), "segunda pagina: 46 en total");
  CHECK(!listInfo().more, "no hay mas");

  flexCloudRequestList(FCL_VIEW_FOLDER, sub.id.c_str(), nullptr);
  CHECK(pumpUntil([]{ return listInfo().state == FCL_LIST_READY; }, 100), "subcarpeta");
  l = listInfo();
  CHECK(l.nCrumbs == 2 && !strcmp(l.crumbs[0].id, d.id.c_str()) && !strcmp(l.crumbs[1].name, sub.name.c_str()), "migas: Fotos > Manana");

  // Pedir otra vista mientras carga la anterior: gana la ultima.
  flexCloudRequestList(FCL_VIEW_TRASH, nullptr, nullptr);
  flexCloudRequestList(FCL_VIEW_SEARCH, nullptr, "foto_4");
  CHECK(pumpUntil([]{ return listInfo().state == FCL_LIST_READY; }, 100), "busqueda");
  CHECK(listInfo().view == FCL_VIEW_SEARCH && listInfo().count == 5, "busqueda 'foto_4': 5 resultados (la ultima peticion gana)");

  // Crear carpeta con nombre Unicode
  const char* nm = "Viaje a Espa\xC3\xB1" "a \xE2\x80\x94 \xE6\x97\x85\xE8\xA1\x8C";
  uint32_t op = flexCloudMkdir("root", nm);
  CHECK(op != 0, "crear carpeta encolado");
  CHECK(waitEv(FCE_OP_DONE, op, 200), "crear carpeta: aviso");
  const FlexCloudEvent* e = findEv(FCE_OP_DONE, op);
  bool found = false; for(auto& kv : C.folders) if(kv.second.name == nm) found = true;
  CHECK(e && e->ok && found, "la carpeta existe en la nube con el nombre EXACTO");
  // Mismo nombre otra vez: conflicto con mensaje claro, sin reintentos.
  int before = (int)gNetLog.size();
  op = flexCloudMkdir("root", nm);
  CHECK(waitEv(FCE_OP_DONE, op, 200), "conflicto: aviso");
  e = findEv(FCE_OP_DONE, op);
  CHECK(e && !e->ok && !strcmp(e->code, "name_conflict"), "nombre repetido: name_conflict");
  CHECK((int)gNetLog.size() - before <= 2, "un error 4xx no se reintenta");

  // Renombrar, papelera, restaurar, borrar para siempre
  FFile& f = addCloudFile("borrame.txt", "hola");
  FclItem it = itemOf(f);
  op = flexCloudRename(&it, "renombrado \xC3\xA1\xC3\xA9\xC3\xAD.txt");
  CHECK(waitEv(FCE_OP_DONE, op, 200) && findEv(FCE_OP_DONE, op) && findEv(FCE_OP_DONE, op)->ok && C.files[it.id].name == "renombrado \xC3\xA1\xC3\xA9\xC3\xAD.txt", "renombrado con tildes");
  op = flexCloudTrash(&it);
  CHECK(waitEv(FCE_OP_DONE, op, 200) && C.files[it.id].trashed, "a la papelera");
  op = flexCloudRestore(&it);
  CHECK(waitEv(FCE_OP_DONE, op, 200) && !C.files[it.id].trashed, "restaurado");
  op = flexCloudDeleteForever(&it);
  CHECK(waitEv(FCE_OP_DONE, op, 200) && !C.files.count(it.id), "borrado para siempre");
  // Sin red: la operacion falla con motivo, no se queda colgada.
  gNetWifi = false; pump(2);
  op = flexCloudMkdir("root", "sin red");
  CHECK(waitEv(FCE_OP_DONE, op, 50) && evCode(FCE_OP_DONE, op) == "no_wifi", "sin Wi-Fi: no_wifi");
  gNetWifi = true;
  auditNetwork("operaciones");
}

static void testUploadBasic(){
  printf("-- subida: partes con huella, verificacion y 'liberar espacio' --\n");
  boot();
  std::string data = pattern(1000 * 1000 + 123, 7);             // 1 MB y pico: 4 partes de 256 KB
  putLocal("/Fotos/IMG_0001.jpg", data);
  uint32_t saves0 = flexCloudTestJournalSaves();
  uint32_t id = flexCloudUpload("/Fotos/IMG_0001.jpg", "Playa \xC3\x91" "and\xC3\xBA.jpg", "root", 0, FCL_JF_FREE_LOCAL);
  CHECK(id != 0, "subida encolada");
  CHECK(flexCloudUpload("/Fotos/IMG_0001.jpg", nullptr, "root", 0, FCL_JF_FREE_LOCAL) == id, "doble toque: la misma subida");
  CHECK(xfer(id).phase == FCX_QUEUED, "en cola");
  CHECK(waitEv(FCE_UPLOAD_DONE, id), "subida terminada");
  const FlexCloudEvent* e = findEv(FCE_UPLOAD_DONE, id);
  CHECK(e && e->ok && (e->flags & FCL_JF_FREE_LOCAL), "aviso con 'liberar espacio'");
  CHECK(e && e->sha256 == sha256hex(data), "el aviso trae la huella del ORIGINAL");
  CHECK(e && C.files.count(e->fileId) && C.files[e->fileId].data == data, "en la nube, byte a byte el original");
  CHECK(e && C.files[e->fileId].name == "Playa \xC3\x91" "and\xC3\xBA.jpg", "con el nombre pedido");
  CHECK(localFile("/Fotos/IMG_0001.jpg") == data, "el gestor NUNCA borra ni toca el original (lo decide la interfaz)");
  CHECK(C.partPuts == 4 && C.creates == 1 && C.completes == 1, "4 partes, 1 sesion, 1 cierre");
  CHECK(C.reserved == 0 && C.used == data.size(), "la reserva se convirtio en uso");
  uint32_t saves = flexCloudTestJournalSaves() - saves0;
  CHECK(saves <= 6, "el diario se escribe solo en los cambios de estado (no por parte)");
  CHECK(xfer(id).phase == FCX_DONE && xfer(id).done == data.size(), "lista de transferencias: terminada al 100 %");
  // Confirmado el aviso (la tarea lo apunta en su siguiente vuelta), tras un
  // reinicio ya no se repite. Si se apaga JUSTO antes, se repite una vez: la
  // interfaz trata los avisos como "al menos una vez" (ver testEventsAfterPowerLoss).
  pump(3);
  powerCycle(); pump(3); drain();
  CHECK(!findEv(FCE_UPLOAD_DONE, id), "aviso ya entregado: no se repite tras reiniciar");
  auditNetwork("subida");
}

static void testUploadResume(){
  printf("-- subida: reanudar tras APAGADO, corte de Wi-Fi y conexion cortada --\n");
  boot();
  std::string data = pattern(3 * 1024 * 1024 + 77, 11);        // 13 partes
  putLocal("/Fotos/largo.avi", data);
  uint32_t id = flexCloudUpload("/Fotos/largo.avi", nullptr, "root", 0, 0);
  CHECK(pumpUntil([]{ return C.partPuts >= 5; }, 2000), "van 5 partes");
  // APAGON a mitad (la RAM se pierde, la flash y la nube siguen).
  int puts0 = C.partPuts;
  powerCycle();
  CHECK(xfer(id).phase == FCX_QUEUED, "tras encender, la subida sigue en la lista");
  // Wi-Fi caido un rato: espera sin quemar intentos.
  gNetWifi = false; gNetLog.clear();
  pump(200);
  CHECK(gNetLog.empty(), "sin Wi-Fi no se intenta nada");
  CHECK(xfer(id).phase == FCX_WAITING_NET, "estado: esperando red");
  gNetWifi = true;
  // Conexion cortada en una parte: se reintenta esa parte.
  int cutOnce = 1;
  C.fault = [&](const NetRequest& rq, NetResponse& rs){
    if(cutOnce && rq.method == "PUT" && rq.url.find("/parts/9") != std::string::npos){ cutOnce = 0; rs.status = HTTPC_ERROR_CONNECTION_LOST; return true; }
    return false;
  };
  CHECK(waitEv(FCE_UPLOAD_DONE, id), "termina");
  const FlexCloudEvent* e = findEv(FCE_UPLOAD_DONE, id);
  CHECK(e && C.files[e->fileId].data == data, "integro byte a byte");
  CHECK(C.creates == 1, "UNA sola sesion: tras el apagado se recupero la misma");
  CHECK(C.statusGets >= 1, "al encender se pregunto a la nube que partes tenia");
  CHECK(C.partPuts - puts0 <= 13 - 5 + 1, "solo se enviaron las partes que faltaban (+1 reintento)");
  std::map<uint32_t, int> seen; for(uint32_t p : C.partLog) seen[p]++;
  int dup = 0; for(auto& kv : seen) if(kv.second > 1) dup++;
  CHECK(dup == 0, "ninguna parte se guardo dos veces en la nube");
  C.fault = nullptr;
  auditNetwork("reanudar");
}

static void testUploadFaults(){
  printf("-- subida: huella alterada, servidor caido, cuota, original cambiado --\n");
  boot();
  // Una parte llega alterada (422): se reintenta esa parte, nada mas.
  std::string data = pattern(600 * 1024, 3);
  putLocal("/a.bin", data);
  int bad = 2;
  C.fault = [&](const NetRequest& rq, NetResponse& rs){
    if(bad && rq.method == "PUT" && rq.url.find("/parts/2") != std::string::npos){ bad--; rs = jerr(422, "checksum_mismatch"); return true; }
    return false;
  };
  uint32_t id = flexCloudUpload("/a.bin", nullptr, "root", 0, 0);
  CHECK(waitEv(FCE_UPLOAD_DONE, id), "parte alterada dos veces: se reintenta y termina");
  CHECK(C.files.count(evFile(FCE_UPLOAD_DONE, id)) && C.files[evFile(FCE_UPLOAD_DONE, id)].data == data, "integro");

  // Servidor 503: espera creciente; nunca mas rapido que el backoff.
  std::vector<unsigned long> at;
  int fails = 4;
  C.fault = [&](const NetRequest& rq, NetResponse& rs){
    if(rq.method == "POST" && rq.url == API + "/uploads"){ at.push_back(rq.at); if(fails){ fails--; rs.status = 503; rs.body = "<html>bad gateway</html>"; return true; } }
    return false;
  };
  putLocal("/b.bin", pattern(10000, 4));
  id = flexCloudUpload("/b.bin", nullptr, "root", 0, 0);
  CHECK(waitEv(FCE_UPLOAD_DONE, id, 100000), "tras 4 errores 503 termina");
  bool spaced = at.size() == 5;
  for(size_t i = 1; i < at.size(); i++) if(at[i] - at[i - 1] < 1900) spaced = false;
  CHECK(spaced, "los reintentos se espacian (>= 2 s, creciendo): sin bucles agresivos");
  CHECK(at.size() == 5 && at[4] - at[3] > at[1] - at[0], "la espera crece");

  // Servidor que no se recupera: se rinde y se puede reintentar a mano.
  C.fault = [&](const NetRequest& rq, NetResponse& rs){
    if(rq.method == "POST" && rq.url == API + "/uploads"){ rs = jerr(500, "internal_error"); return true; }
    return false;
  };
  putLocal("/c.bin", pattern(5000, 5));
  id = flexCloudUpload("/c.bin", nullptr, "root", 0, 0);
  CHECK(waitEv(FCE_UPLOAD_FAILED, id, 400000), "servidor roto: queda FALLIDA (no reintenta para siempre)");
  CHECK(xfer(id).phase == FCX_FAILED && xfer(id).error[0], "con un motivo legible");
  C.fault = nullptr;
  CHECK(flexCloudRetry(id), "reintentar");
  CHECK(waitEv(FCE_UPLOAD_DONE, id), "reintentada: termina");

  // Sin cuota: fallo inmediato, sin reintentos, el original intacto.
  C.total = C.used + 1000;
  std::string big = pattern(50 * 1024, 6);
  putLocal("/d.bin", big);
  int posts = 0;
  C.fault = [&](const NetRequest& rq, NetResponse&){ if(rq.method == "POST" && rq.url == API + "/uploads") posts++; return false; };
  id = flexCloudUpload("/d.bin", nullptr, "root", 0, FCL_JF_FREE_LOCAL);
  CHECK(waitEv(FCE_UPLOAD_FAILED, id), "sin espacio en la nube: fallida");
  const FlexCloudEvent* e = findEv(FCE_UPLOAD_FAILED, id);
  CHECK(e && !strcmp(e->code, "quota_exceeded") && strstr(e->text, "espacio"), "quota_exceeded con mensaje claro");
  CHECK(posts == 1, "cuota llena: no se insiste");
  CHECK(localFile("/d.bin") == big, "el original sigue intacto");
  C.total = 5ull << 30;
  C.fault = nullptr;

  // El original cambia mientras la subida estaba a medias (apagado entre medias).
  std::string v1 = pattern(900 * 1024, 8);
  putLocal("/e.bin", v1);
  id = flexCloudUpload("/e.bin", nullptr, "root", 0, FCL_JF_FREE_LOCAL);
  CHECK(pumpUntil([]{ return C.partPuts >= 1 && !C.uploads.empty(); }, 2000), "empezo");
  int putsBefore = C.partPuts;
  CHECK(pumpUntil([&]{ return C.partPuts >= putsBefore + 1; }, 2000), "una parte mas");
  powerCycle();
  std::string v2 = v1; v2[700 * 1024] ^= 1;                   // mismo tamano, otro contenido
  putLocal("/e.bin", v2);
  CHECK(waitEv(FCE_UPLOAD_FAILED, id), "original cambiado: no se mezcla");
  CHECK(evCode(FCE_UPLOAD_FAILED, id) == "local_changed", "motivo: local_changed");
  CHECK(!findEv(FCE_UPLOAD_DONE, id), "y NUNCA se ofrece liberar espacio");

  // La nube confirma pero con OTRA huella: no se da por buena.
  std::string g = pattern(70000, 9);
  putLocal("/f.bin", g);
  C.fault = [&](const NetRequest& rq, NetResponse& rs){
    if(rq.method == "POST" && rq.url.find("/complete") != std::string::npos){
      rs = serveCloud(rq);
      size_t p = rs.body.find("\"sha256\":\"", rs.body.find("\"file\""));
      if(p != std::string::npos) rs.body[p + 11] = rs.body[p + 11] == 'a' ? 'b' : 'a';
      return true;
    }
    return false;
  };
  id = flexCloudUpload("/f.bin", nullptr, "root", 0, FCL_JF_FREE_LOCAL);
  CHECK(waitEv(FCE_UPLOAD_FAILED, id), "huella distinta en la nube: fallida");
  CHECK(!findEv(FCE_UPLOAD_DONE, id) && localFile("/f.bin") == g, "no se ofrece borrar lo local");
  C.fault = nullptr;

  // El archivo local desaparece: fallo claro.
  putLocal("/g.bin", "x");
  id = flexCloudUpload("/g.bin", nullptr, "root", 0, 0);
  flexFsDelete("/g.bin");
  CHECK(waitEv(FCE_UPLOAD_FAILED, id) && evCode(FCE_UPLOAD_FAILED, id) == "local_missing", "original borrado: local_missing");
  CHECK(flexCloudUpload("/no/existe.bin", nullptr, "root", 0, 0) == 0, "ruta inexistente: ni se encola");
  auditNetwork("fallos de subida");
}

static void testAuthDuringUpload(){
  printf("-- subida: credencial rechazada a mitad --\n");
  boot();
  std::string data = pattern(800 * 1024, 12);
  putLocal("/h.bin", data);
  uint32_t id = flexCloudUpload("/h.bin", nullptr, "root", 0, 0);
  CHECK(pumpUntil([]{ return C.partPuts >= 1; }, 2000), "empezo");
  // La nube rechaza la credencial: Flex Account lo comprueba; el trabajo espera.
  std::string saved = C.tokenHash;
  C.tokenHash = "revocado";
  CHECK(pumpUntil([]{ return status().net == FCN_AUTH; }, 3000), "estado: hay que volver a vincular");
  size_t a0 = gNetLog.size();
  CHECK(pumpUntil([]{ return flexAccountLinkState() == FLEX_LINK_AUTH_REQUIRED; }, 5000, 20), "Flex Account lo comprueba: AUTH_REQUIRED (no 'desvinculado')");
  CHECK(gNetLog.size() - a0 <= 4, "mientras Flex Account lo comprueba, la nube no insiste");
  CHECK(flexAccountLinked(), "la credencial NO se borra sola");
  drain();
  CHECK(!findEv(FCE_UPLOAD_FAILED, id), "la subida no se pierde");
  size_t n0 = gNetLog.size();
  pump(3000, 100);                                     // 5 minutos
  CHECK(gNetLog.size() - n0 <= 3, "credencial rechazada: no se insiste con ella");
  // (Revincular = nueva credencial; aqui el servidor la vuelve a aceptar.)
  C.tokenHash = saved;
  flexAccountRequestValidation();
  CHECK(waitEv(FCE_UPLOAD_DONE, id, 60000), "al validarse de nuevo, la subida sigue y termina");
  CHECK(C.files[evFile(FCE_UPLOAD_DONE, id)].data == data, "integra");
  auditNetwork("credencial");
}

static void testCancel(){
  printf("-- cancelar: la nube suelta la reserva (tambien sin red) --\n");
  boot();
  putLocal("/i.bin", pattern(2 * 1024 * 1024, 13));
  uint32_t id = flexCloudUpload("/i.bin", nullptr, "root", 0, 0);
  CHECK(pumpUntil([]{ return C.partPuts >= 2; }, 2000), "a mitad");
  CHECK(C.reserved == 2 * 1024 * 1024, "la nube tiene la reserva");
  CHECK(flexCloudCancel(id), "cancelar");
  CHECK(xfer(id).phase == FCX_CANCELLED, "cancelada al instante en la interfaz");
  CHECK(pumpUntil([]{ return C.aborts == 1; }, 200), "DELETE /uploads/:id enviado");
  CHECK(C.reserved == 0, "reserva liberada en la nube");
  pump(50);
  drain();
  CHECK(!findEv(FCE_UPLOAD_DONE, id) && !findEv(FCE_UPLOAD_FAILED, id), "cancelada: sin avisos de exito/fallo");
  flexCloudClearFinished();
  CHECK(xfer(id).phase == 255, "limpiar la quita de la lista");

  // Cancelar SIN red: la reserva se suelta cuando vuelve.
  putLocal("/j.bin", pattern(1024 * 1024, 14));
  id = flexCloudUpload("/j.bin", nullptr, "root", 0, 0);
  CHECK(pumpUntil([]{ return C.partPuts >= 3; }, 2000), "a mitad (2)");
  gNetWifi = false; pump(2);
  flexCloudCancel(id);
  flexCloudClearFinished();
  CHECK(xfer(id).phase == 255, "oculta en la lista aunque falte avisar a la nube");
  pump(2);                                              // la tarea apunta la cancelacion
  powerCycle();                                         // y ademas se apaga
  pump(100);
  CHECK(C.reserved == 1024 * 1024 && C.aborts == 1, "sin red: aun no se pudo avisar");
  gNetWifi = true;
  CHECK(pumpUntil([]{ return C.aborts == 2; }, 500), "con red: se avisa (tras reiniciar incluso)");
  CHECK(C.reserved == 0, "reserva liberada");
  int ab = C.aborts; pump(300);
  CHECK(C.aborts == ab, "se avisa una vez, no en bucle");
  auditNetwork("cancelar");
}

static void testDownload(){
  printf("-- descarga: verificada, reanudable con Range, sin mezclar versiones --\n");
  boot();
  std::string data = pattern(700 * 1024 + 5, 21);
  FFile& f = addCloudFile("Concierto \xE2\x99\xAB.avi", data);
  FclItem it = itemOf(f);
  uint32_t id = flexCloudDownload(&it, FCL_JF_TO_LIBRARY);
  CHECK(id != 0 && flexCloudDownload(&it, 0) == id, "encolada (doble toque = una)");
  CHECK(waitEv(FCE_DOWNLOAD_DONE, id), "descargada");
  const FlexCloudEvent* e = findEv(FCE_DOWNLOAD_DONE, id);
  CHECK(e && localFile(e->localPath) == data, "el temporal es el archivo exacto");
  CHECK(e && (e->flags & FCL_JF_TO_LIBRARY) && !strcmp(e->name, "Concierto \xE2\x99\xAB.avi"), "aviso con nombre y destino");
  CHECK(e && !strcmp(e->sha256, f.sha.c_str()), "huella verificada");

  // Corte a mitad: la siguiente peticion pide SOLO lo que falta.
  std::string d2 = pattern(900 * 1024, 22);
  FFile& f2 = addCloudFile("dos.bin", d2);
  FclItem it2 = itemOf(f2);
  int cut = 1;
  C.fault = [&](const NetRequest& rq, NetResponse& rs){
    if(cut && rq.url.find("/download/") != std::string::npos){ cut = 0; rs = serveCloud(rq); rs.cutAt = 300 * 1024 + 17; return true; }
    return false;
  };
  C.ranges.clear();
  id = flexCloudDownload(&it2, 0);
  CHECK(waitEv(FCE_DOWNLOAD_DONE, id), "termina tras el corte");
  CHECK(localFile(evPath(FCE_DOWNLOAD_DONE, id)) == d2, "integra");
  CHECK(C.ranges.size() == 1 && C.ranges[0] == "bytes=307217-", "reanudo con Range desde el byte exacto");
  C.fault = nullptr;

  // Apagado a mitad de una descarga.
  std::string d3 = pattern(1024 * 1024, 23);
  FFile& f3 = addCloudFile("tres.bin", d3);
  FclItem it3 = itemOf(f3);
  C.fault = [&](const NetRequest& rq, NetResponse& rs){
    if(rq.url.find("/download/") != std::string::npos && !rq.headers.count("range")){ rs = serveCloud(rq); rs.cutAt = 512 * 1024; return true; }
    return false;
  };
  C.ranges.clear();
  id = flexCloudDownload(&it3, 0);
  CHECK(pumpUntil([&]{ return xfer(id).phase == FCX_RETRYING || xfer(id).phase == FCX_WAITING_NET; }, 2000), "cortada");
  powerCycle();
  CHECK(waitEv(FCE_DOWNLOAD_DONE, id), "tras encender, termina");
  CHECK(localFile(evPath(FCE_DOWNLOAD_DONE, id)) == d3, "integra tras el apagado");
  CHECK(C.ranges.size() == 1 && C.ranges[0] == "bytes=524288-", "y no volvio a bajar lo que ya tenia");
  C.fault = nullptr;

  // Bytes alterados por el camino: no se da por buena y no queda basura.
  FFile& f4 = addCloudFile("cuatro.bin", pattern(200 * 1024, 24));
  FclItem it4 = itemOf(f4);
  C.corruptDownload = true;
  id = flexCloudDownload(&it4, 0);
  CHECK(waitEv(FCE_DOWNLOAD_FAILED, id), "alterada: fallida");
  CHECK(evCode(FCE_DOWNLOAD_FAILED, id) == "checksum_mismatch", "motivo: checksum_mismatch");
  bool leftover = false; for(auto& kv : gFs) if(kv.first.find("/System/Cloud/dl/") == 0 && !kv.second.dir) leftover = leftover || kv.first.find(std::to_string(id) + ".part") != std::string::npos;
  CHECK(!leftover, "el temporal alterado se borro");
  C.corruptDownload = false;

  // No cabe en el P4: se dice antes de bajar nada.
  FFile& big = addGeneratedFile("enorme.avi", 64ull * 1024 * 1024);
  FclItem itb = itemOf(big);
  int dl0 = C.downloads;
  id = flexCloudDownload(&itb, 0);
  CHECK(waitEv(FCE_DOWNLOAD_FAILED, id) && evCode(FCE_DOWNLOAD_FAILED, id) == "no_space_local", "64 MB no caben en LittleFS: no_space_local");
  CHECK(C.downloads == dl0, "sin gastar red");

  // Cancelar una descarga borra su temporal.
  FFile& f5 = addCloudFile("cinco.bin", pattern(1024 * 1024, 25));
  FclItem it5 = itemOf(f5);
  C.fault = [&](const NetRequest& rq, NetResponse& rs){
    if(rq.url.find("/download/") != std::string::npos){ rs = serveCloud(rq); rs.cutAt = 400 * 1024; return true; }
    return false;
  };
  id = flexCloudDownload(&it5, 0);
  CHECK(pumpUntil([&]{ return xfer(id).done >= 256 * 1024; }, 2000), "a medias");
  flexCloudCancel(id);
  pump(20);
  char tp[64]; snprintf(tp, sizeof(tp), "/System/Cloud/dl/%lu.part", (unsigned long)id);
  CHECK(!flexFsExists(tp), "cancelada: temporal borrado");
  C.fault = nullptr;
  auditNetwork("descarga");
}

static bool encodeJpeg(int w, int h, uint16_t color, std::string& out){
  std::vector<uint16_t> px((size_t)w * h, color);
  FlexJeCfg cfg = { w, h, 85, FLEXJE_SUB_420, FLEXJE_IN_RGB565 };
  out.clear();
  return flexJpegEncodeMem(&cfg, px.data(), (size_t)w * 2, [](void* c, const uint8_t* d, size_t n){ ((std::string*)c)->append((const char*)d, n); return true; }, &out, nullptr, nullptr) == FLEXJE_OK;
}

static void testThumbsAndView(){
  printf("-- miniaturas (objeto aparte) y foto para el visor --\n");
  boot();
  flexCloudSetActive(true);
  std::string jpg;
  CHECK(encodeJpeg(640, 480, 0xF800, jpg), "jpeg de prueba");
  FFile& f = addCloudFile("roja.jpg", pattern(300 * 1024, 31));
  f.thumb = jpg;
  pump(5);
  flexCloudWantThumb(f.id.c_str());
  uint32_t g0 = flexCloudThumbGen();
  CHECK(pumpUntil([&]{ return flexCloudThumbGen() != g0; }, 200), "miniatura procesada");
  struct Px { int side = 0; uint16_t center = 0, corner = 0; } got;
  bool drawn = flexCloudThumbDraw(f.id.c_str(), [](const uint16_t* px, int side, void* u){
    Px* p = (Px*)u; p->side = side; p->center = px[side * (side / 2) + side / 2]; p->corner = px[0]; }, &got);
  CHECK(drawn && got.side == FLEX_CLOUD_THUMB_SIDE, "dibujada a 132x132");
  CHECK((got.center & 0xF800) >= 0xE000 && (got.center & 0x07E0) < 0x0100, "es roja (centro)");
  CHECK((got.corner & 0xF800) >= 0xE000, "es roja (esquina: recorte cuadrado)");
  int t0 = C.thumbs;
  flexCloudWantThumb(f.id.c_str()); pump(20);
  CHECK(C.thumbs == t0, "ya en memoria: no se vuelve a pedir");
  FFile& nothumb = addCloudFile("sin.jpg", "x");
  flexCloudWantThumb(nothumb.id.c_str());
  g0 = flexCloudThumbGen();
  CHECK(pumpUntil([&]{ return flexCloudThumbGen() != g0; }, 200), "sin miniatura: procesada");
  CHECK(!flexCloudThumbDraw(nothumb.id.c_str(), [](const uint16_t*, int, void*){}, nullptr), "sin miniatura: icono (no se dibuja nada)");
  t0 = C.thumbs; pump(500);
  CHECK(C.thumbs == t0, "un 404 no se pide en bucle");
  // Un monton de miniaturas: memoria acotada (LRU de 24).
  size_t ps0 = gNetPsNow;
  for(int i = 0; i < 60; i++){ FFile& x = addCloudFile("m" + std::to_string(i) + ".jpg", "z"); x.thumb = jpg; flexCloudWantThumb(x.id.c_str()); pump(3); }
  pump(400);
  CHECK(gNetPsNow - ps0 <= (size_t)FLEX_CLOUD_THUMBS * FLEX_CLOUD_THUMB_SIDE * FLEX_CLOUD_THUMB_SIDE * 2 + 4096, "miniaturas: como mucho 24 en memoria");
  size_t freed = flexCloudShed();
  CHECK(freed > 0 && gNetPsNow < ps0 + 4096, "flexCloudShed() las suelta");

  // Foto para el visor: un solo hueco, verificada.
  FclItem it = itemOf(f);
  uint32_t op = flexCloudFetchForView(&it);
  CHECK(waitEv(FCE_VIEW_READY, op), "foto lista para el visor");
  const FlexCloudEvent* e = findEv(FCE_VIEW_READY, op);
  CHECK(e && localFile(e->localPath) == f.data, "copia exacta (calidad original, sin recomprimir)");
  FFile& huge = addGeneratedFile("panorama.jpg", 9u * 1024 * 1024);
  FclItem ih = itemOf(huge);
  op = flexCloudFetchForView(&ih);
  CHECK(waitEv(FCE_VIEW_FAILED, op) && evCode(FCE_VIEW_FAILED, op) == "file_too_large", "foto de 9 MB: se dice, no se intenta");
  auditNetwork("miniaturas");
}

static void testStreaming(){
  printf("-- streaming de un video de 160 MB con memoria acotada --\n");
  boot();
  FFile& v = addGeneratedFile("pelicula.avi", 160ull * 1024 * 1024);
  FclItem it = itemOf(v);
  size_t ps0 = gNetPsNow;
  gNetPsPeak = gNetPsNow;
  CHECK(flexCloudStreamOpen(&it), "abrir");
  CHECK(flexCloudStreamSize() == 160u * 1024 * 1024, "tamano");
  uint8_t buf[4096];
  CHECK(flexCloudStreamRead(0, buf, sizeof(buf)) == -1, "aun no hay datos: -1 (NO bloquea)");
  char err[96];
  CHECK(flexCloudStreamState(err, sizeof(err)) == FCS_OPENING, "estado: abriendo");
  auto st = [](int n){ for(int i = 0; i < n; i++){ flexCloudTestStreamStep(); netstubAdvance(2); } };
  st(4);
  CHECK(flexCloudStreamRead(0, buf, sizeof(buf)) == (int)sizeof(buf), "primer bloque llego");
  bool same = true; for(int i = 0; i < (int)sizeof(buf); i++) if(buf[i] != genByte(i)) same = false;
  CHECK(same, "bytes exactos");
  CHECK(flexCloudStreamState(err, sizeof(err)) == FCS_STREAMING, "estado: reproduciendo");
  // Leer como un reproductor: avanzar 20 MB.
  uint64_t bad = 0, waits = 0;
  for(uint32_t off = 0; off < 20u * 1024 * 1024; off += sizeof(buf)){
    int r;
    while((r = flexCloudStreamRead(off, buf, sizeof(buf))) < 0){ st(1); waits++; if(waits > 200000) break; }
    for(int i = 0; i < r; i++) if(buf[i] != genByte(off + i)) bad++;
  }
  CHECK(bad == 0, "20 MB leidos en orden: identicos");
  // Saltar a otro punto: rango nuevo, sin bajar lo de en medio.
  C.ranges.clear();
  flexCloudStreamSeek(120u * 1024 * 1024);
  int r;
  int spins = 0;
  while((r = flexCloudStreamRead(120u * 1024 * 1024, buf, sizeof(buf))) < 0 && spins++ < 100) st(1);
  CHECK(r == (int)sizeof(buf) && buf[0] == genByte(120ull * 1024 * 1024), "salto a 120 MB");
  CHECK(!C.ranges.empty() && C.ranges[0] == "bytes=125829120-", "el salto pidio un rango, no el archivo");
  // El indice del AVI (al final) fijado mientras se reproduce otra zona.
  flexCloudStreamPin(160u * 1024 * 1024 - 100000, 100000);
  spins = 0;
  while(!flexCloudStreamReady(160u * 1024 * 1024 - 100000, 100000) && spins++ < 100) st(1);
  CHECK(flexCloudStreamReady(160u * 1024 * 1024 - 100000, 100000), "el indice del final, disponible");
  flexCloudStreamSeek(40u * 1024 * 1024);
  for(int i = 0; i < 400; i++) st(1);
  CHECK(flexCloudStreamReady(160u * 1024 * 1024 - 100000, 100000), "el indice sigue fijado aunque se reproduzca otra zona");
  size_t peak = gNetPsPeak - ps0;
  printf("   pico de PSRAM del streaming: %zu KB (archivo de 160 MB)\n", peak / 1024);
  CHECK(peak <= (size_t)FLEX_CLOUD_STREAM_BLOCK * FLEX_CLOUD_STREAM_BLOCKS + 64 * 1024, "memoria ACOTADA: la arena de 3 MB y nada mas");
  // Wi-Fi se cae: espera, y vuelve solo.
  gNetWifi = false;
  flexCloudStreamSeek(80u * 1024 * 1024);
  st(5);
  CHECK(flexCloudStreamState(err, sizeof(err)) == FCS_WAITING_NET, "sin Wi-Fi: esperando red (no error)");
  gNetWifi = true;
  spins = 0;
  while((r = flexCloudStreamRead(80u * 1024 * 1024, buf, sizeof(buf))) < 0 && spins++ < 2000) st(1);
  CHECK(r == (int)sizeof(buf), "con red vuelve solo");
  flexCloudStreamClose();
  st(2);
  CHECK(flexCloudStreamState(err, sizeof(err)) == FCS_CLOSED, "cerrado");
  size_t freed = flexCloudShed();
  CHECK(freed >= (size_t)FLEX_CLOUD_STREAM_BLOCK * FLEX_CLOUD_STREAM_BLOCKS, "cerrado y sin uso: la arena se puede soltar");
  auditNetwork("streaming");
}

static void testBigUploadBounded(){
  printf("-- subida de 160 MB: partes de tamano adaptado y memoria acotada --\n");
  boot();
  gFsTotalBytes = 200u * 1024 * 1024;                     // particion grande SOLO para esta prueba
  std::string data(160u * 1024 * 1024, '\0');
  for(size_t i = 0; i < data.size(); i++) data[i] = (char)genByte(i * 7 + 3);
  putLocal("/big.avi", data);
  size_t ps0 = gNetPsNow; gNetPsPeak = gNetPsNow;
  uint32_t id = flexCloudUpload("/big.avi", nullptr, "root", 0, 0);
  // Pasos de huella acotados: la tarea no se queda horas en una vuelta.
  CHECK(pumpUntil([&]{ return C.creates == 1; }, 400), "huellas por tramos y sesion creada");
  CHECK(C.uploads.begin()->second.chunk == 320u * 1024 && C.uploads.begin()->second.total == 512, "160 MB: 512 partes de 320 KB (el P4 sigue como mucho 512)");
  // Apagon a la mitad
  CHECK(pumpUntil([]{ return C.partPuts >= 250; }, 5000), "mitad");
  powerCycle();
  CHECK(waitEv(FCE_UPLOAD_DONE, id, 200000), "terminada tras el apagon");
  const FlexCloudEvent* e = findEv(FCE_UPLOAD_DONE, id);
  CHECK(e && C.files[e->fileId].size == data.size() && C.files[e->fileId].sha == sha256hex(data), "160 MB integros (SHA-256)");
  CHECK(C.creates == 1 && C.partPuts <= 512 + 1, "una sesion y sin repetir partes");
  size_t peak = gNetPsPeak - ps0;
  printf("   pico de PSRAM de la subida: %zu KB (archivo de 160 MB)\n", peak / 1024);
  CHECK(peak < 256u * 1024, "memoria ACOTADA: < 256 KB para subir 160 MB");
  gFs.erase("/big.avi");
  for(auto& kv : C.files) kv.second.data.clear();
}

static void testEventsAfterPowerLoss(){
  printf("-- el aviso de 'terminado' sobrevive a un apagado --\n");
  boot();
  putLocal("/k.jpg", pattern(10000, 41));
  uint32_t id = flexCloudUpload("/k.jpg", nullptr, "root", 7, FCL_JF_FREE_LOCAL);
  CHECK(pumpUntil([&]{ return xfer(id).phase == FCX_DONE; }, 2000), "terminada (sin leer el aviso)");
  powerCycle();                                          // la interfaz no llego a procesarlo
  pump(2); drain();
  const FlexCloudEvent* e = findEv(FCE_UPLOAD_DONE, id);
  CHECK(e && (e->flags & FCL_JF_FREE_LOCAL) && e->mlId == 7 && !strcmp(e->localPath, "/k.jpg"), "se repite tras encender, con todo lo necesario");
  pump(3);                                               // guarda "entregado"
  powerCycle(); pump(2); gEv.clear(); drain();
  CHECK(!findEv(FCE_UPLOAD_DONE, id), "entregado una vez: no se repite mas");
  // El diario lleno de trabajos con aviso pendiente no los pisa.
  for(int i = 0; i < FCL_JOBS_MAX + 2; i++){ char p[32]; snprintf(p, sizeof(p), "/l%d.bin", i); putLocal(p, pattern(100, i)); flexCloudUpload(p, nullptr, "root", 0, 0); }
  CHECK(pumpUntil([]{ return status().activeXfers == 0; }, 4000), "todas hechas");
  int dones = 0; drain(); for(auto& ev : gEv) if(ev.kind == FCE_UPLOAD_DONE) dones++;
  CHECK(dones >= FCL_JOBS_MAX, "cada subida avisa");
}

static void testJournalCorrupt(){
  printf("-- diario danado: se descarta, no se interpreta --\n");
  boot();
  putLocal("/m.bin", pattern(5000, 51));
  flexCloudUpload("/m.bin", nullptr, "root", 0, 0);
  pump(1);
  auto it = gFs.find("/System/Cloud/jobs.bin");
  CHECK(it != gFs.end(), "diario guardado al encolar");
  if(it != gFs.end() && it->second.data.size() > 20) it->second.data[20] ^= 0xFF;
  powerCycle();
  pump(50);
  FlexCloudXfer x[FLEX_CLOUD_XFERS];
  CHECK(flexCloudXfers(x, FLEX_CLOUD_XFERS) == 0, "diario danado: lista vacia, sin basura ni cuelgues");
}

int main(){
  printf("=== FlexOS · Flex Cloud Manager (P4) contra un Flex Cloud simulado ===\n");
  gKey.generate();
  flexAccountTestSetKey(gKey.pub.data());
  netstubReset();
  fsStubReset();
  flexFsBegin();
  flexAccountBegin();
  flexCloudBegin();
  CHECK(gNetTaskCreates >= 2, "Flex Cloud crea sus tareas propias (red y streaming)");
  testStates();
  testListAndOps();
  testUploadBasic();
  testUploadResume();
  testUploadFaults();
  testAuthDuringUpload();
  testCancel();
  testDownload();
  testThumbsAndView();
  testStreaming();
  testEventsAfterPowerLoss();
  testJournalCorrupt();
  testBigUploadBounded();
  gKey.free_();
  printf("=== %d comprobaciones, %d fallos ===\n", gChecks, gFails);
  return gFails ? 1 : 0;
}
