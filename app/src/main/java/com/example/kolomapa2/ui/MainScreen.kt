package com.example.kolomapa2.ui

import android.Manifest
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.os.Environment
import android.provider.Settings
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.animation.*
import androidx.compose.animation.core.*
import androidx.activity.compose.BackHandler
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Settings
import androidx.compose.foundation.background
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsPressedAsState
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.lifecycle.compose.LocalLifecycleOwner
import androidx.lifecycle.viewmodel.compose.viewModel
import com.example.kolomapa2.MainViewModel
import com.example.kolomapa2.models.Trip
import com.example.kolomapa2.utils.BleService
import com.google.accompanist.permissions.ExperimentalPermissionsApi
import com.google.accompanist.permissions.rememberMultiplePermissionsState

@OptIn(ExperimentalPermissionsApi::class, ExperimentalMaterial3Api::class, ExperimentalMaterial3ExpressiveApi::class)
@Composable
fun MainScreen(
    viewModel: MainViewModel = viewModel(),
    incomingGpxUri: Uri? = null
) {
    val trips by viewModel.trips.collectAsState()
    val isLoadingTrips by viewModel.isLoadingTrips.collectAsState()
    val isLoading by viewModel.isLoading.collectAsState()
    val downloadingTrips by viewModel.downloadingTrips.collectAsState()
    val errorMessage by viewModel.errorMessage.collectAsState()
    val storageStats by viewModel.storageStats.collectAsState()
    val serviceState by viewModel.serviceState.collectAsState()
    val transferState by viewModel.transferState.collectAsState()
    val downloadState by viewModel.downloadState.collectAsState()
    val notificationAccessEnabled by viewModel.notificationAccessEnabled.collectAsState()
    val notificationSyncEnabled by viewModel.notificationSyncEnabled.collectAsState()
    val tripsOnEsp by viewModel.tripsOnEsp.collectAsState()
    val activeTripOnEsp by viewModel.activeTripOnEsp.collectAsState()
    val recordedTrips by viewModel.recordedTrips.collectAsState()
    val recordedTripsOnPhone by viewModel.recordedTripsOnPhone.collectAsState()
    val recordedTripsAdded by viewModel.recordedTripsAdded.collectAsState()
    val isLoadingRecordedTrips by viewModel.isLoadingRecordedTrips.collectAsState()
    val recordingDownloadProgress by viewModel.recordingDownloadProgress.collectAsState()
    val downloadedRecordingTrip by viewModel.downloadedRecordingTrip.collectAsState()
    val isNavigateHomeActive by viewModel.isNavigateHomeActive.collectAsState()
    val navigateHomeTrip by viewModel.navigateHomeTrip.collectAsState()
    val isLoadingNavigateHome by viewModel.isLoadingNavigateHome.collectAsState()
    val navigateHomeError by viewModel.navigateHomeError.collectAsState()
    val deviceStatus by viewModel.deviceStatus.collectAsState()
    val espDeviceStatus by viewModel.espDeviceStatus.collectAsState()
    val tripNamesByFile = remember(trips) {
        trips.associate { it.metadata.fileName to it.metadata.name }
    }
    val isBleConnected = serviceState is BleService.ServiceState.Connected ||
        serviceState is BleService.ServiceState.Transferring
    val hasInternet = deviceStatus.wifiConnected || deviceStatus.cellularType.isNotBlank()

    // Check if locate phone is active
    var isLocatePhoneActive by remember { mutableStateOf(false) }
    LaunchedEffect(Unit) {
        while (true) {
            isLocatePhoneActive = viewModel.locatePhoneManager.isLocating()
            kotlinx.coroutines.delay(500)
        }
    }

    var showTripDetails by remember { mutableStateOf<Trip?>(null) }
    var showNavigateHomeDetails by remember { mutableStateOf(false) }
    var showTileProcessingTest by remember { mutableStateOf(false) }
    var showBackButton by remember { mutableStateOf(false) }
    var showImportScreen by remember { mutableStateOf(false) }
    var showRoutePlanning by remember { mutableStateOf(false) }
    var showRecordedTrips by remember { mutableStateOf(false) }
    var pendingImportTrip by remember { mutableStateOf<Pair<Trip, String>?>(null) } // Trip and original filename
    var showDownloadOptionsForTrip by remember { mutableStateOf<Trip?>(null) }
    var showTransferCancelForTrip by remember { mutableStateOf<Trip?>(null) }
    var showRecordedTripDetails by remember { mutableStateOf<Trip?>(null) }

    // Delay back button appearance to prevent title jumping
    LaunchedEffect(
        showTripDetails,
        showNavigateHomeDetails,
        showTileProcessingTest,
        showImportScreen,
        showRoutePlanning,
        showRecordedTrips,
        showRecordedTripDetails
    ) {
        if (showTripDetails != null || showNavigateHomeDetails || showTileProcessingTest ||
            showImportScreen || showRoutePlanning || showRecordedTrips || showRecordedTripDetails != null) {
            kotlinx.coroutines.delay(100)
            showBackButton = true
        } else {
            showBackButton = false
        }
    }

    // Request permissions
    val permissions = buildList {
        // Bluetooth permissions
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            add(Manifest.permission.BLUETOOTH_SCAN)
            add(Manifest.permission.BLUETOOTH_CONNECT)
        } else {
            add(Manifest.permission.BLUETOOTH)
            add(Manifest.permission.BLUETOOTH_ADMIN)
        }

        // Phone state permission for cellular signal strength
        add(Manifest.permission.READ_PHONE_STATE)

        // Notification permission for Android 13+
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            add(Manifest.permission.POST_NOTIFICATIONS)
        }

        // Storage permissions for persistent data
        if (Build.VERSION.SDK_INT <= Build.VERSION_CODES.S_V2) { // Android 12L and below
            add(Manifest.permission.READ_EXTERNAL_STORAGE)
            add(Manifest.permission.WRITE_EXTERNAL_STORAGE)
        }
    }

    val permissionsState = rememberMultiplePermissionsState(permissions)

    // File picker for GPX import
    val filePicker = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.OpenDocument()
    ) { uri: Uri? ->
        uri?.let {
            viewModel.parseGpxForImport(it) { trip, defaultName ->
                pendingImportTrip = trip to defaultName
                showImportScreen = true
            }
        }
    }

    // Storage permission launcher for Android 11+
    val storagePermissionLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.StartActivityForResult()
    ) { _ ->
        // Permission result will be checked when app resumes
    }

    // Load trips asynchronously after UI renders
    LaunchedEffect(Unit) {
        viewModel.loadTrips()
    }

    LaunchedEffect(Unit) {
        if (!permissionsState.allPermissionsGranted) {
            permissionsState.launchMultiplePermissionRequest()
        }

        // For Android 11+ (API 30+), request MANAGE_EXTERNAL_STORAGE if not granted
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            if (!Environment.isExternalStorageManager()) {
                val intent = Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION).apply {
                    data = Uri.parse("package:com.example.kolomapa2")
                }
                storagePermissionLauncher.launch(intent)
            }
        }
    }

    // Handle incoming GPX file from external sources (file manager, share, etc.)
    LaunchedEffect(incomingGpxUri) {
        incomingGpxUri?.let { uri ->
            viewModel.parseGpxForImport(uri) { trip, defaultName ->
                pendingImportTrip = trip to defaultName
                showImportScreen = true
            }
        }
    }

    LaunchedEffect(downloadedRecordingTrip) {
        downloadedRecordingTrip?.let { trip ->
            showRecordedTrips = true
            showRecordedTripDetails = trip
            viewModel.clearDownloadedRecordingTrip()
        }
    }

    LaunchedEffect(showRecordedTrips) {
        if (showRecordedTrips) {
            viewModel.loadRecordedTripsFromStorage()
        }
    }

    // Check notification access on every resume (important for sideloaded apps)
    val lifecycleOwner = LocalLifecycleOwner.current
    DisposableEffect(lifecycleOwner) {
        val observer = LifecycleEventObserver { _, event ->
            when (event) {
                Lifecycle.Event.ON_START,
                Lifecycle.Event.ON_RESUME -> {
                    // Check notification access every time app comes to foreground
                    // This is crucial for sideloaded apps where Android may revoke permissions
                    viewModel.checkNotificationAccess()
                }
                else -> {}
            }
        }
        lifecycleOwner.lifecycle.addObserver(observer)
        onDispose {
            lifecycleOwner.lifecycle.removeObserver(observer)
        }
    }

    // State for delete confirmation dialog
    var showDeleteConfirmation by remember { mutableStateOf<Trip?>(null) }

    // Handle system back button when on detail/import screen
    if (showTripDetails != null || showNavigateHomeDetails || showTileProcessingTest ||
        showImportScreen || showRoutePlanning || showRecordedTrips || showRecordedTripDetails != null) {
        BackHandler(onBack = {
            when {
                showRecordedTripDetails != null -> {
                    showRecordedTripDetails = null
                    showDownloadOptionsForTrip = null
                    showTransferCancelForTrip = null
                }
                showRecordedTrips -> showRecordedTrips = false
                else -> {
                    showTripDetails = null
                    showNavigateHomeDetails = false
                    showTileProcessingTest = false
                    showImportScreen = false
                    showRoutePlanning = false
                    pendingImportTrip = null
                    showDownloadOptionsForTrip = null
                    showTransferCancelForTrip = null
                }
            }
        })
    }

    // Delete confirmation dialog
    showDeleteConfirmation?.let { tripToDelete ->
        AlertDialog(
            onDismissRequest = { showDeleteConfirmation = null },
            title = { Text("Delete Trip") },
            text = { Text("Are you sure you want to delete \"${tripToDelete.metadata.name}\"? This action cannot be undone.") },
            confirmButton = {
                Button(
                    onClick = {
                        if (showRecordedTripDetails != null) {
                            viewModel.deleteRecordedTrip(tripToDelete.metadata.fileName)
                        } else {
                            viewModel.deleteTrip(tripToDelete.metadata.fileName)
                        }
                        showDeleteConfirmation = null
                        showTripDetails = null
                        showRecordedTripDetails = null
                    },
                    colors = ButtonDefaults.buttonColors(
                        containerColor = MaterialTheme.colorScheme.error
                    )
                ) {
                    Text("Delete")
                }
            },
            dismissButton = {
                TextButton(onClick = { showDeleteConfirmation = null }) {
                    Text("Cancel")
                }
            }
        )
    }

    showDownloadOptionsForTrip?.let { tripToManage ->
        DownloadCancelSheet(
            tripName = tripToManage.metadata.name,
            onStopKeep = {
                viewModel.cancelDownload()
                showDownloadOptionsForTrip = null
            },
            onStopDelete = {
                viewModel.cancelDownload()
                viewModel.deleteTrip(tripToManage.metadata.fileName)
                showDownloadOptionsForTrip = null
                showTripDetails = null
            },
            onDismiss = { showDownloadOptionsForTrip = null }
        )
    }

    showTransferCancelForTrip?.let { tripToManage ->
        TransferCancelSheet(
            tripName = tripToManage.metadata.name,
            onCancelTransfer = {
                viewModel.cancelTransfer()
                showTransferCancelForTrip = null
            },
            onDismiss = { showTransferCancelForTrip = null }
        )
    }

    // Locate Phone active dialog
    if (isLocatePhoneActive) {
        AlertDialog(
            onDismissRequest = { viewModel.locatePhoneManager.stopLocatePhone() },
            title = { Text("Phone is Ringing") },
            text = { Text("Your phone is currently playing a ringtone to help you locate it.") },
            confirmButton = {
                Button(
                    onClick = { viewModel.locatePhoneManager.stopLocatePhone() },
                    colors = ButtonDefaults.buttonColors(
                        containerColor = MaterialTheme.colorScheme.primary
                    )
                ) {
                    Text("Stop Ringing")
                }
            }
        )
    }

    // Single persistent scaffold with animated top bar and content
    Scaffold(
        topBar = {
            TopAppBar(
                title = {
                    // Animated app name that fades when back button appears
                    AnimatedVisibility(
                        visible = showTripDetails == null && !showNavigateHomeDetails && !showImportScreen &&
                            !showRoutePlanning && !showRecordedTrips && showRecordedTripDetails == null,
                        enter = fadeIn(
                            animationSpec = tween(
                                durationMillis = 200,
                                delayMillis = 200,
                                easing = LinearEasing
                            )
                        ),
                        exit = fadeOut(
                            animationSpec = tween(
                                durationMillis = 0,
                                easing = LinearEasing
                            )
                        )
                    ) {
                        Text("KoloMapa2")
                    }
                },
                colors = TopAppBarDefaults.topAppBarColors(
                    containerColor = getTopBarColor(serviceState)
                ),
                navigationIcon = {
                    // Animated back button - fades in/out
                    AnimatedVisibility(
                        visible = showBackButton,
                        enter = fadeIn(
                            animationSpec = tween(
                                durationMillis = 150,
                                easing = LinearEasing
                            )
                        ),
                        exit = fadeOut(
                            animationSpec = tween(
                                durationMillis = 100,
                                easing = LinearEasing
                            )
                        )
                    ) {
                        IconButton(onClick = {
                            when {
                                showRecordedTripDetails != null -> {
                                    showRecordedTripDetails = null
                                    showDownloadOptionsForTrip = null
                                    showTransferCancelForTrip = null
                                }
                                showRecordedTrips -> showRecordedTrips = false
                                else -> {
                                    showTripDetails = null
                                    showNavigateHomeDetails = false
                                    showTileProcessingTest = false
                                    showImportScreen = false
                                    showRoutePlanning = false
                                    pendingImportTrip = null
                                    showDownloadOptionsForTrip = null
                                }
                            }
                        }) {
                            Icon(
                                imageVector = Icons.AutoMirrored.Filled.ArrowBack,
                                contentDescription = "Back",
                                tint = MaterialTheme.colorScheme.onSurface
                            )
                        }
                    }
                },
                actions = {
                    // Connection button always visible
                    Box(
                        modifier = Modifier
                            .padding(end = 8.dp)
                            .wrapContentWidth(Alignment.End, unbounded = true),
                        contentAlignment = Alignment.CenterEnd
                    ) {
                        val activeTransferState = transferState ?: (serviceState as? BleService.ServiceState.Transferring)
                        val activeDownloadState = downloadState ?: (serviceState as? BleService.ServiceState.Downloading)
                        val connectionActions = buildList {
                            activeTransferState?.let { state ->
                                val tripName = tripNamesByFile[state.tripFileName] ?: state.tripFileName
                                add(
                                    ConnectionAction(
                                        id = "transfer_${state.tripFileName}",
                                        label = "Transfer: $tripName (${state.percentage}%)",
                                        color = Color(0xFF3B82F6)
                                    ) {
                                        viewModel.cancelTransfer()
                                    }
                                )
                            }
                            activeDownloadState?.let { state ->
                                val tripName = tripNamesByFile[state.tripFileName] ?: state.tripFileName
                                add(
                                    ConnectionAction(
                                        id = "download_${state.tripFileName}",
                                        label = "Download: $tripName (${state.percentage}%)",
                                        color = Color(0xFF8B5CF6)
                                    ) {
                                        viewModel.cancelDownload()
                                    }
                                )
                            }
                            if (isNotEmpty()) {
                                add(
                                    ConnectionAction(
                                        id = "disconnect",
                                        label = "Disconnect",
                                        color = Color(0xFFEF4444)
                                    ) {
                                        viewModel.stopBleService()
                                    }
                                )
                            }
                        }
                        ConnectionButton(
                            serviceState = serviceState,
                            transferState = activeTransferState,
                            downloadState = activeDownloadState,
                            actions = connectionActions,
                            onPrimaryClick = {
                                when {
                                    activeTransferState != null -> viewModel.cancelTransfer()
                                    activeDownloadState != null -> viewModel.cancelDownload()
                                    serviceState is BleService.ServiceState.Idle ||
                                        serviceState is BleService.ServiceState.Error -> {
                                        viewModel.startBleService()
                                    }
                                    serviceState is BleService.ServiceState.Connecting ||
                                        serviceState is BleService.ServiceState.Connected -> {
                                        viewModel.stopBleService()
                                    }
                                }
                            }
                        )
                    }
                }
            )
        },
        floatingActionButton = {
            // Only show FAB on main screen
            AnimatedVisibility(
                visible = showTripDetails == null && !showNavigateHomeDetails && !showTileProcessingTest &&
                    !showImportScreen && !showRoutePlanning && !showRecordedTrips && showRecordedTripDetails == null,
                enter = scaleIn() + fadeIn(),
                exit = scaleOut() + fadeOut()
            ) {
                AddTripFabMenu(
                    onLoadFromGpxClick = {
                        filePicker.launch(arrayOf("*/*"))
                    },
                    onPlanRouteClick = {
                        showRoutePlanning = true
                        viewModel.clearRoutePlanning()
                    },
                    onRecordedTripsClick = {
                        showRecordedTrips = true
                    },
                    onSettingsClick = {
                        showTileProcessingTest = true
                    }
                )
            }
        }
    ) { padding ->
        // Animated content area - only this animates, not the top bar
        // Use a pair to track both trip details and Navigate Home details
        val contentState = when {
            showRecordedTripDetails != null -> "recorded_trip_${showRecordedTripDetails!!.metadata.fileName}"
            showRecordedTrips -> "recorded_trips"
            showRoutePlanning -> "route_planning"
            showTileProcessingTest -> "tile_processing_test"
            showNavigateHomeDetails -> "navigate_home"
            showImportScreen -> "import_trip"
            showTripDetails != null -> "trip_${showTripDetails!!.metadata.fileName}"
            else -> "main"
        }

        AnimatedContent(
            targetState = contentState,
            transitionSpec = {
                val animationDuration = 300
                val isRoot: (String) -> Boolean = { state ->
                    state == "main" || state == "recorded_trips"
                }
                val forward = when {
                    initialState == "main" && targetState == "recorded_trips" -> true
                    initialState == "recorded_trips" && targetState == "main" -> false
                    isRoot(initialState) && !isRoot(targetState) -> true
                    !isRoot(initialState) && isRoot(targetState) -> false
                    else -> true
                }

                if (forward) {
                    // Slide in from right when opening detail
                    (slideInHorizontally(
                        animationSpec = tween(
                            durationMillis = animationDuration,
                            easing = FastOutSlowInEasing
                        ),
                        initialOffsetX = { it }
                    ) + fadeIn(
                        animationSpec = tween(
                            durationMillis = animationDuration,
                            easing = LinearEasing
                        )
                    )).togetherWith(
                        slideOutHorizontally(
                            animationSpec = tween(
                                durationMillis = animationDuration,
                                easing = FastOutSlowInEasing
                            ),
                            targetOffsetX = { -it / 3 }
                        ) + fadeOut(
                            animationSpec = tween(
                                durationMillis = animationDuration / 2,
                                easing = LinearEasing
                            )
                        )
                    )
                } else {
                    // Slide back when returning to list
                    (slideInHorizontally(
                        animationSpec = tween(
                            durationMillis = animationDuration,
                            easing = FastOutSlowInEasing
                        ),
                        initialOffsetX = { -it / 3 }
                    ) + fadeIn(
                        animationSpec = tween(
                            durationMillis = animationDuration,
                            easing = LinearEasing
                        )
                    )).togetherWith(
                        slideOutHorizontally(
                            animationSpec = tween(
                                durationMillis = animationDuration,
                                easing = FastOutSlowInEasing
                            ),
                            targetOffsetX = { it }
                        ) + fadeOut(
                            animationSpec = tween(
                                durationMillis = animationDuration / 2,
                                easing = LinearEasing
                            )
                        )
                    )
                }
            },
            label = "contentTransition",
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
        ) { state ->
            when {
                state == "recorded_trips" -> {
                    RecordedTripsScreen(
                        recordedTrips = recordedTrips,
                        localRecordings = recordedTripsOnPhone,
                        recordedTripsAdded = recordedTripsAdded,
                        isDeviceConnected = isBleConnected,
                        isLoading = isLoadingRecordedTrips,
                        downloadProgress = recordingDownloadProgress,
                        onRefresh = { viewModel.refreshRecordedTrips() },
                        onDownload = { recording -> viewModel.downloadRecordedTrip(recording.dirName) },
                        onOpenTrip = { trip -> showRecordedTripDetails = trip },
                        onAddToTrips = { trip -> viewModel.addRecordedTripToTrips(trip) }
                    )
                }
                state.startsWith("recorded_trip_") -> {
                    showRecordedTripDetails?.let { recordedTrip ->
                        TripDetailContent(
                            trip = recordedTrip,
                            onDelete = { showDeleteConfirmation = recordedTrip },
                            onSendClick = { viewModel.sendTripAndTilesToEsp(recordedTrip) },
                            onStartClick = { viewModel.startTripOnEsp(recordedTrip.metadata.fileName) },
                            onStopClick = { viewModel.stopTripOnEsp() },
                            transferState = if (serviceState is BleService.ServiceState.Transferring)
                                serviceState as BleService.ServiceState.Transferring else null,
                            downloadProgress = downloadingTrips[recordedTrip.metadata.fileName],
                            onDownloadActionClick = { showDownloadOptionsForTrip = recordedTrip },
                            onTransferActionClick = { showTransferCancelForTrip = recordedTrip },
                            isOnEsp = false,
                            isActive = activeTripOnEsp == recordedTrip.metadata.fileName,
                            isDeviceConnected = isBleConnected
                        )
                    }
                }
                state == "route_planning" -> {
                    // Route Planning Screen
                    RoutePlanningScreen(
                        viewModel = viewModel,
                        onBackClick = { showRoutePlanning = false }
                    )
                }
                state == "tile_processing_test" -> {
                    // Tile Processing Test Screen
                    TileProcessingTestScreen(
                        storageManager = viewModel.storageManager,
                        onBackClick = { showTileProcessingTest = false }
                    )
                }
                state == "import_trip" -> {
                    // Import Trip Screen
                    pendingImportTrip?.let { (trip, defaultName) ->
                        ImportTripScreen(
                            trip = trip,
                            defaultName = defaultName,
                            onAddClick = { customName ->
                                viewModel.addImportedTrip(trip, customName)
                                showImportScreen = false
                                pendingImportTrip = null
                            },
                            isAdding = isLoading
                        )
                    }
                }
                state == "navigate_home" -> {
                    // Navigate Home detail content
                    // Use loaded trip if available, otherwise show placeholder
                    val displayTrip = navigateHomeTrip ?: run {
                        // Create placeholder trip for Navigate Home
                        val homeLat = com.example.kolomapa2.utils.Constants.HOME_LATITUDE
                        val homeLon = com.example.kolomapa2.utils.Constants.HOME_LONGITUDE
                        Trip(
                            metadata = com.example.kolomapa2.models.TripMetadata(
                                fileName = com.example.kolomapa2.utils.Constants.NAVIGATE_HOME_TRIP_NAME,
                                name = "Navigate Home",
                                createdAt = System.currentTimeMillis(),
                                totalDistance = 0.0,
                                pointCount = 0,
                                minElevation = 0.0,
                                maxElevation = 0.0,
                                totalElevationGain = 0.0,
                                totalElevationLoss = 0.0,
                                boundingBox = com.example.kolomapa2.models.BoundingBox(
                                    minLat = homeLat - 0.01,
                                    maxLat = homeLat + 0.01,
                                    minLon = homeLon - 0.01,
                                    maxLon = homeLon + 0.01
                                ),
                                routeDescription = "Route to home"
                            ),
                            gpxContent = ""
                        )
                    }

                        TripDetailContent(
                            trip = displayTrip,
                            onDelete = { /* Navigate Home can't be deleted */ },
                            onSendClick = { /* Not used for Navigate Home */ },
                            onStartClick = { viewModel.startNavigateHomeTrip() },
                            onStopClick = { viewModel.stopTripOnEsp() },
                            onLoadClick = { viewModel.loadNavigateHomeRoute() },
                            transferState = if (serviceState is BleService.ServiceState.Transferring)
                                serviceState as BleService.ServiceState.Transferring else null,
                            isOnEsp = false, // Don't show "On Device" badge for Navigate Home
                            isActive = isNavigateHomeActive,
                            isDeviceConnected = isBleConnected,
                            isNavigateHome = true,
                            isLoading = isLoadingNavigateHome,
                            isRouteLoaded = navigateHomeTrip != null,
                            errorMessage = navigateHomeError,
                            onErrorDismiss = { viewModel.clearNavigateHomeError() },
                            onTransferActionClick = { showTransferCancelForTrip = displayTrip }
                    )
                }
                state.startsWith("trip_") -> {
                    // Regular trip detail content
                    showTripDetails?.let { selectedTrip ->
                        TripDetailContent(
                            trip = selectedTrip,
                            onDelete = { showDeleteConfirmation = selectedTrip },
                            onSendClick = { viewModel.sendTripAndTilesToEsp(selectedTrip) },
                            onStartClick = { viewModel.startTripOnEsp(selectedTrip.metadata.fileName) },
                            onStopClick = { viewModel.stopTripOnEsp() },
                            transferState = if (serviceState is BleService.ServiceState.Transferring)
                                serviceState as BleService.ServiceState.Transferring else null,
                            downloadProgress = downloadingTrips[selectedTrip.metadata.fileName],
                            onDownloadActionClick = { showDownloadOptionsForTrip = selectedTrip },
                            onTransferActionClick = { showTransferCancelForTrip = selectedTrip },
                            isOnEsp = tripsOnEsp.contains(selectedTrip.metadata.fileName),
                            isActive = activeTripOnEsp == selectedTrip.metadata.fileName,
                            isDeviceConnected = isBleConnected
                        )
                    }
                }
                else -> {
                // Main screen content
                LazyColumn(
                    modifier = Modifier.fillMaxSize(),
                    contentPadding = PaddingValues(start = 16.dp, end = 16.dp, top = 0.dp, bottom = 16.dp),
                    verticalArrangement = Arrangement.spacedBy(12.dp)
                ) {
                    // Combined Storage Stats & Notification Sync Card
                    item(key = "storage_card") {
                        StorageAndNotificationCard(
                            modifier = Modifier.padding(vertical = 8.dp),
                            storageStats = storageStats,
                            notificationAccessEnabled = notificationAccessEnabled,
                            notificationSyncEnabled = notificationSyncEnabled,
                            onNotificationSyncToggle = { enabled ->
                                viewModel.setNotificationSyncEnabled(enabled)
                            },
                            onEnableAccessClick = {
                                viewModel.openNotificationAccessSettings()
                            },
                            deviceConnected = isBleConnected,
                            espDeviceStatus = espDeviceStatus
                        )
                    }

                    // Error message
                    errorMessage?.let { error ->
                        item(key = "error_message") {
                            Card(
                                modifier = Modifier.fillMaxWidth(),
                                colors = CardDefaults.cardColors(
                                    containerColor = MaterialTheme.colorScheme.errorContainer
                                )
                            ) {
                                Row(
                                    modifier = Modifier.padding(16.dp),
                                    verticalAlignment = Alignment.CenterVertically
                                ) {
                                    Text(
                                        error,
                                        modifier = Modifier.weight(1f),
                                        color = MaterialTheme.colorScheme.onErrorContainer
                                    )
                                    TextButton(onClick = { viewModel.clearError() }) {
                                        Text("Dismiss")
                                    }
                                }
                            }
                        }
                    }

                    // Trip list
                    when {
                        isLoadingTrips -> {
                            // Material 3 Expressive Loading Indicator
                            item(key = "loading_indicator") {
                                Box(
                                    modifier = Modifier
                                        .fillMaxWidth()
                                        .padding(vertical = 32.dp),
                                    contentAlignment = Alignment.Center
                                ) {
                                    ContainedLoadingIndicator(
                                        containerColor = MaterialTheme.colorScheme.primaryContainer,
                                        indicatorColor = MaterialTheme.colorScheme.primary,
                                        modifier = Modifier.size(120.dp)
                                    )
                                }
                            }
                        }
                        trips.isEmpty() -> {
                            item(key = "empty_state") {
                                Box(
                                    modifier = Modifier
                                        .fillMaxWidth()
                                        .padding(vertical = 32.dp),
                                    contentAlignment = Alignment.Center
                                ) {
                                    Text("No trips. Tap + to import a GPX file.")
                                }
                            }
                        }
                        else -> {
                            // Navigate Home Card (special card at top)
                            item(key = "navigate_home_card") {
                                NavigateHomeCard(
                                    isEspConnected = isBleConnected,
                                    hasInternet = hasInternet,
                                    isActive = isNavigateHomeActive,
                                    onClick = {
                                        // Show Navigate Home details page
                                        showNavigateHomeDetails = true
                                    }
                                )
                            }

                            items(
                                items = trips,
                                key = { trip -> trip.metadata.fileName }
                            ) { trip ->
                                val tripDownloadProgress = downloadingTrips[trip.metadata.fileName]
                                android.util.Log.d("MainScreen", "Rendering trip ${trip.metadata.fileName}, downloading=${tripDownloadProgress != null}")
                                if (tripDownloadProgress != null) {
                                    android.util.Log.d("MainScreen", "  Progress: ${tripDownloadProgress.current}/${tripDownloadProgress.total} - ${tripDownloadProgress.message}")
                                }
                                android.util.Log.d("MainScreen", "  Current downloadingTrips keys: ${downloadingTrips.keys.joinToString()}")

                                TripCard(
                                    trip = trip,
                                    onClick = { showTripDetails = trip },
                                    transferState = if (serviceState is BleService.ServiceState.Transferring)
                                        serviceState as BleService.ServiceState.Transferring else null,
                                    downloadProgress = tripDownloadProgress,
                                    isOnEsp = tripsOnEsp.contains(trip.metadata.fileName),
                                    isActive = activeTripOnEsp == trip.metadata.fileName
                                )
                            }
                        }
                    }
                }
            }
        }
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun DownloadCancelSheet(
    tripName: String,
    onStopKeep: () -> Unit,
    onStopDelete: () -> Unit,
    onDismiss: () -> Unit
) {
    val sheetState = rememberModalBottomSheetState(skipPartiallyExpanded = true)
    val scrollState = rememberScrollState()

    ModalBottomSheet(
        onDismissRequest = onDismiss,
        sheetState = sheetState
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .verticalScroll(scrollState)
                .padding(16.dp)
        ) {
            Text(
                "Download in Progress",
                style = MaterialTheme.typography.titleLarge,
                fontWeight = FontWeight.Bold
            )
            Spacer(modifier = Modifier.height(8.dp))
            Text(
                "Stop downloading tiles for \"$tripName\"?",
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            Spacer(modifier = Modifier.height(20.dp))

            FancySheetButton(
                text = "Stop & Keep Trip",
                backgroundColor = Color(0xFF2563EB),
                onClick = onStopKeep
            )
            Spacer(modifier = Modifier.height(12.dp))
            FancySheetButton(
                text = "Stop & Delete Trip",
                backgroundColor = Color(0xFFEF4444),
                onClick = onStopDelete
            )
            Spacer(modifier = Modifier.height(12.dp))
            FancySheetButton(
                text = "Keep Downloading",
                backgroundColor = Color(0xFF6B7280),
                onClick = onDismiss
            )
            Spacer(modifier = Modifier.height(12.dp))
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun TransferCancelSheet(
    tripName: String,
    onCancelTransfer: () -> Unit,
    onDismiss: () -> Unit
) {
    val sheetState = rememberModalBottomSheetState(skipPartiallyExpanded = true)
    val scrollState = rememberScrollState()

    ModalBottomSheet(
        onDismissRequest = onDismiss,
        sheetState = sheetState
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .verticalScroll(scrollState)
                .padding(16.dp)
        ) {
            Text(
                "Transfer in Progress",
                style = MaterialTheme.typography.titleLarge,
                fontWeight = FontWeight.Bold
            )
            Spacer(modifier = Modifier.height(8.dp))
            Text(
                "Cancel transfer for \"$tripName\"?",
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            Spacer(modifier = Modifier.height(20.dp))

            FancySheetButton(
                text = "Cancel Transfer",
                backgroundColor = Color(0xFFEF4444),
                onClick = onCancelTransfer
            )
            Spacer(modifier = Modifier.height(12.dp))
            FancySheetButton(
                text = "Keep Transferring",
                backgroundColor = Color(0xFF6B7280),
                onClick = onDismiss
            )
            Spacer(modifier = Modifier.height(12.dp))
        }
    }
}

@Composable
private fun FancySheetButton(
    text: String,
    backgroundColor: Color,
    onClick: () -> Unit,
    modifier: Modifier = Modifier
) {
    val haptic = LocalHapticFeedback.current
    val interactionSource = remember { MutableInteractionSource() }
    val isPressed by interactionSource.collectIsPressedAsState()
    val scale by animateFloatAsState(
        targetValue = if (isPressed) 0.96f else 1f,
        animationSpec = spring(
            dampingRatio = Spring.DampingRatioMediumBouncy,
            stiffness = Spring.StiffnessHigh
        ),
        label = "sheetButtonScale"
    )

    Box(
        modifier = modifier
            .fillMaxWidth()
            .height(56.dp)
            .graphicsLayer {
                scaleX = scale
                scaleY = scale
            }
            .clip(MaterialTheme.shapes.medium)
            .background(backgroundColor)
            .clickable(
                interactionSource = interactionSource,
                indication = null,
                onClick = {
                    haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                    onClick()
                }
            ),
        contentAlignment = Alignment.Center
    ) {
        Text(
            text,
            style = MaterialTheme.typography.titleMedium,
            fontWeight = FontWeight.Bold,
            color = Color.White
        )
    }
}
