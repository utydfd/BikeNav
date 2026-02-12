#ifndef PAGE_TRIPS_H
#define PAGE_TRIPS_H

#include <GxEPD2_BW.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <SD.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

// External references from main program
extern GxEPD2_BW<GxEPD2_290_BS, GxEPD2_290_BS::HEIGHT> display;
extern U8G2_FOR_ADAFRUIT_GFX u8g2_display;
extern const int DISPLAY_WIDTH;
extern const int DISPLAY_HEIGHT;
extern bool deviceConnected;

// External functions from map_trips.h
extern int countTripsOnSD();
extern bool getTripNameByIndex(int index, char* outName, size_t maxLen);
extern bool getTripDirNameByIndex(int index, char* outName, size_t maxLen);
extern bool readTripNameFromMetadata(const char* tripDirName, char* outName, size_t maxLen);
extern bool readTripListMetadata(const char* tripDirName, char* outName, size_t maxLen, uint64_t* outCreatedAt);

// External from ble_handler.h
extern void requestNavigateHome();

// External from notification_system.h
extern void drawNotificationOverlay();

// External from bitmaps.h
extern const unsigned char ICON_HOME[];
extern const int ICON_SIZE;

// --- TRIPS PAGE STATE ---
const int TRIPS_PER_PAGE = 7;  // Number of trips to show per page (excluding Navigate Home)
int currentTripsPage = 0;      // Current page index (0-based)
int totalTripsPages = 1;       // Total number of pages
int selectedTripIndex = 0;     // Selected trip index within current page (0 = Navigate Home, 1+ = trips)
bool tripsNeedsRedraw = false; // Flag for smart rendering

const int TRIP_NAME_MAX_LEN = 32;
const int TRIP_DIR_MAX_LEN = 64;

struct TripListEntry {
  char dirName[TRIP_DIR_MAX_LEN];
  char displayName[TRIP_NAME_MAX_LEN];
};

struct TripSortEntry {
  char dirName[TRIP_DIR_MAX_LEN];
  char displayName[TRIP_NAME_MAX_LEN];
  uint64_t createdAt;
};

TripListEntry cachedTrips[TRIPS_PER_PAGE];
int cachedTripCount = 0;
int cachedTripsOnPage = 0;
int cachedTripsPageIndex = -1;
bool tripsCacheValid = false;

// --- FUNCTION PROTOTYPES ---
void initTripsPage();
void renderTripsPage();
void updateTripsPage();
void handleTripsEncoder(int delta);
void handleTripsButton();
bool handleTripsBack();
void handleTripsNextPage();
void drawTripsPageDots();
void refreshTripsCache();
void ensureTripsCache();

// --- IMPLEMENTATION ---

/**
 * Initialize trips page
 */
void initTripsPage() {
  Serial.println("Initializing trips page");

  // Reset to first page
  currentTripsPage = 0;
  selectedTripIndex = 0;  // Start with Navigate Home selected
  tripsCacheValid = false;
  refreshTripsCache();

  Serial.printf("Total trips: %d, Pages: %d, Trips per page: %d\n",
                cachedTripCount, totalTripsPages, TRIPS_PER_PAGE);
}

/**
 * Draw page navigation dots at bottom
 */
void drawTripsPageDots() {
  if (totalTripsPages <= 1) {
    return;  // Don't show dots if only one page
  }

  const int DOT_RADIUS = 3;
  const int DOT_SPACING = 10;
  const int INDICATOR_Y = DISPLAY_HEIGHT - 4;  // 4px from bottom

  // Calculate total width needed
  int totalWidth = (totalTripsPages * DOT_RADIUS * 2) + ((totalTripsPages - 1) * DOT_SPACING);
  int startX = (DISPLAY_WIDTH - totalWidth) / 2;  // Center horizontally

  for (int i = 0; i < totalTripsPages; i++) {
    int dotX = startX + (i * (DOT_RADIUS * 2 + DOT_SPACING)) + DOT_RADIUS;

    if (i == currentTripsPage) {
      // Current page - filled circle
      display.fillCircle(dotX, INDICATOR_Y, DOT_RADIUS, GxEPD_BLACK);
    } else {
      // Other pages - empty circle
      display.drawCircle(dotX, INDICATOR_Y, DOT_RADIUS, GxEPD_BLACK);
    }
  }
}

