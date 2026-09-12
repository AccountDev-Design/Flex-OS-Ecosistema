// #############################################################
// ##  FLEX OS ULTRA  ·  PANTALLAS, MAQUINA DE ESTADOS Y ARRANQUE
// ##  ----------------------------------------------------------
// ##  El enum de gState (ST_*), las cadenas de la interfaz, el splash y el
// ##  OOBE. Es el esqueleto que despacha loop() en el .ino.
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
#include "FlexOS_Ultra_Clock.h"   // eslabon anterior de la cadena

// #############################################################
// ##  PANTALLAS + MAQUINA DE ESTADOS + ARRANQUE  (original)
// #############################################################

// ---- Declaraciones adelantadas ----
static void blitToFb(uint16_t* src);
static const char* resetReasonStr();
static void showBootBanner();
static void splashFrame(uint8_t a);
static void splashTick();
static void enterOobeLang();  static void renderOobeLang();  static void oobeLangTick();
static void enterOobeName();  static void drawKeyboard();    static void drawNameField();
static bool hitKey(int px, int py, int &code); static void oobeNameTick();
static void accountOobeEnter(); static void accountOobeTick(); static void accountStoreEnter();
// Flex Account tiene TRES vias de entrada (primer arranque, boton Cuenta de
// Flex Store y Ajustes > General) y una de retorno desde el configurador de
// Wi-Fi que conserva la via original. El texto de la fila de Ajustes lo da
// accountSettingsText, con el estado REAL de la vinculacion.
static void accountSettingsEnter(); static void accountResumeEnter();
static void accountSettingsText(char* out, size_t n);
static void wifiOobeEnter();
static void buildLongDate(char* out, size_t n); static void buildShortDate(char* out, size_t n);
static void renderLock(); static void showLock();
// ---- App Clima: lo que necesitan el Bloqueo y el escritorio. El resto de la
//      app vive en su propia seccion, mucho mas abajo. Solo primitivos en la
//      firma, como exige el auto-prototipado de Arduino.
static void wxDrawIcon(int cx, int cy, int r, uint8_t vis, bool day, uint8_t a);
static void wxHomeWidget(int x, int y, int w, int h, bool wide);
static void wxLockCard(int y);
static int  wxDrawTemp(int x, int y, float t, int size, uint16_t col, uint8_t a);   // numero + simbolo de grado
static const char* wt(int id);                                                     // textos de Clima (5 idiomas)
// Indices de esos textos. Van AQUI y no en la seccion de Clima porque el
// widget colocable del escritorio (WG_CLIMA) se dibuja mucho antes en el
// fichero; la TABLA de cadenas si vive con la app.
enum { WT_TITLE = 0, WT_NODATA, WT_NODATA_SUB, WT_RETRY, WT_ADDLOC, WT_MYLOCS, WT_SEARCH,
       WT_SEARCH_HINT, WT_NORESULT, WT_SEARCHING, WT_HOURLY48, WT_FORECAST, WT_SUNSET_CARD,
       WT_SUNRISE, WT_SUNSET, WT_HUMIDITY, WT_WIND, WT_UV, WT_VISIBILITY, WT_PRECIP,
       WT_FEELS, WT_PRESSURE, WT_NA, WT_UPDATED_AGO, WT_UPDATED_AT, WT_OFFLINE, WT_UPDATING,
       WT_ATTRIB, WT_TODAY, WT_PULL, WT_RELEASE, WT_FAILREFRESH, WT_LOADING, WT_GUST,
       WT_DELETE, WT_NOLOCS, WT_NOW, WT_NSTR };

