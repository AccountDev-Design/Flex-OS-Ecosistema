package com.flexos.flexphone.ui.screens

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.navigation.NavController
import com.flexos.flexphone.device.DeviceAdapter
import com.flexos.flexphone.domain.FlexPhoneState
import com.flexos.flexphone.domain.LinkState
import com.flexos.flexphone.domain.RelayState
import com.flexos.flexphone.domain.Settings
import com.flexos.flexphone.link.FlexLinkService
import com.flexos.flexphone.notifications.FlexNotificationListener
import com.flexos.flexphone.storage.BondStore
import com.flexos.flexphone.storage.SettingsStore
import com.flexos.flexphone.ui.FlexTopBar
import com.flexos.flexphone.ui.Routes
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow

/**
 * PORTADA DE FLEX PHONE EN ANDROID.
 *
 * Es el espejo de la portada del P4: lo importante a la vista (que
 * Flex OS esta vinculado, si hay sesion, bateria, red y el estado del
 * servidor) y las secciones debajo.
 *
 * REGLA QUE ATRAVIESA LA PANTALLA: nada se pinta "por si acaso". Si
 * un dato no ha llegado o una capacidad no esta concedida, se dice
 * -- no se rellena con un valor plausible.
 */
@Composable
fun HomeScreen(nav: NavController, store: SettingsStore, settings: Settings) {
    val ctx = LocalContext.current
    val state = FlexPhoneState.instance
    val link by (state?.link ?: MutableStateFlow(LinkState.OFF)).collectAsState()
    val relay by (state?.relay ?: MutableStateFlow(RelayState.OFF)).collectAsState()
    val error by (state?.error ?: MutableStateFlow<String?>(null)).collectAsState()

    val device = remember { DeviceAdapter(ctx) }
    val bonds = remember { BondStore(ctx) }

    // El estado del telefono se refresca cada pocos segundos mientras
    // la pantalla esta a la vista. NO es sondeo del enlace: son
    // lecturas locales, y solo mientras alguien las esta mirando.
    var battery by remember { mutableStateOf(device.batteryPercent()) }
    var charging by remember { mutableStateOf(device.isCharging()) }
    var onWifi by remember { mutableStateOf(device.isOnWifi()) }
    LaunchedEffect(Unit) {
        while (true) {
            battery = device.batteryPercent()
            charging = device.isCharging()
            onWifi = device.isOnWifi()
            delay(5_000)
        }
    }

    val notifAccess = remember(link) { FlexNotificationListener.hasAccess(ctx) }
    val service = FlexLinkService.current

    Scaffold(topBar = { FlexTopBar("Flex Phone") }) { pad ->
        Column(
            Modifier.padding(pad).fillMaxSize().verticalScroll(rememberScrollState())
                .padding(horizontal = 16.dp, vertical = 8.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            // ---- Tarjeta del enlace ----
            GlassCard {
                Text(
                    bonds.peerName()?.takeIf { it.isNotBlank() }
                        ?: bonds.peerId()?.let { "Flex OS Ultra" }
                        ?: "Sin Flex OS vinculado",
                    style = MaterialTheme.typography.titleLarge,
                    maxLines = 1, overflow = TextOverflow.Ellipsis,
                )
                Row(verticalAlignment = Alignment.CenterVertically) {
                    StatusDot(linkStatus(link))
                    Spacer(Modifier.width(8.dp))
                    Text(linkText(link), style = MaterialTheme.typography.bodyMedium,
                        color = statusColor(linkStatus(link)))
                }
                error?.let {
                    Text(it, style = MaterialTheme.typography.labelMedium,
                        color = MaterialTheme.colorScheme.error)
                }

                Spacer(Modifier.height(4.dp))
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceEvenly) {
                    Stat("Bateria", if (battery in 0..100) "$battery%${if (charging) " +" else ""}" else "--")
                    Stat("Red", if (onWifi) "Wi-Fi" else "Sin Wi-Fi")
                    Stat("Servidor", when (relay) {
                        RelayState.UP -> "Activo"
                        RelayState.STARTING -> "Arrancando"
                        RelayState.ERROR -> "Error"
                        RelayState.SUSPENDED -> "Suspendido"
                        RelayState.OFF -> "Parado"
                    })
                }
            }

            // ---- La accion que toca AHORA ----
            when {
                !onWifi -> Notice(
                    "Sin Wi-Fi",
                    "Flex OS y este telefono tienen que estar en la misma red local. " +
                        "El enlace no funciona por datos moviles.",
                    MaterialTheme.colorScheme.error,
                )
                !notifAccess -> {
                    Notice(
                        "Falta el acceso a notificaciones",
                        "Sin este permiso Flex Phone no puede leer ni una notificacion. " +
                            "Android no permite pedirlo con un dialogo: hay que darlo en Ajustes.",
                        MaterialTheme.colorScheme.tertiary,
                    )
                    Button(
                        onClick = { nav.navigate(Routes.WELCOME) },
                        modifier = Modifier.fillMaxWidth(),
                    ) { Text("Conceder permisos") }
                }
                link == LinkState.OFF -> Button(
                    onClick = { FlexLinkService.start(ctx) },
                    modifier = Modifier.fillMaxWidth(),
                ) { Text("Activar el enlace") }
                link == LinkState.PAIRING -> Button(
                    onClick = { nav.navigate(Routes.PAIR) },
                    modifier = Modifier.fillMaxWidth(),
                ) { Text("Teclear el codigo de Flex OS") }
                !bonds.isPaired() -> Notice(
                    "Esperando a Flex OS",
                    "Abre Flex Phone en Flex OS y pulsa \"Emparejar telefono\". " +
                        "Este telefono ya esta escuchando" +
                        (service?.linkAddress()?.let { " en $it" } ?: "") + ".",
                )
                else -> OutlinedButton(
                    onClick = { FlexLinkService.stop(ctx) },
                    modifier = Modifier.fillMaxWidth(),
                ) { Text("Apagar el enlace") }
            }

            // ---- Secciones ----
            SectionHeader("Secciones")
            NavRow(
                "Notificaciones",
                if (settings.allowedPackages.isEmpty()) "Ninguna aplicacion permitida todavia"
                else "${settings.allowedPackages.size} aplicaciones permitidas",
            ) { nav.navigate(Routes.APPS) }
            NavRow(
                "Servidor del navegador",
                when (relay) {
                    RelayState.UP -> "Activo"
                    RelayState.STARTING -> "Arrancando"
                    RelayState.ERROR -> "Con error"
                    RelayState.SUSPENDED -> "Suspendido por Android"
                    RelayState.OFF -> "Parado"
                },
            ) { nav.navigate(Routes.RELAY) }
            NavRow(
                "Conexion",
                service?.linkAddress()?.let { "$it:${service.linkPort()}" } ?: "Enlace apagado",
            ) { nav.navigate(Routes.CONNECTION) }
            NavRow("Estado del dispositivo", device.displayName) { nav.navigate(Routes.DEVICE) }
            NavRow(
                "Seguridad",
                if (bonds.isPaired()) "Flex OS vinculado" else "Sin vincular",
            ) { nav.navigate(Routes.SECURITY) }
            NavRow("Privacidad", "Que sale del telefono") { nav.navigate(Routes.PRIVACY) }
            NavRow("Diagnostico", "Contadores del enlace") { nav.navigate(Routes.DIAGNOSTICS) }
            NavRow("Ajustes", "Arranque, bateria y protocolo") { nav.navigate(Routes.SETTINGS) }
            NavRow("Acerca de", "Version y limites reales") { nav.navigate(Routes.ABOUT) }

            Spacer(Modifier.height(16.dp))
        }
    }
}

@Composable
private fun Stat(label: String, value: String) {
    Column(horizontalAlignment = Alignment.CenterHorizontally) {
        Text(label, style = MaterialTheme.typography.labelMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant)
        Text(value, style = MaterialTheme.typography.titleMedium, textAlign = TextAlign.Center)
    }
}
