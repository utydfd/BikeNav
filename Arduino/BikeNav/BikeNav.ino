#include <GxEPD2_BW.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <HardwareSerial.h>
#include <TinyGPS++.h>
#include <SPI.h>
#include <SD.h>
#include <time.h>
#include <sys/time.h>

// --- PAGE MANAGEMENT ENUM (must be before page includes) ---
enum PageType {
  PAGE_MAIN_MENU,
  PAGE_MAP,
  PAGE_SPEEDOMETER,
  PAGE_PHONE_APP,
  PAGE_WEATHER,
  PAGE_GAMES,
  PAGE_INFO,
  PAGE_SHUTDOWN,
  PAGE_SETTINGS,
  PAGE_TRACKER,
  PAGE_RECORDING,
  PAGE_RECORDING_OPTIONS,
  PAGE_WEATHER_OPTIONS,
  PAGE_SNAKE
};

// Forward declaration of navigateToPage (needed by page headers)
void navigateToPage(PageType page);

// Include all page headers
#include "timezone.h"
#include "bitmaps.h"
#include "tile_cache.h"
#include "ble_handler.h"
#include "battery_manager.h"
#include "notification_system.h"
#include "power_manager.h"
#include "status_bar.h"
#include "page_main_menu.h"
#include "page_map.h"
#include "page_speedometer.h"
#include "page_phone_app.h"
#include "page_weather.h"
#include "page_radar.h"
#include "page_weather_options.h"
#include "page_games.h"
#include "page_snake.h"
#include "page_info.h"
#include "page_settings.h"
#include "page_tracker.h"
#include "page_recording.h"
#include "page_recording_options.h"

// --- E-PAPER DISPLAY CONFIGURATION ---
#define EPD_CS_PIN 10
#define EPD_DC_PIN 1
#define EPD_RES_PIN 2
#define EPD_BUSY_PIN 3

// --- SD CARD CONFIGURATION ---
#define SD_CS_PIN 4

GxEPD2_BW<GxEPD2_290_BS, GxEPD2_290_BS::HEIGHT> display(GxEPD2_290_BS(EPD_CS_PIN, EPD_DC_PIN, EPD_RES_PIN, EPD_BUSY_PIN));
U8G2_FOR_ADAFRUIT_GFX u8g2_display;

// --- BATTERY MANAGER ---
BatteryManager batteryManager;

// --- GPS CONFIGURATION ---
#define RX1_PIN 8
#define TX1_PIN 9
HardwareSerial GPS_Serial(1);
TinyGPSPlus gps;

// --- TIMEZONE CONFIGURATION ---
// GPS returns UTC time. Automatic DST adjustment for Czech Republic.
// Winter (CET): UTC+1, Summer (CEST): UTC+2
// DST period: Last Sunday of March 2:00 AM to Last Sunday of October 3:00 AM

// Helper function to find the last Sunday of a given month and year
int getLastSundayOfMonth(int year, int month) {
  // Days in month
  const int daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  bool isLeapYear = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
  int lastDay = daysInMonth[month - 1];
  if (month == 2 && isLeapYear) {
    lastDay = 29;
  }

  // Calculate day of week for the last day of the month
  // Using Zeller's algorithm
  int q = lastDay;
  int m = month;
  int y = year;

  // Zeller's algorithm: for January and February, count them as months 13 and 14 of the previous year
  if (m < 3) {
    m += 12;
    y--;
  }

  int h = (q + (13 * (m + 1)) / 5 + y + y / 4 - y / 100 + y / 400) % 7;
  // h: 0=Saturday, 1=Sunday, 2=Monday, ..., 6=Friday

  // Find how many days to go back to reach Sunday (h=1)
  int daysBack = (h + 6) % 7;  // Days to go back from last day to last Sunday

  return lastDay - daysBack;
}

// Check if DST is active for Czech Republic
// DST: Last Sunday of March 2:00 UTC to Last Sunday of October 3:00 UTC
bool isDSTActive(int year, int month, int day, int hour) {
  int marchLastSunday = getLastSundayOfMonth(year, 3);
  int octoberLastSunday = getLastSundayOfMonth(year, 10);

  // Before March or after October -> winter time
  if (month < 3 || month > 10) {
    return false;
  }

  // April to September -> summer time
  if (month > 3 && month < 10) {
    return true;
  }

  // March: check if we're past the last Sunday at 2:00 AM
  if (month == 3) {
    if (day < marchLastSunday) {
      return false;
    } else if (day == marchLastSunday) {
      return hour >= 2;  // DST starts at 2:00 AM UTC
    } else {
      return true;
    }
  }

  // October: check if we're before the last Sunday at 3:00 AM
  if (month == 10) {
    if (day < octoberLastSunday) {
      return true;
    } else if (day == octoberLastSunday) {
      return hour < 3;  // DST ends at 3:00 AM UTC
    } else {
      return false;
    }
  }

  return false;
}

// Get timezone offset based on DST status
int getTimezoneOffset(int year, int month, int day, int hour) {
  return isDSTActive(year, month, day, hour) ? 2 : 1;  // +2 for summer, +1 for winter
}

