package com.flexos.flexphone.cloud.media

import java.io.File
import java.io.IOException

/**
 * Lo que sabe hacer ESTE telefono para llevar un archivo al perfil del P4. La implementacion normal es
 * [StandardConverter]; el servicio solo habla con esta interfaz.
 */
interface MediaConverter {
    /** ¿Sabe hacer [plan] con un archivo asi? (Si no, el archivo queda como "no se puede convertir".) */
    fun canConvert(facts: MediaFacts, plan: Plan): Boolean

    /** Escribe en [out] un archivo del perfil. Lanza [MediaException] con un motivo que se pueda ensenar. */
    fun convert(src: File, facts: MediaFacts, plan: Plan, out: File, progress: (Int) -> Unit, cancelled: () -> Boolean)

    /** Una miniatura JPEG de como mucho [MediaProfile.THUMB_MAX_BYTES], o null si no se puede (audio, formato...). */
    fun thumbnail(src: File, facts: MediaFacts): ByteArray?
}

/**
 * Las conversiones, en Kotlin puro. De la plataforma solo necesita:
 *  · [images]: decodificar/codificar imagenes (BitmapFactory en Android);
 *  · [videoSource]: fotogramas de un vídeo que NO es MJPEG (H.264/HEVC/VP9...: MediaCodec en Android);
 *  · [audioSource]: PCM de un audio que no es WAV (MP3/AAC/FLAC/OGG...: MediaCodec en Android).
 * El MJPEG en AVI/MOV, los WAV y los remuxes no necesitan nada de la plataforma.
 */
class StandardConverter(
    private val images: ImageCodec?,
    private val videoSource: ((File) -> VideoFrameSource?)? = null,
    private val audioSource: ((File) -> AudioPcmSource?)? = null,
) : MediaConverter {

    override fun canConvert(facts: MediaFacts, plan: Plan): Boolean = when (plan) {
        Plan.REMUX -> true
        Plan.TRANSCODE -> when (facts.kind) {
            MediaKind.PHOTO -> images != null
            MediaKind.AUDIO -> facts.container == "wav" || audioSource != null
            MediaKind.VIDEO -> (facts.videoCodec == "mjpeg" && images != null && facts.container in setOf("avi", "mov", "mp4")) || videoSource != null
            MediaKind.OTHER -> false
        }
        else -> false
    }

    override fun convert(src: File, facts: MediaFacts, plan: Plan, out: File, progress: (Int) -> Unit, cancelled: () -> Boolean) {
        try {
            if (plan == Plan.REMUX) {
                try {
                    FileSource(src).use { s ->
                        if (facts.kind == MediaKind.AUDIO) Remux.pcmToWav(s, out, cancelled) else Remux.toAvi(s, out, cancelled, progress)
                    }
                    return
                } catch (e: MediaException) {
                    // Un fotograma que no vale: se cae al camino de recodificar si se sabe.
                    out.delete()
                    if (cancelled() || !canConvert(facts, Plan.TRANSCODE)) throw e
                }
            }
            when (facts.kind) {
                MediaKind.PHOTO -> photo(src, facts, out, cancelled)
                MediaKind.AUDIO -> audio(src, facts, out, progress, cancelled)
                MediaKind.VIDEO -> video(src, facts, out, progress, cancelled)
                MediaKind.OTHER -> throw MediaException("Este archivo no es multimedia.")
            }
        } catch (e: MediaException) {
            out.delete(); throw e
        } catch (e: IOException) {
            out.delete(); throw MediaException("No se pudo convertir: ${e.message}", e)
        } catch (e: OutOfMemoryError) {
            out.delete(); throw MediaException("El archivo es demasiado grande para convertirlo en este teléfono.")
        }
    }

    // ------------------------------------------------------------------ fotos
    private fun photo(src: File, facts: MediaFacts, out: File, cancelled: () -> Boolean) {
        val codec = images ?: throw MediaException("Este teléfono no puede convertir imágenes.")
        if (src.length() > MAX_PHOTO_INPUT) throw MediaException("La imagen es demasiado grande para convertirla.")
        val side = MediaProfile.PHOTO_TARGET_SIDE
        val img = codec.decode(src.readBytes(), side, side, exact = false) ?: throw MediaException("Este teléfono no sabe abrir este formato de imagen.")
        if (cancelled()) throw MediaException("Cancelado.")
        // Android no gira un JPEG por su EXIF: el P4 tampoco, asi que se endereza aqui, una sola vez.
        val upright = Orient.apply(img, facts.orientation ?: 1)
        val jpeg = encodeUnder(codec, upright, MediaProfile.PHOTO_TARGET_QUALITY, MediaProfile.PHOTO_MAX_BYTES.toInt(), 10)
        out.writeBytes(jpeg)
    }

    private fun encodeUnder(codec: ImageCodec, img: RawImage, quality: Int, maxBytes: Int, step: Int): ByteArray {
        var q = quality
        while (true) {
            val b = codec.encodeJpeg(img, q)
            if (b.size <= maxBytes) return b
            if (q <= 20) throw MediaException("No se consiguió una imagen de menos de ${maxBytes / 1024} KB.")
            q -= step
        }
    }

    // ------------------------------------------------------------------ audio
    private fun audio(src: File, facts: MediaFacts, out: File, progress: (Int) -> Unit, cancelled: () -> Boolean) {
        if (facts.container == "wav") { FileSource(src).use { WavConvert.toProfile(it, out, cancelled, progress) }; return }
        val source = audioSource?.invoke(src) ?: throw MediaException("Este teléfono no sabe decodificar este audio.")
        source.use { AudioTranscoder.run(it, out, progress, cancelled) }
    }

    // ------------------------------------------------------------------ vídeo
    private fun video(src: File, facts: MediaFacts, out: File, progress: (Int) -> Unit, cancelled: () -> Boolean) {
        val codec = images
        var source: VideoFrameSource? = null
        if (facts.videoCodec == "mjpeg" && codec != null && facts.container in setOf("avi", "mov", "mp4")) source = JpegFrameSource.open(src, codec)
        if (source == null) source = videoSource?.invoke(src)
        if (source == null) throw MediaException("Este teléfono no sabe decodificar este vídeo.")
        val enc = codec ?: throw MediaException("Este teléfono no puede codificar imágenes.")
        source.use { VideoTranscoder.run(it, out, enc, progress, cancelled) }
    }

    // ------------------------------------------------------------------ miniaturas
    override fun thumbnail(src: File, facts: MediaFacts): ByteArray? {
        val codec = images ?: return null
        try {
            val img: RawImage = when (facts.kind) {
                MediaKind.PHOTO -> {
                    if (src.length() > MAX_PHOTO_INPUT) return null
                    val s = MediaProfile.THUMB_SIDE
                    Orient.apply(codec.decode(src.readBytes(), s, s, exact = false) ?: return null, facts.orientation ?: 1)
                }
                MediaKind.VIDEO -> {
                    var source: VideoFrameSource? = null
                    if (facts.videoCodec == "mjpeg" && facts.container in setOf("avi", "mov", "mp4")) source = JpegFrameSource.open(src, codec)
                    if (source == null) source = videoSource?.invoke(src)
                    source?.use { vs ->
                        val rot = vs.rotation
                        val dw = if (rot == 90 || rot == 270) vs.height else vs.width
                        val dh = if (rot == 90 || rot == 270) vs.width else vs.height
                        val (tw, th) = Geometry.fitThumb(dw, dh)
                        val (pw, ph) = if (rot == 90 || rot == 270) th to tw else tw to th
                        val f = vs.nextFrame(pw, ph) { true } ?: return null
                        Orient.apply(f.image, Geometry.exifFor(rot))
                    } ?: return null
                }
                else -> return null
            }
            var q = 80
            while (q >= 30) {
                val b = codec.encodeJpeg(img, q)
                if (b.size <= MediaProfile.THUMB_MAX_BYTES) return b
                q -= 15
            }
            return null
        } catch (e: Exception) {
            return null            // sin miniatura no pasa nada: el archivo se reproduce igual
        }
    }

    companion object { const val MAX_PHOTO_INPUT = 96L shl 20 }
}

