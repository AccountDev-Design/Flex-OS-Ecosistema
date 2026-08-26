package com.flexos.flexphone.link

import android.content.Context
import android.media.AudioAttributes
import android.media.AudioManager
import android.media.RingtoneManager
import android.os.Handler
import android.os.Looper
import android.os.VibrationEffect
import android.os.Vibrator

/**
 * "Encontrar mi telefono".
 *
 * LIMITES QUE SE RESPETAN A PROPOSITO
 * -----------------------------------
 *   · NO se sube el volumen del usuario ni se toca el perfil de
 *     sonido: se reproduce por el canal de ALARMA, que es el que
 *     Android reserva para esto, y con el volumen que el usuario
 *     tenga puesto;
 *   · NO se elude el modo No molestar. Si el usuario lo activo, esa
 *     decision vale mas que esta funcion; en ese caso queda la
 *     vibracion, que si se permite;
 *   · hay un TOPE de duracion: se para sola a los 30 segundos aunque
 *     nadie la pare, para que un fallo de enlace no deje el telefono
 *     sonando en un bolsillo indefinidamente.
 */
object FindMyPhone {

    private const val MAX_MS = 30_000L

    private var ringtone: android.media.Ringtone? = null
    private var vibrator: Vibrator? = null
    private val handler = Handler(Looper.getMainLooper())
    private val autoStop = Runnable { stop() }

    @Volatile var ringing: Boolean = false
        private set

    fun start(ctx: Context) {
        stop()
        runCatching {
            val uri = RingtoneManager.getActualDefaultRingtoneUri(ctx, RingtoneManager.TYPE_ALARM)
                ?: RingtoneManager.getDefaultUri(RingtoneManager.TYPE_ALARM)
            ringtone = RingtoneManager.getRingtone(ctx, uri)?.apply {
                audioAttributes = AudioAttributes.Builder()
                    .setUsage(AudioAttributes.USAGE_ALARM)
                    .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
                    .build()
                play()
            }
        }
        runCatching {
            vibrator = ctx.getSystemService(Vibrator::class.java)
            val pattern = longArrayOf(0, 400, 300)
            vibrator?.vibrate(VibrationEffect.createWaveform(pattern, 0))
        }
        ringing = true
        handler.removeCallbacks(autoStop)
        handler.postDelayed(autoStop, MAX_MS)
    }

    fun stop() {
        handler.removeCallbacks(autoStop)
        runCatching { ringtone?.stop() }
        runCatching { vibrator?.cancel() }
        ringtone = null; vibrator = null
        ringing = false
    }
}
