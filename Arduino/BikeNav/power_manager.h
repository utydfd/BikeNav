#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H

#include <Arduino.h>
#include <SD.h>
#include <ArduinoJson.h>
#include "driver/rtc_io.h"
#include "driver/gpio.h"
#include "esp_sleep.h"
#include <math.h> // Required for sqrt in noise generation
#include <esp_random.h> // Use the ESP32 Hardware True Random Number Generator
#include <SPI.h>
#include <esp_bt.h>
#include <BLEDevice.h>
#if defined(__has_include)
#if __has_include("esp_private/sar_periph_ctrl.h")
#include "esp_private/sar_periph_ctrl.h"
#define HAS_SAR_PERIPH_CTRL 1
#else
#define HAS_SAR_PERIPH_CTRL 0
#endif
#else
#define HAS_SAR_PERIPH_CTRL 0
#endif

// External references for display and UI
extern GxEPD2_BW<GxEPD2_290_BS, GxEPD2_290_BS::HEIGHT> display;
extern U8G2_FOR_ADAFRUIT_GFX u8g2_display;
extern const int DISPLAY_WIDTH;
extern const int DISPLAY_HEIGHT;
extern class BatteryManager batteryManager;
extern char loadedTrackName[64];
extern int loadedTrackPointCount;

// === Power Management Configuration ===

// RTC memory to track boot count and wake reason
RTC_DATA_ATTR int bootCount = 0;

// Long press duration for power off (milliseconds)
#define POWER_OFF_LONG_PRESS_MS 1500

// Long press duration for returning to main menu (milliseconds)
#define BACK_LONG_PRESS_MS 1500

// Pins to manage during sleep
#define GPS_POWER_PIN 17
#define BACKLIGHT_PIN 38
#define OPTIONS_BUTTON_PIN 18
#define BACK_PIN 47  // Back button (needed for checkBackLongPress)
// Note: CLK_PIN, DT_PIN, SW_PIN, etc. are defined in BikeNav.ino

// === Function Prototypes ===
void printWakeupReason();
void generateDitheredGradient();
void drawBatteryIcon(int x, int y, float percentage);
void renderShutdownScreen();
void parkSpiPinsForSleep();
void configureDeepSleepPowerDomains();
void goToDeepSleep();
void handlePowerOff();

// === Wake-up Reason Detection ===

/**
 * @brief Print the reason why the ESP32-S3 woke up from deep sleep
 * Call this in setup() to understand wake-up behavior
 */
void printWakeupReason() {
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

  bootCount++;
  Serial.println("\n=== Power Manager ===");
  Serial.printf("Boot count: %d\n", bootCount);

  switch(wakeup_reason) {
    case ESP_SLEEP_WAKEUP_EXT0:
      Serial.println("Wakeup caused by external signal (EXT0) - Options button pressed");
      break;
    case ESP_SLEEP_WAKEUP_EXT1:
      Serial.println("Wakeup caused by external signal (EXT1)");
      break;
    case ESP_SLEEP_WAKEUP_TIMER:
      Serial.println("Wakeup caused by timer");
      break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD:
      Serial.println("Wakeup caused by touchpad");
      break;
    case ESP_SLEEP_WAKEUP_ULP:
      Serial.println("Wakeup caused by ULP program");
      break;
    default:
      Serial.printf("Wakeup was not caused by deep sleep: %d (normal boot)\n", wakeup_reason);
      break;
  }
}

// === Noise Generation for Shutdown Screen ===

/**
 * @brief Applies a smoothstep curve to increase contrast.
 * This pushes values closer to 0 and 1, creating a more dramatic image.
 * @param v The input value (0.0 to 1.0).
 * @return The value with contrast applied.
 */
float applyContrastCurve(float v) {
  // Use the smoothstep formula: 3x^2 - 2x^3
  return v * v * (3.0 - 2.0 * v);
}

