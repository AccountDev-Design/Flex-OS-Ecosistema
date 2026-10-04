package com.flexos.flexphone.cloud.media

import java.awt.image.BufferedImage
import java.io.ByteArrayOutputStream
import java.io.File
import javax.imageio.IIOImage
import javax.imageio.ImageIO
import javax.imageio.ImageWriteParam

/** Archivos multimedia SINTETICOS pero reales: lo bastante exactos para que el analizador (y el firmware) los juzguen. */
object MediaFixtures {
    init { System.setProperty("java.awt.headless", "true") }

    private fun img(w: Int, h: Int, seed: Int = 0, type: Int = BufferedImage.TYPE_INT_RGB): BufferedImage {
        val bi = BufferedImage(w, h, type)
        for (y in 0 until h) for (x in 0 until w) {
            val r = (x * 255 / maxOf(1, w - 1)); val g = (y * 255 / maxOf(1, h - 1)); val b = ((x + y + seed * 37) and 255)
            bi.setRGB(x, y, (0xFF shl 24) or (r shl 16) or (g shl 8) or b)
        }
        return bi
    }

    fun jpeg(w: Int, h: Int, seed: Int = 0, quality: Float = 0.8f, progressive: Boolean = false, orientation: Int = 1, gray: Boolean = false): ByteArray {
        val bi = if (gray) BufferedImage(w, h, BufferedImage.TYPE_BYTE_GRAY).also { g -> for (y in 0 until h) for (x in 0 until w) g.raster.setSample(x, y, 0, (x * 255 / maxOf(1, w - 1))) } else img(w, h, seed)
        val wr = ImageIO.getImageWritersByFormatName("jpeg").next()
        val p = wr.defaultWriteParam
        p.compressionMode = ImageWriteParam.MODE_EXPLICIT
        p.compressionQuality = quality
        p.progressiveMode = if (progressive) ImageWriteParam.MODE_DEFAULT else ImageWriteParam.MODE_DISABLED
        val bo = ByteArrayOutputStream()
        ImageIO.createImageOutputStream(bo).use { ios -> wr.output = ios; wr.write(null, IIOImage(bi, null, null), p) }
        wr.dispose()
        val raw = bo.toByteArray()
        return if (orientation == 1) raw else withExif(raw, orientation)
    }

    /** Inserta un APP1 Exif con solo la orientacion, justo tras SOI. */
    fun withExif(jpeg: ByteArray, orientation: Int): ByteArray {
        val tiff = byteArrayOf('M'.code.toByte(), 'M'.code.toByte(), 0, 42, 0, 0, 0, 8, 0, 1, 0x01, 0x12, 0, 3, 0, 0, 0, 1, 0, orientation.toByte(), 0, 0, 0, 0, 0, 0)
        val body = "Exif\u0000\u0000".toByteArray(Charsets.ISO_8859_1) + tiff
        val seg = byteArrayOf(0xFF.toByte(), 0xE1.toByte(), ((body.size + 2) shr 8).toByte(), (body.size + 2).toByte()) + body
        return jpeg.copyOfRange(0, 2) + seg + jpeg.copyOfRange(2, jpeg.size)
    }

    fun png(w: Int, h: Int): ByteArray = encode(img(w, h, type = BufferedImage.TYPE_INT_ARGB), "png")
    fun gif(w: Int, h: Int): ByteArray = encode(img(w, h), "gif")
    fun bmp(w: Int, h: Int): ByteArray = encode(img(w, h), "bmp")
    private fun encode(bi: BufferedImage, fmt: String): ByteArray { val bo = ByteArrayOutputStream(); check(ImageIO.write(bi, fmt, bo)); return bo.toByteArray() }

    /** WebP lossy minimo (solo cabeceras: lo que el analizador lee). */
    fun webpHeader(w: Int, h: Int): ByteArray {
        val b = ByteArray(40)
        "RIFF".toByteArray().copyInto(b); put32le(b, 4, 32); "WEBPVP8 ".toByteArray().copyInto(b, 8); put32le(b, 16, 20)
        b[23] = 0x9D.toByte(); b[24] = 0x01; b[25] = 0x2A; b[26] = w.toByte(); b[27] = (w shr 8).toByte(); b[28] = h.toByte(); b[29] = (h shr 8).toByte()
        return b
    }

    // ------------------------------------------------------------------ AVI
    fun mjpegFrames(w: Int, h: Int, n: Int, quality: Float = 0.6f): List<ByteArray> = (0 until n).map { jpeg(w, h, it, quality) }

