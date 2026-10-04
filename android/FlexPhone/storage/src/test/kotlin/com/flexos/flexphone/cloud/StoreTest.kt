package com.flexos.flexphone.cloud

import java.io.ByteArrayInputStream
import java.io.File
import java.nio.file.Files
import java.security.MessageDigest
import kotlin.test.AfterTest
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertFalse
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import kotlin.test.assertTrue

/**
 * La logica de Flex Cloud del telefono (CloudStore) sin red: carpetas, listas,
 * papelera, cuota LOGICA de 5 GB recortada por el espacio REAL, subidas por partes
 * con huella, reanudacion tras un reinicio, enlaces firmados y miniaturas.
 */
class StoreTest {
    private val tmp: File = Files.createTempDirectory("flexcloud-store").toFile()
    private var t = 1_700_000_000_000L
    private var free = 50L shl 30                  // espacio "real" del telefono

    @AfterTest fun cleanup() { tmp.deleteRecursively() }

    private fun open(cfg: CloudConfig = CloudConfig()): CloudStore {
        val os = ObjectStore(File(tmp, "data"), { free })
        return CloudStore(os, File(tmp, "meta"), cfg, { t }).also { it.recover() }
    }

    private fun sha(b: ByteArray) = ObjectStore.hex(MessageDigest.getInstance("SHA-256").digest(b))
    private fun bytes(n: Int, seed: Int = 1) = ByteArray(n) { ((it * 31 + seed * 7) xor (it shr 3)).toByte() }

    /** Sube un archivo entero por partes como lo haria el P4. */
    private fun upload(c: CloudStore, name: String, data: ByteArray, parent: String? = null, chunk: Long = 64L * 1024): Map<String, Any?> {
        val body = mutableMapOf<String, Any?>("name" to name, "size" to data.size.toLong(), "sha256" to sha(data), "chunkSize" to chunk)
        if (parent != null) body["parentId"] = parent
        val u = c.createUpload(body)
        val id = u["uploadId"] as String
        val cs = (u["chunkSize"] as Long).toInt()
        val parts = (u["totalParts"] as Int)
        for (n in 1..parts) {
            val part = data.copyOfRange((n - 1) * cs, minOf(data.size, n * cs))
            val plan = c.planPart(id, n, sha(part), part.size.toLong())
            assertNull(plan.already)
            c.receivePart(id, n, sha(part), ByteArrayInputStream(part), plan)
        }
        @Suppress("UNCHECKED_CAST")
        return (c.completeUpload(id, emptyMap())["file"] as Map<String, Any?>)
    }

    @Test fun `rev cambia cuando cambia la lista y solo entonces`() {
        val c = open()
        fun rev() = c.quota()["rev"] as Long
        val r0 = rev()
        // leer no la mueve
        c.list(mapOf()); c.quota(); c.fileCount()
        assertEquals(r0, rev(), "listar y pedir la cuota no cambian nada")
        // terminar una subida SI
        val f = upload(c, "Clip.avi", bytes(100_000))
        val r1 = rev()
        assertTrue(r1 > r0, "una subida terminada cambia la lista")
        // el estado de preparacion (lo que el P4 vigila) tambien
        c.setPlayable(f["id"] as String, "preparing", "mjpeg")
        val r2 = rev()
        assertTrue(r2 > r1, "pasar a 'preparando' cambia lo que se ve")
        // renombrar, papelera: tambien
        c.updateFile(f["id"] as String, mapOf("name" to "Otro.avi"))
        val r3 = rev()
        assertTrue(r3 > r2, "renombrar cambia la lista")
        c.trash("file", f["id"] as String)
        assertTrue(rev() > r3, "mandar a la papelera cambia la lista")
        // el progreso vive solo en memoria: no mueve rev (el P4 lo ve por la vigilancia de ese archivo)
        val g = upload(c, "Otro.avi", bytes(50_000, 2))
        c.setPlayable(g["id"] as String, "preparing", "mjpeg")
        val r4 = rev()
        c.setPlayableProgress(g["id"] as String, 40)
        assertEquals(r4, rev(), "el avance de la preparacion no cambia rev")
        // la forma que lee el P4: dentro de "quota"
        assertNotNull(c.quota()["rev"])
    }

