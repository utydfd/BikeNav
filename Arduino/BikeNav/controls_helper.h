#ifndef CONTROLS_HELPER_H
#define CONTROLS_HELPER_H

#include <GxEPD2_BW.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <math.h>
#include "bitmaps.h"

// External references from main program
extern GxEPD2_BW<GxEPD2_290_BS, GxEPD2_290_BS::HEIGHT> display;
extern U8G2_FOR_ADAFRUIT_GFX u8g2_display;
extern const int DISPLAY_WIDTH;
extern const int DISPLAY_HEIGHT;

// CONTROLS bitmap dimensions (from bitmaps.h)
const int CONTROLS_WIDTH = 107;
const int CONTROLS_HEIGHT = 60;

// Button label positions - these correspond to the 5 button areas in the bitmap
// The bitmap shows 4 rounded rectangles in corners plus center encoder
enum ControlButton {
  CONTROL_BACK = 0,           // Top-left button (back)
  CONTROL_SETTINGS = 1,       // Top-right button (settings)
  CONTROL_ENCODER = 2,        // Center button (encoder/OK)
  CONTROL_OPTIONS = 3,        // Bottom-left button (options)
  CONTROL_NEXT_PAGE = 4       // Bottom-right button (next page)
};

// Button configuration structure
struct ButtonLabel {
  ControlButton button;
  const char* label;
};

/**
 * Draw the controls bitmap with button labels
 * Places labels above or below their buttons (encoder rotation gets a pointer line + curved arrow)
 *
 * @param y Y position to draw the controls at (typically near bottom of screen)
 * @param labels Array of ButtonLabel structs defining which buttons have labels
 * @param labelCount Number of labels in the array
 * @param encoderPressLabel Label drawn inside the encoder circle (press action)
 */
