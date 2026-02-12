package com.example.kolomapa2.ui

import androidx.compose.animation.*
import androidx.compose.animation.core.*
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.*
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.outlined.InsertDriveFile
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.Route
import androidx.compose.material.icons.outlined.Apps
import androidx.compose.material.icons.outlined.History
import androidx.compose.material.icons.outlined.Tune
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.unit.dp
import androidx.compose.ui.zIndex

/**
 * Material 3 Expressive Floating Action Button Menu
 * Expands to show ExtendedFABs with integrated text
 */
@OptIn(ExperimentalMaterial3ExpressiveApi::class)
@Composable
fun AddTripFabMenu(
    onLoadFromGpxClick: () -> Unit,
    onPlanRouteClick: () -> Unit,
    onRecordedTripsClick: () -> Unit,
    onSettingsClick: () -> Unit,
    modifier: Modifier = Modifier
) {
    var isExpanded by remember { mutableStateOf(false) }
    val haptic = LocalHapticFeedback.current

    // Animation values for main FAB
    val rotation by animateFloatAsState(
        targetValue = if (isExpanded) 45f else 0f,
        animationSpec = spring(
            dampingRatio = Spring.DampingRatioMediumBouncy,
            stiffness = Spring.StiffnessLow
        ),
        label = "fabRotation"
    )

    Box(
        modifier = modifier,
        contentAlignment = Alignment.BottomEnd
    ) {
        // Scrim overlay when expanded
        AnimatedVisibility(
            visible = isExpanded,
            enter = fadeIn(animationSpec = tween(200)),
            exit = fadeOut(animationSpec = tween(200))
        ) {
            Box(
                modifier = Modifier
                    .fillMaxSize()
                    .zIndex(1f)
                    .clickable(
                        interactionSource = remember { MutableInteractionSource() },
                        indication = null
                    ) {
                        isExpanded = false
                    }
            )
        }

        Column(
            modifier = Modifier
                .zIndex(2f)
                .wrapContentSize(Alignment.BottomEnd, unbounded = true),
            horizontalAlignment = Alignment.End,
            verticalArrangement = Arrangement.spacedBy(16.dp)
        ) {
            // Settings option
            AnimatedVisibility(
                visible = isExpanded,
                enter = fadeIn(
                    animationSpec = tween(durationMillis = 200, delayMillis = 0)
                ) + slideInVertically(
                    animationSpec = spring(
                        dampingRatio = Spring.DampingRatioMediumBouncy,
                        stiffness = Spring.StiffnessMedium
                    ),
                    initialOffsetY = { it / 3 }
                ) + scaleIn(
                    animationSpec = spring(
                        dampingRatio = Spring.DampingRatioMediumBouncy,
                        stiffness = Spring.StiffnessMedium
                    ),
                    initialScale = 0.8f
                ),
                exit = fadeOut(
                    animationSpec = tween(durationMillis = 150)
                ) + slideOutVertically(
                    animationSpec = tween(durationMillis = 150),
                    targetOffsetY = { it / 3 }
                ) + scaleOut(
                    animationSpec = tween(durationMillis = 150),
                    targetScale = 0.8f
                )
            ) {
                ExtendedFloatingActionButton(
                    onClick = {
                        haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                        isExpanded = false
                        onSettingsClick()
                    },
                    containerColor = Color(0xFF6B7280),
                    contentColor = Color.White,
                    elevation = FloatingActionButtonDefaults.elevation(
                        defaultElevation = 6.dp,
                        pressedElevation = 8.dp,
                        hoveredElevation = 8.dp
                    )
                ) {
                    Icon(
                        imageVector = Icons.Outlined.Tune,
                        contentDescription = null,
                        modifier = Modifier.size(24.dp)
                    )
                    Spacer(modifier = Modifier.width(12.dp))
                    Text(
                        text = "Tile Settings",
                        style = MaterialTheme.typography.labelLarge
                    )
                }
            }

            // Plan Route option
            AnimatedVisibility(
                visible = isExpanded,
                enter = fadeIn(
                    animationSpec = tween(durationMillis = 200, delayMillis = 50)
                ) + slideInVertically(
                    animationSpec = spring(
                        dampingRatio = Spring.DampingRatioMediumBouncy,
                        stiffness = Spring.StiffnessMedium
                    ),
                    initialOffsetY = { it / 3 }
                ) + scaleIn(
                    animationSpec = spring(
                        dampingRatio = Spring.DampingRatioMediumBouncy,
                        stiffness = Spring.StiffnessMedium
                    ),
                    initialScale = 0.8f
                ),
                exit = fadeOut(
                    animationSpec = tween(durationMillis = 150)
                ) + slideOutVertically(
                    animationSpec = tween(durationMillis = 150),
                    targetOffsetY = { it / 3 }
                ) + scaleOut(
                    animationSpec = tween(durationMillis = 150),
                    targetScale = 0.8f
                )
            ) {
                ExtendedFloatingActionButton(
                    onClick = {
                        haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                        isExpanded = false
                        onPlanRouteClick()
                    },
                    containerColor = Color(0xFFF59E0B),
                    contentColor = Color.White,
                    elevation = FloatingActionButtonDefaults.elevation(
                        defaultElevation = 6.dp,
                        pressedElevation = 8.dp,
                        hoveredElevation = 8.dp
                    )
                ) {
                    Icon(
                        imageVector = Icons.Default.Route,
                        contentDescription = null,
                        modifier = Modifier.size(24.dp)
                    )
                    Spacer(modifier = Modifier.width(12.dp))
                    Text(
                        text = "Plan Route",
                        style = MaterialTheme.typography.labelLarge
                    )
                }
            }

            // Recorded Trips option
            AnimatedVisibility(
                visible = isExpanded,
                enter = fadeIn(
                    animationSpec = tween(durationMillis = 200, delayMillis = 100)
                ) + slideInVertically(
                    animationSpec = spring(
                        dampingRatio = Spring.DampingRatioMediumBouncy,
                        stiffness = Spring.StiffnessMedium
                    ),
                    initialOffsetY = { it / 3 }
                ) + scaleIn(
                    animationSpec = spring(
                        dampingRatio = Spring.DampingRatioMediumBouncy,
                        stiffness = Spring.StiffnessMedium
                    ),
                    initialScale = 0.8f
                ),
                exit = fadeOut(
                    animationSpec = tween(durationMillis = 150)
                ) + slideOutVertically(
                    animationSpec = tween(durationMillis = 150),
                    targetOffsetY = { it / 3 }
                ) + scaleOut(
                    animationSpec = tween(durationMillis = 150),
                    targetScale = 0.8f
                )
            ) {
                ExtendedFloatingActionButton(
                    onClick = {
                        haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                        isExpanded = false
                        onRecordedTripsClick()
                    },
                    containerColor = Color(0xFF14B8A6),
                    contentColor = Color.White,
                    elevation = FloatingActionButtonDefaults.elevation(
                        defaultElevation = 6.dp,
                        pressedElevation = 8.dp,
                        hoveredElevation = 8.dp
                    )
                ) {
                    Icon(
                        imageVector = Icons.Outlined.History,
                        contentDescription = null,
                        modifier = Modifier.size(24.dp)
                    )
                    Spacer(modifier = Modifier.width(12.dp))
                    Text(
                        text = "Recorded Trips",
                        style = MaterialTheme.typography.labelLarge
                    )
                }
            }

            // Load from GPX option
            AnimatedVisibility(
                visible = isExpanded,
                enter = fadeIn(
                    animationSpec = tween(durationMillis = 200, delayMillis = 150)
                ) + slideInVertically(
                    animationSpec = spring(
                        dampingRatio = Spring.DampingRatioMediumBouncy,
                        stiffness = Spring.StiffnessMedium
                    ),
                    initialOffsetY = { it / 3 }
                ) + scaleIn(
                    animationSpec = spring(
                        dampingRatio = Spring.DampingRatioMediumBouncy,
                        stiffness = Spring.StiffnessMedium
                    ),
                    initialScale = 0.8f
                ),
                exit = fadeOut(
                    animationSpec = tween(durationMillis = 150)
                ) + slideOutVertically(
                    animationSpec = tween(durationMillis = 150),
                    targetOffsetY = { it / 3 }
                ) + scaleOut(
                    animationSpec = tween(durationMillis = 150),
                    targetScale = 0.8f
                )
            ) {
                ExtendedFloatingActionButton(
                    onClick = {
                        haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                        isExpanded = false
                        onLoadFromGpxClick()
                    },
                    containerColor = Color(0xFF2563EB),
                    contentColor = Color.White,
                    elevation = FloatingActionButtonDefaults.elevation(
                        defaultElevation = 6.dp,
                        pressedElevation = 8.dp,
                        hoveredElevation = 8.dp
                    )
                ) {
                    Icon(
                        imageVector = Icons.AutoMirrored.Outlined.InsertDriveFile,
                        contentDescription = null,
                        modifier = Modifier.size(24.dp)
                    )
                    Spacer(modifier = Modifier.width(12.dp))
                    Text(
                        text = "Load from GPX",
                        style = MaterialTheme.typography.labelLarge
                    )
                }
            }

            // Main FAB - now with menu icon instead of +
            FloatingActionButton(
                onClick = {
                    haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                    isExpanded = !isExpanded
                },
                modifier = Modifier.graphicsLayer {
                    rotationZ = rotation
                },
                containerColor = MaterialTheme.colorScheme.primaryContainer,
                contentColor = MaterialTheme.colorScheme.onPrimaryContainer
            ) {
                Icon(
                    imageVector = if (isExpanded) Icons.Filled.Close else Icons.Outlined.Apps,
                    contentDescription = if (isExpanded) "Close menu" else "Open menu"
                )
            }
        }
    }
}
