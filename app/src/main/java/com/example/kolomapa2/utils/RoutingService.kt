package com.example.kolomapa2.utils

import android.util.Log
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.double
import kotlinx.serialization.json.int
import kotlinx.serialization.json.jsonArray
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive
import okhttp3.OkHttpClient
import okhttp3.Request
import java.net.URLEncoder
import java.util.concurrent.TimeUnit

/**
 * Service for fetching bicycle routes from open routing providers
 * Converts route responses to GPX format for bike navigation
 */
class RoutingService {

    private val client = OkHttpClient.Builder()
        .connectTimeout(15, TimeUnit.SECONDS)
        .readTimeout(15, TimeUnit.SECONDS)
        .build()

    private val json = Json {
        ignoreUnknownKeys = true
        isLenient = true
    }

    companion object {
        private const val TAG = "RoutingService"
        private const val OSRM_BASE_URL = "https://router.project-osrm.org/route/v1/bicycle"
    }

    data class RouteResult(
        val gpxContent: String,
        val elevationTotal: Int,
        val elevationMissing: Int,
        val elevationHadError: Boolean
    )

    /**
     * Fetch route from start to end coordinates using bicycle routing
     * @param startLat Starting latitude
     * @param startLon Starting longitude
     * @param endLat Ending latitude
     * @param endLon Ending longitude
     * @param routeName Metadata name for the route
     * @param routeDescription Track name used as the route description
     * @return RouteResult containing GPX content and elevation status
     */
    suspend fun getRoute(
        startLat: Double,
        startLon: Double,
        endLat: Double,
        endLon: Double,
        routeName: String = "Navigate Home",
        routeDescription: String = routeName
    ): Result<RouteResult> = withContext(Dispatchers.IO) {
        try {
            Log.d(TAG, "Fetching route from ($startLat, $startLon) to ($endLat, $endLon)")

            // OSRM expects coordinates in lon,lat order
            val routeUrl = buildString {
                append(OSRM_BASE_URL)
                append("/")
                append(startLon)
                append(",")
                append(startLat)
                append(";")
                append(endLon)
                append(",")
                append(endLat)
                append("?overview=full&geometries=geojson&steps=false")
            }

            Log.d(TAG, "Requesting route from routing service: $routeUrl")

            val request = Request.Builder()
                .url(routeUrl)
                .header("User-Agent", "KoloMapa2/1.0 BikeNav Routing App")
                .build()

            val responseBody = client.newCall(request).execute().use { response ->
                if (!response.isSuccessful) {
                    val errorBody = response.body?.string()
                    throw ApiHttpException("Routing service", response.code, errorBody)
                }
                response.body?.string() ?: throw Exception("Empty response body")
            }
            Log.d(TAG, "Routing response received (${responseBody.length} bytes)")

            val coordinates = extractCoordinatesFromRouteResponse(responseBody)
            Log.d(TAG, "Extracted ${coordinates.size} coordinates from route")

            val gpxContent = convertRouteToGpx(responseBody, routeName, routeDescription)

            Log.d(TAG, "Route converted to GPX successfully")
            Result.success(
                RouteResult(
                    gpxContent = gpxContent,
                    elevationTotal = 0,
                    elevationMissing = 0,
                    elevationHadError = false
                )
            )

        } catch (e: Exception) {
            Log.e(TAG, "Error fetching route", e)
            Result.failure(e)
        }
    }

    /**
     * Extract coordinates from OSRM route response
     * @param routeResponse The JSON response from the routing service
     * @return List of [lon, lat] pairs
     */
    private fun extractCoordinatesFromRouteResponse(routeResponse: String): List<Pair<Double, Double>> {
        val jsonObj = json.parseToJsonElement(routeResponse).jsonObject
        val routes = jsonObj["routes"]?.jsonArray ?: throw Exception("Missing routes in response")
        if (routes.isEmpty()) throw Exception("No routes returned")

        val firstRoute = routes[0].jsonObject
        val geometry = firstRoute["geometry"]?.jsonObject ?: throw Exception("Missing route geometry")
        val coordinates = geometry["coordinates"]?.jsonArray ?: throw Exception("Missing route coordinates")

        return coordinates.mapNotNull { point ->
            val coordArray = point.jsonArray
            if (coordArray.size >= 2) {
                val lon = coordArray[0].jsonPrimitive.double
                val lat = coordArray[1].jsonPrimitive.double
                Pair(lon, lat)
            } else {
                null
            }
        }
    }

