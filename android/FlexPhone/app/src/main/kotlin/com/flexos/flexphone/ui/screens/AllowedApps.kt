package com.flexos.flexphone.ui.screens

import android.content.pm.ApplicationInfo
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.navigation.NavController
import com.flexos.flexphone.domain.Settings
import com.flexos.flexphone.storage.SettingsStore
import com.flexos.flexphone.ui.FlexTopBar
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

/**
 * Que aplicaciones pueden mandar sus notificaciones.
 *
 * DE FABRICA NO HAY NINGUNA. Es deliberado: una app que empieza
 * reenviando TODO a otro dispositivo es una fuga de privacidad por
 * defecto. El usuario elige, una por una.
 */
@Composable
fun AllowedAppsScreen(nav: NavController, store: SettingsStore, settings: Settings) {
    val ctx = LocalContext.current
    val scope = rememberCoroutineScope()
    var apps by remember { mutableStateOf<List<Pair<String, String>>>(emptyList()) }
    var query by remember { mutableStateOf("") }

    LaunchedEffect(Unit) {
        apps = withContext(Dispatchers.IO) {
            val pm = ctx.packageManager
            pm.getInstalledApplications(0)
                // Se ocultan las apps del sistema sin lanzador: no
                // generan notificaciones que le interesen a nadie y
                // solo alargan la lista.
                .filter { it.flags and ApplicationInfo.FLAG_SYSTEM == 0 ||
                          pm.getLaunchIntentForPackage(it.packageName) != null }
                .map { it.packageName to pm.getApplicationLabel(it).toString() }
                .sortedBy { it.second.lowercase() }
        }
    }

    val shown = remember(apps, query) {
        if (query.isBlank()) apps
        else apps.filter { it.second.contains(query, true) || it.first.contains(query, true) }
    }

    Column(Modifier.fillMaxSize()) {
        FlexTopBar("Aplicaciones", onBack = { nav.popBackStack() })
        OutlinedTextField(
            value = query, onValueChange = { query = it },
            label = { Text("Buscar") },
            singleLine = true,
            modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 8.dp),
        )
        Text(
            "Solo se envian las notificaciones de las apps que marques.",
            style = MaterialTheme.typography.labelMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            modifier = Modifier.padding(horizontal = 16.dp),
        )
        if (apps.isEmpty()) {
            Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                CircularProgressIndicator()
            }
            return@Column
        }
        LazyColumn(Modifier.fillMaxSize().padding(horizontal = 16.dp)) {
            items(shown, key = { it.first }) { (pkg, label) ->
                val checked = settings.allowedPackages.contains(pkg)
                SwitchRow(
                    title = label, subtitle = pkg, checked = checked,
                    onChange = { on ->
                        scope.launch {
                            store.update { s ->
                                s.copy(
                                    allowedPackages = if (on) s.allowedPackages + pkg
                                                      else s.allowedPackages - pkg
                                )
                            }
                        }
                    },
                )
                HorizontalDivider()
            }
        }
    }
}
