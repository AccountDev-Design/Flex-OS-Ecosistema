package com.flexos.flexphone.cloud.media

enum class MediaKind { PHOTO, VIDEO, AUDIO, OTHER }

/**
 * Que hay que hacer con un archivo para que el P4 lo reproduzca:
 *  · [NONE]: ya esta dentro del perfil: se reproduce tal cual (nunca se convierte);
 *  · [REMUX]: los flujos ya valen y solo el contenedor no: se re-empaqueta SIN recodificar;
 *  · [TRANSCODE]: algun flujo (codec, resolucion, fps, giro...) no vale: se recodifica;
 *  · [UNSUPPORTED]: es multimedia pero ni el P4 ni este telefono saben convertirlo;
 *  · [CORRUPT]: la cabecera esta rota o el archivo esta cortado.
 */
enum class Plan { NONE, REMUX, TRANSCODE, UNSUPPORTED, CORRUPT }

/** Lo que se SABE del archivo. Un dato que no se pudo leer es null: nunca se inventa. */
class MediaFacts(
    val kind: MediaKind,
    val container: String,
    val videoCodec: String? = null,
    val audioCodec: String? = null,
    val width: Int? = null,
    val height: Int? = null,
    val durationMs: Long? = null,
    val fpsX100: Int? = null,
    val sampleRate: Int? = null,
    val channels: Int? = null,
    val orientation: Int? = null,
    val rotation: Int? = null,
) {
    /** Claves que Flex Cloud acepta en `metadata` (ver `CloudStore.cleanMeta`). */
    fun toMetadata(): Map<String, Any?> {
        val m = LinkedHashMap<String, Any?>()
        width?.let { m["width"] = it }
        height?.let { m["height"] = it }
        durationMs?.let { m["durationMs"] = it }
        orientation?.let { m["orientation"] = it }
        fpsX100?.let { m["fpsX100"] = it }
        sampleRate?.let { m["sampleRate"] = it }
        channels?.let { m["channels"] = it }
        m["container"] = container
        videoCodec?.let { m["codec"] = it }
        audioCodec?.let { m["audioCodec"] = it }
        return m
    }

    val longSide: Int? get() = if (width != null && height != null) maxOf(width, height) else null
}

class Analysis(val facts: MediaFacts, val plan: Plan, val reasons: List<String>) {
    override fun toString() = "Analysis(${facts.container}, $plan, $reasons)"
}

/**
 * El ANALIZADOR DE COMPATIBILIDAD: mira los BYTES (nunca la extension: un `.mkv` renombrado a
 * `.mp4` sigue siendo un `.mkv`), compara con el perfil real del firmware ([MediaProfile]) y dice
 * que hay que hacer. No convierte nada.
 *
 * "Convertible" significa lo que ESTE telefono sabe hacer: contenedores que Android lee
 * (`MediaExtractor`: MP4/MOV/3GP, WebM/Matroska), MJPEG por fotogramas, imagenes que decodifica
 * `BitmapFactory`, y los WAV que lee [WavPcm]. Lo demas (AVI con H.264/Xvid, ProRes, WMA...)
 * es [Plan.UNSUPPORTED], y se dice claro en vez de dejar que el P4 lo intente y falle.
 */
object MediaAnalyzer {

    fun analyze(src: ByteSource): Analysis {
        if (src.size == 0L) return corrupt(MediaKind.OTHER, "vacio", "El archivo está vacío.")
        val h = src.head(0, 64)
        return when {
            h.size >= 3 && h.u8(0) == 0xFF && h.u8(1) == 0xD8 -> jpeg(src)
            h.hasAt(0, "\u0089PNG\r\n\u001a\n") -> png(src, h)
            h.hasAt(0, "GIF8") -> gif(h)
            h.hasAt(0, "RIFF") && h.hasAt(8, "WEBP") -> webp(src, h)
            h.hasAt(0, "RIFF") && h.hasAt(8, "AVI ") -> avi(src)
            h.hasAt(0, "RIFF") && h.hasAt(8, "WAVE") -> wav(src)
            h.hasAt(4, "ftyp") -> isoBase(src)
            h.size >= 4 && h.u32be(0) == 0x1A45DFA3L -> matroska(src)
            h.hasAt(0, "BM") && isBmp(h) -> bmp(h)
            h.hasAt(0, "fLaC") -> flac(h)
            h.hasAt(0, "OggS") -> ogg(src)
            h.hasAt(0, "ID3") || mpegAudioAt(h, 0) -> mp3(src)
            adts(h) -> aac(h)
            h.hasAt(0, "FORM") && h.hasAt(8, "AIFF") -> unsupported(MediaKind.AUDIO, "aiff", "AIFF no es un formato que Flex OS ni este teléfono sepan convertir.")
            h.size >= 4 && h.u32be(0) == 0x3026B275L -> unsupported(MediaKind.VIDEO, "asf", "Los archivos de Windows Media (WMV/WMA) no se pueden convertir.")
            else -> Analysis(MediaFacts(MediaKind.OTHER, "otro"), Plan.NONE, listOf("No es un archivo multimedia."))
        }
    }

