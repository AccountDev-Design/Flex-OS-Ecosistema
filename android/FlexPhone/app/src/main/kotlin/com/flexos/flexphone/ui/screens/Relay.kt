package com.flexos.flexphone.ui.screens

import android.content.Intent
import android.net.Uri
import android.os.PowerManager
import android.provider.Settings as AndroidSettings
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.navigation.NavController
import com.flexos.flexphone.domain.FlexPhoneState
import com.flexos.flexphone.domain.RelayState
import com.flexos.flexphone.domain.Settings
import com.flexos.flexphone.relay.BrowserRelayService
import com.flexos.flexphone.storage.SettingsStore
import com.flexos.flexphone.ui.FlexTopBar
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.launch

/**
 * Browser Relay.
 *
 * Esta pantalla dice la VERDAD sobre la pantalla apagada. Android
 * puede matar el proceso por bateria, memoria o politica del
 * fabricante, y prometer lo contrario solo genera desconfianza
 * cuando falla.
 */
@Composable
fun RelayScreen(nav: NavController, store: SettingsStore, settings: Settings) {
    val ctx = LocalContext.current
    val scope = rememberCoroutineScope()
    val state = FlexPhoneState.instance
    val relay by (state?.relay ?: MutableStateFlow(RelayState.OFF)).collectAsState()
    val info by (state?.relayInfo ?: MutableStateFlow(null)).collectAsState()

    val pm = ctx.getSystemService(PowerManager::class.java)
    var ignoringBattery by remember {
        mutableStateOf(runCatching { pm.isIgnoringBatteryOptimizations(ctx.packageName) }
            .getOrDefault(false))
    }

    Column(Modifier.fillMaxSize()) {
        FlexTopBar("Browser Relay", onBack = { nav.popBackStack() })
        Column(
            Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            SectionCard("Estado") {
                KeyValue("Relay", when (relay) {
                    RelayState.UP -> "Activo"
                    RelayState.STARTING -> "Iniciando"
                    RelayState.ERROR -> "Error"
                    RelayState.SUSPENDED -> "Suspendido por Android"
                    RelayState.OFF -> "Detenido"
                }, emphasis = true)
                info?.let { i ->
                    if (i.port > 0) {
                        KeyValue("Direccion",
                            i.ip.joinToString(".") { (it.toInt() and 0xFF).toString() } + ":${i.port}")
                    }
                    // Se dice que NO hay TLS. No se llama "seguro" a
                    // algo que va en claro por la red local.
                    KeyValue("Cifrado", if (i.tls) "TLS" else "Sin TLS (red local)")
                    if (i.error.isNotEmpty()) {
                        Text(i.error, style = MaterialTheme.typography.bodyMedium,
                            color = MaterialTheme.colorScheme.error)
                    }
                }
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    if (relay == RelayState.OFF || relay == RelayState.ERROR) {
                        Button(onClick = { BrowserRelayService.start(ctx) }) { Text("Iniciar") }
                    } else {
                        // Boton REAL para pararlo.
                        OutlinedButton(onClick = { BrowserRelayService.stop(ctx) }) { Text("Detener") }
                    }
                }
            }

            if (!ignoringBattery) {
                Card(colors = CardDefaults.cardColors(
                    containerColor = MaterialTheme.colorScheme.tertiaryContainer
                )) {
                    Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                        Text("Ahorro de bateria activo",
                            style = MaterialTheme.typography.titleMedium)
                        Text(
                            "Con el ahorro de bateria puesto, Android puede detener el " +
                            "relay en cuanto apagues la pantalla. Para que aguante, " +
                            "excluye Flex Phone de la optimizacion.",
                            style = MaterialTheme.typography.bodyMedium,
                        )
                        FilledTonalButton(onClick = {
                            // Se abre la pantalla de AJUSTES, no se pide
                            // el permiso agresivo de exclusion directa:
                            // Google lo restringe y ademas la decision
                            // es del usuario.
                            runCatching {
                                ctx.startActivity(
                                    Intent(AndroidSettings.ACTION_IGNORE_BATTERY_OPTIMIZATION_SETTINGS)
                                )
                            }
                        }) { Text("Abrir ajustes de bateria") }
                    }
                }
            }

            SectionCard("Limites reales") {
                Text(
                    "El navegador se ejecuta en este telefono. Con la pantalla " +
                    "apagada seguira funcionando mientras Android lo permita, pero " +
                    "no se puede garantizar indefinidamente: algunos fabricantes " +
                    "cierran los procesos en segundo plano por su cuenta.\n\n" +
                    "Si Android detiene el relay, Flex OS lo mostrara como un error " +
                    "en pantalla en vez de quedarse esperando.\n\n" +
                    "No se prometen 60 imagenes por segundo: una pagina remota " +
                    "comprimida y enviada por Wi-Fi a un ESP32 no da eso. El ritmo " +
                    "sube mientras tocas y baja cuando la pagina no cambia.",
                    style = MaterialTheme.typography.bodyMedium,
                )
            }

            SectionCard("Ajustes del relay") {
                KeyValue("Puerto", if (settings.relayPort == 0) "automatico"
                                   else settings.relayPort.toString())
                KeyValue("Cerrar tras inactividad", "${settings.relayIdleTimeoutMin} min")
                KeyValue("Maximo de pestanas", settings.relayMaxTabs.toString())
                KeyValue("Calidad JPEG", settings.relayQuality.toString())
                Text(
                    "Cada pestana es un navegador vivo en memoria. Subir el maximo " +
                    "aumenta la posibilidad de que Android cierre la app.",
                    style = MaterialTheme.typography.labelMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    TextButton(onClick = {
                        scope.launch {
                            store.update {
                                it.copy(relayMaxTabs = (it.relayMaxTabs % 6) + 1)
                            }
                        }
                    }) { Text("Cambiar pestanas") }
                    TextButton(onClick = {
                        scope.launch {
                            store.update {
                                val q = if (it.relayQuality >= 85) 25 else it.relayQuality + 10
                                it.copy(relayQuality = q)
                            }
                        }
                    }) { Text("Cambiar calidad") }
                    TextButton(onClick = {
                        scope.launch {
                            store.update {
                                val t = if (it.relayIdleTimeoutMin >= 60) 1
                                        else it.relayIdleTimeoutMin + 5
                                it.copy(relayIdleTimeoutMin = t)
                            }
                        }
                    }) { Text("Cambiar tiempo") }
                }
            }
        }
    }
}
