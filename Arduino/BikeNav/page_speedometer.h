// page_speedometer.h
#ifndef PAGE_SPEEDOMETER_H
#define PAGE_SPEEDOMETER_H
#include <GxEPD2_BW.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <TinyGPS++.h>
#include "notification_system.h"
#include "status_bar.h"

extern GxEPD2_BW<GxEPD2_290_BS, GxEPD2_290_BS::HEIGHT> display;
extern U8G2_FOR_ADAFRUIT_GFX u8g2_display;
extern const int DISPLAY_WIDTH;
extern const int DISPLAY_HEIGHT;
extern TinyGPSPlus gps;
extern bool gpsValid;
extern const unsigned char ICON_SPEEDOMETER[];

// Speedometer state
struct SpeedometerState {
  float currentSpeed;      // km/h
  float maxSpeed;          // km/h
  float averageSpeed;      // km/h (calculated from distance / time)
  float totalDistance;     // km
  unsigned long tripStartTime;
  unsigned long lastUpdateTime;
  bool tripActive;
  bool showGPSDialog;
  bool showResetDialog;
};

SpeedometerState speedoState = {
  0.0,                     // currentSpeed
  0.0,                     // maxSpeed
  0.0,                     // averageSpeed
  0.0,                     // totalDistance
  0,                       // tripStartTime
  0,                       // lastUpdateTime
  false,                   // tripActive
  false,                   // showGPSDialog
  false                    // showResetDialog
};

const int SPEEDOMETER_SPLIT_HEIGHT = 40;
bool speedometerSplitEnabled = false;

void initSpeedometerPage() {
  // Check if GPS is valid, if not show dialog
  if (!gpsValid) {
    speedoState.showGPSDialog = true;
  }

  // Start trip if GPS is valid
  if (gpsValid && !speedoState.tripActive) {
    speedoState.tripStartTime = millis();
    speedoState.lastUpdateTime = millis();
    speedoState.tripActive = true;
  }
}