// Convert GPS UTC time to local time with automatic DST adjustment
LocalTime getLocalTime() {
  LocalTime localTime;

  if (!gps.time.isValid() || !gps.date.isValid()) {
    // Return zeros if GPS time not available
    localTime.hour = 0;
    localTime.minute = 0;
    localTime.second = 0;
    localTime.day = 0;
    localTime.month = 0;
    localTime.year = 0;
    return localTime;
  }

  // Start with GPS UTC time
  localTime.hour = gps.time.hour();
  localTime.minute = gps.time.minute();
  localTime.second = gps.time.second();
  localTime.day = gps.date.day();
  localTime.month = gps.date.month();
  localTime.year = gps.date.year();

  // Calculate timezone offset based on UTC date/time for automatic DST
  int timezoneOffset = getTimezoneOffset(localTime.year, localTime.month, localTime.day, localTime.hour);
  localTime.hour += timezoneOffset;

  // Handle hour overflow (next day)
  if (localTime.hour >= 24) {
    localTime.hour -= 24;
    localTime.day++;

    // Days in month lookup (non-leap year, February handled separately)
    const int daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

    // Check for leap year for February
    bool isLeapYear = (localTime.year % 4 == 0 && localTime.year % 100 != 0) || (localTime.year % 400 == 0);
    int maxDays = daysInMonth[localTime.month - 1];
    if (localTime.month == 2 && isLeapYear) {
      maxDays = 29;
    }

    // Handle day overflow (next month)
    if (localTime.day > maxDays) {
      localTime.day = 1;
      localTime.month++;

      // Handle month overflow (next year)
      if (localTime.month > 12) {
        localTime.month = 1;
        localTime.year++;
      }
    }
  }
  // Handle hour underflow (previous day)
  else if (localTime.hour < 0) {
    localTime.hour += 24;
    localTime.day--;

    // Handle day underflow (previous month)
    if (localTime.day < 1) {
      localTime.month--;

      // Handle month underflow (previous year)
      if (localTime.month < 1) {
        localTime.month = 12;
        localTime.year--;
      }

      // Set to last day of previous month
      const int daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
      bool isLeapYear = (localTime.year % 4 == 0 && localTime.year % 100 != 0) || (localTime.year % 400 == 0);
      localTime.day = daysInMonth[localTime.month - 1];
      if (localTime.month == 2 && isLeapYear) {
        localTime.day = 29;
      }
    }
  }

  return localTime;
}

// --- INPUT PINS ---
#define CLK_PIN 5
#define DT_PIN  7
#define SW_PIN  6       // Rotary encoder button
#define BACK_PIN 47     // Back button
#define NEXT_PAGE_PIN 14 // Next page button
#define SETTINGS_PIN 15 // Settings button
#define OPTIONS_PIN 18  // Options button
#define BUTTON_DEBOUNCE 100  // Debounce time for faster mode changes
const unsigned long OPTIONS_RELEASE_DEBOUNCE = 60;

// --- GPS STATE ---
// Default if no GPS signal
double currentLat = 50.102382;
double currentLon = 14.392353;

bool gpsValid = false;
unsigned long lastGPSUpdate = 0;

// --- SD CARD STATE ---
bool sdCardPresent = false;
uint64_t sdCardSize = 0;  // Size in MB

// --- TRIP/NAVIGATION STATE ---
bool navigationActive = false;
bool tripRecording = false;

// --- DISPLAY DIMENSIONS ---
const int DISPLAY_WIDTH = 128;
const int DISPLAY_HEIGHT = 296;

// --- PAGE MANAGEMENT VARIABLES ---
PageType currentPage = PAGE_MAIN_MENU;
PageType previousPage = PAGE_MAIN_MENU;
bool settingsOpen = false;
bool skipPageInit = false;  // Skip page initialization when returning from settings
unsigned long lastPageNavigation = 0;  // Timestamp of last page change
const unsigned long PAGE_NAV_COOLDOWN = 300;  // Cooldown period after page navigation (ms)

// --- SETTINGS PANEL STATE ---
// Note: SettingsPanelState enum is defined in page_settings.h
SettingsPanelState settingsPanelState = SETTINGS_CLOSED;

// --- HARDWARE CONTROL STATE ---
bool backlightEnabled = false;   // Backlight (pin 38) - default OFF
bool gpsEnabled = true;          // GPS power (pin 17) - default ON
bool bluetoothEnabled = true;    // Bluetooth - default ON

// Forward declarations (navigateToPage already declared above before includes)
void handleBack();
void handleSettings();
void handleNextPage();
bool handleSpeedometerBack();
void handleSpeedometerOptions();
bool handleMapBack();
bool handleInfoBack();

// External from page_main_menu.h
extern int selectedPageId;

// --- INPUT STATE ---
volatile bool encoderChanged = false;
volatile int encoderDelta = 0;
volatile bool buttonPressed = false;
volatile bool backPressed = false;
volatile bool nextPagePressed = false;
volatile bool settingsPressed = false;
volatile bool optionsPressed = false;
volatile bool chargingStateChanged = false;
volatile unsigned long lastButtonTime = 0;
volatile unsigned long lastBackTime = 0;
volatile unsigned long lastNextPageTime = 0;
volatile unsigned long lastSettingsTime = 0;
volatile unsigned long lastOptionsTime = 0;
volatile unsigned long lastOptionsReleaseTime = 0;
volatile unsigned long lastChargingTime = 0;

