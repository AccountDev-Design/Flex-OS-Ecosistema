package com.flexos.flexphone.cloud.media

import java.io.File
import kotlin.test.AfterTest
import kotlin.test.BeforeTest
import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertNotNull
import kotlin.test.assertTrue

/** Escribir y re-empaquetar sin perder nada: AVI MJPEG, WAV IMA, remuxes y el remuestreo. */
class RemuxTest {
    private lateinit var dir: File
    @BeforeTest fun setUp() { dir = createTempDir("fxremux") }
    @AfterTest fun tearDown() { dir.deleteRecursively() }

    private fun analyze(f: File) = FileSource(f).use { MediaAnalyzer.analyze(it) }
    private fun frames(f: File): List<ByteArray> {
        val out = ArrayList<ByteArray>()
        FileSource(f).use { s -> val h = Avi.parseHeader(s)!!; Avi.forEachFrame(s, h) { _, b -> out.add(b); true } }
        return out
    }

    // ------------------------------------------------------------------ escritor AVI
    @Test fun `el AVI escrito tiene la cabecera de 224 bytes, indice y fotogramas intactos (incluido uno impar)`() {
        val fr = MediaFixtures.mjpegFrames(160, 120, 7)
        val odd = fr.indexOfFirst { it.size % 2 == 1 }
        val f = MediaFixtures.aviMjpeg(dir, "w.avi", 160, 120, 7, 10, fr)
        FileSource(f).use { s ->
            val h = Avi.parseHeader(s)!!
            assertEquals(160, h.width); assertEquals(120, h.height); assertEquals(100_000L, h.usPerFrame); assertEquals(7L, h.totalFrames)
            assertEquals("mjpg", h.videoHandler)
            assertEquals(220L, h.moviPos, "la cabecera mide 224 bytes: LIST movi en 212")
            val idx = Avi.readIndex(s, h)!!
            assertEquals(7, idx.videoFrames); assertEquals(fr.maxOf { it.size }, idx.maxFrameBytes)
        }
        val back = frames(f)
        assertEquals(7, back.size)
        for (i in fr.indices) assertContentEquals(fr[i], back[i], "fotograma $i")
        if (odd >= 0) assertTrue(f.length() % 2 == 0L)
    }

    @Test fun `el AVI no acepta un fotograma vacio ni medidas absurdas`() {
        assertFailsWith<IllegalArgumentException> { AviMjpegWriter(File(dir, "a.avi"), 0, 10, 12) }
        assertFailsWith<IllegalArgumentException> { AviMjpegWriter(File(dir, "b.avi"), 10, 10, 0) }
        AviMjpegWriter(File(dir, "c.avi"), 16, 16, 12).use { w -> assertFailsWith<java.io.IOException> { w.addFrame(ByteArray(0)) } }
    }

    // ------------------------------------------------------------------ remux de vídeo
    @Test fun `MOV con JPEG por fotograma pasa a AVI SIN recodificar (los bytes son los mismos) y queda NONE`() {
        val fr = MediaFixtures.mjpegFrames(160, 120, 8)
        val mov = File(dir, "in.mov").also { it.writeBytes(MediaFixtures.mov("jpeg", 160, 120, fr, fps = 12)) }
        val out = File(dir, "out.avi")
        FileSource(mov).use { Remux.toAvi(it, out) }
        val back = frames(out)
        assertEquals(8, back.size)
        for (i in fr.indices) assertContentEquals(fr[i], back[i], "fotograma $i recodificado")
        val an = analyze(out)
        assertEquals(Plan.NONE, an.plan, an.toString())
        assertEquals(1200, an.facts.fpsX100)
    }

    @Test fun `el remux se niega si un fotograma no vale (progresivo, grande, cortado) para que se recodifique`() {
        val mixed = MediaFixtures.mjpegFrames(160, 120, 3) + MediaFixtures.jpeg(160, 120, 9, progressive = true)
        val mov = File(dir, "mix.mov").also { it.writeBytes(MediaFixtures.mov("jpeg", 160, 120, mixed)) }
        val ex = assertFailsWith<MediaException> { FileSource(mov).use { Remux.toAvi(it, File(dir, "o.avi")) } }
        assertTrue(ex.message!!.contains("fotograma 4"), ex.message)
        val cut = File(dir, "cut.mov").also { val b = MediaFixtures.mov("jpeg", 160, 120, MediaFixtures.mjpegFrames(160, 120, 4)); it.writeBytes(b.copyOf(b.size - 3000)) }
        assertFailsWith<MediaException> { FileSource(cut).use { Remux.toAvi(it, File(dir, "o2.avi")) } }
    }

