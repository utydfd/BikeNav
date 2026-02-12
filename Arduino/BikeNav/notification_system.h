// notification_system.h
#ifndef NOTIFICATION_SYSTEM_H
#define NOTIFICATION_SYSTEM_H

#include <Arduino.h>
#include <GxEPD2_BW.h>
#include <U8g2_for_Adafruit_GFX.h>

extern GxEPD2_BW<GxEPD2_290_BS, GxEPD2_290_BS::HEIGHT> display;
extern U8G2_FOR_ADAFRUIT_GFX u8g2_display;
extern const int DISPLAY_WIDTH;
extern const int DISPLAY_HEIGHT;

// Forward declarations for page rendering
enum PageType;  // Forward declare enum
extern PageType currentPage;

// Forward declaration from ble_handler.h
void sendNotificationDismissal(uint32_t notificationId);

// Render function declarations
void renderMainMenu();
void renderMapPage();
void renderSpeedometerPage();
void renderPhoneAppPage();
void renderWeatherPage();
void renderGamesPage();
void renderInfoPage();
void renderSettingsPage();
void renderTrackerPage();
void renderRecordingPage();
void renderRecordingOptionsPage();

// Notification types
enum NotificationType {
  NOTIFICATION_NORMAL = 0,  // Regular notification, dismissible
  NOTIFICATION_LIVE = 1     // Live notification, persists when panel collapses
};

// Notification structure
struct Notification {
  char heading[32];       // Notification heading (e.g., "Bluetooth")
  char line1[32];         // First line of text
  char line2[32];         // Second line of text
  const unsigned char* icon;  // Icon bitmap pointer (PROGMEM or points to iconData)
  uint8_t iconData[195];  // Embedded icon data for phone notifications (39x39 monochrome, MSB-first)
  bool hasDynamicIcon;    // true = use iconData, false = use icon (PROGMEM)
  unsigned long showTime; // When the notification was shown (millis())
  unsigned long duration; // How long to show (milliseconds, 0 = persistent)
  bool visible;           // Whether the notification is currently visible
  NotificationType type;  // Normal or live notification
  uint32_t phoneNotificationId; // ID of phone notification (0 if not from phone)

  Notification() : icon(nullptr), hasDynamicIcon(false), showTime(0), duration(0), visible(false), type(NOTIFICATION_NORMAL), phoneNotificationId(0) {
    heading[0] = '\0';
    line1[0] = '\0';
    line2[0] = '\0';
    memset(iconData, 0, 195); // Zero-fill icon data
  }
};

// Notification queue configuration
const int MAX_NOTIFICATIONS = 10;
Notification notificationQueue[MAX_NOTIFICATIONS];
int notificationCount = 0;

// Legacy single notification for overlay system (points to most recent)
Notification currentNotification;

// Notification dimensions
const int NOTIFICATION_HEIGHT = 46;
const int NOTIFICATION_PADDING = 4;
const int NOTIFICATION_ICON_SIZE = 39;
const int NOTIFICATION_TEXT_X = NOTIFICATION_PADDING + NOTIFICATION_ICON_SIZE + 2;

// === NOTIFICATION RENDERING STATE ===
const int NOTIFICATION_REFRESH_DEBOUNCE_MS = 500;  // Min 500ms between notification refreshes
const int NOTIFICATION_USER_ACTIVITY_DEBOUNCE_MS = 1000;  // Wait 1s after user activity before auto-refresh

struct NotificationRenderState {
  bool pendingRefresh = false;  // True if notification needs to be displayed
  unsigned long lastRefreshTime = 0;  // Last time notification overlay was refreshed
  unsigned long lastUserActivityTime = 0;  // Track last user interaction (shared with status bar)
  bool initialized = false;
};

NotificationRenderState notificationRenderState;

/**
 * Initialize the notification rendering state tracking
 * Call this once during setup
 */
void initNotificationSystem() {
  notificationRenderState.pendingRefresh = false;
  notificationRenderState.lastRefreshTime = 0;
  notificationRenderState.lastUserActivityTime = 0;
  notificationRenderState.initialized = true;
  Serial.println("Notification system initialized");
}

/**
 * Mark that user activity occurred (encoder scroll, button press)
 * This prevents notifications from auto-refreshing during active use
 * NOTE: This shares the same activity tracking as status bar via external markUserActivity()
 */
void markNotificationUserActivity() {
  notificationRenderState.lastUserActivityTime = millis();
}

