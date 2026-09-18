package com.flexos.flexphone.ui.screens

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.flexos.flexphone.domain.LinkState

/**
 * COMPONENTES COMPARTIDOS.
 *
 * El equivalente Android de `FlexOS_FlexPhone_UI.h`: las mismas
 * piezas (tarjeta, pildora de estado, fila, estado vacio, aviso) con
 * la misma semantica. Las dos mitades del ecosistema tienen que
 * parecer el mismo producto, y eso no se consigue repitiendo estilos
 * pantalla por pantalla.
 *
 * El "vidrio" aqui es una tarjeta con un degradado muy suave y un
 * borde fino. NO es un desenfoque a pantalla completa: en un movil
 * se podria, pero se veria distinto del P4, donde el desenfoque caro
 * no cabe en el presupuesto de cuadro.
 */

// =============================================================
//  Semantica de estado -- IDENTICA a la del firmware
// =============================================================
enum class FlexStatus { OK, BUSY, BAD, OFF }

@Composable
fun statusColor(s: FlexStatus): Color = when (s) {
    FlexStatus.OK -> MaterialTheme.colorScheme.secondary
    FlexStatus.BUSY -> MaterialTheme.colorScheme.tertiary
    FlexStatus.BAD -> MaterialTheme.colorScheme.error
    FlexStatus.OFF -> MaterialTheme.colorScheme.onSurfaceVariant
}

/**
 * El glifo del estado. Va con FORMA ademas de color: circulo lleno,
 * anillo, aspa y guion. Quien no distingue el verde del rojo tiene
 * que poder leer la pantalla igual.
 */
@Composable
fun StatusDot(s: FlexStatus, size: Int = 12) {
    val c = statusColor(s)
    when (s) {
        FlexStatus.OK -> Box(Modifier.size(size.dp).clip(CircleShape).background(c))
        FlexStatus.BUSY -> Box(
            Modifier.size(size.dp).clip(CircleShape).border(3.dp, c, CircleShape)
        )
        FlexStatus.BAD -> Text("✕", color = c, style = MaterialTheme.typography.labelMedium)
        FlexStatus.OFF -> Box(
            Modifier.width(size.dp).height((size / 3).coerceAtLeast(3).dp)
                .clip(RoundedCornerShape(2.dp)).background(c)
        )
    }
}

fun linkStatus(s: LinkState): FlexStatus = when (s) {
    LinkState.READY -> FlexStatus.OK
    LinkState.PAIRING, LinkState.CONNECTING, LinkState.ADVERTISING -> FlexStatus.BUSY
    LinkState.ERROR, LinkState.UNAVAILABLE -> FlexStatus.BAD
    LinkState.OFF -> FlexStatus.OFF
}

/**
 * Texto del estado del enlace. UNA sola forma de decir cada cosa:
 * mezclar "sin conexion", "desconectado" y "no hay Flex OS" para el
 * mismo estado confunde mas de lo que ayuda.
 */
fun linkText(s: LinkState): String = when (s) {
    LinkState.READY -> "Conectado"
    LinkState.PAIRING -> "Emparejando"
    LinkState.CONNECTING -> "Conectando"
    LinkState.ADVERTISING -> "Esperando a Flex OS"
    LinkState.OFF -> "Enlace apagado"
    LinkState.UNAVAILABLE -> "No disponible"
    LinkState.ERROR -> "Error"
}

@Composable
fun linkLabel(s: LinkState): Pair<String, Color> = linkText(s) to statusColor(linkStatus(s))

// =============================================================
//  Tarjetas
// =============================================================
@Composable
fun GlassCard(
    modifier: Modifier = Modifier,
    accent: Color? = null,
    content: @Composable ColumnScope.() -> Unit,
) {
    val scheme = MaterialTheme.colorScheme
    Box(
        modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(20.dp))
            .background(
                Brush.verticalGradient(
                    listOf(
                        scheme.surface,
                        scheme.surfaceVariant.copy(alpha = 0.55f),
                    )
                )
            )
            .border(1.dp, scheme.outline.copy(alpha = 0.35f), RoundedCornerShape(20.dp))
    ) {
        // Franja de acento a la izquierda, como en el P4: destaca sin
        // cambiar el fondo de la tarjeta.
        if (accent != null) {
            Box(
                Modifier.padding(start = 6.dp, top = 14.dp, bottom = 14.dp)
                    .width(4.dp).fillMaxHeight()
                    .clip(RoundedCornerShape(2.dp)).background(accent)
            )
        }
        Column(
            Modifier.padding(start = if (accent != null) 20.dp else 16.dp, top = 16.dp,
                             end = 16.dp, bottom = 16.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp),
            content = content,
        )
    }
}

