'use strict';
// #############################################################
//  tab.js  ·  UNA PESTANA = UNA PAGINA DE CHROMIUM
//  ------------------------------------------------------------
//  Aqui vive todo lo que toca Playwright: navegar, capturar,
//  inyectar toques y teclas, detectar multimedia y vigilar que
//  ninguna navegacion se escape a un destino interno.
//
//  COMO SE CAPTURA (y por que asi)
//  -------------------------------
//  Se usa el "screencast" de CDP (Page.startScreencast). Es la unica
//  ruta que entrega fotogramas CUANDO LA PAGINA CAMBIA en vez de
//  sondear: si el usuario no toca nada, no se envia nada y el
//  dispositivo no gasta ni radio ni CPU. Ademas el fotograma ya llega
//  en JPEG, asi que no hay que rasterizar aparte.
//
//  Ese fotograma se parte en BANDAS horizontales y solo se envian las
//  que han cambiado. Las bandas no son un capricho de ancho de banda:
//  el dispositivo decodifica en el hilo grafico, y una banda de unos
//  cientos de filas tarda decenas de milisegundos, con lo que entre
//  banda y banda vuelve a atender el dedo. Enviar la pagina entera de
//  golpe dejaria la interfaz muda durante toda la decodificacion.
//
//  Si el screencast no esta disponible (otro navegador, CDP
//  desactivado), se cae a page.screenshot() con sondeo. Funciona
//  igual, solo que con mas latencia; se avisa en el log.
// #############################################################

const sharp = require('sharp');
const proto = require('./protocol');
const guard = require('./guard');

// Codigo de tecla especial -> nombre de tecla de Playwright.
const VK_NAMES = {
  [proto.VK.ENTER]: 'Enter',
  [proto.VK.BACKSPACE]: 'Backspace',
  [proto.VK.TAB]: 'Tab',
  [proto.VK.ESC]: 'Escape',
  [proto.VK.UP]: 'ArrowUp',
  [proto.VK.DOWN]: 'ArrowDown',
  [proto.VK.LEFT]: 'ArrowLeft',
  [proto.VK.RIGHT]: 'ArrowRight'
};

// Hash FNV-1a de 32 bits. Sirve para saber si una banda ha cambiado; no
// es criptografico ni lo necesita (una colision solo significa que una
// banda no se refresca hasta el siguiente keyframe).
function fnv1a(buf) {
  let h = 0x811c9dc5;
  for (let i = 0; i < buf.length; i += 7) {          // muestreo 1 de cada 7 bytes
    h ^= buf[i];
    h = Math.imul(h, 0x01000193) >>> 0;
  }
  h ^= buf.length;
  return Math.imul(h, 0x01000193) >>> 0;
}

class Tab {
  constructor(session, id) {
    this.session = session;
    this.id = id;                       // 1..N (canal FBP)
    this.page = null;
    this.cdp = null;
    this.screencastOn = false;
    this.pollTimer = null;

    this.bandHashes = [];
    this.frameId = 0;
    this.lastKeyframeAt = 0;
    this.captureBusy = false;
    this.capturePending = false;
    this.lastCaptureAt = 0;
    this.closed = false;

    this.media = null;                  // descriptor del ultimo <video> visto
    this.mediaTimer = null;
    this.title = '';
    this.url = '';
    this.loading = false;
    this.progress = 0;
  }

  get log() { return this.session.log; }
  get cfg() { return this.session.cfg; }

