package com.flexos.flexphone.cloud.media

import java.io.Closeable
import java.io.File
import java.io.IOException
import java.io.RandomAccessFile

/** Cabecera de un WAV. [tag] ya resuelve WAVE_FORMAT_EXTENSIBLE (0xFFFE) a su subformato. */
class WavInfo(
    val tag: Int,
    val channels: Int,
    val rate: Int,
    val bits: Int,
    val blockAlign: Int,
    val samplesPerBlock: Int,
    val dataPos: Long,
    /** Bytes de datos que de verdad hay (recortados al archivo si la cabecera miente). */
    val dataBytes: Long,
    val fact: Long?,
    val byteRate: Long,
    /** La cabecera declaraba mas datos de los que hay: el archivo esta cortado. */
    val truncated: Boolean,
) {
    val durationMs: Long?
        get() = when {
            tag == TAG_IMA && fact != null && rate > 0 -> fact * 1000 / rate
            tag == TAG_IMA && blockAlign > 0 && rate > 0 -> dataBytes / blockAlign * samplesPerBlock * 1000 / rate
            blockAlign > 0 && rate > 0 -> dataBytes / blockAlign * 1000 / rate
            else -> null
        }

    companion object {
        const val TAG_PCM = 1
        const val TAG_MS_ADPCM = 2
        const val TAG_FLOAT = 3
        const val TAG_ALAW = 6
        const val TAG_ULAW = 7
        const val TAG_IMA = 0x11
        const val TAG_MP3 = 0x55
    }
}

object Wav {
    fun parse(src: ByteSource): WavInfo? {
        val top = src.head(0, 12)
        if (top.size < 12 || !top.hasAt(0, "RIFF") || !top.hasAt(8, "WAVE")) return null
        var p = 12L
        var tag = 0; var ch = 0; var rate = 0; var bits = 0; var ba = 0; var spb = 0; var byteRate = 0L
        var fact: Long? = null
        var haveFmt = false
        var guard = 0
        while (p + 8 <= src.size && guard++ < 64) {
            val hd = src.bytes(p, 8) ?: break
            val id = hd.tag(0)
            val len = hd.u32le(4)
            val v = p + 8
            when (id) {
                "fmt " -> {
                    val n = minOf(len, 64L).toInt()
                    val f = src.bytes(v, n) ?: return null
                    if (n < 16) return null
                    tag = f.u16le(0); ch = f.u16le(2); rate = f.u32le(4).toInt(); byteRate = f.u32le(8)
                    ba = f.u16le(12); bits = f.u16le(14)
                    if (tag == 0xFFFE && n >= 26) tag = f.u16le(24)
                    if (tag == WavInfo.TAG_IMA && n >= 20) spb = f.u16le(18)
                    haveFmt = true
                }
                "fact" -> if (len >= 4) fact = src.bytes(v, 4)?.u32le(0)
                "data" -> {
                    if (!haveFmt) return null
                    val avail = maxOf(0L, src.size - v)
                    // 0 o 0xFFFFFFFF: WAV de flujo (sin tamano): vale lo que haya.
                    val declared = if (len == 0L || len == 0xFFFFFFFFL) avail else len
                    val have = minOf(declared, avail)
                    return WavInfo(tag, ch, rate, bits, ba, spb, v, have, fact, byteRate, declared > avail)
                }
            }
            p = v + len + (len and 1)
        }
        return null
    }
}

object ImaAdpcm {
    private val STEP = intArrayOf(
        7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80,
        88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
        876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
        5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086,
        29794, 32767,
    )
    private val IDX = intArrayOf(-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8)

    class State(var pred: Int = 0, var idx: Int = 0) {
        fun step(nib: Int): Int {
            val step = STEP[idx]
            var d = step shr 3
            if (nib and 4 != 0) d += step
            if (nib and 2 != 0) d += step shr 1
            if (nib and 1 != 0) d += step shr 2
            pred += if (nib and 8 != 0) -d else d
            pred = pred.coerceIn(-32768, 32767)
            idx = (idx + IDX[nib]).coerceIn(0, 88)
            return pred
        }
    }

