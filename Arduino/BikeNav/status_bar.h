#ifndef STATUS_BAR_H
#define STATUS_BAR_H

#include <Arduino.h>
#include <GxEPD2_BW.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <TinyGPS++.h>

// External references
extern GxEPD2_BW<GxEPD2_290_BS, GxEPD2_290_BS::HEIGHT> display;
extern U8G2_FOR_ADAFRUIT_GFX u8g2_display;
extern const int DISPLAY_WIDTH;
extern const int DISPLAY_HEIGHT;
extern TinyGPSPlus gps;
extern class BatteryManager batteryManager;
extern BLEServer* pServer;  // From ble_handler.h
extern bool bluetoothEnabled;

// External functions
extern LocalTime getLocalTime();
extern bool isBLEConnected();  // From page_settings.h

// GPS icon references (to be added to bitmaps.h)
extern const unsigned char ICON_GPS_ACTIVE[];
extern const unsigned char ICON_GPS_DISABLED[];
extern const unsigned char ICON_BT_CONNECTED_SMALL[];
extern const unsigned char ICON_BT_DISCONNECTED_SMALL[];

// Pin definitions
#define GPS_POWER_PIN 17

// === STATUS BAR CONFIGURATION ===
const int STATUS_BAR_HEIGHT = 16;
const int STATUS_BAR_ICON_SIZE = 13;
const int WEATHER_STATUS_BAR_EXTRA_HEIGHT = 25;
const int STATUS_BAR_REFRESH_DEBOUNCE_MS = 5000;  // Min 5s between auto-refreshes
const int USER_ACTIVITY_DEBOUNCE_MS = 1000;       // Wait 1s after user activity before auto-refresh

// === STATE TRACKING ===
struct StatusBarState {
  int lastDisplayedMinute = -1;
  float lastDisplayedBatteryPercent = -1.0;
  bool lastBLEConnected = false;
  bool lastBluetoothEnabled = true;
  bool lastGPSActive = false;
  bool lastChargingState = false;
  unsigned long lastRefreshTime = 0;
  unsigned long lastUserActivityTime = 0;  // Track last user interaction
  bool initialized = false;
};

StatusBarState statusBarState;

struct StatusBarExtras {
  bool showPageDots = false;
  int pageIndex = 0;
  int pageCount = 0;
  bool showUpdateAge = false;
  unsigned long lastUpdateMillis = 0;
  bool showTileTime = false;
  int tileOffsetSteps = 0;
  int tileStepMinutes = 5;
  bool tileBaseValid = false;
  int tileBaseMinutes = 0;
  bool showTimeline = false;
  int timelinePastCount = 0;
  int timelineFutureCount = 0;
  int timelineSelectedOffset = 0;
};

StatusBarExtras statusBarExtras;

bool statusBarHasExtras() {
  return statusBarExtras.showPageDots ||
         statusBarExtras.showUpdateAge ||
         statusBarExtras.showTileTime ||
         statusBarExtras.showTimeline;
}

// === HELPER FUNCTIONS ===

/**
 * @brief Check if GPS is active (powered on and has valid location)
 */
bool isGPSActive() {
  return (digitalRead(GPS_POWER_PIN) == HIGH) && gps.location.isValid();
}

/**
 * @brief Draw a small battery icon for the status bar
 * @param x X position (top-left corner)
 * @param y Y position (top-left corner)
 * @param percentage Battery percentage (0-100) for fill level
 * @param isCharging Whether battery is currently charging
 */
void drawSmallBatteryIcon(int x, int y, float percentage, bool isCharging) {
  const int width = 18;
  const int height = 10;  // Taller battery
  const int tipWidth = 2;
  const int tipHeight = 4;

  // Draw battery body outline
  display.drawRect(x, y, width, height, GxEPD_BLACK);

  // Draw battery tip (positive terminal)
  display.fillRect(x + width, y + (height - tipHeight) / 2, tipWidth, tipHeight, GxEPD_BLACK);

  // Draw fill level (with 2px gap between casing and content for better visibility)
  if (percentage > 0) {
    int fillWidth = (int)((width - 4) * (percentage / 100.0));  // 2px padding on each side
    if (fillWidth > 0) {
      display.fillRect(x + 2, y + 2, fillWidth, height - 4, GxEPD_BLACK);
    }
  }

  // Draw charging indicator (lightning bolt) if charging
  if (isCharging) {
    // Simple lightning bolt: 3 lines forming a zigzag
    int boltX = x + width / 2;
    int boltY = y + 2;  // Adjusted for 2px padding
    display.drawLine(boltX, boltY, boltX - 1, boltY + 2, GxEPD_WHITE);
    display.drawLine(boltX - 1, boltY + 2, boltX + 1, boltY + 2, GxEPD_WHITE);
    display.drawLine(boltX + 1, boltY + 2, boltX, boltY + 5, GxEPD_WHITE);
  }
}

