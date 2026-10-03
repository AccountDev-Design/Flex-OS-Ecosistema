package com.flexos.flexphone.flexcloud

import android.app.Activity
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.net.Uri
import android.os.Bundle
import android.util.TypedValue
import android.view.Gravity
import android.view.View
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.ScrollView
import android.widget.TextView
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.flow.collect
import kotlinx.coroutines.launch

/**
 * ACTIVAR FLEX CLOUD EN ESTE TELEFONO (enlace `flexstorage://attach`).
 *
 * La web de Flex OS (abierta EN este telefono con el QR) pide al P4 una oferta
 * de un solo uso y abre este enlace con la direccion del P4 y la oferta. Aqui:
 *
 *  1. Se pregunta a la persona, con palabras claras, que va a pasar: el Flex
 *     OS de esa direccion podra guardar archivos en un espacio de este
 *     telefono (la cuota que elija, 5 GB de serie) y nada mas.
 *  2. Si acepta, se arranca el servidor de Flex Cloud (escucha solo en la
 *     Wi-Fi) y se empareja con [AttachClient]: aparece el CODIGO DE 6 CIFRAS,
 *     el mismo que sale en la pantalla de Flex OS, donde hay que aceptarlo.
 *  3. Solo cuando Flex OS lo aprueba Y demuestra tener la misma clave se
 *     guarda el emparejamiento (Keystore). Si algo falla, no queda nada.
 *
 * Interfaz con vistas del sistema (sin dependencias): es un dialogo corto.
 */
class StorageAttachActivity : Activity() {

    private enum class Step { CONFIRM, WORKING, CODE, DONE, FAILED }

    // El emparejamiento NO vive aqui: vive en [StorageAttach], que es de la app. Esta
    // pantalla solo lo ENSENA, y por eso puede destruirse y recrearse (giro, tamano de
    // letra, Atras, irse al navegador...) sin cancelar nada. Ver StorageAttach.
    private val ui = CoroutineScope(SupervisorJob() + Dispatchers.Main)

    private lateinit var title: TextView
    private lateinit var body: TextView
    private lateinit var code: TextView
    private lateinit var progress: ProgressBar
    private lateinit var primary: Button
    private lateinit var secondary: Button