    // ------------------------------------------------------------------ helpers
    private fun corrupt(k: MediaKind, c: String, why: String) = Analysis(MediaFacts(k, c), Plan.CORRUPT, listOf(why))
    private fun unsupported(k: MediaKind, c: String, why: String, f: MediaFacts = MediaFacts(k, c)) = Analysis(f, Plan.UNSUPPORTED, listOf(why))
    private fun decide(f: MediaFacts, reasons: List<String>, convertible: Boolean = true): Analysis =
        if (reasons.isEmpty()) Analysis(f, Plan.NONE, emptyList())
        else if (convertible) Analysis(f, Plan.TRANSCODE, reasons)
        else Analysis(f, Plan.UNSUPPORTED, reasons)

    // ------------------------------------------------------------------ imagenes
    private fun jpeg(src: ByteSource): Analysis {
        val j = Jpeg.parse(src) ?: return corrupt(MediaKind.PHOTO, "jpeg", "El JPEG está dañado: no tiene una cabecera válida.")
        val f = MediaFacts(MediaKind.PHOTO, "jpeg", width = j.width, height = j.height, orientation = j.orientation)
        if (!j.hasEoi) return Analysis(f, Plan.CORRUPT, listOf("El JPEG está cortado: le falta el final."))
        val why = ArrayList<String>()
        if (j.progressive) why.add("JPEG progresivo: el P4 solo abre JPEG baseline.")
        else if (!j.sequentialHuffman) why.add("Este tipo de JPEG no lo abre el P4.")
        else if (!j.layoutDecodable) why.add("Este JPEG usa un muestreo o una profundidad que el P4 no abre.")
        if (maxOf(j.width, j.height) > MediaProfile.JPEG_DECODER_MAX_SIDE) why.add("Es más grande de ${MediaProfile.JPEG_DECODER_MAX_SIDE} px por lado.")
        if (j.orientation != 1) why.add("Está girado por EXIF y el P4 no lee EXIF: se endereza.")
        if (src.size > MediaProfile.PHOTO_MAX_BYTES) why.add("Pesa más de ${MediaProfile.PHOTO_MAX_BYTES shr 20} MB.")
        return decide(f, why)
    }

    private fun png(src: ByteSource, h: ByteArray): Analysis {
        if (h.size < 33 || !h.hasAt(12, "IHDR")) return corrupt(MediaKind.PHOTO, "png", "El PNG está dañado.")
        val w = h.u32be(16).toInt(); val ht = h.u32be(20).toInt()
        if (w <= 0 || ht <= 0) return corrupt(MediaKind.PHOTO, "png", "El PNG está dañado.")
        val t = src.head(maxOf(0L, src.size - 12), 12)
        if (!t.hasAt(4, "IEND")) return corrupt(MediaKind.PHOTO, "png", "El PNG está cortado.")
        return Analysis(MediaFacts(MediaKind.PHOTO, "png", width = w, height = ht), Plan.TRANSCODE, listOf("PNG: el P4 solo abre JPEG."))
    }

    private fun gif(h: ByteArray): Analysis {
        if (h.size < 10) return corrupt(MediaKind.PHOTO, "gif", "El GIF está dañado.")
        val f = MediaFacts(MediaKind.PHOTO, "gif", width = h.u16le(6), height = h.u16le(8))
        return Analysis(f, Plan.TRANSCODE, listOf("GIF: el P4 solo abre JPEG (se usa el primer fotograma)."))
    }

    private fun isBmp(h: ByteArray) = h.size >= 18 && h.u32le(14) in setOf(12L, 40L, 52L, 56L, 64L, 108L, 124L)

    private fun bmp(h: ByteArray): Analysis {
        val hdr = h.u32le(14)
        val (w, ht) = if (hdr == 12L) h.u16le(18) to h.u16le(20) else h.u32le(18).toInt() to Math.abs(h.u32le(22).toInt())
        return Analysis(MediaFacts(MediaKind.PHOTO, "bmp", width = w, height = ht), Plan.TRANSCODE, listOf("BMP: el P4 solo abre JPEG."))
    }

