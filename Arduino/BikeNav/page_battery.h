// page_battery.h
#ifndef PAGE_BATTERY_H
#define PAGE_BATTERY_H

#include <GxEPD2_BW.h>
#include <U8g2_for_Adafruit_GFX.h>
#include "notification_system.h"
#include "battery_manager.h"

extern GxEPD2_BW<GxEPD2_290_BS, GxEPD2_290_BS::HEIGHT> display;
extern U8G2_FOR_ADAFRUIT_GFX u8g2_display;
extern const int DISPLAY_WIDTH;
extern const int DISPLAY_HEIGHT;

// External declarations from page_info.h
extern void drawPageIndicator(int currentPage, int totalPages);
extern const int TOTAL_INFO_PAGES;

// Render battery info page
void renderBatteryInfoPage() {
  display.setPartialWindow(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
  display.firstPage();

  do {
    display.fillScreen(GxEPD_WHITE);

    // Setup text rendering
    u8g2_display.setFontMode(1);
    u8g2_display.setForegroundColor(GxEPD_BLACK);
    u8g2_display.setBackgroundColor(GxEPD_WHITE);

    // Title
    u8g2_display.setFont(u8g2_font_helvB12_tf);
    u8g2_display.setCursor(10, 25);
    u8g2_display.print("BATTERY");

    // Draw separator line
    display.drawLine(0, 35, DISPLAY_WIDTH, 35, GxEPD_BLACK);

    // Get battery readings from battery manager
    float batteryVoltage = batteryManager.getVoltage();
    float batteryPercentage = batteryManager.getPercentage();
    bool batteryCharging = batteryManager.getIsCharging();

    // Battery percentage - large display with smaller % sign
    char percentNumber[8];
    snprintf(percentNumber, sizeof(percentNumber), "%.0f", batteryPercentage);

    // Calculate combined width for centering
    u8g2_display.setFont(u8g2_font_fub42_tn);
    int numberWidth = u8g2_display.getUTF8Width(percentNumber);
    u8g2_display.setFont(u8g2_font_helvB14_tf);
    int symbolWidth = u8g2_display.getUTF8Width("%");
    int totalWidth = numberWidth + symbolWidth + 2; // 2px spacing

    int startX = (DISPLAY_WIDTH - totalWidth) / 2;

    // Draw number
    u8g2_display.setFont(u8g2_font_fub42_tn);
    u8g2_display.setCursor(startX, 100);
    u8g2_display.print(percentNumber);

    // Draw % symbol (smaller, aligned to top)
    u8g2_display.setFont(u8g2_font_helvB14_tf);
    u8g2_display.setCursor(startX + numberWidth + 2, 75);
    u8g2_display.print("%");

    // Voltage
    u8g2_display.setFont(u8g2_font_helvB10_tf);
    char voltageText[32];
    snprintf(voltageText, sizeof(voltageText), "Voltage: %.2f V", batteryVoltage);
    u8g2_display.setCursor(10, 140);
    u8g2_display.print(voltageText);

    // Battery status
    u8g2_display.setFont(u8g2_font_helvB10_tf);
    char statusText[32];
    snprintf(statusText, sizeof(statusText), "Status: %s", batteryManager.getStatusString());
    u8g2_display.setCursor(10, 160);
    u8g2_display.print(statusText);

    // Battery health indicator (simple bar)
    int barX = 10;
    int barY = 190;
    int barWidth = DISPLAY_WIDTH - 20;
    int barHeight = 20;

    // Draw bar outline
    display.drawRect(barX, barY, barWidth, barHeight, GxEPD_BLACK);

    // Draw filled portion based on percentage
    if (batteryPercentage > 0) {
      int filledWidth = (barWidth - 4) * batteryPercentage / 100.0;
      display.fillRect(barX + 2, barY + 2, filledWidth, barHeight - 4, GxEPD_BLACK);
    }

    // Percentage text inside bar
    u8g2_display.setFont(u8g2_font_helvB08_tf);
    char barPercentText[8];
    snprintf(barPercentText, sizeof(barPercentText), "%.0f%%", batteryPercentage);
    int barPercentWidth = u8g2_display.getUTF8Width(barPercentText);

    // Determine text color based on fill
    if (batteryPercentage > 50) {
      // White text on black background
      u8g2_display.setForegroundColor(GxEPD_WHITE);
      u8g2_display.setBackgroundColor(GxEPD_BLACK);
    } else {
      // Black text on white background
      u8g2_display.setForegroundColor(GxEPD_BLACK);
      u8g2_display.setBackgroundColor(GxEPD_WHITE);
    }

    u8g2_display.setCursor(barX + (barWidth - barPercentWidth) / 2, barY + 15);
    u8g2_display.print(barPercentText);

    // Reset text colors
    u8g2_display.setForegroundColor(GxEPD_BLACK);
    u8g2_display.setBackgroundColor(GxEPD_WHITE);

    // Draw page indicator
    drawPageIndicator(2, TOTAL_INFO_PAGES);

    // Draw notification overlay
    drawNotificationOverlay();

  } while (display.nextPage());
}

#endif