/**
 * @brief Draw a small GPS icon (13x13)
 * @param x X position (top-left)
 * @param y Y position (top-left)
 * @param active Whether GPS has valid fix
 */
void drawGPSIcon(int x, int y, bool active) {
  if (active) {
    // Draw the bitmap icon
    display.drawBitmap(x, y, ICON_GPS_ACTIVE, STATUS_BAR_ICON_SIZE, STATUS_BAR_ICON_SIZE, GxEPD_BLACK);
  } else {
    display.drawBitmap(x, y, ICON_GPS_DISABLED, STATUS_BAR_ICON_SIZE, STATUS_BAR_ICON_SIZE, GxEPD_BLACK);
  }
}

/**
 * @brief Draw a small BLE icon (13x13)
 * @param x X position (top-left)
 * @param y Y position (top-left)
 * @param connected Whether BLE is connected
 */
void drawBLEIcon(int x, int y, bool connected) {
  const unsigned char* icon = connected ? ICON_BT_CONNECTED_SMALL : ICON_BT_DISCONNECTED_SMALL;
  display.drawBitmap(x, y, icon, STATUS_BAR_ICON_SIZE, STATUS_BAR_ICON_SIZE, GxEPD_BLACK);
}

void clearStatusBarExtras() {
  statusBarExtras = StatusBarExtras();
}

void setStatusBarPageDots(int currentPage, int totalPages) {
  statusBarExtras.showPageDots = (totalPages > 1);
  statusBarExtras.pageIndex = currentPage;
  statusBarExtras.pageCount = totalPages;
}

void clearStatusBarPageDots() {
  statusBarExtras.showPageDots = false;
  statusBarExtras.pageCount = 0;
  statusBarExtras.pageIndex = 0;
}

void setStatusBarUpdateAge(unsigned long lastUpdateMillis) {
  statusBarExtras.showUpdateAge = true;
  statusBarExtras.lastUpdateMillis = lastUpdateMillis;
}

void clearStatusBarUpdateAge() {
  statusBarExtras.showUpdateAge = false;
  statusBarExtras.lastUpdateMillis = 0;
}

void setStatusBarTileTime(int offsetSteps, int stepMinutes) {
  statusBarExtras.showTileTime = true;
  statusBarExtras.tileOffsetSteps = offsetSteps;
  statusBarExtras.tileStepMinutes = stepMinutes;
}

void clearStatusBarTileTime() {
  statusBarExtras.showTileTime = false;
  statusBarExtras.tileOffsetSteps = 0;
  statusBarExtras.tileStepMinutes = 5;
  statusBarExtras.tileBaseValid = false;
  statusBarExtras.tileBaseMinutes = 0;
}

void setStatusBarTileBaseTime(int baseMinutes, bool isValid) {
  statusBarExtras.tileBaseMinutes = baseMinutes;
  statusBarExtras.tileBaseValid = isValid;
}

void setStatusBarTimeline(int pastCount, int futureCount, int selectedOffset, int stepMinutes) {
  statusBarExtras.showTimeline = true;
  statusBarExtras.timelinePastCount = max(0, pastCount);
  statusBarExtras.timelineFutureCount = max(0, futureCount);
  statusBarExtras.timelineSelectedOffset = selectedOffset;
  statusBarExtras.tileStepMinutes = stepMinutes;
}

void clearStatusBarTimeline() {
  statusBarExtras.showTimeline = false;
  statusBarExtras.timelinePastCount = 0;
  statusBarExtras.timelineFutureCount = 0;
  statusBarExtras.timelineSelectedOffset = 0;
}

// === MAIN STATUS BAR FUNCTIONS ===

/**
 * @brief Initialize the status bar state tracking
 * Call this once during setup
 */