    @Test fun `carpetas, conflictos de nombre y migas de pan`() {
        val c = open()
        val a = c.createFolder(mapOf("name" to "Viajes"))
        assertEquals("folder", a["type"])
        assertFailsWith<CloudError> { c.createFolder(mapOf("name" to "viajes")) }.also { assertEquals("name_conflict", it.code) }
        val a2 = c.createFolder(mapOf("name" to "VIAJES", "conflict" to "rename"))
        assertEquals("VIAJES (1)", a2["name"])
        val b = c.createFolder(mapOf("name" to "2026", "parentId" to a["id"]))
        val info = c.getFolder(b["id"] as String)
        @Suppress("UNCHECKED_CAST")
        assertEquals(listOf("Viajes", "2026"), (info["path"] as List<Map<String, Any?>>).map { it["name"] })
        // Una carpeta no puede ir dentro de si misma ni de una hija.
        assertFailsWith<CloudError> { c.updateFolder(a["id"] as String, mapOf("parentId" to b["id"])) }.also { assertEquals("folder_cycle", it.code) }
        assertEquals("Viaje", c.updateFolder(a["id"] as String, mapOf("name" to "Viaje"))["name"])
        assertFailsWith<CloudError> { c.getFolder("fld_" + "a".repeat(24)) }.also { assertEquals("not_found", it.code) }
    }

    @Test fun `subida por partes, lista, descarga y huella identica`() {
        val c = open()
        val data = bytes(300_000)
        val f = upload(c, "Vídeo ☁.avi", data)
        assertEquals("Vídeo ☁.avi", f["name"])
        assertEquals(sha(data), f["sha256"])
        assertEquals("video", f["kind"])
        assertEquals(data.size.toLong(), f["size"])
        @Suppress("UNCHECKED_CAST")
        val items = c.list(mapOf())["items"] as List<Map<String, Any?>>
        assertEquals(listOf(f["id"]), items.map { it["id"] })
        val d = c.fileForDownload(f["id"] as String)
        val got = c.openRead(d.storageKey, 0, d.size - 1).use { it.readBytes() }
        assertTrue(got.contentEquals(data), "los bytes guardados son los subidos")
        val mid = c.openRead(d.storageKey, 1000, 1999).use { it.readBytes() }
        assertTrue(mid.contentEquals(data.copyOfRange(1000, 2000)), "lectura por rango")
        val q = c.quota()
        assertEquals(data.size.toLong(), q["usedBytes"]); assertEquals(0L, q["reservedBytes"]); assertEquals("ok", q["state"])
    }

    @Test fun `partes repetidas, alteradas, de otro tamano o fuera de rango`() {
        val c = open()
        val data = bytes(200_000, 3)
        val u = c.createUpload(mapOf("name" to "a.bin", "size" to data.size.toLong(), "sha256" to sha(data), "chunkSize" to 65536L))
        val id = u["uploadId"] as String
        val p1 = data.copyOfRange(0, 65536)
        assertFailsWith<CloudError> { c.planPart(id, 0, sha(p1), 65536) }.also { assertEquals("part_out_of_range", it.code) }
        assertFailsWith<CloudError> { c.planPart(id, 5, sha(p1), 65536) }.also { assertEquals("part_out_of_range", it.code) }
        assertFailsWith<CloudError> { c.planPart(id, 1, null, 65536) }.also { assertEquals("invalid_request", it.code) }
        assertFailsWith<CloudError> { c.planPart(id, 1, sha(p1), 1000) }.also { assertEquals("part_size_mismatch", it.code) }
        // Huella declarada que no cuadra con los bytes: 422 y la parte no cuenta.
        val bad = c.planPart(id, 1, sha(bytes(65536, 9)), 65536)
        assertFailsWith<CloudError> { c.receivePart(id, 1, sha(bytes(65536, 9)), ByteArrayInputStream(p1), bad) }.also { assertEquals("checksum_mismatch", it.code) }
        // Cuerpo mas largo que la parte: no se escribe fuera de su zona y no cuenta.
        val longPlan = c.planPart(id, 1, sha(p1), -1)
        assertFailsWith<CloudError> { c.receivePart(id, 1, sha(p1), ByteArrayInputStream(p1 + byteArrayOf(1, 2)), longPlan) }.also { assertEquals("part_size_mismatch", it.code) }
        // Cuerpo cortado.
        val shortPlan = c.planPart(id, 1, sha(p1), -1)
        assertFailsWith<CloudError> { c.receivePart(id, 1, sha(p1), ByteArrayInputStream(p1.copyOf(100)), shortPlan) }.also { assertEquals("part_size_mismatch", it.code) }
        // La buena.
        val ok = c.planPart(id, 1, sha(p1), 65536)
        val r = c.receivePart(id, 1, sha(p1), ByteArrayInputStream(p1), ok)
        assertEquals(false, r["alreadyReceived"]); assertEquals(1, r["receivedCount"])
        // Repetirla con la misma huella: no se lee nada.
        val again = c.planPart(id, 1, sha(p1), 65536)
        assertNotNull(again.already); assertEquals(true, again.already!!["alreadyReceived"])
        // Con otra huella: conflicto.
        assertFailsWith<CloudError> { c.planPart(id, 1, sha(bytes(65536, 4)), 65536) }.also { assertEquals("part_conflict", it.code) }
        // Dos conexiones a la vez con la misma parte: la segunda espera turno (503).
        val p2 = data.copyOfRange(65536, 131072)
        val first = c.planPart(id, 2, sha(p2), 65536)
        assertFailsWith<CloudError> { c.planPart(id, 2, sha(p2), 65536) }.also { assertEquals(503, it.status) }
        c.receivePart(id, 2, sha(p2), ByteArrayInputStream(p2), first)
        // Completar con partes que faltan.
        assertFailsWith<CloudError> { c.completeUpload(id, emptyMap()) }.also {
            assertEquals("incomplete_upload", it.code); assertEquals(listOf(3, 4), it.details!!["missing"])
        }
    }

