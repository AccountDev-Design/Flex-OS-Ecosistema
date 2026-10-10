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
| 3 | Interfaz completa en LVGL | En curso (shell: bloqueo, clave, escritorio, caja de apps, recientes, energía, teclado, panel rápido, avisos: banner, Centro de notificaciones y No molestar; apagado completo con deep sleep y filtro de encendido; Modo seguro). Las 19 apps siguen sin migrar (pantalla "Pendiente de migrar") | `docs/spec/` |
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
| Home | Escritorio por páginas, widgets, edición | Escritorio LVGL (páginas, dock, widgets), bloqueo, clave, caja de apps, Recientes, panel rápido, avisos (banner + Centro + No molestar); edición del escritorio pendiente | En curso (Fase 3) | NO |
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
| Avisos: texto guardado | `sysNotify` corta el título y el texto en bytes y puede partir una letra con tilde | Se cortan en caracteres UTF-8 (el banner ya lo hacía en Arduino) | docs/spec/01c §6 |
| Avisos: tarjeta del bloqueo | El título más reciente se dibuja aunque se salga de la tarjeta | Una línea con "..." dentro de la tarjeta | docs/spec/01a §4 |
| Avisos: Centro | Solo se lee el borde con el dedo moviéndose (igual) y se cierra arrastrando a la derecha (igual) | Igual; los avisos del teléfono vinculado llegarán con Flex Phone (el Centro tiene hoy los del sistema) | docs/spec/01c §6 |
