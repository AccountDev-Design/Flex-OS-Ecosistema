// #############################################################
//  ESTABILIDAD DE LA SESION  --  WifiLinkServer contra sockets REALES
//  ------------------------------------------------------------
//  Existe por un fallo concreto que no se podia ver de ninguna otra
//  forma: despues de emparejar, el enlace entraba en un ciclo
//
//      CONECTADO -> DESCONECTADO -> CONECTADO -> ...
//
//  aproximadamente cada segundo, indefinidamente.
//
//  QUE PASABA
//  ----------
//  Todo el estado de la conexion vivia en campos COMPARTIDOS del
//  servidor, y la limpieza de una conexion borraba "la sesion
//  activa" fuera de quien fuera:
//
//    1. muere el socket A (basta un hipo de Wi-Fi);
//    2. el reloj reconecta un segundo despues -> socket B;
//    3. el hilo de A sigue dentro de read() (hasta 2 s), asi que
//       el accept() ve "ya hay sesion" y CIERRA B en el acto;
//    4. el reloj vuelve a conectar -> socket C;
//    5. el hilo de A por fin despierta y su finally cierra "la
//       sesion"... que ya es C.
//
//  Los pasos 3 y 5 se rearman entre si. El ciclo se sostiene solo.
//
//  POR QUE ESTA BATERIA Y NO UNA DE ANDROID
//  ----------------------------------------
//  Es una carrera entre hilos y sockets. Reproducirla a mano en el
//  telefono depende de la suerte; aqui se provoca a proposito --
//  se corta el socket y se reconecta ANTES de que el hilo anterior
//  despierte -- y el resultado es determinista.
//
//  El codigo bajo prueba es el REAL, el que va en el APK: solo se
//  doblan Context, WifiManager y Log (ver stub/).
// #############################################################
import com.flexos.flexphone.link.WifiLinkServer
import com.flexos.flexphone.protocol.FlexAuth
import com.flexos.flexphone.protocol.FlexLink
import com.flexos.flexphone.protocol.PayloadReader
import com.flexos.flexphone.protocol.PayloadWriter
import java.io.DataInputStream
import java.net.InetAddress
import java.net.Socket
import java.util.concurrent.atomic.AtomicInteger

private var run = 0
private var fail = 0

private fun check(cond: Boolean, msg: String) {
    run++
    if (!cond) { fail++; println("  FALLO  $msg") }
}

/**
 * Espera a que algo se cumpla, con tope.
 *
 * El apreton de manos termina cuando el RELOJ manda su prueba; que el
 * servidor la procese lleva unos milisegundos mas. Sondear con tope es
 * lo correcto: un `sleep` fijo o falla en una maquina lenta o alarga
 * la bateria sin motivo.
 */
private fun waitUntil(ms: Long, cond: () -> Boolean): Boolean {
    val until = System.currentTimeMillis() + ms
    while (System.currentTimeMillis() < until) {
        if (cond()) return true
        Thread.sleep(20)
    }
    return cond()
}

// =============================================================
//  UN FLEX OS DE MENTIRA, pero con el protocolo DE VERDAD
// =============================================================
// Habla Flex Link byte a byte: HELLO, reto-respuesta mutuo sobre la
// clave del vinculo y latido. No es un eco.
private class FakeWatch(val port: Int, val key: ByteArray, val hostId: String = "flexos-prueba") {
    val sock = Socket(InetAddress.getByName("127.0.0.1"), port)
    private val inp = DataInputStream(sock.getInputStream())
    private val out = sock.getOutputStream()
    private var counter = 0L
    private var packet = 1
    var session = 0; private set

    init { sock.tcpNoDelay = true; sock.soTimeout = 5_000 }

