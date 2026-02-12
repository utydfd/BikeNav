package com.example.kolomapa2.utils

import android.util.Log
import com.example.kolomapa2.models.GeocodingSuggestion
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.jsonArray
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive
import kotlinx.serialization.json.double
import okhttp3.OkHttpClient
import okhttp3.Request
import java.io.IOException
import java.net.URLEncoder
import java.util.concurrent.TimeUnit

/**
 * Service for geocoding (forward and reverse) using OpenStreetMap Nominatim
 */
class GeocodingService {

    private val client = OkHttpClient.Builder()
        .connectTimeout(10, TimeUnit.SECONDS)
        .readTimeout(10, TimeUnit.SECONDS)
        .build()

    private val json = Json {
        ignoreUnknownKeys = true
        isLenient = true
    }

    companion object {
        private const val TAG = "GeocodingService"
        private const val SEARCH_URL = "https://nominatim.openstreetmap.org/search"
        private const val REVERSE_URL = "https://nominatim.openstreetmap.org/reverse"
        private const val LIMIT = 5
    }

    /**
     * Forward geocoding - search for locations by text query
     * @param query Search text
     * @return List of location suggestions
     */
    suspend fun searchLocations(query: String): Result<List<GeocodingSuggestion>> = withContext(Dispatchers.IO) {
        try {
            if (query.isBlank()) return@withContext Result.success(emptyList())

            val url = buildString {
                append(SEARCH_URL)
                append("?format=jsonv2")
                append("&q=${URLEncoder.encode(query, "UTF-8")}")
                append("&limit=$LIMIT")
                append("&addressdetails=1")
            }

            Log.d(TAG, "Searching locations for query: $query")

            val request = Request.Builder()
                .url(url)
                .header("User-Agent", "KoloMapa2/1.0 BikeNav App")
                .header("Accept", "application/json")
                .build()

            client.newCall(request).execute().use { response ->
                if (!response.isSuccessful) {
                    val errorBody = response.body?.string()
                    Log.e(TAG, "API error: ${response.code}")
                    return@withContext Result.failure(ApiHttpException("Geocoding service", response.code, errorBody))
                }

                val responseBody = response.body?.string()
                    ?: return@withContext Result.failure(IOException("Empty response body"))
                val items = json.parseToJsonElement(responseBody).jsonArray

                val suggestions = items.mapNotNull { item ->
                    try {
                        val itemObj = item.jsonObject
                        val displayName = itemObj["display_name"]?.jsonPrimitive?.content ?: return@mapNotNull null
                        val lat = itemObj["lat"]?.jsonPrimitive?.content?.toDoubleOrNull() ?: return@mapNotNull null
                        val lon = itemObj["lon"]?.jsonPrimitive?.content?.toDoubleOrNull() ?: return@mapNotNull null
                        val parts = displayName.split(",").map { it.trim() }
                        val name = parts.firstOrNull().orEmpty().ifBlank { "Unknown Location" }
                        val location = parts.drop(1).joinToString(", ")

                        GeocodingSuggestion(name, location, lat, lon)
                    } catch (e: Exception) {
                        Log.e(TAG, "Error parsing suggestion item", e)
                        null
                    }
                }

                Log.d(TAG, "Found ${suggestions.size} suggestions for '$query'")
                Result.success(suggestions)
            }
        } catch (e: Exception) {
            Log.e(TAG, "Error searching locations", e)
            Result.failure(e)
        }
    }

    /**
     * Reverse geocoding - get location name from coordinates
     * @param lat Latitude
     * @param lon Longitude
     * @return Location name string
     */
    suspend fun reverseGeocode(lat: Double, lon: Double): Result<String> = withContext(Dispatchers.IO) {
        try {
            val url = buildString {
                append(REVERSE_URL)
                append("?format=jsonv2")
                append("&lat=$lat")
                append("&lon=$lon")
                append("&zoom=18")
            }

            Log.d(TAG, "Reverse geocoding: lat=$lat, lon=$lon")

            val request = Request.Builder()
                .url(url)
                .header("User-Agent", "KoloMapa2/1.0 BikeNav App")
                .header("Accept", "application/json")
                .build()

            client.newCall(request).execute().use { response ->
                if (!response.isSuccessful) {
                    val errorBody = response.body?.string()
                    Log.e(TAG, "API error: ${response.code}")
                    return@withContext Result.failure(ApiHttpException("Geocoding service", response.code, errorBody))
                }

                val responseBody = response.body?.string()
                    ?: return@withContext Result.failure(IOException("Empty response body"))
                val jsonObj = json.parseToJsonElement(responseBody).jsonObject

                val displayName = jsonObj["display_name"]?.jsonPrimitive?.content ?: "Unknown Location"

                Log.d(TAG, "Reverse geocoded to: $displayName")
                Result.success(displayName)
            }

        } catch (e: Exception) {
            Log.e(TAG, "Error reverse geocoding", e)
            Result.failure(e)
        }
    }
}
