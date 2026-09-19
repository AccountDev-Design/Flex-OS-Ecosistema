# Diagnóstico del enlace Flex Phone

Cómo capturar, en **una sola prueba de 60 s**, quién cierra la conexión y por
qué. Y el registro de lo que ya se ha descartado, para no volver a mirarlo.

---

## Estado: el ciclo de 5–7 s (posterior al fix de escritura)

Con `fpwWriteAll()` y `cli.connect(host, port, FLPW_CONNECT_MS)` ya en el
firmware flasheado (P4) y el APK actualizado instalado, el patrón **cambió**:
ya no son 10–13 s, son ~5–7 s, con un repunte corto de ~1 s de "conectado"
entre caídas. Que el patrón haya cambiado de forma en vez de repetirse
idéntico es en sí mismo la prueba de que el binario nuevo SÍ está corriendo
en el reloj — un binario viejo habría reproducido el mismo ciclo de 10–13 s.
Confírmalo además mirando el registro serie: solo el código nuevo imprime
`(s) CONEXION #n ABIERTA/CERRADA ... motivo=...`; si esas líneas aparecen,
no hay duda posible.

### Por qué NO puede ser el mismo mecanismo que el de 10–13 s

El latido (`FLP_LINK_IDLE_PING_MS`) sale cada **8 s** de sesión inactiva. El
`write()` que se arregló solo se ejecuta cuando hay algo que enviar — en la
práctica, ese latido. Si el ciclo nuevo se repite cada 5–7 s, la caída está
ocurriendo **antes de que exista una primera oportunidad de escribir el
latido**: es una caída de apertura/autenticación, no de una escritura en
sesión ya abierta. El mecanismo que se corrigió (una escritura corta dando
un socket vivo por muerto) no puede ser, por aritmética, la causa de un
ciclo más corto que su propio disparador.

### Lado de LECTURA (`fpwPumpRead` / `cli.available()` + `cli.read()`) — revisado, sin el mismo defecto

`fpwPumpRead()` solo llama a `cli.read()` cuando `cli.available() > 0` ya
confirmó que hay bytes, y corta el bucle en el primer `read() <= 0`. No hay
ningún bucle propio de reintento con plazo — al contrario que el `write()`
de antes, aquí no existe una comparación que pueda confundir "ocupado" con
"muerto". Se contrastó además contra el código real de
`NetworkClient::read()`/`available()` de arduino-esp32 (rama `master`):
`available()` usa `ioctl(FIONREAD)`, no bloqueante, y el relleno del buffer
de lectura no repite el mismo bucle de `select()` de 10 intentos que tenía
`write()`. Es decir: el hallazgo de la pista 2 (defecto simétrico en
lectura) **no se confirma en el código que hay hoy en este repositorio**.
Dicho esto, no se pudo verificar con la misma certeza absoluta que el
defecto de escritura (ese se citó contra el fuente exacto del core con los
nombres de las constantes; aquí la verificación fue contra el código de
`master`, no necesariamente la versión exacta que compilas). Si el log
muestra un cierre con motivo `"flujo ilegible"` (el único que señala este
camino), vuelve aquí.

### Transporte P4↔C6 (pista 1) — no descartable sin el log, no tocado

El Wi-Fi del P4 va por `esp_wifi_remote`/SDIO hacia el C6 (esp-hosted),
integrado de forma nativa en arduino-esp32 3.2.1 vía `WiFi.setPins()` — no
es un mecanismo casero de este repositorio. No hay forma de descartar o
confirmar un ciclo propio de ese transporte (reset, resincronización) sin
mirar el log: si el reloj imprime `motivo=el telefono cerro el socket` pero
el teléfono **no** tiene un `CLOSE_REQUEST` a esa misma hora, la caída viene
de más abajo que los dos extremos de la aplicación — de la radio o del
transporte hosted, no de este código. No se ha tocado nada de esa capa.

