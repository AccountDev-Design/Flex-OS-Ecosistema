package com.flexos.flexphone.flexcloud

import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.ImageFormat
import android.media.Image
import android.media.MediaCodec
import android.media.MediaCodecInfo
import android.media.MediaExtractor
import android.media.MediaFormat
import android.os.BatteryManager
import android.os.PowerManager
import android.util.Log
import com.flexos.flexphone.cloud.media.AudioPcmSource
import com.flexos.flexphone.cloud.media.ImageCodec
import com.flexos.flexphone.cloud.media.MediaConverter
import com.flexos.flexphone.cloud.media.MediaException
import com.flexos.flexphone.cloud.media.RawImage
import com.flexos.flexphone.cloud.media.StandardConverter
import com.flexos.flexphone.cloud.media.VideoFrame
import com.flexos.flexphone.cloud.media.VideoFrameSource
import com.flexos.flexphone.cloud.media.Yuv
import com.flexos.flexphone.cloud.media.YuvFrame
import java.io.ByteArrayOutputStream
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * LO QUE ANDROID APORTA a la preparacion multimedia de Flex Cloud (el motor y toda su logica viven en `:storage`, probados en el PC):
 *
 *  · [AndroidImageCodec]: `BitmapFactory` decodifica JPEG/PNG/GIF/BMP/WebP (y HEIC/AVIF segun la version) y `Bitmap.compress`
 *    escribe JPEG baseline;
 *  · [MediaCodecVideoSource]: `MediaExtractor` + `MediaCodec` -> fotogramas YUV -> [Yuv] (RGB y escala en Kotlin puro);
 *  · [MediaCodecAudioSource]: `MediaExtractor` + `MediaCodec` -> PCM de 16 bits;
 *  · la bateria: solo se convierte en un buen momento y con la CPU despierta mientras dura el trabajo.
 *
 * NO se anade ninguna biblioteca: son las APIs del sistema. Los codecs que hay dependen del telefono (un H.264 lo decodifica
 * cualquiera; un AV1 o un HEVC de 10 bits, segun el modelo): si el telefono no sabe, el archivo queda "no se puede convertir" con
 * el motivo, en vez de dejar que el P4 lo intente.
 */
object AndroidMedia {
    private const val TAG = "FlexPhone/Media"

    fun converter(): MediaConverter = StandardConverter(
        AndroidImageCodec(),
        videoSource = { f -> MediaCodecVideoSource.open(f) },
        audioSource = { f -> MediaCodecAudioSource.open(f) },
    )

    /**
     * ¿Es buen momento para convertir? Con el cargador siempre; sin el, no con el ahorro de energia ni con menos de un 15 % de bateria.
     * (Si la espera se alarga una hora, la cola convierte igualmente: nadie se queda sin poder ver un video para siempre.)
     */
    fun goodMoment(ctx: Context): Boolean {
        val app = ctx.applicationContext
        val bi = app.registerReceiver(null, IntentFilter(Intent.ACTION_BATTERY_CHANGED)) ?: return true
        if (bi.getIntExtra(BatteryManager.EXTRA_PLUGGED, 0) != 0) return true
        val level = bi.getIntExtra(BatteryManager.EXTRA_LEVEL, -1)
        val scale = bi.getIntExtra(BatteryManager.EXTRA_SCALE, 100)
        val pct = if (level >= 0 && scale > 0) level * 100 / scale else 100
        val pm = app.getSystemService(PowerManager::class.java)
        if (pm?.isPowerSaveMode == true) return false
        return pct >= 15
    }

    private var wake: PowerManager.WakeLock? = null

    /** La CPU despierta SOLO mientras se convierte un archivo, con tope (aunque algo falle, caduca). */
    @Synchronized fun busy(ctx: Context, on: Boolean) {
        if (on) {
            if (wake == null) {
                val pm = ctx.applicationContext.getSystemService(PowerManager::class.java)
                wake = pm?.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "FlexPhone::media")?.apply { setReferenceCounted(false) }
            }
            wake?.let { if (!it.isHeld) it.acquire(30 * 60 * 1000L) }
        } else {
            wake?.let { if (it.isHeld) it.release() }
        }
    }
}

