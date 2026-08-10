// #############################################################
//  Dobles de los modulos hermanos, para el enlazado de la prueba
//  ------------------------------------------------------------
//  FlexOS_Ultra.ino llama a tres modulos que viven en sus propios
//  .cpp: OTA, sistema de archivos y navegador. Esas unidades ya
//  tienen sus propias pruebas de host (test_app, test_bridge,
//  test_net, test_browser, test_jpeg); aqui solo hace falta que el
//  enlazador encuentre los simbolos para poder EJECUTAR las
//  comprobaciones del reloj que viven en el propio sketch.
//
//  Cada doble devuelve el valor "no disponible" del modulo real,
//  que es el mismo camino que toma la placa si el subsistema no
//  arranca. Nada de esto se compila para la placa.
// #############################################################
#include "Arduino.h"
#include "FlexOS_OTA.h"
#include "FlexOS_FS.h"
#include "FlexOS_Browser.h"

// ---- OTA ----
void        flexOtaBegin(){}
bool        flexOtaBusy(){ return false; }
bool        flexOtaOwnsScreen(){ return false; }
bool        flexOtaOverlayActive(){ return false; }
void        flexOtaRender(){}
void        flexOtaOpenSettings(){}
bool        flexOtaHandleTouch(FlexOtaTouch*){ return false; }
const char* flexOtaStatusText(){ return "Sin comprobar"; }

// ---- Sistema de archivos ----
bool        flexFsBegin(){ return false; }
bool        flexFsReady(){ return false; }
const char* flexFsError(){ return "no montado"; }
uint32_t    flexFsUsedBytes(){ return 0; }
uint32_t    flexFsTotalBytes(){ return 0; }
uint32_t    flexFsDirSize(const char*){ return 0; }
uint32_t    flexFsCatSize(int){ return 0; }
int         flexFsList(const char*, FlexFsEntry*, int){ return 0; }
int         flexFsLargest(FlexFsBig*, int){ return 0; }
int         flexFsReadText(const char*, char* out, size_t n){ if(n) out[0] = 0; return -1; }
bool        flexFsWriteText(const char*, const char*){ return false; }
int         flexFsReadBin(const char*, void*, size_t){ return -1; }
bool        flexFsWriteBin(const char*, const void*, size_t){ return false; }
bool        flexFsDelete(const char*){ return false; }
bool        flexFsRename(const char*, const char*){ return false; }
bool        flexFsTrash(const char*){ return false; }
bool        flexFsRestore(const char*){ return false; }
bool        flexFsEmptyTrash(){ return false; }
bool        flexFsTrashOrigin(const char*, char* out, size_t n){ if(n) out[0] = 0; return false; }
void        flexFsFmtSize(uint32_t, char* out, size_t n){ if(n) snprintf(out, n, "0 B"); }
void        flexFsStem(const char*, char* out, size_t n){ if(n) out[0] = 0; }
bool        flexFsNewName(const char*, const char*, const char*, char* out, size_t n){ if(n) out[0] = 0; return false; }
bool        flexPaintCreate(const char*, uint16_t, uint16_t){ return false; }
bool        flexPaintAppend(const char*, uint16_t, uint8_t, const int16_t*, uint16_t){ return false; }
bool        flexPaintHeader(const char*, FlexPaintHdr*){ return false; }
bool        flexPaintReplay(const char*, float, int, int, FlexPaintSegCb, void*){ return false; }
bool        flexPaintUndo(const char*){ return false; }
bool        flexPaintClear(const char*){ return false; }

// ---- Navegador ----
void flexBrVersionGuard_v4_copia_los_4_ficheros_del_navegador(void){}
void flexBrowserBegin(){}
void flexBrowserEnter(){}
void flexBrowserTick(){}
void flexBrowserExit(){}
bool flexBrowserWantsClose(){ return false; }
bool flexBrowserHandleSystemBack(){ return false; }
void flexBrowserForceRepaint(){}
bool flexBrowserRepaintedFull(){ return false; }
void flexBrowserCancelDrag(){}
bool flexBrowserKeyboardOpen(){ return false; }
const char* flexBrowserEditText(){ return ""; }
const char* flexBrowserEditLabel(){ return ""; }
void flexBrowserKeyText(const char*){}
void flexBrowserKeyBackspace(){}
void flexBrowserKeyEnter(){}
void flexBrowserKeyCancel(){}
