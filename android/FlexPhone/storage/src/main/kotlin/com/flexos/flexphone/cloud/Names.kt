package com.flexos.flexphone.cloud

import java.net.URLEncoder
import java.security.SecureRandom
import java.text.Normalizer
import java.util.Locale

/**
 * Nombres, identificadores, tipos MIME y cabeceras de descarga.
 *
 * Espejo de `cloud/src/cloud/names.js` (Flex Developer Studio): las mismas
 * reglas de nombres, la misma forma de los identificadores y los mismos tipos.
 * El gestor del P4 los lee igual venga la respuesta de Internet o del telefono.
 *
 * Los nombres son TEXTO del usuario: se guardan tal cual (NFC) con tildes, enie,
 * emojis y espacios. Solo se rechaza lo que no puede ser un nombre en ningun
 * sistema: separadores de ruta, NUL y caracteres de control, "." y "..",
 * controles de direccion de texto y mas de 255 bytes UTF-8.
 */
object Names {
    const val MAX_NAME_BYTES = 255
    private const val ALPHABET = "abcdefghijklmnopqrstuvwxyz234567"
    private val rng = SecureRandom()

    val ID_FILE = Regex("^fil_[a-z2-7]{24}$")
    val ID_FOLDER = Regex("^fld_[a-z2-7]{24}$")
    val ID_UPLOAD = Regex("^upl_[a-z2-7]{24}$")

    /** 120 bits aleatorios en base32 minuscula: ni adivinables ni enumerables. */
    fun newId(prefix: String): String = "${prefix}_${base32(randomBytes(15))}"

    fun randomBytes(n: Int): ByteArray = ByteArray(n).also { rng.nextBytes(it) }

    fun base32(bytes: ByteArray): String {
        val out = StringBuilder()
        var bits = 0
        var acc = 0
        for (b in bytes) {
            acc = (acc shl 8) or (b.toInt() and 0xFF)
            bits += 8
            while (bits >= 5) {
                bits -= 5
                out.append(ALPHABET[(acc shr bits) and 31])
            }
            acc = acc and ((1 shl bits) - 1)
        }
        if (bits > 0) out.append(ALPHABET[(acc shl (5 - bits)) and 31])
        return out.toString()
    }

    fun normalizeName(raw: Any?): String {
        if (raw !is String) throw E.nameInvalid("El nombre es obligatorio.")
        val name = Normalizer.normalize(raw, Normalizer.Form.NFC).trim()
        if (name.isEmpty()) throw E.nameInvalid("El nombre no puede estar vacío.")
        if (name == "." || name == "..") throw E.nameInvalid("Ese nombre está reservado.")
        for (c in name) {
            if (c.code < 0x20 || c.code == 0x7f || c == '/' || c == '\\')
                throw E.nameInvalid("El nombre no puede contener \"/\", \"\\\" ni caracteres de control.")
            if (c.code in 0x202a..0x202e || c.code in 0x2066..0x2069)
                throw E.nameInvalid("El nombre contiene caracteres de control de dirección de texto.")
        }
        if (!wellFormed(name)) throw E.nameInvalid("El nombre no es texto Unicode válido.")
        if (name.toByteArray(Charsets.UTF_8).size > MAX_NAME_BYTES)
            throw E.nameInvalid("El nombre es demasiado largo (máximo 255 bytes).")
        return name
    }

    /** Pares sustitutos sueltos (UTF-16 roto) no son un nombre valido. */
    private fun wellFormed(s: String): Boolean {
        var i = 0
        while (i < s.length) {
            val c = s[i]
            if (Character.isHighSurrogate(c)) {
                if (i + 1 >= s.length || !Character.isLowSurrogate(s[i + 1])) return false
                i += 2
                continue
            }
            if (Character.isLowSurrogate(c)) return false
            i++
        }
        return true
    }

    /** Dos nombres que solo difieren en mayusculas (o en la forma Unicode) chocarian en un disco que no las distingue. */
    fun nameKey(name: String): String = Normalizer.normalize(name, Normalizer.Form.NFC).lowercase(Locale.ROOT)

    fun splitExt(name: String): Pair<String, String> {
        val i = name.lastIndexOf('.')
        if (i <= 0 || i == name.length - 1) return name to ""
        return name.substring(0, i) to name.substring(i)
    }

    /** "foto.jpg" -> "foto (1).jpg" -> "foto (2).jpg"... sin pasar de 255 bytes. */
    fun numberedName(name: String, n: Int): String {
        val (stem0, ext) = splitExt(name)
        val suffix = " ($n)"
        var stem = stem0
        while ((stem + suffix + ext).toByteArray(Charsets.UTF_8).size > MAX_NAME_BYTES && stem.isNotEmpty()) {
            val cps = stem.codePoints().toArray()
            stem = String(cps, 0, cps.size - 1)
        }
        return stem + suffix + ext
    }

