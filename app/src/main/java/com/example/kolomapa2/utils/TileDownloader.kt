package com.example.kolomapa2.utils

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import okhttp3.OkHttpClient
import okhttp3.Request
import java.io.IOException
import java.util.concurrent.TimeUnit

class TileDownloader(
    private val storageManager: StorageManager
) {
    private val client = OkHttpClient.Builder()
        .connectTimeout(15, TimeUnit.SECONDS)
        .readTimeout(15, TimeUnit.SECONDS)
        .build()

    // OpenStreetMap raster tile endpoint
    private val tileBaseUrl = "https://tile.openstreetmap.org"

    data class TileDownloadResult(
        val success: Boolean,
        val errorMessage: String? = null
    )

    data class TileDownloadBatchResult(
        val downloadedCount: Int,
        val failedCount: Int,
        val firstErrorMessage: String? = null
    )

    /**
     * Download a single tile (raw PNG only, processing happens on-demand)
     * Returns TileDownloadResult for status and user-facing error messaging
     */
    suspend fun downloadTile(zoom: Int, tileX: Int, tileY: Int): TileDownloadResult {
        return withContext(Dispatchers.IO) {
            try {
                // Check if tile already exists
                if (storageManager.rawTileExists(zoom, tileX, tileY)) {
                    return@withContext TileDownloadResult(success = true)
                }

                // Build URL
                val url = "$tileBaseUrl/$zoom/$tileX/$tileY.png"

                // Download PNG
                val request = Request.Builder()
                    .url(url)
                    .header("User-Agent", "KoloMapa2/1.0 BikeNav App")
                    .build()

                val response = client.newCall(request).execute()
                response.use {
                    if (!it.isSuccessful) {
                        val message = ApiErrorUtils.httpCodeMessage(it.code)
                            ?: "Tile server error (${it.code})"
                        return@withContext TileDownloadResult(success = false, errorMessage = message)
                    }

                    val pngData = it.body?.bytes()
                        ?: return@withContext TileDownloadResult(success = false, errorMessage = "Empty tile response")

                    // Verify it's a valid PNG
                    if (pngData.size < 8 || pngData[0] != 0x89.toByte() || pngData[1] != 0x50.toByte()) {
                        return@withContext TileDownloadResult(success = false, errorMessage = "Invalid map tile data")
                    }

                    // Save raw PNG only (processing happens on-demand when sending to ESP)
                    storageManager.saveRawTile(zoom, tileX, tileY, pngData)

                    TileDownloadResult(success = true)
                }
            } catch (e: IOException) {
                e.printStackTrace()
                TileDownloadResult(success = false, errorMessage = ApiErrorUtils.toUserMessage(e, "Failed to download tile"))
            } catch (e: Exception) {
                e.printStackTrace()
                TileDownloadResult(success = false, errorMessage = ApiErrorUtils.toUserMessage(e, "Failed to download tile"))
            }
        }
    }

    /**
     * Download multiple tiles with progress callback
     * Only downloads tiles that don't already exist
     *
     * @param tiles List of (zoom, tileX, tileY) to download
     * @param onProgress Called with (current, total) after each tile
     * @return Number of tiles successfully downloaded
     */
    suspend fun downloadTiles(
        tiles: List<Triple<Int, Int, Int>>,
        onProgress: (current: Int, total: Int) -> Unit = { _, _ -> }
    ): Int {
        var successCount = 0
        val total = tiles.size

        tiles.forEachIndexed { index, (zoom, tileX, tileY) ->
            val result = downloadTile(zoom, tileX, tileY)
            if (result.success) {
                successCount++
            }
            onProgress(index + 1, total)
        }

        return successCount
    }

    /**
     * Download tiles for a trip's bounding box
     * Returns number of tiles downloaded
     */
    suspend fun downloadTilesForTrip(
        minLat: Double,
        maxLat: Double,
        minLon: Double,
        maxLon: Double,
        tilePreprocessor: TilePreprocessor,
        onProgress: (current: Int, total: Int) -> Unit = { _, _ -> }
    ): TileDownloadBatchResult {
        return withContext(Dispatchers.IO) {
            // Use same zoom levels as Arduino (9-18)
            val zoomLevels = listOf(18, 17, 16, 15, 14, 13, 12, 11, 10, 9)

            // Calculate required tiles with padding
            val tiles = tilePreprocessor.calculateRequiredTiles(
                minLat, maxLat, minLon, maxLon, zoomLevels, padding = 2
            )

            val totalTiles = tiles.size
            var processedCount = 0
            var downloadedCount = 0
            var failedCount = 0
            var firstErrorMessage: String? = null

            println("Total tiles needed: $totalTiles")

            // Process all tiles (download new ones, skip cached ones)
            tiles.forEach { (zoom, tileX, tileY) ->
                if (storageManager.rawTileExists(zoom, tileX, tileY)) {
                    // Tile already cached, just increment progress
                    processedCount++
                    onProgress(processedCount, totalTiles)
                } else {
                    // Download the tile
                    val result = downloadTile(zoom, tileX, tileY)
                    if (result.success) {
                        downloadedCount++
                    } else {
                        failedCount++
                        if (firstErrorMessage == null && !result.errorMessage.isNullOrBlank()) {
                            firstErrorMessage = result.errorMessage
                        }
                    }
                    processedCount++
                    onProgress(processedCount, totalTiles)
                }
            }

            println("Downloaded $downloadedCount new tiles, $totalTiles total")
            TileDownloadBatchResult(
                downloadedCount = downloadedCount,
                failedCount = failedCount,
                firstErrorMessage = firstErrorMessage
            )
        }
    }
}
