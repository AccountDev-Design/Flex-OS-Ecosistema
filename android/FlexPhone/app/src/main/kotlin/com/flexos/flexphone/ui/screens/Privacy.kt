package com.flexos.flexphone.ui.screens

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.navigation.NavController
import com.flexos.flexphone.domain.Settings
import com.flexos.flexphone.storage.SettingsStore
import com.flexos.flexphone.ui.FlexTopBar
import kotlinx.coroutines.launch

@Composable
fun PrivacyScreen(nav: NavController, store: SettingsStore, settings: Settings) {
    val scope = rememberCoroutineScope()
    Column(Modifier.fillMaxSize()) {
        FlexTopBar("Privacidad", onBack = { nav.popBackStack() })
        Column(
            Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            SectionCard("Contenido de las notificaciones") {
                SwitchRow(
                    "No enviar el cuerpo del mensaje",
                    "Flex OS vera la app y el remitente, pero nunca el texto. " +
                    "El cuerpo NI SIQUIERA sale del telefono.",
                    settings.hideContent,
                ) { on -> scope.launch { store.update { it.copy(hideContent = on) } } }

                HorizontalDivider()

                SwitchRow(
                    "Ocultar codigos de un solo uso",
                    "Suprime el cuerpo de lo que parezca un OTP o una " +
                    "verificacion. Ante la duda, se oculta.",
                    settings.hideSensitive,
                ) { on -> scope.launch { store.update { it.copy(hideSensitive = on) } } }
            }

            Card(colors = CardDefaults.cardColors(
                containerColor = MaterialTheme.colorScheme.surfaceVariant
            )) {
                Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    Text("Que sale del telefono", style = MaterialTheme.typography.titleMedium)
                    Text(
                        "Se envia: nombre de la app, paquete, titulo (el remitente), " +
                        "un resumen del texto recortado, la hora, la categoria, la " +
                        "prioridad y las etiquetas de las acciones.\n\n" +
                        "No se envia: el icono de la app, la notificacion completa, " +
                        "tus contactos, tus SMS ni tu ubicacion.\n\n" +
                        "Nada de esto pasa por ningun servidor: el enlace es directo " +
                        "entre tu telefono y tu Flex OS, cifrado por el emparejamiento " +
                        "Bluetooth.\n\n" +
                        "Los textos de tus mensajes NUNCA se escriben en los registros " +
                        "del sistema.",
                        style = MaterialTheme.typography.bodyMedium,
                    )
                }
            }

            SectionCard("En la pantalla de bloqueo de Flex OS") {
                Text(
                    "Flex OS tiene su propio ajuste para ocultar el cuerpo cuando " +
                    "esta bloqueado. Si aqui activas \"No enviar el cuerpo\", el " +
                    "texto no llega al reloj en ningun caso, ni bloqueado ni " +
                    "desbloqueado -- que es la garantia mas fuerte de las dos.",
                    style = MaterialTheme.typography.bodyMedium,
                )
            }
        }
    }
}