/**
 * @brief Generate a procedural dithered gradient using domain warping and Floyd-Steinberg dithering.
 * This creates a complex, organic pattern with high contrast and natural-looking texture.
 * It uses the ESP32's hardware TRNG for genuinely random patterns.
 */
void generateDitheredGradient() {
  // Allocate a buffer to store the full-resolution grayscale map.
  float* grayscaleMap = new float[DISPLAY_WIDTH * DISPLAY_HEIGHT];
  if (!grayscaleMap) {
    Serial.println("Failed to allocate memory for grayscale map!");
    return;
  }

  // --- PASS 1: Generate the high-contrast grayscale noise pattern ---
  
  // No need for randomSeed(), as esp_random() uses the hardware TRNG directly.
  
  // Generate genuinely random parameters for the pattern
  float freq1 = ((esp_random() % 60) + 20) / 1000.0; // Range 20-79
  float freq2 = ((esp_random() % 60) + 20) / 1000.0; // Range 20-79
  float freq3 = ((esp_random() % 15) + 5) / 1000.0;  // Range 5-19
  float offsetX = esp_random() % 1024;
  float offsetY = esp_random() % 1024;
  float warpAmount = (esp_random() % 60) + 20;       // Range 20-79

  // Calculate random center offset
  int centerRange = DISPLAY_WIDTH / 2; // e.g., 296 / 2 = 148
  int centerOffset = (DISPLAY_WIDTH / 4); // e.g., 296 / 4 = 74
  int randomXOffset = (esp_random() % centerRange) - centerOffset; // Range is now [-74, 73]
  int randomYOffset = (esp_random() % centerRange) - centerOffset;

  float centerX = (DISPLAY_WIDTH / 2.0) + randomXOffset;
  float centerY = (DISPLAY_HEIGHT / 2.0) + randomYOffset;

  for (int y = 0; y < DISPLAY_HEIGHT; y++) {
    for (int x = 0; x < DISPLAY_WIDTH; x++) {
      // Domain Warping to create swirls
      float q1 = sin((x + offsetX) * freq1 + sin((y + offsetY) * freq2));
      float q2 = cos(sin((x + offsetX) * freq2) + (y + offsetY) * freq1);

      // Base pattern generation using warped coordinates
      float warpedX = x + q1 * warpAmount;
      float warpedY = y + q2 * warpAmount;
      float noiseValue = sin(warpedX * freq3) + cos(warpedY * freq3);

      // Radial influence for larger forms
      float dx = x - centerX;
      float dy = y - centerY;
      float distance = sqrt(dx * dx + dy * dy);
      noiseValue += cos(distance * 0.02) * 0.5;

      // Normalize the value to a 0.0 to 1.0 range
      noiseValue = (noiseValue + 2.5) / 5.0;
      noiseValue = constrain(noiseValue, 0.0, 1.0);

      // Apply the contrast curve and store in the map
      grayscaleMap[y * DISPLAY_WIDTH + x] = applyContrastCurve(noiseValue);
    }
  }

  // --- PASS 2: Apply Floyd-Steinberg dithering and draw to the display ---

  for (int y = 0; y < DISPLAY_HEIGHT; y++) {
    for (int x = 0; x < DISPLAY_WIDTH; x++) {
      int index = y * DISPLAY_WIDTH + x;
      float oldPixel = grayscaleMap[index];
      
      // Determine the closest color (black=1.0, white=0.0)
      float newPixel = (oldPixel > 0.5) ? 1.0 : 0.0;
      
      // Draw the final black or white pixel
      display.drawPixel(x, y, (newPixel == 1.0) ? GxEPD_BLACK : GxEPD_WHITE);

      // Calculate the error
      float quantError = oldPixel - newPixel;

      // Diffuse the error to neighboring pixels
      if (x + 1 < DISPLAY_WIDTH) {
        grayscaleMap[index + 1] += quantError * 7.0 / 16.0;
      }
      if (y + 1 < DISPLAY_HEIGHT) {
        if (x > 0) {
          grayscaleMap[index + DISPLAY_WIDTH - 1] += quantError * 3.0 / 16.0;
        }
        grayscaleMap[index + DISPLAY_WIDTH] += quantError * 5.0 / 16.0;
        if (x + 1 < DISPLAY_WIDTH) {
          grayscaleMap[index + DISPLAY_WIDTH + 1] += quantError * 1.0 / 16.0;
        }
      }
    }
  }

  // Clean up the allocated memory
  delete[] grayscaleMap;
}


