#ifndef PAGE_RADAR_H
#define PAGE_RADAR_H
#include <GxEPD2_BW.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <TinyGPSPlus.h>
#include <time.h>
#include "notification_system.h"
#include "ble_handler.h"
#include "timezone.h"
#include "status_bar.h"

extern GxEPD2_BW<GxEPD2_290_BS, GxEPD2_290_BS::HEIGHT> display;
extern U8G2_FOR_ADAFRUIT_GFX u8g2_display;
extern const int DISPLAY_WIDTH;
extern const int DISPLAY_HEIGHT;
extern bool deviceConnected;
extern TinyGPSPlus gps;
extern int getTimezoneOffset(int year, int month, int day, int hour);
extern LocalTime getLocalTime();

// Radar page state
bool radarUpdatePending = false;
bool radarOverlayEnabled = false;
bool radarMapLightenEnabled = false;
unsigned long radarLastRequestTime = 0;
const uint8_t RADAR_DEFAULT_ZOOM = 9;
int radarZoomLevel = RADAR_DEFAULT_ZOOM;
int radarFrameOffset = 0;
const unsigned long RADAR_ANIMATION_FRAME_DELAY_MS = 500;
bool radarAnimationActive = false;
int radarAnimationEndOffset = 0;
int radarAnimationCurrentOffset = 0;
unsigned long radarAnimationLastFrameTime = 0;

int findNextReadyRadarFrameOffset(int startOffset, int endOffset) {
  if (startOffset > endOffset) {
    return endOffset + 1;
  }
  for (int offset = startOffset; offset <= endOffset; offset++) {
    if (isRadarFrameReady(offset)) {
      return offset;
    }
  }
  return endOffset + 1;
}

void drawRadarMapContent() {
  int previousZoom = zoomLevel;
  int previousRotation = mapRotation;

  zoomLevel = radarZoomLevel;
  mapRotation = 0;

  // Match map page display metrics
  currentInfoBarHeight = MAP_INFO_BAR_HEIGHT_NORMAL + WEATHER_STATUS_BAR_EXTRA_HEIGHT;
  MAP_DISPLAY_HEIGHT = DISPLAY_HEIGHT - currentInfoBarHeight;
  currentCenterY = MAP_DISPLAY_HEIGHT / 2;

  // Use scrubbed position if navigation is active
  double centerLat = currentLat;
  double centerLon = currentLon;
  if (scrubOffsetMeters != 0 && navigationActive) {
    centerLat = scrubLat;
    centerLon = scrubLon;
  }

  calculateVisibleTiles(centerLat, centerLon, radarZoomLevel);

  const uint8_t* overlayFrame = getRadarFrameData(radarFrameOffset);
  radarMapLightenEnabled = radarOverlayEnabled && overlayFrame != nullptr;
  for (int i = 0; i < tileCount; i++) {
    int tileX = tilesToRender[i].tileX;
    int tileY = tilesToRender[i].tileY;
    int screenX = tilesToRender[i].screenX;
    int screenY = tilesToRender[i].screenY;

    if (!loadAndRenderTile(tileX, tileY, radarZoomLevel, screenX, screenY)) {
      Serial.println("Radar map: Tile not found on SD card");
    }
  }
  radarMapLightenEnabled = false;

  drawRadarOverlay(overlayFrame);

  if (navigationActive && navigationTrack != nullptr) {
    drawNavigationRoute(centerLat, centerLon);
  }

  if (navigationActive) {
    if (scrubOffsetMeters != 0) {
      display.drawCircle(CENTER_X, currentCenterY, 8, GxEPD_BLACK);
      display.drawCircle(CENTER_X, currentCenterY, 7, GxEPD_BLACK);
      display.fillCircle(CENTER_X, currentCenterY, 2, GxEPD_BLACK);
      display.drawLine(CENTER_X - 12, currentCenterY, CENTER_X - 10, currentCenterY, GxEPD_BLACK);
      display.drawLine(CENTER_X + 10, currentCenterY, CENTER_X + 12, currentCenterY, GxEPD_BLACK);
      display.drawLine(CENTER_X, currentCenterY - 12, CENTER_X, currentCenterY - 10, GxEPD_BLACK);
      display.drawLine(CENTER_X, currentCenterY + 10, CENTER_X, currentCenterY + 12, GxEPD_BLACK);
    } else {
      drawNavigationArrow(CENTER_X, currentCenterY, display);
    }
  } else {
    drawLocationMarker(CENTER_X, currentCenterY, display);
  }

  zoomLevel = previousZoom;
  mapRotation = previousRotation;
}

