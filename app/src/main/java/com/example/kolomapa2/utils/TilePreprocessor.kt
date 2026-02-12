package com.example.kolomapa2.utils

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Color
import android.graphics.ColorMatrix
import android.graphics.ColorMatrixColorFilter
import android.graphics.Paint
import android.graphics.Canvas
import com.example.kolomapa2.models.DitherAlgorithm
import com.example.kolomapa2.models.ImageProcessingParams
import com.example.kolomapa2.models.MorphOp
import kotlin.math.pow
import kotlin.math.abs

class TilePreprocessor {

    // Image processing settings - MUST match Arduino exactly
    private val BRIGHTNESS = 0
    private val CONTRAST = 1.3f
    private val THRESHOLD = 128
    private val GAMMA = 2.50f

    private val gammaTable = IntArray(256)

    init {
        initGammaTable()
    }

    /**
     * Initialize gamma correction lookup table
     * Matches Arduino: gammaTable[i] = pow(i/255.0, GAMMA) * 255
     */
    private fun initGammaTable() {
        for (i in 0..255) {
            val normalized = i / 255.0f
            val corrected = normalized.pow(GAMMA)
            gammaTable[i] = (corrected * 255.0f).toInt()
        }
    }

    /**
     * Apply image processing (gamma, brightness, contrast)
     * Matches Arduino applyImageProcessing() function
     */
    private fun applyImageProcessing(gray: Int): Int {
        var processed = gammaTable[gray]
        processed += BRIGHTNESS
        processed = ((processed - 128) * CONTRAST + 128).toInt()
        return processed.coerceIn(0, 255)
    }

    /**
     * Convert PNG tile to 1-bit bitmap with Floyd-Steinberg dithering
     * This matches the Arduino pngDraw() callback EXACTLY
     *
     * @param pngData Raw PNG bytes
     * @return 1-bit packed bitmap (8 pixels per byte), 8KB for 256x256 tile
     */
    fun preprocessTile(pngData: ByteArray): ByteArray {
        // Decode PNG to bitmap
        val bitmap = BitmapFactory.decodeByteArray(pngData, 0, pngData.size)
            ?: throw IllegalArgumentException("Failed to decode PNG")

        if (bitmap.width != 256 || bitmap.height != 256) {
            throw IllegalArgumentException("Tile must be 256x256, got ${bitmap.width}x${bitmap.height}")
        }

        // 1-bit packed output: 256x256 pixels = 65536 bits = 8192 bytes
        val output = ByteArray(8192)

        // Error diffusion buffers (matches Arduino with padding)
        // THREE buffers for proper error propagation
        var errorBuffer1 = IntArray(260) // Next row errors
        var errorBuffer2 = IntArray(260) // Row after next
        var currentError = IntArray(260) // Current row errors (from previous iteration)

        for (y in 0 until 256) {
            // Rotate error buffers exactly like Arduino (pointer swapping)
            // This preserves error propagation from previous row!
            val temp = currentError
            currentError = errorBuffer1  // Use errors accumulated from previous row
            errorBuffer1 = errorBuffer2  // Previous "next row" becomes current "next row"
            errorBuffer2 = temp          // Old current becomes future row
            errorBuffer2.fill(0)         // Clear ONLY the future row

            for (x in 0 until 256) {
                // Get pixel as ARGB
                val pixel = bitmap.getPixel(x, y)

                // Extract RGB components (8-bit each)
                val r = (pixel shr 16) and 0xFF
                val g = (pixel shr 8) and 0xFF
                val b = pixel and 0xFF

                // Convert to grayscale (matches Arduino formula exactly)
                var gray = (r * 299 + g * 587 + b * 114) / 1000

                // Apply image processing (gamma, brightness, contrast)
                gray = applyImageProcessing(gray)

                // Add error diffusion from previous pixels
                gray += currentError[x + 2]
                gray = gray.coerceIn(0, 255)

                // Threshold to 1-bit
                val newPixel = if (gray >= THRESHOLD) 255 else 0
                val error = gray - newPixel

                // Distribute error using Floyd-Steinberg variant (matches Arduino)
                val eighthError = error / 8
                currentError[x + 3] += eighthError
                currentError[x + 4] += eighthError
                errorBuffer1[x + 1] += eighthError
                errorBuffer1[x + 2] += eighthError
                errorBuffer1[x + 3] += eighthError
                errorBuffer2[x + 2] += eighthError

                // Pack bit into output (8 pixels per byte)
                // Bit = 1 for white (255), 0 for black (0)
                if (newPixel == 255) {
                    val byteIndex = (y * 256 + x) / 8
                    val bitIndex = 7 - (x % 8)
                    output[byteIndex] = (output[byteIndex].toInt() or (1 shl bitIndex)).toByte()
                }
            }
            // Error buffers are rotated at start of next iteration - no copy needed!
        }

        bitmap.recycle()
        return output
    }

