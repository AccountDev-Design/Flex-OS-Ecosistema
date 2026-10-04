package com.flexos.flexphone.cloud.media

/**
 * EL PERFIL MULTIMEDIA DE FLEX OS ULTRA (v1).
 *
 * Lo que sigue NO es lo que el ESP32-P4 "podria" decodificar en teoria: es lo que el
 * firmware que hay en `FlexOS_Ultra/` decodifica de verdad y a un ritmo razonable (la cita de
 * cada valor esta en `docs/FLEX-MEDIA-PROFILE.md`). Hay dos cosas distintas:
 *
 *  · el SOBRE de compatibilidad (`*_MAX_*`, `*_ACCEPT_*`): hasta donde un archivo se reproduce
 *    TAL CUAL, sin tocarlo. Un archivo dentro del sobre no se convierte nunca;
 *  · el OBJETIVO de la conversion (`*_TARGET_*`): a lo que se convierte lo demas. Es el perfil
 *    `rec` de la web de Flex OS (`FX.PROFILES` en `webui/app.js`), el mismo que ya vigilan las
 *    pruebas contra el firmware (`tests/host/mediacheck`).
 *
 * El P4 no tiene decodificador de H.264/HEVC/VP9/AV1 ni de MP3/AAC/FLAC/OGG, ni de PNG/WebP/HEIC;
 * y su visor NO reproduce el audio de un AVI. Por eso el vídeo canonico es AVI MJPEG SIN pista de
 * audio, el audio canonico es WAV (IMA ADPCM) y la foto canonica es JPEG baseline.
 */
object MediaProfile {
    const val ID = "flexos-ultra-v1"

    // ---------------------------------------------------------------- JPEG (fotos y fotogramas)
    /** Techo del decodificador propio del P4 (FlexOS_JPEG.h). */
    const val JPEG_DECODER_MAX_SIDE = 4096
    /** Lo que el P4 trae de la nube para una foto (FlexOS_Cloud.cpp VIEW_MAX). */
    const val PHOTO_MAX_BYTES = 8L shl 20
    const val PHOTO_TARGET_SIDE = 1600
    const val PHOTO_TARGET_QUALITY = 85
    /** Una foto compatible mas pesada que esto (o de mas de 2400 px) tambien tiene vista previa ligera. */
    const val PHOTO_PREVIEW_MIN_BYTES = 1_500_000L
    const val PHOTO_PREVIEW_MIN_SIDE = 2400

    /** Miniatura: el P4 la decodifica a 132x132 con recorte central; y su respuesta JSON cabe en 48 KB. */
    const val THUMB_SIDE = 256
    const val THUMB_MAX_BYTES = 40 * 1024

    // ---------------------------------------------------------------- Vídeo (AVI MJPEG)
    /** Lado largo maximo para reproducirlo tal cual (la web llega a 800 en el perfil `high`). */
    const val VIDEO_MAX_SIDE = 800
    const val VIDEO_MAX_FPS = 15
    /** El P4 empieza con un buffer de 192 KB por fotograma y solo lo amplia si hace falta. */
    const val VIDEO_FRAME_MAX_BYTES = 192 * 1024
    const val VIDEO_TARGET_SIDE = 640
    const val VIDEO_TARGET_FPS = 12
    const val VIDEO_TARGET_QUALITY = 62
    const val VIDEO_DIM_MULTIPLE = 8
    const val VIDEO_MIN_SIDE = 16
    /** fccHandler que el demux del firmware acepta (sin distinguir mayusculas). */
    val AVI_FOURCC_ACCEPT = setOf("mjpg", "jpeg", "dmb1", "mjpa")

    // ---------------------------------------------------------------- Audio (WAV)
    /** I2S del P4: 8 a 96 kHz (el parseo acepta mas, pero la salida no). */
    const val AUDIO_RATE_MIN = 8000
    const val AUDIO_RATE_MAX = 96000
    const val AUDIO_MAX_CHANNELS = 2
    const val IMA_MAX_BLOCK_ALIGN = 8192
    const val AUDIO_TARGET_RATE = 22050
    const val AUDIO_TARGET_CHANNELS = 1

    /** Bloque tipico de Microsoft: 256 bytes por canal y por cada 11 kHz (igual que `FX.imaBlockAlign`). */
    fun imaBlockAlign(rate: Int, channels: Int): Int = 256 * channels * maxOf(1, rate / 11025)
    fun imaSamplesPerBlock(blockAlign: Int, channels: Int): Int = ((blockAlign - 4 * channels) * 8) / (4 * channels) + 1
}
