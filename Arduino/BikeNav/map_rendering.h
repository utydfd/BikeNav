#ifndef MAP_RENDERING_H
#define MAP_RENDERING_H

#include <GxEPD2_BW.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <SD.h>
#include <math.h>
#include "timezone.h"

// External references from main program
extern GxEPD2_BW<GxEPD2_290_BS, GxEPD2_290_BS::HEIGHT> display;
extern U8G2_FOR_ADAFRUIT_GFX u8g2_display;
extern const int DISPLAY_WIDTH;
extern const int DISPLAY_HEIGHT;
extern double currentLat;
extern double currentLon;
extern bool gpsValid;
extern TinyGPSPlus gps;
extern bool speedometerSplitEnabled;
extern unsigned long lastSpeedometerOverlayUpdate;

void drawSpeedometerSplitOverlay();

// External tile cache functions
extern uint8_t* tileCacheLookup(int zoom, int tileX, int tileY);
extern uint8_t* tileCacheInsert(int zoom, int tileX, int tileY);
extern void printTileCacheStats();

// External location marker function
extern void drawLocationMarker(int x, int y, GxEPD2_BW<GxEPD2_290_BS, GxEPD2_290_BS::HEIGHT>& display);

// External small icon functions and bitmaps from bitmaps.h
extern const int SMALL_ICON_SIZE;
extern const unsigned char ICON_ZOOM[] PROGMEM;
extern const unsigned char ICON_ROTATION[] PROGMEM;
extern const unsigned char ICON_SCRUB[] PROGMEM;
extern void drawSmallIcon(const uint8_t* bitmap, int x, int y, GxEPD2_BW<GxEPD2_290_BS, GxEPD2_290_BS::HEIGHT>& display);

// External turn icons and functions from bitmaps.h
extern const int TURN_ICON_SIZE;
extern const unsigned char ICON_TURN_LEFT[] PROGMEM;
extern const unsigned char ICON_TURN_RIGHT[] PROGMEM;
extern const unsigned char ICON_TURN_STRAIGHT[] PROGMEM;
extern const unsigned char ICON_TURN_SLIGHT_LEFT[] PROGMEM;
extern const unsigned char ICON_TURN_SLIGHT_RIGHT[] PROGMEM;
extern const unsigned char ICON_TURN_SHARP_LEFT[] PROGMEM;
extern const unsigned char ICON_TURN_SHARP_RIGHT[] PROGMEM;
extern const unsigned char ICON_TURN_UTURN[] PROGMEM;
extern const unsigned char ICON_NO_GPS[] PROGMEM;
extern void drawTurnIcon(const uint8_t* bitmap, int x, int y, GxEPD2_BW<GxEPD2_290_BS, GxEPD2_290_BS::HEIGHT>& display);

// External state from page_map.h
extern int zoomLevel;
extern int mapRotation;

// External battery manager and icon functions from status_bar.h
extern class BatteryManager batteryManager;
extern void drawSmallBatteryIcon(int x, int y, float percentage, bool isCharging);
extern void drawGPSIcon(int x, int y, bool active);
extern void drawBLEIcon(int x, int y, bool connected);
extern bool isGPSActive();
extern bool isBLEConnected();
extern bool bluetoothEnabled;

// External radar overlay state from page_radar.h / ble_handler.h
extern bool radarOverlayEnabled;
extern bool radarHasError;
extern const uint8_t* getRadarFrameData(int offsetSteps);
extern bool radarMapLightenEnabled;

// External enum and mode from page_map.h
enum MapMode;
extern MapMode currentMapMode;

// External scrub mode state from page_map.h
extern int scrubOffsetMeters;
extern double scrubLat;
extern double scrubLon;

// External navigation state from map_navigation.h
extern bool navigationActive;
extern float distanceToNextTurn;
extern int nextTurnType;

// External navigation track data from map_navigation.h
// TrackPoint structure is defined in map_trips.h (included before this file)
extern TrackPoint* navigationTrack;
extern int navigationTrackPointCount;

// External UI functions from page_map.h
extern void drawPageDots();

// --- MAP PAGE CONSTANTS ---
const int MAP_INFO_BAR_HEIGHT_NORMAL = 16;   // Normal mode: 16px info bar (matches centralized status bar)
const int MAP_INFO_BAR_HEIGHT_NAV = 48;      // Navigation mode: 48px info bar

// Dynamic info bar height (will be set based on navigation mode)
int currentInfoBarHeight = MAP_INFO_BAR_HEIGHT_NORMAL;
int MAP_DISPLAY_HEIGHT = DISPLAY_HEIGHT - MAP_INFO_BAR_HEIGHT_NORMAL;

// --- ZOOM CONFIGURATION ---
const int ZOOM_LEVELS[] = {18, 17, 16, 15, 14, 13, 12, 11, 10, 9};
const int ZOOM_COUNT = 10;

// --- SCRUB MODE CONFIGURATION ---
// Scrub step distance per zoom level [18, 17, 16, 15, 14, 13, 12, 11, 10, 9]
// Fine-grained at high zoom, coarse at low zoom
const int SCRUB_STEP_METERS[] = {10, 20, 30, 50, 75, 100, 150, 200, 250, 300};

// --- ROUTE RENDERING CONFIGURATION ---
// Route line width per zoom level [18, 17, 16, 15, 14, 13, 12, 11, 10, 9]
// Tunable: Adjust these values to change route thickness at different zoom levels
const int ROUTE_LINE_WIDTH[] = {6, 4, 3, 3, 2, 2, 2, 2, 2, 2};
const int MAX_ROUTE_SEGMENTS = 300;  // Maximum line segments to draw (performance optimization)

// Center point on screen where GPS location is displayed
const int CENTER_X = DISPLAY_WIDTH / 2;

// Center Y calculation for different modes (exact center of map display area)
const int CENTER_Y_NORMAL = (DISPLAY_HEIGHT - MAP_INFO_BAR_HEIGHT_NORMAL) / 2;     // Middle of map area
const int CENTER_Y_NAV = ((DISPLAY_HEIGHT - MAP_INFO_BAR_HEIGHT_NAV) * 3) / 4;     // 3/4 down (more ahead visible)

