package com.flexos.flexphone.cloud.media

import com.flexos.flexphone.cloud.CloudStore
import java.util.concurrent.LinkedBlockingQueue
import java.util.concurrent.TimeUnit

/**
 * LA COLA DE PREPARACION MULTIMEDIA: cada foto, vídeo o audio que llega a Flex Cloud se MIRA (analizador:
 * por sus bytes, no por su extension) y se deja listo para el P4:
 *
 *   compatible       -> `native`: se reproduce el original. NO se convierte ni se duplica nada.
 *   re-empaquetable  -> `ready`: se pasa a un contenedor del perfil sin recodificar.
 *   recodificable    -> `ready`: se convierte en el telefono (el P4 no hace trabajo pesado).
 *   imposible/roto   -> `unsupported` / `corrupt`, con el motivo, para decirlo en vez de dejar que el P4 falle.
 *
 * El ORIGINAL se conserva siempre. La version del perfil es un objeto aparte, ligado al original (se borra con el, cuenta
 * en la cuota y no sale en las listas). Decision y numeros en `docs/FLEX-MEDIA-PROFILE.md` ("Original + derivado").
 *
 * Un solo hilo de baja prioridad: convierte de uno en uno. [gate] deja que la app aplace las conversiones (bateria baja,
 * ahorro de energia) sin tocar el analisis, que es barato.
 */
