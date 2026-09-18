package com.flexos.flexphone.storage

import android.content.Context
import androidx.datastore.preferences.core.*
import androidx.datastore.preferences.preferencesDataStore
import com.flexos.flexphone.domain.Settings
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.map

/**
 * Persistencia de los ajustes.
 *
 * Se guarda con DataStore, en el almacenamiento PRIVADO de la app.
 *
 * AQUI NO HAY NINGUNA CREDENCIAL. La clave del vinculo vive en
 * [BondStore], envuelta con una clave del Android Keystore, porque es
 * lo unico que impide que un equipo cualquiera de la red abra sesion.
 * De el dispositivo vinculado solo se guarda aqui lo que se ensena en
 * pantalla: su identificador y su nombre.
 */
private val Context.dataStore by preferencesDataStore(name = "flexphone")

class SettingsStore(private val ctx: Context) {

    private object K {
        val ALLOWED = stringSetPreferencesKey("allowed_packages")
        val HIDE_CONTENT = booleanPreferencesKey("hide_content")
        val HIDE_SENSITIVE = booleanPreferencesKey("hide_sensitive")
        val START_ON_BOOT = booleanPreferencesKey("start_on_boot")
        val RELAY_PORT = intPreferencesKey("relay_port")
        val RELAY_IDLE = intPreferencesKey("relay_idle_min")
        val RELAY_TABS = intPreferencesKey("relay_max_tabs")
        val RELAY_QUALITY = intPreferencesKey("relay_quality")
        val FLEXOS_ID = stringPreferencesKey("flexos_id")
        val FLEXOS_NAME = stringPreferencesKey("flexos_name")
        val FIXED_HOST = stringPreferencesKey("fixed_host")
    }

    val flow: Flow<Settings> = ctx.dataStore.data.map { p ->
        Settings(
            allowedPackages = p[K.ALLOWED] ?: emptySet(),
            hideContent = p[K.HIDE_CONTENT] ?: false,
            hideSensitive = p[K.HIDE_SENSITIVE] ?: true,
            startOnBoot = p[K.START_ON_BOOT] ?: false,
            relayPort = p[K.RELAY_PORT] ?: 0,
            relayIdleTimeoutMin = p[K.RELAY_IDLE] ?: 10,
            relayMaxTabs = p[K.RELAY_TABS] ?: 3,
            relayQuality = p[K.RELAY_QUALITY] ?: 62,
            flexosId = p[K.FLEXOS_ID],
            flexosName = p[K.FLEXOS_NAME],
            fixedHost = p[K.FIXED_HOST] ?: "",
        )
    }

    suspend fun update(block: (Settings) -> Settings) {
        ctx.dataStore.edit { p ->
            val cur = Settings(
                allowedPackages = p[K.ALLOWED] ?: emptySet(),
                hideContent = p[K.HIDE_CONTENT] ?: false,
                hideSensitive = p[K.HIDE_SENSITIVE] ?: true,
                startOnBoot = p[K.START_ON_BOOT] ?: false,
                relayPort = p[K.RELAY_PORT] ?: 0,
                relayIdleTimeoutMin = p[K.RELAY_IDLE] ?: 10,
                relayMaxTabs = p[K.RELAY_TABS] ?: 3,
                relayQuality = p[K.RELAY_QUALITY] ?: 62,
                flexosId = p[K.FLEXOS_ID],
                flexosName = p[K.FLEXOS_NAME],
                fixedHost = p[K.FIXED_HOST] ?: "",
            )
            val n = block(cur)
            p[K.ALLOWED] = n.allowedPackages
            p[K.HIDE_CONTENT] = n.hideContent
            p[K.HIDE_SENSITIVE] = n.hideSensitive
            p[K.START_ON_BOOT] = n.startOnBoot
            p[K.RELAY_PORT] = n.relayPort
            p[K.RELAY_IDLE] = n.relayIdleTimeoutMin
            p[K.RELAY_TABS] = n.relayMaxTabs
            p[K.RELAY_QUALITY] = n.relayQuality
            p[K.FIXED_HOST] = n.fixedHost
            if (n.flexosId != null) p[K.FLEXOS_ID] = n.flexosId else p.remove(K.FLEXOS_ID)
            if (n.flexosName != null) p[K.FLEXOS_NAME] = n.flexosName else p.remove(K.FLEXOS_NAME)
        }
    }

    /**
     * Borrado total al desvincular. Se va TODO lo que venga del
     * vinculo: dispositivo, apps permitidas y ajustes del relay.
     */
    suspend fun wipe() {
        ctx.dataStore.edit { it.clear() }
    }
}
