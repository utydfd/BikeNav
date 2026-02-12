#ifndef MAP_NAVIGATION_H
#define MAP_NAVIGATION_H

#include <GxEPD2_BW.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <TinyGPS++.h>
#include <math.h>

// External references from main program
extern GxEPD2_BW<GxEPD2_290_BS, GxEPD2_290_BS::HEIGHT> display;
extern U8G2_FOR_ADAFRUIT_GFX u8g2_display;
extern const int DISPLAY_WIDTH;
extern const int DISPLAY_HEIGHT;
extern TinyGPSPlus gps;  // GPS object for speed and location data

// External UI functions from page_map.h
extern void drawPageDots();
extern void drawStatusBar();
extern void drawStatusBarNoSeparator();

// --- DATA STRUCTURES ---
// Simple coordinate structure for calculations (matches Kotlin data class)
struct Coordinate {
  double lat;
  double lon;

  Coordinate(double latitude, double longitude) : lat(latitude), lon(longitude) {}
};

// Route progress tracking (matches Kotlin data class)
struct RouteProgress {
  int index;      // Current segment index
  double fraction; // Position within segment (0.0 to 1.0)

  RouteProgress(int idx, double frac) : index(idx), fraction(frac) {}
};

// --- NAVIGATION STATE VARIABLES ---
// navigationActive is defined in BikeNav.ino, declared here as extern
extern bool navigationActive;

char activeNavigationTrip[64] = "";         // Directory name of active trip
int currentWaypointIndex = 0;               // Current position in route
float distanceToNextTurn = 0.0;             // Meters to next waypoint
int nextTurnType = 2;                       // Turn icon index (0-7, default: straight)
float totalDistanceRemaining = 0.0;         // Total distance remaining in meters
float totalDistance = 0.0;                  // Total trip distance in meters
float distanceTraveled = 0.0;               // Distance traveled so far in meters
float averageSpeed = 0.0;                   // Average speed in km/h
float currentSpeed = 0.0;                   // Current speed in km/h
float maxSpeed = 0.0;                       // Maximum speed reached in km/h
unsigned long navigationStartTime = 0;      // Millis when navigation started
unsigned long navigationElapsedTime = 0;    // Seconds elapsed during navigation
float currentElevation = 0.0;               // Current elevation in meters
float elevationGain = 0.0;                  // Total elevation gain in meters (accumulated as you travel)
float elevationLoss = 0.0;                  // Total elevation loss in meters (accumulated as you travel)

// Planned trip elevation from metadata (loaded at navigation start)
float plannedElevationGain = 0.0;           // Total planned elevation gain from trip metadata
float plannedElevationLoss = 0.0;           // Total planned elevation loss from trip metadata

// GPS position update flag - set when position changes, cleared after screen update
bool gpsPositionChanged = false;            // Flag to trigger immediate screen update

// --- NAVIGATION TRACK DATA (separate from trip preview track) ---
// TrackPoint structure is defined in map_trips.h (included before this file)

// External TrackPoint data and functions from map_trips.h
extern TrackPoint* loadedTrack;              // Trip preview track (in map_trips.h)
extern int loadedTrackPointCount;
extern bool parseAndLoadGPX(const char* tripDirName);  // GPX parser from map_trips.h

TrackPoint* navigationTrack = nullptr;       // PSRAM-allocated track for active navigation
int navigationTrackPointCount = 0;           // Number of points in navigation track

// --- AUTO-ROTATION CONFIGURATION ---
// Look-ahead configuration for showing maximum path ahead
const float PRIMARY_LOOK_AHEAD_METERS = 150.0;   // Main look-ahead distance to show path ahead
const float MIN_LOOK_AHEAD_METERS = 20.0;        // Minimum look-ahead when near end

// Rotation is intentionally 180° from travel direction to show path ahead
const bool ENABLE_DEBUG_OUTPUT = true;           // Set false to reduce Serial spam

bool autoRotationEnabled = true;  // Can be temporarily disabled by manual rotation
unsigned long lastManualRotationTime = 0;
const unsigned long MANUAL_ROTATION_TIMEOUT = 15000;  // 15 seconds to return to auto
const float ROTATION_CHANGE_THRESHOLD = 5.0;  // degrees - minimum change to update
const float AUTO_ROTATION_MIN_MOVEMENT = 0.3;  // meters - minimum GPS movement to trigger recalculation

// --- FUNCTION PROTOTYPES ---
void startTripNavigation(const char* tripDirName);
void stopTripNavigation();
void updateNavigationState();
void checkGPSPositionChange();  // Check if GPS moved (works in both nav and map mode)
void renderNavigationStatsView();
void renderTripStatsView();  // Legacy placeholder view

// --- HELPER FUNCTIONS ---

/**
 * Calculate distance between two GPS points using Haversine formula
 * Returns distance in meters
 */
float calculateDistance(double lat1, double lon1, double lat2, double lon2) {
  const double R = 6371000.0;  // Earth radius in meters

  double lat1Rad = lat1 * M_PI / 180.0;
  double lat2Rad = lat2 * M_PI / 180.0;
  double dLat = (lat2 - lat1) * M_PI / 180.0;
  double dLon = (lon2 - lon1) * M_PI / 180.0;

  double a = sin(dLat / 2.0) * sin(dLat / 2.0) +
             cos(lat1Rad) * cos(lat2Rad) *
             sin(dLon / 2.0) * sin(dLon / 2.0);

  double c = 2.0 * atan2(sqrt(a), sqrt(1.0 - a));

  return R * c;  // Distance in meters
}

/**
 * Calculate bearing from point 1 to point 2
 * Returns bearing in degrees (0-360, where 0=North, 90=East, 180=South, 270=West)
 */
float calculateBearing(double lat1, double lon1, double lat2, double lon2) {
  double lat1Rad = lat1 * M_PI / 180.0;
  double lat2Rad = lat2 * M_PI / 180.0;
  double dLon = (lon2 - lon1) * M_PI / 180.0;

  double y = sin(dLon) * cos(lat2Rad);
  double x = cos(lat1Rad) * sin(lat2Rad) -
             sin(lat1Rad) * cos(lat2Rad) * cos(dLon);

  double bearingRad = atan2(y, x);
  double bearingDeg = bearingRad * 180.0 / M_PI;

  // Normalize to 0-360
  bearingDeg = fmod(bearingDeg + 360.0, 360.0);

  return bearingDeg;
}

