'use strict';
// #############################################################
//  server.js  ·  SERVICIO REMOTO DE NAVEGACION DE FLEX OS
//  ------------------------------------------------------------
//  · API de control versionada por HTTPS  ->  /v1/health, /v1/version
//  · Sesion interactiva por WebSocket     ->  /v1/session  (FBP/1)
//
//  Arranque:   node src/server.js --config config.json
//  Ver server/README.md para el despliegue y server/PROTOCOL.md para
//  el protocolo.
// #############################################################

const http = require('node:http');
const https = require('node:https');
const fs = require('node:fs');
const { WebSocketServer } = require('ws');
const { chromium } = require('playwright');

const configMod = require('./config');
const proto = require('./protocol');
const { Session } = require('./session');

const VERSION = '1.0.0';

// -------------------------------------------------------------
//  Log
// -------------------------------------------------------------
function makeLog(level) {
  const order = { debug: 0, info: 1, warn: 2, error: 3 };
  const min = order[level] != null ? order[level] : 1;
  const at = () => new Date().toISOString().slice(11, 23);
  const out = (lvl, args) => {
    if (order[lvl] < min) return;
    const fn = lvl === 'error' ? console.error : (lvl === 'warn' ? console.warn : console.log);
    fn(`${at()} ${lvl.toUpperCase().padEnd(5)} ${args.join(' ')}`);
  };
  return {
    debug: (...a) => out('debug', a),
    info: (...a) => out('info', a),
    warn: (...a) => out('warn', a),
    error: (...a) => out('error', a)
  };
}

// -------------------------------------------------------------
//  Servicio
// -------------------------------------------------------------
class BrowserService {
  constructor(cfg) {
    this.cfg = cfg;
    this.log = makeLog(cfg.logLevel);
    this.browser = null;
    this.sessions = new Set();
    this.startedAt = Date.now();
  }

  async startBrowser() {
    const b = this.cfg.browser;
    this.browser = await chromium.launch({
      headless: b.headless,
      executablePath: b.executablePath || undefined,
      args: [
        // Sin sandbox NO: el sandbox de Chromium es justo lo que
        // contiene a una pagina hostil. Si el contenedor no lo permite,
        // se ejecuta con --cap-add=SYS_ADMIN o con seccomp, no
        // desactivandolo. Ver README ("Aislamiento").
        '--disable-background-networking',
        '--disable-sync',
        '--disable-features=Translate,MediaRouter',
        '--no-first-run',
        '--mute-audio',            // no hay ruta de audio hacia el dispositivo
        ...(b.args || [])
      ]
    });
    this.log.info(`Chromium ${this.browser.version()} en marcha`);
  }

  // ---- API de control (HTTPS) ----
  handleHttp(req, res) {
    const url = new URL(req.url, 'http://x');
    const json = (code, obj) => {
      const body = JSON.stringify(obj);
      res.writeHead(code, {
        'content-type': 'application/json; charset=utf-8',
        'content-length': Buffer.byteLength(body),
        'cache-control': 'no-store',
        // El servicio no sirve HTML: cualquier cosa que llegue aqui se
        // trata como datos, nunca como pagina.
        'x-content-type-options': 'nosniff'
      });
      res.end(body);
    };
    if (req.method !== 'GET') return json(405, { error: 'metodo no permitido' });

    switch (url.pathname) {
      case '/v1/health':
        return json(200, {
          status: this.browser ? 'ok' : 'starting',
          protocol: `FBP/${proto.VERSION}`,
          version: VERSION,
          uptimeSec: Math.round((Date.now() - this.startedAt) / 1000),
          sessions: this.sessions.size,
          maxSessions: this.cfg.limits.maxSessions
        });
      case '/v1/version':
        return json(200, { service: VERSION, protocol: proto.VERSION, node: process.version });
      case '/v1/sessions':
        // Solo un resumen: ni URL visitadas ni titulos. Este endpoint es
        // para saber si el servicio esta saturado, no para vigilar lo que
        // hace el usuario.
        return json(200, { sessions: [...this.sessions].map((s) => s.stats()) });
      default:
        return json(404, { error: 'no encontrado' });
    }
  }

