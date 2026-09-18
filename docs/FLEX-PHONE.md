# Flex Phone

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

**Descubrimiento.** Flex OS manda `FLEXPHONE?` + versión a la difusión de su
subred; el teléfono contesta `FLEXPHONE!` + versión + puerto + nombre. Se usa
la difusión **dirigida de la subred**, no `255.255.255.255`.

Si el router aísla a los clientes entre sí, la difusión no llega. Entonces la
dirección se fija a mano (`flexPhoneWifiSetHost`) y la interfaz lo ofrece, en
vez de quedarse «buscando» para siempre sin decir por qué.

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
- una conexión que no autentica en 15 s;
- una sesión sin tráfico en 40 s;
- una trama de otra sesión, repetida, corrupta o de versión futura.

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
2. **En Flex OS**: *Flex Phone → Emparejar teléfono*. Aparece un código de 6
   dígitos.
3. **En el teléfono**: teclea ese código.
4. **En Flex OS**: pulsa *Confirmar aquí*.

Después, elige en *Notificaciones* qué apps pueden enviar las suyas.

---

## 13. Solución de problemas

| Síntoma | Causa probable | Qué hacer |
|---|---|---|
| «Buscando Flex Phone» y no encuentra | el router aísla a los clientes, o el teléfono está en otra red | misma Wi‑Fi; si aun así no, fija la dirección a mano |
| «Este teléfono no está en Wi‑Fi» | datos móviles | el enlace es de red local |
| No llega ninguna notificación | falta el acceso a notificaciones, o la app no está permitida | Bienvenida → permisos; luego *Notificaciones* |
| «El código tecleado no coincide» | código mal, o caducado (2 min) | vuelve a emparejar |
| «Responder» no aparece | esa notificación no trae `RemoteInput` | no es un fallo: esa app no lo permite |
| El enlace se cae al apagar la pantalla | ahorro de batería o política del fabricante | *Estado del dispositivo → Segundo plano* |
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
| **Máquina del enlace (C++)** | **114 comprobaciones**, con un teléfono simulado que habla el protocolo de verdad |
| Vectores dorados (C++) | 10 vectores |
| Núcleo del navegador (C++) | 406 comprobaciones |
| Protocolo Android (Kotlin) | **42/42**, incluidos los vectores compartidos con el firmware |
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
repo1.maven.org  -> alcanzable (por eso :protocol compila y pasa sus 42 pruebas)
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

1. **Enlace real P4 ↔ Android**: descubrimiento UDP, conexión TCP,
   emparejamiento con código, reconexión, notificaciones llegando y respuestas
   con `RemoteInput` sobre una app real.
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
