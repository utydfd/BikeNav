package com.example.kolomapa2.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.*
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.input.pointer.PointerEventType
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.input.pointer.positionChange
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.text.*
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.example.kolomapa2.models.ElevationProfile
import com.example.kolomapa2.models.ElevationProfilePoint
import java.util.Locale
import kotlin.math.abs
import kotlin.math.roundToInt

/**
 * Interactive elevation profile chart using Compose Canvas
 * Displays elevation data with gradient fill and press-and-drag interaction
 * Features velocity-based haptic feedback while dragging
 */
@Composable
fun ElevationProfileChart(
    profile: ElevationProfile?,
    selectedPoint: ElevationProfilePoint? = null,
    onPointSelected: (ElevationProfilePoint?) -> Unit = {},
    modifier: Modifier = Modifier
) {
    val primaryColor = MaterialTheme.colorScheme.primary
    val onSurfaceColor = MaterialTheme.colorScheme.onSurface
    val onSurfaceVariantColor = MaterialTheme.colorScheme.onSurfaceVariant
    val surfaceVariantColor = MaterialTheme.colorScheme.surfaceVariant
    val secondaryColor = MaterialTheme.colorScheme.secondary
    val haptic = LocalHapticFeedback.current

    Card(
        modifier = modifier.fillMaxWidth(),
        elevation = CardDefaults.cardElevation(defaultElevation = 2.dp)
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(12.dp)
        ) {
            Text(
                "Elevation Profile",
                style = MaterialTheme.typography.labelSmall,
                color = onSurfaceVariantColor
            )

            Spacer(modifier = Modifier.height(8.dp))

            if (profile == null || profile.points.isEmpty()) {
                // Placeholder for no data
                Box(
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(160.dp),
                    contentAlignment = androidx.compose.ui.Alignment.Center
                ) {
                    Text(
                        "No elevation data available",
                        style = MaterialTheme.typography.bodyMedium,
                        color = onSurfaceVariantColor
                    )
                }
            } else {
                val density = LocalDensity.current

                // Padding values in pixels
                val leftPaddingPx = with(density) { 44.dp.toPx() }
                val rightPaddingPx = with(density) { 16.dp.toPx() }
                val topPaddingPx = with(density) { 16.dp.toPx() }
                val bottomPaddingPx = with(density) { 28.dp.toPx() }

                // Text measurer for labels
                val textMeasurer = rememberTextMeasurer()

                // Track velocity for haptic feedback
                var lastUpdateTime by remember { mutableLongStateOf(0L) }
                var lastX by remember { mutableFloatStateOf(0f) }
                var accumulatedDistance by remember { mutableFloatStateOf(0f) }

                // Haptic feedback threshold - triggers feedback every N pixels of movement
                val hapticThresholdBase = with(density) { 12.dp.toPx() }

                Canvas(
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(160.dp)
                        .pointerInput(profile) {
                            awaitEachGesture {
                                // Wait for first touch
                                val down = awaitFirstDown(requireUnconsumed = false)
                                val chartWidth = size.width - leftPaddingPx - rightPaddingPx
                                val relativeX = down.position.x - leftPaddingPx

                                // Only respond to touches within the chart area (not Y-axis labels)
                                if (relativeX < 0f || relativeX > chartWidth) {
                                    return@awaitEachGesture
                                }

                                // Helper to find point at x position
                                fun findPointAtX(x: Float): ElevationProfilePoint? {
                                    val rx = x - leftPaddingPx
                                    return if (rx in 0f..chartWidth && profile.totalDistance > 0) {
                                        val fraction = rx / chartWidth
                                        val distance = fraction * profile.totalDistance
                                        profile.points.minByOrNull {
                                            abs(it.distanceFromStart - distance)
                                        }
                                    } else null
                                }

                                // Wait a bit to distinguish between scroll and chart interaction
                                // Track initial position to detect vertical scrolling
                                val startY = down.position.y
                                var totalVerticalMovement = 0f
                                var totalHorizontalMovement = 0f
                                var isInteracting = false

                                // Don't immediately start interacting - wait for more input
                                do {
                                    val event = awaitPointerEvent()
                                    val change = event.changes.firstOrNull() ?: break

                                    if (!change.pressed) break

                                    val deltaX = abs(change.position.x - down.position.x)
                                    val deltaY = abs(change.position.y - startY)
                                    totalHorizontalMovement += abs(change.positionChange().x)
                                    totalVerticalMovement += abs(change.positionChange().y)

                                    // If user is scrolling vertically more than horizontally, let them scroll
                                    if (!isInteracting && totalVerticalMovement > totalHorizontalMovement && totalVerticalMovement > 20f) {
                                        // User wants to scroll, don't consume events
                                        return@awaitEachGesture
                                    }

                                    // Start interacting if horizontal movement is dominant or held long enough
                                    if (!isInteracting && (totalHorizontalMovement > 15f || deltaX > 10f)) {
                                        isInteracting = true
                                        // Initial haptic feedback when starting interaction
                                        haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                                        val initialPoint = findPointAtX(down.position.x)
                                        onPointSelected(initialPoint)
                                        lastX = down.position.x
                                        lastUpdateTime = System.currentTimeMillis()
                                        accumulatedDistance = 0f
                                    }

                                    if (isInteracting && event.type == PointerEventType.Move) {
                                        val currentX = change.position.x
                                        val currentTime = System.currentTimeMillis()

                                        // Calculate velocity (pixels per second)
                                        val timeDelta = (currentTime - lastUpdateTime).coerceAtLeast(1)
                                        val xDelta = abs(currentX - lastX)
                                        val velocity = (xDelta / timeDelta) * 1000f

                                        // Accumulate distance for haptic threshold
                                        accumulatedDistance += xDelta

                                        // Adjust haptic threshold based on velocity
                                        val velocityFactor = when {
                                            velocity > 2000f -> 0.3f  // Very fast - vibrate often
                                            velocity > 1000f -> 0.5f  // Fast
                                            velocity > 500f -> 0.7f   // Medium
                                            velocity > 200f -> 1.0f   // Normal
                                            else -> 1.5f              // Slow - vibrate less often
                                        }
                                        val adjustedThreshold = hapticThresholdBase * velocityFactor

                                        // Trigger haptic feedback when accumulated distance exceeds threshold
                                        if (accumulatedDistance >= adjustedThreshold) {
                                            haptic.performHapticFeedback(HapticFeedbackType.TextHandleMove)
                                            accumulatedDistance = 0f
                                        }

                                        // Update selected point
                                        val point = findPointAtX(currentX)
                                        if (point != null) {
                                            onPointSelected(point)
                                        }

                                        lastX = currentX
                                        lastUpdateTime = currentTime
                                        change.consume()
                                    }
                                } while (event.changes.any { it.pressed })
                            }
                        }
                ) {
                    val chartWidth = size.width - leftPaddingPx - rightPaddingPx
                    val chartHeight = size.height - topPaddingPx - bottomPaddingPx

                    if (chartWidth <= 0 || chartHeight <= 0) return@Canvas

                    // Draw the chart
                    drawElevationChart(
                        profile = profile,
                        selectedPoint = selectedPoint,
                        chartWidth = chartWidth,
                        chartHeight = chartHeight,
                        leftPadding = leftPaddingPx,
                        topPadding = topPaddingPx,
                        bottomPadding = bottomPaddingPx,
                        primaryColor = primaryColor,
                        secondaryColor = secondaryColor,
                        onSurfaceColor = onSurfaceColor,
                        onSurfaceVariantColor = onSurfaceVariantColor,
                        surfaceVariantColor = surfaceVariantColor,
                        textMeasurer = textMeasurer
                    )
                }

                // Show selected point info below chart
                if (selectedPoint != null) {
                    Spacer(modifier = Modifier.height(8.dp))
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceEvenly
                    ) {
                        Column(horizontalAlignment = androidx.compose.ui.Alignment.CenterHorizontally) {
                            Text(
                                "Elevation",
                                style = MaterialTheme.typography.labelSmall,
                                color = onSurfaceVariantColor
                            )
                            Text(
                                "${selectedPoint.elevation.roundToInt()} m",
                                style = MaterialTheme.typography.titleMedium,
                                fontWeight = FontWeight.Bold,
                                color = primaryColor
                            )
                        }
                        Column(horizontalAlignment = androidx.compose.ui.Alignment.CenterHorizontally) {
                            Text(
                                "Distance",
                                style = MaterialTheme.typography.labelSmall,
                                color = onSurfaceVariantColor
                            )
                            Text(
                                formatDistanceKm(selectedPoint.distanceFromStart),
                                style = MaterialTheme.typography.titleMedium,
                                fontWeight = FontWeight.Bold,
                                color = secondaryColor
                            )
                        }
                    }
                }
            }
        }
    }
}

