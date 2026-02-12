#ifndef PAGE_WEATHER_OPTIONS_H
#define PAGE_WEATHER_OPTIONS_H

#include <GxEPD2_BW.h>
#include <U8g2_for_Adafruit_GFX.h>
#include "notification_system.h"
#include "status_bar.h"

extern GxEPD2_BW<GxEPD2_290_BS, GxEPD2_290_BS::HEIGHT> display;
extern U8G2_FOR_ADAFRUIT_GFX u8g2_display;
extern const int DISPLAY_WIDTH;
extern const int DISPLAY_HEIGHT;
extern bool deviceConnected;
extern PageType previousPage;

// Forward declarations
void navigateToPage(PageType page);
void requestWeatherUpdateForLocation(double lat, double lon);

// Weather location preset structure
struct WeatherLocationPreset {
  const char* name;
  double lat;
  double lon;
};

// Hardcoded location presets
const WeatherLocationPreset weatherPresets[] = {
  {"Doma", 50.072128, 14.168567},
  {"Praha Dejvice", 50.101796, 14.393235},
  {"Brno", 49.1951, 16.6068}
};

const int NUM_WEATHER_PRESETS = sizeof(weatherPresets) / sizeof(weatherPresets[0]);

// Weather options state
int weatherOptionsSelectedIndex = -1;  // -1 = current location, 0+ = preset index
int weatherOptionsScrollOffset = 0;
int weatherCurrentLocationIndex = -1; // Track what's currently displayed in weather page

// Helper to get total number of options (current location + presets)
int getWeatherOptionCount() {
  return 1 + NUM_WEATHER_PRESETS; // "Current Location" + presets
}

void initWeatherOptionsPage() {
  weatherOptionsScrollOffset = 0;

  // Set selected index to current location being displayed
  weatherOptionsSelectedIndex = weatherCurrentLocationIndex;

  statusBarState.lastDisplayedMinute = -1; // Force initial clock update
}

