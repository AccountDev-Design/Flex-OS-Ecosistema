# Flex Phone

> ¿Solo quieres **navegar usando el móvil como servidor**? Esa guía, paso a
> paso y sin tecnicismos, está en
> [`docs/NAVEGADOR-CON-EL-MOVIL.md`](NAVEGADOR-CON-EL-MOVIL.md).

Ecosistema que conecta un teléfono Android con **Flex OS Ultra**
(ESP32‑P4) por **Wi‑Fi**: notificaciones reales, respuestas rápidas cuando
Android las permite, control multimedia y un navegador servido por el propio
teléfono.

Este documento describe **lo que hay implementado**, **lo que está pendiente de
prueba física** y **lo que no es posible**, sin mezclar las tres cosas.

---

## 1. Arquitectura

```
                        FLEX PHONE
                             │
          ┌──────────────────┴──────────────────┐
          │                                     │
   ANDROID (el teléfono)                 FLEX OS ULTRA (P4)
   servidor del enlace                   cliente del enlace
          │                                     │
          └──────────────── Wi-Fi ──────────────┘
                             │
                      FLEX LINK v2
                             │
        ┌────────────────────┼────────────────────┐
        │                    │                    │
  Notificaciones         Navegador          Estado / caps
```

### Por qué el teléfono es el servidor

El teléfono está encendido siempre y tiene un servicio en primer plano que lo
mantiene vivo; el reloj se suspende. Al revés, **cada suspensión del P4
cortaría el enlace**.

### Las capas, y qué se prueba dónde

| Capa | Firmware | Android | Se prueba |
|---|---|---|---|
| Protocolo (trama, CRC, fragmentos) | `FlexOS_FlexLink.{h,cpp}` | `protocol/FlexLink.kt` | PC y JVM |
| Emparejamiento y sesión | `FlexOS_FlexAuth.{h,cpp}` | `protocol/FlexAuth.kt` | PC y JVM |
| Transporte (interfaz) | `FlexOS_FlexPhone_Transport.{h,cpp}` | — | PC |
| Transporte Wi‑Fi | `FlexOS_FlexPhone_WiFi.h` | `link/WifiLinkServer.kt` | placa / teléfono |
| Transporte BLE (futuro) | — | `link/GattServer.kt` | ver §8 |
| Máquina del enlace | `FlexOS_FlexPhone_Link.{h,cpp}` | — | PC |
| Modelo | `FlexOS_FlexPhone.{h,cpp}` | `domain/FlexPhoneState.kt` | PC |
| Adaptador de dispositivo | — | `device/DeviceAdapter.kt` | teléfono |
| Interfaz | `FlexOS_FlexPhone_{UI,Bridge}.h` | `ui/` | placa / teléfono |
| Overlays del sistema | `FlexOS_FlexPhone_Overlay.h` | — | placa |

**La lógica no sabe por dónde viajan las tramas.** El enlace habla con un
`FlexPhoneTransport` y nada más. Por eso se compila y se ejercita entero en el
PC contra un transporte de lazo, con un teléfono simulado que habla el
protocolo de verdad.

---

## 2. Transporte

### Wi‑Fi — el actual

Dos sockets, con los **mismos números en los dos lados**
(`FlexOS_FlexPhone_WiFi.h` y `WifiLinkServer.kt`):

| Puerto | Qué |
|---|---|
| TCP **47820** | el enlace: tramas de Flex Link, una detrás de otra |
| UDP **47821** | descubrimiento |

### Descubrimiento — en los dos sentidos

No por simetría: **por fiabilidad**. Preguntan los dos lados, y cualquiera de
las dos preguntas basta para conectar.

| Quién emite | Paquete | Qué lleva |
|---|---|---|
| reloj → difusión | `FLEXPHONE?` | `u8 ver` |
| teléfono → unidifusión | `FLEXPHONE!` | `u8 ver` · `u16 puertoTcp` · `str nombre` |
| **teléfono → difusión** | `FLEXOS?` | `u8 ver` · `u16 puertoTcp` · `str nombre` |
| **reloj → unidifusión** | `FLEXOS!` | `u8 ver` · `u8 flags` · `str id` · `str nombre` |

`flags` bit 0 = el reloj está **ahora** enseñando un código de emparejamiento;
la app lo usa para destacarlo en la lista. `str` es un byte de longitud seguido
de esos bytes, sin cero final; `u16` va con el byte bajo primero.

**Por qué hacía falta el segundo sentido.** Que el reloj emita y Android
*reciba* una difusión es la dirección **frágil**: el controlador Wi‑Fi de
Android descarta las tramas de difusión que no van dirigidas a la MAC del
teléfono **antes** de que lleguen a ningún socket, para ahorrar batería. El
`MulticastLock` (permiso `CHANGE_WIFI_MULTICAST_STATE`) desactiva ese filtro,
pero no cubre la pantalla apagada ni la app en segundo plano en todos los
fabricantes. Síntoma: el ESP32 emite la sonda perfectamente, el teléfono está
escuchando en el puerto correcto y `receive()` no despierta nunca — sin ningún
error que lo explique en ninguna de las dos puntas.

Al revés es **sólido**, y por dos motivos:

1. **Emitir** desde Android no lo filtra nadie, y la respuesta del reloj vuelve
   en **unidifusión**, que tampoco.
2. Cuando el reloj recibe `FLEXOS?` **ya sabe todo lo que necesita**: la
   dirección viene en el origen del paquete y el puerto en la carga. O sea que
   puede abrir la conexión aunque su propia difusión no haya llegado nunca a
   ninguna parte.

Por eso `FlexLinkService` emite `FLEXOS?` **cada cinco segundos mientras no hay
sesión** (unos 40 bytes; con la sesión abierta deja de emitir del todo), y el
reloj atiende el socket UDP **en cada vuelta de su tarea**, tenga teléfono o no
— antes solo lo leía 400 ms de cada 2 s y **solo mientras buscaba**, así que un
reloj ya emparejado era invisible para cualquier móvil que intentara
encontrarlo.

**Quién aprende a quién.** Un destino solo se aprende cuando **no hay
ninguno**, ni en el reloj ni en el teléfono. Así la respuesta de otro
dispositivo de la red no puede robar un enlace a media conversación, que es
exactamente la sesión duplicada que hay que evitar.

**Respaldo sin difusión.** Si la red filtra o aísla a los clientes, no hace
falta difusión en absoluto:

