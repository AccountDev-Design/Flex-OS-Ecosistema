package com.flexos.flexphone.flexcloud

import android.content.Context
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import android.util.Base64
import com.flexos.flexphone.cloud.P4Pairing
import com.flexos.flexphone.cloud.PairingRepo
import java.security.KeyStore
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import javax.crypto.spec.GCMParameterSpec

/**
 * EL FLEX OS CON EL QUE ESTE TELEFONO COMPARTE FLEX CLOUD (Flex Storage).
 *
 * Mismo criterio que [com.flexos.flexphone.storage.BondStore]: la clave del
 * emparejamiento es lo unico que deja a un Flex OS abrir sesion con el servidor
 * de Flex Cloud del telefono, asi que NO se guarda en claro. Se envuelve con una
 * clave AES/GCM del **Android Keystore** (no sale del almacen del sistema): en
 * disco solo queda el cifrado. Va en su propio archivo de preferencias, aparte
 * del enlace de Flex Phone: son dos vinculos distintos y se olvidan por separado.
 *
 * Si el Keystore ya no puede descifrarla (restauracion, reinstalacion), se
 * borra y se devuelve null: la app dice "vuelve a activar Flex Cloud" en vez de
 * presentarse con basura y dejar a Flex OS con un error que no explica nada.
 */
class KeystorePairingRepo(ctx: Context) : PairingRepo {

    private val prefs = ctx.applicationContext.getSharedPreferences(FILE, Context.MODE_PRIVATE)

    companion object {
        private const val FILE = "flexcloud_pairing"
        private const val KEY_ALIAS = "flexphone.flexcloud.v1"
        private const val P_BLOB = "blob"
        private const val P_IV = "iv"
        private const val P_ID = "p4_id"
        private const val P_NAME = "p4_name"
        private const val P_HOST = "p4_host"
        private const val P_AT = "paired_at"
        private const val GCM_TAG_BITS = 128
        private const val KEY_SIZE = 32
    }

    private fun wrapKey(): SecretKey {
        val ks = KeyStore.getInstance("AndroidKeyStore").apply { load(null) }
        (ks.getEntry(KEY_ALIAS, null) as? KeyStore.SecretKeyEntry)?.let { return it.secretKey }
        val kg = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, "AndroidKeyStore")
        kg.init(
            KeyGenParameterSpec.Builder(KEY_ALIAS, KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT)
                .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                // Sin autenticacion del usuario: Flex OS tiene que poder abrir
                // sesion con la pantalla del telefono apagada.
                .setUserAuthenticationRequired(false)
                .build(),
        )
        return kg.generateKey()
    }

    @Synchronized
    override fun load(): P4Pairing? {
        val blob = prefs.getString(P_BLOB, null) ?: return null
        val iv = prefs.getString(P_IV, null) ?: return null
        val id = prefs.getString(P_ID, null) ?: return null
        val key = try {
            val c = Cipher.getInstance("AES/GCM/NoPadding")
            c.init(Cipher.DECRYPT_MODE, wrapKey(), GCMParameterSpec(GCM_TAG_BITS, Base64.decode(iv, Base64.NO_WRAP)))
            c.doFinal(Base64.decode(blob, Base64.NO_WRAP))
        } catch (e: Exception) {
            clear()
            return null
        }
        if (key.size != KEY_SIZE) { clear(); return null }
        return P4Pairing(
            id, prefs.getString(P_NAME, null) ?: "Flex OS",
            key, prefs.getString(P_HOST, null) ?: "", prefs.getLong(P_AT, 0L),
        )
    }

    @Synchronized
    override fun save(p: P4Pairing) {
        require(p.key.size == KEY_SIZE) { "clave de tamano incorrecto" }
        val c = Cipher.getInstance("AES/GCM/NoPadding")
        c.init(Cipher.ENCRYPT_MODE, wrapKey())
        val blob = c.doFinal(p.key)
        // commit(): el emparejamiento tiene que estar en disco ANTES de decirle
        // al P4 que todo fue bien (AttachClient guarda y despues confirma).
        val ok = prefs.edit()
            .putString(P_BLOB, Base64.encodeToString(blob, Base64.NO_WRAP))
            .putString(P_IV, Base64.encodeToString(c.iv, Base64.NO_WRAP))
            .putString(P_ID, p.p4Id)
            .putString(P_NAME, p.p4Name)
            .putString(P_HOST, p.host)
            .putLong(P_AT, p.pairedAt)
            .commit()
        if (!ok) throw IllegalStateException("no se pudo guardar el emparejamiento")
    }

    @Synchronized
    override fun clear() {
        prefs.edit().clear().commit()
        try {
            KeyStore.getInstance("AndroidKeyStore").apply { load(null) }.deleteEntry(KEY_ALIAS)
        } catch (e: Exception) { /* ya no estaba */ }
    }

    /** Lo que se ensena en pantalla sin descifrar nada. */
    fun p4Name(): String? = prefs.getString(P_NAME, null)
    fun p4Host(): String? = prefs.getString(P_HOST, null)
    fun isPaired(): Boolean = prefs.contains(P_BLOB)
}
