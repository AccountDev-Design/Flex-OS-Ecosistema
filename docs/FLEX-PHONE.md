# Flex Phone

Ecosistema que conecta un teléfono Android con **Flex OS Ultra** (ESP32‑P4):
notificaciones reales, respuestas rápidas cuando Android las permite, control
multimedia y un navegador servido por el propio teléfono.

Este documento describe **lo que hay implementado**, **lo que está pendiente de
prueba física** y **lo que no es posible**, sin mezclar las tres cosas.

---

## 1. Arquitectura P4 / C6 / Android

```
┌─────────────────────────── ESP32-P4 ───────────────────────────┐
│  Interfaz, apps, almacenamiento, historial, render, estado     │
│                                                                │
│  App Flex Phone (id 18)      FlexOS_FlexPhone_Bridge.h         │
│         │                                                      │
│  Modelo del teléfono         FlexOS_FlexPhone.{h,cpp}          │
│  Transporte del enlace       FlexOS_FlexPhone_Link.{h,cpp}     │
│  Protocolo Flex Link         FlexOS_FlexLink.{h,cpp}           │
└───────────────┬────────────────────────────────┬───────────────┘
                │ SDIO (esp-hosted)              │ Wi-Fi (por el C6)
┌───────────────┴──────────────┐                 │
│         ESP32-C6             │                 │
│  Wi-Fi  ·  BLE (ver §2)      │                 │
└───────────────┬──────────────┘                 │
                │ BLE GATT                       │
┌───────────────┴────────────────────────────────┴───────────────┐
│                          Android                                │
│  NotificationListenerService · RemoteInput · MediaSession       │
│  Servidor GATT (periférico)  · Browser Relay (WebView + FBP/1)  │
└─────────────────────────────────────────────────────────────────┘
```

**Reparto de responsabilidades**

| Pieza | Qué hace |
|---|---|
| **ESP32‑P4** | Toda la interfaz, las apps, el almacenamiento, el historial y la lógica. No tiene radio propia. |
| **ESP32‑C6** | Wi‑Fi (ya en uso) y, si su firmware lo permite, BLE. Se comunica con el P4 por **SDIO** mediante `esp-hosted`. |
| **Android** | Permisos del sistema, lectura de notificaciones, acciones de respuesta, control multimedia y el navegador del Browser Relay. |

**BLE es solo control.** Notificaciones, estado, órdenes y negociación van por
BLE. Los fotogramas del navegador **nunca** viajan por BLE: van por Wi‑Fi.

---

## 2. El punto crítico: BLE en el ESP32‑P4

> **El ESP32‑P4 no tiene radio Bluetooth.**

No es una suposición. El SDK sólo define `SOC_BLE_SUPPORTED` en los chips con
radio Bluetooth propia, y para el P4 **no está definida**. Es exactamente la
comprobación que ya hacía `FlexOS_Ultra.ino` para deshabilitar el interruptor
de BLE en *Ajustes → Red e Internet*.

En esta placa el **único** camino posible a BLE es el co‑procesador C6, y eso
exige dos cosas que este repositorio no puede dar por hechas:

1. **Firmware `slave` de esp‑hosted en el C6 compilado con Bluetooth**, que
   exponga HCI por el mismo transporte SDIO que ya lleva el Wi‑Fi.
2. **Una pila de host BLE (NimBLE) en el P4** configurada contra ese
   controlador remoto.

### Cómo lo trata el código

`flexPhoneLinkCap()` devuelve:

| Valor | Cuándo |
|---|---|
| `FLP_LINK_CAP_LOCAL` | El chip tiene radio BLE propia (S3, C3, ESP32). |
| `FLP_LINK_CAP_HOSTED` | Hay NimBLE **y** se compiló con `-DFLEXOS_C6_BLE_HCI=1`. |
| `FLP_LINK_CAP_NONE` | Todo lo demás — **incluido el P4 tal cual está hoy**. |

Con `CAP_NONE`, la app Flex Phone muestra una tarjeta con el motivo exacto y
**no** simula un enlace, **no** inventa un teléfono conectado y **no** da por
buenas notificaciones que nadie ha enviado.

### Qué falta para completar la prueba física

Esto es lo que **no** se ha podido verificar en este entorno y hay que hacer
sobre la placa:

