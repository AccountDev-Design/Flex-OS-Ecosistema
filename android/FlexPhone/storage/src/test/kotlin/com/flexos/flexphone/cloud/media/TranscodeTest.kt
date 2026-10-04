package com.flexos.flexphone.cloud.media

import java.awt.RenderingHints
import java.awt.image.BufferedImage
import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.io.File
import javax.imageio.IIOImage
import javax.imageio.ImageIO
import javax.imageio.ImageWriteParam
import kotlin.test.AfterTest
import kotlin.test.BeforeTest
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import kotlin.test.assertTrue

/** [ImageCodec] de las pruebas (javax.imageio): en Android lo hace BitmapFactory. */
class JvmImageCodec : ImageCodec {
    var decodes = 0
    override fun decode(bytes: ByteArray, maxW: Int, maxH: Int, exact: Boolean): RawImage? {
        decodes++
        val src = ImageIO.read(ByteArrayInputStream(bytes)) ?: return null
        val (w, h) = if (exact) maxW to maxH else {
            val s = minOf(1.0, minOf(maxW.toDouble() / src.width, maxH.toDouble() / src.height))
            maxOf(1, Math.round(src.width * s).toInt()) to maxOf(1, Math.round(src.height * s).toInt())
        }
        val dst = BufferedImage(w, h, BufferedImage.TYPE_INT_RGB)
        val g = dst.createGraphics()
        g.color = java.awt.Color.WHITE; g.fillRect(0, 0, w, h)                       // el alfa se aplana sobre blanco
        g.setRenderingHint(RenderingHints.KEY_INTERPOLATION, RenderingHints.VALUE_INTERPOLATION_BILINEAR)
        g.drawImage(src, 0, 0, w, h, null); g.dispose()
        return RawImage(w, h, IntArray(w * h) { dst.getRGB(it % w, it / w) or (0xFF shl 24) })
    }

    override fun encodeJpeg(img: RawImage, quality: Int): ByteArray {
        val bi = BufferedImage(img.width, img.height, BufferedImage.TYPE_INT_RGB)
        for (y in 0 until img.height) for (x in 0 until img.width) bi.setRGB(x, y, img.argb[y * img.width + x])
        val wr = ImageIO.getImageWritersByFormatName("jpeg").next()
        val p = wr.defaultWriteParam
        p.compressionMode = ImageWriteParam.MODE_EXPLICIT; p.compressionQuality = quality / 100f; p.progressiveMode = ImageWriteParam.MODE_DISABLED
        val bo = ByteArrayOutputStream()
        ImageIO.createImageOutputStream(bo).use { ios -> wr.output = ios; wr.write(null, IIOImage(bi, null, null), p) }
        wr.dispose()
        return bo.toByteArray()
    }
}

/** Un "decodificador H.264" falso: N fotogramas a [srcFps] con marcas de tiempo reales; cuenta cuantos convierte. */
class FakeVideoSource(
    override val width: Int, override val height: Int, override val rotation: Int, private val frames: Int, private val srcFps: Double,
    private val corner: Int = 0xFF2040C0.toInt(),
) : VideoFrameSource {
    var converted = 0
    var closed = false
    private var i = 0
    override val durationMs: Long get() = (frames * 1000 / srcFps).toLong()
    override val fps: Double? get() = srcFps
    override fun nextFrame(outW: Int, outH: Int, keep: (Long) -> Boolean): VideoFrame? {
        while (i < frames) {
            val pts = (i * 1_000_000 / srcFps).toLong(); i++
            if (!keep(pts)) continue
            converted++
            // degradado con una esquina de color: asi se puede ver el giro
            val px = IntArray(outW * outH) { k -> val x = k % outW; val y = k / outW; if (x < outW / 4 && y < outH / 4) corner else (0xFF shl 24) or ((x * 255 / outW) shl 16) or ((y * 255 / outH) shl 8) }
            return VideoFrame(pts, RawImage(outW, outH, px))
        }
        return null
    }
    override fun close() { closed = true }
}