/**
 * @brief Draw a simple battery icon
 * @param x X position (top-left corner)
 * @param y Y position (top-left corner)
 * @param percentage Battery percentage (0-100) for fill level
 */
void drawBatteryIcon(int x, int y, float percentage) {
  const int width = 20;
  const int height = 10;
  const int tipWidth = 2;
  const int tipHeight = 4;

  // Draw battery body outline
  display.drawRect(x, y, width, height, GxEPD_BLACK);

  // Draw battery tip (positive terminal)
  display.fillRect(x + width, y + (height - tipHeight) / 2, tipWidth, tipHeight, GxEPD_BLACK);

  // Draw fill level (with 1px padding inside)
  if (percentage > 0) {
    int fillWidth = (int)((width - 4) * (percentage / 100.0));
    if (fillWidth > 0) {
      display.fillRect(x + 2, y + 2, fillWidth, height - 4, GxEPD_BLACK);
    }
  }
}

/**
 * @brief Render stylized shutdown screen with procedural noise
 * Shows battery status in a clean box overlay at the bottom
 * The e-ink display will retain this image without power
 */
void renderShutdownScreen() {
  // Use full window mode for slowest, cleanest refresh (no ghosting)
  display.setFullWindow();

  // Show "Shutting down..." message (this also clears the screen)
  display.firstPage();
  do {
    // Draw centered box with shadow
    const int msgBoxWidth = 85;
    const int msgBoxHeight = 24;
    const int msgBoxX = (DISPLAY_WIDTH - msgBoxWidth) / 2;
    const int msgBoxY = (DISPLAY_HEIGHT - msgBoxHeight) / 2;
    const int shadowOffset = 3;

    // Draw shadow (offset filled rectangle)
    display.fillRect(msgBoxX + shadowOffset, msgBoxY + shadowOffset,
                     msgBoxWidth, msgBoxHeight, GxEPD_BLACK);

    // Draw white box
    display.fillRect(msgBoxX, msgBoxY, msgBoxWidth, msgBoxHeight, GxEPD_WHITE);
    display.drawRect(msgBoxX, msgBoxY, msgBoxWidth, msgBoxHeight, GxEPD_BLACK);

    // Draw "Shutting down..." text (centered)
    u8g2_display.setFontMode(1);
    u8g2_display.setForegroundColor(GxEPD_BLACK);
    u8g2_display.setBackgroundColor(GxEPD_WHITE);
    u8g2_display.setFont(u8g2_font_helvB08_tf);

    const char* shutdownMsg = "Shutting down...";
    int textWidth = u8g2_display.getUTF8Width(shutdownMsg);
    int textX = msgBoxX + (msgBoxWidth - textWidth) / 2;
    int textY = msgBoxY + msgBoxHeight / 2 + 3;

    u8g2_display.setCursor(textX, textY);
    u8g2_display.print(shutdownMsg);
  } while (display.nextPage());

  // Brief pause to show the message
  delay(800);

  // Now render the final decorative shutdown screen
  display.firstPage();

  do {
    // Generate full-screen dithered gradient noise
    generateDitheredGradient();

    // Draw white box at bottom with padding
    const int boxMargin = 8;
    const int boxHeight = 45;
    const int boxX = boxMargin;
    const int boxY = DISPLAY_HEIGHT - boxHeight - boxMargin;
    const int boxWidth = DISPLAY_WIDTH - (boxMargin * 2);

    display.fillRect(boxX, boxY, boxWidth, boxHeight, GxEPD_WHITE);
    display.drawRect(boxX, boxY, boxWidth, boxHeight, GxEPD_BLACK);

    // Setup text rendering
    u8g2_display.setFontMode(1);
    u8g2_display.setForegroundColor(GxEPD_BLACK);
    u8g2_display.setBackgroundColor(GxEPD_WHITE);

    // Draw battery icon and percentage
    float batteryPercent = batteryManager.getPercentage();
    int iconX = boxX + 10;
    int iconY = boxY + 10;

    drawBatteryIcon(iconX, iconY, batteryPercent);

    // Draw battery percentage
    u8g2_display.setFont(u8g2_font_helvB10_tf);
    char percentStr[8];
    snprintf(percentStr, sizeof(percentStr), "%.0f%%", batteryPercent);
    u8g2_display.setCursor(iconX + 26, iconY + 9);
    u8g2_display.print(percentStr);

    // Draw wake instruction text (centered)
    u8g2_display.setFont(u8g2_font_helvR08_tf);
    const char* wakeMsg = "Press Options to wake";
    int msgWidth = u8g2_display.getUTF8Width(wakeMsg);
    u8g2_display.setCursor((DISPLAY_WIDTH - msgWidth) / 2, boxY + boxHeight - 10);
    u8g2_display.print(wakeMsg);

  } while (display.nextPage());

  Serial.println("Shutdown screen rendered with procedural noise and Floyd-Steinberg dithering");
}

