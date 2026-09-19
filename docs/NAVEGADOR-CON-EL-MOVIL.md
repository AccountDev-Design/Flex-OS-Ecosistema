# Navegar en Flex OS usando tu móvil Android como servidor

Guía para principiantes. **No hace falta saber** qué es una IP, un puerto, un
WebSocket ni un servidor. Si algo de eso hiciera falta, aquí se dice
exactamente dónde mirarlo.

> Todo lo que hay aquí está sacado del código que corre de verdad
> (`BrowserRelayService.kt`, `RelayServer.kt`, `FlexOS_FlexPhone_Bridge.h`,
> `FlexOS_Browser.cpp`). No hay ningún paso inventado.

---

## Qué es esto, en una frase

Flex OS **no dibuja las páginas web por su cuenta**. Se las pide a otro
aparato, que las dibuja y le manda las imágenes ya hechas. Ese otro aparato
puede ser tu móvil.

```
   Flex OS Ultra  (el reloj)
        │   Wi-Fi de tu casa
        ▼
   Flex Phone  (la app de tu móvil)
        │
        ▼
   Browser Relay  (el servidor que lleva la app dentro)
        │
        ▼
   El motor de navegación del propio Android
        │
        ▼
      Internet
```

El reloj enseña la página y recibe tus toques; **el móvil hace el trabajo**.

---

## Paso 1 · Lo que necesitas

| | |
|---|---|
| ✅ | Un **Flex OS Ultra** (el reloj, placa P4) |
| ✅ | Un **móvil Android** con la app **Flex Phone** instalada |
| ✅ | Que los **dos estén en la misma red Wi‑Fi** (la de tu casa) |
| ✅ | Que el reloj y el móvil **ya estén emparejados** |

> **Lo del emparejamiento no es opcional.** El servidor del móvil usa la clave
> del vínculo para saber que quien llama es *tu* reloj y no el portátil del
> vecino. Sin emparejar, el servidor **ni siquiera arranca**: se queda en
> `Error`. Si aún no lo has hecho, empareja primero (el reloj enseña seis
> dígitos y se teclean en el móvil).

---

## Paso 2 · Abrir Flex Phone en el móvil

Abre la aplicación **Flex Phone** en el móvil.

En la pantalla de inicio verás una fila que pone **«Servidor del navegador»**
con su estado al lado. Recién empezado pondrá **Parado**.

---

## Paso 3 · Encender el servidor

Toca esa fila. Se abre una pantalla titulada **«Browser Relay»**.

Arriba hay un recuadro **«Estado»**. Debajo, un botón:

- **`Iniciar`** → enciende el servidor.
- **`Detener`** → lo apaga.

Toca **`Iniciar`**.

> También puedes encenderlo **desde el reloj**, sin tocar el móvil: ve a
> *Flex Phone → Servidor* y pulsa **«Iniciar el servidor»**. El reloj se lo
> pide al móvil por el enlace. Ese botón solo aparece si el móvil está
> conectado y ha dicho que puede hacerlo.

---

## Paso 4 · Saber que está funcionando

En la misma pantalla del móvil, la línea **«Relay»** tiene que poner:

| Lo que pone | Qué significa |
|---|---|
| **Activo** | ✅ funcionando, listo |
| Iniciando | está arrancando; espera un par de segundos |
| Detenido | está apagado — vuelve al paso 3 |
| Error | no pudo arrancar (mira el paso 10) |
| Suspendido por Android | el sistema lo frenó por batería (paso 10) |

*(En la fila del inicio los mismos estados se escriben un poco distinto:
«Parado», «Arrancando», «Con error».)*

Cuando pone **Activo** aparecen además:

- **Dirección** → algo como `192.168.1.40:45671`
- **Cifrado** → `Sin TLS (red local)`

También te saldrá una **notificación permanente** en el móvil que pone
*«Esperando a Flex OS en 192.168.1.40:45671»*. Mientras esa notificación esté
ahí, el servidor está vivo.

> **No apuntes esa dirección.** No vas a necesitarla, y además **cambia**: de
> fábrica el puerto lo elige Android cada vez que enciendes el servidor. Se
> enseña solo para que puedas comprobar que la IP es la de tu red.