### Modem-sleep (pista 3) — hueco confirmado en el código, cambio aplicado

No había ninguna llamada a `WiFi.setSleep(false)` (ni a `esp_wifi_set_ps`)
en ningún camino real de conexión — solo el stub de pruebas la declaraba
como no-op. El ahorro de energía de Wi-Fi está encendido por defecto en
arduino-esp32, se reenvía al C6 por `esp_wifi_remote` igual que el resto de
la API, y es la causa clásica de "se ve conectado pero deja de responder"
en sockets TCP persistentes sobre ESP32. Es una sospecha razonable con
evidencia (la ausencia está confirmada en el código; el efecto sobre ESTE
síntoma concreto no), así que se aplicó — no es tocar ningún plazo del
enlace, es una sola llamada tras confirmar la asociación STA, en los dos
sitios donde `gNetOnline` pasa a `true` (`wifiConnTask` y
`wifiAutoConnTask`, en `FlexOS_Ultra_Network.h`). Queda pendiente de
confirmar con el log: si el ciclo de 5–7 s desaparece o cambia de forma
tras esto, era esto. Ten en cuenta el coste: es un reloj de batería, y
`setSleep(false)` sube el consumo de la radio mientras el enlace esté
encendido.

### Build de dos firmwares (pista 4) — no aplica a este fix

Este cambio (y el de `fpwWriteAll`) vive entero en el sketch del P4
(`FlexOS_Ultra.ino` y sus módulos). El C6 corre el firmware "slave" de
esp-hosted, un binario aparte que no se toca para nada de esto: no hay dos
firmwares que sincronizar para ESTE fix en concreto, solo el del P4 (más el
APK de Android, que sí cambió en el mismo commit).

---

## Estado: el ciclo de 10–13 s (RESUELTO)

### CAUSA (candidata, con el mecanismo probado)

`FlexOS_FlexPhone_WiFi.h` daba por **muerto** un socket que solo estaba
**ocupado**:

```c
if(cli.write(frame, n) != n){ fpwSetStatus("se corto al enviar"); goto closed; }
```

`NetworkClient::write()` de arduino‑esp32 **no promete escribirlo todo**. Su
bucle interno hace `select()` con un plazo de 1 s y se rinde tras 10 intentos:

```c
// libraries/Network/src/NetworkClient.cpp  (core 3.1.x)
#define WIFI_CLIENT_MAX_WRITE_RETRY     (10)
#define WIFI_CLIENT_SELECT_TIMEOUT_US   (1000000)
...
while (retry) {
  tv.tv_usec = WIFI_CLIENT_SELECT_TIMEOUT_US;   // 1 s
  retry--;
  if (select(fd + 1, NULL, &set, NULL, &tv) < 0) return 0;   // ← 0 bytes, socket vivo
  ...
}
return totalBytesSent;                                        // ← puede ser < size
```

Es decir: **`write()` puede retener la tarea de red hasta ~10 s y devolver
después una escritura corta, con la conexión perfectamente viva.**

### INTERVALO — por qué 10–13 s y no otro número

| Tramo | Duración |
|---|---|
| El reloj late cada `FLP_LINK_IDLE_PING_MS` | **8 s** |
| `write()` del latido se queda dentro | hasta **10 s** (10 × `select()` de 1 s) |
| Mientras está dentro, `fpwCtx.state` sigue en `FLP_TC_OPEN` | los dos extremos pintan **CONECTADO** |
| Vuelve corta → `goto closed` → cierre | **DESCONECTADO** |
| Reconexión (`flexLinkRetryDelayMs(0)`) | **~1 s** |

Los 10 s son una **constante del core**, no una estimación. Y como la conexión
«dura» más de `FLPW_RECONNECT_OK_MS` (10 s), la espera progresiva se reinicia
cada vuelta: por eso la reconexión se mantiene en ~1 s en lugar de crecer.

