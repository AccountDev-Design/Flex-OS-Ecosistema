# Flex Vault (Carpeta segura) · Flex OS Ultra · ESP32-P4

Ajustes → **Seguridad y privacidad** → **Flex Vault**

Este documento es la referencia de la funcion: que hace, como esta hecha, la
**auditoria de fugas** con el resultado de cada comprobacion, y — sin adornos —
**que apps quedaron compatibles de verdad y cuales no**.

Solo aplica a **Flex OS Ultra (ESP32-P4)**. `FlexOS_Ultra_S3.ino` y
`FlexOS_Pro.ino` no se han tocado. Los modulos comunes (`FlexOS_FS`,
`FlexOS_Vault`) compilan para las tres placas, pero la interfaz de la boveda
solo existe en el sketch del P4.

---

## 1. Que ve el usuario

| Pantalla | Que hay |
|---|---|
| Alta (primera vez) | Elegir **PIN o contraseña propia**, distinta del bloqueo de Flex OS |
| Estado | "Flex Vault protegida", último acceso, bloqueo automático, intentos fallidos, almacenamiento usado |
| Galería privada | Fotos y dibujos cifrados, con vista previa real |
| Notas privadas | Lista y **editor** (el texto se guarda cifrado) |
| Archivos privados | Cualquier otro fichero cifrado |
| Añadir apps a Carpeta segura | Las candidatas, cada una con su estado real |
| Gestionar apps privadas | Por app: almacenamiento, último acceso, **Abrir / Bloquear / Quitar** |
| Quitar una app | Las tres opciones para sus datos (mover / mantener cifrados / eliminar) |
| Registro de seguridad | Aperturas, cierres con su motivo, fallos, cambios de clave, movimientos |
| Cambiar clave | Sin perder ningún archivo |

Desde **Galería, Notas, Archivos y Paint**: mantener pulsado un elemento →
**"Mover a Carpeta segura"**. Al moverlo desaparece de la app normal y solo se
abre desde Flex Vault.

---

## 2. Seguridad técnica

### Esquema de claves (de sobre)

```
contraseña ──PBKDF2-HMAC-SHA256(sal 16 B, 25.000 iter)──▶ KEK (32 B)
                                                            │
clave maestra MK (32 B, ALEATORIA por bóveda) ◀── AES-256-GCM ┘   (desenvuelve)

clave de cada fichero = HMAC-SHA256(MK, "fxv-key-v1" | clase | id | sal-escritura)
clave del índice      = HMAC-SHA256(MK, ... )   ← nombres, rutas, tamaños
```

Consecuencias reales, no teóricas:

- La contraseña **desenvuelve** la clave maestra; **no** es la clave de cifrado
  de los ficheros. Cambiarla reescribe 76 bytes de sobre y **no toca ni un
  byte** de los blobs (hay una comprobación que verifica exactamente eso).
- El **tag GCM del sobre es** la verificación de la contraseña: no se guarda
  ningún "hash de la clave" aparte. Clave incorrecta = el desenvuelto no
  autentica y no se obtiene ninguna clave.
- Cada fichero tiene su propia clave, derivada con una **sal de escritura nueva
  en cada guardado**. Por construcción no puede repetirse un par (clave, nonce),
  que es la única forma de romper AES-GCM.

### Por qué AES-256-GCM

Cifrado autenticado (AEAD): protege el contenido **y** detecta manipulación, que
es lo que hace falta cuando los bytes viven en una partición que cualquiera
puede reescribir con un lector de flash. Y el ESP32-P4 lo trae **en hardware**:
mbedTLS, tal como viene en el core de arduino-esp32, usa el acelerador AES del
chip.

No se usó AES-CBC + HMAC (dos pasadas, más código, más formas de equivocarse) ni
ChaCha20-Poly1305 (correcto, pero **sin acelerador en este chip**: sería más
lento aquí).

### Por qué PBKDF2 y no scrypt / Argon2