/**
 * Find the closest point on the navigation track to current GPS position
 * Returns the index of the closest point
 * Returns -1 if no track loaded or error
 */
int findClosestTrackPoint(double currentLat, double currentLon) {
  if (navigationTrack == nullptr || navigationTrackPointCount == 0) {
    return -1;
  }

  int closestIndex = 0;
  float minDistance = calculateDistance(currentLat, currentLon,
                                       navigationTrack[0].lat,
                                       navigationTrack[0].lon);

  // Search for closest point
  for (int i = 1; i < navigationTrackPointCount; i++) {
    float distance = calculateDistance(currentLat, currentLon,
                                      navigationTrack[i].lat,
                                      navigationTrack[i].lon);

    if (distance < minDistance) {
      minDistance = distance;
      closestIndex = i;
    }
  }

  // Debug output removed to reduce spam - only recalculates when GPS changes now
  return closestIndex;
}

/**
 * Calculate the lat/lon position at a given offset (in meters) from current position
 * offsetMeters can be positive (forward) or negative (backward) along the route
 * Used for scrub mode to preview route ahead or behind
 */
void calculateScrubPosition(int offsetMeters, double* outLat, double* outLon) {
  // Validate inputs
  if (navigationTrack == nullptr || navigationTrackPointCount == 0 || outLat == nullptr || outLon == nullptr) {
    return;
  }

  // Start from current waypoint
  int startIndex = currentWaypointIndex;
  if (startIndex < 0) startIndex = 0;
  if (startIndex >= navigationTrackPointCount) startIndex = navigationTrackPointCount - 1;

  // Handle forward scrubbing (positive offset)
  if (offsetMeters >= 0) {
    float remainingDistance = offsetMeters;

    for (int i = startIndex; i < navigationTrackPointCount - 1; i++) {
      double lat1 = navigationTrack[i].lat;
      double lon1 = navigationTrack[i].lon;
      double lat2 = navigationTrack[i + 1].lat;
      double lon2 = navigationTrack[i + 1].lon;

      float segmentLength = calculateDistance(lat1, lon1, lat2, lon2);

      if (remainingDistance <= segmentLength && segmentLength > 0) {
        // Target is within this segment - interpolate
        double fraction = remainingDistance / segmentLength;
        *outLat = lat1 + (lat2 - lat1) * fraction;
        *outLon = lon1 + (lon2 - lon1) * fraction;
        return;
      }

      remainingDistance -= segmentLength;
    }

    // Reached end of route - return last point
    *outLat = navigationTrack[navigationTrackPointCount - 1].lat;
    *outLon = navigationTrack[navigationTrackPointCount - 1].lon;
  }
  // Handle backward scrubbing (negative offset)
  else {
    float remainingDistance = -offsetMeters;  // Make positive for calculation

    for (int i = startIndex; i > 0; i--) {
      double lat1 = navigationTrack[i].lat;
      double lon1 = navigationTrack[i].lon;
      double lat2 = navigationTrack[i - 1].lat;
      double lon2 = navigationTrack[i - 1].lon;

      float segmentLength = calculateDistance(lat1, lon1, lat2, lon2);

      if (remainingDistance <= segmentLength && segmentLength > 0) {
        // Target is within this segment - interpolate
        double fraction = remainingDistance / segmentLength;
        *outLat = lat1 + (lat2 - lat1) * fraction;
        *outLon = lon1 + (lon2 - lon1) * fraction;
        return;
      }

      remainingDistance -= segmentLength;
    }

    // Reached start of route - return first point
    *outLat = navigationTrack[0].lat;
    *outLon = navigationTrack[0].lon;
  }
}

/**
 * Find a point on the route at a specified distance ahead
 * Returns dynamically allocated Coordinate (caller must delete)
 * Returns nullptr if we've reached the end of the route
 * This matches the Kotlin NavigationService implementation
 */
Coordinate* findPointOnRoute(RouteProgress startProgress, float distanceMeters, TrackPoint* route) {
  if (route == nullptr) return nullptr;

  if (startProgress.index >= navigationTrackPointCount - 1) {
    // At end of route - return last point
    if (navigationTrackPointCount > 0) {
      return new Coordinate(route[navigationTrackPointCount - 1].lat,
                           route[navigationTrackPointCount - 1].lon);
    }
    return nullptr;
  }

  // Calculate remaining distance on current segment
  double lat1 = route[startProgress.index].lat;
  double lon1 = route[startProgress.index].lon;
  double lat2 = route[startProgress.index + 1].lat;
  double lon2 = route[startProgress.index + 1].lon;

  float segmentLength = calculateDistance(lat1, lon1, lat2, lon2);
  float remainingDistanceOnSegment = segmentLength * (1.0 - startProgress.fraction);

  // Check if target distance is on current segment
  if (distanceMeters <= remainingDistanceOnSegment) {
    if (segmentLength > 0) {
      double newFraction = startProgress.fraction + (distanceMeters / segmentLength);
      double newLat = lat1 + (lat2 - lat1) * newFraction;
      double newLon = lon1 + (lon2 - lon1) * newFraction;
      return new Coordinate(newLat, newLon);
    }
    return new Coordinate(lat1, lon1);
  }

  // Need to walk forward along route
  float distanceToCover = distanceMeters - remainingDistanceOnSegment;

  for (int i = startProgress.index + 1; i < navigationTrackPointCount - 1; i++) {
    double currentLat = route[i].lat;
    double currentLon = route[i].lon;
    double nextLat = route[i + 1].lat;
    double nextLon = route[i + 1].lon;

    float currentSegmentLength = calculateDistance(currentLat, currentLon, nextLat, nextLon);

    if (distanceToCover <= currentSegmentLength && currentSegmentLength > 0) {
      double fraction = distanceToCover / currentSegmentLength;
      double newLat = currentLat + (nextLat - currentLat) * fraction;
      double newLon = currentLon + (nextLon - currentLon) * fraction;
      return new Coordinate(newLat, newLon);
    }

    distanceToCover -= currentSegmentLength;
  }

  // Reached end of route - return last point
  if (navigationTrackPointCount > 0) {
    return new Coordinate(route[navigationTrackPointCount - 1].lat,
                         route[navigationTrackPointCount - 1].lon);
  }

  return nullptr;
}

