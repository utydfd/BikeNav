package com.example.kolomapa2.utils

/**
 * Application-wide constants for KoloMapa2
 */
object Constants {
    /**
     * Home location coordinates for "Navigate Home" feature
     * TODO: Replace with your actual home coordinates
     */
    const val HOME_LATITUDE = 50.069549 // Example: Prague, Czech Republic
    const val HOME_LONGITUDE = 14.166099

    /**
     * Temporary trip name for navigate-home routes
     * This trip is stored only in ESP32 PSRAM, not saved to SD card
     */
    const val NAVIGATE_HOME_TRIP_NAME = "_nav_home_temp"
}
