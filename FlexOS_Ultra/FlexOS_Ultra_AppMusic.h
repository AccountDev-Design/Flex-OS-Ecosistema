// #############################################################
// ##  FLEX OS ULTRA  ·  MUSICA  ·  la biblioteca de audio y su reproductor
// ##  ----------------------------------------------------------
// ##  El audio de la biblioteca (lo que sube el movil, lo que se copia
// ##  a /Musica) y un reproductor que SIGUE SONANDO al salir de la app.
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
#include "FlexOS_Ultra_AppGallery.h"   // eslabon anterior de la cadena

// #############################################################
// ##  QUE SUENA DE VERDAD
// ##  ------------------------------------------------------
// ##  WAV PCM de 8 o 16 bits y WAV IMA ADPCM (lo que produce la web al
// ##  convertir un MP3 en el movil). Todo sale como PCM de 16 bits por
// ##  FlexAudioStream (FlexOS_Media, probado en el PC) hacia el codec
// ##  ES8311. MP3, AAC, FLAC y OGG se guardan y se descargan, pero aqui
// ##  se dice que el P4 no los reproduce: no hay decodificador.
// ##
// ##  EN SEGUNDO PLANO. El reproductor NO vive en el tick de la app:
// ##  musAudioTick() va en loop() y alimenta el DMA pase lo que pase en
// ##  pantalla. El DMA guarda ~0,4 s (flexAudioStartPcmBuffered) para que
// ##  un repintado pesado de otra app no deje el altavoz en silencio. El
// ##  codec (I2C) solo se toca desde este hilo, que es el del tactil.
// ##
// ##  LO PROTEGIDO. Sale con candado y pide la clave del sistema para
// ##  sonar. Pasar de pista no entra nunca en algo protegido, y si el P4
// ##  se bloquea con una pista protegida sonando, se para: quien tenga el
// ##  aparato no la oye ni ve su titulo.
// #############################################################
#define MUS_HEAD_H      40            // la cabecera del sistema ya dice "Musica"
#define MUS_ROW_H       64
#define MUS_BAR_H       68            // mini reproductor, abajo de la lista
#define MUS_BUF_MS      400           // DMA: lo que aguanta un repintado pesado
#define MUS_PUMP_MAX    8192          // bytes de PCM por vuelta de loop, como mucho
#define MUS_THUMB_BUDGET 6
#define MUS_UI_MS       500           // refresco de la barra de progreso

enum { MUS_LIST = 0, MUS_NOW = 1 };
static int      musScreen = MUS_LIST;
static int      musScroll = 0, musDragY0 = 0, musDragS0 = 0;
static bool     musDragging = false, musLongFired = false;
static FlexMlView musView;                      // indices en mlTables()->musView (PSRAM)
static bool     musViewReady = false;
static int      musCountCache = 0;
static bool     musMorePending = false;
static uint32_t musSeenRev = 0, musSeenMs = 0;

// ---- Reproductor ----
struct MusIo { FlexFsStream* f; uint32_t size; };
static MusIo           musIo = { NULL, 0 };
static FlexAudioStream musAs;
static uint8_t*        musWork = NULL;
static size_t          musWorkCap = 0;
static uint32_t        musId = 0;               // id del catalogo de lo cargado (0 = abierto por ruta)
static bool            musLoaded = false;       // hay pista preparada
static bool            musPlaying = false;      // suena ahora
static bool            musProtected = false;    // la pista cargada esta protegida
static uint32_t        musDrainUntil = 0;       // se entrego todo: esperar a que suene lo ultimo
static char            musPath[FML_PATH_MAX] = "";
static char            musTitle[FML_NAME_MAX] = "";
static char            musSub[FML_ARTIST_MAX + FML_ALBUM_MAX + 8] = "";
static char            musErr[96] = "";
static char            musPending[FML_PATH_MAX] = "";   // abrir al entrar (desde el Explorador)
static uint32_t        musUiMs = 0;
static bool            musVolDrag = false;

static void musRender();

static void musSyncLocked(){
  if(!musViewReady || !musView.idx){
    MlTables* t = mlTables();
    flexMlViewInit(&musView, t ? t->musView : NULL, FML_CAP, FML_MASK_AUDIO, FML_SORT_TITLE);
    musViewReady = true;
    flexMlViewSync(&musView, &gMs.lib, true);
  } else flexMlViewSync(&musView, &gMs.lib, false);
}

// -------------------------------------------------------------
//  MOTOR: archivo abierto + FlexAudioStream + DMA
// -------------------------------------------------------------
static int musIoRead(void* c, void* b, uint32_t n){ return flexFsStreamRead(((MusIo*)c)->f, b, n); }
static bool musIoSeek(void* c, uint32_t off){ return flexFsStreamSeek(((MusIo*)c)->f, off); }
static uint32_t musIoSize(void* c){ return ((MusIo*)c)->size; }
static int musSink(void*, const void* p, size_t n){ return flexAudioWrite(p, n); }

// Lo que de verdad se ha oido: lo entregado menos lo que aun espera en el DMA.
static uint32_t musPosMs(){
  if(!musLoaded) return 0;
  uint32_t p = flexAsPosMs(&musAs);
  if(musPlaying){
    uint32_t q = flexAudioBufferMs();
    if(musDrainUntil){ int32_t left = (int32_t)(musDrainUntil - millis()); q = left > 0 ? (uint32_t)left : 0; }
    p = p > q ? p - q : 0;
  }
  return p;
}

