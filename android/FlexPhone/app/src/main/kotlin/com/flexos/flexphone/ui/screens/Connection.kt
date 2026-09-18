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
import com.flexos.flexphone.device.DeviceAdapter
import com.flexos.flexphone.domain.FlexPhoneState
import com.flexos.flexphone.domain.LinkState
import com.flexos.flexphone.link.FlexLinkService
import com.flexos.flexphone.link.WifiLinkServer
import com.flexos.flexphone.protocol.FlexLink
import com.flexos.flexphone.ui.FlexTopBar
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow

/**
 * CONEXION.
 *
 * Donde escucha este telefono y en que estado esta el enlace. Todo lo
 * que sale aqui es el estado REAL del servidor: si el puerto no esta
 * abierto, no se ensena un numero de puerto.
 */
@Composable
fun ConnectionScreen(nav: NavController) {
    val ctx = LocalContext.current
    val state = FlexPhoneState.instance
    val link by (state?.link ?: MutableStateFlow(LinkState.OFF)).collectAsState()
    val err by (state?.error ?: MutableStateFlow<String?>(null)).collectAsState()
    val device = remember { DeviceAdapter(ctx) }

    // La direccion local cambia al cambiar de red, y nadie avisa.
    var address by remember { mutableStateOf<String?>(null) }
    var port by remember { mutableStateOf(0) }
    var onWifi by remember { mutableStateOf(device.isOnWifi()) }
    LaunchedEffect(link) {
        while (true) {
            val s = FlexLinkService.current
            address = s?.linkAddress()
            port = s?.linkPort() ?: 0
            onWifi = device.isOnWifi()
            delay(3_000)
        }
    }

    Scaffold(topBar = { FlexTopBar("Conexion") { nav.popBackStack() } }) { pad ->
        Column(
            Modifier.padding(pad).fillMaxSize().verticalScroll(rememberScrollState())
                .padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            SectionCard("Enlace") {
                Row(verticalAlignment = androidx.compose.ui.Alignment.CenterVertically) {
                    StatusDot(linkStatus(link))
                    Spacer(Modifier.width(8.dp))
                    Text(linkText(link), style = MaterialTheme.typography.titleMedium,
                        color = statusColor(linkStatus(link)))
                }
                KeyValue("Transporte", "Wi-Fi (TCP)")
                KeyValue("Escuchando en", if (port > 0 && address != null) "$address:$port" else "--")
                KeyValue("Descubrimiento", if (port > 0) "UDP ${WifiLinkServer.UDP_PORT}" else "--")
                KeyValue("Protocolo", "Flex Link v${FlexLink.VERSION}")
                err?.let {
                    Text(it, style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.error)
                }
            }

            if (!onWifi) {
                Notice(
                    "Este telefono no esta en Wi-Fi",
                    "El enlace es una conexion dentro de la red local. Por datos moviles " +
                        "Flex OS no puede llegar hasta aqui.",
                    MaterialTheme.colorScheme.error,
                )
            }

            if (link == LinkState.OFF) {
                Button(onClick = { FlexLinkService.start(ctx) }, modifier = Modifier.fillMaxWidth()) {
                    Text("Activar el enlace")
                }
            } else {
                OutlinedButton(onClick = { FlexLinkService.stop(ctx) },
                    modifier = Modifier.fillMaxWidth()) {
                    Text("Apagar el enlace")
                }
            }

            Notice(
                "Quien llama a quien",
                "Este telefono ESCUCHA y Flex OS se conecta. Es asi porque el telefono " +
                    "esta encendido siempre y el reloj se suspende: al reves, cada " +
                    "suspension cortaria el enlace.",
            )

            Notice(
                "Si Flex OS no encuentra el telefono",
                "Flex OS pregunta por la red local y este telefono contesta. Algunos " +
                    "routers aislan a los clientes entre si y bloquean esa pregunta. " +
                    "Entonces hay que fijar la direccion de este telefono a mano desde " +
                    "Flex OS: " + (if (port > 0 && address != null) "$address:$port" else "activa antes el enlace para verla") + ".",
                MaterialTheme.colorScheme.tertiary,
            )
        }
    }
}
