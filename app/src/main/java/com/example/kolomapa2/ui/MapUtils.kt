package com.example.kolomapa2.ui

import kotlin.math.*

/**
 * Shared utilities for map rendering and tile management
 */

/**
 * Tile key for map tile identification
 */
data class TileKey(val zoom: Int, val x: Int, val y: Int)

/**
 * Web Mercator projection functions
 */
fun getTileX(lon: Double, zoom: Int): Int {
    val n = 2.0.pow(zoom)
    return ((lon + 180.0) / 360.0 * n).toInt()
}

fun getTileY(lat: Double, zoom: Int): Int {
    val n = 2.0.pow(zoom)
    val latRad = lat * Math.PI / 180.0
    return ((1.0 - ln(tan(latRad) + 1.0 / cos(latRad)) / Math.PI) / 2.0 * n).toInt()
}

fun getTilePixelX(lon: Double, zoom: Int): Double {
    val n = 2.0.pow(zoom)
    val tileXFloat = (lon + 180.0) / 360.0 * n
    val tileX = tileXFloat.toInt()
    return (tileXFloat - tileX) * 256
}

fun getTilePixelY(lat: Double, zoom: Int): Double {
    val n = 2.0.pow(zoom)
    val latRad = lat * Math.PI / 180.0
    val tileYFloat = (1.0 - ln(tan(latRad) + 1.0 / cos(latRad)) / Math.PI) / 2.0 * n
    val tileY = tileYFloat.toInt()
    return (tileYFloat - tileY) * 256
}

/**
 * Inverse projection - tile coordinates to lat/lon
 */
fun tileXToLon(tileX: Double, zoom: Int): Double {
    val n = 2.0.pow(zoom)
    return tileX / n * 360.0 - 180.0
}

fun tileYToLat(tileY: Double, zoom: Int): Double {
    val n = 2.0.pow(zoom)
    val latRad = atan(sinh(Math.PI * (1 - 2 * tileY / n)))
    return latRad * 180.0 / Math.PI
}
