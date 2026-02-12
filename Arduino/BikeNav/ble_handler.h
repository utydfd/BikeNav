#ifndef BLE_HANDLER_H
#define BLE_HANDLER_H

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <SD.h>
#include <esp_wifi.h>
#include <esp_bt.h>
#include <limits.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

// Include notification system and Bluetooth icons
#include "notification_system.h"
extern const unsigned char ICON_BT_CONNECTED[];
extern const unsigned char ICON_BT_DISCONNECTED[];

// GPS coordinates from BikeNav.ino
extern double currentLat;
extern double currentLon;
extern bool bluetoothEnabled;

// Forward declarations for functions defined in BikeNav.ino
void sendEspDeviceStatus();
uint8_t getGpsStage();

// External from map_trips.h
bool readTripListMetadata(const char* tripDirName, char* outName, size_t maxLen, uint64_t* outCreatedAt);

// Hardcoded UUIDs
#define SERVICE_UUID        "12345678-1234-1234-1234-123456789abc"
#define TILE_CHAR_UUID      "12345678-1234-1234-1234-123456789abd"
#define TRIP_CHAR_UUID      "12345678-1234-1234-1234-123456789abe"
#define WEATHER_CHAR_UUID   "12345678-1234-1234-1234-123456789abf"
#define RADAR_CHAR_UUID     "12345678-1234-1234-1234-123456789ac5"
#define NOTIFICATION_CHAR_UUID "12345678-1234-1234-1234-123456789ac0"
#define TRIP_LIST_CHAR_UUID "12345678-1234-1234-1234-123456789ac1"
#define TRIP_CONTROL_CHAR_UUID "12345678-1234-1234-1234-123456789ac2"
#define NAVIGATE_HOME_CHAR_UUID "12345678-1234-1234-1234-123456789ac3"
#define DEVICE_STATUS_CHAR_UUID "12345678-1234-1234-1234-123456789ac4"
#define RECORDING_LIST_CHAR_UUID "12345678-1234-1234-1234-123456789ac6"
#define RECORDING_CONTROL_CHAR_UUID "12345678-1234-1234-1234-123456789ac7"
#define RECORDING_TRANSFER_CHAR_UUID "12345678-1234-1234-1234-123456789ac8"

#define BLE_DEVICE_NAME     "KoloMapa2"

// BLE state
BLEServer* pServer = nullptr;
BLECharacteristic* pTileCharacteristic = nullptr;
BLECharacteristic* pTripCharacteristic = nullptr;
BLECharacteristic* pWeatherCharacteristic = nullptr;
BLECharacteristic* pRadarCharacteristic = nullptr;
BLECharacteristic* pNotificationCharacteristic = nullptr;
BLECharacteristic* pTripListCharacteristic = nullptr;
BLECharacteristic* pTripControlCharacteristic = nullptr;
BLECharacteristic* pNavigateHomeCharacteristic = nullptr;
BLECharacteristic* pDeviceStatusCharacteristic = nullptr;
BLECharacteristic* pRecordingListCharacteristic = nullptr;
BLECharacteristic* pRecordingControlCharacteristic = nullptr;
BLECharacteristic* pRecordingTransferCharacteristic = nullptr;
bool deviceConnected = false;
unsigned long connectionTime = 0;
volatile bool tripListSent = false;      
volatile bool activeTripSent = false;    
volatile bool clientFullyReady = false;  
bool bleInitialized = false;
bool bleShutdownInProgress = false;

// Deferred page navigation
volatile bool pendingPageNavigation = false;
volatile PageType pendingNavigationPage = PAGE_MAP;

// Tile receive buffers
uint8_t* tileReceiveBuffer = nullptr;
uint32_t tileExpectedSize = 0;
uint32_t tileBufferIndex = 0;
bool tileHeaderReceived = false;
bool tileSkipMode = false;
uint8_t tileHeaderBuffer[14]; 
uint32_t tileHeaderBufferIndex = 0;

// Trip receive buffers
uint8_t* tripReceiveBuffer = nullptr;
uint32_t tripExpectedSize = 0;
uint32_t tripBufferIndex = 0;
bool tripHeaderReceived = false;
uint8_t tripHeaderBuffer[10];
uint32_t tripHeaderBufferIndex = 0;

// Weather data structures
struct HourlyWeatherData {
    uint8_t hour; int16_t temp; uint8_t condition; uint8_t precipChance;
} __attribute__((packed));

struct WeatherDataPacket {
    uint8_t hasError; char errorMessage[64]; char location[32];
    int16_t currentTemp; int16_t feelsLike; uint8_t condition; uint8_t humidity;
    uint16_t windSpeed; uint16_t windDir; uint16_t pressure; uint8_t precipChance;
    uint32_t sunrise; uint32_t sunset; uint8_t hourlyCount; HourlyWeatherData hourly[6];
} __attribute__((packed));

uint8_t* weatherReceiveBuffer = nullptr;
uint32_t weatherBufferIndex = 0;
uint32_t weatherExpectedSize = sizeof(WeatherDataPacket);
bool weatherDataReady = false;
WeatherDataPacket currentWeather;
unsigned long lastWeatherUpdate = 0;

// Radar image data (1-bit packed, white=1, black=0)
#define RADAR_IMAGE_WIDTH 128
#define RADAR_IMAGE_HEIGHT 296
#define RADAR_IMAGE_BYTES ((RADAR_IMAGE_WIDTH * RADAR_IMAGE_HEIGHT) / 8)
#define RADAR_ERROR_MESSAGE_SIZE 64
#define RADAR_FRAME_HEADER_SIZE 4
#define RADAR_BASE_TIME_MAGIC 0xA5
#define RADAR_FRAME_STEP_DEFAULT_MINUTES 5
#define RADAR_MAX_PAST_FRAMES 6
#define RADAR_MAX_FUTURE_FRAMES 5
#define RADAR_MAX_FRAMES (RADAR_MAX_PAST_FRAMES + RADAR_MAX_FUTURE_FRAMES + 1)
#define RADAR_PACKET_SIZE (RADAR_FRAME_HEADER_SIZE + RADAR_ERROR_MESSAGE_SIZE + RADAR_IMAGE_BYTES)

uint8_t* radarReceiveBuffer = nullptr;
uint32_t radarBufferIndex = 0;
uint32_t radarExpectedSize = RADAR_PACKET_SIZE;
bool radarDataReady = false;
bool radarHasError = false;
char radarErrorMessage[RADAR_ERROR_MESSAGE_SIZE] = "";
unsigned long lastRadarUpdate = 0;
int radarBaseLocalMinutes = -1;
bool radarBaseLocalMinutesValid = false;
int radarNowcastStepMinutes = 0;
bool radarNowcastStepValid = false;
int radarFrameLocalMinutes[RADAR_MAX_FRAMES];
bool radarFrameLocalMinutesValid[RADAR_MAX_FRAMES];
uint8_t* radarFrames = nullptr;
bool radarFrameReady[RADAR_MAX_FRAMES];
int radarFrameStepMinutes = RADAR_FRAME_STEP_DEFAULT_MINUTES;
int radarFrameTotalCount = RADAR_MAX_FRAMES;
bool radarFramesUpdated = false;

int radarFrameOffsetToIndex(int offsetSteps) {
  return offsetSteps + RADAR_MAX_PAST_FRAMES;
}

bool isRadarFrameOffsetValid(int offsetSteps) {
  int index = radarFrameOffsetToIndex(offsetSteps);
  return index >= 0 && index < RADAR_MAX_FRAMES;
}

void clearRadarFrames() {
  for (int i = 0; i < RADAR_MAX_FRAMES; i++) {
    radarFrameReady[i] = false;
    radarFrameLocalMinutesValid[i] = false;
    radarFrameLocalMinutes[i] = 0;
  }
  radarFrameStepMinutes = RADAR_FRAME_STEP_DEFAULT_MINUTES;
  radarFrameTotalCount = RADAR_MAX_FRAMES;
  radarDataReady = false;
  radarHasError = false;
  radarErrorMessage[0] = '\0';
  radarBaseLocalMinutes = -1;
  radarBaseLocalMinutesValid = false;
  radarNowcastStepMinutes = 0;
  radarNowcastStepValid = false;
  radarFramesUpdated = true;
}

void initRadarFrames() {
  if (radarFrames == nullptr) {
    size_t bytes = RADAR_IMAGE_BYTES * RADAR_MAX_FRAMES;
    radarFrames = (uint8_t*)ps_malloc(bytes);
    if (radarFrames == nullptr) {
      radarFrames = (uint8_t*)malloc(bytes);
    }
    if (radarFrames == nullptr) {
      Serial.println("[RADAR] ERROR: Failed to allocate radar frame storage");
    }
  }
  clearRadarFrames();
}

bool isRadarFrameReady(int offsetSteps) {
  if (!isRadarFrameOffsetValid(offsetSteps)) return false;
  int index = radarFrameOffsetToIndex(offsetSteps);
  return radarFrameReady[index];
}

const uint8_t* getRadarFrameData(int offsetSteps) {
  if (radarFrames == nullptr || !isRadarFrameReady(offsetSteps)) return nullptr;
  int index = radarFrameOffsetToIndex(offsetSteps);
  return radarFrames + (index * RADAR_IMAGE_BYTES);
}

bool getRadarFrameLocalMinutes(int offsetSteps, int* outMinutes) {
  if (!isRadarFrameOffsetValid(offsetSteps)) return false;
  int index = radarFrameOffsetToIndex(offsetSteps);
  if (!radarFrameLocalMinutesValid[index]) return false;
  if (outMinutes != nullptr) {
    *outMinutes = radarFrameLocalMinutes[index];
  }
  return true;
}

int getRadarMinFrameOffset() {
  for (int i = 0; i < RADAR_MAX_FRAMES; i++) {
    if (radarFrameReady[i]) {
      return i - RADAR_MAX_PAST_FRAMES;
    }
  }
  return 0;
}

int getRadarMaxFrameOffset() {
  for (int i = RADAR_MAX_FRAMES - 1; i >= 0; i--) {
    if (radarFrameReady[i]) {
      return i - RADAR_MAX_PAST_FRAMES;
    }
  }
  return 0;
}

// Navigate Home error tracking
bool navigateHomeHasError = false;
char navigateHomeErrorMessage[64] = "";
unsigned long navigateHomeRequestTime = 0;  // Timestamp when request was sent

// Phone Device Status data structures - Android sends phone status to ESP
struct DeviceStatusPacket {
    uint8_t musicPlaying;           // 1 byte
    char songTitle[64];             // 64 bytes
    char songArtist[32];            // 32 bytes
    uint8_t phoneBatteryPercent;    // 1 byte
    uint8_t phoneCharging;          // 1 byte
    uint8_t wifiConnected;          // 1 byte
    char wifiSsid[32];              // 32 bytes
    uint8_t wifiSignalStrength;     // 1 byte
    uint8_t cellularSignalStrength; // 1 byte
    char cellularType[16];          // 16 bytes
    uint8_t notificationSyncEnabled;// 1 byte
} __attribute__((packed));

