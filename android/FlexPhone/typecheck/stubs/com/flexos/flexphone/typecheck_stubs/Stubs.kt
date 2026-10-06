// Dobles minimos para comprobar tipos sin el SDK: en el APK, R lo genera AGP
// y MainActivity es la de verdad (Compose). R se mantiene A MANO aqui: cada
// recurso nuevo que use el codigo Kotlin tiene que anadirse (si falta, la
// comprobacion de tipos lo dice).
package com.flexos.flexphone

object R {
    object drawable {
        const val ic_flexcloud: Int = 0
        const val ic_link: Int = 1
        const val ic_relay: Int = 2
        const val ic_stop: Int = 3
    }
    object string {
        const val app_name: Int = 0
        const val back: Int = 1
        const val channel_link: Int = 2
        const val channel_link_desc: Int = 3
        const val channel_relay: Int = 4
        const val channel_relay_desc: Int = 5
        const val link_connected: Int = 6
        const val link_connecting: Int = 7
        const val link_error: Int = 8
        const val link_needs_wifi: Int = 9
        const val link_off: Int = 10
        const val link_pairing: Int = 11
        const val link_reconnecting: Int = 12
        const val link_searching: Int = 13
        const val link_stopped_by_system: Int = 14
        const val link_unavailable: Int = 15
        const val relay_active: Int = 16
        const val relay_battery_note: Int = 17
        const val relay_client_gone: Int = 18
        const val relay_no_wifi: Int = 19
        const val relay_starting: Int = 20
        const val relay_suspended: Int = 21
        const val relay_title: Int = 22
        const val relay_waiting: Int = 23
        const val stop: Int = 24
        const val relay_reconnecting: Int = 25
        const val relay_keeps_running: Int = 26
    }
}

class MainActivity : android.app.Activity()
