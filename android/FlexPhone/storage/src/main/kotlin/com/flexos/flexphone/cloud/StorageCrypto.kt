package com.flexos.flexphone.cloud

import java.math.BigInteger
import java.security.KeyFactory
import java.security.KeyPair
import java.security.KeyPairGenerator
import java.security.SecureRandom
import java.security.interfaces.ECPrivateKey
import java.security.interfaces.ECPublicKey
import java.security.spec.ECGenParameterSpec
import java.security.spec.ECParameterSpec
import java.security.spec.ECPoint
import java.security.spec.ECPrivateKeySpec
import java.security.spec.ECPublicKeySpec
import javax.crypto.KeyAgreement
import javax.crypto.Mac
import javax.crypto.spec.SecretKeySpec

/**
 * FLEX STORAGE · criptografia del emparejamiento y de las sesiones.
 *
 * ESPEJO EXACTO de `FlexOS_StorageCore.cpp` del firmware. Lo que tiene que
 * coincidir byte a byte es lo que entra en cada HMAC: las etiquetas, el orden y
 * la longitud delante de cada campo. Si un lado cambia y el otro no, el
 * emparejamiento falla sin que nadie pueda decir por que; por eso
 * `StorageCryptoTest` y `tests/host/test_storagecore.cpp` fijan los MISMOS
 * vectores dorados (y el ECDH, los del RFC 5903).
 *
 * El esquema (ver docs/FLEX-STORAGE.md, seccion 4):
 *   Z    = ECDH P-256 (coordenada X, 32 bytes)
 *   K    = HMAC(Z, "flexstorage-v1-key" ‖ lp(oferta) ‖ lp(idP4) ‖ lp(idTel))
 *   SAS  = u32be(HMAC(K, "flexstorage-v1-sas")[0..3]) mod 1 000 000   (6 cifras)
 *   pruebas con etiquetas distintas por rol; sesion por reto-respuesta.
 * lp(x) = un byte con la longitud UTF-8 de x seguido de x (sin ella, ("ab","c")
 * y ("a","bc") darian lo mismo).
 *
 * QUE NO HACE: cifrar los archivos. Viajan en claro por la red local (HTTP),
 * como la web del P4 y Flex Phone. Esto impide que un equipo no emparejado use
 * Flex Cloud y que nadie que solo escuche se lleve la clave.
 */
object StorageCrypto {
    const val KEY_SIZE = 32
    const val POINT_SIZE = 65            // 0x04 ‖ X(32) ‖ Y(32)

    private val TAG_KEY = "flexstorage-v1-key".toByteArray(Charsets.US_ASCII)
    private val TAG_SAS = "flexstorage-v1-sas".toByteArray(Charsets.US_ASCII)
    private val TAG_PHONE_OK = "flexstorage-v1-phone-ok".toByteArray(Charsets.US_ASCII)
    private val TAG_P4_OK = "flexstorage-v1-p4-ok".toByteArray(Charsets.US_ASCII)
    private val TAG_KNOWN = "flexstorage-v1-known".toByteArray(Charsets.US_ASCII)
    private val TAG_SESS = "flexstorage-v1-sess".toByteArray(Charsets.US_ASCII)
    private val TAG_SESS_OK = "flexstorage-v1-sess-ok".toByteArray(Charsets.US_ASCII)

    private val rng = SecureRandom()

    fun random(n: Int): ByteArray = ByteArray(n).also { rng.nextBytes(it) }

    fun hmac(key: ByteArray, vararg parts: ByteArray): ByteArray {
        val mac = Mac.getInstance("HmacSHA256")
        mac.init(SecretKeySpec(key, "HmacSHA256"))
        for (p in parts) mac.update(p)
        return mac.doFinal()
    }

    /** Campo con su longitud delante (1 byte). Un campo de mas de 255 bytes no es valido. */
    fun lp(s: String): ByteArray {
        val b = s.toByteArray(Charsets.UTF_8)
        require(b.size <= 255) { "campo demasiado largo" }
        return byteArrayOf(b.size.toByte()) + b
    }

    fun deriveKey(z: ByteArray, offer: String, p4Id: String, phoneId: String): ByteArray =
        hmac(z, TAG_KEY, lp(offer), lp(p4Id), lp(phoneId))

    /** Codigo de verificacion de 6 cifras que ensenan los dos lados. */
    fun sas(k: ByteArray): String {
        val h = hmac(k, TAG_SAS)
        val v = ((h[0].toLong() and 0xFF) shl 24) or ((h[1].toLong() and 0xFF) shl 16) or
            ((h[2].toLong() and 0xFF) shl 8) or (h[3].toLong() and 0xFF)
        return String.format("%06d", v % 1_000_000L)
    }

    fun phoneProof(k: ByteArray, pairId: String) = hmac(k, TAG_PHONE_OK, lp(pairId))
    fun p4Proof(k: ByteArray, pairId: String) = hmac(k, TAG_P4_OK, lp(pairId))
    fun knownProof(kOld: ByteArray, offer: String) = hmac(kOld, TAG_KNOWN, lp(offer))
    fun sessionMac(k: ByteArray, nonce: String, p4Id: String) = hmac(k, TAG_SESS, lp(nonce), lp(p4Id))
    fun sessionOk(k: ByteArray, nonce: String, token: String) = hmac(k, TAG_SESS_OK, lp(nonce), lp(token))

    fun equalsConstantTime(a: ByteArray, b: ByteArray): Boolean {
        if (a.size != b.size) return false
        var d = 0
        for (i in a.indices) d = d or (a[i].toInt() xor b[i].toInt())
        return d == 0
    }