    /**
     * Codifica UN bloque mono de [spb] muestras desde [pcm] (a partir de [from]; lo que falte se
     * rellena repitiendo el ultimo valor) en [out] ([blockAlign] bytes desde [outOff]). El primer
     * valor va en la cabecera del bloque y el indice de paso se arrastra de un bloque al siguiente
     * (igual que `FX.imaEncode`, que es lo que el firmware ya decodifica en las pruebas).
     */
    fun encodeBlockMono(st: State, pcm: ShortArray, from: Int, count: Int, spb: Int, out: ByteArray, outOff: Int) {
        val s0 = if (count > 0) pcm[from].toInt() else st.pred
        st.pred = s0
        out[outOff] = s0.toByte(); out[outOff + 1] = (s0 shr 8).toByte(); out[outOff + 2] = st.idx.toByte(); out[outOff + 3] = 0
        java.util.Arrays.fill(out, outOff + 4, outOff + 4 + (spb - 1 + 1) / 2, 0)
        for (i in 1 until spb) {
            val s = if (i < count) pcm[from + i].toInt() else st.pred
            var diff = s - st.pred
            var step = STEP[st.idx]
            var nib = 0
            if (diff < 0) { nib = 8; diff = -diff }
            if (diff >= step) { nib = nib or 4; diff -= step }
            step = step shr 1
            if (diff >= step) { nib = nib or 2; diff -= step }
            step = step shr 1
            if (diff >= step) nib = nib or 1
            st.step(nib)
            val k = i - 1
            val at = outOff + 4 + (k shr 1)
            if (k and 1 != 0) out[at] = (out[at].toInt() or (nib shl 4)).toByte() else out[at] = nib.toByte()
        }
    }

    /** Decodifica un bloque IMA de Microsoft (1 o 2 canales) a PCM intercalado; devuelve las tramas. */
    fun decodeBlock(b: ByteArray, off: Int, len: Int, channels: Int, out: ShortArray, outOff: Int): Int {
        if (channels !in 1..2 || len < 4 * channels) return -1
        val spb = (len - 4 * channels) * 2 / channels + 1
        val st = Array(channels) { c ->
            val s0 = (b.u16le(off + 4 * c) shl 16) shr 16
            State(s0, minOf(88, b.u8(off + 4 * c + 2)))
        }
        for (c in 0 until channels) out[outOff + c] = st[c].pred.toShort()
        var f = 1
        if (channels == 1) {
            for (k in 0 until spb - 1) {
                val byte = b.u8(off + 4 + (k shr 1))
                out[outOff + f++] = st[0].step(if (k and 1 != 0) byte shr 4 else byte and 15).toShort()
            }
        } else {
            val groups = (len - 8) / 8
            for (g in 0 until groups) for (c in 0 until 2) for (k in 0 until 4) {
                val byte = b.u8(off + 8 + g * 8 + c * 4 + k)
                out[outOff + (1 + g * 8 + k * 2) * 2 + c] = st[c].step(byte and 15).toShort()
                out[outOff + (1 + g * 8 + k * 2 + 1) * 2 + c] = st[c].step(byte shr 4).toShort()
            }
            f = 1 + groups * 8
        }
        return f
    }
}

/**
 * Escribe un WAV IMA ADPCM mono como el de la web (`FX.imaWav`): fmt de 20 bytes, `fact` con las
 * muestras reales, `data`. Los bloques se escriben a medida que llegan las muestras (nada de
 * guardar el audio entero) y al cerrar se rehace la cabecera. Lo valida el decodificador del
 * firmware (`tests/host/mediacheck wav`).
 */
class ImaWavWriter(file: File, private val rate: Int) : Closeable {
    private val raf = RandomAccessFile(file, "rw")
    private val blockAlign = MediaProfile.imaBlockAlign(rate, 1)
    private val spb = MediaProfile.imaSamplesPerBlock(blockAlign, 1)
    private val pending = ShortArray(spb)
    private var pendingN = 0
    private val block = ByteArray(blockAlign)
    private val st = ImaAdpcm.State()
    private var samples = 0L
    private var dataBytes = 0L
    private var closed = false

    init {
        require(rate in MediaProfile.AUDIO_RATE_MIN..MediaProfile.AUDIO_RATE_MAX) { "frecuencia no valida" }
        require(blockAlign <= MediaProfile.IMA_MAX_BLOCK_ALIGN)
        raf.setLength(0)
        raf.write(ByteArray(HEADER))
    }

