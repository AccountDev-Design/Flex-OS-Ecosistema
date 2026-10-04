package com.flexos.flexphone.cloud.media

import java.io.Closeable
import java.io.File
import java.io.IOException
import java.io.RandomAccessFile

/** Cabecera de un AVI (solo `hdrl`): lo que hace falta para juzgarlo sin leerlo entero. */
class AviHeader(
    val width: Int,
    val height: Int,
    val usPerFrame: Long,
    val totalFrames: Long,
    val suggestedBuffer: Long,
    /** fccHandler de la primera pista de vídeo, en minusculas ("mjpg"...), o "" si no hay. */
    val videoHandler: String,
    /** biCompression de su `strf`, en minusculas (por si el fccHandler viene a cero). */
    val videoCompression: String,
    val audioFormatTag: Int?,
    val audioChannels: Int?,
    val audioRate: Int?,
    val openDml: Boolean,
    /** Posicion del fourcc 'movi' (los desplazamientos de idx1 cuentan desde aqui). */
    val moviPos: Long,
    val moviEnd: Long,
) {
    val fpsX100: Int? get() = if (usPerFrame in 1..9_999_999) (100_000_000L / usPerFrame).toInt() else null
    val durationMs: Long? get() = if (usPerFrame in 1..9_999_999 && totalFrames > 0) totalFrames * usPerFrame / 1000 else null
}

/** Lo que dice el indice `idx1` (si lo hay) sobre los fotogramas de vídeo. */
class AviIndexInfo(val videoFrames: Int, val maxFrameBytes: Int)

object Avi {
    private const val MAX_HDRL = 256 * 1024
    private const val MAX_FRAME = 8L shl 20

    fun parseHeader(src: ByteSource): AviHeader? {
        val top = src.head(0, 12)
        if (top.size < 12 || !top.hasAt(0, "RIFF") || !top.hasAt(8, "AVI ")) return null
        var p = 12L
        var w = 0; var h = 0
        var us = 0L; var frames = 0L; var sugg = 0L
        var vHandler = ""; var vComp = ""
        var aTag: Int? = null; var aCh: Int? = null; var aRate: Int? = null
        var odml = false
        var moviPos = -1L; var moviEnd = 0L
        var guard = 0
        while (p + 12 <= src.size && guard++ < 64) {
            val hd = src.bytes(p, 12) ?: break
            val id = hd.tag(0)
            val len = hd.u32le(4)
            if (id != "LIST") {
                p += 8 + len + (len and 1)
                continue
            }
            val form = hd.tag(8)
            if (form == "movi") {
                moviPos = p + 8
                moviEnd = minOf(src.size, p + 8 + len)
                break
            }
            if (form != "hdrl") { p += 8 + len + (len and 1); continue }
            val body = src.bytes(p + 12, minOf(len - 4, MAX_HDRL.toLong()).toInt().coerceAtLeast(0), MAX_HDRL) ?: break
            var q = 0
            var curType = ""
            while (q + 8 <= body.size) {
                val cid = body.tag(q)
                val clen = body.u32le(q + 4)
                if (cid == "LIST") {
                    if (q + 12 > body.size) break
                    val f = body.tag(q + 8)
                    if (f == "odml") odml = true
                    q += 12                      // entra: strl / odml contienen sus propios trozos
                    continue
                }
                val d = q + 8
                when (cid) {
                    "avih" -> if (d + 40 <= body.size) {
                        us = body.u32le(d); frames = body.u32le(d + 16); sugg = body.u32le(d + 28)
                        w = body.u32le(d + 32).toInt(); h = body.u32le(d + 36).toInt()
                    }
                    "strh" -> if (d + 36 <= body.size) {
                        curType = body.tag(d)
                        if (curType == "vids" && vHandler.isEmpty()) {
                            vHandler = body.tag(d + 4).lowercase()
                            val scale = body.u32le(d + 20); val rate = body.u32le(d + 24)
                            if (scale > 0 && rate > 0) us = scale * 1_000_000L / rate
                            val l = body.u32le(d + 32)
                            if (l > 0 && frames == 0L) frames = l
                        }
                    }
                    "strf" -> {
                        if (curType == "vids" && vComp.isEmpty() && d + 20 <= body.size) {
                            vComp = body.tag(d + 16).lowercase()
                            if (w == 0) w = body.u32le(d + 4).toInt()
                            if (h == 0) h = body.u32le(d + 8).toInt()
                        } else if (curType == "auds" && aTag == null && d + 16 <= body.size) {
                            aTag = body.u16le(d); aCh = body.u16le(d + 2); aRate = body.u32le(d + 4).toInt()
                        }
                    }
                }
                q = d + clen.toInt().coerceAtLeast(0) + (clen.toInt() and 1)
                if (clen > body.size) break
            }
            p += 8 + len + (len and 1)
        }
        if (moviPos < 0) return null
        // OpenDML grande: otro RIFF 'AVIX' tras el primero.
        val riffLen = top.u32le(4)
        val after = 8 + riffLen + (riffLen and 1)
        if (after + 12 <= src.size) {
            val t = src.bytes(after, 12)
            if (t != null && t.hasAt(0, "RIFF") && t.hasAt(8, "AVIX")) odml = true
        }
        return AviHeader(w, h, us, frames, sugg, vHandler, vComp, aTag, aCh, aRate, odml, moviPos, moviEnd)
    }

