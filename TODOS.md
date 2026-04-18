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
- [ ] Verify lap list empty-state transition on real hardware: start with `No laps yet`, run a first real lap, confirm the text clears cleanly and does not ghost when paging away and back

## Deferred to v1.1

- [ ] Multi-track candidate selection UI (PR8 scope reduction)
- [ ] Battery gauge IC integration (no hardware IC in v1.0)
- [ ] OTA firmware update via WiFi
- [ ] Bluetooth LE for lower-power phone connectivity
- [ ] Redesign LIVE sector preview/workflow to show current-sector partial delta with independent coloring in the workbench and firmware data flow
- [ ] Add a dashboard/diagnostics workbench prototype for live status and historical metrics such as satellites, actual vs configured rate, drops, and NMEA tail
- [ ] Follow the display refresh and storage roadmap in `docs/superpowers/plans/2026-04-18-display-refresh-and-storage-roadmap.md`, prioritizing software-only perceived-refresh work first, PSRAM-backed SD batching second, and soldering-time hardware reservations throughout
- [ ] Follow the GPS algorithm optimization roadmap in `docs/superpowers/plans/2026-04-18-gps-algorithm-optimization-roadmap.md`, prioritizing fix-quality scoring and path separation before stronger filtering or fusion work
