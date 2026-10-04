package com.flexos.flexphone.cloud.media

import java.nio.ByteBuffer

/**
 * Un fotograma YUV 4:2:0 de tres planos, tal como lo da `MediaCodec.getOutputImage` (`Image.getPlanes()`), pero sin nada de
 * Android: asi la conversion a RGB y el escalado -- lo que hace el telefono por cada fotograma que se queda -- se prueban en el PC.
 *
 * [width] x [height] es la zona VISIBLE (la `cropRect` del decodificador; muchos decodificadores entregan 1088 filas para un
 * 1080p). Los planos se indexan con sus pasos reales ([yRowStride], [uvRowStride], [uvPixelStride]): un NV12 tiene
 * `uvPixelStride = 2` y los planos U y V apuntan al mismo array desplazados un byte.
 */
class YuvFrame(
    val width: Int,
    val height: Int,
    val cropLeft: Int,
    val cropTop: Int,
    val y: ByteBuffer,
    val yRowStride: Int,
    val yPixelStride: Int,
    val u: ByteBuffer,
    val v: ByteBuffer,
    val uvRowStride: Int,
    val uvPixelStride: Int,
    /** BT.709 (HD) o BT.601 (SD): lo que dice `KEY_COLOR_STANDARD`, o por tamano si no dice nada. */
    val bt709: Boolean,
    /** Rango completo (JPEG) o limitado 16-235 (el de casi todo el vídeo). */
    val fullRange: Boolean,
)

object Yuv {
    /**
     * Convierte [f] a RGB y lo escala a [outW] x [outH] PROMEDIANDO los pixeles de origen que caen en cada pixel de destino (un
     * filtro de caja: al bajar de 1080p a 360p no aparecen dientes de sierra). Si el destino es mayor que el origen, repite.
     */
    fun toArgbScaled(f: YuvFrame, outW: Int, outH: Int): RawImage {
        require(outW > 0 && outH > 0 && f.width > 0 && f.height > 0) { "tamano no valido" }
        val out = IntArray(outW * outH)
        val cw = (f.width + 1) / 2
        val ch = (f.height + 1) / 2
        // coeficientes en punto fijo (x1024)
        val yScale: Int; val yOff: Int
        if (f.fullRange) { yScale = 1024; yOff = 0 } else { yScale = 1192; yOff = 16 }      // 1,164 * 1024
        val rv: Int; val gu: Int; val gv: Int; val bu: Int
        if (f.bt709) { rv = if (f.fullRange) 1612 else 1836; gu = if (f.fullRange) 192 else 218; gv = if (f.fullRange) 479 else 546; bu = if (f.fullRange) 1900 else 2163 }
        else { rv = if (f.fullRange) 1436 else 1634; gu = if (f.fullRange) 352 else 401; gv = if (f.fullRange) 731 else 833; bu = if (f.fullRange) 1815 else 2066 }
        for (dy in 0 until outH) {
            val sy0 = (dy.toLong() * f.height / outH).toInt()
            val sy1 = maxOf(sy0 + 1, ((dy + 1).toLong() * f.height / outH).toInt()).coerceAtMost(f.height)
            val cy0 = sy0 / 2
            val cy1 = maxOf(cy0 + 1, (sy1 + 1) / 2).coerceAtMost(ch)
            for (dx in 0 until outW) {
                val sx0 = (dx.toLong() * f.width / outW).toInt()
                val sx1 = maxOf(sx0 + 1, ((dx + 1).toLong() * f.width / outW).toInt()).coerceAtMost(f.width)
                val cx0 = sx0 / 2
                val cx1 = maxOf(cx0 + 1, (sx1 + 1) / 2).coerceAtMost(cw)
                var ys = 0; var yn = 0
                for (yy in sy0 until sy1) {
                    val row = (f.cropTop + yy) * f.yRowStride
                    for (xx in sx0 until sx1) { ys += f.y.get(row + (f.cropLeft + xx) * f.yPixelStride).toInt() and 0xFF; yn++ }
                }
                var us = 0; var vs = 0; var cn = 0
                for (yy in cy0 until cy1) {
                    val row = (f.cropTop / 2 + yy) * f.uvRowStride
                    for (xx in cx0 until cx1) {
                        val at = row + (f.cropLeft / 2 + xx) * f.uvPixelStride
                        us += f.u.get(at).toInt() and 0xFF; vs += f.v.get(at).toInt() and 0xFF; cn++
                    }
                }
                val yy = ys / yn - yOff
                val uu = us / cn - 128
                val vv = vs / cn - 128
                val base = yy * yScale
                val r = (base + rv * vv + 512) shr 10
                val g = (base - gu * uu - gv * vv + 512) shr 10
                val b = (base + bu * uu + 512) shr 10
                out[dy * outW + dx] = (0xFF shl 24) or (clamp(r) shl 16) or (clamp(g) shl 8) or clamp(b)
            }
        }
        return RawImage(outW, outH, out)
    }

    private fun clamp(v: Int) = if (v < 0) 0 else if (v > 255) 255 else v
}