// --- IMPLEMENTATIONS ---

/**
 * Render trip statistics view
 *
 * Current: Placeholder implementation showing basic trip stats
 * Future: Real-time statistics during active navigation
 *   - Distance traveled vs total distance
 *   - Current speed and average speed
 *   - Elevation profile
 *   - Time elapsed and estimated time remaining
 *   - Next waypoint/turn information
 */
void renderTripStatsView() {
  Serial.println("Rendering trip stats view");

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
    u8g2_display.setCursor(10, 25);
    u8g2_display.print("TRIP STATS");

    // Trip statistics (placeholder)
    u8g2_display.setFont(u8g2_font_helvB10_tf);
    u8g2_display.setCursor(10, 55);
    u8g2_display.print("Distance:");
    u8g2_display.setCursor(10, 75);
    u8g2_display.print("0.0 km");

    u8g2_display.setCursor(10, 105);
    u8g2_display.print("Time:");
    u8g2_display.setCursor(10, 125);
    u8g2_display.print("00:00:00");

    u8g2_display.setCursor(10, 155);
    u8g2_display.print("Avg Speed:");
    u8g2_display.setCursor(10, 175);
    u8g2_display.print("0.0 km/h");

    // Placeholder for future features
    u8g2_display.setFont(u8g2_font_helvB08_tf);
    u8g2_display.setCursor(10, 230);
    u8g2_display.print("Height profile");
    u8g2_display.setCursor(10, 245);
    u8g2_display.print("coming soon");

    drawStatusBar();
    drawPageDots();

    // Draw notification overlay
    drawNotificationOverlay();

  } while (display.nextPage());
}

/**
 * Start trip navigation
 * Activates navigation mode and initializes state
 * Transfers GPX data from loadedTrack to navigationTrack
 */
void startTripNavigation(const char* tripDirName) {
  Serial.printf("Starting navigation for trip: %s\n", tripDirName);

  // Copy trip directory name
  strncpy(activeNavigationTrip, tripDirName, sizeof(activeNavigationTrip) - 1);
  activeNavigationTrip[sizeof(activeNavigationTrip) - 1] = '\0';

  // Load trip metadata for planned elevation stats
  // External function from map_trips.h
  extern bool readTripMetadata(const char* tripDirName, StaticJsonDocument<512>& doc);

  StaticJsonDocument<512> metaDoc;
  if (readTripMetadata(tripDirName, metaDoc)) {
    plannedElevationGain = metaDoc["totalElevationGain"] | 0.0;
    plannedElevationLoss = metaDoc["totalElevationLoss"] | 0.0;
    Serial.printf("Loaded trip metadata: elevation gain=%.0fm, loss=%.0fm\n",
                  plannedElevationGain, plannedElevationLoss);
  } else {
    // Fallback if metadata not available
    plannedElevationGain = 0.0;
    plannedElevationLoss = 0.0;
    Serial.println("WARNING: Could not load trip metadata");
  }

  // Transfer GPX data from loadedTrack to navigationTrack
  // The track was already loaded when viewing trip details
  if (loadedTrack != nullptr && loadedTrackPointCount > 0) {
    Serial.printf("Transferring GPX data: %d points\n", loadedTrackPointCount);
    navigationTrack = loadedTrack;
    navigationTrackPointCount = loadedTrackPointCount;

    // Clear loadedTrack pointers (ownership transferred)
    loadedTrack = nullptr;
    loadedTrackPointCount = 0;

    // Calculate actual total distance from track using Haversine formula
    totalDistance = 0.0;
    for (int i = 0; i < navigationTrackPointCount - 1; i++) {
      double lat1 = navigationTrack[i].lat;
      double lon1 = navigationTrack[i].lon;
      double lat2 = navigationTrack[i + 1].lat;
      double lon2 = navigationTrack[i + 1].lon;

      totalDistance += calculateDistance(lat1, lon1, lat2, lon2);
    }

    Serial.printf("Calculated total distance: %.2f meters\n", totalDistance);
    totalDistanceRemaining = totalDistance;
  } else {
    Serial.println("WARNING: No GPX data loaded for navigation!");
    // Fallback values
    totalDistance = 10000.0;
    totalDistanceRemaining = 10000.0;
  }

  // Initialize navigation state
  navigationActive = true;
  currentWaypointIndex = 0;
  distanceToNextTurn = 500.0;  // Will be calculated from track
  nextTurnType = 2;  // Straight
  distanceTraveled = 0.0;
  averageSpeed = 0.0;
  currentSpeed = 0.0;
  maxSpeed = 0.0;
  navigationStartTime = millis();
  navigationElapsedTime = 0;
  currentElevation = 0.0;
  elevationGain = 0.0;
  elevationLoss = 0.0;

  // Reset auto-rotation state
  autoRotationEnabled = true;

  Serial.println("Navigation started successfully");
}

/**
 * Stop trip navigation
 * Deactivates navigation mode and resets state
 * Frees navigation track from PSRAM
 */
void stopTripNavigation() {
  Serial.println("Stopping navigation");

  // Free navigation track from PSRAM
  if (navigationTrack != nullptr) {
    free(navigationTrack);  // ps_malloc'd memory is freed with regular free()
    navigationTrack = nullptr;
    navigationTrackPointCount = 0;
    Serial.println("Freed navigation track from PSRAM");
  }

  // Reset navigation state
  navigationActive = false;
  activeNavigationTrip[0] = '\0';
  currentWaypointIndex = 0;
  distanceToNextTurn = 0.0;
  nextTurnType = 2;
  totalDistanceRemaining = 0.0;
  totalDistance = 0.0;
  distanceTraveled = 0.0;
  averageSpeed = 0.0;
  currentSpeed = 0.0;
  maxSpeed = 0.0;
  navigationStartTime = 0;
  navigationElapsedTime = 0;
  currentElevation = 0.0;
  elevationGain = 0.0;
  elevationLoss = 0.0;
  plannedElevationGain = 0.0;
  plannedElevationLoss = 0.0;

  // Reset auto-rotation state
  autoRotationEnabled = true;
  lastManualRotationTime = 0;

  Serial.println("Navigation stopped");
}

