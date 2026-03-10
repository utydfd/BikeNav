# BikeNav
An GPS navigation(originally intended for biking) with an E-ink screen, ESP32-S3 MCU and an SD card.
This repository has code for the arduino project and an android app.

## Map format
Its using standart raster 256x256px map tiles, right now from OSM, but can be changed to others.
The tiles are saved on the SD card in a ```ZOOM_LEVEL/X/Y.tile``` folder structure as 1 bit bitmap images.

## Data flow

Either import a GPX track or plan right inside the app(right now using project OSRM API), it downloads the required map tiles and saves them.

I can pick a trip from the main screen and send it over to the ESP via BLE, first gets sent the GPX and also the tiles. They are converted PNG to bitmap, compressed using RLE for faster transfers. The ESP is saving them to the SD card as they come. Its also appending an index of tiles, which is sent from the ESP to the app, so that only the missing tiles are sent.

## ESPs software

The home screen contains smartphone like icons to "apps". The main one is obviously the map, which by default shows the current location. Pressing options brings up list of saved trips, clicking on it shows the preview and i can also start the navigation.

When navigating, the map rotates based on the upcoming GPX track; it shows the next turn distance and arrow(just calculated based on the angle of the turn). There are also screens for the progress and elevation graph. The GPX is loaded into PSRAM *(512kb allocated)* and PSRAM is also used for caching the map tiles *(~6MB allocated)*.

Other apps include the spedometer, phone(which has music controls along with battery %), weather(phone fetches from an API the forecast and also rain radar images), tracker(for recording GPX tracks), info(was used for debugging, has GPS, SD and battery statuses) and also mines and snake games. Those not ideal but playable on the E-ink.

## Hardware

Its on a custom PCB, a standard 2 layer one. There are 2 regulators, one powering the ESP and other parts and the other solely for powering the front light - it has the EN pin connected to a GPIO so its controlled in software. The GPS module is connected via UART and also has an EN pin controllable. Micro SD card and the display are connected via SPI to the ESP. There is a TP4056 chip for charging, without the LEDs - its connected to the ESP so it can know the charging status.

For control, there is a rotary encoder(with a button) and 4 more buttons. The battery voltage is monitored with the ESP.

## BOM
- [Display](https://www.aliexpress.com/item/1005005183232092.html)
- [GP-02 GPS module](https://www.aliexpress.com/item/1005008117586848.html)
- [ESP32-S3 wroom module](https://www.aliexpress.com/item/1005004639132852.html)
- [LDO 3V3 regulator](https://www.aliexpress.com/item/1005009410572746.html)
- [2.2uF Capacitors](https://www.aliexpress.com/item/1005002769519482.html)
- [15mm rotary endoder](https://www.aliexpress.com/item/1631706397.html)
- [USB-C breakout module](https://www.aliexpress.com/item/1005008296556115.html)