  async open() {
    const s = this.session;
    this.page = await s.context.newPage();
    await this.page.setViewportSize({ width: s.viewW, height: s.viewH });
    this.page.setDefaultNavigationTimeout(this.cfg.limits.navigationTimeoutMs);

    // ---- Vigilancia de navegaciones -------------------------------
    // Se comprueba CADA peticion de documento principal, no solo la
    // primera: una redireccion 302 hacia "http://10.0.0.1/" tiene que
    // morir aqui igual que la peticion original.
    await this.page.route('**/*', async (route) => {
      const req = route.request();
      if (req.resourceType() !== 'document' || req.frame() !== this.page.mainFrame()) {
        return route.continue();
      }
      const verdict = await guard.checkUrl(req.url(), {
        allowPrivate: this.cfg.security.allowPrivate,
        allowHosts: this.cfg.security.allowHosts,
        allowedPorts: this.cfg.security.allowedPorts,
        maxLength: this.cfg.limits.maxUrlLength
      });
      if (!verdict.ok) {
        this.log.warn(`[tab ${this.id}] navegacion bloqueada: ${req.url()} -> ${verdict.reason}`);
        s.sendError(this.id, proto.ERR.BLOCKED, verdict.reason);
        return route.abort('blockedbyclient');
      }
      return route.continue();
    });

    this.page.on('framenavigated', (f) => {
      if (f !== this.page.mainFrame()) return;
      this.url = f.url();
      s.send(proto.buildNavigated(s.nextSeq(), this.id, this.url));
      this.forceKeyframe();
    });
    this.page.on('load', () => { this.loading = false; this.progress = 100; this.onChanged(true); this.detectMedia(); });
    this.page.on('domcontentloaded', () => { this.progress = 70; this.pushState(); });
    this.page.on('close', () => { this.closed = true; });
    this.page.on('crash', () => {
      this.log.error(`[tab ${this.id}] la pagina se ha caido`);
      s.sendError(this.id, proto.ERR.INTERNAL, 'La pagina se ha cerrado sola');
    });

    // Dialogos: se descartan solos. Dejarlos abiertos congelaria la
    // pagina y el dispositivo no tiene forma de contestarlos.
    this.page.on('dialog', (d) => { d.dismiss().catch(() => {}); });

    // Ventanas emergentes: no se abre una pestana nueva por su cuenta.
    // Se avisa al dispositivo y se cierra; abrir pestanas sin pedirlo es
    // justo lo que hace insoportable navegar en una pantalla pequena.
    this.page.on('popup', async (p) => {
      const u = p.url();
      try { await p.close(); } catch { /* ya cerrada */ }
      this.log.info(`[tab ${this.id}] popup bloqueado: ${u}`);
      s.sendError(this.id, proto.ERR.BLOCKED, 'Ventana emergente bloqueada');
    });

    // Descargas: el dispositivo no puede guardarlas (no hay sitio ni
    // gestor de ficheros remoto). Se cancelan y se anuncian.
    this.page.on('download', async (d) => {
      const name = d.suggestedFilename();
      let size = 0;
      try { await d.cancel(); } catch { /* ya cancelada */ }
      s.send(proto.buildDownload(s.nextSeq(), this.id, { name, size, url: d.url() }));
      this.log.info(`[tab ${this.id}] descarga cancelada: ${name}`);
    });

    await this.startScreencast();
  }

  // ---- captura -----------------------------------------------------
  async startScreencast() {
    try {
      this.cdp = await this.session.context.newCDPSession(this.page);
      this.cdp.on('Page.screencastFrame', async (ev) => {
        // Hay que confirmar SIEMPRE, tambien si se descarta el
        // fotograma: si no, Chromium deja de mandar.
        this.cdp.send('Page.screencastFrameAck', { sessionId: ev.sessionId }).catch(() => {});
        this.onChanged(false);
      });
      await this.cdp.send('Page.startScreencast', {
        format: 'jpeg', quality: 40, everyNthFrame: 1,
        maxWidth: this.session.viewW, maxHeight: this.session.viewH
      });
      this.screencastOn = true;
    } catch (e) {
      // Sin screencast se sondea. Peor latencia, mismo resultado.
      this.log.warn(`[tab ${this.id}] sin screencast (${e.message}); se usara sondeo`);
      this.screencastOn = false;
      this.pollTimer = setInterval(() => this.onChanged(false), 400);
    }
  }

  forceKeyframe() {
    this.bandHashes = [];
    this.lastKeyframeAt = 0;
    this.onChanged(true);
  }

  // Marca que hay algo que capturar. Coalesce: varias llamadas seguidas
  // producen UNA captura, y nunca mas rapido que minIntervalMs.
  onChanged(immediate) {
    if (this.closed) return;
    this.capturePending = true;
    if (this.captureBusy) return;
    const wait = immediate ? 0 : Math.max(0, this.cfg.capture.minIntervalMs - (Date.now() - this.lastCaptureAt));
    setTimeout(() => this.capture().catch((e) => this.log.debug(`captura: ${e.message}`)), wait);
  }