/**
 * Check if GPS position changed and set flag for screen update
 * Works in both navigation mode and plain map mode
 * Call this from main loop whenever GPS updates
 */
void checkGPSPositionChange() {
  // External references
  extern double currentLat;
  extern double currentLon;
  extern bool gpsValid;

  if (!gpsValid) return;

  // Track last GPS position to detect changes
  static double lastCheckLat = 0.0;
  static double lastCheckLon = 0.0;
  static bool firstCheck = true;

  // Check if GPS coordinates actually changed (not just GPS update event)
  if (!firstCheck) {
    // Check if coordinates are actually different (with some tolerance for GPS noise)
    if (abs(currentLat - lastCheckLat) < 0.00001 && abs(currentLon - lastCheckLon) < 0.00001) {
      // Coordinates haven't changed - skip
      return;
    }

    // Coordinates changed, but check if meaningful distance (filter GPS jitter when stationary)
    float locationChange = calculateDistance(lastCheckLat, lastCheckLon, currentLat, currentLon);
    if (locationChange < 1.0) {  // Less than 1m - likely GPS noise, skip
      return;
    }
  }

  // GPS location changed significantly - trigger screen update
  lastCheckLat = currentLat;
  lastCheckLon = currentLon;
  firstCheck = false;
  gpsPositionChanged = true;  // Trigger immediate screen update
}

/**
 * Calculate automatic map rotation to show maximum path ahead
 *
 * MULTI-POINT WEIGHTED LOOK-AHEAD ALGORITHM:
 * - Samples 3 points ahead at 50m, 100m, and 150m
 * - Weights closer points more heavily (0.5, 0.3, 0.2)
 * - Calculates weighted average bearing using vector components
 * - Rotates map to show path ahead pointing upward
 * - Anticipates turns earlier while staying responsive to immediate direction
 *
 * In scrub mode, uses scrubbed position instead of GPS position
 */
void calculateAutoRotation() {
  // External references
  extern double currentLat;
  extern double currentLon;
  extern bool gpsValid;
  extern int mapRotation;
  extern bool rotationPending;
  extern unsigned long lastRotationChange;
  extern MapMode currentMapMode;
  extern double scrubLat;
  extern double scrubLon;

  if (!gpsValid || navigationTrack == nullptr || navigationTrackPointCount < 2) {
    return;
  }

  // Use scrubbed position if in scrub mode, otherwise use GPS position
  double posLat = currentLat;
  double posLon = currentLon;
  if (currentMapMode == 2) {  // 2 = MAP_MODE_SCRUB
    posLat = scrubLat;
    posLon = scrubLon;
  }

  // Track previous location to detect changes
  static double lastCalculatedLat = 0.0;
  static double lastCalculatedLon = 0.0;
  static bool firstCalculation = true;

  // Check if location has changed significantly
  if (!firstCalculation) {
    float locationChange = calculateDistance(lastCalculatedLat, lastCalculatedLon,
                                            posLat, posLon);
    // Use smaller threshold in scrub mode for responsiveness, larger in GPS mode to avoid jitter
    float threshold = (currentMapMode == 2) ? 0.1 : AUTO_ROTATION_MIN_MOVEMENT;
    if (locationChange < threshold) {
      return;  // Location hasn't changed enough - no need to recalculate
    }
    if (currentMapMode == 2) {
      Serial.printf("Scrub position moved %.1fm - recalculating rotation\n", locationChange);
    } else {
      Serial.printf("GPS moved %.1fm - recalculating rotation\n", locationChange);
    }
  } else {
    Serial.println("First rotation calculation");
  }

  // Find current position on track
  int closestIndex = findClosestTrackPoint(posLat, posLon);
  if (closestIndex < 0) {
    return;  // Error finding position
  }

  // Store current location for next comparison
  lastCalculatedLat = posLat;
  lastCalculatedLon = posLon;
  firstCalculation = false;

  // Calculate current route progress with exact fractional position
  RouteProgress currentProgress = RouteProgress(closestIndex, 0.0);

  if (closestIndex < navigationTrackPointCount - 1) {
    double lat1 = navigationTrack[closestIndex].lat;
    double lon1 = navigationTrack[closestIndex].lon;
    double lat2 = navigationTrack[closestIndex + 1].lat;
    double lon2 = navigationTrack[closestIndex + 1].lon;

    double segmentDx = lon2 - lon1;
    double segmentDy = lat2 - lat1;
    double userDx = posLon - lon1;
    double userDy = posLat - lat1;
    double dot = userDx * segmentDx + userDy * segmentDy;
    double lenSq = segmentDx * segmentDx + segmentDy * segmentDy;

    if (lenSq > 0.0) {
      double fraction = dot / lenSq;
      if (fraction < 0.0) fraction = 0.0;
      if (fraction > 1.0) fraction = 1.0;
      currentProgress.fraction = fraction;
    }
  }

  // Multi-point weighted look-ahead algorithm
  // Sample 3 points at different distances with decreasing weights
  const float lookAheadDistances[] = {50.0, 100.0, 150.0};
  const float lookAheadWeights[] = {0.5, 0.3, 0.2};
  const int numLookAheadPoints = 3;

  // Accumulate weighted bearing vectors (using sin/cos to avoid wrap-around issues)
  float weightedSinSum = 0.0;
  float weightedCosSum = 0.0;
  float totalWeight = 0.0;
  int validPoints = 0;

  for (int i = 0; i < numLookAheadPoints; i++) {
    Coordinate* lookAheadPoint = findPointOnRoute(currentProgress, lookAheadDistances[i], navigationTrack);

    if (lookAheadPoint != nullptr) {
      // Calculate bearing from current position to this look-ahead point
      float bearing = calculateBearing(posLat, posLon,
                                       lookAheadPoint->lat, lookAheadPoint->lon);

      // Convert bearing to vector components for averaging
      float bearingRad = bearing * M_PI / 180.0;
      weightedSinSum += sin(bearingRad) * lookAheadWeights[i];
      weightedCosSum += cos(bearingRad) * lookAheadWeights[i];
      totalWeight += lookAheadWeights[i];
      validPoints++;

      if (ENABLE_DEBUG_OUTPUT) {
        float actualDistance = calculateDistance(posLat, posLon,
                                                 lookAheadPoint->lat, lookAheadPoint->lon);
        Serial.printf("Look-ahead point %d: %.0fm away, bearing=%.1f°, weight=%.1f\n",
                      i + 1, actualDistance, bearing, lookAheadWeights[i]);
      }

      delete lookAheadPoint;
    }
  }

  // If we couldn't find any valid look-ahead points, try a fallback
  if (validPoints == 0) {
    // Near end of route - use last segment bearing
    if (navigationTrackPointCount >= 2) {
      double lat1 = navigationTrack[navigationTrackPointCount - 2].lat;
      double lon1 = navigationTrack[navigationTrackPointCount - 2].lon;
      double lat2 = navigationTrack[navigationTrackPointCount - 1].lat;
      double lon2 = navigationTrack[navigationTrackPointCount - 1].lon;
      float targetBearing = calculateBearing(lat1, lon1, lat2, lon2);

      // Invert rotation (same as main algorithm)
      targetBearing = -targetBearing;
      while (targetBearing < 0.0) targetBearing += 360.0;
      while (targetBearing >= 360.0) targetBearing -= 360.0;

      Serial.println("Using end-of-route bearing");

      // Apply the rotation if it changed significantly
      float bearingDiff = abs(targetBearing - mapRotation);
      if (bearingDiff > 180.0) {
        bearingDiff = 360.0 - bearingDiff;
      }

      if (bearingDiff >= ROTATION_CHANGE_THRESHOLD) {
        Serial.printf("Rotation update: %.1f° → %.1f° (diff=%.1f°)\n",
                      (float)mapRotation, targetBearing, bearingDiff);
        mapRotation = (int)targetBearing;
        rotationPending = true;
        lastRotationChange = millis();
      }
    }
    return;
  }

  // Calculate weighted average bearing from vector components
  float avgBearingRad = atan2(weightedSinSum, weightedCosSum);
  float targetBearing = avgBearingRad * 180.0 / M_PI;

  // Invert rotation: negate bearing to make rotation clockwise
  // This ensures bearing 90° (East) makes East point upward on the map
  targetBearing = -targetBearing;

  // Normalize to 0-360
  while (targetBearing < 0.0) targetBearing += 360.0;
  while (targetBearing >= 360.0) targetBearing -= 360.0;

  if (ENABLE_DEBUG_OUTPUT) {
    Serial.println("\n========== AUTO-ROTATION DEBUG ==========");
    if (currentMapMode == 2) {
      Serial.printf("Scrub position: %.6f, %.6f\n", posLat, posLon);
    } else {
      Serial.printf("GPS: %.6f, %.6f\n", posLat, posLon);
    }
    Serial.printf("Weighted average bearing: %.1f° (from %d points)\n", targetBearing, validPoints);
    Serial.printf("Current map rotation: %d°\n", mapRotation);
    Serial.println("=========================================\n");
  }

  // Apply the rotation if it changed significantly
  float bearingDiff = abs(targetBearing - mapRotation);
  if (bearingDiff > 180.0) {
    bearingDiff = 360.0 - bearingDiff;
  }

  if (bearingDiff >= ROTATION_CHANGE_THRESHOLD) {
    Serial.printf("Rotation update: %.1f° → %.1f° (diff=%.1f°)\n",
                  (float)mapRotation, targetBearing, bearingDiff);
    mapRotation = (int)targetBearing;
    rotationPending = true;
    lastRotationChange = millis();
  } else {
    Serial.printf("Rotation stable: %.1f° (target=%.1f°)\n",
                  (float)mapRotation, targetBearing);
  }
}

