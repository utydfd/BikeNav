#ifndef MAP_TRIPS_H
#define MAP_TRIPS_H

#include <GxEPD2_BW.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <SD.h>
#include <ArduinoJson.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <math.h>

// External references from main program
extern GxEPD2_BW<GxEPD2_290_BS, GxEPD2_290_BS::HEIGHT> display;
extern U8G2_FOR_ADAFRUIT_GFX u8g2_display;
extern const int DISPLAY_WIDTH;
extern const int DISPLAY_HEIGHT;
extern bool deviceConnected;

// From ble_handler.h
void requestNavigateHome();
extern bool navigateHomeHasError;
extern char navigateHomeErrorMessage[64];
extern unsigned long navigateHomeRequestTime;

// External UI functions from page_map.h
extern void drawPageDots();

// External tile rendering functions from map_rendering.h
extern void getTileCoordinates(double lat, double lon, int zoom, int* tileX, int* tileY, double* pixelX, double* pixelY);
extern uint8_t* tileCacheLookup(int zoom, int tileX, int tileY);
extern uint8_t* tileCacheInsert(int zoom, int tileX, int tileY);

// --- TRIP DETAIL STATE ---
char selectedTripDirName[64] = "";  // Directory name of selected trip

// Trip detail button selection (0 = Start, 1 = Delete)
int selectedTripButton = 0;
bool showDeleteConfirmation = false;

// Navigate Home detail view state
bool isNavigateHomeMode = false;      // True when viewing Navigate Home detail
bool navigateHomePathLoaded = false;  // True when Navigate Home path has been loaded
bool waitingForNavigateHomePath = false;  // True when waiting for BLE to deliver Navigate Home path
bool showNavigateHomeError = false;   // True when showing error dialog

// Navigate Home timeout (30 seconds)
const unsigned long NAVIGATE_HOME_TIMEOUT_MS = 30000;

// Flag for deferred trip detail rendering (set by BLE callbacks, cleared by render)
bool tripDetailNeedsRedraw = false;

// --- GPX TRACK DATA STRUCTURES ---
// Packed structure for efficient PSRAM storage (10 bytes per point)
struct __attribute__((packed)) TrackPoint {
  float lat;       // 4 bytes - latitude in decimal degrees
  float lon;       // 4 bytes - longitude in decimal degrees
  int16_t elev;    // 2 bytes - elevation in integer meters (-32768 to +32767)
};

// Global variables for loaded GPX track
TrackPoint* loadedTrack = nullptr;  // PSRAM-allocated track points
int loadedTrackPointCount = 0;      // Number of points in loaded track
char loadedTrackName[64] = "";      // Name of currently loaded trip

// --- FUNCTION PROTOTYPES ---
int countTripsOnSD();
bool getTripNameByIndex(int index, char* outName, size_t maxLen);
bool getTripDirNameByIndex(int index, char* outName, size_t maxLen);
bool readTripMetadata(const char* tripDirName, StaticJsonDocument<512>& doc);
bool readTripNameFromMetadata(const char* tripDirName, char* outName, size_t maxLen);
void deleteDirectory(File dir);
bool deleteTripFromSD(const char* tripDirName);
void drawDeleteConfirmationDialog();
void renderTripDetailView();
// GPX parsing and memory management
bool parseAndLoadGPX(const char* tripDirName);
bool parseAndLoadGPXFromMemory(const char* tripName, const uint8_t* gpxData, uint32_t gpxSize);
void freeLoadedTrack();
bool loadTripForDetails(const char* tripDirName);
// Map preview functions
void calculateTrackBoundingBox(double* minLat, double* maxLat, double* minLon, double* maxLon);
int calculateBestZoomLevel(double minLat, double maxLat, double minLon, double maxLon, int displayWidth, int displayHeight, int margin);
void renderTripMapPreview(int x, int y, int width, int height);

// --- IMPLEMENTATIONS ---

// Helper to count trips on SD card
int countTripsOnSD() {
  File tripsDir = SD.open("/Trips");
  if (!tripsDir) return 0;

  int count = 0;
  File entry = tripsDir.openNextFile();
  while (entry) {
    if (entry.isDirectory()) {
      count++;
    }
    entry.close();
    entry = tripsDir.openNextFile();
  }
  tripsDir.close();
  return count;
}

// Helper to read trip list metadata (name + createdAt) from JSON
bool readTripListMetadata(const char* tripDirName, char* outName, size_t maxLen, uint64_t* outCreatedAt) {
  if (outCreatedAt) *outCreatedAt = 0;
  if (outName && maxLen > 0) outName[0] = '\0';

  char metaPath[96];
  snprintf(metaPath, sizeof(metaPath), "/Trips/%s/%s_meta.json", tripDirName, tripDirName);

  File metaFile = SD.open(metaPath, FILE_READ);
  if (!metaFile) {
    // If metadata doesn't exist, fallback to directory name
    if (outName && maxLen > 0) {
      strncpy(outName, tripDirName, maxLen - 1);
      outName[maxLen - 1] = '\0';
    }
    return false;
  }

  // Read the JSON file
  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, metaFile);
  metaFile.close();

  if (error) {
    // If JSON parsing fails, fallback to directory name
    if (outName && maxLen > 0) {
      strncpy(outName, tripDirName, maxLen - 1);
      outName[maxLen - 1] = '\0';
    }
    return false;
  }

  // Extract the "name" field from JSON
  bool hasName = false;
  const char* tripName = doc["name"];
  if (tripName) {
    hasName = true;
    if (outName && maxLen > 0) {
      strncpy(outName, tripName, maxLen - 1);
      outName[maxLen - 1] = '\0';
    }
  } else {
    // If "name" field doesn't exist, fallback to directory name
    if (outName && maxLen > 0) {
      strncpy(outName, tripDirName, maxLen - 1);
      outName[maxLen - 1] = '\0';
    }
  }

  if (outCreatedAt) {
    *outCreatedAt = doc["createdAt"] | 0ULL;
  }

  return hasName;
}

// Helper to read trip name from metadata JSON
bool readTripNameFromMetadata(const char* tripDirName, char* outName, size_t maxLen) {
  return readTripListMetadata(tripDirName, outName, maxLen, nullptr);
}

struct TripDirSortEntry {
  char dirName[64];
  uint64_t createdAt;
};

static int compareTripDirSortEntryByCreatedAtDesc(const void* left, const void* right) {
  const TripDirSortEntry* a = static_cast<const TripDirSortEntry*>(left);
  const TripDirSortEntry* b = static_cast<const TripDirSortEntry*>(right);
  if (a->createdAt < b->createdAt) return 1;
  if (a->createdAt > b->createdAt) return -1;
  return strcmp(a->dirName, b->dirName);
}

// Helper to get trip name by index (newest first)
bool getTripNameByIndex(int index, char* outName, size_t maxLen) {
  char dirName[64];
  if (!getTripDirNameByIndex(index, dirName, sizeof(dirName))) return false;
  return readTripNameFromMetadata(dirName, outName, maxLen);
}

