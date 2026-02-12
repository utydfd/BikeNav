package com.example.kolomapa2.ui

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.gestures.detectTransformGestures
import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.runtime.snapshots.SnapshotStateMap
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.*
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.IntSize
import androidx.compose.ui.unit.dp
import com.example.kolomapa2.models.BoundingBox
import com.example.kolomapa2.models.LocationPoint
import com.example.kolomapa2.models.Trip
import com.example.kolomapa2.utils.GpxParser
import com.example.kolomapa2.utils.StorageManager
import com.example.kolomapa2.utils.TileDownloader
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File
import kotlin.math.*

/**
 * Map preview component for trip details page
 * Displays GPX route on top of locally stored map tiles
 * Supports pinch-to-zoom and pan gestures
 */
@OptIn(ExperimentalMaterial3ExpressiveApi::class)
@Composable
fun TripMapPreview(
    trip: Trip?,
    centerLocation: LocationPoint? = null,
    startLocation: LocationPoint? = null,
    endLocation: LocationPoint? = null,
    highlightLocation: LocationPoint? = null,
    resetViewOnTripChange: Boolean = true,
    fitPaddingDp: Dp = 24.dp,
    isInteractive: Boolean = false,
    onMapClick: ((Double, Double) -> Unit)? = null,
    modifier: Modifier = Modifier
) {
    val context = LocalContext.current
    val view = LocalView.current
    val density = LocalDensity.current
    val scope = rememberCoroutineScope()

    // Storage and download managers
    val storageManager = remember { StorageManager(context) }
    val tileDownloader = remember { TileDownloader(storageManager) }

    // Map state
    var mapData by remember { mutableStateOf<MapData?>(null) }
    val loadedTiles = remember { mutableStateMapOf<TileKey, Bitmap>() }  // Dynamic tile cache

    // Gesture state
    var scale by remember { mutableFloatStateOf(1f) }
    var offsetX by remember { mutableFloatStateOf(0f) }
    var offsetY by remember { mutableFloatStateOf(0f) }
    var tileZoomLevel by remember { mutableIntStateOf(17) }  // Track which tile zoom we're using
    var canvasSize by remember { mutableStateOf(IntSize.Zero) }
    var pendingReset by remember { mutableStateOf(false) }
    var tileErrorMessage by remember { mutableStateOf<String?>(null) }

    // Load initial map data immediately in background
    LaunchedEffect(trip, centerLocation, resetViewOnTripChange) {
        scope.launch {
            val data = withContext(Dispatchers.IO) {
                if (trip != null) {
                    loadMapData(context, trip)
                } else if (centerLocation != null) {
                    // Create map data from center location only
                    loadMapDataFromLocation(centerLocation)
                } else {
                    // Default to Prague
                    loadMapDataFromLocation(LocationPoint(50.0755, 14.4378, "Prague"))
                }
            }
            mapData = data
            if (data != null) {
                tileZoomLevel = data.baseZoom
                if (resetViewOnTripChange && trip != null) {
                    pendingReset = true
                }
            }
        }
    }

    // Reset the view to fit the full route after a new trip is loaded.
    LaunchedEffect(mapData, canvasSize, pendingReset, resetViewOnTripChange, fitPaddingDp) {
        val data = mapData ?: return@LaunchedEffect
        if (!resetViewOnTripChange || !pendingReset) return@LaunchedEffect
        if (canvasSize.width == 0 || canvasSize.height == 0) return@LaunchedEffect

        val paddingPx = with(density) { fitPaddingDp.toPx() }
        val availableWidth = (canvasSize.width.toFloat() - paddingPx * 2f).coerceAtLeast(1f)
        val availableHeight = (canvasSize.height.toFloat() - paddingPx * 2f).coerceAtLeast(1f)

        tileZoomLevel = calculateFitZoomForViewport(data.boundingBox, availableWidth, availableHeight)
        scale = 1f
        offsetX = 0f
        offsetY = 0f
        pendingReset = false
    }

    // Load tiles dynamically based on viewport with automatic downloading
    LaunchedEffect(mapData, tileZoomLevel, offsetX, offsetY, scale, canvasSize) {
        val data = mapData ?: return@LaunchedEffect
        if (canvasSize.width == 0 || canvasSize.height == 0) return@LaunchedEffect

        withContext(Dispatchers.IO) {
            val visibleTiles = calculateVisibleTileKeys(
                data = data,
                zoom = tileZoomLevel,
                offsetX = offsetX,
                offsetY = offsetY,
                scale = scale,
                canvasWidth = canvasSize.width.toFloat(),
                canvasHeight = canvasSize.height.toFloat()
            )

            // Load missing tiles at current zoom level (raw tiles)
            val storageDir = File(android.os.Environment.getExternalStorageDirectory(), "KoloMapa2/RawTiles")
            visibleTiles.forEach { tileKey ->
                if (!loadedTiles.containsKey(tileKey)) {
                    val tileFile = File(storageDir, "${tileKey.zoom}/${tileKey.x}/${tileKey.y}.tile")

                    // If tile exists on disk, load it
                    if (tileFile.exists()) {
                        val bitmap = loadTileBitmapFromPng(tileFile)
                        if (bitmap != null) {
                            loadedTiles[tileKey] = bitmap
                        }
                    } else {
                        // Tile missing - download it dynamically
                        android.util.Log.d("TripMapPreview", "Downloading missing tile: zoom=${tileKey.zoom}, x=${tileKey.x}, y=${tileKey.y}")

                        val downloadResult = tileDownloader.downloadTile(tileKey.zoom, tileKey.x, tileKey.y)

                        if (downloadResult.success) {
                            android.util.Log.d("TripMapPreview", "Successfully downloaded tile: zoom=${tileKey.zoom}, x=${tileKey.x}, y=${tileKey.y}")

                            // Load the newly downloaded tile
                            val bitmap = loadTileBitmapFromPng(tileFile)
                            if (bitmap != null) {
                                loadedTiles[tileKey] = bitmap
                            }
                            if (tileErrorMessage != null) {
                                tileErrorMessage = null
                            }
                        } else {
                            android.util.Log.w("TripMapPreview", "Failed to download tile: zoom=${tileKey.zoom}, x=${tileKey.x}, y=${tileKey.y}")
                            if (tileErrorMessage == null) {
                                tileErrorMessage = downloadResult.errorMessage ?: "Map tiles unavailable"
                            }
                        }
                    }
                }
            }

            // Clean up tiles that are too far from current zoom (keep adjacent levels for smooth transitions)
            val tilesToRemove = loadedTiles.keys.filter { key ->
                abs(key.zoom - tileZoomLevel) > 2
            }
            tilesToRemove.forEach { key ->
                loadedTiles.remove(key)
            }
        }
    }

    Card(
        modifier = modifier,
        elevation = CardDefaults.cardElevation(defaultElevation = 2.dp),
        colors = CardDefaults.cardColors(
            containerColor = MaterialTheme.colorScheme.surface
        )
    ) {
        Box(
            modifier = Modifier.fillMaxSize(),
            contentAlignment = Alignment.Center
        ) {
            val data = mapData
            if (data != null) {
                    Canvas(
                        modifier = Modifier
                            .fillMaxSize()
                            .onSizeChanged { canvasSize = it }
                            .then(
                                if (isInteractive && onMapClick != null) {
                                    Modifier.pointerInput(Unit) {
                                        detectTapGestures { offset ->
                                            // Convert screen coordinates to lat/lon
                                            val canvasWidth = size.width.toFloat()
                                            val canvasHeight = size.height.toFloat()
                                            val centerX = canvasWidth / 2
                                            val centerY = canvasHeight / 2

                                            val bbox = data.boundingBox
                                            val centerLat = (bbox.minLat + bbox.maxLat) / 2
                                            val centerLon = (bbox.minLon + bbox.maxLon) / 2

                                            val centerTileX = getTileX(centerLon, tileZoomLevel)
                                            val centerTileY = getTileY(centerLat, tileZoomLevel)
                                            val centerPixelX = getTilePixelX(centerLon, tileZoomLevel)
                                            val centerPixelY = getTilePixelY(centerLat, tileZoomLevel)

                                            // Calculate click position relative to center
                                            val clickRelX = offset.x - centerX - offsetX
                                            val clickRelY = offset.y - centerY - offsetY

                                            // Convert to tile pixel coordinates
                                            val clickTileOffsetX = clickRelX / (scale * 256f)
                                            val clickTileOffsetY = clickRelY / (scale * 256f)

                                            // Calculate fractional tile coordinates
                                            val clickFracTileX = centerTileX + (centerPixelX / 256.0) + clickTileOffsetX
                                            val clickFracTileY = centerTileY + (centerPixelY / 256.0) + clickTileOffsetY

                                            // Convert back to lat/lon
                                            val clickLat = tileYToLat(clickFracTileY, tileZoomLevel)
                                            val clickLon = tileXToLon(clickFracTileX, tileZoomLevel)

                                            // Haptic feedback
                                            view.performHapticFeedback(android.view.HapticFeedbackConstants.CONTEXT_CLICK)

                                            // Notify callback
                                            onMapClick(clickLat, clickLon)
                                        }
                                    }
                                } else Modifier
                            )
                            .pointerInput(Unit) {
                                detectTransformGestures { centroid, pan, zoom, _ ->
                                    val oldScale = scale

                                    // Calculate new scale (limit to reasonable range)
                                    val newScale = (scale * zoom).coerceIn(0.4f, 4f)
                                    val scaleDelta = newScale / oldScale

                                    // Calculate centroid relative to canvas center for zoom anchor
                                    val canvasWidth = size.width
                                    val canvasHeight = size.height
                                    val centerX = canvasWidth / 2
                                    val centerY = canvasHeight / 2
                                    val centroidRelX = centroid.x - centerX
                                    val centroidRelY = centroid.y - centerY

                                    // Adjust offset to zoom around the centroid (keeps point under fingers fixed)
                                    // Formula: newOffset = centroidRel * (1 - scaleDelta) + oldOffset * scaleDelta + pan
                                    offsetX = centroidRelX * (1 - scaleDelta) + offsetX * scaleDelta + pan.x
                                    offsetY = centroidRelY * (1 - scaleDelta) + offsetY * scaleDelta + pan.y

                                    scale = newScale

                                    // Determine if we should switch tile zoom levels
                                    // Only switch when we cross thresholds
                                    val targetTileZoom = when {
                                        scale >= 2.5f -> (tileZoomLevel + 1).coerceIn(9, 18)
                                        scale <= 0.6f -> (tileZoomLevel - 1).coerceIn(9, 18)
                                        else -> tileZoomLevel
                                    }

                                    // If we should switch zoom levels, reset scale and update tile zoom
                                    if (targetTileZoom != tileZoomLevel) {
                                        // Calculate the geographic point currently at screen center before switching
                                        val oldZoom = tileZoomLevel
                                        val oldCenterLat = (data.boundingBox.minLat + data.boundingBox.maxLat) / 2
                                        val oldCenterLon = (data.boundingBox.minLon + data.boundingBox.maxLon) / 2
                                        val oldCenterTileX = getTileX(oldCenterLon, oldZoom)
                                        val oldCenterTileY = getTileY(oldCenterLat, oldZoom)
                                        val oldCenterPixelX = getTilePixelX(oldCenterLon, oldZoom)
                                        val oldCenterPixelY = getTilePixelY(oldCenterLat, oldZoom)

                                        // Calculate which point is at screen center (in fractional tile coordinates)
                                        val screenCenterTileOffsetX = -offsetX / (scale * 256f)
                                        val screenCenterTileOffsetY = -offsetY / (scale * 256f)
                                        val screenCenterFracTileX = oldCenterTileX + oldCenterPixelX / 256.0 + screenCenterTileOffsetX
                                        val screenCenterFracTileY = oldCenterTileY + oldCenterPixelY / 256.0 + screenCenterTileOffsetY

                                        // Switch to new zoom level
                                        val zoomDiff = targetTileZoom - tileZoomLevel
                                        tileZoomLevel = targetTileZoom
                                        val scaleFactor = 2f.pow(-zoomDiff)
                                        scale *= scaleFactor

                                        // Calculate new center point coordinates at new zoom level
                                        val newCenterTileX = getTileX(oldCenterLon, tileZoomLevel)
                                        val newCenterTileY = getTileY(oldCenterLat, tileZoomLevel)
                                        val newCenterPixelX = getTilePixelX(oldCenterLon, tileZoomLevel)
                                        val newCenterPixelY = getTilePixelY(oldCenterLat, tileZoomLevel)

                                        // The screen center point's fractional tile coordinate at new zoom
                                        val newScreenCenterFracTileX = screenCenterFracTileX * 2.0.pow(zoomDiff)
                                        val newScreenCenterFracTileY = screenCenterFracTileY * 2.0.pow(zoomDiff)

                                        // Calculate new offset to keep the same point at screen center
                                        val newCenterFracTileX = newCenterTileX + newCenterPixelX / 256.0
                                        val newCenterFracTileY = newCenterTileY + newCenterPixelY / 256.0
                                        offsetX = -(newScreenCenterFracTileX - newCenterFracTileX).toFloat() * 256f * scale
                                        offsetY = -(newScreenCenterFracTileY - newCenterFracTileY).toFloat() * 256f * scale

                                        android.util.Log.d("TripMapPreview", "Switched to tile zoom $tileZoomLevel, adjusted scale to $scale, offset to ($offsetX, $offsetY)")
                                    }
                                }
                            }
                    ) {
                        // Use tileZoomLevel for rendering, scale for visual scaling
                        val currentZoom = tileZoomLevel.coerceIn(9, 18)

                        // Draw tiles with visual scaling
                        drawTiles(data, loadedTiles, currentZoom, offsetX, offsetY, scale)

                        // Draw route
                        drawRoute(data, currentZoom, offsetX, offsetY, scale)

                        // Draw start/end markers (from trip route)
                        drawMarkers(data, currentZoom, offsetX, offsetY, scale)

                        // Draw location selection markers (start/end locations for route planning)
                        if (startLocation != null) {
                            drawLocationMarker(
                                lat = startLocation.lat,
                                lon = startLocation.lon,
                                centerLat = (data.boundingBox.minLat + data.boundingBox.maxLat) / 2,
                                centerLon = (data.boundingBox.minLon + data.boundingBox.maxLon) / 2,
                                zoom = currentZoom,
                                offsetX = offsetX,
                                offsetY = offsetY,
                                scale = scale,
                                color = Color(0xFF10B981) // Green for start
                            )
                        }
                        if (endLocation != null) {
                            drawLocationMarker(
                                lat = endLocation.lat,
                                lon = endLocation.lon,
                                centerLat = (data.boundingBox.minLat + data.boundingBox.maxLat) / 2,
                                centerLon = (data.boundingBox.minLon + data.boundingBox.maxLon) / 2,
                                zoom = currentZoom,
                                offsetX = offsetX,
                                offsetY = offsetY,
                                scale = scale,
                                color = Color(0xFFEF4444) // Red for end
                            )
                        }

                        // Draw highlight marker from elevation profile selection
                        if (highlightLocation != null) {
                            drawLocationMarker(
                                lat = highlightLocation.lat,
                                lon = highlightLocation.lon,
                                centerLat = (data.boundingBox.minLat + data.boundingBox.maxLat) / 2,
                                centerLon = (data.boundingBox.minLon + data.boundingBox.maxLon) / 2,
                                zoom = currentZoom,
                                offsetX = offsetX,
                                offsetY = offsetY,
                                scale = scale,
                                color = Color(0xFFEAB308) // Amber for elevation profile highlight
                            )
                        }
                    }
                tileErrorMessage?.let { message ->
                    Surface(
                        modifier = Modifier
                            .align(Alignment.TopCenter)
                            .padding(12.dp),
                        color = MaterialTheme.colorScheme.errorContainer,
                        shape = MaterialTheme.shapes.medium
                    ) {
                        Row(
                            modifier = Modifier.padding(horizontal = 12.dp, vertical = 8.dp),
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            Text(
                                text = message,
                                modifier = Modifier.weight(1f),
                                color = MaterialTheme.colorScheme.onErrorContainer,
                                style = MaterialTheme.typography.bodySmall
                            )
                            TextButton(onClick = { tileErrorMessage = null }) {
                                Text("Dismiss")
                            }
                        }
                    }
                }
            } else {
                // Show loading or placeholder while map data loads
                Column(
                    modifier = Modifier.fillMaxSize(),
                    horizontalAlignment = Alignment.CenterHorizontally,
                    verticalArrangement = Arrangement.Center
                ) {
                    ContainedLoadingIndicator(
                        containerColor = MaterialTheme.colorScheme.primaryContainer,
                        indicatorColor = MaterialTheme.colorScheme.primary,
                        modifier = Modifier.size(60.dp)
                    )
                    Spacer(modifier = Modifier.height(8.dp))
                    Text(
                        "Loading map...",
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
            }
        }
    }
}

/**
 * Data class to hold map rendering information
 */
private data class MapData(
    val baseZoom: Int,
    val centerTileX: Int,
    val centerTileY: Int,
    val centerPixelX: Double,
    val centerPixelY: Double,
    val routePoints: List<GpxParser.GpxPoint>,
    val boundingBox: BoundingBox
)

/**
 * Load map data asynchronously
 */
private suspend fun loadMapData(context: android.content.Context, trip: Trip): MapData? {
    return withContext(Dispatchers.IO) {
        try {
            android.util.Log.d("TripMapPreview", "Loading map data for trip: ${trip.metadata.name}")

            // Parse GPX to get route points
            val points = parseGpxPoints(trip.gpxContent)
            android.util.Log.d("TripMapPreview", "Parsed ${points.size} GPX points")
            if (points.isEmpty()) {
                android.util.Log.e("TripMapPreview", "No GPX points found")
                return@withContext null
            }

            // Calculate bounding box if not available
            val bbox = trip.metadata.boundingBox
            android.util.Log.d("TripMapPreview", "Bounding box: $bbox")

            // Calculate center point
            val centerLat = (bbox.minLat + bbox.maxLat) / 2
            val centerLon = (bbox.minLon + bbox.maxLon) / 2

            // Calculate appropriate zoom level to fit the entire route
            val baseZoom = calculateFitZoom(bbox, 300.dp.value, 300.dp.value)
            android.util.Log.d("TripMapPreview", "Base zoom level: $baseZoom, center: ($centerLat, $centerLon)")

            // Get tile coordinates for center
            val centerTileX = getTileX(centerLon, baseZoom)
            val centerTileY = getTileY(centerLat, baseZoom)
            val centerPixelX = getTilePixelX(centerLon, baseZoom)
            val centerPixelY = getTilePixelY(centerLat, baseZoom)
            android.util.Log.d("TripMapPreview", "Center tile: ($centerTileX, $centerTileY), pixel: ($centerPixelX, $centerPixelY)")

            MapData(
                baseZoom = baseZoom,
                centerTileX = centerTileX,
                centerTileY = centerTileY,
                centerPixelX = centerPixelX,
                centerPixelY = centerPixelY,
                routePoints = points,
                boundingBox = bbox
            )
        } catch (e: Exception) {
            android.util.Log.e("TripMapPreview", "Error loading map data", e)
            e.printStackTrace()
            null
        }
    }
}

/**
 * Load map data from a single location point (for interactive picking without a trip)
 */
private suspend fun loadMapDataFromLocation(location: LocationPoint): MapData {
    return withContext(Dispatchers.IO) {
        android.util.Log.d("TripMapPreview", "Loading map data for location: ${location.displayName}")

        val baseZoom = 17 // Default zoom level for location picking - zoomed in

        val centerTileX = getTileX(location.lon, baseZoom)
        val centerTileY = getTileY(location.lat, baseZoom)
        val centerPixelX = getTilePixelX(location.lon, baseZoom)
        val centerPixelY = getTilePixelY(location.lat, baseZoom)

        // Create a small bounding box around the location for tile loading
        val latOffset = 0.01
        val lonOffset = 0.01
        val bbox = BoundingBox(
            minLat = location.lat - latOffset,
            maxLat = location.lat + latOffset,
            minLon = location.lon - lonOffset,
            maxLon = location.lon + lonOffset
        )

        MapData(
            baseZoom = baseZoom,
            centerTileX = centerTileX,
            centerTileY = centerTileY,
            centerPixelX = centerPixelX,
            centerPixelY = centerPixelY,
            routePoints = emptyList(), // No route for location picking
            boundingBox = bbox
        )
    }
}

/**
 * Calculate which tile keys are visible in the current viewport
 */
private fun calculateVisibleTileKeys(
    data: MapData,
    zoom: Int,
    offsetX: Float,
    offsetY: Float,
    scale: Float,
    canvasWidth: Float,
    canvasHeight: Float
): List<TileKey> {
    if (scale <= 0f || canvasWidth <= 0f || canvasHeight <= 0f) {
        return emptyList()
    }

    val centerLat = (data.boundingBox.minLat + data.boundingBox.maxLat) / 2
    val centerLon = (data.boundingBox.minLon + data.boundingBox.maxLon) / 2

    val centerTileX = getTileX(centerLon, zoom)
    val centerTileY = getTileY(centerLat, zoom)
    val centerPixelX = getTilePixelX(centerLon, zoom)
    val centerPixelY = getTilePixelY(centerLat, zoom)

    val centerGlobalX = centerTileX * 256.0 + centerPixelX
    val centerGlobalY = centerTileY * 256.0 + centerPixelY

    val halfWidth = canvasWidth / 2f
    val halfHeight = canvasHeight / 2f

    val leftGlobalX = centerGlobalX + ((0f - halfWidth - offsetX) / scale)
    val rightGlobalX = centerGlobalX + ((canvasWidth - halfWidth - offsetX) / scale)
    val topGlobalY = centerGlobalY + ((0f - halfHeight - offsetY) / scale)
    val bottomGlobalY = centerGlobalY + ((canvasHeight - halfHeight - offsetY) / scale)

    val minTileX = floor(min(leftGlobalX, rightGlobalX) / 256.0).toInt()
    val maxTileX = floor(max(leftGlobalX, rightGlobalX) / 256.0).toInt()
    val minTileY = floor(min(topGlobalY, bottomGlobalY) / 256.0).toInt()
    val maxTileY = floor(max(topGlobalY, bottomGlobalY) / 256.0).toInt()

    val tilesPerAxis = 1 shl zoom
    val padding = 1

    val clampedMinX = (minTileX - padding).coerceIn(0, tilesPerAxis - 1)
    val clampedMaxX = (maxTileX + padding).coerceIn(0, tilesPerAxis - 1)
    val clampedMinY = (minTileY - padding).coerceIn(0, tilesPerAxis - 1)
    val clampedMaxY = (maxTileY + padding).coerceIn(0, tilesPerAxis - 1)

    if (clampedMinX > clampedMaxX || clampedMinY > clampedMaxY) {
        return emptyList()
    }

    val tiles = ArrayList<TileKey>((clampedMaxX - clampedMinX + 1) * (clampedMaxY - clampedMinY + 1))
    for (tileX in clampedMinX..clampedMaxX) {
        for (tileY in clampedMinY..clampedMaxY) {
            tiles.add(TileKey(zoom, tileX, tileY))
        }
    }

    return tiles
}

/**
 * Parse GPX content to extract points
 */
private fun parseGpxPoints(gpxContent: String): List<GpxParser.GpxPoint> {
    val points = mutableListOf<GpxParser.GpxPoint>()
    val parser = android.util.Xml.newPullParser()
    parser.setFeature(org.xmlpull.v1.XmlPullParser.FEATURE_PROCESS_NAMESPACES, false)
    parser.setInput(gpxContent.byteInputStream(), null)

    var eventType = parser.eventType
    var currentLat: Double? = null
    var currentLon: Double? = null
    var currentEle: Double? = null
    var inTrkpt = false

    while (eventType != org.xmlpull.v1.XmlPullParser.END_DOCUMENT) {
        when (eventType) {
            org.xmlpull.v1.XmlPullParser.START_TAG -> {
                when (parser.name) {
                    "trkpt" -> {
                        inTrkpt = true
                        currentLat = parser.getAttributeValue(null, "lat")?.toDoubleOrNull()
                        currentLon = parser.getAttributeValue(null, "lon")?.toDoubleOrNull()
                        currentEle = null
                    }
                    "ele" -> {
                        if (inTrkpt && parser.next() == org.xmlpull.v1.XmlPullParser.TEXT) {
                            currentEle = parser.text?.toDoubleOrNull()
                        }
                    }
                }
            }
            org.xmlpull.v1.XmlPullParser.END_TAG -> {
                if (parser.name == "trkpt" && inTrkpt) {
                    if (currentLat != null && currentLon != null) {
                        points.add(GpxParser.GpxPoint(currentLat, currentLon, currentEle))
                    }
                    inTrkpt = false
                }
            }
        }
        eventType = parser.next()
    }

    return points
}

/**
 * Load a tile bitmap from PNG file
 */
private fun loadTileBitmapFromPng(file: File): Bitmap? {
    return try {
        BitmapFactory.decodeFile(file.absolutePath)
    } catch (e: Exception) {
        e.printStackTrace()
        null
    }
}

/**
 * Calculate zoom level to fit bounding box in given size
 */
private fun calculateFitZoom(bbox: BoundingBox, widthDp: Float, heightDp: Float): Int {
    val latDiff = (bbox.maxLat - bbox.minLat).coerceAtLeast(1e-6)
    val lonDiff = (bbox.maxLon - bbox.minLon).coerceAtLeast(1e-6)

    // Calculate zoom level that fits the route
    // Web Mercator: at zoom z, one degree longitude = 256 * 2^z / 360 pixels
    // We want: lonDiff degrees to fit in widthDp pixels
    // So: lonDiff * 256 * 2^z / 360 = widthDp
    // Therefore: 2^z = widthDp * 360 / (lonDiff * 256)
    // z = log2(widthDp * 360 / (lonDiff * 256))

    val lonZoom = log2((widthDp * 360.0) / (lonDiff * 256.0))

    // For latitude, it's more complex due to Mercator projection, but we can approximate
    val latZoom = log2((heightDp * 180.0) / (latDiff * 256.0))

    // Use the smaller zoom to ensure everything fits within the preview.
    val fitZoom = min(latZoom, lonZoom).toInt()

    android.util.Log.d("TripMapPreview", "calculateFitZoom: latDiff=$latDiff, lonDiff=$lonDiff, latZoom=$latZoom, lonZoom=$lonZoom, fitZoom=$fitZoom")

    // Use the same zoom bounds as available tiles.
    return fitZoom.coerceIn(9, 18)
}

/**
 * Calculate zoom level to fit the bounding box within the given viewport size.
 */
private fun calculateFitZoomForViewport(bbox: BoundingBox, widthPx: Float, heightPx: Float): Int {
    val latDiff = (bbox.maxLat - bbox.minLat).coerceAtLeast(1e-6)
    val lonDiff = (bbox.maxLon - bbox.minLon).coerceAtLeast(1e-6)
    if (widthPx <= 0f || heightPx <= 0f) return 17

    val lonZoom = log2((widthPx * 360.0) / (lonDiff * 256.0))
    val latZoom = log2((heightPx * 180.0) / (latDiff * 256.0))

    val fitZoom = min(latZoom, lonZoom).toInt()
    return fitZoom.coerceIn(9, 18)
}

/**
 * Draw tiles on canvas with fallback to adjacent zoom levels for smooth transitions
 */
private fun DrawScope.drawTiles(
    data: MapData,
    loadedTiles: Map<TileKey, Bitmap>,
    zoom: Int,
    offsetX: Float,
    offsetY: Float,
    scale: Float
) {
    val canvasWidth = size.width
    val canvasHeight = size.height
    val centerX = canvasWidth / 2
    val centerY = canvasHeight / 2

    // Get center tile coordinates at current zoom level
    val centerLat = (data.boundingBox.minLat + data.boundingBox.maxLat) / 2
    val centerLon = (data.boundingBox.minLon + data.boundingBox.maxLon) / 2

    val zoomCenterTileX = getTileX(centerLon, zoom)
    val zoomCenterTileY = getTileY(centerLat, zoom)
    val zoomCenterPixelX = getTilePixelX(centerLon, zoom)
    val zoomCenterPixelY = getTilePixelY(centerLat, zoom)

    // Calculate scaled tile size
    val scaledTileSize = (256f * scale).toInt()

    // Calculate which tiles we need to draw
    val tilesHorizontal = (canvasWidth / scaledTileSize).toInt() + 2
    val tilesVertical = (canvasHeight / scaledTileSize).toInt() + 2
    val tileOffsetX = ((offsetX - canvasWidth / 2 + zoomCenterPixelX.toFloat() * scale) / scaledTileSize).toInt()
    val tileOffsetY = ((offsetY - canvasHeight / 2 + zoomCenterPixelY.toFloat() * scale) / scaledTileSize).toInt()

    // Draw tiles in visible range
    for (dx in -tilesHorizontal..tilesHorizontal) {
        for (dy in -tilesVertical..tilesVertical) {
            val tileX = zoomCenterTileX + dx - tileOffsetX
            val tileY = zoomCenterTileY + dy - tileOffsetY

            val tileScreenX = centerX + (tileX - zoomCenterTileX) * 256f * scale - zoomCenterPixelX.toFloat() * scale + offsetX
            val tileScreenY = centerY + (tileY - zoomCenterTileY) * 256f * scale - zoomCenterPixelY.toFloat() * scale + offsetY

            // Only draw if tile is visible
            if (tileScreenX + scaledTileSize > 0 && tileScreenX < canvasWidth &&
                tileScreenY + scaledTileSize > 0 && tileScreenY < canvasHeight) {

                // Try to find the tile at current zoom level first
                val currentZoomKey = TileKey(zoom, tileX, tileY)
                val bitmap = loadedTiles[currentZoomKey]
                    ?: findFallbackTile(loadedTiles, tileX, tileY, zoom)

                if (bitmap != null) {
                    val tileData = findTileData(loadedTiles, tileX, tileY, zoom)
                    if (tileData != null) {
                        drawTileWithFallback(
                            bitmap = tileData.bitmap,
                            tileX = tileX,
                            tileY = tileY,
                            targetZoom = zoom,
                            actualZoom = tileData.zoom,
                            tileScreenX = tileScreenX,
                            tileScreenY = tileScreenY,
                            scale = scale
                        )
                    }
                }
            }
        }
    }
}

/**
 * Find a fallback tile from adjacent zoom levels if exact tile is not available
 */
private fun findFallbackTile(
    loadedTiles: Map<TileKey, Bitmap>,
    tileX: Int,
    tileY: Int,
    targetZoom: Int
): Bitmap? {
    // Try zoom-1 (lower resolution, will be scaled up)
    if (targetZoom > 9) {
        val parentX = tileX / 2
        val parentY = tileY / 2
        loadedTiles[TileKey(targetZoom - 1, parentX, parentY)]?.let { return it }
    }

    // Try zoom+1 (higher resolution, will be scaled down)
    if (targetZoom < 18) {
        for (childDx in 0..1) {
            for (childDy in 0..1) {
                val childX = tileX * 2 + childDx
                val childY = tileY * 2 + childDy
                loadedTiles[TileKey(targetZoom + 1, childX, childY)]?.let { return it }
            }
        }
    }

    return null
}

/**
 * Find tile data including which zoom level it's from
 */
private data class TileData(val bitmap: Bitmap, val zoom: Int, val x: Int, val y: Int)

private fun findTileData(
    loadedTiles: Map<TileKey, Bitmap>,
    tileX: Int,
    tileY: Int,
    targetZoom: Int
): TileData? {
    // Try exact match first
    loadedTiles[TileKey(targetZoom, tileX, tileY)]?.let {
        return TileData(it, targetZoom, tileX, tileY)
    }

    // Try zoom-1 (parent tile)
    if (targetZoom > 9) {
        val parentX = tileX / 2
        val parentY = tileY / 2
        loadedTiles[TileKey(targetZoom - 1, parentX, parentY)]?.let {
            return TileData(it, targetZoom - 1, parentX, parentY)
        }
    }

    // Try zoom+1 (child tiles)
    if (targetZoom < 18) {
        for (childDx in 0..1) {
            for (childDy in 0..1) {
                val childX = tileX * 2 + childDx
                val childY = tileY * 2 + childDy
                loadedTiles[TileKey(targetZoom + 1, childX, childY)]?.let {
                    return TileData(it, targetZoom + 1, childX, childY)
                }
            }
        }
    }

    return null
}

/**
 * Draw a tile, handling cases where we're using a fallback from a different zoom level
 */
private fun DrawScope.drawTileWithFallback(
    bitmap: Bitmap,
    tileX: Int,
    tileY: Int,
    targetZoom: Int,
    actualZoom: Int,
    tileScreenX: Float,
    tileScreenY: Float,
    scale: Float
) {
    val scaledTileSize = (256f * scale).toInt()

    if (actualZoom == targetZoom) {
        // Exact match - draw the whole tile
        drawImage(
            image = bitmap.asImageBitmap(),
            srcOffset = androidx.compose.ui.unit.IntOffset(0, 0),
            srcSize = androidx.compose.ui.unit.IntSize(256, 256),
            dstOffset = androidx.compose.ui.unit.IntOffset(tileScreenX.toInt(), tileScreenY.toInt()),
            dstSize = androidx.compose.ui.unit.IntSize(scaledTileSize + 1, scaledTileSize + 1)
        )
    } else if (actualZoom == targetZoom - 1) {
        // Parent tile (lower resolution) - extract the relevant quadrant
        val quadX = tileX % 2
        val quadY = tileY % 2

        drawImage(
            image = bitmap.asImageBitmap(),
            srcOffset = androidx.compose.ui.unit.IntOffset(quadX * 128, quadY * 128),
            srcSize = androidx.compose.ui.unit.IntSize(128, 128),
            dstOffset = androidx.compose.ui.unit.IntOffset(tileScreenX.toInt(), tileScreenY.toInt()),
            dstSize = androidx.compose.ui.unit.IntSize(scaledTileSize + 1, scaledTileSize + 1)
        )
    } else if (actualZoom == targetZoom + 1) {
        // Child tile (higher resolution) - draw at half size
        val parentX = tileX / 2
        val parentY = tileY / 2
        val childX = tileX
        val childY = tileY

        val offsetInParentX = (childX - parentX * 2) * scaledTileSize / 2
        val offsetInParentY = (childY - parentY * 2) * scaledTileSize / 2

        drawImage(
            image = bitmap.asImageBitmap(),
            srcOffset = androidx.compose.ui.unit.IntOffset(0, 0),
            srcSize = androidx.compose.ui.unit.IntSize(256, 256),
            dstOffset = androidx.compose.ui.unit.IntOffset(
                (tileScreenX - offsetInParentX).toInt(),
                (tileScreenY - offsetInParentY).toInt()
            ),
            dstSize = androidx.compose.ui.unit.IntSize(scaledTileSize * 2 + 1, scaledTileSize * 2 + 1)
        )
    }
}

/**
 * Draw route line on canvas
 */
private fun DrawScope.drawRoute(
    data: MapData,
    zoom: Int,
    offsetX: Float,
    offsetY: Float,
    scale: Float
) {
    val canvasWidth = size.width
    val canvasHeight = size.height
    val centerX = canvasWidth / 2
    val centerY = canvasHeight / 2

    // Get center coordinates at current zoom level
    val centerLat = (data.boundingBox.minLat + data.boundingBox.maxLat) / 2
    val centerLon = (data.boundingBox.minLon + data.boundingBox.maxLon) / 2

    val zoomCenterTileX = getTileX(centerLon, zoom)
    val zoomCenterTileY = getTileY(centerLat, zoom)
    val zoomCenterPixelX = getTilePixelX(centerLon, zoom)
    val zoomCenterPixelY = getTilePixelY(centerLat, zoom)

    val routePath = Path()
    var firstPoint = true

    // Convert route points to screen coordinates at current zoom with scaling
    data.routePoints.forEach { point ->
        val tileX = getTileX(point.lon, zoom)
        val tileY = getTileY(point.lat, zoom)
        val pixelX = getTilePixelX(point.lon, zoom)
        val pixelY = getTilePixelY(point.lat, zoom)

        val screenX = centerX + (tileX - zoomCenterTileX) * 256f * scale + pixelX.toFloat() * scale - zoomCenterPixelX.toFloat() * scale + offsetX
        val screenY = centerY + (tileY - zoomCenterTileY) * 256f * scale + pixelY.toFloat() * scale - zoomCenterPixelY.toFloat() * scale + offsetY

        if (firstPoint) {
            routePath.moveTo(screenX, screenY)
            firstPoint = false
        } else {
            routePath.lineTo(screenX, screenY)
        }
    }

    // Draw route with blue line (width scales with zoom)
    drawPath(
        path = routePath,
        color = Color(0xFF2563EB),
        style = Stroke(width = 6f * scale.coerceAtLeast(0.5f))
    )
}

/**
 * Draw start and end markers
 */
private fun DrawScope.drawMarkers(
    data: MapData,
    zoom: Int,
    offsetX: Float,
    offsetY: Float,
    scale: Float
) {
    if (data.routePoints.isEmpty()) return

    val canvasWidth = size.width
    val canvasHeight = size.height
    val centerX = canvasWidth / 2
    val centerY = canvasHeight / 2

    // Get center coordinates at current zoom level
    val centerLat = (data.boundingBox.minLat + data.boundingBox.maxLat) / 2
    val centerLon = (data.boundingBox.minLon + data.boundingBox.maxLon) / 2

    val zoomCenterTileX = getTileX(centerLon, zoom)
    val zoomCenterTileY = getTileY(centerLat, zoom)
    val zoomCenterPixelX = getTilePixelX(centerLon, zoom)
    val zoomCenterPixelY = getTilePixelY(centerLat, zoom)

    // Start marker (green) - with scaling
    val startPoint = data.routePoints.first()
    val startTileX = getTileX(startPoint.lon, zoom)
    val startTileY = getTileY(startPoint.lat, zoom)
    val startPixelX = getTilePixelX(startPoint.lon, zoom)
    val startPixelY = getTilePixelY(startPoint.lat, zoom)

    val startScreenX = centerX + (startTileX - zoomCenterTileX) * 256f * scale + startPixelX.toFloat() * scale - zoomCenterPixelX.toFloat() * scale + offsetX
    val startScreenY = centerY + (startTileY - zoomCenterTileY) * 256f * scale + startPixelY.toFloat() * scale - zoomCenterPixelY.toFloat() * scale + offsetY

    val markerScale = scale.coerceAtLeast(0.5f)
    drawCircle(
        color = Color(0xFF10B981),
        radius = 8f * markerScale,
        center = Offset(startScreenX, startScreenY)
    )
    drawCircle(
        color = Color.White,
        radius = 3f * markerScale,
        center = Offset(startScreenX, startScreenY)
    )

    // End marker (red) - with scaling
    val endPoint = data.routePoints.last()
    val endTileX = getTileX(endPoint.lon, zoom)
    val endTileY = getTileY(endPoint.lat, zoom)
    val endPixelX = getTilePixelX(endPoint.lon, zoom)
    val endPixelY = getTilePixelY(endPoint.lat, zoom)

    val endScreenX = centerX + (endTileX - zoomCenterTileX) * 256f * scale + endPixelX.toFloat() * scale - zoomCenterPixelX.toFloat() * scale + offsetX
    val endScreenY = centerY + (endTileY - zoomCenterTileY) * 256f * scale + endPixelY.toFloat() * scale - zoomCenterPixelY.toFloat() * scale + offsetY

    drawCircle(
        color = Color(0xFFEF4444),
        radius = 8f * markerScale,
        center = Offset(endScreenX, endScreenY)
    )
    drawCircle(
        color = Color.White,
        radius = 3f * markerScale,
        center = Offset(endScreenX, endScreenY)
    )
}

/**
 * Draw a location marker for route planning
 */
private fun DrawScope.drawLocationMarker(
    lat: Double,
    lon: Double,
    centerLat: Double,
    centerLon: Double,
    zoom: Int,
    offsetX: Float,
    offsetY: Float,
    scale: Float,
    color: Color
) {
    val canvasWidth = size.width
    val canvasHeight = size.height
    val centerX = canvasWidth / 2
    val centerY = canvasHeight / 2

    val centerTileX = getTileX(centerLon, zoom)
    val centerTileY = getTileY(centerLat, zoom)
    val centerPixelX = getTilePixelX(centerLon, zoom)
    val centerPixelY = getTilePixelY(centerLat, zoom)

    val markerTileX = getTileX(lon, zoom)
    val markerTileY = getTileY(lat, zoom)
    val markerPixelX = getTilePixelX(lon, zoom)
    val markerPixelY = getTilePixelY(lat, zoom)

    val markerScreenX = centerX + (markerTileX - centerTileX) * 256f * scale + markerPixelX.toFloat() * scale - centerPixelX.toFloat() * scale + offsetX
    val markerScreenY = centerY + (markerTileY - centerTileY) * 256f * scale + markerPixelY.toFloat() * scale - centerPixelY.toFloat() * scale + offsetY

    val markerScale = scale.coerceAtLeast(0.5f)

    // Outer circle
    drawCircle(
        color = color,
        radius = 12f * markerScale,
        center = Offset(markerScreenX, markerScreenY)
    )
    // Inner circle
    drawCircle(
        color = Color.White,
        radius = 5f * markerScale,
        center = Offset(markerScreenX, markerScreenY)
    )
}
