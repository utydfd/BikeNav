package com.example.kolomapa2.utils

import android.content.Context
import android.content.pm.PackageManager
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.drawable.Drawable
import android.os.Build
import android.service.notification.StatusBarNotification
import android.util.Log
import android.util.LruCache
import androidx.annotation.RequiresApi
import androidx.core.graphics.drawable.toBitmap

/**
 * Utility class for extracting and converting notification icons to monochrome bitmaps
 * suitable for transmission to ESP32 e-paper display.
 */
object IconConverter {
    private const val TAG = "IconConverter"
    private const val ICON_SIZE = 39 // Target icon size (39x39 pixels)
    private const val BYTES_PER_ROW = 5 // ceil(39/8) = 5 bytes per row
    private const val TOTAL_BYTES = ICON_SIZE * BYTES_PER_ROW // 195 bytes total

    // LRU cache for converted icons (10 apps, ~2KB total)
    private val iconCache = LruCache<String, ByteArray>(10)

    /**
     * Extract and convert app icon from notification.
     * Uses fallback chain for reliability.
     * Returns null if icon extraction fails.
     */
    fun extractAndConvertIcon(
        sbn: StatusBarNotification,
        context: Context,
        packageManager: PackageManager
    ): ByteArray? {
        try {
            // Check cache first
            val packageName = sbn.packageName
            iconCache.get(packageName)?.let {
                Log.d(TAG, "Icon cache hit for $packageName")
                return it
            }

            // Extract icon using fallback chain
            val iconBitmap = extractAppIcon(sbn, context, packageManager) ?: run {
                Log.w(TAG, "Failed to extract icon for $packageName")
                return null
            }

            // Convert to monochrome 39x39 bitmap
            val monochromeData = convertToMonochrome(iconBitmap)

            // Cache the converted icon
            iconCache.put(packageName, monochromeData)
            Log.d(TAG, "Converted and cached icon for $packageName")

            return monochromeData

        } catch (e: Exception) {
            Log.e(TAG, "Error extracting/converting icon: ${e.message}", e)
            return null
        }
    }

