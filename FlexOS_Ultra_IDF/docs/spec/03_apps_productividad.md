# 03 · Apps de productividad, utilidades y servicios asociados (área "apps1")

> **Estado del documento: PARCIAL.** Escritos §0-§10 (Reloj, Calculadora, Calendario, Notas, Paint, Archivos,
> Almacenamiento, Flex Storage). Pendientes: Ajustes, Clima, Juegos, apps descargadas, hora/NTP.

> Especificación funcional para reconstruir en **ESP-IDF 5.5 + FreeRTOS + LVGL 9.6** (480×800 vertical, MIPI-DSI,
> táctil GT911) las apps de productividad y utilidades de `FlexOS_Ultra/`: **Reloj** (Hora, Cronómetro, Temporizador)
> con la **cápsula** y la **tarjeta expandida** del cronómetro, **Calculadora**, **Calendario**, **Notas**, **Paint**,
> **Ajustes** (todas las categorías), **Archivos** (explorador) con el **kit de archivos** compartido y la **Papelera**,
> **Almacenamiento** (+ detalle de memoria + Flex Cloud en el teléfono) con el **cuadro de emparejamiento de Flex
> Storage**, **Clima** (motor Open-Meteo, app, widgets), **Juegos → Jumper** (física, nivel, puntuación), el **modelo de
> apps descargadas** que pinta la Caja de aplicaciones y la **hora del sistema / NTP**.
>
> Igual que en `02_diseno_visual_liquid_glass.md`: aquí se describe *qué se ve* y *cómo se comporta*, con las medidas
> exactas del código, para rehacerlo con objetos LVGL. El motor de dibujo Arduino no se reutiliza.

**Fuentes leídas por completo:** `FlexOS_Ultra_AppsBasic.h`, `FlexOS_Ultra_AppChrono.h`, `FlexOS_Ultra_AppNotes.h`,
`FlexOS_Ultra_AppPaint.h`, `FlexOS_Ultra_AppSettings.h`, `FlexOS_Ultra_AppFiles.h`, `FlexOS_Ultra_FileKit.h`,
`FlexOS_Ultra_AppStorage.h`, `FlexOS_Ultra_StoragePair.h`, `FlexOS_Ultra_AppWeather.h`, `FlexOS_Ultra_WeatherKit.h`,
`FlexOS_Weather.h`, `FlexOS_Weather.cpp`, `FlexOS_Ultra_AppGames.h`, `FlexOS_Jumper.h`, `FlexOS_Jumper_Level.h`,
`FlexOS_Ultra_PkgApps.h`, `FlexOS_Ultra_NTP.h`, `FlexOS_Ultra_Clock.h`, `FlexOS_Ultra_Prefs.h`, `FlexOS_FS.h`.
**Leídas en parte (lo que toca a esta área):** `FlexOS_FS.cpp` (listado, papelera, nombres, `.fxp`), `FlexOS_Ultra_Keyboard.h`
(editor de Notas, `:700-1564`), `FlexOS_Ultra_AppFramework.h` (cabecera `uiHdr*`, contenedor Reloj, `APP_REG`, ganchos),
`FlexOS_Ultra_Types.h` (estado del cronómetro, `WxScene`), `FlexOS_Ultra_Session.h` (cadenas `CH[]`, `APP[]`, días/meses),
`FlexOS_Ultra_Shell.h` (enum `WT_*`), `FlexOS_StorageCore.h`, `FlexOS_StorageLink.h/.cpp`, `FlexOS_Mem.h/.cpp`,
`FlexOS_Ultra_AppDrawer.h` (uso de `pkgApps`), `FlexOS_Ultra_Touch.h` (clasificación tap/swipe), `FlexOS_Ultra_Core.h`
(`appTick`), `FlexOS_Ultra.ino` (orden de `loop()`), `FlexOS_Ultra_Home.h` (`buildLongDate/ShortDate`).

Las rutas `archivo:línea` se refieren a `FlexOS_Ultra/` salvo que se diga otra cosa. Los colores se dan en hex de los
valores de 8 bits del código (`rgb565(r,g,b)`); en pantalla salen cuantizados a RGB565. Los tokens `TH_*`, `thCard()`,
`wallAccent()`, `uiSurface*`, el material Liquid Glass (`uiGlass`), la tipografía ("tamaño" 1 = 5×7, ≥2 = Outfit
escalada) y el framework de apps están definidos en **02** (§2, §4, §5, §7, §8) y aquí solo se referencian.

---

## Índice

0. Convenciones compartidas del área
1. Reloj (contenedor con pestañas: Hora, Cronómetro, Temporizador)
2. Cronómetro: lógica, pestaña, cápsula de la barra de estado y tarjeta expandida
3. Calculadora
4. Calendario
5. Kit de archivos compartido (menú contextual, diálogo de nombre, confirmación, Papelera, "sin almacenamiento")
6. Notas (lista + editor)
7. Paint (galería + lienzo + formato `.fxp`)
8. Archivos (explorador `ST_FILES`)
9. Almacenamiento (principal, detalle de memoria, Flex Cloud en el teléfono)
10. Flex Storage: cuadro de aprobación de emparejamiento (overlay global)
11. Ajustes (todas las categorías y opciones)
12. Hora del sistema y NTP
13. Clima: motor Open-Meteo, app y widgets
14. Juegos: Jumper
15. Apps descargadas: modelo de la Caja de aplicaciones
16. Persistencia consolidada del área
17. Servicios, tareas y orden en `loop()`
18. Riesgos de migración y rarezas que no hay que copiar
19. Checklist de funcionalidades

---

## 0. Convenciones compartidas del área

### 0.1 Gestos (clasificación del motor táctil, `Touch.h:332-339`)

| Señal | Definición | Uso en esta área |
|---|---|---|
| `T.pressed` | Flanco de apoyo (un cuadro) | Ancla de arrastres (`dragY0`, `dragS0`) |
| `T.down` | Dedo apoyado | Arrastres, pulsación larga, trazo de Paint, salto de Jumper |
| `T.released` | Flanco de soltar | Fin de arrastre, inercia, teclado del diálogo de nombre |
| `T.tap` | Al soltar: `|dx|<16 && |dy|<16 && duración<550 ms`; **x,y = punto de APOYO** | Casi todas las acciones |
| `T.swipeUp/Down/Left/Right` | Al soltar: desplazamiento > 55 px en el eje dominante | Cierre de la tarjeta del cronómetro |
| Pulsación larga | Cada app la mide: `T.down && millis()-T.downMs > umbral && |x-startX|,|y-startY| < tolerancia` | Notas/Paint/Archivos 550 ms ±14 px; cápsula 700 ms ±12 px; editor de Notas 500 ms ±12 px |

> En LVGL: `LV_EVENT_CLICKED` (con `LV_INDEV_DEF_SCROLL_LIMIT` ≈ 16 px), `LV_EVENT_LONG_PRESSED` (fijar
> `long_press_time` por objeto: 550/700/500 ms), `LV_EVENT_GESTURE` para swipes (`lv_indev_set_gesture_limit` ≈ 55).

### 0.2 Marco de app y cabecera compartida

* Área de ventana: `WIN_TOP = 96`, `WIN_BOT = 736` (`uiBox` = `0, 96, 480, 640`); a pantalla completa vertical
  `uiPad() = 20`, `uiGap() = 15` (02 §8.4). Barra de navegación del sistema `NAV_H = 64` en modo "Botones"
  (`navBarH()` = 64 o 0 en "Gestos iOS", `AppFramework.h:979-985`); la estampa el sistema y **atiende sus toques antes
  que cualquier app**, incluidas las `APP_OWN_TOUCH` (`Core.h:986-990`).
* Cabecera compartida `uiHdrDraw(title, fs, txt, nav, menu)` (`AppFramework.h:403-443`): zona "atrás" 56×56 en (0,0)
  con chevron centrado (trazo 2.4 px, brazo 8 px), zona "menú" 56×56 en (424,0) con tres puntos r=4 separados 14 px,
  título desde x=64 hasta x=416, centrado verticalmente en y=28, reduciendo el tamaño hasta caber y recortando.
  Banda de cabecera `UIHDR_H = 76`. `uiClipViewport(top, bot)` = recorte **exclusivo** del área con scroll.
* Banderas del registro (`AppFramework.h:65-75`): `APP_CUSTOM_HEADER` (pinta su cabecera), `APP_OWN_TOUCH`
  (gestiona todos sus toques), `APP_LAND` (dibuja en horizontal), `APP_FLEX` (maqueta contra `gAppW/gAppH`, sirve en
  ventana de Modo PC), `APP_BG_KEEP`, `APP_IMMERSIVE`.

### 0.3 Registro de apps del área (`AppFramework.h:737-795`, nombres `Session.h:345-365`)

| Id | `IC_*` | Nombre ES (EN/FR/PT/IT) | Banderas | Categoría | Inicio de fábrica | Ganchos (`AppHooks`) |
|---|---|---|---|---|---|---|
| 0 | `IC_RELOJ` | Reloj (Clock/Horloge/Relógio/Orologio) | `APP_FLEX` | Esenciales | Sí | ninguno |
| 3 | `IC_ALMACEN` | Almacenamiento (Storage/Stockage/Armazenamento/Archivi) | `APP_FLEX` | Sistema | Sí | `H_ALM`: backScreen, suspend, resume, close |
| 5 | `IC_NOTAS` | Notas (Notes/Notes/Notas/Note) | `CUSTOM_HEADER\|OWN_TOUCH` | Productividad | Sí | `H_NOTES`: backLayer, backScreen, suspend, resume, close, save/load sesión, dirty |
| 8 | `IC_PAINT` | Paint (Paint/Dessin/Paint/Disegno) | `CUSTOM_HEADER\|OWN_TOUCH` | Ocio | Sí | `H_PAINT`: backScreen, suspend, resume, close, save/load, dirty |
| 9 | `IC_JUEGOS` | Juegos (Games/Jeux/Jogos/Giochi) | `OWN_TOUCH\|CUSTOM_HEADER\|APP_LAND` | Ocio | Sí | `H_GAMES`: suspend, resume, close |
| 10 | `IC_AJUSTES` | Ajustes (Settings/Réglages/Ajustes/Impostazioni) | `CUSTOM_HEADER` | Sistema | Dock | `H_SETTINGS`: backScreen, suspend, resume, save/load, bgWork |
| 11 | `IC_CALC` | Calculadora (Calculator/Calculatrice/Calculadora/Calcolatrice) | `APP_FLEX` | Productividad | Dock | `H_CALC`: resume, save/load |
| 12 | `IC_CALEND` | Calendario (Calendar/Calendrier/Calendário/Calendario) | `APP_FLEX` | Productividad | Dock | `H_CALEND`: resume |
| 14 | `IC_CLIMA` | Clima (Weather/Météo/Clima/Meteo) | `CUSTOM_HEADER\|OWN_TOUCH` | Esenciales | Dock (no nace en la rejilla) | `H_WEATHER`: backScreen, suspend, resume, shed |

Archivos **no es una app**: es el estado `ST_FILES` que se abre desde Almacenamiento (§8). Ajustes nunca se puede ocultar
(`appCanHide`, `Prefs.h:124`). Ajustes y Almacenamiento, Reloj y Calculadora son las apps del modo seguro
(`Recovery.h:551`).

### 0.4 Sesión de app (formato en 02 §8.12)

Ficheros en `/System/Sessions/` (cabecera `SessHdr` 16 B + carga, CRC-32, escritura atómica, carga perezosa la
primera vez que se abre la app, guardado diferido por `sessMarkDirty`):

| Fichero | Versión | Estructura (tamaño real en RV32/x86-64) | Ref. |
|---|---|---|---|
| `calc.bin` | 1 | `CalcSessV1 { double acc; char disp[24]; char op; u8 fresh, err, rsv; }` (40 B) | `AppsBasic.h:315-337` |
| `notas.bin` | 3 | `NoteSessV3 { u8 view, kbLayout, kbFlags, reserved; u16 cursor; i16 selA, selB; i32 editorScroll, listScroll; char path[96]; }` (116 B) | `AppNotes.h:389-467` |
| `paint.bin` | 1 | `PaintSessV1 { u8 view, sizeIx; u16 color; i32 scroll; char path[96]; }` (104 B) | `AppPaint.h:544-566` |
| `ajustes.bin` | 1 | `SetSessV1 { i16 view, sel, scroll, listScroll; }` (8 B) | `AppSettings.h:754-782` |