  async capture() {
    if (this.closed || this.captureBusy || !this.capturePending) return;
    // CONTRAPRESION: si el dispositivo aun no ha confirmado el envio
    // anterior, no se genera otro. Es lo que impide que se acumulen
    // fotogramas que llegarian tarde y ya no valdrian.
    if (!this.session.canSendFrame(this.id)) return;

    this.captureBusy = true;
    this.capturePending = false;
    this.lastCaptureAt = Date.now();
    try {
      const s = this.session;
      const shot = await this.page.screenshot({ type: 'jpeg', quality: 80, timeout: 8000 });

      // Escala del viewport: el dispositivo puede pedir MENOS pixeles de
      // los que ocupa la pagina en su pantalla (perfil de ahorro, o el
      // Flex OS Pro, cuyo panel fisico es la mitad del lienzo logico).
      const outW = Math.max(16, Math.round(s.viewW * s.scalePct / 100));
      const outH = Math.max(16, Math.round(s.viewH * s.scalePct / 100));

      let img = sharp(shot);
      if (outW !== s.viewW || outH !== s.viewH) img = img.resize(outW, outH, { fit: 'fill' });
      const { data, info } = await img.raw().toBuffer({ resolveWithObject: true });

      const chans = info.channels;
      const bandsN = Math.max(1, Math.ceil(s.viewH / this.cfg.capture.bandHeight));
      const keyframe = (Date.now() - this.lastKeyframeAt) > this.cfg.capture.keyframeEveryMs ||
                       this.bandHashes.length !== bandsN;
      if (keyframe) { this.bandHashes = new Array(bandsN).fill(0); this.lastKeyframeAt = Date.now(); }

      const changed = [];
      for (let b = 0; b < bandsN; b++) {
        // Coordenadas EN PANTALLA (las que entiende el dispositivo).
        const devY = b * this.cfg.capture.bandHeight;
        const devH = Math.min(this.cfg.capture.bandHeight, s.viewH - devY);
        if (devH <= 0) continue;
        // Coordenadas en la imagen ya escalada.
        const imgY = Math.round(devY * outH / s.viewH);
        const imgH = Math.max(1, Math.round(devH * outH / s.viewH));
        if (imgY >= info.height) continue;
        const hh = Math.min(imgH, info.height - imgY);
        const slice = data.subarray(imgY * info.width * chans, (imgY + hh) * info.width * chans);
        const hash = fnv1a(slice);
        if (!keyframe && this.bandHashes[b] === hash) continue;
        this.bandHashes[b] = hash;
        changed.push({ b, devY, devH, imgY, imgH: hh });
      }
      if (changed.length === 0) return;

      for (let i = 0; i < changed.length; i++) {
        const c = changed[i];
        const jpeg = await sharp(data, { raw: { width: info.width, height: info.height, channels: chans } })
          .extract({ left: 0, top: c.imgY, width: info.width, height: c.imgH })
          // progressive: FALSE es obligatorio -- el decodificador del
          // dispositivo es baseline y rechaza el progresivo a proposito
          // (ver FlexOS_JPEG.h). mozjpeg no, por el mismo motivo:
          // produce ficheros que no siempre son baseline puro.
          .jpeg({ quality: s.quality, progressive: false, mozjpeg: false, chromaSubsampling: '4:2:0' })
          .toBuffer();

        if (jpeg.length > s.maxFrameBytes) {
          // No cabe en el buffer del dispositivo: se reintenta con menos
          // calidad antes de rendirse. Enviarlo igual seria un mensaje
          // que el dispositivo tiraria entero.
          const retry = await sharp(data, { raw: { width: info.width, height: info.height, channels: chans } })
            .extract({ left: 0, top: c.imgY, width: info.width, height: c.imgH })
            .jpeg({ quality: Math.max(this.cfg.capture.jpegQualityMin, Math.floor(s.quality / 2)),
                    progressive: false, mozjpeg: false, chromaSubsampling: '4:2:0' })
            .toBuffer();
          if (retry.length > s.maxFrameBytes) {
            this.log.warn(`[tab ${this.id}] banda ${c.b} descartada: ${retry.length} B > ${s.maxFrameBytes} B`);
            continue;
          }
          this.sendBand(c, retry, keyframe, i === changed.length - 1);
        } else {
          this.sendBand(c, jpeg, keyframe, i === changed.length - 1);
        }
      }
      this.frameId++;
    } finally {
      this.captureBusy = false;
      if (this.capturePending) this.onChanged(false);
    }
  }