// Helper to get trip directory name by index (newest first)
bool getTripDirNameByIndex(int index, char* outName, size_t maxLen) {
  if (index < 0 || !outName || maxLen == 0) return false;

  int tripCount = countTripsOnSD();
  if (tripCount <= 0) return false;

  TripDirSortEntry* entries = (TripDirSortEntry*)malloc(sizeof(TripDirSortEntry) * tripCount);
  if (!entries) return false;

  File tripsDir = SD.open("/Trips");
  if (!tripsDir) {
    free(entries);
    return false;
  }

  int entryCount = 0;
  File entry = tripsDir.openNextFile();
  while (entry && entryCount < tripCount) {
    if (entry.isDirectory()) {
      const char* dirName = entry.name();
      strncpy(entries[entryCount].dirName, dirName, sizeof(entries[entryCount].dirName) - 1);
      entries[entryCount].dirName[sizeof(entries[entryCount].dirName) - 1] = '\0';
      uint64_t createdAt = 0;
      readTripListMetadata(dirName, nullptr, 0, &createdAt);
      entries[entryCount].createdAt = createdAt;
      entryCount++;
    }
    entry.close();
    entry = tripsDir.openNextFile();
  }
  tripsDir.close();

  if (entryCount <= 0 || index >= entryCount) {
    free(entries);
    return false;
  }

  qsort(entries, entryCount, sizeof(TripDirSortEntry), compareTripDirSortEntryByCreatedAtDesc);
  strncpy(outName, entries[index].dirName, maxLen - 1);
  outName[maxLen - 1] = '\0';
  free(entries);
  return true;
}

// Helper to read full trip metadata JSON
bool readTripMetadata(const char* tripDirName, StaticJsonDocument<512>& doc) {
  char metaPath[96];
  snprintf(metaPath, sizeof(metaPath), "/Trips/%s/%s_meta.json", tripDirName, tripDirName);

  File metaFile = SD.open(metaPath, FILE_READ);
  if (!metaFile) {
    return false;
  }

  DeserializationError error = deserializeJson(doc, metaFile);
  metaFile.close();

  if (error) {
    return false;
  }

  return true;
}

// --- GPX PARSING AND MEMORY MANAGEMENT ---

// Free currently loaded track from PSRAM
void freeLoadedTrack() {
  if (loadedTrack != nullptr) {
    free(loadedTrack);  // ps_malloc'd memory is freed with regular free()
    loadedTrack = nullptr;
  }
  loadedTrackPointCount = 0;
  loadedTrackName[0] = '\0';
  Serial.println("Freed loaded track from PSRAM");
}

// Parse GPX file and load track points into PSRAM
// Returns true on success, false on failure
bool parseAndLoadGPX(const char* tripDirName) {
  unsigned long parseStartTime = millis();

  // Construct GPX file path
  char gpxPath[96];
  snprintf(gpxPath, sizeof(gpxPath), "/Trips/%s/%s.gpx", tripDirName, tripDirName);

  Serial.printf("Parsing GPX file: %s\n", gpxPath);

  // Open GPX file
  File gpxFile = SD.open(gpxPath, FILE_READ);
  if (!gpxFile) {
    Serial.printf("ERROR: Failed to open GPX file: %s\n", gpxPath);
    return false;
  }

  size_t fileSize = gpxFile.size();
  Serial.printf("GPX file size: %d bytes\n", fileSize);

  // First pass: Count track points to know how much memory to allocate
  int pointCount = 0;
  char buffer[512];
  size_t bytesRead;

  Serial.println("First pass: counting track points...");
  while ((bytesRead = gpxFile.read((uint8_t*)buffer, sizeof(buffer) - 1)) > 0) {
    buffer[bytesRead] = '\0';  // Null-terminate

    // Count occurrences of <trkpt (each track point starts with this)
    char* pos = buffer;
    while ((pos = strstr(pos, "<trkpt")) != nullptr) {
      pointCount++;
      pos += 6;  // Move past "<trkpt"
    }
  }

  Serial.printf("Found %d track points\n", pointCount);

  if (pointCount == 0) {
    Serial.println("ERROR: No track points found in GPX file");
    gpxFile.close();
    return false;
  }

  // Allocate PSRAM for track points
  size_t bytesNeeded = pointCount * sizeof(TrackPoint);
  Serial.printf("Allocating %d bytes in PSRAM for track...\n", bytesNeeded);

  TrackPoint* track = (TrackPoint*)ps_malloc(bytesNeeded);
  if (track == nullptr) {
    Serial.println("ERROR: Failed to allocate PSRAM for track");
    gpxFile.close();
    return false;
  }

  Serial.println("PSRAM allocated successfully");

  // Second pass: Parse and store track points
  gpxFile.seek(0);  // Reset to beginning of file
  int currentPoint = 0;
  String accumulator = "";  // Accumulate partial data across buffer boundaries

  Serial.println("Second pass: parsing track points...");

  while ((bytesRead = gpxFile.read((uint8_t*)buffer, sizeof(buffer) - 1)) > 0) {
    buffer[bytesRead] = '\0';
    accumulator += buffer;

    // Process complete <trkpt> elements
    int searchStart = 0;
    while (true) {
      // Find next <trkpt tag
      int trkptStart = accumulator.indexOf("<trkpt", searchStart);
      if (trkptStart == -1) break;

      // Find closing </trkpt> tag
      int trkptEnd = accumulator.indexOf("</trkpt>", trkptStart);
      if (trkptEnd == -1) {
        // Incomplete element, keep in accumulator for next iteration
        break;
      }

      // Extract complete <trkpt>...</trkpt> element
      String trkptElement = accumulator.substring(trkptStart, trkptEnd + 8);

      // Parse latitude
      int latStart = trkptElement.indexOf("lat=\"");
      if (latStart != -1) {
        latStart += 5;  // Move past 'lat="'
        int latEnd = trkptElement.indexOf("\"", latStart);
        if (latEnd != -1) {
          String latStr = trkptElement.substring(latStart, latEnd);
          track[currentPoint].lat = latStr.toFloat();
        }
      }

      // Parse longitude
      int lonStart = trkptElement.indexOf("lon=\"");
      if (lonStart != -1) {
        lonStart += 5;  // Move past 'lon="'
        int lonEnd = trkptElement.indexOf("\"", lonStart);
        if (lonEnd != -1) {
          String lonStr = trkptElement.substring(lonStart, lonEnd);
          track[currentPoint].lon = lonStr.toFloat();
        }
      }

      // Parse elevation (optional)
      int eleStart = trkptElement.indexOf("<ele>");
      if (eleStart != -1) {
        eleStart += 5;  // Move past '<ele>'
        int eleEnd = trkptElement.indexOf("</ele>", eleStart);
        if (eleEnd != -1) {
          String eleStr = trkptElement.substring(eleStart, eleEnd);
          // Convert to integer meters (remove decimal)
          track[currentPoint].elev = (int16_t)eleStr.toFloat();
        }
      } else {
        track[currentPoint].elev = 0;  // No elevation data
      }

      currentPoint++;
      if (currentPoint >= pointCount) break;

      // Move search position past this element
      searchStart = trkptEnd + 8;
    }

    // Keep only unprocessed data in accumulator
    if (searchStart > 0) {
      accumulator = accumulator.substring(searchStart);
    }

    // Prevent accumulator from growing too large (keep last 1KB max)
    if (accumulator.length() > 1024) {
      accumulator = accumulator.substring(accumulator.length() - 1024);
    }

    if (currentPoint >= pointCount) break;
  }

  gpxFile.close();

  unsigned long parseTime = millis() - parseStartTime;
  Serial.printf("GPX parsing completed in %lu ms\n", parseTime);
  Serial.printf("Parsed %d / %d points\n", currentPoint, pointCount);

  if (currentPoint == 0) {
    Serial.println("ERROR: Failed to parse any track points");
    free(track);
    return false;
  }

  // Store the loaded track globally
  loadedTrack = track;
  loadedTrackPointCount = currentPoint;
  strncpy(loadedTrackName, tripDirName, sizeof(loadedTrackName) - 1);
  loadedTrackName[sizeof(loadedTrackName) - 1] = '\0';

  Serial.printf("Track loaded successfully: %d points, %.2f KB in PSRAM\n",
                loadedTrackPointCount, (bytesNeeded / 1024.0));

  // Print first and last points for verification
  if (loadedTrackPointCount > 0) {
    Serial.printf("First point: lat=%.6f, lon=%.6f, elev=%d\n",
                  loadedTrack[0].lat, loadedTrack[0].lon, loadedTrack[0].elev);
    Serial.printf("Last point: lat=%.6f, lon=%.6f, elev=%d\n",
                  loadedTrack[loadedTrackPointCount-1].lat,
                  loadedTrack[loadedTrackPointCount-1].lon,
                  loadedTrack[loadedTrackPointCount-1].elev);
  }

  return true;
}

