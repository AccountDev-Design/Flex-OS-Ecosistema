# flex_ota

Actualizaciones OTA de Flex OS Ultra (referencia: `FlexOS_OTA.h/.cpp` y `FlexOS_OTA_Bridge.h`; spec `docs/spec/03b` §13).

| Capa | Archivo | Qué es | Dónde se prueba |
|---|---|---|---|
| Modelo | `src/flex_ota_model.c` | C puro: lector JSON mínimo, versiones, MD5 utilizable, textos, lectura del manifiesto, decisión de confirmación de arranque | host, contra `FlexOS_OTA.cpp` extraído (4 configuraciones del preprocesador) |
| Servicio | `src/flex_ota.c` | El motor (comprobar, descargar por streaming, verificar, instalar) sobre `flex_ota_ops_t`: red, flash, tiempo, cerrojo y reinicio inyectados | host (lockstep contra Arduino) y simulador (escena `ota`) |
| Transporte | `src/flex_ota_port.c` | `esp_http_client` con certificado raíz fijado, `esp_ota_*`, MD5 con mbedtls, NVS, rollback | **solo compila**: sin hardware |
| Interfaz | `components/flex_ui/src/shell/flex_ota_ui.c` | aviso, changelog, instalación y Ajustes (LVGL) | simulador (escena `ota`) |

## Seguridad del proceso (de fuera adentro)

1. TLS con el certificado raíz **fijado** (`certs/ota_root_ca.pem`, ver su README). Sin él el motor no conecta. No existe TLS sin verificar en la placa.
2. Redirecciones solo a `https://` y como mucho 5.
3. El MD5 del manifiesto se calcula sobre lo escrito y se compara antes de tocar la partición de arranque.
4. `esp_ota_end()` valida la imagen (cabecera, chip y revisión mínima, sumas de segmentos, SHA-256 anexo).
5. El proyecto de la imagen descargada tiene que ser el que corre (`esp_app_desc_t.project_name`).
6. `otadata` solo cambia al final; antes, la ranura activa no se ha tocado: un corte de luz deja el sistema anterior.
7. La imagen nueva arranca pendiente de validar y se confirma a los 15 s de un arranque sano y sin Modo seguro; si el sistema se reinicia antes, el bootloader vuelve a la anterior.
8. **Pendiente (decisión del propietario):** firma digital de los binarios (en Arduino tampoco existe: solo se lee `sig`) y `ANTIROLLBACK` por eFuse (irreversible).

## Identificadores propios
Clave del manifiesto `flex_os_ultra_idf` (el de Arduino usa `flex_os_ultra`); versión local = `PROJECT_VER` del `CMakeLists.txt` raíz (`MAJOR.MINOR.PATCH` estricto).

## Limitaciones conocidas
* Sin hardware: el transporte solo se ha probado contra dobles de ESP-IDF (`tests/host/test_ota_port.c`). Faltan el Wi-Fi (X3), un servidor real y la flash real
  (ver la fila "Actualizaciones OTA" de `ESP_IDF_HARDWARE_REQUIREMENTS.md`).
* En los primeros ~15 s tras instalar una imagen, `esp_ota_begin` se niega (la imagen en marcha aun no esta confirmada) y la interfaz dice "No hay espacio para la
  imagen": hay que esperar a que el log diga "arranque confirmado".
* Pulsar reset antes de la confirmacion devuelve a la imagen anterior (es el rollback del bootloader; esperado).
* Sin certificado fijado (`certs/ota_root_ca.pem`) no hay descarga.