Almacenamiento, Calendario, Clima, Juegos y Reloj **no** guardan sesión (Clima conserva su estado en RAM mientras está
suspendida).

### 0.5 Idiomas

`cfgLang`: 0 ES, 1 EN, 2 FR, 3 PT, 4 IT, 5 ZH. `LI()` = índice de tabla (ZH → EN porque la fuente no tiene glifos CJK,
`Session.h:304`). **Solo están traducidos**: la tabla `CH[]` (`t(S_*)`, `Session.h:308-341`), los nombres de app, días y
meses, la tabla de Clima `WXT[]`, nombres de condición/errores del motor del tiempo y las categorías de la caja. **El
resto de textos de esta área son literales en español** (Notas, Paint, Archivos, Ajustes, Almacenamiento…). En IDF
conviene centralizarlos en un catálogo i18n, conservando el texto ES exacto como valor por defecto.

---

## 1. Reloj (contenedor con pestañas)

### 1.1 Propósito y entrada
App `IC_RELOJ`. Se abre desde Inicio/caja/dock, desde la cápsula del cronómetro (va directa a la pestaña Cronómetro,
§2.6) o desde la tarjeta expandida. Es la "app de referencia" del marco adaptativo (también funciona en ventana de Modo
PC: `APP_FLEX`). **No existen alarmas** en el firmware (ni UI ni motor; `grep alarm` solo aparece en Flex Phone).
La pestaña **Temporizador** es un marcador "En construcción".

### 1.2 Layout (pantalla completa, `AppFramework.h:496-622`)
* Contenedor `appRelojRender()`: rellena `uiBox` con `WIN_BG`, pinta la barra de pestañas, delega el cuerpo de la
  pestaña activa (`gRelojTab`: 0 Hora, 1 Cronómetro, 2 Temporizador) y hace **un** volcado `WIN_TOP..WIN_BOT`.
* **Barra de pestañas** (`relojDrawTabs`, `:511`): solo si `uiH() ≥ 46+60`. Rectángulo `x = 20, y = 101, w = 440,
  h = 36`, radio 18, `uiSurface(UIS_CARD)`. Tres segmentos de 146 px. Activo: relleno `TH_PRIM` inset 2 px, radio 16,
  texto `TH_ONACC`; inactivo: texto `TH_TXT2`. Texto `uiFontFit(label, seg-12, 2)`, centrado.
  Etiquetas `t(S_CRN_HOUR/STOPW/TIMER)`: **"Hora" / "Cronómetro" / "Temporizador"** (EN "Clock/Stopwatch/Timer",
  FR "Heure/Chronomètre/Minuteur", PT "Hora/Cronômetro/Temporizador", IT "Ora/Cronometro/Timer").
* Cuerpo de pestaña `relojBody()` (`:501`): `uiBox` menos 46 px de pestañas → a pantalla completa `x 0, y 142,
  w 480, h 594` (si el lienzo mide < 106 px de alto, sin pestañas).
* **Pestaña Hora** (`appRelojHoraRender`, `:555`): reloj vectorial grande `drawBigClock(clkStr12, cx = 240, cy =
  by + pad + bh/12, capH, thick, TH_TXT)` con `capH = min(bh·2/5, (bw-2·pad)·10/(len·7+2), 150)` (≥ 22), trazo `capH/8`
  (≥ 3). `clkStr12` = `"H:MM"` sin AM/PM (el reloj vectorial solo tiene dígitos y ':'). Debajo, sección opcional 0
  (si quedan ≥ 26 px): fecha larga `buildLongDate` (`"sábado, 4 de julio"` ES/PT; `"samedi 4 juillet"` FR/IT;
  `"Saturday, July 4"` EN) en `TH_TXT2`. Sección opcional 1 (si `bw ≥ 250` y quedan ≥ 90 px): dos tarjetas
  `thCard()` radio `uiPad`, alto ≤ 130: **"Fecha"** → fecha corta `"sáb, 4 jul"`; **"Formato"** → `"24 h"` / `"12 h"`.
  Pie `"Reloj de FlexOS"` `TH_MUTE` tamaño ≤ 2 a `by+bh-pad-uiLineH(2)`. Las secciones aparecen/desaparecen con fundido
  `UI_FADE_MS = 130` (02 §8.4).
* **Pestaña Temporizador** (`:603`): `t(S_CRN_TIMER)` ("Temporizador") tamaño ≤ 4 `TH_TXT` en `cy-24` y `t(S_SOON)`
  ("En construcción") ≤ 2 `TH_TXT2` en `cy+16`.

### 1.3 Interacción
* `relojTabsTouch()` (`:533`): un *tap* dentro de la barra cambia de pestaña (índice por x/146), consume el toque y
  repinta todo. Tocar la activa no hace nada.
* Pestaña Hora: se repinta cuando cambia el minuto (`gMinChanged`, `:629`). Pestaña Cronómetro: §2.4.
* Atrás: chevron estándar del marco (`y ≤ 96 && x < 72`) o barra del sistema → `appClose()`.

### 1.4 Migración
`lv_tabview` (o `lv_buttonmatrix` segmentado) con 3 páginas; reloj grande = etiqueta con fuente vectorial grande
(02 §5.4). Repintado por minuto con un `lv_timer` alineado al cambio de minuto (evento del servicio de hora, §12).
Pendiente funcional: el Temporizador y las alarmas **no existen**; no inventarlos sin especificación.

---

## 2. Cronómetro

Cuatro piezas con una sola fuente de verdad (`AppChrono.h:30-52`): **lógica**, **pestaña** de Reloj, **cápsula** en la
barra de estado y **tarjeta expandida** (overlay modal). Hay dos enganches en `loop()`: `cronoOverlayTouch()` (tacto,
antes que nadie, `.ino:736`) y `cronoCapsuleTick()` (dibujo, al final, `.ino:1014`); además `cronoCardTick()` se queda
la pantalla mientras la tarjeta está visible (`.ino:888-894`).

### 2.1 Lógica (pura, `AppChrono.h:107-209`, estado en `Types.h:481-566`)

Estado: `gCronoSt ∈ {CRONO_IDLE=0, CRONO_RUN=1, CRONO_PAUSE=2}`, `gCronoAccum` (ms consolidados), `gCronoT0`
(`millis()` del último arranque/reanudación), `gCronoLapBase` (total en que empezó la vuelta en curso),
`gCronoLaps[20]` (`{u32 split; u32 total;}`), `gCronoNLaps`, `gCronoLap0` (número visible de la vuelta del índice 0,
empieza en 1), `gCronoBest/Worst` (índices, −1 = no procede), banderas *dirty* (botones, esfera, vueltas, barra).

```
           Iniciar (derecho)            Det. (derecho)
  IDLE ─────────────────────────► RUN ───────────────► PAUSE
   ▲                               ▲  │ Parcial (izq.)    │ Continuar (derecho)
   │                               │  └─► marca vuelta    │
   │                               └──────────────────────┘
   └──────────────── Reinic. (izquierdo, solo en PAUSE) ──┘
```

* `cronoElapsed()` = `accum + (millis() - t0)` si RUN, si no `accum`. Resta **sin signo** (correcta aunque `millis()`
  desborde). `cronoLapElapsed() = elapsed - lapBase`. Número de la vuelta en curso = `lap0 + nLaps`.
* `cronoStart()`: si IDLE pone `accum = lapBase = 0`; re-ancla `t0 = millis()`; RUN. `cronoPause()`: consolida
  `accum += millis()-t0`; PAUSE. `cronoReset()`: todo a cero, `lap0 = 1`, IDLE.
* `cronoLapMark()` (solo en RUN): si hay 20 vueltas, descarta la **más antigua** (compacta) y `lap0++` (los números
  visibles nunca retroceden; tope `0xFFFF`); añade `{split = total - lapBase, total}`; `lapBase = total`; recalcula
  extremos.
* `cronoRecalcExtremes()`: con < 3 vueltas no hay mejor/peor; si todas iguales tampoco; si no, índice del `split`
  mínimo (mejor) y máximo (peor).