En el stack de ESP-IDF / arduino-esp32 **no hay** scrypt ni Argon2. Lo que hay es
PBKDF2-HMAC-SHA256, que es estándar y aceptable con un número de iteraciones
alto. Se implementa sobre HMAC-SHA256 (doce líneas) en vez de llamar a
`mbedtls_pkcs5_*`, porque esa API cambió de nombre entre mbedTLS 2.x y 3.x. Las
iteraciones se **guardan en cada bóveda**, así que subirlas en el futuro no
invalida las que ya existen.

### Ficheros por bloques

Bloques de 4 KB de plano, cada uno con su tag. Los **datos autenticados** de cada
bloque incluyen la cabecera completa, el **número de bloque** y si **es el
último**. Resultado:

- un byte cambiado en el cifrado, en el tag o en la cabecera → no descifra;
- dos bloques intercambiados → no descifra (el orden está autenticado);
- el fichero recortado por el final → no descifra (el bloque que pasaría a ser
  último se autenticó como "no último").

Hay una comprobación para cada uno de esos cinco casos.

Un fichero grande **nunca se carga entero**: se cifra y descifra bloque a bloque
(`flexFsReadAt` / `flexFsAppendBin`), y la vista previa tiene un tope explícito
en lugar de intentar reservar lo que no cabe.

### Limpieza de RAM

Al cerrar la bóveda se borran, con un borrado que el compilador **no puede
eliminar** por "código muerto": la clave maestra, el índice descifrado (lleva los
nombres reales), el texto de la nota abierta, los bytes de la imagen abierta y
los buffers de PIN/contraseña. Los buffers de clave se borran además en cuanto se
usan.

### Intentos fallidos

Contador **en NVS**: reiniciar la placa no lo perdona. 1-3 fallos sin espera, 4-5
medio minuto, 6+ cinco minutos. Tras un reinicio la espera se cobra una vez antes
del primer intento (mismo criterio que el bloqueo del sistema, para que el
usuario no tenga que aprender dos comportamientos).

### Bloqueo del sistema: migración

El PIN y la contraseña de la pantalla de bloqueo se guardaban **en texto legible**
en NVS (`lockpin`, `lockpass`). Ahora son sal + PBKDF2-HMAC-SHA256, comparados en
tiempo constante, y la clave del usuario **ya no existe en ninguna variable del
sketch** (`lsuSaved` desapareció; solo se lee la longitud del PIN, que los puntos
de la pantalla ya revelan).

La migración corre en cada arranque y tiene **tres pasos**:

1. escribe el hash **sin tocar** la clave antigua,
2. comprueba que ese hash valida esa misma clave,
3. y **solo entonces** borra `lockpin`/`lockpass`.

Si algo falla a mitad, el teléfono sigue abriéndose con la clave de siempre: una
migración no puede dejar a nadie fuera de su dispositivo.

### Límite honesto de protección

Por indicación expresa, **no** se activan Secure Boot, Flash Encryption, eFuses,
JTAG irreversible ni modo producción. Eso significa que **quien tenga la placa en
la mano puede leer la flash** y obtener la sal y la clave maestra *envuelta*. Lo
que le frena entonces es únicamente el coste de PBKDF2 sobre la contraseña:

- una contraseña larga sigue siendo inviable de romper;
- **un PIN de 4 dígitos no lo es** frente a un ataque con la flash volcada.

La interfaz lo dice en la pantalla de alta en vez de prometer lo que no puede
cumplir. El formato ya está versionado (`FLEXVAULT_FORMAT_VERSION`) para que
activar Flash Encryption más adelante cierre ese hueco sin cambiar nada del
almacén.

---

## 3. Auditoría de fugas

El principio de todo el diseño: **la privacidad no depende de que cada pantalla
se acuerde de filtrar**. El filtro está en la capa que abre los directorios y en
la elección de no ser una app.

