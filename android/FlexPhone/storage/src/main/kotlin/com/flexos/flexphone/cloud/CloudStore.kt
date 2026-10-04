package com.flexos.flexphone.cloud

import java.io.File
import java.io.FileOutputStream
import java.io.IOException
import java.io.InputStream
import java.text.Normalizer
import java.util.Base64
import java.util.Locale
import javax.crypto.Mac
import javax.crypto.spec.SecretKeySpec

/**
 * Configuracion de Flex Cloud en el telefono.
 *
 * [quotaBytes] es la cuota LOGICA (5 GB de serie). Android no deja reservar una
 * particion para una app, asi que lo que de verdad se puede guardar es el menor
 * de dos: lo que queda de la cuota y lo que el telefono tiene libre menos
 * [deviceMarginBytes] (para que el movil no se quede sin sitio por Flex Cloud).
 */
data class CloudConfig(
    val quotaBytes: Long = 5L shl 30,
    val deviceMarginBytes: Long = 512L shl 20,
    val chunkDefault: Long = 8L shl 20,
    val chunkMin: Long = 64L shl 10,
    val chunkMax: Long = 16L shl 20,
    val maxParts: Int = 20_000,
    val maxFileBytes: Long = 0xFFFF_FFF0L,          // lo que el P4 sabe recorrer (32 bits)
    val uploadTtlMs: Long = 72L * 3600_000,
    val trashRetentionMs: Long = 30L * 86_400_000,
    val signedUrlTtlMs: Long = 15L * 60_000,
    val thumbMaxBytes: Int = 512 * 1024,
    val lowSpaceRatio: Double = 0.9,
    val planName: String = "phone",
)

/**
 * La LOGICA de Flex Cloud en el telefono: carpetas, archivos, papelera, cuota,
 * subidas reanudables y enlaces firmados.
 *
 * Es un ESPEJO de `cloud/src/cloud/service.js` (Flex Developer Studio): mismas
 * reglas, mismos codigos de error y las mismas formas de JSON, porque el gestor
 * de Flex Cloud del P4 (`FlexOS_Cloud.cpp`) habla con esto exactamente igual que
 * con el servicio de Internet. Las diferencias son de entorno, no de contrato:
 *  · Una sola "cuenta" (este telefono): no hay `account_id`.
 *  · Los metadatos van en un indice JSON con escritura atomica en vez de
 *    SQLite (son cientos o miles de filas; cabe de sobra en memoria).
 *  · Las partes se escriben en su sitio (ver [ObjectStore]).
 *
 * Hilos: un cerrojo para TODO el estado. Lo lento (recibir los bytes de una
 * parte, calcular la huella del archivo entero) va fuera del cerrojo y solo la
 * confirmacion va dentro, como en el servicio de Node.
 */