    @Test fun `AVI sin etiqueta de codec se reescribe a MJPG y el firmware lo aceptaria (NONE)`() {
        val f = MediaFixtures.aviMjpeg(dir, "nh.avi", 160, 120, 5, 12)
        MediaFixtures.patchAviCodec(f, "", null)
        assertEquals(Plan.REMUX, analyze(f).plan)
        val out = File(dir, "fixed.avi")
        FileSource(f).use { Remux.toAvi(it, out) }
        assertEquals(Plan.NONE, analyze(out).plan)
        assertEquals(5, frames(out).size)
    }

    // ------------------------------------------------------------------ remux de audio
    @Test fun `PCM de 16 bits dentro de MOV pasa a WAV con los mismos bytes`() {
        val pcm = ByteArray(22050 * 2) { (it * 7).toByte() }
        val mov = File(dir, "a.mov").also { it.writeBytes(MediaFixtures.mov("jpeg", 0, 0, emptyList(), audioPcm = pcm, audioRate = 22050, audioCh = 1)) }
        assertEquals(Plan.REMUX, analyze(mov).plan)
        val out = File(dir, "a.wav")
        FileSource(mov).use { Remux.pcmToWav(it, out) }
        val an = analyze(out)
        assertEquals(Plan.NONE, an.plan, an.toString())
        FileSource(out).use { s -> val w = Wav.parse(s)!!; assertContentEquals(pcm, s.bytes(w.dataPos, w.dataBytes.toInt())) }
    }

    // ------------------------------------------------------------------ WAV -> IMA
    private fun decodeIma(f: File): ShortArray {
        FileSource(f).use { s ->
            val w = Wav.parse(s)!!
            val out = ArrayList<Short>()
            WavPcm.readMono(s, w) { p, n -> for (i in 0 until n) out.add(p[i]) }
            return out.toShortArray()
        }
    }

    private fun snrDb(ref: ShortArray, test: ShortArray, lag: Int = 0): Double {
        var sig = 0.0; var err = 0.0
        val n = minOf(ref.size, test.size - lag)
        for (i in 200 until n - 200) { val r = ref[i].toDouble(); val d = r - test[i + lag]; sig += r * r; err += d * d }
        return 10 * Math.log10(sig / maxOf(err, 1e-9))
    }

    @Test fun `IMA ADPCM ida y vuelta de un seno - SNR de al menos 24 dB (el mismo criterio que la web)`() {
        for (rate in intArrayOf(8000, 11025, 22050)) {
            val pcm = ShortArray(rate * 2) { (Math.sin(2 * Math.PI * 440 * it / rate) * 12000).toInt().toShort() }
            val f = File(dir, "r$rate.wav")
            ImaWavWriter(f, rate).use { it.write(pcm) }
            val back = decodeIma(f)
            assertEquals(pcm.size, back.size, "muestras a $rate Hz")
            assertTrue(snrDb(pcm, back) >= 24, "SNR ${snrDb(pcm, back)} a $rate Hz")
        }
    }

    @Test fun `WAV de 24 bits estereo a 44,1 kHz pasa a IMA mono de 22,05 kHz dentro del perfil`() {
        val src = File(dir, "src.wav").also { it.writeBytes(MediaFixtures.wavPcm(44100, 2, 24, 2.0)) }
        val out = File(dir, "out.wav")
        FileSource(src).use { WavConvert.toProfile(it, out) }
        val an = analyze(out)
        assertEquals(Plan.NONE, an.plan, an.toString())
        assertEquals(22050, an.facts.sampleRate); assertEquals(1, an.facts.channels); assertEquals("ima_adpcm", an.facts.audioCodec)
        assertTrue(Math.abs(an.facts.durationMs!! - 2000) <= 5, "duracion ${an.facts.durationMs}")
    }

