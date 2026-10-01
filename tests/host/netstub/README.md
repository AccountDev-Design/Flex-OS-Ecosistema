# netstub · red, NVS y reloj simulados con COMPORTAMIENTO

Dobles para compilar y EJECUTAR en el PC los modulos que hablan con la red y
con la NVS: `FlexOS_Account.cpp` y `FlexOS_Cloud.cpp`. A diferencia de
`stub/` (que solo da cobertura de compilacion), aqui:

* **Preferences** reproduce la semantica EXACTA de arduino-esp32 3.2.1 /
  `nvs_get_str`: `getString(clave, char*, cap)` devuelve la longitud **con**
  el terminador y `putString` devuelve `strlen`. Esa asimetria es la que
  hacia que la cuenta de Flex Account se descartase en cada arranque, y un
  doble "amable" la escondia. La NVS sobrevive a un `netstubPowerCycle()`.
* **HTTPClient** entrega cada peticion a un manejador de la prueba
  (`gNetHandler`), que contesta como lo haria el servidor: estado, cuerpo,
  cabeceras, fallos de transporte o una conexion que se corta a mitad.
  Cada peticion queda anotada (`gNetLog`) con su TLS: asi se comprueba que
  una peticion con la credencial NUNCA sale con `setInsecure()`.
* **WiFi.status()** lo decide la prueba (`gNetWifi`).
* **millis()** es un reloj virtual: `delay()` y `vTaskDelay()` lo avanzan.
* **FreeRTOS** no crea hilos: la prueba ejecuta la vuelta de cada tarea.

Firmas copiadas de arduino-esp32 3.2.1 (las que usan los modulos y nada mas).