void initStatusBar() {
  statusBarState.lastDisplayedMinute = -1;
  statusBarState.lastDisplayedBatteryPercent = -1.0;
  statusBarState.lastBLEConnected = false;
  statusBarState.lastBluetoothEnabled = bluetoothEnabled;
  statusBarState.lastGPSActive = false;
  statusBarState.lastChargingState = false;
  statusBarState.lastRefreshTime = 0;
  statusBarState.lastUserActivityTime = 0;
  statusBarState.initialized = true;

  Serial.println("Status bar initialized");
}

// Forward declaration for notification system coordination
extern void markNotificationUserActivity();

/**
 * @brief Mark that user activity occurred (encoder scroll, button press)
 * This prevents status bar and notifications from auto-refreshing during active use
 * Call this from encoder ISR and button handlers
 */
void markUserActivity() {
  statusBarState.lastUserActivityTime = millis();
  // Also update notification system to coordinate rendering
  markNotificationUserActivity();
}

/**
 * @brief Draw the complete status bar
 * Shows: Battery icon + %, GPS icon, BLE icon, Time
 * Call this from page render functions
 */
void drawStatusBarInternal(bool drawSeparator) {
  int extrasHeight = statusBarHasExtras() ? WEATHER_STATUS_BAR_EXTRA_HEIGHT : 0;
  int statusBarY = DISPLAY_HEIGHT - STATUS_BAR_HEIGHT;
  int barTopY = statusBarY - extrasHeight;

  // Draw separator line at top of status bar area (including extras)
  if (drawSeparator) {
    display.drawLine(0, barTopY, DISPLAY_WIDTH, barTopY, GxEPD_BLACK);
  }

  // Setup text rendering
  u8g2_display.setFontMode(1);
  u8g2_display.setForegroundColor(GxEPD_BLACK);
  u8g2_display.setBackgroundColor(GxEPD_WHITE);
  u8g2_display.setFont(u8g2_font_helvB08_tf);  // Small bold font

  // === FAR LEFT: Battery Icon + Percentage ===
  float batteryPercent = batteryManager.getPercentage();
  bool isCharging = batteryManager.getIsCharging();

  // Calculate text baseline position first
  char percentStr[6];
  snprintf(percentStr, sizeof(percentStr), "%.0f%%", batteryPercent);
  int percentTextY = statusBarY + STATUS_BAR_HEIGHT - 2;

  // Center battery icon vertically with the text baseline
  // Text baseline is at percentTextY, so align battery icon to match
  int batteryIconY = percentTextY - 9;  // Position icon relative to text baseline (10px height, centered ~9px up)
  drawSmallBatteryIcon(1, batteryIconY, batteryPercent, isCharging);

  // Draw battery percentage right after icon
  u8g2_display.setCursor(23, percentTextY);  // 1 + 18 (width) + 2 (tip) + 2 (spacing)
  u8g2_display.print(percentStr);

  // === CENTER: GPS and BLE Icons ===
  int percentWidth = u8g2_display.getUTF8Width(percentStr);
  int iconsStartX = 23 + percentWidth + 3;  // After battery % + spacing

  // Align icons with text baseline (same as battery icon)
  int iconY = percentTextY - 11;  // 15px icon height, aligned with text baseline

  // GPS Icon
  bool gpsActive = isGPSActive();
  drawGPSIcon(iconsStartX, iconY, gpsActive);

  // BLE Icon
  bool bleConnected = bluetoothEnabled && isBLEConnected();
  if (bluetoothEnabled) {
    drawBLEIcon(iconsStartX + STATUS_BAR_ICON_SIZE + 2, iconY, bleConnected);
  }

  // === FAR RIGHT: Time (no separator line) ===
  char timeStr[8];
  if (gps.time.isValid() && gps.date.isValid() && !(gps.time.hour() == 0 && gps.time.minute() == 0 && gps.time.second() == 0)) {
    LocalTime localTime = getLocalTime();
    snprintf(timeStr, sizeof(timeStr), "%02d:%02d", localTime.hour, localTime.minute);
  } else {
    snprintf(timeStr, sizeof(timeStr), "--:--");
  }

  // Right-align time at the very edge
  int textWidth = u8g2_display.getUTF8Width(timeStr);
  int timeX = DISPLAY_WIDTH - textWidth - 1;  // 1px margin from right edge
  int timeY = statusBarY + STATUS_BAR_HEIGHT - 2;

  u8g2_display.setCursor(timeX, timeY);
  u8g2_display.print(timeStr);

  // Update state tracking after drawing
  if (gps.time.isValid() && gps.date.isValid()) {
    LocalTime localTime = getLocalTime();
    statusBarState.lastDisplayedMinute = localTime.minute;
  } else {
    statusBarState.lastDisplayedMinute = -1;
  }
  statusBarState.lastDisplayedBatteryPercent = batteryPercent;
  statusBarState.lastBLEConnected = bleConnected;
  statusBarState.lastBluetoothEnabled = bluetoothEnabled;
  statusBarState.lastGPSActive = gpsActive;
  statusBarState.lastChargingState = isCharging;
}

