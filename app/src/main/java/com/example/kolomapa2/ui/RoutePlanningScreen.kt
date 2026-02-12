package com.example.kolomapa2.ui

import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.Crossfade
import androidx.compose.animation.SizeTransform
import androidx.compose.animation.expandVertically
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.shrinkVertically
import androidx.compose.animation.core.Spring
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.spring
import androidx.compose.animation.core.animateDpAsState
import androidx.compose.animation.core.tween
import androidx.compose.animation.core.keyframes
import androidx.compose.animation.animateColorAsState
import androidx.compose.animation.core.updateTransition
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsPressedAsState
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.MyLocation
import androidx.compose.material.icons.filled.Place
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Route
import androidx.compose.material.icons.filled.Save
import androidx.compose.material.icons.filled.SwapVert
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Popup
import androidx.compose.ui.window.PopupProperties
import com.example.kolomapa2.MainViewModel
import com.example.kolomapa2.models.ElevationProfilePoint
import com.example.kolomapa2.models.GeocodingSuggestion
import com.example.kolomapa2.models.LocationPoint
import com.example.kolomapa2.models.Trip
import com.example.kolomapa2.ui.theme.LocationEndBlue
import com.example.kolomapa2.ui.theme.LocationStartBlue
import com.example.kolomapa2.utils.ElevationProfileProcessor
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import kotlin.math.roundToInt

@Composable
fun RoutePlanningScreen(
    viewModel: MainViewModel,
    onBackClick: () -> Unit,
    modifier: Modifier = Modifier
) {
    val startLocation by viewModel.startLocation.collectAsState()
    val endLocation by viewModel.endLocation.collectAsState()
    val plannedRoute by viewModel.plannedRoute.collectAsState()
    val isLoadingRoute by viewModel.isLoadingRoute.collectAsState()
    val routePlanningError by viewModel.routePlanningError.collectAsState()
    val startSuggestions by viewModel.startSuggestions.collectAsState()
    val endSuggestions by viewModel.endSuggestions.collectAsState()
    val currentDeviceLocation by viewModel.currentDeviceLocation.collectAsState()

    var showSaveSheet by remember { mutableStateOf(false) }
    var activeField by remember { mutableStateOf<LocationField?>(null) }
    var isPickingOnMap by remember { mutableStateOf(false) }

    val haptic = LocalHapticFeedback.current
    val defaultRouteName = remember(plannedRoute?.metadata?.createdAt) {
        val formatter = SimpleDateFormat("MMM dd", Locale.getDefault())
        val createdAt = plannedRoute?.metadata?.createdAt ?: System.currentTimeMillis()
        "Route ${formatter.format(Date(createdAt))}"
    }

    // Request current device location when screen opens
    LaunchedEffect(Unit) {
        viewModel.requestCurrentDeviceLocation()
    }

    // Haptic feedback when reverse geocoding completes
    LaunchedEffect(startLocation?.displayName) {
        val location = startLocation
        if (location != null && location.displayName?.contains(",") == false) {
            // Name resolved - not just coordinates anymore
            haptic.performHapticFeedback(HapticFeedbackType.TextHandleMove)
        }
    }

    LaunchedEffect(endLocation?.displayName) {
        val location = endLocation
        if (location != null && location.displayName?.contains(",") == false) {
            // Name resolved - not just coordinates anymore
            haptic.performHapticFeedback(HapticFeedbackType.TextHandleMove)
        }
    }

    // Haptic feedback when route planning completes
    LaunchedEffect(plannedRoute) {
        if (plannedRoute != null) {
            haptic.performHapticFeedback(HapticFeedbackType.LongPress)
        }
    }

    // Haptic feedback on route planning error
    LaunchedEffect(routePlanningError) {
        if (routePlanningError != null) {
            haptic.performHapticFeedback(HapticFeedbackType.LongPress)
        }
    }

    Column(modifier = modifier.fillMaxSize()) {
        // Top Section: Location Input Fields
        LocationInputSection(
            startLocation = startLocation,
            endLocation = endLocation,
            startSuggestions = startSuggestions,
            endSuggestions = endSuggestions,
            onStartClick = { activeField = LocationField.START },
            onEndClick = { activeField = LocationField.END },
            onSwapClick = { viewModel.swapLocations() },
            onSuggestionClick = { suggestion, isStart ->
                val location = LocationPoint(
                    suggestion.lat,
                    suggestion.lon,
                    suggestion.name
                )
                if (isStart) {
                    viewModel.setStartLocation(location)
                } else {
                    viewModel.setEndLocation(location)
                }
                activeField = null
            },
            onSearchQueryChange = { query, isStart ->
                if (isStart) {
                    viewModel.searchStartLocation(query)
                } else {
                    viewModel.searchEndLocation(query)
                }
            },
            onMyLocationClick = { isStart ->
                viewModel.requestDeviceLocation(isStart)
                activeField = null
            },
            onPickOnMapClick = { isStart ->
                isPickingOnMap = true
                // Keep activeField set so we know which field to fill
            },
            activeField = activeField,
            isPickingOnMap = isPickingOnMap,
            onDismiss = {
                activeField = null
                isPickingOnMap = false
            }
        )

        // Middle Section: Interactive Map
        Box(
            modifier = Modifier
                .weight(1f)
                .fillMaxWidth()
        ) {
            RoutePlanningMap(
                plannedRoute = plannedRoute,
                startLocation = startLocation,
                endLocation = endLocation,
                currentDeviceLocation = currentDeviceLocation,
                isPickingLocation = isPickingOnMap,
                onMapClick = { lat: Double, lon: Double ->
                    if (isPickingOnMap && activeField != null) {
                        // Haptic feedback on map click
                        haptic.performHapticFeedback(HapticFeedbackType.LongPress)

                        val isStart = activeField == LocationField.START

                        // Show marker immediately with coordinates
                        val tempLocation = LocationPoint(
                            lat = lat,
                            lon = lon,
                            displayName = "${String.format("%.5f", lat)}, ${String.format("%.5f", lon)}"
                        )
                        if (isStart) {
                            viewModel.setStartLocation(tempLocation)
                        } else {
                            viewModel.setEndLocation(tempLocation)
                        }

                        // Reverse geocode asynchronously (will update name later)
                        viewModel.reverseGeocode(lat, lon, isStart)

                        isPickingOnMap = false
                        activeField = null
                    }
                },
                modifier = Modifier
                    .fillMaxSize()
                    .padding(horizontal = 16.dp)
            )
        }

        // Bottom Section: Action Buttons
        BottomActionSection(
            startLocation = startLocation,
            endLocation = endLocation,
            plannedRoute = plannedRoute,
            isLoadingRoute = isLoadingRoute,
            routePlanningError = routePlanningError,
            onPlanClick = {
                haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                viewModel.planRoute()
            },
            onSaveClick = {
                haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                showSaveSheet = true
            },
            onClearClick = {
                haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                viewModel.clearRoutePlanning()
            }
        )
    }

    // Save sheet
    if (showSaveSheet) {
        plannedRoute?.let { route ->
            PlannedRouteSaveSheet(
                plannedRoute = route,
                defaultName = defaultRouteName,
                onSave = { name ->
                    viewModel.savePlannedRoute(name)
                    showSaveSheet = false
                    onBackClick()
                },
                onDismiss = { showSaveSheet = false }
            )
        }
    }
}

