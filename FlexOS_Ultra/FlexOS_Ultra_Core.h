// #############################################################
// ##  FLEX OS ULTRA  ·  NUCLEO DEL SISTEMA: MEMORIA, MULTITAREA Y RENDIMIENTO
// ##  ----------------------------------------------------------
// ##  El gestor de memoria (medida periodica, presion, puerta de admision
// ##  de apps, alivio automatico), los tres botones del sistema
// ##  (atras / inicio / recientes) y el diagnostico de fluidez.
// ##
// ##  COMO ENCAJA ESTE ARCHIVO
// ##  ----------------------------------------------------------
// ##  Es una PARTE del sketch FlexOS_Ultra.ino, no una unidad de
// ##  traduccion independiente. FlexOS_Ultra.ino lo incluye en el
// ##  orden que fija la cadena de cabeceras (cada modulo incluye al
// ##  anterior), asi que todo el sistema sigue compilandose como UN
// ##  SOLO archivo, exactamente igual que antes de separarlo.
// ##
// ##  Consecuencias practicas, y son las que mantienen esto seguro:
// ##    · Las variables globales se DEFINEN una sola vez, aqui, en el
// ##      modulo al que pertenecen. No hace falta `extern` ni existe
// ##      el riesgo de una definicion duplicada en el enlazado.
// ##    · El ORDEN de definicion es el mismo que tenia el .ino: una
// ##      funcion `static` solo se puede llamar despues de definirse,
// ##      y esa relacion se conserva modulo a modulo.
// ##    · La cadena de includes es LINEAL (Types -> ... -> Recovery),
// ##      asi que no hay dependencias circulares posibles.
// ##    · No lo incluyas por tu cuenta desde otro sitio: el punto de
// ##      entrada del sistema es siempre FlexOS_Ultra.ino.
// #############################################################
#pragma once
#include "FlexOS_Ultra_AppFramework.h"   // eslabon anterior de la cadena

// #############################################################
// ##  GESTOR DE MEMORIA Y MULTITAREA  ·  medir, decidir, soltar
// ##  ------------------------------------------------------
// ##  TRES CAPAS, SEPARADAS A PROPOSITO:
// ##
// ##   1. MEDIR (aqui). heap_caps_* y el sistema de archivos, nada mas.
// ##      Toda cifra que el usuario vea sale de esta medida; lo que no se
// ##      puede medir se dice "No disponible" y no se rellena.
// ##
// ##   2. DECIDIR (FlexOS_Mem.cpp). Los cortes de 10/6/5 MB, el bloque
// ##      contiguo minimo, la fragmentacion y el enfriamiento de los
// ##      avisos. Es logica pura y se ejercita entera en el PC
// ##      (tests/host/test_mem.cpp).
// ##
// ##   3. SOLTAR (memShedSystem, junto a themeChanged). Los punteros de
// ##      los buffers grandes viven repartidos por todo el sketch, asi
// ##      que la accion se define ALLI, donde se les ve; aqui solo se
// ##      declara y se orquesta. NUNCA se toca un dato del usuario.
// ##
// ##  POR QUE SE MIDE POR TIEMPO Y NO POR CUADRO. heap_caps_get_free_size
// ##  es barato, pero heap_caps_get_largest_free_block recorre la lista de
// ##  huecos y flexFsUsedBytes() recorre el sistema de archivos entero (eso
// ##  es FLASH). Por eso hay tres cadencias y ninguna cae dentro de la ruta
// ##  de dibujo:
// ##    · rapida  (MEM_TICK_MS)  -> libre y total de PSRAM y de SRAM;
// ##    · lenta   (MEM_BLOCK_MS) -> mayor bloque contiguo;
// ##    · a peticion             -> flash, y solo con la pantalla de
// ##                                detalle a la vista.
// ##
// ##  CERO RESERVAS. La medida vive en una struct estatica y los textos se
// ##  componen con snprintf en buffers del llamante: ni un malloc por
// ##  refresco, ni un String, ni un delay.
// #############################################################
#define MEM_TICK_MS    1000       // libre/total (barato)
#define MEM_BLOCK_MS   2000       // mayor bloque contiguo (recorre huecos)
#define MEM_FLASH_MS   15000      // flash: SOLO con la pantalla de detalle a la vista

static FlexMemSnap   gMem;                 // ULTIMA medida: la unica fuente de cifras
static uint32_t      gMemTickMs = 0, gMemBlockMs = 0, gMemFlashMs = 0;
static uint32_t      gMemPeakUsed = 0;     // pico de PSRAM usada desde el arranque
static uint32_t      gMemInMin    = 0xFFFFFFFFu;  // minimo de SRAM interna libre desde el arranque
static FlexMemAlerts gMemAlerts;
static bool          gMemWantFlash = false;       // la pantalla de detalle esta a la vista

// Lo unico que este bloque necesita LLAMAR y que vive mas abajo. Mismo patron
// que el resto del sketch (ver el bloque de prototipos junto a sysBack).
static uint32_t memShedSystem();      // caches del sistema (definida junto a themeChanged)
static void     swThumbTrim(int keep);// Recientes: deja como mucho 'keep' miniaturas

static inline uint32_t memFreePsram(){ return (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM); }

// -------------------------------------------------------------
//  1) MEDIR
// -------------------------------------------------------------
static void memSample(bool withBlock, bool withFlash){
  gMem.psTotal = (uint32_t)heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
  gMem.psFree  = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  gMem.inTotal = (uint32_t)heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
  gMem.inFree  = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  if(withBlock) gMem.psLargest = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
  // Pico y minimo se llevan AQUI, con exactamente la misma medida que se
  // ensena, en vez de pedirlos al SDK: asi la cifra del historico y la del
  // momento no pueden venir de dos contadores distintos y contradecirse.
  uint32_t used = flexMemUsed(&gMem);
  if(used > gMemPeakUsed) gMemPeakUsed = used;
  if(gMem.inTotal && gMem.inFree < gMemInMin) gMemInMin = gMem.inFree;
  gMem.psPeakUsed = gMemPeakUsed;
  gMem.inMin      = (gMemInMin == 0xFFFFFFFFu) ? 0 : gMemInMin;
  if(withFlash){
    if(flexFsReady()){
      gMem.fsTotal = flexFsTotalBytes();
      gMem.fsUsed  = flexFsUsedBytes();
      gMem.fsValid = 1;
    } else {
      gMem.fsTotal = gMem.fsUsed = 0;
      gMem.fsValid = 0;
    }
  }
}

// Medida COMPLETA e inmediata (bloque contiguo incluido). La usan las
// decisiones: antes de abrir una app pesada no vale una medida de hace 2 s.
static void memSampleNow(){
  memSample(true, false);
  gMemTickMs = gMemBlockMs = millis();
}
// Relectura de la flash bajo demanda (al abrir la pantalla de detalle).
static void memSampleFlashNow(){
  memSample(false, true);
  gMemFlashMs = millis();
}

// Un paso del muestreo periodico. Lo llama loop() y no dibuja nada.
static void memTick(){
  uint32_t now = millis();
  if(now - gMemTickMs < MEM_TICK_MS) return;
  gMemTickMs = now;
  bool blk = (now - gMemBlockMs >= MEM_BLOCK_MS);
  // La medida de flash recorre el sistema de archivos: nunca con el dedo
  // apoyado. Un gesto en curso manda sobre una cifra que puede esperar.
  bool fls = (gMemWantFlash && !T.down && now - gMemFlashMs >= MEM_FLASH_MS);
  if(blk) gMemBlockMs = now;
  if(fls) gMemFlashMs = now;
  memSample(blk, fls);
  // EL MODO VISUAL EFICIENTE SE APAGA SOLO. Lo encendio Optimizar porque la
  // presion seguia alta; en cuanto vuelve a haber holgura de verdad (nivel
  // OPTIMO, no solo "ya no critico") se devuelve el aspecto completo. Nunca se
  // guarda ni se pregunta: no es una preferencia del usuario.
  // Se lee el nivel SOSTENIDO (con histeresis), no el instantaneo: si no, un
  // repintado que baja la memoria un instante volveria a encender el modo
  // eficiente justo despues de apagarlo.
  if(gEffMode && blk && gMemAlerts.levelSeen && gMemAlerts.level == FLEXMEM_LV_OK){
    gEffMode = false;
    glcValid = false; gHomeDirty = true; qsDirty = true;
  }
}