### CALLSITE

`fpwTask()` → bucle de bombeo → `cli.write(frame, n) != n` → `goto closed`,
en `FlexOS_FlexPhone_WiFi.h`.

### CORRECCIÓN

`fpwWriteAll()`: una escritura corta se **termina**, no se abandona —dejar
media trama en el flujo descoloca al otro extremo, que es un fallo peor— y el
socket solo se da por caído cuando `connected()` es falso o cuando se agota un
plazo **propio y explícito** (`FLPW_WRITE_DEADLINE_MS`).

> **Esto no es subir un timeout.** El plazo nuevo sustituye a una comparación
> que daba por muerto lo que no lo estaba. Si el socket muere de verdad, se
> cierra igual de rápido que antes.

### Segundo defecto, probado y corregido

```c
cli.setTimeout(FLPW_CONNECT_MS / 1000 ? FLPW_CONNECT_MS / 1000 : 1);
```

Pretendía fijar 4 s de plazo de conexión. **No hacía nada al socket:**
`setTimeout()` es el plazo de `Stream` (para `readBytes` y compañía) y no toca
`NetworkClient::_timeout`, así que el `connect()` usaba el valor por defecto
(`WIFI_CLIENT_DEF_CONN_TIMEOUT_MS`, 3000 ms) y no los 4000 que se creían
puestos. Ahora se usa la sobrecarga que la API declara **en milisegundos**:
`cli.connect(host, port, FLPW_CONNECT_MS)`.

---

## Lo que YA está descartado (con pruebas que pasan)

No hace falta volver a mirar aquí:

| Sospechoso | Cómo se descartó |
|---|---|
| La máquina de estados del enlace del P4 | `testIdleSessionHolds`: **90 s** de sesión en reposo con solo el latido, sin salir de `READY` y sin contar una sola reconexión |
| `WifiLinkServer` de Android | `testSesionEnReposo`: **75 s** con el latido **real** de 8 s, sin un solo cierre |
| Enmarcado / desincronización | Los dos extremos leen `len` en el mismo desplazamiento (10‑11, LSB primero); los vectores dorados lo fijan |
| Unidades de `setTimeout` | Comprobado contra el código del core 3.1.x: no afecta al socket (era un defecto, pero **no** la causa) |
| El servicio de Android reiniciándose | Todas las llamadas a `start`/`stop` salen de un botón; ninguna es periódica |
| Sesiones duplicadas / carreras de cierre | Corregido y fijado antes (`tests/link/`), y el patrón cambió de 1 Hz a 10–13 s: otra causa |

---

## Cómo capturar la secuencia (prueba de 60 s)

### 1 · Encender los registros

Ya están encendidos en esta rama:

| Interruptor | Dónde | Valor |
|---|---|---|
| `FLEXOS_DIAG_FLEXPHONE` | `FlexOS_FlexPhone_WiFi.h` | **1** |
| `DEBUG_LINK` | `WifiLinkServer.kt` | **true** |

> **Los dos vuelven a 0/false antes de publicar.** En producción no se dejan
> líneas del enlace en el registro.

### 2 · Aislar el PRIMER cierre

Mientras el reloj reconecta sin parar, **cada ciclo tapa al anterior**. Para
ver el primero, compila el firmware con:

```
-DFLEXOS_FLEXPHONE_NO_RECONNECT=1
```

Con eso el reloj se para tras la primera caída, con el motivo ya impreso, en
vez de empezar otro ciclo. Se quita después.

### 3 · Correr la prueba

1. Emparejar y conectar.
2. **No tocar nada** durante 60 s.
3. Guardar los dos registros.

**Reloj** (puerto serie, 115200):

```
[FLEXPHONE   12345] (s) CONEXION #3 ABIERTA con 192.168.1.40:47820
[FLEXPHONE   22881] (w) escritura sin avanzar en 8000 ms: se da por caida
[FLEXPHONE   22883] (s) CONEXION #3 CERRADA tras 10538 ms  motivo=escritura fallida
```

