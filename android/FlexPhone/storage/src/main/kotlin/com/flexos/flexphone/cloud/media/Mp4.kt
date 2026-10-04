package com.flexos.flexphone.cloud.media

/** Una pista de un MP4/MOV (solo lo que hace falta para juzgarla y, si procede, remuxarla). */
class Mp4Track(
    /** `vide`, `soun`... */
    val handler: String,
    val timescale: Long,
    val duration: Long,
    /** fourcc de la primera entrada `stsd`: avc1, hvc1, mp4a, jpeg, sowt... */
    val codec: String,
    val width: Int,
    val height: Int,
    /** Giro que pide la matriz de la pista: 0, 90, 180 o 270 (un movil en vertical graba girado). */
    val rotation: Int,
    val channels: Int,
    val sampleRate: Int,
    val sampleBits: Int,
    val sampleCount: Long,
    /** (cuenta, duracion) de `stts`; vacio si no se leyo. */
    val stts: List<LongArray>,
    val sampleSizes: IntArray?,
    /** (primer trozo, muestras por trozo) de `stsc`. */
    val stsc: List<LongArray>?,
    val chunkOffsets: LongArray?,
) {
    val durationMs: Long? get() = if (timescale > 0 && duration > 0) duration * 1000 / timescale else null
    val fpsX100: Int?
        get() {
            val total = stts.sumOf { it[0] * it[1] }
            val n = stts.sumOf { it[0] }
            return if (total > 0 && n > 0 && timescale > 0) (n * timescale * 100 / total).toInt() else null
        }
}

class Mp4Info(val brand: String, val tracks: List<Mp4Track>) {
    val video: Mp4Track? get() = tracks.firstOrNull { it.handler == "vide" }
    val audio: Mp4Track? get() = tracks.firstOrNull { it.handler == "soun" }
}

object Mp4 {
    private const val MAX_MOOV = 64 shl 20

    class Box(val type: String, val start: Int, val payload: Int, val end: Int)

    /** Cajas hijas de [b] en [from, to). Una caja rota o que se sale ([to]) corta la lista. */
    private fun children(b: ByteArray, from: Int, to: Int): List<Box> {
        val out = ArrayList<Box>()
        var p = from
        while (p + 8 <= to) {
            var size = b.u32be(p)
            var hdr = 8
            if (size == 1L) { if (p + 16 > to) break; size = b.u64be(p + 8); hdr = 16 } else if (size == 0L) size = (to - p).toLong()
            if (size < hdr || p + size > to) break
            out.add(Box(b.tag(p + 4), p, p + hdr, (p + size).toInt()))
            p = (p + size).toInt()
        }
        return out
    }

    /** Marca (`ftyp`) y posicion de `moov`, saltando de caja en caja sin leer el cuerpo de `mdat`. */
    fun locate(src: ByteSource): Triple<String, Long, Long>? {
        var p = 0L
        var brand: String? = null
        var guard = 0
        while (p + 8 <= src.size && guard++ < 256) {
            val h = src.bytes(p, 16.coerceAtMost((src.size - p).toInt())) ?: return null
            var size = h.u32be(0)
            val type = h.tag(4)
            var hdr = 8
            if (size == 1L) { if (h.size < 16) return null; size = h.u64be(8); hdr = 16 } else if (size == 0L) size = src.size - p
            if (size < hdr) return null
            if (type == "ftyp" && h.size >= 12) brand = h.tag(8)
            if (type == "moov") return Triple(brand ?: "", p + hdr, p + size)
            p += size
        }
        return null
    }

    fun brandOf(src: ByteSource): String? {
        val h = src.head(0, 16)
        return if (h.size >= 12 && h.hasAt(4, "ftyp")) h.tag(8) else null
    }

    /** Lee las pistas. [tables]: tambien tamanos, trozos y posiciones de muestras (para remuxar). */
    fun parse(src: ByteSource, tables: Boolean = false): Mp4Info? {
        val (brand, from, to) = locate(src) ?: return null
        val len = to - from
        if (len < 8 || len > MAX_MOOV) return null
        val moov = src.bytes(from, len.toInt(), MAX_MOOV) ?: return null
        val tracks = ArrayList<Mp4Track>()
        for (trak in children(moov, 0, moov.size)) {
            if (trak.type != "trak") continue
            parseTrack(moov, trak, tables)?.let { tracks.add(it) }
        }
        return Mp4Info(brand, tracks)
    }