class FakeAudioSource(override val sampleRate: Int, override val channels: Int, private val seconds: Double) : AudioPcmSource {
    private var pos = 0L
    private val total = (sampleRate * seconds).toLong()
    var closed = false
    override val durationMs: Long get() = (seconds * 1000).toLong()
    override fun read(buf: ShortArray): Int {
        if (pos >= total) return -1
        val frames = minOf(buf.size / channels.toLong(), total - pos).toInt()
        for (i in 0 until frames) for (c in 0 until channels) buf[i * channels + c] = (Math.sin(2 * Math.PI * 440 * (pos + i) / sampleRate) * 10000).toInt().toShort()
        pos += frames
        return frames * channels
    }
    override fun close() { closed = true }
}

class TranscodeTest {
    private lateinit var dir: File
    private val codec = JvmImageCodec()
    @BeforeTest fun setUp() { dir = createTempDir("fxtrans") }
    @AfterTest fun tearDown() { dir.deleteRecursively() }

    private fun facts(f: File) = FileSource(f).use { MediaAnalyzer.analyze(it) }
    private fun conv(src: File, plan: Plan, out: File, c: StandardConverter, cancelled: () -> Boolean = { false }): Analysis {
        val a = facts(src)
        c.convert(src, a.facts, plan, out, {}, cancelled)
        return facts(out)
    }
    private fun file(name: String, b: ByteArray) = File(dir, name).also { it.writeBytes(b) }
    private fun pixel(img: RawImage, x: Int, y: Int) = img.argb[y * img.width + x]

    // ------------------------------------------------------------------ geometria
    @Test fun `fitVideo - lado largo 640, multiplo de 8, minimo 16 y sin ampliar`() {
        assertEquals(640 to 360, Geometry.fitVideo(1920, 1080))
        assertEquals(360 to 640, Geometry.fitVideo(1080, 1920))
        assertEquals(320 to 240, Geometry.fitVideo(320, 240))
        assertEquals(176 to 144, Geometry.fitVideo(176, 144))
        assertEquals(16 to 16, Geometry.fitVideo(3, 5))
        val (w, h) = Geometry.fitVideo(1001, 563); assertTrue(w % 8 == 0 && h % 8 == 0 && maxOf(w, h) <= 648, "$w x $h")
        assertEquals(1600 to 900, Geometry.fitPhoto(4000, 2250)); assertEquals(800 to 600, Geometry.fitPhoto(800, 600))
    }

    @Test fun `orientaciones EXIF - cada una mueve las esquinas donde toca`() {
        // imagen 3x2: A B C / D E F  (identificadas por su valor)
        val img = RawImage(3, 2, intArrayOf(1, 2, 3, 4, 5, 6))
        fun o(n: Int) = Orient.apply(img, n).let { Triple(it.width, it.height, it.argb.toList()) }
        assertEquals(Triple(3, 2, listOf(1, 2, 3, 4, 5, 6)), o(1))
        assertEquals(Triple(3, 2, listOf(3, 2, 1, 6, 5, 4)), o(2))
        assertEquals(Triple(3, 2, listOf(6, 5, 4, 3, 2, 1)), o(3))
        assertEquals(Triple(3, 2, listOf(4, 5, 6, 1, 2, 3)), o(4))
        assertEquals(Triple(2, 3, listOf(1, 4, 2, 5, 3, 6)), o(5))
        assertEquals(Triple(2, 3, listOf(4, 1, 5, 2, 6, 3)), o(6))
        assertEquals(Triple(2, 3, listOf(6, 3, 5, 2, 4, 1)), o(7))
        assertEquals(Triple(2, 3, listOf(3, 6, 2, 5, 1, 4)), o(8))
    }

    // ------------------------------------------------------------------ fotos
    private val photos = StandardConverter(codec)

    @Test fun `PNG con alfa pasa a JPEG baseline del perfil, aplanado sobre blanco`() {
        val src = file("a.png", MediaFixtures.png(300, 200))
        val an = conv(src, Plan.TRANSCODE, File(dir, "o.jpg"), photos)
        assertEquals(Plan.NONE, an.plan, an.toString()); assertEquals("jpeg", an.facts.container); assertEquals(300, an.facts.width)
    }

