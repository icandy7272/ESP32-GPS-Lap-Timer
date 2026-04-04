#pragma once

// ============================================================
// GPIO pin assignments — ESP32-S3 GPS Lap Timer
// Board: ESP32-S3-DevKitC-1 (N16R8)
// Avoid strapping pins: GPIO0, GPIO3, GPIO45, GPIO46
// ============================================================

// --- TFT display (ILI9341, SPI) ---
constexpr int PIN_TFT_MOSI = 11;
constexpr int PIN_TFT_SCLK = 12;
constexpr int PIN_TFT_MISO = 13;  // shared SPI bus MISO (SD reads need this)
constexpr int PIN_TFT_CS   = 10;
constexpr int PIN_TFT_DC   =  9;
constexpr int PIN_TFT_RST  =  8;
constexpr int PIN_TFT_BL   = 47;  // backlight PWM (HIGH = on), moved from GPIO46 (strapping pin)

// --- SD card module (SPI, shared bus with TFT) ---
constexpr int PIN_SD_MOSI  = 11;  // shared with TFT
constexpr int PIN_SD_SCLK  = 12;  // shared with TFT
constexpr int PIN_SD_MISO  = 13;  // SD-only MISO (TFT is write-only)
constexpr int PIN_SD_CS    = 42;  // separate chip-select

// --- GPS module (BK-880, u-blox M9N, UART) ---
constexpr int PIN_GPS_RX   = 17;  // ESP32 RX ← GPS TX
constexpr int PIN_GPS_TX   = 18;  // ESP32 TX → GPS RX
constexpr int PIN_GPS_PPS  = 16;  // 1 Hz / 25 Hz pulse-per-second

// --- Buttons ---
constexpr int PIN_BTN_RECORD = 4;  // latching: power / start-stop recording
constexpr int PIN_BTN_SECTOR = 5;  // momentary: mark sector / lap split

// --- On-board LED (DevKitC-1 RGB LED is GPIO48; plain LED on GPIO2 on many boards) ---
constexpr int PIN_LED = 2;
