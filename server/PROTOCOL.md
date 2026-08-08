# FBP/1 · Flex Browser Protocol, versión 1

Protocolo entre el **dispositivo** (Flex OS Ultra / Ultra S3 / Pro) y el
**servicio remoto de navegación**.

Implementaciones — las dos tienen que cambiar a la vez:

| Lado | Fichero |
|---|---|
| Dispositivo | `FlexOS_Browser.h` (constantes) · `FlexOS_Browser.cpp` (codec) |
| Servicio | `server/src/protocol.js` |

Las pruebas `server/test/protocol.test.js` comprueban los desplazamientos
byte a byte contra los que lee el firmware; `tests/host/test_browser.cpp`
hace lo propio del otro lado.

---

## 1. Transporte

* **Control**: HTTPS. `GET /v1/health`, `GET /v1/version`, `GET /v1/sessions`.
  Respuestas JSON. No hay ninguna operación de navegación por HTTP: sirve
  para comprobar que el servicio está vivo y con qué versión, que es
  justo lo que hace el botón *Probar el servidor* de los ajustes.
* **Sesión**: WebSocket seguro (`wss://host:puerto/v1/session`), subprotocolo
  `fbp.v1`, **tramas binarias**. Una trama WebSocket = un mensaje FBP.
  Sin compresión por mensaje: la carga útil ya son JPEG.

El dispositivo exige `wss://` salvo que se active explícitamente la opción
de desarrollo *Permitir ws:// sin TLS*, que además avisa en pantalla.

---

## 2. Cabecera (12 bytes)

Todos los enteros en **little-endian** (el orden nativo del ESP32: cero
conversiones en el lado con menos CPU).

```
offset  tamaño  campo
  0       1     'F'  (0x46)
  1       1     'B'  (0x42)
  2       1     versión = 1
  3       1     tipo
  4       1     flags
  5       1     canal
  6       2     seq        (uint16, se reinicia en 1 al desbordar)
  8       4     longitud de la carga
 12       …     carga
```

**Canales**

| Canal | Significado |
|---|---|
| `0` | sesión (mensajes que no pertenecen a una pestaña) |
| `1…N` | pestaña *N* |
| `200` | flujo del reproductor multimedia |
| `201` | reservado para el favicon |

El canal del reproductor va aparte a propósito: así un fotograma de vídeo
que llegue tarde nunca se dibuja en el hueco de la página.

**Cadenas**: `uint16` de longitud en bytes + los bytes UTF-8, **sin
terminador**. Los dos lados recortan sin partir un carácter UTF-8 y
sustituyen los caracteres de control por espacios antes de usarlas.

---

## 3. Mensajes del dispositivo al servicio

| Tipo | Nombre | Carga |
|---|---|---|
| `0x01` | HELLO | ver abajo |
| `0x02` | PING | vacía |
| `0x03` | ACK | `u16 últimoSeq`, `u8 pendientes` |
| `0x10` | NAVIGATE | `str url` |
| `0x11` | BACK | vacía |
| `0x12` | FORWARD | vacía |
| `0x13` | RELOAD | vacía |
| `0x14` | STOP | vacía |
| `0x20` | POINTER | `u8 acción`, `u16 x`, `u16 y`, `u8 botón` |
| `0x21` | SCROLL | `i16 dx`, `i16 dy` |
| `0x22` | KEY | `u8 acción`, `u8 código`, `u8 mods`, `str texto` |
| `0x23` | VIEWPORT | `u16 w`, `u16 h`, `u8 calidad`, `u8 perfil`, `u8 escala%` |
| `0x30` | TAB_NEW | vacía |
| `0x31` | TAB_CLOSE | vacía (la pestaña va en el canal) |
| `0x32` | TAB_SELECT | vacía (la pestaña va en el canal) |
| `0x40` | MEDIA | `u8 orden`, `u32 argumento` |
| `0x41` | REQ_FRAME | vacía — «mándame la página entera» |

### HELLO (17 bytes fijos + 2 cadenas)

```
u8   versión del protocolo   (debe ser 1)
u16  ancho del viewport
u16  alto del viewport
u8   calidad JPEG deseada    (20..90)
u8   perfil                  (0 baja latencia · 1 equilibrado · 2 ahorro)
u32  capacidades del dispositivo
u8   formatos que sabe decodificar (bit 0 = JPEG baseline)
u8   máximo de pestañas que admite
u32  tamaño máximo de un mensaje FRAME que puede recibir
str  credencial del dispositivo
str  identificador del dispositivo
```

El servicio responde WELCOME o cierra con ERROR/AUTH. Un HELLO con otra
versión se rechaza: **no** se negocia a la baja.

### Acciones