    private fun webp(src: ByteSource, h: ByteArray): Analysis {
        var w: Int? = null; var ht: Int? = null
        if (h.size >= 30) when {
            h.hasAt(12, "VP8X") -> { w = 1 + (h.u8(24) or (h.u8(25) shl 8) or (h.u8(26) shl 16)); ht = 1 + (h.u8(27) or (h.u8(28) shl 8) or (h.u8(29) shl 16)) }
            h.hasAt(12, "VP8L") && h.u8(20) == 0x2F -> { val b = h.u32le(21); w = 1 + (b and 0x3FFF).toInt(); ht = 1 + ((b shr 14) and 0x3FFF).toInt() }
            h.hasAt(12, "VP8 ") && h.u8(23) == 0x9D && h.u8(24) == 0x01 && h.u8(25) == 0x2A -> { w = h.u16le(26) and 0x3FFF; ht = h.u16le(28) and 0x3FFF }
        }
        if (w == null && src.size < 30) return corrupt(MediaKind.PHOTO, "webp", "El WebP está dañado.")
        return Analysis(MediaFacts(MediaKind.PHOTO, "webp", width = w, height = ht), Plan.TRANSCODE, listOf("WebP: el P4 solo abre JPEG."))
    }

    // ------------------------------------------------------------------ AVI
    private fun avi(src: ByteSource): Analysis {
        val h = Avi.parseHeader(src) ?: return corrupt(MediaKind.VIDEO, "avi", "El AVI está dañado: no tiene cabecera o no tiene datos de vídeo.")
        val handler = h.videoHandler.trimEnd('\u0000', ' ')
        val comp = h.videoCompression.trimEnd('\u0000', ' ')
        val mjpegHandler = handler in MediaProfile.AVI_FOURCC_ACCEPT
        val mjpegComp = comp in MediaProfile.AVI_FOURCC_ACCEPT
        val codecName = when {
            mjpegHandler || mjpegComp -> "mjpeg"
            handler.isNotEmpty() -> handler
            comp.isNotEmpty() -> comp
            else -> null
        }
        val f = MediaFacts(MediaKind.VIDEO, "avi", videoCodec = codecName, audioCodec = h.audioFormatTag?.let { wavTagName(it) },
            width = h.width.takeIf { it > 0 }, height = h.height.takeIf { it > 0 }, durationMs = h.durationMs, fpsX100 = h.fpsX100,
            sampleRate = h.audioRate, channels = h.audioChannels)
        if (handler.isEmpty() && comp.isEmpty()) return Analysis(f, Plan.CORRUPT, listOf("El AVI no tiene pista de vídeo."))
        if (!mjpegHandler && !mjpegComp) {
            return unsupported(MediaKind.VIDEO, "avi", "AVI con vídeo ${codecName ?: "desconocido"}: ni el P4 ni este teléfono saben leerlo (Android no abre AVI).", f)
        }
        if (h.width <= 0 || h.height <= 0) return Analysis(f, Plan.CORRUPT, listOf("El AVI no declara el tamaño del vídeo."))
        val idx = Avi.readIndex(src, h)
        if (h.totalFrames <= 0L && (idx == null || idx.videoFrames == 0)) return Analysis(f, Plan.CORRUPT, listOf("El AVI no tiene fotogramas."))
        // El primer fotograma de verdad: ¿es un JPEG que el firmware decodifica y del tamaño declarado?
        var first: JpegInfo? = null
        var firstBytes = 0
        Avi.forEachFrame(src, h) { _, jb -> if (jb.isNotEmpty()) { first = Jpeg.parse(BytesSource(jb)); firstBytes = jb.size; false } else true }
        val why = ArrayList<String>()
        if (first == null) return Analysis(f, Plan.CORRUPT, listOf("El primer fotograma del AVI no es un JPEG válido."))
        val fi = first!!
        if (!fi.p4Decodable) why.add("Los fotogramas no son JPEG baseline: el P4 no los abre.")
        else if (fi.width != h.width || fi.height != h.height) why.add("El tamaño de los fotogramas no coincide con la cabecera.")
        if (h.openDml) why.add("AVI OpenDML (más de 1 GB): el P4 solo lee la primera parte.")
        if (maxOf(h.width, h.height) > MediaProfile.VIDEO_MAX_SIDE) why.add("Es más grande de ${MediaProfile.VIDEO_MAX_SIDE} px: el P4 no iría a ritmo.")
        val fps100 = h.fpsX100
        if (fps100 != null && fps100 > MediaProfile.VIDEO_MAX_FPS * 100 + 5) why.add("Más de ${MediaProfile.VIDEO_MAX_FPS} fotogramas por segundo.")
        val maxFrame = maxOf(idx?.maxFrameBytes ?: 0, firstBytes, h.suggestedBuffer.coerceAtMost(Int.MAX_VALUE.toLong()).toInt() - 8)
        if (maxFrame > MediaProfile.VIDEO_FRAME_MAX_BYTES) why.add("Hay fotogramas de más de ${MediaProfile.VIDEO_FRAME_MAX_BYTES / 1024} KB.")
        if (why.isNotEmpty()) return Analysis(f, Plan.TRANSCODE, why)
        // Dentro del sobre. Si solo falla la etiqueta del codec (fccHandler vacio) basta con reescribir la cabecera.
        if (!mjpegHandler) return Analysis(f, Plan.REMUX, listOf("La cabecera no declara el códec MJPEG: se reescribe sin recodificar."))
        return Analysis(f, Plan.NONE, emptyList())
    }

