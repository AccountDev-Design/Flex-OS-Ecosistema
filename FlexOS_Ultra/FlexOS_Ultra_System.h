// #############################################################
// ##  FLEX OS ULTRA  ·  SERVICIOS DEL SISTEMA  ·  I2C, caches, optimizar y tema
// ##  ----------------------------------------------------------
// ##  Deteccion incremental de hardware I2C, memShedSystem() (soltar solo
// ##  lo que el sistema sabe reconstruir), la maquina de etapas de
// ##  Optimizar Flex OS y themeChanged(), el punto unico al que llaman
// ##  todos los sitios que cambian el tema.
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
#include "FlexOS_Ultra_AppChrono.h"   // eslabon anterior de la cadena

// #############################################################
// ##  DETECCION DE HARDWARE I2C  (FASE 2)
// ##  ------------------------------------------------------
// ##  CLAVE DE SEGURIDAD: el escaneo corre en el MISMO contexto
// ##  que flexPollTouch() -el loop task, Core 1- llamando a
// ##  hwDetectTick() en cada vuelta. El GT911 tactil vive en el
// ##  mismo bus Wire; al no haber una segunda tarea tocando Wire,
// ##  las transacciones NUNCA se solapan y no hace falta mutex.
// ##  (Esto es a proposito lo contrario del plan original, que
// ##  ponia una tarea de escaneo en Core 1: eso compartia Wire
// ##  con el tactil sin proteccion -> corrupcion del bus/crash.)
// ##
// ##  Ademas el barrido es INCREMENTAL: sondea I2C_SCAN_PER_TICK
// ##  direcciones por vuelta, para no anadir latencia perceptible
// ##  al tactil ni forzar el watchdog. Los dispositivos nuevos
// ##  avisan por la isla dinamica de la Fase 1 (notifPush).
// ##
// ##  ALCANCE HONESTO: solo I2C, que es fiable. La deteccion de
// ##  modulos por GPIO (pulsadores, HC-SR04, servos) NO se hace
// ##  aqui porque no es distinguible sin falsos positivos; esos
// ##  llegaran por asignacion manual de pines en el asistente
// ##  (Fase 3), no por auto-deteccion.
// #############################################################

// Mapea una direccion I2C a un tipo de modulo conocido
static ModuleType identifyI2CDevice(uint8_t addr){
  switch(addr){
    case 0x76: case 0x77: return MOD_BME280;    // BME280 / BMP280
    case 0x68: case 0x69: return MOD_MPU6050;   // MPU6050 / MPU9250
    default:              return MOD_I2C_GENERIC;
  }
}

// Rellena name/sub descriptivos de un modulo I2C
static void i2cDescribe(DetectedModule* m){
  switch(m->type){
    case MOD_BME280:
      snprintf(m->name, sizeof(m->name), "Sensor BME280");
      snprintf(m->sub,  sizeof(m->sub),  "I2C 0x%02X detectado", m->i2cAddr);
      break;
    case MOD_MPU6050:
      snprintf(m->name, sizeof(m->name), "MPU6050");
      snprintf(m->sub,  sizeof(m->sub),  "IMU - I2C 0x%02X", m->i2cAddr);
      break;
    default:
      snprintf(m->name, sizeof(m->name), "Dispositivo I2C");
      snprintf(m->sub,  sizeof(m->sub),  "0x%02X detectado", m->i2cAddr);
      break;
  }
}

// ¿La direccion pertenece a un chip SOLDADO en la placa?
//
// El barrido busca modulos que el usuario CONECTA; los chips que
// vienen de fabrica no son un hallazgo y avisar de ellos en cada
// arranque seria ruido. Hasta ahora solo se excluia el panel tactil;
// se anade el codec de audio, que cuelga del MISMO bus (GPIO7/8) y
// que desde que existe FlexOS_Audio ya se detecta e identifica por su
// propio camino, leyendo su registro de identificacion.
static inline bool i2cIsTouch(uint8_t addr){ return addr == gtAddr || addr == 0x5D || addr == 0x14; }
static inline bool i2cIsOnboard(uint8_t addr){
  return i2cIsTouch(addr) || addr == FLEXAUDIO_I2C_ADDR;
}

// Indice de un modulo por direccion (o -1)
static int i2cFindByAddr(uint8_t addr){
  for(int i = 0; i < detectedCount; i++)
    if(detectedModules[i].i2cAddr == addr) return i;
  return -1;
}

