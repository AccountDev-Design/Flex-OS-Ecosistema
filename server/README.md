# Servicio remoto de navegación de Flex OS

Ejecuta **Chromium de verdad** (con Playwright) y devuelve la página ya
rasterizada al dispositivo. Es la mitad «grande» del navegador de Flex OS
Ultra: la interfaz, el táctil, el teclado, las pestañas, el historial y
el reproductor viven en la placa; el motor web vive aquí.

---

## Por qué hace falta un servicio y no se ejecuta todo en la placa

Un motor web moderno (HTML5 + CSS3 + JavaScript + TLS + fuentes + vídeo)
son cientos de MB de código y necesita cientos de MB de RAM. El
ESP32-P4 tiene, en el mejor caso, 32 MB de PSRAM; el ESP32 clásico de
Flex OS Pro tiene unos 300 KB de SRAM. No es cuestión de optimizar: **no
cabe**. Lo que sí cabe —y va sobrado— es decodificar JPEG y dibujar, así
que el reparto es: aquí se renderiza, allí se muestra y se interactúa.

---

## Requisitos

* Node.js 20 o superior.
* Chromium (lo instala Playwright).
* Linux x64 o arm64. Funciona en una Raspberry Pi 4/5, en un mini-PC o
  en un contenedor.

## Instalación

```bash
cd server
npm install
npm run browsers          # descarga Chromium y sus dependencias
cp config.example.json config.json
```

Genera una credencial para cada placa y ponla en `config.json`:

```bash
node -e "console.log(require('crypto').randomBytes(24).toString('base64url'))"
```

```jsonc
"devices": [
  { "id": "salon-ultra", "token": "el-token-que-acabas-de-generar" }
]
```

**Sin credenciales configuradas el servicio no arranca.** No es un
capricho: un servicio de navegación abierto es un proxy abierto, y
cualquiera en la red podría usarlo para navegar desde tu IP.

## Arranque

```bash
node src/server.js --config config.json
```

```
Servicio de navegacion de Flex OS 1.0.0 · protocolo FBP/1
  control : https://0.0.0.0:8443/v1/health
  sesion  : wss://0.0.0.0:8443/v1/session
```

En el dispositivo: **Navegador → menú (⋮) → Ajustes del navegador**

* **Servidor**: `wss://LA-IP-O-EL-NOMBRE:8443/v1/session`
* **Credencial**: el token de ese dispositivo
* **Probar el servidor**: hace un `GET /v1/health` por HTTPS y dice si responde

---

## Certificados

El dispositivo exige `wss://` por defecto. Tres formas de conseguirlo,
de mejor a peor:

1. **Proxy inverso con TLS** (Caddy, nginx, Traefik) delante del
   servicio, con un certificado de Let's Encrypt. El servicio escucha en
   `127.0.0.1` sin TLS y el proxy pone el cifrado. Es lo más cómodo si
   tienes un nombre de dominio.
2. **Certificado propio en el servicio**: pon las rutas en `tls.key` y
   `tls.cert`.
3. **Sin TLS**, solo para una red de laboratorio: deja `tls` en `null` y
   activa en el dispositivo *Permitir ws:// sin TLS*. El servicio avisa
   en cada arranque y el dispositivo lo marca en sus ajustes.

**Validación del certificado en el dispositivo.** La versión actual usa
`setInsecure()` en `WiFiClientSecure`: acepta cualquier certificado. Eso
protege contra escuchas pasivas pero **no** contra un intermediario
activo. Para fijar un certificado concreto, edita
`FlexOS_BrowserApp.cpp`, busca `gTls->setInsecure()` y sustitúyelo por
`gTls->setCACert(TU_CA_PEM)`. Está documentado así, en vez de fingir que
la conexión es de confianza.

---

## Aislamiento de red (importante)

El servicio ejecuta un navegador completo. `src/guard.js` bloquea los
destinos internos —comprueba el esquema, rechaza IP literales, resuelve
el DNS y mira **todas** las direcciones devueltas, y vuelve a comprobar
**cada redirección**— pero esa es la primera barrera, no la única.

Entre que se resuelve el DNS y Chromium abre la conexión hay una ventana
en la que el mismo nombre podría resolver a otra dirección (*DNS
rebinding*). Cerrarla del todo exige aislamiento de red. Lo recomendable:

```bash
docker run -d --name flexos-browser \
  --network flexos-out \
  --read-only --tmpfs /tmp \
  --cap-drop=ALL --security-opt no-new-privileges \
  -v $PWD/config.json:/app/config.json:ro \
  -p 8443:8443 \
  flexos-browser
```

…con `flexos-out` configurada para **no** poder alcanzar tu red local.
Una regla de firewall equivalente:

```bash
iptables -I DOCKER-USER -s 172.20.0.0/16 -d 10.0.0.0/8      -j REJECT
iptables -I DOCKER-USER -s 172.20.0.0/16 -d 192.168.0.0/16  -j REJECT
iptables -I DOCKER-USER -s 172.20.0.0/16 -d 172.16.0.0/12   -j REJECT
iptables -I DOCKER-USER -s 172.20.0.0/16 -d 169.254.0.0/16  -j REJECT
```

**No desactives el sandbox de Chromium** (`--no-sandbox`). Es justo lo
que contiene a una página hostil. Si el contenedor no lo permite, dale
las capacidades que necesita o usa un perfil seccomp; no lo apagues.

---

## Qué comprueba el guardián de destinos

| Se bloquea | Por qué |
|---|---|
| Todo esquema que no sea `http`/`https` | `file:`, `data:`, `blob:`, `ftp:`, `chrome:`, `view-source:`, `javascript:` |
| `usuario:clave@host` | vector clásico de suplantación |
| IP escritas a mano, en cualquier forma | `127.0.0.1`, `2130706433`, `0x7f000001`, `010.0.0.1`, `[::1]` |
| `localhost`, `*.local`, `*.internal`, `*.lan`, `*.corp`… | nombres que nunca salen de la red local |
| Nombres de una sola etiqueta (`http://intranet/`) | son intranet por definición |
| Todo lo que **resuelva** a loopback, privada, enlace local, CGNAT, multicast o reservada | incluye `169.254.169.254`, los metadatos de AWS/GCP/Azure |
| IPv4 embebida en IPv6 (`::ffff:127.0.0.1`) | vía de escape clásica |

Se comprueba **en cada petición de documento principal**, así que una
redirección 302 hacia un destino interno muere igual que la original.

Para desarrollo hay dos válvulas, y las dos avisan:

* `security.allowHosts`: lista blanca de nombres concretos.
* `security.allowPrivate`: abre **todo** lo interno. Solo en un
  laboratorio aislado.

El dispositivo hace además su propia comprobación antes de enviar nada
(`flexBrResolveInput` / `flexBrCheckUrl`). Dos barreras independientes:
si una falla, la otra sigue.

---

## Multimedia: qué hace y qué no

**Qué hace.** Detecta el `<video>` visible más grande de la página y, si
el usuario lo pide desde el menú del navegador, envía **fotogramas JPEG
de ese elemento** (MJPEG) por el canal 200. El dispositivo los dibuja en
su reproductor propio, con reproducir/pausar, barra de progreso y
pantalla completa.

**Qué NO hace, y por qué:**

* **No hay audio.** Ninguna de las tres placas de Flex OS tiene salida de
  audio cableada: no hay I2S ni DAC en los tres `.ino`. El reproductor
  del dispositivo lo dice en pantalla en vez de dejar un control de
  volumen que no haría nada.
* **No se elude DRM, ni autenticación, ni firmas, ni controles de
  acceso.** Con contenido protegido Chromium entrega fotogramas en negro
  y eso es lo que se envía. **Netflix, Disney+, Widevine y el contenido
  con DRM quedan explícitamente fuera de alcance.**
* **YouTube no se promete.** No se extraen ni se eluden URL protegidas.
  Lo que ocurra al abrirlo depende de sus condiciones de servicio y de
  su detección de navegadores; no se hace nada para rodearlas.

Es una ruta de **prototipo** y así está etiquetada: sin decodificador de
vídeo por hardware, MJPEG es lo único que las tres placas pueden mostrar.

---

## Rendimiento y límites

| Ajuste | Efecto |
|---|---|
| `capture.bandHeight` | Bandas más bajas = el táctil del dispositivo responde antes entre banda y banda |
| `capture.minIntervalMs` | Suelo del ritmo de captura |
| `capture.keyframeEveryMs` | Cada cuánto se refresca la página entera |
| `limits.maxBytesPerMinute` | Corta una página con animación infinita antes de que se coma la conexión |
| `limits.maxFrameBytes` | Tope por banda; el dispositivo lo baja aún más en su HELLO |
| `limits.sessionIdleMs` | Una sesión abandonada libera su contexto de Chromium |

