#ifndef PAGE_SNAKE_H
#define PAGE_SNAKE_H

#include <GxEPD2_BW.h>
#include <U8g2_for_Adafruit_GFX.h>
#include "controls_helper.h"
#include "notification_system.h"
#include "status_bar.h"

extern GxEPD2_BW<GxEPD2_290_BS, GxEPD2_290_BS::HEIGHT> display;
extern U8G2_FOR_ADAFRUIT_GFX u8g2_display;
extern const int DISPLAY_WIDTH;
extern const int DISPLAY_HEIGHT;

// Encoder button pin for speed boost
#define SNAKE_SW_PIN 6

// --- SNAKE GAME CONSTANTS ---
const int SNAKE_CELL_SIZE = 8;  // Each cell is 8x8 pixels
const int SNAKE_GRID_COLS = 16; // 128 / 8 = 16 columns
const int SNAKE_GRID_ROWS = 32; // 256 / 8 = 32 rows (leaving room for score bar)
const int SNAKE_SCORE_BAR_HEIGHT = 20;
const int SNAKE_GRID_START_Y = SNAKE_SCORE_BAR_HEIGHT;
const int SNAKE_MAX_LENGTH = 256; // Maximum snake length

// Snake body thickness (pixels from edge)
const int SNAKE_BODY_MARGIN = 1;  // 1 pixel margin = 6 pixel thick body in 8px cell

// --- DIRECTION ENUM ---
enum SnakeDirection {
  DIR_UP = 0,
  DIR_RIGHT = 1,
  DIR_DOWN = 2,
  DIR_LEFT = 3
};

// --- GAME STATE ENUM ---
enum SnakeGameState {
  SNAKE_STATE_MENU,
  SNAKE_STATE_PLAYING,
  SNAKE_STATE_GAME_OVER
};

// --- SEGMENT TYPES ---
// Each segment can have "digesting food" traveling through it
struct SnakeSegment {
  int x;
  int y;
  bool hasFood;  // True if this segment is "digesting" food (shows as bump)
};

// --- GAME STATE VARIABLES ---
SnakeGameState snakeGameState = SNAKE_STATE_MENU;
SnakeSegment snake[SNAKE_MAX_LENGTH];
int snakeLength = 3;
SnakeDirection snakeDirection = DIR_RIGHT;
SnakeDirection pendingDirection = DIR_RIGHT;

// Food position
int foodX = 0;
int foodY = 0;

// Score
int snakeScore = 0;
int snakeHighScore = 0;

// Game timing
unsigned long lastMoveTime = 0;
const unsigned long SNAKE_MOVE_INTERVAL = 200; // ms between moves
const int SNAKE_BOOST_TILES = 3; // Move this many tiles at once when boosting
bool snakeNeedsRedraw = false;
bool snakeBoostActive = false;  // True when encoder button is held

// Menu selection
int snakeMenuSelection = 0; // 0 = New Game, 1 = (future options)

// Game over dialog selection
int snakeGameOverSelection = 0; // 0 = Play Again, 1 = Menu

// --- UTILITY FUNCTIONS ---

// Check if a cell is part of the snake at given index
bool isSnakeAt(int x, int y, int excludeIndex = -1) {
  for (int i = 0; i < snakeLength; i++) {
    if (i == excludeIndex) continue;
    if (snake[i].x == x && snake[i].y == y) return true;
  }
  return false;
}

// Get snake segment index at position (-1 if not found)
int getSnakeIndexAt(int x, int y) {
  for (int i = 0; i < snakeLength; i++) {
    if (snake[i].x == x && snake[i].y == y) return i;
  }
  return -1;
}

// Get direction from segment to next segment
int getOutgoingDir(int i) {
  if (i >= snakeLength - 1) return -1;
  int dx = snake[i + 1].x - snake[i].x;
  int dy = snake[i + 1].y - snake[i].y;
  if (abs(dx) > 1 || abs(dy) > 1) return -1;
  if (dy < 0) return 0;
  if (dx > 0) return 1;
  if (dy > 0) return 2;
  if (dx < 0) return 3;
  return -1;
}

