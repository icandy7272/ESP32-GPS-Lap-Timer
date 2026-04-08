(function (root, factory) {
  if (typeof module === "object" && module.exports) {
    module.exports = factory();
    return;
  }

  root.UiPreviewScenarios = factory();
})(typeof globalThis !== "undefined" ? globalThis : this, function () {
  const DEFAULT_SCENARIO_ID = "ready-to-drive";
  const TRACKS_BASE = [
    { id: "track_001", name: "Ningbo Kart Center" },
    { id: "track_002", name: "Shanghai International Circuit" },
    { id: "track_003", name: "Zhuhai International Circuit" },
    { id: "track_004", name: "Tianma Circuit" },
    { id: "track_005", name: "Ningbo South Layout" },
    { id: "track_006", name: "Hangzhou Kart Track" },
  ];

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
      tracks: TRACKS_BASE.slice(0, 1),
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
        firmware_version: "v1.0.0",
        uptime_seconds: 3,
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
      tracks: TRACKS_BASE.slice(0, 1),
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
        firmware_version: "v1.0.0",
        uptime_seconds: 14,
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
      tracks: TRACKS_BASE.slice(0, 2),
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
        firmware_version: "v1.0.0",
        uptime_seconds: 96,
        sd_free_gb: 13.8,
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
      sessions: ["session_20260408_01.vbo"],
      tracks: TRACKS_BASE.slice(0, 2),
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
        firmware_version: "v1.0.0",
        uptime_seconds: 842,
        sd_free_gb: 13.4,
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
      sessions: ["session_20260408_02.vbo"],
      tracks: TRACKS_BASE.slice(0, 1),
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
        firmware_version: "v1.0.0",
        uptime_seconds: 1114,
        sd_free_gb: 13.2,
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
      sessions: ["session_20260408_03.vbo"],
      tracks: TRACKS_BASE.slice(0, 1),
      settings: { wifi_ssid: "GPS-LapTimer", wifi_pass: "12345678", brightness: 200 },
      device: {
        screen: "driving",
        delta_ms: 0,
        delta_valid: false,
        off_track: true,
        lap_count: 5,
        best_lap_number: 3,
        current_lap_time_ms: 32180,
        boot_state: "ready",
        firmware_version: "v1.0.0",
        uptime_seconds: 934,
        sd_free_gb: 13.1,
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
      sessions: ["session_20260408_04.vbo", "session_20260407_02.vbo"],
      tracks: TRACKS_BASE.slice(0, 2),
      settings: { wifi_ssid: "GPS-LapTimer", wifi_pass: "12345678", brightness: 200 },
      device: {
        screen: "lap-list",
        delta_ms: 0,
        delta_valid: false,
        off_track: false,
        lap_count: 9,
        best_lap_number: 6,
        current_lap_time_ms: 0,
        boot_state: "ready",
        firmware_version: "v1.0.0",
        uptime_seconds: 1262,
        sd_free_gb: 12.9,
        lap_rows: [
          { label: " 1", time: "0:52.84", delta: "+1.95" },
          { label: " 2", time: "0:52.22", delta: "+1.33" },
          { label: " 3", time: "0:51.67", delta: "+0.78" },
          { label: " 4", time: "0:51.31", delta: "+0.42" },
          { label: " 5", time: "0:51.06", delta: "+0.17" },
          { label: " 6", time: "0:50.89", delta: "BEST", is_best: true },
          { label: " 7", time: "0:51.14", delta: "+0.25" },
          { label: " 8", time: "0:51.48", delta: "+0.59" },
          { label: " 9", time: "0:51.63", delta: "+0.74" },
        ],
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
        "session_20260408_05.vbo",
        "session_20260407_03.vbo",
        "session_20260405_01.vbo",
        "session_20260404_02.vbo",
        "session_20260402_01.vbo",
        "session_20260329_01.vbo",
      ],
      tracks: TRACKS_BASE.slice(0),
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
        firmware_version: "v1.0.0",
        uptime_seconds: 412,
        sd_free_gb: 12.7,
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
