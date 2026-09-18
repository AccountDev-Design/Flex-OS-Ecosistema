package com.flexos.flexphone.notifications

import android.app.Notification
import android.content.pm.PackageManager
import android.os.Build
import android.service.notification.NotificationListenerService
import android.service.notification.StatusBarNotification
import android.util.Log
import com.flexos.flexphone.domain.FlexPhoneState
import com.flexos.flexphone.protocol.Category
import com.flexos.flexphone.protocol.Limits
import com.flexos.flexphone.protocol.NotifPayload
import com.flexos.flexphone.protocol.Priority
import com.flexos.flexphone.protocol.Utf8

/**
 * Lector de notificaciones de Android.
 *
 * QUE SE ENVIA AL P4 Y QUE NO
 * ---------------------------
 * Solo lo que Flex OS necesita para pintar la tarjeta y, si procede,
 * contestar: id, paquete, nombre de app, titulo, un RESUMEN del
 * texto, hora, categoria, prioridad y las etiquetas de las acciones.
 *
 * NO se envia:
 *   · el icono (seria un PNG por notificacion; el P4 tiene su propia
 *     tabla local de iconos por paquete),
 *   · el `Notification` entero ni sus extras,
 *   · nada de las apps que el usuario no haya permitido.
 *
 * Y NO SE ESCRIBE NADA EN EL LOG. Ni titulos, ni cuerpos, ni
 * remitentes. Los unicos `Log` de este fichero son contadores y
 * nombres de paquete, que es lo que hace falta para diagnosticar sin
 * volcar la vida privada del usuario en logcat.
 */
class FlexNotificationListener : NotificationListenerService() {

    companion object {
        private const val TAG = "FlexPhone/Notif"

        /** true si el servicio esta vivo y conectado a Android. */
        @Volatile var connected: Boolean = false
            private set

        /**
         * Instancia viva. Se necesita para responder: la accion de
         * RemoteInput solo se puede disparar desde el proceso que
         * tiene la notificacion.
         */
        @Volatile private var instance: FlexNotificationListener? = null
        fun current(): FlexNotificationListener? = instance

        /**
         * ¿Tiene la app el acceso especial a notificaciones?
         *
         * Se pregunta al ajuste del sistema y no a [connected]: el
         * servicio puede tardar en engancharse despues de conceder el
         * permiso, y en ese hueco `connected` diria false aunque el
         * permiso este. Flex OS decide con esto que funciones ofrece,
         * asi que un "no" temporal le haria esconder una seccion que
         * en realidad esta disponible.
         */
        fun hasAccess(ctx: android.content.Context): Boolean {
            val enabled = android.provider.Settings.Secure.getString(
                ctx.contentResolver, "enabled_notification_listeners",
            ) ?: return false
            return enabled.split(':').any {
                it.isNotBlank() &&
                    android.content.ComponentName.unflattenFromString(it)?.packageName == ctx.packageName
            }
        }

        /**
         * Descarta una notificacion en el TELEFONO. La llama el
         * enlace cuando el usuario la descarta en Flex OS: descartarla
         * en un lado y que siga en el otro es justo lo que hace que un
         * puente de notificaciones resulte molesto.
         *
         * Si Android ya la habia retirado, no pasa nada: la clave ya
         * no esta en la tabla y no se hace nada.
         */
        fun dismiss(id: Long) {
            val self = instance ?: return
            val key = self.keyOf(id) ?: return
            runCatching { self.cancelNotification(key) }
        }
    }

    /**
     * Acciones vivas, por id de notificacion.
     *
     * Se guardan SOLO mientras la notificacion existe: en cuanto
     * Android la retira, la entrada se borra en `onNotificationRemoved`.
     * Un `PendingIntent` de una notificacion que ya no esta falla al
     * dispararse, y esa es exactamente la situacion que Flex OS tiene
     * que ver como "la notificacion ya no existe" y no como un envio
     * silenciosamente perdido.
     */
    private val liveActions = HashMap<Long, Array<Notification.Action>>()

    /**
     * Clave de Android de cada notificacion viva, por id.
     *
     * Hace falta para descartar: `cancelNotification` trabaja con la
     * clave del sistema, no con el id estable que viaja por el
     * enlace. Se guarda en la MISMA seccion critica que las acciones
     * y se borra a la vez, para que no pueda quedar una clave de una
     * notificacion que ya no existe.
     */
    private val liveKeys = HashMap<Long, String>()

    /** Clave de Android de una notificacion viva, o null si ya no esta. */
    internal fun keyOf(notifId: Long): String? =
        synchronized(liveActions) { liveKeys[notifId] }