// Parse GPX data directly from memory buffer (for temporary trips)
bool parseAndLoadGPXFromMemory(const char* tripName, const uint8_t* gpxData, uint32_t gpxSize) {
  unsigned long parseStartTime = millis();

  Serial.printf("Parsing GPX from memory: %s (%d bytes)\n", tripName, gpxSize);

  // Free any previously loaded track
  freeLoadedTrack();

  // First pass: Count track points
  int pointCount = 0;
  uint32_t pos = 0;

  Serial.println("First pass: counting track points...");
  while (pos < gpxSize) {
    // Look for "<trkpt" in the buffer
    if (pos + 6 <= gpxSize) {
      if (memcmp(gpxData + pos, "<trkpt", 6) == 0) {
        pointCount++;
      }
    }
    pos++;
  }

  Serial.printf("Found %d track points\n", pointCount);

  if (pointCount == 0) {
    Serial.println("ERROR: No track points found in GPX data");
    return false;
  }

  // Allocate PSRAM for track points
  size_t bytesNeeded = pointCount * sizeof(TrackPoint);
  Serial.printf("Allocating %d bytes in PSRAM for track...\n", bytesNeeded);

  TrackPoint* track = (TrackPoint*)ps_malloc(bytesNeeded);
  if (track == nullptr) {
    Serial.println("ERROR: Failed to allocate PSRAM for track");
    return false;
  }

  Serial.println("PSRAM allocated successfully");

  // Second pass: Parse track points
  // Convert buffer to String for easier parsing
  String gpxString = "";
  gpxString.reserve(gpxSize + 1);
  for (uint32_t i = 0; i < gpxSize; i++) {
    gpxString += (char)gpxData[i];
  }

  int currentPoint = 0;
  int searchStart = 0;

  Serial.println("Second pass: parsing track points...");

  while (searchStart < gpxString.length() && currentPoint < pointCount) {
    // Find next <trkpt tag
    int trkptStart = gpxString.indexOf("<trkpt", searchStart);
    if (trkptStart == -1) break;

    // Find closing </trkpt> tag
    int trkptEnd = gpxString.indexOf("</trkpt>", trkptStart);
    if (trkptEnd == -1) break;

    // Extract complete <trkpt>...</trkpt> element
    String trkptElement = gpxString.substring(trkptStart, trkptEnd + 8);

    // Parse latitude
    int latStart = trkptElement.indexOf("lat=\"");
    if (latStart != -1) {
      latStart += 5;  // Move past 'lat="'
      int latEnd = trkptElement.indexOf("\"", latStart);
      if (latEnd != -1) {
        String latStr = trkptElement.substring(latStart, latEnd);
        track[currentPoint].lat = latStr.toFloat();
      }
    }

    // Parse longitude
    int lonStart = trkptElement.indexOf("lon=\"");
    if (lonStart != -1) {
      lonStart += 5;  // Move past 'lon="'
      int lonEnd = trkptElement.indexOf("\"", lonStart);
      if (lonEnd != -1) {
        String lonStr = trkptElement.substring(lonStart, lonEnd);
        track[currentPoint].lon = lonStr.toFloat();
      }
    }

    // Parse elevation (optional)
    int eleStart = trkptElement.indexOf("<ele>");
    if (eleStart != -1) {
      eleStart += 5;  // Move past '<ele>'
      int eleEnd = trkptElement.indexOf("</ele>", eleStart);
      if (eleEnd != -1) {
        String eleStr = trkptElement.substring(eleStart, eleEnd);
        track[currentPoint].elev = (int16_t)eleStr.toFloat();
      }
    } else {
      track[currentPoint].elev = 0;
    }

    currentPoint++;
    searchStart = trkptEnd + 8;
  }

  unsigned long parseTime = millis() - parseStartTime;
  Serial.printf("GPX parsing completed in %lu ms\n", parseTime);
  Serial.printf("Parsed %d / %d points\n", currentPoint, pointCount);

  if (currentPoint == 0) {
    Serial.println("ERROR: Failed to parse any track points");
    free(track);
    return false;
  }

  // Store the loaded track globally
  loadedTrack = track;
  loadedTrackPointCount = currentPoint;
  strncpy(loadedTrackName, tripName, sizeof(loadedTrackName) - 1);
  loadedTrackName[sizeof(loadedTrackName) - 1] = '\0';

  Serial.printf("Track loaded successfully from memory: %d points, %.2f KB in PSRAM\n",
                loadedTrackPointCount, (bytesNeeded / 1024.0));

  // Print first and last points for verification
  if (loadedTrackPointCount > 0) {
    Serial.printf("First point: lat=%.6f, lon=%.6f, elev=%d\n",
                  loadedTrack[0].lat, loadedTrack[0].lon, loadedTrack[0].elev);
    Serial.printf("Last point: lat=%.6f, lon=%.6f, elev=%d\n",
                  loadedTrack[loadedTrackPointCount-1].lat,
                  loadedTrack[loadedTrackPointCount-1].lon,
                  loadedTrack[loadedTrackPointCount-1].elev);
  }

  return true;
}

// Load trip metadata and GPX data for trip details view
bool loadTripForDetails(const char* tripDirName) {
  Serial.printf("Loading trip for details: %s\n", tripDirName);

  // Free any previously loaded track
  freeLoadedTrack();

  // Parse and load GPX file into PSRAM
  if (!parseAndLoadGPX(tripDirName)) {
    Serial.println("ERROR: Failed to load GPX data");
    return false;
  }

  Serial.println("Trip loaded successfully for details view");
  return true;
}

// --- MAP PREVIEW FUNCTIONS ---

// Calculate bounding box of loaded track
void calculateTrackBoundingBox(double* minLat, double* maxLat, double* minLon, double* maxLon) {
  if (loadedTrack == nullptr || loadedTrackPointCount == 0) {
    *minLat = *maxLat = *minLon = *maxLon = 0.0;
    return;
  }

  *minLat = *maxLat = loadedTrack[0].lat;
  *minLon = *maxLon = loadedTrack[0].lon;

  for (int i = 1; i < loadedTrackPointCount; i++) {
    if (loadedTrack[i].lat < *minLat) *minLat = loadedTrack[i].lat;
    if (loadedTrack[i].lat > *maxLat) *maxLat = loadedTrack[i].lat;
    if (loadedTrack[i].lon < *minLon) *minLon = loadedTrack[i].lon;
    if (loadedTrack[i].lon > *maxLon) *maxLon = loadedTrack[i].lon;
  }
}

