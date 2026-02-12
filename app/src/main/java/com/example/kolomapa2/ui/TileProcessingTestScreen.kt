package com.example.kolomapa2.ui

import android.graphics.Bitmap
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.unit.dp
import com.example.kolomapa2.models.DitherAlgorithm
import com.example.kolomapa2.models.ImageProcessingParams
import com.example.kolomapa2.models.MorphOp
import com.example.kolomapa2.utils.StorageManager
import com.example.kolomapa2.utils.TilePreprocessor
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun TileProcessingTestScreen(
    storageManager: StorageManager,
    onBackClick: () -> Unit
) {
    val tilePreprocessor = remember { TilePreprocessor() }
    val scope = rememberCoroutineScope()

    // State for tile selection
    var selectedZoom by remember { mutableIntStateOf(15) }
    var availableTiles by remember { mutableStateOf<List<Triple<Int, Int, Int>>>(emptyList()) }
    var selectedTile by remember { mutableStateOf<Triple<Int, Int, Int>?>(null) }

    // State for image processing
    var originalBitmap by remember { mutableStateOf<Bitmap?>(null) }
    var processedBitmap by remember { mutableStateOf<Bitmap?>(null) }
    var isProcessing by remember { mutableStateOf(false) }
    var showTilePicker by remember { mutableStateOf(false) }

    // Image processing parameters with state
    var params by remember { mutableStateOf(ImageProcessingParams.default()) }

    // Preset selection
    var selectedPreset by remember { mutableStateOf("Custom") }

    // Load available tiles for selected zoom level
    LaunchedEffect(selectedZoom) {
        withContext(Dispatchers.IO) {
            val tiles = storageManager.getAvailableTiles(selectedZoom)
            availableTiles = tiles
            if (selectedTile == null && tiles.isNotEmpty()) {
                selectedTile = tiles.first()
            }
        }
    }

    // Process tile when selected or params change
    LaunchedEffect(selectedTile, params) {
        selectedTile?.let { (zoom, x, y) ->
            isProcessing = true
            withContext(Dispatchers.IO) {
                try {
                    val tileData = storageManager.loadRawTile(zoom, x, y)
                    if (tileData != null) {
                        val (original, processed) = tilePreprocessor.preprocessTileWithParams(
                            tileData,
                            params
                        )
                        originalBitmap = original
                        processedBitmap = processed
                    }
                } catch (e: Exception) {
                    e.printStackTrace()
                } finally {
                    isProcessing = false
                }
            }
        }
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Tile Processing Test") },
                navigationIcon = {
                    IconButton(onClick = onBackClick) {
                        Icon(Icons.AutoMirrored.Filled.ArrowBack, "Back")
                    }
                },
                actions = {
                    IconButton(
                        onClick = { showTilePicker = true },
                        enabled = availableTiles.isNotEmpty()
                    ) {
                        Icon(Icons.Default.Map, "Select Tile")
                    }
                }
            )
        }
    ) { paddingValues ->
        LazyColumn(
            modifier = Modifier
                .fillMaxSize()
                .padding(paddingValues)
                .padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp)
        ) {
            // Zoom level selector
            item {
                Card {
                    Column(modifier = Modifier.padding(16.dp)) {
                        Text("Zoom Level: $selectedZoom", style = MaterialTheme.typography.titleMedium)
                        Slider(
                            value = selectedZoom.toFloat(),
                            onValueChange = { selectedZoom = it.toInt() },
                            valueRange = 9f..18f,
                            steps = 8
                        )
                        Text(
                            "Available tiles: ${availableTiles.size}",
                            style = MaterialTheme.typography.bodySmall
                        )
                    }
                }
            }

            // Image preview
            item {
                Card {
                    Column(modifier = Modifier.padding(16.dp)) {
                        Text("Preview", style = MaterialTheme.typography.titleMedium)
                        Spacer(modifier = Modifier.height(8.dp))

                        if (isProcessing) {
                            Box(
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .height(300.dp),
                                contentAlignment = Alignment.Center
                            ) {
                                CircularProgressIndicator()
                            }
                        } else {
                            Row(
                                modifier = Modifier.fillMaxWidth(),
                                horizontalArrangement = Arrangement.spacedBy(8.dp)
                            ) {
                                // Original
                                Column(modifier = Modifier.weight(1f)) {
                                    Text("Original", style = MaterialTheme.typography.labelSmall)
                                    originalBitmap?.let { bitmap ->
                                        Image(
                                            bitmap = bitmap.asImageBitmap(),
                                            contentDescription = "Original",
                                            modifier = Modifier
                                                .fillMaxWidth()
                                                .aspectRatio(1f)
                                                .border(1.dp, MaterialTheme.colorScheme.outline),
                                            contentScale = ContentScale.Fit
                                        )
                                    }
                                }

                                // Processed
                                Column(modifier = Modifier.weight(1f)) {
                                    Text("Processed", style = MaterialTheme.typography.labelSmall)
                                    processedBitmap?.let { bitmap ->
                                        Image(
                                            bitmap = bitmap.asImageBitmap(),
                                            contentDescription = "Processed",
                                            modifier = Modifier
                                                .fillMaxWidth()
                                                .aspectRatio(1f)
                                                .border(1.dp, MaterialTheme.colorScheme.outline),
                                            contentScale = ContentScale.Fit
                                        )
                                    }
                                }
                            }
                        }

                        selectedTile?.let { (zoom, x, y) ->
                            Spacer(modifier = Modifier.height(8.dp))
                            Text(
                                "Tile: Z=$zoom X=$x Y=$y",
                                style = MaterialTheme.typography.bodySmall
                            )
                        }
                    }
                }
            }

            // Presets
            item {
                Card {
                    Column(modifier = Modifier.padding(16.dp)) {
                        Text("Presets", style = MaterialTheme.typography.titleMedium)
                        Spacer(modifier = Modifier.height(8.dp))

                        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                            FilterChip(
                                selected = selectedPreset == "Default",
                                onClick = {
                                    selectedPreset = "Default"
                                    params = ImageProcessingParams.default()
                                },
                                label = { Text("Default") }
                            )
                            FilterChip(
                                selected = selectedPreset == "Thin Lines",
                                onClick = {
                                    selectedPreset = "Thin Lines"
                                    params = ImageProcessingParams.thinLinePreserving()
                                },
                                label = { Text("Thin Lines") }
                            )
                            FilterChip(
                                selected = selectedPreset == "High Contrast",
                                onClick = {
                                    selectedPreset = "High Contrast"
                                    params = ImageProcessingParams.highContrast()
                                },
                                label = { Text("High Contrast") }
                            )
                            FilterChip(
                                selected = selectedPreset == "Detail",
                                onClick = {
                                    selectedPreset = "Detail"
                                    params = ImageProcessingParams.detailPreserving()
                                },
                                label = { Text("Detail") }
                            )
                        }
                    }
                }
            }

            // Basic parameters
            item {
                Card {
                    Column(modifier = Modifier.padding(16.dp)) {
                        Text("Basic Parameters", style = MaterialTheme.typography.titleMedium)
                        Spacer(modifier = Modifier.height(8.dp))

                        // Brightness
                        ParameterSlider(
                            label = "Brightness",
                            value = params.brightness.toFloat(),
                            valueRange = -100f..100f,
                            onValueChange = {
                                selectedPreset = "Custom"
                                params = params.copy(brightness = it.toInt())
                            }
                        )

                        // Contrast
                        ParameterSlider(
                            label = "Contrast",
                            value = params.contrast,
                            valueRange = 0.5f..3.0f,
                            onValueChange = {
                                selectedPreset = "Custom"
                                params = params.copy(contrast = it)
                            }
                        )

                        // Threshold
                        ParameterSlider(
                            label = "Threshold",
                            value = params.threshold.toFloat(),
                            valueRange = 0f..255f,
                            onValueChange = {
                                selectedPreset = "Custom"
                                params = params.copy(threshold = it.toInt())
                            }
                        )

                        // Gamma
                        ParameterSlider(
                            label = "Gamma",
                            value = params.gamma,
                            valueRange = 0.5f..4.0f,
                            onValueChange = {
                                selectedPreset = "Custom"
                                params = params.copy(gamma = it)
                            }
                        )
                    }
                }
            }

            // Dithering algorithm
            item {
                Card {
                    Column(modifier = Modifier.padding(16.dp)) {
                        Text("Dithering Algorithm", style = MaterialTheme.typography.titleMedium)
                        Spacer(modifier = Modifier.height(8.dp))

                        DitherAlgorithm.values().forEach { algorithm ->
                            Row(
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .clickable {
                                        selectedPreset = "Custom"
                                        params = params.copy(ditherAlgorithm = algorithm)
                                    }
                                    .padding(vertical = 8.dp),
                                verticalAlignment = Alignment.CenterVertically
                            ) {
                                RadioButton(
                                    selected = params.ditherAlgorithm == algorithm,
                                    onClick = {
                                        selectedPreset = "Custom"
                                        params = params.copy(ditherAlgorithm = algorithm)
                                    }
                                )
                                Spacer(modifier = Modifier.width(8.dp))
                                Text(algorithm.name.replace("_", " "))
                            }
                        }

                        if (params.ditherAlgorithm != DitherAlgorithm.SIMPLE_THRESHOLD &&
                            !params.ditherAlgorithm.name.startsWith("ORDERED")) {
                            Spacer(modifier = Modifier.height(8.dp))
                            ParameterSlider(
                                label = "Error Diffusion Strength",
                                value = params.errorDiffusionStrength,
                                valueRange = 0f..2f,
                                onValueChange = {
                                    selectedPreset = "Custom"
                                    params = params.copy(errorDiffusionStrength = it)
                                }
                            )
                        }
                    }
                }
            }

            // Advanced preprocessing
            item {
                Card {
                    Column(modifier = Modifier.padding(16.dp)) {
                        Text("Advanced Preprocessing", style = MaterialTheme.typography.titleMedium)
                        Spacer(modifier = Modifier.height(8.dp))

                        // Sharpness
                        ParameterSlider(
                            label = "Sharpness",
                            value = params.sharpness,
                            valueRange = 0f..2f,
                            onValueChange = {
                                selectedPreset = "Custom"
                                params = params.copy(sharpness = it)
                            }
                        )

                        // Edge Enhancement
                        ParameterSlider(
                            label = "Edge Enhancement",
                            value = params.edgeEnhancement,
                            valueRange = 0f..1f,
                            onValueChange = {
                                selectedPreset = "Custom"
                                params = params.copy(edgeEnhancement = it)
                            }
                        )

                        // Bilateral Filter
                        ParameterSlider(
                            label = "Bilateral Filter Radius",
                            value = params.bilateralFilterRadius.toFloat(),
                            valueRange = 0f..5f,
                            onValueChange = {
                                selectedPreset = "Custom"
                                params = params.copy(bilateralFilterRadius = it.toInt())
                            }
                        )
                    }
                }
            }

            // Morphological operations
            item {
                Card {
                    Column(modifier = Modifier.padding(16.dp)) {
                        Text("Morphological Operations", style = MaterialTheme.typography.titleMedium)
                        Spacer(modifier = Modifier.height(8.dp))

                        MorphOp.values().forEach { morphOp ->
                            Row(
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .clickable {
                                        selectedPreset = "Custom"
                                        params = params.copy(morphologicalOperation = morphOp)
                                    }
                                    .padding(vertical = 8.dp),
                                verticalAlignment = Alignment.CenterVertically
                            ) {
                                RadioButton(
                                    selected = params.morphologicalOperation == morphOp,
                                    onClick = {
                                        selectedPreset = "Custom"
                                        params = params.copy(morphologicalOperation = morphOp)
                                    }
                                )
                                Spacer(modifier = Modifier.width(8.dp))
                                Column {
                                    Text(morphOp.name)
                                    Text(
                                        getMorphOpDescription(morphOp),
                                        style = MaterialTheme.typography.bodySmall,
                                        color = MaterialTheme.colorScheme.onSurfaceVariant
                                    )
                                }
                            }
                        }

                        if (params.morphologicalOperation != MorphOp.NONE) {
                            Spacer(modifier = Modifier.height(8.dp))
                            ParameterSlider(
                                label = "Kernel Size",
                                value = params.morphKernelSize.toFloat(),
                                valueRange = 3f..7f,
                                steps = 2,
                                onValueChange = {
                                    selectedPreset = "Custom"
                                    // Ensure odd number
                                    val size = it.toInt()
                                    params = params.copy(morphKernelSize = if (size % 2 == 0) size + 1 else size)
                                }
                            )
                        }
                    }
                }
            }

            // Adaptive thresholding
            item {
                Card {
                    Column(modifier = Modifier.padding(16.dp)) {
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.SpaceBetween,
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            Text("Adaptive Threshold", style = MaterialTheme.typography.titleMedium)
                            Switch(
                                checked = params.useAdaptiveThreshold,
                                onCheckedChange = {
                                    selectedPreset = "Custom"
                                    params = params.copy(useAdaptiveThreshold = it)
                                }
                            )
                        }

                        if (params.useAdaptiveThreshold) {
                            Spacer(modifier = Modifier.height(8.dp))

                            ParameterSlider(
                                label = "Block Size",
                                value = params.adaptiveBlockSize.toFloat(),
                                valueRange = 3f..31f,
                                steps = 14,
                                onValueChange = {
                                    selectedPreset = "Custom"
                                    // Ensure odd number
                                    val size = it.toInt()
                                    params = params.copy(adaptiveBlockSize = if (size % 2 == 0) size + 1 else size)
                                }
                            )

                            ParameterSlider(
                                label = "Constant C",
                                value = params.adaptiveC.toFloat(),
                                valueRange = -10f..10f,
                                onValueChange = {
                                    selectedPreset = "Custom"
                                    params = params.copy(adaptiveC = it.toInt())
                                }
                            )
                        }
                    }
                }
            }
        }
    }

    // Tile picker dialog
    if (showTilePicker) {
        AlertDialog(
            onDismissRequest = { showTilePicker = false },
            title = { Text("Select Tile (Zoom $selectedZoom)") },
            text = {
                LazyVerticalGrid(
                    columns = GridCells.Fixed(3),
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                    verticalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    items(availableTiles) { tile ->
                        val (zoom, x, y) = tile
                        Card(
                            modifier = Modifier
                                .aspectRatio(1f)
                                .clickable {
                                    selectedTile = tile
                                    showTilePicker = false
                                },
                            colors = if (selectedTile == tile) {
                                CardDefaults.cardColors(
                                    containerColor = MaterialTheme.colorScheme.primaryContainer
                                )
                            } else {
                                CardDefaults.cardColors()
                            }
                        ) {
                            Column(
                                modifier = Modifier
                                    .fillMaxSize()
                                    .padding(8.dp),
                                horizontalAlignment = Alignment.CenterHorizontally,
                                verticalArrangement = Arrangement.Center
                            ) {
                                Text("X: $x", style = MaterialTheme.typography.bodySmall)
                                Text("Y: $y", style = MaterialTheme.typography.bodySmall)
                            }
                        }
                    }
                }
            },
            confirmButton = {
                TextButton(onClick = { showTilePicker = false }) {
                    Text("Close")
                }
            }
        )
    }
}

@Composable
fun ParameterSlider(
    label: String,
    value: Float,
    valueRange: ClosedFloatingPointRange<Float>,
    steps: Int = 0,
    onValueChange: (Float) -> Unit
) {
    Column {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween
        ) {
            Text(label, style = MaterialTheme.typography.bodyMedium)
            Text(
                String.format("%.2f", value),
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.primary
            )
        }
        Slider(
            value = value,
            onValueChange = onValueChange,
            valueRange = valueRange,
            steps = steps
        )
    }
    Spacer(modifier = Modifier.height(8.dp))
}

fun getMorphOpDescription(morphOp: MorphOp): String {
    return when (morphOp) {
        MorphOp.NONE -> "No morphological operation"
        MorphOp.DILATE -> "Expand white areas (connect pixels)"
        MorphOp.ERODE -> "Shrink white areas (disconnect pixels)"
        MorphOp.OPEN -> "Remove small white noise"
        MorphOp.CLOSE -> "Fill gaps, connect thin lines"
    }
}
