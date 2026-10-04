package com.flexos.flexphone.cloud.media

import java.io.File
import kotlin.test.AfterTest
import kotlin.test.BeforeTest
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import kotlin.test.assertTrue

/**
 * El analizador contra archivos de VERDAD: cada caso del perfil (que pasa tal cual, que se re-empaqueta,
 * que se recodifica, que no se puede, que esta roto) y que los datos que lee sean los reales.
 */
class MediaAnalyzerTest {
    private lateinit var dir: File
    @BeforeTest fun setUp() { dir = createTempDir("fxmedia") }
    @AfterTest fun tearDown() { dir.deleteRecursively() }

    private fun a(b: ByteArray) = MediaAnalyzer.analyze(BytesSource(b))
    private fun a(f: File) = FileSource(f).use { MediaAnalyzer.analyze(it) }
    private fun assertPlan(p: Plan, an: Analysis) = assertEquals(p, an.plan, an.toString())

    // ------------------------------------------------------------------ fotos
    @Test fun `JPEG baseline dentro del perfil - NONE, sin tocar`() {
        val an = a(MediaFixtures.jpeg(640, 480))
        assertPlan(Plan.NONE, an)
        assertEquals(MediaKind.PHOTO, an.facts.kind); assertEquals(640, an.facts.width); assertEquals(480, an.facts.height)
        assertEquals(1, an.facts.orientation)
    }

    @Test fun `JPEG en gris y de lado grande pero legal siguen siendo NONE`() {
        assertPlan(Plan.NONE, a(MediaFixtures.jpeg(300, 200, gray = true)))
        assertPlan(Plan.NONE, a(MediaFixtures.jpeg(4000, 3000, quality = 0.3f)))
    }

    @Test fun `JPEG progresivo - TRANSCODE (el P4 solo abre baseline)`() {
        val an = a(MediaFixtures.jpeg(320, 240, progressive = true))
        assertPlan(Plan.TRANSCODE, an); assertTrue(an.reasons.any { it.contains("progresivo") }, an.toString())
    }

    @Test fun `JPEG girado por EXIF - TRANSCODE porque el P4 no lee EXIF`() {
        val an = a(MediaFixtures.jpeg(320, 240, orientation = 6))
        assertPlan(Plan.TRANSCODE, an); assertEquals(6, an.facts.orientation)
        assertPlan(Plan.NONE, a(MediaFixtures.jpeg(320, 240, orientation = 1)))
    }

    @Test fun `JPEG de mas de 4096 px o de mas de 8 MB - TRANSCODE`() {
        assertPlan(Plan.TRANSCODE, a(MediaFixtures.jpeg(4200, 100, quality = 0.3f)))
        val big = MediaFixtures.jpeg(1200, 800, quality = 0.9f)
        val padded = big.copyOf(big.size) // no se puede engordar sin romper el EOI: se rellena antes del final
        val withPad = padded.copyOfRange(0, padded.size - 2) + ByteArray((MediaProfile.PHOTO_MAX_BYTES + 10).toInt()) + byteArrayOf(0xFF.toByte(), 0xD9.toByte())
        assertPlan(Plan.TRANSCODE, a(withPad))
    }

    @Test fun `JPEG cortado o con cabecera rota - CORRUPT`() {
        val j = MediaFixtures.jpeg(320, 240)
        assertPlan(Plan.CORRUPT, a(j.copyOf(j.size / 2)))
        assertPlan(Plan.CORRUPT, a(byteArrayOf(0xFF.toByte(), 0xD8.toByte(), 0xFF.toByte(), 0xE0.toByte(), 0, 4, 1, 2)))
    }

    @Test fun `PNG GIF BMP WebP HEIC - TRANSCODE a JPEG con sus medidas`() {
        a(MediaFixtures.png(120, 80)).let { assertPlan(Plan.TRANSCODE, it); assertEquals(120, it.facts.width); assertEquals(80, it.facts.height) }
        a(MediaFixtures.gif(64, 48)).let { assertPlan(Plan.TRANSCODE, it); assertEquals(64, it.facts.width) }
        a(MediaFixtures.bmp(50, 40)).let { assertPlan(Plan.TRANSCODE, it); assertEquals(50, it.facts.width); assertEquals(40, it.facts.height) }
        a(MediaFixtures.webpHeader(333, 222)).let { assertPlan(Plan.TRANSCODE, it); assertEquals(333, it.facts.width); assertEquals(222, it.facts.height) }
        a(MediaFixtures.heic(4032, 3024)).let { assertPlan(Plan.TRANSCODE, it); assertEquals(4032, it.facts.width); assertEquals(3024, it.facts.height) }
    }

