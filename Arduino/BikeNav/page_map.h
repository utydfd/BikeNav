#ifndef PAGE_MAP_H
#define PAGE_MAP_H

#include <GxEPD2_BW.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <SD.h>
#include <ArduinoJson.h>
#include "notification_system.h"
#include "controls_helper.h"

// External references from main program
extern GxEPD2_BW<GxEPD2_290_BS, GxEPD2_290_BS::HEIGHT> display;
extern U8G2_FOR_ADAFRUIT_GFX u8g2_display;
extern const int DISPLAY_WIDTH;
extern const int DISPLAY_HEIGHT;
extern double currentLat;
extern double currentLon;
extern bool gpsValid;
extern bool navigationActive;
extern bool tripRecording;
extern void drawStatusBar();
extern void drawStatusBarNoSeparator();

// External button flags from main program
extern volatile bool buttonPressed;
extern volatile bool backPressed;

// External auto-rotation state from map_navigation.h
extern bool autoRotationEnabled;
extern unsigned long lastManualRotationTime;

// Encoder button long-press detection for reset view
#define ENCODER_LONG_PRESS_MS 1500
unsigned long encoderButtonPressTime = 0;
bool encoderButtonLongPressChecked = false;

// External navigation elevation data from map_navigation.h
extern float currentElevation;
extern float plannedElevationGain;
extern float plannedElevationLoss;

// External navigation distance tracking from map_navigation.h
extern float distanceTraveled;
extern float totalDistanceRemaining;
extern float totalDistance;
extern bool speedometerSplitEnabled;

bool updateSpeedometerData();
void renderSpeedometerSplitOverlay();

// --- MAP SUB-PAGES ---
enum MapSubPage {
  MAP_SUBPAGE_MAP,           // Main map view
  MAP_SUBPAGE_TRIP_STATS,    // Navigation statistics (only if navigationActive)
  MAP_SUBPAGE_HEIGHT_PROFILE,// Elevation profile (only if navigationActive)
  MAP_SUBPAGE_TRIPS,         // Saved trips list
  MAP_SUBPAGE_TRIP_DETAIL    // Trip detail view (metadata, map placeholder, buttons)
};

MapSubPage currentMapSubPage = MAP_SUBPAGE_MAP;

// --- MAP INTERACTION MODE ---
enum MapMode {
  MAP_MODE_ZOOM,      // Encoder controls zoom level
  MAP_MODE_ROTATION,  // Encoder controls map rotation
  MAP_MODE_SCRUB      // Encoder scrubs along route (navigation only)
};

MapMode currentMapMode = MAP_MODE_ZOOM;
int mapRotation = 0;  // Map rotation angle in degrees (0-360)
const int ROTATION_STEP = 18;  // Degrees per encoder step

// Rotation debounce - wait for user to stop rotating before refreshing display
unsigned long lastRotationChange = 0;
bool rotationPending = false;
const unsigned long ROTATION_DEBOUNCE_MS = 50;  // Wait 50ms after last rotation change (faster response)

// Scrub mode - pan along route in navigation mode
int scrubOffsetMeters = 0;  // Offset from current GPS position (+/- meters)
double scrubLat = 0.0;      // Calculated latitude at scrubbed position
double scrubLon = 0.0;      // Calculated longitude at scrubbed position
unsigned long lastScrubChange = 0;
bool scrubPending = false;
const unsigned long SCRUB_TIMEOUT_MS = 15000;  // 15 seconds like rotation timeout

// --- ZOOM STATE ---
int currentZoomIndex = 3;  // Start at zoom 15
int zoomLevel = 15;

// --- MAP UPDATE TIMING ---
unsigned long lastMapUpdate = 0;
const unsigned long MAP_UPDATE_INTERVAL = 10000;  // 10 seconds
const unsigned long SPEEDOMETER_OVERLAY_UPDATE_INTERVAL = 1000;
unsigned long lastSpeedometerOverlayUpdate = 0;
unsigned long lastStatsPageUpdate = 0;  // Track stats page refresh timing
const unsigned long STATS_UPDATE_INTERVAL = 10000;  // 10 seconds for stats page

// --- ELEVATION GRAPH STATE ---
// Distance options for elevation graph (in meters)
const int ELEVATION_DISTANCES[] = {100, 250, 500, 1000, 2000, 3000, 5000, 7000, 10000};
const int ELEVATION_DISTANCE_COUNT = 9;
int selectedElevationDistanceIndex = 4;  // Default to 2km (index 4)
int lastUpcomingDistanceMeters = ELEVATION_DISTANCES[selectedElevationDistanceIndex];
int totalTripWindowMeters = 0;
float totalTripGraphOffset = 0.0f;

int getRemainingElevationDistanceMeters() {
  if (!navigationActive) {
    return ELEVATION_DISTANCES[ELEVATION_DISTANCE_COUNT - 1];
  }
  float remaining = totalDistanceRemaining;
  if (remaining < 0.0f) {
    remaining = 0.0f;
  }
  return (int)(remaining + 0.5f);
}

int getElevationDistanceOptionCount() {
  int remainingMeters = getRemainingElevationDistanceMeters();
  int maxPreset = ELEVATION_DISTANCES[ELEVATION_DISTANCE_COUNT - 1];

  if (remainingMeters >= maxPreset) {
    return ELEVATION_DISTANCE_COUNT;
  }

  int count = 0;
  for (int i = 0; i < ELEVATION_DISTANCE_COUNT; i++) {
    if (ELEVATION_DISTANCES[i] <= remainingMeters) {
      count++;
    }
  }

  if (count == 0) {
    return 1;
  }

  if (ELEVATION_DISTANCES[count - 1] != remainingMeters) {
    count++;
  }

  return count;
}

int getElevationDistanceOption(int index) {
  int remainingMeters = getRemainingElevationDistanceMeters();
  int maxPreset = ELEVATION_DISTANCES[ELEVATION_DISTANCE_COUNT - 1];

  if (remainingMeters >= maxPreset) {
    return ELEVATION_DISTANCES[index];
  }

  int count = 0;
  for (int i = 0; i < ELEVATION_DISTANCE_COUNT; i++) {
    if (ELEVATION_DISTANCES[i] <= remainingMeters) {
      if (count == index) {
        return ELEVATION_DISTANCES[i];
      }
      count++;
    }
  }

  return remainingMeters;
}

void clampElevationDistanceIndex() {
  int optionCount = getElevationDistanceOptionCount();
  if (selectedElevationDistanceIndex < 0) {
    selectedElevationDistanceIndex = 0;
  } else if (selectedElevationDistanceIndex >= optionCount) {
    selectedElevationDistanceIndex = optionCount - 1;
  }
}

int getSelectedElevationDistanceMeters() {
  clampElevationDistanceIndex();
  return getElevationDistanceOption(selectedElevationDistanceIndex);
}

int getTotalTripWindowMeters() {
  int windowMeters = totalTripWindowMeters;
  if (windowMeters <= 0) {
    windowMeters = lastUpcomingDistanceMeters > 0
        ? lastUpcomingDistanceMeters
        : getSelectedElevationDistanceMeters();
  }

  if (totalDistance > 0.0f && windowMeters > totalDistance) {
    windowMeters = (int)(totalDistance + 0.5f);
  }

  if (windowMeters < 1) {
    windowMeters = 1;
  }

  return windowMeters;
}

void clampTotalTripGraphOffset(int windowMeters) {
  if (windowMeters < 0) {
    windowMeters = 0;
  }

  float maxOffset = max(0.0f, totalDistance - windowMeters);
  if (totalTripGraphOffset < 0.0f) {
    totalTripGraphOffset = 0.0f;
  } else if (totalTripGraphOffset > maxOffset) {
    totalTripGraphOffset = maxOffset;
  }
}


// Stat view modes for elevation page
enum ElevationStatView {
  ELEV_STATS_UPCOMING,   // Show upcoming elevation stats (peak, low, change in selected distance)
  ELEV_STATS_TOTAL       // Show total trip elevation stats (total gain/loss, current)
};

ElevationStatView currentElevStatView = ELEV_STATS_UPCOMING;

// Processed elevation data for graphing
struct ElevationGraphData {
  int16_t* elevations;      // Array of elevation values (sampled/downsampled)
  float* distances;         // Array of corresponding distances from current position
  int pointCount;           // Number of points in arrays
  int16_t minElev;          // Minimum elevation in range
  int16_t maxElev;          // Maximum elevation in range
  int16_t elevGain;         // Total elevation gain in range
  int16_t elevLoss;         // Total elevation loss in range
  bool dataValid;           // Whether data has been processed
};

ElevationGraphData elevGraphData = {nullptr, nullptr, 0, 0, 0, 0, 0, false};

// Include the specialized map modules
// IMPORTANT: map_trips.h must come first as it defines TrackPoint structure
#include "map_trips.h"
#include "map_rendering.h"
#include "map_navigation.h"
#include "page_trips.h"  // Standalone trips page

// --- SHARED UI FUNCTIONS ---

// Forward declaration
bool hasElevationData();

