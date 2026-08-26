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

    var notifOk by remember { mutableStateOf(FlexNotificationListener.connected) }
    var bleOk by remember { mutableStateOf(hasBlePermissions(ctx)) }

    val blePerms = rememberLauncherForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions()
    ) { bleOk = hasBlePermissions(ctx) }

    val postNotif = rememberLauncherForActivityResult(
        ActivityResultContracts.RequestPermission()
    ) { }

    // Al volver de Ajustes se relee el estado real, sin sondear.
    LaunchedEffect(Unit) { notifOk = FlexNotificationListener.connected }

    Column(
        Modifier.fillMaxSize().verticalScroll(scroll).padding(24.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp),
    ) {
        Text("Flex Phone", style = MaterialTheme.typography.headlineMedium)
        Text(
            "Conecta tu telefono con Flex OS Ultra. Las notificaciones, la " +
            "musica y -si lo activas- un navegador que corre en el telefono " +
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

        PermissionCard(
            title = "Bluetooth",
            why = "El enlace con Flex OS va por Bluetooth de baja energia. " +
                  "Tambien se usa para anunciarse al reloj mientras lo buscas.",
            note = "No se pide ubicacion: el escaneo se declara con " +
                   "\"neverForLocation\".",
            granted = bleOk,
            action = "Conceder",
            onClick = { blePerms.launch(blePermissions()) },
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
            enabled = notifOk && bleOk,
            modifier = Modifier.fillMaxWidth(),
        ) { Text("Continuar al emparejamiento") }

        if (!notifOk || !bleOk) {
            Text(
                "Faltan permisos por conceder.",
                style = MaterialTheme.typography.labelMedium,
                color = MaterialTheme.colorScheme.error,
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