    @Test fun `PNG cortado - CORRUPT`() {
        val p = MediaFixtures.png(200, 200)
        assertPlan(Plan.CORRUPT, a(p.copyOf(p.size - 20)))
    }

    // ------------------------------------------------------------------ vídeo AVI
    @Test fun `AVI MJPEG baseline pequeño - NONE`() {
        val an = a(MediaFixtures.aviMjpeg(dir, "ok.avi", 320, 240, 10, 12))
        assertPlan(Plan.NONE, an)
        assertEquals("mjpeg", an.facts.videoCodec); assertEquals(320, an.facts.width); assertEquals(1200, an.facts.fpsX100)
        assertEquals(833L, an.facts.durationMs)
    }

    @Test fun `AVI MJPEG demasiado grande o demasiado rapido - TRANSCODE`() {
        assertPlan(Plan.TRANSCODE, a(MediaFixtures.aviMjpeg(dir, "big.avi", 1280, 720, 3, 12)))
        val fast = a(MediaFixtures.aviMjpeg(dir, "fast.avi", 320, 240, 30, 30))
        assertPlan(Plan.TRANSCODE, fast); assertTrue(fast.reasons.any { it.contains("fotogramas por segundo") }, fast.toString())
    }

    @Test fun `AVI con fotogramas progresivos - TRANSCODE`() {
        val frames = (0 until 3).map { MediaFixtures.jpeg(320, 240, it, progressive = true) }
        assertPlan(Plan.TRANSCODE, a(MediaFixtures.aviMjpeg(dir, "prog.avi", 320, 240, 3, 12, frames)))
    }

    @Test fun `AVI cuyo fccHandler no dice MJPG pero el strf si - REMUX (solo se reescribe la cabecera)`() {
        val f = MediaFixtures.aviMjpeg(dir, "nohandler.avi", 320, 240, 5, 12)
        MediaFixtures.patchAviCodec(f, "", null)
        assertPlan(Plan.REMUX, a(f))
    }

    @Test fun `AVI con H264 o Xvid - UNSUPPORTED claro (Android no abre AVI)`() {
        val f = MediaFixtures.aviMjpeg(dir, "h264.avi", 320, 240, 5, 12)
        MediaFixtures.patchAviCodec(f, "H264", "H264")
        val an = a(f)
        assertPlan(Plan.UNSUPPORTED, an); assertEquals("h264", an.facts.videoCodec)
    }

    @Test fun `AVI roto o sin fotogramas - CORRUPT`() {
        val f = MediaFixtures.aviMjpeg(dir, "ok2.avi", 320, 240, 5, 12)
        val bytes = f.readBytes()
        assertPlan(Plan.CORRUPT, a(bytes.copyOf(100)))
        assertPlan(Plan.CORRUPT, a("RIFF\u0000\u0000\u0000\u0000AVI ".toByteArray(Charsets.ISO_8859_1) + ByteArray(64)))
    }

    // ------------------------------------------------------------------ audio WAV
    @Test fun `WAV PCM16 mono o estereo y IMA mono dentro del perfil - NONE`() {
        assertPlan(Plan.NONE, a(MediaFixtures.wavPcm(22050, 1, 16, 0.5)))
        assertPlan(Plan.NONE, a(MediaFixtures.wavPcm(44100, 2, 16, 0.5)))
        assertPlan(Plan.NONE, a(MediaFixtures.wavPcm(11025, 1, 8, 0.5)))
        val ima = a(MediaFixtures.wavIma(dir, "i.wav", 22050, 2.0))
        assertPlan(Plan.NONE, ima); assertEquals("ima_adpcm", ima.facts.audioCodec); assertEquals(2000L, ima.facts.durationMs)
    }

    @Test fun `WAV 24 bits, float, 5_1 o fuera de frecuencia - TRANSCODE`() {
        assertPlan(Plan.TRANSCODE, a(MediaFixtures.wavPcm(48000, 2, 24, 0.3)))
        assertPlan(Plan.TRANSCODE, a(MediaFixtures.wavPcm(44100, 2, 32, 0.3, tag = 3)))
        assertPlan(Plan.TRANSCODE, a(MediaFixtures.wavPcm(4000, 1, 16, 0.3)))
        assertPlan(Plan.TRANSCODE, a(MediaFixtures.wavPcm(192000, 1, 16, 0.1)))
        assertPlan(Plan.TRANSCODE, a(MediaFixtures.ulawWav(8000, 0.5)))
    }