    fun aviMjpeg(dir: File, name: String, w: Int, h: Int, n: Int, fps: Int, frames: List<ByteArray>? = null): File {
        val f = File(dir, name)
        val fr = frames ?: mjpegFrames(w, h, n)
        AviMjpegWriter(f, w, h, fps).use { wr -> for (b in fr) wr.addFrame(b) }
        return f
    }

    /** Cambia el codec declarado de un AVI MJPEG (strh.fccHandler en 112 y strf.biCompression en 188). */
    fun patchAviCodec(f: File, handler: String?, compression: String?) {
        java.io.RandomAccessFile(f, "rw").use { r ->
            if (handler != null) { r.seek(112); r.write(handler.padEnd(4, '\u0000').toByteArray(Charsets.ISO_8859_1)) }
            if (compression != null) { r.seek(188); r.write(compression.toByteArray(Charsets.ISO_8859_1)) }
        }
    }

    // ------------------------------------------------------------------ WAV
    fun wavPcm(rate: Int, ch: Int, bits: Int, seconds: Double, tag: Int = 1, freq: Double = 440.0): ByteArray {
        val frames = (rate * seconds).toInt()
        val bps = bits / 8
        val data = ByteArray(frames * ch * bps)
        for (i in 0 until frames) for (c in 0 until ch) {
            val v = Math.sin(2 * Math.PI * freq * (c + 1) * i / rate)
            val o = (i * ch + c) * bps
            when {
                tag == 3 -> put32le(data, o, java.lang.Float.floatToIntBits((v * 0.5).toFloat()).toLong())
                bits == 8 -> data[o] = ((v * 100).toInt() + 128).toByte()
                bits == 16 -> { val s = (v * 12000).toInt(); data[o] = s.toByte(); data[o + 1] = (s shr 8).toByte() }
                bits == 24 -> { val s = (v * 12000 * 256).toInt(); data[o] = s.toByte(); data[o + 1] = (s shr 8).toByte(); data[o + 2] = (s shr 16).toByte() }
                bits == 32 -> put32le(data, o, (v * 12000 * 65536).toInt().toLong())
            }
        }
        return wavFile(tag, ch, rate, bits, ch * bps, data, null)
    }

    fun wavFile(tag: Int, ch: Int, rate: Int, bits: Int, blockAlign: Int, data: ByteArray, declaredData: Long?): ByteArray {
        val bo = ByteArrayOutputStream()
        fun s(t: String) = bo.write(t.toByteArray(Charsets.ISO_8859_1))
        fun u32(v: Long) { for (i in 0 until 4) bo.write((v shr (8 * i)).toInt() and 0xFF) }
        fun u16(v: Int) { bo.write(v and 0xFF); bo.write((v shr 8) and 0xFF) }
        s("RIFF"); u32(36L + data.size); s("WAVE"); s("fmt "); u32(16)
        u16(tag); u16(ch); u32(rate.toLong()); u32(rate.toLong() * blockAlign); u16(blockAlign); u16(bits)
        s("data"); u32(declaredData ?: data.size.toLong()); bo.write(data)
        return bo.toByteArray()
    }

    fun ulawWav(rate: Int, seconds: Double): ByteArray = wavFile(7, 1, rate, 8, 1, ByteArray((rate * seconds).toInt()) { (0x80 + (it % 40)).toByte() }, null)

    fun wavIma(dir: File, name: String, rate: Int, seconds: Double): File {
        val f = File(dir, name)
        val n = (rate * seconds).toInt()
        val pcm = ShortArray(n) { (Math.sin(2 * Math.PI * 440 * it / rate) * 12000).toInt().toShort() }
        ImaWavWriter(f, rate).use { it.write(pcm) }
        return f
    }

    // ------------------------------------------------------------------ MP4 / MOV
    private fun u32(v: Long) = byteArrayOf((v shr 24).toByte(), (v shr 16).toByte(), (v shr 8).toByte(), v.toByte())
    private fun u16(v: Int) = byteArrayOf((v shr 8).toByte(), v.toByte())
    fun box(type: String, vararg parts: ByteArray): ByteArray {
        val body = parts.fold(ByteArray(0)) { a, b -> a + b }
        return u32(8L + body.size) + type.toByteArray(Charsets.ISO_8859_1) + body
    }
    private fun fullBox(type: String, vararg parts: ByteArray) = box(type, ByteArray(4), *parts)

