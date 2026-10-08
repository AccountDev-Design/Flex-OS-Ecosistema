# flex_storage

**Estado:** Fase 2 hecha (NO PROBADO EN HARDWARE REAL).

* NVS: los espacios `flexos`, `flexcare` y `flexphone` de la versión Arduino se cargan en una caché en
  RAM (`flex_kv`) con los mismos tipos que `Preferences`. Leer nunca toca la flash; escribir marca la
  entrada y la tarea `storage` (único escritor) la graba 300 ms después del último cambio.
* LittleFS en la partición `spiffs` (montado en `/flex`), compatible con el de la versión Arduino
  (`tests/littlefs_compat`). Operaciones en cola con respuesta en la tarea de UI por el buzón.
* Nunca borra la NVS ni formatea LittleFS por un fallo: solo con la ficha de confirmación del usuario.

Referencia funcional en la versión Arduino (`FlexOS_Ultra/`): `FlexOS_FS.*`, `FlexOS_StorageCore.*`,
`FlexOS_Ultra_Prefs.h`, `FlexOS_Ultra_Session.h`. Detalles: `docs/FASE_2_INFORME.md`.