// Marca presencia de una direccion; si es NUEVA la registra y avisa por la isla
static void i2cOnDevicePresent(uint8_t addr){
  if(i2cIsOnboard(addr)) return;
  int idx = i2cFindByAddr(addr);
  if(idx >= 0){
    modSweepId[idx] = i2cSweepId;                 // sigue presente en este barrido
    if(!detectedModules[idx].active){             // reaparecio tras haberse desconectado
      detectedModules[idx].active = true;
      detectedModules[idx].detectedAt = millis();
      notifPush(&detectedModules[idx]);
    }
    return;
  }
  if(detectedCount >= MAX_MODULES_DETECTED) return;
  DetectedModule m;
  memset(&m, 0, sizeof(m));
  m.i2cAddr = addr;
  m.type    = identifyI2CDevice(addr);
  m.active  = true;
  m.numPins = 0;
  m.detectedAt = millis();
  i2cDescribe(&m);
  int slot = detectedCount++;
  detectedModules[slot] = m;
  modSweepId[slot] = i2cSweepId;
  notifPush(&detectedModules[slot]);
}

// Cierra un barrido completo: lo no visto -> inactivo (permite re-aviso al reconectar)
static void i2cEndSweep(){
  for(int i = 0; i < detectedCount; i++)
    if(detectedModules[i].active && modSweepId[i] != i2cSweepId)
      detectedModules[i].active = false;
  i2cSweepId++;
  i2cLastSweep = millis();
}

// Tick de deteccion I2C. Llamar en loop() en el mismo contexto que flexPollTouch.
static void hwDetectTick(){
  if(!gtOk) return;                                          // sin I2C inicializado, nada
  if(!i2cSweeping){
    if(millis() - i2cLastSweep < I2C_SWEEP_INTERVAL) return; // espera entre barridos
    i2cSweeping   = true;
    i2cScanCursor = I2C_SCAN_LO;
  }
  int probes = 0;
  while(i2cSweeping && probes < I2C_SCAN_PER_TICK){
    uint8_t addr = i2cScanCursor;
    if(!i2cIsOnboard(addr)){
      Wire.beginTransmission(addr);
      if(Wire.endTransmission() == 0) i2cOnDevicePresent(addr);   // ACK -> hay dispositivo
    }
    probes++;
    if(i2cScanCursor >= I2C_SCAN_HI){ i2cSweeping = false; i2cEndSweep(); }
    else i2cScanCursor++;
  }
}

