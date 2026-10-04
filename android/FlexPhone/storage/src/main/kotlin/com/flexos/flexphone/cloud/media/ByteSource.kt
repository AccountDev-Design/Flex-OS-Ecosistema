package com.flexos.flexphone.cloud.media

import java.io.Closeable
import java.io.File
import java.io.RandomAccessFile

/**
 * Lectura de un archivo por posicion, sin cargarlo entero: el analizador salta entre las
 * cabeceras (la `moov` de un MP4 puede estar al final de un archivo de 2 GB).
 */
interface ByteSource {
    val size: Long
    /** Lee hasta [len] bytes desde [pos]; devuelve cuantos leyo (0 si [pos] >= size). */
    fun readAt(pos: Long, dst: ByteArray, off: Int, len: Int): Int
}

class BytesSource(private val b: ByteArray) : ByteSource {
    override val size: Long get() = b.size.toLong()
    override fun readAt(pos: Long, dst: ByteArray, off: Int, len: Int): Int {
        if (pos < 0 || pos >= b.size || len <= 0) return 0
        val n = minOf(len.toLong(), b.size - pos).toInt()
        System.arraycopy(b, pos.toInt(), dst, off, n)
        return n
    }
}

class FileSource(file: File) : ByteSource, Closeable {
    private val raf = RandomAccessFile(file, "r")
    override val size: Long = raf.length()
    override fun readAt(pos: Long, dst: ByteArray, off: Int, len: Int): Int {
        if (pos < 0 || pos >= size || len <= 0) return 0
        raf.seek(pos)
        var total = 0
        while (total < len) {
            val r = raf.read(dst, off + total, len - total)
            if (r <= 0) break
            total += r
        }
        return total
    }
    override fun close() = raf.close()
}

/** Exactamente [len] bytes desde [pos], o null si no hay tantos (o es absurdo pedirlos). */
fun ByteSource.bytes(pos: Long, len: Int, maxLen: Int = 64 shl 20): ByteArray? {
    if (pos < 0 || len < 0 || len > maxLen || pos + len > size) return null
    val out = ByteArray(len)
    var got = 0
    while (got < len) {
        val r = readAt(pos + got, out, got, len - got)
        if (r <= 0) return null
        got += r
    }
    return out
}

/** Lo que cabe leer de [pos]: como mucho [len], menos si el archivo se acaba antes. */
fun ByteSource.head(pos: Long, len: Int): ByteArray {
    val n = minOf(len.toLong(), maxOf(0L, size - pos)).toInt()
    return bytes(pos, n, Int.MAX_VALUE) ?: ByteArray(0)
}

// ---- enteros de cabecera (con signo cuidado: nada de Int negativo que parezca un tamano)
fun ByteArray.u8(i: Int): Int = this[i].toInt() and 0xFF
fun ByteArray.u16le(i: Int): Int = u8(i) or (u8(i + 1) shl 8)
fun ByteArray.u32le(i: Int): Long = (u8(i).toLong()) or (u8(i + 1).toLong() shl 8) or (u8(i + 2).toLong() shl 16) or (u8(i + 3).toLong() shl 24)
fun ByteArray.u16be(i: Int): Int = (u8(i) shl 8) or u8(i + 1)
fun ByteArray.u32be(i: Int): Long = (u8(i).toLong() shl 24) or (u8(i + 1).toLong() shl 16) or (u8(i + 2).toLong() shl 8) or u8(i + 3).toLong()
fun ByteArray.u64be(i: Int): Long = (u32be(i) shl 32) or u32be(i + 4)
fun ByteArray.tag(i: Int): String = String(this, i, 4, Charsets.ISO_8859_1)
fun ByteArray.hasAt(i: Int, s: String): Boolean {
    if (i < 0 || i + s.length > size) return false
    for (k in s.indices) if (this[i + k].toInt() and 0xFF != s[k].code) return false
    return true
}