1. Confirmar el modelo exacto de placa y que el C6 está operativo (hoy el
   Wi‑Fi funciona por él, así que el enlace SDIO existe).
2. Flashear en el C6 un firmware `esp-hosted` **con Bluetooth habilitado**.
   ⚠️ **No sobrescribas el firmware del C6 a ciegas**: si pierdes el `slave`
   actual, pierdes también el Wi‑Fi. Haz copia antes.
3. Comprobar que el core de Arduino instalado expone una pila NimBLE que
   pueda hablar con el controlador remoto del C6.
4. Compilar con `-DFLEXOS_C6_BLE_HCI=1` y comprobar que
   `flexPhoneLinkCap()` pasa a `FLP_LINK_CAP_HOSTED`.

**Los pines SDIO no se tocan ni se inventan**: en arduino‑esp32 3.2.0 los fija
el *variant* de la placa en tiempo de compilación, y este código no los
redefine. El Wi‑Fi actual no se altera en ningún punto.

---

## 3. Protocolo Flex Link (v1)

Capa de trama versionada sobre BLE GATT. Implementaciones — **las dos cambian
a la vez**:

| Lado | Fichero |
|---|---|
| Firmware | `FlexOS_FlexLink.{h,cpp}` |
| Android | `android/FlexPhone/protocol/.../FlexLink.kt` |

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

**Límites**: trama ≤ 244 B (MTU 247 − 3 de ATT), carga ≤ 226 B, mensaje
reensamblado ≤ 2048 B, ≤ 16 fragmentos.

### Tipos de mensaje

| Rango | Contenido |
|---|---|
| `0x01–0x07` | HELLO, WELCOME, PING, PONG, BYE, ACK, ERR |
| `0x10–0x13` | PAIR_REQ, PAIR_CODE, PAIR_CONFIRM, UNPAIR |
| `0x20–0x23` | NOTIF_ADD, NOTIF_UPDATE, NOTIF_REMOVE, NOTIF_CLEAR |
| `0x30–0x33` | REPLY_REQ, REPLY_RESULT, ACTION_REQ, ACTION_RESULT |
| `0x40–0x43` | PHONE_STATE, TIME_SYNC, FIND_START, FIND_STOP |
| `0x50–0x51` | MEDIA_STATE, MEDIA_CMD |
| `0x60–0x62` | RELAY_START, RELAY_STOP, RELAY_INFO |

Los números **van al aire y nunca se reordenan**. Un tipo desconocido se
ignora en silencio: es lo que permite añadir mensajes sin romper un firmware
antiguo. Una versión **mayor** no se interpreta y se responde `E_VERSION`.

### Garantías

- CRC16‑CCITT sobre cabecera y carga.
- Fragmentación y reensamblaje con un solo parcial en vuelo (un emisor no
  puede dejar parciales colgados) y caducidad a los 5 s.
- Contador monótono con ventana de 32: descarta repetidos y lo demasiado
  antiguo, tolera el reordenado propio de BLE.
- Reintentos con espera progresiva (500 ms → 30 s) y **límite**: nada se
  reintenta para siempre.
- Todo tamaño que viene del aire se valida **antes** de usarse como índice o
  como longitud de copia.

### Vectores dorados

El firmware y la app implementan el protocolo por separado. Si alguien mueve
un campo en un solo lado no salta ningún error de compilación: el P4 empieza a
descartar tramas por CRC y se ve como *«el teléfono no conecta»*. Para
evitarlo, los bytes exactos están fijados en **las dos** baterías:

- `tests/host/test_flexlink_vectors.cpp`
- `android/.../protocol/src/test/kotlin/.../FlexLinkTest.kt`

### Seguridad

- Emparejamiento **BLE con bonding**: las características GATT se declaran con
  permisos `..._ENCRYPTED`, así que Android exige emparejar y cifra el enlace
  antes de entregar un solo byte.
- Código de 6 dígitos mostrado en Flex OS y **confirmado en los dos
  extremos**. Confirmar sólo en un lado no vincula nada. El código caduca a
  los 2 minutos.
- Sin sesión abierta sólo se aceptan mensajes del apretón de manos: una
  notificación de un dispositivo no emparejado se descarta aunque la trama sea
  válida.