| Debe NO aparecer en | Por qué no puede | Verificado |
|---|---|---|
| **Recientes** | `swPushAndCapture` (que guarda una miniatura del framebuffer) solo se llama desde `appClose`, y la bóveda **no es una app de `APP_REG`**: no hay ruta que la meta ahí. Además, entrar en Recientes cierra la bóveda antes de nada. | `grep` de los cuatro llamantes de captura: todos en el camino de apps |
| **Búsqueda global** | El cajón y el buscador de Modo PC (`dexFilterApps`, `dexMatch`) recorren `APP_REG` y `DEX_SET`. No hay ninguna búsqueda que recorra ficheros. | `grep dexFilterApps/dexMatch` |
| **Notificaciones** | Los cinco llamantes de `notifPush` son detección de módulos y la demo de arranque. La bóveda **nunca** crea una notificación. | `grep notifPush(` |
| **Miniaturas** | Única función que reduce el framebuffer: `captureThumb`, llamada solo por `swPushAndCapture`. | igual que Recientes |
| **Galería normal** | Lista `/Documentos` y `/Paint` con `flexFsList`, que **salta** el almacén. Y al mover un elemento, el original se borra. | prueba: tras mover, el fichero no está y su contenido no aparece en ningún byte |
| **Archivos normales** | `flexFsList/Count/DirSize/CatSize/Largest` saltan el almacén; `Delete/Rename/Trash` y las lecturas/escrituras lo **rechazan**. | prueba de aislamiento (10 comprobaciones) |
| **Modo PC / DeX** | No está en `APP_REG` (ni cajón ni buscador ni ventana). Y abrir la bóveda dentro de una ventana está **prohibido**: `dexHostRun` restaura `gState` al terminar el tick, así que quedaría a medias. La fila de Ajustes lo dice. | guarda `gHosted \|\| gLand` en `vaultSettingsEnter` y en `vaultMoveRequest` |
| **Vista previa de apps** | El panel rápido (`qsCanOpen`) y la isla de notificaciones solo se dibujan sobre `ST_HOME`/`ST_APP`. Sobre `ST_VAULT` no hay ninguna capa del sistema. | `qsCanOpen`, `notifTick` |
| **Historial normal de las apps** | El contenido privado vive en el índice cifrado, no en las carpetas de las apps. Los datos de una app privada están además **marcados como suyos**, y las secciones generales no los listan. | prueba de separación por app |
| **Registros** | El módulo de la bóveda **no imprime nada**: sus 15 usos de `printf` son todos `snprintf` a buffers. El registro de seguridad guarda **códigos**, nunca nombres ni contenido, y va cifrado. | `grep Serial\.` = 0 · prueba: el registro no contiene texto legible |

### Nombres de fichero

Los blobs se llaman por **número** (`/.fxvault/d/7.b`). El nombre de un fichero
no se puede cifrar (lo guarda el sistema de archivos tal cual), así que un
`foto-dni.jpg.fxv` delataría el contenido con solo volcar la flash. El nombre
real vive **dentro** del índice cifrado. Hay una comprobación de que el nombre
original no aparece en claro en ningún byte de la partición.

### Agujero encontrado y cerrado durante la auditoría

`flexFsRestore` deduce la ruta de destino del **nombre** del fichero de la
papelera, o sea de datos que están en el disco y se pueden manipular. Un nombre
fabricado como `@.fxvault@algo` se decodificaba a `/.fxvault/algo` y la
restauración habría escrito **dentro** del almacén. Ahora esa ruta se comprueba y
se rechaza. Era el único punto en el que una ruta de destino no la elegía el
código.

### Lo que sí es visible (y por qué está bien)

`flexFsUsedBytes()` devuelve lo que reporta LittleFS para la partición entera, e
incluye los bytes del almacén. Es un **número total**, sin nombres ni contenido —
el equivalente a que el espacio libre baje. Las *categorías* de la pantalla de
Almacenamiento sí excluyen la bóveda, y la bóveda mide su propio uso por dentro.