/**
 * @brief Park SPI pins to reduce leakage while peripherals remain powered.
 * Leaves pins as high-impedance inputs with no internal pulls.
 */
void parkSpiPinsForSleep() {
  auto parkPin = [](int pin) {
    if (pin < 0) {
      return;
    }
    pinMode(pin, INPUT);
    gpio_pullup_dis((gpio_num_t)pin);
    gpio_pulldown_dis((gpio_num_t)pin);
  };

#ifdef SCK
  parkPin(SCK);
#endif
#ifdef MOSI
  parkPin(MOSI);
#endif
#ifdef MISO
  parkPin(MISO);
#endif
}

/**
 * @brief Configure power domains for minimum deep sleep current
 * Notes:
 * - RTC_PERIPH must stay on for EXT0 wake (Options button).
 * - Disabling RTC slow/fast memory reduces current but clears RTC_DATA_ATTR.
 */
void configureDeepSleepPowerDomains() {
  // Keep RTC peripherals powered for EXT0 wakeup.
#if defined(SOC_PM_SUPPORT_RTC_PERIPH_PD) && SOC_PM_SUPPORT_RTC_PERIPH_PD
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
#endif

  // Power down RTC memories for lower deep sleep current.
#if defined(SOC_PM_SUPPORT_RTC_SLOW_MEM_PD) && SOC_PM_SUPPORT_RTC_SLOW_MEM_PD
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_SLOW_MEM, ESP_PD_OPTION_OFF);
#endif
#if defined(SOC_PM_SUPPORT_RTC_FAST_MEM_PD) && SOC_PM_SUPPORT_RTC_FAST_MEM_PD
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_FAST_MEM, ESP_PD_OPTION_OFF);
#endif

  // Power down the crystal oscillator in deep sleep.
  esp_sleep_pd_config(ESP_PD_DOMAIN_XTAL, ESP_PD_OPTION_OFF);

  // Disable SAR (ADC/TSENS/PWDET) power where supported.
#if HAS_SAR_PERIPH_CTRL
  sar_periph_ctrl_power_disable();
#endif
}