// Dynamic center Y (will be set based on navigation mode)
int currentCenterY = CENTER_Y_NORMAL;

// --- TILE RENDERING STATE ---
struct TileInfo {
  int tileX;
  int tileY;
  int screenX;
  int screenY;
};

TileInfo tilesToRender[25];  // Support up to 5x5 grid for rotated maps
int tileCount = 0;

// --- FUNCTION PROTOTYPES ---
void getTileCoordinates(double lat, double lon, int zoom, int* tileX, int* tileY, double* pixelX, double* pixelY);
bool isTileVisible(int screenX, int screenY, int rotation);
void calculateVisibleTiles(double lat, double lon, int zoom);
bool loadAndRenderTile(int tileX, int tileY, int zoom, int screenX, int screenY);
void updateMapInfoBar();
void refreshMapInfoBar();
void loadAndDisplayMap();
void drawRadarOverlay(const uint8_t* frameData);

// --- IMPLEMENTATIONS ---

void getTileCoordinates(double lat, double lon, int zoom, int* tileX, int* tileY, double* pixelX, double* pixelY) {
  double latRad = lat * M_PI / 180.0;
  double n = pow(2.0, zoom);

  *tileX = (int)((lon + 180.0) / 360.0 * n);
  *tileY = (int)((1.0 - asinh(tan(latRad)) / M_PI) / 2.0 * n);

  double tileXFloat = (lon + 180.0) / 360.0 * n;
  double tileYFloat = (1.0 - asinh(tan(latRad)) / M_PI) / 2.0 * n;

  *pixelX = (tileXFloat - *tileX) * 256;
  *pixelY = (tileYFloat - *tileY) * 256;
}

// Helper function: check if a tile will be visible after rotation
bool isTileVisible(int screenX, int screenY, int rotation) {
  if (rotation == 0) {
    // Simple rectangular check for non-rotated
    return (screenX + 256 > 0 && screenX < DISPLAY_WIDTH &&
            screenY + 256 > 0 && screenY < MAP_DISPLAY_HEIGHT);
  }

  // For rotated tiles we need to check both directions:
  // 1. Do any tile corners (after rotation) fall inside viewport?
  // 2. Do any viewport corners (after inverse rotation) fall inside tile?

  float rotationRad = rotation * M_PI / 180.0;
  float cosAngle = cos(rotationRad);
  float sinAngle = sin(rotationRad);

  // CHECK 1: Tile's 4 corners after rotation -> are they in viewport?
  int tileCorners[4][2] = {
    {screenX, screenY},              // Top-left
    {screenX + 256, screenY},        // Top-right
    {screenX, screenY + 256},        // Bottom-left
    {screenX + 256, screenY + 256}   // Bottom-right
  };

  for (int i = 0; i < 4; i++) {
    float x = tileCorners[i][0];
    float y = tileCorners[i][1];

    // Translate to center
    float relX = x - CENTER_X;
    float relY = y - currentCenterY;

    // Apply rotation
    float rotatedX = relX * cosAngle - relY * sinAngle;
    float rotatedY = relX * sinAngle + relY * cosAngle;

    // Translate back
    int finalX = (int)(rotatedX + CENTER_X);
    int finalY = (int)(rotatedY + currentCenterY);

    // Check if this corner is visible
    if (finalX >= 0 && finalX < DISPLAY_WIDTH &&
        finalY >= 0 && finalY < MAP_DISPLAY_HEIGHT) {
      return true;
    }
  }

  // CHECK 2: Viewport's 4 corners with inverse rotation -> are they in tile bounds?
  // Use inverse rotation to transform viewport corners back to original tile space
  float invCos = cosAngle;   // cos(-x) = cos(x)
  float invSin = -sinAngle;  // sin(-x) = -sin(x)

  int viewportCorners[4][2] = {
    {0, 0},                              // Top-left
    {DISPLAY_WIDTH - 1, 0},              // Top-right
    {0, MAP_DISPLAY_HEIGHT - 1},         // Bottom-left
    {DISPLAY_WIDTH - 1, MAP_DISPLAY_HEIGHT - 1}  // Bottom-right
  };

  for (int i = 0; i < 4; i++) {
    float vx = viewportCorners[i][0];
    float vy = viewportCorners[i][1];

    // Translate to center
    float relX = vx - CENTER_X;
    float relY = vy - currentCenterY;

    // Apply inverse rotation
    float origX = relX * invCos - relY * invSin;
    float origY = relX * invSin + relY * invCos;

    // Translate back
    float finalX = origX + CENTER_X;
    float finalY = origY + currentCenterY;

    // Check if this inverse-rotated viewport corner is inside the tile bounds
    if (finalX >= screenX && finalX < screenX + 256 &&
        finalY >= screenY && finalY < screenY + 256) {
      return true;
    }
  }

  // CHECK 3: Check tile's center point after rotation
  float tileCenterX = screenX + 128;
  float tileCenterY = screenY + 128;

  float relX = tileCenterX - CENTER_X;
  float relY = tileCenterY - currentCenterY;

  float rotatedCenterX = relX * cosAngle - relY * sinAngle + CENTER_X;
  float rotatedCenterY = relX * sinAngle + relY * cosAngle + currentCenterY;

  if (rotatedCenterX >= 0 && rotatedCenterX < DISPLAY_WIDTH &&
      rotatedCenterY >= 0 && rotatedCenterY < MAP_DISPLAY_HEIGHT) {
    return true;
  }

  // CHECK 4: Check viewport center in tile space
  if (CENTER_X >= screenX && CENTER_X < screenX + 256 &&
      currentCenterY >= screenY && currentCenterY < screenY + 256) {
    return true;
  }

  // CHECK 5: Check edge midpoints of tile (catches diagonal intersections)
  int edgeMidpoints[4][2] = {
    {screenX + 128, screenY},         // Top edge midpoint
    {screenX + 256, screenY + 128},   // Right edge midpoint
    {screenX + 128, screenY + 256},   // Bottom edge midpoint
    {screenX, screenY + 128}          // Left edge midpoint
  };

  for (int i = 0; i < 4; i++) {
    float x = edgeMidpoints[i][0];
    float y = edgeMidpoints[i][1];

    float relX = x - CENTER_X;
    float relY = y - currentCenterY;

    float rotatedX = relX * cosAngle - relY * sinAngle + CENTER_X;
    float rotatedY = relX * sinAngle + relY * cosAngle + currentCenterY;

    if (rotatedX >= 0 && rotatedX < DISPLAY_WIDTH &&
        rotatedY >= 0 && rotatedY < MAP_DISPLAY_HEIGHT) {
      return true;
    }
  }

  return false;
}

