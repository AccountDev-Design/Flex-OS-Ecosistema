package com.flexos.flexphone.ui.screens

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
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
import kotlinx.coroutines.delay
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
 *
 * DOS COSAS QUE ESTA PANTALLA NO HACE
 * -----------------------------------
 *  · No ensena NUNCA un indicador de progreso sin un plazo detras.
 *    Cada espera de aqui termina: o en exito, o en un motivo escrito.
 *    Un circulo girando para siempre es lo que hacia imposible saber
 *    si el fallo era la red, el codigo o la propia aplicacion.
 *  · No dice "encontrado" por haber emitido una busqueda. En la lista
 *    solo aparece lo que ha CONTESTADO.
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
            Modifier.padding(pad).fillMaxSize().verticalScroll(rememberScrollState()).padding(24.dp),
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

                LinkState.ADVERTISING, LinkState.CONNECTING -> SearchSection()

                LinkState.PAIRING -> {
                    CodeSection(
                        code = code,
                        onCode = { code = it; sent = false },
                        valid = valid,
                        sent = sent,
                        err = err,
                        onSubmit = {
                            sent = FlexLinkService.current?.submitPairingCode(code) ?: false
                        },
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
                    Text(err ?: "El enlace fallo.", style = MaterialTheme.typography.bodyMedium)
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

// =============================================================
//  Buscar relojes
// =============================================================
/** Cuanto dura una busqueda, y cada cuanto se repite la pregunta. */
private const val SEARCH_MS = 9_000L
private const val SEARCH_STEP_MS = 1_500L

/**
 * La busqueda en esta red.
 *
 * SE PREGUNTA DESDE EL TELEFONO. La direccion contraria -- el reloj
 * emitiendo y Android recibiendo -- es la que muchos routers y el
 * propio ahorro de bateria de Android dejan a medias. Emitir desde
 * aqui no lo filtra nadie, y la respuesta del reloj vuelve en
 * unidifusion, que tampoco.
 *
 * Y sirve para algo mas que pintar una lista: al recibir la pregunta,
 * el reloj aprende de ella la direccion y el puerto de este telefono,
 * asi que puede conectarse aunque su propia difusion no haya llegado
 * nunca hasta aqui.
 */
@Composable
private fun SearchSection() {
    val state = FlexPhoneState.instance
    val watches by (state?.watches ?: MutableStateFlow(emptyList<FlexPhoneState.Watch>()))
        .collectAsState()

    var round by remember { mutableIntStateOf(0) }      // cada valor nuevo = otra busqueda
    var searching by remember { mutableStateOf(true) }
    var couldSend by remember { mutableStateOf(true) }
    var manual by remember { mutableStateOf("") }
    var manualMsg by remember { mutableStateOf<String?>(null) }

    // La busqueda TIENE FINAL. Se pregunta varias veces durante unos
    // segundos y despues se para: un buscador que gira indefinidamente
    // no distingue "todavia no ha contestado" de "aqui no hay nadie".
    LaunchedEffect(round) {
        searching = true
        val svc = FlexLinkService.current
        var t = 0L
        while (t < SEARCH_MS) {
            couldSend = (svc?.searchWatches() ?: 0) > 0
            delay(SEARCH_STEP_MS)
            t += SEARCH_STEP_MS
        }
        searching = false
    }

    Text(
        if (searching) "Buscando relojes en esta red..."
        else "Preguntando cada pocos segundos",
        style = MaterialTheme.typography.titleMedium,
    )
    if (searching) LinearProgressIndicator(Modifier.fillMaxWidth())

    if (!couldSend) {
        Notice(
            "No se pudo emitir la busqueda",
            "Este telefono no tiene una direccion en ninguna red Wi-Fi. Conectalo a la " +
                "misma red que Flex OS y vuelve a intentarlo.",
            MaterialTheme.colorScheme.error,
        )
    }

    // ---- Lo que ha contestado ----
    if (watches.isNotEmpty()) {
        SectionHeader("Relojes que han contestado")
        watches.forEach { w -> WatchRow(w) }
    } else if (!searching) {
        EmptyState(
            "No contesto ningun reloj",
            "Ni la busqueda de este telefono ni la del reloj llegaron al otro lado. " +
                "Casi siempre es una de estas tres: el enlace no esta encendido en Flex OS, " +
                "no estais en la misma red, o el router aisla a los clientes entre si. " +
                "El telefono sigue preguntando en segundo plano; si aparece, saldra aqui solo.",
        )
    }

    Button(onClick = { round++ }, enabled = !searching, modifier = Modifier.fillMaxWidth()) {
        Text(if (searching) "Buscando..." else "Buscar otra vez")
    }

    // ---- Lo que hay que hacer en el reloj ----
    Notice(
        "Que hacer en el reloj",
        "Abre Flex Phone en Flex OS y pulsa \"Emparejar telefono\". Aparecera un codigo " +
            "de seis digitos que se teclea aqui.",
    )

    // ---- La direccion de este telefono, para teclearla en el reloj ----
    val svc = FlexLinkService.current
    val addr = svc?.linkAddress()
    val port = svc?.linkPort() ?: 0
    if (addr != null && port > 0) {
        SectionCard("Direccion de este telefono") {
            KeyValue("Direccion", "$addr:$port", emphasis = true)
            Text(
                "Si el reloj no lo encuentra solo, se puede fijar esta direccion a mano en " +
                    "Flex OS: Flex Phone → Conexion → Fijar direccion.",
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
    }

    // ---- Respaldo: preguntar a una direccion concreta ----
    SectionCard("Buscar por direccion") {
        Text(
            "Para redes que filtran la difusion. Lee la direccion del reloj en Flex OS " +
                "(Flex Phone → Conexion) y escribela aqui: la pregunta va directa, sin " +
                "difusion de por medio.",
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
        OutlinedTextField(
            value = manual,
            onValueChange = { v ->
                // Solo lo que puede formar una direccion IPv4.
                manual = v.filter { c -> c.isDigit() || c == '.' }.take(15)
                manualMsg = null
            },
            singleLine = true,
            label = { Text("Direccion del reloj") },
            placeholder = { Text("192.168.1.45") },
            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Decimal),
            modifier = Modifier.fillMaxWidth(),
        )
        manualMsg?.let {
            Text(it, style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
        Button(
            onClick = {
                val ok = FlexLinkService.current?.probeWatchAt(manual) ?: false
                // El mensaje dice lo que HA PASADO, no lo que se espera
                // que pase: se mando la pregunta. Si el reloj esta ahi,
                // aparecera arriba en un par de segundos.
                manualMsg = if (ok) "Pregunta enviada a $manual. Si hay un Flex OS ahi, " +
                                    "aparecera en la lista de arriba."
                            else "Esa no es una direccion valida."
            },
            enabled = manual.count { it == '.' } == 3 && manual.isNotBlank(),
            modifier = Modifier.fillMaxWidth(),
        ) { Text("Preguntar a esta direccion") }
    }
}

@Composable
private fun WatchRow(w: FlexPhoneState.Watch) {
    var nudged by remember(w.id) { mutableStateOf(false) }
    GlassCard(
        accent = if (w.pairing) MaterialTheme.colorScheme.secondary
                 else MaterialTheme.colorScheme.primary,
        modifier = Modifier.clickable {
            // Volver a preguntarle SOLO a el. El reloj aprende de esa
            // pregunta la direccion de este telefono y se conecta: es
            // lo mas util que puede hacer un toque aqui, porque quien
            // abre la conexion es el reloj, no el telefono.
            nudged = FlexLinkService.current?.probeWatchAt(w.address) ?: false
        },
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f)) {
                Text(w.name, style = MaterialTheme.typography.titleMedium)
                Text(
                    w.address + "  ·  " + w.id,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
            if (w.pairing) {
                Text(
                    "ensenando codigo",
                    style = MaterialTheme.typography.labelMedium,
                    color = MaterialTheme.colorScheme.secondary,
                )
            }
        }
        if (nudged) {
            Text(
                "Pregunta enviada. El reloj deberia conectarse en unos segundos.",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
    }
}

// =============================================================
//  El codigo
// =============================================================
@Composable
private fun CodeSection(
    code: String,
    onCode: (String) -> Unit,
    valid: Boolean,
    sent: Boolean,
    err: String?,
    onSubmit: () -> Unit,
) {
    // ¿Ha llegado ya la sal de Flex OS? Sin ella no se puede derivar
    // nada, y pulsar "Emparejar" no mandaria nada. Se dice, en vez de
    // aceptar el codigo y dejar la pantalla esperando.
    val ready = FlexLinkService.current?.isAwaitingCode() ?: false

    Text("Teclea el codigo que ensena Flex OS", style = MaterialTheme.typography.titleMedium)
    OutlinedTextField(
        value = code,
        onValueChange = { v ->
            // Solo digitos y como mucho seis: asi no se puede enviar
            // algo que ya se sabe que no es un codigo.
            onCode(v.filter { it.isDigit() }.take(6))
        },
        singleLine = true,
        textStyle = TextStyle(fontSize = 34.sp, textAlign = TextAlign.Center, letterSpacing = 8.sp),
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
        onClick = onSubmit,
        enabled = valid && ready,
        modifier = Modifier.fillMaxWidth(),
    ) { Text(if (sent) "Comprobando..." else "Emparejar") }

    if (!ready) {
        Notice(
            "Esperando a Flex OS",
            "El reloj todavia no ha mandado su parte del emparejamiento. Comprueba que el " +
                "codigo sigue en su pantalla; si caduco, vuelve a pulsar \"Emparejar telefono\".",
            MaterialTheme.colorScheme.tertiary,
        )
    } else if (sent) {
        // El plazo lo lleva el servidor: si Flex OS no contesta,
        // manda un fallo con el motivo y esta pantalla pasa a ERROR.
        // Aqui solo se dice que la espera tiene final.
        Notice(
            "Comprobando en el reloj",
            "Flex OS esta verificando el codigo. Si no contesta en unos segundos, aqui " +
                "aparecera el motivo -- esta espera no se queda colgada.",
        )
    }
    Notice(
        "El codigo no se envia",
        "Se usa aqui para calcular la clave del vinculo. Por la red solo viaja la prueba " +
            "de que los dos habeis llegado a la misma.",
    )
}
