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
| OTA | Manifiesto + MD5 | — | Pendiente (Fase 14) | NO |
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
| 3 (en curso: + Paint con la galería, el lienzo y el rasterizador de Arduino) | `v3` dev | 1 515 296 B | 57,8 % | 36,1 % |

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
