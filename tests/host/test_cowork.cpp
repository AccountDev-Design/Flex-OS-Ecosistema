// #############################################################
//  MOTOR DE TRABAJOS COWORK  ·  pruebas de host
//  ------------------------------------------------------------
//  El motor recibe el reloj POR PARAMETRO, asi que aqui se mueve
//  a mano: las esperas progresivas, los tiempos maximos y la
//  caducidad se comprueban de verdad sin dormir ni un
//  milisegundo. Esa es la razon de que el modulo no llame nunca
//  a millis().
//
//  Lo que mas importa de esta bateria:
//    · que un resultado NO se de por bueno si el documento de
//      origen cambio mientras se trabajaba;
//    · que cancelar y terminar BORREN el snapshot (puede venir
//      de Flex Vault);
//    · que el fichero de historial, TRUNCADO en cualquier punto,
//      no produzca una lectura fuera de rango.
// #############################################################

#include "FlexOS_Cowork.h"

#include <stdio.h>
#include <string.h>

static int gFails = 0, gChecks = 0;
static void chk(bool c, const char* what){
  gChecks++;
  if(!c){ printf("  FALLO: %s\n", what); gFails++; }
}

static CoworkJob gSlots[CW_MAX_JOBS];

int main(){
  printf("=== FlexOS - motor de trabajos Cowork ===\n");
  coworkAttachStorage(gSlots, CW_MAX_JOBS);
  chk(coworkReady(), "el motor arranca con su almacen");
  chk(coworkCount() == 0, "y empieza vacio");

  // ---- huella del documento ----
  chk(coworkHash("hola", 4) == coworkHash("hola", 4), "la huella es estable");
  chk(coworkHash("hola", 4) != coworkHash("hOla", 4), "y distingue un cambio");
  chk(coworkHash("", 0) != 0, "nunca sale 0 (0 significa 'sin documento')");

  // ---- encolar ----
  const char* doc = "una nota del usuario";
  uint32_t h = coworkHash(doc, strlen(doc));
  uint32_t id1 = coworkSubmit(CW_KIND_SUMMARY, "Resumir nota", doc, strlen(doc),
                              "/Notas/a.txt", h, 0, 1000);
  chk(id1 != 0, "encola un trabajo");
  chk(coworkActiveCount() == 1, "cuenta como activo");
  chk(coworkFind(id1) != NULL, "se encuentra por id");
  chk(coworkFind(id1)->state == CW_QUEUED, "nace en cola");
  chk(coworkFind(id1)->progress == 0xFF, "sin progreso calculable de entrada");
  chk(!strcmp(coworkFind(id1)->in, doc), "el snapshot es una COPIA propia");

  // Entrada mas grande que el limite: se RECHAZA, no se trunca.
  { static char big[CW_IN_MAX + 64];
    memset(big, 'x', sizeof(big) - 1); big[sizeof(big) - 1] = 0;
    chk(coworkSubmit(CW_KIND_SUMMARY, "grande", big, sizeof(big) - 1, NULL, 0, 0, 1000) == 0,
        "una entrada demasiado grande se rechaza (no se trunca)"); }

  // ---- una sola operacion de red a la vez ----
  chk(coworkNextForNetwork(1000) == coworkFind(id1), "sale a la red el mas antiguo");
  coworkBeginWork(id1, 1000);
  uint32_t id2 = coworkSubmit(CW_KIND_TRANSLATE, "Traducir", "hola", 4, NULL, 0, 0, 1100);
  chk(id2 != 0, "se puede encolar otro mientras tanto");
  chk(coworkNextForNetwork(1200) == NULL, "pero NO sale un segundo a la red");

  // ---- progreso ----
  coworkProgress(id1, 40);
  chk(coworkFind(id1)->progress == 40, "el progreso se guarda");
  coworkProgress(id1, 200);
  chk(coworkFind(id1)->progress == 100, "un progreso fuera de rango se acota");

  // ---- EL CASO CLAVE: el documento cambio mientras se trabajaba ----
  const char* edited = "una nota del usuario, ya editada";
  coworkFinish(id1, "resumen", 7, coworkHash(edited, strlen(edited)), false, 2000);
  { CoworkJob* j = coworkFind(id1);
    chk(j->state == CW_DONE,     "termina");
    chk(j->srcChanged,           "detecta que el origen cambio");
    chk(j->needsConfirm,         "y por eso pide confirmacion");
    chk(!j->seen,                "nace sin ver");
    chk(j->inLen == 0 && j->in[0] == 0, "el snapshot se BORRA al terminar"); }

  // Mismo documento sin tocar: no pide confirmacion.
  { uint32_t id = coworkSubmit(CW_KIND_CORRECT, "Corregir", doc, strlen(doc), "/Notas/b.txt", h, 0, 3000);
    coworkBeginWork(id, 3000);
    coworkFinish(id, "ok", 2, h, false, 3100);
    CoworkJob* j = coworkFind(id);
    chk(!j->srcChanged,   "sin cambios, el origen coincide");
    chk(!j->needsConfirm, "y no hace falta confirmar");
    coworkDelete(id); }

  // Sin documento de origen (srcHash 0): nunca se marca como cambiado.
  { uint32_t id = coworkSubmit(CW_KIND_ANALYZE, "Analizar", "x", 1, NULL, 0, 0, 3200);
    coworkBeginWork(id, 3200);
    coworkFinish(id, "y", 1, 12345, false, 3300);
    chk(!coworkFind(id)->srcChanged, "sin origen no puede haber cambio");
    coworkDelete(id); }

  // ---- reintentos con espera progresiva ----
  coworkBeginWork(id2, 4000);
  coworkFail(id2, CW_ERR_NONET, "sin conexion", 4000);
  { CoworkJob* j = coworkFind(id2);
    chk(j->state == CW_QUEUED,         "un fallo de red reintenta");
    chk(j->retryAtMs == 4000 + 2000,   "espera 2 s la primera vez");
    chk(j->startMs == 0,               "y el cronometro del presupuesto se reinicia"); }
  chk(coworkNextForNetwork(5000) == NULL,          "durante la espera no sale");
  chk(coworkNextForNetwork(6000) == coworkFind(id2), "cumplida la espera, si sale");
  coworkBeginWork(id2, 6000);
  coworkFail(id2, CW_ERR_NONET, "sin conexion", 6000);
  chk(coworkFind(id2)->retryAtMs == 6000 + 6000, "la segunda espera es 6 s (x3)");
  coworkBeginWork(id2, 13000);
  coworkFail(id2, CW_ERR_NONET, "sin conexion", 13000);
  chk(coworkFind(id2)->state == CW_ERROR, "tras 3 intentos se rinde");
  chk(coworkFind(id2)->inLen == 0,        "y suelta el snapshot");

  // Lo que NO es reintentable no gasta radio repitiendose.
  { uint32_t id = coworkSubmit(CW_KIND_SUMMARY, "S", "z", 1, NULL, 0, 0, 14000);
    coworkFail(id, CW_ERR_NOCFG, "sin configurar", 14000);
    chk(coworkFind(id)->state == CW_ERROR, "'sin configurar' no reintenta");
    coworkDelete(id);
    uint32_t id3 = coworkSubmit(CW_KIND_SUMMARY, "S", "z", 1, NULL, 0, 0, 14000);
    coworkFail(id3, CW_ERR_DENIED, "sin permiso", 14000);
    chk(coworkFind(id3)->state == CW_ERROR, "'permiso denegado' tampoco reintenta");
    coworkDelete(id3); }

  // ---- cancelacion ----
  { uint32_t id = coworkSubmit(CW_KIND_TONE, "Tono", "secreto", 7, NULL, 0, 0, 15000);
    chk(coworkCancel(id), "se cancela una tarea viva");
    CoworkJob* j = coworkFind(id);
    chk(j->state == CW_CANCELLED, "queda cancelada");
    chk(j->inLen == 0 && j->in[0] == 0, "cancelar BORRA el snapshot");
    chk(!coworkCancel(id), "no se cancela dos veces");
    coworkDelete(id); }
  chk(!coworkCancel(id1), "un trabajo TERMINADO ya no se cancela");
  chk(!coworkCancel(999999), "un id inexistente no revienta");

  // ---- tiempo maximo ----
  { uint32_t id = coworkSubmit(CW_KIND_ANALYZE, "A", "q", 1, NULL, 0, 1000, 20000);
    coworkBeginWork(id, 20000);
    coworkPump(20900);
    chk(coworkFind(id)->state == CW_WORKING, "dentro del presupuesto sigue");
    coworkPump(21500);
    chk(coworkFind(id)->state == CW_QUEUED, "pasado el presupuesto corta y reintenta");
    // El presupuesto se cuenta desde que EMPIEZA, no desde que se encola:
    // horas en cola sin red no son tiempo trabajado.
    coworkPump(999999);
    chk(coworkFind(id)->state == CW_QUEUED, "en cola no se le agota el tiempo");
    coworkDelete(id); }

  // ---- pausa por carga / bateria ----
  { uint32_t id = coworkSubmit(CW_KIND_FIND, "F", "w", 1, NULL, 0, 0, 30000);
    coworkPauseAll(CW_PAUSE_LOAD);
    chk(coworkFind(id)->state == CW_PAUSED, "se pausa por carga");
    chk(coworkAnyPaused(CW_PAUSE_LOAD),     "y se sabe por que");
    coworkResumeAll(CW_PAUSE_BATTERY);
    chk(coworkFind(id)->state == CW_PAUSED, "otro motivo NO lo reanuda");
    coworkPause(id, CW_PAUSE_USER);
    coworkResumeAll(CW_PAUSE_LOAD);
    chk(coworkFind(id)->state == CW_PAUSED, "lo que paro el usuario sigue pausado");
    chk(coworkResume(id), "se reanuda a mano");
    chk(coworkFind(id)->state == CW_QUEUED, "y vuelve a la cola");
    chk(coworkNextForNetwork(30000) != NULL, "un pausado no bloquea la salida a la red");
    coworkDelete(id); }

  // ---- ver / eliminar ----
  chk(coworkUnseenCount() >= 1, "hay resultados sin ver");
  coworkMarkSeen(id1);
  chk(coworkFind(id1)->seen, "se marca como visto");

  // ---- persistencia ----
  uint8_t blob[4096];
  size_t nb = coworkExport(blob, sizeof(blob));
  chk(nb > 12, "exporta el historial");
  { // Un trabajo ACTIVO no sobrevive a un reinicio: su servidor ya no le
    // espera y revivirlo solo produciria un error diferido.
    uint32_t act = coworkSubmit(CW_KIND_SUMMARY, "activo", "a", 1, NULL, 0, 0, 40000);
    size_t n2 = coworkExport(blob, sizeof(blob));
    coworkAttachStorage(gSlots, CW_MAX_JOBS);
    chk(coworkImport(blob, n2), "importa");
    chk(coworkFind(id1) != NULL, "el TERMINADO vuelve");
    chk(coworkFind(act) == NULL, "el ACTIVO no vuelve");
    chk(coworkFind(id1)->seen,   "y conserva que ya se habia visto"); }

  // Un blob TRUNCADO en cualquier punto se rechaza sin salirse de rango.
  // Este fichero lo escribimos nosotros, pero puede llegar cortado por un
  // corte de corriente a mitad de escritura.
  for(size_t k = 0; k <= nb; k++){
    coworkAttachStorage(gSlots, CW_MAX_JOBS);
    coworkImport(blob, k);          // no debe reventar (lo vigila ASan)
  }
  chk(true, "ningun truncamiento del historial se sale de rango");

  // Cabecera manipulada.
  coworkAttachStorage(gSlots, CW_MAX_JOBS);
  { uint8_t bad[32]; memcpy(bad, blob, 32); bad[0] ^= 0xFF;
    chk(!coworkImport(bad, 32), "un magic distinto se rechaza"); }
  { uint8_t bad[32]; memcpy(bad, blob, 32); bad[4] = 99;
    chk(!coworkImport(bad, 32), "una version distinta se rechaza"); }
  chk(!coworkImport(NULL, 100), "NULL se rechaza");

  // Los ids NO se reciclan: una notificacion en vuelo que apunte a un
  // trabajo borrado tiene que fallar al buscarlo, no encontrar OTRO.
  coworkAttachStorage(gSlots, CW_MAX_JOBS);
  { uint32_t a = coworkSubmit(CW_KIND_SUMMARY, "a", "1", 1, NULL, 0, 0, 50000);
    coworkDelete(a);
    uint32_t b = coworkSubmit(CW_KIND_SUMMARY, "b", "2", 1, NULL, 0, 0, 50000);
    chk(a != b, "un id borrado no se reutiliza");
    coworkDelete(b); }

  // ---- cola llena ----
  coworkAttachStorage(gSlots, CW_MAX_JOBS);
  { int ok = 0;
    for(int i = 0; i < CW_MAX_JOBS + 4; i++)
      if(coworkSubmit(CW_KIND_SUMMARY, "x", "y", 1, NULL, 0, 0, 60000)) ok++;
    chk(ok == CW_MAX_JOBS, "la cola se llena y deja de aceptar");
    chk(coworkCount() == CW_MAX_JOBS, "sin pasarse de su tamano");
    // Con todo ACTIVO no se descarta nada para hacer sitio.
    chk(coworkSubmit(CW_KIND_SUMMARY, "z", "y", 1, NULL, 0, 0, 60000) == 0,
        "con todo activo, el nuevo se rechaza");
    // En cuanto uno termina y se ve, su hueco se puede reciclar.
    CoworkJob* first = coworkAt(0);
    uint32_t fid = first->id;
    coworkBeginWork(fid, 60000);
    coworkFinish(fid, "r", 1, 0, false, 60100);
    coworkMarkSeen(fid);
    chk(coworkSubmit(CW_KIND_SUMMARY, "nuevo", "y", 1, NULL, 0, 0, 60200) != 0,
        "un terminado y visto deja sitio al siguiente"); }

  // ---- sin almacen conectado ----
  coworkAttachStorage(NULL, 0);
  chk(!coworkReady(), "sin almacen, el motor lo dice");
  chk(coworkSubmit(CW_KIND_SUMMARY, "x", "y", 1, NULL, 0, 0, 0) == 0, "y no encola nada");
  chk(coworkAt(0) == NULL && coworkFind(1) == NULL, "y las consultas devuelven vacio");
  chk(coworkPump(1000) == 0, "y avanzar no hace nada");

  printf("=== %d comprobaciones, %d fallos ===\n", gChecks, gFails);
  return gFails ? 1 : 0;
}
