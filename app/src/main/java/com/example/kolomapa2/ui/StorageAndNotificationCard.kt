package com.example.kolomapa2.ui

import androidx.compose.animation.*
import androidx.compose.animation.core.*
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material.icons.outlined.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.rotate
import androidx.compose.ui.draw.scale
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.lerp
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.example.kolomapa2.utils.StorageStats
import kotlin.math.roundToInt

@Composable
fun BatteryIcon(
    batteryPercent: Int,
    modifier: Modifier = Modifier
) {
    val fillColor = when {
        batteryPercent > 50 -> Color(0xFF4CAF50)
        batteryPercent > 20 -> Color(0xFF2196F3)
        else -> Color(0xFFF44336)
    }

    Canvas(modifier = modifier.size(24.dp, 12.dp)) {
        val width = size.width
        val height = size.height
        val strokeWidth = 1.5.dp.toPx()
        val tipWidth = 2.dp.toPx()
        val tipHeight = 4.dp.toPx()
        val bodyWidth = width - tipWidth

        drawRoundRect(
            color = Color.Gray,
            topLeft = Offset(0f, 0f),
            size = Size(bodyWidth, height),
            cornerRadius = CornerRadius(1.dp.toPx(), 1.dp.toPx()),
            style = androidx.compose.ui.graphics.drawscope.Stroke(width = strokeWidth)
        )

        drawRoundRect(
            color = Color.Gray,
            topLeft = Offset(bodyWidth, (height - tipHeight) / 2),
            size = Size(tipWidth, tipHeight),
            cornerRadius = CornerRadius(1.dp.toPx(), 1.dp.toPx())
        )

        if (batteryPercent > 0) {
            val padding = 2.dp.toPx()
            val fillWidth = ((bodyWidth - padding * 2) * (batteryPercent / 100f)).coerceAtLeast(0f)
            if (fillWidth > 0) {
                drawRoundRect(
                    color = fillColor,
                    topLeft = Offset(padding, padding),
                    size = Size(fillWidth, height - padding * 2),
                    cornerRadius = CornerRadius(1.dp.toPx(), 1.dp.toPx())
                )
            }
        }
    }
}

private val ELEMENT_HEIGHT = 72.dp

@Composable
private fun StatBubble(
    icon: ImageVector,
    label: String,
    value: String,
    subtitle: String,
    accentColor: Color,
    modifier: Modifier = Modifier
) {
    val infiniteTransition = rememberInfiniteTransition(label = "bubble")
    val scale by infiniteTransition.animateFloat(
        initialValue = 1f,
        targetValue = 1.01f,
        animationSpec = infiniteRepeatable(
            animation = tween(2000, easing = FastOutSlowInEasing),
            repeatMode = RepeatMode.Reverse
        ),
        label = "bubbleScale"
    )

    Surface(
        modifier = modifier
            .height(ELEMENT_HEIGHT)
            .scale(scale),
        shape = RoundedCornerShape(16.dp),
        color = accentColor.copy(alpha = 0.16f)
    ) {
        Row(
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(12.dp),
            modifier = Modifier
                .fillMaxSize()
                .padding(horizontal = 14.dp, vertical = 12.dp)
        ) {
            Box(
                modifier = Modifier
                    .size(36.dp)
                    .clip(RoundedCornerShape(12.dp))
                    .background(accentColor.copy(alpha = 0.22f)),
                contentAlignment = Alignment.Center
            ) {
                Icon(
                    imageVector = icon,
                    contentDescription = null,
                    tint = accentColor,
                    modifier = Modifier.size(22.dp)
                )
            }
            Column(
                verticalArrangement = Arrangement.Center
            ) {
                Text(
                    label,
                    style = MaterialTheme.typography.labelMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                Row(
                    verticalAlignment = Alignment.Bottom,
                    horizontalArrangement = Arrangement.spacedBy(6.dp)
                ) {
                    Text(
                        value,
                        style = MaterialTheme.typography.headlineSmall.copy(
                            fontWeight = FontWeight.Bold
                        ),
                        color = MaterialTheme.colorScheme.onSurface
                    )
                    Text(
                        subtitle,
                        style = MaterialTheme.typography.labelMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.8f),
                        modifier = Modifier.padding(bottom = 3.dp)
                    )
                }
            }
        }
    }
}