* Botón izquierdo: RUN → Parcial; PAUSE → Reiniciar; IDLE → nada. Botón derecho: RUN → pausar; si no → iniciar.
* Etiquetas: izquierda `PAUSE ? "Reinic." : "Parcial"`; derecha `RUN ? "Det." : PAUSE ? "Continuar" : "Iniciar"`
  (`t(S_CRN_BRESET/BLAP/BSTOP/S_CONTINUE/BSTART)`; EN "Reset/Lap/Stop/Continue/Start", FR "Réinit./Tour/Arrêt/Continuer/
  Démarrer", PT "Zerar/Parcial/Parar/Continuar/Iniciar", IT "Azzera/Parziale/Stop/Continua/Avvia").
* `cronoFmt(ms, cent)` (único formateador): `h>0` → `"%lu:%02lu:%02lu.%02lu"` / `"%lu:%02lu:%02lu"`; si no
  `"%02lu:%02lu.%02lu"` / `"%02lu:%02lu"` (centésimas = `(ms%1000)/10`). Casos de prueba existentes
  (`tests/host/ino_compile.cpp:2215-2220`): 0 → `"00:00.00"`, 55730 → `"00:55.73"`, 72870 → `"01:12.87"`/`"01:12"`,
  3723456 → `"1:02:03.45"`/`"1:02:03"`.
* **Sin persistencia**: un reinicio pierde el cronómetro (hay un comentario con el diseño previsto: volcar a NVS al
  pausar/parar/apagar, nunca por segundo, `Types.h:568-573`).

### 2.2 Icono vectorial (`cronoGlyph(cx, cy, r, col, th)`, `:228`)
Esfera = anillo de radio `r`, grosor `max(2, 2·th)`; pulsador superior: segmento horizontal de `±0.42r` a
`y = cy - r - 0.42r` y vástago vertical de `cy - 1.55r` a `cy - 0.92r`; aguja del centro hacia las "2 en punto"
(`+0.52r, -0.46r`). Usado en la cápsula (r = 7, trazo 1.5) y en la tarjeta (r = 15, trazo 2.2).

### 2.3 Pestaña Cronómetro de Reloj — layout (`cronoLayout`, `:413`, valores a pantalla completa)

Cuerpo `relojBody` = `(0, 142, 480, 594)`, `pad = 20`.

| Elemento | Fórmula | Valor a 480×800 |
|---|---|---|
| Radio de botones `btnR` | `clamp(bw/5, 22, 48)` | 48 |
| Centro Y botones `btnCy` | `by + bh - btnR - pad` | 668 |
| X botón izquierdo / derecho | `bw/4`, `bw·3/4` | 120 / 360 |
| Radio de marcas `rTick` | `clamp(min(bw/2-pad, (bh-2btnR-2pad)/2), 40, 150)` | 150 |
| Radio del disco `rDisc` | `rTick·78/100` | 117 |
| Centro esfera | `(bx+bw/2, by+rTick+pad)` | (240, 312) |
| Banda de la esfera | `cy ± (rTick+3)` | 159..465 |
| Tabla de vueltas: `tblY` | `dialY1 + pad` | 485 |
| Filas visibles | `min(4, ((btnCy-btnR-pad)-(tblY+30))/32)`, tabla solo si `bw ≥ 260` | 2 |
| Columnas Vuelta/Parcial/Total | `bw·15%`, `50%`, `84%` (centros) | 72 / 240 / 403 |

**Esfera** (`cronoDrawDial`, `:457`): fondo `WIN_BG`; 60 marcas radiales: cada 5 (mayores) de `r` a `0.87r`, trazo 1.9,
`TH_TXT2`; menores de `r` a `0.93r`, trazo 1.1, `TH_MUTE`. Disco central `TH_SURF2` radio `rDisc` (AA). Dos agujas con
cola de `0.14r` y longitud `0.93r`: **vuelta en curso** (una vuelta de esfera por minuto: ángulo = `(lap % 60000)/60000 ·
360° - 90°`) trazo 1.4 `TH_TXT2`; **total** trazo 2.3 color acento `wallAccent()`. Eje: círculo acento radio
`0.055r + 3`. Tiempo central `cronoFmt(ms, true)` centrado en `(cx, cy - 2.3·fs)` con el mayor tamaño ≤ 7 que quepa en
`2·rDisc - 18` px, `TH_TXT`.

**Tabla de vueltas** (`cronoDrawLaps`, `:513`): cabeceras **"Vuelta" · "Tiempo parcial" · "Tiempo total"**
(`S_CRN_LAP/SPLIT/TOTAL`; EN "Lap / Lap time / Total time") centradas en sus columnas, tamaño `uiFontFit("Tiempo
parcial", bw/3-8, 2)`, `TH_TXT2`; divisor 1 px `TH_DIV` de `pad` a `bw-pad` en `tblY + uiLineH(fs) + 6`; filas cada 32
px desde `yLine + 8`, **más reciente arriba**, tamaño 2: número `"%02d"` (`lap0+i`), parcial y total con centésimas.
Color: mejor vuelta = acento, peor = `TH_DANGER`, resto `TH_TXT`. Vacía: **"(Sin vueltas)"** tamaño 2 `TH_MUTE`
centrado.

**Botones** (`cronoDrawButtons`, `:550`): izquierdo círculo relleno `thCard()` radio 48, etiqueta tamaño
`uiFontFit(label, 2r-12, 3)`, color `TH_TXT` si el cronómetro no está en IDLE, si no `TH_DIS`. Derecho: círculo
relleno con alfa 70 de `TH_DANGER` (si RUN) o acento, más contorno 1 px del mismo color y etiqueta del mismo color.

### 2.4 Pestaña Cronómetro — interacción y refresco
* `cronoTabTouch()` (`:585`): *tap* dentro de un círculo de radio `btnR + 6` centrado en cada botón → acción
  izquierda/derecha; consume el toque.
* `cronoTabTick()` (`:601`): repinta botones si `gCronoBtnDirty`, la tabla si `gCronoLapsDirty`, y la esfera a **~30 fps
  (cada 33 ms) solo mientras RUN** o si está invalidada (una vez). Parado no consume nada.
* La pestaña se pinta compuesta en el back buffer y publicada de una vez (`present`); en horizontal/hospedada se dibuja
  directa.

### 2.5 Cápsula compacta en la barra de estado (`:242-393`)

* **Cuándo**: con el cronómetro activo (RUN o PAUSE) y una superficie válida (`cronoBarSurface`, `:316`):
  1 = escritorio (sin cortina abierta, sin animación de cortina, sin modo edición); 2 = marco estándar de app (apps
  **sin** `APP_CUSTOM_HEADER`). Nunca en: suspensión, horizontal, hospedada en DeX, app que oculta la barra de estado,
  app inmersiva, Modo Kiosco, bloqueo. (Clima pinta la cápsula en su propio cromo con `cronoBarClock`, pero allí no
  escucha toques ni se actualiza por segundo; ver §18.)
* **Geometría fija** (`:83-88`): `y = 12`, `h = 26`, icono 18, `padL 8`, `gap 6`, `padR 11`. Ancho = `8 + 18 + 6 +
  textW(plantilla, 2) + 11` con plantilla `"00:00"` (< 1 h) o `"0:00:00"` (≥ 1 h): el ancho depende solo de la clase de
  formato, nunca de los dígitos (permite repintarla encima de sí misma).
* **Posición** (`cronoBarClock`, `:291`): la hora normal de la barra se pinta en `(20, y)` tamaño 2; la cápsula en
  `x = 20 + textW(hora, 2) + 12`. Si `x + ancho > 480 - 66 - 12 = 402` (no pisa Wi-Fi ni batería), la cápsula se mueve a
  `x = 20` y **la hora normal no se dibuja**.
* **Aspecto** (`cronoCapsuleDraw`, `:262`): píldora **opaca** radio 13 color acento activo (`uiSurfFlat(UIS_ACCENT)` =
  `wallAccent()`), **sin vidrio a propósito** (se re-estampa sobre sí misma cada segundo; con vidrio se desenfocaría su
  propia salida). Icono en `(x + 8 + 9, y + 14)` radio 7 trazo 1.5 `TH_ONACC`; tiempo `"MM:SS"`/`"H:MM:SS"` tamaño 2
  `TH_ONACC` en `x + 32`.
* **Refresco** (`cronoCapsuleTick`, `:371`): si debe aparecer/desaparecer o cambia de ancho → rehace la barra entera
  (escritorio: `renderHome()+showHome()`; marco de app: rellena `0..46` con `WIN_BG` y `appDrawChrome`); si solo cambió
  el segundo o el estado → re-estampa la píldora (banda `11..39`). No hace nada mientras la tarjeta está visible ni
  durante un OTA.
* **Toque** (`cronoOverlayTouch`, `:911-930`, zona = píldora ±6 px):
  * *Tap* sobre el **icono** (`x ≤ capX + 8 + 18 + 3`) → `cronoOpenApp()` (abre Reloj en la pestaña Cronómetro).
  * *Tap* en el resto → `cronoCardOpen()` (tarjeta expandida).
  * **Pulsación larga** > 700 ms sin moverse más de 12 px → `cronoOpenApp()` (una vez por contacto).
* `cronoOpenApp()` (`:617`): cierra la tarjeta al instante; si Reloj ya está delante, cambia a la pestaña 1; si hay otra
  app, `appClose()`; si Reloj tiene **candado de app** y hay PIN → pide verificación (`lsuStartVerifyFor(LSU_AFTER_
  OPENAPP, IC_RELOJ)`); si no, `enterApp(IC_RELOJ)` con `gRelojTab = 1`.

### 2.6 Tarjeta expandida (overlay modal propio, `:637-871`)

* **No es una notificación**: no caduca; solo la cierra el usuario. Mientras está visible `loop()` le cede la pantalla
  (nadie más compone) y el tacto global le llega antes que a la isla de notificaciones.
* **Apertura** (`cronoCardOpen`, `:770`): no en OTA, horizontal ni hospedada. Necesita PSRAM para dos bandas
  (`480×236×2` = banda `y 8..243` capturada y `480×96×2` = sub-banda dinámica); si no hay → abre la app. Captura la banda
  real de pantalla, pre-desenfoca esa banda una vez si Liquid Glass está activo, estado `CC_OPENING`.
* **Geometría** (`:91-105`): `x = 20, y = 30, w = 440, h = 196`, radio 42. Animación **190 ms**, curva ease-out cúbica
  `1-(1-p)^3`, morfeo lineal de `x, y, w, h, radio` desde la cápsula (`capX, 12, capW, 26, r13`) a la tarjeta. Superficie
  `uiSurfaceA(UIS_ELEVATED)` con alfa 200 (vidrio) / 240 (plano) **en todos los cuadros**; encima un velo del acento con
  alfa `255·(1-p)` (morfeo de color cápsula→tarjeta). El contenido entra a partir de `p ≥ 0.55` con alfa
  `255·(p-0.55)/0.45`. Cierre: misma animación invertida (190 ms) y restauración píxel a píxel de la banda capturada.
* **Contenido**: cuadrado acento 62×62 radio 20 en `(44, 56)` con el glifo (r = 15, trazo 2.2, `TH_ONACC`, solo cuando
  alfa > 200); tiempo con centésimas en `x = 126, y = 60`, tamaño máximo 7 que quepa en 314 px, `TH_TXT`;
  **"Vuelta N"** (`t(S_CRN_LAP)` + número de la vuelta en curso) tamaño 2 `TH_TXT2` en `(126, 112)`; separador vertical
  2 px `TH_DIV` en `x = 239`, `y 168..208`; etiquetas de los dos botones tamaño 3 centradas en `x = 130` y `x = 350`,
  `y = 178` (izquierda `TH_DIS` si IDLE / `TH_TXT`; derecha `TH_DANGER` si RUN / acento).
* **Refresco en reposo**: ~30 fps solo la sub-banda dinámica `y 52..147` (tiempo + "Vuelta N"); además re-estampa la
  cápsula si cambió el segundo (sus filas 12..38 asoman por encima de la tarjeta).
* **Toque** (`cronoOverlayTouch`, `:887-909`): mientras anima, se tragan todos los eventos. Abierta:
  * *Swipe* hacia arriba → contraer.
  * *Tap* fuera de la tarjeta → contraer.
  * *Tap* con `y ≥ 158` (fila de botones): mitad izquierda (`x < 240`) → acción izquierda; derecha → acción derecha.
    Si tras la acción el cronómetro queda IDLE (Reinic.) → contraer; si no, recompone etiquetas.
  * *Tap* en el resto de la tarjeta (tiempo o icono) → abrir la app (cierre instantáneo).
  * Se descartan `pressed/released/swipeLeft/Right/Down` (modal).
* **Abandono sin restaurar** (`cronoCardDrop`): si un OTA toma la pantalla o el estado deja de ser `ST_HOME/ST_APP`
  (bloqueo, apagado, Modo PC…). Con la pantalla apagándose (`gSuspOn`) no se dibuja pero sigue abierta.

### 2.7 Migración del cronómetro
* **Lógica pura reutilizable tal cual**: `cronoElapsed/Start/Pause/Reset/LapMark/RecalcExtremes/Fmt` → módulo C sin
  dependencias (`now_ms` inyectado) con pruebas en `tests/host` (formato, desbordamiento de `millis`, 21 vueltas →
  descarte de la más antigua y numeración continua, extremos con 2/3 vueltas e iguales).
* Servicio global `flex_chrono` (estado + eventos `CHRONO_STATE_CHANGED`, `CHRONO_SECOND_TICK`) consumido por: pestaña
  Reloj, cápsula de la barra de estado (objeto LVGL en la capa superior `lv_layer_top`, ancho fijo por clase de formato)
  y tarjeta (objeto en `lv_layer_top` con `lv_anim` de `x/y/w/h/radius` 190 ms `lv_anim_path_ease_out` y fundido de
  contenido). En LVGL no hace falta capturar/restaurar bandas.
* Esfera: `lv_scale` circular (60 marcas, mayores cada 5) + dos `lv_line` como agujas + etiqueta; refresco con
  `lv_timer` de 33 ms solo en RUN.
* Respetar: cápsula sin vidrio, prioridad cápsula > hora normal, zona de toque del icono vs resto, umbral 700 ms.

---

## 3. Calculadora

### 3.1 Propósito y entrada
App `IC_CALC` (dock). Calculadora de cuatro operaciones con línea de operación visible ("25 + 10"), porcentaje, cambio
de signo, borrado y "Error" explícito. **Sin teclas de memoria** (se retiraron; `AppsBasic.h:163-169`).

### 3.2 Lógica (pura, `AppsBasic.h:33-152`)
Estado: `calcDisp[24]` (entrada en curso, `"0"`), `calcAcc` (double), `calcOp ∈ {0,'+','-','x','/'}`, `calcFresh`
(el próximo dígito empieza una entrada nueva), `calcErr`.

| Tecla (etiqueta → código) | Efecto |
|---|---|
| `0..9` | Si `fresh` o el display es `"0"` → sustituye; si no, añade (máx. 16 caracteres). `fresh=false` |
| `.` | Si `fresh` → `"0."`; si no y no hay punto y `L<15` → añade |
| `C` → `'c'` | Display `"0"`, acc 0, sin operador, `fresh=true` |
| `DEL` → `'\b'` | Si no `fresh` y hay texto: borra el último carácter; vacío → `"0"` |
| `+/-` → `'n'` | Si el display no es `"0"`: quita o antepone `'-'` |
| `%` | `display = display/100` (formateado), `fresh=true` |
| `+ - x /` | Si había operación a medias y no `fresh` → la cierra (encadenado: `2+3+4 = 9`); si el resultado no es finito → `"Error"`. Si no había operador → `acc = display`. Si había operador y `fresh` → **solo cambia el operador**. `op = k`, `fresh=true` |
| `=` | Si hay operador → `acc = acc op display`, se formatea, `op = 0`; `fresh=true` |

* `calcCompute`: `/` por 0 devuelve `NaN` (antes devolvía 0). `calcFinite(v)` = `v==v && |v| < 1e308`.
* Formato `calcFmtTo`: `"%g"` (6 cifras significativas), `-0` → `0`, no finito → `"Error"`.
* Con `"Error"` en pantalla: `C`, dígito o `.` limpian todo (y el dígito/punto se aplica); operadores se ignoran.
* **Línea mostrada** `calcLine`: error → `"Error"`; sin operador → display; con operador y `fresh` → `"acc op"`
  (`"25 +"`); con operador → `"acc op display"` (`"25 + 10"`). Los símbolos son los de las teclas (`x`, `/`).
* Pruebas existentes: `tests/host/ino_compile.cpp:5998-6050` (`testCalculadora`: suma, resta, producto, división,
  porcentaje, encadenado `2+3+4=9`, cambio de operador, etc.).

### 3.3 Layout adaptativo (`calcLayout`, `:179-218`; valores a 480×640)
* Caja = `uiBox` (`0, 96, 480, 640`). Margen `m = clamp(gAppW/30, 4, 16)` = 16; `gap = clamp(gAppW/40, 3, 12)` = 12.
  La **rejilla tiene prioridad**: el display aspira a 1/5 del alto útil (≤ 120) pero cede hasta desaparecer.
* **Display**: `(16, 112, 448, 120)`, radio `clamp(dh/5, 2, 14)` = 14, `uiSurface(UIS_CARD)`. Texto alineado a la
  derecha en `x = 454`, tamaño 5 si `dh ≥ 90` (4 si ≥ 64, 3 si ≥ 40, si no 2), reducido hasta caber en `dw - 20`,
  centrado verticalmente (`dy + dh/2 - fs·4`), `TH_TXT`.
* **Rejilla 5×4**: teclas 103×84 en `x = 16, 131, 246, 361`; `y = 248, 344, 440, 536, 632`; radio `clamp(bw/6, 3, 14)` = 14.

```
  C    +/-   %    /
  7    8     9    x
  4    5     6    -
  1    2     3    +
  0    .     =    DEL
```
* Colores: columna de operadores (col 3) y la tecla `=` → **naranja de marca `#F59628`** con texto `TH_ONACC` (igual en
  claro y oscuro); fila 0 (C, +/-, %) → `TH_SURF2`; resto → `thCard2()`. Con Liquid Glass y vertical → panel de vidrio
  con ese tinte; si no, relleno plano.
* Fuente de tecla `calcFontFor`: `lim = min(w,h)`: ≥ 56 → 4, ≥ 38 → 3, ≥ 24 → 2, si no 1; etiquetas de más de un
  carácter un tamaño menos; se reduce hasta caber en `w-6`. A 480×800: 4 (3 para "+/-" y "DEL").

### 3.4 Interacción
* *Tap* dentro del rectángulo de una tecla (bordes incluidos) → `calcKeyFromLabel` → repinta **solo el display**
  (`calcRenderDisplay`) y marca la sesión. Sin estado "pressed" visual.
* `calcEnter`: la primera apertura tras el arranque recupera la sesión; las siguientes **reinician a "0"** (salvo
  re-maquetado `gRelayout`). `calcResume` repinta sin reiniciar.

### 3.5 Persistencia
`calc.bin` v1 (§0.4). Se ignora si `disp` está vacío o el operador no es `+-x/`.

### 3.6 Migración
* Lógica pura (`calcKey`, `calcLine`, `calcCompute`, `calcFmtTo`) → módulo C + pruebas host (portar `testCalculadora`).
* UI: `lv_buttonmatrix` 5×4 (mapa con `"\n"`), estilos por botón (`LV_BUTTONMATRIX_CTRL_CUSTOM_1` para naranja) y una
  `lv_label` alineada a la derecha con `LV_LABEL_LONG_SCROLL`/auto-reducción. `%g` produce p.ej. `1e+06`: conservar o
  documentar el cambio.

---

## 4. Calendario

### 4.1 Propósito y entrada
App `IC_CALEND` (dock). Vista del **mes en curso** con el día de hoy resaltado. **Sin navegación de meses ni eventos.**

### 4.2 Layout (`calRender`, `AppsBasic.h:351-403`; a 480×640, `pad 20`, `gap 15`)
* Fondo `WIN_BG`. Panel lateral **"Hoy"** si `bw ≥ 430` y quedan ≥ 28 px por celda: `sideW = min(bw/3, 210)` = 160 →
  **se muestra a pantalla completa vertical**. Rejilla `gridW = bw - sideW - gap - 2·pad` = 265 (celda `cw = 37`).
* Cabecera `"%s %d"` con el mes en minúscula de `MO_FULL` (`"julio 2026"`; EN `"July 2026"`), centrada sobre la
  rejilla, tamaño `uiFontFit(hdr, gridW, uiFontH(bh/12))`, `TH_TXT`.
* Fila de días de la semana **fija en español** `D L M M J V S` (empieza en domingo), `TH_TXT2`, tamaño
  `uiFontFit("W", cw-2, 2)`.
* Celdas: primer día `fw = ((rtcWd - (rtcD-1)) % 7 + 7) % 7`; filas `ceil((fw+dim)/7)`; alto `ch = availH/rows`
  (≥ 12); número del día tamaño `uiFontH(ch·2/3)` `TH_TXT`; **hoy** = círculo `TH_PRIM` radio `min(cw,ch)/2 - 2` (≥ 6)
  con número `TH_ONACC`. Nunca dibuja fuera del marco.
* Panel lateral (`x = 300, y = 116, w = 160, h = 600`, radio `pad`, `thCard()`): `"Hoy"` (literal, no traducido)
  `TH_TXT2`; día del mes enorme `TH_ACCS` (tamaño `uiFontFit(dd, sideW-24, uiFontH(shh/3))`); fecha larga
  `buildLongDate` `TH_TXT` tamaño ≤ 2. El panel entra/sale con fundido de sección (130 ms).

### 4.3 Interacción y estados
Sin toques propios (atrás = marco). Se repinta al cambiar el minuto (`calTick`, `:409`). `calResume` = repintar.

### 4.4 Migración
`lv_calendar` (fijar `lv_calendar_set_today_date`, primer día domingo, nombres de días localizados — **corregir** la fila
fija en español), y un `lv_obj` lateral con breakpoint de ancho. Actualizar con el evento "cambio de día" del servicio
de hora.

---

## 5. Kit de archivos compartido (`FlexOS_Ultra_FileKit.h`)

Piezas de interfaz que usan **Notas, Paint y Archivos** (y la Galería, fuera de esta área). Regla del kit (`:42-46`): no
simulan nada; cada acción llama a `flexFs*` sobre el fichero real y quien la usa **vuelve a leer el directorio** y
repinta. Si un borrado falla, el elemento sigue en la lista. Son **capas modales** con prioridad fija en el `tick` de
cada pantalla: Papelera → Confirmación → Nombre → Menú → contenido. `fkCloseAll()` cierra las cuatro y libera la lista
de la papelera; se llama al entrar/suspender/cerrar cada app (la superficie de diálogos es global).

### 5.1 Menú contextual (`:48-156`)
* Acciones: `FK_ACT_SEL=0` **"Seleccionar"**, `FK_ACT_DEL=1` **"Eliminar"**, `FK_ACT_REN=2` **"Renombrar"**,
  `FK_ACT_TRASH=3` **"Papelera"**, `FK_ACT_CLOUD=4` **"Subir a Flex Cloud"** (5.ª fila solo en Archivos sobre un fichero).
* Geometría: `w = 272`, fila 46, relleno 10 → `h = 4·46+20 = 204` (o 250 con 5 filas). Origen pedido `(px, py)`
  (normalmente el punto del toque con `y-40`, o `(440, 56)` desde los tres puntos); se ajusta a `x ∈ [8, 200]`,
  `y ∈ [30, 730-h]`. Superficie `uiSurface(r=18, UIS_ELEVATED)`.
* Filas: etiqueta tamaño 3 en `(x+16, ry+10)` recortada antes de `x+w-42`; color `TH_DANGER` para "Eliminar", resto
  `uiSurfOn(UIS_ELEVATED)`; icono vectorial centrado en `(x+w-32, ry+23)`: mano que pulsa (`TH_TXT2`), papelera **roja
  rellena** (borrado definitivo), campo de texto con cursor, papelera **blanca de contorno** (`TH_TXT`, mover a la
  papelera), nube con flecha arriba.
* `fkMenuHit`: fuera del panel → −1 (cierra sin acción); dentro → fila (acotada). El menú se cierra con cualquier *tap*.

### 5.2 Diálogo de nombre con teclado (`:164-251`)
* Pantalla completa sobre `TH_PAGE` hasta el panel del teclado. Título tamaño 3 centrado en y=40 (p. ej.
  **"Renombrar nota"**, **"Renombrar dibujo"**, **"Renombrar"**); chevron "cancelar" en (18..30, 30..46).
* Campo `uiSurfaceFlat(24, 130, 432, 56, r14, UIS_CARD)`; texto tamaño 2 en (38, 146) recortado a 440; cursor 2×28 color
  `wallAccent()` tras el texto. Ayuda tamaño 1 `TH_TXT2` en y=206: **"Escribe el nombre y pulsa Guardar"** (o el texto
  `hint` que pase el llamante, p. ej. la Galería).
* Teclado del sistema (02/teclado): panel vidrio o `TH_KEYPANEL` desde `KB_Y-4`; 3 filas de teclas; fila de función:
  **"shift"**, capa (ABC/NUM/EMOJI), **"ES"/"EN"**, **"espacio"**, **"<-"**, **"Guardar"**. Al abrir: capa ES, sin
  mayúsculas, sin barra/chips (`kbExtrasOn = false`).
* `fkNameTick` actúa **al soltar** (`T.released`): shift conmuta; capa cicla NUM → EMOJI → ES/EN → NUM; idioma
  conmuta ES/EN; espacio; borrar un **carácter UTF-8** completo; "Guardar" → devuelve 1 si el nombre no está vacío (si
  está vacío devuelve −1 = cancelado). *Tap* en `x < 60, y < 90` → cancelar (−1). Cualquier otra tecla añade su texto.
  **Se rechaza cualquier texto que contenga `'/'`**. Longitud máx. `FLEXFS_NAME_MAX-1 = 47` bytes.
* `flexFsRename(path, nuevo)`: solo nombre; si el nuevo no trae extensión **conserva la del original**; no pisa un
  fichero existente (devuelve false y no avisa).

### 5.3 Confirmación (`:259-297`)
* Tarjeta `x = 36, y = 290, w = 408, h = 220`, `uiSurface(r24, UIS_ELEVATED)`. Mensaje tamaño 2 centrado en y+26;
  subtítulo tamaño 1 `TH_TXT2` en y+62. Botones en `y = 434`, alto 56, radio 16, ancho 180: **"Cancelar"** (`TH_SURF`,
  texto `TH_TXT`, x 52) y **"Borrar"** (`TH_DANGER`, texto `TH_ONACC`, x 268).
* *Tap* en Cancelar → −1; en Borrar → 1; **fuera de la tarjeta → −1** (cancelar); dentro sin botón → nada.
* Mensajes usados: **"¿Borrar definitivamente?"** + nombre; **"¿Vaciar la papelera?"** + **"Se borrará todo su contenido"**.

### 5.4 Papelera compartida (`:315-489`)
* Abre con `fkTrashOpen()` (desde el menú sin elemento seleccionado → acción "Papelera"). Lista **real** de `/Papelera`
  (hasta 256 elementos, en PSRAM mientras está abierta; `fkTrashTotal` = recuento real aunque supere el tope).
* Layout: fondo `TH_PAGE`; chevron (18..30, 10..26); título **"Papelera"** tamaño 4 centrado en y=34; subtítulo tamaño
  1 `TH_TXT2` en y=86: `"%d elementos  ·  %s"` (tamaño total de la papelera) o `"%d elementos (se ven %d)  ·  %s"`.
  Vacía: **"La papelera está vacía"** tamaño 2 `TH_TXT2` en y=320.
* Filas desde y=120, paso 66 (tarjeta 448×58, x 16, radio 14, `uiSurfaceFlat(UIS_CARD)`; seleccionada `TH_SEL`): nombre
  original (último segmento de la ruta decodificada) tamaño 2 en (30, y+8); `"<ruta original>  ·  <tamaño>"` tamaño 1
  `TH_TXT2` en y+34.
* Barra inferior `y = 678`, alto 56, radio 16: con selección → **"Restaurar"** (`TH_OK`, x 16, w 216) y **"Borrar"**
  (`TH_DANGER`, x 248, w 216); sin selección y con elementos → **"Vaciar papelera"** (`TH_SURF2`, texto `TH_DANGER`,
  x 120, w 240).
* Interacción: arrastre vertical con umbral 8 px (scroll máx. `120 + n·66 + 30 - 660`); *tap* en `x<60, y<60` → salir
  (cierra todo el kit); *tap* en fila → selecciona/deselecciona; Restaurar → `flexFsRestore` (vuelve a su carpeta
  original, recreándola si hace falta; si existe un homónimo → `"<stem> (k)<ext>"`, k=2..99); Borrar → confirmación →
  `flexFsDelete` (definitivo); Vaciar → confirmación → `flexFsEmptyTrash`.
* **Codificación de la papelera** (`FS.cpp:456-523`): la ruta original viaja en el nombre cambiando `'/'` por `'@'`
  (`/Paint/Dibujo 1.fxp` → `/Papelera/@Paint@Dibujo 1.fxp`); colisión → sufijo `"(k)"` (k = 2..99) **pegado al final del
  nombre codificado** (detrás de la extensión). Restaurar decodifica y exige que empiece por `'/'`. La papelera no se
  tira a sí misma. Las carpetas se mueven enteras.

### 5.5 Pantalla "sin almacenamiento" (`fkNoFsScreen`, `:303`)
Fondo `TH_PAGE`, chevron, título de la app tamaño 3 en y=40; **"Sin almacenamiento"** tamaño 3 `TH_ERR` en y=300;
`flexFsError()` tamaño 1 `TH_TXT2` en y=344 (p. ej. `"sin particion de datos (elige un Partition Scheme con SPIFFS)"`);
**"Arduino IDE > Herramientas > Partition Scheme"** tamaño 1 `TH_MUTE` en y=380. Salida: *tap* en `x<60, y<60`.
→ En IDF cambiar el consejo (no hay Arduino IDE; la tabla de particiones es fija, ver `docs/PARTICIONES.md`).

### 5.6 Texto en caja (`fkTextBox`, `:65`)
Vista previa de notas: corta **por caracteres** (no por palabras) a lo ancho `w`, respeta `\n`, interlineado
`uiLineH(size)+4`, para cuando no cabe la siguiente línea; un carácter más ancho que la caja se descarta.

### 5.7 Migración del kit
Componentes LVGL reutilizables: `flex_ctx_menu` (lista en `lv_layer_top` con iconos a la derecha), `flex_name_dialog`
(`lv_textarea` de una línea + `lv_keyboard` del sistema, filtro `'/'`), `flex_confirm` (`lv_msgbox` con "Cancelar"/
"Borrar" en rojo y cierre al tocar fuera), `flex_trash_view`. Las operaciones de disco deben ir a la tarea de
almacenamiento (`flex_fs_*_async`) y la lista se recarga en el callback. La codificación `'/'→'@'` es lógica pura
(pruebas: ruta con `'@'` original — hoy no se escapa y se decodificaría mal, ver §18).

---

## 6. Notas

### 6.1 Propósito y entrada
App `IC_NOTAS` (`APP_CUSTOM_HEADER | APP_OWN_TOUCH`). Cada nota es un fichero **real** `.txt` UTF-8 en `/Notas`; el título
es el **nombre del fichero** sin extensión y la vista previa son sus primeros bytes. Dos vistas: `noteView = 0` lista,
`1` editor. Sin almacenamiento → pantalla §5.5 con título "Notas".

### 6.2 Lista — layout (`noteRenderList`, `AppNotes.h:121-170`)
* Fondo `TH_PAGE`; cabecera `uiHdrDraw("Notas:", 5, TH_TXT, TH_NAV, menú = sí)` — **el título lleva dos puntos**
  (literal del código).
* Viewport con scroll desde `y = 60` (`UIHDR_ZONE + 4`) hasta 799.
* Tarjetas en 2 columnas: `w = (480 - 3·16)/2 = 216`; `h = clamp((800-96-90)/2 - 44, 120, 300) = 263`;
  `x = 16 / 248`; `y = 96 + fila·(263 + 44) - scroll`. Título (nombre sin extensión) tamaño 2 `TH_TXT` **centrado 34 px
  por encima** de la tarjeta. Tarjeta radio 14: vidrio cacheado `drawGlassCardFlat(TH_GLASS sobre TH_PAGE)` o `TH_SURF`.
  Contenido: vista previa real (96 bytes) con `fkTextBox` tamaño 2 `TH_TXT` en `(x+10, y+10, w-20, h-20)`; vacía →
  **"(vacía)"** tamaño 2 `TH_MUTE`.
* Marcada en modo selección: doble contorno `TH_PRIM` (radios 14/13) + círculo `TH_PRIM` r=11 en `(x+w-20, y+20)` con
  check `TH_ONACC` (trazo 2.4).
* Lista vacía: **"No hay notas todavía"** tamaño 3 `TH_TXT2` en y=320 y **"Pulsa el boton de abajo para crear una"**
  (sic, sin tilde) tamaño 1 `TH_MUTE` en y=364.
* **Botón flotante** (FAB) `noteDrawFab`: círculo r=56 en `(396, 650)` con borde `TH_BORDER` 5 px y relleno `TH_SURF`;
  glifo de hoja `TH_TXT` 34×44 con tres renglones `TH_SURF` y un lápiz (trazos 5 y 4 px).
* **Barra de selección** (en lugar del FAB): `(12, 672, 456, 60)` radio 16, vidrio `TH_GLASS2` o `TH_SURF2`;
  **"Selección"** tamaño 2 en (28, 692); **"Papelera"** tamaño 2 `TH_WARN` alineado a la derecha en x=340;
  **"Salir"** `TH_TXT2` alineado a la derecha en x=452.
* El menú contextual (§5.1) se dibuja encima si está abierto.

### 6.3 Lista — interacción (`noteListTick`, `:248-351`)
1. Capas modales del kit primero (papelera, confirmación, nombre, menú).
2. **Scroll**: ancla en `T.pressed`; con `T.down` y contenido desbordado, arrastre si `|dy| > 8`; scroll máx.
   `96 + filas·307 + 40 - 740`. El arrastre anula la pulsación larga. Al soltar → sesión sucia.
3. **Pulsación larga** (> 550 ms, ±14 px) sobre una tarjeta → `noteSelIdx = i`, menú contextual en `(T.x, T.y-40)`.
4. *Tap*: tres puntos (zona 56×56 arriba a la derecha) → menú sin selección en `(440, 56)`; chevron → `appClose()`.
5. En modo selección: *tap* en la barra (`672..732`): `x > 380` → **Salir** (limpia la selección); `x > 250` →
   **Papelera** (mueve **todas** las marcadas a `/Papelera`, sin confirmación) y sale del modo.
6. Fuera del modo: *tap* dentro del círculo del FAB → **nueva nota**.
7. *Tap* en tarjeta: en modo selección conmuta su marca; si no, **abre el editor**.

Acciones del menú (`noteMenuAction`, `:222`): Seleccionar → modo selección con esa nota marcada; Eliminar →
confirmación **"¿Borrar definitivamente?"** + título → `flexFsDelete`; Renombrar → diálogo **"Renombrar nota"** con el
título → `flexFsRename`; Papelera → `flexFsTrash` (si se abrió desde los tres puntos, sin selección, abre la Papelera).

**Nueva nota** (`noteNew`, `:208`): `flexFsNewName("/Notas", "Sin título", ".txt")` → primer `"Sin título N.txt"` libre
(N = 1..999 mirando el disco); crea el fichero vacío; si falla (disco lleno) no aparece nada; si lo encuentra en la
lista recargada, abre el editor.

### 6.4 Editor (`FlexOS_Ultra_Keyboard.h:700-1564`, entrada `noteOpen`, `AppNotes.h:179`)
* Al abrir: lee el fichero entero al **búfer de trabajo** (`noteBuffer`: 4096 B en PSRAM; 512 B estáticos si no hay
  PSRAM) → **máximo 4095 bytes por nota** (ver riesgo §18); cursor al final; título de barra = nombre del fichero.
* **Layout** (`noteRenderAll`, `:1186`): fondo `TH_PAGE`; chevron `TH_NAV` en (18..30, 10..26); nombre del fichero
  tamaño 3 centrado en y=14; "hoja" `TH_SURF` en `(8, 48, 464, noteTxtBot()-48)` donde `noteTxtBot() = kbPanelTop()-8`;
  texto tamaño 2 `TH_TXT` desde `(18, 60)`, interlineado 26, ajuste por carácter a `x ≤ 462`; teclado del sistema con
  barra superior y chips (02/teclado) que se apoya **sobre** la barra de navegación (`kbBotReserve = NAV_H`).
* Cursor 2×22 `TH_PRIM`; selección `TH_SEL`; manijas = línea vertical 24 px + círculo r=7 `TH_PRIM` en cada extremo.
* Menú flotante de selección: 4 botones 92×28 separados 4 px, centrados, en `y = manijaA.y - 44` (≥ 50), sobre
  `uiSurface(UIS_ELEVATED)`: **"Cortar"**, **"Copiar"**, **"Pegar"**, **"Todo"**.
* Chip **"Copiado"** 1200 ms (fundido en los últimos 400 ms) en `noteTxtBot()-46`.
* Corrector (si `gKbSpell`): palabras de ≥ 3 letras que no están en el diccionario local se subrayan con puntos `TH_ERR`
  (salvo la palabra que se está escribiendo).
* **Interacción** (`noteEditorTick`, `:1413-1564`):
  * Animación de apertura del teclado 300 ms (no lee entrada mientras dura).
  * Escritura rápida multitáctil (tecla al TOCAR), destello de tecla, acentos por pulsación larga sobre vocal
    (`gKbLpMs`, 500 ms por defecto): á à â ã / é è ê / í ì î / ó ò ô õ / ú ù û ü.
  * *Tap* en el texto → coloca el cursor; pulsación larga 500 ms (±12 px) → selecciona palabra y abre menú; arrastrar
    manijas extiende la selección; arrastre vertical > 12 px (si hay contenido oculto) → scroll del texto.
  * Barra superior del teclado: emoji, idioma, portapapeles (panel de 12 ranuras), ajustes del teclado, "más"
    (seleccionar todo / insertar `"<fecha corta> <hora>"`).
  * Volver: *tap* en `x<60, y<44` (`noteTick`) o en `x<52, y<44` (`sysBack`) → **guarda** y vuelve a la lista.
* **Autoguardado**: si cambió la longitud del texto → `noteDirtyMs = millis()`; guardado real (`flexFsWriteText`,
  reescritura completa) **2000 ms después de la última modificación**, al volver a la lista, al suspender y al cerrar.
  `noteDirty()` (cambios sin guardar) = editor abierto y `noteDirtyMs != 0`.

### 6.5 Ciclo de vida y sesión (`:354-533`)
* `noteEnter`: si la sesión decía "editor" y el fichero existe → reabre el editor con texto, cursor, selección, scroll y
  **estado del teclado** (capa, idioma, mayúsculas, panel portapapeles, menú "más", menú de selección; `noteKbFlags`
  bits 0..4). Si no, lista.
* `noteBackLayer` (botón atrás): cierra en orden popup de acentos → menú "más" → portapapeles → selección/menú → menú
  contextual. `noteBackScreen`: editor → lista (guardando).
* `noteSuspend`: guarda, captura el teclado, cierra los diálogos del kit, corta arrastres. `noteCloseApp`: además libera
  el búfer de PSRAM y olvida la sesión.
* Persistencia: `notas.bin` v3 (§0.4) + el propio `.txt`.

### 6.6 Casos límite
* `NOTE_MAX_LIST = 16`: solo se listan 16 entradas de `/Notas` (y el recorte ocurre **antes** de ordenar, ver §18).
* La vista previa lee 96 bytes por nota en cada recarga (16 lecturas de flash por operación).
* Orden: carpetas primero y luego nombre sin mayúsculas (`strcasecmp`): `"Sin título 10"` va antes que `"Sin título 2"`.

### 6.7 Migración
`lv_obj` con `LV_FLEX_FLOW_ROW_WRAP` de tarjetas (2 columnas) con scroll nativo, FAB flotante, barra de selección; el
editor con `lv_textarea` multilínea + `lv_keyboard` del sistema (el texto sigue en PSRAM; guardar en la tarea de
almacenamiento con escritura atómica). Mantener: guardado a los 2 s, guardado al salir/suspender, restauración de
cursor/scroll/teclado. **Eliminar** el límite silencioso de 4 KB o, como mínimo, abrir en solo lectura las notas que lo
superen.

---

## 7. Paint

### 7.1 Propósito y entrada
App `IC_PAINT` (`APP_CUSTOM_HEADER | APP_OWN_TOUCH`). Galería de dibujos (`.fxp` en `/Paint`) + lienzo. Cada dibujo se
guarda **trazo a trazo al levantar el dedo** (no hay botón Guardar). `paintView`: 0 galería, 1 lienzo. Sin
almacenamiento → §5.5 con título "Paint". Sin memoria para el búfer del trazo → pantalla `#0E1018` con **"Sin memoria
para el lienzo"** tamaño 2 `#F08C8C` en y=320.

### 7.2 Constantes (`AppPaint.h:53-65`)
`P_TOP = 96`; `P_BOT = 800 - navBarH() - 66` (= 670 con barra de botones, 734 con gestos); lienzo `x = 8, w = 464`,
`h = P_BOT - 96` (574 / 638). Paleta (6): **`#1E1E28`, `#E63C3C`, `#F09628`, `#F0D232`, `#50B478`, `#3C78EB`**; grosores
(radio del pincel) **3, 6, 12** px; 512 puntos por trazo de trabajo; 16 dibujos listados; 2 columnas; tarjetas desde
y=118.

### 7.3 Galería (`paintRenderGallery`, `:182-246`)
* Fondo `TH_PAGE`; `uiHdrDraw("Paint", 5, TH_TXT, TH_NAV, menú)`; viewport desde y=60.
* Tarjetas: `w = 216`, `h = min(216·PAINT_CH/PAINT_CW, 340)` (= 267 con barra de botones); `x = 16/248`;
  `y = 118 + fila·(h + 46) - scroll`; nombre centrado 34 px por encima, tamaño 2. Tarjeta **blanca `#FFFFFF`** radio 12
  con **miniatura real**: se reproducen los trazos del fichero a escala `min((w-8)/docW, (h-8)/docH)`, centrada, con
  recorte = intersección de la tarjeta (−4 px) y el viewport.
* Marcada: doble contorno `TH_PRIM` (radios 12/11) + círculo r=10 en `(x+w-18, y+18)` con check.
* Vacía: **"No hay dibujos todavía"** tamaño 3 `TH_TXT2` (y=320) y **"Pulsa + para crear uno"** tamaño 1 `TH_MUTE`
  (y=364).
* FAB "+" r=52 en `(404, 654)` (borde `TH_BORDER`, relleno `TH_SURF`, cruz `TH_TXT` 44×10 y 10×44 radio 4) con la etiqueta
  **"Nuevo dibujo"** tamaño 2 alineada a la derecha en `x = 346, y = 620`.
* Barra de selección idéntica a Notas (§6.2).

### 7.4 Galería — interacción (`paintGalleryTick`, `:362-452`)
Idéntica a Notas (§6.3): capas del kit, scroll (umbral 8 px, máx. `118 + filas·(h+46) + 40 - 740`), pulsación larga
550 ms ±14 px → menú; tres puntos; chevron → `appClose()`; barra de selección (Salir/Papelera sin confirmación); FAB →
**nuevo dibujo**; *tap* en tarjeta → abrir o marcar. Menú: Renombrar → **"Renombrar dibujo"**; Eliminar →
**"¿Borrar definitivamente?"**.

**Nuevo dibujo** (`paintNew`, `:313`): `flexFsNewName("/Paint", "Dibujo", ".fxp")` → `"Dibujo N.fxp"`; crea cabecera con
`w = PAINT_CW (464)`, `h = PAINT_CH`; pide a la biblioteca de medios un re-escaneo (`mlRequestScan()`, para que la
Galería lo vea); abre el lienzo.

### 7.5 Lienzo (`paintRenderCanvas`, `:272-298`; `paintTools`, `:249-270`)
* Cabecera sobre `TH_PAGE` (0..96): chevron `TH_NAV` (18..30, 10..26); nombre tamaño 3 centrado en y=30; subtítulo
  `"%u trazos guardados"` tamaño 1 `TH_TXT2` en y=66 (contador de la cabecera del fichero).
* Zona `(8, 96, 464, PAINT_CH)` en gris `#DADEE6`; el documento se encaja con **letterbox** (`paintDocLayout`, `:96`):
  escala `min(PAINT_CW/docW, PAINT_CH/docH)`, centrado, hoja `#FAFAFC`. Se respeta el tamaño guardado del documento
  (dibujos creados antes de reservar la barra de navegación conservan sus 64 px).
* El lienzo se **reconstruye desde el fichero** (no hay bitmap en RAM).
* **Barra de herramientas** `y = P_BOT .. 800-navBarH()` fondo `thCard()`; fila en `y = P_BOT + 8`, alto 34:
  * 6 colores: círculos r=15 centrados en `x = 27, 67, 107, 147, 187, 227`; el seleccionado con doble aro `TH_TXT` r=17/16.
  * Grosor: botón `(258, y, 44, 34)` radio 8 `TH_SURF2` con un punto `TH_TXT` del **radio real** (3/6/12).
  * **"Deshacer"**: `(310, y, 76, 34)` radio 8 `TH_SURF2`, texto tamaño 1 `TH_TXT`.
  * **"Limpiar"**: `(392, y, 66, 34)` radio 8 `TH_DANGER`, texto tamaño 1 `TH_ONACC`.

### 7.6 Lienzo — interacción (`paintCanvasTick`, `:454-515`)
* **Trazo**: mientras `T.down` dentro del documento: primer punto → disco AA del radio actual; siguientes → segmento AA
  desde el anterior. Se descartan puntos a menos de 2 px (`dx²+dy² < 4`). Cada punto se guarda en **coordenadas de
  documento** (`(v - org)/escala`, redondeado y acotado). Al llegar a 512 puntos se cierra el trazo y se abre otro que
  empieza en el último punto (sin perder continuidad). Solo se vuelca la franja de filas tocada.
* **Al levantar el dedo** → `flexPaintAppend(path, color, radio_doc, puntos)` (radio en unidades de documento =
  `radio/escala`, 1..255).
* *Tap* en `x<48, y<48` → cierra el trazo, vuelve a la galería.
* *Tap* en la fila de herramientas (`y` en `[P_BOT+4, P_BOT+46]`): color (por franja de 34 px) → selecciona; grosor →
  cicla 3→6→12→3; **Deshacer** → `flexPaintUndo` (reescribe el fichero sin el último trazo) y repinta; **Limpiar**
  (`x ≥ 392`, sin límite derecho) → `flexPaintClear` (**sin confirmación**) y repinta.
* No hay goma, ni zoom, ni selector de color libre.

### 7.7 Formato `.fxp` (lógica pura, `FlexOS_FS.h:287-335`, `FlexOS_FS.cpp:686-839`)

```
Cabecera FlexPaintHdr (12 B, little-endian):
  u8  magic[4] = 'F','X','P','1'
  u16 w, h          tamaño del lienzo del documento
  u16 strokes       número de trazos
  u16 reserved = 0
Por cada trazo FlexPaintStroke (6 B) + pts × (i16 x, i16 y):
  u16 color (RGB565)   u8 size (radio, unidades de documento)   u8 flags = 0   u16 pts
```
* `flexPaintCreate`: escribe la cabecera con 0 trazos. `flexPaintAppend`: abre en `r+`, añade el trazo al final y
  reescribe el contador. `flexPaintUndo`: copia a `<ruta>.tmp` todos los trazos menos el último y renombra (no hay
  `truncate`). `flexPaintClear`: recrea la cabecera con el mismo w/h. `flexPaintReplay(sc, ox, oy, cb)`: emite cada
  segmento escalado; el radio también se escala (≥ 1); un trazo de un punto emite un segmento degenerado (disco).
* Tamaño típico 3-8 KB por dibujo frente a 542 KB de un bitmap.

### 7.8 Sesión y ciclo de vida
`paint.bin` v1 (vista, grosor, color, scroll, ruta). `paintEnter` restaura el lienzo si la sesión lo decía y el fichero
existe (color por defecto `P_PAL[0]` si no). `paintBackScreen`: lienzo → galería. `paintSuspend`: cierra el trazo
(lo escribe) y los diálogos. `paintDirty()` = hay un trazo en curso sin volcar. `paintCloseApp` libera el búfer.

### 7.9 Migración
* Formato `.fxp` y `flexPaintReplay` → lógica pura con pruebas (crear, añadir, deshacer, limpiar, reproducir a escala,
  fichero truncado a mitad de trazo).
* Lienzo: `lv_canvas` del tamaño del documento escalado (464×574 RGB565 = 532 KB en PSRAM) o, mejor, dibujo incremental
  sobre un `lv_canvas` + capa de trazos con `lv_draw_line` (extremos redondeados). Miniaturas: renderizar cada `.fxp` a un
  `lv_image` pequeño **una vez** y cachearlo (hoy se reproduce cada fichero en cada repintado de la galería).
* Escrituras al levantar el dedo → tarea de almacenamiento (asíncrono); el trazo en curso se mantiene en RAM hasta su
  confirmación.

---

## 8. Archivos (explorador, estado `ST_FILES`)

### 8.1 Propósito y entrada
Explorador de la partición LittleFS de usuario y, en una segunda pestaña, de **Flex Cloud**. **No es una app**: es una
pantalla a pantalla completa por encima de Almacenamiento, a la que se entra con la fila **"Todos los archivos … Ver..."**
(`filesEnter()` → raíz `/`) o con `filesEnterAt(dir)`. Siempre entra en la pestaña **"Este dispositivo"**.
Salir (chevron o atrás) **vuelve a Almacenamiento repintándolo** (los tamaños pudieron cambiar): `gState = ST_APP`, marco
de app completo, `almEnter()` (`AppFiles.h:236-249`).

### 8.2 Layout (`filesRender`, `:150-221`)
* Fondo `TH_PAGE`; cabecera `uiHdrDraw("Archivos:", 5, …, menú)` — **con dos puntos** (literal).
* **Selector segmentado** `filesDrawSeg` en `(16, 74, 448, 36)`, radio 18, fondo `TH_SURF2`; mitad activa con relleno
  `TH_PRIM` (inset 3 px, radio 15); textos tamaño 1 centrados: **"Este dispositivo"** | **"Flex Cloud"** (activo
  `TH_ONACC`, inactivo `TH_TXT2`).
* Ruta actual (`filesDir`) tamaño 2 `TH_TXT2` en `(16, 124)` recortada a x=420.
* Viewport con scroll desde y=150. Filas desde `FILES_TOP = 168`, paso 70 (tarjeta `(12, y, 456, 62)` radio 12,
  vidrio `TH_GLASS` o `TH_SURF`; marcada en modo selección `TH_SEL`).
  * Fila 0 si no estamos en la raíz: icono de carpeta 38 px en (28, y+12), **".."** tamaño 3 en (84, y+18) y
    **"subir"** tamaño 1 `TH_TXT2` alineado a la derecha en x=452.
  * Carpeta: icono de carpeta (`almFolderIcon`: pestaña `#F0AF3C`, cuerpo `#FACD5A`); subtítulo derecho tamaño 2
    `TH_TXT2` en y+40: `"%u elementos"` (conteo real abriendo la carpeta) o **"Carpeta"** si no se sabe (`0xFFFF`).
  * Fichero: hoja `#FAFAFC` 30×42 radio 5 con banda superior `#C8CCD6`; subtítulo = tamaño (`"53 KB"`, `"1.2 MB"`,
    `"812 B"`).
  * Nombre tamaño 3 `TH_TXT` en (84, y+10) recortado a x=330.
* Carpeta vacía: **"Carpeta vacía"** tamaño 3 `TH_MUTE` en y=320.
* Barra de selección igual que Notas (§6.2) y menú contextual encima.
* **Pestaña Flex Cloud**: la zona `(0, 114, 480, 626)` la pinta el kit de nube (`ckRender`, `FlexOS_Ultra_CloudKit.h`,
  otra área) con el anfitrión `{ título "Archivos", modo CKM_BROWSE, caja, repintar = filesRender, abrir = filesCkOpen }`;
  aquí solo cabecera y selector.

### 8.3 Interacción (`filesTick`, `:344-455`)
* Capas del kit (papelera, confirmación, nombre, menú) como en Notas.
* Scroll: umbral 8 px, máx. `168 + filas·70 + 30 - 720`. Pulsación larga (550 ms, ±14 px) sobre una fila de elemento →
  menú contextual; sobre un **fichero** el menú lleva la 5.ª fila **"Subir a Flex Cloud"**.
* *Tap*: tres puntos → menú sin selección; chevron → salir a Almacenamiento; selector "Flex Cloud" → pestaña de nube
  (sale del modo selección).
* Modo selección: barra Salir / Papelera (mueve todo lo marcado a la papelera sin confirmar).
* *Tap* en ".." → `filesGoUp()` (corta en el último `/`; nunca por encima de `/`).
* *Tap* en carpeta → entra (scroll 0). En modo selección conmuta la marca.
* *Tap* en fichero → **abrir** (`filesOpenEntry`, `:272`):
  * Foto, dibujo, vídeo o audio compatible (`flexMediaClassify`) → `gState = ST_APP`, `gMediaReturnApp = IC_ALMACEN`
    (al salir del visor se vuelve a Almacenamiento) y `mediaOpenInPlayer(ruta)`.
  * Formato reconocido pero no reproducible → notificación del módulo de medios con el motivo concreto
    (`flexMediaUnsupportedReason`) o **"Formato no compatible"**; no se intenta abrir.
  * Otro tipo → abre el menú contextual en `(180, 200)`.
* Acciones del menú: Seleccionar, Eliminar (**"¿Borrar definitivamente?"** + nombre completo con extensión), Renombrar
  (**"Renombrar"**, conserva extensión), Papelera, Subir a Flex Cloud:
  * Si la cuenta no permite subir (`ckCloudBlock()` devuelve motivo) → notificación **"Flex Cloud"** + motivo.
  * Si se encola (`flexCloudUpload(ruta, nombre, "root", 0, 0)`) → notificación título = nombre del fichero, texto
    **"Subiendo a Flex Cloud (se conserva aquí)"**.
  * Si no → `ckNotifyFail(nombre, "No se pudo poner en cola")`.
* Pestaña nube: *tap* en chevron → `ckBack()` o, si no queda nada que retroceder, salir; tres puntos → menú del kit de
  nube; selector "Este dispositivo" → vuelve a la lista local. El resto lo gestiona `ckTick()`.

### 8.4 Reglas y casos límite
* **Biblioteca oculta**: `/System/Media` (catálogo, miniaturas y carpeta protegida de la Galería, `FML_DIR_ROOT`) no se
  lista en `/System` y no se puede entrar (si `filesDir` cae dentro, se redirige a `/System`).
* `FILES_MAX = 24` entradas por carpeta (ver §18: el resto no se ve).
* Al listar una carpeta se abre y recorre **cada subcarpeta** para contar elementos y sumar tamaños (coste O(árbol) en
  el hilo de UI).
* Si `filesScroll > filas·70` se resetea a 0 tras recargar.

### 8.5 Migración
Pantalla LVGL propia (no app) con `lv_list`/filas personalizadas, `lv_buttonmatrix` de dos segmentos, ruta en una
etiqueta con `LV_LABEL_LONG_DOT`. Listado y recuentos en la tarea de almacenamiento (asíncrono, con "cargando"). La
apertura por tipo usa el clasificador de medios portable (`flex_portable/FlexOS_Media`).

---

## 9. Almacenamiento

### 9.1 Propósito y entrada
App `IC_ALMACEN` (`APP_FLEX`). Tres pantallas internas (`almScreen`): `ALM_SCR_MAIN` (resumen), `ALM_SCR_DETAIL`
(detalles de memoria y sistema), `ALM_SCR_PHONE` (Flex Cloud en tu teléfono). Todo lo que muestra sale de medidas
reales (LittleFS, `heap_caps_*`, `esp_reset_reason`). `almOpenPhone()` abre la app directamente en la pantalla del
teléfono (lo usa Flex Cloud cuando no hay teléfono o fue rechazado).

### 9.2 Pantalla principal (`almRenderMain`, `AppStorage.h:169-300`; a 480×640, pad 20, gap 15)
* Fondo `WIN_BG`; título **"Almacenamiento"** con `uiTitle` (tamaño `uiFontH(640/12)` = 5).
* Sin LittleFS: **"Sin almacenamiento"** tamaño 3 `TH_ERR`, `flexFsError()` tamaño 1 `TH_TXT2`, **"Elige un Partition
  Scheme con SPIFFS"** tamaño 1 `TH_MUTE`.
* **Medidores** `simpBar(y, etiqueta, valor, pct, color)` (`:38`): x = 40, ancho 400; etiqueta a la izquierda y valor a la
  derecha (tamaño ≤ 2, cada uno ≤ 200 px); barra alto `clamp(bh/22, 9, 20)` = 20 radio 10, pista `TH_TRACK`, relleno
  proporcional del color.
  1. **"Memoria interna"** → `"<usado> / <total>  (<pct>%)"` (partición de datos LittleFS), color `#5AA0F0`.
  2. **"PSRAM"** → `"%u / %u MB  (%d%%)"` color `#5AB478`; sin PSRAM → **"No disponible en esta placa"**, barra a 0
     `#787C8C`.
* **Tarjeta "Detalles de memoria y sistema"** (si cabe): `(20, y, 440, 50)` radio 14 `uiSurface(UIS_ELEVATED)`; título
  tamaño 2 en (40, y+4); punto de salud r=5 en (46, y+32) y texto tamaño 1 `TH_TXT2` en (58, y+26): **"Óptimo"** (`TH_OK`),
  **"Atención"** (`TH_WARN`), **"Crítico"** (`TH_ERR`) según `flexMemHealth`. Píldora **"Optimizar"** `(330, y+9, 118,
  32)` radio 16 `TH_PRIM`, texto tamaño 1 `TH_ONACC`.
* **Tarjeta "Flex Cloud en tu teléfono"** (`almPhoneCard`, `:151`): `(20, y, 440, 52)` radio 14 elevada; nube del acento r=13
  en (46, y+27); título tamaño 2 en (70, y+5); punto de estado r=4 en (74, y+37) y línea tamaño 1 `TH_TXT2` en (84, y+31)
  recortada; chevron a la derecha. Estado (`almPhoneState`, `:133`):
  `"<nombre> · conectado"` (`TH_OK`), `"<nombre> · conectando..."` (`TH_PRIM`, nunca respondió en este arranque),
  `"<nombre> · desconectado"` (`TH_WARN`), `"<nombre> · en pausa"` (`TH_MUTE`), **"Hay que volver a emparejar"** (`TH_ERR`),
  **"Sin teléfono"** (`TH_MUTE`); nombre por defecto **"Teléfono"**. Con cuota válida se añade `"  ·  <línea de cuota>"`;
  sin teléfono `" · actívalo desde la web"`.
* **Categorías** (fila `uiLineH(2)+12` = 30): punto r=8 + nombre tamaño 2 + tamaño real a la derecha:
  **"Documentos"** (`/Documentos` + `/Notas`, `#F55555`), **"Sistema"** (`/System`, `#FAD75F`), **"Aplicaciones"**
  (`/Paint`, `#5FE16E`), **"Papelera"** (`/Papelera`, `#3CCDF0`).
* **"Archivos grandes"** tamaño 2: los 3 mayores ficheros del árbol entero (excluyendo `/System/Media/…`): icono carpeta
  34 px, nombre tamaño 2, `"<ruta>  ·  <tamaño>"` tamaño 1 `TH_TXT2`; vacío → **"No hay archivos guardados"** `TH_MUTE`.
* Fila **"Todos los archivos"** / **"Ver..."** (`TH_ACCS`) `(20, y, 440, 44)` radio 12, vidrio `TH_GLASS` o `TH_SURF`.
* Cada sección solo se pinta si cabe (sin scroll en esta pantalla).

### 9.3 Interacción principal (`almTick`, `:798-809`)
*Tap* en la tarjeta del teléfono → pantalla Teléfono; en la tarjeta de detalles → si cae en la píldora **Optimizar** →
`optStart()` (optimizador del sistema, `System.h:309-341`, otra área), si no → pantalla Detalle; en "Ver..." →
`filesEnter()` (§8).

### 9.4 Detalle de memoria y sistema (`:302-638`)
* Viewport `WIN_TOP..WIN_BOT-1` (96..735) con scroll propio; contenido en **5 secciones de alto fijo** `96, 240, 168, 236,
  200` (total 940 → scroll máx. 300). Cada sección lleva una **firma** de sus valores; una vez por segundo (y sin dedo
  apoyado) se recalculan y **solo se repinta la sección que cambió** (si está visible). Al entrar se activa la medida de
  flash (`gMemWantFlash = true`, cara) y el tamaño de firmware se lee una vez por arranque.
* Formato de fila (`almDetRow`): etiqueta tamaño 1 `TH_TXT2` en x=40 (recortada a 260), valor tamaño 2 alineado a la
  derecha en x=440 (y−3), paso 24; notas tamaño 1 `TH_MUTE` paso 18; título de sección tamaño 2 `TH_TXT` + 26.
* Sección 0: **"Detalles de memoria y sistema"** + botón **"Optimizar Flex OS"** `(36, y, 408, 44)` radio 22
  `uiSurface(UIS_ELEVATED)` con borde `TH_BORDER`, texto tamaño 2 `TH_ACCS`.
* Sección 1 **"PSRAM (memoria de trabajo)"**: "Total detectada", "En uso" (`"<x> (<pct>%)"`), "Libre", "Pico de uso
  desde el arranque", "Bloque libre más grande", "Fragmentación" (`"Baja|Media|Alta (<pct>%)"`, alta en `TH_WARN`;
  pct = `100 - mayorBloque·100/libre`, Media ≥ 35, Alta ≥ 65), "Estado" (Óptimo/Atención/Crítico); nota
  `"Mayor consumo: <app> <mem>  ·  <app2> <mem2>"` (las dos apps abiertas con más PSRAM **medida**) o
  **"Mayor consumo: sin medida todavía."**; nota **"La usan las apps abiertas, las imágenes y el doble búfer de
  pantalla."**. Sin PSRAM: **"No disponible en esta placa."**