// Draw page navigation dots (matching info page design)
void drawPageDots() {
  const int DOT_RADIUS = 3;
  const int DOT_SPACING = 10;
  const int NAV_DOT_LINE_OFFSET = 16;  // Move dots up one text line during navigation
  const int INDICATOR_Y = navigationActive
      ? DISPLAY_HEIGHT - 4 - NAV_DOT_LINE_OFFSET
      : DISPLAY_HEIGHT - 4;  // 4px from bottom (same as info page)

  // Dynamic page count based on navigationActive state and elevation data availability
  // When navigationActive is true: MAP, TRIP_STATS, [HEIGHT_PROFILE if has elevation] (2-3 pages)
  // When navigationActive is false: MAP, TRIPS (2 pages)
  int totalPages;
  if (navigationActive) {
    // Check if elevation data is available
    totalPages = hasElevationData() ? 3 : 2;
  } else {
    totalPages = 2;
  }

  // Calculate total width needed
  int totalWidth = (totalPages * DOT_RADIUS * 2) + ((totalPages - 1) * DOT_SPACING);
  int startX = (DISPLAY_WIDTH - totalWidth) / 2;  // Center horizontally

  // Map currentMapSubPage to dot index
  int currentDotIndex;
  if (navigationActive) {
    // Navigation mode: 2-3 pages available depending on elevation data
    // MAP_SUBPAGE_MAP (0) -> dot 0
    // MAP_SUBPAGE_TRIP_STATS (1) -> dot 1
    // MAP_SUBPAGE_HEIGHT_PROFILE (2) -> dot 2 (only if elevation data available)
    if (currentMapSubPage == MAP_SUBPAGE_MAP) {
      currentDotIndex = 0;
    } else if (currentMapSubPage == MAP_SUBPAGE_TRIP_STATS) {
      currentDotIndex = 1;
    } else if (currentMapSubPage == MAP_SUBPAGE_HEIGHT_PROFILE) {
      currentDotIndex = 2;
    } else {
      currentDotIndex = 0;  // Shouldn't reach trips in nav mode via dots
    }
  } else {
    // Normal mode: 2 pages
    // MAP_SUBPAGE_MAP (0) -> dot 0
    // MAP_SUBPAGE_TRIPS (3) -> dot 1
    if (currentMapSubPage == MAP_SUBPAGE_MAP) {
      currentDotIndex = 0;
    } else {
      currentDotIndex = 1;  // MAP_SUBPAGE_TRIPS
    }
  }

  for (int i = 0; i < totalPages; i++) {
    int dotX = startX + (i * (DOT_RADIUS * 2 + DOT_SPACING)) + DOT_RADIUS;

    if (i == currentDotIndex) {
      // Current page - filled circle
      display.fillCircle(dotX, INDICATOR_Y, DOT_RADIUS, GxEPD_BLACK);
    } else {
      // Other pages - empty circle
      display.drawCircle(dotX, INDICATOR_Y, DOT_RADIUS, GxEPD_BLACK);
    }
  }
}

// --- MAIN PAGE INTERFACE FUNCTIONS ---

// Forward declarations for view functions
void renderHeightProfileView();
void freeElevationGraphData();

// Function to open trip detail view (called from page_trips.h)
void openTripDetail(const char* tripDirName) {
  strncpy(selectedTripDirName, tripDirName, sizeof(selectedTripDirName) - 1);
  selectedTripDirName[sizeof(selectedTripDirName) - 1] = '\0';

  selectedTripButton = 0;  // Reset to Start button

  // Load GPX data into PSRAM
  extern bool loadTripForDetails(const char* tripDirName);
  loadTripForDetails(selectedTripDirName);

  currentMapSubPage = MAP_SUBPAGE_TRIP_DETAIL;
  renderTripDetailView();
}

// Helper function to check if elevation data is available
bool hasElevationData() {
  extern TrackPoint* navigationTrack;
  extern int navigationTrackPointCount;

  if (navigationTrack == nullptr) {
    Serial.println("hasElevationData: navigationTrack is nullptr");
    return false;
  }

  if (navigationTrackPointCount == 0) {
    Serial.println("hasElevationData: navigationTrackPointCount is 0");
    return false;
  }

  // Check if any elevation values are non-zero
  int nonZeroCount = 0;
  for (int i = 0; i < navigationTrackPointCount; i++) {
    if (navigationTrack[i].elev != 0) {
      nonZeroCount++;
    }
  }

  Serial.printf("hasElevationData: %d/%d points have non-zero elevation\n", nonZeroCount, navigationTrackPointCount);

  if (nonZeroCount > 0) {
    return true;
  }

  Serial.println("hasElevationData: All elevations are 0, hiding elevation page");
  return false;  // All elevations are 0
}

void renderMapPage() {
  switch (currentMapSubPage) {
    case MAP_SUBPAGE_MAP:
      loadAndDisplayMap();  // Load map synchronously - tiles are on SD card, fast enough
      break;
    case MAP_SUBPAGE_TRIP_STATS:
      // Use navigation stats if navigation is active, otherwise use placeholder
      if (navigationActive) {
        renderNavigationStatsView();
        lastStatsPageUpdate = millis();  // Reset timer when page is first shown
      } else {
        renderTripStatsView();
      }
      break;
    case MAP_SUBPAGE_HEIGHT_PROFILE:
      renderHeightProfileView();
      break;
    case MAP_SUBPAGE_TRIPS:
      renderTripsPage();  // New standalone trips page
      break;
    case MAP_SUBPAGE_TRIP_DETAIL:
      renderTripDetailView();
      break;
  }
}

void initMapPage() {
  zoomLevel = ZOOM_LEVELS[currentZoomIndex];
  currentMapSubPage = MAP_SUBPAGE_MAP;
  lastMapUpdate = millis();
  lastSpeedometerOverlayUpdate = 0;

  // Set proper map display parameters based on navigation mode
  if (navigationActive) {
    currentCenterY = CENTER_Y_NAV;
    currentInfoBarHeight = MAP_INFO_BAR_HEIGHT_NAV;
    MAP_DISPLAY_HEIGHT = DISPLAY_HEIGHT - MAP_INFO_BAR_HEIGHT_NAV;
  } else {
    currentCenterY = CENTER_Y_NORMAL;
    currentInfoBarHeight = MAP_INFO_BAR_HEIGHT_NORMAL;
    MAP_DISPLAY_HEIGHT = DISPLAY_HEIGHT - MAP_INFO_BAR_HEIGHT_NORMAL;
  }
}

void updateMapPage() {
  if (speedometerSplitEnabled) {
    updateSpeedometerData();
  }

  // Check if trip detail needs redraw (triggered by BLE data arrival)
  if (currentMapSubPage == MAP_SUBPAGE_TRIP_DETAIL) {
    extern bool tripDetailNeedsRedraw;
    extern bool isNavigateHomeMode;
    extern bool waitingForNavigateHomePath;
    extern bool navigateHomeHasError;
    extern bool showNavigateHomeError;

    if (isNavigateHomeMode && waitingForNavigateHomePath) {
      checkNavigateHomeTimeout();
    }

    if (isNavigateHomeMode && navigateHomeHasError && !showNavigateHomeError) {
      tripDetailNeedsRedraw = true;
    }

    if (tripDetailNeedsRedraw) {
      Serial.println("Deferred trip detail redraw triggered");
      renderTripDetailView();
      return;
    }
  }

  // Check if we need to redraw trips list after scrolling has stopped
  if (currentMapSubPage == MAP_SUBPAGE_TRIPS) {
    updateTripsPage();  // New trips page update function
    return;
  }

  // Auto-refresh stats page every 10 seconds to update elapsed time etc.
  if (currentMapSubPage == MAP_SUBPAGE_TRIP_STATS && navigationActive) {
    if (millis() - lastStatsPageUpdate >= STATS_UPDATE_INTERVAL) {
      Serial.println("Stats page auto-refresh");
      updateNavigationState();  // Update navigation data
      renderNavigationStatsView();
      lastStatsPageUpdate = millis();
    }
    return;
  }

  // Force exit from SCRUB mode if navigation becomes inactive
  if (currentMapMode == MAP_MODE_SCRUB && !navigationActive) {
    currentMapMode = MAP_MODE_ZOOM;
    scrubOffsetMeters = 0;
    scrubPending = false;
    Serial.println("Navigation stopped: exiting SCRUB mode");
    refreshMapInfoBar();
  }
  // Also reset scrub offset if navigation stops in any mode
  if (!navigationActive && scrubOffsetMeters != 0) {
    scrubOffsetMeters = 0;
    Serial.println("Navigation stopped: resetting scrub offset");
  }

  // Check scrub timeout - reset to GPS position after 15 seconds of inactivity
  // Applies when scrubbed, regardless of current mode (ZOOM, ROTATION, or SCRUB)
  if (scrubOffsetMeters != 0 &&
      !scrubPending && !rotationPending &&
      (millis() - lastScrubChange >= SCRUB_TIMEOUT_MS)) {
    scrubOffsetMeters = 0;
    Serial.println("Scrub timeout: resetting to GPS position");
    loadAndDisplayMap();
    lastMapUpdate = millis();
    return;  // Don't check other updates if we just reset scrub
  }

  // Check if we need to redraw after scrub has stopped
  if (scrubPending && (millis() - lastScrubChange >= ROTATION_DEBOUNCE_MS)) {
    scrubPending = false;
    // Apply auto-rotation based on scrubbed position if enabled
    if (autoRotationEnabled && navigationActive) {
      calculateAutoRotation();
    }
    loadAndDisplayMap();
    lastMapUpdate = millis();
    return;  // Don't check periodic update if we just did a scrub redraw
  }

  // Check if we need to redraw after rotation has stopped
  if (rotationPending && (millis() - lastRotationChange >= ROTATION_DEBOUNCE_MS)) {
    rotationPending = false;
    // Update navigation state before screen refresh
    if (navigationActive) {
      updateNavigationState();
    }
    loadAndDisplayMap();
    lastMapUpdate = millis();
    return;  // Don't check periodic update if we just did a rotation redraw
  }

  // Check if GPS position changed - update screen immediately for responsive navigation
  // IMPORTANT: Don't interrupt rotation or scrub debouncing - wait for them to complete first
  // When scrubbed, GPS updates don't trigger screen refresh (viewing scrubbed position)
  extern bool gpsPositionChanged;
  if (currentMapSubPage == MAP_SUBPAGE_MAP && gpsPositionChanged &&
      !rotationPending && !scrubPending) {
    gpsPositionChanged = false;  // Clear flag
    // Only redraw if not viewing a scrubbed position
    if (scrubOffsetMeters == 0) {
      // Update navigation state before screen refresh
      if (navigationActive) {
        updateNavigationState();
      }
      loadAndDisplayMap();
      lastMapUpdate = millis();
      return;  // Don't check periodic update if we just did a GPS update
    }
  }

  // Periodic refresh for map view (fallback if GPS stops updating)
  // Don't interrupt rotation or scrub debouncing - wait for them to complete first
  // Also don't refresh when viewing a scrubbed position
  if (currentMapSubPage == MAP_SUBPAGE_MAP &&
      (millis() - lastMapUpdate >= MAP_UPDATE_INTERVAL) &&
      !rotationPending && !scrubPending && scrubOffsetMeters == 0) {
    // Update navigation state before screen refresh
    if (navigationActive) {
      updateNavigationState();
    }
    loadAndDisplayMap();
    lastMapUpdate = millis();
  }

  if (speedometerSplitEnabled && currentMapSubPage == MAP_SUBPAGE_MAP) {
    unsigned long now = millis();
    if (!rotationPending && !scrubPending &&
        now - lastSpeedometerOverlayUpdate >= SPEEDOMETER_OVERLAY_UPDATE_INTERVAL) {
      renderSpeedometerSplitOverlay();
      lastSpeedometerOverlayUpdate = now;
    }
  }
}

