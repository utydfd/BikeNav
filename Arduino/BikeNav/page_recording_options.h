#ifndef PAGE_RECORDING_OPTIONS_H
#define PAGE_RECORDING_OPTIONS_H

#include <GxEPD2_BW.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <TinyGPS++.h>
#include <time.h>
#include "notification_system.h"
#include "status_bar.h"
#include "bitmaps.h"

// External references
extern GxEPD2_BW<GxEPD2_290_BS, GxEPD2_290_BS::HEIGHT> display;
extern U8G2_FOR_ADAFRUIT_GFX u8g2_display;
extern const int DISPLAY_WIDTH;
extern const int DISPLAY_HEIGHT;
extern TinyGPSPlus gps;

// External from status_bar.h
extern void drawStatusBar();
extern void markUserActivity();
extern const unsigned char ICON_MUSIC_PAUSE[];
extern const int MUSIC_ICON_SIZE;
extern const unsigned char ICON_TRACKER[];

// External from page_recording.h
extern bool saveRecordingToGPX(const char* recordingName);
extern void stopRecording();
extern int recordedPointsCount;
extern bool isRecordingPaused;
extern unsigned long recordingStartTime;
extern long long recordingStartEpochMs;
extern bool isViewingRecording;
extern char viewedRecordingName[64];
extern char viewedRecordingDirName[64];
extern unsigned long viewedRecordingDurationSec;
extern long long viewedRecordingStartEpochMs;
extern bool deleteRecordingFromSD(const char* recordingDirName);
extern void clearViewingRecordingState();
extern bool computeRecordingStats(float* totalDistanceMeters,
                                  float* totalElevationGain,
                                  float* totalElevationLoss,
                                  float* minElevation,
                                  float* maxElevation);

void formatElapsedTime(char* out, size_t outSize, unsigned long totalSeconds) {
  unsigned long minutes = totalSeconds / 60;
  unsigned long seconds = totalSeconds % 60;
  snprintf(out, outSize, "%lu min %lu s", minutes, seconds);
}

// --- RECORDING FREQUENCY OPTIONS ---
enum RecordingFrequency {
  FREQ_ASAP,    // When location actually changes
  FREQ_3S,      // Every 3 seconds
  FREQ_5S,      // Every 5 seconds
  FREQ_7S,      // Every 7 seconds
  FREQ_10S,     // Every 10 seconds
  FREQ_15S,     // Every 15 seconds
  FREQ_30S      // Every 30 seconds
};

// Frequency options with display strings
const char* FREQ_LABELS[] = {"ASAP", "3s", "5s", "7s", "10s", "15s", "30s"};
const int FREQ_COUNT = 7;

// Recording options state
int selectedFrequencyIndex = 0;  // Default to ASAP
bool showFrequencyPopup = false;  // Whether frequency picker popup is shown
int selectedButton = 0;           // Selected button (0=Frequency, 1=Pause/Resume, 2=Save, 3=Discard)
int selectedViewButton = 0;       // Selected button when viewing (0=Back, 1=Delete)

/**
 * Get recording name based on current date
 * Format: "DD-MM-YYYY" or "DD-MM-YYYY_N" if multiple recordings on same day
 */
void getRecordingName(char* outName, size_t maxLen) {
  // Get current date from GPS
  if (gps.date.isValid() && gps.time.isValid()) {
    // Format: DD-MM-YYYY (European format)
    snprintf(outName, maxLen, "%02d-%02d-%04d",
             gps.date.day(), gps.date.month(), gps.date.year());
  } else {
    // Fallback if GPS date not available
    strcpy(outName, "Recording");
    return;
  }

  // Check if a recording with this name already exists
  // If so, append _N where N is the next available number
  char baseName[64];
  strncpy(baseName, outName, sizeof(baseName) - 1);
  baseName[sizeof(baseName) - 1] = '\0';

  int suffix = 1;
  while (true) {
    // Check if directory exists on SD card
    char checkPath[96];
    snprintf(checkPath, sizeof(checkPath), "/Recordings/%s", outName);

    File dir = SD.open(checkPath);
    if (!dir) {
      // Directory doesn't exist - this name is available
      break;
    }
    dir.close();

    // Name exists, try next suffix
    suffix++;
    snprintf(outName, maxLen, "%s_%d", baseName, suffix);
  }
}