    private fun wavTagName(t: Int) = when (t) {
        WavInfo.TAG_PCM -> "pcm"; WavInfo.TAG_MS_ADPCM -> "adpcm_ms"; WavInfo.TAG_FLOAT -> "pcm_float"
        WavInfo.TAG_ALAW -> "alaw"; WavInfo.TAG_ULAW -> "ulaw"; WavInfo.TAG_IMA -> "ima_adpcm"; WavInfo.TAG_MP3 -> "mp3"
        0x2000 -> "ac3"; 0xFF, 0x1600 -> "aac"; else -> "0x" + t.toString(16)
    }

    // ------------------------------------------------------------------ WAV
    private fun wav(src: ByteSource): Analysis {
        val w = Wav.parse(src) ?: return corrupt(MediaKind.AUDIO, "wav", "El WAV está dañado: no tiene cabecera o datos de audio.")
        val codec = when (w.tag) { WavInfo.TAG_PCM -> "pcm_" + (if (w.bits == 8) "u8" else "s${w.bits}le"); else -> wavTagName(w.tag) }
        val f = MediaFacts(MediaKind.AUDIO, "wav", audioCodec = codec, durationMs = w.durationMs, sampleRate = w.rate.takeIf { it > 0 }, channels = w.channels.takeIf { it > 0 })
        if (w.channels <= 0 || w.rate <= 0) return Analysis(f, Plan.CORRUPT, listOf("El WAV no declara canales o frecuencia."))
        if (w.truncated) return Analysis(f, Plan.CORRUPT, listOf("El WAV está cortado: declara más audio del que hay."))
        if (w.dataBytes <= 0L) return Analysis(f, Plan.CORRUPT, listOf("El WAV no tiene audio."))
        val why = ArrayList<String>()
        val pcm = (w.tag == WavInfo.TAG_PCM && (w.bits == 8 || w.bits == 16))
        val ima = w.tag == WavInfo.TAG_IMA && w.bits == 4 && w.blockAlign in (4 * w.channels)..MediaProfile.IMA_MAX_BLOCK_ALIGN &&
            w.blockAlign % (4 * w.channels) == 0 && w.samplesPerBlock == MediaProfile.imaSamplesPerBlock(w.blockAlign, w.channels)
        if (!pcm && !ima) why.add("Formato de audio ${wavTagName(w.tag)} de ${w.bits} bits: el P4 solo reproduce PCM 8/16 bits e IMA ADPCM.")
        if (w.channels > MediaProfile.AUDIO_MAX_CHANNELS) why.add("Más de 2 canales.")
        if (w.rate !in MediaProfile.AUDIO_RATE_MIN..MediaProfile.AUDIO_RATE_MAX) why.add("Frecuencia de ${w.rate} Hz fuera de ${MediaProfile.AUDIO_RATE_MIN}-${MediaProfile.AUDIO_RATE_MAX} Hz.")
        if (w.blockAlign <= 0) why.add("Bloque de audio no válido.")
        return decide(f, why, convertible = WavPcm.readable(w))
    }

    // ------------------------------------------------------------------ MP4 / MOV / HEIF
    private val HEIF_BRANDS = setOf("heic", "heix", "hevc", "hevx", "heim", "heis", "mif1", "msf1", "avif", "avis")

    private fun videoCodecName(fourcc: String) = when (fourcc.lowercase()) {
        "avc1", "avc3" -> "h264"; "hvc1", "hev1", "dvh1", "dvhe" -> "hevc"; "mp4v" -> "mpeg4"; "vp09" -> "vp9"; "vp08" -> "vp8"
        "av01" -> "av1"; "jpeg", "mjpa", "mjpb" -> "mjpeg"; "s263", "h263" -> "h263"
        "apch", "apcn", "apcs", "apco", "ap4h" -> "prores"
        else -> fourcc.trim().lowercase()
    }

    private fun audioCodecName(fourcc: String) = when (fourcc.lowercase()) {
        "mp4a" -> "aac"; "sowt" -> "pcm_s16le"; "twos" -> "pcm_s16be"; "alac" -> "alac"; "ac-3" -> "ac3"; "ec-3" -> "eac3"
        ".mp3" -> "mp3"; "samr" -> "amr_nb"; "sawb" -> "amr_wb"; "opus" -> "opus"; "fl32" -> "pcm_f32"; "in24" -> "pcm_s24"
        else -> fourcc.trim().lowercase()
    }