void calculateVisibleTiles(double lat, double lon, int zoom) {
  tileCount = 0;

  int centerTileX, centerTileY;
  double centerPixelX, centerPixelY;
  getTileCoordinates(lat, lon, zoom, &centerTileX, &centerTileY, &centerPixelX, &centerPixelY);

  int centerTileScreenX = CENTER_X - (int)centerPixelX;
  int centerTileScreenY = currentCenterY - (int)centerPixelY;

  // Determine search range based on rotation
  int searchRange;
  if (mapRotation == 0) {
    searchRange = 2;  // 5x5 grid for non-rotated (we'll filter with visibility check)
  } else {
    // For rotated maps, we need a bit larger search space
    searchRange = 3;  // 7x7 grid (we'll filter with visibility check)
  }

  // Check each tile in the search range
  for (int dy = -searchRange; dy <= searchRange; dy++) {
    for (int dx = -searchRange; dx <= searchRange; dx++) {
      int tileX = centerTileX + dx;
      int tileY = centerTileY + dy;

      int screenX = centerTileScreenX + (dx * 256);
      int screenY = centerTileScreenY + (dy * 256);

      // Check if this tile will be visible after rotation
      if (isTileVisible(screenX, screenY, mapRotation)) {
        if (tileCount < 25) {
          tilesToRender[tileCount].tileX = tileX;
          tilesToRender[tileCount].tileY = tileY;
          tilesToRender[tileCount].screenX = screenX;
          tilesToRender[tileCount].screenY = screenY;
          tileCount++;
        }
      }
    }
  }

  Serial.printf("Rotation: %d° - Loading %d tiles\n", mapRotation, tileCount);
}

/**
 * Load and render a preprocessed 1-bit tile from SD card or cache
 * Tile format: 256x256 pixels, 1 bit per pixel, packed (8KB total)
 * Uses PSRAM cache for fast access on repeated renders
 */
bool loadAndRenderTile(int tileX, int tileY, int zoom, int screenX, int screenY) {
  uint8_t* tileData = nullptr;
  bool fromCache = false;

  // STEP 1: Try to get tile from PSRAM cache
  tileData = tileCacheLookup(zoom, tileX, tileY);
  if (tileData) {
    // Cache HIT! Use cached data
    fromCache = true;
  } else {
    // Cache MISS - need to load from SD card
    // Build file path: /Map/{zoom}/{tileX}/{tileY}.bin
    char tilePath[64];
    sprintf(tilePath, "/Map/%d/%d/%d.bin", zoom, tileX, tileY);

    if (!SD.exists(tilePath)) {
      // Tile not on SD card - can't render
      return false;
    }

    File file = SD.open(tilePath, FILE_READ);
    if (!file) {
      return false;
    }

    // File should be exactly 8192 bytes (256x256 pixels / 8 bits per byte)
    if (file.size() != 8192) {
      file.close();
      return false;
    }

    // Get a cache slot to write into (may evict LRU tile)
    tileData = tileCacheInsert(zoom, tileX, tileY);
    if (tileData) {
      // Read entire tile into cache
      file.read(tileData, 8192);
      fromCache = false; // Just loaded, not from existing cache
    } else {
      // Cache not available - fall back to line-by-line rendering (slower)
      file.close();

      // Fallback: render directly from SD without cache
      file = SD.open(tilePath, FILE_READ);
      if (!file) return false;

      float rotationRad = mapRotation * M_PI / 180.0;
      float cosAngle = cos(rotationRad);
      float sinAngle = sin(rotationRad);
      uint8_t lineBuffer[32];

      for (int y = 0; y < 256; y++) {
        file.read(lineBuffer, 32);
        for (int x = 0; x < 256; x++) {
          uint8_t byteVal = lineBuffer[x / 8];
          uint8_t bitIndex = 7 - (x % 8);
          bool isWhite = (byteVal >> bitIndex) & 1;
          if (isWhite) continue;
          if (radarMapLightenEnabled && ((x + y) & 1)) continue;

          int screenX_original = screenX + x;
          int screenY_original = screenY + y;
          int screenX_final = screenX_original;
          int screenY_final = screenY_original;

          if (mapRotation != 0) {
            float relX = screenX_original - CENTER_X;
            float relY = screenY_original - currentCenterY;
            float rotatedX = relX * cosAngle - relY * sinAngle;
            float rotatedY = relX * sinAngle + relY * cosAngle;
            screenX_final = (int)(rotatedX + CENTER_X + 0.5);
            screenY_final = (int)(rotatedY + currentCenterY + 0.5);
          }

          if (screenX_final >= 0 && screenX_final < DISPLAY_WIDTH &&
              screenY_final >= 0 && screenY_final < MAP_DISPLAY_HEIGHT) {
            display.drawPixel(screenX_final, screenY_final, GxEPD_BLACK);
          }
        }
      }
      file.close();
      return true;
    }

    file.close();
  }

  // STEP 2: Render tile from memory (cache)
  // This is MUCH faster than reading from SD card!

  // Pre-calculate rotation parameters
  float rotationRad = mapRotation * M_PI / 180.0;
  float cosAngle = cos(rotationRad);
  float sinAngle = sin(rotationRad);

  // Render from cached tile data
  for (int y = 0; y < 256; y++) {
    // Calculate byte offset for this line (32 bytes per line)
    int lineOffset = y * 32;

    for (int x = 0; x < 256; x++) {
      // Extract bit (1 = white, 0 = black)
      int byteIndex = lineOffset + (x / 8);
      uint8_t byteVal = tileData[byteIndex];
      uint8_t bitIndex = 7 - (x % 8);
      bool isWhite = (byteVal >> bitIndex) & 1;

      // Skip white pixels (background)
      if (isWhite) {
        continue;
      }
      if (radarMapLightenEnabled && ((x + y) & 1)) {
        continue;
      }

      // Calculate original screen position (before rotation)
      int screenX_original = screenX + x;
      int screenY_original = screenY + y;

      int screenX_final = screenX_original;
      int screenY_final = screenY_original;

      // Apply rotation around center point (CENTER_X, currentCenterY)
      if (mapRotation != 0) {
        // Translate to origin (relative to center point)
        float relX = screenX_original - CENTER_X;
        float relY = screenY_original - currentCenterY;

        // Apply rotation
        float rotatedX = relX * cosAngle - relY * sinAngle;
        float rotatedY = relX * sinAngle + relY * cosAngle;

        // Translate back
        screenX_final = (int)(rotatedX + CENTER_X + 0.5);
        screenY_final = (int)(rotatedY + currentCenterY + 0.5);
      }

      // Skip if outside display area
      if (screenX_final < 0 || screenX_final >= DISPLAY_WIDTH ||
          screenY_final < 0 || screenY_final >= MAP_DISPLAY_HEIGHT) {
        continue;
      }

      // Draw pixel
      display.drawPixel(screenX_final, screenY_final, GxEPD_BLACK);
    }
  }

  return true;
}