    @Test fun `JPEG progresivo de 4000 px pasa a baseline de 1600 px`() {
        val src = file("p.jpg", MediaFixtures.jpeg(4000, 2500, progressive = true, quality = 0.5f))
        val an = conv(src, Plan.TRANSCODE, File(dir, "o.jpg"), photos)
        assertEquals(Plan.NONE, an.plan, an.toString()); assertEquals(1600, an.facts.width); assertEquals(1000, an.facts.height)
    }

    @Test fun `JPEG girado por EXIF sale derecho, con el giro ya aplicado y sin EXIF`() {
        val src = file("r.jpg", MediaFixtures.jpeg(300, 200, orientation = 6))
        val an = conv(src, Plan.TRANSCODE, File(dir, "o.jpg"), photos)
        assertEquals(Plan.NONE, an.plan, an.toString())
        assertEquals(200, an.facts.width, "el 6 intercambia"); assertEquals(300, an.facts.height); assertEquals(1, an.facts.orientation)
    }

    @Test fun `una foto que Android no sabe abrir da un error claro y no deja archivo`() {
        val src = file("x.jpg", MediaFixtures.heic(100, 100))
        val out = File(dir, "o.jpg")
        val ex = assertFailsWith<MediaException> { photos.convert(src, facts(src).facts, Plan.TRANSCODE, out, {}, { false }) }
        assertTrue(ex.message!!.contains("formato de imagen"), ex.message); assertTrue(!out.exists())
    }

    @Test fun `miniaturas - foto, vídeo MJPEG y sin miniatura para audio`() {
        val p = photos.thumbnail(file("t.jpg", MediaFixtures.jpeg(4000, 3000, quality = 0.9f)), facts(file("t2.jpg", MediaFixtures.jpeg(4000, 3000, quality = 0.9f))).facts)!!
        val pj = Jpeg.parse(BytesSource(p))!!
        assertTrue(p.size <= MediaProfile.THUMB_MAX_BYTES && pj.p4Decodable && maxOf(pj.width, pj.height) == MediaProfile.THUMB_SIDE, "${p.size} ${pj.width}x${pj.height}")
        val avi = MediaFixtures.aviMjpeg(dir, "v.avi", 320, 240, 5, 12)
        val tv = photos.thumbnail(avi, facts(avi).facts)
        assertNotNull(tv); assertEquals(256, Jpeg.parse(BytesSource(tv))!!.width)
        assertNull(photos.thumbnail(file("a.wav", MediaFixtures.wavPcm(22050, 1, 16, 0.2)), facts(file("a2.wav", MediaFixtures.wavPcm(22050, 1, 16, 0.2))).facts))
    }

    // ------------------------------------------------------------------ vídeo
    @Test fun `H264 de 1080p a 30 fps pasa a AVI MJPEG de 640x360 a 12 fps, y solo convierte los fotogramas que se quedan`() {
        val src = file("v.mp4", MediaFixtures.mov("avc1", 1920, 1080, List(3) { ByteArray(500) }, fps = 30, brand = "isom"))
        val fake = FakeVideoSource(1920, 1080, 0, frames = 90, srcFps = 30.0)
        val c = StandardConverter(codec, videoSource = { fake })
        val out = File(dir, "o.avi")
        val an = conv(src, Plan.TRANSCODE, out, c)
        assertEquals(Plan.NONE, an.plan, an.toString())
        assertEquals(640, an.facts.width); assertEquals(360, an.facts.height); assertEquals(1200, an.facts.fpsX100)
        assertEquals(36, frameCount(out), "3 s a 12 fps")
        assertEquals(36, fake.converted, "los otros 54 no se convirtieron a RGB")
        assertTrue(fake.closed)
    }

    private fun frameCount(f: File) = FileSource(f).use { Avi.parseHeader(it)!!.totalFrames.toInt() }