void drawControlsWithLabels(int y, const ButtonLabel* labels, int labelCount,
                             const char* encoderPressLabel = nullptr) {
  // Calculate X position to center the bitmap
  int bitmapX = (DISPLAY_WIDTH - CONTROLS_WIDTH) / 2;

  // Setup text rendering first to measure text
  u8g2_display.setFontMode(1);
  u8g2_display.setForegroundColor(GxEPD_BLACK);
  u8g2_display.setBackgroundColor(GxEPD_WHITE);
  u8g2_display.setFont(u8g2_font_helvB08_tf);

  const int TEXT_HEIGHT = 10;  // Approximate text height for font
  const int TEXT_GAP = 2;      // Gap between bitmap edge and text
  const int ROW_GAP = 2;       // Gap between stacked text rows

  const int LEFT_CENTER_X = bitmapX + 20;
  const int RIGHT_CENTER_X = bitmapX + (CONTROLS_WIDTH - 20);
  const int CENTER_X = bitmapX + (CONTROLS_WIDTH / 2);
  const int CENTER_Y = y + (CONTROLS_HEIGHT / 2);
  const int ENCODER_RADIUS = 18;
  const int LINE_GAP = 8;
  const int TOP_LINE_GAP = 16;
  const int PRESS_TEXT_UP = 5;
  const int ARROW_RADIUS_OFFSET = 4;
  const int ARC_GAP = 2;
  const int TOP_ARC_GAP = 6;
  const int TOP_LINE_END_TRIM = 8;
  const int BOTTOM_LINE_END_PUSH = 4;
  const int ARROWHEAD_LEN = 6;
  const float ARROWHEAD_SPREAD = 0.55f;  // ~32 degrees
  const float ARC_SPAN = 0.90f;          // ~52 degrees

  const int TOP_BASELINE = y - TEXT_GAP;
  const int BOTTOM_BASELINE = y + CONTROLS_HEIGHT + TEXT_GAP + TEXT_HEIGHT;
  const int EXTRA_BOTTOM_BASELINE = BOTTOM_BASELINE + TEXT_HEIGHT + ROW_GAP;
  const int EXTRA_TOP_BASELINE = TOP_BASELINE - TEXT_HEIGHT - ROW_GAP;

  bool hasTopLabels = false;
  bool hasBottomLabels = false;
  for (int i = 0; i < labelCount; i++) {
    if (labels[i].button == CONTROL_BACK || labels[i].button == CONTROL_SETTINGS) {
      hasTopLabels = true;
    }
    if (labels[i].button == CONTROL_OPTIONS || labels[i].button == CONTROL_NEXT_PAGE) {
      hasBottomLabels = true;
    }
  }

  int encoderBaseline = BOTTOM_BASELINE;
  if (hasBottomLabels) {
    if (EXTRA_BOTTOM_BASELINE <= DISPLAY_HEIGHT) {
      encoderBaseline = EXTRA_BOTTOM_BASELINE;
    } else if (EXTRA_TOP_BASELINE - TEXT_HEIGHT >= 0) {
      encoderBaseline = EXTRA_TOP_BASELINE;
    }
  } else if (hasTopLabels) {
    encoderBaseline = BOTTOM_BASELINE;
  }

  auto drawThickLine = [&](int x1, int y1, int x2, int y2) {
    display.drawLine(x1, y1, x2, y2, GxEPD_BLACK);
    int dx = x2 - x1;
    int dy = y2 - y1;
    int offsetX = -dy;
    int offsetY = dx;
    if (abs(offsetX) >= abs(offsetY)) {
      offsetX = (offsetX >= 0) ? 1 : -1;
      offsetY = 0;
    } else {
      offsetX = 0;
      offsetY = (offsetY >= 0) ? 1 : -1;
    }

    int midX = (x1 + x2) / 2;
    int midY = (y1 + y2) / 2;
    int radialX = midX - CENTER_X;
    int radialY = midY - CENTER_Y;
    if ((offsetX * radialX + offsetY * radialY) < 0) {
      offsetX = -offsetX;
      offsetY = -offsetY;
    }

    display.drawLine(x1 + offsetX, y1 + offsetY, x2 + offsetX, y2 + offsetY, GxEPD_BLACK);
  };

  auto drawArc = [&](int centerX, int centerY, int radius, float startAngle, float endAngle) {
    const int steps = 10;
    float step = (endAngle - startAngle) / steps;
    float prevX = centerX + cosf(startAngle) * radius;
    float prevY = centerY + sinf(startAngle) * radius;

    for (int i = 1; i <= steps; i++) {
      float angle = startAngle + (step * i);
      float nextX = centerX + cosf(angle) * radius;
      float nextY = centerY + sinf(angle) * radius;
      drawThickLine((int)lroundf(prevX), (int)lroundf(prevY), (int)lroundf(nextX), (int)lroundf(nextY));
      prevX = nextX;
      prevY = nextY;
    }
  };

  auto drawArrowhead = [&](int tipX, int tipY, float directionAngle, int length, float spread) {
    float leftAngle = directionAngle + spread;
    float rightAngle = directionAngle - spread;
    int leftX = (int)lroundf(tipX - (cosf(leftAngle) * length));
    int leftY = (int)lroundf(tipY - (sinf(leftAngle) * length));
    int rightX = (int)lroundf(tipX - (cosf(rightAngle) * length));
    int rightY = (int)lroundf(tipY - (sinf(rightAngle) * length));
    drawThickLine(tipX, tipY, leftX, leftY);
    drawThickLine(tipX, tipY, rightX, rightY);
  };

  // Draw the CONTROLS bitmap first
  display.drawBitmap(bitmapX, y, CONTROLS, CONTROLS_WIDTH, CONTROLS_HEIGHT, GxEPD_BLACK);

  // Draw encoder press label inside the circle
  if (encoderPressLabel && encoderPressLabel[0] != '\0') {
    int pressWidth = u8g2_display.getUTF8Width(encoderPressLabel);
    int pressX = CENTER_X - (pressWidth / 2);
    int pressBaseline = CENTER_Y + (TEXT_HEIGHT / 2) - 1 - PRESS_TEXT_UP;

    if (pressX < 0) {
      pressX = 0;
    }
    int maxPressX = DISPLAY_WIDTH - pressWidth;
    if (pressX > maxPressX) {
      pressX = maxPressX;
    }
    if (pressBaseline < TEXT_HEIGHT) {
      pressBaseline = TEXT_HEIGHT;
    }
    if (pressBaseline > DISPLAY_HEIGHT) {
      pressBaseline = DISPLAY_HEIGHT;
    }

    u8g2_display.setCursor(pressX, pressBaseline);
    u8g2_display.print(encoderPressLabel);
  }

  // Process each label and place it next to its button
  for (int i = 0; i < labelCount; i++) {
    const ButtonLabel& label = labels[i];
    if (label.label == nullptr || label.label[0] == '\0') {
      continue;
    }
    int textWidth = u8g2_display.getUTF8Width(label.label);
    int centerX = CENTER_X;
    int baseline = BOTTOM_BASELINE;

    switch (label.button) {
      case CONTROL_BACK:
        centerX = LEFT_CENTER_X;
        baseline = TOP_BASELINE;
        if (baseline - TEXT_HEIGHT < 0) {
          baseline = BOTTOM_BASELINE;
        }
        break;
      case CONTROL_SETTINGS:
        centerX = RIGHT_CENTER_X;
        baseline = TOP_BASELINE;
        if (baseline - TEXT_HEIGHT < 0) {
          baseline = BOTTOM_BASELINE;
        }
        break;
      case CONTROL_OPTIONS:
        centerX = LEFT_CENTER_X;
        baseline = BOTTOM_BASELINE;
        if (baseline > DISPLAY_HEIGHT) {
          baseline = TOP_BASELINE;
        }
        break;
      case CONTROL_NEXT_PAGE:
        centerX = RIGHT_CENTER_X;
        baseline = BOTTOM_BASELINE;
        if (baseline > DISPLAY_HEIGHT) {
          baseline = TOP_BASELINE;
        }
        break;
      case CONTROL_ENCODER:
        centerX = CENTER_X;
        baseline = encoderBaseline;
        if (baseline > DISPLAY_HEIGHT) {
          baseline = TOP_BASELINE;
        }
        break;
    }

    int textX = centerX - (textWidth / 2);
    if (textX < 0) {
      textX = 0;
    }
    int maxX = DISPLAY_WIDTH - textWidth;
    if (textX > maxX) {
      textX = maxX;
    }
    if (baseline - TEXT_HEIGHT < 0) {
      baseline = TEXT_HEIGHT;
    }

    if (label.button == CONTROL_ENCODER) {
      int textCenterX = textX + (textWidth / 2);
      int textCenterY = baseline - (TEXT_HEIGHT / 2);
      float dx = (float)(textCenterX - CENTER_X);
      float dy = (float)(textCenterY - CENTER_Y);
      float distance = sqrtf(dx * dx + dy * dy);
      float arrowRadius = ENCODER_RADIUS + ARROW_RADIUS_OFFSET;

      if (distance > 0.5f) {
        int lineGap = (dy < 0.0f) ? TOP_LINE_GAP : LINE_GAP;
        int arcGap = (dy < 0.0f) ? TOP_ARC_GAP : ARC_GAP;
        float lineEndRadius = arrowRadius - arcGap;
        if (dy < 0.0f) {
          lineEndRadius -= TOP_LINE_END_TRIM;
        } else if (dy > 0.0f) {
          lineEndRadius += BOTTOM_LINE_END_PUSH;
        }

        float maxAllowedEndRadius = distance - (float)lineGap - 0.5f;
        if (lineEndRadius > maxAllowedEndRadius) {
          lineEndRadius = maxAllowedEndRadius;
        }
        if (lineEndRadius < 0.0f) {
          lineEndRadius = 0.0f;
        }
        float lineStartX = textCenterX - dx * (lineGap / distance);
        float lineStartY = textCenterY - dy * (lineGap / distance);
        float lineEndX = CENTER_X + dx * (lineEndRadius / distance);
        float lineEndY = CENTER_Y + dy * (lineEndRadius / distance);

        drawThickLine(
          (int)lroundf(lineStartX),
          (int)lroundf(lineStartY),
          (int)lroundf(lineEndX),
          (int)lroundf(lineEndY)
        );

        float angle = atan2f(dy, dx);
        float arcStart = angle - (ARC_SPAN / 2.0f);
        float arcEnd = angle + (ARC_SPAN / 2.0f);

        drawArc(CENTER_X, CENTER_Y, (int)arrowRadius, arcStart, arcEnd);

        int startX = (int)lroundf(CENTER_X + cosf(arcStart) * arrowRadius);
        int startY = (int)lroundf(CENTER_Y + sinf(arcStart) * arrowRadius);
        int endX = (int)lroundf(CENTER_X + cosf(arcEnd) * arrowRadius);
        int endY = (int)lroundf(CENTER_Y + sinf(arcEnd) * arrowRadius);
        float tangentStart = arcStart + 1.5708f;
        float tangentEnd = arcEnd + 1.5708f;

        drawArrowhead(startX, startY, tangentStart + 3.14159f, ARROWHEAD_LEN, ARROWHEAD_SPREAD);
        drawArrowhead(endX, endY, tangentEnd, ARROWHEAD_LEN, ARROWHEAD_SPREAD);
      }
    }

    u8g2_display.setCursor(textX, baseline);
    u8g2_display.print(label.label);
  }
}

