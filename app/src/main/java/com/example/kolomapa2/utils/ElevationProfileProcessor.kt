package com.example.kolomapa2.utils

import android.util.Xml
import com.example.kolomapa2.models.ElevationProfile
import com.example.kolomapa2.models.ElevationProfilePoint
import org.xmlpull.v1.XmlPullParser
import kotlin.math.*

/**
 * Utility for processing GPX content into elevation profile data
 */
object ElevationProfileProcessor {

    private data class GpxPoint(
        val lat: Double,
        val lon: Double,
        val ele: Double?
    )

    /**
     * Process GPX content and extract elevation profile
     * @param gpxContent The raw GPX XML content
     * @return ElevationProfile if elevation data exists, null otherwise
     */
    fun processGpxContent(gpxContent: String): ElevationProfile? {
        val points = parseGpxPoints(gpxContent)
        if (points.isEmpty()) return null

        // Check if we have any elevation data
        val pointsWithElevation = points.filter { it.ele != null }
        if (pointsWithElevation.isEmpty()) return null

        // Calculate cumulative distances and build profile points
        val profilePoints = mutableListOf<ElevationProfilePoint>()
        var cumulativeDistance = 0.0

        for ((index, point) in points.withIndex()) {
            if (index > 0) {
                val prev = points[index - 1]
                cumulativeDistance += haversineDistance(
                    prev.lat, prev.lon,
                    point.lat, point.lon
                )
            }

            // Use elevation or interpolate/skip if null
            val elevation = point.ele
            if (elevation != null) {
                profilePoints.add(
                    ElevationProfilePoint(
                        distanceFromStart = cumulativeDistance,
                        elevation = elevation,
                        lat = point.lat,
                        lon = point.lon,
                        index = index
                    )
                )
            }
        }

        if (profilePoints.isEmpty()) return null

        val minElevation = profilePoints.minOf { it.elevation }
        val maxElevation = profilePoints.maxOf { it.elevation }
        val elevationRange = (maxElevation - minElevation).coerceAtLeast(10.0)

        return ElevationProfile(
            points = profilePoints,
            totalDistance = cumulativeDistance,
            minElevation = minElevation,
            maxElevation = maxElevation,
            elevationRange = elevationRange
        )
    }

    /**
     * Find the closest profile point to a given distance
     */
    fun findPointAtDistance(profile: ElevationProfile, distance: Double): ElevationProfilePoint? {
        if (profile.points.isEmpty()) return null
        return profile.points.minByOrNull { abs(it.distanceFromStart - distance) }
    }

    /**
     * Parse GPX content to extract track points
     */
    private fun parseGpxPoints(gpxContent: String): List<GpxPoint> {
        val points = mutableListOf<GpxPoint>()

        try {
            val parser = Xml.newPullParser()
            parser.setFeature(XmlPullParser.FEATURE_PROCESS_NAMESPACES, false)
            parser.setInput(gpxContent.byteInputStream(), null)

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
        } catch (e: Exception) {
            // Return empty list on parse error
        }

        return points
    }

    /**
     * Calculate distance between two coordinates using Haversine formula
     * @return Distance in meters
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
}
