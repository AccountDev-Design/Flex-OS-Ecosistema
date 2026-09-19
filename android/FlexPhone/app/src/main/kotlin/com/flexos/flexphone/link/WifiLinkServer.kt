package com.flexos.flexphone.link

import android.content.Context
import android.net.wifi.WifiManager
import android.util.Log
import com.flexos.flexphone.protocol.Discovery
import com.flexos.flexphone.protocol.FlexAuth
import com.flexos.flexphone.protocol.FlexLink
import com.flexos.flexphone.protocol.PayloadReader
import com.flexos.flexphone.protocol.PayloadWriter
import java.io.InputStream
import java.io.OutputStream
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetAddress
import java.net.InetSocketAddress
import java.net.NetworkInterface
import java.net.ServerSocket
import java.net.Socket
import java.util.concurrent.atomic.AtomicBoolean

/**
 * SERVIDOR Wi-Fi DEL ENLACE.
 *
 * Es la pieza que hace que Flex Phone funcione de verdad en el
 * ESP32-P4. Antes el enlace iba por BLE, y el P4 no tiene radio
 * Bluetooth: la aplicacion entera vivia en "no disponible".
 *
 * QUIEN ES SERVIDOR
 * -----------------
 * El TELEFONO escucha y Flex OS se conecta. El telefono esta
 * encendido siempre y tiene un servicio en primer plano; el reloj se
 * suspende. Al reves, cada suspension del P4 cortaria el enlace.
 *
 * DOS SOCKETS
 * -----------
 *   · UDP 47821 -- descubrimiento. Flex OS pregunta a la difusion de
 *     la red y este servidor contesta con su puerto TCP y su nombre.
 *   · TCP 47820 -- el enlace. Tramas de Flex Link, una detras de
 *     otra.
 *
 * CONTROL DE ACCESO -- lo importante
 * ----------------------------------
 * Abrir un puerto en la red local significa que CUALQUIER equipo de
 * esa red puede llamar. Por eso:
 *   · una sola sesion a la vez;
 *   · sin apreton de manos completo no se acepta NINGUN mensaje que
 *     no sea del propio apreton;
 *   · el que llama tiene que demostrar que tiene la clave del
 *     vinculo, y este servidor demuestra que la tiene tambien;
 *   · una conexion que no autentica en unos segundos se cierra;
 *   · latido y tiempo de espera, para que un socket muerto no ocupe
 *     la sesion para siempre.
 *
 * LO QUE ESTO NO DA: la carga viaja EN CLARO por la red local. Impide
 * que un dispositivo no emparejado abra sesion; no protege frente a
 * quien ya este escuchando la misma red. La interfaz lo dice tal
 * cual, no se anuncia como cifrado.
 */
