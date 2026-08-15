# Flex Intelligence — archivo para futuras versiones BETA

Este código se retiró por completo de la versión estable de Flex OS Ultra.
Nada dentro de esta carpeta se incluye, enlaza ni ejecuta al compilar
`FlexOS_Ultra.ino`.

## Qué se conserva

- `firmware-modules/`: corrector local, motor Cowork y cliente/protocolo HTTPS.
- `server-ai/`: backend de referencia y adaptador para un proveedor HTTP real.
- `tests/`: pruebas unitarias portables de los tres módulos.
- La última integración completa en la interfaz queda preservada en el historial
  de Git, commit `e6c6e2cd00931dc8667c354276388b094d134543`.

No se copia el `.ino` completo aquí porque duplicaría todo el sistema operativo y
haría que ambas versiones se desincronicen. Para estudiar la integración antigua:

```bash
git show e6c6e2cd00931dc8667c354276388b094d134543:FlexOS_Ultra.ino
```

## Por qué no está en la versión estable

La integración mezclaba el asistente, el panel global, el compositor, la cola de
trabajos y las notificaciones dentro del archivo principal. En la placa eso podía
repintar zonas que pertenecían a otra pantalla, superponer avisos y dejar una UI
inconsistente. Además, una tarjeta de finalización no debe aparecer si no existe
un trabajo real y verificable detrás.

La versión estable conserva únicamente notificaciones con una fuente real:
hardware detectado por el bus I2C y noticias recibidas desde el servicio que haya
configurado el usuario. No quedan disparadores de demostración.

## Condiciones para volver a integrarlo

Una futura BETA no debe copiar la integración anterior sin rediseñarla. Como
mínimo tiene que cumplir todo esto:

1. Un proveedor real configurado y una prueba de conexión exitosa; las funciones
   no soportadas deben declararlo, nunca fingir un resultado.
2. Cada notificación debe corresponder a una transición persistida de un trabajo
   real, con identificador, hora, origen y resultado comprobables.
3. Un único coordinador de notificaciones: una tarjeta visible por vez, reemplazo
   por identidad y descarte coherente; ninguna ruta paralela que pinte por encima.
4. El trabajo de fondo no puede tocar el framebuffer. Solo publica eventos para
   que el hilo de UI los componga en un punto controlado.
5. Cancelación, pausa por permisos y recuperación tras reinicio verificadas en
   la placa ESP32-P4, además de las pruebas de host.
6. Pruebas visuales en pantalla real para Inicio, apps verticales, teclado,
   bloqueo, panel rápido y cambio de páginas antes de considerarlo estable.

## Pruebas de los módulos archivados

Desde esta carpeta:

```bash
make
```

Estas pruebas validan la lógica portable. No validan la antigua integración
gráfica ni convierten por sí solas el prototipo en una función lista para publicar.
