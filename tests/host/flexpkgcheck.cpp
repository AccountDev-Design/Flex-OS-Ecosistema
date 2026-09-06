// #############################################################
//  flexpkgcheck  ·  valida un .flexpkg con el NUCLEO DEL FIRMWARE
//  ------------------------------------------------------------
//  Compila FlexOS_PkgCore.cpp -- el mismo archivo que corre en el
//  ESP32-P4 -- y lo aplica a un paquete del disco. Sirve para que el
//  SDK no pueda divergir del firmware en silencio: sdk/test/sdk.test.js
//  empaqueta con las herramientas y lo verifica CON ESTO.
//
//  Uso:
//    flexpkgcheck <paquete.flexpkg> [--json] [--fw <version>]
//
//  Codigo de salida: 0 si el paquete es valido, 1 si no.
// #############################################################
#include "../../FlexOS_Ultra/FlexOS_PkgCore.h"
#include "../../FlexOS_Ultra/FlexOS_AppGrant.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

struct FileBuf { unsigned char* data; unsigned long size; };

static bool bufRead(void* user, uint32_t off, void* dst, uint32_t n){
  FileBuf* f = (FileBuf*)user;
  if((unsigned long long)off + n > (unsigned long long)f->size) return false;
  memcpy(dst, f->data + off, n);
  return true;
}

static const char* runtimeName(uint8_t r){
  return r == FLEXPKG_RT_APP1 ? "flex-app-v1" : "flex-ui-1";
}

int main(int argc, char** argv){
  const char* path = nullptr;
  const char* fw = "1.0.0";
  bool asJson = false;
  for(int i = 1; i < argc; i++){
    if(!strcmp(argv[i], "--json")) asJson = true;
    else if(!strcmp(argv[i], "--fw") && i + 1 < argc) fw = argv[++i];
    else path = argv[i];
  }
  if(!path){
    std::fprintf(stderr, "uso: flexpkgcheck <paquete.flexpkg> [--json] [--fw <version>]\n");
    return 2;
  }

  FILE* f = fopen(path, "rb");
  if(!f){ std::fprintf(stderr, "no se puede abrir %s\n", path); return 2; }
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  if(n <= 0 || n > 32L * 1024 * 1024){ fclose(f); std::fprintf(stderr, "tamano invalido\n"); return 2; }
  unsigned char* data = (unsigned char*)malloc((size_t)n);
  if(!data || fread(data, 1, (size_t)n, f) != (size_t)n){ fclose(f); free(data); return 2; }
  fclose(f);

  FileBuf fb{ data, (unsigned long)n };
  FlexPkgReader rd{ bufRead, (uint32_t)n, &fb };
  FlexPkgCoreOut out;
  char err[160] = "";
  FlexPkgErrorCode rc = flexPkgCoreRun(&rd, nullptr, &out, nullptr, nullptr, err, sizeof(err), fw);
  free(data);

  if(asJson){
    std::printf("{\"ok\":%s,\"code\":%d,\"error\":\"%s\"", rc == FLEXPKG_OK ? "true" : "false", (int)rc, err);
    if(rc == FLEXPKG_OK){
      std::printf(",\"id\":\"%s\",\"name\":\"%s\",\"version\":\"%s\",\"versionCode\":%u"
                  ",\"runtime\":\"%s\",\"entry\":\"%s\",\"files\":%u,\"payloadBytes\":%u"
                  ",\"packageSha256\":\"%s\",\"developerKeySha256\":\"%s\""
                  ",\"grantBytes\":%u,\"systemPermissions\":%u,\"permissions\":%u"
                  ",\"memoryKB\":%u,\"storageKB\":%u",
                  out.info.id, out.info.name, out.info.versionName, (unsigned)out.info.versionCode,
                  runtimeName(out.info.runtime), out.info.entry, (unsigned)out.info.fileCount,
                  (unsigned)out.info.payloadBytes, out.info.packageSha256, out.info.developerKeySha256,
                  (unsigned)out.grantLen, (unsigned)out.info.systemPermissions,
                  (unsigned)out.info.permissions, (unsigned)out.info.memoryKB,
                  (unsigned)out.info.storageKB);
    }
    std::printf("}\n");
  } else if(rc == FLEXPKG_OK){
    std::printf("OK  %s %s (codigo %u)\n", out.info.id, out.info.versionName, (unsigned)out.info.versionCode);
    std::printf("    runtime   %s\n", runtimeName(out.info.runtime));
    std::printf("    entry     %s\n", out.info.entry);
    std::printf("    archivos  %u  payload %u bytes\n", (unsigned)out.info.fileCount, (unsigned)out.info.payloadBytes);
    std::printf("    sha256    %s\n", out.info.packageSha256);
    std::printf("    dev       %s\n", out.info.developerKeySha256);
    std::printf("    permisos  manifest=0x%04X  sistema=0x%08X  grant=%u bytes\n",
                (unsigned)out.info.permissions, (unsigned)out.info.systemPermissions, (unsigned)out.grantLen);
  } else {
    std::printf("RECHAZADO (%d): %s\n", (int)rc, err);
  }
  return rc == FLEXPKG_OK ? 0 : 1;
}