class WifiLinkServer(
    private val ctx: Context,
    private val phoneId: String,
    private val phoneName: String,
    /** Clave del vinculo guardada, o null si todavia no hay ninguno. */
    private val bondKey: () -> ByteArray?,
    /** Codigo que el usuario ha tecleado durante un emparejamiento. */
    private val pairingCode: () -> String?,
    /** Se llama cuando un emparejamiento se cierra con exito. */
    private val onPaired: (key: ByteArray, flexosId: String) -> Unit,
    private val onEvent: (Event) -> Unit,
    /** Mensajes de aplicacion ya reensamblados. */
    private val onMessage: (type: Int, payload: ByteArray) -> Unit,
) {
    sealed class Event {
        data class Listening(val port: Int, val address: String) : Event()
        data class Error(val message: String) : Event()
        data class SessionOpen(val peer: String) : Event()
        object SessionClosed : Event()
        /** El otro extremo pidio emparejar: hay que ensenar el campo del codigo. */
        object PairingRequested : Event()
        data class PairingFailed(val why: String) : Event()
        /** La lista de relojes que han contestado a una busqueda. */
        data class WatchesFound(val watches: List<Watch>) : Event()
    }

    /**
     * Un Flex OS que ha contestado a una busqueda.
     *
     * NO es una sesion: es solo "este reloj esta ahi y me oye". Que
     * aparezca aqui ya demuestra dos cosas -- que llega el ida y el
     * vuelta --, que es justo lo que no se sabia cuando el
     * emparejamiento fallaba sin decir nada.
     */
    data class Watch(
        val id: String,
        val name: String,
        val address: String,
        /** El reloj esta AHORA ensenando un codigo de emparejamiento. */
        val pairing: Boolean,
        val seenAt: Long = System.currentTimeMillis(),
    )

    companion object {
        private const val TAG = "FlexPhone/WifiLink"
        // Los MISMOS numeros que FlexOS_FlexPhone_WiFi.h. Cambiar uno
        // solo en un lado deja al telefono y al reloj sin encontrarse,
        // y sin ningun error que lo explique.
        const val TCP_PORT = Discovery.TCP_PORT
        const val UDP_PORT = Discovery.UDP_PORT
        // #############################################################
        // ##  LA BUSQUEDA AL REVES -- la direccion que SI funciona
        // ##  ------------------------------------------------------
        // ##  PROBE/REPLY es el reloj preguntando y el telefono
        // ##  contestando. Esa direccion depende de que Android
        // ##  RECIBA una difusion, y el controlador Wi-Fi las filtra
        // ##  para ahorrar bateria: el cerrojo de multidifusion
        // ##  ayuda, pero con la pantalla apagada o la app en segundo
        // ##  plano no siempre basta.
        // ##
        // ##  ASK/ANS es al reves: EMITE el telefono (emitir nunca se
        // ##  filtra) y el reloj contesta en UNIDIFUSION, que tampoco.
        // ##  Ademas, al recibir la sonda el reloj ya sabe la IP (el
        // ##  origen del paquete) y el puerto (la carga), asi que
        // ##  puede conectar aunque su propia difusion no llegue a
        // ##  ninguna parte.
        // ##
        // ##  LOS BYTES viven en :protocol (Discovery.kt), que es donde
        // ##  se pueden comparar con los de C++ sin Android de por
        // ##  medio: los mismos vectores estan en DiscoveryTest.kt y en
        // ##  tests/host/test_flexphone_discovery.cpp.
        // #############################################################
        /** Un reloj que lleva esto sin contestar sale de la lista. */
        private const val WATCH_TTL_MS = 20_000L

        /** Sin autenticar en este tiempo, la conexion se cierra. */
        private const val AUTH_TIMEOUT_MS = 15_000
        /** Sin recibir nada en este tiempo, la sesion se da por muerta. */
        private const val IDLE_TIMEOUT_MS = 40_000
        /**
         * Cada cuanto despierta la lectura para mirar los plazos.
         *
         * NO es un tiempo de espera de la sesion: `read` vuelve cada
         * dos segundos sin datos y no pasa nada. Existe porque los dos
         * plazos de arriba solo se comprobaban DESPUES de que `read`
         * volviera, y `read` bloqueaba los 40 s del plazo largo. O sea
         * que el plazo de autenticacion de 15 s no podia dispararse
         * nunca: una conexion muda ocupaba la unica sesion casi un
         * minuto, y el Flex OS de verdad se encontraba la puerta
         * cerrada sin ninguna explicacion.
         */
        private const val POLL_MS = 2_000
        /**
         * Desde que se manda el codigo hasta que Flex OS lo acepta.
         * Pasado esto se da por fallido: la pantalla NUNCA se queda en
         * "Comprobando..." para siempre.
         */
        private const val PAIR_CONFIRM_TIMEOUT_MS = 12_000
        /** Tope de lo acumulado sin completar una trama. */
        private const val ACC_MAX = FlexLink.MAX_FRAME * 3
    }

    private val running = AtomicBoolean(false)
    private var server: ServerSocket? = null
    private var udp: DatagramSocket? = null

    // #############################################################
    // ##  EL CERROJO DE MULTIDIFUSION -- sin esto no llega NADA
    // ##  ------------------------------------------------------
    // ##  El descubrimiento de Flex OS es una sonda a la DIFUSION de
    // ##  la red. Y el controlador Wi-Fi de Android descarta las
    // ##  tramas de difusion y multidifusion que no van dirigidas a
    // ##  la MAC del propio telefono ANTES de que lleguen a ningun
    // ##  socket, para ahorrar bateria.
    // ##
    // ##  O sea: sin este cerrojo, el ESP32 emite la sonda
    // ##  perfectamente, el telefono esta escuchando en el puerto
    // ##  correcto... y receive() no despierta nunca. No hay ningun
    // ##  error que lo explique, que es lo que lo hacia tan dificil
    // ##  de ver.
    // ##
    // ##  Se suelta mientras hay sesion abierta: con el enlace
    // ##  establecido ya no hace falta oir sondas, y el filtro del
    // ##  controlador es justo lo que ahorra bateria.
    // #############################################################
    private var multicastLock: WifiManager.MulticastLock? = null

    private fun acquireMulticast() {
        if (multicastLock?.isHeld == true) return
        runCatching {
            val wm = ctx.applicationContext.getSystemService(Context.WIFI_SERVICE) as? WifiManager
            multicastLock = wm?.createMulticastLock("flexphone-discovery")?.apply {
                setReferenceCounted(false)
                acquire()
            }
        }.onFailure { Log.w(TAG, "no se pudo tomar el cerrojo de multidifusion") }
        Log.d(TAG, "cerrojo de multidifusion: ${if (multicastLock?.isHeld == true) "tomado" else "NO"}")
    }

    private fun releaseMulticast() {
        runCatching { if (multicastLock?.isHeld == true) multicastLock?.release() }
        multicastLock = null
    }
    private var tcpThread: Thread? = null
    private var udpThread: Thread? = null

    // -- sesion viva (una sola) --
    @Volatile private var out: OutputStream? = null
    @Volatile private var sock: Socket? = null
    @Volatile private var session = 0
    @Volatile private var authed = false
    @Volatile private var txCounter = 0L
    @Volatile private var txPacket = 1
    private var nonce: ByteArray? = null
    private var sessionKey: ByteArray? = null
    private var flexosId: String = ""

    val port: Int get() = server?.localPort ?: 0
    fun isRunning(): Boolean = running.get()
    fun hasSession(): Boolean = authed && out != null

    // =========================================================
    //  Arranque y parada
    // =========================================================
    fun start(): Boolean {
        if (running.get()) return true
        return try {
            server = ServerSocket().apply {
                reuseAddress = true
                bind(InetSocketAddress(TCP_PORT))
            }
            udp = DatagramSocket(null).apply {
                reuseAddress = true
                broadcast = true
                bind(InetSocketAddress(UDP_PORT))
            }
            running.set(true)
            acquireMulticast()
            tcpThread = Thread({ acceptLoop() }, "flex-link-tcp").apply { isDaemon = true; start() }
            udpThread = Thread({ discoveryLoop() }, "flex-link-udp").apply { isDaemon = true; start() }
            onEvent(Event.Listening(server!!.localPort, localWifiAddress() ?: "?"))
            true
        } catch (e: Exception) {
            // El motivo REAL, no un generico: un puerto ocupado y un
            // telefono sin Wi-Fi se arreglan de formas distintas.
            onEvent(Event.Error(
                if (e is java.net.BindException) "el puerto $TCP_PORT ya esta en uso"
                else "no se pudo abrir el enlace"
            ))
            stop()
            false
        }
    }

    fun stop() {
        running.set(false)
        releaseMulticast()
        // La lista de relojes deja de ser cierta en cuanto se para el
        // socket: se vacia en vez de quedarse mostrando dispositivos
        // que ya no se estan viendo.
        synchronized(watches) { watches.clear() }
        closeSession("servidor parado")
        runCatching { server?.close() }; server = null
        runCatching { udp?.close() }; udp = null
        tcpThread = null
        udpThread = null
    }

    // =========================================================
    //  Descubrimiento
    // =========================================================
    private fun discoveryLoop() {
        val buf = ByteArray(96)
        Log.d(TAG, "descubrimiento escuchando en UDP $UDP_PORT")
        while (running.get()) {
            try {
                val pkt = DatagramPacket(buf, buf.size)
                udp?.receive(pkt) ?: break
                // (b) ALGO ha llegado. Si esta linea no sale nunca y el
                //     ESP32 si dice que emite, el controlador Wi-Fi esta
                //     filtrando la difusion (ver el cerrojo de arriba) o
                //     el router aisla a los clientes entre si.
                Log.d(TAG, "(b) ${pkt.length} B de ${pkt.address?.hostAddress}:${pkt.port}")
                when {
                    Discovery.isProbe(buf, pkt.length) -> onWatchProbe(buf, pkt)
                    Discovery.isAnswer(buf, pkt.length) -> onWatchAnswer(buf, pkt)
                    // Lo normal que queda aqui es oir la PROPIA sonda de
                    // vuelta: la difusion se entrega tambien al que la
                    // emitio. No es un fallo.
                }
            } catch (e: Exception) {
                if (running.get()) Log.w(TAG, "descubrimiento detenido: ${e.javaClass.simpleName}")
                break
            }
        }
    }

    /** El reloj pregunta quien hay: se contesta con el puerto TCP. */
    private fun onWatchProbe(buf: ByteArray, pkt: DatagramPacket) {
        val probe = Discovery.parseProbe(buf, pkt.length) ?: return
        // Una version que no entendemos NO se contesta: seria invitar a
        // una conexion que va a fallar despues.
        if (probe.ver < FlexLink.VERSION_MIN) {
            Log.w(TAG, "(b) sonda de protocolo v${probe.ver}, se necesita v${FlexLink.VERSION_MIN}")
            return
        }
        val p = server?.localPort ?: TCP_PORT
        val reply = Discovery.buildReply(p, phoneName)
        udp?.send(DatagramPacket(reply, reply.size, pkt.address, pkt.port))
        // (c) contestado. Si se ve esto y el ESP32 no registra su (d),
        //     la respuesta se pierde de vuelta -- ahi ya es la red, no
        //     el codigo.
        Log.d(TAG, "(c) respondido a ${pkt.address?.hostAddress}:${pkt.port}, puerto TCP $p")
    }

    /** El reloj contesta a NUESTRA busqueda: entra en la lista. */
    private fun onWatchAnswer(buf: ByteArray, pkt: DatagramPacket) {
        val a = Discovery.parseAnswer(buf, pkt.length) ?: return
        if (a.ver < FlexLink.VERSION_MIN) {
            Log.w(TAG, "(2) un reloj contesto con protocolo v${a.ver}, se necesita v${FlexLink.VERSION_MIN}")
            return
        }
        val addr = pkt.address?.hostAddress ?: return
        val w = Watch(id = a.id, name = a.name, address = addr, pairing = a.pairing)
        // (2) un reloj CONTESTO. Que se vea esta linea demuestra que la
        //     ida y la vuelta funcionan; si no se ve nunca, el problema
        //     esta en la red (aislamiento de clientes) o el reloj no
        //     tiene el enlace encendido.
        Log.d(TAG, "(2) reloj: ${w.name} (${w.id}) en ${w.address}${if (w.pairing) " [emparejando]" else ""}")
        synchronized(watches) { watches[w.id] = w }
        publishWatches()
    }

    // =========================================================
    //  Buscar relojes (la direccion fiable)
    // =========================================================
    private val watches = LinkedHashMap<String, Watch>()

    private fun publishWatches() {
        val now = System.currentTimeMillis()
        val live = synchronized(watches) {
            // Un reloj que dejo de contestar SALE. Dejarlo ahi seria
            // ofrecer un dispositivo que ya no esta, y el usuario lo
            // tocaria y no pasaria nada.
            watches.entries.removeAll { now - it.value.seenAt > WATCH_TTL_MS }
            watches.values.sortedBy { it.name }
        }
        onEvent(Event.WatchesFound(live))
    }

    private fun askPacket(): ByteArray =
        Discovery.buildAsk(server?.localPort ?: TCP_PORT, phoneName)

    /**
     * Pregunta a toda la red quien es un Flex OS.
     *
     * Devuelve a cuantos destinos se pudo emitir. Cero significa que
     * este telefono no pudo mandar NADA -- sin Wi-Fi, o la interfaz
     * sin direccion --, y eso se dice en pantalla en vez de dejar un
     * buscador girando.
     */
    fun probeForWatches(): Int {
        val u = udp ?: return 0
        val pkt = askPacket()
        var sent = 0
        // Difusion DIRIGIDA de cada interfaz activa. Es la que menos
        // molesta y la que mas pilas dejan pasar.
        runCatching {
            NetworkInterface.getNetworkInterfaces().toList()
                .filter { it.isUp && !it.isLoopback }
                .flatMap { it.interfaceAddresses }
                .mapNotNull { it.broadcast }
                .distinct()
                .forEach { b ->
                    runCatching { u.send(DatagramPacket(pkt, pkt.size, b, UDP_PORT)); sent++ }
                }
        }
        // Y la LIMITADA, porque hay puntos de acceso que filtran la
        // dirigida y dejan pasar esta. Son 40 bytes.
        runCatching {
            u.send(DatagramPacket(pkt, pkt.size, InetAddress.getByName("255.255.255.255"), UDP_PORT))
            sent++
        }
        // (1) emitido. Si esto sale y nunca llega un (2), la red esta
        //     filtrando la difusion o aislando a los clientes: para eso
        //     esta probeHost.
        Log.d(TAG, "(1) busqueda emitida a $sent destinos")
        return sent
    }

    /**
     * Pregunta a UNA direccion concreta, sin difusion de por medio.
     *
     * Es el respaldo para las redes que filtran o aislan: el usuario
     * lee la IP en la pantalla de Flex OS y la teclea aqui. El paquete
     * va en unidifusion, que no filtra nadie, y al recibirlo el reloj
     * aprende la IP y el puerto de este telefono del propio paquete --
     * asi que puede conectar sin haber descubierto nada.
     */
    fun probeHost(ip: String): Boolean {
        val u = udp ?: return false
        return runCatching {
            val a = InetAddress.getByName(ip.trim())
            val pkt = askPacket()
            u.send(DatagramPacket(pkt, pkt.size, a, UDP_PORT))
            Log.d(TAG, "(1) busqueda directa a $ip")
            true
        }.getOrElse {
            Log.w(TAG, "direccion invalida: $ip")
            false
        }
    }

    /** IP local de la interfaz Wi-Fi, para ensenarla. Null si no hay. */
    fun localWifiAddress(): String? = runCatching {
        NetworkInterface.getNetworkInterfaces().toList()
            .asSequence()
            .filter { it.isUp && !it.isLoopback }
            .flatMap { it.inetAddresses.toList().asSequence() }
            .firstOrNull { !it.isLoopbackAddress && it is java.net.Inet4Address }
            ?.hostAddress
    }.getOrNull()

    // =========================================================
    //  Aceptacion
    // =========================================================
    private fun acceptLoop() {
        while (running.get()) {
            val s = try {
                server?.accept() ?: break
            } catch (e: Exception) {
                if (running.get()) onEvent(Event.Error("el enlace dejo de aceptar conexiones"))
                break
            }
            // UNA sesion a la vez. Una segunda conexion se cierra en el
            // acto en vez de repartir el enlace entre dos Flex OS.
            if (sock != null) { runCatching { s.close() }; continue }
            Thread({ serve(s) }, "flex-link-conn").apply { isDaemon = true; start() }
        }
    }

    private fun serve(s: Socket) {
        sock = s
        authed = false
        session = 0
        txCounter = 0
        txPacket = 1
        sessionKey = null
        nonce = null
        flexosId = ""
        val peer = s.inetAddress?.hostAddress ?: "?"
        try {
            s.tcpNoDelay = true             // el enlace manda mensajes cortos
            // Se despierta cada POLL_MS para mirar los plazos. Poner
            // aqui IDLE_TIMEOUT_MS hacia que `read` bloqueara 40 s y
            // que ningun plazo mas corto pudiera dispararse.
            s.soTimeout = POLL_MS
            val input: InputStream = s.getInputStream()
            out = s.getOutputStream()

            val acc = ByteArray(ACC_MAX)
            var accN = 0
            val opened = System.currentTimeMillis()
            var lastRx = opened

            while (running.get() && !s.isClosed) {
                val now = System.currentTimeMillis()
                // Sin autenticar en el plazo, fuera. Una conexion que
                // se queda abierta sin autenticar ocupa la unica sesion
                // y deja fuera al Flex OS de verdad.
                if (!authed && now - opened > AUTH_TIMEOUT_MS) {
                    onEvent(Event.PairingFailed("la otra parte no se autentico"))
                    break
                }
                // NINGUN ESTADO INFINITO. El codigo se mando y Flex OS
                // no lo confirmo: se dice por que en vez de dejar la
                // pantalla en "Comprobando..." hasta que caduque la
                // ventana de emparejamiento.
                val sentAt = pairSentAt
                if (!authed && sentAt != 0L && now - sentAt > PAIR_CONFIRM_TIMEOUT_MS) {
                    pairSentAt = 0L
                    onEvent(Event.PairingFailed(
                        "Flex OS no acepto el codigo. Comprueba que es el que ensena la pantalla del reloj."
                    ))
                    break
                }
                if (now - lastRx > IDLE_TIMEOUT_MS) {
                    onEvent(Event.Error("sin respuesta de Flex OS"))
                    break
                }
                val room = ACC_MAX - accN
                if (room <= 0) {
                    // Lleno sin una trama completa: el otro extremo no
                    // habla este protocolo.
                    onEvent(Event.Error("el otro extremo no habla Flex Link"))
                    break
                }
                val got = try { input.read(acc, accN, room) } catch (e: java.net.SocketTimeoutException) {
                    continue                    // solo es que no hubo datos en este tramo
                }
                if (got <= 0) break
                lastRx = System.currentTimeMillis()
                accN += got
                accN = drainFrames(acc, accN) ?: break
            }
        } catch (e: Exception) {
            // Un socket que se cierra por el otro lado es normal, no un
            // fallo que haya que ensenar como error.
            Log.d(TAG, "conexion terminada")
        } finally {
            closeSession(null)
        }
    }

    /**
     * TCP es un FLUJO: no respeta los limites de las tramas. Se
     * reconstruyen con la propia cabecera de Flex Link, que ya dice su
     * longitud. Devuelve los bytes que quedan sin consumir, o null si
     * el flujo esta descolocado y hay que cerrar.
     *
     * Si la marca no cuadra NO se busca la siguiente a ciegas: un
     * flujo corrupto en el que se va saltando hasta encontrar algo que
     * parezca una cabecera acaba interpretando basura como mensajes.
     */
    private fun drainFrames(acc: ByteArray, accN: Int): Int? {
        var at = 0
        while (accN - at >= FlexLink.HDR_SIZE) {
            if (acc[at] != 0xF1.toByte() || acc[at + 1] != 0x58.toByte()) {
                onEvent(Event.Error("el flujo llego descolocado"))
                return null
            }
            val len = (acc[at + 10].toInt() and 0xFF) or ((acc[at + 11].toInt() and 0xFF) shl 8)
            if (len > FlexLink.MAX_PAYLOAD) {
                onEvent(Event.Error("trama de tamano imposible"))
                return null
            }
            val total = FlexLink.HDR_SIZE + len
            if (accN - at < total) break               // falta cola: se espera
            handleFrame(acc.copyOfRange(at, at + total))
            at += total
        }
        if (at > 0 && accN > at) acc.copyInto(acc, 0, at, accN)
        return accN - at
    }

    // =========================================================
    //  Un mensaje entrante
    // =========================================================
    private fun handleFrame(frame: ByteArray) {
        val r = FlexLink.readFrame(frame)
        if (r !is FlexLink.ReadResult.Ok) {
            // Version incompatible: se dice y se corta. Seguir hablando
            // con un extremo que no entiende el protocolo solo produce
            // un "no conecta" sin explicacion en las dos puntas.
            if (r is FlexLink.ReadResult.BadVersion) {
                sendRaw(FlexLink.T_ERR, byteArrayOf(FlexLink.E_VERSION.toByte()), 0)
                onEvent(Event.Error("Flex OS habla otra version del protocolo"))
                runCatching { sock?.close() }
            }
            return
        }
        val h = r.header
        val body = r.payload

        // ---- Control de acceso ----
        // Fuera de sesion SOLO pasa el apreton de manos. Una orden de
        // un extremo sin autenticar se descarta aunque la trama sea
        // perfecta: ese es justo el ataque que hay que parar.
        if (!authed && h.type !in HANDSHAKE_TYPES) return

        when (h.type) {
            FlexLink.T_HELLO -> onHello(body)
            FlexLink.T_PAIR_CODE -> onPairCode(body)
            FlexLink.T_AUTH_CHALLENGE -> onAuthChallenge(body)
            FlexLink.T_AUTH_OK -> onAuthOk(body, h.session)
            FlexLink.T_PING -> sendRaw(FlexLink.T_PONG, ByteArray(0), h.session)
            FlexLink.T_PONG -> { /* latido: nada que hacer */ }
            FlexLink.T_ERR -> onPeerError(body)
            FlexLink.T_BYE -> { runCatching { sock?.close() } }
            else -> if (authed) onMessage(h.type, body)
        }
    }

    /**
     * Flex OS dice que algo fue mal.
     *
     * ANTES ESTE MENSAJE SE TIRABA. `T_ERR` estaba en la lista de
     * tipos permitidos sin sesion, pero no tenia rama en el `when`, asi
     * que caia en el `else` y, sin autenticar, no hacia nada. O sea que
     * cuando Flex OS avisaba de que el codigo estaba mal, aqui no se
     * enteraba nadie: la pantalla seguia diciendo "Comprobando..." y el
     * usuario no tenia forma de saber que habia pasado.
     */
    private fun onPeerError(body: ByteArray) {
        val code = if (body.isNotEmpty()) body[0].toInt() and 0xFF else FlexLink.E_INTERNAL
        pairSentAt = 0L
        val why = when (code) {
            FlexLink.E_AUTH ->
                "el codigo no coincide con el que ensena Flex OS"
            FlexLink.E_TIMEOUT ->
                "el emparejamiento caduco en Flex OS; vuelve a pulsar \"Emparejar telefono\" en el reloj"
            FlexLink.E_NOTPAIRED ->
                "Flex OS ya no tiene guardado el vinculo con este telefono"
            FlexLink.E_VERSION ->
                "Flex OS habla otra version del protocolo"
            FlexLink.E_BUSY ->
                "Flex OS ya tiene otro telefono conectado"
            else -> "Flex OS rechazo la conexion (codigo $code)"
        }
        Log.w(TAG, "T_ERR de Flex OS: $code -- $why")
        if (authed) onEvent(Event.Error(why)) else onEvent(Event.PairingFailed(why))
    }

    private fun onHello(body: ByteArray) {
        val rd = PayloadReader(body)
        val ver = rd.u8()
        flexosId = rd.str()
        if (ver < FlexLink.VERSION_MIN) {
            sendRaw(FlexLink.T_ERR, byteArrayOf(FlexLink.E_VERSION.toByte()), 0)
            onEvent(Event.Error("Flex OS habla una version anterior del protocolo"))
            return
        }
        val w = PayloadWriter()
        w.u8(FlexLink.VERSION)
        w.str(phoneId, FlexAuth.ID_MAX - 1)
        w.str(phoneName, 31)
        sendRaw(FlexLink.T_WELCOME, w.build(), 0)
    }

    /**
     * Flex OS manda la SAL. El codigo no viene en el mensaje: lo
     * teclea el usuario en esta app. Si todavia no lo ha tecleado, se
     * avisa a la interfaz y se espera -- no se contesta con nada.
     */
    private fun onPairCode(body: ByteArray) {
        val rd = PayloadReader(body)
        val salt = rd.bytes(FlexAuth.SALT_SIZE)
        val n = rd.bytes(FlexAuth.NONCE_SIZE)
        val hostId = rd.str()
        if (!rd.ok || hostId.isEmpty()) return

        // LA SAL SE GUARDA SIEMPRE, haya codigo o no.
        //
        // Antes solo se guardaba dentro de completePairing, o sea SOLO
        // cuando el usuario ya habia tecleado el codigo. En el orden
        // normal -- Flex OS manda la sal y el usuario teclea despues --
        // la sal se tiraba, y submitPairingCode salia por
        // `pendingSalt ?: return false` sin enviar nada. Flex OS se
        // quedaba esperando un PAIR_CONFIRM que no iba a llegar nunca:
        // ese era el "cargando infinito".
        pendingSalt = salt
        nonce = n
        flexosId = hostId
        Log.d(TAG, "PAIR_CODE recibido de $hostId: sal guardada")

        val code = pairingCode()
        if (code == null || !FlexAuth.isValidCode(code)) {
            // Se le pide el codigo al usuario. En ningun caso se deriva
            // una clave con un codigo inventado.
            onEvent(Event.PairingRequested)
            return
        }
        completePairing(code, salt, n)
    }

    /**
     * La interfaz llama aqui cuando el usuario teclea el codigo.
     *
     * Devuelve false si TODAVIA no se puede emparejar -- porque la sal
     * de Flex OS no ha llegado, o porque el codigo no tiene forma de
     * codigo. Ese false llega hasta la pantalla: el usuario tiene que
     * ver "esperando a Flex OS" en vez de un spinner que no termina.
     */
    fun submitPairingCode(code: String): Boolean {
        if (!FlexAuth.isValidCode(code)) return false
        val n = nonce
        val salt = pendingSalt
        if (n == null || salt == null) {
            Log.w(TAG, "codigo tecleado pero Flex OS aun no ha mandado la sal")
            return false
        }
        completePairing(code, salt, n)
        return true
    }

    /** ¿Ha llegado ya la sal? La pantalla lo necesita para no pedir el codigo antes de tiempo. */
    fun isAwaitingCode(): Boolean = pendingSalt != null && nonce != null

    @Volatile private var pendingSalt: ByteArray? = null
    /** Cuando se mando el PAIR_CONFIRM, para no esperarlo eternamente. 0 = no hay ninguno en vuelo. */
    @Volatile private var pairSentAt: Long = 0L

    private fun completePairing(code: String, salt: ByteArray, n: ByteArray) {
        val key = FlexAuth.deriveKey(code, salt, flexosId, phoneId)
        sessionKey = key
        // Se manda la prueba. Si el codigo estaba mal, Flex OS lo
        // rechaza y NO empareja: el error se ve alli, con el motivo.
        val ok = sendRaw(FlexLink.T_PAIR_CONFIRM, FlexAuth.proof(key, FlexAuth.ROLE_PHONE, n, 0), 0)
        Log.d(TAG, "PAIR_CONFIRM enviado: $ok")
        if (ok) pairSentAt = System.currentTimeMillis()
        else onEvent(Event.PairingFailed("se corto la conexion al enviar el codigo"))
    }

    private fun onAuthChallenge(body: ByteArray) {
        val rd = PayloadReader(body)
        val n = rd.bytes(FlexAuth.NONCE_SIZE)
        val sess = rd.u16()
        if (!rd.ok) return
        val key = bondKey()
        if (key == null) {
            // Flex OS cree que estamos vinculados y aqui no hay clave:
            // el usuario borro los datos de la app. Se dice, en vez de
            // dejar a Flex OS esperando una respuesta que no llegara.
            sendRaw(FlexLink.T_ERR, byteArrayOf(FlexLink.E_NOTPAIRED.toByte()), sess)
            onEvent(Event.PairingFailed("este telefono ya no tiene el vinculo guardado"))
            return
        }
        nonce = n
        session = sess
        sessionKey = key
        sendRaw(FlexLink.T_AUTH_RESPONSE, FlexAuth.proof(key, FlexAuth.ROLE_PHONE, n, sess), sess)
    }

    /**
     * Flex OS devuelve SU prueba. Se comprueba: sin esto, un equipo
     * cualquiera de la red podria hacerse pasar por el reloj y
     * quedarse con todas las notificaciones del usuario.
     */
    private fun onAuthOk(body: ByteArray, sess: Int) {
        val key = sessionKey ?: bondKey() ?: return
        val n = nonce ?: return
        val rd = PayloadReader(body)
        val got = rd.bytes(FlexAuth.PROOF_SIZE)
        if (!rd.ok) return
        if (!FlexAuth.verify(key, FlexAuth.ROLE_HOST, n, sess, got)) {
            onEvent(Event.PairingFailed("quien contesta no es el Flex OS emparejado"))
            runCatching { sock?.close() }
            return
        }
        // Emparejamiento nuevo: ahora SI se guarda la clave. No antes:
        // guardarla al derivarla dejaria un vinculo con quien resulto
        // no ser Flex OS.
        if (bondKey() == null) onPaired(key, flexosId)
        session = sess
        authed = true
        pairSentAt = 0L
        releaseMulticast()          // con sesion abierta ya no hay que oir sondas
        onEvent(Event.SessionOpen(sock?.inetAddress?.hostAddress ?: "?"))
    }

    private fun closeSession(why: String?) {
        val had = sock != null
        runCatching { sock?.close() }
        sock = null
        out = null
        authed = false
        session = 0
        sessionKey = null
        nonce = null
        pendingSalt = null
        pairSentAt = 0L
        // Sin sesion vuelve a hacer falta oir las sondas del reloj.
        if (running.get()) acquireMulticast()
        if (had) onEvent(Event.SessionClosed)
        if (why != null) Log.d(TAG, why)
    }

    // =========================================================
    //  Envio
    // =========================================================
    /** Manda un mensaje de aplicacion. Exige sesion autenticada. */
    fun send(type: Int, payload: ByteArray): Boolean {
        if (!authed) return false
        return sendRaw(type, payload, session)
    }

    private fun sendRaw(type: Int, payload: ByteArray, sess: Int): Boolean {
        val o = out ?: return false
        // Se trocea segun el MTU del protocolo. Un mensaje medio
        // entregado no se puede reensamblar, asi que el fallo de un
        // fragmento invalida el mensaje entero.
        val perFrag = FlexLink.MAX_PAYLOAD
        val frags = if (payload.isEmpty()) 1
                    else (payload.size + perFrag - 1) / perFrag
        if (frags > FlexLink.MAX_FRAGS) return false
        val packet = txPacket.also { txPacket = if (it >= 0xFFFF) 1 else it + 1 }
        return try {
            synchronized(this) {
                var at = 0
                for (i in 0 until frags) {
                    val take = minOf(perFrag, payload.size - at)
                    val chunk = if (take > 0) payload.copyOfRange(at, at + take) else ByteArray(0)
                    txCounter++
                    val frame = FlexLink.writeFrame(
                        FlexLink.Header(
                            type = type, session = sess, packet = packet,
                            frag = i, fragCount = frags, counter = txCounter,
                        ),
                        chunk,
                    )
                    o.write(frame)
                    at += take
                }
                o.flush()
            }
            true
        } catch (e: Exception) {
            closeSession("se corto al enviar")
            false
        }
    }

    private val HANDSHAKE_TYPES = intArrayOf(
        FlexLink.T_HELLO, FlexLink.T_WELCOME, FlexLink.T_BYE, FlexLink.T_ERR,
        FlexLink.T_PAIR_REQ, FlexLink.T_PAIR_CODE, FlexLink.T_PAIR_CONFIRM,
        FlexLink.T_AUTH_CHALLENGE, FlexLink.T_AUTH_RESPONSE, FlexLink.T_AUTH_OK,
        FlexLink.T_PING, FlexLink.T_PONG,
    )
}
