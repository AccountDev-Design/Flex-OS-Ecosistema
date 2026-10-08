# 02 · Sistema de diseño visual, Liquid Glass, tipografía, iconos, framework de apps y Modo PC (DeX)

> Especificación funcional para reconstruir en **ESP-IDF 5.5 + FreeRTOS + LVGL 9.6** (480×800 vertical, MIPI-DSI,
> táctil GT911) todo lo que en la versión Arduino `FlexOS_Ultra/` define el **aspecto** del sistema y el **marco**
> en el que viven las apps. El motor de dibujo propio de Arduino **no se reutiliza**: aquí se describe *qué se ve* y
> *cómo se comporta*, con las medidas exactas del código, para rehacerlo con objetos/estilos/eventos/animaciones LVGL.

**Fuentes leídas por completo:** `FlexOS_Ultra_Theme.h`, `FlexOS_Ultra_Gfx.h`, `FlexOS_Ultra_Font.h`,
`FlexOS_Ultra_Icons.h`, `FlexOS_Ultra_Wallpaper.h`, `FlexOS_Ultra_QuickPanelGlass.h`, `FlexOS_Ultra_AppFramework.h`,
`FlexOS_Ultra_DeX.h`, `FlexOS_Ultra_DeXDraw.h`, `FlexOS_Ultra_DeXInput.h`.
**Leídas en parte (lo que toca a esta área):** `FlexOS_Ultra_Core.h` (ciclo de vida, transiciones, gestos, memoria),
`FlexOS_Ultra_System.h` (`themeChanged`, `memShedSystem`), `FlexOS_Ultra_Prefs.h` (claves NVS), `FlexOS_Ultra_Session.h`
(formato de sesión, cadenas, nombres de apps), `FlexOS_Ultra_Home.h` / `FlexOS_Ultra_HomeCfg.h` (backdrop del escritorio,
temas, `homeCfgSave`), `FlexOS_Ultra_Types.h` (tipos), `FlexOS_Ultra_Text.h` (fuente 5×7), `FlexOS_Ultra_AppSettings.h`,
`FlexOS_Ultra_QuickPanel.h`, `FlexOS_Ultra_QuickPanelEdit.h`, `FlexOS_Ultra_AppChrono.h`, `FlexOS_Ultra_Conn.h`,
`FlexOS_FlexPhone_UI.h`.

Las rutas `archivo:línea` se refieren a `FlexOS_Ultra/` salvo que se diga otra cosa.

---

## 0. Convenciones de este documento

| Concepto Arduino | Significado | Equivalente LVGL |
|---|---|---|
| `SCR_W=480`, `SCR_H=800` (`Types.h:289`) | Pantalla física vertical | `FLEX_UI_W/H` en `flex_ui_theme.h` |
| `LW=SCR_H=800`, `LH=SCR_W=480` (`Types.h:292`) | Lienzo lógico horizontal (Modo PC, pantalla completa horizontal) | Pantalla rotada 90° |
| Colores `TC(r,g,b)` / `rgb565(r,g,b)` | Siempre RGB565 (se pierden los 3/2/3 bits bajos) | `lv_color_hex(0xRRGGBB)`; el panel es RGB565 igualmente |
| `mix565(a,b,t)` (`Gfx.h:318`) | Mezcla: `t=0`→`a`, `t=255`→`b`; división exacta por 255 sin dividir (`DIV255`) | `opa` de LVGL (0..255, `LV_OPA_*`) tiene **la misma escala** |
| `fillRoundRectA(...,a)` | Relleno con alfa `a`/255 | `bg_opa = a` |
| "tamaño de fuente" `size` | 1 = mapa de bits 5×7; ≥2 = Outfit escalada (ver §5) | Fuentes LVGL generadas con `lv_font_conv` |
| `gLand` | El motor dibuja girado 90°: `(lx,ly)` lógico → píxel físico `x = 479-ly`, `y = lx` (`Gfx.h:347`) | Rotación de display LVGL (ver riesgos §11) |
| `gClipY0/gClipY1/gClipX0/gClipX1` | Recorte rectangular de toda primitiva | Recorte natural de los objetos padre / `LV_OBJ_FLAG_OVERFLOW_VISIBLE` |
| `x,y` de un texto | **Tope de las mayúsculas/dígitos**, no la línea | En LVGL la `y` de una etiqueta es el tope de la caja de línea (ver §5.2) |

Hex de color: se dan desde los valores de 8 bits del código. En pantalla salen cuantizados a RGB565.

---

## 1. Motor gráfico de referencia (semántica que hay que conservar, no código a portar)

El motor Arduino (`FlexOS_Ultra_Gfx.h`) es un compositor por software. No se porta, pero varias de sus reglas son
**requisitos de comportamiento** que la versión LVGL debe mantener:

1. **Un solo dueño del pipeline de pantalla** (`flxFlush`, `Gfx.h:121`): la tarea de UI inicia la transferencia y espera
   el callback DMA con un tope de **120 ms** (`Gfx.h:149`); si falla escribe `"[GFX] draw_bitmap fallo; cuadro descartado"`
   o `"[GFX] timeout esperando DMA2D; compositor liberado"` (como máximo cada 2 s) y descarta el cuadro, nunca se bloquea.
   En IDF esto ya lo resuelve el puerto LVGL DIRECT de `docs/ARQUITECTURA_GRAFICA_LVGL.md`.
2. **Capas estampadas en el último momento** antes del panel (`flxFlush`): candado del Modo Kiosco
   (`kioskStampBadge`), **barra de navegación del sistema** (`navStampBar`), **banner de notificación** (`fpbStampBegin`).
   Ninguna app puede pintarlas ni taparlas. En LVGL: objetos en `lv_layer_top()`/`lv_layer_sys()`.
3. **Framebuffers a cero al arrancar** (`Gfx.h:289`): nunca debe verse basura de PSRAM (destello de color saturado).
   En LVGL: limpiar los dos framebuffers del DPI antes de habilitar el panel.
4. **Propiedad del back buffer** (`bbufClaim`, `Gfx.h:213`): si otro compositor escribió, el siguiente compone el cuadro
   entero una vez. Causa histórica del "blur pegado detrás de la app anterior". En LVGL no aplica (invalidaciones), pero el
   **efecto prohibido** se mantiene: un panel de vidrio jamás puede desenfocar píxeles de otra pantalla.
5. **`flxClampRect`** (`Gfx.h:501`, lógica pura): encaja un rect en `[0,limW)×[0,limH)` con tamaño mínimo y dejando
   siempre `keep` px alcanzables; el borde superior queda en `[0, limH-keep]` para que la zona de agarre (barra de título)
   nunca quede fuera. **Reutilizable tal cual** (se usa en DeX con `keep = 96`).
6. **Rectángulo redondeado**: inset por fila `r - isqrt(r² - d²)` con `d = r-1-k` (`rrInset`, `Gfx.h:529`); radio
   recortado a `min(w,h)/2`. Equivale a `radius` de LVGL (LVGL añade AA; es aceptable).
7. **Primitivas AA** (`fillCircleAA`, `strokeSegAA`, `Gfx.h:728/762`): cobertura = `r + 0.5 - dist`, alfa = cobertura·255;
   trazos con puntas redondeadas. En LVGL: `lv_draw_line` con `round_start/round_end`, `lv_draw_arc`, radios.
8. **Latido de actividad** `uiBusyFor(ms)` (`Gfx.h:245`): una app con movimiento propio pide ritmo alto sin dedo en
   pantalla; el bucle cede 1 ms en vez de 5 ms. En LVGL: periodo de `lv_timer_handler` / prioridad de la tarea UI.

---

## 2. Color: tokens, apariencia, acento y temas

### 2.1 Modelo: tres preferencias ORTOGONALES

| Variable | Qué elige | NVS (`flexos`) | Defecto | Dónde se cambia |
|---|---|---|---|---|
| `gDark` (`Theme.h:50`) | **Apariencia**: paleta oscura o clara (`TH()`) | `"dark"` bool | `true` (oscuro) | Ajustes → Pantalla → "Modo de apariencia"; Panel rápido "Tema"; DeX "Modo oscuro"; temas integrados |
| `uiGlass` (`Theme.h:41`) | **Material**: Liquid Glass (true) o Plano (false) | `"glass"` bool | `false` (Plano) | Ajustes → Pantalla → "Estilo"; Ajustes → Personalización → "Personalizar UI"; Panel rápido "Vidrio"; DeX "Liquid Glass" |
| `gIconStyle` (`Theme.h:51`) | **Estilo de icono**: 0 Plano, 1 Vidrio | `"iconstyle"` int | `0` | Ajustes → Personalización → "Iconos" |
| `gGlassLvl` (`Theme.h:423`) | Intensidad del vidrio 0..100, pasos de 5 | `"glasslv"` int | `50` (acotado 0..100 al leer, `Prefs.h:260`) | Panel rápido, deslizador "Intensidad del vidrio" (solo visible con Liquid Glass) |

Regla de oro (`Theme.h:36-50`): **el vidrio funciona en las dos apariencias** porque tinte y texto salen del tema.
Cambiar apariencia = cambiar qué struct devuelve `TH()` (`Theme.h:180`); ningún componente usa `gDark ? A : B`.

### 2.2 Paleta semántica (`FlexTheme`, `Theme.h:75-174`)

| Campo | Macro | Uso | Oscuro | Claro |
|---|---|---|---|---|
| page | `TH_PAGE` | Fondo de pantalla completa | `#12141C` | `#F4F7FB` |
| win | `TH_WIN` (= `WIN_BG`) | Fondo de ventana de app | `#12141C` | `#F4F7FB` |
| surf | `TH_SURF` | Tarjeta plana | `#222632` | `#FFFFFF` |
| surf2 | `TH_SURF2` | Superficie elevada (menú, diálogo, tecla, chip) | `#303648` | `#ECF0F8` |
| glass | `TH_GLASS` | Tinte de vidrio para `surf` | `#303648` | `#F6F8FC` |
| glass2 | `TH_GLASS2` | Tinte de vidrio elevado | `#28325A` | `#E0E8F6` |
| txt | `TH_TXT` | Texto principal | `#F0F2F8` | `#14161E` |
| txt2 | `TH_TXT2` | Texto secundario / valor | `#A0A6B6` | `#606674` |
| mute | `TH_MUTE` | Ayuda, pie, deshabilitado, asas | `#787E8E` | `#848A98` |
| nav | `TH_NAV` | Iconos de barra de estado/navegación en marco de app | `#E8ECF6` | `#222632` |
| border | `TH_BORDER` | Borde de superficie | `#424A5E` | `#CAD2E0` |
| divider | `TH_DIV` | Divisor interior | `#343A4A` | `#E0E5EE` |
| disabled | `TH_DIS` | Control desactivado | `#7C8292` | `#848A96` |
| track | `TH_TRACK` | Pista de switch/slider apagado | `#383E56` | `#CAD0DC` |
| sel | `TH_SEL` | Selección / foco | `#305CA8` | `#B2CEF6` |
| scrim | `TH_SCRIM` | Velo detrás de modales | `#080A12` | `#222836` |
| shadow | `TH_SHADOW` | Sombra | `#000000` | `#465064` |
| onAcc | `TH_ONACC` | Texto/icono sobre primaria/destructiva | `#FFFFFF` | `#FFFFFF` |
| primary | `TH_PRIM` | Acción primaria / acento del tema | `#3C6EEB` | `#2D5FE1` |
| danger | `TH_DANGER` | Acción destructiva | `#C84646` | `#C43434` |
| ok | `TH_OK` | Éxito | `#5AC88C` | `#168C58` |
| warn | `TH_WARN` | Advertencia | `#F0B45A` | `#B0740C` |
| err | `TH_ERR` | Error, punto de aviso | `#EB5050` | `#CC2E2E` |
| accSoft | `TH_ACCS` | Enlace / valor destacado | `#8CB4FA` | `#285CBE` |
| keyPanel | `TH_KEYPANEL` | Fondo del teclado | `#1E222E` | `#DEE2EC` |
| keyFace | `TH_KEYFACE` | Tecla normal | `#343846` | `#FFFFFF` |
| keyAlt | `TH_KEYALT` | Tecla de función | `#424656` | `#D6DBE6` |

Contraste declarado (`Theme.h:143`): texto principal ≥ ~4.5:1, secundario ≥ ~3:1 sobre su superficie, en plano y en vidrio.
Teclado: la relación panel/tecla se **invierte** entre apariencias (oscuro: tecla más clara que el panel; claro: tecla blanca
sobre panel gris), por eso tiene tokens propios.

**Superficie según material** (`Theme.h:236`): `thCard()` = `uiGlass ? glass : surf`; `thCard2()` = `uiGlass ? glass2 : surf2`.

### 2.3 Excepción deliberada: chrome sobre el wallpaper (`Theme.h:211-222`)

