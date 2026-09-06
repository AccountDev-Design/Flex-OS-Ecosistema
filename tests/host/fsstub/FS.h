#pragma once
// #############################################################
//  Doble de FS.h / LittleFS PARA LAS PRUEBAS DE HOST
//  ------------------------------------------------------------
//  Un sistema de archivos EN MEMORIA con el subconjunto exacto de la
//  API de Arduino que usa FlexOS_Package.cpp: open/read/write/seek,
//  exists, mkdir, rmdir, remove, rename, openNextFile, totalBytes y
//  usedBytes.
//
//  POR QUE EXISTE. La transaccion de instalacion (stage -> active,
//  reversion a .old, limpieza de temporales) es codigo de riesgo real:
//  un fallo ahi deja al usuario sin su app o con dos versiones
//  mezcladas. Sin este doble no habia forma de ejercitarlo fuera de la
//  placa, y por tanto no habia forma de probarlo.
//
//  ADEMAS PERMITE FALLAR A PROPOSITO. gFsFailRename / gFsFailWriteAfter
//  hacen que una operacion concreta falle en el momento elegido, que es
//  como se comprueba la REVERSION de verdad y no "de palabra".
//
//  Nada de esto se compila para la placa.
// #############################################################
// Arduino.h define min/max como MACROS y eso rompe <string>, <map> y
// <vector> (que declaran metodos con esos nombres). Se apartan mientras se
// incluye la biblioteca estandar y se restauran justo despues, para que el
// codigo del firmware que los use siga viendolos igual.
#pragma push_macro("min")
#pragma push_macro("max")
#undef min
#undef max
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <string>
#include <map>
#include <vector>
#include <memory>
#pragma pop_macro("min")
#pragma pop_macro("max")

#define SeekSet 0
#define SeekCur 1
#define SeekEnd 2

// ---- Interruptores de fallo, para las pruebas ---------------------------
extern bool     gFsFailRename;       // todo rename() falla
extern int      gFsFailWriteAfter;   // <0 desactivado; si no, falla la escritura N
extern int      gFsWriteCount;
extern uint32_t gFsTotalBytes;

struct FsNode {
  bool dir = false;
  std::vector<uint8_t> data;
};

// Mapa ruta -> nodo. Las rutas son absolutas y sin barra final.
extern std::map<std::string, FsNode> gFs;

class File {
 public:
  File() {}
  File(const std::string& path, bool write) : path_(path) {
    auto it = gFs.find(path);
    if (write) {
      // Crear/truncar. El padre tiene que existir y ser un directorio.
      std::string parent = path.substr(0, path.find_last_of('/'));
      if (!parent.empty()) {
        auto p = gFs.find(parent);
        if (p == gFs.end() || !p->second.dir) return;
      }
      if (it != gFs.end() && it->second.dir) return;
      FsNode n; n.dir = false;
      gFs[path] = n;
      open_ = true;
      return;
    }
    if (it == gFs.end()) return;
    open_ = true;
    dir_ = it->second.dir;
  }

  explicit operator bool() const { return open_; }
  bool operator!() const { return !open_; }

  bool isDirectory() const { return dir_; }
  const char* name() const { return path_.c_str(); }

  size_t size() const {
    auto it = gFs.find(path_);
    return (it == gFs.end() || it->second.dir) ? 0 : it->second.data.size();
  }

  size_t read(uint8_t* dst, size_t n) {
    auto it = gFs.find(path_);
    if (it == gFs.end() || it->second.dir) return 0;
    const auto& d = it->second.data;
    if (pos_ >= d.size()) return 0;
    size_t avail = d.size() - pos_;
    if (n > avail) n = avail;
    memcpy(dst, d.data() + pos_, n);
    pos_ += n;
    return n;
  }

  size_t write(const uint8_t* src, size_t n) {
    auto it = gFs.find(path_);
    if (it == gFs.end() || it->second.dir) return 0;
    if (gFsFailWriteAfter >= 0 && gFsWriteCount >= gFsFailWriteAfter) return 0;
    gFsWriteCount++;
    auto& d = it->second.data;
    if (pos_ + n > d.size()) d.resize(pos_ + n);
    memcpy(d.data() + pos_, src, n);
    pos_ += n;
    return n;
  }