    private val DECODABLE_VIDEO = setOf("h264", "hevc", "mpeg4", "vp9", "vp8", "av1", "h263")
    private val DECODABLE_AUDIO = setOf("aac", "mp3", "opus", "amr_nb", "amr_wb", "vorbis", "flac")

    private fun isoBase(src: ByteSource): Analysis {
        val brand = Mp4.brandOf(src) ?: return corrupt(MediaKind.VIDEO, "mp4", "El archivo está dañado.")
        if (brand.trim() in HEIF_BRANDS) {
            val sz = Mp4.heifSize(src)
            val f = MediaFacts(MediaKind.PHOTO, brand.trim(), width = sz?.get(0), height = sz?.get(1))
            return Analysis(f, Plan.TRANSCODE, listOf("HEIC/AVIF: el P4 solo abre JPEG."))
        }
        val container = if (brand == "qt  ") "mov" else if (brand == "M4A " || brand == "M4B ") "m4a" else "mp4"
        val info = Mp4.parse(src) ?: return corrupt(MediaKind.VIDEO, container, "El archivo está dañado o cortado (no tiene el índice `moov`; ¿una grabación interrumpida?).")
        val v = info.video
        val a = info.audio
        if (v == null && a == null) return unsupported(MediaKind.OTHER, container, "El archivo no tiene pistas de audio ni de vídeo que se puedan reproducir.")
        if (v == null) return mp4Audio(a!!, container)
        val vc = videoCodecName(v.codec)
        val swap = v.rotation == 90 || v.rotation == 270
        val dispW = if (swap) v.height else v.width
        val dispH = if (swap) v.width else v.height
        val f = MediaFacts(MediaKind.VIDEO, container, videoCodec = vc, audioCodec = a?.let { audioCodecName(it.codec) },
            width = dispW.takeIf { it > 0 }, height = dispH.takeIf { it > 0 }, durationMs = v.durationMs, fpsX100 = v.fpsX100,
            sampleRate = a?.sampleRate?.takeIf { it > 0 }, channels = a?.channels?.takeIf { it > 0 }, rotation = v.rotation)
        if (v.sampleCount <= 0L) return Analysis(f, Plan.CORRUPT, listOf("El vídeo no tiene fotogramas."))
        val why = ArrayList<String>()
        if (vc == "mjpeg" && v.codec.lowercase() == "jpeg") {
            // JPEG por fotograma dentro de un MOV/MP4: si ya cabe en el perfil solo hay que cambiar de caja.
            if (v.rotation != 0) why.add("El vídeo está girado ${v.rotation}°: se endereza.")
            if (maxOf(dispW, dispH) > MediaProfile.VIDEO_MAX_SIDE) why.add("Es más grande de ${MediaProfile.VIDEO_MAX_SIDE} px.")
            if ((v.fpsX100 ?: 0) > MediaProfile.VIDEO_MAX_FPS * 100 + 5) why.add("Más de ${MediaProfile.VIDEO_MAX_FPS} fotogramas por segundo.")
            if (why.isEmpty()) return Analysis(f, Plan.REMUX, listOf("MJPEG dentro de ${container.uppercase()}: se pasa a AVI sin recodificar."))
            return Analysis(f, Plan.TRANSCODE, why)
        }
        if (vc == "mjpeg") return Analysis(f, Plan.TRANSCODE, listOf("MJPEG de tipo ${v.codec}: se recodifica a JPEG baseline."))
        if (vc !in DECODABLE_VIDEO) return unsupported(MediaKind.VIDEO, container, "Vídeo ${vc}: este teléfono no sabe decodificarlo.", f)
        why.add("Vídeo ${vc.uppercase()}: el P4 no tiene decodificador; se convierte a AVI MJPEG.")
        if (a != null) why.add("El P4 no reproduce el audio de un vídeo: el resultado no lleva sonido.")
        return Analysis(f, Plan.TRANSCODE, why)
    }

    private fun mp4Audio(a: Mp4Track, container: String): Analysis {
        val ac = audioCodecName(a.codec)
        val f = MediaFacts(MediaKind.AUDIO, container, audioCodec = ac, durationMs = a.durationMs, sampleRate = a.sampleRate.takeIf { it > 0 }, channels = a.channels.takeIf { it > 0 })
        if (ac == "pcm_s16le" && a.channels in 1..2 && a.sampleRate in MediaProfile.AUDIO_RATE_MIN..MediaProfile.AUDIO_RATE_MAX)
            return Analysis(f, Plan.REMUX, listOf("PCM de 16 bits dentro de ${container.uppercase()}: se pasa a WAV sin recodificar."))
        if (ac !in DECODABLE_AUDIO) return unsupported(MediaKind.AUDIO, container, "Audio ${ac}: este teléfono no sabe decodificarlo.", f)
        return Analysis(f, Plan.TRANSCODE, listOf("Audio ${ac.uppercase()}: el P4 solo reproduce WAV; se convierte."))
    }

