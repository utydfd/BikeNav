// page_settings.h
#ifndef PAGE_SETTINGS_H
#define PAGE_SETTINGS_H
#include <GxEPD2_BW.h>
#include <U8g2_for_Adafruit_GFX.h>
#include "notification_system.h"
#include "bitmaps.h"

extern GxEPD2_BW<GxEPD2_290_BS, GxEPD2_290_BS::HEIGHT> display;
extern U8G2_FOR_ADAFRUIT_GFX u8g2_display;
extern const int DISPLAY_WIDTH;
extern const int DISPLAY_HEIGHT;

// Forward declare SettingsPanelState enum (defined in BikeNav.ino)
enum SettingsPanelState {
  SETTINGS_CLOSED = 0,
  SETTINGS_NOTIFICATIONS = 1,
  SETTINGS_QUICK_SETTINGS = 2
};

// External references from BikeNav.ino
extern bool backlightEnabled;
extern bool gpsEnabled;
extern bool bluetoothEnabled;
extern SettingsPanelState settingsPanelState;

// External references for GPS and BLE
extern TinyGPSPlus gps;
extern BLEServer* pServer;
extern void startBLE();
extern void stopBLE();

// External references to battery manager
#include "battery_manager.h"

// External reference to LocalTime and getLocalTime from BikeNav.ino
extern LocalTime getLocalTime();

// --- NOTIFICATIONS PANEL STATE ---
int selectedNotificationIndex = 0;
int expandedNotificationIndex = -1;  // -1 = no notification expanded

// --- QUICK SETTINGS STATE ---
const int TILE_COUNT = 5;  // Added time/date tile
enum QuickSettingsTile {
  TILE_BLUETOOTH = 0,
  TILE_BACKLIGHT = 1,
  TILE_GPS = 2,
  TILE_BATTERY = 3,
  TILE_DATETIME = 4
};
int selectedTileIndex = 0;

// GPS confirmation dialog state
bool showingGPSDialog = false;

// BT confirmation dialog state
bool showingBTDialog = false;

// --- HELPER FUNCTIONS ---

// Get GPS acquisition stage - use the correct logic from page_info.h
// 0=no data, 1=has time, 2=has date, 3=has location
int getGPSStage() {
  // Check location first (fully locked)
  if (gps.location.isValid() && gps.satellites.value() > 0) {
    return 3;  // GPS_STAGE_LOCATION
  }

  // Check date - valid if year >= 2025 (not default 2000)
  bool dateAcquired = false;
  if (gps.date.isValid() && gps.date.year() >= 2025) {
    dateAcquired = true;
  }

  // Check time - valid if not default 00:00:00
  bool timeAcquired = false;
  if (gps.time.isValid()) {
    // Time is acquired if it's not all zeros
    if (!(gps.time.hour() == 0 && gps.time.minute() == 0 && gps.time.second() == 0)) {
      timeAcquired = true;
    }
  }

  // Return appropriate stage
  if (dateAcquired) {
    return 2;  // GPS_STAGE_DATE
  } else if (timeAcquired) {
    return 1;  // GPS_STAGE_TIME
  } else {
    return 0;  // GPS_STAGE_NO_DATA
  }
}

// Check if BLE is connected
bool isBLEConnected() {
  if (pServer == nullptr) {
    return false;
  }
  return pServer->getConnectedCount() > 0;
}

// External reference to drawSegmentedProgressBar from page_info.h
extern void drawSegmentedProgressBar(int x, int y, int width, int height, int segments, int filled);

// --- RENDER FUNCTIONS ---

