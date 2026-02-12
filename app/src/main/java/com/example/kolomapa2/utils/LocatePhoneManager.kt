package com.example.kolomapa2.utils

import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.media.AudioAttributes
import android.media.AudioManager
import android.media.Ringtone
import android.media.RingtoneManager
import android.os.Build
import android.os.VibrationEffect
import android.os.Vibrator
import android.os.VibratorManager
import android.util.Log
import androidx.core.app.NotificationCompat
import com.example.kolomapa2.MainActivity
import com.example.kolomapa2.R
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

/**
 * Manages "locate phone" functionality - plays ringtone and vibrates.
 */
class LocatePhoneManager(private val context: Context) {

    companion object {
        private const val CHANNEL_ID = "locate_phone_channel"
        private const val NOTIFICATION_ID = 1002  // Different from BleService (1001)
        const val ACTION_STOP_LOCATE = "com.example.kolomapa2.STOP_LOCATE_PHONE"
    }

    private var currentRingtone: Ringtone? = null
    private val audioManager = context.getSystemService(Context.AUDIO_SERVICE) as AudioManager
    private val notificationManager = context.getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
    private var locateJob: kotlinx.coroutines.Job? = null
    private var isLocating = false
    private var screenUnlockReceiver: BroadcastReceiver? = null

    init {
        createNotificationChannel()
    }

