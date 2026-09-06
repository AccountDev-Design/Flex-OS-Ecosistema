// Estado global del sistema de archivos en memoria (ver fsstub/FS.h).
#include "FS.h"

bool     gFsFailRename = false;
int      gFsFailWriteAfter = -1;
int      gFsWriteCount = 0;
uint32_t gFsTotalBytes = 11136u * 1024u;   // la particion real del P4

std::map<std::string, FsNode> gFs;
FlexFsStub LittleFS;

void fsStubReset(){
  gFs.clear();
  gFsFailRename = false;
  gFsFailWriteAfter = -1;
  gFsWriteCount = 0;
  gFsTotalBytes = 11136u * 1024u;
  FsNode root; root.dir = true;
  gFs["/"] = root;
}
