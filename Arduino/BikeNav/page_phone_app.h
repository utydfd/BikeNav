#ifndef PAGE_PHONE_APP_H
#define PAGE_PHONE_APP_H
#include <GxEPD2_BW.h>
#include <U8g2_for_Adafruit_GFX.h>
#include "notification_system.h"
#include "bitmaps.h"
#include "status_bar.h"

extern GxEPD2_BW<GxEPD2_290_BS, GxEPD2_290_BS::HEIGHT> display;
extern U8G2_FOR_ADAFRUIT_GFX u8g2_display;
extern const int DISPLAY_WIDTH;
extern const int DISPLAY_HEIGHT;
extern bool deviceConnected;
extern bool deviceStatusReceived;
extern bool deviceStatusChanged;

// Music control icons
extern const unsigned char ICON_MUSIC_PREVIOUS[];
extern const unsigned char ICON_MUSIC_PLAY[];
extern const unsigned char ICON_MUSIC_PAUSE[];
extern const unsigned char ICON_MUSIC_NEXT[];
extern const int MUSIC_ICON_SIZE;

// DeviceStatusPacket and currentDeviceStatus are defined in ble_handler.h
// (already included in BikeNav.ino before this file)

// Device control functions from ble_handler.h
extern void sendMusicPlayPause();
extern void sendMusicNext();
extern void sendMusicPrevious();
extern void sendLocatePhone();
extern void sendToggleNotificationSync();
extern void sendRequestDeviceStatus();

// Selectable items on the phone app page
enum PhoneAppItem {
    ITEM_MUSIC_PREVIOUS = 0,  // First button (left)
    ITEM_MUSIC_PLAY_PAUSE,     // Second button (center)
    ITEM_MUSIC_NEXT,           // Third button (right)
    ITEM_NOTIFICATION_SYNC,    // Fourth item
    ITEM_LOCATE_PHONE,         // Fifth item
    ITEM_COUNT
};

int selectedPhoneItem = ITEM_MUSIC_PLAY_PAUSE;
unsigned long lastDeviceStatusRequest = 0;
bool showLocatePhoneConfirmation = false;
int locatePhoneConfirmSelection = 0; // 0 = Cancel, 1 = Confirm

void initPhoneAppPage() {
    selectedPhoneItem = ITEM_MUSIC_PLAY_PAUSE;
    showLocatePhoneConfirmation = false;
    locatePhoneConfirmSelection = 0;
    // Request initial device status
    sendRequestDeviceStatus();
    lastDeviceStatusRequest = millis();
}

// Helper function to check if music controls should be enabled
bool areMusicControlsEnabled() {
    String songTitle = String(currentDeviceStatus.songTitle);
    songTitle.trim();
    return (songTitle.length() > 0 &&
            songTitle != " " &&
            songTitle.charAt(0) != '\0');
}

// Draw a bordered button/switch
void drawSelectableItem(int x, int y, int width, int height, const char* label, bool isSelected, bool showCheckbox = false, bool checkboxValue = false) {
    // Draw border if selected
    if (isSelected) {
        display.fillRect(x - 2, y - 2, width + 4, height + 4, GxEPD_BLACK);
        display.fillRect(x, y, width, height, GxEPD_WHITE);
    } else {
        display.drawRect(x, y, width, height, GxEPD_BLACK);
    }

    // Draw label with medium-sized font that fits better
    u8g2_display.setFont(u8g2_font_helvB08_tf);
    u8g2_display.setForegroundColor(GxEPD_BLACK);
    u8g2_display.setCursor(x + 8, y + height / 2 + 4);
    u8g2_display.print(label);

    // Draw checkbox if needed
    if (showCheckbox) {
        int checkboxSize = 14;
        int checkboxX = x + width - checkboxSize - 8;
        int checkboxY = y + (height - checkboxSize) / 2;
        display.drawRect(checkboxX, checkboxY, checkboxSize, checkboxSize, GxEPD_BLACK);
        if (checkboxValue) {
            // Draw check mark
            display.fillRect(checkboxX + 3, checkboxY + 3, checkboxSize - 6, checkboxSize - 6, GxEPD_BLACK);
        }
    }
}