- En la app: **Emparejar → Buscar por dirección**. Se teclea la dirección del
  reloj (la enseña Flex OS en *Flex Phone → Conexión → Este reloj*) y el
  `FLEXOS?` va en unidifusión. El reloj aprende de ese paquete la dirección del
  teléfono y conecta.
- En el reloj: `flexPhoneWifiSetHost()` fija a mano la dirección del teléfono
  (la enseña la app en *Emparejar → Dirección de este teléfono*).

Los dos caminos siguen existiendo. Ninguno sustituye al descubrimiento
automático: lo respaldan.

**Los bytes están escritos dos veces** — `FlexOS_FlexPhone_Discovery.h` y
`protocol/Discovery.kt` — así que los dos ficheros tienen **vectores dorados
idénticos** en `tests/host/test_flexphone_discovery.cpp` y `DiscoveryTest.kt`.
Si alguien mueve un campo en un lado, una de las dos baterías falla. Sin eso,
un desajuste solo se ve como «el reloj no aparece en la lista».

**Para depurarlo.** Pon `FLEXOS_DIAG_FLEXPHONE` a 1 en
`FlexOS_FlexPhone_WiFi.h` para ver los pasos del reloj por el puerto serie, y
mira el logcat con la etiqueta `FlexPhone/WifiLink` para los del teléfono.

Sentido reloj → teléfono (el frágil):

| Se ve | No se ve | Dónde está el problema |
|---|---|---|
| (a) | (b) | el cerrojo de multidifusión, o aislamiento de clientes en el router |
| (a) (b) | (c) | el teléfono descarta la sonda: mira la versión de protocolo |
| (a) (b) (c) | (d) | la respuesta se pierde de vuelta: es la red, no el código |
| nada | (a) | el enlace no está arrancado en el reloj |

Sentido teléfono → reloj (el fiable):

| Se ve | No se ve | Dónde está el problema |
|---|---|---|
| (1) | (b) del reloj | la difusión no sale del teléfono: prueba *Buscar por dirección* |
| (1) y (b) | (2) | la respuesta en unidifusión se pierde: es la red |
| nada | (1) | el enlace no está encendido en el teléfono, o no hay Wi‑Fi |

**Enmarcado.** TCP es un flujo y no respeta los límites de las tramas. Se
reconstruyen con la propia cabecera de Flex Link, que ya lleva su longitud: no
hace falta un segundo enmarcado. Si la marca no cuadra, el flujo está
descolocado y **se corta y se reconecta** — buscar la siguiente marca a ciegas
en un flujo corrupto acaba interpretando basura como si fueran mensajes.

**Dónde corre.** En su propia tarea FreeRTOS, igual que el navegador. Entre esa
tarea y el bucle gráfico solo pasan tramas completas por dos colas de tamaño
fijo con un mutex corto. El hilo gráfico **nunca** llama a `connect()`,
`read()` ni `write()`.

### BLE — preparado, hoy no se usa

El ESP32‑P4 **no tiene radio Bluetooth**: `soc_caps.h` del SDK no define
`SOC_BLE_SUPPORTED` para ese chip. Mientras siga así, el otro extremo de un
servidor GATT no existe, y anunciarse por BLE sería gastar batería del teléfono
para que no llame nadie.

`GattServer.kt` **se conserva a propósito**: es el segundo transporte de la
arquitectura. Lo que le falta para volver a estar vivo:

1. firmware `esp-hosted` en el C6 compilado **con Bluetooth**, exponiendo HCI
   por el mismo transporte SDIO que ya lleva el Wi‑Fi;
2. una pila de host BLE (NimBLE) en el P4 contra ese controlador remoto;
3. actualizar ese servidor al apretón de manos de Flex Link v2 (`T_AUTH_*`),
   que es lo que sustituyó al bonding como fuente de autenticación.

⚠️ **No sobrescribas el firmware del C6 a ciegas**: si pierdes el `slave`
actual, pierdes también el Wi‑Fi. Haz copia antes.

En el manifiesto de Android, BLE está declarado con `required="false"`. Con
`"true"` quedaban fuera de Google Play teléfonos donde la app funciona
perfectamente. Sus permisos **no se piden en tiempo de ejecución**.

---

## 3. Protocolo Flex Link v2

Implementaciones — **las dos cambian a la vez**:

| Lado | Fichero |
|---|---|
| Firmware | `FlexOS_FlexLink.{h,cpp}` |
| Android | `android/FlexPhone/protocol/.../FlexLink.kt` |

### Por qué v2

v1 iba sobre BLE, y el bonding de BLE autenticaba y cifraba **por debajo**. Un
socket TCP en la red local no hace nada de eso, así que v2 añade apretón de
manos con clave (`T_AUTH_*`) y negociación de capacidades (`T_CAPS`).

Un extremo v1 no sabe demostrar que tiene la clave, y por eso v2 **no lo
acepta**: sería abrir sesión a quien no puede probar que emparejó. Que el
rechazo sea por versión hace que el usuario lea «actualiza la app» en vez de un
fallo mudo.

### Cabecera (18 bytes, little‑endian)

```
offset  tamaño  campo
  0       2     magia 0xF1 0x58
  2       1     versión
  3       1     tipo
  4       2     sesión
  6       2     paquete (todos los fragmentos comparten el suyo)
  8       1     índice de fragmento
  9       1     total de fragmentos
 10       2     longitud de la carga EN ESTA trama
 12       4     contador monótono (anti-repetición)
 16       2     CRC16-CCITT de [0..15] + carga
```

**Límites**: trama ≤ 244 B, carga ≤ 226 B, mensaje reensamblado ≤ 2048 B, ≤ 16
fragmentos. Por Wi‑Fi cabría mucho más y **aun así se conserva el mismo tope**:
subirlo obligaría a que los dos extremos llevaran buffers distintos según el
transporte, y el único premio serían menos fragmentos en mensajes que ya caben
en uno o dos.

### Tipos de mensaje