    private fun parseTrack(b: ByteArray, trak: Box, tables: Boolean): Mp4Track? {
        var rotation = 0
        var tw = 0; var th = 0
        var handler = ""
        var timescale = 0L; var duration = 0L
        var codec = ""; var width = 0; var height = 0; var ch = 0; var rate = 0; var bits = 0
        var sampleCount = 0L
        var stts: List<LongArray> = emptyList()
        var sizes: IntArray? = null
        var stsc: List<LongArray>? = null
        var offsets: LongArray? = null
        for (c in children(b, trak.payload, trak.end)) {
            when (c.type) {
                "tkhd" -> {
                    val v = b.u8(c.payload)
                    val m = c.payload + if (v == 1) 52 else 40
                    if (m + 44 <= c.end) {
                        val a = b.u32be(m).toInt(); val bb = b.u32be(m + 4).toInt()
                        rotation = when {
                            a == 0x10000 && bb == 0 -> 0
                            a == 0 && bb == 0x10000 -> 90
                            a == -0x10000 && bb == 0 -> 180
                            a == 0 && bb == -0x10000 -> 270
                            else -> 0
                        }
                        tw = (b.u32be(m + 36) shr 16).toInt(); th = (b.u32be(m + 40) shr 16).toInt()
                    }
                }
                "mdia" -> for (m in children(b, c.payload, c.end)) {
                    when (m.type) {
                        "mdhd" -> {
                            val v = b.u8(m.payload)
                            if (v == 1 && m.payload + 32 <= m.end) { timescale = b.u32be(m.payload + 20); duration = b.u64be(m.payload + 24) }
                            else if (m.payload + 20 <= m.end) { timescale = b.u32be(m.payload + 12); duration = b.u32be(m.payload + 16) }
                        }
                        "hdlr" -> if (m.payload + 12 <= m.end) handler = b.tag(m.payload + 8)
                        "minf" -> for (s in children(b, m.payload, m.end)) if (s.type == "stbl") for (t in children(b, s.payload, s.end)) {
                            when (t.type) {
                                "stsd" -> if (t.payload + 8 + 36 <= t.end) {
                                    val e = t.payload + 8
                                    codec = b.tag(e + 4)
                                    if (handler == "vide") { width = b.u16be(e + 32); height = b.u16be(e + 34) }
                                    else if (handler == "soun") { ch = b.u16be(e + 24); bits = b.u16be(e + 26); rate = (b.u32be(e + 32) shr 16).toInt() }
                                }
                                "stts" -> if (t.payload + 8 <= t.end) {
                                    val n = b.u32be(t.payload + 4).toInt().coerceIn(0, 1 shl 20)
                                    val list = ArrayList<LongArray>()
                                    for (i in 0 until n) { val o = t.payload + 8 + 8 * i; if (o + 8 > t.end) break; list.add(longArrayOf(b.u32be(o), b.u32be(o + 4))) }
                                    stts = list
                                }
                                "stsz" -> if (t.payload + 12 <= t.end) {
                                    val fixed = b.u32be(t.payload + 4)
                                    sampleCount = b.u32be(t.payload + 8)
                                    if (tables) {
                                        if (sampleCount > (1 shl 24)) return null
                                        val n = sampleCount.toInt()
                                        sizes = IntArray(n) { i -> if (fixed != 0L) fixed.toInt() else if (t.payload + 12 + 4 * i + 4 <= t.end) b.u32be(t.payload + 12 + 4 * i).toInt() else -1 }
                                    }
                                }
                                "stsc" -> if (tables && t.payload + 8 <= t.end) {
                                    val n = b.u32be(t.payload + 4).toInt().coerceIn(0, 1 shl 20)
                                    val list = ArrayList<LongArray>()
                                    for (i in 0 until n) { val o = t.payload + 8 + 12 * i; if (o + 12 > t.end) break; list.add(longArrayOf(b.u32be(o), b.u32be(o + 4))) }
                                    stsc = list
                                }
                                "stco", "co64" -> if (tables && t.payload + 8 <= t.end) {
                                    val n = b.u32be(t.payload + 4).toInt().coerceIn(0, 1 shl 22)
                                    val w = if (t.type == "co64") 8 else 4
                                    val list = LongArray(n)
                                    for (i in 0 until n) { val o = t.payload + 8 + w * i; if (o + w > t.end) return null; list[i] = if (w == 8) b.u64be(o) else b.u32be(o) }
                                    offsets = list
                                }
                            }
                        }
                    }
                }
            }
        }
        if (handler.isEmpty() || codec.isEmpty()) return null
        if (handler == "vide" && width == 0) { width = tw; height = th }
        return Mp4Track(handler, timescale, duration, codec, width, height, rotation, ch, rate, bits, sampleCount, stts, sizes, stsc, offsets)
    }

    /** (posicion, tamano) de cada muestra de una pista con tablas, en orden. null si las tablas no cuadran. */
    fun sampleLocations(t: Mp4Track): List<LongArray>? {
        val sizes = t.sampleSizes ?: return null
        val stsc = t.stsc ?: return null
        val offs = t.chunkOffsets ?: return null
        if (stsc.isEmpty() || offs.isEmpty()) return null
        val out = ArrayList<LongArray>(sizes.size)
        var s = 0
        for (chunk in offs.indices) {
            val chunkNo = chunk + 1L
            var per = 0L
            for (e in stsc) if (e[0] <= chunkNo) per = e[1] else break
            var pos = offs[chunk]
            for (k in 0 until per) {
                if (s >= sizes.size) return out
                val sz = sizes[s]
                if (sz < 0) return null
                out.add(longArrayOf(pos, sz.toLong()))
                pos += sz; s++
            }
        }
        return if (out.size == sizes.size) out else null
    }

    /** Dimensiones de una imagen HEIF/AVIF (caja `ispe` dentro de `meta`), o null. */
    fun heifSize(src: ByteSource): IntArray? {
        var p = 0L
        var guard = 0
        while (p + 8 <= src.size && guard++ < 64) {
            val h = src.bytes(p, 8) ?: return null
            var size = h.u32be(0)
            if (size == 0L) size = src.size - p
            if (size < 8) return null
            if (h.tag(4) == "meta") {
                val n = minOf(size, 1L shl 20).toInt()
                val body = src.bytes(p, n) ?: return null
                return findIspe(body, 12, n)
            }
            p += size
        }
        return null
    }

    private fun findIspe(b: ByteArray, from: Int, to: Int): IntArray? {
        for (c in children(b, from, to)) {
            when (c.type) {
                "ispe" -> if (c.payload + 12 <= c.end) return intArrayOf(b.u32be(c.payload + 4).toInt(), b.u32be(c.payload + 8).toInt())
                "iprp", "ipco" -> findIspe(b, c.payload, c.end)?.let { return it }
            }
        }
        return null
    }

}
