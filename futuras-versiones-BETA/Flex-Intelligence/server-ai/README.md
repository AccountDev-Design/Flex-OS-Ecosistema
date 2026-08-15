# Backend de referencia de Flex Intelligence

Servidor mínimo para el asistente de **Flex OS Ultra (ESP32-P4)**. El
firmware ya trae el cliente HTTPS; esto es el otro lado.

* **Node.js 20 o superior** (probado con 22). Se usa `node --test`, que
  llegó en la 18, y `fetch`, que es estable desde la 21.
* **Cero dependencias.** Todo sale de los módulos que trae Node
  (`node:https`, `node:crypto`, `node:fs`). No hay `npm install`.
* **Ningún secreto en el código.** Token, certificado y clave del
  proveedor salen del entorno.
* Es un proyecto **aparte** de `server/`, que es el servicio del
  navegador y no tiene nada que ver con esto.

---

## 1. Arrancarlo en Ubuntu

```bash
cd futuras-versiones-BETA/Flex-Intelligence/server-ai

# 1. Un token. Sin él el servidor no arranca.
openssl rand -hex 24            # copia el resultado

# 2. Un certificado para la IP de tu PC en la red local.
hostname -I                     # mira tu IP, p. ej. 192.168.1.50
bash tools/make-cert.sh 192.168.1.50

# 3. Configura y arranca.
cp .env.example .env
nano .env                       # pega el token en FLEXAI_TOKEN
set -a && . ./.env && set +a
npm start
```

Debe salir:

```
[flexai] escuchando en https://0.0.0.0:8443/v1/flex
[flexai] proveedor: local
[flexai] token cargado del entorno (no se muestra)
```

Si tienes cortafuegos: `sudo ufw allow 8443/tcp`.

---

## 2. Configurarlo desde el dispositivo

En **Flex Intelligence → Ajustes**:

| Fila | Qué poner |
|---|---|
| Servidor (HTTPS) | `https://192.168.1.50:8443/v1/flex` — tu IP y tu puerto |
| Identificador | cualquier nombre corto, p. ej. `flexos-p4-001` |
| Token | el que generaste en el paso 1 |
| Certificado raíz | el contenido de `certs/server.crt` (lo imprime `make-cert.sh`) |
| Permitir texto | **Sí** |

Y luego **Probar conexión**. Debe decir «Listo».

### Si falla

| Dice | Qué pasa |
|---|---|
| «Falta el certificado del servidor» | No pegaste el PEM, o no empieza por `-----BEGIN CERTIFICATE-----` |
| «Token rechazado» | El token del dispositivo no es el de `FLEXAI_TOKEN` |
| «Sin conexión» | El dispositivo no llega a esa IP: distinta red, o cortafuegos |
| «Sin configurar» | Falta la URL, el identificador o el token |

El firmware **exige `https://` y valida el certificado**, y no tiene
ningún interruptor para saltárselo. No es una molestia: por ese socket
van el texto del usuario y su token.

---

## 3. Qué hace de verdad, y qué no

El proveedor de fábrica (`FLEXAI_PROVIDER=local`) **no llama a ningún
servicio y no necesita ninguna clave**. Hace transformaciones
deterministas sobre el texto, y sirve para probar el camino completo —
firmware, TLS, token, protocolo, acciones y confirmación — sin contratar
nada ni mandar el texto del usuario a un tercero.

| Tarea | Qué hace el proveedor local |
|---|---|
| Resumir | Resumen extractivo real: las dos primeras frases → propone `create_note` |
| Corregir | Normaliza espacios y puntuación → propone `replace_text` si cambió algo |
| Analizar | Cuenta palabras, frases y caracteres |
| Explicar error | Devuelve el texto recibido, diciendo que no lo interpreta |
| Traducir / Tono | **Dice que no puede**: hace falta un modelo de lenguaje |
| Imagen | **Dice que no analiza imágenes** |
| Buscar archivos | Dice que eso se resuelve en el dispositivo |

Lo importante es lo último: **cuando no puede hacer algo, lo dice** en
lugar de devolver el original disimulado. Un backend de pruebas que
miente hace que se den por buenas cosas que no funcionan.

### Conectar un modelo real

```bash
FLEXAI_PROVIDER=http
FLEXAI_UPSTREAM_URL=https://tu-proveedor.example/v1/responses
FLEXAI_UPSTREAM_KEY=...        # se lee del entorno; no se escribe en disco
FLEXAI_UPSTREAM_MODEL=...
```

