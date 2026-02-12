#ifndef PAGE_GAMES_H
#define PAGE_GAMES_H

#include <GxEPD2_BW.h>
#include <U8g2_for_Adafruit_GFX.h>
#include "notification_system.h"
#include "status_bar.h"
#include "controls_helper.h"

extern GxEPD2_BW<GxEPD2_290_BS, GxEPD2_290_BS::HEIGHT> display;
extern U8G2_FOR_ADAFRUIT_GFX u8g2_display;
extern const int DISPLAY_WIDTH;
extern const int DISPLAY_HEIGHT;
extern void handleOptions();
extern TinyGPSPlus gps;
extern LocalTime getLocalTime();

// Button pins (from BikeNav.ino)
#define SETTINGS_PIN 15
#define NEXT_PAGE_PIN 14

// --- MINESWEEPER CONSTANTS ---
const int GRID_COLS = 8;
const int GRID_ROWS = 16;  // Increased from 13 to 16 rows
const int CELL_WIDTH = 16;  // 128 / 8 = 16
const int CELL_HEIGHT = 16; // 1:1 aspect ratio (square cells)
const int GRID_HEIGHT = GRID_ROWS * CELL_HEIGHT; // 256 pixels
const int MINES_STATUS_BAR_HEIGHT = 20;
const int GRID_START_Y = MINES_STATUS_BAR_HEIGHT;

// --- GAME STATES ---
enum GameState {
  STATE_DIFFICULTY_SELECT,
  STATE_PLAYING,
  STATE_GAME_OVER
};

// --- DIFFICULTY LEVELS ---
enum Difficulty {
  DIFF_EASY = 0,
  DIFF_MEDIUM = 1,
  DIFF_HARD = 2
};

const int MINES_COUNT[] = {15, 25, 35}; // Easy, Medium, Hard
const char* DIFFICULTY_NAMES[] = {"Easy", "Medium", "Hard"};

// --- CELL STATES ---
const uint8_t CELL_HIDDEN = 0;
const uint8_t CELL_REVEALED = 1;
const uint8_t CELL_FLAGGED = 2;

// --- GAME STATE VARIABLES ---
GameState gameState = STATE_DIFFICULTY_SELECT;
Difficulty currentDifficulty = DIFF_EASY;
int selectedDifficulty = 0; // For difficulty selection screen

// Grid data
uint8_t grid[GRID_COLS][GRID_ROWS];        // Cell states: CELL_HIDDEN, CELL_REVEALED, CELL_FLAGGED
bool isMine[GRID_COLS][GRID_ROWS];         // True if cell contains mine
uint8_t adjacentMines[GRID_COLS][GRID_ROWS]; // Number of adjacent mines (0-8)

// Cursor position
int cursorX = 0;
int cursorY = 0;

// Game state flags
bool gameWon = false;
bool gameLost = false;
int revealedCells = 0;
int flaggedCells = 0;
int selectedDialogOption = 0; // 0 = Same Difficulty, 1 = Change Difficulty

// Time tracking
unsigned long gameStartTime = 0;
unsigned long gameEndTime = 0;
bool gameTimerStarted = false;

// Smart scrolling for cursor movement
unsigned long lastCursorMoveTime = 0;
const unsigned long CURSOR_DEBOUNCE_MS = 80;
bool gamesNeedsRedraw = false;

// Button hold tracking for continuous movement
unsigned long lastVerticalMoveTime = 0;
const unsigned long VERTICAL_MOVE_INITIAL_DELAY = 300;  // Initial delay before repeat
const unsigned long VERTICAL_MOVE_REPEAT_DELAY = 150;   // Delay between repeats
bool verticalMoveStarted = false;

// --- SAVED GAME STATE ---
struct SavedGameState {
  uint8_t grid[GRID_COLS][GRID_ROWS];
  bool isMine[GRID_COLS][GRID_ROWS];
  uint8_t adjacentMines[GRID_COLS][GRID_ROWS];
  int cursorX;
  int cursorY;
  int revealedCells;
  Difficulty difficulty;
  bool gameWon;
  bool gameLost;
  unsigned long elapsedTimeMs;  // Store elapsed time instead of start time
  bool gameTimerStarted;
};

SavedGameState savedGame;
bool hasSavedGame = false;

// --- UTILITY FUNCTIONS ---

// Initialize grid to all hidden cells
void initGrid() {
  for (int x = 0; x < GRID_COLS; x++) {
    for (int y = 0; y < GRID_ROWS; y++) {
      grid[x][y] = CELL_HIDDEN;
      isMine[x][y] = false;
      adjacentMines[x][y] = 0;
    }
  }
  revealedCells = 0;
  flaggedCells = 0;
  gameTimerStarted = false;
  gameStartTime = 0;
  gameEndTime = 0;
}

// Count flagged cells
int countFlags() {
  int count = 0;
  for (int x = 0; x < GRID_COLS; x++) {
    for (int y = 0; y < GRID_ROWS; y++) {
      if (grid[x][y] == CELL_FLAGGED) {
        count++;
      }
    }
  }
  return count;
}