// Button state tracking (prevents double-press when holding button)
volatile bool waitingForButtonRelease = false;  // True after processing encoder press, waiting for physical release
volatile bool waitingForOptionsRelease = false; // True after processing options press, waiting for physical release

// --- INTERRUPT SERVICE ROUTINES ---
/* Rotary encoder interrupt routine based on Oleg Mazurov's code
 * https://chome.nerpa.tech/mcu/rotary-encoder-interrupt-service-routine-for-avr-micros/
 * Updates encoderDelta when encoder has rotated a full detent (4 steps)
 */
void IRAM_ATTR encoderISR() {
  static uint8_t old_AB = 3;  // Lookup table index
  static int8_t encval = 0;   // Encoder value
  static const int8_t enc_states[] = {0,-1,1,0,1,0,0,-1,-1,0,0,1,0,1,-1,0}; // Lookup table

  old_AB <<= 2;  // Remember previous state

  if (digitalRead(CLK_PIN)) old_AB |= 0x02;  // Add current state of pin A (CLK)
  if (digitalRead(DT_PIN)) old_AB |= 0x01;   // Add current state of pin B (DT)

  encval += enc_states[(old_AB & 0x0f)];

  // Update counter if encoder has rotated a full indent, that is at least 4 steps
  if (encval > 3) {        // Four steps forward
    encoderDelta += 1;     // ACCUMULATE instead of overwrite - allows fast rotation
    encoderChanged = true;
    encval = 0;
  }
  else if (encval < -3) {  // Four steps backward
    encoderDelta += -1;    // ACCUMULATE instead of overwrite - allows fast rotation
    encoderChanged = true;
    encval = 0;
  }
}

void IRAM_ATTR buttonISR() {
  unsigned long currentTime = millis();
  if (currentTime - lastButtonTime > BUTTON_DEBOUNCE) {
    if (digitalRead(SW_PIN) == LOW) {
      buttonPressed = true;
      lastButtonTime = currentTime;
    }
  }
}

void IRAM_ATTR backISR() {
  unsigned long currentTime = millis();
  if (currentTime - lastBackTime > BUTTON_DEBOUNCE) {
    if (digitalRead(BACK_PIN) == LOW) {
      backPressed = true;
      lastBackTime = currentTime;
    }
  }
}

void IRAM_ATTR nextPageISR() {
  unsigned long currentTime = millis();
  if (currentTime - lastNextPageTime > BUTTON_DEBOUNCE) {
    if (digitalRead(NEXT_PAGE_PIN) == LOW) {
      nextPagePressed = true;
      lastNextPageTime = currentTime;
    }
  }
}

void IRAM_ATTR settingsISR() {
  unsigned long currentTime = millis();
  if (currentTime - lastSettingsTime > BUTTON_DEBOUNCE) {
    if (digitalRead(SETTINGS_PIN) == LOW) {
      settingsPressed = true;
      lastSettingsTime = currentTime;
    }
  }
}

void IRAM_ATTR optionsISR() {
  unsigned long currentTime = millis();
  if (digitalRead(OPTIONS_PIN) == LOW) {
    if (!waitingForOptionsRelease && currentTime - lastOptionsTime > BUTTON_DEBOUNCE) {
      optionsPressed = true;
      lastOptionsTime = currentTime;
    }
  } else {
    lastOptionsReleaseTime = currentTime;
  }
}

void IRAM_ATTR chargingISR() {
  unsigned long currentTime = millis();
  // Debounce charging state changes (500ms to avoid noise)
  if (currentTime - lastChargingTime > 500) {
    chargingStateChanged = true;
    lastChargingTime = currentTime;
  }
}

// --- PAGE NAVIGATION ---
void navigateToPage(PageType page) {
  if (page == PAGE_TRACKER && isRecording) {
    page = PAGE_RECORDING;
  }

  previousPage = currentPage;
  currentPage = page;
  lastPageNavigation = millis();  // Record page navigation time for cooldown

  clearStatusBarExtras();

  // Check if we should skip initialization (e.g., returning from settings)
  bool shouldInit = !skipPageInit;
  if (skipPageInit) {
    skipPageInit = false;  // Reset flag after using it
  }

  // Initialize and render the page
  switch (currentPage) {
    case PAGE_MAIN_MENU:
      if (shouldInit) initMainMenu();
      renderMainMenu();
      break;
    case PAGE_MAP:
      if (shouldInit) initMapPage();
      renderMapPage();
      break;
    case PAGE_SPEEDOMETER:
      if (shouldInit) initSpeedometerPage();
      renderSpeedometerPage();
      break;
    case PAGE_PHONE_APP:
      if (shouldInit) initPhoneAppPage();
      renderPhoneAppPage();
      break;
    case PAGE_WEATHER:
      if (shouldInit) initWeatherPage();
      renderWeatherPage();
      break;
    case PAGE_WEATHER_OPTIONS:
      if (shouldInit) initWeatherOptionsPage();
      renderWeatherOptionsPage();
      break;
    case PAGE_GAMES:
      if (shouldInit) initGamesPage();
      renderGamesPage();
      break;
    case PAGE_INFO:
      if (shouldInit) initInfoPage();
      renderInfoPage();
      break;
    case PAGE_SETTINGS:
      if (shouldInit) initSettingsPage();
      renderSettingsPage();
      break;
    case PAGE_TRACKER:
      if (shouldInit) initTrackerPage();
      renderTrackerPage();
      break;
    case PAGE_RECORDING:
      if (shouldInit) initRecordingPage();
      renderRecordingPage();
      break;
    case PAGE_RECORDING_OPTIONS:
      if (shouldInit) initRecordingOptionsPage();
      renderRecordingOptionsPage();
      break;
    case PAGE_SNAKE:
      if (shouldInit) initSnakePage();
      renderSnakePage();
      break;
    case PAGE_SHUTDOWN:
      // Render informative shutdown screen with battery and trip stats
      // The e-ink display will retain this image without power
      renderShutdownScreen();
      delay(1500);  // Give user time to see the screen

      // Enter deep sleep with proper power management
      goToDeepSleep();
      break;
  }
}

