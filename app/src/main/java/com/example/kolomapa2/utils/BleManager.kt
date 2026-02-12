package com.example.kolomapa2.utils

import android.annotation.SuppressLint
import android.bluetooth.*
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.os.ParcelUuid
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import java.io.ByteArrayOutputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.*

@SuppressLint("MissingPermission")
class BleManager(private val context: Context) {

    companion object {
        val SERVICE_UUID: UUID = UUID.fromString("12345678-1234-1234-1234-123456789abc")

        val TILE_CHARACTERISTIC_UUID: UUID = UUID.fromString("12345678-1234-1234-1234-123456789abd")
        val TRIP_CHARACTERISTIC_UUID: UUID = UUID.fromString("12345678-1234-1234-1234-123456789abe")
        val WEATHER_CHARACTERISTIC_UUID: UUID = UUID.fromString("12345678-1234-1234-1234-123456789abf")
        val RADAR_CHARACTERISTIC_UUID: UUID = UUID.fromString("12345678-1234-1234-1234-123456789ac5")
        val NOTIFICATION_CHARACTERISTIC_UUID: UUID = UUID.fromString("12345678-1234-1234-1234-123456789ac0")
        val TRIP_LIST_CHARACTERISTIC_UUID: UUID = UUID.fromString("12345678-1234-1234-1234-123456789ac1")
        val TRIP_CONTROL_CHARACTERISTIC_UUID: UUID = UUID.fromString("12345678-1234-1234-1234-123456789ac2")
        val NAVIGATE_HOME_CHARACTERISTIC_UUID: UUID = UUID.fromString("12345678-1234-1234-1234-123456789ac3")
        val DEVICE_STATUS_CHARACTERISTIC_UUID: UUID = UUID.fromString("12345678-1234-1234-1234-123456789ac4")
        val RECORDING_LIST_CHARACTERISTIC_UUID: UUID = UUID.fromString("12345678-1234-1234-1234-123456789ac6")
        val RECORDING_CONTROL_CHARACTERISTIC_UUID: UUID = UUID.fromString("12345678-1234-1234-1234-123456789ac7")
        val RECORDING_TRANSFER_CHARACTERISTIC_UUID: UUID = UUID.fromString("12345678-1234-1234-1234-123456789ac8")

        const val DEVICE_NAME = "KoloMapa2"
        const val MTU_SIZE = 517
        const val CHUNK_SIZE = 500
        private const val WEATHER_PACKET_SIZE = 145
        const val RADAR_IMAGE_WIDTH = 128
        const val RADAR_IMAGE_HEIGHT = 296
        private const val RADAR_ERROR_MESSAGE_SIZE = 64
        private const val RADAR_IMAGE_BYTES = (RADAR_IMAGE_WIDTH * RADAR_IMAGE_HEIGHT) / 8
        private const val RADAR_FRAME_HEADER_SIZE = 4
        private const val RADAR_PACKET_SIZE = RADAR_FRAME_HEADER_SIZE + RADAR_ERROR_MESSAGE_SIZE + RADAR_IMAGE_BYTES
        private const val RADAR_DEFAULT_STEP_MINUTES = 5
        private const val RADAR_BASE_TIME_MAGIC: Byte = 0xA5.toByte()
        private const val TILE_INV_ACTION_REQUEST = 0x10
        private const val TILE_INV_ACTION_START = 0x11
        private const val TILE_INV_ACTION_DATA = 0x12
        private const val TILE_INV_ACTION_END = 0x13
        private const val TILE_INV_ACTION_ERROR = 0x14
        private const val TILE_INV_RECORD_SIZE = 9
        private const val TILE_INV_TIMEOUT_MS = 20000L
        private const val RECORDING_CONTROL_ACTION_LIST = 0x01
        private const val RECORDING_CONTROL_ACTION_DOWNLOAD = 0x02
        private const val RECORDING_TRANSFER_ACTION_START = 0x30
        private const val RECORDING_TRANSFER_ACTION_DATA = 0x31
        private const val RECORDING_TRANSFER_ACTION_END = 0x32
        private const val RECORDING_TRANSFER_ACTION_ERROR = 0x33
    }

    private val bluetoothAdapter: BluetoothAdapter? =
        (context.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager).adapter
    private var bluetoothGatt: BluetoothGatt? = null
    private var tileCharacteristic: BluetoothGattCharacteristic? = null
    private var tripCharacteristic: BluetoothGattCharacteristic? = null
    private var weatherCharacteristic: BluetoothGattCharacteristic? = null
    private var radarCharacteristic: BluetoothGattCharacteristic? = null
    private var notificationCharacteristic: BluetoothGattCharacteristic? = null
    private var tripListCharacteristic: BluetoothGattCharacteristic? = null
    private var tripControlCharacteristic: BluetoothGattCharacteristic? = null
    private var navigateHomeCharacteristic: BluetoothGattCharacteristic? = null
    private var deviceStatusCharacteristic: BluetoothGattCharacteristic? = null
    private var recordingListCharacteristic: BluetoothGattCharacteristic? = null
    private var recordingControlCharacteristic: BluetoothGattCharacteristic? = null
    private var recordingTransferCharacteristic: BluetoothGattCharacteristic? = null

    var onWeatherRequest: ((latitude: Double, longitude: Double) -> Unit)? = null
    var onRadarRequest: ((latitude: Double, longitude: Double, zoom: Int) -> Unit)? = null
    var onNavigateHomeRequest: ((latitude: Double, longitude: Double) -> Unit)? = null
    var onNotificationDismissed: ((notificationId: Int) -> Unit)? = null
    var onTripListReceived: ((tripNames: List<String>) -> Unit)? = null
    var onActiveTripChanged: ((tripName: String?) -> Unit)? = null
    var onEspDeviceStatusReceived: ((status: EspDeviceStatus) -> Unit)? = null
    var onRecordingListReceived: ((recordingNames: List<String>) -> Unit)? = null
    var onRecordingTransferProgress: ((recordingName: String, receivedBytes: Int, totalBytes: Int) -> Unit)? = null
    var onRecordingTransferCompleted: ((recordingName: String, metadataJson: String, gpxContent: String) -> Unit)? = null
    var onRecordingTransferError: ((recordingName: String?, message: String) -> Unit)? = null

    // Device status command callbacks from ESP32
    var onMusicPlayPause: (() -> Unit)? = null
    var onMusicNext: (() -> Unit)? = null
    var onMusicPrevious: (() -> Unit)? = null
    var onLocatePhone: (() -> Unit)? = null
    var onToggleNotificationSync: (() -> Unit)? = null
    var onDeviceStatusRequested: (() -> Unit)? = null

    private val _connectionState = MutableStateFlow<ConnectionState>(ConnectionState.DISCONNECTED)
    val connectionState: StateFlow<ConnectionState> = _connectionState

    private val _transferProgress = MutableStateFlow(TransferProgress(0, 0, ""))
    val transferProgress: StateFlow<TransferProgress> = _transferProgress

    // Channel to receive ACKs from ESP32 (using Channel to avoid missing emissions)
    private val _tileAckChannel = kotlinx.coroutines.channels.Channel<Boolean>(kotlinx.coroutines.channels.Channel.CONFLATED)

    private val tileInventoryRequestMutex = Mutex()

    @Volatile
    private var tileInventoryCollector: TileInventoryCollector? = null

    @Volatile
    private var writeCompleted = true

    @Volatile
    private var weatherNotificationsEnabled = false

    @Volatile
    private var tileNotificationsEnabled = false

    @Volatile
    private var radarNotificationsEnabled = false

    @Volatile
    private var notificationNotificationsEnabled = false

    @Volatile
    private var tripListNotificationsEnabled = false

    @Volatile
    private var tripControlNotificationsEnabled = false

    @Volatile
    private var navigateHomeNotificationsEnabled = false

    @Volatile
    private var deviceStatusNotificationsEnabled = false

    @Volatile
    private var recordingListNotificationsEnabled = false

    @Volatile
    private var recordingTransferNotificationsEnabled = false

    private data class RecordingTransferState(
        val recordingName: String,
        val metadataSize: Int,
        val gpxSize: Int,
        val buffer: ByteArrayOutputStream,
        var receivedBytes: Int = 0
    )

    @Volatile
    private var recordingTransferState: RecordingTransferState? = null

    sealed class ConnectionState {
        object DISCONNECTED : ConnectionState()
        object SCANNING : ConnectionState()
        object CONNECTING : ConnectionState()
        object CONNECTED : ConnectionState()
        object BLUETOOTH_DISABLED : ConnectionState()
        data class ERROR(val message: String) : ConnectionState()
    }

    data class TransferProgress(val current: Int, val total: Int, val message: String)

    data class EspDeviceStatus(
        val batteryPercent: Int,
        val gpsStage: Int,  // 0 = no data, 1 = time, 2 = date, 3 = location locked
        val satelliteCount: Int
    )

    private data class TileInventoryCollector(
        val deferred: CompletableDeferred<Set<Long>>,
        val tiles: HashSet<Long>,
        var started: Boolean = false
    )

    private fun compressRLE(data: ByteArray): ByteArray {
        val output = ByteArrayOutputStream()
        var i = 0
        while (i < data.size) {
            val value = data[i]
            var count = 1
            while (i + count < data.size && count < 255 && data[i + count] == value) {
                count++
            }
            output.write(count)
            output.write(value.toInt())
            i += count
        }
        return output.toByteArray()
    }

    private fun readIntBE(data: ByteArray, offset: Int): Int {
        return ((data[offset].toInt() and 0xFF) shl 24) or
            ((data[offset + 1].toInt() and 0xFF) shl 16) or
            ((data[offset + 2].toInt() and 0xFF) shl 8) or
            (data[offset + 3].toInt() and 0xFF)
    }