// La medida publicada. Todo lo que se dibuja lee de aqui, jamas del SDK.
static const FlexMemSnap* memSnap(){ return &gMem; }

// -------------------------------------------------------------
//  2) METADATOS POR APP
//  ------------------------------------------------------------
//  Clase de peso: tabla constante indexada por IC_*. Vive en flash, no
//  ocupa RAM y se lee de un vistazo, que es justo lo que hace falta para
//  poder revisar la politica sin recorrer el sketch entero.
// -------------------------------------------------------------
static const uint8_t APP_WEIGHT[APP_N] = {
  FLEXMEM_W_LIGHT,    // 0  Reloj
  FLEXMEM_W_HEAVY,    // 1  Galeria      (decodifica JPEG, cache de miniaturas)
  FLEXMEM_W_HEAVY,    // 2  Multimedia   (video por bloques, fotograma completo)
  FLEXMEM_W_LIGHT,    // 3  Almacenamiento
  FLEXMEM_W_HEAVY,    // 4  Modo PC/DeX  (fondo compuesto + ventanas)
  FLEXMEM_W_LIGHT,    // 5  Notas
  FLEXMEM_W_MEDIUM,   // 6  Educacion
  FLEXMEM_W_HEAVY,    // 7  Navegador    (tarea de red + cache de fotogramas)
  FLEXMEM_W_MEDIUM,   // 8  Code IDE
  FLEXMEM_W_MEDIUM,   // 9  Bienestar
  FLEXMEM_W_MEDIUM,   // 10 Paint        (trazo en PSRAM)
  FLEXMEM_W_MEDIUM,   // 11 Juegos
  FLEXMEM_W_LIGHT,    // 12 Ajustes
  FLEXMEM_W_LIGHT,    // 13 Calculadora
  FLEXMEM_W_LIGHT,    // 14 Calendario
  FLEXMEM_W_HEAVY,    // 15 Camara       (buffer de sensor)
  FLEXMEM_W_LIGHT,    // 16 Clima
  FLEXMEM_W_MEDIUM,   // 17 Flex Store
  FLEXMEM_W_LIGHT     // 18 Flex Phone
};
static int appWeight(int id){ return (id >= 0 && id < APP_N) ? (int)APP_WEIGHT[id] : FLEXMEM_W_LIGHT; }
static const char* appWeightName(int id){
  switch(appWeight(id)){
    case FLEXMEM_W_HEAVY:  return "Pesada";
    case FLEXMEM_W_MEDIUM: return "Media";
    default:               return "Ligera";
  }
}

// HUELLA MEDIDA DE CADA APP, EN BYTES. No sale de una tabla: es la PSRAM que
// desaparecio mientras corria su enter()/resume(), menos la que devolvio al
// suspenderse o al soltar recursos. Las dos son mediciones reales tomadas
// alrededor de una llamada durante la cual no corre nada mas.
//
// LIMITE HONESTO -- y por eso la interfaz dice "estimado": no captura lo que
// la app reserve DESPUES de construirse (una foto que se abre mas tarde), y
// otro subsistema podria reservar en la misma ventana. Sin medida se escribe
// "--", nunca un numero inventado.
static uint32_t gAppMem[APP_N];
static bool     gAppMemKnown[APP_N];

// PAUSADA vs. ESTADO GUARDADO. Las dos son ALIFE_SUSPENDED en el motor -- el
// estado logico vivo es el mismo --, y lo que las separa es si a la app ya se
// le han soltado los recursos pesados reconstruibles. Esta marca es lo que
// permite decirlo en la tarjeta de Recientes sin mentir: "Pausada" = todavia
// tiene lo suyo cargado; "Estado guardado" = se le solto y al volver lo rehace.
// Se pone al soltar de verdad (memShedApp mide que se libero algo) y se quita
// al reanudarla o al cerrarla.
static bool     gAppShed[APP_N];

// Cierra una medida abierta con memFreePsram() antes de construir la app.
static void appMemCommit(int id, uint32_t freeBefore){
  if(id < 0 || id >= APP_N || freeBefore == 0) return;
  uint32_t now = memFreePsram();
  if(freeBefore > now){ gAppMem[id] = freeBefore - now; gAppMemKnown[id] = true; }
  else if(!gAppMemKnown[id]){ gAppMem[id] = 0; gAppMemKnown[id] = true; }  // no reservo: dato real
}
// La app devolvio memoria (suspend o shed): su huella baja en lo medido.
static void appMemReleased(int id, uint32_t bytes){
  if(id < 0 || id >= APP_N || !gAppMemKnown[id]) return;
  gAppMem[id] = (gAppMem[id] > bytes) ? (gAppMem[id] - bytes) : 0;
}
static void appMemForget(int id){
  if(id < 0 || id >= APP_N) return;
  gAppMem[id] = 0; gAppMemKnown[id] = false; gAppShed[id] = false;
}
static bool     appMemHas(int id){ return id >= 0 && id < APP_N && gAppMemKnown[id]; }
static uint32_t appMemBytes(int id){ return appMemHas(id) ? gAppMem[id] : 0u; }

// CAMBIOS SIN GUARDAR. Dos fuentes, las dos reales: la marca de sesion
// desfasada del framework y, si la app la publica, su propia respuesta.
static bool appUnsaved(int id){
  if(id < 0 || id >= APP_N) return false;
  const AppHooks* h = appHooks(id);
  if(h && h->dirty && h->dirty()) return true;
  return gSessNeedSave[id];
}

// -------------------------------------------------------------
//  3) SOLTAR  ·  recursos de una APP suspendida
//  ------------------------------------------------------------
//  Nunca la app en primer plano y nunca una con trabajo real en curso. El
//  estado logico NO se toca: la app sigue SUSPENDIDA y al volver reconstruye
//  lo que se le solto. Es la diferencia entre "adelgazar" y "cerrar", y por
//  eso esto se intenta SIEMPRE antes de cerrar nada.
// -------------------------------------------------------------
static uint32_t memShedApp(int id){
  if(id < 0 || id >= APP_N) return 0;
  if(gAppState[id] != ALIFE_SUSPENDED) return 0;   // la activa, jamas
  if(appBgBusy(id)) return 0;                      // trabajo real: intocable
  const AppHooks* h = appHooks(id);
  if(!(h && h->shed)) return 0;
  uint32_t before = memFreePsram();
  size_t hint = h->shed();               // la app dice SI solto algo...
  uint32_t after = memFreePsram();
  uint32_t got = after > before ? (after - before) : 0u;   // ...y aqui se MIDE cuanto
  appMemReleased(id, got);
  // La marca de "estado guardado" depende de que la app haya soltado de verdad,
  // no de cuantos bytes salieron: soltar una cache que ocupaba poco sigue
  // significando que al volver hay que reconstruirla. La CIFRA que se ensena,
  // en cambio, es siempre la medida -- nunca la pista.
  if(hint || got) gAppShed[id] = true;
  return got;
}