La captura se dispara por **cambio de la página** (screencast de CDP), no
por reloj: si nadie toca nada, no se envía nada.

## Aislamiento entre dispositivos

Cada dispositivo recibe su propio `BrowserContext`: cookies,
almacenamiento local, caché y permisos separados. Dos placas nunca
comparten la sesión de un sitio, y al cerrar la sesión se cierra el
contexto, con lo que **no queda nada** de ese dispositivo en el
servidor.

## API de control

| Ruta | Devuelve |
|---|---|
| `GET /v1/health` | estado, versión del protocolo, tiempo en marcha, sesiones |
| `GET /v1/version` | versión del servicio, del protocolo y de Node |
| `GET /v1/sessions` | resumen por sesión: pestañas, viewport, fotogramas, KB, inactividad |

`/v1/sessions` **no** expone URL ni títulos: sirve para saber si el
servicio está saturado, no para vigilar lo que hace el usuario.

---

## Pruebas

```bash
npm test
```

* `test/protocol.test.js` — el codec, byte a byte, contra los mismos
  desplazamientos que lee el firmware.
* `test/guard.test.js` — el guardián anti-SSRF, rango por rango,
  incluidos los bordes de cada CIDR y las formas raras de escribir una IP.
* `test/e2e.test.js` — **extremo a extremo**: levanta el servicio con
  Chromium de verdad, sirve una página en local, se conecta como si
  fuera la placa, navega, toca, se desplaza, y **decodifica cada banda
  JPEG recibida con el decodificador del propio firmware**
  (`tests/host/build/jpegcheck`). Esta última prueba se salta sola si
  falta Chromium o si no se ha compilado la herramienta:

```bash
make -C ../tests/host tools     # compila jpegcheck
npm test
```

## Despliegue como servicio

```ini
# /etc/systemd/system/flexos-browser.service
[Unit]
Description=Servicio remoto de navegacion de Flex OS
After=network-online.target

[Service]
Type=simple
User=flexos
WorkingDirectory=/opt/flexos-browser
ExecStart=/usr/bin/node src/server.js --config /etc/flexos/browser.json
Restart=on-failure
RestartSec=5
# El fichero de configuracion lleva credenciales: solo lo lee este usuario.
# 0400, propietario flexos.
NoNewPrivileges=true
PrivateTmp=true
ProtectSystem=strict
ProtectHome=true
ReadWritePaths=/tmp

[Install]
WantedBy=multi-user.target
```

## Configuración por entorno

| Variable | Equivale a |
|---|---|
| `FLEXBR_CONFIG` | `--config` |
| `FLEXBR_HOST`, `FLEXBR_PORT` | `host`, `port` |
| `FLEXBR_TLS_KEY`, `FLEXBR_TLS_CERT` | `tls.key`, `tls.cert` |
| `FLEXBR_DEVICES` | `devices`, en formato `id1:token1,id2:token2` |
| `FLEXBR_ALLOW_PRIVATE=1` | `security.allowPrivate` |
| `FLEXBR_ALLOW_HOSTS` | `security.allowHosts`, separados por comas |
| `FLEXBR_CHROMIUM` | `browser.executablePath` |
| `FLEXBR_LOG_LEVEL` | `logLevel` |

## Problemas frecuentes

**«El servicio NO arranca»** — faltan credenciales en `devices[]`.

**El dispositivo dice «El servidor debe usar wss:// (TLS)»** — o pones
certificados, o activas *Permitir ws:// sin TLS* en los ajustes del
dispositivo (solo para pruebas).

**El dispositivo dice «Credencial no válida»** — el token del
dispositivo no coincide con ninguno de `devices[]`. Ojo con los espacios
al copiarlo.

**«Memoria insuficiente» en el dispositivo** — el navegador no arranca la
sesión remota si no hay heap interno suficiente para TLS. Cierra otras
apps. En Flex OS Pro el margen es muy estrecho: es esperable.

**Todo va lento** — baja `capture.bandHeight` y la calidad, o pon el
perfil *Ahorro de datos* en el dispositivo. Mira `flex://about` para ver
fotogramas descartados y milisegundos de decodificación.

**Chromium no arranca** — `npm run browsers`, o apunta
`browser.executablePath` a un Chromium ya instalado.
