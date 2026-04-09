(function (root, factory) {
  if (typeof module === "object" && module.exports) {
    module.exports = factory();
    return;
  }

  root.UiPreviewWebConsole = factory();
})(typeof globalThis !== "undefined" ? globalThis : this, function () {
  function escapeHtml(value) {
    return String(value == null ? "" : value)
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;")
      .replace(/'/g, "&#39;");
  }

  function formatBestLap(bestLapMs) {
    const normalized = Number(bestLapMs) || 0;
    if (normalized <= 0) {
      return "--";
    }

    return (normalized / 1000).toFixed(3) + "s";
  }

  function renderEmptyListItem(label) {
    return '<li class="web-console__item web-console__item--empty">' + escapeHtml(label) + "</li>";
  }

  function getRecordingCtaLabel(isRecording) {
    return isRecording ? "Stop Recording" : "Start Recording";
  }

  function renderSessionsMarkup(sessions) {
    if (!Array.isArray(sessions) || sessions.length === 0) {
      return renderEmptyListItem("No sessions");
    }

    return sessions
      .map(function (sessionName) {
        const safeSession = escapeHtml(sessionName);
        return (
          '<li class="web-console__item">' +
          '<span class="web-console__session-link">' +
          safeSession +
          "</span>" +
          "</li>"
        );
      })
      .join("");
  }

  function renderTracksMarkup(tracks) {
    if (!Array.isArray(tracks) || tracks.length === 0) {
      return renderEmptyListItem("No tracks");
    }

    return tracks
      .map(function (track) {
        const trackId = escapeHtml((track && track.id) || "unknown");
        const trackName = escapeHtml((track && track.name) || "Unknown");
        return (
          '<li class="web-console__item web-console__item--track">' +
          '<span class="web-console__track-name">' +
          trackName +
          " (" +
          trackId +
          ")" +
          "</span>" +
          '<span class="web-console__track-actions">' +
          '<button type="button" class="web-console__action-button" disabled>Select</button>' +
          '<button type="button" class="web-console__action-button web-console__action-button--danger" disabled>Delete</button>' +
          "</span>" +
          "</li>"
        );
      })
      .join("");
  }

  function renderWebConsoleMarkup(scenario) {
    const data = scenario || {};
    const status = data.status || {};
    const settings = data.settings || {};
    const isRecording = Boolean(status.recording);
    const gpsFix = status.gps_fix ? "Yes" : "No";
    const recordingState = isRecording ? "REC" : "Idle";
    const currentLap = Number(status.current_lap) || 0;
    const satellites = Number(status.satellites) || 0;
    const bestLap = formatBestLap(status.best_lap_ms);
    const trackName = status.track ? escapeHtml(status.track) : "None";
    const ssid = escapeHtml(settings.wifi_ssid || "");
    const wifiPass = escapeHtml(settings.wifi_pass || "");
    const brightness = Number(settings.brightness);
    const sessions = Array.isArray(data.sessions) ? data.sessions : [];
    const tracks = Array.isArray(data.tracks) ? data.tracks : [];

    return (
      '<div class="web-console-preview">' +
      '<div class="web-console__card" data-card="status">' +
      "<h2>Status</h2>" +
      '<div class="web-console__row"><span class="web-console__label">GPS Fix</span><span class="web-console__value">' +
      gpsFix +
      "</span></div>" +
      '<div class="web-console__row"><span class="web-console__label">Satellites</span><span class="web-console__value">' +
      satellites +
      "</span></div>" +
      '<div class="web-console__row"><span class="web-console__label">Recording</span><span class="web-console__value">' +
      recordingState +
      "</span></div>" +
      '<div class="web-console__row"><span class="web-console__label">Current Lap</span><span class="web-console__value">' +
      currentLap +
      "</span></div>" +
      '<div class="web-console__row"><span class="web-console__label">Best Lap</span><span class="web-console__value">' +
      bestLap +
      "</span></div>" +
      '<div class="web-console__row"><span class="web-console__label">Track</span><span class="web-console__value">' +
      trackName +
      "</span></div>" +
      '<button type="button" class="web-console__recording-cta' +
      (isRecording ? " web-console__recording-cta--active" : "") +
      '" disabled>' +
      getRecordingCtaLabel(isRecording) +
      "</button>" +
      "</div>" +
      '<div class="web-console__card" data-card="sessions">' +
      "<h2>Sessions</h2>" +
      '<ul class="web-console__list">' +
      renderSessionsMarkup(sessions) +
      "</ul>" +
      "</div>" +
      '<div class="web-console__card" data-card="tracks">' +
      "<h2>Tracks</h2>" +
      '<ul class="web-console__list">' +
      renderTracksMarkup(tracks) +
      "</ul>" +
      '<h3 class="web-console__subheading">Add Track</h3>' +
      '<form class="web-console__form" aria-label="Add Track form shell">' +
      '<input class="web-console__input" placeholder="Track name" disabled />' +
      '<p class="web-console__hint">Start/Finish Line</p>' +
      '<input class="web-console__input" placeholder="SF lat1" disabled />' +
      '<input class="web-console__input" placeholder="SF lon1" disabled />' +
      '<input class="web-console__input" placeholder="SF lat2" disabled />' +
      '<input class="web-console__input" placeholder="SF lon2" disabled />' +
      '<input class="web-console__input" placeholder="SF heading (deg)" disabled />' +
      '<p class="web-console__hint">Sector Splits (optional, up to 3)</p>' +
      '<button type="button" class="web-console__secondary-button" disabled>Add Sector Split</button>' +
      '<button type="button" class="web-console__primary-button" disabled>Create Track</button>' +
      "</form>" +
      "</div>" +
      '<div class="web-console__card" data-card="settings">' +
      "<h2>Settings</h2>" +
      '<form class="web-console__form" aria-label="Settings form shell">' +
      '<label class="web-console__field">SSID<input class="web-console__input" value="' +
      ssid +
      '" disabled /></label>' +
      '<label class="web-console__field">Password<input class="web-console__input" type="password" value="' +
      wifiPass +
      '" disabled /></label>' +
      '<label class="web-console__field">Brightness<input class="web-console__input" value="' +
      (Number.isFinite(brightness) ? brightness : 0) +
      '" disabled /></label>' +
      '<button type="button" class="web-console__primary-button" disabled>Save Settings</button>' +
      "</form>" +
      "</div>" +
      "</div>"
    );
  }

  return {
    renderWebConsoleMarkup: renderWebConsoleMarkup,
  };
});