// Suelta la pista: para el audio y cierra el archivo, pero recuerda CUAL
// era (para volver a darle a reproducir). La memoria de trabajo se queda
// para la siguiente (se suelta al cerrar la app o con musShed).
static void musUnload(){
  if(musPlaying) flexAudioStop();
  musPlaying = false;
  musDrainUntil = 0;
  if(musIo.f){ flexFsStreamClose(musIo.f); musIo.f = NULL; }
  musLoaded = false;
}
// Ademas la OLVIDA: ni titulo, ni ruta. Es lo que toca cuando la pista deja
// de existir o de poder ensenarse (se borra, se bloquea, se bloquea el P4).
static void musForget(){
  musUnload();
  musId = 0; musProtected = false;
  musPath[0] = 0; musTitle[0] = 0; musSub[0] = 0;
}

// Prepara una pista. false con el motivo en musErr (y nada abierto).
static bool musLoad(uint32_t id, const char* path){
  char p[FML_PATH_MAX];
  snprintf(p, sizeof(p), "%s", path ? path : "");   // path puede ser musPath
  musForget();
  musErr[0] = 0;
  musId = id;
  snprintf(musPath, sizeof(musPath), "%s", p);
  const char* nm = strrchr(musPath, '/');
  mlCopyText(musTitle, sizeof(musTitle), nm ? nm + 1 : musPath);
  musSub[0] = 0;
  FlexMlRec r;
  bool have = id && mlGet(id, &r);
  if(have){
    flexMlDisplayName(&r, musTitle, sizeof(musTitle));
    if(r.artist[0] && r.album[0]) snprintf(musSub, sizeof(musSub), "%s  \xC2\xB7  %s", r.artist, r.album);
    else snprintf(musSub, sizeof(musSub), "%s", r.artist[0] ? r.artist : r.album);
    musProtected = (r.flags & FML_R_LOCKED) != 0;
    if(!(r.flags & FML_R_PLAYABLE)){
      const char* why = flexMlWhyUnplayable(r.fmt);
      snprintf(musErr, sizeof(musErr), "%s", why ? why : (r.state == FML_S_ERROR ? "El archivo est\xC3\xA1 da\xC3\xB1" "ado"
                                                                            : "Flex OS no puede reproducir este archivo"));
      musProtected = false;
      return false;
    }
  }
  if(!flexAudioAvailable()){
    snprintf(musErr, sizeof(musErr), "Sin salida de audio: %s", flexAudioError());
    musProtected = false;
    return false;
  }
  musIo.f = flexFsOpenRead(musPath);
  musIo.size = musIo.f ? flexFsStreamSize(musIo.f) : 0;
  if(!musIo.f){ snprintf(musErr, sizeof(musErr), "No se pudo abrir el archivo"); musProtected = false; return false; }
  FlexMediaIO io; io.read = musIoRead; io.seek = musIoSeek; io.size = musIoSize; io.ctx = &musIo;
  FlexWavInfo w;
  int rc = flexWavParse(&io, &w);
  size_t need = rc == FLEXWAV_OK ? flexAsWorkBytes(&w) : 0;
  if(rc != FLEXWAV_OK || !need){
    snprintf(musErr, sizeof(musErr), "%s", rc == FLEXWAV_ERR_CODEC ? "Este WAV usa un c\xC3\xB3" "dec que Flex OS no reproduce"
                                                                   : "El archivo de audio est\xC3\xA1 da\xC3\xB1" "ado");
    flexFsStreamClose(musIo.f); musIo.f = NULL; musProtected = false;
    return false;
  }
  if(musWorkCap < need){
    mediaFree(musWork);
    musWork = (uint8_t*)mediaAlloc(need);
    musWorkCap = musWork ? need : 0;
  }
  if(!musWork || !flexAsOpen(&musAs, &io, &w, musWork, musWorkCap)){
    snprintf(musErr, sizeof(musErr), "Sin memoria para el audio");
    flexFsStreamClose(musIo.f); musIo.f = NULL; musProtected = false;
    return false;
  }
  musLoaded = true;
  return true;
}

// Una vuelta del reproductor. Va en loop(), no en el tick de la app: la
// musica sigue en segundo plano. Nunca bloquea: entrega lo que el DMA acepte.
static void musNext(int delta, bool automatic);
static void musAudioTick(){
  // Una pista protegida no sigue sonando (ni se recuerda) con el aparato
  // bloqueado: quien lo tenga no la oye ni ve su titulo.
  if(musProtected && gState == ST_LOCK){ musForget(); return; }
  if(!musPlaying) return;
  if(musDrainUntil){
    if((int32_t)(millis() - musDrainUntil) < 0) return;
    musDrainUntil = 0;
    musNext(+1, true);                            // la siguiente de la lista (sin protegidas)
    return;
  }
  int r = flexAsPump(&musAs, musSink, NULL, MUS_PUMP_MAX);
  if(r < 0){
    musUnload();
    snprintf(musErr, sizeof(musErr), "Se perdi\xC3\xB3 el acceso al archivo");
    sysNotify("M\xC3\xBAsica", musErr);
    if(gState == ST_APP && gAppId == IC_MUSICA) musRender();
    return;
  }
  // Todo entregado: queda sonar lo que hay en el DMA.
  if(musAs.ended) musDrainUntil = (millis() + flexAudioBufferMs() + 40u) | 1u;
}