    fun write(pcm: ShortArray, n: Int = pcm.size) {
        check(!closed)
        var i = 0
        while (i < n) {
            val take = minOf(spb - pendingN, n - i)
            System.arraycopy(pcm, i, pending, pendingN, take)
            pendingN += take; i += take; samples += take
            if (pendingN == spb) flushBlock(spb)
        }
    }

    private fun flushBlock(count: Int) {
        ImaAdpcm.encodeBlockMono(st, pending, 0, count, spb, block, 0)
        raf.write(block)
        dataBytes += blockAlign
        pendingN = 0
    }

    override fun close() {
        if (closed) return
        closed = true
        try {
            if (pendingN > 0) flushBlock(pendingN)
            raf.seek(0)
            raf.write(header())
            raf.fd.sync()
        } finally {
            raf.close()
        }
    }

    private fun header(): ByteArray {
        val b = ByteArray(HEADER)
        var p = 0
        fun s(t: String) { t.toByteArray(Charsets.ISO_8859_1).copyInto(b, p); p += 4 }
        fun u32(v: Long) { b[p] = v.toByte(); b[p + 1] = (v shr 8).toByte(); b[p + 2] = (v shr 16).toByte(); b[p + 3] = (v shr 24).toByte(); p += 4 }
        fun u16(v: Int) { b[p] = v.toByte(); b[p + 1] = (v shr 8).toByte(); p += 2 }
        s("RIFF"); u32(HEADER - 8 + dataBytes + (dataBytes and 1)); s("WAVE")
        s("fmt "); u32(20)
        u16(WavInfo.TAG_IMA); u16(1); u32(rate.toLong()); u32(rate.toLong() * blockAlign / spb); u16(blockAlign); u16(4); u16(2); u16(spb)
        s("fact"); u32(4); u32(samples)
        s("data"); u32(dataBytes)
        check(p == HEADER)
        return b
    }

    companion object { const val HEADER = 12 + 8 + 20 + 12 + 8 }
}

/**
 * Remuestreo MONO en flujo (sin tener el audio entero): al BAJAR la frecuencia promedia el tramo de
 * entrada que cae en cada muestra de salida (area; hace de filtro antialias) y al SUBIRLA
 * interpola linealmente.
 */
class MonoResampler(private val inRate: Int, private val outRate: Int, private val sink: (ShortArray, Int) -> Unit) {
    private val ratio = inRate.toDouble() / outRate
    private val out = ShortArray(4096)
    private var outN = 0
    private var inPos = 0.0           // posicion de entrada (en muestras) del inicio de la proxima salida
    private var consumed = 0L
    private var acc = 0.0
    private var accW = 0.0
    private var prev = 0.0
    private var havePrev = false

    fun push(pcm: ShortArray, n: Int) {
        if (inRate == outRate) { sink(pcm, n); return }
        for (i in 0 until n) feed(pcm[i].toDouble())
    }

    fun finish() {
        if (inRate != outRate && ratio > 1 && accW > 0.5) emit(acc / accW)
        flush()
    }

    private fun feed(x: Double) {
        if (ratio > 1) {
            // area: la muestra de entrada k cubre [k, k+1); la salida j cubre [j*r, (j+1)*r)
            var start = consumed.toDouble()
            val end = start + 1.0
            var left = 1.0
            while (left > 1e-9) {
                val outEnd = inPos + ratio
                val take = minOf(end, outEnd) - start
                acc += x * take; accW += take
                start += take; left -= take
                if (start >= outEnd - 1e-9) { emit(acc / accW); acc = 0.0; accW = 0.0; inPos = outEnd }
            }
            consumed++
        } else {
            if (havePrev) {
                // salidas que caen entre la muestra anterior (consumed-1) y esta (consumed)
                while (inPos < consumed) {
                    val t = inPos - (consumed - 1)
                    emit(prev + (x - prev) * t)
                    inPos += ratio
                }
            } else inPos = 0.0
            prev = x; havePrev = true
            consumed++
        }
    }

    private fun emit(v: Double) {
        out[outN++] = Math.round(v).coerceIn(-32768, 32767).toShort()
        if (outN == out.size) flush()
    }

    private fun flush() { if (outN > 0) { sink(out, outN); outN = 0 } }
}