// SOLTAR TODO LO SEGURO, en orden LRU (la menos usada primero). 'stopAt'
// permite parar en cuanto haya bastante: no se suelta de mas por deporte.
// El bucle esta acotado por APP_N, asi que no puede quedarse dando vueltas.
static uint32_t memShedAll(uint32_t stopAt){
  uint32_t got = memShedSystem();
  if(stopAt && got >= stopAt) return got;
  uint8_t done[APP_N];
  memset(done, 0, sizeof(done));
  for(int pass = 0; pass < APP_N; pass++){
    int id = -1; uint32_t oldest = 0xFFFFFFFFu;
    for(int i = 0; i < APP_N; i++){
      if(done[i]) continue;
      if(gAppState[i] != ALIFE_SUSPENDED) continue;
      if(appBgBusy(i)) continue;
      if(gAppSeenMs[i] <= oldest){ oldest = gAppSeenMs[i]; id = i; }
    }
    if(id < 0) break;
    done[id] = 1;
    got += memShedApp(id);
    if(stopAt && got >= stopAt) break;
  }
  return got;
}

// -------------------------------------------------------------
//  4) PRESUPUESTO AL ABRIR
//  ------------------------------------------------------------
//  Devuelve FLEXMEM_OK si la app se puede abrir. Si el nucleo dice "todavia
//  no", se sueltan recursos reconstruibles, se vuelve a MEDIR y se pregunta
//  otra vez -- UNA sola vez: si tras soltar sigue sin caber, la respuesta es
//  no y el motivo lo da el nucleo. Desde aqui no se cierra ninguna app: eso
//  es del desalojo, y solo con la sesion ya guardada.
// -------------------------------------------------------------
static int memAdmitApp(int id){
  int w = appWeight(id);
  memSampleNow();
  int v = flexMemCanOpen(memSnap(), w);
  if(v != FLEXMEM_NEED_SHED) return v;
  memShedAll(0);
  memSampleNow();
  v = flexMemCanOpen(memSnap(), w);
  return (v == FLEXMEM_NEED_SHED) ? FLEXMEM_DENY_PSRAM : v;
}

// Motivo legible de una negativa: dice el EFECTO, no el mecanismo.
static const char* memDenyText(int verdict){
  switch(verdict){
    case FLEXMEM_DENY_BLOCK: return "Memoria libre en trozos peque\xC3\xB1os";
    case FLEXMEM_DENY_SRAM:  return "Memoria interna baja: se protege el sistema";
    default:                 return "Cierra una app o pulsa Optimizar Flex OS";
  }
}

// La apertura se ha negado. Se dice CUAL app y POR QUE, con el mismo canal
// que el resto de avisos del sistema. Nunca en silencio: un icono que no hace
// nada al tocarlo es peor que un "no" explicado.
static void appDenyMemory(int id, int verdict){
  char t[72];
  snprintf(t, sizeof(t), "%s no se abre ahora", appName(id));
  sysNotify(t, memDenyText(verdict));
  Serial.printf("[MEM] apertura denegada: %s (veredicto %d, libre %u KB, bloque %u KB)\n",
                appName(id), verdict,
                (unsigned)(gMem.psFree / 1024u), (unsigned)(gMem.psLargest / 1024u));
}

// -------------------------------------------------------------
//  5) MEMORIA AUTOMATICA  ·  primero actuar, avisar solo si hace falta
//  ------------------------------------------------------------
//  EL PRINCIPIO: el usuario no tiene por que pensar en la RAM. Con memoria
//  suficiente NO se ve absolutamente nada -- ni banner, ni barra, ni icono,
//  ni recordatorio. Cuando el sistema se aprieta, PRIMERO se arregla solo, y
//  unicamente si despues de arreglarlo sigue apretado se dice una vez.
//
//  LA SECUENCIA, en orden:
//    1. Nivel SOSTENIDO con histeresis (flexMemLevelStep). Empeorar es
//       inmediato; mejorar exige margen, asi que el estado no parpadea.
//    2. OK y NOTICE (por encima de 6 MB): no se hace ni se dice nada.
//    3. Al EMPEORAR a WARN o CRITICAL -- o cada FLEXMEM_RELIEF_MS mientras
//       siga apretado -- se sueltan recursos reconstruibles y se vuelve a
//       MEDIR. Esto no se ve: es el sistema haciendo su trabajo.
//    4. Solo si tras el alivio SIGUE en WARN o CRITICAL se saca UNA tarjeta
//       por la isla de notificaciones, que es temporal y no cambia el layout.
//
//  Por que esto y no un panel permanente: un aviso que esta siempre deja de
//  leerse, y un boton que casi nunca hace falta ocupa sitio todo el rato.
//
//  NO se avisa desde cualquier pantalla: en el bloqueo, en Modo seguro, en
//  mitad de un borrado o de una transicion, una tarjeta que aparece es una
//  interrupcion y no una ayuda. Con la cortina abierta no hace falta filtrar
//  aqui: la isla ya encola sin dibujar y ARMA la tarjeta cuando vuelve a
//  verse (ver notifPush), que es exactamente el comportamiento correcto.
// -------------------------------------------------------------
static void memAlertTick(){
  if(gSafeMode || gFrPending) return;

  // ---- 1) nivel sostenido ----
  // Se actualiza SIEMPRE, en cualquier pantalla: es una comparacion de
  // enteros sin efectos secundarios, y congelarla mientras el usuario esta en
  // Recientes o en el explorador dejaria el estado (y con el, el modo visual
  // eficiente) atascado en el ultimo valor que se vio desde Inicio.
  uint32_t now = millis();
  int lv = flexMemLevelStep(memSnap(), &gMemAlerts);
  bool rose = gMemAlerts.rose != 0;

  // Lo que SI se limita por pantalla es ACTUAR y AVISAR: soltar buffers en
  // mitad de una animacion o sacar una tarjeta sobre el bloqueo no ayuda a
  // nadie.
  if(gState != ST_HOME && gState != ST_APP) return;
  if(appTrVisible()) return;                 // en mitad de una animacion, nadie mas compone
  if(optActive()) return;                    // el panel de Optimizar ya esta haciendo esto
  // COORDINACION CON LA RADIO. Con el enlace del C6 levantandose, el alivio
  // automatico espera: no es urgente y el handshake SDIO si es sensible al
  // tiempo. Es una cesion no bloqueante y muy localizada -- se reintenta en la
  // vuelta siguiente -- y no un candado global.
  if(wifiRadioBusy()) return;

  // ---- 2) con holgura, silencio absoluto ----
  if(lv >= FLEXMEM_LV_WARN){
    // ---- 3) ALIVIO AUTOMATICO: al empeorar, y luego a lo sumo cada minuto ----
    if(rose || flexMemReliefDue(&gMemAlerts, now)){
      flexMemReliefDone(&gMemAlerts, now);
      memShedAll(0);                         // caches del sistema + apps suspendidas
      memSampleNow();
      lv = flexMemLevelStep(memSnap(), &gMemAlerts);
      // ---- 4) ¿se arreglo solo? entonces ni una tarjeta ----
      if(lv < FLEXMEM_LV_WARN) rose = false;
      else if(rose){
        if(lv >= FLEXMEM_LV_CRITICAL)
          sysNotify("Memoria cr\xC3\xADtica",
                    "Flex OS est\xC3\xA1 protegiendo el sistema. Cierra una app.");
        else
          sysNotify("Memoria casi llena",
                    "Flex OS liber\xC3\xB3 recursos en segundo plano.");
      }
    }
  }

  // ---- Avisos SECUNDARIOS (fragmentacion, SRAM y flash) ----
  // Son condiciones distintas de "queda poca PSRAM" y tienen su propio
  // enfriamiento por clase; los de PSRAM ya no salen de aqui.
  int k = flexMemAlertPick(memSnap(), now, &gMemAlerts);
  switch(k){
    case FLEXMEM_AL_FRAG:
      sysNotify("Memoria libre repartida en trozos peque\xC3\xB1os",
                "Las im\xC3\xA1genes o apps pesadas pueden tardar m\xC3\xA1s"); break;
    case FLEXMEM_AL_SRAM:
      sysNotify("Memoria interna del sistema baja",
                "Se limitan cargas pesadas para proteger Wi-Fi y t\xC3\xA1" "ctil"); break;
    case FLEXMEM_AL_FLASH80:
      sysNotify("Almacenamiento interno en uso elevado",
                "Limpiar la cach\xC3\xA9 temporal puede ayudar"); break;
    case FLEXMEM_AL_FLASH90:
      sysNotify("Almacenamiento interno casi lleno",
                "Las actualizaciones y los datos nuevos podr\xC3\xAD" "an fallar"); break;
    default: break;
  }
}