/**
 * Render trips page
 */
void renderTripsPage() {
  Serial.println("Rendering trips page");
  ensureTripsCache();

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
    u8g2_display.setCursor(4, 20);
    u8g2_display.print("TRIPS");

    // Trip count in top right corner
    char tripCountStr[16];
    snprintf(tripCountStr, sizeof(tripCountStr), "%d", cachedTripCount);
    u8g2_display.setFont(u8g2_font_helvB10_tf);
    int tripCountWidth = u8g2_display.getUTF8Width(tripCountStr);
    u8g2_display.setCursor(DISPLAY_WIDTH - tripCountWidth - 4, 20);
    u8g2_display.print(tripCountStr);

    int yPos = 45;
    const int lineHeight = 20;

    // --- NAVIGATE HOME SECTION (Always at top, visually distinct) ---
    bool isNavigateHomeSelected = (selectedTripIndex == 0);

    // Draw distinct box for Navigate Home
    int navHomeBoxHeight = 48;  // Height for icon
    int navHomeBoxTop = yPos - 16;

    if (isNavigateHomeSelected) {
      // Selected - filled background
      display.fillRect(2, navHomeBoxTop, DISPLAY_WIDTH - 4, navHomeBoxHeight, GxEPD_BLACK);
      u8g2_display.setForegroundColor(GxEPD_WHITE);
      u8g2_display.setBackgroundColor(GxEPD_BLACK);
    } else {
      // Not selected - outlined box
      display.drawRect(2, navHomeBoxTop, DISPLAY_WIDTH - 4, navHomeBoxHeight, GxEPD_BLACK);
      display.drawRect(3, navHomeBoxTop + 1, DISPLAY_WIDTH - 6, navHomeBoxHeight - 2, GxEPD_BLACK);
    }

    // Draw home icon (39x39) - left-aligned with padding
    int iconX = 8;  // Left padding
    int iconY = navHomeBoxTop + (navHomeBoxHeight - ICON_SIZE) / 2;
    display.drawBitmap(iconX, iconY, ICON_HOME, ICON_SIZE, ICON_SIZE,
                       isNavigateHomeSelected ? GxEPD_WHITE : GxEPD_BLACK);

    // Navigate Home text - 2 lines, positioned to the right of icon
    u8g2_display.setFont(u8g2_font_helvB10_tf);
    int textX = iconX + ICON_SIZE + 8;  // Position after icon with 8px spacing
    int textYLine1 = navHomeBoxTop + (navHomeBoxHeight / 2) - 4;  // First line above center
    int textYLine2 = navHomeBoxTop + (navHomeBoxHeight / 2) + 10; // Second line below center

    u8g2_display.setCursor(textX, textYLine1);
    u8g2_display.print("Navigate");
    u8g2_display.setCursor(textX, textYLine2);
    u8g2_display.print("Home");

    // Reset colors
    u8g2_display.setForegroundColor(GxEPD_BLACK);
    u8g2_display.setBackgroundColor(GxEPD_WHITE);

    yPos += navHomeBoxHeight + 10;  // Move down after Navigate Home section

    // --- TRIPS LIST ---
    if (cachedTripCount == 0) {
      // No trips available
      u8g2_display.setFont(u8g2_font_helvR08_tf);
      u8g2_display.setCursor(8, yPos + 20);
      u8g2_display.print("No trips on device");

      u8g2_display.setCursor(8, yPos + 40);
      u8g2_display.print("Transfer trips from");
      u8g2_display.setCursor(8, yPos + 55);
      u8g2_display.print("your phone via BLE");
    } else {
      // Show page indicator if multiple pages
      if (totalTripsPages > 1) {
        u8g2_display.setFont(u8g2_font_helvR08_tf);
        char pageStr[32];
        snprintf(pageStr, sizeof(pageStr), "Page %d/%d", currentTripsPage + 1, totalTripsPages);
        int pageStrWidth = u8g2_display.getUTF8Width(pageStr);
        u8g2_display.setCursor(DISPLAY_WIDTH - pageStrWidth - 4, yPos - 2);
        u8g2_display.print(pageStr);
      }

      // Render trips for current page
      u8g2_display.setFont(u8g2_font_helvB08_tf);

      for (int i = 0; i < cachedTripsOnPage; i++) {
        // selectedTripIndex: 0 = Navigate Home, 1+ = trips (1-based for trips)
        bool isSelected = (selectedTripIndex == i + 1);

        // Highlight selected trip
        if (isSelected) {
          // Draw selection rectangle - moved down by 3px for better vertical centering
          display.fillRect(2, yPos - 11, DISPLAY_WIDTH - 4, lineHeight - 2, GxEPD_BLACK);
          u8g2_display.setForegroundColor(GxEPD_WHITE);
          u8g2_display.setBackgroundColor(GxEPD_BLACK);
          u8g2_display.setCursor(8, yPos);
          u8g2_display.print("> ");
          u8g2_display.print(cachedTrips[i].displayName);
          // Reset colors
          u8g2_display.setForegroundColor(GxEPD_BLACK);
          u8g2_display.setBackgroundColor(GxEPD_WHITE);
        } else {
          u8g2_display.setCursor(8, yPos);
          u8g2_display.print("  ");
          u8g2_display.print(cachedTrips[i].displayName);
        }
        yPos += lineHeight;
      }
    }

    // Draw page dots at bottom (if multiple pages)
    drawTripsPageDots();

    // Draw notification overlay
    drawNotificationOverlay();

  } while (display.nextPage());

  tripsNeedsRedraw = false;
}

