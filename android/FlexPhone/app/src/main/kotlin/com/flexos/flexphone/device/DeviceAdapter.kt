package com.flexos.flexphone.device

import android.app.ActivityManager
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.net.ConnectivityManager
import android.net.NetworkCapabilities
import android.os.BatteryManager
import android.os.Build
import android.os.Environment
import android.os.PowerManager
import android.os.StatFs
import android.provider.Settings
import com.flexos.flexphone.protocol.Caps
import com.flexos.flexphone.protocol.CapsPayload
import com.flexos.flexphone.protocol.FlexLink
import com.flexos.flexphone.protocol.PhoneState

/**
 * ADAPTADOR DE DISPOSITIVO.
 *
 * Es la unica pieza de la app que mira el hardware y el fabricante.
 * Todo lo de arriba -- protocolo, enlace, notificaciones, servidor --
 * habla con este adaptador y NO con `Build.MANUFACTURER`.
 *
 * POR QUE IMPORTA
 * ---------------
 * El Galaxy A55 es el telefono de referencia, no la plataforma. Si
 * el nucleo consultara Samsung directamente, cada arreglo para un
 * Samsung seria un riesgo nuevo para un Pixel, un Motorola o un
 * Xiaomi. Aqui las diferencias de fabricante se quedan encerradas en
 * un sitio, y son SIEMPRE opcionales: lo que no se reconoce cae al
 * camino estandar de Android, que es el que funciona en todos.
 *
 * NADA DE APIS PRIVADAS. Solo APIs publicas del SDK. No se usan
 * Knox, One UI ni reflexion sobre clases ocultas: eso ata la app a
 * un fabricante y se rompe en la siguiente version del sistema.
 *
 * Y NADA DE DATOS QUE NO HAGAN FALTA: no se lee IMEI, ni numero de
 * serie, ni cuentas, ni ubicacion. Lo que sale de aqui es lo que
 * Flex OS ensena en pantalla y nada mas.
 */
class DeviceAdapter(private val ctx: Context) {

    /** Nombre legible del telefono. Es el que el usuario reconoce. */
    val displayName: String
        get() {
            // El nombre que el usuario le puso al dispositivo, si lo
            // hay. Es mas util que "SM-A556B" para alguien que tenga
            // dos telefonos en la misma red.
            val userName = runCatching {
                Settings.Global.getString(ctx.contentResolver, "device_name")
            }.getOrNull()
            if (!userName.isNullOrBlank()) return userName
            val model = Build.MODEL ?: ""
            val brand = (Build.BRAND ?: "").replaceFirstChar { it.uppercase() }
            return when {
                model.isBlank() -> brand.ifBlank { "Android" }
                model.startsWith(brand, ignoreCase = true) -> model
                brand.isBlank() -> model
                else -> "$brand $model"
            }
        }

    val model: String get() = Build.MODEL ?: ""
    val vendor: String get() = Build.MANUFACTURER ?: ""
    val osVersion: String get() = "Android ${Build.VERSION.RELEASE ?: Build.VERSION.SDK_INT}"

    // ---------------------------------------------------------
    //  Estado real del dispositivo
    // ---------------------------------------------------------
    /**
     * Nivel de bateria en 0..100, o -1 si el sistema no lo dice.
     * -1 viaja como 255 ("desconocida"): NUNCA como 0, que en
     * pantalla se leeria como una bateria vacia.
     */
    fun batteryPercent(): Int {
        val bm = ctx.getSystemService(Context.BATTERY_SERVICE) as? BatteryManager ?: return -1
        val v = runCatching { bm.getIntProperty(BatteryManager.BATTERY_PROPERTY_CAPACITY) }
            .getOrDefault(-1)
        return if (v in 0..100) v else -1
    }