---

## 4. Apps privadas: qué quedó compatible de verdad

El criterio no se negocia: **una app es compatible si TODO su estado persistente
puede redirigirse al almacén cifrado**. Si solo se puede separar una parte, la
app **no** es compatible — una "versión privada" que comparte caché, sesión o
historial con la normal es peor que no tenerla, porque parece privada y no lo es.

### Compatibles

| App | Por qué de verdad lo es |
|---|---|
| **Notas** (id 5) | Todo su estado son ficheros de texto. La versión privada lista, crea, edita y guarda **cifrado** (`flexVaultWrite`), con datos marcados como suyos. |
| **Galería** (id 1) | Su contenido son ficheros de imagen. La galería privada los descifra **a RAM** y los pinta desde ahí: JPEG con `flexJpegDecode`, dibujos con `flexPaintReplayMem`. Nunca se escribe una copia en claro en la partición para poder enseñar algo. |
| **Archivos** (id 3) | El explorador privado opera sobre el propio almacén cifrado. La administración del sistema (formatear, papelera, tamaños) no forma parte de la versión privada. |

### Todavía NO compatibles, con el motivo exacto que se muestra al usuario

| App | Motivo real |
|---|---|
| **Navegador** (id 7) | Su historial y sus favoritos **sí** se podrían redirigir (van por `brHostFileRead/Write`), pero **su sesión y su caché viven en el servicio de navegación**, fuera de la placa (`server/src/session.js`). Una versión "privada" compartiría cookies y sesión con la normal. Separarlo requiere sesiones por perfil en el servicio. |
| **Paint** (id 10) | Guarda **cada trazo directamente en el fichero** mientras dibujas (`flexPaintAppend`). Una versión privada necesitaría un `.fxp` temporal **en claro** en la partición durante toda la sesión de dibujo. Los dibujos ya hechos **sí** se pueden mover a la bóveda y se ven dentro (desde RAM); lo que falta es el editor. |
| **Calendario** (id 14) | Todavía **no guarda datos propios**. No hay nada que separar: una versión privada sería una copia falsa. |

### Prohibidas (nunca entran en la bóveda)

**Ajustes** (id 12) y **Modo PC** (id 4).

Ajustes es la puerta de la OTA, del bloqueo, del apagado, de la recuperación y de
la propia Flex Vault: una copia "privada" sería una segunda consola de
administración detrás de la clave. Modo PC hospeda otras apps en sus ventanas, así
que meterlo dentro sería una vía para colar cualquier app en la bóveda **sin
pasar por este filtro**.

El resto de apps de Flex OS Ultra no se ofrecen: no son candidatas.

---

## 5. Cierre automático

`vaultLockFromSystem()` es el **único** camino de cierre, y lo llaman los seis
sitios que lo necesitan:

| Situación | Dónde |
|---|---|
| Apagar la pantalla (gesto de suspensión) | `suspEnter` |
| Despertar de la suspensión | `suspWakeLockScreen` |
| Auto-bloqueo por inactividad del sistema | `autoLockNow` |
| Apagado completo | `poffEnter` |
| Cualquier verificación de la clave **del sistema** | `lsuStartVerify` |
| Entrar en Recientes | `activarMultitarea` |

Más: el **temporizador propio** de inactividad de la bóveda (15 s / 30 s / 1 min /
5 min / solo al salir), **salir** de la bóveda, y el **arranque** — `flexVaultBegin`
no descifra nada, así que reiniciar nunca la deja abierta.

Acertar la clave **del sistema** no da acceso a la bóveda: son dos claves
distintas a propósito, y la del sistema **cierra** la bóveda en vez de abrirla.

Si había una nota privada a medio escribir, se guarda **cifrada** antes de tirar la
clave: cerrar por inactividad no puede costarle al usuario lo que acababa de
escribir.

---

## 6. OTA

