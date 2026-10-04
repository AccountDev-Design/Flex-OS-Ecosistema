package com.flexos.flexphone.ui.screens

import android.content.Intent
import android.os.PowerManager
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.navigation.NavController
import com.flexos.flexphone.flexcloud.FlexCloudPhone
import com.flexos.flexphone.flexcloud.FlexStorageService
import com.flexos.flexphone.flexcloud.StorageAttach
import com.flexos.flexphone.flexcloud.StorageAttachActivity
import com.flexos.flexphone.ui.FlexTopBar
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.withContext

/**
 * FLEX CLOUD EN ESTE TELEFONO (Flex Storage).
 *
 * El espacio que este telefono presta a Flex OS: si esta activo, que Flex OS
 * lo usa, cuanto ocupa DE VERDAD, la cuota que la persona decide (1, 2 o 5 GB)
 * y como dejar de compartir. Emparejar no se hace aqui: se empieza en la web de
 * Flex OS (en este telefono) y se aprueba en la pantalla de Flex OS.
 *
 * Misma regla que el resto de la app: nada "por si acaso". Si un dato no esta,
 * se dice; los numeros salen del servidor de Flex Cloud del propio telefono.
 */
@Composable
fun FlexCloudScreen(nav: NavController) {
    val ctx = LocalContext.current
    val s by FlexCloudPhone.status.collectAsState()
    // "Activado" se OBSERVA en el estado (FlexCloudPhone.setEnabled lo publica): antes se
    // leia de las preferencias cada 3 s, y tras emparejar la pantalla seguia diciendo
    // "Sin activar" hasta el siguiente refresco.
    val enabled = s.enabled
    val attach by StorageAttach.state.collectAsState()
    var linkText by remember { mutableStateOf("") }
    var quotaMsg by remember { mutableStateOf<String?>(null) }
    var confirmForget by remember { mutableStateOf(false) }
    var confirmWipe by remember { mutableStateOf(false) }
    var batteryOk by remember { mutableStateOf(true) }

    // Lecturas locales (disco y servidor del telefono), fuera del hilo de la
    // interfaz y solo mientras la pantalla esta a la vista.
    LaunchedEffect(Unit) {
        while (true) {
            withContext(Dispatchers.IO) { FlexCloudPhone.refresh(ctx) }
            val pm = ctx.getSystemService(PowerManager::class.java)
            batteryOk = pm?.isIgnoringBatteryOptimizations(ctx.packageName) ?: true
            delay(3_000)
        }
    }

    val active = s.running && s.lastContactMs > 0 && System.currentTimeMillis() - s.lastContactMs < 120_000
    val status = when {
        !s.paired -> FlexStatus.OFF
        s.running && active -> FlexStatus.OK
        s.running -> FlexStatus.BUSY
        enabled -> FlexStatus.BAD
        else -> FlexStatus.OFF
    }
    val statusText = when {
        !s.paired -> "Sin activar"
        s.running && active -> "Flex OS conectado"
        s.running -> "Esperando a Flex OS"
        enabled -> s.error ?: "Sin servidor"
        else -> "Detenido"
    }

    Scaffold(topBar = { FlexTopBar("Flex Cloud", onBack = { nav.popBackStack() }) }) { pad ->
        Column(
            Modifier.padding(pad).fillMaxSize().verticalScroll(rememberScrollState())
                .padding(horizontal = 16.dp, vertical = 8.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            // ---- quien y como esta ----
            GlassCard {
                Text(
                    if (s.paired) (s.p4Name ?: "Flex OS") else "Ningún Flex OS",
                    style = MaterialTheme.typography.titleLarge,
                    maxLines = 1, overflow = TextOverflow.Ellipsis,
                )
                Row(verticalAlignment = Alignment.CenterVertically) {
                    StatusDot(status)
                    Spacer(Modifier.width(8.dp))
                    Text(statusText, style = MaterialTheme.typography.bodyMedium, color = statusColor(status))
                }
                s.address?.let { KeyValue("Escuchando en", it) }
                if (s.lastContactMs > 0) KeyValue("Última petición de Flex OS", ago(s.lastContactMs))
                if (s.error != null && s.running) {
                    Text(s.error ?: "", style = MaterialTheme.typography.labelMedium, color = MaterialTheme.colorScheme.tertiary)
                }
                if (s.paired) {
                    SwitchRow(
                        "Compartir con Flex OS",
                        "Solo en tu Wi‑Fi y solo con este Flex OS",
                        checked = enabled,
                    ) { on ->
                        if (on) { FlexCloudPhone.setEnabled(ctx, true); FlexStorageService.start(ctx) }
                        else FlexStorageService.stop(ctx)
                    }
                }
            }

            // ---- un emparejamiento EN CURSO: se ve desde aqui aunque la pantalla del codigo se haya ido ----
            when (val a = attach) {
                is StorageAttach.UiState.Working, is StorageAttach.UiState.Code, is StorageAttach.UiState.Confirm -> {
                    Notice(
                        "Emparejando con Flex OS",
                        when (a) {
                            is StorageAttach.UiState.Code ->
                                "Código ${a.sas.take(3)} ${a.sas.drop(3)}: compáralo con el de la pantalla de ${a.p4Name} y acéptalo allí."
                            is StorageAttach.UiState.Confirm -> "Falta que confirmes que quieres compartir espacio con el Flex OS de ${a.p4Ip}."
                            else -> "Conectando con Flex OS…"
                        },
                    )
                    Button(
                        onClick = {
                            ctx.startActivity(Intent(ctx, StorageAttachActivity::class.java))
                        },
                        modifier = Modifier.fillMaxWidth(),
                    ) { Text("Ver el emparejamiento") }
                }
                is StorageAttach.UiState.Done -> Notice("Emparejado", "${a.p4Name} ya puede guardar archivos aquí.")
                is StorageAttach.UiState.Failed -> Notice(a.title, a.message, MaterialTheme.colorScheme.error)
                else -> {}
            }

            if (!s.paired) {
                Notice(
                    "Cómo activarlo",
                    "En este teléfono, abre la web de Flex OS (escanea el QR de Galería › Conectar con el móvil) " +
                        "y pulsa «Activar Flex Cloud en este teléfono». Después comprueba el código de 6 cifras " +
                        "y acéptalo en la pantalla de Flex OS.",
                )
                // ---- RESPALDO: si el navegador no abre Flex Phone, el enlace se pega aqui ----
                GlassCard {
                    Text("¿«Abrir Flex Phone» no hizo nada?", style = MaterialTheme.typography.titleMedium)
                    Text(
                        "Algunos navegadores no abren enlaces de aplicaciones. En la ventana de la web de Flex OS " +
                            "pulsa «Copiar enlace», pégalo aquí y sigue desde este teléfono.",
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                    androidx.compose.material3.OutlinedTextField(
                        value = linkText,
                        onValueChange = { linkText = it.take(300) },
                        singleLine = true,
                        label = { Text("Enlace de Flex OS (flexstorage://…)") },
                        modifier = Modifier.fillMaxWidth(),
                    )
                    Button(
                        onClick = {
                            if (StorageAttach.offerText(ctx, linkText)) linkText = ""
                            ctx.startActivity(Intent(ctx, StorageAttachActivity::class.java))
                        },
                        enabled = linkText.trim().startsWith("flexstorage://"),
                        modifier = Modifier.fillMaxWidth(),
                    ) { Text("Usar este enlace") }
                }
            }

            // ---- espacio ----
            SectionHeader("Espacio")
            GlassCard {
                val total = s.quotaGb.toLong() shl 30
                val taken = s.usedBytes + s.reservedBytes
                Text("${fmtBytes(taken)} de ${s.quotaGb} GB", style = MaterialTheme.typography.titleMedium)
                Meter(if (total > 0) taken.toFloat() / total.toFloat() else 0f)
                Text(
                    "Disponible: ${fmtBytes(s.availableBytes)}" +
                        if (s.limitedByDevice) " (lo que queda libre en el teléfono)" else "",
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                if (s.trashBytes > 0) {
                    Text(
                        "En la papelera: ${fmtBytes(s.trashBytes)} (ocupa hasta que se vacía, a los 30 días)",
                        style = MaterialTheme.typography.labelMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
                Text(
                    if (s.files == 1) "1 archivo" else "${s.files} archivos",
                    style = MaterialTheme.typography.labelMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                if (s.mediaWorking || s.mediaQueued > 0) {
                    // Lo que Flex OS no abre (H.264, PNG, MP3...) se prepara AQUI, en el teléfono, y no en el reloj.
                    Text(
                        "Preparando archivos para Flex OS" + if (s.mediaQueued > 0) " · ${s.mediaQueued} en cola" else "",
                        style = MaterialTheme.typography.labelMedium,
                        color = MaterialTheme.colorScheme.primary,
                    )
                }
                Text("Espacio que prestas a Flex OS", style = MaterialTheme.typography.bodyMedium)
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    for (gb in FlexCloudPhone.QUOTA_CHOICES_GB) {
                        if (gb == s.quotaGb) {
                            Button(onClick = {}) { Text("$gb GB") }
                        } else {
                            OutlinedButton(onClick = {
                                quotaMsg = FlexCloudPhone.setQuotaGb(ctx, gb)
                            }) { Text("$gb GB") }
                        }
                    }
                }
                quotaMsg?.let {
                    Text(it, style = MaterialTheme.typography.labelMedium, color = MaterialTheme.colorScheme.error)
                }
            }

            if (s.paired && !batteryOk) {
                Notice(
                    "Con la pantalla apagada",
                    "Android puede dormir la Wi‑Fi de las apps para ahorrar batería, y entonces Flex OS no " +
                        "llegaría al teléfono. Si quieres que funcione siempre, quita la optimización de batería " +
                        "de Flex Phone.",
                    MaterialTheme.colorScheme.tertiary,
                )
                OutlinedButton(
                    onClick = {
                        runCatching {
                            ctx.startActivity(
                                Intent(android.provider.Settings.ACTION_IGNORE_BATTERY_OPTIMIZATION_SETTINGS)
                                    .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                            )
                        }
                    },
                    modifier = Modifier.fillMaxWidth(),
                ) { Text("Abrir los ajustes de batería") }
            }

            Notice(
                "Qué ve Flex OS",
                "Solo la carpeta privada de Flex Cloud de esta app. Ni tus fotos, ni tus archivos, ni otras " +
                    "aplicaciones. El servidor solo acepta conexiones de tu red local y con la sesión del Flex OS " +
                    "emparejado; los enlaces para ver un archivo caducan a los 15 minutos. Los archivos viajan " +
                    "SIN CIFRAR por tu Wi‑Fi, como el resto del enlace con Flex OS: úsalo en una red de confianza.",
            )

            if (s.paired) {
                OutlinedButton(onClick = { confirmForget = true }, modifier = Modifier.fillMaxWidth()) {
                    Text("Olvidar este Flex OS")
                }
            }
            if (s.files > 0 || s.usedBytes > 0) {
                TextButton(onClick = { confirmWipe = true }, modifier = Modifier.fillMaxWidth()) {
                    Text("Borrar todo lo de Flex Cloud", color = MaterialTheme.colorScheme.error)
                }
            }
            Spacer(Modifier.height(16.dp))
        }
    }

    if (confirmForget) {
        AlertDialog(
            onDismissRequest = { confirmForget = false },
            title = { Text("¿Olvidar este Flex OS?") },
            text = {
                Text(
                    "Se borra la clave del emparejamiento y el teléfono deja de compartir espacio. " +
                        "Tus archivos de Flex Cloud se quedan aquí hasta que los borres."
                )
            },
            confirmButton = {
                TextButton(onClick = {
                    FlexStorageService.stop(ctx)
                    FlexCloudPhone.forget(ctx)
                    confirmForget = false
                }) { Text("Olvidar") }
            },
            dismissButton = { TextButton(onClick = { confirmForget = false }) { Text("Cancelar") } },
        )
    }
    if (confirmWipe) {
        AlertDialog(
            onDismissRequest = { confirmWipe = false },
            title = { Text("¿Borrar todo lo de Flex Cloud?") },
            text = {
                Text(
                    "Se borran de este teléfono todos los archivos de Flex Cloud (${fmtBytes(s.usedBytes)}), " +
                        "también los de la papelera. No se puede deshacer."
                )
            },
            confirmButton = {
                TextButton(onClick = {
                    val wasOn = enabled
                    FlexStorageService.stop(ctx)
                    FlexCloudPhone.wipe(ctx)
                    if (wasOn && s.paired) { FlexCloudPhone.setEnabled(ctx, true); FlexStorageService.start(ctx) }
                    confirmWipe = false
                }) { Text("Borrar", color = MaterialTheme.colorScheme.error) }
            },
            dismissButton = { TextButton(onClick = { confirmWipe = false }) { Text("Cancelar") } },
        )
    }
}

/** "1,5 KB", "160 MB", "5 GB": como fclFmtBytes del P4 (base 1024, coma decimal). */
internal fun fmtBytes(n: Long): String {
    if (n < 1024) return "${n.coerceAtLeast(0)} B"
    val u = arrayOf("KB", "MB", "GB", "TB")
    var v = n / 1024.0
    var i = 0
    while (v >= 1024 && i < 3) { v /= 1024; i++ }
    var s = if (v >= 100) "%.0f".format(java.util.Locale.ROOT, v) else "%.1f".format(java.util.Locale.ROOT, v)
    s = s.replace('.', ',')
    if (s.endsWith(",0")) s = s.dropLast(2)
    return "$s ${u[i]}"
}

private fun ago(ms: Long): String {
    val s = ((System.currentTimeMillis() - ms) / 1000).coerceAtLeast(0)
    return when {
        s < 60 -> "hace $s s"
        s < 3600 -> "hace ${s / 60} min"
        s < 86_400 -> "hace ${s / 3600} h"
        else -> "hace ${s / 86_400} días"
    }
}