    /** Lee `idx1` (si esta justo tras `movi`) y cuenta los fotogramas de vídeo y el mayor. */
    fun readIndex(src: ByteSource, h: AviHeader): AviIndexInfo? {
        val t = src.bytes(h.moviEnd + (h.moviEnd and 1), 8) ?: return null
        if (!t.hasAt(0, "idx1")) return null
        val len = t.u32le(4)
        val start = h.moviEnd + (h.moviEnd and 1) + 8
        val n = minOf(len, src.size - start, 8L shl 20) / 16
        if (n <= 0) return null
        val buf = src.bytes(start, (n * 16).toInt(), 8 shl 20) ?: return null
        var count = 0; var max = 0L
        for (i in 0 until n.toInt()) {
            val o = i * 16
            val c = buf.tag(o)
            if (c.length == 4 && c[2] == 'd' && (c[3] == 'c' || c[3] == 'b')) {
                count++
                val sz = buf.u32le(o + 12)
                if (sz > max) max = sz
            }
        }
        return AviIndexInfo(count, max.coerceAtMost(Int.MAX_VALUE.toLong()).toInt())
    }

    /**
     * Recorre los fotogramas de vídeo ('##dc'/'##db') en orden, como el demux del firmware (sin
     * necesitar idx1). [cb] devuelve false para parar. Trozos vacios = fotograma repetido.
     */
    fun forEachFrame(src: ByteSource, h: AviHeader, cb: (index: Int, jpeg: ByteArray) -> Boolean) {
        var p = h.moviPos + 4
        var n = 0
        while (p + 8 <= h.moviEnd) {
            val hd = src.bytes(p, 8) ?: return
            val id = hd.tag(0)
            val len = hd.u32le(4)
            if (id == "LIST") { p += 12; continue }                        // 'LIST rec ': entra
            val isVideo = id.length == 4 && id[2] == 'd' && (id[3] == 'c' || id[3] == 'b')
            if (isVideo) {
                if (len > MAX_FRAME) throw IOException("fotograma absurdo")
                val body = src.bytes(p + 8, len.toInt(), 8 shl 20) ?: return
                if (!cb(n++, body)) return
            }
            p += 8 + len + (len and 1)
        }
    }
}

/**
 * Escribe un AVI MJPEG como el de la web de Flex OS (`FX.aviMux`): el mismo diseño de cabecera de
 * 224 bytes, solo vídeo, `idx1` al final. Lo valida el demux del propio firmware
 * (`tests/host/mediacheck avi`). Sin audio: el P4 no reproduce el audio de un AVI.
 *
 * Escribe los fotogramas en el archivo a medida que llegan (nunca el vídeo entero en memoria) y
 * al cerrar pone el indice y rehace la cabecera con los totales.
 */