void handleMapEncoder(int delta) {
  // Handle elevation page distance selection or scrolling
  if (currentMapSubPage == MAP_SUBPAGE_HEIGHT_PROFILE) {
    if (currentElevStatView == ELEV_STATS_TOTAL) {
      int windowMeters = getTotalTripWindowMeters();
      int stepMeters = max(1, windowMeters / 4);
      totalTripGraphOffset += delta * stepMeters;
      clampTotalTripGraphOffset(windowMeters);
      Serial.printf("Elevation window start: %dm\n", (int)totalTripGraphOffset);
    } else {
      // Scroll through distance options
      selectedElevationDistanceIndex += delta;

      // Clamp to bounds (no wrap-around)
      clampElevationDistanceIndex();

      int selectedDistance = getSelectedElevationDistanceMeters();
      lastUpcomingDistanceMeters = selectedDistance;
      Serial.printf("Elevation distance changed to: %dm\n", selectedDistance);
    }

    // Re-render page with new distance selection
    renderHeightProfileView();
    return;
  }

  // Handle trips list scrolling with immediate rendering (no delay)
  if (currentMapSubPage == MAP_SUBPAGE_TRIPS) {
    handleTripsEncoder(delta);  // New trips page encoder handler
    return;
  }

  // Handle trip detail button selection
  if (currentMapSubPage == MAP_SUBPAGE_TRIP_DETAIL) {
    // Don't allow button switching when error dialog is showing
    extern bool showNavigateHomeError;
    if (showNavigateHomeError) {
      return;
    }

    // Don't allow button switching when confirmation dialog is showing
    if (showDeleteConfirmation) {
      return;
    }

    // Don't allow button switching in Navigate Home mode (only one button)
    extern bool isNavigateHomeMode;
    if (isNavigateHomeMode) {
      return;  // No button switching needed
    }

    if (delta > 0) {
      // Move to next button (Start -> Delete)
      selectedTripButton = 1;
    } else {
      // Move to previous button (Delete -> Start)
      selectedTripButton = 0;
    }
    renderTripDetailView();
    return;
  }

  // Only allow map interaction on map view
  if (currentMapSubPage != MAP_SUBPAGE_MAP) return;

  if (currentMapMode == MAP_MODE_ZOOM) {
    // ZOOM MODE: Change zoom level
    // Apply all accumulated zoom steps at once (handles fast rotation)
    currentZoomIndex -= delta;  // delta > 0 means zoom in (decrease index)

    // Clamp to valid range
    if (currentZoomIndex < 0) {
      currentZoomIndex = 0;
    } else if (currentZoomIndex >= ZOOM_COUNT) {
      currentZoomIndex = ZOOM_COUNT - 1;
    }

    zoomLevel = ZOOM_LEVELS[currentZoomIndex];

    // Reset scrub timeout when zooming while scrubbed
    if (scrubOffsetMeters != 0) {
      lastScrubChange = millis();
    }

    loadAndDisplayMap();  // Load and display immediately
    lastMapUpdate = millis();
  } else if (currentMapMode == MAP_MODE_ROTATION) {
    // ROTATION MODE: Update rotation immediately in memory, defer display refresh
    // Apply all accumulated rotation steps at once (handles fast rotation)
    mapRotation += delta * ROTATION_STEP;

    // Normalize to 0-359 range
    while (mapRotation >= 360) {
      mapRotation -= 360;
    }
    while (mapRotation < 0) {
      mapRotation += 360;
    }

    Serial.printf("Map rotation: %d degrees (%d steps, pending refresh)\n", mapRotation, delta);

    // If navigation is active, disable auto-rotation temporarily
    if (navigationActive) {
      if (autoRotationEnabled) {
        Serial.println("Manual rotation: disabling auto-rotation temporarily");
        autoRotationEnabled = false;
      }
      lastManualRotationTime = millis();
    }

    // Reset scrub timeout when rotating while scrubbed
    if (scrubOffsetMeters != 0) {
      lastScrubChange = millis();
    }

    // Mark that we need to redraw, but don't do it yet
    rotationPending = true;
    lastRotationChange = millis();
  } else if (currentMapMode == MAP_MODE_SCRUB) {
    // SCRUB MODE: Move along route forward/backward, defer display refresh
    // Apply delta based on current zoom level
    scrubOffsetMeters += delta * SCRUB_STEP_METERS[currentZoomIndex];

    // Clamp scrub offset to route bounds
    // Can't go before start: minimum offset = -distanceTraveled
    // Can't go past end: maximum offset = totalDistanceRemaining
    int minOffset = -(int)distanceTraveled;
    int maxOffset = (int)totalDistanceRemaining;

    if (scrubOffsetMeters < minOffset) {
      scrubOffsetMeters = minOffset;
      Serial.println("Scrub: reached start of route");
    } else if (scrubOffsetMeters > maxOffset) {
      scrubOffsetMeters = maxOffset;
      Serial.println("Scrub: reached end of route");
    }

    Serial.printf("Scrub offset: %d meters (clamped to %d..%d)\n",
                  scrubOffsetMeters, minOffset, maxOffset);

    // Calculate new position along route
    calculateScrubPosition(scrubOffsetMeters, &scrubLat, &scrubLon);

    // Mark that we need to redraw, but don't do it yet
    scrubPending = true;
    lastScrubChange = millis();
  }
}