* Sección 2 **"Memoria interna del chip (SRAM)"**: "Total utilizable", "En uso", "Libre" (`TH_WARN` si < 64 KB), "Mínimo
  desde el arranque"; notas **"La usan el sistema, el táctil, el Wi-Fi y las tareas internas."** y **"No conserva datos:
  al reiniciar se vacía entera."**; sin medida **"No disponible."**
* Sección 3 **"Almacenamiento interno (flash)"**: "Capacidad de datos", "Usado" (`TH_WARN` ≥ 80 %, `TH_ERR` ≥ 90 %),
  "Libre", "Tamaño del firmware" (o **"No disponible"**), "Recursos del sistema", "Datos de apps", "Caché temporal"
  (`/System/Cache`), "Archivos de usuario"; sin montar: **"No disponible: la partición de datos no está montada."**
* Sección 4 **"Estado del sistema"**: "Tiempo encendido" (`"%lud %luh %lum"`/`"%luh %lum"`/`"%lum"`), "Último arranque"
  (**"Encendido normal"**, **"Reinicio por software"**, **"Despertar de suspensión"**, **"Fallo del sistema (crash)"**,
  **"Watchdog de tarea"**, **"Watchdog de interrupción"**, **"Watchdog del chip"**, **"Caída de tensión"**, **"Reinicio
  externo"**, **"No disponible"**), "Versión" (`"Flex OS <versión local OTA>"`), "Wi-Fi" (**"Conectado"** `TH_OK` /
  **"Desconectado"**), "Ritmo del sistema" (`"%u vueltas/s"`), "Apps activas / pausadas / guardadas" (`"a / p / g"`);
  nota **"No hay medidor global de FPS: el ritmo del bucle es la medida real."**
