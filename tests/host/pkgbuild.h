#pragma once
// #############################################################
//  CONSTRUCTOR DE .flexpkg PARA LAS PRUEBAS DE HOST
//  ------------------------------------------------------------
//  Fabrica paquetes FLXP v1 REALES (JSON canónico, índice con SHA-256
//  por archivo, hash global, firma ECDSA P-256 y, opcionalmente, el
//  trailer de grant FLXG firmado por una clave "de Flex Store").
//
//  POR QUE AQUI Y NO CON EL SDK. Para que `make` en tests/host no
//  dependa de Node: la batería tiene que poder ejecutarse con sólo g++
//  y OpenSSL. La compatibilidad SDK <-> firmware se comprueba aparte,
//  en sdk/test/sdk.test.js, que empaqueta con el SDK y valida con el
//  MISMO núcleo del firmware a través de build/flexpkgcheck.
//
//  Las claves son EFIMERAS: se generan en cada ejecución y no se
//  escriben en disco. Aquí no hay ninguna clave privada versionada.
// #############################################################
#pragma push_macro("min")
#pragma push_macro("max")
#undef min
#undef max
#include <string>
#include <vector>
#include <map>
#pragma pop_macro("min")
#pragma pop_macro("max")

#include <stdint.h>
#include <string.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/evp.h>
#include <openssl/obj_mac.h>

namespace pkgb {

typedef std::vector<uint8_t> Bytes;

inline Bytes sha256(const uint8_t* p, size_t n) {
  Bytes out(32);
  unsigned int len = 0;
  EVP_MD_CTX* c = EVP_MD_CTX_new();
  EVP_DigestInit_ex(c, EVP_sha256(), nullptr);
  EVP_DigestUpdate(c, p, n);
  EVP_DigestFinal_ex(c, out.data(), &len);
  EVP_MD_CTX_free(c);
  return out;
}
inline Bytes sha256(const Bytes& b) { return sha256(b.data(), b.size()); }

inline std::string hex(const Bytes& b) {
  static const char* h = "0123456789abcdef";
  std::string s;
  for (uint8_t c : b) { s += h[c >> 4]; s += h[c & 15]; }
  return s;
}

// Par de claves P-256 efímero.
struct Key {
  EC_KEY* ec = nullptr;
  Bytes pub;                       // 65 bytes SEC1 sin comprimir

  void generate() {
    ec = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
    EC_KEY_generate_key(ec);
    pub.assign(65, 0);
    unsigned char* buf = nullptr;
    size_t n = EC_KEY_key2buf(ec, POINT_CONVERSION_UNCOMPRESSED, &buf, nullptr);
    if (n == 65 && buf) memcpy(pub.data(), buf, 65);
    if (buf) OPENSSL_free(buf);
  }
  void free_() { if (ec) { EC_KEY_free(ec); ec = nullptr; } }