    private val MIME_BY_EXT = mapOf(
        ".jpg" to "image/jpeg", ".jpeg" to "image/jpeg", ".png" to "image/png", ".gif" to "image/gif", ".webp" to "image/webp",
        ".heic" to "image/heic", ".heif" to "image/heif", ".bmp" to "image/bmp", ".svg" to "image/svg+xml", ".avif" to "image/avif",
        ".mp4" to "video/mp4", ".m4v" to "video/mp4", ".mov" to "video/quicktime", ".webm" to "video/webm", ".mkv" to "video/x-matroska",
        ".avi" to "video/x-msvideo", ".3gp" to "video/3gpp",
        ".mp3" to "audio/mpeg", ".m4a" to "audio/mp4", ".aac" to "audio/aac", ".wav" to "audio/wav", ".ogg" to "audio/ogg",
        ".flac" to "audio/flac", ".opus" to "audio/opus",
        ".pdf" to "application/pdf", ".txt" to "text/plain", ".md" to "text/markdown", ".csv" to "text/csv", ".json" to "application/json",
        ".zip" to "application/zip", ".7z" to "application/x-7z-compressed", ".rar" to "application/vnd.rar", ".gz" to "application/gzip",
        ".doc" to "application/msword", ".docx" to "application/vnd.openxmlformats-officedocument.wordprocessingml.document",
        ".xls" to "application/vnd.ms-excel", ".xlsx" to "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet",
        ".ppt" to "application/vnd.ms-powerpoint", ".pptx" to "application/vnd.openxmlformats-officedocument.presentationml.presentation",
        ".html" to "text/html", ".htm" to "text/html", ".js" to "text/javascript", ".css" to "text/css",
        ".flexpkg" to "application/x-flexpkg", ".fxp" to "application/x-flex-paint", ".bin" to "application/octet-stream",
    )
    private val MIME_RE = Regex("^[a-z0-9][a-z0-9!#$&^_.+-]{0,63}/[a-z0-9][a-z0-9!#$&^_.+-]{0,127}$", RegexOption.IGNORE_CASE)

    fun mimeFor(name: String, declared: Any?): String {
        val ext = splitExt(name).second.lowercase(Locale.ROOT)
        if (declared is String && MIME_RE.matches(declared) && declared != "application/octet-stream") return declared.lowercase(Locale.ROOT)
        return MIME_BY_EXT[ext] ?: "application/octet-stream"
    }

    fun kindFor(mime: String): String = when {
        mime.startsWith("image/") -> "photo"
        mime.startsWith("video/") -> "video"
        mime.startsWith("audio/") -> "audio"
        mime == "application/pdf" || mime.startsWith("text/") || mime.contains("document") || mime.contains("sheet") ||
            mime.contains("presentation") || mime == "application/msword" || mime == "application/vnd.ms-excel" -> "document"
        mime.contains("zip") || mime.contains("compressed") || mime.contains("rar") || mime.contains("gzip") -> "archive"
        else -> "other"
    }

    private val ACTIVE = Regex("^(text/html|application/xhtml\\+xml|image/svg\\+xml|text/xml|application/xml|text/javascript|application/javascript)$")

    /** Tipos que un navegador podria EJECUTAR (HTML, SVG, XML, JS): siempre como descarga. */
    fun inlineSafe(mime: String): Boolean {
        if (ACTIVE.matches(mime)) return false
        return mime.startsWith("image/") || mime.startsWith("video/") || mime.startsWith("audio/") ||
            mime == "application/pdf" || mime == "text/plain"
    }

    /** RFC 6266 / 5987: nombre ASCII de respaldo + filename* con el nombre real. */
    fun contentDisposition(name: String, inline: Boolean): String {
        val nfkd = Normalizer.normalize(name, Normalizer.Form.NFKD)
        val fb = StringBuilder()
        for (c in nfkd) {
            fb.append(
                when {
                    c.code < 0x20 || c.code > 0x7e -> '_'
                    c == '"' || c == '\\' || c == ';' -> '_'
                    else -> c
                },
            )
        }
        val fallback = fb.toString().ifEmpty { "archivo" }
        // encodeURIComponent deja sin escapar A-Z a-z 0-9 - _ . ! ~ * ' ( ); el
        // servicio de Node escapa ademas ' ( ) *. URLEncoder escapa todo salvo
        // A-Z a-z 0-9 . - * _ y pone '+' por el espacio: se corrige.
        val enc = URLEncoder.encode(name, "UTF-8").replace("+", "%20").replace("*", "%2A").replace("%21", "!").replace("%7E", "~")
        return "${if (inline) "inline" else "attachment"}; filename=\"$fallback\"; filename*=UTF-8''$enc"
    }
}