> **Sobre «Sin TLS»:** dentro de tu red de casa los datos viajan sin cifrar.
> La app lo dice tal cual en vez de llamarlo «seguro». No uses esto en una
> red Wi‑Fi pública.

---

## Paso 5 · Ir al reloj

En el **Flex OS Ultra**, abre la aplicación **Flex Phone**.

Comprueba en la parte de arriba que el móvil aparece como **conectado**. Si
pone «buscando» o «conectando», espera unos segundos.

---

## Paso 6 · Elegir de dónde salen las páginas

Dentro de Flex Phone, en la lista de secciones, entra en **«Navegador»**.

Verás un apartado **FUENTE** con estas opciones:

| Opción | Qué hace |
|---|---|
| **Automático** | usa el móvil si su servidor está encendido; si no, el que tengas configurado |
| **Flex Phone** | usa **solo** el móvil |
| **PC/Ubuntu manual** | usa un ordenador que hayas configurado aparte |

Toca **«Flex Phone»** (o deja **«Automático»**, que también vale).

Abajo, en **«Fuente activa»**, tiene que poner **`Flex Phone`**.

> Si la opción «Flex Phone» aparece **apagada y no se deja tocar**, es que el
> servidor del móvil no está encendido. Vuelve al paso 3.
>
> «Flex Phone» es un modo **exclusivo**: si lo eliges y el servidor se apaga,
> el reloj **no se cambia en secreto** al ordenador. Te lo dice y se queda
> parado, para que sepas lo que está pasando de verdad.

---

## Paso 7 · Abrir el navegador

Vuelve al escritorio de Flex OS y abre la aplicación **«Navegador»**.

Escribe una dirección y listo.

---

## Paso 8 · ¿Hay que meter la IP y el puerto?

**No. Para el móvil, no.**

El móvil le dice al reloj su dirección y su puerto **él solo**, por el mismo
enlace del emparejamiento, cada vez que enciende el servidor. El reloj usa
**la que el móvil acaba de anunciar**, así que aunque el router le cambie la
IP mañana, sigue funcionando sin que toques nada.

La contraseña tampoco se teclea ni viaja: los dos extremos la calculan por su
cuenta a partir de la clave del emparejamiento.