/**
 * Draw radar overlay on top of map tiles (if enabled).
 * Uses same rotation as map tiles to stay aligned.
 */
void drawRadarOverlay(const uint8_t* frameData) {
  if (!radarOverlayEnabled || radarHasError || frameData == nullptr) return;

  const int bytesPerRow = RADAR_IMAGE_WIDTH / 8;
  int yOffset = currentCenterY - (DISPLAY_HEIGHT / 2);

  float rotationRad = mapRotation * M_PI / 180.0;
  float cosAngle = cos(rotationRad);
  float sinAngle = sin(rotationRad);

  for (int y = 0; y < RADAR_IMAGE_HEIGHT; y++) {
    int screenY_original = y + yOffset;
    int rowOffset = y * bytesPerRow;

    for (int x = 0; x < RADAR_IMAGE_WIDTH; x++) {
      int byteIndex = rowOffset + (x / 8);
      uint8_t byteVal = frameData[byteIndex];
      uint8_t bitIndex = 7 - (x % 8);
      bool isWhite = (byteVal >> bitIndex) & 1;

      if (isWhite) continue;

      int screenX_original = x;
      int screenX_final = screenX_original;
      int screenY_final = screenY_original;

      if (mapRotation != 0) {
        float relX = screenX_original - CENTER_X;
        float relY = screenY_original - currentCenterY;
        float rotatedX = relX * cosAngle - relY * sinAngle;
        float rotatedY = relX * sinAngle + relY * cosAngle;
        screenX_final = (int)(rotatedX + CENTER_X + 0.5);
        screenY_final = (int)(rotatedY + currentCenterY + 0.5);
      }

      if (screenX_final < 0 || screenX_final >= DISPLAY_WIDTH ||
          screenY_final < 0 || screenY_final >= MAP_DISPLAY_HEIGHT) {
        continue;
      }

      display.drawPixel(screenX_final, screenY_final, GxEPD_BLACK);
    }
  }
}

void updateNavigationInfoBar();  // Forward declaration

/**
 * Cohen-Sutherland line clipping algorithm
 * Clips a line segment to fit within a rectangle
 * Returns true if line is visible (at least partially), false if completely outside
 * Modifies x1, y1, x2, y2 to clipped coordinates
 */
bool clipLineToRect(int* x1, int* y1, int* x2, int* y2,
                    int rectLeft, int rectTop, int rectRight, int rectBottom) {
  // Cohen-Sutherland region codes
  const int INSIDE = 0; // 0000
  const int LEFT = 1;   // 0001
  const int RIGHT = 2;  // 0010
  const int BOTTOM = 4; // 0100
  const int TOP = 8;    // 1000

  // Compute region code for a point
  auto computeCode = [&](int x, int y) -> int {
    int code = INSIDE;
    if (x < rectLeft) code |= LEFT;
    else if (x >= rectRight) code |= RIGHT;
    if (y < rectTop) code |= BOTTOM;
    else if (y >= rectBottom) code |= TOP;
    return code;
  };

  int code1 = computeCode(*x1, *y1);
  int code2 = computeCode(*x2, *y2);

  while (true) {
    if (!(code1 | code2)) {
      // Both points inside - accept
      return true;
    } else if (code1 & code2) {
      // Both points outside same region - reject
      return false;
    } else {
      // Line needs clipping
      int codeOut = code1 ? code1 : code2;
      int x, y;

      // Find intersection point
      if (codeOut & TOP) {
        x = *x1 + (*x2 - *x1) * (rectBottom - 1 - *y1) / (*y2 - *y1);
        y = rectBottom - 1;
      } else if (codeOut & BOTTOM) {
        x = *x1 + (*x2 - *x1) * (rectTop - *y1) / (*y2 - *y1);
        y = rectTop;
      } else if (codeOut & RIGHT) {
        y = *y1 + (*y2 - *y1) * (rectRight - 1 - *x1) / (*x2 - *x1);
        x = rectRight - 1;
      } else { // LEFT
        y = *y1 + (*y2 - *y1) * (rectLeft - *x1) / (*x2 - *x1);
        x = rectLeft;
      }

      // Update the point outside the rectangle
      if (codeOut == code1) {
        *x1 = x;
        *y1 = y;
        code1 = computeCode(*x1, *y1);
      } else {
        *x2 = x;
        *y2 = y;
        code2 = computeCode(*x2, *y2);
      }
    }
  }
}

/**
 * Check if a line segment intersects with a rectangle (viewport)
 * Uses Cohen-Sutherland-style intersection testing
 */