// Calculate best zoom level to fit the track within display bounds
// Margin is the padding in pixels from the edges
int calculateBestZoomLevel(double minLat, double maxLat, double minLon, double maxLon,
                          int displayWidth, int displayHeight, int margin) {
  // Available display area after margins
  int availableWidth = displayWidth - (2 * margin);
  int availableHeight = displayHeight - (2 * margin);

  // Try zoom levels from highest (most detailed) to lowest
  const int zoomLevels[] = {18, 17, 16, 15, 14, 13, 12, 11, 10, 9};
  const int zoomCount = sizeof(zoomLevels) / sizeof(zoomLevels[0]);

  for (int i = 0; i < zoomCount; i++) {
    int zoom = zoomLevels[i];

    // Get tile coordinates for corners of bounding box
    int minTileX, minTileY, maxTileX, maxTileY;
    double pixelX, pixelY;

    getTileCoordinates(minLat, minLon, zoom, &minTileX, &maxTileY, &pixelX, &pixelY);
    getTileCoordinates(maxLat, maxLon, zoom, &maxTileX, &minTileY, &pixelX, &pixelY);

    // Get precise pixel coordinates for bounding box corners
    double minPixelX, minPixelY, maxPixelX, maxPixelY;
    getTileCoordinates(minLat, minLon, zoom, &minTileX, &maxTileY, &minPixelX, &maxPixelY);
    getTileCoordinates(maxLat, maxLon, zoom, &maxTileX, &minTileY, &maxPixelX, &minPixelY);

    // Calculate pixel distance across the bounding box
    double totalPixelWidth = (maxTileX - minTileX) * 256 + (maxPixelX - minPixelX);
    double totalPixelHeight = (maxTileY - minTileY) * 256 + (maxPixelY - minPixelY);

    // Check if this zoom level fits within available space
    if (totalPixelWidth <= availableWidth && totalPixelHeight <= availableHeight) {
      Serial.printf("Best zoom level: %d (track: %.0f x %.0f px, available: %d x %d px)\n",
                    zoom, totalPixelWidth, totalPixelHeight, availableWidth, availableHeight);
      return zoom;
    }
  }

  // If nothing fits, return lowest zoom level
  Serial.println("Using minimum zoom level 9 (track too large)");
  return 9;
}

// Render map preview with track overlay
void renderTripMapPreview(int x, int y, int width, int height) {
  if (loadedTrack == nullptr || loadedTrackPointCount == 0) {
    Serial.println("No track loaded for preview");
    return;
  }

  Serial.printf("Rendering map preview at (%d,%d) size %dx%d\n", x, y, width, height);

  // Calculate bounding box
  double minLat, maxLat, minLon, maxLon;
  calculateTrackBoundingBox(&minLat, &maxLat, &minLon, &maxLon);

  Serial.printf("Track bounds: lat[%.6f, %.6f] lon[%.6f, %.6f]\n",
                minLat, maxLat, minLon, maxLon);

  // Calculate best zoom level (12px margin for virtual rectangle)
  const int PREVIEW_MARGIN = 12;
  int previewZoom = calculateBestZoomLevel(minLat, maxLat, minLon, maxLon,
                                           width, height, PREVIEW_MARGIN);

  // Calculate center point of track
  double centerLat = (minLat + maxLat) / 2.0;
  double centerLon = (minLon + maxLon) / 2.0;

  // Get tile coordinates for center point
  int centerTileX, centerTileY;
  double centerPixelX, centerPixelY;
  getTileCoordinates(centerLat, centerLon, previewZoom,
                    &centerTileX, &centerTileY, &centerPixelX, &centerPixelY);

  // Calculate screen position for center tile
  int centerScreenX = (x + width / 2) - (int)centerPixelX;
  int centerScreenY = (y + height / 2) - (int)centerPixelY;

  Serial.printf("Center: lat=%.6f, lon=%.6f, tile=(%d,%d), screen=(%d,%d)\n",
                centerLat, centerLon, centerTileX, centerTileY, centerScreenX, centerScreenY);

  // Render tiles in the preview area
  int tilesRendered = 0;
  for (int dy = -2; dy <= 2; dy++) {
    for (int dx = -2; dx <= 2; dx++) {
      int tileX = centerTileX + dx;
      int tileY = centerTileY + dy;
      int screenX = centerScreenX + (dx * 256);
      int screenY = centerScreenY + (dy * 256);

      // Check if tile intersects with preview area
      if (screenX + 256 > x && screenX < x + width &&
          screenY + 256 > y && screenY < y + height) {

        // Try to load tile
        uint8_t* tileData = tileCacheLookup(previewZoom, tileX, tileY);
        bool fromCache = (tileData != nullptr);

        if (!tileData) {
          // Load from SD card
          char tilePath[64];
          sprintf(tilePath, "/Map/%d/%d/%d.bin", previewZoom, tileX, tileY);

          if (SD.exists(tilePath)) {
            File file = SD.open(tilePath, FILE_READ);
            if (file && file.size() == 8192) {
              tileData = tileCacheInsert(previewZoom, tileX, tileY);
              if (tileData) {
                file.read(tileData, 8192);
              }
            }
            if (file) file.close();
          }
        }

        if (tileData) {
          // Render tile data to display
          for (int ty = 0; ty < 256; ty++) {
            int lineOffset = ty * 32;  // 32 bytes per line (256 pixels / 8 bits)

            for (int tx = 0; tx < 256; tx++) {
              int pixelX = screenX + tx;
              int pixelY = screenY + ty;

              // Clip to preview area
              if (pixelX >= x && pixelX < x + width &&
                  pixelY >= y && pixelY < y + height) {

                // Get pixel value from tile data (1 = white, 0 = black)
                int byteIndex = lineOffset + (tx / 8);
                uint8_t bitIndex = 7 - (tx % 8);
                bool isWhite = (tileData[byteIndex] >> bitIndex) & 1;

                // Skip white pixels (optimization) or draw all pixels
                if (!isWhite) {
                  display.drawPixel(pixelX, pixelY, GxEPD_BLACK);
                } else {
                  display.drawPixel(pixelX, pixelY, GxEPD_WHITE);
                }
              }
            }
          }
          tilesRendered++;
        }
      }
    }
  }

  Serial.printf("Rendered %d tiles for preview\n", tilesRendered);

  // Draw track route (simplified for performance)
  // Downsample: draw every Nth point based on track density
  int step = max(1, loadedTrackPointCount / 200);  // Max 200 line segments

  Serial.printf("Drawing route with step=%d (%d points)\n", step, loadedTrackPointCount / step);

  for (int i = 0; i < loadedTrackPointCount - step; i += step) {
    double lat1 = loadedTrack[i].lat;
    double lon1 = loadedTrack[i].lon;
    double lat2 = loadedTrack[i + step].lat;
    double lon2 = loadedTrack[i + step].lon;

    // Convert to screen coordinates
    int tile1X, tile1Y, tile2X, tile2Y;
    double pixel1X, pixel1Y, pixel2X, pixel2Y;
    getTileCoordinates(lat1, lon1, previewZoom, &tile1X, &tile1Y, &pixel1X, &pixel1Y);
    getTileCoordinates(lat2, lon2, previewZoom, &tile2X, &tile2Y, &pixel2X, &pixel2Y);

    // Use double precision to avoid rounding errors at high zoom levels
    int screen1X = (int)(centerScreenX + (tile1X - centerTileX) * 256.0 + pixel1X + 0.5);
    int screen1Y = (int)(centerScreenY + (tile1Y - centerTileY) * 256.0 + pixel1Y + 0.5);
    int screen2X = (int)(centerScreenX + (tile2X - centerTileX) * 256.0 + pixel2X + 0.5);
    int screen2Y = (int)(centerScreenY + (tile2Y - centerTileY) * 256.0 + pixel2Y + 0.5);

    // Draw 3px wide line (draw main line + offset lines for thickness)
    // Draw the main line first
    display.drawLine(screen1X, screen1Y, screen2X, screen2Y, GxEPD_BLACK);

    // Add thickness by drawing offset lines in cross pattern (1px offset for 3px total width)
    display.drawLine(screen1X - 1, screen1Y, screen2X - 1, screen2Y, GxEPD_BLACK);
    display.drawLine(screen1X + 1, screen1Y, screen2X + 1, screen2Y, GxEPD_BLACK);
    display.drawLine(screen1X, screen1Y - 1, screen2X, screen2Y - 1, GxEPD_BLACK);
    display.drawLine(screen1X, screen1Y + 1, screen2X, screen2Y + 1, GxEPD_BLACK);

    // Add diagonal offsets for better coverage at 45-degree angles
    display.drawLine(screen1X - 1, screen1Y - 1, screen2X - 1, screen2Y - 1, GxEPD_BLACK);
    display.drawLine(screen1X + 1, screen1Y - 1, screen2X + 1, screen2Y - 1, GxEPD_BLACK);
    display.drawLine(screen1X - 1, screen1Y + 1, screen2X - 1, screen2Y + 1, GxEPD_BLACK);
    display.drawLine(screen1X + 1, screen1Y + 1, screen2X + 1, screen2Y + 1, GxEPD_BLACK);
  }

  // Draw start marker (filled circle)
  if (loadedTrackPointCount > 0) {
    double startLat = loadedTrack[0].lat;
    double startLon = loadedTrack[0].lon;

    int startTileX, startTileY;
    double startPixelX, startPixelY;
    getTileCoordinates(startLat, startLon, previewZoom, &startTileX, &startTileY, &startPixelX, &startPixelY);

    int startScreenX = centerScreenX + (startTileX - centerTileX) * 256 + (int)startPixelX;
    int startScreenY = centerScreenY + (startTileY - centerTileY) * 256 + (int)startPixelY;

    // Draw filled circle (radius 5px)
    display.fillCircle(startScreenX, startScreenY, 5, GxEPD_BLACK);
    display.drawCircle(startScreenX, startScreenY, 6, GxEPD_BLACK);  // Outer ring
  }

  // Draw end marker (filled square)
  if (loadedTrackPointCount > 0) {
    double endLat = loadedTrack[loadedTrackPointCount - 1].lat;
    double endLon = loadedTrack[loadedTrackPointCount - 1].lon;

    int endTileX, endTileY;
    double endPixelX, endPixelY;
    getTileCoordinates(endLat, endLon, previewZoom, &endTileX, &endTileY, &endPixelX, &endPixelY);

    int endScreenX = centerScreenX + (endTileX - centerTileX) * 256 + (int)endPixelX;
    int endScreenY = centerScreenY + (endTileY - centerTileY) * 256 + (int)endPixelY;

    // Draw filled square (6x6px)
    display.fillRect(endScreenX - 3, endScreenY - 3, 7, 7, GxEPD_BLACK);
    display.drawRect(endScreenX - 4, endScreenY - 4, 9, 9, GxEPD_BLACK);  // Outer ring
  }

  Serial.println("Map preview rendering complete");
}