// ============================================================================================ imagenes
class AndroidImageCodec : ImageCodec {
    override fun decode(bytes: ByteArray, maxW: Int, maxH: Int, exact: Boolean): RawImage? {
        val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
        BitmapFactory.decodeByteArray(bytes, 0, bytes.size, bounds)
        val sw = bounds.outWidth
        val sh = bounds.outHeight
        if (sw <= 0 || sh <= 0) return null
        val (tw, th) = if (exact) maxW to maxH else {
            val s = minOf(1.0, minOf(maxW.toDouble() / sw, maxH.toDouble() / sh))
            maxOf(1, Math.round(sw * s).toInt()) to maxOf(1, Math.round(sh * s).toInt())
        }
        // Se decodifica ya reducido (potencias de 2): una foto de 12 MP no ocupa 48 MB de memoria para acabar en 1600 px.
        var sample = 1
        while (sw / (sample * 2) >= tw && sh / (sample * 2) >= th) sample *= 2
        val opts = BitmapFactory.Options().apply { inSampleSize = sample; inPreferredConfig = Bitmap.Config.ARGB_8888 }
        val bmp = try { BitmapFactory.decodeByteArray(bytes, 0, bytes.size, opts) } catch (e: OutOfMemoryError) { null } ?: return null
        val alpha = bmp.hasAlpha()
        try {
            val scaled = if (bmp.width != tw || bmp.height != th) Bitmap.createScaledBitmap(bmp, tw, th, true) else bmp
            val px = IntArray(tw * th)
            scaled.getPixels(px, 0, tw, 0, 0, tw, th)
            if (scaled !== bmp) scaled.recycle()
            for (i in px.indices) {
                val c = px[i]
                if (!alpha) { px[i] = c or (0xFF shl 24); continue }
                // el alfa se aplana sobre BLANCO (un PNG con fondo transparente no sale negro)
                val a = c ushr 24
                val r = (((c shr 16) and 255) * a + 255 * (255 - a)) / 255
                val g = (((c shr 8) and 255) * a + 255 * (255 - a)) / 255
                val b = ((c and 255) * a + 255 * (255 - a)) / 255
                px[i] = (0xFF shl 24) or (r shl 16) or (g shl 8) or b
            }
            return RawImage(tw, th, px)
        } finally {
            bmp.recycle()
        }
    }

    override fun encodeJpeg(img: RawImage, quality: Int): ByteArray {
        val bmp = Bitmap.createBitmap(img.argb, img.width, img.height, Bitmap.Config.ARGB_8888)
        try {
            val out = ByteArrayOutputStream(64 * 1024)
            if (!bmp.compress(Bitmap.CompressFormat.JPEG, quality.coerceIn(1, 100), out)) throw MediaException("No se pudo codificar el JPEG.")
            return out.toByteArray()
        } finally {
            bmp.recycle()
        }
    }
}