    // ------------------------------------------------------------ hexadecimal
    fun hex(b: ByteArray): String = ObjectStore.hex(b)

    /** Hexadecimal en minusculas de exactamente [bytes] bytes, o null. */
    fun unhex(s: String?, bytes: Int): ByteArray? {
        if (s == null || s.length != bytes * 2) return null
        val out = ByteArray(bytes)
        for (i in 0 until bytes) {
            val hi = Character.digit(s[2 * i], 16)
            val lo = Character.digit(s[2 * i + 1], 16)
            if (hi < 0 || lo < 0) return null
            out[i] = ((hi shl 4) or lo).toByte()
        }
        return out
    }

    // ------------------------------------------------------------ ECDH P-256
    private val P = BigInteger("ffffffff00000001000000000000000000000000ffffffffffffffffffffffff", 16)
    private val B = BigInteger("5ac635d8aa3a93e7b3ebbd55769886bc651d06b0cc53b0f63bce3c3e27d2604b", 16)
    private val A = P.subtract(BigInteger.valueOf(3))

    /** Par de claves efimero del emparejamiento. */
    class Ecdh private constructor(private val kp: KeyPair) {
        val params: ECParameterSpec get() = (kp.public as ECPublicKey).params

        /** Clave publica sin comprimir: 0x04 ‖ X ‖ Y. */
        fun publicPoint(): ByteArray {
            val w = (kp.public as ECPublicKey).w
            return byteArrayOf(4) + fixed32(w.affineX) + fixed32(w.affineY)
        }

        /** Secreto compartido (X, 32 bytes) con la clave publica del otro lado. */
        fun shared(peer: ByteArray): ByteArray {
            val pub = decodePoint(peer, params)
            val ka = KeyAgreement.getInstance("ECDH")
            ka.init(kp.private)
            ka.doPhase(pub, true)
            val z = ka.generateSecret()
            return if (z.size == 32) z else fixed32(BigInteger(1, z))
        }

        companion object {
            fun generate(): Ecdh {
                val g = KeyPairGenerator.getInstance("EC")
                g.initialize(ECGenParameterSpec("secp256r1"), rng)
                return Ecdh(g.generateKeyPair())
            }

            /** Para los vectores del RFC 5903: clave privada fija. */
            fun fromPrivate(d: ByteArray): Ecdh {
                val params = generate().params
                val kf = KeyFactory.getInstance("EC")
                val priv = kf.generatePrivate(ECPrivateKeySpec(BigInteger(1, d), params)) as ECPrivateKey
                // Q = d·G, a partir de la propia clave privada con la API del JDK.
                val q = scalarMultG(BigInteger(1, d), params)
                val pub = kf.generatePublic(ECPublicKeySpec(q, params))
                return Ecdh(KeyPair(pub, priv))
            }
        }
    }

    fun fixed32(v: BigInteger): ByteArray {
        val b = v.toByteArray()
        return when {
            b.size == 32 -> b
            b.size == 33 && b[0] == 0.toByte() -> b.copyOfRange(1, 33)
            b.size < 32 -> ByteArray(32 - b.size) + b
            else -> throw IllegalArgumentException("entero demasiado grande")
        }
    }

    /** Punto sin comprimir -> clave publica, COMPROBANDO que esta en la curva (ataques de curva invalida). */
    fun decodePoint(p: ByteArray, params: ECParameterSpec): ECPublicKey {
        require(p.size == POINT_SIZE && p[0] == 4.toByte()) { "clave publica no valida" }
        val x = BigInteger(1, p.copyOfRange(1, 33))
        val y = BigInteger(1, p.copyOfRange(33, 65))
        require(x < P && y < P) { "clave publica fuera del cuerpo" }
        val lhs = y.multiply(y).mod(P)
        val rhs = x.multiply(x).multiply(x).add(A.multiply(x)).add(B).mod(P)
        require(lhs == rhs) { "la clave publica no esta en la curva P-256" }
        return KeyFactory.getInstance("EC").generatePublic(ECPublicKeySpec(ECPoint(x, y), params)) as ECPublicKey
    }

    // Multiplicacion escalar afin (solo para construir los vectores de prueba:
    // el emparejamiento real usa KeyPairGenerator/KeyAgreement del sistema).
    private fun scalarMultG(k: BigInteger, params: ECParameterSpec): ECPoint {
        var r: ECPoint = ECPoint.POINT_INFINITY
        var add = params.generator
        var n = k
        while (n.signum() > 0) {
            if (n.testBit(0)) r = pointAdd(r, add)
            add = pointAdd(add, add)
            n = n.shiftRight(1)
        }
        return r
    }

    private fun pointAdd(p1: ECPoint, p2: ECPoint): ECPoint {
        if (p1 == ECPoint.POINT_INFINITY) return p2
        if (p2 == ECPoint.POINT_INFINITY) return p1
        val x1 = p1.affineX; val y1 = p1.affineY; val x2 = p2.affineX; val y2 = p2.affineY
        val lambda = if (x1 == x2) {
            if (y1.add(y2).mod(P).signum() == 0) return ECPoint.POINT_INFINITY
            x1.multiply(x1).multiply(BigInteger.valueOf(3)).add(A).multiply(y1.shiftLeft(1).modInverse(P)).mod(P)
        } else {
            y2.subtract(y1).multiply(x2.subtract(x1).modInverse(P)).mod(P)
        }
        val x3 = lambda.multiply(lambda).subtract(x1).subtract(x2).mod(P)
        val y3 = lambda.multiply(x1.subtract(x3)).subtract(y1).mod(P)
        return ECPoint(x3, y3)
    }
}
