package com.flexos.flexphone.ui.screens

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.navigation.NavController
import com.flexos.flexphone.protocol.Fbp
import com.flexos.flexphone.protocol.FlexLink
import com.flexos.flexphone.ui.FlexTopBar

@Composable
fun AboutScreen(nav: NavController) {
    val ctx = LocalContext.current
    val version = runCatching {
        ctx.packageManager.getPackageInfo(ctx.packageName, 0).versionName
    }.getOrDefault("?")

    Column(Modifier.fillMaxSize()) {
        FlexTopBar("Acerca de", onBack = { nav.popBackStack() })
        Column(
            Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            SectionCard("Flex Phone") {
                KeyValue("Version de la app", version ?: "?")
                KeyValue("Protocolo Flex Link", "v${FlexLink.VERSION}")
                KeyValue("Protocolo del navegador", "FBP/${Fbp.VERSION}")
            }

            SectionCard("Que puede y que no puede hacer") {
                Text(
                    "PUEDE:\n" +
                    "· mostrar tus notificaciones en Flex OS;\n" +
                    "· responder a un mensaje CUANDO la notificacion de Android " +
                    "trae una accion de respuesta (RemoteInput);\n" +
                    "· controlar la reproduccion de musica;\n" +
                    "· hacer sonar el telefono para encontrarlo;\n" +
                    "· servir un navegador a Flex OS por Wi-Fi.\n\n" +
                    "NO PUEDE:\n" +
                    "· iniciar una conversacion nueva de WhatsApp ni de otras apps " +
                    "de mensajeria. WhatsApp no ofrece una interfaz publica para " +
                    "eso, y Flex Phone no automatiza WhatsApp Web ni usa APIs no " +
                    "oficiales: pondria tu cuenta en riesgo;\n" +
                    "· responder a una notificacion que ya desaparecio;\n" +
                    "· hacer llamadas ni enviar SMS desde Flex OS sin el telefono. " +
                    "El ESP32-P4 no tiene modem celular.",
                    style = MaterialTheme.typography.bodyMedium,
                )
            }

            SectionCard("Codigo y documentacion") {
                Text(
                    "El protocolo esta documentado en docs/FLEX-PHONE.md del " +
                    "repositorio, junto con las pruebas fisicas pendientes y como " +
                    "compilar esta app.",
                    style = MaterialTheme.typography.bodyMedium,
                )
            }
        }
    }
}