void renderWeatherOptionsPage() {
  display.setPartialWindow(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
  display.firstPage();

  do {
    display.fillScreen(GxEPD_WHITE);

    // Setup text rendering
    u8g2_display.setFontMode(1);
    u8g2_display.setForegroundColor(GxEPD_BLACK);
    u8g2_display.setBackgroundColor(GxEPD_WHITE);

    // === HEADER ===
    u8g2_display.setFont(u8g2_font_helvB14_te);
    u8g2_display.setCursor(5, 19);
    u8g2_display.print("Weather in:");

    // Draw separator line
    display.drawLine(0, 23, DISPLAY_WIDTH, 23, GxEPD_BLACK);

    // === OPTIONS LIST ===
    int statusBarY = DISPLAY_HEIGHT - STATUS_BAR_HEIGHT;
    int listStartY = 30;
    int itemHeight = 30;
    int availableHeight = statusBarY - listStartY;
    int maxVisibleItems = availableHeight / itemHeight;

    int totalOptions = getWeatherOptionCount();

    // Map selection index to display index (0 = current location at -1, 1+ = presets)
    int displayIndex = (weatherOptionsSelectedIndex == -1) ? 0 : (weatherOptionsSelectedIndex + 1);

    // Adjust scroll offset to keep selection visible
    if (displayIndex < weatherOptionsScrollOffset) {
      weatherOptionsScrollOffset = displayIndex;
    }
    if (displayIndex >= weatherOptionsScrollOffset + maxVisibleItems) {
      weatherOptionsScrollOffset = displayIndex - maxVisibleItems + 1;
    }

    // Clamp scroll offset
    int maxScroll = max(0, totalOptions - maxVisibleItems);
    if (weatherOptionsScrollOffset > maxScroll) {
      weatherOptionsScrollOffset = maxScroll;
    }
    if (weatherOptionsScrollOffset < 0) {
      weatherOptionsScrollOffset = 0;
    }

    // Draw scroll indicators
    u8g2_display.setFont(u8g2_font_helvB10_tr);
    if (weatherOptionsScrollOffset > 0) {
      u8g2_display.setCursor(DISPLAY_WIDTH - 15, listStartY + 10);
      u8g2_display.print("^");
    }
    if (totalOptions > maxVisibleItems && weatherOptionsScrollOffset < maxScroll) {
      u8g2_display.setCursor(DISPLAY_WIDTH - 15, statusBarY - 10);
      u8g2_display.print("v");
    }

    // Draw visible items
    int visibleCount = min(maxVisibleItems, totalOptions - weatherOptionsScrollOffset);

    for (int i = 0; i < visibleCount; i++) {
      int displayIndex = weatherOptionsScrollOffset + i;  // 0 = current location, 1+ = presets
      int y = listStartY + (i * itemHeight);

      // Map display index to selection index (-1 for current location, 0+ for presets)
      int mappedIndex = (displayIndex == 0) ? -1 : (displayIndex - 1);

      bool isSelected = (mappedIndex == weatherOptionsSelectedIndex);
      bool isCurrentlyDisplayed = (mappedIndex == weatherCurrentLocationIndex);

      // Highlight selected item
      if (isSelected) {
        display.fillRect(0, y - 2, DISPLAY_WIDTH, itemHeight, GxEPD_BLACK);
        u8g2_display.setForegroundColor(GxEPD_WHITE);
        u8g2_display.setBackgroundColor(GxEPD_BLACK);
      } else {
        u8g2_display.setForegroundColor(GxEPD_BLACK);
        u8g2_display.setBackgroundColor(GxEPD_WHITE);
      }

      // Draw option text
      u8g2_display.setFont(u8g2_font_helvB10_tf);

      if (displayIndex == 0) {
        // Current location option
        u8g2_display.setCursor(5, y + 12);
        u8g2_display.print("Current Location");

        // Show indicator if this is what's displayed
        if (isCurrentlyDisplayed) {
          u8g2_display.setFont(u8g2_font_helvR08_tr);
          u8g2_display.setCursor(5, y + 24);
          u8g2_display.print("(Active)");
        }
      } else {
        // Preset location (displayIndex - 1 gives preset array index)
        WeatherLocationPreset preset = weatherPresets[displayIndex - 1];
        u8g2_display.setCursor(5, y + 12);

        // Truncate name if too long
        char displayName[30];
        strncpy(displayName, preset.name, sizeof(displayName) - 1);
        displayName[sizeof(displayName) - 1] = '\0';
        u8g2_display.print(displayName);

        // Show coordinates or active indicator
        u8g2_display.setFont(u8g2_font_helvR08_tr);
        u8g2_display.setCursor(5, y + 24);

        if (isCurrentlyDisplayed) {
          u8g2_display.print("(Active)");
        } else {
          char coordStr[25];
          snprintf(coordStr, sizeof(coordStr), "%.2f, %.2f", preset.lat, preset.lon);
          u8g2_display.print(coordStr);
        }
      }

      // Reset colors for next item
      u8g2_display.setForegroundColor(GxEPD_BLACK);
      u8g2_display.setBackgroundColor(GxEPD_WHITE);
    }

    // Draw status bar
    drawStatusBar();

    // Draw notification overlay
    drawNotificationOverlay();

  } while (display.nextPage());
}

void updateWeatherOptionsPage() {
  // Let the centralized status bar handle time/battery/GPS/BLE updates
  updateStatusBar();
}

void handleWeatherOptionsEncoder(int delta) {
  markUserActivity();

  // Scroll through options
  weatherOptionsSelectedIndex += delta;

  // Wrap around (starts at -1 for current location, goes 0 to NUM_WEATHER_PRESETS-1 for presets)
  if (weatherOptionsSelectedIndex < -1) {
    weatherOptionsSelectedIndex = NUM_WEATHER_PRESETS - 1; // Last preset
  } else if (weatherOptionsSelectedIndex >= NUM_WEATHER_PRESETS) {
    weatherOptionsSelectedIndex = -1; // Back to current location
  }

  renderWeatherOptionsPage();
}

void handleWeatherOptionsButton() {
  markUserActivity();

  if (!deviceConnected) {
    extern const unsigned char ICON_BT_DISCONNECTED[];
    showNotification("BLE Required", "Connect phone", "to load weather", ICON_BT_DISCONNECTED, 2000);
    return;
  }

  // Apply selected location
  double selectedLat, selectedLon;

  if (weatherOptionsSelectedIndex == -1) {
    // Use current GPS location
    extern double currentLat;
    extern double currentLon;
    selectedLat = currentLat;
    selectedLon = currentLon;
  } else {
    // Use preset location
    WeatherLocationPreset preset = weatherPresets[weatherOptionsSelectedIndex];
    selectedLat = preset.lat;
    selectedLon = preset.lon;
  }

  // Update the current location index
  weatherCurrentLocationIndex = weatherOptionsSelectedIndex;

  // Clear previous weather data to show loading state
  extern bool weatherDataReady;
  extern WeatherDataPacket currentWeather;
  weatherDataReady = false;
  memset(&currentWeather, 0, sizeof(currentWeather));

  // Request weather for selected location
  requestWeatherUpdateForLocation(selectedLat, selectedLon);

  // Set pending flag for weather page
  extern bool weatherUpdatePending;
  weatherUpdatePending = true;

  // Navigate back to weather page
  navigateToPage(PAGE_WEATHER);
}

bool handleWeatherOptionsBack() {
  // Return to weather page without changes
  navigateToPage(PAGE_WEATHER);
  return true;
}

#endif