| Rango | Contenido |
|---|---|
| `0x01–0x07` | HELLO, WELCOME, PING, PONG, BYE, ACK, ERR |
| `0x08–0x0A` | **AUTH_CHALLENGE, AUTH_RESPONSE, AUTH_OK** (v2) |
| `0x10–0x13` | PAIR_REQ, PAIR_CODE, PAIR_CONFIRM, UNPAIR |
| `0x20–0x23` | NOTIF_ADD, NOTIF_UPDATE, NOTIF_REMOVE, NOTIF_CLEAR |
| `0x30–0x33` | REPLY_REQ, REPLY_RESULT, ACTION_REQ, ACTION_RESULT |
| `0x40–0x43` | PHONE_STATE, TIME_SYNC, FIND_START, FIND_STOP |
| `0x44` | **CAPS** (v2) |
| `0x50–0x51` | MEDIA_STATE, MEDIA_CMD |
| `0x60–0x62` | RELAY_START, RELAY_STOP, RELAY_INFO |

Los números **van al aire y nunca se reordenan**. Un tipo desconocido se ignora
en silencio: es lo que permite añadir mensajes sin romper un firmware antiguo.

### Garantías

- CRC16‑CCITT sobre cabecera y carga.
- Fragmentación con un solo parcial en vuelo y caducidad a los 5 s.
- Contador monótono con ventana de 32.
- Reintentos con espera progresiva (500 ms → 30 s) y **límite**.
- Todo tamaño que viene del aire se valida **antes** de usarse.

### Vectores dorados

El firmware y la app implementan el protocolo por separado. Si alguien mueve un
campo en un solo lado no salta ningún error de compilación: el P4 empieza a
descartar tramas por CRC y se ve como *«el teléfono no conecta»*. Los bytes
exactos están fijados en **las dos** baterías:

- `tests/host/test_flexlink_vectors.cpp` y `tests/host/test_flexauth.cpp`
- `android/.../protocol/src/test/kotlin/.../FlexLinkTest.kt` y `FlexAuthTest.kt`

---

## 4. Emparejamiento y sesión

### El flujo

```
Flex OS (cliente TCP)                   Teléfono (servidor TCP)
   │── HELLO {ver, id} ───────────────────────────►│
   │◄─────────────────── WELCOME {ver, id, nombre} │

   ── sin vínculo: EMPAREJAMIENTO ──
   │── PAIR_CODE {sal, reto, id} ─────────────────►│   (el código NO viaja)
   │        el usuario lee el código en Flex OS
   │        y lo TECLEA en el teléfono
   │◄────────────────────── PAIR_CONFIRM {prueba}  │
   │── AUTH_OK {prueba de Flex OS} ───────────────►│

   ── con vínculo: SESIÓN ──
   │── AUTH_CHALLENGE {reto, sesión} ─────────────►│
   │◄───────────────────── AUTH_RESPONSE {prueba}  │
   │── AUTH_OK {prueba de Flex OS} ───────────────►│

   │◄──────────────────────────────── CAPS ────────│
   │◄──────────────────────── PHONE_STATE ─────────│
```

### Derivación

```
clave = HMAC( código , "flexphone-pair-v2" || sal || idFlexOS || idTeléfono )
```

**El código de 6 dígitos nunca se transmite.** Por la red solo viajan la sal y
los identificadores, que son públicos. La prueba de host recorre *todo* lo que
Flex OS pone en el canal y falla si el código aparece.

Los identificadores entran con su **longitud delante**: sin eso, `("ab","c")` y
`("a","bc")` darían la misma clave.

Seis dígitos es poca entropía, y por eso no basta con la derivación: el código
**caduca a los dos minutos** y cada emparejamiento genera una sal nueva, así
que no hay ventana práctica para probarlos todos contra un extremo vivo.

### Reto‑respuesta MUTUO

```
pruebaTeléfono = HMAC(clave, "flexphone-peer-v2" || reto || sesión)
pruebaFlexOS   = HMAC(clave, "flexphone-host-v2" || reto || sesión)
```

Las dos etiquetas son **distintas a propósito**: reenviar la prueba ajena no
sirve de nada. Sin la mitad de Flex OS, un equipo cualquiera de la red podría
hacerse pasar por el reloj y quedarse con todas las notificaciones del usuario.
Autenticar en un solo sentido sería justo la mitad útil.

### Dónde vive la clave

| Lado | Dónde |
|---|---|
| Flex OS | NVS (`flexphone/bondkey`), no LittleFS: es material de clave |
| Android | envuelta con AES/GCM de una clave del **Android Keystore** |

En Android, lo que acaba en disco es un cifrado. Revocar borra el cifrado **y**
la clave envolvente: dejarla viva permitiría descifrar una copia de seguridad
antigua del fichero.

Una clave que no se puede descifrar (restauración del teléfono, reinstalación)
se limpia y se pide emparejar de nuevo, en vez de autenticar con basura.

### El límite real, sin adornos

**La carga viaja EN CLARO por la red local.** Esto impide que un dispositivo
**no emparejado** abra sesión; **no** protege frente a quien ya esté escuchando
la misma red. No es TLS y la interfaz no lo enseña como si lo fuera — hay una
tarjeta que lo dice, en Flex OS y en el teléfono.

### Lo que el servidor rechaza

- más de una sesión a la vez (la segunda conexión se cierra);
- cualquier mensaje que no sea del apretón de manos sin sesión autenticada;
- una conexión que no autentica en 15 s **si no hay emparejamiento en
  marcha** (con sesión de emparejamiento abierta, el plazo pasa a ser el de
  esa sesión: los mismos 2 min que el reloj — ver *Una sola fuente de verdad*);
- una sesión sin tráfico en 40 s;
- una trama de otra sesión, repetida, corrupta o de versión futura.

> Abrir un puerto en la red local significa que **cualquier** equipo de esa red
> puede llamar. Nada de lo de arriba se relaja para que el emparejamiento sea
> más fácil: el reloj **no** acepta una conexión de la LAN por el hecho de
> venir de la LAN. Lo único que se mejoró fue **encontrarse**; quien se
> encuentra sigue teniendo que demostrar que tiene la clave.

### Ningún estado infinito

Toda espera de este flujo termina — en éxito o en un motivo escrito. Los
plazos, y de dónde sale cada uno:

| Espera | Plazo | Quién lo aplica | Qué pasa al vencer |
|---|---|---|---|
| alguien abre el puerto y **no se presenta** | 15 s | teléfono | se cierra y se libera la sesión |
| un Flex OS presentado que **aún no empareja** | 2 min | teléfono | se cierra y se libera la sesión |
| conexión con emparejamiento en marcha | lo que le quede a la sesión (2 min) | teléfono | `PairingFailed` con `CODE_EXPIRED` |
| código enviado sin confirmar | 12 s | teléfono | `PairingFailed` con `LINK` — **el socket NO se cierra** y el código **se conserva** |
| código en pantalla sin confirmar | 2 min desde `pair.startedMs` | reloj | cierra la sesión **y avisa** con `T_ERR`/`E_TIMEOUT` |
| autenticación de sesión | `FLP_LINK_AUTH_TIMEOUT_MS` | reloj | vuelve a buscar |
| sesión sin tráfico | 40 s | los dos | se corta y se reconecta — **no corre mientras se empareja** |

### El latido también mientras se empareja

Durante un emparejamiento el canal está **legítimamente mudo**: el reloj manda
su sal una vez y se calla, y el teléfono no tiene nada que decir hasta que el
usuario teclee seis dígitos. Eso son cuarenta, cincuenta o setenta segundos de
silencio perfectamente normales.

El plazo de inactividad de 40 s del teléfono daba ese socket por muerto y lo
cerraba. El usuario terminaba de teclear, pulsaba *Emparejar* y leía **«se
cortó la conexión al enviar el código»** — con el código correcto y la ventana
todavía abierta en los dos lados. El emparejamiento **moría de silencio**.

Ahora:

- el reloj **late** (`FLNK_T_PING`) también en `FLP_LS_PAIRING` y `FLP_LS_AUTH`,
  no solo en `READY`. `PING`/`PONG` entran en `typeAllowedWithoutSession()`
  porque el latido hace falta justo cuando todavía no hay sesión; no llevan
  carga, no abren sesión y no tocan el modelo (el teléfono ya los aceptaba sin
  sesión por lo mismo);
- el plazo de inactividad del teléfono **no corre** mientras hay una sesión de
  emparejamiento viva — el cinturón además de los tirantes, y lo que cubre a un
  reloj con firmware anterior;
- al abrirse la sesión, el reloj de «enlace muerto» **arranca de cero**. Un
  emparejamiento puede durar minutos, así que `lastRxMs` podía llegar a `READY`
  más viejo que los 30 s del plazo: el enlace se declaraba muerto en el cuadro
  siguiente a emparejar bien.

### La sesión persistente: una conexión, un dueño

Después de emparejar, el enlace entraba en un ciclo
`CONECTADO → DESCONECTADO → CONECTADO` de aproximadamente **1 Hz**.

**Quién colgaba primero: el teléfono**, y por dos motivos distintos.

**1) La limpieza de una conexión cerraba la de otra.** Todo el estado vivía en
campos compartidos del servidor (`sock`, `out`, `authed`, `txCounter`…):
`serve()` los pisaba al entrar y `closeSession()` los borraba al salir, fuera
de quién fuera la sesión en ese momento.

```
muere el socket A            (basta un hipo de Wi-Fi)
  → el reloj reconecta 1 s después           → socket B
  → el hilo de A sigue dentro de read()      → accept() cierra B: "ya hay sesión"
  → el reloj reconecta                        → socket C
  → el hilo de A despierta y su finally cierra "la sesión"... que ya es C
```

Los dos últimos pasos se rearman entre sí: la conexión buena moría siempre a
manos de la limpieza de la anterior.

**2) Un socket muerto guardaba el hueco.** Cuando el socket del reloj muere
**sin aviso** — se va el Wi-Fi, el reloj se reinicia — en el teléfono no hay
ningún FIN que leer: `read()` sigue bloqueado y esa conexión conserva la sesión
hasta que salte el plazo de inactividad, 40 s después. Mientras tanto el reloj
reintentaba cada segundo y se le cerraba la puerta cada segundo.

Ahora:

| Antes | Ahora |
|---|---|
| estado de la conexión en campos compartidos | cada conexión es un objeto `Conn` con su estado dentro |
| `closeSession()` cerraba «la sesión activa» | `closeSession(c)` cierra **su** conexión, y solo si aún es la dueña |
| `accept()` cerraba a quien llegara si había sesión | se le sirve, pero **no** se le da la sesión |
| el hueco se soltaba al morir el socket | el hueco **cambia de dueño al autenticarse** |

La regla es una sola: **el hueco de la sesión solo cambia de dueño cuando
alguien completa la autenticación.** Un desconocido no puede autenticarse, así
que no puede desalojar a nadie — lo fija `testUnaSolaSesion`. Y el reloj de
verdad, que sí puede, recupera su sesión sin esperar a que se enfríe el
cadáver.

El reloj, por su parte, pasa a reintentar con **espera progresiva** en vez de
cada segundo fijo (`FLPW_RECONNECT_MAX_MS`), y una conexión que aguanta
`FLPW_RECONNECT_OK_MS` reinicia esa cuenta. Eso **no** es lo que cura el ciclo
—eso se cura en el teléfono— pero evita martillear el puerto cuando de verdad
no se puede conectar.

### Cómo se comprueba: `tests/link/`

El fallo era una **carrera entre hilos y sockets**, y reproducirla a mano en el
teléfono depende de la suerte. `tests/link/run.sh` compila el
`WifiLinkServer` **real** —el mismo fichero que va en el APK— contra sockets
TCP de verdad, con tres dobles mínimos del SDK (`Context`, `WifiManager`,
`Log`), y provoca la carrera a propósito:

| Comprobación | Qué fija |
|---|---|
| una sesión sana no se cae sola | ocho latidos seguidos sin un solo cierre |
| reconectar en el acto | la limpieza de la vieja no mata a la nueva |
| seis reconexiones a 1 Hz | el ciclo del usuario termina en **una** sesión estable |
| el cadáver no bloquea el hueco | el reloj vuelve a entrar sin esperar 40 s |
| un segundo Flex OS | **no** desaloja al que ya tiene la sesión |

La cuarta falla con el código anterior y pasa con el actual: ese es el cambio.

### El reto de cada sesión se siembra

Al pasar el emparejamiento a tener su propio reto (`pair.nonce`), el reto de
**sesión** (`L->nonce`) se quedó sin sembrar: un vínculo ya guardado
reconectaba siempre con el mismo (todo ceros). La sesión se abría igual —los
dos extremos usan el que viaja— pero un reto predecible es justo lo que el
reto‑respuesta existe para evitar. Ahora se genera en cada `AUTH_CHALLENGE`,
con la fuente de azar que instala el puente (`flexPhoneLinkSetRandom`), y
`testSessionNonceVaries` fija que dos reconexiones nunca comparten reto.

