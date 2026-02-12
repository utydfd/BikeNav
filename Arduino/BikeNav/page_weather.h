#ifndef PAGE_WEATHER_H
#define PAGE_WEATHER_H
#include <GxEPD2_BW.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <TinyGPSPlus.h>
#include <time.h>
#include "notification_system.h"
#include "bitmaps.h"
#include "ble_handler.h"
#include "timezone.h"
#include "status_bar.h"

extern GxEPD2_BW<GxEPD2_290_BS, GxEPD2_290_BS::HEIGHT> display;
extern U8G2_FOR_ADAFRUIT_GFX u8g2_display;
extern const int DISPLAY_WIDTH;
extern const int DISPLAY_HEIGHT;
extern bool deviceConnected;
extern bool weatherDataReady;
extern WeatherDataPacket currentWeather;
extern unsigned long lastWeatherUpdate;
extern TinyGPSPlus gps;
extern int getTimezoneOffset(int year, int month, int day, int hour);
extern LocalTime getLocalTime();

// Radar page hooks
void initRadarPage();
void renderRadarPage();
void updateRadarPage();
void handleRadarButton();
void handleRadarEncoder(int delta);
void handleRadarOptions();

// Weather page state
int weatherScrollOffset = 0;
const int HOURLY_ITEMS_VISIBLE = 4; // Show 4 hourly items at once
int weatherVisibleCapacity = HOURLY_ITEMS_VISIBLE;
bool weatherUpdatePending = false;
const int TOTAL_WEATHER_PAGES = 2;
int currentWeatherSubPage = 0; // 0 = summary, 1 = radar
const int WEATHER_PAGE_DOT_RADIUS = 3;
const int WEATHER_PAGE_DOT_SPACING = 10;
const int WEATHER_PAGE_DOT_CENTER_OFFSET = WEATHER_STATUS_BAR_EXTRA_HEIGHT - 8;
const int WEATHER_PAGE_DOT_CONTENT_GAP = 2;
const int MAX_HOURLY_ENTRIES = 6;

// Helper function to get weather icon based on condition code
const unsigned char* getWeatherIcon(uint8_t conditionCode) {
  if (conditionCode == 0 || conditionCode == 1) return WEATHER_CLEAR;           // Clear/Mainly clear
  if (conditionCode >= 2 && conditionCode <= 3) return WEATHER_CLOUDY;          // Partly cloudy/Cloudy
  if (conditionCode >= 45 && conditionCode <= 48) return WEATHER_FOG;           // Fog
  if (conditionCode >= 51 && conditionCode <= 67) return WEATHER_RAIN;          // Drizzle/Rain
  if (conditionCode >= 71 && conditionCode <= 77) return WEATHER_SNOW;          // Snow
  if (conditionCode >= 80 && conditionCode <= 82) return WEATHER_RAIN;          // Rain showers
  if (conditionCode >= 85 && conditionCode <= 86) return WEATHER_SNOW;          // Snow showers
  if (conditionCode >= 95 && conditionCode <= 99) return WEATHER_THUNDER;       // Thunderstorm
  return ICON_WEATHER; // Default
}

// Helper function to get wind direction string
const char* getWindDirection(uint16_t degrees) {
  if (degrees >= 337 || degrees < 22) return "N";
  if (degrees >= 22 && degrees < 67) return "NE";
  if (degrees >= 67 && degrees < 112) return "E";
  if (degrees >= 112 && degrees < 157) return "SE";
  if (degrees >= 157 && degrees < 202) return "S";
  if (degrees >= 202 && degrees < 247) return "SW";
  if (degrees >= 247 && degrees < 292) return "W";
  if (degrees >= 292 && degrees < 337) return "NW";
  return "?";
}

bool isHourlyEntryValid(const HourlyWeatherData& hourly) {
  return !(hourly.hour == 0 && hourly.temp == 0);
}

int countValidHourlyEntries() {
  int count = 0;
  int limit = min((int)currentWeather.hourlyCount, MAX_HOURLY_ENTRIES);
  for (int i = 0; i < limit; i++) {
    if (isHourlyEntryValid(currentWeather.hourly[i])) {
      count++;
    }
  }
  return count;
}