// Generate mines randomly, avoiding the first clicked cell and its neighbors
void generateMines(int avoidX, int avoidY) {
  int mineCount = MINES_COUNT[currentDifficulty];
  int placed = 0;

  while (placed < mineCount) {
    int x = random(GRID_COLS);
    int y = random(GRID_ROWS);

    // Don't place mine on first click or where mine already exists
    if (isMine[x][y]) {
      continue;
    }

    // Don't place mine on first clicked cell or any of its 8 neighbors
    // This guarantees the first click will reveal an area (cascade)
    bool tooCloseToFirstClick = false;
    for (int dx = -1; dx <= 1; dx++) {
      for (int dy = -1; dy <= 1; dy++) {
        if (x == avoidX + dx && y == avoidY + dy) {
          tooCloseToFirstClick = true;
          break;
        }
      }
      if (tooCloseToFirstClick) break;
    }

    if (tooCloseToFirstClick) {
      continue;
    }

    isMine[x][y] = true;
    placed++;
  }

  // Calculate adjacent mine counts
  for (int x = 0; x < GRID_COLS; x++) {
    for (int y = 0; y < GRID_ROWS; y++) {
      if (isMine[x][y]) continue;

      int count = 0;
      // Check all 8 adjacent cells
      for (int dx = -1; dx <= 1; dx++) {
        for (int dy = -1; dy <= 1; dy++) {
          if (dx == 0 && dy == 0) continue;
          int nx = x + dx;
          int ny = y + dy;
          if (nx >= 0 && nx < GRID_COLS && ny >= 0 && ny < GRID_ROWS) {
            if (isMine[nx][ny]) count++;
          }
        }
      }
      adjacentMines[x][y] = count;
    }
  }
}

// Recursive flood fill for revealing empty cells
void revealCell(int x, int y) {
  // Bounds check
  if (x < 0 || x >= GRID_COLS || y < 0 || y >= GRID_ROWS) return;

  // Already revealed or flagged
  if (grid[x][y] != CELL_HIDDEN) return;

  // Reveal this cell
  grid[x][y] = CELL_REVEALED;
  revealedCells++;

  // If this cell has adjacent mines, stop here
  if (adjacentMines[x][y] > 0) return;

  // Otherwise, recursively reveal adjacent cells
  for (int dx = -1; dx <= 1; dx++) {
    for (int dy = -1; dy <= 1; dy++) {
      if (dx == 0 && dy == 0) continue;
      revealCell(x + dx, y + dy);
    }
  }
}

// Check if player won (all non-mine cells revealed)
bool checkWin() {
  int totalCells = GRID_COLS * GRID_ROWS;
  int mineCount = MINES_COUNT[currentDifficulty];
  return revealedCells == (totalCells - mineCount);
}

// Reveal all mines (for game over)
void revealAllMines() {
  for (int x = 0; x < GRID_COLS; x++) {
    for (int y = 0; y < GRID_ROWS; y++) {
      if (isMine[x][y]) {
        grid[x][y] = CELL_REVEALED;
      }
    }
  }
}

// Save current game state
void saveGame() {
  // Copy all grid data
  memcpy(savedGame.grid, grid, sizeof(grid));
  memcpy(savedGame.isMine, isMine, sizeof(isMine));
  memcpy(savedGame.adjacentMines, adjacentMines, sizeof(adjacentMines));

  // Copy game variables
  savedGame.cursorX = cursorX;
  savedGame.cursorY = cursorY;
  savedGame.revealedCells = revealedCells;
  savedGame.difficulty = currentDifficulty;
  savedGame.gameWon = gameWon;
  savedGame.gameLost = gameLost;
  savedGame.gameTimerStarted = gameTimerStarted;

  // Save elapsed time (not absolute start time)
  if (gameTimerStarted) {
    savedGame.elapsedTimeMs = millis() - gameStartTime;
  } else {
    savedGame.elapsedTimeMs = 0;
  }

  hasSavedGame = true;
}

// Restore saved game state
void restoreGame() {
  if (!hasSavedGame) return;

  // Restore all grid data
  memcpy(grid, savedGame.grid, sizeof(grid));
  memcpy(isMine, savedGame.isMine, sizeof(isMine));
  memcpy(adjacentMines, savedGame.adjacentMines, sizeof(adjacentMines));

  // Restore game variables
  cursorX = savedGame.cursorX;
  cursorY = savedGame.cursorY;
  revealedCells = savedGame.revealedCells;
  currentDifficulty = savedGame.difficulty;
  gameWon = savedGame.gameWon;
  gameLost = savedGame.gameLost;

  // Restore timer - calculate new start time to preserve elapsed time
  gameTimerStarted = savedGame.gameTimerStarted;
  if (gameTimerStarted) {
    // Set start time so that (millis() - gameStartTime) equals the saved elapsed time
    gameStartTime = millis() - savedGame.elapsedTimeMs;
  }

  // Clear save after restoring (single-use)
  hasSavedGame = false;
}

