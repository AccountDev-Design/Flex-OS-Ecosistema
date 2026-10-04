package com.flexos.flexphone.cloud

import java.io.File
import java.io.FileOutputStream
import java.io.IOException
import java.io.InputStream
import java.io.RandomAccessFile
import java.security.MessageDigest

/**
 * Los BYTES de Flex Cloud en el telefono: un almacen de objetos en una carpeta.
 *
 * Mismo modelo que `FsObjectStore` del servicio de Node: los archivos se
 * guardan con claves aleatorias (nunca con el nombre del usuario, que es solo
 * metadato) y un archivo solo se publica cuando su SHA-256 ya cuadra. Un corte a
 * mitad nunca deja un objeto a medio escribir con apariencia de bueno.
 *
 * UNA DIFERENCIA A PROPOSITO, porque esto es un telefono: las partes de una
 * subida se escriben EN SU SITIO dentro de un unico archivo de datos del tamano
 * final (`uploads/<id>/data.bin`), no en ficheros sueltos que luego se unen.
 * Unir partes obliga a tener el archivo DOS veces en el disco al terminar; con
 * un video de 4 GB eso son 8 GB libres que el movil puede no tener. Asi el pico
 * es el tamano del archivo y terminar es un renombrado.
 *  · Una parte confirmada deja una marca `<n>-<sha256>.ok` y su zona del
 *    archivo ya no se vuelve a escribir nunca (repetirla con la misma huella no
 *    reescribe; con otra es `part_conflict`).
 *  · Una parte que llega mal (corte, huella distinta, mas larga de lo debido)
 *    deja su zona sin marca: el reintento la sobrescribe.
 *  · Nunca se escribe fuera de la zona de la parte: lo que sobra se detecta
 *    leyendo un byte de mas que no se guarda.
 *
 * La carpeta es la que la app le da (en Android, el almacenamiento PRIVADO de
 * la app: `filesDir/FlexCloud`, que no necesita ningun permiso y que ni otras
 * apps ni el P4 pueden recorrer). Todas las rutas se componen con
 * claves e ids validados, nunca con texto ajeno.
 *
 * [freeSpace]: espacio REAL que le queda a la app (Android:
 * `StorageManager.getAllocatableBytes`). [allocate]: reserva de verdad el sitio
 * de un archivo antes de escribirlo (Android: `StorageManager.allocateBytes`,
 * que hace `fallocate`); si la reserva es real, [reservesOnCreate] = true y la
 * cuota no vuelve a descontar lo pendiente de cada subida.
 */