/**
 * Update navigation state
 * Called periodically to update navigation data
 * Tracks position, calculates distances, and handles auto-rotation
 * Note: GPS position change detection is now handled by checkGPSPositionChange()
 */
void updateNavigationState() {
  if (!navigationActive) return;

  // External references
  extern double currentLat;
  extern double currentLon;
  extern bool gpsValid;

  // Calculate elapsed time (always update this)
  navigationElapsedTime = (millis() - navigationStartTime) / 1000;

  // Check if auto-rotation should resume after manual override
  if (!autoRotationEnabled && (millis() - lastManualRotationTime >= MANUAL_ROTATION_TIMEOUT)) {
    Serial.println("Auto-rotation resuming after manual override timeout");
    autoRotationEnabled = true;
    // Notification would appear here, but simplified for now
  }

  // Update position tracking if GPS is valid and track is loaded
  if (gpsValid && navigationTrack != nullptr && navigationTrackPointCount > 0) {
    // Find current position on track
    int closestIndex = findClosestTrackPoint(currentLat, currentLon);

    if (closestIndex >= 0) {
      currentWaypointIndex = closestIndex;

      // Calculate distance traveled (sum from start to current GPS position)
      distanceTraveled = 0.0;

      // Add all complete segments before current position
      for (int i = 0; i < currentWaypointIndex && i < navigationTrackPointCount - 1; i++) {
        double lat1 = navigationTrack[i].lat;
        double lon1 = navigationTrack[i].lon;
        double lat2 = navigationTrack[i + 1].lat;
        double lon2 = navigationTrack[i + 1].lon;

        distanceTraveled += calculateDistance(lat1, lon1, lat2, lon2);
      }

      // Add distance from closest waypoint to actual current GPS position
      if (currentWaypointIndex < navigationTrackPointCount) {
        double waypointLat = navigationTrack[currentWaypointIndex].lat;
        double waypointLon = navigationTrack[currentWaypointIndex].lon;
        float distanceFromWaypoint = calculateDistance(waypointLat, waypointLon, currentLat, currentLon);
        distanceTraveled += distanceFromWaypoint;
      }

      // Calculate remaining distance (from current GPS position to end)
      totalDistanceRemaining = 0.0;

      // Add distance from current GPS position to next waypoint
      if (currentWaypointIndex < navigationTrackPointCount - 1) {
        double nextWaypointLat = navigationTrack[currentWaypointIndex + 1].lat;
        double nextWaypointLon = navigationTrack[currentWaypointIndex + 1].lon;
        totalDistanceRemaining += calculateDistance(currentLat, currentLon, nextWaypointLat, nextWaypointLon);
      }

      // Add all remaining complete segments after next waypoint
      for (int i = currentWaypointIndex + 1; i < navigationTrackPointCount - 1; i++) {
        double lat1 = navigationTrack[i].lat;
        double lon1 = navigationTrack[i].lon;
        double lat2 = navigationTrack[i + 1].lat;
        double lon2 = navigationTrack[i + 1].lon;

        totalDistanceRemaining += calculateDistance(lat1, lon1, lat2, lon2);
      }

      // Calculate distance to next turn and turn type
      // Look ahead to find the next significant turn
      distanceToNextTurn = 0.0;
      nextTurnType = 2;  // Default: straight

      const float TURN_DETECTION_THRESHOLD = 20.0;  // Minimum bearing change in degrees to detect a turn
      const float TURN_LOOKAHEAD_SEGMENTS = 10;     // Look ahead up to 10 segments to find a turn

      // Calculate current bearing (from previous point to next point)
      float currentBearing = 0.0;
      if (currentWaypointIndex > 0 && currentWaypointIndex < navigationTrackPointCount - 1) {
        currentBearing = calculateBearing(
          navigationTrack[currentWaypointIndex - 1].lat,
          navigationTrack[currentWaypointIndex - 1].lon,
          navigationTrack[currentWaypointIndex + 1].lat,
          navigationTrack[currentWaypointIndex + 1].lon
        );
      } else if (currentWaypointIndex < navigationTrackPointCount - 1) {
        // At start of route, use bearing to next point
        currentBearing = calculateBearing(
          currentLat, currentLon,
          navigationTrack[currentWaypointIndex + 1].lat,
          navigationTrack[currentWaypointIndex + 1].lon
        );
      }

      // Look ahead to find next significant turn
      bool turnFound = false;
      float accumulatedDistance = 0.0;

      // Start with distance from current position to first waypoint being checked
      if (currentWaypointIndex < navigationTrackPointCount) {
        accumulatedDistance = calculateDistance(
          currentLat, currentLon,
          navigationTrack[currentWaypointIndex].lat,
          navigationTrack[currentWaypointIndex].lon
        );
      }

      for (int i = currentWaypointIndex; i < navigationTrackPointCount - 2 && i < currentWaypointIndex + TURN_LOOKAHEAD_SEGMENTS; i++) {
        // Calculate distance for this segment
        float segmentDistance = calculateDistance(
          navigationTrack[i].lat, navigationTrack[i].lon,
          navigationTrack[i + 1].lat, navigationTrack[i + 1].lon
        );

        // Calculate bearing change at this waypoint
        float bearingBefore = calculateBearing(
          navigationTrack[i].lat, navigationTrack[i].lon,
          navigationTrack[i + 1].lat, navigationTrack[i + 1].lon
        );

        float bearingAfter = calculateBearing(
          navigationTrack[i + 1].lat, navigationTrack[i + 1].lon,
          navigationTrack[i + 2].lat, navigationTrack[i + 2].lon
        );

        // Calculate angle difference
        float bearingChange = bearingAfter - bearingBefore;

        // Normalize to -180 to 180 range
        while (bearingChange > 180.0) bearingChange -= 360.0;
        while (bearingChange < -180.0) bearingChange += 360.0;

        accumulatedDistance += segmentDistance;

        // Check if this is a significant turn
        if (abs(bearingChange) >= TURN_DETECTION_THRESHOLD) {
          turnFound = true;
          distanceToNextTurn = accumulatedDistance;

          // Determine turn type based on bearing change
          float absBearingChange = abs(bearingChange);

          if (absBearingChange >= 135.0) {
            // U-turn (135-180 degrees)
            nextTurnType = 7;
          } else if (absBearingChange >= 75.0) {
            // Sharp turn (75-135 degrees)
            nextTurnType = (bearingChange > 0) ? 6 : 5;  // 6=sharp right, 5=sharp left
          } else if (absBearingChange >= 45.0) {
            // Regular turn (45-75 degrees)
            nextTurnType = (bearingChange > 0) ? 1 : 0;  // 1=right, 0=left
          } else {
            // Slight turn (20-45 degrees)
            nextTurnType = (bearingChange > 0) ? 4 : 3;  // 4=slight right, 3=slight left
          }

          break;
        }
      }

      // If no turn found, calculate distance to end of route
      if (!turnFound) {
        distanceToNextTurn = totalDistanceRemaining;
        nextTurnType = 2;  // Straight
      }

      // Update elevation and track gain/loss
      static float lastElevation = 0.0;
      static bool firstElevationUpdate = true;

      // Reset elevation tracking when navigation just started (both gain/loss are 0)
      if (elevationGain == 0.0 && elevationLoss == 0.0) {
        firstElevationUpdate = true;
      }

      if (currentWaypointIndex < navigationTrackPointCount) {
        float newElevation = navigationTrack[currentWaypointIndex].elev;

        // Initialize on first update
        if (firstElevationUpdate) {
          lastElevation = newElevation;
          currentElevation = newElevation;
          firstElevationUpdate = false;
        } else {
          // Calculate elevation change
          float elevationChange = newElevation - lastElevation;

          // Only count changes > 1m to avoid GPS noise
          if (elevationChange > 1.0) {
            elevationGain += elevationChange;
          } else if (elevationChange < -1.0) {
            elevationLoss += abs(elevationChange);
          }

          lastElevation = newElevation;
          currentElevation = newElevation;
        }
      }

      // Update current speed from GPS
      if (gps.speed.isValid()) {
        currentSpeed = gps.speed.kmph();

        // Update max speed if current speed is higher
        if (currentSpeed > maxSpeed) {
          maxSpeed = currentSpeed;
        }
      }

      // Calculate average speed from total distance and elapsed time
      if (navigationElapsedTime > 0) {
        averageSpeed = (distanceTraveled / navigationElapsedTime) * 3.6;  // m/s to km/h
      }

      // Debug output for distance tracking
      Serial.printf("Navigation update: traveled=%.1fm (%.2fkm), remaining=%.1fm (%.2fkm), progress=%.1f%%\n",
                    distanceTraveled, distanceTraveled/1000.0,
                    totalDistanceRemaining, totalDistanceRemaining/1000.0,
                    (distanceTraveled / totalDistance) * 100.0);
    }
  }

  // Perform auto-rotation if enabled
  if (autoRotationEnabled && gpsValid) {
    calculateAutoRotation();
  }
}