DeviceStatusPacket currentDeviceStatus;
bool deviceStatusReceived = false;
bool deviceStatusChanged = false;
unsigned long lastDeviceStatusUpdate = 0;

// ESP Device Status data structures - ESP sends its status to Android
struct EspDeviceStatusPacket {
    uint8_t batteryPercent;         // 0-100
    uint8_t gpsStage;               // 0 = no data, 1 = time, 2 = date, 3 = location locked
    uint8_t satelliteCount;         // Number of satellites in view (0-255)
} __attribute__((packed));

EspDeviceStatusPacket currentEspDeviceStatus;
unsigned long lastDeviceStatusSendTime = 0;
const unsigned long DEVICE_STATUS_SEND_INTERVAL = 5000; // Send every 5 seconds
bool periodicStatusUpdatesEnabled = false; // Only send when app is in foreground

// Forward declarations and external references
class BatteryManager;
extern TinyGPSPlus gps;
extern BatteryManager batteryManager;
extern int zoomLevel;

struct PhoneNotification {
    uint32_t id;
    char appName[32];
    char title[64];
    char text[128];
    uint8_t hasIcon;        // 0 = no icon, 1 = has icon
    uint8_t iconData[195];  // Icon bitmap data (39x39 monochrome, MSB-first)
} __attribute__((packed));

extern void addPhoneNotification(uint32_t id, const char* appName, const char* title, const char* text,
                                 const uint8_t* iconData, bool hasIcon);
extern void dismissPhoneNotificationById(uint32_t id);
extern void sendNotificationDismissal(uint32_t id);

const char* MAP_DIR = "/Map";
const char* TRIPS_DIR = "/Trips";
const char* RECORDINGS_DIR = "/Recordings";
const char* MAP_INDEX_PATH = "/Map/index.bin";

const uint8_t TILE_INV_ACTION_REQUEST = 0x10;
const uint8_t TILE_INV_ACTION_START = 0x11;
const uint8_t TILE_INV_ACTION_DATA = 0x12;
const uint8_t TILE_INV_ACTION_END = 0x13;
const uint8_t TILE_INV_ACTION_ERROR = 0x14;
const uint8_t TILE_INV_RECORD_SIZE = 9;
const uint8_t TILE_INV_MAX_RECORDS_PER_CHUNK = 50;
const unsigned long TILE_INV_CHUNK_INTERVAL_MS = 10;

volatile bool tileInventoryRequestPending = false;
bool tileInventorySending = false;
File tileInventoryFile;
uint32_t tileInventoryFileSize = 0;
uint32_t tileInventoryBytesSent = 0;
unsigned long tileInventoryLastSendMs = 0;

const uint8_t RECORDING_CONTROL_ACTION_LIST = 0x01;
const uint8_t RECORDING_CONTROL_ACTION_DOWNLOAD = 0x02;
const uint8_t RECORDING_TRANSFER_ACTION_START = 0x30;
const uint8_t RECORDING_TRANSFER_ACTION_DATA = 0x31;
const uint8_t RECORDING_TRANSFER_ACTION_END = 0x32;
const uint8_t RECORDING_TRANSFER_ACTION_ERROR = 0x33;
const uint16_t RECORDING_TRANSFER_CHUNK_SIZE = 480;
const unsigned long RECORDING_TRANSFER_CHUNK_INTERVAL_MS = 5;

volatile bool recordingListPending = false;
volatile bool recordingTransferPending = false;
bool recordingTransferSending = false;
char pendingRecordingName[64] = "";
File recordingMetaFile;
File recordingGpxFile;
uint32_t recordingMetaSize = 0;
uint32_t recordingGpxSize = 0;
uint32_t recordingBytesSent = 0;
unsigned long recordingTransferLastSendMs = 0;

// Forward declarations
bool saveTileToSD(int zoom, int tileX, int tileY, uint8_t* data, uint32_t size);
void saveTripToSD(const char* fileName, uint8_t* gpxData, uint32_t gpxSize, uint8_t* metaData, uint32_t metaSize);
bool decompressRLE(uint8_t* compressed, uint32_t compressedSize, uint8_t* decompressed, uint32_t* decompressedSize);
void scanAndSendTripList();
void scanAndSendRecordingList();
void sendActiveTripUpdate();
bool loadAndStartTripByName(const char* tripName);
void requestNavigateHome();
void appendTileIndexRecord(uint8_t zoom, uint32_t tileX, uint32_t tileY);
bool rebuildTileIndex();
void startTileInventorySend();
void updateTileInventorySend();
void sendTileInventoryError();
void startRecordingTransfer(const char* recordingDirName);
void updateRecordingTransferSend();
void finishRecordingTransfer();
void sendRecordingTransferError(const char* message);

// RLE decompression
bool decompressRLE(uint8_t* compressed, uint32_t compressedSize, uint8_t* decompressed, uint32_t* decompressedSize) {
    uint32_t srcIdx = 0; uint32_t dstIdx = 0; uint32_t maxDst = *decompressedSize;
    while (srcIdx < compressedSize && dstIdx < maxDst) {
        if (srcIdx + 1 >= compressedSize) return false;
        uint8_t count = compressed[srcIdx++];
        uint8_t value = compressed[srcIdx++];
        if (dstIdx + count > maxDst) return false;
        for (int i = 0; i < count; i++) decompressed[dstIdx++] = value;
    }
    *decompressedSize = dstIdx;
    return true;
}

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
      Serial.println("BLE Client connected");
      showNotification("Bluetooth", "Phone connected", "", ICON_BT_CONNECTED, 2000);  // 2 seconds

      // Reset Tile State
      tileBufferIndex = 0; tileHeaderBufferIndex = 0; tileHeaderReceived = false; tileSkipMode = false;
      if (tileReceiveBuffer) { free(tileReceiveBuffer); tileReceiveBuffer = nullptr; }

      // Reset Trip State
      tripBufferIndex = 0; tripHeaderBufferIndex = 0; tripHeaderReceived = false;
      if (tripReceiveBuffer) { free(tripReceiveBuffer); tripReceiveBuffer = nullptr; }

      weatherBufferIndex = 0;
      if (weatherReceiveBuffer) { free(weatherReceiveBuffer); weatherReceiveBuffer = nullptr; }
      radarBufferIndex = 0;
      if (radarReceiveBuffer) { free(radarReceiveBuffer); radarReceiveBuffer = nullptr; }

      connectionTime = millis();
      tripListSent = false; activeTripSent = false; clientFullyReady = false;
      tileInventoryRequestPending = false;
      tileInventorySending = false;
      recordingListPending = false;
      recordingTransferPending = false;
      recordingTransferSending = false;
      pendingRecordingName[0] = '\0';
      if (recordingMetaFile) { recordingMetaFile.close(); }
      if (recordingGpxFile) { recordingGpxFile.close(); }
      recordingMetaSize = 0;
      recordingGpxSize = 0;
      recordingBytesSent = 0;
      recordingTransferLastSendMs = 0;
    }

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
      Serial.println("BLE Client disconnected");
      if (!bleShutdownInProgress && bluetoothEnabled) {
        showNotification("Bluetooth", "Phone disconnected", "", ICON_BT_DISCONNECTED, 2000);  // 2 seconds
      }
      tileInventoryRequestPending = false;
      tileInventorySending = false;
      if (tileInventoryFile) { tileInventoryFile.close(); }
      recordingListPending = false;
      recordingTransferPending = false;
      recordingTransferSending = false;
      pendingRecordingName[0] = '\0';
      if (recordingMetaFile) { recordingMetaFile.close(); }
      if (recordingGpxFile) { recordingGpxFile.close(); }
      recordingMetaSize = 0;
      recordingGpxSize = 0;
      recordingBytesSent = 0;
      recordingTransferLastSendMs = 0;
      if (!bleShutdownInProgress && bluetoothEnabled) {
        if (tileReceiveBuffer) free(tileReceiveBuffer); tileReceiveBuffer = nullptr;
        if (tripReceiveBuffer) free(tripReceiveBuffer); tripReceiveBuffer = nullptr;
        if (weatherReceiveBuffer) free(weatherReceiveBuffer); weatherReceiveBuffer = nullptr;
        if (radarReceiveBuffer) free(radarReceiveBuffer); radarReceiveBuffer = nullptr;
        pServer->startAdvertising();
      }
    }
};
/*
 * ======================================================================================
 * CRITICAL ARCHITECTURE NOTE: BLE TILE TRANSFER & SD CARD STABILITY
 * ======================================================================================
 * 
 * THE PROBLEM:
 * The tile transfer system previously suffered from intermittent failures, data corruption
 * ("Header CORRUPT: Z=255"), and write errors ("Failed to open tile for writing").
 * 
 * ROOT CAUSES IDENTIFIED:
 * 1. SPI BUS CONTENTION: The SD Card and the E-Paper Display share the same SPI bus. 
 *    When the main loop triggers a display refresh (e.g., status bar update), the SPI bus 
 *    becomes busy. Any attempt to call `SD.open()` during this window fails immediately.
 * 
 * 2. FILE DESCRIPTOR EXHAUSTION: The ESP32's FatFS driver has a limit on open file handles.
 *    Previous code checked `SD.exists()` and `SD.mkdir()` for every single tile. These checks
 *    consume file descriptors faster than the system releases them, causing valid `SD.open()` 
 *    calls to fail even when the bus was free.
 * 
 * 3. SYNC LOSS: Without strict flow control, the Android app sends the next tile while the 
 *    ESP32 is still struggling with the SD card. The ESP32 then interprets the incoming 
 *    payload data as a new Header, leading to garbage values and a broken stream.
 * 
 * ======================================================================================
 * THE SOLUTION (DO NOT MODIFY WITHOUT CAUTION):
 * ======================================================================================
 * 
 * 1. CONTENTION RETRY LOOP (in `saveTileToSD`):
 *    We treat `SD.open()` failures as temporary (likely due to Display refresh). 
 *    We utilize a retry loop with small `delay()`s to wait for the SPI bus to free up.
 * 
 * 2. MINIMIZED FILE OPERATIONS:
 *    - "Blind Remove": We call `SD.remove()` without checking `SD.exists()` first.
 *    - "Lazy Mkdir": We attempt to write the file assuming directories exist. We only 
 *      check/create directories (`SD.mkdir`) if the write fails after retries.
 *    - This reduces file system overhead by ~60% per tile.
 * 
 * 3. STRICT STOP-AND-WAIT FLOW CONTROL:
 *    - ESP32 sends the ACK (Notification) ONLY AFTER the SD write returns (success or fail).
 *    - On write failure, we add a throttle `delay(200)` to let the system recover before 
 *      requesting the next packet.
 * 
 * WARNING: Adding redundant `SD.exists()` checks or removing the retry logic will 
 * immediately re-introduce write failures during map rendering or display updates.
 * ======================================================================================
 */
class TileCharacteristicCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      String valueStr = pCharacteristic->getValue();
      uint32_t len = valueStr.length();
      if (len == 0) return;

      const uint8_t* data = (const uint8_t*)valueStr.c_str();

      // STATE 1: Accumulate header
      if (!tileHeaderReceived) {
        if (tileHeaderBufferIndex == 0 && len >= 1) {
            if (data[0] > 0x01) {
                // If we get garbage, we ignore it. The Android app relies on ACK to send next header.
                Serial.printf("IGNORING GARBAGE Header byte: 0x%02X\n", data[0]);
                return; 
            }
        }

        uint32_t bytesNeeded = 14 - tileHeaderBufferIndex;
        uint32_t bytesToCopy = (len < bytesNeeded) ? len : bytesNeeded;

        memcpy(tileHeaderBuffer + tileHeaderBufferIndex, data, bytesToCopy);
        tileHeaderBufferIndex += bytesToCopy;

        if (tileHeaderBufferIndex < 14) return;

        // Parse header
        uint8_t flags = tileHeaderBuffer[0];
        uint8_t zoom = tileHeaderBuffer[1];
        uint32_t tileX = ((uint32_t)tileHeaderBuffer[2] << 24) | ((uint32_t)tileHeaderBuffer[3] << 16) | ((uint32_t)tileHeaderBuffer[4] << 8) | (uint32_t)tileHeaderBuffer[5];
        uint32_t tileY = ((uint32_t)tileHeaderBuffer[6] << 24) | ((uint32_t)tileHeaderBuffer[7] << 16) | ((uint32_t)tileHeaderBuffer[8] << 8) | (uint32_t)tileHeaderBuffer[9];
        tileExpectedSize = ((uint32_t)tileHeaderBuffer[10] << 24) | ((uint32_t)tileHeaderBuffer[11] << 16) | ((uint32_t)tileHeaderBuffer[12] << 8) | (uint32_t)tileHeaderBuffer[13];

        // Sanity Check
        if (flags > 0x01 || zoom > 20 || tileExpectedSize == 0 || tileExpectedSize > 1000000) {
            Serial.printf("Header CORRUPT: F=%02X Z=%d Size=%d. Resetting.\n", flags, zoom, tileExpectedSize);
            resetTileState();
            return;
        }

        // Prepare buffer
        tileSkipMode = false;
        if (tileReceiveBuffer) free(tileReceiveBuffer);
        tileReceiveBuffer = (uint8_t*)malloc(tileExpectedSize + 14); 
        
        if (!tileReceiveBuffer) {
            Serial.println("ERROR: OOM tile buffer -> Skipping");
            tileSkipMode = true;
        } else {
            tileReceiveBuffer[0] = flags;
            tileReceiveBuffer[1] = zoom;
            memcpy(tileReceiveBuffer + 2, &tileX, 4);
            memcpy(tileReceiveBuffer + 6, &tileY, 4);
        }

        tileBufferIndex = 0;
        tileHeaderReceived = true;

        uint32_t dataInPacket = len - bytesToCopy;
        if (dataInPacket > 0) {
            if (dataInPacket > tileExpectedSize) dataInPacket = tileExpectedSize;
            if (tileSkipMode) tileBufferIndex += dataInPacket;
            else if (tileReceiveBuffer) {
                memcpy(tileReceiveBuffer + 10, data + bytesToCopy, dataInPacket);
                tileBufferIndex += dataInPacket;
            }
        }
        if (tileBufferIndex >= tileExpectedSize) finishTileProcessing();
        return;
      }

      // STATE 2: Receive Payload
      if (tileHeaderReceived) {
        uint32_t remainingSpace = tileExpectedSize - tileBufferIndex;
        uint32_t copyLen = (len <= remainingSpace) ? len : remainingSpace;
        if (tileSkipMode) tileBufferIndex += copyLen;
        else if (tileReceiveBuffer) {
            memcpy(tileReceiveBuffer + 10 + tileBufferIndex, data, copyLen);
            tileBufferIndex += copyLen;
        }
        if (tileBufferIndex >= tileExpectedSize) finishTileProcessing();
      }
    }

private:
    void resetTileState() {
      tileBufferIndex = 0; tileHeaderBufferIndex = 0; tileHeaderReceived = false;
      tileExpectedSize = 0; tileSkipMode = false;
      if (tileReceiveBuffer) { free(tileReceiveBuffer); tileReceiveBuffer = nullptr; }
      memset(tileHeaderBuffer, 0, sizeof(tileHeaderBuffer));
    }

    void finishTileProcessing() {
      bool success = true;

      if (tileSkipMode) {
         delay(5); 
      } 
      else if (tileReceiveBuffer) {
        uint8_t flags = tileReceiveBuffer[0];
        uint8_t zoom = tileReceiveBuffer[1];
        uint32_t tileX, tileY;
        memcpy(&tileX, tileReceiveBuffer + 2, 4);
        memcpy(&tileY, tileReceiveBuffer + 6, 4);
        uint8_t* tileData = tileReceiveBuffer + 10;

        unsigned long start = millis();
        if (flags & 0x01) {
            uint32_t maxDecomp = 16384;
            uint8_t* decompressed = (uint8_t*)malloc(maxDecomp);
            if (decompressed) {
                uint32_t actualSize = maxDecomp;
                if (decompressRLE(tileData, tileExpectedSize, decompressed, &actualSize)) {
                    success = saveTileToSD(zoom, tileX, tileY, decompressed, actualSize);
                } else success = false;
                free(decompressed);
            } else success = false;
        } else {
            success = saveTileToSD(zoom, tileX, tileY, tileData, tileExpectedSize);
        }
        
        if (success) {
            Serial.printf("Saved tile %d/%d/%d (%lu ms)\n", zoom, tileX, tileY, millis() - start);
        } else {
            Serial.printf("FAILED to save tile %d/%d/%d (%lu ms)\n", zoom, tileX, tileY, millis() - start);
            // Throttle on failure to allow background tasks (Display/SD) to recover
            delay(100); 
        }
      }

      // CRITICAL: Reset state BEFORE notifying.
      resetTileState();

      if (deviceConnected && pTileCharacteristic) {
        uint8_t ack[] = {0x01};
        pTileCharacteristic->setValue(ack, 1);
        pTileCharacteristic->notify();
      }
    }
};

class TripCharacteristicCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      String valueStr = pCharacteristic->getValue();
      uint32_t len = valueStr.length();
      if (len == 0) return;
      const uint8_t* data = (const uint8_t*)valueStr.c_str();

      if (!tripHeaderReceived) {
        uint32_t bytesNeeded = 10 - tripHeaderBufferIndex;
        uint32_t bytesToCopy = (len < bytesNeeded) ? len : bytesNeeded;
        memcpy(tripHeaderBuffer + tripHeaderBufferIndex, data, bytesToCopy);
        tripHeaderBufferIndex += bytesToCopy;

        if (tripHeaderBufferIndex < 10) return;

        uint16_t nameLen = ((uint16_t)tripHeaderBuffer[0] << 8) | (uint16_t)tripHeaderBuffer[1];
        uint32_t gpxLen = ((uint32_t)tripHeaderBuffer[2] << 24) | ((uint32_t)tripHeaderBuffer[3] << 16) | ((uint32_t)tripHeaderBuffer[4] << 8) | (uint32_t)tripHeaderBuffer[5];
        uint32_t metaLen = ((uint32_t)tripHeaderBuffer[6] << 24) | ((uint32_t)tripHeaderBuffer[7] << 16) | ((uint32_t)tripHeaderBuffer[8] << 8) | (uint32_t)tripHeaderBuffer[9];

        tripExpectedSize = 10 + nameLen + gpxLen + metaLen;
        if (tripExpectedSize > 524288 || tripExpectedSize < 10) {
          tripHeaderBufferIndex = 0; tripHeaderReceived = false; return;
        }

        if (tripReceiveBuffer) free(tripReceiveBuffer);
        tripReceiveBuffer = (uint8_t*)malloc(tripExpectedSize);
        if (!tripReceiveBuffer) {
          tripHeaderBufferIndex = 0; tripHeaderReceived = false; return;
        }

        memcpy(tripReceiveBuffer, tripHeaderBuffer, 10);
        tripBufferIndex = 10;
        tripHeaderReceived = true;

        uint32_t dataInPacket = len - bytesToCopy;
        if (dataInPacket > 0) {
          uint32_t copyLen = (dataInPacket <= (tripExpectedSize - tripBufferIndex)) ? dataInPacket : (tripExpectedSize - tripBufferIndex);
          memcpy(tripReceiveBuffer + tripBufferIndex, data + bytesToCopy, copyLen);
          tripBufferIndex += copyLen;
        }
        if (tripBufferIndex >= tripExpectedSize) processTripAndReset();
        return;
      }

      if (tripHeaderReceived && tripReceiveBuffer) {
        uint32_t remainingSpace = tripExpectedSize - tripBufferIndex;
        uint32_t copyLen = (len <= remainingSpace) ? len : remainingSpace;
        memcpy(tripReceiveBuffer + tripBufferIndex, data, copyLen);
        tripBufferIndex += copyLen;
        if (tripBufferIndex >= tripExpectedSize) processTripAndReset();
      }
    }

    void processTripAndReset() {
        uint16_t nameLen = ((uint16_t)tripReceiveBuffer[0] << 8) | (uint16_t)tripReceiveBuffer[1];
        uint32_t gpxLen = ((uint32_t)tripReceiveBuffer[2] << 24) | ((uint32_t)tripReceiveBuffer[3] << 16) | ((uint32_t)tripReceiveBuffer[4] << 8) | (uint32_t)tripReceiveBuffer[5];
        uint32_t metaLen = ((uint32_t)tripReceiveBuffer[6] << 24) | ((uint32_t)tripReceiveBuffer[7] << 16) | ((uint32_t)tripReceiveBuffer[8] << 8) | (uint32_t)tripReceiveBuffer[9];

        uint8_t* nameData = tripReceiveBuffer + 10;
        uint8_t* gpxData = nameData + nameLen;
        uint8_t* metaData = gpxData + gpxLen;

        char fileName[256] = {0};
        uint16_t copySize = (nameLen < 255) ? nameLen : 255;
        memcpy(fileName, nameData, copySize);

        bool isTempTrip = (strcmp(fileName, "_nav_home_temp") == 0);
        if (isTempTrip) {
          extern bool parseAndLoadGPXFromMemory(const char* tripName, const uint8_t* gpxData, uint32_t gpxSize);
          extern void startTripNavigation(const char* tripDirName);
          extern char loadedTrackName[64];
          extern bool waitingForNavigateHomePath;
          extern bool navigateHomePathLoaded;
          extern bool tripDetailNeedsRedraw;

          if (parseAndLoadGPXFromMemory(fileName, gpxData, gpxLen)) {
            strncpy(loadedTrackName, fileName, sizeof(loadedTrackName) - 1); loadedTrackName[sizeof(loadedTrackName) - 1] = '\0';

            // Check if we're waiting for Navigate Home path in detail view (new flow)
            if (waitingForNavigateHomePath) {
              // New flow: Just load the trip and update state, don't start navigation yet
              Serial.println("Navigate Home trip loaded - waiting for user to press Start");
              navigateHomePathLoaded = true;
              waitingForNavigateHomePath = false;

              // Flag for deferred rendering (avoid calling render from BLE callback)
              tripDetailNeedsRedraw = true;
            } else {
              // Old flow: Auto-start navigation immediately (for backward compatibility)
              startTripNavigation(fileName); sendActiveTripUpdate();
              pendingPageNavigation = true; pendingNavigationPage = PAGE_MAP;
            }
          }
        } else {
          saveTripToSD(fileName, gpxData, gpxLen, metaData, metaLen);
        }
        tripBufferIndex = 0; tripHeaderBufferIndex = 0; tripHeaderReceived = false;
        free(tripReceiveBuffer); tripReceiveBuffer = nullptr;
    }
};

class WeatherCharacteristicCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      String value = pCharacteristic->getValue();
      uint32_t len = value.length();

      Serial.printf("[WEATHER] onWrite #%d, received %d bytes (buffer currently at %d/%d)\n",
        (weatherReceiveBuffer == nullptr ? 1 : 2), len, weatherBufferIndex, weatherExpectedSize);

      if (len == 0) {
        Serial.println("[WEATHER] Empty data, ignoring");
        return;
      }

      const uint8_t* data = (const uint8_t*)value.c_str();

      if (weatherReceiveBuffer == nullptr) {
        Serial.printf("[WEATHER] Starting new packet, allocating buffer for %d bytes\n", weatherExpectedSize);
        weatherReceiveBuffer = (uint8_t*)malloc(weatherExpectedSize);
        if (weatherReceiveBuffer == nullptr) {
          Serial.println("[WEATHER] ERROR: malloc failed!");
          return;
        }
        weatherBufferIndex = 0;
      }

      uint32_t bytesToCopy = weatherExpectedSize - weatherBufferIndex;
      if (len < bytesToCopy) bytesToCopy = len;
      memcpy(weatherReceiveBuffer + weatherBufferIndex, data, bytesToCopy);
      weatherBufferIndex += bytesToCopy;
      Serial.printf("[WEATHER] Buffered %d bytes, total: %d/%d\n", bytesToCopy, weatherBufferIndex, weatherExpectedSize);

      if (weatherBufferIndex >= weatherExpectedSize) {
        Serial.println("[WEATHER] Complete packet received, parsing...");
        memcpy(&currentWeather, weatherReceiveBuffer, sizeof(WeatherDataPacket));

        // Debug: print what we received
        Serial.printf("[WEATHER] hasError: %d\n", currentWeather.hasError);
        Serial.printf("[WEATHER] location: %s\n", currentWeather.location);
        Serial.printf("[WEATHER] temp: %d (%.1f°C)\n", currentWeather.currentTemp, currentWeather.currentTemp / 10.0);
        Serial.printf("[WEATHER] hourlyCount: %d\n", currentWeather.hourlyCount);
        for (int i = 0; i < currentWeather.hourlyCount && i < 6; i++) {
          Serial.printf("[WEATHER]   Hour %d: %02d:00, %.1f°C, cond=%d, rain=%d%%\n",
            i, currentWeather.hourly[i].hour, currentWeather.hourly[i].temp / 10.0,
            currentWeather.hourly[i].condition, currentWeather.hourly[i].precipChance);
        }

        weatherDataReady = true;
        lastWeatherUpdate = millis();
        Serial.println("[WEATHER] weatherDataReady set to TRUE");

        free(weatherReceiveBuffer);
        weatherReceiveBuffer = nullptr;
        weatherBufferIndex = 0;
      }
    }
};

class RadarCharacteristicCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      String value = pCharacteristic->getValue();
      uint32_t len = value.length();

      Serial.printf("[RADAR] onWrite #%d, received %d bytes (buffer currently at %d/%d)\n",
        (radarReceiveBuffer == nullptr ? 1 : 2), len, radarBufferIndex, radarExpectedSize);

      if (len == 0) {
        Serial.println("[RADAR] Empty data, ignoring");
        return;
      }

      const uint8_t* data = (const uint8_t*)value.c_str();

      if (radarReceiveBuffer == nullptr) {
        Serial.printf("[RADAR] Starting new packet, allocating buffer for %d bytes\n", radarExpectedSize);
        radarReceiveBuffer = (uint8_t*)malloc(radarExpectedSize);
        if (radarReceiveBuffer == nullptr) {
          Serial.println("[RADAR] ERROR: malloc failed!");
          return;
        }
        radarBufferIndex = 0;
      }

      uint32_t bytesToCopy = radarExpectedSize - radarBufferIndex;
      if (len < bytesToCopy) bytesToCopy = len;
      memcpy(radarReceiveBuffer + radarBufferIndex, data, bytesToCopy);
      radarBufferIndex += bytesToCopy;
      Serial.printf("[RADAR] Buffered %d bytes, total: %d/%d\n", bytesToCopy, radarBufferIndex, radarExpectedSize);

      if (radarBufferIndex >= radarExpectedSize) {
        Serial.println("[RADAR] Complete packet received, parsing...");
        uint8_t hasError = radarReceiveBuffer[0];
        int8_t frameOffsetSteps = (int8_t)radarReceiveBuffer[1];
        uint8_t stepMinutes = radarReceiveBuffer[2];
        uint8_t totalFrames = radarReceiveBuffer[3];
        // Frame local time-of-day (minutes since midnight).
        uint16_t baseMinutesRaw = (uint16_t)radarReceiveBuffer[RADAR_FRAME_HEADER_SIZE] |
                                  ((uint16_t)radarReceiveBuffer[RADAR_FRAME_HEADER_SIZE + 1] << 8);
        uint8_t baseMagic = radarReceiveBuffer[RADAR_FRAME_HEADER_SIZE + 2];
        uint8_t nowcastStepRaw = radarReceiveBuffer[RADAR_FRAME_HEADER_SIZE + 3];
        bool frameTimeValid = (baseMagic == RADAR_BASE_TIME_MAGIC && baseMinutesRaw < 24 * 60);

        if (stepMinutes > 0) {
          radarFrameStepMinutes = stepMinutes;
        }
        if (totalFrames > 0) {
          radarFrameTotalCount = min((int)totalFrames, RADAR_MAX_FRAMES);
        }

        if (hasError != 0) {
          radarBaseLocalMinutesValid = false;
          radarNowcastStepValid = false;
          if (frameOffsetSteps == 0) {
            radarHasError = true;
            memcpy(radarErrorMessage, radarReceiveBuffer + RADAR_FRAME_HEADER_SIZE, RADAR_ERROR_MESSAGE_SIZE);
            radarErrorMessage[RADAR_ERROR_MESSAGE_SIZE - 1] = '\0';
            radarDataReady = false;
            radarFramesUpdated = true;
            Serial.printf("[RADAR] errorMessage: %s\n", radarErrorMessage);
          }
        } else {
          if (frameTimeValid) {
            radarBaseLocalMinutes = (int)baseMinutesRaw;
            radarBaseLocalMinutesValid = true;
            if (nowcastStepRaw > 0) {
              radarNowcastStepMinutes = (int)nowcastStepRaw;
              radarNowcastStepValid = true;
            } else {
              radarNowcastStepValid = false;
            }
          } else {
            radarBaseLocalMinutesValid = false;
            radarNowcastStepValid = false;
          }
          if (radarFrames == nullptr) {
            initRadarFrames();
          }

          if (radarFrames != nullptr && isRadarFrameOffsetValid(frameOffsetSteps)) {
            int frameIndex = radarFrameOffsetToIndex(frameOffsetSteps);
            const uint8_t* frameData = radarReceiveBuffer + RADAR_FRAME_HEADER_SIZE + RADAR_ERROR_MESSAGE_SIZE;
            memcpy(radarFrames + (frameIndex * RADAR_IMAGE_BYTES), frameData, RADAR_IMAGE_BYTES);
            radarFrameReady[frameIndex] = true;
            if (frameTimeValid) {
              radarFrameLocalMinutes[frameIndex] = (int)baseMinutesRaw;
              radarFrameLocalMinutesValid[frameIndex] = true;
            } else {
              radarFrameLocalMinutesValid[frameIndex] = false;
            }
            radarFramesUpdated = true;

            if (frameOffsetSteps == 0) {
              radarDataReady = true;
              radarHasError = false;
              radarErrorMessage[0] = '\0';
              lastRadarUpdate = millis();
            }
          } else {
            Serial.printf("[RADAR] Ignoring frame offset %d (out of range)\n", frameOffsetSteps);
          }
        }

        Serial.printf("[RADAR] frameOffset=%d, hasError=%d\n", frameOffsetSteps, hasError ? 1 : 0);

        free(radarReceiveBuffer);
        radarReceiveBuffer = nullptr;
        radarBufferIndex = 0;
      }
    }
};

class NotificationCharacteristicCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      String valueStr = pCharacteristic->getValue();
      uint32_t len = valueStr.length();
      if (len == 0) return;
      const uint8_t* data = (const uint8_t*)valueStr.c_str();
      if (len < 5) return;
      uint8_t action = data[0];

      if (action == 0x01) {
        // Parse notification (supports both old 229-byte and new 425-byte formats)
        if (len >= 425) {
          // New format with icon data
          PhoneNotification notif;
          notif.id = ((uint32_t)data[1] << 24) | ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 8) | (uint32_t)data[4];
          memcpy(notif.appName, data + 5, 32); notif.appName[31] = '\0';
          memcpy(notif.title, data + 37, 64); notif.title[63] = '\0';
          memcpy(notif.text, data + 101, 128); notif.text[127] = '\0';
          notif.hasIcon = data[229];  // Icon present flag
          if (notif.hasIcon == 0x01) {
            memcpy(notif.iconData, data + 230, 195);  // Copy icon bitmap data
          }
          Serial.print("Notification received (with icon): ");
          Serial.println(notif.appName);
          addPhoneNotification(notif.id, notif.appName, notif.title, notif.text,
                             notif.hasIcon ? notif.iconData : nullptr, notif.hasIcon);
        } else if (len >= 229) {
          // Old format without icon data (backward compatibility)
          PhoneNotification notif;
          notif.id = ((uint32_t)data[1] << 24) | ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 8) | (uint32_t)data[4];
          memcpy(notif.appName, data + 5, 32); notif.appName[31] = '\0';
          memcpy(notif.title, data + 37, 64); notif.title[63] = '\0';
          memcpy(notif.text, data + 101, 128); notif.text[127] = '\0';
          notif.hasIcon = 0;
          Serial.print("Notification received (no icon): ");
          Serial.println(notif.appName);
          addPhoneNotification(notif.id, notif.appName, notif.title, notif.text, nullptr, false);
        }
      } else if (action == 0x02) {
        // Dismiss notification
        uint32_t notifId = ((uint32_t)data[1] << 24) | ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 8) | (uint32_t)data[4];
        dismissPhoneNotificationById(notifId);
      }
    }
};

class TripControlCharacteristicCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      String valueStr = pCharacteristic->getValue();
      uint32_t len = valueStr.length();
      if (len == 0) return;
      const uint8_t* data = (const uint8_t*)valueStr.c_str();
      uint8_t action = data[0];
      if (action == 0x00) {
        extern void stopTripNavigation(); stopTripNavigation(); sendActiveTripUpdate();
        pendingPageNavigation = true; pendingNavigationPage = PAGE_MAP;
      } else if (action == 0x01) {
        if (len < 2) return;
        uint8_t nameLen = data[1]; if (len < 2 + nameLen) return;
        char tripName[256] = {0}; memcpy(tripName, data + 2, nameLen); tripName[nameLen] = '\0';
        if (loadAndStartTripByName(tripName)) sendActiveTripUpdate();
      } else if (action == 0xFF) {
        clientFullyReady = true;
      } else if (action == TILE_INV_ACTION_REQUEST) {
        tileInventoryRequestPending = true;
      }
    }
};

class RecordingControlCharacteristicCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      String valueStr = pCharacteristic->getValue();
      uint32_t len = valueStr.length();
      if (len == 0) return;
      const uint8_t* data = (const uint8_t*)valueStr.c_str();
      uint8_t action = data[0];
      if (action == RECORDING_CONTROL_ACTION_LIST) {
        recordingListPending = true;
      } else if (action == RECORDING_CONTROL_ACTION_DOWNLOAD) {
        if (len < 2) return;
        uint8_t nameLen = data[1];
        if (len < 2 + nameLen) return;
        if (nameLen >= sizeof(pendingRecordingName)) {
          nameLen = sizeof(pendingRecordingName) - 1;
        }
        memcpy(pendingRecordingName, data + 2, nameLen);
        pendingRecordingName[nameLen] = '\0';
        recordingTransferPending = true;
      }
    }
};

class NavigateHomeCharacteristicCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      String valueStr = pCharacteristic->getValue();
      uint32_t len = valueStr.length();
      if (len == 0) return;

      const uint8_t* data = (const uint8_t*)valueStr.c_str();

      // Check if this is a Navigate Home request from app (first byte = 0x01)
      if (len == 1 && data[0] == 0x01) {
        Serial.println("Navigate Home REQUEST received from Android app");

        // Clear previous error state
        navigateHomeHasError = false;
        navigateHomeErrorMessage[0] = '\0';

        // Trigger Navigate Home (this will send GPS coords back to app)
        requestNavigateHome();
        return;
      }

      // Check if this is an error response (first byte = 0xFF)
      if (len >= 1 && data[0] == 0xFF) {
        Serial.println("Navigate Home ERROR received from Android");

        // Set error flag
        navigateHomeHasError = true;

        // Extract error message (rest of the packet after first byte)
        uint32_t msgLen = min((uint32_t)(len - 1), (uint32_t)63);
        memcpy(navigateHomeErrorMessage, data + 1, msgLen);
        navigateHomeErrorMessage[msgLen] = '\0';

        // Trim trailing null bytes
        for (int i = msgLen - 1; i >= 0; i--) {
          if (navigateHomeErrorMessage[i] == '\0' || navigateHomeErrorMessage[i] == ' ') {
            navigateHomeErrorMessage[i] = '\0';
          } else {
            break;
          }
        }

        if (navigateHomeErrorMessage[0] == '\0') {
          snprintf(navigateHomeErrorMessage, sizeof(navigateHomeErrorMessage), "Route request failed");
        }

        Serial.printf("Error message: '%s'\n", navigateHomeErrorMessage);

        // Reset waiting state
        extern bool waitingForNavigateHomePath;
        extern bool navigateHomePathLoaded;
        waitingForNavigateHomePath = false;
        navigateHomePathLoaded = false;

        // Trigger re-render to show error
        extern bool tripsNeedsRedraw;
        tripsNeedsRedraw = true;
      }
    }
};

class DeviceStatusCharacteristicCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      String valueStr = pCharacteristic->getValue();
      uint32_t len = valueStr.length();
      if (len == 0) return;

      const uint8_t* data = (const uint8_t*)valueStr.c_str();

      // Check if this is a device status update (full packet)
      if (len >= sizeof(DeviceStatusPacket)) {
        memcpy(&currentDeviceStatus, data, sizeof(DeviceStatusPacket));
        deviceStatusReceived = true;
        deviceStatusChanged = true;  // Set flag to trigger page refresh
        lastDeviceStatusUpdate = millis();

        Serial.println("[DEVICE_STATUS] Received device status update:");
        Serial.printf("  Music: %s\n", currentDeviceStatus.musicPlaying ? "Playing" : "Paused");
        Serial.printf("  Song: %s - %s\n", currentDeviceStatus.songArtist, currentDeviceStatus.songTitle);
        Serial.printf("  Battery: %d%% (%s)\n", currentDeviceStatus.phoneBatteryPercent,
                      currentDeviceStatus.phoneCharging ? "Charging" : "Not Charging");
        Serial.printf("  WiFi: %s", currentDeviceStatus.wifiConnected ? "Connected" : "Disconnected");
        if (currentDeviceStatus.wifiConnected) {
          Serial.printf(" (%s, %d%%)", currentDeviceStatus.wifiSsid, currentDeviceStatus.wifiSignalStrength);
        }
        Serial.println();
        Serial.printf("  Cellular: %s (%d%%)\n", currentDeviceStatus.cellularType, currentDeviceStatus.cellularSignalStrength);
        Serial.printf("  Notification Sync: %s\n", currentDeviceStatus.notificationSyncEnabled ? "Enabled" : "Disabled");
      }
      // Check if this is a command response (single byte commands)
      else if (len == 1) {
        uint8_t response = data[0];
        if (response == 0x01) {
          Serial.println("[DEVICE_STATUS] Locate phone command confirmed");
        }
        else if (response == 0x30) {
          periodicStatusUpdatesEnabled = true;
          Serial.println("[DEVICE_STATUS] Periodic status updates ENABLED (app in foreground)");
          // Send immediate update when enabled
          sendEspDeviceStatus();
          lastDeviceStatusSendTime = millis();
        }
        else if (response == 0x31) {
          periodicStatusUpdatesEnabled = false;
          Serial.println("[DEVICE_STATUS] Periodic status updates DISABLED (app in background)");
        }
      }
    }
};

// Function to send commands to phone
void sendDeviceCommand(uint8_t command) {
  if (!deviceConnected || pDeviceStatusCharacteristic == nullptr) return;

  uint8_t cmdData[1] = {command};
  pDeviceStatusCharacteristic->setValue(cmdData, 1);
  pDeviceStatusCharacteristic->notify();

  Serial.printf("[DEVICE_STATUS] Sent command: 0x%02X\n", command);
}

// Music control commands
void sendMusicPlayPause() { sendDeviceCommand(0x01); }
void sendMusicNext() { sendDeviceCommand(0x02); }
void sendMusicPrevious() { sendDeviceCommand(0x03); }

// Phone commands
void sendLocatePhone() { sendDeviceCommand(0x10); }
void sendToggleNotificationSync() { sendDeviceCommand(0x20); }
void sendRequestDeviceStatus() { sendDeviceCommand(0x21); }

void requestWeatherUpdate() {
  if (!deviceConnected || pWeatherCharacteristic == nullptr) return;
  uint8_t gpsData[8];
  float* latPtr = (float*)&gpsData[0]; float* lonPtr = (float*)&gpsData[4];
  *latPtr = (float)currentLat; *lonPtr = (float)currentLon;
  pWeatherCharacteristic->setValue(gpsData, 8); pWeatherCharacteristic->notify();
}

void requestWeatherUpdateForLocation(double lat, double lon) {
  if (!deviceConnected || pWeatherCharacteristic == nullptr) return;
  uint8_t gpsData[8];
  float* latPtr = (float*)&gpsData[0]; float* lonPtr = (float*)&gpsData[4];
  *latPtr = (float)lat; *lonPtr = (float)lon;
  pWeatherCharacteristic->setValue(gpsData, 8); pWeatherCharacteristic->notify();
}

void requestRadarUpdateForLocation(double lat, double lon, uint8_t zoom) {
  if (!deviceConnected || pRadarCharacteristic == nullptr) return;
  uint8_t radarData[9];
  float* latPtr = (float*)&radarData[0]; float* lonPtr = (float*)&radarData[4];
  *latPtr = (float)lat; *lonPtr = (float)lon;
  radarData[8] = zoom;
  pRadarCharacteristic->setValue(radarData, sizeof(radarData));
  pRadarCharacteristic->notify();
}

void requestRadarUpdate() {
  requestRadarUpdateForLocation(currentLat, currentLon, (uint8_t)zoomLevel);
}

void requestNavigateHome() {
  if (!deviceConnected || pNavigateHomeCharacteristic == nullptr) return;

  // Clear previous error state
  navigateHomeHasError = false;
  navigateHomeErrorMessage[0] = '\0';

  // Record request timestamp for timeout detection
  navigateHomeRequestTime = millis();

  uint8_t gpsData[8];
  float* latPtr = (float*)&gpsData[0]; float* lonPtr = (float*)&gpsData[4];
  *latPtr = (float)currentLat; *lonPtr = (float)currentLon;
  pNavigateHomeCharacteristic->setValue(gpsData, 8); pNavigateHomeCharacteristic->notify();

  Serial.printf("Navigate Home request sent: lat=%.6f, lon=%.6f\n", currentLat, currentLon);
}

void resetBleRuntimeState() {
  deviceConnected = false;
  connectionTime = 0;
  tripListSent = false;
  activeTripSent = false;
  clientFullyReady = false;
  periodicStatusUpdatesEnabled = false;
  lastDeviceStatusSendTime = 0;
  recordingListPending = false;
  recordingTransferPending = false;
  recordingTransferSending = false;
  pendingRecordingName[0] = '\0';
  if (recordingMetaFile) { recordingMetaFile.close(); }
  if (recordingGpxFile) { recordingGpxFile.close(); }
  recordingMetaSize = 0;
  recordingGpxSize = 0;
  recordingBytesSent = 0;
  recordingTransferLastSendMs = 0;

  tileBufferIndex = 0;
  tileExpectedSize = 0;
  tileHeaderBufferIndex = 0;
  tileHeaderReceived = false;
  tileSkipMode = false;
  if (tileReceiveBuffer) { free(tileReceiveBuffer); tileReceiveBuffer = nullptr; }

  tripBufferIndex = 0;
  tripExpectedSize = 0;
  tripHeaderBufferIndex = 0;
  tripHeaderReceived = false;
  if (tripReceiveBuffer) { free(tripReceiveBuffer); tripReceiveBuffer = nullptr; }

  weatherBufferIndex = 0;
  if (weatherReceiveBuffer) { free(weatherReceiveBuffer); weatherReceiveBuffer = nullptr; }

  radarBufferIndex = 0;
  if (radarReceiveBuffer) { free(radarReceiveBuffer); radarReceiveBuffer = nullptr; }
}

