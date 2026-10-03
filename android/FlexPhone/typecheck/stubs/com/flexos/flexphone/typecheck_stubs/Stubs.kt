// Dobles minimos para comprobar tipos sin el SDK: en el APK, R lo genera AGP
// y MainActivity es la de verdad (Compose).
package com.flexos.flexphone

object R {
    object drawable { const val ic_flexcloud: Int = 0 }
}

class MainActivity : android.app.Activity()