    /**
     * Get tile coordinates for a given lat/lon and zoom level
     * Matches Arduino getTileCoordinates()
     */
    fun getTileCoordinates(lat: Double, lon: Double, zoom: Int): Pair<Int, Int> {
        val latRad = Math.toRadians(lat)
        val n = 2.0.pow(zoom)

        val tileX = ((lon + 180.0) / 360.0 * n).toInt()
        val tileY = ((1.0 - kotlin.math.asinh(kotlin.math.tan(latRad)) / Math.PI) / 2.0 * n).toInt()

        return Pair(tileX, tileY)
    }

    /**
     * Calculate all tiles needed for a bounding box with padding
     * Returns list of (zoom, tileX, tileY) tuples
     */
    fun calculateRequiredTiles(
        minLat: Double,
        maxLat: Double,
        minLon: Double,
        maxLon: Double,
        zoomLevels: List<Int>,
        padding: Int = 2 // Number of tiles to add as padding on each side
    ): List<Triple<Int, Int, Int>> {
        val tiles = mutableListOf<Triple<Int, Int, Int>>()

        for (zoom in zoomLevels) {
            // Get corner tiles
            val (minTileX, maxTileY) = getTileCoordinates(minLat, minLon, zoom)
            val (maxTileX, minTileY) = getTileCoordinates(maxLat, maxLon, zoom)

            // Add padding
            val startX = (minTileX - padding).coerceAtLeast(0)
            val endX = maxTileX + padding
            val startY = (minTileY - padding).coerceAtLeast(0)
            val endY = maxTileY + padding

            for (tileX in startX..endX) {
                for (tileY in startY..endY) {
                    tiles.add(Triple(zoom, tileX, tileY))
                }
            }
        }

        return tiles
    }

    /**
     * Preprocess tile with custom parameters (for testing/tuning)
     * Returns both original and processed bitmaps for comparison
     */
    fun preprocessTileWithParams(
        pngData: ByteArray,
        params: ImageProcessingParams
    ): Pair<Bitmap, Bitmap> {
        // Decode original
        val original = BitmapFactory.decodeByteArray(pngData, 0, pngData.size)
            ?: throw IllegalArgumentException("Failed to decode PNG")

        // Create mutable copy for processing
        val processed = original.copy(Bitmap.Config.ARGB_8888, true)

        // Apply preprocessing filters
        applyPreprocessing(processed, params)

        // Convert to 1-bit with chosen dithering algorithm
        val monochrome = convertToMonochrome(processed, params)

        return Pair(original, monochrome)
    }

    /**
     * Apply preprocessing filters (sharpening, edge enhancement, bilateral filter)
     */
    private fun applyPreprocessing(bitmap: Bitmap, params: ImageProcessingParams) {
        val width = bitmap.width
        val height = bitmap.height

        // Apply bilateral filter if enabled (smooths while preserving edges)
        if (params.bilateralFilterRadius > 0) {
            applyBilateralFilter(bitmap, params.bilateralFilterRadius)
        }

        // Apply sharpening if enabled
        if (params.sharpness > 0f) {
            applySharpen(bitmap, params.sharpness)
        }

        // Apply edge enhancement if enabled
        if (params.edgeEnhancement > 0f) {
            applyEdgeEnhancement(bitmap, params.edgeEnhancement)
        }
    }

    /**
     * Simple bilateral filter approximation (edge-preserving smoothing)
     */
    private fun applyBilateralFilter(bitmap: Bitmap, radius: Int) {
        val width = bitmap.width
        val height = bitmap.height
        val pixels = IntArray(width * height)
        bitmap.getPixels(pixels, 0, width, 0, 0, width, height)
        val output = IntArray(width * height)

        for (y in 0 until height) {
            for (x in 0 until width) {
                val centerPixel = pixels[y * width + x]
                val centerGray = Color.red(centerPixel) // Assuming grayscale
                var sumWeight = 0f
                var sumColor = 0f

                for (dy in -radius..radius) {
                    for (dx in -radius..radius) {
                        val nx = (x + dx).coerceIn(0, width - 1)
                        val ny = (y + dy).coerceIn(0, height - 1)
                        val pixel = pixels[ny * width + nx]
                        val gray = Color.red(pixel)

                        // Spatial weight (distance)
                        val spatialDist = dx * dx + dy * dy
                        val spatialWeight = Math.exp(-spatialDist / (2.0 * radius * radius))

                        // Range weight (color difference)
                        val colorDiff = abs(gray - centerGray)
                        val rangeWeight = Math.exp(-colorDiff * colorDiff / 2000.0)

                        val weight = (spatialWeight * rangeWeight).toFloat()
                        sumWeight += weight
                        sumColor += gray * weight
                    }
                }

                val result = (sumColor / sumWeight).toInt().coerceIn(0, 255)
                output[y * width + x] = Color.rgb(result, result, result)
            }
        }

        bitmap.setPixels(output, 0, width, 0, 0, width, height)
    }

