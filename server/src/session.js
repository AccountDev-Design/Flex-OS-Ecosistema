'use strict';
// #############################################################
//  session.js  ·  UNA SESION = UN DISPOSITIVO
//  ------------------------------------------------------------
//  Cada dispositivo recibe su PROPIO BrowserContext de Playwright.
//  Eso no es un detalle: un contexto es un perfil aislado -- cookies,
//  almacenamiento local, cache y permisos propios. Dos dispositivos
//  nunca comparten sesion en un sitio web, y cerrar el contexto borra
//  todo lo que ese dispositivo dejo.
//
//  Aqui viven ademas los limites (pestanas, ancho de banda,
//  inactividad) y la CONTRAPRESION: el servicio no genera un
//  fotograma nuevo mientras el dispositivo no haya confirmado el
//  anterior.
// #############################################################

const crypto = require('node:crypto');
const proto = require('./protocol');
const { Tab } = require('./tab');

class Session {
  constructor(server, ws, remote) {
    this.server = server;
    this.cfg = server.cfg;
    this.log = server.log;
    this.ws = ws;
    this.remote = remote;

    this.id = crypto.randomBytes(8).toString('hex');
    this.deviceId = null;
    this.authed = false;
    this.context = null;

    this.tabs = new Map();              // id -> Tab
    this.activeTab = 1;
    this.seq = 1;

    this.viewW = 480;
    this.viewH = 640;
    this.quality = 62;
    this.scalePct = 100;
    this.profile = 1;
    this.maxFrameBytes = this.cfg.limits.maxFrameBytes;

    // Contrapresion y consumo.
    this.inflight = new Map();          // canal -> nº de FRAME sin confirmar
    this.lastAckSeq = 0;
    this.bytesThisMinute = 0;
    this.minuteStart = Date.now();
    this.lastActivity = Date.now();
    this.closed = false;
  }

  nextSeq() {
    this.seq = (this.seq + 1) & 0xffff;
    if (this.seq === 0) this.seq = 1;
    return this.seq;
  }

  touch() { this.lastActivity = Date.now(); }

  // ---- envio -------------------------------------------------------
  send(buf) {
    if (this.closed || this.ws.readyState !== 1) return false;
    // Limite de ancho de banda por minuto. Es lo que impide que una
    // pagina con una animacion a pantalla completa se coma la conexion
    // (y la bateria del dispositivo) sin freno.
    const now = Date.now();
    if (now - this.minuteStart > 60000) { this.minuteStart = now; this.bytesThisMinute = 0; }
    if (this.bytesThisMinute + buf.length > this.cfg.limits.maxBytesPerMinute) {
      if (!this.bwWarned) {
        this.bwWarned = true;
        this.log.warn(`[${this.id}] limite de ancho de banda alcanzado; se descartan fotogramas`);
        this.sendError(0, proto.ERR.LIMIT, 'Limite de datos por minuto alcanzado');
      }
      return false;
    }
    this.bwWarned = false;
    this.bytesThisMinute += buf.length;
    this.ws.send(buf, { binary: true });
    return true;
  }

  sendFrame(channel, buf, jpegLen) {
    if (!this.send(buf)) return;
    this.inflight.set(channel, (this.inflight.get(channel) || 0) + 1);
    this.framesSent = (this.framesSent || 0) + 1;
    this.bytesFrames = (this.bytesFrames || 0) + jpegLen;
  }

  // Se permite UN fotograma en vuelo por canal. Mas seria acumular
  // trabajo que el dispositivo no puede seguir: dibuja en el mismo hilo
  // que atiende el dedo.
  canSendFrame(channel) {
    return (this.inflight.get(channel) || 0) < 1;
  }

  sendError(channel, code, msg) {
    this.send(proto.buildError(this.nextSeq(), channel, code, String(msg || '').slice(0, 119)));
  }