* Formato de tamaños `flexMemFmt`: `"x.y GB"`, `"x.y MB"`, `"N KB"`, `"N B"` (base 1024).
* Interacción: el arrastre solo cuenta si **empezó dentro** del viewport; umbral 10 px; *tap* (sin arrastre) en el botón
  "Optimizar Flex OS" → `optStart()`. Atrás → vuelve a la principal (`almBackScreen`) y apaga la medida de flash.

### 9.5 Flex Cloud en tu teléfono (`:640-790`)
* Título **"Flex Cloud en tu teléfono"** (`uiTitle`, tamaño `uiFontH(640/14)`).
* Tarjeta de estado `(20, y, 440, 92)` radio 16 elevada; punto r=6 en (42, y+22) con el color del estado.
  * Sin teléfono: **"Ningún teléfono emparejado"** tamaño 2 y **"Flex Cloud usará Internet (Flex Account) si la tienes."**
  * Con teléfono: nombre tamaño 2; `"<modelo>  ·  <ip>:<puerto>"` tamaño 1; motivo tamaño 1:
    **"El teléfono ya no reconoce este Flex OS: vuelve a emparejarlo."** (`TH_ERR`), **"En pausa: Flex Cloud usa Internet
    hasta que lo reactives."**, **"Conectado: Flex Cloud guarda en este teléfono."**, **"Conectando con el teléfono..."**,
    **"Desconectado: comprueba que está en la misma Wi-Fi."**