    /**
     * Convert route response to GPX format
     * @param routeResponse The JSON response from the routing service
     * @return GPX file content as String
     */
    private fun convertRouteToGpx(
        routeResponse: String,
        routeName: String,
        routeDescription: String
    ): String {
        val jsonObj = json.parseToJsonElement(routeResponse).jsonObject
        val routes = jsonObj["routes"]?.jsonArray ?: throw Exception("Missing routes in response")
        if (routes.isEmpty()) throw Exception("No routes returned")

        val firstRoute = routes[0].jsonObject
        val geometry = firstRoute["geometry"]?.jsonObject ?: throw Exception("Missing route geometry")
        val coordinates = geometry["coordinates"]?.jsonArray ?: throw Exception("Missing route coordinates")
        val distance = firstRoute["distance"]?.jsonPrimitive?.double?.toInt() ?: 0
        val duration = firstRoute["duration"]?.jsonPrimitive?.double?.toInt() ?: 0

        // Build GPX XML
        val gpxBuilder = StringBuilder()
        gpxBuilder.append("""<?xml version="1.0" encoding="UTF-8"?>""").append("\n")
        gpxBuilder.append("""<gpx version="1.1" creator="KoloMapa2 - OSM Routing" xmlns="http://www.topografix.com/GPX/1/1">""").append("\n")
        val safeRouteName = escapeXml(routeName.ifBlank { "Route" })
        val safeRouteDescription = escapeXml(routeDescription.ifBlank { safeRouteName })

        gpxBuilder.append("  <metadata>\n")
        gpxBuilder.append("    <name>$safeRouteName</name>\n")
        gpxBuilder.append("    <desc>$safeRouteDescription - Distance: ${distance}m, Duration: ${duration}s</desc>\n")
        gpxBuilder.append("  </metadata>\n")
        gpxBuilder.append("  <trk>\n")
        gpxBuilder.append("    <name>$safeRouteDescription</name>\n")
        gpxBuilder.append("    <type>cycling</type>\n")
        gpxBuilder.append("    <trkseg>\n")

        // Convert GeoJSON LineString coordinates to GPX trackpoints.
        // Format is [longitude, latitude].
        var validPoints = 0
        var invalidPoints = 0

        coordinates.forEachIndexed { index, coord ->
            val coordArray = coord.jsonArray
            if (coordArray.size >= 2) {
                val lon = coordArray[0].jsonPrimitive.double
                val lat = coordArray[1].jsonPrimitive.double

                // Validate coordinates are within valid ranges
                // Latitude: -90 to 90, Longitude: -180 to 180
                if (lat >= -90 && lat <= 90 && lon >= -180 && lon <= 180) {
                    gpxBuilder.append("      <trkpt lat=\"$lat\" lon=\"$lon\"></trkpt>\n")
                    validPoints++
                } else {
                    Log.w(TAG, "Skipping invalid coordinate: lat=$lat, lon=$lon")
                    invalidPoints++
                }
            }
        }

        if (invalidPoints > 0) {
            Log.w(TAG, "Skipped $invalidPoints invalid coordinates out of ${validPoints + invalidPoints} total")
        }
        Log.d(TAG, "Converted $validPoints valid coordinates to GPX")

        gpxBuilder.append("    </trkseg>\n")
        gpxBuilder.append("  </trk>\n")
        gpxBuilder.append("</gpx>\n")

        return gpxBuilder.toString()
    }

    private fun escapeXml(input: String): String {
        return input
            .replace("&", "&amp;")
            .replace("<", "&lt;")
            .replace(">", "&gt;")
            .replace("\"", "&quot;")
            .replace("'", "&apos;")
    }
}
