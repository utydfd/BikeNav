package com.example.kolomapa2.utils

import android.util.Xml
import com.example.kolomapa2.models.BoundingBox
import com.example.kolomapa2.models.Trip
import com.example.kolomapa2.models.TripMetadata
import org.xmlpull.v1.XmlPullParser
import java.io.InputStream
import kotlin.math.*

class GpxParser {

    data class GpxPoint(
        val lat: Double,
        val lon: Double,
        val ele: Double?
    )

    data class GpxNames(
        val metadataName: String?,
        val trackName: String?
    )

    /**
     * Parse GPX file and create Trip object with metadata
     */
    fun parseGpx(inputStream: InputStream, fileName: String): Trip {
        val gpxContent = inputStream.bufferedReader().use { it.readText() }

        // Parse GPX to extract points
        val points = parseGpxPoints(gpxContent.byteInputStream())

        // Extract names from GPX
        val gpxNames = parseGpxNames(gpxContent.byteInputStream())

        // Use metadata name if available, otherwise track name, otherwise filename
        val displayName = gpxNames.metadataName
            ?: gpxNames.trackName
            ?: fileName.removeSuffix(".gpx")

        // Calculate metadata (pass track name as route description)
        val metadata = calculateMetadata(points, displayName, fileName, gpxNames.trackName)

        return Trip(metadata, gpxContent)
    }

    private fun parseGpxPoints(inputStream: InputStream): List<GpxPoint> {
        val points = mutableListOf<GpxPoint>()

        val parser = Xml.newPullParser()
        parser.setFeature(XmlPullParser.FEATURE_PROCESS_NAMESPACES, false)
        parser.setInput(inputStream, null)

        var eventType = parser.eventType
        var currentLat: Double? = null
        var currentLon: Double? = null
        var currentEle: Double? = null
        var inTrkpt = false

        while (eventType != XmlPullParser.END_DOCUMENT) {
            when (eventType) {
                XmlPullParser.START_TAG -> {
                    when (parser.name) {
                        "trkpt" -> {
                            inTrkpt = true
                            currentLat = parser.getAttributeValue(null, "lat")?.toDoubleOrNull()
                            currentLon = parser.getAttributeValue(null, "lon")?.toDoubleOrNull()
                            currentEle = null
                        }
                        "ele" -> {
                            if (inTrkpt && parser.next() == XmlPullParser.TEXT) {
                                currentEle = parser.text?.toDoubleOrNull()
                            }
                        }
                    }
                }
                XmlPullParser.END_TAG -> {
                    if (parser.name == "trkpt" && inTrkpt) {
                        if (currentLat != null && currentLon != null) {
                            points.add(GpxPoint(currentLat, currentLon, currentEle))
                        }
                        inTrkpt = false
                    }
                }
            }
            eventType = parser.next()
        }

        return points
    }

    private fun parseGpxNames(inputStream: InputStream): GpxNames {
        val parser = Xml.newPullParser()
        parser.setFeature(XmlPullParser.FEATURE_PROCESS_NAMESPACES, false)
        parser.setInput(inputStream, null)

        var eventType = parser.eventType
        var inMetadata = false
        var inTrack = false
        var inName = false
        var metadataName: String? = null
        var trackName: String? = null

        while (eventType != XmlPullParser.END_DOCUMENT) {
            when (eventType) {
                XmlPullParser.START_TAG -> {
                    when (parser.name) {
                        "metadata" -> inMetadata = true
                        "trk" -> inTrack = true
                        "name" -> inName = true
                    }
                }
                XmlPullParser.TEXT -> {
                    if (inName) {
                        val text = parser.text
                        when {
                            inMetadata && metadataName == null -> metadataName = text
                            inTrack && !inMetadata && trackName == null -> trackName = text
                        }
                    }
                }
                XmlPullParser.END_TAG -> {
                    when (parser.name) {
                        "metadata" -> inMetadata = false
                        "trk" -> inTrack = false
                        "name" -> inName = false
                    }
                }
            }
            eventType = parser.next()
        }

        return GpxNames(metadataName, trackName)
    }