// Render a single notification card
void renderNotificationCard(int y, Notification* notif, bool selected, bool expanded) {
  const int cardX = 2;
  const int cardWidth = DISPLAY_WIDTH - 4;
  const int cardHeight = expanded ? 100 : 50;  // 2x height when expanded

  if (selected) {
    // Selected: black background
    display.fillRect(cardX, y, cardWidth, cardHeight, GxEPD_BLACK);
    display.drawRect(cardX, y, cardWidth, cardHeight, GxEPD_WHITE);

    u8g2_display.setForegroundColor(GxEPD_WHITE);
    u8g2_display.setBackgroundColor(GxEPD_BLACK);
  } else {
    // Unselected: white background with border
    display.fillRect(cardX, y, cardWidth, cardHeight, GxEPD_WHITE);
    display.drawRect(cardX, y, cardWidth, cardHeight, GxEPD_BLACK);

    u8g2_display.setForegroundColor(GxEPD_BLACK);
    u8g2_display.setBackgroundColor(GxEPD_WHITE);
  }

  // Draw icon (39x39) - always in same position
  const int iconX = cardX + 4;
  const int iconY = y + 5;  // Fixed position from top, doesn't move when expanded
  const int iconBottomY = iconY + 39;  // Y position where icon ends

  if (notif->icon != nullptr) {
    if (selected) {
      display.drawBitmap(iconX, iconY, notif->icon, 39, 39, GxEPD_WHITE);
    } else {
      display.drawBitmap(iconX, iconY, notif->icon, 39, 39, GxEPD_BLACK);
    }
  }

  // Draw text
  const int textXRight = cardX + 47;  // Text starts to the right of icon
  const int textXFull = cardX + 4;    // Text starts from left edge (under icon)
  int textStartY = y + 16;

  u8g2_display.setFont(u8g2_font_helvB08_tf);
  u8g2_display.setCursor(textXRight, textStartY);
  u8g2_display.print(notif->heading);

  u8g2_display.setFont(u8g2_font_profont10_tf);

  if (expanded) {
    // Expanded view: Show full text with word wrapping
    // First 3 lines to the right of icon, then full width below
    const int maxCharsRight = 12;   // Chars per line next to icon
    const int maxCharsFull = 19;    // Chars per line below icon (full width)
    const int lineHeight = 11;
    const int linesNextToIcon = 3;  // Number of lines to render next to icon
    int currentY = textStartY + 12;

    // Combine line1 and line2 into one string
    String fullText = String(notif->line1);
    if (strlen(notif->line2) > 0) {
      fullText += " ";
      fullText += String(notif->line2);
    }

    int textIndex = 0;
    int lineCount = 0;
    while (textIndex < fullText.length() && currentY < y + cardHeight - 6) {
      // First 4 lines next to icon, then full width
      bool nextToIcon = (lineCount < linesNextToIcon);
      int maxChars = nextToIcon ? maxCharsRight : maxCharsFull;
      int textX = nextToIcon ? textXRight : textXFull;

      // Extract segment for this line
      String segment = fullText.substring(textIndex, min(textIndex + maxChars, (int)fullText.length()));
      u8g2_display.setCursor(textX, currentY);
      u8g2_display.print(segment);

      textIndex += maxChars;
      currentY += lineHeight;
      lineCount++;
    }
  } else {
    // Normal view: Two lines next to icon only
    u8g2_display.setCursor(textXRight, textStartY + 12);
    u8g2_display.print(notif->line1);

    u8g2_display.setCursor(textXRight, textStartY + 22);
    u8g2_display.print(notif->line2);
  }
}

// Render notifications panel
void renderNotificationsPanel() {
  int count = getNotificationCount();

  if (count == 0) {
    // Empty state
    u8g2_display.setFontMode(1);
    u8g2_display.setForegroundColor(GxEPD_BLACK);
    u8g2_display.setBackgroundColor(GxEPD_WHITE);
    u8g2_display.setFont(u8g2_font_helvB10_tf);

    u8g2_display.setCursor(10, DISPLAY_HEIGHT / 2 - 20);
    u8g2_display.print("No notifications");

    u8g2_display.setFont(u8g2_font_profont10_tf);
    u8g2_display.setCursor(10, DISPLAY_HEIGHT / 2 + 5);
    u8g2_display.print("Press Settings to");
    u8g2_display.setCursor(10, DISPLAY_HEIGHT / 2 + 20);
    u8g2_display.print("show Quick Settings");
  } else {
    // Render scrollable list of notifications
    // When a notification is expanded, we need to adjust spacing
    int yPos = 2;
    int visibleNotifications = 0;

    // Start rendering from selected notification and expand outward
    for (int i = 0; i < count && yPos < DISPLAY_HEIGHT - 20; i++) {
      int notifIndex = i;
      bool selected = (notifIndex == selectedNotificationIndex);
      bool expanded = (notifIndex == expandedNotificationIndex);

      // Calculate card height
      int cardHeight = expanded ? 100 : 50;
      int cardSpacing = 4;

      // Stop if card won't fit
      if (yPos + cardHeight > DISPLAY_HEIGHT - 20) {
        break;
      }

      renderNotificationCard(yPos, &notificationQueue[notifIndex], selected, expanded);
      yPos += cardHeight + cardSpacing;
      visibleNotifications++;
    }

    // Draw count at bottom
    u8g2_display.setFontMode(1);
    u8g2_display.setForegroundColor(GxEPD_BLACK);
    u8g2_display.setBackgroundColor(GxEPD_WHITE);
    u8g2_display.setFont(u8g2_font_profont10_tf);

    char countStr[16];
    snprintf(countStr, sizeof(countStr), "%d/%d", selectedNotificationIndex + 1, count);
    int textWidth = u8g2_display.getUTF8Width(countStr);
    u8g2_display.setCursor(DISPLAY_WIDTH - textWidth - 4, DISPLAY_HEIGHT - 4);
    u8g2_display.print(countStr);
  }
}

