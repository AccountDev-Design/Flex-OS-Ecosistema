package com.flexos.flexphone.ui.screens

import android.content.Intent
import android.net.Uri
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
import com.flexos.flexphone.device.DeviceAdapter
import com.flexos.flexphone.notifications.FlexNotificationListener
import com.flexos.flexphone.protocol.Caps
import com.flexos.flexphone.relay.BrowserRelayService
import com.flexos.flexphone.ui.FlexTopBar
import kotlinx.coroutines.delay

/**
 * ESTADO DEL DISPOSITIVO.
 *
 * Lo mismo que Flex OS ensena de este telefono, para que el usuario
 * pueda comprobar que lo que se ve alli es lo que hay aqui.
 *
 * Tambien las CAPACIDADES: que puede hacer este telefono y que
 * ademas esta concedido ahora. Es exactamente lo que se le manda a
 * Flex OS, asi que si alli falta una funcion, aqui se ve por que.
 */
@Composable
fun DeviceScreen(nav: NavController) {
    val ctx = LocalContext.current
    val device = remember { DeviceAdapter(ctx) }

    var battery by remember { mutableStateOf(device.batteryPercent()) }
    var charging by remember { mutableStateOf(device.isCharging()) }
    var powerSave by remember { mutableStateOf(device.isPowerSaveMode()) }
    var storage by remember { mutableStateOf(device.storageMb()) }
    var memory by remember { mutableStateOf(device.memoryMb()) }
    var exempt by remember { mutableStateOf(device.isIgnoringBatteryOptimizations()) }
    LaunchedEffect(Unit) {
        while (true) {
            battery = device.batteryPercent()
            charging = device.isCharging()
            powerSave = device.isPowerSaveMode()
            storage = device.storageMb()
            memory = device.memoryMb()
            exempt = device.isIgnoringBatteryOptimizations()
            delay(5_000)
        }
    }

    val caps = remember(battery, exempt) {
        device.caps(
            notifAccess = FlexNotificationListener.hasAccess(ctx),
            relayRunning = BrowserRelayService.isRunning(),
            mediaActive = false,   // sin sesion abierta no se puede saber desde aqui
        )
    }

    Scaffold(topBar = { FlexTopBar("Estado del dispositivo") { nav.popBackStack() } }) { pad ->
        Column(
            Modifier.padding(pad).fillMaxSize().verticalScroll(rememberScrollState())
                .padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            SectionCard("Bateria") {
                KeyValue(
                    "Nivel",
                    if (battery in 0..100) "$battery%" else "Desconocido",
                    emphasis = true,
                )
                Meter(
                    if (battery in 0..100) battery / 100f else 0f,
                    when {
                        battery in 0..15 -> MaterialTheme.colorScheme.error
                        battery in 16..30 -> MaterialTheme.colorScheme.tertiary
                        else -> MaterialTheme.colorScheme.secondary
                    },
                )
                if (charging) KeyValue("Estado", "Cargando")
                if (powerSave) KeyValue("Ahorro de bateria", "Activo")
            }

            if (storage.second > 0) {
                SectionCard("Almacenamiento") {
                    KeyValue(
                        "Usado",
                        "${(storage.second - storage.first) / 1024} / ${storage.second / 1024} GB",
                    )
                    Meter(1f - storage.first.toFloat() / storage.second)
                }
            }
            if (memory.second > 0) {
                SectionCard("Memoria") {
                    KeyValue("Usada", "${memory.second - memory.first} / ${memory.second} MB")
                    Meter(1f - memory.first.toFloat() / memory.second)
                }
            }

            SectionCard("Dispositivo") {
                KeyValue("Nombre", device.displayName)
                KeyValue("Modelo", device.model.ifBlank { "--" })
                KeyValue("Fabricante", device.vendor.ifBlank { "--" })
                KeyValue("Sistema", device.osVersion)
            }

            SectionHeader("Capacidades")
            SectionCard("Lo que Flex OS puede ofrecer") {
                CapRow("Notificaciones", Caps.NOTIF, caps.supported, caps.granted)
                CapRow("Respuestas", Caps.REPLY, caps.supported, caps.granted)
                CapRow("Multimedia", Caps.MEDIA, caps.supported, caps.granted)
                CapRow("Servidor del navegador", Caps.RELAY, caps.supported, caps.granted)
                CapRow("Encontrar mi telefono", Caps.FIND, caps.supported, caps.granted)
                CapRow("Estado del dispositivo", Caps.STATE, caps.supported, caps.granted)
                CapRow("Hora", Caps.TIME, caps.supported, caps.granted)
                CapRow("Bluetooth LE", Caps.BLE, caps.supported, caps.granted)
            }

            SectionHeader("Segundo plano")
            if (!exempt) {
                Notice(
                    "El enlace puede detenerse con la pantalla apagada",
                    device.backgroundAdviceOrNull()
                        ?: "Excluye Flex Phone de la optimizacion de bateria.",
                    MaterialTheme.colorScheme.tertiary,
                )
                OutlinedButton(
                    onClick = {
                        // Se abre el AJUSTE del sistema. No se pide la
                        // exencion con un dialogo automatico: Android lo
                        // trata como abuso y es razonable que asi sea.
                        runCatching {
                            ctx.startActivity(
                                Intent(AndroidSettings.ACTION_IGNORE_BATTERY_OPTIMIZATION_SETTINGS)
                                    .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                            )
                        }
                    },
                    modifier = Modifier.fillMaxWidth(),
                ) { Text("Abrir ajustes de bateria") }
            } else {
                Notice(
                    "Sin restricciones de bateria",
                    "Este telefono no aplicara su optimizacion a Flex Phone. Aun asi, " +
                        "Android puede detener el servicio por memoria: no se promete " +
                        "que funcione indefinidamente.",
                    MaterialTheme.colorScheme.secondary,
                )
            }

            OutlinedButton(
                onClick = {
                    runCatching {
                        ctx.startActivity(
                            Intent(AndroidSettings.ACTION_APPLICATION_DETAILS_SETTINGS)
                                .setData(Uri.fromParts("package", ctx.packageName, null))
                                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                        )
                    }
                },
                modifier = Modifier.fillMaxWidth(),
            ) { Text("Ajustes de la aplicacion") }
        }
    }
}

@Composable
private fun CapRow(name: String, bit: Int, supported: Int, granted: Int) {
    val status = when {
        granted and bit != 0 -> FlexStatus.OK
        supported and bit != 0 -> FlexStatus.BAD
        else -> FlexStatus.OFF
    }
    val detail = when {
        granted and bit != 0 -> "Disponible"
        supported and bit == 0 -> "No disponible en este telefono"
        bit == Caps.NOTIF || bit == Caps.REPLY || bit == Caps.FIND ->
            "Falta el acceso a notificaciones"
        bit == Caps.MEDIA -> "Sin reproductor activo"
        bit == Caps.RELAY -> "El servidor no esta en marcha"
        bit == Caps.BLE -> "No se usa: el enlace va por Wi-Fi"
        else -> "Desactivado"
    }
    StatusRow(name, status, detail)
}