void initBLE() {
  if (bleInitialized) {
    BLEDevice::startAdvertising();
    return;
  }

  uint32_t originalCpuFreq = getCpuFrequencyMhz();
  setCpuFrequencyMhz(80); delay(10);
  digitalWrite(17, LOW); delay(20);
  btStop(); esp_wifi_stop(); esp_wifi_deinit(); delay(15);

  BLEDevice::init(BLE_DEVICE_NAME);
  delay(30);
  BLEDevice::setPower(ESP_PWR_LVL_N12); delay(15);
  BLEDevice::setMTU(517); delay(10);

  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);

  pTileCharacteristic = pService->createCharacteristic(TILE_CHAR_UUID, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY);
  pTileCharacteristic->setCallbacks(new TileCharacteristicCallbacks());
  pTileCharacteristic->addDescriptor(new BLE2902());

  pTripCharacteristic = pService->createCharacteristic(TRIP_CHAR_UUID, BLECharacteristic::PROPERTY_WRITE);
  pTripCharacteristic->setCallbacks(new TripCharacteristicCallbacks());

  pWeatherCharacteristic = pService->createCharacteristic(WEATHER_CHAR_UUID, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY);
  pWeatherCharacteristic->setCallbacks(new WeatherCharacteristicCallbacks());
  pWeatherCharacteristic->addDescriptor(new BLE2902());

  pRadarCharacteristic = pService->createCharacteristic(RADAR_CHAR_UUID, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY);
  pRadarCharacteristic->setCallbacks(new RadarCharacteristicCallbacks());
  pRadarCharacteristic->addDescriptor(new BLE2902());

  pNotificationCharacteristic = pService->createCharacteristic(NOTIFICATION_CHAR_UUID, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY);
  pNotificationCharacteristic->setCallbacks(new NotificationCharacteristicCallbacks());
  pNotificationCharacteristic->addDescriptor(new BLE2902());

  pTripListCharacteristic = pService->createCharacteristic(TRIP_LIST_CHAR_UUID, BLECharacteristic::PROPERTY_NOTIFY);
  pTripListCharacteristic->addDescriptor(new BLE2902());

  pTripControlCharacteristic = pService->createCharacteristic(TRIP_CONTROL_CHAR_UUID, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY);
  pTripControlCharacteristic->setCallbacks(new TripControlCharacteristicCallbacks());
  pTripControlCharacteristic->addDescriptor(new BLE2902());

  pNavigateHomeCharacteristic = pService->createCharacteristic(NAVIGATE_HOME_CHAR_UUID, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY);
  pNavigateHomeCharacteristic->setCallbacks(new NavigateHomeCharacteristicCallbacks());
  pNavigateHomeCharacteristic->addDescriptor(new BLE2902());

  pDeviceStatusCharacteristic = pService->createCharacteristic(DEVICE_STATUS_CHAR_UUID, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY);
  pDeviceStatusCharacteristic->setCallbacks(new DeviceStatusCharacteristicCallbacks());
  pDeviceStatusCharacteristic->addDescriptor(new BLE2902());

  pRecordingListCharacteristic = pService->createCharacteristic(RECORDING_LIST_CHAR_UUID, BLECharacteristic::PROPERTY_NOTIFY);
  pRecordingListCharacteristic->addDescriptor(new BLE2902());

  pRecordingControlCharacteristic = pService->createCharacteristic(RECORDING_CONTROL_CHAR_UUID, BLECharacteristic::PROPERTY_WRITE);
  pRecordingControlCharacteristic->setCallbacks(new RecordingControlCharacteristicCallbacks());

  pRecordingTransferCharacteristic = pService->createCharacteristic(RECORDING_TRANSFER_CHAR_UUID, BLECharacteristic::PROPERTY_NOTIFY);
  pRecordingTransferCharacteristic->addDescriptor(new BLE2902());

  pService->start();
  
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06); pAdvertising->setMaxPreferred(0x12);
  BLEDevice::startAdvertising();
  delay(15);

  BLEDevice::setPower(ESP_PWR_LVL_P9); delay(20);
  digitalWrite(17, HIGH); delay(100);
  setCpuFrequencyMhz(originalCpuFreq); delay(100);
  bleInitialized = true;
  bleShutdownInProgress = false;
}

void stopBLE() {
  if (!bleInitialized && pServer == nullptr) {
    return;
  }

  Serial.println("Disabling Bluetooth/BLE...");
  bleShutdownInProgress = true;

  if (deviceConnected && pServer != nullptr) {
    Serial.println("BLE device connected - disconnecting first...");
    uint16_t connId = pServer->getConnId();
    pServer->disconnect(connId);

    unsigned long disconnectStart = millis();
    while (deviceConnected && (millis() - disconnectStart < 2000)) {
      delay(10);
    }

    if (deviceConnected) {
      Serial.println("WARNING: Device still connected after timeout");
    } else {
      Serial.println("Device disconnected successfully");
    }

    delay(100);
  }

  if (pServer != nullptr) {
    BLEAdvertising *pAdvertising = pServer->getAdvertising();
    if (pAdvertising != nullptr) {
      pAdvertising->stop();
    }
  }

  if (!deviceConnected) {
    resetBleRuntimeState();
  }

  BLEDevice::setPower(ESP_PWR_LVL_N12);
  bleShutdownInProgress = false;
  Serial.println("Bluetooth/BLE disabled");
}

void startBLE() {
  Serial.println("Enabling Bluetooth/BLE...");
  if (bleInitialized) {
    bleShutdownInProgress = false;
    BLEDevice::setPower(ESP_PWR_LVL_N12); delay(15);
    if (pServer != nullptr) {
      BLEAdvertising *pAdvertising = pServer->getAdvertising();
      if (pAdvertising != nullptr) {
        pAdvertising->start();
      }
    } else {
      BLEDevice::startAdvertising();
    }
    delay(15);
    BLEDevice::setPower(ESP_PWR_LVL_P9); delay(20);
    Serial.println("Bluetooth/BLE enabled");
    return;
  }

  initBLE();
}

void initSDCardFolders() {
  if (!SD.exists(MAP_DIR)) SD.mkdir(MAP_DIR);
  if (!SD.exists(TRIPS_DIR)) SD.mkdir(TRIPS_DIR);
}

// OPTIMIZED SAVE TILE TO SD WITH CONTENTION RETRY
bool saveTileToSD(int zoom, int tileX, int tileY, uint8_t* data, uint32_t size) {
  char zoomPath[32]; sprintf(zoomPath, "%s/%d", MAP_DIR, zoom);
  char xPath[48]; sprintf(xPath, "%s/%d", zoomPath, tileX);
  char tilePath[64]; sprintf(tilePath, "%s/%d.bin", xPath, tileY);

  // 1. Blindly remove the file if it exists.
  // We do NOT check SD.exists() first to save a file handle.
  SD.remove(tilePath);

  File file;
  
  // 2. Attempt to open file. 
  // If the Display is refreshing (SPI busy) or map is rendering (Handle busy), this might fail.
  // We use a retry loop to bridge the gap of the display update (which takes ~100-200ms).
  for (int i = 0; i < 3; i++) {
    file = SD.open(tilePath, FILE_WRITE);
    if (file) break;

    // Open failed. Wait a bit for other tasks to release SPI/Handles.
    delay(50);
  }

  // 3. If still failed, it might be that directories are missing (first time setup).
  if (!file) {
    if (!SD.exists(zoomPath)) SD.mkdir(zoomPath);
    if (!SD.exists(xPath)) SD.mkdir(xPath);
    
    // One last try
    file = SD.open(tilePath, FILE_WRITE);
  }

  if (file) {
    file.write(data, size);
    file.close(); // CRITICAL
    appendTileIndexRecord((uint8_t)zoom, (uint32_t)tileX, (uint32_t)tileY);
    return true;
  } else {
    Serial.printf("ERROR: Failed to open tile %d/%d/%d for writing after retries\n", zoom, tileX, tileY);
    return false;
  }
}

void saveTripToSD(const char* fileName, uint8_t* gpxData, uint32_t gpxSize, uint8_t* metaData, uint32_t metaSize) {
  char tripPath[64]; sprintf(tripPath, "%s/%s", TRIPS_DIR, fileName);
  if (!SD.exists(tripPath)) SD.mkdir(tripPath);
  char gpxPath[80]; sprintf(gpxPath, "%s/%s.gpx", tripPath, fileName);
  File gpxFile = SD.open(gpxPath, FILE_WRITE);
  if (gpxFile) { gpxFile.write(gpxData, gpxSize); gpxFile.close(); }
  char metaPath[96]; sprintf(metaPath, "%s/%s_meta.json", tripPath, fileName);
  File metaFile = SD.open(metaPath, FILE_WRITE);
  if (metaFile) { metaFile.write(metaData, metaSize); metaFile.close(); }
}

uint8_t* loadTileFromSD(int zoom, int tileX, int tileY, uint32_t* outSize) {
  char tilePath[64]; sprintf(tilePath, "%s/%d/%d/%d.bin", MAP_DIR, zoom, tileX, tileY);
  if (!SD.exists(tilePath)) return nullptr;
  File file = SD.open(tilePath, FILE_READ);
  if (!file) return nullptr;
  *outSize = file.size();
  uint8_t* data = (uint8_t*)malloc(*outSize);
  if (data) file.read(data, *outSize);
  file.close();
  return data;
}

const char* getBaseName(const char* path) {
  const char* slash = strrchr(path, '/');
  return slash ? slash + 1 : path;
}

bool parseIntFromName(const char* name, int* outValue) {
  if (!name || !outValue) return false;
  char* endPtr = nullptr;
  long value = strtol(name, &endPtr, 10);
  if (endPtr == name || value < 0 || value > INT32_MAX) return false;
  *outValue = (int)value;
  return true;
}

void writeTileIndexRecord(File& file, uint8_t zoom, uint32_t tileX, uint32_t tileY) {
  uint8_t record[TILE_INV_RECORD_SIZE];
  record[0] = zoom;
  record[1] = (tileX >> 24) & 0xFF;
  record[2] = (tileX >> 16) & 0xFF;
  record[3] = (tileX >> 8) & 0xFF;
  record[4] = tileX & 0xFF;
  record[5] = (tileY >> 24) & 0xFF;
  record[6] = (tileY >> 16) & 0xFF;
  record[7] = (tileY >> 8) & 0xFF;
  record[8] = tileY & 0xFF;
  file.write(record, sizeof(record));
}

