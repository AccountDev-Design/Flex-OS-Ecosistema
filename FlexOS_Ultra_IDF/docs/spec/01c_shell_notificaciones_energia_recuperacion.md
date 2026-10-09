# 01c · Shell: notificaciones, energía, recuperación, sesiones, idiomas, preferencias y memoria

> **Continuación de `01b_shell_paneles_navegacion_teclado.md`.** 01b documenta §0-§5 (convenciones, núcleo y estados
> `ST_*`, táctil base, barra de estado, navegación y Panel rápido completo, **incluido su modo edición y el catálogo
> "Añadir un control"** en 01b §5.9-§5.11). Este documento sigue **la misma numeración** que 01b ya cita
> (§6 notificaciones, §9 suspensión, §10 apagado, §11 Modo seguro, §12 kiosco, §13 restablecimiento, §14 sesiones,
> §16 preferencias, §17 memoria) para que las referencias cruzadas de 01b sean válidas. §7-§8 (teclado y Ajustes del
> teclado) los documenta otro ingeniero en un documento aparte: aquí **no** se describen.
>
> Especificación funcional para reconstruir en **ESP-IDF 5.5 + FreeRTOS + LVGL 9.6** (480×800 vertical) sin leer el
> código Arduino. El motor de dibujo propio no se reutiliza: se describe *qué se ve* y *cómo se comporta*, con las
> medidas exactas del código.

**Fuentes leídas por completo:** `FlexOS_Ultra.ino`, `FlexOS_Ultra_Core.h`, `FlexOS_Ultra_Notif.h`,
`FlexOS_Ultra_QuickPanelEdit.h`, `FlexOS_Ultra_System.h`, `FlexOS_Ultra_Recovery.h`, `FlexOS_Ultra_Power.h`,
`FlexOS_Ultra_Session.h`, `FlexOS_Ultra_Prefs.h`, `FlexOS_Ultra_Text.h`, `FlexOS_Ultra_Touch.h`,
`FlexOS_Ultra_Lock.h` (auto-bloqueo y kiosco), `FlexOS_Mem.h`, `FlexOS_Mem.cpp`. Además, porque el comportamiento de esta
área vive allí: `FlexOS_FlexPhone_Overlay.h` (banner, Centro de notificaciones, No molestar — **es el presentador real
de todos los avisos**), `FlexOS_FlexPhone_UI.h`/`_UI_Scroll.h` (componentes `fg*` del Centro), `FlexOS_Ultra_Types.h`
(constantes de suspensión, apagado e isla), `FlexOS_Ultra_HAL.h` (backlight y DCS del panel), `FlexOS_Ultra_Gfx.h`
(`flxFlush`: estampado de banner/candado), `FlexOS_Ultra_Media.h` (`sysNotify`/`sysSay`/`mediaNotify`),
`FlexOS_Ultra_AppFramework.h:1325-1438` (motor de sesiones), las `saveSess/loadSess` de cada app, `FlexOS_FS.cpp`
(`flexFsWriteBinAtomic`, `flexFsFactoryErase`), `FlexOS_Ultra_AppSettings.h` (filas que abren esta área),
`FlexOS_Ultra_AppStorage.h`/`DeviceTests.h` (entradas a Optimizar) y `FlexOS_Ultra_Theme.h` (modo visual eficiente).

**Documentos hermanos:** 01a (arranque, bloqueo, clave, kiosco: pantalla de zona excluida, escritorio, táctil) se cita
como **[01a §x]**; 01b como **[01b §x]**; el material Liquid Glass, tipografía y componentes como **[02 §x]**.
Rutas `archivo:línea` relativas a `FlexOS_Ultra/`.

**Convenciones:** las de [01b §0] (coordenadas físicas 480×800; `y` de texto = tope de mayúsculas; "tamaño N" de fuente:
1 = mapa de bits 5×7, N≥2 = Outfit con caja de línea 8·N px; `mix565(a,b,t)` con t 0..255 = `opa` de LVGL; tokens
`TH_*` con sus hex en [01b §0.3]). Colores literales `rgb565(r,g,b)` se dan como `#RRGGBB`. Los textos se copian
**tal cual** (si el código no lleva tilde se marca *(sic)*).

---

## 6. Notificaciones: modelo, isla (legado), banner flotante, Centro de notificaciones y No molestar

### 6.0 Arquitectura en una frase

Hay **un modelo** de avisos del sistema (`gNotifs[]`, historial de 3) y **un único presentador** en pantalla: el
**banner flotante** (`fpb*`, `FlexOS_FlexPhone_Overlay.h`). La "isla dinámica" (`FlexOS_Ultra_Notif.h`) **ya no pinta
nada** (código inerte, §6.2). El **Centro de notificaciones** (`fpc*`) es una pantalla completa que entra por el borde
izquierdo y junta los avisos del sistema con los del teléfono vinculado (Flex Phone). **No molestar** es un estado real
persistido que silencia **solo los avisos del teléfono**.

```
sysNotify/mediaNotify ──► notifPush ──► gNotifs[3] (modelo: Centro, widget de bloqueo, campana de DeX)
                                   └──► fpbPushSystemKeyed ─┐
sysSay (dentro de una app) ──► fpbPushSystem ───────────────┼─► cola del banner (6, por prioridad) ─► banner
Flex Phone (modelo fphModel) ─► phoneNotifyBridge ─► fpbPush┘        (solo si phoneCanInterrupt)
```

### 6.1 Modelo de avisos del sistema (`Types.h:370-446`, `Notif.h:138-199`, `Media.h:55-98`)

**Tipos** (`ModuleType`, `Types.h:382-385`): `MOD_UNKNOWN` = aviso del sistema (icono de la app Ajustes),
`MOD_MEDIA` = aviso del reproductor/explorador (icono de Multimedia) (`drawModuleIcon`, `Conn.h:343-351`). El barrido
genérico del bus I2C que antes generaba avisos de hardware **se retiró** (`System.h:32-54`): no hay más tipos.

**Contenido** (`DetectedModule`, `Types.h:388-395`): `name[72]` (título), `sub[40]` (texto secundario), `i2cAddr`
(siempre 0), `active`, `detectedAt`. **Entrada del modelo** (`Notification`, `Types.h:429-437`): `mod`, `active`,
`phase` (`NP_IN, NP_IDLE, NP_DRAG, NP_OUT, NP_SPRING`), `bornMs` (orden del Centro), `slideX`, `armed` (siempre
`false` ahora), `key` (huella).

**Productores** (únicas puertas):

| Función | Qué hace | Ref. |
|---|---|---|
| `sysNotify(title, sub)` | aviso del sistema `MOD_UNKNOWN` → `notifPush` | `Media.h:66-77` |
| `mediaNotify(type, title, sub)` | igual con `MOD_MEDIA` | `Media.h:88-98` |
| `sysSay(app, title, sub)` | **si `gState == ST_APP`** → `fpbPushSystem(app, …)` (solo banner, **no** entra en el historial); si no → `sysNotify` | `Media.h:83-86` |
| `fpbPushSystem(app, title, body)` | banner directo de origen sistema, sin huella ni icono | `Overlay.h:412-414` |

**`notifPush(m)`** (`Notif.h:181-199`):
1. Huella `key = FNV-1a` sobre `type`, `i2cAddr` y **solo el título** (`name`), nunca `sub`; 0 se reserva → 1
   (`notifKeyOf`, `:141-149`). Dos avisos con el mismo título son el **mismo** aviso.
2. Si ya hay una entrada activa con esa huella y no está saliendo (`phase != NP_OUT`): se **actualiza** su contenido y
   `bornMs = millis()` (vuelve a ser la más reciente). Si no: si el historial está lleno (`NOTIF_MAX = 3`) se elimina el
   más antiguo (índice 0) y se añade al final con `phase = NP_IDLE`, `armed = false`.
3. Siempre: `fpbPushSystemKeyed(key, type, name, sub)` → banner (§6.3), que también deduplica por huella.

`notifRemove(idx)` compacta la cola y **borra la ranura entera** (`memset`) (`Notif.h:162-172`).

**Consumidores del modelo:** Centro de notificaciones (§6.5), widget de notificaciones de la pantalla de bloqueo
(`Home.h:531`: muestra `gNotifs[gNotifCount-1].mod.name` o `t(S_NONOTIFS)`) [01a §4.3], campana y panel de
notificaciones de Modo PC (`DeXDraw.h:828`, `:1053-1061`, texto vacío `"Sin notificaciones"`).

### 6.2 Isla dinámica (legado inerte) (`FlexOS_Ultra_Notif.h`)

Se conserva compilada solo porque el compositor del paso de página de Inicio la referencia (`Home.h:1235-1266`,
`hpOwnsIsland`), pero **`notifBandOn` nunca se pone a `true` y ninguna entrada se arma (`armed=false`)**, así que
`notifHandleTouch` (`:254-299`) no encuentra tarjetas y `notifTick` (`:333-348`) sale en su primera comprobación.
Se documenta para no reintroducirla por error y por si se quiere reutilizar su geometría:

| Constante (`Types.h:412-424`) | Valor |
|---|---|
| `NOTIF_MAX` / `NOTIF_VISIBLE` | 3 (profundidad del historial) / 1 tarjeta a la vez |
| Tarjeta | x=16, y=`NOTIF_Y0`=56, 448×64, radio 28, separación 10 |
| Entrada | caída de 24 px en 280 ms, ease-out cúbica `1-(1-p)^3` |
| `NOTIF_HOLD_MS` | 5000 (ya no se usa) |
| Banda | y=26..126 (100 filas) |

Aspecto (`notifDrawCard`, `:214-247`): cola triangular hacia arriba (cx±8, y)→(cx, y−9) `thCard2()`; superficie
`uiSurface(UIS_ELEVATED)`; brillo `TH_TXT` α 42→0 de arriba abajo solo con Liquid Glass; borde `TH_BORDER`; icono 40 px
en (x+12, y+12); título tamaño 2 en (x+62, y+14) `TH_TXT`; subtítulo tamaño 1 en (x+62, y+38) `TH_TXT2`, ambos
recortados en x+w−32; aspa "X" en (x+w−22, y+20) ±5 px, trazo 1,8. Gestos: tocar a ±16 px de la X → salir; *swipe*
izquierda iniciado en la tarjeta → salir; arrastre solo hacia la izquierda (tope −(448+40)), al soltar < −112 px (W/4) →
salir, si no → muelle. `notifPauseForDrawer` (`:127-136`) la "congela" al subir la Caja de apps (inerte también).

> **Migración:** no portar la isla. Portar **solo el modelo** (`notifPush` con deduplicación por huella del título,
> historial acotado) como servicio `flex_notif` que publica en `FLEX_EV_NOTIF`.

### 6.3 Presentador único y cola del banner (`Overlay.h:173-422`)

Reglas (`Overlay.h:173-196`):
* **Uno a la vez.** Lo que llega con un banner a la vista **espera**; nadie le quita la pantalla al que se lee.
* **Prioridad** (`fpbRank`, `:340-343`): sistema (2) > teléfono con `pri ≥ FLP_PRI_HIGH` (1: HIGH o URGENT) > teléfono
  normal (0). Dentro de cada nivel, por orden de llegada.
* **Cola de 6** (`FPB_QUEUE`). Llena (`fpbEnqueue`, `:349-365`): si hay alguno de **menos** prioridad, sale el más nuevo
  de ellos y se cuenta en el resumen `fpbMore`; si no, el nuevo no entra y también se cuenta (`fpbMore++`). El que
  vuelve a la cola tras perder la pantalla (`front`) tiene preferencia sobre el último.
* **Deduplicación de avisos del sistema** (`fpbOffer`, `:373-392`): mismo aviso = misma huella (o, sin huella, mismo
  título **y** cuerpo). Si está a la vista y cambió el texto → se actualiza en sitio (`fpbRefresh`: se re-renderiza y su
  tiempo vuelve a contar desde cero); si está en la cola → se actualiza allí; nunca dos tarjetas iguales.
* **Si algo le quita la pantalla a medias** (transición de app, cortina, modal, giro): `fpbRequeue` (`:654-662`) lo
  devuelve **al frente** de su nivel con el tiempo que ya estuvo visible (`shownMs`), y reaparece en cuanto se pueda,
  con el vidrio de lo que haya debajo entonces y en la orientación nueva. Uno que ya estaba saliendo se pierde.

Mensaje en cola (`FlexPhoneBannerMsg`, `:198-208`): `app[32]`, `title[64]`, `body[120]` (copiados con recorte UTF-8
seguro `flexLinkUtf8Copy`), `id` (del teléfono), `key` (huella), `shownMs`, `src` (`FPN_SRC_PHONE=0`/`FPN_SRC_SYSTEM=1`),
`pri`, `icon` (1+`ModuleType` en avisos del modelo; 0 = sin icono).

### 6.4 Banner flotante (`fpb*`)

#### 6.4.1 Layout

| Orientación | Tarjeta (posición de reposo) | Radio | Ref. |
|---|---|---|---|
| Vertical | x=14, y=18, **452×72** | 18 | `:144-147, :155` |
| Horizontal (`gLand`, coordenadas **lógicas** 800×480) | x=24, y=14, **300×64** | 18 | `:151-154` |

Nunca en el centro de la pantalla (decisión de diseño explícita).

Contenido (`fpbDrawContent`, `:429-451`), coordenadas relativas a la tarjeta (w,h):

| Elemento | Posición / tamaño | Estilo |
|---|---|---|
| Barra de prioridad | (8, 12), 4×(h−24), radio 2 | `TH_PRIM` si `pri ≥ HIGH`; si no `TH_DIV` |
| Icono (solo avisos del modelo) | (20, (h−is)/2), `is = min(h−28, 40)` → 40 px en V, 36 px en H | icono de Ajustes (sistema) o Multimedia (medios) |
| `tx` (inicio del texto) | 20+is+12 = **72** (V con icono), 68 (H con icono), **22** sin icono | |
| Línea de app | tamaño 1 en (tx, 10), ancho máx. `w−tx−18`, con "..." | `TH_MUTE`; texto `app` o **`"Flex OS"`** si está vacío |
| Título | tamaño 2 en (tx, 28), mismo ancho, "..." | `TH_TXT` |
| Cuerpo (si hay) | tamaño 1 en (tx, 50), "..." | `TH_TXT2` |
| Resumen de pendientes | `"+%d"` (cola + `fpbMore`), tamaño 1 alineado a la derecha en (w−14, 10) | `TH_MUTE`; solo si > 0 |

Material (`fpbRender`, `:465-505`): **vidrio del sistema** `uiSurface(0,0,w,h,18,UIS_ELEVATED)` [02 §4.8] calculado sobre
lo que hay **debajo de la tarjeta en su posición actual** (no una foto de su sitio de reposo), sin usar bandas
pre-desenfocadas de otros dueños, con **tinte mínimo 150/255** (`FPB_MIN_MIX`: el texto se lee sobre cualquier fondo).
Fuera de pantalla (entrando/saliendo) se repite el píxel del borde. Se recalcula como mucho cada **80 ms**
(`FPB_REGLASS_MS`) si lo de debajo repintó esas filas, y en cada cuadro en que la tarjeta se mueve.
Sombra (`fpbBlit`, `:516-556`): copia de la forma desplazada (+3, +4), `TH_SHADOW` α **60**, solo donde la tarjeta no
tapa. Recorte de esquinas con la misma curva del vidrio (`glInset`).

Recorte de textos con `fgTextEllipsis` (`FlexPhone_UI.h:166-184`): corta en frontera UTF-8 y añade `"..."` (tres puntos
ASCII, no "…").

#### 6.4.2 Cuándo se puede dibujar (`fpbScreenAllows`, `:316-328`; `fpbCanShow`, `:331-334`)

El banner **no se arma** (y si ya está, se retira y vuelve a la cola) cuando:
* `notifSecureScreen()` (`Notif.h:98-112`): `ST_SPLASH`, `ST_OOBE_LANG`, `ST_OOBE_NAME`, `ST_OOBE_ACCOUNT`, `ST_LOCK`,
  `ST_LOCKSETUP` (alta/verificación de clave), `ST_POWEROFF_CONFIRM`, `ST_POWEROFF_ANIM`;
* restablecimiento en curso o `ST_FACTORY`, o **Modo seguro**;
* OTA dueño de la pantalla, u Optimizar Flex OS a la vista;
* **Modo PC** (`gState==ST_APP && gAppId==IC_MODOPC`) o app hospedada en DeX (`gHosted`) — exclusión dura: se registra
  pero no se dibuja;
* pantalla suspendida (`gSuspOn`);
* transición de app dibujando (`appTrOwnsScreen`);
* aviso de caída, tarjeta del cronómetro o aprobación de teléfono (Flex Storage) visibles;
* cortina del Panel rápido (`qsPanelY != 0 || qsAnimOn || qsDragging`);
* Centro de notificaciones abierto/arrastrándose/animándose (`fpcBusy`);
* `ST_HOMECFG` (Personalizar inicio).

Además, un aviso **del teléfono** no se muestra con No molestar (`phoneCanInterrupt`); uno **del sistema siempre**
(es la respuesta a algo que el usuario pidió). Mientras no se puede, la cola **espera** sin perder nada.
La pregunta se repite en **cada transferencia al panel** (`fpbStampBegin`): si alguien se queda la pantalla a mitad
de banner, ese mismo cuadro ya sale limpio (`fpbSuppressed`, `fpbCleanNeed`).

> Ojo: **sí** se permite sobre `ST_DRAWER`, `ST_SWITCHER`, `ST_CTX`, `ST_KIOSKSET`, `ST_WIFI`, `ST_CONN`, `ST_FILES`,
> `ST_KBSET`, `ST_SAFE`(no: Modo seguro lo veta), **`ST_THEFT`** y sobre la app clavada del kiosco (ver §6.10).

#### 6.4.3 Interacción (`fpbTouch`, `:704-770`; se llama en `loop` antes que la isla, el OTA y el `switch`)

* Es **no modal**: el toque es suyo **solo si el dedo baja dentro de la tarjeta** (sin la sombra) mientras está
  `FPB_IN` o `FPB_SHOWN` y no hay tragado de episodio. Desde ese apoyo, **el episodio entero** (bajar, arrastrar, soltar)
  es del banner y la pantalla de debajo no lo ve (`touchHoldBack`, [01b §2.3]). Un episodio que empezó fuera es de la
  pantalla de debajo aunque pase por encima de la tarjeta.
* **Arrastre horizontal** 1:1 a izquierda o derecha a partir de **10 px** (`FPB_DRAG_PX`); tope ±(ancho+40).
  Velocidad medida en ventanas de ≥16 ms; si el dedo estuvo quieto >100 ms antes de soltar, velocidad 0.
