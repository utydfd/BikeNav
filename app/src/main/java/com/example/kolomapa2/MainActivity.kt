package com.example.kolomapa2

import android.content.Intent
import android.net.Uri
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.viewModels
import androidx.compose.runtime.mutableStateOf
import androidx.core.splashscreen.SplashScreen.Companion.installSplashScreen
import com.example.kolomapa2.ui.MainScreen
import com.example.kolomapa2.ui.theme.KoloMapa2Theme
import com.example.kolomapa2.utils.BleService

class MainActivity : ComponentActivity() {
    // Hold the incoming GPX URI to be processed by MainScreen
    val incomingGpxUri = mutableStateOf<Uri?>(null)

    // Get the ViewModel
    private val viewModel: MainViewModel by viewModels()

    override fun onCreate(savedInstanceState: Bundle?) {
        // Install splash screen before calling super.onCreate()
        installSplashScreen()

        super.onCreate(savedInstanceState)
        enableEdgeToEdge()

        // Handle incoming intents (GPX file opens)
        handleIntent(intent)

        setContent {
            KoloMapa2Theme {
                MainScreen(incomingGpxUri = incomingGpxUri.value)
            }
        }
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        handleIntent(intent)
    }

    override fun onStart() {
        super.onStart()
        // App is now visible - enable periodic status updates
        viewModel.enablePeriodicStatusUpdates()
    }

    override fun onResume() {
        super.onResume()
        // Stop locate phone alarm if active when app comes to foreground
        if (viewModel.locatePhoneManager.isLocating()) {
            viewModel.locatePhoneManager.stopLocatePhone()
        }
    }

    override fun onStop() {
        super.onStop()
        // App is going to background - disable periodic status updates
        viewModel.disablePeriodicStatusUpdates()
    }

    private fun handleIntent(intent: Intent?) {
        when (intent?.action) {
            Intent.ACTION_VIEW,
            Intent.ACTION_EDIT,
            Intent.ACTION_PICK,
            Intent.ACTION_SEND -> {
                val uri = intent.data ?: intent.getParcelableExtra(Intent.EXTRA_STREAM)
                uri?.let {
                    // Verify it's a GPX file or XML file (we'll accept both)
                    val path = it.path?.lowercase() ?: ""
                    val mimeType = intent.type?.lowercase() ?: ""
                    val lastSegment = it.lastPathSegment?.lowercase() ?: ""

                    // Accept if: path ends with .gpx, OR mime type contains gpx/xml, OR filename ends with .gpx
                    if (path.endsWith(".gpx") ||
                        lastSegment.endsWith(".gpx") ||
                        mimeType.contains("gpx") ||
                        (mimeType.contains("xml") && lastSegment.endsWith(".gpx"))) {
                        incomingGpxUri.value = it
                    }
                }
            }
        }

        if (intent?.getBooleanExtra(BleService.EXTRA_AUTO_CONNECT, false) == true) {
            viewModel.startBleService()
        }
    }
}