    /**
     * Extract app icon using fallback chain:
     * 1. notification.getLargeIcon() - primary source
     * 2. notification.smallIcon - fallback
     * 3. packageManager.getApplicationIcon() - last resort
     */
    private fun extractAppIcon(
        sbn: StatusBarNotification,
        context: Context,
        packageManager: PackageManager
    ): Bitmap? {
        val notification = sbn.notification ?: return null

        // Try large icon first (preferred for app icons)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            notification.getLargeIcon()?.let { icon ->
                try {
                    val drawable = icon.loadDrawable(context)
                    return drawable?.toBitmap(ICON_SIZE, ICON_SIZE, Bitmap.Config.ARGB_8888)
                } catch (e: Exception) {
                    Log.w(TAG, "Failed to load large icon: ${e.message}")
                }
            }
        } else {
            @Suppress("DEPRECATION")
            notification.largeIcon?.let { bitmap ->
                return scaleBitmap(bitmap, ICON_SIZE)
            }
        }

        // Try small icon as fallback
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            notification.smallIcon?.let { icon ->
                try {
                    val drawable = icon.loadDrawable(context)
                    return drawable?.toBitmap(ICON_SIZE, ICON_SIZE, Bitmap.Config.ARGB_8888)
                } catch (e: Exception) {
                    Log.w(TAG, "Failed to load small icon: ${e.message}")
                }
            }
        }

        // Last resort: get app icon from package manager
        try {
            val drawable = packageManager.getApplicationIcon(sbn.packageName)
            return drawable.toBitmap(ICON_SIZE, ICON_SIZE, Bitmap.Config.ARGB_8888)
        } catch (e: Exception) {
            Log.w(TAG, "Failed to get app icon from package manager: ${e.message}")
        }

        return null
    }

    /**
     * Scale bitmap to target size using bilinear interpolation.
     */
    private fun scaleBitmap(bitmap: Bitmap, targetSize: Int): Bitmap {
        return Bitmap.createScaledBitmap(bitmap, targetSize, targetSize, true)
    }

    /**
     * Convert color bitmap to monochrome using Otsu's method for automatic threshold.
     * Output format: MSB-first bit packing, 5 bytes per row, 195 bytes total.
     */
    private fun convertToMonochrome(bitmap: Bitmap): ByteArray {
        // Ensure bitmap is exactly 39x39
        val scaledBitmap = if (bitmap.width != ICON_SIZE || bitmap.height != ICON_SIZE) {
            scaleBitmap(bitmap, ICON_SIZE)
        } else {
            bitmap
        }

        // Build histogram and calculate Otsu's threshold
        val histogram = IntArray(256)
        val brightnessMap = Array(ICON_SIZE) { IntArray(ICON_SIZE) }
        var totalPixels = 0

        for (y in 0 until ICON_SIZE) {
            for (x in 0 until ICON_SIZE) {
                val pixel = scaledBitmap.getPixel(x, y)
                val r = Color.red(pixel)
                val g = Color.green(pixel)
                val b = Color.blue(pixel)
                val alpha = Color.alpha(pixel)

                // Handle transparency: treat transparent pixels as white (255)
                val brightness = if (alpha < 128) {
                    255
                } else {
                    // Use weighted luminance formula (better than simple average)
                    ((0.299 * r + 0.587 * g + 0.114 * b).toInt()).coerceIn(0, 255)
                }

                brightnessMap[y][x] = brightness
                histogram[brightness]++
                totalPixels++
            }
        }

        // Calculate Otsu's threshold
        val threshold = calculateOtsuThreshold(histogram, totalPixels)
        Log.d(TAG, "Otsu's threshold: $threshold")

        // Pack into byte array (MSB-first, 5 bytes per row)
        val iconData = ByteArray(TOTAL_BYTES)
        var byteIndex = 0

        for (y in 0 until ICON_SIZE) {
            for (x in 0 until ICON_SIZE step 8) {
                var byte = 0

                for (bit in 0 until 8) {
                    val pixelX = x + bit
                    if (pixelX < ICON_SIZE) {
                        val brightness = brightnessMap[y][pixelX]

                        // Below threshold = black (1), above = white (0)
                        if (brightness < threshold) {
                            byte = byte or (1 shl (7 - bit)) // MSB first
                        }
                    }
                    // Padding bits remain 0 (white)
                }

                iconData[byteIndex++] = byte.toByte()
            }
        }

        // Cleanup scaled bitmap if we created a copy
        if (scaledBitmap !== bitmap) {
            scaledBitmap.recycle()
        }

        return iconData
    }

    /**
     * Calculate optimal threshold using Otsu's method.
     * Finds threshold that minimizes intra-class variance (maximizes inter-class variance).
     */
    private fun calculateOtsuThreshold(histogram: IntArray, totalPixels: Int): Int {
        var sum = 0L
        for (i in 0..255) {
            sum += i * histogram[i]
        }

        var sumBackground = 0L
        var weightBackground = 0
        var maxVariance = 0.0
        var threshold = 0

        for (t in 0..255) {
            weightBackground += histogram[t]
            if (weightBackground == 0) continue

            val weightForeground = totalPixels - weightBackground
            if (weightForeground == 0) break

            sumBackground += t * histogram[t]

            val meanBackground = sumBackground.toDouble() / weightBackground
            val meanForeground = (sum - sumBackground).toDouble() / weightForeground

            // Calculate inter-class variance
            val variance = weightBackground.toDouble() * weightForeground.toDouble() *
                          (meanBackground - meanForeground) * (meanBackground - meanForeground)

            if (variance > maxVariance) {
                maxVariance = variance
                threshold = t
            }
        }

        return threshold
    }

    /**
     * Clear the icon cache (useful for testing or memory management).
     */
    fun clearCache() {
        iconCache.evictAll()
        Log.d(TAG, "Icon cache cleared")
    }

    /**
     * Get current cache size.
     */
    fun getCacheSize(): Int {
        return iconCache.size()
    }
}