void handleMapButton() {
  // If there's a pending rotation or scrub redraw, force it now before mode change
  // This ensures the user sees the final state before switching modes
  if (rotationPending) {
    rotationPending = false;
    loadAndDisplayMap();
    lastMapUpdate = millis();
  }
  if (scrubPending) {
    scrubPending = false;
    loadAndDisplayMap();
    lastMapUpdate = millis();
  }

  // Button actions based on current sub-page
  if (currentMapSubPage == MAP_SUBPAGE_MAP) {
    // Cycle through modes: ZOOM → ROTATION → SCRUB (if nav active) → ZOOM
    if (currentMapMode == MAP_MODE_ZOOM) {
      currentMapMode = MAP_MODE_ROTATION;
      Serial.println("Map mode: ROTATION");
    } else if (currentMapMode == MAP_MODE_ROTATION) {
      // Only allow SCRUB mode when navigation is active
      if (navigationActive) {
        currentMapMode = MAP_MODE_SCRUB;
        // Keep current scrubOffsetMeters - it persists across mode changes
        Serial.println("Map mode: SCRUB");
      } else {
        currentMapMode = MAP_MODE_ZOOM;
        Serial.println("Map mode: ZOOM");
      }
    } else if (currentMapMode == MAP_MODE_SCRUB) {
      currentMapMode = MAP_MODE_ZOOM;
      // Don't reset scrubOffsetMeters - it persists across mode changes
      Serial.println("Map mode: ZOOM (scrub offset preserved)");
    }
    // Refresh info bar to show new mode icon immediately
    refreshMapInfoBar();
  } else if (currentMapSubPage == MAP_SUBPAGE_HEIGHT_PROFILE) {
    // Toggle between upcoming and total trip stat views
    if (currentElevStatView == ELEV_STATS_UPCOMING) {
      currentElevStatView = ELEV_STATS_TOTAL;
      lastUpcomingDistanceMeters = getSelectedElevationDistanceMeters();
      totalTripWindowMeters = lastUpcomingDistanceMeters;
      totalTripGraphOffset = distanceTraveled;
      clampTotalTripGraphOffset(getTotalTripWindowMeters());
      Serial.println("Elevation stats: TOTAL TRIP");
    } else {
      currentElevStatView = ELEV_STATS_UPCOMING;
      Serial.println("Elevation stats: UPCOMING");
    }
    // Re-render page with new stat view
    renderHeightProfileView();
  } else if (currentMapSubPage == MAP_SUBPAGE_TRIPS) {
    handleTripsButton();  // New trips page button handler
  } else if (currentMapSubPage == MAP_SUBPAGE_TRIP_DETAIL) {
    // Check if Navigate Home error dialog is showing (highest priority)
    extern bool showNavigateHomeError;
    if (showNavigateHomeError) {
      // OK button pressed in error dialog - dismiss error
      Serial.println("Dismissing Navigate Home error dialog");

      showNavigateHomeError = false;
      navigateHomeHasError = false;
      navigateHomeErrorMessage[0] = '\0';

      // Re-render the page
      renderTripDetailView();
      return;
    }

    // Check if confirmation dialog is showing
    if (showDeleteConfirmation) {
      // OK button pressed in confirmation dialog - execute deletion
      Serial.printf("Deleting trip: %s\n", selectedTripDirName);

      bool success = deleteTripFromSD(selectedTripDirName);

      // Hide confirmation dialog
      showDeleteConfirmation = false;

      if (success) {
        // Return to trips list after successful deletion
        selectedTripButton = 0;  // Reset button selection
        freeLoadedTrack();  // Free GPX data from PSRAM
        currentMapSubPage = MAP_SUBPAGE_TRIPS;

        // Reset trips page state
        initTripsPage();

        renderTripsPage();
      } else {
        // Deletion failed, just close dialog and stay on detail page
        renderTripDetailView();
      }
    } else {
      // No confirmation dialog showing - handle button press
      if (selectedTripButton == 0) {
        // Check if we're in Navigate Home mode
        extern bool isNavigateHomeMode;
        extern bool navigateHomePathLoaded;

        if (isNavigateHomeMode && !navigateHomePathLoaded) {
          // Navigate Home mode - Load path button pressed
          if (deviceConnected) {
            Serial.println("Navigate Home: Requesting route from phone");

            // Set flag so BLE handler knows we're waiting for Navigate Home path
            extern bool waitingForNavigateHomePath;
            waitingForNavigateHomePath = true;

            // Request navigate home from phone
            extern void requestNavigateHome();
            requestNavigateHome();

            // Re-render the detail view to show loading state
            renderTripDetailView();
          } else {
            // Not connected - button shouldn't be clickable, but handle gracefully
            Serial.println("Navigate Home: Cannot load path - not connected");
          }
        } else {
          // Regular trip Start button OR Navigate Home with path loaded
          // For Navigate Home, use the loadedTrackName which is "_nav_home_temp"
          const char* tripNameToStart = selectedTripDirName[0] != '\0' ? selectedTripDirName : loadedTrackName;
          Serial.printf("Starting navigation for trip: %s\n", tripNameToStart);

          // Start trip navigation (transfers ownership of loadedTrack to navigationTrack)
          startTripNavigation(tripNameToStart);

          // Reset Navigate Home mode flags
          if (isNavigateHomeMode) {
            isNavigateHomeMode = false;
            navigateHomePathLoaded = false;
          }

          // Notify Android about active trip change
          extern void sendActiveTripUpdate();
          sendActiveTripUpdate();

          // Switch to navigation mode
          currentCenterY = CENTER_Y_NAV;
          currentInfoBarHeight = MAP_INFO_BAR_HEIGHT_NAV;
          MAP_DISPLAY_HEIGHT = DISPLAY_HEIGHT - MAP_INFO_BAR_HEIGHT_NAV;

          // NOTE: Do NOT free loadedTrack here - ownership transferred to navigationTrack
          // loadedTrack pointer is now null after transfer

          // Go to navigation map view
          currentMapSubPage = MAP_SUBPAGE_MAP;
          renderMapPage();
        }
      } else if (selectedTripButton == 1) {
        // Delete button pressed - show confirmation dialog
        Serial.printf("Showing delete confirmation for: %s\n", selectedTripDirName);
        showDeleteConfirmation = true;
        renderTripDetailView();
      }
    }
  }
}

bool handleMapBack() {
  // If delete confirmation is showing, cancel it
  if (showDeleteConfirmation) {
    showDeleteConfirmation = false;
    renderTripDetailView();
    return true;  // Back button handled
  }

  // If on trip detail page, go back to trips list
  if (currentMapSubPage == MAP_SUBPAGE_TRIP_DETAIL) {
    selectedTripButton = 0;  // Reset button selection
    freeLoadedTrack();  // Free GPX data from PSRAM

    // Reset Navigate Home mode flags if active
    extern bool isNavigateHomeMode;
    extern bool navigateHomePathLoaded;
    extern bool waitingForNavigateHomePath;
    if (isNavigateHomeMode) {
      isNavigateHomeMode = false;
      navigateHomePathLoaded = false;
      waitingForNavigateHomePath = false;
      Serial.println("Exiting Navigate Home detail view");
    }

    currentMapSubPage = MAP_SUBPAGE_TRIPS;
    initTripsPage();  // Reinitialize trips page state
    renderTripsPage();
    return true;  // Back button handled
  }
  // If on trips list, go back to map view
  else if (currentMapSubPage == MAP_SUBPAGE_TRIPS) {
    return handleTripsBack();  // New trips page back handler
  }
  // If on trip stats, go back to map view
  else if (currentMapSubPage == MAP_SUBPAGE_TRIP_STATS) {
    currentMapSubPage = MAP_SUBPAGE_MAP;
    renderMapPage();
    return true;  // Back button handled
  }
  // If on height profile, go back to map view
  else if (currentMapSubPage == MAP_SUBPAGE_HEIGHT_PROFILE) {
    currentElevStatView = ELEV_STATS_UPCOMING;  // Reset to default view
    currentMapSubPage = MAP_SUBPAGE_MAP;
    renderMapPage();
    return true;  // Back button handled
  }
  return false;  // Not handled, let default back behavior proceed
}

void handleMapNextPage() {
  Serial.println("Map: Next page button pressed");

  // If on trips page, handle pagination instead of cycling sub-pages
  if (currentMapSubPage == MAP_SUBPAGE_TRIPS) {
    handleTripsNextPage();  // Handle trips page pagination
    return;
  }

  // If there's a pending rotation redraw, force it now before page change
  if (rotationPending) {
    rotationPending = false;
    loadAndDisplayMap();
    lastMapUpdate = millis();
  }

  // Free loaded track if leaving trip detail page
  if (currentMapSubPage == MAP_SUBPAGE_TRIP_DETAIL) {
    freeLoadedTrack();
  }

  if (navigationActive) {
    // Cycle: Map -> Trip Stats -> [Height Profile if available] -> Map
    bool elevDataAvailable = hasElevationData();

    switch (currentMapSubPage) {
      case MAP_SUBPAGE_MAP:
        currentMapSubPage = MAP_SUBPAGE_TRIP_STATS;
        break;
      case MAP_SUBPAGE_TRIP_STATS:
        // Skip height profile if no elevation data available
        if (elevDataAvailable) {
          currentMapSubPage = MAP_SUBPAGE_HEIGHT_PROFILE;
        } else {
          currentMapSubPage = MAP_SUBPAGE_MAP;
        }
        break;
      case MAP_SUBPAGE_HEIGHT_PROFILE:
        currentMapSubPage = MAP_SUBPAGE_MAP;
        break;
      case MAP_SUBPAGE_TRIPS:
      case MAP_SUBPAGE_TRIP_DETAIL:  // Shouldn't reach these during nav, but handle it
        currentMapSubPage = MAP_SUBPAGE_MAP;
        break;
    }
  } else {
    // Cycle: Map -> Trips -> Map
    switch (currentMapSubPage) {
      case MAP_SUBPAGE_MAP:
        currentMapSubPage = MAP_SUBPAGE_TRIPS;
        initTripsPage();  // Initialize trips page when entering
        break;
      case MAP_SUBPAGE_TRIPS:
      case MAP_SUBPAGE_TRIP_STATS:       // Shouldn't happen, but handle it
      case MAP_SUBPAGE_HEIGHT_PROFILE:   // Shouldn't happen, but handle it
      case MAP_SUBPAGE_TRIP_DETAIL:      // From detail, cycle back to map
        currentMapSubPage = MAP_SUBPAGE_MAP;
        break;
    }
  }

  // Render the new sub-page (returns instantly for map view)
  renderMapPage();
}

/**
 * Reset map view to default state
 * Without navigation: Rotate to 0 degrees and set sensible zoom
 * With navigation: Reset scrub offset, set sensible zoom, re-enable auto-rotation
 */