// Draw a button with a bitmap icon
void drawIconButton(int x, int y, int width, int height, const unsigned char* icon, int iconSize, bool isSelected, bool isEnabled = true) {
    if (!isEnabled) {
        // Draw dotted border for disabled state
        for (int i = x; i < x + width; i += 3) {
            display.drawPixel(i, y, GxEPD_BLACK);
            display.drawPixel(i, y + height - 1, GxEPD_BLACK);
        }
        for (int i = y; i < y + height; i += 3) {
            display.drawPixel(x, i, GxEPD_BLACK);
            display.drawPixel(x + width - 1, i, GxEPD_BLACK);
        }

        // Draw icon with dotted pattern (every other pixel)
        int iconX = x + (width - iconSize) / 2;
        int iconY = y + (height - iconSize) / 2;
        for (int dy = 0; dy < iconSize; dy++) {
            for (int dx = 0; dx < iconSize; dx++) {
                int byteIndex = dy * ((iconSize + 7) / 8) + (dx / 8);
                int bitIndex = 7 - (dx % 8);
                if ((icon[byteIndex] >> bitIndex) & 0x01) {
                    // Only draw every other pixel for grayed-out effect
                    if ((dx + dy) % 2 == 0) {
                        display.drawPixel(iconX + dx, iconY + dy, GxEPD_BLACK);
                    }
                }
            }
        }
    } else {
        // Normal enabled state
        if (isSelected) {
            display.fillRect(x - 2, y - 2, width + 4, height + 4, GxEPD_BLACK);
            display.fillRect(x, y, width, height, GxEPD_WHITE);
        } else {
            display.drawRect(x, y, width, height, GxEPD_BLACK);
        }

        // Draw icon centered in button
        int iconX = x + (width - iconSize) / 2;
        int iconY = y + (height - iconSize) / 2;
        display.drawBitmap(iconX, iconY, icon, iconSize, iconSize, GxEPD_BLACK);
    }
}

// Draw a section header
void drawSectionHeader(int y, const char* title) {
    u8g2_display.setFont(u8g2_font_helvB12_tf);
    u8g2_display.setForegroundColor(GxEPD_BLACK);
    u8g2_display.setCursor(4, y);
    u8g2_display.print(title);
    // Draw underline across full width
    display.drawLine(4, y + 3, DISPLAY_WIDTH - 4, y + 3, GxEPD_BLACK);
}

// Draw an info row (non-interactive)
void drawInfoRow(int y, const char* label, const char* value) {
    u8g2_display.setFont(u8g2_font_helvR10_tf);
    u8g2_display.setForegroundColor(GxEPD_BLACK);
    u8g2_display.setCursor(8, y);
    u8g2_display.print(label);
    u8g2_display.print(": ");
    u8g2_display.print(value);
}

// Draw battery icon with percentage
void drawBatteryIcon(int x, int y, int percent, bool charging) {
    int width = 24;
    int height = 12;

    // Battery outline
    display.drawRect(x, y, width, height, GxEPD_BLACK);
    // Battery tip
    display.fillRect(x + width, y + 3, 2, height - 6, GxEPD_BLACK);

    // Battery fill based on percentage
    int fillWidth = (width - 4) * percent / 100;
    if (fillWidth > 0) {
        display.fillRect(x + 2, y + 2, fillWidth, height - 4, GxEPD_BLACK);
    }

    // Charging indicator (lightning bolt symbol)
    if (charging) {
        u8g2_display.setFont(u8g2_font_helvB08_tf);
        u8g2_display.setCursor(x + width + 6, y + 10);
        u8g2_display.print("+");
    }
}

// Draw signal strength bars
void drawSignalBars(int x, int y, int strength) {
    // strength is 0-100, convert to 0-4 bars
    int bars = (strength + 20) / 25; // 0-4
    int barWidth = 3;
    int barSpacing = 1;
    int maxHeight = 10;

    for (int i = 0; i < 4; i++) {
        int barHeight = (i + 1) * maxHeight / 4;
        int barY = y + maxHeight - barHeight;

        if (i < bars) {
            display.fillRect(x + i * (barWidth + barSpacing), barY, barWidth, barHeight, GxEPD_BLACK);
        } else {
            display.drawRect(x + i * (barWidth + barSpacing), barY, barWidth, barHeight, GxEPD_BLACK);
        }
    }
}

