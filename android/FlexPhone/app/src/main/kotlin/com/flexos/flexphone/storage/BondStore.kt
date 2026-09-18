package com.flexos.flexphone.storage

import android.content.Context
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import android.util.Base64
import com.flexos.flexphone.protocol.FlexAuth
import java.security.KeyStore
import java.security.SecureRandom
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import javax.crypto.spec.GCMParameterSpec

/**
 * ALMACEN DEL VINCULO.
 *
 * Guarda la clave del emparejamiento y el identificador estable de
 * este telefono. Va aparte de [SettingsStore] a proposito: los
 * ajustes son preferencias y esto es MATERIAL DE CLAVE, y las dos
 * cosas no se borran ni se respaldan igual.
 *
 * POR QUE NO EN CLARO
 * -------------------
 * El almacenamiento privado de la app ya deja fuera a las demas
 * aplicaciones, pero la clave del vinculo es lo unico que impide que
 * un equipo cualquiera de la red abra sesion y se lleve las
 * notificaciones. Aqui se envuelve con una clave del **Android
 * Keystore**, que no sale del almacen de claves del sistema: lo que
 * acaba en disco es un cifrado, no la clave.
 *
 * Se usa AES/GCM del propio sistema. Sin dependencias nuevas: todo
 * esto esta en la plataforma desde API 23 y el minSdk de la app es
 * 26, asi que no hay ningun camino alternativo que mantener.
 *
 * QUE NO SE GUARDA AQUI: el codigo de seis digitos. Es de un solo
 * uso, caduca en dos minutos y no hace falta despues -- guardarlo
 * solo seria darle a alguien una segunda forma de derivar la clave.
 */
class BondStore(ctx: Context) {

    private val prefs = ctx.getSharedPreferences(FILE, Context.MODE_PRIVATE)

    companion object {
        private const val FILE = "flexphone_bond"
        private const val KEY_ALIAS = "flexphone.bond.v2"
        private const val P_BLOB = "bond_blob"
        private const val P_IV = "bond_iv"
        private const val P_PEER = "bond_peer"        // id del Flex OS emparejado
        private const val P_NAME = "bond_name"
        private const val P_SELF = "self_id"          // id estable de ESTE telefono
        private const val P_AT = "bond_at"
        private const val GCM_TAG_BITS = 128
    }

    // ---------------------------------------------------------
    //  Clave envolvente (Android Keystore)
    // ---------------------------------------------------------
    private fun wrapKey(): SecretKey {
        val ks = KeyStore.getInstance("AndroidKeyStore").apply { load(null) }
        (ks.getEntry(KEY_ALIAS, null) as? KeyStore.SecretKeyEntry)?.let { return it.secretKey }
        val kg = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, "AndroidKeyStore")
        kg.init(
            KeyGenParameterSpec.Builder(
                KEY_ALIAS,
                KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT,
            )
                .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                // Sin exigir autenticacion del usuario: el enlace tiene
                // que poder reanudarse con la pantalla apagada, que es
                // justo para lo que existe.
                .setUserAuthenticationRequired(false)
                .build(),
        )
        return kg.generateKey()
    }

    // ---------------------------------------------------------
    //  Identidad de este telefono
    // ---------------------------------------------------------
    /**
     * Identificador ESTABLE de este telefono. Entra en la derivacion
     * de la clave, asi que tiene que sobrevivir a los reinicios.
     *
     * Se genera al azar la primera vez. NO se usa ANDROID_ID ni nada
     * parecido: eso identificaria el telefono ante cualquiera que
     * escuche el descubrimiento, y aqui solo hace falta un valor que
     * no se repita.
     */
    fun selfId(): String {
        prefs.getString(P_SELF, null)?.let { if (it.isNotEmpty()) return it }
        val b = ByteArray(8).also { SecureRandom().nextBytes(it) }
        val id = "phone-" + b.joinToString("") { "%02x".format(it) }
        prefs.edit().putString(P_SELF, id).apply()
        return id
    }

    // ---------------------------------------------------------
    //  El vinculo
    // ---------------------------------------------------------
    fun isPaired(): Boolean = prefs.contains(P_BLOB)

    /** Id del Flex OS emparejado, o null. */
    fun peerId(): String? = prefs.getString(P_PEER, null)
    fun peerName(): String? = prefs.getString(P_NAME, null)
    /** Cuando se emparejo (epoch ms), o 0. */
    fun pairedAt(): Long = prefs.getLong(P_AT, 0L)

    fun save(key: ByteArray, peerId: String, peerName: String?) {
        require(key.size == FlexAuth.KEY_SIZE) { "clave de tamano incorrecto" }
        val c = Cipher.getInstance("AES/GCM/NoPadding")
        c.init(Cipher.ENCRYPT_MODE, wrapKey())
        val blob = c.doFinal(key)
        prefs.edit()
            .putString(P_BLOB, Base64.encodeToString(blob, Base64.NO_WRAP))
            .putString(P_IV, Base64.encodeToString(c.iv, Base64.NO_WRAP))
            .putString(P_PEER, peerId)
            .putString(P_NAME, peerName ?: "")
            .putLong(P_AT, System.currentTimeMillis())
            .apply()
    }

    /**
     * Devuelve la clave, o null si no hay vinculo o si el almacen ya
     * no puede descifrarla.
     *
     * Ese segundo caso pasa de verdad: restaurar el telefono o
     * reinstalar la app deja el fichero pero no la clave del
     * Keystore. Se limpia y se devuelve null, para que la app diga
     * "vuelve a emparejar" en vez de intentar autenticar con basura
     * y dejar a Flex OS con un error que no explica nada.
     */
    fun key(): ByteArray? {
        val blob = prefs.getString(P_BLOB, null) ?: return null
        val iv = prefs.getString(P_IV, null) ?: return null
        return try {
            val c = Cipher.getInstance("AES/GCM/NoPadding")
            c.init(
                Cipher.DECRYPT_MODE, wrapKey(),
                GCMParameterSpec(GCM_TAG_BITS, Base64.decode(iv, Base64.NO_WRAP)),
            )
            c.doFinal(Base64.decode(blob, Base64.NO_WRAP))
        } catch (e: Exception) {
            clear()
            null
        }
    }

    /**
     * Revoca el vinculo. Borra el cifrado Y la clave envolvente del
     * Keystore: dejar la envolvente viva permitiria descifrar una
     * copia de seguridad antigua del fichero.
     */
    fun clear() {
        prefs.edit()
            .remove(P_BLOB).remove(P_IV).remove(P_PEER).remove(P_NAME).remove(P_AT)
            .apply()
        runCatching {
            KeyStore.getInstance("AndroidKeyStore").apply { load(null) }.deleteEntry(KEY_ALIAS)
        }
    }
}