class CloudStore(
    private val store: ObjectStore,
    private val dir: File,
    val cfg: CloudConfig = CloudConfig(),
    private val now: () -> Long = System::currentTimeMillis,
    private val log: (String) -> Unit = {},
) {
    // ------------------------------------------------------------- modelo
    class Folder(
        val id: String, var parentId: String?, var name: String, var nameKey: String,
        val createdAt: Long, var updatedAt: Long,
        var deletedAt: Long? = null, var trashRoot: Boolean = false, var trashBatch: String? = null,
    )

    class FileRec(
        val id: String, var parentId: String?, var name: String, var nameKey: String,
        val storageKey: String, val size: Long, val mime: String, val kind: String, val sha256: String,
        var version: Int, val source: String, var metadata: Map<String, Any?>?,
        var thumbKey: String? = null, var thumbMime: String? = null, var thumbSize: Long = 0,
        val createdAt: Long, var updatedAt: Long,
        var deletedAt: Long? = null, var trashRoot: Boolean = false, var trashBatch: String? = null,
    ) {
        /** Lo que hay que saber para que el P4 lo reproduzca (null = todavia no se ha mirado, o no es multimedia). */
        var playable: Playable? = null
    }

    /**
     * El estado de un archivo multimedia respecto al PERFIL de Flex OS (ver `media/MediaProfile`):
     *  native      ya vale tal cual, se reproduce el original;
     *  pending     esperando turno para prepararlo;   preparing  convirtiendose ahora ([progress] 0..99);
     *  ready       hay una version del perfil guardada ([key], [size]...); se reproduce esa;
     *  failed      se intento y no salio ([reason]), se puede reintentar;
     *  unsupported ni el P4 ni este telefono saben convertirlo ([reason]);
     *  corrupt     el archivo esta roto o cortado ([reason]).
     */
    class Playable(
        var state: String, var plan: String, var reason: String? = null, var progress: Int = 0,
        var key: String? = null, var size: Long = 0, var mime: String? = null, var sha256: String? = null,
        var meta: Map<String, Any?>? = null, var updatedAt: Long = 0,
    )

    class Upload(
        val id: String, val parentId: String?, val name: String, val mime: String, val size: Long,
        val chunkSize: Long, val totalParts: Int, val sha256: String?, val clientKey: String?,
        var state: String, val storageKey: String, val source: String, val metadata: Map<String, Any?>?,
        val createdAt: Long, var updatedAt: Long, var expiresAt: Long,
        var fileId: String? = null, var error: String? = null,
        val parts: MutableMap<Int, String> = java.util.TreeMap(),       // n -> SHA-256 (las marcas del disco)
    ) {
        fun live() = state == "active" || state == "completing"
    }

    private val lock = Any()
    private val folders = LinkedHashMap<String, Folder>()
    private val files = LinkedHashMap<String, FileRec>()
    private val uploads = LinkedHashMap<String, Upload>()
    private val completing = HashSet<String>()
    private val writingParts = HashSet<String>()                         // "<upload>:<n>" escribiendose ahora
    private var secret: ByteArray = Names.randomBytes(32)
    private val indexFile = File(dir, "index.json")
    private var lastSaveMs = 0L
    private var dirty = false
    // El indice no se pudo leer al arrancar: los objetos del disco NO se tratan
    // como huerfanos (borrarlos seria perder los archivos por un indice roto).
    private var indexWasDamaged = false
    /** Se avisa (fuera del cerrojo) cuando un archivo nuevo ya esta guardado: la cola multimedia lo recoge. */
    @Volatile var onFileReady: ((String) -> Unit)? = null
    @Volatile private var freeCacheMs = 0L
    @Volatile private var freeCache = 0L

    init {
        if (!dir.isDirectory && !dir.mkdirs()) throw IOException("no se pudo crear la carpeta de Flex Cloud")
        load()
    }

    // ------------------------------------------------------- persistencia
    @Suppress("UNCHECKED_CAST")
    private fun load() {
        if (!indexFile.isFile) { save(); return }
        val root = try { Json.parseObject(indexFile.readText(Charsets.UTF_8)) } catch (e: Exception) {
            // Indice ilegible: se aparta (no se borra) y se empieza de cero. Los
            // objetos huerfanos los recoge recover().
            log("indice de Flex Cloud ilegible: se aparta")
            indexWasDamaged = true
            indexFile.renameTo(File(dir, "index.damaged-${now()}.json"))
            save()
            return
        }
        (root["secret"] as? String)?.let { if (it.length == 64) secret = hexToBytes(it) }
        for (m in (root["folders"] as? List<Map<String, Any?>>).orEmpty()) {
            val id = m["id"] as? String ?: continue
            if (!Names.ID_FOLDER.matches(id)) continue
            folders[id] = Folder(
                id, m["parentId"] as? String, m["name"] as? String ?: continue, m["nameKey"] as? String ?: continue,
                m["createdAt"].jsonLong() ?: 0, m["updatedAt"].jsonLong() ?: 0,
                m["deletedAt"].jsonLong(), m["trashRoot"] == true, m["trashBatch"] as? String,
            )
        }
        for (m in (root["files"] as? List<Map<String, Any?>>).orEmpty()) {
            val id = m["id"] as? String ?: continue
            if (!Names.ID_FILE.matches(id)) continue
            files[id] = FileRec(
                id, m["parentId"] as? String, m["name"] as? String ?: continue, m["nameKey"] as? String ?: continue,
                m["storageKey"] as? String ?: continue, m["size"].jsonLong() ?: continue, m["mime"] as? String ?: "application/octet-stream",
                m["kind"] as? String ?: "other", m["sha256"] as? String ?: continue, (m["version"].jsonLong() ?: 1).toInt(),
                m["source"] as? String ?: "device", m["metadata"] as? Map<String, Any?>,
                m["thumbKey"] as? String, m["thumbMime"] as? String, m["thumbSize"].jsonLong() ?: 0,
                m["createdAt"].jsonLong() ?: 0, m["updatedAt"].jsonLong() ?: 0,
                m["deletedAt"].jsonLong(), m["trashRoot"] == true, m["trashBatch"] as? String,
            ).also { f -> (m["playable"] as? Map<String, Any?>)?.let { f.playable = playableFrom(it) } }
        }
        for (m in (root["uploads"] as? List<Map<String, Any?>>).orEmpty()) {
            val id = m["id"] as? String ?: continue
            if (!Names.ID_UPLOAD.matches(id)) continue
            uploads[id] = Upload(
                id, m["parentId"] as? String, m["name"] as? String ?: continue, m["mime"] as? String ?: "application/octet-stream",
                m["size"].jsonLong() ?: continue, m["chunkSize"].jsonLong() ?: continue, (m["totalParts"].jsonLong() ?: continue).toInt(),
                m["sha256"] as? String, m["clientKey"] as? String, m["state"] as? String ?: "failed",
                m["storageKey"] as? String ?: continue, m["source"] as? String ?: "device", m["metadata"] as? Map<String, Any?>,
                m["createdAt"].jsonLong() ?: 0, m["updatedAt"].jsonLong() ?: 0, m["expiresAt"].jsonLong() ?: 0,
                m["fileId"] as? String, m["error"] as? String,
            )
        }
    }

    private fun folderMap(f: Folder) = linkedMapOf<String, Any?>(
        "id" to f.id, "parentId" to f.parentId, "name" to f.name, "nameKey" to f.nameKey,
        "createdAt" to f.createdAt, "updatedAt" to f.updatedAt, "deletedAt" to f.deletedAt,
        "trashRoot" to f.trashRoot, "trashBatch" to f.trashBatch,
    )

    private fun fileMap(f: FileRec) = linkedMapOf<String, Any?>(
        "id" to f.id, "parentId" to f.parentId, "name" to f.name, "nameKey" to f.nameKey, "storageKey" to f.storageKey,
        "size" to f.size, "mime" to f.mime, "kind" to f.kind, "sha256" to f.sha256, "version" to f.version,
        "source" to f.source, "metadata" to f.metadata, "thumbKey" to f.thumbKey, "thumbMime" to f.thumbMime,
        "thumbSize" to f.thumbSize, "createdAt" to f.createdAt, "updatedAt" to f.updatedAt, "deletedAt" to f.deletedAt,
        "trashRoot" to f.trashRoot, "trashBatch" to f.trashBatch, "playable" to f.playable?.let(::playableMap),
    )

    private fun playableMap(p: Playable) = linkedMapOf<String, Any?>(
        "state" to p.state, "plan" to p.plan, "reason" to p.reason, "key" to p.key, "size" to p.size, "mime" to p.mime,
        "sha256" to p.sha256, "meta" to p.meta, "updatedAt" to p.updatedAt,
    )

    @Suppress("UNCHECKED_CAST")
    private fun playableFrom(m: Map<String, Any?>): Playable? {
        val st = m["state"] as? String ?: return null
        // Lo que estaba convirtiendose cuando el telefono se apago vuelve a la cola.
        val state = if (st == "preparing") "pending" else st
        return Playable(state, m["plan"] as? String ?: "none", m["reason"] as? String, 0, m["key"] as? String, m["size"].jsonLong() ?: 0,
            m["mime"] as? String, m["sha256"] as? String, m["meta"] as? Map<String, Any?>, m["updatedAt"].jsonLong() ?: 0)
    }

    private fun uploadMap(u: Upload) = linkedMapOf<String, Any?>(
        "id" to u.id, "parentId" to u.parentId, "name" to u.name, "mime" to u.mime, "size" to u.size,
        "chunkSize" to u.chunkSize, "totalParts" to u.totalParts, "sha256" to u.sha256, "clientKey" to u.clientKey,
        "state" to u.state, "storageKey" to u.storageKey, "source" to u.source, "metadata" to u.metadata,
        "createdAt" to u.createdAt, "updatedAt" to u.updatedAt, "expiresAt" to u.expiresAt,
        "fileId" to u.fileId, "error" to u.error,
    )

    /** Escritura ATOMICA del indice: temporal + fsync + renombrado. */
    private fun save() {
        val doc = linkedMapOf<String, Any?>(
            "v" to 1, "secret" to ObjectStore.hex(secret),
            "folders" to folders.values.map(::folderMap),
            "files" to files.values.map(::fileMap),
            // Las subidas terminadas hace mas de un dia ya no le sirven a nadie.
            "uploads" to uploads.values.filter { it.live() || now() - it.updatedAt < 86_400_000 }.map(::uploadMap),
        )
        val tmp = File(dir, "index.json.tmp")
        FileOutputStream(tmp).use { out ->
            out.write(Json.write(doc).toByteArray(Charsets.UTF_8))
            out.fd.sync()
        }
        if (!tmp.renameTo(indexFile)) throw IOException("no se pudo guardar el indice de Flex Cloud")
        lastSaveMs = now()
        dirty = false
    }

    /** Guarda si hay cambios menores pendientes (caducidad de subidas) y hace tiempo de la ultima vez. */
    private fun saveIfStale() { if (dirty && now() - lastSaveMs > 60_000) save() }

    // ---------------------------------------------------------------- cuota
    fun deviceFreeBytes(): Long {
        val t = now()
        if (t - freeCacheMs > 2000 || t < freeCacheMs) { freeCache = store.freeBytes(); freeCacheMs = t }
        return freeCache
    }

    private fun variantBytes(f: FileRec): Long = f.playable?.takeIf { it.key != null }?.size ?: 0L
    private fun usedBytes() = files.values.sumOf { it.size + variantBytes(it) }
    private fun trashBytes() = files.values.filter { it.deletedAt != null }.sumOf { it.size + variantBytes(it) }
    private fun reservedBytes() = uploads.values.filter { it.live() }.sumOf { it.size }
    private fun receivedOf(u: Upload): Long = u.parts.keys.sumOf { expectedPartSize(u, it) }

    /** Lo que de verdad se puede guardar ahora: el menor entre la cuota y el telefono. */
    private fun availableBytes(): Pair<Long, Boolean> {
        val used = usedBytes()
        val reserved = reservedBytes()
        val logical = (cfg.quotaBytes - used - reserved).coerceAtLeast(0)
        // Con reserva real (fallocate) el espacio libre ya descuenta las subidas en
        // curso; si no, lo que les falta por llegar todavia no ocupa disco.
        val pending = if (store.reservesOnCreate) 0L else uploads.values.filter { it.live() }.sumOf { (it.size - receivedOf(it)).coerceAtLeast(0) }
        val device = (deviceFreeBytes() - cfg.deviceMarginBytes - pending).coerceAtLeast(0)
        return if (device < logical) device to true else logical to false
    }

    fun quota(): Map<String, Any?> = synchronized(lock) {
        val total = cfg.quotaBytes
        val used = usedBytes()
        val reserved = reservedBytes()
        val (available, limited) = availableBytes()
        val committed = used + reserved
        val ratio = if (total > 0) committed.toDouble() / total else 1.0
        val state = when {
            available <= 0 -> "full"
            ratio >= cfg.lowSpaceRatio || (limited && available < total / 10) -> "low"
            else -> "ok"
        }
        linkedMapOf(
            "plan" to cfg.planName,
            "totalBytes" to total,
            "usedBytes" to used,
            "reservedBytes" to reserved,
            "trashBytes" to trashBytes(),
            "availableBytes" to available,
            "percentUsed" to minOf(100.0, Math.round(ratio * 1000) / 10.0),
            "state" to state,
            // Extra del telefono (el P4 y la web los leen si estan): el espacio real.
            "deviceFreeBytes" to deviceFreeBytes(),
            "limitedByDevice" to limited,
        )
    }

    fun fileCount(): Int = synchronized(lock) { files.values.count { it.deletedAt == null } }

    // ------------------------------------------------------------- carpetas
    private fun liveFolder(id: String?): Folder? {
        if (id == null || id == "" || id == "root") return null
        if (!Names.ID_FOLDER.matches(id)) throw E.notFound("La carpeta")
        val f = folders[id]
        if (f == null || f.deletedAt != null) throw E.notFound("La carpeta")
        return f
    }

    private fun parentIdOf(raw: Any?): String? {
        if (raw != null && raw !is String) throw E.invalid("parentId no válido.")
        return liveFolder(raw as String?)?.id
    }

    private fun nameTaken(parent: String?, key: String, exceptId: String? = null): Boolean {
        for (f in folders.values) if (f.deletedAt == null && f.parentId == parent && f.nameKey == key && f.id != exceptId) return true
        for (f in files.values) if (f.deletedAt == null && f.parentId == parent && f.nameKey == key && f.id != exceptId) return true
        return false
    }

    private fun freeName(parent: String?, name: String, exceptId: String? = null): String {
        if (!nameTaken(parent, Names.nameKey(name), exceptId)) return name
        for (n in 1 until 10000) {
            val cand = Names.numberedName(name, n)
            if (!nameTaken(parent, Names.nameKey(cand), exceptId)) return cand
        }
        throw E.nameConflict(name)
    }

    private fun breadcrumb(folderId: String?): List<Map<String, Any?>> {
        val path = ArrayList<Map<String, Any?>>()
        var id = folderId
        var i = 0
        while (id != null && i < MAX_DEPTH) {
            val f = folders[id] ?: break
            path.add(0, linkedMapOf("id" to f.id, "name" to f.name))
            id = f.parentId
            i++
        }
        return path
    }

    private fun depth(folderId: String?) = breadcrumb(folderId).size

    fun folderView(f: Folder): Map<String, Any?> = linkedMapOf<String, Any?>(
        "type" to "folder", "id" to f.id, "name" to f.name, "parentId" to f.parentId,
        "createdAt" to f.createdAt, "updatedAt" to f.updatedAt,
    ).also { if (f.deletedAt != null) it["deletedAt"] = f.deletedAt }

    fun fileView(f: FileRec): Map<String, Any?> = linkedMapOf<String, Any?>(
        "type" to "file", "id" to f.id, "name" to f.name, "parentId" to f.parentId, "size" to f.size,
        "mime" to f.mime, "kind" to f.kind, "sha256" to f.sha256, "version" to f.version, "status" to "ready",
        "storageLocation" to "phone", "source" to f.source, "hasThumbnail" to (f.thumbKey != null),
        "metadata" to f.metadata, "createdAt" to f.createdAt, "updatedAt" to f.updatedAt,
    ).also {
        if (f.deletedAt != null) it["deletedAt"] = f.deletedAt
        f.playable?.let { p -> it["playable"] = playableView(f, p) }
    }

    /** `playable` en el JSON: que hay que saber para abrirlo en el P4 (y que dice el telefono mientras lo prepara). */
    private fun playableView(f: FileRec, p: Playable): Map<String, Any?> {
        val m = linkedMapOf<String, Any?>("state" to p.state, "plan" to p.plan, "profile" to MEDIA_PROFILE)
        when (p.state) {
            "native" -> { m["size"] = f.size; m["mime"] = f.mime; m["sha256"] = f.sha256; f.metadata?.let { md -> for (k in PLAYABLE_FACTS) md[k]?.let { v -> m[k] = v } } }
            "ready" -> { m["size"] = p.size; m["mime"] = p.mime; p.sha256?.let { h -> m["sha256"] = h }; p.meta?.let { md -> for (k in PLAYABLE_FACTS) md[k]?.let { v -> m[k] = v } } }
            "preparing" -> m["progress"] = p.progress
            "failed", "unsupported", "corrupt" -> m["reason"] = p.reason
        }
        return m
    }

    fun createFolder(body: Map<String, Any?>): Map<String, Any?> = synchronized(lock) {
        val clean = Names.normalizeName(body["name"])
        val parent = parentIdOf(body["parentId"])
        if (parent != null && depth(parent) >= MAX_DEPTH - 1) throw E.invalid("Demasiados niveles de carpetas.")
        var final = clean
        if (nameTaken(parent, Names.nameKey(clean))) {
            if (body["conflict"] != "rename") throw E.nameConflict(clean)
            final = freeName(parent, clean)
        }
        val t = now()
        val f = Folder(Names.newId("fld"), parent, final, Names.nameKey(final), t, t)
        folders[f.id] = f
        save()
        folderView(f)
    }

    fun getFolder(id: String?): Map<String, Any?> = synchronized(lock) {
        val f = liveFolder(id) ?: return@synchronized linkedMapOf<String, Any?>("id" to "root", "name" to "Flex Cloud", "parentId" to null, "path" to emptyList<Any>())
        LinkedHashMap(folderView(f)).also { it["path"] = breadcrumb(f.id) }
    }

    fun updateFolder(id: String, body: Map<String, Any?>): Map<String, Any?> = synchronized(lock) {
        val f = liveFolder(id) ?: throw E.invalid("La carpeta raíz no se puede cambiar.")
        var parent = f.parentId
        if (body.containsKey("parentId")) {
            parent = parentIdOf(body["parentId"])
            // Mover una carpeta dentro de si misma (o de un descendiente) la dejaria colgada de un ciclo.
            var p = parent
            var i = 0
            while (p != null && i < MAX_DEPTH) {
                if (p == f.id) throw E.folderCycle()
                p = folders[p]?.parentId
                i++
            }
        }
        val newName = if (body.containsKey("name")) Names.normalizeName(body["name"]) else f.name
        if (nameTaken(parent, Names.nameKey(newName), f.id)) throw E.nameConflict(newName)
        f.name = newName; f.nameKey = Names.nameKey(newName); f.parentId = parent; f.updatedAt = now()
        save()
        folderView(f)
    }

    // --------------------------------------------------------------- listados
    fun list(q: Map<String, String>): Map<String, Any?> = synchronized(lock) {
        val limit = (q["limit"]?.toIntOrNull() ?: 100).coerceIn(1, PAGE_MAX)
        val offset = decodeCursor(q["cursor"])
        val view = q["view"] ?: "folder"
        val desc = q["order"]?.lowercase(Locale.ROOT) == "desc"
        val kinds = setOf("photo", "video", "audio", "document", "archive", "other", "media")
        val kind = q["kind"]?.takeIf { it in kinds }
        fun kindOk(k: String) = kind == null || (if (kind == "media") k == "photo" || k == "video" else k == kind)

        // Filas comunes: (es carpeta, id, name_key, size, kind, updated_at)
        data class Row(val folder: Boolean, val id: String, val nameKey: String, val size: Long, val kind: String, val updated: Long, val deleted: Long)

        fun sortCmp(sort: String?): Comparator<Row> {
            val base: Comparator<Row> = when (sort) {
                "date" -> compareBy { it.updated }
                "size" -> compareBy { it.size }
                "type" -> compareBy { it.kind }
                else -> compareBy { it.nameKey }
            }
            return if (desc) base.reversed() else base
        }
        // Carpetas primero (como "ORDER BY t DESC" del servicio), luego el orden pedido, luego el id.
        fun withFolders(c: Comparator<Row>): Comparator<Row> =
            compareByDescending<Row> { it.folder }.then(c).thenBy { it.id }

        var folderInfo: Map<String, Any?>? = null
        val rows: List<Row> = when (view) {
            "trash" -> {
                val r = ArrayList<Row>()
                for (f in folders.values) if (f.deletedAt != null && f.trashRoot) r.add(Row(true, f.id, f.nameKey, 0, "folder", f.deletedAt!!, f.deletedAt!!))
                for (f in files.values) if (f.deletedAt != null && f.trashRoot) r.add(Row(false, f.id, f.nameKey, f.size, f.kind, f.deletedAt!!, f.deletedAt!!))
                r.sortedWith(compareByDescending<Row> { it.deleted }.thenBy { it.id })
            }
            "recent" -> files.values.filter { it.deletedAt == null && kindOk(it.kind) }
                .map { Row(false, it.id, it.nameKey, it.size, it.kind, it.updatedAt, 0) }
                .sortedWith(compareByDescending<Row> { it.updated }.thenBy { it.id })
            "search" -> {
                val term = Normalizer.normalize(q["q"] ?: "", Normalizer.Form.NFC).lowercase(Locale.ROOT).trim().take(100)
                if (term.isEmpty()) return@synchronized linkedMapOf<String, Any?>("items" to emptyList<Any>(), "nextCursor" to null, "folder" to null)
                val r = ArrayList<Row>()
                for (f in folders.values) if (f.deletedAt == null && f.nameKey.contains(term)) r.add(Row(true, f.id, f.nameKey, 0, "folder", f.updatedAt, 0))
                for (f in files.values) if (f.deletedAt == null && f.nameKey.contains(term) && kindOk(f.kind)) r.add(Row(false, f.id, f.nameKey, f.size, f.kind, f.updatedAt, 0))
                r.sortedWith(withFolders(sortCmp(q["sort"])))
            }
            else -> {
                val parent = parentIdOf(q["parentId"])
                folderInfo = if (parent == null) linkedMapOf("id" to "root", "name" to "Flex Cloud", "parentId" to null, "path" to emptyList<Any>())
                else LinkedHashMap(folderView(folders[parent]!!)).also { it["path"] = breadcrumb(parent) }
                val r = ArrayList<Row>()
                if (kind == null) for (f in folders.values) if (f.deletedAt == null && f.parentId == parent) r.add(Row(true, f.id, f.nameKey, 0, "folder", f.updatedAt, 0))
                for (f in files.values) if (f.deletedAt == null && f.parentId == parent && kindOk(f.kind)) r.add(Row(false, f.id, f.nameKey, f.size, f.kind, f.updatedAt, 0))
                r.sortedWith(withFolders(sortCmp(q["sort"])))
            }
        }
        val page = rows.drop(offset).take(limit + 1)
        val more = page.size > limit
        val items = page.take(limit).map { row ->
            if (row.folder) {
                val f = folders[row.id]!!
                val v = LinkedHashMap(folderView(f))
                if (view == "trash") v["itemCount"] = files.values.count { it.trashBatch != null && it.trashBatch == f.trashBatch }
                v
            } else fileView(files[row.id]!!)
        }
        linkedMapOf("items" to items, "nextCursor" to (if (more) encodeCursor(offset + limit) else null), "folder" to folderInfo)
    }

    // --------------------------------------------------------------- archivos
    private fun file(id: String, live: Boolean = true): FileRec {
        if (!Names.ID_FILE.matches(id)) throw E.notFound("El archivo")
        val f = files[id] ?: throw E.notFound("El archivo")
        if (live && f.deletedAt != null) throw E.notFound("El archivo")
        return f
    }

    fun getFile(id: String): Map<String, Any?> = synchronized(lock) {
        val f = file(id, live = false)
        LinkedHashMap(fileView(f)).also { it["path"] = breadcrumb(f.parentId) }
    }

    /** Lo necesario para servir los bytes (copia: el registro puede cambiar luego). */
    class Download(val storageKey: String, val size: Long, val mime: String, val name: String, val sha256: String, val updatedAt: Long)

    /** Flujo de [start, end] de un objeto (descargas, streaming, miniaturas). */
    fun openRead(key: String, start: Long, end: Long): InputStream = store.read(key, start, end)

    fun fileForDownload(id: String): Download = synchronized(lock) {
        val f = file(id)
        Download(f.storageKey, f.size, f.mime, f.name, f.sha256, f.updatedAt)
    }

    fun updateFile(id: String, body: Map<String, Any?>): Map<String, Any?> = synchronized(lock) {
        val f = file(id)
        val parent = if (body.containsKey("parentId")) parentIdOf(body["parentId"]) else f.parentId
        val newName = if (body.containsKey("name")) Names.normalizeName(body["name"]) else f.name
        if (nameTaken(parent, Names.nameKey(newName), f.id)) throw E.nameConflict(newName)
        f.name = newName; f.nameKey = Names.nameKey(newName); f.parentId = parent; f.updatedAt = now()
        save()
        fileView(f)
    }

    // --------------------------------------------------------------- papelera
    // Mandar a la papelera NO libera cuota (el archivo sigue guardado y se puede
    // restaurar). Solo el borrado definitivo la libera.
    fun trash(type: String, id: String): Map<String, Any?> = synchronized(lock) {
        val r = trashLocked(type, id)
        save()
        r
    }

    private fun trashLocked(type: String, id: String): Map<String, Any?> {
        val t = now()
        val batch = Names.newId("trb")
        if (type == "file") {
            val f = file(id)
            f.deletedAt = t; f.trashRoot = true; f.trashBatch = batch
            return linkedMapOf("batch" to batch, "files" to 1, "folders" to 0)
        }
        val root = liveFolder(id) ?: throw E.invalid("La carpeta raíz no se puede borrar.")
        // La carpeta y todas sus descendientes vivas.
        val ids = ArrayList<String>()
        val queue = ArrayDeque<String>().apply { add(root.id) }
        while (queue.isNotEmpty() && ids.size < 100_000) {
            val cur = queue.removeFirst()
            ids.add(cur)
            for (f in folders.values) if (f.parentId == cur && f.deletedAt == null && f.id != root.id) queue.add(f.id)
        }
        var nFiles = 0
        for (fid in ids) {
            val f = folders[fid]!!
            f.deletedAt = t; f.trashRoot = fid == root.id; f.trashBatch = batch
            for (x in files.values) if (x.parentId == fid && x.deletedAt == null) {
                x.deletedAt = t; x.trashRoot = false; x.trashBatch = batch; nFiles++
            }
        }
        return linkedMapOf("batch" to batch, "files" to nFiles, "folders" to ids.size)
    }

    private fun trashRootRow(type: String, id: String): Any {
        if (type == "file") {
            if (!Names.ID_FILE.matches(id)) throw E.notFound("El archivo")
            return files[id] ?: throw E.notFound("El archivo")
        }
        if (!Names.ID_FOLDER.matches(id)) throw E.notFound("La carpeta")
        return folders[id] ?: throw E.notFound("La carpeta")
    }

    fun restore(type: String, id: String): Map<String, Any?> = synchronized(lock) {
        val row = trashRootRow(type, id)
        val deletedAt = if (row is FileRec) row.deletedAt else (row as Folder).deletedAt
        if (deletedAt == null) {
            return@synchronized linkedMapOf("restored" to 0, "item" to (if (row is FileRec) fileView(row) else folderView(row as Folder)))
        }
        val trashRoot = if (row is FileRec) row.trashRoot else (row as Folder).trashRoot
        if (!trashRoot) throw E.invalid("Este elemento se borró junto con su carpeta: restaura la carpeta.")
        // Si la carpeta de origen ya no existe (o sigue en la papelera), vuelve a la raiz.
        // Si el nombre esta ocupado, se renombra: nunca se pisa nada.
        var parent = if (row is FileRec) row.parentId else (row as Folder).parentId
        if (parent != null && folders[parent]?.deletedAt != null || (parent != null && folders[parent] == null)) parent = null
        val oldName = if (row is FileRec) row.name else (row as Folder).name
        val selfId = if (row is FileRec) row.id else (row as Folder).id
        val name = freeName(parent, oldName, selfId)
        val batch = if (row is FileRec) row.trashBatch else (row as Folder).trashBatch
        val t = now()
        when (row) {
            is FileRec -> { row.deletedAt = null; row.trashRoot = false; row.trashBatch = null; row.parentId = parent; row.name = name; row.nameKey = Names.nameKey(name); row.updatedAt = t }
            is Folder -> { row.deletedAt = null; row.trashRoot = false; row.trashBatch = null; row.parentId = parent; row.name = name; row.nameKey = Names.nameKey(name); row.updatedAt = t }
        }
        var restored = 1
        if (batch != null) {
            for (f in folders.values) if (f.trashBatch == batch) { f.deletedAt = null; f.trashBatch = null; restored++ }
            for (f in files.values) if (f.trashBatch == batch) { f.deletedAt = null; f.trashBatch = null; restored++ }
        }
        save()
        linkedMapOf("restored" to restored, "item" to (if (row is FileRec) fileView(row) else folderView(row as Folder)))
    }

    /** Borrado DEFINITIVO: registros fuera y cuota liberada; los bytes se borran despues. */
    fun permanentDelete(type: String, id: String): Map<String, Any?> {
        val removed: Purged = synchronized(lock) {
            var row = trashRootRow(type, id)
            val deleted = if (row is FileRec) row.deletedAt else (row as Folder).deletedAt
            if (deleted == null) { trashLocked(type, id); row = trashRootRow(type, id) }
            val trashRoot = if (row is FileRec) row.trashRoot else (row as Folder).trashRoot
            if (!trashRoot) throw E.invalid("Este elemento se borró junto con su carpeta: borra la carpeta.")
            val batch = (if (row is FileRec) row.trashBatch else (row as Folder).trashBatch)!!
            val p = purgeBatch(batch)
            save()
            p
        }
        removeObjects(removed.keys)
        return linkedMapOf("files" to removed.files, "folders" to removed.folders, "freedBytes" to removed.bytes)
    }

    private class Purged(val files: Int, val folders: Int, val bytes: Long, val keys: List<String>)

    private fun purgeBatch(batch: String): Purged {
        val gone = files.values.filter { it.trashBatch == batch }
        val bytes = gone.sumOf { it.size + variantBytes(it) }
        val keys = gone.flatMap { listOfNotNull(it.storageKey, it.thumbKey, it.playable?.key) }
        for (f in gone) files.remove(f.id)
        val fids = folders.values.filter { it.trashBatch == batch }.map { it.id }
        for (fid in fids) {
            // Lo que se mando a la papelera POR SEPARADO antes que la carpeta (otro
            // lote) pierde su carpeta: se queda en la papelera, en la raiz.
            for (x in files.values) if (x.parentId == fid) x.parentId = null
            for (x in folders.values) if (x.parentId == fid) x.parentId = null
            folders.remove(fid)
        }
        return Purged(gone.size, fids.size, bytes, keys)
    }

    private fun removeObjects(keys: List<String>) {
        for (k in keys) try { store.remove(k) } catch (e: Exception) { log("no se pudo borrar un objeto") }
    }

    fun emptyTrash(): Map<String, Any?> {
        var freed = 0L
        var n = 0
        val keys = ArrayList<String>()
        synchronized(lock) {
            val batches = LinkedHashSet<String>()
            for (f in files.values) if (f.deletedAt != null && f.trashRoot) f.trashBatch?.let(batches::add)
            for (f in folders.values) if (f.deletedAt != null && f.trashRoot) f.trashBatch?.let(batches::add)
            for (b in batches) {
                val r = purgeBatch(b)
                freed += r.bytes; n += r.files; keys.addAll(r.keys)
            }
            save()
        }
        removeObjects(keys)
        return linkedMapOf("files" to n, "freedBytes" to freed)
    }

    fun purgeTrash(): Int {
        val limit = now() - cfg.trashRetentionMs
        val keys = ArrayList<String>()
        var n = 0
        synchronized(lock) {
            val batches = LinkedHashSet<String>()
            for (f in files.values) if (f.deletedAt != null && f.trashRoot && f.deletedAt!! <= limit) f.trashBatch?.let(batches::add)
            for (f in folders.values) if (f.deletedAt != null && f.trashRoot && f.deletedAt!! <= limit) f.trashBatch?.let(batches::add)
            if (batches.isEmpty()) return 0
            for (b in batches) { val r = purgeBatch(b); n += r.files; keys.addAll(r.keys) }
            save()
        }
        removeObjects(keys)
        return n
    }

    // ------------------------------------------------------------- miniaturas
    // La miniatura es un objeto APARTE: el original nunca se toca.
    fun setThumbnail(id: String, bytes: ByteArray, mime: String): Map<String, Any?> {
        if (!Regex("^image/(jpeg|png|webp)$").matches(mime)) throw E.invalid("La miniatura debe ser JPEG, PNG o WebP.")
        if (bytes.isEmpty() || bytes.size > cfg.thumbMaxBytes) throw E.payloadTooLarge()
        val okMagic = (mime == "image/jpeg" && bytes.size > 2 && bytes[0] == 0xff.toByte() && bytes[1] == 0xd8.toByte()) ||
            (mime == "image/png" && bytes.size > 4 && String(bytes, 0, 4, Charsets.ISO_8859_1) == "\u0089PNG") ||
            (mime == "image/webp" && bytes.size > 12 && String(bytes, 0, 4, Charsets.ISO_8859_1) == "RIFF" && String(bytes, 8, 4, Charsets.ISO_8859_1) == "WEBP")
        if (!okMagic) throw E.invalid("La miniatura no es una imagen válida.")
        synchronized(lock) { file(id) }
        val key = store.newStorageKey()
        store.putSmall(key, bytes)
        val old = synchronized(lock) {
            val cur = try { file(id) } catch (e: CloudError) { store.remove(key); throw e }
            val o = cur.thumbKey
            cur.thumbKey = key; cur.thumbMime = mime; cur.thumbSize = bytes.size.toLong()
            save()
            o
        }
        if (old != null) try { store.remove(old) } catch (e: Exception) { /* lo recoge recover() */ }
        return linkedMapOf("id" to id, "hasThumbnail" to true)
    }

    class Thumb(val key: String, val mime: String, val size: Long)

    fun thumbnail(id: String): Thumb = synchronized(lock) {
        val f = file(id, live = false)
        val k = f.thumbKey ?: throw E.notFound("La miniatura")
        Thumb(k, f.thumbMime ?: "image/jpeg", f.thumbSize)
    }

    // ---------------------------------------------------------------- subidas
    private fun upload(id: String): Upload {
        if (!Names.ID_UPLOAD.matches(id)) throw E.uploadNotFound()
        return uploads[id] ?: throw E.uploadNotFound()
    }

    fun createUpload(body: Map<String, Any?>, source: String = "device"): Map<String, Any?> {
        val name = Names.normalizeName(body["name"])
        val size = body["size"].jsonLong()
        if (size == null || size < 0) throw E.invalid("El tamaño del archivo no es válido.")
        if (size > cfg.maxFileBytes) throw E.fileTooLarge(cfg.maxFileBytes)
        val shaRaw = body["sha256"]
        val sha = if (shaRaw == null) null else (shaRaw as? String)?.lowercase(Locale.ROOT)
        if (shaRaw != null && (sha == null || !isHex64(sha))) throw E.invalid("sha256 debe ser hexadecimal de 64 caracteres.")
        val clientKey = (body["clientKey"] as? String)?.takeIf { Regex("^[A-Za-z0-9_.:/@+=-]{8,200}$").matches(it) }
        val conflict = if (body["conflict"] == "fail") "fail" else "rename"
        var chunk = body["chunkSize"].jsonLong() ?: cfg.chunkDefault
        chunk = chunk.coerceIn(cfg.chunkMin, cfg.chunkMax)
        while (size > 0 && ceilDiv(size, chunk) > cfg.maxParts && chunk < cfg.chunkMax) chunk = minOf(cfg.chunkMax, chunk * 2)
        val totalParts = if (size == 0L) 0 else ceilDiv(size, chunk)
        if (totalParts > cfg.maxParts) throw E.fileTooLarge(cfg.chunkMax * cfg.maxParts)
        val mime = Names.mimeFor(name, body["mimeType"])
        val meta = cleanMeta(body["metadata"])

        val u: Upload
        synchronized(lock) {
            val parent = parentIdOf(body["parentId"])
            val t = now()
            // REANUDAR: misma clave del cliente, nombre, tamano y carpeta -> la sesion que ya existia.
            if (clientKey != null) {
                val prev = uploads.values.filter {
                    it.clientKey == clientKey && it.state == "active" && it.size == size && it.name == name && it.parentId == parent
                }.maxByOrNull { it.createdAt }
                if (prev != null && prev.expiresAt > t && store.hasUploadData(prev.id)) {
                    prev.expiresAt = t + cfg.uploadTtlMs; prev.updatedAt = t
                    save()
                    return LinkedHashMap(uploadView(prev)).also { it["resumed"] = true }
                }
            }
            if (conflict == "fail" && nameTaken(parent, Names.nameKey(name))) throw E.nameConflict(name)
            // RESERVA: usado + reservado + este archivo <= cuota, y que quepa en el telefono.
            val (available, limited) = availableBytes()
            if (size > available) {
                if (limited) throw E.noSpaceDevice(available)
                throw E.quotaExceeded(size, available)
            }
            u = Upload(
                Names.newId("upl"), parent, name, mime, size, chunk, totalParts, sha, clientKey, "active",
                store.newStorageKey(), source, meta, t, t, t + cfg.uploadTtlMs,
            )
            uploads[u.id] = u
        }
        try {
            store.createUploadData(u.id, size)
        } catch (e: IOException) {
            synchronized(lock) { uploads.remove(u.id) }
            store.removeUpload(u.id)
            throw E.noSpaceDevice(deviceFreeBytes())
        }
        synchronized(lock) { save() }
        log("subida creada ${u.id} ($size bytes, $totalParts partes)")
        return LinkedHashMap(synchronized(lock) { uploadView(u) }).also { it["resumed"] = false }
    }

    private fun uploadView(u: Upload): Map<String, Any?> {
        val v = linkedMapOf<String, Any?>(
            "uploadId" to u.id, "name" to u.name, "size" to u.size, "mime" to u.mime, "parentId" to u.parentId,
            "chunkSize" to u.chunkSize, "totalParts" to u.totalParts, "receivedParts" to u.parts.keys.toList(),
            "receivedBytes" to receivedOf(u), "sha256" to u.sha256, "state" to u.state,
            "createdAt" to u.createdAt, "updatedAt" to u.updatedAt, "expiresAt" to u.expiresAt,
        )
        u.fileId?.let { fid -> v["fileId"] = fid; files[fid]?.let { v["file"] = fileView(it) } }
        u.error?.let { v["error"] = it }
        return v
    }

    fun uploadStatus(id: String): Map<String, Any?> = synchronized(lock) {
        val u = upload(id)
        if (u.state == "active" && u.expiresAt <= now()) expireLocked(u)
        uploadView(u)
    }

    fun listUploads(): List<Map<String, Any?>> = synchronized(lock) {
        val t = now()
        uploads.values.filter { it.live() && it.expiresAt > t }.sortedByDescending { it.updatedAt }.take(50).map(::uploadView)
    }

    fun expectedPartSize(u: Upload, n: Int): Long = if (n < u.totalParts) u.chunkSize else u.size - (u.totalParts - 1).toLong() * u.chunkSize

    private fun activeUpload(id: String): Upload {
        val u = upload(id)
        if (u.state != "active") {
            if (u.state == "expired") throw E.uploadExpired()
            throw E.uploadState(u.state)
        }
        if (u.expiresAt <= now()) { expireLocked(u); throw E.uploadExpired() }
        return u
    }

    private fun progress(u: Upload, n: Int, size: Long, sha: String, already: Boolean): Map<String, Any?> = linkedMapOf(
        "part" to n, "size" to size, "sha256" to sha, "alreadyReceived" to already,
        "receivedBytes" to receivedOf(u), "receivedCount" to u.parts.size, "totalParts" to u.totalParts,
    )

    /**
     * Prepara la recepcion de la parte [n]: si ya esta (misma huella) contesta sin
     * leer el cuerpo; si no, devuelve lo necesario para escribirla en su sitio.
     */
    class PartPlan(val already: Map<String, Any?>?, val offset: Long, val expected: Long)

    fun planPart(id: String, n: Int, declaredSha: String?, contentLength: Long): PartPlan = synchronized(lock) {
        val u = activeUpload(id)
        if (n < 1 || n > u.totalParts) throw E.partRange(u.totalParts)
        if (declaredSha == null || !isHex64(declaredSha)) throw E.invalid("Falta el SHA-256 de la parte (cabecera X-Part-SHA256 o Content-Digest).")
        val expected = expectedPartSize(u, n)
        val prev = u.parts[n]
        if (prev != null) {
            if (prev == declaredSha) return@synchronized PartPlan(progress(u, n, expected, prev, true), 0, expected)
            throw E.partConflict()
        }
        if (contentLength >= 0 && contentLength != expected) throw E.partSize(expected, contentLength)
        // La misma parte por dos conexiones a la vez (la de antes de un corte de
        // Wi-Fi que aun no se sabe muerta y la del reintento) NO se escribe a la
        // vez en la misma zona: la segunda espera su turno con un 503 que el P4
        // y la web tratan como "reintenta".
        val key = "$id:$n"
        if (!writingParts.add(key)) throw CloudError(503, "upload_busy", "Esa parte se está recibiendo por otra conexión.")
        PartPlan(null, (n - 1).toLong() * u.chunkSize, expected)
    }

    fun receivePart(id: String, n: Int, declaredSha: String, src: InputStream, plan: PartPlan): Map<String, Any?> {
        val key = "$id:$n"
        try {
            val got = try {
                store.receivePart(id, plan.offset, src, plan.expected)
            } catch (e: IOException) {
                throw CloudError(503, "upload_busy", "Se cortó la parte a mitad: se reintentará.")
            }
            if (got.tooLong || got.size != plan.expected) throw E.partSize(plan.expected, if (got.tooLong) plan.expected + 1 else got.size)
            if (got.sha256 != declaredSha) {
                log("parte con checksum incorrecto $id/$n")
                throw E.checksum(declaredSha, got.sha256)
            }
            synchronized(lock) {
                val cur = uploads[id]
                if (cur == null || cur.state != "active") throw E.uploadState(cur?.state ?: "gone")
                val won = cur.parts[n]
                if (won != null) {
                    if (won != got.sha256) throw E.partConflict()
                    return progress(cur, n, got.size, won, true)
                }
                store.commitPart(id, n, got.sha256)
                cur.parts[n] = got.sha256
                val t = now()
                cur.updatedAt = t; cur.expiresAt = t + cfg.uploadTtlMs
                dirty = true
                saveIfStale()
                return progress(cur, n, got.size, got.sha256, false)
            }
        } finally {
            synchronized(lock) { writingParts.remove(key) }
        }
    }

    fun completeUpload(id: String, body: Map<String, Any?>): Map<String, Any?> {
        val u: Upload
        val declared: String?
        synchronized(lock) {
            val u0 = upload(id)
            if (u0.state == "completed") return uploadView(u0)                       // idempotente
            if (u0.state == "completing" || id in completing) throw E.uploadState("completing")
            u = activeUpload(id)
            val bodySha = body["sha256"]
            declared = if (bodySha != null) (bodySha as? String)?.lowercase(Locale.ROOT) ?: throw E.invalid("sha256 no válido.") else u.sha256
            if (declared != null && !isHex64(declared)) throw E.invalid("sha256 debe ser hexadecimal de 64 caracteres.")
            if (u.sha256 != null && declared != null && declared != u.sha256) throw E.invalid("El sha256 no coincide con el declarado al crear la subida.")
            if (u.parts.size != u.totalParts) {
                val missing = ArrayList<Int>()
                var k = 1
                while (k <= u.totalParts && missing.size < 100) { if (!u.parts.containsKey(k)) missing.add(k); k++ }
                throw E.incomplete(missing)
            }
            completing.add(u.id)
            u.state = "completing"; u.updatedAt = now()
            save()
        }
        val hashed: ObjectStore.Hashed
        try {
            hashed = store.hashUploadData(u.id)
            if (hashed.size != u.size) throw IOException("tamano ${hashed.size} != ${u.size}")
            if (declared != null && hashed.sha256 != declared) {
                // Cada parte llego verificada: si el total no cuadra, el archivo del
                // cliente cambio entre partes. Se descarta y se libera la reserva.
                synchronized(lock) { u.state = "failed"; u.error = "checksum_mismatch"; u.updatedAt = now(); completing.remove(u.id); save() }
                store.removeUpload(u.id)
                log("subida descartada: SHA-256 del archivo no coincide (${u.id})")
                throw E.checksum(declared, hashed.sha256)
            }
            store.publishUpload(u.id, u.storageKey)
        } catch (e: IOException) {
            // Fallo de disco a mitad: la subida vuelve a 'active' con sus partes; se puede reintentar.
            synchronized(lock) { if (u.state == "completing") u.state = "active"; completing.remove(u.id); save() }
            log("fallo al terminar la subida ${u.id}: ${e.message}")
            throw CloudError(500, "internal_error", "No se pudo terminar la subida en el teléfono. Vuelve a intentarlo.")
        }
        synchronized(lock) {
            val t = now()
            var parent = u.parentId
            if (parent != null && (folders[parent] == null || folders[parent]!!.deletedAt != null)) parent = null
            val name = freeName(parent, u.name)
            val fid = Names.newId("fil")
            files[fid] = FileRec(
                fid, parent, name, Names.nameKey(name), u.storageKey, u.size, u.mime, Names.kindFor(u.mime), hashed.sha256,
                1, u.source, u.metadata, createdAt = t, updatedAt = t,
            )
            u.state = "completed"; u.fileId = fid; u.updatedAt = t
            completing.remove(u.id)
            save()
        }
        store.removeUpload(u.id)
        log("subida completada ${u.id} -> ${u.fileId}")
        u.fileId?.let { announce(it) }
        return synchronized(lock) { uploadView(u) }
    }

    fun abortUpload(id: String): Map<String, Any?> {
        val drop: Boolean
        synchronized(lock) {
            val u = upload(id)
            if (u.state == "completing") throw E.uploadState("completing")
            drop = u.state == "active"
            if (drop) { u.state = "aborted"; u.updatedAt = now(); save() }
        }
        if (drop) store.removeUpload(id)
        return synchronized(lock) { uploadView(upload(id)) }
    }

    private fun expireLocked(u: Upload) {
        if (u.state != "active") return
        u.state = "expired"; u.updatedAt = now()
        save()
        try { store.removeUpload(u.id) } catch (e: Exception) { /* lo recoge recover() */ }
    }

    fun expireUploads(): Int = synchronized(lock) {
        val t = now()
        val list = uploads.values.filter { it.state == "active" && it.expiresAt <= t && it.id !in completing }
        for (u in list) expireLocked(u)
        saveIfStale()
        list.size
    }

    // ------------------------------------------------------- enlaces firmados
    // URL temporal para UN archivo (el navegador la usa contra el telefono:
    // descargas y <video> con rangos de verdad, sin pasar por el P4).
    fun signLink(id: String): Pair<String, Long> = synchronized(lock) {
        val f = file(id)
        val exp = now() + cfg.signedUrlTtlMs
        val payload = b64url(Json.write(linkedMapOf("f" to f.id, "v" to f.version, "e" to exp)).toByteArray(Charsets.UTF_8))
        val sig = b64url(hmac(secret, payload.toByteArray(Charsets.US_ASCII)))
        "$payload.$sig" to exp
    }

    fun verifyLink(token: String): Download = synchronized(lock) {
        val parts = token.split('.')
        if (parts.size != 2 || parts[0].isEmpty() || parts[1].isEmpty() || token.length > 512) throw E.linkInvalid()
        val expect = hmac(secret, parts[0].toByteArray(Charsets.US_ASCII))
        val got = try { Base64.getUrlDecoder().decode(parts[1]) } catch (e: Exception) { throw E.linkInvalid() }
        if (!constantTimeEq(expect, got)) throw E.linkInvalid()
        val d = try { Json.parseObject(String(Base64.getUrlDecoder().decode(parts[0]), Charsets.UTF_8)) } catch (e: Exception) { throw E.linkInvalid() }
        val exp = d["e"].jsonLong() ?: throw E.linkInvalid()
        if (exp < now()) throw E.linkInvalid()
        val f = files[d["f"] as? String ?: throw E.linkInvalid()] ?: throw E.linkInvalid()
        if (f.deletedAt != null || f.version.toLong() != d["v"].jsonLong()) throw E.linkInvalid()
        Download(f.storageKey, f.size, f.mime, f.name, f.sha256, f.updatedAt)
    }

    // ------------------------------------------------------------ importar
    /**
     * Guarda un archivo que llega del PROPIO telefono (Compartir -> Flex Cloud),
     * sin pasar por la red. Mismas reglas de nombre y cuota que una subida.
     */
    fun importFile(name: String, parentId: String?, src: InputStream, sizeHint: Long, mimeHint: String?): Map<String, Any?> {
        val clean = Names.normalizeName(name)
        val maxNow = synchronized(lock) {
            parentIdOf(parentId)
            val (available, limited) = availableBytes()
            if (sizeHint > available) { if (limited) throw E.noSpaceDevice(available) else throw E.quotaExceeded(sizeHint, available) }
            available
        }
        val key = store.newStorageKey()
        val h = try { store.importStream(key, src, maxNow) } catch (e: IOException) { throw E.quotaExceeded(sizeHint, maxNow) }
        return synchronized(lock) {
            val parent = try { parentIdOf(parentId) } catch (e: CloudError) { null }
            val final = freeName(parent, clean)
            val mime = Names.mimeFor(final, mimeHint)
            val t = now()
            val fid = Names.newId("fil")
            files[fid] = FileRec(fid, parent, final, Names.nameKey(final), key, h.size, mime, Names.kindFor(mime), h.sha256, 1, "phone", null, createdAt = t, updatedAt = t)
            save()
            fileView(files[fid]!!)
        }.also { v -> (v["id"] as? String)?.let { announce(it) } }
    }


    // ------------------------------------------------------- multimedia (perfil de Flex OS)
    private fun announce(id: String) { try { onFileReady?.invoke(id) } catch (e: Exception) { log("aviso multimedia fallido") } }

    /** Lo que la cola multimedia necesita de un archivo. */
    class MediaSource(val id: String, val key: String, val size: Long, val name: String, val mime: String, val kind: String,
                      val metadata: Map<String, Any?>?, val hasThumb: Boolean, val playableState: String?, val sha256: String)

    fun mediaSource(id: String): MediaSource? = synchronized(lock) {
        val f = files[id]?.takeIf { it.deletedAt == null } ?: return null
        MediaSource(f.id, f.storageKey, f.size, f.name, f.mime, f.kind, f.metadata, f.thumbKey != null, f.playable?.state, f.sha256)
    }

    /** Archivos multimedia vivos que aun no se han mirado o que se quedaron en cola (para reanudar tras reiniciar). */
    fun mediaToPrepare(): List<String> = synchronized(lock) {
        files.values.filter { it.deletedAt == null && it.kind in MEDIA_KINDS && (it.playable == null || it.playable!!.state == "pending" || (it.playable!!.state == "unsupported" && it.playable!!.plan != "none")) }.map { it.id }
    }

    fun originalFile(key: String): File = store.fileOf(key)
    fun newTempFile(prefix: String): File = store.newTempFile(prefix)

    /** ¿Cabe [bytes] mas (cuota y espacio real del telefono)? */
    fun canFit(bytes: Long): Boolean = synchronized(lock) { availableBytes().first >= bytes }

    /** Los datos que el analizador SI leyo pasan a `metadata` (los del cliente que no coincidan se conservan). */
    fun mergeMetadata(id: String, facts: Map<String, Any?>) = synchronized(lock) {
        val f = files[id] ?: return@synchronized
        val m = LinkedHashMap<String, Any?>(f.metadata ?: emptyMap())
        m.putAll(facts)
        f.metadata = m
        dirty = true
    }

    /** Cambia el estado de preparacion. Los cambios de estado se guardan; el progreso solo vive en memoria. */
    fun setPlayable(id: String, state: String, plan: String, reason: String? = null) = synchronized(lock) {
        val f = files[id] ?: return@synchronized
        val old = f.playable
        val p = Playable(state, plan, reason, 0, old?.key, old?.size ?: 0, old?.mime, old?.sha256, old?.meta, now())
        f.playable = p
        save()
    }

    fun setPlayableProgress(id: String, pct: Int) = synchronized(lock) {
        val p = files[id]?.playable ?: return@synchronized
        if (p.state == "preparing") p.progress = pct.coerceIn(0, 99)
    }

    /**
     * Guarda [tmp] (un temporal de este almacen) como la version del perfil de [id]. null = no cabia o el archivo
     * ya no existe. La cuota incluye la version: es espacio que ocupa de verdad.
     */
    fun attachVariant(id: String, tmp: File, mime: String, meta: Map<String, Any?>, plan: String): Boolean {
        val size = tmp.length()
        val ok = synchronized(lock) { files[id]?.deletedAt == null && files[id] != null && availableBytes().first >= size }
        if (!ok) { tmp.delete(); return false }
        val key = store.newStorageKey()
        val h = store.adoptFile(key, tmp)
        var orphan = false
        var old: String? = null
        synchronized(lock) {
            val f = files[id]
            if (f == null) orphan = true
            else {
                old = f.playable?.key
                f.playable = Playable("ready", plan, null, 0, key, h.size, mime, h.sha256, meta, now())
                save()
            }
        }
        val drop = if (orphan) key else old
        if (drop != null) try { store.remove(drop) } catch (e: Exception) { /* lo recoge recover() */ }
        return !orphan
    }

    /** Lo que se sirve al P4: la version del perfil si la hay, el original si ya vale tal cual; si no, `not_ready`. */
    fun playableDownload(id: String): Download = synchronized(lock) {
        val f = file(id)
        val p = f.playable ?: throw E.notReady("unknown", "Este archivo todavía no se ha preparado para Flex OS.")
        when (p.state) {
            "native" -> Download(f.storageKey, f.size, f.mime, f.name, f.sha256, f.updatedAt)
            "ready" -> Download(p.key!!, p.size, p.mime ?: "application/octet-stream", derivedName(f.name, p.mime), p.sha256 ?: "", p.updatedAt)
            else -> throw E.notReady(p.state, p.reason)
        }
    }

    private fun derivedName(name: String, mime: String?): String {
        val stem = name.substringBeforeLast('.', name)
        val ext = when (mime) { "video/x-msvideo" -> ".avi"; "audio/wav" -> ".wav"; "image/jpeg" -> ".jpg"; else -> "" }
        return stem + ext
    }

    /** Reintenta lo que fallo (o lo que no se pudo convertir porque aun no habia conversor). */
    fun retryPrepare(id: String): Map<String, Any?> {
        synchronized(lock) {
            val f = file(id)
            val p = f.playable
            if (f.kind !in MEDIA_KINDS) throw E.invalid("Este archivo no es multimedia.")
            if (p != null && p.state in setOf("pending", "preparing", "native", "ready")) return fileView(f)
            f.playable = Playable("pending", p?.plan ?: "none", null, 0, null, 0, null, null, null, now())
            save()
        }
        announce(id)
        return synchronized(lock) { fileView(file(id)) }
    }

    // ------------------------------------------------------------ mantenimiento
    /** Al arrancar: lo que quedo a medias por un corte se deja coherente. */
    fun recover(): Map<String, Any?> = synchronized(lock) {
        store.sweepTemp()
        var stuck = 0
        for (u in uploads.values) if (u.state == "completing") { u.state = "active"; stuck++ }
        val keep = uploads.values.filter { it.state == "active" }.map { it.id }.toSet()
        for (d in store.listUploadDirs()) if (d !in keep) store.removeUpload(d)
        for (u in uploads.values) {
            if (u.state != "active") continue
            if (!store.hasUploadData(u.id)) { u.state = "failed"; u.error = "data_lost"; continue }
            u.parts.clear()
            for ((n, sha) in store.partsOf(u.id)) if (n in 1..u.totalParts) u.parts[n] = sha
        }
        // Objetos que ningun registro reclama: un borrado definitivo interrumpido.
        val keys = HashSet<String>()
        for (f in files.values) { keys.add(f.storageKey); f.thumbKey?.let(keys::add); f.playable?.key?.let(keys::add) }
        for (u in uploads.values) if (u.live()) keys.add(u.storageKey)
        if (!indexWasDamaged) store.sweepOrphans(keys)
        save()
        linkedMapOf("stuck" to stuck, "uploads" to keep.size)
    }

    // ------------------------------------------------------------ utilidades
    companion object {
        const val PAGE_MAX = 200
        val MEDIA_KINDS = setOf("photo", "video", "audio")
        const val MAX_DEPTH = 64
        // El cliente puede mandar estos; el analizador del telefono (MediaAnalyzer) rellena los de medios.
        private val META_INT = setOf("width", "height", "durationMs", "takenAt", "orientation", "fpsX100", "sampleRate", "channels")
        private val META_STR = setOf("device", "localPath", "origin", "container", "codec", "audioCodec")
        const val MEDIA_PROFILE = "flexos-ultra-v1"
        private val PLAYABLE_FACTS = listOf("width", "height", "durationMs", "fpsX100", "sampleRate", "channels", "container", "codec", "audioCodec")

        fun isHex64(s: String) = s.length == 64 && s.all { it in '0'..'9' || it in 'a'..'f' }
        fun ceilDiv(a: Long, b: Long): Int = ((a + b - 1) / b).toInt()

        fun cleanMeta(raw: Any?): Map<String, Any?>? {
            val m = raw as? Map<*, *> ?: return null
            val out = LinkedHashMap<String, Any?>()
            for ((k, v) in m) {
                val key = k as? String ?: continue
                if (key in META_INT) { val n = v.jsonLong(); if (n != null && n >= 0) out[key] = n }
                if (key in META_STR && v is String && v.length <= 200) out[key] = v
            }
            return out.ifEmpty { null }
        }

        fun encodeCursor(o: Int): String = b64url("{\"o\":$o}".toByteArray(Charsets.UTF_8))
        fun decodeCursor(c: String?): Int {
            if (c.isNullOrEmpty() || c.length > 64) return 0
            return try {
                val o = Json.parseObject(String(Base64.getUrlDecoder().decode(c), Charsets.UTF_8))["o"].jsonLong()
                if (o != null && o in 0..Int.MAX_VALUE) o.toInt() else 0
            } catch (e: Exception) { 0 }
        }

        fun b64url(b: ByteArray): String = Base64.getUrlEncoder().withoutPadding().encodeToString(b)
        fun hmac(key: ByteArray, msg: ByteArray): ByteArray =
            Mac.getInstance("HmacSHA256").apply { init(SecretKeySpec(key, "HmacSHA256")) }.doFinal(msg)
        fun constantTimeEq(a: ByteArray, b: ByteArray): Boolean {
            if (a.size != b.size) return false
            var d = 0
            for (i in a.indices) d = d or (a[i].toInt() xor b[i].toInt())
            return d == 0
        }
        fun hexToBytes(h: String): ByteArray = ByteArray(h.length / 2) { i -> h.substring(2 * i, 2 * i + 2).toInt(16).toByte() }
    }
}
