// Dobles de androidx.core (Google Maven, no disponible aqui): solo las firmas que usa la app.
@file:Suppress("unused", "UNUSED_PARAMETER")
package androidx.core.app

import android.app.Notification
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.os.Bundle

class NotificationCompat {
    companion object { const val PRIORITY_LOW = -1 }
    open class Style
    class BigTextStyle : Style() { fun bigText(t: CharSequence?): BigTextStyle = this }
    class Builder(ctx: Context, channelId: String) {
        fun setSmallIcon(i: Int) = this
        fun setContentTitle(t: CharSequence?) = this
        fun setContentText(t: CharSequence?) = this
        fun setStyle(s: Style?) = this
        fun setContentIntent(p: PendingIntent?) = this
        fun setOngoing(b: Boolean) = this
        fun setSilent(b: Boolean) = this
        fun setPriority(p: Int) = this
        fun addAction(icon: Int, title: CharSequence?, p: PendingIntent?) = this
        fun build(): Notification = error("solo para comprobar tipos")
    }
}

class RemoteInput {
    class Builder(key: String) {
        fun setLabel(l: CharSequence?) = this
        fun setChoices(c: Array<CharSequence>?) = this
        fun setAllowFreeFormInput(b: Boolean) = this
        fun build(): RemoteInput = error("solo para comprobar tipos")
    }
    companion object {
        const val SOURCE_FREE_FORM_INPUT = 0
        fun addResultsToIntent(inputs: Array<RemoteInput>, intent: Intent, results: Bundle) {}
        fun setResultsSource(intent: Intent, source: Int) {}
    }
}