El wallpaper es **contenido** (no se retiñe con el tema y siempre es un degradado saturado/oscuro); lo que va encima
(barra de estado del Inicio y del Bloqueo, etiquetas de iconos, glifos de navegación, reloj grande, "Desliza arriba para
desbloquear") usa colores fijos en las dos apariencias:

| Macro | Uso | Color |
|---|---|---|
| `TH_ONWALL` | Texto/iconos sobre el wallpaper | `#FFFFFF` |
| `TH_ONWALL2` | Secundario sobre el wallpaper | `#D7DEEE` |
| `TH_WALLSURF` | Tarjeta/tecla sobre el wallpaper (plano) | `#2C365C` |
| `TH_WALLSURF2` | Ídem, tinte de vidrio | `#303C6E` |
| `TH_WALLPANEL` | Panel grande (teclado, apagado) sobre el wallpaper | `#24283A` |

### 2.4 Colores fuera de la paleta (hardcode que hay que tokenizar en IDF)

| Uso | Oscuro | Claro | Ref |
|---|---|---|---|
| Barra de navegación (modo Botones): fondo | `#0D0F16` | `#EEF1F7` | `AppFramework.h:1015` |
| Ídem: glifos | `#E8ECF5` | `#2C303C` | `AppFramework.h:1016` |
| Ídem: línea superior 1 px | `#1E222E` | `#D6DBE4` | `AppFramework.h:1017` |
| Borde de vidrio filas 0-2 | `#FFFFFF` | = | `Theme.h:527` |
| Borde de vidrio mitad superior | `#CDD6E4` | = | `Theme.h:527` |
| Borde de vidrio mitad inferior | `#161C28` | = | `Theme.h:527` |
| Especular / sombreado del vidrio | blanco / negro | = | `Theme.h:462` |
| Velo de `blurBg` | `#080A12` α70 | = (a propósito) | `Theme.h:1046` |
| Texto oscuro de `onColor` | `#10121A` | = | `Wallpaper.h:468` |
| Cursor de DeX | relleno `#FFFFFF`, contorno `#181C26` | = | `DeXDraw.h:1207` |
| Acentos de categorías de Ajustes | ver §7.10 | | `AppSettings.h:413` |

### 2.5 Acento activo (`wallAccent`, `Theme.h:228`)

```
wallAccent()  = (gWallPalOn && gWallPalOk) ? gWallAcc  : TH_PRIM
wallAccent2() = (gWallPalOn && gWallPalOk) ? gWallAcc2 : TH_ACCS
```
* `gWallPalOn` = "Aplicar paleta al sistema" (NVS `"wallpal"`, bool, defecto `false`).
* El acento extraído del fondo se calcula con `wallPaletteBuild` (§3.4). Lo usa, entre otros, el relleno de los
  deslizadores del panel rápido y el rol `UIS_ACCENT`.
* **Texto sobre cualquier color variable**: `onColor(bg)` (`Wallpaper.h:468`): luma Rec.601 `lum565(bg) > 140` →
  `#10121A`, si no `#FFFFFF`. `lum565`: canales expandidos a 8 bits, `(r·77 + g·151 + b·28) >> 8`.

### 2.6 Temas integrados ("looks", `Wallpaper.h:497-516`, aplicación `HomeCfg.h:401`)

Un tema **no** es una paleta nueva: fija a la vez apariencia, material, estilo de icono, fondo (escritorio y bloqueo) y paleta.

| # | Nombre (texto exacto) | dark | glass | iconStyle | fondo | paleta del fondo | Acento | Acento claro |
|---|---|---|---|---|---|---|---|---|
| 0 | Flex Original | 1 | 0 | 0 | 0 Flex Original | 0 | `#3C6EEB` | `#8CB4FA` |
| 1 | Claro | 0 | 1 | 0 | 1 Aurora | 0 | `#2D5FE1` | `#82B4FA` |
| 2 | Oscuro | 1 | 1 | 1 | 2 Nocturno | 0 | `#607CEB` | `#A0B4FF` |
| 3 | AMOLED | 1 | 0 | 0 | 4 Onyx | 0 | `#788CFF` | `#B4C4FF` |
| 4 | Oceano | 1 | 1 | 1 | 5 Oceano | 1 | `#26BEC4` | `#96F0EC` |
| 5 | Violeta | 1 | 1 | 1 | 6 Violeta | 1 | `#BA60E8` | `#E0A8F8` |
| 6 | Naturaleza | 0 | 1 | 0 | 7 Naturaleza | 1 | `#48AA50` | `#A8E096` |
| 7 | Alto contraste | 1 | 0 | 0 | 4 Onyx | 0 | `#FFD60A` | `#FFEC78` |

`hcApplyLook(i)` (`HomeCfg.h:401`): guarda el estado anterior, aplica los 5 campos, valida
(`fondo < WALL_N`, `iconStyle ∈ {0,1}`); si valida: `wallEnsureImage()`, `hcRebuildBlur()`, `hcBuildThumb()`
(miniatura + `wallPaletteBuild`), y si la paleta está apagada `gWallAcc = lookAcc(i)`. Si no valida restaura **todo** y
muestra `"No se pudo aplicar el tema"`. Después `homeCfgSave()` + `themeChanged(true)`. NVS: `"hlook"` int (0..7, defecto 0).

> **Rareza / bug a no copiar:** el acento propio de cada tema (`ar,ag,ab / sr,sg,sb`) solo se copia a `gWallAcc` cuando
> la paleta está **apagada**, pero `wallAccent()` solo lo lee cuando la paleta está **encendida**. Resultado: el acento del
> tema solo aparece en la muestra del selector (`HomeCfg.h:627`); en el sistema manda `TH_PRIM`. "Alto contraste" nunca
> pinta amarillo. En IDF: decidir explícitamente que el acento del tema **sí** sea el acento del sistema cuando la paleta del
> fondo está apagada.

### 2.7 Propagación de un cambio de tema (`themeChanged(save)`, `System.h:417`)

Único punto al que llaman todos los que tocan `gDark`/`uiGlass` (Ajustes, Panel rápido, DeX, temas):
1. `save` → `cfgSavePrefs()` (NVS `flexos`, claves `"dark"`, `"glass"`, `"glasslv"`, `"iconstyle"`… ver §10).
2. Invalida cachés con píxeles ya tematizados **sin liberar memoria**: `gHomeDirty` (escritorio), `qsDirty` (cortina del
   panel rápido), `glcValid=false` (tarjeta de vidrio cacheada), `uiGlassBandEnd()` (banda pre-desenfocada),
   `dexBgWall=0xFF` (fondo DeX) y **libera las miniaturas de Recientes** (capturas con el tema viejo).
   No invalida `blurBg` (el wallpaper es contenido, mismo velo en ambas apariencias).
3. Repinta la pantalla visible: Inicio/Bloqueo re-renderizan; en app: Ajustes conserva navegación/scroll; Notas y Paint
   conservan su vista; el resto re-ejecuta `enter()` con `gRelayout=true` ("re-dibujar, no re-inicializar"); Juegos no se
   interrumpe; Modo PC se repinta solo en su bucle.

> **Bug a no copiar (IDs obsoletos):** `themeChanged` compara `gAppId` con números del **registro v1**:
> `12` (era Ajustes, ahora Calendario), `10` (era Paint, ahora **Ajustes**), `11` (era Juegos, ahora Calculadora). Con el
> registro actual (`IC_AJUSTES = 10`) cambiar el tema desde Ajustes ejecuta la rama de Paint. Lo mismo ocurre con el dock
> (`drawHomeDock` pinta `12+i`, `getIconRect` usa "dock = ids 12..15"): en v1 eran Ajustes/Calculadora/Calendario/Cámara,
> hoy serían Calendario/Cámara/Clima/Flex Store. En IDF: **nunca** comparar con números literales; usar los `IC_*` y
> definir el dock como lista explícita.

En LVGL: un `flex_theme_apply()` que reconstruye los `lv_style_t` compartidos y llama a `lv_obj_report_style_change(NULL)`;
las cachés de imagen (backdrops de vidrio, miniaturas) se marcan sucias y se regeneran en diferido.

---

## 3. Fondos de pantalla (wallpapers)

### 3.1 Catálogo y almacenamiento

* `WALL_N = 8` fondos **procedurales** (se generan por código, no son imágenes) + `WALL_IMG = 200` (imagen JPEG del
  almacenamiento). Nombres exactos (`Wallpaper.h:44`): `"Flex Original", "Aurora", "Nocturno", "Halo", "Onyx", "Oceano",
  "Violeta", "Naturaleza"`.
* Preferencias (`homeCfgSave`/`homeCfgLoad`, `Home.h:1573-1614`, NVS `flexos`):

| Clave | Tipo | Defecto | Contenido |
|---|---|---|---|
| `"wallh"` | int | 0 | Fondo del escritorio (`0..7` o `200`) |
| `"walll"` | int | 0 | Fondo del bloqueo (`0..7` o `200`) |
| `"wallfit"` | int | 0 | Encuadre de la imagen: 0 rellenar · 1 ajustar · 2 centrar |
| `"wallpal"` | bool | false | Aplicar paleta del fondo al sistema |
| `"hlook"` | int | 0 | Tema integrado activo |
| `"wallpb"` | bytes[80] | vacío | Ruta de la imagen (char[80] terminado en 0). **Nota:** el comentario de `Wallpaper.h:49` dice `"wallp"`, la clave real es `"wallpb"` |

* Al cargar se acota todo: valores fuera de rango → 0; ruta sin terminador o clave ausente → vacía.
  Si no hay imagen y la paleta está apagada: `gWallAcc = lookAcc(hlook)`.
* Generación: `drawWallpaperRowsId(buf, id, blobs, y0, y1)` (`Wallpaper.h:277`) pinta **solo las filas** pedidas,
  fuerza vertical (`gLand=false`) y recorte completo de ancho, y restaura después. Con `id` inválido o `WALL_IMG` sin imagen
  cargada → **ruta segura**: fondo 0.

### 3.2 Algoritmos exactos de los 8 fondos (`Wallpaper.h:206-272`)

Primitivas comunes:
* **Diagonal de 3 paradas** `wallDiag3(lo, mid, hi)`: `tx = x·255/479`, `ty = (799−y)·255/799`, `t = (tx+ty)/2`;
  `t<128 → mix(lo, mid, 2t)`, si no `mix(mid, hi, 2(t−128))`. `lo` queda abajo-izquierda, `hi` arriba-derecha.
* **Bilineal de 4 esquinas** `wallCorners4(tl,tr,bl,br)`: `ty = y·255/799`, `L = mix(tl,bl,ty)`, `R = mix(tr,br,ty)`,
  píxel `= mix(L,R,tx)`.
* **Halo/anillo radial por LUT** (`wlutDisc`, `wlutRing`, `wallRadial`): alfa en función de la distancia `d` al centro.
  `wlutDisc(rIn,rOut,aIn)`: `d ≤ rIn → aIn`; `d ≥ rOut → 0`; lineal entre medias. `wlutRing(r,hw,aPk)`:
  `aPk·(hw−|d−r|)/hw` si `|d−r| < hw`. Se mezcla el color `c` sobre lo que hay con ese alfa.
* **Disco opaco con degradado lineal** `wallDisc(cx,cy,r,c0,c1,dirx,diry)`:
  `t = ((x−cx)·dirx + (y−cy)·diry)·127 / (r·(|dirx|+|diry|)) + 128` (acotado 0..255), píxel `= mix(c0,c1,t)`.

| id | Nombre | Receta |
|---|---|---|
| 0 | Flex Original | Diagonal 3 paradas: morado `#702EE6` (abajo-izq) → azul `#2896F5` (centro) → verde `#50E04A` (arriba-der) (`Gfx.h:836`). Con `blobs=true` (escritorio): círculo `#96EBB4` α60 en (360,150) r=220 y círculo `#96A0F0` α55 en (90,560) r=260 |
| 1 | Aurora | Esquinas TL `#CDEEEC`, TR `#3084F0`, BL `#48D0CE`, BR `#1856DC`; halo blanco disc(236,258,α90) en (330,205); disco (330,205) r=246 `#1A44CD`→`#68B0FC` dir(−1,−1); halo blanco disc(244,264,α105) en (96,486); disco (96,486) r=250 `#56DCB0`→`#BAF8DC` dir(1,−1); brillo `#8CCDFF` disc(150,330,α60) en (430,760) |
| 2 | Nocturno | Diagonal `#04050E`/`#08091A`/`#050612`; disco (340,110) r=300 `#0A0C26`→`#3E4698` dir(−1,−1) + anillo r300 hw6 α225 `#B6C0FF`; disco (92,620) r=330 `#090A22`→`#464EA0` dir(1,−1) + anillo r330 hw7 α240 `#C4CCFF`; disco (456,468) r=186 `#08091E`→`#30367C` dir(−1,1) + anillo r186 hw5 α205 `#A4AEFA`; brillo disc(70,260,α34) `#7886EB` en (286,300) |
| 3 | Halo | Diagonal `#020207`/`#050510`/`#020208`; brillo disc(230,430,α70) `#2E4ACD` en (72,430); centro común (206,402): núcleo disc(112,132,α255) `#11163A`; anillo(118,26,α190) `#4A70EC`; anillo(122,5,α235) `#B0C6FF`; anillo(252,34,α150) `#3A5CE0`; anillo(256,5,α200) `#9EB6FF`; anillo(392,44,α120) `#2C4ACE`; anillo(396,5,α165) `#8CA8FA` |
| 4 | Onyx | Negro `#000000` + disc(60,380,α46) `#1E40B4` en (240,690) |
| 5 | Oceano | Diagonal `#032048`/`#0A74A8`/`#26CEC0` + disc(70,300,α50) `#B4F4EC` en (400,180) |
| 6 | Violeta | Diagonal `#1A083A`/`#7A28B4`/`#E87CCC` + disc(80,320,α46) `#FFD2F0` en (90,250) |
| 7 | Naturaleza | Diagonal `#082C16`/`#3A8A3A`/`#BADE7A` + disc(80,300,α44) `#ECFABE` en (380,640) |

El fondo 0 es **bit a bit** el histórico (una placa que actualiza no ve cambio). Coste: LUT de 256 colores + rampa
horizontal de 480 entradas (`Gfx.h:831`), por bandas.

### 3.3 Fondo desde imagen del almacenamiento (`wallImgLoad`, `Wallpaper.h:368`)

* Se decodifica **una sola vez** a 480×800 RGB565 en `wallImg` (768 KB PSRAM, alineado a 64) al elegirla y al arrancar;
  después cada banda es un `memcpy`. Al volver a un fondo integrado se libera (`wallImgDrop`).
* Límite de lectura `WALL_IMG_MAX_BYTES = 512 KB`. Decodificador `FlexOS_JPEG` (el mismo de Galería), lectura LittleFS.
* Encuadre con el divisor del decodificador (1, 2, 4, 8): **rellenar** = mayor divisor que aún cubre 480×800 (se recorta
  centrado); **ajustar** = menor divisor que cabe entera (se centra; el hueco muestra el fondo 0 con manchas);
  **centrar** = 1:1 recortado por el centro. Antes de decodificar se pinta el fondo 0 debajo.
* Abortar porque "ya se llenó la pantalla" **no** es error (caso normal de rellenar/centrar).
* Mensajes exactos en `gWallErr` (máx. 39 caracteres), por orden de comprobación:
  `"Sin imagen elegida"`, `"La imagen ya no esta"`, `"Imagen demasiado grande"` (0 bytes o > 512 KB), `"Sin memoria"`,
  `"No se pudo leer"`, `"No es un JPEG valido"`, `"JPEG progresivo no admitido"`, `"Sin PSRAM para el fondo"`, y si no se
  escribió ninguna fila, `flexJpegErrStr(r)`.
* `wallEnsureImage()` (`Wallpaper.h:440`): si ninguno de los dos fondos es `WALL_IMG` libera la imagen; si hace falta y no
  carga, el fondo que la usaba vuelve a 0 **sin reiniciar**.

### 3.4 Paleta extraída del fondo (`wallPaletteBuild`, `Wallpaper.h:469`)

Rejilla de muestreo `y = 8, 48, …` (paso 40) × `x = 8, 32, …` (paso 24) ≈ 400 muestras, **solo al cambiar el fondo**.
Elige la muestra de mayor saturación `max−min` ignorando las casi negras (`max ≤ 40`). Si su luma < 70 se aclara
`c·2 + 40` por canal (tope 255). `gWallAcc = ese color`; `gWallAcc2 = (c + 255)/2` por canal. Respaldo si no hay muestras
saturadas: `#5A96F5`. Se llama desde `hcBuildThumb()` sobre el fondo recién pintado.

### 3.5 Fondo "desenfocado" reutilizable `blurBg` (`Theme.h:1029-1067`)

* Lo usan Recientes, verificación de clave, apagado, caja de apps y bloqueo. Se compone **una vez por sesión**
  (`ensureBlurBg`) y al cambiar de fondo (`hcRebuildBlur`, `HomeCfg.h:186`): `drawWallpaper(blurBg, blobs=true)` + velo
  `#080A12` α70 a pantalla completa (`blurBgVeil`), el **mismo en las dos apariencias**.
* Fuerza vertical y recorte completo al componerlo (bug histórico: llegar desde Juegos con `gLand=true` lo dejaba roto
  para siempre).
* **Rareza:** pese al nombre, **no hay desenfoque**: es el wallpaper velado. Con fondos procedurales no se nota; con una
  foto de usuario detrás de Recientes se ve nítida. En IDF: generar de verdad la versión desenfocada (ver §4.12).

### 3.6 Notas de migración de fondos

* **Lógica pura reutilizable tal cual:** generadores de los 8 fondos (escriben píxeles RGB565 en un búfer),
  `wallPaletteBuild`, `lum565`, `onColor`, selección del divisor de encuadre. Portarlos a C puro en `flex_ui` (o
  `flex_portable`) que escriba en un `lv_draw_buf_t` RGB565 de 480×800 en PSRAM, y comprobar **bit a bit** contra la
  versión Arduino en `tests/host` (patrón de `testKitTemaYPapelera`, `ino_compile.cpp:10383`; paleta en `:3114`).
* El fondo se muestra como `lv_image` a pantalla completa en la capa más baja del escritorio/bloqueo. Regenerar solo al
  cambiar de fondo/tema (nunca por cuadro).
* JPEG: usar el decodificador de IDF/`esp_jpeg` o el `FlexOS_JPEG` portado; mantener los mismos límites y textos de error.
* Persistencia: mantener **exactamente** las claves NVS anteriores para que una placa actualizada conserve su fondo.

---

## 4. Liquid Glass (material de vidrio)

### 4.1 Qué es (y qué no es)

Aproximación por software del vidrio de iOS 26 (`Theme.h:31-35`): **desenfoque real del fondo (box-blur separable) +
tinte adaptativo + especular arriba + sombreado abajo + borde direccional (luz desde arriba a la izquierda)**.
**No** hay refracción ni distorsión (se retiró el material avanzado, `Theme.h:407-415`).

### 4.2 Algoritmo base `drawLiquidGlassPanelEx(x,y,w,h,rad,tint,blurR)` (`Theme.h:576`)

```
0. Si hay backdrop activo y aplicable → glassFromBackdrop (§4.5) y fin.
1. Si gLand (horizontal) → relleno redondeado del tinte con alfa 210 (sin blur) y fin.
2. Reservar glassBuf (480×800×2 = 768 KB PSRAM) si no existe; sin PSRAM → relleno tinte α210 y fin.
3. Recortar el panel a la pantalla; rad = min(rad, w/2, h/2).
4. Filas visibles vy0..vy1 = panel ∩ [gClipY0, gClipY1]; si ninguna → fin.
   Filas a copiar y desenfocar: j0 = (vy0−y) − blurR … j1 = (vy1−y) + blurR (acotado al panel).
5. Tinte adaptativo: muestrear 1 de cada 4 filas × 1 de cada 8 columnas de TODO el panel (no solo la banda):
      glassLuma(c) = ((r5 + g6)·5 + b5·2) >> 1         (0..266, mismo dominio para fondo y tinte)
      dif = |media(luma fondo) − glassLuma(tint)|, acotado a 128
      tintMix = gGlTintMin + dif·(gGlTintMax − gGlTintMin)/128
      tintMix = max(tintMix, gGlMinMix)                 (suelo de legibilidad, normalmente 0)
   Sin muestras → gGlTintBase. Cuanto MÁS se parecen fondo y tinte, MENOS tinte (no se aplana).
6. Copiar filas j0..j1 a glassBuf y glassBlur(w, hc, blurR):
      pasada horizontal y luego vertical, ventana [i−R, i+R] que SE ENCOGE en los bordes
      (se divide por el nº real de muestras); resultado exacto (recíproco de 20 bits).
7. Componer cada fila visible j (0..h−1 del panel):
      ins = inset de esquina redondeada (glInset: rad − isqrt(rad² − dy²))
      si j < 0.45·h:  luz = blanco,  α = (1 − j/(0.45h))·gGlSpec
      si no:          luz = negro,   α = ((j − 0.45h)/(0.55h))·gGlShade
      píxel = mix( mix(blur, tint, tintMix), luz, α )     para i ∈ [ins, w−1−ins]
      Borde (solo los píxeles extremos izquierdo y derecho de la fila, en ins y w−1−ins):
        color = j<3 ? #FFFFFF : (j < h/2 ? #CDD6E4 : #161C28)
        peso izq = (j<h/2) ? gGlCornS : gGlCornW ;  peso der = (j<h/2) ? gGlCornW : gGlCornS
        píxel = mix(píxel, color, peso)
```
Observaciones visuales:
* El "borde" son los dos laterales más el contorno de las esquinas redondeadas (porque el inset varía por fila); **no hay
  línea horizontal** arriba/abajo, el canto superior lo da el especular.
* Con nivel 50: tinte 46..70/255 (**18-27 %**), especular 26/255 (~10 %) en el 45 % superior, sombreado 30/255 (~12 %) en
  el 55 % inferior, borde con peso 156/104 (61 %/41 %).
* `drawLiquidGlassPanel(x,y,w,h,rad,tint)` = `Ex` con `glassBlurR()` (`Theme.h:685`).

### 4.3 Intensidad del vidrio (`glassLevelApply`, `Theme.h:421-439`)

Un único nivel `gGlassLvl` (0..100, **pasos de 5**, se redondea hacia abajo) mueve los cuatro parámetros reales.
`d = nivel − 50`; división entera C (trunca hacia cero):

| Parámetro | Fórmula | 0 (sutil) | 25 | **50** | 75 | 100 (intenso) |
|---|---|---|---|---|---|---|
| Radio de blur `gGlR` | `6 + 4d/50` | 2 | 4 | **6** | 8 | 10 |
| Tinte mín `gGlTintMin` | `46 + 16d/50` | 30 | 38 | **46** | 54 | 62 |
| Tinte máx `gGlTintMax` | `70 + 20d/50` | 50 | 60 | **70** | 80 | 90 |
| Tinte base | `(mín+máx)/2` | 40 | 49 | **58** | 67 | 76 |
| Especular `gGlSpec` | `26 + 12d/50` | 14 | 20 | **26** | 32 | 38 |
| Sombreado `gGlShade` | `30 + 14d/50` | 16 | 23 | **30** | 37 | 44 |
| Borde fuerte `gGlCornS` | `156 + 40d/50` | 116 | 136 | **156** | 176 | 196 |
| Borde débil `gGlCornW` | `104 + 30d/50` | 74 | 89 | **104** | 119 | 134 |

El nivel 50 es **exactamente** el material histórico. Se recalcula una vez al cambiar (nunca por píxel/cuadro).
Tests existentes: `testIntensidadVidrio` (`tests/host/ino_compile.cpp:7583`: pasos de 5, tope 100, monotonía, nivel 50 =
histórico).

### 4.4 Modo visual eficiente (`gEffMode`, `Theme.h:652-684`)

* **Temporal, no es preferencia**: no se guarda en NVS, no aparece en Ajustes, no cambia el estilo elegido.
* Lo **enciende** "Optimizar Flex OS" (etapa 4, `System.h:~381`) solo si tras soltar todo lo seguro el nivel de memoria
  sigue ≥ `FLEXMEM_LV_WARN`; lo **apaga** `memTick()` (`Core.h:156`) cuando el nivel sostenido vuelve a `OK`.
  Al cambiar: `glcValid=false; gHomeDirty=true; qsDirty=true`.
* Efectos: radio de blur ≤ `GLASS_BLUR_R_EFF = 2` (`glassBlurR`); **sombras a la mitad de alfa** (`effShadow(a)`);
  Recientes conserva 1 miniatura en vez de 2 (`swThumbTrim(gEffMode ? 1 : 2)`, `System.h:127`).
* Con estilo Plano no cambia nada (no hay vidrio).

### 4.5 Vidrio sobre fondo pre-desenfocado: *backdrop* del escritorio (`Theme.h:466-574`, `Home.h:830-886`)

**Problema que resuelve:** las páginas del escritorio se deslizan sobre un wallpaper **fijo**; un panel desenfocado una
vez y luego desplazado arrastraba la imagen de donde se compuso.

* El escritorio prepara `hgBd`: la franja de página del wallpaper (filas `HOME_PAGE_TOP=72` … `HOME_BAND_BOT_MAX−1=595`,
  480×524, ~500 KB) **ya desenfocada** con el radio vigente, usando `R` filas de margen por cada lado
  (`homeBackdropEnsure`, `Home.h:830`). Solo se rehace si cambia el fondo o el radio. Sin vidrio en el escritorio
  (Plano + iconos Planos) ni se calcula ni se retiene.
* Mientras está activo (`homeGlassBegin`), cada panel lee el backdrop **en su posición actual de pantalla** (tinte
  adaptativo medido sobre la parte visible, cada 4 filas × 8 columnas) y aplica el material: mover el panel es releer en
  otro sitio, sin blur.
* No aplica (y se usa la ruta normal) si: no hay backdrop, el radio no coincide, `gLand`, o el panel se sale de las filas
  cubiertas. A diferencia de la ruta normal, el panel **se recorta** (no encoge) al salirse por un lado.
* Grabación: hasta `GL_REC_MAX = 48` paneles por página (`gGlRec[2][48]`, slot 0 = página actual, 1 = vecina) para
  **re-pintarlos desplazados** durante el deslizamiento (`homeGlassReplay`); desbordamiento → `gGlRecOvf`.

### 4.6 Vidrio sobre fondo plano: tarjeta cacheada y filas constantes (`Theme.h:689-821`)

Observación clave: sobre un color **uniforme** el box-blur devuelve ese mismo color, así que el resultado solo depende de
`(w, h, rad, tinte, color de fondo)`.
* `drawGlassCardFlat(x,y,w,h,rad,tint,bg)`: si `h ≤ GLC_MAX_H = 96` construye **una vez** la tarjeta (`glcBuild`, búferes
  `glcScratch` y `glcCard` de 480×96×2 = 92 KB cada uno) y luego cada tarjeta es un `memcpy` por fila; clave de caché
  `(w,h,rad,tint,bg)`. Más alta de 96 → `drawGlassPanelFlatRows` (cada fila interior es **un solo color**:
  `mix(mix(bg,tint,tintMix), luz, α)`, sin `glassBuf`; funciona también sin PSRAM). En `gLand` → panel normal.
* Es lo que permite que las listas con scroll (Ajustes, Ajustes del teclado, Flex Phone, CloudKit…) **mantengan el vidrio
  mientras se arrastra** (antes se apagaba: "al hacer scroll el material perdía el desenfoque").
* `uiSurfaceFlat(x,y,w,h,rad,role,bg)` (`Theme.h:1013`): solo debe usarlo quien acaba de rellenar ese fondo plano.

### 4.7 Vidrio durante animaciones: banda pre-desenfocada (`Theme.h:875-984`)

Para overlays que se animan sobre un fondo quieto (menú contextual de pulsación larga, menús de medios, tarjeta del
cronómetro): se desenfoca **una vez** la banda `[y0,y1]` al empezar (`uiGlassBandBegin`), y cada cuadro solo muestrea de
ella (`uiGlassPanelCached`) con una opacidad del material `a` (el panel aparece sin dejar de ser vidrio).
* Alto máximo `UIGL_BAND_MAX_H = 440` filas (peor caso: menú de medios de 8 opciones = 8×50+20 = 420); reserva en múltiplos
  de 32 filas, crece hasta el overlay más alto usado; máximo 480×440×2 = 394 KB.
* Tinte adaptativo medido una vez sobre toda la banda (no cambia de color a mitad de animación).
* Devuelve `false` (el llamante pinta plano o sin animar vidrio sobre vidrio) si: `gLand`, banda > 440, sin PSRAM.
* En el panel cacheado, el borde se escala por `a`: `peso·a/255`.

### 4.8 Superficie única del sistema (`uiSurfaceA`, `Theme.h:854-1027`)

Roles: `UIS_CARD` (0) tarjeta apoyada en la página · `UIS_ELEVATED` (1) menú, diálogo, tecla, chip ·
`UIS_ACCENT` (2) superficie de acción primaria.

| Rol | Color plano | Tinte de vidrio | Texto encima (`uiSurfOn`) |
|---|---|---|---|
| CARD | `TH_SURF` | `TH_GLASS` | `TH_TXT` |
| ELEVATED | `TH_SURF2` | `TH_GLASS2` | `TH_TXT` |
| ACCENT | `wallAccent()` | `wallAccent()` | `TH_ONACC` |

Decisión (`uiSurfaceA(x,y,w,h,rad,role,a)`):
```
a == 0                          → nada
uiGlass && banda activa         → uiGlassPanelCached(tinte, a)
uiGlass && a == 255             → drawLiquidGlassPanel(tinte)
uiGlass && a < 255 (sin banda)  → relleno redondeado del TINTE con alfa a (respaldo)
Plano                           → relleno redondeado del color PLANO con alfa a
```
`uiWallSurface(x,y,w,h,rad,col,blurR)` (`Theme.h:1024`): superficie sobre el wallpaper con colores `TH_WALL*` y radio de
blur propio (Recientes: hoja `TH_WALLSURF2/TH_WALLSURF` r24 blur 12; apagado: panel `TH_WALLPANEL` r44 blur 9 y pistas
blur 7; robo: r24 blur 10). Con Plano → relleno sólido.

**Suelo de tinte `gGlMinMix`** (`Theme.h:445`): quien dibuja vidrio sobre contenido ajeno (banner de notificación sobre una
foto clara o un documento blanco) lo sube a **150** mientras pinta (`FPB_MIN_MIX`, `FlexPhone_Overlay.h:167`; el visor usa
`VW_GLASS_MIN_MIX = 150`) y lo devuelve a 0.

### 4.9 Material del Panel rápido (cortina) — `FlexOS_Ultra_QuickPanelGlass.h`

#### 4.9.1 Capa de vidrio a 1/4 de escala (`qpGlassBuild`, `:741`)
1. Fondo `qsBgSrc()`: `homeBuf` en el escritorio, o **captura del último cuadro de la app** (`qsCaptureApp`, 768 KB,
   tomada una vez al empezar el gesto; sin memoria la cortina **no se abre**).
2. Reducción 4×4 con media de caja → 120×200 (`qsGlassSm`, 48 KB).
3. Box-blur corto con índices sujetos a los bordes (divisor constante `2R+1`), `R = clamp((glassBlurR()+2)/4, 1, 3)`
   (nivel 0→1, 50→2, 100→3).
4. Por píxel: si `uiGlass`, `mix(c, TH_GLASS2, 40)`; después velo `mix(c, TH_PAGE, qpVeilAlpha())`; en el quinto superior
   (40 filas pequeñas = 160 px reales) un extra `mix(c, TH_SURF2, 18 − 18·j/40)` (profundidad de cabecera).
   `qpVeilAlpha()` = **255 con Plano** (superficie sólida `TH_PAGE`), con vidrio `152 + (d<0 ? 16d/50 : 32d/50)` → 136..184.
5. Expansión bilineal por filas (`qpGlassRows`), opcionalmente cacheada a resolución completa en `qsGlassFull` (768 KB,
   "lujo opcional"). El panel compuesto (vidrio + contenido) vive en `qsBuf` (768 KB) y se compone **perezosamente** solo
   en las filas que el borde revela (`qsEnsureComposed`).
6. Se recalcula solo al cambiar de fondo (abrir sobre otra app/escritorio) o de tema; nunca por cuadro.
7. `qpFreeBuffers()` (`:820`) suelta todo (qsBuf, qsGlassSm, qsGlassFull, captura) al cerrar la cortina, al cancelar el
   editor y ante cualquier cambio de estado del sistema.

#### 4.9.2 Superficie del panel `qpGlassSurface(x,y,w,h,rad,tint,mixBase)` (`:52`)
Igual que §4.2 pero **sin copiar ni desenfocar** (el fondo de debajo ya es la capa desenfocada):
* tinte adaptativo en `[mixBase−12, mixBase+12]` (misma regla de luma, 1/32 de píxeles);
* especular/sombreado **acotados en píxeles**: zona superior `min(45 %·h, 70 px)` con `α = spec − spec·j/hTop`; zona
  inferior `min(55 %·h, 90 px)` con `α = shade·(j−yBot)/hBot`; en Plano se usan los valores fijos 26/30 y borde 156/104;
* mezclas base (`qpMixAdj`, `:146`; Plano → **255** = sólido): tarjeta/módulo **128**, círculo apagado **112**, acento
  (control activo) **178**, cabeceras fijas **168**; con vidrio se desplazan `d<0 ? 12d/50 : 24d/50`.
* Colores del panel (`QuickPanel.h:975`): tarjeta `qpCard() = thCard2()`; círculo apagado `qpTileOff() = mix(thCard2,
  TH_TXT, 34)`; cápsula encendida `qpCapOn() = mix(thCard2, TH_PRIM, 70)`.
* Sombra de tarjeta (`qpSurface`, `:159`): tres líneas bajo el borde inferior, de `x+rad` a `x+w−rad`, `TH_SHADOW` con alfa
  `effShadow(34, 22, 10)`.
* Glifos con hueco (luna, engranaje, cámara, candado, carpeta): el color de recorte se toma del **píxel real ya compuesto**
  en el centro del icono (`qpIcoBgAt`, `QuickPanel.h:345`).

#### 4.9.3 Pantallas dibujadas en este archivo (cortina, editor, catálogo)

Constantes (`QuickPanel.h:82-117`): `QP_MX=16` (margen), `QP_CONT_W=448`, `QP_GAP=12`, `QP_CW=103` (columna),
`QP_HDR_H=116` (cabecera fija), `QP_FOOT_H=34` (franja del asa), vista `116..765`, `QP_RH1=74`, `QP_RH2=160`,
`QP_RAD=26`, `QP_RAD_S=20`, `QP_GPAD=14`, `QP_TROW=98`, `QP_TCIRC=62`, `QP_HANDLE_H=22`, filas de tarjeta 2..5,
`QP_TOUCH_MIN=44`, `QP_TCOLW=105`, `QP_FLASH_MS=200`, `QS_OPEN_PCT=40`, `QS_SHADOW_H=18`, `QS_HANDLE_MARGIN=22`.

**Cabecera fija** (`qpDrawHeader`, `:202`):
* Hora (`clkStrBar`) en (16,22) tamaño **6** `TH_TXT` (caja de línea 48 px).
* Fecha corta (`buildShortDate`, p. ej. "mié, 19 ago.") a la derecha de la hora en x = 16 + ancho + 14, y=54, tamaño 2,
  `TH_TXT2`, recortada en x = 304.
* Segunda línea (16,84) tamaño 1 `TH_TXT2`: `"Modo avión activo"` o el estado real de Wi-Fi (`connWifiSub`). **No** hay
  porcentaje de batería (no se mide).
* Tres botones circulares r=22 centrados en y=50, x = 338 (lápiz), 390 (apagado, solo si `POWEROFF_ON`), 442 (engranaje);
  cara de vidrio `qpCard()` mezcla tarjeta; apagado con borde `mix(TH_BORDER, TH_DANGER, 140)` y glifo `TH_DANGER`; glifos
  tamaño 26 `TH_TXT`. Zona táctil: cuadrado de lado `QP_TOUCH_MIN/2+2 = 24` px de semilado alrededor del centro.
  Lápiz → editor; apagado → restaura fondo, cierra cortina, `poffEnter()`; engranaje → abre Ajustes.

**Módulos** (`qpDrawModule`, `:266`): superficie r20 cara `qpCapOn()` (activo) o `qpCard()`; icono en círculo r=22
(`TH_PRIM` + glifo `TH_ONACC` si activo, si no `qpTileOff()` + `TH_TXT2`, glifo tamaño 30).
* Vertical: icono centrado en `y+36`; título tamaño 1 en `icy+30`; subtítulo tamaño 1 `TH_TXT2` en `icy+44`.
* Horizontal: icono en `x+36`; título tamaño 2 en `icy−17` y subtítulo tamaño 1 en `icy+5` (o título en `icy−8` sin
  subtítulo), recortados a `x+w−12`.
* Destello de toque: `TH_TXT` con alfa `110·(1 − t/200 ms)`.

**Deslizador** (`qpDrawSliderBody`, `:237`): pista alto `th = max(h−20, 40)`, centrada; pista = vidrio `TH_TRACK`;
relleno = vidrio `wallAccent()` mezcla acento, ancho `th + (w−th)·pct/100` (nunca menor que el diámetro); icono tamaño 30 en
el extremo izquierdo (`TH_ONACC` si está sobre el relleno, si no `TH_TXT2`); valor tamaño 2 alineado a la derecha en
`x+w−20` (`TH_ONACC` si el relleno pasa de `w−70`). El de **intensidad del vidrio** reserva a la derecha un botón cuadrado
`th×th` (separación 8) de "restablecer" (vidrio `TH_TRACK`, glifo reset 26, `TH_MUTE` si ya está en 50, si no `TH_TXT`).
Texto del valor (`qpSubGlassFx`): `"Sutil N%"` (<35), `"Normal N%"`, `"Intenso N%"` (>65).

**Tarjeta expandible de círculos** (`qpDrawGroup`, `:305`): superficie r26; contenido recortado al interior
`[y+14, y+h−22)` × `[x+2, x+w−3]`; círculos de 62 px de diámetro (activo `TH_PRIM` mezcla acento, apagado
`qpTileOff()` mezcla 112), glifo 32; etiqueta tamaño 1 en `cy+31+8` (hasta el ancho de columna − 6, recortada) y
subtítulo real tamaño 1 `TH_TXT2` en `cy+31+22`. Asa inferior 56×6 r3 `TH_MUTE` centrada en `y+h−14`. Indicador de scroll
interno 3 px de ancho en `x+w−8`, alto proporcional (mín. 24), `TH_MUTE`, solo si hay más filas.

**Edición** (`qpDrawEditChrome`, `:401`): botón "−" `TH_DANGER` círculo r14 (módulos) / r12 (círculos) en la esquina
superior izquierda con glifo menos `TH_ONACC`; asa de redimensión en el borde derecho 6×32 r3 `TH_PRIM` + 4×16
`mix(TH_PRIM,TH_TXT,90)` (círculos: 6×24); botón de orientación en la esquina inferior izquierda r13
`mix(qpCard, TH_TXT, 70)` con arco y flecha; rechazo de tamaño: velo `TH_DANGER` alfa `120·(1 − t/350 ms)`. Hueco de
inserción: `TH_PRIM` α60 + borde `TH_PRIM`. Fantasma que sigue al dedo (`qpDrawGhost`) con sombra de 4 líneas alfa
`effShadow(76,56,36,16)`.
* Bloque "Añadir un control" (`qpDrawAddBlock`): vidrio `mix(qpCard, TH_PRIM, 70)` r20, borde `mix(TH_BORDER,TH_PRIM,150)`,
  "+" 28 y texto `"Añadir un control"` tamaño 2 `TH_TXT`.
* Cabecera del editor (`qpDrawEditHeader`, alto 96): banda de vidrio `TH_GLASS2` mezcla 168 a todo el ancho; `"Cancelar"`
  (16,20) tamaño 2 `TH_TXT2`; `"Editar panel"` centrado tamaño 2 `TH_TXT`; `"Listo"` alineado a la derecha tamaño 2
  `TH_PRIM`; `"Restablecer diseño"` centrado y=56 tamaño 1 `TH_TXT2`; divisor `TH_DIV` en y=95. Zonas: botones y 6..50,
  restablecer y 50..96.

**Catálogo "Añadir un control"** (`qpDrawCatalog`, `:523`): hoja de vidrio `TH_GLASS2` desde y=96; rejilla de 4 columnas,
filas de 112 px, círculos de 62 (seleccionado `TH_PRIM`), nombre tamaño 1 `TH_TXT` y categoría tamaño 1 `TH_TXT2`;
vacío: `"No queda ningún control disponible"` tamaño 2 `TH_MUTE`. Cabecera fija de 96: `"Atrás"` (16,22) tamaño 2
`TH_TXT2`, `"Añadir un control"` centrado tamaño 2, `"Solo se listan controles con función real"` y=56 tamaño 1; divisor
en y=95. Solo se ofrecen controles disponibles en esta placa y no presentes (el deslizador de intensidad no se ofrece con
vidrio apagado).

**Asa de cierre** (`qpDrawFooter`): píldora 88×6 r3 `TH_MUTE` centrada en y = 780.

#### 4.9.4 Comportamiento de la cortina (máquina de gestos `qpPanelTouch`, `:1446`)

Estados de gesto (decididos al presionar, **no cambian hasta soltar**): `QG_NONE, QG_PENDING, QG_CURTAIN, QG_SCROLL,
QG_GSCROLL, QG_RESIZE, QG_SLIDER, QG_EDDRAG, QG_CATSCROLL`.
* Cortina a medio abrir (`qsPanelY < 800`): todo el gesto es de la cortina.
* Toque en cabecera: botón (pendiente) o agarre (cortina). Franja inferior (y > 766): cortina. Asa de la tarjeta
  (±10/+14 px): redimensión. Deslizador: actúa en el acto. Otro bloque: pendiente.
* **Arrastre de la cortina 1:1 con el dedo** (sin easing ni filtro); zona muerta inicial 6 px; velocidad filtrada solo para
  decidir al soltar (`QS_VEL_TAU = 45 ms`). Al soltar: velocidad > 0.45 px/ms → abrir; < −0.45 → cerrar; si no, abrir si
  `qsPanelY ≥ 40 %` (320 px). Un **toque** solo cierra si cae en la franja inferior con el panel abierto del todo; tocar el
  vacío **no** cierra.
* Animación al soltar (`qsAnimTo`, `qsAnimStep` en `QuickPanelEdit.h:389`): duración `150 + dist·150/800` ms, curva
  **ease-in-out cúbica**; tocar durante la animación la cancela y devuelve el control al dedo.
* Borde móvil: sombra de 18 filas `TH_SHADOW` α `70·(1 − k/18)` (eficiente: mitad) + tirador 56×5 r2 `TH_MUTE` en
  `py−14`.
* Pendiente → scroll si `|dy| > 8` o `|dx| > 16`; si nació en la cabecera se convierte en arrastre de cortina; si nació en
  la tarjeta con filas ocultas → scroll interno. Pulsación larga **480 ms** → acción secundaria (`detail`, suele abrir
  Ajustes del control).
* Scroll con rebote elástico `0.42·exceso`; inercia `v·e^(−dt/190)`; asentamiento a los límites `1−e^(−dt/90)`.
* Redimensión de la tarjeta 1:1 con resistencia fuera de rango (máx. ±40 px); al soltar, "snap" a filas completas con
  duración `min(140 + 2·dist, 420)` ms y **ease-out-back** suave (`c1 = 0.55`), acotado a los límites.
* **Intensidad del vidrio:** mientras se arrastra solo se mueve el indicador (cuantizado a pasos de 5); al soltar
  `qpGlassFxCommit(v)` (`:1179`): `glassLevelApply`, invalida tarjeta cacheada y banda, rehace el escritorio fuera de
  pantalla, recompone la cortina y difiere el guardado NVS (`qpSavePrefs`) para después de publicar el cuadro. Tocar el
  botón restablecer → nivel 50. Si había una app debajo, al cerrar se repinta vía `themeChanged(false)`.
* Brillo: PWM real en el acto, guardado NVS diferido; volumen: registro del códec en el acto.
* Instrumentación `QP_PROF=1`: presupuesto 16 ms/cuadro; informe por `Serial` al cerrar
  (`"[QP] %s: %lu cuadros, medio %lu us, peor %lu us, lentos(>%d ms) %lu"` …).

### 4.10 Vidrio en Modo PC (`pcGlassPanel`, `DeXDraw.h:37`)

En horizontal la ruta de blur no vale (indexación vertical directa). DeX usa: relleno redondeado del tinte con **alfa 205**
+ borde 1 px `TH_BORDER`. `dexSurface` (`:49`): con Plano, relleno sólido + borde `DEX_BORDER`. El resto del sistema en
`gLand` cae a "tinte α210 sin blur". Esto **elimina los paneles fantasma** que salían al pintar iconos de estilo Vidrio en
coordenadas rotadas (`Theme.h:578-587`).

### 4.11 Inventario de memoria del material y quién la libera

| Búfer | Tamaño | Dónde | Lo libera |
|---|---|---|---|
| `glassBuf` (scratch de blur) | 768 KB | `Theme.h:244` | `memShedSystem` |
| `glcScratch` + `glcCard` | 2×92 KB | `Theme.h:769` | `memShedSystem` (+ `glcValid=false`) |
| `uiGlBand` | ≤ 394 KB | `Theme.h:884` | `uiGlassBandFree` |
| `blurBg` | 768 KB | `Theme.h:1030` | `memShedSystem` (si no está a la vista) |
| `hgBd` + `hpBg` (escritorio) | ~2×500 KB | `Home.h:806` | `memShedSystem` fuera del escritorio |
| `qsBuf`, `qsGlassFull`, `qsAppSnap`, `qsGlassSm` | 3×768 KB + 48 KB | QuickPanelGlass | `qpFreeBuffers` al cerrar |
| `dexBg` | 768 KB | `DeX.h:137` | `dexBgFree` al salir/suspender DeX |
| `wallImg` | 768 KB | `Wallpaper.h:51` | `wallImgDrop` sin fondo imagen |

`memShedSystem()` (`System.h:75`) devuelve los bytes **medidos** recuperados (nunca una cifra inventada).

### 4.12 Mapeo a LVGL 9.6 (propuesta) y riesgos

Estrategia recomendada (coherente con `docs/ARQUITECTURA_GRAFICA_LVGL.md`, "vidrio cacheado" por defecto):
1. **Backdrop cacheado por fondo**: al cambiar fondo/tema/intensidad, generar fuera del bucle de dibujo una imagen
   desenfocada del wallpaper (reducir 1/4 → box-blur `R_sm` → ampliar, como §4.9.1; opcional PPA para escalar).
   Mantener **una** copia (no una por superficie).
2. **Panel de vidrio** = `lv_obj` con `radius`, `clip_corner=true`, `bg_opa=0`; hijo 1: `lv_image` del backdrop colocado en
   `(−abs_x, −abs_y)` para que se vea la parte de detrás; hijo 2: capa de tinte `bg_color = tinte`,
   `bg_opa = tintMix` (calculado con `glassTintMix` sobre la media de luma del recorte, **una vez** al maquetar/mover) y un
   `bg_grad` vertical con paradas: blanco `opa=gGlSpec` en 0 % → transparente en 45 % → negro `opa=gGlShade` en 100 %
   (requiere `LV_GRADIENT_MAX_STOPS ≥ 3`); borde: aproximar con `border_width=1`, `border_side=LEFT|RIGHT` y color/opa
   medio, o un evento `LV_EVENT_DRAW_POST` que pinte las dos líneas con el peso direccional exacto.
3. **Sobre contenido no fijo** (overlays sobre una app): `lv_snapshot_take` de la región al abrir + blur una vez (equivalente
   a `uiGlassBandBegin`); nunca blur por cuadro durante animaciones (animar `opa`/posición del panel ya compuesto).
   Si LVGL 9.6 ofrece desenfoque de backdrop en estilos, reservarlo para superficies pequeñas y quietas.
4. **Plano** = mismo objeto sin hijos de backdrop: `bg_color` del rol con `bg_opa` total.
5. Centralizar en `flex_ui` una API `flex_surface_create(parent, role, material)` que haga el papel de `uiSurfaceA`; ningún
   componente decide material por su cuenta.

Lógica pura a portar con tests en `tests/host`: `glassLuma`, `glassTintMix`, `glassLevelApply` (tabla §4.3),
`glassBlur` (box-blur separable con ventana encogida; resultado exacto), `qpGlassBuild` (reducción + blur + velo),
`qpMixAdj`, `qpVeilAlpha`, `effShadow`. Tests Arduino de referencia: `testBlurNoPegado` (`:4934`),
`testLiquidGlassSinApilar` (`:5059`), `testVidrioSinArrastre` (`:7192`), `testIntensidadVidrio` (`:7583`),
`testPulsacionLargaVidrio` (`:10047`), `testBannerVidrioSigueAlFondo` (`:13477`).

Prohibiciones heredadas (bugs reales ya corregidos que no deben reaparecer):
* vidrio sobre vidrio en animaciones (cada cuadro desenfocando el panel del cuadro anterior → cada vez más claro);
* panel desenfocado que se desplaza con la imagen del sitio donde se compuso;
* desenfocar píxeles de otra pantalla (blur "pegado");
* apagar el vidrio mientras se hace scroll;
* capa plana desvanecida durante la animación que solo al final se vuelve vidrio.

---

## 5. Tipografía

### 5.1 Fuente (`FlexOS_Ultra_Font.h`)

* **Outfit-Regular**, un solo peso, maestro **antialiasado 4 bpp** generado (`FG[127]` glifos, `FBM[31539]` bytes).
  Métricas del maestro: `FONT_LINEH = 51`, `FONT_ASC = 40` (el maestro está a **40 px de em**). Tipo
  `FGlyph {w, h, bx, topoff, adv, off}` (`Types.h:45`).
* **No hay negrita ni cursiva**: la jerarquía se hace con tamaño y color.
* Repertorio (`fontIdx`, `Font.h:1641`): ASCII 0x20..0x7E + `á é í ó ú ü ñ Á É Í Ó Ú Ü Ñ à è ì ò ù â ê î ô û ã õ ç Ç ¿ ¡ ° ·`.
  Cualquier otro carácter → `?`. Chino: sin glifos (el idioma chino usa las cadenas inglesas, `LI()`, `Session.h:305`).
* Escalado: `escala = size·8/51` (`FONT_HPS = 8`), muestreo **bilineal** del maestro; un píxel se pinta si su cobertura
  > 4/255. `y` del texto = **tope de mayúsculas/dígitos** (`FONT_CAPOFF = 11` unidades del maestro).
* **Tamaño 1** = mapa de bits **5×7 nítido** (`FlexOS_Ultra_Text.h`, `FONT5x7`), avance fijo **6 px**, 7 px de alto, sin
  suavizado, con acentos dibujados como trazos AA encima (agudo, grave, circunflejo, tilde, diéresis, cedilla) y glifos extra
  `¿`, `¡` y `·` (el punto medio faltaba y salía `?` en "Notas · Pausada · 180 KB").

Tabla de equivalencias (para generar fuentes LVGL con `lv_font_conv` a partir de **Outfit Regular**):

| size | Uso típico | Caja de línea (px) | Em equivalente (px) | Altura de mayúsculas (px) | Desfase y (tope caja→tope mayúsculas) | Fuente LVGL sugerida |
|---|---|---|---|---|---|---|
| 1 | Etiquetas pequeñas, subtítulos, pies, nombres bajo iconos | 7 (bitmap) | — | 7 | 0 | Outfit 10-11 px (o fuente pixel 5×7 si se quiere el aspecto exacto) |
| 2 | Texto de cuerpo, títulos de fila, botones, reloj de barra de estado | 16 | 12.5 | ~9 | ~3.5 | Outfit 13 px |
| 3 | Título de cabecera de app, títulos de sección grandes | 24 | 18.8 | ~13.6 | ~5 | Outfit 19 px |
| 4 | Títulos de pantalla ("Ajustes"), nombres en ventanas | 32 | 25.1 | ~18 | ~7 | Outfit 25 px |
| 5 | Números/códigos grandes (emparejamiento, brújula) | 40 | 31.4 | ~23 | ~8.6 | Outfit 31 px |
| 6 | Hora de la cabecera del panel rápido | 48 | 37.6 | ~27 | ~10.4 | Outfit 38 px |

Recuento de uso en todo el firmware (llamadas `drawText*`): tamaño 1 ≈ 402, tamaño 2 ≈ 368, tamaño 3 ≈ 125,
tamaño 4 ≈ 33, tamaño 5 ≈ 6, tamaño 6 = 1, más ~100 con tamaño calculado (`uiFontFit`, `uiFontH`).
`uiLineH(fs)` del framework aproxima la línea como `fs ≤ 1 ? 8 : 9·fs` (§8.4).

### 5.2 Decodificación UTF-8 y plegado tipográfico (`Font.h:1688-1750`) — lógica pura

* `utf8Decode`: decodifica 1-4 bytes; byte suelto o secuencia cortada → U+FFFD (que se pinta `?`) **sin comerse** el byte
  siguiente.
* Invisibles que no se dibujan ni ocupan (`cpZeroWidth`): U+00AD (guion blando), U+200B..U+200F, U+2060, U+FEFF,
  U+FE00..U+FE0F (selectores de variante de emoji).
* Plegado (`cpFold`): U+2000..U+200A, U+00A0, U+1680, U+202F, U+205F, U+3000 → espacio; U+2010..U+2015 y U+2212 → `-`;
  U+2018..U+201B, U+2032 → `'`; U+201C..U+201E, U+00AB, U+00BB, U+2033 → `"`; U+2022, U+2027, U+2219 → `·`;
  U+2026 → `.`.
* Motivo: notificaciones de Android ("Google Play Store" salía con `?` por el espacio fino) y emojis (antes 4 `?`).
* **Rareza:** U+2026 (puntos suspensivos) se pliega a **un** punto. En IDF conviene plegarlo a `...`.
* Test de referencia: `testTextoUnicode` (`ino_compile.cpp:11006`).

### 5.3 Ajuste de texto al ancho

* `uiLabelFit(src, maxW, size, out, cap)` (`Font.h:1893`): mide con la fuente real; si no cabe corta en frontera de
  carácter y termina en `"..."`; nunca rebasa `cap` ni parte UTF-8. Ej.: "Antutu Benchmark for Flex OS" → "Antutu...".
  Tests: `ino_compile.cpp:1964-1998`.
* `drawTextClip(x,y,s,size,col,maxRight)` (`AppSettings.h:111`): corta sin puntos al llegar a `maxRight`.
* `dexTextFit` (`DeX.h:252`): corta y termina en **`".."`** (dos puntos), búfer de 48 bytes.
* En LVGL: `lv_label_set_long_mode(LV_LABEL_LONG_DOT)` con ancho fijo (LVGL usa "..."); para los textos de DeX unificar a
  "..." (diferencia cosmética aceptable).

### 5.4 Reloj vectorial grande (`drawBigChar`/`drawBigClock`, `Font.h:2031-2124`)

Dígitos `0-9` y `:` trazados con segmentos/arcos AA de grosor `thick` sobre una caja `ancho = 0.60·capH`, alto `capH`;
avance `0.72·capH` por dígito y `0.34·capH` para `:` (dos discos de radio `thick`). Usos: reloj del bloqueo/escritorio
(`Home.h:511`: centrado en x=240, y=242, `capH=140`, `thick=18`, `TH_ONWALL`) y pestaña Hora de la app Reloj
(`capH = alto·2/5`, máx. 150, mín. 22, `thick = capH/8` ≥ 3). En LVGL: fuente dedicada solo con `0123456789:` en una
variante fina de Outfit a ~140-150 px (poca flash), o dibujo vectorial propio en un evento de dibujo.

---

## 6. Iconos

Todos los iconos son **vectoriales por código** (ni un bitmap). Se dibujan en una caja `[x, x+S) × [y, y+S)` y **nunca se
salen** de ella (lo verifica `testIconosEnSuCaja`, `ino_compile.cpp:5197`, para todos los iconos y los dos estilos).

### 6.1 Iconos de app (`drawAppIcon(id,x,y,S)`, `Icons.h:88`)

Son **marca**: color propio fijo, no cambian con la apariencia. Base (`iconBase`, `Icons.h:77`): cuadrado redondeado de
radio **22 % de S**.
* Estilo **Plano** (`gIconStyle=0`): relleno del color de marca + brillo en la **mitad superior** blanco α22 (rectángulo
  redondeado de S×S/2 con el mismo radio).
* Estilo **Vidrio** (`gIconStyle=1`): `drawLiquidGlassPanel(x,y,S,S,r, color de marca)` → el color de marca actúa como
  **tinte** (18-27 %) sobre el fondo desenfocado; en horizontal, tinte α210.
* Grosor genérico de trazo `tk = max(S/12, 2)`.

| id | `IC_*` | Nombre ES (clave `APP[id][0]`) | Fondo | Glifo (proporciones respecto a S) |
|---|---|---|---|---|
| 0 | `IC_RELOJ` | Reloj | `#F5F5F7` | Esfera: anillo r=0.36S grosor 2 `#46464A`; marcas 12/3/6/9 (2×S/12); aguja horaria `#1E1E1E` hacia arriba-izquierda, minutero `#F58C1E` hacia arriba-derecha; eje `#1E1E1E` |
| 1 | `IC_GALERIA` | Galería | `#FFFFFF` | Flor de 8 pétalos (círculos r=0.135S a distancia 0.17S cada 45°): `#E94040 #F09628 #F0D232 #5AC85A #32BEBE #3C78EB #7850DC #DC50C8`; centro blanco r=0.11S |
| 2 | `IC_MULTIMEDIA` | Multimedia | `#1B5FD9` | Triángulo "play" blanco (−0.12S,±0.18S)→(+0.22S,0) |
| 3 | `IC_ALMACEN` | Almacenamiento | `#3B7BD9` | Carpeta `#E1ECFA` (pestaña 0.30×0.12 + cuerpo 0.64×0.34) con nubecita `#CDDEF5` |
| 4 | `IC_MODOPC` | Modo PC | `#1E3A6E` | Monitor: marco `#EBF0FA` 0.68×0.40 r4, pantalla `#2D5FCD`, pie y base `#C8D2E1` |
| 5 | `IC_NOTAS` | Notas | `#E8A75A` | Hoja blanca 0.48×0.62 r5 con 3 renglones `#B4B4B9`; lápiz `#785A28` en diagonal con punta `#F5D25A` |
| 6 | `IC_NAV` | Navegador | `#2E9BE6` | Globo: anillo r=0.30S, ecuador y meridiano rectos, dos arcos de meridiano r=0.18S, todo blanco |
| 7 | `IC_BRUJULA` | Flex Compass | `#102A60` | Esfera `#F6F8FC` r=0.34S con anillo `#1E428A`; 4 marcas cardinales `#607496`; aguja norte `#E23E3E` / sur `#6C7484`; eje `#1E283A` |
| 8 | `IC_PAINT` | Paint | `#F1E7D2` | Paleta `#ECE2CD` r=0.27S con hueco; 4 manchas `#E64646 #F0C83C #4682EB #50BE5A`; pincel `#8C643C` |
| 9 | `IC_JUEGOS` | Juegos | `#8E1E1E` | Mando: contorno doble redondeado blanco 0.72×0.30; cruceta y dos botones blancos |
| 10 | `IC_AJUSTES` | Ajustes | `#8A8F98` | Engranaje `#464A54`: disco r=0.26S + 8 dientes (r=0.075S a 0.30S); hueco central del color de fondo r=0.10S |
| 11 | `IC_CALC` | Calculadora | `#3A3A3C` | Pantalla `#D2D2D7` 0.64×0.16; teclado 3×4 (`#96969B`, última columna `#F5961E`) |
| 12 | `IC_CALEND` | Calendario | `#FFFFFF` | Banda superior `#E84646` 0.68×0.16; número "1" vectorial `#3C3C40` (drawBigChar capH 0.44S) |
| 13 | `IC_CAMARA` | Cámara | `#4A4A4E` | Objetivo `#1E1E20` r=0.27S con anillo `#787880`, cristal `#3C485F`, reflejo `#96AFCD`; flash `#BEBEC3` |
| 14 | `IC_CLIMA` | Clima | `#307CE2` | Sol `#FFCE48` r=0.18S con halo `#FFD660` α70 r=0.26S; nube blanca delante abajo-izquierda |
| 15 | `IC_FLEXSTORE` | Flex Store | `#695BE6` | Bolsa blanca 0.56×0.46 con asa trapezoidal y ranura del color de fondo |
| 16 | `IC_FLEXPHONE` | Flex Phone | `#2684FF` | Teléfono blanco 0.34×0.54 con pantalla del color de fondo y botón; dos arcos de enlace a la derecha |
| 17 | `IC_DEVCARE` | Device Care | `#14847A` | Escudo blanco (rect redondeado + punta) con pulso de diagnóstico de 5 tramos del color de fondo |
| 18 | `IC_MUSICA` | Música | `#EC4C6C` | Dos corcheas blancas unidas por barra |

`APP_N = 19`. El índice del enum **es** el id que viaja a NVS (favoritas, ocultas, candados); una app nueva se añade al
final. Traducción de registros antiguos: `APPREG_VER = 3`, mapas `APPREG_MAP_V1` (22 apps) y `APPREG_MAP_V2` (19)
(`Home.h:87-131`). Nombres en los 5 idiomas: `APP[APP_N][5]` (`Session.h:345`).

### 6.2 Iconos de barra de estado (`Icons.h:336-348`)

* **Wi-Fi** `drawWifi(cx, by, R, col)`: tres arcos de 225° a 315° (abiertos hacia arriba) de radio R, 0.66R, 0.33R,
  grosor 2, y punto r=2 en la base. Uso: marco de app `(414, 28, R=11)`.
* **Batería** `drawBattery(x,y,w,h,level,col)`: contorno doble redondeado r2, borne 2×h/3 a la derecha, relleno
  `(w−6)·level/100`. Uso: marco de app `(434, 20, 30×15)`.
  > **Rareza:** el nivel es **siempre 82** (marco de app, DeX, Ajustes → Batería "82%"), aunque el código dice que no se
  > mide la batería y que no se inventan datos. En IDF: no mostrar nivel si no hay medida real (o leerlo de verdad).

### 6.3 Glifos de navegación y cabecera

| Glifo | Geometría | Ref |
|---|---|---|
| Atrás (barra) | Triángulo relleno (cx−10, ny+8), (cx+8, ny−2), (cx+8, ny+18) | `AppFramework.h:1034` |
| Inicio (barra) | Anillo 2 px: dos círculos r=12 y r=11 | `:1035` |
| Recientes (barra) | Cuadrado redondeado 22×22 r4 (contorno) | `:1036` |
| Salir de pantalla completa | Cuatro esquinas en L hacia dentro, semilado 9, brazos 5, trazo 2 | `:1081` |
| Indicador de inicio (modo gestos) | Píldora 130×5 r2 centrada, 20 px sobre el borde inferior | `Home.h:459` |
| Chevron "volver" cabecera estándar | Trazos AA 2.4: (30,66)→(18,58)→(30,50) | `AppFramework.h:269` |
| Chevron "volver" `uiHdrChevron` | Centrado en la zona 56×56: (32,20)→(24,28)→(32,36), 2.4 | `:421` |
| Menú (tres puntos) | Círculos r=4 en x=452, y=14/28/42 | `:428` |
| Chevron de fila (Ajustes) | Trazos 2.0: (cx−3,cy−6)→(cx+3,cy)→(cx−3,cy+6) | `AppSettings.h:248` |

### 6.4 Glifos del Panel rápido (`QuickPanel.h:296-511`, firma `(cx, cy, s, col)`)

`qpIcoDnd` (luna/campana tachada de No molestar), `qpIcoWifi` (dos arcos + punto, grosor 3), `qpIcoAirplane` (avión con
alas en delta, morro y cola), `qpIcoBle` (runa Bluetooth con trazos 1.7), `qpIcoSpeaker` (altavoz con 0/1/2 ondas según el
volumen real: >5 y >45), `qpIcoMute` (altavoz con aspa), `qpIcoSun` (disco + 8 rayos), `qpIcoMoon` (media luna por
recorte con el color real de la superficie), `qpIcoGlass` (dos láminas cuadradas redondeadas superpuestas),
`qpIcoGlassFx` (láminas + reflejo diagonal), `qpIcoReset` (flecha circular 40°..320° con punta), `qpIcoBattSave` (batería
con rayo), `qpIcoGear` (disco + 8 dientes + hueco), `qpIcoSignal` (4 barras crecientes), `qpIcoMonitor` (pantalla con pie),
`qpIcoUpdate` (flecha hacia abajo sobre bandeja), `qpIcoFolder` (carpeta con interior recortado), `qpIcoCamera` (cuerpo +
objetivo recortado), `qpIcoImage` (marco con sol y dos montañas), `qpIcoStopwatch` (esfera + pulsador + aguja), `qpIcoLock`
(candado con ojo recortado), `qpIcoPower` (arco −68°..248° + barra vertical), `qpIcoClock` (esfera + dos agujas),
`qpIcoPencil` (lápiz diagonal con punta), `qpIcoPlus`, `qpIcoMinus`.

### 6.5 Iconos de Ajustes (`AppSettings.h:151-227`)

Glifos de fila `drawRowGlyph`: `RI_GLOBE` (globo), `RI_CAL` (calendario), `RI_CLOCK` (reloj), `RI_PIN` (chincheta con hueco
del color de la tarjeta), `RI_REFRESH` (flecha circular), `RI_CLOUD` (nube), `RI_RESET` (flecha circular inversa),
`RI_DOT` (punto r=4, por defecto). Categorías `drawSetCatIcon` (S=32): engranaje, sol, altavoz, Wi-Fi, cubo isométrico,
pincel, candado, batería, discos apilados, `</>`, info `(i)` (General, Pantalla, Sonido, Red, Dispositivos,
Personalización, Seguridad, Batería, Almacenamiento, Desarrollador, Sistema, Acerca de).

### 6.6 Iconos de Modo PC (`DeXDraw.h:113-157`, `:173-192`, `:1183-1228`)

Chip base `dexIcoChip`: cuadrado redondeado radio s/4, `DEX_ACCENT` si activo, si no `TH_SURF2`; glifo `TH_ONACC` o
`DEX_TXT_HI`. Glifos: cuadrícula 3×3 (cajón), lupa (buscador), campana con punto de aviso r4 `TH_ERR` si hay
notificaciones, engranaje simplificado (ajustes rápidos), touchpad (rect + línea). Botones de ventana: minimizar (barra
11×2), maximizar (rect 11×9) / restaurar (dos rects 9×8 superpuestos), cerrar (aspa ±4 trazo 1.5 sobre `TH_DANGER`).
Cursor: flecha (dos triángulos blancos con contorno `#181C26`), manita (palma 13×15 r5 + dedo 5×12 r2), cursor de texto
(I-beam 3×19 con remates 9×2).

### 6.7 Migración de iconos

* Opción recomendada: exportar cada icono a **SVG** a partir de estas recetas y convertir a imágenes LVGL (RGB565A8) en los
  tamaños usados (escritorio 72, dock 64, cajón 52/56, DeX 16/20/32/40/52/54, ventanas, Recientes), o a una fuente de
  iconos para los glifos monocromos (barra, panel rápido, Ajustes, DeX). El estilo Vidrio de icono = base de vidrio
  (§4.12) + glifo encima; el estilo Plano = base de color + brillo superior α22.
* Mantener el contrato "el icono nunca sale de su caja" y los colores de marca exactos.
* Glifos con hueco: en LVGL usar transparencia real en vez de "recortar con el color de la superficie" (el truco
  `qpIcoBgAt` desaparece).

---

## 7. Componentes comunes (métricas de referencia)

### 7.1 Cabecera de app compartida (`uiHdr*`, `AppFramework.h:378-443`)

```
0            56              424            480
|-- atrás --|---- título ----|--- menú ----|
```
`UIHDR_ZONE = 56` (zonas táctiles 56×56, ≥ 44), `UIHDR_H = 76` (alto de banda), `UIHDR_GAP = 8`, título desde x=64 hasta
x=416 (con menú) o hasta 472 (sin menú), centro vertical de glifos y=28. El título baja de tamaño hasta caber y si aun así
no cabe se recorta (`drawTextClip`); nunca invade un botón. Hit-tests `uiHdrBackHit`, `uiHdrMenuHit`. Test:
`testCabeceras` (`ino_compile.cpp:2730`).

### 7.2 Cabecera estándar del framework (apps sin `APP_CUSTOM_HEADER`) (`appDrawHeader`, `:264`)

Chevron en (18..30, 50..66) y nombre de la app centrado (x=240) en y=53, **tamaño 3**, `TH_TXT`. Toque: cualquier `tap`
con `y ≤ WIN_TOP (96)` y `x < 72` → `sysBack()` (`Core.h:1003`).

### 7.3 Barra de estado del marco de app (`appDrawChrome`, `:237`)

Se **borra** antes de pintar (`fillRect(0,0,480,46, WIN_BG)`) para que la hora proporcional no se superponga
("AM" doble). Hora en x=20, y=16, tamaño 2 (`cronoBarClock`, con cápsula del cronómetro si está activo; si no cabe, la
cápsula manda y la hora se oculta); Wi-Fi (414,28,R11); batería (434,20,30×15); color `TH_NAV`. En modo gestos pinta el
indicador de inicio (`drawHomeIndicator(800, 180)` con color por defecto `TH_ONWALL`).
> **Rareza:** en apariencia clara el indicador blanco sobre `TH_WIN` claro es casi invisible (Ajustes usa `TH_NAV`).
> En IDF: usar `TH_NAV` en el marco de app.

### 7.4 Tarjetas y filas

| Componente | Medidas | Material | Ref |
|---|---|---|---|
| Fila de Ajustes `setRowCard` | Paso 64; tarjeta x=14, w=452, alto 56, r14; glifo en (40, centro); título tamaño 2 en (66, y+10); valor tamaño 1 `TH_TXT2` en (66, y+34); chevron en x=446; recorte del texto en x=436 | Vidrio cacheado `TH_GLASS` sobre `TH_PAGE`, o `TH_SURF` | `AppSettings.h:239` |
| Tarjeta de categoría | 452×66 r16, separación 8, icono 32 en (30, centro), título tamaño 2 en (76, y+14), subtítulo tamaño 1 en (76, y+38) | Ídem | `AppSettings.h:419` |
| Línea informativa `drawInfoLine` | Etiqueta tamaño 1 `TH_TXT2` a la izquierda, valor tamaño 1 `TH_TXT` alineado a x=468; paso 24 | — | `AppSettings.h:252` |
| Tarjeta Flex Phone `fgCard` | r16 por defecto; variante con barra de acento 4 px a la izquierda (x+3, y+10, h−20) | Vidrio cacheado | `FlexPhone_UI.h:146` |

### 7.5 Interruptor (pastilla) `connPill` (`Conn.h:208`)

Pista redondeada (radio h/2): `TH_OK` encendido, `TH_TRACK` apagado, `TH_DIS` deshabilitado; mando circular `TH_ONACC`
de radio `h/2 − 4`, a la derecha si encendido; deshabilitado: aspa `TH_MUTE` sobre el mando. Flex Phone usa 46×24 r12 con
`TH_PRIM`/`TH_TRACK`. En LVGL: `lv_switch` con estilos de estos tokens.

### 7.6 Botones `fgButton` (`FlexPhone_UI.h:239`)

Radio `min(h/2, 16)`; estilos: normal `TH_SURF2`/`TH_TXT`, primario `TH_PRIM`/`TH_ONACC`, peligro `TH_SURF2`/`TH_DANGER`,
deshabilitado `TH_TRACK`/`TH_DIS` (**no registra zona táctil**: no puede haber un botón que parezca pulsable y no haga
nada). Texto tamaño 1 centrado.

### 7.7 Deslizadores

Panel rápido (§4.9.3), brillo de DeX (pista 26 px r13, §9.8), barras de progreso de apps (6 px r3 sobre `TH_TRACK`).
En LVGL: `lv_slider` / `lv_bar` con `radius = LV_RADIUS_CIRCLE`.

### 7.8 Pestañas segmentadas (Reloj, `relojDrawTabs`, `AppFramework.h:511`)

Barra de alto `46 − 10 = 36` en `y = WIN_TOP + 5`, márgenes `uiPad()`; superficie `uiSurface(..., r = h/2, UIS_CARD)`;
segmento activo relleno `TH_PRIM` r=(h−4)/2 con texto `TH_ONACC`; inactivos `TH_TXT2`; texto `uiFontFit(..., seg−12, 2)`.
Textos `t(S_CRN_HOUR)`="Hora", `t(S_CRN_STOPW)`="Cronómetro", `t(S_CRN_TIMER)`="Temporizador". Se ocultan si el lienzo
mide menos de 106 px de alto (ventanas de DeX diminutas). El toque en la barra se consume.

### 7.9 Menús, diálogos, hojas y toasts

* Menús y diálogos del sistema: rol `UIS_ELEVATED` vía `uiSurfaceA` con banda pre-desenfocada si se animan (§4.7).
  Ejemplos: menú contextual del escritorio (`AppDrawer.h:156`, opacidad `238·a/255`), tarjeta del cronómetro
  (`AppChrono.h:727`, α200 con vidrio / α240 plano), aviso de caída (`FallAlert.h:222`, r28), emparejamiento de
  almacenamiento (`StoragePair.h:139`, r28).
* Toasts/avisos del sistema: **isla de notificaciones** (`sysNotify(title, sub)`, `Media.h:66`; cola `NOTIF_MAX = 3`,
  una tarjeta a la vez) — especificada en el área de notificaciones. Toasts locales: `swToast`, `storeToast`, `wxToast`,
  `fphToastShow`.
* Placeholder de app sin implementar (`appPlaceholderEnter`, `AppFramework.h:480`): con vidrio, panel `TH_GLASS2`
  (36, 248, 408×268, r26); icono 88 en (196, 286); `"En construcción"` tamaño 3 `TH_TXT` en y=422; `"Llega en el
  Milestone 2"` tamaño 2 `TH_TXT2` en y=464 (claves `S_SOON`, `S_M2`).

### 7.10 Acentos de categorías de Ajustes (`AppSettings.h:413`)

General `#4678EB`, Pantalla `#F0AA32`, Sonido `#4678EB`, Red e Internet `#3C96EB`, Dispositivos `#50B478`,
Personalización `#5A6EEB`, Seguridad y privacidad `#5A5F6E`, Batería `#50BE6E`, Almacenamiento `#965AD2`,
Desarrollador `#464B5A`, Sistema `#4678EB`, Acerca de `#4678EB`.

### 7.11 Puntos de control del diseño en Ajustes (textos exactos)

* Ajustes → Pantalla (`AppSettings.h:315`): `"Brillo"` (valor `"N%"`, cada toque +25 cíclico 25→100), `"Estilo"`
  (`"Liquid Glass"`/`"Plano"`), `"Modo de apariencia"` (`"Oscuro"`/`"Claro"`), `"Barra de navegacion"`
  (`"Botones"`/`"Gestos iOS"`), ayuda `"Toca una fila para cambiarla"`.
* Ajustes → Personalización (`:329`): `"Personalizar UI"` (`"Liquid Glass"`/`"Plano"`), `"Iconos"` (`"Vidrio"`/`"Plano"`),
  `"Transiciones"` (`"Zoom"`/`"Fundido"`/`"Deslizar"`, cíclico), `"Teclado"` (`"Compacto"`/`"Normal"`/`"Grande"`), ayuda
  `"Toca una fila para cambiar su estilo"`.
* Panel rápido: `"Tema"`/`"Modo oscuro"` (sub `"Oscuro"`/`"Claro"`), `"Vidrio"`/`"Liquid Glass"` (sub
  `"Liquid Glass"`/`"Plano"`), `"Vidrio"`/`"Intensidad del vidrio"` (deslizador 4×1, solo con vidrio).

---

## 8. Framework de apps (`FlexOS_Ultra_AppFramework.h` + ciclo de vida en `FlexOS_Ultra_Core.h`)

### 8.1 Propósito

Una app de Flex OS **es** su fila en `APP_REG` (`AppFramework.h:739`); su id es el índice (= enum `IC_*`). El framework
posee el marco (barra de estado, cabecera con "atrás", barra de navegación), los gestos de salida, las transiciones, el
ciclo de vida (suspender/reanudar/cerrar), la persistencia de sesión y el reparto de memoria. Las apps solo pintan su
contenido en el área de ventana y atienden sus toques.

### 8.2 Registro de apps y banderas

`FlexApp { enter, tick, flags, cat, dflt, hooks }` (`AppFramework.h:64`).

| Bandera | Valor | Significado |
|---|---|---|
| `APP_CUSTOM_HEADER` | 1 | La app pinta su propia cabecera (no la centrada del framework); tampoco recibe el marco estándar |
| `APP_OWN_TOUCH` | 2 | La app gestiona todos sus toques (solo el sistema conserva la barra de abajo) |
| `APP_LAND` | 4 | La app dibuja en horizontal y pone `gLand` por su cuenta (Juegos) |
| `APP_FLEX` | 8 | Maqueta contra `gAppW/gAppH` (lienzo real): se dibuja 1:1 en ventanas de DeX y admite pantalla completa |
| `APP_BG_KEEP` | 16 | Lista blanca de segundo plano: si su `bgWork()` dice que trabaja, ni "Cerrar todo" ni el desalojo la tocan |
| `APP_IMMERSIVE` | 32 | Sabe ir a pantalla completa (vertical/horizontal); Inicio ofrece "Pantalla completa" en su menú |

| id | App | Flags | Categoría | Inicio de fábrica | Hooks (`AppHooks`) |
|---|---|---|---|---|---|
| 0 | Reloj | FLEX | Esenciales | sí | — |
| 1 | Galería | FLEX, BG_KEEP | Multimedia | sí | backLayer, backScreen, suspend, resume, close, bgWork, shed, dirty |
| 2 | Multimedia | CUSTOM_HEADER, OWN_TOUCH | Multimedia | sí | backLayer, backScreen, suspend, resume, close, saveSess, loadSess, shed |
| 3 | Almacenamiento | FLEX | Sistema | sí | backScreen, suspend, resume, close |
| 4 | Modo PC | CUSTOM_HEADER | Sistema | sí | suspend, resume, close |
| 5 | Notas | CUSTOM_HEADER, OWN_TOUCH | Productividad | sí | backLayer, backScreen, suspend, resume, close, saveSess, loadSess, dirty |
| 6 | Navegador | FLEX, OWN_TOUCH, IMMERSIVE | Esenciales | sí | backLayer, suspend, resume, close, shed |
| 7 | Flex Compass | CUSTOM_HEADER, OWN_TOUCH | Esenciales | sí | backLayer, suspend, resume, close, shed |
| 8 | Paint | CUSTOM_HEADER, OWN_TOUCH | Ocio | sí | backScreen, suspend, resume, close, saveSess, loadSess, dirty |
| 9 | Juegos | OWN_TOUCH, CUSTOM_HEADER, LAND | Ocio | sí | suspend, resume, close |
| 10 | Ajustes | CUSTOM_HEADER | Sistema | no | backScreen, suspend, resume, saveSess, loadSess, bgWork (OTA en curso) |
| 11 | Calculadora | FLEX | Productividad | no | resume, saveSess, loadSess |
| 12 | Calendario | FLEX | Productividad | no | resume |
| 13 | Cámara | CUSTOM_HEADER, OWN_TOUCH | Multimedia | no | suspend, resume, close, shed |
| 14 | Clima | CUSTOM_HEADER, OWN_TOUCH | Esenciales | no | backScreen, suspend, resume, shed |
| 15 | Flex Store | CUSTOM_HEADER, OWN_TOUCH, FLEX | Sistema | sí | suspend, resume, close |
| 16 | Flex Phone | CUSTOM_HEADER, OWN_TOUCH, FLEX | Esenciales | no | backScreen, suspend, resume, close |
| 17 | Device Care | CUSTOM_HEADER, OWN_TOUCH, FLEX | Sistema | sí | backScreen, suspend, resume, close, saveSess, loadSess, bgWork |
| 18 | Música | FLEX, BG_KEEP | Multimedia | no | backLayer, backScreen, suspend, resume, close, bgWork, shed |

Categorías (`APP_CAT_NAME`, `:652`), ES/EN/FR/PT/IT: `"Esenciales","Essentials","Essentiels","Essenciais","Essenziali"`;
`"Multimedia","Media","Médias","Mídia","Multimedia"`; `"Productividad","Productivity","Productivité","Produtividade",
"Produttività"`; `"Sistema","System","Système","Sistema","Sistema"`; `"Ocio","Fun","Loisirs","Lazer","Svago"`.
Favoritas de fábrica = las 12 con `APP_DEF_FAV` (`drawerRegistryDefaults`, `:804`; `drawerRegistryAdopt(fromId)` aplica la
fábrica solo a ids nuevos). Escritorio de fábrica `HOME_FACTORY` (`Home.h:88`): Reloj, Galería, Multimedia,
Almacenamiento / Modo PC, Notas, Flex Store, Navegador / Flex Compass, Device Care, Paint, Juegos.

Contrato `AppHooks` (`Types.h:131`), todos opcionales (NULL = no aplica):
`backLayer()` cierra teclado/menú/diálogo propio (true = cerró algo) · `backScreen()` retrocede una pantalla interna ·
`suspend()` congela y suelta lo secundario · `resume()` repinta desde el estado lógico **sin reiniciar** · `close()` libera
todo · `saveSess()`/`loadSess()` sesión en disco · `bgWork()` trabajo real en segundo plano · `shed()` suelta recursos
pesados reconstruibles (solo suspendida) · `dirty()` cambios sin guardar.

### 8.3 Área de ventana y marco

* `WIN_TOP = 96`, `WIN_BOT = 736` (800 − 64). Embebida en DeX (`gHosted`) o en pantalla completa: `0` y `gAppH` (sin
  marco). `WIN_BG = TH_WIN` (`AppFramework.h:48-50`).
* Marco estándar: barra de estado (§7.3), cabecera (§7.2) para apps sin `APP_CUSTOM_HEADER`, barra de navegación del
  sistema (§8.8).
* Apertura con marco (`appTrFinishOpen`, `Core.h:878`): fuerza vertical, recorte completo, `immersiveLayout()`; en
  pantalla completa limpia el lienzo entero con `WIN_BG`; si no, `appDrawChrome` + `appDrawHeader`.

### 8.4 Kit de maquetación adaptativa (`AppFramework.h:275-376`) — lógica pura

| Función | Regla |
|---|---|
| `uiBox(x,y,w,h)` | `x=0, y=WIN_TOP, w=gAppW, h=WIN_BOT−WIN_TOP`, mínimos 32×32 |
| `uiPad()` | `clamp(min(w,h)/24, 5, 22)` (a pantalla completa vertical: 20) |
| `uiGap()` | `max(uiPad·3/4, 4)` |
| `uiFontFit(t, maxw, maxSize)` | Mayor tamaño ≤ min(maxSize, 5) cuyo ancho cabe; mínimo 1 |
| `uiFontH(lineH)` | ≥44→5, ≥33→4, ≥23→3, ≥14→2, si no 1 |
| `uiLineH(fs)` | `fs ≤ 1 ? 8 : 9·fs` |
| `uiTitle(x,y,w,t,col,maxSize)` | Título centrado que cabe; devuelve `y + uiLineH(fs) + uiGap/2` |
| `uiSection(id, want)` | Secciones opcionales con *breakpoint*: aparecen/desaparecen **enteras** con fundido `UI_FADE_MS = 130` ms (máx. 12 secciones registradas); mientras hay fundido `uiFading = true` (DeX sigue pidiendo cuadros) |
| `uiRectA`, `uiText*` | Relleno/texto con alfa para las secciones que funden |
| `uiClipViewport(top, bot)` / `uiClipFull()` | Recorte **exclusivo** del área con scroll: fuera de él no se escribe ni un píxel aunque una coordenada esté mal (corrige filas pintadas sobre la cabecera). Test `testListasConScroll` (`:2834`) |
| `gRelayout` | `true` mientras se re-ejecuta `enter()` solo para re-maquetar: la app debe re-dibujar sin reiniciar estado |

App de referencia **Reloj** (`appRelojRender`, `:611`): contenedor único que limpia, pinta pestañas, delega el cuerpo y hace
**un** flush. Pestaña Hora: reloj vectorial (§5.4) en `by + pad + bh/12`; fecha larga si quedan ≥ 26 px (sección 0);
tarjetas `"Fecha"` (fecha corta) y `"Formato"` (`"24 h"`/`"12 h"`) si `ancho ≥ 250` y quedan ≥ 90 px (sección 1, alto
≤ 130, radio `uiPad`, `thCard()`); pie `"Reloj de FlexOS"` `TH_MUTE`. Pestaña Temporizador: `"Temporizador"` +
`"En construcción"`. En LVGL: `lv_obj` con `flex`/`grid` y *media queries* manuales (callback de `LV_EVENT_SIZE_CHANGED`)
que ocultan/muestran secciones con `lv_anim` de `opa` 130 ms.

### 8.5 Estados del ciclo de vida (`AppLife`, `Types.h:117`)

```
             enterApp (cerrada)               appTrFinishOpen
 CLOSED ───────────────────────────► RUNNING ◄────────────── RESUMING
   ▲                                  │   ▲                      ▲
   │ appTerminate (guardó o forzado)  │   │ enterApp (suspendida, con resume)
   │                                  ▼   │
   └──────────────────────────── SUSPENDED ─┘
                                    ▲
             appSuspend (Inicio, Recientes, otra app, atrás sin capas)
```
* Solo **una** app corre a la vez (`ALIFE_RUNNING`); las suspendidas conservan su estado lógico sin `tick()` ni toques.
* No se guarda un framebuffer por app: solo la miniatura de Recientes de 150×250 (como mucho 2, o 1 en modo eficiente).
* Sin tope fijo de apps abiertas: manda el **presupuesto de memoria medido**.

### 8.6 Navegación: los tres botones (`Core.h:551-575`) y salida al escritorio

* **Atrás** `sysBack()`: (1) `backLayer()` cierra una capa propia → fin; (2) `backScreen()` retrocede una pantalla → fin;
  (3) `appClose()` (suspende y vuelve a Inicio). Bloqueado en Modo Kiosco.
* **Inicio** `sysHome()`: en app → `appClose()`; hospedada en DeX → petición 1 (cerrar ventana); en otra pantalla →
  `enterHome()`.
* **Recientes** `sysRecents()`: fuerza vertical, suspende la app (o cancela una apertura pendiente), sale de pantalla
  completa, recompone el escritorio si está sucio, cancela transiciones y abre el gestor (`activarMultitarea`).
  Hospedada → petición 3.
* `appClose()` (`Core.h:635`): cierra la cortina; kiosco → nada; hospedada → petición 1; **resetea `gLand=false`** y el
  recorte (corrige el escritorio girado e irrecuperable); suspende (miniatura + guardado armado); `immersiveLeave()`;
  recompone `homeBuf` si está sucio; **cambia el estado lógico a Inicio en el acto** (`enterHomeState`) y lanza la
  animación de cierre como capa visual que no bloquea.
* `enterApp(id)` (`Core.h:683`): cierra la cortina; kiosco solo permite su app; hospedada → petición 2 (otra ventana);
  restablecimiento en curso → nada; Modo seguro filtra apps; **admisión de memoria** solo para apps cerradas (volver a una
  suspendida nunca se bloquea); carga de sesión perezosa; si estaba suspendida y tiene `resume` → `RESUMING`, si no
  `RUNNING`; estado lógico inmediato; `touchDropAll()`; lanza la transición de apertura. El `enter()/resume()` corre en el
  **último cuadro** de la apertura.
* `touchDropAll()` (`AppFramework.h:1179`): en **cada** cambio de pantalla corta el episodio táctil y traga el contacto
  hasta que el dedo se levante de verdad (evita el "toque fantasma" en la pantalla nueva).

Mensajes de memoria (exactos, vía `sysNotify(título, subtítulo)`, `Core.h:333-415`):
* Apertura denegada: título `"<App> no se abre ahora"`; subtítulo `"Memoria libre en trozos pequeños"`
  (fragmentación) / `"Memoria interna baja: se protege el sistema"` (SRAM) / `"Cierra una app o pulsa Optimizar Flex OS"`.
* Alivio automático insuficiente: `"Memoria crítica"` + `"Flex OS está protegiendo el sistema. Cierra una app."`, o
  `"Memoria casi llena"` + `"Flex OS liberó recursos en segundo plano."`.
* Secundarios: `"Memoria libre repartida en trozos pequeños"` / `"Las imágenes o apps pesadas pueden tardar más"`;
  `"Memoria interna del sistema baja"` / `"Se limitan cargas pesadas para proteger Wi-Fi y táctil"`;
  `"Almacenamiento interno en uso elevado"` / `"Limpiar la caché temporal puede ayudar"`;
  `"Almacenamiento interno casi lleno"` / `"Las actualizaciones y los datos nuevos podrían fallar"`.
* Orden de desalojo (`appEnforceMemoryBudget`, `Core.h:438`): 1) soltar cachés del sistema y `shed()` de suspendidas en
  orden LRU; 2) solo si sigue crítico, cerrar la suspendida menos reciente **y solo si su sesión se pudo guardar**; nunca la
  de primer plano ni una con `bgWork()`. Pesos por app `APP_WEIGHT` (`Core.h:176`): pesadas = Galería, Multimedia, Modo PC,
  Navegador, Cámara; medias = Paint, Juegos, Flex Store, Música; el resto ligeras.