static bool musPlay(){
  if(!musLoaded) return false;
  if(musAs.ended || musDrainUntil){ musDrainUntil = 0; flexAsSeekMs(&musAs, 0); }
  if(!flexAudioStartPcmBuffered(musAs.wav.sampleRate, musAs.wav.channels, 16, MUS_BUF_MS)){
    snprintf(musErr, sizeof(musErr), "%s", flexAudioError());
    return false;
  }
  musPlaying = true;
  musAudioTick();                                 // llena el DMA ya
  return true;
}
// Pausa: se retoma desde lo que se OYO, no desde lo que se habia entregado.
static void musPause(){
  if(!musPlaying) return;
  uint32_t pos = musPosMs();
  flexAudioStop();
  musPlaying = false;
  musDrainUntil = 0;
  flexAsSeekMs(&musAs, pos);
}
static void musSeek(uint32_t ms){
  if(!musLoaded) return;
  bool was = musPlaying;
  if(was){ flexAudioStop(); musPlaying = false; }
  musDrainUntil = 0;
  flexAsSeekMs(&musAs, ms);
  if(was) musPlay();
}

// La pista que se esta tocando va a cambiar en el disco (bloquear, borrar,
// renombrar): se suelta ANTES, con el archivo aun en su sitio.
static void musBeforeChange(uint32_t id){
  if(id && id == musId) musForget();
}

// Empieza una pista: la carga, suena y se ensena "Reproduciendo".
static void musStart(uint32_t id, const char* path){
  gMlBeforeChange = musBeforeChange;
  bool ok = musLoad(id, path);
  if(ok && !musPlay()) ok = false;
  musScreen = MUS_NOW;
  musRender();
  if(!ok && musErr[0]) sysNotify("M\xC3\xBAsica", musErr);
}

// Anterior/siguiente por el orden de la lista, SALTANDO lo protegido y lo
// que el P4 no reproduce. Automatico (al acabar una pista) = sin dar la
// vuelta: al final de la lista, se para.
static void musNext(int delta, bool automatic){
  char path[FML_PATH_MAX] = "";
  uint32_t id = 0;
  mlLock();
  musSyncLocked();
  int n = musView.n;
  int cur = musId ? flexMlViewFindId(&musView, &gMs.lib, musId) : -1;
  int base = cur >= 0 ? cur : (delta > 0 ? -1 : n);
  // Una pista abierta por ruta (fuera de la biblioteca) que termina no
  // arranca la biblioteca sola.
  if(automatic && cur < 0) n = 0;
  for(int step = 1; step <= n && !id; step++){
    int k = base + delta * step;
    if(automatic && (k < 0 || k >= n)) break;
    k = ((k % n) + n) % n;
    const FlexMlRec* r = &gMs.lib.recs[musView.idx[k]];
    if((r->flags & FML_R_LOCKED) || !(r->flags & FML_R_PLAYABLE)) continue;
    if(r->id == musId && !automatic) break;       // dio la vuelta: no hay otra
    id = r->id;
    snprintf(path, sizeof(path), "%s", r->path);
  }
  mlUnlock();
  if(!id){
    if(automatic){ musUnload(); if(gState == ST_APP && gAppId == IC_MUSICA) musRender(); }
    return;
  }
  bool ok = musLoad(id, path) && musPlay();
  if(!ok && musErr[0]) sysNotify("M\xC3\xBAsica", musErr);
  if(gState == ST_APP && gAppId == IC_MUSICA) musRender();
}

// -------------------------------------------------------------
//  DIBUJO
// -------------------------------------------------------------
// Portada de una pista (la miniatura del catalogo) o NULL. Solo desde el
// hilo de la interfaz: el puntero es de la cache compartida.
static const uint16_t* musCoverOf(uint32_t id){
  if(!id) return NULL;
  const uint16_t* th = NULL;
  mlLock();
  int i = flexMlFindId(&gMs.lib, id);
  int budget = 1; bool more = false;
  if(i >= 0 && !(gMs.lib.recs[i].flags & FML_R_LOCKED)) th = mlThumbGetLocked(&gMs.lib.recs[i], &budget, &more);
  mlUnlock();
  return th;
}
// Portada al doble (132 -> 264), vecino mas cercano: nitida y sin reservar nada.
static void musBlitCover2x(const uint16_t* src, int x, int y){
  for(int ry = 0; ry < ML_SIDE * 2; ry++){
    const uint16_t* row = src + (size_t)(ry >> 1) * ML_SIDE;
    for(int rx = 0; rx < ML_SIDE * 2; rx++) px(x + rx, y + ry, row[rx >> 1]);
  }
}

static int musRowY(int i){ int bx, by, bw, bh; uiBox(bx, by, bw, bh); return by + MUS_HEAD_H + i * MUS_ROW_H - musScroll; }
static bool musShowBar(){ return musPath[0] && !mkMulti; }