void drawNavigationStatRow(int y, const char* label, const char* value) {
  const int leftX = 6;
  const int rightPadding = 6;

  u8g2_display.setFont(u8g2_font_helvR08_tf);
  u8g2_display.setCursor(leftX, y);
  u8g2_display.print(label);

  u8g2_display.setFont(u8g2_font_helvB10_tf);
  int valueWidth = u8g2_display.getUTF8Width(value);
  u8g2_display.setCursor(DISPLAY_WIDTH - rightPadding - valueWidth, y);
  u8g2_display.print(value);
}

void drawNavigationSectionTitle(int y, const char* title) {
  u8g2_display.setFont(u8g2_font_helvB08_tf);
  u8g2_display.setCursor(6, y);
  u8g2_display.print(title);
}

void drawNavigationSectionDivider(int y) {
  display.drawLine(6, y, DISPLAY_WIDTH - 6, y, GxEPD_BLACK);
}

void formatNavigationDuration(unsigned long seconds, char* buffer, size_t bufferSize) {
  unsigned long hours = seconds / 3600;
  unsigned long minutes = (seconds % 3600) / 60;
  unsigned long secs = seconds % 60;

  if (hours > 0) {
    snprintf(buffer, bufferSize, "%lu:%02lu:%02lu", hours, minutes, secs);
  } else {
    snprintf(buffer, bufferSize, "%02lu:%02lu", minutes, secs);
  }
}

