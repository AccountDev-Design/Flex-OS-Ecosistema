package com.flexos.flexphone.ui

import androidx.compose.foundation.layout.*
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.rememberNavController
import com.flexos.flexphone.R
import com.flexos.flexphone.domain.FlexPhoneState
import com.flexos.flexphone.storage.BondStore
import com.flexos.flexphone.storage.SettingsStore
import com.flexos.flexphone.ui.screens.*
import kotlinx.coroutines.flow.collectLatest

/**
 * NAVEGACION DE LA APP.
 *
 * Una PORTADA y las secciones colgando de ella -- la misma forma que
 * en Flex OS. Las dos mitades del ecosistema tienen que poder
 * explicarse con el mismo mapa.
 *
 * La primera pantalla depende de si ya hay un Flex OS vinculado: a
 * quien ya emparejo no se le vuelve a ensenar la bienvenida.
 */
object Routes {
    const val WELCOME = "welcome"
    const val HOME = "home"
    const val PAIR = "pair"
    const val APPS = "apps"
    const val PRIVACY = "privacy"
    const val RELAY = "relay"
    const val CONNECTION = "connection"
    const val DEVICE = "device"
    const val SECURITY = "security"
    const val DIAGNOSTICS = "diagnostics"
    const val SETTINGS = "settings"
    const val ABOUT = "about"
}

@Composable
fun FlexPhoneNav() {
    val nav = rememberNavController()
    val ctx = LocalContext.current
    val store = remember { SettingsStore(ctx) }
    val bonds = remember { BondStore(ctx) }
    val state = FlexPhoneState.instance

    // Los ajustes se OBSERVAN; no se sondean.
    var settings by remember { mutableStateOf(state?.settings ?: com.flexos.flexphone.domain.Settings()) }
    LaunchedEffect(Unit) { store.flow.collectLatest { settings = it } }

    // La verdad sobre el vinculo la tiene BondStore, que es quien
    // guarda la clave. Preguntar a los ajustes daria "emparejado" con
    // un vinculo cuya clave ya no se puede descifrar.
    val start = if (bonds.isPaired()) Routes.HOME else Routes.WELCOME

    NavHost(navController = nav, startDestination = start, modifier = Modifier.fillMaxSize()) {
        composable(Routes.WELCOME) { WelcomeScreen(nav) }
        composable(Routes.HOME) { HomeScreen(nav, store, settings) }
        composable(Routes.PAIR) { PairScreen(nav, store) }
        composable(Routes.APPS) { AllowedAppsScreen(nav, store, settings) }
        composable(Routes.PRIVACY) { PrivacyScreen(nav, store, settings) }
        composable(Routes.RELAY) { RelayScreen(nav, store, settings) }
        composable(Routes.CONNECTION) { ConnectionScreen(nav) }
        composable(Routes.DEVICE) { DeviceScreen(nav) }
        composable(Routes.SECURITY) { SecurityScreen(nav, store, settings) }
        composable(Routes.DIAGNOSTICS) { DiagnosticsScreen(nav) }
        composable(Routes.SETTINGS) { SettingsScreen(nav, store, settings) }
        composable(Routes.ABOUT) { AboutScreen(nav) }
    }
}

/** Cabecera comun con boton de volver. */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun FlexTopBar(title: String, onBack: (() -> Unit)? = null) {
    TopAppBar(
        title = { Text(title) },
        navigationIcon = {
            if (onBack != null) {
                IconButton(onClick = onBack) {
                    Icon(
                        Icons.AutoMirrored.Filled.ArrowBack,
                        contentDescription = stringResource(R.string.back),
                    )
                }
            }
        },
    )
}