// Draw confirmation popup with shadow
void drawConfirmationPopup(const char* message, int selection) {
    int popupWidth = 120;
    int popupHeight = 85;
    int popupX = (DISPLAY_WIDTH - popupWidth) / 2;
    int popupY = (DISPLAY_HEIGHT - popupHeight) / 2;
    int shadowOffset = 3;

    // Draw shadow
    display.fillRect(popupX + shadowOffset, popupY + shadowOffset,
                     popupWidth, popupHeight, GxEPD_BLACK);

    // Draw white popup background
    display.fillRect(popupX, popupY, popupWidth, popupHeight, GxEPD_WHITE);

    // Draw popup border
    display.drawRect(popupX, popupY, popupWidth, popupHeight, GxEPD_BLACK);
    display.drawRect(popupX + 1, popupY + 1, popupWidth - 2, popupHeight - 2, GxEPD_BLACK);

    // Draw message with bigger font
    u8g2_display.setFont(u8g2_font_helvB10_tf);
    u8g2_display.setForegroundColor(GxEPD_BLACK);
    u8g2_display.setBackgroundColor(GxEPD_WHITE);

    int msgWidth = u8g2_display.getUTF8Width(message);
    u8g2_display.setCursor(popupX + (popupWidth - msgWidth) / 2, popupY + 24);
    u8g2_display.print(message);

    // Draw buttons
    int buttonWidth = 48;
    int buttonHeight = 24;
    int buttonY = popupY + popupHeight - buttonHeight - 10;
    int buttonSpacing = 8;

    // Cancel button (left)
    int cancelX = popupX + 8;
    if (selection == 0) {
        // Selected: black background with white text
        display.fillRect(cancelX, buttonY, buttonWidth, buttonHeight, GxEPD_BLACK);
        u8g2_display.setFont(u8g2_font_helvB08_tf);
        u8g2_display.setForegroundColor(GxEPD_WHITE);
        u8g2_display.setBackgroundColor(GxEPD_BLACK);
        int cancelWidth = u8g2_display.getUTF8Width("Cancel");
        u8g2_display.setCursor(cancelX + (buttonWidth - cancelWidth) / 2, buttonY + 16);
        u8g2_display.print("Cancel");
    } else {
        // Not selected: white background with black border and text
        display.drawRect(cancelX, buttonY, buttonWidth, buttonHeight, GxEPD_BLACK);
        u8g2_display.setFont(u8g2_font_helvB08_tf);
        u8g2_display.setForegroundColor(GxEPD_BLACK);
        u8g2_display.setBackgroundColor(GxEPD_WHITE);
        int cancelWidth = u8g2_display.getUTF8Width("Cancel");
        u8g2_display.setCursor(cancelX + (buttonWidth - cancelWidth) / 2, buttonY + 16);
        u8g2_display.print("Cancel");
    }

    // Confirm button (right)
    int confirmX = popupX + popupWidth - buttonWidth - 8;
    if (selection == 1) {
        // Selected: black background with white text
        display.fillRect(confirmX, buttonY, buttonWidth, buttonHeight, GxEPD_BLACK);
        u8g2_display.setFont(u8g2_font_helvB08_tf);
        u8g2_display.setForegroundColor(GxEPD_WHITE);
        u8g2_display.setBackgroundColor(GxEPD_BLACK);
        int confirmWidth = u8g2_display.getUTF8Width("OK");
        u8g2_display.setCursor(confirmX + (buttonWidth - confirmWidth) / 2, buttonY + 16);
        u8g2_display.print("OK");
    } else {
        // Not selected: white background with black border and text
        display.drawRect(confirmX, buttonY, buttonWidth, buttonHeight, GxEPD_BLACK);
        u8g2_display.setFont(u8g2_font_helvB08_tf);
        u8g2_display.setForegroundColor(GxEPD_BLACK);
        u8g2_display.setBackgroundColor(GxEPD_WHITE);
        int confirmWidth = u8g2_display.getUTF8Width("OK");
        u8g2_display.setCursor(confirmX + (buttonWidth - confirmWidth) / 2, buttonY + 16);
        u8g2_display.print("OK");
    }
}