* **Soltar**: descarta si `|desplazamiento| ≥ ancho/3` (151 px en V, 100 px en H) **o** lanzamiento en el mismo sentido
  con `|v| ≥ 0,7 px/ms` y `|desplazamiento| ≥ 40 px`. Descartar = sale deslizándose hacia ese lado y, si es del teléfono,
  **se borra también en el teléfono** (`fpbDismissed`, `:688-702`: envía `FLNK_T_NOTIF_REMOVE` con el id si el enlace
  está listo, la quita del modelo y reconstruye conversaciones). Si no llega: muelle de vuelta a su sitio.
* **Toque** sin arrastre: cierra el banner (sube por donde entró); si era **del teléfono** además abre Flex Phone en la
  sección Notificaciones (`fphSection = FPH_NOTIFS`; si estaba en una app, `appClose()` y `enterApp(IC_FLEXPHONE)`).
  Un aviso del sistema no navega a ningún sitio.
* Mientras el dedo lo arrastra, el muelle corre o está desplazado, **no caduca** (`fpbT0` se rearma).
* Si el banner desaparece a mitad del gesto, el resto del episodio no es de nadie (`gTouchSwallow = true`).

#### 6.4.4 Estados, tiempos y curvas (`fpbTick`, `:773-866`)

Estados `FPB_HIDDEN → FPB_ARMED → FPB_IN → FPB_SHOWN → FPB_OUT → FPB_HIDDEN`.

| Fase | Duración | Movimiento | Curva |
|---|---|---|---|
| Entrada `FPB_IN` | **220 ms** | cae desde `fpbDropMax = y + h + 10` (100 px en V, 88 en H) por encima de su sitio | ease-out cuadrática `1-(1-p)^2` |
| A la vista `FPB_SHOWN` | **4200 ms** (`FPB_HOLD_MS`); si vuelve de la cola tras perder la pantalla: `max(1500, 4200 − shownMs)` | quieto | — |
| Muelle (no llegó al umbral) | **140 ms** | desplazamiento → 0 | ease-out cuadrática |
| Salida `FPB_OUT` dir 0 (cierre/caducidad/tap) | **180 ms** | sube por donde entró (hasta `fpbDropMax`) | ease-out cuadrática |
| Salida dir ±1 (descartado con el dedo) | 180 ms | desde su posición hasta ±(ancho+40) | ease-out cuadrática |

Al terminar la salida: `fpbFinish(true)` (devuelve al panel sus filas limpias, suelta buffers) y `fpbMore = 0`.

#### 6.4.5 Memoria y estampado (detalle de implementación que condiciona el diseño)

* Buffers solo mientras hay banner: lienzo de tarjeta 480×72×2 = 69 120 B y respaldo de 80 KB, en PSRAM. **No se arma**
  si la PSRAM libre < `FLEXMEM_CRIT_BYTES` (5 MB) + ambos (`fpbArm`, `:664-677`): el aviso no se dibuja (sigue en el
  Centro si era del modelo) y se abandona sin reintento.
* El banner **nunca vive en el framebuffer**: se estampa en `flxFlush` justo antes de cada transferencia al panel y se
  retira justo después (`Gfx.h:112-164`). Por eso la pantalla de debajo puede seguir animándose (un juego sigue
  corriendo) y al irse no deja rastro.

> **Migración (LVGL):** objeto en `lv_layer_sys()` (encima de todo menos de los modales que lo suprimen), oculto con
> `LV_OBJ_FLAG_HIDDEN` cuando `fpbScreenAllows()` sea falso; posición animada con `lv_anim` (`lv_anim_path_ease_out`
> cuadrática ≈ `lv_anim_path_ease_out`; si se quiere exacta, `path_cb` propio `1-(1-p)^2`). Arrastre con
> `LV_EVENT_PRESSING` y `LV_OBJ_FLAG_PRESS_LOCK`; la captura de episodio la da LVGL por construcción (el objeto pulsado
> recibe el resto del gesto). Vidrio: el de [02 §4.12]; si no hay *backdrop* real, fondo `TH_GLASS2`/`TH_SURF` con
> opacidad ≥ 150/255 + sombra (`shadow_ofs_x=3`, `shadow_ofs_y=4`, `shadow_opa=60`). La cola/prioridad/deduplicación/
> reencolado es **lógica pura** (ver §6.11).

### 6.5 Centro de notificaciones (`fpc*`, `Overlay.h:868-1196`)

#### 6.5.1 Propósito y cómo se llega

Pantalla completa con el **historial**: notificaciones del teléfono (`fphModel.notif[]`, hasta `FLP_NOTIF_MAX = 40`) y
avisos del sistema (`gNotifs[]`, hasta 3), en una sola lista (máx. 43, `FPN_LIST_MAX`). Es "dueño de la pantalla"
mientras está abierto, se arrastra o se anima (`fpcGlobalHandle` se evalúa en `loop` **antes** que la cortina; si
devuelve `true`, solo corren `kioskTick`, `notifTick`, OTA y la pausa: la pantalla de debajo no hace tick)
[01b §1.3 paso 8].

**Puede abrirse** (`fpcCanOpen`, `:898-907`) solo en vertical, no hospedado en DeX, sin Modo edición, **sin kiosco**,
sin OTA (dueño u overlay), sin cortina (ni arrastre ni animación) y con `gState` = `ST_HOME` o `ST_APP`. Si deja de
poder estar abierto estando abierto → `fpcForceClose()`.

**Gesto de apertura** (`:1090-1116`): con el dedo **apoyado y moviéndose** (`T.down`), nacido en el borde izquierdo
`T.startX < 26` (`SYS_EDGE_LEFT_W`) y `T.startY > 40`, cuando el recorrido hacia dentro supera **12 px**
(`FPC_INTENT_PX`) y es mayor que el vertical. Un toque en esa franja sin deslizar sigue siendo un toque normal de la
pantalla de debajo. Al reconocerse: construye la lista, resetea el scroll, sigue al dedo desde donde nació
(`fpcX = −480 + dx`, acotado a −480..0) y se queda el episodio (`touchHoldBack`).

**Cierre:** soltar el arrastre de apertura con `fpcX ≤ −240` (o sin haberse movido) → cerrado; arrastre hacia la
**derecha** dentro del Centro abierto con `x − startX > 60` y `|dy| < 40` → anima a cerrado (`:1166-1170`).
Al cerrarse: `fpcForceClose()` pone `gHomeDirty = true` (el escritorio se recompone).

> **Rareza / bug a no copiar:** el contenido **no se desplaza con `fpcX`** (`fpcRender` pinta siempre a x=0): en cuanto
> el gesto se reconoce el Centro aparece entero, y la "animación" de 190 ms solo cambia una variable. En IDF animar de
> verdad el `x` del contenedor (−480 → 0).

#### 6.5.2 Layout (`fpcRender`, `:962-1030`)

Fondo **opaco** `TH_PAGE` a pantalla completa (sin vidrio).

| Elemento | Posición / tamaño | Estilo / texto |
|---|---|---|
| Título | tamaño 3 en (20, 26) | `TH_TXT`; `"Notificaciones"` (EN `"Notifications"`) |
| Subtítulo | tamaño 1 en (20, 60) | `TH_MUTE`; `"%d en total"` / `"%d in total"`; vacío: `"Nada pendiente"` / `"Nothing pending"` |
| Píldora No molestar | (372, 30) 92×34, radio 17 | fondo `TH_PRIM` si activo, `TH_SURF2` si no; texto tamaño 1 centrado en (418, 39): `"Silencio"`/`"DND on"` (`TH_ONACC`) o `"Avisos"`/`"DND off"` (`TH_TXT2`); zona táctil id 1 |
| Divisor | línea (20, 84) de 440 px | `TH_DIV` |
| Vista con scroll | y = 96 .. 792 | `FgScroll {off, content, 96, 792}` |
| Fila (cada 78 px, primera en y = 96 − scroll) | tarjeta (16, y) 448×70 radio 16 (`fgCard`: vidrio plano tinte `TH_GLASS` con Liquid Glass / `TH_SURF` sin él, sobre `TH_PAGE`) + barra de acento (19, y+10) 4×50 radio 2 | acento: `TH_PRIM` si `pri ≥ HIGH`, `TH_ACCS` si es del sistema, `TH_DIV` si es del teléfono normal |
| · App | tamaño 1 en (38, y+8), máx. 370 px | `TH_MUTE`; `"Flex OS"` en avisos del sistema; en el teléfono `app` o el nombre de paquete |
| · Contador de grupo | `"x%u"` tamaño 1 alineado a la derecha en (446, y+8) | `TH_MUTE`; solo si hay >1 de la misma app |
| · Título | tamaño 2 en (38, y+26), máx. 404 px | `TH_TXT` |
| · Cuerpo | tamaño 1 en (38, y+50), máx. 404 px | `TH_TXT2`; si el teléfono oculta el contenido: `"Contenido oculto"` / `"Content hidden"` |
| Botón | tras la última fila +6: (16, y) 448×42, radio 16 | `fgButton` estilo peligro: fondo `TH_SURF2`, texto tamaño 1 `TH_DANGER` `"Borrar todas"` / `"Clear all"`; id 2 |
| Barra de scroll | x=471, ancho 4, radio 2, alto `vista²/contenido` (mín. 28) | `TH_TRACK`; solo si hay más contenido que vista |
| Vacío (`fgEmpty(156,…)`) | círculo r=26 en (240, 186) `TH_SURF2` + glifo de estado "apagado"; título tamaño 2 centrado en y=226 `TH_TXT2`; párrafo tamaño 1 en (48, 256) ancho 384, máx. 3 líneas de 16 px, `TH_MUTE` | título `"Todo al dia"` *(sic)* / `"All clear"`; párrafo con DND: `"No molestar esta activado. Lo que llegue se guarda aqui, sin banner y sin sonido."` *(sic)* / `"Do not disturb is on. Anything that arrives is kept here, without a banner or a sound."`; sin DND: `"Aqui aparecen las notificaciones de Flex OS y las del telefono vinculado."` *(sic)* / `"Notifications from Flex OS and from the linked phone show up here."` |

Orden de la lista (`fpcBuild`, `:912-957`): **más reciente primero** por `whenMs` (recepción en el teléfono `rxMs`, o
`bornMs` del sistema), inserción estable. Se reconstruye al abrir, al llegar algo nuevo con el Centro abierto
(`phoneNotifyBridge`) y tras borrar.

#### 6.5.3 Interacción con el Centro abierto (`:1160-1186`, `fpcHandleHit :1051-1086`)

* **Scroll vertical** 1:1 con `fgDragStep` (umbral 8 px, `FG_DRAG_SLOP`); el `tap` que llega al soltar tras desplazar
  no cuenta como toque (`FG_DRAG_CONSUMED`). Sin inercia ni goma.
* **Toque** (hit-test por zonas registradas al pintar; la última pintada gana):
  * id 1 → conmuta No molestar (`phoneDndSet(!gDnd)`, persiste) y repinta;
  * id 2 → **Borrar todas**: vacía las notificaciones del teléfono (`flexPhoneNotifClearAll`) **y** el historial del
    sistema (`gNotifCount = 0`), reconstruye y resetea el scroll. Sin confirmación;
  * fila del **teléfono** → cierra el Centro y abre Flex Phone en Notificaciones (suspendiendo la app actual);
  * fila del **sistema** → se descarta (`notifRemove`) sin confirmación.
* Arrastre a la derecha (>60 px, |dy|<40) → cierra con animación.
* No hay descarte por deslizamiento de filas en el Centro (solo en el banner).

#### 6.5.4 Animación

`FPC_ANIM_MS = 190`, ease-out cuadrática `1-(1-p)^2` sobre `fpcX` (ver rareza en §6.5.1).

### 6.6 No molestar (`Overlay.h:44-82`, `:1252-1264`)

* Estado `gDnd`, NVS **namespace `flexphone`**, clave **`dnd`** (bool, defecto `false`). Se carga en
  `flexPhoneBegin()` → `flexPhoneOverlayBegin()` (`FlexPhone_Bridge.h:284`): **no se carga en Modo seguro** (queda
  `false`). Se guarda en cada cambio (`phoneDndSet`, solo si cambia).
* Efecto: `phoneCanInterrupt()` = `!gDnd` → **ningún banner del teléfono**; la notificación se registra igual y aparece
  en el Centro. `phoneCanSound()` = `!gDnd && !flexAudioMuted()` existe pero **no hay sonido de notificación** en el
  sistema (no se finge; `:1225-1236`). Los avisos del **sistema** no se ven afectados.
* Silencio (`flexAudioMuted`) ≠ No molestar: silencio quita el sonido pero el banner sí sale.
* Controles: píldora del Centro y control `QSID_DND` del Panel rápido [01b §5.3] con subtítulos `"Sin avisos"` (DND),
  `"Avisos, sin sonido"` (silenciado), `"Avisos normales"`.

### 6.7 Puente con Flex Phone (`phoneNotifyBridge`, `Overlay.h:1202-1237`)

Llamado desde `flexPhoneTick()` en cada vuelta en que el enlace está activo (`FlexPhone_Bridge.h:334`). Busca la
notificación con mayor `rxMs`; si su `id` difiere del último presentado (`fpnLastSeenId`), es nueva: si el Centro está
abierto lo reconstruye; si DND → termina; si no → `fpbPush(FPN_SRC_PHONE, id, app|pkg, title, cuerpo|"" , pri)`.

> **Rareza:** solo mira la **más reciente**: si llegan varias entre dos vueltas, solo la última genera banner (las demás
> quedan en el Centro). En IDF, consumir eventos `FLEX_EV_NOTIF` uno a uno.

### 6.8 Catálogo de avisos que se presentan (texto exacto)

**De esta área** (memoria, §17):

| Título | Cuerpo | Cuándo | Ref. |
|---|---|---|---|
| `"%s no se abre ahora"` (nombre de app) | `"Memoria libre en trozos pequeños"` (bloque) / `"Memoria interna baja: se protege el sistema"` (SRAM) / `"Cierra una app o pulsa Optimizar Flex OS"` (PSRAM) | apertura negada por presupuesto | `Core.h:334-352` |
| `"Memoria crítica"` | `"Flex OS está protegiendo el sistema. Cierra una app."` | nivel sube a CRITICAL y el alivio no basta | `Core.h:416-418` |
| `"Memoria casi llena"` | `"Flex OS liberó recursos en segundo plano."` | nivel sube a WARN y el alivio no basta | `:420-421` |
| `"Memoria libre repartida en trozos pequeños"` | `"Las imágenes o apps pesadas pueden tardar más"` | fragmentación alta con ≥6 MB libres | `:431-433` |
| `"Memoria interna del sistema baja"` | `"Se limitan cargas pesadas para proteger Wi-Fi y táctil"` | SRAM interna < 64 KB | `:434-436` |
| `"Almacenamiento interno en uso elevado"` | `"Limpiar la caché temporal puede ayudar"` | LittleFS ≥ 80 % (solo medido con el detalle de memoria a la vista) | `:437-439` |
| `"Almacenamiento interno casi lleno"` | `"Las actualizaciones y los datos nuevos podrían fallar"` | LittleFS ≥ 90 % | `:440-442` |

**De otras áreas** (se presentan por el mismo canal; textos y lógica en sus documentos): Flex Account
(`"Flex Account"` / `"Cuenta desvinculada de este P4"`, `Account_Bridge.h:270`); Flex Storage/Cloud
(`StoragePair.h:267-341`: `"El emparejamiento ya había caducado"`, `"Aprobado: termina en el teléfono"`,
`"Teléfono rechazado"`, `"Activado en %s"`, `"Vuelve a emparejar el teléfono"`, `"Desbloquea para aprobar el teléfono"`,
`"Un teléfono quiere emparejarse"`; `AppStorage.h:763-773`); Flex Cloud/CloudKit (`CloudKit.h:132,991-992`, `sysSay`
dentro de apps; `"Flex Account: sesión perdida"`/`"Flex Account: sesión caducó"` + `"Vuelve a vincular en Ajustes >
General"`); Flex Web Server (`WebServer.h:678-759`); Protección contra robo (`Theft.h:573,579`); Device Care / caídas
(`FallAlert.h:442`); visor/medios (`MediaViewer.h:836,1555,1694,2160`, `MediaKit.h:285-379`, `AppFiles.h:287-304`
`mediaNotify(MOD_MEDIA, nombre, "Formato no compatible")`); Galería/editores (`GalleryEdit.h`, `GalleryVideoEdit.h`);
Música (`AppMusic.h:264-443`, texto dinámico `musErr`); biblioteca de medios (`MediaLib.h:344`, cola de 50 B por
mensaje); apps flex-app-v1 (`AppHost_Bridge.h:276`).

**Del teléfono:** título/cuerpo/app de la notificación Android, prioridad `FLP_PRI_MIN..URGENT`
(`FlexPhone.h:62`).

### 6.9 Persistencia

| Dato | Dónde | Formato |
|---|---|---|
| No molestar | NVS `flexphone` / `dnd` | bool (u8), defecto false |
| Historial del sistema `gNotifs[]` | **solo RAM** (se pierde al reiniciar) | — |
| Notificaciones del teléfono | modelo de Flex Phone (`fphSave`, documento de Flex Phone) | — |
| Cola del banner | solo RAM | — |

### 6.10 Casos límite, errores y bugs

* Historial del sistema de **solo 3 entradas** (`NOTIF_MAX`): el 4.º aviso expulsa el más antiguo del Centro.
* `sysSay` dentro de una app **no deja rastro en el Centro** (solo banner).
* Banner permitido sobre **`ST_THEFT`** (pantalla de bloqueo por robo) y sobre la app clavada del kiosco: puede mostrar
  contenido de notificaciones del teléfono en un aparato "bloqueado por robo". Decidir en IDF (recomendado: vetar).
* Centro: las filas que suben por el scroll **se pintan encima de la cabecera** (no hay recorte en y<96) y, como sus
  zonas táctiles se registran después, **una fila puede tapar la píldora de No molestar**. En IDF: contenedor con scroll
  y `LV_OBJ_FLAG_OVERFLOW_VISIBLE` desactivado.
* Al cerrar el Centro **encima de una app** solo se marca `gHomeDirty`; nada repinta la app → puede quedar el último
  cuadro del Centro hasta que la app repinte por su cuenta (en LVGL desaparece el problema: el Centro es una capa).
