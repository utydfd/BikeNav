#ifndef PAGE_TRACKER_H
#define PAGE_TRACKER_H

#include <GxEPD2_BW.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <SD.h>
#include "notification_system.h"
#include "status_bar.h"
#include "bitmaps.h"

extern GxEPD2_BW<GxEPD2_290_BS, GxEPD2_290_BS::HEIGHT> display;
extern U8G2_FOR_ADAFRUIT_GFX u8g2_display;
extern const int DISPLAY_WIDTH;
extern const int DISPLAY_HEIGHT;
extern double currentLat;
extern double currentLon;
extern bool gpsValid;

// External from bitmaps.h
extern const unsigned char ICON_RECORD[];
extern const unsigned char ICON_TRACKER[];
extern const int ICON_SIZE;
extern bool loadRecordingForView(const char* recordingDirName, const char* displayName);

// --- HELPER FUNCTIONS ---

const int TRACKER_HEADER_Y = 18;
const int TRACKER_CARD_Y = 26;
const int TRACKER_CARD_HEIGHT = 52;
const int TRACKER_LIST_ITEM_HEIGHT = 18;

int trackerSelectedIndex = 0;  // 0 = New Recording, 1+ = saved recordings
int trackerScrollOffset = 0;   // First visible recording index (0-based)
int lastRecordingCount = -1;

// Count recordings on SD card
int countRecordingsOnSD() {
  File recordingsDir = SD.open("/Recordings");
  if (!recordingsDir) return 0;

  int count = 0;
  File entry = recordingsDir.openNextFile();
  while (entry) {
    if (entry.isDirectory()) {
      count++;
    }
    entry.close();
    entry = recordingsDir.openNextFile();
  }
  recordingsDir.close();
  return count;
}

// Get recording name by index (reads from metadata JSON)
bool getRecordingNameByIndex(int index, char* outName, size_t maxLen) {
  File recordingsDir = SD.open("/Recordings");
  if (!recordingsDir) return false;

  int currentIndex = 0;
  File entry = recordingsDir.openNextFile();
  while (entry) {
    if (entry.isDirectory()) {
      if (currentIndex == index) {
        const char* dirName = entry.name();

        // Try to read name from metadata JSON
        char metaPath[96];
        snprintf(metaPath, sizeof(metaPath), "/Recordings/%s/%s_meta.json", dirName, dirName);

        File metaFile = SD.open(metaPath, FILE_READ);
        if (metaFile) {
          // Simple JSON parsing - look for "name" field
          String jsonContent = "";
          while (metaFile.available()) {
            jsonContent += (char)metaFile.read();
          }
          metaFile.close();

          // Extract name field
          int nameStart = jsonContent.indexOf("\"name\":\"");
          if (nameStart != -1) {
            nameStart += 8;  // Move past "name":"
            int nameEnd = jsonContent.indexOf("\"", nameStart);
            if (nameEnd != -1) {
              String name = jsonContent.substring(nameStart, nameEnd);
              strncpy(outName, name.c_str(), maxLen - 1);
              outName[maxLen - 1] = '\0';
              entry.close();
              recordingsDir.close();
              return true;
            }
          }
        }

        // Fallback to directory name
        strncpy(outName, dirName, maxLen - 1);
        outName[maxLen - 1] = '\0';
        entry.close();
        recordingsDir.close();
        return true;
      }
      currentIndex++;
    }
    entry.close();
    entry = recordingsDir.openNextFile();
  }
  recordingsDir.close();
  return false;
}

bool getRecordingDirNameByIndex(int index, char* outName, size_t maxLen) {
  File recordingsDir = SD.open("/Recordings");
  if (!recordingsDir) return false;

  int currentIndex = 0;
  File entry = recordingsDir.openNextFile();
  while (entry) {
    if (entry.isDirectory()) {
      if (currentIndex == index) {
        strncpy(outName, entry.name(), maxLen - 1);
        outName[maxLen - 1] = '\0';
        entry.close();
        recordingsDir.close();
        return true;
      }
      currentIndex++;
    }
    entry.close();
    entry = recordingsDir.openNextFile();
  }
  recordingsDir.close();
  return false;
}

