// ============================================================
// TFT_eSPI User Setup — ILI9341 on ESP32-S3
// ZJY320S0800TG02, 3.2" 320×240 (landscape = 320 wide)
// Place this file in src/ and add  -I src  to build_flags
// so TFT_eSPI finds it before its own User_Setup.h.
// ============================================================

#define USER_SETUP_LOADED 1  // Tells TFT_eSPI to skip its own setup

// --- Driver ---
#define ILI9341_DRIVER

// --- Resolution (portrait native; rotation applied at runtime) ---
#define TFT_WIDTH   240
#define TFT_HEIGHT  320

// --- SPI pins ---
#define TFT_MOSI  11
#define TFT_SCLK  12
#define TFT_MISO  13   // shared SPI bus MISO (needed for SD card on same bus)
#define TFT_CS    10
#define TFT_DC     9
#define TFT_RST    8
#define TFT_BL    47   // moved from GPIO46 (strapping pin) to GPIO47

// ESP32-S3: force TFT_eSPI onto its explicit FSPI code path.
// Without this, the library mixes Arduino's FSPI index (0) with the
// ESP-IDF register macros (which expect SPI2/SPI3 => 2/3) and can
// crash in TFT_eSPI::init() before any pixels are drawn.
#define USE_FSPI_PORT

// --- Backlight active level ---
#define TFT_BACKLIGHT_ON HIGH

// --- Fonts to compile in ---
#define LOAD_GLCD    // Font 1. Original Adafruit 8 px font
#define LOAD_FONT2   // Font 2. Small 16 px high font, needs ~3.4 kB in flash
#define LOAD_FONT4   // Font 4. Medium 26 px high, needs ~5.7 kB in flash
#define LOAD_FONT6   // Font 6. Large 48 px, numbers/symbols only
#define LOAD_FONT7   // Font 7. 7-segment 48 px, numbers/symbols only
#define LOAD_FONT8   // Font 8. Large 75 px, numbers/symbols only
#define LOAD_GFXFF   // FreeFonts — FF1..FF48
#define SMOOTH_FONT  // Anti-aliased VLW fonts

// --- SPI speed ---
#define SPI_FREQUENCY       40000000   // 40 MHz — safe for most ILI9341 clones
#define SPI_READ_FREQUENCY   6000000   // Slower read back