void initRadarPage() {
  statusBarState.lastDisplayedMinute = -1; // Force initial clock update
  radarFrameOffset = 0;
  radarOverlayEnabled = true;
  radarAnimationActive = false;

  // Request radar update if connected and no recent data
  if (deviceConnected && !radarUpdatePending) {
    unsigned long timeSinceUpdate = millis() - lastRadarUpdate;
    if (!radarDataReady || timeSinceUpdate > 300000) { // 5 minutes
      clearRadarFrames();
      double requestLat = currentLat;
      double requestLon = currentLon;
      if (scrubOffsetMeters != 0 && navigationActive) {
        requestLat = scrubLat;
        requestLon = scrubLon;
      }
      requestRadarUpdateForLocation(requestLat, requestLon, (uint8_t)radarZoomLevel);
      radarUpdatePending = true;
      radarLastRequestTime = millis();
    }
  }
}

void renderRadarOverlayImage(int yOffset) {
  const uint8_t* frameData = getRadarFrameData(radarFrameOffset);
  if (frameData == nullptr) {
    return;
  }
  const int bytesPerRow = RADAR_IMAGE_WIDTH / 8;

  for (int y = 0; y < RADAR_IMAGE_HEIGHT; y++) {
    int screenY = y + yOffset;
    if (screenY < 0 || screenY >= DISPLAY_HEIGHT - STATUS_BAR_HEIGHT - WEATHER_STATUS_BAR_EXTRA_HEIGHT) {
      continue;
    }

    int rowOffset = y * bytesPerRow;
    for (int x = 0; x < RADAR_IMAGE_WIDTH; x++) {
      int byteIndex = rowOffset + (x / 8);
      uint8_t byteVal = frameData[byteIndex];
      uint8_t bitIndex = 7 - (x % 8);
      bool isWhite = (byteVal >> bitIndex) & 1;

      if (isWhite) {
        continue;
      }

      display.drawPixel(x, screenY, GxEPD_BLACK);
    }
  }
}

const int RADAR_MSG_BOX_MARGIN = 6;
const int RADAR_MSG_BOX_SHADOW = 2;
const int RADAR_MSG_BOX_PADDING_X = 6;
const int RADAR_MSG_BOX_PADDING_Y = 6;
const int RADAR_MSG_MAX_LINES = 5;
const int RADAR_MSG_LINE_LEN = 48;

void drawRadarMessageBoxFrame(int x, int y, int w, int h) {
  display.fillRect(x + RADAR_MSG_BOX_SHADOW, y + RADAR_MSG_BOX_SHADOW, w, h, GxEPD_BLACK);
  display.fillRect(x, y, w, h, GxEPD_WHITE);
  display.drawRect(x, y, w, h, GxEPD_BLACK);
  display.drawRect(x + 1, y + 1, w - 2, h - 2, GxEPD_BLACK);
}

