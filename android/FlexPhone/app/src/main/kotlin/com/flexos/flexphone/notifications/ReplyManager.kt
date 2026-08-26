package com.flexos.flexphone.notifications

import android.app.Notification
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.os.Bundle
import android.util.Log
import androidx.core.app.RemoteInput
import com.flexos.flexphone.protocol.FlexLink
import com.flexos.flexphone.protocol.Limits
import com.flexos.flexphone.protocol.ReplyResult
import com.flexos.flexphone.protocol.Utf8

/**
 * Envio de respuestas rapidas.
 *
 * ESTA CLASE EXISTE PARA NO MENTIR
 * --------------------------------
 * Todo lo demas de Flex Phone puede fallar de forma visible. Esto no:
 * si aqui se devolviera "enviado" sin que Android hubiera aceptado la
 * accion, el usuario creeria haber contestado a alguien y no lo
 * habria hecho. Por eso:
 *
 *   · nunca se devuelve exito "por adelantado": se devuelve DESPUES
 *     de que `PendingIntent.send()` vuelva sin excepcion;
 *   · si la notificacion ya no existe -> E_GONE;
 *   · si no hay RemoteInput -> E_NOREPLY (y Flex OS no habria
 *     ofrecido el boton, porque `canReply` iba en false);
 *   · si el PendingIntent esta cancelado -> E_GONE, que es lo que de
 *     verdad ha pasado: la conversacion ya no esta ahi.
 *
 * Y UN LIMITE HONESTO: que Android acepte la accion significa que la
 * app de mensajeria RECIBIO el texto, no que el mensaje haya llegado
 * al destinatario. Eso ultimo no lo sabe nadie mas que la propia app.
 * La documentacion lo dice; la interfaz de Flex OS dice "entregado a
 * la app", no "leido".
 */
object ReplyManager {

    private const val TAG = "FlexPhone/Reply"

    /**
     * Intenta responder. Devuelve SIEMPRE un [ReplyResult] con el
     * codigo REAL; nunca lanza.
     */
    fun reply(ctx: Context, notifId: Long, actionIndex: Int, text: String): ReplyResult {
        val listener = FlexNotificationListener.current()
            ?: return fail(notifId, FlexLink.E_DENIED, "el lector de notificaciones no esta activo")

        if (text.isEmpty()) return fail(notifId, FlexLink.E_INTERNAL, "respuesta vacia")
        // Se recorta ANTES de enviar, en frontera UTF-8.
        val body = Utf8.clip(text, Limits.REPLY - 1)

        val action = listener.actionFor(notifId, actionIndex)
            ?: return fail(notifId, FlexLink.E_GONE, "la notificacion ya no existe")

        val inputs = action.remoteInputs
        if (inputs == null || inputs.isEmpty()) {
            // Esto solo puede pasar si la notificacion cambio entre
            // que se anuncio y que se contesto. Es un error REAL y se
            // dice tal cual.
            return fail(notifId, FlexLink.E_NOREPLY, "esta notificacion ya no admite respuesta")
        }

        val intent = action.actionIntent
            ?: return fail(notifId, FlexLink.E_GONE, "la accion ya no tiene destino")

        return try {
            // Se rellenan TODOS los RemoteInput declarados: algunas
            // apps declaran mas de uno y fallan si falta alguno.
            val results = Bundle()
            for (ri in inputs) results.putCharSequence(ri.resultKey, body)

            val fill = Intent()
            RemoteInput.addResultsToIntent(
                inputs.map { androidRemoteInputToAndroidX(it) }.toTypedArray(), fill, results
            )
            // Algunas apps exigen que se declare que la respuesta la
            // escribio una persona y no un asistente automatico.
            RemoteInput.setResultsSource(fill, RemoteInput.SOURCE_FREE_FORM_INPUT)

            intent.send(ctx, 0, fill)
            // Solo AQUI, y solo si send() no lanzo, se puede decir que
            // Android acepto la accion.
            Log.i(TAG, "respuesta entregada a la app (id=$notifId)")   // sin el texto
            ReplyResult(notifId, FlexLink.E_NONE)
        } catch (e: PendingIntent.CanceledException) {
            // La conversacion se cerro o la notificacion caduco entre
            // medias. Es exactamente E_GONE.
            fail(notifId, FlexLink.E_GONE, "la conversacion ya no esta disponible")
        } catch (e: SecurityException) {
            fail(notifId, FlexLink.E_DENIED, "Android denego la accion")
        } catch (e: Exception) {
            fail(notifId, FlexLink.E_INTERNAL, "no se pudo entregar la respuesta")
        }
    }

    /**
     * Dispara una accion SIN texto (por ejemplo "Marcar como leido").
     */
    fun action(ctx: Context, notifId: Long, actionIndex: Int): ReplyResult {
        val listener = FlexNotificationListener.current()
            ?: return fail(notifId, FlexLink.E_DENIED, "el lector no esta activo")
        val action = listener.actionFor(notifId, actionIndex)
            ?: return fail(notifId, FlexLink.E_GONE, "la accion ya no existe")
        val intent = action.actionIntent
            ?: return fail(notifId, FlexLink.E_GONE, "la accion ya no tiene destino")
        return try {
            intent.send()
            ReplyResult(notifId, FlexLink.E_NONE)
        } catch (e: PendingIntent.CanceledException) {
            fail(notifId, FlexLink.E_GONE, "la accion caduco")
        } catch (e: Exception) {
            fail(notifId, FlexLink.E_INTERNAL, "no se pudo ejecutar la accion")
        }
    }

    private fun fail(id: Long, code: Int, why: String): ReplyResult {
        // Se registra el MOTIVO, nunca el contenido.
        Log.w(TAG, "respuesta no enviada (id=$id): $why")
        return ReplyResult(id, code)
    }

    /**
     * Los `RemoteInput` del framework y los de AndroidX son tipos
     * distintos que describen lo mismo. AndroidX ofrece el helper que
     * rellena el Intent, asi que se traduce uno en otro conservando
     * la clave de resultado, que es lo unico que importa aqui.
     */
    private fun androidRemoteInputToAndroidX(ri: android.app.RemoteInput): RemoteInput =
        RemoteInput.Builder(ri.resultKey)
            .setLabel(ri.label)
            .setAllowFreeFormInput(ri.allowFreeFormInput)
            .apply { ri.choices?.let { setChoices(it) } }
            .build()
}

/**
 * Acceso a las acciones vivas. Vive aqui, como extension, para que
 * `liveActions` siga siendo privado del servicio: nadie mas puede
 * quedarse una referencia a un `PendingIntent` y usarla mas tarde.
 */
internal fun FlexNotificationListener.actionFor(
    notifId: Long, index: Int,
): Notification.Action? = liveActionsSnapshot(notifId)?.getOrNull(index)