/**
 * @brief Prepare system for deep sleep and enter deep sleep mode
 * This function handles:
 * - Turning off GPS with pin hold
 * - Turning off backlight with pin hold
 * - Hibernating e-paper display
 * - Deinitializing SD card for low power
 * - Shutting down SPI bus
 * - Disabling Bluetooth/BLE completely (saves ~1-3 mA)
 * - Powering down RTC domains/ADC for minimum deep sleep current
 * - Configuring wakeup source (options button)
 * - Entering deep sleep
 *
 * Expected deep sleep current: <200 µA (down from ~5-10 mA without optimizations)
 * Major power savings come from BLE shutdown and display hibernation.
 */
void goToDeepSleep() {
  Serial.println("\n=== Initiating Deep Sleep ===");

  // 1. Turn off GPS and hold pin state
  Serial.println("Turning off GPS...");
  digitalWrite(GPS_POWER_PIN, LOW);  // Turn off GPS
  delay(10);  // Let the pin settle

  // Hold GPS pin LOW during deep sleep
  gpio_hold_en((gpio_num_t)GPS_POWER_PIN);
  Serial.println("GPS pin held LOW");

  // 2. Turn off backlight and hold pin state
  Serial.println("Turning off backlight...");
  digitalWrite(BACKLIGHT_PIN, LOW);  // Turn off backlight
  delay(10);  // Let the pin settle

  // Hold backlight pin LOW during deep sleep
  gpio_hold_en((gpio_num_t)BACKLIGHT_PIN);
  Serial.println("Backlight pin held LOW");

  // 3. Power off e-paper display (CRITICAL for power saving)
  // This completely cuts power to the display controller (lowest power mode)
  // The e-ink screen will retain the shutdown image without any power
  Serial.println("Powering off e-paper display...");
  display.powerOff();
  Serial.println("Display powered off");

  // 4. Deinitialize SD card to save power
  // Note: SD card cannot be completely turned off, but this reduces power consumption
  Serial.println("Deinitializing SD card...");
  SD.end();
  Serial.println("SD card deinitialized");

  // 5. Shut down SPI bus
  Serial.println("Shutting down SPI bus...");
  SPI.end();
  parkSpiPinsForSleep();
  Serial.println("SPI bus shut down");

  // 6. Disable Bluetooth/BLE completely (MAJOR power saving: ~1-3 mA)
  // BLE controller remains partially active unless explicitly shut down
  Serial.println("Disabling Bluetooth/BLE...");

  // CRITICAL: If a device is connected, disconnect it first before deinitializing
  // This prevents heap corruption from freeing memory that's still in use
  extern BLEServer* pServer;
  extern bool deviceConnected;

  if (deviceConnected && pServer != nullptr) {
    Serial.println("BLE device connected - disconnecting first...");

    // Get the connection ID and disconnect
    uint16_t connId = pServer->getConnId();
    pServer->disconnect(connId);

    // Wait for disconnection to complete (with timeout)
    unsigned long disconnectStart = millis();
    while (deviceConnected && (millis() - disconnectStart < 2000)) {
      delay(10);
    }

    if (deviceConnected) {
      Serial.println("WARNING: Device still connected after timeout");
    } else {
      Serial.println("Device disconnected successfully");
    }

    // Additional delay to ensure cleanup is complete
    delay(100);
  }

  // Use BLEDevice::deinit() which handles both bluedroid and controller shutdown
  // This is the Arduino ESP32 BLE library way to completely shut down BLE
  BLEDevice::deinit(true);  // true = release all resources
  Serial.println("BLE completely deinitialized");

  // Additionally disable the BT controller at hardware level for maximum power savings
  if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED) {
    esp_bt_controller_disable();
    Serial.println("BT controller disabled");
  }
  if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_INITED) {
    esp_bt_controller_deinit();
    Serial.println("BT controller deinitialized");
  }

  Serial.println("Bluetooth/BLE completely shut down");

  // 7. Enable deep sleep hold for all held pins
  // This ensures pins maintain their state during deep sleep
  gpio_deep_sleep_hold_en();
  Serial.println("Deep sleep hold enabled for all pins");

  // 8. Wait for Options button to be released
  // This prevents immediate wake-up if button is still held from long-press
  Serial.println("Waiting for Options button release...");
  unsigned long waitStart = millis();
  while (digitalRead(OPTIONS_BUTTON_PIN) == LOW) {
    delay(10);
    // Timeout after 5 seconds in case button is stuck
    if (millis() - waitStart > 5000) {
      Serial.println("WARNING: Button release timeout!");
      break;
    }
  }
  Serial.println("Button released");
  delay(500);  // Longer delay for pin to stabilize

  // 9. Properly configure RTC GPIO for wakeup
  // Initialize pin as RTC GPIO with pull-up
  gpio_num_t wakeup_gpio = (gpio_num_t)OPTIONS_BUTTON_PIN;

  // Configure RTC GPIO pull-up to keep pin HIGH when not pressed
  rtc_gpio_pullup_en(wakeup_gpio);
  rtc_gpio_pulldown_dis(wakeup_gpio);
  Serial.println("RTC GPIO configured with pull-up");

  // Wait for pin to stabilize after RTC configuration
  delay(200);

  // Verify pin is HIGH before configuring wakeup
  int pinState = digitalRead(OPTIONS_BUTTON_PIN);
  Serial.printf("Options button state before sleep: %s\n", pinState == HIGH ? "HIGH (released)" : "LOW (pressed)");

  if (pinState == LOW) {
    Serial.println("ERROR: Button still LOW after release wait!");
    Serial.println("Aborting sleep to prevent immediate wakeup");
    // Disable GPIO holds and return
    gpio_deep_sleep_hold_dis();
    gpio_hold_dis((gpio_num_t)GPS_POWER_PIN);
    gpio_hold_dis((gpio_num_t)BACKLIGHT_PIN);
    digitalWrite(GPS_POWER_PIN, HIGH);  // Restore GPS
    return;
  }

  // 10. Configure wakeup source - Options button (pin 18)
  // ESP32-S3 uses ext0 for single GPIO wakeup
  // Wake on LOW (button pressed, with INPUT_PULLUP)
  esp_sleep_enable_ext0_wakeup(wakeup_gpio, 0);  // 0 = LOW level
  Serial.println("Configured wakeup on Options button (LOW)");

  // 11. Configure power domains for deep sleep savings
  configureDeepSleepPowerDomains();

  // 12. Final message before sleep
  Serial.println("Entering deep sleep NOW...");
  Serial.println("Press Options button to wake up");
  Serial.println("Expected deep sleep current: <200 µA");
  Serial.flush();  // Ensure all serial output is sent
  delay(100);

  // 13. Enter deep sleep
  esp_deep_sleep_start();

  // Code never reaches here - ESP32 will restart from setup() on wake
}

