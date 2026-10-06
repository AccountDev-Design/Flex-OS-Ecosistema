# Causas raíz: navegador en DeX, Relay del teléfono, avisos y teclado

Auditoría de Flex OS Ultra (ESP32-P4, panel 480×800 y sus orientaciones) a
partir de cinco capturas de la placa: avisos solapados en Inicio con
"Google?Play?Store", la web del teléfono ampliada, franja blanca al desplazar,
ventana del navegador en DeX blanca a la izquierda y negra a la derecha, y la
hora de la barra de estado con restos. Ninguna corrección es un parche por
captura: cada una ataca la causa que producía el síntoma en todos los casos.

Prohibido por diseño (y no usado): escalas fijas (0,5 / 0,7), repintar todo
en cada cuadro, recargar la página o recrear el WebView al redimensionar,
`if(width == 480)`, ocultar el desborde sin arreglar el viewport, esperas
fijas o "esperar a un click".

---

## 1 · Navegador: viewport, redimensionado y recorte

| Síntoma | Causa raíz | Corrección | Dónde |
|---|---|---|---|
| Al redimensionar una ventana de DeX, girar o entrar en pantalla completa la página sigue maquetada al tamaño viejo | El viewport solo se negociaba al conectar; nada lo volvía a pedir | Una sola fuente de verdad (el gestor de ventanas) leída en **cada tick**; re-maquetado inmediato y `VIEWPORT` al asentarse (140 ms) o cada 400 ms como mucho durante el arrastre | `FlexOS_BrowserApp.cpp` (`brSyncGeometry`, `brViewportFor`, `brSendViewport`) |
| "Hay que hacer click para que se actualice" | Las apps de DeX solo recibían tick con un toque o al cambiar el minuto | Tick en **cada vuelta** para todas las ventanas visibles, con toque neutro y suelta sintética | `FlexOS_Ultra_DeXDraw.h` (`dexHostTickIdle`), `FlexOS_Ultra_DeXInput.h` (`pcTick`) |
| Página cortada en x=480, negra a la derecha (ventana apaisada / horizontal) | El recorte en horizontal solo acotaba un eje; `brHostBlitRow` y `brHostClip` suponían el lienzo vertical | Recorte de los dos ejes lógicos (`gClipLY*`) y escritura girada directa | `FlexOS_Ultra_Gfx.h`, `FlexOS_Browser_Bridge.h` |
| Web gigante tras tocar un ajuste de calidad | Los ajustes mandaban `VIEWPORT 0×0` y el relay lo convertía en 120×120 | Un único constructor de `VIEWPORT` con el tamaño real; el relay trata < 120 como "sin cambio" (igual que `session.js`) | `FlexOS_BrowserApp.cpp`, `RelayEngine.kt` |
| Al re-maquetar, el navegador reaccionaba al dedo que arrastraba el borde | El re-maquetado ejecutaba la app con el toque REAL del sistema (en coordenadas del panel) | Toque neutro fuera de cualquier botón | `FlexOS_Ultra_DeXDraw.h` (`dexHostRun`) |

## 2 · Relay del teléfono (Flex Phone → P4)