    @Test fun `WAV con un codec que ni sabemos leer - UNSUPPORTED, cortado o vacio - CORRUPT`() {
        val ms = MediaFixtures.wavFile(2, 1, 22050, 4, 512, ByteArray(2048), null)
        assertPlan(Plan.UNSUPPORTED, a(ms))
        val w = MediaFixtures.wavPcm(22050, 1, 16, 1.0)
        assertPlan(Plan.CORRUPT, a(w.copyOf(w.size / 2)))
        assertPlan(Plan.CORRUPT, a(w.copyOf(30)))
        assertPlan(Plan.CORRUPT, a(MediaFixtures.wavFile(1, 1, 22050, 16, 2, ByteArray(0), null)))
    }

    // ------------------------------------------------------------------ MP4 / MOV / MKV
    private fun fakeFrames(n: Int) = MediaFixtures.mjpegFrames(160, 120, n, 0.5f)

    @Test fun `MP4 con H264 - TRANSCODE con codec, tamano, duracion y giro reales`() {
        val m = MediaFixtures.mov("avc1", 1920, 1080, List(24) { ByteArray(1000) }, fps = 24, rotation = 90, brand = "isom")
        val an = a(m)
        assertPlan(Plan.TRANSCODE, an)
        assertEquals("h264", an.facts.videoCodec); assertEquals("mp4", an.facts.container)
        assertEquals(1080, an.facts.width, "vertical: el giro de 90 intercambia"); assertEquals(1920, an.facts.height); assertEquals(90, an.facts.rotation)
        assertEquals(2400, an.facts.fpsX100); assertEquals(1000L, an.facts.durationMs)
    }

    @Test fun `MP4 con HEVC y con VP9 - TRANSCODE, con ProRes - UNSUPPORTED`() {
        assertPlan(Plan.TRANSCODE, a(MediaFixtures.mov("hvc1", 3840, 2160, List(5) { ByteArray(500) }, brand = "isom")))
        assertPlan(Plan.TRANSCODE, a(MediaFixtures.mov("vp09", 1280, 720, List(5) { ByteArray(500) }, brand = "isom")))
        assertPlan(Plan.UNSUPPORTED, a(MediaFixtures.mov("apch", 1920, 1080, List(5) { ByteArray(500) })))
    }

    @Test fun `MP4 con la moov al principio (faststart) o al final se leen igual`() {
        val frames = List(10) { ByteArray(800) }
        assertEquals(Plan.TRANSCODE, a(MediaFixtures.mov("avc1", 640, 360, frames, brand = "isom", moovFirst = true)).plan)
        assertEquals(Plan.TRANSCODE, a(MediaFixtures.mov("avc1", 640, 360, frames, brand = "isom", moovFirst = false)).plan)
    }

    @Test fun `MP4 sin moov (grabacion interrumpida) - CORRUPT`() {
        assertPlan(Plan.CORRUPT, a(MediaFixtures.mov("avc1", 640, 360, List(10) { ByteArray(800) }, brand = "isom", withMoov = false)))
    }

    @Test fun `MOV con JPEG por fotograma dentro del perfil - REMUX, girado o grande - TRANSCODE`() {
        val ok = a(MediaFixtures.mov("jpeg", 160, 120, fakeFrames(6), fps = 12))
        assertPlan(Plan.REMUX, ok); assertEquals("mjpeg", ok.facts.videoCodec)
        assertPlan(Plan.TRANSCODE, a(MediaFixtures.mov("jpeg", 160, 120, fakeFrames(6), fps = 12, rotation = 90)))
        assertPlan(Plan.TRANSCODE, a(MediaFixtures.mov("jpeg", 1600, 900, fakeFrames(2), fps = 12)))
        assertPlan(Plan.TRANSCODE, a(MediaFixtures.mov("jpeg", 160, 120, fakeFrames(6), fps = 30)))
    }

    @Test fun `M4A con AAC - TRANSCODE de audio, con ALAC - UNSUPPORTED`() {
        val an = a(MediaFixtures.m4a("mp4a", 44100, 2, 10))
        assertPlan(Plan.TRANSCODE, an); assertEquals(MediaKind.AUDIO, an.facts.kind); assertEquals("aac", an.facts.audioCodec); assertEquals(44100, an.facts.sampleRate)
        assertPlan(Plan.UNSUPPORTED, a(MediaFixtures.m4a("alac", 44100, 2, 10)))
    }

