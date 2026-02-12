package com.example.kolomapa2.utils

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import okhttp3.OkHttpClient
import okhttp3.Request
import java.io.IOException
import java.time.ZoneOffset
import java.time.ZonedDateTime
import java.time.format.DateTimeFormatter
import java.util.concurrent.TimeUnit

/**
 * Service for fetching the latest radar composite PNG from CHMU.
 */
class RadarApiService {

    companion object {
        private const val BASE_URL = "https://opendata.chmi.cz/meteorology/weather/radar/composite/maxz/png_masked/"
        private const val FILE_PREFIX = "pacz2gmaps3.z_max3d."
        private const val FILE_SUFFIX = ".0.png"
        private const val NOWCAST_BASE_URL = "https://opendata.chmi.cz/meteorology/weather/radar/composite/fct_maxz/png_masked/"
        private const val NOWCAST_FILE_PREFIX = "pacz2gmaps3.fct_z_max."
        private const val NOWCAST_TAR_SUFFIX = ".ft60s10.tar"
        const val UPDATE_STEP_MINUTES = 5
        const val NOWCAST_STEP_MINUTES = 10
        const val RADAR_PAST_STEPS = 6
        const val RADAR_FUTURE_STEPS = 5
        private const val CURRENT_LOOKBACK_STEPS = 3
        private val TIMESTAMP_FORMAT = DateTimeFormatter.ofPattern("yyyyMMdd.HHmm")
        private const val TAR_BLOCK_SIZE = 512
    }

    private val client = OkHttpClient.Builder()
        .connectTimeout(15, TimeUnit.SECONDS)
        .readTimeout(15, TimeUnit.SECONDS)
        .build()

    data class RadarFramePng(
        val timestamp: ZonedDateTime,
        val pngData: ByteArray
    )

    suspend fun fetchLatestRadarPng(): Result<ByteArray> = withContext(Dispatchers.IO) {
        fetchCurrentRadarPng().fold(
            onSuccess = { Result.success(it.pngData) },
            onFailure = { Result.failure(it) }
        )
    }

    suspend fun fetchCurrentRadarPng(): Result<RadarFramePng> = withContext(Dispatchers.IO) {
        try {
            val aligned = alignToRadarStep(ZonedDateTime.now(ZoneOffset.UTC))

            for (step in 0..CURRENT_LOOKBACK_STEPS) {
                val candidate = aligned.minusMinutes((step * UPDATE_STEP_MINUTES).toLong())
                val pngResult = fetchRadarPngForTimestamp(candidate)
                if (pngResult.isSuccess) {
                    val pngData = pngResult.getOrThrow()
                    return@withContext Result.success(RadarFramePng(candidate, pngData))
                }
            }

            Result.failure(IOException("Radar PNG not available for recent timestamps"))
        } catch (e: Exception) {
            Result.failure(e)
        }
    }

    suspend fun fetchRadarPngForTimestamp(timestamp: ZonedDateTime): Result<ByteArray> = withContext(Dispatchers.IO) {
        try {
            val url = buildRadarUrl(timestamp)

            val request = Request.Builder()
                .url(url)
                .header("User-Agent", "KoloMapa2/1.0 BikeNav Weather Radar")
                .build()

            val response = client.newCall(request).execute()
            if (!response.isSuccessful) {
                response.close()
                return@withContext Result.failure(IOException("Radar PNG not available for $timestamp"))
            }

            val body = response.body?.bytes()
            response.close()

            if (body == null || body.size < 8 || body[0] != 0x89.toByte() || body[1] != 0x50.toByte()) {
                return@withContext Result.failure(IOException("Radar PNG invalid for $timestamp"))
            }

            Result.success(body)
        } catch (e: Exception) {
            Result.failure(e)
        }
    }

    suspend fun fetchNowcastRadarPng(
        baseTimestamp: ZonedDateTime,
        leadMinutes: Int
    ): Result<ByteArray> = withContext(Dispatchers.IO) {
        try {
            val tarUrl = buildNowcastTarUrl(baseTimestamp)
            val tarBytes = fetchBytesByUrl(tarUrl).getOrElse { return@withContext Result.failure(it) }
            val entryName = buildNowcastTarEntryName(baseTimestamp, leadMinutes)
            val pngBytes = extractTarEntry(tarBytes, entryName)
                ?: return@withContext Result.failure(IOException("Radar nowcast not available for $leadMinutes"))

            if (pngBytes.size < 8 || pngBytes[0] != 0x89.toByte() || pngBytes[1] != 0x50.toByte()) {
                return@withContext Result.failure(IOException("Radar nowcast PNG invalid for $leadMinutes"))
            }

            Result.success(pngBytes)
        } catch (e: Exception) {
            Result.failure(e)
        }
    }