| Síntoma | Causa raíz | Corrección | Dónde |
|---|---|---|---|
| m.youtube.com ampliado | El WebView se medía a W×H **píxeles del teléfono**: con densidad 2,75 su viewport CSS era de ~175 px | Vista de `W·d × H·d` px → viewport CSS de **W×H**; captura con escala `1/d`; toque y scroll en px CSS × d | `RelayTab.kt` |
| Al desplazar, la superficie entera se mueve y queda una franja blanca | `draw()` pinta el contenido desplazado `scrollX/Y` y nadie trasladaba el lienzo; el bitmap reutilizado conservaba el cuadro anterior | `translate(-scrollX, -scrollY)` y `eraseColor` por captura | `RelayTab.kt` (`capture`) |
| Fotogramas duplicados / bitmap compartido tras reconectar | Un `detach` + `attach` rápido dejaba **dos** hilos de bombeo | Una generación por arranque; el hilo viejo sale solo | `RelayEngine.kt` |
| El P4 se reinicia o pierde la Wi‑Fi y no puede volver | Solo una sesión: la reconexión se cerraba hasta que el socket viejo caducara (60 s) | La conexión nueva **autenticada** sustituye a la vieja; tope de conexiones; 30 s de silencio = P4 que ya no está | `RelayServer.kt`, `RelaySession.kt` |
| La página se pierde al reconectar | Las pestañas se destruían al irse el cliente | Se conservan un periodo de gracia (ajuste) | `RelayEngine.kt` |
| El relay se para solo | Cierre por inactividad; `dataSync` (Android 15 lo corta a las 6 h); `START_NOT_STICKY`; parada sin Wi‑Fi | `connectedDevice`, `START_STICKY` con la intención guardada, sin apagado por inactividad, espera a la Wi‑Fi, cerrojos solo con cliente y renovados | `BrowserRelayService.kt`, `AndroidManifest.xml`, `RelayServer.kt` |
| Estados confusos ("Error" al pararlo a mano) | El P4 solo tenía "error" para cualquier parada | Bits nuevos y compatibles en `RELAY_INFO` (detenido por el usuario, suspendido) y textos únicos: **Relay activo / Reconectando / Desconectado / Detenido por el usuario** | `Codec.kt`, `FlexPhoneState.kt`, `FlexOS_FlexPhone.cpp`, `FlexOS_FlexPhone_Bridge.h` |

## 3 · Avisos y Liquid Glass

| Síntoma | Causa raíz | Corrección | Dónde |
|---|---|---|---|
| Dos avisos a la vez, uno encima de otro | **Dos presentadores** independientes sobre la misma franja: la isla (sistema, solo Inicio) y el banner (teléfono, global) | **Presentador único**: el banner; la isla queda como modelo (Centro, DeX, widget) | `FlexOS_Ultra_Notif.h`, `FlexOS_FlexPhone_Overlay.h` |
| Orden arbitrario | Sin prioridad | Cola ordenada: sistema > teléfono urgente > teléfono; sin expropiar al visible | `fpbEnqueue` |
| El aviso desaparece al abrir una app, al bajar la cortina o al girar | Se **abandonaba** en vez de volver a la cola | Vuelve al frente de la cola con el tiempo que le quedaba y reaparece con el vidrio y la orientación nuevos | `fpbRequeue` |
| Vidrio con fondo viejo | Re-vidrio cada 150 ms | 80 ms cuando lo de debajo cambia (nunca por cuadro) | `FPB_REGLASS_MS` |
| "Google?Play?Store", emojis como "????" | Los espacios duro/fino de Android y cualquier carácter de 3 o 4 bytes se pintaban `?` (y los de 4 dejaban bytes sueltos) | Decodificación UTF‑8 completa + plegado tipográfico (espacios, comillas, guiones, viñetas) y caracteres invisibles sin ancho | `FlexOS_Ultra_Font.h` (`nextCP`), `FlexOS_Ultra_AppSettings.h` |

## 4 · Teclado y entrada del navegador

| Síntoma | Causa raíz | Corrección |
|---|---|---|
| En DeX el teclado caía girado sobre el escritorio | Se pintaba siempre en `fb` en vertical | Se pinta en el lienzo de la ventana; solo la marca sucia si el teclado cambia |
| "Espacio" abría Inicio, "Ir" abría Recientes | La fila de funciones quedaba debajo de la barra de navegación (no se reservaba su altura) | Reserva de la barra en la geometría del teclado |
| Tocar la barra de direcciones cerraba el teclado | Todo toque por encima del teclado lo cerraba | El campo que se edita (y la página al escribir en ella) se queda el toque |
| Pulsaciones perdidas al escribir deprisa | Solo valía un "tap" perfecto | La tecla se resuelve donde se apoyó el dedo, con tolerancia de una tecla |
| "Atrás" cerraba el navegador | No había capa de atrás del navegador en el sistema | `backLayer` del navegador: primero teclado/capas/pantalla completa, después historial |
| Restos de "AM" en la barra de estado | El marco no borraba la zona del reloj antes de repintar | Se limpia la franja antes de dibujar |

