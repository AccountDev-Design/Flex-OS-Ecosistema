# Registro de decisiones técnicas · Navegador de Flex OS

Una entrada por decisión que no era obvia. Formato: **qué se decidió**,
**qué alternativas había** y **por qué se descartaron**. La idea es que
dentro de seis meses se pueda revisar una decisión sin tener que
reconstruir el razonamiento desde cero.

---

## D1 · Arquitectura híbrida en vez de un motor web en la placa

**Decidido.** El dispositivo pone la interfaz y la interacción; un
servicio remoto con Chromium pone el motor web y devuelve la página
rasterizada.

**Alternativas.**
1. *Un motor HTML propio en la placa.* Se puede hacer un renderizador de
   HTML sencillo, y de hecho existen (`littlevgl` + parsers, `HTMLViewer`).
   Pero «los sitios modernos» del enunciado usan CSS Grid, Flexbox, fuentes
   web y JavaScript: un renderizador de juguete enseñaría una versión rota
   de casi todo. Sería peor que no tenerlo, porque parecería que funciona.
2. *Un proxy que simplifique el HTML* (tipo Opera Mini clásico). Requiere
   igualmente un motor en la placa, y las páginas modernas se rompen al
   simplificarlas.

**Por qué la elegida.** Es la única que enseña los sitios **como son**.
El coste —depender de un servicio— se declara desde el primer momento y
la app funciona sin él (páginas internas, historial, favoritos).

---

## D2 · Decodificador JPEG propio en vez de una biblioteca

**Decidido.** `FlexOS_JPEG.cpp`, escrito desde cero, sin dependencias.

**Alternativas.**
1. *`esp32-camera` (`jpg2rgb565`).* Solo está garantizada donde hay
   soporte de cámara; en Flex OS Pro y en el P4 no se puede dar por hecho.
2. *tjpgd de ROM.* No expone la misma API en las tres familias de chip.
3. *JPEGDEC / TJpg_Decoder de Arduino.* Añaden una dependencia externa que
   el usuario tendría que instalar a mano, y el proyecto no usa ninguna.

**Por qué la elegida.** (a) Compila igual en P4, S3, ESP32 clásico **y en
un PC**, que es lo que permite probarlo de verdad: 191 comprobaciones
contra libjpeg-turbo, con sanitizers. (b) Decodifica por filas de MCU y
entrega con callback, así que nunca existe la imagen entera en memoria.
(c) Escala 1/2, 1/4 y 1/8 durante la decodificación.

**Coste asumido.** ~600 líneas propias que hay que mantener. Mitigado con
la batería de pruebas.

---

## D3 · Solo baseline; el progresivo se rechaza

**Decidido.** El decodificador acepta SOF0/SOF1 y rechaza SOF2
(progresivo) con un error claro. El servicio codifica siempre con
`progressive: false`.

**Por qué.** Un decodificador progresivo necesita el coeficiente completo
de la imagen en memoria antes de poder pintar nada: para 480×800 en 4:2:0
son ~1,1 MB de `int16`. Eso no cabe en Flex OS Pro y en el resto sería
tirar PSRAM. Rechazarlo con un mensaje es mejor que decodificarlo mal.

---

## D4 · Ampliación de croma por vecino más próximo

**Decidido.** Al convertir 4:2:0 / 4:2:2 a RGB se replica la muestra de
croma vecina, en vez de interpolar.

**Alternativa.** La interpolación triangular («fancy upsampling») de
libjpeg, que es lo que hace la referencia contra la que se compara.

**Por qué.** Cuesta la mitad de CPU y no necesita mantener una fila extra
de croma. El coste medido está en `docs/NAVEGADOR.md`: en una página real
el error medio es de 0,32 pasos de RGB565 y el máximo, 4 pasos, en el
borde de un bloque de color saturado. En texto sobre fondo claro —que es
el 95 % de lo que se ve en un navegador— el croma es casi constante y la
diferencia es cero.

---

## D5 · La caché de página es el JPEG comprimido, no el mapa de bits

**Decidido.** Al repintar (cerrar un menú, salir del reproductor) se
vuelve a decodificar el JPEG guardado en vez de copiar un framebuffer.

**Alternativa.** Guardar la página descomprimida en RGB565.