void appendTileIndexRecord(uint8_t zoom, uint32_t tileX, uint32_t tileY) {
  File indexFile = SD.open(MAP_INDEX_PATH, FILE_APPEND);
  if (!indexFile) {
    delay(5);
    indexFile = SD.open(MAP_INDEX_PATH, FILE_APPEND);
  }
  if (indexFile) {
    writeTileIndexRecord(indexFile, zoom, tileX, tileY);
    indexFile.close();
  }
}

bool rebuildTileIndex() {
  File mapDir = SD.open(MAP_DIR);
  if (!mapDir) return false;

  SD.remove(MAP_INDEX_PATH);
  File indexFile = SD.open(MAP_INDEX_PATH, FILE_WRITE);
  if (!indexFile) { mapDir.close(); return false; }

  int recordsWritten = 0;
  File zoomEntry = mapDir.openNextFile();
  while (zoomEntry) {
    if (zoomEntry.isDirectory()) {
      const char* zoomName = getBaseName(zoomEntry.name());
      int zoom = -1;
      if (parseIntFromName(zoomName, &zoom) && zoom >= 0 && zoom <= 20) {
        File xEntry = zoomEntry.openNextFile();
        while (xEntry) {
          if (xEntry.isDirectory()) {
            const char* xName = getBaseName(xEntry.name());
            int tileX = -1;
            if (parseIntFromName(xName, &tileX)) {
              File tileEntry = xEntry.openNextFile();
              while (tileEntry) {
                if (!tileEntry.isDirectory()) {
                  const char* tileName = getBaseName(tileEntry.name());
                  int tileY = -1;
                  if (parseIntFromName(tileName, &tileY)) {
                    writeTileIndexRecord(indexFile, (uint8_t)zoom, (uint32_t)tileX, (uint32_t)tileY);
                    recordsWritten++;
                    if ((recordsWritten % 200) == 0) delay(1);
                  }
                }
                tileEntry.close();
                tileEntry = xEntry.openNextFile();
              }
            }
          }
          xEntry.close();
          xEntry = zoomEntry.openNextFile();
        }
      }
    }
    zoomEntry.close();
    zoomEntry = mapDir.openNextFile();
  }

  indexFile.close();
  mapDir.close();
  Serial.printf("Tile index rebuilt: %d records\n", recordsWritten);
  return true;
}

void sendTileInventoryError() {
  if (tileInventoryFile) tileInventoryFile.close();
  tileInventorySending = false;
  if (!deviceConnected || pTripControlCharacteristic == nullptr) return;
  uint8_t packet[2] = {TILE_INV_ACTION_ERROR, 0x01};
  pTripControlCharacteristic->setValue(packet, sizeof(packet));
  pTripControlCharacteristic->notify();
}

void startTileInventorySend() {
  if (!deviceConnected || pTripControlCharacteristic == nullptr) return;
  if (tileInventorySending) return;

  if (!SD.exists(MAP_INDEX_PATH)) {
    Serial.println("Tile index missing, rebuilding...");
    if (!rebuildTileIndex()) {
      Serial.println("Tile index rebuild failed");
      sendTileInventoryError();
      return;
    }
  }

  tileInventoryFile = SD.open(MAP_INDEX_PATH, FILE_READ);
  if (!tileInventoryFile) {
    sendTileInventoryError();
    return;
  }

  tileInventoryFileSize = tileInventoryFile.size();
  tileInventoryBytesSent = 0;
  tileInventorySending = true;
  tileInventoryLastSendMs = 0;

  uint32_t totalRecords = tileInventoryFileSize / TILE_INV_RECORD_SIZE;
  uint8_t startPacket[5];
  startPacket[0] = TILE_INV_ACTION_START;
  startPacket[1] = (totalRecords >> 24) & 0xFF;
  startPacket[2] = (totalRecords >> 16) & 0xFF;
  startPacket[3] = (totalRecords >> 8) & 0xFF;
  startPacket[4] = totalRecords & 0xFF;
  pTripControlCharacteristic->setValue(startPacket, sizeof(startPacket));
  pTripControlCharacteristic->notify();
}

void finishTileInventorySend() {
  if (tileInventoryFile) tileInventoryFile.close();
  tileInventorySending = false;
  uint8_t endPacket[1] = {TILE_INV_ACTION_END};
  if (deviceConnected && pTripControlCharacteristic != nullptr) {
    pTripControlCharacteristic->setValue(endPacket, sizeof(endPacket));
    pTripControlCharacteristic->notify();
  }
}

void updateTileInventorySend() {
  if (!tileInventorySending || !deviceConnected || pTripControlCharacteristic == nullptr) return;
  unsigned long now = millis();
  if (now - tileInventoryLastSendMs < TILE_INV_CHUNK_INTERVAL_MS) return;

  if (!tileInventoryFile) {
    sendTileInventoryError();
    tileInventorySending = false;
    return;
  }

  uint32_t remaining = tileInventoryFileSize - tileInventoryBytesSent;
  uint32_t maxBytes = TILE_INV_MAX_RECORDS_PER_CHUNK * TILE_INV_RECORD_SIZE;
  uint32_t bytesToRead = (remaining < maxBytes) ? remaining : maxBytes;
  bytesToRead -= (bytesToRead % TILE_INV_RECORD_SIZE);

  if (bytesToRead == 0) {
    finishTileInventorySend();
    return;
  }

  uint8_t buffer[1 + TILE_INV_MAX_RECORDS_PER_CHUNK * TILE_INV_RECORD_SIZE];
  buffer[0] = TILE_INV_ACTION_DATA;
  int bytesRead = tileInventoryFile.read(buffer + 1, bytesToRead);
  if (bytesRead <= 0) {
    finishTileInventorySend();
    return;
  }

  tileInventoryBytesSent += bytesRead;
  pTripControlCharacteristic->setValue(buffer, 1 + bytesRead);
  pTripControlCharacteristic->notify();
  tileInventoryLastSendMs = now;

  if (tileInventoryBytesSent >= tileInventoryFileSize) {
    finishTileInventorySend();
  }
}

void sendRecordingTransferError(const char* message) {
  if (recordingMetaFile) recordingMetaFile.close();
  if (recordingGpxFile) recordingGpxFile.close();
  recordingTransferSending = false;
  if (!deviceConnected || pRecordingTransferCharacteristic == nullptr) return;

  uint8_t buffer[1 + 1 + 60];
  buffer[0] = RECORDING_TRANSFER_ACTION_ERROR;
  uint8_t msgLen = 0;
  if (message != nullptr) {
    size_t rawLen = strlen(message);
    if (rawLen > 60) rawLen = 60;
    msgLen = (uint8_t)rawLen;
  }
  buffer[1] = msgLen;
  if (msgLen > 0) {
    memcpy(buffer + 2, message, msgLen);
  }
  pRecordingTransferCharacteristic->setValue(buffer, 2 + msgLen);
  pRecordingTransferCharacteristic->notify();
}

void startRecordingTransfer(const char* recordingDirName) {
  if (!deviceConnected || pRecordingTransferCharacteristic == nullptr) return;
  if (recordingTransferSending) return;
  if (recordingDirName == nullptr || recordingDirName[0] == '\0') {
    sendRecordingTransferError("Invalid recording");
    return;
  }

  char metaPath[128];
  char gpxPath[128];
  snprintf(metaPath, sizeof(metaPath), "%s/%s/%s_meta.json", RECORDINGS_DIR, recordingDirName, recordingDirName);
  snprintf(gpxPath, sizeof(gpxPath), "%s/%s/%s.gpx", RECORDINGS_DIR, recordingDirName, recordingDirName);

  recordingMetaFile = SD.open(metaPath, FILE_READ);
  recordingGpxFile = SD.open(gpxPath, FILE_READ);

  if (!recordingGpxFile) {
    if (recordingMetaFile) recordingMetaFile.close();
    sendRecordingTransferError("Recording not found");
    return;
  }

  recordingMetaSize = recordingMetaFile ? recordingMetaFile.size() : 0;
  recordingGpxSize = recordingGpxFile.size();
  recordingBytesSent = 0;
  recordingTransferSending = true;
  recordingTransferLastSendMs = 0;

  uint8_t nameLen = strlen(recordingDirName);
  if (nameLen >= sizeof(pendingRecordingName)) {
    nameLen = sizeof(pendingRecordingName) - 1;
  }

  uint8_t startPacket[1 + 1 + 64 + 4 + 4];
  uint32_t index = 0;
  startPacket[index++] = RECORDING_TRANSFER_ACTION_START;
  startPacket[index++] = nameLen;
  memcpy(startPacket + index, recordingDirName, nameLen);
  index += nameLen;
  startPacket[index++] = (recordingMetaSize >> 24) & 0xFF;
  startPacket[index++] = (recordingMetaSize >> 16) & 0xFF;
  startPacket[index++] = (recordingMetaSize >> 8) & 0xFF;
  startPacket[index++] = recordingMetaSize & 0xFF;
  startPacket[index++] = (recordingGpxSize >> 24) & 0xFF;
  startPacket[index++] = (recordingGpxSize >> 16) & 0xFF;
  startPacket[index++] = (recordingGpxSize >> 8) & 0xFF;
  startPacket[index++] = recordingGpxSize & 0xFF;

  pRecordingTransferCharacteristic->setValue(startPacket, index);
  pRecordingTransferCharacteristic->notify();
}

void finishRecordingTransfer() {
  if (recordingMetaFile) recordingMetaFile.close();
  if (recordingGpxFile) recordingGpxFile.close();
  recordingTransferSending = false;

  if (!deviceConnected || pRecordingTransferCharacteristic == nullptr) return;
  uint8_t endPacket[1] = {RECORDING_TRANSFER_ACTION_END};
  pRecordingTransferCharacteristic->setValue(endPacket, sizeof(endPacket));
  pRecordingTransferCharacteristic->notify();
}