/**
 * Force an immediate refresh of the current page to show the notification
 * Internal function used by smart rendering system
 */
void forceNotificationRefresh() {
  // Call the appropriate render function based on current page
  switch (currentPage) {
    case 0: renderMainMenu(); break;         // PAGE_MAIN_MENU
    case 1: renderMapPage(); break;          // PAGE_MAP
    case 2: renderSpeedometerPage(); break;  // PAGE_SPEEDOMETER
    case 3: renderPhoneAppPage(); break;     // PAGE_PHONE_APP
    case 4: renderWeatherPage(); break;      // PAGE_WEATHER
    case 5: renderGamesPage(); break;        // PAGE_GAMES
    case 6: renderInfoPage(); break;         // PAGE_INFO
    case 8: renderSettingsPage(); break;     // PAGE_SETTINGS
    case 9: renderTrackerPage(); break;      // PAGE_TRACKER
    case 10: renderRecordingPage(); break;   // PAGE_RECORDING
    case 11: renderRecordingOptionsPage(); break;  // PAGE_RECORDING_OPTIONS
    // PAGE_SHUTDOWN (7) doesn't need refresh
  }

  // Update refresh timestamp
  notificationRenderState.lastRefreshTime = millis();
}

/**
 * Get the total number of notifications in the queue
 */
int getNotificationCount() {
  return notificationCount;
}

/**
 * Get the number of live notifications in the queue
 */
int getLiveNotificationCount() {
  int count = 0;
  for (int i = 0; i < notificationCount; i++) {
    if (notificationQueue[i].type == NOTIFICATION_LIVE) {
      count++;
    }
  }
  return count;
}

/**
 * Add a notification to the queue
 * @param heading Notification heading text
 * @param line1 First line of body text
 * @param line2 Second line of body text
 * @param icon Bitmap icon to display (39x39 pixels from bitmaps.h)
 * @param type Notification type (NOTIFICATION_NORMAL or NOTIFICATION_LIVE)
 * @param duration How long to show notification in milliseconds (0 = persistent)
 */
void addNotification(const char* heading, const char* line1, const char* line2,
                     const unsigned char* icon, NotificationType type = NOTIFICATION_NORMAL,
                     unsigned long duration = 5000) {
  // If queue is full, remove oldest normal notification to make room
  if (notificationCount >= MAX_NOTIFICATIONS) {
    // Find and remove oldest normal notification
    for (int i = 0; i < notificationCount; i++) {
      if (notificationQueue[i].type == NOTIFICATION_NORMAL) {
        // Shift all notifications after this one forward
        for (int j = i; j < notificationCount - 1; j++) {
          notificationQueue[j] = notificationQueue[j + 1];
        }
        notificationCount--;
        break;
      }
    }
    // If still full (all live notifications), remove oldest one
    if (notificationCount >= MAX_NOTIFICATIONS) {
      for (int i = 0; i < notificationCount - 1; i++) {
        notificationQueue[i] = notificationQueue[i + 1];
      }
      notificationCount--;
    }
  }

  // Add new notification at the end
  Notification* notif = &notificationQueue[notificationCount];

  strncpy(notif->heading, heading, sizeof(notif->heading) - 1);
  notif->heading[sizeof(notif->heading) - 1] = '\0';

  strncpy(notif->line1, line1, sizeof(notif->line1) - 1);
  notif->line1[sizeof(notif->line1) - 1] = '\0';

  strncpy(notif->line2, line2, sizeof(notif->line2) - 1);
  notif->line2[sizeof(notif->line2) - 1] = '\0';

  notif->icon = icon;
  notif->showTime = millis();
  notif->duration = duration;
  notif->visible = true;
  notif->type = type;

  notificationCount++;

  // Update legacy currentNotification for overlay system
  currentNotification = *notif;
}

/**
 * Dismiss a notification at a specific index
 * @param index Index in the notification queue (0 to notificationCount-1)
 */
void dismissNotification(int index) {
  if (index < 0 || index >= notificationCount) {
    return;
  }

  // Check if this is a phone notification and send dismissal to phone
  if (notificationQueue[index].phoneNotificationId != 0) {
    sendNotificationDismissal(notificationQueue[index].phoneNotificationId);
  }

  // Shift all notifications after this one forward
  for (int i = index; i < notificationCount - 1; i++) {
    notificationQueue[i] = notificationQueue[i + 1];
  }

  notificationCount--;

  // Update legacy currentNotification if needed
  if (notificationCount > 0) {
    currentNotification = notificationQueue[notificationCount - 1];
  } else {
    currentNotification.visible = false;
  }
}