bool lineSegmentIntersectsRect(int x1, int y1, int x2, int y2,
                                int rectLeft, int rectTop, int rectRight, int rectBottom) {
  // Check if either endpoint is inside the rectangle
  if ((x1 >= rectLeft && x1 < rectRight && y1 >= rectTop && y1 < rectBottom) ||
      (x2 >= rectLeft && x2 < rectRight && y2 >= rectTop && y2 < rectBottom)) {
    return true;
  }

  // Check if line segment intersects any of the 4 rectangle edges
  // Helper lambda to check line-line intersection
  auto linesIntersect = [](int x1, int y1, int x2, int y2,
                           int x3, int y3, int x4, int y4) -> bool {
    // Calculate the direction of the lines
    float denom = (float)((y4 - y3) * (x2 - x1) - (x4 - x3) * (y2 - y1));
    if (denom == 0) return false; // Parallel lines

    float ua = ((x4 - x3) * (y1 - y3) - (y4 - y3) * (x1 - x3)) / denom;
    float ub = ((x2 - x1) * (y1 - y3) - (y2 - y1) * (x1 - x3)) / denom;

    // Check if intersection point is on both line segments
    return (ua >= 0 && ua <= 1 && ub >= 0 && ub <= 1);
  };

  // Check intersection with all 4 edges of the rectangle
  // Top edge
  if (linesIntersect(x1, y1, x2, y2, rectLeft, rectTop, rectRight, rectTop)) return true;
  // Bottom edge
  if (linesIntersect(x1, y1, x2, y2, rectLeft, rectBottom, rectRight, rectBottom)) return true;
  // Left edge
  if (linesIntersect(x1, y1, x2, y2, rectLeft, rectTop, rectLeft, rectBottom)) return true;
  // Right edge
  if (linesIntersect(x1, y1, x2, y2, rectRight, rectTop, rectRight, rectBottom)) return true;

  return false;
}

/**
 * Draw navigation route on the map
 * Renders the GPX track with rotation applied
 * centerLat, centerLon: The map center coordinates (GPS position or scrubbed position)
 */
void drawNavigationRoute(double centerLat, double centerLon) {
  // Check if we have a navigation track loaded
  if (navigationTrack == nullptr || navigationTrackPointCount < 2) {
    return;
  }

  Serial.println("Drawing navigation route...");

  // Get line width for current zoom level
  int lineWidth = ROUTE_LINE_WIDTH[currentZoomIndex];
  Serial.printf("Route line width: %d px (zoom level %d)\n", lineWidth, zoomLevel);

  // Calculate downsampling step for performance
  // Aim for MAX_ROUTE_SEGMENTS line segments maximum
  int step = max(1, navigationTrackPointCount / MAX_ROUTE_SEGMENTS);
  int segmentsToRender = navigationTrackPointCount / step;
  Serial.printf("Rendering %d route segments (step=%d, total points=%d)\n",
                segmentsToRender, step, navigationTrackPointCount);

  // Get center tile coordinates (map center - GPS or scrubbed position)
  int centerTileX, centerTileY;
  double centerPixelX, centerPixelY;
  getTileCoordinates(centerLat, centerLon, zoomLevel,
                    &centerTileX, &centerTileY, &centerPixelX, &centerPixelY);

  // Pre-calculate rotation parameters
  float rotationRad = mapRotation * M_PI / 180.0;
  float cosAngle = cos(rotationRad);
  float sinAngle = sin(rotationRad);

  int segmentsDrawn = 0;
  int segmentsOffscreen = 0;

  // Draw route segments
  for (int i = 0; i < navigationTrackPointCount - step; i += step) {
    // Get two consecutive points for this segment
    double lat1 = navigationTrack[i].lat;
    double lon1 = navigationTrack[i].lon;
    double lat2 = navigationTrack[i + step].lat;
    double lon2 = navigationTrack[i + step].lon;

    // Convert point 1 to screen coordinates
    int tile1X, tile1Y;
    double pixel1X, pixel1Y;
    getTileCoordinates(lat1, lon1, zoomLevel, &tile1X, &tile1Y, &pixel1X, &pixel1Y);

    // Calculate screen position (non-rotated) - use double precision to avoid rounding errors
    double screen1X_original = CENTER_X + (tile1X - centerTileX) * 256.0 + (pixel1X - centerPixelX);
    double screen1Y_original = currentCenterY + (tile1Y - centerTileY) * 256.0 + (pixel1Y - centerPixelY);

    // Convert point 2 to screen coordinates
    int tile2X, tile2Y;
    double pixel2X, pixel2Y;
    getTileCoordinates(lat2, lon2, zoomLevel, &tile2X, &tile2Y, &pixel2X, &pixel2Y);

    // Use double precision for accurate coordinate transformation
    double screen2X_original = CENTER_X + (tile2X - centerTileX) * 256.0 + (pixel2X - centerPixelX);
    double screen2Y_original = currentCenterY + (tile2Y - centerTileY) * 256.0 + (pixel2Y - centerPixelY);

    // Apply rotation if needed
    int screen1X_final, screen1Y_final, screen2X_final, screen2Y_final;

    if (mapRotation != 0) {
      // Rotate point 1 - use double precision for accuracy
      double rel1X = screen1X_original - CENTER_X;
      double rel1Y = screen1Y_original - currentCenterY;
      double rotated1X = rel1X * cosAngle - rel1Y * sinAngle;
      double rotated1Y = rel1X * sinAngle + rel1Y * cosAngle;
      screen1X_final = (int)(rotated1X + CENTER_X + 0.5);
      screen1Y_final = (int)(rotated1Y + currentCenterY + 0.5);

      // Rotate point 2 - use double precision for accuracy
      double rel2X = screen2X_original - CENTER_X;
      double rel2Y = screen2Y_original - currentCenterY;
      double rotated2X = rel2X * cosAngle - rel2Y * sinAngle;
      double rotated2Y = rel2X * sinAngle + rel2Y * cosAngle;
      screen2X_final = (int)(rotated2X + CENTER_X + 0.5);
      screen2Y_final = (int)(rotated2Y + currentCenterY + 0.5);
    } else {
      // No rotation - round to nearest pixel
      screen1X_final = (int)(screen1X_original + 0.5);
      screen1Y_final = (int)(screen1Y_original + 0.5);
      screen2X_final = (int)(screen2X_original + 0.5);
      screen2Y_final = (int)(screen2Y_original + 0.5);
    }

    // Properly clip the line segment to the map display area to prevent drawing over info bar
    // Use Cohen-Sutherland algorithm for accurate clipping
    int clipped1X = screen1X_final;
    int clipped1Y = screen1Y_final;
    int clipped2X = screen2X_final;
    int clipped2Y = screen2Y_final;

    if (!clipLineToRect(&clipped1X, &clipped1Y, &clipped2X, &clipped2Y,
                        0, 0, DISPLAY_WIDTH, MAP_DISPLAY_HEIGHT)) {
      // Line is completely outside viewport - skip it
      segmentsOffscreen++;
      continue;
    }

    // Draw line with configured thickness
    // For lineWidth=1: draw single line
    // For lineWidth>1: draw with filled circle brush at each point
    if (lineWidth == 1) {
      display.drawLine(clipped1X, clipped1Y, clipped2X, clipped2Y, GxEPD_BLACK);
    } else {
      // Draw the main line first
      display.drawLine(clipped1X, clipped1Y, clipped2X, clipped2Y, GxEPD_BLACK);

      // Add thickness by drawing offset lines in a cross pattern
      int halfWidth = lineWidth / 2;
      for (int offset = 1; offset <= halfWidth; offset++) {
        // Draw offset lines in 4 directions for better coverage at all angles
        display.drawLine(clipped1X - offset, clipped1Y, clipped2X - offset, clipped2Y, GxEPD_BLACK);
        display.drawLine(clipped1X + offset, clipped1Y, clipped2X + offset, clipped2Y, GxEPD_BLACK);
        display.drawLine(clipped1X, clipped1Y - offset, clipped2X, clipped2Y - offset, GxEPD_BLACK);
        display.drawLine(clipped1X, clipped1Y + offset, clipped2X, clipped2Y + offset, GxEPD_BLACK);

        // Add diagonal offsets for even better coverage at 45-degree angles
        if (offset == 1) {
          display.drawLine(clipped1X - offset, clipped1Y - offset, clipped2X - offset, clipped2Y - offset, GxEPD_BLACK);
          display.drawLine(clipped1X + offset, clipped1Y - offset, clipped2X + offset, clipped2Y - offset, GxEPD_BLACK);
          display.drawLine(clipped1X - offset, clipped1Y + offset, clipped2X - offset, clipped2Y + offset, GxEPD_BLACK);
          display.drawLine(clipped1X + offset, clipped1Y + offset, clipped2X + offset, clipped2Y + offset, GxEPD_BLACK);
        }
      }
    }

    segmentsDrawn++;
  }

  Serial.printf("Route rendering complete: %d segments drawn, %d offscreen\n",
                segmentsDrawn, segmentsOffscreen);
}

