package com.example.kolomapa2

import android.app.Application
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothManager
import android.content.BroadcastReceiver
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.ServiceConnection
import android.net.Uri
import android.os.Build
import android.os.IBinder
import android.provider.Settings
import androidx.core.app.NotificationManagerCompat
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.example.kolomapa2.models.RecordingInfo
import com.example.kolomapa2.models.RecordingMetadata
import com.example.kolomapa2.models.Trip
import com.example.kolomapa2.models.WeatherCondition
import com.example.kolomapa2.utils.*
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.TimeoutCancellationException
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeout
import kotlinx.serialization.decodeFromString
import kotlinx.serialization.encodeToString
import kotlinx.serialization.json.Json
import android.util.Log
import java.time.ZoneId
import java.util.concurrent.atomic.AtomicBoolean

class MainViewModel(application: Application) : AndroidViewModel(application) {

    internal val storageManager = StorageManager(application)
    private val gpxParser = GpxParser()
    private val tilePreprocessor = TilePreprocessor()
    private val tileDownloader = TileDownloader(storageManager)
    private val weatherApiService = WeatherApiService()
    private val radarApiService = RadarApiService()
    private val recordingJson = Json { ignoreUnknownKeys = true }
    private val radarOverlayProcessor = RadarOverlayProcessor()
    private val routingService = RoutingService()
    private val deviceStatusManager = DeviceStatusManager(application)
    private val deviceStatusSendMutex = Mutex()
    private val ignoreDeviceStatusUpdates = AtomicBoolean(false)
    private var lastSentDeviceStatus: DeviceStatusManager.DeviceStatus? = null
    private var deviceStatusObserverJob: Job? = null
    private var navigateHomeRequestTimeoutJob: Job? = null
    internal val locatePhoneManager = LocatePhoneManager(application)  // Expose for manual stop
    private val geocodingService = GeocodingService()  // For route planning location search

    // BLE Manager from service (set when service connects)
    var bleManager: BleManager? = null
        private set

    // BLE Service
    private var bleService: BleService? = null
    private var serviceBound = false

    private val _serviceState = MutableStateFlow<BleService.ServiceState>(BleService.ServiceState.Idle)
    val serviceState: StateFlow<BleService.ServiceState> = _serviceState
    private val _transferState = MutableStateFlow<BleService.ServiceState.Transferring?>(null)
    val transferState: StateFlow<BleService.ServiceState.Transferring?> = _transferState
    private val _downloadState = MutableStateFlow<BleService.ServiceState.Downloading?>(null)
    val downloadState: StateFlow<BleService.ServiceState.Downloading?> = _downloadState

    private val isBleConnectedForCommands: Boolean
        get() = bleManager?.connectionState?.value is BleManager.ConnectionState.CONNECTED &&
            _serviceState.value !is BleService.ServiceState.Transferring

    private val serviceConnection = object : ServiceConnection {
        override fun onServiceConnected(name: ComponentName?, service: IBinder?) {
            val binder = service as BleService.LocalBinder
            bleService = binder.getService()
            bleManager = binder.getBleManager()
            serviceBound = true

            Log.d("MainViewModel", "Service connected, setting up callbacks on bleManager: $bleManager")

            // Set up weather request handler from ESP32
            bleManager?.onWeatherRequest = { latitude, longitude ->
                Log.d("MainViewModel", "Weather request callback triggered!")
                handleWeatherRequest(latitude, longitude)
            }

            // Set up radar request handler from ESP32
            bleManager?.onRadarRequest = { latitude, longitude, zoom ->
                Log.d("MainViewModel", "Radar request callback triggered!")
                handleRadarRequest(latitude, longitude, zoom)
            }

            // Set up navigate home request handler from ESP32
            bleManager?.onNavigateHomeRequest = { latitude, longitude ->
                Log.d("MainViewModel", "Navigate home request callback triggered!")
                handleNavigateHomeRequest(latitude, longitude)
            }

            // Set up notification dismissal handler from ESP32
            bleManager?.onNotificationDismissed = { notificationId ->
                Log.d("MainViewModel", "Notification dismissal from ESP32: ID=$notificationId")
                handleNotificationDismissalFromEsp(notificationId)
            }

            // Set up trip list handler from ESP32
            bleManager?.onTripListReceived = { tripNames ->
                Log.d("MainViewModel", "Trip list received from ESP32: ${tripNames.size} trips")
                _tripsOnEsp.value = tripNames.toSet()
                tripNames.forEach { name ->
                    Log.d("MainViewModel", "  - Trip on ESP32: $name")
                }
            }

            // Set up recording list handler from ESP32
            bleManager?.onRecordingListReceived = { recordingNames ->
                Log.d("MainViewModel", "Recording list received from ESP32: ${recordingNames.size} recordings")
                _recordedTrips.value = recordingNames.map { name ->
                    RecordingInfo(name = name, dirName = name)
                }
                _isLoadingRecordedTrips.value = false
            }

            bleManager?.onRecordingTransferProgress = { recordingName, receivedBytes, totalBytes ->
                _recordingDownloadProgress.value = RecordingDownloadProgress(
                    recordingName = recordingName,
                    current = receivedBytes,
                    total = totalBytes,
                    message = "Downloading"
                )
            }

            bleManager?.onRecordingTransferCompleted = { recordingName, metadataJson, gpxContent ->
                viewModelScope.launch {
                    try {
                        val trip = withContext(Dispatchers.IO) {
                            buildTripFromRecording(recordingName, metadataJson, gpxContent)
                        }
                        withContext(Dispatchers.IO) {
                            storageManager.saveRecordedTrip(trip)
                        }
                        _recordedTripsOnPhone.value = withContext(Dispatchers.IO) {
                            storageManager.loadAllRecordedTrips()
                        }
                        _recordedTripsAdded.value = withContext(Dispatchers.IO) {
                            _recordedTripsOnPhone.value.mapNotNull { recorded ->
                                if (storageManager.isRecordedTripAdded(recorded.metadata.fileName)) {
                                    recorded.metadata.fileName
                                } else {
                                    null
                                }
                            }.toSet()
                        }
                        _downloadedRecordingTrip.value = trip
                    } catch (e: Exception) {
                        Log.e("MainViewModel", "Failed to import recording: ${e.message}", e)
                        _errorMessage.value = "Failed to import recording: ${e.message}"
                    } finally {
                        _recordingDownloadProgress.value = null
                    }
                }
            }

            bleManager?.onRecordingTransferError = { recordingName, message ->
                Log.e("MainViewModel", "Recording transfer error for ${recordingName ?: "unknown"}: $message")
                _recordingDownloadProgress.value = null
                _errorMessage.value = "Recording transfer failed: $message"
            }

            // Set up active trip handler from ESP32
            bleManager?.onActiveTripChanged = { tripName ->
                Log.d("MainViewModel", "Active trip changed on ESP32: ${tripName ?: "none"}")
                _activeTripOnEsp.value = tripName

                // Track Navigate Home active state
                _isNavigateHomeActive.value = tripName == Constants.NAVIGATE_HOME_TRIP_NAME
                if (_isNavigateHomeActive.value) {
                    Log.d("MainViewModel", "Navigate Home is now ACTIVE")
                } else {
                    Log.d("MainViewModel", "Navigate Home is now INACTIVE")
                }
            }

            // Set up ESP32 device status handler
            bleManager?.onEspDeviceStatusReceived = { status ->
                Log.d("MainViewModel", "ESP32 device status received: battery=${status.batteryPercent}%, GPS stage=${status.gpsStage}")
                _espDeviceStatus.value = status
            }

            // Set up device status command handlers from ESP32
            bleManager?.onMusicPlayPause = {
                Log.d("MainViewModel", "Music Play/Pause command from ESP32")
                deviceStatusManager.sendMediaControl(DeviceStatusManager.MediaControlAction.PLAY_PAUSE)
                // Send updated status after command
                viewModelScope.launch {
                    kotlinx.coroutines.delay(500) // Wait for media state to update
                    refreshAndSendDeviceStatus(includeSignalStrength = false)
                }
            }

            bleManager?.onMusicNext = {
                Log.d("MainViewModel", "Music Next command from ESP32")
                deviceStatusManager.sendMediaControl(DeviceStatusManager.MediaControlAction.NEXT)
                // Send updated status after command
                viewModelScope.launch {
                    kotlinx.coroutines.delay(500)
                    refreshAndSendDeviceStatus(includeSignalStrength = false)
                }
            }

            bleManager?.onMusicPrevious = {
                Log.d("MainViewModel", "Music Previous command from ESP32")
                deviceStatusManager.sendMediaControl(DeviceStatusManager.MediaControlAction.PREVIOUS)
                // Send updated status after command
                viewModelScope.launch {
                    kotlinx.coroutines.delay(500)
                    refreshAndSendDeviceStatus(includeSignalStrength = false)
                }
            }

            bleManager?.onLocatePhone = {
                Log.w("MainViewModel", "⚠️ Locate Phone command received from ESP32!")
                Log.w("MainViewModel", "Stack trace:", Exception("Locate phone triggered"))
                locatePhoneManager.startLocatePhone(10000) // Ring for 10 seconds
            }

            bleManager?.onToggleNotificationSync = {
                Log.d("MainViewModel", "Toggle Notification Sync command from ESP32")
                toggleNotificationSyncFromEsp()
            }

            bleManager?.onDeviceStatusRequested = {
                Log.d("MainViewModel", "Device status requested by ESP32")
                viewModelScope.launch {
                    refreshAndSendDeviceStatus(includeSignalStrength = true, force = true)
                }
            }

            // Set up notification listener callbacks
            setupNotificationListenerCallbacks()

            // Start change-driven device status updates
            startDeviceStatusChangeObserver()

            // Monitor service state
            viewModelScope.launch {
                bleService?.serviceState?.collect { state ->
                    _serviceState.value = state

                    // When connected, enable periodic updates and request immediate data
                    if (state is BleService.ServiceState.Connected) {
                        Log.d("MainViewModel", "BLE Connected - enabling periodic updates and requesting immediate data")
                        enablePeriodicStatusUpdates()
                        refreshAndSendDeviceStatus(includeSignalStrength = false, force = true)
                    }
                }
            }

            viewModelScope.launch {
                bleService?.transferState?.collect { state ->
                    _transferState.value = state
                }
            }

            viewModelScope.launch {
                bleService?.downloadState?.collect { state ->
                    _downloadState.value = state
                    if (state == null) {
                        activeDownloadTripFileName?.let { fileName ->
                            _downloadingTrips.value = _downloadingTrips.value - fileName
                            activeDownloadTripFileName = null
                        }
                    } else {
                        activeDownloadTripFileName?.takeIf { it != state.tripFileName }?.let { fileName ->
                            _downloadingTrips.value = _downloadingTrips.value - fileName
                        }
                        activeDownloadTripFileName = state.tripFileName
                        val isPreparing = state.message.equals("Preparing download", ignoreCase = true)
                        val message = if (isPreparing) state.message else "${state.message} ${state.percentage}%"
                        _downloadingTrips.value = _downloadingTrips.value +
                            (state.tripFileName to DownloadProgress(
                                current = if (isPreparing) 0 else state.percentage,
                                total = if (isPreparing) 0 else 100,
                                message = message
                            ))
                    }
                }
            }
        }

        override fun onServiceDisconnected(name: ComponentName?) {
            bleService = null
            serviceBound = false
            _serviceState.value = BleService.ServiceState.Idle
            _transferState.value = null
            _downloadState.value = null
            _downloadingTrips.value = emptyMap()
            activeDownloadTripFileName = null
        }
    }