  sendBand(c, jpeg, keyframe, last) {
    const s = this.session;
    let flags = 0;
    if (keyframe) flags |= proto.FR.KEYFRAME;
    if (last) flags |= proto.FR.LAST;
    s.sendFrame(this.id, proto.buildFrame(s.nextSeq(), this.id, {
      x: 0, y: c.devY, w: s.viewW, h: c.devH,
      flags, frameId: this.frameId, jpeg
    }), jpeg.length);
  }

  // ---- estado ------------------------------------------------------
  async pushState() {
    if (this.closed || !this.page) return;
    const s = this.session;
    let flags = 0;
    if (this.loading) flags |= proto.ST.LOADING;
    try {
      if (this.page.url() && this.page.url() !== 'about:blank') {
        flags |= this.page.url().startsWith('https://') ? proto.ST.SECURE : proto.ST.INSECURE;
      }
    } catch { /* pagina cerrandose */ }
    if (this.canGoBack) flags |= proto.ST.CAN_BACK;
    if (this.canGoForward) flags |= proto.ST.CAN_FWD;
    if (this.media) flags |= proto.ST.MEDIA;
    if (this.editing) flags |= proto.ST.EDITING;

    let title = this.title;
    try { title = await this.page.title(); } catch { /* pagina cerrandose */ }
    this.title = title;
    this.url = this.page.url();
    s.send(proto.buildState(s.nextSeq(), this.id, {
      flags, progress: this.progress, title: this.title, url: this.url
    }));
  }

  // ---- navegacion --------------------------------------------------
  async navigate(rawUrl) {
    const s = this.session;
    const verdict = await guard.checkUrl(rawUrl, {
      allowPrivate: this.cfg.security.allowPrivate,
      allowHosts: this.cfg.security.allowHosts,
      allowedPorts: this.cfg.security.allowedPorts,
      maxLength: this.cfg.limits.maxUrlLength
    });
    if (!verdict.ok) {
      this.log.warn(`[tab ${this.id}] destino rechazado: ${rawUrl} -> ${verdict.reason}`);
      s.sendError(this.id, proto.ERR.BLOCKED, verdict.reason);
      return;
    }
    this.loading = true;
    this.progress = 10;
    await this.pushState();
    try {
      await this.page.goto(verdict.url.href, { waitUntil: 'domcontentloaded' });
      this.loading = false;
      this.progress = 100;
      this.forceKeyframe();
    } catch (e) {
      this.loading = false;
      this.progress = 0;
      const timeout = /timeout/i.test(e.message);
      const cert = /ERR_CERT|SSL/i.test(e.message);
      s.sendError(this.id, timeout ? proto.ERR.TIMEOUT : (cert ? proto.ERR.CERT : proto.ERR.NAV), e.message);
      this.log.warn(`[tab ${this.id}] fallo al navegar: ${e.message}`);
    }
    await this.pushState();
    await this.updateHistoryFlags();
    await this.detectMedia();
  }

  async updateHistoryFlags() {
    try {
      const r = await this.page.evaluate(() => ({ len: history.length }));
      // Chromium no expone "puedo ir atras" directamente. history.length
      // > 1 es la aproximacion que usan todos los navegadores empotrados;
      // se marca "adelante" solo despues de haber ido atras, que es
      // cuando de verdad hay algo delante.
      this.canGoBack = r.len > 1;
    } catch { this.canGoBack = false; }
  }

  async goBack() {
    try { await this.page.goBack({ waitUntil: 'domcontentloaded' }); this.canGoForward = true; }
    catch { /* no hay atras */ }
    this.forceKeyframe();
    await this.pushState();
  }
  async goForward() {
    try { await this.page.goForward({ waitUntil: 'domcontentloaded' }); }
    catch { this.canGoForward = false; }
    this.forceKeyframe();
    await this.pushState();
  }
  async reload() {
    this.loading = true; this.progress = 10; await this.pushState();
    try { await this.page.reload({ waitUntil: 'domcontentloaded' }); } catch { /* cancelado */ }
    this.loading = false; this.progress = 100;
    this.forceKeyframe();
    await this.pushState();
  }
  async stop() {
    try { await this.page.evaluate(() => window.stop()); } catch { /* sin pagina */ }
    this.loading = false;
    await this.pushState();
  }