/**
 * @brief Handle graceful power-off with display feedback
 * Shows shutdown screen before entering deep sleep
 * This should be called from the main code when long-press is detected
 */
void handlePowerOff() {
  Serial.println("Power off initiated - long press detected");

  // The display shutdown screen is handled in BikeNav.ino navigateToPage(PAGE_SHUTDOWN)
  // That function will call goToDeepSleep() after showing the message

  // For immediate power off without display:
  goToDeepSleep();
}

/**
 * @brief Initialize power management on wake-up
 * Call this in setup() after waking from deep sleep to restore normal operation
 * This function:
 * - Disables GPIO hold on GPS and backlight pins
 * - Restores GPS power
 * - Keeps backlight off by default (user can enable via settings)
 *
 * Note: Other peripherals (Display, SPI, BLE, SD card) are re-initialized
 * automatically in BikeNav.ino setup() function after this call
 */
void initPowerManager() {
  // Disable deep sleep hold to allow normal GPIO operation
  gpio_deep_sleep_hold_dis();

  // Release hold on GPS pin and restore power
  gpio_hold_dis((gpio_num_t)GPS_POWER_PIN);
  digitalWrite(GPS_POWER_PIN, HIGH);  // Turn GPS back on
  Serial.println("GPS power restored");

  // Release hold on backlight pin (keep it off by default)
  gpio_hold_dis((gpio_num_t)BACKLIGHT_PIN);
  digitalWrite(BACKLIGHT_PIN, LOW);  // Keep backlight off
  Serial.println("Backlight hold released (default OFF)");

  Serial.println("Power manager initialized");
}