class ObjectStore(
    val root: File,
    private val freeSpace: () -> Long = { root.usableSpace },
    private val allocate: ((RandomAccessFile, Long) -> Unit)? = null,
    val reservesOnCreate: Boolean = false,
) {
    private val objects = File(root, "objects")
    private val uploads = File(root, "uploads")
    private val tmp = File(root, "tmp")

    init {
        for (d in listOf(root, objects, uploads, tmp)) if (!d.isDirectory && !d.mkdirs()) throw IOException("no se pudo crear ${d.name}")
    }

    fun freeBytes(): Long = try { freeSpace().coerceAtLeast(0) } catch (e: Exception) { 0 }

    private val keyRe = Regex("^[a-z2-7]{24}$")
    private val markRe = Regex("^(\\d{1,6})-([a-f0-9]{64})\\.ok$")

    fun newStorageKey(): String = Names.base32(Names.randomBytes(15))

    private fun objFile(key: String): File {
        require(keyRe.matches(key)) { "clave de objeto no valida" }
        return File(File(objects, key.substring(0, 2)), key)
    }

    private fun uploadDir(uploadId: String): File {
        require(Names.ID_UPLOAD.matches(uploadId)) { "id de subida no valido" }
        return File(uploads, uploadId)
    }

    private fun dataFile(uploadId: String) = File(uploadDir(uploadId), "data.bin")

    private fun newTmp(prefix: String): File = File(tmp, "$prefix-${Names.base32(Names.randomBytes(10))}.tmp")

    fun exists(key: String): Boolean = objFile(key).isFile
    fun size(key: String): Long = objFile(key).length()

    // ------------------------------------------------------------ subidas
    /** Crea el archivo de datos de una subida con su tamano final (y lo reserva si se puede). */
    fun createUploadData(uploadId: String, size: Long) {
        val dir = uploadDir(uploadId)
        if (!dir.isDirectory && !dir.mkdirs()) throw IOException("no se pudo crear la carpeta de la subida")
        RandomAccessFile(dataFile(uploadId), "rw").use { raf ->
            allocate?.invoke(raf, size)
            raf.setLength(size)
        }
    }

    fun hasUploadData(uploadId: String): Boolean = dataFile(uploadId).isFile

    class Received(val size: Long, val sha256: String, val tooLong: Boolean)

    /**
     * Escribe la parte que llega por [src] en [offset] del archivo de datos,
     * como mucho [expected] bytes, mientras calcula su SHA-256. Si el cuerpo
     * trae mas, se lee UN byte de mas (que no se escribe) para decirlo.
     */
    fun receivePart(uploadId: String, offset: Long, src: InputStream, expected: Long): Received {
        val md = MessageDigest.getInstance("SHA-256")
        var size = 0L
        var tooLong = false
        RandomAccessFile(dataFile(uploadId), "rw").use { raf ->
            raf.seek(offset)
            val buf = ByteArray(64 * 1024)
            while (size < expected) {
                val want = minOf(buf.size.toLong(), expected - size).toInt()
                val r = src.read(buf, 0, want)
                if (r < 0) break
                raf.write(buf, 0, r)
                md.update(buf, 0, r)
                size += r
            }
            if (size == expected && src.read() >= 0) tooLong = true
            raf.fd.sync()
        }
        return Received(size, ObjectStore.hex(md.digest()), tooLong)
    }

    /** Confirma una parte: deja su marca (creacion atomica). */
    fun commitPart(uploadId: String, n: Int, sha256: String) {
        val f = File(uploadDir(uploadId), "$n-$sha256.ok")
        if (!f.exists() && !f.createNewFile()) throw IOException("no se pudo confirmar la parte $n")
    }

    /** Partes confirmadas de una subida (sus marcas en el disco): n -> SHA-256. */
    fun partsOf(uploadId: String): MutableMap<Int, String> {
        val out = java.util.TreeMap<Int, String>()
        val files = uploadDir(uploadId).listFiles() ?: return out
        for (f in files) {
            if (f.name == "data.bin") continue
            val m = markRe.matchEntire(f.name)
            if (m == null) { f.delete(); continue }     // restos de otra cosa: fuera
            val n = m.groupValues[1].toInt()
            if (out.containsKey(n)) { f.delete(); continue }
            out[n] = m.groupValues[2]
        }
        return out
    }

    class Hashed(val size: Long, val sha256: String)

    /** SHA-256 del archivo de datos entero (1 MB por lectura). */
    fun hashUploadData(uploadId: String): Hashed {
        val md = MessageDigest.getInstance("SHA-256")
        var size = 0L
        dataFile(uploadId).inputStream().use { inp ->
            val buf = ByteArray(1 shl 20)
            while (true) {
                val r = inp.read(buf)
                if (r < 0) break
                md.update(buf, 0, r)
                size += r
            }
        }
        return Hashed(size, hex(md.digest()))
    }

    /** Publica el archivo de una subida terminada: un renombrado, sin copiar nada. */
    fun publishUpload(uploadId: String, key: String) {
        val dst = objFile(key)
        dst.parentFile?.let { if (!it.isDirectory && !it.mkdirs()) throw IOException("no se pudo crear la carpeta del objeto") }
        if (!dataFile(uploadId).renameTo(dst)) throw IOException("no se pudo publicar el objeto")
    }

    fun removeUpload(uploadId: String) { uploadDir(uploadId).deleteRecursively() }

    fun listUploadDirs(): List<String> = (uploads.list() ?: emptyArray()).filter { Names.ID_UPLOAD.matches(it) }

    // ------------------------------------------------------------ objetos sueltos
    fun putSmall(key: String, bytes: ByteArray) {
        val t = newTmp("s")
        FileOutputStream(t).use { it.write(bytes); it.fd.sync() }
        val dst = objFile(key)
        dst.parentFile?.let { if (!it.isDirectory && !it.mkdirs()) throw IOException("no se pudo crear la carpeta del objeto") }
        if (!t.renameTo(dst)) { t.delete(); throw IOException("no se pudo guardar el objeto") }
    }

    /**
     * Importa un flujo como objeto nuevo (Compartir -> Flex Cloud en el propio
     * telefono): temporal + SHA-256 + renombrado. Como mucho [max] bytes.
     */
    fun importStream(key: String, src: InputStream, max: Long): Hashed {
        val t = newTmp("i")
        val md = MessageDigest.getInstance("SHA-256")
        var size = 0L
        try {
            FileOutputStream(t).use { out ->
                val buf = ByteArray(256 * 1024)
                while (true) {
                    val r = src.read(buf)
                    if (r < 0) break
                    size += r
                    if (size > max) throw IOException("demasiado grande")
                    out.write(buf, 0, r)
                    md.update(buf, 0, r)
                }
                out.fd.sync()
            }
            val dst = objFile(key)
            dst.parentFile?.let { if (!it.isDirectory && !it.mkdirs()) throw IOException("no se pudo crear la carpeta del objeto") }
            if (!t.renameTo(dst)) throw IOException("no se pudo guardar el objeto")
        } catch (e: IOException) {
            t.delete()
            throw e
        }
        return Hashed(size, hex(md.digest()))
    }

    /** Flujo de [start, end] (ambos incluidos) de un objeto, sin cargarlo entero. */
    fun read(key: String, start: Long, end: Long): InputStream {
        val raf = RandomAccessFile(objFile(key), "r")
        raf.seek(start)
        return RangeStream(raf, end - start + 1)
    }

    /** Un archivo temporal en la carpeta de temporales del almacen (mismo disco que los objetos: adoptarlo es un renombrado). */
    fun newTempFile(prefix: String): File = newTmp(prefix)

    /** El archivo de un objeto, solo para LEERLO (el conversor lo abre por su ruta). */
    fun fileOf(key: String): File = objFile(key)

    /** Publica [f] (un temporal de este almacen) como objeto [key]: SHA-256 y renombrado atomico. */
    fun adoptFile(key: String, f: File): Hashed {
        val md = MessageDigest.getInstance("SHA-256")
        var size = 0L
        f.inputStream().use { inp ->
            val buf = ByteArray(1 shl 20)
            while (true) { val r = inp.read(buf); if (r < 0) break; md.update(buf, 0, r); size += r }
        }
        val dst = objFile(key)
        dst.parentFile?.let { if (!it.isDirectory && !it.mkdirs()) throw IOException("no se pudo crear la carpeta del objeto") }
        if (!f.renameTo(dst)) throw IOException("no se pudo guardar el objeto")
        return Hashed(size, hex(md.digest()))
    }

    fun readSmall(key: String, max: Int): ByteArray {
        val f = objFile(key)
        if (f.length() > max) throw IOException("objeto demasiado grande")
        return f.readBytes()
    }

    fun remove(key: String) {
        val f = objFile(key)
        f.delete()
        f.parentFile?.let { p -> if (p.list()?.isEmpty() == true) p.delete() }
    }

    /** Al arrancar: los temporales son de operaciones que no terminaron. */
    fun sweepTemp() {
        tmp.listFiles()?.forEach { it.delete() }
        uploads.listFiles()?.forEach { if (!Names.ID_UPLOAD.matches(it.name)) it.deleteRecursively() }
    }

    /** Objetos que ningun registro reclama (un borrado a medias): se recogen. */
    fun sweepOrphans(keep: Set<String>) {
        objects.listFiles()?.forEach { shard ->
            shard.listFiles()?.forEach { f -> if (keyRe.matches(f.name) && f.name !in keep) f.delete() }
            if (shard.list()?.isEmpty() == true) shard.delete()
        }
    }

    private class RangeStream(private val raf: RandomAccessFile, private var left: Long) : InputStream() {
        override fun read(): Int {
            if (left <= 0) return -1
            val r = raf.read()
            if (r >= 0) left-- else left = 0
            return r
        }
        override fun read(b: ByteArray, off: Int, len: Int): Int {
            if (left <= 0) return -1
            val r = raf.read(b, off, minOf(len.toLong(), left).toInt())
            if (r > 0) left -= r else left = 0
            return r
        }
        override fun close() = raf.close()
    }

    companion object {
        private val HEX = "0123456789abcdef".toCharArray()
        fun hex(b: ByteArray): String {
            val out = CharArray(b.size * 2)
            for (i in b.indices) {
                out[2 * i] = HEX[(b[i].toInt() shr 4) and 15]
                out[2 * i + 1] = HEX[b[i].toInt() and 15]
            }
            return String(out)
        }
    }
}