## 5 · Pantalla completa

Modo inmersivo del sistema (pedido por la app o por Inicio, aplicado por el
gestor): lienzo entero 480×800 u 800×480, barra de navegación transitoria
(gesto desde el borde inferior), "atrás" sale de pantalla completa
conservando la página. En Inicio, el menú de pulsación larga ofrece
**Pantalla completa** y **Pantalla completa horizontal** a las apps que
declaran `APP_IMMERSIVE` (hoy, el navegador).

---

## 6 · Segunda vuelta: los mismos síntomas, con las causas que seguían ahí

Tres vídeos de la placa después de la primera ronda: la web del teléfono
**seguía ampliada y la superficie se movía al deslizar**, al redimensionar el
navegador en DeX quedaban **huecos negros y viewport viejo**, el vidrio de los
avisos **se llevaba el fondo**, y los **botones de DeX solo respondían tocando
un punto exacto**. Las pruebas nuevas se han ejecutado también contra el código
anterior (mismo arnés, solo con los tres ficheros de DeX/aviso revertidos): 24
comprobaciones fallan allí y pasan todas con las correcciones.

### 6.1 · Navegador ampliado y superficie que se mueve (Relay)

Cadena revisada de punta a punta: P4 `brViewportFor` → `VIEWPORT(W,H)` →
`RelaySession` → `RelayEngine.resize` → `RelayTab.layoutFor` → WebView (viewport
CSS) → `capture` (`draw` sobre bitmap) → JPEG → `FRAME(0,0,W,H)` → P4
`brHostBlit` 1:1. El P4 ya pinta 1:1 y ya manda el viewport real (lo demuestran
`test_net` y `test_bridge`); lo que fallaba estaba en el **teléfono**, y en que
**el P4 no podía saber qué Relay tenía delante**.

| Síntoma | Causa raíz | Corrección | Dónde |
|---|---|---|---|
| La web sigue ampliada tras "corregirlo" | El APK instalado en el teléfono era **anterior** a la corrección (la app nunca subió de versión: `versionCode 1`) y el P4 no tenía forma de enterarse | El **build del Relay** viaja al final del id de sesión del `WELCOME` (`flexphone-<hex>.r6`, el formato no cambia). El P4 lo lee, avisa *"Reinstala Flex Phone: su Relay es antiguo (web ampliada)"* y lo enseña en `flex://about`; la app lo muestra en *Acerca de* | `Fbp.kt` (`tagSessionId`), `RelayEngine.kt` (`BUILD`), `FlexOS_Browser.cpp` (`flexBrRelayBuild/Kind/IsStale`), `FlexOS_BrowserApp.cpp`, `About.kt` |
| Aun con el APK nuevo, la escala podía no ser la supuesta | La densidad se **suponía** (`displayMetrics.density`); si el motor web aplica otra (tamaño de pantalla de Android, fabricante) el viewport CSS deja de ser W×H y todo sale grande | La densidad se **mide**: se pregunta a la página (`window.devicePixelRatio`) y, si difiere de la supuesta, se re-maqueta con la suya (bucle cerrado) | `RelayTab.kt` (`probeScale`, `layoutFor`) |
| Páginas sin `<meta viewport>` encogidas o con otra escala | `useWideViewPort` + `loadWithOverviewMode` activos: el motor las maquetaba a 980 px CSS y las **encogía** para que cupieran; el desplazamiento en px CSS dejaba de coincidir con lo que ve el P4 | Apagados: viewport CSS = ancho de la vista, como el servicio de Ubuntu/PC (Chromium de escritorio, `deviceScaleFactor 1`) | `RelayTab.kt` (`configure`) |
| Al deslizar se mueve toda la superficie y queda una franja | El desplazamiento era `View.scrollBy`, que **no recorta**: un deslizamiento hacia abajo en lo alto de la página dejaba `scrollY` negativo (el motor lo recortaba por dentro, la vista no) y la captura pintaba el contenido desplazado | El desplazamiento lo hace el **motor** sobre el documento (`window.scrollBy`, recorta contra los límites; con respaldo para contenedores desplazables). La vista **nunca** se desplaza: el rectángulo capturado es siempre el viewport | `RelayTab.kt` (`SCROLL_JS`, `scroll`) |
| Cuadro con el desplazamiento viejo, o en blanco | Una entrada tiene su efecto **después** de llegar (JS, composición) y se capturaba justo antes | `settle()` repite el "hay algo nuevo" a 110 y 380 ms (un único `Runnable`, sin cola) | `RelayTab.kt` |
| Bitmap auxiliar de ~9 MB por cuadro | Con `RGB_565` el motor no pinta directo y reserva uno del tamaño de la vista (W·d × H·d) en cada captura | `ARGB_8888`: pinta directo | `RelayTab.kt` (`capture`) |
| Barras, resplandor de borde, zoom del sistema | Sin desactivar `scrollbars`/`overScroll`/zoom/`textZoom` | Desactivados: nada de lo que la vista pinta encima se cuela en el cuadro | `RelayTab.kt` (`configure`) |