    @Test fun `un movil en vertical (giro 90) sale derecho y vertical`() {
        val src = file("v.mp4", MediaFixtures.mov("avc1", 1920, 1080, List(3) { ByteArray(500) }, fps = 24, rotation = 90, brand = "isom"))
        val fake = FakeVideoSource(1920, 1080, 90, frames = 24, srcFps = 24.0)
        val out = File(dir, "o.avi")
        val an = conv(src, Plan.TRANSCODE, out, StandardConverter(codec, videoSource = { fake }))
        assertEquals(Plan.NONE, an.plan, an.toString())
        assertEquals(360, an.facts.width); assertEquals(640, an.facts.height)
        // la esquina de color estaba arriba a la izquierda del fotograma; girado 90 horario queda arriba a la DERECHA
        val first = FileSource(out).use { s -> val h = Avi.parseHeader(s)!!; Avi.FrameCursor(s, h).next()!! }
        val img = codec.decode(first, 360, 640, exact = true)!!
        val tr = pixel(img, 340, 20); val tl = pixel(img, 20, 20)
        assertTrue((tr and 0xFF) > 0x80 && ((tr shr 16) and 0xFF) < 0x80, "esquina de color arriba a la derecha: ${Integer.toHexString(tr)}")
        assertTrue((tl and 0xFF) < 0x40, "arriba a la izquierda ya no esta la esquina de color: ${Integer.toHexString(tl)}")
    }

    @Test fun `un vídeo de 10 fps se queda en 10, no se inventan fotogramas`() {
        val src = file("v.mp4", MediaFixtures.mov("avc1", 640, 480, List(3) { ByteArray(500) }, fps = 10, brand = "isom"))
        val out = File(dir, "o.avi")
        val an = conv(src, Plan.TRANSCODE, out, StandardConverter(codec, videoSource = { FakeVideoSource(640, 480, 0, 20, 10.0) }))
        assertEquals(1000, an.facts.fpsX100); assertEquals(20, frameCount(out))
    }

    @Test fun `MJPEG de 1280x720 en AVI se recodifica por fotogramas sin MediaCodec`() {
        val src = MediaFixtures.aviMjpeg(dir, "big.avi", 1280, 720, 4, 24)
        val out = File(dir, "o.avi")
        val an = conv(src, Plan.TRANSCODE, out, StandardConverter(codec))
        assertEquals(Plan.NONE, an.plan, an.toString()); assertEquals(640, an.facts.width); assertEquals(360, an.facts.height)
        assertEquals(2, frameCount(out), "4 fotogramas a 24 fps = 1/6 s: 2 a 12 fps")
    }

    @Test fun `MOV con JPEG girado se recodifica y sale derecho`() {
        val src = file("r.mov", MediaFixtures.mov("jpeg", 160, 120, MediaFixtures.mjpegFrames(160, 120, 6), fps = 12, rotation = 90))
        val out = File(dir, "o.avi")
        val an = conv(src, Plan.TRANSCODE, out, StandardConverter(codec))
        assertEquals(Plan.NONE, an.plan, an.toString()); assertEquals(120, an.facts.width); assertEquals(160, an.facts.height)
    }

    @Test fun `la conversion de vídeo se cancela y no deja nada`() {
        val src = file("v.mp4", MediaFixtures.mov("avc1", 640, 480, List(3) { ByteArray(500) }, fps = 24, brand = "isom"))
        val out = File(dir, "o.avi")
        var n = 0
        val c = StandardConverter(codec, videoSource = { FakeVideoSource(640, 480, 0, 240, 24.0) })
        assertFailsWith<MediaException> { c.convert(src, facts(src).facts, Plan.TRANSCODE, out, {}, { ++n > 5 }) }
        assertTrue(!out.exists())
    }

    @Test fun `un vídeo sin ningun fotograma legible falla con motivo`() {
        val src = file("v.mp4", MediaFixtures.mov("avc1", 640, 480, List(3) { ByteArray(500) }, fps = 24, brand = "isom"))
        val c = StandardConverter(codec, videoSource = { FakeVideoSource(640, 480, 0, 0, 24.0) })
        val ex = assertFailsWith<MediaException> { c.convert(src, facts(src).facts, Plan.TRANSCODE, File(dir, "o.avi"), {}, { false }) }
        assertTrue(ex.message!!.contains("ningún fotograma"), ex.message)
    }