// --- RENDERING FUNCTIONS ---

// Draw a 3D raised button effect (Windows 3.1 style) with checkerboard pattern
void drawRaisedCell(int x, int y, int w, int h) {
  // Fill background with white first
  display.fillRect(x, y, w, h, GxEPD_WHITE);

  // Draw checkerboard pattern (every other pixel) - leave room for borders
  for (int py = y + 2; py < y + h - 2; py++) {
    for (int px = x + 2; px < x + w - 2; px++) {
      // Checkerboard: alternate pixels based on position
      if ((px + py) % 2 == 0) {
        display.drawPixel(px, py, GxEPD_BLACK);
      }
    }
  }

  // Windows 3.1 style 3D effect:
  // Light highlight on top and left (white - already there from fillRect)
  display.drawLine(x, y, x + w - 2, y, GxEPD_WHITE); // Top highlight
  display.drawLine(x, y, x, y + h - 2, GxEPD_WHITE); // Left highlight

  // Dark shadow on bottom and right
  display.drawLine(x + 1, y + h - 1, x + w - 1, y + h - 1, GxEPD_BLACK); // Bottom shadow
  display.drawLine(x + w - 1, y + 1, x + w - 1, y + h - 1, GxEPD_BLACK); // Right shadow

  // Inner dark line above shadow for depth
  display.drawLine(x + 2, y + h - 2, x + w - 2, y + h - 2, GxEPD_BLACK); // Bottom inner
  display.drawLine(x + w - 2, y + 2, x + w - 2, y + h - 2, GxEPD_BLACK); // Right inner
}

// Draw a flat revealed cell with thin borders (Windows 3.1 style)
void drawFlatCell(int x, int y, int w, int h) {
  // Fill with white
  display.fillRect(x, y, w, h, GxEPD_WHITE);

  // Draw very thin border - just right and bottom edges for subtle definition
  display.drawLine(x + w - 1, y, x + w - 1, y + h - 1, GxEPD_BLACK); // Right edge
  display.drawLine(x, y + h - 1, x + w - 1, y + h - 1, GxEPD_BLACK); // Bottom edge
}

// Draw top status bar with difficulty label and flag count
void drawTopStatusBar() {
  // Draw thick separator line at bottom of status bar
  display.drawLine(0, MINES_STATUS_BAR_HEIGHT - 2, DISPLAY_WIDTH, MINES_STATUS_BAR_HEIGHT - 2, GxEPD_BLACK);
  display.drawLine(0, MINES_STATUS_BAR_HEIGHT - 1, DISPLAY_WIDTH, MINES_STATUS_BAR_HEIGHT - 1, GxEPD_BLACK);

  // Setup text rendering
  u8g2_display.setFontMode(1);
  u8g2_display.setForegroundColor(GxEPD_BLACK);
  u8g2_display.setBackgroundColor(GxEPD_WHITE);
  u8g2_display.setFont(u8g2_font_helvB08_tf);

  // Draw difficulty label on left
  const char* diffLabel = DIFFICULTY_NAMES[currentDifficulty];
  u8g2_display.setCursor(2, 12);
  u8g2_display.print(diffLabel);

  // Draw flag count on right with flag icon
  int totalMines = MINES_COUNT[currentDifficulty];
  flaggedCells = countFlags();
  char flagStr[16];
  snprintf(flagStr, sizeof(flagStr), "%d/%d", flaggedCells, totalMines);
  int flagWidth = u8g2_display.getUTF8Width(flagStr);

  // Draw small flag icon (scaled down from 16x16 to 12x12 for status bar)
  int iconX = DISPLAY_WIDTH - flagWidth - 16;
  int iconY = 2;
  // Draw a simplified flag at reduced size
  for (int y = 0; y < 12; y++) {
    for (int x = 0; x < 12; x++) {
      // Scale down the 16x16 ICON_FLAG by sampling
      int srcX = (x * 16) / 12;
      int srcY = (y * 16) / 12;
      int byteIndex = srcY * 2 + (srcX / 8);
      int bitIndex = 7 - (srcX % 8);
      if (pgm_read_byte(&ICON_FLAG[byteIndex]) & (1 << bitIndex)) {
        display.drawPixel(iconX + x, iconY + y, GxEPD_BLACK);
      }
    }
  }

  u8g2_display.setCursor(DISPLAY_WIDTH - flagWidth - 2, 12);
  u8g2_display.print(flagStr);
}

// Bottom status bar now uses centralized status_bar.h
// (removed old drawBottomStatusBar function)