/**
 * Clear all notifications from the queue
 */
void clearAllNotifications() {
  notificationCount = 0;
  currentNotification.visible = false;
}

/**
 * Clear only normal notifications, keep live notifications
 */
void clearNormalNotifications() {
  int writeIndex = 0;
  for (int readIndex = 0; readIndex < notificationCount; readIndex++) {
    if (notificationQueue[readIndex].type == NOTIFICATION_LIVE) {
      if (writeIndex != readIndex) {
        notificationQueue[writeIndex] = notificationQueue[readIndex];
      }
      writeIndex++;
    }
  }
  notificationCount = writeIndex;

  // Update legacy currentNotification
  if (notificationCount > 0) {
    currentNotification = notificationQueue[notificationCount - 1];
  } else {
    currentNotification.visible = false;
  }
}

/**
 * Show a notification with the specified content
 * @param heading Notification heading text
 * @param line1 First line of body text
 * @param line2 Second line of body text
 * @param icon Bitmap icon to display (39x39 pixels from bitmaps.h)
 * @param duration How long to show notification in milliseconds (default 5000 = 5 seconds)
 */
void showNotification(const char* heading, const char* line1, const char* line2,
                     const unsigned char* icon, unsigned long duration = 5000) {
  // Use the new queue system
  addNotification(heading, line1, line2, icon, NOTIFICATION_NORMAL, duration);

  // Mark pending for smart rendering - will be displayed when safe
  notificationRenderState.pendingRefresh = true;
  Serial.println("Notification: Marked pending for smart refresh");
}

/**
 * Draw the notification overlay on top of the current page
 * Call this at the end of each page's render function
 */
void drawNotificationOverlay() {
  if (!currentNotification.visible) {
    return;
  }

  // Draw notification rectangle (missing top border for "popping from top" effect)
  int notifX = 0;
  int notifY = 0;
  int notifWidth = DISPLAY_WIDTH;
  int notifHeight = NOTIFICATION_HEIGHT;

  // Fill background with black for high contrast
  display.fillRect(notifX, notifY, notifWidth, notifHeight, GxEPD_BLACK);

  // Draw white frame inside the black fill to make it look like a distinct card
  // Inset by 2 pixels from edges for a clean border
  int frameInset = 2;
  int frameLeft = notifX + frameInset;                    // x = 2
  int frameRight = notifX + notifWidth - frameInset - 1;   // x = 125 (for 128px width)
  int frameTop = notifY + 0;                     // y = 2
  int frameBottom = notifY + notifHeight - frameInset - 1; // y = 49 (for 52px height)

  // Draw frame borders (left, right, bottom - no top for "popping from top" effect)
  display.drawLine(frameLeft, frameTop, frameLeft, frameBottom, GxEPD_WHITE);         // Left vertical
  display.drawLine(frameRight, frameTop, frameRight, frameBottom, GxEPD_WHITE);       // Right vertical
  display.drawLine(frameLeft, frameBottom, frameRight, frameBottom, GxEPD_WHITE);     // Bottom horizontal

  // Draw icon on the left (inverted colors)
  if (currentNotification.icon != nullptr) {
    int iconX = notifX + NOTIFICATION_PADDING;
    int iconY = notifY + (notifHeight - NOTIFICATION_ICON_SIZE) / 2;
    display.drawBitmap(iconX, iconY, currentNotification.icon,
                      NOTIFICATION_ICON_SIZE, NOTIFICATION_ICON_SIZE, GxEPD_WHITE);
  }

  // Draw text on the right (white text on black background)
  u8g2_display.setFontMode(1);
  u8g2_display.setForegroundColor(GxEPD_WHITE);
  u8g2_display.setBackgroundColor(GxEPD_BLACK);

  int textX = notifX + NOTIFICATION_TEXT_X;
  int textStartY = notifY + 16;  // Moved down from 12 to give more breathing room from top

  // Heading (bold, slightly larger)
  u8g2_display.setFont(u8g2_font_helvB10_tf);
  u8g2_display.setCursor(textX, textStartY);
  u8g2_display.print(currentNotification.heading);

  // Line 1 (regular, smaller - 7px height for better fit)
  u8g2_display.setFont(u8g2_font_profont10_tf);
  u8g2_display.setCursor(textX, textStartY + 12);
  u8g2_display.print(currentNotification.line1);

  // Line 2 (regular, smaller - 7px height for better fit)
  u8g2_display.setCursor(textX, textStartY + 23);
  u8g2_display.print(currentNotification.line2);
}

