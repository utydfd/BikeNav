package com.example.kolomapa2.utils

import android.content.Context
import android.os.Environment
import com.example.kolomapa2.models.Trip
import com.example.kolomapa2.models.TripMetadata
import kotlinx.serialization.encodeToString
import kotlinx.serialization.json.Json
import java.io.File

class StorageManager(private val context: Context) {
    // Use external storage so data persists across app reinstalls
    private val baseDir = File(Environment.getExternalStorageDirectory(), "KoloMapa2")
    private val tripsDir = File(baseDir, "Trips")
    private val recordingsDir = File(baseDir, "Recordings")
    private val mapDir = File(baseDir, "Map")
    private val rawTilesDir = File(baseDir, "RawTiles") // For testing/tuning

    init {
        // Create directories if they don't exist
        tripsDir.mkdirs()
        recordingsDir.mkdirs()
        mapDir.mkdirs()
        rawTilesDir.mkdirs()
    }

    /**
     * Save a trip to storage (both GPX and metadata JSON)
     */
    fun saveTrip(trip: Trip) {
        val tripFolder = File(tripsDir, trip.metadata.fileName)
        tripFolder.mkdirs()

        // Save GPX file
        val gpxFile = File(tripFolder, "${trip.metadata.fileName}.gpx")
        gpxFile.writeText(trip.gpxContent)

        // Save metadata JSON
        val metadataFile = File(tripFolder, "${trip.metadata.fileName}_meta.json")
        val json = Json.encodeToString(trip.metadata)
        metadataFile.writeText(json)
    }

    /**
     * Load all trips from storage
     */
    fun loadAllTrips(): List<Trip> {
        val trips = mutableListOf<Trip>()

        tripsDir.listFiles()?.forEach { tripFolder ->
            if (tripFolder.isDirectory) {
                val metadataFile = File(tripFolder, "${tripFolder.name}_meta.json")
                val gpxFile = File(tripFolder, "${tripFolder.name}.gpx")

                if (metadataFile.exists() && gpxFile.exists()) {
                    try {
                        val metadata = Json.decodeFromString<TripMetadata>(metadataFile.readText())
                        val gpxContent = gpxFile.readText()
                        trips.add(Trip(metadata, gpxContent))
                    } catch (e: Exception) {
                        e.printStackTrace()
                    }
                }
            }
        }

        return trips.sortedByDescending { it.metadata.createdAt }
    }

    /**
     * Save a recorded trip separately from planned/imported trips.
     */
    fun saveRecordedTrip(trip: Trip) {
        val tripFolder = File(recordingsDir, trip.metadata.fileName)
        tripFolder.mkdirs()

        val gpxFile = File(tripFolder, "${trip.metadata.fileName}.gpx")
        gpxFile.writeText(trip.gpxContent)

        val metadataFile = File(tripFolder, "${trip.metadata.fileName}_meta.json")
        val json = Json.encodeToString(trip.metadata)
        metadataFile.writeText(json)
    }

    /**
     * Load all recorded trips from storage.
     */
    fun loadAllRecordedTrips(): List<Trip> {
        val trips = mutableListOf<Trip>()

        recordingsDir.listFiles()?.forEach { tripFolder ->
            if (tripFolder.isDirectory) {
                val metadataFile = File(tripFolder, "${tripFolder.name}_meta.json")
                val gpxFile = File(tripFolder, "${tripFolder.name}.gpx")

                if (metadataFile.exists() && gpxFile.exists()) {
                    try {
                        val metadata = Json.decodeFromString<TripMetadata>(metadataFile.readText())
                        val gpxContent = gpxFile.readText()
                        trips.add(Trip(metadata, gpxContent))
                    } catch (e: Exception) {
                        e.printStackTrace()
                    }
                }
            }
        }

        return trips.sortedByDescending { it.metadata.createdAt }
    }

    /**
     * Mark a recorded trip as copied into the main Trips list.
     */
    fun markRecordedTripAdded(fileName: String) {
        val tripFolder = File(recordingsDir, fileName)
        if (!tripFolder.exists()) return
        val marker = File(tripFolder, "added_to_trips.flag")
        marker.writeText("1")
    }

    /**
     * Check if a recorded trip was copied into the main Trips list.
     */
    fun isRecordedTripAdded(fileName: String): Boolean {
        val tripFolder = File(recordingsDir, fileName)
        if (!tripFolder.exists()) return false
        return File(tripFolder, "added_to_trips.flag").exists()
    }

    /**
     * Delete a trip
     */
    fun deleteTrip(fileName: String) {
        val tripFolder = File(tripsDir, fileName)
        tripFolder.deleteRecursively()
    }

    /**
     * Delete a recorded trip
     */
    fun deleteRecordedTrip(fileName: String) {
        val tripFolder = File(recordingsDir, fileName)
        tripFolder.deleteRecursively()
    }

