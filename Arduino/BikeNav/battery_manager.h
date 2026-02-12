// battery_manager.h
#ifndef BATTERY_MANAGER_H
#define BATTERY_MANAGER_H

#include <Arduino.h>

// Battery configuration
#define BATTERY_PIN 16
#define BATTERY_CHARGING_PIN 48  // TP4056 CHRG pin (active low, pull-up)
#define ADC_MAX_VALUE 4095  // 12-bit ADC
#define ADC_VOLTAGE_REF 3.3f  // Reference voltage

// ADC calibration values (to be filled in after calibration)
// Use the BatteryCalibration sketch to find these values
#define ADC_0_PERCENT 1881     // ADC at 3.85V
#define ADC_100_PERCENT 2504   // ADC at 4.02V

// Battery voltage thresholds (typical for Li-ion/Li-Po)
#define BATTERY_VOLTAGE_MIN 3.2f  // Minimum safe voltage
#define BATTERY_VOLTAGE_MAX 4.2f  // Maximum voltage (fully charged)

// Moving average configuration
#define BATTERY_SAMPLE_COUNT 10  // Number of samples to average
#define BATTERY_UPDATE_INTERVAL 5000  // Update every 5 seconds (in ms)

class BatteryManager {
private:
  // Sample buffer for moving average
  int adcSamples[BATTERY_SAMPLE_COUNT];
  int currentSampleIndex;
  int sampleCount;  // Number of valid samples collected

  // Cached values
  float cachedVoltage;
  float cachedPercentage;
  bool isCharging;
  bool previousChargingState;

  // Timing
  unsigned long lastUpdateTime;
  bool initialized;

  // Read raw ADC value
  int readBatteryADC() {
    return analogRead(BATTERY_PIN);
  }

  // Read charging state from TP4056 (active low)
  bool readChargingState() {
    return digitalRead(BATTERY_CHARGING_PIN) == LOW;
  }

  // Calculate average of samples
  float getAveragedADC() {
    if (sampleCount == 0) return 0;

    long sum = 0;
    for (int i = 0; i < sampleCount; i++) {
      sum += adcSamples[i];
    }
    return (float)sum / sampleCount;
  }

  // Convert ADC value to voltage
  float adcToVoltage(float adcValue) {
    // If calibration values are set, use linear interpolation
    if (ADC_0_PERCENT != 0 || ADC_100_PERCENT != 4095) {
      // Map from calibrated ADC range to voltage range
      float voltage = mapFloat(adcValue, ADC_0_PERCENT, ADC_100_PERCENT,
                               BATTERY_VOLTAGE_MIN, BATTERY_VOLTAGE_MAX);
      return constrain(voltage, 0.0f, BATTERY_VOLTAGE_MAX + 0.5f);
    }

    // Default: direct ADC to voltage conversion (assumes no voltage divider)
    float voltage = (adcValue / (float)ADC_MAX_VALUE) * ADC_VOLTAGE_REF;
    return voltage;
  }

  // Calculate battery percentage from voltage
  float voltageToPercentage(float voltage) {
    // Use non-linear Li-ion discharge curve approximation
    // This provides more accurate readings across the discharge range

    if (voltage >= BATTERY_VOLTAGE_MAX) {
      return 100.0f;
    } else if (voltage <= BATTERY_VOLTAGE_MIN) {
      return 0.0f;
    }

    // Simplified Li-ion discharge curve (piecewise linear approximation)
    // 4.2V = 100%, 3.9V = 75%, 3.7V = 50%, 3.5V = 25%, 3.2V = 0%

    if (voltage > 3.9f) {
      // 100% - 75%: 4.2V to 3.9V (fast drop)
      return mapFloat(voltage, 3.9f, BATTERY_VOLTAGE_MAX, 75.0f, 100.0f);
    } else if (voltage > 3.7f) {
      // 75% - 50%: 3.9V to 3.7V (moderate drop)
      return mapFloat(voltage, 3.7f, 3.9f, 50.0f, 75.0f);
    } else if (voltage > 3.5f) {
      // 50% - 25%: 3.7V to 3.5V (slow drop)
      return mapFloat(voltage, 3.5f, 3.7f, 25.0f, 50.0f);
    } else {
      // 25% - 0%: 3.5V to 3.2V (fast drop)
      return mapFloat(voltage, BATTERY_VOLTAGE_MIN, 3.5f, 0.0f, 25.0f);
    }
  }

  // Float mapping function
  float mapFloat(float x, float in_min, float in_max, float out_min, float out_max) {
    if (x <= in_min) return out_min;
    if (x >= in_max) return out_max;
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
  }

public:
  BatteryManager() :
    currentSampleIndex(0),
    sampleCount(0),
    cachedVoltage(0.0f),
    cachedPercentage(0.0f),
    isCharging(false),
    previousChargingState(false),
    lastUpdateTime(0),
    initialized(false) {
    // Initialize sample buffer
    for (int i = 0; i < BATTERY_SAMPLE_COUNT; i++) {
      adcSamples[i] = 0;
    }
  }

