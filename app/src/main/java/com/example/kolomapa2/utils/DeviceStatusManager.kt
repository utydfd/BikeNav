package com.example.kolomapa2.utils

import android.Manifest
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.PackageManager
import android.media.AudioManager
import android.media.MediaMetadata
import android.media.session.MediaController
import android.media.session.MediaSessionManager
import android.media.session.PlaybackState
import android.net.ConnectivityManager
import android.net.NetworkCapabilities
import android.net.Network
import android.net.wifi.WifiManager
import android.os.BatteryManager
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.telephony.SignalStrength
import android.telephony.TelephonyCallback
import android.telephony.TelephonyDisplayInfo
import android.telephony.TelephonyManager
import android.util.Log
import androidx.core.app.ActivityCompat
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow

/**
 * Manages collection of device status information for BLE sync to ESP32.
 * Collects battery, network, and music information.
 */
class DeviceStatusManager(private val context: Context) {

    data class DeviceStatus(
        val musicPlaying: Boolean = false,
        val songTitle: String = "",
        val songArtist: String = "",
        val phoneBatteryPercent: Int = 0,
        val phoneCharging: Boolean = false,
        val wifiConnected: Boolean = false,
        val wifiSsid: String = "",
        val wifiSignalStrength: Int = 0,
        val cellularSignalStrength: Int = 0,
        val cellularType: String = ""
    )

    private val _deviceStatus = MutableStateFlow(DeviceStatus())
    val deviceStatus: StateFlow<DeviceStatus> = _deviceStatus

    private val batteryManager = context.getSystemService(Context.BATTERY_SERVICE) as BatteryManager
    private val wifiManager = context.applicationContext.getSystemService(Context.WIFI_SERVICE) as WifiManager
    private val connectivityManager = context.getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
    private val telephonyManager = context.getSystemService(Context.TELEPHONY_SERVICE) as TelephonyManager
    private val mediaSessionManager = context.getSystemService(Context.MEDIA_SESSION_SERVICE) as MediaSessionManager

    // Cellular signal strength tracking
    private var currentCellularSignalStrength: Int = 0
    private var currentNetworkType: String = ""
    private var telephonyCallback: Any? = null  // TelephonyCallback

    // WiFi SSID caching (to handle background restrictions)
    private var cachedWifiSsid: String = ""
    private var currentWifiSignalStrength: Int = 0

    private val mediaCallbackHandler = Handler(Looper.getMainLooper())
    private var activeMediaController: MediaController? = null
    private var mediaSessionsListener: MediaSessionManager.OnActiveSessionsChangedListener? = null

    private val mediaControllerCallback = object : MediaController.Callback() {
        override fun onMetadataChanged(metadata: MediaMetadata?) {
            updateMusicStatus(activeMediaController)
        }

        override fun onPlaybackStateChanged(state: PlaybackState?) {
            updateMusicStatus(activeMediaController)
        }
    }