@OptIn(ExperimentalMaterial3ExpressiveApi::class)
@Composable
fun StorageAndNotificationCard(
    modifier: Modifier = Modifier,
    storageStats: StorageStats?,
    notificationAccessEnabled: Boolean,
    notificationSyncEnabled: Boolean,
    onNotificationSyncToggle: (Boolean) -> Unit,
    onEnableAccessClick: () -> Unit,
    deviceConnected: Boolean = false,
    espDeviceStatus: com.example.kolomapa2.utils.BleManager.EspDeviceStatus? = null
) {
    // Animated entry
    var isVisible by rememberSaveable { mutableStateOf(false) }
    LaunchedEffect(Unit) {
        if (!isVisible) {
            isVisible = true
        }
    }

    val stats = storageStats
    val mapTilesValue = stats?.tileCount?.toString() ?: "--"
    val mapTilesSubtitle = stats?.let { "${it.totalMapSizeMB.roundToInt()} MB" } ?: "Loading..."
    val tripsValue = stats?.tripCount?.toString() ?: "--"
    val tripsSubtitle = stats?.let { "${it.totalTripSizeMB.roundToInt()} MB" } ?: "Loading..."

    val haptic = LocalHapticFeedback.current

    AnimatedVisibility(
        visible = isVisible,
        enter = fadeIn(tween(400)) + slideInVertically(
            animationSpec = spring(dampingRatio = Spring.DampingRatioLowBouncy),
            initialOffsetY = { -it / 2 }
        )
    ) {
        Column(
            modifier = modifier.fillMaxWidth(),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            val mapTilesAccent = Color(0xFFB07A45)
            val tripsAccent = Color(0xFF7C6BBA)

            // Row 1: Map Tiles + Notifications
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(12.dp)
            ) {
                // Map Tiles bubble
                StatBubble(
                    icon = Icons.Outlined.Map,
                    label = "Map Tiles",
                    value = mapTilesValue,
                    subtitle = mapTilesSubtitle,
                    accentColor = mapTilesAccent,
                    modifier = Modifier.weight(1f)
                )

                // Notification Sync Bubble
                val syncColor = when {
                    !notificationAccessEnabled -> MaterialTheme.colorScheme.error
                    notificationSyncEnabled -> Color(0xFF10B981)
                    else -> MaterialTheme.colorScheme.outline
                }

                // Pulsing animation when syncing
                val infiniteTransition = rememberInfiniteTransition(label = "sync")
                val pulseScale by infiniteTransition.animateFloat(
                    initialValue = 1f,
                    targetValue = 1.1f,
                    animationSpec = infiniteRepeatable(
                        animation = tween(1000),
                        repeatMode = RepeatMode.Reverse
                    ),
                    label = "pulse"
                )

                Surface(
                    modifier = Modifier
                        .weight(1f)
                        .height(ELEMENT_HEIGHT),
                    shape = RoundedCornerShape(16.dp),
                    color = syncColor.copy(alpha = 0.12f)
                ) {
                    Row(
                        modifier = Modifier
                            .fillMaxSize()
                            .padding(horizontal = 14.dp),
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(12.dp)
                    ) {
                        // Icon with toggle behavior when access is enabled
                        Box(
                            modifier = Modifier
                                .size(40.dp)
                                .clip(RoundedCornerShape(12.dp))
                                .background(
                                    if (notificationAccessEnabled && notificationSyncEnabled)
                                        syncColor.copy(alpha = 0.2f)
                                    else
                                        Color.Transparent
                                )
                                .then(
                                    if (notificationAccessEnabled)
                                        Modifier.clickable {
                                            haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                                            onNotificationSyncToggle(!notificationSyncEnabled)
                                        }
                                    else Modifier
                                ),
                            contentAlignment = Alignment.Center
                        ) {
                            Icon(
                                imageVector = if (notificationAccessEnabled && notificationSyncEnabled)
                                    Icons.Filled.Notifications
                                else if (notificationAccessEnabled)
                                    Icons.Outlined.Notifications
                                else
                                    Icons.Outlined.NotificationsOff,
                                contentDescription = null,
                                tint = syncColor,
                                modifier = Modifier
                                    .size(24.dp)
                                    .then(
                                        if (notificationSyncEnabled && notificationAccessEnabled)
                                            Modifier.scale(pulseScale)
                                        else Modifier
                                    )
                            )
                        }

                        Column(
                            modifier = Modifier.weight(1f)
                        ) {
                            Text(
                                "Notifications",
                                style = MaterialTheme.typography.titleSmall,
                                fontWeight = FontWeight.SemiBold,
                                color = MaterialTheme.colorScheme.onSurface
                            )
                            Text(
                                when {
                                    !notificationAccessEnabled -> "Access required"
                                    notificationSyncEnabled -> "Syncing to device"
                                    else -> "Tap icon to enable"
                                },
                                style = MaterialTheme.typography.bodySmall,
                                color = syncColor
                            )
                        }

                        if (!notificationAccessEnabled) {
                            Button(
                                onClick = onEnableAccessClick,
                                contentPadding = PaddingValues(horizontal = 12.dp, vertical = 4.dp),
                                modifier = Modifier.height(32.dp),
                                colors = ButtonDefaults.buttonColors(
                                    containerColor = syncColor
                                )
                            ) {
                                Text(
                                    "Enable",
                                    style = MaterialTheme.typography.labelSmall,
                                    fontWeight = FontWeight.Bold
                                )
                            }
                        }
                    }
                }
            }

            // Row 2: Trips + Device Status
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(12.dp)
            ) {
                // Trips bubble
                StatBubble(
                    icon = Icons.Outlined.Route,
                    label = "Trips",
                    value = tripsValue,
                    subtitle = tripsSubtitle,
                    accentColor = tripsAccent,
                    modifier = Modifier.weight(1f)
                )

                // Device Status Bubble - Always visible
                val isBleConnected = deviceConnected
                val hasStatus = espDeviceStatus != null
                val status = espDeviceStatus

                val batteryColor = when {
                    !isBleConnected -> MaterialTheme.colorScheme.outline
                    !hasStatus -> MaterialTheme.colorScheme.onSurfaceVariant
                    status != null && status.batteryPercent > 50 -> Color(0xFF4CAF50)
                    status != null && status.batteryPercent > 20 -> Color(0xFF2196F3)
                    else -> Color(0xFFF44336)
                }

                val gpsBaseColor = Color(0xFF3B82F6)
                val gpsStageColors = listOf(
                    lerp(gpsBaseColor, Color.Black, 0.35f),
                    gpsBaseColor,
                    lerp(gpsBaseColor, Color.White, 0.25f)
                )
                val gpsNoSignalColor = gpsStageColors.first().copy(alpha = 0.6f)

                val gpsColor = when {
                    !isBleConnected -> MaterialTheme.colorScheme.outline
                    !hasStatus -> MaterialTheme.colorScheme.onSurfaceVariant
                    status?.gpsStage == 3 -> gpsStageColors[2]
                    status?.gpsStage == 2 -> gpsStageColors[1]
                    status?.gpsStage == 1 -> gpsStageColors[0]
                    status?.gpsStage == 0 -> gpsNoSignalColor
                    else -> gpsNoSignalColor
                }

                val gpsStatusText = when {
                    !isBleConnected -> "Disconnected"
                    !hasStatus -> "Connected"
                    status?.gpsStage == 0 -> "No Signal"
                    status?.gpsStage == 1 -> "Time Lock"
                    status?.gpsStage == 2 -> "Date Lock"
                    status?.gpsStage == 3 -> "${status.satelliteCount} sat"
                    else -> "Unknown"
                }

                // GPS colors for segmented bar
                val gpsColors = gpsStageColors

                // Rotating animation when searching
                val gpsTransition = rememberInfiniteTransition(label = "gps")
                val rotation by gpsTransition.animateFloat(
                    initialValue = 0f,
                    targetValue = 360f,
                    animationSpec = infiniteRepeatable(
                        animation = tween(2000, easing = LinearEasing)
                    ),
                    label = "gpsRotation"
                )

                Surface(
                    modifier = Modifier
                        .weight(1f)
                        .height(ELEMENT_HEIGHT),
                    shape = RoundedCornerShape(16.dp),
                    color = if (isBleConnected) Color(0xFF3B82F6).copy(alpha = 0.12f)
                    else MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.5f)
                ) {
                    Column(
                        modifier = Modifier
                            .fillMaxSize()
                            .padding(horizontal = 14.dp, vertical = 10.dp),
                        verticalArrangement = Arrangement.SpaceEvenly
                    ) {
                        // Header row with Device title and battery
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.SpaceBetween
                        ) {
                            Text(
                                "Device",
                                style = MaterialTheme.typography.titleSmall,
                                fontWeight = FontWeight.SemiBold,
                                color = if (isBleConnected) MaterialTheme.colorScheme.onSurface
                                else MaterialTheme.colorScheme.onSurfaceVariant
                            )
                            Row(
                                verticalAlignment = Alignment.CenterVertically,
                                horizontalArrangement = Arrangement.spacedBy(4.dp)
                            ) {
                                Icon(
                                    imageVector = when {
                                        !isBleConnected -> Icons.Outlined.Battery0Bar
                                        !hasStatus -> Icons.Outlined.Battery0Bar
                                        status != null && status.batteryPercent > 80 -> Icons.Outlined.BatteryFull
                                        status != null && status.batteryPercent > 50 -> Icons.Outlined.Battery5Bar
                                        status != null && status.batteryPercent > 20 -> Icons.Outlined.Battery3Bar
                                        else -> Icons.Outlined.Battery1Bar
                                    },
                                    contentDescription = null,
                                    tint = batteryColor,
                                    modifier = Modifier.size(18.dp)
                                )
                                Text(
                                    if (isBleConnected && hasStatus && status != null) "${status.batteryPercent}%" else "--",
                                    style = MaterialTheme.typography.titleSmall,
                                    fontWeight = FontWeight.Bold,
                                    color = batteryColor
                                )
                            }
                        }

                        // GPS row with icon, segmented bar, and status
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.spacedBy(8.dp)
                        ) {
                            Icon(
                                imageVector = when {
                                    !isBleConnected -> Icons.Filled.GpsOff
                                    !hasStatus -> Icons.Filled.GpsNotFixed
                                    status?.gpsStage == 3 -> Icons.Filled.GpsFixed
                                    status?.gpsStage in 1..2 -> Icons.Filled.GpsNotFixed
                                    else -> Icons.Filled.GpsOff
                                },
                                contentDescription = null,
                                tint = gpsColor,
                                modifier = Modifier
                                    .size(18.dp)
                                    .then(
                                        if (isBleConnected && status?.gpsStage in 1..2)
                                            Modifier.rotate(rotation)
                                        else Modifier
                                    )
                            )

                            // 3-level segmented bar
                            Row(
                                modifier = Modifier.weight(1f),
                                horizontalArrangement = Arrangement.spacedBy(3.dp)
                            ) {
                                for (i in 1..3) {
                                    Box(
                                        modifier = Modifier
                                            .weight(1f)
                                            .height(6.dp)
                                            .clip(RoundedCornerShape(3.dp))
                                            .background(
                                                if (isBleConnected && hasStatus && status != null && i <= status.gpsStage)
                                                    gpsColors[i - 1]
                                                else
                                                    MaterialTheme.colorScheme.surfaceVariant
                                            )
                                    )
                                }
                            }

                            // Status text
                            Text(
                                gpsStatusText,
                                style = MaterialTheme.typography.labelMedium,
                                fontWeight = FontWeight.Medium,
                                color = gpsColor
                            )
                        }
                    }
                }
            }
        }
    }
}