/**
 * Update trips page (called from main loop)
 */
void updateTripsPage() {
  if (tripsNeedsRedraw) {
    tripsNeedsRedraw = false;
    tripsCacheValid = false;
    renderTripsPage();
  }
}

/**
 * Handle encoder rotation
 */
void handleTripsEncoder(int delta) {
  ensureTripsCache();
  int tripsOnCurrentPage = cachedTripsOnPage;

  // Total selectable items on current page: Navigate Home (1) + trips on this page
  int totalItemsOnPage = 1 + tripsOnCurrentPage;

  // Update selection with loopable scrolling
  selectedTripIndex += delta;

  // Wrap around (loopable)
  if (selectedTripIndex < 0) {
    // Going up from top - wrap to bottom
    selectedTripIndex = totalItemsOnPage - 1;
  } else if (selectedTripIndex >= totalItemsOnPage) {
    // Going down from bottom - wrap to top
    selectedTripIndex = 0;
  }

  Serial.printf("Trips encoder: delta=%d, selected=%d/%d\n", delta, selectedTripIndex, totalItemsOnPage - 1);

  // Render immediately for responsive scrolling
  renderTripsPage();
}

/**
 * Handle encoder button press
 */
void handleTripsButton() {
  Serial.println("Trips button pressed");

  ensureTripsCache();

  // Check if Navigate Home is selected
  if (selectedTripIndex == 0) {
    // Navigate Home selected - open Navigate Home detail view
    Serial.println("Opening Navigate Home detail view");

    // Set Navigate Home mode flags
    extern bool isNavigateHomeMode;
    extern bool navigateHomePathLoaded;
    isNavigateHomeMode = true;
    navigateHomePathLoaded = false;

    // Clear selected trip name (Navigate Home has no directory)
    extern char selectedTripDirName[64];
    selectedTripDirName[0] = '\0';

    // Reset button selection
    extern int selectedTripButton;
    selectedTripButton = 0;

    // Clear any previously loaded track
    extern void freeLoadedTrack();
    freeLoadedTrack();

    // Switch to trip detail sub-page
    extern MapSubPage currentMapSubPage;
    extern void renderTripDetailView();
    currentMapSubPage = MAP_SUBPAGE_TRIP_DETAIL;
    renderTripDetailView();
  } else {
    // Trip selected - open trip detail
    int totalTrips = cachedTripCount;

    // Convert selectedTripIndex (1-based for trips) to global trip index
    int globalTripIndex = (currentTripsPage * TRIPS_PER_PAGE) + (selectedTripIndex - 1);

    if (globalTripIndex >= 0 && globalTripIndex < totalTrips) {
      // Get trip directory name and open detail view
      int pageIndex = selectedTripIndex - 1;
      if (pageIndex >= 0 && pageIndex < cachedTripsOnPage && cachedTrips[pageIndex].dirName[0] != '\0') {
        Serial.printf("Opening trip detail: %s (global index %d)\n", cachedTrips[pageIndex].dirName, globalTripIndex);

        // Call external function to open trip detail (from page_map.h)
        extern void openTripDetail(const char* tripDirName);
        openTripDetail(cachedTrips[pageIndex].dirName);
      } else {
        char selectedTripDirName[64];
        if (getTripDirNameByIndex(globalTripIndex, selectedTripDirName, sizeof(selectedTripDirName))) {
          Serial.printf("Opening trip detail: %s (global index %d)\n", selectedTripDirName, globalTripIndex);

          // Call external function to open trip detail (from page_map.h)
          extern void openTripDetail(const char* tripDirName);
          openTripDetail(selectedTripDirName);
        }
      }
    }
  }
}

