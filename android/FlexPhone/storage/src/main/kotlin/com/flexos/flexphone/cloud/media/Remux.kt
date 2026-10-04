package com.flexos.flexphone.cloud.media

import java.io.File
import java.io.IOException
import java.io.RandomAccessFile

/** Un fallo al preparar un archivo: lo bastante claro para ensenarselo a la persona. */
class MediaException(message: String, cause: Throwable? = null) : IOException(message, cause)

/**
 * RE-EMPAQUETAR sin recodificar (el proceso menos destructivo): los bytes de cada fotograma o muestra
 * se copian tal cual a un contenedor que el P4 abre. Si un solo fotograma no vale, se lanza
 * [MediaException] y quien llama recurre a recodificar.
 */
object Remux {

    /** JPEG-en-MOV/MP4 (`jpeg`) o AVI sin etiqueta de codec -> AVI MJPEG del perfil. */
    fun toAvi(src: ByteSource, out: File, cancelled: () -> Boolean = { false }, progress: (Int) -> Unit = {}) {
        val avi = Avi.parseHeader(src)
        if (avi != null) { aviToAvi(src, avi, out, cancelled, progress); return }
        val info = Mp4.parse(src, tables = true) ?: throw MediaException("No se pudo leer el contenedor.")
        val v = info.video ?: throw MediaException("No hay pista de vídeo.")
        if (v.codec.lowercase() != "jpeg") throw MediaException("El vídeo no es JPEG por fotograma.")
        val locs = Mp4.sampleLocations(v) ?: throw MediaException("Las tablas de muestras del archivo no cuadran.")
        val fps = ((v.fpsX100 ?: 0) / 100.0).let { Math.round(it).toInt() }.coerceIn(1, MediaProfile.VIDEO_MAX_FPS)
        if (v.width <= 0 || v.height <= 0) throw MediaException("El vídeo no declara su tamaño.")
        AviMjpegWriter(out, v.width, v.height, fps).use { w ->
            for ((i, l) in locs.withIndex()) {
                if (cancelled()) throw MediaException("Cancelado.")
                if (l[1] <= 0L || l[1] > MediaProfile.VIDEO_FRAME_MAX_BYTES) throw MediaException("El fotograma ${i + 1} pesa ${l[1]} bytes: no cabe en el perfil.")
                val b = src.bytes(l[0], l[1].toInt()) ?: throw MediaException("El fotograma ${i + 1} está fuera del archivo (¿cortado?).")
                checkFrame(b, v.width, v.height, i)
                w.addFrame(b)
                if (i % 64 == 0) progress(((i + 1) * 100L / locs.size).toInt())
            }
        }
    }

    private fun aviToAvi(src: ByteSource, h: AviHeader, out: File, cancelled: () -> Boolean, progress: (Int) -> Unit) {
        if (h.width <= 0 || h.height <= 0 || h.usPerFrame <= 0) throw MediaException("El AVI no declara tamaño o ritmo.")
        val fps = Math.round(1_000_000.0 / h.usPerFrame).toInt().coerceIn(1, MediaProfile.VIDEO_MAX_FPS)
        var failure: MediaException? = null
        AviMjpegWriter(out, h.width, h.height, fps).use { w ->
            Avi.forEachFrame(src, h) { i, b ->
                if (cancelled()) { failure = MediaException("Cancelado."); return@forEachFrame false }
                try {
                    if (b.isEmpty()) { if (w.frames == 0) throw MediaException("El primer fotograma está vacío.") ; return@forEachFrame true }
                    if (b.size > MediaProfile.VIDEO_FRAME_MAX_BYTES) throw MediaException("El fotograma ${i + 1} pesa ${b.size} bytes: no cabe en el perfil.")
                    checkFrame(b, h.width, h.height, i)
                    w.addFrame(b)
                    if (i % 64 == 0 && h.totalFrames > 0) progress(((i + 1) * 100L / h.totalFrames).toInt().coerceAtMost(100))
                    true
                } catch (e: MediaException) { failure = e; false }
            }
        }
        failure?.let { out.delete(); throw it }
    }