void drawGPSDialog() {
  // Dialog dimensions - larger to fit text
  int dialogWidth = 110;
  int dialogHeight = 100;
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

  // Title - use larger bold font and split into 2 lines
  u8g2_display.setFont(u8g2_font_helvB12_tf);
  const char* title1 = "GPS";
  const char* title2 = "Unavailable";
  int title1Width = u8g2_display.getUTF8Width(title1);
  int title2Width = u8g2_display.getUTF8Width(title2);
  u8g2_display.setCursor(dialogX + (dialogWidth - title1Width) / 2, dialogY + 16);
  u8g2_display.print(title1);
  u8g2_display.setCursor(dialogX + (dialogWidth - title2Width) / 2, dialogY + 30);
  u8g2_display.print(title2);

  // Message
  u8g2_display.setFont(u8g2_font_helvB08_tf);
  const char* msg1 = "No GPS signal";
  const char* msg2 = "detected";
  int msg1Width = u8g2_display.getUTF8Width(msg1);
  int msg2Width = u8g2_display.getUTF8Width(msg2);
  u8g2_display.setCursor(dialogX + (dialogWidth - msg1Width) / 2, dialogY + 50);
  u8g2_display.print(msg1);
  u8g2_display.setCursor(dialogX + (dialogWidth - msg2Width) / 2, dialogY + 62);
  u8g2_display.print(msg2);

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

void drawResetDialog() {
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
  const char* title = "Reset Trip?";
  int titleWidth = u8g2_display.getUTF8Width(title);
  u8g2_display.setCursor(dialogX + (dialogWidth - titleWidth) / 2, dialogY + 20);
  u8g2_display.print(title);

  // Message
  u8g2_display.setFont(u8g2_font_helvB08_tf);
  const char* msg1 = "Reset timer,";
  const char* msg2 = "distance and";
  const char* msg3 = "statistics?";
  int msg1Width = u8g2_display.getUTF8Width(msg1);
  int msg2Width = u8g2_display.getUTF8Width(msg2);
  int msg3Width = u8g2_display.getUTF8Width(msg3);
  u8g2_display.setCursor(dialogX + (dialogWidth - msg1Width) / 2, dialogY + 38);
  u8g2_display.print(msg1);
  u8g2_display.setCursor(dialogX + (dialogWidth - msg2Width) / 2, dialogY + 50);
  u8g2_display.print(msg2);
  u8g2_display.setCursor(dialogX + (dialogWidth - msg3Width) / 2, dialogY + 62);
  u8g2_display.print(msg3);

  // OK button (highlighted)
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

bool updateSpeedometerData() {
  bool gpsDialogChanged = false;

  // Update GPS data if available
  if (gpsValid && gps.speed.isValid()) {
    // Update current speed
    speedoState.currentSpeed = gps.speed.kmph();

    // Update max speed
    if (speedoState.currentSpeed > speedoState.maxSpeed) {
      speedoState.maxSpeed = speedoState.currentSpeed;
    }

    // Calculate distance traveled since last update
    if (speedoState.tripActive && speedoState.lastUpdateTime > 0) {
      unsigned long currentTime = millis();
      float elapsedHours = (currentTime - speedoState.lastUpdateTime) / 3600000.0; // Convert ms to hours
      float distanceDelta = speedoState.currentSpeed * elapsedHours; // km
      speedoState.totalDistance += distanceDelta;
      speedoState.lastUpdateTime = currentTime;
    } else if (speedoState.tripActive) {
      speedoState.lastUpdateTime = millis();
    }

    // Calculate average speed (distance / elapsed time in hours)
    if (speedoState.tripActive) {
      unsigned long elapsedMs = millis() - speedoState.tripStartTime;
      float elapsedHours = elapsedMs / 3600000.0; // Convert ms to hours
      if (elapsedHours > 0) {
        speedoState.averageSpeed = speedoState.totalDistance / elapsedHours;
      }
    }

    // Start trip if not active
    if (!speedoState.tripActive) {
      speedoState.tripStartTime = millis();
      speedoState.lastUpdateTime = millis();
      speedoState.tripActive = true;
    }

    // Hide GPS dialog if GPS is now valid
    if (speedoState.showGPSDialog) {
      speedoState.showGPSDialog = false;
      gpsDialogChanged = true;
    }
  } else if (!gpsValid && !speedoState.showGPSDialog) {
    // GPS was lost, show dialog
    speedoState.showGPSDialog = true;
    gpsDialogChanged = true;
  }

  return gpsDialogChanged;
}

void renderSpeedometerPage() {
  // Use partial window for fast, flicker-free updates
  display.setPartialWindow(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
  display.firstPage();

  do {
    display.fillScreen(GxEPD_WHITE);

    // Setup text rendering
    u8g2_display.setFontMode(1);
    u8g2_display.setForegroundColor(GxEPD_BLACK);
    u8g2_display.setBackgroundColor(GxEPD_WHITE);

    // Calculate available height (minus status bar)
    int contentHeight = DISPLAY_HEIGHT - STATUS_BAR_HEIGHT;

    // === CURRENT SPEED (HUGE font, top portion) ===
    int speedSectionHeight = contentHeight * 2 / 3;

    // Format speed with one decimal place
    char speedText[16];
    if (gpsValid && gps.speed.isValid()) {
      snprintf(speedText, sizeof(speedText), "%.1f", speedoState.currentSpeed);
    } else {
      snprintf(speedText, sizeof(speedText), "--");
    }

    // Use HUGE font for speed - logisoso42 is much larger
    u8g2_display.setFont(u8g2_font_fub42_tn);
    int speedWidth = u8g2_display.getUTF8Width(speedText);
    int speedX = (DISPLAY_WIDTH - speedWidth) / 2;
    int speedY = speedSectionHeight / 2 + 20;
    u8g2_display.setCursor(speedX, speedY);
    u8g2_display.print(speedText);

    // "km/h" label below speed
    u8g2_display.setFont(u8g2_font_helvB10_tf);
    const char* unitText = "km/h";
    int unitWidth = u8g2_display.getUTF8Width(unitText);
    u8g2_display.setCursor((DISPLAY_WIDTH - unitWidth) / 2, speedY + 20);
    u8g2_display.print(unitText);

    // === STATISTICS GRID (4 items in 2x2 grid) ===
    int gridTop = speedSectionHeight + 5;
    int gridHeight = contentHeight - speedSectionHeight - 10;
    int cellWidth = DISPLAY_WIDTH / 2;
    int cellHeight = gridHeight / 2;

    // Format trip duration
    char durationText[16];
    if (speedoState.tripActive) {
      unsigned long elapsed = (millis() - speedoState.tripStartTime) / 1000; // seconds
      unsigned long hours = elapsed / 3600;
      unsigned long minutes = (elapsed % 3600) / 60;
      unsigned long seconds = elapsed % 60;
      snprintf(durationText, sizeof(durationText), "%02lu:%02lu:%02lu", hours, minutes, seconds);
    } else {
      snprintf(durationText, sizeof(durationText), "00:00:00");
    }

    // Format distance
    char distanceText[16];
    snprintf(distanceText, sizeof(distanceText), "%.2f km", speedoState.totalDistance);

    // Format max speed
    char maxSpeedText[16];
    snprintf(maxSpeedText, sizeof(maxSpeedText), "%.1f", speedoState.maxSpeed);

    // Format average speed
    char avgSpeedText[16];
    snprintf(avgSpeedText, sizeof(avgSpeedText), "%.1f", speedoState.averageSpeed);

    u8g2_display.setFont(u8g2_font_helvB08_tf);

    // Left margin for text alignment
    const int leftMargin = 2;

    // Top-left: Duration
    int labelY = gridTop + 14;
    int valueY = gridTop + 28;
    u8g2_display.setCursor(leftMargin, labelY);
    u8g2_display.print("Duration");
    u8g2_display.setFont(u8g2_font_helvB10_tf);
    u8g2_display.setCursor(leftMargin, valueY);
    u8g2_display.print(durationText);

    // Top-right: Distance
    u8g2_display.setFont(u8g2_font_helvB08_tf);
    u8g2_display.setCursor(cellWidth + leftMargin, labelY);
    u8g2_display.print("Distance");
    u8g2_display.setFont(u8g2_font_helvB10_tf);
    u8g2_display.setCursor(cellWidth + leftMargin, valueY);
    u8g2_display.print(distanceText);

    // Bottom-left: Max Speed
    labelY = gridTop + cellHeight + 14;
    valueY = gridTop + cellHeight + 28;
    u8g2_display.setFont(u8g2_font_helvB08_tf);
    u8g2_display.setCursor(leftMargin, labelY);
    u8g2_display.print("Max Speed");
    u8g2_display.setFont(u8g2_font_helvB10_tf);
    u8g2_display.setCursor(leftMargin, valueY);
    u8g2_display.print(maxSpeedText);
    u8g2_display.setFont(u8g2_font_helvB08_tf);
    u8g2_display.print(" km/h");

    // Bottom-right: Avg Speed
    u8g2_display.setFont(u8g2_font_helvB08_tf);
    u8g2_display.setCursor(cellWidth + leftMargin, labelY);
    u8g2_display.print("Avg Speed");
    u8g2_display.setFont(u8g2_font_helvB10_tf);
    u8g2_display.setCursor(cellWidth + leftMargin, valueY);
    u8g2_display.print(avgSpeedText);
    u8g2_display.setFont(u8g2_font_helvB08_tf);
    u8g2_display.print(" km/h");

    // Draw grid borders - create 4 equal rectangles
    // Top border
    display.drawLine(0, gridTop, DISPLAY_WIDTH, gridTop, GxEPD_BLACK);
    // Middle horizontal line
    display.drawLine(0, gridTop + cellHeight, DISPLAY_WIDTH, gridTop + cellHeight, GxEPD_BLACK);
    // Bottom border
    display.drawLine(0, gridTop + gridHeight, DISPLAY_WIDTH, gridTop + gridHeight, GxEPD_BLACK);
    // Vertical line (full height from top to bottom)
    display.drawLine(cellWidth, gridTop, cellWidth, gridTop + gridHeight, GxEPD_BLACK);

    // Draw status bar
    drawStatusBar();

    // Draw GPS dialog on top if needed
    if (speedoState.showGPSDialog) {
      drawGPSDialog();
    }

    // Draw reset dialog on top if needed
    if (speedoState.showResetDialog) {
      drawResetDialog();
    }

    // Draw notification overlay
    drawNotificationOverlay();

  } while (display.nextPage());
}

void drawSpeedometerSplitOverlay() {
  display.fillRect(0, 0, DISPLAY_WIDTH, SPEEDOMETER_SPLIT_HEIGHT, GxEPD_WHITE);
  display.drawLine(0, SPEEDOMETER_SPLIT_HEIGHT - 1, DISPLAY_WIDTH, SPEEDOMETER_SPLIT_HEIGHT - 1, GxEPD_BLACK);

  u8g2_display.setFontMode(1);
  u8g2_display.setForegroundColor(GxEPD_BLACK);
  u8g2_display.setBackgroundColor(GxEPD_WHITE);

  char speedText[16];
  if (gpsValid && gps.speed.isValid()) {
    if (speedoState.currentSpeed >= 100.0f) {
      snprintf(speedText, sizeof(speedText), "%.0f", speedoState.currentSpeed);
    } else {
      snprintf(speedText, sizeof(speedText), "%.1f", speedoState.currentSpeed);
    }
  } else {
    snprintf(speedText, sizeof(speedText), "--");
  }

  const char* unitText = "km/h";
  const int unitPadding = 4;

  u8g2_display.setFont(u8g2_font_fub30_tn);
  int speedWidth = u8g2_display.getUTF8Width(speedText);
  u8g2_display.setFont(u8g2_font_helvB08_tf);
  int unitWidth = u8g2_display.getUTF8Width(unitText);

  int totalWidth = speedWidth + unitPadding + unitWidth;
  int speedX = (DISPLAY_WIDTH - totalWidth) / 2;
  int speedY = SPEEDOMETER_SPLIT_HEIGHT - 4;

  u8g2_display.setFont(u8g2_font_fub30_tn);
  u8g2_display.setCursor(speedX, speedY);
  u8g2_display.print(speedText);

  u8g2_display.setFont(u8g2_font_helvB08_tf);
  int unitX = speedX + speedWidth + unitPadding;
  int unitY = speedY;
  u8g2_display.setCursor(unitX, unitY);
  u8g2_display.print(unitText);

  if (!gpsValid || !gps.speed.isValid()) {
    u8g2_display.setFont(u8g2_font_helvB08_tf);
    u8g2_display.setCursor(2, 12);
    u8g2_display.print("No GPS");
  }
}

void renderSpeedometerSplitOverlay() {
  display.setPartialWindow(0, 0, DISPLAY_WIDTH, SPEEDOMETER_SPLIT_HEIGHT);
  display.firstPage();

  do {
    drawSpeedometerSplitOverlay();

    if (currentNotification.visible) {
      drawNotificationOverlay();
    }
  } while (display.nextPage());
}

void updateSpeedometerPage() {
  bool gpsDialogChanged = updateSpeedometerData();

  if (gpsValid && gps.speed.isValid()) {
    renderSpeedometerPage();
  } else if (!gpsValid && gpsDialogChanged) {
    renderSpeedometerPage();
  }

  // Let the status bar handle its own smart refresh
  // Note: Since speedometer re-renders frequently, this won't trigger often
  // but it's here for consistency and for when GPS is invalid
  updateStatusBar();
}

void resetTripData() {
  // Reset all trip statistics
  speedoState.maxSpeed = 0.0;
  speedoState.averageSpeed = 0.0;
  speedoState.totalDistance = 0.0;
  speedoState.tripStartTime = millis();
  speedoState.lastUpdateTime = millis();
  speedoState.tripActive = true;
}

void handleSpeedometerEncoder(int delta) {
  // No encoder functionality for speedometer
}

void handleSpeedometerButton() {
  // If reset dialog is showing, confirm reset
  if (speedoState.showResetDialog) {
    speedoState.showResetDialog = false;
    resetTripData();
    renderSpeedometerPage();
    return;
  }

  // If GPS dialog is showing, navigate to GPS info page
  if (speedoState.showGPSDialog) {
    speedoState.showGPSDialog = false;
    navigateToPage(PAGE_INFO);
    return;
  }

  speedometerSplitEnabled = !speedometerSplitEnabled;
  Serial.printf("Speedometer split overlay %s\n",
                speedometerSplitEnabled ? "enabled" : "disabled");

  showNotification(
      "Speedometer",
      "Map overlay",
      speedometerSplitEnabled ? "Enabled" : "Disabled",
      ICON_SPEEDOMETER,
      3000);
  renderSpeedometerPage();
}

void handleSpeedometerOptions() {
  // Show reset confirmation dialog
  speedoState.showResetDialog = true;
  renderSpeedometerPage();
}

bool handleSpeedometerBack() {
  // If reset dialog is showing, dismiss it
  if (speedoState.showResetDialog) {
    speedoState.showResetDialog = false;
    renderSpeedometerPage();
    return true; // Handled, don't propagate
  }

  // If GPS dialog is showing, navigate back to main menu
  if (speedoState.showGPSDialog) {
    speedoState.showGPSDialog = false;
    return false; // Let default back behavior proceed to main menu
  }
  return false; // Not handled, let default back behavior proceed
}

#endif