    fun isCharging(): Boolean {
        val f = IntentFilter(Intent.ACTION_BATTERY_CHANGED)
        val i = runCatching { ctx.registerReceiver(null, f) }.getOrNull() ?: return false
        val status = i.getIntExtra(BatteryManager.EXTRA_STATUS, -1)
        return status == BatteryManager.BATTERY_STATUS_CHARGING ||
               status == BatteryManager.BATTERY_STATUS_FULL
    }

    /** ¿El ahorro de bateria de Android esta activo AHORA? */
    fun isPowerSaveMode(): Boolean {
        val pm = ctx.getSystemService(Context.POWER_SERVICE) as? PowerManager ?: return false
        return runCatching { pm.isPowerSaveMode }.getOrDefault(false)
    }

    /**
     * ¿La app esta exenta de las optimizaciones de bateria? No se
     * intenta forzar: se consulta para poder DECIRSELO al usuario.
     * Saltarse las protecciones de Android a escondidas es lo que
     * hace que este tipo de apps acaben desinstaladas.
     */
    fun isIgnoringBatteryOptimizations(): Boolean {
        val pm = ctx.getSystemService(Context.POWER_SERVICE) as? PowerManager ?: return false
        return runCatching { pm.isIgnoringBatteryOptimizations(ctx.packageName) }
            .getOrDefault(false)
    }

    /** FlexLink.NET_*: por donde sale este telefono a la red. */
    fun networkKind(): Int {
        val cm = ctx.getSystemService(Context.CONNECTIVITY_SERVICE) as? ConnectivityManager
            ?: return NET_UNKNOWN
        val caps = runCatching { cm.getNetworkCapabilities(cm.activeNetwork) }.getOrNull()
            ?: return NET_NONE
        return when {
            caps.hasTransport(NetworkCapabilities.TRANSPORT_WIFI) -> NET_WIFI
            caps.hasTransport(NetworkCapabilities.TRANSPORT_CELLULAR) -> NET_MOBILE
            caps.hasTransport(NetworkCapabilities.TRANSPORT_ETHERNET) -> NET_WIFI
            else -> NET_UNKNOWN
        }
    }

    fun isOnWifi(): Boolean = networkKind() == NET_WIFI

    /** Almacenamiento del usuario en MB. Pair(libre, total). */
    fun storageMb(): Pair<Long, Long> = runCatching {
        val st = StatFs(Environment.getDataDirectory().absolutePath)
        val free = st.availableBytes / (1024L * 1024L)
        val total = st.totalBytes / (1024L * 1024L)
        free to total
    }.getOrDefault(0L to 0L)

    /** Memoria en MB. Pair(libre, total). */
    fun memoryMb(): Pair<Long, Long> = runCatching {
        val am = ctx.getSystemService(Context.ACTIVITY_SERVICE) as ActivityManager
        val mi = ActivityManager.MemoryInfo()
        am.getMemoryInfo(mi)
        (mi.availMem / (1024L * 1024L)) to (mi.totalMem / (1024L * 1024L))
    }.getOrDefault(0L to 0L)

    /**
     * El estado completo que viaja a Flex OS. Solo campos que el
     * sistema ha dado de verdad: lo que no se sabe va a 0 o a 255, y
     * el firmware lo pinta como "--".
     */
    fun phoneState(): PhoneState {
        val (sFree, sTotal) = storageMb()
        val (mFree, mTotal) = memoryMb()
        return PhoneState(
            name = displayName,
            battery = batteryPercent(),
            charging = isCharging(),
            net = networkKind(),
            storageFreeMb = sFree, storageTotalMb = sTotal,
            ramFreeMb = mFree, ramTotalMb = mTotal,
            powerSave = isPowerSaveMode(),
        )
    }

