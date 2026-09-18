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
import com.flexos.flexphone.domain.FlexPhoneState
import com.flexos.flexphone.domain.LinkState
import com.flexos.flexphone.domain.Settings
import com.flexos.flexphone.link.FlexLinkService
import com.flexos.flexphone.protocol.FlexLink
import com.flexos.flexphone.storage.BondStore
import com.flexos.flexphone.storage.SettingsStore
import com.flexos.flexphone.ui.FlexTopBar
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.launch
import java.text.DateFormat
import java.util.Date

/**
 * SEGURIDAD.
 *
 * El dispositivo vinculado, como funciona el emparejamiento y -- lo
 * que mas importa -- QUE NO PROTEGE. Un usuario que cree que sus
 * notificaciones van cifradas por su red toma decisiones distintas a
 * uno que sabe que no.
 */
@Composable
fun SecurityScreen(nav: NavController, store: SettingsStore, settings: Settings) {
    val ctx = LocalContext.current
    val scope = rememberCoroutineScope()
    val state = FlexPhoneState.instance
    val link by (state?.link ?: MutableStateFlow(LinkState.OFF)).collectAsState()
    val bonds = remember { BondStore(ctx) }

    var paired by remember { mutableStateOf(bonds.isPaired()) }
    var confirmRevoke by remember { mutableStateOf(false) }

    Scaffold(topBar = { FlexTopBar("Seguridad") { nav.popBackStack() } }) { pad ->
        Column(
            Modifier.padding(pad).fillMaxSize().verticalScroll(rememberScrollState())
                .padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            SectionHeader("Dispositivo vinculado")
            if (paired) {
                SectionCard(bonds.peerName()?.takeIf { it.isNotBlank() } ?: "Flex OS") {
                    KeyValue("Identificador", bonds.peerId() ?: "--")
                    KeyValue(
                        "Emparejado",
                        bonds.pairedAt().takeIf { it > 0 }?.let {
                            DateFormat.getDateTimeInstance(DateFormat.SHORT, DateFormat.SHORT)
                                .format(Date(it))
                        } ?: "--",
                    )
                    KeyValue("Sesion", linkText(link))
                    KeyValue("Protocolo", "Flex Link v${FlexLink.VERSION}")
                }
                Button(
                    onClick = { confirmRevoke = true },
                    modifier = Modifier.fillMaxWidth(),
                    colors = ButtonDefaults.buttonColors(
                        containerColor = MaterialTheme.colorScheme.error,
                    ),
                ) { Text("Revocar y olvidar") }
                Text(
                    "Borra la clave del vinculo de este telefono. Flex OS tendra que " +
                        "volver a emparejar desde cero.",
                    style = MaterialTheme.typography.labelMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            } else {
                SectionCard("Sin vincular") {
                    Text(
                        "Todavia no hay ningun Flex OS emparejado con este telefono.",
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
            }

            SectionHeader("Como funciona")
            Notice(
                "El codigo no viaja",
                "Flex OS ensena seis digitos y tu los tecleas aqui. Ese codigo no se " +
                    "manda por la red: los dos extremos derivan de el la misma clave y " +
                    "luego se demuestran que la tienen, cada uno al otro.",
            )
            Notice(
                "Tambien se comprueba a Flex OS",
                "No basta con que este telefono demuestre la clave. Flex OS devuelve su " +
                    "propia prueba, distinta, asi que un equipo cualquiera de la red no " +
                    "puede hacerse pasar por tu reloj reenviando lo que acaba de oir.",
            )
            Notice(
                "Lo que esto NO protege",
                "El contenido viaja SIN CIFRAR por la red local. Esto impide que un " +
                    "dispositivo que no ha emparejado abra sesion; no protege frente a " +
                    "quien ya este escuchando tu red. No es TLS y no se ensena como si lo fuera.",
                MaterialTheme.colorScheme.tertiary,
            )
            Notice(
                "Donde vive la clave",
                "Envuelta con una clave del Android Keystore, que no sale del almacen del " +
                    "sistema. Lo que hay en disco es un cifrado, no la clave. Revocar borra " +
                    "las dos cosas.",
            )
        }
    }

    if (confirmRevoke) {
        AlertDialog(
            onDismissRequest = { confirmRevoke = false },
            title = { Text("¿Revocar el vinculo?") },
            text = {
                Text(
                    "Se borra la clave de este telefono y la sesion se cierra. Flex OS " +
                        "dejara de recibir notificaciones hasta que vuelvas a emparejar."
                )
            },
            confirmButton = {
                TextButton(onClick = {
                    FlexLinkService.current?.forgetBond() ?: bonds.clear()
                    scope.launch { store.update { it.copy(flexosId = null, flexosName = null) } }
                    paired = false
                    confirmRevoke = false
                }) { Text("Revocar") }
            },
            dismissButton = {
                TextButton(onClick = { confirmRevoke = false }) { Text("Cancelar") }
            },
        )
    }
}