    /**
     * Create notification channel for locate phone alerts.
     */
    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val channel = NotificationChannel(
                CHANNEL_ID,
                "Locate Phone Alerts",
                NotificationManager.IMPORTANCE_HIGH
            ).apply {
                description = "Notifications for locate phone alarm"
                enableVibration(false) // We handle vibration manually
                setSound(null, null) // We handle ringtone manually
            }
            notificationManager.createNotificationChannel(channel)
        }
    }

    /**
     * Check if locate phone is currently active.
     */
    fun isLocating(): Boolean = isLocating

    /**
     * Start locate phone - play ringtone and vibrate for a few seconds.
     */
    fun startLocatePhone(durationMs: Long = 10000) {
        // Cancel any existing locate operation
        stopLocatePhone()

        isLocating = true

        // Show notification
        showLocateNotification()

        // Register screen unlock receiver to auto-stop alarm
        registerScreenUnlockReceiver()

        locateJob = CoroutineScope(Dispatchers.Main).launch {
            try {
                Log.d("LocatePhoneManager", "Starting locate phone")

                // Start vibration
                startVibration(durationMs)

                // Play ringtone
                playRingtone()

                // Stop after duration
                delay(durationMs)
                stopLocatePhoneInternal()

                Log.d("LocatePhoneManager", "Locate phone finished")
            } catch (e: kotlinx.coroutines.CancellationException) {
                Log.d("LocatePhoneManager", "Locate phone cancelled")
                stopLocatePhoneInternal()
            } catch (e: Exception) {
                Log.e("LocatePhoneManager", "Error in locate phone", e)
                stopLocatePhoneInternal()
            }
        }
    }

    /**
     * Stop locate phone immediately.
     */
    fun stopLocatePhone() {
        Log.d("LocatePhoneManager", "Stopping locate phone (external call)")
        locateJob?.cancel()
        locateJob = null
        stopLocatePhoneInternal()
    }

    /**
     * Show notification with action to stop alarm.
     */
    private fun showLocateNotification() {
        // Create intent to open app
        val openAppIntent = Intent(context, MainActivity::class.java).apply {
            flags = Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TOP
        }
        val openAppPendingIntent = PendingIntent.getActivity(
            context,
            0,
            openAppIntent,
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )

        // Create intent to stop alarm
        val stopIntent = Intent(ACTION_STOP_LOCATE).apply {
            setPackage(context.packageName)
        }
        val stopPendingIntent = PendingIntent.getBroadcast(
            context,
            0,
            stopIntent,
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )

        // Build notification
        val notification = NotificationCompat.Builder(context, CHANNEL_ID)
            .setSmallIcon(R.drawable.ic_notification)
            .setContentTitle("Finding your phone")
            .setContentText("Tap to open app or stop alarm")
            .setPriority(NotificationCompat.PRIORITY_HIGH)
            .setCategory(NotificationCompat.CATEGORY_ALARM)
            .setAutoCancel(true)
            .setOngoing(true)
            .setContentIntent(openAppPendingIntent)
            .addAction(
                R.drawable.ic_notification,
                "Stop Alarm",
                stopPendingIntent
            )
            .build()

        notificationManager.notify(NOTIFICATION_ID, notification)
        Log.d("LocatePhoneManager", "Notification shown")
    }

    /**
     * Register broadcast receiver for screen unlock events.
     */
    private fun registerScreenUnlockReceiver() {
        if (screenUnlockReceiver != null) {
            return // Already registered
        }

        screenUnlockReceiver = object : BroadcastReceiver() {
            override fun onReceive(context: Context?, intent: Intent?) {
                when (intent?.action) {
                    Intent.ACTION_USER_PRESENT -> {
                        Log.d("LocatePhoneManager", "Screen unlocked - stopping alarm")
                        stopLocatePhone()
                    }
                    ACTION_STOP_LOCATE -> {
                        Log.d("LocatePhoneManager", "Stop action from notification")
                        stopLocatePhone()
                    }
                }
            }
        }

        val filter = IntentFilter().apply {
            addAction(Intent.ACTION_USER_PRESENT) // Screen unlocked
            addAction(ACTION_STOP_LOCATE) // Stop button pressed
        }

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            context.registerReceiver(screenUnlockReceiver, filter, Context.RECEIVER_NOT_EXPORTED)
        } else {
            context.registerReceiver(screenUnlockReceiver, filter)
        }

        Log.d("LocatePhoneManager", "Screen unlock receiver registered")
    }

    /**
     * Unregister screen unlock receiver.
     */
    private fun unregisterScreenUnlockReceiver() {
        screenUnlockReceiver?.let {
            try {
                context.unregisterReceiver(it)
                Log.d("LocatePhoneManager", "Screen unlock receiver unregistered")
            } catch (e: Exception) {
                Log.e("LocatePhoneManager", "Error unregistering receiver", e)
            }
            screenUnlockReceiver = null
        }
    }

    /**
     * Internal method to stop ringtone and vibration.
     */
    private fun stopLocatePhoneInternal() {
        try {
            isLocating = false

            // Stop ringtone
            currentRingtone?.stop()
            currentRingtone = null

            // Stop vibration
            val vibrator = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                val vibratorManager = context.getSystemService(Context.VIBRATOR_MANAGER_SERVICE) as VibratorManager
                vibratorManager.defaultVibrator
            } else {
                @Suppress("DEPRECATION")
                context.getSystemService(Context.VIBRATOR_SERVICE) as Vibrator
            }
            vibrator.cancel()

            // Dismiss notification
            notificationManager.cancel(NOTIFICATION_ID)

            // Unregister screen unlock receiver
            unregisterScreenUnlockReceiver()

            Log.d("LocatePhoneManager", "Stopped locate phone internal")
        } catch (e: Exception) {
            Log.e("LocatePhoneManager", "Error stopping locate phone", e)
        }
    }

    private fun playRingtone() {
        try {
            // Get default ringtone URI
            val ringtoneUri = RingtoneManager.getDefaultUri(RingtoneManager.TYPE_RINGTONE)

            // Create ringtone
            val ringtone = RingtoneManager.getRingtone(context, ringtoneUri)

            // Set audio attributes to play at max volume
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
                val audioAttributes = AudioAttributes.Builder()
                    .setUsage(AudioAttributes.USAGE_ALARM)
                    .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
                    .build()
                ringtone.audioAttributes = audioAttributes
            } else {
                @Suppress("DEPRECATION")
                ringtone.streamType = AudioManager.STREAM_ALARM
            }

            // Play ringtone
            ringtone.play()
            currentRingtone = ringtone

            Log.d("LocatePhoneManager", "Playing ringtone")
        } catch (e: Exception) {
            Log.e("LocatePhoneManager", "Error playing ringtone", e)
        }
    }

    private fun startVibration(durationMs: Long) {
        try {
            val vibrator = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                val vibratorManager = context.getSystemService(Context.VIBRATOR_MANAGER_SERVICE) as VibratorManager
                vibratorManager.defaultVibrator
            } else {
                @Suppress("DEPRECATION")
                context.getSystemService(Context.VIBRATOR_SERVICE) as Vibrator
            }

            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                // Create vibration pattern: 500ms on, 200ms off, repeat
                val pattern = longArrayOf(0, 500, 200, 500, 200, 500, 200, 500, 200)
                val vibrationEffect = VibrationEffect.createWaveform(pattern, 0) // 0 = repeat from start
                vibrator.vibrate(vibrationEffect)
            } else {
                @Suppress("DEPRECATION")
                val pattern = longArrayOf(0, 500, 200, 500, 200, 500, 200, 500, 200)
                vibrator.vibrate(pattern, 0) // 0 = repeat from start
            }

            Log.d("LocatePhoneManager", "Started vibration")
        } catch (e: Exception) {
            Log.e("LocatePhoneManager", "Error starting vibration", e)
        }
    }
}
