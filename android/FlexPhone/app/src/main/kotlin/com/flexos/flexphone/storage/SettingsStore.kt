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
 * Aqui NO hay ninguna credencial: las claves del enlace BLE las
 * gestiona el bonding del sistema, y desvincular las borra con
 * `removeBond`. Lo unico que se guarda del dispositivo es su
 * direccion y su nombre, para poder reconectar.
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
        val BOND_ADDR = stringPreferencesKey("bond_addr")
        val BOND_NAME = stringPreferencesKey("bond_name")
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
            bondedDeviceAddress = p[K.BOND_ADDR],
            bondedDeviceName = p[K.BOND_NAME],
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
                bondedDeviceAddress = p[K.BOND_ADDR],
                bondedDeviceName = p[K.BOND_NAME],
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
            if (n.bondedDeviceAddress != null) p[K.BOND_ADDR] = n.bondedDeviceAddress
            else p.remove(K.BOND_ADDR)
            if (n.bondedDeviceName != null) p[K.BOND_NAME] = n.bondedDeviceName
            else p.remove(K.BOND_NAME)
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
