package com.example.kolomapa2.models

/**
 * Represents a geographic location point for route planning
 */
data class LocationPoint(
    val lat: Double,
    val lon: Double,
    val displayName: String,
    val isCurrentLocation: Boolean = false
)

/**
 * Forward geocoding suggestion from the geocoding service
 */
data class GeocodingSuggestion(
    val name: String,
    val location: String,
    val lat: Double,
    val lon: Double
)

/**
 * Tracks which location field (start or end) is being filled by device GPS request
 */
enum class LocationTarget {
    START,
    END,
    MAP_CENTER  // For centering the map without setting start/end
}
