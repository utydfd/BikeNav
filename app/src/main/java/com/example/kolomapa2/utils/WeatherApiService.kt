package com.example.kolomapa2.utils

import android.util.Log
import com.example.kolomapa2.models.CurrentWeather
import com.example.kolomapa2.models.HourlyForecast
import com.example.kolomapa2.models.WeatherCondition
import com.example.kolomapa2.models.WeatherData
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.double
import kotlinx.serialization.json.int
import kotlinx.serialization.json.jsonArray
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive
import kotlinx.serialization.json.long
import okhttp3.OkHttpClient
import okhttp3.Request
import java.time.Instant
import java.time.ZoneOffset
import java.util.concurrent.TimeUnit

/**
 * Service for fetching weather data from Open-Meteo API
 * and location names from Nominatim (OpenStreetMap) API
 */
class WeatherApiService {

    private val client = OkHttpClient.Builder()
        .connectTimeout(15, TimeUnit.SECONDS)
        .readTimeout(15, TimeUnit.SECONDS)
        .build()

    private val json = Json {
        ignoreUnknownKeys = true
        isLenient = true
    }

    companion object {
        private const val TAG = "WeatherApiService"
        private const val OPEN_METEO_BASE_URL = "https://api.open-meteo.com/v1/forecast"
        private const val NOMINATIM_BASE_URL = "https://nominatim.openstreetmap.org/reverse"
    }

    /**
     * Fetch complete weather data for given coordinates
     * @param latitude GPS latitude
     * @param longitude GPS longitude
     * @return WeatherData including current conditions and 6-hour forecast
     */
    suspend fun fetchWeather(latitude: Double, longitude: Double): Result<WeatherData> = withContext(Dispatchers.IO) {
        try {
            Log.d(TAG, "Fetching weather for lat=$latitude, lon=$longitude")

            // Fetch location name
            val locationName = fetchLocationName(latitude, longitude).getOrElse {
                Log.w(TAG, "Failed to fetch location name: ${it.message}, using coordinates")
                "${String.format("%.4f", latitude)}, ${String.format("%.4f", longitude)}"
            }

            // Build Open-Meteo API URL
            val weatherUrl = buildString {
                append(OPEN_METEO_BASE_URL)
                append("?latitude=$latitude")
                append("&longitude=$longitude")
                append("&current=temperature_2m,apparent_temperature,relative_humidity_2m,")
                append("precipitation_probability,weather_code,surface_pressure,wind_speed_10m,wind_direction_10m")
                append("&hourly=temperature_2m,weather_code,precipitation_probability")
                append("&daily=sunrise,sunset")
                append("&timezone=auto")
                append("&timeformat=unixtime") // Get timestamps as Unix epoch
                append("&forecast_days=1") // Get today's forecast
            }

            Log.d(TAG, "Fetching from Open-Meteo: $weatherUrl")

            val request = Request.Builder()
                .url(weatherUrl)
                .header("User-Agent", "KoloMapa2/1.0 BikeNav Weather App")
                .build()

            val responseBody = client.newCall(request).execute().use { response ->
                if (!response.isSuccessful) {
                    val errorBody = response.body?.string()
                    throw ApiHttpException("Open-Meteo", response.code, errorBody)
                }
                response.body?.string() ?: throw Exception("Empty response body")
            }
            Log.d(TAG, "Open-Meteo response received: ${responseBody.take(200)}...")

            val weatherData = parseWeatherResponse(responseBody, locationName)

            Log.d(TAG, "Weather data parsed successfully for $locationName")
            Result.success(weatherData)

        } catch (e: Exception) {
            Log.e(TAG, "Error fetching weather", e)
            Result.failure(e)
        }
    }

    /**
     * Fetch location name using Nominatim reverse geocoding
     */
    private suspend fun fetchLocationName(latitude: Double, longitude: Double): Result<String> = withContext(Dispatchers.IO) {
        try {
            val url = "$NOMINATIM_BASE_URL?lat=$latitude&lon=$longitude&format=json"

            val request = Request.Builder()
                .url(url)
                .header("User-Agent", "KoloMapa2/1.0 BikeNav Weather App")
                .build()

            val responseBody = client.newCall(request).execute().use { response ->
                if (!response.isSuccessful) {
                    val errorBody = response.body?.string()
                    throw ApiHttpException("Nominatim", response.code, errorBody)
                }
                response.body?.string() ?: throw Exception("Empty response body")
            }
            val jsonObj = json.parseToJsonElement(responseBody).jsonObject

            // Try to get city/town/village name, fallback to display_name
            val address = jsonObj["address"]?.jsonObject
            val locationName = address?.let {
                it["city"]?.jsonPrimitive?.content
                    ?: it["town"]?.jsonPrimitive?.content
                    ?: it["village"]?.jsonPrimitive?.content
                    ?: it["municipality"]?.jsonPrimitive?.content
                    ?: jsonObj["display_name"]?.jsonPrimitive?.content?.split(",")?.get(0)
            } ?: "Unknown Location"

            Result.success(locationName)

        } catch (e: Exception) {
            Log.e(TAG, "Error fetching location name", e)
            Result.failure(e)
        }
    }

