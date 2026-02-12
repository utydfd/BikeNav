package com.example.kolomapa2.models

import kotlinx.serialization.Serializable

data class RecordingInfo(
    val name: String,
    val dirName: String
)

@Serializable
data class RecordingMetadata(
    val name: String? = null,
    val createdAt: Long? = null,
    val durationSec: Long? = null,
    val totalDistance: Double? = null,
    val totalElevationGain: Double? = null,
    val totalElevationLoss: Double? = null,
    val minElevation: Double? = null,
    val maxElevation: Double? = null,
    val pointCount: Int? = null
)
