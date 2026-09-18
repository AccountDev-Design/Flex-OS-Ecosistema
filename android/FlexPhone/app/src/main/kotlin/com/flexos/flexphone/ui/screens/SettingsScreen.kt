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
import com.flexos.flexphone.domain.Settings
import com.flexos.flexphone.link.FlexLinkService
import com.flexos.flexphone.link.WifiLinkServer
import com.flexos.flexphone.protocol.FlexLink
import com.flexos.flexphone.storage.BondStore
import com.flexos.flexphone.storage.SettingsStore
import com.flexos.flexphone.ui.FlexTopBar
import kotlinx.coroutines.launch

/**
 * AJUSTES.
 *
 * Arranque, protocolo y borrado. Cada interruptor hace algo REAL:
 * aqui no hay ninguna preferencia que se guarde y no se use.
 */
@Composable
fun SettingsScreen(nav: NavController, store: SettingsStore, settings: Settings) {
    val ctx = LocalContext.current
    val scope = rememberCoroutineScope()
    val device = remember { DeviceAdapter(ctx) }
    val bonds = remember { BondStore(ctx) }
    var confirmWipe by remember { mutableStateOf(false) }

    Scaffold(topBar = { FlexTopBar("Ajustes") { nav.popBackStack() } }) { pad ->
        Column(
            Modifier.padding(pad).fillMaxSize().verticalScroll(rememberScrollState())
                .padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            SectionCard("Arranque") {
                SwitchRow(
                    "Reanudar al encender el telefono",
                    "Solo si ya hay un Flex OS vinculado",
                    settings.startOnBoot,
                ) { on -> scope.launch { store.update { it.copy(startOnBoot = on) } } }
            }

            SectionCard("Enlace") {
                KeyValue("Protocolo", "Flex Link v${FlexLink.VERSION}")
                KeyValue("Puerto del enlace", WifiLinkServer.TCP_PORT.toString())
                KeyValue("Puerto de descubrimiento", WifiLinkServer.UDP_PORT.toString())
                KeyValue("Identificador de este telefono", bonds.selfId())
            }

            SectionHeader("Bluetooth LE")
            Notice(
                "Hoy no se usa",
                "Flex OS Ultra corre sobre un ESP32-P4, que no tiene radio Bluetooth. " +
                    "Mientras eso siga asi, anunciar por BLE seria gastar bateria de este " +
                    "telefono para que no llame nadie. El enlace va por Wi-Fi, y el " +
                    "transporte BLE queda escrito y preparado para cuando el " +
                    "co-procesador C6 pueda darlo.",
            )

            SectionHeader("Datos")
            Button(
                onClick = { confirmWipe = true },
                modifier = Modifier.fillMaxWidth(),
                colors = ButtonDefaults.buttonColors(
                    containerColor = MaterialTheme.colorScheme.error,
                ),
            ) { Text("Borrar todo lo de Flex Phone") }
            Text(
                "Vinculo, aplicaciones permitidas y ajustes del servidor. No se toca " +
                    "ninguna notificacion del telefono.",
                style = MaterialTheme.typography.labelMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )

            SectionHeader("Este telefono")
            SectionCard(device.displayName) {
                KeyValue("Modelo", device.model.ifBlank { "--" })
                KeyValue("Sistema", device.osVersion)
            }
        }
    }

    if (confirmWipe) {
        AlertDialog(
            onDismissRequest = { confirmWipe = false },
            title = { Text("¿Borrar los datos de Flex Phone?") },
            text = {
                Text(
                    "Se borran el vinculo con Flex OS, las aplicaciones permitidas y los " +
                        "ajustes del servidor. Habra que emparejar de nuevo."
                )
            },
            confirmButton = {
                TextButton(onClick = {
                    FlexLinkService.stop(ctx)
                    bonds.clear()
                    scope.launch { store.wipe() }
                    confirmWipe = false
                    nav.popBackStack()
                }) { Text("Borrar") }
            },
            dismissButton = {
                TextButton(onClick = { confirmWipe = false }) { Text("Cancelar") }
            },
        )
    }
}