/** Convierte un WAV (PCM 8/16/24/32, float32, µ-law, A-law, IMA) a PCM16 mono en flujo. */
object WavPcm {
    /** ¿Sabe este lector decodificar el WAV? */
    fun readable(w: WavInfo): Boolean = w.channels in 1..8 && w.blockAlign > 0 && when (w.tag) {
        WavInfo.TAG_PCM -> w.bits in intArrayOf(8, 16, 24, 32)
        WavInfo.TAG_FLOAT -> w.bits == 32
        WavInfo.TAG_ALAW, WavInfo.TAG_ULAW -> w.bits == 8
        WavInfo.TAG_IMA -> w.bits == 4 && w.channels in 1..2 && w.blockAlign >= 4 * w.channels && w.blockAlign <= 65536
        else -> false
    }

    /** Lee todo el PCM mono (mezclando canales) y lo entrega en trozos de [cb]. */
    fun readMono(src: ByteSource, w: WavInfo, cb: (ShortArray, Int) -> Unit) {
        if (!readable(w)) throw IOException("WAV no soportado (formato ${w.tag}, ${w.bits} bits)")
        val ch = w.channels
        val ba = w.blockAlign
        val chunkFrames = maxOf(1, 32768 / ba)
        val buf = ByteArray(chunkFrames * ba)
        val framesPerBlock = if (w.tag == WavInfo.TAG_IMA) w.samplesPerBlock.takeIf { it > 0 } ?: ((ba - 4 * ch) * 2 / ch + 1) else 1
        val out = ShortArray(chunkFrames * framesPerBlock)
        val tmp = ShortArray(framesPerBlock * ch)
        var pos = w.dataPos
        var left = w.dataBytes
        var remainingFrames = if (w.tag == WavInfo.TAG_IMA) (w.fact ?: Long.MAX_VALUE) else Long.MAX_VALUE
        while (left >= ba) {
            val want = minOf(buf.size.toLong(), left / ba * ba).toInt()
            val got = src.readAt(pos, buf, 0, want)
            if (got < ba) break
            val units = got / ba
            var o = 0
            for (u in 0 until units) {
                val off = u * ba
                when (w.tag) {
                    WavInfo.TAG_IMA -> {
                        val f = ImaAdpcm.decodeBlock(buf, off, ba, ch, tmp, 0)
                        if (f < 0) throw IOException("bloque IMA no valido")
                        val use = minOf(f.toLong(), remainingFrames).toInt()
                        for (i in 0 until use) {
                            var s = 0
                            for (c in 0 until ch) s += tmp[i * ch + c]
                            out[o++] = (s / ch).toShort()
                        }
                        remainingFrames -= use
                    }
                    else -> {
                        var s = 0.0
                        for (c in 0 until ch) s += sampleAt(buf, off + c * (w.bits / 8), w)
                        out[o++] = Math.round(s / ch).coerceIn(-32768, 32767).toInt().toShort()
                    }
                }
            }
            cb(out, o)
            pos += got; left -= got
        }
    }

    private fun sampleAt(b: ByteArray, i: Int, w: WavInfo): Double = when (w.tag) {
        WavInfo.TAG_PCM -> when (w.bits) {
            8 -> ((b.u8(i) - 128) shl 8).toDouble()
            16 -> ((b.u16le(i) shl 16) shr 16).toDouble()
            24 -> ((b.u8(i) or (b.u8(i + 1) shl 8) or (b.u8(i + 2) shl 16)) shl 8 shr 16).toDouble()
            else -> (b.u32le(i).toInt() shr 16).toDouble()
        }
        WavInfo.TAG_FLOAT -> {
            val f = java.lang.Float.intBitsToFloat(b.u32le(i).toInt()).coerceIn(-1f, 1f)
            (if (f < 0) f * 32768.0 else f * 32767.0)
        }
        WavInfo.TAG_ULAW -> ulaw(b.u8(i)).toDouble()
        else -> alaw(b.u8(i)).toDouble()
    }

    fun ulaw(code: Int): Int {
        val u = code.inv() and 0xFF
        var t = ((u and 0x0F) shl 3) + 0x84
        t = t shl ((u and 0x70) shr 4)
        return if (u and 0x80 != 0) 0x84 - t else t - 0x84
    }

    fun alaw(code: Int): Int {
        val a = code xor 0x55
        var t = (a and 0x0F) shl 4
        val seg = (a and 0x70) shr 4
        when (seg) {
            0 -> t += 8
            1 -> t += 0x108
            else -> { t += 0x108; t = t shl (seg - 1) }
        }
        return if (a and 0x80 != 0) t else -t
    }
}
