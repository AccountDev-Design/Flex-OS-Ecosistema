package com.flexos.flexphone.link

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import com.flexos.flexphone.storage.SettingsStore
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch

/**
 * Reanudacion tras reiniciar el telefono.
 *
 * SOLO actua si el usuario marco la opcion Y hay un dispositivo
 * vinculado. Arrancar un servicio en primer plano en cada arranque
 * sin que nadie lo haya pedido es exactamente lo que hace que la
 * gente vaya a Ajustes a restringir la app.
 */
class BootReceiver : BroadcastReceiver() {
    override fun onReceive(ctx: Context, intent: Intent) {
        if (intent.action != Intent.ACTION_BOOT_COMPLETED) return
        val pending = goAsync()
        CoroutineScope(Dispatchers.IO).launch {
            try {
                val s = SettingsStore(ctx).flow.first()
                if (s.startOnBoot && s.isPaired) FlexLinkService.start(ctx)
            } finally {
                pending.finish()
            }
        }
    }
}