/**
 * Check if notification display needs refresh and perform smart update
 * Call this from main loop or page update functions
 * Similar to updateStatusBar() - coordinates with user activity and respects debouncing
 * @param forceUpdate If true, bypass user activity check (use when page is already re-rendering)
 * @return true if refresh was performed
 */
bool updateNotificationDisplay(bool forceUpdate = false) {
  if (!notificationRenderState.initialized) {
    return false;
  }

  unsigned long currentTime = millis();

  // Check if enough time has passed since last refresh (debouncing)
  if (currentTime - notificationRenderState.lastRefreshTime < NOTIFICATION_REFRESH_DEBOUNCE_MS) {
    return false;
  }

  // PRIORITY CHECK: Don't auto-refresh during user activity (unless forced)
  // This keeps the UI responsive during scrolling/interaction
  if (!forceUpdate) {
    unsigned long timeSinceActivity = currentTime - notificationRenderState.lastUserActivityTime;
    if (timeSinceActivity < NOTIFICATION_USER_ACTIVITY_DEBOUNCE_MS) {
      // User is actively interacting, skip auto-refresh
      return false;
    }
  }

  // Check if there's a pending notification refresh
  if (!notificationRenderState.pendingRefresh) {
    return false;
  }

  // Perform refresh - call the page render function
  Serial.println("Notification: Performing smart refresh");
  forceNotificationRefresh();

  // Clear pending flag
  notificationRenderState.pendingRefresh = false;

  return true;
}

/**
 * Update notification state (handle auto-dismiss)
 * Call this in the main loop
 */
void updateNotifications() {
  // Check all notifications for auto-dismiss
  bool needsRefresh = false;
  unsigned long currentTime = millis();

  for (int i = notificationCount - 1; i >= 0; i--) {
    Notification* notif = &notificationQueue[i];

    // For NOTIFICATION_LIVE with duration > 0 (phone notifications):
    // Hide overlay after duration expires, but keep in queue
    if (notif->type == NOTIFICATION_LIVE && notif->duration > 0) {
      unsigned long elapsed = currentTime - notif->showTime;
      if (elapsed >= notif->duration) {
        // Check if this is the current overlay notification
        if (currentNotification.phoneNotificationId == notif->phoneNotificationId &&
            currentNotification.phoneNotificationId != 0) {
          // Hide the overlay but keep notification in queue
          currentNotification.visible = false;
          needsRefresh = true;
        }
      }
      continue;  // Don't dismiss NOTIFICATION_LIVE from queue
    }

    // Skip persistent notifications (duration = 0)
    if (notif->duration == 0) {
      continue;
    }

    // For NOTIFICATION_NORMAL: dismiss after duration expires
    unsigned long elapsed = currentTime - notif->showTime;
    if (elapsed >= notif->duration) {
      dismissNotification(i);
      needsRefresh = true;
    }
  }

  // Update legacy currentNotification visibility
  // Only set visible if we have notifications AND current notification hasn't expired
  if (notificationCount > 0) {
    // Check if currentNotification overlay has expired (for NOTIFICATION_LIVE)
    if (currentNotification.type == NOTIFICATION_LIVE && currentNotification.duration > 0) {
      unsigned long elapsed = currentTime - currentNotification.showTime;
      if (elapsed >= currentNotification.duration) {
        // Overlay has expired - hide it and trigger refresh
        if (currentNotification.visible) {  // Only trigger refresh if it was visible
          currentNotification.visible = false;
          needsRefresh = true;
          Serial.println("Phone notification overlay expired - hiding");
        }
      } else {
        currentNotification.visible = true;
      }
    } else {
      currentNotification.visible = true;
    }
  } else {
    currentNotification.visible = false;
  }

  // Mark pending refresh if any notification was dismissed
  // Let updateNotificationDisplay() handle the actual refresh with smart timing
  if (needsRefresh) {
    notificationRenderState.pendingRefresh = true;
  }
}

/**
 * Add a phone notification to the queue
 * Phone notifications are persistent (NOTIFICATION_LIVE type, duration = 0)
 * @param id Phone notification ID for bidirectional sync
 * @param appName Name of the app that generated the notification
 * @param title Notification title
 * @param text Notification body text
 * @param iconData Pointer to 195-byte icon bitmap (39x39 monochrome, MSB-first), or nullptr for default icon
 * @param hasIcon True if iconData contains valid icon data
 */