static void musDrawRow(const FlexMlRec* r, int x, int y, int w, int &budget){
  int h = MUS_ROW_H - 8;
  bool sel = mkMulti && mkIsSel(r->id);
  bool cur = musLoaded && r->id == musId;
  if(sel) fillRoundRect(x, y, w, h, 12, TH_SEL);
  else if(uiGlass) drawGlassCardFlat(x, y, w, h, 12, TH_GLASS, WIN_BG);
  else        fillRoundRect(x, y, w, h, 12, TH_SURF);
  int tx = x + 8, ty = y + (h - ML_SMALL) / 2;
  if(r->flags & FML_R_LOCKED){
    fillRoundRect(tx, ty, ML_SMALL, ML_SMALL, 8, TH_SURF2);
    mlPadlock(tx + ML_SMALL / 2, ty + ML_SMALL / 2 - 2, 8, TH_TXT2, false);
    drawText(x + 64, y + (h - uiLineH(2)) / 2, "Protegido", 2, TH_TXT2);
  } else {
    bool more = false;
    const uint16_t* th = (r->flags & FML_R_THUMB) ? mlThumbGetLocked(r, &budget, &more) : NULL;
    if(more) musMorePending = true;
    if(th) mlBlitThumbSmall(th, tx, ty, 8);
    else   drawAppIcon(IC_MUSICA, tx, ty, ML_SMALL);
    char nm[FML_NAME_MAX]; flexMlDisplayName(r, nm, sizeof(nm));
    int right = x + w - (mkMulti ? 44 : cur ? 40 : 12);
    drawTextClip(x + 64, y + 8, nm, 2, cur ? TH_PRIM : TH_TXT, right);
    char sub[80], dur[16] = "";
    if(r->durMs) mlFmtDur(r->durMs, dur, sizeof(dur));
    if(r->state == FML_S_ERROR) snprintf(sub, sizeof(sub), "Archivo da\xC3\xB1" "ado");
    else if(!(r->flags & FML_R_PLAYABLE)) snprintf(sub, sizeof(sub), "%s  \xC2\xB7  Solo guardar", flexMlFmtName(r->fmt));
    else if(r->artist[0]) snprintf(sub, sizeof(sub), "%s%s%s", r->artist, dur[0] ? "  \xC2\xB7  " : "", dur);
    else snprintf(sub, sizeof(sub), "%s%s%s", flexMlFmtName(r->fmt), dur[0] ? "  \xC2\xB7  " : "", dur);
    drawTextClip(x + 64, y + 34, sub, 1, r->state == FML_S_ERROR ? TH_WARN : TH_TXT2, right);
    if(cur && !mkMulti){                                   // la que suena: tres barras
      for(int k = 0; k < 3; k++){
        int bh2 = musPlaying ? 8 + ((k * 5 + 3) % 12) : 6;
        fillRect(x + w - 32 + k * 7, y + h / 2 + 8 - bh2, 4, bh2, TH_PRIM);
      }
    }
  }
  if(mkMulti){
    int cx = x + w - 22, cy = y + h / 2;
    fillCircle(cx, cy, 11, sel ? TH_PRIM : rgb565(0, 0, 0));
    drawCircle(cx, cy, 11, rgb565(255, 255, 255));
    if(sel){
      strokeSegAA(cx - 5, cy, cx - 1, cy + 5, 2.0f, TH_ONACC);
      strokeSegAA(cx - 1, cy + 5, cx + 6, cy - 5, 2.0f, TH_ONACC);
    }
  }
}

// Boton de reproducir/pausa (tambien lo usa el mini reproductor).
static void musDrawPlayBtn(int cx, int cy, int r, bool playing){
  fillCircle(cx, cy, r, TH_PRIM);
  int s = r * 2 / 5;
  if(playing){
    fillRect(cx - s, cy - s - 2, s * 2 / 3 + 1, s * 2 + 4, TH_ONACC);
    fillRect(cx + s / 3, cy - s - 2, s * 2 / 3 + 1, s * 2 + 4, TH_ONACC);
  } else fillTriangle(cx - s + 2, cy - s - 3, cx - s + 2, cy + s + 3, cx + s + 4, cy, TH_ONACC);
}

static int musBarTop(){ int bx, by, bw, bh; uiBox(bx, by, bw, bh); return by + bh - MUS_BAR_H - 10; }
static void musDrawMiniBar(){
  int bx, by, bw, bh; uiBox(bx, by, bw, bh);
  int y = musBarTop(), x = bx + 10, w = bw - 20;
  if(uiGlass) drawLiquidGlassPanel(x, y, w, MUS_BAR_H, 20, TH_GLASS2);
  else        fillRoundRect(x, y, w, MUS_BAR_H, 20, TH_SURF2);
  const uint16_t* cov = musProtected ? NULL : musCoverOf(musId);
  int ty = y + (MUS_BAR_H - ML_SMALL) / 2;
  if(musProtected){ fillRoundRect(x + 12, ty, ML_SMALL, ML_SMALL, 8, TH_SURF); mlPadlock(x + 12 + ML_SMALL / 2, ty + ML_SMALL / 2 - 2, 8, TH_TXT2, false); }
  else if(cov) mlBlitThumbSmall(cov, x + 12, ty, 8);
  else drawAppIcon(IC_MUSICA, x + 12, ty, ML_SMALL);
  // Lo protegido no ensena su titulo en la lista, aunque suene.
  drawTextClip(x + 68, y + 14, musProtected ? "Contenido protegido" : musTitle, 2, TH_TXT, x + w - 70);
  drawTextClip(x + 68, y + 40, musPlaying ? "Sonando" : "En pausa", 1, TH_TXT2, x + w - 70);
  musDrawPlayBtn(x + w - 36, y + MUS_BAR_H / 2, 22, musPlaying);
}