**Por qué.** 480×800×2 = 768 KB por pestaña. En Flex OS Pro no cabe, y en
Ultra son 768 KB de PSRAM por pestaña que se pueden usar para otra cosa.
El JPEG equivalente son 20–60 KB. El coste es CPU en un repintado
ocasional, **no** por fotograma.

**Consecuencia.** En Pro, que no tiene PSRAM ni para eso, no hay caché:
al repintar se pide un fotograma nuevo al servicio (`REQ_FRAME`).

---

## D6 · Bandas horizontales en vez de la página entera

**Decidido.** El servicio parte cada actualización en bandas de ~200 px y
solo envía las que han cambiado.

**Por qué no la página entera.** El dispositivo decodifica en el **hilo
gráfico**, el mismo que atiende el táctil. Decodificar 480×800 de golpe
lo deja mudo durante toda la operación. Una banda tarda decenas de
milisegundos, y entre banda y banda se vuelve a mirar el dedo. La
prioridad, tal como pide el enunciado, es que el tacto se sienta
responsivo, no la tasa de fotogramas.

**Efecto secundario bueno.** Detectar bandas sucias sale casi gratis
(un hash por banda) y ahorra la mayor parte del tráfico: al desplazarse
por texto, la mayoría de las bandas repiten.

---

## D7 · Contrapresión con un solo fotograma en vuelo

**Decidido.** El servicio no genera el siguiente fotograma hasta que el
dispositivo confirma el anterior. El dispositivo, si le llega uno
mientras aún tiene otro sin dibujar, **descarta el viejo**.

**Alternativa.** Una cola con N fotogramas.

**Por qué.** Una cola solo sirve para dibujar tarde cosas que ya no valen.
Además, mientras el dispositivo no consume, la tarea de red no lee del
socket, así que la ventana TCP frena al servicio sola: la contrapresión
es real, no un contador.

---

## D8 · El escalado del viewport lo aplica el dispositivo al volcar

**Decidido.** El servicio puede rasterizar a menos píxeles de los que la
página ocupa en pantalla; el dispositivo amplía al volcar cada fila.

**Por qué.** En Flex OS Pro el lienzo lógico mide 480×640 pero el panel
físico es de 240×320. Pidiendo el 50 % se envía **exactamente** lo que la
pantalla puede mostrar: la mitad de bytes y la mitad de trabajo de
decodificación, con el mismo resultado visible. Sin esto, el Pro
decodificaría el doble de píxeles para tirar la mitad.

**Por qué no bajar el `deviceScaleFactor` de Chromium.** Cambiaría la
maquetación de la página (los medios de CSS reaccionan al ancho en
píxeles CSS). Renderizar a tamaño completo y reducir la imagen mantiene
la página tal cual y solo baja la resolución.

---

## D9 · WebSocket escrito a mano

**Decidido.** Cliente RFC 6455 propio dentro de `FlexOS_BrowserApp.cpp`,
con SHA-1 y base64 propios en el núcleo portable.

**Alternativas.** `arduinoWebSockets`, `ESP32-WebSocket`, el cliente de
ESP-IDF.

**Por qué.** Mismo argumento que D2: ninguna está garantizada en las tres
familias sin instalar algo a mano, y lo que hace falta es pequeño
(binario, sin extensiones, sin compresión). Poner SHA-1 y base64 en el
núcleo portable permite probarlos contra los vectores del RFC 3174 y del
propio RFC 6455 — si el `Sec-WebSocket-Accept` estuviera mal, el síntoma
en la placa sería «no conecta», sin más pista.

---

## D10 · Bloquear TODAS las IP escritas a mano, no solo las privadas

**Decidido.** `http://93.184.216.34/` —una IP pública perfectamente
legítima— también se bloquea por defecto.

**Por qué.** Aceptar IP literales obliga a acertar con todas las formas de
escribirlas (`2130706433`, `0x7f000001`, `010.0.0.1`, `::ffff:10.0.0.1`)
y con todos los rangos reservados que vayan apareciendo. Bloquearlas
todas es una regla que no se puede rodear y que un usuario normal no
nota. Quien de verdad necesite una IP, activa la opción de desarrollo o
la pone en la lista blanca del servicio.

---