    /**
     * Apply sharpening using unsharp mask technique
     */
    private fun applySharpen(bitmap: Bitmap, strength: Float) {
        val width = bitmap.width
        val height = bitmap.height
        val pixels = IntArray(width * height)
        bitmap.getPixels(pixels, 0, width, 0, 0, width, height)
        val output = IntArray(width * height)

        // Simple 3x3 Laplacian kernel
        for (y in 1 until height - 1) {
            for (x in 1 until width - 1) {
                val center = Color.red(pixels[y * width + x])
                val laplacian = -Color.red(pixels[(y-1) * width + x]) -
                        Color.red(pixels[y * width + (x-1)]) +
                        4 * center -
                        Color.red(pixels[y * width + (x+1)]) -
                        Color.red(pixels[(y+1) * width + x])

                val sharpened = (center + laplacian * strength).toInt().coerceIn(0, 255)
                output[y * width + x] = Color.rgb(sharpened, sharpened, sharpened)
            }
        }

        bitmap.setPixels(output, 0, width, 0, 0, width, height)
    }

    /**
     * Apply edge enhancement using Sobel operator
     */
    private fun applyEdgeEnhancement(bitmap: Bitmap, strength: Float) {
        val width = bitmap.width
        val height = bitmap.height
        val pixels = IntArray(width * height)
        bitmap.getPixels(pixels, 0, width, 0, 0, width, height)
        val output = IntArray(width * height)

        for (y in 1 until height - 1) {
            for (x in 1 until width - 1) {
                // Sobel operators
                val gx = -Color.red(pixels[(y-1) * width + (x-1)]) +
                        Color.red(pixels[(y-1) * width + (x+1)]) -
                        2 * Color.red(pixels[y * width + (x-1)]) +
                        2 * Color.red(pixels[y * width + (x+1)]) -
                        Color.red(pixels[(y+1) * width + (x-1)]) +
                        Color.red(pixels[(y+1) * width + (x+1)])

                val gy = -Color.red(pixels[(y-1) * width + (x-1)]) -
                        2 * Color.red(pixels[(y-1) * width + x]) -
                        Color.red(pixels[(y-1) * width + (x+1)]) +
                        Color.red(pixels[(y+1) * width + (x-1)]) +
                        2 * Color.red(pixels[(y+1) * width + x]) +
                        Color.red(pixels[(y+1) * width + (x+1)])

                val edge = Math.sqrt((gx * gx + gy * gy).toDouble()).toInt()
                val original = Color.red(pixels[y * width + x])
                val enhanced = (original + edge * strength).toInt().coerceIn(0, 255)
                output[y * width + x] = Color.rgb(enhanced, enhanced, enhanced)
            }
        }

        bitmap.setPixels(output, 0, width, 0, 0, width, height)
    }