  // Firma un hash de 32 bytes y devuelve r||s en 64 bytes.
  Bytes sign(const Bytes& hash) const {
    Bytes out(64, 0);
    ECDSA_SIG* sig = ECDSA_do_sign(hash.data(), 32, ec);
    const BIGNUM* r = nullptr; const BIGNUM* s = nullptr;
    ECDSA_SIG_get0(sig, &r, &s);
    BN_bn2binpad(r, out.data(), 32);
    BN_bn2binpad(s, out.data() + 32, 32);
    ECDSA_SIG_free(sig);
    return out;
  }
  std::string fingerprint() const { return hex(sha256(pub)); }
};

inline void put16(Bytes& b, size_t off, uint16_t v) { b[off] = v & 0xFF; b[off + 1] = (v >> 8) & 0xFF; }
inline void put32(Bytes& b, size_t off, uint32_t v) {
  b[off] = v & 0xFF; b[off + 1] = (v >> 8) & 0xFF; b[off + 2] = (v >> 16) & 0xFF; b[off + 3] = (v >> 24) & 0xFF;
}
inline void put64(Bytes& b, size_t off, uint64_t v) {
  put32(b, off, (uint32_t)(v & 0xFFFFFFFFull));
  put32(b, off + 4, (uint32_t)(v >> 32));
}

// ---- Grant FLXG v1 -----------------------------------------------------
struct GrantSpec {
  std::string packageId;
  std::string versionName;
  uint32_t versionCode = 1;
  Bytes packageSha256;             // 32 bytes crudos
  std::string developerFingerprint; // 64 hex
  uint32_t mask = 0;
  uint16_t countOverride = 0xFFFF; // 0xFFFF = calcular el popcount correcto
  uint64_t notBefore = 0, notAfter = 0;
  uint16_t purpose = 1;
  uint16_t formatVersion = 1;
};

inline Bytes buildGrant(const GrantSpec& g, const Key& storeKey) {
  Bytes b(296, 0);
  b[0] = 'F'; b[1] = 'L'; b[2] = 'X'; b[3] = 'G';
  put16(b, 4, g.formatVersion);
  put16(b, 6, 0);
  put16(b, 8, g.purpose);
  uint16_t count = g.countOverride;
  if (count == 0xFFFF) { count = 0; for (uint32_t m = g.mask; m; m >>= 1) count += (m & 1); }
  put16(b, 10, count);
  put32(b, 12, g.versionCode);
  put64(b, 16, g.notBefore);
  put64(b, 24, g.notAfter);
  for (size_t i = 0; i < 32 && i < g.packageSha256.size(); i++) b[32 + i] = g.packageSha256[i];
  for (size_t i = 0; i < 32; i++) {
    auto hv = [&](char c) -> int {
      return (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : 0;
    };
    if (g.developerFingerprint.size() == 64)
      b[64 + i] = (uint8_t)((hv(g.developerFingerprint[i * 2]) << 4) | hv(g.developerFingerprint[i * 2 + 1]));
  }
  memcpy(b.data() + 96, g.packageId.c_str(), g.packageId.size() < 96 ? g.packageId.size() : 96);
  memcpy(b.data() + 192, g.versionName.c_str(), g.versionName.size() < 32 ? g.versionName.size() : 32);
  put32(b, 224, g.mask);
  put32(b, 228, 0);
  Bytes signedPart(b.begin(), b.begin() + 232);
  Bytes sig = storeKey.sign(sha256(signedPart));
  memcpy(b.data() + 232, sig.data(), 64);
  return b;
}

// ---- Paquete FLXP v1 ---------------------------------------------------
struct FileSpec { std::string path; Bytes data; };

struct PkgSpec {
  std::string id = "com.flexos.demo";
  std::string name = "Demo";
  std::string versionName = "1.0.0";
  uint32_t versionCode = 1;
  std::string minFlexOS = "1.0.0";
  std::string runtime = "flex-ui-1";
  std::string entry = "app/main.json";
  std::string summary = "";
  std::string category = "";
  uint32_t memoryKB = 128;
  uint32_t storageKB = 0;
  uint32_t instrPerTick = 0, usPerTick = 0, drawPerFrame = 0;
  std::vector<std::string> permissions;
  std::vector<std::string> systemPermissions;
  std::vector<FileSpec> files;
};

// JSON canónico: compacto, en el orden en que cJSON lo va a reimprimir.
inline std::string manifestJson(const PkgSpec& s, const std::string& devFp) {
  std::string j = "{";
  j += "\"schema\":1";
  j += ",\"id\":\"" + s.id + "\"";
  j += ",\"name\":\"" + s.name + "\"";
  j += ",\"version\":{\"name\":\"" + s.versionName + "\",\"code\":" + std::to_string(s.versionCode) + "}";
  j += ",\"minFlexOS\":\"" + s.minFlexOS + "\"";
  j += ",\"runtime\":\"" + s.runtime + "\"";
  j += ",\"entry\":\"" + s.entry + "\"";
  j += ",\"developerKeySha256\":\"" + devFp + "\"";
  if (!s.summary.empty()) j += ",\"summary\":\"" + s.summary + "\"";
  if (!s.category.empty()) j += ",\"category\":\"" + s.category + "\"";
  j += ",\"limits\":{\"memoryKB\":" + std::to_string(s.memoryKB) +
       ",\"storageKB\":" + std::to_string(s.storageKB);
  if (s.instrPerTick) j += ",\"instrPerTick\":" + std::to_string(s.instrPerTick);
  if (s.usPerTick) j += ",\"usPerTick\":" + std::to_string(s.usPerTick);
  if (s.drawPerFrame) j += ",\"drawPerFrame\":" + std::to_string(s.drawPerFrame);
  j += "}";
  j += ",\"permissions\":[";
  for (size_t i = 0; i < s.permissions.size(); i++) {
    if (i) j += ",";
    j += "\"" + s.permissions[i] + "\"";
  }
  j += "]";
  if (!s.systemPermissions.empty()) {
    j += ",\"systemPermissions\":[";
    for (size_t i = 0; i < s.systemPermissions.size(); i++) {
      if (i) j += ",";
      j += "\"" + s.systemPermissions[i] + "\"";
    }
    j += "]";
  }
  j += "}";
  return j;
}

struct Built {
  Bytes bytes;
  std::string manifest;
  std::string index;
  Bytes signedHash;
  std::string devFingerprint;
};

// Opciones para romper el paquete a propósito, una cosa cada vez.
struct Damage {
  bool badSignature = false;     // se firma con otra clave
  bool badGlobalHash = false;    // el hash de la cabecera no cuadra
  bool badFileHash = false;      // el SHA-256 del primer archivo no cuadra
  bool badReserved = false;      // bytes 60..63 distintos de cero
  bool wrongDevFingerprint = false;
  bool sizeMismatch = false;     // se añade un byte al final
  bool nonCanonicalJson = false; // manifest con un espacio
  int  grantLenOverride = -1;    // longitud declarada del grant distinta
};

inline Built buildPackage(const PkgSpec& s, const Key& dev, const Bytes* grant = nullptr,
                          const Damage& dmg = Damage()) {
  Built out;
  out.devFingerprint = dmg.wrongDevFingerprint
      ? std::string(64, 'a')
      : dev.fingerprint();

  std::string manifest = manifestJson(s, out.devFingerprint);
  if (dmg.nonCanonicalJson) manifest = "{ " + manifest.substr(1);

  // Payload e índice
  Bytes payload;
  std::string index = "[";
  for (size_t i = 0; i < s.files.size(); i++) {
    const FileSpec& f = s.files[i];
    uint32_t off = (uint32_t)payload.size();
    Bytes h = sha256(f.data);
    if (dmg.badFileHash && i == 0) h[0] ^= 0xFF;
    if (i) index += ",";
    index += "{\"path\":\"" + f.path + "\",\"offset\":" + std::to_string(off) +
             ",\"size\":" + std::to_string(f.data.size()) +
             ",\"sha256\":\"" + hex(h) + "\"}";
    payload.insert(payload.end(), f.data.begin(), f.data.end());
  }
  index += "]";

  Bytes toHash;
  toHash.insert(toHash.end(), manifest.begin(), manifest.end());
  toHash.insert(toHash.end(), index.begin(), index.end());
  toHash.insert(toHash.end(), payload.begin(), payload.end());
  Bytes signedHash = sha256(toHash);

  Bytes headerHash = signedHash;
  if (dmg.badGlobalHash) headerHash[0] ^= 0xFF;

  Key other;
  Bytes sig;
  if (dmg.badSignature) { other.generate(); sig = other.sign(signedHash); other.free_(); }
  else sig = dev.sign(signedHash);

  uint32_t grantLen = grant ? (uint32_t)grant->size() : 0u;
  if (dmg.grantLenOverride >= 0) grantLen = (uint32_t)dmg.grantLenOverride;

  Bytes b(64, 0);
  b[0] = 'F'; b[1] = 'L'; b[2] = 'X'; b[3] = 'P';
  put16(b, 4, 1);
  put16(b, 6, 0);
  put32(b, 8, (uint32_t)manifest.size());
  put32(b, 12, (uint32_t)index.size());
  put32(b, 16, (uint32_t)payload.size());
  put16(b, 20, 65);
  put16(b, 22, 64);
  for (int i = 0; i < 32; i++) b[24 + i] = headerHash[i];
  put32(b, 56, grantLen);
  if (dmg.badReserved) b[62] = 1;

  b.insert(b.end(), manifest.begin(), manifest.end());
  b.insert(b.end(), index.begin(), index.end());
  b.insert(b.end(), payload.begin(), payload.end());
  b.insert(b.end(), dev.pub.begin(), dev.pub.end());
  if (dmg.badSignature && false) {}
  b.insert(b.end(), sig.begin(), sig.end());
  if (grant) b.insert(b.end(), grant->begin(), grant->end());
  if (dmg.sizeMismatch) b.push_back(0);

  out.bytes = b;
  out.manifest = manifest;
  out.index = index;
  out.signedHash = signedHash;
  return out;
}

// Un entrypoint flex-ui-1 mínimo y válido.
inline Bytes uiEntry() {
  const char* j = "{\"schema\":1,\"startScreen\":\"home\",\"screens\":[{\"id\":\"home\","
                  "\"title\":\"Hola\",\"components\":[{\"type\":\"text\",\"id\":\"t\","
                  "\"text\":\"Hola Flex\",\"x\":20,\"y\":120,\"width\":400}]}]}";
  return Bytes(j, j + strlen(j));
}

}  // namespace pkgb
#pragma GCC diagnostic pop
