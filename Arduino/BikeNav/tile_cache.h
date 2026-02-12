#ifndef TILE_CACHE_H
#define TILE_CACHE_H

#include <Arduino.h>

// --- TILE CACHE CONFIGURATION ---
#define TILE_CACHE_SIZE 768      // Number of tiles to cache (768 tiles = 6MB)
#define TILE_DATA_SIZE 8192      // Size of each tile in bytes (256x256 / 8)

// --- CACHE ENTRY STRUCTURE ---
struct TileCacheEntry {
  int zoom;
  int tileX;
  int tileY;
  unsigned long lastUsed;     // millis() timestamp for LRU tracking
  bool valid;                 // Is this cache entry populated?
  uint8_t* data;              // Pointer to tile data in PSRAM (8KB each)
};

// --- GLOBAL CACHE STATE ---
TileCacheEntry* tileCache = nullptr;       // Array of cache entries
uint8_t* tileCacheData = nullptr;          // Contiguous block of tile data in PSRAM
unsigned long cacheHits = 0;               // Statistics
unsigned long cacheMisses = 0;
unsigned long cacheEvictions = 0;

// --- CACHE INITIALIZATION ---
bool initTileCache() {
  Serial.println("Initializing tile cache in PSRAM...");

  // Check if PSRAM is available
  if (!psramFound()) {
    Serial.println("ERROR: PSRAM not found! Cache disabled.");
    return false;
  }

  Serial.printf("PSRAM size: %d bytes\n", ESP.getPsramSize());
  Serial.printf("Free PSRAM: %d bytes\n", ESP.getFreePsram());

  // Allocate cache entry array in PSRAM
  size_t entriesSize = TILE_CACHE_SIZE * sizeof(TileCacheEntry);
  tileCache = (TileCacheEntry*)ps_malloc(entriesSize);
  if (!tileCache) {
    Serial.println("ERROR: Failed to allocate tile cache entries");
    return false;
  }

  // Allocate contiguous block for all tile data in PSRAM
  size_t dataSize = TILE_CACHE_SIZE * TILE_DATA_SIZE;
  tileCacheData = (uint8_t*)ps_malloc(dataSize);
  if (!tileCacheData) {
    Serial.println("ERROR: Failed to allocate tile cache data");
    free(tileCache);
    tileCache = nullptr;
    return false;
  }

  // Initialize cache entries
  for (int i = 0; i < TILE_CACHE_SIZE; i++) {
    tileCache[i].zoom = -1;
    tileCache[i].tileX = -1;
    tileCache[i].tileY = -1;
    tileCache[i].lastUsed = 0;
    tileCache[i].valid = false;
    tileCache[i].data = tileCacheData + (i * TILE_DATA_SIZE);  // Point to slice of data block
  }

  // Reset statistics
  cacheHits = 0;
  cacheMisses = 0;
  cacheEvictions = 0;

  Serial.printf("Tile cache initialized: %d tiles, %.2f MB total\n",
                TILE_CACHE_SIZE,
                (entriesSize + dataSize) / 1024.0 / 1024.0);
  Serial.printf("Free PSRAM after init: %d bytes\n", ESP.getFreePsram());

  return true;
}

// --- CACHE LOOKUP ---
// Returns pointer to tile data if found in cache, nullptr otherwise
uint8_t* tileCacheLookup(int zoom, int tileX, int tileY) {
  if (!tileCache) return nullptr;

  unsigned long currentTime = millis();

  // Linear search through cache (fast enough for 128 entries)
  for (int i = 0; i < TILE_CACHE_SIZE; i++) {
    if (tileCache[i].valid &&
        tileCache[i].zoom == zoom &&
        tileCache[i].tileX == tileX &&
        tileCache[i].tileY == tileY) {

      // Cache hit! Update LRU timestamp
      tileCache[i].lastUsed = currentTime;
      cacheHits++;

      return tileCache[i].data;
    }
  }

  // Cache miss
  cacheMisses++;
  return nullptr;
}