    // Bluetooth Adapter
    private val bluetoothManager = application.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager
    private val bluetoothAdapter: BluetoothAdapter? = bluetoothManager.adapter

    // Bluetooth state
    private val _isBluetoothEnabled = MutableStateFlow(bluetoothAdapter?.isEnabled == true)
    val isBluetoothEnabled: StateFlow<Boolean> = _isBluetoothEnabled

    private var pendingConnection = false // Flag to auto-connect after BT is enabled

    // Bluetooth state receiver
    private val bluetoothStateReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context?, intent: Intent?) {
            when (intent?.action) {
                BluetoothAdapter.ACTION_STATE_CHANGED -> {
                    val state = intent.getIntExtra(BluetoothAdapter.EXTRA_STATE, BluetoothAdapter.ERROR)
                    when (state) {
                        BluetoothAdapter.STATE_OFF -> {
                            Log.w("MainViewModel", "Bluetooth turned OFF")
                            _isBluetoothEnabled.value = false
                            // Disconnect if currently connected
                            if (_serviceState.value !is BleService.ServiceState.Idle) {
                                Log.w("MainViewModel", "Disconnecting due to Bluetooth OFF")
                                stopBleService()
                            }
                        }
                        BluetoothAdapter.STATE_ON -> {
                            Log.i("MainViewModel", "Bluetooth turned ON")
                            _isBluetoothEnabled.value = true
                            // Clear Bluetooth error message
                            if (_errorMessage.value?.contains("Bluetooth") == true) {
                                _errorMessage.value = null
                            }
                            // Auto-connect if user tried to connect while BT was off
                            if (pendingConnection) {
                                Log.i("MainViewModel", "Auto-connecting after Bluetooth enabled")
                                pendingConnection = false
                                startBleService()
                            }
                        }
                    }
                }
            }
        }
    }

    // Device Status State (expose device status manager's state flow)
    val deviceStatus = deviceStatusManager.deviceStatus

    // UI State
    private val _trips = MutableStateFlow<List<Trip>>(emptyList())
    val trips: StateFlow<List<Trip>> = _trips

    private val _isLoadingTrips = MutableStateFlow(true)
    val isLoadingTrips: StateFlow<Boolean> = _isLoadingTrips

    private val _isLoading = MutableStateFlow(false)
    val isLoading: StateFlow<Boolean> = _isLoading

    // Per-trip download progress tracking (fileName -> progress)
    private val _downloadingTrips = MutableStateFlow<Map<String, DownloadProgress>>(emptyMap())
    val downloadingTrips: StateFlow<Map<String, DownloadProgress>> = _downloadingTrips
    private var activeDownloadTripFileName: String? = null

    private val _errorMessage = MutableStateFlow<String?>(null)
    val errorMessage: StateFlow<String?> = _errorMessage

    private val _storageStats = MutableStateFlow<StorageStats?>(null)
    val storageStats: StateFlow<StorageStats?> = _storageStats

    private val _notificationAccessEnabled = MutableStateFlow(false)
    val notificationAccessEnabled: StateFlow<Boolean> = _notificationAccessEnabled

    private val _notificationSyncEnabled = MutableStateFlow(
        application.getSharedPreferences("kolomapa2_prefs", Context.MODE_PRIVATE)
            .getBoolean("notification_sync_enabled", true)
    )
    val notificationSyncEnabled: StateFlow<Boolean> = _notificationSyncEnabled

    private val _tripsOnEsp = MutableStateFlow<Set<String>>(emptySet())
    val tripsOnEsp: StateFlow<Set<String>> = _tripsOnEsp

    private val _activeTripOnEsp = MutableStateFlow<String?>(null)
    val activeTripOnEsp: StateFlow<String?> = _activeTripOnEsp

    private val _isNavigateHomeActive = MutableStateFlow(false)
    val isNavigateHomeActive: StateFlow<Boolean> = _isNavigateHomeActive

    private val _navigateHomeTrip = MutableStateFlow<Trip?>(null)
    val navigateHomeTrip: StateFlow<Trip?> = _navigateHomeTrip

    private val _isLoadingNavigateHome = MutableStateFlow(false)
    val isLoadingNavigateHome: StateFlow<Boolean> = _isLoadingNavigateHome

    private val _navigateHomeError = MutableStateFlow<String?>(null)
    val navigateHomeError: StateFlow<String?> = _navigateHomeError

    // Route Planning State
    private val _startLocation = MutableStateFlow<com.example.kolomapa2.models.LocationPoint?>(null)
    val startLocation: StateFlow<com.example.kolomapa2.models.LocationPoint?> = _startLocation

    private val _endLocation = MutableStateFlow<com.example.kolomapa2.models.LocationPoint?>(null)
    val endLocation: StateFlow<com.example.kolomapa2.models.LocationPoint?> = _endLocation

    private val _startSuggestions = MutableStateFlow<List<com.example.kolomapa2.models.GeocodingSuggestion>>(emptyList())
    val startSuggestions: StateFlow<List<com.example.kolomapa2.models.GeocodingSuggestion>> = _startSuggestions

    private val _endSuggestions = MutableStateFlow<List<com.example.kolomapa2.models.GeocodingSuggestion>>(emptyList())
    val endSuggestions: StateFlow<List<com.example.kolomapa2.models.GeocodingSuggestion>> = _endSuggestions

    private val _plannedRoute = MutableStateFlow<Trip?>(null)
    val plannedRoute: StateFlow<Trip?> = _plannedRoute

    private val _isLoadingRoute = MutableStateFlow(false)
    val isLoadingRoute: StateFlow<Boolean> = _isLoadingRoute

    private val _routePlanningError = MutableStateFlow<String?>(null)
    val routePlanningError: StateFlow<String?> = _routePlanningError

    private val _locationRequestTarget = MutableStateFlow<com.example.kolomapa2.models.LocationTarget?>(null)

    private val _currentDeviceLocation = MutableStateFlow<com.example.kolomapa2.models.LocationPoint?>(null)
    val currentDeviceLocation: StateFlow<com.example.kolomapa2.models.LocationPoint?> = _currentDeviceLocation

    private val _espDeviceStatus = MutableStateFlow<BleManager.EspDeviceStatus?>(null)
    val espDeviceStatus: StateFlow<BleManager.EspDeviceStatus?> = _espDeviceStatus

    private val _recordedTrips = MutableStateFlow<List<RecordingInfo>>(emptyList())
    val recordedTrips: StateFlow<List<RecordingInfo>> = _recordedTrips

    private val _recordedTripsOnPhone = MutableStateFlow<List<Trip>>(emptyList())
    val recordedTripsOnPhone: StateFlow<List<Trip>> = _recordedTripsOnPhone

    private val _recordedTripsAdded = MutableStateFlow<Set<String>>(emptySet())
    val recordedTripsAdded: StateFlow<Set<String>> = _recordedTripsAdded

    private val _isLoadingRecordedTrips = MutableStateFlow(false)
    val isLoadingRecordedTrips: StateFlow<Boolean> = _isLoadingRecordedTrips

    private val _recordingDownloadProgress = MutableStateFlow<RecordingDownloadProgress?>(null)
    val recordingDownloadProgress: StateFlow<RecordingDownloadProgress?> = _recordingDownloadProgress

    private val _downloadedRecordingTrip = MutableStateFlow<Trip?>(null)
    val downloadedRecordingTrip: StateFlow<Trip?> = _downloadedRecordingTrip

    data class DownloadProgress(
        val current: Int,
        val total: Int,
        val message: String
    )

    data class RecordingDownloadProgress(
        val recordingName: String,
        val current: Int,
        val total: Int,
        val message: String
    )

    init {
        Log.d("MainViewModel", "========== MainViewModel INIT START ==========")
        // Note: loadTrips() is now called from MainScreen's LaunchedEffect
        // to ensure UI renders before loading starts
        updateStorageStats()
        checkNotificationAccess()

        // Register Bluetooth state receiver
        val filter = IntentFilter(BluetoothAdapter.ACTION_STATE_CHANGED)
        getApplication<Application>().registerReceiver(bluetoothStateReceiver, filter)
        Log.d("MainViewModel", "Bluetooth state receiver registered")

        Log.d("MainViewModel", "========== MainViewModel INIT COMPLETE ==========")
    }

    /**
     * Load all trips from storage
     */
    fun loadTrips() {
        viewModelScope.launch {
            _isLoadingTrips.value = true
            _trips.value = storageManager.loadAllTrips()
            updateStorageStats()
            _isLoadingTrips.value = false
        }
    }

    fun loadRecordedTripsFromStorage() {
        viewModelScope.launch {
            val recordings = withContext(Dispatchers.IO) {
                storageManager.loadAllRecordedTrips()
            }
            _recordedTripsOnPhone.value = recordings
            _recordedTripsAdded.value = withContext(Dispatchers.IO) {
                recordings.mapNotNull { trip ->
                    if (storageManager.isRecordedTripAdded(trip.metadata.fileName)) {
                        trip.metadata.fileName
                    } else {
                        null
                    }
                }.toSet()
            }
        }
    }

    /**
     * Request the list of recorded trips from the ESP32.
     */
    fun refreshRecordedTrips() {
        viewModelScope.launch {
            if (!isBleConnectedForCommands) {
                _errorMessage.value = "Device not connected"
                return@launch
            }

            _isLoadingRecordedTrips.value = true
            val success = bleManager?.requestRecordingList() ?: false
            if (!success) {
                _isLoadingRecordedTrips.value = false
                _errorMessage.value = "Failed to request recordings from device"
            }
        }
    }

    /**
     * Download a recorded trip from the ESP32.
     */
    fun downloadRecordedTrip(recordingName: String) {
        val existingTrip = _recordedTripsOnPhone.value.firstOrNull { it.metadata.fileName == recordingName }
        if (existingTrip != null) {
            _downloadedRecordingTrip.value = existingTrip
            return
        }

        viewModelScope.launch {
            if (!isBleConnectedForCommands) {
                _errorMessage.value = "Device not connected"
                return@launch
            }

            _recordingDownloadProgress.value = RecordingDownloadProgress(
                recordingName = recordingName,
                current = 0,
                total = 0,
                message = "Requesting"
            )
            val success = bleManager?.requestRecordingTransfer(recordingName) ?: false
            if (!success) {
                _recordingDownloadProgress.value = null
                _errorMessage.value = "Failed to request recording download"
            }
        }
    }

    fun clearDownloadedRecordingTrip() {
        _downloadedRecordingTrip.value = null
    }

    fun addRecordedTripToTrips(recordingTrip: Trip) {
        viewModelScope.launch {
            if (_recordedTripsAdded.value.contains(recordingTrip.metadata.fileName)) {
                return@launch
            }
            _recordedTripsAdded.value = _recordedTripsAdded.value + recordingTrip.metadata.fileName
            try {
                val uniqueFileName = createUniqueTripFileName(recordingTrip.metadata.fileName)
                val tripToSave = recordingTrip.copy(
                    metadata = recordingTrip.metadata.copy(fileName = uniqueFileName)
                )
                withContext(Dispatchers.IO) {
                    storageManager.saveTrip(tripToSave)
                    storageManager.markRecordedTripAdded(recordingTrip.metadata.fileName)
                }
                _trips.value = withContext(Dispatchers.IO) {
                    storageManager.loadAllTrips()
                }
                updateStorageStats()
            } catch (e: Exception) {
                Log.e("MainViewModel", "Failed to add recording to trips: ${e.message}", e)
                _errorMessage.value = "Failed to add recording to trips"
                _recordedTripsAdded.value = _recordedTripsAdded.value - recordingTrip.metadata.fileName
            }
        }
    }

    fun deleteRecordedTrip(fileName: String) {
        viewModelScope.launch {
            withContext(Dispatchers.IO) {
                storageManager.deleteRecordedTrip(fileName)
            }
            _recordedTripsOnPhone.value = withContext(Dispatchers.IO) {
                storageManager.loadAllRecordedTrips()
            }
            _recordedTripsAdded.value = _recordedTripsAdded.value - fileName
        }
    }

    /**
     * Parse GPX file for import preview
     * Calls the callback with the parsed trip and default name
     */
    fun parseGpxForImport(uri: Uri, callback: (Trip, String) -> Unit) {
        viewModelScope.launch {
            try {
                _errorMessage.value = null

                // Parse GPX
                val inputStream = getApplication<Application>().contentResolver.openInputStream(uri)
                    ?: throw IllegalArgumentException("Cannot open file")

                val fileName = uri.lastPathSegment?.substringAfterLast('/') ?: "imported_trip.gpx"
                val trip = gpxParser.parseGpx(inputStream, fileName)
                inputStream.close()

                // Extract default name (without .gpx extension)
                val defaultName = fileName.removeSuffix(".gpx")

                // Call the callback with the parsed trip
                callback(trip, defaultName)

            } catch (e: Exception) {
                e.printStackTrace()
                _errorMessage.value = "Failed to parse GPX: ${e.message}"
            }
        }
    }

    private fun buildTripFromRecording(recordingName: String, metadataJson: String, gpxContent: String): Trip {
        val parsedTrip = gpxParser.parseGpx(gpxContent.byteInputStream(), "$recordingName.gpx")
        var metadata = parsedTrip.metadata.copy(fileName = recordingName)

        if (metadataJson.isNotBlank()) {
            try {
                val recordingMeta = recordingJson.decodeFromString<RecordingMetadata>(metadataJson)
                metadata = metadata.copy(
                    name = recordingMeta.name ?: metadata.name,
                    createdAt = recordingMeta.createdAt ?: metadata.createdAt,
                    totalDistance = recordingMeta.totalDistance ?: metadata.totalDistance,
                    totalElevationGain = recordingMeta.totalElevationGain ?: metadata.totalElevationGain,
                    totalElevationLoss = recordingMeta.totalElevationLoss ?: metadata.totalElevationLoss,
                    minElevation = recordingMeta.minElevation ?: metadata.minElevation,
                    maxElevation = recordingMeta.maxElevation ?: metadata.maxElevation,
                    pointCount = recordingMeta.pointCount ?: metadata.pointCount
                )
            } catch (e: Exception) {
                Log.w("MainViewModel", "Failed to parse recording metadata: ${e.message}")
            }
        }

        return parsedTrip.copy(metadata = metadata)
    }

    private fun createUniqueTripFileName(baseName: String): String {
        val existing = _trips.value.map { it.metadata.fileName }.toSet()
        if (!existing.contains(baseName)) return baseName

        var index = 1
        var candidate = "${baseName}_$index"
        while (existing.contains(candidate)) {
            index++
            candidate = "${baseName}_$index"
        }
        return candidate
    }

    /**
     * Add an imported trip with a custom name
     * Downloads tiles and saves the trip to storage
     */
    fun addImportedTrip(trip: Trip, customName: String) {
        viewModelScope.launch {
            try {
                _isLoading.value = true
                _errorMessage.value = null

                // Update trip metadata with custom name
                val sanitizedFileName = "$customName.gpx".replace(Regex("[^a-zA-Z0-9._-]"), "_")
                val updatedTrip = trip.copy(
                    metadata = trip.metadata.copy(
                        fileName = sanitizedFileName,
                        name = customName
                    )
                )

                // Save trip
                withContext(Dispatchers.IO) {
                    storageManager.saveTrip(updatedTrip)
                }

                // Add trip to list immediately (so it appears while downloading)
                _trips.value = withContext(Dispatchers.IO) {
                    storageManager.loadAllTrips()
                }
                updateStorageStats()

                // Download tiles using foreground service
                Log.d("MainViewModel", "Starting tile download service for $sanitizedFileName")
                val bbox = updatedTrip.metadata.boundingBox

                // Start foreground service for download
                val intent = Intent(getApplication(), BleService::class.java)
                getApplication<Application>().startForegroundService(intent)

                // Bind to service if not already bound
                if (!serviceBound) {
                    getApplication<Application>().bindService(intent, serviceConnection, Context.BIND_AUTO_CREATE)
                }

                // Wait a bit for service to bind
                kotlinx.coroutines.delay(500)

                // Start download through service
                bleService?.downloadTilesForTrip(
                    tripFileName = sanitizedFileName,
                    minLat = bbox.minLat,
                    maxLat = bbox.maxLat,
                    minLon = bbox.minLon,
                    maxLon = bbox.maxLon,
                    tileDownloader = tileDownloader,
                    tilePreprocessor = tilePreprocessor,
                    onError = { message ->
                        _errorMessage.value = "Tile download issue: $message"
                    }
                ) { success, downloadedCount ->
                    if (success) {
                        Log.d("MainViewModel", "Tile download complete for $sanitizedFileName: $downloadedCount tiles")
                        println("Trip imported! Downloaded $downloadedCount new tiles")
                    } else {
                        Log.e("MainViewModel", "Tile download failed for $sanitizedFileName")
                        if (_errorMessage.value == null) {
                            _errorMessage.value = "Failed to download tiles"
                        }
                    }
                }

            } catch (e: Exception) {
                e.printStackTrace()
                _errorMessage.value = "Failed to add trip: ${e.message}"
            } finally {
                _isLoading.value = false
            }
        }
    }

    /**
     * Import a GPX file from URI
     */
    fun importGpx(uri: Uri, fileName: String) {
        viewModelScope.launch {
            try {
                _isLoading.value = true
                _errorMessage.value = null

                // Parse GPX
                val trip = withContext(Dispatchers.IO) {
                    val inputStream = getApplication<Application>().contentResolver.openInputStream(uri)
                        ?: throw IllegalArgumentException("Cannot open file")
                    inputStream.use { gpxParser.parseGpx(it, fileName) }
                }

                // IMPORTANT: Use the sanitized fileName from the parsed trip metadata as the key
                // because GpxParser sanitizes the fileName (removes .gpx, replaces special chars)
                val sanitizedFileName = trip.metadata.fileName

                // Save trip
                withContext(Dispatchers.IO) {
                    storageManager.saveTrip(trip)
                }

                // Add trip to list immediately (so it appears while downloading)
                _trips.value = withContext(Dispatchers.IO) {
                    storageManager.loadAllTrips()
                }
                updateStorageStats()

                // Download tiles using foreground service
                Log.d("MainViewModel", "Starting tile download service for $sanitizedFileName")
                val bbox = trip.metadata.boundingBox

                // Start foreground service for download
                val intent = Intent(getApplication(), BleService::class.java)
                getApplication<Application>().startForegroundService(intent)

                // Bind to service if not already bound
                if (!serviceBound) {
                    getApplication<Application>().bindService(intent, serviceConnection, Context.BIND_AUTO_CREATE)
                }

                // Wait a bit for service to bind
                kotlinx.coroutines.delay(500)

                // Start download through service
                bleService?.downloadTilesForTrip(
                    tripFileName = sanitizedFileName,
                    minLat = bbox.minLat,
                    maxLat = bbox.maxLat,
                    minLon = bbox.minLon,
                    maxLon = bbox.maxLon,
                    tileDownloader = tileDownloader,
                    tilePreprocessor = tilePreprocessor,
                    onError = { message ->
                        _errorMessage.value = "Tile download issue: $message"
                    }
                ) { success, downloadedCount ->
                    if (success) {
                        Log.d("MainViewModel", "Tile download complete for $sanitizedFileName: $downloadedCount tiles")
                        println("Trip imported! Downloaded $downloadedCount new tiles")
                    } else {
                        Log.e("MainViewModel", "Tile download failed for $sanitizedFileName")
                        if (_errorMessage.value == null) {
                            _errorMessage.value = "Failed to download tiles"
                        }
                    }
                }

            } catch (e: Exception) {
                e.printStackTrace()
                _errorMessage.value = "Failed to import GPX: ${e.message}"
            } finally {
                _isLoading.value = false
            }
        }
    }

    /**
     * Delete a trip
     */
    fun deleteTrip(fileName: String) {
        viewModelScope.launch {
            storageManager.deleteTrip(fileName)
            loadTrips()
        }
    }

    /**
     * Send a trip to ESP32S3 via BLE
     */
    fun sendTripToEsp(trip: Trip) {
        viewModelScope.launch {
            try {
                _isLoading.value = true

                // Send trip data
                val metadataJson = Json.encodeToString(trip.metadata)
                val success = bleManager?.sendTrip(
                    trip.metadata.fileName,
                    trip.gpxContent,
                    metadataJson
                ) ?: false

                if (success) {
                    println("Trip sent successfully!")
                } else {
                    _errorMessage.value = "Failed to send trip to ESP32S3"
                }
            } catch (e: Exception) {
                e.printStackTrace()
                _errorMessage.value = "Error sending trip: ${e.message}"
            } finally {
                _isLoading.value = false
            }
        }
    }

    /**
     * Send all tiles for a trip to ESP32S3
     */
    fun sendTilesToEsp(trip: Trip) {
        viewModelScope.launch {
            try {
                _isLoading.value = true

                val zoomLevels = listOf(18, 17, 16, 15, 14, 13, 12, 11, 10, 9)
                val bbox = trip.metadata.boundingBox

                // Calculate required tiles
                val tiles = tilePreprocessor.calculateRequiredTiles(
                    bbox.minLat, bbox.maxLat, bbox.minLon, bbox.maxLon, zoomLevels, padding = 2
                )

                val tilesToSend = if (tiles.isNotEmpty()) {
                    bleManager?.filterMissingTiles(tiles) ?: tiles
                } else {
                    tiles
                }
                // Send tiles (processed on-the-fly)
                val sentCount = bleManager?.sendTiles(tilesToSend, storageManager, tilePreprocessor) ?: 0

                println("Sent $sentCount tiles to ESP32S3")
            } catch (e: Exception) {
                e.printStackTrace()
                _errorMessage.value = "Error sending tiles: ${e.message}"
            } finally {
                _isLoading.value = false
            }
        }
    }

    /**
     * Send trip and tiles to ESP32S3 sequentially via foreground service.
     * First sends the trip (GPX + metadata), then sends all tiles.
     */
    fun sendTripAndTilesToEsp(trip: Trip) {
        viewModelScope.launch {
            try {
                _errorMessage.value = null

                // Ensure service is running and connected
                if (!serviceBound || bleService == null) {
                    _errorMessage.value = "BLE service not connected. Please connect first."
                    return@launch
                }

                if (!isBleConnectedForCommands) {
                    _errorMessage.value = "Not connected to ESP. Please connect first."
                    return@launch
                }

                // Check if trip already exists on ESP32
                val tripFileName = trip.metadata.fileName
                if (_tripsOnEsp.value.contains(tripFileName)) {
                    Log.w("MainViewModel", "Trip '$tripFileName' already exists on ESP32, skipping transfer")
                    _errorMessage.value = "Trip already exists on ESP32"
                    return@launch
                }

                // Calculate required tiles
                val zoomLevels = listOf(18, 17, 16, 15, 14, 13, 12, 11, 10, 9)
                val bbox = trip.metadata.boundingBox
                val tiles = tilePreprocessor.calculateRequiredTiles(
                    bbox.minLat, bbox.maxLat, bbox.minLon, bbox.maxLon, zoomLevels, padding = 2
                )

                // Send via service (tiles will be processed on-the-fly)
                bleService?.sendTripAndTiles(trip, tiles, storageManager, tilePreprocessor) { success, error ->
                    if (success) {
                        println("Transfer completed successfully!")
                        // Add trip to the list of trips on ESP32
                        _tripsOnEsp.value = _tripsOnEsp.value + tripFileName
                        Log.d("MainViewModel", "Added trip to ESP32 list: $tripFileName")
                    } else {
                        _errorMessage.value = error ?: "Transfer failed"
                    }
                }
            } catch (e: Exception) {
                e.printStackTrace()
                _errorMessage.value = "Error sending data: ${e.message}"
            }
        }
    }

    /**
     * Start BLE foreground service and connect to ESP
     */
    fun startBleService() {
        val currentState = _serviceState.value

        // Only prevent starting if actively transferring or already connected
        if (currentState is BleService.ServiceState.Transferring) {
            Log.w("MainViewModel", "Cannot start - currently transferring")
            return
        }

        if (currentState is BleService.ServiceState.Connected || currentState is BleService.ServiceState.Downloading) {
            Log.w("MainViewModel", "Already connected")
            return
        }

        // If currently connecting, allow retry (in case it's stuck)
        if (currentState is BleService.ServiceState.Connecting) {
            Log.w("MainViewModel", "Already connecting - forcing restart")
            // Force stop and restart
            stopBleService()
            // Small delay to ensure cleanup
            viewModelScope.launch {
                kotlinx.coroutines.delay(500)
                startBleService()
            }
            return
        }

        // Check if Bluetooth adapter is available
        if (bluetoothAdapter == null) {
            Log.e("MainViewModel", "Bluetooth adapter not available on this device")
            _errorMessage.value = "Bluetooth is not available on this device."
            return
        }

        // Check if Bluetooth is enabled
        if (!bluetoothAdapter.isEnabled) {
            Log.w("MainViewModel", "Bluetooth is OFF, prompting user to enable")
            pendingConnection = true
            promptEnableBluetooth()
            return
        }

        // Clean up any stale service binding
        if (serviceBound) {
            Log.w("MainViewModel", "Cleaning up stale service binding before restart")
            try {
                getApplication<Application>().unbindService(serviceConnection)
            } catch (e: Exception) {
                Log.e("MainViewModel", "Error cleaning up stale binding", e)
            }
            serviceBound = false
            bleService = null
            bleManager = null
        }

        // Bluetooth is enabled, proceed with connection
        Log.d("MainViewModel", "Starting BLE service... (current state: $currentState)")
        val intent = Intent(getApplication(), BleService::class.java)

        // Always call startForegroundService to trigger onStartCommand (even if service exists)
        getApplication<Application>().startForegroundService(intent)

        // Bind to service to get reference
        getApplication<Application>().bindService(intent, serviceConnection, Context.BIND_AUTO_CREATE)
    }

    /**
     * Prompt user to enable Bluetooth with a system dialog
     */
    private fun promptEnableBluetooth() {
        // Use ACTION_REQUEST_ENABLE which shows a system dialog on all Android versions
        // This requires BLUETOOTH_CONNECT permission on Android 12+, which we already have
        val intent = Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE).apply {
            flags = Intent.FLAG_ACTIVITY_NEW_TASK
        }
        try {
            getApplication<Application>().startActivity(intent)
            Log.d("MainViewModel", "Bluetooth enable dialog shown")
        } catch (e: Exception) {
            Log.e("MainViewModel", "Failed to show Bluetooth enable dialog", e)
            // Fallback: open Bluetooth settings if dialog fails
            val settingsIntent = Intent(Settings.ACTION_BLUETOOTH_SETTINGS).apply {
                flags = Intent.FLAG_ACTIVITY_NEW_TASK
            }
            getApplication<Application>().startActivity(settingsIntent)
        }
    }

    /**
     * Cancel ongoing transfer but stay connected
     */
    fun cancelTransfer() {
        bleService?.cancelTransfer()
    }

    /**
     * Cancel ongoing download
     */
    fun cancelDownload() {
        bleService?.cancelDownload()
    }

    /**
     * Stop BLE service and disconnect
     */
    fun stopBleService() {
        Log.d("MainViewModel", "Stopping BLE service...")

        // Always try to disconnect and clean up, regardless of state
        bleService?.disconnect()

        if (serviceBound) {
            try {
                getApplication<Application>().unbindService(serviceConnection)
            } catch (e: Exception) {
                Log.e("MainViewModel", "Error unbinding service", e)
            }
            serviceBound = false
        }

        bleService = null
        bleManager = null

        // Always clear state to ensure UI is synchronized
        _serviceState.value = BleService.ServiceState.Idle
        _transferState.value = null
        _downloadState.value = null
        _tripsOnEsp.value = emptySet()
        _activeTripOnEsp.value = null
        _isNavigateHomeActive.value = false
        _navigateHomeTrip.value = null
        _downloadingTrips.value = emptyMap()
        activeDownloadTripFileName = null
    }

    /**
     * Start a trip on the ESP32
     */
    fun startTripOnEsp(tripFileName: String) {
        viewModelScope.launch {
            try {
                if (!isBleConnectedForCommands) {
                    _errorMessage.value = "Not connected to ESP. Please connect first."
                    return@launch
                }

                // Prevent starting other trips when Navigate Home is active
                if (_isNavigateHomeActive.value) {
                    _errorMessage.value = "Cannot start trip while Navigate Home is active"
                    Log.w("MainViewModel", "Blocked trip start attempt while Navigate Home is active")
                    return@launch
                }

                val success = bleManager?.sendStartTrip(tripFileName) ?: false
                if (success) {
                    Log.d("MainViewModel", "Start trip command sent successfully: $tripFileName")
                } else {
                    _errorMessage.value = "Failed to start trip on ESP32"
                }
            } catch (e: Exception) {
                Log.e("MainViewModel", "Error starting trip on ESP32", e)
                _errorMessage.value = "Error starting trip: ${e.message}"
            }
        }
    }

    /**
     * Stop the active trip on the ESP32
     */
    fun stopTripOnEsp() {
        viewModelScope.launch {
            try {
                if (!isBleConnectedForCommands) {
                    _errorMessage.value = "Not connected to ESP. Please connect first."
                    return@launch
                }

                val success = bleManager?.sendStopTrip() ?: false
                if (success) {
                    Log.d("MainViewModel", "Stop trip command sent successfully")
                } else {
                    _errorMessage.value = "Failed to stop trip on ESP32"
                }
            } catch (e: Exception) {
                Log.e("MainViewModel", "Error stopping trip on ESP32", e)
                _errorMessage.value = "Error stopping trip: ${e.message}"
            }
        }
    }

    /**
     * Enable periodic ESP32 device status updates (when app is in foreground)
     */
    fun enablePeriodicStatusUpdates() {
        viewModelScope.launch {
            try {
                if (isBleConnectedForCommands) {
                    val success = bleManager?.enablePeriodicStatusUpdates() ?: false
                    if (success) {
                        Log.d("MainViewModel", "Enabled periodic status updates on ESP32")
                    } else {
                        Log.w("MainViewModel", "Failed to enable periodic status updates")
                    }
                }
            } catch (e: Exception) {
                Log.e("MainViewModel", "Error enabling periodic status updates", e)
            }
        }
    }

    /**
     * Disable periodic ESP32 device status updates (when app is in background)
     */
    fun disablePeriodicStatusUpdates() {
        viewModelScope.launch {
            try {
                if (isBleConnectedForCommands) {
                    val success = bleManager?.disablePeriodicStatusUpdates() ?: false
                    if (success) {
                        Log.d("MainViewModel", "Disabled periodic status updates on ESP32")
                    } else {
                        Log.w("MainViewModel", "Failed to disable periodic status updates")
                    }
                }
            } catch (e: Exception) {
                Log.e("MainViewModel", "Error disabling periodic status updates", e)
            }
        }
    }

    /**
     * Load Navigate Home route from app side
     * Step 1: Fetches GPS from ESP32 and gets route from API, prepares trip
     * Step 2: User must explicitly start the trip with startNavigateHomeTrip()
     */
    fun loadNavigateHomeRoute() {
        viewModelScope.launch {
            try {
                if (!isBleConnectedForCommands) {
                    _navigateHomeError.value = "Not connected to ESP. Please connect first."
                    _isLoadingNavigateHome.value = false
                    navigateHomeRequestTimeoutJob?.cancel()
                    navigateHomeRequestTimeoutJob = null
                    return@launch
                }

                // Prevent loading if another trip is active
                if (_activeTripOnEsp.value != null && _activeTripOnEsp.value != Constants.NAVIGATE_HOME_TRIP_NAME) {
                    _navigateHomeError.value = "Please stop the current trip first"
                    _isLoadingNavigateHome.value = false
                    navigateHomeRequestTimeoutJob?.cancel()
                    navigateHomeRequestTimeoutJob = null
                    Log.w("MainViewModel", "Blocked Navigate Home request while another trip is active: ${_activeTripOnEsp.value}")
                    return@launch
                }

                _isLoadingNavigateHome.value = true
                _navigateHomeError.value = null
                navigateHomeRequestTimeoutJob?.cancel()
                navigateHomeRequestTimeoutJob = null

                Log.d("MainViewModel", "Loading Navigate Home route...")

                // Send request to ESP32 to get GPS location
                val success = bleManager?.sendNavigateHomeRequest() ?: false
                if (!success) {
                    _navigateHomeError.value = "Failed to request GPS from device"
                    _isLoadingNavigateHome.value = false
                    return@launch
                }

                Log.d("MainViewModel", "Navigate Home GPS request sent, waiting for response...")
                navigateHomeRequestTimeoutJob = viewModelScope.launch {
                    kotlinx.coroutines.delay(20000)
                    if (_isLoadingNavigateHome.value && _navigateHomeTrip.value == null) {
                        val message = "No GPS response from device"
                        _navigateHomeError.value = "Navigate home failed: $message"
                        _isLoadingNavigateHome.value = false
                        bleManager?.sendNavigateHomeError(message)
                    }
                }
                // The response will be handled by handleNavigateHomeRequest callback
                // which will create the trip and store it in _navigateHomeTrip

            } catch (e: Exception) {
                Log.e("MainViewModel", "Error loading Navigate Home route", e)
                _navigateHomeError.value = "Error: ${e.message}"
                _isLoadingNavigateHome.value = false
            }
        }
    }

    /**
     * Start Navigate Home trip after route is loaded
     * Sends the prepared Navigate Home trip to ESP32 and starts navigation
     */
    fun startNavigateHomeTrip() {
        viewModelScope.launch {
            try {
                val trip = _navigateHomeTrip.value
                if (trip == null) {
                    _navigateHomeError.value = "Please load the route first"
                    return@launch
                }

                sendNavigateHomeTripToEsp(trip, notifyEspOnError = false)

            } catch (e: Exception) {
                Log.e("MainViewModel", "Error starting Navigate Home trip", e)
                _navigateHomeError.value = "Error: ${e.message}"
            }
        }
    }

    private fun sendNavigateHomeTripToEsp(trip: Trip, notifyEspOnError: Boolean) {
        if (!isBleConnectedForCommands) {
            val message = "Not connected to ESP. Please connect first."
            _navigateHomeError.value = message
            if (notifyEspOnError) {
                viewModelScope.launch {
                    bleManager?.sendNavigateHomeError(message)
                }
            }
            return
        }

        Log.d("MainViewModel", "Sending Navigate Home route to ESP32...")

        val zoomLevels = listOf(18, 17, 16, 15, 14, 13, 12, 11, 10, 9)
        val bbox = trip.metadata.boundingBox
        val requiredTiles = tilePreprocessor.calculateRequiredTiles(
            minLat = bbox.minLat,
            maxLat = bbox.maxLat,
            minLon = bbox.minLon,
            maxLon = bbox.maxLon,
            zoomLevels = zoomLevels,
            padding = 2
        )

        val sender = bleService
        if (sender == null) {
            val message = "BLE service unavailable"
            _navigateHomeError.value = message
            if (notifyEspOnError) {
                viewModelScope.launch {
                    bleManager?.sendNavigateHomeError(message)
                }
            }
            return
        }

        sender.sendTripAndTiles(trip, requiredTiles, storageManager, tilePreprocessor) { success, error ->
            if (success) {
                Log.d("MainViewModel", "Navigate Home route sent successfully to ESP32")
            } else {
                val message = error ?: "Failed to send route to device"
                Log.e("MainViewModel", "Failed to send Navigate Home route: $message")
                _navigateHomeError.value = message
                if (notifyEspOnError) {
                    viewModelScope.launch {
                        bleManager?.sendNavigateHomeError(message)
                    }
                }
            }
        }
    }

    /**
     * Update storage statistics
     */
    private fun updateStorageStats() {
        viewModelScope.launch {
            _storageStats.value = withContext(Dispatchers.IO) {
                storageManager.getStorageStats()
            }
        }
    }

    /**
     * Clear error message
     */
    fun clearError() {
        _errorMessage.value = null
    }

    fun clearNavigateHomeError() {
        _navigateHomeError.value = null
    }

    private fun hasInternetConnection(): Boolean {
        val status = deviceStatusManager.deviceStatus.value
        return status.wifiConnected || status.cellularType.isNotBlank()
    }

    /**
     * Handle weather request from ESP32
     * Fetches weather data and sends it back via BLE
     */
    private fun handleWeatherRequest(latitude: Double, longitude: Double) {
        viewModelScope.launch {
            try {
                Log.d("MainViewModel", "Weather request received: lat=$latitude, lon=$longitude")
                if (!hasInternetConnection()) {
                    val errorMsg = "No internet connection"
                    bleManager?.sendWeatherError(errorMsg)
                    _errorMessage.value = "Failed to fetch weather: $errorMsg"
                    return@launch
                }

                // Fetch weather data from API
                val result = weatherApiService.fetchWeather(latitude, longitude)

                result.onSuccess { weatherData ->
                    Log.d("MainViewModel", "Weather data fetched successfully for ${weatherData.location}")

                    // Convert WeatherCondition enum to code for ESP32
                    val conditionCode = weatherData.current.condition.code

                    // Prepare hourly data
                    val hourlyData = weatherData.hourly.map { hourly ->
                        BleManager.HourlyWeatherData(
                            hour = hourly.hour,
                            temp = hourly.temperature,
                            condition = hourly.condition.code,
                            precipChance = hourly.precipitationChance
                        )
                    }

                    // Send weather data to ESP32 with retry
                    var success = false
                    var attempts = 0
                    val maxAttempts = 3

                    while (!success && attempts < maxAttempts) {
                        attempts++
                        Log.d("MainViewModel", "Sending weather data to ESP32 (attempt $attempts/$maxAttempts)...")

                        success = bleManager?.sendWeather(
                            location = weatherData.location,
                            currentTemp = weatherData.current.temperature,
                            feelsLike = weatherData.current.feelsLike,
                            condition = conditionCode,
                            humidity = weatherData.current.humidity,
                            windSpeed = weatherData.current.windSpeed,
                            windDir = weatherData.current.windDirection,
                            pressure = weatherData.current.pressure,
                            precipChance = weatherData.current.precipitationChance,
                            sunrise = weatherData.current.sunrise,
                            sunset = weatherData.current.sunset,
                            hourlyData = hourlyData
                        ) ?: false

                        if (!success && attempts < maxAttempts) {
                            Log.w("MainViewModel", "Send failed, retrying in 500ms...")
                            kotlinx.coroutines.delay(500)
                        }
                    }

                    if (success) {
                        Log.d("MainViewModel", "Weather data sent successfully to ESP32")
                    } else {
                        Log.e("MainViewModel", "Failed to send weather data to ESP32 after $attempts attempts")
                        bleManager?.sendWeatherError("Failed to send to device")
                        _errorMessage.value = "Failed to send weather data to device"
                    }
                }

                result.onFailure { error ->
                    Log.e("MainViewModel", "Failed to fetch weather data", error)

                    val errorMsg = ApiErrorUtils.toUserMessage(error, "Weather service error")

                    bleManager?.sendWeatherError(errorMsg)
                    _errorMessage.value = "Failed to fetch weather: $errorMsg"
                }

            } catch (e: Exception) {
                Log.e("MainViewModel", "Error handling weather request", e)
                val message = ApiErrorUtils.toUserMessage(e, "Weather error")
                bleManager?.sendWeatherError(message)
                _errorMessage.value = "Weather error: $message"
            }
        }
    }

    /**
     * Handle radar request from ESP32
     * Fetches radar image, processes it, and sends it back via BLE
     */
    private fun handleRadarRequest(latitude: Double, longitude: Double, zoom: Int) {
        viewModelScope.launch {
            try {
                Log.d("MainViewModel", "Radar request received: lat=$latitude, lon=$longitude, zoom=$zoom")
                if (!hasInternetConnection()) {
                    val errorMsg = "No internet connection"
                    val stepMinutes = RadarApiService.UPDATE_STEP_MINUTES
                    val totalFrames = RadarApiService.RADAR_PAST_STEPS +
                        RadarApiService.RADAR_FUTURE_STEPS + 1
                    bleManager?.sendRadarError(errorMsg, stepMinutes, totalFrames)
                    return@launch
                }

                val pastSteps = RadarApiService.RADAR_PAST_STEPS
                val futureSteps = RadarApiService.RADAR_FUTURE_STEPS
                val stepMinutes = RadarApiService.UPDATE_STEP_MINUTES
                val totalFrames = pastSteps + futureSteps + 1

                val result = radarApiService.fetchCurrentRadarPng()

                result.onSuccess { current ->
                    val currentLocalMinutes = current.timestamp
                        .withZoneSameInstant(ZoneId.systemDefault())
                        .let { it.hour * 60 + it.minute }
                    val nowcastStepMinutes = RadarApiService.NOWCAST_STEP_MINUTES
                    val nowcastBase = radarApiService.alignToNowcastStep(current.timestamp)
                    val overlay = radarOverlayProcessor.buildOverlay(current.pngData, latitude, longitude, zoom)

                    val currentSent = sendRadarFrameWithRetries(
                        offsetSteps = 0,
                        stepMinutes = stepMinutes,
                        totalFrames = totalFrames,
                        overlay = overlay,
                        frameLocalMinutes = currentLocalMinutes,
                        nowcastStepMinutes = nowcastStepMinutes,
                        maxAttempts = 3
                    )

                    if (!currentSent) {
                        Log.e("MainViewModel", "Failed to send current radar frame after retries")
                        bleManager?.sendRadarError("Failed to send to device", stepMinutes, totalFrames)
                        return@onSuccess
                    }

                    for (step in 1..pastSteps) {
                        val offset = -step
                        val timestamp = current.timestamp.plusMinutes((offset * stepMinutes).toLong())
                        val frameResult = radarApiService.fetchRadarPngForTimestamp(timestamp)
                        if (frameResult.isFailure) {
                            Log.w("MainViewModel", "Radar past frame missing for offset $offset")
                            continue
                        }

                        val frameLocalMinutes = timestamp
                            .withZoneSameInstant(ZoneId.systemDefault())
                            .let { it.hour * 60 + it.minute }
                        val frameOverlay = radarOverlayProcessor.buildOverlay(
                            frameResult.getOrThrow(),
                            latitude,
                            longitude,
                            zoom
                        )

                        val frameSent = sendRadarFrameWithRetries(
                            offsetSteps = offset,
                            stepMinutes = stepMinutes,
                            totalFrames = totalFrames,
                            overlay = frameOverlay,
                            frameLocalMinutes = frameLocalMinutes,
                            nowcastStepMinutes = nowcastStepMinutes,
                            maxAttempts = 1
                        )

                        if (!frameSent) {
                            Log.w("MainViewModel", "Failed to send radar past frame offset $offset")
                        }
                    }

                    for (step in 1..futureSteps) {
                        val leadMinutes = step * nowcastStepMinutes
                        val frameResult = radarApiService.fetchNowcastRadarPng(
                            baseTimestamp = nowcastBase,
                            leadMinutes = leadMinutes
                        )
                        if (frameResult.isFailure) {
                            Log.w("MainViewModel", "Radar nowcast missing for +$leadMinutes minutes")
                            continue
                        }

                        val forecastTimestamp = nowcastBase.plusMinutes(leadMinutes.toLong())
                        val frameLocalMinutes = forecastTimestamp
                            .withZoneSameInstant(ZoneId.systemDefault())
                            .let { it.hour * 60 + it.minute }

                        val frameOverlay = radarOverlayProcessor.buildOverlay(
                            frameResult.getOrThrow(),
                            latitude,
                            longitude,
                            zoom
                        )

                        val frameSent = sendRadarFrameWithRetries(
                            offsetSteps = step,
                            stepMinutes = stepMinutes,
                            totalFrames = totalFrames,
                            overlay = frameOverlay,
                            frameLocalMinutes = frameLocalMinutes,
                            nowcastStepMinutes = nowcastStepMinutes,
                            maxAttempts = 1
                        )

                        if (!frameSent) {
                            Log.w("MainViewModel", "Failed to send radar nowcast offset $step")
                        }
                    }
                }

                result.onFailure { error ->
                    Log.e("MainViewModel", "Failed to fetch radar image", error)
                    val errorMsg = ApiErrorUtils.toUserMessage(error, "Radar service error")
                    bleManager?.sendRadarError(errorMsg, stepMinutes, totalFrames)
                }
            } catch (e: Exception) {
                Log.e("MainViewModel", "Error handling radar request", e)
                val stepMinutes = RadarApiService.UPDATE_STEP_MINUTES
                val totalFrames = RadarApiService.RADAR_PAST_STEPS +
                    RadarApiService.RADAR_FUTURE_STEPS + 1
                val message = ApiErrorUtils.toUserMessage(e, "Radar error")
                bleManager?.sendRadarError(message, stepMinutes, totalFrames)
            }
        }
    }

    private fun buildRadarOffsets(pastSteps: Int, futureSteps: Int): List<Int> {
        val offsets = mutableListOf(0)
        val maxStep = maxOf(pastSteps, futureSteps)
        for (step in 1..maxStep) {
            if (step <= pastSteps) offsets.add(-step)
            if (step <= futureSteps) offsets.add(step)
        }
        return offsets
    }

    private suspend fun sendRadarFrameWithRetries(
        offsetSteps: Int,
        stepMinutes: Int,
        totalFrames: Int,
        overlay: ByteArray,
        frameLocalMinutes: Int?,
        nowcastStepMinutes: Int?,
        maxAttempts: Int
    ): Boolean {
        var attempts = 0
        var success = false

        while (!success && attempts < maxAttempts) {
            attempts++
            Log.d("MainViewModel", "Sending radar frame $offsetSteps (attempt $attempts/$maxAttempts)...")
            success = bleManager?.sendRadarFrame(
                offsetSteps,
                stepMinutes,
                totalFrames,
                overlay,
                frameLocalMinutes,
                nowcastStepMinutes
            ) ?: false
            if (!success && attempts < maxAttempts) {
                Log.w("MainViewModel", "Radar frame $offsetSteps send failed, retrying in 500ms...")
                kotlinx.coroutines.delay(500)
            }
        }

        return success
    }

    /**
     * Handle navigate home request from ESP32
     * @param latitude Current GPS latitude from ESP32
     * @param longitude Current GPS longitude from ESP32
     */
    private fun handleNavigateHomeRequest(latitude: Double, longitude: Double) {
        viewModelScope.launch {
            try {
                Log.d("MainViewModel", "GPS location received from device: lat=$latitude, lon=$longitude")

                val routePlanningTarget = _locationRequestTarget.value
                val shouldHandleNavigateHome = _isLoadingNavigateHome.value || routePlanningTarget == null
                val deviceInitiatedRequest = !_isLoadingNavigateHome.value && routePlanningTarget == null

                // Validate GPS coordinates
                if (latitude == 0.0 && longitude == 0.0) {
                    Log.e("MainViewModel", "Invalid GPS coordinates from ESP32")

                    // Check if this is for route planning or navigate home
                    if (!shouldHandleNavigateHome && routePlanningTarget != null) {
                        _routePlanningError.value = "No GPS signal from device"
                        _locationRequestTarget.value = null
                    } else {
                        navigateHomeRequestTimeoutJob?.cancel()
                        navigateHomeRequestTimeoutJob = null
                        _navigateHomeError.value = "Navigate home failed: No GPS signal"
                        _isLoadingNavigateHome.value = false
                        bleManager?.sendNavigateHomeError("No GPS signal")
                    }
                    return@launch
                }

                // Check if this GPS request is for route planning
                if (!shouldHandleNavigateHome && routePlanningTarget != null) {
                    Log.d("MainViewModel", "Using device location for route planning (${routePlanningTarget.name})")

                    val location = com.example.kolomapa2.models.LocationPoint(
                        lat = latitude,
                        lon = longitude,
                        displayName = "Device Location",
                        isCurrentLocation = true
                    )

                    when (routePlanningTarget) {
                        com.example.kolomapa2.models.LocationTarget.START -> setStartLocation(location)
                        com.example.kolomapa2.models.LocationTarget.END -> setEndLocation(location)
                        com.example.kolomapa2.models.LocationTarget.MAP_CENTER -> {
                            _currentDeviceLocation.value = location
                        }
                    }

                    _locationRequestTarget.value = null
                    return@launch
                }

                // Otherwise, continue with Navigate Home logic
                Log.d("MainViewModel", "Processing Navigate Home request")
                navigateHomeRequestTimeoutJob?.cancel()
                navigateHomeRequestTimeoutJob = null
                _locationRequestTarget.value = null
                if (!_isLoadingNavigateHome.value) {
                    _isLoadingNavigateHome.value = true
                }
                _navigateHomeError.value = null

                // Get home coordinates from constants
                val homeLat = Constants.HOME_LATITUDE
                val homeLon = Constants.HOME_LONGITUDE

                Log.d("MainViewModel", "Routing to home: lat=$homeLat, lon=$homeLon")
                if (!hasInternetConnection()) {
                    _navigateHomeError.value = "Navigate home failed: No internet connection"
                    _isLoadingNavigateHome.value = false
                    bleManager?.sendNavigateHomeError("No internet connection")
                    return@launch
                }

                // Fetch route from routing service with timeout
                val routeResult = withTimeout(30000) {
                    routingService.getRoute(
                        startLat = latitude,
                        startLon = longitude,
                        endLat = homeLat,
                        endLon = homeLon,
                        routeName = "Navigate Home",
                        routeDescription = "Navigate Home"
                    )
                }

                routeResult.onSuccess { route ->
                    Log.d("MainViewModel", "Route fetched successfully, parsing GPX...")

                    // Parse GPX to get trip metadata (convert String to InputStream)
                    val trip = try {
                        gpxParser.parseGpx(route.gpxContent.byteInputStream(), Constants.NAVIGATE_HOME_TRIP_NAME)
                    } catch (e: Exception) {
                        Log.e("MainViewModel", "Failed to parse route GPX", e)
                        _navigateHomeError.value = "Navigate home failed: Invalid route data"
                        _isLoadingNavigateHome.value = false
                        bleManager?.sendNavigateHomeError("Invalid route data")
                        return@launch
                    }

                    Log.d("MainViewModel", "Route parsed: ${trip.metadata.totalDistance}m, ${trip.metadata.pointCount} points")
                    if (route.elevationHadError) {
                        val missingInfo = if (route.elevationMissing > 0 && route.elevationTotal > 0) {
                            " (${route.elevationMissing}/${route.elevationTotal} points)"
                        } else {
                            ""
                        }
                        _navigateHomeError.value = "Elevation data unavailable$missingInfo"
                    }

                    // Validate route is not empty
                    if (trip.metadata.pointCount == 0) {
                        Log.e("MainViewModel", "Route has no points")
                        _navigateHomeError.value = "Navigate home failed: Empty route"
                        _isLoadingNavigateHome.value = false
                        bleManager?.sendNavigateHomeError("Empty route")
                        return@launch
                    }

                    // Download tiles for the route (only downloads missing tiles)
                    Log.d("MainViewModel", "Downloading tiles for Navigate Home route...")
                    val bbox = trip.metadata.boundingBox
                    val downloadResult = try {
                        tileDownloader.downloadTilesForTrip(
                            minLat = bbox.minLat,
                            maxLat = bbox.maxLat,
                            minLon = bbox.minLon,
                            maxLon = bbox.maxLon,
                            tilePreprocessor = tilePreprocessor
                        ) { current, total ->
                            Log.d("MainViewModel", "Download progress: $current/$total")
                        }
                    } catch (e: Exception) {
                        Log.e("MainViewModel", "Failed to download tiles", e)
                        TileDownloader.TileDownloadBatchResult(
                            downloadedCount = 0,
                            failedCount = 1,
                            firstErrorMessage = ApiErrorUtils.toUserMessage(e, "Failed to download tiles")
                        )
                    }

                    if (downloadResult.failedCount > 0) {
                        val message = downloadResult.firstErrorMessage ?: "Some tiles failed to download"
                        val tileError = "Map tiles unavailable: $message"
                        _navigateHomeError.value = if (_navigateHomeError.value.isNullOrBlank()) {
                            tileError
                        } else {
                            "${_navigateHomeError.value} | $tileError"
                        }
                    }

                    Log.d("MainViewModel", "Downloaded ${downloadResult.downloadedCount} new tiles, route ready!")

                    // Store the trip for later sending when user confirms
                    _navigateHomeTrip.value = trip
                    _isLoadingNavigateHome.value = false

                    Log.d("MainViewModel", "Navigate Home route loaded and ready to start")

                    if (deviceInitiatedRequest) {
                        sendNavigateHomeTripToEsp(trip, notifyEspOnError = true)
                    }

                }

                routeResult.onFailure { error ->
                    Log.e("MainViewModel", "Failed to fetch route", error)

                    val errorMsg = ApiErrorUtils.toUserMessage(error, "Routing failed")

                    _navigateHomeError.value = "Failed to get route: $errorMsg"
                    _isLoadingNavigateHome.value = false

                    // Send error to ESP32
                    bleManager?.sendNavigateHomeError(errorMsg)
                }

            } catch (e: TimeoutCancellationException) {
                Log.e("MainViewModel", "Navigate home request timed out", e)
                _navigateHomeError.value = "Navigate home timed out"
                _isLoadingNavigateHome.value = false
                bleManager?.sendNavigateHomeError("Request timed out")
            } catch (e: Exception) {
                Log.e("MainViewModel", "Error handling navigate home request", e)
                val message = ApiErrorUtils.toUserMessage(e, "Navigate home error")
                _navigateHomeError.value = "Navigate home error: $message"
                _isLoadingNavigateHome.value = false
                bleManager?.sendNavigateHomeError(message)
            }
        }
    }

    /**
     * Check if notification access is enabled
     */
    fun checkNotificationAccess() {
        val enabledListeners = NotificationManagerCompat.getEnabledListenerPackages(getApplication())
        val isEnabled = enabledListeners.contains(getApplication<Application>().packageName)
        val previousState = _notificationAccessEnabled.value

        // Log state changes (important for debugging sideloaded app permission issues)
        if (previousState != isEnabled) {
            Log.w("MainViewModel", "⚠️ Notification access changed: $previousState -> $isEnabled")
            if (!isEnabled) {
                Log.e("MainViewModel", "❌ Notification access was REVOKED! (Common for sideloaded apps)")
                // Automatically disable notification sync if access is lost
                if (_notificationSyncEnabled.value) {
                    Log.w("MainViewModel", "Auto-disabling notification sync due to lost access")
                    setNotificationSyncEnabled(false)
                }
            } else {
                Log.i("MainViewModel", "✓ Notification access was GRANTED!")
            }
        } else {
            Log.d("MainViewModel", "Notification access check: $isEnabled (no change)")
        }

        _notificationAccessEnabled.value = isEnabled
    }

    /**
     * Open notification access settings
     */
    fun openNotificationAccessSettings() {
        val intent = Intent(Settings.ACTION_NOTIFICATION_LISTENER_SETTINGS).apply {
            flags = Intent.FLAG_ACTIVITY_NEW_TASK
        }
        getApplication<Application>().startActivity(intent)
    }

    /**
     * Toggle notification sync on/off
     */
    fun setNotificationSyncEnabled(enabled: Boolean) {
        _notificationSyncEnabled.value = enabled
        getApplication<Application>()
            .getSharedPreferences("kolomapa2_prefs", Context.MODE_PRIVATE)
            .edit()
            .putBoolean("notification_sync_enabled", enabled)
            .apply()
        Log.d("MainViewModel", "Notification sync ${if (enabled) "enabled" else "disabled"}")
    }

    /**
     * Set up callbacks for NotificationListener service
     */
    private fun setupNotificationListenerCallbacks() {
        NotificationListener.onNotificationReceived = { notificationData ->
            viewModelScope.launch {
                try {
                    Log.d("MainViewModel", "Notification received: ${notificationData.appName} - ${notificationData.title}")

                    // Only send if notification sync is enabled and BLE is connected
                    if (_notificationSyncEnabled.value && isBleConnectedForCommands) {
                        bleManager?.sendNotification(
                            notificationId = notificationData.id,
                            appName = notificationData.appName,
                            title = notificationData.title,
                            text = notificationData.text,
                            iconData = notificationData.iconData
                        )
                    }
                } catch (e: Exception) {
                    Log.e("MainViewModel", "Error sending notification to ESP32", e)
                }
            }
        }

        NotificationListener.onNotificationRemoved = { notificationId ->
            viewModelScope.launch {
                try {
                    Log.d("MainViewModel", "Notification removed on phone: ID=$notificationId")

                    // Send dismissal to ESP32 if notification sync is enabled and connected
                    if (_notificationSyncEnabled.value && isBleConnectedForCommands) {
                        bleManager?.sendNotificationDismissal(notificationId)
                    }
                } catch (e: Exception) {
                    Log.e("MainViewModel", "Error sending notification dismissal to ESP32", e)
                }
            }
        }
    }

    /**
     * Handle notification dismissal from ESP32
     * Dismiss the notification on the phone
     */
    private fun handleNotificationDismissalFromEsp(notificationId: Int) {
        try {
            Log.d("MainViewModel", "Dismissing notification on phone: ID=$notificationId")
            NotificationListener.getInstance()?.dismissNotification(notificationId)
        } catch (e: Exception) {
            Log.e("MainViewModel", "Error dismissing notification on phone", e)
        }
    }

    /**
     * Start observing device status changes and push updates to the ESP32.
     */
    private fun startDeviceStatusChangeObserver() {
        if (deviceStatusObserverJob != null) {
            return
        }

        deviceStatusObserverJob = viewModelScope.launch {
            deviceStatusManager.deviceStatus.collect { status ->
                if (ignoreDeviceStatusUpdates.get()) return@collect
                if (!isBleConnectedForCommands) return@collect
                if (!shouldSendDeviceStatusUpdate(status, lastSentDeviceStatus)) return@collect
                sendDeviceStatusToEsp(status, includeSignalStrength = false)
            }
        }
    }

    private fun shouldSendDeviceStatusUpdate(
        status: DeviceStatusManager.DeviceStatus,
        lastSent: DeviceStatusManager.DeviceStatus?
    ): Boolean {
        if (lastSent == null) {
            return true
        }

        return status.musicPlaying != lastSent.musicPlaying ||
            status.songTitle != lastSent.songTitle ||
            status.songArtist != lastSent.songArtist ||
            status.phoneBatteryPercent != lastSent.phoneBatteryPercent ||
            status.wifiConnected != lastSent.wifiConnected ||
            status.wifiSsid != lastSent.wifiSsid ||
            status.cellularType != lastSent.cellularType
    }

    private fun applyRssiPolicy(
        status: DeviceStatusManager.DeviceStatus,
        includeSignalStrength: Boolean
    ): DeviceStatusManager.DeviceStatus {
        if (includeSignalStrength) {
            return status
        }

        val lastSent = lastSentDeviceStatus
        return status.copy(
            wifiSignalStrength = lastSent?.wifiSignalStrength ?: 0,
            cellularSignalStrength = lastSent?.cellularSignalStrength ?: 0
        )
    }

    private suspend fun sendDeviceStatusToEsp(
        status: DeviceStatusManager.DeviceStatus,
        includeSignalStrength: Boolean,
        force: Boolean = false
    ) {
        deviceStatusSendMutex.withLock {
            if (!isBleConnectedForCommands) return

            val statusToSend = applyRssiPolicy(status, includeSignalStrength)
            if (!force && !shouldSendDeviceStatusUpdate(statusToSend, lastSentDeviceStatus)) {
                return
            }

            try {
                Log.d("MainViewModel", "Sending device status to ESP32:")
                Log.d("MainViewModel", "  Music: ${if (statusToSend.musicPlaying) "Playing" else "Paused"}")
                Log.d("MainViewModel", "  Song: ${statusToSend.songTitle} - ${statusToSend.songArtist}")
                Log.d("MainViewModel", "  Battery: ${statusToSend.phoneBatteryPercent}%")
                Log.d("MainViewModel", "  WiFi: ${if (statusToSend.wifiConnected) "Connected (${statusToSend.wifiSsid}, ${statusToSend.wifiSignalStrength}%)" else "Disconnected"}")
                Log.d("MainViewModel", "  Cellular: ${statusToSend.cellularType} (${statusToSend.cellularSignalStrength}%)")
                Log.d("MainViewModel", "  Notification Sync: ${if (_notificationSyncEnabled.value) "Enabled" else "Disabled"}")

                val success = bleManager?.sendDeviceStatus(
                    musicPlaying = statusToSend.musicPlaying,
                    songTitle = statusToSend.songTitle,
                    songArtist = statusToSend.songArtist,
                    phoneBatteryPercent = statusToSend.phoneBatteryPercent,
                    phoneCharging = statusToSend.phoneCharging,
                    wifiConnected = statusToSend.wifiConnected,
                    wifiSsid = statusToSend.wifiSsid,
                    wifiSignalStrength = statusToSend.wifiSignalStrength,
                    cellularSignalStrength = statusToSend.cellularSignalStrength,
                    cellularType = statusToSend.cellularType,
                    // Only report as enabled if BOTH user enabled it AND permissions are granted
                    notificationSyncEnabled = _notificationSyncEnabled.value && _notificationAccessEnabled.value
                ) ?: false

                if (success) {
                    lastSentDeviceStatus = statusToSend
                } else {
                    Log.w("MainViewModel", "Failed to send device status to ESP32")
                }
            } catch (e: Exception) {
                Log.e("MainViewModel", "Error sending device status to ESP32", e)
            }
        }
    }

    private suspend fun refreshAndSendDeviceStatus(
        includeSignalStrength: Boolean,
        force: Boolean = false
    ) {
        ignoreDeviceStatusUpdates.set(true)
        try {
            val status = deviceStatusManager.collectDeviceStatus()
            sendDeviceStatusToEsp(status, includeSignalStrength, force)
        } finally {
            ignoreDeviceStatusUpdates.set(false)
        }
    }

    /**
     * Toggle notification sync from ESP32 command.
     */
    private fun toggleNotificationSyncFromEsp() {
        val newValue = !_notificationSyncEnabled.value
        Log.d("MainViewModel", "Toggling notification sync from ESP32: $newValue")
        setNotificationSyncEnabled(newValue)

        // Send updated device status
        viewModelScope.launch {
            sendDeviceStatusToEsp(
                status = deviceStatusManager.deviceStatus.value,
                includeSignalStrength = false,
                force = true
            )
        }
    }

    // ============ Route Planning Methods ============

    fun setStartLocation(location: com.example.kolomapa2.models.LocationPoint) {
        _startLocation.value = location
        _startSuggestions.value = emptyList()
    }

    fun setEndLocation(location: com.example.kolomapa2.models.LocationPoint) {
        _endLocation.value = location
        _endSuggestions.value = emptyList()
    }

    fun swapLocations() {
        android.util.Log.d("SWAP_DEBUG", "swapLocations() called")
        android.util.Log.d("SWAP_DEBUG", "Before: start=${_startLocation.value?.displayName}, end=${_endLocation.value?.displayName}")
        val temp = _startLocation.value
        _startLocation.value = _endLocation.value
        _endLocation.value = temp
        android.util.Log.d("SWAP_DEBUG", "After: start=${_startLocation.value?.displayName}, end=${_endLocation.value?.displayName}")
    }

    fun searchStartLocation(query: String) {
        viewModelScope.launch {
            try {
                if (query.isBlank()) {
                    _startSuggestions.value = emptyList()
                    return@launch
                }
                if (!hasInternetConnection()) {
                    _routePlanningError.value = "No internet connection"
                    _startSuggestions.value = emptyList()
                    return@launch
                }
                val result = geocodingService.searchLocations(query)
                result.onSuccess { suggestions ->
                    _startSuggestions.value = suggestions
                    _routePlanningError.value = null
                }
                result.onFailure { error ->
                    Log.e("MainViewModel", "Error searching start location", error)
                    val message = ApiErrorUtils.toUserMessage(error, "Location search failed")
                    _routePlanningError.value = "Location search failed: $message"
                    _startSuggestions.value = emptyList()
                }
            } catch (e: Exception) {
                Log.e("MainViewModel", "Error searching start location", e)
            }
        }
    }

    fun searchEndLocation(query: String) {
        viewModelScope.launch {
            try {
                if (query.isBlank()) {
                    _endSuggestions.value = emptyList()
                    return@launch
                }
                if (!hasInternetConnection()) {
                    _routePlanningError.value = "No internet connection"
                    _endSuggestions.value = emptyList()
                    return@launch
                }
                val result = geocodingService.searchLocations(query)
                result.onSuccess { suggestions ->
                    _endSuggestions.value = suggestions
                    _routePlanningError.value = null
                }
                result.onFailure { error ->
                    Log.e("MainViewModel", "Error searching end location", error)
                    val message = ApiErrorUtils.toUserMessage(error, "Location search failed")
                    _routePlanningError.value = "Location search failed: $message"
                    _endSuggestions.value = emptyList()
                }
            } catch (e: Exception) {
                Log.e("MainViewModel", "Error searching end location", e)
            }
        }
    }

    fun reverseGeocode(lat: Double, lon: Double, isStart: Boolean) {
        viewModelScope.launch {
            try {
                if (!hasInternetConnection()) {
                    _routePlanningError.value = "No internet connection"
                    val fallbackName = "Unknown Location"
                    val location = com.example.kolomapa2.models.LocationPoint(lat, lon, fallbackName)
                    if (isStart) {
                        setStartLocation(location)
                    } else {
                        setEndLocation(location)
                    }
                    return@launch
                }
                val result = geocodingService.reverseGeocode(lat, lon)
                val locationName = result.getOrElse { error ->
                    Log.e("MainViewModel", "Error reverse geocoding", error)
                    val message = ApiErrorUtils.toUserMessage(error, "Reverse geocoding failed")
                    _routePlanningError.value = "Reverse geocoding failed: $message"
                    "Unknown Location"
                }
                if (result.isSuccess) {
                    _routePlanningError.value = null
                }
                val location = com.example.kolomapa2.models.LocationPoint(lat, lon, locationName)
                if (isStart) {
                    setStartLocation(location)
                } else {
                    setEndLocation(location)
                }
            } catch (e: Exception) {
                Log.e("MainViewModel", "Error reverse geocoding", e)
            }
        }
    }

    fun requestDeviceLocation(isStart: Boolean) {
        viewModelScope.launch {
            try {
                // Store which field is being filled (start or end)
                _locationRequestTarget.value = if (isStart)
                    com.example.kolomapa2.models.LocationTarget.START
                else
                    com.example.kolomapa2.models.LocationTarget.END

                // Send BLE request to ESP32 for GPS location (same as Navigate Home)
                val success = bleManager?.sendNavigateHomeRequest() ?: false
                if (!success) {
                    _routePlanningError.value = "Failed to request GPS from device"
                    _locationRequestTarget.value = null
                    return@launch
                }

                Log.d("MainViewModel", "Device location requested for ${if (isStart) "start" else "end"}")
                // Response handled by handleNavigateHomeRequest callback
            } catch (e: Exception) {
                Log.e("MainViewModel", "Error requesting device location", e)
                _routePlanningError.value = "Error: ${e.message}"
                _locationRequestTarget.value = null
            }
        }
    }

    fun requestCurrentDeviceLocation() {
        viewModelScope.launch {
            try {
                // Request GPS for map centering only
                _locationRequestTarget.value = com.example.kolomapa2.models.LocationTarget.MAP_CENTER

                // Send BLE request to ESP32 for GPS location
                val success = bleManager?.sendNavigateHomeRequest() ?: false
                if (!success) {
                    Log.w("MainViewModel", "Failed to request current GPS from device")
                    _locationRequestTarget.value = null
                    return@launch
                }

                Log.d("MainViewModel", "Current device location requested for map centering")
                // Response handled by handleNavigateHomeRequest callback
            } catch (e: Exception) {
                Log.e("MainViewModel", "Error requesting current device location", e)
                _locationRequestTarget.value = null
            }
        }
    }

    fun planRoute() {
        viewModelScope.launch {
            try {
                val start = _startLocation.value
                val end = _endLocation.value

                if (start == null || end == null) {
                    _routePlanningError.value = "Please select both start and end locations"
                    return@launch
                }

                if (start.lat == end.lat && start.lon == end.lon) {
                    _routePlanningError.value = "Start and end locations cannot be the same"
                    return@launch
                }

                if (!hasInternetConnection()) {
                    _routePlanningError.value = "No internet connection"
                    return@launch
                }

                _isLoadingRoute.value = true
                _routePlanningError.value = null

                Log.d("MainViewModel", "Planning route from (${start.lat}, ${start.lon}) to (${end.lat}, ${end.lon})")

                val routeDescription = "Route from ${start.displayName} to ${end.displayName}"
                val routeResult = routingService.getRoute(
                    startLat = start.lat,
                    startLon = start.lon,
                    endLat = end.lat,
                    endLon = end.lon,
                    routeName = "Planned Route",
                    routeDescription = routeDescription
                )

                routeResult.onSuccess { route ->
                    val trip = gpxParser.parseGpx(
                        route.gpxContent.byteInputStream(),
                        "planned_route_${System.currentTimeMillis()}.gpx"
                    )
                    _plannedRoute.value = trip
                    if (route.elevationHadError) {
                        val missingInfo = if (route.elevationMissing > 0 && route.elevationTotal > 0) {
                            " (${route.elevationMissing}/${route.elevationTotal} points)"
                        } else {
                            ""
                        }
                        _routePlanningError.value = "Elevation data unavailable$missingInfo"
                    }
                    Log.d("MainViewModel", "Route planned successfully: ${trip.metadata.totalDistance}m, ${trip.metadata.pointCount} points")
                }

                routeResult.onFailure { error ->
                    val message = ApiErrorUtils.toUserMessage(error, "Routing failed")
                    _routePlanningError.value = "Failed to plan route: $message"
                    Log.e("MainViewModel", "Route planning failed", error)
                }
            } catch (e: Exception) {
                Log.e("MainViewModel", "Error planning route", e)
                _routePlanningError.value = "Error: ${e.message}"
            } finally {
                _isLoadingRoute.value = false
            }
        }
    }

    fun savePlannedRoute(customName: String) {
        viewModelScope.launch {
            try {
                val trip = _plannedRoute.value ?: return@launch

                _isLoading.value = true
                _routePlanningError.value = null

                val sanitizedFileName = "$customName.gpx".replace(Regex("[^a-zA-Z0-9._-]"), "_")
                val updatedTrip = trip.copy(
                    metadata = trip.metadata.copy(
                        fileName = sanitizedFileName,
                        name = customName
                    )
                )

                Log.d("MainViewModel", "Saving planned route as: $sanitizedFileName")

                // Save trip
                withContext(Dispatchers.IO) {
                    storageManager.saveTrip(updatedTrip)
                }

                // Reload trips
                _trips.value = withContext(Dispatchers.IO) {
                    storageManager.loadAllTrips()
                }
                updateStorageStats()

                // Download tiles using BLE service (same pattern as Navigate Home)
                val bbox = updatedTrip.metadata.boundingBox

                val intent = Intent(getApplication(), BleService::class.java)
                getApplication<Application>().startForegroundService(intent)

                if (!serviceBound) {
                    getApplication<Application>().bindService(intent, serviceConnection, Context.BIND_AUTO_CREATE)
                }

                kotlinx.coroutines.delay(500)

                bleService?.downloadTilesForTrip(
                    tripFileName = sanitizedFileName,
                    minLat = bbox.minLat,
                    maxLat = bbox.maxLat,
                    minLon = bbox.minLon,
                    maxLon = bbox.maxLon,
                    tileDownloader = tileDownloader,
                    tilePreprocessor = tilePreprocessor,
                    onError = { message ->
                        _routePlanningError.value = "Tile download issue: $message"
                    }
                ) { success, downloadedCount ->
                    if (success) {
                        Log.d("MainViewModel", "Tile download complete for planned route: $downloadedCount tiles")
                    } else {
                        if (_routePlanningError.value == null) {
                            _routePlanningError.value = "Failed to download tiles"
                        }
                    }
                }

                // Reset route planning state
                clearRoutePlanning()

            } catch (e: Exception) {
                Log.e("MainViewModel", "Error saving planned route", e)
                _routePlanningError.value = "Failed to save: ${e.message}"
            } finally {
                _isLoading.value = false
            }
        }
    }

    fun clearRoutePlanning() {
        _startLocation.value = null
        _endLocation.value = null
        _plannedRoute.value = null
        _startSuggestions.value = emptyList()
        _endSuggestions.value = emptyList()
        _routePlanningError.value = null
        _locationRequestTarget.value = null
        Log.d("MainViewModel", "Route planning state cleared")
    }

    override fun onCleared() {
        super.onCleared()

        // Stop any ongoing locate phone operation
        locatePhoneManager.stopLocatePhone()

        // Unregister Bluetooth state receiver
        try {
            getApplication<Application>().unregisterReceiver(bluetoothStateReceiver)
            Log.d("MainViewModel", "Bluetooth state receiver unregistered")
        } catch (e: Exception) {
            Log.e("MainViewModel", "Error unregistering Bluetooth receiver", e)
        }

        // Clear notification listener callbacks
        NotificationListener.onNotificationReceived = null
        NotificationListener.onNotificationRemoved = null

        if (serviceBound) {
            getApplication<Application>().unbindService(serviceConnection)
            serviceBound = false
        }
        bleManager?.disconnect()
    }
}