void formatNavigationHoursMinutes(unsigned long seconds, char* buffer, size_t bufferSize) {
  unsigned long hours = seconds / 3600;
  unsigned long minutes = (seconds % 3600) / 60;

  if (hours > 0) {
    snprintf(buffer, bufferSize, "%luh %lumin", hours, minutes);
  } else {
    snprintf(buffer, bufferSize, "%lumin", minutes);
  }
}

/**
 * Draw a circular progress arc
 * centerX, centerY: center of the circle
 * radius: outer radius
 * thickness: line thickness
 * progress: 0.0 to 1.0
 * Draws from top (12 o'clock) clockwise
 */
void drawProgressArc(int centerX, int centerY, int radius, int thickness, float progress) {
  // Draw background circle (thin outline)
  display.drawCircle(centerX, centerY, radius, GxEPD_BLACK);
  display.drawCircle(centerX, centerY, radius - thickness, GxEPD_BLACK);

  if (progress <= 0.0f) {
    return;
  }
  if (progress > 1.0f) {
    progress = 1.0f;
  }

  // Draw progress arc (filled) by rasterizing pixels in the ring.
  // Screen coordinates use +Y down, so atan2 increases clockwise.
  const float startAngle = -M_PI / 2.0f;  // -90 degrees
  const float arcAngle = 2.0f * M_PI * progress;
  const int innerRadius = max(0, radius - thickness);
  const int outerRadius = max(innerRadius, radius);
  const int innerSq = innerRadius * innerRadius;
  const int outerSq = outerRadius * outerRadius;

  for (int y = -outerRadius; y <= outerRadius; y++) {
    for (int x = -outerRadius; x <= outerRadius; x++) {
      int rSq = (x * x) + (y * y);
      if (rSq < innerSq || rSq > outerSq) {
        continue;
      }
      float angle = atan2f((float)y, (float)x);
      float angleFromStart = angle - startAngle;
      if (angleFromStart < 0.0f) {
        angleFromStart += (2.0f * M_PI);
      }
      if (angleFromStart <= arcAngle) {
        display.drawPixel(centerX + x, centerY + y, GxEPD_BLACK);
      }
    }
  }
}

/**
 * Render navigation statistics view
 * Shows real-time navigation data during active navigation
 * Modern design with circular progress indicator
 */