### 8.7 Transiciones de app interrumpibles (`AppFramework.h:821-961`, motor en `Core.h:806-962`)

Principio: **estado lógico ≠ estado visual**. La intención del usuario cambia el estado en el acto; la animación es una
capa que se sigue dibujando sin mandar sobre nada. Como mucho dos capas (`gTrIn` entra creciendo, `gTrOut` sale
encogiendo), nunca dos apps renderizando. Cada intención incrementa `gTrGen`; una finalización de generación vieja se
descarta. Al re-dirigir se parte del **progreso visual actual** y la duración se escala con la distancia restante.

| Constante | Valor |
|---|---|
| `ATR_OPEN_MS` | 210 ms (icono → pantalla completa) |
| `ATR_CLOSE_MS` | 190 ms (pantalla → icono); si viene de un *flick* de la barra de gestos con velocidad `v` > 0.35 px/ms: `190 · max(0.35/v, 0.45)` |
| `ATR_MIN_MS` | 60 ms (suelo al re-dirigir) |
| `ATR_RAD_SMALL` / `ATR_RAD_FULL` | radio 26 px en el icono → 4 px a pantalla completa (interpolado) |
| `ATR_FADE_P` | 0.35: por debajo, la capa **saliente** se desvanece `255·p/0.35` |
| Curva | **ease-out cúbica** `1 − (1−u)³` sobre el tramo restante, reloj `micros()` (se saltan estados si un cuadro se retrasa) |