El build 6 también exige que el P4 lo diga: `FLEXBR_RELAY_MIN_BUILD` (6) en
`FlexOS_Browser.h`. Un Relay de otro servicio (Ubuntu/PC, id de sesión sin el
prefijo `flexphone-`) nunca se marca como antiguo.

### 6.2 · Redimensionar el navegador en DeX (grande ↔ pequeña, apaisada ↔ vertical)

| Síntoma | Causa raíz | Corrección | Dónde |
|---|---|---|---|
| Viewport / maqueta vieja hasta hacer un click | `dexHostRun` anotaba el tamaño "ya maquetado" (`rw/rh`) **también tras un tick**. El re-maquetado está frenado a 45 ms mientras se arrastra; el tick de ese mismo cuadro lo daba por hecho y el `enter()` pendiente no llegaba nunca | `rw/rh/rl` solo se anotan cuando corre `enter()`; y la **forma** de la ventana (apaisada/vertical) cuenta como cambio | `FlexOS_Ultra_DeXDraw.h` (`dexHostRun`, `dexHostRelayout`) |
| Franja negra al agrandar | El lienzo de la ventana nace a cero y la app solo pinta el área que ya conoce: la parte nueva se copiaba tal cual (negra) hasta que la app re-maquetaba | `dexHostExpose` lleva la cuenta del tamaño del que el lienzo es **bueno** y rellena con el color del cuerpo solo lo **nuevo** (la app lo pisa al re-maquetar); lo ya bueno no se toca (sin parpadeo) | `FlexOS_Ultra_DeXDraw.h` (`dexHostExpose`, `dexHostFill`) |
| Basura al girar la forma de la ventana | El mismo lienzo se lee con otro eje | Al girar no vale nada de lo que había: se rellena entero y solo se copia lo que la app dibuja después | ídem |
| La imagen vieja asoma al encoger o durante la animación de maximizar | `dexHostBlit` copiaba el área completa de salida aunque el lienzo bueno fuera menor | Solo se copia la extensión válida; el resto se queda con el color del cuerpo | `FlexOS_Ultra_DeXDraw.h` (`dexHostBlit`) |

El navegador no se recrea ni se recarga al redimensionar: el re-maquetado entra
por `navEnter(gRelayout)` → un solo `flexBrowserEnter()` en toda la sesión.

### 6.3 · Liquid Glass de los avisos