// ============================================================================================ vídeo
class MediaCodecVideoSource private constructor(
    private val extractor: MediaExtractor,
    private val decoder: MediaCodec,
    override val width: Int,
    override val height: Int,
    override val rotation: Int,
    override val durationMs: Long?,
    override val fps: Double?,
) : VideoFrameSource {
    private val info = MediaCodec.BufferInfo()
    private var inputDone = false
    private var outputDone = false
    private var bt709 = maxOf(width, height) >= 1280
    private var fullRange = false
    private var stride = 0
    private var sliceHeight = 0
    private var colorFormat = 0
    private var cropL = 0
    private var cropT = 0
    private var cropW = width
    private var cropH = height

    override fun nextFrame(outW: Int, outH: Int, keep: (Long) -> Boolean): VideoFrame? {
        var idle = 0
        val t0 = System.currentTimeMillis()
        while (!outputDone) {
            if (!inputDone) feed()
            val idx = decoder.dequeueOutputBuffer(info, 10_000)
            when {
                idx == MediaCodec.INFO_TRY_AGAIN_LATER -> {
                    // Un decodificador que no contesta en 15 s no va a contestar: se dice, no se cuelga el telefono.
                    if (++idle > 20 && System.currentTimeMillis() - t0 > 15_000) throw MediaException("El decodificador de vídeo de este teléfono no respondió.")
                }
                idx == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED -> readFormat(decoder.outputFormat)
                idx >= 0 -> {
                    idle = 0
                    val eos = info.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM != 0
                    var frame: VideoFrame? = null
                    if (info.size > 0 && keep(info.presentationTimeUs)) frame = VideoFrame(info.presentationTimeUs, toRgb(idx, outW, outH))
                    decoder.releaseOutputBuffer(idx, false)
                    if (eos) outputDone = true
                    if (frame != null) return frame
                }
            }
        }
        return null
    }

    private fun feed() {
        val i = decoder.dequeueInputBuffer(0)
        if (i < 0) return
        val buf = decoder.getInputBuffer(i) ?: return
        val n = extractor.readSampleData(buf, 0)
        if (n < 0) {
            decoder.queueInputBuffer(i, 0, 0, 0, MediaCodec.BUFFER_FLAG_END_OF_STREAM)
            inputDone = true
        } else {
            decoder.queueInputBuffer(i, 0, n, extractor.sampleTime, 0)
            extractor.advance()
        }
    }

    private fun readFormat(f: MediaFormat) {
        if (f.containsKey("crop-left") && f.containsKey("crop-right") && f.containsKey("crop-top") && f.containsKey("crop-bottom")) {
            cropL = f.getInteger("crop-left"); cropT = f.getInteger("crop-top")
            cropW = f.getInteger("crop-right") - cropL + 1; cropH = f.getInteger("crop-bottom") - cropT + 1
        }
        if (f.containsKey(MediaFormat.KEY_STRIDE)) stride = f.getInteger(MediaFormat.KEY_STRIDE)
        if (f.containsKey(MediaFormat.KEY_SLICE_HEIGHT)) sliceHeight = f.getInteger(MediaFormat.KEY_SLICE_HEIGHT)
        if (f.containsKey(MediaFormat.KEY_COLOR_FORMAT)) colorFormat = f.getInteger(MediaFormat.KEY_COLOR_FORMAT)
        if (f.containsKey(MediaFormat.KEY_COLOR_STANDARD)) bt709 = f.getInteger(MediaFormat.KEY_COLOR_STANDARD) == MediaFormat.COLOR_STANDARD_BT709
        if (f.containsKey(MediaFormat.KEY_COLOR_RANGE)) fullRange = f.getInteger(MediaFormat.KEY_COLOR_RANGE) == MediaFormat.COLOR_RANGE_FULL
    }

    private fun toRgb(idx: Int, outW: Int, outH: Int): RawImage {
        val img: Image? = decoder.getOutputImage(idx)
        if (img != null) {
            try {
                if (img.format != ImageFormat.YUV_420_888) throw MediaException("Este vídeo (10 bits o HDR) no se puede convertir en este teléfono.")
                val c = img.cropRect
                val p = img.planes
                val w = if (c.width() > 0) c.width() else cropW
                val h = if (c.height() > 0) c.height() else cropH
                return Yuv.toArgbScaled(
                    YuvFrame(w, h, c.left, c.top, p[0].buffer, p[0].rowStride, p[0].pixelStride, p[1].buffer, p[2].buffer, p[1].rowStride, p[1].pixelStride, bt709, fullRange),
                    outW, outH,
                )
            } finally {
                img.close()
            }
        }
        // Decodificadores que solo dan buffers (planar o semi-planar): el diseno lo dice el formato de salida.
        val buf = decoder.getOutputBuffer(idx) ?: throw MediaException("El decodificador no entrego imagen.")
        val st = if (stride > 0) stride else width
        val sh = if (sliceHeight > 0) sliceHeight else height
        val bb = buf.duplicate().also { it.position(info.offset); it.limit(info.offset + info.size) }.slice()
        return when (colorFormat) {
            MediaCodecInfo.CodecCapabilities.COLOR_FormatYUV420SemiPlanar -> {
                val u = bb.duplicate().also { it.position(st * sh) }.slice()
                val v = bb.duplicate().also { it.position(st * sh + 1) }.slice()
                Yuv.toArgbScaled(YuvFrame(cropW, cropH, cropL, cropT, bb, st, 1, u, v, st, 2, bt709, fullRange), outW, outH)
            }
            MediaCodecInfo.CodecCapabilities.COLOR_FormatYUV420Planar -> {
                val u = bb.duplicate().also { it.position(st * sh) }.slice()
                val v = bb.duplicate().also { it.position(st * sh + (st / 2) * (sh / 2)) }.slice()
                Yuv.toArgbScaled(YuvFrame(cropW, cropH, cropL, cropT, bb, st, 1, u, v, st / 2, 1, bt709, fullRange), outW, outH)
            }
            else -> throw MediaException("Formato de color del decodificador no soportado ($colorFormat).")
        }
    }

    override fun close() {
        try { decoder.stop() } catch (e: Exception) { /* ya parado */ }
        try { decoder.release() } catch (e: Exception) { /* ya liberado */ }
        try { extractor.release() } catch (e: Exception) { /* ya liberado */ }
    }

    companion object {
        /** null = este telefono no sabe abrir este vídeo (sin decodificador para su codec, o el contenedor no se lee). */
        fun open(file: File): VideoFrameSource? {
            val ex = MediaExtractor()
            try { ex.setDataSource(file.path) } catch (e: Exception) { ex.release(); return null }
            var track = -1
            var fmt: MediaFormat? = null
            for (i in 0 until ex.trackCount) {
                val f = ex.getTrackFormat(i)
                if (f.getString(MediaFormat.KEY_MIME)?.startsWith("video/") == true) { track = i; fmt = f; break }
            }
            val f = fmt
            if (track < 0 || f == null) { ex.release(); return null }
            ex.selectTrack(track)
            val mime = f.getString(MediaFormat.KEY_MIME) ?: run { ex.release(); return null }
            val dec = try { MediaCodec.createDecoderByType(mime) } catch (e: Exception) { Log.i("FlexPhone/Media", "sin decodificador para $mime"); ex.release(); return null }
            f.setInteger(MediaFormat.KEY_COLOR_FORMAT, MediaCodecInfo.CodecCapabilities.COLOR_FormatYUV420Flexible)
            try { dec.configure(f, null, null, 0); dec.start() } catch (e: Exception) { dec.release(); ex.release(); return null }
            val w = f.getInteger(MediaFormat.KEY_WIDTH)
            val h = f.getInteger(MediaFormat.KEY_HEIGHT)
            val rot = if (f.containsKey(MediaFormat.KEY_ROTATION)) f.getInteger(MediaFormat.KEY_ROTATION) else 0
            val dur = if (f.containsKey(MediaFormat.KEY_DURATION)) f.getLong(MediaFormat.KEY_DURATION) / 1000 else null
            val fps = if (f.containsKey(MediaFormat.KEY_FRAME_RATE)) try { f.getInteger(MediaFormat.KEY_FRAME_RATE).toDouble() } catch (e: Exception) { try { f.getFloat(MediaFormat.KEY_FRAME_RATE).toDouble() } catch (e2: Exception) { null } } else null
            return MediaCodecVideoSource(ex, dec, w, h, ((rot % 360) + 360) % 360, dur, fps)
        }
    }
}