// DESALOJO POR PRESUPUESTO DE MEMORIA (antes: por un numero fijo de sesiones).
// ---------------------------------------------------------------------------
// Ya NO hay un tope arbitrario de apps abiertas: el usuario puede tener tantas
// como quepan de forma segura. Lo que decide es la MEMORIA MEDIDA, y el orden
// de intentos es siempre el mismo, del menos invasivo al mas:
//
//   1. soltar caches del sistema y recursos reconstruibles de las apps
//      suspendidas (LRU primero) -- nadie pierde su sitio;
//   2. si el sistema sigue en zona critica, cerrar la app suspendida MENOS
//      reciente, y SOLO si su sesion se pudo guardar. Si no se pudo guardar,
//      se conserva: perder el trabajo del usuario en silencio para "liberar
//      recursos" no es una opcion.
//
// La app en primer plano nunca entra en el reparto, y una con trabajo real en
// segundo plano (lista blanca) tampoco.
static void appEnforceMemoryBudget(){
  FLEXDIAG_WIFI("appEnforceMemoryBudget");
  if(wifiRadioBusy()) return;              // la radio primero (ver wifiRadioBusy)
  memSampleNow();
  if(flexMemLevel(memSnap()) != FLEXMEM_LV_CRITICAL) return;

  memShedAll(0);                                       // 1) adelgazar, sin cerrar nada
  memSampleNow();
  if(flexMemLevel(memSnap()) != FLEXMEM_LV_CRITICAL) return;

  // 2) ultimo recurso. Acotado por APP_N: no puede quedarse dando vueltas.
  for(int guard = 0; guard < APP_N; guard++){
    int victim = -1; uint32_t oldest = 0xFFFFFFFFu;
    for(int i = 0; i < APP_N; i++){
      if(gAppState[i] != ALIFE_SUSPENDED) continue;
      if(appBgBusy(i)) continue;                       // trabajo real en curso: intocable
      if(gAppSeenMs[i] <= oldest){ oldest = gAppSeenMs[i]; victim = i; }
    }
    if(victim < 0) break;                              // todas protegidas: no se fuerza nada
    if(!appTerminate(victim, false)) break;            // no se pudo guardar: mejor conservar
    appMemForget(victim);
    for(int c = 0; c < swCardCount(); c++) if(swCardApp(c) == victim){ swDropCard(c); break; }
    Serial.printf("[LIFE] presupuesto de memoria: %s cerrada\n", appName(victim));
    memSampleNow();
    if(flexMemLevel(memSnap()) != FLEXMEM_LV_CRITICAL) break;
  }
}

// RUNNING -> SUSPENDED. Conserva TODAS las capas propias (teclado, menu,
// dialogo, selector), toma la miniatura exacta y deja que la app pause solo
// recursos/animaciones. Cerrar capas pertenece exclusivamente a ATRAS: Inicio
// debe volver despues a la app tal y como estaba, igual que en un telefono.
static void appSuspend(int id, bool landscape){
  if(id < 0 || id >= APP_N) return;
  if(gAppState[id] == ALIFE_SUSPENDED) return;
  if(landscape || (APP_REG[id].flags & APP_LAND)) swPushNoThumb(id);   // miniatura girada: mejor ninguna
  else                                            swPushAndCapture(id);
  const AppHooks* h = appHooks(id);
  if(h && h->suspend) h->suspend();
  gAppState[id] = ALIFE_SUSPENDED;
  gAppSeenMs[id] = millis();
  // GUARDADO EN LA VUELTA SIGUIENTE, NO AQUI. Suspender es un punto seguro de
  // guardado, pero escribir en flash ANTES de la animacion de salida mete el
  // coste de la escritura justo dentro de la transicion (con el lienzo de Paint
  // son mas de 100 ms de tiron). Se deja el volcado ARMADO y vencido, asi que
  // sessAutosaveTick lo hace en la siguiente vuelta del bucle -- con el
  // escritorio ya publicado y el dedo ya levantado. La ventana de riesgo es de
  // una vuelta de loop; cerrar la app (appTerminate) si escribe en el acto.
  if(gSessNeedSave[id]){
    if(gSessDirtyApp != id) sessMarkDirty(id);
    gSessDirtyMs = gSessDirtyFirstMs = 0;
  }
  appEnforceMemoryBudget();
}

// APP QUE NUNCA LLEGO A EXISTIR.
// ---------------------------------------------------------------------------
// Con las transiciones interrumpibles, entre el toque en el icono y el enter()
// de la app pasan unos milisegundos en los que la app YA es la activa
// logicamente pero todavia no ha construido nada. Si el usuario se va en esa
// ventana -- gesto Home, boton atras, Recientes --, suspenderla seria escribir
// una sesion y una miniatura de una pantalla que nadie ha pintado, y al volver
// a abrirla se llamaria a su resume() sin que su enter() haya corrido nunca.
// Devuelve true si habia una apertura pendiente para 'id' y la ha cancelado,
// dejando el ciclo de vida como estaba antes del toque.
static bool appCancelPendingOpen(int id){
  if(!gTrEnterPending || gTrEnterGen != gTrGen) return false;
  if(!gTrIn.on || gTrIn.app != id) return false;
  gTrEnterPending = false;
  if(id >= 0 && id < APP_N) gAppState[id] = gTrPrevLife;
  return true;
}

// Deja las tarjetas de Recientes y el ciclo de vida coherentes: una tarjeta
// solo puede existir para una app abierta o suspendida.
static void swSyncFromLife(){
  for(int i = swCardCount() - 1; i >= 0; i--)
    if(gAppState[swCardApp(i)] == ALIFE_CLOSED) swDropCard(i);
}