    private fun packTileKey(zoom: Int, tileX: Int, tileY: Int): Long {
        val z = (zoom.toLong() and 0xFF) shl 40
        val x = (tileX.toLong() and 0xFFFFF) shl 20
        val y = tileY.toLong() and 0xFFFFF
        return z or x or y
    }

    private fun packTileKey(tile: Triple<Int, Int, Int>): Long {
        return packTileKey(tile.first, tile.second, tile.third)
    }

    private val gattCallback = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(gatt: BluetoothGatt, status: Int, newState: Int) {
            when (newState) {
                BluetoothProfile.STATE_CONNECTED -> {
                    _connectionState.value = ConnectionState.CONNECTING
                    gatt.requestConnectionPriority(BluetoothGatt.CONNECTION_PRIORITY_HIGH)
                    gatt.requestMtu(MTU_SIZE)
                }
                BluetoothProfile.STATE_DISCONNECTED -> {
                    _connectionState.value = ConnectionState.DISCONNECTED
                    tileInventoryCollector?.deferred?.completeExceptionally(
                        RuntimeException("Disconnected during tile inventory")
                    )
                    tileInventoryCollector = null
                    bluetoothGatt?.close()
                    bluetoothGatt = null
                }
            }
        }

        override fun onMtuChanged(gatt: BluetoothGatt, mtu: Int, status: Int) {
            if (status == BluetoothGatt.GATT_SUCCESS) {
                gatt.discoverServices()
            }
        }