  // ---- ciclo de vida -----------------------------------------------
  async authenticate(hello) {
    const devices = this.cfg.devices || [];
    const tokenBuf = Buffer.from(String(hello.token || ''), 'utf8');
    let match = null;
    for (const d of devices) {
      const want = Buffer.from(String(d.token), 'utf8');
      // Comparacion en tiempo constante: comparar con === filtraria la
      // longitud y el prefijo de la credencial por el tiempo de
      // respuesta. timingSafeEqual exige longitudes iguales, asi que se
      // comprueba antes y se sigue recorriendo toda la lista igualmente.
      const ok = want.length === tokenBuf.length && crypto.timingSafeEqual(want, tokenBuf);
      if (ok && !match) match = d;
    }
    if (!match) return false;
    this.deviceId = match.id;
    this.authed = true;
    return true;
  }

  async start(hello) {
    const lim = this.cfg.limits;
    this.viewW = Math.max(120, Math.min(lim.maxViewportW, hello.w || 480));
    this.viewH = Math.max(120, Math.min(lim.maxViewportH, hello.h || 640));
    this.quality = Math.max(this.cfg.capture.jpegQualityMin,
                            Math.min(this.cfg.capture.jpegQualityMax, hello.quality || 62));
    this.profile = hello.profile || 1;
    this.maxFrameBytes = Math.min(lim.maxFrameBytes, hello.maxFrameBytes || lim.maxFrameBytes);
    this.maxTabs = Math.max(1, Math.min(lim.maxTabsPerSession, hello.maxTabs || 1));

    this.context = await this.server.browser.newContext({
      viewport: { width: this.viewW, height: this.viewH },
      deviceScaleFactor: 1,
      locale: this.cfg.browser.locale,
      timezoneId: this.cfg.browser.timezone,
      userAgent: this.cfg.browser.userAgent || undefined,
      acceptDownloads: false,
      javaScriptEnabled: true,
      bypassCSP: false,
      serviceWorkers: 'block'
    });
    this.context.setDefaultTimeout(this.cfg.limits.navigationTimeoutMs);

    const tab = new Tab(this, 1);
    await tab.open();
    this.tabs.set(1, tab);
    this.activeTab = 1;

    this.send(proto.buildWelcome(this.nextSeq(), {
      viewW: this.viewW, viewH: this.viewH,
      caps: 0x0f, maxTabs: this.maxTabs,
      maxFrameBytes: this.maxFrameBytes,
      sessionId: this.id
    }));
    this.log.info(`[${this.id}] sesion lista para "${this.deviceId}" (${this.viewW}x${this.viewH}, q=${this.quality})`);
  }

  async close(reason) {
    if (this.closed) return;
    this.closed = true;
    for (const t of this.tabs.values()) await t.close().catch(() => {});
    this.tabs.clear();
    // Cerrar el contexto borra cookies, almacenamiento y cache de ESTE
    // dispositivo: no queda nada suyo entre sesiones.
    if (this.context) { try { await this.context.close(); } catch { /* ya cerrado */ } }
    this.context = null;
    try { this.ws.close(); } catch { /* ya cerrado */ }
    this.log.info(`[${this.id}] sesion cerrada (${reason})`);
  }

  tab(id) { return this.tabs.get(id) || this.tabs.get(this.activeTab); }

  async newTab() {
    if (this.tabs.size >= this.maxTabs) {
      this.sendError(0, proto.ERR.LIMIT, `Maximo de ${this.maxTabs} pestanas`);
      return null;
    }
    let id = 1;
    while (this.tabs.has(id)) id++;
    if (id > 200) { this.sendError(0, proto.ERR.LIMIT, 'Sin identificadores de pestana'); return null; }
    const t = new Tab(this, id);
    await t.open();
    this.tabs.set(id, t);
    this.pushTabs();
    return t;
  }