enum class LocationField {
    START, END
}

@Composable
private fun LocationInputSection(
    startLocation: LocationPoint?,
    endLocation: LocationPoint?,
    startSuggestions: List<GeocodingSuggestion>,
    endSuggestions: List<GeocodingSuggestion>,
    onStartClick: () -> Unit,
    onEndClick: () -> Unit,
    onSwapClick: () -> Unit,
    onSuggestionClick: (GeocodingSuggestion, Boolean) -> Unit,
    onSearchQueryChange: (String, Boolean) -> Unit,
    onMyLocationClick: (Boolean) -> Unit,
    onPickOnMapClick: (Boolean) -> Unit,
    activeField: LocationField?,
    isPickingOnMap: Boolean,
    onDismiss: () -> Unit,
    modifier: Modifier = Modifier
) {
    val haptic = LocalHapticFeedback.current

    // Track swap state - toggles between false (normal) and true (swapped)
    var isSwapped by remember { mutableStateOf(false) }
    var hasSwappedData by remember { mutableStateOf(false) }
    var swapButtonRotation by remember { mutableStateOf(0f) }

    // Animated swap offset (0 = start on top, 1 = end on top)
    val swapOffset by animateFloatAsState(
        targetValue = if (isSwapped) 1f else 0f,
        animationSpec = tween(durationMillis = 300, easing = androidx.compose.animation.core.FastOutSlowInEasing),
        label = "swapOffset"
    )

    // Animated button rotation
    val animatedRotation by animateFloatAsState(
        targetValue = swapButtonRotation,
        animationSpec = spring(
            dampingRatio = Spring.DampingRatioMediumBouncy,
            stiffness = Spring.StiffnessMedium
        ),
        label = "swapRotation"
    )

    // Calculate field height for slide animation
    val density = LocalDensity.current
    val fieldHeightPx = with(density) { 80.dp.roundToPx() }

    // Calculate peak offset (0 -> 1 -> 0) for bounce animation
    val peakOffset = if (swapOffset < 0.5f) swapOffset * 2f else (1f - swapOffset) * 2f

    // Trigger data swap at midpoint of animation - only once per swap
    LaunchedEffect(swapOffset, isSwapped) {
        // Swap data at the midpoint (around 0.5) - only once per direction change
        if (swapOffset > 0.4f && swapOffset < 0.6f && !hasSwappedData) {
            hasSwappedData = true
            onSwapClick()
        } else if (swapOffset < 0.1f || swapOffset > 0.9f) {
            // Reset the flag when animation reaches either end
            hasSwappedData = false
        }
    }

    Card(
        modifier = modifier
            .fillMaxWidth()
            .padding(16.dp),
        elevation = CardDefaults.cardElevation(defaultElevation = 4.dp)
    ) {
        Row(
            modifier = Modifier.padding(14.dp),
            horizontalArrangement = Arrangement.spacedBy(10.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            // Location fields column with swap animation
            Column(
                modifier = Modifier.weight(1f),
                verticalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                // Start Location Field - always on top, only data swaps
                LocationInputField(
                    label = "Start Location",
                    location = startLocation,
                    suggestions = startSuggestions,
                    isActive = activeField == LocationField.START,
                    isPickingOnMap = isPickingOnMap && activeField == LocationField.START,
                    isStartField = true,
                    onClick = {
                        haptic.performHapticFeedback(HapticFeedbackType.TextHandleMove)
                        onStartClick()
                    },
                    onSuggestionClick = {
                        haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                        onSuggestionClick(it, true)
                    },
                    onSearchQueryChange = { onSearchQueryChange(it, true) },
                    onMyLocationClick = {
                        haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                        onMyLocationClick(true)
                    },
                    onPickOnMapClick = {
                        haptic.performHapticFeedback(HapticFeedbackType.TextHandleMove)
                        onPickOnMapClick(true)
                    },
                    onDismiss = {
                        haptic.performHapticFeedback(HapticFeedbackType.TextHandleMove)
                        onDismiss()
                    },
                    modifier = Modifier.graphicsLayer {
                        // Bounce effect during swap - moves down and back
                        translationY = peakOffset * fieldHeightPx * 0.3f
                        alpha = 1f - (peakOffset * 0.3f)
                    }
                )

                // End Location Field - always on bottom, only data swaps
                LocationInputField(
                    label = "End Location",
                    location = endLocation,
                    suggestions = endSuggestions,
                    isActive = activeField == LocationField.END,
                    isPickingOnMap = isPickingOnMap && activeField == LocationField.END,
                    isStartField = false,
                    onClick = {
                        haptic.performHapticFeedback(HapticFeedbackType.TextHandleMove)
                        onEndClick()
                    },
                    onSuggestionClick = {
                        haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                        onSuggestionClick(it, false)
                    },
                    onSearchQueryChange = { onSearchQueryChange(it, false) },
                    onMyLocationClick = {
                        haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                        onMyLocationClick(false)
                    },
                    onPickOnMapClick = {
                        haptic.performHapticFeedback(HapticFeedbackType.TextHandleMove)
                        onPickOnMapClick(false)
                    },
                    onDismiss = {
                        haptic.performHapticFeedback(HapticFeedbackType.TextHandleMove)
                        onDismiss()
                    },
                    modifier = Modifier.graphicsLayer {
                        // Bounce effect during swap - moves up and back
                        translationY = -peakOffset * fieldHeightPx * 0.3f
                        alpha = 1f - (peakOffset * 0.3f)
                    }
                )
            }

            // Swap Button on the right with animation
            IconButton(
                onClick = {
                    haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                    swapButtonRotation += 180f
                    isSwapped = !isSwapped
                },
                modifier = Modifier.size(48.dp)
            ) {
                Icon(
                    imageVector = Icons.Default.SwapVert,
                    contentDescription = "Swap locations",
                    modifier = Modifier
                        .size(24.dp)
                        .graphicsLayer {
                            rotationZ = animatedRotation
                        }
                )
            }
        }
    }
}

@Composable
private fun LocationInputField(
    label: String,
    location: LocationPoint?,
    suggestions: List<GeocodingSuggestion>,
    isActive: Boolean,
    isPickingOnMap: Boolean,
    isStartField: Boolean,
    onClick: () -> Unit,
    onSuggestionClick: (GeocodingSuggestion) -> Unit,
    onSearchQueryChange: (String) -> Unit,
    onMyLocationClick: () -> Unit,
    onPickOnMapClick: () -> Unit,
    onDismiss: () -> Unit,
    modifier: Modifier = Modifier
) {
    // Use different shades of blue for start and end fields
    val accentColor = if (isStartField) LocationStartBlue else LocationEndBlue
    var searchQuery by remember { mutableStateOf("") }

    // Track if we're waiting for reverse geocoding (coordinates displayed but name not yet resolved)
    val isGeocodingInProgress = location?.displayName?.contains(",") == true &&
                                location.displayName?.matches(Regex("^-?\\d+\\.\\d+,\\s*-?\\d+\\.\\d+$")) == true

    // Reset search query when field becomes inactive
    LaunchedEffect(isActive) {
        if (!isActive) {
            searchQuery = ""
        }
    }

    // Border animation
    val borderColor by animateColorAsState(
        targetValue = when {
            isActive || isPickingOnMap -> accentColor
            else -> MaterialTheme.colorScheme.outline
        },
        animationSpec = spring(dampingRatio = Spring.DampingRatioMediumBouncy),
        label = "borderColor"
    )

    val borderWidth by animateDpAsState(
        targetValue = if (isActive || isPickingOnMap) 2.dp else 1.dp,
        animationSpec = spring(dampingRatio = Spring.DampingRatioMediumBouncy),
        label = "borderWidth"
    )

    Box(modifier = modifier) {
        Column {
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.SpaceBetween,
                modifier = Modifier.fillMaxWidth()
            ) {
                Text(
                    text = label,
                    style = MaterialTheme.typography.labelMedium,
                    color = if (isActive) accentColor else MaterialTheme.colorScheme.onSurfaceVariant,
                    fontWeight = if (isActive) FontWeight.Bold else FontWeight.Normal
                )
                if (isPickingOnMap) {
                    Text(
                        text = "Tap on map to select",
                        style = MaterialTheme.typography.labelSmall,
                        color = accentColor,
                        fontWeight = FontWeight.Bold
                    )
                } else if (isActive) {
                    Text(
                        text = "Search or pick below",
                        style = MaterialTheme.typography.labelSmall,
                        color = accentColor
                    )
                }
            }

            Spacer(modifier = Modifier.height(4.dp))

            // Display/Input Field - consistent sizing across all states
            if (isActive && !isPickingOnMap) {
                OutlinedTextField(
                    value = searchQuery,
                    onValueChange = {
                        searchQuery = it
                        onSearchQueryChange(it)
                    },
                    modifier = Modifier.fillMaxWidth(),
                    placeholder = { Text("Search location...") },
                    singleLine = true,
                    colors = OutlinedTextFieldDefaults.colors(
                        focusedBorderColor = accentColor,
                        unfocusedBorderColor = accentColor.copy(alpha = 0.7f)
                    ),
                    textStyle = MaterialTheme.typography.bodyMedium,
                    trailingIcon = {
                        IconButton(onClick = onDismiss, modifier = Modifier.size(36.dp)) {
                            Icon(
                                Icons.Default.Close,
                                contentDescription = "Cancel",
                                modifier = Modifier.size(20.dp)
                            )
                        }
                    },
                    leadingIcon = {
                        Icon(
                            Icons.Default.Place,
                            contentDescription = null,
                            tint = accentColor,
                            modifier = Modifier.size(22.dp)
                        )
                    }
                )
            } else if (isPickingOnMap) {
                // Picking on map mode - show highlighted state
                androidx.compose.animation.AnimatedVisibility(
                    visible = true,
                    enter = androidx.compose.animation.fadeIn() + androidx.compose.animation.expandVertically(),
                    exit = androidx.compose.animation.fadeOut() + androidx.compose.animation.shrinkVertically()
                ) {
                    Surface(
                        modifier = Modifier
                            .fillMaxWidth()
                            .clickable(onClick = onDismiss),
                        shape = MaterialTheme.shapes.small,
                        border = BorderStroke(borderWidth, borderColor),
                        color = accentColor.copy(alpha = 0.15f)
                    ) {
                        Row(
                            modifier = Modifier.padding(14.dp),
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            Icon(
                                imageVector = Icons.Default.Place,
                                contentDescription = null,
                                tint = accentColor,
                                modifier = Modifier.size(22.dp)
                            )
                            Spacer(modifier = Modifier.width(10.dp))
                            Text(
                                text = location?.displayName ?: "Selecting on map...",
                                style = MaterialTheme.typography.bodyMedium,
                                color = accentColor,
                                fontWeight = FontWeight.Medium
                            )
                            Spacer(modifier = Modifier.weight(1f))

                            // Show loading indicator during geocoding
                            if (isGeocodingInProgress) {
                                CircularProgressIndicator(
                                    modifier = Modifier.size(20.dp),
                                    strokeWidth = 2.dp,
                                    color = accentColor
                                )
                                Spacer(modifier = Modifier.width(8.dp))
                            }

                            IconButton(onClick = onDismiss, modifier = Modifier.size(36.dp)) {
                                Icon(
                                    Icons.Default.Close,
                                    contentDescription = "Cancel",
                                    tint = accentColor,
                                    modifier = Modifier.size(20.dp)
                                )
                            }
                        }
                    }
                }
            } else {
                // Display selected location or prompt
                androidx.compose.animation.AnimatedVisibility(
                    visible = true,
                    enter = androidx.compose.animation.fadeIn() + androidx.compose.animation.expandVertically(),
                    exit = androidx.compose.animation.fadeOut() + androidx.compose.animation.shrinkVertically()
                ) {
                    Surface(
                        modifier = Modifier
                            .fillMaxWidth()
                            .clickable(onClick = onClick),
                        shape = MaterialTheme.shapes.small,
                        border = BorderStroke(borderWidth, borderColor)
                    ) {
                        Row(
                            modifier = Modifier.padding(14.dp),
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            Icon(
                                imageVector = if (location?.isCurrentLocation == true)
                                    Icons.Default.MyLocation else Icons.Default.Place,
                                contentDescription = null,
                                tint = accentColor,
                                modifier = Modifier.size(22.dp)
                            )
                            Spacer(modifier = Modifier.width(10.dp))
                            Column(modifier = Modifier.weight(1f)) {
                                Text(
                                    text = location?.displayName ?: "Tap to select location",
                                    style = MaterialTheme.typography.bodyMedium,
                                    color = if (location != null)
                                        MaterialTheme.colorScheme.onSurface
                                    else
                                        MaterialTheme.colorScheme.onSurfaceVariant
                                )
                            }

                            // Show loading indicator during geocoding
                            if (isGeocodingInProgress) {
                                Spacer(modifier = Modifier.width(8.dp))
                                CircularProgressIndicator(
                                    modifier = Modifier.size(20.dp),
                                    strokeWidth = 2.dp,
                                    color = accentColor
                                )
                            }
                        }
                    }
                }
            }
        }

        // Suggestions Dropdown - floating popup that doesn't affect layout
        if (isActive && !isPickingOnMap && (suggestions.isNotEmpty() || searchQuery.isEmpty())) {
            val density = LocalDensity.current
            val offsetY = with(density) { 86.dp.roundToPx() }

            Popup(
                alignment = Alignment.TopStart,
                offset = androidx.compose.ui.unit.IntOffset(0, offsetY),
                properties = PopupProperties(focusable = false)
            ) {
                Surface(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(horizontal = 32.dp), // Match the Card's 16dp + Column's 16dp padding
                    shape = MaterialTheme.shapes.small,
                    color = MaterialTheme.colorScheme.surface,
                    tonalElevation = 3.dp,
                    shadowElevation = 8.dp,
                    border = BorderStroke(
                        width = 1.dp,
                        color = accentColor.copy(alpha = 0.3f)
                    )
                ) {
                    Column {
                        // Subtle top divider for visual connection
                        HorizontalDivider(
                            thickness = 2.dp,
                            color = accentColor.copy(alpha = 0.5f)
                        )

                        LazyColumn(
                            modifier = Modifier.heightIn(max = 250.dp)
                        ) {
                            // Hardcoded options (only if no search query)
                            if (searchQuery.isEmpty()) {
                                item {
                                    SuggestionItem(
                                        title = "Device Location",
                                        subtitle = "Use current GPS position from device",
                                        icon = Icons.Default.MyLocation,
                                        onClick = onMyLocationClick
                                    )
                                }
                                item {
                                    HorizontalDivider(
                                        modifier = Modifier.padding(horizontal = 12.dp),
                                        color = MaterialTheme.colorScheme.outlineVariant
                                    )
                                }
                                item {
                                    SuggestionItem(
                                        title = "Pick on Map",
                                        subtitle = "Select location by clicking on the map",
                                        icon = Icons.Default.Place,
                                        onClick = onPickOnMapClick
                                    )
                                }

                                // Divider between hardcoded options and search results
                                if (suggestions.isNotEmpty()) {
                                    item {
                                        HorizontalDivider(
                                            modifier = Modifier.padding(vertical = 4.dp),
                                            thickness = 2.dp,
                                            color = MaterialTheme.colorScheme.outlineVariant
                                        )
                                    }
                                }
                            }

                            // Search suggestions
                            items(suggestions) { suggestion ->
                                SuggestionItem(
                                    title = suggestion.name,
                                    subtitle = suggestion.location,
                                    icon = Icons.Default.Place,
                                    onClick = { onSuggestionClick(suggestion) }
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
private fun SuggestionItem(
    title: String,
    subtitle: String,
    icon: ImageVector,
    onClick: () -> Unit,
    modifier: Modifier = Modifier
) {
    Surface(
        modifier = modifier.fillMaxWidth(),
        color = MaterialTheme.colorScheme.surface,
        onClick = onClick
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 16.dp, vertical = 12.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            // Icon with background
            Surface(
                shape = MaterialTheme.shapes.small,
                color = MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.5f),
                modifier = Modifier.size(40.dp)
            ) {
                Box(
                    contentAlignment = Alignment.Center,
                    modifier = Modifier.fillMaxSize()
                ) {
                    Icon(
                        imageVector = icon,
                        contentDescription = null,
                        tint = MaterialTheme.colorScheme.primary,
                        modifier = Modifier.size(20.dp)
                    )
                }
            }

            Spacer(modifier = Modifier.width(16.dp))

            // Text content
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = title,
                    style = MaterialTheme.typography.bodyLarge,
                    fontWeight = FontWeight.Medium,
                    color = MaterialTheme.colorScheme.onSurface
                )
                if (subtitle.isNotBlank()) {
                    Spacer(modifier = Modifier.height(2.dp))
                    Text(
                        text = subtitle,
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        maxLines = 1
                    )
                }
            }
        }
    }
}

@Composable
private fun RoutePlanningMap(
    plannedRoute: com.example.kolomapa2.models.Trip?,
    startLocation: LocationPoint?,
    endLocation: LocationPoint?,
    currentDeviceLocation: LocationPoint?,
    isPickingLocation: Boolean,
    onMapClick: (Double, Double) -> Unit,
    modifier: Modifier = Modifier
) {
    // Priority: startLocation > endLocation > currentDeviceLocation
    val centerLocation = startLocation ?: endLocation ?: currentDeviceLocation

    TripMapPreview(
        trip = plannedRoute,
        centerLocation = centerLocation,
        startLocation = startLocation,
        endLocation = endLocation,
        resetViewOnTripChange = true,
        fitPaddingDp = 32.dp,
        isInteractive = isPickingLocation,
        onMapClick = if (isPickingLocation) onMapClick else null,
        modifier = modifier
    )
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun PlannedRouteSaveSheet(
    plannedRoute: Trip,
    defaultName: String,
    onSave: (String) -> Unit,
    onDismiss: () -> Unit
) {
    val sheetState = rememberModalBottomSheetState(skipPartiallyExpanded = true)
    var tripName by rememberSaveable { mutableStateOf(defaultName) }
    LaunchedEffect(defaultName) {
        if (tripName.isBlank()) {
            tripName = defaultName
        }
    }
    val trimmedName = tripName.trim()
    val isSaveEnabled = trimmedName.isNotBlank()
    val scrollState = rememberScrollState()

    // Elevation profile state
    val elevationProfile = remember(plannedRoute.gpxContent) {
        ElevationProfileProcessor.processGpxContent(plannedRoute.gpxContent)
    }
    var selectedProfilePoint by remember { mutableStateOf<ElevationProfilePoint?>(null) }

    val interactionSource = remember { MutableInteractionSource() }
    val isPressed by interactionSource.collectIsPressedAsState()
    val scale by animateFloatAsState(
        targetValue = if (isSaveEnabled && isPressed) 0.95f else 1f,
        animationSpec = spring(
            dampingRatio = Spring.DampingRatioMediumBouncy,
            stiffness = Spring.StiffnessHigh
        ),
        label = "saveButtonScale"
    )

    ModalBottomSheet(
        onDismissRequest = onDismiss,
        sheetState = sheetState
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .verticalScroll(scrollState)
                .imePadding()
                .padding(16.dp)
        ) {
            Text(
                "Save planned route",
                style = MaterialTheme.typography.titleLarge,
                fontWeight = FontWeight.Bold
            )
            Spacer(modifier = Modifier.height(8.dp))
            Text(
                "Give this route a name before saving it to your trips.",
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            Spacer(modifier = Modifier.height(16.dp))

            OutlinedTextField(
                value = tripName,
                onValueChange = { tripName = it },
                label = { Text("Trip name") },
                singleLine = true,
                modifier = Modifier.fillMaxWidth()
            )

            Spacer(modifier = Modifier.height(16.dp))

            // Elevation profile chart
            ElevationProfileChart(
                profile = elevationProfile,
                selectedPoint = selectedProfilePoint,
                onPointSelected = { point ->
                    selectedProfilePoint = point
                },
                modifier = Modifier.fillMaxWidth()
            )

            Spacer(modifier = Modifier.height(16.dp))

            PlannedRouteMetadataCard(plannedRoute = plannedRoute)

            Spacer(modifier = Modifier.height(16.dp))

            Box(
                modifier = Modifier.fillMaxWidth(),
                contentAlignment = Alignment.Center
            ) {
                Box(
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(56.dp)
                        .graphicsLayer {
                            scaleX = if (isSaveEnabled) scale else 1f
                            scaleY = if (isSaveEnabled) scale else 1f
                        }
                        .clip(MaterialTheme.shapes.medium)
                        .background(
                            if (isSaveEnabled) Color(0xFF2563EB)
                            else Color(0xFF6B7280)
                        )
                        .then(
                            if (isSaveEnabled) {
                                Modifier.clickable(
                                    interactionSource = interactionSource,
                                    indication = null,
                                    onClick = { onSave(trimmedName) }
                                )
                            } else {
                                Modifier
                            }
                        ),
                    contentAlignment = Alignment.Center
                ) {
                    Text(
                        if (isSaveEnabled) "Save Trip" else "Enter a trip name",
                        style = MaterialTheme.typography.titleMedium,
                        fontWeight = FontWeight.Bold,
                        color = Color.White
                    )
                }
            }

            Spacer(modifier = Modifier.height(12.dp))
        }
    }
}

@Composable
private fun PlannedRouteMetadataCard(
    plannedRoute: Trip
) {
    Card(
        modifier = Modifier.fillMaxWidth(),
        elevation = CardDefaults.cardElevation(defaultElevation = 2.dp)
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(12.dp)
        ) {
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
                            formatDistance(plannedRoute.metadata.totalDistance),
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
                        "${plannedRoute.metadata.pointCount}",
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
                            "${plannedRoute.metadata.minElevation.roundToInt()}",
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
                            "${plannedRoute.metadata.maxElevation.roundToInt()}",
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
                            "+${plannedRoute.metadata.totalElevationGain.roundToInt()}",
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
                            "-${plannedRoute.metadata.totalElevationLoss.roundToInt()}",
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
        }
    }
}

// Match distance formatting used across trip cards and detail screens.
private fun formatDistance(distanceInMeters: Double): String {
    val km = distanceInMeters / 1000
    return if (km % 1.0 == 0.0) {
        km.toInt().toString()
    } else {
        String.format(Locale.US, "%.1f", km)
    }
}

@Composable
private fun BottomActionSection(
    startLocation: LocationPoint?,
    endLocation: LocationPoint?,
    plannedRoute: com.example.kolomapa2.models.Trip?,
    isLoadingRoute: Boolean,
    routePlanningError: String?,
    onPlanClick: () -> Unit,
    onSaveClick: () -> Unit,
    onClearClick: () -> Unit,
    modifier: Modifier = Modifier
) {
    // Haptic feedback
    val haptic = LocalHapticFeedback.current

    // Interaction source for button press animation
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

    Column(
        modifier = modifier
            .fillMaxWidth()
            .padding(16.dp)
    ) {
        // Error message with animation
        androidx.compose.animation.AnimatedVisibility(
            visible = routePlanningError != null,
            enter = androidx.compose.animation.fadeIn() + androidx.compose.animation.expandVertically(),
            exit = androidx.compose.animation.fadeOut() + androidx.compose.animation.shrinkVertically()
        ) {
            routePlanningError?.let { error ->
                Card(
                    colors = CardDefaults.cardColors(
                        containerColor = MaterialTheme.colorScheme.errorContainer
                    ),
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(bottom = 12.dp)
                ) {
                    Text(
                        text = error,
                        color = MaterialTheme.colorScheme.onErrorContainer,
                        style = MaterialTheme.typography.bodyMedium,
                        modifier = Modifier.padding(12.dp)
                    )
                }
            }
        }

        // Action buttons with crossfade animation
        androidx.compose.animation.Crossfade(
            targetState = plannedRoute != null,
            animationSpec = spring(
                dampingRatio = Spring.DampingRatioMediumBouncy,
                stiffness = Spring.StiffnessMedium
            ),
            label = "actionButtonsCrossfade"
        ) { hasRoute ->
            if (!hasRoute) {
                // Plan Route button with custom style
                val isButtonEnabled = startLocation != null && endLocation != null && !isLoadingRoute

                Box(
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(56.dp)
                        .graphicsLayer {
                            scaleX = if (isButtonEnabled) scale else 1f
                            scaleY = if (isButtonEnabled) scale else 1f
                        }
                        .clip(MaterialTheme.shapes.medium)
                        .background(
                            if (isButtonEnabled) Color(0xFF10B981) else Color(0xFF6B7280)
                        )
                        .then(
                            if (isButtonEnabled) {
                                Modifier.clickable(
                                    interactionSource = interactionSource,
                                    indication = null,
                                    onClick = {
                                        haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                                        onPlanClick()
                                    }
                                )
                            } else {
                                Modifier
                            }
                        ),
                    contentAlignment = Alignment.Center
                ) {
                    androidx.compose.animation.Crossfade(
                        targetState = isLoadingRoute,
                        animationSpec = spring(dampingRatio = Spring.DampingRatioMediumBouncy),
                        label = "loadingCrossfade"
                    ) { loading ->
                        if (loading) {
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
                                    "Planning Route...",
                                    style = MaterialTheme.typography.titleMedium,
                                    fontWeight = FontWeight.Bold,
                                    color = Color.White
                                )
                            }
                        } else {
                            Row(
                                horizontalArrangement = Arrangement.spacedBy(8.dp),
                                verticalAlignment = Alignment.CenterVertically
                            ) {
                                Icon(
                                    Icons.Default.Route,
                                    contentDescription = null,
                                    tint = Color.White
                                )
                                Text(
                                    "Plan Route",
                                    style = MaterialTheme.typography.titleMedium,
                                    fontWeight = FontWeight.Bold,
                                    color = Color.White
                                )
                            }
                        }
                    }
                }
            } else {
            // Show route info + Save button with Reset icon button
            plannedRoute?.let { route ->
                Column {
                    Card(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(bottom = 12.dp)
                    ) {
                        Column(modifier = Modifier.padding(12.dp)) {
                            Text(
                                "Route Planned",
                                style = MaterialTheme.typography.titleSmall,
                                fontWeight = FontWeight.Bold
                            )
                            Spacer(modifier = Modifier.height(4.dp))
                            Text(
                                "${(route.metadata.totalDistance / 1000).roundToInt()} km • " +
                                "${route.metadata.pointCount} points",
                                style = MaterialTheme.typography.bodyMedium,
                                color = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                        }
                    }

                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.spacedBy(12.dp),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        // Reset icon button
                        IconButton(
                            onClick = onClearClick,
                            modifier = Modifier.size(56.dp)
                        ) {
                            Icon(
                                imageVector = Icons.Default.Refresh,
                                contentDescription = "Reset",
                                tint = MaterialTheme.colorScheme.error,
                                modifier = Modifier.size(28.dp)
                            )
                        }

                        // Save Trip button with custom style
                        Box(
                            modifier = Modifier
                                .weight(1f)
                                .height(56.dp)
                                .graphicsLayer {
                                    scaleX = scale
                                    scaleY = scale
                                }
                                .clip(MaterialTheme.shapes.medium)
                                .background(Color(0xFF2563EB))
                                .clickable(
                                    interactionSource = interactionSource,
                                    indication = null,
                                    onClick = {
                                        haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                                        onSaveClick()
                                    }
                                ),
                            contentAlignment = Alignment.Center
                        ) {
                            Row(
                                horizontalArrangement = Arrangement.spacedBy(8.dp),
                                verticalAlignment = Alignment.CenterVertically
                            ) {
                                Icon(
                                    Icons.Default.Save,
                                    contentDescription = null,
                                    tint = Color.White
                                )
                                Text(
                                    "Save Trip",
                                    style = MaterialTheme.typography.titleMedium,
                                    fontWeight = FontWeight.Bold,
                                    color = Color.White
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