Estilos (`gAnimStyle`, NVS `"animstyle"`, 0 por defecto):
* **0 Zoom**: la tarjeta interpola desde el rectángulo del icono (`getIconRect`: casilla real de la rejilla en la página
  visible; dock: x = 40 + i·112, y = 640, 64 px) hasta 480×800.
* **1 Fundido**: siempre a pantalla completa, la opacidad **es** la animación (`255·p`).
* **2 Deslizar**: sube desde abajo, `y0 = 800·(1−p)`, sin radio.

La tarjeta es un **rectángulo redondeado del color de fondo de la app** (`TH_PAGE` o `WIN_BG`) sobre `homeBuf`, sin el
contenido de la app (el contenido se construye al terminar). Primer cuadro de cada intención: cuadro completo. Orden: saliente
debajo, entrante encima. Mientras haya capas, la transición es la **única** que dibuja (`appTrOwnsScreen`).
Una apertura abandonada antes de su `enter()` se **cancela** sin suspender nada (`appCancelPendingOpen`, `Core.h:526`).
Test: `testTransicionesApps` (`ino_compile.cpp:5289`). Diagnóstico opcional `FLEX_DIAG` (`[FLUIDEZ] …`, apagado).
En LVGL: un `lv_obj` "tarjeta" en una capa superior animado con `lv_anim` (x, y, w, h, radius, opa) y un *path* ease-out
cúbico personalizado; el `enter()` de la pantalla LVGL de la app se ejecuta en el `ready_cb`; mantener la cancelación por
generación.