// Helper to recursively delete a directory
void deleteDirectory(File dir) {
  while (true) {
    File entry = dir.openNextFile();
    if (!entry) {
      break;
    }

    if (entry.isDirectory()) {
      deleteDirectory(entry);
    }

    char path[128];
    strncpy(path, entry.path(), sizeof(path) - 1);
    path[sizeof(path) - 1] = '\0';
    entry.close();

    SD.remove(path);
  }
}

// Delete trip from SD card (GPX, metadata, and folder)
bool deleteTripFromSD(const char* tripDirName) {
  char tripPath[64];
  snprintf(tripPath, sizeof(tripPath), "/Trips/%s", tripDirName);

  if (!SD.exists(tripPath)) {
    Serial.printf("Trip folder not found: %s\n", tripPath);
    return false;
  }

  // Open the directory
  File tripDir = SD.open(tripPath);
  if (!tripDir || !tripDir.isDirectory()) {
    Serial.println("Failed to open trip directory");
    return false;
  }

  // Delete all files in the directory
  deleteDirectory(tripDir);
  tripDir.close();

  // Delete the directory itself
  bool success = SD.rmdir(tripPath);

  if (success) {
    Serial.printf("Successfully deleted trip: %s\n", tripDirName);
  } else {
    Serial.printf("Failed to delete trip folder: %s\n", tripPath);
  }

  return success;
}

void drawDeleteConfirmationDialog() {
  // Dialog dimensions
  int dialogWidth = 110;
  int dialogHeight = 90;
  int dialogX = (DISPLAY_WIDTH - dialogWidth) / 2;
  int dialogY = (DISPLAY_HEIGHT - dialogHeight) / 2 - 20;

  // Draw shadow (offset by 2 pixels)
  display.fillRect(dialogX + 2, dialogY + 2, dialogWidth, dialogHeight, GxEPD_BLACK);

  // Draw dialog background
  display.fillRect(dialogX, dialogY, dialogWidth, dialogHeight, GxEPD_WHITE);
  display.drawRect(dialogX, dialogY, dialogWidth, dialogHeight, GxEPD_BLACK);
  display.drawRect(dialogX + 1, dialogY + 1, dialogWidth - 2, dialogHeight - 2, GxEPD_BLACK);

  // Setup text rendering
  u8g2_display.setFontMode(1);
  u8g2_display.setForegroundColor(GxEPD_BLACK);
  u8g2_display.setBackgroundColor(GxEPD_WHITE);

  // Title
  u8g2_display.setFont(u8g2_font_helvB12_tf);
  const char* title = "Delete Trip?";
  int titleWidth = u8g2_display.getUTF8Width(title);
  u8g2_display.setCursor(dialogX + (dialogWidth - titleWidth) / 2, dialogY + 18);
  u8g2_display.print(title);

  // Message
  u8g2_display.setFont(u8g2_font_helvB08_tf);
  const char* msg1 = "This will";
  const char* msg2 = "permanently";
  const char* msg3 = "delete the trip";
  int msg1Width = u8g2_display.getUTF8Width(msg1);
  int msg2Width = u8g2_display.getUTF8Width(msg2);
  int msg3Width = u8g2_display.getUTF8Width(msg3);

  u8g2_display.setCursor(dialogX + (dialogWidth - msg1Width) / 2, dialogY + 36);
  u8g2_display.print(msg1);
  u8g2_display.setCursor(dialogX + (dialogWidth - msg2Width) / 2, dialogY + 48);
  u8g2_display.print(msg2);
  u8g2_display.setCursor(dialogX + (dialogWidth - msg3Width) / 2, dialogY + 60);
  u8g2_display.print(msg3);

  // OK button
  int buttonWidth = 40;
  int buttonHeight = 18;
  int buttonX = dialogX + (dialogWidth - buttonWidth) / 2;
  int buttonY = dialogY + dialogHeight - buttonHeight - 6;

  display.fillRect(buttonX, buttonY, buttonWidth, buttonHeight, GxEPD_BLACK);

  u8g2_display.setFont(u8g2_font_helvB10_tf);
  u8g2_display.setForegroundColor(GxEPD_WHITE);
  u8g2_display.setBackgroundColor(GxEPD_BLACK);
  const char* buttonText = "OK";
  int buttonTextWidth = u8g2_display.getUTF8Width(buttonText);
  u8g2_display.setCursor(buttonX + (buttonWidth - buttonTextWidth) / 2, buttonY + 13);
  u8g2_display.print(buttonText);
}