// #############################################################
// ##  LOS TRES BOTONES
// #############################################################
// ATRAS: 1) cierra una capa propia de la app (teclado emergente, menu,
// dialogo, selector); 2) si no hay capa, retrocede UNA pantalla interna de la
// app; 3) si tampoco hay, suspende y vuelve al escritorio. Nunca hace falta
// pulsarlo muchas veces solo para salir.
static void sysBack(){
  if(KIOSK_ON && kioskOn) return;
  if(gState != ST_APP) return;
  int id = gAppId;
  const AppHooks* h = appHooks(id);
  if(h && h->backLayer && h->backLayer()){ touchDropAll(); return; }
  if(h && h->backScreen && h->backScreen()){ touchDropAll(); return; }
  appClose();
}
// INICIO: al escritorio desde donde sea, suspendiendo (nunca reiniciando) la
// app. No borra su contenido y no anade ninguna entrada de historial.
static void sysHome(){
  if(KIOSK_ON && kioskOn) return;
  if(gHosted){ gHostReq = 1; return; }
  if(gState == ST_APP){ appClose(); return; }
  if(gState != ST_HOME){ enterHome(); touchDropAll(); }
}
// RECIENTES: suspende la app actual (para que su tarjeta refleje lo ultimo que
// habia en pantalla) y abre el gestor real.
static void sysRecents(){
  if(KIOSK_ON && kioskOn) return;
  if(gHosted){ gHostReq = 3; return; }
  if(gState == ST_APP){
    bool land = gLand;
    gLand = false;
    gClipY0 = 0; gClipY1 = SCR_H - 1; gClipX0 = 0; gClipX1 = SCR_W - 1;
    setBuf(fb);
    // Igual que en appClose: una app que todavia no ha corrido su enter() no se
    // suspende (no tendria ni estado ni miniatura que guardar), se cancela.
    if(!appCancelPendingOpen(gAppId)) appSuspend(gAppId, land);
    if(gHomeDirty) renderHome();
  }
  appTrCancel();          // Recientes dibuja la pantalla entera: ninguna capa sobrevive
  swSyncFromLife();
  activarMultitarea();
  touchDropAll();
}
// Reparto del toque en la franja del sistema. Devuelve true si lo consumio: el
// tick de la app no llega a verlo nunca.
static bool navBarHandle(){
  if(!navBarVisible()) return false;
  int top = navBarTop();
  bool inBar = (T.y >= top);
  // El destello se apaga SOLO, con una unica publicacion de la banda. Va lo
  // primero para que tambien se apague cuando el dedo ya no toca nada.
  if(gNavGlow >= 0 && gNavPress < 0 && (millis() - gNavGlowMs) >= NAV_PRESS_MS){
    gNavGlow = -1;
    flxFlush(top, SCR_H - 1);
  }
  if(T.pressed && inBar){
    gNavPress = gNavGlow = (T.x < SCR_W / 3) ? 0 : (T.x < SCR_W * 2 / 3 ? 1 : 2);
    gNavGlowMs = millis();
    flxFlush(top, SCR_H - 1);                 // el destello lo estampa navStampBar
    return true;
  }
  if(T.down && gNavPress >= 0) return true;   // arrastre iniciado en la barra: no llega a la app
  if(gNavPress >= 0 && (T.released || T.tap)){
    int btn = gNavPress;
    gNavPress = -1;
    gNavGlowMs = millis();                    // a partir de aqui el destello se desvanece solo
    flxFlush(top, SCR_H - 1);
    if(!inBar){ gNavGlow = -1; return true; } // el dedo se fue de la barra: se cancela la accion
    if(btn == 0)      sysBack();
    else if(btn == 1) sysHome();
    else              sysRecents();
    return true;
  }
  if(inBar && (T.tap || T.released || T.down)) return true;   // nada se filtra a la app
  return false;
}

// SALIR DE LA APP AL ESCRITORIO. Ya no destruye nada: SUSPENDE. La app conserva
// su estado logico, deja de recibir tick() y toques, y su tarjeta queda en
// Recientes con la miniatura de lo ultimo que se vio. Volver a abrirla la
// REANUDA (ver enterApp), no la reinicia.
static void appClose(){
  // La cortina no puede sobrevivir a un cambio de app: se cierra y suelta el
  // toque ANTES de tocar nada mas (ver qsForceClose).
  qsForceClose();
  // FASE 4: un unico candado cierra TODAS las salidas de la app -- boton atras,
  // chevron de la cabecera, gesto rapido de la barra iOS, y cualquier app que
  // llame a appClose desde su propio tick. Poniendolo aqui no hay que ir
  // parcheando cada camino por separado (y ninguno nuevo se escapa).
  if(KIOSK_ON && kioskOn) return;
  if(gHosted){ gHostReq = 1; return; }        // dentro de una ventana: cierra la VENTANA
  // El FRAMEWORK devuelve el motor a portrait, no la app. Antes cada app
  // landscape tenia que acordarse de hacer gLand=false por su cuenta (pcExit,
  // una salida propia); si se salia por cualquier otra via -- gesto de la barra,
  // boton de atras, o una app que llamara a appClose desde dentro de su propio
  // tick -- gLand se quedaba en true y TODO lo que se pintara despues pasaba por
  // putPhys: el escritorio salia girado 90 y recortado (ly solo llega a 479, asi
  // que la mitad inferior desaparecia), y de ahi ya no se recuperaba sin
  // reiniciar. Resetear aqui cierra esa clase entera de fallo de una vez.
  bool wasLand = gLand;
  gLand = false;
  gClipY0 = 0; gClipY1 = SCR_H - 1;
  gClipX0 = 0; gClipX1 = SCR_W - 1;
  setBuf(fb);
  int outId = gAppId;
  // Si la app aun no habia corrido su enter(), no se la suspende: nunca llego a
  // existir (ver appCancelPendingOpen).
  if(!appCancelPendingOpen(outId))
    appSuspend(outId, wasLand);        // conserva capas, toma miniatura y arma el guardado
  // La transicion compone SOBRE homeBuf: si Ajustes lo dejo sucio, hay que
  // recomponerlo ANTES, o la animacion de cierre encoge hacia el escritorio
  // viejo y este cambia de golpe al terminar.
  if(gHomeDirty) renderHome();
  // ESTADO LOGICO, EN EL ACTO: Inicio manda desde esta linea y ya acepta toques
  // (homeTick corre en la MISMA vuelta de loop). La app saliente solo sigue
  // existiendo como capa visual, y esa capa no gobierna la navegacion.
  enterHomeState();
  touchDropAll();                      // ni un solo evento del gesto anterior llega al escritorio
  appTrBeginClose(outId);              // capa visual; NO bloquea
}
// ABRIR O REANUDAR. Si la app estaba SUSPENDIDA y tiene hook de reanudacion, se
// la reanuda: repinta desde su estado logico, sin pasar por enter() -- que es lo
// que reiniciaba la nota, el lienzo o el scroll. Una app sin hook de reanudacion
// se reconstruye con enter(), que para una pantalla realmente estatica (por
// ejemplo Educacion) es exactamente lo correcto.
static void enterApp(int id){
  qsForceClose();                 // ninguna app se abre con la cortina a medias
  if(id < 0 || id >= APP_N) return;
  // FASE 4: en kiosco solo se puede estar en la app clavada. Esto bloquea que
  // una app abra otra a pantalla completa y deje kioskApp apuntando a otro sitio.
  if(KIOSK_ON && kioskOn && id != kioskApp) return;
  if(gHosted){ gHostReq = 2; gHostReqApp = id; return; }   // -> otra ventana de DeX
  if(gFrPending) return;                          // restablecimiento en curso: nada se abre
  if(gSafeMode && !safeAppAllowed(id)){ safeDenyApp(id); return; }
  // PRESUPUESTO DE MEMORIA. Solo puede NEGAR una app que no este ya abierta:
  // volver a una app suspendida jamas se bloquea (su memoria ya esta contada,
  // y dejar al usuario sin poder volver a su nota seria peor que el apuro que
  // se intenta evitar). Antes de negar nada, memAdmitApp suelta recursos
  // reconstruibles y vuelve a medir.
  if(gAppState[id] == ALIFE_CLOSED){
    int verdict = memAdmitApp(id);
    if(verdict != FLEXMEM_OK){ appDenyMemory(id, verdict); return; }
  }
  appLoadSessionOnce(id);
  bool resuming = (gAppState[id] == ALIFE_SUSPENDED);
  const AppHooks* h = appHooks(id);
  if(resuming && !(h && h->resume)) resuming = false;   // sin hook: se reconstruye con enter()
  uint8_t prevLife = gAppState[id];
  gAppState[id] = resuming ? ALIFE_RESUMING : ALIFE_RUNNING;
  // ESTADO LOGICO, EN EL ACTO. A partir de esta linea la app nueva es la que
  // manda, aunque en pantalla todavia se vea encogiendo la anterior. Es lo que
  // permite encadenar aperturas sin esperar a que termine ninguna animacion.
  gAppId = id; gState = ST_APP;
  if(gHomeDirty) renderHome();                    // la transicion compone SOBRE homeBuf
  // EL DEDO QUE ABRIO LA APP SE TRAGA AQUI, no al terminar la animacion. Si se
  // tragara al final, un dedo que ya se levanto y volvio a bajar (el gesto
  // encadenado del video) perderia su toque nuevo. El candado se suelta solo
  // cuando el contacto se levanta de verdad, asi que un "down" nuevo entra sin
  // ningun retraso.
  touchDropAll();
  appTrBeginOpen(id, resuming, prevLife);         // capa visual; NO bloquea
}


