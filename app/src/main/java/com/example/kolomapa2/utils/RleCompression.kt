package com.example.kolomapa2.utils

import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * Run-Length Encoding (RLE) compression for map tiles.
 * Optimized for 1-bit bitmap tiles which often have large uniform areas.
 */
object RleCompression {

    /**
     * Compress tile data using RLE.
     *
     * Format:
     * - Header: [compressed_size (2 bytes, big-endian)]
     * - Data: [count1][value1][count2][value2]...
     *   - count: 1 byte (1-255, number of consecutive identical bytes)
     *   - value: 1 byte (the byte value being repeated)
     *
     * @param data Original tile data (typically 8192 bytes)
     * @return Compressed data with 2-byte size header
     */
    fun encode(data: ByteArray): ByteArray {
        if (data.isEmpty()) {
            return byteArrayOf(0, 0) // Empty with size header
        }

        // Worst case: every byte is different = original_size * 2 + header
        val maxSize = data.size * 2 + 2
        val compressed = ByteBuffer.allocate(maxSize).order(ByteOrder.BIG_ENDIAN)

        // Reserve space for compressed size header (will write at the end)
        compressed.position(2)

        var i = 0
        while (i < data.size) {
            val currentByte = data[i]
            var count = 1

            // Count consecutive identical bytes (max 255)
            while (i + count < data.size &&
                   data[i + count] == currentByte &&
                   count < 255) {
                count++
            }

            // Write [count][value]
            compressed.put(count.toByte())
            compressed.put(currentByte)

            i += count
        }

        val compressedSize = compressed.position()
        val result = ByteArray(compressedSize)

        // Write compressed size header (excluding the header itself)
        result[0] = ((compressedSize - 2) shr 8).toByte()
        result[1] = (compressedSize - 2).toByte()

        // Copy compressed data
        compressed.position(2)
        compressed.get(result, 2, compressedSize - 2)

        return result
    }

    /**
     * Decompress RLE-encoded tile data.
     *
     * @param compressed Compressed data with 2-byte size header
     * @return Decompressed original tile data (8192 bytes)
     * @throws IllegalArgumentException if data is corrupted
     */
    fun decode(compressed: ByteArray): ByteArray {
        if (compressed.size < 2) {
            throw IllegalArgumentException("Invalid compressed data: too short")
        }

        // Read compressed size from header
        val compressedSize = ((compressed[0].toInt() and 0xFF) shl 8) or
                            (compressed[1].toInt() and 0xFF)

        if (compressedSize + 2 != compressed.size) {
            throw IllegalArgumentException("Invalid compressed data: size mismatch")
        }

        // Allocate output buffer (tiles are always 8192 bytes)
        val decompressed = ByteBuffer.allocate(8192)

        var i = 2
        while (i < compressed.size) {
            if (i + 1 >= compressed.size) {
                throw IllegalArgumentException("Invalid compressed data: incomplete run")
            }

            val count = compressed[i].toInt() and 0xFF
            val value = compressed[i + 1]

            if (count == 0) {
                throw IllegalArgumentException("Invalid compressed data: zero count")
            }

            // Write 'count' copies of 'value'
            repeat(count) {
                decompressed.put(value)
            }

            i += 2
        }

        if (decompressed.position() != 8192) {
            throw IllegalArgumentException(
                "Invalid compressed data: decompressed to ${decompressed.position()} bytes, expected 8192"
            )
        }

        return decompressed.array()
    }

    /**
     * Calculate compression ratio.
     *
     * @param originalSize Original data size
     * @param compressedSize Compressed data size (including header)
     * @return Compression ratio (e.g., 0.5 means 50% of original size)
     */
    fun getCompressionRatio(originalSize: Int, compressedSize: Int): Double {
        return compressedSize.toDouble() / originalSize.toDouble()
    }

    /**
     * Get human-readable compression statistics.
     */
    fun getCompressionStats(originalSize: Int, compressedSize: Int): String {
        val ratio = getCompressionRatio(originalSize, compressedSize)
        val savings = ((1.0 - ratio) * 100).toInt()
        return "Compressed: $compressedSize bytes (${(ratio * 100).toInt()}% of original, $savings% savings)"
    }
}