void resetMapView() {
  Serial.println("Map: Reset view triggered (encoder long press)");

  // Clear any pending rotation or scrub redraws
  rotationPending = false;
  scrubPending = false;

  if (navigationActive) {
    // NAVIGATION MODE: Reset to current position with auto-rotation

    // Reset scrub offset to current GPS position
    scrubOffsetMeters = 0;
    Serial.println("Reset: Cleared scrub offset (back to current position)");

    // Re-enable auto-rotation if it was disabled by manual rotation
    if (!autoRotationEnabled) {
      autoRotationEnabled = true;
      Serial.println("Reset: Re-enabled auto-rotation");
    }

    // Always recalculate auto-rotation immediately on reset
    // This ensures the rotation is correct right away, not after a few seconds
    Serial.println("Reset: Recalculating auto-rotation for current position");
    calculateAutoRotation();

    // Set sensible zoom level (15 = zoom index 3)
    currentZoomIndex = 3;
    zoomLevel = ZOOM_LEVELS[currentZoomIndex];
    Serial.printf("Reset: Set zoom to level %d\n", zoomLevel);

  } else {
    // NORMAL MODE: Reset rotation and zoom

    // Reset rotation to 0 degrees (north up)
    mapRotation = 0;
    rotationPending = false;
    Serial.println("Reset: Set rotation to 0° (north up)");

    // Set sensible zoom level (15 = zoom index 3)
    currentZoomIndex = 3;
    zoomLevel = ZOOM_LEVELS[currentZoomIndex];
    Serial.printf("Reset: Set zoom to level %d\n", zoomLevel);
  }

  // Immediately redraw the map with new view settings
  loadAndDisplayMap();
  lastMapUpdate = millis();

  Serial.println("Reset: View reset complete");
}

/**
 * Check if encoder button is being held for reset view
 * Call this in loop() to detect long press for map view reset
 * Returns true if long press detected
 * NOTE: Should only be called when on map page and map subpage
 */
bool checkEncoderLongPress() {
  // External reference to ISR flags
  extern volatile bool buttonPressed;
  extern volatile bool waitingForButtonRelease;

  // SW_PIN is defined in BikeNav.ino as pin 6
  const int SW_PIN = 6;

  // Check if encoder button is currently pressed (LOW with INPUT_PULLUP)
  if (digitalRead(SW_PIN) == LOW) {
    // Button is pressed
    if (encoderButtonPressTime == 0) {
      // First detection of press - record time (after ISR has already triggered)
      // Note: The first button press will be handled normally (mode cycling)
      // Long press will only trigger if user continues holding beyond threshold
      encoderButtonPressTime = millis();
      encoderButtonLongPressChecked = false;
      Serial.println("Encoder button pressed - hold for 1.5s to reset view");
    } else if (!encoderButtonLongPressChecked) {
      // Button is being held - check duration
      unsigned long pressDuration = millis() - encoderButtonPressTime;

      if (pressDuration >= ENCODER_LONG_PRESS_MS) {
        // Long press detected!
        encoderButtonLongPressChecked = true;  // Prevent multiple triggers
        waitingForButtonRelease = true;  // Prevent further button presses until release
        Serial.println("Encoder long press detected - resetting view!");
        return true;
      }
    }
  } else {
    // Button released - reset tracking
    if (encoderButtonPressTime > 0) {
      unsigned long pressDuration = millis() - encoderButtonPressTime;
      if (pressDuration < ENCODER_LONG_PRESS_MS) {
        // Short press - was handled by normal button handler
        Serial.printf("Encoder button released after %lums (short press)\n", pressDuration);
      }
    }
    encoderButtonPressTime = 0;
    encoderButtonLongPressChecked = false;
  }

  return false;  // No long press detected
}

/**
 * Handle options button press
 * During navigation: Show confirmation to stop navigation
 * Outside navigation: Show trips list
 */
