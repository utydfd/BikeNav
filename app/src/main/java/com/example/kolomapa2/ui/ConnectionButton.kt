package com.example.kolomapa2.ui

import androidx.compose.animation.*
import androidx.compose.animation.core.*
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsPressedAsState
import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.CompositingStrategy
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.zIndex
import androidx.compose.ui.layout.onGloballyPositioned
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.IntSize
import androidx.compose.ui.unit.dp
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.window.Popup
import androidx.compose.ui.window.PopupPositionProvider
import androidx.compose.ui.window.PopupProperties
import androidx.compose.ui.unit.IntRect
import androidx.compose.ui.unit.LayoutDirection
import com.example.kolomapa2.utils.BleService
import kotlin.math.roundToInt

data class ConnectionAction(
    val id: String,
    val label: String,
    val color: Color,
    val onClick: () -> Unit
)

@Composable
fun ConnectionButton(
    serviceState: BleService.ServiceState,
    transferState: BleService.ServiceState.Transferring?,
    downloadState: BleService.ServiceState.Downloading?,
    actions: List<ConnectionAction>,
    onPrimaryClick: () -> Unit
) {
    // Haptic feedback
    val haptic = LocalHapticFeedback.current

    // Interaction source to detect press state
    val interactionSource = remember { MutableInteractionSource() }
    val isPressed by interactionSource.collectIsPressedAsState()

    // Bounce animation based on press state
    val scale by animateFloatAsState(
        targetValue = if (isPressed) 0.92f else 1f,
        animationSpec = spring(
            dampingRatio = Spring.DampingRatioMediumBouncy,
            stiffness = Spring.StiffnessHigh
        ),
        label = "buttonScale"
    )

    // Trigger haptic feedback when pressed
    LaunchedEffect(isPressed) {
        if (isPressed) {
            haptic.performHapticFeedback(HapticFeedbackType.LongPress)
        }
    }

    val hasActions = actions.isNotEmpty()
    var isExpanded by remember { mutableStateOf(false) }
    LaunchedEffect(hasActions) {
        if (!hasActions) {
            isExpanded = false
        }
    }
    val transition = updateTransition(targetState = isExpanded, label = "actionExpand")

    // Traffic light colors and content
    // Derive a stable state type key that doesn't change with percentage updates
    val hasTransfer = transferState != null
    val hasDownload = downloadState != null

    val stateType = when {
        serviceState is BleService.ServiceState.Idle -> "idle"
        serviceState is BleService.ServiceState.Connecting -> "connecting"
        serviceState is BleService.ServiceState.Error -> "error"
        hasTransfer && hasDownload -> "busy"
        hasTransfer -> "transferring"
        hasDownload -> "downloading"
        serviceState is BleService.ServiceState.Connected -> "connected"
        else -> "connected"
    }

    val (targetColor, isConnecting, statusText) = when {
        serviceState is BleService.ServiceState.Idle -> Triple(Color(0xFFEF4444), false, "Connect") // Red
        serviceState is BleService.ServiceState.Connecting -> Triple(Color(0xFFF59E0B), true, "Connecting") // Amber/Yellow
        serviceState is BleService.ServiceState.Error -> Triple(Color(0xFFEF4444), false, "!") // Red
        hasTransfer && hasDownload -> {
            val transferPct = transferState?.percentage ?: 0
            val downloadPct = downloadState?.percentage ?: 0
            Triple(Color(0xFF6366F1), false, "Transfer $transferPct% + Download $downloadPct%")
        }
        hasTransfer -> {
            val pct = transferState?.percentage ?: 0
            Triple(Color(0xFF3B82F6), false, "Transferring $pct%")
        }
        hasDownload -> {
            val pct = downloadState?.percentage ?: 0
            Triple(Color(0xFF8B5CF6), false, "Downloading $pct%") // Purple for downloads
        }
        serviceState is BleService.ServiceState.Connected -> Triple(Color(0xFF10B981), false, "Connected") // Green
        else -> Triple(Color(0xFF10B981), false, "Connected")
    }

    // Animated color transition - use tween for smoother, more predictable animation
    val buttonColor by animateColorAsState(
        targetValue = targetColor,
        animationSpec = tween(
            durationMillis = 300,
            easing = FastOutSlowInEasing
        ),
        label = "buttonColor"
    )

    val primaryButtonHeight = 40.dp
    val actionHeight = primaryButtonHeight
    val actionSpacing = 6.dp
    val density = LocalDensity.current
    val configuration = LocalConfiguration.current
    var anchorPosition by remember { mutableStateOf(IntOffset.Zero) }
    var anchorSize by remember { mutableStateOf(IntSize.Zero) }
    val showPopup = hasActions && (transition.currentState || transition.targetState)

    Box(
        contentAlignment = Alignment.TopEnd
    ) {
        if (showPopup) {
            val nudgeX = with(density) { (-2).dp.roundToPx() }
            val nudgeY = with(density) { actionSpacing.roundToPx() }
            val leftMarginPx = with(density) { 8.dp.roundToPx() }
            val rightMarginPx = with(density) { 8.dp.roundToPx() }
            val popupPositionProvider = remember(anchorPosition, anchorSize, nudgeX, nudgeY, leftMarginPx, rightMarginPx) {
                object : PopupPositionProvider {
                    override fun calculatePosition(
                        anchorBounds: IntRect,
                        windowSize: IntSize,
                        layoutDirection: LayoutDirection,
                        popupContentSize: IntSize
                    ): IntOffset {
                        val anchorBottom = anchorPosition.y + anchorSize.height
                        val desiredX = anchorPosition.x + anchorSize.width - popupContentSize.width + nudgeX
                        val minX = leftMarginPx
                        val maxX = (windowSize.width - popupContentSize.width - rightMarginPx)
                            .coerceAtLeast(minX)
                        val clampedX = desiredX.coerceIn(minX, maxX)
                        return IntOffset(clampedX, anchorBottom + nudgeY)
                    }
                }
            }
            Popup(
                popupPositionProvider = popupPositionProvider,
                onDismissRequest = { isExpanded = false },
                properties = PopupProperties(focusable = true)
            ) {
                val maxPopupWidth = configuration.screenWidthDp.dp - 16.dp
                val spacingCount = (actions.size - 1).coerceAtLeast(0)
                val totalHeight = (actionHeight * actions.size) + (actionSpacing * spacingCount)
                Box(
                    modifier = Modifier
                        .widthIn(max = maxPopupWidth)
                        .height(totalHeight),
                    contentAlignment = Alignment.TopEnd
                ) {
                    actions.forEachIndexed { index, action ->
                        key(action.id) {
                            val actionInteractionSource = remember(action.id) { MutableInteractionSource() }
                            val actionPressed by actionInteractionSource.collectIsPressedAsState()
                            val actionPressScale by animateFloatAsState(
                                targetValue = if (actionPressed) 0.92f else 1f,
                                animationSpec = spring(
                                    dampingRatio = Spring.DampingRatioMediumBouncy,
                                    stiffness = Spring.StiffnessHigh
                                ),
                                label = "actionPressScale_${action.id}"
                            )
                            LaunchedEffect(actionPressed) {
                                if (actionPressed) {
                                    haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                                }
                            }
                            val actionOffset = (actionHeight + actionSpacing) * index
                            val actionOffsetPx = with(density) { actionOffset.toPx() }
                            val delayMillis = index * 45
                            val progress by transition.animateFloat(
                                transitionSpec = {
                                    if (targetState) {
                                        tween(
                                            durationMillis = 220,
                                            delayMillis = delayMillis,
                                            easing = FastOutSlowInEasing
                                        )
                                    } else {
                                        tween(
                                            durationMillis = 180,
                                            delayMillis = 0,
                                            easing = FastOutSlowInEasing
                                        )
                                    }
                                },
                                label = "actionProgress_${action.id}"
                            ) { expanded -> if (expanded) 1f else 0f }
                            val scaleFactor = 0.25f + 0.75f * progress
                            Button(
                                onClick = {
                                    action.onClick()
                                    isExpanded = false
                                },
                                enabled = progress > 0.1f,
                                colors = ButtonDefaults.buttonColors(
                                    containerColor = action.color,
                                    contentColor = Color.White
                                ),
                                modifier = Modifier
                                    .height(actionHeight)
                                    .defaultMinSize(minWidth = 0.dp)
                                    .zIndex(1f + index)
                                    .graphicsLayer {
                                        translationY = actionOffsetPx * progress
                                        scaleX = scaleFactor * actionPressScale
                                        scaleY = scaleFactor * actionPressScale
                                        alpha = progress
                                    },
                                interactionSource = actionInteractionSource,
                                contentPadding = PaddingValues(horizontal = 12.dp, vertical = 0.dp)
                            ) {
                                Text(
                                    text = action.label,
                                    style = MaterialTheme.typography.labelLarge,
                                    fontWeight = FontWeight.SemiBold,
                                    maxLines = 1,
                                    overflow = TextOverflow.Ellipsis
                                )
                            }
                        }
                    }
                }
            }
        }

        Button(
            onClick = {
                if (hasActions) {
                    haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                    isExpanded = !isExpanded
                } else {
                    onPrimaryClick()
                }
            },
            colors = ButtonDefaults.buttonColors(
                containerColor = buttonColor,
                contentColor = Color.White
            ),
            interactionSource = interactionSource,
            modifier = Modifier
                .height(primaryButtonHeight)
                .onGloballyPositioned { coordinates ->
                    val position = coordinates.localToWindow(Offset.Zero)
                    anchorPosition = IntOffset(position.x.roundToInt(), position.y.roundToInt())
                    anchorSize = coordinates.size
                }
                .graphicsLayer {
                    // Hardware acceleration for smooth animations
                    scaleX = scale
                    scaleY = scale
                    // Force render layer caching for better performance
                    compositingStrategy = CompositingStrategy.Offscreen
                }
                .animateContentSize(
                    animationSpec = tween(
                        durationMillis = 250,
                        easing = FastOutSlowInEasing
                    )
                ),
            contentPadding = PaddingValues(horizontal = 16.dp, vertical = 0.dp)
        ) {
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.Center
            ) {
                // Animated content based on state - optimized for smooth transitions
                // Use stateType as key to only animate when state changes, not on percentage updates
                key(stateType) {
                    AnimatedContent(
                        targetState = isConnecting,
                        transitionSpec = {
                        fadeIn(
                            animationSpec = tween(
                                durationMillis = 150,
                                delayMillis = 75,
                                easing = LinearEasing
                            )
                        ) + scaleIn(
                            animationSpec = tween(
                                durationMillis = 150,
                                easing = FastOutSlowInEasing
                            ),
                            initialScale = 0.85f
                        ) togetherWith fadeOut(
                            animationSpec = tween(
                                durationMillis = 75,
                                easing = LinearEasing
                            )
                        ) + scaleOut(
                            animationSpec = tween(
                                durationMillis = 150,
                                easing = FastOutSlowInEasing
                            ),
                            targetScale = 0.85f
                        )
                    },
                    label = "connectingContent"
                ) { connecting ->
                    Row(
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.Center
                    ) {
                        if (connecting) {
                            // Show loading indicator when connecting
                            CircularProgressIndicator(
                                modifier = Modifier.size(16.dp),
                                color = Color.White,
                                strokeWidth = 2.dp
                            )
                            Spacer(modifier = Modifier.width(8.dp))
                            Text(
                                text = statusText,
                                style = MaterialTheme.typography.labelLarge,
                                fontWeight = FontWeight.Bold,
                                modifier = Modifier.graphicsLayer {
                                    // Hardware acceleration for text rendering
                                    compositingStrategy = CompositingStrategy.Offscreen
                                }
                            )
                        } else {
                            // Show status dot for other states
                            Box(
                                modifier = Modifier
                                    .size(10.dp)
                                    .padding(end = 4.dp)
                            ) {
                                Surface(
                                    shape = MaterialTheme.shapes.extraLarge,
                                    color = Color.White,
                                    modifier = Modifier.fillMaxSize()
                                ) {}
                            }
                            Spacer(modifier = Modifier.width(4.dp))
                            Text(
                                text = statusText,
                                style = MaterialTheme.typography.labelLarge,
                                fontWeight = FontWeight.Bold,
                                modifier = Modifier.graphicsLayer {
                                    // Hardware acceleration for text rendering
                                    compositingStrategy = CompositingStrategy.Offscreen
                                }
                            )
                        }
                    }
                }
                }
            }
        }
    }
}

@Composable
fun getTopBarColor(serviceState: BleService.ServiceState): Color {
    return when (serviceState) {
        is BleService.ServiceState.Idle -> MaterialTheme.colorScheme.surfaceContainer
        is BleService.ServiceState.Connecting -> MaterialTheme.colorScheme.surfaceContainer.copy(alpha = 0.95f)
        is BleService.ServiceState.Connected -> MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.3f)
        is BleService.ServiceState.Transferring -> MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.4f)
        is BleService.ServiceState.Downloading -> MaterialTheme.colorScheme.tertiaryContainer.copy(alpha = 0.4f)
        is BleService.ServiceState.Error -> MaterialTheme.colorScheme.errorContainer.copy(alpha = 0.3f)
    }
}