    // ------------------------------------------------------------------ audio
    @Test fun `AAC (cualquier audio que Android decodifique) pasa a WAV IMA mono de 22,05 kHz`() {
        val src = file("a.m4a", MediaFixtures.m4a("mp4a", 44100, 2, 10))
        val out = File(dir, "o.wav")
        val an = conv(src, Plan.TRANSCODE, out, StandardConverter(codec, audioSource = { FakeAudioSource(44100, 2, 3.0) }))
        assertEquals(Plan.NONE, an.plan, an.toString())
        assertEquals(22050, an.facts.sampleRate); assertEquals(1, an.facts.channels)
        assertTrue(Math.abs(an.facts.durationMs!! - 3000) <= 5, "${an.facts.durationMs}")
    }

    @Test fun `un audio de 48 kHz y otro de 6 kHz acaban en el rango del P4`() {
        val src = file("a.m4a", MediaFixtures.m4a("mp4a", 48000, 1, 10))
        for ((rate, want) in listOf(48000 to 22050, 6000 to 8000)) {
            val out = File(dir, "o$rate.wav")
            val an = conv(src, Plan.TRANSCODE, out, StandardConverter(codec, audioSource = { FakeAudioSource(rate, 1, 2.0) }))
            assertEquals(Plan.NONE, an.plan, an.toString()); assertEquals(want, an.facts.sampleRate)
        }
    }

    @Test fun `que no haya decodificador de audio se dice, y los WAV no lo necesitan`() {
        val m4a = file("a.m4a", MediaFixtures.m4a("mp4a", 44100, 2, 10))
        val c = StandardConverter(codec)
        assertTrue(!c.canConvert(facts(m4a).facts, Plan.TRANSCODE))
        assertTrue(c.canConvert(facts(file("w.wav", MediaFixtures.wavPcm(48000, 2, 24, 0.2))).facts, Plan.TRANSCODE))
        assertFailsWith<MediaException> { c.convert(m4a, facts(m4a).facts, Plan.TRANSCODE, File(dir, "o.wav"), {}, { false }) }
    }

    // ------------------------------------------------------------------ que sabe hacer cada configuracion
    @Test fun `canConvert segun lo que haya detras`() {
        val h264 = facts(file("v.mp4", MediaFixtures.mov("avc1", 640, 480, List(3) { ByteArray(500) }, fps = 24, brand = "isom"))).facts
        val png = facts(file("p.png", MediaFixtures.png(10, 10))).facts
        val mjpegAvi = facts(MediaFixtures.aviMjpeg(dir, "m.avi", 1280, 720, 2, 24)).facts
        val none = StandardConverter(null)
        assertTrue(!none.canConvert(h264, Plan.TRANSCODE) && !none.canConvert(png, Plan.TRANSCODE) && !none.canConvert(mjpegAvi, Plan.TRANSCODE))
        val imgOnly = StandardConverter(codec)
        assertTrue(!imgOnly.canConvert(h264, Plan.TRANSCODE) && imgOnly.canConvert(png, Plan.TRANSCODE) && imgOnly.canConvert(mjpegAvi, Plan.TRANSCODE))
        assertTrue(StandardConverter(codec, videoSource = { null }).canConvert(h264, Plan.TRANSCODE))
        assertTrue(none.canConvert(h264, Plan.REMUX) && !none.canConvert(h264, Plan.NONE) && !none.canConvert(h264, Plan.UNSUPPORTED))
    }

    @Test fun `un remux que falla en un fotograma cae a recodificar`() {
        val mixed = MediaFixtures.mjpegFrames(160, 120, 3) + MediaFixtures.jpeg(160, 120, 9, progressive = true)
        val src = file("mix.mov", MediaFixtures.mov("jpeg", 160, 120, mixed, fps = 12))
        val out = File(dir, "o.avi")
        // el analizador diria REMUX (el primer fotograma vale); el remux falla en el 4.o y se recodifica
        val an = conv(src, Plan.REMUX, out, StandardConverter(codec))
        assertEquals(Plan.NONE, an.plan, an.toString())
        assertEquals(4, frameCount(out))
    }
}