    // ------------------------------------------------------------------ Matroska / WebM
    private fun matroska(src: ByteSource): Analysis {
        val m = Ebml.probe(src) ?: return corrupt(MediaKind.VIDEO, "mkv", "El archivo Matroska/WebM está dañado.")
        val container = if (m.docType == "webm") "webm" else "mkv"
        val vc = m.videoCodec?.let { ebmlVideoName(it) }
        val ac = m.audioCodec?.let { ebmlAudioName(it) }
        if (m.hasVideo) {
            val f = MediaFacts(MediaKind.VIDEO, container, videoCodec = vc, audioCodec = ac, width = m.width, height = m.height,
                durationMs = m.durationMs, fpsX100 = m.fpsX100, sampleRate = m.sampleRate, channels = m.channels)
            val why = arrayListOf("${container.uppercase()} (${vc ?: "vídeo"}): el P4 solo abre AVI MJPEG; se convierte.")
            if (ac != null) why.add("El P4 no reproduce el audio de un vídeo: el resultado no lleva sonido.")
            return Analysis(f, Plan.TRANSCODE, why)
        }
        val f = MediaFacts(MediaKind.AUDIO, container, audioCodec = ac, durationMs = m.durationMs, sampleRate = m.sampleRate, channels = m.channels)
        if (!m.hasAudio) return unsupported(MediaKind.OTHER, container, "El archivo no tiene pistas de audio ni de vídeo que se puedan reproducir.")
        return Analysis(f, Plan.TRANSCODE, listOf("Audio ${ac ?: ""} en ${container.uppercase()}: el P4 solo reproduce WAV; se convierte."))
    }

    private fun ebmlVideoName(id: String) = when {
        id.startsWith("V_MPEG4/ISO/AVC") -> "h264"; id.startsWith("V_MPEGH/ISO/HEVC") -> "hevc"; id == "V_VP8" -> "vp8"; id == "V_VP9" -> "vp9"
        id == "V_AV1" -> "av1"; id == "V_MJPEG" -> "mjpeg"; id.startsWith("V_MPEG4") -> "mpeg4"; else -> id.lowercase()
    }

    private fun ebmlAudioName(id: String) = when {
        id == "A_OPUS" -> "opus"; id == "A_VORBIS" -> "vorbis"; id.startsWith("A_AAC") -> "aac"; id == "A_FLAC" -> "flac"
        id.startsWith("A_MPEG/L3") -> "mp3"; id == "A_AC3" -> "ac3"; else -> id.lowercase()
    }

    // ------------------------------------------------------------------ audio suelto
    private fun flac(h: ByteArray): Analysis {
        if (h.size < 42 || h.u8(4) and 0x7F != 0) return corrupt(MediaKind.AUDIO, "flac", "El FLAC está dañado.")
        val rate = (h.u8(18) shl 12) or (h.u8(19) shl 4) or (h.u8(20) shr 4)
        val ch = ((h.u8(20) shr 1) and 7) + 1
        val total = ((h.u8(21) and 15).toLong() shl 32) or h.u32be(22)
        val f = MediaFacts(MediaKind.AUDIO, "flac", audioCodec = "flac", sampleRate = rate.takeIf { it > 0 }, channels = ch,
            durationMs = if (rate > 0 && total > 0) total * 1000 / rate else null)
        return Analysis(f, Plan.TRANSCODE, listOf("FLAC: el P4 solo reproduce WAV; se convierte."))
    }

    private fun ogg(src: ByteSource): Analysis {
        val h = src.head(0, 128)
        val opus = indexOf(h, "OpusHead".toByteArray())
        val vorbis = indexOf(h, "\u0001vorbis".toByteArray())
        return when {
            opus >= 0 && opus + 14 <= h.size -> Analysis(MediaFacts(MediaKind.AUDIO, "ogg", audioCodec = "opus", channels = h.u8(opus + 9), sampleRate = 48000),
                Plan.TRANSCODE, listOf("Opus: el P4 solo reproduce WAV; se convierte."))
            vorbis >= 0 && vorbis + 16 <= h.size -> Analysis(MediaFacts(MediaKind.AUDIO, "ogg", audioCodec = "vorbis", channels = h.u8(vorbis + 11), sampleRate = h.u32le(vorbis + 12).toInt()),
                Plan.TRANSCODE, listOf("Vorbis: el P4 solo reproduce WAV; se convierte."))
            else -> unsupported(MediaKind.AUDIO, "ogg", "Este OGG no lleva Vorbis ni Opus: no se puede convertir.")
        }
    }