    /**
     * Convert to monochrome using selected dithering algorithm
     */
    private fun convertToMonochrome(bitmap: Bitmap, params: ImageProcessingParams): Bitmap {
        val width = bitmap.width
        val height = bitmap.height

        // Build gamma table for these params
        val customGammaTable = IntArray(256) { i ->
            val normalized = i / 255.0f
            val corrected = normalized.pow(params.gamma)
            (corrected * 255.0f).toInt()
        }

        // Convert to grayscale with processing
        val grayPixels = IntArray(width * height)
        for (y in 0 until height) {
            for (x in 0 until width) {
                val pixel = bitmap.getPixel(x, y)
                val r = Color.red(pixel)
                val g = Color.green(pixel)
                val b = Color.blue(pixel)

                // Convert to grayscale
                var gray = (r * 299 + g * 587 + b * 114) / 1000

                // Apply gamma, brightness, contrast
                gray = customGammaTable[gray]
                gray += params.brightness
                gray = ((gray - 128) * params.contrast + 128).toInt()
                gray = gray.coerceIn(0, 255)

                grayPixels[y * width + x] = gray
            }
        }

        // Apply dithering
        val monochrome = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888)
        when (params.ditherAlgorithm) {
            DitherAlgorithm.FLOYD_STEINBERG -> {
                applyFloydSteinberg(grayPixels, width, height, params, monochrome)
            }
            DitherAlgorithm.FLOYD_STEINBERG_REDUCED -> {
                applyFloydSteinbergReduced(grayPixels, width, height, params, monochrome)
            }
            DitherAlgorithm.ATKINSON -> {
                applyAtkinson(grayPixels, width, height, params, monochrome)
            }
            DitherAlgorithm.JARVIS_JUDICE_NINKE -> {
                applyJarvisJudiceNinke(grayPixels, width, height, params, monochrome)
            }
            DitherAlgorithm.STUCKI -> {
                applyStucki(grayPixels, width, height, params, monochrome)
            }
            DitherAlgorithm.SIERRA -> {
                applySierra(grayPixels, width, height, params, monochrome)
            }
            DitherAlgorithm.BURKES -> {
                applyBurkes(grayPixels, width, height, params, monochrome)
            }
            DitherAlgorithm.SIMPLE_THRESHOLD -> {
                applySimpleThreshold(grayPixels, width, height, params, monochrome)
            }
            DitherAlgorithm.ORDERED_BAYER_2x2 -> {
                applyOrderedDither(grayPixels, width, height, params, monochrome, 2)
            }
            DitherAlgorithm.ORDERED_BAYER_4x4 -> {
                applyOrderedDither(grayPixels, width, height, params, monochrome, 4)
            }
            DitherAlgorithm.ORDERED_BAYER_8x8 -> {
                applyOrderedDither(grayPixels, width, height, params, monochrome, 8)
            }
        }

        // Apply morphological operations if specified
        if (params.morphologicalOperation != MorphOp.NONE) {
            applyMorphologicalOperation(monochrome, params)
        }