/**
 * Handle back button
 */
bool handleTripsBack() {
  // Return to map view
  extern PageType currentPage;
  extern void navigateToPage(PageType page);

  Serial.println("Trips: Back to map");
  navigateToPage(PAGE_MAP);
  return true;  // Back handled
}

/**
 * Handle next page button (pagination)
 */
void handleTripsNextPage() {
  ensureTripsCache();
  if (totalTripsPages <= 1) {
    return;  // No pagination needed
  }

  // Move to next page (wrap around)
  currentTripsPage++;
  if (currentTripsPage >= totalTripsPages) {
    currentTripsPage = 0;  // Wrap to first page
  }

  // Reset selection to Navigate Home when changing pages
  selectedTripIndex = 0;

  Serial.printf("Trips: Next page -> %d/%d\n", currentTripsPage + 1, totalTripsPages);

  tripsCacheValid = false;
  refreshTripsCache();

  // Render new page
  renderTripsPage();
}

static int compareTripsByCreatedAtDesc(const void* left, const void* right) {
  const TripSortEntry* a = static_cast<const TripSortEntry*>(left);
  const TripSortEntry* b = static_cast<const TripSortEntry*>(right);
  if (a->createdAt < b->createdAt) return 1;
  if (a->createdAt > b->createdAt) return -1;
  return strcmp(a->dirName, b->dirName);
}

