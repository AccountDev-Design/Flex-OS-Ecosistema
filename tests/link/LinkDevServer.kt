// #############################################################
//  LinkDevServer  --  el TELEFONO del banco de integracion del enlace
//  ------------------------------------------------------------
//  Arranca el WifiLinkServer REAL (el del APK) ya vinculado con una clave
//  conocida y reproduce lo que FlexLinkService hace con sus eventos:
//
//     · al abrir sesion manda T_CAPS y T_PHONE_STATE;
//     · cada mensaje de aplicacion que llega se CUENTA por tipo.
//
//  Lo usa tests/host/link_e2e.sh contra la tarea de red REAL del P4
//  (FlexOS_FlexPhone_WiFi.h) compilada en el PC. Por la entrada estandar
//  acepta:  `dump` (imprime los contadores), `stop`, `start` y `quit`.
//
//  No es una copia de FlexLinkService (necesita Android): es lo minimo de el
//  que influye en el SOCKET. Lo que no se reproduce se dice en
//  docs/DIAGNOSTICO-ENLACE.md.
// #############################################################
import com.flexos.flexphone.link.WifiLinkServer
import com.flexos.flexphone.protocol.CapsPayload
import com.flexos.flexphone.protocol.FlexLink
import com.flexos.flexphone.protocol.PhoneState
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.atomic.AtomicInteger

private fun hex(s: String): ByteArray = ByteArray(s.length / 2) { s.substring(it * 2, it * 2 + 2).toInt(16).toByte() }

fun main(args: Array<String>) {
    val key = hex(args[0])
    val phoneId = if (args.size > 1) args[1] else "phone-prueba"
    val t0 = System.currentTimeMillis()
    fun log(s: String) { println("[%6d] %s".format(System.currentTimeMillis() - t0, s)); System.out.flush() }
    val rx = ConcurrentHashMap<Int, AtomicInteger>()
    val opened = AtomicInteger(0); val closed = AtomicInteger(0)
    lateinit var server: WifiLinkServer
    fun caps() = CapsPayload(
        supported = 0x3F, granted = 0x03, protoVer = FlexLink.VERSION,
        model = "A55", vendor = "samsung", osVer = "Android 15",
    )
    fun state() = PhoneState(name = "Telefono de prueba", battery = 37, charging = false, net = 2)
    server = WifiLinkServer(
        ctx = android.content.Context(),
        phoneId = phoneId, phoneName = "Telefono de prueba",
        bondKey = { key },
        onPaired = { _, _ -> },
        onEvent = { ev ->
            when (ev) {
                is WifiLinkServer.Event.SessionOpen -> {
                    log("EVENT SessionOpen #${opened.incrementAndGet()} peer=${ev.peer}")
                    // Lo mismo que FlexLinkService.onServerEvent(SessionOpen).
                    server.send(FlexLink.T_CAPS, caps().encode())
                    server.send(FlexLink.T_PHONE_STATE, state().encode())
                }
                is WifiLinkServer.Event.SessionClosed -> log("EVENT SessionClosed #${closed.incrementAndGet()}")
                is WifiLinkServer.Event.Error -> log("EVENT Error ${ev.message}")
                is WifiLinkServer.Event.PairingFailed -> log("EVENT PairingFailed ${ev.why}")
                else -> {}
            }
        },
        onMessage = { type, _ -> rx.computeIfAbsent(type) { AtomicInteger(0) }.incrementAndGet() },
    )
    if (!server.start()) { log("ERROR no arranco"); return }
    log("READY listening")
    while (true) {
        val line = readLine() ?: break
        when (line.trim()) {
            "dump" -> log("DUMP opened=${opened.get()} closed=${closed.get()} " +
                rx.entries.sortedBy { it.key }.joinToString(" ") { "t%02x=%d".format(it.key, it.value.get()) })
            "stop" -> { server.stop(); log("STOPPED") }
            "start" -> log("STARTED=${server.start()}")
            "quit" -> break
        }
    }
    server.stop()
}