/** Convierte vídeo (cualquier [VideoFrameSource]) a AVI MJPEG del perfil. */
object VideoTranscoder {
    fun run(source: VideoFrameSource, out: File, images: ImageCodec, progress: (Int) -> Unit, cancelled: () -> Boolean) {
        val rot = source.rotation
        val turned = rot == 90 || rot == 270
        val dispW = if (turned) source.height else source.width
        val dispH = if (turned) source.width else source.height
        if (dispW <= 0 || dispH <= 0) throw MediaException("El vídeo no declara su tamaño.")
        val (tw, th) = Geometry.fitVideo(dispW, dispH)
        val (preW, preH) = if (turned) th to tw else tw to th
        val srcFps = source.fps?.takeIf { it > 0.5 && it < 500 } ?: 30.0
        val fps = Math.round(minOf(srcFps, MediaProfile.VIDEO_TARGET_FPS.toDouble())).toInt().coerceIn(1, MediaProfile.VIDEO_TARGET_FPS)
        val period = 1_000_000.0 / fps
        val dur = source.durationMs?.takeIf { it > 0 }?.let { it * 1000.0 }
        val orient = Geometry.exifFor(rot)
        var next = 0.0
        var lastPct = -1
        AviMjpegWriter(out, tw, th, fps).use { w ->
            while (true) {
                if (cancelled()) throw MediaException("Cancelado.")
                val limit = next
                val f = source.nextFrame(preW, preH) { pts -> pts >= limit - 1000 } ?: break
                next = if (f.ptsUs > limit + period * 2) f.ptsUs + period else limit + period
                val img = Orient.apply(f.image, orient)
                var q = MediaProfile.VIDEO_TARGET_QUALITY
                var jpeg = images.encodeJpeg(img, q)
                while (jpeg.size > MediaProfile.VIDEO_FRAME_MAX_BYTES && q > 20) { q -= 10; jpeg = images.encodeJpeg(img, q) }
                if (jpeg.size > MediaProfile.VIDEO_FRAME_MAX_BYTES) throw MediaException("Un fotograma no cabe en ${MediaProfile.VIDEO_FRAME_MAX_BYTES / 1024} KB ni a la calidad mínima.")
                w.addFrame(jpeg)
                if (dur != null) { val pct = (f.ptsUs * 100 / dur).toInt().coerceIn(0, 99); if (pct != lastPct) { lastPct = pct; progress(pct) } }
            }
            if (w.frames == 0) throw MediaException("No se pudo leer ningún fotograma del vídeo.")
        }
    }
}