int getTrackerListHeaderY() {
  return TRACKER_CARD_Y + TRACKER_CARD_HEIGHT + 12;
}

int getTrackerListStartY() {
  return getTrackerListHeaderY() + 12;
}

int getTrackerVisibleCount() {
  int contentBottom = DISPLAY_HEIGHT - STATUS_BAR_HEIGHT - 2;
  int available = contentBottom - getTrackerListStartY();
  int slots = available / TRACKER_LIST_ITEM_HEIGHT;
  return max(1, slots);
}

void clampTrackerSelection(int recordingCount) {
  int totalItems = 1 + recordingCount;
  if (totalItems <= 0) {
    trackerSelectedIndex = 0;
    return;
  }
  if (trackerSelectedIndex < 0) {
    trackerSelectedIndex = 0;
  } else if (trackerSelectedIndex >= totalItems) {
    trackerSelectedIndex = totalItems - 1;
  }
}

void updateTrackerScroll(int recordingCount) {
  int visibleCount = getTrackerVisibleCount();
  int maxOffset = max(0, recordingCount - visibleCount);

  if (trackerSelectedIndex <= 0) {
    trackerScrollOffset = min(trackerScrollOffset, maxOffset);
    return;
  }

  int selectedRecordingIndex = trackerSelectedIndex - 1;
  if (selectedRecordingIndex < trackerScrollOffset) {
    trackerScrollOffset = selectedRecordingIndex;
  } else if (selectedRecordingIndex >= trackerScrollOffset + visibleCount) {
    trackerScrollOffset = selectedRecordingIndex - visibleCount + 1;
  }

  if (trackerScrollOffset < 0) {
    trackerScrollOffset = 0;
  } else if (trackerScrollOffset > maxOffset) {
    trackerScrollOffset = maxOffset;
  }
}

// --- PAGE FUNCTIONS ---

void initTrackerPage() {
  trackerSelectedIndex = 0;
  trackerScrollOffset = 0;
  lastRecordingCount = -1;
}

