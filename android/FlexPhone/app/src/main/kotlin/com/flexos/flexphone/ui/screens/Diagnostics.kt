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
            SectionCard("Enlace") {
                KeyValue("Estado", linkLabel(link).first, emphasis = true)
                KeyValue("Lector de notificaciones",
                    if (FlexNotificationListener.connected) "activo" else "sin permiso")
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