// #############################################################
// ##  SOLTAR CACHES DEL SISTEMA  ·  memShedSystem()
// ##  ------------------------------------------------------
// ##  Vive AQUI, justo antes de themeChanged y de setup(), por el mismo
// ##  motivo que themeChanged: es el unico punto del sketch desde el que
// ##  se ven TODOS los buffers grandes (blurBg, glassBuf, la cortina, las
// ##  paginas de Ajustes, la hoja de la caja de apps, el fondo de DeX...).
// ##  El gestor de memoria, mucho mas arriba, solo la declara y la llama.
// ##
// ##  REGLA UNICA: aqui solo entra lo que el sistema sabe RECONSTRUIR SOLO.
// ##  Ni una nota, ni un dibujo, ni un ajuste, ni un archivo del usuario.
// ##  Cada bloque lleva ademas su guardia de "no mientras se este viendo":
// ##  soltar el fondo desenfocado con Recientes en pantalla cambiaria el
// ##  fondo delante del usuario a mitad de un gesto.
// ##
// ##  Devuelve los BYTES DE PSRAM realmente recuperados, medidos antes y
// ##  despues. Si no se libero nada devuelve 0 -- y entonces la interfaz
// ##  dice que no encontro nada, en vez de inventarse una cifra.
// #############################################################
static uint32_t memShedSystem(){
  FLEXDIAG_WIFI("memShedSystem: entrada");
  uint32_t before = memFreePsram();

  // Fondo desenfocado del wallpaper (768 KB). Lo usan Recientes, el bloqueo,
  // la caja de apps y el apagado: no se suelta con ninguno a la vista.
  if(blurBg && gState != ST_SWITCHER && gState != ST_LOCK && gState != ST_LOCKSETUP &&
     gState != ST_DRAWER && gState != ST_POWEROFF_CONFIRM && gState != ST_POWEROFF_ANIM){
    free(blurBg); blurBg = NULL;
  }
  // Panel rapido: panel compuesto + vidrio expandido + instantanea de la app
  // (hasta 2,3 MB). Solo con la cortina cerrada del todo.
  if(qsPanelY == 0 && !qsAnimOn){ qpFreeBuffers(); qsDirty = true; }
  // Scratch del desenfoque de Liquid Glass (768 KB). Se rehace en el
  // siguiente panel que lo necesite.
  if(glassBuf){ heap_caps_free(glassBuf); glassBuf = NULL; }
  // Tarjeta Liquid Glass cacheada. Al soltarla hay que invalidar su firma, o el
  // siguiente uso creeria que sigue siendo valida y leeria un puntero nulo.
  if(glcScratch || glcCard){
    if(glcScratch){ heap_caps_free(glcScratch); glcScratch = NULL; }
    if(glcCard){ heap_caps_free(glcCard); glcCard = NULL; }
    glcValid = false;
  }
  // Paginas de la animacion de Ajustes (768 KB x 2). Solo con Ajustes fuera de
  // primer plano: dentro estan a punto de usarse en cada cambio de categoria.
  if(!(gState == ST_APP && gAppId == IC_AJUSTES)){
    if(setPgOut){ heap_caps_free(setPgOut); setPgOut = NULL; }
    if(setPgIn){  heap_caps_free(setPgIn);  setPgIn  = NULL; }
  }
  // Hoja de la caja de aplicaciones (768 KB).
  if(drwPage && gState != ST_DRAWER){
    heap_caps_free(drwPage); drwPage = NULL; drwPageSig = -1;
  }
  // Fondo compuesto de Modo PC / DeX (768 KB), solo con DeX cerrado.
  if(gAppState[IC_MODOPC] == ALIFE_CLOSED) dexBgFree();
  // Banda estatica del deslizador de apagado.
  if(poffBand && gState != ST_POWEROFF_CONFIRM && gState != ST_POWEROFF_ANIM){
    heap_caps_free(poffBand); poffBand = NULL;
  }
  // Miniaturas de Recientes por encima del tope justo: la lista de tarjetas NO
  // se toca, solo sus capturas (las que se sueltan caen al icono de la app).
  swThumbTrim(gEffMode ? 1 : 2);

  uint32_t after = memFreePsram();
  return after > before ? (after - before) : 0u;
}

// #############################################################
// ##  OPTIMIZAR FLEX OS  ·  maquina de etapas NO bloqueante
// ##  ------------------------------------------------------
// ##  QUE HACE DE VERDAD, etapa por etapa. Nada de esto es decorado: si
// ##  una etapa no encuentra nada que soltar, la cifra final lo dice.
// ##    1. Analizando memoria       -> medida completa (bloque incluido)
// ##    2. Liberando cache temporal -> /System/Cache en flash + las caches
// ##       reconstruibles de las apps SUSPENDIDAS (miniaturas de Galeria,
// ##       fotogramas del Navegador, buffers de Multimedia)
// ##    3. Suspendiendo recursos no usados -> caches del SISTEMA
// ##       (memShedSystem) y miniaturas de Recientes sobrantes
// ##    4. Verificando estabilidad  -> vuelve a medir; si la presion sigue
// ##       alta, enciende el MODO VISUAL EFICIENTE (temporal)
// ##    5. Optimizacion finalizada  -> cifras REALES: bytes recuperados
// ##       (medidos antes/despues) y PSRAM disponible ahora
// ##
// ##  LO QUE NO HACE, A PROPOSITO: no borra notas, dibujos, ajustes,
// ##  archivos ni apps; no reinicia; no cierra la
// ##  app activa; y no toca ninguna app con trabajo real en segundo plano.
// ##
// ##  NO BLOQUEA. Una etapa por vuelta de loop, separadas por tiempo con
// ##  millis(): cero delay(), cero while() de espera. La animacion es un
// ##  repintado de la banda del panel, no de la pantalla entera.
// #############################################################
enum { OPT_IDLE = 0, OPT_ANALYZE, OPT_CACHE, OPT_SUSPEND, OPT_VERIFY, OPT_DONE };
#define OPT_STEP_MS  420           // lo que dura cada etapa a la vista
#define OPT_X   30
#define OPT_Y   214
#define OPT_W   (SCR_W - 60)
#define OPT_H   372
#define OPT_BW  180
#define OPT_BH  46
#define OPT_BX  ((SCR_W - OPT_BW) / 2)
#define OPT_BY  (OPT_Y + OPT_H - OPT_BH - 18)

