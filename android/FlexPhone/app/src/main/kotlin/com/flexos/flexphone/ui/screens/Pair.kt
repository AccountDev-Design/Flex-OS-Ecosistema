package com.flexos.flexphone.ui.screens

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.navigation.NavController
import com.flexos.flexphone.domain.FlexPhoneState
import com.flexos.flexphone.domain.LinkState
import com.flexos.flexphone.link.FlexLinkService
import com.flexos.flexphone.protocol.FlexAuth
import com.flexos.flexphone.storage.SettingsStore
import com.flexos.flexphone.ui.FlexTopBar
import kotlinx.coroutines.flow.MutableStateFlow

/**
 * EMPAREJAMIENTO.
 *
 * Flex OS ensena seis digitos y AQUI se teclean. Ese es todo el
 * secreto compartido: el codigo NO viaja por la red, los dos extremos
 * derivan de el la misma clave y luego se demuestran que la tienen.
 *
 * Por eso esta pantalla tiene un campo de texto y no un boton de
 * "confirmar": confirmar sin teclear nada no probaria nada, y seria
 * justo lo que permitiria que un equipo cercano se vinculara solo.
 */
@Composable
fun PairScreen(nav: NavController, store: SettingsStore) {
    val ctx = LocalContext.current
    val state = FlexPhoneState.instance
    val link by (state?.link ?: MutableStateFlow(LinkState.OFF)).collectAsState()
    val err by (state?.error ?: MutableStateFlow<String?>(null)).collectAsState()

    var code by remember { mutableStateOf("") }
    var sent by remember { mutableStateOf(false) }
    val valid = FlexAuth.isValidCode(code)

    // Al abrirse la sesion, el emparejamiento termino: se sale solo.
    LaunchedEffect(link) {
        if (link == LinkState.READY && sent) nav.popBackStack()
    }

    Scaffold(topBar = { FlexTopBar("Emparejar", onBack = { nav.popBackStack() }) }) { pad ->
        Column(
            Modifier.padding(pad).fillMaxSize().padding(24.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp),
        ) {
            when (link) {
                LinkState.OFF, LinkState.UNAVAILABLE -> {
                    Text(
                        "Activa primero el enlace en este telefono. Despues, en Flex OS: " +
                            "Flex Phone → Emparejar telefono.",
                        style = MaterialTheme.typography.bodyLarge,
                    )
                    err?.let {
                        Text(it, color = MaterialTheme.colorScheme.error,
                            style = MaterialTheme.typography.bodyMedium)
                    }
                    Button(
                        onClick = { FlexLinkService.start(ctx) },
                        modifier = Modifier.fillMaxWidth(),
                        enabled = link != LinkState.UNAVAILABLE,
                    ) { Text("Activar el enlace") }
                }

                LinkState.ADVERTISING, LinkState.CONNECTING -> {
                    Text("Esperando a Flex OS...", style = MaterialTheme.typography.bodyLarge)
                    LinearProgressIndicator(Modifier.fillMaxWidth())
                    Notice(
                        "Que hacer en el reloj",
                        "Abre Flex Phone en Flex OS y pulsa \"Emparejar telefono\". " +
                            "Aparecera un codigo de seis digitos.",
                    )
                    Notice(
                        "Si no aparece nada",
                        "Los dos tienen que estar en la MISMA red Wi-Fi. Algunos routers " +
                            "aislan a los clientes entre si; en ese caso hay que fijar la " +
                            "direccion de este telefono desde Flex OS.",
                        MaterialTheme.colorScheme.tertiary,
                    )
                }

                LinkState.PAIRING -> {
                    Text(
                        "Teclea el codigo que ensena Flex OS",
                        style = MaterialTheme.typography.titleMedium,
                    )
                    OutlinedTextField(
                        value = code,
                        onValueChange = { v ->
                            // Solo digitos y como mucho seis: asi no se
                            // puede enviar algo que ya se sabe que no es
                            // un codigo.
                            code = v.filter { it.isDigit() }.take(6)
                            sent = false
                        },
                        singleLine = true,
                        textStyle = TextStyle(
                            fontSize = 34.sp, textAlign = TextAlign.Center, letterSpacing = 8.sp,
                        ),
                        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.NumberPassword),
                        modifier = Modifier.fillMaxWidth(),
                        supportingText = { Text("Seis digitos") },
                        isError = code.isNotEmpty() && !valid,
                    )
                    err?.let {
                        Text(it, color = MaterialTheme.colorScheme.error,
                            style = MaterialTheme.typography.bodyMedium)
                    }
                    Button(
                        onClick = {
                            sent = FlexLinkService.current?.submitPairingCode(code) ?: false
                        },
                        enabled = valid,
                        modifier = Modifier.fillMaxWidth(),
                    ) { Text(if (sent) "Comprobando..." else "Emparejar") }
                    Notice(
                        "El codigo no se envia",
                        "Se usa aqui para calcular la clave del vinculo. Por la red solo " +
                            "viaja la prueba de que los dos habeis llegado a la misma.",
                    )
                }

                LinkState.READY -> {
                    Text("Emparejado", style = MaterialTheme.typography.titleLarge,
                        color = MaterialTheme.colorScheme.secondary)
                    Notice(
                        "Siguiente paso",
                        "Elige en Notificaciones que aplicaciones pueden enviar las suyas. " +
                            "De fabrica no hay ninguna: una app que reenvia todo por defecto " +
                            "es una fuga de privacidad.",
                    )
                    Button(onClick = { nav.popBackStack() }, modifier = Modifier.fillMaxWidth()) {
                        Text("Listo")
                    }
                }

                LinkState.ERROR -> {
                    Text("No se pudo emparejar", style = MaterialTheme.typography.titleMedium,
                        color = MaterialTheme.colorScheme.error)
                    Text(
                        err ?: "El enlace fallo.",
                        style = MaterialTheme.typography.bodyMedium,
                    )
                    Notice(
                        "Comprueba",
                        "• Que el codigo es el MISMO que ensena Flex OS\n" +
                            "• Que los dos estan en la misma red Wi-Fi\n" +
                            "• Que el codigo no ha caducado (dura dos minutos)",
                        MaterialTheme.colorScheme.tertiary,
                    )
                    Button(
                        onClick = {
                            code = ""; sent = false
                            FlexLinkService.stop(ctx)
                            FlexLinkService.start(ctx)
                        },
                        modifier = Modifier.fillMaxWidth(),
                    ) { Text("Volver a intentarlo") }
                }
            }
        }
    }
}
