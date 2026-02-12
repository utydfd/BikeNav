package com.example.kolomapa2.utils

import android.app.Notification
import android.content.Intent
import android.os.Build
import android.service.notification.NotificationListenerService
import android.service.notification.StatusBarNotification
import android.util.Log
import java.util.concurrent.ConcurrentHashMap

class NotificationListener : NotificationListenerService() {

    companion object {
        private const val TAG = "NotificationListener"

        // Static instance to allow other components to interact with the service
        @Volatile
        private var instance: NotificationListener? = null

        fun getInstance(): NotificationListener? = instance

        // Callback for when notifications are received
        var onNotificationReceived: ((NotificationData) -> Unit)? = null
        var onNotificationRemoved: ((Int) -> Unit)? = null
    }

    // Track active notifications by their hash ID
    private val activeNotifications = ConcurrentHashMap<Int, String>()

    data class NotificationData(
        val id: Int,
        val appName: String,
        val title: String,
        val text: String,
        val timestamp: Long,
        val iconData: ByteArray? = null // 195 bytes if present, null if extraction failed
    ) {
        // Override equals/hashCode to handle ByteArray properly
        override fun equals(other: Any?): Boolean {
            if (this === other) return true
            if (javaClass != other?.javaClass) return false

            other as NotificationData

            if (id != other.id) return false
            if (appName != other.appName) return false
            if (title != other.title) return false
            if (text != other.text) return false
            if (timestamp != other.timestamp) return false
            if (iconData != null) {
                if (other.iconData == null) return false
                if (!iconData.contentEquals(other.iconData)) return false
            } else if (other.iconData != null) return false

            return true
        }

        override fun hashCode(): Int {
            var result = id
            result = 31 * result + appName.hashCode()
            result = 31 * result + title.hashCode()
            result = 31 * result + text.hashCode()
            result = 31 * result + timestamp.hashCode()
            result = 31 * result + (iconData?.contentHashCode() ?: 0)
            return result
        }
    }

    override fun onCreate() {
        super.onCreate()
        instance = this
        Log.d(TAG, "NotificationListener service created")
    }

    override fun onDestroy() {
        super.onDestroy()
        instance = null
        Log.d(TAG, "NotificationListener service destroyed")
    }

    override fun onListenerConnected() {
        super.onListenerConnected()
        Log.d(TAG, "NotificationListener connected")
    }

    override fun onListenerDisconnected() {
        super.onListenerDisconnected()
        Log.d(TAG, "NotificationListener disconnected")
    }

    override fun onNotificationPosted(sbn: StatusBarNotification) {
        try {
            // Filter out notifications we don't want to sync
            if (shouldFilterNotification(sbn)) {
                return
            }

            val notification = sbn.notification ?: return
            val extras = notification.extras ?: return

            // Extract notification data
            val appName = getAppName(sbn.packageName)
            val title = extras.getCharSequence(Notification.EXTRA_TITLE)?.toString() ?: ""
            val text = extras.getCharSequence(Notification.EXTRA_TEXT)?.toString() ?: ""

            // Skip empty notifications
            if (title.isEmpty() && text.isEmpty()) {
                return
            }

            // Generate unique ID from notification key
            val notificationId = sbn.key.hashCode()

            // Store in active notifications map
            activeNotifications[notificationId] = sbn.key

            // Truncate text to fit packet size
            val truncatedTitle = title.take(63)
            val truncatedText = text.take(127)

            // Extract and convert app icon (may return null if extraction fails)
            val iconData = try {
                IconConverter.extractAndConvertIcon(sbn, applicationContext, packageManager)
            } catch (e: Exception) {
                Log.w(TAG, "Failed to extract icon for $appName: ${e.message}")
                null
            }

            val notificationData = NotificationData(
                id = notificationId,
                appName = appName.take(31),
                title = truncatedTitle,
                text = truncatedText,
                timestamp = sbn.postTime,
                iconData = iconData
            )

            Log.d(TAG, "Notification posted: $appName - $truncatedTitle" +
                      if (iconData != null) " (with icon)" else " (no icon)")

            // Send to BLE if callback is set
            onNotificationReceived?.invoke(notificationData)

        } catch (e: Exception) {
            Log.e(TAG, "Error processing notification: ${e.message}", e)
        }
    }

    override fun onNotificationRemoved(sbn: StatusBarNotification) {
        try {
            val notificationId = sbn.key.hashCode()

            // Only process if we were tracking this notification
            if (activeNotifications.containsKey(notificationId)) {
                activeNotifications.remove(notificationId)

                Log.d(TAG, "Notification removed: ID=$notificationId")

                // Notify BLE to dismiss on ESP32
                onNotificationRemoved?.invoke(notificationId)
            }
        } catch (e: Exception) {
            Log.e(TAG, "Error processing notification removal: ${e.message}", e)
        }
    }

    /**
     * Dismiss a notification from the phone's status bar.
     * Called when user dismisses notification on ESP32.
     */
    fun dismissNotification(notificationId: Int) {
        try {
            val key = activeNotifications[notificationId]
            if (key != null) {
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
                    cancelNotification(key)
                    Log.d(TAG, "Dismissed notification: ID=$notificationId")
                }
                activeNotifications.remove(notificationId)
            }
        } catch (e: Exception) {
            Log.e(TAG, "Error dismissing notification: ${e.message}", e)
        }
    }

    /**
     * Filter out notifications that shouldn't be synced to ESP32.
     */
    private fun shouldFilterNotification(sbn: StatusBarNotification): Boolean {
        val notification = sbn.notification ?: return true

        // Filter out our own app
        if (sbn.packageName == packageName) {
            return true
        }

        // Filter out system UI
        if (sbn.packageName == "com.android.systemui") {
            return true
        }

        // Filter out ongoing notifications (like music players, ongoing calls)
        if ((notification.flags and Notification.FLAG_ONGOING_EVENT) != 0) {
            return true
        }

        // Filter out low priority notifications
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            if (notification.channelId?.contains("silent", ignoreCase = true) == true) {
                return true
            }
        }

        // Filter out group summary notifications (they're just containers)
        if ((notification.flags and Notification.FLAG_GROUP_SUMMARY) != 0) {
            return true
        }

        return false
    }

    /**
     * Get user-friendly app name from package name.
     */
    private fun getAppName(packageName: String): String {
        return try {
            val applicationInfo = packageManager.getApplicationInfo(packageName, 0)
            packageManager.getApplicationLabel(applicationInfo).toString()
        } catch (e: Exception) {
            // Fallback to package name if we can't get the app name
            packageName.substringAfterLast('.').replaceFirstChar { it.uppercase() }
        }
    }
}