void renderTrackerPage() {
  // Use fastest refresh (partial window)
  display.setPartialWindow(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
  display.firstPage();

  do {
    display.fillScreen(GxEPD_WHITE);

    // Setup text rendering
    u8g2_display.setFontMode(1);
    u8g2_display.setForegroundColor(GxEPD_BLACK);
    u8g2_display.setBackgroundColor(GxEPD_WHITE);

    int recordingCount = countRecordingsOnSD();
    clampTrackerSelection(recordingCount);
    updateTrackerScroll(recordingCount);

    // --- HEADER ---
    u8g2_display.setFont(u8g2_font_helvB12_tf);
    u8g2_display.setCursor(4, TRACKER_HEADER_Y);
    u8g2_display.print("TRACKER");

    // --- NEW RECORDING CARD ---
    bool newSelected = (trackerSelectedIndex == 0);
    int cardX = 4;
    int cardY = TRACKER_CARD_Y;
    int cardW = DISPLAY_WIDTH - 8;
    int cardH = TRACKER_CARD_HEIGHT;

    if (newSelected) {
      display.fillRect(cardX, cardY, cardW, cardH, GxEPD_BLACK);
      u8g2_display.setForegroundColor(GxEPD_WHITE);
      u8g2_display.setBackgroundColor(GxEPD_BLACK);
    } else {
      display.drawRect(cardX, cardY, cardW, cardH, GxEPD_BLACK);
      display.drawRect(cardX + 1, cardY + 1, cardW - 2, cardH - 2, GxEPD_BLACK);
    }

    int iconX = cardX + 6;
    int iconY = cardY + (cardH - ICON_SIZE) / 2;
    display.drawBitmap(iconX, iconY, ICON_RECORD, ICON_SIZE, ICON_SIZE,
                       newSelected ? GxEPD_WHITE : GxEPD_BLACK);

    const char* line1 = "New";
    const char* line2 = "Recording";
    u8g2_display.setFont(u8g2_font_helvB08_tf);
    int textX = iconX + ICON_SIZE + 6;
    int maxRight = DISPLAY_WIDTH - 4;
    int minTextX = iconX + ICON_SIZE + 2;
    int maxTextWidth = max(u8g2_display.getUTF8Width(line1),
                           u8g2_display.getUTF8Width(line2));
    if (textX + maxTextWidth > maxRight) {
      textX = maxRight - maxTextWidth;
      if (textX < minTextX) {
        textX = minTextX;
      }
      if (textX + maxTextWidth > maxRight) {
        u8g2_display.setFont(u8g2_font_helvB08_tf);
        maxTextWidth = max(u8g2_display.getUTF8Width(line1),
                           u8g2_display.getUTF8Width(line2));
        if (textX + maxTextWidth > maxRight) {
          textX = maxRight - maxTextWidth;
          if (textX < minTextX) {
            textX = minTextX;
          }
        }
      }
    }
    int textYLine1 = cardY + (cardH / 2) - 3;
    int textYLine2 = cardY + (cardH / 2) + 8;
    u8g2_display.setCursor(textX, textYLine1);
    u8g2_display.print(line1);
    u8g2_display.setCursor(textX, textYLine2);
    u8g2_display.print(line2);

    u8g2_display.setForegroundColor(GxEPD_BLACK);
    u8g2_display.setBackgroundColor(GxEPD_WHITE);

    // --- SAVED RECORDINGS HEADER ---
    int listHeaderY = getTrackerListHeaderY();
    u8g2_display.setFont(u8g2_font_helvB08_tf);
    u8g2_display.setCursor(6, listHeaderY);
    u8g2_display.print("Saved Recordings");

    int visibleCount = getTrackerVisibleCount();
    u8g2_display.setFont(u8g2_font_helvR08_tf);
    char countStr[12];
    snprintf(countStr, sizeof(countStr), "%d", recordingCount);
    int countWidth = u8g2_display.getUTF8Width(countStr);
    int countX = DISPLAY_WIDTH - countWidth - 4;
    u8g2_display.setCursor(countX, listHeaderY);
    u8g2_display.print(countStr);

    if (recordingCount > visibleCount) {
      int totalPages = (recordingCount + visibleCount - 1) / visibleCount;
      int currentPage = (trackerScrollOffset / visibleCount) + 1;
      char pageStr[12];
      snprintf(pageStr, sizeof(pageStr), "%d/%d", currentPage, totalPages);
      int pageWidth = u8g2_display.getUTF8Width(pageStr);
      int pageX = countX - pageWidth - 4;
      if (pageX > 70) {
        u8g2_display.setCursor(pageX, listHeaderY);
        u8g2_display.print(pageStr);
      }
    }

    int listStartY = getTrackerListStartY();
    display.drawLine(4, listHeaderY + 6, DISPLAY_WIDTH - 4, listHeaderY + 6, GxEPD_BLACK);

    if (recordingCount == 0) {
      // No recordings - show message (3 lines)
      u8g2_display.setFont(u8g2_font_helvR08_tf);

      const char* line1 = "No recordings yet";
      int line1Width = u8g2_display.getUTF8Width(line1);
      u8g2_display.setCursor((DISPLAY_WIDTH - line1Width) / 2, listStartY + 34);
      u8g2_display.print(line1);

      const char* line2 = "Start a new one";
      int line2Width = u8g2_display.getUTF8Width(line2);
      u8g2_display.setCursor((DISPLAY_WIDTH - line2Width) / 2, listStartY + 50);
      u8g2_display.print(line2);

      const char* line3 = "above";
      int line3Width = u8g2_display.getUTF8Width(line3);
      u8g2_display.setCursor((DISPLAY_WIDTH - line3Width) / 2, listStartY + 66);
      u8g2_display.print(line3);
    } else {
      // Show recordings list
      int displayCount = min(recordingCount - trackerScrollOffset, visibleCount);
      int yPos = listStartY + 12;

      for (int i = 0; i < displayCount; i++) {
        int recordingIndex = trackerScrollOffset + i;
        char recordingName[64];
        if (getRecordingNameByIndex(recordingIndex, recordingName, sizeof(recordingName))) {
          bool isSelected = (trackerSelectedIndex == recordingIndex + 1);
          int rowTop = yPos - 12;
          int rowHeight = TRACKER_LIST_ITEM_HEIGHT;
          int rowX = 4;
          int rowW = DISPLAY_WIDTH - 8;

          if (isSelected) {
            display.fillRect(rowX, rowTop, rowW, rowHeight, GxEPD_BLACK);
            u8g2_display.setForegroundColor(GxEPD_WHITE);
            u8g2_display.setBackgroundColor(GxEPD_BLACK);
          } else {
            u8g2_display.setForegroundColor(GxEPD_BLACK);
            u8g2_display.setBackgroundColor(GxEPD_WHITE);
          }

          u8g2_display.setFont(u8g2_font_helvR08_tf);
          u8g2_display.setCursor(rowX + 6, yPos);
          u8g2_display.print(isSelected ? "> " : "  ");

          if (strlen(recordingName) > 18) {
            recordingName[15] = '.';
            recordingName[16] = '.';
            recordingName[17] = '.';
            recordingName[18] = '\0';
          }

          u8g2_display.print(recordingName);
          yPos += TRACKER_LIST_ITEM_HEIGHT;
        }
      }
    }

    // --- STATUS BAR (STANDARD) ---
    drawStatusBar();

    // --- NOTIFICATION OVERLAY ---
    drawNotificationOverlay();

  } while (display.nextPage());
}

void updateTrackerPage() {
  // Check if recording count changed (new recordings added/deleted)
  int currentCount = countRecordingsOnSD();
  if (lastRecordingCount != -1 && currentCount != lastRecordingCount) {
    Serial.printf("[TRACKER] Recording count changed (%d -> %d), refreshing page\n",
                  lastRecordingCount, currentCount);
    lastRecordingCount = currentCount;
    clampTrackerSelection(currentCount);
    updateTrackerScroll(currentCount);
    renderTrackerPage();
    return;
  }
  lastRecordingCount = currentCount;

  // Let the centralized status bar handle time/battery/GPS/BLE updates
  // This runs at lower priority and respects user activity
  updateStatusBar();
}

void handleTrackerEncoder(int delta) {
  markUserActivity();  // Prevent status bar auto-refresh during interaction

  int recordingCount = countRecordingsOnSD();
  int totalItems = 1 + recordingCount;

  if (totalItems <= 0) {
    trackerSelectedIndex = 0;
    return;
  }

  trackerSelectedIndex += delta;
  if (trackerSelectedIndex < 0) {
    trackerSelectedIndex = totalItems - 1;
  } else if (trackerSelectedIndex >= totalItems) {
    trackerSelectedIndex = 0;
  }

  updateTrackerScroll(recordingCount);
  renderTrackerPage();
}

void handleTrackerButton() {
  markUserActivity();  // Prevent status bar auto-refresh during interaction

  if (trackerSelectedIndex == 0) {
    // Start new recording
    Serial.println("Starting new recording - navigating to recording page");
    navigateToPage(PAGE_RECORDING);
    return;
  }

  int recordingCount = countRecordingsOnSD();
  int recordingIndex = trackerSelectedIndex - 1;
  if (recordingIndex < 0 || recordingIndex >= recordingCount) {
    return;
  }

  char recordingDirName[64];
  if (!getRecordingDirNameByIndex(recordingIndex, recordingDirName, sizeof(recordingDirName))) {
    Serial.println("Failed to resolve recording directory");
    showNotification("Recording", "Load failed", "", ICON_TRACKER, 3000);
    return;
  }

  char recordingName[64];
  if (!getRecordingNameByIndex(recordingIndex, recordingName, sizeof(recordingName))) {
    strncpy(recordingName, recordingDirName, sizeof(recordingName) - 1);
    recordingName[sizeof(recordingName) - 1] = '\0';
  }

  Serial.printf("Opening recording: %s (%s)\n", recordingName, recordingDirName);
  if (!loadRecordingForView(recordingDirName, recordingName)) {
    showNotification("Recording", "Load failed", "", ICON_TRACKER, 3000);
    return;
  }

  navigateToPage(PAGE_RECORDING);
}

#endif