    private val batteryReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            updateBatteryStatus(intent)
        }
    }

    private val networkCallback = object : ConnectivityManager.NetworkCallback() {
        override fun onAvailable(network: Network) {
            refreshNetworkStatus()
        }

        override fun onCapabilitiesChanged(network: Network, networkCapabilities: NetworkCapabilities) {
            refreshNetworkStatus()
        }

        override fun onLost(network: Network) {
            refreshNetworkStatus()
        }
    }

    init {
        // Start listening for cellular signal strength and network type changes
        startCellularSignalListener()
        registerBatteryListener()
        registerNetworkCallback()
        registerMediaSessionListener()
        collectDeviceStatus()
    }

    /**
     * Collect current device status.
     */
    fun collectDeviceStatus(): DeviceStatus {
        val batteryPercent = getBatteryLevel()
        val isCharging = isCharging()
        val (wifiConnected, wifiSsid, wifiStrength) = getWifiInfo()
        val cellularStrength = getCellularSignalStrength()
        val cellularType = getCellularNetworkType()
        val (musicPlaying, songTitle, songArtist) = getMusicInfo()

        updateDeviceStatus(
            musicPlaying = musicPlaying,
            songTitle = songTitle,
            songArtist = songArtist,
            phoneBatteryPercent = batteryPercent,
            phoneCharging = isCharging,
            wifiConnected = wifiConnected,
            wifiSsid = wifiSsid,
            wifiSignalStrength = wifiStrength,
            cellularSignalStrength = cellularStrength,
            cellularType = cellularType
        )

        return _deviceStatus.value
    }

    private fun updateDeviceStatus(
        musicPlaying: Boolean? = null,
        songTitle: String? = null,
        songArtist: String? = null,
        phoneBatteryPercent: Int? = null,
        phoneCharging: Boolean? = null,
        wifiConnected: Boolean? = null,
        wifiSsid: String? = null,
        wifiSignalStrength: Int? = null,
        cellularSignalStrength: Int? = null,
        cellularType: String? = null
    ) {
        val current = _deviceStatus.value
        val updated = current.copy(
            musicPlaying = musicPlaying ?: current.musicPlaying,
            songTitle = songTitle ?: current.songTitle,
            songArtist = songArtist ?: current.songArtist,
            phoneBatteryPercent = phoneBatteryPercent ?: current.phoneBatteryPercent,
            phoneCharging = phoneCharging ?: current.phoneCharging,
            wifiConnected = wifiConnected ?: current.wifiConnected,
            wifiSsid = wifiSsid ?: current.wifiSsid,
            wifiSignalStrength = wifiSignalStrength ?: current.wifiSignalStrength,
            cellularSignalStrength = cellularSignalStrength ?: current.cellularSignalStrength,
            cellularType = cellularType ?: current.cellularType
        )

        if (updated != current) {
            _deviceStatus.value = updated
        }
    }

    private fun registerBatteryListener() {
        try {
            val filter = IntentFilter(Intent.ACTION_BATTERY_CHANGED)
            val initial = context.registerReceiver(batteryReceiver, filter)
            if (initial != null) {
                updateBatteryStatus(initial)
            }
        } catch (e: Exception) {
            Log.e("DeviceStatusManager", "Error registering battery receiver", e)
        }
    }

    private fun updateBatteryStatus(intent: Intent) {
        val level = intent.getIntExtra(BatteryManager.EXTRA_LEVEL, -1)
        val scale = intent.getIntExtra(BatteryManager.EXTRA_SCALE, -1)
        val percent = if (level >= 0 && scale > 0) (level * 100 / scale) else getBatteryLevel()
        val status = intent.getIntExtra(BatteryManager.EXTRA_STATUS, -1)
        val charging = status == BatteryManager.BATTERY_STATUS_CHARGING || status == BatteryManager.BATTERY_STATUS_FULL

        updateDeviceStatus(
            phoneBatteryPercent = percent,
            phoneCharging = charging
        )
    }

    private fun registerNetworkCallback() {
        try {
            connectivityManager.registerDefaultNetworkCallback(networkCallback)
            refreshNetworkStatus()
        } catch (e: Exception) {
            Log.e("DeviceStatusManager", "Error registering network callback", e)
        }
    }

    private fun refreshNetworkStatus() {
        val (wifiConnected, wifiSsid, wifiStrength) = getWifiInfo()
        currentWifiSignalStrength = wifiStrength
        val cellularType = getCellularNetworkType()

        updateDeviceStatus(
            wifiConnected = wifiConnected,
            wifiSsid = wifiSsid,
            cellularType = cellularType
        )
    }

    private fun registerMediaSessionListener() {
        try {
            val componentName = android.content.ComponentName(
                context,
                com.example.kolomapa2.utils.NotificationListener::class.java
            )

            val listener = MediaSessionManager.OnActiveSessionsChangedListener { controllers ->
                handleActiveSessionsChanged(controllers)
            }

            mediaSessionsListener = listener
            mediaSessionManager.addOnActiveSessionsChangedListener(listener, componentName, mediaCallbackHandler)
            handleActiveSessionsChanged(mediaSessionManager.getActiveSessions(componentName))
        } catch (e: SecurityException) {
            Log.w("DeviceStatusManager", "NotificationListener not enabled - cannot observe media sessions", e)
        } catch (e: Exception) {
            Log.e("DeviceStatusManager", "Error registering media session listener", e)
        }
    }

    private fun handleActiveSessionsChanged(controllers: List<MediaController>?) {
        val controller = controllers?.firstOrNull()
        if (activeMediaController === controller) {
            return
        }

        activeMediaController?.unregisterCallback(mediaControllerCallback)
        activeMediaController = controller

        if (controller != null) {
            controller.registerCallback(mediaControllerCallback, mediaCallbackHandler)
        }

        updateMusicStatus(controller)
    }

    private fun updateMusicStatus(controller: MediaController?) {
        if (controller == null) {
            updateDeviceStatus(
                musicPlaying = false,
                songTitle = "",
                songArtist = ""
            )
            return
        }

        val metadata = controller.metadata
        val playbackState = controller.playbackState
        val isPlaying = playbackState?.state == PlaybackState.STATE_PLAYING
        val title = metadata?.getString(MediaMetadata.METADATA_KEY_TITLE) ?: ""
        val artist = metadata?.getString(MediaMetadata.METADATA_KEY_ARTIST) ?: ""

        updateDeviceStatus(
            musicPlaying = isPlaying,
            songTitle = title,
            songArtist = artist
        )
    }

    private fun getBatteryLevel(): Int {
        return try {
            batteryManager.getIntProperty(BatteryManager.BATTERY_PROPERTY_CAPACITY)
        } catch (e: Exception) {
            Log.e("DeviceStatusManager", "Error getting battery level", e)
            0
        }
    }

    private fun isCharging(): Boolean {
        return try {
            val intentFilter = IntentFilter(Intent.ACTION_BATTERY_CHANGED)
            val batteryStatus = context.registerReceiver(null, intentFilter)
            val status = batteryStatus?.getIntExtra(BatteryManager.EXTRA_STATUS, -1) ?: -1
            status == BatteryManager.BATTERY_STATUS_CHARGING || status == BatteryManager.BATTERY_STATUS_FULL
        } catch (e: Exception) {
            Log.e("DeviceStatusManager", "Error checking charging status", e)
            false
        }
    }

    private fun getWifiInfo(): Triple<Boolean, String, Int> {
        return try {
            val network = connectivityManager.activeNetwork
            val capabilities = connectivityManager.getNetworkCapabilities(network)

            if (capabilities != null && capabilities.hasTransport(NetworkCapabilities.TRANSPORT_WIFI)) {
                val wifiInfo = wifiManager.connectionInfo
                var ssid = wifiInfo.ssid.replace("\"", "") // Remove quotes
                val rssi = wifiInfo.rssi

                // Handle Android's background WiFi SSID restriction
                // When app is in background/screen locked, SSID returns "<unknown ssid>"
                if (ssid == "<unknown ssid>" || ssid.isEmpty()) {
                    // Use cached SSID if available
                    if (cachedWifiSsid.isNotEmpty()) {
                        ssid = cachedWifiSsid
                        Log.d("DeviceStatusManager", "Using cached WiFi SSID: $ssid (background restriction)")
                    } else {
                        // No cached SSID available
                        ssid = "<unknown ssid>"
                    }
                } else {
                    // Valid SSID received - cache it for future use
                    cachedWifiSsid = ssid
                    Log.d("DeviceStatusManager", "WiFi SSID cached: $ssid")
                }

                // Convert RSSI (-100 to -50 dBm) to percentage (0-100)
                val signalStrength = when {
                    rssi >= -50 -> 100
                    rssi <= -100 -> 0
                    else -> ((rssi + 100) * 2).coerceIn(0, 100)
                }

                currentWifiSignalStrength = signalStrength
                Triple(true, ssid, signalStrength)
            } else {
                // Not connected to WiFi - clear cache
                cachedWifiSsid = ""
                currentWifiSignalStrength = 0
                Triple(false, "", 0)
            }
        } catch (e: Exception) {
            Log.e("DeviceStatusManager", "Error getting WiFi info", e)
            currentWifiSignalStrength = 0
            Triple(false, "", 0)
        }
    }

    /**
     * Start listening for cellular signal strength and network type changes.
     * Requires Android 14+ (minSdk 34).
     */
    private fun startCellularSignalListener() {
        // Check for READ_PHONE_STATE permission
        if (ActivityCompat.checkSelfPermission(context, Manifest.permission.READ_PHONE_STATE) != PackageManager.PERMISSION_GRANTED) {
            Log.w("DeviceStatusManager", "READ_PHONE_STATE permission not granted - cannot monitor cellular signal")
            return
        }

        try {
            // Use TelephonyCallback to monitor both signal strength and display info (network type)
            val callback = object : TelephonyCallback(),
                TelephonyCallback.SignalStrengthsListener,
                TelephonyCallback.DisplayInfoListener {

                override fun onSignalStrengthsChanged(signalStrength: SignalStrength) {
                    // Get signal level (0-4)
                    val level = signalStrength.level
                    // Convert to percentage (0-100)
                    currentCellularSignalStrength = when (level) {
                        0 -> 0      // No signal
                        1 -> 25     // Poor
                        2 -> 50     // Moderate
                        3 -> 75     // Good
                        4 -> 100    // Excellent
                        else -> 0
                    }
                    Log.d("DeviceStatusManager", "Cellular signal strength updated: $currentCellularSignalStrength% (level $level)")
                }

                override fun onDisplayInfoChanged(displayInfo: TelephonyDisplayInfo) {
                    // Get the override network type which includes 5G detection
                    val overrideNetworkType = displayInfo.overrideNetworkType
                    val networkType = displayInfo.networkType

                    currentNetworkType = when (overrideNetworkType) {
                        TelephonyDisplayInfo.OVERRIDE_NETWORK_TYPE_NR_NSA,
                        TelephonyDisplayInfo.OVERRIDE_NETWORK_TYPE_NR_ADVANCED -> "5G"
                        TelephonyDisplayInfo.OVERRIDE_NETWORK_TYPE_LTE_CA -> "LTE+"
                        TelephonyDisplayInfo.OVERRIDE_NETWORK_TYPE_LTE_ADVANCED_PRO -> "LTE+"
                        else -> {
                            // Fall back to basic network type
                            when (networkType) {
                                TelephonyManager.NETWORK_TYPE_NR -> "5G"
                                TelephonyManager.NETWORK_TYPE_LTE -> "LTE"
                                TelephonyManager.NETWORK_TYPE_HSPAP -> "HSPA+"
                                TelephonyManager.NETWORK_TYPE_HSPA,
                                TelephonyManager.NETWORK_TYPE_HSUPA,
                                TelephonyManager.NETWORK_TYPE_HSDPA -> "HSPA"
                                TelephonyManager.NETWORK_TYPE_UMTS -> "3G"
                                TelephonyManager.NETWORK_TYPE_EDGE -> "EDGE"
                                TelephonyManager.NETWORK_TYPE_GPRS -> "GPRS"
                                TelephonyManager.NETWORK_TYPE_CDMA,
                                TelephonyManager.NETWORK_TYPE_1xRTT -> "2G"
                                else -> ""
                            }
                        }
                    }
                    Log.d("DeviceStatusManager", "Network type updated: $currentNetworkType (override: $overrideNetworkType, base: $networkType)")
                    updateDeviceStatus(cellularType = getCellularNetworkType())
                }
            }

            telephonyCallback = callback
            val executor = context.mainExecutor
            telephonyManager.registerTelephonyCallback(executor, callback)
            Log.d("DeviceStatusManager", "Registered TelephonyCallback for signal strength and network type")
        } catch (e: Exception) {
            Log.e("DeviceStatusManager", "Error setting up cellular signal listener", e)
        }
    }

    /**
     * Stop listening for cellular signal strength changes.
     */
    fun stopCellularSignalListener() {
        try {
            telephonyCallback?.let { callback ->
                if (callback is TelephonyCallback) {
                    telephonyManager.unregisterTelephonyCallback(callback)
                    Log.d("DeviceStatusManager", "Unregistered TelephonyCallback")
                }
            }
            telephonyCallback = null
        } catch (e: Exception) {
            Log.e("DeviceStatusManager", "Error stopping cellular signal listener", e)
        }
    }

    private fun getCellularSignalStrength(): Int {
        return currentCellularSignalStrength
    }

    private fun getCellularNetworkType(): String {
        return try {
            // Check if we have any cellular network available (not just active network)
            // This way we show cellular type even when WiFi is the active connection
            val allNetworks = connectivityManager.allNetworks
            var hasCellular = false

            for (network in allNetworks) {
                val capabilities = connectivityManager.getNetworkCapabilities(network)
                if (capabilities != null && capabilities.hasTransport(NetworkCapabilities.TRANSPORT_CELLULAR)) {
                    hasCellular = true
                    break
                }
            }

            // Return cached network type if cellular is available
            // The TelephonyCallback keeps this updated even when WiFi is active
            if (hasCellular) {
                currentNetworkType
            } else {
                ""
            }
        } catch (e: Exception) {
            Log.e("DeviceStatusManager", "Error getting cellular network type", e)
            ""
        }
    }

    private fun getMusicInfo(): Triple<Boolean, String, String> {
        return try {
            val mediaSessionManager = context.getSystemService(Context.MEDIA_SESSION_SERVICE) as MediaSessionManager

            // Use the NotificationListener component to access media sessions
            val componentName = android.content.ComponentName(
                context,
                com.example.kolomapa2.utils.NotificationListener::class.java
            )

            val activeSessions = try {
                mediaSessionManager.getActiveSessions(componentName)
            } catch (e: SecurityException) {
                Log.w("DeviceStatusManager", "NotificationListener not enabled - cannot access media sessions", e)
                emptyList()
            }

            if (activeSessions.isNotEmpty()) {
                val controller = activeSessions[0]
                val metadata = controller.metadata
                val playbackState = controller.playbackState

                val isPlaying = playbackState?.state == android.media.session.PlaybackState.STATE_PLAYING
                val title = metadata?.getString(android.media.MediaMetadata.METADATA_KEY_TITLE) ?: ""
                val artist = metadata?.getString(android.media.MediaMetadata.METADATA_KEY_ARTIST) ?: ""

                Log.d("DeviceStatusManager", "Music info: $title by $artist (playing: $isPlaying)")
                Triple(isPlaying, title, artist)
            } else {
                Log.d("DeviceStatusManager", "No active media sessions")
                Triple(false, "", "")
            }
        } catch (e: Exception) {
            Log.e("DeviceStatusManager", "Error getting music info", e)
            Triple(false, "", "")
        }
    }

    /**
     * Send media control command (play/pause, next, previous).
     */
    fun sendMediaControl(action: MediaControlAction) {
        try {
            val mediaSessionManager = context.getSystemService(Context.MEDIA_SESSION_SERVICE) as MediaSessionManager

            // Use the NotificationListener component to access media sessions
            val componentName = android.content.ComponentName(
                context,
                com.example.kolomapa2.utils.NotificationListener::class.java
            )

            val activeSessions = try {
                mediaSessionManager.getActiveSessions(componentName)
            } catch (e: SecurityException) {
                Log.w("DeviceStatusManager", "NotificationListener not enabled - cannot send media control", e)
                return
            }

            if (activeSessions.isNotEmpty()) {
                val controller = activeSessions[0]
                val controls = controller.transportControls

                when (action) {
                    MediaControlAction.PLAY_PAUSE -> {
                        val playbackState = controller.playbackState
                        if (playbackState?.state == android.media.session.PlaybackState.STATE_PLAYING) {
                            controls.pause()
                            Log.d("DeviceStatusManager", "Sent pause command")
                        } else {
                            controls.play()
                            Log.d("DeviceStatusManager", "Sent play command")
                        }
                    }
                    MediaControlAction.NEXT -> {
                        controls.skipToNext()
                        Log.d("DeviceStatusManager", "Sent next track command")
                    }
                    MediaControlAction.PREVIOUS -> {
                        controls.skipToPrevious()
                        Log.d("DeviceStatusManager", "Sent previous track command")
                    }
                }
            } else {
                Log.w("DeviceStatusManager", "No active media sessions")
            }
        } catch (e: Exception) {
            Log.e("DeviceStatusManager", "Error sending media control", e)
        }
    }

    enum class MediaControlAction {
        PLAY_PAUSE,
        NEXT,
        PREVIOUS
    }
}