static void renderHome(); static void showHome(); static bool hitHomeIcon(int px, int py, int &id);
static void enterHome();  static void enterApp(int id); static void appTick();
static void swPushAndCapture(uint8_t id); static void activarMultitarea(); static void swTick();  // App Switcher
static void swPushNoThumb(uint8_t id);   // apps landscape: sin miniatura (ver appClose)
static void lsuEnter(); static void lsuTick();             // Seguridad -> Bloqueo (PIN/Contraseña)
static void lsuStartVerify();                              // pedir PIN/contraseña al desbloquear
static void composeUnlock(int off); static void animateTo(int from, int to);
static void lockTick(); static void homeTick();
static bool handleiOSGestures();                          // gestos de la barra inferior (modo iOS)
static void lsuStartVerifyFor(int what, int id);          // FASE 3: verificar y luego hacer algo (abrir app, kiosco...)
static void ctxOpen(int slot); static void ctxTick();     // FASE 2: menu contextual de long-press
// CAJA DE APLICACIONES (estilo One UI). Se define abajo del todo -- necesita el
// registro, enterApp y el escritorio ya pintados -- pero homeTick() (que abre
// con el gesto) y loop() (que la anima) estan antes. Firmas con tipos
// primitivos, como exige el auto-prototipado de Arduino.
static void drawerOpen();                                 // gesto hacia arriba desde Inicio
static void drawerTick();                                 // su tick, desde loop() (ST_DRAWER)
static bool drawerCanOpen();                              // false si manda un overlay global
static void notifPauseForDrawer();                        // limpia la isla sin perder su cola
static void kioskShowBadge();                             // FASE 4: candado discreto de "kiosco activo"
static void kioskSetTick();                               // FASE 4: pantalla de definir el area excluida
static void kioskExitNow();                               // FASE 4: salida ya verificada
static void poffEnter();                                  // APAGADO: pantalla de confirmacion ("desliza para apagar")
static void poffTick();                                   // APAGADO: arrastre del slider + cancelar
static void poffBeginAnim();                              // APAGADO: confirmado -> arranca la animacion final
static void poffAnimTick();                               // APAGADO: un paso de la animacion (fundido + "Flex OS" + deep sleep)
// SSID activo como char*, SIN String. WiFi.SSID() devuelve String y el widget de
// Wi-Fi del escritorio se refresca cada 2 s: no puede depender de una asignacion
// dinamica. El sistema solo se conecta a la red GUARDADA, asi que wifiSavedSSID
// -- que ya es un char[33] del bloque Wi-Fi -- es el dato correcto y real.
static const char* wifiActiveSSID();
// CRONOMETRO. El modulo entero se define abajo del todo (necesita el motor
// grafico completo, el marco de apps y el registro), pero el escritorio
// (renderHome), el marco de las apps (appDrawChrome), la app Reloj y loop()
// -- todos ANTES en el fichero -- necesitan estos puntos de entrada. Firmas
// con tipos primitivos, como exige el auto-prototipado de Arduino.
static bool cronoActive();                                // corriendo o pausado
static uint32_t cronoElapsed();                           // UNICA fuente del tiempo transcurrido
static void cronoBarClock(int y, uint16_t col);           // hora + capsula de la barra de estado
static void cronoCapsuleTick();                           // repinta la capsula al cambiar el segundo
static void cronoOverlayTouch();                          // capsula + tarjeta: se quedan el toque
static bool cronoCardVisible();                           // hay tarjeta expandida en pantalla
static void cronoCardTick();                              // anima y compone la tarjeta expandida
static void cronoTabRender();                             // pestana Cronometro de Reloj (sin flush)
static void cronoTabTick();                               // su tick (esfera ~30 fps + botones)
// Texto recortado por la derecha. Se define con el resto de Ajustes.
static int  drawTextClip(int x, int y, const char* s, int size, uint16_t col, int maxRight);
static void kbsEnter();                                   // FASE E: Ajustes -> Teclado (pantalla propia)
static void kbsTick();                                    // FASE E: su tick, desde loop()
static void noteRenderAll();                              // Notas: repintado completo (lo llaman el portapapeles y los chips)
static void noteInsert(const char* s);                    // Notas: insercion en el cursor (la usa el portapapeles)
// ---- Navegacion inferior, ciclo de vida y sesiones (bloque nuevo) ----
static void sysBack();                                    // boton flecha: capa -> pantalla interna -> Home
static void sysHome();                                    // boton circulo: al escritorio, suspendiendo la app
static void sysRecents();                                 // boton cuadrado: gestor de apps recientes
static bool navBarHandle();                               // reparte el toque entre los tres botones
static int  navBarH();                                    // alto reservado abajo (0 en modo gestos)
static bool navBarVisible();                              // el sistema esta dibujando la barra ahora mismo
static void navStampBar(int y0, int y1);                  // la estampa en fb dentro de flxFlush (un solo propietario)
static void touchDropAll();                               // corta el episodio tactil en curso (sin toques fantasma)
static const AppHooks* appHooks(int id);
static void appLoadSessionOnce(int id);                   // relee de disco la sesion de una app (una vez por arranque)
static void sessAutosaveTick();                           // guardado diferido por inactividad
static void sessMarkDirty(int id);                         // la app cambio: rearma el temporizador de guardado
static void swCloseAll();                                 // "Cerrar todo" de Recientes
static void swSyncFromLife();                             // deja las tarjetas y el ciclo de vida coherentes
static void frEnterWizard();                              // Ajustes -> General -> Restablecer datos de fabrica
static void frTick();                                     // asistente + motor de borrado por etapas
static void frResumeAfterBoot();                          // retoma un borrado interrumpido
static void frAfterVerify();                              // seguridad superada -> ultimo paso
static void frCancelToSettings();                         // cancelar en cualquier pantalla del asistente
static void safeToastTick();                              // retira solo el aviso de Modo seguro
static void safeEnter();                                  // pantalla de Modo seguro
static void safeTick();
static bool safeAppAllowed(int id);                       // que apps se pueden abrir en Modo seguro
static void safeDenyApp(int id);                          // aviso real al intentar abrir una app no permitida
static void appClose();                                   // salir de la app al escritorio (suspendiendo)
// ---- Gestor de memoria (bloque grande, mas abajo) ----
static void sysNotify(const char* title, const char* sub);  // aviso REAL por la isla de notificaciones
static void appMemForget(int id);                           // olvida la huella medida de una app cerrada
static void memTick();                                      // muestreo periodico (lo llama loop)
static void memAlertTick();                                 // avisos de memoria con enfriamiento
static bool wifiRadioBusy();                                // hay una operacion de radio en vuelo
// ---- Optimizar Flex OS (motor junto a themeChanged: ve todos los buffers) ----
// Se abre SOLO desde Almacenamiento -> Detalles de memoria y sistema. Ya no
// vive en Recientes: alli era un boton permanente para un problema que casi
// nunca existe, y el sistema ahora libera memoria por su cuenta (memAlertTick).
static void optStart();                                     // arranca la secuencia (no bloquea)
static bool optActive();                                    // el panel esta a la vista
static void optTick();                                      // una etapa por vuelta de loop
// TRANSICIONES INTERRUMPIBLES (ver el bloque grande mas abajo). Se declaran
// aqui porque homeTick(), loop() y las salidas de app las llaman antes de que
// el motor este definido -- y porque el auto-prototipado de Arduino no genera
// prototipos para funciones que usan tipos declarados despues.
static void enterHomeState();                             // estado logico de Inicio, SIN dibujar
static bool appTrVisible();                               // hay capa de transicion en pantalla
static bool appTrOwnsScreen();                            // ...y por tanto es la unica que dibuja
static void appTrBeginOpen(int id, bool resuming, uint8_t prevLife);
static void appTrBeginClose(int id);
static void appTrCancel();                                // corta toda capa de transicion
static void appTrFinishOpen();                            // ultimo cuadro: corre enter()/resume()
static void appTrTick();                                  // un cuadro de la transicion
static bool appTerminate(int id, bool force);             // cerrar de verdad: libera recursos de la app
static void appSuspend(int id, bool landscape);
static int  swCardCount();                                // Recientes: numero de tarjetas vivas
static int  swCardApp(int idx);                           // Recientes: app de una tarjeta
static void swDropCard(int idx);                          // Recientes: quita la tarjeta (no toca el ciclo de vida)
static void flexFeedWdt();                                // TWDT: se alimenta tambien en bucles largos de guardado