### 8.8 Barra de navegación del sistema (modo "Botones", `gNavMode = 0`)

* Franja reservada `NAV_H = 64` (y 736..799), **la pinta y atiende el sistema** (se estampa en cada transferencia), nunca la
  app. Fondo `navBgCol`, línea superior 1 px `navLineCol`, glifos `navFgCol` (§2.4). Centros x = 80 / 240 / 400,
  `ny = 748` (glifos en `ny+8 = 756`).
* Visible si: modo Botones, en app, no hospedada, no `gLand`, no kiosco, la app no es `APP_LAND`; en pantalla completa
  inmersiva se sustituye por la barra transitoria.
* Toque (`navBarHandle`, `Core.h:578`): al presionar en la franja se elige el tercio (x<160 atrás, <320 inicio, resto
  recientes) y se muestra un destello `fillCircleA` r=24 α46 color `navFgCol`; sigue visible `NAV_PRESS_MS = 130` ms tras
  soltar; si el dedo sale de la barra antes de soltar **se cancela**; un *tap* cuyo apoyo no se vio se resuelve donde se
  apoyó; nada se filtra a la app.
* NVS: `"navmode"` int (0 Botones, 1 Gestos iOS), defecto 0.

### 8.9 Barra de gestos (modo "Gestos iOS", `gNavMode = 1`, `handleiOSGestures`, `Core.h:1063`)

