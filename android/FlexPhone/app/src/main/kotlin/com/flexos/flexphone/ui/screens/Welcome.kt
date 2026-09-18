package com.flexos.flexphone.ui.screens

import android.content.Intent
import android.os.Build
import android.provider.Settings
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.navigation.NavController
import com.flexos.flexphone.notifications.FlexNotificationListener
import com.flexos.flexphone.ui.Routes

/**
 * Bienvenida y permisos.
 *
 * Cada permiso se explica POR QUE hace falta y QUE pasa si no se
 * concede. Una lista de permisos sin explicacion es la forma mas
 * rapida de que alguien desinstale una app que lee notificaciones.
 */
@Composable
fun WelcomeScreen(nav: NavController) {
    val ctx = LocalContext.current
    val scroll = rememberScrollState()

    val device = remember { com.flexos.flexphone.device.DeviceAdapter(ctx) }
    var notifOk by remember { mutableStateOf(FlexNotificationListener.hasAccess(ctx)) }
    var onWifi by remember { mutableStateOf(device.isOnWifi()) }

    val postNotif = rememberLauncherForActivityResult(
        ActivityResultContracts.RequestPermission()
    ) { }

    // Al volver de Ajustes se relee el estado REAL. Se comprueba cada
    // pocos segundos mientras esta pantalla esta a la vista porque el
    // usuario sale a Ajustes y vuelve, y Android no avisa de eso.
    LaunchedEffect(Unit) {
        while (true) {
            notifOk = FlexNotificationListener.hasAccess(ctx)
            onWifi = device.isOnWifi()
            kotlinx.coroutines.delay(1_500)
        }
    }

    Column(
        Modifier.fillMaxSize().verticalScroll(scroll).padding(24.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp),
    ) {
        Text("Flex Phone", style = MaterialTheme.typography.headlineMedium)
        Text(
            "Conecta tu telefono con Flex OS Ultra por Wi-Fi. Las notificaciones, " +
            "la musica y -si lo activas- un navegador que corre en el telefono " +
            "aparecen en el reloj.",
            style = MaterialTheme.typography.bodyLarge,
        )

        PermissionCard(
            title = "Acceso a notificaciones",
            why = "Es lo que permite ver tus notificaciones en Flex OS y responder " +
                  "a los mensajes que Android deja responder. Sin esto la app no " +
                  "puede hacer casi nada.",
            note = "Android no permite pedirlo con un dialogo: se concede en su " +
                   "pantalla de Ajustes.",
            granted = notifOk,
            action = "Abrir Ajustes",
            onClick = {
                ctx.startActivity(Intent(Settings.ACTION_NOTIFICATION_LISTENER_SETTINGS))
            },
        )

        // NO se pide Bluetooth. El enlace va por Wi-Fi, y pedir un
        // permiso que no se va a usar es la forma mas rapida de que
        // alguien desinstale una app que lee notificaciones.
        PermissionCard(
            title = "Red Wi-Fi",
            why = "El enlace con Flex OS es una conexion dentro de tu red local: los " +
                  "dos tienen que estar en el mismo Wi-Fi. No hace falta conceder " +
                  "nada, pero si el telefono no esta en Wi-Fi el enlace no puede " +
                  "funcionar.",
            note = if (onWifi) null
                   else "Ahora mismo este telefono no esta en una red Wi-Fi.",
            granted = onWifi,
            action = "Abrir ajustes de Wi-Fi",
            onClick = {
                runCatching { ctx.startActivity(Intent(Settings.ACTION_WIFI_SETTINGS)) }
            },
        )

        if (Build.VERSION.SDK_INT >= 33) {
            PermissionCard(
                title = "Mostrar notificaciones",
                why = "Solo para la notificacion del propio servicio, la que te " +
                      "deja pararlo desde la barra.",
                note = null,
                granted = ctx.checkSelfPermission(
                    android.Manifest.permission.POST_NOTIFICATIONS
                ) == android.content.pm.PackageManager.PERMISSION_GRANTED,
                action = "Conceder",
                onClick = { postNotif.launch(android.Manifest.permission.POST_NOTIFICATIONS) },
            )
        }

        Card(colors = CardDefaults.cardColors(
            containerColor = MaterialTheme.colorScheme.surfaceVariant
        )) {
            Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Text("Lo que Flex Phone NO hace", style = MaterialTheme.typography.titleMedium)
                Text(
                    "· No inicia conversaciones nuevas de WhatsApp ni de ninguna otra " +
                    "app de mensajeria. Solo puede responder cuando la notificacion " +
                    "de Android trae una accion de respuesta.\n" +
                    "· No lee tus contactos ni tus SMS.\n" +
                    "· No manda el contenido de tus mensajes a ningun servidor: el " +
                    "enlace es directo entre tu telefono y tu Flex OS.\n" +
                    "· Sin telefono, Flex OS no puede llamar ni enviar SMS.",
                    style = MaterialTheme.typography.bodyMedium,
                )
            }
        }

        Button(
            onClick = { nav.navigate(Routes.PAIR) },
            // El acceso a notificaciones es lo unico imprescindible.
            // El Wi-Fi hace falta para conectar, pero no para empezar
            // a configurar, asi que no bloquea el boton.
            enabled = notifOk,
            modifier = Modifier.fillMaxWidth(),
        ) { Text("Continuar al emparejamiento") }

        if (!notifOk) {
            Text(
                "Falta el acceso a notificaciones.",
                style = MaterialTheme.typography.labelMedium,
                color = MaterialTheme.colorScheme.error,
            )
        } else if (!onWifi) {
            Text(
                "Podras configurarlo todo, pero el enlace no conectara hasta que " +
                    "este telefono este en Wi-Fi.",
                style = MaterialTheme.typography.labelMedium,
                color = MaterialTheme.colorScheme.tertiary,
            )
        }
    }
}

@Composable
private fun PermissionCard(
    title: String, why: String, note: String?,
    granted: Boolean, action: String, onClick: () -> Unit,
) {
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Row(
                Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Text(title, style = MaterialTheme.typography.titleMedium)
                if (granted) {
                    Text("Concedido", color = MaterialTheme.colorScheme.secondary,
                        style = MaterialTheme.typography.labelMedium)
                }
            }
            Text(why, style = MaterialTheme.typography.bodyMedium)
            note?.let {
                Text(it, style = MaterialTheme.typography.labelMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant)
            }
            if (!granted) {
                FilledTonalButton(onClick = onClick) { Text(action) }
            }
        }
    }
}


/**
 * Permisos del transporte BLE. HOY NO SE PIDEN: el enlace va por
 * Wi-Fi. Se conservan junto a [com.flexos.flexphone.link.GattServer],
 * que es el transporte preparado para cuando el co-procesador C6 del
 * ESP32-P4 pueda ofrecer Bluetooth.
 */
internal fun blePermissions(): Array<String> =
    if (Build.VERSION.SDK_INT >= 31) arrayOf(
        android.Manifest.permission.BLUETOOTH_CONNECT,
        android.Manifest.permission.BLUETOOTH_ADVERTISE,
        android.Manifest.permission.BLUETOOTH_SCAN,
    ) else arrayOf(
        android.Manifest.permission.BLUETOOTH,
        android.Manifest.permission.BLUETOOTH_ADMIN,
        android.Manifest.permission.ACCESS_FINE_LOCATION,
    )

internal fun hasBlePermissions(ctx: android.content.Context): Boolean =
    blePermissions().all {
        ctx.checkSelfPermission(it) == android.content.pm.PackageManager.PERMISSION_GRANTED
    }