  // ---- sesion (WebSocket) ----
  async onConnection(ws, req) {
    const remote = req.socket.remoteAddress;

    if (this.sessions.size >= this.cfg.limits.maxSessions) {
      this.log.warn(`conexion rechazada desde ${remote}: limite de sesiones`);
      ws.send(proto.buildError(1, 0, proto.ERR.LIMIT, 'El servicio esta al limite de sesiones'));
      ws.close();
      return;
    }

    const s = new Session(this, ws, remote);
    this.sessions.add(s);
    this.log.info(`[${s.id}] conexion desde ${remote}`);

    // El HELLO tiene un plazo: una conexion que se queda callada ocupa
    // una plaza de sesion sin autenticarse.
    const helloTimer = setTimeout(() => {
      if (!s.authed) {
        this.log.warn(`[${s.id}] sin HELLO a tiempo`);
        s.sendError(0, proto.ERR.AUTH, 'No se recibio HELLO');
        s.close('sin hello').catch(() => {});
      }
    }, 8000);

    ws.on('message', async (data, isBinary) => {
      if (!isBinary) return;                          // FBP es binario
      const buf = Buffer.isBuffer(data) ? data : Buffer.from(data);
      const msg = proto.readHeader(buf);
      if (!msg) {
        s.sendError(0, proto.ERR.PROTOCOL, 'Mensaje mal formado');
        return;
      }
      try {
        if (!s.authed) {
          if (msg.type !== proto.C.HELLO) {
            s.sendError(0, proto.ERR.AUTH, 'Falta autenticacion');
            await s.close('sin autenticar');
            return;
          }
          const hello = proto.parseHello(msg.payload);
          if (hello.protoVer !== proto.VERSION) {
            s.sendError(0, proto.ERR.PROTOCOL, `Se esperaba FBP/${proto.VERSION}`);
            await s.close('version distinta');
            return;
          }
          if (!(await s.authenticate(hello))) {
            this.log.warn(`[${s.id}] credencial rechazada (dispositivo "${hello.deviceId}")`);
            s.sendError(0, proto.ERR.AUTH, 'Credencial no valida');
            await s.close('credencial no valida');
            return;
          }
          clearTimeout(helloTimer);
          await s.start(hello);
          return;
        }
        await s.handle(msg);
      } catch (e) {
        // Un mensaje mal formado no puede tumbar el servicio ni la
        // sesion: se informa y se sigue.
        this.log.warn(`[${s.id}] error tratando 0x${msg.type.toString(16)}: ${e.message}`);
        s.sendError(msg.channel, proto.ERR.PROTOCOL, 'Mensaje no valido');
      }
    });

    ws.on('close', () => {
      clearTimeout(helloTimer);
      this.sessions.delete(s);
      s.close('cerrada por el cliente').catch(() => {});
    });
    ws.on('error', (e) => {
      this.log.warn(`[${s.id}] error de socket: ${e.message}`);
    });
  }

  // Caducidad por inactividad: una sesion abandonada mantiene vivo un
  // contexto de Chromium entero (decenas de MB).
  startReaper() {
    this.reaper = setInterval(() => {
      const now = Date.now();
      for (const s of [...this.sessions]) {
        if (now - s.lastActivity > this.cfg.limits.sessionIdleMs) {
          this.log.info(`[${s.id}] caducada por inactividad`);
          s.sendError(0, proto.ERR.EXPIRED, 'Sesion caducada por inactividad');
          this.sessions.delete(s);
          s.close('inactividad').catch(() => {});
        }
      }
    }, 30000);
  }

  async stop() {
    clearInterval(this.reaper);
    for (const s of [...this.sessions]) await s.close('apagado').catch(() => {});
    this.sessions.clear();
    if (this.browser) await this.browser.close().catch(() => {});
  }
}

// -------------------------------------------------------------
//  main
// -------------------------------------------------------------
async function main() {
  const cfg = configMod.load();
  const svc = new BrowserService(cfg);
  const { fatal, warn } = configMod.validate(cfg);

  for (const w of warn) svc.log.warn(w);
  if (fatal.length) {
    for (const f of fatal) svc.log.error(f);
    svc.log.error('El servicio NO arranca. Ver server/README.md y server/config.example.json.');
    process.exit(2);
  }

  let server;
  if (cfg.tls.key && cfg.tls.cert) {
    server = https.createServer({
      key: fs.readFileSync(cfg.tls.key),
      cert: fs.readFileSync(cfg.tls.cert),
      minVersion: 'TLSv1.2'
    }, (req, res) => svc.handleHttp(req, res));
  } else {
    server = http.createServer((req, res) => svc.handleHttp(req, res));
  }

  const wss = new WebSocketServer({
    server,
    path: '/v1/session',
    maxPayload: 1 * 1024 * 1024,       // el dispositivo nunca envia mensajes grandes
    perMessageDeflate: false           // los JPEG ya vienen comprimidos
  });
  wss.on('connection', (ws, req) => { svc.onConnection(ws, req).catch((e) => svc.log.error(e.stack)); });

  await svc.startBrowser();
  svc.startReaper();

  server.listen(cfg.port, cfg.host, () => {
    const scheme = (cfg.tls.key && cfg.tls.cert) ? 'https' : 'http';
    const wsScheme = scheme === 'https' ? 'wss' : 'ws';
    svc.log.info(`Servicio de navegacion de Flex OS ${VERSION} · protocolo FBP/${proto.VERSION}`);
    svc.log.info(`  control : ${scheme}://${cfg.host}:${cfg.port}/v1/health`);
    svc.log.info(`  sesion  : ${wsScheme}://${cfg.host}:${cfg.port}/v1/session`);
    svc.log.info(`  pon esa direccion de sesion en el dispositivo: Navegador -> menu -> Ajustes -> Servidor`);
  });

  const bye = async (sig) => {
    svc.log.info(`${sig}: cerrando...`);
    await svc.stop();
    server.close(() => process.exit(0));
    setTimeout(() => process.exit(0), 3000).unref();
  };
  process.on('SIGINT', () => bye('SIGINT'));
  process.on('SIGTERM', () => bye('SIGTERM'));
}

if (require.main === module) {
  main().catch((e) => { console.error(e); process.exit(1); });
}

module.exports = { BrowserService, makeLog, VERSION };