## D11 · Dos barreras anti-SSRF independientes

**Decidido.** El dispositivo valida antes de enviar y el servicio valida
otra vez, con resolución DNS y comprobación de **cada** redirección.

**Por qué.** El dispositivo no puede resolver DNS de forma fiable ni saber
en qué red está el servicio. El servicio sí. Y al revés: si alguien
alcanzara el servicio con otro cliente, la barrera del dispositivo no
serviría. Cada una cubre lo que la otra no.

**Lo que ninguna de las dos cierra.** Entre la resolución DNS y la
conexión de Chromium hay una ventana para un *DNS rebinding*. Eso solo se
cierra con aislamiento de red, y así está escrito en `server/README.md`
en vez de dar la impresión de que el código basta.

---

## D12 · El teclado del omnibox vive en el puente, no en el módulo

**Decidido.** `FlexOS_Browser_Bridge.h` dibuja y gestiona el teclado
usando las funciones `kb*` del `.ino`.

**Alternativa.** Un teclado propio dentro del módulo.

**Por qué.** El teclado de FlexOS tiene cuatro capas, tamaños
configurables, alto contraste, opacidad y estilo. Reimplementarlo sería
un segundo teclado que se desincronizaría del primero al primer cambio.
El puente es el único sitio que ve a la vez el módulo y los `static` del
`.ino`, así que es donde toca.

**Además:** el teclado se abre con `kbExtrasOn = false`, igual que hace la
pantalla de contraseña del Wi-Fi. La barra de accesos trae el
**portapapeles**, y un portapapeles a un toque dentro de un campo que
puede ser la contraseña de un sitio web es un agujero, no una comodidad.

---

## D13 · Lo que se teclea en una página no se guarda en el dispositivo

**Decidido.** En modo «escribir en la página» cada pulsación se envía al
servicio inmediatamente y **no** se acumula en ningún buffer local.

**Por qué.** Si se guardara, una contraseña escrita en un formulario
quedaría en la RAM del navegador hasta el siguiente uso del teclado. No
guardarla es gratis y elimina el problema.

---

## D14 · Historial y favoritos solo viven en RAM con la app abierta

**Decidido.** Se reservan en `flexBrowserEnter()` y se liberan en
`flexBrowserExit()`; el contenido vive en LittleFS.

**Alternativa (la primera versión).** Reservarlos en `flexBrowserBegin()`,
que corre en el arranque del sistema.

**Por qué se cambió.** `Begin()` no se deshace nunca, así que eso dejaba
memoria ocupada de forma permanente por una app que quizá no se abre. En
Flex OS Pro son ~21 KB de **RAM interna**, que es justo la que después
necesita mbedTLS. Volver a leerlos al abrir cuesta una lectura de
LittleFS. Hay un test que comprueba que `Begin()` no reserva nada.

---

## D15 · Dos umbrales de heap, no uno

**Decidido.** `FLEXBR_TLS_HEAP` (lo que hace falta al conectar) y
`FLEXBR_MIN_FREE_HEAP` (lo anterior más lo que el navegador va a reservar
del heap interno, que es 0 en las placas con PSRAM).

**Por qué se cambió.** Con un solo umbral, Flex OS Pro daba un falso
positivo: pasaba la comprobación al abrir la app, reservaba 54 KB y
después TLS ya no cabía. El usuario habría visto reintentos infinitos sin
explicación.

---

## D16 · Escrituras en flash agrupadas

**Decidido.** Historial y favoritos se marcan «sucios» y se vuelcan como
mucho cada 20 s, y siempre al cerrar la app. Los ajustes van a NVS solo
cuando cambian.

**Por qué.** Escribir en cada navegación desgastaría la flash sin motivo
—una sesión de media hora son decenas de escrituras— y cada escritura
apaga la caché de los dos núcleos, que es justo lo que produce el tirón
en pantalla que este proyecto ya arregló una vez en el OTA. Hay un test
que comprueba que 200 `tick()` seguidos no escriben nada.

---

## D17 · La app conserva el marco estándar de Flex OS

**Decidido.** El navegador se registra con `APP_FLEX | APP_OWN_TOUCH`,
**sin** `APP_CUSTOM_HEADER`.