void drawStatusBar() {
  drawStatusBarInternal(true);
}

void drawStatusBarNoSeparator() {
  drawStatusBarInternal(false);
}

void drawStatusBarExtras() {
  int extrasHeight = WEATHER_STATUS_BAR_EXTRA_HEIGHT;
  if (extrasHeight <= 0) {
    return;
  }

  int extrasY = DISPLAY_HEIGHT - STATUS_BAR_HEIGHT - extrasHeight;
  if (extrasY < 0) {
    extrasY = 0;
  }

  display.fillRect(0, extrasY, DISPLAY_WIDTH, extrasHeight, GxEPD_WHITE);

  u8g2_display.setFontMode(1);
  u8g2_display.setForegroundColor(GxEPD_BLACK);
  u8g2_display.setBackgroundColor(GxEPD_WHITE);
  u8g2_display.setFont(u8g2_font_helvB08_tf);

  int line1Y = extrasY + 12;

  char updateStr[8];
  updateStr[0] = '\0';
  if (statusBarExtras.showUpdateAge && statusBarExtras.lastUpdateMillis > 0) {
    unsigned long minutesAgo = (millis() - statusBarExtras.lastUpdateMillis) / 60000;
    if (minutesAgo == 0) {
      snprintf(updateStr, sizeof(updateStr), "Now");
    } else if (minutesAgo < 60) {
      snprintf(updateStr, sizeof(updateStr), "%lum", minutesAgo);
    } else {
      unsigned long hoursAgo = minutesAgo / 60;
      snprintf(updateStr, sizeof(updateStr), "%luh", hoursAgo);
    }
  }

  if (updateStr[0] != '\0') {
    u8g2_display.setCursor(2, line1Y);
    u8g2_display.print(updateStr);
  }

  char tileStr[8];
  tileStr[0] = '\0';
  if (statusBarExtras.showTileTime) {
    int stepMinutes = statusBarExtras.tileStepMinutes > 0
                        ? statusBarExtras.tileStepMinutes
                        : 5;
    if (statusBarExtras.tileBaseValid &&
        statusBarExtras.tileBaseMinutes >= 0 &&
        statusBarExtras.tileBaseMinutes < 24 * 60) {
      int tileMinutes = statusBarExtras.tileBaseMinutes;
      while (tileMinutes < 0) tileMinutes += 24 * 60;
      while (tileMinutes >= 24 * 60) tileMinutes -= 24 * 60;
      int tileHour = tileMinutes / 60;
      int tileMinute = tileMinutes % 60;
      snprintf(tileStr, sizeof(tileStr), "%02d:%02d", tileHour, tileMinute);
    } else if (gps.time.isValid() && gps.date.isValid() &&
               !(gps.time.hour() == 0 && gps.time.minute() == 0 && gps.time.second() == 0)) {
      LocalTime localTime = getLocalTime();
      int nowMinutes = localTime.hour * 60 + localTime.minute;
      int ageMinutes = 0;
      if (statusBarExtras.lastUpdateMillis > 0) {
        ageMinutes = (millis() - statusBarExtras.lastUpdateMillis) / 60000;
      }
      int baseMinutes = nowMinutes - ageMinutes;
      while (baseMinutes < 0) baseMinutes += 24 * 60;
      while (baseMinutes >= 24 * 60) baseMinutes -= 24 * 60;
      baseMinutes -= (baseMinutes % stepMinutes);
      int offsetMinutes = statusBarExtras.tileOffsetSteps * stepMinutes;
      int tileMinutes = baseMinutes + offsetMinutes;
      while (tileMinutes < 0) tileMinutes += 24 * 60;
      while (tileMinutes >= 24 * 60) tileMinutes -= 24 * 60;
      int tileHour = tileMinutes / 60;
      int tileMinute = tileMinutes % 60;
      snprintf(tileStr, sizeof(tileStr), "%02d:%02d", tileHour, tileMinute);
    } else {
      snprintf(tileStr, sizeof(tileStr), "--:--");
    }
  }

  if (tileStr[0] != '\0') {
    int tileWidth = u8g2_display.getUTF8Width(tileStr);
    int tileX = DISPLAY_WIDTH - tileWidth - 2;
    u8g2_display.setCursor(tileX, line1Y);
    u8g2_display.print(tileStr);
  }

  if (statusBarExtras.showPageDots && statusBarExtras.pageCount > 1) {
    const int dotRadius = 3;
    const int dotSpacing = 10;
    int totalWidth = (statusBarExtras.pageCount * dotRadius * 2) +
                     ((statusBarExtras.pageCount - 1) * dotSpacing);
    int startX = (DISPLAY_WIDTH - totalWidth) / 2;
    int dotY = line1Y - 4;
    for (int i = 0; i < statusBarExtras.pageCount; i++) {
      int dotX = startX + (i * (dotRadius * 2 + dotSpacing)) + dotRadius;
      if (i == statusBarExtras.pageIndex) {
        display.fillCircle(dotX, dotY, dotRadius, GxEPD_BLACK);
      } else {
        display.drawCircle(dotX, dotY, dotRadius, GxEPD_BLACK);
      }
    }
  }

  if (statusBarExtras.showTimeline) {
    int totalBoxes = statusBarExtras.timelinePastCount + statusBarExtras.timelineFutureCount + 1;
    if (totalBoxes > 0) {
      const int gap = 2;
      const int minBoxWidth = 8;
      const int margin = 2;
      int maxWidth = DISPLAY_WIDTH - (margin * 2);
      int maxBoxes = (maxWidth + gap) / (minBoxWidth + gap);
      maxBoxes = max(1, maxBoxes);
      int visibleBoxes = min(totalBoxes, maxBoxes);
      int boxHeight = 8;

      int currentIndex = statusBarExtras.timelinePastCount;
      int selectedIndex = currentIndex + statusBarExtras.timelineSelectedOffset;
      selectedIndex = max(0, min(totalBoxes - 1, selectedIndex));

      int startIndex = 0;
      if (visibleBoxes < totalBoxes) {
        startIndex = selectedIndex - (visibleBoxes / 2);
        startIndex = max(0, min(totalBoxes - visibleBoxes, startIndex));
      }

      int boxWidth = (maxWidth - (visibleBoxes - 1) * gap) / visibleBoxes;
      boxWidth = max(boxWidth, minBoxWidth);
      int timelineWidth = (boxWidth * visibleBoxes) + (gap * (visibleBoxes - 1));
      int startX = margin + (maxWidth - timelineWidth) / 2;
      int y = extrasY + extrasHeight - (boxHeight + 1);

      for (int i = 0; i < visibleBoxes; i++) {
        int index = startIndex + i;
        int x = startX + i * (boxWidth + gap);
        bool isFuture = index > currentIndex;

        if (isFuture) {
          for (int px = x; px < x + boxWidth; px += 2) {
            display.drawPixel(px, y, GxEPD_BLACK);
            display.drawPixel(px, y + boxHeight, GxEPD_BLACK);
          }
          for (int py = y; py <= y + boxHeight; py += 2) {
            display.drawPixel(x, py, GxEPD_BLACK);
            display.drawPixel(x + boxWidth, py, GxEPD_BLACK);
          }
        } else {
          display.drawRect(x, y, boxWidth + 1, boxHeight + 1, GxEPD_BLACK);
        }

        if (index == currentIndex) {
          display.drawRect(x + 1, y + 1, boxWidth - 1, boxHeight - 1, GxEPD_BLACK);
        }

        if (index == selectedIndex) {
          display.fillRect(x + 2, y + 2, boxWidth - 3, boxHeight - 3, GxEPD_BLACK);
          if (index == currentIndex) {
            display.fillRect(x + (boxWidth / 2), y + (boxHeight / 2), 2, 2, GxEPD_WHITE);
          }
        }
      }
    }
  }
}