    private fun indexOf(b: ByteArray, pat: ByteArray): Int {
        outer@ for (i in 0..b.size - pat.size) { for (k in pat.indices) if (b[i + k] != pat[k]) continue@outer; return i }
        return -1
    }

    private fun mpegAudioAt(b: ByteArray, p: Int): Boolean {
        if (p + 4 > b.size || b.u8(p) != 0xFF || b.u8(p + 1) and 0xE0 != 0xE0) return false
        val ver = (b.u8(p + 1) shr 3) and 3
        val layer = (b.u8(p + 1) shr 1) and 3
        return ver != 1 && layer != 0 && (b.u8(p + 2) shr 4) != 15 && (b.u8(p + 2) shr 4) != 0 && ((b.u8(p + 2) shr 2) and 3) != 3
    }

    private fun adts(h: ByteArray) = h.size >= 7 && h.u8(0) == 0xFF && h.u8(1) and 0xF6 == 0xF0 && ((h.u8(2) shr 2) and 15) < 13

    private fun mp3(src: ByteSource): Analysis {
        var p = 0
        val head = src.head(0, 8192)
        if (head.hasAt(0, "ID3") && head.size >= 10) {
            val sz = (head.u8(6) shl 21) or (head.u8(7) shl 14) or (head.u8(8) shl 7) or head.u8(9)
            p = 10 + sz
        }
        val b = if (p < 8192) head else src.head(p.toLong(), 8192).also { p = 0 }
        var q = p
        while (q + 4 <= b.size && !mpegAudioAt(b, q)) q++
        if (q + 4 > b.size) return corrupt(MediaKind.AUDIO, "mp3", "El MP3 está dañado: no se encuentra ningún fotograma de audio.")
        val ver = (b.u8(q + 1) shr 3) and 3
        val rates = intArrayOf(44100, 48000, 32000)
        val rate = when (ver) { 3 -> rates[(b.u8(q + 2) shr 2) and 3]; 2 -> rates[(b.u8(q + 2) shr 2) and 3] / 2; else -> rates[(b.u8(q + 2) shr 2) and 3] / 4 }
        val ch = if ((b.u8(q + 3) shr 6) == 3) 1 else 2
        // Duracion SOLO si el archivo trae su cabecera Xing/Info (numero de fotogramas): sin ella seria adivinar.
        val side = if (ver == 3) (if (ch == 1) 17 else 32) else (if (ch == 1) 9 else 17)
        val x = q + 4 + side
        var dur: Long? = null
        if (x + 12 <= b.size && (b.hasAt(x, "Xing") || b.hasAt(x, "Info")) && b.u32be(x + 4) and 1L != 0L) {
            val frames = b.u32be(x + 8)
            dur = frames * (if (ver == 3) 1152 else 576) * 1000 / rate
        }
        return Analysis(MediaFacts(MediaKind.AUDIO, "mp3", audioCodec = "mp3", sampleRate = rate, channels = ch, durationMs = dur),
            Plan.TRANSCODE, listOf("MP3: el P4 solo reproduce WAV; se convierte."))
    }

    private fun aac(h: ByteArray): Analysis {
        val rates = intArrayOf(96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350)
        val ch = ((h.u8(2) and 1) shl 2) or (h.u8(3) shr 6)
        return Analysis(MediaFacts(MediaKind.AUDIO, "aac", audioCodec = "aac", sampleRate = rates[(h.u8(2) shr 2) and 15], channels = ch.takeIf { it > 0 }),
            Plan.TRANSCODE, listOf("AAC: el P4 solo reproduce WAV; se convierte."))
    }
}

/** Sonda ligera de EBML (Matroska/WebM): solo las pistas y la duracion, en el primer MB. */
object Ebml {
    class Probe(
        val docType: String, val hasVideo: Boolean, val hasAudio: Boolean, val videoCodec: String?, val audioCodec: String?,
        val width: Int?, val height: Int?, val fpsX100: Int?, val durationMs: Long?, val sampleRate: Int?, val channels: Int?,
    )

    private class St {
        var docType = ""; var timecodeScale = 1_000_000L; var durationUnits: Double? = null
        var hasVideo = false; var hasAudio = false; var vCodec: String? = null; var aCodec: String? = null
        var w: Int? = null; var h: Int? = null; var defDur: Long? = null; var rate: Int? = null; var ch: Int? = null
        var trackType = 0; var curCodec: String? = null; var curW: Int? = null; var curH: Int? = null; var curDur: Long? = null
        var curRate: Int? = null; var curCh: Int? = null
    }