### Un envío que no sale no es un rechazo

Si la prueba no llegó a salir (socket caído, o Flex OS sin contestar), el
código tecleado **se conserva**: `PairingSession.onSendFailed()` vuelve a
`AWAITING_CODE` sin borrarlo ni contar un rechazo, y `proofForTypedCode()` lo
reenvía solo en cuanto el reloj reabre el canal. Tratarlo como rechazo borraba
el código y obligaba a teclear otra vez exactamente lo mismo, contra un reloj
que seguía enseñando exactamente el mismo código.

### Una sola fuente de verdad: la sesión de emparejamiento

El código, la sal, el reto, la clave a medio derivar y el plazo **pertenecen a
una sesión**, con un solo dueño en cada lado:

| Lado | Dónde vive | Nace | Muere |
|---|---|---|---|
| Flex OS | `FlexPhonePairing` (`FlexOS_FlexPhone_Link.h`) | `flexPhoneLinkBeginPairing`, y **solo** desde el botón *Emparejar teléfono* | caduca, se cancela, se completa, o el usuario pide otro |
| Android | `PairingSession` (`:protocol`) | al llegar una sal **distinta** | caduca, se completa, o se para el servicio |

La interfaz del reloj pinta `flexPhoneLinkCode()`; el servidor valida
`pair.key`, derivada de **ese mismo** `pair.code` con **esa misma** `pair.salt`.
No hay una segunda copia en ninguna capa.

**El canal no manda sobre la sesión.** El socket puede caerse y volver las
veces que quiera: el código no cambia, el estado sigue siendo *emparejando* y
la sal se reenvía tal cual al reconectar. El plazo cuelga de `startedMs`, así
que un vaivén de red no le regala otros dos minutos.

### Cuatro fallos que hacían que el código «no coincidiera»

Todos daban el mismo síntoma —*tecleo lo que veo y me dice que no coincide*— y
ninguno estaba en la comparación:

1. **El teléfono contestaba con un código viejo.** `typedCode` vivía suelto en
   `FlexLinkService` y sobrevivía a los intentos fallidos. Al pulsar otra vez
   *Emparejar teléfono*, el reloj abría una sesión nueva (código **y sal**
   nuevos) y la app mandaba en el acto la prueba del intento **anterior**. El
   reloj la rechazaba, con razón. Ahora un código pertenece a la sal para la
   que se tecleó: una sal distinta es una sesión distinta y nace sin código.
2. **El teléfono cerraba el socket cada 15 s.** El plazo de autenticación
   corría desde el `accept()`, o sea **mientras el usuario leía seis dígitos**
   de la pantalla del reloj — y el reloj abre el socket en cuanto descubre el
   teléfono, que puede ser mucho antes de que nadie pulse nada. El reloj
   reconectaba, y vuelta a empezar.
3. **Ese ciclo sacaba al reloj de *emparejando*.** Un `FLP_TC_FAILED` llevaba
   el enlace a *buscando* pasara lo que pasara, y al reconectar el `WELCOME`
   lo devolvía a *emparejando* con el código viejo todavía en memoria: eso es
   el **parpadeo del código** que se veía en la pantalla del reloj.
4. **El plazo se reiniciaba solo.** La caducidad colgaba de `stateSinceMs`,
   que lo reinicia cualquier cambio de estado. Cada corte del canal le daba al
   código otra ventana entera, y el aviso de caducidad no llegaba nunca.

Y dos cosas que ahora dice bien la interfaz: un rechazo **no** tira la sesión
(el reloj sigue enseñando el mismo código, así que se puede corregir un dígito
y reintentar), y el mensaje de error corresponde al fallo real —
`CODE_REJECTED`, `CODE_EXPIRED` o `LINK` — en vez de mandar siempre a revisar
unos dígitos que podían estar bien.

Tres fallos concretos que hacían que la pantalla de emparejamiento se quedara
en «Comprobando…» para siempre, y lo que los curaba:

1. **La sal se tiraba.** `onPairCode` solo la guardaba dentro de
   `completePairing`, o sea **solo si el usuario ya había tecleado el código**.
   En el orden normal —Flex OS manda la sal y el usuario teclea después— se
   perdía, y `submitPairingCode` salía por `pendingSalt ?: return false` sin
   enviar nada. Ahora la sal se guarda **siempre** al recibirla.
2. **El resultado se descartaba.** `FlexLinkService.submitPairingCode`
   devolvía `true` pasara lo que pasara. Ahora devuelve el resultado real y la
   pantalla lo usa.
3. **`T_ERR` se tiraba.** Estaba en la lista de tipos permitidos sin sesión,
   pero **no tenía rama** en el `when`, así que caía en el `else` y, sin
   autenticar, no hacía nada. Cuando Flex OS avisaba de que el código estaba
   mal, en el teléfono no se enteraba nadie. Ahora hay rama, y el reloj además
   **manda** ese aviso al fallar la prueba y al caducar la ventana.

Y un cuarto que dejaba la única sesión ocupada casi un minuto: el plazo de
autenticación se comprobaba **después** de que `read()` volviera, y `read()`
bloqueaba los 40 s del plazo largo. Ahora el socket despierta cada 2 s solo
para mirar los plazos.

---

## 5. Capacidades

**No todos los Android pueden lo mismo**, y del mismo teléfono no siempre está
todo concedido. Por eso van **dos mapas** y no uno:

| Mapa | Qué es |
|---|---|
| `supported` | lo que ese modelo de teléfono puede hacer |
| `granted` | lo que además está permitido y activo **ahora** |

La diferencia entre los dos es justo lo que se le puede explicar al usuario:
*«tu teléfono puede, pero falta darle el acceso a notificaciones»*. Con un solo
mapa habría que elegir entre esconder la función o enseñarla rota.

**Flex OS no ofrece como funcional nada que no esté en `granted`.** Un botón
que no hace nada es peor que no tenerlo.

| Bit | Función |
|---|---|
| `NOTIF` | leer notificaciones |
| `REPLY` | responder (Android dio `RemoteInput`) |
| `MEDIA` | control multimedia |
| `RELAY` | servidor del navegador |
| `FIND` | encontrar mi teléfono |
| `STATE` | estado del dispositivo |
| `TIME` | sincronizar la hora |
| `BLE` | el teléfono puede hablar BLE |