// Get direction from previous segment to this segment
int getIncomingDir(int i) {
  if (i <= 0) return -1;
  int dx = snake[i].x - snake[i - 1].x;
  int dy = snake[i].y - snake[i - 1].y;
  if (abs(dx) > 1 || abs(dy) > 1) return -1;
  if (dy < 0) return 0;
  if (dx > 0) return 1;
  if (dy > 0) return 2;
  if (dx < 0) return 3;
  return -1;
}

// Draw the entire snake as a continuous solid body
void drawSnakeContinuous() {
  const int BODY_WIDTH = 6;  // Width of snake body in pixels
  const int HALF = BODY_WIDTH / 2;

  if (snakeLength < 1) return;

  // === STEP 1: Draw entire snake body as solid black ===

  // Draw each segment and connection
  for (int i = 0; i < snakeLength; i++) {
    int cx = snake[i].x * SNAKE_CELL_SIZE + SNAKE_CELL_SIZE / 2;
    int cy = SNAKE_GRID_START_Y + snake[i].y * SNAKE_CELL_SIZE + SNAKE_CELL_SIZE / 2;

    // Food bulge makes segment wider
    bool hasFood = snake[i].hasFood && i > 0 && i < snakeLength - 1;
    int segHalf = hasFood ? HALF + 1 : HALF;

    // Draw segment as filled black rectangle
    display.fillRect(cx - segHalf, cy - segHalf, segHalf * 2, segHalf * 2, GxEPD_BLACK);

    // Draw connection to next segment
    if (i < snakeLength - 1) {
      int nx = snake[i + 1].x;
      int ny = snake[i + 1].y;
      int dx = nx - snake[i].x;
      int dy = ny - snake[i].y;

      // Skip wrap-around connections
      if (abs(dx) > 1 || abs(dy) > 1) continue;

      int ncx = nx * SNAKE_CELL_SIZE + SNAKE_CELL_SIZE / 2;
      int ncy = SNAKE_GRID_START_Y + ny * SNAKE_CELL_SIZE + SNAKE_CELL_SIZE / 2;

      if (dx != 0) {  // Horizontal connection
        int minX = min(cx, ncx) - HALF;
        int maxX = max(cx, ncx) + HALF;
        display.fillRect(minX, cy - HALF, maxX - minX, BODY_WIDTH, GxEPD_BLACK);
      } else if (dy != 0) {  // Vertical connection
        int minY = min(cy, ncy) - HALF;
        int maxY = max(cy, ncy) + HALF;
        display.fillRect(cx - HALF, minY, BODY_WIDTH, maxY - minY, GxEPD_BLACK);
      }
    }
  }

  // === STEP 2: Round outside corners at turns (aggressive rounding) ===

  for (int i = 1; i < snakeLength - 1; i++) {
    int inDir = getIncomingDir(i);
    int outDir = getOutgoingDir(i);

    // Check if this is a corner
    if (inDir == -1 || outDir == -1 || inDir == outDir) continue;

    int cx = snake[i].x * SNAKE_CELL_SIZE + SNAKE_CELL_SIZE / 2;
    int cy = SNAKE_GRID_START_Y + snake[i].y * SNAKE_CELL_SIZE + SNAKE_CELL_SIZE / 2;

    // Determine which corner to round (the outside corner)
    int cornerX = cx, cornerY = cy;
    int dirX = 0, dirY = 0;

    if ((inDir == 0 && outDir == 1) || (inDir == 3 && outDir == 2)) {
      cornerX = cx - HALF; cornerY = cy - HALF; dirX = -1; dirY = -1;
    } else if ((inDir == 0 && outDir == 3) || (inDir == 1 && outDir == 2)) {
      cornerX = cx + HALF - 1; cornerY = cy - HALF; dirX = 1; dirY = -1;
    } else if ((inDir == 2 && outDir == 1) || (inDir == 3 && outDir == 0)) {
      cornerX = cx - HALF; cornerY = cy + HALF - 1; dirX = -1; dirY = 1;
    } else if ((inDir == 2 && outDir == 3) || (inDir == 1 && outDir == 0)) {
      cornerX = cx + HALF - 1; cornerY = cy + HALF - 1; dirX = 1; dirY = 1;
    }

    // Remove larger corner section for truly rounded look (6 pixels)
    // Corner pixel
    display.drawPixel(cornerX, cornerY, GxEPD_WHITE);
    // Adjacent pixels
    display.drawPixel(cornerX + dirX, cornerY, GxEPD_WHITE);
    display.drawPixel(cornerX, cornerY + dirY, GxEPD_WHITE);
    // Second row
    display.drawPixel(cornerX + dirX * 2, cornerY, GxEPD_WHITE);
    display.drawPixel(cornerX + dirX, cornerY + dirY, GxEPD_WHITE);
    display.drawPixel(cornerX, cornerY + dirY * 2, GxEPD_WHITE);
  }

  // === STEP 3: Draw BIG snake head - pixel perfect symmetric ===
  // Uses odd pixel counts for true symmetry around center

  {
    int hx = snake[0].x * SNAKE_CELL_SIZE + SNAKE_CELL_SIZE / 2;
    int hy = SNAKE_GRID_START_Y + snake[0].y * SNAKE_CELL_SIZE + SNAKE_CELL_SIZE / 2;

    switch (snakeDirection) {
      case DIR_RIGHT:
        // Round head (symmetric)
        display.fillCircle(hx, hy, 4, GxEPD_BLACK);
        // Connect to body (7 pixels tall: -3 to +3)
        display.fillRect(hx - 3, hy - 3, 4, 7, GxEPD_BLACK);
        // Snout (3 pixels tall, centered: -1, 0, +1)
        display.fillRect(hx + 3, hy - 1, 2, 3, GxEPD_BLACK);
        display.drawPixel(hx + 5, hy, GxEPD_BLACK);  // Tip centered
        // Eyes (symmetric: -2 and +2)
        display.drawPixel(hx + 1, hy - 2, GxEPD_WHITE);
        display.drawPixel(hx + 1, hy + 2, GxEPD_WHITE);
        break;

      case DIR_LEFT:
        // Round head (symmetric)
        display.fillCircle(hx, hy, 4, GxEPD_BLACK);
        // Connect to body (7 pixels tall: -3 to +3)
        display.fillRect(hx - 1, hy - 3, 4, 7, GxEPD_BLACK);
        // Snout (3 pixels tall, centered: -1, 0, +1)
        display.fillRect(hx - 5, hy - 1, 2, 3, GxEPD_BLACK);
        display.drawPixel(hx - 6, hy, GxEPD_BLACK);  // Tip centered
        // Eyes (symmetric: -2 and +2)
        display.drawPixel(hx - 2, hy - 2, GxEPD_WHITE);
        display.drawPixel(hx - 2, hy + 2, GxEPD_WHITE);
        break;

      case DIR_UP:
        // Round head (symmetric)
        display.fillCircle(hx, hy, 4, GxEPD_BLACK);
        // Connect to body (7 pixels wide: -3 to +3)
        display.fillRect(hx - 3, hy - 1, 7, 4, GxEPD_BLACK);
        // Snout (3 pixels wide, centered: -1, 0, +1)
        display.fillRect(hx - 1, hy - 5, 3, 2, GxEPD_BLACK);
        display.drawPixel(hx, hy - 6, GxEPD_BLACK);  // Tip centered
        // Eyes (symmetric: -2 and +2)
        display.drawPixel(hx - 2, hy - 2, GxEPD_WHITE);
        display.drawPixel(hx + 2, hy - 2, GxEPD_WHITE);
        break;

      case DIR_DOWN:
        // Round head (symmetric)
        display.fillCircle(hx, hy, 4, GxEPD_BLACK);
        // Connect to body (7 pixels wide: -3 to +3)
        display.fillRect(hx - 3, hy - 3, 7, 4, GxEPD_BLACK);
        // Snout (3 pixels wide, centered: -1, 0, +1)
        display.fillRect(hx - 1, hy + 3, 3, 2, GxEPD_BLACK);
        display.drawPixel(hx, hy + 5, GxEPD_BLACK);  // Tip centered
        // Eyes (symmetric: -2 and +2)
        display.drawPixel(hx - 2, hy + 1, GxEPD_WHITE);
        display.drawPixel(hx + 2, hy + 1, GxEPD_WHITE);
        break;
    }
  }

  // === STEP 4: Draw pointy tapered tail ===

  if (snakeLength > 1) {
    int tailIdx = snakeLength - 1;
    int tx = snake[tailIdx].x * SNAKE_CELL_SIZE + SNAKE_CELL_SIZE / 2;
    int ty = SNAKE_GRID_START_Y + snake[tailIdx].y * SNAKE_CELL_SIZE + SNAKE_CELL_SIZE / 2;

    // Get direction FROM body TO tail (tail points in this direction, away from body)
    int dx = snake[tailIdx].x - snake[tailIdx - 1].x;
    int dy = snake[tailIdx].y - snake[tailIdx - 1].y;

    // Handle wrap-around
    if (dx > 1) dx = -1;
    if (dx < -1) dx = 1;
    if (dy > 1) dy = -1;
    if (dy < -1) dy = 1;

    int tailDir;
    if (dy < 0) tailDir = 0;       // Tail points UP (away from body below)
    else if (dx > 0) tailDir = 1;  // Tail points RIGHT (away from body on left)
    else if (dy > 0) tailDir = 2;  // Tail points DOWN (away from body above)
    else tailDir = 3;              // Tail points LEFT (away from body on right)

    // Clear tail area first
    display.fillRect(tx - HALF, ty - HALF, BODY_WIDTH, BODY_WIDTH, GxEPD_WHITE);

    // Draw pointy tail that tapers to a point in the direction away from body
    // Using a triangular shape for a more snake-like appearance
    switch (tailDir) {
      case 0: // Tail points UP - taper from bottom (body) to top (tip)
        for (int row = 0; row < BODY_WIDTH; row++) {
          // Row 0 is bottom (body side, full width), row 5 is top (tip, narrowest)
          int width = BODY_WIDTH - row;
          if (width < 1) width = 1;
          int startX = tx - width / 2;
          display.fillRect(startX, ty + HALF - 1 - row, width, 1, GxEPD_BLACK);
        }
        break;

      case 2: // Tail points DOWN - taper from top (body) to bottom (tip)
        for (int row = 0; row < BODY_WIDTH; row++) {
          int width = BODY_WIDTH - row;
          if (width < 1) width = 1;
          int startX = tx - width / 2;
          display.fillRect(startX, ty - HALF + row, width, 1, GxEPD_BLACK);
        }
        break;

      case 3: // Tail points LEFT - taper from right (body) to left (tip)
        for (int col = 0; col < BODY_WIDTH; col++) {
          int height = BODY_WIDTH - col;
          if (height < 1) height = 1;
          int startY = ty - height / 2;
          display.fillRect(tx + HALF - 1 - col, startY, 1, height, GxEPD_BLACK);
        }
        break;

      case 1: // Tail points RIGHT - taper from left (body) to right (tip)
        for (int col = 0; col < BODY_WIDTH; col++) {
          int height = BODY_WIDTH - col;
          if (height < 1) height = 1;
          int startY = ty - height / 2;
          display.fillRect(tx - HALF + col, startY, 1, height, GxEPD_BLACK);
        }
        break;
    }
  }
}