// Render live notifications section (for quick settings)
void renderLiveNotifications() {
  int liveCount = getLiveNotificationCount();
  if (liveCount == 0) {
    return;
  }

  // Draw live notifications at top with visual separation
  int yPos = 2;
  for (int i = 0; i < notificationCount && yPos < 120; i++) {
    if (notificationQueue[i].type == NOTIFICATION_LIVE) {
      renderNotificationCard(yPos, &notificationQueue[i], false, false);  // Not expanded in quick settings
      yPos += 54;
    }
  }

  // Draw separator line
  display.drawLine(0, yPos + 2, DISPLAY_WIDTH, yPos + 2, GxEPD_BLACK);
  display.drawLine(0, yPos + 3, DISPLAY_WIDTH, yPos + 3, GxEPD_BLACK);
}

// Render a tile
void drawTile(int x, int y, int width, int height, const char* label, const char* state, bool selected) {
  // Draw border
  if (selected) {
    display.fillRect(x, y, width, height, GxEPD_BLACK);
    display.fillRect(x + 2, y + 2, width - 4, height - 4, GxEPD_WHITE);
    display.drawRect(x + 2, y + 2, width - 4, height - 4, GxEPD_BLACK);
  } else {
    display.drawRect(x, y, width, height, GxEPD_BLACK);
  }

  u8g2_display.setFontMode(1);
  u8g2_display.setForegroundColor(GxEPD_BLACK);
  u8g2_display.setBackgroundColor(GxEPD_WHITE);

  // Label
  u8g2_display.setFont(u8g2_font_helvB08_tf);
  u8g2_display.setCursor(x + 6, y + 14);
  u8g2_display.print(label);

  // State
  u8g2_display.setFont(u8g2_font_profont10_tf);
  u8g2_display.setCursor(x + 6, y + height - 6);
  u8g2_display.print(state);
}

// Render a tile with an extra status line between label and state
void drawTileWithStatus(
  int x,
  int y,
  int width,
  int height,
  const char* label,
  const char* status,
  const char* state,
  bool selected
) {
  if (selected) {
    display.fillRect(x, y, width, height, GxEPD_BLACK);
    display.fillRect(x + 2, y + 2, width - 4, height - 4, GxEPD_WHITE);
    display.drawRect(x + 2, y + 2, width - 4, height - 4, GxEPD_BLACK);
  } else {
    display.drawRect(x, y, width, height, GxEPD_BLACK);
  }

  u8g2_display.setFontMode(1);
  u8g2_display.setForegroundColor(GxEPD_BLACK);
  u8g2_display.setBackgroundColor(GxEPD_WHITE);

  u8g2_display.setFont(u8g2_font_helvB08_tf);
  u8g2_display.setCursor(x + 6, y + 14);
  u8g2_display.print(label);

  u8g2_display.setFont(u8g2_font_profont10_tf);
  u8g2_display.setCursor(x + 6, y + 22);
  u8g2_display.print(status);

  u8g2_display.setCursor(x + 6, y + height - 6);
  u8g2_display.print(state);
}

