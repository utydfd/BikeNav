package com.example.kolomapa2.utils

import android.graphics.BitmapFactory
import android.graphics.Color
import kotlin.math.asinh
import kotlin.math.pow
import kotlin.math.tan

/**
 * Builds a 1-bit radar overlay aligned to the map projection.
 * Output encoding: bit=1 is white (transparent), bit=0 is black (rain).
 */
class RadarOverlayProcessor(
    private val outputWidth: Int = BleManager.RADAR_IMAGE_WIDTH,
    private val outputHeight: Int = BleManager.RADAR_IMAGE_HEIGHT
) {

    companion object {
        private const val TILE_SIZE = 256.0
        private const val RADAR_WEST = 11.267
        private const val RADAR_EAST = 20.770
        private const val RADAR_SOUTH = 48.047
        private const val RADAR_NORTH = 52.167
        private const val THRESHOLD = 128
        private const val BRIGHTNESS = 0
        private const val CONTRAST = 1.3f
        private const val GAMMA = 2.50f
        private const val PALETTE_MATCH_THRESHOLD_SQ = 40000
        private const val MIN_INTENSITY_GRAY = 220
        private const val MAX_INTENSITY_GRAY = 60
        private const val SATURATION_CUTOFF = 0.12f
        private const val VALUE_CUTOFF = 0.08f

        private val RADAR_PALETTE = intArrayOf(
            Color.rgb(56, 0, 112),  // 4 dBZ
            Color.rgb(48, 0, 168),  // 8 dBZ
            Color.rgb(0, 0, 252),   // 12 dBZ
            Color.rgb(0, 108, 192), // 16 dBZ
            Color.rgb(0, 160, 0),   // 20 dBZ
            Color.rgb(0, 188, 0),   // 24 dBZ
            Color.rgb(52, 216, 0),  // 28 dBZ
            Color.rgb(156, 220, 0), // 32 dBZ
            Color.rgb(224, 220, 0), // 36 dBZ
            Color.rgb(252, 176, 0), // 40 dBZ
            Color.rgb(252, 132, 0), // 44 dBZ
            Color.rgb(252, 88, 0),  // 48 dBZ
            Color.rgb(252, 0, 0),   // 52 dBZ
            Color.rgb(160, 0, 0)    // 56 dBZ
        )

        private val INTENSITY_TO_GRAY = buildIntensityToGray()

        private fun buildIntensityToGray(): IntArray {
            val levels = RADAR_PALETTE.size
            val mapping = IntArray(levels + 1)
            mapping[0] = 255
            if (levels == 1) {
                mapping[1] = MIN_INTENSITY_GRAY
                return mapping
            }
            val range = MIN_INTENSITY_GRAY - MAX_INTENSITY_GRAY
            for (i in 1..levels) {
                val step = i - 1
                mapping[i] = (MIN_INTENSITY_GRAY - (range * step) / (levels - 1))
                    .coerceIn(0, 255)
            }
            return mapping
        }
    }

    private val gammaTable = IntArray(256)

    init {
        initGammaTable()
    }

    fun buildOverlay(pngData: ByteArray, centerLat: Double, centerLon: Double, zoom: Int): ByteArray {
        val bitmap = BitmapFactory.decodeByteArray(pngData, 0, pngData.size)
            ?: throw IllegalArgumentException("Failed to decode radar PNG")

        val radarWidth = bitmap.width
        val radarHeight = bitmap.height
        val radarPixels = IntArray(radarWidth * radarHeight)
        bitmap.getPixels(radarPixels, 0, radarWidth, 0, 0, radarWidth, radarHeight)
        bitmap.recycle()

        val outputBytesPerRow = (outputWidth + 7) / 8
        val output = ByteArray(outputBytesPerRow * outputHeight)

        val westNorm = lonToX(RADAR_WEST)
        val eastNorm = lonToX(RADAR_EAST)
        val northNorm = latToY(RADAR_NORTH)
        val southNorm = latToY(RADAR_SOUTH)

        val worldSize = TILE_SIZE * 2.0.pow(zoom.toDouble())
        val centerWorldX = lonToX(centerLon) * worldSize
        val centerWorldY = latToY(centerLat) * worldSize

        val centerX = outputWidth / 2.0
        val centerY = outputHeight / 2.0

        var errorBuffer1 = IntArray(outputWidth + 4)
        var errorBuffer2 = IntArray(outputWidth + 4)
        var currentError = IntArray(outputWidth + 4)

        for (y in 0 until outputHeight) {
            val temp = currentError
            currentError = errorBuffer1
            errorBuffer1 = errorBuffer2
            errorBuffer2 = temp
            errorBuffer2.fill(0)

            val worldY = centerWorldY + (y - centerY)
            val yNorm = worldY / worldSize
            val radarY = if (yNorm in northNorm..southNorm) {
                ((yNorm - northNorm) / (southNorm - northNorm) * (radarHeight - 1)).toInt()
            } else {
                -1
            }

            for (x in 0 until outputWidth) {
                val worldX = centerWorldX + (x - centerX)
                val xNorm = worldX / worldSize

                val intensityLevel = if (radarY >= 0 && xNorm in westNorm..eastNorm) {
                    val radarX = ((xNorm - westNorm) / (eastNorm - westNorm) * (radarWidth - 1)).toInt()
                    val pixel = radarPixels[radarY * radarWidth + radarX]
                    classifyIntensity(pixel)
                } else {
                    0
                }

                var gray = INTENSITY_TO_GRAY[intensityLevel]
                gray = applyImageProcessing(gray)

                val accumulatedError = if (intensityLevel == 0) 0 else currentError[x + 2]
                gray = (gray + accumulatedError).coerceIn(0, 255)

                val newPixel = if (gray >= THRESHOLD) 255 else 0
                val error = gray - newPixel

                if (intensityLevel != 0) {
                    val eighthError = error / 8
                    currentError[x + 3] += eighthError
                    currentError[x + 4] += eighthError
                    errorBuffer1[x + 1] += eighthError
                    errorBuffer1[x + 2] += eighthError
                    errorBuffer1[x + 3] += eighthError
                    errorBuffer2[x + 2] += eighthError
                }

                if (newPixel == 255) {
                    val byteIndex = (y * outputBytesPerRow) + (x / 8)
                    val bitIndex = 7 - (x % 8)
                    output[byteIndex] = (output[byteIndex].toInt() or (1 shl bitIndex)).toByte()
                }
            }
        }

        return output
    }

    private fun initGammaTable() {
        for (i in 0..255) {
            val normalized = i / 255.0f
            val corrected = normalized.pow(GAMMA)
            gammaTable[i] = (corrected * 255.0f).toInt()
        }
    }

    private fun applyImageProcessing(gray: Int): Int {
        var processed = gammaTable[gray]
        processed += BRIGHTNESS
        processed = ((processed - 128) * CONTRAST + 128).toInt()
        return processed.coerceIn(0, 255)
    }

    private fun classifyIntensity(pixel: Int): Int {
        val alpha = Color.alpha(pixel)
        if (alpha < 32) return 0

        var r = Color.red(pixel)
        var g = Color.green(pixel)
        var b = Color.blue(pixel)

        if (alpha in 1..254) {
            r = (r * 255 / alpha).coerceIn(0, 255)
            g = (g * 255 / alpha).coerceIn(0, 255)
            b = (b * 255 / alpha).coerceIn(0, 255)
        }
        if (r < 8 && g < 8 && b < 8) return 0

        val hsv = FloatArray(3)
        Color.RGBToHSV(r, g, b, hsv)
        val sat = hsv[1]
        val value = hsv[2]
        if (value < VALUE_CUTOFF) return 0
        if (sat < SATURATION_CUTOFF) return 0

        var closestIndex = -1
        var closestDistance = Int.MAX_VALUE
        for (i in RADAR_PALETTE.indices) {
            val paletteColor = RADAR_PALETTE[i]
            val dr = r - Color.red(paletteColor)
            val dg = g - Color.green(paletteColor)
            val db = b - Color.blue(paletteColor)
            val distance = dr * dr + dg * dg + db * db
            if (distance < closestDistance) {
                closestDistance = distance
                closestIndex = i
                if (distance == 0) break
            }
        }

        if (closestDistance > PALETTE_MATCH_THRESHOLD_SQ) return 0
        return closestIndex + 1
    }

    private fun lonToX(lon: Double): Double {
        return (lon + 180.0) / 360.0
    }

    private fun latToY(lat: Double): Double {
        val latRad = Math.toRadians(lat)
        return (1.0 - asinh(tan(latRad)) / Math.PI) / 2.0
    }
}