* "Borrar todas" no vacía la **cola del banner** (puede salir después un banner de algo ya borrado).
* Banner sin PSRAM suficiente → se pierde la presentación (no reintenta).
* Textos del Centro solo en ES/EN (`LI() == 1 ? EN : ES`): FR/PT/IT ven español (§15.3).

### 6.11 Notas de migración

* **Lógica pura reutilizable tal cual** → `flex_portable/notif_model.c` + pruebas host:
  `notifKeyOf` (FNV-1a), `notifPush`/`notifRemove` (historial acotado con dedupe), `fpbRank`, `fpbEnqueue`
  (cola ordenada de 6 con resumen `+N`), `fpbSameSys`, `fpbOffer` (actualización en sitio), `fpbRequeue` y el cálculo de
  `hold = max(1500, 4200 − shownMs)`, `fpcBuild` (fusión y orden por tiempo), umbrales de descarte
  (`|dx| ≥ w/3` o `v ≥ 0,7 px/ms ∧ |dx| ≥ 40`). Pruebas: cola llena con prioridades mezcladas; aviso igual con texto
  nuevo a la vista (se actualiza, tiempo a cero); reencolado al frente; DND bloquea teléfono y no sistema.
  Pruebas existentes en Arduino: `testNotifUnaSola` (`tests/host/ino_compile.cpp:2418`), `testBannerNotificacion`
  (`:10593`), `testBannerVidrioSigueAlFondo` (`:13477`).
* **Servicio** `flex_notif` (tarea UI o propio): recibe avisos por `flex_bus_post(FLEX_EV_NOTIF, …)` desde cualquier
  tarea (Wi-Fi, nube, medios ya no llaman a la UI directamente); la UI se suscribe y presenta.
* **UI**: banner en `lv_layer_sys()`; Centro como `lv_obj` 480×800 en `lv_layer_top()` cuyo `x` se anima y que entra
  en la **pila de dueños** de [01b §1.3] por encima del Panel rápido. El gesto de borde izquierdo va en la capa de gestos
  del sistema (`flex_touch_arb`/capa siguiente), con la misma regla "nace en el borde + se mueve como gesto".
* Ampliar el historial del sistema (p. ej. 20) y persistirlo es una decisión de producto; si se hace, mantener la
  deduplicación por título.

---

## 7-8. Teclado y Ajustes del teclado

Fuera del alcance de este documento (documento aparte del teclado). Aquí solo se listan sus **claves NVS** en §16 por
completitud de `cfgLoad`.

---

## 9. Suspensión de pantalla (apagado normal) y bloqueo automático

[01a §5.11-§5.12 y §14.5] ya describen el gesto y el auto-bloqueo; aquí se completa con el detalle exacto.

### 9.1 Propósito

"Apagar la pantalla" sin dormir el chip: `loop()` sigue (Wi-Fi, reloj, música, descargas); solo se apaga la **salida
visual**: backlight a 0 por PWM + comando DCS `DISPOFF`. **`gState` no cambia**; framebuffer intacto. Al despertar, el
propio fundido del backlight hace de "fade-in" (`Touch.h:102-127`).

Entradas: **doble toque con dos dedos** en cualquier sitio; control **"Bloquear"/"Bloquear ahora"** del Panel rápido
(`qpTapLock`, `QuickPanel.h:262`: cierra la cortina y llama a `suspEnter()`; solo disponible con clave configurada).
Salida: **doble toque con un dedo** (no hay botón físico).

### 9.2 Detector de doble toque (`suspGestureUpdate`, `Touch.h:233-304`)

Trabaja sobre la **cuenta de dedos del GT911** (`gtFingers`, válida si el último frame tiene <90 ms), no sobre `T`.

| Constante (`Types.h:325-330`) | Valor | Significado |
|---|---|---|
| `SUSP_TAP_WINDOW_MS` | 450 | máximo entre el final del 1.er toque y el final del 2.º |
| `SUSP_TAP_GAP_MS` | 45 | separación mínima real (filtra rebotes) |
| `SUSP_TAP_MAX_MS` | 600 | duración máxima de un toque (más = pulsación larga, rompe la cadena) |
| `SUSP_TAP_FRAMES` | 2 | lecturas **consecutivas** con n≥2 para confirmar "dos dedos" |
| `SUSP_FADE_STEP_MS` / `SUSP_FADE_STEP` | 10 ms / 6 puntos | fundido: 6 % cada 10 ms (80 %→0 ≈ 140 ms; 100 %→0 ≈ 170 ms) |

Episodio = del primer dedo abajo a todos arriba. 3+ dedos o >600 ms → anula ambas cadenas. Episodio de 2 dedos
confirmado: si había otro válido en los últimos 450 ms y han pasado ≥45 ms → **suspender**, salvo **veto**: kiosco
activo, `kbTypingNow()` (dedos sobre el teclado) o `gTouchPinchUsed` (una app usó el episodio como pellizco). Episodio de
1 dedo: **solo se escucha suspendido**; dos seguidos → **despertar**. Las dos cadenas son excluyentes.

**Tragado** (`gSuspSwallow`): suspendido → **todos** los eventos de `T` (incluido `down`) se anulan; despierto → solo los
episodios ya confirmados de 2 dedos, salvo tecleando o si una app es dueña de los dos dedos (`gTouchOwnsTwoFinger`,
p. ej. el visor). Así el gesto no abre iconos ni dispara el menú contextual.

### 9.3 Entrar, despertar y fundido (`Touch.h:184-230`, `Power.h:633-657`, `HAL.h:130-163, :284-300`)

* `suspEnter()`: `qsForceClose()`; guarda `gSuspBright = gBright`; `gSuspOn = true`; fundido a 0.
* `suspFadeTick()` (en `loop`, paso 4 de [01b §1.3]): cada 10 ms ±6 puntos con `blWritePct` (rampa **lineal** 0..100 →
  duty 0..255, **sin tocar `gBright`**). Al llegar a 0: `gSuspDark = true` y `panelDisplayOff()` (DCS `0x28`, si
  `PANEL_DCS_SLEEP_ON`). Al llegar al destino >0: `setBacklight(gSuspBright)` (mapa normal 5..100 → duty 25..255) y
  `gSuspOn = false` (el táctil vuelve a fluir).
* `suspWake()`: **antes de encender nada** llama a `suspWakeLockScreen()`: si hay clave (`gLockType > 0`) y no se estaba
  ya en `ST_LOCK`/`ST_LOCKSETUP`, anota `gSuspRetState`/`gSuspRetApp`, sale del Modo edición, `gRippleActive=false`,
  `qsPanelY = 0`, fuerza vertical y recorte completo, `gState = ST_LOCK`, compone y vuelca el bloqueo **a oscuras**.
  La app **no se cierra**: tras acertar la clave se vuelve a ella [01a §5.12]. Después, si estaba en DISPOFF →
  `panelDisplayOn()` (`0x29`), y fundido hacia `gSuspBright` **desde el PWM real** (`gBlPct`, no desde `gBright`: evita
  el fogonazo).
* Sin clave: se despierta donde estaba.

### 9.4 Qué ocurre mientras está suspendida

Sigue corriendo todo el `loop` (servicios, red, música). Vetado o pausado: banner (`fpbScreenAllows`), auto-bloqueo
(`autoLockTick` sale), todo el táctil salvo el doble toque de 1 dedo. El apagado completo reutiliza este mismo fundido
(§10.5).

### 9.5 Bloqueo automático por inactividad (`autoLockTick`, `Lock.h:356-396`; `autoLockNow`, `:343-355`)

Orden exacto de comprobaciones en cada vuelta:
1. `AUTOLOCK_ON`; kiosco → no hace nada; suspendido → no hace nada; `ST_POWEROFF_CONFIRM/ANIM` → no hace nada.
2. OTA ocupado o dueño de la pantalla → **rearma** (`gLastTouchMs = now`) y sale (nunca bloquea a mitad de una OTA).
3. Cualquier `T.down/pressed/released` → rearma y sale (cualquier pantalla).
4. `gAutoLockMs == 0` ("Nunca") → sale.
5. Solo en `ST_HOME`, `ST_APP`, `ST_DRAWER`, `ST_HOMECFG`; no en horizontal ni hospedado; no con la cortina abierta.
6. Primera vez (`gLastTouchMs == 0`) → arma y sale. Vencido → `autoLockNow()`.

`autoLockNow`: cierra Personalizar inicio **guardando**, sale del Modo edición, si hay app → `appClose()` (suspende con
su animación), `renderHome`, `renderLock`, `gState = ST_LOCK`, el bloqueo **baja** desde arriba (`animateTo(800, 0)`,
[01a §5.11]), `gLastTouchMs = now`. **Se aplica aunque no haya clave** (cae en el bloqueo de deslizar).

Opciones/valor: NVS `flexos/autolockms` (§16), lista `30000 "30 segundos"`, `60000 "1 minuto"` (defecto),
`300000 "5 minutos"`, `600000 "10 minutos"`, `1800000 "30 minutos"`, `0 "Nunca"` (`Prefs.h:54-73`). Ajustes →
Seguridad → `"Bloqueo de inactividad"` cicla y rearma el temporizador (`AppSettings.h:676-683`).

### 9.6 Casos límite y bugs

* **Bug grave a no copiar:** el gesto de suspensión **no está vetado en `ST_FACTORY`** y la rama exclusiva del
  restablecimiento en `loop` (`.ino:721-726`) **no ejecuta `suspFadeTick`**. Un doble toque de dos dedos en el asistente
  deja `gSuspOn = true` sin fundido → todo el táctil tragado. Con clave, el despertar salta a `ST_LOCK` saliendo del
  asistente; **sin clave**, el táctil queda muerto hasta reiniciar (y en la pantalla de fallo, con marcador persistente,
  vuelve a ella). En IDF: vetar suspensión en `ST_FACTORY`/apagado/Optimizar/OTA y que el fundido sea un `lv_anim`/timer
  independiente del dueño de la pantalla.
* El fundido avanza por pasos de `loop` (≥10 ms): si una vuelta tarda más, el fundido se alarga (no salta).
* `gPoffPin` (apagado seguro) **nunca** se consulta al suspender (`Prefs.h:76-87`): suspender no pide nada.
* El detector usa la cuenta de dedos con 90 ms de seguridad; un GT911 que no manda el frame de "0 dedos" se resuelve por
  tiempo.

### 9.7 Notas de migración

* El detector **ya está portado** como lógica pura en `FlexOS_Ultra_IDF/components/flex_touch/src/flex_touch_arb.c`
  (`flex_arb_poll`, eventos `FLEX_ARB_EV_SUSPEND/WAKE`) con prueba `tests/host/test_touch_arb.c`. Añadir: veto por
  estado de pantalla (factory, apagado, Optimizar) y entrada de "suspendido" desde el servicio de energía.
* Servicio `flex_power` (no existe aún en IDF: `components/flex_power/` vacío): `suspend()`/`wake()`, fundido del
  backlight con `ledc_set_fade_with_time` o un `esp_timer` de 10 ms (rampa lineal 0..255), DCS `0x28/0x29` por
  `esp_lcd_panel_io_tx_param`, publica `FLEX_EV_POWER` (`SUSPENDED`, `WOKEN`). Durante la suspensión pausar el
  refresco de LVGL (`lv_display_enable_invalidation(disp, false)` o no llamar a `lv_timer_handler` salvo lo necesario)
  para ahorrar.
* El bloqueo al despertar se compone **antes** del DISPON: en LVGL cargar la pantalla de bloqueo y forzar
  `lv_refr_now()` con el backlight a 0.
* `autoLockTick` es lógica pura con entradas (estado, kiosco, OTA, toque, ms) → prueba host.

---

## 10. Apagado completo ("Apagar" → deep sleep real)

### 10.1 Propósito y entradas

Apagado real del ESP32-P4 en *deep sleep*, con confirmación deslizante, clave opcional, animación final y **filtro de
encendido de 3 s**. Entradas: botón de encendido de la cabecera del Panel rápido (círculo r=22 en (390,50), [01b §5.6])
y el control `QSID_POWEROFF` (`qpTapPoweroff`, `QuickPanel.h:263`) → `qsRestoreBg(); qsForceClose(); poffEnter()`.
`POWEROFF_ON = 1` (`Types.h:315`).

### 10.2 Pantalla "¿Apagar FlexOS?" (`ST_POWEROFF_CONFIRM`, `poffDrawStatic/poffDrawKnob`, `Power.h:729-782`)

Siempre en vertical y a pantalla completa (fuerza `gLand=false` y recorte completo; `ensureBlurBg()`).

