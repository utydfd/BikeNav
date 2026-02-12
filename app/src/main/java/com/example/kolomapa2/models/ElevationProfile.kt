package com.example.kolomapa2.models

/**
 * Represents a single point on the elevation profile chart
 */
data class ElevationProfilePoint(
    val distanceFromStart: Double,  // Distance from start in meters
    val elevation: Double,          // Elevation in meters
    val lat: Double,                // Latitude for map highlighting
    val lon: Double,                // Longitude for map highlighting
    val index: Int                  // Index in original point list
)

/**
 * Represents the complete elevation profile for a trip
 */
data class ElevationProfile(
    val points: List<ElevationProfilePoint>,
    val totalDistance: Double,      // Total distance in meters
    val minElevation: Double,       // Minimum elevation in meters
    val maxElevation: Double,       // Maximum elevation in meters
    val elevationRange: Double      // maxElevation - minElevation
)