    /**
     * Escribe una trama. Devuelve false si el socket ya no admite.
     *
     * No lanza: una bateria que se va en una traza a mitad esconde
     * los fallos que vengan despues, y aqui lo que interesa es
     * verlos TODOS.
     */
    fun send(type: Int, body: ByteArray, sess: Int): Boolean = try {
        counter++
        val frame = FlexLink.writeFrame(
            FlexLink.Header(type = type, session = sess, packet = packet++,
                            frag = 0, fragCount = 1, counter = counter),
            body,
        )
        out.write(frame); out.flush(); true
    } catch (e: Exception) { false }

    /** Lee UNA trama completa. Devuelve null si el socket murio. */
    fun recv(): Pair<FlexLink.Header, ByteArray>? = try {
        val hdr = ByteArray(FlexLink.HDR_SIZE)
        inp.readFully(hdr)
        val len = (hdr[10].toInt() and 0xFF) or ((hdr[11].toInt() and 0xFF) shl 8)
        val full = ByteArray(FlexLink.HDR_SIZE + len)
        hdr.copyInto(full)
        if (len > 0) inp.readFully(full, FlexLink.HDR_SIZE, len)
        val r = FlexLink.readFrame(full)
        if (r is FlexLink.ReadResult.Ok) Pair(r.header, r.payload) else null
    } catch (e: Exception) { null }

    /**
     * El apreton de manos COMPLETO de un reloj ya vinculado.
     * Devuelve true solo si el telefono demostro tambien su clave.
     */
    fun handshake(): Boolean {
        val w = PayloadWriter(); w.u8(FlexLink.VERSION); w.str(hostId, FlexAuth.ID_MAX - 1)
        send(FlexLink.T_HELLO, w.build(), 0)
        // WELCOME (ignorando latidos que puedan cruzarse)
        var got = recv() ?: return false
        while (got.first.type != FlexLink.T_WELCOME) got = recv() ?: return false

        session = 0x4242
        val nonce = ByteArray(FlexAuth.NONCE_SIZE) { (it * 7 + 3).toByte() }
        val ch = PayloadWriter(); ch.bytes(nonce); ch.u16(session)
        send(FlexLink.T_AUTH_CHALLENGE, ch.build(), session)

        var resp = recv() ?: return false
        while (resp.first.type != FlexLink.T_AUTH_RESPONSE) resp = recv() ?: return false
        val proof = PayloadReader(resp.second).bytes(FlexAuth.PROOF_SIZE)
        if (!FlexAuth.verify(key, FlexAuth.ROLE_PHONE, nonce, session, proof)) return false

        send(FlexLink.T_AUTH_OK, FlexAuth.proof(key, FlexAuth.ROLE_HOST, nonce, session), session)
        return true
    }

    fun ping(): Boolean = send(FlexLink.T_PING, ByteArray(0), session)
    fun close() = runCatching { sock.close() }
    val alive: Boolean get() = !sock.isClosed && sock.isConnected
}

// =============================================================
//  El servidor bajo prueba
// =============================================================
private class Harness {
    val key = ByteArray(FlexAuth.KEY_SIZE) { (it + 1).toByte() }
    val opened = AtomicInteger(0)
    val closed = AtomicInteger(0)
    @Volatile var lastError: String? = null

    val server = WifiLinkServer(
        ctx = android.content.Context(),
        phoneId = "phone-prueba",
        phoneName = "Telefono de prueba",
        bondKey = { key },
        onPaired = { _, _ -> },
        onEvent = { ev ->
            when (ev) {
                is WifiLinkServer.Event.SessionOpen -> opened.incrementAndGet()
                is WifiLinkServer.Event.SessionClosed -> closed.incrementAndGet()
                is WifiLinkServer.Event.Error -> lastError = ev.message
                else -> {}
            }
        },
        onMessage = { _, _ -> },
    )

    fun start(): Boolean = server.start()
    fun stop() = server.stop()
    fun watch() = FakeWatch(WifiLinkServer.TCP_PORT, key)
}