    /**
     * Parse Open-Meteo JSON response into WeatherData
     */
    private fun parseWeatherResponse(responseBody: String, locationName: String): WeatherData {
        val jsonObj = json.parseToJsonElement(responseBody).jsonObject

        // Parse daily data for sunrise/sunset
        val daily = jsonObj["daily"]?.jsonObject ?: throw Exception("Missing daily weather data")
        val sunriseArray = daily["sunrise"]?.jsonArray ?: throw Exception("Missing sunrise data")
        val sunsetArray = daily["sunset"]?.jsonArray ?: throw Exception("Missing sunset data")

        val sunrise = sunriseArray[0].jsonPrimitive.long
        val sunset = sunsetArray[0].jsonPrimitive.long

        // Parse current weather
        val current = jsonObj["current"]?.jsonObject ?: throw Exception("Missing current weather data")
        val currentWeather = CurrentWeather(
            temperature = current["temperature_2m"]?.jsonPrimitive?.double?.toFloat() ?: 0f,
            feelsLike = current["apparent_temperature"]?.jsonPrimitive?.double?.toFloat() ?: 0f,
            condition = WeatherCondition.fromCode(current["weather_code"]?.jsonPrimitive?.int ?: 0),
            humidity = current["relative_humidity_2m"]?.jsonPrimitive?.int ?: 0,
            windSpeed = current["wind_speed_10m"]?.jsonPrimitive?.double?.toFloat() ?: 0f,
            windDirection = current["wind_direction_10m"]?.jsonPrimitive?.double?.toInt() ?: 0,
            pressure = current["surface_pressure"]?.jsonPrimitive?.double?.toInt() ?: 1013,
            precipitationChance = current["precipitation_probability"]?.jsonPrimitive?.int ?: 0,
            timestamp = current["time"]?.jsonPrimitive?.long ?: System.currentTimeMillis() / 1000,
            sunrise = sunrise,
            sunset = sunset
        )

        // Parse hourly forecast (next 6 hours)
        val hourly = jsonObj["hourly"]?.jsonObject ?: throw Exception("Missing hourly weather data")
        val times = hourly["time"]?.jsonArray ?: throw Exception("Missing hourly times")
        val temps = hourly["temperature_2m"]?.jsonArray ?: throw Exception("Missing hourly temperatures")
        val codes = hourly["weather_code"]?.jsonArray ?: throw Exception("Missing hourly weather codes")
        val precips = hourly["precipitation_probability"]?.jsonArray

        val hourlyForecasts = mutableListOf<HourlyForecast>()

        // Get UTC offset from response
        val utcOffsetSeconds = jsonObj["utc_offset_seconds"]?.jsonPrimitive?.int ?: 0
        val zoneOffset = ZoneOffset.ofTotalSeconds(utcOffsetSeconds)

        // Find the current hour in the API response
        val currentTime = System.currentTimeMillis() / 1000
        var startIndex = 0

        // Find the index that corresponds to the next full hour after current time
        for (i in times.indices) {
            val timestamp = times[i].jsonPrimitive.long
            if (timestamp > currentTime) {
                startIndex = i
                break
            }
        }

        // Get next 6 hours starting from the next hour
        val maxHours = minOf(6, times.size - startIndex)
        for (i in 0 until maxHours) {
            val index = startIndex + i
            val timestamp = times[index].jsonPrimitive.long

            // Convert Unix timestamp to hour using proper date/time API
            val instant = Instant.ofEpochSecond(timestamp)
            val hour = instant.atOffset(zoneOffset).hour

            hourlyForecasts.add(
                HourlyForecast(
                    hour = hour,
                    temperature = temps[index].jsonPrimitive.double.toFloat(),
                    condition = WeatherCondition.fromCode(codes[index].jsonPrimitive.int),
                    precipitationChance = precips?.get(index)?.jsonPrimitive?.int ?: 0
                )
            )
        }

        return WeatherData(
            location = locationName,
            current = currentWeather,
            hourly = hourlyForecasts
        )
    }
}
