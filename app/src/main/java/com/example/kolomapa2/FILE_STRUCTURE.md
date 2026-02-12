# KoloMapa2 - File Structure

Android companion app for ESP32S3 bicycle navigation device. This app downloads GPX routes, processes map tiles, and sends data to the ESP device via Bluetooth LE.

## Root Level

### `MainActivity.kt`
Main Android activity entry point. Handles incoming GPX file intents from file managers or share actions. Sets up Compose UI and delegates to MainScreen.

### `MainViewModel.kt`
Central ViewModel coordinating all app logic. Manages:
- Trip import/deletion
- Map tile downloading with progress tracking
- BLE connection to ESP32S3
- Data transfer (trips and tiles) to device
- UI state (loading, errors, storage stats)

## Models (`models/`)

### `Trip.kt`
Simple data class combining trip metadata and GPX content string.

### `TripMetadata.kt`
Serializable trip information including:
- Name, distance, elevation gain/loss
- Point count and bounding box
- Route description with origin/destination parsing
- Timestamp

## UI (`ui/`)

### `MainScreen.kt`
Main Compose UI with:
- Trip list display with cards
- BLE connection status indicator
- Import GPX file picker
- Trip details dialog
- Storage statistics
- Transfer progress indicators
- Permission requests (Bluetooth, location, storage)

### Theme files (`ui/theme/`)
- `Color.kt` - Material Design color palette
- `Theme.kt` - App theme configuration
- `Type.kt` - Typography definitions

## Utils (`utils/`)

### `BleManager.kt`
Bluetooth LE communication with ESP32S3:
- Device scanning and connection management
- Custom service UUID: 12345678-1234-1234-1234-123456789abc
- Tile transfer with RLE compression (16KB ESP limit)
- Tile inventory sync to skip tiles already on device
- Trip data transfer (GPX + metadata JSON)
- MTU negotiation (517 bytes) and chunked transfers (500 byte chunks)
- Flow control with write completion flags

### `GpxParser.kt`
Parses GPX XML files to extract:
- Track points (lat/lon/elevation)
- Trip names from metadata and track elements
- Route descriptions with origin→destination parsing
- Statistics: distance (Haversine formula), elevation changes
- Bounding box calculation

### `RleCompression.kt`
Run-Length Encoding compression for map tiles:
- Optimized for 1-bit bitmaps with large uniform areas
- Format: [count][value] pairs with 2-byte size header
- Compression ratio calculation
- Encode/decode with validation

### `StorageManager.kt`
File system operations in external storage (`/KoloMapa2/`):
- Trips: Save/load GPX files and metadata JSON
- Map tiles: Store preprocessed 1-bit bitmaps (zoom/x/y.bin)
- Storage statistics (total size, counts)
- Tile existence checking

### `TileDownloader.kt`
Downloads map tiles from OpenStreetMap tile servers:
- Uses keyless public raster tiles (`z/x/y.png`)
- Downloads zoom levels 9-18 with 2-tile padding
- Caching: skips already downloaded tiles
- Automatic preprocessing to 1-bit format
- Progress callbacks

### `TilePreprocessor.kt`
Converts PNG map tiles to 1-bit bitmaps (matches ESP32S3 processing):
- Floyd-Steinberg dithering with error diffusion
- Image processing: gamma correction (2.50), contrast (1.3), threshold (128)
- Output: 8KB per tile (256x256 pixels, 8 pixels per byte)
- Tile coordinate calculation (lat/lon → tile x/y)
- MUST match Arduino preprocessing exactly for consistency

## Data Flow

1. User imports GPX → `GpxParser` extracts route → `StorageManager` saves
2. `TileDownloader` fetches tiles for route bounding box
3. `TilePreprocessor` converts PNG → 1-bit bitmap → `StorageManager` saves
4. User sends to ESP → `BleManager` requests tile inventory, transfers trip + missing tiles via BLE
5. ESP32S3 receives and displays on e-paper screen

## Key Design Decisions

- External storage for persistence across reinstalls
- Exact preprocessing match with Arduino for consistent map rendering
- RLE compression for efficient BLE transfer (ESP has 16KB tile limit)
- Chunked transfers with flow control for reliability
- Offline-first: all data cached locally before transfer