int buildHourlyIndexMap(int* indexMap, int maxEntries) {
  int count = 0;
  int limit = min((int)currentWeather.hourlyCount, maxEntries);
  for (int i = 0; i < limit; i++) {
    if (isHourlyEntryValid(currentWeather.hourly[i])) {
      indexMap[count++] = i;
    }
  }
  return count;
}

void formatWeatherUpdateLabel(char* label, size_t labelSize, unsigned long lastUpdateMillis) {
  if (labelSize == 0) {
    return;
  }

  label[0] = '\0';
  if (lastUpdateMillis == 0) {
    return;
  }

  unsigned long minutesAgo = (millis() - lastUpdateMillis) / 60000;
  if (minutesAgo == 0) {
    snprintf(label, labelSize, "Updated Now");
  } else {
    snprintf(label, labelSize, "Updated %lumin ago", minutesAgo);
  }
}

void drawWeatherPageDots(int currentPage, int totalPages, int statusBarY) {
  if (totalPages <= 1) {
    return;
  }

  int dotCenterY = statusBarY - WEATHER_PAGE_DOT_CENTER_OFFSET;
  int totalWidth = (totalPages * WEATHER_PAGE_DOT_RADIUS * 2) +
                   ((totalPages - 1) * WEATHER_PAGE_DOT_SPACING);
  int startX = (DISPLAY_WIDTH - totalWidth) / 2;

  for (int i = 0; i < totalPages; i++) {
    int dotX = startX + (i * (WEATHER_PAGE_DOT_RADIUS * 2 + WEATHER_PAGE_DOT_SPACING)) +
               WEATHER_PAGE_DOT_RADIUS;
    if (i == currentPage) {
      display.fillCircle(dotX, dotCenterY, WEATHER_PAGE_DOT_RADIUS, GxEPD_BLACK);
    } else {
      display.drawCircle(dotX, dotCenterY, WEATHER_PAGE_DOT_RADIUS, GxEPD_BLACK);
    }
  }
}

void initWeatherPage() {
  weatherScrollOffset = 0;
  currentWeatherSubPage = 0;
  // Don't reset weatherUpdatePending here - it may have been set by weather options page
  statusBarState.lastDisplayedMinute = -1; // Force initial clock update

  // Request weather update if connected and no recent data
  // Only request if not already pending (to avoid duplicate requests from weather options page)
  if (deviceConnected && !weatherUpdatePending) {
    unsigned long timeSinceUpdate = millis() - lastWeatherUpdate;
    // Auto-request if no data or data older than 30 minutes
    if (!weatherDataReady || timeSinceUpdate > 1800000) {
      requestWeatherUpdate();
      weatherUpdatePending = true;
    }
  }
}

