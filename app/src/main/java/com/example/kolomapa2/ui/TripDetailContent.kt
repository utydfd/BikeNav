package com.example.kolomapa2.ui

import androidx.compose.animation.core.Spring
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.spring
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsPressedAsState
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.example.kolomapa2.MainViewModel
import com.example.kolomapa2.models.ElevationProfilePoint
import com.example.kolomapa2.models.LocationPoint
import com.example.kolomapa2.models.Trip
import com.example.kolomapa2.ui.theme.TripOnDeviceBlue
import com.example.kolomapa2.ui.theme.TripTransferBlue
import com.example.kolomapa2.ui.theme.TripTransferBlueLight
import com.example.kolomapa2.utils.BleService
import com.example.kolomapa2.utils.ElevationProfileProcessor
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import kotlin.math.roundToInt

// Helper function to format distance with one decimal place (omitting .0)
private fun formatDistance(distanceInMeters: Double): String {
    val km = distanceInMeters / 1000
    return if (km % 1.0 == 0.0) {
        km.toInt().toString()
    } else {
        String.format(Locale.US, "%.1f", km)
    }
}

@OptIn(ExperimentalMaterial3Api::class, ExperimentalMaterial3ExpressiveApi::class)
@Composable
fun TripDetailContent(
    trip: Trip,
    onDelete: () -> Unit,
    onSendClick: () -> Unit,
    onStartClick: () -> Unit,
    onStopClick: () -> Unit,
    onLoadClick: (() -> Unit)? = null,
    transferState: BleService.ServiceState.Transferring? = null,
    downloadProgress: MainViewModel.DownloadProgress? = null,
    onDownloadActionClick: (() -> Unit)? = null,
    onTransferActionClick: (() -> Unit)? = null,
    isOnEsp: Boolean = false,
    isActive: Boolean = false,
    isDeviceConnected: Boolean = false,
    isNavigateHome: Boolean = false,
    isLoading: Boolean = false,
    isRouteLoaded: Boolean = false,
    errorMessage: String? = null,
    onErrorDismiss: (() -> Unit)? = null
) {
    // Format the timestamp (European format: DD.MM.YYYY HH:mm)
    val dateFormat = SimpleDateFormat("dd.MM.yyyy HH:mm", Locale.getDefault())
    val dateTime = dateFormat.format(Date(trip.metadata.createdAt))

    // Check if this trip is being transferred
    val isTransferring = transferState?.tripFileName == trip.metadata.fileName
    val isTransferActive = transferState != null
    val transferProgress = if (isTransferring) transferState?.percentage ?: 0 else 0
    val isDownloading = downloadProgress != null
    val isTransferBlocking = isTransferActive && !isActive && !isDownloading && !isTransferring
    val downloadProgressValue = if (downloadProgress != null && downloadProgress.total > 0) {
        ((downloadProgress.current.toFloat() / downloadProgress.total) * 100).toInt()
    } else 0

    // Animate the progress fill
    val animatedProgress by animateFloatAsState(
        targetValue = when {
            isTransferring -> transferProgress / 100f
            isDownloading -> downloadProgressValue / 100f
            else -> 0f
        },
        animationSpec = spring(
            dampingRatio = Spring.DampingRatioMediumBouncy,
            stiffness = Spring.StiffnessMedium
        ),
        label = "buttonProgressFill"
    )

    // Haptic feedback for button
    val haptic = LocalHapticFeedback.current

    // Interaction source to detect press state
    val interactionSource = remember { MutableInteractionSource() }
    val isPressed by interactionSource.collectIsPressedAsState()

    // Bounce animation based on press state
    val scale by animateFloatAsState(
        targetValue = if (isPressed) 0.95f else 1f,
        animationSpec = spring(
            dampingRatio = Spring.DampingRatioMediumBouncy,
            stiffness = Spring.StiffnessHigh
        ),
        label = "buttonScale"
    )

    // Elevation profile state
    val elevationProfile = remember(trip.gpxContent) {
        ElevationProfileProcessor.processGpxContent(trip.gpxContent)
    }
    var selectedProfilePoint by remember { mutableStateOf<ElevationProfilePoint?>(null) }

    val highlightLocation = selectedProfilePoint?.let {
        LocationPoint(it.lat, it.lon, "")
    }

    Column(
        modifier = Modifier.fillMaxSize()
    ) {
            // Scrollable content
            Column(
                modifier = Modifier
                    .weight(1f)
                    .fillMaxWidth()
                    .verticalScroll(rememberScrollState())
                    .padding(24.dp)
            ) {
                // Trip name in big letters with delete icon
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Column(modifier = Modifier.weight(1f)) {
                        Text(
                            trip.metadata.name,
                            style = MaterialTheme.typography.displayMedium,
                            fontWeight = FontWeight.Bold,
                            color = MaterialTheme.colorScheme.onSurface
                        )
                        // Show badge if trip is active, on ESP32, or Navigate Home route is loaded
                        if (isActive) {
                            Spacer(modifier = Modifier.height(8.dp))
                            Surface(
                                color = Color(0xFF10B981), // Green for active trip
                                shape = MaterialTheme.shapes.small
                            ) {
                                Text(
                                    "Active Trip",
                                    style = MaterialTheme.typography.labelMedium,
                                    color = Color.White,
                                    modifier = Modifier.padding(horizontal = 8.dp, vertical = 4.dp)
                                )
                            }
                        } else if (isOnEsp) {
                            Spacer(modifier = Modifier.height(8.dp))
                            Surface(
                                color = TripOnDeviceBlue,
                                shape = MaterialTheme.shapes.small
                            ) {
                                Text(
                                    "On Device",
                                    style = MaterialTheme.typography.labelMedium,
                                    color = Color.White,
                                    modifier = Modifier.padding(horizontal = 8.dp, vertical = 4.dp)
                                )
                            }
                        } else if (isNavigateHome && isRouteLoaded) {
                            Spacer(modifier = Modifier.height(8.dp))
                            Surface(
                                color = TripTransferBlueLight, // Blue for loaded route
                                shape = MaterialTheme.shapes.small
                            ) {
                                Text(
                                    "Route Loaded",
                                    style = MaterialTheme.typography.labelMedium,
                                    color = Color.White,
                                    modifier = Modifier.padding(horizontal = 8.dp, vertical = 4.dp)
                                )
                            }
                        }
                    }
                    // Show reload button for Navigate Home when route is loaded, otherwise show delete button
                    if (isNavigateHome && isRouteLoaded) {
                        IconButton(
                            onClick = { onLoadClick?.invoke() },
                            enabled = !isLoading && isDeviceConnected
                        ) {
                            Icon(
                                imageVector = Icons.Default.Refresh,
                                contentDescription = "Reload route",
                                tint = if (!isLoading && isDeviceConnected)
                                    MaterialTheme.colorScheme.primary
                                else
                                    MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.38f),
                                modifier = Modifier.size(32.dp)
                            )
                        }
                    } else if (!isNavigateHome) {
                        IconButton(onClick = onDelete) {
                            Icon(
                                imageVector = Icons.Default.Delete,
                                contentDescription = "Delete trip",
                                tint = MaterialTheme.colorScheme.error,
                                modifier = Modifier.size(32.dp)
                            )
                        }
                    }
                }

                Spacer(modifier = Modifier.height(12.dp))

                if (errorMessage != null) {
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
                                errorMessage,
                                modifier = Modifier.weight(1f),
                                color = MaterialTheme.colorScheme.onErrorContainer
                            )
                            if (onErrorDismiss != null) {
                                TextButton(onClick = onErrorDismiss) {
                                    Text("Dismiss")
                                }
                            }
                        }
                    }

                    Spacer(modifier = Modifier.height(12.dp))
                }

                // Map preview - larger size
                TripMapPreview(
                    trip = trip,
                    highlightLocation = highlightLocation,
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(450.dp)
                )

                Spacer(modifier = Modifier.height(12.dp))

                // Elevation profile chart
                ElevationProfileChart(
                    profile = elevationProfile,
                    selectedPoint = selectedProfilePoint,
                    onPointSelected = { point ->
                        selectedProfilePoint = point
                    },
                    modifier = Modifier.fillMaxWidth()
                )

                Spacer(modifier = Modifier.height(12.dp))

                // Metadata organized similar to storage stats - compact version
                Card(
                    modifier = Modifier.fillMaxWidth(),
                    elevation = CardDefaults.cardElevation(defaultElevation = 2.dp)
                ) {
                    Column(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(12.dp)
                    ) {
                        // First row: Distance and Points
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.SpaceBetween
                        ) {
                            Column(modifier = Modifier.weight(1f)) {
                                Text(
                                    "Distance",
                                    style = MaterialTheme.typography.labelSmall,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant
                                )
                                Row(verticalAlignment = Alignment.Bottom) {
                                    Text(
                                        formatDistance(trip.metadata.totalDistance),
                                        style = MaterialTheme.typography.headlineSmall,
                                        fontWeight = FontWeight.Bold,
                                        color = MaterialTheme.colorScheme.primary
                                    )
                                    Text(
                                        " km",
                                        style = MaterialTheme.typography.labelSmall,
                                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                                        modifier = Modifier.padding(bottom = 3.dp)
                                    )
                                }
                            }
                            Column(modifier = Modifier.weight(1f)) {
                                Text(
                                    "Points",
                                    style = MaterialTheme.typography.labelSmall,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant
                                )
                                Text(
                                    "${trip.metadata.pointCount}",
                                    style = MaterialTheme.typography.headlineSmall,
                                    fontWeight = FontWeight.Bold,
                                    color = MaterialTheme.colorScheme.secondary
                                )
                            }
                        }

                        HorizontalDivider(
                            modifier = Modifier.padding(vertical = 8.dp),
                            color = MaterialTheme.colorScheme.outlineVariant
                        )

                        // Second row: Min and Max Elevation
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.SpaceBetween
                        ) {
                            Column(modifier = Modifier.weight(1f)) {
                                Text(
                                    "Min Elevation",
                                    style = MaterialTheme.typography.labelSmall,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant
                                )
                                Row(verticalAlignment = Alignment.Bottom) {
                                    Text(
                                        "${trip.metadata.minElevation.roundToInt()}",
                                        style = MaterialTheme.typography.headlineSmall,
                                        fontWeight = FontWeight.Bold,
                                        color = MaterialTheme.colorScheme.primary
                                    )
                                    Text(
                                        " m",
                                        style = MaterialTheme.typography.labelSmall,
                                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                                        modifier = Modifier.padding(bottom = 3.dp)
                                    )
                                }
                            }
                            Column(modifier = Modifier.weight(1f)) {
                                Text(
                                    "Max Elevation",
                                    style = MaterialTheme.typography.labelSmall,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant
                                )
                                Row(verticalAlignment = Alignment.Bottom) {
                                    Text(
                                        "${trip.metadata.maxElevation.roundToInt()}",
                                        style = MaterialTheme.typography.headlineSmall,
                                        fontWeight = FontWeight.Bold,
                                        color = MaterialTheme.colorScheme.primary
                                    )
                                    Text(
                                        " m",
                                        style = MaterialTheme.typography.labelSmall,
                                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                                        modifier = Modifier.padding(bottom = 3.dp)
                                    )
                                }
                            }
                        }

                        HorizontalDivider(
                            modifier = Modifier.padding(vertical = 8.dp),
                            color = MaterialTheme.colorScheme.outlineVariant
                        )

                        // Third row: Elevation Gain and Loss
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.SpaceBetween
                        ) {
                            Column(modifier = Modifier.weight(1f)) {
                                Text(
                                    "Elevation Gain",
                                    style = MaterialTheme.typography.labelSmall,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant
                                )
                                Row(verticalAlignment = Alignment.Bottom) {
                                    Text(
                                        "+${trip.metadata.totalElevationGain.roundToInt()}",
                                        style = MaterialTheme.typography.headlineSmall,
                                        fontWeight = FontWeight.Bold,
                                        color = MaterialTheme.colorScheme.tertiary
                                    )
                                    Text(
                                        " m",
                                        style = MaterialTheme.typography.labelSmall,
                                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                                        modifier = Modifier.padding(bottom = 3.dp)
                                    )
                                }
                            }
                            Column(modifier = Modifier.weight(1f)) {
                                Text(
                                    "Elevation Loss",
                                    style = MaterialTheme.typography.labelSmall,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant
                                )
                                Row(verticalAlignment = Alignment.Bottom) {
                                    Text(
                                        "-${trip.metadata.totalElevationLoss.roundToInt()}",
                                        style = MaterialTheme.typography.headlineSmall,
                                        fontWeight = FontWeight.Bold,
                                        color = MaterialTheme.colorScheme.tertiary
                                    )
                                    Text(
                                        " m",
                                        style = MaterialTheme.typography.labelSmall,
                                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                                        modifier = Modifier.padding(bottom = 3.dp)
                                    )
                                }
                            }
                        }

                        HorizontalDivider(
                            modifier = Modifier.padding(vertical = 8.dp),
                            color = MaterialTheme.colorScheme.outlineVariant
                        )

                        // Fourth row: Created (and Route if available)
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.SpaceBetween
                        ) {
                            Column(modifier = Modifier.weight(1f)) {
                                Text(
                                    "Created",
                                    style = MaterialTheme.typography.labelSmall,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant
                                )
                                Text(
                                    dateTime,
                                    style = MaterialTheme.typography.bodyMedium,
                                    fontWeight = FontWeight.SemiBold,
                                    color = MaterialTheme.colorScheme.onSurface
                                )
                            }
                            if (trip.metadata.routeFrom != null && trip.metadata.routeTo != null) {
                                Column(modifier = Modifier.weight(1f)) {
                                    Text(
                                        "Route",
                                        style = MaterialTheme.typography.labelSmall,
                                        color = MaterialTheme.colorScheme.onSurfaceVariant
                                    )
                                    Text(
                                        "${trip.metadata.routeFrom} → ${trip.metadata.routeTo}",
                                        style = MaterialTheme.typography.bodyMedium,
                                        fontWeight = FontWeight.SemiBold,
                                        color = MaterialTheme.colorScheme.onSurface,
                                        maxLines = 1
                                    )
                                }
                            } else if (trip.metadata.routeDescription != null) {
                                Column(modifier = Modifier.weight(1f)) {
                                    Text(
                                        "Route",
                                        style = MaterialTheme.typography.labelSmall,
                                        color = MaterialTheme.colorScheme.onSurfaceVariant
                                    )
                                    Text(
                                        trip.metadata.routeDescription!!,
                                        style = MaterialTheme.typography.bodyMedium,
                                        fontWeight = FontWeight.SemiBold,
                                        color = MaterialTheme.colorScheme.onSurface,
                                        maxLines = 1
                                    )
                                }
                            }
                        }
                    }
                }
            }

        // Button at bottom (Load / Reload / Send / Start / Stop) with progress fill
        val isButtonEnabled = if (isTransferring) {
            true
        } else if (isTransferBlocking) {
            false
        } else if (isDownloading) {
            true
        } else if (isNavigateHome) {
            // For Navigate Home:
            // - Load/Reload button: needs device connected
            // - Start button: needs device connected and route loaded
            // - Stop button: always enabled if active
            if (isActive) true else isDeviceConnected
        } else {
            // For normal trips: needs device connected
            isDeviceConnected
        }

        Box(
            modifier = Modifier
                .fillMaxWidth()
                .padding(24.dp)
                .height(56.dp)
                .graphicsLayer {
                    scaleX = if (isButtonEnabled && !isLoading) scale else 1f
                    scaleY = if (isButtonEnabled && !isLoading) scale else 1f
                }
                .clip(MaterialTheme.shapes.medium)
                .background(
                    when {
                        isDownloading -> Color(0xFFF59E0B) // Amber for downloading
                        isTransferring -> TripTransferBlue // Blue for transferring
                        !isButtonEnabled || isLoading -> Color(0xFF6B7280) // Gray for disabled/loading
                        isActive -> Color(0xFFEF4444) // Red for active (stop button)
                        isNavigateHome && !isRouteLoaded -> Color(0xFF1B4D48) // Dark greenish-cyan for Navigate Home load button
                        isNavigateHome && isRouteLoaded -> Color(0xFF0F766E) // Darker teal for Navigate Home start button
                        isOnEsp -> Color(0xFF10B981) // Green for on device (start button)
                        else -> Color(0xFF2563EB) // Blue for send button
                    }
                )
                .then(
                    if (isButtonEnabled && !isLoading) {
                        Modifier.clickable(
                            interactionSource = interactionSource,
                            indication = null,
                            onClick = {
                                haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                                when {
                                    isDownloading -> onDownloadActionClick?.invoke()
                                    isTransferring -> onTransferActionClick?.invoke()
                                    isActive -> onStopClick()
                                    isNavigateHome && !isRouteLoaded -> onLoadClick?.invoke() // Load button
                                    isNavigateHome && isRouteLoaded -> onStartClick() // Start button
                                    isOnEsp -> onStartClick() // Start button (normal trips)
                                    else -> onSendClick() // Send button (normal trips)
                                }
                            }
                        )
                    } else {
                        Modifier
                    }
                ),
            contentAlignment = Alignment.Center
        ) {
            // Progress fill background (fills from left to right)
            if (isTransferring) {
                Box(
                    modifier = Modifier
                        .matchParentSize()
                        .graphicsLayer {
                            clip = true
                            scaleX = animatedProgress
                            transformOrigin = androidx.compose.ui.graphics.TransformOrigin(0f, 0.5f)
                        }
                        .background(TripTransferBlueLight)
                )
            } else if (isDownloading) {
                Box(
                    modifier = Modifier
                        .matchParentSize()
                        .graphicsLayer {
                            clip = true
                            scaleX = animatedProgress
                            transformOrigin = androidx.compose.ui.graphics.TransformOrigin(0f, 0.5f)
                        }
                        .background(Color(0xFFFCD34D))
                )
            }

            // Button text on top
            if (isLoading) {
                Row(
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    CircularProgressIndicator(
                        modifier = Modifier.size(20.dp),
                        color = Color.White,
                        strokeWidth = 2.dp
                    )
                    Text(
                        "Loading Route...",
                        style = MaterialTheme.typography.titleMedium,
                        fontWeight = FontWeight.Bold,
                        color = Color.White
                    )
                }
            } else {
                Text(
                    when {
                        isDownloading -> downloadProgress?.message ?: "Downloading..."
                        isTransferring -> "Transferring ${transferProgress}%"
                        isTransferBlocking -> "Transfer in Progress"
                        !isButtonEnabled -> "Device Not Connected"
                        isActive -> "Stop Trip"
                        isNavigateHome && !isRouteLoaded -> "Load Route"
                        isNavigateHome && isRouteLoaded -> "Start Trip"
                        isOnEsp -> "Start Trip"
                        else -> "Send Trip to Device"
                    },
                    style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.Bold,
                    color = Color.White
                )
            }
        }
    }
}