    // ---------------------------------------------------------
    //  Capacidades
    // ---------------------------------------------------------
    /**
     * [supported] es lo que este telefono PUEDE hacer; [granted] lo
     * que ademas esta concedido y activo ahora mismo.
     *
     * `notifAccess` y `relayRunning` los sabe la app, no el
     * adaptador: son un permiso especial y un servicio, no hardware.
     */
    fun caps(notifAccess: Boolean, relayRunning: Boolean, mediaActive: Boolean): CapsPayload {
        var supported = Caps.NOTIF or Caps.STATE or Caps.TIME or Caps.RELAY or Caps.FIND
        // Responder depende de que Android exponga RemoteInput, que es
        // por notificacion y no por telefono; se declara soportado y
        // cada notificacion dice si ella admite respuesta.
        supported = supported or Caps.REPLY
        // Multimedia necesita el mismo permiso de acceso a
        // notificaciones para escuchar las sesiones.
        supported = supported or Caps.MEDIA
        if (hasBluetoothLe()) supported = supported or Caps.BLE

        var granted = Caps.STATE or Caps.TIME
        if (notifAccess) granted = granted or Caps.NOTIF or Caps.REPLY or Caps.FIND
        if (notifAccess && mediaActive) granted = granted or Caps.MEDIA
        if (relayRunning) granted = granted or Caps.RELAY
        // BLE NO se concede aunque el telefono lo tenga: hoy el enlace
        // va por Wi-Fi y no hay transporte BLE al otro lado. Anunciarlo
        // como concedido seria ofrecer algo que no existe.

        return CapsPayload(
            supported = supported,
            granted = granted,
            protoVer = FlexLink.VERSION,
            model = model,
            vendor = vendor,
            osVer = osVersion,
        )
    }

    private fun hasBluetoothLe(): Boolean = runCatching {
        ctx.packageManager.hasSystemFeature(android.content.pm.PackageManager.FEATURE_BLUETOOTH_LE)
    }.getOrDefault(false)

    // ---------------------------------------------------------
    //  Diferencias de fabricante -- opcionales, nunca obligatorias
    // ---------------------------------------------------------
    /**
     * Varios fabricantes anaden restricciones de segundo plano por
     * encima de las de Android. Aqui SOLO se devuelve un consejo para
     * ensenarselo al usuario: no se cambia nada, no se abre ningun
     * ajuste a escondidas y no se intenta esquivar el sistema.
     *
     * `null` = no hay nada especial que decir en este telefono, que es
     * el caso por defecto y el que hace que la app funcione igual en
     * un fabricante que no este en esta lista.
     */
    fun backgroundAdviceOrNull(): String? {
        if (isIgnoringBatteryOptimizations()) return null
        return when (vendor.lowercase()) {
            "xiaomi", "redmi", "poco" ->
                "En MIUI/HyperOS, marca Flex Phone como \"Sin restricciones\" y activa " +
                "\"Inicio automatico\" para que el enlace sobreviva a la pantalla apagada."
            "huawei", "honor" ->
                "En EMUI/MagicOS, pon Flex Phone en \"Gestion manual\" dentro del " +
                "lanzamiento de aplicaciones."
            "oppo", "realme", "oneplus" ->
                "En ColorOS/OxygenOS, permite la ejecucion en segundo plano de Flex Phone " +
                "y desactiva su optimizacion de bateria."
            "vivo", "iqoo" ->
                "En Funtouch/OriginOS, permite el consumo en segundo plano de Flex Phone."
            "samsung" ->
                "En One UI, saca Flex Phone de \"Aplicaciones en suspension\" dentro de " +
                "Bateria y mantenimiento."
            "meizu", "asus", "transsion", "tecno", "infinix", "itel" ->
                "Este fabricante anade su propia gestion de segundo plano: excluye " +
                "Flex Phone para que el enlace siga vivo."
            // Google, Motorola, Nothing, Sony y los demas se quedan con
            // el consejo estandar de Android, que es el que hay abajo.
            else ->
                "Excluye Flex Phone de la optimizacion de bateria para que el enlace " +
                "siga vivo con la pantalla apagada."
        }
    }

    companion object {
        // Mismos numeros que FLP_NET_* en FlexOS_FlexPhone.h.
        const val NET_UNKNOWN = 0
        const val NET_NONE = 1
        const val NET_WIFI = 2
        const val NET_MOBILE = 3
    }
}