void renderWeatherPage() {
  if (currentWeatherSubPage == 1) {
    renderRadarPage();
    return;
  }

  // Always use partial window for consistent, fast updates (like map and info pages)
  display.setPartialWindow(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
  display.firstPage();

  do {
    display.fillScreen(GxEPD_WHITE);

    // Setup text rendering
    u8g2_display.setFontMode(1);
    u8g2_display.setForegroundColor(GxEPD_BLACK);
    u8g2_display.setBackgroundColor(GxEPD_WHITE);

    if (!weatherDataReady) {
      // Show loading state
      u8g2_display.setFont(u8g2_font_helvB10_tf);
      int centerY = DISPLAY_HEIGHT / 2;

      if (!deviceConnected) {
        u8g2_display.setFont(u8g2_font_helvB12_tf);
        const char* lines[] = {"Connect to", "phone first", "and press", "to refresh"};
        const int lineCount = 4;
        const int lineHeight = 16;
        int textY = centerY - ((lineCount - 1) * lineHeight) / 2;

        for (int i = 0; i < lineCount; i++) {
          int lineWidth = u8g2_display.getUTF8Width(lines[i]);
          int textX = (DISPLAY_WIDTH - lineWidth) / 2;
          u8g2_display.setCursor(textX, textY);
          u8g2_display.print(lines[i]);
          textY += lineHeight;
        }
      } else if (weatherUpdatePending) {
        u8g2_display.setFont(u8g2_font_helvB12_tf);
        u8g2_display.setCursor(10, centerY - 8);
        u8g2_display.print("Loading");
        u8g2_display.setCursor(10, centerY + 12);
        u8g2_display.print("weather...");
      } else {
        u8g2_display.setFont(u8g2_font_helvB12_tf);
        u8g2_display.setCursor(10, centerY - 12);
        u8g2_display.print("No weather");
        u8g2_display.setCursor(10, centerY + 8);
        u8g2_display.print("Press button");
        u8g2_display.setCursor(10, centerY + 28);
        u8g2_display.print("to refresh");
      }
    } else if (currentWeather.hasError) {
      // Show error message
      u8g2_display.setFont(u8g2_font_helvB12_tf);
      u8g2_display.setCursor(10, 30);
      u8g2_display.print("Weather Error!");

      u8g2_display.setFont(u8g2_font_helvB08_tf);

      // Wrap error message across multiple lines
      const int maxCharsPerLine = 35;
      char* errorMsg = currentWeather.errorMessage;
      int msgLen = strlen(errorMsg);
      int y = 50;

      for (int i = 0; i < msgLen && y < DISPLAY_HEIGHT - 20; i += maxCharsPerLine) {
        char line[maxCharsPerLine + 1];
        int chunkLen = min(maxCharsPerLine, msgLen - i);
        strncpy(line, errorMsg + i, chunkLen);
        line[chunkLen] = '\0';

        u8g2_display.setCursor(10, y);
        u8g2_display.print(line);
        y += 12;
      }

      // Show refresh hint
      u8g2_display.setFont(u8g2_font_profont10_tf);
      u8g2_display.setCursor(10, DISPLAY_HEIGHT - 5);
      u8g2_display.print("Press button to retry");
    } else {
      // === LOCATION HEADING (with word wrap if needed) ===
      u8g2_display.setFont(u8g2_font_helvB14_te);

      // Check if location fits on one line
      int locationWidth = u8g2_display.getUTF8Width(currentWeather.location);
      int maxWidth = DISPLAY_WIDTH - 10;

      int locationBottomY = 19;
      if (locationWidth <= maxWidth) {
        // Fits on one line
        u8g2_display.setCursor(5, 19);
        u8g2_display.print(currentWeather.location);
      } else {
        // Need to split into two lines by words
        char line1[32] = "";
        char line2[32] = "";
        char* token;
        char locationCopy[64];
        strncpy(locationCopy, currentWeather.location, sizeof(locationCopy) - 1);
        locationCopy[sizeof(locationCopy) - 1] = '\0';

        token = strtok(locationCopy, " ");
        bool onSecondLine = false;

        while (token != NULL) {
          char testLine[32];
          snprintf(testLine, sizeof(testLine), "%s%s%s",
                   onSecondLine ? line2 : line1,
                   (onSecondLine ? line2 : line1)[0] ? " " : "",
                   token);

          if (u8g2_display.getUTF8Width(testLine) <= maxWidth) {
            if (onSecondLine) {
              strncat(line2, (line2[0] ? " " : ""), sizeof(line2) - strlen(line2) - 1);
              strncat(line2, token, sizeof(line2) - strlen(line2) - 1);
            } else {
              strncat(line1, (line1[0] ? " " : ""), sizeof(line1) - strlen(line1) - 1);
              strncat(line1, token, sizeof(line1) - strlen(line1) - 1);
            }
          } else {
            if (!onSecondLine) {
              onSecondLine = true;
              strncat(line2, token, sizeof(line2) - strlen(line2) - 1);
            }
          }
          token = strtok(NULL, " ");
        }

        u8g2_display.setCursor(5, 19);
        u8g2_display.print(line1);
        if (line2[0]) {
          u8g2_display.setCursor(5, 36);
          u8g2_display.print(line2);
          locationBottomY = 36;
        }
      }

      // Draw top separator line
      int separatorY = locationBottomY + 6;
      display.drawLine(0, separatorY, DISPLAY_WIDTH, separatorY, GxEPD_BLACK);

      // === CURRENT WEATHER SECTION ===
      int currentWeatherY = separatorY + 4;

      // Draw weather icon (39x39) on the left
      display.drawBitmap(0, currentWeatherY, getWeatherIcon(currentWeather.condition), 39, 39, GxEPD_BLACK);

      // Draw large temperature on the right with °C in smaller font
      u8g2_display.setFont(u8g2_font_helvB24_tn);
      char tempStr[16];
      snprintf(tempStr, sizeof(tempStr), "%.1f", currentWeather.currentTemp / 10.0);
      u8g2_display.setCursor(42, currentWeatherY + 26);
      u8g2_display.print(tempStr);

      // Add °C in smaller font
      u8g2_display.setFont(u8g2_font_helvB10_tf);
      u8g2_display.print("°C");

      // Draw feels like temperature below current temp
      u8g2_display.setFont(u8g2_font_helvB08_tf);
      char feelsStr[24];
      snprintf(feelsStr, sizeof(feelsStr), "Feels like %.1f°C", currentWeather.feelsLike / 10.0);
      u8g2_display.setCursor(42, currentWeatherY + 38);
      u8g2_display.print(feelsStr);

      // === SEGMENTED DETAILS GRID (2x2) ===
      int gridTop = currentWeatherY + 46;
      int gridHeight = 56;
      int cellWidth = DISPLAY_WIDTH / 2;
      int cellHeight = gridHeight / 2;
      const int leftMargin = 3;

      // Format values
      char windStr[16];
      snprintf(windStr, sizeof(windStr), "%.1f %s",
               currentWeather.windSpeed / 10.0, getWindDirection(currentWeather.windDir));

      char humidStr[8];
      snprintf(humidStr, sizeof(humidStr), "%d%%", currentWeather.humidity);

      char rainStr[8];
      snprintf(rainStr, sizeof(rainStr), "%d%%", currentWeather.precipChance);

      // Determine next sun event (sunrise or sunset)
      LocalTime currentLocalTime = getLocalTime();

      // Get current timezone offset (same offset used for GPS time)
      int timezoneOffsetHours = 1; // Default to CET (+1)
      if (currentLocalTime.year > 0) {
        // Use the GPS UTC time to calculate the correct offset
        timezoneOffsetHours = getTimezoneOffset(currentLocalTime.year, currentLocalTime.month,
                                                currentLocalTime.day, gps.time.hour());
      }

      // Apply timezone offset to sunrise/sunset timestamps
      // Unix timestamps are in UTC, we need to add offset to get local time
      time_t sunriseRaw = (time_t)currentWeather.sunrise + (timezoneOffsetHours * 3600);
      time_t sunsetRaw = (time_t)currentWeather.sunset + (timezoneOffsetHours * 3600);

      // Convert adjusted timestamps to local time components
      struct tm sunriseInfo, sunsetInfo;
      gmtime_r(&sunriseRaw, &sunriseInfo);
      gmtime_r(&sunsetRaw, &sunsetInfo);

      // Compare current time with sunrise/sunset times
      int currentMinutes = currentLocalTime.hour * 60 + currentLocalTime.minute;
      int sunriseMinutes = sunriseInfo.tm_hour * 60 + sunriseInfo.tm_min;
      int sunsetMinutes = sunsetInfo.tm_hour * 60 + sunsetInfo.tm_min;

      bool showSunrise = currentMinutes < sunriseMinutes;
      bool showSunset = currentMinutes >= sunriseMinutes && currentMinutes < sunsetMinutes;

      // If we're past sunset, show next day's sunrise
      if (currentMinutes >= sunsetMinutes) {
        showSunrise = true;
        showSunset = false;
      }

      char sunEventLabel[12];
      char sunEventTime[8];

      if (showSunrise) {
        snprintf(sunEventLabel, sizeof(sunEventLabel), "Sunrise");
        snprintf(sunEventTime, sizeof(sunEventTime), "%02d:%02d", sunriseInfo.tm_hour, sunriseInfo.tm_min);
      } else {
        snprintf(sunEventLabel, sizeof(sunEventLabel), "Sunset");
        snprintf(sunEventTime, sizeof(sunEventTime), "%02d:%02d", sunsetInfo.tm_hour, sunsetInfo.tm_min);
      }

      // Top-left: Next sun event (Sunrise or Sunset)
      int labelY = gridTop + 11;
      int valueY = gridTop + 23;
      u8g2_display.setFont(u8g2_font_helvB08_tf);
      u8g2_display.setCursor(leftMargin, labelY);
      u8g2_display.print(sunEventLabel);
      u8g2_display.setFont(u8g2_font_helvB10_tf);
      u8g2_display.setCursor(leftMargin, valueY);
      u8g2_display.print(sunEventTime);

      // Top-right: Wind
      u8g2_display.setFont(u8g2_font_helvB08_tf);
      u8g2_display.setCursor(cellWidth + leftMargin, labelY);
      u8g2_display.print("Wind");
      u8g2_display.setFont(u8g2_font_helvB10_tf);
      u8g2_display.setCursor(cellWidth + leftMargin, valueY);
      u8g2_display.print(windStr);

      // Bottom-left: Humidity
      labelY = gridTop + cellHeight + 11;
      valueY = gridTop + cellHeight + 23;
      u8g2_display.setFont(u8g2_font_helvB08_tf);
      u8g2_display.setCursor(leftMargin, labelY);
      u8g2_display.print("Humidity");
      u8g2_display.setFont(u8g2_font_helvB10_tf);
      u8g2_display.setCursor(leftMargin, valueY);
      u8g2_display.print(humidStr);

      // Bottom-right: Rain Chance
      u8g2_display.setFont(u8g2_font_helvB08_tf);
      u8g2_display.setCursor(cellWidth + leftMargin, labelY);
      u8g2_display.print("Rain");
      u8g2_display.setFont(u8g2_font_helvB10_tf);
      u8g2_display.setCursor(cellWidth + leftMargin, valueY);
      u8g2_display.print(rainStr);

      // Draw grid lines (3 sides + middle dividers)
      display.drawLine(0, gridTop, DISPLAY_WIDTH, gridTop, GxEPD_BLACK);                       // Top line
      display.drawLine(0, gridTop + cellHeight, DISPLAY_WIDTH, gridTop + cellHeight, GxEPD_BLACK); // Middle horizontal
      display.drawLine(cellWidth, gridTop, cellWidth, gridTop + gridHeight, GxEPD_BLACK);      // Middle vertical

      // Draw separator line before hourly forecast (also serves as grid bottom)
      int hourlyTop = gridTop + gridHeight + 4;
      display.drawLine(0, hourlyTop, DISPLAY_WIDTH, hourlyTop, GxEPD_BLACK);

      // === HOURLY FORECAST SECTION (vertical scrolling) ===
      int hourlyHeaderY = hourlyTop + 14;
      u8g2_display.setFont(u8g2_font_helvB12_tr);
      u8g2_display.setCursor(5, hourlyHeaderY);
      u8g2_display.print("Next hours");

      // Draw vertical list of hourly forecasts
      // Calculate available space before status bar
      int statusBarY = DISPLAY_HEIGHT - STATUS_BAR_HEIGHT;
      int contentBottomY = statusBarY;
      if (TOTAL_WEATHER_PAGES > 1) {
        int dotCenterY = statusBarY - WEATHER_PAGE_DOT_CENTER_OFFSET;
        int dotsTopY = dotCenterY - WEATHER_PAGE_DOT_RADIUS;
        contentBottomY = dotsTopY - WEATHER_PAGE_DOT_CONTENT_GAP;
      }
      int availableHeight = contentBottomY - (hourlyHeaderY + 8);

      int itemHeight = 41; // Extra space so 39px icons clear divider lines
      int startY = hourlyHeaderY + 8;
      const int hourlyAfterFirstOffset = 3;

      int hourlyIndexMap[MAX_HOURLY_ENTRIES];
      int validHourlyCount = buildHourlyIndexMap(hourlyIndexMap, MAX_HOURLY_ENTRIES);

      // Calculate how many items actually fit
      int maxVisibleItems = availableHeight / itemHeight;
      int minVisibleItems = min(2, validHourlyCount);
      if (maxVisibleItems < minVisibleItems) {
        maxVisibleItems = minVisibleItems;
      }
      int visibleCapacity = min(maxVisibleItems, HOURLY_ITEMS_VISIBLE);
      int maxOffset = max(0, validHourlyCount - visibleCapacity);
      if (weatherScrollOffset > maxOffset) {
        weatherScrollOffset = maxOffset;
      }
      weatherVisibleCapacity = visibleCapacity;
      int startIdx = weatherScrollOffset;
      int actualVisibleItems = min(visibleCapacity, validHourlyCount - startIdx);

      for (int i = 0; i < actualVisibleItems && (startIdx + i) < validHourlyCount; i++) {
        int hourlyIndex = hourlyIndexMap[startIdx + i];
        HourlyWeatherData& hourly = currentWeather.hourly[hourlyIndex];

        int y = startY + (i * itemHeight);
        if (i > 0) {
          y += hourlyAfterFirstOffset;
        }

        // Don't draw if it would overlap with status bar
        int bottomLimit = contentBottomY + (i > 0 ? hourlyAfterFirstOffset : 0);
        if (y + itemHeight > bottomLimit) break;

        // Draw weather icon (39x39) on the left - but scaled smaller
        // Using a smaller area: draw at 24x24 size
        const unsigned char* icon = getWeatherIcon(hourly.condition);
        // For now, draw full size icon - we'll optimize later if needed
        display.drawBitmap(5, y, icon, 39, 39, GxEPD_BLACK);

        // Time label
        u8g2_display.setFont(u8g2_font_helvB12_tr);
        char hourStr[8];
        snprintf(hourStr, sizeof(hourStr), "%02d:00", hourly.hour);
        u8g2_display.setCursor(50, y + 12);
        u8g2_display.print(hourStr);

        // Temperature
        u8g2_display.setFont(u8g2_font_helvB10_tr);
        char hTempStr[12];
        snprintf(hTempStr, sizeof(hTempStr), "%.1f°C", hourly.temp / 10.0);
        u8g2_display.setCursor(50, y + 24);
        u8g2_display.print(hTempStr);

        // Rain chance
        u8g2_display.setFont(u8g2_font_helvR08_tr);
        char precipStr[12];
        snprintf(precipStr, sizeof(precipStr), "Rain: %d%%", hourly.precipChance);
        u8g2_display.setCursor(50, y + 34);
        u8g2_display.print(precipStr);

        // Draw separator line between items (except after last visible one)
        if (i < actualVisibleItems - 1) {
          display.drawLine(5, y + itemHeight - 1, DISPLAY_WIDTH - 5, y + itemHeight - 1, GxEPD_BLACK);
        }
      }

      // Draw scrollbar if there's more content than can fit on screen
      if (validHourlyCount > visibleCapacity) {
        const int scrollbarX = DISPLAY_WIDTH - 4;  // 4 pixels from right edge
        const int scrollbarWidth = 3;
        const int scrollbarTop = startY;
        const int scrollbarHeight = availableHeight;

        // Calculate scrollbar thumb size and position
        float visibleRatio = (float)visibleCapacity / (float)validHourlyCount;
        int thumbHeight = max(10, (int)(scrollbarHeight * visibleRatio));  // Minimum 10px thumb

        float scrollProgress = (float)weatherScrollOffset / (float)(validHourlyCount - visibleCapacity);
        int thumbY = scrollbarTop + (int)((scrollbarHeight - thumbHeight) * scrollProgress);

        // Draw scrollbar track (outline)
        display.drawRect(scrollbarX - 1, scrollbarTop, scrollbarWidth + 2, scrollbarHeight, GxEPD_BLACK);

        // Draw scrollbar thumb (filled rectangle)
        display.fillRect(scrollbarX, thumbY, scrollbarWidth, thumbHeight, GxEPD_BLACK);
      }
    }

    clearStatusBarExtras();
    drawWeatherPageDots(currentWeatherSubPage, TOTAL_WEATHER_PAGES,
                        DISPLAY_HEIGHT - STATUS_BAR_HEIGHT);
    if (weatherDataReady && !currentWeather.hasError) {
      char updateLabel[24];
      formatWeatherUpdateLabel(updateLabel, sizeof(updateLabel), lastWeatherUpdate);
      if (updateLabel[0] != '\0') {
        u8g2_display.setFont(u8g2_font_helvB08_tf);
        int updateWidth = u8g2_display.getUTF8Width(updateLabel);
        int updateX = (DISPLAY_WIDTH - updateWidth) / 2;
        int updateY = DISPLAY_HEIGHT - STATUS_BAR_HEIGHT - 2;
        u8g2_display.setCursor(updateX, updateY);
        u8g2_display.print(updateLabel);
      }
    }
    drawStatusBar();

    // Draw notification overlay
    drawNotificationOverlay();

  } while (display.nextPage());
}

void updateWeatherPage() {
  if (currentWeatherSubPage == 1) {
    updateRadarPage();
    return;
  }

  // Check if weather data arrived
  if (weatherUpdatePending && weatherDataReady) {
    Serial.println("[WEATHER PAGE] Data arrived! Re-rendering page...");
    weatherUpdatePending = false;
    renderWeatherPage(); // Refresh display with new data
    return;
  }

  // Debug: log state periodically (every 5 seconds)
  static unsigned long lastDebugLog = 0;
  if (millis() - lastDebugLog > 5000) {
    Serial.printf("[WEATHER PAGE] Waiting... pending=%d, ready=%d\n", weatherUpdatePending, weatherDataReady);
    lastDebugLog = millis();
  }

  // Let the centralized status bar handle time/battery/GPS/BLE updates
  updateStatusBar();
}

void handleWeatherEncoder(int delta) {
  markUserActivity();  // Prevent status bar auto-refresh during interaction

  if (currentWeatherSubPage != 0) {
    handleRadarEncoder(delta);
    return;
  }

  // Only allow scrolling if we have more items than can fit on screen
  if (!weatherDataReady) return;
  int validHourlyCount = countValidHourlyEntries();
  int visibleCapacity = min(weatherVisibleCapacity, HOURLY_ITEMS_VISIBLE);
  if (visibleCapacity < 1) {
    visibleCapacity = 1;
  }
  if (validHourlyCount <= visibleCapacity) return;

  // Scroll through hourly forecast vertically
  weatherScrollOffset += delta;

  // Clamp scroll offset to valid range
  if (weatherScrollOffset < 0) {
    weatherScrollOffset = 0;
  }

  // Don't scroll past the end
  int maxOffset = max(0, validHourlyCount - visibleCapacity);
  if (weatherScrollOffset > maxOffset) {
    weatherScrollOffset = maxOffset;
  }

  renderWeatherPage();
}

void handleWeatherButton() {
  markUserActivity();  // Prevent status bar auto-refresh during interaction

  if (currentWeatherSubPage == 1) {
    handleRadarButton();
    return;
  }

  Serial.println("Weather button pressed - IGNORED (use Weather Options to refresh)");

  // Encoder button is disabled on weather page to prevent accidental refresh
  // User should use Weather Options page to refresh weather
}

void handleWeatherNextPage() {
  markUserActivity();

  currentWeatherSubPage = (currentWeatherSubPage + 1) % TOTAL_WEATHER_PAGES;
  if (currentWeatherSubPage == 0) {
    initWeatherPage();
    renderWeatherPage();
  } else {
    initRadarPage();
    renderRadarPage();
  }
}

void handleWeatherOptions() {
  if (currentWeatherSubPage == 1) {
    handleRadarOptions();
    return;
  }

  // Navigate to weather options page
  navigateToPage(PAGE_WEATHER_OPTIONS);
}

#endif
