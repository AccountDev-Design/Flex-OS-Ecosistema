@file:Suppress("unused", "UNUSED_PARAMETER")
package com.flexos.flexphone.storage

import android.content.Context
import com.flexos.flexphone.domain.Settings
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.emptyFlow

/** SettingsStore usa DataStore (Google Maven): aqui solo su superficie. */
class SettingsStore(ctx: Context) {
    val flow: Flow<Settings> = emptyFlow()
    suspend fun update(block: (Settings) -> Settings) {}
    suspend fun wipe() {}
}
