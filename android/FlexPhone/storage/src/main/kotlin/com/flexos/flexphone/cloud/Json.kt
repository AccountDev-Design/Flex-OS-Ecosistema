package com.flexos.flexphone.cloud

/**
 * JSON minimo, sin dependencias.
 *
 * Lo que entra por aqui llega de la red (el P4, el navegador a traves del P4),
 * asi que el lector es ESTRICTO y ACOTADO: profundidad maxima, nada de basura
 * despues del valor, cadenas sin caracteres de control sin escapar y numeros
 * con la gramatica de JSON. Cualquier cosa rara es [JsonException], nunca un
 * valor a medias.
 *
 * Tipos: null, Boolean, Long (enteros que caben), Double, String,
 * List<Any?> y Map<String, Any?> (conserva el orden de las claves).
 */
class JsonException(msg: String) : RuntimeException(msg)

object Json {
    const val MAX_DEPTH = 32

    fun parse(text: String, maxDepth: Int = MAX_DEPTH): Any? {
        val p = Parser(text, maxDepth)
        p.ws()
        val v = p.value(0)
        p.ws()
        if (p.i != text.length) throw JsonException("basura despues del JSON")
        return v
    }

    /** Un objeto JSON en la raiz (el unico cuerpo que acepta la API). */
    fun parseObject(text: String): Map<String, Any?> {
        @Suppress("UNCHECKED_CAST")
        return parse(text) as? Map<String, Any?> ?: throw JsonException("se esperaba un objeto")
    }

    fun write(v: Any?): String = StringBuilder().also { writeTo(it, v) }.toString()

    private fun writeTo(sb: StringBuilder, v: Any?) {
        when (v) {
            null -> sb.append("null")
            is Boolean -> sb.append(if (v) "true" else "false")
            is Int, is Long, is Short, is Byte -> sb.append(v.toString())
            is Double -> num(sb, v)
            is Float -> num(sb, v.toDouble())
            is String -> str(sb, v)
            is Map<*, *> -> {
                sb.append('{')
                var first = true
                for ((k, value) in v) {
                    if (!first) sb.append(',')
                    first = false
                    str(sb, k.toString())
                    sb.append(':')
                    writeTo(sb, value)
                }
                sb.append('}')
            }
            is Iterable<*> -> {
                sb.append('[')
                var first = true
                for (e in v) {
                    if (!first) sb.append(',')
                    first = false
                    writeTo(sb, e)
                }
                sb.append(']')
            }
            is Array<*> -> writeTo(sb, v.asList())
            else -> str(sb, v.toString())
        }
    }

    private fun num(sb: StringBuilder, d: Double) {
        if (!d.isFinite()) { sb.append("null"); return }
        if (d == Math.rint(d) && Math.abs(d) < 9.007199254740992E15) sb.append(d.toLong()) else sb.append(d.toString())
    }

    private fun str(sb: StringBuilder, s: String) {
        sb.append('"')
        for (c in s) {
            when {
                c == '"' -> sb.append("\\\"")
                c == '\\' -> sb.append("\\\\")
                c == '\n' -> sb.append("\\n")
                c == '\r' -> sb.append("\\r")
                c == '\t' -> sb.append("\\t")
                c < ' ' || c == ' ' || c == ' ' || c == '\u007f' ->
                    sb.append("\\u").append(String.format("%04x", c.code))
                else -> sb.append(c)
            }
        }
        sb.append('"')
    }

    private class Parser(val s: String, val maxDepth: Int) {
        var i = 0

        fun ws() {
            while (i < s.length) {
                val c = s[i]
                if (c == ' ' || c == '\t' || c == '\n' || c == '\r') i++ else break
            }
        }

        fun value(depth: Int): Any? {
            if (depth > maxDepth) throw JsonException("JSON demasiado profundo")
            if (i >= s.length) throw JsonException("JSON incompleto")
            return when (val c = s[i]) {
                '{' -> obj(depth)
                '[' -> arr(depth)
                '"' -> string()
                't' -> lit("true", true)
                'f' -> lit("false", false)
                'n' -> lit("null", null)
                else -> if (c == '-' || c in '0'..'9') number() else throw JsonException("caracter inesperado")
            }
        }