void addPhoneNotification(uint32_t id, const char* appName, const char* title, const char* text,
                         const uint8_t* iconData = nullptr, bool hasIcon = false) {
  extern const unsigned char ICON_INFO[];

  // Create heading from app name
  char heading[32];
  strncpy(heading, appName, sizeof(heading) - 1);
  heading[sizeof(heading) - 1] = '\0';

  // Use title as line1 and text as line2
  char line1[32];
  strncpy(line1, title, sizeof(line1) - 1);
  line1[sizeof(line1) - 1] = '\0';

  char line2[32];
  strncpy(line2, text, sizeof(line2) - 1);
  line2[sizeof(line2) - 1] = '\0';

  // Check if notification with this ID already exists (update it)
  for (int i = 0; i < notificationCount; i++) {
    if (notificationQueue[i].phoneNotificationId == id) {
      // Update existing notification
      strncpy(notificationQueue[i].heading, heading, sizeof(notificationQueue[i].heading) - 1);
      strncpy(notificationQueue[i].line1, line1, sizeof(notificationQueue[i].line1) - 1);
      strncpy(notificationQueue[i].line2, line2, sizeof(notificationQueue[i].line2) - 1);
      notificationQueue[i].showTime = millis();

      // Update icon if provided
      if (hasIcon && iconData != nullptr) {
        memcpy(notificationQueue[i].iconData, iconData, 195);
        notificationQueue[i].hasDynamicIcon = true;
        notificationQueue[i].icon = notificationQueue[i].iconData; // Point to embedded data
      } else {
        notificationQueue[i].hasDynamicIcon = false;
        notificationQueue[i].icon = ICON_INFO; // Use default icon
      }

      notificationRenderState.pendingRefresh = true;
      return;
    }
  }

  // Add new notification using existing addNotification function
  // Duration = 5000ms for overlay (same as BT notifications)
  // Type = NOTIFICATION_LIVE so it persists in panel until manually dismissed
  addNotification(heading, line1, line2, ICON_INFO, NOTIFICATION_LIVE, 5000);

  // Set phone notification ID and icon data on the most recently added notification
  if (notificationCount > 0) {
    notificationQueue[notificationCount - 1].phoneNotificationId = id;

    // Copy icon data if provided
    if (hasIcon && iconData != nullptr) {
      memcpy(notificationQueue[notificationCount - 1].iconData, iconData, 195);
      notificationQueue[notificationCount - 1].hasDynamicIcon = true;
      notificationQueue[notificationCount - 1].icon = notificationQueue[notificationCount - 1].iconData; // Point to embedded data
    } else {
      notificationQueue[notificationCount - 1].hasDynamicIcon = false;
      notificationQueue[notificationCount - 1].icon = ICON_INFO; // Keep default icon
    }

    // Update currentNotification to match the queue entry (for floating overlay)
    // This is needed because addNotification() creates a copy before we set the icon
    currentNotification = notificationQueue[notificationCount - 1];
    // Fix icon pointer to point to currentNotification's own iconData if dynamic
    if (currentNotification.hasDynamicIcon) {
      currentNotification.icon = currentNotification.iconData;
    }
  }

  // Mark pending for smart rendering
  notificationRenderState.pendingRefresh = true;

  Serial.printf("Phone notification added: ID=%u, %s - %s\n", id, appName, title);
}

/**
 * Dismiss a phone notification by its ID
 * Called when the notification is dismissed on the phone
 * @param id Phone notification ID
 */
void dismissPhoneNotificationById(uint32_t id) {
  for (int i = 0; i < notificationCount; i++) {
    if (notificationQueue[i].phoneNotificationId == id) {
      Serial.printf("Dismissing phone notification: ID=%u\n", id);

      // Shift all notifications after this one forward
      for (int j = i; j < notificationCount - 1; j++) {
        notificationQueue[j] = notificationQueue[j + 1];
      }

      notificationCount--;

      // Update legacy currentNotification if needed
      if (notificationCount > 0) {
        currentNotification = notificationQueue[notificationCount - 1];
      } else {
        currentNotification.visible = false;
      }

      // Mark pending refresh
      notificationRenderState.pendingRefresh = true;
      return;
    }
  }

  Serial.printf("Phone notification ID=%u not found in queue\n", id);
}

#endif // NOTIFICATION_SYSTEM_H