int wrapRadarMessageLines(const char* message, char lines[][RADAR_MSG_LINE_LEN], int maxLines, int maxWidth) {
  if (message == nullptr || message[0] == '\0' || maxLines <= 0) {
    return 0;
  }

  char buffer[RADAR_ERROR_MESSAGE_SIZE];
  strncpy(buffer, message, sizeof(buffer) - 1);
  buffer[sizeof(buffer) - 1] = '\0';

  char currentLine[RADAR_MSG_LINE_LEN] = "";
  int lineCount = 0;
  char* token = strtok(buffer, " ");

  while (token != nullptr && lineCount < maxLines) {
    char testLine[RADAR_MSG_LINE_LEN];
    if (currentLine[0] == '\0') {
      snprintf(testLine, sizeof(testLine), "%s", token);
    } else {
      snprintf(testLine, sizeof(testLine), "%s %s", currentLine, token);
    }

    if (u8g2_display.getUTF8Width(testLine) <= maxWidth) {
      strncpy(currentLine, testLine, sizeof(currentLine) - 1);
      currentLine[sizeof(currentLine) - 1] = '\0';
    } else {
      if (currentLine[0] != '\0') {
        strncpy(lines[lineCount], currentLine, RADAR_MSG_LINE_LEN - 1);
        lines[lineCount][RADAR_MSG_LINE_LEN - 1] = '\0';
        lineCount++;
        currentLine[0] = '\0';
      }
      strncpy(currentLine, token, RADAR_MSG_LINE_LEN - 1);
      currentLine[RADAR_MSG_LINE_LEN - 1] = '\0';
    }

    token = strtok(nullptr, " ");
  }

  if (currentLine[0] != '\0' && lineCount < maxLines) {
    strncpy(lines[lineCount], currentLine, RADAR_MSG_LINE_LEN - 1);
    lines[lineCount][RADAR_MSG_LINE_LEN - 1] = '\0';
    lineCount++;
  }

  return lineCount;
}

void drawRadarStatusBox(const char* line1, const char* line2) {
  const int boxWidth = DISPLAY_WIDTH - (RADAR_MSG_BOX_MARGIN * 2);
  const int lineHeight = 14;
  const int textWidth = boxWidth - (RADAR_MSG_BOX_PADDING_X * 2);
  char wrappedLines[2][RADAR_MSG_LINE_LEN];
  const char* renderLines[2] = {line1, line2};
  int lineCount = 1;
  int boxX = RADAR_MSG_BOX_MARGIN;

  u8g2_display.setFontMode(1);
  u8g2_display.setForegroundColor(GxEPD_BLACK);
  u8g2_display.setBackgroundColor(GxEPD_WHITE);
  u8g2_display.setFont(u8g2_font_helvB10_tf);

  if (line2 != nullptr && line2[0] != '\0') {
    lineCount = 2;
  } else {
    int lineWidth = u8g2_display.getUTF8Width(line1);
    if (lineWidth > textWidth) {
      lineCount = wrapRadarMessageLines(line1, wrappedLines, 2, textWidth);
      if (lineCount < 1) {
        lineCount = 1;
        wrappedLines[0][0] = '\0';
      }
      renderLines[0] = wrappedLines[0];
      renderLines[1] = (lineCount > 1) ? wrappedLines[1] : nullptr;
    }
  }

  int boxHeight = (RADAR_MSG_BOX_PADDING_Y * 2) + (lineHeight * lineCount);
  int boxY = (MAP_DISPLAY_HEIGHT - boxHeight) / 2;

  if (boxY < 2) {
    boxY = 2;
  }

  drawRadarMessageBoxFrame(boxX, boxY, boxWidth, boxHeight);

  int textY = boxY + RADAR_MSG_BOX_PADDING_Y + lineHeight - 2;

  for (int i = 0; i < lineCount; i++) {
    int lineWidth = u8g2_display.getUTF8Width(renderLines[i]);
    int textX = boxX + (boxWidth - lineWidth) / 2;
    u8g2_display.setCursor(textX, textY);
    u8g2_display.print(renderLines[i]);
    textY += lineHeight;
  }
}