// #############################################################
// ##  DIAGNOSTICO DE FLUIDEZ  ·  APAGADO por defecto
// ##  ------------------------------------------------------
// ##  Mide lo que hace falta para saber si la navegacion va fina, sin que el
// ##  funcionamiento normal dependa de Serial ni de esta instrumentacion:
// ##  con FLEX_DIAG en 0 (el valor por defecto) todo lo de aqui se compila a
// ##  NADA -- ni una variable, ni una rama, ni un byte de RAM.
// ##
// ##  Que mide:
// ##   · cuadros de transicion, tiempo medio y PEOR tiempo de cuadro;
// ##   · cuantos cuadros pasaron de 16,67 ms (el presupuesto de 60 FPS);
// ##   · latencia del toque al CAMBIO LOGICO (lo que de verdad se percibe);
// ##   · latencia del toque al PRIMER CUADRO visual de la transicion;
// ##   · RAM y PSRAM libres;
// ##   · numero de generacion, transicion activa y app logica;
// ##   · intenciones re-dirigidas (una transicion reemplazada por otra).
// ##
// ##  Para encenderlo: poner FLEX_DIAG a 1 y abrir el monitor serie. El
// ##  volcado sale al terminar cada transicion, nunca por cuadro.
// #############################################################
#define FLEX_DIAG 0

