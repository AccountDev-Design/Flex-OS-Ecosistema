package com.flexos.flexphone.ui

import androidx.compose.foundation.layout.*
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
import com.flexos.flexphone.storage.SettingsStore
import com.flexos.flexphone.ui.screens.*
import kotlinx.coroutines.flow.collectLatest

/**
 * Navegacion de la app. Nueve pantallas, las que pide el diseno:
 * bienvenida, emparejamiento, estado, apps permitidas, privacidad,
 * Browser Relay, dispositivos vinculados, diagnostico y acerca de.
 *
 * La primera pantalla depende de si ya hay un dispositivo vinculado:
 * a quien ya emparejo no se le vuelve a ensenar la bienvenida cada
 * vez que abre la app.
 */
object Routes {
    const val WELCOME = "welcome"
    const val PAIR = "pair"
    const val STATUS = "status"
    const val APPS = "apps"
    const val PRIVACY = "privacy"
    const val RELAY = "relay"
    const val DEVICES = "devices"
    const val DIAGNOSTICS = "diagnostics"
    const val ABOUT = "about"
}

@Composable
fun FlexPhoneNav() {
    val nav = rememberNavController()
    val ctx = LocalContext.current
    val store = remember { SettingsStore(ctx) }
    val state = FlexPhoneState.instance

    // Los ajustes se OBSERVAN; no se sondean.
    var settings by remember { mutableStateOf(state?.settings ?: com.flexos.flexphone.domain.Settings()) }
    LaunchedEffect(Unit) { store.flow.collectLatest { settings = it } }

    val start = if (settings.isPaired) Routes.STATUS else Routes.WELCOME

    Scaffold(modifier = Modifier.fillMaxSize()) { pad ->
        NavHost(
            navController = nav,
            startDestination = start,
            modifier = Modifier.padding(pad),
        ) {
            composable(Routes.WELCOME) { WelcomeScreen(nav) }
            composable(Routes.PAIR) { PairScreen(nav, store) }
            composable(Routes.STATUS) { StatusScreen(nav, store, settings) }
            composable(Routes.APPS) { AllowedAppsScreen(nav, store, settings) }
            composable(Routes.PRIVACY) { PrivacyScreen(nav, store, settings) }
            composable(Routes.RELAY) { RelayScreen(nav, store, settings) }
            composable(Routes.DEVICES) { DevicesScreen(nav, store, settings) }
            composable(Routes.DIAGNOSTICS) { DiagnosticsScreen(nav) }
            composable(Routes.ABOUT) { AboutScreen(nav) }
        }
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
                        androidx.compose.material.icons.Icons.AutoMirrored.Filled.ArrowBack,
                        contentDescription = stringResource(R.string.back),
                    )
                }
            }
        },
    )
}