void updateMapInfoBar() {
  // Use navigation info bar if navigation is active
  if (navigationActive) {
    updateNavigationInfoBar();
    return;
  }

  // Setup text rendering
  u8g2_display.setFontMode(1);
  u8g2_display.setForegroundColor(GxEPD_BLACK);
  u8g2_display.setBackgroundColor(GxEPD_WHITE);
  u8g2_display.setFont(u8g2_font_helvB08_tf);

  int infoY = MAP_DISPLAY_HEIGHT;

  // Fill info bar area with white
  for (int y = infoY; y < DISPLAY_HEIGHT; y++) {
    for (int x = 0; x < DISPLAY_WIDTH; x++) {
      display.drawPixel(x, y, GxEPD_WHITE);
    }
  }

  // Draw separator line at top of info bar
  display.drawLine(0, infoY, DISPLAY_WIDTH, infoY, GxEPD_BLACK);

  // Calculate baseline Y position for text (matching centralized status bar)
  int textY = DISPLAY_HEIGHT - 2;  // Same as centralized status bar

  // === LEFT SIDE: Battery Icon + GPS Icon + BLE Icon ===
  // Match exact positioning from status_bar.h

  // Battery icon (no percentage text)
  float batteryPercent = batteryManager.getPercentage();
  bool isCharging = batteryManager.getIsCharging();
  int batteryIconY = textY - 9;  // Same calculation as centralized status bar
  drawSmallBatteryIcon(1, batteryIconY, batteryPercent, isCharging);

  // GPS and BLE icons start after battery
  int iconsStartX = 1 + 18 + 2 + 2;  // 1px margin + battery (18) + tip (2) + spacing (2) = 23
  int statusIconY = textY - 11;  // Same calculation as centralized status bar

  // GPS icon (15x15)
  bool gpsActive = isGPSActive();
  drawGPSIcon(iconsStartX, statusIconY, gpsActive);

  // BLE icon (15x15)
  bool bleConnected = bluetoothEnabled && isBLEConnected();
  if (bluetoothEnabled) {
    drawBLEIcon(iconsStartX + 13 + 2, statusIconY, bleConnected);  // Icon (13) + spacing (2)
  }

  // === MODE ICON + VALUE (after BLE icon) ===
  // Prepare mode string based on current mode
  char modeStr[16];
  const uint8_t* modeIcon;
  if (currentMapMode == 0) {  // MAP_MODE_ZOOM (enum value 0)
    modeIcon = ICON_ZOOM;
    snprintf(modeStr, sizeof(modeStr), "%d", zoomLevel);
  } else if (currentMapMode == 1) {  // MAP_MODE_ROTATION (enum value 1)
    modeIcon = ICON_ROTATION;
    snprintf(modeStr, sizeof(modeStr), "%d°", mapRotation);
  } else {  // MAP_MODE_SCRUB (enum value 2)
    modeIcon = ICON_SCRUB;
    if (abs(scrubOffsetMeters) >= 1000) {
      float scrubKm = scrubOffsetMeters / 1000.0;
      snprintf(modeStr, sizeof(modeStr), "%+.1fkm", scrubKm);
    } else {
      snprintf(modeStr, sizeof(modeStr), "%+dm", scrubOffsetMeters);
    }
  }

  // Position mode icon after BLE icon
  int modeIconX = iconsStartX + 13 + 2 + 13 + 3;  // After BLE icon + spacing (3px)
  int modeIconY = textY - 12;  // Center 14px icon in 16px bar: baseline - 12
  drawSmallIcon(modeIcon, modeIconX, modeIconY, display);

  // Draw mode value text next to icon
  u8g2_display.setCursor(modeIconX + SMALL_ICON_SIZE + 2, textY);
  u8g2_display.print(modeStr);

  // === FAR RIGHT: Time (matching centralized status bar) ===
  char timeStr[8];
  if (gps.time.isValid() && gps.date.isValid() &&
      !(gps.time.hour() == 0 && gps.time.minute() == 0 && gps.time.second() == 0)) {
    LocalTime localTime = getLocalTime();
    snprintf(timeStr, sizeof(timeStr), "%02d:%02d", localTime.hour, localTime.minute);
  } else {
    snprintf(timeStr, sizeof(timeStr), "--:--");
  }

  // Calculate position to right-align time (matching centralized status bar)
  int timeTextWidth = u8g2_display.getUTF8Width(timeStr);
  int timeX = DISPLAY_WIDTH - timeTextWidth - 1;  // 1px margin from right edge

  u8g2_display.setCursor(timeX, textY);
  u8g2_display.print(timeStr);
}