**Escribir una dirección a mano solo sirve para la otra ruta**, la de un
ordenador (`PC/Ubuntu manual`), y se hace en los **Ajustes del navegador**,
no aquí. Si alguna vez ves el mensaje *«Sin servidor configurado
(flex://settings)»*, es que elegiste esa ruta y no has puesto ningún
ordenador — no tiene nada que ver con el móvil.

---

## Paso 9 · Comprobar que funciona

1. En el reloj, el navegador carga una página de verdad.
2. En el móvil, la notificación cambia a **«Sirviendo el navegador a Flex OS»**.
3. En el reloj, *Flex Phone → Navegador* pone **Fuente activa: `Flex Phone`**.

Si las tres cosas se cumplen, está funcionando.

---

## Paso 10 · Si algo va mal

Busca el mensaje que te sale y haz lo que dice esta tabla.

### «El servidor del teléfono no está activo»

El relay está apagado. Enciéndelo (paso 3) y mira que ponga **Activo**.

### «Empareja el teléfono para usar su navegador»

No hay vínculo entre los dos aparatos, o se borró. Empareja otra vez.
Sin vínculo no hay contraseña, y sin contraseña el servidor no deja entrar a
nadie.

### El relay pone «Error» nada más darle a Iniciar

Casi siempre es una de dos:

- **No estáis emparejados.** Mira arriba.
- **El puerto está ocupado** por otra app del móvil. En *Browser Relay →
  Ajustes del relay*, el **Puerto** puesto en `automatico` deja que Android
  elija uno libre. Si tienes un número fijo puesto, cámbialo a automático.

### «Suspendido por Android»

Android frenó la app para ahorrar batería. En la pantalla **Browser Relay**
aparece un aviso **«Ahorro de batería activo»** con un botón **«Abrir ajustes
de batería»**: úsalo para que el sistema deje de restringir Flex Phone.

### El relay se apaga solo al rato

Es a propósito: si nadie lo usa durante un tiempo, se cierra para no gastar
batería. Ese tiempo se cambia en *Browser Relay → Ajustes del relay →
**Cerrar tras inactividad*** (de fábrica, **10 minutos**).

### «Browser Relay desconectado» / el reloj deja de recibir páginas

Repasa, en este orden:

1. ¿El móvil y el reloj siguen en **la misma Wi‑Fi**?
2. ¿La notificación del relay sigue en el móvil? Si desapareció, Android
   mató el servicio: vuelve a darle a **Iniciar**.
3. ¿El router **aísla a los clientes** entre sí? Algunos routers traen una
   opción («aislamiento de clientes», «AP isolation») que impide que dos
   aparatos de la misma Wi‑Fi se hablen. Hay que apagarla.
4. La pantalla **Servidor** del reloj enseña el error que informó el móvil,
   si lo hubo. Ahí suele estar el motivo exacto.

---

## Lo que Android puede hacerte, y conviene saber

Esto no es pesimismo: es cómo funciona Android, y la app prefiere decirlo a
prometer algo que no puede cumplir.

| Situación | Qué pasa de verdad |
|---|---|
| **Pantalla apagada** | suele seguir funcionando: el relay mantiene la CPU y el Wi‑Fi despiertos mientras hay sesión |
| **Batería baja / ahorro de energía** | Android puede frenar o matar el servicio. Sale como `Suspendido` o `Error`, no como silencio |
| **Fabricante** (Xiaomi, Samsung, Huawei…) | algunos cierran servicios en segundo plano de forma agresiva. Quita a Flex Phone de la «optimización de batería» |
| **Cambio de Wi‑Fi** o salir de casa | se corta. Al volver hay que encender el relay otra vez |
| **Poca memoria** | Android puede cerrar la app para dar sitio a otra |
| **Nadie lo usa** | se cierra solo pasado el tiempo de inactividad (a propósito) |

Y una cosa más: **esto gasta batería del móvil**. Está dibujando páginas web y
mandándolas por Wi‑Fi. Para ratos largos, ten el móvil cargando.

---

## Cómo encaja todo por dentro

Para quien quiera saber qué ocurre debajo:

```
Flex OS Ultra (P4)
  │
  │  1. Enlace Flex Link por Wi-Fi (TCP 47820)
  │     · empareja, autentica y mantiene la sesión
  │     · por aquí el móvil ANUNCIA la IP y el puerto de su relay
  ▼
Flex Phone (Android)
  │
  │  2. BrowserRelayService levanta RelayServer
  │     · servicio en primer plano, con WakeLock parcial y WifiLock
  │     · contraseña = HMAC(clave del vínculo, "flexphone-relay-v2")
  │       — se calcula en los dos lados, NO viaja
  ▼
RelayServer  (HTTP + WebSocket)
  │     · /v1/health    → ¿está vivo?
  │     · /v1/version   → qué protocolo habla
  │     · /v1/session   → WebSocket, subprotocolo "fbp.v1"
  ▼
RelayEngine → motor de navegación de Android (WebView)
  │     · dibuja la página y la manda como JPEG
  ▼
Internet
```

El reloj construye la dirección él solo:
`ws://<ip que anunció el móvil>:<puerto>/v1/session` — eso está en
`brHostResolveBackend()`, en `FlexOS_FlexPhone_Bridge.h`.

Los dos extremos hablan **FBP/1**, el mismo protocolo que usa el servidor de
Ubuntu (`server/`). Por eso el navegador de Flex OS **no cambia** según de
dónde vengan las páginas: solo cambia a quién se las pide.

---

## Ver también

- [`docs/FLEX-PHONE.md`](FLEX-PHONE.md) — el enlace, el emparejamiento y sus límites reales
- [`docs/NAVEGADOR.md`](NAVEGADOR.md) — el navegador y el protocolo FBP/1
- [`docs/DECISIONES-NAVEGADOR.md`](DECISIONES-NAVEGADOR.md) — por qué Flex OS no dibuja la web por su cuenta
