# Flex OS Ultra — ESP-IDF · Informe de migración

Rama: `flexos-esp-idf` · Carpeta: `FlexOS_Ultra_IDF/` · Referencia funcional (no se modifica):
`FlexOS_Ultra/` en la rama `claude/flexos-ota-system-blq9g9-2csacx`.

> **NADA ESTÁ PROBADO EN HARDWARE REAL.** Mientras una funcionalidad no tenga evidencia física
> (log, foto o vídeo de la placa), su columna "Hardware probado" dice **NO**. "Implementado" significa
> compilado para `esp32p4` y verificado en el PC hasta donde se puede (pruebas de host, simulador,
> lectura del código fuente de ESP-IDF).

## Estado por fase

| Fase | Contenido | Estado | Informe |
|---|---|---|---|
| 0 | Infraestructura: proyecto, sdkconfig por revisión, particiones provisionales, herramientas | Hecha | `docs/FASE_0_1_INFORME.md` |
| 1 | Pantalla ST7701 por MIPI-DSI, 2 framebuffers, LVGL DIRECT, GT911, pantalla de prueba | Hecha | `docs/FASE_0_1_INFORME.md` |
| 2 | Arquitectura base: bus de eventos, buzón de UI, almacenamiento (NVS + LittleFS compatibles), ajustes, monitor del sistema, lógica portable | Hecha | `docs/FASE_2_INFORME.md` |
| 3 | Interfaz completa en LVGL | En curso (shell: bloqueo, clave, escritorio, caja de apps, recientes, energía, teclado, panel rápido, avisos: banner, Centro de notificaciones y No molestar; apagado completo con deep sleep y filtro de encendido; Modo seguro; restablecimiento de fábrica; primera configuración; widgets como Arduino; menú contextual del escritorio; Modo kiosco; Modo edición del escritorio; Personalizar inicio). Las 19 apps siguen sin migrar (pantalla "Pendiente de migrar") | `docs/spec/` |
| 4 | Liquid Glass (3 niveles) | Parcial: superficies de vidrio sobre fondo y sobre color liso en uso; bandas visibles y PPA sin medir | `docs/spec/02` |
| 5 | Táctil y gestos | Parcial: arbitraje antes de LVGL, bordes del sistema, suspensión con dos dedos; falta el veto al teclear y los umbrales de toque de Arduino | `docs/spec/01b` |
| 6 | Wi-Fi por ESP32-C6 (esp-hosted) | Pendiente | |
| 7 | Flex Account | Pendiente | |
| 8 | Flex Cloud | Pendiente | |
| 9 | Multimedia | Pendiente | |
| 10 | Cámara | Pendiente | |
| 11 | Audio ES8311 | Pendiente | |
| 12 | Device Care | Pendiente (backend de métricas hecho en la Fase 2) | |
| 13 | Flex Compass | Pendiente | |
| 14 | OTA con identificadores propios | Pendiente | |
| 15 | Particiones A/B con el tamaño medido | Pendiente | |
| 16 | Revisión de seguridad y estabilidad | Pendiente | |
| 17 | Optimización medida | Pendiente | |

## Matriz de funcionalidades

| Funcionalidad | Arduino | ESP-IDF | Estado | Hardware probado |
|---|---|---|---|---|
| Home | Escritorio por páginas, widgets, edición | Escritorio LVGL (páginas, dock, widgets), bloqueo, clave, caja de apps, Recientes, panel rápido, avisos (banner + Centro + No molestar), menú contextual, Modo kiosco, Modo edición; Personalizar inicio (sin "Mis imágenes" como fondo) | En curso (Fase 3) | NO |
| Interfaz LVGL | Motor gráfico propio (no se reutiliza) | LVGL 9.6 DIRECT, 2 FB en PSRAM, solo LVGL dibuja (`check_lvgl_only.py`) | Base hecha (Fase 1); pantallas pendientes | NO |
| Liquid Glass | Desenfoque propio con caché | Superficies de vidrio LVGL (fondo desenfocado, tinte adaptativo, luz y borde); bandas visibles | Parcial (Fase 4) | NO |
| Táctil | GT911 en el bucle | GT911 en tarea propia, 5 dedos, recuperación del bus; arbitraje del sistema antes de LVGL (bordes, suspensión) | Parcial (Fases 1 y 5) | NO |
| Wi-Fi | esp-hosted (C6) | — | Pendiente (Fase 6) | NO |
| Flex Account | ES256, claves fijadas | Lógica portable copiada (`FlexAuth`) | Pendiente (Fase 7) | NO |
| Flex Cloud | Control + streaming | Lógica portable copiada (`CloudCore`) | Pendiente (Fase 8) | NO |
| OTA | Manifiesto + MD5, descarga por streaming, sin firma ni rollback | Motor portable probado contra `FlexOS_OTA.cpp` extraido (4 configuraciones), interfaz LVGL de cuatro capas, transporte con `esp_http_client` (CA fijado), `esp_ota`, MD5 de lo escrito, proyecto de la imagen y confirmacion de arranque con el rollback del bootloader | Hecho en codigo y simulador; el transporte solo contra dobles de ESP-IDF (faltan el Wi-Fi X3, el servidor y la flash reales) | NO |
| Cámara | Sin sensor identificado ("SIN SEÑAL") | — | Pendiente (Fase 10) | NO |
| Audio | ES8311 + I2S | — | Pendiente (Fase 11) | NO |
| Vídeo | AVI MJPEG | Lógica portable copiada (`Media`, `VidEdit`) | Pendiente (Fase 9) | NO |
| Galería | Rejilla, visor, editor | Lógica portable copiada (`MediaLib`, `MediaStore`, `MediaThumb`, `ImgEdit`, `JPEG`, `JPEGEnc`) | Pendiente (Fase 9) | NO |
| Música | Biblioteca + reproductor | — | Pendiente (Fases 9/11) | NO |
| Device Care | Estado, diagnóstico, caídas, salud… | Monitor del sistema (heap, PSRAM, temperatura, CPU por núcleo, tareas, reinicio, coredump) | Backend hecho (Fase 2); UI pendiente (Fase 12) | NO |
| Compass | BNO085 + AHRS | Lógica portable copiada (`FallDetect`, `Theft`) | Pendiente (Fase 13) | NO |
| Almacenamiento | NVS (Preferences) + LittleFS | NVS compatible (mismos tipos y claves) en caché + LittleFS compatible (probado v2.8 ↔ v2.11), escritor único, sin borrados automáticos | Hecho (Fase 2) | NO |
| Seguridad | Passcode, grants, firmas | Lógica portable copiada (`AppGrant`, `FlexAuth`, `StorageCore`); reparación de datos solo con ficha de confirmación | Parcial; revisión completa en la Fase 16 | NO |

## Tamaño medido (por fase)