// ---- Estado global ----
// ST_CTX y ST_KIOSKSET son de las Fases 2 y 4. Se anaden al FINAL del enum a
// proposito: los valores de los estados anteriores no se mueven. Lo mismo vale
// para los dos estados del apagado, anadidos al final por el mismo motivo.
// ST_KBSET (Fase E del teclado) se anade tambien al final, por identico motivo.
// ST_FACTORY (asistente + borrado de Restablecer datos de fabrica) y ST_SAFE
// (Modo seguro) se anaden tambien AL FINAL, por identico motivo: ningun valor
// anterior se mueve, asi que nada de lo que ya funcionaba cambia de numero.
enum { ST_SPLASH = 0, ST_OOBE_LANG, ST_OOBE_NAME, ST_LOCK, ST_HOME, ST_APP, ST_SWITCHER, ST_LOCKSETUP, ST_WIFI, ST_CTX, ST_KIOSKSET,
       ST_POWEROFF_CONFIRM, ST_POWEROFF_ANIM, ST_KBSET,
       // Anadidos AL FINAL por el mismo motivo que los anteriores: no mover
       // el valor de ningun estado ya existente.
       ST_CONN,        // Conectividad: Wifi / BLE / Modo avion
       ST_FILES,       // Explorador de archivos real
       // FLEX VAULT (Carpeta segura). Estado propio, y no una app de APP_REG,
       // para que no pueda aparecer en Recientes, en el buscador de Modo PC ni
       // debajo del panel rapido y la isla de notificaciones. Ver la cabecera
       // del bloque FLEX VAULT - INTERFAZ.
       ST_VAULT,
       // CAJA DE APLICACIONES. Estado propio y anadido AL FINAL, por el mismo
       // motivo que todos los anteriores: ningun valor ya existente se mueve.
       // Es un estado y no un modo dentro de ST_HOME porque tiene su propio
       // gesto, su propio scroll y su propio menu contextual, y porque asi la
       // isla de notificaciones y el panel rapido -- que se apagan solos fuera
       // de ST_HOME -- no compiten por el mismo framebuffer.
       ST_DRAWER,
       // MODO PERSONALIZACION DEL INICIO. Estado propio y anadido AL FINAL, por
       // el mismo motivo que todos los anteriores: ningun valor ya existente se
       // mueve. Es un estado y no un modo dentro de ST_HOME porque tiene su
       // propio dibujo a pantalla completa, sus propios gestos y su propio
       // ciclo de vida, y porque asi la isla de notificaciones y el panel
       // rapido -- que se apagan solos fuera de ST_HOME -- no compiten por el
       // mismo framebuffer.
       ST_HOMECFG,
       // FLEX ACCOUNT. Se anade AL FINAL: conserva todos los IDs anteriores.
       ST_OOBE_ACCOUNT,
       // Recuperacion y restablecimiento siempre al final para conservar ABI.
       ST_FACTORY, ST_SAFE };

static int  gState = ST_SPLASH;
static unsigned long splashStart = 0;
static int  lockOff = 0, lastLockOff = -1;
static int  oobeSel = 0;
static int  gAppId  = 0;
static bool editMode = false;                                   // Modo Edicion del Home