void drawRadarErrorBox(const char* errorMessage) {
  const int boxWidth = DISPLAY_WIDTH - (RADAR_MSG_BOX_MARGIN * 2);
  const int titleHeight = 16;
  const int bodyLineHeight = 12;
  const int titleSpacing = 4;
  const int maxBoxHeight = MAP_DISPLAY_HEIGHT - 12;
  const int textWidth = boxWidth - (RADAR_MSG_BOX_PADDING_X * 2);
  int maxBodyLines = (maxBoxHeight - (RADAR_MSG_BOX_PADDING_Y * 2) - titleHeight - titleSpacing) / bodyLineHeight;
  if (maxBodyLines < 1) {
    maxBodyLines = 1;
  }
  if (maxBodyLines > RADAR_MSG_MAX_LINES) {
    maxBodyLines = RADAR_MSG_MAX_LINES;
  }

  u8g2_display.setFontMode(1);
  u8g2_display.setForegroundColor(GxEPD_BLACK);
  u8g2_display.setBackgroundColor(GxEPD_WHITE);
  u8g2_display.setFont(u8g2_font_helvB08_tf);

  char lines[RADAR_MSG_MAX_LINES][RADAR_MSG_LINE_LEN];
  int lineCount = wrapRadarMessageLines(errorMessage, lines, maxBodyLines, textWidth);
  if (lineCount == 0) {
    strncpy(lines[0], "Unknown error", RADAR_MSG_LINE_LEN - 1);
    lines[0][RADAR_MSG_LINE_LEN - 1] = '\0';
    lineCount = 1;
  }

  int boxHeight = (RADAR_MSG_BOX_PADDING_Y * 2) + titleHeight + titleSpacing + (lineCount * bodyLineHeight);
  if (boxHeight > maxBoxHeight) {
    boxHeight = maxBoxHeight;
  }

  int boxX = RADAR_MSG_BOX_MARGIN;
  int boxY = (MAP_DISPLAY_HEIGHT - boxHeight) / 2;

  if (boxY < 2) {
    boxY = 2;
  }

  drawRadarMessageBoxFrame(boxX, boxY, boxWidth, boxHeight);

  const char* title = "Radar Error!";
  u8g2_display.setFont(u8g2_font_helvB12_tf);
  int titleWidth = u8g2_display.getUTF8Width(title);
  int titleX = boxX + (boxWidth - titleWidth) / 2;
  int titleY = boxY + RADAR_MSG_BOX_PADDING_Y + titleHeight - 2;
  u8g2_display.setCursor(titleX, titleY);
  u8g2_display.print(title);

  u8g2_display.setFont(u8g2_font_helvB08_tf);
  int textY = titleY + titleSpacing + bodyLineHeight;
  int textX = boxX + RADAR_MSG_BOX_PADDING_X;
  for (int i = 0; i < lineCount; i++) {
    u8g2_display.setCursor(textX, textY);
    u8g2_display.print(lines[i]);
    textY += bodyLineHeight;
  }
}