@Composable
fun SectionCard(title: String, content: @Composable ColumnScope.() -> Unit) {
    GlassCard {
        Text(title, style = MaterialTheme.typography.titleMedium)
        content()
    }
}

@Composable
fun SectionHeader(text: String) {
    Text(
        text.uppercase(),
        style = MaterialTheme.typography.labelMedium,
        color = MaterialTheme.colorScheme.onSurfaceVariant,
        modifier = Modifier.padding(start = 4.dp, top = 4.dp),
    )
}

// =============================================================
//  Filas
// =============================================================
@Composable
fun KeyValue(label: String, value: String, emphasis: Boolean = false) {
    Row(
        Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(
            label,
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            modifier = Modifier.weight(1f, fill = false),
        )
        Spacer(Modifier.width(12.dp))
        Text(
            value,
            style = if (emphasis) MaterialTheme.typography.titleMedium
                    else MaterialTheme.typography.bodyMedium,
            maxLines = 2,
            overflow = TextOverflow.Ellipsis,
            modifier = Modifier.weight(1f, fill = false),
        )
    }
}

/** Fila de estado: glifo, nombre y detalle. La usa el diagnostico. */
@Composable
fun StatusRow(name: String, status: FlexStatus, detail: String?) {
    Row(
        Modifier.fillMaxWidth().padding(vertical = 2.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        StatusDot(status)
        Spacer(Modifier.width(12.dp))
        Text(name, style = MaterialTheme.typography.bodyMedium, modifier = Modifier.weight(1f))
        if (!detail.isNullOrBlank()) {
            Text(
                detail,
                style = MaterialTheme.typography.labelMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                maxLines = 2,
                overflow = TextOverflow.Ellipsis,
                modifier = Modifier.weight(1.4f),
            )
        }
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
        Spacer(Modifier.width(12.dp))
        Switch(checked = checked, onCheckedChange = onChange)
    }
}

/** Fila de navegacion a otra pantalla. */
@Composable
fun NavRow(title: String, subtitle: String?, enabled: Boolean = true, onClick: () -> Unit) {
    GlassCard(
        Modifier.then(if (enabled) Modifier.clickable(onClick = onClick) else Modifier)
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f)) {
                Text(
                    title,
                    style = MaterialTheme.typography.titleMedium,
                    color = if (enabled) MaterialTheme.colorScheme.onSurface
                            else MaterialTheme.colorScheme.onSurfaceVariant,
                    maxLines = 1, overflow = TextOverflow.Ellipsis,
                )
                subtitle?.let {
                    Text(
                        it,
                        style = MaterialTheme.typography.labelMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        maxLines = 2, overflow = TextOverflow.Ellipsis,
                    )
                }
            }
            Text("›", style = MaterialTheme.typography.titleLarge,
                color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
    }
}

// =============================================================
//  Estado vacio y avisos
// =============================================================
/**
 * Toda lista que puede estar vacia tiene que decir POR QUE lo esta.
 * Una pantalla en blanco parece un fallo.
 */
@Composable
fun EmptyState(title: String, why: String) {
    Column(
        Modifier.fillMaxWidth().padding(vertical = 40.dp, horizontal = 24.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(10.dp),
    ) {
        Box(
            Modifier.size(56.dp).clip(CircleShape)
                .background(MaterialTheme.colorScheme.surfaceVariant),
            contentAlignment = Alignment.Center,
        ) { StatusDot(FlexStatus.OFF, 18) }
        Text(title, style = MaterialTheme.typography.titleMedium)
        Text(
            why,
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
    }
}

@Composable
fun Notice(title: String, body: String, accent: Color = MaterialTheme.colorScheme.primary) {
    GlassCard(accent = accent) {
        Text(title, style = MaterialTheme.typography.titleMedium, color = accent)
        Text(body, style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant)
    }
}

/** Barra de medida: bateria, almacenamiento, memoria. */
@Composable
fun Meter(fraction: Float, color: Color = MaterialTheme.colorScheme.primary) {
    LinearProgressIndicator(
        progress = { fraction.coerceIn(0f, 1f) },
        modifier = Modifier.fillMaxWidth().height(8.dp).clip(RoundedCornerShape(4.dp)),
        color = color,
        trackColor = MaterialTheme.colorScheme.surfaceVariant,
    )
}