    @Test fun `WebM y Matroska - TRANSCODE con sus pistas leidas`() {
        val w = a(MediaFixtures.matroska("webm", "V_VP9", 1280, 720, "A_OPUS"))
        assertPlan(Plan.TRANSCODE, w)
        assertEquals("webm", w.facts.container); assertEquals("vp9", w.facts.videoCodec); assertEquals("opus", w.facts.audioCodec)
        assertEquals(1280, w.facts.width); assertEquals(720, w.facts.height); assertEquals(10_000L, w.facts.durationMs); assertEquals(3000, w.facts.fpsX100)
        val m = a(MediaFixtures.matroska("matroska", "V_MPEG4/ISO/AVC", 1920, 1080, "A_AAC"))
        assertPlan(Plan.TRANSCODE, m); assertEquals("mkv", m.facts.container); assertEquals("h264", m.facts.videoCodec)
        val audioOnly = a(MediaFixtures.matroska("webm", null, 0, 0, "A_VORBIS", 44100, 2))
        assertPlan(Plan.TRANSCODE, audioOnly); assertEquals(MediaKind.AUDIO, audioOnly.facts.kind)
    }

    @Test fun `la extension no cuenta - un MKV llamado mp4 sigue siendo MKV`() {
        val f = File(dir, "pelicula.mp4"); f.writeBytes(MediaFixtures.matroska("matroska", "V_MPEG4/ISO/AVC", 1920, 1080, null))
        assertEquals("mkv", a(f).facts.container)
        val g = File(dir, "foto.jpg"); g.writeBytes(MediaFixtures.png(10, 10))
        assertEquals("png", a(g).facts.container)
    }

    // ------------------------------------------------------------------ audio suelto
    @Test fun `MP3 FLAC OGG AAC - TRANSCODE, con la duracion solo si es fiable`() {
        val mp3 = a(MediaFixtures.mp3(44100, 2, xingFrames = 1000))
        assertPlan(Plan.TRANSCODE, mp3); assertEquals(44100, mp3.facts.sampleRate); assertEquals(2, mp3.facts.channels); assertEquals(26_122L, mp3.facts.durationMs)
        assertNull(a(MediaFixtures.mp3(44100, 2, xingFrames = null)).facts.durationMs, "sin cabecera Xing no se inventa la duracion")
        assertPlan(Plan.TRANSCODE, a(MediaFixtures.mp3(48000, 1, id3 = true)))
        val fl = a(MediaFixtures.flac(44100, 2, 441000))
        assertPlan(Plan.TRANSCODE, fl); assertEquals(10_000L, fl.facts.durationMs); assertEquals(2, fl.facts.channels)
        assertPlan(Plan.TRANSCODE, a(MediaFixtures.oggOpus(2))); assertPlan(Plan.TRANSCODE, a(MediaFixtures.oggVorbis(2, 44100)))
        assertPlan(Plan.TRANSCODE, a(MediaFixtures.aacAdts(4, 2)))
    }

    @Test fun `no multimedia - OTHER sin tocar, vacio - CORRUPT, formatos sin conversion - UNSUPPORTED`() {
        val an = a("%PDF-1.7 hola".toByteArray())
        assertPlan(Plan.NONE, an); assertEquals(MediaKind.OTHER, an.facts.kind)
        assertPlan(Plan.CORRUPT, a(ByteArray(0)))
        assertPlan(Plan.UNSUPPORTED, a("FORM\u0000\u0000\u0000\u0000AIFF".toByteArray(Charsets.ISO_8859_1) + ByteArray(40)))
        assertPlan(Plan.UNSUPPORTED, a(byteArrayOf(0x30, 0x26, 0xB2.toByte(), 0x75) + ByteArray(60)))
    }

    @Test fun `los metadatos solo llevan lo que se leyo`() {
        val m = a(MediaFixtures.jpeg(64, 48)).facts.toMetadata()
        assertEquals(64, m["width"]); assertEquals(48, m["height"]); assertEquals("jpeg", m["container"])
        assertTrue("durationMs" !in m && "codec" !in m && "sampleRate" !in m, m.toString())
        val v = a(MediaFixtures.matroska("webm", "V_VP9", 640, 480, null)).facts.toMetadata()
        assertEquals("vp9", v["codec"]); assertTrue("audioCodec" !in v)
    }
}