void handleMapOptions() {
  Serial.println("Map: Options button pressed");

  if (navigationActive) {
    // Show navigation stop confirmation
    Serial.println("Active navigation - showing stop confirmation");

    display.setPartialWindow(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    display.firstPage();

    do {
      display.fillScreen(GxEPD_WHITE);

      // Setup text rendering
      u8g2_display.setFontMode(1);
      u8g2_display.setForegroundColor(GxEPD_BLACK);
      u8g2_display.setBackgroundColor(GxEPD_WHITE);

      // Title
      u8g2_display.setFont(u8g2_font_helvB12_tf);
      u8g2_display.setCursor(15, 30);
      u8g2_display.print("NAVIGATION");

      // Trip name
      u8g2_display.setFont(u8g2_font_helvB10_tf);
      u8g2_display.setCursor(15, 50);
      u8g2_display.print("Active trip:");

      u8g2_display.setFont(u8g2_font_helvB08_tf);
      u8g2_display.setCursor(15, 68);
      // Display "Navigate Home" instead of "_nav_home_temp" for better readability
      if (strcmp(activeNavigationTrip, "_nav_home_temp") == 0) {
        u8g2_display.print("Navigate Home");
      } else {
        u8g2_display.print(activeNavigationTrip);
      }

      // Navigation stats
      char statStr[32];
      u8g2_display.setCursor(15, 95);
      snprintf(statStr, sizeof(statStr), "%.1f / %.1f km",
               distanceTraveled / 1000.0, totalDistance / 1000.0);
      u8g2_display.print(statStr);

      u8g2_display.setCursor(15, 110);
      int hours = navigationElapsedTime / 3600;
      int minutes = (navigationElapsedTime % 3600) / 60;
      snprintf(statStr, sizeof(statStr), "Time: %02d:%02d", hours, minutes);
      u8g2_display.print(statStr);

      // Draw controls with labels
      drawControlsBackEncoder(190, "Continue", "Stop");

      drawNotificationOverlay();

    } while (display.nextPage());

    // Wait for button press
    bool waiting = true;
    while (waiting) {
      if (buttonPressed) {
        buttonPressed = false;

        // Stop navigation
        Serial.println("Stopping navigation");
        stopTripNavigation();

        // Notify Android about active trip change
        extern void sendActiveTripUpdate();
        sendActiveTripUpdate();

        // Free elevation graph data
        freeElevationGraphData();

        // Reset map display parameters
        currentCenterY = CENTER_Y_NORMAL;
        currentInfoBarHeight = MAP_INFO_BAR_HEIGHT_NORMAL;
        MAP_DISPLAY_HEIGHT = DISPLAY_HEIGHT - MAP_INFO_BAR_HEIGHT_NORMAL;

        // Return to trips list
        currentMapSubPage = MAP_SUBPAGE_TRIPS;
        initTripsPage();  // Reinitialize trips page state
        renderTripsPage();
        waiting = false;
      }
      if (backPressed) {
        backPressed = false;
        // Continue navigation, return to map
        currentMapSubPage = MAP_SUBPAGE_MAP;
        renderMapPage();
        waiting = false;
      }
      delay(10);
    }
  } else {
    // Not navigating - show trips list
    currentMapSubPage = MAP_SUBPAGE_TRIPS;
    initTripsPage();  // Initialize trips page state
    renderTripsPage();
  }
}

void formatDistanceLabel(float meters, char* out, size_t outSize) {
  int metersRounded = (int)(meters + 0.5f);
  if (metersRounded >= 1000) {
    if (metersRounded % 1000 == 0) {
      snprintf(out, outSize, "%dkm", metersRounded / 1000);
    } else {
      snprintf(out, outSize, "%.1fkm", meters / 1000.0f);
    }
  } else {
    snprintf(out, outSize, "%dm", metersRounded);
  }
}

void drawTotalTripScrollIndicator(int y) {
  const int SELECTOR_HEIGHT = 22;
  const int TRACK_HEIGHT = 6;

  display.fillRect(0, y, DISPLAY_WIDTH, SELECTOR_HEIGHT, GxEPD_WHITE);

  u8g2_display.setFontMode(1);
  u8g2_display.setForegroundColor(GxEPD_BLACK);
  u8g2_display.setBackgroundColor(GxEPD_WHITE);

  int windowMeters = getTotalTripWindowMeters();
  if (totalDistance <= 0.0f || windowMeters <= 0) {
    u8g2_display.setFont(u8g2_font_helvR08_tf);
    u8g2_display.setCursor(5, y + 14);
    u8g2_display.print("No distance data");
    return;
  }

  clampTotalTripGraphOffset(windowMeters);

  float windowStart = totalTripGraphOffset;
  float windowEnd = windowStart + windowMeters;
  if (windowEnd > totalDistance) {
    windowEnd = totalDistance;
  }

  char startLabel[16];
  char endLabel[16];
  formatDistanceLabel(windowStart, startLabel, sizeof(startLabel));
  formatDistanceLabel(windowEnd, endLabel, sizeof(endLabel));

  u8g2_display.setFont(u8g2_font_helvR08_tf);
  int startWidth = u8g2_display.getUTF8Width(startLabel);
  int endWidth = u8g2_display.getUTF8Width(endLabel);

  const int labelY = y + 9;
  const int leftLabelX = 2;
  int rightLabelX = DISPLAY_WIDTH - endWidth - 2;

  u8g2_display.setCursor(leftLabelX, labelY);
  u8g2_display.print(startLabel);
  u8g2_display.setCursor(rightLabelX, labelY);
  u8g2_display.print(endLabel);

  int trackX = leftLabelX + startWidth + 6;
  int trackRight = rightLabelX - 6;
  int trackWidth = trackRight - trackX;
  if (trackWidth < 20) {
    trackX = 4;
    trackWidth = DISPLAY_WIDTH - 8;
  }

  int trackY = y + 12;
  display.drawRect(trackX, trackY, trackWidth, TRACK_HEIGHT, GxEPD_BLACK);

  float visibleRatio = windowMeters / totalDistance;
  if (visibleRatio > 1.0f) visibleRatio = 1.0f;
  int thumbWidth = max(10, (int)(trackWidth * visibleRatio));

  float scrollProgress = 0.0f;
  if (totalDistance > windowMeters) {
    scrollProgress = windowStart / (totalDistance - windowMeters);
    if (scrollProgress < 0.0f) scrollProgress = 0.0f;
    if (scrollProgress > 1.0f) scrollProgress = 1.0f;
  }

  int thumbX = trackX + (int)((trackWidth - thumbWidth) * scrollProgress);
  display.fillRect(thumbX, trackY + 1, thumbWidth, TRACK_HEIGHT - 2, GxEPD_BLACK);

  display.drawLine(trackX, trackY - 1, trackX, trackY + TRACK_HEIGHT, GxEPD_BLACK);
  display.drawLine(trackX + trackWidth - 1, trackY - 1,
                   trackX + trackWidth - 1, trackY + TRACK_HEIGHT, GxEPD_BLACK);
}

/**
 * Draw horizontal distance selector (Android clock-picker style)
 * Shows current selection in center with adjacent options visible
 */
void drawDistanceSelector(int y) {
  if (currentElevStatView == ELEV_STATS_TOTAL) {
    drawTotalTripScrollIndicator(y);
    return;
  }

  const int SELECTOR_HEIGHT = 22;
  const int ITEM_WIDTH = 42;  // Width per distance option
  const int CENTER_X = DISPLAY_WIDTH / 2;

  clampElevationDistanceIndex();
  int optionCount = getElevationDistanceOptionCount();

  // Draw background for selector area
  display.fillRect(0, y, DISPLAY_WIDTH, SELECTOR_HEIGHT, GxEPD_WHITE);

  u8g2_display.setFontMode(1);
  u8g2_display.setForegroundColor(GxEPD_BLACK);
  u8g2_display.setBackgroundColor(GxEPD_WHITE);

  lastUpcomingDistanceMeters = getElevationDistanceOption(selectedElevationDistanceIndex);

  // Show 3 items: [prev] [CURRENT] [next]
  for (int offset = -1; offset <= 1; offset++) {
    int index = selectedElevationDistanceIndex + offset;

    // Skip if out of bounds (no wrap-around)
    if (index < 0 || index >= optionCount) {
      continue;
    }

    int distance = getElevationDistanceOption(index);
    char distStr[16];
    formatDistanceLabel(distance, distStr, sizeof(distStr));

    // Calculate position
    int textX = CENTER_X + (offset * ITEM_WIDTH);
    int textY = y + 15;

    if (offset == 0) {
      // Current selection - bold and highlighted
      u8g2_display.setFont(u8g2_font_helvB10_tf);

      // Draw highlight box (moved up for better spacing)
      int textWidth = u8g2_display.getUTF8Width(distStr);
      int boxX = textX - textWidth / 2 - 4;
      int boxY = y + 1;  // Moved up from y + 3 to y + 1
      int boxWidth = textWidth + 8;
      int boxHeight = 16;
      display.drawRect(boxX, boxY, boxWidth, boxHeight, GxEPD_BLACK);

      // Center text
      u8g2_display.setCursor(textX - textWidth / 2, textY);
      u8g2_display.print(distStr);
    } else {
      // Adjacent options - smaller and lighter
      u8g2_display.setFont(u8g2_font_helvR08_tf);
      int textWidth = u8g2_display.getUTF8Width(distStr);
      u8g2_display.setCursor(textX - textWidth / 2, textY - 2);
      u8g2_display.print(distStr);
    }
  }
}

/**
 * Free elevation graph data from memory
 * Call this when leaving elevation page or when navigation stops
 */
void freeElevationGraphData() {
  if (elevGraphData.elevations != nullptr) {
    free(elevGraphData.elevations);
    elevGraphData.elevations = nullptr;
  }
  if (elevGraphData.distances != nullptr) {
    free(elevGraphData.distances);
    elevGraphData.distances = nullptr;
  }
  elevGraphData.pointCount = 0;
  elevGraphData.dataValid = false;
}

int findTrackIndexAtDistance(float targetDistance) {
  if (navigationTrack == nullptr || navigationTrackPointCount == 0) {
    return 0;
  }

  if (targetDistance <= 0.0f) {
    return 0;
  }

  float distanceAccumulated = 0.0f;
  for (int i = 0; i < navigationTrackPointCount - 1; i++) {
    float segmentDist = calculateDistance(
      navigationTrack[i].lat, navigationTrack[i].lon,
      navigationTrack[i + 1].lat, navigationTrack[i + 1].lon
    );
    distanceAccumulated += segmentDist;
    if (distanceAccumulated >= targetDistance) {
      return i + 1;
    }
  }

  return navigationTrackPointCount - 1;
}

bool getTripElevationRange(int16_t* outMinElev, int16_t* outMaxElev) {
  static TrackPoint* cachedTrack = nullptr;
  static int cachedCount = 0;
  static int16_t cachedMin = 0;
  static int16_t cachedMax = 0;
  static bool cacheValid = false;

  if (navigationTrack == nullptr || navigationTrackPointCount == 0) {
    cacheValid = false;
    return false;
  }

  if (!cacheValid || cachedTrack != navigationTrack || cachedCount != navigationTrackPointCount) {
    cachedMin = 32767;
    cachedMax = -32768;
    for (int i = 0; i < navigationTrackPointCount; i++) {
      int16_t elev = navigationTrack[i].elev;
      if (elev < cachedMin) cachedMin = elev;
      if (elev > cachedMax) cachedMax = elev;
    }

    cachedTrack = navigationTrack;
    cachedCount = navigationTrackPointCount;
    cacheValid = true;
  }

  if (outMinElev != nullptr) {
    *outMinElev = cachedMin;
  }
  if (outMaxElev != nullptr) {
    *outMaxElev = cachedMax;
  }

  return true;
}

/**
 * Process elevation data for graphing
 * Extracts elevation profile for upcoming or total trip window
 * Downsamples to ~100 points for efficient rendering
 */
void processElevationData() {
  // Free previous data if it exists
  if (elevGraphData.elevations != nullptr) {
    free(elevGraphData.elevations);
    elevGraphData.elevations = nullptr;
  }
  if (elevGraphData.distances != nullptr) {
    free(elevGraphData.distances);
    elevGraphData.distances = nullptr;
  }

  elevGraphData.dataValid = false;

  // Check if we have navigation data
  if (!navigationActive || navigationTrack == nullptr || navigationTrackPointCount == 0) {
    Serial.println("No navigation data available for elevation graph");
    return;
  }

  int startIndex = 0;
  int endIndex = 0;

  if (currentElevStatView == ELEV_STATS_TOTAL) {
    int windowMeters = getTotalTripWindowMeters();
    if (windowMeters <= 0 || totalDistance <= 0.0f) {
      Serial.println("No valid window for total trip elevation graph");
      return;
    }

    clampTotalTripGraphOffset(windowMeters);

    float windowStart = totalTripGraphOffset;
    float windowEnd = windowStart + windowMeters;
    if (windowEnd > totalDistance) {
      windowEnd = totalDistance;
    }

    startIndex = findTrackIndexAtDistance(windowStart);
    endIndex = findTrackIndexAtDistance(windowEnd);
  } else {
    // Get selected distance in meters
    int targetDistance = getSelectedElevationDistanceMeters();
    if (targetDistance <= 0) {
      Serial.println("Invalid target distance for elevation graph");
      return;
    }

    // Find current position in track
    startIndex = currentWaypointIndex;
    if (startIndex < 0 || startIndex >= navigationTrackPointCount) {
      startIndex = 0;  // Fallback to beginning
    }

    // Walk forward through track, accumulating distance
    float distanceAccumulated = 0.0;
    endIndex = startIndex;

    while (distanceAccumulated < targetDistance && endIndex < navigationTrackPointCount - 1) {
      float segmentDist = calculateDistance(
        navigationTrack[endIndex].lat, navigationTrack[endIndex].lon,
        navigationTrack[endIndex + 1].lat, navigationTrack[endIndex + 1].lon
      );
      distanceAccumulated += segmentDist;
      endIndex++;
    }
  }

  if (endIndex <= startIndex) {
    endIndex = min(startIndex + 1, navigationTrackPointCount - 1);
  }

  // Calculate number of source points
  int sourcePointCount = endIndex - startIndex + 1;
  if (sourcePointCount < 2) {
    Serial.println("Not enough points for elevation graph");
    return;
  }

  // Determine output point count (target ~100 points for smooth graph)
  const int TARGET_POINTS = 100;
  int outputPointCount = min(sourcePointCount, TARGET_POINTS);

  // Allocate arrays
  elevGraphData.elevations = (int16_t*)malloc(outputPointCount * sizeof(int16_t));
  elevGraphData.distances = (float*)malloc(outputPointCount * sizeof(float));

  if (elevGraphData.elevations == nullptr || elevGraphData.distances == nullptr) {
    Serial.println("Failed to allocate memory for elevation graph");
    if (elevGraphData.elevations != nullptr) free(elevGraphData.elevations);
    if (elevGraphData.distances != nullptr) free(elevGraphData.distances);
    elevGraphData.elevations = nullptr;
    elevGraphData.distances = nullptr;
    return;
  }

  // Sample/downsample points
  float sampleInterval = (float)(sourcePointCount - 1) / (float)(outputPointCount - 1);

  elevGraphData.minElev = 32767;  // Start with max int16
  elevGraphData.maxElev = -32768; // Start with min int16
  elevGraphData.elevGain = 0;
  elevGraphData.elevLoss = 0;

  float cumulativeDistance = 0.0;

  for (int i = 0; i < outputPointCount; i++) {
    // Calculate source index (with interpolation potential)
    float sourceIndexFloat = i * sampleInterval;
    int sourceIndex = startIndex + (int)sourceIndexFloat;

    // Clamp to valid range
    if (sourceIndex >= endIndex) sourceIndex = endIndex;

    // Get elevation (simple sampling, could add interpolation later)
    int16_t elev = navigationTrack[sourceIndex].elev;
    elevGraphData.elevations[i] = elev;

    // Calculate distance for this point
    if (i == 0) {
      elevGraphData.distances[i] = 0.0;
    } else {
      // Calculate cumulative distance
      int prevSourceIndex = startIndex + (int)((i - 1) * sampleInterval);
      if (prevSourceIndex >= endIndex) prevSourceIndex = endIndex;

      float segmentDist = calculateDistance(
        navigationTrack[prevSourceIndex].lat, navigationTrack[prevSourceIndex].lon,
        navigationTrack[sourceIndex].lat, navigationTrack[sourceIndex].lon
      );
      cumulativeDistance += segmentDist;
      elevGraphData.distances[i] = cumulativeDistance;
    }

    // Update min/max
    if (elev < elevGraphData.minElev) elevGraphData.minElev = elev;
    if (elev > elevGraphData.maxElev) elevGraphData.maxElev = elev;

    // Calculate gain/loss
    if (i > 0) {
      int16_t elevDiff = elev - elevGraphData.elevations[i - 1];
      if (elevDiff > 0) {
        elevGraphData.elevGain += elevDiff;
      } else if (elevDiff < 0) {
        elevGraphData.elevLoss += -elevDiff;
      }
    }
  }

  elevGraphData.pointCount = outputPointCount;
  elevGraphData.dataValid = true;

  Serial.printf("Elevation data processed: %d points, %dm to %dm, +%dm/-%dm\n",
                outputPointCount, elevGraphData.minElev, elevGraphData.maxElev,
                elevGraphData.elevGain, elevGraphData.elevLoss);
}

/**
 * Draw elevation graph with axes, grid, and profile line
 */
void drawElevationGraph(int graphX, int graphY, int graphWidth, int graphHeight) {
  if (!elevGraphData.dataValid || elevGraphData.pointCount < 2) {
    // Draw placeholder
    u8g2_display.setFont(u8g2_font_helvB10_tf);
    u8g2_display.setCursor(graphX + 10, graphY + graphHeight / 2);
    u8g2_display.print("No data");
    return;
  }

  // Auto-scale Y-axis with 5% padding
  int16_t baseMin = elevGraphData.minElev;
  int16_t baseMax = elevGraphData.maxElev;
  if (currentElevStatView == ELEV_STATS_TOTAL) {
    int16_t tripMin = 0;
    int16_t tripMax = 0;
    if (getTripElevationRange(&tripMin, &tripMax)) {
      baseMin = tripMin;
      baseMax = tripMax;
    }
  }

  int16_t elevRange = baseMax - baseMin;
  if (elevRange < 10) elevRange = 10;  // Minimum range for flat terrain

  int16_t elevPadding = elevRange * 0.05;
  if (elevPadding < 1) elevPadding = 5;  // Ensure at least 5m padding
  int16_t yMin = baseMin - elevPadding;
  int16_t yMax = baseMax + elevPadding;
  elevRange = yMax - yMin;

  // Safety check: ensure elevRange is never zero (prevents divide-by-zero crash)
  if (elevRange <= 0) {
    elevRange = 10;  // Use minimum range for flat/no elevation data
    yMin = baseMin - 5;
    yMax = baseMin + 5;
  }

  // Get max distance for X-axis
  float maxDist = elevGraphData.distances[elevGraphData.pointCount - 1];
  if (maxDist <= 0) maxDist = 1.0;  // Prevent divide-by-zero

  // --- Draw grid lines ---
  u8g2_display.setFont(u8g2_font_helvR08_tf);  // Larger readable font for labels

  // 5 horizontal lines (elevation)
  for (int i = 0; i < 5; i++) {
    int16_t elevValue = yMin + (elevRange * i / 4);
    int lineY = graphY + graphHeight - ((elevValue - yMin) * graphHeight / elevRange);

    // Clamp to graph bounds
    if (lineY < graphY) lineY = graphY;
    if (lineY > graphY + graphHeight) lineY = graphY + graphHeight;

    // Draw horizontal grid line (thin)
    display.drawLine(graphX, lineY, graphX + graphWidth, lineY, GxEPD_BLACK);

    // Draw elevation label on left (no "m" suffix)
    char labelStr[8];
    snprintf(labelStr, sizeof(labelStr), "%d", elevValue);
    u8g2_display.setCursor(graphX - 20, lineY + 3);
    u8g2_display.print(labelStr);
  }

  // 4 vertical lines (distance) - better spacing to avoid label crowding
  for (int i = 0; i < 4; i++) {
    float distValue = maxDist * i / 3;  // Divide by 3 for 4 points (0%, 33%, 66%, 100%)
    int lineX = graphX + (distValue * graphWidth / maxDist);

    // Draw vertical grid line
    display.drawLine(lineX, graphY, lineX, graphY + graphHeight, GxEPD_BLACK);

    // Draw distance label at bottom (no "m" suffix)
    char labelStr[8];
    if (distValue >= 1000) {
      snprintf(labelStr, sizeof(labelStr), "%.1f", distValue / 1000.0);
    } else {
      snprintf(labelStr, sizeof(labelStr), "%d", (int)distValue);
    }
    int labelWidth = u8g2_display.getUTF8Width(labelStr);

    // Special handling for first label - offset right to avoid "you are here" dot
    if (i == 0) {
      u8g2_display.setCursor(lineX + 3, graphY + graphHeight + 10);
    }
    // Special handling for last label - align right to prevent cutoff
    else if (i == 3) {
      u8g2_display.setCursor(lineX - labelWidth, graphY + graphHeight + 10);
    }
    // Center other labels
    else {
      u8g2_display.setCursor(lineX - labelWidth / 2, graphY + graphHeight + 10);
    }
    u8g2_display.print(labelStr);
  }

  // --- Draw elevation profile with dithered fill below ---
  // First pass: dithered pattern below the elevation line
  for (int i = 0; i < elevGraphData.pointCount - 1; i++) {
    float dist1 = elevGraphData.distances[i];
    float dist2 = elevGraphData.distances[i + 1];
    int16_t elev1 = elevGraphData.elevations[i];
    int16_t elev2 = elevGraphData.elevations[i + 1];

    // Map to screen coordinates
    int x1 = graphX + (dist1 * graphWidth / maxDist);
    int x2 = graphX + (dist2 * graphWidth / maxDist);
    int y1 = graphY + graphHeight - ((elev1 - yMin) * graphHeight / elevRange);
    int y2 = graphY + graphHeight - ((elev2 - yMin) * graphHeight / elevRange);

    // Clamp to graph bounds
    if (x1 < graphX) x1 = graphX;
    if (x2 > graphX + graphWidth) x2 = graphX + graphWidth;
    if (y1 < graphY) y1 = graphY;
    if (y1 > graphY + graphHeight) y1 = graphY + graphHeight;
    if (y2 < graphY) y2 = graphY;
    if (y2 > graphY + graphHeight) y2 = graphY + graphHeight;

    // Draw gradient dithered pattern below the line (80% density at top, 20% at bottom)
    int bottomY = graphY + graphHeight;
    if (x2 > x1) {
      for (int x = x1; x <= x2; x++) {
        float t = (float)(x - x1) / (float)(x2 - x1);
        int lineY = y1 + (y2 - y1) * t;

        // Draw gradient dithered pixels below the elevation line (start at lineY+1 to connect better)
        for (int y = lineY + 1; y < bottomY; y++) {
          // Calculate gradient factor: 0.0 at top (near line), 1.0 at bottom
          float gradientFactor = (float)(y - lineY) / (float)(bottomY - lineY);

          // Interpolate density from 80% at top to 20% at bottom
          float density = 0.80 - (gradientFactor * 0.60);  // 0.80 to 0.20

          // Use a Bayer-like dithering pattern for smooth gradient
          // Calculate threshold based on pixel position in 4x4 Bayer matrix
          int bayerMatrix[4][4] = {
            { 0,  8,  2, 10},
            {12,  4, 14,  6},
            { 3, 11,  1,  9},
            {15,  7, 13,  5}
          };
          int threshold = bayerMatrix[y % 4][x % 4];

          // Convert threshold (0-15) to 0.0-1.0 range
          float normalizedThreshold = threshold / 15.0;

          // Draw pixel if density is higher than threshold
          if (density > normalizedThreshold) {
            display.drawPixel(x, y, GxEPD_BLACK);
          }
        }
      }
    }
  }

  // Second pass: draw thick elevation line on top (uniform thickness)
  for (int i = 0; i < elevGraphData.pointCount - 1; i++) {
    float dist1 = elevGraphData.distances[i];
    float dist2 = elevGraphData.distances[i + 1];
    int16_t elev1 = elevGraphData.elevations[i];
    int16_t elev2 = elevGraphData.elevations[i + 1];

    // Map to screen coordinates
    int x1 = graphX + (dist1 * graphWidth / maxDist);
    int x2 = graphX + (dist2 * graphWidth / maxDist);
    int y1 = graphY + graphHeight - ((elev1 - yMin) * graphHeight / elevRange);
    int y2 = graphY + graphHeight - ((elev2 - yMin) * graphHeight / elevRange);

    // Clamp to graph bounds
    if (x1 < graphX) x1 = graphX;
    if (x2 > graphX + graphWidth) x2 = graphX + graphWidth;
    if (y1 < graphY) y1 = graphY;
    if (y1 > graphY + graphHeight) y1 = graphY + graphHeight;
    if (y2 < graphY) y2 = graphY;
    if (y2 > graphY + graphHeight) y2 = graphY + graphHeight;

    // Draw thick line with uniform width regardless of angle
    // Main line
    display.drawLine(x1, y1, x2, y2, GxEPD_BLACK);
    // Offsets in all directions for uniform thickness
    display.drawLine(x1 - 1, y1, x2 - 1, y2, GxEPD_BLACK);
    display.drawLine(x1 + 1, y1, x2 + 1, y2, GxEPD_BLACK);
    display.drawLine(x1, y1 - 1, x2, y2 - 1, GxEPD_BLACK);
    display.drawLine(x1, y1 + 1, x2, y2 + 1, GxEPD_BLACK);
  }

  // --- Draw "you are here" vertical line at start ---
  display.drawLine(graphX, graphY, graphX, graphY + graphHeight, GxEPD_BLACK);
  display.drawLine(graphX + 1, graphY, graphX + 1, graphY + graphHeight, GxEPD_BLACK);

  // Draw marker circle at current position (smaller to avoid label obstruction)
  int currentY = graphY + graphHeight - ((elevGraphData.elevations[0] - yMin) * graphHeight / elevRange);
  display.fillCircle(graphX, currentY, 2, GxEPD_BLACK);
}

/**
 * Render height profile view
 * Shows elevation profile for upcoming route section with interactive distance selector
 */
void renderHeightProfileView() {
  Serial.println("Rendering height profile view");

  // Process elevation data for current distance selection
  processElevationData();

  display.setPartialWindow(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
  display.firstPage();

  do {
    display.fillScreen(GxEPD_WHITE);

    // Setup text rendering
    u8g2_display.setFontMode(1);
    u8g2_display.setForegroundColor(GxEPD_BLACK);
    u8g2_display.setBackgroundColor(GxEPD_WHITE);

    // --- Distance selector / scroll indicator at top ---
    drawDistanceSelector(5);

    // --- Graph area ---
    // Layout: 22px for Y-axis labels on left, graph fills rest to right edge
    const int GRAPH_Y = 38;        // Below distance selector (moved down from 32)
    const int GRAPH_HEIGHT = 150;  // Tall graph for detail
    const int Y_AXIS_WIDTH = 22;   // Space for elevation labels
    const int GRAPH_X = Y_AXIS_WIDTH;
    const int GRAPH_WIDTH = DISPLAY_WIDTH - Y_AXIS_WIDTH - 5;  // 5px right margin to prevent cutoff

    // Draw graph
    drawElevationGraph(GRAPH_X, GRAPH_Y, GRAPH_WIDTH, GRAPH_HEIGHT);

    // Check if elevation data is all zeros (no elevation info)
    bool hasElevationData = (elevGraphData.maxElev > 0 || elevGraphData.minElev < 0 ||
                             elevGraphData.elevGain > 0 || elevGraphData.elevLoss > 0);

    // Show warning if no elevation data
    if (!hasElevationData) {
      u8g2_display.setFont(u8g2_font_helvR08_tf);
      u8g2_display.setCursor(GRAPH_X + 10, GRAPH_Y + GRAPH_HEIGHT / 2 - 10);
      u8g2_display.print("No elevation data");
      u8g2_display.setCursor(GRAPH_X + 10, GRAPH_Y + GRAPH_HEIGHT / 2 + 5);
      u8g2_display.print("available for route");
    }

    // --- Stats display panel ---
    const int STATS_Y = GRAPH_Y + GRAPH_HEIGHT + 22;  // More space to avoid x-axis labels
    char statStr[32];

    // Display different stats based on current view mode
    if (currentElevStatView == ELEV_STATS_UPCOMING) {
      // === UPCOMING VIEW: Show elevation stats for selected distance ahead ===

      // View title (smaller, subtle)
      u8g2_display.setFont(u8g2_font_helvR08_tf);
      u8g2_display.setCursor(5, STATS_Y);
      u8g2_display.print("Upcoming");

      if (elevGraphData.dataValid) {
        // Large prominent stat: Elevation change (most important for navigation)
        u8g2_display.setFont(u8g2_font_helvB10_tf);
        u8g2_display.setCursor(5, STATS_Y + 18);
        snprintf(statStr, sizeof(statStr), "+%dm / -%dm", elevGraphData.elevGain, elevGraphData.elevLoss);
        u8g2_display.print(statStr);

        // Secondary stats: Peak and Low (smaller, less prominent)
        u8g2_display.setFont(u8g2_font_helvR08_tf);

        // Peak
        u8g2_display.setCursor(5, STATS_Y + 38);
        u8g2_display.print("Peak:");
        u8g2_display.setFont(u8g2_font_helvB08_tf);
        u8g2_display.setCursor(35, STATS_Y + 38);
        snprintf(statStr, sizeof(statStr), "%dm", elevGraphData.maxElev);
        u8g2_display.print(statStr);

        // Low
        u8g2_display.setFont(u8g2_font_helvR08_tf);
        u8g2_display.setCursor(5, STATS_Y + 52);
        u8g2_display.print("Low:");
        u8g2_display.setFont(u8g2_font_helvB08_tf);
        u8g2_display.setCursor(35, STATS_Y + 52);
        snprintf(statStr, sizeof(statStr), "%dm", elevGraphData.minElev);
        u8g2_display.print(statStr);
      } else {
        // No data available
        u8g2_display.setFont(u8g2_font_helvR08_tf);
        u8g2_display.setCursor(5, STATS_Y + 24);
        u8g2_display.print("No elevation data");
        u8g2_display.setCursor(5, STATS_Y + 38);
        u8g2_display.print("available");
      }

    } else if (currentElevStatView == ELEV_STATS_TOTAL) {
      // === TOTAL TRIP VIEW: Show overall trip elevation stats ===

      // View title (smaller, subtle)
      u8g2_display.setFont(u8g2_font_helvR08_tf);
      u8g2_display.setCursor(5, STATS_Y);
      u8g2_display.print("Total Trip");

      // Current elevation (large, prominent)
      u8g2_display.setFont(u8g2_font_helvR08_tf);
      u8g2_display.setCursor(5, STATS_Y + 18);
      u8g2_display.print("Now:");
      u8g2_display.setFont(u8g2_font_helvB12_tf);
      u8g2_display.setCursor(35, STATS_Y + 20);
      snprintf(statStr, sizeof(statStr), "%.0fm", currentElevation);
      u8g2_display.print(statStr);

      // Total planned elevation gain/loss for entire trip (from metadata)
      u8g2_display.setFont(u8g2_font_helvR08_tf);
      u8g2_display.setCursor(5, STATS_Y + 38);
      u8g2_display.print("Gain:");
      u8g2_display.setFont(u8g2_font_helvB08_tf);
      u8g2_display.setCursor(35, STATS_Y + 38);
      snprintf(statStr, sizeof(statStr), "+%.0fm", plannedElevationGain);
      u8g2_display.print(statStr);

      u8g2_display.setFont(u8g2_font_helvR08_tf);
      u8g2_display.setCursor(5, STATS_Y + 52);
      u8g2_display.print("Loss:");
      u8g2_display.setFont(u8g2_font_helvB08_tf);
      u8g2_display.setCursor(35, STATS_Y + 52);
      snprintf(statStr, sizeof(statStr), "-%.0fm", plannedElevationLoss);
      u8g2_display.print(statStr);
    }

    // View indicator (show which view is active)
    const int INDICATOR_X = DISPLAY_WIDTH - 20;
    const int INDICATOR_Y = STATS_Y + 25;

    // Draw two small dots to indicate view mode (like page dots)
    if (currentElevStatView == ELEV_STATS_UPCOMING) {
      display.fillCircle(INDICATOR_X, INDICATOR_Y, 2, GxEPD_BLACK);      // Filled = active
      display.drawCircle(INDICATOR_X, INDICATOR_Y + 8, 2, GxEPD_BLACK);  // Empty = inactive
    } else {
      display.drawCircle(INDICATOR_X, INDICATOR_Y, 2, GxEPD_BLACK);      // Empty = inactive
      display.fillCircle(INDICATOR_X, INDICATOR_Y + 8, 2, GxEPD_BLACK);  // Filled = active
    }

    // --- Draw page dots at bottom ---
    drawStatusBarNoSeparator();
    drawPageDots();

    // --- Draw notification overlay ---
    drawNotificationOverlay();

  } while (display.nextPage());
}

#endif // PAGE_MAP_H
