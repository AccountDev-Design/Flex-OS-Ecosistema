package com.flexos.flexphone.ui.screens

import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.navigation.NavController
import com.flexos.flexphone.domain.Settings
import com.flexos.flexphone.link.FlexLinkService
import com.flexos.flexphone.storage.SettingsStore
import com.flexos.flexphone.ui.FlexTopBar
import kotlinx.coroutines.launch

/**
 * Dispositivos vinculados y DESVINCULAR.
 *
 * Desvincular borra de verdad: el vinculo BLE (con `removeBond`), los
 * ajustes guardados y las apps permitidas. No queda un "por si
 * vuelves" escondido.
 */
@Composable
fun DevicesScreen(nav: NavController, store: SettingsStore, settings: Settings) {
    val ctx = LocalContext.current
    val scope = rememberCoroutineScope()
    var confirming by remember { mutableStateOf(false) }

    Column(Modifier.fillMaxSize()) {
        FlexTopBar("Dispositivos", onBack = { nav.popBackStack() })
        Column(
            Modifier.fillMaxSize().padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            if (!settings.isPaired) {
                Text("No hay ningun Flex OS vinculado.",
                    style = MaterialTheme.typography.bodyLarge)
            } else {
                SectionCard(settings.bondedDeviceName ?: "Flex OS") {
                    // La direccion completa NO se enseria entera: es un
                    // identificador estable del dispositivo.
                    KeyValue("Direccion", settings.bondedDeviceAddress
                        ?.takeLast(5)?.let { "···$it" } ?: "-")
                    KeyValue("Apps permitidas", settings.allowedPackages.size.toString())
                    SwitchRow(
                        "Reanudar al reiniciar el telefono",
                        "Apagado de fabrica. Si lo activas, el enlace se " +
                        "restablece solo tras encender el telefono.",
                        settings.startOnBoot,
                    ) { on -> scope.launch { store.update { it.copy(startOnBoot = on) } } }
                }

                if (!confirming) {
                    OutlinedButton(
                        onClick = { confirming = true },
                        modifier = Modifier.fillMaxWidth(),
                        colors = ButtonDefaults.outlinedButtonColors(
                            contentColor = MaterialTheme.colorScheme.error
                        ),
                    ) { Text("Desvincular") }
                } else {
                    Card(colors = CardDefaults.cardColors(
                        containerColor = MaterialTheme.colorScheme.errorContainer
                    )) {
                        Column(Modifier.padding(16.dp),
                            verticalArrangement = Arrangement.spacedBy(8.dp)) {
                            Text("¿Desvincular este Flex OS?",
                                style = MaterialTheme.typography.titleMedium)
                            Text(
                                "Se borrara el vinculo Bluetooth, la lista de " +
                                "aplicaciones permitidas y los ajustes del relay. " +
                                "Tendras que emparejar otra vez desde cero.",
                                style = MaterialTheme.typography.bodyMedium,
                            )
                            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                                Button(onClick = {
                                    scope.launch {
                                        // 1) parar el enlace,
                                        FlexLinkService.stop(ctx)
                                        // 2) soltar el bonding del sistema,
                                        removeBond(ctx, settings.bondedDeviceAddress)
                                        // 3) y borrar TODO lo guardado.
                                        store.wipe()
                                        confirming = false
                                        nav.popBackStack()
                                    }
                                }) { Text("Si, desvincular") }
                                TextButton(onClick = { confirming = false }) { Text("Cancelar") }
                            }
                        }
                    }
                }
            }
        }
    }
}

/**
 * Suelta el bonding BLE. `removeBond` no es publica, asi que se
 * llama por reflexion -- es la via que usan todas las apps que
 * necesitan olvidar un dispositivo. Si falla, no se rompe nada: el
 * usuario siempre puede olvidarlo desde los ajustes de Bluetooth.
 */
private fun removeBond(ctx: android.content.Context, address: String?) {
    if (address == null) return
    runCatching {
        val mgr = ctx.getSystemService(android.content.Context.BLUETOOTH_SERVICE)
            as? android.bluetooth.BluetoothManager ?: return
        val dev = mgr.adapter?.getRemoteDevice(address) ?: return
        dev.javaClass.getMethod("removeBond").invoke(dev)
    }
}