// Check for Navigate Home timeout and trigger error if needed
void checkNavigateHomeTimeout() {
  // Only check if we're waiting for a path and no error has been set yet
  if (waitingForNavigateHomePath && !navigateHomeHasError) {
    unsigned long elapsed = millis() - navigateHomeRequestTime;
    if (elapsed > NAVIGATE_HOME_TIMEOUT_MS) {
      Serial.println("Navigate Home request TIMEOUT!");

      // Set error state
      navigateHomeHasError = true;
      snprintf(navigateHomeErrorMessage, sizeof(navigateHomeErrorMessage), "Route request timed out");

      // Reset waiting state
      waitingForNavigateHomePath = false;
      navigateHomePathLoaded = false;
    }
  }
}

void drawNavigateHomeErrorDialog() {
  // Dialog dimensions - wider to accommodate longer error messages
  int dialogWidth = 120;
  int dialogHeight = 110;
  int dialogX = (DISPLAY_WIDTH - dialogWidth) / 2;
  int dialogY = (DISPLAY_HEIGHT - dialogHeight) / 2 - 20;

  // Draw shadow (offset by 2 pixels)
  display.fillRect(dialogX + 2, dialogY + 2, dialogWidth, dialogHeight, GxEPD_BLACK);

  // Draw dialog background
  display.fillRect(dialogX, dialogY, dialogWidth, dialogHeight, GxEPD_WHITE);
  display.drawRect(dialogX, dialogY, dialogWidth, dialogHeight, GxEPD_BLACK);
  display.drawRect(dialogX + 1, dialogY + 1, dialogWidth - 2, dialogHeight - 2, GxEPD_BLACK);

  // Setup text rendering
  u8g2_display.setFontMode(1);
  u8g2_display.setForegroundColor(GxEPD_BLACK);
  u8g2_display.setBackgroundColor(GxEPD_WHITE);

  // Title
  u8g2_display.setFont(u8g2_font_helvB12_tf);
  const char* title = "Error";
  int titleWidth = u8g2_display.getUTF8Width(title);
  u8g2_display.setCursor(dialogX + (dialogWidth - titleWidth) / 2, dialogY + 20);
  u8g2_display.print(title);

  // Error message - word wrap if needed
  u8g2_display.setFont(u8g2_font_helvR08_tf);
  int yPos = dialogY + 40;
  const int maxCharsPerLine = 18;  // Approximate characters per line

  char msgCopy[64];
  strncpy(msgCopy, navigateHomeErrorMessage, sizeof(msgCopy) - 1);
  msgCopy[sizeof(msgCopy) - 1] = '\0';

  // Simple word wrapping
  char* line = msgCopy;
  int lineCount = 0;
  while (*line && lineCount < 4) {  // Max 4 lines
    // Find a good break point
    char* breakPoint = line;
    int len = 0;
    while (*breakPoint && len < maxCharsPerLine) {
      breakPoint++;
      len++;
    }

    // If we didn't reach the end, try to break at a space
    if (*breakPoint && len == maxCharsPerLine) {
      char* space = breakPoint;
      while (space > line && *space != ' ') {
        space--;
      }
      if (space > line) {
        breakPoint = space;
        *breakPoint = '\0';  // Temporarily terminate
        breakPoint++;        // Move past the space
      } else {
        // No space found, force break
        char temp = *breakPoint;
        *breakPoint = '\0';
      }
    }

    // Draw this line
    int lineWidth = u8g2_display.getUTF8Width(line);
    u8g2_display.setCursor(dialogX + (dialogWidth - lineWidth) / 2, yPos);
    u8g2_display.print(line);

    yPos += 12;
    lineCount++;

    // Move to next part
    line = breakPoint;
    if (*line == '\0') break;
  }

  // OK button
  int buttonWidth = 40;
  int buttonHeight = 18;
  int buttonX = dialogX + (dialogWidth - buttonWidth) / 2;
  int buttonY = dialogY + dialogHeight - buttonHeight - 8;

  display.fillRect(buttonX, buttonY, buttonWidth, buttonHeight, GxEPD_BLACK);

  u8g2_display.setFont(u8g2_font_helvB10_tf);
  u8g2_display.setForegroundColor(GxEPD_WHITE);
  u8g2_display.setBackgroundColor(GxEPD_BLACK);
  const char* buttonText = "OK";
  int buttonTextWidth = u8g2_display.getUTF8Width(buttonText);
  u8g2_display.setCursor(buttonX + (buttonWidth - buttonTextWidth) / 2, buttonY + 13);
  u8g2_display.print(buttonText);
}