static void musRenderList(){
  setBuf(fb);
  int bx, by, bw, bh; uiBox(bx, by, bw, bh);
  fillRect(bx, by, bw, bh, WIN_BG);
  int pad = uiPad();
  musMorePending = false;
  mlThumbNewPass();
  int budget = MUS_THUMB_BUDGET;
  for(int i = 0; i < 3; i++) fillCircle(bx + bw - pad - 4, by + 8 + i * 10, 3, TH_NAV);

  if(!gMlOk){
    const char* t = !flexFsReady() ? "Sin almacenamiento" : gSafeMode ? "Modo seguro" : "Biblioteca no disponible";
    drawTextC(bx + bw / 2, by + bh / 2 - 20, t, 3, TH_TXT2);
    drawTextC(bx + bw / 2, by + bh / 2 + 16, !flexFsReady() ? flexFsError() : "La biblioteca no se abre ahora", 1, TH_MUTE);
    musCountCache = 0;
    flxFlush(WIN_TOP, WIN_BOT);
    return;
  }

  mlLock();
  musSyncLocked();
  const int n = musView.n;
  musCountCache = n;
  musSeenRev = gMs.lib.rev; musSeenMs = millis();
  mkPruneLocked(&musView);
  { char cnt[48];
    if(mkMulti) snprintf(cnt, sizeof(cnt), "%u seleccionada%s", (unsigned)mkSelN, mkSelN == 1 ? "" : "s");
    else if(n == 1) snprintf(cnt, sizeof(cnt), "1 canci\xC3\xB3n");
    else snprintf(cnt, sizeof(cnt), "%d canciones", n);
    drawText(bx + pad, by + 12, cnt, 1, mkMulti ? TH_PRIM : TH_TXT2); }
  if(gMs.scanning) drawTextR(bx + bw - pad - 22, by + 12, "Buscando archivos...", 1, TH_TXT2);

  if(n == 0){
    drawTextC(bx + bw / 2, by + bh / 2 - 40, "No hay m\xC3\xBAsica", 3, TH_TXT2);
    drawTextC(bx + bw / 2, by + bh / 2 - 4, "S\xC3\xBA" "bela desde el m\xC3\xB3vil:", 1, TH_MUTE);
    drawTextC(bx + bw / 2, by + bh / 2 + 14, "men\xC3\xBA > Conectar con el m\xC3\xB3vil", 1, TH_MUTE);
    drawTextC(bx + bw / 2, by + bh / 2 + 40, "Un MP3 se convierte en el m\xC3\xB3vil a WAV, que Flex OS s\xC3\xAD reproduce", 1, TH_MUTE);
  }

  uiClipViewport(by + MUS_HEAD_H - 6, by + bh - 1);
  for(int i = 0; i < n; i++){
    int y = musRowY(i);
    if(y + MUS_ROW_H < by + MUS_HEAD_H - 6 || y > by + bh) continue;
    musDrawRow(&gMs.lib.recs[musView.idx[i]], bx + pad, y, bw - 2 * pad, budget);
  }
  uiClipFull();
  int selLocked = 0, selOpen = 0, selectable = 0;
  if(mkMulti) mkCountLocked(&musView, &selLocked, &selOpen, &selectable);
  mlUnlock();

  if(mkMulti) mkDrawBar(selLocked, selOpen, selectable);
  else if(musShowBar()) musDrawMiniBar();
  mkDrawOverlays();
  flxFlush(WIN_TOP, WIN_BOT);
}

// ---- Pantalla "Reproduciendo" ----
struct MusNowGeom { int coverX, coverY, side, titleY, barX, barW, barY, ctrlY, volY, volX, volW; };
static MusNowGeom musNowGeom(){
  int bx, by, bw, bh; uiBox(bx, by, bw, bh);
  MusNowGeom g;
  g.side = (bw - 2 * uiPad() >= ML_SIDE * 2 + 16 && bh >= 660) ? ML_SIDE * 2 : ML_SIDE;
  g.coverX = bx + (bw - g.side) / 2;
  g.coverY = by + 36;
  g.titleY = g.coverY + g.side + 18;
  g.barX = bx + 2 * uiPad() + 8; g.barW = bw - 4 * uiPad() - 16;
  g.barY = g.titleY + 78;
  g.ctrlY = g.barY + 78;
  g.volY = g.ctrlY + 74;
  g.volX = g.barX + 30; g.volW = g.barW - 60;
  return g;
}