void renderPhoneAppPage() {
    display.setPartialWindow(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    display.firstPage();

    do {
        display.fillScreen(GxEPD_WHITE);
        u8g2_display.setFontMode(1);
        u8g2_display.setBackgroundColor(GxEPD_WHITE);

        int yPos = 20;

        if (!deviceConnected || !deviceStatusReceived) {
            // Show "Connect phone to see info" message (multi-line)
            u8g2_display.setFont(u8g2_font_helvB10_tf);
            u8g2_display.setForegroundColor(GxEPD_BLACK);

            const char* line1 = "Connect phone";
            const char* line2 = "to see info";

            int line1Width = u8g2_display.getUTF8Width(line1);
            int line2Width = u8g2_display.getUTF8Width(line2);

            int centerY = DISPLAY_HEIGHT / 2;
            u8g2_display.setCursor((DISPLAY_WIDTH - line1Width) / 2, centerY - 8);
            u8g2_display.print(line1);
            u8g2_display.setCursor((DISPLAY_WIDTH - line2Width) / 2, centerY + 8);
            u8g2_display.print(line2);
        } else {
            // Song info (non-interactive, centered) with adaptive font sizing
            String songTitle = String(currentDeviceStatus.songTitle);
            // Trim leading/trailing whitespace and null characters
            songTitle.trim();
            if (songTitle.length() == 0 || songTitle == " " || songTitle.charAt(0) == '\0') {
                songTitle = "No song playing";
            }

            // Adaptive font sizing based on text width
            const int maxWidth = DISPLAY_WIDTH - 8; // Leave 4px margin on each side
            u8g2_display.setFont(u8g2_font_helvB12_tf); // Try large font first
            int songWidth = u8g2_display.getUTF8Width(songTitle.c_str());

            if (songWidth > maxWidth) {
                // Try medium font
                u8g2_display.setFont(u8g2_font_helvB10_tf);
                songWidth = u8g2_display.getUTF8Width(songTitle.c_str());

                if (songWidth > maxWidth) {
                    // Use small font and truncate if still too long
                    u8g2_display.setFont(u8g2_font_helvB08_tf);
                    songWidth = u8g2_display.getUTF8Width(songTitle.c_str());

                    // Truncate with ellipsis if still too long
                    while (songWidth > maxWidth && songTitle.length() > 3) {
                        songTitle = songTitle.substring(0, songTitle.length() - 1);
                        String tempTitle = songTitle + "...";
                        songWidth = u8g2_display.getUTF8Width(tempTitle.c_str());
                        if (songWidth <= maxWidth) {
                            songTitle = tempTitle;
                            break;
                        }
                    }
                    songWidth = u8g2_display.getUTF8Width(songTitle.c_str());
                }
            }

            u8g2_display.setCursor((DISPLAY_WIDTH - songWidth) / 2, yPos);
            u8g2_display.print(songTitle);
            yPos += 16;

            String songArtist = String(currentDeviceStatus.songArtist);
            songArtist.trim();
            if (songArtist.length() > 0 && songArtist != " " && songArtist.charAt(0) != '\0') {
                if (songArtist.length() > 18) {
                    songArtist = songArtist.substring(0, 18) + "...";
                }
                u8g2_display.setFont(u8g2_font_helvR10_tf);
                int artistWidth = u8g2_display.getUTF8Width(songArtist.c_str());
                u8g2_display.setCursor((DISPLAY_WIDTH - artistWidth) / 2, yPos);
                u8g2_display.print(songArtist);
                yPos += 14;
            }
            yPos += 16;

            // Music control buttons (3 buttons side by side with icons)
            int buttonSize = 30;  // Square buttons
            int buttonSpacing = 8;
            int totalButtonWidth = (buttonSize * 3) + (buttonSpacing * 2);
            int buttonsX = (DISPLAY_WIDTH - totalButtonWidth) / 2;

            // Check if music controls should be enabled (only if there's a valid song)
            bool musicControlsEnabled = areMusicControlsEnabled();

            // Previous button
            drawIconButton(buttonsX, yPos, buttonSize, buttonSize,
                          ICON_MUSIC_PREVIOUS, MUSIC_ICON_SIZE,
                          selectedPhoneItem == ITEM_MUSIC_PREVIOUS,
                          musicControlsEnabled);

            // Play/Pause button (changes icon based on state)
            const unsigned char* playPauseIcon = currentDeviceStatus.musicPlaying ? ICON_MUSIC_PAUSE : ICON_MUSIC_PLAY;
            drawIconButton(buttonsX + buttonSize + buttonSpacing, yPos, buttonSize, buttonSize,
                          playPauseIcon, MUSIC_ICON_SIZE,
                          selectedPhoneItem == ITEM_MUSIC_PLAY_PAUSE,
                          musicControlsEnabled);

            // Next button
            drawIconButton(buttonsX + (buttonSize + buttonSpacing) * 2, yPos, buttonSize, buttonSize,
                          ICON_MUSIC_NEXT, MUSIC_ICON_SIZE,
                          selectedPhoneItem == ITEM_MUSIC_NEXT,
                          musicControlsEnabled);
            yPos += buttonSize + 18;

            // Notification sync toggle - full width
            drawSelectableItem(4, yPos, DISPLAY_WIDTH - 8, 24,
                               "Notification Sync", selectedPhoneItem == ITEM_NOTIFICATION_SYNC,
                               true, currentDeviceStatus.notificationSyncEnabled);
            yPos += 54;

            // ===== PHONE STATUS SECTION =====
            drawSectionHeader(yPos, "Phone Status");
            yPos += 20;

            // Battery info with larger icon
            char batteryText[32];
            sprintf(batteryText, "%d%%", currentDeviceStatus.phoneBatteryPercent);
            drawInfoRow(yPos, "Battery", batteryText);
            drawBatteryIcon(DISPLAY_WIDTH - 38, yPos - 12, currentDeviceStatus.phoneBatteryPercent, currentDeviceStatus.phoneCharging);
            yPos += 16;

            // Network info - show WiFi SSID if connected, otherwise cellular type
            if (currentDeviceStatus.wifiConnected) {
                // WiFi connected - show SSID without label
                String wifiInfo = String(currentDeviceStatus.wifiSsid);
                wifiInfo.trim();
                if (wifiInfo.length() == 0 || wifiInfo == "<unknown ssid>") {
                    wifiInfo = "WiFi";
                }
                u8g2_display.setFont(u8g2_font_helvR10_tf);
                u8g2_display.setForegroundColor(GxEPD_BLACK);
                u8g2_display.setCursor(8, yPos);
                u8g2_display.print(wifiInfo.c_str());
                drawSignalBars(DISPLAY_WIDTH - 24, yPos - 12, currentDeviceStatus.wifiSignalStrength);
            } else {
                // Not on WiFi - check if on cellular
                String cellularType = String(currentDeviceStatus.cellularType);
                cellularType.trim();

                if (cellularType.length() > 0 && cellularType != "Unknown") {
                    // On cellular data - show type (5G, LTE, etc.) without label
                    u8g2_display.setFont(u8g2_font_helvR10_tf);
                    u8g2_display.setForegroundColor(GxEPD_BLACK);
                    u8g2_display.setCursor(8, yPos);
                    u8g2_display.print(cellularType.c_str());
                    drawSignalBars(DISPLAY_WIDTH - 24, yPos - 12, currentDeviceStatus.cellularSignalStrength);
                } else {
                    // No connection
                    u8g2_display.setFont(u8g2_font_helvR10_tf);
                    u8g2_display.setForegroundColor(GxEPD_BLACK);
                    u8g2_display.setCursor(8, yPos);
                    u8g2_display.print("No connection");
                }
            }
            yPos += 20;

            // Locate phone button - full width
            drawSelectableItem(4, yPos, DISPLAY_WIDTH - 8, 24,
                               "Locate Phone", selectedPhoneItem == ITEM_LOCATE_PHONE);
            yPos += 26;
        }

        // Draw status bar at bottom
        drawStatusBar();

        // Draw notification overlay
        drawNotificationOverlay();

        // Draw confirmation popup if active
        if (showLocatePhoneConfirmation) {
            drawConfirmationPopup("Locate phone?", locatePhoneConfirmSelection);
        }

    } while (display.nextPage());
}

void updatePhoneAppPage() {
    // Check if device status has changed and refresh screen
    if (deviceStatusChanged) {
        deviceStatusChanged = false;  // Clear flag
        Serial.println("[PHONE_APP] Device status changed, refreshing display");
        renderPhoneAppPage();
    }

    // Periodically request device status updates (every 5 seconds)
    if (deviceConnected && (millis() - lastDeviceStatusRequest > 5000)) {
        sendRequestDeviceStatus();
        lastDeviceStatusRequest = millis();
    }

    // Update status bar with lower priority (won't refresh during user activity)
    updateStatusBar(false);
}

void handlePhoneAppEncoder(int delta) {
    // Mark user activity
    markUserActivity();

    // Handle confirmation popup navigation
    if (showLocatePhoneConfirmation) {
        if (delta != 0) {
            locatePhoneConfirmSelection = 1 - locatePhoneConfirmSelection; // Toggle between 0 and 1
            renderPhoneAppPage();
        }
        return;
    }

    // Don't allow scrolling if not connected or no data
    if (!deviceConnected || !deviceStatusReceived) return;

    bool musicEnabled = areMusicControlsEnabled();
    int oldItem = selectedPhoneItem;

    if (delta > 0) {
        selectedPhoneItem = (selectedPhoneItem + 1) % ITEM_COUNT;

        // Skip disabled music controls
        if (!musicEnabled && selectedPhoneItem >= ITEM_MUSIC_PREVIOUS && selectedPhoneItem <= ITEM_MUSIC_NEXT) {
            selectedPhoneItem = ITEM_NOTIFICATION_SYNC;
        }
    } else if (delta < 0) {
        selectedPhoneItem = (selectedPhoneItem - 1 + ITEM_COUNT) % ITEM_COUNT;

        // Skip disabled music controls (going backwards)
        if (!musicEnabled && selectedPhoneItem >= ITEM_MUSIC_PREVIOUS && selectedPhoneItem <= ITEM_MUSIC_NEXT) {
            selectedPhoneItem = (ITEM_MUSIC_PREVIOUS - 1 + ITEM_COUNT) % ITEM_COUNT;
        }
    }

    renderPhoneAppPage();
}

void handlePhoneAppButton() {
    // Mark user activity
    markUserActivity();

    // Handle confirmation popup button press
    if (showLocatePhoneConfirmation) {
        if (locatePhoneConfirmSelection == 1) {
            // OK selected - locate phone
            Serial.println("Action: Locate Phone");
            sendLocatePhone();

            // Close confirmation
            showLocatePhoneConfirmation = false;

            // Show feedback
            display.setPartialWindow(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
            display.firstPage();
            do {
                display.fillScreen(GxEPD_WHITE);
                u8g2_display.setFontMode(1);
                u8g2_display.setForegroundColor(GxEPD_BLACK);
                u8g2_display.setBackgroundColor(GxEPD_WHITE);
                u8g2_display.setFont(u8g2_font_helvB12_tf);
                int textWidth = u8g2_display.getUTF8Width("Phone ringing...");
                u8g2_display.setCursor((DISPLAY_WIDTH - textWidth) / 2, DISPLAY_HEIGHT / 2);
                u8g2_display.print("Phone ringing...");
            } while (display.nextPage());

            delay(1500);
        } else {
            // Cancel selected
            showLocatePhoneConfirmation = false;
        }
        renderPhoneAppPage();
        return;
    }

    if (!deviceConnected || !deviceStatusReceived) return;

    switch (selectedPhoneItem) {
        case ITEM_MUSIC_PLAY_PAUSE:
            // Ignore if music controls are disabled
            if (!areMusicControlsEnabled()) {
                Serial.println("Music controls disabled - ignoring Play/Pause");
                return;
            }
            Serial.println("Music: Play/Pause");
            sendMusicPlayPause();
            // Immediately update UI (optimistic update)
            currentDeviceStatus.musicPlaying = !currentDeviceStatus.musicPlaying;
            renderPhoneAppPage();
            break;

        case ITEM_MUSIC_PREVIOUS:
            // Ignore if music controls are disabled
            if (!areMusicControlsEnabled()) {
                Serial.println("Music controls disabled - ignoring Previous");
                return;
            }
            Serial.println("Music: Previous");
            sendMusicPrevious();
            break;

        case ITEM_MUSIC_NEXT:
            // Ignore if music controls are disabled
            if (!areMusicControlsEnabled()) {
                Serial.println("Music controls disabled - ignoring Next");
                return;
            }
            Serial.println("Music: Next");
            sendMusicNext();
            break;

        case ITEM_NOTIFICATION_SYNC:
            Serial.println("Toggle: Notification Sync");
            sendToggleNotificationSync();
            // Immediately update UI (optimistic update)
            currentDeviceStatus.notificationSyncEnabled = !currentDeviceStatus.notificationSyncEnabled;
            renderPhoneAppPage();
            break;

        case ITEM_LOCATE_PHONE:
            // Show confirmation popup
            showLocatePhoneConfirmation = true;
            locatePhoneConfirmSelection = 0; // Default to Cancel
            renderPhoneAppPage();
            break;
    }
}

#endif
