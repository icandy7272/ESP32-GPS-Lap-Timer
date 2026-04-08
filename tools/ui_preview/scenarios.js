(function (root, factory) {
  if (typeof module === "object" && module.exports) {
    module.exports = factory();
    return;
  }

  root.UiPreviewScenarios = factory();
})(typeof globalThis !== "undefined" ? globalThis : this, function () {
  const DEFAULT_SCENARIO_ID = "ready-to-drive";

  const SCENARIOS = [
    {
      id: "cold-boot",
      label: "Cold Boot",
      status: {
        gps_fix: false,
        satellites: 0,
        recording: false,
        current_lap: 0,
        best_lap_ms: 0,
        track: "Ningbo Kart Center",
      },
      sessions: [],
      tracks: [{ id: 1, name: "Ningbo Kart Center", points: 18 }],
      settings: { wifi_ssid: "GPS-LapTimer", wifi_pass: "12345678", brightness: 200 },
      device: {
        screen: "boot",
        delta_ms: 0,
        delta_valid: false,
        off_track: false,
        lap_count: 0,
        best_lap_number: -1,
        current_lap_time_ms: 0,
        boot_state: "splash",
      },
    },
    {
      id: "gps-searching",
      label: "GPS Searching",
      status: {
        gps_fix: false,
        satellites: 4,
        recording: false,
        current_lap: 0,
        best_lap_ms: 0,
        track: "Ningbo Kart Center",
      },
      sessions: [],
      tracks: [{ id: 1, name: "Ningbo Kart Center", points: 18 }],
      settings: { wifi_ssid: "GPS-LapTimer", wifi_pass: "12345678", brightness: 200 },
      device: {
        screen: "boot",
        delta_ms: 0,
        delta_valid: false,
        off_track: false,
        lap_count: 0,
        best_lap_number: -1,
        current_lap_time_ms: 0,
        boot_state: "gps-searching",
      },
    },
    {
      id: "ready-to-drive",
      label: "Ready To Drive",
      status: {
        gps_fix: true,
        satellites: 10,
        recording: false,
        current_lap: 1,
        best_lap_ms: 52380,
        track: "Ningbo Kart Center",
      },
      sessions: [],
      tracks: [
        { id: 1, name: "Ningbo Kart Center", points: 18 },
        { id: 2, name: "Shanghai International Circuit", points: 24 },
      ],
      settings: { wifi_ssid: "GPS-LapTimer", wifi_pass: "12345678", brightness: 200 },
      device: {
        screen: "driving",
        delta_ms: 0,
        delta_valid: false,
        off_track: false,
        lap_count: 0,
        best_lap_number: -1,
        current_lap_time_ms: 0,
        boot_state: "ready",
      },
    },
    {
      id: "recording",
      label: "Recording",
      status: {
        gps_fix: true,
        satellites: 11,
        recording: true,
        current_lap: 4,
        best_lap_ms: 51230,
        track: "Ningbo Kart Center",
      },
      sessions: [{ id: "2026-04-08-run-1", laps: 4, best_lap_ms: 51230 }],
      tracks: [
        { id: 1, name: "Ningbo Kart Center", points: 18 },
        { id: 2, name: "Shanghai International Circuit", points: 24 },
      ],
      settings: { wifi_ssid: "GPS-LapTimer", wifi_pass: "12345678", brightness: 200 },
      device: {
        screen: "driving",
        delta_ms: -130,
        delta_valid: true,
        off_track: false,
        lap_count: 4,
        best_lap_number: 3,
        current_lap_time_ms: 18890,
        boot_state: "ready",
      },
    },
    {
      id: "best-lap-improved",
      label: "Best Lap Improved",
      status: {
        gps_fix: true,
        satellites: 12,
        recording: true,
        current_lap: 7,
        best_lap_ms: 50120,
        track: "Ningbo Kart Center",
      },
      sessions: [{ id: "2026-04-08-run-2", laps: 7, best_lap_ms: 50120 }],
      tracks: [{ id: 1, name: "Ningbo Kart Center", points: 18 }],
      settings: { wifi_ssid: "GPS-LapTimer", wifi_pass: "12345678", brightness: 200 },
      device: {
        screen: "driving",
        delta_ms: -280,
        delta_valid: true,
        off_track: false,
        lap_count: 7,
        best_lap_number: 7,
        current_lap_time_ms: 430,
        boot_state: "ready",
      },
    },
    {
      id: "off-track",
      label: "Off Track",
      status: {
        gps_fix: true,
        satellites: 9,
        recording: true,
        current_lap: 5,
        best_lap_ms: 51540,
        track: "Ningbo Kart Center",
      },
      sessions: [{ id: "2026-04-08-run-3", laps: 5, best_lap_ms: 51540 }],
      tracks: [{ id: 1, name: "Ningbo Kart Center", points: 18 }],
      settings: { wifi_ssid: "GPS-LapTimer", wifi_pass: "12345678", brightness: 200 },
      device: {
        screen: "status",
        delta_ms: 0,
        delta_valid: false,
        off_track: true,
        lap_count: 5,
        best_lap_number: 3,
        current_lap_time_ms: 0,
        boot_state: "ready",
      },
    },
    {
      id: "session-review",
      label: "Session Review",
      status: {
        gps_fix: true,
        satellites: 10,
        recording: false,
        current_lap: 0,
        best_lap_ms: 50890,
        track: "Ningbo Kart Center",
      },
      sessions: [
        { id: "2026-04-08-run-4", laps: 8, best_lap_ms: 50890 },
        { id: "2026-04-07-run-2", laps: 11, best_lap_ms: 51110 },
      ],
      tracks: [
        { id: 1, name: "Ningbo Kart Center", points: 18 },
        { id: 2, name: "Shanghai International Circuit", points: 24 },
      ],
      settings: { wifi_ssid: "GPS-LapTimer", wifi_pass: "12345678", brightness: 200 },
      device: {
        screen: "lap-list",
        delta_ms: 0,
        delta_valid: false,
        off_track: false,
        lap_count: 8,
        best_lap_number: 6,
        current_lap_time_ms: 0,
        boot_state: "ready",
      },
    },
    {
      id: "heavy-track-library",
      label: "Heavy Track Library",
      status: {
        gps_fix: true,
        satellites: 11,
        recording: false,
        current_lap: 0,
        best_lap_ms: 0,
        track: "Ningbo Kart Center",
      },
      sessions: [
        { id: "2026-04-08-run-5", laps: 5, best_lap_ms: 52002 },
        { id: "2026-04-07-run-3", laps: 12, best_lap_ms: 51239 },
        { id: "2026-04-05-run-1", laps: 9, best_lap_ms: 51555 },
      ],
      tracks: [
        { id: 1, name: "Ningbo Kart Center", points: 18 },
        { id: 2, name: "Shanghai International Circuit", points: 24 },
        { id: 3, name: "Zhuhai International Circuit", points: 20 },
        { id: 4, name: "Tianma Circuit", points: 16 },
        { id: 5, name: "Ningbo South Layout", points: 22 },
        { id: 6, name: "Hangzhou Kart Track", points: 15 },
      ],
      settings: { wifi_ssid: "GPS-LapTimer", wifi_pass: "12345678", brightness: 200 },
      device: {
        screen: "status",
        delta_ms: 0,
        delta_valid: false,
        off_track: false,
        lap_count: 0,
        best_lap_number: -1,
        current_lap_time_ms: 0,
        boot_state: "ready",
      },
    },
  ];

  function getScenarioById(id) {
    const found = SCENARIOS.find(function (scenario) {
      return scenario.id === id;
    });

    if (found) {
      return found;
    }

    return (
      SCENARIOS.find(function (scenario) {
        return scenario.id === DEFAULT_SCENARIO_ID;
      }) || SCENARIOS[0]
    );
  }

  return {
    SCENARIOS: SCENARIOS,
    DEFAULT_SCENARIO_ID: DEFAULT_SCENARIO_ID,
    getScenarioById: getScenarioById,
  };
});