| Constante | Valor |
|---|---|
| `GB_STRIP_H` | 44 px (franja inferior que vigila) |
| `GB_CLAIM_DY` | 12 px de recorrido hacia arriba para reclamar el toque (antes, la app sigue recibiendo sus eventos) |
| `GB_HOME_DY` | 30 px mínimo para reconocer Inicio |
| `GB_FLICK_VEL` | −0.35 px/ms (hacia arriba) |
| `GB_RECENTS_MS` | 300 ms: mantenido ≥ 300 ms → Recientes |
| `GB_VEL_TAU` | 40 ms (filtro de velocidad) |

*Flick* (recorrido > 30 y velocidad ≤ −0.35 dentro de 300 ms) → Inicio **con el dedo todavía apoyado**. Si no, al soltar:
recorrido > 30 → Recientes si duró ≥ 300 ms, Inicio si no. En kiosco se ignora (y la app no se congela). Se dibuja el
indicador de inicio (§6.3).

### 8.10 Pantalla completa inmersiva y barra transitoria (`AppFramework.h:98-122`, `:1039-1323`)

* Solo apps `APP_FLEX` no `APP_LAND`; la pide la app (`immersiveRequest(app, 0 normal | 1 vertical | 2 horizontal)`) o el
  menú del escritorio (`immersiveOpen`, última orientación `gImmPrefLand`). Se aplica en `appTick` **antes** del `tick` de
  la app, nunca a mitad de un cuadro (`immersiveApplyPending`): limpia el lienzo con `WIN_BG`, repone el marco si vuelve a
  normal, `touchDropAll()`.
* Lienzo: vertical 480×800; horizontal `gLand=true`, `gAppW=800`, `gAppH=480` (`immersiveLayout`).
* Es propiedad de la **sesión**: suspender y volver la conserva; cerrar de verdad la borra.
* Barra transitoria: oculta al entrar; deslizar desde los últimos `NAV_IMM_EDGE = 26` px del borde inferior **lógico** y
  recorrer `NAV_IMM_CLAIM_DY = 14` px la revela; 4 botones en `cw·(2i+1)/8`: atrás, inicio, recientes, **salir de pantalla
  completa**; se oculta sola a los `NAV_IMM_HIDE_MS = 3500` ms sin tocarla; se estampa sobre la página y fb recupera los
  píxeles al terminar (no ensucia la app). En horizontal ocupa las primeras 64 columnas físicas.
* Mapeo táctil en horizontal: `lx = py`, `ly = 479 − px`.

### 8.11 Manejo de horizontal (`gLand`)

* El motor gira 90°: lógico 800×480. Lo activan Modo PC (en `pcEnter`/`pcResume`), Juegos (`APP_LAND`, cada cuadro) y la
  pantalla completa horizontal. El **framework** lo devuelve a vertical al salir (`appClose`, `sysRecents`,
  `appTrFinishOpen`, `ensureBlurBg`, `drawWallpaperRowsId`); ninguna app debe tener que acordarse.
* En horizontal, el vidrio cae a tinte translúcido (§4.10) y no hay barra de navegación ni gestos (`appTick`, `Core.h:997`:
  `if(gLand){ tick; return; }`). Suspender en horizontal → **sin miniatura** en Recientes.
* `gClipLY0/gClipLY1` recortan la Y lógica en horizontal (lo usa el puente del navegador).

### 8.12 Persistencia de sesión de app (`AppFramework.h:1325-1438`, formato en `Session.h:55-104`)

* Archivos en LittleFS `/System/Sessions/<app>.bin` (cachés en `/System/Cache`). Existentes: `ajustes.bin` (v1:
  `{int16 view, sel, scroll, listScroll}`), `calc.bin` (v1), `media.bin` (v2), `notas.bin` (v3), `paint.bin` (v1).
* Cabecera `SessHdr {uint32 magic = 0x584C4631 ('1FLX'), uint16 ver, uint16 app, uint32 len, uint32 crc}` (16 B) + carga
  útil; total ≤ `SESS_IO_MAX = 1024` B (estado de UI, nunca el documento). CRC-32 reflejado `0xEDB88320` sin tabla.
* Escritura **atómica**: `<ruta>.tmp` → cerrar → releer y verificar longitud y CRC → renombrar. Lectura que no valida
  (magic, versión, app, longitud, CRC) → se ignora y la app abre en estado seguro.
* **Guardado diferido** (`sessMarkDirty` / `sessAutosaveTick`): tras `SESS_IDLE_MS = 1200` ms sin actividad o como tope
  `SESS_MAXWAIT_MS = 30000` ms; **nunca con el dedo apoyado** ni durante un restablecimiento; marca por app
  `gSessNeedSave` para no reescribir una sesión idéntica (desgaste de flash). Al suspender se arma el guardado para la
  vuelta siguiente (fuera de la animación).
* **Carga perezosa**: `loadSess()` la primera vez que se abre la app, no en el arranque.
* `appTerminate(id, force)`: guarda; si falla y no es forzado **no se cierra** (no se pierde trabajo del usuario); alimenta
  el watchdog alrededor de `close()` (el del navegador puede tardar hasta 3 s); olvida la huella medida.
* Sin sistema de archivos todo sigue funcionando (las sesiones quedan en RAM).

### 8.13 Notas de migración del framework

* **Puro y reutilizable** (con tests en `tests/host`): `uiPad/uiGap/uiFontFit/uiFontH/uiLineH`, `uiSection` (fundido por
  tiempo), `appTrP/appTrRect/appTrAlpha/appTrAim` (geometría y curvas de transición), `flxClampRect`, `crc32u`,
  `sessWrite/sessRead` (formato), lógica de admisión/desalojo (ya separada en `FlexOS_Mem.cpp`, `test_mem.cpp`),
  `handleiOSGestures` (umbrales como máquina de estados pura), `navImmBtnAt`, mapeos de coordenadas.
* **Dependiente de Arduino**: `millis/micros` → `esp_timer_get_time`; `heap_caps_*` igual en IDF; `Serial` → `ESP_LOG*`;
  LittleFS → `esp_littlefs`; NVS `Preferences` → `nvs_flash` con el **mismo espacio "flexos" y las mismas claves/tipos**
  (`Preferences::putBool` guarda `uint8`, `putInt` `int32`, `putBytes` blob).
* En LVGL cada app es una `lv_screen` (o un contenedor en la pantalla de apps) creada en `enter()`; suspender = ocultar y
  soltar recursos pesados manteniendo el estado lógico en structs de la app; reanudar = volver a mostrar sin recrear.
  El marco (barra de estado, barra de navegación) vive en `lv_layer_top()` para que ninguna app lo tape.
* Rarezas a no copiar: IDs literales en `themeChanged`/dock (§2.7); batería fija 82 %; indicador de inicio blanco sobre fondo
  claro; Ajustes no se reanuda con su detalle si el *Panel rápido* cambia el tema (por el bug de IDs).

---

## 9. Modo PC / DeX (`FlexOS_Ultra_DeX.h`, `FlexOS_Ultra_DeXDraw.h`, `FlexOS_Ultra_DeXInput.h`)

### 9.1 Propósito y cómo se llega

Escritorio **horizontal 800×480 en la propia pantalla** (clon del DeX de tabletas; no hay salida a monitor externo,
`DeX.h:30-51`), con ventanas reales de las apps de Flex OS, barra de tareas, cajón, buscador, panel de ajustes rápidos y
notificaciones, Recientes propio, menús contextuales y touchpad virtual.
* Entrar: icono **Modo PC** (id 4, en la rejilla de fábrica), control "Modo PC" del Panel rápido (`qpTapDex`). Es la app
  `IC_MODOPC` (`pcEnter`/`pcTick`, flags `APP_CUSTOM_HEADER`); pone `gLand = true`.
* Salir: solo desde dentro — menú de la barra de tareas → `"Salir de Modo PC"` o botón rojo `"Salir de Modo PC"` del panel
  de ajustes rápidos (`pcExit`, `DeXInput.h:554`). En horizontal no hay barra de navegación ni gestos del sistema.
  `pcExit` corta el tick (`dexExiting`), guarda el brillo pendiente, vuelve a vertical, cierra overlays, **libera los
  lienzos de las ventanas y el fondo** y llama a `appClose()` (lo que **suspende** Modo PC: la disposición se conserva).

### 9.2 Geometría (`DeX.h:53-87`)

| Constante | Valor | Uso |
|---|---|---|
| `DEX_TB_H` | 58 | Alto de la barra de tareas (y = 422 visible) |
| `DEX_TTL_H` | 38 | Barra de título de ventana |
| `DEX_BTN_W` | 38 | Ancho de cada control de ventana |
| `DEX_MIN_W × DEX_MIN_H` | 240 × 140 | Tamaño mínimo de ventana |
| `DEX_GRIP` | 22 | Agarre de redimensión hacia **fuera** (hacia dentro 6) |
| `DEX_ANIM_MS` | 180 | Animaciones de ventana y overlays |
| `DEX_TB_ANIM` | 150 | Deslizamiento de auto-ocultar |
| `DEX_LONG_MS` | 560 | Pulsación larga → menú (> 550 a propósito: nunca dispara además un tap) |
| `DEX_DTAP_MS` | 400 | Doble toque (radio 26 px) |
| `DEX_SNAP_EDGE` | 12 | Franja de borde que propone anclaje |
| `DEX_TB_IDLE_MS` | 1800 | Inactividad antes de ocultar la barra (auto-ocultar) |
| `DEX_TB_REVEAL` | 18 | Franja inferior que vuelve a mostrar la barra |
| `DEX_PAD_W × H` | 210 × 140 | Touchpad virtual |
| `DEX_DRW_W × H` | 672 × 404 | Cajón de apps |
| `DEX_FND_W × H` | 576 × 404 | Buscador tipo Finder |
| `DEX_NP_W` | 360 | Panel de ajustes rápidos/notificaciones |
| `DEX_KEEP` | 96 | Píxeles de ventana siempre agarrables |
| Área útil | alto `480 − 58 = 422` (o 480 con auto-ocultar) | `dexWorkBottom()` |

Paleta (alias del tema, sin colores propios, `DeX.h:89-105`): `DEX_ACCENT = TH_PRIM`, barra `TH_PAGE` (plano) /
`TH_GLASS` (vidrio), panel `TH_SURF`, panel elevado `TH_SURF2`, textos `TH_TXT`/`TH_TXT2`, borde `TH_BORDER`, marco
`TH_SURF`, cuerpo `TH_WIN`, título activo `TH_SURF2`, título inactivo `mix(TH_SURF, TH_PAGE, 110)`.

### 9.3 Fondo del escritorio DeX (`DeXDraw.h:58-110`)

Degradado vertical `a` (arriba) → `b` (abajo) + dos círculos suaves: (680, 70) r=150 color `mix(b, blanco, 18)` y
(90, 390) r=190 color `mix(a, blanco, 12)`. **Sin iconos sueltos** en el escritorio.

| Variante (`dexWall`) | Oscuro a → b | Claro a → b |
|---|---|---|
| 0 | `#0A1026` → `#1C407C` | `#96BAEC` → `#E2ECFA` |
| 1 | `#1C0C2C` → `#562460` | `#D6C4EE` → `#F6ECFC` |
| 2 | `#061E20` → `#104E54` | `#B0DED6` → `#E8F6F2` |

Se cachea en `dexBg` (768 KB) al entrar y al cambiar variante/apariencia; sin PSRAM se pinta procedural cada vez.
"Cambiar fondo" del menú del escritorio cicla 0→1→2. **No se persiste** (vuelve a 0 en cada sesión).

### 9.4 Ventanas

Modelo `PWin {open, x, y, w, h, app, mini, snap, rx, ry, rw, rh}` (`Types.h:52`), **máximo 4** (`pwins[4]`), orden Z
`dexOrder[4]` ([3] = frente), foco `dexFocus`. Anclajes `SNAP_FREE, SNAP_L, SNAP_R, SNAP_TL, SNAP_TR, SNAP_BL, SNAP_BR,
SNAP_MAX`; `rx..rh` = geometría a restaurar.