    @Test fun `el archivo cambio entre partes - la subida se descarta y libera la reserva`() {
        val c = open()
        val data = bytes(100_000, 5)
        val u = c.createUpload(mapOf("name" to "x.bin", "size" to data.size.toLong(), "sha256" to sha(bytes(100_000, 6)), "chunkSize" to 65536L))
        val id = u["uploadId"] as String
        for (n in 1..2) {
            val part = data.copyOfRange((n - 1) * 65536, minOf(data.size, n * 65536))
            c.receivePart(id, n, sha(part), ByteArrayInputStream(part), c.planPart(id, n, sha(part), part.size.toLong()))
        }
        assertEquals(data.size.toLong(), c.quota()["reservedBytes"])
        assertFailsWith<CloudError> { c.completeUpload(id, emptyMap()) }.also { assertEquals("checksum_mismatch", it.code) }
        assertEquals(0L, c.quota()["reservedBytes"])
        assertEquals("failed", c.uploadStatus(id)["state"])
    }

    @Test fun `reanudar con la misma clave del cliente y tras un reinicio`() {
        var c = open()
        val data = bytes(250_000, 7)
        val body = mapOf("name" to "largo.avi", "size" to data.size.toLong(), "sha256" to sha(data), "chunkSize" to 65536L, "clientKey" to "p4:${sha(data)}:${data.size}")
        val u = c.createUpload(body)
        val id = u["uploadId"] as String
        val p1 = data.copyOfRange(0, 65536)
        c.receivePart(id, 1, sha(p1), ByteArrayInputStream(p1), c.planPart(id, 1, sha(p1), 65536))
        // "Apagon": se abre otra instancia sobre la misma carpeta.
        c = open()
        val again = c.createUpload(body)
        assertEquals(id, again["uploadId"]); assertEquals(true, again["resumed"])
        assertEquals(listOf(1), again["receivedParts"]); assertEquals(65536L, again["receivedBytes"])
        for (n in 2..4) {
            val part = data.copyOfRange((n - 1) * 65536, minOf(data.size, n * 65536))
            c.receivePart(id, n, sha(part), ByteArrayInputStream(part), c.planPart(id, n, sha(part), part.size.toLong()))
        }
        val done = c.completeUpload(id, mapOf("sha256" to sha(data)))
        assertEquals("completed", done["state"])
        assertEquals(done, c.completeUpload(id, emptyMap()), "completar es idempotente")
    }

    @Test fun `cuota logica de 5 GB recortada por el espacio real del telefono`() {
        val c = open(CloudConfig(quotaBytes = 1_000_000, deviceMarginBytes = 100_000))
        upload(c, "a.bin", bytes(400_000))
        var q = c.quota()
        assertEquals(1_000_000L, q["totalBytes"]); assertEquals(600_000L, q["availableBytes"]); assertEquals(40.0, q["percentUsed"])
        assertFailsWith<CloudError> { c.createUpload(mapOf("name" to "b.bin", "size" to 700_000L)) }.also {
            assertEquals("quota_exceeded", it.code); assertEquals(507, it.status)
        }
        // El telefono se queda casi sin sitio: manda el espacio real (y se dice).
        free = 300_000
        t += 3000                                       // el espacio real se mide como mucho cada 2 s
        q = c.quota()
        assertEquals(200_000L, q["availableBytes"]); assertEquals(true, q["limitedByDevice"])
        assertFailsWith<CloudError> { c.createUpload(mapOf("name" to "c.bin", "size" to 250_000L)) }.also { assertEquals("quota_exceeded", it.code) }
        // Lo reservado por una subida en curso cuenta como ocupado (y lo que le falta por llegar, en el disco).
        free = 50L shl 30
        t += 3000
        c.createUpload(mapOf("name" to "d.bin", "size" to 500_000L))
        q = c.quota()
        assertEquals(500_000L, q["reservedBytes"]); assertEquals(100_000L, q["availableBytes"]); assertEquals("low", q["state"])
    }