void renderNavigationStatsView() {
  Serial.println("Rendering navigation stats view");

  display.setPartialWindow(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
  display.firstPage();

  do {
    display.fillScreen(GxEPD_WHITE);

    // Setup text rendering
    u8g2_display.setFontMode(1);
    u8g2_display.setForegroundColor(GxEPD_BLACK);
    u8g2_display.setBackgroundColor(GxEPD_WHITE);

    const int leftX = 6;
    const int rightX = DISPLAY_WIDTH - 6;
    const int centerX = DISPLAY_WIDTH / 2;
    char valueStr[32];
    char timeStr[16];

    // === CIRCULAR PROGRESS INDICATOR ===
    const int circleRadius = 52;
    const int circleThickness = 7;
    const int circleCenterX = centerX;
    const int circleCenterY = 62;

    float progressPercent = 0.0f;
    if (totalDistance > 0) {
      progressPercent = distanceTraveled / totalDistance;
      if (progressPercent < 0.0f) progressPercent = 0.0f;
      if (progressPercent > 1.0f) progressPercent = 1.0f;
    }

    // Draw the progress arc
    drawProgressArc(circleCenterX, circleCenterY, circleRadius, circleThickness, progressPercent);

    // Percentage in the center (large, bold)
    u8g2_display.setFont(u8g2_font_helvB24_tf);
    snprintf(valueStr, sizeof(valueStr), "%.0f%%", progressPercent * 100.0f);
    int percentWidth = u8g2_display.getUTF8Width(valueStr);
    u8g2_display.setCursor(circleCenterX - percentWidth / 2, circleCenterY + 10);
    u8g2_display.print(valueStr);

    // Distance below circle: "2.5 / 15.0 km" (bigger font)
    int distanceY = circleCenterY + circleRadius + 18;
    u8g2_display.setFont(u8g2_font_helvB12_tf);
    snprintf(valueStr, sizeof(valueStr), "%.1f / %.1f km",
             distanceTraveled / 1000.0, totalDistance / 1000.0);
    int distWidth = u8g2_display.getUTF8Width(valueStr);
    u8g2_display.setCursor(centerX - distWidth / 2, distanceY);
    u8g2_display.print(valueStr);

    // === REMAINING DISTANCE - CENTERED BELOW ===
    int remainingY = distanceY + 24;

    // Remaining distance (centered, medium size)
    u8g2_display.setFont(u8g2_font_helvB14_tf);
    snprintf(valueStr, sizeof(valueStr), "%.1f km left", totalDistanceRemaining / 1000.0);
    int remainingWidth = u8g2_display.getUTF8Width(valueStr);
    u8g2_display.setCursor(centerX - remainingWidth / 2, remainingY);
    u8g2_display.print(valueStr);

    // === DIVIDER ===
    int dividerY = remainingY + 14;
    display.drawLine(leftX, dividerY, rightX, dividerY, GxEPD_BLACK);

    // === BOTTOM STATS - TWO COLUMNS + CENTERED SPEED ===
    int headingY = dividerY + 16;
    int valueY = headingY + 18;

    // Calculate column positions (2 columns + centered speed row)
    int col1X = DISPLAY_WIDTH / 4;       // ETA - left half
    int col2X = DISPLAY_WIDTH * 3 / 4;   // Elapsed - right half
    int speedCenterX = DISPLAY_WIDTH / 2;

    // === COLUMN 1: ETA ===
    u8g2_display.setFont(u8g2_font_helvR08_tf);
    int etaLabelWidth = u8g2_display.getUTF8Width("ETA");
    u8g2_display.setCursor(col1X - etaLabelWidth / 2, headingY);
    u8g2_display.print("ETA");

    if (averageSpeed > 0) {
      float remainingHours = (totalDistanceRemaining / 1000.0f) / averageSpeed;
      if (remainingHours < 0.0f) remainingHours = 0.0f;
      unsigned long remainingSeconds = (unsigned long)(remainingHours * 3600.0f);
      formatNavigationHoursMinutes(remainingSeconds, timeStr, sizeof(timeStr));
    } else {
      snprintf(timeStr, sizeof(timeStr), "--");
    }

    u8g2_display.setFont(u8g2_font_helvB10_tf);
    int etaWidth = u8g2_display.getUTF8Width(timeStr);
    u8g2_display.setCursor(col1X - etaWidth / 2, valueY);
    u8g2_display.print(timeStr);

    // === COLUMN 2: ELAPSED ===
    u8g2_display.setFont(u8g2_font_helvR08_tf);
    int elapsedLabelWidth = u8g2_display.getUTF8Width("Elapsed");
    u8g2_display.setCursor(col2X - elapsedLabelWidth / 2, headingY);
    u8g2_display.print("Elapsed");

    formatNavigationDuration(navigationElapsedTime, timeStr, sizeof(timeStr));
    u8g2_display.setFont(u8g2_font_helvB10_tf);
    int elapsedWidth = u8g2_display.getUTF8Width(timeStr);
    u8g2_display.setCursor(col2X - elapsedWidth / 2, valueY);
    u8g2_display.print(timeStr);

    // === SPEED (CENTERED BELOW ETA/ELAPSED) ===
    int speedHeadingY = valueY + 18;
    int speedValueY = speedHeadingY + 18;
    int speedValue2Y = speedValueY + 16;

    u8g2_display.setFont(u8g2_font_helvR08_tf);
    int speedLabelWidth = u8g2_display.getUTF8Width("Speed");
    u8g2_display.setCursor(speedCenterX - speedLabelWidth / 2, speedHeadingY);
    u8g2_display.print("Speed");

    // Average speed
    u8g2_display.setFont(u8g2_font_helvB10_tf);
    snprintf(valueStr, sizeof(valueStr), "%.0f avg", averageSpeed);
    int avgWidth = u8g2_display.getUTF8Width(valueStr);
    u8g2_display.setCursor(speedCenterX - avgWidth / 2, speedValueY);
    u8g2_display.print(valueStr);

    // Max speed
    snprintf(valueStr, sizeof(valueStr), "%.0f max", maxSpeed);
    int maxWidth = u8g2_display.getUTF8Width(valueStr);
    u8g2_display.setCursor(speedCenterX - maxWidth / 2, speedValue2Y);
    u8g2_display.print(valueStr);

    drawStatusBarNoSeparator();
    drawPageDots();

    // Draw notification overlay
    drawNotificationOverlay();

  } while (display.nextPage());
}

// --- FUTURE NAVIGATION IMPLEMENTATIONS ---

/*
 * When implementing active trip navigation, consider adding:
 *
 * 1. Route Loading:
 *    - Load GPX file from selected trip
 *    - Parse waypoints and route segments
 *    - Calculate total distance and elevation
 *
 * 2. Position Tracking:
 *    - Match current GPS position to route
 *    - Calculate distance along route
 *    - Detect off-route conditions
 *
 * 3. Turn-by-Turn:
 *    - Identify upcoming turns/waypoints
 *    - Calculate bearing to next waypoint
 *    - Show directional arrows on map
 *    - Provide distance to next turn
 *
 * 4. Navigation UI:
 *    - Overlay route on map view
 *    - Highlight current segment
 *    - Show navigation compass
 *    - Display remaining distance/time
 *
 * 5. Statistics Tracking:
 *    - Real-time distance calculation
 *    - Speed averaging
 *    - Elevation gain tracking
 *    - Time tracking
 *    - Calories burned (optional)
 *
 * 6. Alerts & Notifications:
 *    - Approaching waypoint
 *    - Off-route warning
 *    - Destination reached
 *    - Low battery during navigation
 *
 * Example function signatures:
 *
 * void startTripNavigation(const char* tripDirName) {
 *   // Load GPX file
 *   // Initialize navigation state
 *   // Start GPS tracking
 *   // Switch to navigation view
 * }
 *
 * void updateNavigationPosition() {
 *   // Update current position
 *   // Match to route
 *   // Calculate remaining distance
 *   // Update ETA
 * }
 *
 * void renderNavigationOverlay() {
 *   // Draw route on map
 *   // Show current position marker
 *   // Display next waypoint indicator
 *   // Show navigation stats
 * }
 */

#endif // MAP_NAVIGATION_H
