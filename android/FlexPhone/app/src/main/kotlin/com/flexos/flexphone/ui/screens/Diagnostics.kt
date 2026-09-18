package com.flexos.flexphone.ui.screens

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.navigation.NavController
import com.flexos.flexphone.domain.FlexPhoneState
import com.flexos.flexphone.domain.LinkState
import com.flexos.flexphone.notifications.FlexNotificationListener
import com.flexos.flexphone.ui.FlexTopBar
import kotlinx.coroutines.flow.MutableStateFlow

/**
 * Diagnostico de conexion.
 *
 * SOLO CONTADORES. Aqui no aparece ni un titulo de notificacion, ni
 * un remitente, ni una URL: si el usuario ensena esta pantalla para
 * pedir ayuda, no debe estar revelando su vida privada.
 */
@Composable
fun DiagnosticsScreen(nav: NavController) {
    val ctx = androidx.compose.ui.platform.LocalContext.current
    val state = FlexPhoneState.instance
    val diag by (state?.diag ?: MutableStateFlow(FlexPhoneState.Diag())).collectAsState()
    val link by (state?.link ?: MutableStateFlow(LinkState.OFF)).collectAsState()
    val err by (state?.error ?: MutableStateFlow<String?>(null)).collectAsState()

    Column(Modifier.fillMaxSize()) {
        FlexTopBar("Diagnostico", onBack = { nav.popBackStack() })
        Column(
            Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            // Cada fila es un servicio REAL con su estado REAL. Si algo
            // falla, se dice CUAL y por que: "no funciona" a secas no
            // le sirve a nadie para arreglarlo.
            SectionCard("Servicios") {
                StatusRow("Enlace", linkStatus(link), linkText(link))
                val listener = com.flexos.flexphone.notifications.FlexNotificationListener
                StatusRow(
                    "Lector de notificaciones",
                    when {
                        listener.connected -> FlexStatus.OK
                        listener.hasAccess(ctx) -> FlexStatus.BUSY
                        else -> FlexStatus.BAD
                    },
                    when {
                        listener.connected -> "Conectado"
                        // El permiso puede estar concedido y el servicio
                        // tardar en engancharse. Son dos cosas distintas
                        // y se dicen por separado.
                        listener.hasAccess(ctx) -> "Permiso dado, enganchando"
                        else -> "Falta el acceso en Ajustes de Android"
                    },
                )
                val relayUp = com.flexos.flexphone.relay.BrowserRelayService.isRunning()
                StatusRow(
                    "Servidor del navegador",
                    if (relayUp) FlexStatus.OK else FlexStatus.OFF,
                    if (relayUp) "Escuchando" else "Parado",
                )
                val svc = com.flexos.flexphone.link.FlexLinkService.current
                StatusRow(
                    "Puerto del enlace",
                    if ((svc?.linkPort() ?: 0) > 0) FlexStatus.OK else FlexStatus.OFF,
                    svc?.linkAddress()?.let { "$it:${svc.linkPort()}" } ?: "Enlace apagado",
                )
                err?.let {
                    Text(it, style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.error)
                }
            }

            SectionCard("Trafico") {
                KeyValue("Mensajes enviados", diag.sent.toString())
                KeyValue("Mensajes recibidos", diag.received.toString())
                KeyValue("Descartados", diag.dropped.toString())
                KeyValue("Tramas invalidas", diag.badFrames.toString())
                KeyValue("Reconexiones", diag.reconnects.toString())
            }

            SectionCard("Notificaciones") {
                KeyValue("Reenviadas", diag.notificationsForwarded.toString())
                KeyValue("No enviadas (sin enlace)", diag.notificationsFiltered.toString())
            }

            SectionCard("Respuestas") {
                KeyValue("Entregadas a la app", diag.repliesOk.toString())
                KeyValue("Fallidas", diag.repliesFailed.toString())
                Text(
                    "\"Entregada a la app\" significa que Android acepto la accion " +
                    "de respuesta. No significa que el destinatario la haya recibido " +
                    "o leido: eso solo lo sabe la propia aplicacion de mensajeria.",
                    style = MaterialTheme.typography.labelMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }

            Card(colors = CardDefaults.cardColors(
                containerColor = MaterialTheme.colorScheme.surfaceVariant
            )) {
                Text(
                    "Esta pantalla solo muestra contadores. No aparece el contenido " +
                    "de ninguna notificacion, ni remitentes, ni direcciones web: " +
                    "puedes ensenarla sin revelar nada tuyo.",
                    style = MaterialTheme.typography.bodyMedium,
                    modifier = Modifier.padding(16.dp),
                )
            }
        }
    }
}
