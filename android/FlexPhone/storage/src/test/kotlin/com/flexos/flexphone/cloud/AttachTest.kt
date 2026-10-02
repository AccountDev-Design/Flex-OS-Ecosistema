package com.flexos.flexphone.cloud

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import kotlin.test.assertTrue

/**
 * Emparejar el telefono con un Flex OS (lado telefono) contra un P4 SIMULADO que
 * sigue el mismo contrato que `FlexOS_StorageCore.cpp` (y que la prueba de
 * extremo a extremo con el servidor del P4 de verdad vuelve a comprobar).
 */
class AttachTest {
    /** El P4 simulado: ofertas de un solo uso, ECDH, aprobacion en "pantalla". */
    class FakeP4(val p4Id: String = "flexos-fake01", val name: String = "Flex OS Ultra") : FormPoster {
        val offers = HashSet<String>()
        class Pairing(val id: String, val key: ByteArray, val phoneId: String, var state: String, val autoApproved: Boolean)
        val pairings = HashMap<String, Pairing>()
        var storedPhone: Pair<String, ByteArray>? = null      // (phoneId, clave) ya emparejado
        var sasShown: String? = null
        var approveOnPoll = 0                                  // aprueba tras N sondeos (0 = no)
        var deny = false
        var polls = 0
        var tamperProof = false

        fun newOffer(): String = StorageCrypto.hex(StorageCrypto.random(16)).also { offers.add(it) }

        override fun post(url: String, form: Map<String, String>, timeoutMs: Int): Pair<Int, String> {
            val path = url.substringAfter("8080")
            if (path == "/api/fs/phone/pair") {
                val offer = form["offer"] ?: return 400 to "{\"error\":\"falta la oferta\"}"
                if (!offers.remove(offer)) return 403 to "{\"error\":\"La oferta no vale o ya se uso\"}"
                val peer = StorageCrypto.unhex(form["pub"], 65) ?: return 400 to "{\"error\":\"clave no valida\"}"
                val ecdh = StorageCrypto.Ecdh.generate()
                val z = ecdh.shared(peer)
                val pid = form["pid"]!!
                val key = StorageCrypto.deriveKey(z, offer, p4Id, pid)
                val known = storedPhone?.let { (sp, old) ->
                    sp == pid && form["kp4"] == p4Id &&
                        StorageCrypto.unhex(form["known"], 32)?.let { StorageCrypto.equalsConstantTime(it, StorageCrypto.knownProof(old, offer)) } == true
                } == true
                val id = StorageCrypto.hex(StorageCrypto.random(16))
                pairings[id] = Pairing(id, key, pid, if (known) "approved" else "pending", known)
                if (!known) sasShown = StorageCrypto.sas(key)
                return 202 to Json.write(mapOf("pairId" to id, "pub" to StorageCrypto.hex(ecdh.publicPoint()), "p4id" to p4Id,
                    "p4name" to name, "approve" to if (known) 0 else 1, "expiresIn" to 90))
            }
            val id = path.removePrefix("/api/fs/phone/pair/")
            val p = pairings[id] ?: return 404 to "{\"error\":\"no existe\"}"
            val proof = StorageCrypto.unhex(form["proof"], 32)
            if (proof == null || !StorageCrypto.equalsConstantTime(proof, StorageCrypto.phoneProof(p.key, id))) return 403 to "{\"error\":\"prueba incorrecta\",\"state\":\"bad_proof\"}"
            polls++
            if (deny) return 403 to "{\"error\":\"Rechazado en Flex OS\",\"state\":\"denied\"}"
            if (p.state == "pending" && approveOnPoll in 1..polls) p.state = "approved"
            if (p.state != "approved") return 200 to "{\"state\":\"pending\"}"
            storedPhone = p.phoneId to p.key
            val pr = StorageCrypto.p4Proof(p.key, id).also { if (tamperProof) it[0] = (it[0] + 1).toByte() }
            return 200 to Json.write(mapOf("state" to "approved", "proof" to StorageCrypto.hex(pr)))
        }
    }

    private val phone = PhoneInfo("a55-0f1e2d3c4b5a6978", "Galaxy A55 5G", "SM-A556B")
    private var t = 0L
    private fun client(p4: FakeP4, repo: PairingRepo) = AttachClient(phone, 47830, repo, p4, { t }, { t += it })