* Tarjeta de cuota (solo si el teléfono está listo, es el destino de Flex Cloud y hay cuota válida) `(20, y, 440, 76)`:
  línea de cuota tamaño 2; barra 8 px radio 4 (`TH_TRACK`; relleno `TH_DANGER` lleno / `TH_WARN` bajo / acento; mínimo
  8 px si > 0); pista tamaño 1 (`TH_WARN` si no está OK). Textos de `fclQuotaLine/Hint` (Flex Cloud, otra área).
* Sin teléfono, ayuda (ajuste por palabras, tamaño 1): **"Cómo activarlo: 1) En el teléfono, abre la web de Flex OS
  (Galería > Conectar con el móvil, escanea el QR). 2) Pulsa «Activar Flex Cloud en este teléfono». 3) Compara el código
  de 6 cifras y acepta aquí."** Siempre: **"Flex Cloud guarda tus archivos en el espacio que el teléfono le reserva
  (hasta 5 GB) y Flex OS solo ve ese espacio. Los archivos viajan sin cifrar por tu Wi-Fi: úsalo en una red de
  confianza."**
* Botones (ancho completo, alto 46, radio 16, `TH_SURF2`): **"Poner en pausa"** / **"Volver a conectar"** (si READY u
  OFF) y **"Olvidar este teléfono"** (texto `TH_DANGER`). Olvidar pide **doble toque en 4 s**: el primero arma el botón
  (fondo `TH_DANGER`, texto **"Pulsa otra vez para olvidarlo"** `TH_ONACC`).