| Fase | Variante | Firmware | % ranura A (2,5 MB) | % ranura B (4 MB) |
|---|---|---|---|---|
| 1 | `lt_v3` dev | ver `docs/FASE_0_1_INFORME.md` | | |
| 2 | `lt_v3` dev | 918 864 B | 35,1 % | 21,9 % |
| 2 | `v3` dev | 921 056 B | 35,1 % | 22,0 % |
| 3 (en curso: shell, panel rápido, avisos, apagado) | `v3` dev | 1 341 632 B | 51,2 % | 32,0 % |
| 3 (en curso: + Modo seguro) | `v3` dev | 1 351 200 B | 51,5 % | 32,2 % |
| 3 (en curso: + restablecimiento de fábrica) | `v3` dev | 1 369 152 B | 52,2 % | 32,6 % |
| 3 (en curso: + primera configuración) | `v3` dev | 1 372 432 B | 52,4 % | 32,7 % |
| 3 (en curso: + revisión 5, widgets como Arduino) | `v3` dev | 1 377 584 B | 52,6 % | 32,8 % |
| 3 (en curso: + menú contextual y Modo kiosco) | `v3` dev | 1 384 400 B | 52,8 % | 33,0 % |
| 3 (en curso: + Modo edición del escritorio) | `v3` dev | 1 391 504 B | 53,1 % | 33,2 % |
| 3 (en curso: + Personalizar inicio) | `v3` dev | 1 418 448 B | 54,1 % | 33,8 % |
| 3 (en curso: + sesiones de app) | `v3` dev | 1 420 592 B | 54,2 % | 33,9 % |
| 3 (en curso: + memoria y Optimizar) | `v3` dev | 1 429 152 B | 54,5 % | 34,1 % |
| 3 (en curso: + arranque) | `v3` dev | 1 430 800 B | 54,6 % | 34,1 % |
| 3 (en curso: + Recientes con memoria y sesiones) | `v3` dev | 1 431 568 B | 54,6 % | 34,1 % |
| 3 (en curso: + Calculadora y correcciones de la revisión 6) | `v3` dev | 1 443 840 B | 55,1 % | 34,4 % |
| 3 (en curso: + kit de componentes, Calendario y kit de archivos con su capa de operaciones) | `v3` dev | 1 454 976 B | 55,6 % | 34,7 % |
| 3 (en curso: + Ajustes del teclado con su editor de atajos) | `v3` dev | 1 548 032 B | 59,1 % | 36,9 % |
| 3 (en curso: + Reloj y Cronometro con su capsula y su tarjeta) | `v3` dev | 1 564 656 B | 59,7 % | 37,3 % |
| 3 (en curso: + Almacenamiento y Archivos: medidas asincronas, detalles, explorador modal con filas por ventana) | `v3` dev | 1 600 768 B | 61,1 % | 38,2 % |
| 3 (en curso: + Juegos: Jumper de Arduino sobre el motor de dibujo de Arduino extraido, con su fuente de 31 KB) | `v3` dev | 1 690 576 B | 64,5 % | 40,3 % |
| 3 (en curso: + Device Care: pantallas como operaciones de dibujo con el pintor LVGL, historial persistente y diagnostico) | `v3` dev | 1 736 608 B | 66,2 % | 41,4 % |
| 3 (en curso: + hora por red: sockets y DNS de lwIP, servicio y trabajador) | `v3` dev | 1 829 696 B | 69,8 % | 43,6 % |
| 3 (en curso: + actualizaciones OTA: cliente HTTP/TLS —`esp_http_client`, mbedtls—, `esp_ota`, interfaz de cuatro capas y transporte) | `v3` dev | 2 076 864 B | 79,2 % | 49,5 % |

La tabla de particiones sigue **provisional (A)** hasta medir el firmware con la interfaz completa
(Fase 15). No se ha grabado nada.

## Diferencias de implementación respecto a Arduino (documentadas)