class AviMjpegWriter(
    private val file: File,
    private val width: Int,
    private val height: Int,
    private val fps: Int,
) : Closeable {
    private val raf = RandomAccessFile(file, "rw")
    private val index = ArrayList<IntArray>()          // (offset desde 'movi', longitud)
    private var maxFrame = 0
    private var moviBytes = 4L
    private var closed = false

    init {
        require(width in 1..MediaProfile.JPEG_DECODER_MAX_SIDE && height in 1..MediaProfile.JPEG_DECODER_MAX_SIDE) { "dimensiones no validas" }
        require(fps in 1..60) { "fps no valido" }
        raf.setLength(0)
        raf.write(ByteArray(224))
    }

    val frames: Int get() = index.size

    fun addFrame(jpeg: ByteArray) {
        check(!closed) { "AVI cerrado" }
        if (jpeg.isEmpty()) throw IOException("fotograma vacio")
        raf.write(chunkHeader("00dc", jpeg.size))
        raf.write(jpeg)
        if (jpeg.size and 1 == 1) raf.write(0)
        index.add(intArrayOf((moviBytes).toInt(), jpeg.size))
        moviBytes += 8 + jpeg.size + (jpeg.size and 1)
        if (jpeg.size > maxFrame) maxFrame = jpeg.size
        if (moviBytes > 0x7FFF_0000L) throw IOException("AVI demasiado grande (OpenDML no esta soportado)")
    }

    override fun close() {
        if (closed) return
        closed = true
        try {
            val idx = ByteArray(8 + 16 * index.size)
            "idx1".toByteArray(Charsets.ISO_8859_1).copyInto(idx)
            put32(idx, 4, 16L * index.size)
            for ((i, e) in index.withIndex()) {
                val o = 8 + 16 * i
                "00dc".toByteArray(Charsets.ISO_8859_1).copyInto(idx, o)
                put32(idx, o + 4, 0x10); put32(idx, o + 8, e[0].toLong()); put32(idx, o + 12, e[1].toLong())
            }
            raf.write(idx)
            raf.seek(0)
            raf.write(header(index.size, maxFrame, moviBytes))
            raf.fd.sync()
        } finally {
            raf.close()
        }
    }

    private fun chunkHeader(id: String, len: Int): ByteArray {
        val b = ByteArray(8)
        id.toByteArray(Charsets.ISO_8859_1).copyInto(b)
        put32(b, 4, len.toLong())
        return b
    }

    private fun header(n: Int, maxF: Int, moviLen: Long): ByteArray {
        val hdrlLen = 4 + 64 + 124
        val riffLen = 4L + (8 + hdrlLen) + (8 + moviLen) + (8 + 16L * n)
        val b = ByteArray(224)
        var p = 0
        fun s(t: String) { t.toByteArray(Charsets.ISO_8859_1).copyInto(b, p); p += 4 }
        fun u32(v: Long) { put32(b, p, v); p += 4 }
        fun u16(v: Int) { b[p] = v.toByte(); b[p + 1] = (v shr 8).toByte(); p += 2 }
        s("RIFF"); u32(riffLen); s("AVI ")
        s("LIST"); u32(hdrlLen.toLong()); s("hdrl")
        s("avih"); u32(56)
        u32(1_000_000L / fps); u32(Math.ceil(maxF.toDouble() * fps).toLong()); u32(0); u32(0x10); u32(n.toLong()); u32(0); u32(1)
        u32(maxF + 8L); u32(width.toLong()); u32(height.toLong()); u32(0); u32(0); u32(0); u32(0)
        s("LIST"); u32(4 + 64 + 48); s("strl")
        s("strh"); u32(56)
        s("vids"); s("MJPG"); u32(0); u16(0); u16(0); u32(0); u32(1); u32(fps.toLong()); u32(0); u32(n.toLong()); u32(maxF + 8L)
        u32(0xFFFFFFFFL); u32(0); u16(0); u16(0); u16(width); u16(height)
        s("strf"); u32(40)
        u32(40); u32(width.toLong()); u32(height.toLong()); u16(1); u16(24); s("MJPG"); u32(width.toLong() * height * 3); u32(0); u32(0); u32(0); u32(0)
        s("LIST"); u32(moviLen); s("movi")
        check(p == 224) { "cabecera AVI de $p bytes" }
        return b
    }

    private fun put32(b: ByteArray, at: Int, v: Long) {
        b[at] = v.toByte(); b[at + 1] = (v shr 8).toByte(); b[at + 2] = (v shr 16).toByte(); b[at + 3] = (v shr 24).toByte()
    }
}