// Render quick settings grid
void renderQuickSettingsGrid() {
  // Calculate starting Y position (below live notifications if any)
  int liveCount = getLiveNotificationCount();
  int startY = (liveCount > 0) ? (liveCount * 54 + 6) : 2;

  // Adjust if not enough space
  if (startY > 60) {
    startY = 60;
  }

  const int tileMargin = 4;  // Normal spacing between tiles
  const int tileWidth = (DISPLAY_WIDTH - tileMargin) / 2;  // No left/right margins
  const int tileHeight = 40;

  // Row 1: Bluetooth and Backlight (side by side)
  int row1Y = startY;

  const char* btPowerState = bluetoothEnabled ? "ON" : "OFF";
  const char* btStatusState = bluetoothEnabled ? (isBLEConnected() ? "Connected" : "Waiting") : "Disabled";
  drawTileWithStatus(
    0,
    row1Y,
    tileWidth,
    tileHeight,
    "Bluetooth",
    btStatusState,
    btPowerState,
    selectedTileIndex == TILE_BLUETOOTH
  );

  const char* blState = backlightEnabled ? "ON" : "OFF";
  drawTile(tileWidth + tileMargin, row1Y, tileWidth, tileHeight, "Backlight", blState, selectedTileIndex == TILE_BACKLIGHT);

  // Row 2: GPS tile (full width, edge to edge)
  int row2Y = row1Y + tileHeight + tileMargin;
  int gpsHeight = 50;

  if (selectedTileIndex == TILE_GPS) {
    display.fillRect(0, row2Y, DISPLAY_WIDTH, gpsHeight, GxEPD_BLACK);
    display.fillRect(2, row2Y + 2, DISPLAY_WIDTH - 4, gpsHeight - 4, GxEPD_WHITE);
    display.drawRect(2, row2Y + 2, DISPLAY_WIDTH - 4, gpsHeight - 4, GxEPD_BLACK);
  } else {
    display.drawRect(0, row2Y, DISPLAY_WIDTH, gpsHeight, GxEPD_BLACK);
  }

  u8g2_display.setFontMode(1);
  u8g2_display.setForegroundColor(GxEPD_BLACK);
  u8g2_display.setBackgroundColor(GxEPD_WHITE);

  u8g2_display.setFont(u8g2_font_helvB08_tf);
  u8g2_display.setCursor(6, row2Y + 14);
  u8g2_display.print("GPS");

  if (gpsEnabled) {
    int stage = getGPSStage();
    if (stage == 3) {
      // GPS locked - show satellite count
      char satStr[16];
      snprintf(satStr, sizeof(satStr), "%d sats", gps.satellites.value());
      u8g2_display.setFont(u8g2_font_profont10_tf);
      u8g2_display.setCursor(6, row2Y + gpsHeight - 6);
      u8g2_display.print(satStr);
    } else {
      // GPS acquiring - show current stage with progress bar
      u8g2_display.setFont(u8g2_font_profont10_tf);
      u8g2_display.setCursor(6, row2Y + 26);

      // Show what we're currently acquiring
      const char* statusText = "";
      switch (stage) {
        case 0: statusText = "Acquiring time..."; break;
        case 1: statusText = "Acquiring date..."; break;
        case 2: statusText = "Acquiring location..."; break;
      }
      u8g2_display.print(statusText);

      // Show 3 segments: Time, Date, Location
      drawSegmentedProgressBar(6, row2Y + 32, 80, 10, 3, stage);
    }
  } else {
    u8g2_display.setFont(u8g2_font_profont10_tf);
    u8g2_display.setCursor(6, row2Y + gpsHeight - 6);
    u8g2_display.print("OFF");
  }

  // Row 3: Battery tile (full width, edge to edge)
  int row3Y = row2Y + gpsHeight + tileMargin;
  int batteryHeight = 60;

  if (selectedTileIndex == TILE_BATTERY) {
    display.fillRect(0, row3Y, DISPLAY_WIDTH, batteryHeight, GxEPD_BLACK);
    display.fillRect(2, row3Y + 2, DISPLAY_WIDTH - 4, batteryHeight - 4, GxEPD_WHITE);
    display.drawRect(2, row3Y + 2, DISPLAY_WIDTH - 4, batteryHeight - 4, GxEPD_BLACK);
  } else {
    display.drawRect(0, row3Y, DISPLAY_WIDTH, batteryHeight, GxEPD_BLACK);
  }

  u8g2_display.setFont(u8g2_font_helvB08_tf);
  u8g2_display.setCursor(6, row3Y + 14);
  u8g2_display.print("Battery");

  // Battery percentage (large) - get from battery manager
  float percentage = batteryManager.getPercentage();
  char percentStr[8];
  snprintf(percentStr, sizeof(percentStr), "%.0f%%", percentage);
  u8g2_display.setFont(u8g2_font_helvB12_tf);
  u8g2_display.setCursor(6, row3Y + 32);
  u8g2_display.print(percentStr);

  // Battery voltage - get from battery manager
  float voltage = batteryManager.getVoltage();
  char voltStr[16];
  snprintf(voltStr, sizeof(voltStr), "%.2fV", voltage);
  u8g2_display.setFont(u8g2_font_profont10_tf);
  u8g2_display.setCursor(60, row3Y + 32);
  u8g2_display.print(voltStr);

  // Horizontal battery icon
  int battIconX = 6;
  int battIconY = row3Y + 38;
  int battIconW = 60;
  int battIconH = 14;

  // Battery outline
  display.drawRect(battIconX, battIconY, battIconW, battIconH, GxEPD_BLACK);
  // Battery terminal
  display.fillRect(battIconX + battIconW, battIconY + 3, 3, battIconH - 6, GxEPD_BLACK);

  // Fill based on percentage
  int fillWidth = (int)((battIconW - 4) * (percentage / 100.0));
  if (fillWidth > 0) {
    display.fillRect(battIconX + 2, battIconY + 2, fillWidth, battIconH - 4, GxEPD_BLACK);
  }

  // Charging status
  u8g2_display.setCursor(battIconX + battIconW + 8, battIconY + 10);
  if (batteryManager.getIsCharging()) {
    u8g2_display.print("CHG");
  } else if (percentage >= 95.0f) {
    u8g2_display.print("FULL");
  } else {
    u8g2_display.print("---");
  }

  // Row 4: Date/Time tile (full width, edge to edge)
  int row4Y = row3Y + batteryHeight + tileMargin;
  int datetimeHeight = 50;

  if (selectedTileIndex == TILE_DATETIME) {
    display.fillRect(0, row4Y, DISPLAY_WIDTH, datetimeHeight, GxEPD_BLACK);
    display.fillRect(2, row4Y + 2, DISPLAY_WIDTH - 4, datetimeHeight - 4, GxEPD_WHITE);
    display.drawRect(2, row4Y + 2, DISPLAY_WIDTH - 4, datetimeHeight - 4, GxEPD_BLACK);
  } else {
    display.drawRect(0, row4Y, DISPLAY_WIDTH, datetimeHeight, GxEPD_BLACK);
  }

  u8g2_display.setFont(u8g2_font_helvB08_tf);
  u8g2_display.setCursor(6, row4Y + 14);
  u8g2_display.print("Clock");

  // Get local time
  LocalTime localTime = getLocalTime();

  // Date
  char dateStr[32];
  if (localTime.year > 0) {
    snprintf(dateStr, sizeof(dateStr), "%02d/%02d/%04d", localTime.day, localTime.month, localTime.year);
  } else {
    snprintf(dateStr, sizeof(dateStr), "--/--/----");
  }
  u8g2_display.setFont(u8g2_font_profont10_tf);
  u8g2_display.setCursor(6, row4Y + 30);
  u8g2_display.print(dateStr);

  // Time
  char timeStr[16];
  if (localTime.year > 0) {
    snprintf(timeStr, sizeof(timeStr), "%02d:%02d:%02d", localTime.hour, localTime.minute, localTime.second);
  } else {
    snprintf(timeStr, sizeof(timeStr), "--:--:--");
  }
  u8g2_display.setCursor(6, row4Y + datetimeHeight - 6);
  u8g2_display.print(timeStr);
}