static int      optStage   = OPT_IDLE;
static uint32_t optStepMs  = 0;
static uint32_t optFree0   = 0;      // PSRAM libre al empezar (para la cifra final)
static uint32_t optGained  = 0;      // bytes REALMENTE recuperados
static int      optCacheN  = 0;      // archivos de cache temporal borrados
static bool     optEffOn   = false;  // esta pasada encendio el modo eficiente

static const char* OPT_STEP_TXT[6] = {
  "", "Analizando memoria", "Liberando cach\xC3\xA9 temporal",
  "Suspendiendo recursos no usados", "Verificando estabilidad",
  "Optimizaci\xC3\xB3n finalizada"
};

static bool optActive(){ return optStage != OPT_IDLE; }

// Marca de etapa: un circulo. Relleno = hecha, contorno = en curso o pendiente.
static void optMark(int x, int cy, bool done, bool cur){
  if(done){ fillCircle(x, cy, 8, TH_OK); return; }
  drawCircle(x, cy, 8, cur ? TH_PRIM : TH_DIV);
  if(cur) fillCircle(x, cy, 4, TH_PRIM);
}

// Dibuja el panel y publica SOLO su banda. El fondo de la banda se toma de fb
// (la pantalla que hay debajo), asi que no hace falta guardar ninguna captura:
// al cerrar, el anfitrion se repinta entero por su propio camino.
static void optRender(){
  setBuf(bbuf);
  for(int j = OPT_Y - 8; j < OPT_Y + OPT_H + 8 && j < SCR_H; j++)
    if(j >= 0) memcpy(bbuf + (size_t)j * SCR_W, fb + (size_t)j * SCR_W, SCR_W * 2);
  uiSurface(OPT_X, OPT_Y, OPT_W, OPT_H, 24, UIS_ELEVATED);
  drawRoundRect(OPT_X, OPT_Y, OPT_W, OPT_H, 24, TH_BORDER);

  int y = OPT_Y + 20;
  drawTextC(SCR_W / 2, y, "Optimizar Flex OS", 3, TH_TXT);
  y += 42;
  for(int st = OPT_ANALYZE; st <= OPT_DONE; st++){
    bool done = (optStage > st) || (optStage == OPT_DONE && st == OPT_DONE);
    bool cur  = (optStage == st);
    optMark(OPT_X + 30, y + 9, done, cur);
    drawTextClip(OPT_X + 50, y, OPT_STEP_TXT[st], 2,
                 cur ? TH_TXT : (done ? TH_TXT2 : TH_MUTE), OPT_X + OPT_W - 16);
    y += 30;
  }
  y += 10;
  if(optStage == OPT_DONE){
    char l[80];
    if(optGained > 0){
      char g[24]; flexMemFmt(optGained, g, sizeof(g));
      snprintf(l, sizeof(l), "Se liberaron %s de recursos temporales.", g);
    } else {
      snprintf(l, sizeof(l), "No se encontraron recursos temporales seguros para liberar.");
    }
    drawTextClip(OPT_X + 20, y, l, 1, TH_TXT, OPT_X + OPT_W - 20); y += 20;
    if(optCacheN > 0){
      snprintf(l, sizeof(l), "Cach\xC3\xA9 temporal en almacenamiento: %d archivo%s.",
               optCacheN, optCacheN == 1 ? "" : "s");
      drawTextClip(OPT_X + 20, y, l, 1, TH_TXT2, OPT_X + OPT_W - 20); y += 20;
    }
    const FlexMemSnap* m = memSnap();
    if(m->psTotal){
      char fr[24]; flexMemFmt(m->psFree, fr, sizeof(fr));
      snprintf(l, sizeof(l), "PSRAM disponible: %s.", fr);
    } else {
      snprintf(l, sizeof(l), "PSRAM: No disponible en esta placa.");
    }
    drawTextClip(OPT_X + 20, y, l, 1, TH_TXT, OPT_X + OPT_W - 20); y += 20;
    if(optEffOn)
      drawTextClip(OPT_X + 20, y, "Modo visual eficiente activado temporalmente.", 1,
                   TH_WARN, OPT_X + OPT_W - 20);
    fillRoundRect(OPT_BX, OPT_BY, OPT_BW, OPT_BH, OPT_BH / 2, TH_PRIM);
    drawTextC(SCR_W / 2, OPT_BY + (OPT_BH - 18) / 2, "Hecho", 2, TH_ONACC);
  } else {
    drawTextClip(OPT_X + 20, y, "No se borran notas, dibujos ni archivos.", 1,
                 TH_MUTE, OPT_X + OPT_W - 20);
  }
  present(OPT_Y - 8, OPT_Y + OPT_H + 7 < SCR_H ? OPT_Y + OPT_H + 7 : SCR_H - 1);
  setBuf(fb);       // el destino vuelve a la pantalla: nadie hereda bbuf
}

