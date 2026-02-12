package com.example.kolomapa2.models

import kotlinx.serialization.Serializable

/**
 * Complete weather data including current conditions and hourly forecast
 */
@Serializable
data class WeatherData(
    val location: String,
    val current: CurrentWeather,
    val hourly: List<HourlyForecast>
)

/**
 * Current weather conditions
 */
@Serializable
data class CurrentWeather(
    val temperature: Float,          // Celsius
    val feelsLike: Float,             // Celsius
    val condition: WeatherCondition,
    val humidity: Int,                // Percentage 0-100
    val windSpeed: Float,             // km/h
    val windDirection: Int,           // Degrees 0-360
    val pressure: Int,                // hPa
    val precipitationChance: Int,     // Percentage 0-100
    val timestamp: Long,              // Unix timestamp
    val sunrise: Long,                // Sunrise Unix timestamp
    val sunset: Long                  // Sunset Unix timestamp
)

/**
 * Hourly forecast data
 */
@Serializable
data class HourlyForecast(
    val hour: Int,                    // Hour of day 0-23
    val temperature: Float,           // Celsius
    val condition: WeatherCondition,
    val precipitationChance: Int      // Percentage 0-100
)

/**
 * Weather conditions based on WMO Weather interpretation codes
 * https://open-meteo.com/en/docs
 */
@Serializable
enum class WeatherCondition(val code: Int, val description: String) {
    CLEAR(0, "Clear"),
    MAINLY_CLEAR(1, "Mainly Clear"),
    PARTLY_CLOUDY(2, "Partly Cloudy"),
    CLOUDY(3, "Cloudy"),
    FOG(45, "Fog"),
    RIME_FOG(48, "Rime Fog"),
    DRIZZLE_LIGHT(51, "Light Drizzle"),
    DRIZZLE_MODERATE(53, "Drizzle"),
    DRIZZLE_DENSE(55, "Heavy Drizzle"),
    FREEZING_DRIZZLE_LIGHT(56, "Light Freezing Drizzle"),
    FREEZING_DRIZZLE_DENSE(57, "Freezing Drizzle"),
    RAIN_LIGHT(61, "Light Rain"),
    RAIN_MODERATE(63, "Rain"),
    RAIN_HEAVY(65, "Heavy Rain"),
    FREEZING_RAIN_LIGHT(66, "Light Freezing Rain"),
    FREEZING_RAIN_HEAVY(67, "Freezing Rain"),
    SNOW_LIGHT(71, "Light Snow"),
    SNOW_MODERATE(73, "Snow"),
    SNOW_HEAVY(75, "Heavy Snow"),
    SNOW_GRAINS(77, "Snow Grains"),
    RAIN_SHOWERS_LIGHT(80, "Light Showers"),
    RAIN_SHOWERS_MODERATE(81, "Showers"),
    RAIN_SHOWERS_VIOLENT(82, "Heavy Showers"),
    SNOW_SHOWERS_LIGHT(85, "Light Snow Showers"),
    SNOW_SHOWERS_HEAVY(86, "Snow Showers"),
    THUNDERSTORM(95, "Thunderstorm"),
    THUNDERSTORM_LIGHT_HAIL(96, "Thunderstorm with Hail"),
    THUNDERSTORM_HEAVY_HAIL(99, "Thunderstorm with Heavy Hail");

    companion object {
        fun fromCode(code: Int): WeatherCondition {
            return values().find { it.code == code } ?: CLEAR
        }

        /**
         * Get simplified condition for icon display
         */
        fun getSimplifiedCondition(condition: WeatherCondition): String {
            return when (condition) {
                CLEAR, MAINLY_CLEAR -> "clear"
                PARTLY_CLOUDY -> "partly_cloudy"
                CLOUDY -> "cloudy"
                FOG, RIME_FOG -> "fog"
                DRIZZLE_LIGHT, DRIZZLE_MODERATE, DRIZZLE_DENSE,
                RAIN_LIGHT, RAIN_MODERATE, RAIN_HEAVY,
                RAIN_SHOWERS_LIGHT, RAIN_SHOWERS_MODERATE, RAIN_SHOWERS_VIOLENT -> "rain"
                FREEZING_DRIZZLE_LIGHT, FREEZING_DRIZZLE_DENSE,
                FREEZING_RAIN_LIGHT, FREEZING_RAIN_HEAVY -> "freezing_rain"
                SNOW_LIGHT, SNOW_MODERATE, SNOW_HEAVY, SNOW_GRAINS,
                SNOW_SHOWERS_LIGHT, SNOW_SHOWERS_HEAVY -> "snow"
                THUNDERSTORM, THUNDERSTORM_LIGHT_HAIL, THUNDERSTORM_HEAVY_HAIL -> "thunderstorm"
            }
        }
    }
}