    private val MATRIX = mapOf(
        0 to intArrayOf(0x10000, 0, 0, 0x10000), 90 to intArrayOf(0, 0x10000, -0x10000, 0),
        180 to intArrayOf(-0x10000, 0, 0, -0x10000), 270 to intArrayOf(0, -0x10000, 0x10000, 0),
    )

    private fun tkhd(w: Int, h: Int, rot: Int, audio: Boolean): ByteArray {
        val m = MATRIX[rot]!!
        val matrix = u32(m[0].toLong() and 0xFFFFFFFFL) + u32(m[1].toLong() and 0xFFFFFFFFL) + u32(0) + u32(m[2].toLong() and 0xFFFFFFFFL) + u32(m[3].toLong() and 0xFFFFFFFFL) + u32(0) + u32(0) + u32(0) + u32(0x40000000)
        return fullBox("tkhd", u32(0), u32(0), u32(1), u32(0), u32(0), ByteArray(8), u16(0), u16(0), u16(if (audio) 0x100 else 0), u16(0), matrix, u32(w.toLong() shl 16), u32(h.toLong() shl 16))
    }

    private fun mdhd(ts: Long, dur: Long) = fullBox("mdhd", u32(0), u32(0), u32(ts), u32(dur), u16(0x55C4), u16(0))
    private fun hdlr(t: String) = fullBox("hdlr", u32(0), t.toByteArray(), ByteArray(12), byteArrayOf(0))

    private fun stblCommon(sttsEntries: List<Pair<Int, Int>>, sizes: IntArray, fixedSize: Int, perChunk: Int, firstOffset: Long): ByteArray {
        val stts = fullBox("stts", u32(sttsEntries.size.toLong()), *sttsEntries.map { u32(it.first.toLong()) + u32(it.second.toLong()) }.toTypedArray())
        val stsc = fullBox("stsc", u32(1), u32(1), u32(perChunk.toLong()), u32(1))
        val n = sizes.size
        val stsz = if (fixedSize != 0) fullBox("stsz", u32(fixedSize.toLong()), u32(n.toLong()))
        else fullBox("stsz", u32(0), u32(n.toLong()), *sizes.map { u32(it.toLong()) }.toTypedArray())
        val chunks = (n + perChunk - 1) / perChunk
        val offs = ArrayList<Long>()
        var pos = firstOffset
        var s = 0
        for (c in 0 until chunks) { offs.add(pos); for (k in 0 until perChunk) { if (s < n) { pos += if (fixedSize != 0) fixedSize else sizes[s]; s++ } } }
        val stco = fullBox("stco", u32(chunks.toLong()), *offs.map { u32(it) }.toTypedArray())
        return stts + stsc + stsz + stco
    }

    /** MOV con una pista de vídeo `codec` (jpeg, avc1...) y, si [audioPcm], otra de audio `sowt`. */
    fun mov(codec: String, w: Int, h: Int, frames: List<ByteArray>, fps: Int = 12, rotation: Int = 0, brand: String = "qt  ", moovFirst: Boolean = false,
            audioPcm: ByteArray? = null, audioRate: Int = 22050, audioCh: Int = 1, withMoov: Boolean = true): ByteArray {
        val ts = 600L
        val delta = (ts / fps).toInt()
        val ftyp = box("ftyp", brand.toByteArray(), u32(0), brand.toByteArray())
        val mdatPayload = frames.fold(ByteArray(0)) { a, b -> a + b } + (audioPcm ?: ByteArray(0))
        val mdat = box("mdat", mdatPayload)
        fun moov(base: Long): ByteArray {
            val vEntry = u32(0) + codec.toByteArray() + ByteArray(6) + u16(1) + ByteArray(16) + u16(w) + u16(h) + u32(0x480000) + u32(0x480000) + u32(0) + u16(1) + ByteArray(32) + u16(24) + u16(0xFFFF)
            val stsdV = fullBox("stsd", u32(1), patchSize(vEntry))
            val vStbl = box("stbl", stsdV, stblCommon(listOf(frames.size to delta), frames.map { it.size }.toIntArray(), 0, 3, base))
            val vTrak = box("trak", tkhd(w, h, rotation, false), box("mdia", mdhd(ts, frames.size.toLong() * delta), hdlr("vide"), box("minf", vStbl)))
            var traks = if (frames.isEmpty()) ByteArray(0) else vTrak
            if (audioPcm != null) {
                val aEntry = u32(0) + "sowt".toByteArray() + ByteArray(6) + u16(1) + ByteArray(8) + u16(audioCh) + u16(16) + u16(0) + u16(0) + u32(audioRate.toLong() shl 16)
                val stsdA = fullBox("stsd", u32(1), patchSize(aEntry))
                val frameBytes = audioCh * 2
                val nFrames = audioPcm.size / frameBytes
                val aOffset = base + frames.sumOf { it.size }
                val aStbl = stblCommon(listOf(nFrames to 1), IntArray(nFrames) { frameBytes }, frameBytes, 1024, aOffset)
                traks += box("trak", tkhd(0, 0, 0, true), box("mdia", mdhd(audioRate.toLong(), nFrames.toLong()), hdlr("soun"), box("minf", box("stbl", stsdA, aStbl))))
            }
            return box("moov", fullBox("mvhd", u32(0), u32(0), u32(ts), u32(frames.size.toLong() * delta), u32(0x10000), u16(0x100), ByteArray(70)), traks)
        }
        val dataStart = ftyp.size + 8L
        return when {
            !withMoov -> ftyp + mdat
            moovFirst -> { val m0 = moov(0); ftyp + moov(ftyp.size + m0.size + 8L) + mdat }
            else -> ftyp + mdat + moov(dataStart)
        }
    }