// ============================================================================================ audio
class MediaCodecAudioSource private constructor(
    private val extractor: MediaExtractor,
    private val decoder: MediaCodec,
    private var rate: Int,
    private var ch: Int,
    override val durationMs: Long?,
) : AudioPcmSource {
    private val info = MediaCodec.BufferInfo()
    private var inputDone = false
    private var outputDone = false
    private var float32 = false
    private var cur: ShortArray? = null
    private var curPos = 0
    override val sampleRate: Int get() = rate
    override val channels: Int get() = ch

    init { prime() }

    /** La frecuencia y los canales REALES salen del decodificador (un AAC con SBR sale al doble de lo que dice el archivo). */
    private fun prime() {
        val t0 = System.currentTimeMillis()
        while (cur == null && !outputDone) {
            if (System.currentTimeMillis() - t0 > 15_000) throw MediaException("El decodificador de audio de este teléfono no respondió.")
            step()
        }
    }

    private fun feed() {
        val i = decoder.dequeueInputBuffer(0)
        if (i < 0) return
        val buf = decoder.getInputBuffer(i) ?: return
        val n = extractor.readSampleData(buf, 0)
        if (n < 0) {
            decoder.queueInputBuffer(i, 0, 0, 0, MediaCodec.BUFFER_FLAG_END_OF_STREAM)
            inputDone = true
        } else {
            decoder.queueInputBuffer(i, 0, n, extractor.sampleTime, 0)
            extractor.advance()
        }
    }

    private fun step() {
        if (!inputDone) feed()
        val idx = decoder.dequeueOutputBuffer(info, 10_000)
        when {
            idx == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED -> {
                val f = decoder.outputFormat
                if (f.containsKey(MediaFormat.KEY_SAMPLE_RATE)) rate = f.getInteger(MediaFormat.KEY_SAMPLE_RATE)
                if (f.containsKey(MediaFormat.KEY_CHANNEL_COUNT)) ch = f.getInteger(MediaFormat.KEY_CHANNEL_COUNT)
                float32 = f.containsKey(MediaFormat.KEY_PCM_ENCODING) && f.getInteger(MediaFormat.KEY_PCM_ENCODING) == android.media.AudioFormat.ENCODING_PCM_FLOAT
            }
            idx >= 0 -> {
                if (info.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM != 0) outputDone = true
                if (info.size > 0) {
                    val b: ByteBuffer = (decoder.getOutputBuffer(idx) ?: ByteBuffer.allocate(0)).duplicate().also {
                        it.position(info.offset); it.limit(info.offset + info.size); it.order(ByteOrder.nativeOrder())
                    }
                    cur = if (float32) {
                        val fb = b.asFloatBuffer(); ShortArray(fb.remaining()) { (fb.get().coerceIn(-1f, 1f) * 32767f).toInt().toShort() }
                    } else {
                        val sb = b.asShortBuffer(); ShortArray(sb.remaining()).also { sb.get(it) }
                    }
                    curPos = 0
                }
                decoder.releaseOutputBuffer(idx, false)
            }
        }
    }

    override fun read(buf: ShortArray): Int {
        var n = 0
        val t0 = System.currentTimeMillis()
        while (n < buf.size) {
            val c = cur
            if (c != null && curPos < c.size) {
                val take = minOf(buf.size - n, c.size - curPos)
                System.arraycopy(c, curPos, buf, n, take)
                curPos += take; n += take
                continue
            }
            cur = null
            if (outputDone) break
            step()
            if (cur == null && System.currentTimeMillis() - t0 > 15_000) throw MediaException("El decodificador de audio de este teléfono no respondió.")
        }
        return if (n == 0 && outputDone) -1 else n
    }

    override fun close() {
        try { decoder.stop() } catch (e: Exception) { /* ya parado */ }
        try { decoder.release() } catch (e: Exception) { /* ya liberado */ }
        try { extractor.release() } catch (e: Exception) { /* ya liberado */ }
    }

    companion object {
        /** null = este telefono no sabe decodificar este audio. */
        fun open(file: File): AudioPcmSource? {
            val ex = MediaExtractor()
            try { ex.setDataSource(file.path) } catch (e: Exception) { ex.release(); return null }
            var track = -1
            var fmt: MediaFormat? = null
            for (i in 0 until ex.trackCount) {
                val f = ex.getTrackFormat(i)
                if (f.getString(MediaFormat.KEY_MIME)?.startsWith("audio/") == true) { track = i; fmt = f; break }
            }
            val f = fmt
            if (track < 0 || f == null) { ex.release(); return null }
            ex.selectTrack(track)
            val mime = f.getString(MediaFormat.KEY_MIME) ?: run { ex.release(); return null }
            val dec = try { MediaCodec.createDecoderByType(mime) } catch (e: Exception) { ex.release(); return null }
            try { dec.configure(f, null, null, 0); dec.start() } catch (e: Exception) { dec.release(); ex.release(); return null }
            val sr = if (f.containsKey(MediaFormat.KEY_SAMPLE_RATE)) f.getInteger(MediaFormat.KEY_SAMPLE_RATE) else 44100
            val c = if (f.containsKey(MediaFormat.KEY_CHANNEL_COUNT)) f.getInteger(MediaFormat.KEY_CHANNEL_COUNT) else 2
            val dur = if (f.containsKey(MediaFormat.KEY_DURATION)) f.getLong(MediaFormat.KEY_DURATION) / 1000 else null
            return try { MediaCodecAudioSource(ex, dec, sr, c, dur) } catch (e: MediaException) {
                try { dec.stop() } catch (x: Exception) { /* nada */ }
                dec.release(); ex.release(); throw e
            }
        }
    }
}
