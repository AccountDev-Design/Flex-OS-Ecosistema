@file:JvmName("E2eMediaFixturesCliKt")

package com.flexos.flexphone.cloud

import com.flexos.flexphone.cloud.media.MediaFixtures
import java.io.File

/**
 * Los archivos multimedia con los que `tests/host/phone_e2e.cpp` prueba que el P4 reproduce lo que el telefono prepara.
 *
 *   E2eMediaFixturesCli <carpeta>
 */
fun main(args: Array<String>) {
    val dir = File(args[0]).also { it.mkdirs() }
    fun w(name: String, b: ByteArray) = File(dir, name).writeBytes(b)
    w("m_h264.mp4", MediaFixtures.mov("avc1", 1280, 720, List(24) { ByteArray(4000) { b -> b.toByte() } }, fps = 24, brand = "isom"))
    w("m_png.png", MediaFixtures.png(400, 300))
    w("m_prog.jpg", MediaFixtures.jpeg(1200, 900, progressive = true))
    w("m_wav24.wav", MediaFixtures.wavPcm(48000, 2, 24, 1.0))
    w("m_aac.m4a", MediaFixtures.m4a("mp4a", 44100, 2, 20))
    val tmp = createTempDir("e2e-avi")
    w("m_native.avi", MediaFixtures.aviMjpeg(tmp, "n.avi", 320, 240, 24, 12).readBytes())
    w("m_ima.wav", MediaFixtures.wavIma(tmp, "i.wav", 22050, 3.0).readBytes())
    w("m_big.jpg", MediaFixtures.jpeg(3200, 2400, quality = 0.9f))
    w("m_cut.jpg", MediaFixtures.jpeg(320, 240).let { it.copyOf(it.size / 2) })
    w("m_h264.avi", MediaFixtures.aviMjpeg(tmp, "h.avi", 160, 120, 4, 12).also { MediaFixtures.patchAviCodec(it, "H264", "H264") }.readBytes())
    tmp.deleteRecursively()
}