void handleBack() {
  if (settingsOpen) {
    // Check if settings page wants to handle the back button (e.g., dismiss dialogs)
    if (handleSettingsBack()) {
      return;  // Settings page handled it (dismissed dialog)
    }

    // Handle back button in settings based on current state
    if (settingsPanelState == SETTINGS_QUICK_SETTINGS) {
      // From Quick Settings -> go back to Notifications
      settingsPanelState = SETTINGS_NOTIFICATIONS;
      renderSettingsPage();
    } else if (settingsPanelState == SETTINGS_NOTIFICATIONS) {
      // From Notifications -> close settings completely
      settingsOpen = false;
      settingsPanelState = SETTINGS_CLOSED;
      skipPageInit = true;  // Preserve page state when returning from settings
      navigateToPage(previousPage);
    }
  } else if (currentPage == PAGE_SPEEDOMETER && handleSpeedometerBack()) {
    // Speedometer page handled the back button (dismissed GPS dialog)
    return;
  } else if (currentPage == PAGE_MAP && handleMapBack()) {
    // Map page handled the back button (navigated between sub-pages)
    return;
  } else if (currentPage == PAGE_INFO && handleInfoBack()) {
    // Info page handled the back button (dismissed format dialog)
    return;
  } else if (currentPage == PAGE_GAMES && handleGamesBack()) {
    // Games page handled the back button (saved game state)
    return;
  } else if (currentPage == PAGE_SNAKE && handleSnakeBack()) {
    // Snake page handled the back button
    return;
  } else if (currentPage == PAGE_WEATHER_OPTIONS && handleWeatherOptionsBack()) {
    // Weather options page handled the back button (return to weather page)
    return;
  } else if (currentPage == PAGE_RECORDING && handleRecordingBack()) {
    // Recording page handled the back button (e.g., opened options)
    return;
  } else if (currentPage == PAGE_RECORDING_OPTIONS) {
    // Recording options page may handle back button (e.g., closing popup)
    if (handleRecordingOptionsBack()) {
      // Page handled it (popup was closed)
      return;
    }
    // If not handled, return to recording page
    navigateToPage(PAGE_RECORDING);
    return;
  } else if (currentPage != PAGE_MAIN_MENU) {
    // Return to main menu
    navigateToPage(PAGE_MAIN_MENU);
  }
}

void handleSettings() {
  // Special handling for Games page - use Settings button for up navigation
  if (currentPage == PAGE_GAMES) {
    handleGamesSettings();
    return;
  }

  // Special handling for Snake page
  if (currentPage == PAGE_SNAKE) {
    handleSnakeSettings();
    return;
  }

  if (!settingsOpen) {
    // First press: Open settings to notifications panel
    settingsOpen = true;
    settingsPanelState = SETTINGS_NOTIFICATIONS;
    previousPage = currentPage;
    navigateToPage(PAGE_SETTINGS);
  } else {
    // Already open - cycle states
    if (settingsPanelState == SETTINGS_NOTIFICATIONS) {
      // Second press: Switch to quick settings
      settingsPanelState = SETTINGS_QUICK_SETTINGS;
      // Re-render the settings page
      renderSettingsPage();
    }
    // If already on QUICK_SETTINGS, stay there (third+ press does nothing)
  }
}

void handleNextPage() {
  // Delegate to the current page's next page handler
  switch (currentPage) {
    case PAGE_MAP:
      handleMapNextPage();
      break;
    case PAGE_INFO:
      handleInfoNextPage();
      break;
    case PAGE_GAMES:
      handleGamesNextPage();
      break;
    case PAGE_SNAKE:
      handleSnakeNextPage();
      break;
    case PAGE_SPEEDOMETER:
      // No next page handler for speedometer
      break;
    case PAGE_WEATHER:
      handleWeatherNextPage();
      break;
    case PAGE_TRACKER:
      // Add handler when implemented
      break;
    // Other pages can ignore or implement as needed
  }
}

// --- BLE DEVICE STATUS FUNCTIONS ---
// Determine GPS stage: 0 = no data, 1 = time, 2 = date, 3 = location locked
uint8_t getGpsStage() {
  if (gps.location.isValid() && gps.satellites.value() > 0) {
    return 3;  // Location locked
  }

  bool dateAcquired = false;
  if (gps.date.isValid() && gps.date.year() >= 2025) {
    dateAcquired = true;
  }

  bool timeAcquired = false;
  if (gps.time.isValid()) {
    if (!(gps.time.hour() == 0 && gps.time.minute() == 0 && gps.time.second() == 0)) {
      timeAcquired = true;
    }
  }

  if (dateAcquired) return 2;  // Date available
  if (timeAcquired) return 1;  // Time available
  return 0;                    // No GPS data
}