    @Test fun `float, 32 bits, u-law y 8 bits tambien se convierten, y un 8 kHz no se sube de frecuencia`() {
        val cases = listOf(MediaFixtures.wavPcm(48000, 1, 32, 1.0, tag = 3), MediaFixtures.wavPcm(44100, 2, 32, 1.0), MediaFixtures.ulawWav(8000, 1.0), MediaFixtures.wavPcm(11025, 1, 8, 1.0))
        for ((i, b) in cases.withIndex()) {
            val src = File(dir, "s$i.wav").also { it.writeBytes(b) }
            val out = File(dir, "o$i.wav")
            FileSource(src).use { WavConvert.toProfile(it, out) }
            assertEquals(Plan.NONE, analyze(out).plan, "caso $i")
        }
        val low = File(dir, "low.wav").also { it.writeBytes(MediaFixtures.ulawWav(8000, 1.0)) }
        val o = File(dir, "low-o.wav"); FileSource(low).use { WavConvert.toProfile(it, o) }
        assertEquals(8000, analyze(o).facts.sampleRate)
    }

    @Test fun `un WAV ilegible no se convierte y deja el motivo`() {
        val ms = File(dir, "ms.wav").also { it.writeBytes(MediaFixtures.wavFile(2, 1, 22050, 4, 512, ByteArray(2048), null)) }
        val ex = assertFailsWith<MediaException> { FileSource(ms).use { WavConvert.toProfile(it, File(dir, "x.wav")) } }
        assertTrue(ex.message!!.contains("formato 2"), ex.message)
    }

    @Test fun `la conversion se puede cancelar y no deja un archivo a medias`() {
        val src = File(dir, "big.wav").also { it.writeBytes(MediaFixtures.wavPcm(44100, 2, 16, 20.0)) }
        val out = File(dir, "c.wav")
        assertFailsWith<MediaException> { FileSource(src).use { WavConvert.toProfile(it, out, cancelled = { true }) } }
        assertTrue(!out.exists(), "el resultado a medias se borra")
    }

    // ------------------------------------------------------------------ remuestreo
    private fun zeroCrossings(p: ShortArray): Int { var c = 0; for (i in 1 until p.size) if (p[i - 1] < 0 && p[i] >= 0) c++; return c }

    @Test fun `bajar de 44,1 a 22,05 kHz conserva la frecuencia y la mitad de las muestras`() {
        val pcm = ShortArray(44100) { (Math.sin(2 * Math.PI * 1000 * it / 44100) * 10000).toInt().toShort() }
        val out = ArrayList<Short>()
        MonoResampler(44100, 22050) { p, n -> for (i in 0 until n) out.add(p[i]) }.also { it.push(pcm, pcm.size); it.finish() }
        assertTrue(Math.abs(out.size - 22050) <= 2, "${out.size}")
        assertTrue(Math.abs(zeroCrossings(out.toShortArray()) - 1000) <= 3, "cruces ${zeroCrossings(out.toShortArray())}")
    }

    @Test fun `subir de 8 a 22,05 kHz conserva la frecuencia, y misma frecuencia no toca nada`() {
        val pcm = ShortArray(8000) { (Math.sin(2 * Math.PI * 200 * it / 8000) * 10000).toInt().toShort() }
        val up = ArrayList<Short>()
        MonoResampler(8000, 22050) { p, n -> for (i in 0 until n) up.add(p[i]) }.also { it.push(pcm, pcm.size); it.finish() }
        assertTrue(Math.abs(up.size - 22050) <= 3, "${up.size}")
        assertTrue(Math.abs(zeroCrossings(up.toShortArray()) - 200) <= 3)
        val same = ArrayList<Short>()
        MonoResampler(22050, 22050) { p, n -> for (i in 0 until n) same.add(p[i]) }.also { it.push(pcm, pcm.size) }
        assertEquals(pcm.toList(), same)
    }

    // ------------------------------------------------------------------ tablas MP4
    @Test fun `las posiciones de las muestras de un MOV salen de stsc stco stsz`() {
        val fr = List(7) { ByteArray(100 + it * 10) { b -> (it + b).toByte() } }
        val mov = MediaFixtures.mov("jpeg", 16, 16, fr)
        val info = FileSource(File(dir, "t.mov").also { it.writeBytes(mov) }).use { Mp4.parse(it, tables = true)!! }
        val locs = Mp4.sampleLocations(info.video!!)!!
        assertEquals(7, locs.size)
        for (i in fr.indices) assertContentEquals(fr[i], mov.copyOfRange(locs[i][0].toInt(), (locs[i][0] + locs[i][1]).toInt()), "muestra $i")
    }
}