/**
 * Draw vertical frequency selector for popup
 * Shows 5 items centered on current selection
 */
void drawVerticalFrequencySelector(int popupX, int popupY, int popupWidth) {
  const int ITEM_HEIGHT = 16;
  const int CENTER_Y_OFFSET = 80;  // Offset from popup top (more space from heading)
  const int centerX = popupX + popupWidth / 2;

  u8g2_display.setFontMode(1);
  u8g2_display.setForegroundColor(GxEPD_BLACK);
  u8g2_display.setBackgroundColor(GxEPD_WHITE);

  // Show 5 items: [prev2] [prev] [CURRENT] [next] [next2]
  for (int offset = -2; offset <= 2; offset++) {
    int index = selectedFrequencyIndex + offset;

    // Skip if out of bounds (no wrap-around)
    if (index < 0 || index >= FREQ_COUNT) {
      continue;
    }

    const char* freqStr = FREQ_LABELS[index];

    // Calculate position
    int itemY = popupY + CENTER_Y_OFFSET + (offset * ITEM_HEIGHT);

    if (offset == 0) {
      // Current selection - bold and highlighted
      u8g2_display.setFont(u8g2_font_helvB10_tf);

      // Draw highlight box
      int textWidth = u8g2_display.getUTF8Width(freqStr);
      int boxX = centerX - textWidth / 2 - 4;
      int boxY = itemY - 13;
      int boxWidth = textWidth + 8;
      int boxHeight = 16;
      display.drawRect(boxX, boxY, boxWidth, boxHeight, GxEPD_BLACK);

      // Center text
      u8g2_display.setCursor(centerX - textWidth / 2, itemY);
      u8g2_display.print(freqStr);
    } else {
      // Adjacent options - smaller and lighter
      u8g2_display.setFont(u8g2_font_helvR08_tf);
      int textWidth = u8g2_display.getUTF8Width(freqStr);
      u8g2_display.setCursor(centerX - textWidth / 2, itemY);
      u8g2_display.print(freqStr);
    }
  }
}

// --- PAGE INTERFACE FUNCTIONS ---

void initRecordingOptionsPage() {
  // Initialize with current settings
  showFrequencyPopup = false;
  selectedButton = 0;
  selectedViewButton = 0;
  Serial.println("Recording options page initialized");
}

