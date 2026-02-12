package com.example.kolomapa2.ui

import androidx.compose.animation.core.Spring
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.spring
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsPressedAsState
import androidx.compose.foundation.layout.*
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowForward
import androidx.compose.material.icons.filled.Home
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun NavigateHomeCard(
    isEspConnected: Boolean,
    hasInternet: Boolean,
    isActive: Boolean = false,
    onClick: () -> Unit
) {
    // Haptic feedback
    val haptic = LocalHapticFeedback.current

    // Interaction source to detect press state
    val interactionSource = remember { MutableInteractionSource() }
    val isPressed by interactionSource.collectIsPressedAsState()

    // Determine if button is enabled
    val isEnabled = isEspConnected && hasInternet

    // Bounce animation based on press state (only when enabled)
    val scale by animateFloatAsState(
        targetValue = if (isPressed && isEnabled) 0.95f else 1f,
        animationSpec = spring(
            dampingRatio = Spring.DampingRatioMediumBouncy,
            stiffness = Spring.StiffnessHigh
        ),
        label = "navigateHomeCardScale"
    )

    Card(
        modifier = Modifier
            .fillMaxWidth()
            .graphicsLayer {
                scaleX = scale
                scaleY = scale
            }
            .then(
                if (isEnabled) {
                    Modifier.clickable(
                        interactionSource = interactionSource,
                        indication = null,
                        onClick = {
                            haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                            onClick()
                        }
                    )
                } else {
                    Modifier
                }
            ),
        elevation = CardDefaults.cardElevation(defaultElevation = if (isActive) 6.dp else 4.dp),
        colors = CardDefaults.cardColors(
            containerColor = when {
                !isEnabled -> MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.5f) // Gray when disabled
                isActive -> Color(0xFF0F766E) // Darker teal when active
                else -> Color(0xFF1B4D48) // Dark greenish-cyan when inactive but enabled
            }
        )
    ) {
        Box(
            modifier = Modifier.fillMaxWidth()
        ) {
            // Gradient overlay for visual appeal
            if (isEnabled) {
                Box(
                    modifier = Modifier
                        .matchParentSize()
                        .background(
                            androidx.compose.ui.graphics.Brush.horizontalGradient(
                                colors = if (isActive) {
                                    listOf(
                                        Color(0xFF0F766E).copy(alpha = 0.9f), // Dark teal
                                        Color(0xFF115E59).copy(alpha = 0.9f)  // Deeper teal
                                    )
                                } else {
                                    listOf(
                                        Color(0xFF1B4D48).copy(alpha = 0.9f), // Dark greenish-cyan
                                        Color(0xFF173D39).copy(alpha = 0.9f)  // Deeper dark teal
                                    )
                                }
                            )
                        )
                )
            }

            // Content
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(24.dp),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                // Left side: Home icon and text
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(16.dp)
                ) {
                    // Home icon
                    Icon(
                        imageVector = Icons.Default.Home,
                        contentDescription = "Navigate Home",
                        modifier = Modifier.size(48.dp),
                        tint = if (isEnabled) Color.White else MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.5f)
                    )

                    // Text column
                    Column {
                        Text(
                            "Navigate Home",
                            style = MaterialTheme.typography.titleLarge,
                            fontWeight = FontWeight.Bold,
                            color = if (isEnabled) Color.White else MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.5f)
                        )
                        if (isActive) {
                            Spacer(modifier = Modifier.height(6.dp))
                            Surface(
                                color = Color(0xFF10B981),
                                shape = MaterialTheme.shapes.small
                            ) {
                                Text(
                                    "Active",
                                    style = MaterialTheme.typography.labelSmall,
                                    color = Color.White,
                                    modifier = Modifier.padding(horizontal = 8.dp, vertical = 4.dp)
                                )
                            }
                        }
                        Spacer(modifier = Modifier.height(4.dp))

                        // Status text
                        Text(
                            when {
                                !isEspConnected && !hasInternet -> "Device offline • No internet"
                                !isEspConnected -> "Device not connected"
                                !hasInternet -> "No internet connection"
                                isActive -> "Navigating home..."
                                else -> "Tap to navigate home"
                            },
                            style = MaterialTheme.typography.bodyMedium,
                            color = if (isEnabled) Color.White.copy(alpha = 0.9f) else MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.5f)
                        )
                    }
                }

                // Right side: Arrow icon (only when enabled)
                if (isEnabled) {
                    Icon(
                        imageVector = Icons.Default.ArrowForward,
                        contentDescription = "Go",
                        modifier = Modifier.size(32.dp),
                        tint = Color.White
                    )
                }
            }
        }
    }
}
