package com.flexos.flexphone.cloud.media

/** Lo que dice la cabecera de un JPEG (sin decodificarlo). */
class JpegInfo(
    val width: Int,
    val height: Int,
    val precision: Int,
    /** (id, h, v) de cada componente, en el orden del SOF. */
    val components: List<IntArray>,
    /** SOF0 (baseline) o SOF1 (secuencial extendido): lo unico que el P4 decodifica. */
    val sequentialHuffman: Boolean,
    val progressive: Boolean,
    /** Orientacion EXIF 1..8 (1 = sin girar o sin EXIF). */
    val orientation: Int,
    /** Hay marca EOI al final (un JPEG cortado no la tiene). */
    val hasEoi: Boolean,
) {
    /**
     * ¿Lo decodifica el firmware? (FlexOS_JPEG.cpp: 8 bits, 1 o 3 componentes, croma 1x1 y luma
     * 1x1/2x1/1x2/2x2; progresivo, aritmetico y 12 bits NO.)
     */
    val layoutDecodable: Boolean
        get() {
            if (!sequentialHuffman || precision != 8) return false
            return when (components.size) {
                1 -> true
                3 -> {
                    val y = components[0]
                    val okY = y[1] in 1..2 && y[2] in 1..2
                    val okC = components[1][1] == 1 && components[1][2] == 1 && components[2][1] == 1 && components[2][2] == 1
                    okY && okC
                }
                else -> false
            }
        }

    val dimensionsOk: Boolean
        get() = width in 1..MediaProfile.JPEG_DECODER_MAX_SIDE && height in 1..MediaProfile.JPEG_DECODER_MAX_SIDE

    val p4Decodable: Boolean get() = layoutDecodable && dimensionsOk
}

object Jpeg {
    /** null si no es un JPEG o la cabecera esta rota (sin SOF antes de los datos). */
    fun parse(src: ByteSource, maxHeader: Int = 1 shl 20): JpegInfo? {
        val head = src.head(0, minOf(src.size, maxHeader.toLong()).toInt())
        if (head.size < 4 || head.u8(0) != 0xFF || head.u8(1) != 0xD8) return null
        var p = 2
        var orientation = 1
        var sof: JpegInfo? = null
        var w = 0; var h = 0; var prec = 0
        var comps: List<IntArray> = emptyList()
        var seq = false; var prog = false
        while (p + 4 <= head.size) {
            if (head.u8(p) != 0xFF) return null
            while (p < head.size && head.u8(p) == 0xFF) p++            // relleno 0xFF
            if (p >= head.size) return null
            val m = head.u8(p++)
            if (m == 0xD9) break
            if (m == 0x01 || m in 0xD0..0xD7 || m == 0x00) continue
            if (p + 2 > head.size) return null
            val len = head.u16be(p)
            if (len < 2 || p + len > head.size) return null
            val seg = p + 2
            val segLen = len - 2
            when {
                m == 0xE1 && segLen >= 14 && head.hasAt(seg, "Exif\u0000\u0000") -> orientation = exifOrientation(head, seg + 6, segLen - 6) ?: orientation
                m in 0xC0..0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC -> {
                    if (segLen < 6) return null
                    prec = head.u8(seg)
                    h = head.u16be(seg + 1)
                    w = head.u16be(seg + 3)
                    val n = head.u8(seg + 5)
                    if (n < 1 || segLen < 6 + 3 * n) return null
                    comps = (0 until n).map { i -> intArrayOf(head.u8(seg + 6 + 3 * i), head.u8(seg + 7 + 3 * i) shr 4, head.u8(seg + 7 + 3 * i) and 15) }
                    seq = m == 0xC0 || m == 0xC1
                    prog = m == 0xC2
                    sof = JpegInfo(w, h, prec, comps, seq, prog, orientation, false)
                }
                m == 0xDA -> { p = head.size; break }                    // datos: ya hay SOF o no habra
            }
            p += len
            if (m == 0xDA) break
        }
        val f = sof ?: return null
        if (f.width <= 0 || f.height <= 0) return null
        return JpegInfo(f.width, f.height, f.precision, f.components, f.sequentialHuffman, f.progressive, orientation, endsWithEoi(src))
    }

    private fun endsWithEoi(src: ByteSource): Boolean {
        val n = minOf(src.size, 2048L).toInt()
        val t = src.head(src.size - n, n)
        for (i in t.size - 2 downTo 0) if (t.u8(i) == 0xFF && t.u8(i + 1) == 0xD9) return true
        return false
    }

    /** Orientacion del IFD0 de un EXIF (etiqueta 0x0112), o null. */
    private fun exifOrientation(b: ByteArray, tiff: Int, len: Int): Int? {
        if (len < 8) return null
        val le = when {
            b.hasAt(tiff, "II") -> true
            b.hasAt(tiff, "MM") -> false
            else -> return null
        }
        fun u16(i: Int) = if (le) b.u16le(i) else b.u16be(i)
        fun u32(i: Int) = if (le) b.u32le(i) else b.u32be(i)
        val ifd = u32(tiff + 4)
        if (ifd < 8 || ifd + 2 > len) return null
        val n = u16(tiff + ifd.toInt())
        for (k in 0 until n) {
            val e = tiff + ifd.toInt() + 2 + 12 * k
            if (e + 12 > tiff + len || e + 12 > b.size) return null
            if (u16(e) == 0x0112 && u16(e + 2) == 3) {
                val v = u16(e + 8)
                return if (v in 1..8) v else null
            }
        }
        return null
    }
}