    private fun patchSize(entry: ByteArray): ByteArray { val b = entry.copyOf(); val s = b.size.toLong(); for (i in 0 until 4) b[i] = (s shr (8 * (3 - i))).toByte(); return b }

    /** Un M4A (solo audio) con `codec` (mp4a...), sin muestras reales: lo que el analizador necesita. */
    fun m4a(codec: String, rate: Int, ch: Int, seconds: Int): ByteArray {
        val ftyp = box("ftyp", "M4A ".toByteArray(), u32(0), "M4A ".toByteArray())
        val aEntry = u32(0) + codec.toByteArray() + ByteArray(6) + u16(1) + ByteArray(8) + u16(ch) + u16(16) + u16(0) + u16(0) + u32(rate.toLong() shl 16)
        val stsd = fullBox("stsd", u32(1), patchSize(aEntry))
        val n = rate * seconds / 1024
        val stbl = box("stbl", stsd, fullBox("stts", u32(1), u32(n.toLong()), u32(1024)), fullBox("stsz", u32(0), u32(n.toLong()), *Array(n) { u32(100) }))
        val moov = box("moov", box("trak", tkhd(0, 0, 0, true), box("mdia", mdhd(rate.toLong(), rate.toLong() * seconds), hdlr("soun"), box("minf", stbl))))
        return ftyp + moov
    }

    fun heic(w: Int, h: Int): ByteArray {
        val ftyp = box("ftyp", "heic".toByteArray(), u32(0), "mif1heic".toByteArray())
        val ispe = fullBox("ispe", u32(w.toLong()), u32(h.toLong()))
        val meta = fullBox("meta", box("iprp", box("ipco", ispe)))
        return ftyp + meta
    }

    // ------------------------------------------------------------------ Matroska / WebM
    private fun ebml(id: ByteArray, payload: ByteArray) = id + byteArrayOf(0x10, (payload.size shr 16).toByte(), (payload.size shr 8).toByte(), payload.size.toByte()) + payload
    private fun eu(id: Int, v: Long): ByteArray {
        val idb = if (id > 0xFFFF) byteArrayOf((id shr 16).toByte(), (id shr 8).toByte(), id.toByte()) else if (id > 0xFF) byteArrayOf((id shr 8).toByte(), id.toByte()) else byteArrayOf(id.toByte())
        val n = if (v > 0xFFFFFF) 4 else if (v > 0xFFFF) 3 else if (v > 0xFF) 2 else 1
        return ebml(idb, ByteArray(n) { (v shr (8 * (n - 1 - it))).toByte() })
    }
    private fun ef(id: Int, v: Double, double: Boolean = false): ByteArray {
        val idb = if (id > 0xFF) byteArrayOf((id shr 8).toByte(), id.toByte()) else byteArrayOf(id.toByte())
        val bits = if (double) java.lang.Double.doubleToLongBits(v) else java.lang.Float.floatToIntBits(v.toFloat()).toLong()
        val n = if (double) 8 else 4
        return ebml(idb, ByteArray(n) { (bits shr (8 * (n - 1 - it))).toByte() })
    }
    private fun es(idHex: Int, s: String): ByteArray {
        val idb = if (idHex > 0xFF) byteArrayOf((idHex shr 8).toByte(), idHex.toByte()) else byteArrayOf(idHex.toByte())
        return ebml(idb, s.toByteArray())
    }