* Puntero: `0 down · 1 up · 2 move · 3 tap · 4 cancel`
* Teclado: `0 texto · 1 pulsar · 2 soltar`
* Teclas especiales: `1 Enter · 2 Retroceso · 3 Tab · 4 Esc · 5…8 flechas`
* Reproductor: `0 play · 1 pausa · 2 buscar (arg = ms) · 3 parar · 4 volumen · 5 abrir`

---

## 4. Mensajes del servicio al dispositivo

| Tipo | Nombre | Carga |
|---|---|---|
| `0x81` | WELCOME | ver abajo |
| `0x82` | STATE | `u8 flags`, `u8 progreso`, `str título`, `str url` |
| `0x83` | FRAME | ver abajo |
| `0x84` | MEDIA | `u8 tipo`, `u32 duraciónMs`, `u16 w`, `u16 h`, `str mime`, `str url`, `str título` |
| `0x85` | ERROR | `u16 código`, `str mensaje` |
| `0x86` | PONG | vacía |
| `0x87` | NAVIGATED | `str url` |
| `0x88` | TABS | `u8 nº`, `u8 activa`, luego `u8 id` + `str título` por pestaña |
| `0x89` | DOWNLOAD | `str nombre`, `u32 tamaño`, `str url` |

### WELCOME

```
u8   versión del protocolo
u16  ancho del viewport concedido
u16  alto del viewport concedido
u32  capacidades del servicio
u16  máximo de pestañas concedido
u32  tamaño máximo de FRAME que enviará
str  identificador de sesión
```

### FRAME (18 bytes fijos + imagen)

```
u16  x        \
u16  y         |  región EN COORDENADAS DE PANTALLA del dispositivo
u16  w         |
u16  h        /
u8   formato  (1 = JPEG baseline)
u8   flags    (bit 0 keyframe · bit 1 última región de esta actualización)
u32  frameId
u32  bytes de la imagen
…    imagen
```

**La imagen puede ser más pequeña que `w`×`h`**: si el dispositivo pidió
escala reducida (`VIEWPORT.escala%`), el servicio rasteriza a menos
píxeles y el dispositivo la amplía al volcarla. Eso es lo que permite
que Flex OS Pro reciba justo los píxeles que su panel físico puede
mostrar (240 de ancho sobre un lienzo lógico de 480) en vez del doble.

**El JPEG tiene que ser baseline.** El decodificador del dispositivo
(`FlexOS_JPEG.cpp`) rechaza el progresivo a propósito, así que el
servicio codifica siempre con `progressive: false` y sin mozjpeg.

### Flags de STATE

| Bit | Significado |
|---|---|
| 0 | cargando |
| 1 | se puede ir atrás |
| 2 | se puede ir adelante |
| 3 | HTTPS con certificado válido |
| 4 | HTTP plano o certificado con avisos |
| 5 | la página tiene multimedia reproducible |
| 6 | la página tiene el foco en un campo de texto |

El bit 6 es el que hace que el teclado del dispositivo se abra solo al
pulsar sobre un formulario.

### Códigos de error

`1` credencial · `2` destino bloqueado · `3` fallo de navegación ·
`4` tiempo agotado · `5` límite del servicio · `6` error interno ·
`7` certificado del sitio · `8` mensaje mal formado · `9` sesión caducada

El dispositivo muestra **su propio texto** según el código; el mensaje
del servicio solo se usa como pista en los registros. Un servicio
comprometido no puede escribir lo que quiera en la pantalla.

---

## 5. Control de flujo

Es la parte que más se nota en el uso real, así que va explícita:

1. El servicio envía **como mucho un FRAME por canal sin confirmar**.
2. El dispositivo dibuja y envía `ACK` (con el último `seq` dibujado).
3. Hasta que llega el `ACK`, el servicio **no genera** el siguiente
   fotograma: no se acumula trabajo que llegaría tarde.
4. El dispositivo, por su lado, si le llega un FRAME mientras aún tiene
   uno sin dibujar, **descarta el viejo**: el nuevo es más útil.
5. Mientras el dispositivo no consume, la tarea de red no lee más del
   socket, así que la ventana TCP frena al servicio sola.

No hay una tasa de fotogramas prometida. Los perfiles ajustan calidad y
escala, no FPS. Lo que se mide de verdad está en `flex://about` del
dispositivo (fotogramas dibujados, descartados, ms de decodificación,
bytes por minuto).

## 6. Regiones sucias

Después del primer keyframe, el servicio compara el hash de cada banda
horizontal con el de la captura anterior y **solo envía las que han
cambiado**. Cada cierto tiempo (`capture.keyframeEveryMs`) manda la
página entera marcada como keyframe, para recuperarse de un fotograma
perdido. El dispositivo también puede pedir un keyframe con `REQ_FRAME`
(lo hace al repintar tras cerrar un menú o si una banda no decodifica).