// =============================================================
//  1) Una sesion normal se queda quieta
// =============================================================
private fun testSesionEstable(h: Harness) {
    println("[enlace] una sesion sana no se cae sola")
    val w = h.watch()
    check(w.handshake(), "el apreton de manos no se completo")
    check(waitUntil(2_000) { h.server.hasSession() }, "no quedo sesion abierta tras autenticar")

    val cerradasAntes = h.closed.get()
    // Ocho segundos de latido, que es mas que POLL_MS y que varias
    // vueltas del bucle de lectura.
    repeat(8) {
        w.ping()
        val r = w.recv()
        check(r != null && r.first.type == FlexLink.T_PONG, "no contesto al latido")
        Thread.sleep(1_000)
    }
    check(h.server.hasSession(), "la sesion se cayo sola estando sana")
    check(h.closed.get() == cerradasAntes, "cerro la sesion sin motivo")
    check(w.alive, "el telefono cerro el socket de una sesion sana")
    w.close()
}

// =============================================================
//  2) LA REGRESION: reconectar no puede matar a quien llega
// =============================================================
// Se corta el socket y se reconecta EN EL ACTO, sin darle tiempo al
// hilo anterior a despertar de su read() (POLL_MS = 2 s). Con el
// fallo, la limpieza del hilo viejo cerraba la conexion nueva.
private fun testReconexionInmediata(h: Harness) {
    println("[enlace] reconectar en el acto NO mata a la conexion nueva")
    val vieja = h.watch()
    check(vieja.handshake(), "el primer apreton de manos no se completo")
    check(waitUntil(2_000) { h.server.hasSession() }, "no quedo la primera sesion")

    // Se corta en seco, como un Wi-Fi que parpadea.
    vieja.close()

    // Y el reloj vuelve YA, mucho antes de que el hilo viejo despierte.
    val nueva = h.watch()
    check(nueva.handshake(), "el segundo apreton de manos no se completo")
    check(waitUntil(2_000) { h.server.hasSession() },
        "el reloj no pudo volver a entrar: el hueco lo guardaba el socket muerto")

    // AQUI moria antes: el `finally` del hilo viejo despierta pasados
    // los 2 s del read() y cerraba "la sesion activa", que ya era esta.
    Thread.sleep(3_500)
    check(h.server.hasSession(),
        "LA LIMPIEZA DE LA CONEXION VIEJA MATO A LA NUEVA (el bucle de 1 s)")
    check(nueva.alive, "el socket nuevo aparece cerrado")

    // Y sigue respondiendo de verdad, no solo "parece viva".
    nueva.ping()
    val r = nueva.recv()
    check(r != null && r.first.type == FlexLink.T_PONG,
        "la conexion superviviente ya no contesta")
    nueva.close()
    Thread.sleep(2_500)
}

// =============================================================
//  3) El ciclo completo que veia el usuario
// =============================================================
// Seis reconexiones seguidas al ritmo al que reintenta el reloj
// (1 Hz). Al final tiene que quedar UNA sesion viva y estable.
private fun testCicloDeUnSegundo(h: Harness) {
    println("[enlace] seis reconexiones a 1 Hz terminan en UNA sesion estable")
    var ultima: FakeWatch? = null
    repeat(6) { i ->
        val w = h.watch()
        check(w.handshake(), "vuelta $i: el apreton de manos no se completo")
        check(waitUntil(2_000) { h.server.hasSession() }, "vuelta $i: no quedo sesion")
        if (i < 5) { w.close(); Thread.sleep(1_000) } else ultima = w
    }
    // Se deja reposar mas que el read() y que cualquier limpieza
    // rezagada de las cinco conexiones anteriores.
    Thread.sleep(4_000)
    check(h.server.hasSession(), "tras el ciclo no quedo ninguna sesion viva")
    val w = ultima
    check(w != null && w.alive, "la ultima conexion no sobrevivio al ciclo")
    if (w != null) {
        w.ping()
        val r = w.recv()
        check(r != null && r.first.type == FlexLink.T_PONG, "la sesion final no contesta")
        w.close()
    }
}