    /**
     * Parse route description to extract origin and destination
     * Handles formats like "Route from Prague to Berlin" or "From Prague to Berlin"
     */
    private fun parseRouteInfo(routeDescription: String?): Pair<String?, String?> {
        if (routeDescription == null) return null to null

        // Pattern matches: "from X to Y" or "From X to Y"
        val pattern = Regex("(?:route )?from (.+?) to (.+)", RegexOption.IGNORE_CASE)
        val match = pattern.find(routeDescription)

        return if (match != null) {
            val from = match.groupValues[1].trim()
            val to = match.groupValues[2].trim()
            from to to
        } else {
            null to null
        }
    }

    private fun calculateMetadata(points: List<GpxPoint>, name: String, fileName: String, routeDescription: String?): TripMetadata {
        val (routeFrom, routeTo) = parseRouteInfo(routeDescription)

        if (points.isEmpty()) {
            return TripMetadata(
                name = name,
                fileName = sanitizeFileName(fileName),
                totalDistance = 0.0,
                totalElevationGain = 0.0,
                totalElevationLoss = 0.0,
                minElevation = 0.0,
                maxElevation = 0.0,
                pointCount = 0,
                boundingBox = BoundingBox(0.0, 0.0, 0.0, 0.0),
                routeDescription = routeDescription,
                routeFrom = routeFrom,
                routeTo = routeTo
            )
        }

        // Calculate bounding box
        val lats = points.map { it.lat }
        val lons = points.map { it.lon }
        val boundingBox = BoundingBox(
            minLat = lats.min(),
            maxLat = lats.max(),
            minLon = lons.min(),
            maxLon = lons.max()
        )

        // Calculate elevation stats
        val elevations = points.mapNotNull { it.ele }
        val minElevation = elevations.minOrNull() ?: 0.0
        val maxElevation = elevations.maxOrNull() ?: 0.0

        // Calculate total distance and elevation gain/loss
        var totalDistance = 0.0
        var totalElevationGain = 0.0
        var totalElevationLoss = 0.0

        for (i in 1 until points.size) {
            val prev = points[i - 1]
            val curr = points[i]

            // Calculate distance between consecutive points
            totalDistance += haversineDistance(
                prev.lat, prev.lon,
                curr.lat, curr.lon
            )

            // Calculate elevation changes
            if (prev.ele != null && curr.ele != null) {
                val elevationDiff = curr.ele - prev.ele
                if (elevationDiff > 0) {
                    totalElevationGain += elevationDiff
                } else {
                    totalElevationLoss += abs(elevationDiff)
                }
            }
        }

        return TripMetadata(
            name = name,
            fileName = sanitizeFileName(fileName),
            totalDistance = totalDistance,
            totalElevationGain = totalElevationGain,
            totalElevationLoss = totalElevationLoss,
            minElevation = minElevation,
            maxElevation = maxElevation,
            pointCount = points.size,
            boundingBox = boundingBox,
            routeDescription = routeDescription,
            routeFrom = routeFrom,
            routeTo = routeTo
        )
    }

    /**
     * Calculate distance between two coordinates using Haversine formula
     * Returns distance in meters
     */
    private fun haversineDistance(lat1: Double, lon1: Double, lat2: Double, lon2: Double): Double {
        val earthRadius = 6371000.0 // meters

        val dLat = Math.toRadians(lat2 - lat1)
        val dLon = Math.toRadians(lon2 - lon1)

        val a = sin(dLat / 2).pow(2) +
                cos(Math.toRadians(lat1)) * cos(Math.toRadians(lat2)) *
                sin(dLon / 2).pow(2)

        val c = 2 * atan2(sqrt(a), sqrt(1 - a))

        return earthRadius * c
    }

    private fun sanitizeFileName(fileName: String): String {
        return fileName
            .removeSuffix(".gpx")
            .replace(Regex("[^a-zA-Z0-9_-]"), "_")
    }
}
