package com.flexos.flexphone.cloud.media

import java.nio.ByteBuffer
import kotlin.test.Test
import kotlin.test.assertTrue

/** La conversion YUV -> RGB y el escalado de cada fotograma que decodifica el telefono. */
class YuvTest {
    private fun rgb(x: Int, y: Int, w: Int, h: Int): Triple<Int, Int, Int> = Triple(x * 255 / maxOf(1, w - 1), y * 255 / maxOf(1, h - 1), 128 + (x - y) * 100 / maxOf(w, h))

    /** RGB -> YUV (BT.601 o 709, limitado o completo) para fabricar el origen conocido. */
    private fun toYuv(r: Int, g: Int, b: Int, bt709: Boolean, full: Boolean): IntArray {
        val kr = if (bt709) 0.2126 else 0.299
        val kb = if (bt709) 0.0722 else 0.114
        val y = kr * r + (1 - kr - kb) * g + kb * b
        val u = (b - y) / (2 * (1 - kb))
        val v = (r - y) / (2 * (1 - kr))
        return if (full) intArrayOf(Math.round(y).toInt().coerceIn(0, 255), Math.round(u + 128).toInt().coerceIn(0, 255), Math.round(v + 128).toInt().coerceIn(0, 255))
        else intArrayOf(Math.round(16 + y * 219 / 255).toInt().coerceIn(16, 235), Math.round(128 + u * 224 / 255).toInt().coerceIn(16, 240), Math.round(128 + v * 224 / 255).toInt().coerceIn(16, 240))
    }

    private enum class Layout { I420, I420_PADDED, NV12 }

    private fun frame(w: Int, h: Int, layout: Layout, bt709: Boolean, full: Boolean, cropLeft: Int = 0, cropTop: Int = 0, padW: Int = 0, padH: Int = 0): YuvFrame {
        val sw = w + padW + cropLeft; val sh = h + padH + cropTop              // el area decodificada es mayor que la visible
        val yStride = if (layout == Layout.I420) sw else sw + 16
        val yArr = ByteArray(yStride * sh)
        val cw = (sw + 1) / 2; val chh = (sh + 1) / 2
        val uvStride = when (layout) { Layout.I420 -> cw; Layout.I420_PADDED -> cw + 8; Layout.NV12 -> yStride }
        val uvPix = if (layout == Layout.NV12) 2 else 1
        val uArr = ByteArray(uvStride * chh + 2); val vArr = if (layout == Layout.NV12) uArr else ByteArray(uvStride * chh)
        val uOff = 0; val vOff = if (layout == Layout.NV12) 1 else 0
        for (yy in 0 until sh) for (xx in 0 until sw) {
            val (r, g, b) = rgb((xx - cropLeft).coerceIn(0, w - 1), (yy - cropTop).coerceIn(0, h - 1), w, h)
            val p = toYuv(r, g, b, bt709, full)
            yArr[yy * yStride + xx] = p[0].toByte()
            if (xx % 2 == 0 && yy % 2 == 0) {
                uArr[uOff + (yy / 2) * uvStride + (xx / 2) * uvPix] = p[1].toByte()
                vArr[vOff + (yy / 2) * uvStride + (xx / 2) * uvPix] = p[2].toByte()
            }
        }
        val ub = ByteBuffer.wrap(uArr)
        val vb = if (layout == Layout.NV12) ByteBuffer.wrap(uArr).also { it.position(1) }.slice() else ByteBuffer.wrap(vArr)
        return YuvFrame(w, h, cropLeft, cropTop, ByteBuffer.wrap(yArr), yStride, 1, ub, vb, uvStride, uvPix, bt709, full)
    }

    private fun maxErr(img: RawImage, w: Int, h: Int, scaleBack: Boolean = false): Int {
        var m = 0
        for (y in 0 until img.height) for (x in 0 until img.width) {
            val sx = if (scaleBack) (x * w + w / 2) / img.width else x
            val sy = if (scaleBack) (y * h + h / 2) / img.height else y
            val (r, g, b) = rgb(sx.coerceIn(0, w - 1), sy.coerceIn(0, h - 1), w, h)
            val p = img.argb[y * img.width + x]
            m = maxOf(m, Math.abs(((p shr 16) and 255) - r), Math.abs(((p shr 8) and 255) - g), Math.abs((p and 255) - b))
        }
        return m
    }

    @Test fun `sin escalar, el RGB sale como el de origen en los tres diseños de planos y los dos rangos`() {
        for (layout in Layout.values()) for (bt709 in listOf(false, true)) for (full in listOf(false, true)) {
            val img = Yuv.toArgbScaled(frame(64, 48, layout, bt709, full), 64, 48)
            val e = maxErr(img, 64, 48)
            assertTrue(e <= 14, "$layout 709=$bt709 completo=$full: error maximo $e")      // el submuestreo del croma explica unos niveles
        }
    }

    @Test fun `con la estandar equivocada el color SE NOTA (por eso se lee de MediaFormat)`() {
        val f709 = frame(64, 48, Layout.I420, true, false)
        val wrong = Yuv.toArgbScaled(YuvFrame(f709.width, f709.height, 0, 0, f709.y, f709.yRowStride, 1, f709.u, f709.v, f709.uvRowStride, f709.uvPixelStride, false, false), 64, 48)
        assertTrue(maxErr(wrong, 64, 48) > 14, "BT.601 aplicado a un 709 deberia dar otro color")
    }

    @Test fun `la zona visible (crop) y el relleno del decodificador no se cuelan en la imagen`() {
        // decodificadores que dan 1088 filas para un 1080p: lo visible empieza en (cropLeft, cropTop)
        val img = Yuv.toArgbScaled(frame(64, 48, Layout.I420_PADDED, false, false, cropLeft = 2, cropTop = 2, padW = 6, padH = 8), 64, 48)
        assertTrue(maxErr(img, 64, 48) <= 22, "error ${maxErr(img, 64, 48)}")
    }

    @Test fun `al bajar de tamano promedia (filtro de caja) y el resultado sigue siendo la misma imagen`() {
        val big = frame(320, 240, Layout.NV12, false, false)
        val img = Yuv.toArgbScaled(big, 80, 60)
        assertTrue(img.width == 80 && img.height == 60)
        val e = maxErr(img, 320, 240, scaleBack = true)
        assertTrue(e <= 20, "error maximo $e")
        val imgW = Yuv.toArgbScaled(big, 100, 30)                              // otra proporcion tampoco rompe nada
        assertTrue(imgW.argb.all { (it ushr 24) == 0xFF })
    }

    @Test fun `anchos y altos impares no se salen de los planos`() {
        for ((w, h) in listOf(33 to 25, 1 to 1, 2 to 1, 7 to 9, 65 to 3)) {
            val img = Yuv.toArgbScaled(frame(w, h, Layout.NV12, false, false), maxOf(1, w / 2), maxOf(1, h / 2))
            assertTrue(img.argb.size == maxOf(1, w / 2) * maxOf(1, h / 2))
        }
    }
}