* Acciones: pausa/reanudar → `flexStorageSetEnabled(on)` → notificación **"Flex Cloud"**: **"Teléfono reactivado"** /
  **"En pausa: se usa Internet"** o **"No se pudo guardar el cambio"**; olvidar → `flexStorageForget()` (borra la clave
  de la NVS) → **"Teléfono olvidado"**.
* Refresco: firma FNV-1a de estado/nombre/IP/puerto/cuota/armado una vez por segundo (sin dedo) → repinta si cambia.
  Mientras está visible marca `ckMarkVisible()` (Flex Cloud mantiene la cuota al día).
* **Emparejar no se hace aquí**: se inicia desde la web en el teléfono y se aprueba en el cuadro global (§10).

### 9.6 Ciclo de vida
`almSuspend` apaga la medida de flash; `almResume` vuelve a la pantalla interna activa (re-entrando); `almCloseApp`
resetea pantalla, scroll y armado; **sin sesión en disco**.

### 9.7 Migración
* Pantallas como `lv_obj` con `lv_bar` para medidores; el detalle con secciones en un contenedor con scroll y
  actualización por `lv_timer` 1 s que solo toca las etiquetas cuyo valor cambió (LVGL ya invalida solo lo necesario:
  la técnica de firmas sobra salvo para evitar trabajo de formateo).
