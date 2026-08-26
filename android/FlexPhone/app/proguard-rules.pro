# Flex Phone -- reglas de R8 para la version release.
#
# El modulo :protocol se accede por reflexion en ningun sitio, asi
# que no hace falta conservarlo entero. Lo que SI hay que conservar
# son las clases que Android instancia por nombre desde el
# manifiesto: si R8 las renombra, el sistema no las encuentra y el
# servicio simplemente no arranca.
-keep class com.flexos.flexphone.FlexPhoneApp
-keep class com.flexos.flexphone.MainActivity
-keep class com.flexos.flexphone.notifications.FlexNotificationListener
-keep class com.flexos.flexphone.link.FlexLinkService
-keep class com.flexos.flexphone.link.BootReceiver
-keep class com.flexos.flexphone.relay.BrowserRelayService

# removeBond se llama por reflexion (no es API publica).
-keepclassmembers class android.bluetooth.BluetoothDevice {
    public boolean removeBond();
}