        private fun lit(word: String, v: Any?): Any? {
            if (!s.startsWith(word, i)) throw JsonException("literal no valido")
            i += word.length
            return v
        }

        private fun obj(depth: Int): Map<String, Any?> {
            i++ // {
            val m = LinkedHashMap<String, Any?>()
            ws()
            if (i < s.length && s[i] == '}') { i++; return m }
            while (true) {
                ws()
                if (i >= s.length || s[i] != '"') throw JsonException("se esperaba una clave")
                val k = string()
                ws()
                if (i >= s.length || s[i] != ':') throw JsonException("se esperaba ':'")
                i++
                ws()
                m[k] = value(depth + 1)
                ws()
                if (i >= s.length) throw JsonException("objeto incompleto")
                when (s[i]) {
                    ',' -> i++
                    '}' -> { i++; return m }
                    else -> throw JsonException("se esperaba ',' o '}'")
                }
            }
        }

        private fun arr(depth: Int): List<Any?> {
            i++ // [
            val l = ArrayList<Any?>()
            ws()
            if (i < s.length && s[i] == ']') { i++; return l }
            while (true) {
                ws()
                l.add(value(depth + 1))
                ws()
                if (i >= s.length) throw JsonException("lista incompleta")
                when (s[i]) {
                    ',' -> i++
                    ']' -> { i++; return l }
                    else -> throw JsonException("se esperaba ',' o ']'")
                }
            }
        }

        private fun string(): String {
            i++ // "
            val sb = StringBuilder()
            while (true) {
                if (i >= s.length) throw JsonException("cadena sin cerrar")
                val c = s[i++]
                when {
                    c == '"' -> return sb.toString()
                    c == '\\' -> {
                        if (i >= s.length) throw JsonException("escape incompleto")
                        when (val e = s[i++]) {
                            '"' -> sb.append('"'); '\\' -> sb.append('\\'); '/' -> sb.append('/')
                            'b' -> sb.append('\b'); 'f' -> sb.append('\u000c'); 'n' -> sb.append('\n')
                            'r' -> sb.append('\r'); 't' -> sb.append('\t')
                            'u' -> {
                                if (i + 4 > s.length) throw JsonException("escape \\u incompleto")
                                val hex = s.substring(i, i + 4)
                                if (!hex.all { it in '0'..'9' || it in 'a'..'f' || it in 'A'..'F' }) throw JsonException("escape \\u no valido")
                                sb.append(hex.toInt(16).toChar())
                                i += 4
                            }
                            else -> throw JsonException("escape no valido: \\$e")
                        }
                    }
                    c < ' ' -> throw JsonException("caracter de control en una cadena")
                    else -> sb.append(c)
                }
            }
        }

        private fun number(): Any {
            val start = i
            if (s[i] == '-') i++
            if (i >= s.length) throw JsonException("numero incompleto")
            if (s[i] == '0') i++
            else if (s[i] in '1'..'9') { while (i < s.length && s[i] in '0'..'9') i++ }
            else throw JsonException("numero no valido")
            var isInt = true
            if (i < s.length && s[i] == '.') {
                isInt = false; i++
                if (i >= s.length || s[i] !in '0'..'9') throw JsonException("numero no valido")
                while (i < s.length && s[i] in '0'..'9') i++
            }
            if (i < s.length && (s[i] == 'e' || s[i] == 'E')) {
                isInt = false; i++
                if (i < s.length && (s[i] == '+' || s[i] == '-')) i++
                if (i >= s.length || s[i] !in '0'..'9') throw JsonException("numero no valido")
                while (i < s.length && s[i] in '0'..'9') i++
            }
            val t = s.substring(start, i)
            if (isInt) t.toLongOrNull()?.let { return it }
            return t.toDouble()
        }
    }
}

// ---------------------------------------------------------------- lectura comoda
/** Entero JSON exacto (Long o un Double sin decimales), o null. */
fun Any?.jsonLong(): Long? = when (this) {
    is Long -> this
    is Int -> this.toLong()
    is Double -> if (this == Math.rint(this) && Math.abs(this) < 9.007199254740992E15) this.toLong() else null
    else -> null
}