  // ---- entrada -----------------------------------------------------
  async pointer(ev) {
    const s = this.session;
    const x = Math.max(0, Math.min(s.viewW - 1, ev.x));
    const y = Math.max(0, Math.min(s.viewH - 1, ev.y));
    try {
      if (ev.action === proto.PTR.TAP) {
        await this.page.mouse.click(x, y, { delay: 30 });
        // Tras un toque puede haberse enfocado un campo de texto: el
        // dispositivo necesita saberlo para abrir el teclado solo.
        await this.checkEditing();
      } else if (ev.action === proto.PTR.DOWN) {
        await this.page.mouse.move(x, y);
        await this.page.mouse.down();
      } else if (ev.action === proto.PTR.UP) {
        await this.page.mouse.up();
        await this.checkEditing();
      } else if (ev.action === proto.PTR.MOVE) {
        await this.page.mouse.move(x, y);
      }
    } catch (e) { this.log.debug(`puntero: ${e.message}`); }
    this.onChanged(true);
  }

  async scroll(ev) {
    try { await this.page.mouse.wheel(ev.dx, ev.dy); }
    catch (e) { this.log.debug(`scroll: ${e.message}`); }
    this.onChanged(true);
  }

  async key(ev) {
    try {
      if (ev.action === proto.KEY.TEXT && ev.text) {
        await this.page.keyboard.insertText(ev.text);
      } else if (ev.code && VK_NAMES[ev.code]) {
        await this.page.keyboard.press(VK_NAMES[ev.code]);
      }
    } catch (e) { this.log.debug(`teclado: ${e.message}`); }
    this.onChanged(true);
    await this.checkEditing();
  }

  async checkEditing() {
    let editing = false;
    try {
      editing = await this.page.evaluate(() => {
        const a = document.activeElement;
        if (!a) return false;
        const t = a.tagName;
        if (t === 'TEXTAREA') return true;
        if (a.isContentEditable) return true;
        if (t !== 'INPUT') return false;
        const kind = (a.getAttribute('type') || 'text').toLowerCase();
        return ['text', 'search', 'url', 'email', 'password', 'tel', 'number'].includes(kind);
      });
    } catch { /* pagina navegando */ }
    if (editing !== this.editing) {
      this.editing = editing;
      await this.pushState();
    }
  }

  // ---- multimedia --------------------------------------------------
  //  ALCANCE, DICHO SIN ADORNOS:
  //   · Se detecta un <video> con dimensiones y duracion utiles.
  //   · Se reproduce enviando FOTOGRAMAS JPEG del propio elemento
  //     (MJPEG). Es la ruta que las tres placas SI pueden decodificar:
  //     ninguna lleva decodificador de video por hardware.
  //   · NO HAY AUDIO. Ninguna placa de Flex OS tiene salida de audio
  //     cableada (ni I2S ni DAC en los tres .ino), asi que no se
  //     promete: el reproductor del dispositivo lo dice en pantalla.
  //   · NO se eluden DRM, firmas, autenticacion ni controles de acceso.
  //     Con contenido protegido, Chromium entrega fotogramas en negro y
  //     eso es lo que se envia: no se intenta rodearlo. Netflix,
  //     Disney+, Widevine y similares quedan FUERA DE ALCANCE.
  async detectMedia() {
    if (!this.cfg.media.enabled) return;
    let info = null;
    try {
      info = await this.page.evaluate(() => {
        const vs = Array.from(document.querySelectorAll('video'));
        let best = null, bestArea = 0;
        for (const v of vs) {
          const r = v.getBoundingClientRect();
          const area = r.width * r.height;
          if (area > bestArea && r.width > 40 && r.height > 40) { best = v; bestArea = area; }
        }
        if (!best) return null;
        const r = best.getBoundingClientRect();
        return {
          x: Math.round(r.x), y: Math.round(r.y),
          w: Math.round(r.width), h: Math.round(r.height),
          vw: best.videoWidth | 0, vh: best.videoHeight | 0,
          duration: isFinite(best.duration) ? Math.round(best.duration * 1000) : 0,
          src: best.currentSrc || best.src || '',
          // Un elemento con claves de cifrado es contenido protegido.
          protected: !!(best.mediaKeys) || best.querySelectorAll('source[type*="drm"]').length > 0,
          title: document.title || ''
        };
      });
    } catch { return; }

    const had = !!this.media;
    this.media = info;
    if (info) {
      const s = this.session;
      s.send(proto.buildMedia(s.nextSeq(), this.id, {
        kind: 1,
        durationMs: info.duration,
        w: info.vw || info.w,
        h: info.vh || info.h,
        mime: info.protected ? 'video/protected' : 'video/unknown',
        url: '',                        // la URL de origen NO se reenvia
        title: info.title
      }));
    }
    if (had !== !!info) await this.pushState();
  }