    /**
     * Copia de las acciones vivas de una notificacion, para
     * `ReplyManager`. Devuelve `null` si la notificacion ya no esta:
     * ese `null` es lo que acaba siendo un E_GONE honesto en Flex OS.
     */
    internal fun liveActionsSnapshot(notifId: Long): Array<Notification.Action>? =
        synchronized(liveActions) { liveActions[notifId] }

    override fun onListenerConnected() {
        super.onListenerConnected()
        connected = true
        instance = this
        Log.i(TAG, "lector conectado")
        // Al conectar se vuelca lo que ya hay en la barra: si el
        // telefono se emparejo con notificaciones ya presentes, el P4
        // las recibe igual en vez de empezar en blanco.
        runCatching { activeNotifications }.getOrNull()?.forEach { onNotificationPosted(it) }
    }

    override fun onListenerDisconnected() {
        super.onListenerDisconnected()
        connected = false
        instance = null
        synchronized(liveActions) { liveActions.clear(); liveKeys.clear() }
        Log.i(TAG, "lector desconectado")
    }

    override fun onNotificationPosted(sbn: StatusBarNotification?) {
        val n = sbn ?: return
        val state = FlexPhoneState.instance ?: return

        // 1) Filtro por app. Si el usuario no la permitio, ni se mira.
        if (!state.settings.isPackageAllowed(n.packageName)) return

        // 2) Se descarta el ruido que no aporta nada en la muneca:
        //    grupos resumen y notificaciones en curso del propio
        //    sistema (reproductores, descargas) salvo que sean media.
        val notif = n.notification ?: return
        if (notif.flags and Notification.FLAG_GROUP_SUMMARY != 0) return

        val payload = build(n) ?: return

        // 3) Solo se recuerdan las acciones si de verdad hay alguna.
        //    La clave del sistema SI se guarda siempre: hace falta para
        //    poder descartar la notificacion desde Flex OS, tenga o no
        //    acciones.
        synchronized(liveActions) {
            liveKeys[payload.id] = n.key
            notif.actions?.let { if (it.isNotEmpty()) liveActions[payload.id] = it }
        }

        state.onNotificationPosted(payload)
    }

    override fun onNotificationRemoved(sbn: StatusBarNotification?) {
        val n = sbn ?: return
        val id = stableId(n)
        // La accion muere con la notificacion: no se conserva un
        // PendingIntent caducado que luego mienta.
        synchronized(liveActions) { liveActions.remove(id); liveKeys.remove(id) }
        FlexPhoneState.instance?.onNotificationRemoved(id)
    }

    // -----------------------------------------------------------
    //  Construccion del mensaje
    // -----------------------------------------------------------
    private fun build(sbn: StatusBarNotification): NotifPayload? {
        val notif = sbn.notification ?: return null
        val extras = notif.extras ?: return null
        val state = FlexPhoneState.instance ?: return null

        val title = extras.getCharSequence(Notification.EXTRA_TITLE)?.toString().orEmpty()
        val rawText = (extras.getCharSequence(Notification.EXTRA_TEXT)
            ?: extras.getCharSequence(Notification.EXTRA_BIG_TEXT))?.toString().orEmpty()

        val sensitive = isSensitive(notif, extras, rawText)
        // PRIVACIDAD: si el usuario pidio ocultar el contenido, o la
        // notificacion es sensible, el cuerpo NI SIQUIERA SALE del
        // telefono. No se manda para que el P4 lo esconda: se manda
        // vacio con el bit `hidden`, que es lo unico que no se puede
        // filtrar despues.
        val hide = state.settings.hideContent || (sensitive && state.settings.hideSensitive)
        val text = if (hide) "" else Utf8.clip(rawText, Limits.TEXT - 1)

        val actions = notif.actions ?: emptyArray()
        // -- LA REGLA DE REMOTE INPUT --
        // Solo se declara `canReply` si existe una accion con al menos
        // un RemoteInput. Es lo unico que autoriza a Flex OS a pintar
        // el boton "Responder". Sin esto, el boton existiria y no
        // haria nada.
        var replyIdx = -1
        for (i in actions.indices) {
            val ri = actions[i].remoteInputs
            if (ri != null && ri.isNotEmpty()) { replyIdx = i; break }
        }

        return NotifPayload(
            id = stableId(sbn),
            pkg = Utf8.clip(sbn.packageName, Limits.PKG - 1),
            app = Utf8.clip(appLabel(sbn.packageName), Limits.APPNAME - 1),
            // El TITULO se manda siempre (es el remitente): Flex OS
            // tiene que poder ensenar "WhatsApp · Ana" en la pantalla
            // de bloqueo aunque el cuerpo vaya oculto. Lo que se
            // suprime al ocultar es el CUERPO, arriba.
            title = Utf8.clip(title, Limits.TITLE - 1),
            text = text,
            whenMs = notif.`when`.let { if (it > 0) it / 1000 else sbn.postTime / 1000 },
            category = mapCategory(notif.category),
            priority = mapPriority(sbn, notif),
            hidden = hide,
            sensitive = sensitive,
            canReply = replyIdx >= 0,
            replyAction = if (replyIdx >= 0) replyIdx else 0,
            actions = actions.take(Limits.ACTIONS).map {
                Utf8.clip(it.title?.toString().orEmpty(), Limits.ACTLABEL - 1)
            },
        )
    }

