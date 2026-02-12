package com.example.kolomapa2.models

import kotlinx.serialization.Serializable

@Serializable
data class TripMetadata(
    val name: String,
    val fileName: String,
    val totalDistance: Double, // in meters
    val totalElevationGain: Double, // in meters
    val totalElevationLoss: Double, // in meters
    val minElevation: Double,
    val maxElevation: Double,
    val pointCount: Int,
    val boundingBox: BoundingBox,
    val routeDescription: String? = null, // Full route description from GPX (e.g. "Route from Prague to U Brusnice")
    val routeFrom: String? = null, // Origin extracted from route description
    val routeTo: String? = null, // Destination extracted from route description
    val createdAt: Long = System.currentTimeMillis()
)

@Serializable
data class BoundingBox(
    val minLat: Double,
    val maxLat: Double,
    val minLon: Double,
    val maxLon: Double
)
