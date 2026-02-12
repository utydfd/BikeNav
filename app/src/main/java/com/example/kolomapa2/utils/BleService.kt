package com.example.kolomapa2.utils

import android.app.*
import android.content.Context
import android.content.Intent
import android.os.Binder
import android.os.Build
import android.os.IBinder
import android.os.PowerManager
import androidx.core.app.NotificationCompat
import com.example.kolomapa2.MainActivity
import com.example.kolomapa2.R
import com.example.kolomapa2.models.Trip
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.serialization.encodeToString
import kotlinx.serialization.json.Json

class BleService : Service() {
    /*
 * ======================================================================================
 * CRITICAL STABILITY NOTE: TILE TRANSFER SYNCHRONIZATION (The "Ridiculous Zoom" Bug)
 * ======================================================================================
 *
 * THE ISSUE:
 * Previously, tile transfers failed with data corruption. The ESP32 logs would show
 * "Ignoring invalid header start: flags=0xFF zoom=255". This happened because the
 * Android app sent "Tile B" immediately after "Tile A" finished uploading.
 *
 * THE ROOT CAUSE:
 * The ESP32 writes to the SD card much slower than BLE can receive data.
 * 1. Android finishes sending Tile A chunks.
 * 2. ESP32 receives Tile A, but then BLOCKS execution to write 8KB-16KB to SD card.
 * 3. Android immediately starts sending Tile B.
 * 4. Because the ESP32 is busy writing to SD, it desynchronizes from the BLE stream.
 *    When it wakes up, it reads the *middle* of Tile B's compressed data as a new
 *    Header, leading to garbage values (e.g., Zoom 255).
 *
 * THE FIX (STOP-AND-WAIT FLOW CONTROL):
 * We implemented Application-Level Flow Control using BLE Notifications (ACKs).
 * Standard BLE "Write With Response" is NOT sufficient because the BLE Stack sends
 * the response before the App Logic (SD Write) is actually finished.
 *
 * DO NOT CHANGE THE FOLLOWING LOGIC:
 *
 * 1. ESP32 SIDE:
 *    - Must keep `PROPERTY_NOTIFY` on the Tile Characteristic.
 *    - Must call `pTileCharacteristic->notify()` ONLY AFTER `saveTileToSD` returns.
 *
 * 2. ANDROID SIDE:
 *    - Must listen for the notification via `onCharacteristicChanged`.
 *    - Must use `_tileAckFlow.first()` (or equivalent) to SUSPEND execution
 *      after sending a tile.
 *    - Must NOT send the next tile until the ACK is received or a timeout occurs.
 *
 * Removing this wait logic will re-introduce data corruption immediately.
 * ======================================================================================
 */
    companion object {
        private const val NOTIFICATION_ID = 1001
        private const val CHANNEL_ID = "ble_service_channel"
        private const val ACTION_DISCONNECT = "com.example.kolomapa2.ACTION_DISCONNECT"
        private const val ACTION_NOTIFICATION_DISMISSED = "com.example.kolomapa2.ACTION_NOTIFICATION_DISMISSED"
        const val EXTRA_AUTO_CONNECT = "com.example.kolomapa2.EXTRA_AUTO_CONNECT"
    }

    sealed class ServiceState {
        object Idle : ServiceState()
        object Connecting : ServiceState()
        data class Connected(val deviceName: String = "KoloMapa2") : ServiceState()
        data class Transferring(
            val message: String,
            val percentage: Int,
            val tripFileName: String,
            val tilesSent: Int? = null,
            val tilesTotal: Int? = null
        ) : ServiceState()
        data class Downloading(val message: String, val percentage: Int, val tripFileName: String) : ServiceState()
        data class Error(val message: String) : ServiceState()
    }

    private val _serviceState = MutableStateFlow<ServiceState>(ServiceState.Idle)
    val serviceState: StateFlow<ServiceState> = _serviceState
    private val _transferState = MutableStateFlow<ServiceState.Transferring?>(null)
    val transferState: StateFlow<ServiceState.Transferring?> = _transferState
    private val _downloadState = MutableStateFlow<ServiceState.Downloading?>(null)
    val downloadState: StateFlow<ServiceState.Downloading?> = _downloadState

    private lateinit var bleManager: BleManager
    private lateinit var notificationManager: NotificationManager
    private var wakeLock: PowerManager.WakeLock? = null

