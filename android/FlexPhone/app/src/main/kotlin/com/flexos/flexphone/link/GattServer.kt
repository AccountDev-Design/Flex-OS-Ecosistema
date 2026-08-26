package com.flexos.flexphone.link

import android.annotation.SuppressLint
import android.bluetooth.*
import android.bluetooth.le.AdvertiseCallback
import android.bluetooth.le.AdvertiseData
import android.bluetooth.le.AdvertiseSettings
import android.content.Context
import android.os.Build
import android.util.Log
import com.flexos.flexphone.domain.FlexPhoneState
import com.flexos.flexphone.domain.LinkState
import com.flexos.flexphone.protocol.AntiReplay
import com.flexos.flexphone.protocol.FlexLink
import com.flexos.flexphone.protocol.Reassembler
import java.util.UUID
import java.util.concurrent.ConcurrentLinkedQueue
import java.util.concurrent.atomic.AtomicLong

/**
 * Servidor GATT: el telefono es el PERIFERICO y Flex OS el central.
 *
 * POR QUE ASI Y NO AL REVES
 * -------------------------
 * El P4 no tiene radio propia: su BLE, si lo hay, viene del C6. Que
 * el telefono anuncie y el reloj conecte reduce el trabajo del lado
 * del C6 al de un cliente GATT normal, que es lo que mejor soportan
 * las pilas de host sobre HCI remoto.
 *
 * SEGURIDAD
 *   · las caracteristicas se declaran con PERMISOS CIFRADOS
 *     (..._ENCRYPTED), asi que Android exige emparejamiento y cifra
 *     el enlace antes de entregar un solo byte;
 *   · sin bonding, un dispositivo cualquiera no puede escribir;
 *   · el contador anti-repeticion descarta lo repetido;
 *   · el anuncio NO lleva el nombre del telefono ni datos del
 *     usuario, solo el UUID del servicio.
 */