static void musDrawProgress(bool publish){
  MusNowGeom g = musNowGeom();
  int bx, by, bw, bh; uiBox(bx, by, bw, bh);
  setBuf(fb);
  fillRect(bx, g.barY - 12, bw, 50, WIN_BG);
  uint32_t dur = musLoaded ? flexAsDurMs(&musAs) : 0, pos = musPosMs();
  if(pos > dur) pos = dur;
  fillRoundRect(g.barX, g.barY, g.barW, 6, 3, TH_TRACK);
  int fw = dur ? (int)((uint64_t)g.barW * pos / dur) : 0;
  fillRoundRect(g.barX, g.barY, fw, 6, 3, TH_PRIM);
  if(musLoaded) fillCircle(g.barX + fw, g.barY + 3, 9, TH_TXT);
  char a[16], b[16];
  mlFmtDur(pos, a, sizeof(a));
  if(dur) mlFmtDur(dur, b, sizeof(b)); else snprintf(b, sizeof(b), "--:--");
  drawText(g.barX, g.barY + 16, a, 1, TH_TXT2);
  drawTextR(g.barX + g.barW, g.barY + 16, b, 1, TH_TXT2);
  if(publish) flxFlush(g.barY - 12, g.barY + 38);
}

static void musDrawVolume(bool publish){
  MusNowGeom g = musNowGeom();
  int bx, by, bw, bh; uiBox(bx, by, bw, bh);
  setBuf(fb);
  fillRect(bx, g.volY - 16, bw, 34, WIN_BG);
  // Altavoces pequeno y grande a los lados, la barra REAL en medio.
  for(int k = 0; k < 2; k++){
    int sx = k ? g.volX + g.volW + 12 : g.barX, s = k ? 7 : 5;
    fillRect(sx, g.volY - s / 2, s, s, TH_TXT2);
    fillTriangle(sx + s, g.volY - s / 2, sx + s, g.volY + s / 2, sx + s + s + 2, g.volY - s - 2, TH_TXT2);
    fillTriangle(sx + s, g.volY + s / 2, sx + s + s + 2, g.volY + s + 2, sx + s + s + 2, g.volY - s - 2, TH_TXT2);
  }
  int v = flexAudioVolume();
  fillRoundRect(g.volX, g.volY - 2, g.volW, 5, 2, TH_TRACK);
  int fw = g.volW * v / FLEXAUDIO_VOL_MAX;
  fillRoundRect(g.volX, g.volY - 2, fw, 5, 2, TH_ACCS);
  fillCircle(g.volX + fw, g.volY, 8, TH_TXT);
  if(publish) flxFlush(g.volY - 16, g.volY + 18);
}

static void musRenderNow(){
  setBuf(fb);
  int bx, by, bw, bh; uiBox(bx, by, bw, bh);
  fillRect(bx, by, bw, bh, WIN_BG);
  MusNowGeom g = musNowGeom();
  // (Volver a la lista: la flecha de la cabecera del sistema o ATRAS.)
  drawTextC(bx + bw / 2, by + 10, "Reproduciendo", 1, TH_TXT2);

  // Portada: la del ID3 que aporto el movil, o el icono de la app. Lo
  // protegido no ensena la suya.
  const uint16_t* cov = musProtected ? NULL : musCoverOf(musId);
  if(cov && g.side == ML_SIDE * 2) musBlitCover2x(cov, g.coverX, g.coverY);
  else if(cov) mlBlitThumb(cov, g.coverX, g.coverY, g.side, g.side, 16, WIN_BG);
  else drawAppIcon(IC_MUSICA, g.coverX, g.coverY, g.side);

  const char* title = musTitle[0] ? musTitle : "Nada sonando";
  drawTextC(bx + bw / 2, g.titleY, title, 3, TH_TXT);
  if(musErr[0]) drawTextC(bx + bw / 2, g.titleY + 40, musErr, 1, TH_ERR);
  else if(musSub[0]) drawTextC(bx + bw / 2, g.titleY + 40, musSub, 1, TH_TXT2);

  if(!flexAudioAvailable()){
    // Sin codec no se pinta un reproductor que no suena: se dice por que.
    drawTextC(bx + bw / 2, g.barY + 10, "Sin salida de audio", 2, TH_WARN);
    drawTextC(bx + bw / 2, g.barY + 40, flexAudioError(), 1, TH_MUTE);
    flxFlush(WIN_TOP, WIN_BOT);
    musUiMs = millis();
    return;
  }
  musDrawProgress(false);
  int cx = bx + bw / 2;
  musDrawPlayBtn(cx, g.ctrlY, 34, musPlaying);
  for(int k = -1; k <= 1; k += 2){                         // anterior / siguiente
    int x0 = cx + k * 96;
    fillTriangle(x0 - k * 12, g.ctrlY - 12, x0 - k * 12, g.ctrlY + 12, x0 + k * 6, g.ctrlY, TH_TXT);
    fillTriangle(x0 + k * 2, g.ctrlY - 12, x0 + k * 2, g.ctrlY + 12, x0 + k * 20, g.ctrlY, TH_TXT);
    fillRect(k > 0 ? x0 + 20 : x0 - 23, g.ctrlY - 12, 3, 24, TH_TXT);
  }
  musDrawVolume(false);
  flxFlush(WIN_TOP, WIN_BOT);
  musUiMs = millis();
}

