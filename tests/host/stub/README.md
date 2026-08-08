# Entorno Arduino simulado (solo para pruebas de host)

Estas cabeceras NO forman parte del firmware. Existen para poder
**compilar y enlazar en el PC** `FlexOS_BrowserApp.cpp`, que es codigo de
dispositivo (FreeRTOS, WiFi, TLS) y por tanto no se puede probar de otra
forma sin la placa delante.

Lo que aportan es cobertura de *compilacion*: tipos, firmas, declaraciones
que faltan, variables sin usar, conversiones peligrosas. No simulan la red
ni el hardware: `tests/host/test_app.cpp` implementa el puente `brHost*`
contra un lienzo en memoria y comprueba el ciclo de vida, la liberacion de
recursos y el comportamiento del dibujo.

Si una firma de estas cabeceras se separa de la real de arduino-esp32, el
codigo compilaria aqui y fallaria en la placa: por eso se han copiado
literalmente las firmas que el modulo usa, y nada mas.
