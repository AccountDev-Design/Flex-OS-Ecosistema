package com.flexos.flexphone.ui.screens

import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.navigation.NavController
import com.flexos.flexphone.domain.FlexPhoneState
import com.flexos.flexphone.domain.LinkState
import com.flexos.flexphone.link.FlexLinkService
import com.flexos.flexphone.storage.SettingsStore
import com.flexos.flexphone.ui.FlexTopBar
import com.flexos.flexphone.ui.Routes

/**
 * Emparejamiento.
 *
 * El codigo lo GENERA Flex OS y se muestra en el reloj; aqui el
 * usuario lo confirma. Hacen falta LAS DOS confirmaciones: sin eso,
 * un dispositivo cercano podria vincularse solo.
 */
@Composable
fun PairScreen(nav: NavController, store: SettingsStore) {
    val ctx = LocalContext.current
    val state = FlexPhoneState.instance
    val link by (state?.link ?: kotlinx.coroutines.flow.MutableStateFlow(LinkState.OFF))
        .collectAsState()
    val code by (state?.pairCode ?: kotlinx.coroutines.flow.MutableStateFlow<String?>(null))
        .collectAsState()
    val err by (state?.error ?: kotlinx.coroutines.flow.MutableStateFlow<String?>(null))
        .collectAsState()

    Column(Modifier.fillMaxSize()) {
        FlexTopBar("Emparejar", onBack = { nav.popBackStack() })
        Column(
            Modifier.fillMaxSize().padding(24.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp),
        ) {
            when (link) {
                LinkState.OFF, LinkState.UNAVAILABLE -> {
                    Text(
                        "Abre Flex Phone en tu Flex OS Ultra y pulsa \"Emparejar telefono\". " +
                        "Despues activa el enlace aqui.",
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
                    ) { Text("Activar enlace") }
                }
                LinkState.ADVERTISING, LinkState.CONNECTING -> {
                    Text("Buscando tu Flex OS...", style = MaterialTheme.typography.bodyLarge)
                    LinearProgressIndicator(Modifier.fillMaxWidth())
                    Text(
                        "Asegurate de que el reloj esta en la pantalla de emparejamiento.",
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
                LinkState.PAIRING -> {
                    Text(
                        "Comprueba que este codigo es EL MISMO que aparece en tu Flex OS:",
                        style = MaterialTheme.typography.bodyLarge,
                    )
                    Card(Modifier.fillMaxWidth()) {
                        Text(
                            code ?: "······",
                            style = MaterialTheme.typography.headlineMedium.copy(
                                fontSize = 40.sp, letterSpacing = 8.sp,
                            ),
                            textAlign = TextAlign.Center,
                            modifier = Modifier.fillMaxWidth().padding(24.dp),
                        )
                    }
                    Text(
                        "Si no coincide, NO confirmes: puede ser otro dispositivo.",
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.error,
                    )
                    Button(
                        onClick = {
                            // Confirmar AQUI es solo la mitad: el enlace
                            // no se abre hasta que Flex OS confirma tambien.
                            state?.sender?.invoke(
                                com.flexos.flexphone.protocol.FlexLink.T_PAIR_CONFIRM, ByteArray(0)
                            )
                        },
                        modifier = Modifier.fillMaxWidth(),
                    ) { Text("Coincide, confirmar") }
                    OutlinedButton(
                        onClick = { FlexLinkService.stop(ctx) },
                        modifier = Modifier.fillMaxWidth(),
                    ) { Text("Cancelar") }
                }
                LinkState.READY -> {
                    Text("Emparejado", style = MaterialTheme.typography.headlineMedium,
                        color = MaterialTheme.colorScheme.secondary)
                    Text(
                        "Ya puedes elegir que aplicaciones envian sus notificaciones.",
                        style = MaterialTheme.typography.bodyLarge,
                    )
                    Button(
                        onClick = { nav.navigate(Routes.APPS) },
                        modifier = Modifier.fillMaxWidth(),
                    ) { Text("Elegir aplicaciones") }
                    TextButton(
                        onClick = { nav.navigate(Routes.STATUS) },
                        modifier = Modifier.fillMaxWidth(),
                    ) { Text("Ir al estado") }
                }
                LinkState.ERROR -> {
                    Text("No se pudo emparejar", style = MaterialTheme.typography.titleLarge,
                        color = MaterialTheme.colorScheme.error)
                    Text(err ?: "Error desconocido", style = MaterialTheme.typography.bodyMedium)
                    Button(
                        onClick = { FlexLinkService.start(ctx) },
                        modifier = Modifier.fillMaxWidth(),
                    ) { Text("Reintentar") }
                }
            }
        }
    }
}
