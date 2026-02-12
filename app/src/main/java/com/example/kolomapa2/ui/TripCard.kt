package com.example.kolomapa2.ui

import androidx.compose.animation.core.Spring
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.spring
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsPressedAsState
import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clipToBounds
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.example.kolomapa2.models.Trip
import com.example.kolomapa2.ui.theme.TripOnDeviceBlue
import com.example.kolomapa2.ui.theme.TripOnDeviceTint
import com.example.kolomapa2.ui.theme.TripTransferBlue
import com.example.kolomapa2.ui.theme.TripTransferBlueLight
import com.example.kolomapa2.utils.BleService
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import kotlin.math.roundToInt

// Import DownloadProgress from MainViewModel
import com.example.kolomapa2.MainViewModel

// Helper function to format distance with one decimal place (omitting .0)
private fun formatDistance(distanceInMeters: Double): String {
    val km = distanceInMeters / 1000
    return if (km % 1.0 == 0.0) {
        km.toInt().toString()
    } else {
        String.format(Locale.US, "%.1f", km)
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun TripCard(
    trip: Trip,
    onClick: () -> Unit,
    transferState: BleService.ServiceState.Transferring? = null,
    downloadProgress: MainViewModel.DownloadProgress? = null,
    isOnEsp: Boolean = false,
    isActive: Boolean = false
) {
    // Haptic feedback
    val haptic = LocalHapticFeedback.current

    // Interaction source to detect press state
    val interactionSource = remember { MutableInteractionSource() }
    val isPressed by interactionSource.collectIsPressedAsState()

    // Bounce animation based on press state (faster response)
    val scale by animateFloatAsState(
        targetValue = if (isPressed) 0.95f else 1f,
        animationSpec = spring(
            dampingRatio = Spring.DampingRatioMediumBouncy,
            stiffness = Spring.StiffnessHigh
        ),
        label = "tripCardScale"
    )

    // Format the timestamp (European format: DD.MM.YYYY)
    val dateFormat = SimpleDateFormat("dd.MM.yyyy", Locale.getDefault())
    val dateAdded = dateFormat.format(Date(trip.metadata.createdAt))

    // Check if this trip is being transferred or downloaded
    val isTransferring = transferState?.tripFileName == trip.metadata.fileName
    val isDownloading = downloadProgress != null

    // Debug logging
    if (downloadProgress != null) {
        android.util.Log.d("TripCard", "Trip ${trip.metadata.fileName} is downloading: ${downloadProgress.current}/${downloadProgress.total}")
    }

    val transferProgress = if (isTransferring) transferState?.percentage ?: 0 else 0
    val downloadProgressValue = if (isDownloading && downloadProgress.total > 0) {
        ((downloadProgress.current.toFloat() / downloadProgress.total) * 100).toInt()
    } else 0
    val tileProgressLabel = if (isTransferring) {
        val sent = transferState?.tilesSent
        val total = transferState?.tilesTotal
        if (sent != null && total != null && total > 0) {
            "${sent.coerceAtMost(total)}/$total tiles"
        } else {
            null
        }
    } else {
        null
    }

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
                onClick = {
                    haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                    onClick()
                }
            ),
        elevation = CardDefaults.cardElevation(defaultElevation = 2.dp)
    ) {
        Box(
            modifier = Modifier.fillMaxWidth()
        ) {
            // Background color overlay based on state
            if (!isTransferring && !isDownloading) {
                Box(
                    modifier = Modifier
                        .matchParentSize()
                                .background(
                            when {
                                isActive -> Color(0xFF10B981).copy(alpha = 0.08f) // Green tint for active
                                isOnEsp -> TripOnDeviceTint // Blue tint for on device
                                else -> Color.Transparent
                            }
                        )
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
                // Left side: Trip name and date with status badge
                Column(
                    modifier = Modifier.weight(1f)
                ) {
                    Text(
                        trip.metadata.name,
                        style = MaterialTheme.typography.titleLarge,
                        fontWeight = FontWeight.Bold,
                        color = MaterialTheme.colorScheme.onSurface
                    )
                    Spacer(modifier = Modifier.height(4.dp))

                    // Date and status badge in a row
                    Row(
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        // Show download progress text or date
                        if (isDownloading) {
                            Text(
                                downloadProgress?.message ?: "Downloading...",
                                style = MaterialTheme.typography.bodyMedium,
                                color = Color(0xFFF59E0B), // Amber color for downloading
                                fontWeight = FontWeight.Medium
                            )
                        } else if (tileProgressLabel != null) {
                            Text(
                                tileProgressLabel,
                                style = MaterialTheme.typography.bodyMedium,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                                fontWeight = FontWeight.Medium
                            )
                        } else {
                            Text(
                                dateAdded,
                                style = MaterialTheme.typography.bodyMedium,
                                color = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                        }

                        // Status badge next to date
                        when {
                            isDownloading -> {
                                Surface(
                                    color = Color(0xFFF59E0B), // Amber for downloading
                                    shape = MaterialTheme.shapes.small
                                ) {
                                    Text(
                                        "Downloading",
                                        style = MaterialTheme.typography.labelSmall,
                                        color = Color.White,
                                        modifier = Modifier.padding(horizontal = 8.dp, vertical = 4.dp)
                                    )
                                }
                            }
                            tileProgressLabel != null -> {
                                Surface(
                                    color = TripTransferBlue,
                                    shape = MaterialTheme.shapes.small
                                ) {
                                    Text(
                                        "Transferring",
                                        style = MaterialTheme.typography.labelSmall,
                                        color = Color.White,
                                        modifier = Modifier.padding(horizontal = 8.dp, vertical = 4.dp)
                                    )
                                }
                            }
                            isActive || isOnEsp -> {
                                Surface(
                                    color = when {
                                        isActive -> Color(0xFF10B981) // Green for active trip
                                        else -> TripOnDeviceBlue // Blue for on device
                                    },
                                    shape = MaterialTheme.shapes.small
                                ) {
                                    Text(
                                        when {
                                            isActive -> "Active"
                                            else -> "On Device"
                                        },
                                        style = MaterialTheme.typography.labelSmall,
                                        color = when {
                                            isActive -> Color.White
                                            else -> Color.White
                                        },
                                        modifier = Modifier.padding(horizontal = 8.dp, vertical = 4.dp)
                                    )
                                }
                            }
                        }
                    }
                }

                // Right side: Distance
                Column(
                    horizontalAlignment = Alignment.End
                ) {
                    Text(
                        formatDistance(trip.metadata.totalDistance),
                        style = MaterialTheme.typography.headlineMedium,
                        fontWeight = FontWeight.Bold,
                        color = MaterialTheme.colorScheme.primary
                    )
                    Text(
                        "km",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
            }

            // Progress fill overlay (placed after content to overlay on top)
            if (isTransferring || isDownloading) {
                Box(
                    modifier = Modifier
                        .matchParentSize()
                        .graphicsLayer {
                            clip = true
                            scaleX = animatedProgress
                            transformOrigin = androidx.compose.ui.graphics.TransformOrigin(0f, 0.5f)
                        }
                        .background(
                            when {
                                isDownloading -> Color(0xFFF59E0B).copy(alpha = 0.2f) // Amber for downloading
                                else -> TripTransferBlueLight.copy(alpha = 0.28f) // Blue for transferring
                            }
                        )
                )
            }
        }
    }
}
