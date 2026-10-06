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

## Cómo se ha comprobado

| Prueba | Qué demuestra |
|---|---|
| `test_ino` · `testDexTiempoReal` | tick sin toque; lo que la app dibuja llega al panel sin click; el área se actualiza en cada paso del arrastre; toque neutro y suelta sintética; maximizar a 800×480; teclado dentro de la ventana y nada fuera |
| `test_ino` · `testNotifUnaSola`, `testBannerNotificacion` (secciones 5, 6, 13, 14), `testIslaEncimaAlDeslizar` | un solo presentador, prioridad, sin expropiar, re-encolado tras cortina/giro/transición, encima de la página que se desliza, sin rastro |
| `test_ino` · `testTextoUnicode` | espacios Unicode, emojis, invisibles y comillas: mismos píxeles que el texto normal |
| `test_ino` · `testPantallaCompletaDesdeInicio` | menú de 5 filas solo para el navegador; apertura en vertical y horizontal con el lienzo entero; salida limpia |
| `test_net` (sockets reales) | HELLO con el área real; un redimensionado = un VIEWPORT, sin reconexión |
| `test_app`, `test_bridge` | re-maquetado, pantalla completa, teclado (reserva, toque encima, tolerancia), recorte girado |
| `test_flexphone`, `:protocol:test` (Kotlin) | bits de `RELAY_INFO` en los dos extremos |
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