    @Test fun `papelera, restaurar y borrar definitivo liberan lo que deben`() {
        val c = open()
        val folder = c.createFolder(mapOf("name" to "Fotos"))
        val fid = folder["id"] as String
        val f1 = upload(c, "1.jpg", bytes(10_000, 1), fid)
        val f2 = upload(c, "2.jpg", bytes(20_000, 2))
        c.trash("file", f2["id"] as String)
        assertEquals(30_000L, c.quota()["usedBytes"], "la papelera sigue ocupando")
        assertEquals(20_000L, c.quota()["trashBytes"])
        c.trash("folder", fid)
        @Suppress("UNCHECKED_CAST")
        val trash = c.list(mapOf("view" to "trash"))["items"] as List<Map<String, Any?>>
        assertEquals(setOf(fid, f2["id"]), trash.map { it["id"] }.toSet())
        assertEquals(1, trash.first { it["id"] == fid }["itemCount"])
        // Lo borrado con su carpeta no se restaura suelto.
        assertFailsWith<CloudError> { c.restore("file", f1["id"] as String) }
        assertEquals(2, c.restore("folder", fid)["restored"])
        // Restaurar con el nombre ocupado: se numera, nunca se pisa.
        upload(c, "2.jpg", bytes(5_000, 9))
        @Suppress("UNCHECKED_CAST")
        val back = c.restore("file", f2["id"] as String)["item"] as Map<String, Any?>
        assertEquals("2 (1).jpg", back["name"])
        val freed = c.permanentDelete("folder", fid)
        assertEquals(10_000L, freed["freedBytes"])
        assertEquals(25_000L, c.quota()["usedBytes"])
        c.trash("file", f2["id"] as String)
        assertEquals(20_000L, c.emptyTrash()["freedBytes"])
        // Y lo purgado a los 30 dias.
        val f3 = upload(c, "3.jpg", bytes(1_000, 3))
        c.trash("file", f3["id"] as String)
        t += 31L * 86_400_000
        assertEquals(1, c.purgeTrash())
    }

    @Test fun `vistas - recientes, fotos y videos, busqueda, orden y paginas`() {
        val c = open()
        val names = listOf("b.jpg", "a.avi", "C.wav", "d.pdf", "e.zip")
        for ((i, n) in names.withIndex()) { t += 1000; upload(c, n, bytes(1000 + i, i)) }
        c.createFolder(mapOf("name" to "zz"))
        @Suppress("UNCHECKED_CAST")
        fun ids(q: Map<String, String>) = (c.list(q)["items"] as List<Map<String, Any?>>).map { it["name"] }
        assertEquals(listOf("zz", "a.avi", "b.jpg", "C.wav", "d.pdf", "e.zip"), ids(mapOf("parentId" to "root")))
        assertEquals(listOf("zz", "e.zip", "d.pdf", "C.wav", "b.jpg", "a.avi"), ids(mapOf("sort" to "name", "order" to "desc")))
        assertEquals(listOf("e.zip", "d.pdf", "C.wav", "a.avi", "b.jpg"), ids(mapOf("view" to "recent")))
        assertEquals(listOf("a.avi", "b.jpg"), ids(mapOf("view" to "recent", "kind" to "media")))
        assertEquals(listOf("C.wav"), ids(mapOf("view" to "search", "q" to "c.W")))
        assertEquals(emptyList<Any?>(), ids(mapOf("view" to "search", "q" to "   ")))
        val p1 = c.list(mapOf("limit" to "2"))
        assertNotNull(p1["nextCursor"])
        @Suppress("UNCHECKED_CAST")
        val p2 = (c.list(mapOf("limit" to "2", "cursor" to p1["nextCursor"] as String))["items"] as List<Map<String, Any?>>).map { it["name"] }
        assertEquals(listOf("b.jpg", "C.wav"), p2)
        assertEquals(CloudStore.encodeCursor(2), p1["nextCursor"])
        assertEquals("eyJvIjoyfQ", CloudStore.encodeCursor(2), "el mismo cursor que el servicio de Node")
    }