    private fun checkFrame(b: ByteArray, w: Int, h: Int, i: Int) {
        val j = Jpeg.parse(BytesSource(b)) ?: throw MediaException("El fotograma ${i + 1} no es un JPEG.")
        if (!j.p4Decodable) throw MediaException("El fotograma ${i + 1} no es JPEG baseline.")
        if (j.width != w || j.height != h) throw MediaException("El fotograma ${i + 1} no tiene el tamaño declarado.")
    }

    /** PCM de 16 bits little-endian dentro de MOV/MP4 (`sowt`) -> WAV PCM16. */
    fun pcmToWav(src: ByteSource, out: File, cancelled: () -> Boolean = { false }) {
        val info = Mp4.parse(src, tables = true) ?: throw MediaException("No se pudo leer el contenedor.")
        val a = info.audio ?: throw MediaException("No hay pista de audio.")
        if (a.codec.lowercase() != "sowt" || a.channels !in 1..2 || a.sampleRate <= 0) throw MediaException("El audio no es PCM de 16 bits.")
        val locs = Mp4.sampleLocations(a) ?: throw MediaException("Las tablas de muestras del archivo no cuadran.")
        RandomAccessFile(out, "rw").use { raf ->
            raf.setLength(0)
            raf.write(ByteArray(44))
            var data = 0L
            val frameBytes = a.channels * 2
            for (l in locs) {
                if (cancelled()) throw MediaException("Cancelado.")
                var left = l[1]
                var pos = l[0]
                while (left > 0) {
                    val n = minOf(left, 64L * 1024).toInt()
                    val b = src.bytes(pos, n) ?: throw MediaException("El audio está fuera del archivo (¿cortado?).")
                    raf.write(b); data += n; pos += n; left -= n
                }
            }
            if (data % frameBytes != 0L) { raf.setLength(raf.length() - data % frameBytes); data -= data % frameBytes }
            raf.seek(0)
            raf.write(pcmHeader(a.channels, a.sampleRate, data))
        }
    }

    fun pcmHeader(ch: Int, rate: Int, dataBytes: Long): ByteArray {
        val b = ByteArray(44)
        var p = 0
        fun s(t: String) { t.toByteArray(Charsets.ISO_8859_1).copyInto(b, p); p += 4 }
        fun u32(v: Long) { b[p] = v.toByte(); b[p + 1] = (v shr 8).toByte(); b[p + 2] = (v shr 16).toByte(); b[p + 3] = (v shr 24).toByte(); p += 4 }
        fun u16(v: Int) { b[p] = v.toByte(); b[p + 1] = (v shr 8).toByte(); p += 2 }
        s("RIFF"); u32(36 + dataBytes + (dataBytes and 1)); s("WAVE"); s("fmt "); u32(16)
        u16(1); u16(ch); u32(rate.toLong()); u32(rate.toLong() * ch * 2); u16(ch * 2); u16(16)
        s("data"); u32(dataBytes)
        return b
    }
}

/** WAV -> WAV IMA ADPCM mono del perfil, en Kotlin puro (frecuencia como mucho 22,05 kHz, nunca menos de 8 kHz). */
object WavConvert {
    fun toProfile(src: ByteSource, out: File, cancelled: () -> Boolean = { false }, progress: (Int) -> Unit = {}) {
        val w = Wav.parse(src) ?: throw MediaException("El WAV está dañado.")
        if (!WavPcm.readable(w)) throw MediaException("Este formato de WAV no se puede leer (formato ${w.tag}, ${w.bits} bits).")
        val rate = w.rate.coerceIn(MediaProfile.AUDIO_RATE_MIN, MediaProfile.AUDIO_TARGET_RATE)
        val total = w.dataBytes.coerceAtLeast(1)
        var done = 0L
        try {
            ImaWavWriter(out, rate).use { wr ->
                val rs = MonoResampler(w.rate, rate) { pcm, n -> wr.write(pcm, n) }
                WavPcm.readMono(src, w) { pcm, n ->
                    if (cancelled()) throw MediaException("Cancelado.")
                    rs.push(pcm, n)
                    done += n.toLong() * 2
                    progress((done * 100 / total).toInt().coerceAtMost(100))
                }
                rs.finish()
            }
        } catch (e: IOException) {
            out.delete(); throw if (e is MediaException) e else MediaException("No se pudo convertir el audio: ${e.message}", e)
        }
    }
}
