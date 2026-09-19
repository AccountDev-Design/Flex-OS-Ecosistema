package com.flexos.flexphone.protocol

/**
 * SESION DE EMPAREJAMIENTO -- lado Android.
 *
 * ESPEJO de `FlexPhonePairing` en `FlexOS_FlexPhone_Link.h`. Los dos
 * extremos tienen ahora la misma idea de que es una sesion: un
 * codigo, una sal, un reto, un plazo y un estado, todo con el mismo
 * dueno.
 *
 * POR QUE EXISTE ESTE FICHERO
 * ---------------------------
 * El codigo tecleado vivia suelto en el servicio (`typedCode`) y
 * sobrevivia a los intentos fallidos. Cuando el usuario volvia a
 * pulsar "Emparejar telefono" en el reloj, Flex OS generaba un codigo
 * y una SAL NUEVOS, mandaba su PAIR_CODE... y esta app contestaba
 * sola, en el acto, con el codigo del intento ANTERIOR. Flex OS lo
 * rechazaba -- correctamente -- y el usuario leia "el codigo no
 * coincide" sin que le hubieran dejado teclear el nuevo.
 *
 * La regla que lo arregla esta en [submit]: un codigo pertenece a LA
 * SAL para la que se tecleo. Una sal distinta es una sesion distinta,
 * y lo tecleado antes no se reaprovecha jamas.
 *
 * Kotlin/JVM puro: sin `android.*`. Se prueba con un JDK, sin
 * emulador y sin placa (`PairingSessionTest`).
 */
