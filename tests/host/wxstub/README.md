# Entorno simulado de las pruebas del motor meteorológico

Sólo lo que usa `FlexOS_Weather.cpp`. Se compila con `-Iwxstub -Istub`,
así que estos ficheros **tapan** a los de `stub/` cuando existe el mismo
nombre y el resto (`Arduino.h`, `sdkconfig.h`…) se toma de allí.

Por qué hay una copia propia en vez de ampliar `stub/`:

* `Preferences.h` aquí **guarda de verdad** (la cache del pronóstico son
  ~1,3 KB y hay que releerla tras un "reinicio"); el de `stub/` no guarda
  nada a propósito, porque a las otras pruebas les basta con compilar.
* La superficie de red (`WiFi`, `HTTPClient`) que necesita este módulo
  incluye métodos que las pruebas del navegador no usan
  (`setFollowRedirects`, `useHTTP10`, `setCACert`…). Ampliar el `stub/`
  compartido por esto habría tocado pruebas que ya funcionan.

Nada de esto se compila nunca para la placa.
