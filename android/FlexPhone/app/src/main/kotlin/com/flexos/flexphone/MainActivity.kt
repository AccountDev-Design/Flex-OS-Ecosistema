package com.flexos.flexphone

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import com.flexos.flexphone.ui.FlexPhoneNav
import com.flexos.flexphone.ui.theme.FlexPhoneTheme

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContent { FlexPhoneTheme { FlexPhoneNav() } }
    }
}