`src/providers.js` hace la llamada y saca el texto de las formas más
comunes de respuesta. Adaptarlo a un proveedor concreto es cambiar
`pickText()`.

---

## 4. El protocolo

**Petición** — `POST /v1/flex`, `Authorization: Bearer <token>`:

```json
{ "v": 1, "device": "flexos-p4-001", "task": "summarize", "text": "...", "extra": "es" }
```

`task` ∈ `correct` · `summarize` · `translate` · `tone` · `explain` ·
`analyze` · `image` · `find`.

**Respuesta** — sólo estas tres claves:

```json
{ "text": "...", "action": "create_note", "arg": "Resumen" }
```

`action` sólo puede ser una de estas cinco, y **cualquier otra cosa se
convierte en `show_text`** antes de salir:

| Acción | Qué hace el dispositivo | ¿Confirma? |
|---|---|---|
| `show_text` | Muestra el resultado | No |
| `create_note` | Crea una nota en `/Notas` | **Sí** |
| `replace_text` | Reemplaza el documento de origen | **Sí** |
| `append_note` | Añade al final del documento de origen | **Sí** |
| `open_app` | Abre una app del sistema | **Sí** |

**Error**:

```json
{ "error": "token invalido" }
```

### Lo que el servidor NO puede hacer, por diseño

* **No elige rutas.** `arg` se rechaza si contiene `/`, `\` o `..`. Y
  aunque no lo hiciera, el firmware lo reduce a un nombre y lo fuerza
  dentro de `/Notas`: `replace_text` y `append_note` escriben sobre la
  ruta que puso el **propio dispositivo**, nunca sobre una del servidor.
* **No ejecuta nada.** No hay acción para ejecutar código, comandos,
  borrar, mover ni compartir. No están en la lista y no se pueden añadir
  desde el servidor.
* **No escribe sin permiso.** Las cuatro acciones que cambian algo
  muestran una confirmación con vista previa. Si el documento cambió
  mientras el trabajo corría, la confirmación es doble.
* **No ve imágenes ni archivos.** Esta versión no envía ninguno.
* **No toca Flex Vault.** El contenido protegido sólo sale con la bóveda
  abierta, el archivo elegido a mano y una confirmación, cada vez.

---

## 5. Probar sin el dispositivo

Con TLS:

```bash
curl -sk https://localhost:8443/v1/flex \
  -H "Authorization: Bearer $FLEXAI_TOKEN" \
  -H 'content-type: application/json' \
  -d '{"v":1,"device":"pc","task":"summarize","text":"Uno. Dos. Tres."}'
```

```json
{"text":"Uno. Dos.\n\n(resumen extractivo local: primeras frases, 3 palabras de origen)","action":"create_note","arg":"Resumen"}
```

Comprobando que el token se valida:

```bash
curl -sk https://localhost:8443/v1/flex -H "Authorization: Bearer mal" \
  -H 'content-type: application/json' -d '{"v":1,"device":"pc","task":"analyze","text":"x"}'
# {"error":"token invalido"}   (HTTP 401)
```

Y que una acción inventada no sale:

```bash
node -e '
const {shapeResponse} = require("./src/protocol");
console.log(shapeResponse({text:"ok", action:"delete_file", arg:"../../System"}));'
# { text: "ok", action: "show_text" }
```

### Pruebas

```bash
npm test        # 14 comprobaciones, sin red y sin certificados
```

---

## 6. Seguridad

* **El token se compara en tiempo constante** (`crypto.timingSafeEqual`).
  Un `===` filtra por el tiempo de respuesta cuántos caracteres acertó
  quien lo prueba, y eso convierte una fuerza bruta imposible en una
  posible.
* **Sin token no arranca.** Se prefiere no arrancar a quedarse abierto.
* **Revocar es cambiar `FLEXAI_TOKEN` y reiniciar.** El dispositivo dirá
  «Token rechazado» hasta que pongas el nuevo.
* **El cuerpo se corta antes de acumularlo** (4 KB). Sin ese corte,
  mandar 100 MB llenaría la memoria del proceso.
* **Límite por dispositivo** (30/min por defecto): corta un bucle
  accidental del firmware sin estorbar al uso normal.
* **Los fallos internos no se le cuentan al dispositivo.** El mensaje de
  un proveedor puede llevar su URL o parte de una clave: eso va al log
  del servidor, y al dispositivo le llega «el proveedor no respondió».
* **Nada se guarda.** Ni el texto, ni las respuestas, ni un historial.