void renderRadarPage() {
  display.setPartialWindow(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
  display.firstPage();

  do {
    display.fillScreen(GxEPD_WHITE);

    u8g2_display.setFontMode(1);
    u8g2_display.setForegroundColor(GxEPD_BLACK);
    u8g2_display.setBackgroundColor(GxEPD_WHITE);

    drawRadarMapContent();

    if (!radarDataReady) {
      if (!deviceConnected) {
        drawRadarStatusBox("Connect to phone", nullptr);
      } else if (radarUpdatePending) {
        drawRadarStatusBox("Loading radar...", nullptr);
      } else {
        drawRadarStatusBox("No radar data", nullptr);
      }
    } else if (radarHasError) {
      drawRadarErrorBox(radarErrorMessage);
    } else if (radarOverlayEnabled && !isRadarFrameReady(radarFrameOffset)) {
      drawRadarStatusBox("Loading frame...", nullptr);
    }

    setStatusBarPageDots(1, 2);
    int stepMinutes = radarFrameStepMinutes > 0 ? radarFrameStepMinutes : RADAR_FRAME_STEP_DEFAULT_MINUTES;
    setStatusBarTimeline(RADAR_MAX_PAST_FRAMES, RADAR_MAX_FUTURE_FRAMES, radarFrameOffset, stepMinutes);
    int frameMinutes = 0;
    if (getRadarFrameLocalMinutes(radarFrameOffset, &frameMinutes)) {
      setStatusBarTileBaseTime(frameMinutes, true);
    } else {
      setStatusBarTileBaseTime(0, false);
    }
    setStatusBarTileTime(radarFrameOffset, stepMinutes);
    if (radarDataReady && !radarHasError) {
      setStatusBarUpdateAge(lastRadarUpdate);
    } else {
      clearStatusBarUpdateAge();
    }

    drawStatusBarExtras();
    drawStatusBar();
    drawNotificationOverlay();

  } while (display.nextPage());
}

void updateRadarPage() {
  if (radarUpdatePending && radarDataReady) {
    Serial.println("[RADAR PAGE] Data arrived! Re-rendering page...");
    radarUpdatePending = false;
    radarFramesUpdated = false;
    renderRadarPage();
    return;
  }

  if (radarFramesUpdated) {
    radarFramesUpdated = false;
    if (radarHasError) {
      radarUpdatePending = false;
    }
    renderRadarPage();
    return;
  }

  if (radarAnimationActive) {
    unsigned long now = millis();
    if (now - radarAnimationLastFrameTime >= RADAR_ANIMATION_FRAME_DELAY_MS) {
      int nextOffset = findNextReadyRadarFrameOffset(radarAnimationCurrentOffset + 1,
                                                     radarAnimationEndOffset);
      if (nextOffset > radarAnimationEndOffset) {
        radarAnimationActive = false;
        return;
      }
      radarAnimationCurrentOffset = nextOffset;
      radarFrameOffset = nextOffset;
      radarAnimationLastFrameTime = now;
      renderRadarPage();
    }
    return;
  }

  static unsigned long lastDebugLog = 0;
  if (millis() - lastDebugLog > 5000) {
    Serial.printf("[RADAR PAGE] Waiting... pending=%d, ready=%d\n", radarUpdatePending, radarDataReady);
    lastDebugLog = millis();
  }

  updateStatusBar();
}

void handleRadarButton() {
  markUserActivity();
  radarAnimationActive = false;

  if (!radarDataReady) {
    if (!radarUpdatePending && deviceConnected) {
      clearRadarFrames();
      double requestLat = currentLat;
      double requestLon = currentLon;
      if (scrubOffsetMeters != 0 && navigationActive) {
        requestLat = scrubLat;
        requestLon = scrubLon;
      }
      requestRadarUpdateForLocation(requestLat, requestLon, (uint8_t)radarZoomLevel);
      radarUpdatePending = true;
      radarLastRequestTime = millis();
      renderRadarPage();
    }
    return;
  }

  radarOverlayEnabled = !radarOverlayEnabled;
  renderRadarPage();
}

void handleRadarOptions() {
  if (radarAnimationActive) {
    radarAnimationActive = false;
    renderRadarPage();
    return;
  }

  if (!radarDataReady || radarHasError) {
    return;
  }

  int minOffset = getRadarMinFrameOffset();
  int maxOffset = getRadarMaxFrameOffset();
  if (minOffset > maxOffset) {
    int temp = minOffset;
    minOffset = maxOffset;
    maxOffset = temp;
  }

  int firstOffset = findNextReadyRadarFrameOffset(minOffset, maxOffset);
  if (firstOffset > maxOffset) {
    return;
  }

  radarOverlayEnabled = true;
  radarAnimationActive = true;
  radarAnimationEndOffset = maxOffset;
  radarAnimationCurrentOffset = firstOffset;
  radarFrameOffset = firstOffset;
  radarAnimationLastFrameTime = millis();
  renderRadarPage();
}

void handleRadarEncoder(int delta) {
  if (delta == 0) return;

  radarAnimationActive = false;
  radarFrameOffset += delta;
  if (radarFrameOffset < -RADAR_MAX_PAST_FRAMES) {
    radarFrameOffset = -RADAR_MAX_PAST_FRAMES;
  } else if (radarFrameOffset > RADAR_MAX_FUTURE_FRAMES) {
    radarFrameOffset = RADAR_MAX_FUTURE_FRAMES;
  }

  renderRadarPage();
}

#endif