static void optStart(){
  if(optActive()) return;
  optStage  = OPT_ANALYZE;
  optStepMs = millis();
  optGained = 0; optCacheN = 0; optEffOn = false;
  memSampleNow();
  optFree0 = gMem.psFree;
  touchDropAll();                 // el toque que abrio el panel no se filtra
  optRender();
}

// Cierre: devuelve la pantalla a Almacenamiento, repintandola entera por su
// propio camino. No hay ninguna captura que restaurar y por tanto ningun
// buffer extra.
static void optFinish(){
  optStage = OPT_IDLE;
  touchDropAll();
  almRender();
}

// UNA etapa por vuelta, separada por tiempo. El trabajo de cada etapa corre una
// sola vez, al ENTRAR en ella.
static void optTick(){
  if(optStage == OPT_IDLE) return;
  if(optStage == OPT_DONE){
    if(T.tap){
      // "Hecho", o un toque fuera del panel: las dos cierran.
      optFinish();
    }
    return;
  }
  if(T.tap) touchDropAll();                      // el panel es modal mientras trabaja
  if(millis() - optStepMs < OPT_STEP_MS) return;
  optStepMs = millis();

  switch(optStage){
    case OPT_ANALYZE:
      memSampleNow();
      optStage = OPT_CACHE;
      break;
    case OPT_CACHE: {
      // 2a) cache temporal EN ALMACENAMIENTO. Es la carpeta que el propio
      //     sistema declara desechable; no se toca ninguna otra.
      optCacheN = fsWipeDir(FS_DIR_CACHE);
      // 2b) caches reconstruibles de las apps SUSPENDIDAS (miniaturas,
      //     fotogramas, buffers de medios). La app activa no se toca.
      for(int i = 0; i < APP_N; i++) memShedApp(i);
      optStage = OPT_SUSPEND;
      break;
    }
    case OPT_SUSPEND:
      memShedSystem();
      optStage = OPT_VERIFY;
      break;
    case OPT_VERIFY: {
      memSampleNow();
      // MODO VISUAL EFICIENTE: solo si la presion SIGUE alta despues de soltar.
      // Es temporal y no toca las preferencias del usuario (ver gEffMode).
      if(flexMemLevel(memSnap()) >= FLEXMEM_LV_WARN && !gEffMode){
        gEffMode = true; optEffOn = true;
        glcValid = false; gHomeDirty = true; qsDirty = true;
      }
      uint32_t now = gMem.psFree;
      optGained = (now > optFree0) ? (now - optFree0) : 0u;
      optStage = OPT_DONE;
      break;
    }
    default: break;
  }
  optRender();
}