`BLE` **no se concede** aunque el teléfono lo tenga: hoy no hay transporte BLE
al otro lado, y anunciarlo como concedido sería ofrecer algo que no existe.

---

## 6. Compatibilidad Android

El Galaxy A55 es el **teléfono de referencia, no la plataforma**.

`device/DeviceAdapter.kt` es la **única** pieza que mira `Build.MANUFACTURER`.
Todo lo de arriba —protocolo, enlace, notificaciones, servidor— habla con ese
adaptador. Si el núcleo consultara Samsung directamente, cada arreglo para un
Samsung sería un riesgo nuevo para un Pixel, un Motorola o un Xiaomi.

- **Sin APIs privadas.** Nada de Knox, One UI ni reflexión sobre clases
  ocultas: eso ata la app a un fabricante y se rompe en la siguiente versión.
- **Las diferencias de fabricante son opcionales.** Lo que el adaptador no
  reconoce cae al camino estándar de Android, que es el que funciona en todos.
  Hoy lo único específico por fabricante es el **texto del consejo** sobre
  restricciones de segundo plano — no se cambia ningún comportamiento.
- **Solo se lee lo necesario**: no hay IMEI, ni número de serie, ni cuentas, ni
  ubicación.

`minSdk` sigue en **26** y `targetSdk` en **35**: no se ha subido nada.

---

## 7. Notificaciones en Flex OS

```
   Notificaciones Android        Avisos nativos de Flex OS
             │                              │
             └──────────────┬───────────────┘
                            │
                   Centro de notificaciones
                            │
              ┌─────────────┴─────────────┐
              │                           │
      Centro (borde izq.)          Banner flotante
```

### Gestos

| Gesto | Abre |
|---|---|
| Borde **izquierdo** | Centro de notificaciones |
| Borde **derecho** | Panel rápido (Control Center) |
| Borde **superior** | Panel rápido (el de siempre, se conserva) |

El borde superior no se retira: cambiaría un gesto ya aprendido y no hace falta
para añadir el otro.

**Los dos gestos nuevos exigen intención.** No basta con que el gesto nazca en
la franja del borde: el del borde derecho necesita que el dedo se haya movido
hacia dentro más de lo que se ha movido en vertical. Sin eso, un
desplazamiento vertical que empiece cerca del borde abriría el panel por
accidente dentro de una lista o de un juego.

### El banner flotante

La isla dinámica (`FlexOS_Ultra_Notif.h`) solo vive en el escritorio: compone
sobre `homeBuf`, que solo es un fondo válido ahí. Una notificación del teléfono
tiene que verse **estés donde estés**, así que el banner sigue el patrón del
aviso de caída: captura su banda, dibuja encima y la devuelve pixel a pixel al
cerrarse.

Con una diferencia que es la importante: **no es modal**. No se queda la
pantalla, no para la app de debajo y no le roba el toque salvo que el dedo
caiga dentro de su tarjeta. Y compone **después** de que la pantalla de debajo
haya dibujado, así que siempre queda encima sin parpadear.

| | |
|---|---|
| Vertical | banda compacta **arriba**. Nunca en el centro |
| Horizontal | arriba y **más estrecho**: una banda a lo ancho se comería la mitad útil de un juego |
| A la vez | **uno**. Lo que llega detrás espera turno y se resume (`+3`), no se apila |
| Descartar | arrastre a la izquierda; si viene del teléfono, se descarta **también allí** |
| Tocar | abre Flex Phone en Notificaciones |
| Solo | a los 4,2 s — y no mientras el dedo lo está tocando |

**DeX es una exclusión dura.** Con el escritorio de Modo PC delante el usuario
está trabajando con ventanas colocadas a mano, y taparlas con un aviso del
teléfono es desproporcionado. Se registra igual y está en el Centro.

### Silencio y No molestar

|  | Centro | Banner | Sonido |
|---|---|---|---|
| Normal | ✓ | ✓ | ✓ *(ver abajo)* |
| Silencio del sistema | ✓ | ✓ | ✗ |
| **No molestar** | ✓ | ✗ | ✗ |

En los tres casos la notificación **se registra**. Silenciar no es tirar.

No molestar es un estado real y persistido, con interruptor en el panel rápido
(`QSID_DND`) y otro en la cabecera del Centro.

> **Sonido: hoy no lo hay, y no se finge.** El audio de Flex OS es una tubería
> PCM (`flexAudioStartPcm` / `flexAudioWrite`): no existe un camino de «sonido
> de notificación», y fabricar un tono desde el bucle gráfico es exactamente lo
> que este módulo no puede hacer. `phoneCanSound()` ya está escrito y dice la
> verdad sobre cuándo *se podría*; el día que exista un reproductor de avisos,
> el único cambio es llamarlo ahí dentro.

---

## 8. Navegador

El navegador **no dibuja la web**: lo hace un backend y manda los fotogramas.
Hay dos, y hablan el **mismo FBP/1**:

```
              Browser Client (el P4)
                      │
               protocolo FBP/1
                      │
          ┌───────────┴───────────┐
          │                       │
     Ubuntu / PC          Flex Phone (Android)
```

La diferencia entre los dos es solo **dónde** se conecta y **con qué**
credencial, y eso no lo puede saber el navegador: el teléfono anuncia su ip y
su puerto por el enlace, y su credencial se deriva de la clave del
emparejamiento.

Por eso el host resuelve el backend y el navegador solo pregunta
(`brHostResolveBackend`). Así no hay dos copias de la lógica de «qué fuente
toca», y añadir un backend en el futuro no obliga a tocar el cliente.

### Token del relay

```
token = HMAC(clave del vínculo, "flexphone-relay-v2")   → 32 hex
```

**No viaja por la red**: los dos extremos lo calculan por su cuenta. Antes se
derivaba de la dirección BLE del dispositivo emparejado, y una dirección MAC es
**pública**: cualquiera que la viera podía calcular el token y entrar al relay.

### Fuente

*Navegador → Ajustes → Fuente*, y también desde *Flex Phone → Navegador*:

| Modo | Comportamiento |
|---|---|
| **Automático** | teléfono si su servidor está arriba → nube → PC/Ubuntu |
| **Flex Phone** | solo el teléfono |
| **Nube** | solo el servidor en la nube del usuario |
| **PC/Ubuntu** | el comportamiento de siempre |