void renderRecordingOptionsPage() {
  // Use fastest refresh (partial window)
  display.setPartialWindow(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
  display.firstPage();

  do {
    display.fillScreen(GxEPD_WHITE);

    // Setup text rendering
    u8g2_display.setFontMode(1);
    u8g2_display.setForegroundColor(GxEPD_BLACK);
    u8g2_display.setBackgroundColor(GxEPD_WHITE);

    // --- TITLE ---
    u8g2_display.setFont(u8g2_font_helvB12_tf);
    const char* title = "Options";
    int titleWidth = u8g2_display.getUTF8Width(title);
    u8g2_display.setCursor((DISPLAY_WIDTH - titleWidth) / 2, 20);
    u8g2_display.print(title);

    // --- RECORDING STATUS (PAUSED) ---
    int nameY = 45;
    if (!isViewingRecording && isRecordingPaused) {
      const int statusBoxX = 6;
      const int statusBoxY = 26;
      const int statusBoxWidth = DISPLAY_WIDTH - 12;
      const int statusBoxHeight = 18;

      display.fillRect(statusBoxX, statusBoxY, statusBoxWidth, statusBoxHeight, GxEPD_BLACK);
      u8g2_display.setForegroundColor(GxEPD_WHITE);
      u8g2_display.setBackgroundColor(GxEPD_BLACK);
      display.drawBitmap(statusBoxX + 4, statusBoxY + 1,
                         ICON_MUSIC_PAUSE, MUSIC_ICON_SIZE, MUSIC_ICON_SIZE, GxEPD_WHITE);

      u8g2_display.setFont(u8g2_font_helvB08_tf);
      u8g2_display.setCursor(statusBoxX + 4 + MUSIC_ICON_SIZE + 6, statusBoxY + 13);
      u8g2_display.print("Recording Paused");

      u8g2_display.setForegroundColor(GxEPD_BLACK);
      u8g2_display.setBackgroundColor(GxEPD_WHITE);
      nameY = statusBoxY + statusBoxHeight + 12;
    }

    // --- RECORDING NAME ---
    u8g2_display.setFont(u8g2_font_helvB08_tf);
    const char* nameLabel = "Name:";
    u8g2_display.setCursor(10, nameY);
    u8g2_display.print(nameLabel);

    // Get and display recording name
    char recordingName[64];
    if (isViewingRecording) {
      strncpy(recordingName, viewedRecordingName, sizeof(recordingName) - 1);
      recordingName[sizeof(recordingName) - 1] = '\0';
      if (recordingName[0] == '\0') {
        strncpy(recordingName, "Recording", sizeof(recordingName) - 1);
        recordingName[sizeof(recordingName) - 1] = '\0';
      }
    } else {
      getRecordingName(recordingName, sizeof(recordingName));
    }
    u8g2_display.setFont(u8g2_font_helvR08_tf);
    int nameLabelWidth = u8g2_display.getUTF8Width(nameLabel);
    u8g2_display.setCursor(10 + nameLabelWidth + 8, nameY);  // Added more spacing (8 instead of 4)
    u8g2_display.print(recordingName);

    // --- RECORDING STATS ---
    float totalDistanceMeters = 0.0f;
    float totalElevGain = 0.0f;
    float totalElevLoss = 0.0f;
    float minElevation = 0.0f;
    float maxElevation = 0.0f;
    bool hasElevation = computeRecordingStats(&totalDistanceMeters, &totalElevGain,
                                             &totalElevLoss, &minElevation, &maxElevation);
    float distanceKm = totalDistanceMeters / 1000.0f;

    int statsY = nameY + 12;
    const int statsLabelX = 10;
    const int statsValueX = 68;
    const int statsLineHeight = 11;

    unsigned long elapsedSec = 0;
    long long startEpochMs = 0;
    if (isViewingRecording) {
      elapsedSec = viewedRecordingDurationSec;
      startEpochMs = viewedRecordingStartEpochMs;
    } else {
      elapsedSec = (millis() - recordingStartTime) / 1000;
      startEpochMs = recordingStartEpochMs;
      if (startEpochMs == 0) {
        time_t now = time(nullptr);
        if (now > 0) {
          startEpochMs = ((long long)now * 1000LL) - ((long long)elapsedSec * 1000LL);
        }
      }
    }

    char startStr[20];
    if (startEpochMs > 0) {
      time_t startTime = (time_t)(startEpochMs / 1000);
      struct tm* timeinfo = localtime(&startTime);
      if (timeinfo != nullptr) {
        strftime(startStr, sizeof(startStr), "%d.%m %H:%M", timeinfo);
      } else {
        snprintf(startStr, sizeof(startStr), "--");
      }
    } else {
      snprintf(startStr, sizeof(startStr), "--");
    }

    u8g2_display.setFont(u8g2_font_helvB08_tf);
    u8g2_display.setCursor(statsLabelX, statsY);
    u8g2_display.print("Start");
    u8g2_display.setFont(u8g2_font_helvR08_tf);
    u8g2_display.setCursor(statsValueX, statsY);
    u8g2_display.print(startStr);
    statsY += statsLineHeight;

    char elapsedStr[16];
    formatElapsedTime(elapsedStr, sizeof(elapsedStr), elapsedSec);
    u8g2_display.setFont(u8g2_font_helvB08_tf);
    u8g2_display.setCursor(statsLabelX, statsY);
    u8g2_display.print("Elapsed");
    u8g2_display.setFont(u8g2_font_helvR08_tf);
    u8g2_display.setCursor(statsValueX, statsY);
    u8g2_display.print(elapsedStr);
    statsY += statsLineHeight;

    u8g2_display.setFont(u8g2_font_helvB08_tf);
    u8g2_display.setCursor(statsLabelX, statsY);
    u8g2_display.print("Points");
    u8g2_display.setFont(u8g2_font_helvR08_tf);
    u8g2_display.setCursor(statsValueX, statsY);
    u8g2_display.print(recordedPointsCount);
    statsY += statsLineHeight;

    u8g2_display.setFont(u8g2_font_helvB08_tf);
    u8g2_display.setCursor(statsLabelX, statsY);
    u8g2_display.print("Distance");
    u8g2_display.setFont(u8g2_font_helvR08_tf);
    u8g2_display.setCursor(statsValueX, statsY);
    u8g2_display.print(distanceKm, 2);
    u8g2_display.print(" km");
    statsY += statsLineHeight;

    u8g2_display.setFont(u8g2_font_helvB08_tf);
    u8g2_display.setCursor(statsLabelX, statsY);
    u8g2_display.print("Elevation");
    u8g2_display.setFont(u8g2_font_helvR08_tf);
    u8g2_display.setCursor(statsValueX, statsY);
    if (hasElevation) {
      u8g2_display.print("+");
      u8g2_display.print((int)totalElevGain);
      u8g2_display.print(" / -");
      u8g2_display.print((int)totalElevLoss);
      u8g2_display.print(" m");
    } else {
      u8g2_display.print("--");
    }
    statsY += statsLineHeight;

    // --- SEPARATOR LINE ---
    int separatorY = statsY + 4;
    display.drawLine(0, separatorY, DISPLAY_WIDTH, separatorY, GxEPD_BLACK);

    // --- BUTTONS ---
    const int BUTTON_WIDTH = 110;
    const int BUTTON_HEIGHT = 24;
    const int BUTTON_SHADOW = 2;
    const int BUTTON_SPACING = 8;
    int buttonX = (DISPLAY_WIDTH - BUTTON_WIDTH) / 2;
    int buttonY = separatorY + 15;

    if (isViewingRecording) {
      bool backSelected = (selectedViewButton == 0);
      bool deleteSelected = (selectedViewButton == 1);

      if (backSelected) {
        display.fillRect(buttonX + BUTTON_SHADOW, buttonY + BUTTON_SHADOW,
                         BUTTON_WIDTH, BUTTON_HEIGHT, GxEPD_BLACK);
        display.fillRect(buttonX, buttonY, BUTTON_WIDTH, BUTTON_HEIGHT, GxEPD_BLACK);
        u8g2_display.setForegroundColor(GxEPD_WHITE);
        u8g2_display.setBackgroundColor(GxEPD_BLACK);
      } else {
        display.fillRect(buttonX + BUTTON_SHADOW, buttonY + BUTTON_SHADOW,
                         BUTTON_WIDTH, BUTTON_HEIGHT, GxEPD_BLACK);
        display.drawRect(buttonX, buttonY, BUTTON_WIDTH, BUTTON_HEIGHT, GxEPD_BLACK);
        display.fillRect(buttonX + 1, buttonY + 1, BUTTON_WIDTH - 2, BUTTON_HEIGHT - 2, GxEPD_WHITE);
        u8g2_display.setForegroundColor(GxEPD_BLACK);
        u8g2_display.setBackgroundColor(GxEPD_WHITE);
      }
      u8g2_display.setFont(u8g2_font_helvB08_tf);
      const char* backText = "Back to Map";
      int backTextWidth = u8g2_display.getUTF8Width(backText);
      u8g2_display.setCursor(buttonX + (BUTTON_WIDTH - backTextWidth) / 2, buttonY + 15);
      u8g2_display.print(backText);

      u8g2_display.setForegroundColor(GxEPD_BLACK);
      u8g2_display.setBackgroundColor(GxEPD_WHITE);

      buttonY += BUTTON_HEIGHT + BUTTON_SPACING;

      if (deleteSelected) {
        display.fillRect(buttonX + BUTTON_SHADOW, buttonY + BUTTON_SHADOW,
                         BUTTON_WIDTH, BUTTON_HEIGHT, GxEPD_BLACK);
        display.fillRect(buttonX, buttonY, BUTTON_WIDTH, BUTTON_HEIGHT, GxEPD_BLACK);
        u8g2_display.setForegroundColor(GxEPD_WHITE);
        u8g2_display.setBackgroundColor(GxEPD_BLACK);
      } else {
        display.fillRect(buttonX + BUTTON_SHADOW, buttonY + BUTTON_SHADOW,
                         BUTTON_WIDTH, BUTTON_HEIGHT, GxEPD_BLACK);
        display.drawRect(buttonX, buttonY, BUTTON_WIDTH, BUTTON_HEIGHT, GxEPD_BLACK);
        display.fillRect(buttonX + 1, buttonY + 1, BUTTON_WIDTH - 2, BUTTON_HEIGHT - 2, GxEPD_WHITE);
        u8g2_display.setForegroundColor(GxEPD_BLACK);
        u8g2_display.setBackgroundColor(GxEPD_WHITE);
      }
      const char* deleteText = "Delete";
      int deleteTextWidth = u8g2_display.getUTF8Width(deleteText);
      u8g2_display.setCursor(buttonX + (BUTTON_WIDTH - deleteTextWidth) / 2, buttonY + 15);
      u8g2_display.print(deleteText);
      u8g2_display.setForegroundColor(GxEPD_BLACK);
      u8g2_display.setBackgroundColor(GxEPD_WHITE);
    } else {
      // Button 1: Change Frequency
      bool btn0Selected = (selectedButton == 0 && !showFrequencyPopup);
      if (btn0Selected) {
        display.fillRect(buttonX + BUTTON_SHADOW, buttonY + BUTTON_SHADOW,
                         BUTTON_WIDTH, BUTTON_HEIGHT, GxEPD_BLACK);
        display.fillRect(buttonX, buttonY, BUTTON_WIDTH, BUTTON_HEIGHT, GxEPD_BLACK);
        u8g2_display.setForegroundColor(GxEPD_WHITE);
        u8g2_display.setBackgroundColor(GxEPD_BLACK);
      } else {
        display.fillRect(buttonX + BUTTON_SHADOW, buttonY + BUTTON_SHADOW,
                         BUTTON_WIDTH, BUTTON_HEIGHT, GxEPD_BLACK);
        display.drawRect(buttonX, buttonY, BUTTON_WIDTH, BUTTON_HEIGHT, GxEPD_BLACK);
        display.fillRect(buttonX + 1, buttonY + 1, BUTTON_WIDTH - 2, BUTTON_HEIGHT - 2, GxEPD_WHITE);
      }
      u8g2_display.setFont(u8g2_font_helvB08_tf);
      char freqBtnText[32];
      snprintf(freqBtnText, sizeof(freqBtnText), "Frequency: %s", FREQ_LABELS[selectedFrequencyIndex]);
      int freqTextWidth = u8g2_display.getUTF8Width(freqBtnText);
      u8g2_display.setCursor(buttonX + (BUTTON_WIDTH - freqTextWidth) / 2, buttonY + 15);
      u8g2_display.print(freqBtnText);
      u8g2_display.setForegroundColor(GxEPD_BLACK);
      u8g2_display.setBackgroundColor(GxEPD_WHITE);

      buttonY += BUTTON_HEIGHT + BUTTON_SPACING;

      // Button 2: Pause/Resume Recording
      bool btn1Selected = (selectedButton == 1 && !showFrequencyPopup);
      if (btn1Selected) {
        display.fillRect(buttonX + BUTTON_SHADOW, buttonY + BUTTON_SHADOW,
                         BUTTON_WIDTH, BUTTON_HEIGHT, GxEPD_BLACK);
        display.fillRect(buttonX, buttonY, BUTTON_WIDTH, BUTTON_HEIGHT, GxEPD_BLACK);
        u8g2_display.setForegroundColor(GxEPD_WHITE);
        u8g2_display.setBackgroundColor(GxEPD_BLACK);
      } else {
        display.fillRect(buttonX + BUTTON_SHADOW, buttonY + BUTTON_SHADOW,
                         BUTTON_WIDTH, BUTTON_HEIGHT, GxEPD_BLACK);
        display.drawRect(buttonX, buttonY, BUTTON_WIDTH, BUTTON_HEIGHT, GxEPD_BLACK);
        display.fillRect(buttonX + 1, buttonY + 1, BUTTON_WIDTH - 2, BUTTON_HEIGHT - 2, GxEPD_WHITE);
      }
      const char* pauseText = isRecordingPaused ? "Resume Recording" : "Pause Recording";
      int pauseTextWidth = u8g2_display.getUTF8Width(pauseText);
      u8g2_display.setCursor(buttonX + (BUTTON_WIDTH - pauseTextWidth) / 2, buttonY + 15);
      u8g2_display.print(pauseText);
      u8g2_display.setForegroundColor(GxEPD_BLACK);
      u8g2_display.setBackgroundColor(GxEPD_WHITE);

      buttonY += BUTTON_HEIGHT + BUTTON_SPACING + 8;

      // Separator
      display.drawLine(10, buttonY, DISPLAY_WIDTH - 10, buttonY, GxEPD_BLACK);
      buttonY += 12;

      // Button 3: Save and Exit
      bool btn2Selected = (selectedButton == 2 && !showFrequencyPopup);
      if (btn2Selected) {
        display.fillRect(buttonX + BUTTON_SHADOW, buttonY + BUTTON_SHADOW,
                         BUTTON_WIDTH, BUTTON_HEIGHT, GxEPD_BLACK);
        display.fillRect(buttonX, buttonY, BUTTON_WIDTH, BUTTON_HEIGHT, GxEPD_BLACK);
        u8g2_display.setForegroundColor(GxEPD_WHITE);
        u8g2_display.setBackgroundColor(GxEPD_BLACK);
      } else {
        display.fillRect(buttonX + BUTTON_SHADOW, buttonY + BUTTON_SHADOW,
                         BUTTON_WIDTH, BUTTON_HEIGHT, GxEPD_BLACK);
        display.drawRect(buttonX, buttonY, BUTTON_WIDTH, BUTTON_HEIGHT, GxEPD_BLACK);
        display.fillRect(buttonX + 1, buttonY + 1, BUTTON_WIDTH - 2, BUTTON_HEIGHT - 2, GxEPD_WHITE);
      }
      const char* saveText = "Save and Exit";
      int saveTextWidth = u8g2_display.getUTF8Width(saveText);
      u8g2_display.setCursor(buttonX + (BUTTON_WIDTH - saveTextWidth) / 2, buttonY + 15);
      u8g2_display.print(saveText);
      u8g2_display.setForegroundColor(GxEPD_BLACK);
      u8g2_display.setBackgroundColor(GxEPD_WHITE);

      buttonY += BUTTON_HEIGHT + BUTTON_SPACING;

      // Button 4: Discard and Exit
      bool btn3Selected = (selectedButton == 3 && !showFrequencyPopup);
      if (btn3Selected) {
        display.fillRect(buttonX + BUTTON_SHADOW, buttonY + BUTTON_SHADOW,
                         BUTTON_WIDTH, BUTTON_HEIGHT, GxEPD_BLACK);
        display.fillRect(buttonX, buttonY, BUTTON_WIDTH, BUTTON_HEIGHT, GxEPD_BLACK);
        u8g2_display.setForegroundColor(GxEPD_WHITE);
        u8g2_display.setBackgroundColor(GxEPD_BLACK);
      } else {
        display.fillRect(buttonX + BUTTON_SHADOW, buttonY + BUTTON_SHADOW,
                         BUTTON_WIDTH, BUTTON_HEIGHT, GxEPD_BLACK);
        display.drawRect(buttonX, buttonY, BUTTON_WIDTH, BUTTON_HEIGHT, GxEPD_BLACK);
        display.fillRect(buttonX + 1, buttonY + 1, BUTTON_WIDTH - 2, BUTTON_HEIGHT - 2, GxEPD_WHITE);
      }
      const char* discardText = "Discard and Exit";
      int discardTextWidth = u8g2_display.getUTF8Width(discardText);
      u8g2_display.setCursor(buttonX + (BUTTON_WIDTH - discardTextWidth) / 2, buttonY + 15);
      u8g2_display.print(discardText);
      u8g2_display.setForegroundColor(GxEPD_BLACK);
      u8g2_display.setBackgroundColor(GxEPD_WHITE);

      // --- FREQUENCY POPUP (if shown) ---
      if (showFrequencyPopup) {
        const int POPUP_WIDTH = 110;
        const int POPUP_HEIGHT = 140;
        const int POPUP_SHADOW = 3;
        int popupX = (DISPLAY_WIDTH - POPUP_WIDTH) / 2;
        int popupY = (DISPLAY_HEIGHT - POPUP_HEIGHT) / 2 - 20;

        // Draw shadow
        display.fillRect(popupX + POPUP_SHADOW, popupY + POPUP_SHADOW,
                         POPUP_WIDTH, POPUP_HEIGHT, GxEPD_BLACK);

        // Draw popup background
        display.fillRect(popupX, popupY, POPUP_WIDTH, POPUP_HEIGHT, GxEPD_WHITE);
        display.drawRect(popupX, popupY, POPUP_WIDTH, POPUP_HEIGHT, GxEPD_BLACK);
        display.drawRect(popupX + 1, popupY + 1, POPUP_WIDTH - 2, POPUP_HEIGHT - 2, GxEPD_BLACK);

        // Title
        u8g2_display.setFont(u8g2_font_helvB10_tf);
        const char* popupTitle = "Frequency";
        int popupTitleWidth = u8g2_display.getUTF8Width(popupTitle);
        u8g2_display.setCursor(popupX + (POPUP_WIDTH - popupTitleWidth) / 2, popupY + 18);
        u8g2_display.print(popupTitle);

        // Draw vertical frequency selector
        drawVerticalFrequencySelector(popupX, popupY, POPUP_WIDTH);

        // Hint text at bottom
        u8g2_display.setFont(u8g2_font_helvR08_tf);
        const char* hint = "Press to confirm";
        int hintWidth = u8g2_display.getUTF8Width(hint);
        u8g2_display.setCursor(popupX + (POPUP_WIDTH - hintWidth) / 2, popupY + POPUP_HEIGHT - 10);
        u8g2_display.print(hint);
      }
    }

    // --- STATUS BAR ---
    drawStatusBar();

    // --- NOTIFICATION OVERLAY ---
    drawNotificationOverlay();

  } while (display.nextPage());
}

void updateRecordingOptionsPage() {
  // Let the centralized status bar handle time/battery/GPS/BLE updates
  updateStatusBar();
}

void handleRecordingOptionsEncoder(int delta) {
  markUserActivity();

  if (isViewingRecording) {
    if (delta == 0) {
      return;
    }
    selectedViewButton += delta;
    if (selectedViewButton < 0) {
      selectedViewButton = 1;
    } else if (selectedViewButton > 1) {
      selectedViewButton = 0;
    }
    renderRecordingOptionsPage();
    return;
  }

  if (showFrequencyPopup) {
    // Change frequency selection in popup
    selectedFrequencyIndex += delta;  // Match encoder direction used on other pages

    // Clamp to valid range
    if (selectedFrequencyIndex < 0) {
      selectedFrequencyIndex = 0;
    } else if (selectedFrequencyIndex >= FREQ_COUNT) {
      selectedFrequencyIndex = FREQ_COUNT - 1;
    }

    Serial.printf("Recording frequency: %s\n", FREQ_LABELS[selectedFrequencyIndex]);

    // Re-render page with new selection
    renderRecordingOptionsPage();
  } else {
    // Navigate between buttons
    selectedButton += delta;

    // Clamp to valid range (0-3)
    if (selectedButton < 0) {
      selectedButton = 0;
    } else if (selectedButton > 3) {
      selectedButton = 3;
    }

    Serial.printf("Selected button: %d\n", selectedButton);

    // Re-render page
    renderRecordingOptionsPage();
  }
}

void handleRecordingOptionsButton() {
  markUserActivity();
  if (isViewingRecording) {
    if (selectedViewButton == 0) {
      navigateToPage(PAGE_RECORDING);
      return;
    }
    if (viewedRecordingDirName[0] == '\0') {
      showNotification("Recording", "Delete failed", "", ICON_TRACKER, 3000);
      return;
    }
    if (!deleteRecordingFromSD(viewedRecordingDirName)) {
      showNotification("Recording", "Delete failed", "", ICON_TRACKER, 3000);
      return;
    }

    stopRecording();
    clearViewingRecordingState();
    showNotification("Recording", "Deleted", "", ICON_TRACKER, 2000);
    navigateToPage(PAGE_TRACKER);
    return;
  }

  if (showFrequencyPopup) {
    // Close frequency popup (confirm selection)
    showFrequencyPopup = false;
    Serial.println("Frequency popup closed");
    renderRecordingOptionsPage();
  } else {
    // Handle button press based on selected button
    if (selectedButton == 0) {
      // Open frequency popup
      showFrequencyPopup = true;
      Serial.println("Opening frequency popup");
      renderRecordingOptionsPage();
    } else if (selectedButton == 1) {
      // Pause/Resume Recording
      isRecordingPaused = !isRecordingPaused;
      Serial.printf("Recording %s\n", isRecordingPaused ? "paused" : "resumed");

      // Show notification
      if (isRecordingPaused) {
        showNotification("Recording", "Paused", "", ICON_TRACKER, 2000);
      } else {
        showNotification("Recording", "Resumed", "", ICON_TRACKER, 2000);
      }

      // Re-render page to update button text
      renderRecordingOptionsPage();
    } else if (selectedButton == 2) {
      // Save and Exit
      Serial.println("Saving recording...");

      // Check if we have any points recorded
      if (recordedPointsCount == 0) {
        showNotification("Recording", "No points recorded", "", ICON_TRACKER, 3000);
        Serial.println("Cannot save - no points recorded, ending recording");

        // Stop recording and free PSRAM
        stopRecording();

        // Return to tracker page
        navigateToPage(PAGE_TRACKER);
        return;
      }

      // Get recording name
      char recordingName[64];
      getRecordingName(recordingName, sizeof(recordingName));

      // Save to GPX file
      bool success = saveRecordingToGPX(recordingName);

      if (success) {
        showNotification("Recording", "Saved", "", ICON_TRACKER, 2000);
        Serial.printf("Recording saved: %s\n", recordingName);

        // Stop recording and free PSRAM
        stopRecording();

        // Return to tracker page
        navigateToPage(PAGE_TRACKER);
      } else {
        showNotification("Recording", "Save failed", "", ICON_TRACKER, 3000);
        Serial.println("Failed to save recording");
      }

    } else if (selectedButton == 3) {
      // Discard and Exit
      Serial.println("Discarding recording...");

      // Stop recording and free PSRAM (without saving)
      stopRecording();

      showNotification("Recording", "Discarded", "", ICON_TRACKER, 2000);

      // Return to tracker page
      navigateToPage(PAGE_TRACKER);
    }
  }
}

bool handleRecordingOptionsBack() {
  if (showFrequencyPopup) {
    // Close frequency popup
    showFrequencyPopup = false;
    Serial.println("Frequency popup closed (back button)");
    renderRecordingOptionsPage();
    return true;  // We handled it
  } else {
    // Return to recording page
    Serial.println("Exiting recording options page");
    return false;  // Let caller handle page navigation
  }
}

#endif // PAGE_RECORDING_OPTIONS_H