| Área | Arduino | ESP-IDF | Motivo |
|---|---|---|---|
| Ajustes | `Preferences.put*` escribe al momento | Caché en RAM + escritura diferida 300 ms por un único escritor | La UI nunca espera a la flash |
| LittleFS sin montar | `LittleFS.begin(true)` **formatea** si no monta | Nunca formatea; avisa y pide confirmación | No perder datos del usuario |
| Gráficos | Motor propio sobre framebuffer | Solo LVGL | Requisito del proyecto |
| Orden de grabado de ajustes | Cada `put*` va a la flash al momento | El escritor único graba en el **orden de los cambios** (no en el de la lista) | Quien encadena escrituras dependientes (la clave) cuenta con ese orden si se corta la corriente |
| Cambiar la clave del bloqueo | Cinco claves NVS sin orden garantizado: un corte entre la sal y el hash nuevos deja una pareja que no abre con ninguna clave | Diario atómico `lockjrn` (un único blob con todo) grabado y confirmado **antes** de tocar las claves de siempre; el arranque completa un cambio a medias. Quitar la clave pone "sin clave" antes de borrar el hash | Probado con un corte en cada escritura posible (4 estados × 3 operaciones): tras el arranque abre la de antes o la nueva, nunca ninguna, y Arduino ve lo mismo. Arduino no lee `lockjrn` |
| NVS que no arranca (sin páginas libres) | `Preferences` falla y todo vale por defecto (el bloqueo desaparece) | Se lee en **solo lectura** (copia de la partición marcada `readonly`: NVS no repara ni escribe nada) y los ajustes, incluida la clave, siguen en vigor; los cambios quedan en RAM. Si ni así se puede leer, valores por defecto como en Arduino | No borrar datos sin confirmación y no abrir el bloqueo por un fallo de la flash. Límite: con NVS ilegible del todo el bloqueo no se aplica (igual que Arduino) |
| Despertar con clave | El bloqueo se dibuja y se enciende en el mismo bucle | El panel se enciende (DISPON + fundido) solo cuando el cuadro del bloqueo ha salido entero por el DPI (dibujado + 2 fines de cuadro, máx. 250 ms) | Con DPI el panel muestra lo que llega: encender antes dejaría ver 1–2 cuadros de la app |
| Bloqueo con clave mientras cae (inactividad) | La caída bloquea el bucle: no se lee el táctil | Un escudo bajo el bloqueo se traga los toques a lo de debajo; un toque sobre el bloqueo lo termina en el acto; con el aparato bloqueado no se abre ninguna app | En LVGL la caída es una animación y el tactil sigue vivo: sin esto, un toque la congelaba a medias y el escritorio quedaba usable sin PIN |
| Candado de app a medio verificar | Cuenta como "ya bloqueado": ni el despertar ni la inactividad bloquean, y "atrás" deja el escritorio | Solo la verificación que sale del bloqueo cuenta; con un candado de app abierto, despertar o la inactividad bloquean de verdad | Hueco heredado de Arduino |
| Recientes con app con candado | Reabre la app sin pedir la clave | Pasa por la misma puerta que el escritorio: pide la clave | Hueco heredado de Arduino |
| Diario de la clave | — | v2: guarda la huella de lo que había (tipo, sal y hash). El arranque solo completa un cambio de ESP-IDF; si Arduino cambió o quitó la clave después, el diario se descarta | Nunca pisar datos que escribió el firmware Arduino |
| Derivar la clave | El error del HMAC se ignora | Un fallo del HMAC (sin memoria) hace fallar la operación; al guardar se deriva dos veces y se compara antes de escribir nada | Un hash que no es el de la clave dejaría al usuario fuera |
| Escritor de ajustes | — | Una escritura que falla (NVS llena) se salta en esa pasada y el resto sigue; dentro del grupo de la clave (`lock*`) nada se adelanta a la que falló. Los buffers de la caché se borran con ceros antes de liberarse | Que un ajuste que no cabe no bloquee el contador de fallos ni la clave; no dejar secretos en el heap |
| Panel rápido: qué controles salen | `qpNormalize` quita del blob lo no disponible en ese momento (sin PIN, "Bloquear" desaparece para siempre al pulsar "Listo") | Se conserva todo lo que puede existir en la placa y solo se OCULTA lo que no funciona ahora (sin backend migrado o por estado) | docs/spec/01b §5.11 |
| Panel rápido: borde derecho | Exige el cuadro del apoyo y nunca se dispara | Se evalúa con el dedo moviéndose hacia dentro (más que en vertical); lo que LVGL estuviera desplazando vuelve a su punto de encaje | docs/spec/01b §5.1 |
| Panel rápido: "Modo oscuro" | Encendido = tema CLARO | Encendido = oscuro | docs/spec/01b §5.3 |
| Panel rápido: fondo sobre una app | Captura de 768 KB de la app, reducida y desenfocada | El color de ventana de la app bajo el mismo velo (el desenfoque de una pantalla de app es casi su color); sobre el escritorio, su fondo ya desenfocado | Sin 768 KB extra de PSRAM por cada apertura |
| Panel rápido: dos dedos sobre un control | El episodio que el arbitraje se traga (gesto de suspender) se leía como "soltar sin mover" y ejecutaba el control (cambiaba el tema, abría la cámara) | Un episodio anulado no es un toque; tampoco en el editor ni en el catálogo | docs/spec/01b §5 |
| Panel rápido: atajos a apps (Ajustes, Cámara, Galería, Modo PC, Archivos, Conectividad) | Abren la app | Abren la misma entrada que el icono del escritorio; mientras la app no esté migrada es la pantalla "Pendiente de migrar a ESP-IDF" (honesta). Cronómetro sí se oculta: es un interruptor con estado propio (cronoStart/Pause), no un atajo | Revisión adversarial 3 (hallazgo descartado con motivo) |
| Apagado: filtro de encendido | Sin dedo espera la ventana entera (4,2 s) antes de volver a dormir: con el despertar por temporizador el chip está despierto ~91 % del tiempo | Vuelve a dormir tras 300 ms sin dedo (al empezar o al levantarlo); levantarlo menos solo reinicia la cuenta de 3 s. El GT911 se busca sin pulso de reset (siguió escaneando con RST retenido) | docs/spec/01c §10.6 |
| Apagado: Cancelar | Vuelve siempre a Inicio, aunque se viniera de una app (que quedaba viva debajo) | Vuelve a donde se estaba (Inicio o la app delante) | docs/spec/01c §10.8 |
| Apagado: con "Apagado seguro" | La animación arranca desde la pantalla de la clave | Arranca desde el deslizador (la clave se cierra al acertar) | Visual |
| Apagado: durante la animación final | El doble toque con dos dedos podía suspender a medias | No se suspende ni se bloquea: la animación termina y duerme | docs/spec/01c §10.5 |
| Modo seguro: acceso | Entra sin pantalla de bloqueo y ofrece escritorio, Ajustes y Explorador sin clave (3 reinicios anormales provocados = acceso sin PIN) | Toda fila que da acceso pide la clave (una vez por arranque); "Reiniciar normalmente" no | docs/spec/01c §11.6 (riesgo) |
| Modo seguro: pie de la pantalla | "Un arranque estable limpia el contador solo" (en Modo seguro no ocurre nunca) | "Para salir: Reiniciar normalmente" | docs/spec/01c §11.6 |
| Modo seguro: qué es anormal | PANIC, INT_WDT, TASK_WDT, WDT, BROWNOUT | Además CPU_LOCKUP y PWR_GLITCH (el P4 los informa) | docs/spec/01c §11.1 |
| Modo seguro: bloquear | — | Bloquear (p. ej. al despertar con clave) cierra la pantalla; al desbloquear, escritorio limitado con su píldora | Coherencia |
| Modo seguro: textos | Sin tildes ("minimo", "caches", "fabrica") | Con tildes | Visual |
| Modo seguro: "Reiniciar normalmente" sin poder grabar | Escritura síncrona y reinicio | Se graba fuera de la interfaz (puede esperar a "Limpiar cachés" en la cola del escritor); si no se puede grabar NO reinicia (volvería en silencio al Modo seguro) y avisa "No se pudo reiniciar" | Revisión adversarial 5 |
| Caja de apps: "Añadir a inicio" con las páginas llenas | La fila dice "Inicio completo" y está atenuada, aunque la acción (`drwFavToggle`) crearía otra página | "Añadir a inicio" activa mientras queden páginas por crear (crea la página); "Inicio completo" solo con las 5 páginas llenas | Revisión adversarial 5 (coherencia con `drwFavToggle`) |
| Kiosco y Modo seguro | El arranque va al Modo seguro pero el kiosco sigue activo: "Ir al escritorio" da un escritorio con los vetos del kiosco | En Modo seguro el kiosco no se aplica en ese arranque (la NVS no cambia; el Modo seguro ya pide la clave); "Reiniciar normalmente" vuelve a la app clavada | docs/spec/01c §11.6 |
| Kiosco: al salir | La app clavada queda "en marcha" con el escritorio delante | Se suspende por el camino normal (pasa a Recientes) | docs/spec/01c §12.5 |
| Kiosco: textos | Sin tildes ("tactil", "manten") | Con tildes | Visual |
| Candados de app: guardado | `applockm` se leía en 16 bits: los de Flex Phone, Device Care y Música se perdían al reiniciar | 32 bits; se guarda con el escritorio | docs/spec/01a §5.13 |
| Menú contextual: "Pantalla completa" | Activa en el Navegador | Se ve atenuada hasta migrar las apps inmersivas (marco de apps) | Pendiente |
| Modo edición: quitar un widget | Tocar la insignia lo quita y, al soltar, el toque cuenta como "en vacío": se sale del Modo edición | Se quita y se sigue editando | docs/spec/01a §8 (toque consumido) |
| Modo edición: diseño bloqueado | Tocar un widget lo selecciona y al soltar se deselecciona (no se ve nunca) | Queda seleccionado (lo que dice la especificación) | docs/spec/01a §8 |
| Modo edición: suspender | Sale del modo | Igual; también al bloquear y con Atrás/Inicio/Recientes | — |
| Widgets: agujas del reloj analógico | Trazo con antialias de 2,4 px (hora) y 1,8 px (minutos) | 2 px las dos (LVGL usa anchos enteros); la hora se distingue por la longitud, como en Arduino | Visual |
| Candado de app pedido desde otra app (rueda del panel rápido) | Cancelar la clave llevaba a Inicio | Vuelve a la app que estaba delante (ir a Inicio la dejaba huérfana y Recientes se trababa) | Revisión adversarial 4 |
| Personalizar inicio: miniaturas de página | Se dibujan del modelo a escala con los iconos en estilo Plano (sin Vidrio por icono) | Captura real (`lv_snapshot`) de cada página del escritorio reducida al vecino más cercano y compuesta sobre el fondo; los iconos salen con el estilo activo | Sin motor propio; coste de una captura por página al abrir y tras cada cambio |
| Personalizar inicio: pellizco | `gtPollMulti()` desde `loop()` | Mismo clasificador (`flex_pz_update`) como consumidor previo a LVGL, con el multipunto de `flex_touch_get_frame()` (tarea del GT911); la acción se difiere con `lv_async_call` | El gancho corre dentro de la lectura del indev de LVGL, donde no se puede crear UI |
| Personalizar inicio: "Mis imágenes" | Lista y decodifica el JPEG al momento | Lista por la tarea de almacenamiento (`flex_fs_list_async`; nunca la flash desde la UI) y "Usar" avisa de que no se puede hasta la Fase 9 | Decodificador JPEG por hardware aún sin integrar |
| Sesiones: lectura | `appLoadSessionOnce` lee el archivo la primera vez que se abre la app | Se leen TODAS al arrancar por la tarea de almacenamiento (≤ 1 KB por app) y se aplican de forma perezosa antes de crear la app | La flash nunca se toca desde la UI |
| Sesiones: escritura | `flexFsWriteBinAtomic` síncrona; si falla, la app no se cierra | Cola del escritor único (atómica: temporal + renombrado). Poner la escritura en cola es "guardado"; un fallo posterior del disco se reintenta 3 veces y se registra | Una sesión es estado de interfaz, no el documento; si ni siquiera se puede encolar, la app no se cierra (salvo el restablecimiento) |
| Sesiones: corte de corriente | Un corte entre renombrar a `.bak` y a la ruta final deja solo `.bak` y `.tmp`: la sesión se pierde | El renombrado atómico del escritor no pasa por `.bak`: queda la sesión vieja o la nueva | Hueco de Arduino (01c §14.3) |
| Memoria: medir | `memTick` mide cada 1 s en el bucle y el mayor bloque cada 2 s / 10 s | Un `lv_timer` de 1 s con `heap_caps_*`; el mayor bloque con las mismas cadencias (2 s apretado, 10 s con holgura y sin dedo ni transición) | Mismo reparto; el estado de las apps se copia del marco antes de decidir |
| Memoria: soltar | `memShedSystem` llama a una lista fija de cachés propias | Registro `flex_mem_register_cache` (fondos desenfocados y miniaturas de Recientes hoy; el panel rápido, el vidrio y la caja se registran cuando se midan) con guardia por pantalla; los bytes liberados son los MEDIDOS | Las cachés de LVGL no existen igual que las del motor propio |
| Memoria: radio | `wifiRadioBusy` cede el alivio mientras el enlace del C6 se levanta | Gancho débil `flex_net_radio_busy` (falso hasta la Fase 6) | Aún no hay enlace con el C6 |
| Optimizar: fondo | Captura de la banda del panel para no apilar el vidrio | Un `lv_obj` modal sobre la pantalla: LVGL repinta debajo sin apilar capas | No hace falta la captura |
| Optimizar: caché temporal | `fsWipeDir` síncrono en el bucle | `flex_fs_wipe_dir_async`: la cuenta llega a la UI cuando termina y completa la línea del resultado | La flash no se toca desde la UI |
| Arranque: banda y splash | `delay(2200)` y `delay(16)` por cuadro en `setup()` (la tarea no atiende nada) | `lv_timer`: la tarea de UI sigue viva (los toques se tragan con una raíz modal) | Nada bloquea la tarea LVGL |
| Arranque: Modo seguro | La banda (si hubo reinicio anormal) y luego la pantalla de Modo seguro; el splash nunca sale | Igual (la rama de Modo seguro del final del splash de Arduino es código muerto) | Fiel al comportamiento real |
| Arranque: reinicio anormal para la banda | Todo motivo salvo POWERON, SW y DEEPSLEEP (incluye EXT y desconocido) | Igual; el criterio del Modo seguro es otro (`flex_safe_abnormal`, además PWR_GLITCH y CPU_LOCKUP) | Dos preguntas distintas |
| Calculadora: % y notación | `%g`: 6 cifras significativas y `1e+06` para números grandes | Igual (misma `snprintf`) | Fiel; documentado en 03 §3.6 |
| Calculadora: teclas | Sin estado pulsado | Sin estado pulsado (toque = clic < 16 px y < 550 ms) | Igual |
| Notas: tamaño del texto | Buffer de 4 KB en PSRAM (512 B sin ella); al abrir una nota mayor la **recorta en silencio** y al guardar escribe el recorte: se pierde lo que sobraba | Buffer de 16 KB en PSRAM; una nota que no cabe NO se abre (aviso "demasiado grande") y no se toca; tampoco se abre un archivo con bytes 0 (cortaría el texto al guardar) | Integridad de datos (spec 03 §6.4/§18) |
| Notas: insertar sin sitio | `noteInsert` borra antes la selección y luego rechaza el texto: pegar algo grande sobre una selección la destruía | Si no cabe (contando lo que libera la selección) no se cambia nada y se avisa "La nota está llena" | Integridad de datos |
| Notas: autoguardado | Se arma si cambia la LONGITUD del texto: reemplazar una selección por otra de igual tamaño no se guardaba | Se arma con cada cambio (revisión del texto) | Integridad de datos |
| Notas: guardar falla | Se ignora el resultado | Se avisa "No se pudo guardar", el texto sigue en memoria y se reintenta cada segundo | Integridad de datos |
| Notas: lista | `NOTE_MAX_LIST = 16`, recortada ANTES de ordenar (se perdían notas sin avisar); aparecían carpetas y abrirlas montaba un editor sobre un directorio | Hasta 64 notas, ordenadas (sin distinguir mayúsculas) antes de recortar, con aviso si hay más; solo ficheros | Integridad de datos |
| Notas: vista previa | 16 lecturas síncronas de 96 bytes por recarga, en el hilo de la UI | Lecturas asíncronas de los primeros 95 bytes (`flex_fs_read_head_async`), una a una; la tarjeta se rellena al llegar | Arquitectura: la UI no toca la flash |
| Notas: errores al renombrar / crear | Silenciosos | Avisos: "Ya hay una nota con ese nombre", "Nombre demasiado largo", "No se pudo crear la nota" | Usabilidad |
| Notas: "atrás" con un diálogo del kit abierto | Solo el menú contextual lo cerraba; los demás dejaban salir de la app con el diálogo abierto | Cierra el de arriba (la papelera avisa para recargar la lista) | Coherencia |
| Notas: manijas y cursor fuera de la hoja | Si la selección o el cursor quedan fuera de lo visible (se arrastró el scroll), el recorrido se corta y se dibujan en (18, …) | Se calculan sobre todo el texto y la hoja los recorta | Fidelidad visual |
| Notas: portapapeles | El texto se corta a 199 / 511 bytes aunque parta un carácter UTF-8 | Corta en un límite de carácter | Integridad de datos |
| Notas: botón "ajustes del teclado" | Abre `kbsEnter()` | Llama a `flex_kbsettings_open()` (símbolo débil que pondrá la app Ajustes, A11); mientras no exista, aviso "Ajustes del teclado: con la app Ajustes" | Dependencia de A11 |
| Paint: dibujos con el fichero cortado | `flexPaintAppend` añade el trazo DETRÁS de los bytes sueltos de un trazo cortado por un apagón: el dibujo queda ilegible a partir de ahí | Al abrir se recorta al último trazo completo y se corrige el contador | Integridad de datos |
| Paint: tamaño de un dibujo | Sin tope: crece hasta llenar el disco | Tope de 192 KB en memoria; el trazo que no cabe se avisa ("El dibujo es demasiado grande") y un fichero mayor no se abre | Integridad de datos / memoria |
| Paint: guardado | Cada trazo se añade al fichero (la cabecera se reescribe en el sitio) y el resultado se ignora | El dibujo se escribe entero y de forma atómica en la tarea de almacenamiento (una escritura en vuelo; la siguiente lleva el dibujo ya actualizado); si falla se avisa "No se pudo guardar el dibujo" y se reintenta con el siguiente trazo o al salir | Arquitectura (la UI no toca la flash) / integridad |
| Paint: trazo de más de 512 puntos | Al cerrar el trazo se vuelve a abrir con el último punto pero se DESCARTA el actual: un segmento dibujado en pantalla no queda en el fichero | El trazo nuevo lleva el último punto y el actual: cada segmento dibujado está guardado | Integridad de datos |
| Paint: lista | `PAINT_MAX_LIST = 16` recortada antes de ordenar; salen carpetas y restos | Hasta 24 dibujos, ordenados antes de recortar, con aviso si hay más; solo ficheros `.fxp` (no se ven carpetas ni restos `.tmp~`) | Integridad de datos |
| Paint: miniaturas | Se reproducen todos los ficheros en cada repintado de la galería | Se renderizan una vez por carga de la lista (lecturas asíncronas de una en una) en búferes que `shed()` suelta | Rendimiento / arquitectura |
| Paint: limpiar un dibujo vacío | Reescribe el fichero y repinta | No hace nada (sin desgaste de flash) | Rendimiento |
| Paint: contador de la cabecera | "N trazos guardados", siempre en plural y solo al abrir / deshacer / limpiar | Singular con 1 trazo ("1 trazo guardado") y se actualiza tras cada trazo | Usabilidad |
| Paint: suspender con el dedo apoyado | La app no se ticaba suspendida | LVGL sigue entregando el dedo apoyado al lienzo oculto: se ignora mientras está suspendida (un trazo suelto aparecía al soltar) | Fidelidad |
| Paint: errores al crear, abrir o renombrar | Silenciosos | Avisos: "No se pudo crear el dibujo", "No es un dibujo de Paint", "No se pudo abrir el dibujo", "Ya hay un dibujo con ese nombre", "Sin memoria para el lienzo" | Usabilidad |
| Paint: bordes suaves | `fillCircleAA` / `strokeSegAA` | Portados a `flex_raster.c` con la misma aritmética (en lugar de `lv_draw_line`, que no coincide y crea una tarea de dibujo por segmento) y comparados píxel a píxel | Fidelidad visual / rendimiento |
| Ajustes: barra de estado | La app pinta la suya con Wi-Fi y bateria FIJOS (82 %) y los glifos de navegacion | Usa la barra global del sistema (valores reales), transparente sobre la pagina | Fidelidad de datos |
| Ajustes: valores vivos | Se congelan hasta el siguiente toque (hora, estado NTP...) | Se leen de nuevo al abrir una categoria, tras cada accion y al reanudar; la papelera se mide en la tarea de almacenamiento y la fila se actualiza al llegar ("--" mientras tanto). No hay refresco por minuto (como Arduino) | Arquitectura (la UI no toca la flash) |
| Ajustes: servicios sin migrar | Siempre estan | Hora por red, actualizaciones, Flex Account, Conectividad/Wi-Fi/BLE, proteccion contra robo y audio son simbolos debiles: la fila dice "No disponible" (en Sincronizar ahora tambien) y tocarla avisa "Proximamente" en vez de abrir nada | Honestidad: no se ofrece lo que no funciona (inventario A11n) |
| Ajustes: guardado | `cfgSavePrefs` reescribe 30 claves por toque | Cada fila escribe solo su clave (`flex_theme_set_*`, `flex_set_lang`, `flex_cfg_set_*`) | Desgaste de NVS |
| Ajustes: desplazamiento | Umbral de arrastre de 6 px propio | El de LVGL, sin inercia ni rebote | Arquitectura |
| Ajustes: volver desde el borde | Se decide al soltar con `T` | Igual (>70 px a la derecha y <90 de desvio), siguiendo el tactil del arbitro con un temporizador de 16 ms (LVGL da la pulsacion por perdida en cuanto el dedo se mueve) | Arquitectura |
| Ajustes: transicion | Se copian filas de dos lienzos de 750 KB y la sombra son 8 columnas con alfa 62..6 | Dos paginas LVGL cuya `x` sigue la misma curva (270 ms, cubica, paralaje 28 %); sombra de LVGL de 12 px al 40 % | Ahorra 1,5 MB de PSRAM y la animacion funciona tambien en una ventana |
| Ajustes: pagina desde y = 30 | Banda de la transicion desde y = 40 | Desde y = 30 para que el titulo de tamano 4 no quede cortado por su caja de linea | Fidelidad visual |
| Ajustes: Fecha y hora / Zona horaria | Filas con chevron sin accion | Igual (informativas); el ajuste manual llega con la hora por red (A11n) | — |
| Ajustes del teclado: pantalla | Estado propio `ST_KBSET` a todo el alto | Pantalla modal sobre la app que la abre (Ajustes o el engranaje del teclado de Notas), como Optimizar: Atras va de pagina en pagina, Inicio / Recientes / el bloqueo la cierran, y al salir avisa a quien la abrio | Arquitectura |
| Ajustes del teclado: editor de atajos | El teclado se REDIBUJA a mano dentro del editor (copia de la geometria y las teclas) | Es el teclado del sistema (`flex_kb`) escribiendo en el campo enfocado; se apoya sobre la barra de navegacion, que en IDF sigue siempre a la vista | Una sola implementacion del teclado |
| Ajustes del teclado: idioma activo | `kbLangEs` global | Dura lo que el encendido y se sincroniza con la tecla ES/EN del teclado del editor | Fidelidad |
| Ajustes del teclado: "dedos a la vez" | Maximo de puntos del GT911 visto | El indev de LVGL entrega UN punto: la fila dice "1 dedo", que es lo cierto (la escritura rapida multitoque sigue pendiente, 01b) | Honestidad |
| Ajustes del teclado: transicion | Copia de buffers por tiempo | Objetos LVGL: zoom del 88 % al 100 %, fundido o deslizar, 90 ms | Arquitectura |
| Reloj: reloj grande de la pestana Hora | Fuente vectorial propia (`drawBigClock`: cifras trazadas con segmentos y arcos de grosor `thick`) | La fuente Outfit de 200 px del bloqueo escalada (`transform_scale`) para que la altura de las cifras sea la que calcula Arduino (`capH`); el trazo `thick` no existe, la fuente ya trae su peso | Arquitectura (LVGL) / cosmetico |
| Reloj: secciones opcionales (fecha, tarjetas) | Aparecen y desaparecen con un fundido de 130 ms (`uiSection`) | Aparecen enteras sin fundido: el lienzo solo cambia al abrir la app, no se arrastra el borde de una ventana | Cosmetico |
| Reloj: Temporizador y alarmas | Pestana "Proximamente"; no hay alarmas | Igual (no se finge un motor). El inventario separa esos puntos (A01t) | Fidelidad / honestidad |
| Reloj: pestanas | `relojTabsTouch` reparte el toque por `x / 146` | Tres segmentos de 146 px (`seg = w / n`) y el activo inset 2 px (142 x 32), como `relojDrawTabs`; el kit `flex_kit_segmented` usaba antes 16 px de margen y segmentos de 148: corregido | Fidelidad |
| Cronometro: texto de la esfera, la tarjeta y la tabla | Fuente de mapa de bits tamanos 1-7 | Outfit 13-38: el tamano se elige con el mismo algoritmo (el mayor que cabe en el disco / el hueco) | Arquitectura |
| Cronometro: esfera | `strokeSegAA` con grosores fraccionarios (1,1 / 1,9 / 1,4 / 2,3) | `lv_draw_line` de ancho entero (1 / 2 / 1 / 2) con el antialiasing de LVGL | Cosmetico |
| Cronometro: persistencia | No se guarda (la propia fuente deja "PERSISTENCIA pendiente") | Igual: se pierde al reiniciar; sin sesion de app | Fidelidad |
| Almacenamiento: medida de las categorias y de los archivos grandes | Sincrona en el dibujo (`almScan`: recorre el arbol entero con la UI parada) | En la tarea de almacenamiento (`flex_fs_scan_async`); la pantalla dice "..." y "Calculando..." hasta que llega, "--" y "No se pudo leer" si falla. Se mide al entrar, al volver del explorador y tras Optimizar | Arquitectura / honestidad |
| Almacenamiento: archivos grandes | Pide 12 y filtra los de la biblioteca (`/System/Media/`): con 9 o mas por encima se queda sin ninguno | La biblioteca se salta al recorrer: siempre salen los 3 mayores de verdad | Fidelidad (error corregido) |
| Almacenamiento: fila "Todos los archivos / Ver..." | Se omite si los archivos grandes ocupan el sitio (a 640 px, ya con dos): el explorador quedaba inalcanzable | Tiene prioridad: solo se pinta un archivo grande si despues cabe la fila; si ni uno cabe, el titulo se queda sin archivos y sin "No hay archivos guardados" (los hay). Probado frente a Arduino con 4 000 variantes (mismo resultado salvo estos casos, comprobados aparte) | Fidelidad (error corregido) |
| Almacenamiento: linea "ruta · tamano" de un archivo grande | `ln[64]`: una ruta larga pierde el tamano | Buffer sin recorte: el tamano se ve siempre | Fidelidad (error corregido) |
| Almacenamiento: pantalla "sin almacenamiento" | Aconseja elegir un Partition Scheme con SPIFFS en el IDE | "Reinstala el firmware o repara el almacenamiento en Ajustes" (la tabla de particiones es fija) | Arquitectura |
| Almacenamiento: tarjeta "Flex Cloud en tu telefono" | Estado real del telefono emparejado (Flex Storage) | "No disponible todavia" y un aviso al tocarla: el servicio de telefono no esta migrado (A04c). No se finge "Sin telefono" ni "activalo desde la web" | Honestidad |
| Almacenamiento: tamano del firmware | `ESP.getSketchSize()` | `esp_image_get_metadata` de la particion en ejecucion (`image_len`), una vez por arranque; 0 = "No disponible" | Plataforma (pendiente en la placa) |
| Almacenamiento: "Ritmo del sistema" | `gLoopRate` (vueltas del `loop()`) | Vueltas por segundo de la tarea de UI (`flex_metrics.ui_loops`) | Plataforma |
| Archivos: lista | `FILES_MAX = 24` entradas en el orden del disco, luego ordenadas; mascara de 32 bits | Hasta 256 entradas leidas, ordenadas (carpetas primero, sin distinguir mayusculas y, a igualdad, por bytes); filas por ventana (solo las visibles mas un colchon) | Fidelidad (limite subido) / rendimiento |
| Archivos: nombres de 48 caracteres o mas, y rutas de 160 o mas | Se truncan en silencio: borrar o renombrar tocaba el archivo que se llama como el principio | No se listan (no se pueden operar con seguridad). Probado con un arbol de 118 caracteres y entradas de 40, 41 y 8 | Seguridad de datos |
| Archivos: toques sobre la cabecera, la ruta y el selector | Una fila desplazada por debajo recibia toques ahi | La lista solo recoge toques dentro de su ventana (probado en 3 000 casos) | Fidelidad (error corregido) |
| Archivos: pulsacion larga y arrastre | 550 ms y menos de 14 px; con scroll, mas de 8 px es arrastre | Las mismas reglas del modelo (`flex_files_long`) sobre el indev de LVGL; el scroll es nativo (umbral de LVGL) | Arquitectura |
| Archivos: gesto Atras del sistema | No hace nada dentro del explorador (solo la flecha sale) | Cierra la capa abierta (menu, dialogo, papelera), vuelve de la nube a "Este dispositivo" y, si no, sale a Almacenamiento (igual que la flecha); Inicio, Recientes y el bloqueo lo cierran | Fidelidad (mejora) |
| Archivos: "Flex Cloud" y "Subir a Flex Cloud" | La nube real (CloudKit) / cola de subidas | "Flex Cloud no disponible" y un aviso al subir: sin N4 no hay a donde (A04c) | Honestidad |
| Archivos: abrir una foto, dibujo, video o audio | `mediaOpenInPlayer` | Un aviso "El visor aun no esta disponible en esta version": Galeria y Multimedia no estan migradas (A02 / A03) | Honestidad |
| Juegos: motor de dibujo | El del sketch (`Gfx.h`, `Font.h`) sobre `bbuf` / `fb` con DMA2D | El MISMO codigo extraido (`jumper_engine.inc`, generado y comprobado contra los originales) sobre un lienzo RGB565 de 480 x 800 en PSRAM que LVGL muestra como una imagen. Excepcion ACOTADA a la regla "toda la UI con LVGL" (`check_lvgl_only.py`, `GAME_CANVAS_ALLOWED`): el juego rasteriza cada cuadro con cientos de primitivas, mezcla alfa por pixel y fuente antialiasada; reescribirlo en LVGL cambiaria el aspecto y la fisica de temporizacion. No toca el panel | Arquitectura (excepcion documentada) |
| Juegos: doble buffer | `fb` / `bbuf` se intercambian y `flxFlush` copia por DMA2D | Un solo lienzo: el juego y LVGL corren en la misma tarea, asi que no hay desgarro; el intercambio de punteros de `jmpPresent` es inocuo y "presentar" invalida la imagen | Arquitectura |
| Juegos: horizontal | `gLand = true` gira el motor 90 grados y el sistema oculta la barra y los gestos | Igual: el lienzo es fisico y el motor gira al dibujar (mismas coordenadas tactiles); `FLEX_APP_LAND` deja la app a pantalla completa sin barra de navegacion ni gestos del sistema (cortina, Centro de avisos) y sin miniatura en Recientes. Sin rotacion de la pantalla ni del panel | Fidelidad |
| Juegos: reloj y cadencia | `micros()`; `loop()` del sketch | `esp_timer_get_time()` (el simulador, el reloj de LVGL); un `lv_timer` de 4 ms da vueltas al juego y este presenta a 60 Hz exactos por si mismo | Plataforma |
| Juegos: records | `Preferences` ("flexos": `jmp_bn`, `jmp_bp`, `jmp_coin`, `jmp_cmp`, `jmp_att`) | `flex_cfg_*` con las mismas claves: la NVS de Arduino se lee tal cual. Los intentos solo se guardan en los puntos de guardado del juego (como en Arduino) | Fidelidad |
| Juegos: memoria | Dos framebuffers de 768 KB | Un lienzo de 768 KB mientras la app existe (se libera al cerrarla); sin `shed` (reconstruirlo exigiria otro puntero de lienzo en el juego) | Arquitectura |
| Device Care: dibujo | Primitivas de `Gfx.h` sobre `bbuf` / `fb` y repintado de bandas con `present()` | Cada pantalla es una LISTA DE OPERACIONES DE DIBUJO con las coordenadas exactas de Arduino (`flex_dc_screens.c`, comparada primitiva a primitiva con las funciones originales extraidas); `flex_dc_ui.c` las pinta con LVGL (superficies del sistema, vectores en UN objeto de dibujo, textos como etiquetas). Las animaciones (anillo, latido, celdas) cambian solo vectores y cifras en su sitio: no hay bandas ni `present()` | Arquitectura |
| Device Care: reparto de toques | El test de pantalla va PRIMERO; el chevron de la cabecera se atiende antes que el resto | Igual: la capa del test cubre la pantalla y lo recoge todo (en la pregunta final el chevron no sale: solo la barra de navegacion); la zona de atras es x < 72, y <= 96 | Fidelidad |
| Device Care: division del test tactil | `(T.x - gx) / cw` en enteros: trunca hacia cero | Igual, a proposito: un toque a menos de una celda por fuera de la rejilla (arriba o a la izquierda) cae en la fila o columna 0, como alli | Fidelidad |
| Device Care: test de pantalla | `hLine` por fila, `vLine` y `fillRect` sobre el bufer | Fondos y rejilla con las primitivas de dibujo de LVGL en la capa de pantalla completa; el degradado es el de LVGL (dos paradas, de 0 a 255) en lugar de una linea por fila: el mismo valor `y * 255 / (h - 1)` salvo el cuantizado del panel de 16 bits | Arquitectura |
| Device Care: metricas | `memSnap`, `esp_task_wdt_status`, `gLoopRate`, `flexFs*`, `clkAnchored`, `gTimeNvsOk`, `flxFlushFault`, `gtRecoverN`, NVS `flexcare` / `badboot` | `flex_mem_snap`, `esp_task_wdt_status`, `flex_metrics.ui_loops`, `flex_storage_get_status`, `flex_i2c_get_stats().recoveries`, `flex_system_abnormal_boots()` (mismo espacio y clave de NVS, contado en `flex_system_start`) y `esp_reset_reason`. El reloj SIEMPRE tiene ancla (hay hora semilla desde `flex_clock_init`) y el volcado al panel no reporta fallos medibles (sin DMA2D): no se cuenta lo que no se mide | Plataforma / Honestidad |
| Device Care: historial | `dcHistSave` escribe en el acto en la tarea de UI (`flexFsWriteBinAtomic`) | Lectura y escritura ASINCRONAS por la tarea de almacenamiento, una a la vez: un registro que llega mientras se escribe espera y se escribe despues; un registro que llega mientras aun se lee va encima de lo leido; un fallo lo deja en RAM y se reintenta (al siguiente registro, al suspender, al cerrar). Mismo archivo y formato (`/System/devcare.bin`, `'DCAR'`, version 1, 16 registros de 8 bytes) | Arquitectura / Seguridad de datos |
| Device Care: lista del historial | Repinta la lista con el desplazamiento (`dcScroll`, arrastre con umbral de 8 px) | La lista entera con su alto real dentro de una vista con desplazamiento NATIVO de LVGL (mismo contenido y alto que `dcHistContentH`); el desplazamiento se conserva al volver | Arquitectura |
| Device Care: IMU y caidas (A17h) | Sondeo del GY-BNO085 al entrar en Caidas, estados ausente / sondeando / listo, prueba con lecturas en vivo, detector en segundo plano, aviso global y revision tras impacto | NO implementado (depende de A08 / N6). La pantalla de Caidas dice "No disponible", nombra el modulo que espera (los textos de Arduino) y una nota en cada idioma; el diagnostico anota "IMU no disponible" y no puntua ni a favor ni en contra; el historial no recibe eventos de caida; `bg_work` es falso. Esta pantalla NO existe en Arduino | Honestidad |
| Device Care: caja en Modo PC | `uiBox` contra `gAppW` x `gAppH` cuando esta alojada | Las pantallas se maquetan contra un contexto (`flex_dc_ctx_t`: caja, idioma, metrica de texto) y estan comparadas en cajas de 300 a 480 de ancho y de 330 a 800 de alto; la app usa hoy la caja de pantalla completa (0, 96, 480, 640): alojarla en una ventana espera a Modo PC (A05) | Pendiente (A05) |
| Hora por red: socket y origen | `WiFiUDP` (`beginPacket` / `parsePacket` cada 20 ms), acepta el datagrama de CUALQUIER origen | Socket BSD de lwIP con `select` sobre el tiempo que queda; solo acepta datagramas del mismo address y puerto a los que se pregunto, de 48 bytes o mas (los cortos y los vacios no cierran la espera). La primera respuesta completa cierra la espera, valga o no, como en Arduino | Seguridad |
| Hora por red: respuestas que un servidor de verdad no manda | Solo exige `secs >= MIN_EPOCH` | ademas modo 4 (servidor), aviso de reloj distinto de 3 (alarma) y estrato de 1 a 15 (0 es un kiss-o'-death; 16, no sincronizado). La lectura de los segundos (bytes 40..43) y el suelo de 2026 son los de Arduino | Seguridad |
| Hora por red: la hora de fabrica | El primer guardado horario guarda la semilla de fabrica y el siguiente arranque la presenta como "Hora aproximada" | Solo se guarda una hora que lo merece: la de la red, la de un ajuste manual o la aproximada recuperada de la NVS. La semilla de fabrica no (la spec 03b §12.8 lo marcaba como "no copiar") | Fidelidad (error corregido) |
| Hora por red: epoca corrupta en la NVS | `clkSetEpoch((uint32_t)e)` trunca una epoca de mas de 32 bits | No se aplica (un valor de 64 bits que no cabe es un dato corrupto) | Seguridad de datos |
| Hora por red: espera progresiva | `NTP_BACKOFF_MIN << (fails - 1)` (los fallos se acotan a 8) | Igual, y el desplazamiento esta definido para cualquier `fails` (con 33 o mas no lo estaba) | Robustez |
| Hora por red: tarea | Una tarea nueva por consulta (`xTaskCreatePinnedToCore`, 4 KB) | UN trabajador `flex.ntp` (5 KB, prioridad 1, nucleo 1) que cada segundo decide y, si toca, pregunta; la interfaz solo pone banderas y lo despierta (`xTaskNotifyGive`) | Arquitectura |
| Hora por red: reloj | `clkSetEpoch` sobre `millis()` + ancla, re-anclaje horario | El reloj POSIX del sistema (`settimeofday`) detras de `flex_time`; `flex_clock_synced()` = la hora vino de la red o de un ajuste manual (la semilla y la aproximada no) | Plataforma |
| Hora por red: sin Wi-Fi | Cualquier placa con la radio | La radio llega por el ESP32-C6 (X3, N1b): sin ella el estado dice "Esperando Wi-Fi" y no se consulta nada; el servicio de Wi-Fi avisa con `flex_net_set_online()`. El tamano del firmware sube 93 KB por los sockets y el DNS de lwIP | Honestidad |
| Hora por red: control del panel rapido | `qpAvNtp()` = Wi-Fi compilado y fuera del modo avion; `qpTapNtp()` = `ntpRequestSync(true)` | Igual: con el servicio enlazado y fuera del modo avion; el toque pide la sincronizacion sin esperar la progresiva | Fidelidad |
| OTA: clave del manifiesto | `flex_os_ultra` | `flex_os_ultra_idf`: el binario de Arduino y el de IDF no son intercambiables | Seguridad / compatibilidad |
| OTA: capas | Un solo `.cpp` con `HTTPClient`, `Update` y `Preferences` dentro | Modelo C puro + motor sobre `flex_ota_ops_t` (red, flash, tiempo, cerrojo, reinicio inyectados) + `flex_ota_port.c`. El motor y el modelo se prueban en el host comparandolos con el `.cpp` de Arduino compilado tal cual (50 725 comprobaciones, ASan/UBSan y TSan) | Probabilidad de acierto sin hardware |
| OTA: integridad del binario | `Update.setMD5()` | El MD5 se calcula sobre lo ESCRITO (mbedtls) y se compara antes de cerrar; `esp_ota_end()` valida ademas cabecera, chip, revision minima y SHA-256 anexo. Un MD5 de relleno (ceros, no hexadecimal) se ignora, como en Arduino | Seguridad |
| OTA: imagen de otra aplicacion | No se comprueba | El `project_name` de la imagen descargada tiene que ser el del firmware que corre: el `.bin` de Arduino no se instala | Seguridad |
| OTA: confirmacion de arranque y rollback | Ninguna (`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` no existe) | La imagen nueva arranca pendiente de validar y se confirma a los 15 s de un arranque sano y sin Modo seguro; con Modo seguro no se confirma y el siguiente reinicio vuelve a la anterior | Seguridad |
| OTA: TLS | `WiFiClientSecure` con `setCACert`; sin CA, error (`FLEXOS_OTA_ALLOW_INSECURE 0`) | `esp_http_client` con `cert_pem` fijado (`components/flex_ota/certs/ota_root_ca.pem`); sin el archivo el motor no conecta. El TLS sin verificar no existe en la placa (`http_begin` lo rechaza aunque se pida) | Seguridad |
| OTA: redirecciones | `HTTPC_STRICT_FOLLOW_REDIRECTS` | Hasta 5, solo a `https://` (la bajada la rechaza `esp_http_client`) y con `Accept-Encoding: identity` para que el `Content-Length` sea el del binario (no hay `useHTTP10`) | Seguridad |
| OTA: firma y anti-rollback | Arquitectura preparada y apagada (`sig` solo se lee; la version anterior se rechaza en software si se activa) | Igual: se lee y se guarda `sig`, ni se verifica ni se usa el contador de eFuses. Pendiente de decidir por el propietario (claves, irreversible) | Honestidad: no se ofrece lo que no funciona |
| OTA: tarea | `flexOTA`, pila `FLEXOS_OTA_TASK_STACK`, prioridad 1, nucleo 1 | `flex.ota`, 10 KB, prioridad 1, nucleo 1; la interfaz solo pone ordenes (atomicas) y lee una instantanea coherente | Equivalente |
| OTA: cache de flash | Cerrojo con la subida al panel (`otaHostLockPanel`) | Sin cerrojo (`panel_lock` vacio): el controlador de flash de IDF gestiona la cache y el panel DSI lee de la PSRAM. A medir en la placa (tiempo de cuadro durante la escritura) | Arquitectura |
| OTA: lector del manifiesto | `strncpy` + terminador en los campos globales | El mismo resultado, y la estructura de salida del modelo tambien queda terminada en 0 aunque la cadena llene el buffer (el compilador de IDF lo exigio) | Error corregido |
| OTA: interfaz | Pixeles sobre el framebuffer compartido; la capa a pantalla completa tapa tambien la barra de navegacion al dibujar todo | Objetos LVGL; la barra de navegacion se oculta explicitamente mientras la capa ocupa la pantalla y vuelve al cerrarla; el gesto atras (deslizar a la derecha) sale del changelog y de Ajustes. Verificada con huellas de objetos y de pixeles en modo oscuro y claro | Arquitectura |
| Cronometro: reloj de tiempo | `millis()` | `lv_tick_get()` (ms de 32 bits que dan la vuelta; la misma resta sin signo) | Fidelidad |
| Capsula: material y posicion | Pildora opaca dibujada en la barra (`cronoCapsuleDraw`) | Igual (opaca a proposito, hijo de la barra de estado del escritorio y del marco estandar); la hora normal cede si no cabe todo | Fidelidad |
| Capsula: con la cortina o el Centro de avisos abiertos sobre una app | La superficie 2 no mira el panel: sigue sensible bajo la cortina | El gancho no reclama el toque si la cortina o el Centro estan abiertos | Endurecimiento |
| Tarjeta expandida: fondo | Captura la banda de pantalla y la restaura al cerrar (dos bufers PERMANENTES de PSRAM: 480 x 236 x 2 y 480 x 96 x 2 = 318 KB) | Objetos LVGL en `lv_layer_top` que se crean al abrir y se borran al cerrar: 0 bytes permanentes | Memoria |
| Tarjeta: vidrio | Desenfoca la banda entera (widgets incluidos) con alfa 200; plano 240 | Vidrio opaco (255) sobre el fondo ya desenfocado (LVGL no desenfoca los widgets de debajo); plano 240 como Arduino | Cosmetico / legibilidad |
| Tarjeta: ultimo cuadro de la animacion | `cronoLerp` trunca hacia 0 al disminuir: la tarjeta queda en x = 21 si la capsula esta a la derecha de 20 | Reposo exacto en x = 20 (el modelo conserva la formula para compararla con Arduino) | Correccion |
| Tarjeta: pantalla del OTA | `flexOtaOwnsScreen()` la suelta | El OTA no esta migrado (N2): el gancho se anade con el | Dependencia |
| Marco de apps: cabecera estandar y el tema | Se repinta todo al cambiar el tema | El chevron y el titulo no se recoloreaban al cambiar Oscuro / Claro con la app delante (los colores se leian al crear): el shell los recolorea | Correccion |
| Tema: oyentes | Lista fija de 24 | Un oyente repetido (misma funcion y contexto) ya no se registra dos veces: un modulo que se arranca de nuevo no agota la lista | Correccion |
| Notas: lápiz del botón flotante | Trazos de 5 y 4 px | 5 y 3 px (el motor de trazos de LVGL solo da anchos impares) | Cosmético |
| Teclado: barra y chips | `FLEX_KB_F_EXTRAS` preparado y sin dibujar | Barra de 5 botones (carita, globo, portapapeles, engranaje, más), chips de sugerencias o de símbolos con fundido de 140 ms, el panel sube y avisa (`on_resize`) | Cierra el "pendiente" de spec 01b T.8 |
| Calendario: iniciales de los días | Fijas "D L M M J V S" | Las del idioma activo (`flex_i18n_wd_short`); en español salen las mismas | Idioma |
| Calendario: números de dos cifras | Tamaño fijo del texto de las celdas | Se ajusta a la celda (`flex_ui_font_fit`) para que 28–31 no se salgan del círculo del día | Legibilidad |
| Papelera: nombres largos | `encodeTrashName` recorta a 47 bytes en silencio: el fichero se restauraba con otro nombre o pisaba otro de la papelera | Si el nombre codificado no cabe (47 bytes, con el sufijo `(k)`), la operación falla y el fichero se queda donde estaba | Integridad de datos (`flex_fs_names.c`) |
| Papelera: `@` y `%` en el nombre | `a@b.txt` se restauraba como carpeta `a` con el fichero `b.txt` (la `@` es el separador) | `@` y `%` del nombre original viajan escapados (`%40`, `%25`); lo que dejó Arduino (sin escapes) se lee igual | Integridad de datos |
| Papelera: sufijos agotados | Con las 98 variantes ocupadas reutilizaba la última, que existe, y la sobrescribía | Falla y no toca nada | Integridad de datos |
| Papelera: restaurar con 2 o más colisiones | `ext` apuntaba DENTRO del búfer `dst`, que el bucle sobrescribe: desde la 3.ª colisión el nombre salía `"x (3) (2).txt"` | `"<stem> (k)<ext>"` como dice el spec (reproducido contra Arduino en `test_fsnames.c`) | Defecto de Arduino |
| Papelera: ruta de la entrada | Una entrada `@..@x` se restauraba fuera de la raíz | Se rechaza (ruta con `..`, hacia dentro de la propia papelera o carpeta pelada) | Seguridad |
| Renombrar | Nombre + extensión recortados a 47 bytes sin avisar | Si no cabe (≥ 48 bytes) devuelve error; el diálogo de nombre ya limita a 46 | Integridad de datos |
| Diálogo de nombre: tope | El spec decía 47 bytes; el código admite 46 (`L + sl < NAME_MAX - 1`) | 46 bytes, igual que el código; spec corregido | Fidelidad |
| Diálogo de nombre: teclado | Panel hasta el borde inferior | Se apoya sobre la barra de navegación cuando está visible (`bottom_reserve = 64`): si no, la fila de funciones quedaba bajo ella | Barra de navegación del marco |
| Papelera: borrar un elemento | La confirmación muestra el nombre codificado (`@Notas@a`) | Muestra el nombre original | Usabilidad |
| Papelera: lista y cabecera | Redibuja todo y los elementos pueden pisar la cabecera al desplazar | Lista con ventana de filas (solo las de la vista) entre la cabecera y la barra; cabeceras fijas | Memoria: 256 elementos no son 700 objetos |
| "Sin almacenamiento" | Aconseja el Partition Scheme de Arduino IDE | La tabla de particiones es fija: aconseja reinstalar el firmware o reparar el almacenamiento en Ajustes | Sin Arduino IDE |
| Borrar | `deleteRec` ignora los fallos de los hijos y devuelve el de `rmdir` | Vacía por tandas (`flex_fs_wipe_tree`) y devuelve error si algo no se pudo borrar | Integridad de datos |
| Operaciones de archivo | Síncronas, en el hilo de la UI | Todas en la tarea de almacenamiento (`flex_fs_*_async`), sin carreras entre comprobar y renombrar; la UI nunca toca la flash | Arquitectura |
| Umbrales de toque | Toque = < 16 px y < 550 ms | Igual, aplicado al indev de LVGL (por defecto LVGL usa 10 px y 400 ms) | docs/spec/01b |
| Restablecimiento: qué se borra de la NVS | Solo los espacios de nombres conocidos: quedaban el vínculo con el teléfono, Flex Storage, Device Care, robo, audio, clima, navegador (un aparato "restablecido" seguía emparejado) | TODOS los espacios de nombres salvo el marcador `flexreset` (incluidos los del sistema: credenciales Wi-Fi del controlador, calibración del PHY); el aviso lo dice ("Vínculo con el teléfono y Flex Storage") | docs/spec/01c §13.5 (riesgo de privacidad) |
| Restablecimiento: marcador terminado | Se borra al entrar al OOBE | Igual: se borra al entrar a la primera configuración | docs/spec/01c §13.6 |
| Primera configuración: idioma | Al entrar marca siempre Español | Marca el idioma guardado (Español en una placa nueva o tras restablecer, porque la NVS queda vacía) | Coherencia |
| Primera configuración: Flex Account | Tras el nombre, paso de la cuenta (necesita Wi-Fi) | Pendiente de las Fases 6–7: hoy termina tras el nombre; el paso se engancha en `flex_oobe_account_step()` | — |
| Restablecimiento: ejecución | En `loop`, etapa por vuelta; apaga la radio desde loopTask | En el escritor único de almacenamiento, en exclusiva (ningún volcado de ajustes se cuela entre etapas); la radio se apagará por `flex_wifi` cuando exista (Fase 6) | docs/spec/01c §13.7-13.8 |
| Restablecimiento: aviso de OTA en curso | Pantalla "Actualización en curso" | No existe hasta que haya OTA (Fase 14) | — |
| Restablecimiento: Cancelar | Vuelve a Ajustes o al Modo seguro | Vuelve a donde se estaba (la app delante, Inicio o el Modo seguro) | Coherencia |
| Restablecimiento: suspender | El doble toque con dos dedos dentro del asistente dejaba el táctil sin respuesta | Vetado mientras el asistente está abierto | docs/spec/01c §9.6 |
| Avisos: texto guardado | `sysNotify` corta el título y el texto en bytes y puede partir una letra con tilde | Se cortan en caracteres UTF-8 (el banner ya lo hacía en Arduino) | docs/spec/01c §6 |
| Avisos: tarjeta del bloqueo | El título más reciente se dibuja aunque se salga de la tarjeta | Una línea con "..." dentro de la tarjeta | docs/spec/01a §4 |
| Avisos: Centro | Solo se lee el borde con el dedo moviéndose (igual) y se cierra arrastrando a la derecha (igual) | Igual; los avisos del teléfono vinculado llegarán con Flex Phone (el Centro tiene hoy los del sistema) | docs/spec/01c §6 |