/**
 * Update navigation info bar
 * Larger info bar (48px) with turn direction and next turn distance
 */
void updateNavigationInfoBar() {
  // Setup text rendering
  u8g2_display.setFontMode(1);
  u8g2_display.setForegroundColor(GxEPD_BLACK);
  u8g2_display.setBackgroundColor(GxEPD_WHITE);
  u8g2_display.setFont(u8g2_font_helvB08_tf);

  int infoY = MAP_DISPLAY_HEIGHT;

  // Fill info bar area with white (cover entire area to ensure clean background)
  // Don't skip any area - dots will be drawn on top later
  for (int y = infoY; y < DISPLAY_HEIGHT; y++) {
    for (int x = 0; x < DISPLAY_WIDTH; x++) {
      display.drawPixel(x, y, GxEPD_WHITE);
    }
  }

  // Draw separator line
  for (int x = 0; x < DISPLAY_WIDTH; x++) {
    display.drawPixel(x, infoY, GxEPD_BLACK);
  }

  // --- LINE 1: Turn icon, distance, mode icon+value ---
  int line1Y = infoY + 2;  // Reduced top padding to avoid battery icon collision

  // Check if GPS is valid - show NO GPS icon if not
  if (!gpsValid) {
    // Draw NO GPS icon on left
    drawTurnIcon(ICON_NO_GPS, 2, line1Y, display);

    // Draw "No GPS" text next to icon (same font as distance)
    u8g2_display.setFont(u8g2_font_helvB14_tf);
    u8g2_display.setCursor(2 + TURN_ICON_SIZE + 4, line1Y + 16);  // Same position as distance text
    u8g2_display.print("No GPS");
  } else {
    // Get turn icon based on nextTurnType
    const uint8_t* turnIcon;
    switch (nextTurnType) {
      case 0: turnIcon = ICON_TURN_LEFT; break;
      case 1: turnIcon = ICON_TURN_RIGHT; break;
      case 2: turnIcon = ICON_TURN_STRAIGHT; break;
      case 3: turnIcon = ICON_TURN_SLIGHT_LEFT; break;
      case 4: turnIcon = ICON_TURN_SLIGHT_RIGHT; break;
      case 5: turnIcon = ICON_TURN_SHARP_LEFT; break;
      case 6: turnIcon = ICON_TURN_SHARP_RIGHT; break;
      case 7: turnIcon = ICON_TURN_UTURN; break;
      default: turnIcon = ICON_TURN_STRAIGHT; break;
    }

    // Draw turn icon on left
    drawTurnIcon(turnIcon, 2, line1Y, display);

    // Format distance to next turn
    char distStr[16];
    if (distanceToNextTurn >= 1000) {
      snprintf(distStr, sizeof(distStr), "%.1fkm", distanceToNextTurn / 1000.0);
    } else {
      snprintf(distStr, sizeof(distStr), "%.0fm", distanceToNextTurn);
    }

    // Draw distance next to turn icon (larger font)
    u8g2_display.setFont(u8g2_font_helvB14_tf);
    u8g2_display.setCursor(2 + TURN_ICON_SIZE + 4, line1Y + 16);  // Adjusted to fit better
    u8g2_display.print(distStr);
  }

  // Draw mode icon and value on the right (to avoid interfering with turn distance)
  // Show current mode (zoom, rotation, or scrub)
  char modeStr[16];
  const uint8_t* modeIcon;
  if (currentMapMode == 0) {  // Zoom mode
    modeIcon = ICON_ZOOM;
    snprintf(modeStr, sizeof(modeStr), "%d", zoomLevel);
  } else if (currentMapMode == 1) {  // Rotation mode
    modeIcon = ICON_ROTATION;
    snprintf(modeStr, sizeof(modeStr), "%d°", mapRotation);
  } else {  // Scrub mode
    modeIcon = ICON_SCRUB;
    if (abs(scrubOffsetMeters) >= 1000) {
      float scrubKm = scrubOffsetMeters / 1000.0;
      snprintf(modeStr, sizeof(modeStr), "%+.1fkm", scrubKm);
    } else {
      snprintf(modeStr, sizeof(modeStr), "%+dm", scrubOffsetMeters);
    }
  }

  u8g2_display.setFont(u8g2_font_helvB08_tf);
  int modeTextWidth = u8g2_display.getUTF8Width(modeStr);

  // Position icon on the right side with padding
  int modeIconX = DISPLAY_WIDTH - SMALL_ICON_SIZE - 4;  // 4px padding from right edge
  drawSmallIcon(modeIcon, modeIconX, line1Y, display);

  // Position text below the icon, right-aligned
  int modeTextX = DISPLAY_WIDTH - modeTextWidth - 4;  // 4px padding from right edge
  u8g2_display.setCursor(modeTextX, line1Y + SMALL_ICON_SIZE + 11);
  u8g2_display.print(modeStr);

  // --- LINE 2: Battery icon (left) and Time (right) - matching centralized status bar ---
  // Calculate baseline Y position (matching centralized status bar)
  int textY = DISPLAY_HEIGHT - 2;

  // === LEFT: Battery %, GPS, and BLE (matching centralized status bar layout) ===
  float batteryPercent = batteryManager.getPercentage();
  bool isCharging = batteryManager.getIsCharging();
  int batteryIconY = textY - 9;  // Same calculation as centralized status bar
  drawSmallBatteryIcon(1, batteryIconY, batteryPercent, isCharging);

  char percentStr[6];
  snprintf(percentStr, sizeof(percentStr), "%.0f%%", batteryPercent);
  u8g2_display.setFont(u8g2_font_helvB08_tf);
  u8g2_display.setCursor(23, textY);
  u8g2_display.print(percentStr);

  int percentWidth = u8g2_display.getUTF8Width(percentStr);
  int iconsStartX = 23 + percentWidth + 3;
  int statusIconY = textY - 11;  // Icons aligned to baseline

  bool gpsActive = isGPSActive();
  drawGPSIcon(iconsStartX, statusIconY, gpsActive);

  bool bleConnected = bluetoothEnabled && isBLEConnected();
  if (bluetoothEnabled) {
    drawBLEIcon(iconsStartX + 13 + 2, statusIconY, bleConnected);
  }

  // === RIGHT: Time (matching centralized status bar position) ===
  char timeStr[8];
  if (gps.time.isValid() && gps.date.isValid() &&
      !(gps.time.hour() == 0 && gps.time.minute() == 0 && gps.time.second() == 0)) {
    LocalTime localTime = getLocalTime();
    snprintf(timeStr, sizeof(timeStr), "%02d:%02d",
             localTime.hour, localTime.minute);
  } else {
    snprintf(timeStr, sizeof(timeStr), "--:--");
  }

  int timeTextWidth = u8g2_display.getUTF8Width(timeStr);
  int timeX = DISPLAY_WIDTH - timeTextWidth - 1;  // 1px margin from right edge (matching centralized status bar)
  u8g2_display.setCursor(timeX, textY);
  u8g2_display.print(timeStr);
}