/**
 * Common 2-button layout: Back and Encoder press
 * Usage: Navigation confirmation screens
 */
void drawControlsBackEncoder(int y, const char* backLabel, const char* encoderPressLabel) {
  ButtonLabel labels[] = {
    {CONTROL_BACK, backLabel}
  };

  drawControlsWithLabels(y, labels, 1, encoderPressLabel);
}

/**
 * Common 3-button layout
 */
void drawControlsThreeButton(int y, ControlButton btn1, const char* label1,
                              ControlButton btn2, const char* label2,
                              ControlButton btn3, const char* label3,
                              const char* encoderPressLabel = nullptr) {
  ButtonLabel labels[] = {
    {btn1, label1},
    {btn2, label2},
    {btn3, label3}
  };

  drawControlsWithLabels(y, labels, 3, encoderPressLabel);
}

/**
 * Version for 4 buttons
 */
void drawControlsFourButton(int y, ControlButton btn1, const char* label1,
                             ControlButton btn2, const char* label2,
                             ControlButton btn3, const char* label3,
                             ControlButton btn4, const char* label4,
                             const char* encoderPressLabel = nullptr) {
  ButtonLabel labels[] = {
    {btn1, label1},
    {btn2, label2},
    {btn3, label3},
    {btn4, label4}
  };

  drawControlsWithLabels(y, labels, 4, encoderPressLabel);
}

/**
 * Version for all 5 buttons
 */
void drawControlsAllButtons(int y, const char* backLabel, const char* settingsLabel,
                             const char* encoderLabel, const char* optionsLabel,
                             const char* nextPageLabel, const char* encoderPressLabel = nullptr) {
  ButtonLabel labels[] = {
    {CONTROL_BACK, backLabel},
    {CONTROL_SETTINGS, settingsLabel},
    {CONTROL_ENCODER, encoderLabel},
    {CONTROL_OPTIONS, optionsLabel},
    {CONTROL_NEXT_PAGE, nextPageLabel}
  };

  drawControlsWithLabels(y, labels, 5, encoderPressLabel);
}

#endif // CONTROLS_HELPER_H