  // Initialize the battery manager
  void begin() {
    pinMode(BATTERY_PIN, INPUT);
    pinMode(BATTERY_CHARGING_PIN, INPUT_PULLUP);  // TP4056 CHRG pin with pull-up

    // Read initial charging state
    isCharging = readChargingState();
    previousChargingState = isCharging;

    // Take initial samples to fill the buffer quickly
    for (int i = 0; i < BATTERY_SAMPLE_COUNT; i++) {
      adcSamples[i] = readBatteryADC();
      delay(10);  // Small delay between samples
    }
    sampleCount = BATTERY_SAMPLE_COUNT;
    currentSampleIndex = 0;

    // Calculate initial values
    updateCachedValues();

    initialized = true;
    lastUpdateTime = millis();

    Serial.println("Battery Manager initialized");
    Serial.printf("Charging pin (GPIO %d) state: %s (raw: %d)\n",
                  BATTERY_CHARGING_PIN,
                  isCharging ? "CHARGING" : "NOT CHARGING",
                  digitalRead(BATTERY_CHARGING_PIN));
    Serial.printf("Initial voltage: %.2fV, percentage: %.0f%%, charging: %s\n",
                  cachedVoltage, cachedPercentage, isCharging ? "YES" : "NO");
  }

  // Update battery readings (call this in main loop)
  // Returns true if values were updated
  bool update() {
    if (!initialized) return false;

    unsigned long currentTime = millis();

    // Check if it's time for an update
    if (currentTime - lastUpdateTime >= BATTERY_UPDATE_INTERVAL) {
      // Add new sample to buffer
      adcSamples[currentSampleIndex] = readBatteryADC();
      currentSampleIndex = (currentSampleIndex + 1) % BATTERY_SAMPLE_COUNT;

      // Update sample count (max at BATTERY_SAMPLE_COUNT)
      if (sampleCount < BATTERY_SAMPLE_COUNT) {
        sampleCount++;
      }

      // Update cached values
      updateCachedValues();

      lastUpdateTime = currentTime;
      return true;
    }

    return false;
  }

  // Force an immediate update (useful for testing)
  void forceUpdate() {
    adcSamples[currentSampleIndex] = readBatteryADC();
    currentSampleIndex = (currentSampleIndex + 1) % BATTERY_SAMPLE_COUNT;
    if (sampleCount < BATTERY_SAMPLE_COUNT) {
      sampleCount++;
    }
    updateCachedValues();
    lastUpdateTime = millis();
  }

  // Update cached voltage and percentage values
  void updateCachedValues() {
    float avgADC = getAveragedADC();
    cachedVoltage = adcToVoltage(avgADC);
    cachedPercentage = voltageToPercentage(cachedVoltage);

    // Update charging state and detect changes
    int rawPinState = digitalRead(BATTERY_CHARGING_PIN);
    bool newChargingState = readChargingState();

    // Debug logging - print pin state every update
    Serial.printf("DEBUG: Charging pin %d raw value: %d, interpreted as: %s\n",
                  BATTERY_CHARGING_PIN, rawPinState,
                  newChargingState ? "CHARGING" : "NOT CHARGING");

    // Detect charging state change
    if (newChargingState != previousChargingState) {
      if (newChargingState) {
        Serial.println("*** CHARGING STARTED ***");
        Serial.printf("Charging pin went LOW (pin reading: %d)\n", rawPinState);
        Serial.printf("Battery voltage: %.2fV, percentage: %.0f%%\n",
                      cachedVoltage, cachedPercentage);
      } else {
        Serial.println("*** CHARGING STOPPED ***");
        Serial.printf("Charging pin went HIGH (pin reading: %d)\n", rawPinState);
        Serial.printf("Battery voltage: %.2fV, percentage: %.0f%%\n",
                      cachedVoltage, cachedPercentage);
      }
      previousChargingState = newChargingState;
    }

    isCharging = newChargingState;
  }

  // Getters for battery information
  float getVoltage() const {
    return cachedVoltage;
  }

  float getPercentage() const {
    return cachedPercentage;
  }

  bool getIsCharging() const {
    return isCharging;
  }

  unsigned long getLastUpdateTime() const {
    return lastUpdateTime;
  }

  // Get raw ADC value (useful for debugging)
  int getRawADC() {
    return readBatteryADC();
  }

  // Get averaged ADC value (useful for debugging)
  float getAveragedADCValue() {
    return getAveragedADC();
  }

  // Check if battery is critically low
  bool isCriticallyLow() const {
    return cachedPercentage < 5.0f;
  }

  // Check if battery is low
  bool isLow() const {
    return cachedPercentage < 20.0f;
  }

  // Get battery health status as string
  const char* getStatusString() const {
    return isCharging ? "Charging" : "Not Charging";
  }

  // Debug: Print detailed charging pin information
  void debugChargingPin() {
    Serial.println("=== CHARGING PIN DEBUG ===");
    Serial.printf("Pin number: GPIO %d\n", BATTERY_CHARGING_PIN);

    // Read pin multiple times to check for consistency
    int reading1 = digitalRead(BATTERY_CHARGING_PIN);
    delay(10);
    int reading2 = digitalRead(BATTERY_CHARGING_PIN);
    delay(10);
    int reading3 = digitalRead(BATTERY_CHARGING_PIN);

    Serial.printf("Pin readings: %d, %d, %d\n", reading1, reading2, reading3);
    Serial.printf("Interpreted as: %s (expecting LOW=0 for charging)\n",
                  readChargingState() ? "CHARGING" : "NOT CHARGING");
    Serial.printf("Current cached state: %s\n", isCharging ? "CHARGING" : "NOT CHARGING");
    Serial.printf("Battery voltage: %.2fV\n", cachedVoltage);
    Serial.println("==========================");
  }
};

// Global battery manager instance
extern BatteryManager batteryManager;

#endif