* `flexMemHealth/FragPct/FlashPct/Fmt` ya están en `flex_portable/FlexOS_Mem` (pruebas en `tests/host/test_mem.cpp`).
* `flexFsCatSize`/`flexFsLargest`/`flexFsDirSize` recorren el árbol entero: ejecutarlos en la tarea de almacenamiento y
  publicar el resultado por evento.

---

## 10. Flex Storage: cuadro de aprobación de emparejamiento (`FlexOS_Ultra_StoragePair.h`)

### 10.1 Propósito y entrada
Overlay **global** (encima de cualquier pantalla) que aparece cuando un teléfono pide emparejarse como destino de Flex
Cloud (la web de Flex OS le dio una oferta de un solo uso). Muestra el **código de 6 cifras** (SAS) que el teléfono
enseña a la vez (sale del mismo ECDH P-256 en los dos lados); la persona compara y decide. El teléfono no gana ningún
acceso a Flex OS. La lógica de decisión vive en `FlexOS_StorageCore` (portable, probada en `tests/host/test_storagecore.cpp`).

### 10.2 Vigilancia (`spaWatch`, `:281-324`, cada 250 ms desde `loop()`, `.ino:757`)
* Lee el estado publicado `flexStorageInfo()` (no bloquea).
* **Avisos por flanco** del teléfono emparejado (el primero solo se anota): pasa a READY (y no venía de OFF) →
  notificación **"Flex Cloud"** `"Activado en <nombre>"` (o `"Activado en tu teléfono"`); pasa a REJECTED → **"Vuelve a
  emparejar el teléfono"**.
* Si hay emparejamiento esperando (`pairWaiting`) y el cuadro está oculto: si ya se decidió ese mismo (clave
  `sas|nombre|ip`) no hace nada; si no se puede mostrar → avisa **una vez** por la isla: **"Flex Storage"** +
  **"Desbloquea para aprobar el teléfono"** (pantalla segura) o **"Un teléfono quiere emparejarse"**; si se puede → arma
  el cuadro (`SPA_ARMED`), corta el episodio táctil en curso.
* Con el cuadro visible: si cambia la cuenta atrás o el emparejamiento → recomponer. Si el emparejamiento terminó por
  otro lado → cerrar.
* **No se puede mostrar** (`spaCanShow`, `:76`): pantalla segura/bloqueo/OOBE/alta de PIN, protección contra robo,
  restablecimiento pendiente o en curso, modo seguro, OTA dueño de la pantalla, optimizador, Modo PC, suspensión,
  transición de app, tarjeta del cronómetro o aviso de caída visibles, cortina abierta o animándose.

### 10.3 Layout
* **Vertical** (`spaDrawVertical`, `:133`): velo `TH_SCRIM` alfa `120·p` sobre la banda `y 100..564`; tarjeta
  `(24, y, 432, h)` con `h = 440·(0.74 + 0.26p)` centrada en `112..552`, `uiSurfaceA(r28, UIS_ELEVATED, alfa 255p)` y
  borde `TH_BORDER`. Contenido a partir de `p ≥ 0.6`:
  * Glifo de teléfono 44 px `TH_PRIM` (pantalla `TH_WIN`) en (240, y+46).
  * **"¿Emparejar este teléfono?"** tamaño ≤ 3 en y+80; nombre del teléfono (o **"Teléfono"**) tamaño ≤ 2 en y+118;
    `"<modelo>  ·  <ip>"` tamaño 1 `TH_TXT2` en y+146.
  * Caja del código `(80, y+168, 320, 76)` radio 18 `TH_SURF` + borde `TH_BORDER`; `"123 456"` tamaño 5 `TH_TXT`.
  * Texto (ajuste por palabras, tamaño 1 `TH_TXT2`) en y+258: **"Comprueba que el teléfono enseña el mismo código. Solo
    será el destino de Flex Cloud: no verá nada de Flex OS."**
  * Cuenta atrás `"Quedan %u:%02u para decidir"` tamaño 1 `TH_MUTE` en y+304.
  * Botones `x = 48, w = 384`: **"Emparejar"** `(y+h-118, alto 48, radio 24)` `TH_PRIM` texto `TH_ONACC`; **"Rechazar"**
    `(y+h-62, alto 46, radio 23)` `TH_SURF` texto `TH_TXT2`.
* **Horizontal** (`spaDrawLandscape`, `:170`): tarjeta `(80, y, 640, 340·(0.74+0.26p))` en coordenadas lógicas; dos
  columnas: izquierda título/nombre/subtítulo/caja de código `(lx, y+116, 278, 76)`; derecha explicación y cuenta atrás;
  botones en fila al pie: "Rechazar" a la izquierda, "Emparejar" a la derecha (ancho 278, alto 46).

### 10.4 Interacción y estados (`spaTick`, `:329-388`)
`SPA_HIDDEN → ARMED → IN (200 ms, ease-out cuadrática 1-(1-p)²) → SHOWN → OUT (200 ms) → HIDDEN`.
* ARMED: reserva PSRAM para la banda (solo si queda por encima del suelo crítico `FLEXMEM_CRIT_BYTES = 5 MB`); si no
  puede → abandona y notifica **"Un teléfono quiere emparejarse"**.
* SHOWN: *tap* en Emparejar → `flexStoragePairDecide(true)`; en Rechazar → `false`. **Fuera de los botones no cierra**
  (evita decidir con la palma). Se tragan swipes y pressed/released.
* Tras decidir: si ya había caducado → **"El emparejamiento ya había caducado"**; aprobado → **"Aprobado: termina en el
  teléfono"**; rechazado → **"Teléfono rechazado"** (todas con título **"Flex Storage"**).
* Abandono inmediato (sin restaurar): cambio de orientación, OTA, restablecimiento, optimizador, cortina, bloqueo o
  robo (el aviso de "espera" se rearma para volver a salir al desbloquear), banda no disponible, o pulsar la barra de
  navegación (se retira **sin decidir** y vuelve a salir donde se esté mientras el emparejamiento siga).
* Válvula: se cierra solo a los `FST_PAIR_TTL_MS + 10 s` = 130 s (el núcleo caduca a los 120 s).
* Mientras está visible `loop()` le cede la pantalla (`.ino:925-931`).

### 10.5 Migración
Modal LVGL en `lv_layer_top` con `lv_obj` de fondo semitransparente que absorbe toques (pero **sin** cerrar al tocar
fuera), animación de escala/opacidad 200 ms; contenido re-calculado por evento `STORAGE_PAIR_CHANGED` del servicio
Flex Storage (en vez de sondear cada 250 ms). `FstCore` ya es portable (`flex_portable/FlexOS_StorageCore`).
Persistencia del teléfono: NVS espacio **`flexstor`**, clave **`phone`** (blob ≤ 192 B con firma, versión y CRC).