| Síntoma | Causa raíz | Corrección | Dónde |
|---|---|---|---|
| El cristal "se lleva" el fondo al entrar, salir o arrastrar | `fpbRender` resolvía el vidrio **una vez**, en el sitio de reposo, y la tarjeta se estampaba desplazada: llevaba pegada la **foto** del fondo de otro sitio. (La prueba lo muestra: con la tarjeta 250 px a un lado, el vidrio daba exactamente la misma luminancia que en reposo.) | El fondo se muestrea bajo la tarjeta **donde está ahora** (con repetición del píxel de borde fuera de la pantalla, que es lo que haría el desenfoque) y solo se vuelve a resolver si la tarjeta se movió; en reposo no cuesta nada | `FlexOS_FlexPhone_Overlay.h` (`fpbRender`, `fpbPaint`) |
| Fondo pegado al desaparecer | Es el **mismo** fallo: en la salida (la tarjeta se desliza) arrastraba la foto del fondo de reposo; no se ha podido reproducir ningún resto *después* de irse (el panel ya era `fb` tal cual) | El mismo cambio; además la marca de posición del vidrio muere con la tarjeta (`fpbRendX/Y`) y las pruebas comprueban 8 ciclos y 4 descartes con el dedo: al irse el panel es `fb` y `fpbCv`/`fpbSave` están liberados | `fpbFinish`, `fpbFreeBufs` |

### 6.4 · Botones de DeX

| Síntoma | Causa raíz | Corrección | Dónde |
|---|---|---|---|
| Hay que tocar un punto exacto en la barra de tareas | Cada control se **dibuja** como un chip de 30–34 px y se tocaba con ese mismo rectángulo exacto, sin tolerancia y sin cubrir el hueco con el vecino; los iconos de apps sí tenían margen, así que unos botones de la misma barra se tocaban bien y otros no | `dexTbHit`: **una sola** resolución para toda la barra. El toque va al control cuyo centro queda más cerca en horizontal (con alcance acotado) y vale toda la altura de la barra; el primer control llega al borde izquierdo y el reloj al derecho. La geometría sale de los mismos rectángulos que usa el dibujo | `FlexOS_Ultra_DeXInput.h` |
| Minimizar / maximizar / cerrar fallan en la esquina de la ventana | La franja de agarre **interior** del borde (6 px) tenía prioridad: arriba de los tres controles y a la derecha de "cerrar" empezaba un redimensionado en vez de pulsar el botón | Un control de la barra de título gana a la franja interior; fuera de la ventana el agarre del borde sigue mandando (`dexWinBtnAt`) | `FlexOS_Ultra_DeXInput.h` |
| Fallos con el dedo "rodando" | Un toque se atendía donde se **levantaba** el dedo (hasta 12 px de rodadura) | Un toque vale donde **empezó**, como el sistema a pantalla completa (`tDoRelease`) | `FlexOS_Ultra_DeXInput.h` (`dexPointer`) |

El diseño visual no cambia: ni un píxel de dibujo se ha tocado, solo las zonas
de toque.

---

## Cómo se ha comprobado

