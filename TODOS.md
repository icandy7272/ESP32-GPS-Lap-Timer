# Hardware Validation TODOs

## Before v1.0 Release

- [ ] Verify GPS cold-start time under open sky (target < 30s)
- [ ] Measure lap timing accuracy against known reference (target +/- 0.02s)
- [ ] SD card endurance test: 2-hour continuous recording session
- [ ] Power-loss recovery test: pull power mid-session, verify .tmp rename
- [ ] TFT readability in direct sunlight at max brightness
- [ ] WiFi range test: phone at 10m / 20m from device
- [ ] Thermal test: 1-hour session in enclosure at 40C ambient
- [ ] Battery runtime measurement (if battery board is connected)

## Deferred to v1.1

- [ ] Multi-track candidate selection UI (PR8 scope reduction)
- [ ] Battery gauge IC integration (no hardware IC in v1.0)
- [ ] OTA firmware update via WiFi
- [ ] Bluetooth LE for lower-power phone connectivity
