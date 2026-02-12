#ifndef PAGE_MAIN_MENU_H
#define PAGE_MAIN_MENU_H

#include <GxEPD2_BW.h>
#include <U8g2_for_Adafruit_GFX.h>
#include "bitmaps.h"
#include "timezone.h"
#include "notification_system.h"
#include "status_bar.h"

extern GxEPD2_BW<GxEPD2_290_BS, GxEPD2_290_BS::HEIGHT> display;
extern U8G2_FOR_ADAFRUIT_GFX u8g2_display;
extern const int DISPLAY_WIDTH;
extern const int DISPLAY_HEIGHT;
extern double currentLat;
extern double currentLon;
extern bool gpsValid;
extern TinyGPSPlus gps;

// Menu app structure
struct MenuApp {
  const char* name;
  const uint8_t* icon;
  int pageId; // Maps to PageType enum
};

// Menu configuration: 2 columns × 4 rows = 8 apps
const int MENU_COLS = 2;
const int MENU_ROWS = 4;
const int MENU_APPS_COUNT = 8;

// App grid layout
const int ICON_SPACING = 8;
const int LABEL_HEIGHT = 14;
const int APP_HEIGHT = ICON_SIZE + ICON_SPACING + LABEL_HEIGHT;
// STATUS_BAR_HEIGHT is now defined in status_bar.h

MenuApp menuApps[MENU_APPS_COUNT] = {
  {"Map", ICON_MAP, 1},              // PAGE_MAP = 1
  {"Speed", ICON_SPEEDOMETER, 2},     // PAGE_SPEEDOMETER = 2
  {"Phone", ICON_PHONE, 3},           // PAGE_PHONE_APP = 3
  {"Weather", ICON_WEATHER, 4},       // PAGE_WEATHER = 4
  {"Tracker", ICON_TRACKER, 9},       // PAGE_TRACKER = 9
  {"Info", ICON_INFO, 6},             // PAGE_INFO = 6
  {"Mines", ICON_GAMES, 5},           // PAGE_GAMES = 5
  {"Snake", ICON_SNAKE, PAGE_SNAKE}   // PAGE_SNAKE = 13
};

int selectedAppIndex = 0;
int selectedPageId = 0;  // Will be set when button is pressed

// Smart scrolling state
unsigned long lastScrollTime = 0;
const unsigned long SCROLL_DEBOUNCE_MS = 1;  // Wait 1ms after last scroll before redrawing
bool needsRedraw = false;

void initMainMenu() {
  selectedAppIndex = 0;
}

void renderMainMenu() {
  // Always use partial window for fast updates
  display.setPartialWindow(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
  display.firstPage();
  
  do {
    display.fillScreen(GxEPD_WHITE);
    
    // Calculate grid dimensions
    int gridHeight = DISPLAY_HEIGHT - STATUS_BAR_HEIGHT;
    int appWidth = DISPLAY_WIDTH / MENU_COLS;
    int appHeight = gridHeight / MENU_ROWS;
    
    // Setup text rendering
    u8g2_display.setFontMode(1);  // Transparent mode
    u8g2_display.setForegroundColor(GxEPD_BLACK);
    u8g2_display.setBackgroundColor(GxEPD_WHITE);
    u8g2_display.setFont(u8g2_font_helvB10_tf);  // Simple, reliable font
    
    // Draw each app
    for (int i = 0; i < MENU_APPS_COUNT; i++) {
      int row = i / MENU_COLS;
      int col = i % MENU_COLS;
      
      int appX = col * appWidth;
      int appY = row * appHeight;
      
      // Center icon in the app cell
      int iconX = appX + (appWidth - ICON_SIZE) / 2;
      int iconY = appY + ICON_SPACING;
      
      // Draw icon
      drawIcon(menuApps[i].icon, iconX, iconY, display);
      
      // Draw label below icon
      int textWidth = u8g2_display.getUTF8Width(menuApps[i].name);
      int labelX = appX + (appWidth - textWidth) / 2;
      int labelY = iconY + ICON_SIZE + ICON_SPACING + 8;
      
      u8g2_display.setCursor(labelX, labelY);
      u8g2_display.print(menuApps[i].name);
      
      // Draw selection rectangle around selected app
      if (i == selectedAppIndex) {
        display.drawRect(appX + 2, appY + 2, appWidth - 4, appHeight - 4, GxEPD_BLACK);
        display.drawRect(appX + 3, appY + 3, appWidth - 6, appHeight - 6, GxEPD_BLACK);
      }
    }
    
    // Draw status bar
    drawStatusBar();

    // Draw notification overlay
    drawNotificationOverlay();

  } while (display.nextPage());
}

void handleMainMenuEncoder(int delta) {
  // Mark user activity to prevent status bar auto-refresh during scrolling
  markUserActivity();

  // Move selection immediately in memory
  selectedAppIndex += delta;

  // Wrap around
  if (selectedAppIndex < 0) {
    selectedAppIndex = MENU_APPS_COUNT - 1;
  } else if (selectedAppIndex >= MENU_APPS_COUNT) {
    selectedAppIndex = 0;
  }

  // Mark that we need to redraw, but don't do it yet
  needsRedraw = true;
  lastScrollTime = millis();
}

void updateMainMenu() {
  // Check if we need to redraw after scrolling has stopped
  if (needsRedraw && (millis() - lastScrollTime >= SCROLL_DEBOUNCE_MS)) {
    needsRedraw = false;
    renderMainMenu();
    return;
  }

  // Let the status bar handle its own smart refresh
  // It will check for time, battery, BLE, GPS changes and update if needed
  updateStatusBar();
}

void handleMainMenuButton();  // Forward declaration for main

void handleMainMenuButton() {
  // Mark user activity
  markUserActivity();

  // If there's a pending redraw, force it now before selection
  // This ensures the user sees the correct selection before navigating
  if (needsRedraw) {
    needsRedraw = false;
    renderMainMenu();
  }

  // Store which page to navigate to
  selectedPageId = menuApps[selectedAppIndex].pageId;
  Serial.printf("Button pressed! selectedAppIndex=%d, pageId=%d\n", selectedAppIndex, selectedPageId);
}

#endif // PAGE_MAIN_MENU_H