// Render GPS confirmation dialog
void renderGPSDialog() {
  const int dialogW = 100;
  const int dialogH = 60;
  const int dialogX = (DISPLAY_WIDTH - dialogW) / 2;
  const int dialogY = (DISPLAY_HEIGHT - dialogH) / 2;

  // Dialog background with shadow
  display.fillRect(dialogX + 2, dialogY + 2, dialogW, dialogH, GxEPD_BLACK);
  display.fillRect(dialogX, dialogY, dialogW, dialogH, GxEPD_WHITE);
  display.drawRect(dialogX, dialogY, dialogW, dialogH, GxEPD_BLACK);

  u8g2_display.setFontMode(1);
  u8g2_display.setForegroundColor(GxEPD_BLACK);
  u8g2_display.setBackgroundColor(GxEPD_WHITE);

  u8g2_display.setFont(u8g2_font_helvB08_tf);
  u8g2_display.setCursor(dialogX + 10, dialogY + 16);
  u8g2_display.print("Turn off GPS?");

  u8g2_display.setFont(u8g2_font_profont10_tf);
  u8g2_display.setCursor(dialogX + 10, dialogY + 32);
  u8g2_display.print("Press: Turn off");
  u8g2_display.setCursor(dialogX + 10, dialogY + 44);
  u8g2_display.print("Back: Cancel");
}