| Elemento | Geometría | Estilo |
|---|---|---|
| Fondo | pantalla completa | fondo de pantalla desenfocado `blurBg` (o `TH_SCRIM` sólido si no hay) + velo `TH_SCRIM` α150 |
| Panel | `uiWallSurface(28, 232, 424, 424, r=44, TH_WALLPANEL #24283A, nivel 9)` | vidrio "sobre wallpaper" [02 §2.3] |
| Título | tamaño 3 centrado en y=268 | `TH_ONWALL` (#FFFFFF): **`"¿Apagar FlexOS?"`** |
| Subtítulo | tamaño 1 centrado en y=306 | `TH_ONWALL2` (#D7DEEE): **`"El sistema entrará en reposo profundo"`** |
| Pista | sombra (42, 356) 400×96 r48 `TH_SHADOW` α60; superficie `uiWallSurface(40, 352, 400, 96, r48, TH_WALLSURF #2C365C, 7)`; contorno `TH_ONWALL2` | |
| Estela | (40, 352), ancho `knob + 96` (máx. 400), alto 96, r48 | `TH_DANGER` α `40 + p·130/100` (p = % recorrido); solo si knob>0 |
| Rótulo | tamaño 2 centrado en (258, 391) | `TH_ONWALL` α `235 − 2p` (desaparece al avanzar): **`"desliza para apagar"`** |
| Pomo | círculo r=42 centrado en (`88 + knob`, 400) `TH_ONWALL`; sombra r42 en (+1,+2) `TH_SHADOW` α70; glifo de encendido IEC 5009 de 30 px `TH_DANGER` | `knob` ∈ 0..304 |
| Botón Cancelar | sombra (132, 564) 220×72 r36 α60; superficie (130, 560, 220, 72, r36, `TH_WALLSURF`, 7); contorno `TH_ONWALL2`; texto tamaño 2 centrado en y=587 | **`"Cancelar"`** `TH_ONWALL` |

Constantes: `POFF_TRACK_*` 40/352/400/96, `POFF_KNOB_PAD 6`, `POFF_KNOB_D 84`, `POFF_RUN = 400−12−84 = 304`,
`POFF_DONE_PCT 92` (→ knob ≥ 280), banda repintada y=344..456 (`Power.h:673-690`). La parte estática se compone una
vez y la banda del deslizador se cachea (~135 KB en PSRAM, `poffBand`, se reserva una vez por sesión y la suelta
`memShedSystem` fuera de estas pantallas).

### 10.3 Interacción (`poffTick`, `Power.h:834-878`)

* **Agarre**: `T.pressed` con y ∈ [352, 448] y x ∈ [kx−24, kx+84+24] (kx = 46 + knob). El pomo sigue al dedo **1:1**
  conservando el desfase del agarre.
* **Soltar** con ≥92 % del recorrido → pomo al final; si `poffPinRequired()` (`gPoffPin && gLockType > 0`,
  `POWEROFF_PIN_ON`) → verificación `LSU_AFTER_POWEROFF` [01a §5.1]; si no → animación final. Menos de 92 % → vuelve
  solo a 0 con aproximación proporcional **por vuelta de `loop`**: `knob += (d±3)/4`, engancha cuando |d|<3.
* **Cancelar**: `T.tap` dentro de (130..350, 560..632) → `gState = ST_HOME; renderHome(); showHome()`.
* No hay gesto de atrás ni barra de navegación; el Panel rápido/Centro no se pueden abrir (no es `ST_HOME`/`ST_APP`).

### 10.4 Apagado seguro (`gPoffPin`)

* Preferencia NVS `flexos/poffpin` (bool, defecto `false`). Ajustes → Seguridad → **`"Apagado seguro"`** con subtítulo
  `"Configura antes un PIN"` (sin clave: inerte), `"Activado"` / `"Desactivado"` (`AppSettings.h:365-368, :685-692`).
* Con clave correcta → `lsuFinishAfter` → `poffBeginAnim()` (la animación arranca desde lo que hay en pantalla, la
  verificación). **Cancelar** la verificación → `poffEnter()` otra vez (deslizador en reposo), nunca apaga ni va al
  escritorio (`Power.h:110-112`).

### 10.5 Animación final (`ST_POWEROFF_ANIM`, `poffBeginAnim/poffAnimTick`, `Power.h:935-997`)

Copia el cuadro actual en `lockBuf` (no reserva 750 KB nuevos) y avanza por fases medidas con `millis()`:

| Fase | Duración | Qué se ve |
|---|---|---|
| 0 | **520 ms** | fundido **lineal** a negro del cuadro congelado (`mix565(px, negro, e·255/520)`) |
| 1 | **260 ms** | aparece **`"Flex OS"`** tamaño 5 centrado en y=374 (`SCR_H/2 − 26`), `TH_ONWALL`, α 0→255 lineal |
| 2 | **700 ms** | texto quieto |
| 3 | **620 ms** | texto α 255→0 lineal |
| 4 | ≈140-170 ms | fundido del backlight a 0 (mismo mecanismo que §9.3) |
| 5 | — | `panelSleepIn()` (DCS `0x28` + `0x10` SLPIN), `poffSaveCleanFlag()`, `poffEnterDeepSleep()` (no retorna) |

Total visible ≈ 2,3 s. No es interrumpible ni cancelable una vez empezada.

### 10.6 Deep sleep y filtro de encendido (`poffEnterDeepSleep :892-919`, `poffWakeGate :1056-1083`)

**Dormir:** el reset del GT911 (GPIO 3, `PIN_TP_RST`) se deja en **alto** con `gpio_hold_en` (en el P4 no existe
`gpio_deep_sleep_hold_en`; `SOC_GPIO_SUPPORT_HOLD_SINGLE_IO_IN_DSLP=1`, válido en silicio ≥ v3.0) para que el táctil siga
escaneando. Fuente de despertar:
* `POFF_WAKE_GPIO` (INT del GT911) en 0..15 → `esp_sleep_enable_ext1_wakeup_io(1<<pin, nivel)` (ext0 no existe en P4);
* **valor actual `-1`** (INT no cableado / pin no confirmado) → **temporizador** cada `POFF_WAKE_POLL_MS = 400 ms`
  (modo degradado) (`Types.h:331-356`).

**Despertar** (`poffWakeGate`, llamado en `setup()` **antes** de encender el panel, tras `Serial.begin` y 60 ms): solo
si `esp_reset_reason() == ESP_RST_DEEPSLEEP`. Suelta el *hold* (dejando el pin en alto antes), inicializa I2C+GT911
(`flexTouchInit`); si el táctil no responde → arranca normal. Durante hasta **4200 ms** (`POFF_WAKE_GATE_MS`) sondea cada
10 ms: con ≥1 dedo (frame de <120 ms) cuenta tiempo sostenido; **3000 ms** seguidos (`POFF_WAKE_HOLD_MS`) → arranque
completo; si el dedo se levanta, el contador vuelve a 0; agotada la ventana → vuelve a dormir. El usuario nunca ve un
destello (panel y backlight siguen apagados).

> **Bug de consumo a no copiar:** sin dedo, el filtro **espera la ventana completa de 4,2 s** antes de volver a dormir.
> Con el despertar por temporizador cada 0,4 s, el chip pasa ~91 % del tiempo despierto: el "apagado" consume casi como
> encendido. En IDF: salir en cuanto no haya dedo en las primeras lecturas (p. ej. 2-3 sondeos sin contacto) y, mejor,
> confirmar y cablear el INT del GT911 para ext1 (ver `docs/ESP_IDF_HARDWARE_REQUIREMENTS.md`).

Tras el arranque (`.ino:535-542`): si viene de deep sleep lee **y borra** `flexos/cleanoff` → `gBootCleanOff`
(hoy solo se registra en el log `"[PWR] arranque desde deep sleep (apagado limpio: %s)"`; no cambia el comportamiento)
y la banda forense **no** se muestra (DEEPSLEEP cuenta como arranque normal).

### 10.7 Persistencia (`poffSaveCleanFlag`, `Power.h:923-932`)

Justo antes de dormir: `flexos/cleanoff = true`, `flexos/bright = gBright`, `flexAudioSavePrefs()` (NVS `flexaudio`:
`vol` u8 defecto 70, `mute` bool), `clkSaveNvs()` (NVS `flexos_time`: `epoch` u64, `lastsync` u64). **No** se llama a
`sessFlushNow()`: un guardado de sesión diferido pendiente (<1,2 s) se pierde al apagar (ver §14.8).

### 10.8 Casos límite y bugs

* **Cancelar siempre vuelve a Inicio** (comentario: "solo se abre desde `ST_HOME`"), pero el Panel rápido es global y
  puede abrirse **sobre una app**: la app no se suspende ni se cierra (queda `ALIFE_RUNNING` con `gState = ST_HOME`). En
  IDF: cancelar debe volver a la pantalla de origen (pila de dueños).
* El retorno del pomo depende del ritmo del bucle (no del tiempo): en IDF animarlo con `lv_anim` (≈150 ms ease-out).
* Sin PSRAM para `poffBand` el pomo no se anima (la pantalla sigue usable con Cancelar).
* `bright` se reescribe al apagar aunque no haya cambiado.

### 10.9 Notas de migración

* `flex_power`: `poweroff_request()` → pantalla de confirmación (LVGL: `lv_slider` personalizado o `lv_obj` arrastrable
  con estela por `lv_obj_set_style_bg_opa` proporcional) → verificación opcional (`flex_security`) → animación
  (`lv_anim` sobre la opacidad de un velo negro y del texto) → fundido backlight → DCS SLPIN → guardar prefs
  **sincrónicamente** (`flex_cfg_flush(timeout)` del escritor único de `flex_storage`) **y vaciar sesiones pendientes** →
  `gpio_hold_en(3)` → `esp_sleep_enable_ext1_wakeup_io` o temporizador → `esp_deep_sleep_start()`.
* El filtro de 3 s es código de **arranque temprano** (antes de LVGL y del panel): portarlo a `app_main` tras
  inicializar solo I2C + GT911. Lógica pura extraíble: "¿sostenido N ms?" sobre una secuencia de (ms, dedos) → prueba
  host (sostenido 3 s → arranca; soltar a 2,9 s y volver → reinicia cuenta; sin dedo → duerme **pronto**).
* Lógica pura del deslizador: `knob = clamp(x − grab − 46, 0, 304)`, completado si `knob·100/304 ≥ 92`.

---

## 11. Modo seguro

### 11.1 Detección en el arranque (`safeBootEval`, `Session.h:165-187`; `safeStableTick`, `:192-200`)

* Se llama en `setup()` tras `flexTouchInit()` y antes de cualquier tarea pesada.
* **Reinicio anormal** = `ESP_RST_PANIC`, `ESP_RST_INT_WDT`, `ESP_RST_TASK_WDT`, `ESP_RST_WDT`, `ESP_RST_BROWNOUT`.
  **No** cuentan `POWERON`, `SW` (reinicio voluntario y el de OTA) ni `DEEPSLEEP`.
* NVS **`flexsafe`**: `fails` (int, 0..250, contador de anormales **consecutivos**), `cause` (int, `esp_reset_reason_t`
  que disparó la cadena). En un anormal: `fails++`, `cause = motivo`, se guarda. En uno normal se conserva la causa
  guardada.
* **Modo seguro** si `fails ≥ 3` (`SAFE_FAIL_MAX`).
* Arranque **estable**: a los **60 s** (`SAFE_STABLE_MS`) sin reiniciar, si no es Modo seguro y `fails > 0` → `fails = 0`
  (una sola escritura) y log `"[SAFE] arranque estable: contador de reinicios anormales a cero"`.
* Log de arranque: `"[SAFE] motivo=%d anormal=%s fallos=%u modo_seguro=%s"`.

### 11.2 Pantalla de Modo seguro (`ST_SAFE`, `safeRender`, `Recovery.h:634-662`)

Se entra **directamente desde `setup()`** (sin splash ni bloqueo; `.ino:570`) y desde la píldora "Modo seguro" del
escritorio [01b §3.1] (`HomeCfg.h:1240`). Respeta tema claro/oscuro (fondo `PAGE_BG`=`TH_PAGE`, tarjeta
`SET_CARD_BG`=`TH_SURF`, textos `TH_TXT`/`TH_TXT2`/`TH_MUTE`).

| Elemento | Posición | Texto / estilo |
|---|---|---|
| Título | tamaño 3 centrado y=44 | `"Modo seguro"` color #E2A046 |
| Subtítulo | tamaño 1 centrado y=82 | `"FlexOS ha arrancado con lo minimo"` *(sic)* `TH_TXT2` |
| Tarjeta de causa | (20,112) 440×148 r16 `TH_SURF` | |
| · | tamaño 2 en (36,126) | `"Motivo del ultimo fallo"` *(sic)* `TH_TXT` |
| · causa | tamaño 2 en (36,154), recorte x=448 | #D6684A: `"Fallo del sistema (crash)"` (PANIC), `"Watchdog de tarea (TASK_WDT)"`, `"Watchdog de interrupcion (INT_WDT)"` *(sic)*, `"Watchdog del chip"` (WDT), `"Caida de tension (brownout)"` *(sic)*, `"Reinicio inesperado"` (otro) (`safeCauseText`, `Session.h:152-161`) |
| · | tamaño 1 en (36,184) | `"Reinicios anormales seguidos: %u"` `TH_TXT2` |
| · | tamaño 1 en (36,206) y (36,226) | `"Apps no esenciales y personalizacion"` *(sic)* / `"desactivadas mientras dure este modo."` |
| 7 filas | y = 292 + i·66 (292, 358, 424, 490, 556, 622, 688); tarjeta (20, y) 440×58 r14 | título tamaño 2 en (36, y+10); subtítulo tamaño 1 en (36, y+34); color `TH_TXT`/`TH_TXT2`, desactivada `TH_MUTE`; la última en #D6544A |
| Pie | tamaño 1 centrado y=760 | `"Un arranque estable limpia el contador solo"` `TH_MUTE` |

Filas (`SAFE_ROW_T`, `safeRowVal`, `Recovery.h:607-633`):

| # | Título | Subtítulo | Acción al tocar (`safeTick :670-690`) |
|---|---|---|---|
| 0 | `"Reiniciar normalmente"` | `"Sale del Modo seguro y arranca normal"` | `safeExitAndReboot()`: `fails = 0`, guarda, log `"[SAFE] saliendo del Modo seguro -> reinicio normal"`, 40 ms, `esp_restart()` |
| 1 | `"Apps de terceros"` | sin apps: `"Ninguna instalada: nada que desactivar"` (fila **inerte**, sin repintar); con apps: `"Desactivar las %d instaladas"` | `flexos/apps3rd = 0` y repinta |
| 2 | `"Limpiar caches seguras"` *(sic)* | `"Vistas previas y datos temporales"` | `safeClearCaches()` (§11.4) y repinta |
| 3 | `"Ajustes"` | `"Disponible en Modo seguro"` | `renderHome(); enterApp(IC_AJUSTES)` |
| 4 | `"Explorador de archivos"` | `"Disponible en Modo seguro"` | `renderHome(); enterApp(IC_ALMACEN)` |
| 5 | `"Ir al escritorio (limitado)"` | `"Toca \"Modo seguro\" en el escritorio para volver"` | `gHomeDirty = true; enterHome(); touchDropAll()` |
| 6 | `"Restablecer datos de fabrica"` *(sic)* | `"Borra todo el contenido del dispositivo"` | `frFromSafe = true; frEnterWizard()` (§13) |

Zona táctil de cada fila: y ∈ [y, y+58], todo el ancho (solo `T.tap`).

### 11.3 Restricciones y aviso de app no disponible

* **Lista blanca de apps** (`safeAppAllowed`, `Recovery.h:549-552`): `IC_AJUSTES`, `IC_ALMACEN`, `IC_RELOJ`, `IC_CALC`.
  Cualquier otra → `safeDenyApp(id)` desde `enterApp` (`Core.h:691`).
* `safeDenyApp` (`:568-578`): sobre el escritorio, banda y=300..375: tarjeta (28, 308) 424×60 r16 #181A24 α240,
  `"No disponible en Modo seguro"` tamaño 2 centrado y=318 #F0F4FC, nombre de la app tamaño 1 centrado y=342 #AAB2C4.
  Se retira a los **1800 ms** restaurando la banda desde el escritorio (`safeToastTick`, solo en `ST_HOME`).
* No arrancan en Modo seguro (`.ino`): radio Wi-Fi/C6, navegador, OTA, paquetes, biblioteca de medios, Flex Store,
  Flex Account, Flex Storage, Flex Cloud, modo avión restaurado, clima, Flex Phone (y por tanto No molestar), detección de
  caídas y protección contra robo; en `loop` no corren reconexión Wi-Fi, NTP, clima ni Flex Phone; sin avisos de memoria
  (`memAlertTick` sale), sin banner, sin destello de iconos (`Widgets.h:605`).
* Galería/Multimedia/Música muestran `"Modo seguro"` como motivo de biblioteca no disponible; Galería añade
  `"La biblioteca no se abre hasta reiniciar con normalidad"`.

### 11.4 Limpiar cachés seguras (`safeClearCaches`, `Recovery.h:592-602`)

Borra el contenido de `/System/Cache` (cuenta de archivos), suelta el fondo desenfocado `blurBg`, libera el visor de
medios si nadie lo usa (`vwRelease`), la escena de la cámara y el trazo de Paint si Paint está cerrado; marca
`qsDirty`/`gHomeDirty`. Log `"[SAFE] caches limpiadas (%d archivos)"`. **No** toca notas, dibujos, ajustes ni
credenciales. No hay mensaje en pantalla del resultado.

### 11.5 Persistencia

`flexsafe/fails` (int), `flexsafe/cause` (int), `flexos/apps3rd` (int, máscara heredada de "apps de terceros"; siempre 0:
este firmware no la rellena — la fila sale inerte).

### 11.6 Casos límite, bugs y **riesgo de seguridad**

* **Riesgo grave:** el Modo seguro entra **sin pantalla de bloqueo** y ofrece "Ir al escritorio", Ajustes y el
  Explorador. Combinado con que cambiar la clave no pide la actual [01a §5.15], **provocar 3 reinicios anormales
  (p. ej. *brownouts*) da acceso al aparato sin PIN**. En IDF: el Modo seguro debe pasar por la verificación de la clave
  (si existe) antes de cualquier fila salvo "Reiniciar normalmente" y "Restablecer" (que ya la pide).
* En Modo seguro el contador **no se limpia solo** (`safeStableTick` sale si `gSafeMode`): apagar y encender (POWERON)
  vuelve a entrar en Modo seguro indefinidamente; el pie "Un arranque estable limpia el contador solo" engaña. Solo
  "Reiniciar normalmente" o un restablecimiento lo limpian.
* El comentario promete "sin fondo ni widgets del usuario", pero el escritorio en Modo seguro **sí** carga el fondo y los
  widgets (`homeCfgLoad` corre siempre); solo añade la píldora y quita el destello.
* Kiosco + Modo seguro: el arranque ignora el kiosco (va a `ST_SAFE`), "Ir al escritorio" da un escritorio con
  `kioskOn` aún activo (los vetos del kiosco siguen, ver §12.4).
* Un `cause` no anormal (p. ej. `ESP_RST_SW`) no puede llegar aquí salvo NVS corrupta → `"Reinicio inesperado"`.

### 11.7 Notas de migración

* **Lógica pura** (`flex_portable/safe_boot.c`): `safe_eval(reason, saved_fails, saved_cause) → {fails, cause,
  safe}` y `safe_stable(ms)`. Pruebas host: 3 PANIC seguidos → seguro; PANIC, POWERON, PANIC → fails=2 (POWERON no
  limpia); 60 s estables → 0; OTA (`ESP_RST_SW`) no suma; tope 250.
* IDF: `esp_reset_reason()` idéntico; además `flex_system` ya lee la partición de *coredump* (`crash_present`,
  `crash_reason`): mostrar esa causa real en la tarjeta.
* El arranque reducido se expresa como **perfil de servicios** en `app_main` (no iniciar `flex_wifi`, `flex_cloud`,
  `flex_account`, `flex_ota`, `flex_media`… con `safe=true`).
* UI LVGL: pantalla propia con lista de 7 `lv_obj` tarjeta (o `lv_list`), estado `LV_STATE_DISABLED` para la fila 1
  sin apps; toast de app negada como `lv_obj` temporal con `lv_timer` de 1800 ms.

---

## 12. Modo Kiosco — detalles de ejecución (complemento de [01a §5.14])

[01a §5.14] documenta propósito, pantalla de zona excluida (`ST_KIOSKSET`), candado, salida y NVS. Aquí, lo que falta.

### 12.1 Arranque y persistencia

* `cfgLoad` lee `kioskon`, `kioskapp`, `kioskx/y/w/h`; si la app está fuera de 0..18 o **no hay clave**
  (`gLockType == 0`) → se desactiva en RAM (no se reescribe NVS) (`Prefs.h:280-287`).
* Al terminar el splash con kiosco activo → `renderHome(); enterApp(kioskApp); kioskShowBadge()` **sin pasar por el
  bloqueo** (`Home.h:310-314`): reiniciar no es vía de escape (ni tampoco exige PIN para usar la app clavada).
* `kioskStart` (`Lock.h:460-468`): guarda en NVS **antes** de abrir la app con su animación normal y publica la banda del
  candado.
* `kioskExitNow` (`Lock.h:469-479`): borra estado y NVS (`kioskon=false`, `kioskapp=-1`, zona 0), fuerza vertical,
  `renderHome(); enterHome()`, rearma el auto-bloqueo.

### 12.2 Candado estampado (`kioskStampBadge`, `Lock.h:418-457`)

Se dibuja **dentro de `flxFlush`** antes de cada transferencia (nunca sale una banda sin él), solo en `ST_APP`, en
coordenadas **físicas** aunque la app dibuje en horizontal. Recuadro de estampado 444..479 × 40..71. Dibujo: pastilla
(448,44) 24×24 r7 `TH_PAGE` α200; arco: círculo r5 en (460,53) `TH_TXT` con hueco r3 `TH_PAGE`; cuerpo (453,55) 14×9
r2 `TH_TXT`. Al entrar en kiosco se publica esa banda una vez (`kioskShowBadge`).

### 12.3 Gesto de salida (`kioskTick`, `Lock.h:500-515`)

En cada vuelta (incluso con Centro/cortina, ver [01b §1.3]): si no hay dedo → rearma (`kioskExitFired=false`; el rearme
va antes del filtro de estado, para poder reintentar tras cancelar la verificación). Solo en `ST_APP`: dedo apoyado,
apoyo dentro de la zona del candado ampliada 14 px (x 434..486, y 30..82), **>1000 ms** sin moverse más de 12 px →
dispara **una vez por contacto** `lsuStartVerifyFor(LSU_AFTER_KIOSKOUT, kioskApp)`. Acierto → `kioskExitNow()`;
cancelar → vuelve **a la app clavada** con el candado (`Power.h:113-116`), nunca al escritorio.

### 12.4 Tabla completa de vetos (todo lo que el kiosco desactiva)

| Qué | Dónde | Efecto |
|---|---|---|
| Atrás / Inicio / Recientes | `Core.h:552, 563, 571` | no hacen nada |
| `appClose` (cualquier salida de la app) | `Core.h:646` | no hace nada |
| `enterApp(id ≠ kioskApp)` | `Core.h:688` | no hace nada (una app no puede abrir otra) |
| Barra de gestos iOS | `Core.h:1068` | devuelve false: la app sigue recibiendo el toque, sin escape |
| Barra de navegación de 3 botones | `AppFramework.h:1001` | invisible |
| Barra transitoria de pantalla completa | `AppFramework.h:998, :1279` | no existe |
| Panel rápido | `QuickPanelEdit.h:492` | no se abre |
| Centro de notificaciones | `Overlay.h:902` | no se abre |
| Cápsula del cronómetro | `AppChrono.h:320` | oculta |
| Caja de aplicaciones | `AppDrawer.h:1120` | no se abre |
| Recientes (`activarMultitarea`) | `AppSwitcher.h:468` | no se abre |
| Crear/cambiar clave (`lsuEnter`) | `Power.h:420` | no se abre |
| Bloqueo automático | `Lock.h:361` | desactivado |
| Gesto de suspensión | `Touch.h:277` | vetado |
| Zona excluida | `Touch.h:352`, `Lock.h:493-498` | toques dentro → "sin dato" **solo en `ST_APP`**; el candado siempre gana |

**No** vetados: banner (el toque en uno del teléfono intenta `appClose`+`enterApp(IC_FLEXPHONE)` y ambos se ignoran:
solo se cierra el banner), OTA (su overlay puede salir), aviso de caída, aprobación de teléfono.

### 12.5 Casos límite

* Pantallas `ST_*` que la propia app clavada abra (p. ej. si es Ajustes: Wi-Fi, Conectividad, Archivos, Ajustes del
  teclado): **sin candado, sin gesto de salida y sin zona excluida** (todo exige `ST_APP`); su navegación interna
  devuelve a la app.
* Tras `kioskExitNow` la app clavada no pasa por `appSuspend` (`enterHome` no suspende): queda en `ALIFE_RUNNING` con
  `gState = ST_HOME` (comprobar en IDF que el ciclo de vida queda coherente: suspenderla o cerrarla).
* Kiosco + Modo seguro: ver §11.6.

### 12.6 Notas de migración

* Política central `flex_shell_policy` con `kiosk_active()` consultada por **todas** las salidas (en IDF es fácil olvidar
  una: usar la tabla §12.4 como lista de pruebas).
* El filtro de zona ya está en `flex_touch_arb` (`kiosk_filter`, `kx..kh`, `flex_arb_kiosk_in_exit`); la UI debe poner
  `kiosk_filter = kiosk_on && pantalla == APP`.
* Candado como `lv_obj` en `lv_layer_sys()` (por encima de la app, debajo de nada), pulsación larga de 1000 ms con
  `LV_EVENT_LONG_PRESSED` configurando `long_press_time = 1000` **solo para ese objeto** (o temporizador propio, ya que
  `long_press_time` es por indev).

---

## 13. Restablecimiento de datos de fábrica

### 13.1 Entradas

* Ajustes → General → fila `"Restablecer"` / `"Opciones de fabrica"` *(sic)* (`AppSettings.h:302, :644`) →
  `frEnterWizard()`.
* Modo seguro → fila 6 (`frFromSafe = true`).
* Reanudación automática tras corte (`frResumeAfterBoot`, §13.6).

`frEnterWizard` (`Recovery.h:374-394`): si hay **OTA en curso** (`flexOtaBusy`) muestra un aviso y no entra (§13.3.6);
si no, fuerza vertical, `gState = ST_FACTORY`, vista `FRV_INTRO`. Mientras `gState == ST_FACTORY` o hay borrado
pendiente, `loop` solo ejecuta `clkUpdate(); frTick()` (nadie más dibuja ni recibe toques) [01b §1.3 paso 3].

### 13.2 Máquina de vistas (`FRV_*`, `Recovery.h:48`)

```
FRV_INTRO --Cancelar--> (Ajustes | Modo seguro)
    |--Continuar, con clave--> verificación LSU_AFTER_FACTORY --ok--> FRV_SLIDE ; --cancelar--> Ajustes
    |--Continuar, sin clave--> FRV_TYPE --"RESTABLECER" + Continuar--> FRV_SLIDE ; --flecha--> Ajustes/Seguro
FRV_SLIDE --deslizar ≥92 %--> frBeginWipe → FRV_RUN ; --Cancelar--> Ajustes/Seguro
FRV_RUN --etapa a etapa--> (FR_ST_DONE: "Listo" + reinicio) | FRV_FAIL
FRV_FAIL --Reintentar--> FRV_RUN ; --Reiniciar--> esp_restart (y vuelve a FRV_FAIL si el marcador sigue)
```

Cancelar (`frCancelToSettings`, `:365-373`): si se venía del Modo seguro → `safeEnter()`; si no → `gState = ST_APP`,
`gAppId = IC_AJUSTES` (en ejecución), `settingsRender()`. Nunca se ha modificado ningún dato.

### 13.3 Pantallas (colores propios del asistente, `Recovery.h:73-76`)

`frBg` oscuro #10121A / claro #F6F8FC; `frHi` #F0F2F8 / #14161E; `frLo` #A0A6B6 / #6E7484; `frCard` #1E222E / #FFFFFF.
Rojo de acción #C83C3C; viñetas #DC5050; verde de palabra correcta #5ABE82; pomo #D2423C; advertencia #DC6E5A; etapa
fallida #E6785A. Todo a pantalla completa, sin barra de estado ni navegación.

#### 13.3.1 Aviso (`FRV_INTRO`, `frDrawIntro :86-120`)

| Elemento | Posición | Texto |
|---|---|---|
| Título (2 líneas) | tamaño 3 centrado y=44 y y=76 | `"Restablecer datos"` / `"de fabrica"` *(sic)* `frHi` |
| Tarjeta 1 | (20,124) 440×300 r16 `frCard` | |
| · | tamaño 2 en (36,140) | `"Se eliminaran de este dispositivo:"` *(sic)* `frHi` |
| · 7 viñetas | y = 170 + i·22: círculo r3 en (42, y+8) #DC5050; texto tamaño 1 en (54, y) `frLo` | `"Cuentas locales y sus tokens"`, `"Redes Wi-Fi guardadas y sus claves"`, `"PIN o contrasena de bloqueo"` *(sic)*, `"Ajustes, fondo y personalizacion"` *(sic)*, `"Notas, dibujos y archivos del usuario"`, `"Apps instaladas y sus datos"`, `"Sesiones abiertas e historial"` |
| Tarjeta 2 | (20,440) 440×92 r16 | `"NO se elimina:"` tamaño 2 en (36,452); tamaño 1 en (36,480) `"El firmware instalado sigue siendo el mismo."` y (36,500) `"No se vuelve a una version anterior por OTA."` *(sic)* |
| Botón Cancelar | (28,704) 202×58 r29 `frCard` | `"Cancelar"` tamaño 2 `frHi` centrado y=723 |
| Botón Continuar | (250,704) 202×58 r29 #C83C3C | `"Continuar"` tamaño 2 blanco |

Toque: solo `T.tap` en y ∈ [704,762]: x ∈ [28,230] → cancelar; x ∈ [250,452] → con clave `lsuStartVerifyFor(
LSU_AFTER_FACTORY, -1)` (mismo verificador, mismas penalizaciones, [01a §5]); sin clave → `FRV_TYPE` (prepara el
teclado del bloqueo en español, mayúsculas, sin barra).

#### 13.3.2 Escribir RESTABLECER (`FRV_TYPE`, `frDrawType :124-145`, solo **sin** clave configurada)

| Elemento | Posición | Texto / estilo |
|---|---|---|
| Flecha atrás | trazos (30,26)→(18,18)→(30,10), grosor 2,4 `frHi`; zona táctil x<48, y<48 | |
| Título | tamaño 3 centrado y=46 | `"Confirma el borrado"` |
| Explicación | tamaño 1 centrado y=84 y y=104 `frLo` | `"Este equipo no tiene bloqueo configurado."` / `"Escribe RESTABLECER para continuar."` |
| Campo | (32,150) 416×56 r12 `frCard`; texto tamaño 3 centrado y=168 | lo escrito o `"..."`; #5ABE82 si coincide, `frHi` si no |
| Botón | (32,230) 416×58 r29: #C83C3C si coincide, `frCard` si no | `"Continuar"` tamaño 2 blanco / `frLo` |
| Teclado | el del bloqueo del sistema (`lsuDrawKb`) en su posición habitual | (documento del teclado) |

Reglas: solo letras A-Z (se convierten a mayúscula), máx. 19 caracteres; tecla de función 0 = mayúsculas, 4 = borrar;
"Continuar" solo actúa si el texto es exactamente `RESTABLECER` (`FR_WORD`). Toque en el botón (y 230..288) con la
palabra correcta → `frAfterVerify()`.

#### 13.3.3 Último paso (`FRV_SLIDE`, `frDrawSlide :158-178`)

| Elemento | Posición | Texto / estilo |
|---|---|---|
| Título | tamaño 3 centrado y=120 | `"Ultimo paso"` *(sic)* |
| Tarjeta | (24,180) 432×150 r16 `frCard`; tamaño 2 centrado y=202, 230, 258 | `"Se borrara todo el contenido"` / `"y la configuracion de este"` / `"dispositivo."` *(sic)* |
| Advertencia | tamaño 1 centrado y=292 #DC6E5A | `"Esta accion no se puede deshacer."` *(sic)* |
| Pista | (40,600) 400×72 r36 `frCard`; rótulo tamaño 2 centrado y=626 `frLo` | `"Desliza para restablecer"` |
| Pomo | círculo d=60 (r30) centrado en (76 + knob, 636) #D2423C con chevron ">" blanco (trazo 2,6) | knob ∈ 0..328 (`FRS_RUN = 400−12−60`) |
| Cancelar | (28,704) 424×58 r29 `frCard` | `"Cancelar"` |

Agarre: y ∈ [600,672], x ∈ [kx−24, kx+84] (kx = 46+knob); arrastre 1:1. Soltar con `knob·100/328 ≥ 92` (≥302) →
`frBeginWipe()`; si no → **vuelve a 0 de golpe** (sin animación). "Nunca se completa con un toque".

#### 13.3.4 Progreso (`FRV_RUN`, `frDrawRun :197-211`)

`"Restableciendo"` tamaño 3 centrado y=240 `frHi`; `"Paso %d de %d"` tamaño 2 y=300 `frLo`; nombre de la etapa
tamaño 2 y=336 `frHi`; barra de **7 segmentos** de 49×10 r5 en x = 48 + i·55, y=396 (#C83C3C hasta la etapa actual,
`frCard` el resto); `"No apagues el dispositivo"` tamaño 1 y=440 `frLo`. Sin porcentajes. Cada etapa se ejecuta en
una vuelta de `loop` y su nombre queda visible **≥260 ms** (`FR_STEP_MIN_MS`).

Nombres (`FR_STAGE_TXT`): `"Preparando el dispositivo"`, `"Borrando credenciales y tokens"`,
`"Cerrando la sesion remota"` *(sic)*, `"Borrando datos de aplicaciones"`, `"Borrando archivos del usuario"`,
`"Borrando ajustes del sistema"`, `"Restaurando valores de fabrica"` *(sic)*.

Final: `"Listo"` tamaño 3 centrado y=380 y `"Reiniciando..."` tamaño 2 y=420, luego `esp_restart()`.

#### 13.3.5 Fallo (`FRV_FAIL`, `frDrawFail :212-227`)

`"Restablecimiento"` / `"incompleto"` tamaño 3 y=200/236; `"Fallo en:"` tamaño 2 y=300 `frLo`; etapa tamaño 2 y=330
#E6785A; `"El dispositivo se queda en recuperacion:"` *(sic)* y `"no arranca con datos a medias."` tamaño 1 y=380/400.
Botones (28,628) 424×58 #C83C3C `"Reintentar"` y (28,704) 424×58 `frCard` `"Reiniciar"`.
* Reintentar: si falló el **primer marcador** (nada borrado, `!gFrPending`) → vuelve a armar todo (`frBeginWipe`);
  si no → reanuda la etapa anotada en `err` (las etapas son idempotentes) tras confirmarlo en NVS.
* Reiniciar → `esp_restart()`; con el marcador en `FAIL` el arranque vuelve a esta pantalla.

#### 13.3.6 Aviso de OTA en curso

Pantalla completa `frBg`: `"Actualizacion en curso"` *(sic)* tamaño 2 centrado y=380 `frHi`; `"Espera a que termine
para restablecer"` tamaño 1 y=412 `frLo`. Se retira sola a los **2200 ms** o con un toque → vuelve a Ajustes
(`frTick :431-434`, usa `frKnob = -1` como marca).

### 13.4 Etapas y qué se borra exactamente (`frStageRun`, `Recovery.h:234-327`)

| Etapa (`FR_ST_*`, valor) | Acción exacta |
|---|---|
| `ARMED` (1) | Anula guardados diferidos (`gSessDirtyApp = -1`); cierra navegador (`flexBrowserExit`), tienda (`storeExit`), cancela Flex Account (`flexAccountCancel`); llama a `close()` de **todas** las apps no cerradas y las marca cerradas; vacía Recientes. |
| `TOKENS` (2) | Borra la red Wi-Fi guardada (NVS `flexos_wifi` entero, `wifiCredsForget`); borra la cuenta local (NVS `flexacct` entero + rota el id de instalación, `flexAccountForgetLocal`); si la radio está encendida: `WiFi.disconnect(true,true)` + `WIFI_OFF`; borra `flexos/lockpin`, `lockpass`, `locktype`, `lockfails`; `gLockType = 0`. |
| `REMOTE` (3) | Nada (no existe API de revocación): log `"[RESET] sin servicio de cuenta remota: no hay sesion que revocar"`; siempre éxito. |
| `APPDATA` (4) | Vacía `/System/Sessions` y `/System/Cache`; marca todas las sesiones como no cargadas/no pendientes. |
| `FILES` (5) | **Formatea LittleFS** (`flexFsFactoryErase`: desmonta, formatea, monta, recrea `/Paint`, `/Notas`, `/System`, `/Documentos`, `/Papelera`). Error → `"No se pudo formatear LittleFS"` / `"LittleFS no monto tras formatear"`. Sin FS montado → éxito. |
| `NVS` (6) | `clear()` de los namespaces **conocidos**: `flexos`, `flexos_wifi`, `flexos_time`, `flexota`, `flexqs`, `flexacct`, `fxvault` (función retirada), `flexsafe`. **Nunca** borrado global (se perdería el marcador `flexreset`). |
| `DEFAULTS` (7) | Valores de fábrica en RAM (OOBE pendiente, idioma 0, 12 h, plano, oscuro, iconos planos, brillo 80, sin clave, auto-bloqueo 60 s, sin apagado seguro, sin candados, sin kiosco, widgets de bloqueo = reloj, navegación botones, transición zoom, nombre `"FlexOS Ultra"`, atajos del teclado de fábrica, orden de iconos 0..11, Notas/Paint a su vista inicial). |
| `DONE` (8) | Se escribe el marcador "terminado", `"Listo"`, `esp_restart()`. |
| `FAIL` (9) | Marcador de fallo con `err` = etapa que falló. |

### 13.5 Qué **NO** se borra (namespaces supervivientes) — riesgo de privacidad

Quedan en NVS tras un restablecimiento: **`flexphone`** (vínculo con el teléfono: `bondpeer`, `bondname`, `bondkey`,
`selfid`, `autolink`, `dnd`), **`flexstor`** (teléfono emparejado de Flex Storage), `flexcare` (Device Care),
`flextheft` (protección contra robo), `flexaudio` (volumen), `flexwx` (ubicaciones del clima), `flexos_br`
(preferencias del navegador). LittleFS sí queda vacío. Esto contradice el texto de la pantalla ("Cuentas locales y sus
tokens", "Sesiones abiertas e historial"): **un aparato restablecido sigue emparejado con el teléfono anterior**.
En IDF: borrar **todos** los namespaces del sistema salvo `flexreset` (lista blanca inversa) o usar
`flex_storage_nvs_erase_confirmed` preservando solo el marcador.

### 13.6 Marcador transaccional (`Session.h:212-298`)

NVS **`flexreset`**: `pending` (bool), `ver` (int, `FR_FMT_VER = 1`), `stage` (int), `err` (int).
* `frSaveState()` escribe las cuatro claves **y las relee para verificarlas**; si algo no cuadra devuelve false y el
  motor se detiene (nunca confía solo en RAM).
* `frBeginWipe()` (`Recovery.h:404-415`) escribe `pending=true, stage=ARMED` **antes de tocar un solo dato**; si falla →
  `FRV_FAIL` con nada borrado.
* `frAdvance()` (`:328-361`) ejecuta la etapa; éxito → `stage++` y guarda **antes** de pasar a la siguiente; fallo →
  `stage=FAIL, err=etapa`.
* `frLoadState()` (en `setup()`, antes de casi todo): `pending && ver==1 && stage==DONE` → `gFrConfirmPending`
  (arranque normal; el marcador se borra **al llegar al OOBE** como aparato nuevo, `frClearMarker` desde
  `enterOobeLang`, `Home.h:352`, log `"[RESET] arranque limpio confirmado: marcador de recuperacion borrado"`);
  `pending && ver==1 && 1 ≤ stage ≤ FAIL` → **reanuda**; `pending` con otra versión → se descarta el marcador.
* Con borrado pendiente, `setup()` no arranca radio, navegador, OTA, paquetes, biblioteca, ni migra la clave: aplica el
  brillo y llama a `frResumeAfterBoot()` (log `"[RESET] borrado interrumpido: se retoma en la etapa %u"`).

### 13.7 Casos límite y bugs

* `FR_ST_TOKENS` apaga la radio con `WiFi.disconnect`/`WiFi.mode` **desde loopTask**, en contra de la regla del propio
  proyecto ("esp-hosted nunca se toca desde loopTask", `Network.h:243-250`): riesgo de bloqueo del watchdog. En IDF:
  pedirlo a `flex_wifi` y esperar su evento.
* §9.6: suspensión dentro del asistente bloquea el táctil.
* Namespaces supervivientes (§13.5).
* `FRV_SLIDE` no anima el retorno; `FRV_TYPE` no admite tildes (correcto) ni pegar.
* El aviso de OTA reutiliza `frKnob = -1` como estado (frágil).

### 13.8 Notas de migración

* **Lógica pura**: máquina de etapas + marcador (con un `kv` simulado) → `flex_portable/factory_reset.c`; pruebas host:
  corte tras cada etapa → reanuda en la siguiente; fallo de escritura del marcador → no borra; versión desconocida →
  descarta; `DONE` → no reanuda y espera confirmación del OOBE.
* La ejecución real (formatear, borrar NVS) en **una tarea de trabajo** de `flex_storage` (ya existen
  `flex_storage_fs_format_confirmed(token)` y `flex_storage_nvs_erase_confirmed(token)`), con la UI LVGL solo
  mostrando el progreso por eventos `FLEX_EV_STORAGE`; el TWDT lo alimenta la tarea.
* UI: 6 pantallas LVGL; los deslizadores de confirmación (apagado y restablecimiento) comparten un componente
  `flex_slide_confirm` con umbral 92 %.

---

## 14. Sesiones de app (guardar y restaurar el estado de UI)

### 14.1 Qué es

Cada app con estado de interfaz que merece sobrevivir a un reinicio (vista, scroll, selección, archivo abierto) lo
guarda en un archivo pequeño versionado en LittleFS. **Nunca el documento** (las notas y dibujos tienen sus propios
archivos). El estado lógico de una app suspendida vive en RAM; la sesión en disco es para el reinicio.

### 14.2 Formato (`Session.h:55-102`, `Types.h:158`)

* Directorio `/System/Sessions` (`FS_DIR_SESS`), creado en `setup()` junto a `/System/Cache` (`FS_DIR_CACHE`).
* Cabecera `SessHdr` (16 B, little-endian): `uint32 magic = 0x584C4631` (bytes en disco `31 46 4C 58` = "1FLX"),
  `uint16 ver`, `uint16 app` (id `IC_*`), `uint32 len`, `uint32 crc` (CRC-32 IEEE reflejado, polinomio `0xEDB88320`,
  init `~0`, xor final: el estándar de zlib; implementación sin tabla `crc32u`).
* Carga útil ≤ `1024 − 16 = 1008` B (`SESS_IO_MAX`, buffer único estático `gSessIo`).
* `sessRead(path, wantVer, app, out, max)`: 0 si no hay FS, tamaño <16 o >1024, lectura corta, magic/versión/app
  distintos, `len > max` o `16 + len != tamaño`, o **CRC incorrecto**. Una sesión inválida **no se repara**: se ignora y
  la app abre en estado seguro.

### 14.3 Escritura atómica (`sessWrite` → `flexFsWriteBinAtomic`, `FlexOS_FS.cpp:636-661`)

1. Borra `<ruta>.tmp`, escribe `<ruta>.tmp`, comprueba **solo el tamaño** (no relee el CRC, aunque el comentario de
   `Session.h:41-48` lo afirma).
2. Borra `<ruta>.bak`; si existía el archivo, lo renombra a `.bak`.
3. Renombra `.tmp` → ruta; si falla, restaura `.bak`.
4. Borra `.bak`.

> **Hueco:** no existe barrido de `.tmp` ni recuperación de `.bak` al arrancar (el comentario lo promete). Un corte
> entre los pasos 2 y 3 deja solo `.bak` y `.tmp` → la sesión se pierde (la app abre en estado seguro). En IDF: al
> montar, si falta `X` y existe `X.bak` → renombrar; borrar `*.tmp`.

### 14.4 Guardado diferido (`AppFramework.h:1346-1414`)

* Las apps llaman a `sessMarkDirty(id)` cuando cambian algo. Solo hay **una app "sucia" a la vez**: si otra lo estaba,
  se guarda **ya**. Marca `gSessNeedSave[id]`.
* `sessAutosaveTick()` (paso 4 del `loop`): no hace nada con restablecimiento pendiente ni con el **dedo apoyado**; vuelca
  tras **1200 ms** sin nuevas marcas (`SESS_IDLE_MS`) o, como tope, **30 s** desde la primera (`SESS_MAXWAIT_MS`).
* `appSaveSession(id)`: sin hook o sin cambios → éxito trivial (no reescribe una sesión idéntica: desgaste de flash);
  éxito → limpia la marca.
* `appSuspend` (`Core.h:494-517`) **arma** el volcado vencido para la **vuelta siguiente** (fuera de la animación de
  salida: con Paint eran >100 ms de tirón).
* `appTerminate(id, force)` (`AppFramework.h:1416-1438`) guarda **en el acto**; si falla y no es forzado, la app **no se
  cierra** (no se pierde trabajo); alimenta el TWDT antes y después de `close()` (el del navegador puede tardar hasta
  3 s); olvida la huella de memoria.

### 14.5 Restauración perezosa

`appLoadSessionOnce(id)` la primera vez que se abre cada app en el arranque (`enterApp`, y algunas apps al entrar:
Multimedia, Ajustes, Calculadora). `gSessLoaded[id]` evita releer. Al cerrar Multimedia se vuelve a marcar como no
cargada.

### 14.6 Sesiones por app

| App | Archivo | Ver | Contenido (struct, tamaño aprox.) | Validación al cargar | Ref. |
|---|---|---|---|---|---|
| Ajustes | `ajustes.bin` | 1 | `{int16 view, sel, scroll, listScroll}` (8 B) | `sel` fuera de 0..11 → se ignora entera; scroll <0 → 0 | `AppSettings.h:754-783` |
| Calculadora | `calc.bin` | 1 | `{double acc; char disp[24]; char op; u8 fresh, err, rsv}` (40 B) | `disp` vacío o `op` ∉ `+-x/` → se ignora | `AppsBasic.h:315-337` |
| Notas | `notas.bin` | 3 | `{u8 view, kbLayout, kbFlags, reserved; u16 cursor; i16 selA, selB; i32 editorScroll, listScroll; char path[96]}` (116 B); al guardar en el editor **también guarda la nota** | `view=1` solo si la ruta existe; `kbLayout<4` | `AppNotes.h:389-460` |
| Paint | `paint.bin` | 1 | `{u8 view, sizeIx; u16 color; i32 scroll; char path[96]}` (104 B); vacía el trazo pendiente antes | `view=1` solo si la ruta existe; `sizeIx<3` | `AppPaint.h:544-565` |
| Multimedia | `media.bin` | 2 | `{i32 filter; u32 resumeKey[8]; u32 resumeFrame[8]}` (68 B): pestaña (la de nube no se restaura) y posición de los 8 últimos vídeos | `filter` válido o 0 | `AppMultimedia.h:528-615` |
| Device Care | (propio) | — | `saveSess = dcHistSave()`, `loadSess = dcHistLoad()` (historial, no usa `sessWrite`) | — | `DeviceCare.h:1635-1636` |

Con LittleFS no montado todo funciona igual: las sesiones quedan solo en RAM.

### 14.7 Casos límite y bugs

* El apagado completo no vacía la sesión pendiente (§10.7); un reinicio por software tampoco.
* Los structs se escriben "en crudo": su relleno (*padding*) depende del compilador. En IDF (mismo RISC-V, mismo ABI)
  son compatibles si se definen **idénticos** (recomendado: `_Static_assert(sizeof(...) == N)`).
* El restablecimiento borra todas las sesiones (etapa 4).

### 14.8 Notas de migración

* **Lógica pura**: `crc32u`, empaquetado/desempaquetado de cabecera, validación → `flex_portable/session.c`; prueba
  host: vector `crc32("123456789") = 0xCBF43926`; archivo truncado, versión distinta, app distinta, CRC alterado → 0.
* E/S por la cola del escritor único de `flex_storage` (`flex_fs_write_async`) con escritura atómica (tmp+rename) y
  recuperación de `.bak` al montar; nunca desde el hilo de LVGL durante un gesto.
* Mantener rutas y versiones (`/System/Sessions/*.bin`) para conservar sesiones de una placa que pase de Arduino a IDF
  (la base de montaje cambia de `/littlefs` a `/flex`, la ruta relativa no).
* Debounce idéntico (1200 ms / 30 s / nunca con el dedo abajo) en un `lv_timer` de la tarea UI o en el servicio.

---

## 15. Idiomas y textos

### 15.1 Idiomas (`Session.h:300-305`, `Prefs.h:35`)

`NLANG = 6`: índice `cfgLang` 0 ES, 1 EN, 2 FR, 3 PT, 4 IT, 5 ZH. Endónimos (`LANG_ENDONYM`): `"Español"`,
`"English"`, `"Français"`, `"Português"`, `"Italiano"`, `"中文"`. Ajustes muestra `"Chinese"` para ZH
(`langNameCur`, `AppSettings.h:134`) porque **no hay glifos CJK**.
`LI()` = columna de las tablas: `cfgLang` salvo ZH → 1 (inglés). Persistencia: `flexos/lang` (§16).
Cambio: OOBE [01a §3.1] o Ajustes → General → `"Idioma"` que **cicla** `(cfgLang+1) % 6`, guarda (`cfgSavePrefs`) y marca
el escritorio para recomponerse (`AppSettings.h:636`). No hay recarga de pantallas abiertas más allá de Ajustes.

### 15.2 Cómo se guardan las cadenas

* **Tabla general** `CH[S_NSTR][5]` (columnas ES, EN, FR, PT, IT), acceso `t(id) = CH[id][LI()]` (`Session.h:308-342`):

| Id | ES | EN | FR | PT | IT |
|---|---|---|---|---|---|
| `S_SELLANG` | Selecciona tu idioma | Select your language | Choisis ta langue | Selecione o idioma | Seleziona la lingua |
| `S_CONTINUE` | Continuar | Continue | Continuer | Continuar | Continua |
| `S_YOURNAME` | ¿Cómo se llama el equipo? | Name your device | Nomme ton appareil | Nomeie o dispositivo | Nomina il dispositivo |
| `S_NAMEHINT` | Toca para escribir | Tap to type | Touche pour écrire | Toque para escrever | Tocca per scrivere |
| `S_START` | Comenzar | Get started | Commencer | Começar | Inizia |
| `S_SWIPE` | Desliza arriba para desbloquear | Swipe up to unlock | Glisse vers le haut | Deslize para desbloquear | Scorri per sbloccare |
| `S_WEATHER` | Clima | Weather | Météo | Clima | Meteo |
| `S_NOEVENTS` | (Sin eventos) | (No events) | (Aucun événement) | (Sem eventos) | (Nessun evento) |
| `S_NOTIFS` | Notificaciones | Notifications | Notifications | Notificações | Notifiche |
| `S_NONOTIFS` | (Sin notificaciones) | (No notifications) | (Aucune notification) | (Sem notificações) | (Nessuna notifica) |
| `S_SOON` | En construcción | Coming soon | Bientôt disponible | Em breve | Prossimamente |
| `S_M2` | Llega en el Milestone 2 | Arrives in Milestone 2 | Arrive au Milestone 2 | Chega no Milestone 2 | Arriva nel Milestone 2 |
| `S_BACK` | Volver | Back | Retour | Voltar | Indietro |
| `S_WELCOME` | Bienvenido a | Welcome to | Bienvenue sur | Bem-vindo ao | Benvenuto in |
| `S_CRN_HOUR` | Hora | Clock | Heure | Hora | Ora |
| `S_CRN_STOPW` | Cronómetro | Stopwatch | Chronomètre | Cronômetro | Cronometro |
| `S_CRN_TIMER` | Temporizador | Timer | Minuteur | Temporizador | Timer |
| `S_CRN_LAP` | Vuelta | Lap | Tour | Volta | Giro |
| `S_CRN_SPLIT` | Tiempo parcial | Lap time | Temps interm. | Tempo parcial | Tempo parziale |
| `S_CRN_TOTAL` | Tiempo total | Total time | Temps total | Tempo total | Tempo totale |
| `S_CRN_BLAP` | Parcial | Lap | Tour | Parcial | Parziale |
| `S_CRN_BSTOP` | Det. | Stop | Arrêt | Parar | Stop |
| `S_CRN_BRESET` | Reinic. | Reset | Réinit. | Zerar | Azzera |
| `S_CRN_BSTART` | Iniciar | Start | Démarrer | Iniciar | Avvia |
| `S_CRN_NOLAPS` | (Sin vueltas) | (No laps) | (Aucun tour) | (Sem voltas) | (Nessun giro) |

* **Nombres de apps** `APP[19][5]`, `appName(id)` (`Session.h:345-366`) — tabla completa en [01a §15].
* **Fechas** (`Session.h:369-396`): `WD_FULL[5][7]` (domingo primero: `Domingo, Lunes, Martes, Miércoles, Jueves,
  Viernes, Sábado` / `Sunday…` / `Dimanche…` / `Domingo, Segunda, Terça, Quarta, Quinta, Sexta, Sábado` /
  `Domenica, Lunedì, Martedì, Mercoledì, Giovedì, Venerdì, Sabato`); `WD_SHORT` (`dom lun mar mié jue vie sáb` /
  `Sun Mon Tue Wed Thu Fri Sat` / `dim lun mar mer jeu ven sam` / `dom seg ter qua qui sex sáb` /
  `dom lun mar mer gio ven sab`); `MO_FULL` (meses en minúscula: `enero…diciembre`, `January…December`,
  `janvier, février, mars, avril, mai, juin, juillet, août, septembre, octobre, novembre, décembre`,
  `janeiro, fevereiro, março, abril, maio, junho, julho, agosto, setembro, outubro, novembro, dezembro`,
  `gennaio…dicembre`); `MO_SHORT` (`ene feb mar abr may jun jul ago sep oct nov dic` / `Jan…Dec` /
  `jan fév mar avr mai jui jul aoû sep oct nov déc` / `jan fev mar abr mai jun jul ago set out nov dez` /
  `gen feb mar apr mag giu lug ago set ott nov dic`).
* **Tablas de módulo** con el mismo patrón `[id][5]` + `LI()`: Device Care `DCH`/`dct()` (`DeviceCare.h:178`),
  Protección contra robo `TPH`/`tpt()` (`Theft.h:149`) y `THEFT_ST[5][5]` (`FlexOS_Theft.cpp:704`), Clima `WXT`/`wt()`
  (`WeatherKit.h:156`), Calculadora `CALC_LBL[5][4]` (`AppsBasic.h:38`). El motor de clima recibe `cfgLang` crudo (0..5).
* **Patrón bilingüe en línea** `LI() == 1 ? "EN" : "ES"`: Flex Phone (180 usos), Centro y banner (10), parte del
  Inicio/AppFramework.
* **Todo lo demás del shell está solo en español** y escrito en el código (incluidas todas las pantallas de este
  documento: apagado, Modo seguro, restablecimiento, Optimizar, avisos de memoria).

### 15.3 Fuente y caracteres

Tamaño 1: mapa de bits 5×7 ASCII 0x20-0x7E + acentos compuestos por trazos (agudo, grave, circunflejo, tilde, diéresis,
cedilla) sobre á é í ó ú ü ñ Á É Í Ó Ú Ñ Ü à è ì ò ù â ê î ô û ã õ ç Ç, más `¿` `¡` `·`; cualquier otro código → `?`
(`Text.h:83-107`). Tamaños ≥2: Outfit [02 §5]. Sin CJK ni emoji en la fuente del sistema.

### 15.4 Casos límite y bugs

* `cfgLoad` **no valida** `lang`: un valor fuera de 0..5 indexa fuera de las tablas (lectura fuera de límites). Validar.
* FR/PT/IT ven **español** en Flex Phone, Centro y banner (patrón bilingüe).
* Textos sin tildes en pantallas de recuperación (marcados *(sic)*): decidir si se corrigen en IDF (recomendado) — la
  fuente del sistema LVGL debe incluir los glifos acentuados de ES/FR/PT/IT.

### 15.5 Notas de migración

* Tabla de traducciones única (p. ej. `flex_i18n` con `enum` de claves y `const char* [N][6]`, ZH → EN mientras no haya
  fuente CJK) generada desde un CSV; **mover a la tabla todas las cadenas solo-ES** del shell aunque de momento tengan
  una columna.
* Al cambiar `cfgLang` publicar `FLEX_EV_SETTINGS(lang)`; las pantallas LVGL abiertas se recrean o refrescan sus
  `lv_label`.
* Fuentes LVGL: Outfit convertida con `lv_font_conv` incluyendo Latin-1 Supplement (U+00A0-00FF) y `·`; la fuente 5×7
  de tamaño 1 se sustituye por una fuente pequeña nítida (Montserrat 10/12 o una bitmap propia) [02 §5].

---

## 16. Preferencias del sistema (`cfgLoad` / `cfgSavePrefs` y demás escritores) (`FlexOS_Ultra_Prefs.h`)

### 16.1 Funciones

| Función | Cuándo | Qué hace | Ref. |
|---|---|---|---|
| `cfgLoad()` | `setup()` paso 7 (tras migrar la clave) | abre `flexos` en lectura y carga todo lo de la tabla §16.2, normaliza `glasslv`, `lockfails`, `autolockms`, kiosco y teclado | `Prefs.h:251-295` |
| `cfgSavePrefs()` | cada cambio de Ajustes, `themeChanged(true)`, deslizadores del Panel rápido (diferido), etc. | escribe **siempre** las 12 claves del sistema + las 16 del teclado | `:297-313` |
| `cfgSaveOobe()` | fin del OOBE (Flex Account) | `oobe=true`, `lang`, `name` | `:346-353` |
| `kbPrefsSave()` | Ajustes del teclado | solo claves `kb*` | `:245-249` |
| `lockFailsSave()` | cada fallo/acierto de clave | solo `lockfails` | `:317-321` |
| `appLockSet(id,on)` | candado por app (tras verificar) | solo `applockm` | `:327-334` |
| `kioskSave()` | entrar/salir del kiosco | `kiosk*` | `:336-345` |
| `poffSaveCleanFlag()` | apagado completo | `cleanoff`, `bright` (+ audio y hora) | `Power.h:923-932` |

### 16.2 Todas las claves de `flexos` que leen/escriben estas funciones

Tipos NVS de Arduino `Preferences`: `putBool` → `u8`, `putInt` → `i32`, `putString` → `str`, `putBytes` → `blob`.

| Clave | Tipo | Defecto | Validación en carga | Variable | Escribe |
|---|---|---|---|---|---|
| `oobe` | bool | false | — | `cfgOobeDone` | `cfgSaveOobe` |
| `lang` | i32 | 0 | **ninguna** (0..5 esperado) | `cfgLang` | `cfgSavePrefs`, `cfgSaveOobe`, OOBE nombre (`Home.h:433`) |
| `name` | str | `"FlexOS Ultra"` | truncado a 23 bytes | `cfgName[24]` | `cfgSaveOobe` |
| `h24` | bool | false | — | `g24h` | `cfgSavePrefs` |
| `glass` | bool | false | — | `uiGlass` | `cfgSavePrefs` |
| `glasslv` | i32 | 50 (`GLASS_LVL_DEF`) | fuera de 0..100 → 50; `glassLevelApply` cuantiza a pasos de 5 | `gGlassLvl` | `cfgSavePrefs` |
| `dark` | bool | true | — | `gDark` | `cfgSavePrefs` |
| `iconstyle` | i32 | 0 | ninguna (0 plano, 1 vidrio) | `gIconStyle` | `cfgSavePrefs` |
| `bright` | i32 | 80 | ninguna (al aplicarlo `setBacklight` acota 5..100) | `gBright` | `cfgSavePrefs`, `poffSaveCleanFlag` |
| `locktype` | i32 | 0 | ninguna | `gLockType` | `flexLockStore/Clear` [01a §5.10] |
| `lockfails` | i32 | 0 | <0 → 0 | `lockFails` | `lockFailsSave` |
| `autolockms` | i32 | 60000 | no está en la lista → 60000 | `gAutoLockMs` | `cfgSavePrefs` |
| `poffpin` | bool | false | — | `gPoffPin` | `cfgSavePrefs` |
| `applockm` | i32 | 0 | **truncado a 16 bits** (bug, [01a §5.13]) | `gAppLock` | `appLockSet` |
| `kioskon` | bool | false | false si app inválida o sin clave | `kioskOn` | `kioskSave` |
| `kioskapp` | i32 | −1 | 0..18 o se desactiva | `kioskApp` | `kioskSave` |
| `kioskx` `kiosky` `kioskw` `kioskh` | i32 | 0 | ninguna | `kioskEx*` | `kioskSave` |
| `lockwidgets` | i32 (→ u8) | 1 (`LW_CLOCK`) | ninguna; bits 1 reloj, 2 clima, 4 calendario, 8 notificaciones | `gLockWidgets` | `cfgSavePrefs` |
| `navmode` | i32 | 0 | ninguna (0 botones, 1 gestos) | `gNavMode` | `cfgSavePrefs` |
| `animstyle` | i32 | 0 | ninguna (0 zoom, 1 fundido, 2 deslizar) | `gAnimStyle` | `cfgSavePrefs` |
| `kbsize` | i32 | 1 | 0..2 | `gKbSize` | `cfgSavePrefs`, `kbPrefsSave` |
| `kbfast` | bool | true | — | `gKbFastType` | ídem |
| `kbtool` | bool | true | — | `gKbToolbar` | ídem |
| `kbpred` | bool | true | — | `gKbPredict` | ídem |
| `kbspell` | bool | false | — | `gKbSpell` | ídem |
| `kbemoji` | bool | false | — | `gKbEmojiSug` | ídem |
| `kbhicon` | bool | false | — | `gKbHiCon` | ídem |
| `kbopa` | i32 | 100 | 40..100 | `gKbOpacity` | ídem |
| `kbstyle` | i32 | 0 | 0..2 | `gKbStyle` | ídem |
| `kbfont` | i32 | 1 | 0..2 | `gKbFontSc` | ídem |
| `kblp` | i32 | 500 | 350/500/700 | `gKbLpMs` | ídem |
| `kbfx` | i32 | 100 | 60/100/160 | `gKbFxMs` | ídem |
| `kbsyms` | blob 4 B | {0,1,2,3} | cada uno 0..15 | `gKbSym[4]` | ídem |
| `kbscabr` | blob 80 B (8×10) | atajos de fábrica | si el tamaño de `kbscabr` o `kbscexp` no cuadra → fábrica | `gKbScAbr` | ídem |
| `kbscexp` | blob 192 B (8×24) | `xq`→`porque`, `q`→`que`, `tb`→`también`, `pf`→`por favor` | ídem | `gKbScExp` | ídem |
| `cleanoff` | bool | false | se lee **y borra** solo si el arranque es `DEEPSLEEP` | `gBootCleanOff` | `poffSaveCleanFlag` |
| `fxvpurge` | i32 | 0 | 1 = purga de la Carpeta segura retirada ya hecha | — | `setup()` |
| `apps3rd` | i32 | 0 | máscara heredada (Modo seguro) | — | Modo seguro fila 1 |

Otras claves de `flexos` (sal/hash de la clave, escritorio, fondos, paquetes): [01a §16].

### 16.3 Otros namespaces de esta área

| Namespace | Clave | Tipo | Defecto | Uso |
|---|---|---|---|---|
| `flexsafe` | `fails` / `cause` | i32 / i32 | 0 / motivo | §11 |
| `flexreset` | `pending` / `ver` / `stage` / `err` | bool / i32 / i32 / i32 | false / 0 / 0 / 0 | §13.6 |
| `flexphone` | `dnd` | bool | false | §6.6 |
| `flexqs` | `qp1` | blob 124 B | fábrica | [01b §5.11] |
| `flexaudio` | `vol` / `mute` | u8 / bool | 70 / false | volumen (Panel rápido, apagado) |
| `flexos_time` | `epoch` / `lastsync` | u64 / u64 | — | hora persistida (cada hora y al apagar) |

### 16.4 Casos límite y bugs

* `cfgSavePrefs` reescribe **28 claves** por cada cambio de una sola (desgaste de flash; NVS solo reescribe si el valor
  cambia, pero cada `put` recorre la página).
* Sin validar: `lang`, `iconstyle`, `navmode`, `animstyle`, `bright`, `kioskx..h`, `locktype`.
* `applockm` truncado a 16 bits (apps 16-18 pierden el candado al reiniciar).

### 16.5 Notas de migración

* `flex_storage` ya expone `flex_cfg_get_*/set_*` sobre el namespace `"flexos"` (`FLEX_NVS_NS`) con escritor único y
  `flex_cfg_flush`. Para **conservar** la configuración de una placa Arduino, leer cada clave con **su tipo exacto**
  (`bool` → `nvs_get_u8`, `int` → `nvs_get_i32`, `name` → `nvs_get_str`, blobs → `nvs_get_blob`, `vol` → `u8`,
  `epoch` → `u64`): una lectura con otro tipo falla y devuelve el defecto.
* Un `flex_prefs_load()` puro que reciba un "lector de kv" y aplique **todas** las normalizaciones (incluidas las que hoy
  faltan) → prueba host con valores corruptos (lang=99, bright=0, kioskapp=40 con locktype=0…).
* Guardado: un setter por preferencia que publica `FLEX_EV_SETTINGS` y deja el volcado al escritor (agrupado), en lugar
  de `cfgSavePrefs` global.

---

## 17. Memoria: medir, decidir, soltar y "Optimizar Flex OS"

Tres capas separadas a propósito (`Core.h:31-63`): **medir** (`Core.h`, solo `heap_caps_*` y FS), **decidir**
(`FlexOS_Mem.cpp`, lógica pura con pruebas `tests/host/test_mem.cpp`), **soltar** (`memShedSystem` en `System.h`, donde se
ven todos los buffers). Nunca se toca un dato del usuario.

### 17.1 Medir (`memSample/memTick`, `Core.h:64-162`)

`FlexMemSnap` (`FlexOS_Mem.h:94-105`): `psTotal, psFree, psLargest, psPeakUsed, inTotal, inFree, inMin, fsTotal,
fsUsed, fsValid`. Tres cadencias, **nunca dentro del dibujo**:

| Dato | Cadencia | Notas |
|---|---|---|
| libre/total de PSRAM y SRAM interna | cada **1000 ms** (`MEM_TICK_MS`) | barato |
| mayor bloque contiguo de PSRAM | cada **2000 ms** con memoria "apretada" (sin nivel aún, nivel ≥ NOTICE o PSRAM libre < 11 MB); cada **10 000 ms** con holgura **y sin interacción** (dedo, transición, gesto de página) | `heap_caps_get_largest_free_block` recorre el heap en sección crítica: produce tirones |
| uso de LittleFS | cada **15 000 ms**, **solo con la pantalla de detalle de memoria a la vista** (`gMemWantFlash`) y sin dedo | recorre la flash |

Pico de PSRAM usada y mínimo de SRAM libre se llevan con la misma medida. `memSampleNow()` mide en el acto (con bloque)
para las **decisiones** (abrir una app pesada, alivio, Optimizar).

### 17.2 Decidir (`FlexOS_Mem.cpp`)

Constantes (`FlexOS_Mem.h:110-191`) — pensadas para 32 MB de PSRAM:

| Nombre | Valor |
|---|---|
| `NOTICE` / `WARN` / `CRIT` (entrada) | 10 MB / 6 MB / 5 MB libres |
| Salida (histéresis) `EXIT_NOTICE/WARN/CRIT` | 11 / 7 / 6 MB; `EXIT_SRAM` 56 KB; `EXIT_BLOCK` 1,5 MB |
| SRAM interna `LOW` / `MIN` | 64 KB / 40 KB |
| Colchón al abrir `COST_LIGHT/MEDIUM/HEAVY` | 256 KB / 1 MB / 3 MB |
| Bloque contiguo mínimo `BLOCK_HEAVY/MEDIUM` | 1 MB / 384 KB |
| Enfriamientos `CD_MEM` / `CD_FRAG` / `CD_FLASH` / `CD_GLOBAL` | 5 min / 10 min / 15 min / 30 s |
| `RELIEF_MS` | 60 s |

* **Nivel instantáneo** `flexMemLevel` (`:39-51`): sin PSRAM → OK; SRAM < 40 KB → CRITICAL; PSRAM < 5 MB → CRITICAL;
  bloque < 1 MB → CRITICAL; < 6 MB → WARN; < 10 MB → NOTICE; si no OK. Salud: CRITICAL → rojo, WARN/NOTICE → ámbar,
  OK → verde (`flexMemHealth`).
* **Nivel sostenido** `flexMemLevelStep/Hyst` (`:146-191`): empeorar es inmediato; mejorar exige superar el umbral de
  salida del nivel actual y **baja como mucho un nivel por evaluación**; `rose = 1` solo si esta evaluación empeoró.
* **Fragmentación** `100 − bloque·100/libre`: ≥65 % alta, ≥35 % media.
* **Veredicto de apertura** `flexMemCanOpen(snap, peso)` (`:99-131`): sin PSRAM → OK; SRAM < 40 KB y app no ligera →
  `DENY_SRAM`; **ligera → OK siempre**; PSRAM < 5 MB → `DENY_PSRAM`; bloque < necesario (1 MB pesada / 384 KB media):
  si libre ≥ 6 MB + necesario → `NEED_SHED`, si no `DENY_BLOCK`; libre < colchón + 5 MB → `NEED_SHED`; pesada con
  libre < 6 MB → `NEED_SHED`; si no OK.
* **Avisos secundarios** `flexMemAlertPick` (`:229-275`): candidatos en orden SRAM (< 64 KB), FRAG (alta con ≥ 6 MB
  libres), FLASH90/FLASH80 (≥90 / ≥80 % medido); cada clase con su enfriamiento (la primera vez sale sin esperar) y
  separación global de 30 s entre dos avisos cualesquiera.
* Formato `flexMemFmt`: `"%u B"`, `"%u KB"`, `"%u.%u MB"`, `"%u.%u GB"` (un decimal truncado); `flexMemFmtPair` `"%s / %s"`.

### 17.3 Admisión al abrir una app (`memAdmitApp`, `Core.h:322-352`)

Solo para apps **cerradas** (volver a una suspendida nunca se bloquea). Peso por app `APP_WEIGHT[19]` (`Core.h:174-203`):
**pesadas** Galería, Multimedia, Modo PC, Navegador, Cámara; **medias** Paint, Juegos, Flex Store, Música; **ligeras**
el resto (Reloj, Almacenamiento, Notas, Flex Compass, Ajustes, Calculadora, Calendario, Clima, Flex Phone, Device Care).
Nombres para la interfaz: `"Pesada"`, `"Media"`, `"Ligera"`. Flujo: medir → veredicto; si `NEED_SHED` → soltar todo lo
seguro (`memShedAll(0)`) → medir → otra vez; si sigue `NEED_SHED` → `DENY_PSRAM`. Negada → aviso `"%s no se abre
ahora"` (§6.8) y log `"[MEM] apertura denegada: %s (veredicto %d, libre %u KB, bloque %u KB)"`. Nunca se cierra una app
desde aquí.

### 17.4 Alivio automático y avisos (`memAlertTick`, `Core.h:381-445`)

Cada vuelta: sale en Modo seguro o restablecimiento; actualiza **siempre** el nivel sostenido. Solo **actúa y avisa**
en `ST_HOME`/`ST_APP`, sin transición, sin Optimizar a la vista y sin la radio levantándose (`wifiRadioBusy`).
Con nivel ≥ WARN y (acaba de empeorar o han pasado 60 s desde el último alivio): `memShedAll(0)` → medir → recalcular;
si ya bajó de WARN no se dice nada; si sigue y **acaba de empeorar** → un aviso (`"Memoria crítica"` o `"Memoria casi
llena"`, §6.8). OK/NOTICE: silencio absoluto (sin barras ni iconos). Después, avisos secundarios (fragmentación, SRAM,
flash) con sus enfriamientos.

### 17.5 Soltar memoria

**`memShedSystem()`** (`System.h:75-131`) — cachés que el sistema sabe reconstruir, cada una con su guardia "no mientras
se vea"; devuelve los bytes de PSRAM **medidos** recuperados:

| Recurso | Tamaño aprox. | Se suelta si… |
|---|---|---|
| Fondo desenfocado `blurBg` | 768 KB | no se está en Recientes, bloqueo, alta/verificación de clave, Caja de apps ni apagado |
| Panel rápido (compuesto + vidrio + captura de app) | hasta 2,3 MB | cortina cerrada del todo |
| Scratch de Liquid Glass `glassBuf` | 768 KB | siempre |
| Deslizamiento del escritorio (página vecina + máscaras) | ~560 KB | sin gesto de página en curso |
| *Backdrop* del escritorio (fondo limpio + desenfocado) | ~1 MB | fuera de `ST_HOME` (y marca `gHomeDirty`) |
| Tarjeta de vidrio cacheada (`glcScratch`, `glcCard`) | — | siempre (invalida su firma) |
| Páginas de animación de Ajustes (`setPgOut/In`) | 768 KB × 2 | Ajustes no en primer plano |
| Hoja de la Caja de apps `drwPage` | 768 KB | fuera de `ST_DRAWER` |
| Iconos de apps descargadas | hasta 32 KB | fuera de `ST_DRAWER` |
| Fondo de Modo PC | 768 KB | Modo PC cerrado |
| Banda del deslizador de apagado `poffBand` | ~135 KB | fuera de las pantallas de apagado |
| Miniaturas de Recientes (73 KB c/u) | — | deja como mucho 2 (1 en modo eficiente) |

**`memShedApp(id)`** (`Core.h:270-287`): solo apps **suspendidas** y sin trabajo real en segundo plano (`appBgBusy`: en
lista blanca `APP_BG_KEEP` **y** su hook `bgWork()` dice que trabaja); llama al hook `shed()` de la app, mide lo liberado,
lo descuenta de su huella y marca `gAppShed[id]` (Recientes pasa de "Pausada" a "Estado guardado").
**`memShedAll(stopAt)`** (`:292-311`): primero `memShedSystem`, luego apps suspendidas de **la menos usada a la más**
(LRU por `gAppSeenMs`), hasta `stopAt` si se indica.

### 17.6 Desalojo por presupuesto (`appEnforceMemoryBudget`, `Core.h:462-488`)

Se ejecuta en **cada `appSuspend`** (y cede si la radio se está levantando). Solo si el nivel instantáneo es CRITICAL:
1) `memShedAll(0)`; 2) si sigue CRITICAL, **cierra** (`appTerminate(id, false)`) la suspendida menos reciente sin
trabajo en segundo plano, **solo si su sesión se pudo guardar**; quita su tarjeta de Recientes; log
`"[LIFE] presupuesto de memoria: %s cerrada"`; repite hasta salir de CRITICAL o no quedar candidatas. No hay tope fijo de
apps abiertas. La app en primer plano nunca entra.

### 17.7 Modo visual eficiente (`gEffMode`, `Theme.h:652-684`) [02 §4.4]

Temporal, **no se guarda ni aparece en Ajustes**. Lo enciende Optimizar si tras soltar el nivel sigue ≥ WARN; se apaga
solo cuando el nivel **sostenido** vuelve a OK (`memTick`, `Core.h:158-161`). Efectos: radio de desenfoque del vidrio
≤ 2 px, sombras con la mitad de alfa, Recientes conserva 1 miniatura. Al cambiar invalida la tarjeta de vidrio, el
escritorio y la cortina.

### 17.8 "Optimizar Flex OS" (`System.h:133-393`)

**Entradas:** Almacenamiento → pantalla principal, píldora `"Optimizar"` 118×32 r16 `TH_PRIM` texto tamaño 1 `TH_ONACC`
en la fila "Detalles de memoria y sistema" (`AppStorage.h:234-241`); Almacenamiento → detalle, botón `"Optimizar Flex
OS"` (36, y) 408×44 radio 22, superficie elevada + borde `TH_BORDER`, texto tamaño 2 `TH_ACCS` (`:428-434`); Device Care
→ Optimización → botón `dct(DCS_OPTRUN)` (`DeviceTests.h:897-906`, `optStartCb(dcOptimDone)`: al terminar anota la pasada
en el historial y vuelve a su pantalla).

**Propietario de la pantalla:** mientras `optActive()`, `loop` solo ejecuta `optTick(); flexOtaRender()` **sin pausa**
[01b §1.3 paso 6]. Modal: durante el trabajo cualquier toque se traga.

**Layout** (panel flotante sobre la pantalla que lo abrió; el fondo se **captura una vez** — banda y=206..593, ~372 KB —
y cada cuadro se compone sobre esa copia; sin PSRAM para la captura → material plano sólido):

| Elemento | Geometría | Estilo / texto |
|---|---|---|
| Panel | (30, 214) 420×372 radio 24 | `uiSurface(UIS_ELEVATED)` + borde `TH_BORDER` |
| Título | tamaño 3 centrado y=234 | `"Optimizar Flex OS"` `TH_TXT` |
| 5 etapas | y = 276 + i·30; marca en (60, y+9); texto tamaño 2 en (80, y), recorte x=434 | marca: hecha = círculo r8 `TH_OK`; en curso = aro r8 `TH_PRIM` + punto r4; pendiente = aro `TH_DIV`. Texto: en curso `TH_TXT`, hecha `TH_TXT2`, pendiente `TH_MUTE` |
| Pie en curso | tamaño 1 en (50, 436) | `"No se borran notas, dibujos ni archivos."` `TH_MUTE` |
| Resultado (al terminar), líneas de 20 px desde (50, 436), recorte x=430 | tamaño 1 | `"Se liberaron %s de recursos temporales."` o `"No se encontraron recursos temporales seguros para liberar."` (`TH_TXT`); si hubo: `"Caché temporal en almacenamiento: %d archivo%s."` (`TH_TXT2`, "s" si ≠1); `"PSRAM disponible: %s."` o `"PSRAM: No disponible en esta placa."` (`TH_TXT`); si se activó: `"Modo visual eficiente activado temporalmente."` (`TH_WARN`) |
| Botón | (150, 522) 180×46 radio 23 `TH_PRIM` | `"Hecho"` tamaño 2 `TH_ONACC` centrado y=536 |

Etapas (`OPT_STEP_TXT`), una por vuelta cada **420 ms** (`OPT_STEP_MS`), el trabajo se hace **al entrar** en la etapa:

| # | Texto | Trabajo |
|---|---|---|
| 1 | `"Analizando memoria"` | medida completa (`memSampleNow`); la PSRAM libre inicial se toma al empezar |
| 2 | `"Liberando caché temporal"` | borra el contenido de `/System/Cache` (cuenta archivos) + `memShedApp` de todas las apps suspendidas |
| 3 | `"Suspendiendo recursos no usados"` | `memShedSystem()` |
| 4 | `"Verificando estabilidad"` | mide; si nivel ≥ WARN y no estaba → **modo visual eficiente**; calcula `ganado = libre_ahora − libre_inicial` (0 si negativo) |
| 5 | `"Optimización finalizada"` | pantalla de resultado (la etapa 5 sale marcada como hecha) |

Duración hasta el resultado ≈ 4 × 420 = 1,7 s. Cierre: **cualquier toque** (en "Hecho" o fuera) → suelta la captura,
`touchDropAll`, vuelve a quien lo abrió (callback) o repinta Almacenamiento. **No** borra notas, dibujos, ajustes,
archivos ni apps; no reinicia; no cierra la app activa; no toca apps con trabajo en segundo plano.

### 17.9 Huella medida por app (`Core.h:213-260`) — consumidor: Recientes [01a §12]

PSRAM que desaparece durante `enter()`/`resume()` (medida justo antes y después en `appTrFinishOpen`, `Core.h:899-902`),
menos lo devuelto al suspender/soltar. Sin medida → la interfaz escribe `"--"`, nunca un número inventado ("estimado").
`appUnsaved(id)` = hook `dirty()` o sesión pendiente (aviso de cambios sin guardar en Recientes).

### 17.10 Notas de migración

* `FlexOS_Mem.cpp` **ya está** en `components/flex_portable/src/` tal cual: portar `tests/host/test_mem.cpp` del árbol
  Arduino a `FlexOS_Ultra_IDF/tests/host` (no existe todavía). Revisar los umbrales con la PSRAM real de la placa
  (32 MB) y el consumo de LVGL (búferes de dibujo en PSRAM).
* Medir en la tarea `flex_system` (ya mide cada 2 s: `heap_psram_free/largest`, `heap_int_*`), publicar `FLEX_EV_SYSTEM`
  con el `FlexMemSnap` y ejecutar `flexMemLevelStep`/alivio allí; la UI solo presenta. Mantener la regla "bloque
  contiguo cada 10 s con holgura" (la llamada sigue siendo cara en IDF).
* **Soltar** en LVGL: registro de "cachés reconstruibles" (`flex_mem_register_cache(name, size_fn, free_fn, guard_fn)`)
  para fondos desenfocados, *snapshots* (`lv_draw_buf`), imágenes decodificadas, miniaturas; `lv_image_cache_drop(NULL)`
  y `lv_cache` de LVGL entran en el alivio. Las apps exponen `shed()` igual que hoy.
* Optimizar como `lv_obj` modal en `lv_layer_top()` con `lv_timer` de 420 ms; el trabajo pesado (borrar `/System/Cache`)
  por la cola de `flex_storage` (no en el hilo de LVGL).
* Bug a no copiar de `themeChanged` (IDs literales 12/10/11 que no corresponden a Ajustes/Paint/Juegos, `System.h:438-458`)
  [02 §2.7].

---

## 18. Bucle `loop()` — complemento de [01b §1.3] para los servicios de esta área

[01b §1.3] da el orden completo de `loop()`, el ritmo (`loopPaceMs` 1/5 ms) y la prioridad de dueños de pantalla. Aquí,
el papel de cada servicio de esta área dentro de ese orden y su destino en IDF:

| Paso en `loop` (`.ino`) | Función | Qué hace | Cadencia | Destino IDF propuesto |
|---|---|---|---|---|
| 1 (`:671`) | `flexFeedWdt()` | alimenta el TWDT; si la suscripción se perdió (la pila esp-hosted reinicia el TWDT) reintenta `esp_task_wdt_add` cada 5 s con un solo log (`"[WDT] loopTask ya no esta suscrito al Task Watchdog; reintentando cada 5 s"` / `"[WDT] loopTask resuscrito al Task Watchdog"`) (`Power.h:1024-1042`) | cada vuelta | cada tarea larga se suscribe y alimenta su TWDT; no hace falta el reintento si el Wi-Fi no reconfigura el TWDT |
| 1 | `loopRateTick()` | vueltas/s (`gLoopRate`), para diagnóstico (`Home.h:223-229`) | 1 s | `flex_metrics` |
| 1 | `flexPollTouch()` | incluye kiosco, tragado, **suspensión**, pellizco | cada vuelta | `flex_touch` + `flex_touch_arb` |
| 3 (`:721-726`) | `frTick()` exclusivo | §13 | 1 etapa/vuelta, ≥260 ms | tarea de trabajo + pantalla LVGL |
| 4 | `safeStableTick()` | §11.1 | una vez a los 60 s | `esp_timer` one-shot |
| 4 | `sessAutosaveTick()` | §14.4 | cada vuelta (debounce) | `lv_timer` 100 ms en UI |
| 4 | `safeToastTick()` | §11.3 | 1800 ms | `lv_timer` one-shot |
| 4 | `memTick()` / `memAlertTick()` | §17.1/§17.4 | 1 s / cada vuelta | tarea `flex_system` |
| 4 | `suspFadeTick()` | §9.3 | 10 ms | `flex_power` (ledc fade / `esp_timer`) |
| 4 | `autoLockTick()` | §9.5 (lee `T` sin filtrar, antes de que nadie lo consuma) | cada vuelta | UI (`lv_display_get_inactive_time()` + vetos) |
| 4 | `fpbTouch()` / `notifHandleTouch()` | banner (§6.4.3) / isla (inerte) | cada vuelta | eventos LVGL |
| 4 | `clkPersistTick()` | hora a NVS cada hora | 1 h | `esp_timer` |
| 6 (`:817-821`) | `optTick()` exclusivo | §17.8 | 420 ms/etapa | `lv_timer` + modal |
| 7 | `flexPhoneTick()` → `phoneNotifyBridge` | §6.7 | cada vuelta | tarea Flex Phone → `FLEX_EV_NOTIF` |
| 8 (`:854-860`) | `fpcGlobalHandle()` | §6.5 (dueño de la pantalla) | cada vuelta | capa LVGL en la pila de dueños |
| 11 (`:965-966`) | `poffTick` / `poffAnimTick` | §10 | cada vuelta | pantalla LVGL + `lv_anim` |
| 11 (`:973-974`) | `safeTick` / `frTick` | §11 / §13 | cada vuelta | pantallas LVGL |
| 12 | `kioskTick()` | §12.3 | cada vuelta | evento de pulsación larga del candado |
| 15 | `notifTick()` / `fpbTick()` | isla inerte / banner | 33 ms / cada vuelta | `lv_anim` del banner |

**Diagnóstico de tirones** `FLEXHITCH(...)` (`Types.h:690-719`): con `FLEXOS_DIAG_HITCH=1` mide cada servicio envuelto y
registra los que superan 2500 µs (`"[hitch] %s: %lu us (peor %lu us, %lu veces, anterior hace %lu ms)"`, como mucho uno
cada 2 s). Apagado por defecto. En IDF: `flex_metrics` + `esp_timer_get_time` por servicio.

---

## 19. Riesgos principales de migración de esta área (resumen)

1. **Modo seguro = bypass del bloqueo** (§11.6): entra sin PIN y da Ajustes/escritorio/Explorador. Exigir la clave.
2. **Restablecimiento incompleto** (§13.5): sobreviven el vínculo con el teléfono (`flexphone`, claves), Flex Storage,
   Device Care, robo, clima, navegador y audio. Borrar todo salvo el marcador.
3. **Apagado que no ahorra** (§10.6): el filtro de 3 s mantiene el chip despierto ~91 % con despertar por temporizador;
   además el pin INT del GT911 no está confirmado. Salida temprana sin dedo y cablear ext1.
4. **Suspensión en `ST_FACTORY`** bloquea el táctil (§9.6); vetar y desacoplar el fundido del dueño de pantalla.
5. **Banner sobre `ST_THEFT`** y bajo el kiosco (§6.10): posible fuga de contenido de notificaciones.
6. **Compatibilidad NVS/sesiones** (§16.5, §14.7): tipos exactos de `Preferences` y structs con el mismo relleno, o
   decidir explícitamente "primer arranque" en IDF.
7. **Escritura atómica sin recuperación** de `.bak`/`.tmp` (§14.3).
8. **Avisos solo ES/EN** en Flex Phone/Centro y `lang` sin validar (§15.4).
9. Centro de notificaciones sin animación real y con filas que pisan la cabecera (§6.5.1, §6.10).
10. Cancelar el apagado desde una app deja la app en `RUNNING` con Inicio delante (§10.8); misma clase de incoherencia
    al salir del kiosco (§12.5).

---

## 20. Checklist de funcionalidades del área

| Funcionalidad | Archivo | Detalle clave |
|---|---|---|
| Modelo de avisos del sistema `gNotifs` | `Notif.h:181-199`, `Types.h:412-446` | historial de 3, dedupe por FNV-1a del título, `bornMs` |
| `sysNotify` / `mediaNotify` / `sysSay` | `Media.h:66-98` | `sysSay` en app → solo banner, sin historial |
| Iconos de aviso | `Conn.h:343-351` | sistema = icono Ajustes, medios = icono Multimedia |
| Isla dinámica (legado inerte) | `Notif.h` | no portar; solo el modelo |
| Cola única del banner | `Overlay.h:340-422` | 6 plazas, prioridad sistema > urgente > normal, "+N", dedupe, reencolado al frente |
| Banner: layout V/H | `Overlay.h:144-155, :429-451` | 452×72 en (14,18) / 300×64 en (24,14), r18, barra de prioridad, `"Flex OS"` |
| Banner: vidrio y sombra | `Overlay.h:465-556` | vidrio sobre lo de debajo, tinte mín. 150, sombra (3,4) α60, recálculo ≤80 ms |
| Banner: supresión | `Overlay.h:316-334` | pantallas seguras, modales, cortina, Centro, DeX, suspensión, Modo seguro, OTA, Optimizar |
| Banner: gestos | `Overlay.h:704-770` | episodio propio, 10 px, descarte w/3 o 0,7 px/ms+40 px, tap → Flex Phone |
| Banner: tiempos | `Overlay.h:157-165, :773-866` | 220/4200/140/180 ms, mín. 1500 al volver |
| Banner: memoria | `Overlay.h:664-677` | 69 KB + 80 KB PSRAM, exige 5 MB libres |
| Descarte sincronizado con el teléfono | `Overlay.h:688-702` | `FLNK_T_NOTIF_REMOVE` |
| Centro: apertura por borde izquierdo | `Overlay.h:1090-1116` | x<26, y>40, >12 px hacia dentro y > vertical |
| Centro: layout | `Overlay.h:962-1030` | cabecera 96, filas 78, píldora DND, "Borrar todas", vacío |
| Centro: acciones | `Overlay.h:1051-1086` | DND, borrar todas, abrir Flex Phone, descartar aviso del sistema |
| Centro: cierre | `Overlay.h:1138-1170` | soltar ≤ −240, arrastre derecha >60 px, 190 ms |
| No molestar | `Overlay.h:56-82, :1258-1264` | NVS `flexphone/dnd`, solo teléfono, subtítulos del Panel rápido |
| Puente Flex Phone → banner | `Overlay.h:1202-1237` | solo la más reciente por vuelta |
| Avisos de memoria (textos) | `Core.h:334-445` | 7 avisos + negativa de apertura |
| Suspensión: gesto 2 dedos ×2 | `Touch.h:233-304`, `Types.h:325-330` | 450/45/600 ms, 2 lecturas, vetos kiosco/teclado/pellizco |
| Suspensión: despertar 1 dedo ×2 | `Touch.h:281-290` | solo escuchado suspendido |
| Suspensión: fundido + DCS | `Touch.h:184-230`, `HAL.h:130-163, :284-300` | 6 %/10 ms lineal, DISPOFF/DISPON, brillo exacto restaurado |
| Bloqueo al despertar | `Power.h:633-657` | compone el bloqueo a oscuras, recuerda app/estado |
| Control "Bloquear" del Panel rápido | `QuickPanel.h:262` | = suspender (bloqueo al despertar) |
| Auto-bloqueo por inactividad | `Lock.h:343-396`, `Prefs.h:52-73` | orden de vetos, opciones, rearme en OTA |
| Apagado: pantalla de confirmación | `Power.h:729-829` | panel 424×424, deslizador 400×96, Cancelar 220×72 |
| Apagado: deslizador | `Power.h:834-878` | 1:1, 92 % (≥280/304), retorno proporcional |
| Apagado seguro (`poffpin`) | `Power.h:718-724`, `AppSettings.h:365-368, :685-692` | clave antes de apagar; cancelar vuelve al deslizador |
| Apagado: animación final | `Power.h:935-997` | 520/260/700/620 ms + fundido, `"Flex OS"` tamaño 5 |
| Deep sleep | `Power.h:892-919`, `Types.h:331-356` | hold GPIO3, ext1 o temporizador 400 ms |
| Filtro de encendido 3 s | `Power.h:1056-1083` | ventana 4,2 s, dedo sostenido 3 s; bug de consumo |
| Persistencia al apagar | `Power.h:923-932` | `cleanoff`, `bright`, audio, hora |
| Arranque desde deep sleep | `.ino:535-542` | lee y borra `cleanoff`, sin banda forense |
| Modo seguro: detección | `Session.h:133-210` | anormales PANIC/WDT/BROWNOUT, 3 seguidos, 60 s estable |
| Modo seguro: pantalla y 7 filas | `Recovery.h:604-690` | textos exactos, causa real |
| Modo seguro: lista blanca y toast | `Recovery.h:549-588` | Ajustes, Almacenamiento, Reloj, Calculadora; 1800 ms |
| Modo seguro: limpiar cachés | `Recovery.h:592-602` | `/System/Cache` + buffers |
| Modo seguro: arranque reducido | `.ino:393-501, :766-827` | sin red, nube, cuenta, tienda, medios, teléfono |
| Kiosco: arranque directo a la app | `Home.h:310-314`, `Prefs.h:280-287` | sin bloqueo, sin clave → desactivado |
| Kiosco: candado estampado | `Lock.h:418-457`, `Gfx.h:127-131` | (448,44) 24×24, en cada transferencia |
| Kiosco: salida | `Lock.h:500-515`, `Power.h:113-116` | >1000 ms, <12 px, una vez por contacto; cancelar → app |
| Kiosco: vetos | tabla §12.4 | 15 puntos de veto |
| Restablecer: entrada y aviso OTA | `Recovery.h:374-394, :429-434` | 2200 ms |
| Restablecer: pantalla de aviso | `Recovery.h:86-120` | 7 viñetas, "NO se elimina" |
| Restablecer: escribir RESTABLECER | `Recovery.h:124-145, :479-510` | solo sin clave, A-Z, 19 máx. |
| Restablecer: verificación con clave | `Recovery.h:465`, `Power.h:46, :109` | `LSU_AFTER_FACTORY` |
| Restablecer: deslizador final | `Recovery.h:147-178, :511-529` | 92 % (≥302/328), sin animación de vuelta |
| Restablecer: progreso por etapas | `Recovery.h:181-211, :328-361` | 7 etapas, ≥260 ms cada una |
| Restablecer: qué borra cada etapa | `Recovery.h:234-327` | tabla §13.4 |
| Restablecer: namespaces que sobreviven | §13.5 | riesgo de privacidad |
| Restablecer: marcador transaccional | `Session.h:212-298` | `flexreset`, verificación por relectura, reanudación |
| Restablecer: confirmación en OOBE | `Home.h:352` | borra el marcador |
| Restablecer: pantalla de fallo | `Recovery.h:212-227, :440-457` | Reintentar (idempotente) / Reiniciar |
| Sesiones: formato `SessHdr` + CRC | `Session.h:55-102`, `Types.h:158` | 16 B, "1FLX", ≤1008 B de datos |
| Sesiones: escritura atómica | `FlexOS_FS.cpp:636-661` | tmp + bak + rename; sin recuperación al arrancar |
| Sesiones: guardado diferido | `AppFramework.h:1393-1414` | 1200 ms / 30 s / nunca con el dedo |
| Sesiones: carga perezosa | `AppFramework.h:1385-1390` | una vez por arranque |
| Sesiones: por app | tabla §14.6 | Ajustes, Calculadora, Notas, Paint, Multimedia, Device Care |
| `appTerminate` sin perder trabajo | `AppFramework.h:1416-1438` | no cierra si no guarda |
| Idiomas y `LI()` | `Session.h:300-305` | 6 idiomas, ZH → EN |
| Tabla `CH` (25 cadenas × 5) | `Session.h:308-342` | §15.2 |
| Días y meses | `Session.h:369-396` | §15.2 |
| Tablas de módulo y patrón bilingüe | §15.2 | DCH, TPH, WXT, CALC_LBL; `LI()==1` |
| Cambio de idioma en Ajustes | `AppSettings.h:636` | cicla ×6 |
| Fuente: acentos soportados | `Text.h:83-107` | Latin-1 parcial + ¿ ¡ · |
| `cfgLoad` / `cfgSavePrefs` | `Prefs.h:251-313` | tabla §16.2 (todas las claves) |
| `cfgSaveOobe`, `kioskSave`, `appLockSet`, `lockFailsSave` | `Prefs.h:317-353` | escritores parciales |
| Otros namespaces del área | §16.3 | `flexsafe`, `flexreset`, `flexphone`, `flexqs`, `flexaudio`, `flexos_time` |
| Medición de memoria | `Core.h:64-162` | 1 s / 2-10 s bloque / 15 s flash |
| Decisión de memoria (puro) | `FlexOS_Mem.cpp` | niveles, histéresis, veredicto, avisos con enfriamiento |
| Admisión al abrir | `Core.h:322-352, :697-700` | solo apps cerradas, `NEED_SHED` → soltar → reintentar |
| Alivio automático | `Core.h:381-445` | ≥WARN, cada 60 s como mucho, aviso solo si sigue |
| `memShedSystem` | `System.h:75-131` | 12 recursos con guardias |
| `memShedApp` / `memShedAll` | `Core.h:270-311` | LRU, sin tocar trabajo en segundo plano |
| Desalojo por presupuesto | `Core.h:462-488` | cierra LRU solo si guardó la sesión |
| Modo visual eficiente | `Theme.h:652-684`, `Core.h:158-161` | temporal, blur ≤2, sombras/2, 1 miniatura |
| Optimizar Flex OS | `System.h:133-393` | 5 etapas de 420 ms, cifras medidas, "Hecho" |
| Entradas a Optimizar | `AppStorage.h:234-241, :428-434, :619-624, :804`; `DeviceTests.h:897-906` | Almacenamiento ×2, Device Care |
| Huella medida por app | `Core.h:213-260, :899-902` | `"--"` sin medida |
| Lugar de los servicios en `loop` | §18 | destino IDF por servicio |
| Diagnóstico `FLEXHITCH` | `Types.h:690-719` | >2500 µs |
| Alimentación defensiva del TWDT | `Power.h:1024-1042` | reintento cada 5 s |