    fun matroska(docType: String, vCodec: String?, w: Int, h: Int, aCodec: String?, rate: Int = 48000, ch: Int = 2, seconds: Int = 10): ByteArray {
        val header = ebml(byteArrayOf(0x1A, 0x45, 0xDF.toByte(), 0xA3.toByte()), es(0x4282, docType))
        val info = ebml(byteArrayOf(0x15, 0x49, 0xA9.toByte(), 0x66), eu(0x2AD7B1, 1_000_000) + ef(0x4489, seconds * 1000.0, true))
        var tracks = ByteArray(0)
        if (vCodec != null) tracks += ebml(byteArrayOf(0xAE.toByte()), eu(0xD7, 1) + eu(0x83, 1) + es(0x86, vCodec) + eu(0x23E383, 33_333_333) +
            ebml(byteArrayOf(0xE0.toByte()), eu(0xB0, w.toLong()) + eu(0xBA, h.toLong())))
        if (aCodec != null) tracks += ebml(byteArrayOf(0xAE.toByte()), eu(0xD7, 2) + eu(0x83, 2) + es(0x86, aCodec) +
            ebml(byteArrayOf(0xE1.toByte()), ef(0xB5, rate.toDouble()) + eu(0x9F, ch.toLong())))
        val seg = info + ebml(byteArrayOf(0x16, 0x54, 0xAE.toByte(), 0x6B), tracks)
        return header + byteArrayOf(0x18, 0x53, 0x80.toByte(), 0x67, 0x01, 0xFF.toByte(), 0xFF.toByte(), 0xFF.toByte(), 0xFF.toByte(), 0xFF.toByte(), 0xFF.toByte(), 0xFF.toByte()) + seg
    }

    // ------------------------------------------------------------------ audio suelto
    fun mp3(rate: Int = 44100, ch: Int = 2, xingFrames: Long? = 1000, id3: Boolean = false): ByteArray {
        val b = ByteArray(4096)
        var p = 0
        if (id3) { "ID3".toByteArray().copyInto(b); b[3] = 3; b[9] = 20; p = 30 }
        val srIdx = when (rate) { 44100 -> 0; 48000 -> 1; else -> 2 }
        b[p] = 0xFF.toByte(); b[p + 1] = 0xFB.toByte(); b[p + 2] = ((9 shl 4) or (srIdx shl 2)).toByte(); b[p + 3] = (if (ch == 1) 0xC0 else 0x00).toByte()
        if (xingFrames != null) {
            val x = p + 4 + (if (ch == 1) 17 else 32)
            "Xing".toByteArray().copyInto(b, x); b[x + 7] = 1; for (i in 0 until 4) b[x + 8 + i] = (xingFrames shr (8 * (3 - i))).toByte()
        }
        return b
    }

    fun flac(rate: Int, ch: Int, totalSamples: Long): ByteArray {
        val b = ByteArray(64)
        "fLaC".toByteArray().copyInto(b); b[4] = 0x80.toByte(); b[7] = 34
        b[18] = (rate shr 12).toByte(); b[19] = (rate shr 4).toByte(); b[20] = (((rate and 15) shl 4) or ((ch - 1) shl 1)).toByte()
        b[21] = ((totalSamples shr 32) and 15).toByte(); for (i in 0 until 4) b[22 + i] = (totalSamples shr (8 * (3 - i))).toByte()
        return b
    }

    fun oggOpus(ch: Int): ByteArray { val b = ByteArray(128); "OggS".toByteArray().copyInto(b); "OpusHead".toByteArray().copyInto(b, 28); b[37] = ch.toByte(); return b }
    fun oggVorbis(ch: Int, rate: Int): ByteArray { val b = ByteArray(128); "OggS".toByteArray().copyInto(b); b[28] = 1; "vorbis".toByteArray().copyInto(b, 29); b[39] = ch.toByte(); put32le(b, 40, rate.toLong()); return b }
    fun aacAdts(rateIdx: Int, ch: Int): ByteArray { val b = ByteArray(64); b[0] = 0xFF.toByte(); b[1] = 0xF1.toByte(); b[2] = ((1 shl 6) or (rateIdx shl 2) or (ch shr 2)).toByte(); b[3] = ((ch and 3) shl 6).toByte(); return b }

    private fun put32le(b: ByteArray, at: Int, v: Long) { for (i in 0 until 4) b[at + i] = (v shr (8 * i)).toByte() }
}