class PairingSession(
    /** Sal de ESTA sesion, tal como la mando Flex OS. Es su identidad. */
    val salt: ByteArray,
    /** Reto de ESTA sesion. La prueba se calcula sobre el. */
    val nonce: ByteArray,
    /** Identificador del Flex OS que abrio la sesion. */
    val hostId: String,
    /** Identificador de este telefono. */
    val phoneId: String,
    /** Cuando llego la sal, en el MISMO reloj que se le pasa a [state]. */
    val startedAt: Long,
) {
    init {
        require(salt.size == FlexAuth.SALT_SIZE) { "sal de tamano incorrecto" }
        require(nonce.size == FlexAuth.NONCE_SIZE) { "reto de tamano incorrecto" }
    }

    enum class State {
        /** Hay sal y se espera a que el usuario teclee el codigo. */
        AWAITING_CODE,
        /** Se mando PAIR_CONFIRM y se espera la respuesta de Flex OS. */
        CONFIRM_SENT,
        /** Flex OS acepto el codigo y demostro su identidad. */
        DONE,
        /** Flex OS rechazo el codigo. Se puede volver a teclear. */
        REJECTED,
    }

    /**
     * El codigo que el usuario tecleo PARA ESTA SAL, o null.
     *
     * No es publico de escritura a proposito: entra por [submit], que
     * es el unico sitio que comprueba el formato y el plazo.
     */
    var code: String? = null
        private set

    var state: State = State.AWAITING_CODE
        private set

    /** Clave derivada del codigo de esta sesion, o null si aun no hay. */
    var key: ByteArray? = null
        private set

    /** Cuantas veces rechazo Flex OS un codigo de esta sesion. */
    var rejects: Int = 0
        private set

    /**
     * El plazo va con la SESION, no con el socket.
     *
     * Son los mismos dos minutos que `FLP_LINK_PAIR_WINDOW_MS` en el
     * firmware, y se cuentan desde que llego la sal. Contarlos desde
     * que se abrio la conexion -- que es lo que hacia el servidor --
     * le daba al usuario quince segundos para leer seis digitos de la
     * pantalla de un reloj y teclearlos.
     */
    val expiresAt: Long get() = startedAt + WINDOW_MS

    fun isExpired(now: Long): Boolean = now >= expiresAt

    fun remainingMs(now: Long): Long = (expiresAt - now).coerceAtLeast(0L)

    /** ¿Es esta la sal de esta sesion? Es lo que decide si algo se reaprovecha. */
    fun hasSalt(other: ByteArray): Boolean = salt.contentEquals(other)

    /** Resultado de teclear un codigo. Cada rama tiene un motivo REAL detras. */
    sealed class Submit {
        /** Listo para enviar: [proof] es lo unico que sale por la red. */
        data class Ready(val proof: ByteArray, val key: ByteArray) : Submit() {
            override fun equals(other: Any?): Boolean =
                other is Ready && proof.contentEquals(other.proof) && key.contentEquals(other.key)
            override fun hashCode(): Int = proof.contentHashCode() * 31 + key.contentHashCode()
        }
        /** No son seis digitos. No se deriva nada con basura. */
        object BadFormat : Submit()
        /** El codigo de Flex OS ya caduco: hay que pedir otro en el reloj. */
        object Expired : Submit()
        /** Ya hay un PAIR_CONFIRM en vuelo para este mismo codigo. */
        object AlreadyInFlight : Submit()
        /** La sesion ya se cerro con exito. */
        object AlreadyDone : Submit()
    }

    /**
     * El usuario tecleo [typed]. Devuelve la prueba que hay que
     * mandar, o el motivo REAL por el que todavia no se puede.
     *
     * DOS PULSACIONES SEGUIDAS NO MANDAN DOS PRUEBAS. Si ya hay una en
     * vuelo con el mismo codigo se dice [Submit.AlreadyInFlight] en
     * vez de abrir un segundo intento: dos PAIR_CONFIRM identicos solo
     * sirven para que el anti-repeticion del reloj tire uno y el
     * usuario no sepa cual conto.
     */
    fun submit(typed: String, now: Long): Submit {
        if (state == State.DONE) return Submit.AlreadyDone
        if (!FlexAuth.isValidCode(typed)) return Submit.BadFormat
        if (isExpired(now)) return Submit.Expired
        if (state == State.CONFIRM_SENT && typed == code) return Submit.AlreadyInFlight

        // El codigo se guarda con la sal para la que se tecleo. Esa
        // pareja es lo que impide que reaparezca en la sesion siguiente.
        code = typed
        val k = FlexAuth.deriveKey(typed, salt, hostId, phoneId)
        key = k
        state = State.CONFIRM_SENT
        // La prueba del emparejamiento va SIEMPRE sobre sesion 0: es lo
        // que verifica `FLNK_T_PAIR_CONFIRM` en el firmware.
        return Submit.Ready(FlexAuth.proof(k, FlexAuth.ROLE_PHONE, nonce, PAIR_SESSION), k)
    }

    /**
     * La prueba del codigo QUE YA ESTABA TECLEADO para esta sal.
     *
     * Existe por la reconexion: el reloj reenvia su sal cada vez que
     * se reabre el canal, y la prueba que se mando por el socket
     * anterior murio con el. Esto la recalcula sin pedirle al usuario
     * que vuelva a teclear lo que ya tecleo -- y sin inventarse nada:
     * si no hay codigo para ESTA sal, devuelve null.
     */
    fun proofForTypedCode(now: Long): Submit.Ready? {
        val c = code ?: return null
        if (state == State.DONE || isExpired(now)) return null
        val k = key ?: FlexAuth.deriveKey(c, salt, hostId, phoneId)
        key = k
        state = State.CONFIRM_SENT
        return Submit.Ready(FlexAuth.proof(k, FlexAuth.ROLE_PHONE, nonce, PAIR_SESSION), k)
    }

    /**
     * La prueba NO PUDO SALIR (socket caido al enviar, o Flex OS no
     * contesto). Eso no dice nada sobre el codigo.
     *
     * Por eso NO es [onRejected]: el codigo tecleado se conserva, y
     * [proofForTypedCode] lo reenvia en cuanto el reloj vuelve a
     * abrir el canal. Tratarlo como un rechazo borraba el codigo y
     * dejaba al usuario tecleando otra vez lo mismo, contra un reloj
     * que seguia ensenando exactamente el mismo codigo.
     */
    fun onSendFailed() {
        if (state == State.CONFIRM_SENT) state = State.AWAITING_CODE
    }

    /** Flex OS rechazo el codigo. La sesion SIGUE VIVA: se puede corregir un digito. */
    fun onRejected() {
        if (state == State.DONE) return
        rejects++
        state = State.REJECTED
        // El codigo rechazado no se conserva: el siguiente intento
        // tiene que venir del usuario, no de lo que hubiera guardado.
        code = null
        key = null
    }

    /** Flex OS acepto y demostro su identidad. */
    fun onAccepted() { state = State.DONE }

    /** ¿Se puede teclear ya? La pantalla lo usa para no pedir el codigo antes de tiempo. */
    fun acceptsCode(now: Long): Boolean =
        state != State.DONE && !isExpired(now)

    companion object {
        /**
         * Los MISMOS dos minutos que `FLP_LINK_PAIR_WINDOW_MS`.
         * Cambiar uno solo de los dos lados deja al usuario con un
         * codigo que un extremo da por bueno y el otro no.
         */
        const val WINDOW_MS = 120_000L

        /**
         * La prueba del emparejamiento va sobre la "sesion" 0, porque
         * en ese momento todavia no hay ninguna abierta. Es el mismo
         * cero que usa el firmware al verificar.
         */
        const val PAIR_SESSION = 0
    }
}

/**
 * POR QUE FALLO UN EMPAREJAMIENTO, de verdad.
 *
 * Vive en `:protocol` y no en la capa de red a proposito: es lo que
 * la pantalla necesita para decir la verdad, y meterlo dentro del
 * servidor obligaba al modelo de la app a depender de los sockets
 * para nombrar un fallo.
 *
 * La diferencia importa: "el codigo no coincide" y "se cayo el
 * socket" se arreglan de formas distintas, y ensenar el primero
 * cuando paso el segundo manda al usuario a revisar unos digitos que
 * estaban bien.
 */
enum class PairFailure {
    /** Flex OS comparo el codigo y no cuadro. */
    CODE_REJECTED,
    /** El codigo de Flex OS caduco antes de que llegara la prueba. */
    CODE_EXPIRED,
    /** Se corto la conexion o nadie contesto. NO se llego a comparar ningun codigo. */
    LINK,
}