#if FLEX_DIAG
#define FLEX_DIAG_BUDGET_US 16667      // 1/60 s
static uint32_t dgFrames = 0, dgUs = 0, dgWorst = 0, dgSlow = 0;
static uint32_t dgIntentUs = 0;        // micros() del toque que origino la intencion
static uint32_t dgLogicUs  = 0;        // latencia toque -> cambio de estado logico
static uint32_t dgFirstUs  = 0;        // latencia toque -> primer cuadro visual
static bool     dgAwaitFirst = false;
static uint32_t dgRetarget = 0;        // transiciones re-dirigidas
static const char* dgWhat = "";
// La llama toda intencion nueva (abrir / cerrar), ANTES de tocar el estado.
static void dgIntent(const char* what){
  if(appTrVisible()) dgRetarget++;
  dgIntentUs = (uint32_t)micros();
  dgAwaitFirst = true;
  dgWhat = what;
  dgFrames = dgUs = dgWorst = dgSlow = 0;
}
// La llama el motor justo despues de cambiar el estado LOGICO.
static void dgLogic(){ if(dgIntentUs) dgLogicUs = (uint32_t)micros() - dgIntentUs; }
// Un cuadro de transicion.
static void dgFrame(uint32_t us){
  if(dgAwaitFirst){ dgFirstUs = (uint32_t)micros() - dgIntentUs; dgAwaitFirst = false; }
  dgFrames++; dgUs += us;
  if(us > dgWorst) dgWorst = us;
  if(us > FLEX_DIAG_BUDGET_US) dgSlow++;
}
// Volcado al terminar la transicion.
static void dgReport(){
  if(!dgFrames) return;
  Serial.printf("[FLUIDEZ] %-6s gen=%lu app=%d frames=%lu media=%lu us peor=%lu us "
                ">16.67ms=%lu  toque->logico=%lu us  toque->1er cuadro=%lu us  "
                "redirigidas=%lu  RAM=%u  PSRAM=%u\n",
                dgWhat, (unsigned long)gTrGen, gAppId,
                (unsigned long)dgFrames, (unsigned long)(dgUs / dgFrames),
                (unsigned long)dgWorst, (unsigned long)dgSlow,
                (unsigned long)dgLogicUs, (unsigned long)dgFirstUs,
                (unsigned long)dgRetarget,
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  dgFrames = 0;
}
#else
#define dgIntent(w)  ((void)0)
#define dgLogic()    ((void)0)
#define dgFrame(us)  ((void)0)
#define dgReport()   ((void)0)
#endif

// ---- MOTOR DE LA TRANSICION (necesita gAppState / APP_REG) ----------------
// Fuerza un cuadro completo en el primer frame de cada intencion nueva: es lo
// que repone la pantalla que ocupaba la app anterior (o la que deja una capa
// recien creada) sin arrastrar restos. Cuesta un memcpy de homeBuf, una vez por
// intencion -- exactamente lo que ya hacia winRevealAnim en su primer cuadro.
static bool gTrFirst = false;
static bool gTrEnterResume = false;

// Duracion del cierre. Si venimos de un gesto de la barra, la VELOCIDAD REAL del
// dedo manda: un flick fuerte cierra antes, uno que apenas pasa el umbral entra
// con la duracion nominal. Sin esto se veria un escalon entre el recorrido del
// dedo y la animacion que lo continua. Con suelo, para que un flick violento no
// degenere en un corte de un solo cuadro.
static uint32_t appTrCloseMs(){
  float v = gGbFireVel;
  if(v <= 0.35f) return ATR_CLOSE_MS;
  float f = 0.35f / v;
  if(f < 0.45f) f = 0.45f;
  return (uint32_t)(ATR_CLOSE_MS * f);
}
// La capa que estaba ENTRANDO pasa a ser la que SALE (el usuario la abandono a
// mitad de apertura): encoge de vuelta hacia su icono desde donde este.
static void appTrDemoteIn(){
  if(!gTrIn.on) return;
  gTrOut = gTrIn;
  appTrAim(&gTrOut, 0.0f, appTrCloseMs());
  gTrIn.on = false;
}
// Rellena una capa con los datos de dibujo de una app.
static void appTrFill(AppTrLayer* L, int id){
  L->app = id;
  getIconRect(id, L->ix, L->iy, L->is);
  L->bg  = (APP_REG[id].flags & APP_CUSTOM_HEADER) ? TH_PAGE : WIN_BG;
  L->by0 = 0; L->by1 = -1;
}
// APERTURA. El estado logico ya lo cambio enterApp(); aqui solo empieza la capa.
static void appTrBeginOpen(int id, bool resuming, uint8_t prevLife){
  dgIntent("abrir"); dgLogic();
  gTrGen++;
  appTrDemoteIn();
  appTrFill(&gTrIn, id);
  gTrIn.on = false;
  appTrAim(&gTrIn, 1.0f, ATR_OPEN_MS);
  gTrEnterPending = true; gTrEnterGen = gTrGen;
  gTrEnterResume = resuming; gTrPrevLife = prevLife;
  gTrFirst = true;
}
// CIERRE. La tarjeta parte de pantalla completa (o del punto donde estuviera la
// apertura que se acaba de abandonar) y encoge hacia el icono de la app.
static void appTrBeginClose(int id){
  dgIntent("cerrar"); dgLogic();
  gTrGen++;
  gTrEnterPending = false;
  if(gTrIn.on && gTrIn.app == id){
    appTrDemoteIn();                       // continua desde su progreso real
  } else {
    appTrDemoteIn();                       // lo que hubiera entrando, que se retire
    appTrFill(&gTrOut, id);
    gTrOut.p0 = 1.0f; gTrOut.p1 = 1.0f; gTrOut.on = true;
    gTrOut.t0us = (uint32_t)micros(); gTrOut.durus = 0;
    appTrAim(&gTrOut, 0.0f, appTrCloseMs());
  }
  gGbFireVel = 0;                         // la velocidad se consume una sola vez
  gTrFirst = true;
}
// Corta toda transicion sin dibujar nada (cambios de pantalla que no son
// Inicio: Recientes, bloqueo, OTA, restablecimiento...). Nadie queda a medias.
static void appTrCancel(){
  gTrGen++;
  gTrIn.on = gTrOut.on = false;
  gTrEnterPending = false;
  gTrFirst = false;
}
// Ultimo cuadro de una apertura: la app ya ocupa la pantalla, asi que ahora --
// y no antes -- se construye su contenido. Si la intencion cambio por el
// camino (generacion vieja, o el estado logico ya no apunta a esta app), no se
// abre nada: una transicion reemplazada no puede resucitar.
static void appTrFinishOpen(){
  int id = gTrIn.app;
  bool resuming = gTrEnterResume;
  bool valid = gTrEnterPending && gTrEnterGen == gTrGen;
  gTrIn.on = false; gTrOut.on = false; gTrEnterPending = false;
  if(!valid || id < 0 || id >= APP_N) return;
  if(gState != ST_APP || gAppId != id) return;
  gLand = false;
  gClipX0 = 0; gClipX1 = SCR_W - 1; gClipY0 = 0; gClipY1 = SCR_H - 1;
  setBuf(fb);
  const AppHooks* h = appHooks(id);
  if(resuming && !(h && h->resume)) resuming = false;
  if(!(APP_REG[id].flags & APP_CUSTOM_HEADER)){   // apps normales: marco estandar
    appDrawChrome(id);
    appDrawHeader(id);
  }
  // HUELLA MEDIDA. La PSRAM libre se lee justo antes y justo despues de
  // construir la app: en esa ventana no corre nada mas, asi que la diferencia
  // es suya. Es la unica cifra de consumo que ensena Recientes, y por eso no
  // hay ninguna tabla de "consumo tipico" en el sistema.
  uint32_t psBefore = memFreePsram();
  if(resuming) h->resume();                       // reanuda: mismo contenido, misma posicion
  else if(APP_REG[id].enter) APP_REG[id].enter(); // la app pinta su contenido
  appMemCommit(id, psBefore);
  gAppShed[id] = false;                           // vuelve a tener lo suyo cargado
  gAppState[id] = ALIFE_RUNNING;
  gAppSeenMs[id] = millis();
  flxFlushAll();
}
// UN cuadro de la transicion. Es el UNICO sitio que dibuja mientras hay capas
// vivas (igual que qsTick con la cortina), asi que no puede haber dos duenos de
// la pantalla ni bandas de nadie mas colandose entre medias.
static void appTrTick(){
  if(!appTrVisible()) return;
  uint32_t now = (uint32_t)micros();
  uint32_t dgT0 = now; (void)dgT0;
  float pi = 0, po = 0;
  int ix0 = 0, iy0 = 0, ix1 = 0, iy1 = 0, irad = 0;
  int ox0 = 0, oy0 = 0, ox1 = 0, oy1 = 0, orad = 0;
  if(gTrIn.on){  pi = appTrP(&gTrIn,  now); appTrRect(&gTrIn,  pi, ix0, iy0, ix1, iy1, irad); }
  if(gTrOut.on){ po = appTrP(&gTrOut, now); appTrRect(&gTrOut, po, ox0, oy0, ox1, oy1, orad); }
  // Banda del cuadro: union de lo que cada capa ocupaba y de lo que ocupa ahora.
  int b0 = 0x7FFF, b1 = -1;
  if(gTrIn.on){
    if(iy0 < b0) b0 = iy0; if(iy1 > b1) b1 = iy1;
    if(gTrIn.by1 >= gTrIn.by0){ if(gTrIn.by0 < b0) b0 = gTrIn.by0; if(gTrIn.by1 > b1) b1 = gTrIn.by1; }
  }
  if(gTrOut.on){
    if(oy0 < b0) b0 = oy0; if(oy1 > b1) b1 = oy1;
    if(gTrOut.by1 >= gTrOut.by0){ if(gTrOut.by0 < b0) b0 = gTrOut.by0; if(gTrOut.by1 > b1) b1 = gTrOut.by1; }
  }
  if(gTrFirst){ b0 = 0; b1 = SCR_H - 1; gTrFirst = false; }
  if(b0 < 0) b0 = 0;
  if(b1 > SCR_H - 1) b1 = SCR_H - 1;
  if(b1 < b0){ b0 = 0; b1 = SCR_H - 1; }
  // Recorte completo: la transicion se dibuja encima de CUALQUIER cosa y alguna
  // app pudo dejar una banda estrecha activa para su lista con scroll.
  int sc0 = gClipY0, sc1 = gClipY1, sx0 = gClipX0, sx1 = gClipX1;
  gClipX0 = 0; gClipX1 = SCR_W - 1; gClipY0 = b0; gClipY1 = b1;
  setBuf(bbuf);
  for(int j = b0; j <= b1; j++)                   // Inicio detras: superficie estable
    memcpy(bbuf + (size_t)j * SCR_W, homeBuf + (size_t)j * SCR_W, (size_t)SCR_W * 2);
  // ORDEN: primero la saliente, encima la entrante. Es la prioridad que pide el
  // encargo y la que se ve en el video (la nueva tapa a la que se va).
  if(gTrOut.on){
    uint8_t a = appTrAlpha(po, true);
    if(a == 255) fillRoundRect (ox0, oy0, ox1 - ox0, oy1 - oy0, orad, gTrOut.bg);
    else if(a)   fillRoundRectA(ox0, oy0, ox1 - ox0, oy1 - oy0, orad, gTrOut.bg, a);
  }
  if(gTrIn.on){
    uint8_t a = appTrAlpha(pi, false);
    if(a == 255) fillRoundRect (ix0, iy0, ix1 - ix0, iy1 - iy0, irad, gTrIn.bg);
    else if(a)   fillRoundRectA(ix0, iy0, ix1 - ix0, iy1 - iy0, irad, gTrIn.bg, a);
  }
  present(b0, b1);
  gClipY0 = sc0; gClipY1 = sc1; gClipX0 = sx0; gClipX1 = sx1;
  setBuf(fb);
  gTrIn.by0  = iy0; gTrIn.by1  = iy1;
  gTrOut.by0 = oy0; gTrOut.by1 = oy1;
  dgFrame((uint32_t)micros() - dgT0);
  // Finalizaciones, SIEMPRE por tiempo (nunca por numero de cuadros).
  if(gTrOut.on && (now - gTrOut.t0us) >= gTrOut.durus && gTrOut.p1 <= 0.0f) gTrOut.on = false;
  if(gTrIn.on  && (now - gTrIn.t0us)  >= gTrIn.durus  && gTrIn.p1  >= 1.0f) appTrFinishOpen();
  if(!appTrVisible()) dgReport();
}

static void appTick(){
  // APERTURA EN VUELO: enter()/resume() todavia no ha corrido, asi que la app no
  // tiene ni estado ni pixeles. Llamar a su tick() aqui seria usarla sin
  // construir. Se atiende solo el gesto de salida -- el usuario puede abandonar
  // una apertura a mitad de camino, que es medio test del video.
  if(gTrEnterPending){
    if(gNavMode == 1 && handleiOSGestures()) return;
    if(navBarHandle()) return;
    return;
  }
  if(gLand){ if(APP_REG[gAppId].tick) APP_REG[gAppId].tick(); return; }  // Modo PC / Juegos: gestionan todo por su cuenta
  if(gNavMode == 1 && handleiOSGestures()) return;   // gestos iOS: swipe-arriba -> Home/multitarea
  // BARRA DE NAVEGACION DEL SISTEMA. Va lo PRIMERO y para TODAS las apps,
  // incluidas las APP_OWN_TOUCH: la franja de abajo es del sistema, no de la
  // app, y el toque que cae ahi no se filtra nunca hacia el tick de la app.
  if(navBarHandle()) return;
  // Chevron "atras" de la cabecera estandar: misma logica que el boton atras.
  if(T.tap && !(APP_REG[gAppId].flags & APP_CUSTOM_HEADER) && T.y <= WIN_TOP && T.x < 72){
    sysBack(); return;
  }
  if(APP_REG[gAppId].tick) APP_REG[gAppId].tick();
}

// ESTADO logico de "estar en Inicio", SIN dibujar. Lo comparten enterHome() (que
// ademas vuelca el escritorio) y appClose() (que deja el dibujo a la capa de
// transicion). Separarlos es lo que permite que Inicio sea el destino logico
// -- y por tanto interactivo -- desde el primer instante del cierre, en vez de
// desde el ultimo cuadro de la animacion.
static void enterHomeState(){
  if(hcActive) hcClose(true);      // vuelta al escritorio desde CUALQUIER ruta: sin restos ni fugas
  gIconOvrApp = -1;               // el origen prestado por la caja de apps caduca aqui (ver getIconRect)
  qsForceClose();                 // volver al escritorio nunca deja la cortina a medias
  gState = ST_HOME; lockOff = 0; lastLockOff = -1;
  // Antes se volcaba homeBuf tal cual. Si venias de Ajustes de cambiar idioma,
  // formato de hora, Liquid Glass o estilo de iconos, homeBuf seguia siendo el
  // ANTERIOR y el escritorio contradecia al ajuste que acababas de tocar,
  // hasta que el reloj cambiaba de minuto y lo repintaba por su cuenta.
  if(gHomeDirty) renderHome();
}
static void enterHome(){
  appTrCancel();                  // cualquier capa de transicion muere aqui: se vuelca Inicio entero
  enterHomeState();
  blitToFb(homeBuf); flxFlushAll();
}

// Gestos de la barra inferior (modo iOS). Solo actua si el toque EMPEZO en los
// ultimos ~44 px de la pantalla (la zona de la barra). Al soltar:
//   · deslizamiento hacia arriba rapido (<300 ms) -> Home
//   · deslizamiento hacia arriba mantenido (>=300 ms) -> App Switcher
// Devuelve true si consumio el gesto (para que el tick no siga procesando).
// BARRA DE GESTOS  ·  reconocimiento DURANTE el arrastre
// ---------------------------------------------------------------------------
// Antes esto solo miraba T.released: el gesto no existia hasta soltar y hasta
// entonces el destino logico seguia siendo la app. Con la animacion ademas
// bloqueando, el usuario tenia que esperar dos veces. Ahora:
//
//  · La franja inferior VIGILA desde que el dedo se apoya, pero NO reclama el
//    toque hasta que hay recorrido vertical real (GB_CLAIM_DY). Antes de eso la
//    app sigue recibiendo sus eventos, asi que un toque pequeno en los 44 px de
//    abajo -- un boton, una tecla, la barra de herramientas de Paint -- sigue
//    funcionando exactamente igual y NO cierra la app.
//  · En cuanto se reclama, la secuencia entera es de la barra: ni un evento mas
//    se filtra a la app.
//  · Un FLICK hacia arriba (recorrido + velocidad, antes de GB_RECENTS_MS)
//    resuelve Home EN EL ACTO, con el dedo todavia apoyado. Ese es el caso del
//    video: el destino logico pasa a Inicio y la app saliente se queda solo
//    como capa visual.
//  · Un arrastre LENTO o mantenido conserva el comportamiento de siempre: se
//    resuelve al soltar, y >= GB_RECENTS_MS abre Recientes.
//  · Al resolverse, sysHome()/sysRecents() llaman a touchDropAll(): el resto de
//    ESE contacto no puede pulsar un icono de Inicio por accidente, y el candado
//    se suelta solo cuando el dedo se levanta de verdad -- asi que un "down"
//    NUEVO entra sin ningun retraso. No hay bloqueo por tiempo en ningun sitio.
#define GB_STRIP_H     44         // franja viva de la barra de gestos
#define GB_CLAIM_DY    12         // recorrido a partir del cual la barra reclama el toque
#define GB_HOME_DY     30         // recorrido minimo para reconocer Home
#define GB_FLICK_VEL -0.35f       // px/ms hacia arriba: por encima de esto es un flick
#define GB_RECENTS_MS 300         // mantenido por encima de esto -> Recientes
#define GB_VEL_TAU    40.0f       // constante de tiempo del filtro de velocidad
static bool     gGbWatch = false; // el dedo bajo en la franja (aun no reclamado)
static bool     gGbOwn   = false; // la barra es la duena de esta secuencia
static bool     gGbFired = false; // ya se resolvio (Home / Recientes)
static int      gGbY0 = 0, gGbLastY = 0;
static uint32_t gGbT0 = 0, gGbLastMs = 0;
static float    gGbVel = 0;       // px/ms; negativa = hacia arriba

static void gbReset(){ gGbWatch = gGbOwn = gGbFired = false; gGbVel = 0; }

static bool handleiOSGestures(){
  if(gNavMode != 1){ gbReset(); return false; }
  // FASE 4: en kiosco los gestos de la barra (Home y switcher) se ignoran. Se
  // devuelve false, no true: asi appTick sigue llamando al tick de la app y esta
  // no se congela -- solo pierde la via de escape.
  if(KIOSK_ON && kioskOn){ gbReset(); return false; }

  if(T.pressed && T.y > SCR_H - GB_STRIP_H){          // el dedo entra en la franja
    gGbWatch = true; gGbOwn = false; gGbFired = false;
    gGbY0 = gGbLastY = T.y; gGbT0 = gGbLastMs = millis(); gGbVel = 0;
    return false;                                     // todavia NO se reclama nada
  }
  if(!gGbWatch) return false;

  if(T.down){
    uint32_t now = millis();
    uint32_t dt = now - gGbLastMs; if(dt < 1) dt = 1; if(dt > 100) dt = 100;
    float inst = (float)(T.y - gGbLastY) / (float)dt;         // negativa hacia arriba
    gGbVel += (inst - gGbVel) * ((float)dt / ((float)dt + GB_VEL_TAU));
    gGbLastY = T.y; gGbLastMs = now;
    int dy = gGbY0 - T.y;                                     // positivo si el dedo subio
    if(!gGbOwn){
      if(dy < GB_CLAIM_DY) return false;                      // aun es de la app
      gGbOwn = true;                                          // a partir de aqui, de la barra
    }
    if(gGbFired) return true;                                 // resuelto: se traga el resto
    // FLICK: recorrido suficiente Y velocidad hacia arriba, dentro de la ventana
    // rapida. Se resuelve YA, sin esperar a soltar.
    if(dy > GB_HOME_DY && gGbVel <= GB_FLICK_VEL && (now - gGbT0) < GB_RECENTS_MS){
      gGbFired = true;
      gGbFireVel = -gGbVel;
      if(gState == ST_APP) sysHome();                         // suspende, no cierra
      else                 enterHome();
    }
    return true;
  }

  // Soltar. Un gesto que no llego a flick se resuelve aqui, igual que siempre.
  if(!gGbFired && gGbOwn){
    int dy = gGbY0 - gGbLastY;
    unsigned long dur = millis() - gGbT0;
    if(dy > GB_HOME_DY){
      gGbFireVel = (gGbVel < 0) ? -gGbVel : 0.0f;
      if(dur >= GB_RECENTS_MS)  sysRecents();   // mantener -> Recientes (suspende la app antes)
      else if(gState == ST_APP) sysHome();      // rapido en app -> escritorio (suspende, no cierra)
      else                      enterHome();    // rapido en Home -> refresca
    }
  }
  bool owned = gGbOwn;
  gbReset();
  return owned;                                 // si la barra lo reclamo, no se filtra a nadie
}