- El anuncio BLE **no** lleva el nombre del teléfono, sólo el UUID del
  servicio.
- Desvincular borra el bonding (`removeBond`), los ajustes y las apps
  permitidas.
- **Nunca** se escriben textos privados, claves ni cuerpos de mensajes en los
  registros — ni en el firmware ni en la app.

---

## 4. Compilar la app Android

### Requisitos

| Pieza | Versión |
|---|---|
| Gradle | 8.14.x (incluido el *wrapper*) |
| Android Gradle Plugin | 8.5.2 |
| Kotlin | 2.0.21 |
| Compose BOM | 2024.09.03 |
| JDK | 17 o superior (el *bytecode* se emite en 17) |
| `compileSdk` / `targetSdk` | 35 |
| `minSdk` | 26 |

### Pasos

```bash
cd android/FlexPhone

# 1) Indica dónde está tu SDK
cp local.properties.example local.properties
$EDITOR local.properties          # sdk.dir=/ruta/a/Android/Sdk
#    (o exporta ANDROID_HOME)

# 2) APK de depuración
./gradlew :app:assembleDebug

# 3) Instalar
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

El APK sale en `app/build/outputs/apk/debug/app-debug.apk` y se firma con la
clave de depuración que genera Gradle. **No hay ninguna credencial en el
repositorio**; para una *release* usa tu propio *keystore* (queda fuera por
`.gitignore`).

### Sin SDK de Android

El módulo `:protocol` es Kotlin/JVM puro y **se construye y se prueba sin SDK,
sin emulador y sin placa**:

```bash
./gradlew :protocol:test
```

`settings.gradle.kts` incluye `:app` sólo si encuentra el SDK, precisamente
para que la ausencia de SDK no rompa esta verificación.

---

## 5. Conceder permisos y emparejar

### Permisos

1. **Acceso a notificaciones** — Android no permite pedirlo con un diálogo. La
   pantalla de bienvenida abre *Ajustes → Notificaciones → Acceso especial*.
   Sin esto la app no puede hacer casi nada.
2. **Bluetooth** — `BLUETOOTH_CONNECT`, `BLUETOOTH_ADVERTISE` y
   `BLUETOOTH_SCAN` en Android 12+; `BLUETOOTH`/`BLUETOOTH_ADMIN` (y
   `ACCESS_FINE_LOCATION`, que el sistema exigía) hasta Android 11. El escaneo
   se declara `neverForLocation`: **no se pide ubicación en Android 12+**.
3. **Mostrar notificaciones** (Android 13+) — sólo para la notificación del
   propio servicio, la que permite pararlo.

### Emparejar

1. En Flex OS: **Flex Phone → Centro → Emparejar teléfono**. Aparece un código
   de 6 dígitos.
2. En Android: **Flex Phone → Activar enlace**.
3. Comprueba que el código es **el mismo** en los dos. Si no coincide, no
   confirmes: puede ser otro dispositivo.
4. Confirma en ambos. Sólo entonces se abre la sesión.

Después, elige en *Aplicaciones* cuáles pueden enviar sus notificaciones. **De
fábrica no hay ninguna**: una app que reenvía todo por defecto es una fuga de
privacidad.

---

## 6. Browser Relay

Permite que el teléfono sustituya al servicio Ubuntu/PC. Habla **el mismo
FBP/1** (`server/PROTOCOL.md`), así que el navegador del P4 no necesita ni una
línea nueva y el servidor Ubuntu **sigue funcionando igual que siempre**.

### Activar

- **Desde Android**: *Browser Relay → Iniciar*.
- **Desde Flex OS**: *Flex Phone → Navegador → Iniciar* (petición autenticada
  por el enlace BLE ya emparejado).

El teléfono anuncia por BLE su **IP y puerto reales**; Flex OS comprueba
`/v1/health` y abre la sesión WebSocket. Nunca se muestra «conectado» sin
haber verificado las dos cosas.

### Excluir del ahorro de batería

Con el ahorro de batería activo, Android puede detener el relay en cuanto se
apaga la pantalla. En *Browser Relay* hay un botón que abre
*Ajustes → Batería → Optimización* para excluir Flex Phone.

Varios fabricantes (Xiaomi, Huawei, Samsung, OPPO…) añaden restricciones
propias además de las de Android. Suele hacer falta marcar la app como
«sin restricciones» o «inicio automático» en sus ajustes.

### Límites reales, sin adornos

- **No se promete que funcione indefinidamente con la pantalla apagada.**
  Android puede matar el proceso por batería, memoria o política del
  fabricante. Si lo hace, Flex OS lo muestra como **error visible**, no se
  queda esperando fotogramas.
- **No se prometen 60 FPS.** Una página remota renderizada en el teléfono,
  comprimida a JPEG y enviada por Wi‑Fi a un ESP32 que además tiene que
  decodificarla no da eso. El ritmo sube mientras se toca y baja cuando la
  página no cambia.
- **Sin TLS en la red local.** Se sirve en claro dentro de la red, igual que
  el modo de desarrollo del servidor Ubuntu. El enlace de control (BLE) sí va
  cifrado por bonding. La interfaz dice *«Sin TLS (red local)»*: no se anuncia
  como cifrado algo que no lo está.
- **Sólo un Flex OS a la vez.** Una segunda conexión se cierra en vez de
  repartir los fotogramas entre dos.

### Seguridad del relay

- El `HELLO` exige el token derivado del vínculo BLE. Quien no ha emparejado
  no entra aunque alcance el puerto.
- Esquemas `file:`, `content:` y `javascript:` **bloqueados**; acceso a
  ficheros locales desactivado en el WebView. Un teléfono no puede quedar
  expuesto a través del relay.
- Las cookies viven en el almacenamiento privado de la app y **nunca** se
  envían por el enlace ni se escriben en los registros.
- Sesiones inactivas cerradas tras un tiempo configurable, con sus *locks*.

---

## 7. Fuente del navegador en Flex OS

*Navegador → Ajustes → Fuente*:

| Modo | Comportamiento |
|---|---|
| **Automático** | Flex Phone si el relay está arriba → nube configurada → PC/Ubuntu. |
| **Flex Phone** | Sólo el teléfono. |
| **Nube** | Sólo el servidor en la nube del usuario. |
| **PC/Ubuntu manual** | El comportamiento de siempre. |

Los modos exclusivos **no** se caen en secreto a otra fuente: un «Flex Phone»
que en realidad tirara del PC sería justo la mentira que este selector evita.
Si el elegido no está disponible, se dice el motivo concreto.

Nunca se muestra conexión correcta sin verificar `/v1/health` y abrir una
sesión válida.

### Servidor en la nube

Prepara compatibilidad con el backend existente (`server/`) para instalarlo en
un VPS Linux. **No se incluyen credenciales ni se contrata ningún servicio**:
el usuario configura la URL. **El servidor Ubuntu actual no se elimina ni se
modifica.**

---

## 8. Límites reales de WhatsApp y otras apps

**Lo que Flex Phone sí hace**

- Recibe notificaciones reales de WhatsApp (y de cualquier app permitida).
- **Responde** cuando la notificación de Android expone una acción con
  `RemoteInput` — que es lo que WhatsApp ofrece para responder desde la barra.
- Devuelve a Flex OS el resultado **real**: si la acción caducó o la
  notificación desapareció, se dice, no se simula un envío.

**Lo que no hace, y por qué**

- **No inicia conversaciones nuevas.** WhatsApp personal no ofrece una API
  pública que permita a un ESP32 actuar como cliente independiente.
- **No automatiza WhatsApp Web**, no extrae cookies, tokens ni sesiones, y no
  usa APIs privadas ni ingeniería inversa. Cualquiera de esas cosas pondría en
  riesgo la cuenta del usuario.
- **No muestra un botón «Enviar» que no haga nada.** Si Android no dio
  `RemoteInput`, `canReply` va en `false` y Flex OS **no ofrece** el botón.
- En modo independiente WhatsApp aparece como **«Requiere teléfono
  conectado»**, no como una función autónoma.

**Matiz importante sobre «enviado»**: que Android acepte la acción significa
que la app de mensajería **recibió** el texto, no que el destinatario lo haya
recibido o leído. Eso sólo lo sabe la propia app. La interfaz dice «entregada
a la app».

**Sin teléfono ni módem celular**, el ESP32‑P4 no puede hacer llamadas, enviar
SMS ni ser un teléfono autónomo. La interfaz lo dice.

### Qué sí funciona sin teléfono

- Historial de notificaciones previamente sincronizadas (las 20 más recientes
  se conservan en flash).
- Borradores locales de mensajes.
- Multimedia almacenada localmente.
- Navegador mediante servidor en la nube configurado.
- Servicios propios de Flex OS que ya existen.

---

## 9. Privacidad

**Qué sale del teléfono**: nombre de la app, paquete, título (el remitente),
un resumen recortado del texto, la hora, la categoría, la prioridad y las
etiquetas de las acciones.

**Qué no sale**: el icono de la app, la notificación completa, contactos, SMS
ni ubicación.

- Con *«no enviar el cuerpo»*, el texto **ni siquiera sale del teléfono** — no
  se manda para que el P4 lo esconda, que es lo único que no se puede filtrar
  después.
- Detección conservadora de códigos de un solo uso: ante la duda, se oculta.
  Equivocarse marcando de más sólo esconde un texto; al revés enseña un OTP en
  una pantalla que puede estar sobre una mesa.
- En la pantalla de bloqueo de Flex OS se respeta la configuración: se puede
  mostrar sólo app y remitente. Lo marcado como sensible **no se muestra
  nunca**.
- Borrado automático por antigüedad (48 h de fábrica).
- El diagnóstico muestra **sólo contadores**: se puede enseñar para pedir
  ayuda sin revelar nada.
- Las copias de seguridad de Android están **desactivadas** para toda la app.
- Nada pasa por ningún servidor: el enlace es directo entre el teléfono y el
  Flex OS del usuario.

---

## 10. Rendimiento y memoria en el P4

Reglas que cumple el código nuevo del firmware:

- Sin `String` de Arduino, sin `delay()`, sin recursión.
- Sin peticiones de red en el bucle gráfico y sin esperar respuestas BLE
  bloqueando la interfaz.
- Buffers **fijos** con límites explícitos; `snprintf` y `memcpy` validado;
  **ningún** `strcpy` ni `sprintf`.
- UTF‑8 validado y truncado en frontera de carácter.
- Cola circular de 40 notificaciones que expulsa por *(prioridad, antigüedad)*:
  nunca tira una urgente teniendo algo de prioridad menor.
- Escritura en flash **agrupada** (cada 30 s como mucho, y al salir de la app),
  con un blob acotado a **11 914 B** en el peor caso: a flash sólo van las 20
  más recientes, y ninguna si el usuario apagó el historial.
- Recepción, procesado, persistencia y render separados.
- Contadores de diagnóstico (fotogramas descartados, paquetes inválidos, cola
  llena, reconexiones) visibles **sólo** en la pantalla de diagnóstico.

**Coste con Flex Phone inactivo**: `flexPhoneTick()` sale en su primera línea
si el enlace está apagado o no disponible. El escritorio, el panel rápido, los
juegos y el navegador no pagan nada por que esta app exista.

**Memoria** (medida, no estimada — `sizeof` de las estructuras reales):

| Estructura | Tamaño |
|---|---|
| `FlexPhoneNotif` × 40 | 17 440 B |
| `FlexPhoneConv` × 12 | 1 680 B |
| `FlexPhoneDraft` × 8 | 3 136 B |
| **`FlexPhoneModel`** (instancia única) | **22 568 B** |
| **`FlexPhoneLink`** (instancia única) | **6 464 B** |
| **Total en BSS** | **29 032 B (28,4 KB)** |
| Blob a flash (peor caso, buffer temporal en PSRAM) | 11 914 B |

Son dos estructuras de tamaño fijo decidido en compilación. Sin reservas
dinámicas repetidas y sin fragmentar el heap que necesitan el navegador y la
galería. El buffer de serialización se pide a PSRAM y se libera enseguida: 12
KB en la pila del bucle principal no serían aceptables.

---

## 11. Solución de problemas

| Síntoma | Causa probable | Qué hacer |
|---|---|---|
| «BLE no disponible» en Flex OS | El P4 no tiene radio y el C6 no está preparado | Ver §2. Es lo esperado hoy. |
| No llega ninguna notificación | Falta el acceso a notificaciones, o la app no está permitida | Bienvenida → permisos; luego *Aplicaciones*. |
| «Responder» no aparece | Esa notificación no trae `RemoteInput` | No es un fallo: esa app no permite responder desde la barra. |
| «La notificación ya no existe» | Desapareció mientras escribías | Vuelve a abrir la conversación en el teléfono. |
| El relay se detiene al apagar la pantalla | Ahorro de batería o política del fabricante | Excluir Flex Phone (§6). Puede no bastar en algunos teléfonos. |
| Flex OS no encuentra el relay | Teléfono y reloj en redes Wi‑Fi distintas | Deben estar en la misma red local. |
| «Dispositivo no emparejado» al conectar el navegador | El token no coincide | Vuelve a emparejar por BLE. |
| El enlace se cae y vuelve | Distancia o interferencias | Es normal; reconecta con espera progresiva y límite. |

---

## 12. Estado de verificación

Se distingue con cuidado entre las tres cosas:

### ✅ Compilado y probado (ejecutado de verdad)

| Qué | Resultado |
|---|---|
| `FlexOS_Ultra.ino` completo | Compila; auto‑prototipado y cableado verificados |
| Protocolo Flex Link (C++) | 85 comprobaciones, ASan + UBSan |
| Modelo Flex Phone (C++) | 111 comprobaciones, ASan + UBSan |
| Transporte del enlace (C++) | 89 comprobaciones, ASan + UBSan |
| Vectores dorados (C++) | 6 vectores |
| Núcleo del navegador (C++) | 406 comprobaciones (24 nuevas de fuente) |
| Iconos del sistema | 19 iconos × 2 estilos × 3 tamaños, ninguno fuera de su caja |
| Protocolo Android (Kotlin) | `:protocol:test` → 31/31 |

### ⚠️ Validado estáticamente, sin ejecutar

**La app Android (`:app`) no se ha podido compilar en este entorno.** No es un
problema de configuración del proyecto: el repositorio Maven de Google no es
alcanzable desde aquí, así que el Android Gradle Plugin no se puede descargar.

Comando intentado:

```
./gradlew :app:assembleDebug
```

Error real (resumido):

```
* What went wrong:
Plugin [id: 'com.android.application', version: '8.5.2'] was not found in any
of the following sources:
- Plugin Repositories (could not resolve plugin artifact
  'com.android.application:com.android.application.gradle.plugin:8.5.2')
  Searched in the following repositories:
    Google
    MavenRepo