// Draw food (apple)
void drawFood(int cellX, int cellY) {
  int pixelX = cellX * SNAKE_CELL_SIZE;
  int pixelY = SNAKE_GRID_START_Y + (cellY * SNAKE_CELL_SIZE);

  // Draw a small apple shape
  // Stem
  display.drawPixel(pixelX + 3, pixelY, GxEPD_BLACK);
  display.drawPixel(pixelX + 4, pixelY, GxEPD_BLACK);
  display.drawPixel(pixelX + 4, pixelY + 1, GxEPD_BLACK);

  // Apple body with checkerboard fill
  display.fillCircle(pixelX + 3, pixelY + 4, 3, GxEPD_BLACK);

  // Add white highlight
  display.drawPixel(pixelX + 2, pixelY + 3, GxEPD_WHITE);
}

// Spawn food at random empty location
void spawnFood() {
  bool valid = false;
  while (!valid) {
    foodX = random(SNAKE_GRID_COLS);
    foodY = random(SNAKE_GRID_ROWS);

    // Check if position is not on snake
    valid = true;
    for (int i = 0; i < snakeLength; i++) {
      if (snake[i].x == foodX && snake[i].y == foodY) {
        valid = false;
        break;
      }
    }
  }
}

// Initialize a new game
void initSnakeGame() {
  snakeLength = 3;
  snakeDirection = DIR_RIGHT;
  pendingDirection = DIR_RIGHT;
  snakeScore = 0;
  lastMoveTime = millis();

  // Initialize snake in center
  int startX = SNAKE_GRID_COLS / 2;
  int startY = SNAKE_GRID_ROWS / 2;

  for (int i = 0; i < snakeLength; i++) {
    snake[i].x = startX - i;
    snake[i].y = startY;
    snake[i].hasFood = false;
  }

  // Spawn first food
  spawnFood();

  snakeGameState = SNAKE_STATE_PLAYING;
}