// Draw difficulty selection screen
void renderDifficultySelect() {
  display.setPartialWindow(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
  display.firstPage();

  do {
    display.fillScreen(GxEPD_WHITE);

    u8g2_display.setFontMode(1);
    u8g2_display.setForegroundColor(GxEPD_BLACK);
    u8g2_display.setBackgroundColor(GxEPD_WHITE);

    // Title
    u8g2_display.setFont(u8g2_font_helvB12_tf);
    const char* title = "MINESWEEPER";
    int titleWidth = u8g2_display.getUTF8Width(title);
    u8g2_display.setCursor((DISPLAY_WIDTH - titleWidth) / 2, 30);
    u8g2_display.print(title);

    // Button styling (inspired by trip details)
    const int BUTTON_WIDTH = 110;
    const int BUTTON_HEIGHT = 22;
    const int BUTTON_SPACING = 10;
    const int SHADOW_OFFSET = 2;
    int START_Y = hasSavedGame ? 55 : 80;
    int optionIndex = 0;

    // Show "Continue Game" if there's a saved game - visually distinct with arrow
    if (hasSavedGame) {
      int buttonX = (DISPLAY_WIDTH - BUTTON_WIDTH) / 2;
      int buttonY = START_Y + (optionIndex * (BUTTON_HEIGHT + BUTTON_SPACING));

      // Draw shadow
      display.fillRect(buttonX + SHADOW_OFFSET, buttonY + SHADOW_OFFSET,
                       BUTTON_WIDTH, BUTTON_HEIGHT, GxEPD_BLACK);

      if (selectedDifficulty == optionIndex) {
        // Selected - filled with inverted text
        display.fillRect(buttonX, buttonY, BUTTON_WIDTH, BUTTON_HEIGHT, GxEPD_BLACK);
        // Thicker triple border for emphasis
        display.drawRect(buttonX, buttonY, BUTTON_WIDTH, BUTTON_HEIGHT, GxEPD_BLACK);
        display.drawRect(buttonX + 1, buttonY + 1, BUTTON_WIDTH - 2, BUTTON_HEIGHT - 2, GxEPD_BLACK);
        display.drawRect(buttonX + 2, buttonY + 2, BUTTON_WIDTH - 4, BUTTON_HEIGHT - 4, GxEPD_BLACK);

        u8g2_display.setFont(u8g2_font_helvB08_tf);
        const char* continueText = "> Continue Game";  // Arrow prefix
        int textWidth = u8g2_display.getUTF8Width(continueText);
        u8g2_display.setForegroundColor(GxEPD_WHITE);
        u8g2_display.setBackgroundColor(GxEPD_BLACK);
        u8g2_display.setCursor(buttonX + (BUTTON_WIDTH - textWidth) / 2, buttonY + 14);
        u8g2_display.print(continueText);
      } else {
        // Not selected - white background with thicker border
        display.fillRect(buttonX, buttonY, BUTTON_WIDTH, BUTTON_HEIGHT, GxEPD_WHITE);
        // Thicker triple border for emphasis
        display.drawRect(buttonX, buttonY, BUTTON_WIDTH, BUTTON_HEIGHT, GxEPD_BLACK);
        display.drawRect(buttonX + 1, buttonY + 1, BUTTON_WIDTH - 2, BUTTON_HEIGHT - 2, GxEPD_BLACK);
        display.drawRect(buttonX + 2, buttonY + 2, BUTTON_WIDTH - 4, BUTTON_HEIGHT - 4, GxEPD_BLACK);

        u8g2_display.setFont(u8g2_font_helvB08_tf);
        const char* continueText = "> Continue Game";  // Arrow prefix
        int textWidth = u8g2_display.getUTF8Width(continueText);
        u8g2_display.setForegroundColor(GxEPD_BLACK);
        u8g2_display.setBackgroundColor(GxEPD_WHITE);
        u8g2_display.setCursor(buttonX + (BUTTON_WIDTH - textWidth) / 2, buttonY + 14);
        u8g2_display.print(continueText);
      }

      optionIndex++;
    }

    // Show difficulty option buttons
    for (int i = 0; i < 3; i++) {
      int buttonX = (DISPLAY_WIDTH - BUTTON_WIDTH) / 2;
      int buttonY = START_Y + (optionIndex * (BUTTON_HEIGHT + BUTTON_SPACING));

      // Build option text
      char optionText[32];
      snprintf(optionText, sizeof(optionText), "%s (%d mines)",
               DIFFICULTY_NAMES[i], MINES_COUNT[i]);

      // Draw shadow
      display.fillRect(buttonX + SHADOW_OFFSET, buttonY + SHADOW_OFFSET,
                       BUTTON_WIDTH, BUTTON_HEIGHT, GxEPD_BLACK);

      if (optionIndex == selectedDifficulty) {
        // Selected - filled with inverted text
        display.fillRect(buttonX, buttonY, BUTTON_WIDTH, BUTTON_HEIGHT, GxEPD_BLACK);
        display.drawRect(buttonX, buttonY, BUTTON_WIDTH, BUTTON_HEIGHT, GxEPD_BLACK);
        display.drawRect(buttonX + 1, buttonY + 1, BUTTON_WIDTH - 2, BUTTON_HEIGHT - 2, GxEPD_BLACK);

        u8g2_display.setFont(u8g2_font_helvB08_tf);
        int textWidth = u8g2_display.getUTF8Width(optionText);
        u8g2_display.setForegroundColor(GxEPD_WHITE);
        u8g2_display.setBackgroundColor(GxEPD_BLACK);
        u8g2_display.setCursor(buttonX + (BUTTON_WIDTH - textWidth) / 2, buttonY + 14);
        u8g2_display.print(optionText);
      } else {
        // Not selected - white background with border
        display.fillRect(buttonX, buttonY, BUTTON_WIDTH, BUTTON_HEIGHT, GxEPD_WHITE);
        display.drawRect(buttonX, buttonY, BUTTON_WIDTH, BUTTON_HEIGHT, GxEPD_BLACK);
        display.drawRect(buttonX + 1, buttonY + 1, BUTTON_WIDTH - 2, BUTTON_HEIGHT - 2, GxEPD_BLACK);

        u8g2_display.setFont(u8g2_font_helvB08_tf);
        int textWidth = u8g2_display.getUTF8Width(optionText);
        u8g2_display.setForegroundColor(GxEPD_BLACK);
        u8g2_display.setBackgroundColor(GxEPD_WHITE);
        u8g2_display.setCursor(buttonX + (BUTTON_WIDTH - textWidth) / 2, buttonY + 14);
        u8g2_display.print(optionText);
      }

      optionIndex++;
    }

    // Draw controls helper at bottom
    drawControlsFourButton(190,
      CONTROL_ENCODER, "Move horizontally",
      CONTROL_SETTINGS, "Up",
      CONTROL_OPTIONS, "Mark flag",
      CONTROL_NEXT_PAGE, "Down",
      "Reveal");

    // Draw bottom status bar (centralized)
    drawStatusBar();

    drawNotificationOverlay();
  } while (display.nextPage());
}

// Draw game board
void renderGameBoard() {
  display.setPartialWindow(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
  display.firstPage();

  do {
    display.fillScreen(GxEPD_WHITE);

    // Draw top status bar with difficulty
    drawTopStatusBar();

    // Draw bottom status bar (centralized)
    drawStatusBar();

    // Setup font for numbers
    u8g2_display.setFontMode(1);
    u8g2_display.setForegroundColor(GxEPD_BLACK);
    u8g2_display.setBackgroundColor(GxEPD_WHITE);
    u8g2_display.setFont(u8g2_font_helvB08_tf);

    // Draw each cell
    for (int y = 0; y < GRID_ROWS; y++) {
      for (int x = 0; x < GRID_COLS; x++) {
        int cellX = x * CELL_WIDTH;
        int cellY = GRID_START_Y + (y * CELL_HEIGHT);

        if (grid[x][y] == CELL_REVEALED) {
          // Revealed cell - flat
          drawFlatCell(cellX, cellY, CELL_WIDTH, CELL_HEIGHT);

          if (isMine[x][y]) {
            // Draw mine bitmap (centered in 16x16 cell)
            display.drawBitmap(cellX, cellY, ICON_MINE, 16, 16, GxEPD_BLACK);
          } else if (adjacentMines[x][y] > 0) {
            // Draw number
            char numStr[2];
            numStr[0] = '0' + adjacentMines[x][y];
            numStr[1] = '\0';
            u8g2_display.setCursor(cellX + 5, cellY + 11);
            u8g2_display.print(numStr);
          }
        } else if (grid[x][y] == CELL_FLAGGED) {
          // Flagged cell - raised with flag bitmap
          drawRaisedCell(cellX, cellY, CELL_WIDTH, CELL_HEIGHT);
          display.drawBitmap(cellX, cellY, ICON_FLAG, 16, 16, GxEPD_BLACK);
        } else {
          // Hidden cell - raised
          drawRaisedCell(cellX, cellY, CELL_WIDTH, CELL_HEIGHT);
        }

        // Draw cursor
        if (x == cursorX && y == cursorY && !gameLost && !gameWon) {
          // Thick border for cursor
          display.drawRect(cellX + 1, cellY + 1, CELL_WIDTH - 2, CELL_HEIGHT - 2, GxEPD_BLACK);
          display.drawRect(cellX + 2, cellY + 2, CELL_WIDTH - 4, CELL_HEIGHT - 4, GxEPD_BLACK);
        }
      }
    }

    drawNotificationOverlay();
  } while (display.nextPage());
}

// Draw game over dialog
void renderGameOverDialog() {
  display.setPartialWindow(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
  display.firstPage();

  do {
    display.fillScreen(GxEPD_WHITE);

    // Draw status bars
    drawTopStatusBar();
    drawStatusBar();

    // First draw the game board in background (with all mines revealed)
    u8g2_display.setFontMode(1);
    u8g2_display.setForegroundColor(GxEPD_BLACK);
    u8g2_display.setBackgroundColor(GxEPD_WHITE);
    u8g2_display.setFont(u8g2_font_helvB08_tf);

    for (int y = 0; y < GRID_ROWS; y++) {
      for (int x = 0; x < GRID_COLS; x++) {
        int cellX = x * CELL_WIDTH;
        int cellY = GRID_START_Y + (y * CELL_HEIGHT);

        if (grid[x][y] == CELL_REVEALED) {
          drawFlatCell(cellX, cellY, CELL_WIDTH, CELL_HEIGHT);

          if (isMine[x][y]) {
            // Draw mine bitmap
            display.drawBitmap(cellX, cellY, ICON_MINE, 16, 16, GxEPD_BLACK);
          } else if (adjacentMines[x][y] > 0) {
            char numStr[2];
            numStr[0] = '0' + adjacentMines[x][y];
            numStr[1] = '\0';
            u8g2_display.setCursor(cellX + 5, cellY + 11);
            u8g2_display.print(numStr);
          }
        } else if (grid[x][y] == CELL_FLAGGED) {
          drawRaisedCell(cellX, cellY, CELL_WIDTH, CELL_HEIGHT);
          // Draw flag bitmap
          display.drawBitmap(cellX, cellY, ICON_FLAG, 16, 16, GxEPD_BLACK);
        } else {
          drawRaisedCell(cellX, cellY, CELL_WIDTH, CELL_HEIGHT);
        }
      }
    }

    // Draw dialog on top
    int dialogWidth = 110;
    int dialogHeight = 120;
    int dialogX = (DISPLAY_WIDTH - dialogWidth) / 2;
    int dialogY = (DISPLAY_HEIGHT - dialogHeight) / 2 - 20;

    // Shadow (Windows 3.1 style)
    display.fillRect(dialogX + 2, dialogY + 2, dialogWidth, dialogHeight, GxEPD_BLACK);

    // Background
    display.fillRect(dialogX, dialogY, dialogWidth, dialogHeight, GxEPD_WHITE);

    // Double border
    display.drawRect(dialogX, dialogY, dialogWidth, dialogHeight, GxEPD_BLACK);
    display.drawRect(dialogX + 1, dialogY + 1, dialogWidth - 2, dialogHeight - 2, GxEPD_BLACK);

    // Title
    u8g2_display.setFontMode(1);
    u8g2_display.setForegroundColor(GxEPD_BLACK);
    u8g2_display.setBackgroundColor(GxEPD_WHITE);
    u8g2_display.setFont(u8g2_font_helvB12_tf);

    const char* title = gameWon ? "You Win!" : "Game Over!";
    int titleWidth = u8g2_display.getUTF8Width(title);
    u8g2_display.setCursor(dialogX + (dialogWidth - titleWidth) / 2, dialogY + 25);
    u8g2_display.print(title);

    // Calculate and display time
    u8g2_display.setFont(u8g2_font_helvB08_tf);
    unsigned long elapsedTime = (gameEndTime - gameStartTime) / 1000; // Convert to seconds
    int minutes = elapsedTime / 60;
    int seconds = elapsedTime % 60;
    char timeStr[16];
    snprintf(timeStr, sizeof(timeStr), "Time: %02d:%02d", minutes, seconds);
    int timeWidth = u8g2_display.getUTF8Width(timeStr);
    u8g2_display.setCursor(dialogX + (dialogWidth - timeWidth) / 2, dialogY + 40);
    u8g2_display.print(timeStr);

    // Buttons
    u8g2_display.setFont(u8g2_font_helvB08_tf);
    const int BUTTON_HEIGHT = 20;
    const int BUTTON_SPACING = 8;
    const int BUTTONS_START_Y = dialogY + 55;

    const char* button1Text = "Same Difficulty";
    const char* button2Text = "Change Difficulty";

    // Button 1
    int button1Y = BUTTONS_START_Y;
    int button1TextWidth = u8g2_display.getUTF8Width(button1Text);
    int button1Width = button1TextWidth + 10;
    int button1X = dialogX + (dialogWidth - button1Width) / 2;

    if (selectedDialogOption == 0) {
      // Selected - filled
      display.fillRect(button1X, button1Y, button1Width, BUTTON_HEIGHT, GxEPD_BLACK);
      u8g2_display.setForegroundColor(GxEPD_WHITE);
      u8g2_display.setBackgroundColor(GxEPD_BLACK);
    } else {
      // Not selected - outline
      display.drawRect(button1X, button1Y, button1Width, BUTTON_HEIGHT, GxEPD_BLACK);
      u8g2_display.setForegroundColor(GxEPD_BLACK);
      u8g2_display.setBackgroundColor(GxEPD_WHITE);
    }
    u8g2_display.setCursor(button1X + 5, button1Y + 14);
    u8g2_display.print(button1Text);

    // Button 2
    int button2Y = BUTTONS_START_Y + BUTTON_HEIGHT + BUTTON_SPACING;
    int button2TextWidth = u8g2_display.getUTF8Width(button2Text);
    int button2Width = button2TextWidth + 10;
    int button2X = dialogX + (dialogWidth - button2Width) / 2;

    u8g2_display.setForegroundColor(GxEPD_BLACK);
    u8g2_display.setBackgroundColor(GxEPD_WHITE);

    if (selectedDialogOption == 1) {
      // Selected - filled
      display.fillRect(button2X, button2Y, button2Width, BUTTON_HEIGHT, GxEPD_BLACK);
      u8g2_display.setForegroundColor(GxEPD_WHITE);
      u8g2_display.setBackgroundColor(GxEPD_BLACK);
    } else {
      // Not selected - outline
      display.drawRect(button2X, button2Y, button2Width, BUTTON_HEIGHT, GxEPD_BLACK);
      u8g2_display.setForegroundColor(GxEPD_BLACK);
      u8g2_display.setBackgroundColor(GxEPD_WHITE);
    }
    u8g2_display.setCursor(button2X + 5, button2Y + 14);
    u8g2_display.print(button2Text);

    drawNotificationOverlay();
  } while (display.nextPage());
}

// --- PAGE INTERFACE FUNCTIONS ---

void initGamesPage() {
  gameState = STATE_DIFFICULTY_SELECT;
  selectedDifficulty = 0;
  currentDifficulty = DIFF_EASY;
  gamesNeedsRedraw = false;
  lastVerticalMoveTime = 0;
  verticalMoveStarted = false;
}

void renderGamesPage() {
  switch (gameState) {
    case STATE_DIFFICULTY_SELECT:
      renderDifficultySelect();
      break;
    case STATE_PLAYING:
      renderGameBoard();
      break;
    case STATE_GAME_OVER:
      renderGameOverDialog();
      break;
  }
}

void updateGamesPage() {
  // Handle continuous vertical movement when buttons are held
  if (gameState == STATE_PLAYING && !gameWon && !gameLost) {
    bool settingsHeld = digitalRead(SETTINGS_PIN) == LOW;
    bool nextPageHeld = digitalRead(NEXT_PAGE_PIN) == LOW;

    if (settingsHeld || nextPageHeld) {
      unsigned long currentTime = millis();
      unsigned long requiredDelay = verticalMoveStarted ? VERTICAL_MOVE_REPEAT_DELAY : VERTICAL_MOVE_INITIAL_DELAY;

      if (currentTime - lastVerticalMoveTime >= requiredDelay) {
        // Move cursor (2 rows when held, 1 row on first press)
        int moveAmount = verticalMoveStarted ? 2 : 1;

        if (settingsHeld) {
          cursorY -= moveAmount;
          // Handle wrapping for negative values
          while (cursorY < 0) cursorY += GRID_ROWS;
        } else {
          cursorY += moveAmount;
          // Handle wrapping for values >= GRID_ROWS
          cursorY = cursorY % GRID_ROWS;
        }

        lastVerticalMoveTime = currentTime;
        lastCursorMoveTime = currentTime;
        verticalMoveStarted = true;
        gamesNeedsRedraw = true;
      }
    } else {
      // Reset when buttons released
      verticalMoveStarted = false;
    }
  }

  // Handle debounced redraw for cursor movement
  if (gamesNeedsRedraw && (millis() - lastCursorMoveTime >= CURSOR_DEBOUNCE_MS)) {
    gamesNeedsRedraw = false;
    renderGamesPage();
    return;
  }

  // Let the centralized status bar handle time/battery/GPS/BLE updates
  updateStatusBar();
}

void handleGamesEncoder(int delta) {
  markUserActivity();  // Prevent status bar auto-refresh during interaction

  if (gameState == STATE_DIFFICULTY_SELECT) {
    // Change menu selection (4 options if saved game, 3 otherwise)
    int maxOption = hasSavedGame ? 3 : 2;  // Continue + 3 difficulties OR just 3 difficulties
    selectedDifficulty += delta;
    if (selectedDifficulty < 0) selectedDifficulty = maxOption;
    if (selectedDifficulty > maxOption) selectedDifficulty = 0;
    renderDifficultySelect();

  } else if (gameState == STATE_PLAYING) {
    // Move cursor horizontally within current row only
    if (!gameWon && !gameLost) {
      // Move cursor left/right
      cursorX += delta;

      // Wrap around within the current row
      if (cursorX < 0) {
        cursorX = GRID_COLS - 1;  // Wrap to right edge
      } else if (cursorX >= GRID_COLS) {
        cursorX = 0;  // Wrap to left edge
      }

      // Mark for debounced redraw
      gamesNeedsRedraw = true;
      lastCursorMoveTime = millis();
    }

  } else if (gameState == STATE_GAME_OVER) {
    // Change dialog option selection
    selectedDialogOption = 1 - selectedDialogOption; // Toggle between 0 and 1
    renderGameOverDialog();
  }
}

void handleGamesButton() {
  markUserActivity();  // Prevent status bar auto-refresh during interaction

  if (gameState == STATE_DIFFICULTY_SELECT) {
    // Check if "Continue Game" was selected
    if (hasSavedGame && selectedDifficulty == 0) {
      // Restore saved game
      restoreGame();
      gameState = STATE_PLAYING;
      renderGameBoard();
    } else {
      // Start new game with selected difficulty
      // Adjust difficulty index if there's a saved game (option indices shifted by 1)
      int difficultyIndex = hasSavedGame ? selectedDifficulty - 1 : selectedDifficulty;
      currentDifficulty = (Difficulty)difficultyIndex;
      gameState = STATE_PLAYING;

      // Initialize new game
      initGrid();
      cursorX = GRID_COLS / 2;
      cursorY = GRID_ROWS / 2;
      gameWon = false;
      gameLost = false;

      // Clear any existing save (starting fresh)
      hasSavedGame = false;

      // Don't generate mines yet - wait for first click
      renderGameBoard();
    }

  } else if (gameState == STATE_PLAYING) {
    // Reveal cell at cursor
    if (grid[cursorX][cursorY] == CELL_FLAGGED || gameWon || gameLost) {
      return; // Can't reveal flagged cells or if game is over
    }

    // First click - generate mines and start timer
    static bool firstClick = true;
    if (revealedCells == 0) {
      generateMines(cursorX, cursorY);
      firstClick = false;

      // Start the timer
      if (!gameTimerStarted) {
        gameStartTime = millis();
        gameTimerStarted = true;
      }
    }

    // Check if mine
    if (isMine[cursorX][cursorY]) {
      // Game over - hit mine
      gameLost = true;
      gameEndTime = millis(); // Stop the timer
      revealAllMines();
      gameState = STATE_GAME_OVER;
      selectedDialogOption = 0;
      hasSavedGame = false;  // Clear save on game over
      renderGameOverDialog();
    } else {
      // Reveal cell (and adjacent if empty)
      revealCell(cursorX, cursorY);

      // Check win condition
      if (checkWin()) {
        gameWon = true;
        gameEndTime = millis(); // Stop the timer
        gameState = STATE_GAME_OVER;
        selectedDialogOption = 0;
        hasSavedGame = false;  // Clear save on game completion
        renderGameOverDialog();
      } else {
        renderGameBoard();
      }
    }

  } else if (gameState == STATE_GAME_OVER) {
    // Handle dialog button selection
    if (selectedDialogOption == 0) {
      // Same difficulty - restart game
      gameState = STATE_PLAYING;
      initGrid();
      cursorX = GRID_COLS / 2;
      cursorY = GRID_ROWS / 2;
      gameWon = false;
      gameLost = false;
      renderGameBoard();
    } else {
      // Change difficulty - go back to selection
      gameState = STATE_DIFFICULTY_SELECT;
      selectedDifficulty = currentDifficulty;
      renderDifficultySelect();
    }
  }
}

// Handle options button (place/remove flag)
void handleGamesOptions() {
  if (gameState == STATE_PLAYING && !gameWon && !gameLost) {
    // Toggle flag on current cursor position
    if (grid[cursorX][cursorY] == CELL_HIDDEN) {
      grid[cursorX][cursorY] = CELL_FLAGGED;
      renderGameBoard();
    } else if (grid[cursorX][cursorY] == CELL_FLAGGED) {
      grid[cursorX][cursorY] = CELL_HIDDEN;
      renderGameBoard();
    }
    // Can't flag revealed cells
  }
}

// Handle settings button (move cursor up one row)
void handleGamesSettings() {
  if (gameState == STATE_PLAYING && !gameWon && !gameLost) {
    cursorY--;
    if (cursorY < 0) cursorY = GRID_ROWS - 1; // Wrap to bottom
    gamesNeedsRedraw = true;
    lastCursorMoveTime = millis();
    lastVerticalMoveTime = millis();  // Set time to prevent double-trigger from hold detection
    verticalMoveStarted = false;      // Reset so initial delay applies on next hold
  }
}

// Handle next page button (move cursor down one row)
void handleGamesNextPage() {
  if (gameState == STATE_PLAYING && !gameWon && !gameLost) {
    cursorY++;
    if (cursorY >= GRID_ROWS) cursorY = 0; // Wrap to top
    gamesNeedsRedraw = true;
    lastCursorMoveTime = millis();
    lastVerticalMoveTime = millis();  // Set time to prevent double-trigger from hold detection
    verticalMoveStarted = false;      // Reset so initial delay applies on next hold
  }
}

// Handle back button - save game if in progress
bool handleGamesBack() {
  if (gameState == STATE_PLAYING && !gameWon && !gameLost) {
    // Save the current game before exiting
    saveGame();
    return false;  // Allow navigation to main menu
  }
  return false;  // Allow navigation to main menu
}

#endif // PAGE_GAMES_H
