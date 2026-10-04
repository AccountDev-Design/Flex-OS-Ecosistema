@file:JvmName("MediaFixturesCliKt")

package com.flexos.flexphone.cloud.media

import com.flexos.flexphone.cloud.Json
import java.io.File

/**
 * Genera archivos multimedia (los que ESCRIBE este codigo y casos hostiles) en una carpeta, y para cada uno
 * imprime una linea JSON con el veredicto del analizador. `tests/host/media_profile_e2e.sh` pasa despues
 * esos archivos por el decodificador y el demux REALES del firmware (`mediacheck`) y compara.
 *
 *   MediaFixturesCli <carpeta>
 *
 * "hard": el fallo es de decodificacion (el firmware TIENE que rechazarlo); si es false, el veredicto es de politica
 * (tamano, ritmo...) y el firmware podria abrirlo igualmente.
 */
fun main(args: Array<String>) {
    val dir = File(args[0]).also { it.mkdirs() }
    val out = ArrayList<Map<String, Any?>>()

    fun add(name: String, bytes: ByteArray, hard: Boolean = false, expect: String? = null) {
        val f = File(dir, name); f.writeBytes(bytes)
        val a = FileSource(f).use { MediaAnalyzer.analyze(it) }
        out.add(linkedMapOf("file" to name, "container" to a.facts.container, "plan" to a.plan.name, "hard" to hard, "expect" to expect, "reasons" to a.reasons.joinToString(" | ")))
    }
    fun addFile(f: File, hard: Boolean = false, expect: String? = null) {
        val a = FileSource(f).use { MediaAnalyzer.analyze(it) }
        out.add(linkedMapOf("file" to f.name, "container" to a.facts.container, "plan" to a.plan.name, "hard" to hard, "expect" to expect, "reasons" to a.reasons.joinToString(" | ")))
    }

    // ---- lo que escribe este codigo: el firmware tiene que aceptarlo
    for ((w, h, fps, n) in listOf(listOf(160, 120, 10, 6), listOf(320, 240, 12, 12), listOf(640, 480, 12, 8), listOf(480, 272, 15, 5), listOf(176, 144, 5, 4)))
        addFile(MediaFixtures.aviMjpeg(dir, "w_${w}x${h}_${fps}fps.avi", w, h, n, fps), expect = "NONE")
    // fotogramas de tamano impar, dimensiones no multiplo de 8, un solo fotograma
    addFile(MediaFixtures.aviMjpeg(dir, "w_odd_dims.avi", 203, 117, 5, 8), expect = "NONE")
    addFile(MediaFixtures.aviMjpeg(dir, "w_one_frame.avi", 160, 120, 1, 12), expect = "NONE")
    for (rate in intArrayOf(8000, 11025, 16000, 22050, 44100)) addFile(MediaFixtures.wavIma(dir, "w_ima_$rate.wav", rate, 1.5), expect = "NONE")

    // ---- conversiones reales de Kotlin puro
    run {
        val src = File(dir, "src_24bit_stereo.wavsrc").also { it.writeBytes(MediaFixtures.wavPcm(44100, 2, 24, 1.5)) }
        val o = File(dir, "conv_24bit_stereo_44k.wav"); FileSource(src).use { WavConvert.toProfile(it, o) }; src.delete(); addFile(o, expect = "NONE")
        val src2 = File(dir, "src_f32.wavsrc").also { it.writeBytes(MediaFixtures.wavPcm(48000, 1, 32, 1.5, tag = 3)) }
        val o2 = File(dir, "conv_float_48k.wav"); FileSource(src2).use { WavConvert.toProfile(it, o2) }; src2.delete(); addFile(o2, expect = "NONE")
        val mov = File(dir, "src_mjpeg.movsrc").also { it.writeBytes(MediaFixtures.mov("jpeg", 160, 120, MediaFixtures.mjpegFrames(160, 120, 8), fps = 12)) }
        val o3 = File(dir, "remux_mov_mjpeg.avi"); FileSource(mov).use { Remux.toAvi(it, o3) }; mov.delete(); addFile(o3, expect = "NONE")
        val pcm = ByteArray(22050 * 2) { (Math.sin(2 * Math.PI * 300 * (it / 2) / 22050) * 9000).toInt().let { v -> if (it % 2 == 0) v.toByte() else (v shr 8).toByte() } }
        val m2 = File(dir, "src_pcm.movsrc").also { it.writeBytes(MediaFixtures.mov("jpeg", 0, 0, emptyList(), audioPcm = pcm, audioRate = 22050, audioCh = 1)) }
        val o4 = File(dir, "remux_mov_pcm.wav"); FileSource(m2).use { Remux.pcmToWav(it, o4) }; m2.delete(); addFile(o4, expect = "NONE")
    }

    // ---- fotos
    add("p_baseline_640.jpg", MediaFixtures.jpeg(640, 480), expect = "NONE")
    add("p_gray.jpg", MediaFixtures.jpeg(300, 200, gray = true), expect = "NONE")
    add("p_progressive.jpg", MediaFixtures.jpeg(320, 240, progressive = true), hard = true)
    add("p_exif6.jpg", MediaFixtures.jpeg(320, 240, orientation = 6))
    add("p_truncated.jpg", MediaFixtures.jpeg(320, 240).let { it.copyOf(it.size / 2) }, hard = true)
    add("p_png.png", MediaFixtures.png(100, 80), hard = true)

    // ---- hostiles
    val h264 = MediaFixtures.aviMjpeg(dir, "h_h264.avi", 160, 120, 4, 12); MediaFixtures.patchAviCodec(h264, "H264", "H264"); addFile(h264, hard = true)
    add("h_wav_ms_adpcm.wav", MediaFixtures.wavFile(2, 1, 22050, 4, 512, ByteArray(2048), null), hard = true)
    add("h_wav_24bit.wav", MediaFixtures.wavPcm(44100, 2, 24, 0.2), hard = true)
    add("h_wav_cut.wav", MediaFixtures.wavPcm(22050, 1, 16, 1.0).let { it.copyOf(it.size / 2) })
    add("h_empty.avi", "RIFF\u0000\u0000\u0000\u0000AVI ".toByteArray(Charsets.ISO_8859_1) + ByteArray(64), hard = true)

    println(Json.write(linkedMapOf("items" to out)))
}