    fun probe(src: ByteSource): Probe? {
        val b = src.head(0, 1 shl 20)
        val st = St()
        var sawSegment = false
        var p = 0
        while (p < b.size) {
            val id = readId(b, p) ?: break
            val sz = readSize(b, p + id.second) ?: break
            val body = p + id.second + sz.second
            val end = if (sz.first < 0 || body + sz.first > b.size) b.size else (body + sz.first).toInt()
            when (id.first) {
                0x1A45DFA3L -> walk(b, body, end, st)
                0x18538067L -> { sawSegment = true; walk(b, body, end, st) }
            }
            if (id.first == 0x18538067L && sz.first < 0) break
            p = end
        }
        if (!sawSegment || st.docType.isEmpty()) return null
        val dur = st.durationUnits?.let { (it * st.timecodeScale / 1_000_000.0).toLong() }?.takeIf { it > 0 }
        return Probe(st.docType, st.hasVideo, st.hasAudio, st.vCodec, st.aCodec, st.w, st.h, st.defDur?.let { if (it > 0) (100_000_000_000L / it).toInt() else null }, dur, st.rate, st.ch)
    }

    private fun walk(b: ByteArray, from: Int, to: Int, st: St) {
        var p = from
        while (p < to) {
            val id = readId(b, p) ?: return
            val sz = readSize(b, p + id.second) ?: return
            val body = p + id.second + sz.second
            val end = if (sz.first < 0 || body + sz.first > to) to else (body + sz.first).toInt()
            if (body > to) return
            when (id.first) {
                0x1549A966L, 0x1654AE6BL, 0xE0L, 0xE1L -> walk(b, body, end, st)               // Info, Tracks, Video, Audio
                0xAEL -> {                                                                       // TrackEntry
                    st.trackType = 0; st.curCodec = null; st.curW = null; st.curH = null; st.curDur = null; st.curRate = null; st.curCh = null
                    walk(b, body, end, st)
                    if (st.trackType == 1 && !st.hasVideo) { st.hasVideo = true; st.vCodec = st.curCodec; st.w = st.curW; st.h = st.curH; st.defDur = st.curDur }
                    if (st.trackType == 2 && !st.hasAudio) { st.hasAudio = true; st.aCodec = st.curCodec; st.rate = st.curRate; st.ch = st.curCh }
                }
                0x4282L -> st.docType = String(b, body, (end - body).coerceAtLeast(0), Charsets.ISO_8859_1).trim('\u0000')
                0x2AD7B1L -> st.timecodeScale = uint(b, body, end)
                0x4489L -> st.durationUnits = float(b, body, end)
                0x83L -> st.trackType = uint(b, body, end).toInt()
                0x86L -> st.curCodec = String(b, body, (end - body).coerceAtLeast(0), Charsets.ISO_8859_1).trim('\u0000')
                0xB0L -> st.curW = uint(b, body, end).toInt()
                0xBAL -> st.curH = uint(b, body, end).toInt()
                0x23E383L -> st.curDur = uint(b, body, end)
                0xB5L -> st.curRate = float(b, body, end).toInt()
                0x9FL -> st.curCh = uint(b, body, end).toInt()
                0x1F43B675L -> return                                                            // Cluster: ya no hay cabeceras
            }
            p = end
        }
    }

    private fun uint(b: ByteArray, from: Int, to: Int): Long { var v = 0L; for (i in from until minOf(to, from + 8)) v = (v shl 8) or (b[i].toLong() and 0xFF); return v }
    private fun float(b: ByteArray, from: Int, to: Int): Double = when (to - from) {
        4 -> java.lang.Float.intBitsToFloat(uint(b, from, to).toInt()).toDouble()
        8 -> java.lang.Double.longBitsToDouble(uint(b, from, to))
        else -> 0.0
    }

    /** (id con sus bits de marca, longitud en bytes) */
    private fun readId(b: ByteArray, p: Int): Pair<Long, Int>? {
        if (p >= b.size) return null
        val first = b.u8(p)
        val len = when { first and 0x80 != 0 -> 1; first and 0x40 != 0 -> 2; first and 0x20 != 0 -> 3; first and 0x10 != 0 -> 4; else -> return null }
        if (p + len > b.size) return null
        var v = 0L
        for (i in 0 until len) v = (v shl 8) or b.u8(p + i).toLong()
        return v to len
    }

    /** (tamano o -1 si es desconocido, longitud en bytes) */
    private fun readSize(b: ByteArray, p: Int): Pair<Long, Int>? {
        if (p >= b.size) return null
        val first = b.u8(p)
        var len = 1
        var mask = 0x80
        while (len <= 8 && first and mask == 0) { len++; mask = mask shr 1 }
        if (len > 8 || p + len > b.size) return null
        var v = (first and (mask - 1)).toLong()
        var allOnes = (first and (mask - 1)) == mask - 1
        for (i in 1 until len) { val x = b.u8(p + i); v = (v shl 8) or x.toLong(); if (x != 0xFF) allOnes = false }
        return (if (allOnes) -1L else v) to len
    }
}
