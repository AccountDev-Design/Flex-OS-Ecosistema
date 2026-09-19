package android.net.wifi

/** El cerrojo de multidifusion no existe en un PC: aqui no hace nada. */
class WifiManager {
    class MulticastLock {
        val isHeld: Boolean = false
        fun setReferenceCounted(v: Boolean) {}
        fun acquire() {}
        fun release() {}
    }
    fun createMulticastLock(tag: String): MulticastLock = MulticastLock()
}