// Move the snake
void moveSnake() {
  // Apply pending direction change
  snakeDirection = pendingDirection;

  // Calculate new head position
  int newX = snake[0].x;
  int newY = snake[0].y;

  switch (snakeDirection) {
    case DIR_UP:    newY--; break;
    case DIR_DOWN:  newY++; break;
    case DIR_LEFT:  newX--; break;
    case DIR_RIGHT: newX++; break;
  }

  // Wrap around edges
  if (newX < 0) newX = SNAKE_GRID_COLS - 1;
  if (newX >= SNAKE_GRID_COLS) newX = 0;
  if (newY < 0) newY = SNAKE_GRID_ROWS - 1;
  if (newY >= SNAKE_GRID_ROWS) newY = 0;

  // Check collision with self
  for (int i = 0; i < snakeLength; i++) {
    if (snake[i].x == newX && snake[i].y == newY) {
      // Game over!
      snakeGameState = SNAKE_STATE_GAME_OVER;
      if (snakeScore > snakeHighScore) {
        snakeHighScore = snakeScore;
      }
      snakeNeedsRedraw = true;  // Trigger redraw to show game over screen
      return;
    }
  }

  // Check if eating food
  bool ateFood = (newX == foodX && newY == foodY);

  // Move food through body (digestion effect)
  // Shift hasFood flags down the body
  for (int i = snakeLength - 1; i > 0; i--) {
    snake[i].hasFood = snake[i - 1].hasFood;
  }

  // If the tail had food, extend the snake by one segment
  if (snakeLength > 0 && snake[snakeLength - 1].hasFood) {
    // Clear the food flag FIRST (before extending)
    snake[snakeLength - 1].hasFood = false;
    // Extend snake by duplicating tail position
    if (snakeLength < SNAKE_MAX_LENGTH) {
      snake[snakeLength].x = snake[snakeLength - 1].x;
      snake[snakeLength].y = snake[snakeLength - 1].y;
      snake[snakeLength].hasFood = false;
      snakeLength++;
    }
  }

  // Move body (shift all segments back)
  for (int i = snakeLength - 1; i > 0; i--) {
    snake[i].x = snake[i - 1].x;
    snake[i].y = snake[i - 1].y;
    // hasFood already shifted above
  }

  // Move head
  snake[0].x = newX;
  snake[0].y = newY;
  snake[0].hasFood = ateFood;  // Head starts "digesting" if we ate

  // If we ate food, add score and spawn new food
  if (ateFood) {
    snakeScore++;
    spawnFood();
  }

  snakeNeedsRedraw = true;
}