**Teléfono** (`adb logcat -s FlexPhone/WifiLink FlexPhone/LinkSvc`):

```
CLIENT_ACCEPTED #3 192.168.1.45
SESSION_CONNECTED #3 sesion=17025
PING_RECEIVED #3 -> PONG_SENT
CLOSE_REQUEST conn=#3 reason=... thread=flex-link-conn-3 authenticated=true owner=true aliveConns=1 upMs=10538
CLIENT_DISCONNECTED conn=#3 reason=... upMs=10538
```

### 4 · Leerlos

Lo único que hay que mirar es **quién imprime su cierre primero**:

| Si primero sale… | Entonces cierra… |
|---|---|
| `(s) CONEXION #n CERRADA motivo=escritura fallida` | el **reloj**, y es el camino de escritura |
| `(s) CONEXION #n CERRADA motivo=el telefono cerro el socket` | el **teléfono** — busca su `CLOSE_REQUEST` a esa misma hora |
| `CLOSE_REQUEST … reason=sin respuesta en 40 s` | el **teléfono**, por inactividad: no están llegando los latidos |
| `SESSION_HANDOVER conn=#a -> conn=#b` | el reloj abrió **dos** sockets y el segundo desalojó al primero |
| `SERVICE_DESTROYED` justo antes | **Android**, matando el servicio: el problema es de ciclo de vida, no del socket |

Y los números que hacen falta para el informe:

- **Δ** = `upMs` del cierre (cuánto duró la conexión).
- **Δreconnect** = diferencia entre `CERRADA` y la siguiente `ABIERTA`.

---

## Dónde está cada temporizador

| Plazo | Valor | Dónde | Qué hace al vencer |
|---|---|---|---|
| `FLP_LINK_IDLE_PING_MS` | 8 s | reloj | manda un latido |
| `FLP_LINK_ACK_TIMEOUT_MS` | 3 s | reloj | abandona la medida de latencia (**no** cierra) |
| `FLP_LINK_AUTH_TIMEOUT_MS` | 10 s | reloj | abandona un apretón de manos a medias |
| `FLP_LINK_DEAD_MS` | 30 s | reloj | sin recibir nada **en sesión**: se da por caído |
| `FLPW_WRITE_DEADLINE_MS` | 8 s | reloj | una trama que no avanza **ni un byte** |
| `FLPW_CONNECT_MS` | 4 s | reloj | abrir el socket |
| `FLPW_RECONNECT_MAX_MS` | 15 s | reloj | tope de la espera progresiva |
| `FLPW_RECONNECT_OK_MS` | 10 s | reloj | aguantar esto reinicia la espera |
| `AUTH_TIMEOUT_MS` | 15 s | teléfono | quien abre el puerto y **no se presenta** |
| `IDLE_TIMEOUT_MS` | 40 s | teléfono | sin recibir nada (**no** corre emparejando) |
| `POLL_MS` | 2 s | teléfono | cada cuánto despierta a mirar los plazos |
| `PAIR_CONFIRM_TIMEOUT_MS` | 12 s | teléfono | código enviado sin respuesta |
| `WIFI_CLIENT_MAX_WRITE_RETRY × SELECT_TIMEOUT` | **10 s** | **el core**, no este repositorio | `write()` devuelve corto |

Esa última fila es la que importaba: no está en este código y por eso no
aparecía al buscar temporizadores aquí.

---

## Las baterías que cubren esto

```
tests/host/   make            el enlace del P4 (tiempo simulado)
tests/link/   ./run.sh        el WifiLinkServer REAL contra sockets TCP
```

Ninguna de las dos cubre `fpwTask`: necesita la pila de red del ESP32. Por eso
el fallo sobrevivió a las dos — y por eso la instrumentación de arriba es la
que cierra el caso.