    /**
     * Id ESTABLE de una notificacion.
     *
     * Tiene que sobrevivir a las actualizaciones (WhatsApp reusa la
     * misma notificacion al llegar otro mensaje del mismo chat) para
     * que Flex OS la ACTUALICE en su sitio en vez de acumular
     * duplicados. La clave de Android ya cumple eso; aqui solo se
     * reduce a 32 bits, que es lo que viaja por el enlace.
     */
    private fun stableId(sbn: StatusBarNotification): Long {
        val key = sbn.key ?: "${sbn.packageName}:${sbn.id}:${sbn.tag}"
        return (key.hashCode().toLong() and 0xFFFFFFFFL)
    }

    private val labelCache = HashMap<String, String>()
    private fun appLabel(pkg: String): String = labelCache.getOrPut(pkg) {
        runCatching {
            val pm: PackageManager = packageManager
            pm.getApplicationLabel(pm.getApplicationInfo(pkg, 0)).toString()
        }.getOrDefault(pkg)
    }

    private fun mapCategory(c: String?): Int = when (c) {
        Notification.CATEGORY_MESSAGE -> Category.MSG
        Notification.CATEGORY_CALL -> Category.CALL
        Notification.CATEGORY_EMAIL -> Category.EMAIL
        Notification.CATEGORY_SOCIAL -> Category.SOCIAL
        Notification.CATEGORY_ALARM, Notification.CATEGORY_REMINDER -> Category.ALARM
        Notification.CATEGORY_TRANSPORT -> Category.TRANSPORT
        Notification.CATEGORY_SYSTEM, Notification.CATEGORY_SERVICE -> Category.SYS
        else -> Category.OTHER
    }

    private fun mapPriority(sbn: StatusBarNotification, notif: Notification): Int {
        // A partir de Android 8 la importancia real la da el canal, no
        // el campo `priority` (que esta obsoleto).
        val rank = runCatching {
            val r = android.service.notification.NotificationListenerService.Ranking()
            if (currentRanking?.getRanking(sbn.key, r) == true) r.importance else null
        }.getOrNull()
        if (rank != null) {
            return when {
                rank >= android.app.NotificationManager.IMPORTANCE_HIGH -> Priority.HIGH
                rank == android.app.NotificationManager.IMPORTANCE_DEFAULT -> Priority.DEFAULT
                rank == android.app.NotificationManager.IMPORTANCE_LOW -> Priority.LOW
                else -> Priority.MIN
            }
        }
        @Suppress("DEPRECATION")
        return when {
            notif.priority >= Notification.PRIORITY_HIGH -> Priority.HIGH
            notif.priority <= Notification.PRIORITY_LOW -> Priority.LOW
            else -> Priority.DEFAULT
        }
    }

    /**
     * Deteccion de contenido sensible.
     *
     * Es deliberadamente CONSERVADORA: ante la duda, marca sensible.
     * Equivocarse marcando de mas solo esconde un texto; equivocarse
     * al reves ensena un codigo de un solo uso en una pantalla que
     * puede estar sobre una mesa.
     */
    private fun isSensitive(notif: Notification, extras: android.os.Bundle, text: String): Boolean {
        // 1) Lo que el propio Android ya marca como privado.
        if (notif.visibility == Notification.VISIBILITY_SECRET) return true
        if (notif.visibility == Notification.VISIBILITY_PRIVATE) return true
        // 2) Categoria que Android reserva para credenciales (API 33+).
        if (Build.VERSION.SDK_INT >= 33 && notif.category == "cred") return true
        // 3) Heuristica sobre el texto: patrones de codigo de un solo
        //    uso. No se registra el texto en ningun sitio.
        if (OTP_HINT.containsMatchIn(text)) return true
        return false
    }
}

/**
 * Pistas de "codigo de un solo uso" en varios idiomas, junto a un
 * numero de 4 a 8 cifras. No pretende ser exhaustivo: es una red de
 * seguridad razonable, y el usuario siempre puede desactivar la app
 * entera desde la pantalla de aplicaciones permitidas.
 */
private val OTP_HINT = Regex(
    """(?i)\b(otp|c[oó]digo|codigo|code|verificaci[oó]n|verification|token|pin|2fa|contrase[nñ]a|password)\b[^\d]{0,20}\d{4,8}"""
)