```

Comprobación de red desde el propio entorno:

```
repo1.maven.org  -> 200
dl.google.com    -> CONNECT tunnel failed, response 403
```

Es decir: Maven Central **sí** se alcanza (por eso `:protocol` compila y
ejecuta sus 31 pruebas) y el Maven de Google **no**. En una máquina con SDK y
salida a `dl.google.com`, `./gradlew :app:assembleDebug` debería completarse:
las versiones de Gradle, AGP, Kotlin y Compose están fijadas y son
compatibles entre sí.

**Qué queda por verificar de `:app`**: que compila y que el APK se instala. El
código de protocolo que comparte con el firmware **sí** está verificado, que
es la parte que más silenciosamente se puede romper.

### ⏳ Pendiente de prueba física

Nada de esto se ha probado sobre hardware, y **no se afirma que funcione**:

1. **BLE por el C6** — todo lo de §2. Hasta que eso esté, el enlace informa
   `CAP_NONE` y la app lo dice en pantalla.
2. **Enlace real P4 ↔ Android**: emparejamiento, MTU negociado, notificaciones
   llegando y respuestas con `RemoteInput` sobre una app real.
3. **Browser Relay con la pantalla apagada** — medir a **1, 5 y 15 minutos**
   en el teléfono objetivo, con y sin exclusión del ahorro de batería, y
   anotar el resultado por fabricante.
4. **Consumo**: batería del teléfono con el relay activo, y del P4 con el
   enlace en reposo.
5. **Fotogramas por segundo reales** del relay sobre Wi‑Fi con páginas
   distintas.
6. **Reconexión** tras alejarse y volver, y tras apagar y encender el
   Bluetooth.
7. **Multimedia con varias sesiones** activas a la vez.
8. **«Encontrar mi teléfono»** con el modo No molestar activo (debe respetarlo
   y quedarse en vibración).