- **Compatibilidad intacta**: el manifiesto, el flujo y los estados son los de
  siempre. Los códigos de error nuevos se añaden **al final** del enum, así que
  ningún valor anterior se mueve.
- **`setInsecure()` prohibido en una versión publicable.** Antes, sin
  `FLEXOS_OTA_ROOT_CA` se aceptaba **cualquier** certificado. Eso convierte la
  actualización en la mejor vía de entrada del dispositivo: quien controle la red
  sirve el manifiesto **y** el binario, así que el MD5 del manifiesto no protege
  de nada. Ahora el interruptor es explícito (`FLEXOS_OTA_ALLOW_INSECURE`, por
  defecto **0**) y sin CA la OTA falla con "Falta el certificado del servidor".
- **Firma digital y anti-rollback: arquitectura lista, apagada.** El manifiesto
  ya admite y guarda `sig`/`sigalg`; existen `OTA_ERR_NO_SIG`, `OTA_ERR_BAD_SIG`
  y `OTA_ERR_ROLLBACK`; el comparador de versiones está en su sitio. Los dos
  interruptores están a 0 porque encenderlos cambia lo que el dispositivo acepta
  como firmware válido y hay que probarlo primero en una placa de pruebas. El
  anti-rollback **de verdad** necesita el contador seguro de eFuses, que es
  irreversible: esta versión se queda en la comprobación por software y no toca
  ni un eFuse.
- **Noticias**: ese cliente también llamaba siempre a `setInsecure()`, y por ese
  socket viaja la **clave de API** del usuario. Ahora, por defecto, una URL
  `https` no se consulta sin verificar. Dos salidas: incrustar la CA del
  proveedor al publicar (`FLEXOS_NEWS_ROOT_CA`) o que el usuario lo autorice a
  mano en Ajustes → Noticias, con la advertencia delante.

---

## 7. Ficheros y pruebas

| Fichero | Qué aporta |
|---|---|
| `FlexOS_Vault.h/.cpp` | **Nuevo.** Criptografía, claves, almacén, índice, registro, apps privadas y el hash con sal del bloqueo del sistema. Cero interfaz. |
| `FlexOS_FS.h/.cpp` | Almacén invisible para la capa normal, puerta de servicio `flexFsPriv*`, lectura/escritura parcial, Paint desde memoria. |
| `FlexOS_Ultra.ino` | Interfaz de la bóveda (`ST_VAULT`), fila de Ajustes, quinta acción del menú de pulsación larga, Galería real, ganchos de cierre. |
| `FlexOS_OTA.h/.cpp` | Sin TLS sin verificar, arquitectura de firma y anti-rollback. |
| `tests/host/test_vault.cpp` | **190 comprobaciones** sobre el módulo real. |

Ejecutar todo:

```sh
cd tests/host && make          # 854 comprobaciones, incluidas las 190 de la bóveda
```

Lo que las pruebas verifican de verdad: vectores de PBKDF2, cambio de clave sin
recifrar (comparando los bytes del blob antes y después), las cinco formas de
manipular un blob, el intercambio de bloques, troceado con último bloque parcial,
el contador de intentos tras un reinicio, las tres opciones al quitar una app, la
separación de datos por app, las tres rutas de migración del bloqueo, y que ni un
nombre ni un byte privado quedan en claro en la partición.

**Límite honesto de las pruebas**: la primitiva AES-GCM que se ejercita en el PC
es la de OpenSSL; en la placa es la de mbedTLS con el acelerador del P4. Lo que se
verifica es la **lógica de esta capa** — esquema de sobre, derivación, troceado,
formato en disco, índice, bloqueo, migración —, que es la parte escrita aquí y la
que puede estar mal. La primitiva la firma una biblioteca auditada en los dos
casos.

**Y lo que ninguna prueba de host puede verificar**: que esto se comporta igual en
la placa. Falta la prueba en hardware real (P4 con PSRAM y su partición de datos)
antes de publicar.
