package com.flexos.flexphone.flexcloud

import android.content.Context
import android.net.ConnectivityManager
import android.net.NetworkCapabilities
import android.os.Build
import android.os.storage.StorageManager
import android.provider.Settings
import android.util.Log
import com.flexos.flexphone.cloud.CloudConfig
import com.flexos.flexphone.cloud.CloudServer
import com.flexos.flexphone.cloud.CloudStore
import com.flexos.flexphone.cloud.Http
import com.flexos.flexphone.cloud.ObjectStore
import com.flexos.flexphone.cloud.PhoneInfo
import com.flexos.flexphone.cloud.SessionManager
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import java.io.File
import java.io.IOException
import java.net.Inet4Address
import java.net.InetAddress
import java.security.SecureRandom

/**
 * FLEX CLOUD EN ESTE TELEFONO (Flex Storage) · lo que hay por proceso.
 *
 * El servidor de Flex Cloud (modulo :storage, probado en el PC) necesita en
 * Android cuatro cosas, y todas salen de aqui:
 *
 *  · UNA CARPETA PRIVADA de la app (`filesDir/FlexCloud`): ni otras apps ni el
 *    P4 pueden recorrer nada mas del telefono. No hace falta ningun permiso de
 *    almacenamiento, y no se pide ninguno.
 *  · LA CUOTA QUE ELIGE LA PERSONA (1, 2 o 5 GB; 5 de serie). Es el espacio que
 *    el telefono "presta" a Flex OS; lo que de verdad cabe es el menor entre
 *    eso y lo que el telefono tiene libre (menos un margen), y asi se publica.
 *  · LA DIRECCION WI-FI del telefono: el servidor escucha SOLO ahi y solo si es
 *    de una red privada. Con datos moviles no escucha, y en ningun caso queda
 *    abierto a Internet (ademas, el propio servidor cierra cualquier conexion
 *    que no venga de la red local).
 *  · EL EMPAREJAMIENTO, envuelto con el Keystore ([KeystorePairingRepo]).
 *
 * El estado ([status]) lo leen la pantalla, la notificacion y la actividad del
 * enlace `flexstorage://`; lo refresca el servicio ([FlexStorageService]).
 */
object FlexCloudPhone {
    private const val TAG = "FlexPhone/FlexCloud"
    private const val PREFS = "flexcloud"
    private const val P_ENABLED = "enabled"
    private const val P_QUOTA_GB = "quota_gb"
    private const val P_PHONE_ID = "phone_id"
    private const val P_PORT = "port"
    private const val GIB = 1L shl 30

    /** Las cuotas que se ofrecen (en GB). */
    val QUOTA_CHOICES_GB = intArrayOf(1, 2, 5)

    data class Status(
        val running: Boolean = false,
        val address: String? = null,          // "192.168.1.60:47830" mientras escucha
        val error: String? = null,            // por que no esta escuchando (o un aviso)
        val paired: Boolean = false,
        val p4Name: String? = null,
        val p4Host: String? = null,
        val lastContactMs: Long = 0L,         // ultima peticion autenticada del P4
        val quotaGb: Int = 5,
        val usedBytes: Long = 0L,
        val trashBytes: Long = 0L,
        val reservedBytes: Long = 0L,
        val availableBytes: Long = 0L,
        val limitedByDevice: Boolean = false,
        val files: Int = 0,
    )

    private val _status = MutableStateFlow(Status())
    val status: StateFlow<Status> = _status

    private val lock = Any()
    @Volatile private var store: CloudStore? = null
    @Volatile private var storeQuotaGb = 0
    @Volatile private var server: CloudServer? = null

