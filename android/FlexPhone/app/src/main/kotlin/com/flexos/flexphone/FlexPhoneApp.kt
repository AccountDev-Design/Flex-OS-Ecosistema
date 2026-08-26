package com.flexos.flexphone

import android.app.Application
import com.flexos.flexphone.domain.FlexPhoneState
import com.flexos.flexphone.storage.SettingsStore
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.launch

/**
 * Punto de entrada del proceso. Crea el estado unico y lo mantiene
 * sincronizado con los ajustes persistidos.
 *
 * NO arranca ningun servicio aqui. Abrir la app no debe encender la
 * radio ni levantar un servicio en primer plano: eso lo decide el
 * usuario, y se nota en la bateria.
 */
class FlexPhoneApp : Application() {
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)

    override fun onCreate() {
        super.onCreate()
        val store = SettingsStore(this)
        val state = FlexPhoneState()
        FlexPhoneState.instance = state
        scope.launch { store.flow.collectLatest { state.settings = it } }
    }
}