// Send ESP32 device status to Android
void sendEspDeviceStatus() {
  if (!deviceConnected || pDeviceStatusCharacteristic == nullptr) return;

  // Update current device status
  currentEspDeviceStatus.batteryPercent = (uint8_t)constrain(batteryManager.getPercentage(), 0.0f, 100.0f);
  currentEspDeviceStatus.gpsStage = getGpsStage();
  currentEspDeviceStatus.satelliteCount = gps.satellites.isValid() ? (uint8_t)constrain(gps.satellites.value(), 0, 255) : 0;

  // Send the 3-byte packet
  uint8_t* packetPtr = (uint8_t*)&currentEspDeviceStatus;
  pDeviceStatusCharacteristic->setValue(packetPtr, sizeof(EspDeviceStatusPacket));
  pDeviceStatusCharacteristic->notify();

  Serial.printf("[ESP_STATUS] Sent device status: battery=%d%%, GPS stage=%d, sats=%d\n",
                currentEspDeviceStatus.batteryPercent, currentEspDeviceStatus.gpsStage,
                currentEspDeviceStatus.satelliteCount);
}

// --- SETUP ---
void setup() {
  Serial.begin(115200);
  delay(100);  // Minimal delay for serial

  Serial.println("\n=== Bike Navigation System ===");

  // Check wake-up reason and initialize power manager
  printWakeupReason();
  initPowerManager();

  pinMode(EPD_CS_PIN, OUTPUT);
  digitalWrite(EPD_CS_PIN, HIGH);
  pinMode(SD_CS_PIN, OUTPUT);
  digitalWrite(SD_CS_PIN, HIGH);
  delay(100);  // Let pins stabilize

  // Initialize display
  display.init(115200, true, 50, false);
  display.setRotation(2);
  
  // Initialize u8g2 fonts
  u8g2_display.begin(display);
  u8g2_display.setFontMode(1);                    // Transparent mode (1) for e-ink
  u8g2_display.setFontDirection(0);               // Left to right
  
  Serial.printf("Display dimensions: %dx%d\n", DISPLAY_WIDTH, DISPLAY_HEIGHT);
  
  // Setup GPS
  GPS_Serial.begin(9600, SERIAL_8N1, RX1_PIN, TX1_PIN);
  pinMode(17, OUTPUT);
  digitalWrite(17, HIGH);

  // Setup backlight (pin 38)
  pinMode(38, OUTPUT);
  digitalWrite(38, LOW);  // Default OFF

  // Setup input pins
  pinMode(CLK_PIN, INPUT_PULLUP);
  pinMode(DT_PIN, INPUT_PULLUP);
  pinMode(SW_PIN, INPUT_PULLUP);
  pinMode(BACK_PIN, INPUT_PULLUP);
  pinMode(NEXT_PAGE_PIN, INPUT_PULLUP);
  pinMode(SETTINGS_PIN, INPUT_PULLUP);
  pinMode(OPTIONS_PIN, INPUT_PULLUP);

  // Attach interrupts
  // Encoder: both pins must be on CHANGE to catch all state transitions
  attachInterrupt(digitalPinToInterrupt(CLK_PIN), encoderISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(DT_PIN), encoderISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(SW_PIN), buttonISR, FALLING);
  attachInterrupt(digitalPinToInterrupt(BACK_PIN), backISR, FALLING);
  attachInterrupt(digitalPinToInterrupt(NEXT_PAGE_PIN), nextPageISR, FALLING);
  attachInterrupt(digitalPinToInterrupt(SETTINGS_PIN), settingsISR, FALLING);
  attachInterrupt(digitalPinToInterrupt(OPTIONS_PIN), optionsISR, CHANGE);

  // Charging state interrupt (CHANGE to catch both plug and unplug)
  // Note: Battery manager will initialize the pin with INPUT_PULLUP
  // TEMPORARILY DISABLED
  // attachInterrupt(digitalPinToInterrupt(BATTERY_CHARGING_PIN), chargingISR, CHANGE);
  
  // Initialize SD card with robust retry logic
  Serial.println("Initializing SD card...");

  // Give SD card time to power up and stabilize (critical for reliability)
  delay(200);

  // Try to initialize SD card with multiple attempts and lower SPI speed
  const int MAX_SD_RETRIES = 5;
  const uint32_t SD_SPI_SPEED = 4000000; // 4 MHz - slower but more reliable

  sdCardPresent = false;
  for (int attempt = 1; attempt <= MAX_SD_RETRIES && !sdCardPresent; attempt++) {
    Serial.printf("SD init attempt %d/%d...", attempt, MAX_SD_RETRIES);

    // Try with explicit SPI speed configuration
    sdCardPresent = SD.begin(SD_CS_PIN, SPI, SD_SPI_SPEED);

    if (sdCardPresent) {
      // Verify we can actually read the card
      uint8_t cardType = SD.cardType();
      if (cardType == CARD_NONE) {
        Serial.println(" Card detected but type unknown, retrying...");
        SD.end();
        sdCardPresent = false;
        delay(500);
        continue;
      }

      sdCardSize = SD.cardSize() / (1024 * 1024);  // Convert to MB
      Serial.printf(" SUCCESS!\n");
      Serial.printf("Card Type: %s\n",
                   cardType == CARD_MMC ? "MMC" :
                   cardType == CARD_SD ? "SDSC" :
                   cardType == CARD_SDHC ? "SDHC" : "UNKNOWN");
      Serial.printf("Card Size: %llu MB\n", sdCardSize);
      Serial.printf("Total space: %llu MB\n", SD.totalBytes() / (1024 * 1024));
      Serial.printf("Used space: %llu MB\n", SD.usedBytes() / (1024 * 1024));
    } else {
      Serial.println(" FAILED");
      if (attempt < MAX_SD_RETRIES) {
        delay(500);  // Wait before retry
      }
    }
  }

  if (!sdCardPresent) {
    Serial.println("SD Card initialization failed after all attempts");
  }

  // GPS will lock in background - no waiting
  Serial.println("GPS will acquire fix in background...");
  Serial.println("Using default location (Prague) until GPS lock");

  // Initialize SD card folder structure
  initSDCardFolders();

  // Initialize BLE server
  if (bluetoothEnabled) {
    startBLE();
  }

  // Initialize radar frame storage
  initRadarFrames();

  // Initialize battery manager
  batteryManager.begin();

  // Initialize status bar
  initStatusBar();

  // Initialize notification system
  initNotificationSystem();

  // Initialize tile cache in PSRAM
  if (!initTileCache()) {
    Serial.println("WARNING: Tile cache disabled - maps will load from SD only");
  }

  // Initialize pages
  initMapPage();

  // Show startup notifications for missing hardware
  // Only notify if hardware is missing - no notification if everything is OK
  if (!psramFound()) {
    showNotification(
      "Hardware",
      "PSRAM not found!",
      "Tile cache disabled",
      ICON_INFO,
      0  // Persistent (won't auto-dismiss)
    );
  }

  if (!sdCardPresent) {
    showNotification(
      "Hardware",
      "SD Card not found!",
      "Check card insertion",
      ICON_INFO,
      0  // Persistent (won't auto-dismiss)
    );
  }

  // Show main menu
  navigateToPage(PAGE_MAIN_MENU);
  if (psramFound()) {
    Serial.printf("PSRAM found: %d bytes\n", ESP.getPsramSize());
    Serial.printf("Free PSRAM: %d bytes\n", ESP.getFreePsram());
  } else {
    Serial.println("No PSRAM found!");
  }

}