// === Long Press Detection Variables ===

// Track options button press time for long-press detection (power off)
unsigned long optionsButtonPressTime = 0;
bool optionsButtonLongPressChecked = false;

// Track back button press time for long-press detection (return to main menu)
unsigned long backButtonPressTime = 0;
bool backButtonLongPressChecked = false;

/**
 * @brief Check if options button is being held for power-off
 * Call this in loop() to detect long press for power off
 * Returns true if long press detected and power-off initiated
 * NOTE: Should only be called from main menu page to avoid interfering with other pages
 */
bool checkPowerOffLongPress() {
  // Check if options button is currently pressed (LOW with INPUT_PULLUP)
  if (digitalRead(OPTIONS_BUTTON_PIN) == LOW) {
    // Button is pressed
    if (optionsButtonPressTime == 0) {
      // First detection of press - record time
      optionsButtonPressTime = millis();
      optionsButtonLongPressChecked = false;
      Serial.println("Options button pressed - hold for 1.5s to power off");
    } else if (!optionsButtonLongPressChecked) {
      // Button is being held - check duration
      unsigned long pressDuration = millis() - optionsButtonPressTime;

      if (pressDuration >= POWER_OFF_LONG_PRESS_MS) {
        // Long press detected!
        optionsButtonLongPressChecked = true;  // Prevent multiple triggers
        Serial.println("Long press detected - powering off!");

        // Initiate power off (will show shutdown screen and enter deep sleep)
        return true;
      }
    }
  } else {
    // Button released - reset tracking
    if (optionsButtonPressTime > 0) {
      unsigned long pressDuration = millis() - optionsButtonPressTime;
      if (pressDuration < POWER_OFF_LONG_PRESS_MS) {
        Serial.printf("Options button released after %lums (short press)\n", pressDuration);
      }
    }
    optionsButtonPressTime = 0;
    optionsButtonLongPressChecked = false;
  }

  return false;  // No long press detected
}

/**
 * @brief Check if back button is being held for return to main menu
 * Call this in loop() to detect long press for quick navigation to main menu
 * Returns true if long press detected
 * NOTE: Should not be called when already on main menu
 */
bool checkBackLongPress() {
  // Check if back button is currently pressed (LOW with INPUT_PULLUP)
  if (digitalRead(BACK_PIN) == LOW) {
    // Button is pressed
    if (backButtonPressTime == 0) {
      // First detection of press - record time
      backButtonPressTime = millis();
      backButtonLongPressChecked = false;
      Serial.println("Back button pressed - hold for 1.5s to return to main menu");
    } else if (!backButtonLongPressChecked) {
      // Button is being held - check duration
      unsigned long pressDuration = millis() - backButtonPressTime;

      if (pressDuration >= BACK_LONG_PRESS_MS) {
        // Long press detected!
        backButtonLongPressChecked = true;  // Prevent multiple triggers
        Serial.println("Back long press detected - returning to main menu!");
        return true;
      }
    }
  } else {
    // Button released - reset tracking
    if (backButtonPressTime > 0) {
      unsigned long pressDuration = millis() - backButtonPressTime;
      if (pressDuration < BACK_LONG_PRESS_MS) {
        // Don't log short press here - it's handled by the normal Back ISR
      }
    }
    backButtonPressTime = 0;
    backButtonLongPressChecked = false;
  }

  return false;  // No long press detected
}

#endif // POWER_MANAGER_H