private fun DrawScope.drawElevationChart(
    profile: ElevationProfile,
    selectedPoint: ElevationProfilePoint?,
    chartWidth: Float,
    chartHeight: Float,
    leftPadding: Float,
    topPadding: Float,
    bottomPadding: Float,
    primaryColor: Color,
    secondaryColor: Color,
    onSurfaceColor: Color,
    onSurfaceVariantColor: Color,
    surfaceVariantColor: Color,
    textMeasurer: TextMeasurer
) {
    val points = profile.points
    if (points.isEmpty() || profile.totalDistance <= 0) return

    // Calculate scales
    val xScale = chartWidth / profile.totalDistance.toFloat()
    val yScale = chartHeight / profile.elevationRange.toFloat()

    // Helper to convert data to canvas coordinates
    fun dataToCanvas(distance: Double, elevation: Double): Offset {
        val x = leftPadding + (distance * xScale).toFloat()
        val y = topPadding + chartHeight - ((elevation - profile.minElevation) * yScale).toFloat()
        return Offset(x, y)
    }

    // Draw grid lines (horizontal)
    val gridLineCount = 3
    for (i in 0..gridLineCount) {
        val elevation = profile.minElevation + (profile.elevationRange * i / gridLineCount)
        val y = dataToCanvas(0.0, elevation).y
        drawLine(
            color = surfaceVariantColor,
            start = Offset(leftPadding, y),
            end = Offset(leftPadding + chartWidth, y),
            strokeWidth = 1f
        )
    }

    // Build path for elevation line
    val linePath = Path().apply {
        points.forEachIndexed { index, point ->
            val pos = dataToCanvas(point.distanceFromStart, point.elevation)
            if (index == 0) {
                moveTo(pos.x, pos.y)
            } else {
                lineTo(pos.x, pos.y)
            }
        }
    }

    // Build path for gradient fill
    val fillPath = Path().apply {
        val firstPoint = dataToCanvas(points.first().distanceFromStart, points.first().elevation)
        val lastPoint = dataToCanvas(points.last().distanceFromStart, points.last().elevation)
        val baseY = topPadding + chartHeight

        moveTo(firstPoint.x, baseY)
        points.forEach { point ->
            val pos = dataToCanvas(point.distanceFromStart, point.elevation)
            lineTo(pos.x, pos.y)
        }
        lineTo(lastPoint.x, baseY)
        close()
    }

    // Draw gradient fill
    drawPath(
        path = fillPath,
        brush = Brush.verticalGradient(
            colors = listOf(
                primaryColor.copy(alpha = 0.3f),
                primaryColor.copy(alpha = 0.05f)
            ),
            startY = topPadding,
            endY = topPadding + chartHeight
        )
    )

    // Draw elevation line
    drawPath(
        path = linePath,
        color = primaryColor,
        style = Stroke(width = 3f, cap = StrokeCap.Round, join = StrokeJoin.Round)
    )

    // Draw X-axis labels
    val labelStyle = TextStyle(
        fontSize = 10.sp,
        color = onSurfaceVariantColor
    )

    // Start label (0 km)
    val startLabel = textMeasurer.measure("0", labelStyle)
    drawText(
        textLayoutResult = startLabel,
        topLeft = Offset(leftPadding, topPadding + chartHeight + 4f)
    )

    // End label (total distance)
    val totalKm = profile.totalDistance / 1000
    val endLabelText = if (totalKm >= 10) {
        "${totalKm.roundToInt()} km"
    } else {
        String.format(Locale.US, "%.1f km", totalKm)
    }
    val endLabel = textMeasurer.measure(endLabelText, labelStyle)
    drawText(
        textLayoutResult = endLabel,
        topLeft = Offset(leftPadding + chartWidth - endLabel.size.width, topPadding + chartHeight + 4f)
    )

    // Middle label
    if (chartWidth > 200) {
        val midDistance = profile.totalDistance / 2000
        val midLabelText = if (midDistance >= 10) {
            "${midDistance.roundToInt()}"
        } else {
            String.format(Locale.US, "%.1f", midDistance)
        }
        val midLabel = textMeasurer.measure(midLabelText, labelStyle)
        drawText(
            textLayoutResult = midLabel,
            topLeft = Offset(
                leftPadding + chartWidth / 2 - midLabel.size.width / 2,
                topPadding + chartHeight + 4f
            )
        )
    }

    // Draw Y-axis labels
    // Min elevation
    val minLabel = textMeasurer.measure("${profile.minElevation.roundToInt()}m", labelStyle)
    drawText(
        textLayoutResult = minLabel,
        topLeft = Offset(2f, topPadding + chartHeight - minLabel.size.height / 2)
    )

    // Max elevation
    val maxLabel = textMeasurer.measure("${profile.maxElevation.roundToInt()}m", labelStyle)
    drawText(
        textLayoutResult = maxLabel,
        topLeft = Offset(2f, topPadding - maxLabel.size.height / 2)
    )

    // Draw selected point indicator
    if (selectedPoint != null) {
        val selectedPos = dataToCanvas(selectedPoint.distanceFromStart, selectedPoint.elevation)

        // Vertical dashed line
        val dashLength = 8f
        val gapLength = 4f
        var currentY = selectedPos.y
        while (currentY < topPadding + chartHeight) {
            val endY = minOf(currentY + dashLength, topPadding + chartHeight)
            drawLine(
                color = secondaryColor.copy(alpha = 0.6f),
                start = Offset(selectedPos.x, currentY),
                end = Offset(selectedPos.x, endY),
                strokeWidth = 2f
            )
            currentY = endY + gapLength
        }

        // Outer circle
        drawCircle(
            color = secondaryColor,
            radius = 8f,
            center = selectedPos
        )

        // Inner circle
        drawCircle(
            color = Color.White,
            radius = 4f,
            center = selectedPos
        )
    }
}

private fun formatDistanceKm(distanceMeters: Double): String {
    val km = distanceMeters / 1000
    return if (km >= 10) {
        "${km.roundToInt()} km"
    } else {
        String.format(Locale.US, "%.1f km", km)
    }
}
