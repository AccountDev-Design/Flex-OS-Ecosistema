package com.flexos.flexphone.protocol

/**
 * REGISTRO DE DIAGNOSTICO DEL ENLACE (telefono).
 *
 * Para saber QUIEN cierra una conexion y POR QUE sin tener que dejar logcat
 * encendido ni conectar un cable. Cumple lo que se le pide a un diagnostico que
 * no puede formar parte del comportamiento normal:
 *
 *  · SIEMPRE ACOTADO: un anillo de [CAPACITY] entradas. Nunca crece y la
 *    entrada mas vieja se pisa. Unos pocos KB en total.
 *  · NO BLOQUEA: la seccion critica es una asignacion de indice y una escritura
 *    en un array. No hay E/S, ni red, ni espera.
 *  · NO ES RUIDO: no escribe en logcat salvo que se encienda [mirror] a mano
 *    (interruptor "Registro detallado" en Diagnostico). Por defecto, silencio.
 *  · NUNCA CONTENIDO: se apuntan hechos (numero de conexion, motivo del cierre,
 *    cuanto duro). Ni mensajes, ni claves, ni el codigo de emparejamiento.
 *
 * JVM puro (sin `android.*`): lo comparten el servidor del enlace, el servicio
 * y las pruebas de PC.
 */
object LinkDiag {
    const val CAPACITY = 160

    /** Una entrada: cuando, que etiqueta, y el hecho. */
    data class Entry(val atMs: Long, val tag: String, val text: String)

    private val buf = arrayOfNulls<Entry>(CAPACITY)
    private var next = 0
    private var total = 0L
    private val lock = Any()

    /**
     * Si no es null, cada entrada se copia ADEMAS a este destino (logcat en la
     * app). Lo pone la app cuando la persona enciende el registro detallado.
     */
    @Volatile var mirror: ((tag: String, text: String) -> Unit)? = null

    fun d(tag: String, text: String) = add(tag, text)
    fun w(tag: String, text: String) = add(tag, "AVISO $text")

    fun add(tag: String, text: String) {
        val e = Entry(System.currentTimeMillis(), tag, text.take(160))
        synchronized(lock) {
            buf[next] = e
            next = (next + 1) % CAPACITY
            total++
        }
        mirror?.invoke(tag, e.text)
    }

    /** Las ultimas [max] entradas, de la MAS RECIENTE a la mas vieja. */
    fun recent(max: Int = CAPACITY): List<Entry> = synchronized(lock) {
        val n = minOf(max, total.coerceAtMost(CAPACITY.toLong()).toInt())
        List(n) { i -> buf[(next - 1 - i + 2 * CAPACITY) % CAPACITY]!! }
    }

    /** Cuantas entradas se han apuntado desde que arranco el proceso. */
    fun count(): Long = synchronized(lock) { total }

    fun clear() = synchronized(lock) { buf.fill(null); next = 0; total = 0 }
}
