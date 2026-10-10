# flex_power

Apagado completo con deep sleep real y filtro de encendido de 3 s (docs/spec/01c §10;
Arduino: `FlexOS_Ultra_Power.h:892-1083`). La pantalla "¿Apagar FlexOS?" y la animación
final están en `flex_ui` (`flex_poweroff_ui.c`).

* `flex_poweroff_model.c`: deslizador y filtro, C puro (pruebas en `tests/host`).
* `flex_poweroff.c`: guardar sincrónicamente, retener el reset del GT911 y dormir;
  filtro en `app_main` antes de encender la pantalla.

Despertar: el INT del GT911 no está identificado (`FLEX_PIN_TP_INT = -1`), así que se
usa el temporizador cada 400 ms (modo degradado). El filtro vuelve a dormir en cuanto
no hay dedo (300 ms), no tras la ventana entera de 4,2 s como Arduino.

Salida de emergencia: solo un despertar de deep sleep pasa por el filtro; un corte de
alimentación o el botón de reset arrancan siempre.

**NO PROBADO EN HARDWARE REAL.**