// Refresh just the info bar (for mode changes)
void refreshMapInfoBar() {
  // Use partial window to only update the info bar area
  display.setPartialWindow(0, MAP_DISPLAY_HEIGHT, DISPLAY_WIDTH, currentInfoBarHeight);
  display.firstPage();

  do {
    updateMapInfoBar();

    // Draw page dots if navigation is active (they're in the info bar area)
    if (navigationActive) {
      drawPageDots();
    }
  } while (display.nextPage());
}

void loadAndDisplayMap() {
  Serial.println("Loading map tiles from SD card...");

  // Use scrubbed position if scrub offset is active, otherwise use GPS position
  // Scrub offset persists across all modes (ZOOM, ROTATION, SCRUB)
  double centerLat = currentLat;
  double centerLon = currentLon;
  if (scrubOffsetMeters != 0 && navigationActive) {
    centerLat = scrubLat;
    centerLon = scrubLon;
  }

  calculateVisibleTiles(centerLat, centerLon, zoomLevel);

  // Start display update - ONE e-ink refresh for ALL tiles
  display.setPartialWindow(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
  display.firstPage();

  do {
    display.fillScreen(GxEPD_WHITE);
    radarMapLightenEnabled = false;

    // Load and render ALL tiles from SD card
    for (int i = 0; i < tileCount; i++) {
      int tileX = tilesToRender[i].tileX;
      int tileY = tilesToRender[i].tileY;
      int screenX = tilesToRender[i].screenX;
      int screenY = tilesToRender[i].screenY;

      Serial.printf("Tile %d/%d: x=%d y=%d z=%d\n",
                    i+1, tileCount, tileX, tileY, zoomLevel);

      if (!loadAndRenderTile(tileX, tileY, zoomLevel, screenX, screenY)) {
        Serial.println("Tile not found on SD card");
      }
    }

    // Radar overlay rendering is handled only on the radar page.

    // Draw navigation route on top of tiles (if navigation is active)
    if (navigationActive && navigationTrack != nullptr) {
      drawNavigationRoute(centerLat, centerLon);
    }

    // Draw location marker or navigation arrow on top
    // When scrubbed (offset != 0), draw crosshair marker regardless of mode
    if (navigationActive) {
      if (scrubOffsetMeters != 0) {
        // Draw scrub position marker (crosshair + ring for high visibility)
        // Outer ring
        display.drawCircle(CENTER_X, currentCenterY, 8, GxEPD_BLACK);
        display.drawCircle(CENTER_X, currentCenterY, 7, GxEPD_BLACK);
        // Inner dot
        display.fillCircle(CENTER_X, currentCenterY, 2, GxEPD_BLACK);
        // Crosshair lines
        display.drawLine(CENTER_X - 12, currentCenterY, CENTER_X - 10, currentCenterY, GxEPD_BLACK);
        display.drawLine(CENTER_X + 10, currentCenterY, CENTER_X + 12, currentCenterY, GxEPD_BLACK);
        display.drawLine(CENTER_X, currentCenterY - 12, CENTER_X, currentCenterY - 10, GxEPD_BLACK);
        display.drawLine(CENTER_X, currentCenterY + 10, CENTER_X, currentCenterY + 12, GxEPD_BLACK);
      } else {
        // Normal navigation arrow at GPS position
        drawNavigationArrow(CENTER_X, currentCenterY, display);
      }
    } else {
      drawLocationMarker(CENTER_X, currentCenterY, display);
    }

    // Draw info bar
    updateMapInfoBar();

    // Draw page dots (only show during navigation mode)
    if (navigationActive) {
      drawPageDots();
    }

    if (speedometerSplitEnabled) {
      drawSpeedometerSplitOverlay();
      lastSpeedometerOverlayUpdate = millis();
    }

    // Draw notification overlay
    drawNotificationOverlay();

  } while (display.nextPage());

  Serial.println("Map fully loaded and displayed");
  printTileCacheStats();  // Show cache performance
}

#endif // MAP_RENDERING_H