        override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) {
            if (status == BluetoothGatt.GATT_SUCCESS) {
                val service = gatt.getService(SERVICE_UUID)
                if (service != null) {
                    tileCharacteristic = service.getCharacteristic(TILE_CHARACTERISTIC_UUID)
                    tripCharacteristic = service.getCharacteristic(TRIP_CHARACTERISTIC_UUID)
                    weatherCharacteristic = service.getCharacteristic(WEATHER_CHARACTERISTIC_UUID)
                    radarCharacteristic = service.getCharacteristic(RADAR_CHARACTERISTIC_UUID)
                    notificationCharacteristic = service.getCharacteristic(NOTIFICATION_CHARACTERISTIC_UUID)
                    tripListCharacteristic = service.getCharacteristic(TRIP_LIST_CHARACTERISTIC_UUID)
                    tripControlCharacteristic = service.getCharacteristic(TRIP_CONTROL_CHARACTERISTIC_UUID)
                    navigateHomeCharacteristic = service.getCharacteristic(NAVIGATE_HOME_CHARACTERISTIC_UUID)
                    deviceStatusCharacteristic = service.getCharacteristic(DEVICE_STATUS_CHARACTERISTIC_UUID)
                    recordingListCharacteristic = service.getCharacteristic(RECORDING_LIST_CHARACTERISTIC_UUID)
                    recordingControlCharacteristic = service.getCharacteristic(RECORDING_CONTROL_CHARACTERISTIC_UUID)
                    recordingTransferCharacteristic = service.getCharacteristic(RECORDING_TRANSFER_CHARACTERISTIC_UUID)

                    if (tileCharacteristic != null && tripCharacteristic != null && weatherCharacteristic != null &&
                        radarCharacteristic != null && notificationCharacteristic != null &&
                        tripListCharacteristic != null && tripControlCharacteristic != null &&
                        navigateHomeCharacteristic != null && deviceStatusCharacteristic != null &&
                        recordingListCharacteristic != null && recordingControlCharacteristic != null &&
                        recordingTransferCharacteristic != null) {
                        // CRITICAL: Only enable one at a time! BLE allows only one GATT operation at a time.
                        // Weather notifications first, then tile, then notification, then trip list,
                        // then trip control, then recording list, then recording transfer,
                        // then navigate home characteristic in onDescriptorWrite callback
                        android.util.Log.d("BleManager", "Enabling weather notifications first...")
                        weatherNotificationsEnabled = false
                        tileNotificationsEnabled = false
                        radarNotificationsEnabled = false
                        notificationNotificationsEnabled = false
                        tripListNotificationsEnabled = false
                        tripControlNotificationsEnabled = false
                        navigateHomeNotificationsEnabled = false
                        deviceStatusNotificationsEnabled = false
                        recordingListNotificationsEnabled = false
                        recordingTransferNotificationsEnabled = false
                        enableWeatherNotifications(gatt)
                        // Connection state will be set to CONNECTED in onDescriptorWrite after all notifications are enabled
                    } else {
                        _connectionState.value = ConnectionState.ERROR("Characteristics not found")
                        gatt.disconnect()
                    }
                } else {
                    _connectionState.value = ConnectionState.ERROR("Service not found")
                    gatt.disconnect()
                }
            }
        }

        // IMPORTANT: On Android 13+ (API 33+), both the deprecated and new methods can be called.
        // To prevent duplicate processing, we track if we're already handling a notification.
        @Volatile
        private var isHandlingNotification = false

        @Deprecated("Deprecated in API 33", ReplaceWith("onCharacteristicChanged(gatt, characteristic, value)"))
        override fun onCharacteristicChanged(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic) {
            synchronized(this) {
                if (isHandlingNotification) {
                    android.util.Log.d("BleManager", "Skipping duplicate onCharacteristicChanged (deprecated method)")
                    return
                }
                isHandlingNotification = true
            }
            try {
                handleCharacteristicChanged(characteristic, characteristic.value)
            } finally {
                isHandlingNotification = false
            }
        }

        override fun onCharacteristicChanged(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic, value: ByteArray) {
            synchronized(this) {
                if (isHandlingNotification) {
                    android.util.Log.d("BleManager", "Skipping duplicate onCharacteristicChanged (new method)")
                    return
                }
                isHandlingNotification = true
            }
            try {
                handleCharacteristicChanged(characteristic, value)
            } finally {
                isHandlingNotification = false
            }
        }

        private fun handleCharacteristicChanged(characteristic: BluetoothGattCharacteristic, data: ByteArray?) {
            android.util.Log.d("BleManager", "onCharacteristicChanged: UUID=${characteristic.uuid}, dataSize=${data?.size}")

            if (characteristic.uuid == WEATHER_CHARACTERISTIC_UUID && data != null && data.size == 8) {
                val buffer = ByteBuffer.wrap(data).apply { order(ByteOrder.LITTLE_ENDIAN) }
                val lat = buffer.float.toDouble()
                val lon = buffer.float.toDouble()
                onWeatherRequest?.invoke(lat, lon)
            }
            else if (characteristic.uuid == RADAR_CHARACTERISTIC_UUID && data != null && data.size >= 8) {
                val buffer = ByteBuffer.wrap(data).apply { order(ByteOrder.LITTLE_ENDIAN) }
                val lat = buffer.float.toDouble()
                val lon = buffer.float.toDouble()
                val zoom = if (data.size >= 9) (data[8].toInt() and 0xFF) else 15
                onRadarRequest?.invoke(lat, lon, zoom)
            }
            // Handle Navigate Home request
            else if (characteristic.uuid == NAVIGATE_HOME_CHARACTERISTIC_UUID && data != null && data.size == 8) {
                val buffer = ByteBuffer.wrap(data).apply { order(ByteOrder.LITTLE_ENDIAN) }
                val lat = buffer.float.toDouble()
                val lon = buffer.float.toDouble()
                android.util.Log.d("BleManager", "Navigate Home request received: lat=$lat, lon=$lon")
                onNavigateHomeRequest?.invoke(lat, lon)
            }
            // Handle Tile ACK
            else if (characteristic.uuid == TILE_CHARACTERISTIC_UUID) {
                android.util.Log.d("BleManager", "Tile ACK notification received! Sending to channel...")
                CoroutineScope(Dispatchers.Default).launch {
                    try {
                        _tileAckChannel.trySend(true)
                        android.util.Log.d("BleManager", "Tile ACK sent to channel successfully")
                    } catch (e: Exception) {
                        android.util.Log.e("BleManager", "Error sending ACK to channel: ${e.message}")
                    }
                }
            }
            // Handle notification dismissal from ESP32
            else if (characteristic.uuid == NOTIFICATION_CHARACTERISTIC_UUID && data != null && data.size == 5) {
                try {
                    val buffer = ByteBuffer.wrap(data).apply { order(ByteOrder.BIG_ENDIAN) }
                    val action = buffer.get()
                    if (action == 0x02.toByte()) { // Dismissal action
                        val notificationId = buffer.getInt()
                        android.util.Log.d("BleManager", "Received dismissal from ESP32 for notification ID: $notificationId")
                        onNotificationDismissed?.invoke(notificationId)
                    }
                } catch (e: Exception) {
                    android.util.Log.e("BleManager", "Error parsing notification dismissal: ${e.message}")
                }
            }
            // Handle trip list from ESP32
            else if (characteristic.uuid == TRIP_LIST_CHARACTERISTIC_UUID && data != null && data.size >= 2) {
                try {
                    val buffer = ByteBuffer.wrap(data).apply { order(ByteOrder.BIG_ENDIAN) }
                    val tripCount = buffer.short.toInt()
                    android.util.Log.d("BleManager", "Received trip list from ESP32: $tripCount trips")

                    val tripNames = mutableListOf<String>()
                    for (i in 0 until tripCount) {
                        if (!buffer.hasRemaining()) break
                        val nameLen = buffer.get().toInt() and 0xFF
                        if (buffer.remaining() < nameLen) break

                        val nameBytes = ByteArray(nameLen)
                        buffer.get(nameBytes)
                        val tripName = String(nameBytes, Charsets.UTF_8)
                        tripNames.add(tripName)
                        android.util.Log.d("BleManager", "  - Trip on ESP32: $tripName")
                    }

                    android.util.Log.d("BleManager", "Parsed ${tripNames.size} trip names from ESP32")
                    onTripListReceived?.invoke(tripNames)
                } catch (e: Exception) {
                    android.util.Log.e("BleManager", "Error parsing trip list: ${e.message}", e)
                }
            }
            // Handle recording list from ESP32
            else if (characteristic.uuid == RECORDING_LIST_CHARACTERISTIC_UUID && data != null && data.size >= 2) {
                try {
                    val buffer = ByteBuffer.wrap(data).apply { order(ByteOrder.BIG_ENDIAN) }
                    val recordingCount = buffer.short.toInt()
                    android.util.Log.d("BleManager", "Received recording list from ESP32: $recordingCount recordings")

                    val recordingNames = mutableListOf<String>()
                    for (i in 0 until recordingCount) {
                        if (!buffer.hasRemaining()) break
                        val nameLen = buffer.get().toInt() and 0xFF
                        if (buffer.remaining() < nameLen) break

                        val nameBytes = ByteArray(nameLen)
                        buffer.get(nameBytes)
                        val recordingName = String(nameBytes, Charsets.UTF_8)
                        recordingNames.add(recordingName)
                        android.util.Log.d("BleManager", "  - Recording on ESP32: $recordingName")
                    }

                    android.util.Log.d("BleManager", "Parsed ${recordingNames.size} recording names from ESP32")
                    onRecordingListReceived?.invoke(recordingNames)
                } catch (e: Exception) {
                    android.util.Log.e("BleManager", "Error parsing recording list: ${e.message}", e)
                }
            }
            // Handle active trip update from ESP32
            else if (characteristic.uuid == TRIP_CONTROL_CHARACTERISTIC_UUID && data != null && data.isNotEmpty()) {
                try {
                    val action = data[0].toInt() and 0xFF
                    when (action) {
                        0x02 -> { // Active trip update
                            if (data.size >= 2) {
                                val nameLen = data[1].toInt() and 0xFF
                                if (nameLen == 0) {
                                    android.util.Log.d("BleManager", "Active trip cleared on ESP32")
                                    onActiveTripChanged?.invoke(null)
                                } else if (data.size >= 2 + nameLen) {
                                    val nameBytes = data.copyOfRange(2, 2 + nameLen)
                                    val tripName = String(nameBytes, Charsets.UTF_8)
                                    android.util.Log.d("BleManager", "Active trip changed on ESP32: $tripName")
                                    onActiveTripChanged?.invoke(tripName)
                                }
                            }
                        }
                        TILE_INV_ACTION_START -> handleTileInventoryStart(data)
                        TILE_INV_ACTION_DATA -> handleTileInventoryData(data)
                        TILE_INV_ACTION_END -> handleTileInventoryEnd()
                        TILE_INV_ACTION_ERROR -> handleTileInventoryError(data)
                    }
                } catch (e: Exception) {
                    android.util.Log.e("BleManager", "Error parsing trip control packet: ${e.message}", e)
                }
            }
            // Handle recording transfer from ESP32
            else if (characteristic.uuid == RECORDING_TRANSFER_CHARACTERISTIC_UUID && data != null && data.isNotEmpty()) {
                try {
                    handleRecordingTransferPacket(data)
                } catch (e: Exception) {
                    android.util.Log.e("BleManager", "Error handling recording transfer packet: ${e.message}", e)
                }
            }
            // Handle ESP32 device status packet (3 bytes)
            else if (characteristic.uuid == DEVICE_STATUS_CHARACTERISTIC_UUID && data != null && data.size == 3) {
                try {
                    val batteryPercent = data[0].toInt() and 0xFF
                    val gpsStage = data[1].toInt() and 0xFF
                    val satelliteCount = data[2].toInt() and 0xFF

                    val status = EspDeviceStatus(
                        batteryPercent = batteryPercent,
                        gpsStage = gpsStage,
                        satelliteCount = satelliteCount
                    )

                    android.util.Log.d("BleManager", "Received ESP32 device status: battery=$batteryPercent%, GPS stage=$gpsStage, satellites=$satelliteCount")
                    onEspDeviceStatusReceived?.invoke(status)
                } catch (e: Exception) {
                    android.util.Log.e("BleManager", "Error parsing ESP32 device status: ${e.message}", e)
                }
            }
            // Handle device status commands from ESP32 (1 byte)
            else if (characteristic.uuid == DEVICE_STATUS_CHARACTERISTIC_UUID && data != null && data.size == 1) {
                try {
                    val command = data[0]
                    android.util.Log.d("BleManager", "Received device status command: 0x${String.format("%02X", command)}")

                    when (command.toInt() and 0xFF) {
                        0x01 -> {
                            android.util.Log.d("BleManager", "Music Play/Pause command")
                            onMusicPlayPause?.invoke()
                        }
                        0x02 -> {
                            android.util.Log.d("BleManager", "Music Next command")
                            onMusicNext?.invoke()
                        }
                        0x03 -> {
                            android.util.Log.d("BleManager", "Music Previous command")
                            onMusicPrevious?.invoke()
                        }
                        0x10 -> {
                            android.util.Log.d("BleManager", "Locate Phone command")
                            onLocatePhone?.invoke()
                        }
                        0x20 -> {
                            android.util.Log.d("BleManager", "Toggle Notification Sync command")
                            onToggleNotificationSync?.invoke()
                        }
                        0x21 -> {
                            android.util.Log.d("BleManager", "Request Device Status command")
                            onDeviceStatusRequested?.invoke()
                        }
                    }
                } catch (e: Exception) {
                    android.util.Log.e("BleManager", "Error parsing device status command: ${e.message}", e)
                }
            }
        }

        override fun onCharacteristicWrite(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic, status: Int) {
            val charName = when (characteristic.uuid) {
                WEATHER_CHARACTERISTIC_UUID -> "WEATHER"
                RADAR_CHARACTERISTIC_UUID -> "RADAR"
                NOTIFICATION_CHARACTERISTIC_UUID -> "NOTIFICATION"
                TRIP_CHARACTERISTIC_UUID -> "TRIP"
                TILE_CHARACTERISTIC_UUID -> "TILE"
                RECORDING_CONTROL_CHARACTERISTIC_UUID -> "RECORDING_CONTROL"
                else -> characteristic.uuid.toString()
            }
            android.util.Log.d("BleManager", "onCharacteristicWrite: $charName, status=$status, writeCompleted=$writeCompleted -> true")
            writeCompleted = true
        }

        override fun onDescriptorWrite(gatt: BluetoothGatt, descriptor: android.bluetooth.BluetoothGattDescriptor, status: Int) {
            val charUuid = descriptor.characteristic.uuid
            if (status == BluetoothGatt.GATT_SUCCESS) {
                android.util.Log.d("BleManager", "✓ Descriptor write SUCCESS for $charUuid")

                // Track which notifications are enabled and sequence them
                when (charUuid) {
                    WEATHER_CHARACTERISTIC_UUID -> {
                        weatherNotificationsEnabled = true
                        // Now enable radar notifications
                        android.util.Log.d("BleManager", "Enabling radar notifications...")
                        enableRadarNotifications(gatt)
                    }
                    RADAR_CHARACTERISTIC_UUID -> {
                        radarNotificationsEnabled = true
                        // Now enable tile notifications
                        android.util.Log.d("BleManager", "Enabling tile notifications...")
                        enableTileNotifications(gatt)
                    }
                    TILE_CHARACTERISTIC_UUID -> {
                        tileNotificationsEnabled = true
                        // Now enable notification characteristic notifications
                        android.util.Log.d("BleManager", "Enabling notification characteristic notifications...")
                        enableNotificationNotifications(gatt)
                    }
                    NOTIFICATION_CHARACTERISTIC_UUID -> {
                        notificationNotificationsEnabled = true
                        // Now enable trip list notifications
                        android.util.Log.d("BleManager", "Enabling trip list notifications...")
                        enableTripListNotifications(gatt)
                    }
                    TRIP_LIST_CHARACTERISTIC_UUID -> {
                        tripListNotificationsEnabled = true
                        // Now enable trip control notifications
                        android.util.Log.d("BleManager", "Enabling trip control notifications...")
                        enableTripControlNotifications(gatt)
                    }
                    TRIP_CONTROL_CHARACTERISTIC_UUID -> {
                        tripControlNotificationsEnabled = true
                        // Now enable recording list notifications
                        android.util.Log.d("BleManager", "Enabling recording list notifications...")
                        enableRecordingListNotifications(gatt)
                    }
                    RECORDING_LIST_CHARACTERISTIC_UUID -> {
                        recordingListNotificationsEnabled = true
                        // Now enable recording transfer notifications
                        android.util.Log.d("BleManager", "Enabling recording transfer notifications...")
                        enableRecordingTransferNotifications(gatt)
                    }
                    RECORDING_TRANSFER_CHARACTERISTIC_UUID -> {
                        recordingTransferNotificationsEnabled = true
                        // Now enable navigate home notifications
                        android.util.Log.d("BleManager", "Enabling navigate home notifications...")
                        enableNavigateHomeNotifications(gatt)
                    }
                    NAVIGATE_HOME_CHARACTERISTIC_UUID -> {
                        navigateHomeNotificationsEnabled = true
                        // Now enable device status notifications (last one)
                        android.util.Log.d("BleManager", "Enabling device status notifications...")
                        enableDeviceStatusNotifications(gatt)
                    }
                    DEVICE_STATUS_CHARACTERISTIC_UUID -> {
                        deviceStatusNotificationsEnabled = true
                        android.util.Log.d("BleManager", "✓ All notifications enabled!")
                        // Now we're truly connected
                        _connectionState.value = ConnectionState.CONNECTED

                        // Send ready signal to ESP32 to trigger immediate sync
                        CoroutineScope(Dispatchers.IO).launch {
                            delay(50)  // Small delay to ensure connection state is fully updated
                            sendClientReadySignal()
                        }
                    }
                }
            } else {
                android.util.Log.e("BleManager", "✗ Descriptor write FAILED for $charUuid, status=$status")
                _connectionState.value = ConnectionState.ERROR("Failed to enable notifications")
            }
        }
    }

    private fun handleTileInventoryStart(data: ByteArray) {
        val collector = tileInventoryCollector ?: return
        collector.tiles.clear()
        collector.started = true
        if (data.size >= 5) {
            val totalRecords = readIntBE(data, 1)
            android.util.Log.d("BleManager", "Tile inventory start: $totalRecords records")
        } else {
            android.util.Log.d("BleManager", "Tile inventory start")
        }
    }

    private fun handleTileInventoryData(data: ByteArray) {
        val collector = tileInventoryCollector ?: return
        if (!collector.started) collector.started = true

        var index = 1
        while (index + TILE_INV_RECORD_SIZE - 1 < data.size) {
            val zoom = data[index].toInt() and 0xFF
            val tileX = readIntBE(data, index + 1)
            val tileY = readIntBE(data, index + 5)
            collector.tiles.add(packTileKey(zoom, tileX, tileY))
            index += TILE_INV_RECORD_SIZE
        }
    }

    private fun handleTileInventoryEnd() {
        val collector = tileInventoryCollector ?: return
        collector.deferred.complete(collector.tiles)
        tileInventoryCollector = null
    }

    private fun handleTileInventoryError(data: ByteArray) {
        val collector = tileInventoryCollector ?: return
        val errorCode = if (data.size > 1) data[1].toInt() and 0xFF else 0
        collector.deferred.completeExceptionally(RuntimeException("Tile inventory error: $errorCode"))
        tileInventoryCollector = null
    }

    private fun handleRecordingTransferPacket(data: ByteArray) {
        val action = data[0].toInt() and 0xFF
        when (action) {
            RECORDING_TRANSFER_ACTION_START -> {
                if (data.size < 2 + 8) {
                    android.util.Log.e("BleManager", "Recording transfer start packet too short")
                    return
                }
                val nameLen = data[1].toInt() and 0xFF
                val headerSize = 2 + nameLen + 8
                if (data.size < headerSize) {
                    android.util.Log.e("BleManager", "Recording transfer start packet missing header data")
                    return
                }
                val nameBytes = data.copyOfRange(2, 2 + nameLen)
                val recordingName = String(nameBytes, Charsets.UTF_8)
                val metaSize = readIntBE(data, 2 + nameLen)
                val gpxSize = readIntBE(data, 2 + nameLen + 4)
                val totalSize = metaSize + gpxSize
                android.util.Log.d("BleManager", "Recording transfer start: $recordingName meta=$metaSize gpx=$gpxSize")

                recordingTransferState = RecordingTransferState(
                    recordingName = recordingName,
                    metadataSize = metaSize,
                    gpxSize = gpxSize,
                    buffer = ByteArrayOutputStream(totalSize)
                )
                onRecordingTransferProgress?.invoke(recordingName, 0, totalSize)
            }
            RECORDING_TRANSFER_ACTION_DATA -> {
                val state = recordingTransferState ?: return
                if (data.size <= 1) return
                val payload = data.copyOfRange(1, data.size)
                state.buffer.write(payload)
                state.receivedBytes += payload.size
                val totalSize = state.metadataSize + state.gpxSize
                onRecordingTransferProgress?.invoke(state.recordingName, state.receivedBytes, totalSize)
            }
            RECORDING_TRANSFER_ACTION_END -> {
                val state = recordingTransferState ?: return
                val totalSize = state.metadataSize + state.gpxSize
                val bytes = state.buffer.toByteArray()
                if (bytes.size < totalSize) {
                    android.util.Log.w("BleManager", "Recording transfer incomplete: ${bytes.size}/$totalSize bytes")
                }
                val metaEnd = state.metadataSize.coerceAtMost(bytes.size)
                val gpxEnd = (state.metadataSize + state.gpxSize).coerceAtMost(bytes.size)
                val metaBytes = bytes.copyOfRange(0, metaEnd)
                val gpxBytes = if (gpxEnd > metaEnd) bytes.copyOfRange(metaEnd, gpxEnd) else ByteArray(0)
                val metaJson = metaBytes.toString(Charsets.UTF_8)
                val gpxContent = gpxBytes.toString(Charsets.UTF_8)
                onRecordingTransferCompleted?.invoke(state.recordingName, metaJson, gpxContent)
                recordingTransferState = null
            }
            RECORDING_TRANSFER_ACTION_ERROR -> {
                val state = recordingTransferState
                val msgLen = if (data.size >= 2) data[1].toInt() and 0xFF else 0
                val msg = if (msgLen > 0 && data.size >= 2 + msgLen) {
                    String(data.copyOfRange(2, 2 + msgLen), Charsets.UTF_8)
                } else {
                    "Recording transfer failed"
                }
                onRecordingTransferError?.invoke(state?.recordingName, msg)
                recordingTransferState = null
            }
        }
    }

    private val scanCallback = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            val device = result.device
            val record = result.scanRecord
            val matchesService = record?.serviceUuids?.any { it.uuid == SERVICE_UUID } == true
            val matchesName = device.name == DEVICE_NAME || record?.deviceName == DEVICE_NAME
            if (matchesService || matchesName) {
                stopScan()
                connectToDevice(device)
            }
        }

        override fun onScanFailed(errorCode: Int) {
            if (errorCode == ScanCallback.SCAN_FAILED_ALREADY_STARTED) {
                _connectionState.value = ConnectionState.SCANNING
                return
            }
            _connectionState.value = ConnectionState.ERROR("Scan failed: $errorCode")
        }
    }

    fun isBluetoothEnabled(): Boolean {
        return bluetoothAdapter?.isEnabled == true
    }

    fun startScan() {
        if (bluetoothAdapter == null) {
            _connectionState.value = ConnectionState.ERROR("Bluetooth not available")
            return
        }

        if (!bluetoothAdapter.isEnabled) {
            _connectionState.value = ConnectionState.BLUETOOTH_DISABLED
            return
        }

        _connectionState.value = ConnectionState.SCANNING

        val scanner = bluetoothAdapter.bluetoothLeScanner
        val filters = listOf(
            ScanFilter.Builder()
                .setServiceUuid(ParcelUuid(SERVICE_UUID))
                .build()
        )

        val settings = ScanSettings.Builder()
            .setScanMode(ScanSettings.SCAN_MODE_BALANCED)
            .build()

        stopScan()
        scanner?.startScan(filters, settings, scanCallback)
    }

    fun stopScan() {
        bluetoothAdapter?.bluetoothLeScanner?.stopScan(scanCallback)
    }

    private fun connectToDevice(device: BluetoothDevice) {
        _connectionState.value = ConnectionState.CONNECTING
        bluetoothGatt = device.connectGatt(context, false, gattCallback)
    }

    fun disconnect() {
        bluetoothGatt?.disconnect()
        bluetoothGatt?.close()
        bluetoothGatt = null
        recordingTransferState = null
        _connectionState.value = ConnectionState.DISCONNECTED
    }

    private fun enableTileNotifications(gatt: BluetoothGatt) {
        tileCharacteristic?.let { characteristic ->
            gatt.setCharacteristicNotification(characteristic, true)
            val descriptor = characteristic.getDescriptor(
                UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
            )
            descriptor?.let {
                it.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
                gatt.writeDescriptor(it)
            }
        }
    }

    private fun enableWeatherNotifications(gatt: BluetoothGatt) {
        weatherCharacteristic?.let { characteristic ->
            gatt.setCharacteristicNotification(characteristic, true)
            val descriptor = characteristic.getDescriptor(
                UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
            )
            descriptor?.let {
                it.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
                gatt.writeDescriptor(it)
            }
        }
    }

    private fun enableRadarNotifications(gatt: BluetoothGatt) {
        radarCharacteristic?.let { characteristic ->
            gatt.setCharacteristicNotification(characteristic, true)
            val descriptor = characteristic.getDescriptor(
                UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
            )
            descriptor?.let {
                it.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
                gatt.writeDescriptor(it)
            }
        }
    }

    private fun enableNotificationNotifications(gatt: BluetoothGatt) {
        notificationCharacteristic?.let { characteristic ->
            gatt.setCharacteristicNotification(characteristic, true)
            val descriptor = characteristic.getDescriptor(
                UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
            )
            descriptor?.let {
                it.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
                gatt.writeDescriptor(it)
            }
        }
    }

    private fun enableTripListNotifications(gatt: BluetoothGatt) {
        tripListCharacteristic?.let { characteristic ->
            gatt.setCharacteristicNotification(characteristic, true)
            val descriptor = characteristic.getDescriptor(
                UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
            )
            descriptor?.let {
                it.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
                gatt.writeDescriptor(it)
            }
        }
    }

    private fun enableTripControlNotifications(gatt: BluetoothGatt) {
        tripControlCharacteristic?.let { characteristic ->
            gatt.setCharacteristicNotification(characteristic, true)
            val descriptor = characteristic.getDescriptor(
                UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
            )
            descriptor?.let {
                it.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
                gatt.writeDescriptor(it)
            }
        }
    }

    private fun enableRecordingListNotifications(gatt: BluetoothGatt) {
        recordingListCharacteristic?.let { characteristic ->
            gatt.setCharacteristicNotification(characteristic, true)
            val descriptor = characteristic.getDescriptor(
                UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
            )
            descriptor?.let {
                it.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
                gatt.writeDescriptor(it)
            }
        }
    }

    private fun enableRecordingTransferNotifications(gatt: BluetoothGatt) {
        recordingTransferCharacteristic?.let { characteristic ->
            gatt.setCharacteristicNotification(characteristic, true)
            val descriptor = characteristic.getDescriptor(
                UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
            )
            descriptor?.let {
                it.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
                gatt.writeDescriptor(it)
            }
        }
    }

    private fun enableNavigateHomeNotifications(gatt: BluetoothGatt) {
        navigateHomeCharacteristic?.let { characteristic ->
            gatt.setCharacteristicNotification(characteristic, true)
            val descriptor = characteristic.getDescriptor(
                UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
            )
            descriptor?.let {
                it.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
                gatt.writeDescriptor(it)
            }
        }
    }

    private fun enableDeviceStatusNotifications(gatt: BluetoothGatt) {
        deviceStatusCharacteristic?.let { characteristic ->
            gatt.setCharacteristicNotification(characteristic, true)
            val descriptor = characteristic.getDescriptor(
                UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
            )
            descriptor?.let {
                it.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
                gatt.writeDescriptor(it)
            }
        }
    }

    /**
     * Internal helper to send data chunks.
     * Returns true when chunks are sent, but does NOT wait for SD card ACK.
     */
    private suspend fun sendTileChunks(zoom: Int, tileX: Int, tileY: Int, tileData: ByteArray): Boolean {
        return withContext(Dispatchers.IO) {
            if (_connectionState.value != ConnectionState.CONNECTED || tileCharacteristic == null) {
                return@withContext false
            }

            try {
                val chunkStartTime = System.currentTimeMillis()
                val compressedData = compressRLE(tileData)
                val useCompression = compressedData.size < tileData.size * 0.9 && compressedData.size <= 16384
                val dataToSend = if (useCompression) compressedData else tileData

                if (dataToSend.size > 16384) {
                    return@withContext false
                }

                val header = ByteBuffer.allocate(14).apply {
                    order(ByteOrder.BIG_ENDIAN)
                    put(if (useCompression) 0x01 else 0x00)
                    put(zoom.toByte())
                    putInt(tileX)
                    putInt(tileY)
                    putInt(dataToSend.size)
                }.array()

                val fullData = header + dataToSend
                var offset = 0
                val numChunks = (fullData.size + CHUNK_SIZE - 1) / CHUNK_SIZE

                android.util.Log.d("BleManager", "Sending tile $zoom/$tileX/$tileY: ${tileData.size} -> ${dataToSend.size} bytes (${if(useCompression) "compressed" else "raw"}), $numChunks chunks")

                while (offset < fullData.size) {
                    val chunkSize = minOf(CHUNK_SIZE, fullData.size - offset)
                    val chunk = fullData.copyOfRange(offset, offset + chunkSize)

                    var retries = 0
                    while (!writeCompleted && retries < 100) {
                        delay(10)
                        retries++
                    }

                    if (!writeCompleted) return@withContext false

                    writeCompleted = false
                    tileCharacteristic?.value = chunk
                    val success = bluetoothGatt?.writeCharacteristic(tileCharacteristic) ?: false

                    if (!success) return@withContext false

                    offset += chunkSize
                    // No delay needed - write callback provides flow control
                }

                // Wait for final write callback for the last chunk
                var retries = 0
                while (!writeCompleted && retries < 100) {
                    delay(10)
                    retries++
                }

                val chunkTime = System.currentTimeMillis() - chunkStartTime
                android.util.Log.d("BleManager", "Chunks sent for tile $zoom/$tileX/$tileY in ${chunkTime}ms (waiting for ACK...)")

                writeCompleted
            } catch (e: Exception) {
                android.util.Log.e("BleManager", "Error sending tile chunks: ${e.message}")
                e.printStackTrace()
                false
            }
        }
    }

    /**
     * Send multiple tiles with FLOW CONTROL.
     * onProgress is now optional (nullable) for backward compatibility.
     */
    suspend fun sendTiles(
        tiles: List<Triple<Int, Int, Int>>,
        storageManager: StorageManager,
        tilePreprocessor: TilePreprocessor,
        onProgress: ((Int, Int) -> Unit)? = null
    ): Int {
        var successCount = 0
        val total = tiles.size

        tiles.forEachIndexed { index, (zoom, tileX, tileY) ->
            // Report progress immediately for every tile (UI responsiveness)
            onProgress?.invoke(index + 1, total)

            // Load raw PNG and process on-the-fly
            val rawTileData = storageManager.loadRawTile(zoom, tileX, tileY)
            if (rawTileData != null) {
                val tileData = tilePreprocessor.preprocessTile(rawTileData)
                if (tileData != null) {

                // === CRITICAL FIX: DRAIN STALE ACKS ===
                // Ensure the channel is empty before we start sending this tile.
                // If we don't do this, a leftover ACK from a previous tile (or a bounce)
                // will cause us to send the NEXT tile immediately, breaking sync.
                while (_tileAckChannel.tryReceive().isSuccess) {
                    android.util.Log.w("BleManager", "Drained stale ACK before sending tile $zoom/$tileX/$tileY")
                }

                val chunksSent = sendTileChunks(zoom, tileX, tileY, tileData)

                if (chunksSent) {
                    val ackStartTime = System.currentTimeMillis()
                    try {
                        android.util.Log.d("BleManager", "Waiting for ACK on tile $zoom/$tileX/$tileY...")

                        // Now we wait for the NEW ack
                        withTimeout(3000) {
                            _tileAckChannel.receive()
                        }

                        val ackTime = System.currentTimeMillis() - ackStartTime
                        android.util.Log.d("BleManager", "✓ ACK received for tile $zoom/$tileX/$tileY in ${ackTime}ms")
                        successCount++
                    } catch (e: TimeoutCancellationException) {
                        val timeoutTime = System.currentTimeMillis() - ackStartTime
                        android.util.Log.e("BleManager", "✗ TIMEOUT waiting for ACK on tile $zoom/$tileX/$tileY after ${timeoutTime}ms")
                    }
                }
                }
            }
        }

        return successCount
    }

    private suspend fun sendTripControlPacket(dataToSend: ByteArray): Boolean {
        return withContext(Dispatchers.IO) {
            if (_connectionState.value != ConnectionState.CONNECTED || tripControlCharacteristic == null) {
                return@withContext false
            }

            var retries = 0
            while (!writeCompleted && retries < 100) {
                delay(10)
                retries++
            }

            if (!writeCompleted) return@withContext false

            writeCompleted = false
            tripControlCharacteristic?.value = dataToSend
            val success = bluetoothGatt?.writeCharacteristic(tripControlCharacteristic) ?: false

            if (success) {
                retries = 0
                while (!writeCompleted && retries < 100) {
                    delay(10)
                    retries++
                }
            }

            success && writeCompleted
        }
    }

    private suspend fun sendRecordingControlPacket(dataToSend: ByteArray): Boolean {
        return withContext(Dispatchers.IO) {
            if (_connectionState.value != ConnectionState.CONNECTED || recordingControlCharacteristic == null) {
                return@withContext false
            }

            var retries = 0
            while (!writeCompleted && retries < 100) {
                delay(10)
                retries++
            }

            if (!writeCompleted) return@withContext false

            writeCompleted = false
            recordingControlCharacteristic?.value = dataToSend
            val success = bluetoothGatt?.writeCharacteristic(recordingControlCharacteristic) ?: false

            if (success) {
                retries = 0
                while (!writeCompleted && retries < 100) {
                    delay(10)
                    retries++
                }
            }

            success && writeCompleted
        }
    }

    private suspend fun requestTileInventoryKeys(timeoutMs: Long = TILE_INV_TIMEOUT_MS): Set<Long>? {
        if (_connectionState.value != ConnectionState.CONNECTED || tripControlCharacteristic == null) {
            return null
        }

        return tileInventoryRequestMutex.withLock {
            if (tileInventoryCollector != null) {
                android.util.Log.w("BleManager", "Tile inventory request already in progress")
                return@withLock null
            }

            val deferred = CompletableDeferred<Set<Long>>()
            val collector = TileInventoryCollector(deferred, HashSet())
            tileInventoryCollector = collector

            val requestPacket = byteArrayOf(TILE_INV_ACTION_REQUEST.toByte())
            val sent = sendTripControlPacket(requestPacket)
            if (!sent) {
                tileInventoryCollector = null
                return@withLock null
            }

            try {
                withTimeout(timeoutMs) {
                    deferred.await()
                }
            } catch (e: Exception) {
                android.util.Log.w("BleManager", "Tile inventory request failed: ${e.message}")
                null
            } finally {
                if (tileInventoryCollector == collector) {
                    tileInventoryCollector = null
                }
            }
        }
    }

    suspend fun filterMissingTiles(
        tiles: List<Triple<Int, Int, Int>>
    ): List<Triple<Int, Int, Int>> {
        if (tiles.isEmpty()) return tiles

        val inventory = requestTileInventoryKeys()
        if (inventory == null) {
            android.util.Log.w("BleManager", "Tile inventory unavailable, sending all tiles")
            return tiles
        }

        val missing = tiles.filterNot { inventory.contains(packTileKey(it)) }
        android.util.Log.d("BleManager", "Tile inventory: ${inventory.size} existing, ${missing.size}/${tiles.size} missing")
        return missing
    }

    /**
     * Send trip with progress callback.
     */
    suspend fun sendTripWithProgress(
        fileName: String,
        gpxContent: String,
        metadataJson: String,
        onProgress: (bytesSent: Long, totalBytes: Long) -> Unit
    ): Boolean {
        return withContext(Dispatchers.IO) {
            if (_connectionState.value != ConnectionState.CONNECTED || tripCharacteristic == null) {
                return@withContext false
            }

            try {
                val nameBytes = fileName.toByteArray(Charsets.UTF_8)
                val gpxBytes = gpxContent.toByteArray(Charsets.UTF_8)
                val metaBytes = metadataJson.toByteArray(Charsets.UTF_8)

                val header = ByteBuffer.allocate(10).apply {
                    order(ByteOrder.BIG_ENDIAN)
                    putShort(nameBytes.size.toShort())
                    putInt(gpxBytes.size)
                    putInt(metaBytes.size)
                }.array()

                val fullData = header + nameBytes + gpxBytes + metaBytes
                val totalBytes = fullData.size.toLong()

                var offset = 0
                while (offset < fullData.size) {
                    val chunkSize = minOf(CHUNK_SIZE, fullData.size - offset)
                    val chunk = fullData.copyOfRange(offset, offset + chunkSize)

                    var retries = 0
                    while (!writeCompleted && retries < 100) {
                        delay(10)
                        retries++
                    }

                    if (!writeCompleted) return@withContext false

                    writeCompleted = false
                    tripCharacteristic?.value = chunk
                    bluetoothGatt?.writeCharacteristic(tripCharacteristic)

                    offset += chunkSize
                    onProgress(offset.toLong(), totalBytes)
                    delay(20)
                }

                var retries = 0
                while (!writeCompleted && retries < 100) {
                    delay(10)
                    retries++
                }

                writeCompleted
            } catch (e: Exception) {
                e.printStackTrace()
                false
            }
        }
    }

    /**
     * Simple wrapper for backward compatibility with MainViewModel.
     */
    suspend fun sendTrip(fileName: String, gpxContent: String, metadataJson: String): Boolean {
        return sendTripWithProgress(fileName, gpxContent, metadataJson) { _, _ -> }
    }

    /**
     * Send weather error.
     */
    suspend fun sendWeatherError(errorMessage: String): Boolean {
        return withContext(Dispatchers.IO) {
            if (_connectionState.value != ConnectionState.CONNECTED || weatherCharacteristic == null) return@withContext false
            try {
                val buffer = ByteBuffer.allocate(256).apply {
                    order(ByteOrder.LITTLE_ENDIAN)
                    put(1.toByte()) // hasError
                    val errorBytes = errorMessage.take(63).toByteArray(Charsets.UTF_8)
                    put(errorBytes)
                    repeat(64 - errorBytes.size) { put(0.toByte()) }
                    repeat(256 - 65) { put(0.toByte()) }
                }
                val dataToSend = buffer.array().copyOfRange(0, WEATHER_PACKET_SIZE)
                writeCompleted = false
                weatherCharacteristic?.value = dataToSend
                val res = bluetoothGatt?.writeCharacteristic(weatherCharacteristic) ?: false
                if (res) {
                    var retries = 0
                    while (!writeCompleted && retries < 100) { delay(10); retries++ }
                }
                res
            } catch (e: Exception) {
                false
            }
        }
    }

    /**
     * Send Navigate Home error to ESP32.
     * Sends an error message back via the Navigate Home characteristic.
     * Format: 1 byte (0xFF = error flag) + error message string (max 63 bytes)
     */
    suspend fun sendNavigateHomeError(errorMessage: String): Boolean {
        return withContext(Dispatchers.IO) {
            if (_connectionState.value != ConnectionState.CONNECTED || navigateHomeCharacteristic == null) {
                return@withContext false
            }

            try {
                android.util.Log.d("BleManager", "Sending Navigate Home error: $errorMessage")

                val buffer = ByteBuffer.allocate(64).apply {
                    order(ByteOrder.LITTLE_ENDIAN)
                    put(0xFF.toByte()) // Error flag (different from GPS coords which are floats)

                    // Error message string (max 63 bytes)
                    val errorBytes = errorMessage.take(63).toByteArray(Charsets.UTF_8)
                    put(errorBytes)

                    // Pad remaining bytes with zeros
                    repeat(63 - errorBytes.size) { put(0.toByte()) }
                }

                val dataToSend = buffer.array()

                // Wait for previous write to complete
                var retries = 0
                while (!writeCompleted && retries < 100) {
                    delay(10)
                    retries++
                }

                if (!writeCompleted) return@withContext false

                writeCompleted = false
                navigateHomeCharacteristic?.value = dataToSend
                val success = bluetoothGatt?.writeCharacteristic(navigateHomeCharacteristic) ?: false

                if (success) {
                    retries = 0
                    while (!writeCompleted && retries < 100) {
                        delay(10)
                        retries++
                    }
                }

                android.util.Log.d("BleManager", "Navigate Home error sent: $success")
                success && writeCompleted
            } catch (e: Exception) {
                android.util.Log.e("BleManager", "Error sending Navigate Home error: ${e.message}")
                false
            }
        }
    }

    /**
     * Send weather data.
     */
    suspend fun sendWeather(
        location: String, currentTemp: Float, feelsLike: Float, condition: Int,
        humidity: Int, windSpeed: Float, windDir: Int, pressure: Int, precipChance: Int,
        sunrise: Long, sunset: Long, hourlyData: List<HourlyWeatherData>
    ): Boolean {
        return withContext(Dispatchers.IO) {
            if (_connectionState.value != ConnectionState.CONNECTED || weatherCharacteristic == null) return@withContext false
            try {
                val buffer = ByteBuffer.allocate(256).apply {
                    order(ByteOrder.LITTLE_ENDIAN)
                    put(0.toByte()) // hasError
                    repeat(64) { put(0.toByte()) }
                    val locationBytes = location.take(31).toByteArray(Charsets.UTF_8)
                    put(locationBytes)
                    repeat(32 - locationBytes.size) { put(0.toByte()) }
                    putShort((currentTemp * 10).toInt().toShort())
                    putShort((feelsLike * 10).toInt().toShort())
                    put(condition.toByte())
                    put(humidity.toByte())
                    putShort((windSpeed * 10).toInt().toShort())
                    putShort(windDir.toShort())
                    putShort(pressure.toShort())
                    put(precipChance.toByte())
                    putInt(sunrise.toInt())
                    putInt(sunset.toInt())

                    // CRITICAL: ESP32 expects EXACTLY 6 hourly entries (fixed size 149 bytes)
                    // Always send count=6, pad with zeros if needed
                    val hoursToSend = hourlyData.take(6)
                    put(6.toByte())  // Always 6, even if we have fewer

                    // Send actual hourly data
                    hoursToSend.forEach { hourly ->
                        put(hourly.hour.toByte())
                        putShort((hourly.temp * 10).toInt().toShort())
                        put(hourly.condition.toByte())
                        put(hourly.precipChance.toByte())
                    }

                    // Pad remaining slots with zeros to always reach 6 entries
                    val paddingCount = 6 - hoursToSend.size
                    repeat(paddingCount) {
                        put(0.toByte()) // hour
                        putShort(0.toShort()) // temp
                        put(0.toByte()) // condition
                        put(0.toByte()) // precipChance
                    }
                }

                val dataToSend = buffer.array().copyOfRange(0, buffer.position())
                android.util.Log.d("BleManager", "sendWeather: Prepared packet of ${dataToSend.size} bytes")

                var offset = 0
                while (offset < dataToSend.size) {
                    val chunkSize = minOf(CHUNK_SIZE, dataToSend.size - offset)
                    val chunk = dataToSend.copyOfRange(offset, offset + chunkSize)

                    var retries = 0
                    while (!writeCompleted && retries < 100) { delay(10); retries++ }
                    if (!writeCompleted) {
                        android.util.Log.e("BleManager", "sendWeather: Previous write not completed before chunk at offset $offset")
                        return@withContext false
                    }

                    writeCompleted = false
                    weatherCharacteristic?.value = chunk
                    val success = bluetoothGatt?.writeCharacteristic(weatherCharacteristic) ?: false

                    if (!success) {
                        android.util.Log.e("BleManager", "sendWeather: writeCharacteristic() returned false at offset $offset")
                        return@withContext false
                    }

                    android.util.Log.d("BleManager", "sendWeather: Chunk written at offset $offset, size $chunkSize")
                    offset += chunkSize
                    delay(20)
                }

                android.util.Log.d("BleManager", "sendWeather: All chunks sent, waiting for final callback...")
                var retries = 0
                val startWait = System.currentTimeMillis()
                while (!writeCompleted && retries < 100) { delay(10); retries++ }
                val waitTime = System.currentTimeMillis() - startWait

                if (!writeCompleted) {
                    android.util.Log.e("BleManager", "sendWeather: Callback timeout after ${waitTime}ms, retries=$retries")
                } else {
                    android.util.Log.d("BleManager", "sendWeather: Success! Callback received after ${waitTime}ms")
                }

                writeCompleted
            } catch (e: Exception) {
                false
            }
        }
    }

    /**
     * Send radar overlay (1-bit packed, white=1, black=0).
     */
    suspend fun sendRadar(overlayData: ByteArray): Boolean {
        return sendRadarFrame(0, RADAR_DEFAULT_STEP_MINUTES, 1, overlayData, null, null)
    }

    /**
     * Send a radar frame with timeline metadata.
     */
    suspend fun sendRadarFrame(
        offsetSteps: Int,
        stepMinutes: Int,
        totalFrames: Int,
        overlayData: ByteArray,
        frameLocalMinutes: Int?,
        nowcastStepMinutes: Int?
    ): Boolean {
        return withContext(Dispatchers.IO) {
            if (_connectionState.value != ConnectionState.CONNECTED || radarCharacteristic == null) return@withContext false
            if (overlayData.size != RADAR_IMAGE_BYTES) {
                android.util.Log.e("BleManager", "sendRadarFrame: Invalid overlay size ${overlayData.size}, expected $RADAR_IMAGE_BYTES")
                return@withContext false
            }

            try {
                val buffer = ByteBuffer.allocate(RADAR_PACKET_SIZE).apply {
                    order(ByteOrder.LITTLE_ENDIAN)
                    put(0.toByte()) // hasError
                    put(offsetSteps.toByte())
                    put(stepMinutes.coerceIn(1, 255).toByte())
                    put(totalFrames.coerceIn(1, 255).toByte())
                    val baseMinutesValue = frameLocalMinutes?.takeIf { it in 0..1439 } ?: 0xFFFF
                    val nowcastStepValue = nowcastStepMinutes?.coerceIn(1, 255) ?: 0
                    putShort(baseMinutesValue.toShort())
                    put(RADAR_BASE_TIME_MAGIC)
                    put(nowcastStepValue.toByte())
                    repeat(RADAR_ERROR_MESSAGE_SIZE - 4) { put(0.toByte()) }
                    put(overlayData)
                }

                val dataToSend = buffer.array()

                var offset = 0
                while (offset < dataToSend.size) {
                    val chunkSize = minOf(CHUNK_SIZE, dataToSend.size - offset)
                    val chunk = dataToSend.copyOfRange(offset, offset + chunkSize)

                    var retries = 0
                    while (!writeCompleted && retries < 100) { delay(10); retries++ }
                    if (!writeCompleted) return@withContext false

                    writeCompleted = false
                    radarCharacteristic?.value = chunk
                    val success = bluetoothGatt?.writeCharacteristic(radarCharacteristic) ?: false
                    if (!success) return@withContext false

                    offset += chunkSize
                    delay(20)
                }

                var retries = 0
                while (!writeCompleted && retries < 100) { delay(10); retries++ }
                writeCompleted
            } catch (e: Exception) {
                android.util.Log.e("BleManager", "sendRadarFrame: Error ${e.message}")
                false
            }
        }
    }

    /**
     * Send radar error.
     */
    suspend fun sendRadarError(errorMessage: String, stepMinutes: Int, totalFrames: Int): Boolean {
        return withContext(Dispatchers.IO) {
            if (_connectionState.value != ConnectionState.CONNECTED || radarCharacteristic == null) return@withContext false
            try {
                val buffer = ByteBuffer.allocate(RADAR_PACKET_SIZE).apply {
                    order(ByteOrder.LITTLE_ENDIAN)
                    put(1.toByte()) // hasError
                    put(0.toByte()) // frameOffsetSteps
                    put(stepMinutes.coerceIn(1, 255).toByte())
                    put(totalFrames.coerceIn(1, 255).toByte())
                    val errorBytes = errorMessage.take(RADAR_ERROR_MESSAGE_SIZE - 1).toByteArray(Charsets.UTF_8)
                    put(errorBytes)
                    repeat(RADAR_ERROR_MESSAGE_SIZE - errorBytes.size) { put(0.toByte()) }
                    repeat(RADAR_IMAGE_BYTES) { put(0.toByte()) }
                }
                val dataToSend = buffer.array()

                var offset = 0
                while (offset < dataToSend.size) {
                    val chunkSize = minOf(CHUNK_SIZE, dataToSend.size - offset)
                    val chunk = dataToSend.copyOfRange(offset, offset + chunkSize)

                    var retries = 0
                    while (!writeCompleted && retries < 100) { delay(10); retries++ }
                    if (!writeCompleted) return@withContext false

                    writeCompleted = false
                    radarCharacteristic?.value = chunk
                    val success = bluetoothGatt?.writeCharacteristic(radarCharacteristic) ?: false
                    if (!success) return@withContext false

                    offset += chunkSize
                    delay(20)
                }

                var retries = 0
                while (!writeCompleted && retries < 100) { delay(10); retries++ }
                writeCompleted
            } catch (e: Exception) {
                android.util.Log.e("BleManager", "sendRadarError: Error ${e.message}")
                false
            }
        }
    }

    data class HourlyWeatherData(
        val hour: Int, val temp: Float, val condition: Int, val precipChance: Int
    )

    /**
     * Send notification to ESP32.
     */
    suspend fun sendNotification(
        notificationId: Int,
        appName: String,
        title: String,
        text: String,
        iconData: ByteArray? = null
    ): Boolean {
        return withContext(Dispatchers.IO) {
            if (_connectionState.value != ConnectionState.CONNECTED || notificationCharacteristic == null) {
                return@withContext false
            }

            try {
                // Packet size: 425 bytes (229 base + 1 flag + 195 icon data)
                val buffer = ByteBuffer.allocate(425).apply {
                    order(ByteOrder.BIG_ENDIAN)

                    // Action: 0x01 = Add notification
                    put(0x01.toByte())

                    // Notification ID (4 bytes)
                    putInt(notificationId)

                    // App name (32 bytes, null-terminated)
                    val appNameBytes = appName.take(31).toByteArray(Charsets.UTF_8)
                    put(appNameBytes)
                    repeat(32 - appNameBytes.size) { put(0.toByte()) }

                    // Title (64 bytes, null-terminated)
                    val titleBytes = title.take(63).toByteArray(Charsets.UTF_8)
                    put(titleBytes)
                    repeat(64 - titleBytes.size) { put(0.toByte()) }

                    // Text (128 bytes, null-terminated)
                    val textBytes = text.take(127).toByteArray(Charsets.UTF_8)
                    put(textBytes)
                    repeat(128 - textBytes.size) { put(0.toByte()) }

                    // Icon present flag (1 byte)
                    if (iconData != null && iconData.size == 195) {
                        put(0x01.toByte()) // Icon present
                        put(iconData) // Icon bitmap data (195 bytes)
                    } else {
                        put(0x00.toByte()) // No icon
                        repeat(195) { put(0.toByte()) } // Zero padding
                    }
                }

                val dataToSend = buffer.array()

                android.util.Log.d("BleManager", "Sending notification: $appName - $title" +
                                  if (iconData != null) " (with icon)" else " (no icon)")

                // Wait for previous write to complete
                var retries = 0
                while (!writeCompleted && retries < 100) {
                    delay(10)
                    retries++
                }

                if (!writeCompleted) return@withContext false

                writeCompleted = false
                notificationCharacteristic?.value = dataToSend
                val success = bluetoothGatt?.writeCharacteristic(notificationCharacteristic) ?: false

                if (success) {
                    retries = 0
                    while (!writeCompleted && retries < 100) {
                        delay(10)
                        retries++
                    }
                }

                success && writeCompleted
            } catch (e: Exception) {
                android.util.Log.e("BleManager", "Error sending notification: ${e.message}")
                false
            }
        }
    }

    /**
     * Send notification dismissal to ESP32.
     */
    suspend fun sendNotificationDismissal(notificationId: Int): Boolean {
        return withContext(Dispatchers.IO) {
            if (_connectionState.value != ConnectionState.CONNECTED || notificationCharacteristic == null) {
                return@withContext false
            }

            try {
                val buffer = ByteBuffer.allocate(5).apply {
                    order(ByteOrder.BIG_ENDIAN)

                    // Action: 0x02 = Remove notification
                    put(0x02.toByte())

                    // Notification ID
                    putInt(notificationId)
                }

                val dataToSend = buffer.array()

                android.util.Log.d("BleManager", "Sending dismissal for notification ID: $notificationId")

                // Wait for previous write to complete
                var retries = 0
                while (!writeCompleted && retries < 100) {
                    delay(10)
                    retries++
                }

                if (!writeCompleted) return@withContext false

                writeCompleted = false
                notificationCharacteristic?.value = dataToSend
                val success = bluetoothGatt?.writeCharacteristic(notificationCharacteristic) ?: false

                if (success) {
                    retries = 0
                    while (!writeCompleted && retries < 100) {
                        delay(10)
                        retries++
                    }
                }

                success && writeCompleted
            } catch (e: Exception) {
                android.util.Log.e("BleManager", "Error sending notification dismissal: ${e.message}")
                false
            }
        }
    }

    /**
     * Send start trip command to ESP32.
     */
    suspend fun sendStartTrip(tripFileName: String): Boolean {
        return withContext(Dispatchers.IO) {
            if (_connectionState.value != ConnectionState.CONNECTED || tripControlCharacteristic == null) {
                return@withContext false
            }

            try {
                val nameBytes = tripFileName.toByteArray(Charsets.UTF_8)
                val buffer = ByteBuffer.allocate(2 + nameBytes.size).apply {
                    order(ByteOrder.BIG_ENDIAN)

                    // Action: 0x01 = Start trip
                    put(0x01.toByte())

                    // Trip name length and name
                    put(nameBytes.size.toByte())
                    put(nameBytes)
                }

                val dataToSend = buffer.array()

                android.util.Log.d("BleManager", "Sending start trip command: $tripFileName")

                // Wait for previous write to complete
                var retries = 0
                while (!writeCompleted && retries < 100) {
                    delay(10)
                    retries++
                }

                if (!writeCompleted) return@withContext false

                writeCompleted = false
                tripControlCharacteristic?.value = dataToSend
                val success = bluetoothGatt?.writeCharacteristic(tripControlCharacteristic) ?: false

                if (success) {
                    retries = 0
                    while (!writeCompleted && retries < 100) {
                        delay(10)
                        retries++
                    }
                }

                success && writeCompleted
            } catch (e: Exception) {
                android.util.Log.e("BleManager", "Error sending start trip command: ${e.message}")
                false
            }
        }
    }

    /**
     * Request list of recorded trips from ESP32.
     */
    suspend fun requestRecordingList(): Boolean {
        val requestPacket = byteArrayOf(RECORDING_CONTROL_ACTION_LIST.toByte())
        return sendRecordingControlPacket(requestPacket)
    }

    /**
     * Request a recorded trip transfer from ESP32 by directory name.
     */
    suspend fun requestRecordingTransfer(recordingName: String): Boolean {
        val nameBytes = recordingName.toByteArray(Charsets.UTF_8)
        if (nameBytes.size > 255) return false
        val buffer = ByteArray(2 + nameBytes.size)
        buffer[0] = RECORDING_CONTROL_ACTION_DOWNLOAD.toByte()
        buffer[1] = nameBytes.size.toByte()
        System.arraycopy(nameBytes, 0, buffer, 2, nameBytes.size)
        return sendRecordingControlPacket(buffer)
    }

    /**
     * Send client ready signal to ESP32.
     * This triggers immediate sync of trip list and active trip.
     */
    private suspend fun sendClientReadySignal(): Boolean {
        return withContext(Dispatchers.IO) {
            if (_connectionState.value != ConnectionState.CONNECTED || tripControlCharacteristic == null) {
                return@withContext false
            }

            try {
                val buffer = ByteBuffer.allocate(1).apply {
                    // Action: 0xFF = Client ready
                    put(0xFF.toByte())
                }

                val dataToSend = buffer.array()

                android.util.Log.d("BleManager", "Sending client ready signal to ESP32")

                // Wait for previous write to complete
                var retries = 0
                while (!writeCompleted && retries < 100) {
                    delay(10)
                    retries++
                }

                if (!writeCompleted) return@withContext false

                writeCompleted = false
                tripControlCharacteristic?.value = dataToSend
                val success = bluetoothGatt?.writeCharacteristic(tripControlCharacteristic) ?: false

                if (success) {
                    retries = 0
                    while (!writeCompleted && retries < 100) {
                        delay(10)
                        retries++
                    }
                }

                android.util.Log.d("BleManager", "Client ready signal sent: $success")
                success && writeCompleted
            } catch (e: Exception) {
                android.util.Log.e("BleManager", "Error sending client ready signal: ${e.message}")
                false
            }
        }
    }

    /**
     * Send stop trip command to ESP32.
     */
    suspend fun sendStopTrip(): Boolean {
        return withContext(Dispatchers.IO) {
            if (_connectionState.value != ConnectionState.CONNECTED || tripControlCharacteristic == null) {
                return@withContext false
            }

            try {
                val buffer = ByteBuffer.allocate(1).apply {
                    // Action: 0x00 = Stop trip
                    put(0x00.toByte())
                }

                val dataToSend = buffer.array()

                android.util.Log.d("BleManager", "Sending stop trip command")

                // Wait for previous write to complete
                var retries = 0
                while (!writeCompleted && retries < 100) {
                    delay(10)
                    retries++
                }

                if (!writeCompleted) return@withContext false

                writeCompleted = false
                tripControlCharacteristic?.value = dataToSend
                val success = bluetoothGatt?.writeCharacteristic(tripControlCharacteristic) ?: false

                if (success) {
                    retries = 0
                    while (!writeCompleted && retries < 100) {
                        delay(10)
                        retries++
                    }
                }

                success && writeCompleted
            } catch (e: Exception) {
                android.util.Log.e("BleManager", "Error sending stop trip command: ${e.message}")
                false
            }
        }
    }

    /**
     * Send Navigate Home request to ESP32.
     * This triggers the ESP32 to send its current GPS location back via the Navigate Home characteristic.
     * Format: 1 byte (0x01 = request flag)
     */
    suspend fun sendNavigateHomeRequest(): Boolean {
        return withContext(Dispatchers.IO) {
            if (_connectionState.value != ConnectionState.CONNECTED || navigateHomeCharacteristic == null) {
                return@withContext false
            }

            try {
                android.util.Log.d("BleManager", "Sending Navigate Home request to ESP32")

                val buffer = ByteBuffer.allocate(1).apply {
                    // Action: 0x01 = Request Navigate Home from app
                    put(0x01.toByte())
                }

                val dataToSend = buffer.array()

                // Wait for previous write to complete
                var retries = 0
                while (!writeCompleted && retries < 100) {
                    delay(10)
                    retries++
                }

                if (!writeCompleted) return@withContext false

                writeCompleted = false
                navigateHomeCharacteristic?.value = dataToSend
                val success = bluetoothGatt?.writeCharacteristic(navigateHomeCharacteristic) ?: false

                if (success) {
                    retries = 0
                    while (!writeCompleted && retries < 100) {
                        delay(10)
                        retries++
                    }
                }

                android.util.Log.d("BleManager", "Navigate Home request sent: $success")
                success && writeCompleted
            } catch (e: Exception) {
                android.util.Log.e("BleManager", "Error sending Navigate Home request: ${e.message}")
                false
            }
        }
    }

    /**
     * Send device status update to ESP32.
     * This includes music state, battery level, network info, etc.
     */
    suspend fun sendDeviceStatus(
        musicPlaying: Boolean,
        songTitle: String,
        songArtist: String,
        phoneBatteryPercent: Int,
        phoneCharging: Boolean,
        wifiConnected: Boolean,
        wifiSsid: String,
        wifiSignalStrength: Int,
        cellularSignalStrength: Int,
        cellularType: String,
        notificationSyncEnabled: Boolean
    ): Boolean {
        return withContext(Dispatchers.IO) {
            if (_connectionState.value != ConnectionState.CONNECTED || deviceStatusCharacteristic == null) {
                return@withContext false
            }

            try {
                android.util.Log.d("BleManager", "Sending device status update...")

                val buffer = ByteBuffer.allocate(256).apply {
                    order(ByteOrder.LITTLE_ENDIAN)

                    // Music state
                    put(if (musicPlaying) 1.toByte() else 0.toByte())

                    // Song title (64 bytes, null-terminated)
                    val songTitleBytes = songTitle.take(63).toByteArray(Charsets.UTF_8)
                    put(songTitleBytes)
                    repeat(64 - songTitleBytes.size) { put(0.toByte()) }

                    // Song artist (32 bytes, null-terminated)
                    val songArtistBytes = songArtist.take(31).toByteArray(Charsets.UTF_8)
                    put(songArtistBytes)
                    repeat(32 - songArtistBytes.size) { put(0.toByte()) }

                    // Phone battery
                    put(phoneBatteryPercent.toByte())
                    put(if (phoneCharging) 1.toByte() else 0.toByte())

                    // WiFi info
                    put(if (wifiConnected) 1.toByte() else 0.toByte())

                    // WiFi SSID (32 bytes, null-terminated)
                    val wifiSsidBytes = wifiSsid.take(31).toByteArray(Charsets.UTF_8)
                    put(wifiSsidBytes)
                    repeat(32 - wifiSsidBytes.size) { put(0.toByte()) }

                    put(wifiSignalStrength.toByte())

                    // Cellular info
                    put(cellularSignalStrength.toByte())

                    // Cellular type (16 bytes, null-terminated)
                    val cellularTypeBytes = cellularType.take(15).toByteArray(Charsets.UTF_8)
                    put(cellularTypeBytes)
                    repeat(16 - cellularTypeBytes.size) { put(0.toByte()) }

                    // Notification sync state
                    put(if (notificationSyncEnabled) 1.toByte() else 0.toByte())
                }

                val dataToSend = buffer.array().copyOfRange(0, buffer.position())
                android.util.Log.d("BleManager", "Device status packet size: ${dataToSend.size} bytes")

                // Wait for previous write to complete
                var retries = 0
                while (!writeCompleted && retries < 100) {
                    delay(10)
                    retries++
                }

                if (!writeCompleted) return@withContext false

                writeCompleted = false
                deviceStatusCharacteristic?.value = dataToSend
                val success = bluetoothGatt?.writeCharacteristic(deviceStatusCharacteristic) ?: false

                if (success) {
                    retries = 0
                    while (!writeCompleted && retries < 100) {
                        delay(10)
                        retries++
                    }
                }

                android.util.Log.d("BleManager", "Device status sent: $success")
                success && writeCompleted
            } catch (e: Exception) {
                android.util.Log.e("BleManager", "Error sending device status: ${e.message}")
                false
            }
        }
    }

    /**
     * Enable periodic device status updates on ESP32 (when app is in foreground)
     */
    suspend fun enablePeriodicStatusUpdates(): Boolean {
        return withContext(Dispatchers.IO) {
            try {
                if (bluetoothGatt == null || deviceStatusCharacteristic == null) {
                    android.util.Log.w("BleManager", "Cannot enable periodic updates: not connected or characteristic null")
                    return@withContext false
                }

                val command = byteArrayOf(0x30.toByte())
                deviceStatusCharacteristic?.value = command
                val success = bluetoothGatt?.writeCharacteristic(deviceStatusCharacteristic) ?: false

                android.util.Log.d("BleManager", "Enable periodic updates command sent: $success")
                success
            } catch (e: Exception) {
                android.util.Log.e("BleManager", "Error enabling periodic updates: ${e.message}")
                false
            }
        }
    }

    /**
     * Disable periodic device status updates on ESP32 (when app is in background)
     */
    suspend fun disablePeriodicStatusUpdates(): Boolean {
        return withContext(Dispatchers.IO) {
            try {
                if (bluetoothGatt == null || deviceStatusCharacteristic == null) {
                    android.util.Log.w("BleManager", "Cannot disable periodic updates: not connected or characteristic null")
                    return@withContext false
                }

                val command = byteArrayOf(0x31.toByte())
                deviceStatusCharacteristic?.value = command
                val success = bluetoothGatt?.writeCharacteristic(deviceStatusCharacteristic) ?: false

                android.util.Log.d("BleManager", "Disable periodic updates command sent: $success")
                success
            } catch (e: Exception) {
                android.util.Log.e("BleManager", "Error disabling periodic updates: ${e.message}")
                false
            }
        }
    }
}