static void musRender(){
  if(webSheetIsOpen()){ webSheetRender(); return; }   // la hoja del servidor manda mientras esta abierta
  if(musScreen == MUS_NOW) musRenderNow();
  else musRenderList();
}

// -------------------------------------------------------------
//  TOQUES
// -------------------------------------------------------------
static int musListMaxScroll(){
  int bx, by, bw, bh; uiBox(bx, by, bw, bh);
  int tail = mkMulti ? MKB_H + 26 : musShowBar() ? MUS_BAR_H + 26 : 30;
  int m = MUS_HEAD_H + musCountCache * MUS_ROW_H + tail - bh;
  return m > 0 ? m : 0;
}
static uint32_t musHitId(int tx, int ty){
  int bx, by, bw, bh; uiBox(bx, by, bw, bh);
  if(ty < by + MUS_HEAD_H - 6) return 0;
  uint32_t id = 0;
  mlLock();
  musSyncLocked();
  for(int i = 0; i < musView.n && !id; i++){
    int y = musRowY(i);
    if(ty >= y && ty <= y + MUS_ROW_H - 8 && tx >= bx && tx <= bx + bw) id = gMs.lib.recs[musView.idx[i]].id;
  }
  mlUnlock();
  return id;
}
static void musSelectAll(){
  mlLock();
  musSyncLocked();
  mkSelectAllLocked(&musView);
  mlUnlock();
  musRender();
}
// Abrir desde la lista (ya autorizado si estaba protegido): suena y se ve.
static void musOpenId(uint32_t id){
  FlexMlRec r;
  if(!mlGet(id, &r)) return;
  if(musLoaded && id == musId){ musScreen = MUS_NOW; if(!musPlaying) musPlay(); musRender(); return; }
  musStart(id, r.path);
}
static const MediaListApp MUS_APP = { "M\xC3\xBAsica", musRender, musOpenId, NULL };

static void musListTouch(){
  int bx, by, bw, bh; uiBox(bx, by, bw, bh);
  int pad = uiPad();
  if(T.pressed){ musDragging = false; musDragY0 = T.y; musDragS0 = musScroll; }
  if(T.down && !musDragging && abs(T.y - musDragY0) > 8) musDragging = true;
  if(T.down && musDragging){
    int ns = musDragS0 - (T.y - musDragY0), mx = musListMaxScroll();
    if(ns < 0) ns = 0;
    if(ns > mx) ns = mx;
    if(ns != musScroll){ musScroll = ns; musRender(); }
    return;
  }
  if(!gHosted && T.down && !musLongFired && (millis() - T.downMs) > 550
     && abs(T.x - T.startX) < 14 && abs(T.y - T.startY) < 14){
    musLongFired = true;
    uint32_t id = musHitId(T.startX, T.startY);
    if(id && !mkMulti){ mkOpenItemMenu(id, T.x, T.y + 10, NULL, 0); return; }
    if(id && mkMulti){ mkToggle(id); musRender(); return; }
  }
  if(!T.down) musLongFired = false;
  if(!T.tap) return;
  if(musDragging){ musDragging = false; return; }
  if(mkMulti && mkBarTouch(musSelectAll)) return;
  if(T.x > bx + bw - pad - 40 && T.y < by + 34){
    static const uint8_t acts[3] = { MA_CONNECT, MA_SELECT, MA_TRASHBIN };
    mkOpenAppMenu(bx + bw - MM_W / 2 - 16, by + 34, acts, 3);
    return;
  }
  if(musShowBar() && T.y >= musBarTop() && T.y <= musBarTop() + MUS_BAR_H){
    if(T.x > bx + bw - 80){
      if(musPlaying) musPause();
      else if(musLoaded) musPlay();
      else { musStart(musId, musPath); musScreen = MUS_LIST; }
      musRender();
    }
    else { musScreen = MUS_NOW; musRender(); }
    return;
  }
  uint32_t id = musHitId(T.x, T.y);
  if(!id) return;
  if(mkMulti){ mkToggle(id); musRender(); return; }
  FlexMlRec r;
  if(mlGet(id, &r) && !(r.flags & FML_R_LOCKED) && !(r.flags & FML_R_PLAYABLE)){
    // Se guarda y se descarga, pero aqui no suena: se dice por que.
    const char* why = flexMlWhyUnplayable(r.fmt);
    mmDlgOpen("No se puede reproducir", why ? why : "Flex OS no puede reproducir este archivo", "Entendido", "", false);
    return;
  }
  mkRequestOpen(id);                              // lo protegido pide antes la clave
}

