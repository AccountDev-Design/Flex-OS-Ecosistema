package com.flexos.flexphone.ui.theme

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Typography
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.sp

/**
 * Identidad visual de Flex OS llevada a Material 3.
 *
 * El azul de enlace (0xFF2684FF) es EL MISMO que el del icono de Flex
 * Phone en el P4: las dos mitades del ecosistema tienen que parecer
 * la misma cosa.
 *
 * NO se usa color dinamico de Android 12 aunque este disponible: aqui
 * la marca del sistema pesa mas que el fondo de pantalla del
 * telefono, y asi la app se ve igual en los dos dispositivos.
 */
private val FlexBlue = Color(0xFF2684FF)
private val FlexBlueDark = Color(0xFF0F5FD8)
private val FlexTeal = Color(0xFF20AB7E)
private val FlexAmber = Color(0xFFE0A200)
private val FlexRed = Color(0xFFD64545)

private val LightColors = lightColorScheme(
    primary = FlexBlue, onPrimary = Color.White,
    primaryContainer = Color(0xFFD9E9FF), onPrimaryContainer = Color(0xFF07264F),
    secondary = FlexTeal, onSecondary = Color.White,
    tertiary = FlexAmber, error = FlexRed,
    background = Color(0xFFF7F8FA), onBackground = Color(0xFF14161A),
    surface = Color.White, onSurface = Color(0xFF14161A),
    surfaceVariant = Color(0xFFEDEFF3), onSurfaceVariant = Color(0xFF454A52),
    outline = Color(0xFFC3C7CE),
)

private val DarkColors = darkColorScheme(
    primary = FlexBlue, onPrimary = Color(0xFF04203F),
    primaryContainer = FlexBlueDark, onPrimaryContainer = Color(0xFFD9E9FF),
    secondary = FlexTeal, onSecondary = Color(0xFF00281B),
    tertiary = FlexAmber, error = Color(0xFFFF8A80),
    background = Color(0xFF101215), onBackground = Color(0xFFE4E6EA),
    surface = Color(0xFF181B1F), onSurface = Color(0xFFE4E6EA),
    surfaceVariant = Color(0xFF262A30), onSurfaceVariant = Color(0xFFB4BAC3),
    outline = Color(0xFF3C424A),
)

private val FlexTypography = Typography(
    headlineMedium = TextStyle(fontSize = 26.sp, fontWeight = FontWeight.SemiBold),
    titleLarge = TextStyle(fontSize = 20.sp, fontWeight = FontWeight.SemiBold),
    titleMedium = TextStyle(fontSize = 16.sp, fontWeight = FontWeight.Medium),
    bodyLarge = TextStyle(fontSize = 15.sp),
    bodyMedium = TextStyle(fontSize = 14.sp),
    labelMedium = TextStyle(fontSize = 12.sp, fontWeight = FontWeight.Medium),
)

@Composable
fun FlexPhoneTheme(
    dark: Boolean = isSystemInDarkTheme(),
    content: @Composable () -> Unit,
) {
    MaterialTheme(
        colorScheme = if (dark) DarkColors else LightColors,
        typography = FlexTypography,
        content = content,
    )
}