    private val serviceScope = CoroutineScope(SupervisorJob() + Dispatchers.Main)
    private val binder = LocalBinder()
    private var transferJob: Job? = null
    private var downloadJob: Job? = null
    private var connectionTimeoutJob: Job? = null
    private var reconnectJob: Job? = null
    private var stopRequested = false
    private val scanWindowMs = 4000L
    private val scanPauseMs = 5000L

    inner class LocalBinder : Binder() {
        fun getService(): BleService = this@BleService
        fun getBleManager(): BleManager = bleManager
    }

    override fun onBind(intent: Intent?): IBinder = binder

    override fun onCreate() {
        super.onCreate()
        bleManager = BleManager(applicationContext)
        notificationManager = getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
        createNotificationChannel()

        val powerManager = getSystemService(Context.POWER_SERVICE) as PowerManager
        wakeLock = powerManager.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "KoloMapa2::BleServiceWakeLock")

        serviceScope.launch {
            bleManager.connectionState.collect { connectionState ->
                android.util.Log.d("BleService", "BleManager state changed: $connectionState -> ServiceState will be updated")
                when (connectionState) {
                    is BleManager.ConnectionState.SCANNING -> {
                        android.util.Log.d("BleService", "Setting ServiceState to Connecting")
                        _serviceState.value = ServiceState.Connecting
                        updateNotification()
                    }
                    is BleManager.ConnectionState.CONNECTING -> {
                        android.util.Log.d("BleService", "Setting ServiceState to Connecting")
                        _serviceState.value = ServiceState.Connecting
                        updateNotification()

                        // Start connection timeout (30 seconds)
                        connectionTimeoutJob?.cancel()
                        connectionTimeoutJob = serviceScope.launch {
                            delay(30000)
                            if (_serviceState.value is ServiceState.Connecting) {
                                android.util.Log.w("BleService", "Connection timeout - disconnecting")
                                _serviceState.value = ServiceState.Error("Connection timeout - retrying")
                                updateNotification()
                                bleManager.disconnect()
                                // Don't stop service - let user retry
                            }
                        }
                    }
                    is BleManager.ConnectionState.CONNECTED -> {
                        android.util.Log.d("BleService", "BLE Connected! Setting ServiceState to Connected")
                        // Cancel timeout on successful connection
                        connectionTimeoutJob?.cancel()
                        connectionTimeoutJob = null
                        reconnectJob?.cancel()
                        reconnectJob = null

                        if (_serviceState.value !is ServiceState.Transferring) {
                            _serviceState.value = ServiceState.Connected()
                            updateNotification()
                            android.util.Log.d("BleService", "ServiceState updated to: ${_serviceState.value}")
                        } else {
                            android.util.Log.d("BleService", "Not updating to Connected - currently transferring")
                        }
                    }
                    is BleManager.ConnectionState.DISCONNECTED -> {
                        // Cancel timeout on disconnect
                        connectionTimeoutJob?.cancel()
                        connectionTimeoutJob = null

                    if (!stopRequested) {
                        scheduleReconnect()
                    } else {
                        _serviceState.value = ServiceState.Idle
                        updateNotification()
                    }
                    }
                    is BleManager.ConnectionState.ERROR -> {
                        // Cancel timeout on error
                        connectionTimeoutJob?.cancel()
                        connectionTimeoutJob = null

                        _serviceState.value = ServiceState.Error(connectionState.message)
                        updateNotification()
                        if (!stopRequested) {
                            scheduleReconnect(initialDelayMs = 3000L)
                        }
                    }
                    is BleManager.ConnectionState.BLUETOOTH_DISABLED -> {
                        connectionTimeoutJob?.cancel()
                        connectionTimeoutJob = null
                        _serviceState.value = ServiceState.Error("Bluetooth disabled - enable to reconnect")
                        updateNotification()
                    }
                    else -> {}
                }
            }
        }
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (intent?.action == ACTION_NOTIFICATION_DISMISSED) {
            when (_serviceState.value) {
                is ServiceState.Connecting,
                is ServiceState.Connected,
                is ServiceState.Error -> disconnect()
                else -> {}
            }
            return START_NOT_STICKY
        }
        if (intent?.action == ACTION_DISCONNECT) {
            stopRequested = true
            reconnectJob?.cancel()
            reconnectJob = null
            // Handle cancel/disconnect based on current state
            when {
                _transferState.value != null -> cancelTransfer()
                _downloadState.value != null -> cancelDownload()
                else -> disconnect()
            }
            return START_NOT_STICKY
        }
        stopRequested = false
        startForeground(NOTIFICATION_ID, buildNotification())