**Dibujo** (`dexDrawWindow`, `DeXDraw.h:739`): radio 14; sombra desplazada (3, 6) `TH_SHADOW` α100 activa / α62 inactiva
(`effShadow`); cuerpo `dexSurface(TH_SURF)`; barra de título de 38 px (`TH_SURF2` activa) con línea inferior `TH_BORDER`;
icono 20 px en (x+10, y+9); nombre tamaño 2 en (x+38, y+11) recortado a `w − 52 − 114` (`".."`); tres controles a la
derecha, cada uno 38×30 en `y+4`, `bx = x + w − (3−k)·38 − 4`: minimizar (barra 11×2), maximizar/restaurar (rect 11×9 o dos
rects 9×8 si está anclada), **cerrar** con fondo `TH_DANGER` α200 (α90 inactiva) r5 y aspa `TH_ONACC`. Área de cliente
`(x+1, y+38, w−2, h−39)`. Ventana inactiva: velo `TH_SCRIM` α34 sobre toda la ventana. Sin app hospedada (o Modo PC, que no
se hospeda a sí mismo): cuerpo `TH_WIN` con nombre de la app tamaño 4 en (x+20, y+16), `"Ventana de Samsung DeX"` tamaño
2, `"Arrastra la barra de titulo a un borde"` y `"para anclarla a media pantalla."` tamaño 1, e icono grande (≤ 96 px)
abajo a la derecha (cada línea solo si cabe en alto).

**Apertura** (`dexOpenFrom(app, sx, sy, ss)`, `DeX.h:563`): si ya existe → restaurar/traer al frente; si no hay hueco se
**recicla la más antigua** (`dexOrder[0]`, liberando su lienzo). Tamaño inicial con la **proporción de la app**
(`dexHostDefaultSize`: cliente de alto `workBottom − 38 − 21`, ancho proporcional, máx. 720; Modo PC 448×284);
posición `(66 + n·38, 30 + n·26)` acotada; arranca la app real (`dexHostOpen`); animación `DXA_OPEN` desde el rect de
origen (icono del cajón/buscador o de la barra) en 180 ms.
**Cerrar / minimizar / restaurar**: animación hacia/desde el icono de la app en la barra (`dexTbAppRect`).
**Maximizar** (`dexToggleMax`): anclada → vuelve a libre con `rx..rh`; libre → `SNAP_MAX`. **Cascada** (menú del
escritorio): todas libres con tamaño por defecto en `(66 + n·44, 26 + n·30)`.
**Saneado único** `dexClampWin` → `flxClampRect(..., 800, workBottom, mínimo, DEX_KEEP=96)`; con ventana libre el mínimo es
el de legibilidad de la app (`dexHostMinSize`: cliente de 330 px de alto para apps verticales y 240 para horizontales,
convertido a ventana, ≥ 240×140). `dexClampAll()` re-encaja todo al activar/desactivar el auto-ocultar.

**Animador** (`DeX.h:442-525`): una sola ranura; pedir otra ventana finaliza la anterior **en su estado final**; re-dirigir
la misma parte de su rect interpolado actual. Curva ease-out cúbica. Durante abrir/cerrar/minimizar se dibuja una versión
simplificada **sin contenido** (`dexDrawWinAnim`): alfa `40 + 215·f` (f = p al abrir/restaurar, 1−p al cerrar/minimizar),
sombra α`a·70/255`, barra de título escalada `38·h/260` (≥ 6), icono centrado (≤ 56) solo con α ≥ 210. `DXA_GEOM`
(anclar/maximizar) dibuja la ventana completa interpolada.

**Anclaje por arrastre** (`dexSnapHit`, `DeX.h:308`): puntero en el borde superior (≤ 12 px) → cuadrante superior si está a
≤ 170 px de una esquina, si no maximizar; borde izquierdo/derecho → cuadrante si está a ≤ 110 px de arriba/abajo, si no
mitad; borde inferior → cuadrante inferior en las esquinas. Mientras se arrastra se dibuja el **contorno fantasma**
(`dexDrawGhost`): relleno `mix(TH_PRIM, TH_SURF, 150)` α70 con inset 6 y r16 + dos bordes. Al soltar se aplica el anclaje
(guardando `rx..rh` si estaba libre). Arrastrar una ventana anclada la "despega": recupera `rw×rh` manteniendo la proporción
horizontal del dedo y la barra de título centrada bajo él.

### 9.5 Barra de tareas (`dexTbLayout`, `DeX.h:424`; `dexTaskbar`, `DeXDraw.h:793`)

* Fondo a todo el ancho: con vidrio `pcGlassPanel(TH_GLASS, α205)` + borde; plano `TH_PAGE`; línea superior `TH_BORDER`.
* Izquierda: **cajón** (chip 34×34 en x=12, y = barra+12) y **buscador** (x=56).
* Centro: grupo **fijadas + abiertas** (máx. 10): fijadas `DEX_PIN` = Navegador, Notas, Calculadora, Ajustes,
  Almacenamiento, Galería; luego las apps abiertas no fijadas. Icono 32 px (40 con "Iconos grandes"), paso `icono + 20`; el
  grupo se centra y, si no cabe entre `x=106` y el touchpad − 16, se estrecha el paso (y el icono, mín. 18). Abierta:
  resalte `DEX_ACCENT` α76 (con foco) / α34 en `(x−6, y−4, s+12, s+8)` r8 y raya inferior de 16 px (foco) u 8 px, 3 px de
  alto, en `y+s+6`.
* Derecha (de derecha a izquierda desde x=784): reloj (`clkStrBar` tamaño 2 alineado a la derecha en barra+12) y fecha
  corta (tamaño 1 `TH_TXT2` en barra+34); batería 30×15; Wi-Fi (R=11); engranaje 30×30; campana 30×30 (punto `TH_ERR` si
  hay notificaciones); touchpad 30×30.
* **Zonas táctiles** (`dexTbHit`, `DeXInput.h:137`): toda la altura de la barra; se asigna el control cuyo **centro** está
  más cerca en horizontal con alcance ±26 px (apps ±28); el cajón llega hasta el borde izquierdo; desde `reloj − 100` hasta
  el borde derecho es del reloj.
* Acciones: cajón/buscador abren o cierran su overlay; touchpad conmuta (cursor al centro: 400, 200); campana y engranaje
  abren/cierran el panel de ajustes rápidos; **reloj → Recientes**; app: no abierta → abrir desde su icono; minimizada →
  restaurar; con foco → **minimizar**; otra → traer al frente. Pulsación larga → menú de la barra.
* **Auto-ocultar** (`dexTbAuto`): tras 1800 ms sin interacción (sin overlay, menú ni arrastre) baja 58 px en 150 ms
  ease-out; reaparece si el puntero entra en los últimos 18 px o toca la barra. No se persiste.

### 9.6 Overlays

Todos se abren/cierran en 180 ms (`dexOvProg`, ease-out cúbica); mientras crecen solo se pinta la caja con fundido (escala
0.86→1, α `235·p`, sombra `effShadow(90·p)`), el contenido aparece al asentarse (con un cuadro extra de "pulso" para no
quedarse vacío). Un overlay captura todos los toques; tocar fuera lo cierra.

**Cajón de apps** (`dexDrawerDraw`, `DeXDraw.h:933`): pop-up centrado en el área útil 672×404 r20 con sombra (4, 8) α110;
campo de búsqueda (fx+18, fy+14, 636×36) placeholder `"Buscar aplicaciones"`; rejilla de 6 columnas (`(672−28)/6 = 107`),
filas de 76, icono 52 en `fy + 62 + fila·76`, nombre tamaño 1 debajo (recortado); máximo 18 resultados; vacío
`"Sin resultados"` tamaño 2. Teclado compacto en `fy + 292`. Toque en icono (zona `±8 / −6..+18`): cierra y abre la ventana
**creciendo desde ese icono**.

**Buscador tipo Finder** (`dexFinderDraw`, `:959`): 576×404, placeholder `"Buscar apps y ajustes"`; sección
`"Aplicaciones"` (tamaño 1, máx. 4 filas de 27 px, icono 20 + nombre tamaño 2) y sección `"Ajustes"` (máx. 3: chip 20×20
con punto de acento) sobre la lista `"Pantalla", "Sonido", "Red e Internet", "Bateria", "Aplicaciones", "Almacenamiento",
"Seguridad", "Acerca de"`; vacío `"Sin resultados"`. Tocar un ajuste abre la ventana de **Ajustes** (no la categoría
concreta). Búsqueda: subcadena sin distinguir mayúsculas, byte a byte (los acentos del nombre no casan con la consulta ASCII).

**Teclado compacto del buscador** (`dexKbDraw`, `:853`): 3 filas de 32 px con separación 5: `qwertyuiop` (10),
`asdfghjkl` (9), `zxcvbnm` + **espacio** (barra 13×2) + **borrar** (flecha); teclas `TH_SURF2` r6, letra tamaño 2. Consulta
máx. 19 caracteres. Botón limpiar (círculo r9 `TH_BORDER` con aspa) a la derecha del campo cuando hay texto.

**Panel de ajustes rápidos / notificaciones** (`dexNotifDraw`, `:1022`): 360 de ancho en x = 428, y = 10, alto
`workBottom − 22`, r18, se **despliega hacia abajo** (alto `nh·p`). Título `"Ajustes rapidos"` tamaño 2. Cuatro mosaicos
2×2 de 162×58 r12 (`DEX_ACCENT` encendido / `TH_SURF2`) con círculo indicador y etiqueta tamaño 1: `"Liquid Glass"`,
`"Modo oscuro"`, `"Barra auto"`, `"Touchpad"` (los dos primeros llaman a `themeChanged()`). `"Brillo"`: pista 336×26
redondeada, relleno `DEX_ACCENT` ≥ 26 px, valor `"N%"` tamaño 2; arrastre aplica PWM en el acto (mín. 5 %) y guarda NVS al
soltar (`dexBrightCommit`). `"Notificaciones"`: tarjetas reales 42 px r12 con punto de acento, nombre del módulo tamaño 2 y
`"Modulo detectado"`; vacío `"Sin notificaciones"`. Botón inferior `"Salir de Modo PC"` 34 px `TH_DANGER`/`TH_ONACC`.

**Recientes de DeX** (`dexRecentsDraw`, `:1088`): velo `TH_SCRIM` α`170·p` sobre el área útil; tarjetas 190×148 r14
separadas 18 y centradas (ventanas abiertas de delante a atrás): banda de título 24 px `TH_SURF2` con icono 16 y nombre
tamaño 1, icono grande 54 centrado, estado `"Minimizada"` / `"En ejecucion"`, aspa de cerrar arriba a la derecha (zona
26×24); vacío `"Sin ventanas abiertas"` tamaño 3. Pie `"Arrastra una tarjeta hacia arriba para cerrarla"` tamaño 1
`TH_TXT2`. Interacción: presionar fuera de las tarjetas cierra; arrastrar una tarjeta solo hacia arriba y soltar con
`dy < −45` cierra la ventana (y el overlay si era la última); tocar el aspa cierra; tocar la tarjeta restaura la ventana.

**Menús contextuales** (`dexMenuDraw`, `:1154`): 216 de ancho, filas de 30, alto `10 + n·30`, r12, sombra (3,5) α120,
etiquetas tamaño 2 en (x+14, fila+8); acotado a la pantalla con margen 6.

| Origen (pulsación larga) | Opciones (texto exacto) |
|---|---|
| Escritorio (tipo 0) | `"Cambiar fondo"`, `"Organizar en cascada"`, `"Recientes"`, `"Cajon de apps"` |
| Barra de tareas (tipo 1, centrado sobre el dedo, encima de la barra) | `"Barra: auto-ocultar ON"`/`"Barra: auto-ocultar OFF"`, `"Iconos grandes"`/`"Iconos normales"`, `"Recientes"`, `"Salir de Modo PC"` (en `TH_DANGER`) |
| Barra de título (tipo 2) | `"Minimizar"`, `"Maximizar"`/`"Restaurar"`, `"Anclar a la izquierda"`, `"Anclar a la derecha"`, `"Cerrar"` (en `TH_DANGER`) |

**Touchpad virtual** (`dexPadDraw`, `:1173`): 210×140 r14 abajo a la derecha (576, workBottom−154), `TH_SURF` α190 +
borde, divisor `TH_DIV` a 30 px del fondo, textos `"Touchpad"` y `"Toca = clic · Manten = menu"` tamaño 1. Movimiento
**relativo** del cursor con ganancia 1.45; tocar = clic; mantener 560 ms = clic derecho (menú); el pad no arrastra
ventanas. Cursor (§6.6) que cambia de forma: I-beam sobre el campo de búsqueda, manita sobre overlays, barra de tareas,
menús y barras de título, flecha en el resto. Con el pad activo el cursor también sigue al dedo directo.

### 9.7 Entrada (`dexPointer`, `dexInput`, `DeXInput.h:40-549`)

* Normalización: `lx = T.y`, `ly = 479 − T.x`; el puntero (`pX, pY, pDown, pPressed, pReleased, pTap, pLong, pDTap`) sale del
  dedo o del touchpad, y toda la lógica es idéntica en ambos casos.
* **Clic** = soltar sin haber arrastrado (`T.moved`, umbral 12 px del sistema) y sin pulsación larga, **sin límite de
  duración** (no se usa `T.tap` < 550 ms); el clic se atiende **donde empezó** el toque (el dedo rueda). Doble toque < 400 ms
  a < 26 px. Pulsación larga: 560 ms sin moverse.
* Prioridad: 1) menú abierto (lo come todo; fuera lo cierra) → 2) arrastre/redimensión en curso → 3) overlay activo →
  4) barra de tareas → 5) ventanas → 6) escritorio (larga → menú; toque con overlay → cerrarlo).
* Ventanas: al presionar, la máscara de redimensión (8 zonas, 22 px fuera / 6 dentro; arriba solo los primeros píxeles para
  no robar el arrastre del título) **pierde** frente a los controles de la barra de título; zona de arrastre = barra de
  título + 6 px por encima + 4 a los lados; presionar trae al frente. Arrastrar: posición acotada **en cada paso**;
  redimensionar: respeta 240×140 y re-encaja dentro del área útil. Toque en controles: zona de toque = toda la altura de la
  barra (38) y cerrar +4 px hasta el borde. Doble toque en el título → maximizar/restaurar. Larga en el título → menú.
  Área de cliente → se **traduce y entrega a la app hospedada** (§9.8).

### 9.8 Hospedaje de apps reales en ventanas (`DeXDraw.h:194-723`)

* Cada ventana tiene su **lienzo** 480×800 (`DexHost.surf`, 768 KB) donde la app corre **igual que a pantalla completa**:
  `gRtTarget` desvía `setBuf(fb)`/`flxFlush` al lienzo (no tocan el panel). Un lienzo por ventana porque las apps pintan de
  forma incremental.
* `dexHostRun(i, doEnter, doTick, inject)` (`:336`): guarda y restaura **todo** el estado global (búfer, `gLand`, recortes,
  `gAppId`, `gState`, `gAppW/H`, `T`); fija `gHosted = true`, el lienzo lógico de la ventana y un toque **neutro** si no hay
  inyección (el arrastre del borde nunca llega a la app). Restaurar `gState` descarta la navegación a pantallas completas
  del sistema que intente la app. Una app no puede hospedar a otra (`dexHostBusy`). Modo PC no es hospedable.
* Peticiones de la app hospedada (`gHostReq`): 1 cerrar → cierra la ventana; 2 abrir app → otra ventana desde su icono de la
  barra; 3 Recientes → Recientes de DeX. Se atienden fuera de la pila de la app (`dexHostServe`).
* **Encaje** (`dexHostFit`, `:256`) — lógica pura, única fuente de verdad de dibujo y toque:
  * App `APP_FLEX`: lienzo **del tamaño real** del área de cliente (horizontal si la ventana es apaisada, hasta 800×480;
    vertical hasta 480×800), escala 1:1, sin barras; la app se re-maqueta (`gRelayout`) al cambiar de tamaño, como mucho
    cada `DEX_RELAYOUT_MS = 45` ms y siempre una vez al estabilizarse (`dexHostRelayout`); la parte nueva del lienzo se
    rellena con `TH_WIN` para no ver negro (`dexHostExpose`).
  * App no adaptativa: lienzo fijo 480×800 (u 800×480 si `APP_LAND`) escalado **conservando proporción** (letterbox
    centrado con barras `TH_WIN`), pasos en coma fija 16.16 (`dexStep`, con recorte del último índice). Escalado cacheado
    en orden de volcado; con reducción > 1.5× promedia 2×2 muestras, si no vecino más cercano; se recalcula solo cuando la
    app dibuja o cambia el tamaño.
* **Toque** (`dexHostMapT`/`dexHostTouch`, `:605`): inversa exacta del encaje; si cae en las barras del letterbox no se
  entrega. El evento se construye en una variable local con `startX/startY` también traducidos (bug histórico: escribir en la
  `T` global dejaba `T.moved` siempre a true y ninguna app respondía salvo Paint). Si el dedo apoyado sale del área de la
  ventana se le entrega **una** suelta en su último punto (`dexHostTickIdle`).
* **Tick continuo** de todas las ventanas visibles en cada vuelta (contenido en tiempo real: red, reloj, reproductor), sin
  repetir el tick de la ventana que ya recibió su toque en esa vuelta.
* Tests de referencia: `testDexTiempoReal` (`:12875`), `testDexRedimensionado` (`:13104`), `testDexToqueBotones` (`:13284`).

### 9.9 Composición y ritmo (`dexCompose`/`dexPaint`, `DeXDraw.h:1233-1273`; `pcTick`, `DeXInput.h:666`)

Orden de capas: fondo → ventanas de atrás a delante (animada o completa; minimizadas no) → fantasma de anclaje (si se
arrastra) → Recientes (debajo de la barra) → barra de tareas → cajón/buscador/panel → menú → touchpad + cursor.
Banda sucia en **X lógica** (= filas físicas): arrastrar una ventana solo recompone su franja de columnas ±10 px; marcar todo
en cambios globales. Techo de **~33 fps** (un repintado cada ≥ 30 ms), no bloqueante. Cambio de minuto → repintado completo.
`pcTick`: animaciones → overlays → barra → maquetación de la barra → entrada → re-maquetado de ventanas → tick de ventanas →
pintar.

### 9.10 Ciclo de vida de Modo PC (`DeXInput.h:579-664`)

* `pcEnter`: reinicia todo (4 ventanas cerradas, sin foco, overlays/menú/pad apagados, cursor centrado), `gLand = true`,
  cachea el fondo, abre la **ventana de bienvenida** (`dexOpen(IC_MODOPC)`, panel informativo) y pinta.
* `pcSuspend`: conserva **la disposición** (PWin, orden, foco) y suelta lo pesado: lienzos de las ventanas (768 KB cada
  uno + escalados) y fondo (768 KB); ninguna animación sobrevive; `gLand = false`.
* `pcResume`: vuelve a horizontal, reconstruye fondo y barra, y **re-abre los lienzos de delante a atrás mientras quede
  memoria** (`memFreePsram() ≥ FLEXMEM_CRIT_BYTES + 768 KB`); las apps hospedadas vuelven a ejecutar `enter()` (su estado
  interno se reinicia).
* `pcCloseApp` (cerrar la tarjeta de Recientes del sistema): además borra la disposición.
* `themeChanged` no repinta Modo PC: lo hace su propio bucle al ver `dexBgWall = 0xFF`.

### 9.11 Persistencia de Modo PC

**Nada** de DeX se guarda en NVS ni en disco: variante de fondo, auto-ocultar, iconos grandes, touchpad y disposición se
pierden al reiniciar (la disposición sí sobrevive a suspender/reanudar en la misma sesión). Lo único persistente que toca
DeX son `"glass"`, `"dark"` (vía `themeChanged`) y `"bright"` (vía `cfgSavePrefs`).