void renderTripDetailView() {
  Serial.println("Rendering trip detail view");

  // Clear the deferred redraw flag (this render is now happening)
  tripDetailNeedsRedraw = false;

  // Check for Navigate Home timeout
  if (isNavigateHomeMode) {
    checkNavigateHomeTimeout();

    // If error occurred (either timeout or from BLE), show error dialog
    if (navigateHomeHasError && !showNavigateHomeError) {
      showNavigateHomeError = true;
      Serial.printf("Navigate Home error detected: %s\n", navigateHomeErrorMessage);
    }
  }

  // Check if we're in Navigate Home mode
  bool isNavigateHome = isNavigateHomeMode;
  bool pathLoaded = navigateHomePathLoaded;

  // Read trip metadata (only for regular trips with a directory)
  StaticJsonDocument<512> metaDoc;
  bool metadataLoaded = false;
  if (!isNavigateHome && selectedTripDirName[0] != '\0') {
    metadataLoaded = readTripMetadata(selectedTripDirName, metaDoc);
  }
  // Note: Navigate Home trips don't have metadata from SD card
  // In the future, we could parse metadata from the BLE-received data

  display.setPartialWindow(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
  display.firstPage();

  do {
    display.fillScreen(GxEPD_WHITE);

    // Setup text rendering
    u8g2_display.setFontMode(1);
    u8g2_display.setForegroundColor(GxEPD_BLACK);
    u8g2_display.setBackgroundColor(GxEPD_WHITE);

    // --- METADATA SECTION (TOP) ---
    int yPos = 18;  // Moved down from 10

    if (isNavigateHome) {
      // --- NAVIGATE HOME MODE ---
      // Title
      u8g2_display.setFont(u8g2_font_helvB10_tf);
      u8g2_display.setCursor(4, yPos);
      u8g2_display.print("Navigate Home");
      yPos += 16;

      if (!pathLoaded) {
        // Path not loaded yet - show instructions
        u8g2_display.setFont(u8g2_font_helvB08_tf);
        u8g2_display.setCursor(4, yPos);
        u8g2_display.print("Load route from your");
        yPos += 12;
        u8g2_display.setCursor(4, yPos);
        u8g2_display.print("current location to home");
        yPos += 12;
      } else {
        // Path loaded - show metadata if available
        if (metadataLoaded) {
          u8g2_display.setFont(u8g2_font_helvB08_tf);

          // Distance (convert from meters to km)
          double totalDistanceMeters = metaDoc["totalDistance"] | 0.0;
          double distanceKm = totalDistanceMeters / 1000.0;
          u8g2_display.setCursor(4, yPos);
          u8g2_display.print("Dist: ");
          u8g2_display.print(distanceKm, 2);
          u8g2_display.print(" km");
          yPos += 12;

          // Elevation gain and loss
          double elevGain = metaDoc["totalElevationGain"] | 0.0;
          double elevLoss = metaDoc["totalElevationLoss"] | 0.0;
          u8g2_display.setCursor(4, yPos);
          u8g2_display.print("Elev: +");
          u8g2_display.print((int)elevGain);
          u8g2_display.print("m -");
          u8g2_display.print((int)elevLoss);
          u8g2_display.print("m");
          yPos += 12;

          // Point count
          int pointCount = metaDoc["pointCount"] | 0;
          u8g2_display.setCursor(4, yPos);
          u8g2_display.print("Points: ");
          u8g2_display.print(pointCount);
          yPos += 12;
        }
      }
    } else if (metadataLoaded) {
      // --- REGULAR TRIP MODE ---
      // Trip name (title)
      const char* tripName = metaDoc["name"] | "Unknown Trip";
      u8g2_display.setFont(u8g2_font_helvB10_tf);
      u8g2_display.setCursor(4, yPos);
      u8g2_display.print(tripName);
      yPos += 16;

      // Trip metadata in compact format
      u8g2_display.setFont(u8g2_font_helvB08_tf);

      // Date and time (if available) - European format: DD.MM.YYYY HH:MM
      long long createdAt = metaDoc["createdAt"] | 0LL;
      if (createdAt > 0) {
        // Convert milliseconds to seconds
        time_t timestamp = createdAt / 1000;
        struct tm* timeinfo = localtime(&timestamp);

        char dateStr[32];
        strftime(dateStr, sizeof(dateStr), "%d.%m.%Y %H:%M", timeinfo);

        u8g2_display.setCursor(4, yPos);
        u8g2_display.print(dateStr);
        yPos += 12;
      }

      // Distance (convert from meters to km)
      double totalDistanceMeters = metaDoc["totalDistance"] | 0.0;
      double distanceKm = totalDistanceMeters / 1000.0;
      u8g2_display.setCursor(4, yPos);
      u8g2_display.print("Dist: ");
      u8g2_display.print(distanceKm, 2);
      u8g2_display.print(" km");
      yPos += 12;

      // Elevation gain and loss
      double elevGain = metaDoc["totalElevationGain"] | 0.0;
      double elevLoss = metaDoc["totalElevationLoss"] | 0.0;
      u8g2_display.setCursor(4, yPos);
      u8g2_display.print("Elev: +");
      u8g2_display.print((int)elevGain);
      u8g2_display.print("m -");
      u8g2_display.print((int)elevLoss);
      u8g2_display.print("m");
      yPos += 12;

      // Point count
      int pointCount = metaDoc["pointCount"] | 0;
      u8g2_display.setCursor(4, yPos);
      u8g2_display.print("Points: ");
      u8g2_display.print(pointCount);
      yPos += 12;
    } else {
      // Fallback if metadata not available
      u8g2_display.setFont(u8g2_font_helvB10_tf);
      u8g2_display.setCursor(4, yPos);
      u8g2_display.print(selectedTripDirName);
      yPos += 16;

      u8g2_display.setFont(u8g2_font_helvB08_tf);
      u8g2_display.setCursor(4, yPos);
      u8g2_display.print("No metadata");
      yPos += 12;
    }

    // Draw separator line (closer to details for more map space)
    yPos += 2;
    display.drawLine(0, yPos, DISPLAY_WIDTH, yPos, GxEPD_BLACK);
    yPos += 4;

    // --- BUTTONS SECTION (AT BOTTOM) ---
    int buttonWidth = 56;
    int buttonHeight = 24;
    int buttonSpacing = 8;
    int shadowOffset = 2;
    int buttonMarginBottom = 6;  // Space from bottom edge

    // Position buttons at the very bottom
    int buttonsTop = DISPLAY_HEIGHT - buttonHeight - buttonMarginBottom;

    // --- MAP PLACEHOLDER SECTION (FILL REMAINING SPACE) ---
    int mapPlaceholderTop = yPos;
    int mapPlaceholderHeight = buttonsTop - yPos - 8;  // Leave 8px gap before buttons

    // Render map preview with track overlay (only if path is loaded or in regular mode)
    if (isNavigateHome && !pathLoaded) {
      // Navigate Home mode with no path loaded
      // Check if we're waiting for the route
      extern bool waitingForNavigateHomePath;

      if (waitingForNavigateHomePath) {
        // Show loading indicator
        u8g2_display.setFont(u8g2_font_helvB10_tf);
        const char* loadingText = "Loading route...";
        int textWidth = u8g2_display.getUTF8Width(loadingText);
        u8g2_display.setCursor((DISPLAY_WIDTH - textWidth) / 2, mapPlaceholderTop + mapPlaceholderHeight / 2 - 10);
        u8g2_display.print(loadingText);

        // Draw simple spinner/progress indicator
        u8g2_display.setFont(u8g2_font_helvR08_tf);
        const char* pleaseWait = "Please wait...";
        int pwWidth = u8g2_display.getUTF8Width(pleaseWait);
        u8g2_display.setCursor((DISPLAY_WIDTH - pwWidth) / 2, mapPlaceholderTop + mapPlaceholderHeight / 2 + 10);
        u8g2_display.print(pleaseWait);
      } else {
        // Show instruction message - no border
        u8g2_display.setFont(u8g2_font_helvR08_tf);
        const char* line1 = "Press 'Load' to request";
        const char* line2 = "route from your phone";
        int line1Width = u8g2_display.getUTF8Width(line1);
        int line2Width = u8g2_display.getUTF8Width(line2);
        u8g2_display.setCursor((DISPLAY_WIDTH - line1Width) / 2, mapPlaceholderTop + mapPlaceholderHeight / 2 - 10);
        u8g2_display.print(line1);
        u8g2_display.setCursor((DISPLAY_WIDTH - line2Width) / 2, mapPlaceholderTop + mapPlaceholderHeight / 2 + 6);
        u8g2_display.print(line2);
      }
    } else if (loadedTrack != nullptr && loadedTrackPointCount > 0) {
      // Draw border for map preview area
      display.drawRect(4, mapPlaceholderTop, DISPLAY_WIDTH - 8, mapPlaceholderHeight, GxEPD_BLACK);
      renderTripMapPreview(4, mapPlaceholderTop, DISPLAY_WIDTH - 8, mapPlaceholderHeight);
    } else {
      // Regular trip mode - show loading message without border
      u8g2_display.setFont(u8g2_font_helvB10_tf);
      const char* mapPlaceholder = "Loading...";
      int mapTextWidth = u8g2_display.getUTF8Width(mapPlaceholder);
      u8g2_display.setCursor((DISPLAY_WIDTH - mapTextWidth) / 2, mapPlaceholderTop + mapPlaceholderHeight / 2);
      u8g2_display.print(mapPlaceholder);
    }

    // --- BUTTON RENDERING ---
    if (isNavigateHome) {
      // Navigate Home mode - show single button (Load path or Start)
      // Center the single button
      int singleButtonX = (DISPLAY_WIDTH - buttonWidth) / 2;
      int singleButtonY = buttonsTop;

      // Determine button text and enabled state
      const char* buttonText;
      bool buttonEnabled;

      // Check if we're waiting for route
      extern bool waitingForNavigateHomePath;

      if (!pathLoaded) {
        buttonText = "Load";
        // Disabled if not connected OR if waiting for path
        buttonEnabled = deviceConnected && !waitingForNavigateHomePath;
      } else {
        buttonText = "Start";
        buttonEnabled = true;  // Always enabled once path is loaded
      }

      // Show connection status ABOVE button (only when path not loaded to avoid map collision)
      if (!pathLoaded) {
        u8g2_display.setFont(u8g2_font_helvR08_tf);
        u8g2_display.setForegroundColor(GxEPD_BLACK);
        u8g2_display.setBackgroundColor(GxEPD_WHITE);
        const char* statusMsg;
        if (!deviceConnected) {
          statusMsg = "Phone: Not connected";
        } else if (waitingForNavigateHomePath) {
          statusMsg = "Phone: Receiving route...";
        } else {
          statusMsg = "Phone: Connected";
        }
        int statusWidth = u8g2_display.getUTF8Width(statusMsg);
        u8g2_display.setCursor((DISPLAY_WIDTH - statusWidth) / 2, singleButtonY - 8);
        u8g2_display.print(statusMsg);
      }

      // Draw shadow
      display.fillRect(singleButtonX + shadowOffset, singleButtonY + shadowOffset,
                       buttonWidth, buttonHeight, GxEPD_BLACK);

      // Draw button (always in selected state since there's only one)
      if (buttonEnabled) {
        // Enabled - filled/inverted
        display.fillRect(singleButtonX, singleButtonY, buttonWidth, buttonHeight, GxEPD_BLACK);
        display.drawRect(singleButtonX, singleButtonY, buttonWidth, buttonHeight, GxEPD_BLACK);
        display.drawRect(singleButtonX + 1, singleButtonY + 1, buttonWidth - 2, buttonHeight - 2, GxEPD_BLACK);

        u8g2_display.setFont(u8g2_font_helvB10_tf);
        int textWidth = u8g2_display.getUTF8Width(buttonText);
        u8g2_display.setForegroundColor(GxEPD_WHITE);
        u8g2_display.setBackgroundColor(GxEPD_BLACK);
        u8g2_display.setCursor(singleButtonX + (buttonWidth - textWidth) / 2, singleButtonY + 16);
        u8g2_display.print(buttonText);
      } else {
        // Disabled - grey/outline only
        display.fillRect(singleButtonX, singleButtonY, buttonWidth, buttonHeight, GxEPD_WHITE);
        display.drawRect(singleButtonX, singleButtonY, buttonWidth, buttonHeight, GxEPD_BLACK);

        u8g2_display.setFont(u8g2_font_helvR08_tf);  // Smaller font for disabled state
        int textWidth = u8g2_display.getUTF8Width(buttonText);
        u8g2_display.setForegroundColor(GxEPD_BLACK);
        u8g2_display.setBackgroundColor(GxEPD_WHITE);
        u8g2_display.setCursor(singleButtonX + (buttonWidth - textWidth) / 2, singleButtonY + 15);
        u8g2_display.print(buttonText);
      }
    } else {
      // Regular trip mode - show Start and Delete buttons
      // Calculate button positions (centered horizontally)
      int totalButtonsWidth = (buttonWidth * 2) + buttonSpacing;
      int buttonsStartX = (DISPLAY_WIDTH - totalButtonsWidth) / 2;

      // Start button (left)
      int startButtonX = buttonsStartX;
      int startButtonY = buttonsTop;

    // Draw shadow
    display.fillRect(startButtonX + shadowOffset, startButtonY + shadowOffset,
                     buttonWidth, buttonHeight, GxEPD_BLACK);

    // Draw button based on selection state
    if (selectedTripButton == 0) {
      // Selected - filled/inverted
      display.fillRect(startButtonX, startButtonY, buttonWidth, buttonHeight, GxEPD_BLACK);
      display.drawRect(startButtonX, startButtonY, buttonWidth, buttonHeight, GxEPD_BLACK);
      display.drawRect(startButtonX + 1, startButtonY + 1, buttonWidth - 2, buttonHeight - 2, GxEPD_BLACK);

      u8g2_display.setFont(u8g2_font_helvB10_tf);
      const char* startText = "Start";
      int startTextWidth = u8g2_display.getUTF8Width(startText);
      u8g2_display.setForegroundColor(GxEPD_WHITE);
      u8g2_display.setBackgroundColor(GxEPD_BLACK);
      u8g2_display.setCursor(startButtonX + (buttonWidth - startTextWidth) / 2, startButtonY + 16);
      u8g2_display.print(startText);
    } else {
      // Not selected - normal
      display.fillRect(startButtonX, startButtonY, buttonWidth, buttonHeight, GxEPD_WHITE);
      display.drawRect(startButtonX, startButtonY, buttonWidth, buttonHeight, GxEPD_BLACK);
      display.drawRect(startButtonX + 1, startButtonY + 1, buttonWidth - 2, buttonHeight - 2, GxEPD_BLACK);

      u8g2_display.setFont(u8g2_font_helvB10_tf);
      const char* startText = "Start";
      int startTextWidth = u8g2_display.getUTF8Width(startText);
      u8g2_display.setForegroundColor(GxEPD_BLACK);
      u8g2_display.setBackgroundColor(GxEPD_WHITE);
      u8g2_display.setCursor(startButtonX + (buttonWidth - startTextWidth) / 2, startButtonY + 16);
      u8g2_display.print(startText);
    }

    // Delete button (right)
    int deleteButtonX = startButtonX + buttonWidth + buttonSpacing;
    int deleteButtonY = buttonsTop;

    // Draw shadow
    display.fillRect(deleteButtonX + shadowOffset, deleteButtonY + shadowOffset,
                     buttonWidth, buttonHeight, GxEPD_BLACK);

    // Draw button based on selection state
    if (selectedTripButton == 1) {
      // Selected - filled/inverted
      display.fillRect(deleteButtonX, deleteButtonY, buttonWidth, buttonHeight, GxEPD_BLACK);
      display.drawRect(deleteButtonX, deleteButtonY, buttonWidth, buttonHeight, GxEPD_BLACK);
      display.drawRect(deleteButtonX + 1, deleteButtonY + 1, buttonWidth - 2, buttonHeight - 2, GxEPD_BLACK);

      u8g2_display.setFont(u8g2_font_helvB10_tf);
      const char* deleteText = "Delete";
      int deleteTextWidth = u8g2_display.getUTF8Width(deleteText);
      u8g2_display.setForegroundColor(GxEPD_WHITE);
      u8g2_display.setBackgroundColor(GxEPD_BLACK);
      u8g2_display.setCursor(deleteButtonX + (buttonWidth - deleteTextWidth) / 2, deleteButtonY + 16);
      u8g2_display.print(deleteText);
    } else {
      // Not selected - normal
      display.fillRect(deleteButtonX, deleteButtonY, buttonWidth, buttonHeight, GxEPD_WHITE);
      display.drawRect(deleteButtonX, deleteButtonY, buttonWidth, buttonHeight, GxEPD_BLACK);
      display.drawRect(deleteButtonX + 1, deleteButtonY + 1, buttonWidth - 2, buttonHeight - 2, GxEPD_BLACK);

      u8g2_display.setFont(u8g2_font_helvB10_tf);
      const char* deleteText = "Delete";
      int deleteTextWidth = u8g2_display.getUTF8Width(deleteText);
      u8g2_display.setForegroundColor(GxEPD_BLACK);
      u8g2_display.setBackgroundColor(GxEPD_WHITE);
      u8g2_display.setCursor(deleteButtonX + (buttonWidth - deleteTextWidth) / 2, deleteButtonY + 16);
      u8g2_display.print(deleteText);
    }

      // Draw delete confirmation dialog on top if needed
      if (showDeleteConfirmation) {
        drawDeleteConfirmationDialog();
      }
    }  // End of else block for regular trip mode

    // Draw Navigate Home error dialog on top if needed (for both modes)
    if (showNavigateHomeError) {
      drawNavigateHomeErrorDialog();
    }

    // Draw notification overlay
    drawNotificationOverlay();

  } while (display.nextPage());
}

#endif // MAP_TRIPS_H