    /**
     * Save a processed tile (1-bit bitmap)
     */
    fun saveTile(zoom: Int, tileX: Int, tileY: Int, tileData: ByteArray) {
        val zoomDir = File(mapDir, zoom.toString())
        val xDir = File(zoomDir, tileX.toString())
        xDir.mkdirs()

        val tileFile = File(xDir, "$tileY.bin")
        tileFile.writeBytes(tileData)
    }

    /**
     * Load a tile from storage
     */
    fun loadTile(zoom: Int, tileX: Int, tileY: Int): ByteArray? {
        val tileFile = File(mapDir, "$zoom/$tileX/$tileY.bin")
        return if (tileFile.exists()) {
            tileFile.readBytes()
        } else {
            null
        }
    }

    /**
     * Check if a tile exists
     */
    fun tileExists(zoom: Int, tileX: Int, tileY: Int): Boolean {
        val tileFile = File(mapDir, "$zoom/$tileX/$tileY.bin")
        return tileFile.exists()
    }

    /**
     * Get all existing tiles for a specific zoom level
     */
    fun getExistingTiles(zoom: Int): Set<Pair<Int, Int>> {
        val tiles = mutableSetOf<Pair<Int, Int>>()
        val zoomDir = File(mapDir, zoom.toString())

        if (zoomDir.exists()) {
            zoomDir.listFiles()?.forEach { xDir ->
                if (xDir.isDirectory) {
                    val tileX = xDir.name.toIntOrNull() ?: return@forEach
                    xDir.listFiles()?.forEach { tileFile ->
                        val tileY = tileFile.nameWithoutExtension.toIntOrNull()
                        if (tileY != null) {
                            tiles.add(Pair(tileX, tileY))
                        }
                    }
                }
            }
        }

        return tiles
    }

    /**
     * Get storage stats
     */
    fun getStorageStats(): StorageStats {
        val totalTripSize = tripsDir.walkTopDown().filter { it.isFile }.sumOf { it.length() }
        val processedMapSize = mapDir.walkTopDown().filter { it.isFile }.sumOf { it.length() }
        val rawMapSize = rawTilesDir.walkTopDown().filter { it.isFile }.sumOf { it.length() }
        val totalMapSize = processedMapSize + rawMapSize
        val tripCount = tripsDir.listFiles()?.count { it.isDirectory } ?: 0
        val processedTileCount = mapDir.walkTopDown().filter { it.isFile && it.extension == "bin" }.count()
        val rawTileCount = rawTilesDir.walkTopDown().filter { it.isFile && it.extension == "tile" }.count()
        val tileCount = processedTileCount + rawTileCount

        return StorageStats(
            totalTripSizeMB = totalTripSize / (1024.0 * 1024.0),
            totalMapSizeMB = totalMapSize / (1024.0 * 1024.0),
            tripCount = tripCount,
            tileCount = tileCount
        )
    }

    /**
     * Save raw tile PNG for testing/tuning
     */
    fun saveRawTile(zoom: Int, tileX: Int, tileY: Int, pngData: ByteArray) {
        val zoomDir = File(rawTilesDir, zoom.toString())
        val xDir = File(zoomDir, tileX.toString())
        xDir.mkdirs()

        val tileFile = File(xDir, "$tileY.tile")
        tileFile.writeBytes(pngData)
    }

    /**
     * Load raw tile PNG (for testing/tuning)
     */
    fun loadRawTile(zoom: Int, tileX: Int, tileY: Int): ByteArray? {
        val tileFile = File(rawTilesDir, "$zoom/$tileX/$tileY.tile")
        return if (tileFile.exists()) {
            tileFile.readBytes()
        } else {
            null
        }
    }

    /**
     * Check if raw tile exists
     */
    fun rawTileExists(zoom: Int, tileX: Int, tileY: Int): Boolean {
        val tileFile = File(rawTilesDir, "$zoom/$tileX/$tileY.tile")
        return tileFile.exists()
    }

    /**
     * Get all available raw tiles for a zoom level
     * Returns list of (zoom, x, y) triples
     */
    fun getAvailableTiles(zoom: Int): List<Triple<Int, Int, Int>> {
        val tiles = mutableListOf<Triple<Int, Int, Int>>()
        val zoomDir = File(rawTilesDir, zoom.toString())

        if (zoomDir.exists()) {
            zoomDir.listFiles()?.forEach { xDir ->
                if (xDir.isDirectory) {
                    val tileX = xDir.name.toIntOrNull() ?: return@forEach
                    xDir.listFiles()?.forEach { tileFile ->
                        if (tileFile.extension == "tile") {
                            val tileY = tileFile.nameWithoutExtension.toIntOrNull()
                            if (tileY != null) {
                                tiles.add(Triple(zoom, tileX, tileY))
                            }
                        }
                    }
                }
            }
        }

        return tiles.sortedWith(compareBy({ it.second }, { it.third }))
    }
}

data class StorageStats(
    val totalTripSizeMB: Double,
    val totalMapSizeMB: Double,
    val tripCount: Int,
    val tileCount: Int
)