class MediaPipeline(
    private val cloud: CloudStore,
    private val converter: MediaConverter?,
    private val gate: () -> Boolean = { true },
    private val log: (String) -> Unit = {},
) {
    private val queue = LinkedBlockingQueue<String>()
    private val queued = HashSet<String>()
    private val lock = Any()
    @Volatile private var running = false
    @Volatile private var busy = false
    private var worker: Thread? = null

    /** Cuantos hay en cola y cual se esta convirtiendo ahora (para la pantalla Flex Cloud del telefono). */
    class Counts(val queued: Int, val working: Boolean)
    fun counts() = Counts(synchronized(lock) { queued.size }, busy)

    fun start() {
        synchronized(lock) {
            if (running) return
            running = true
            worker = Thread({ loop() }, "flex-media").apply { isDaemon = true; priority = Thread.MIN_PRIORITY; start() }
        }
        cloud.onFileReady = { enqueue(it) }
        for (id in cloud.mediaToPrepare()) enqueue(id)
    }

    fun stop() {
        running = false
        cloud.onFileReady = null
        worker?.interrupt()
    }

    fun enqueue(id: String) {
        synchronized(lock) { if (!queued.add(id)) return }
        queue.offer(id)
    }

    /** Espera a que la cola se vacie (pruebas). */
    fun awaitIdle(timeoutMs: Long): Boolean {
        val end = System.currentTimeMillis() + timeoutMs
        while (System.currentTimeMillis() < end) {
            if (synchronized(lock) { queued.isEmpty() } && !busy) return true
            Thread.sleep(10)
        }
        return false
    }

    private fun loop() {
        while (running) {
            val id = try { queue.poll(1, TimeUnit.SECONDS) } catch (e: InterruptedException) { break } ?: continue
            busy = true
            try { process(id) } catch (e: Exception) { log("media: ${e.javaClass.simpleName} al preparar un archivo") }
            finally { synchronized(lock) { queued.remove(id) }; busy = false }
        }
    }

    /** Prepara un archivo, en este hilo (la cola lo llama; las pruebas tambien). */
    fun process(id: String) {
        val src = cloud.mediaSource(id) ?: return
        if (src.kind !in CloudStore.MEDIA_KINDS) return
        val file = cloud.originalFile(src.key)
        val a = try { FileSource(file).use { MediaAnalyzer.analyze(it) } } catch (e: java.io.IOException) {
            cloud.setPlayable(id, "failed", "none", "No se pudo leer el archivo en el teléfono."); return
        }
        val f = a.facts
        if (f.kind != MediaKind.OTHER) cloud.mergeMetadata(id, f.toMetadata())
        if (!src.hasThumb && converter != null) {
            try { converter.thumbnail(file, f)?.let { cloud.setThumbnail(id, it, "image/jpeg") } } catch (e: Exception) { /* sin miniatura no pasa nada */ }
        }
        when (a.plan) {
            Plan.NONE -> {
                if (f.kind == MediaKind.OTHER) { cloud.setPlayable(id, "corrupt", "none", "El archivo no es del tipo que indica su nombre."); return }
                cloud.setPlayable(id, "native", "none")
                maybePreview(id, src, file, a)
            }
            Plan.CORRUPT -> cloud.setPlayable(id, "corrupt", "none", a.reasons.firstOrNull() ?: "El archivo está dañado.")
            Plan.UNSUPPORTED -> cloud.setPlayable(id, "unsupported", "none", a.reasons.firstOrNull() ?: "Este formato no se puede convertir.")
            Plan.REMUX, Plan.TRANSCODE -> {
                val planName = a.plan.name.lowercase()
                if (converter == null || !converter.canConvert(f, a.plan)) {
                    cloud.setPlayable(id, "unsupported", planName, "Este teléfono no sabe convertir este formato todavía (${f.container}${f.videoCodec?.let { c -> " · $c" } ?: ""}).")
                    return
                }
                convert(id, src, file, a)
            }
        }
    }

    private fun convert(id: String, src: CloudStore.MediaSource, file: java.io.File, a: Analysis) {
        val planName = a.plan.name.lowercase()
        cloud.setPlayable(id, "pending", planName)
        var waited = 0
        while (running && !gate() && waited < MAX_GATE_WAITS) { try { Thread.sleep(GATE_POLL_MS) } catch (e: InterruptedException) { return }; waited++ }
        if (!running && converter == null) return
        cloud.setPlayable(id, "preparing", planName)
        val tmp = cloud.newTempFile("m")
        var lastCheck = 0L
        var gone = false
        val cancelled = {
            val t = System.currentTimeMillis()
            if (t - lastCheck > 500) { lastCheck = t; if (cloud.mediaSource(id) == null) gone = true }
            gone || !running
        }
        try {
            converter!!.convert(file, a.facts, a.plan, tmp, { cloud.setPlayableProgress(id, it) }, cancelled)
            if (gone || !running) { tmp.delete(); return }
            val check = FileSource(tmp).use { MediaAnalyzer.analyze(it) }
            // Nunca se publica lo que el propio analizador no daria por bueno: el P4 no tiene que fiarse del conversor.
            if (check.plan != Plan.NONE || check.facts.kind != a.facts.kind) {
                tmp.delete()
                cloud.setPlayable(id, "failed", planName, "El resultado de la conversión no cumple el perfil de Flex OS (${check.plan.name.lowercase()}).")
                log("media: resultado no valido (${check.plan})")
                return
            }
            val mime = when (check.facts.kind) { MediaKind.VIDEO -> "video/x-msvideo"; MediaKind.AUDIO -> "audio/wav"; else -> "image/jpeg" }
            if (!cloud.attachVariant(id, tmp, mime, check.facts.toMetadata(), planName)) {
                if (cloud.mediaSource(id) != null) cloud.setPlayable(id, "failed", planName, "No hay espacio en el teléfono para guardar la versión preparada.")
            }
        } catch (e: MediaException) {
            tmp.delete()
            if (gone || !running) return
            cloud.setPlayable(id, "failed", planName, e.message ?: "No se pudo convertir.")
        } catch (e: Exception) {
            tmp.delete()
            log("media: fallo inesperado ${e.javaClass.simpleName}")
            cloud.setPlayable(id, "failed", planName, "No se pudo convertir este archivo.")
        }
    }

    /**
     * Una foto compatible pero grande (varios MB: un movil de 12 MP) tambien tiene version ligera: el P4 la trae en un
     * momento y gasta menos PSRAM. Si no sale, no pasa nada: se abre el original.
     */
    private fun maybePreview(id: String, src: CloudStore.MediaSource, file: java.io.File, a: Analysis) {
        val f = a.facts
        if (f.kind != MediaKind.PHOTO || converter == null) return
        val big = src.size > MediaProfile.PHOTO_PREVIEW_MIN_BYTES || (f.longSide ?: 0) > MediaProfile.PHOTO_PREVIEW_MIN_SIDE
        if (!big || !converter.canConvert(f, Plan.TRANSCODE)) return
        val tmp = cloud.newTempFile("pv")
        try {
            converter.convert(file, f, Plan.TRANSCODE, tmp, {}, { !running })
            val check = FileSource(tmp).use { MediaAnalyzer.analyze(it) }
            if (check.plan != Plan.NONE || tmp.length() >= src.size * 9 / 10) { tmp.delete(); return }
            cloud.attachVariant(id, tmp, "image/jpeg", check.facts.toMetadata(), "preview")
        } catch (e: Exception) {
            tmp.delete()
        }
    }

    companion object {
        private const val GATE_POLL_MS = 2_000L
        private const val MAX_GATE_WAITS = 1800            // una hora esperando un buen momento; despues se convierte igualmente
    }
}