// --- RENDERING FUNCTIONS ---

void drawSnakeScoreBar() {
  // Draw separator line
  display.drawLine(0, SNAKE_SCORE_BAR_HEIGHT - 2, DISPLAY_WIDTH, SNAKE_SCORE_BAR_HEIGHT - 2, GxEPD_BLACK);
  display.drawLine(0, SNAKE_SCORE_BAR_HEIGHT - 1, DISPLAY_WIDTH, SNAKE_SCORE_BAR_HEIGHT - 1, GxEPD_BLACK);

  u8g2_display.setFontMode(1);
  u8g2_display.setForegroundColor(GxEPD_BLACK);
  u8g2_display.setBackgroundColor(GxEPD_WHITE);
  u8g2_display.setFont(u8g2_font_helvB08_tf);

  // Draw "SNAKE" on left
  u8g2_display.setCursor(2, 12);
  u8g2_display.print("SNAKE");

  // Draw score on right
  char scoreStr[16];
  snprintf(scoreStr, sizeof(scoreStr), "Score: %d", snakeScore);
  int scoreWidth = u8g2_display.getUTF8Width(scoreStr);
  u8g2_display.setCursor(DISPLAY_WIDTH - scoreWidth - 2, 12);
  u8g2_display.print(scoreStr);
}