Los modos exclusivos **no se caen en secreto a otra fuente**: un «Flex Phone»
que en realidad tirara del PC sería justo la mentira que este selector evita.
Si el elegido no está disponible, se dice el motivo concreto.

**El servidor Ubuntu no se elimina ni se modifica.**

### Límites reales, sin adornos

- **No se promete que funcione indefinidamente con la pantalla apagada.**
  Android puede matar el proceso por batería, memoria o política del
  fabricante. Si lo hace, Flex OS lo muestra como **error visible**, no se
  queda esperando fotogramas.
- **No se prometen 60 FPS.** Una página renderizada en el teléfono, comprimida
  a JPEG y enviada por Wi‑Fi a un ESP32 que además tiene que decodificarla no
  da eso.
- **Sin TLS en la red local.** La interfaz dice *«Sin TLS (red local)»*.
- **Solo un Flex OS a la vez.**

---

## 9. Rendimiento

Reglas que cumple el código del firmware:

- Sin `String` de Arduino, sin `delay()`, sin recursión.
- **Ninguna** llamada de red en el hilo gráfico. El transporte vive en su
  propia tarea, y entre las dos solo pasan tramas completas por colas de tamaño
  fijo.
- Un **tope de tramas por vuelta** (`FLP_LINK_RX_PER_TICK`): si el teléfono
  manda una ráfaga, se reparte entre cuadros en vez de comerse uno entero.
- Buffers **fijos** con límites explícitos; `snprintf` y `memcpy` validado;
  ningún `strcpy` ni `sprintf`.
- Cola circular de 40 notificaciones que expulsa por *(prioridad, antigüedad)*.
- Escritura en flash **agrupada** (cada 30 s como mucho, y al salir de la app).
- **Repintado por cambio real, no por cuadro**: con Flex Phone abierto y sin
  novedades no se redibuja nada.
- El banner **reserva su banda al abrirse y la suelta al cerrarse**: retener
  memoria permanentemente por algo que puede no ocurrir en horas no se
  sostiene. Si no hay memoria, no se dibuja — la notificación sigue en el
  Centro.

**Coste con Flex Phone inactivo**: `flexPhoneTick()` y `fpbTick()` salen en su
primera línea. El escritorio, el panel rápido, los juegos y el navegador no
pagan nada por que esta app exista.

**Batería del teléfono**: el servidor no sondea. Las notificaciones son eventos
de Android, el estado se manda **cuando cambia** (con un latido de un minuto) y
el enlace en reposo intercambia un PING cada 8 s.

---

## 10. Privacidad

**Qué sale del teléfono**: nombre de la app, paquete, título, un resumen
recortado del texto, la hora, la categoría, la prioridad, las etiquetas de las
acciones y —si el usuario no lo desactiva— batería, red, almacenamiento y
memoria.

**Qué no sale**: el icono de la app, la notificación completa, contactos, SMS,
ubicación, IMEI ni número de serie.

- Con *«no enviar el cuerpo»*, el texto **ni siquiera sale del teléfono**.
- Detección conservadora de códigos de un solo uso: ante la duda, se oculta.
- De fábrica **no hay ninguna app permitida**: una app que reenvía todo por
  defecto es una fuga de privacidad.
- Borrado automático por antigüedad (48 h de fábrica).
- El diagnóstico muestra **solo contadores**: se puede enseñar para pedir ayuda
  sin revelar nada.
- Las copias de seguridad de Android están **desactivadas** para toda la app.
- Nada pasa por ningún servidor.

---

## 11. Compilar la app Android

| Pieza | Versión |
|---|---|
| Gradle | 8.14.x (incluido el *wrapper*) |
| Android Gradle Plugin | 8.5.2 |
| Kotlin | 2.0.21 |
| Compose BOM | 2024.09.03 |
| JDK | 17 o superior |
| `compileSdk` / `targetSdk` | 35 |
| `minSdk` | 26 |