// --- MAIN LOOP ---
void loop() {
  // Update BLE handler (for delayed trip list sending)
  updateBleHandler();

  // Handle pending page navigation from BLE (deferred to avoid display corruption)
  extern volatile bool pendingPageNavigation;
  extern volatile PageType pendingNavigationPage;
  if (pendingPageNavigation) {
    pendingPageNavigation = false;  // Clear flag immediately to avoid repeated navigation

    // Check if enough time has passed since last navigation (cooldown)
    unsigned long timeSinceLastNav = millis() - lastPageNavigation;
    if (timeSinceLastNav >= PAGE_NAV_COOLDOWN) {
      Serial.printf("Executing deferred navigation to page %d\n", pendingNavigationPage);
      navigateToPage(pendingNavigationPage);
    } else {
      // Still in cooldown - re-queue for next loop iteration
      Serial.printf("Navigation still in cooldown (%lu ms remaining), re-queuing\n",
                    PAGE_NAV_COOLDOWN - timeSinceLastNav);
      pendingPageNavigation = true;
    }
  }

  // Update battery manager (checks internally if update interval has elapsed)
  batteryManager.update();

  // Handle charging state change interrupt
  if (chargingStateChanged) {
    chargingStateChanged = false;  // Clear interrupt flag

    // Force battery manager to read current charging state
    batteryManager.forceUpdate();

    // Show notification
    bool isNowCharging = batteryManager.getIsCharging();

    if (isNowCharging) {
      // Charging started
      char percentStr[16];
      snprintf(percentStr, sizeof(percentStr), "%.0f%%", batteryManager.getPercentage());

      showNotification(
        "Battery",
        "Charging started",
        percentStr,
        ICON_INFO,
        5000  // Show for 5 seconds
      );

      Serial.println("Battery: Charging started");
    } else {
      // Charging stopped (finished or unplugged)
      char percentStr[16];
      snprintf(percentStr, sizeof(percentStr), "%.0f%%", batteryManager.getPercentage());

      char line2[32];
      if (batteryManager.getPercentage() >= 95.0f) {
        snprintf(line2, sizeof(line2), "%s - Full", percentStr);
      } else {
        snprintf(line2, sizeof(line2), "%s", percentStr);
      }

      showNotification(
        "Battery",
        "Charging stopped",
        line2,
        ICON_INFO,
        5000  // Show for 5 seconds
      );

      Serial.println("Battery: Charging stopped");
    }
  }

  // Read GPS data
  static bool timeSetFromGPS = false;
  while (GPS_Serial.available() > 0) {
    if (gps.encode(GPS_Serial.read())) {
      if (gps.location.isValid()) {
        currentLat = gps.location.lat();
        currentLon = gps.location.lng();
        if (!gpsValid) {
          Serial.println("GPS lock acquired!");
          Serial.printf("Location: %.6f, %.6f\n", currentLat, currentLon);
        }
        gpsValid = true;
        lastGPSUpdate = millis();

        // Check if GPS position changed and trigger screen update if needed
        // Works in both navigation mode and plain map mode
        checkGPSPositionChange();
      }

      // Set system time from GPS (once when GPS first locks)
      if (!timeSetFromGPS && gps.date.isValid() && gps.time.isValid()) {
        struct tm timeinfo;
        timeinfo.tm_year = gps.date.year() - 1900;  // Years since 1900
        timeinfo.tm_mon = gps.date.month() - 1;      // Months since January (0-11)
        timeinfo.tm_mday = gps.date.day();
        timeinfo.tm_hour = gps.time.hour();
        timeinfo.tm_min = gps.time.minute();
        timeinfo.tm_sec = gps.time.second();
        timeinfo.tm_isdst = -1;  // Auto-detect DST

        time_t utcTime = mktime(&timeinfo);

        // Apply timezone offset (CET/CEST for Czech Republic)
        int timezoneOffset = isDSTActive(gps.date.year(), gps.date.month(), gps.date.day(), gps.time.hour()) ? 2 : 1;
        time_t localTime = utcTime + (timezoneOffset * 3600);

        // Set the system time
        struct timeval tv = { .tv_sec = localTime, .tv_usec = 0 };
        settimeofday(&tv, NULL);

        timeSetFromGPS = true;
        Serial.printf("System time set from GPS: %04d-%02d-%02d %02d:%02d:%02d (UTC+%d)\n",
                     gps.date.year(), gps.date.month(), gps.date.day(),
                     gps.time.hour(), gps.time.minute(), gps.time.second(), timezoneOffset);
      }
    }
  }
  
  // Handle encoder rotation
  if (encoderChanged) {
    // Atomically capture and reset the accumulated delta value
    // This prevents race conditions and ensures we don't lose counts
    noInterrupts();
    int capturedDelta = encoderDelta;
    encoderDelta = 0;
    encoderChanged = false;
    interrupts();

    // Mark user activity to prevent status bar from auto-refreshing during scrolling
    markUserActivity();

    switch (currentPage) {
      case PAGE_MAIN_MENU:
        handleMainMenuEncoder(capturedDelta);
        break;
      case PAGE_MAP:
        handleMapEncoder(capturedDelta);
        break;
      case PAGE_SETTINGS:
        handleSettingsEncoder(capturedDelta);
        break;
      case PAGE_TRACKER:
        handleTrackerEncoder(capturedDelta);
        break;
      case PAGE_RECORDING:
        handleRecordingEncoder(capturedDelta);
        break;
      case PAGE_RECORDING_OPTIONS:
        handleRecordingOptionsEncoder(capturedDelta);
        break;
      case PAGE_INFO:
        handleInfoEncoder(capturedDelta);
        break;
      case PAGE_WEATHER:
        handleWeatherEncoder(capturedDelta);
        break;
      case PAGE_WEATHER_OPTIONS:
        handleWeatherOptionsEncoder(capturedDelta);
        break;
      case PAGE_GAMES:
        handleGamesEncoder(capturedDelta);
        break;
      case PAGE_SNAKE:
        handleSnakeEncoder(capturedDelta);
        break;
      case PAGE_PHONE_APP:
        handlePhoneAppEncoder(capturedDelta);
        break;
      // Add other page encoder handlers as needed
    }
  }
  
  // Check for long-press on encoder button to reset map view
  // ONLY on map page and map subpage (not trips list or other sub-pages)
  // Check this BEFORE normal button handling to prevent mode cycling during long press
  if (currentPage == PAGE_MAP && currentMapSubPage == MAP_SUBPAGE_MAP) {
    extern bool checkEncoderLongPress();
    extern void resetMapView();
    if (checkEncoderLongPress()) {
      // Long press detected - reset map view
      resetMapView();
    }
  } else if (currentPage == PAGE_RECORDING && isRecording) {
    extern bool checkEncoderLongPress();
    extern void resetRecordingView();
    if (checkEncoderLongPress()) {
      // Long press detected - reset recording view
      resetRecordingView();
    }
  }

  // Check if we're waiting for button release
  if (waitingForButtonRelease) {
    // Check if button has been physically released
    if (digitalRead(SW_PIN) == HIGH) {
      // Button released! Clear any pending presses and reset state
      waitingForButtonRelease = false;
      buttonPressed = false;  // Clear any ISR triggers that happened while holding
      Serial.println("Button released, ready for next press");
    } else {
      // Still holding button, clear any ISR triggers
      buttonPressed = false;
    }
  }

  // Check if we're waiting for options release
  if (waitingForOptionsRelease) {
    // Check if options button has been physically released
    if (digitalRead(OPTIONS_PIN) == HIGH) {
      if (millis() - lastOptionsReleaseTime >= OPTIONS_RELEASE_DEBOUNCE) {
        // Button released! Clear any pending presses and reset state
        waitingForOptionsRelease = false;
        optionsPressed = false;  // Clear any ISR triggers that happened while holding
        Serial.println("Options released, ready for next press");
      }
    } else {
      // Still holding button, clear any ISR triggers
      optionsPressed = false;
    }
  }

  // Handle encoder button press
  if (buttonPressed && !waitingForButtonRelease) {
    buttonPressed = false;
    markUserActivity();  // Mark user activity

    // Check cooldown period after page navigation to prevent button carry-over
    if (millis() - lastPageNavigation < PAGE_NAV_COOLDOWN) {
      // Still in cooldown period, ignore button press
      Serial.println("Button press ignored (page navigation cooldown)");
    } else {
      // Process button press normally
      waitingForButtonRelease = true;  // Wait for physical release before accepting another press
      switch (currentPage) {
      case PAGE_MAIN_MENU:
        handleMainMenuButton();
        // Check if a page was selected
        if (selectedPageId > 0) {
          Serial.printf("Navigating to page ID: %d\n", selectedPageId);
          navigateToPage((PageType)selectedPageId);
          selectedPageId = 0;  // Reset
        }
        break;
      case PAGE_MAP:
        handleMapButton();
        break;
      case PAGE_SPEEDOMETER:
        handleSpeedometerButton();
        break;
      case PAGE_SETTINGS:
        handleSettingsButton();
        break;
      case PAGE_TRACKER:
        handleTrackerButton();
        break;
      case PAGE_RECORDING:
        handleRecordingButton();
        break;
      case PAGE_RECORDING_OPTIONS:
        handleRecordingOptionsButton();
        break;
      case PAGE_INFO:
        handleInfoButton();
        break;
      case PAGE_WEATHER:
        handleWeatherButton();
        break;
      case PAGE_WEATHER_OPTIONS:
        handleWeatherOptionsButton();
        break;
      case PAGE_GAMES:
        handleGamesButton();
        break;
      case PAGE_SNAKE:
        handleSnakeButton();
        break;
      case PAGE_PHONE_APP:
        handlePhoneAppButton();
        break;
      // Add other page button handlers as needed
      }
    }
  }
  
  // Handle back button
  if (backPressed) {
    backPressed = false;
    if (digitalRead(OPTIONS_PIN) == LOW) {
      // Ignore spurious back presses while options is held.
    } else {
      markUserActivity();
      handleBack();
    }
  }

  // Handle next page button
  if (nextPagePressed) {
    nextPagePressed = false;
    markUserActivity();
    handleNextPage();
  }
  
  // Handle settings button
  if (settingsPressed) {
    settingsPressed = false;
    markUserActivity();
    handleSettings();
  }

  // Handle options button
  if (optionsPressed) {
    optionsPressed = false;
    markUserActivity();
    waitingForOptionsRelease = true;  // Wait for physical release before accepting another press
    // Delegate to current page's options handler
    switch (currentPage) {
      case PAGE_MAP:
        handleMapOptions();
        break;
      case PAGE_RECORDING:
        // Navigate to recording options page
        navigateToPage(PAGE_RECORDING_OPTIONS);
        break;
      case PAGE_INFO:
        handleInfoOptions();
        break;
      case PAGE_SPEEDOMETER:
        handleSpeedometerOptions();
        break;
      case PAGE_WEATHER:
        handleWeatherOptions();
        break;
      case PAGE_SETTINGS:
        handleSettingsOptions();
        break;
      case PAGE_GAMES:
        handleGamesOptions();
        break;
      case PAGE_SNAKE:
        handleSnakeOptions();
        break;
      // Add other page option handlers as needed
    }
  }

  // Update current page
  switch (currentPage) {
    case PAGE_MAIN_MENU:
      updateMainMenu();
      break;
    case PAGE_MAP:
      updateMapPage();
      break;
    case PAGE_SPEEDOMETER:
      updateSpeedometerPage();
      break;
    case PAGE_TRACKER:
      updateTrackerPage();
      break;
    case PAGE_RECORDING:
      updateRecordingPage();
      break;
    case PAGE_RECORDING_OPTIONS:
      updateRecordingOptionsPage();
      break;
    case PAGE_INFO:
      updateInfoPage();
      break;
    case PAGE_SETTINGS:
      updateSettingsPage();
      break;
    case PAGE_WEATHER:
      updateWeatherPage();
      break;
    case PAGE_WEATHER_OPTIONS:
      updateWeatherOptionsPage();
      break;
    case PAGE_GAMES:
      updateGamesPage();
      break;
    case PAGE_SNAKE:
      updateSnakePage();
      break;
    case PAGE_PHONE_APP:
      updatePhoneAppPage();
      break;
    // Add other page update handlers as needed
  }

  // Update notification system (handle auto-dismiss)
  updateNotifications();

  // Update notification display with smart rendering (waits for user activity to finish)
  updateNotificationDisplay();

  // Check for long-press on options button for power off
  // ONLY on main menu to avoid interfering with other pages (e.g., game scrolling)
  if (currentPage == PAGE_MAIN_MENU) {
    if (checkPowerOffLongPress()) {
      // Long press detected - navigate to shutdown page
      navigateToPage(PAGE_SHUTDOWN);
      // Note: execution won't return from here, device will enter deep sleep
    }
  }

  // Check for long-press on back button to return to main menu
  // Works on all pages EXCEPT main menu (already there)
  if (currentPage != PAGE_MAIN_MENU) {
    if (checkBackLongPress()) {
      // Long press detected - return directly to main menu
      if (currentPage == PAGE_RECORDING && isViewingRecording) {
        stopRecording();
        clearViewingRecordingState();
      }
      navigateToPage(PAGE_MAIN_MENU);
    }
  }

  delay(50);
}