// #############################################################
// ##  PROPAGACION DE UN CAMBIO DE TEMA  ·  themeChanged()
// ##  ------------------------------------------------------
// ##  Punto UNICO al que llaman todos los sitios que tocan gDark o uiGlass
// ##  (Ajustes -> Pantalla, Ajustes -> Personalizacion y el panel de Modo PC).
// ##  Hace las tres cosas que hacian falta para que el tema se aplique sin
// ##  reiniciar:
// ##    1) persiste por el flujo NVS de siempre (cfgSavePrefs, namespace
// ##       "flexos", mismas claves "dark"/"glass": nada que migrar);
// ##    2) invalida TODAS las caches visuales que guardan pixeles ya
// ##       tematizados -- escritorio (homeBuf), cortina del panel rapido
// ##       (qsBuf), tarjeta Liquid Glass cacheada (glcCard), fondo de Modo PC
// ##       (dexBg) y las miniaturas de Recientes, que son capturas del
// ##       framebuffer hechas con el tema ANTERIOR;
// ##    3) repinta la pantalla que este a la vista.
// ##  Lo que NO hace, a proposito: liberar y volver a reservar buffers grandes.
// ##  homeBuf, qsBuf, glcCard, dexBg y blurBg conservan su memoria (misma
// ##  geometria, mismo formato RGB565); solo se marcan como sucios para que su
// ##  proximo uso los recomponga. Cambiar de tema no mueve ni un byte de PSRAM.
// ##  Va aqui, justo antes de setup(), porque necesita ver los renderers de
// ##  todas las pantallas (Ajustes, Wi-Fi, Ajustes del teclado, Inicio...).
// #############################################################
static void themeChanged(bool save){
  if(save) cfgSavePrefs();                    // 1) NVS: mismo flujo y mismas claves de siempre

  // 2) caches visuales dependientes del tema (ninguna se libera: solo se marca)
  gHomeDirty = true;                          // homeBuf: iconos, etiquetas y widgets del escritorio
  qsDirty    = true;                          // qsBuf: la cortina se compone a partir de homeBuf
  glcValid   = false;                          // tarjeta Liquid Glass cacheada (tinte y fondo cambian)
  uiGlassBandEnd();                            // banda pre-desenfocada: lleva el tinte y el fondo VIEJOS
  dexBgWall  = 0xFF;                           // fondo de Modo PC: fuerza dexBgBuild() en el proximo frame
  // Miniaturas de Recientes: son capturas del framebuffer, o sea pixeles con el
  // tema viejo dentro. Se sueltan (y se recuperan solos la proxima vez que se
  // salga de esa app) en lugar de mostrar tarjetas con la apariencia anterior.
  for(int i = 0; i < swCount; i++)
    if(swTasks[i].thumb){ free(swTasks[i].thumb); swTasks[i].thumb = NULL; }
  // La isla dinamica recompone su tarjeta desde cero en cada aparicion, y
  // blurBg es el wallpaper desenfocado (contenido del usuario, no tema): ni una
  // ni otro necesitan invalidarse.

  // 3) repintado de la pantalla actual. Modo PC (app 4) queda fuera a
  // proposito: su propio bucle (pcTick) redibuja el escritorio entero en cada
  // frame y ya reconstruye dexBg al ver que dexBgWall cambio.
  if(gState == ST_APP && gAppId == 4) return;
  switch(gState){
    case ST_HOME:     renderHome(); showHome(); break;
    case ST_LOCK:     renderLock(); showLock(); break;
    case ST_APP:
      // Ajustes es de donde sale el 99% de los cambios de tema: se repinta con
      // su propio render, que CONSERVA la navegacion (setView), la seleccion y
      // el scroll -- volver a llamar a settingsEnter() devolveria al usuario a
      // la lista de categorias en cuanto tocara el interruptor.
      if(gAppId == 12){ settingsRender(); break; }
      // Las galerias persistentes de Notas y Paint conservan su vista actual:
      // no se vuelve a entrar a la app ni se altera el fichero del usuario.
      if(gAppId == 5){ if(noteView == 0) noteRenderList(); else noteRenderAll(); break; }
      if(gAppId == 10){ if(paintView == 0) paintRenderGallery(); else paintRenderCanvas(); break; }
      // El resto de apps se re-maquetan por la via de siempre (gRelayout), que
      // es la misma que usa Modo PC al redimensionar una ventana: enter() debe
      // RE-DIBUJAR, no re-inicializar.
      // Juegos queda fuera a proposito: su enter() vuelve al menu y un cambio
      // de apariencia no debe interrumpir la partida.
      if(gAppId != 11 && APP_REG[gAppId].enter){
        gRelayout = true; APP_REG[gAppId].enter(); gRelayout = false;
      }
      break;
    case ST_KBSET:    kbsRender(); break;
    case ST_WIFI:     wifiSettingsRepaint(); break;
    case ST_SWITCHER: swRenderCards(); break;
    case ST_FILES:    filesRender(); break;
    case ST_CONN:     connRender(); break;
    case ST_VAULT:    vaultRender(); break;
    default: break;                            // splash/OOBE/apagado: se repintan solos
  }
}
