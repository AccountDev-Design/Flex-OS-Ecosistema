# Escritorio: páginas, widgets y Liquid Glass

Cómo se componen y se deslizan las páginas del Inicio de Flex OS Ultra (P4,
480×800), por qué el vidrio ya no arrastra el fondo, cómo pertenecen los widgets
a una página y cómo funciona la intensidad de Liquid Glass del Panel rápido.

Código: `FlexOS_Ultra_Theme.h` (material), `FlexOS_Ultra_Home.h` (páginas,
deslizamiento, modo edición, persistencia), `FlexOS_Ultra_Widgets.h` (modelo y
validación de widgets), `FlexOS_Ultra_QuickPanel*.h` (control de intensidad).
Pruebas: `tests/host/ino_compile.cpp` → `testVidrioSinArrastre`,
`testWidgetsDePagina`, `testIntensidadVidrio`, `testDeslizarPaginas`.

## 1. El fondo "pegado" al cristal

**Causa.** El deslizamiento entre páginas deja el wallpaper fijo y mueve como
primer plano todo píxel de la página que difiere del wallpaper limpio. Un panel
de vidrio (widget, icono de estilo Vidrio) es un desenfoque del wallpaper que
tenía debajo *cuando se compuso*: difiere entero del fondo limpio, así que
viajaba con la copia del fondo de su posición original dentro. El cristal
arrastraba el wallpaper.

**Arreglo.**

- `homeBackdropEnsure()` guarda, una vez por fondo y por radio de desenfoque,
  la franja de página del wallpaper **limpio** (`hpBg`) y **ya desenfocado**
  (`hgBd`), en coordenadas de pantalla.
- Mientras se compone una página, `homeGlassBegin()` activa ese backdrop:
  `drawLiquidGlassPanelEx()` no copia ni desenfoca, lee `hgBd` en la posición
  del panel (`glassFromBackdrop()`) y anota el panel.
- Al empezar el gesto se calcula una **máscara de contenido** por página
  (`hpMaskBuild()`: la página contra su base = fondo limpio + sus paneles).
- Cada cuadro (`hpRenderFrame()`): fondo limpio sin desplazar → paneles de vidrio
  de las dos páginas en su posición **actual**, leyendo el backdrop ahí →
  contenido desplazado solo donde marca la máscara.
- La página quieta y el último cuadro del gesto salen de la misma función con el
  mismo fondo: al soltar no hay salto (lo comprueba la prueba píxel a píxel).

Sin PSRAM para el backdrop, el vidrio vuelve al desenfoque por panel y el
deslizamiento se comporta como antes: se degrada, no se rompe.

Otras superficies revisadas: la cortina del Panel rápido publica la fila *j* de
su vidrio en la fila *j* (coherente); el desbloqueo desliza una capa entera con
su propio fondo; el menú contextual y la tarjeta del cronómetro usan una banda
pre-desenfocada sobre un fondo quieto; la isla de notificaciones recompone su
vidrio en cada cuadro; el icono arrastrado en Modo Edición se desenfoca en su
posición real.

## 2. Widgets de página

- La franja donde vivían **Clima y Calendario fijos** es ahora la **fila 0
  (cabecera)** de la rejilla de widgets de cada página (y=72, 120 px). Filas
  1.. = filas de iconos, con la geometría de siempre.
- Todo lo de una página (cabecera + rejilla) forma la **franja de página**
  (`HOME_PAGE_TOP`..`HOME_BAND_BOT_MAX`) y viaja con ella. Barra de estado, dock
  y barra de navegación son comunes y no se mueven. El gesto solo recorre la
  cabecera si alguna de las dos páginas tiene algo en ella.
- **Validación espacial única** (`homeWgPlaceOk`): tamaño dentro de los límites
  del tipo (`WG_REG`: minW..maxW, minH..maxH), alto real suficiente para su
  contenido (`minPxH`: calendario 110 px, clima compacto 116 px), dentro de la
  rejilla y sin pisar iconos ni widgets.
- **Modo Edición:** tocar un widget lo selecciona y muestra su asa de tamaño
  (esquina inferior derecha, zona táctil de 44 px); arrastrarla redimensiona por
  celdas. Sostener un widget (o un icono) 700 ms contra un borde lo lleva a la
  página vecina.
- **Regla determinista al cambiar de página** (`homeWgToPage`): mismo sitio y
  tamaño → primer hueco con su tamaño → primer hueco con el tamaño mínimo → si
  nada cabe (o la página ya tiene `HOME_WG_MAX` = 6), no se mueve y se avisa.
  Nunca pisa ni desplaza lo que hay.
- **Normalización:** un widget que ya no cabe (cambio de rejilla, datos
  corruptos, solape) se recoloca con la misma regla antes de retirarse.

### Persistencia

| Clave NVS | Contenido |
|---|---|
| `hwg2` | widgets v2: 6 por página, filas con cabecera (157 B) |
| `hwg`  | widgets v1 (congelada): solo se lee para migrar; se conserva para poder bajar de versión |

Migración al primer arranque: los widgets v1 bajan una fila y Clima y Calendario
pasan a la cabecera de la página **principal**, en el mismo sitio en que se
veían. Se escribe `hwg2` una sola vez.

## 3. Intensidad de Liquid Glass

- Un nivel 0..100 en pasos de 5 (`gGlassLvl`, NVS `glasslv`). `glassLevelApply()`
  deriva los parámetros **reales** del renderer: radio del desenfoque (2..10),
  translucidez/tinte (46..70 → 30..50 sutil, 62..90 intenso), especular y
  sombreado, y peso del borde. No hay distorsión ni refracción que ofrecer (el
  material avanzado se retiró), así que no hay control para eso.
- **Nivel 50 = material de siempre, bit a bit** (la prueba compara 36 paneles con
  una copia literal del código anterior).
- Control `QSID_GLASSFX` del Panel rápido: deslizador 4×1 con botón de
  restablecer. No está en la configuración de fábrica: se añade desde "Añadir un
  control". Solo se ve y responde con Liquid Glass activo (`qpCtlShown`); con el
  vidrio apagado queda oculto pero no se borra de la configuración.
- El material se aplica **al soltar**, una vez: invalida la tarjeta cacheada y
  la banda pre-desenfocada, rehace el escritorio (el backdrop se desenfoca de
  nuevo si cambió el radio) y la capa de vidrio de la cortina. Mientras se
  arrastra solo se mueve el indicador. NVS diferida fuera del gesto. Con una app
  debajo, se repinta al cerrar la cortina por la vía de un cambio de tema.

## Memoria

La franja de página (524 filas) se guarda tres veces (vecina, fondo limpio,
fondo desenfocado) más dos máscaras de 1 bit: ~1,5 MB de PSRAM
(`HP_PSRAM_BYTES`, ≤ 1,6 MB comprobado en las pruebas). Todo es reconstruible y
`memShedSystem()` lo suelta bajo presión. Sin vidrio en el escritorio (estilo
Plano e iconos Planos) el fondo desenfocado ni se reserva ni se calcula.