```bash
cd android/FlexPhone
cp local.properties.example local.properties
$EDITOR local.properties          # sdk.dir=/ruta/a/Android/Sdk
./gradlew :app:assembleDebug
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

**No hay ninguna credencial en el repositorio.**

### Sin SDK de Android

El módulo `:protocol` es Kotlin/JVM puro y **se construye y se prueba sin SDK,
sin emulador y sin placa**:

```bash
./gradlew :protocol:test
```

---

## 12. Emparejar

1. **En el teléfono**: concede el acceso a notificaciones y pulsa *Activar el
   enlace*. Los dos tienen que estar en la **misma red Wi‑Fi**.
2. **En el teléfono**: *Emparejar*. La pantalla busca relojes durante unos
   segundos y enseña los que **han contestado**. Ver el reloj ahí ya demuestra
   que la ida y la vuelta funcionan.
3. **En Flex OS**: *Flex Phone → Emparejar teléfono*. Aparece un código de 6
   dígitos, y el reloj pasa a mostrarse como *enseñando código* en la lista del
   teléfono.
4. **En el teléfono**: teclea ese código.
5. **En Flex OS**: pulsa *Confirmar aquí*.

Después, elige en *Notificaciones* qué apps pueden enviar las suyas.

**Si el reloj no aparece en la lista**, no hace falta rendirse a la difusión:

- *Emparejar → Buscar por dirección*, con la dirección que enseña Flex OS en
  *Flex Phone → Conexión → Este reloj*. La pregunta va en unidifusión y el
  reloj aprende de ella la dirección del teléfono.
- O al revés: fija en Flex OS la dirección que enseña la app en *Emparejar →
  Dirección de este teléfono*.

### Casos de prueba manuales

Lo que hay que comprobar en hardware real. Ninguno de estos puede acabar en un
indicador de progreso que no termina.

| # | Caso | Resultado esperado |
|---|---|---|
| A | Los dos en la misma Wi‑Fi, enlace encendido en ambos | el reloj aparece en la lista en pocos segundos y conecta solo |
| B | Emparejar con el código correcto | *Emparejado*, y el vínculo sobrevive a reiniciar los dos |
| C | Emparejar con un código **equivocado** | el teléfono dice *el código no coincide* **en segundos**, no al caducar |
| D | Teclear el código cuando ya **caducó** | mensaje de caducado y vuelta a buscar, en los dos lados |
| E | Router con aislamiento de clientes | la lista sale vacía con su explicación; *Buscar por dirección* sí conecta |
| F | Apagar el Wi‑Fi del teléfono con la sesión abierta | el reloj dice *se perdió el Wi‑Fi* y reconecta solo al volver |
| G | El DHCP da otra IP al teléfono | el reloj olvida el destino y vuelve a encontrarlo sin tocar nada |
| H | Dos móviles con la app en la misma red | el reloj contesta a los dos, pero **solo abre una sesión** |
| I | Revocar el vínculo en cualquiera de los dos lados | el otro pide emparejar de nuevo; ninguna sesión queda a medias |

---

## 13. Solución de problemas

| Síntoma | Causa probable | Qué hacer |
|---|---|---|
| «Buscando Flex Phone» y no encuentra | el router aísla a los clientes, o el teléfono está en otra red | misma Wi‑Fi; si aun así no, *Emparejar → Buscar por dirección* |
| El reloj no sale en la lista del teléfono | el enlace no está encendido en Flex OS, o la red filtra | enciéndelo; luego *Buscar por dirección* con la IP que enseña el reloj |
| «Flex OS no aceptó el código» a los 12 s | código equivocado, o el reloj ya no está en emparejamiento | vuelve a pulsar *Emparejar teléfono* en el reloj |
| «Este teléfono no está en Wi‑Fi» | datos móviles | el enlace es de red local |
| No llega ninguna notificación | falta el acceso a notificaciones, o la app no está permitida | Bienvenida → permisos; luego *Notificaciones* |
| «El código tecleado no coincide» | código mal, o caducado (2 min) | vuelve a emparejar |
| «Responder» no aparece | esa notificación no trae `RemoteInput` | no es un fallo: esa app no lo permite |
| El enlace se cae al apagar la pantalla | ahorro de batería o política del fabricante | *Estado del dispositivo → Segundo plano* |
| «Android detuvo el enlace» | el sistema mató el servicio | vuelve a activarlo; si se repite, excluye la app del ahorro de batería |
| «La app del teléfono es de una versión anterior» | app v1 contra firmware v2 | actualiza la app |
| El puerto 47820 ya está en uso | otra app lo tiene | ciérrala o reinicia el teléfono |

---

## 14. Estado de verificación

Se distingue con cuidado entre las tres cosas.

### ✅ Compilado y probado (ejecutado de verdad)

| Qué | Resultado |
|---|---|
| `FlexOS_Ultra.ino` completo | compila; cableado, auto‑prototipado y presupuesto de pila verificados |
| Los tres perfiles de placa (P4, S3, Pro) | compilan y pasan |
| Protocolo Flex Link (C++) | 85 comprobaciones, ASan + UBSan |
| **Emparejamiento y sesión (C++)** | **26 comprobaciones**, contra FIPS 180‑4 y RFC 4231 |
| Modelo Flex Phone (C++) | 111 comprobaciones |
| **Máquina del enlace (C++)** | **125 comprobaciones**, con un teléfono simulado que habla el protocolo de verdad |
| **Desplazamiento de las listas (C++)** | **24 comprobaciones**, con una secuencia de cuadros como la del táctil real |
| **Bytes del descubrimiento (C++)** | **108 comprobaciones**, con paquetes cortados, mentirosos e imposibles |
| Vectores dorados (C++) | 10 vectores |
| Núcleo del navegador (C++) | 406 comprobaciones |
| Protocolo Android (Kotlin) | **55/55**, incluidos los vectores compartidos con el firmware |
| Servicio de render (Node) | 35/35 |
| SDK de apps (Node) | 10/10 |

Lo que comprueban las pruebas del enlace es sobre todo **lo que se niega a
hacer**: declararse conectado sin autenticar, emparejar con una sola parte,
emparejar con un código equivocado, aceptar una notificación sin sesión,
aceptar tramas repetidas o de otra sesión, conservar el estado de un teléfono
que ya no está, dejar la clave en memoria después de olvidar, inventarse una
latencia, y reintentar para siempre.

### ⚠️ Validado estáticamente, sin ejecutar

**El módulo `:app` de Android no se ha podido compilar en este entorno.** No es
un problema de configuración: el repositorio Maven de Google
(`dl.google.com`) no es alcanzable desde aquí, así que el Android Gradle Plugin
no se puede descargar.

```
repo1.maven.org  -> alcanzable (por eso :protocol compila y pasa sus 55 pruebas)
dl.google.com    -> CONNECT rechazado
```

En una máquina con SDK y salida a `dl.google.com`,
`./gradlew :app:assembleDebug` debería completarse: las versiones están fijadas
y son compatibles entre sí.

**Qué queda por verificar de `:app`**: que compila y que el APK se instala. El
código de protocolo que comparte con el firmware **sí** está verificado, que es
la parte que más silenciosamente se puede romper.

### ⏳ Pendiente de prueba física

Nada de esto se ha probado sobre hardware, y **no se afirma que funcione**:

1. **Enlace real P4 ↔ Android**: los **casos A–I** de §12 — descubrimiento UDP
   en los dos sentidos, respaldo por dirección, conexión TCP, emparejamiento
   con código correcto y equivocado, caducidad, reconexión tras cambio de IP,
   dos móviles a la vez, revocación, notificaciones llegando y respuestas con
   `RemoteInput` sobre una app real.
2. **El banner dentro de un juego apaisado**: que aparezca arriba, que el juego
   no se pause, que no cambie la orientación, que no se note en los FPS y que
   no deje rastro al irse.
3. **Centro de notificaciones**: el gesto del borde izquierdo contra el scroll
   de una app, y el del borde derecho contra el de una lista.
4. **Pantalla apagada** — medir a **1, 5 y 15 minutos**, con y sin exclusión del
   ahorro de batería, y anotar el resultado **por fabricante**.
5. **Consumo**: batería del teléfono con el enlace activo, y del P4 con el
   enlace en reposo.
6. **Latencia real** de una notificación (A55 → P4) sobre Wi‑Fi doméstico.
7. **Cambio de red**: router reiniciado, IP nueva, Wi‑Fi apagado y encendido.
8. **BLE por el C6** — todo lo de §2.
9. **Sin IMU**: que el sistema arranque y siga interactivo con el GY‑BNO085
   retirado, y que Flex Phone funcione igual. Flex Phone **no toca** el servicio
   del IMU ni ninguna de sus dependencias.