    fun alignToNowcastStep(time: ZonedDateTime): ZonedDateTime {
        return alignToStep(time, NOWCAST_STEP_MINUTES)
    }

    private fun alignToRadarStep(time: ZonedDateTime): ZonedDateTime {
        return alignToStep(time, UPDATE_STEP_MINUTES)
    }

    private fun alignToStep(time: ZonedDateTime, stepMinutes: Int): ZonedDateTime {
        val step = if (stepMinutes > 0) stepMinutes else 1
        val alignedMinute = (time.minute / step) * step
        return time
            .withMinute(alignedMinute)
            .withSecond(0)
            .withNano(0)
    }

    private fun buildRadarUrl(timestamp: ZonedDateTime): String {
        val formatted = timestamp.format(TIMESTAMP_FORMAT)
        return "$BASE_URL$FILE_PREFIX$formatted$FILE_SUFFIX"
    }

    private fun buildNowcastTarUrl(baseTimestamp: ZonedDateTime): String {
        val formatted = baseTimestamp.format(TIMESTAMP_FORMAT)
        return "$NOWCAST_BASE_URL$NOWCAST_FILE_PREFIX$formatted$NOWCAST_TAR_SUFFIX"
    }

    private fun buildNowcastTarEntryName(baseTimestamp: ZonedDateTime, leadMinutes: Int): String {
        val baseFormatted = baseTimestamp.format(TIMESTAMP_FORMAT)
        val validTime = baseTimestamp.plusMinutes(leadMinutes.toLong()).format(TIMESTAMP_FORMAT)
        return "$baseFormatted/$NOWCAST_FILE_PREFIX$validTime.$leadMinutes.png"
    }

    private fun fetchRadarPngByUrl(url: String): Result<ByteArray> {
        val body = fetchBytesByUrl(url).getOrElse { return Result.failure(it) }
        if (body.size < 8 || body[0] != 0x89.toByte() || body[1] != 0x50.toByte()) {
            return Result.failure(IOException("Radar PNG invalid"))
        }
        return Result.success(body)
    }

    private fun fetchBytesByUrl(url: String): Result<ByteArray> {
        val request = Request.Builder()
            .url(url)
            .header("User-Agent", "KoloMapa2/1.0 BikeNav Weather Radar")
            .build()

        val response = client.newCall(request).execute()
        if (!response.isSuccessful) {
            response.close()
            return Result.failure(IOException("Radar data not available"))
        }

        val body = response.body?.bytes()
        response.close()

        return if (body == null) {
            Result.failure(IOException("Radar data empty"))
        } else {
            Result.success(body)
        }
    }

    private fun extractTarEntry(tarBytes: ByteArray, entryName: String): ByteArray? {
        var offset = 0
        while (offset + TAR_BLOCK_SIZE <= tarBytes.size) {
            val header = tarBytes.copyOfRange(offset, offset + TAR_BLOCK_SIZE)
            if (header.all { it == 0.toByte() }) {
                return null
            }

            val name = header.copyOfRange(0, 100)
                .toString(Charsets.US_ASCII)
                .trimEnd('\u0000')
            val sizeText = header.copyOfRange(124, 136)
                .toString(Charsets.US_ASCII)
                .trim { it == '\u0000' || it == ' ' }
            val size = sizeText.toLongOrNull(8) ?: 0L
            val dataStart = offset + TAR_BLOCK_SIZE
            val dataEnd = dataStart + size.toInt()

            if (name == entryName) {
                return if (dataEnd <= tarBytes.size) {
                    tarBytes.copyOfRange(dataStart, dataEnd)
                } else {
                    null
                }
            }

            val paddedSize = ((size + TAR_BLOCK_SIZE - 1) / TAR_BLOCK_SIZE) * TAR_BLOCK_SIZE
            offset = dataStart + paddedSize.toInt()
        }
        return null
    }
}