    private fun prefs(ctx: Context) = ctx.applicationContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE)

    // ------------------------------------------------------------ ajustes
    /** La persona activo Flex Cloud (y no lo ha detenido): se reanuda tras reiniciar. */
    fun isEnabled(ctx: Context): Boolean = prefs(ctx).getBoolean(P_ENABLED, false)
    fun setEnabled(ctx: Context, on: Boolean) { prefs(ctx).edit().putBoolean(P_ENABLED, on).apply() }

    fun quotaGb(ctx: Context): Int {
        val q = prefs(ctx).getInt(P_QUOTA_GB, 5)
        return if (QUOTA_CHOICES_GB.contains(q)) q else 5
    }

    /**
     * Cambia la cuota. No se deja bajar de lo que ya esta guardado (contando la
     * papelera y las subidas en curso): se diria que hay espacio que no existe.
     * Devuelve null si se aplico, o el motivo.
     */
    fun setQuotaGb(ctx: Context, gb: Int): String? {
        if (!QUOTA_CHOICES_GB.contains(gb)) return "Cuota no valida"
        val s = _status.value
        if (gb * GIB < s.usedBytes + s.reservedBytes) return "Ya hay mas guardado que eso: libera espacio en Flex Cloud primero"
        prefs(ctx).edit().putInt(P_QUOTA_GB, gb).apply()
        synchronized(lock) {
            // El almacen se rehace con la cuota nueva; si el servidor estaba
            // escuchando, se reinicia en el mismo puerto.
            val wasRunning = server?.isRunning == true
            stopServerLocked()
            store = null
            if (wasRunning) startServerLocked(ctx)
        }
        refresh(ctx)
        return null
    }

    // ------------------------------------------------------------ identidad
    /**
     * Quien es este telefono para Flex OS: un id aleatorio y estable (no el
     * ANDROID_ID ni nada que lo identifique fuera de aqui), el nombre que la
     * persona le puso en Ajustes de Android y el modelo.
     */
    fun phoneInfo(ctx: Context): PhoneInfo {
        val p = prefs(ctx)
        var id = p.getString(P_PHONE_ID, null)
        if (id.isNullOrEmpty()) {
            val b = ByteArray(8).also { SecureRandom().nextBytes(it) }
            id = "fs-" + b.joinToString("") { "%02x".format(it) }
            p.edit().putString(P_PHONE_ID, id).apply()
        }
        val name = try { Settings.Global.getString(ctx.contentResolver, "device_name") } catch (e: Exception) { null }
        val model = Build.MODEL ?: "Android"
        return PhoneInfo(id, (name?.takeIf { it.isNotBlank() } ?: model).take(47), model.take(23))
    }

    fun repo(ctx: Context): KeystorePairingRepo = KeystorePairingRepo(ctx)

    /** La carpeta de Flex Cloud: privada de la app. */
    fun root(ctx: Context): File = File(ctx.applicationContext.filesDir, "FlexCloud")

    // ------------------------------------------------------------ red
    /**
     * La direccion IPv4 de la Wi-Fi, solo si es de una red privada. Sin Wi-Fi
     * (o con una direccion publica) devuelve null y el servidor no escucha.
     */
    @Suppress("DEPRECATION")
    fun wifiAddress(ctx: Context): InetAddress? {
        val cm = ctx.applicationContext.getSystemService(ConnectivityManager::class.java) ?: return null
        for (n in cm.allNetworks) {
            val caps = cm.getNetworkCapabilities(n) ?: continue
            if (!caps.hasTransport(NetworkCapabilities.TRANSPORT_WIFI)) continue
            val lp = cm.getLinkProperties(n) ?: continue
            for (la in lp.linkAddresses) {
                val a = la.address
                if (a is Inet4Address && !a.isLoopbackAddress && Http.isLocal(a)) return a
            }
        }
        return null
    }

    // ------------------------------------------------------------ almacen
    private fun storeLocked(ctx: Context): CloudStore {
        val gb = quotaGb(ctx)
        store?.let { if (storeQuotaGb == gb) return it }
        val app = ctx.applicationContext
        val dir = root(app)
        val sm = app.getSystemService(StorageManager::class.java)
        val objects = File(dir, "data")
        if (!objects.isDirectory) objects.mkdirs()
        val uuid = try { sm?.getUuidForPath(objects) } catch (e: IOException) { null }
        // Espacio REAL que le queda a la app (incluye la cache que Android
        // puede liberar) y reserva de verdad (fallocate) antes de escribir.
        val os = if (sm != null && uuid != null) ObjectStore(
            objects,
            freeSpace = { sm.getAllocatableBytes(uuid) },
            allocate = { raf, size -> sm.allocateBytes(raf.fd, size) },
            reservesOnCreate = true,
        ) else ObjectStore(objects)
        val st = CloudStore(os, File(dir, "meta"), CloudConfig(quotaBytes = gb * GIB), log = { Log.i(TAG, it) })
        st.recover()
        store = st
        storeQuotaGb = gb
        return st
    }

    // ------------------------------------------------------------ servidor
    /** Arranca el servidor (si no lo estaba). Devuelve null o el motivo para la persona. */
    fun startServer(ctx: Context): String? = synchronized(lock) { startServerLocked(ctx) }

    private fun startServerLocked(ctx: Context): String? {
        if (server?.isRunning == true) return null
        val bind = wifiAddress(ctx) ?: return fail(ctx, "Sin Wi-Fi: conecta el teléfono a la misma red que Flex OS")
        val st = try { storeLocked(ctx) } catch (e: Exception) {
            Log.w(TAG, "no se pudo abrir Flex Cloud: ${e.javaClass.simpleName}")
            return fail(ctx, "No se pudo abrir la carpeta de Flex Cloud")
        }
        val app = ctx.applicationContext
        val srv = CloudServer(st, SessionManager(), repo(app), { phoneInfo(app) }, log = { Log.i(TAG, it) })
        val p = prefs(app)
        val want = p.getInt(P_PORT, CloudServer.DEFAULT_PORT)
        var notice: String? = null
        val port = try {
            srv.start(bind, want, fallbackAny = false)
        } catch (e: IOException) {
            // Puerto ocupado: otro libre. Flex OS conoce el de antes, asi que hay
            // que volver a activar Flex Cloud desde la web para que sepa el nuevo.
            val alt = try { srv.start(bind, 0, fallbackAny = false) } catch (e2: IOException) {
                return fail(app, "No se pudo abrir el servidor de Flex Cloud")
            }
            if (p.contains(P_PORT)) notice = "Puerto nuevo: vuelve a activar Flex Cloud desde la web de Flex OS"
            alt
        }
        p.edit().putInt(P_PORT, port).apply()
        server = srv
        _status.value = _status.value.copy(running = true, address = "${bind.hostAddress}:$port", error = notice)
        refresh(app)
        return null
    }

    private fun fail(ctx: Context, why: String): String {
        _status.value = _status.value.copy(running = false, address = null, error = why)
        refresh(ctx)
        return why
    }

    fun stopServer(ctx: Context) {
        synchronized(lock) { stopServerLocked() }
        _status.value = _status.value.copy(running = false, address = null)
        refresh(ctx)
    }

    private fun stopServerLocked() {
        try { server?.stop() } catch (e: Exception) { /* ya parado */ }
        server = null
    }

    /** La Wi-Fi cambio (otra IP, otra red): se vuelve a escuchar en la direccion nueva. */
    fun rebind(ctx: Context): String? = synchronized(lock) {
        val now = wifiAddress(ctx)
        val cur = _status.value.address?.substringBefore(':')
        if (server?.isRunning == true && now != null && now.hostAddress == cur) return@synchronized null
        stopServerLocked()
        startServerLocked(ctx)
    }

    /** Puerto en el que escucha (0 = no escucha). */
    fun port(): Int = server?.takeIf { it.isRunning }?.port ?: 0

    // ------------------------------------------------------------ estado
    /** Relee el estado: emparejamiento, ultimo contacto y la cuota REAL. */
    fun refresh(ctx: Context) {
        val app = ctx.applicationContext
        val r = repo(app)
        // Sin haber usado nunca Flex Cloud no se crea nada en disco solo por mirar.
        val st = store ?: if (!r.isPaired() && !root(app).isDirectory) null
            else try { synchronized(lock) { storeLocked(app) } } catch (e: Exception) { null }
        var s = _status.value.copy(
            paired = r.isPaired(), p4Name = r.p4Name(), p4Host = r.p4Host(),
            lastContactMs = server?.lastP4Contact ?: _status.value.lastContactMs,
            quotaGb = quotaGb(app),
        )
        if (st != null) {
            try {
                val q = st.quota()
                fun n(k: String) = (q[k] as? Number)?.toLong() ?: 0L
                s = s.copy(
                    usedBytes = n("usedBytes"), trashBytes = n("trashBytes"), reservedBytes = n("reservedBytes"),
                    availableBytes = n("availableBytes"), limitedByDevice = q["limitedByDevice"] == true,
                    files = st.fileCount(),
                )
            } catch (e: Exception) { /* la proxima vez */ }
        }
        _status.value = s
    }

    /**
     * Olvidar Flex OS: se borra el emparejamiento (y su clave del Keystore) y se
     * deja de servir. Los archivos se quedan (se borran aparte, con [wipe]).
     */
    fun forget(ctx: Context) {
        stopServer(ctx)
        setEnabled(ctx, false)
        repo(ctx).clear()
        refresh(ctx)
    }

    /** Borrar TODO lo de Flex Cloud de este telefono (con el servidor parado). */
    fun wipe(ctx: Context): Boolean {
        synchronized(lock) {
            stopServerLocked()
            store = null
        }
        val ok = root(ctx).deleteRecursively()
        _status.value = Status(quotaGb = quotaGb(ctx))
        refresh(ctx)
        return ok
    }
}