        return monochrome
    }

    /**
     * Floyd-Steinberg dithering (current algorithm)
     */
    private fun applyFloydSteinberg(
        pixels: IntArray,
        width: Int,
        height: Int,
        params: ImageProcessingParams,
        output: Bitmap
    ) {
        val working = pixels.copyOf()
        val threshold = if (params.useAdaptiveThreshold) 0 else params.threshold
        val strength = params.errorDiffusionStrength

        for (y in 0 until height) {
            for (x in 0 until width) {
                var gray = working[y * width + x]

                // Adaptive threshold
                if (params.useAdaptiveThreshold) {
                    val localThreshold = calculateLocalThreshold(working, x, y, width, height, params)
                    gray = working[y * width + x]
                }

                val newPixel = if (gray >= (if (params.useAdaptiveThreshold)
                    calculateLocalThreshold(working, x, y, width, height, params) else params.threshold))
                    255 else 0
                val error = ((gray - newPixel) * strength).toInt()

                output.setPixel(x, y, if (newPixel == 255) Color.WHITE else Color.BLACK)

                // Distribute error
                if (x + 1 < width) working[y * width + (x + 1)] =
                    (working[y * width + (x + 1)] + error * 7 / 16).coerceIn(0, 255)
                if (y + 1 < height) {
                    if (x > 0) working[(y + 1) * width + (x - 1)] =
                        (working[(y + 1) * width + (x - 1)] + error * 3 / 16).coerceIn(0, 255)
                    working[(y + 1) * width + x] =
                        (working[(y + 1) * width + x] + error * 5 / 16).coerceIn(0, 255)
                    if (x + 1 < width) working[(y + 1) * width + (x + 1)] =
                        (working[(y + 1) * width + (x + 1)] + error * 1 / 16).coerceIn(0, 255)
                }
            }
        }
    }

    /**
     * Floyd-Steinberg with reduced error diffusion (better for thin lines)
     */
    private fun applyFloydSteinbergReduced(
        pixels: IntArray,
        width: Int,
        height: Int,
        params: ImageProcessingParams,
        output: Bitmap
    ) {
        val working = pixels.copyOf()
        val strength = params.errorDiffusionStrength * 0.5f // Reduced by half

        for (y in 0 until height) {
            for (x in 0 until width) {
                val gray = working[y * width + x]
                val newPixel = if (gray >= params.threshold) 255 else 0
                val error = ((gray - newPixel) * strength).toInt()

                output.setPixel(x, y, if (newPixel == 255) Color.WHITE else Color.BLACK)

                // Same distribution pattern but reduced strength
                if (x + 1 < width) working[y * width + (x + 1)] =
                    (working[y * width + (x + 1)] + error * 7 / 16).coerceIn(0, 255)
                if (y + 1 < height) {
                    if (x > 0) working[(y + 1) * width + (x - 1)] =
                        (working[(y + 1) * width + (x - 1)] + error * 3 / 16).coerceIn(0, 255)
                    working[(y + 1) * width + x] =
                        (working[(y + 1) * width + x] + error * 5 / 16).coerceIn(0, 255)
                    if (x + 1 < width) working[(y + 1) * width + (x + 1)] =
                        (working[(y + 1) * width + (x + 1)] + error * 1 / 16).coerceIn(0, 255)
                }
            }
        }
    }

    /**
     * Atkinson dithering (better for thin lines, less contrast)
     */
    private fun applyAtkinson(
        pixels: IntArray,
        width: Int,
        height: Int,
        params: ImageProcessingParams,
        output: Bitmap
    ) {
        val working = pixels.copyOf()
        val strength = params.errorDiffusionStrength

        for (y in 0 until height) {
            for (x in 0 until width) {
                val gray = working[y * width + x]
                val newPixel = if (gray >= params.threshold) 255 else 0
                val error = ((gray - newPixel) * strength / 8).toInt()

                output.setPixel(x, y, if (newPixel == 255) Color.WHITE else Color.BLACK)

                // Atkinson distributes to 6 neighbors
                if (x + 1 < width) working[y * width + (x + 1)] =
                    (working[y * width + (x + 1)] + error).coerceIn(0, 255)
                if (x + 2 < width) working[y * width + (x + 2)] =
                    (working[y * width + (x + 2)] + error).coerceIn(0, 255)
                if (y + 1 < height) {
                    if (x > 0) working[(y + 1) * width + (x - 1)] =
                        (working[(y + 1) * width + (x - 1)] + error).coerceIn(0, 255)
                    working[(y + 1) * width + x] =
                        (working[(y + 1) * width + x] + error).coerceIn(0, 255)
                    if (x + 1 < width) working[(y + 1) * width + (x + 1)] =
                        (working[(y + 1) * width + (x + 1)] + error).coerceIn(0, 255)
                }
                if (y + 2 < height) {
                    working[(y + 2) * width + x] =
                        (working[(y + 2) * width + x] + error).coerceIn(0, 255)
                }
            }
        }
    }

    /**
     * Jarvis-Judice-Ninke dithering (wider error distribution)
     */
    private fun applyJarvisJudiceNinke(
        pixels: IntArray,
        width: Int,
        height: Int,
        params: ImageProcessingParams,
        output: Bitmap
    ) {
        val working = pixels.copyOf()
        val strength = params.errorDiffusionStrength

        for (y in 0 until height) {
            for (x in 0 until width) {
                val gray = working[y * width + x]
                val newPixel = if (gray >= params.threshold) 255 else 0
                val error = ((gray - newPixel) * strength).toInt()

                output.setPixel(x, y, if (newPixel == 255) Color.WHITE else Color.BLACK)

                // JJN distributes across 3 rows
                if (x + 1 < width) working[y * width + (x + 1)] =
                    (working[y * width + (x + 1)] + error * 7 / 48).coerceIn(0, 255)
                if (x + 2 < width) working[y * width + (x + 2)] =
                    (working[y * width + (x + 2)] + error * 5 / 48).coerceIn(0, 255)

                if (y + 1 < height) {
                    if (x > 1) working[(y + 1) * width + (x - 2)] =
                        (working[(y + 1) * width + (x - 2)] + error * 3 / 48).coerceIn(0, 255)
                    if (x > 0) working[(y + 1) * width + (x - 1)] =
                        (working[(y + 1) * width + (x - 1)] + error * 5 / 48).coerceIn(0, 255)
                    working[(y + 1) * width + x] =
                        (working[(y + 1) * width + x] + error * 7 / 48).coerceIn(0, 255)
                    if (x + 1 < width) working[(y + 1) * width + (x + 1)] =
                        (working[(y + 1) * width + (x + 1)] + error * 5 / 48).coerceIn(0, 255)
                    if (x + 2 < width) working[(y + 1) * width + (x + 2)] =
                        (working[(y + 1) * width + (x + 2)] + error * 3 / 48).coerceIn(0, 255)
                }

                if (y + 2 < height) {
                    if (x > 1) working[(y + 2) * width + (x - 2)] =
                        (working[(y + 2) * width + (x - 2)] + error * 1 / 48).coerceIn(0, 255)
                    if (x > 0) working[(y + 2) * width + (x - 1)] =
                        (working[(y + 2) * width + (x - 1)] + error * 3 / 48).coerceIn(0, 255)
                    working[(y + 2) * width + x] =
                        (working[(y + 2) * width + x] + error * 5 / 48).coerceIn(0, 255)
                    if (x + 1 < width) working[(y + 2) * width + (x + 1)] =
                        (working[(y + 2) * width + (x + 1)] + error * 3 / 48).coerceIn(0, 255)
                    if (x + 2 < width) working[(y + 2) * width + (x + 2)] =
                        (working[(y + 2) * width + (x + 2)] + error * 1 / 48).coerceIn(0, 255)
                }
            }
        }
    }

    /**
     * Stucki dithering
     */
    private fun applyStucki(
        pixels: IntArray,
        width: Int,
        height: Int,
        params: ImageProcessingParams,
        output: Bitmap
    ) {
        val working = pixels.copyOf()
        val strength = params.errorDiffusionStrength

        for (y in 0 until height) {
            for (x in 0 until width) {
                val gray = working[y * width + x]
                val newPixel = if (gray >= params.threshold) 255 else 0
                val error = ((gray - newPixel) * strength).toInt()

                output.setPixel(x, y, if (newPixel == 255) Color.WHITE else Color.BLACK)

                // Stucki distributes across 3 rows
                if (x + 1 < width) working[y * width + (x + 1)] =
                    (working[y * width + (x + 1)] + error * 8 / 42).coerceIn(0, 255)
                if (x + 2 < width) working[y * width + (x + 2)] =
                    (working[y * width + (x + 2)] + error * 4 / 42).coerceIn(0, 255)

                if (y + 1 < height) {
                    if (x > 1) working[(y + 1) * width + (x - 2)] =
                        (working[(y + 1) * width + (x - 2)] + error * 2 / 42).coerceIn(0, 255)
                    if (x > 0) working[(y + 1) * width + (x - 1)] =
                        (working[(y + 1) * width + (x - 1)] + error * 4 / 42).coerceIn(0, 255)
                    working[(y + 1) * width + x] =
                        (working[(y + 1) * width + x] + error * 8 / 42).coerceIn(0, 255)
                    if (x + 1 < width) working[(y + 1) * width + (x + 1)] =
                        (working[(y + 1) * width + (x + 1)] + error * 4 / 42).coerceIn(0, 255)
                    if (x + 2 < width) working[(y + 1) * width + (x + 2)] =
                        (working[(y + 1) * width + (x + 2)] + error * 2 / 42).coerceIn(0, 255)
                }

                if (y + 2 < height) {
                    if (x > 1) working[(y + 2) * width + (x - 2)] =
                        (working[(y + 2) * width + (x - 2)] + error * 1 / 42).coerceIn(0, 255)
                    if (x > 0) working[(y + 2) * width + (x - 1)] =
                        (working[(y + 2) * width + (x - 1)] + error * 2 / 42).coerceIn(0, 255)
                    working[(y + 2) * width + x] =
                        (working[(y + 2) * width + x] + error * 4 / 42).coerceIn(0, 255)
                    if (x + 1 < width) working[(y + 2) * width + (x + 1)] =
                        (working[(y + 2) * width + (x + 1)] + error * 2 / 42).coerceIn(0, 255)
                    if (x + 2 < width) working[(y + 2) * width + (x + 2)] =
                        (working[(y + 2) * width + (x + 2)] + error * 1 / 42).coerceIn(0, 255)
                }
            }
        }
    }

    /**
     * Sierra dithering
     */
    private fun applySierra(
        pixels: IntArray,
        width: Int,
        height: Int,
        params: ImageProcessingParams,
        output: Bitmap
    ) {
        val working = pixels.copyOf()
        val strength = params.errorDiffusionStrength

        for (y in 0 until height) {
            for (x in 0 until width) {
                val gray = working[y * width + x]
                val newPixel = if (gray >= params.threshold) 255 else 0
                val error = ((gray - newPixel) * strength).toInt()

                output.setPixel(x, y, if (newPixel == 255) Color.WHITE else Color.BLACK)

                // Sierra distributes across 3 rows
                if (x + 1 < width) working[y * width + (x + 1)] =
                    (working[y * width + (x + 1)] + error * 5 / 32).coerceIn(0, 255)
                if (x + 2 < width) working[y * width + (x + 2)] =
                    (working[y * width + (x + 2)] + error * 3 / 32).coerceIn(0, 255)

                if (y + 1 < height) {
                    if (x > 1) working[(y + 1) * width + (x - 2)] =
                        (working[(y + 1) * width + (x - 2)] + error * 2 / 32).coerceIn(0, 255)
                    if (x > 0) working[(y + 1) * width + (x - 1)] =
                        (working[(y + 1) * width + (x - 1)] + error * 4 / 32).coerceIn(0, 255)
                    working[(y + 1) * width + x] =
                        (working[(y + 1) * width + x] + error * 5 / 32).coerceIn(0, 255)
                    if (x + 1 < width) working[(y + 1) * width + (x + 1)] =
                        (working[(y + 1) * width + (x + 1)] + error * 4 / 32).coerceIn(0, 255)
                    if (x + 2 < width) working[(y + 1) * width + (x + 2)] =
                        (working[(y + 1) * width + (x + 2)] + error * 2 / 32).coerceIn(0, 255)
                }

                if (y + 2 < height) {
                    if (x > 0) working[(y + 2) * width + (x - 1)] =
                        (working[(y + 2) * width + (x - 1)] + error * 2 / 32).coerceIn(0, 255)
                    working[(y + 2) * width + x] =
                        (working[(y + 2) * width + x] + error * 3 / 32).coerceIn(0, 255)
                    if (x + 1 < width) working[(y + 2) * width + (x + 1)] =
                        (working[(y + 2) * width + (x + 1)] + error * 2 / 32).coerceIn(0, 255)
                }
            }
        }
    }

    /**
     * Burkes dithering (2-row, faster than Sierra)
     */
    private fun applyBurkes(
        pixels: IntArray,
        width: Int,
        height: Int,
        params: ImageProcessingParams,
        output: Bitmap
    ) {
        val working = pixels.copyOf()
        val strength = params.errorDiffusionStrength

        for (y in 0 until height) {
            for (x in 0 until width) {
                val gray = working[y * width + x]
                val newPixel = if (gray >= params.threshold) 255 else 0
                val error = ((gray - newPixel) * strength).toInt()

                output.setPixel(x, y, if (newPixel == 255) Color.WHITE else Color.BLACK)

                // Burkes distributes across 2 rows
                if (x + 1 < width) working[y * width + (x + 1)] =
                    (working[y * width + (x + 1)] + error * 8 / 32).coerceIn(0, 255)
                if (x + 2 < width) working[y * width + (x + 2)] =
                    (working[y * width + (x + 2)] + error * 4 / 32).coerceIn(0, 255)

                if (y + 1 < height) {
                    if (x > 1) working[(y + 1) * width + (x - 2)] =
                        (working[(y + 1) * width + (x - 2)] + error * 2 / 32).coerceIn(0, 255)
                    if (x > 0) working[(y + 1) * width + (x - 1)] =
                        (working[(y + 1) * width + (x - 1)] + error * 4 / 32).coerceIn(0, 255)
                    working[(y + 1) * width + x] =
                        (working[(y + 1) * width + x] + error * 8 / 32).coerceIn(0, 255)
                    if (x + 1 < width) working[(y + 1) * width + (x + 1)] =
                        (working[(y + 1) * width + (x + 1)] + error * 4 / 32).coerceIn(0, 255)
                    if (x + 2 < width) working[(y + 1) * width + (x + 2)] =
                        (working[(y + 1) * width + (x + 2)] + error * 2 / 32).coerceIn(0, 255)
                }
            }
        }
    }

    /**
     * Simple threshold (no dithering)
     */
    private fun applySimpleThreshold(
        pixels: IntArray,
        width: Int,
        height: Int,
        params: ImageProcessingParams,
        output: Bitmap
    ) {
        for (y in 0 until height) {
            for (x in 0 until width) {
                val gray = pixels[y * width + x]
                val newPixel = if (gray >= params.threshold) 255 else 0
                output.setPixel(x, y, if (newPixel == 255) Color.WHITE else Color.BLACK)
            }
        }
    }

    /**
     * Ordered dithering using Bayer matrix
     */
    private fun applyOrderedDither(
        pixels: IntArray,
        width: Int,
        height: Int,
        params: ImageProcessingParams,
        output: Bitmap,
        matrixSize: Int
    ) {
        val matrix = when (matrixSize) {
            2 -> arrayOf(
                intArrayOf(0, 2),
                intArrayOf(3, 1)
            )
            4 -> arrayOf(
                intArrayOf(0, 8, 2, 10),
                intArrayOf(12, 4, 14, 6),
                intArrayOf(3, 11, 1, 9),
                intArrayOf(15, 7, 13, 5)
            )
            8 -> arrayOf(
                intArrayOf(0, 32, 8, 40, 2, 34, 10, 42),
                intArrayOf(48, 16, 56, 24, 50, 18, 58, 26),
                intArrayOf(12, 44, 4, 36, 14, 46, 6, 38),
                intArrayOf(60, 28, 52, 20, 62, 30, 54, 22),
                intArrayOf(3, 35, 11, 43, 1, 33, 9, 41),
                intArrayOf(51, 19, 59, 27, 49, 17, 57, 25),
                intArrayOf(15, 47, 7, 39, 13, 45, 5, 37),
                intArrayOf(63, 31, 55, 23, 61, 29, 53, 21)
            )
            else -> throw IllegalArgumentException("Invalid matrix size")
        }

        val scale = 256 / (matrixSize * matrixSize)

        for (y in 0 until height) {
            for (x in 0 until width) {
                val gray = pixels[y * width + x]
                val threshold = matrix[y % matrixSize][x % matrixSize] * scale
                val newPixel = if (gray >= threshold) 255 else 0
                output.setPixel(x, y, if (newPixel == 255) Color.WHITE else Color.BLACK)
            }
        }
    }

    /**
     * Calculate local adaptive threshold
     */
    private fun calculateLocalThreshold(
        pixels: IntArray,
        x: Int,
        y: Int,
        width: Int,
        height: Int,
        params: ImageProcessingParams
    ): Int {
        val halfBlock = params.adaptiveBlockSize / 2
        var sum = 0
        var count = 0

        for (dy in -halfBlock..halfBlock) {
            for (dx in -halfBlock..halfBlock) {
                val nx = (x + dx).coerceIn(0, width - 1)
                val ny = (y + dy).coerceIn(0, height - 1)
                sum += pixels[ny * width + nx]
                count++
            }
        }

        return (sum / count) - params.adaptiveC
    }

    /**
     * Apply morphological operations
     */
    private fun applyMorphologicalOperation(bitmap: Bitmap, params: ImageProcessingParams) {
        when (params.morphologicalOperation) {
            MorphOp.DILATE -> dilate(bitmap, params.morphKernelSize)
            MorphOp.ERODE -> erode(bitmap, params.morphKernelSize)
            MorphOp.OPEN -> {
                erode(bitmap, params.morphKernelSize)
                dilate(bitmap, params.morphKernelSize)
            }
            MorphOp.CLOSE -> {
                dilate(bitmap, params.morphKernelSize)
                erode(bitmap, params.morphKernelSize)
            }
            MorphOp.NONE -> {}
        }
    }

    /**
     * Dilate (expand white areas)
     */
    private fun dilate(bitmap: Bitmap, kernelSize: Int) {
        val width = bitmap.width
        val height = bitmap.height
        val pixels = IntArray(width * height)
        bitmap.getPixels(pixels, 0, width, 0, 0, width, height)
        val output = IntArray(width * height)

        val radius = kernelSize / 2

        for (y in 0 until height) {
            for (x in 0 until width) {
                var maxVal = 0
                for (dy in -radius..radius) {
                    for (dx in -radius..radius) {
                        val nx = (x + dx).coerceIn(0, width - 1)
                        val ny = (y + dy).coerceIn(0, height - 1)
                        val pixel = pixels[ny * width + nx]
                        if (Color.red(pixel) > maxVal) {
                            maxVal = Color.red(pixel)
                        }
                    }
                }
                output[y * width + x] = if (maxVal > 128) Color.WHITE else Color.BLACK
            }
        }

        bitmap.setPixels(output, 0, width, 0, 0, width, height)
    }

    /**
     * Erode (shrink white areas)
     */
    private fun erode(bitmap: Bitmap, kernelSize: Int) {
        val width = bitmap.width
        val height = bitmap.height
        val pixels = IntArray(width * height)
        bitmap.getPixels(pixels, 0, width, 0, 0, width, height)
        val output = IntArray(width * height)

        val radius = kernelSize / 2

        for (y in 0 until height) {
            for (x in 0 until width) {
                var minVal = 255
                for (dy in -radius..radius) {
                    for (dx in -radius..radius) {
                        val nx = (x + dx).coerceIn(0, width - 1)
                        val ny = (y + dy).coerceIn(0, height - 1)
                        val pixel = pixels[ny * width + nx]
                        if (Color.red(pixel) < minVal) {
                            minVal = Color.red(pixel)
                        }
                    }
                }
                output[y * width + x] = if (minVal > 128) Color.WHITE else Color.BLACK
            }
        }

        bitmap.setPixels(output, 0, width, 0, 0, width, height)
    }
}