/** Convierte audio (cualquier [AudioPcmSource]) a WAV IMA ADPCM mono del perfil. */
object AudioTranscoder {
    fun run(source: AudioPcmSource, out: File, progress: (Int) -> Unit, cancelled: () -> Boolean) {
        val ch = source.channels
        if (ch < 1 || source.sampleRate <= 0) throw MediaException("El audio no declara canales o frecuencia.")
        val rate = source.sampleRate.coerceIn(MediaProfile.AUDIO_RATE_MIN, MediaProfile.AUDIO_TARGET_RATE)
        val totalFrames = source.durationMs?.takeIf { it > 0 }?.let { it * source.sampleRate / 1000 }
        var frames = 0L
        var lastPct = -1
        ImaWavWriter(out, rate).use { wr ->
            val rs = MonoResampler(source.sampleRate, rate) { p, n -> wr.write(p, n) }
            val buf = ShortArray(4096 * ch)
            val mono = ShortArray(4096)
            while (true) {
                if (cancelled()) throw MediaException("Cancelado.")
                val n = source.read(buf)
                if (n < 0) break
                val f = n / ch
                for (i in 0 until f) { var s = 0; for (c in 0 until ch) s += buf[i * ch + c]; mono[i] = (s / ch).toShort() }
                rs.push(mono, f)
                frames += f
                if (totalFrames != null) { val pct = (frames * 100 / totalFrames).toInt().coerceIn(0, 99); if (pct != lastPct) { lastPct = pct; progress(pct) } }
            }
            rs.finish()
        }
        if (frames == 0L) { out.delete(); throw MediaException("No se pudo leer ningún dato de audio.") }
    }
}

/** MJPEG por fotograma (AVI o MOV/MP4 con `jpeg`) como fuente de vídeo: se decodifican con el [ImageCodec], sin MediaCodec. */
class JpegFrameSource private constructor(
    private val src: FileSource,
    override val width: Int,
    override val height: Int,
    override val rotation: Int,
    override val durationMs: Long?,
    override val fps: Double?,
    private val images: ImageCodec,
    private val nextBytes: () -> ByteArray?,
    private val frameUs: Double,
) : VideoFrameSource {
    private var index = 0
    private var last: RawImage? = null
    private var lastDims = 0 to 0

    override fun nextFrame(outW: Int, outH: Int, keep: (Long) -> Boolean): VideoFrame? {
        while (true) {
            val b = nextBytes() ?: return null
            val pts = (index * frameUs).toLong()
            index++
            if (!keep(pts)) { continue }
            if (b.isEmpty()) { val l = last; if (l != null && lastDims == (outW to outH)) return VideoFrame(pts, l); continue }
            val img = images.decode(b, outW, outH, exact = true) ?: throw MediaException("No se pudo decodificar el fotograma $index.")
            last = img; lastDims = outW to outH
            return VideoFrame(pts, img)
        }
    }

    override fun close() = src.close()

    companion object {
        /** null si el archivo no es AVI/MOV con JPEG por fotograma. */
        fun open(file: File, images: ImageCodec): JpegFrameSource? {
            val fs = FileSource(file)
            try {
                val avi = Avi.parseHeader(fs)
                if (avi != null) {
                    if (avi.width <= 0 || avi.height <= 0 || avi.usPerFrame <= 0) { fs.close(); return null }
                    val cur = Avi.FrameCursor(fs, avi)
                    return JpegFrameSource(fs, avi.width, avi.height, 0, avi.durationMs, 1_000_000.0 / avi.usPerFrame, images, { cur.next() }, avi.usPerFrame.toDouble())
                }
                val info = Mp4.parse(fs, tables = true)
                val v = info?.video
                if (v == null || v.codec.lowercase() != "jpeg") { fs.close(); return null }
                val locs = Mp4.sampleLocations(v)
                val fps = (v.fpsX100 ?: 0) / 100.0
                if (locs == null || fps <= 0) { fs.close(); return null }
                var i = 0
                return JpegFrameSource(fs, v.width, v.height, v.rotation, v.durationMs, fps, images, {
                    if (i >= locs.size) null else locs[i++].let { l -> fs.bytes(l[0], l[1].toInt()) ?: throw MediaException("El vídeo está cortado.") }
                }, 1_000_000.0 / fps)
            } catch (e: Exception) {
                fs.close(); throw e
            }
        }
    }
}