void updateRecordingTransferSend() {
  if (!recordingTransferSending || !deviceConnected || pRecordingTransferCharacteristic == nullptr) return;
  unsigned long now = millis();
  if (now - recordingTransferLastSendMs < RECORDING_TRANSFER_CHUNK_INTERVAL_MS) return;

  uint32_t totalSize = recordingMetaSize + recordingGpxSize;
  if (recordingBytesSent >= totalSize) {
    finishRecordingTransfer();
    return;
  }

  uint32_t remaining = totalSize - recordingBytesSent;
  uint32_t bytesToRead = (remaining < RECORDING_TRANSFER_CHUNK_SIZE) ? remaining : RECORDING_TRANSFER_CHUNK_SIZE;

  uint8_t buffer[1 + RECORDING_TRANSFER_CHUNK_SIZE];
  buffer[0] = RECORDING_TRANSFER_ACTION_DATA;

  int bytesRead = 0;
  if (recordingBytesSent < recordingMetaSize) {
    uint32_t metaRemaining = recordingMetaSize - recordingBytesSent;
    uint32_t metaToRead = (metaRemaining < bytesToRead) ? metaRemaining : bytesToRead;
    bytesRead = recordingMetaFile ? recordingMetaFile.read(buffer + 1, metaToRead) : 0;
  } else {
    uint32_t gpxOffset = recordingBytesSent - recordingMetaSize;
    uint32_t gpxRemaining = recordingGpxSize - gpxOffset;
    uint32_t gpxToRead = (gpxRemaining < bytesToRead) ? gpxRemaining : bytesToRead;
    bytesRead = recordingGpxFile ? recordingGpxFile.read(buffer + 1, gpxToRead) : 0;
  }

  if (bytesRead <= 0) {
    sendRecordingTransferError("Read failed");
    return;
  }

  recordingBytesSent += bytesRead;
  pRecordingTransferCharacteristic->setValue(buffer, 1 + bytesRead);
  pRecordingTransferCharacteristic->notify();
  recordingTransferLastSendMs = now;

  if (recordingBytesSent >= totalSize) {
    finishRecordingTransfer();
  }
}

void sendNotificationDismissal(uint32_t notificationId) {
  if (!deviceConnected || pNotificationCharacteristic == nullptr) return;
  uint8_t packet[5];
  packet[0] = 0x02; packet[1] = (notificationId >> 24) & 0xFF; packet[2] = (notificationId >> 16) & 0xFF;
  packet[3] = (notificationId >> 8) & 0xFF; packet[4] = notificationId & 0xFF;
  pNotificationCharacteristic->setValue(packet, 5); pNotificationCharacteristic->notify();
}

struct BleTripSortEntry {
  char dirName[64];
  uint64_t createdAt;
};

static int compareBleTripSortEntryByCreatedAtDesc(const void* left, const void* right) {
  const BleTripSortEntry* a = static_cast<const BleTripSortEntry*>(left);
  const BleTripSortEntry* b = static_cast<const BleTripSortEntry*>(right);
  if (a->createdAt < b->createdAt) return 1;
  if (a->createdAt > b->createdAt) return -1;
  return strcmp(a->dirName, b->dirName);
}

void scanAndSendTripList() {
  if (!deviceConnected || pTripListCharacteristic == nullptr) return;
  File tripsDir = SD.open(TRIPS_DIR);
  if (!tripsDir) {
    uint8_t emptyList[2] = {0x00, 0x00}; pTripListCharacteristic->setValue(emptyList, 2); pTripListCharacteristic->notify(); return;
  }
  int tripCount = 0;
  File entry = tripsDir.openNextFile();
  while (entry) {
    if (entry.isDirectory() && strcmp(entry.name(), "_nav_home_temp") != 0) tripCount++;
    entry.close(); entry = tripsDir.openNextFile();
  }
  tripsDir.close();

  uint8_t* buffer = (uint8_t*)malloc(512);
  if (!buffer) return;
  uint32_t bufferIndex = 2;

  if (tripCount == 0) {
    buffer[0] = 0x00; buffer[1] = 0x00;
    pTripListCharacteristic->setValue(buffer, bufferIndex); pTripListCharacteristic->notify(); free(buffer); return;
  }

  BleTripSortEntry* entries = (BleTripSortEntry*)malloc(sizeof(BleTripSortEntry) * tripCount);
  if (!entries) {
    tripsDir = SD.open(TRIPS_DIR);
    if (tripsDir) {
      entry = tripsDir.openNextFile();
      int tripsAdded = 0;
      while (entry && tripsAdded < tripCount) {
        if (entry.isDirectory()) {
          const char* tripName = entry.name();
          if (strcmp(tripName, "_nav_home_temp") != 0) {
            uint8_t nameLen = strlen(tripName);
            if (bufferIndex + 1 + nameLen > 500) break;
            buffer[bufferIndex++] = nameLen;
            memcpy(buffer + bufferIndex, tripName, nameLen);
            bufferIndex += nameLen;
            tripsAdded++;
          }
        }
        entry.close(); entry = tripsDir.openNextFile();
      }
      tripsDir.close();
      buffer[0] = (tripsAdded >> 8) & 0xFF; buffer[1] = tripsAdded & 0xFF;
    } else {
      buffer[0] = 0x00; buffer[1] = 0x00;
    }
    pTripListCharacteristic->setValue(buffer, bufferIndex); pTripListCharacteristic->notify(); free(buffer); return;
  }

  tripsDir = SD.open(TRIPS_DIR);
  int entryCount = 0;
  if (tripsDir) {
    entry = tripsDir.openNextFile();
    while (entry && entryCount < tripCount) {
      if (entry.isDirectory()) {
        const char* tripName = entry.name();
        if (strcmp(tripName, "_nav_home_temp") != 0) {
          strncpy(entries[entryCount].dirName, tripName, sizeof(entries[entryCount].dirName) - 1);
          entries[entryCount].dirName[sizeof(entries[entryCount].dirName) - 1] = '\0';
          uint64_t createdAt = 0;
          readTripListMetadata(tripName, nullptr, 0, &createdAt);
          entries[entryCount].createdAt = createdAt;
          entryCount++;
        }
      }
      entry.close(); entry = tripsDir.openNextFile();
    }
    tripsDir.close();
  }

  if (entryCount <= 0) {
    free(entries);
    buffer[0] = 0x00; buffer[1] = 0x00;
    pTripListCharacteristic->setValue(buffer, bufferIndex); pTripListCharacteristic->notify(); free(buffer); return;
  }

  qsort(entries, entryCount, sizeof(BleTripSortEntry), compareBleTripSortEntryByCreatedAtDesc);

  int tripsAdded = 0;
  for (int i = 0; i < entryCount; i++) {
    uint8_t nameLen = strlen(entries[i].dirName);
    if (bufferIndex + 1 + nameLen > 500) break;
    buffer[bufferIndex++] = nameLen;
    memcpy(buffer + bufferIndex, entries[i].dirName, nameLen);
    bufferIndex += nameLen;
    tripsAdded++;
  }
  buffer[0] = (tripsAdded >> 8) & 0xFF; buffer[1] = tripsAdded & 0xFF;
  free(entries);
  pTripListCharacteristic->setValue(buffer, bufferIndex); pTripListCharacteristic->notify(); free(buffer);
}

void scanAndSendRecordingList() {
  if (!deviceConnected || pRecordingListCharacteristic == nullptr) return;

  File recordingsDir = SD.open(RECORDINGS_DIR);
  if (!recordingsDir) {
    uint8_t emptyBuffer[2] = {0x00, 0x00};
    pRecordingListCharacteristic->setValue(emptyBuffer, sizeof(emptyBuffer));
    pRecordingListCharacteristic->notify();
    return;
  }

  int recordingCount = 0;
  File entry = recordingsDir.openNextFile();
  while (entry) {
    if (entry.isDirectory()) recordingCount++;
    entry.close(); entry = recordingsDir.openNextFile();
  }
  recordingsDir.close();

  uint8_t* buffer = (uint8_t*)malloc(512);
  if (!buffer) return;
  buffer[0] = (recordingCount >> 8) & 0xFF; buffer[1] = recordingCount & 0xFF;
  uint32_t bufferIndex = 2;

  recordingsDir = SD.open(RECORDINGS_DIR);
  if (recordingsDir) {
    entry = recordingsDir.openNextFile();
    int recordingsAdded = 0;
    while (entry && recordingsAdded < recordingCount) {
      if (entry.isDirectory()) {
        const char* recordingName = entry.name();
        uint8_t nameLen = strlen(recordingName);
        if (bufferIndex + 1 + nameLen > 500) break;
        buffer[bufferIndex++] = nameLen;
        memcpy(buffer + bufferIndex, recordingName, nameLen);
        bufferIndex += nameLen;
        recordingsAdded++;
      }
      entry.close(); entry = recordingsDir.openNextFile();
    }
    recordingsDir.close();
    if (recordingsAdded < recordingCount) { buffer[0] = (recordingsAdded >> 8) & 0xFF; buffer[1] = recordingsAdded & 0xFF; }
  }

  pRecordingListCharacteristic->setValue(buffer, bufferIndex); pRecordingListCharacteristic->notify(); free(buffer);
}

void sendActiveTripUpdate() {
  if (!deviceConnected || pTripControlCharacteristic == nullptr) return;
  extern char activeNavigationTrip[64]; extern bool navigationActive;
  uint8_t buffer[256]; uint8_t* ptr = buffer; *ptr++ = 0x02;
  if (navigationActive && activeNavigationTrip[0] != '\0') {
    uint8_t nameLen = strlen(activeNavigationTrip); *ptr++ = nameLen;
    memcpy(ptr, activeNavigationTrip, nameLen); ptr += nameLen;
  } else { *ptr++ = 0x00; }
  pTripControlCharacteristic->setValue(buffer, ptr - buffer); pTripControlCharacteristic->notify();
}

bool loadAndStartTripByName(const char* tripName) {
  extern void startTripNavigation(const char* tripDirName);
  extern bool parseAndLoadGPX(const char* tripDirName);
  extern char loadedTrackName[64];
  if (parseAndLoadGPX(tripName)) {
    strncpy(loadedTrackName, tripName, sizeof(loadedTrackName) - 1); loadedTrackName[sizeof(loadedTrackName) - 1] = '\0';
    startTripNavigation(tripName);
    pendingPageNavigation = true; pendingNavigationPage = PAGE_MAP;
    return true;
  }
  return false;
}

void updateBleHandler() {
  if (deviceConnected && !tripListSent) {
    if (clientFullyReady || (millis() - connectionTime > 3000)) { scanAndSendTripList(); tripListSent = true; }
  }
  if (deviceConnected && !activeTripSent) {
    if (clientFullyReady || (millis() - connectionTime > 3000)) { sendActiveTripUpdate(); activeTripSent = true; }
  }
  if (deviceConnected && tileInventoryRequestPending) {
    tileInventoryRequestPending = false;
    if (!tileInventorySending) startTileInventorySend();
  }
  if (deviceConnected && tileInventorySending) {
    updateTileInventorySend();
  }
  if (deviceConnected && recordingListPending) {
    recordingListPending = false;
    scanAndSendRecordingList();
  }
  if (deviceConnected && recordingTransferPending) {
    recordingTransferPending = false;
    startRecordingTransfer(pendingRecordingName);
  }
  if (deviceConnected && recordingTransferSending) {
    updateRecordingTransferSend();
  }
  // Send ESP32 device status periodically (only when app is in foreground)
  if (deviceConnected && periodicStatusUpdatesEnabled && (millis() - lastDeviceStatusSendTime >= DEVICE_STATUS_SEND_INTERVAL)) {
    sendEspDeviceStatus();
    lastDeviceStatusSendTime = millis();
  }
}

#endif // BLE_HANDLER_H