void refreshTripsCache() {
  cachedTripCount = countTripsOnSD();

  cachedTripsOnPage = 0;
  for (int i = 0; i < TRIPS_PER_PAGE; i++) {
    cachedTrips[i].dirName[0] = '\0';
    cachedTrips[i].displayName[0] = '\0';
  }

  if (cachedTripCount == 0) {
    totalTripsPages = 1;
    currentTripsPage = 0;
    cachedTripsPageIndex = currentTripsPage;
    selectedTripIndex = 0;
    tripsCacheValid = true;
    return;
  }

  TripSortEntry* tripEntries = (TripSortEntry*)malloc(sizeof(TripSortEntry) * cachedTripCount);
  if (!tripEntries) {
    totalTripsPages = (cachedTripCount + TRIPS_PER_PAGE - 1) / TRIPS_PER_PAGE;
    if (totalTripsPages < 1) totalTripsPages = 1;
    if (currentTripsPage >= totalTripsPages) currentTripsPage = totalTripsPages - 1;
    cachedTripsPageIndex = currentTripsPage;

    int startTripIndex = currentTripsPage * TRIPS_PER_PAGE;
    int endTripIndex = min(startTripIndex + TRIPS_PER_PAGE, cachedTripCount);

    File tripsDir = SD.open("/Trips");
    if (!tripsDir) {
      tripsCacheValid = true;
      return;
    }

    int currentIndex = 0;
    File entry = tripsDir.openNextFile();
    while (entry) {
      if (entry.isDirectory()) {
        if (currentIndex >= startTripIndex && currentIndex < endTripIndex && cachedTripsOnPage < TRIPS_PER_PAGE) {
          const char* dirName = entry.name();
          strncpy(cachedTrips[cachedTripsOnPage].dirName, dirName, TRIP_DIR_MAX_LEN - 1);
          cachedTrips[cachedTripsOnPage].dirName[TRIP_DIR_MAX_LEN - 1] = '\0';
          readTripNameFromMetadata(dirName, cachedTrips[cachedTripsOnPage].displayName, TRIP_NAME_MAX_LEN);
          cachedTripsOnPage++;
        }
        currentIndex++;
        if (currentIndex >= endTripIndex) {
          entry.close();
          break;
        }
      }
      entry.close();
      entry = tripsDir.openNextFile();
    }
    tripsDir.close();

    int totalItemsOnPage = 1 + cachedTripsOnPage;
    if (selectedTripIndex >= totalItemsOnPage) selectedTripIndex = 0;
    tripsCacheValid = true;
    return;
  }

  totalTripsPages = (cachedTripCount + TRIPS_PER_PAGE - 1) / TRIPS_PER_PAGE;
  if (totalTripsPages < 1) totalTripsPages = 1;
  if (currentTripsPage >= totalTripsPages) currentTripsPage = totalTripsPages - 1;
  cachedTripsPageIndex = currentTripsPage;

  File tripsDir = SD.open("/Trips");
  if (!tripsDir) {
    free(tripEntries);
    tripsCacheValid = true;
    return;
  }

  int entryCount = 0;
  File entry = tripsDir.openNextFile();
  while (entry && entryCount < cachedTripCount) {
    if (entry.isDirectory()) {
      const char* dirName = entry.name();
      strncpy(tripEntries[entryCount].dirName, dirName, TRIP_DIR_MAX_LEN - 1);
      tripEntries[entryCount].dirName[TRIP_DIR_MAX_LEN - 1] = '\0';
      uint64_t createdAt = 0;
      readTripListMetadata(dirName, tripEntries[entryCount].displayName, TRIP_NAME_MAX_LEN, &createdAt);
      tripEntries[entryCount].createdAt = createdAt;
      if (tripEntries[entryCount].displayName[0] == '\0') {
        strncpy(tripEntries[entryCount].displayName, dirName, TRIP_NAME_MAX_LEN - 1);
        tripEntries[entryCount].displayName[TRIP_NAME_MAX_LEN - 1] = '\0';
      }
      entryCount++;
    }
    entry.close();
    entry = tripsDir.openNextFile();
  }
  tripsDir.close();

  if (entryCount < cachedTripCount) cachedTripCount = entryCount;
  totalTripsPages = (cachedTripCount + TRIPS_PER_PAGE - 1) / TRIPS_PER_PAGE;
  if (totalTripsPages < 1) totalTripsPages = 1;
  if (currentTripsPage >= totalTripsPages) currentTripsPage = totalTripsPages - 1;
  cachedTripsPageIndex = currentTripsPage;

  if (cachedTripCount == 0) {
    free(tripEntries);
    selectedTripIndex = 0;
    tripsCacheValid = true;
    return;
  }

  qsort(tripEntries, cachedTripCount, sizeof(TripSortEntry), compareTripsByCreatedAtDesc);

  int startTripIndex = currentTripsPage * TRIPS_PER_PAGE;
  int endTripIndex = min(startTripIndex + TRIPS_PER_PAGE, cachedTripCount);

  for (int i = startTripIndex; i < endTripIndex; i++) {
    strncpy(cachedTrips[cachedTripsOnPage].dirName, tripEntries[i].dirName, TRIP_DIR_MAX_LEN - 1);
    cachedTrips[cachedTripsOnPage].dirName[TRIP_DIR_MAX_LEN - 1] = '\0';
    strncpy(cachedTrips[cachedTripsOnPage].displayName, tripEntries[i].displayName, TRIP_NAME_MAX_LEN - 1);
    cachedTrips[cachedTripsOnPage].displayName[TRIP_NAME_MAX_LEN - 1] = '\0';
    cachedTripsOnPage++;
  }
  free(tripEntries);

  int totalItemsOnPage = 1 + cachedTripsOnPage;
  if (selectedTripIndex >= totalItemsOnPage) selectedTripIndex = 0;

  tripsCacheValid = true;
}

void ensureTripsCache() {
  if (!tripsCacheValid || cachedTripsPageIndex != currentTripsPage) {
    refreshTripsCache();
  }
}

#endif // PAGE_TRIPS_H
