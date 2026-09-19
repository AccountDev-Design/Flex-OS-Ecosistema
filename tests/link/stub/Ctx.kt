// #############################################################
//  DOBLES DEL SDK DE ANDROID  --  solo para esta bateria
//  ------------------------------------------------------------
//  WifiLinkServer.kt toca TRES cosas de Android: Context (para
//  pedir el WifiManager), WifiManager (el cerrojo de
//  multidifusion) y Log. Nada mas: el resto del fichero es JVM
//  puro -- sockets, hilos y bytes.
//
//  Con estos tres dobles, el servidor REAL se compila y se ejecuta
//  en un PC, contra sockets TCP de verdad. Eso permite reproducir
//  el bucle de conecta/desconecta sin telefono y sin placa, que es
//  justo lo que no se podia comprobar de ninguna otra forma.
//
//  NO son un doble del servidor: el codigo bajo prueba es el que
//  va en el APK, sin tocar una linea.
// #############################################################
package android.content

open class Context {
    open val applicationContext: Context get() = this
    open fun getSystemService(name: String): Any? = null
    companion object { const val WIFI_SERVICE = "wifi" }
}