**Por qué.** Así el framework sigue pintando la barra de estado, la de
navegación y la cabecera con el nombre de la app: el navegador se ve como
una app más de Flex OS y no como algo pegado. Los 96 px de cabecera son
un coste, pero la coherencia visual del sistema lo compensa.

`APP_OWN_TOUCH` es necesario porque el botón *atrás* debe retroceder en
el historial de la página antes de cerrar la app, y eso el framework no
lo puede saber. Los dos puntos de salida (chevron y barra de navegación)
se atienden a mano en `navTick()`.

---

## D18 · `appClose()` libera los recursos del navegador

**Decidido.** Una línea en `appClose()` de cada `.ino`:
`if(gAppId == IC_NAV) flexBrowserExit();`

**Por qué.** Hay cinco vías de cierre distintas (botón atrás, chevron,
gesto de la barra iOS, menú del propio navegador, cierre de la ventana de
DeX) y `appClose()` es el único punto por el que pasan todas. Poner la
liberación en cada vía por separado garantizaba olvidarse de una.

---

## D19 · El vídeo es MJPEG y no tiene audio

**Decidido.** El reproductor recibe fotogramas JPEG del elemento
`<video>` y dice explícitamente «sin audio» en pantalla.

**Por qué.** Se buscó en los tres `.ino`: no hay ni una referencia a I2S,
a `dacWrite` ni a ningún pin de altavoz. **Ninguna de las tres placas
tiene salida de audio cableada.** Y ninguna tiene decodificador de vídeo
por hardware. Con eso, MJPEG es lo único que se puede mostrar de verdad.

Poner un control de volumen que no hiciera nada habría sido peor que no
ponerlo. Se dibuja «sin audio» en su sitio.

---

## D20 · DRM y YouTube: fuera, y sin rodeos

**Decidido.** No se elude ninguna protección. Con contenido protegido,
Chromium entrega fotogramas en negro y eso es lo que se envía. La URL de
origen del vídeo **no** se reenvía al dispositivo.

**Por qué.** Es lo que pide el enunciado y además es lo correcto. Netflix,
Disney+ y Widevine quedan fuera de alcance y así está escrito en la
documentación y en los comentarios del código, no como una limitación
técnica sobrevenida sino como una decisión.

---

## D21 · Las ventanas emergentes se bloquean

**Decidido.** `page.on('popup')` cierra la ventana y avisa al dispositivo.

**Por qué.** Abrir pestañas sin pedirlo en una pantalla de 480×800, con
un máximo de 1 a 6 pestañas y con memoria contada, es insoportable. Se
avisa para que el usuario sepa que ha pasado algo.

---

## D22 · Entorno Arduino simulado para probar el código de placa

**Decidido.** `tests/host/stub/` con las cabeceras mínimas de Arduino,
FreeRTOS, WiFi y Preferences, y dos pruebas (`test_app`, `test_bridge`)
que compilan y ejecutan el código de dispositivo en el PC.

**Alternativa.** No probar esa parte hasta tener la placa.

**Por qué.** Son ~2.000 líneas de código de dispositivo. Sin esto, el
primer error de compilación aparecería en el IDE del usuario, y el primer
error de lógica —una fuga al cerrar la app, un dibujo que se sale del
área— en la placa, sin depurador. Con el simulador se comprueban en el
PC, y además en **los tres perfiles de placa**, que es donde cambian los
presupuestos.

**Riesgo asumido y anotado.** Si una firma del simulador se separa de la
real de arduino-esp32, el código compilaría aquí y fallaría allí. Por eso
las firmas están copiadas literalmente y `stub/README.md` lo dice.

---

## D23 · Comparar el decodificador en pasos de RGB565, no en niveles de 8 bits

**Decidido.** Las tolerancias del test se miden en pasos del formato de
pantalla (0..31 en rojo/azul, 0..63 en verde).

**Por qué.** En 8 bits, un único paso de diferencia en rojo «vale» 8, así
que un decodificador perfecto salvo redondeo daría números grandes y el
test no distinguiría eso de un fallo de verdad. En pasos de 565, 0 es
idéntico y 1 es el mínimo representable: la métrica dice algo. Con ella,
las rejillas de prueba dan error **exactamente 0** y las fotos reales,
0,07–0,47 de media.