// --- CACHE INSERT ---
// Inserts tile data into cache, evicting LRU entry if full
// Returns pointer to cache slot where data should be written
uint8_t* tileCacheInsert(int zoom, int tileX, int tileY) {
  if (!tileCache) return nullptr;

  unsigned long currentTime = millis();

  // First, try to find an invalid (empty) slot
  for (int i = 0; i < TILE_CACHE_SIZE; i++) {
    if (!tileCache[i].valid) {
      // Found empty slot
      tileCache[i].zoom = zoom;
      tileCache[i].tileX = tileX;
      tileCache[i].tileY = tileY;
      tileCache[i].lastUsed = currentTime;
      tileCache[i].valid = true;

      return tileCache[i].data;
    }
  }

  // Cache is full - find LRU (oldest) entry to evict
  int oldestIndex = 0;
  unsigned long oldestTime = tileCache[0].lastUsed;

  for (int i = 1; i < TILE_CACHE_SIZE; i++) {
    if (tileCache[i].lastUsed < oldestTime) {
      oldestTime = tileCache[i].lastUsed;
      oldestIndex = i;
    }
  }

  // Evict oldest entry
  cacheEvictions++;

  tileCache[oldestIndex].zoom = zoom;
  tileCache[oldestIndex].tileX = tileX;
  tileCache[oldestIndex].tileY = tileY;
  tileCache[oldestIndex].lastUsed = currentTime;
  tileCache[oldestIndex].valid = true;

  return tileCache[oldestIndex].data;
}

// --- CACHE CLEAR ---
// Invalidates all cache entries (useful for testing/debugging)
void tileCacheClear() {
  if (!tileCache) return;

  for (int i = 0; i < TILE_CACHE_SIZE; i++) {
    tileCache[i].valid = false;
    tileCache[i].lastUsed = 0;
  }

  cacheHits = 0;
  cacheMisses = 0;
  cacheEvictions = 0;

  Serial.println("Tile cache cleared");
}

// --- CACHE STATISTICS ---
void printTileCacheStats() {
  if (!tileCache) {
    Serial.println("Tile cache not initialized");
    return;
  }

  // Count valid entries
  int validEntries = 0;
  for (int i = 0; i < TILE_CACHE_SIZE; i++) {
    if (tileCache[i].valid) {
      validEntries++;
    }
  }

  // Calculate hit rate
  unsigned long totalAccesses = cacheHits + cacheMisses;
  float hitRate = (totalAccesses > 0) ? (100.0 * cacheHits / totalAccesses) : 0.0;

  Serial.println("=== TILE CACHE STATISTICS ===");
  Serial.printf("Cache size: %d tiles\n", TILE_CACHE_SIZE);
  Serial.printf("Valid entries: %d (%.1f%% full)\n", validEntries, 100.0 * validEntries / TILE_CACHE_SIZE);
  Serial.printf("Cache hits: %lu\n", cacheHits);
  Serial.printf("Cache misses: %lu\n", cacheMisses);
  Serial.printf("Cache evictions: %lu\n", cacheEvictions);
  Serial.printf("Hit rate: %.1f%%\n", hitRate);
  Serial.printf("Free PSRAM: %d bytes\n", ESP.getFreePsram());
  Serial.println("============================");
}

// --- HELPER: Get cache slot info (for debugging) ---
void printCacheSlot(int index) {
  if (!tileCache || index < 0 || index >= TILE_CACHE_SIZE) return;

  TileCacheEntry* entry = &tileCache[index];
  Serial.printf("Slot %d: ", index);
  if (entry->valid) {
    Serial.printf("z=%d x=%d y=%d lastUsed=%lu\n",
                  entry->zoom, entry->tileX, entry->tileY, entry->lastUsed);
  } else {
    Serial.println("EMPTY");
  }
}

#endif // TILE_CACHE_H