### 9.12 Notas de migración de Modo PC

* **Lógica pura reutilizable** (portar a C y testear en host): `dexSnapRect`, `dexSnapHit`, `dexClampWin`/`flxClampRect`,
  `dexHostFit`, `dexStep`, `dexHostMapT`, `dexHostMinSize`, `dexHostDefaultSize`, `dexTbItems`, `dexTbGroup`,
  `dexTbItemRect`, `dexTbHit`, `dexResizeMask`, `dexMatch`/`dexFilterApps`, máquina del puntero (`dexPointer`: clic, larga,
  doble), animador (`dexAP`, `dexAnimCur`, finalización con estado final), `dexMenuLabel`/`dexMenuCount`.
* **Arquitectura LVGL propuesta**: una pantalla `lv_screen` horizontal (display rotado 90° o contenedor de 800×480 con
  transformación) con: fondo `lv_image`, cada ventana como `lv_obj` arrastrable (marco + barra de título + cliente),
  barra de tareas en un contenedor flex, overlays en la capa superior, cursor como `lv_image` en `lv_layer_sys()`.
* **El hospedaje de apps es el punto más delicado**: en Arduino cada app pinta en su propio framebuffer. En LVGL lo natural
  es que **cada app construya su UI dentro de un contenedor padre que se le pasa** (a pantalla completa: la pantalla de la
  app; en DeX: el área de cliente de la ventana) y maquete contra el tamaño de ese padre (`LV_EVENT_SIZE_CHANGED`). Las
  apps no adaptativas pueden mostrarse escaladas con `transform_scale` del contenedor (coste de CPU) o renderizarse en un
  `lv_canvas`/`lv_snapshot` escalado. Hay que diseñar desde el principio la API de apps "con contenedor" para no repetir el
  truco de `gRtTarget`.
* **Rotación**: confirmar en LVGL 9.6 + DPI el modo de render compatible con rotación por software (la rotación por
  software de LVGL está pensada para el modo PARTIAL; con DIRECT puede requerir PPA o un búfer intermedio). Es un riesgo
  de rendimiento (ver §11).
* Rarezas a no copiar: textos sin tildes (`"Cajon de apps"`, `"Ajustes rapidos"`, `"Modulo detectado"`, `"En ejecucion"`,
  `"Bateria"`, `"Manten"`, `"titulo"`) — corregirlos; el texto `"Ventana de Samsung DeX"` nombra una marca ajena — usar
  `"Ventana de Modo PC"`; el Finder abre Ajustes genérico en vez de la categoría; el menú del título solo ofrece mitades
  (no cuadrantes); las preferencias de DeX no se guardan; reiniciar el estado interno de las apps hospedadas al reanudar.

---

## 10. Persistencia consolidada de esta área

### 10.1 NVS, espacio `"flexos"`

| Clave | Tipo (Preferences) | Defecto | Variable | Escribe | Ref |
|---|---|---|---|---|---|
| `"dark"` | bool | `true` | `gDark` | `cfgSavePrefs` | `Prefs.h:263/303` |
| `"glass"` | bool | `false` | `uiGlass` | `cfgSavePrefs` | `Prefs.h:256/301` |
| `"glasslv"` | int | `50` (acotado 0..100) | `gGlassLvl` | `cfgSavePrefs` | `Prefs.h:260/302` |
| `"iconstyle"` | int | `0` | `gIconStyle` | `cfgSavePrefs` | `Prefs.h:264/304` |
| `"animstyle"` | int | `0` (0 zoom, 1 fundido, 2 deslizar) | `gAnimStyle` | `cfgSavePrefs` | `Prefs.h:290/308` |
| `"navmode"` | int | `0` (0 botones, 1 gestos) | `gNavMode` | `cfgSavePrefs` | `Prefs.h:289/307` |
| `"bright"` | int | `80` | `gBright` | `cfgSavePrefs` | `Prefs.h:265/305` |
| `"lang"` | int | `0` (ES; 1 EN, 2 FR, 3 PT, 4 IT, 5 ZH→EN) | `cfgLang` | `cfgSavePrefs` | `Prefs.h:254` |
| `"h24"` | bool | `false` | `g24h` | `cfgSavePrefs` | `Prefs.h:255` |
| `"wallh"` | int | `0` | `gWallHome` | `homeCfgSave` | `Home.h:1575` |
| `"walll"` | int | `0` | `gWallLock` | `homeCfgSave` | `Home.h:1576` |
| `"wallfit"` | int | `0` | `gWallFit` | `homeCfgSave` | `Home.h:1577` |
| `"wallpal"` | bool | `false` | `gWallPalOn` | `homeCfgSave` | `Home.h:1578` |
| `"hlook"` | int | `0` | `gHomeLook` | `homeCfgSave` | `Home.h:1579` |
| `"wallpb"` | bytes[80] | vacío | `gWallPath` | `homeCfgSave` | `Home.h:1583` |
| `"appver"`, `"appn"`, `"appfav"`, `"apphide"` | int | — | versión del registro y máscaras de apps | `homeOrderSave` | `Home.h:1562-1565` |

`cfgSavePrefs` reescribe **todas** sus claves de una vez (decenas de ms de flash): por eso el brillo de DeX y la intensidad
del Panel rápido se guardan **al soltar**, nunca durante el arrastre.

### 10.2 LittleFS

`/System/Sessions/{ajustes,calc,media,notas,paint}.bin` (formato §8.12), `/System/Cache/` (cachés borrables por Optimizar y
Modo seguro). Imagen de fondo: ruta arbitraria elegida por el usuario (JPEG baseline ≤ 512 KB).

---

## 11. Riesgos principales de migración (resumen)

1. **Liquid Glass en LVGL**: el material real (blur del fondo + tinte adaptativo por luminancia + especular/sombreado
   por filas + borde direccional) no es un estilo nativo. Riesgo de rendimiento (blur por cuadro) y de fidelidad (borde con
   peso distinto por lado, tinte adaptativo). Mitigación: backdrop desenfocado **cacheado** por fondo/tema/intensidad,
   panel = recorte de ese backdrop + capa de tinte con gradiente de 3 paradas, snapshot + blur una vez para overlays sobre
   contenido; nunca vidrio sobre vidrio en animaciones; tinte adaptativo calculado una vez por panel con la función pura.
2. **Horizontal / Modo PC y hospedaje de apps**: el original gira todo el motor (`gLand`) y ejecuta cada app en un
   framebuffer propio que luego escala. En LVGL hay que (a) resolver la rotación con DPI en modo DIRECT (posible PPA o modo
   PARTIAL) y (b) diseñar la API de apps para construirse dentro de un contenedor de tamaño variable (ventana DeX, pantalla
   completa vertical u horizontal). Es un cambio de arquitectura que conviene fijar **antes** de portar las apps.
3. **Tipografía y medidas**: la `y` de texto del original es el tope de mayúsculas y los tamaños son una escala lineal de
   un maestro de 40 px de em. Hay que generar fuentes Outfit a 13/19/25/31/38 px (+ una pequeña ~10-11 px o pixel 5×7) con
   el mismo repertorio Latin-1 reducido, aplicar el desfase vertical y conservar el plegado UTF-8 (sanear cadenas externas
   antes de `lv_label`). Sin esto las pantallas "se mueven" varios píxeles y aparecen cuadrados en notificaciones.
4. **Bugs de IDs literales** (`themeChanged`, dock, `getIconRect`) y datos inventados (batería 82 %): no copiar; usar
   `IC_*`, listas explícitas y datos reales.
5. **Memoria**: el original vive de soltar y rehacer cachés grandes (768 KB cada una) con mediciones reales. En LVGL los
   `lv_draw_buf` de backdrops, miniaturas, fondos y lienzos deben tener un **dueño** y un `shed()` equivalente, y el modo
   visual eficiente (blur ≤ 2, sombras a la mitad, 1 miniatura) debe existir.

---

## 12. Checklist de funcionalidades del área

| Funcionalidad | Archivo | Detalle clave |
|---|---|---|
| Paleta semántica oscura (27 tokens) | `FlexOS_Ultra_Theme.h:114` | `TH_PAGE #12141C`, `TH_PRIM #3C6EEB`… (§2.2) |
| Paleta semántica clara (27 tokens) | `FlexOS_Ultra_Theme.h:146` | `TH_PAGE #F4F7FB`, `TH_PRIM #2D5FE1`… |
| Colores sobre wallpaper `TH_ONWALL*`, `TH_WALL*` | `FlexOS_Ultra_Theme.h:218` | Iguales en ambas apariencias |
| Apariencia `gDark` | `FlexOS_Ultra_Theme.h:50` | NVS `"dark"` bool, defecto true |
| Material `uiGlass` | `FlexOS_Ultra_Theme.h:41` | NVS `"glass"` bool, defecto false; ortogonal a `gDark` |
| Estilo de icono `gIconStyle` | `FlexOS_Ultra_Theme.h:51` | NVS `"iconstyle"`; 0 Plano, 1 Vidrio |
| Acento `wallAccent()` / `onColor()` | `Theme.h:228`, `Wallpaper.h:468` | Paleta del fondo o `TH_PRIM`; texto por luma > 140 |
| Temas integrados (8 looks) | `FlexOS_Ultra_Wallpaper.h:504`, `HomeCfg.h:401` | NVS `"hlook"`; aplicación atómica con vuelta atrás; bug del acento muerto |
| Propagación de tema `themeChanged` | `FlexOS_Ultra_System.h:417` | NVS + invalidar cachés sin liberar + repintar; bug IDs v1 |
| 8 fondos procedurales | `FlexOS_Ultra_Wallpaper.h:206-272` | Recetas exactas §3.2; fondo 0 bit a bit histórico |
| Fondo desde JPEG | `FlexOS_Ultra_Wallpaper.h:368` | ≤ 512 KB, baseline, encuadre rellenar/ajustar/centrar, 8 mensajes de error |
| Paleta extraída del fondo | `FlexOS_Ultra_Wallpaper.h:469` | Rejilla 24×40, máxima saturación, aclarado si luma < 70 |
| Fondo velado `blurBg` | `FlexOS_Ultra_Theme.h:1046` | Wallpaper + `#080A12` α70 (sin blur real) |
| NVS de fondos | `FlexOS_Ultra_Home.h:1573` | `"wallh"`, `"walll"`, `"wallfit"`, `"wallpal"`, `"hlook"`, `"wallpb"` |
| Liquid Glass base | `FlexOS_Ultra_Theme.h:576` | Blur caja R, tinte adaptativo, especular 45 %/sombra 55 %, borde 156/104 |
| Intensidad del vidrio | `FlexOS_Ultra_Theme.h:426` | NVS `"glasslv"`, pasos de 5, tabla §4.3 |
| Suelo de tinte `gGlMinMix` | `FlexOS_Ultra_Theme.h:445` | 150 para banner y visor |
| Modo visual eficiente | `FlexOS_Ultra_Theme.h:666`, `Core.h:156`, `System.h:381` | Temporal: blur ≤ 2, sombras ½, 1 miniatura |
| Backdrop del escritorio + grabación de paneles | `Theme.h:486-574`, `Home.h:830-886` | Franja 72..595 desenfocada; 48 paneles por página |
| Tarjeta de vidrio cacheada / filas planas | `FlexOS_Ultra_Theme.h:714-821` | `GLC_MAX_H = 96`; vidrio durante el scroll |
| Banda pre-desenfocada para animaciones | `FlexOS_Ultra_Theme.h:883-984` | ≤ 440 filas, opacidad de material `a` |
| Superficie única `uiSurfaceA` (roles CARD/ELEVATED/ACCENT) | `FlexOS_Ultra_Theme.h:854-1027` | Tabla de decisión §4.8; `uiWallSurface` |
| Sombras (`effShadow`) | `Theme.h:680`, `QuickPanelGlass.h:159`, `DeXDraw.h:742` | Recetas por componente; ½ en eficiente |
| Material del Panel rápido (capa 1/4) | `FlexOS_Ultra_QuickPanelGlass.h:741` | 120×200, blur 1-3, velo 136..184 (255 plano), tinte `TH_GLASS2` 40 |
| Superficies del Panel rápido `qpGlassSurface` | `FlexOS_Ultra_QuickPanelGlass.h:52` | Mezclas 128/112/178/168; luz acotada 70/90 px |
| Cabecera del Panel rápido | `FlexOS_Ultra_QuickPanelGlass.h:202` | Hora tamaño 6, fecha, red real, 3 botones r22 |
| Módulos, deslizadores, tarjeta de círculos | `FlexOS_Ultra_QuickPanelGlass.h:237-385` | Medidas §4.9.3 |
| Editor y catálogo del Panel rápido | `FlexOS_Ultra_QuickPanelGlass.h:401-564` | Textos exactos "Editar panel", "Añadir un control"… |
| Gestos de la cortina | `FlexOS_Ultra_QuickPanelGlass.h:1446` | 1:1, flick 0.45 px/ms, 40 %, anim 150+dist·150/800 ms in-out cúbica |
| Deslizador de intensidad (commit al soltar) | `FlexOS_Ultra_QuickPanelGlass.h:1179` | Guardado NVS diferido; reset a 50 |
| Vidrio de DeX `pcGlassPanel` | `FlexOS_Ultra_DeXDraw.h:37` | Tinte α205 + borde, sin blur |
| Fuente Outfit 4bpp + 5×7 | `FlexOS_Ultra_Font.h`, `FlexOS_Ultra_Text.h` | Maestro 40 px em; tamaños 1..6 (§5.1) |
| Decodificación/plegado UTF-8 | `FlexOS_Ultra_Font.h:1688-1750` | Invisibles, espacios, comillas, guiones, viñetas |
| Ajuste de texto | `Font.h:1893`, `AppSettings.h:111`, `DeX.h:252` | "...", recorte, ".." |
| Reloj vectorial grande | `FlexOS_Ultra_Font.h:2031-2124` | Dígitos trazados; bloqueo capH 140 grosor 18 |
| 19 iconos de app | `FlexOS_Ultra_Icons.h:88-333` | Colores de marca, radio 22 %, Plano/Vidrio; nunca salen de su caja |
| Iconos de estado Wi-Fi/batería | `FlexOS_Ultra_Icons.h:336` | Batería fija 82 % (rareza) |
| Glifos de navegación, cabecera, Panel rápido, Ajustes, DeX | `AppFramework.h:1020-1101`, `QuickPanel.h:296-511`, `AppSettings.h:151`, `DeXDraw.h:113` | Inventario §6.3-6.6 |
| Cabecera compartida `uiHdr*` | `FlexOS_Ultra_AppFramework.h:403-443` | Zonas 56×56, banda 76 |
| Marco de app (barra de estado + cabecera) | `FlexOS_Ultra_AppFramework.h:237-272` | Borrado 0..46, título tamaño 3 en y=53 |
| Kit adaptativo `uiBox/uiPad/uiFontFit/uiSection` | `FlexOS_Ultra_AppFramework.h:287-376` | Fundido 130 ms |
| Recorte de viewport con scroll | `FlexOS_Ultra_AppFramework.h:467` | Recorte exclusivo |
| Placeholder y app Reloj (pestañas) | `FlexOS_Ultra_AppFramework.h:480-631` | Textos exactos §7.9, §8.4 |
| Registro `APP_REG` y banderas | `FlexOS_Ultra_AppFramework.h:64-794` | 19 apps, 6 banderas, hooks §8.2 |
| Ciclo de vida (CLOSED/RUNNING/SUSPENDED/RESUMING) | `Types.h:117`, `Core.h:494-718` | Suspender no reinicia; admisión de memoria |
| Botones atrás/inicio/recientes | `FlexOS_Ultra_Core.h:551-575` | Capa → pantalla → Inicio |
| Barra de navegación (Botones) | `FlexOS_Ultra_AppFramework.h:979-1037`, `Core.h:578` | 64 px, tercios, destello 130 ms |
| Barra de gestos (iOS) | `FlexOS_Ultra_Core.h:1063` | 44/12/30 px, −0.35 px/ms, 300 ms |
| Transiciones interrumpibles (3 estilos) | `AppFramework.h:866-961`, `Core.h:806-962` | 210/190/60 ms, radio 26→4, generaciones |
| Pantalla completa inmersiva + barra transitoria | `FlexOS_Ultra_AppFramework.h:98-1323` | Borde 26, reclamo 14, oculta a 3500 ms, 4 botones |
| Manejo de horizontal `gLand` | `Gfx.h:346`, `Core.h:635`, `AppFramework.h:1198` | El framework vuelve a vertical |
| Sesiones de app (formato atómico, diferido, perezoso) | `AppFramework.h:1346-1438`, `Session.h:55-104` | `'1FLX'`, ≤ 1024 B, 1200 ms / 30 s |
| Desalojo por memoria y mensajes | `FlexOS_Ultra_Core.h:224-460` | LRU, solo si la sesión se guardó; textos exactos |
| `touchDropAll` en cada cambio de pantalla | `FlexOS_Ultra_AppFramework.h:1179` | Sin toques fantasma |
| Modo PC: entrada/salida y ventana de bienvenida | `FlexOS_Ultra_DeXInput.h:554-602` | Solo se sale desde el menú de la barra o el panel |
| Fondo DeX (3 variantes × 2 apariencias) | `FlexOS_Ultra_DeXDraw.h:58` | Cacheado 768 KB, no persistido |
| Ventanas DeX: marco, controles, foco, Z, máx. 4 | `FlexOS_Ultra_DeX.h:112-612`, `DeXDraw.h:739` | r14, título 38, botones 38×30 |
| Anclaje (mitades, cuadrantes, maximizar) + fantasma | `FlexOS_Ultra_DeX.h:295-330`, `DeXDraw.h:776` | Bordes 12 px, esquinas 170/110 |
| Animador de ventanas interrumpible | `FlexOS_Ultra_DeX.h:442-525` | 180 ms ease-out, alfa 40..255 |
| Barra de tareas: layout, fijadas, hit-test, auto-ocultar | `FlexOS_Ultra_DeX.h:374-440`, `DeXInput.h:137` | 58 px, alcance ±26, 1800/150 ms |
| Cajón de apps DeX | `FlexOS_Ultra_DeXDraw.h:927`, `DeXInput.h:238` | 672×404, 6 columnas, teclado |
| Buscador tipo Finder | `FlexOS_Ultra_DeXDraw.h:953`, `DeXInput.h:258` | Apps (4) + ajustes (3) |
| Teclado compacto del buscador | `FlexOS_Ultra_DeXDraw.h:843-901` | 3 filas, espacio, borrar, limpiar |
| Panel de ajustes rápidos/notificaciones DeX | `FlexOS_Ultra_DeXDraw.h:988`, `DeXInput.h:291` | 4 mosaicos, brillo real, salir |
| Recientes DeX | `FlexOS_Ultra_DeXDraw.h:1070`, `DeXInput.h:318` | Tarjetas 190×148, arrastrar arriba > 45 px |
| Menús contextuales DeX (3 tipos) | `FlexOS_Ultra_DeXDraw.h:1122`, `DeXInput.h:207` | Textos exactos §9.6 |
| Touchpad virtual y cursor | `FlexOS_Ultra_DeXDraw.h:1168-1228`, `DeXInput.h:46` | Ganancia 1.45, 3 formas de cursor |
| Entrada DeX (clic, larga, doble, prioridad) | `FlexOS_Ultra_DeXInput.h:40-549` | 560 ms, 400 ms/26 px, clic donde empezó |
| Hospedaje de apps en ventanas | `FlexOS_Ultra_DeXDraw.h:194-723` | Lienzo por ventana, encaje FLEX 1:1 / letterbox, toque inverso exacto |
| Composición por banda y ~33 fps | `FlexOS_Ultra_DeXDraw.h:1233`, `DeXInput.h:666` | Banda en X lógica, ≥ 30 ms |
| Ciclo de vida de DeX | `FlexOS_Ultra_DeXInput.h:625-664` | Conserva disposición, suelta lienzos, re-abre según memoria |
