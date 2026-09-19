# Diagnóstico del enlace Flex Phone

Cómo capturar, en **una sola prueba de 60 s**, quién cierra la conexión y por
qué. Y el registro de lo que ya se ha descartado, para no volver a mirarlo.

---

## Estado: el ciclo de 10–13 s

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