| Prueba | Qué demuestra |
|---|---|
| `test_ino` · `testDexTiempoReal` | tick sin toque; lo que la app dibuja llega al panel sin click; el área se actualiza en cada paso del arrastre; toque neutro y suelta sintética; maximizar a 800×480; teclado dentro de la ventana y nada fuera |
| `test_ino` · `testNotifUnaSola`, `testBannerNotificacion` (secciones 5, 6, 13, 14), `testIslaEncimaAlDeslizar` | un solo presentador, prioridad, sin expropiar, re-encolado tras cortina/giro/transición, encima de la página que se desliza, sin rastro |
| `test_ino` · `testDexRedimensionado` | arrastrar la esquina de vertical a apaisada y de vuelta con pasos de 30 ms (por debajo del límite de 45 ms): en **cada** cuadro, ni un píxel negro ni resto en el área de cliente y nada fuera de la ventana; la app re-maqueta al tamaño **final** sin tocar nada; maximizar/restaurar; el navegador con la imagen retrasada 3 ticks ve el viewport nuevo, repinta, no se recrea (un solo `enter()`) y un swipe vertical no mueve ni redimensiona la ventana |
| `test_ino` · `testDexToqueBotones` | cada píxel del dibujo de cada control de la barra y de los iconos de apps abre lo suyo, a cualquier altura de la barra; centro, esquinas y base tocados de verdad; minimizar/maximizar/cerrar en el centro y las 4 esquinas sin empezar un redimensionado; un toque vale donde empezó; el borde, la esquina y la barra de título siguen redimensionando/arrastrando |
| `test_ino` · `testBannerVidrioSigueAlFondo` | sobre un fondo oscuro/claro con borde: el vidrio es el del fondo que tiene debajo en cada posición (reposo, 150 px, 250 px, de vuelta, saliendo), `fb` nunca guarda la tarjeta, 8 ciclos alternos sin rastro del anterior y 4 descartes con el dedo sin memoria colgada |
| `test_ino` · `testTextoUnicode` | espacios Unicode, emojis, invisibles y comillas: mismos píxeles que el texto normal |
| `test_ino` · `testPantallaCompletaDesdeInicio` | menú de 5 filas solo para el navegador; apertura en vertical y horizontal con el lienzo entero; salida limpia |
| `test_net` (sockets reales) | HELLO con el área real; un redimensionado = un VIEWPORT, sin reconexión; el build del Relay llega en el `WELCOME` y un Relay antiguo provoca el aviso de reinstalar |
| `test_app`, `test_bridge` | re-maquetado, pantalla completa, teclado (reserva, toque encima, tolerancia), recorte girado |
| `test_browser` | `flexBrRelayBuild/Kind/IsStale`: mismas reglas que `Fbp.sessionBuild` (marca `.rN`, sin marca, basura, no apila marcas) |
| `test_flexphone`, `:protocol:test` (Kotlin) | bits de `RELAY_INFO` en los dos extremos; el build del Relay en el id de sesión (`tagSessionId`/`sessionBuild`) con los mismos ejemplos que el firmware |
| `gradle -PflexTypecheck :typecheck:compileKotlin` | el código del Relay compila contra Android API 35 real |

## Lo que NO se ha podido probar aquí (dicho sin rodeos)

- **Nada en la placa ESP32-P4 real**: ni la DMA2D, ni el panel MIPI-DSI, ni el
  táctil GT911, ni tiempos reales de cuadro. Las pruebas usan los dobles del
  arnés (`tests/host`) con el código real del sketch.
- **Nada en un teléfono Android real**: el Relay se ha comprobado en tipos
  (contra las clases reales de Android 35) y en el protocolo, pero no se ha
  ejecutado un WebView. En particular, sin dispositivo no está verificado: que
  el viewport CSS resultante sea exactamente W×H en todos los fabricantes, el
  comportamiento con la pantalla del teléfono apagada, `START_STICKY` en
  Android 12+ (Android puede negar el paso a primer plano desde segundo plano:
  se informa como error y se reintenta al arrancar el enlace), y el ritmo real
  de fotogramas.
- **Vídeo durante un redimensionado**: la ruta es la misma que la de la página
  (bandas JPEG), pero no hay una prueba con un flujo de vídeo real.
- Las apps de DeX que **no** son adaptativas (`APP_FLEX`) siguen escaladas con
  bandas (letterbox) dentro de la ventana: se ven y se tocan bien a cualquier
  tamaño, pero no se re-maquetan.
- **Segunda vuelta (sección 6), sin placa ni teléfono**: el **coste por cuadro**
  del vidrio de los avisos (ahora se re-muestrea el fondo mientras la tarjeta se
  mueve; en reposo no cuesta nada) y la **fluidez** de la animación de entrada
  no se han medido en la placa; el **GT911** real (deriva del dedo, ruido) no se
  ha probado, solo el reparto de las zonas de toque; y el Relay **no se ha
  ejecutado sobre un WebView real**: que el viewport CSS sea exactamente W×H y
  que el desplazamiento por `window.scrollBy` sea 1:1 está razonado y tipado,
  no medido. Para que la corrección del Relay llegue al teléfono hay que
  **reinstalar Flex Phone**: el P4 avisa si el Relay es anterior al build 6 y
  *Acerca de* muestra el build instalado.