void renderSnakeMenu() {
  display.setPartialWindow(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
  display.firstPage();

  do {
    display.fillScreen(GxEPD_WHITE);

    u8g2_display.setFontMode(1);
    u8g2_display.setForegroundColor(GxEPD_BLACK);
    u8g2_display.setBackgroundColor(GxEPD_WHITE);

    // Title with retro styling
    u8g2_display.setFont(u8g2_font_helvB12_tf);
    const char* title = "SNAKE";
    int titleWidth = u8g2_display.getUTF8Width(title);
    u8g2_display.setCursor((DISPLAY_WIDTH - titleWidth) / 2, 40);
    u8g2_display.print(title);

    // Draw a decorative snake preview - solid body, no stripes
    int iconY = 58;
    int snakeStartX = 20;
    int snakeWidth = 80;
    int bodyW = 6;
    int bodyHalf = bodyW / 2;

    // Draw solid black body (no stripes)
    display.fillRect(snakeStartX, iconY - bodyHalf, snakeWidth, bodyW, GxEPD_BLACK);

    // Draw BIG round head (at right end) - pixel perfect symmetric
    int headX = snakeStartX + snakeWidth + 3;
    int headY = iconY;

    // Round head (symmetric)
    display.fillCircle(headX, headY, 4, GxEPD_BLACK);
    // Connect to body (7 pixels tall: -3 to +3)
    display.fillRect(headX - 4, headY - 3, 4, 7, GxEPD_BLACK);
    // Snout (3 pixels tall, centered: -1, 0, +1)
    display.fillRect(headX + 3, headY - 1, 2, 3, GxEPD_BLACK);
    display.drawPixel(headX + 5, headY, GxEPD_BLACK);  // Tip centered
    // Eyes (symmetric: -2 and +2)
    display.drawPixel(headX + 1, headY - 2, GxEPD_WHITE);
    display.drawPixel(headX + 1, headY + 2, GxEPD_WHITE);

    // Draw pointy tail (extends left from body)
    int tailX = snakeStartX - bodyW;  // Start tail before body
    int tailY = iconY;

    // Triangular tail tapering to the left (point on left, wide on right connecting to body)
    for (int col = 0; col < bodyW; col++) {
      int height = col + 1;  // Grows from 1 pixel at tip to bodyW at body connection
      int startY = tailY - height / 2;
      display.fillRect(tailX + col, startY, 1, height, GxEPD_BLACK);
    }

    // High score display
    u8g2_display.setFont(u8g2_font_helvB08_tf);
    char highStr[24];
    snprintf(highStr, sizeof(highStr), "High Score: %d", snakeHighScore);
    int highWidth = u8g2_display.getUTF8Width(highStr);
    u8g2_display.setCursor((DISPLAY_WIDTH - highWidth) / 2, 85);
    u8g2_display.print(highStr);

    // Play button
    const int BUTTON_WIDTH = 100;
    const int BUTTON_HEIGHT = 24;
    int buttonX = (DISPLAY_WIDTH - BUTTON_WIDTH) / 2;
    int buttonY = 110;

    // Shadow
    display.fillRect(buttonX + 2, buttonY + 2, BUTTON_WIDTH, BUTTON_HEIGHT, GxEPD_BLACK);

    // Button (always selected since it's the only option)
    display.fillRect(buttonX, buttonY, BUTTON_WIDTH, BUTTON_HEIGHT, GxEPD_BLACK);

    u8g2_display.setForegroundColor(GxEPD_WHITE);
    u8g2_display.setBackgroundColor(GxEPD_BLACK);
    const char* playText = "PLAY";
    int playWidth = u8g2_display.getUTF8Width(playText);
    u8g2_display.setCursor(buttonX + (BUTTON_WIDTH - playWidth) / 2, buttonY + 16);
    u8g2_display.print(playText);

    ButtonLabel labels[] = {
      {CONTROL_ENCODER, "Turn snake"}
    };
    drawControlsWithLabels(190, labels, 1, "Boost");

    drawStatusBar();
    drawNotificationOverlay();
  } while (display.nextPage());
}

void renderSnakeGame() {
  display.setPartialWindow(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
  display.firstPage();

  do {
    display.fillScreen(GxEPD_WHITE);

    // Draw score bar
    drawSnakeScoreBar();

    // Draw status bar at bottom
    drawStatusBar();

    // Draw food
    drawFood(foodX, foodY);

    // Draw snake as continuous body (Nokia 3310 style)
    drawSnakeContinuous();

    drawNotificationOverlay();
  } while (display.nextPage());
}

void renderSnakeGameOver() {
  display.setPartialWindow(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
  display.firstPage();

  do {
    display.fillScreen(GxEPD_WHITE);

    // Draw the game state in background (dimmed by dialog)
    drawSnakeScoreBar();
    drawStatusBar();

    // Draw snake (dead pose - continuous body)
    drawSnakeContinuous();

    // Game Over dialog
    int dialogWidth = 110;
    int dialogHeight = 100;
    int dialogX = (DISPLAY_WIDTH - dialogWidth) / 2;
    int dialogY = (DISPLAY_HEIGHT - dialogHeight) / 2 - 20;

    // Shadow
    display.fillRect(dialogX + 2, dialogY + 2, dialogWidth, dialogHeight, GxEPD_BLACK);

    // Background
    display.fillRect(dialogX, dialogY, dialogWidth, dialogHeight, GxEPD_WHITE);

    // Double border
    display.drawRect(dialogX, dialogY, dialogWidth, dialogHeight, GxEPD_BLACK);
    display.drawRect(dialogX + 1, dialogY + 1, dialogWidth - 2, dialogHeight - 2, GxEPD_BLACK);

    u8g2_display.setFontMode(1);
    u8g2_display.setForegroundColor(GxEPD_BLACK);
    u8g2_display.setBackgroundColor(GxEPD_WHITE);

    // Title
    u8g2_display.setFont(u8g2_font_helvB12_tf);
    const char* title = "GAME OVER";
    int titleWidth = u8g2_display.getUTF8Width(title);
    u8g2_display.setCursor(dialogX + (dialogWidth - titleWidth) / 2, dialogY + 22);
    u8g2_display.print(title);

    // Score
    u8g2_display.setFont(u8g2_font_helvB08_tf);
    char scoreStr[24];
    snprintf(scoreStr, sizeof(scoreStr), "Score: %d", snakeScore);
    int scoreWidth = u8g2_display.getUTF8Width(scoreStr);
    u8g2_display.setCursor(dialogX + (dialogWidth - scoreWidth) / 2, dialogY + 38);
    u8g2_display.print(scoreStr);

    // New high score?
    if (snakeScore >= snakeHighScore && snakeScore > 0) {
      const char* newHigh = "NEW HIGH!";
      int newHighWidth = u8g2_display.getUTF8Width(newHigh);
      u8g2_display.setCursor(dialogX + (dialogWidth - newHighWidth) / 2, dialogY + 52);
      u8g2_display.print(newHigh);
    }

    // Buttons
    const int BTN_WIDTH = 90;
    const int BTN_HEIGHT = 18;
    int btn1Y = dialogY + 60;
    int btn2Y = dialogY + 82;
    int btnX = dialogX + (dialogWidth - BTN_WIDTH) / 2;

    // Play Again button
    if (snakeGameOverSelection == 0) {
      display.fillRect(btnX, btn1Y, BTN_WIDTH, BTN_HEIGHT, GxEPD_BLACK);
      u8g2_display.setForegroundColor(GxEPD_WHITE);
      u8g2_display.setBackgroundColor(GxEPD_BLACK);
    } else {
      display.drawRect(btnX, btn1Y, BTN_WIDTH, BTN_HEIGHT, GxEPD_BLACK);
      u8g2_display.setForegroundColor(GxEPD_BLACK);
      u8g2_display.setBackgroundColor(GxEPD_WHITE);
    }
    const char* again = "Play Again";
    int againWidth = u8g2_display.getUTF8Width(again);
    u8g2_display.setCursor(btnX + (BTN_WIDTH - againWidth) / 2, btn1Y + 13);
    u8g2_display.print(again);

    // Menu button (not shown for now - just play again)

    drawNotificationOverlay();
  } while (display.nextPage());
}

// --- PAGE INTERFACE FUNCTIONS ---

void initSnakePage() {
  snakeGameState = SNAKE_STATE_MENU;
  snakeMenuSelection = 0;
  snakeGameOverSelection = 0;
  snakeNeedsRedraw = false;
}

void renderSnakePage() {
  switch (snakeGameState) {
    case SNAKE_STATE_MENU:
      renderSnakeMenu();
      break;
    case SNAKE_STATE_PLAYING:
      renderSnakeGame();
      break;
    case SNAKE_STATE_GAME_OVER:
      renderSnakeGameOver();
      break;
  }
}

void updateSnakePage() {
  if (snakeGameState == SNAKE_STATE_PLAYING) {
    // Check if encoder button is held for speed boost
    snakeBoostActive = (digitalRead(SNAKE_SW_PIN) == LOW);

    unsigned long currentTime = millis();
    if (currentTime - lastMoveTime >= SNAKE_MOVE_INTERVAL) {
      lastMoveTime = currentTime;

      // Move multiple tiles when boosting
      int tilesToMove = snakeBoostActive ? SNAKE_BOOST_TILES : 1;
      for (int i = 0; i < tilesToMove && snakeGameState == SNAKE_STATE_PLAYING; i++) {
        moveSnake();
      }
    }
  }

  // Check for redraw (works for both playing and game over states)
  if (snakeNeedsRedraw) {
    snakeNeedsRedraw = false;
    renderSnakePage();
  }

  // Let status bar handle updates
  updateStatusBar();
}

void handleSnakeEncoder(int delta) {
  markUserActivity();

  if (snakeGameState == SNAKE_STATE_MENU) {
    // Only one option, nothing to do
    renderSnakeMenu();
  } else if (snakeGameState == SNAKE_STATE_PLAYING) {
    // Rotate snake direction
    // Left rotation (counter-clockwise): delta < 0
    // Right rotation (clockwise): delta > 0

    int newDir = (int)pendingDirection;

    if (delta > 0) {
      // Turn right (clockwise)
      newDir = (newDir + 1) % 4;
    } else if (delta < 0) {
      // Turn left (counter-clockwise)
      newDir = (newDir + 3) % 4;  // +3 is same as -1 mod 4
    }

    pendingDirection = (SnakeDirection)newDir;

  } else if (snakeGameState == SNAKE_STATE_GAME_OVER) {
    // Toggle selection (only one button now)
    snakeGameOverSelection = 0;
    renderSnakeGameOver();
  }
}

void handleSnakeButton() {
  markUserActivity();

  if (snakeGameState == SNAKE_STATE_MENU) {
    // Start game
    initSnakeGame();
    renderSnakePage();
  } else if (snakeGameState == SNAKE_STATE_PLAYING) {
    // Button press during game - could be pause or boost
    // For now, do nothing (pure encoder control)
  } else if (snakeGameState == SNAKE_STATE_GAME_OVER) {
    // Play again
    initSnakeGame();
    renderSnakePage();
  }
}

void handleSnakeOptions() {
  // Options button - currently unused
}

void handleSnakeSettings() {
  // Settings button - currently unused
}

void handleSnakeNextPage() {
  // Next page button - currently unused
}

bool handleSnakeBack() {
  if (snakeGameState == SNAKE_STATE_PLAYING) {
    // Exit to menu from game
    snakeGameState = SNAKE_STATE_MENU;
    renderSnakePage();
    return true;  // Handled, don't navigate away
  } else if (snakeGameState == SNAKE_STATE_GAME_OVER) {
    // Exit to menu from game over
    snakeGameState = SNAKE_STATE_MENU;
    renderSnakePage();
    return true;  // Handled, don't navigate away
  }
  return false;  // Allow navigation to main menu
}

#endif // PAGE_SNAKE_H