        val currentState = _serviceState.value
        // Start scan if idle or in error state (allows retry after failure)
        if (currentState is ServiceState.Idle || currentState is ServiceState.Error) {
            android.util.Log.d("BleService", "Starting scan from state: $currentState")
            serviceScope.launch {
                // Force disconnect first to clean up any stuck state
                bleManager.disconnect()
                delay(200)
                scheduleReconnect()
            }
        }
        return START_STICKY
    }

    override fun onDestroy() {
        super.onDestroy()
        connectionTimeoutJob?.cancel()
        connectionTimeoutJob = null
        reconnectJob?.cancel()
        reconnectJob = null
        transferJob?.cancel()
        transferJob = null
        downloadJob?.cancel()
        downloadJob = null
        serviceScope.cancel()
        bleManager.disconnect()
        if (wakeLock?.isHeld == true) wakeLock?.release()
    }

    fun cancelTransfer() {
        transferJob?.cancel()
        transferJob = null
        _transferState.value = null
        _serviceState.value = ServiceState.Connected()
        updateNotification()
        if (wakeLock?.isHeld == true) wakeLock?.release()
    }

    fun cancelDownload() {
        downloadJob?.cancel()
        downloadJob = null
        _downloadState.value = null

        finishDownloadState()
        if (wakeLock?.isHeld == true) wakeLock?.release()
    }

    fun disconnect() {
        serviceScope.launch {
            stopRequested = true
            reconnectJob?.cancel()
            reconnectJob = null
            // Cancel any ongoing transfer first
            transferJob?.cancel()
            transferJob = null
            downloadJob?.cancel()
            downloadJob = null
            _transferState.value = null
            _downloadState.value = null

            _serviceState.value = ServiceState.Idle
            bleManager.disconnect()
            stopForeground(STOP_FOREGROUND_REMOVE)
            stopSelf()
        }
    }

    fun downloadTilesForTrip(
        tripFileName: String,
        minLat: Double,
        maxLat: Double,
        minLon: Double,
        maxLon: Double,
        tileDownloader: TileDownloader,
        tilePreprocessor: TilePreprocessor,
        onError: (String) -> Unit = {},
        onComplete: (success: Boolean, downloadedCount: Int) -> Unit
    ) {
        downloadJob = serviceScope.launch {
            try {
                wakeLock?.acquire(10 * 60 * 1000L)

                _downloadState.value = ServiceState.Downloading("Preparing download", 0, tripFileName)
                updateNotification()

                val downloadResult = tileDownloader.downloadTilesForTrip(
                    minLat = minLat,
                    maxLat = maxLat,
                    minLon = minLon,
                    maxLon = maxLon,
                    tilePreprocessor = tilePreprocessor
                ) { current, total ->
                    val pct = if (total > 0) ((current.toFloat() / total) * 100).toInt() else 0
                    val downloadingState = ServiceState.Downloading("Downloading tiles", pct, tripFileName)
                    _downloadState.value = downloadingState
                    updateNotification()
                }

                if (downloadResult.failedCount > 0) {
                    onError(downloadResult.firstErrorMessage ?: "Some tiles failed to download")
                }
                onComplete(downloadResult.failedCount == 0, downloadResult.downloadedCount)
                finishDownloadState()

            } catch (e: CancellationException) {
                onComplete(false, 0)
                finishDownloadState()
            } catch (e: Exception) {
                android.util.Log.e("BleService", "Error downloading tiles", e)
                onComplete(false, 0)
                finishDownloadState()
            } finally {
                _downloadState.value = null
                if (wakeLock?.isHeld == true) wakeLock?.release()
                downloadJob = null
            }
        }
    }

    fun sendTripAndTiles(
        trip: Trip,
        tiles: List<Triple<Int, Int, Int>>,
        storageManager: StorageManager,
        tilePreprocessor: TilePreprocessor,
        onComplete: (success: Boolean, error: String?) -> Unit
    ) {
        transferJob = serviceScope.launch {
            try {
                wakeLock?.acquire(10 * 60 * 1000L)

                if (bleManager.connectionState.value !is BleManager.ConnectionState.CONNECTED) {
                    onComplete(false, "Not connected")
                    return@launch
                }

                // 1. Send Trip
                _transferState.value = ServiceState.Transferring("Transferring", 0, trip.metadata.fileName)
                _serviceState.value = ServiceState.Transferring("Transferring", 0, trip.metadata.fileName)
                updateNotification()

                val metadataJson = Json.encodeToString(trip.metadata)
                val tripSuccess = bleManager.sendTripWithProgress(trip.metadata.fileName, trip.gpxContent, metadataJson) { bytes, total ->
                    val pct = ((bytes.toFloat() / total) * 10).toInt()
                    val transferringState = ServiceState.Transferring("Transferring", pct, trip.metadata.fileName)
                    _transferState.value = transferringState
                    _serviceState.value = transferringState
                    updateNotification()
                }

                if (!tripSuccess) {
                    onComplete(false, "Failed to send trip")
                    return@launch
                }

                // 2. Send Tiles (Delegated entirely to BleManager for flow control)
                if (tiles.isNotEmpty()) {
                    val checkingState = ServiceState.Transferring("Checking tiles", 10, trip.metadata.fileName)
                    _transferState.value = checkingState
                    _serviceState.value = checkingState
                    updateNotification()

                    val tilesToSend = bleManager.filterMissingTiles(tiles)
                    if (tilesToSend.isNotEmpty()) {
                        val tilesTotal = tilesToSend.size
                        val transferringState = ServiceState.Transferring(
                            "Transferring",
                            10,
                            trip.metadata.fileName,
                            tilesSent = 0,
                            tilesTotal = tilesTotal
                        )
                        _transferState.value = transferringState
                        _serviceState.value = transferringState
                        updateNotification()

                        // Using the new ACK-based sending function with on-the-fly processing
                        bleManager.sendTiles(tilesToSend, storageManager, tilePreprocessor) { current, total ->
                            // Map progress 10% -> 100%
                            val pct = 10 + ((current.toFloat() / total) * 90).toInt()
                            val progressState = ServiceState.Transferring(
                                "Transferring",
                                pct,
                                trip.metadata.fileName,
                                tilesSent = current,
                                tilesTotal = total
                            )
                            _transferState.value = progressState
                            _serviceState.value = progressState
                            updateNotification()
                        }
                    } else {
                        val transferringState = ServiceState.Transferring("Transferring", 100, trip.metadata.fileName)
                        _transferState.value = transferringState
                        _serviceState.value = transferringState
                        updateNotification()
                    }
                }

                onComplete(true, null)
                _transferState.value = null
                _serviceState.value = ServiceState.Connected()
                updateNotification()

            } catch (e: CancellationException) {
                // Transfer was cancelled - this is expected, don't log as error
                onComplete(false, "Transfer cancelled")
                _transferState.value = null
                _serviceState.value = ServiceState.Connected()
                updateNotification()
            } catch (e: Exception) {
                onComplete(false, e.message)
                _transferState.value = null
                _serviceState.value = ServiceState.Connected()
                updateNotification()
            } finally {
                _transferState.value = null
                if (wakeLock?.isHeld == true) wakeLock?.release()
                transferJob = null
            }
        }
    }

    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val channel = NotificationChannel(CHANNEL_ID, "BLE & Downloads", NotificationManager.IMPORTANCE_LOW).apply {
                description = "KoloMapa2 Connection & Tile Downloads"
                setShowBadge(false)
            }
            notificationManager.createNotificationChannel(channel)
        }
    }

    private fun buildNotification(): Notification {
        val intent = Intent(this, MainActivity::class.java).apply { flags = Intent.FLAG_ACTIVITY_SINGLE_TOP }
        val disconnectIntent = Intent(this, BleService::class.java).apply { action = ACTION_DISCONNECT }
        val disconnectPendingIntent = PendingIntent.getService(this, 0, disconnectIntent, PendingIntent.FLAG_IMMUTABLE)
        val dismissIntent = Intent(this, BleService::class.java).apply { action = ACTION_NOTIFICATION_DISMISSED }
        val dismissPendingIntent = PendingIntent.getService(this, 1, dismissIntent, PendingIntent.FLAG_IMMUTABLE)

        val builder = NotificationCompat.Builder(this, CHANNEL_ID)
            .setSmallIcon(R.drawable.ic_launcher_foreground)
            .setOngoing(true)
            .setPriority(NotificationCompat.PRIORITY_LOW)

        val activeTransfer = _transferState.value
        val activeDownload = _downloadState.value

        when {
            activeTransfer != null -> builder.setContentTitle(activeTransfer.message).setContentText("${activeTransfer.percentage}%")
                .setProgress(100, activeTransfer.percentage, false)
                .addAction(android.R.drawable.ic_menu_close_clear_cancel, "Cancel", disconnectPendingIntent)
            activeDownload != null -> builder.setContentTitle(activeDownload.message).setContentText("${activeDownload.tripFileName} - ${activeDownload.percentage}%")
                .setProgress(100, activeDownload.percentage, false)
                .addAction(android.R.drawable.ic_menu_close_clear_cancel, "Cancel", disconnectPendingIntent)
            else -> when (val state = _serviceState.value) {
                is ServiceState.Idle -> {
                    intent.putExtra(EXTRA_AUTO_CONNECT, true)
                    builder.setContentTitle("Disconnected").setContentText("Tap to reconnect")
                }
                is ServiceState.Connecting -> builder.setContentTitle("Connecting").setContentText("Scanning for device...")
                    .setDeleteIntent(dismissPendingIntent)
                    .addAction(android.R.drawable.ic_menu_close_clear_cancel, "Cancel", disconnectPendingIntent)
                is ServiceState.Connected -> builder.setContentTitle("Connected").setContentText("${state.deviceName} ready")
                    .setDeleteIntent(dismissPendingIntent)
                    .addAction(android.R.drawable.ic_menu_close_clear_cancel, "Disconnect", disconnectPendingIntent)
                is ServiceState.Transferring -> builder.setContentTitle(state.message).setContentText("${state.percentage}%")
                    .setProgress(100, state.percentage, false)
                    .addAction(android.R.drawable.ic_menu_close_clear_cancel, "Cancel", disconnectPendingIntent)
                is ServiceState.Downloading -> builder.setContentTitle(state.message).setContentText("${state.tripFileName} - ${state.percentage}%")
                    .setProgress(100, state.percentage, false)
                    .addAction(android.R.drawable.ic_menu_close_clear_cancel, "Cancel", disconnectPendingIntent)
                is ServiceState.Error -> {
                    intent.putExtra(EXTRA_AUTO_CONNECT, true)
                    builder.setContentTitle("Connection Error").setContentText(state.message)
                        .setDeleteIntent(dismissPendingIntent)
                        .addAction(android.R.drawable.ic_menu_close_clear_cancel, "Close", disconnectPendingIntent)
                }
            }
        }
        val pendingIntent = PendingIntent.getActivity(this, 0, intent, PendingIntent.FLAG_IMMUTABLE)
        builder.setContentIntent(pendingIntent)
        return builder.build()
    }

    private fun updateNotification() {
        notificationManager.notify(NOTIFICATION_ID, buildNotification())
    }

    private fun finishDownloadState() {
        if (_transferState.value != null ||
            _serviceState.value is ServiceState.Transferring ||
            _serviceState.value is ServiceState.Connecting ||
            _serviceState.value is ServiceState.Error) {
            updateNotification()
            return
        }
        if (bleManager.connectionState.value is BleManager.ConnectionState.CONNECTED) {
            _serviceState.value = ServiceState.Connected()
            updateNotification()
        } else {
            if (!stopRequested) {
                scheduleReconnect()
            } else {
                _serviceState.value = ServiceState.Idle
                updateNotification()
            }
        }
    }

    private fun scheduleReconnect(initialDelayMs: Long = 0L) {
        if (stopRequested) {
            return
        }
        reconnectJob?.cancel()
        reconnectJob = serviceScope.launch {
            delay(initialDelayMs)
            while (isActive && !stopRequested) {
                when (bleManager.connectionState.value) {
                    is BleManager.ConnectionState.CONNECTED,
                    is BleManager.ConnectionState.CONNECTING -> return@launch
                    else -> {}
                }

                if (!bleManager.isBluetoothEnabled()) {
                    _serviceState.value = ServiceState.Error("Bluetooth disabled - enable to reconnect")
                    updateNotification()
                    delay(scanPauseMs)
                    continue
                }

                _serviceState.value = ServiceState.Connecting
                updateNotification()
                bleManager.startScan()

                delay(scanWindowMs)
                if (bleManager.connectionState.value is BleManager.ConnectionState.SCANNING) {
                    bleManager.stopScan()
                }

                delay(scanPauseMs)
            }
        }
    }
}