// Render BT confirmation dialog
void renderBTDialog() {
  const int dialogW = 100;
  const int dialogH = 60;
  const int dialogX = (DISPLAY_WIDTH - dialogW) / 2;
  const int dialogY = (DISPLAY_HEIGHT - dialogH) / 2;

  // Dialog background with shadow
  display.fillRect(dialogX + 2, dialogY + 2, dialogW, dialogH, GxEPD_BLACK);
  display.fillRect(dialogX, dialogY, dialogW, dialogH, GxEPD_WHITE);
  display.drawRect(dialogX, dialogY, dialogW, dialogH, GxEPD_BLACK);

  u8g2_display.setFontMode(1);
  u8g2_display.setForegroundColor(GxEPD_BLACK);
  u8g2_display.setBackgroundColor(GxEPD_WHITE);

  u8g2_display.setFont(u8g2_font_helvB08_tf);
  u8g2_display.setCursor(dialogX + 8, dialogY + 16);
  u8g2_display.print("Disconnect BT?");

  u8g2_display.setFont(u8g2_font_profont10_tf);
  u8g2_display.setCursor(dialogX + 10, dialogY + 32);
  u8g2_display.print("Press: Turn off");
  u8g2_display.setCursor(dialogX + 10, dialogY + 44);
  u8g2_display.print("Back: Cancel");
}

// --- MAIN RENDER FUNCTION ---

void initSettingsPage() {
  selectedNotificationIndex = 0;
  expandedNotificationIndex = -1;  // Reset expansion when page is opened
  selectedTileIndex = 0;
  showingGPSDialog = false;
  showingBTDialog = false;
}

