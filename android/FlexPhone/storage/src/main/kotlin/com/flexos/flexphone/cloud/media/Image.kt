package com.flexos.flexphone.cloud.media

import java.io.Closeable

/** Pixeles opacos ARGB (el alfa ya se aplano sobre blanco al decodificar). */
class RawImage(val width: Int, val height: Int, val argb: IntArray) {
    init { require(width > 0 && height > 0 && argb.size == width * height) { "imagen mal formada" } }
}

/**
 * Lo unico que hace falta de la PLATAFORMA para tratar imagenes. En Android: `BitmapFactory` (decodifica
 * JPEG/PNG/GIF/BMP/WebP/HEIC segun la version) y `Bitmap.compress(JPEG)`, que escribe JPEG baseline
 * secuencial. En las pruebas del PC: `javax.imageio`.
 */
interface ImageCodec {
    /**
     * Decodifica [bytes]. Con [exact] lo escala a EXACTAMENTE [maxW] x [maxH]; sin [exact] lo escala para que quepa
     * en esa caja conservando la proporcion y SIN ampliar nunca. null = no sabe este formato.
     */
    fun decode(bytes: ByteArray, maxW: Int, maxH: Int, exact: Boolean): RawImage?
    /** JPEG baseline 4:2:0 con [quality] 1..100. */
    fun encodeJpeg(img: RawImage, quality: Int): ByteArray
}

/** Un fotograma de vídeo ya en RGB (y escalado al tamano que se pidio). */
class VideoFrame(val ptsUs: Long, val image: RawImage)

/**
 * Una fuente de fotogramas de vídeo. En Android: `MediaExtractor` + `MediaCodec`. Para MJPEG en AVI/MOV:
 * [JpegFrameSource], en Kotlin puro.
 */
interface VideoFrameSource : Closeable {
    /** Tamano en el archivo, ANTES de aplicar el giro. */
    val width: Int
    val height: Int
    /** Giro (grados, sentido horario) que hay que aplicar para verlo derecho: 0, 90, 180, 270. */
    val rotation: Int
    val durationMs: Long?
    /** Fotogramas por segundo del original, si se sabe. */
    val fps: Double?
    /**
     * El siguiente fotograma escalado a [outW] x [outH] (tamano ANTES del giro). Para los que [keep] rechaza
     * (por su marca de tiempo en microsegundos) se salta la conversion, que es lo caro, y se pasa al siguiente.
     * null = fin del vídeo.
     */
    fun nextFrame(outW: Int, outH: Int, keep: (ptsUs: Long) -> Boolean): VideoFrame?
}

/** PCM de 16 bits intercalado decodificado de cualquier formato de audio (en Android: `MediaExtractor` + `MediaCodec`). */
interface AudioPcmSource : Closeable {
    val sampleRate: Int
    val channels: Int
    val durationMs: Long?
    /** Rellena [buf] (muestras intercaladas); devuelve cuantas, o -1 al terminar. */
    fun read(buf: ShortArray): Int
}

object Geometry {
    /** Tamano de una foto del perfil: lado largo <= objetivo, sin ampliar nunca. */
    fun fitPhoto(w: Int, h: Int, maxSide: Int = MediaProfile.PHOTO_TARGET_SIDE): Pair<Int, Int> = fit(w, h, maxSide, 1)

    /** Tamano de un vídeo del perfil: lado largo <= objetivo, multiplo de 8 y minimo 16 (como `fitVideo` de la web). */
    fun fitVideo(w: Int, h: Int, maxSide: Int = MediaProfile.VIDEO_TARGET_SIDE): Pair<Int, Int> {
        val (a, b) = fit(w, h, maxSide, 1)
        fun r(v: Int) = maxOf(MediaProfile.VIDEO_MIN_SIDE, ((v + 4) / MediaProfile.VIDEO_DIM_MULTIPLE) * MediaProfile.VIDEO_DIM_MULTIPLE)
        return r(a) to r(b)
    }

    fun fitThumb(w: Int, h: Int): Pair<Int, Int> = fit(w, h, MediaProfile.THUMB_SIDE, 1)

    private fun fit(w: Int, h: Int, maxSide: Int, mult: Int): Pair<Int, Int> {
        val long = maxOf(w, h)
        if (long <= maxSide) return maxOf(mult, w) to maxOf(mult, h)
        val s = maxSide.toDouble() / long
        return maxOf(mult, Math.round(w * s).toInt()) to maxOf(mult, Math.round(h * s).toInt())
    }

    /** Giro de MP4/MOV (grados horarios) -> orientacion EXIF equivalente. */
    fun exifFor(rotation: Int): Int = when (rotation) { 90 -> 6; 180 -> 3; 270 -> 8; else -> 1 }

    /** ¿Intercambia ancho y alto esta orientacion EXIF? (5, 6, 7 y 8) */
    fun swaps(orientation: Int) = orientation in 5..8
}

object Orient {
    /** Aplica una orientacion EXIF 1..8: devuelve la imagen tal como debe verse. */
    fun apply(img: RawImage, orientation: Int): RawImage {
        if (orientation !in 2..8) return img
        val w = img.width; val h = img.height
        val sw = Geometry.swaps(orientation)
        val dw = if (sw) h else w; val dh = if (sw) w else h
        val out = IntArray(dw * dh)
        for (y in 0 until h) for (x in 0 until w) {
            val (dx, dy) = when (orientation) {
                2 -> (w - 1 - x) to y
                3 -> (w - 1 - x) to (h - 1 - y)
                4 -> x to (h - 1 - y)
                5 -> y to x
                6 -> (h - 1 - y) to x
                7 -> (h - 1 - y) to (w - 1 - x)
                else -> y to (w - 1 - x)           // 8
            }
            out[dy * dw + dx] = img.argb[y * w + x]
        }
        return RawImage(dw, dh, out)
    }
}