static void musNowTouch(){
  int bx, by, bw, bh; uiBox(bx, by, bw, bh);
  MusNowGeom g = musNowGeom();
  // Volumen: se arrastra; el registro del codec se escribe al cambiar y la
  // NVS solo al soltar (cada escritura de flash es un tiron).
  if(T.pressed && musLoaded && flexAudioAvailable() && abs(T.y - g.volY) <= 22 && T.x >= g.volX - 12 && T.x <= g.volX + g.volW + 12)
    musVolDrag = true;
  if(musVolDrag){
    if(T.down){
      int v = (T.x - g.volX) * FLEXAUDIO_VOL_MAX / (g.volW > 0 ? g.volW : 1);
      if(v < 0) v = 0;
      if(v > FLEXAUDIO_VOL_MAX) v = FLEXAUDIO_VOL_MAX;
      if(abs(v - (int)flexAudioVolume()) >= 2 || v == 0 || v == FLEXAUDIO_VOL_MAX){
        flexAudioSetVolume((uint8_t)v);
        musDrawVolume(true);
      }
      return;
    }
    musVolDrag = false;
    flexAudioSavePrefs();                         // NVS una vez, al soltar (no por paso)
    return;
  }
  if(!T.tap) return;
  if(!flexAudioAvailable()) return;
  if(musLoaded && abs(T.y - g.barY) <= 22 && T.x >= g.barX - 10 && T.x <= g.barX + g.barW + 10){
    int rel = T.x - g.barX;
    if(rel < 0) rel = 0;
    if(rel > g.barW) rel = g.barW;
    musSeek((uint32_t)((uint64_t)flexAsDurMs(&musAs) * (uint32_t)rel / (uint32_t)(g.barW > 0 ? g.barW : 1)));
    musDrawProgress(true);
    return;
  }
  if(abs(T.y - g.ctrlY) <= 40){
    int cx = bx + bw / 2;
    if(abs(T.x - cx) <= 40){
      if(musPlaying) musPause();
      else if(musLoaded) musPlay();
      else if(musPath[0]) musStart(musId, musPath);
      musRender();
    } else if(T.x < cx - 50 && T.x > cx - 150){
      // Anterior: si ya van unos segundos, vuelve al principio (como
      // cualquier reproductor); si no, a la pista anterior.
      if(musLoaded && musPosMs() > 3000){ musSeek(0); musDrawProgress(true); }
      else musNext(-1, false);
    } else if(T.x > cx + 50 && T.x < cx + 150) musNext(+1, false);
  }
}

// -------------------------------------------------------------
//  APERTURA DESDE OTRAS APPS (el Explorador, via Multimedia)
// -------------------------------------------------------------
static void musOpenPending(){
  if(!musPending[0]) return;
  char p[FML_PATH_MAX];
  snprintf(p, sizeof(p), "%s", musPending);
  musPending[0] = 0;
  uint32_t id = 0;
  mlLock();
  int i = flexMlFindPath(&gMs.lib, p);
  if(i >= 0) id = gMs.lib.recs[i].id;
  mlUnlock();
  musStart(id, p);
}
static void musOpenPath(const char* path){
  if(!path || !path[0]) return;
  snprintf(musPending, sizeof(musPending), "%s", path);
  if(gState == ST_APP && gAppId == IC_MUSICA){ musOpenPending(); return; }
  if(gState == ST_APP) appClose();
  enterApp(IC_MUSICA);
}

// -------------------------------------------------------------
//  CICLO DE VIDA
// -------------------------------------------------------------
static void musEnter(){
  mkBind(&MUS_APP);
  gMlBeforeChange = musBeforeChange;
  if(!gRelayout){
    musScroll = 0;
    mkReset();
    if(gMlOk) mlRequestScan();                   // lo copiado por otras vias aparece al entrar
    musScreen = musPlaying ? MUS_NOW : MUS_LIST;
  }
  if(musPending[0]){ musOpenPending(); return; }
  musRender();
}

static void musTick(){
  if(musScreen == MUS_NOW){
    if(mkTick()) return;                          // (un dialogo abierto desde aqui)
    if(musPlaying && millis() - musUiMs >= MUS_UI_MS){ musUiMs = millis(); musDrawProgress(true); }
    musNowTouch();
    return;
  }
  if(mkTick()) return;                            // menu, dialogos, papelera, hoja del servidor
  if(gMlOk && !musDragging && millis() - musSeenMs >= 300 && mlRev() != musSeenRev){ musRender(); return; }
  if(musMorePending && !T.down){ musRender(); return; }
  musListTouch();
}

// ATRAS: capas y seleccion; luego de "Reproduciendo" a la lista.
static bool musBackLayer(){ return musScreen == MUS_LIST && mkBackLayer(); }
static bool musBackScreen(){
  if(musScreen != MUS_NOW) return false;
  musScreen = MUS_LIST;
  musRender();
  return true;
}
// Segundo plano: la musica SIGUE (la alimenta loop()); lo que se va son las
// capas y la seleccion.
static void musSuspend(){ musDragging = false; musLongFired = false; musVolDrag = false; mkSuspend(); }
static void musResume(){ mkBind(&MUS_APP); musRender(); }
// Trabajo real en segundo plano: solo mientras suena.
static bool musBgWork(){ return musPlaying; }
// Soltar sin cerrar: miniaturas; el bloque de audio solo si no suena nada.
static size_t musShed(){
  size_t n = mlThumbDropAll();
  if(!musLoaded && musWork){ mediaFree(musWork); musWork = NULL; musWorkCap = 0; n++; }
  return n;
}
// Cerrar la app PARA la musica y lo suelta todo.
static void musCloseApp(){
  musForget();
  mediaFree(musWork); musWork = NULL; musWorkCap = 0;
  musErr[0] = 0;
  musScreen = MUS_LIST; musScroll = 0;
  if(mkApp == &MUS_APP) mkReset();
  if(gMlBeforeChange == musBeforeChange) gMlBeforeChange = NULL;
}
