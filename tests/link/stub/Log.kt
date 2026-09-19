package android.util

/** Las lineas del enlace van a la salida estandar para poder leerlas. */
object Log {
    @Volatile var verbose = false
    fun d(tag: String, msg: String): Int { if (verbose) println("  [$tag] $msg"); return 0 }
    fun w(tag: String, msg: String): Int { if (verbose) println("  [$tag] AVISO $msg"); return 0 }
    fun e(tag: String, msg: String): Int { println("  [$tag] ERROR $msg"); return 0 }
}
