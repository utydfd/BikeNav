package com.example.kolomapa2.ui

import androidx.compose.animation.core.Spring
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.spring
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsPressedAsState
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.example.kolomapa2.MainViewModel
import com.example.kolomapa2.models.RecordingInfo
import com.example.kolomapa2.models.Trip

@Composable
fun RecordedTripsScreen(
    recordedTrips: List<RecordingInfo>,
    localRecordings: List<Trip>,
    recordedTripsAdded: Set<String>,
    isDeviceConnected: Boolean,
    isLoading: Boolean,
    downloadProgress: MainViewModel.RecordingDownloadProgress?,
    onRefresh: () -> Unit,
    onDownload: (RecordingInfo) -> Unit,
    onOpenTrip: (Trip) -> Unit,
    onAddToTrips: (Trip) -> Unit
) {
    val localTripsByName = remember(localRecordings) {
        localRecordings.associateBy { it.metadata.fileName }
    }
    val displayRecordings = remember(recordedTrips, localRecordings) {
        if (recordedTrips.isNotEmpty()) {
            recordedTrips
        } else {
            localRecordings.map { trip ->
                RecordingInfo(
                    name = trip.metadata.name,
                    dirName = trip.metadata.fileName
                )
            }
        }
    }

    LaunchedEffect(isDeviceConnected) {
        if (isDeviceConnected) {
            onRefresh()
        }
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(16.dp)
    ) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Text(
                text = "Recorded Trips",
                style = MaterialTheme.typography.headlineMedium,
                fontWeight = FontWeight.Bold
            )
            TextButton(onClick = onRefresh, enabled = isDeviceConnected && !isLoading) {
                Text("Refresh")
            }
        }

        Spacer(modifier = Modifier.height(12.dp))

        if (!isDeviceConnected) {
            Card(
                modifier = Modifier.fillMaxWidth(),
                colors = CardDefaults.cardColors(
                    containerColor = MaterialTheme.colorScheme.errorContainer
                )
            ) {
                Text(
                    text = "Connect to the device to load recordings from the ESP.",
                    modifier = Modifier.padding(16.dp),
                    color = MaterialTheme.colorScheme.onErrorContainer
                )
            }
        }

        when {
            isLoading -> {
                Box(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(vertical = 32.dp),
                    contentAlignment = Alignment.Center
                ) {
                    CircularProgressIndicator()
                }
            }
            displayRecordings.isEmpty() -> {
                Box(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(vertical = 32.dp),
                    contentAlignment = Alignment.Center
                ) {
                    Text("No recorded trips available.")
                }
            }
            else -> {
                LazyColumn(
                    modifier = Modifier.fillMaxSize(),
                    verticalArrangement = Arrangement.spacedBy(12.dp)
                ) {
                    items(displayRecordings, key = { it.dirName }) { recording ->
                        val localTrip = localTripsByName[recording.dirName]
                        val isDownloading = downloadProgress?.recordingName == recording.dirName
                        val progress = downloadProgress?.let { progress ->
                            if (progress.total > 0) progress.current.toFloat() / progress.total else null
                        }
                        val isAddedToTrips = recordedTripsAdded.contains(recording.dirName)
                        RecordedTripCard(
                            recording = recording,
                            localTrip = localTrip,
                            isDownloading = isDownloading,
                            progress = progress,
                            isAddedToTrips = isAddedToTrips,
                            onDownload = { onDownload(recording) },
                            onOpenTrip = { localTrip?.let(onOpenTrip) },
                            onAddToTrips = { localTrip?.let(onAddToTrips) },
                            isDeviceConnected = isDeviceConnected
                        )
                    }
                }
            }
        }
    }
}

// Status colors for recorded trips
private val RecordingDownloadedColor = Color(0xFF10B981) // Green for downloaded/ready
private val RecordingDownloadingColor = Color(0xFFF59E0B) // Amber for downloading
private val RecordingOnDeviceColor = Color(0xFF6366F1) // Indigo for on ESP only
private val RecordingAddedColor = Color(0xFF8B5CF6) // Purple for added to trips

