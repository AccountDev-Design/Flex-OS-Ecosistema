package com.flexos.flexphone.ui.screens

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.navigation.NavController
import com.flexos.flexphone.domain.*
import com.flexos.flexphone.link.FlexLinkService
import com.flexos.flexphone.notifications.FlexNotificationListener
import com.flexos.flexphone.storage.SettingsStore
import com.flexos.flexphone.ui.FlexTopBar
import com.flexos.flexphone.ui.Routes
import kotlinx.coroutines.flow.MutableStateFlow

/** Estado del dispositivo conectado: la pantalla principal. */
@Composable
fun StatusScreen(nav: NavController, store: SettingsStore, settings: Settings) {
    val ctx = LocalContext.current
    val state = FlexPhoneState.instance
    val link by (state?.link ?: MutableStateFlow(LinkState.OFF)).collectAsState()
    val err by (state?.error ?: MutableStateFlow<String?>(null)).collectAsState()
    val media by (state?.media ?: MutableStateFlow(null)).collectAsState()
    val relay by (state?.relay ?: MutableStateFlow(RelayState.OFF)).collectAsState()

    val (label, color) = linkLabel(link)

    Column(Modifier.fillMaxSize()) {
        FlexTopBar("Flex Phone")
        Column(
            Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            SectionCard("Enlace") {
                KeyValue("Estado", label, emphasis = true)
                KeyValue("Dispositivo", settings.bondedDeviceName ?: "Ninguno")
                err?.let {
                    Text(it, style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.error)
                }
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    if (link == LinkState.OFF || link == LinkState.ERROR) {
                        Button(onClick = { FlexLinkService.start(ctx) }) { Text("Activar") }
                    } else {
                        OutlinedButton(onClick = { FlexLinkService.stop(ctx) }) { Text("Desactivar") }
                    }
                    if (!settings.isPaired) {
                        TextButton(onClick = { nav.navigate(Routes.PAIR) }) { Text("Emparejar") }
                    }
                }
            }

            if (!FlexNotificationListener.connected) {
                // Aviso REAL: sin este permiso la app no puede hacer su
                // trabajo, y hay que decirlo donde se ve.
                Card(colors = CardDefaults.cardColors(
                    containerColor = MaterialTheme.colorScheme.errorContainer
                )) {
                    Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                        Text("Sin acceso a notificaciones",
                            style = MaterialTheme.typography.titleMedium)
                        Text(
                            "Flex OS no recibira ninguna notificacion hasta que lo actives.",
                            style = MaterialTheme.typography.bodyMedium,
                        )
                        FilledTonalButton(onClick = { nav.navigate(Routes.WELCOME) }) {
                            Text("Revisar permisos")
                        }
                    }
                }
            }

            SectionCard("Multimedia") {
                val m = media
                if (m == null) {
                    // Sin sesion no se inventa una tarjeta.
                    Text("Nada reproduciendose",
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant)
                } else {
                    KeyValue("Titulo", m.title, emphasis = true)
                    if (m.artist.isNotEmpty()) KeyValue("Artista", m.artist)
                    KeyValue("App", m.app)
                }
            }

            SectionCard("Browser Relay") {
                KeyValue("Estado", when (relay) {
                    RelayState.UP -> "Activo"
                    RelayState.STARTING -> "Iniciando"
                    RelayState.ERROR -> "Error"
                    RelayState.SUSPENDED -> "Suspendido por Android"
                    RelayState.OFF -> "Detenido"
                })
                FilledTonalButton(onClick = { nav.navigate(Routes.RELAY) }) {
                    Text("Configurar")
                }
            }

            SectionCard("Ajustes") {
                TextButton(onClick = { nav.navigate(Routes.APPS) }) {
                    Text("Aplicaciones permitidas (${settings.allowedPackages.size})")
                }
                TextButton(onClick = { nav.navigate(Routes.PRIVACY) }) { Text("Privacidad") }
                TextButton(onClick = { nav.navigate(Routes.DEVICES) }) { Text("Dispositivos vinculados") }
                TextButton(onClick = { nav.navigate(Routes.DIAGNOSTICS) }) { Text("Diagnostico") }
                TextButton(onClick = { nav.navigate(Routes.ABOUT) }) { Text("Acerca de") }
            }
        }
    }
}