  async mediaCommand(cmd, arg) {
    if (!this.cfg.media.enabled || !this.media) return;
    try {
      if (cmd === proto.MED.OPEN || cmd === proto.MED.PLAY) {
        await this.page.evaluate(() => { const v = document.querySelector('video'); if (v) v.play().catch(() => {}); });
        this.startMediaStream();
      } else if (cmd === proto.MED.PAUSE) {
        await this.page.evaluate(() => { const v = document.querySelector('video'); if (v) v.pause(); });
      } else if (cmd === proto.MED.SEEK) {
        await this.page.evaluate((ms) => { const v = document.querySelector('video'); if (v) v.currentTime = ms / 1000; }, arg);
      } else if (cmd === proto.MED.STOP) {
        await this.page.evaluate(() => { const v = document.querySelector('video'); if (v) v.pause(); });
        this.stopMediaStream();
      }
    } catch (e) { this.log.debug(`media: ${e.message}`); }
  }

  startMediaStream() {
    if (this.mediaTimer) return;
    const s = this.session;
    const period = Math.max(60, Math.round(1000 / Math.max(1, this.cfg.media.fps)));
    let busy = false;
    this.mediaTimer = setInterval(async () => {
      if (busy || this.closed || !this.media) return;
      if (!s.canSendFrame(proto.CH.MEDIA)) return;      // contrapresion tambien aqui
      busy = true;
      try {
        const m = this.media;
        const clip = {
          x: Math.max(0, m.x), y: Math.max(0, m.y),
          width: Math.min(m.w, s.viewW - Math.max(0, m.x)),
          height: Math.min(m.h, s.viewH - Math.max(0, m.y))
        };
        if (clip.width < 8 || clip.height < 8) return;
        const shot = await this.page.screenshot({ type: 'png', clip, timeout: 4000 });
        const maxW = Math.min(this.cfg.media.maxWidth, s.viewW);
        const jpeg = await sharp(shot)
          .resize({ width: maxW, withoutEnlargement: true })
          .jpeg({ quality: this.cfg.media.quality, progressive: false, mozjpeg: false })
          .toBuffer();
        if (jpeg.length <= s.maxFrameBytes) {
          const meta = await sharp(jpeg).metadata();
          s.sendFrame(proto.CH.MEDIA, proto.buildFrame(s.nextSeq(), proto.CH.MEDIA, {
            x: 0, y: 0, w: meta.width, h: meta.height,
            flags: proto.FR.KEYFRAME | proto.FR.LAST,
            frameId: ++this.frameId, jpeg
          }), jpeg.length);
        }
      } catch (e) { this.log.debug(`media frame: ${e.message}`); }
      finally { busy = false; }
    }, period);
  }

  stopMediaStream() {
    if (this.mediaTimer) { clearInterval(this.mediaTimer); this.mediaTimer = null; }
  }

  async close() {
    this.closed = true;
    this.stopMediaStream();
    if (this.pollTimer) { clearInterval(this.pollTimer); this.pollTimer = null; }
    if (this.cdp && this.screencastOn) {
      try { await this.cdp.send('Page.stopScreencast'); } catch { /* ya cerrada */ }
    }
    if (this.cdp) { try { await this.cdp.detach(); } catch { /* ya separada */ } }
    if (this.page) { try { await this.page.close(); } catch { /* ya cerrada */ } }
    this.page = null; this.cdp = null;
  }
}

module.exports = { Tab, fnv1a, VK_NAMES };