    @Test fun `emparejar - codigo igual en los dos lados, aprobado en el P4 y clave guardada`() {
        val p4 = FakeP4().apply { approveOnPoll = 3 }
        val repo = MemoryPairingRepo()
        var sas: String? = null
        val r = client(p4, repo).attach("192.168.1.50:8080", p4.newOffer(), { s, _ -> sas = s })
        assertTrue(r is AttachClient.Result.Paired, r.toString())
        assertNotNull(sas); assertEquals(p4.sasShown, sas, "los dos lados ensenan el mismo codigo")
        val saved = repo.load()!!
        assertEquals("flexos-fake01", saved.p4Id)
        assertTrue(saved.key.contentEquals(p4.storedPhone!!.second))
        assertEquals("192.168.1.50:8080", saved.host)
        assertEquals(3, p4.polls)
    }

    @Test fun `la oferta es de un solo uso`() {
        val p4 = FakeP4().apply { approveOnPoll = 1 }
        val offer = p4.newOffer()
        assertTrue(client(p4, MemoryPairingRepo()).attach("192.168.1.50:8080", offer, { _, _ -> }) is AttachClient.Result.Paired)
        val again = client(p4, MemoryPairingRepo()).attach("192.168.1.50:8080", offer, { _, _ -> })
        assertTrue(again is AttachClient.Result.Failed && again.message.contains("oferta"), again.toString())
    }

    @Test fun `rechazado en el P4 o sin aprobar a tiempo - no se guarda nada`() {
        val repo = MemoryPairingRepo()
        val denied = FakeP4().apply { deny = true }
        assertTrue(client(denied, repo).attach("192.168.1.50:8080", denied.newOffer(), { _, _ -> }) is AttachClient.Result.Failed)
        assertNull(repo.load())
        val slow = FakeP4()                                   // nadie pulsa "Permitir"
        val r = client(slow, repo).attach("192.168.1.50:8080", slow.newOffer(), { _, _ -> })
        assertTrue(r is AttachClient.Result.Failed && r.message.contains("tiempo"), r.toString())
        assertNull(repo.load())
    }

    @Test fun `un P4 que no demuestra tener la clave no se guarda`() {
        val p4 = FakeP4().apply { approveOnPoll = 1; tamperProof = true }
        val repo = MemoryPairingRepo()
        val r = client(p4, repo).attach("192.168.1.50:8080", p4.newOffer(), { _, _ -> })
        assertTrue(r is AttachClient.Result.Failed, r.toString())
        assertNull(repo.load())
    }

    @Test fun `un telefono ya emparejado vuelve sin aprobar otra vez y rota la clave`() {
        val p4 = FakeP4().apply { approveOnPoll = 1 }
        val repo = MemoryPairingRepo()
        assertTrue(client(p4, repo).attach("192.168.1.50:8080", p4.newOffer(), { _, _ -> }) is AttachClient.Result.Paired)
        val k1 = repo.load()!!.key
        p4.approveOnPoll = 0; p4.sasShown = null
        var asked = false
        val r = client(p4, repo).attach("192.168.1.77:8080", p4.newOffer(), { _, _ -> asked = true })
        assertTrue(r is AttachClient.Result.Paired, r.toString())
        assertTrue(!asked && p4.sasShown == null, "no se vuelve a pedir el codigo")
        assertTrue(!repo.load()!!.key.contentEquals(k1), "la clave nueva sustituye a la vieja")
        assertEquals("192.168.1.77:8080", repo.load()!!.host)
    }

    @Test fun `direcciones y ofertas raras se rechazan sin tocar la red`() {
        val p4 = FakeP4()
        val c = client(p4, MemoryPairingRepo())
        assertTrue(c.attach("8.8.8.8:8080", p4.newOffer(), { _, _ -> }) is AttachClient.Result.Failed)
        assertTrue(c.attach("192.168.1.50:8080", "corta", { _, _ -> }) is AttachClient.Result.Failed)
        assertTrue(c.attach("192.168.1.50:8080", "Z".repeat(32), { _, _ -> }) is AttachClient.Result.Failed)
        assertEquals(0, p4.polls)
    }
}
