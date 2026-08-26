package com.flexos.flexphone.media

import android.content.ComponentName
import android.content.Context
import android.media.session.MediaController
import android.media.session.MediaSessionManager
import android.media.session.PlaybackState
import com.flexos.flexphone.domain.FlexPhoneState
import com.flexos.flexphone.notifications.FlexNotificationListener
import com.flexos.flexphone.protocol.Limits
import com.flexos.flexphone.protocol.MediaCmd
import com.flexos.flexphone.protocol.MediaState
import com.flexos.flexphone.protocol.MediaStatus
import com.flexos.flexphone.protocol.Utf8

/**
 * Control multimedia.
 *
 * COMO SE OBTIENE EL PERMISO
 * --------------------------
 * `MediaSessionManager.getActiveSessions` exige un
 * NotificationListenerService autorizado -- el mismo que Flex Phone
 * ya usa para las notificaciones. Es la via OFICIAL: no se usa
 * ninguna API privada ni se toca la reproduccion de nadie.
 *
 * LO QUE NO HACE
 *   · no se apropia del control: se OBSERVA la sesion que ya esta
 *     activa y se le mandan ordenes de transporte, nada mas;
 *   · no pide foco de audio, asi que no puede interrumpir lo que
 *     este sonando;
 *   · NO se envia la caratula. Una portada son decenas de KB y el
 *     enlace es BLE: mandarla dejaria el resto del protocolo sin
 *     ancho de banda durante segundos.
 */
class MediaBridge(
    private val ctx: Context,
    private val state: FlexPhoneState,
) {
    private var manager: MediaSessionManager? = null
    private var controller: MediaController? = null

    private val listener = MediaSessionManager.OnActiveSessionsChangedListener { list ->
        rebind(list)
    }

    private val callback = object : MediaController.Callback() {
        override fun onPlaybackStateChanged(s: PlaybackState?) = publish()
        override fun onMetadataChanged(m: android.media.MediaMetadata?) = publish()
        override fun onSessionDestroyed() {
            controller = null
            // La sesion murio: se dice que no hay nada, en vez de
            // dejar la ultima cancion como si siguiera sonando.
            state.setMedia(null)
        }
    }

    fun start() {
        if (!FlexNotificationListener.connected) return    // sin permiso no hay sesiones
        val comp = ComponentName(ctx, FlexNotificationListener::class.java)
        manager = ctx.getSystemService(Context.MEDIA_SESSION_SERVICE) as? MediaSessionManager
        runCatching {
            manager?.addOnActiveSessionsChangedListener(listener, comp)
            rebind(manager?.getActiveSessions(comp))
        }
    }

    fun stop() {
        runCatching { manager?.removeOnActiveSessionsChangedListener(listener) }
        runCatching { controller?.unregisterCallback(callback) }
        controller = null; manager = null
    }

    /**
     * Elige UNA sesion cuando hay varias.
     *
     * Criterio: la que este REPRODUCIENDO. Si ninguna lo esta, la
     * primera de la lista (Android las da por prioridad). Sin este
     * criterio, con Spotify y un video abiertos a la vez, los botones
     * del reloj controlarian una sesion al azar.
     */
    private fun rebind(sessions: List<MediaController>?) {
        runCatching { controller?.unregisterCallback(callback) }
        val list = sessions.orEmpty()
        val chosen = list.firstOrNull { it.playbackState?.state == PlaybackState.STATE_PLAYING }
            ?: list.firstOrNull()
        controller = chosen?.also { it.registerCallback(callback) }
        publish()
    }

    private fun publish() {
        val c = controller
        if (c == null) { state.setMedia(null); return }
        val md = c.metadata
        val title = md?.getString(android.media.MediaMetadata.METADATA_KEY_TITLE).orEmpty()
        // Sin titulo no hay nada util que ensenar: se informa de que
        // no hay sesion en vez de mandar una tarjeta vacia.
        if (title.isBlank()) { state.setMedia(null); return }
        val artist = (md?.getString(android.media.MediaMetadata.METADATA_KEY_ARTIST)
            ?: md?.getString(android.media.MediaMetadata.METADATA_KEY_ALBUM_ARTIST)).orEmpty()
        val status = when (c.playbackState?.state) {
            PlaybackState.STATE_PLAYING -> MediaStatus.PLAYING
            PlaybackState.STATE_PAUSED -> MediaStatus.PAUSED
            else -> MediaStatus.STOP
        }
        state.setMedia(
            MediaState(
                title = Utf8.clip(title, Limits.MEDIA_TXT - 1),
                artist = Utf8.clip(artist, Limits.MEDIA_TXT - 1),
                app = Utf8.clip(appLabel(c.packageName), Limits.APPNAME - 1),
                status = status,
            )
        )
    }

    /** Ejecuta una orden de transporte. Silencioso si no hay sesion. */
    fun command(cmd: Int) {
        val t = controller?.transportControls ?: return
        runCatching {
            when (cmd) {
                MediaCmd.PLAY -> t.play()
                MediaCmd.PAUSE -> t.pause()
                MediaCmd.NEXT -> t.skipToNext()
                MediaCmd.PREV -> t.skipToPrevious()
                else -> Unit
            }
        }
    }

    private fun appLabel(pkg: String?): String {
        if (pkg == null) return ""
        return runCatching {
            val pm = ctx.packageManager
            pm.getApplicationLabel(pm.getApplicationInfo(pkg, 0)).toString()
        }.getOrDefault(pkg)
    }
}