  bool seek(uint32_t off, int /*mode*/ = SeekSet) {
    auto it = gFs.find(path_);
    if (it == gFs.end()) return false;
    if (off > it->second.data.size()) return false;
    pos_ = off;
    return true;
  }

  void close() { open_ = false; }

  // Recorrido de directorio: SOLO los hijos directos.
  //
  // El cursor es el NOMBRE del ultimo hijo devuelto, no un indice. Es
  // deliberado: removeTree() borra entradas MIENTRAS recorre, y con un
  // indice el borrado desplazaria la lista y se saltaria archivos. Es el
  // mismo contrato que da el iterador de LittleFS en la placa.
  File openNextFile() {
    if (!dir_) return File();
    std::string prefix = (path_ == "/") ? "/" : path_ + "/";
    auto it = cursor_.empty() ? gFs.lower_bound(prefix) : gFs.upper_bound(cursor_);
    for (; it != gFs.end(); ++it) {
      if (it->first.compare(0, prefix.size(), prefix) != 0) break;
      std::string rest = it->first.substr(prefix.size());
      if (rest.find('/') != std::string::npos) continue;   // nieto, no hijo
      cursor_ = it->first;
      return File(it->first, false);
    }
    return File();
  }

 private:
  std::string path_;
  std::string cursor_;
  bool open_ = false;
  bool dir_ = false;
  size_t pos_ = 0;
};

class FlexFsStub {
 public:
  File open(const char* path, const char* mode = "r") {
    return File(path, mode && mode[0] == 'w');
  }
  bool exists(const char* path) { return gFs.count(path) != 0; }
  bool mkdir(const char* path) {
    if (gFs.count(path)) return gFs[path].dir;
    std::string p(path);
    std::string parent = p.substr(0, p.find_last_of('/'));
    if (!parent.empty() && (!gFs.count(parent) || !gFs[parent].dir)) return false;
    FsNode n; n.dir = true;
    gFs[p] = n;
    return true;
  }
  bool rmdir(const char* path) {
    auto it = gFs.find(path);
    if (it == gFs.end() || !it->second.dir) return false;
    std::string prefix = std::string(path) + "/";
    for (const auto& kv : gFs)
      if (kv.first.compare(0, prefix.size(), prefix) == 0) return false;   // no vacio
    gFs.erase(it);
    return true;
  }
  bool remove(const char* path) {
    auto it = gFs.find(path);
    if (it == gFs.end() || it->second.dir) return false;
    gFs.erase(it);
    return true;
  }
  // rename de un ARBOL entero, como hace LittleFS con un directorio.
  bool rename(const char* from, const char* to) {
    if (gFsFailRename) return false;
    auto it = gFs.find(from);
    if (it == gFs.end()) return false;
    if (gFs.count(to)) return false;
    std::string f(from), t(to);
    std::vector<std::pair<std::string, FsNode>> moved;
    std::string prefix = f + "/";
    for (auto& kv : gFs) {
      if (kv.first == f) moved.push_back({t, kv.second});
      else if (kv.first.compare(0, prefix.size(), prefix) == 0)
        moved.push_back({t + kv.first.substr(f.size()), kv.second});
    }
    for (auto it2 = gFs.begin(); it2 != gFs.end();) {
      if (it2->first == f || it2->first.compare(0, prefix.size(), prefix) == 0) it2 = gFs.erase(it2);
      else ++it2;
    }
    for (auto& m : moved) gFs[m.first] = m.second;
    return true;
  }
  uint32_t totalBytes() { return gFsTotalBytes; }
  uint32_t usedBytes() {
    uint32_t n = 0;
    for (const auto& kv : gFs) n += (uint32_t)kv.second.data.size();
    return n;
  }
};

extern FlexFsStub LittleFS;

// Reinicia el sistema de archivos y los interruptores de fallo.
void fsStubReset();