  async closeTab(id) {
    const t = this.tabs.get(id);
    if (!t) return;
    await t.close();
    this.tabs.delete(id);
    if (this.activeTab === id) this.activeTab = this.tabs.keys().next().value || 1;
    this.pushTabs();
  }

  pushTabs() {
    const list = [...this.tabs.values()].map((t) => ({ id: t.id, title: t.title || '' }));
    this.send(proto.buildTabs(this.nextSeq(), list, this.activeTab));
  }

  // ---- mensajes entrantes -------------------------------------------
  async handle(msg) {
    this.touch();
    const t = this.tab(msg.channel);

    switch (msg.type) {
      case proto.C.PING:
        this.send(proto.buildPong(this.nextSeq()));
        break;

      case proto.C.ACK: {
        const a = proto.parseAck(msg.payload);
        this.lastAckSeq = a.lastSeq;
        // El ACK libera la contrapresion de TODOS los canales: el
        // dispositivo dibuja de uno en uno, asi que confirmar significa
        // "ya he terminado con lo anterior".
        this.inflight.clear();
        break;
      }

      case proto.C.NAVIGATE: {
        if (!t) break;
        const url = proto.parseNavigate(msg.payload);
        await t.navigate(url);
        break;
      }
      case proto.C.BACK:    if (t) await t.goBack(); break;
      case proto.C.FORWARD: if (t) await t.goForward(); break;
      case proto.C.RELOAD:  if (t) await t.reload(); break;
      case proto.C.STOP:    if (t) await t.stop(); break;

      case proto.C.POINTER: if (t) await t.pointer(proto.parsePointer(msg.payload)); break;
      case proto.C.SCROLL:  if (t) await t.scroll(proto.parseScroll(msg.payload)); break;
      case proto.C.KEY:     if (t) await t.key(proto.parseKey(msg.payload)); break;

      case proto.C.VIEWPORT: {
        const v = proto.parseViewport(msg.payload);
        const lim = this.cfg.limits;
        if (v.w >= 120 && v.h >= 120) {
          this.viewW = Math.min(lim.maxViewportW, v.w);
          this.viewH = Math.min(lim.maxViewportH, v.h);
          for (const tb of this.tabs.values()) {
            if (tb.page) await tb.page.setViewportSize({ width: this.viewW, height: this.viewH }).catch(() => {});
          }
        }
        if (v.quality) this.quality = Math.max(this.cfg.capture.jpegQualityMin,
                                               Math.min(this.cfg.capture.jpegQualityMax, v.quality));
        if (v.scalePct >= 25 && v.scalePct <= 100) this.scalePct = v.scalePct;
        this.profile = v.profile;
        for (const tb of this.tabs.values()) tb.forceKeyframe();
        break;
      }

      case proto.C.TAB_NEW: await this.newTab(); break;
      case proto.C.TAB_CLOSE: await this.closeTab(msg.channel); break;
      case proto.C.TAB_SELECT:
        if (this.tabs.has(msg.channel)) {
          this.activeTab = msg.channel;
          this.tabs.get(msg.channel).forceKeyframe();
        }
        break;

      case proto.C.MEDIA: {
        const m = proto.parseMedia(msg.payload);
        const active = this.tabs.get(this.activeTab);
        if (active) await active.mediaCommand(m.cmd, m.arg);
        break;
      }

      case proto.C.REQ_FRAME:
        if (t) t.forceKeyframe();
        break;

      default:
        this.log.debug(`[${this.id}] tipo desconocido 0x${msg.type.toString(16)}`);
        break;
    }
  }

  stats() {
    return {
      id: this.id, device: this.deviceId, tabs: this.tabs.size,
      viewport: `${this.viewW}x${this.viewH}`, quality: this.quality,
      framesSent: this.framesSent || 0,
      kbSent: Math.round((this.bytesFrames || 0) / 1024),
      idleMs: Date.now() - this.lastActivity
    };
  }
}

module.exports = { Session };