    private val bg = Color.rgb(11, 16, 32)
    private val card = Color.rgb(24, 31, 54)
    private val txt = Color.rgb(238, 242, 251)
    private val txt2 = Color.rgb(168, 179, 204)
    private val accent = Color.rgb(79, 125, 255)
    private val bad = Color.rgb(255, 107, 107)
    private val ok = Color.rgb(57, 201, 138)

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        buildUi()
        // El MISMO enlace tras recrear la actividad no es una oferta nueva: StorageAttach lo
        // reconoce y no toca lo que esta en marcha.
        // Sin enlace (se abre desde la pantalla Flex Cloud para VER un emparejamiento en curso)
        // solo se observa.
        if (intent?.data != null) StorageAttach.offer(this, intent?.data)
        ui.launch { StorageAttach.state.collect { render(it) } }
    }

    // launchMode=singleTask: un enlace NUEVO con la actividad ya abierta llega aqui.
    override fun onNewIntent(intent: android.content.Intent?) {
        super.onNewIntent(intent)
        setIntent(intent)
        StorageAttach.offer(this, intent?.data)
    }

    private fun render(st: StorageAttach.UiState) {
        when (st) {
            is StorageAttach.UiState.Idle -> if (!isFinishing) finish()
            is StorageAttach.UiState.Confirm -> show(Step.CONFIRM, "¿Usar este teléfono como Flex Cloud?",
                "El Flex OS de ${st.p4Ip} podrá guardar archivos en un espacio de este teléfono " +
                    "(hasta ${st.quotaGb} GB, lo puedes cambiar en Flex Phone) y leerlos cuando se lo pidas.\n\n" +
                    "No verá nada más del teléfono: ni tus fotos, ni tus archivos, ni tus apps. Solo funciona en tu Wi‑Fi, " +
                    "y lo que se envía viaja por ella sin cifrar: úsalo en una red de confianza.")
            is StorageAttach.UiState.Working -> show(Step.WORKING, "Conectando con Flex OS…", "Preparando Flex Cloud en este teléfono.")
            is StorageAttach.UiState.Code -> show(Step.CODE, "Comprueba el código",
                "En la pantalla de ${st.p4Name} aparece este mismo código. Si coincide, pulsa «Emparejar» allí.", st.sas)
            is StorageAttach.UiState.Done -> show(Step.DONE, "Flex Cloud activado",
                "${st.p4Name} ya puede guardar archivos aquí. Lo verás en la web de Flex OS y en Flex Phone › Flex Cloud.")
            is StorageAttach.UiState.Failed -> show(Step.FAILED, st.title, st.message)
        }
    }

    private fun dp(v: Int): Int = TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, v.toFloat(), resources.displayMetrics).toInt()

    private fun round(color: Int, radiusDp: Int): GradientDrawable =
        GradientDrawable().apply { setColor(color); cornerRadius = dp(radiusDp).toFloat() }

    private fun buildUi() {
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setBackgroundColor(bg)
            setPadding(dp(20), dp(48), dp(20), dp(24))
        }
        val box = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            background = round(card, 24)
            setPadding(dp(22), dp(24), dp(22), dp(22))
        }
        title = TextView(this).apply {
            setTextColor(txt); setTextSize(TypedValue.COMPLEX_UNIT_SP, 22f); typeface = Typeface.DEFAULT_BOLD
        }
        body = TextView(this).apply {
            setTextColor(txt2); setTextSize(TypedValue.COMPLEX_UNIT_SP, 15f)
            setLineSpacing(0f, 1.2f); setPadding(0, dp(12), 0, 0)
        }
        code = TextView(this).apply {
            setTextColor(txt); setTextSize(TypedValue.COMPLEX_UNIT_SP, 44f)
            typeface = Typeface.create(Typeface.MONOSPACE, Typeface.BOLD)
            gravity = Gravity.CENTER; letterSpacing = 0.08f
            background = round(bg, 18)
            setPadding(0, dp(14), 0, dp(14))
            visibility = View.GONE
        }
        progress = ProgressBar(this).apply { isIndeterminate = true; visibility = View.GONE }
        primary = Button(this).apply {
            isAllCaps = false; setTextColor(Color.WHITE); setTextSize(TypedValue.COMPLEX_UNIT_SP, 16f)
            background = round(accent, 16)
        }
        secondary = Button(this).apply {
            isAllCaps = false; setTextColor(txt); setTextSize(TypedValue.COMPLEX_UNIT_SP, 16f)
            background = round(Color.rgb(40, 48, 74), 16)
        }
        val lp = { top: Int -> LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT).apply { topMargin = dp(top) } }
        box.addView(title)
        box.addView(body)
        box.addView(code, lp(18))
        box.addView(progress, lp(18))
        box.addView(primary, lp(22).apply { height = dp(52) })
        box.addView(secondary, lp(10).apply { height = dp(52) })
        root.addView(box)
        setContentView(ScrollView(this).apply { setBackgroundColor(bg); addView(root) })
    }

    private fun show(step: Step, t: String, b: String, sas: String? = null) {
        title.text = t
        body.text = b
        code.visibility = if (sas != null) View.VISIBLE else View.GONE
        if (sas != null) code.text = if (sas.length == 6) sas.substring(0, 3) + " " + sas.substring(3) else sas
        progress.visibility = if (step == Step.WORKING || step == Step.CODE) View.VISIBLE else View.GONE
        title.setTextColor(when (step) { Step.FAILED -> bad; Step.DONE -> ok; else -> txt })
        when (step) {
            Step.CONFIRM -> {
                primary.visibility = View.VISIBLE; primary.text = "Activar Flex Cloud"
                primary.setOnClickListener { StorageAttach.begin(this) }
                secondary.visibility = View.VISIBLE; secondary.text = "Cancelar"
                secondary.setOnClickListener { StorageAttach.dismiss(); finish() }
            }
            Step.WORKING, Step.CODE -> {
                primary.visibility = View.GONE
                // Cancelar es lo UNICO que corta un emparejamiento en curso. Atras, el giro de
                // pantalla o irse al navegador no lo cortan: se puede volver y sigue ahi.
                secondary.visibility = View.VISIBLE; secondary.text = "Cancelar"
                secondary.setOnClickListener { StorageAttach.cancel(this); finish() }
            }
            Step.DONE, Step.FAILED -> {
                primary.visibility = View.VISIBLE; primary.text = "Cerrar"
                primary.setOnClickListener { StorageAttach.dismiss(); finish() }
                secondary.visibility = View.GONE
            }
        }
    }

    override fun onDestroy() {
        // Solo se deja de OBSERVAR. El emparejamiento sigue (ver StorageAttach).
        ui.cancel()
        // Una pregunta sin responder (Confirm) no se queda esperando: si la persona se va, se olvida.
        if (isFinishing && StorageAttach.state.value is StorageAttach.UiState.Confirm) StorageAttach.dismiss()
        super.onDestroy()
    }
}
