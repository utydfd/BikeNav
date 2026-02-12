package com.example.kolomapa2.models

/**
 * Parameters for image processing/dithering algorithms
 * All parameters can be tuned in real-time in the test screen
 */
data class ImageProcessingParams(
    // Basic parameters (tuned for optimal map rendering)
    val brightness: Int = 0,
    val contrast: Float = 1.3f,
    val threshold: Int = 128,
    val gamma: Float = 2.50f,

    // Additional Android image processing options
    val saturation: Float = 0f, // Desaturation amount (0 = original, 1 = full grayscale)
    val sharpness: Float = 1.0f, // Sharpening amount (tuned for clarity)
    val edgeEnhancement: Float = 0f, // Edge enhancement (0 = none, positive = enhance)

    // Dithering options
    val ditherAlgorithm: DitherAlgorithm = DitherAlgorithm.FLOYD_STEINBERG,
    val errorDiffusionStrength: Float = 1.0f, // Multiplier for error diffusion (0.0 to 2.0)

    // Advanced preprocessing
    val bilateralFilterRadius: Int = 0, // 0 = off, >0 = smooth while preserving edges
    val morphologicalOperation: MorphOp = MorphOp.NONE, // Dilate/erode to connect/disconnect pixels
    val morphKernelSize: Int = 3, // Kernel size for morphological operations

    // Adaptive thresholding (alternative to fixed threshold)
    val useAdaptiveThreshold: Boolean = false,
    val adaptiveBlockSize: Int = 11, // Must be odd
    val adaptiveC: Int = 2 // Constant subtracted from mean
) {
    companion object {
        /**
         * Production settings (tuned for optimal map rendering)
         * These are the settings used when sending tiles to ESP32
         */
        fun production() = ImageProcessingParams(
            brightness = 0,
            contrast = 1.3f,
            threshold = 128,
            gamma = 2.50f,
            sharpness = 1.0f,
            edgeEnhancement = 0f,
            ditherAlgorithm = DitherAlgorithm.FLOYD_STEINBERG,
            errorDiffusionStrength = 1.0f,
            bilateralFilterRadius = 0,
            morphologicalOperation = MorphOp.NONE,
            useAdaptiveThreshold = false
        )

        /**
         * Default settings (same as production for consistency)
         */
        fun default() = production()

        /**
         * Preset optimized for preserving thin lines and road edges
         */
        fun thinLinePreserving() = ImageProcessingParams(
            brightness = 10,
            contrast = 1.1f,
            threshold = 140,
            gamma = 1.8f,
            sharpness = 0.3f,
            edgeEnhancement = 0.2f,
            ditherAlgorithm = DitherAlgorithm.ATKINSON,
            errorDiffusionStrength = 0.75f,
            morphologicalOperation = MorphOp.CLOSE,
            morphKernelSize = 3
        )

        /**
         * Preset for high contrast, bold features
         */
        fun highContrast() = ImageProcessingParams(
            brightness = 0,
            contrast = 1.6f,
            threshold = 128,
            gamma = 2.2f,
            ditherAlgorithm = DitherAlgorithm.FLOYD_STEINBERG,
            morphologicalOperation = MorphOp.DILATE,
            morphKernelSize = 3
        )

        /**
         * Preset for detailed, fine features
         */
        fun detailPreserving() = ImageProcessingParams(
            brightness = 5,
            contrast = 1.2f,
            threshold = 130,
            gamma = 2.0f,
            sharpness = 0.5f,
            ditherAlgorithm = DitherAlgorithm.FLOYD_STEINBERG_REDUCED,
            errorDiffusionStrength = 0.5f
        )
    }
}

enum class DitherAlgorithm {
    FLOYD_STEINBERG,        // Current algorithm (7/16 error to right, rest below)
    FLOYD_STEINBERG_REDUCED,// Floyd-Steinberg with reduced error diffusion
    ATKINSON,              // Atkinson (better for thin lines, less contrast)
    JARVIS_JUDICE_NINKE,  // JJN (wider error distribution)
    STUCKI,               // Stucki (similar to JJN but different weights)
    SIERRA,               // Sierra (3-row distribution)
    BURKES,               // Burkes (2-row, faster than Sierra)
    SIMPLE_THRESHOLD,     // No dithering, just threshold
    ORDERED_BAYER_2x2,    // Ordered dithering (Bayer matrix 2x2)
    ORDERED_BAYER_4x4,    // Ordered dithering (Bayer matrix 4x4)
    ORDERED_BAYER_8x8     // Ordered dithering (Bayer matrix 8x8)
}

enum class MorphOp {
    NONE,     // No morphological operation
    DILATE,   // Expand white areas (connect nearby pixels)
    ERODE,    // Shrink white areas (disconnect pixels)
    OPEN,     // Erode then dilate (remove small white noise)
    CLOSE     // Dilate then erode (fill small black gaps, connect thin lines)
}