    @Test fun `enlaces firmados caducan y mueren con el archivo`() {
        val c = open()
        val f = upload(c, "v.avi", bytes(5_000))
        val (tok, exp) = c.signLink(f["id"] as String)
        assertEquals(t + 15 * 60_000, exp)
        assertEquals(5_000L, c.verifyLink(tok).size)
        assertFailsWith<CloudError> { c.verifyLink(tok.dropLast(2) + "xx") }.also { assertEquals("link_invalid", it.code) }
        assertFailsWith<CloudError> { c.verifyLink("abc") }
        t += 16 * 60_000
        assertFailsWith<CloudError> { c.verifyLink(tok) }
        val (tok2, _) = c.signLink(f["id"] as String)
        c.trash("file", f["id"] as String)
        assertFailsWith<CloudError> { c.verifyLink(tok2) }
    }

    @Test fun `miniaturas aparte del original`() {
        val c = open()
        val f = upload(c, "p.jpg", bytes(3_000))
        val jpeg = byteArrayOf(0xff.toByte(), 0xd8.toByte(), 1, 2, 3)
        assertFailsWith<CloudError> { c.setThumbnail(f["id"] as String, byteArrayOf(1, 2, 3), "image/jpeg") }
        assertFailsWith<CloudError> { c.setThumbnail(f["id"] as String, jpeg, "image/gif") }
        assertFailsWith<CloudError> { c.setThumbnail(f["id"] as String, ByteArray(600 * 1024), "image/jpeg") }
        assertEquals(true, c.setThumbnail(f["id"] as String, jpeg, "image/jpeg")["hasThumbnail"])
        assertEquals(5L, c.thumbnail(f["id"] as String).size)
        @Suppress("UNCHECKED_CAST")
        assertEquals(true, (c.list(mapOf())["items"] as List<Map<String, Any?>>)[0]["hasThumbnail"])
    }

    @Test fun `subidas que caducan, se cancelan o pierden su carpeta`() {
        val c = open()
        val u = c.createUpload(mapOf("name" to "q.bin", "size" to 1000L))
        c.abortUpload(u["uploadId"] as String)
        assertEquals("aborted", c.uploadStatus(u["uploadId"] as String)["state"])
        val u2 = c.createUpload(mapOf("name" to "w.bin", "size" to 1000L))
        t += 73L * 3600_000
        assertEquals(1, c.expireUploads())
        assertFailsWith<CloudError> { c.planPart(u2["uploadId"] as String, 1, "a".repeat(64), 1000) }.also { assertEquals("upload_expired", it.code) }
        assertEquals(0L, c.quota()["reservedBytes"])
        // La carpeta destino se borra mientras se sube: el archivo va a la raiz.
        val folder = c.createFolder(mapOf("name" to "tmp"))
        val data = bytes(1000)
        val u3 = c.createUpload(mapOf("name" to "x.bin", "size" to 1000L, "parentId" to folder["id"]))
        c.trash("folder", folder["id"] as String)
        val id3 = u3["uploadId"] as String
        c.receivePart(id3, 1, sha(data), ByteArrayInputStream(data), c.planPart(id3, 1, sha(data), 1000))
        @Suppress("UNCHECKED_CAST")
        assertNull((c.completeUpload(id3, emptyMap())["file"] as Map<String, Any?>)["parentId"])
    }

    @Test fun `importar desde el propio telefono respeta nombres y cuota`() {
        val c = open(CloudConfig(quotaBytes = 10_000, deviceMarginBytes = 0))
        val f = c.importFile("nota.txt", null, ByteArrayInputStream(bytes(3000)), 3000, null)
        assertEquals("text/plain", f["mime"]); assertEquals("phone", f["source"])
        val f2 = c.importFile("nota.txt", null, ByteArrayInputStream(bytes(3000)), 3000, null)
        assertEquals("nota (1).txt", f2["name"])
        assertFailsWith<CloudError> { c.importFile("big.bin", null, ByteArrayInputStream(bytes(5000)), 5000, null) }
        // Sin tamano conocido: se corta al pasar de lo que queda.
        assertFailsWith<CloudError> { c.importFile("big.bin", null, ByteArrayInputStream(bytes(5000)), 0, null) }
        assertEquals(6000L, c.quota()["usedBytes"])
    }

    @Test fun `un indice danado se aparta y no se pierde el arranque`() {
        val c = open()
        upload(c, "a.bin", bytes(100))
        File(tmp, "meta/index.json").writeText("{roto")
        val c2 = open()
        assertEquals(0, c2.fileCount())
        assertTrue(File(tmp, "meta").listFiles()!!.any { it.name.startsWith("index.damaged-") })
        assertFalse(File(tmp, "meta/index.json.tmp").exists())
    }
}