void renderSettingsPage() {
  display.setPartialWindow(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
  display.firstPage();

  do {
    display.fillScreen(GxEPD_WHITE);

    if (settingsPanelState == SETTINGS_NOTIFICATIONS) {
      // Render notifications panel
      renderNotificationsPanel();
    } else if (settingsPanelState == SETTINGS_QUICK_SETTINGS) {
      // Render live notifications (if any)
      renderLiveNotifications();

      // Render quick settings grid
      renderQuickSettingsGrid();
    }

    // Render dialogs on top if showing
    if (showingGPSDialog) {
      renderGPSDialog();
    } else if (showingBTDialog) {
      renderBTDialog();
    }

  } while (display.nextPage());
}

// --- UPDATE FUNCTION (for animations) ---

void updateSettingsPage() {
  // Update GPS progress bar when stage changes (not time-based animation)
  // No need for constant re-rendering - progress bar is static based on GPS stage
  // Only re-render if there's an actual change in GPS stage

  static int lastGPSStage = -1;
  static bool lastBleConnected = false;
  static bool lastBluetoothEnabled = false;
  if (settingsPanelState == SETTINGS_QUICK_SETTINGS && gpsEnabled) {
    int currentStage = getGPSStage();
    if (currentStage != lastGPSStage) {
      lastGPSStage = currentStage;
      renderSettingsPage();
    }
  } else {
    lastGPSStage = -1;  // Reset when not on quick settings
  }

  if (settingsPanelState == SETTINGS_QUICK_SETTINGS) {
    bool bleConnected = isBLEConnected();
    if (bleConnected != lastBleConnected || bluetoothEnabled != lastBluetoothEnabled) {
      lastBleConnected = bleConnected;
      lastBluetoothEnabled = bluetoothEnabled;
      renderSettingsPage();
    }
  } else {
    lastBleConnected = false;
    lastBluetoothEnabled = bluetoothEnabled;
  }

  // No need to constantly re-render dialogs - they're static now
}

// --- INPUT HANDLERS ---

// Check if back button should be handled by settings page (returns true if handled)
bool handleSettingsBack() {
  // If a dialog is showing, dismiss it
  if (showingGPSDialog || showingBTDialog) {
    showingGPSDialog = false;
    showingBTDialog = false;
    renderSettingsPage();
    return true;  // Handled - don't propagate
  }
  return false;  // Not handled - let BikeNav.ino handle it
}

void handleSettingsEncoder(int delta) {
  // Cancel dialogs on encoder movement
  if (showingGPSDialog || showingBTDialog) {
    showingGPSDialog = false;
    showingBTDialog = false;
    renderSettingsPage();
    return;
  }

  if (settingsPanelState == SETTINGS_NOTIFICATIONS) {
    // Scroll through notifications
    int count = getNotificationCount();
    if (count > 0) {
      // Collapse expanded notification when scrolling away
      expandedNotificationIndex = -1;

      selectedNotificationIndex += delta;

      if (selectedNotificationIndex < 0) {
        selectedNotificationIndex = count - 1;
      } else if (selectedNotificationIndex >= count) {
        selectedNotificationIndex = 0;
      }

      renderSettingsPage();
    }
  } else if (settingsPanelState == SETTINGS_QUICK_SETTINGS) {
    int step = 0;
    if (delta > 0) {
      step = 1;
    } else if (delta < 0) {
      step = -1;
    } else {
      return;
    }

    // Navigate through tiles
    selectedTileIndex += step;

    if (selectedTileIndex < 0) {
      selectedTileIndex = TILE_COUNT - 1;
    } else if (selectedTileIndex >= TILE_COUNT) {
      selectedTileIndex = 0;
    }

    renderSettingsPage();
  }
}

void handleSettingsButton() {
  // Handle button press based on context

  // GPS dialog - single press to confirm
  if (showingGPSDialog) {
    // Turn off GPS
    gpsEnabled = false;
    digitalWrite(17, LOW);
    showingGPSDialog = false;
    renderSettingsPage();
    return;
  }

  // BT dialog - single press to confirm
  if (showingBTDialog) {
    // Turn off Bluetooth
    bluetoothEnabled = false;
    stopBLE();
    showingBTDialog = false;
    renderSettingsPage();
    return;
  }

  // Normal button handling
  if (settingsPanelState == SETTINGS_NOTIFICATIONS) {
    // Toggle expansion of selected notification
    int count = getNotificationCount();
    if (count > 0) {
      if (expandedNotificationIndex == selectedNotificationIndex) {
        // Already expanded - collapse it
        expandedNotificationIndex = -1;
      } else {
        // Expand this notification
        expandedNotificationIndex = selectedNotificationIndex;
      }

      renderSettingsPage();
    }
  } else if (settingsPanelState == SETTINGS_QUICK_SETTINGS) {
    // Activate selected tile
    switch (selectedTileIndex) {
      case TILE_BLUETOOTH:
        if (bluetoothEnabled && isBLEConnected()) {
          // Show confirmation dialog
          showingBTDialog = true;
        } else {
          // Toggle immediately
          bluetoothEnabled = !bluetoothEnabled;
          if (bluetoothEnabled) {
            startBLE();
          } else {
            stopBLE();
          }
        }
        renderSettingsPage();
        break;

      case TILE_BACKLIGHT:
        // Toggle backlight
        backlightEnabled = !backlightEnabled;
        digitalWrite(38, backlightEnabled ? HIGH : LOW);
        renderSettingsPage();
        break;

      case TILE_GPS:
        if (gpsEnabled) {
          // Show confirmation dialog to turn off
          showingGPSDialog = true;
          renderSettingsPage();
        } else {
          // Turn on immediately
          gpsEnabled = true;
          digitalWrite(17, HIGH);
          renderSettingsPage();
        }
        break;

      case TILE_BATTERY:
        // Battery is non-interactive (display only)
        break;

      case TILE_DATETIME:
        // Date/Time is non-interactive (display only)
        break;
    }
  }
}

void handleSettingsOptions() {
  // Options button handler - dismiss selected notification
  if (settingsPanelState == SETTINGS_NOTIFICATIONS) {
    int count = getNotificationCount();
    if (count > 0) {
      // Dismiss the selected notification
      dismissNotification(selectedNotificationIndex);

      // Collapse expansion state
      expandedNotificationIndex = -1;

      // Adjust selection if needed
      if (selectedNotificationIndex >= getNotificationCount() && selectedNotificationIndex > 0) {
        selectedNotificationIndex--;
      }

      renderSettingsPage();
    }
  }
  // No action for quick settings panel
}

#endif // PAGE_SETTINGS_H