@Composable
private fun RecordedTripCard(
    recording: RecordingInfo,
    localTrip: Trip?,
    isDownloading: Boolean,
    progress: Float?,
    isAddedToTrips: Boolean,
    onDownload: () -> Unit,
    onOpenTrip: () -> Unit,
    onAddToTrips: () -> Unit,
    isDeviceConnected: Boolean
) {
    val haptic = LocalHapticFeedback.current
    val interactionSource = remember { MutableInteractionSource() }
    val isPressed by interactionSource.collectIsPressedAsState()

    // Bounce animation
    val scale by animateFloatAsState(
        targetValue = if (isPressed) 0.95f else 1f,
        animationSpec = spring(
            dampingRatio = Spring.DampingRatioMediumBouncy,
            stiffness = Spring.StiffnessHigh
        ),
        label = "cardScale"
    )

    val isDownloaded = localTrip != null
    val canOpen = isDownloaded && !isDownloading
    val canDownload = isDeviceConnected && !isDownloading && !isDownloaded
    val canAddToTrips = isDownloaded && !isDownloading && !isAddedToTrips

    // Determine card state and colors
    val (statusLabel, statusColor, backgroundTint) = when {
        isDownloading -> Triple("Downloading", RecordingDownloadingColor, RecordingDownloadingColor.copy(alpha = 0.08f))
        isAddedToTrips -> Triple("In Trips", RecordingAddedColor, RecordingAddedColor.copy(alpha = 0.08f))
        isDownloaded -> Triple("Downloaded", RecordingDownloadedColor, RecordingDownloadedColor.copy(alpha = 0.08f))
        isDeviceConnected -> Triple("On Device", RecordingOnDeviceColor, Color.Transparent)
        else -> Triple("Offline", MaterialTheme.colorScheme.outline, Color.Transparent)
    }

    // Animate progress fill
    val animatedProgress by animateFloatAsState(
        targetValue = if (isDownloading && progress != null) progress else 0f,
        animationSpec = spring(
            dampingRatio = Spring.DampingRatioMediumBouncy,
            stiffness = Spring.StiffnessMedium
        ),
        label = "progressFill"
    )

    Card(
        modifier = Modifier
            .fillMaxWidth()
            .graphicsLayer {
                scaleX = scale
                scaleY = scale
            }
            .clickable(
                interactionSource = interactionSource,
                indication = null,
                enabled = canOpen || canDownload,
                onClick = {
                    haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                    if (isDownloaded) onOpenTrip() else onDownload()
                }
            ),
        elevation = CardDefaults.cardElevation(defaultElevation = 2.dp)
    ) {
        Box(modifier = Modifier.fillMaxWidth()) {
            // Background tint based on state
            if (!isDownloading) {
                Box(
                    modifier = Modifier
                        .matchParentSize()
                        .background(backgroundTint)
                )
            }

            // Content
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(20.dp),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                // Left side: Trip name and status
                Column(modifier = Modifier.weight(1f)) {
                    Text(
                        text = recording.name,
                        style = MaterialTheme.typography.titleLarge,
                        fontWeight = FontWeight.Bold,
                        color = MaterialTheme.colorScheme.onSurface
                    )
                    Spacer(modifier = Modifier.height(6.dp))

                    Row(
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(10.dp)
                    ) {
                        // Hint text
                        Text(
                            text = when {
                                isDownloading -> "Downloading..."
                                isDownloaded -> "Tap to open"
                                isDeviceConnected -> "Tap to download"
                                else -> "Connect device"
                            },
                            style = MaterialTheme.typography.bodyMedium,
                            color = if (isDownloading) statusColor else MaterialTheme.colorScheme.onSurfaceVariant
                        )

                        // Status badge
                        Surface(
                            color = statusColor,
                            shape = MaterialTheme.shapes.small
                        ) {
                            Text(
                                text = statusLabel,
                                style = MaterialTheme.typography.labelSmall,
                                color = Color.White,
                                modifier = Modifier.padding(horizontal = 8.dp, vertical = 4.dp)
                            )
                        }
                    }
                }

                // Right side: Add to Trips button (only when downloaded and not yet added)
                if (canAddToTrips) {
                    TextButton(
                        onClick = {
                            haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                            onAddToTrips()
                        }
                    ) {
                        Text("Add to Trips")
                    }
                }
            }

            // Progress fill overlay for downloading
            if (isDownloading) {
                Box(
                    modifier = Modifier
                        .matchParentSize()
                        .graphicsLayer {
                            clip = true
                            scaleX = animatedProgress
                            transformOrigin = androidx.compose.ui.graphics.TransformOrigin(0f, 0.5f)
                        }
                        .background(RecordingDownloadingColor.copy(alpha = 0.2f))
                )
            }
        }
    }
}