// =============================================================
//  3 bis) EL CADAVER QUE OCUPABA EL HUECO
// =============================================================
// ESTA ES LA PRUEBA DEL FALLO.
//
// Cuando el socket del reloj muere SIN aviso -- se va el Wi-Fi, el
// reloj se reinicia -- en el telefono no hay ningun FIN que leer:
// `read()` sigue bloqueado y la conexion muerta conserva el hueco de
// la sesion hasta que salte el plazo de inactividad, 40 s despues.
//
// Mientras tanto el reloj reintenta cada segundo, y antes se le
// cerraba la puerta cada segundo: "ya hay sesion". Eso es el ciclo
// que se veia, y dura lo que tarde el cadaver en enfriarse.
//
// Aqui el cadaver se modela con una conexion autenticada que deja de
// hablar Y deja de leer, pero mantiene el socket abierto: es
// exactamente lo que el telefono ve en ese caso.
//
// La regla que lo arregla: el hueco solo cambia de dueno cuando
// alguien SE AUTENTICA. Un desconocido no puede, asi que nadie
// puede usar esto para tirar una sesion ajena (lo comprueba la 4).
private fun testCadaverNoBloqueaElHueco(h: Harness) {
    println("[enlace] una conexion muerta no deja al reloj fuera de su propia sesion")
    val cadaver = h.watch()
    check(cadaver.handshake(), "el apreton de manos del primero no se completo")
    check(waitUntil(2_000) { h.server.hasSession() }, "no quedo la primera sesion")

    // A partir de aqui el "reloj" no dice ni lee nada, pero su socket
    // sigue abierto: el telefono no tiene forma de saber que murio.

    // El reloj de verdad vuelve por un socket NUEVO y se autentica.
    val vuelto = h.watch()
    check(vuelto.handshake(),
        "EL RELOJ NO PUDO VOLVER A ENTRAR: el hueco lo guardaba una conexion muerta")
    check(waitUntil(3_000) { h.server.hasSession() },
        "no quedo sesion despues de que el reloj volviera")

    // Y la sesion buena es la NUEVA: contesta de verdad.
    vuelto.ping()
    val r = vuelto.recv()
    check(r != null && r.first.type == FlexLink.T_PONG,
        "la sesion recuperada no contesta al latido")

    cadaver.close()
    vuelto.close()
    Thread.sleep(2_500)
}

// =============================================================
//  4) Una segunda conexion NO desaloja a la que ya esta
// =============================================================
private fun testUnaSolaSesion(h: Harness) {
    println("[enlace] un segundo Flex OS no desaloja al que ya tiene la sesion")
    val buena = h.watch()
    check(buena.handshake(), "el apreton de manos no se completo")
    check(waitUntil(2_000) { h.server.hasSession() }, "no quedo sesion")

    // Otro equipo de la red llama al puerto. Se le cierra, y el que ya
    // estaba NO se entera.
    val intrusa = runCatching { h.watch() }.getOrNull()
    Thread.sleep(500)
    check(h.server.hasSession(), "la conexion buena murio al llegar otra")
    buena.ping()
    val r = buena.recv()
    check(r != null && r.first.type == FlexLink.T_PONG,
        "la conexion buena dejo de contestar al llegar otra")
    intrusa?.close()
    buena.close()
    Thread.sleep(2_500)
}

fun main() {
    println("\n=== FlexOS · estabilidad de la sesion de Flex Phone ===")
    android.util.Log.verbose = System.getenv("FLEX_LINK_VERBOSE") != null

    val h = Harness()
    if (!h.start()) {
        println("  FALLO  el servidor no pudo abrir el puerto ${WifiLinkServer.TCP_PORT}")
        println("=== 1 comprobaciones, 1 fallos ===")
        kotlin.system.exitProcess(1)
    }
    try {
        testSesionEstable(h)
        testReconexionInmediata(h)
        testCicloDeUnSegundo(h)
        testCadaverNoBloqueaElHueco(h)
        testUnaSolaSesion(h)
    } finally {
        h.stop()
    }
    println("=== $run comprobaciones, $fail fallos ===")
    kotlin.system.exitProcess(if (fail > 0) 1 else 0)
}
