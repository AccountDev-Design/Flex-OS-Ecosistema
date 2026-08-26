package com.flexos.flexphone.ui.screens

import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import com.flexos.flexphone.domain.LinkState

/** Piezas de interfaz compartidas por varias pantallas. */

@Composable
fun SectionCard(title: String, content: @Composable ColumnScope.() -> Unit) {
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Text(title, style = MaterialTheme.typography.titleMedium)
            content()
        }
    }
}

@Composable
fun KeyValue(label: String, value: String, emphasis: Boolean = false) {
    Row(
        Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(label, style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant)
        Text(
            value,
            style = if (emphasis) MaterialTheme.typography.titleMedium
                    else MaterialTheme.typography.bodyMedium,
        )
    }
}

@Composable
fun SwitchRow(title: String, subtitle: String?, checked: Boolean, onChange: (Boolean) -> Unit) {
    Row(
        Modifier.fillMaxWidth().padding(vertical = 4.dp),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Column(Modifier.weight(1f)) {
            Text(title, style = MaterialTheme.typography.bodyLarge)
            subtitle?.let {
                Text(it, style = MaterialTheme.typography.labelMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant)
            }
        }
        Switch(checked = checked, onCheckedChange = onChange)
    }
}

/**
 * Texto y color del estado del enlace.
 *
 * "Conectado" en verde SOLO en READY. Cualquier otro estado se
 * enseria como lo que es: si no hay sesion, no se pinta de verde.
 */
@Composable
fun linkLabel(s: LinkState): Pair<String, Color> = when (s) {
    LinkState.READY -> "Conectado" to MaterialTheme.colorScheme.secondary
    LinkState.PAIRING -> "Emparejando" to MaterialTheme.colorScheme.tertiary
    LinkState.CONNECTING -> "Conectando" to MaterialTheme.colorScheme.tertiary
    LinkState.ADVERTISING -> "Buscando Flex OS" to MaterialTheme.colorScheme.onSurfaceVariant
    LinkState.OFF -> "Desactivado" to MaterialTheme.colorScheme.onSurfaceVariant
    LinkState.UNAVAILABLE -> "No disponible" to MaterialTheme.colorScheme.error
    LinkState.ERROR -> "Error" to MaterialTheme.colorScheme.error
}