class GattServer(
    private val ctx: Context,
    private val state: FlexPhoneState,
    private val onMessage: (type: Int, payload: ByteArray) -> Unit,
) {
    companion object {
        private const val TAG = "FlexPhone/Gatt"

        /** Servicio Flex Link. UUID propio, no uno estandar reutilizado. */
        val SERVICE: UUID = UUID.fromString("f1580001-4c65-4f53-8f6c-6578f1584c4b")
        /** El P4 ESCRIBE aqui (telefono <- Flex OS). */
        val RX: UUID = UUID.fromString("f1580002-4c65-4f53-8f6c-6578f1584c4b")
        /** El P4 se SUSCRIBE aqui (telefono -> Flex OS). */
        val TX: UUID = UUID.fromString("f1580003-4c65-4f53-8f6c-6578f1584c4b")
        private val CCCD: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")

        /** MTU al que se aspira; el real lo decide la negociacion. */
        const val DESIRED_MTU = 247
    }

    private var manager: BluetoothManager? = null
    private var server: BluetoothGattServer? = null
    private var txChar: BluetoothGattCharacteristic? = null
    private var peer: BluetoothDevice? = null
    private var advertiser: android.bluetooth.le.BluetoothLeAdvertiser? = null

    /** MTU NEGOCIADO. Arranca en el minimo garantizado, no en el deseado. */
    @Volatile var mtu: Int = 23
        private set
    private val usablePayload: Int get() = (mtu - 3).coerceAtMost(FlexLink.MAX_FRAME)

    private val reasm = Reassembler()
    private val antiReplay = AntiReplay()
    private val txCounter = AtomicLong(1)
    private var session = 0
    private var packetId = 0

    /** Cola de salida acotada: se descarta antes que crecer sin fin. */
    private val outbox = ConcurrentLinkedQueue<ByteArray>()
    private val OUTBOX_MAX = 64
    @Volatile private var sending = false

    @SuppressLint("MissingPermission")
    fun start(): Boolean {
        val mgr = ctx.getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager
        if (mgr == null) {
            state.setLink(LinkState.UNAVAILABLE, "este dispositivo no tiene Bluetooth")
            return false
        }
        val adapter = mgr.adapter
        if (adapter == null || !adapter.isEnabled) {
            state.setLink(LinkState.UNAVAILABLE, "el Bluetooth esta apagado")
            return false
        }
        if (!hasPermissions()) {
            state.setLink(LinkState.UNAVAILABLE, "faltan permisos de Bluetooth")
            return false
        }
        manager = mgr

        return try {
            server = mgr.openGattServer(ctx, callback) ?: run {
                state.setLink(LinkState.ERROR, "Android no dejo abrir el servidor GATT")
                return false
            }
            val svc = BluetoothGattService(SERVICE, BluetoothGattService.SERVICE_TYPE_PRIMARY)

            // PERMISOS CIFRADOS: Android exige bonding antes de
            // entregar o aceptar datos. Es lo que impide que un
            // dispositivo no emparejado escriba en RX.
            val rx = BluetoothGattCharacteristic(
                RX,
                BluetoothGattCharacteristic.PROPERTY_WRITE or
                    BluetoothGattCharacteristic.PROPERTY_WRITE_NO_RESPONSE,
                BluetoothGattCharacteristic.PERMISSION_WRITE_ENCRYPTED,
            )
            val tx = BluetoothGattCharacteristic(
                TX,
                BluetoothGattCharacteristic.PROPERTY_NOTIFY,
                BluetoothGattCharacteristic.PERMISSION_READ_ENCRYPTED,
            ).apply {
                addDescriptor(
                    BluetoothGattDescriptor(
                        CCCD,
                        BluetoothGattDescriptor.PERMISSION_READ or
                            BluetoothGattDescriptor.PERMISSION_WRITE_ENCRYPTED,
                    )
                )
            }
            svc.addCharacteristic(rx)
            svc.addCharacteristic(tx)
            txChar = tx
            server?.addService(svc)
            advertise(adapter)
            state.setLink(LinkState.ADVERTISING)
            true
        } catch (e: SecurityException) {
            state.setLink(LinkState.UNAVAILABLE, "faltan permisos de Bluetooth")
            false
        } catch (e: Exception) {
            state.setLink(LinkState.ERROR, "no se pudo iniciar el enlace")
            false
        }
    }

    @SuppressLint("MissingPermission")
    private fun advertise(adapter: BluetoothAdapter) {
        advertiser = adapter.bluetoothLeAdvertiser ?: return
        val settings = AdvertiseSettings.Builder()
            .setAdvertiseMode(AdvertiseSettings.ADVERTISE_MODE_BALANCED)
            .setTxPowerLevel(AdvertiseSettings.ADVERTISE_TX_POWER_MEDIUM)
            .setConnectable(true)
            .setTimeout(0)
            .build()
        // El anuncio lleva SOLO el UUID del servicio. Sin nombre de
        // dispositivo: un anuncio permanente con el nombre real es un
        // identificador que cualquiera alrededor puede seguir.
        val data = AdvertiseData.Builder()
            .setIncludeDeviceName(false)
            .setIncludeTxPowerLevel(false)
            .addServiceUuid(android.os.ParcelUuid(SERVICE))
            .build()
        runCatching { advertiser?.startAdvertising(settings, data, advCallback) }
    }

    private val advCallback = object : AdvertiseCallback() {
        override fun onStartFailure(errorCode: Int) {
            val why = when (errorCode) {
                ADVERTISE_FAILED_DATA_TOO_LARGE -> "el anuncio no cabe"
                ADVERTISE_FAILED_TOO_MANY_ADVERTISERS -> "hay demasiados anuncios activos"
                ADVERTISE_FAILED_ALREADY_STARTED -> return          // ya estaba: no es un fallo
                ADVERTISE_FAILED_FEATURE_UNSUPPORTED ->
                    "este telefono no puede anunciarse por BLE"
                else -> "Android rechazo el anuncio"
            }
            state.setLink(LinkState.ERROR, why)
        }
    }

    @SuppressLint("MissingPermission")
    fun stop() {
        // Despedida LIMPIA antes de cerrar, si habia sesion.
        if (state.link.value == LinkState.READY) sendMessage(FlexLink.T_BYE, ByteArray(0))
        runCatching { advertiser?.stopAdvertising(advCallback) }
        runCatching { peer?.let { server?.cancelConnection(it) } }
        runCatching { server?.close() }
        server = null; txChar = null; peer = null; advertiser = null
        outbox.clear(); reasm.reset(); antiReplay.reset()
        mtu = 23; session = 0
        state.setLink(LinkState.OFF)
    }

    // ---------------------------------------------------------
    //  Envio
    // ---------------------------------------------------------
    /**
     * Envia un mensaje. Devuelve false si no hay con quien hablar o
     * la cola esta llena -- NUNCA bloquea esperando sitio.
     */
    @SuppressLint("MissingPermission")
    fun sendMessage(type: Int, payload: ByteArray): Boolean {
        val d = peer ?: return false
        val ch = txChar ?: return false
        if (payload.size > FlexLink.MAX_MESSAGE) return false
        if (outbox.size >= OUTBOX_MAX) { state.countDropped(); return false }

        packetId = (packetId + 1) and 0xFFFF
        val frames = try {
            FlexLink.fragment(
                type, session, packetId, payload,
                counterStart = txCounter.getAndAdd(FlexLink.MAX_FRAGS.toLong()),
                mtu = usablePayload,
            )
        } catch (e: IllegalArgumentException) {
            return false
        }
        frames.forEach { outbox.add(it) }
        pump(d, ch)
        state.countSent()
        return true
    }

    /**
     * Vacia la cola de a UNA notificacion: BLE no admite otra hasta
     * que la anterior se confirma (`onNotificationSent`). Mandarlas
     * todas de golpe hace que la pila las descarte en silencio.
     */
    @SuppressLint("MissingPermission")
    private fun pump(d: BluetoothDevice, ch: BluetoothGattCharacteristic) {
        if (sending) return
        val frame = outbox.poll() ?: return
        sending = true
        runCatching {
            if (Build.VERSION.SDK_INT >= 33) {
                server?.notifyCharacteristicChanged(d, ch, false, frame)
            } else {
                @Suppress("DEPRECATION")
                ch.value = frame
                @Suppress("DEPRECATION")
                server?.notifyCharacteristicChanged(d, ch, false)
            }
        }.onFailure { sending = false }
    }

    // ---------------------------------------------------------
    //  Callbacks
    // ---------------------------------------------------------
    private val callback = object : BluetoothGattServerCallback() {

        @SuppressLint("MissingPermission")
        override fun onConnectionStateChange(device: BluetoothDevice?, status: Int, newState: Int) {
            if (device == null) return
            if (newState == BluetoothProfile.STATE_CONNECTED) {
                peer = device
                session = (System.currentTimeMillis().toInt() or 1) and 0xFFFF
                reasm.reset(); antiReplay.reset()
                state.setLink(LinkState.CONNECTING)
                Log.i(TAG, "conectado")            // sin la direccion MAC
            } else {
                peer = null
                mtu = 23
                outbox.clear(); sending = false
                reasm.reset(); antiReplay.reset()
                state.countReconnect()
                // Se vuelve a ANUNCIAR, no a "error": el reloj puede
                // haberse alejado un momento.
                state.setLink(LinkState.ADVERTISING)
            }
        }

        override fun onMtuChanged(device: BluetoothDevice?, newMtu: Int) {
            // Se acepta el MTU REAL que negocio la pila. No se asume
            // el deseado: si el central solo da 23, se fragmenta mas.
            mtu = newMtu.coerceIn(23, DESIRED_MTU)
            Log.i(TAG, "MTU negociado: $mtu")
        }

        @SuppressLint("MissingPermission")
        override fun onCharacteristicWriteRequest(
            device: BluetoothDevice?, requestId: Int,
            characteristic: BluetoothGattCharacteristic?,
            preparedWrite: Boolean, responseNeeded: Boolean,
            offset: Int, value: ByteArray?,
        ) {
            if (responseNeeded) {
                runCatching {
                    server?.sendResponse(device, requestId, BluetoothGatt.GATT_SUCCESS, offset, null)
                }
            }
            if (characteristic?.uuid != RX) return
            val data = value ?: return
            handleIncoming(data)
        }

        @SuppressLint("MissingPermission")
        override fun onDescriptorWriteRequest(
            device: BluetoothDevice?, requestId: Int,
            descriptor: BluetoothGattDescriptor?, preparedWrite: Boolean,
            responseNeeded: Boolean, offset: Int, value: ByteArray?,
        ) {
            if (responseNeeded) {
                runCatching {
                    server?.sendResponse(device, requestId, BluetoothGatt.GATT_SUCCESS, offset, null)
                }
            }
        }

        @SuppressLint("MissingPermission")
        override fun onNotificationSent(device: BluetoothDevice?, status: Int) {
            sending = false
            val d = device ?: peer ?: return
            val ch = txChar ?: return
            pump(d, ch)                      // siguiente trama, si queda
        }
    }

    /** Camino completo de una trama entrante. */
    private fun handleIncoming(frame: ByteArray) {
        when (val r = FlexLink.readFrame(frame)) {
            is FlexLink.ReadResult.Err -> {
                state.countBadFrame()
                Log.w(TAG, "trama descartada: ${r.reason}")   // el motivo, no los bytes
            }
            is FlexLink.ReadResult.Ok -> {
                // Anti-repeticion ANTES del reensamblador: una trama
                // repetida no puede alterar un mensaje a medias.
                if (!antiReplay.check(r.header.counter)) { state.countDropped(); return }
                state.countReceived()
                when (reasm.feed(r.header, r.payload, System.currentTimeMillis())) {
                    Reassembler.Result.DONE -> onMessage(r.header.type, reasm.message())
                    Reassembler.Result.NEED_MORE -> Unit
                    Reassembler.Result.DROP -> state.countDropped()
                }
            }
        }
    }

    /** Caduca un parcial que nunca se completo. Lo llama el servicio. */
    fun tick(nowMs: Long) {
        if (reasm.expire(nowMs, 5_000)) state.countDropped()
    }

    private fun hasPermissions(): Boolean {
        val need = if (Build.VERSION.SDK_INT >= 31)
            listOf(
                android.Manifest.permission.BLUETOOTH_CONNECT,
                android.Manifest.permission.BLUETOOTH_ADVERTISE,
            )
        else listOf(android.Manifest.permission.BLUETOOTH)
        return need.all {
            ctx.checkSelfPermission(it) == android.content.pm.PackageManager.PERMISSION_GRANTED
        }
    }
}
