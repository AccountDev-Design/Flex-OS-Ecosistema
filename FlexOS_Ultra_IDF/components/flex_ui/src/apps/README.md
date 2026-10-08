# Apps de Flex OS Ultra (LVGL)

Una app = un archivo `app_<nombre>.c` que define `const flex_app_ops_t flex_app_<nombre>_ops`
(ver `../shell/flex_app.h`). El registro (`../shell/flex_app_registry.c`) la enlaza por su
nombre; mientras una app no exista, se muestra la pantalla "En construcción".

Reglas: solo LVGL (nada de dibujo por píxeles salvo un lienzo real como Paint), textos con
`flex_t()`/tablas de idioma, colores de `flex_th()`, superficies con `flex_surface()`, nada
bloqueante en la tarea de UI (archivos con `flex_fs_*_async`, red por servicios).