/**
 * @brief Check if status bar needs refresh and perform partial update
 * Call this from page update functions or main loop
 * @param forceUpdate If true, bypass user activity check (use when page is already re-rendering)
 * Returns true if refresh was performed
 */
bool updateStatusBar(bool forceUpdate = false) {
  if (!statusBarState.initialized) {
    return false;
  }

  unsigned long currentTime = millis();

  // Check if enough time has passed since last refresh (debouncing)
  if (currentTime - statusBarState.lastRefreshTime < STATUS_BAR_REFRESH_DEBOUNCE_MS) {
    return false;
  }

  // PRIORITY CHECK: Don't auto-refresh during user activity (unless forced)
  // This keeps the UI responsive during scrolling/interaction
  if (!forceUpdate) {
    unsigned long timeSinceActivity = currentTime - statusBarState.lastUserActivityTime;
    if (timeSinceActivity < USER_ACTIVITY_DEBOUNCE_MS) {
      // User is actively interacting, skip auto-refresh
      return false;
    }
  }

  // Get current status
  int currentMinute = -1;
  if (gps.time.isValid() && gps.date.isValid() && !(gps.time.hour() == 0 && gps.time.minute() == 0 && gps.time.second() == 0)) {
    LocalTime localTime = getLocalTime();
    currentMinute = localTime.minute;
  }

  float currentBatteryPercent = batteryManager.getPercentage();
  bool currentBluetoothEnabled = bluetoothEnabled;
  bool currentBLEConnected = currentBluetoothEnabled && isBLEConnected();
  bool currentGPSActive = isGPSActive();
  bool currentChargingState = batteryManager.getIsCharging();

  // Check if any value has changed
  bool needsRefresh = false;

  if (currentMinute != statusBarState.lastDisplayedMinute) {
    needsRefresh = true;
    Serial.printf("Status bar: Time changed (%02d -> %02d)\n",
                  statusBarState.lastDisplayedMinute, currentMinute);
  }

  if (abs(currentBatteryPercent - statusBarState.lastDisplayedBatteryPercent) >= 1.0) {
    needsRefresh = true;
    Serial.printf("Status bar: Battery changed (%.0f%% -> %.0f%%)\n",
                  statusBarState.lastDisplayedBatteryPercent, currentBatteryPercent);
  }

  if (currentChargingState != statusBarState.lastChargingState) {
    needsRefresh = true;
    Serial.printf("Status bar: Charging state changed (%s -> %s)\n",
                  statusBarState.lastChargingState ? "charging" : "not charging",
                  currentChargingState ? "charging" : "not charging");
  }

  if (currentBLEConnected != statusBarState.lastBLEConnected) {
    needsRefresh = true;
    Serial.printf("Status bar: BLE %s\n", currentBLEConnected ? "connected" : "disconnected");
  }

  if (currentBluetoothEnabled != statusBarState.lastBluetoothEnabled) {
    needsRefresh = true;
    Serial.printf("Status bar: Bluetooth %s\n", currentBluetoothEnabled ? "enabled" : "disabled");
  }

  if (currentGPSActive != statusBarState.lastGPSActive) {
    needsRefresh = true;
    Serial.printf("Status bar: GPS %s\n", currentGPSActive ? "active" : "inactive");
  }

  // Perform partial refresh if needed
  if (needsRefresh) {
    Serial.println("Status bar: Performing partial refresh");

    int extrasHeight = statusBarHasExtras() ? WEATHER_STATUS_BAR_EXTRA_HEIGHT : 0;
    int totalHeight = STATUS_BAR_HEIGHT + extrasHeight;
    int statusY = DISPLAY_HEIGHT - totalHeight;
    display.setPartialWindow(0, statusY, DISPLAY_WIDTH, totalHeight);

    display.firstPage();
    do {
      // Clear status bar area
      display.fillRect(0, statusY, DISPLAY_WIDTH, totalHeight, GxEPD_WHITE);

      // Redraw status bar
      if (extrasHeight > 0) {
        drawStatusBarExtras();
      }
      drawStatusBar();

    } while (display.nextPage());

    statusBarState.lastRefreshTime = currentTime;
    return true;
  }

  return false;
}

#endif  // STATUS_BAR_H
