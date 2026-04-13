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

  function hasOwn(object, key) {
    return Boolean(object) && Object.prototype.hasOwnProperty.call(object, key);
  }

  function toFiniteNumber(value, fallback) {
    var normalized = Number(value);
    return Number.isFinite(normalized) ? normalized : fallback;
  }

  function formatBestLap(bestLapMs) {
    var normalized = Number(bestLapMs) || 0;
    if (normalized <= 0) {
      return "--";
    }

    return (normalized / 1000).toFixed(3) + "s";
  }

  function normalizeHeading(deg) {
    var value = toFiniteNumber(deg, 0) % 360;
    if (value < 0) {
      value += 360;
    }
    return value;
  }

  function bearingDeg(p1, p2) {
    var lat1 = p1.lat * Math.PI / 180;
    var lat2 = p2.lat * Math.PI / 180;
    var dLon = (p2.lon - p1.lon) * Math.PI / 180;
    var y = Math.sin(dLon) * Math.cos(lat2);
    var x = Math.cos(lat1) * Math.sin(lat2) -
      Math.sin(lat1) * Math.cos(lat2) * Math.cos(dLon);
    return normalizeHeading(Math.atan2(y, x) * 180 / Math.PI);
  }

  function crossingHeadingDeg(p1, p2, flipped) {
    var heading = normalizeHeading(bearingDeg(p1, p2) + 90);
    if (flipped) {
      heading = normalizeHeading(heading + 180);
    }
    return heading;
  }

  function compassLabel(deg) {
    var labels = ["N", "NE", "E", "SE", "S", "SW", "W", "NW"];
    return labels[Math.floor((normalizeHeading(deg) + 22.5) / 45) % 8];
  }

  function headingArrow(deg) {
    var arrows = ["↑", "↗", "→", "↘", "↓", "↙", "←", "↖"];
    return arrows[Math.floor((normalizeHeading(deg) + 22.5) / 45) % 8];
  }

  function clonePoint(point) {
    if (!point) {
      return null;
    }
    var lat = toFiniteNumber(point.lat, null);
    var lon = toFiniteNumber(point.lon, null);
    if (lat == null || lon == null) {
      return null;
    }
    return {
      lat: lat,
      lon: lon,
      sampleCount: hasOwn(point, "sampleCount") ? toFiniteNumber(point.sampleCount, 6) : 6,
      spreadM: hasOwn(point, "spreadM") ? toFiniteNumber(point.spreadM, 0) : 0,
      captureAgeMs: hasOwn(point, "captureAgeMs") ? toFiniteNumber(point.captureAgeMs, 0) : 0,
      confidence: hasOwn(point, "confidence") ? String(point.confidence || "") : "",
    };
  }

  function formatCoord(point, fallback) {
    if (!point) {
      return fallback || "Not set";
    }
    return point.lat.toFixed(7) + ", " + point.lon.toFixed(7);
  }

  function pointSampleCount(point) {
    return hasOwn(point, "sampleCount") ? toFiniteNumber(point.sampleCount, 6) : 6;
  }

  function pointSpreadMeters(point) {
    return hasOwn(point, "spreadM") ? toFiniteNumber(point.spreadM, 0) : 0;
  }

  function pointConfidenceTier(point) {
    if (point && point.confidence) {
      return String(point.confidence);
    }
    var sampleCount = pointSampleCount(point);
    var spreadM = pointSpreadMeters(point);
    if (sampleCount >= 5 && spreadM <= 1.2) {
      return "high";
    }
    if (sampleCount >= 3 && spreadM <= 2.5) {
      return "medium";
    }
    return "low";
  }

  function pointConfidenceActionLabel(point) {
    var tier = typeof point === "string" ? point : pointConfidenceTier(point);
    if (tier === "high") {
      return "Good - ready to save";
    }
    if (tier === "medium") {
      return "Acceptable - short lines may drift";
    }
    return "Noisy - try again";
  }

  function renderEmptyListItem(label) {
    return '<li class="web-console__item web-console__item--empty">' + escapeHtml(label) + "</li>";
  }

  function hasSelectedTrack(status) {
    return Boolean(status.track && status.track !== "No Track");
  }

  function normalizeStatus(source, tracks, scenario) {
    var status = source || {};
    var trackId = status.track_id || "";
    if (!trackId && Array.isArray(tracks)) {
      var matchedTrack = tracks.find(function (track) {
        return track && track.name === status.track;
      });
      trackId = matchedTrack ? matchedTrack.id : "";
    }

    var normalized = {
      gps_fix: Boolean(status.gps_fix),
      satellites: Number(status.satellites) || 0,
      recording: Boolean(status.recording),
      current_lap: Number(status.current_lap) || 0,
      best_lap_ms: Number(status.best_lap_ms) || 0,
      track: String(status.track || ""),
      track_id: String(trackId || ""),
      track_source: String(status.track_source || ""),
      track_locked_manual: Boolean(status.track_locked_manual),
      recording_cta_state: String(status.recording_cta_state || ""),
      recording_cta_reason: String(status.recording_cta_reason || ""),
      current_track_distance_m: Number(
        status.current_track_distance_m != null
          ? status.current_track_distance_m
          : (scenario && scenario.current_track_distance_m)
      ),
      nearby_tracks: Array.isArray(status.nearby_tracks)
        ? status.nearby_tracks.slice()
        : (Array.isArray(scenario && scenario.nearby_tracks)
            ? scenario.nearby_tracks.slice()
            : []),
    };

    if (!normalized.track_source) {
      normalized.track_source = hasSelectedTrack(normalized)
        ? "Auto-detected"
        : "Waiting to select";
    }

    if (!normalized.recording_cta_state) {
      normalized.recording_cta_state = normalized.recording
        ? "recording"
        : (hasSelectedTrack(normalized) ? "ready" : "blocked_no_track");
    }

    if (!normalized.recording_cta_reason &&
        normalized.recording_cta_state === "blocked_no_track") {
      normalized.recording_cta_reason = "Select a track before recording.";
    }

    if (!Number.isFinite(normalized.current_track_distance_m)) {
      normalized.current_track_distance_m = -1;
    }

    normalized.nearby_tracks = normalized.nearby_tracks
      .filter(function (track) {
        return Boolean(track && track.id && track.name && Number.isFinite(Number(track.distance_m)));
      })
      .map(function (track) {
        return {
          id: String(track.id),
          name: String(track.name),
          distance_m: Number(track.distance_m),
        };
      })
      .sort(function (left, right) {
        return left.distance_m - right.distance_m;
      });

    return normalized;
  }

  function getRecordingCtaConfig(status) {
    if (status.recording || status.recording_cta_state === "recording") {
      return {
        label: "Stop Recording",
        disabled: false,
        active: true,
      };
    }

    if (status.recording_cta_state === "ready") {
      return {
        label: "Ready to Record",
        disabled: false,
        active: false,
      };
    }

    if (status.recording_cta_state === "blocked_no_track") {
      return {
        label: "Select Track to Record",
        disabled: true,
        active: false,
      };
    }

    return {
      label: "Start Recording",
      disabled: false,
      active: false,
    };
  }

  function renderSessionsMarkup(sessions) {
    if (!Array.isArray(sessions) || sessions.length === 0) {
      return renderEmptyListItem("No sessions");
    }

    function normalizeSession(session) {
      if (typeof session === "string") {
        return {
          filename: session,
          date: "Unknown date",
          track: "Unknown track",
          best_lap_ms: -1,
        };
      }

      return {
        filename: String((session && session.filename) || "session.vbo"),
        date: String((session && session.date) || "Unknown date"),
        track: String((session && session.track) || "Unknown track"),
        best_lap_ms: Number((session && session.best_lap_ms) || -1),
      };
    }

    return sessions
      .map(function (session) {
        var item = normalizeSession(session);
        return (
          '<li class="web-console__item web-console__item--session">' +
          '<div class="web-console__session-card">' +
          '<div class="web-console__session-grid">' +
          '<div><span class="web-console__session-label">Date</span><strong>' +
          escapeHtml(item.date) +
          "</strong></div>" +
          '<div><span class="web-console__session-label">Track</span><strong>' +
          escapeHtml(item.track) +
          "</strong></div>" +
          '<div><span class="web-console__session-label">Best Lap</span><strong>' +
          escapeHtml(formatBestLap(item.best_lap_ms)) +
          "</strong></div>" +
          "</div>" +
          '<div class="web-console__session-actions">' +
          '<span class="web-console__session-link">Download ' +
          escapeHtml(item.filename) +
          "</span>" +
          "</div>" +
          "</div>" +
          "</li>"
        );
      })
      .join("");
  }

  function renderCurrentTrackMarkup(status) {
    var currentTrackName = hasSelectedTrack(status)
      ? escapeHtml(status.track)
      : "No Track selected";
    var distanceText = status.current_track_distance_m >= 0
      ? Math.round(status.current_track_distance_m) + " m away"
      : "";
    var lockChip = status.track_locked_manual
      ? '<span class="web-console__track-badge">' +
        escapeHtml(status.track_source === "Newly created" ? "New" : "Manual") +
        "</span>"
      : "";

    return (
      '<div class="web-console__current-track">' +
      '<div class="web-console__row web-console__row--tight">' +
      '<span class="web-console__label">Current Track</span>' +
      lockChip +
      "</div>" +
      '<div class="web-console__current-track-name">' +
      currentTrackName +
      "</div>" +
      '<div class="web-console__helper">' +
      escapeHtml(status.track_source) +
      "</div>" +
      '<div class="web-console__helper">' +
      escapeHtml(distanceText) +
      "</div>" +
      "</div>"
    );
  }

  function renderNearbyTracksMarkup(status) {
    var headerAction = status.track_locked_manual
      ? '<button type="button" class="web-console__inline-action">Resume Auto</button>'
      : "";
    var nearbyMarkup = "";

    if (!status.nearby_tracks.length) {
      nearbyMarkup = '<div class="web-console__helper">No nearby alternatives right now.</div>';
    } else {
      nearbyMarkup = status.nearby_tracks
        .map(function (track) {
          return (
            '<div class="web-console__nearby-track">' +
            '<div class="web-console__nearby-track-copy">' +
            '<strong>' + escapeHtml(track.name) + "</strong>" +
            '<span class="web-console__helper">' + escapeHtml(String(Math.round(track.distance_m))) + ' m away</span>' +
            "</div>" +
            '<button type="button" class="web-console__inline-action">Use This Track</button>' +
            "</div>"
          );
        })
        .join("");
    }

    return (
      '<div class="web-console__current-track web-console__current-track--nearby">' +
      '<div class="web-console__row web-console__row--tight">' +
      '<span class="web-console__label">Nearby Tracks</span>' +
      headerAction +
      "</div>" +
      nearbyMarkup +
      "</div>"
    );
  }

  function renderTracksMarkup(tracks, status) {
    if (!Array.isArray(tracks) || tracks.length === 0) {
      return renderEmptyListItem("No tracks");
    }

    return tracks
      .map(function (track) {
        var trackId = escapeHtml((track && track.id) || "unknown");
        var trackName = escapeHtml((track && track.name) || "Unknown");
        var isCurrent = Boolean(track && (
          (status.track_id && track.id === status.track_id) ||
          (!status.track_id && track.name === status.track)
        ));
        return (
          '<li class="web-console__item web-console__item--track">' +
          '<span class="web-console__track-name-wrap">' +
          '<span class="web-console__track-name">' +
          trackName +
          " (" +
          trackId +
          ")" +
          "</span>" +
          (isCurrent
            ? '<span class="web-console__track-badge web-console__track-badge--current">Current</span>'
            : "") +
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

  function normalizeLine(source) {
    var line = source || {};
    var p1 = clonePoint(line.p1);
    var p2 = clonePoint(line.p2);
    var flipped = Boolean(line.flipped);
    var heading = hasOwn(line, "heading") ? toFiniteNumber(line.heading, null) : null;

    if (heading == null && p1 && p2) {
      heading = crossingHeadingDeg(p1, p2, flipped);
    }

    return {
      p1: p1,
      p2: p2,
      heading: heading,
      flipped: flipped,
    };
  }

  function normalizeSector(source, index) {
    var normalized = normalizeLine(source);
    normalized.id = (source && source.id) || String(index + 1);
    return normalized;
  }

  function normalizeMessage(message) {
    var normalized = message || {};
    return {
      kind: normalized.kind || "",
      text: normalized.text || "",
    };
  }

  function normalizeTrackCreationState(data) {
    var status = data.status || {};
    var source = data.track_creation || data.trackCreation || {};
    var gpsSource = source.gps || {};
    var sectors = Array.isArray(source.sectors)
      ? source.sectors.map(function (sector, index) {
          return normalizeSector(sector, index);
        })
      : [];

    var gps = {
      fix: hasOwn(gpsSource, "fix") ? Boolean(gpsSource.fix) : Boolean(status.gps_fix),
      satellites: hasOwn(gpsSource, "satellites")
        ? toFiniteNumber(gpsSource.satellites, 0)
        : toFiniteNumber(status.satellites, 0),
      lat: hasOwn(gpsSource, "lat")
        ? toFiniteNumber(gpsSource.lat, 0)
        : toFiniteNumber(status.lat, 0),
      lon: hasOwn(gpsSource, "lon")
        ? toFiniteNumber(gpsSource.lon, 0)
        : toFiniteNumber(status.lon, 0),
    };

    gps.stability = String(
      gpsSource.stability ||
      (gps.fix
        ? (gps.satellites >= 4 ? "stable" : "low_sats")
        : "no_fix")
    );

    return {
      name: String(source.name || ""),
      stage: String(source.stage || ""),
      gps: gps,
      startFinish: normalizeLine(source.start_finish || source.startFinish),
      sectors: sectors,
      sectorsExpanded: hasOwn(source, "sectorsExpanded")
        ? Boolean(source.sectorsExpanded)
        : (hasOwn(source, "sectors_expanded")
            ? Boolean(source.sectors_expanded)
            : sectors.length > 0),
      message: normalizeMessage(source.message),
      submitPending: hasOwn(source, "submitPending")
        ? Boolean(source.submitPending)
        : Boolean(source.submit_pending),
    };
  }

  function hasValidFix(draft) {
    return Boolean(draft && draft.gps && draft.gps.fix);
  }

  function getGpsStatusInfo(draft) {
    var state = (draft && draft.gps && draft.gps.stability) || "no_fix";

    if (state === "stable") {
      return {
        state: state,
        ready: true,
        statusClass: "ok",
        label: "Stable - ready to mark",
        barClass: "web-console__gps-bar--ok",
      };
    }

    if (state === "stabilizing") {
      return {
        state: state,
        ready: false,
        statusClass: "warn",
        label: "Hold still... stabilizing",
        barClass: "web-console__gps-bar--warn",
      };
    }

    if (state === "low_sats") {
      return {
        state: state,
        ready: false,
        statusClass: "err",
        label: "Waiting for better GPS",
        barClass: "web-console__gps-bar--err",
      };
    }

    return {
      state: "no_fix",
      ready: false,
      statusClass: "err",
      label: "Waiting for fix",
      barClass: "web-console__gps-bar--err",
    };
  }

  function canMarkWithCurrentGps(draft) {
    return getGpsStatusInfo(draft).ready;
  }

  function sectorIsComplete(sector) {
    return Boolean(sector && sector.p1 && sector.p2 && sector.heading != null);
  }

  function hasIncompleteSectors(draft) {
    return draft.sectors.some(function (sector) {
      return !sectorIsComplete(sector);
    });
  }

  function lineConfidenceInfo(line) {
    if (!line || !line.p1 || !line.p2) {
      return {
        ready: false,
        tier: "low",
        label: "Mark both points to review",
        lineLengthM: 0,
        shortThresholdM: 5,
        maxSpreadM: 0,
        isShort: false,
      };
    }

    var lineLengthM = haversineApproxMeters(line.p1, line.p2);
    var maxSpreadM = Math.max(pointSpreadMeters(line.p1), pointSpreadMeters(line.p2));
    var shortThresholdM = Math.max(5, 4 * maxSpreadM);
    var isShort = lineLengthM < shortThresholdM;
    var p1Tier = pointConfidenceTier(line.p1);
    var p2Tier = pointConfidenceTier(line.p2);
    var minTier = Math.min(
      p1Tier === "high" ? 3 : (p1Tier === "medium" ? 2 : 1),
      p2Tier === "high" ? 3 : (p2Tier === "medium" ? 2 : 1),
    );

    if (isShort && (p1Tier !== "high" || p2Tier !== "high")) {
      return {
        ready: false,
        tier: "low",
        label: "Short line: higher confidence required",
        lineLengthM: lineLengthM,
        shortThresholdM: shortThresholdM,
        maxSpreadM: maxSpreadM,
        isShort: true,
      };
    }

    if (minTier >= 3) {
      return {
        ready: true,
        tier: "high",
        label: "Good - ready to save",
        lineLengthM: lineLengthM,
        shortThresholdM: shortThresholdM,
        maxSpreadM: maxSpreadM,
        isShort: isShort,
      };
    }

    if (minTier >= 2) {
      return {
        ready: true,
        tier: "medium",
        label: "Acceptable - short lines may drift",
        lineLengthM: lineLengthM,
        shortThresholdM: shortThresholdM,
        maxSpreadM: maxSpreadM,
        isShort: isShort,
      };
    }

    return {
      ready: false,
      tier: "low",
      label: "Noisy - try again",
      lineLengthM: lineLengthM,
      shortThresholdM: shortThresholdM,
      maxSpreadM: maxSpreadM,
      isShort: isShort,
    };
  }

  function isReviewReady(draft) {
    return Boolean(
      draft.name.trim() &&
      draft.startFinish.p1 &&
      draft.startFinish.p2 &&
      draft.startFinish.heading != null &&
      !hasIncompleteSectors(draft)
    );
  }

  function isCreateReady(draft) {
    return Boolean(
      isReviewReady(draft) &&
      lineConfidenceInfo(draft.startFinish).ready &&
      !draft.submitPending
    );
  }

  function currentTrackCreationStage(draft) {
    if (!draft.name.trim()) {
      return "name";
    }
    if (!isReviewReady(draft)) {
      return "start_finish";
    }
    return "review";
  }

  function renderGpsBar(draft) {
    var gpsStatus = getGpsStatusInfo(draft);
    var fixOk = hasValidFix(draft);
    var coords = fixOk
      ? formatCoord({ lat: draft.gps.lat, lon: draft.gps.lon }, "--")
      : "--";

    return (
      '<div class="web-console__gps-bar ' +
      gpsStatus.barClass +
      '">' +
      '<div class="web-console__gps-status">' +
      '<div class="web-console__gps-status-main">' +
      '<span class="web-console__gps-indicator"></span>' +
      '<span class="web-console__gps-text">' +
      escapeHtml(gpsStatus.label) +
      "</span>" +
      "</div>" +
      '<span class="web-console__gps-sats">' +
      escapeHtml(String(draft.gps.satellites)) +
      " sats</span>" +
      "</div>" +
      '<div class="web-console__helper">Current position: ' +
      escapeHtml(coords) +
      "</div>" +
      "</div>"
    );
  }

  function renderMarkRow(label, point, isMarked, isDisabled, markedLabel) {
    return (
      '<div class="web-console__mark-row">' +
      '<button type="button" class="web-console__mark-button' +
      (isMarked ? " web-console__mark-button--marked" : "") +
      (isDisabled ? " web-console__mark-button--disabled" : "") +
      '"' +
      (isDisabled ? " disabled" : "") +
      ">" +
      escapeHtml(isMarked ? (markedLabel || ("✓ " + label)) : "Mark " + label) +
      "</button>" +
      '<div class="web-console__coord">' +
      escapeHtml(formatCoord(point, "Not set")) +
      "</div>" +
      "</div>"
    );
  }

  function renderHeadingBar(heading) {
    return (
      '<div class="web-console__heading-bar">' +
      '<span class="web-console__heading-value">' +
      escapeHtml(headingArrow(heading) + " " + Math.round(heading) + "° " + compassLabel(heading)) +
      "</span>" +
      '<button type="button" class="web-console__inline-action">Flip</button>' +
      "</div>"
    );
  }

  function renderStartFinishSection(draft) {
    var startFinish = draft.startFinish;
    var p1Marked = Boolean(startFinish.p1);
    var p2Marked = Boolean(startFinish.p2);
    var canMark = canMarkWithCurrentGps(draft);
    var showHeading = startFinish.heading != null && p1Marked && p2Marked;

    return (
      '<div class="web-console__track-group">' +
      '<p class="web-console__helper web-console__helper--section">Start/Finish</p>' +
      renderMarkRow("P1", startFinish.p1, p1Marked, !canMark, "Re-mark P1") +
      renderMarkRow("P2", startFinish.p2, p2Marked, !canMark || !p1Marked, "Re-mark P2") +
      (showHeading ? renderHeadingBar(startFinish.heading) : "") +
      "</div>"
    );
  }

  function renderSectorCard(sector, index, draft) {
    var p1Marked = Boolean(sector.p1);
    var p2Marked = Boolean(sector.p2);
    var canMark = canMarkWithCurrentGps(draft);
    var headingMarkup = sectorIsComplete(sector)
      ? renderHeadingBar(sector.heading)
      : '<div class="web-console__helper">Mark P1 then P2 to calculate heading.</div>';

    return (
      '<div class="web-console__sector-card">' +
      '<div class="web-console__sector-head">' +
      '<strong>Sector ' +
      escapeHtml(String(index + 1)) +
      "</strong>" +
      '<button type="button" class="web-console__inline-action">Delete</button>' +
      "</div>" +
      renderMarkRow("P1", sector.p1, p1Marked, !canMark) +
      renderMarkRow("P2", sector.p2, p2Marked, !canMark || !p1Marked) +
      headingMarkup +
      "</div>"
    );
  }

  function renderSectorList(draft) {
    if (!draft.sectorsExpanded) {
      return "";
    }

    if (!draft.sectors.length) {
      return (
        '<div class="web-console__sector-list">' +
        '<div class="web-console__helper">No sectors yet. Add up to 3 optional split lines.</div>' +
        "</div>"
      );
    }

    return (
      '<div class="web-console__sector-list">' +
      draft.sectors
        .map(function (sector, index) {
          return renderSectorCard(sector, index, draft);
        })
        .join("") +
      "</div>"
    );
  }

  function renderTrackMessage(message) {
    var className = "web-console__track-msg";
    if (message.kind === "success") {
      className += " web-console__track-msg--success";
    }
    if (message.kind === "error") {
      className += " web-console__track-msg--error";
    }

    return '<div class="' + className + '">' + escapeHtml(message.text) + "</div>";
  }

  function renderTrackCreationSteps(stage) {
    var steps = [
      { label: "1. Name", state: stage === "name" ? "active" : "done" },
      {
        label: "2. Start/Finish",
        state: stage === "start_finish" ? "active" : (stage === "review" ? "done" : ""),
      },
      { label: "3. Review", state: stage === "review" ? "active" : "" },
    ];

    return (
      '<div class="web-console__creation-steps">' +
      steps.map(function (step) {
        return (
          '<div class="web-console__creation-step' +
          (step.state ? " web-console__creation-step--" + step.state : "") +
          '">' +
          escapeHtml(step.label) +
          "</div>"
        );
      }).join("") +
      "</div>"
    );
  }

  function reviewLineLengthMeters(line) {
    if (!line || !line.p1 || !line.p2) {
      return 0;
    }
    return haversineApproxMeters(line.p1, line.p2);
  }

  function haversineApproxMeters(p1, p2) {
    var radiusM = 6371000;
    var lat1 = p1.lat * Math.PI / 180;
    var lat2 = p2.lat * Math.PI / 180;
    var dLat = (p2.lat - p1.lat) * Math.PI / 180;
    var dLon = (p2.lon - p1.lon) * Math.PI / 180;
    var a = Math.sin(dLat / 2) * Math.sin(dLat / 2) +
      Math.cos(lat1) * Math.cos(lat2) * Math.sin(dLon / 2) * Math.sin(dLon / 2);
    return radiusM * 2 * Math.atan2(Math.sqrt(a), Math.sqrt(1 - a));
  }

  function localOriginPoint(points) {
    var lat = 0;
    var lon = 0;
    points.forEach(function (point) {
      lat += point.lat;
      lon += point.lon;
    });
    return {
      lat: lat / points.length,
      lon: lon / points.length,
    };
  }

  function projectPointMeters(point, origin) {
    var lonScale = 111320 * Math.cos(origin.lat * Math.PI / 180);
    if (Math.abs(lonScale) < 0.000001) {
      lonScale = 0.000001;
    }
    return {
      x: (point.lon - origin.lon) * lonScale,
      y: (point.lat - origin.lat) * 110540,
    };
  }

  function reviewProjection(lines, width, height, padding) {
    var points = [];
    lines.forEach(function (entry) {
      points.push(entry.line.p1);
      points.push(entry.line.p2);
    });
    var origin = localOriginPoint(points);
    var bounds = {
      minX: Infinity,
      maxX: -Infinity,
      minY: Infinity,
      maxY: -Infinity,
    };

    points.forEach(function (point) {
      var projected = projectPointMeters(point, origin);
      bounds.minX = Math.min(bounds.minX, projected.x);
      bounds.maxX = Math.max(bounds.maxX, projected.x);
      bounds.minY = Math.min(bounds.minY, projected.y);
      bounds.maxY = Math.max(bounds.maxY, projected.y);
    });

    if (bounds.minX === bounds.maxX) {
      bounds.minX -= 0.5;
      bounds.maxX += 0.5;
    }
    if (bounds.minY === bounds.maxY) {
      bounds.minY -= 0.5;
      bounds.maxY += 0.5;
    }

    var innerW = width - padding * 2;
    var innerH = height - padding * 2;
    var spanX = bounds.maxX - bounds.minX;
    var spanY = bounds.maxY - bounds.minY;
    var scale = Math.min(innerW / spanX, innerH / spanY);
    var usedW = spanX * scale;
    var usedH = spanY * scale;

    return {
      origin: origin,
      bounds: bounds,
      width: width,
      height: height,
      padding: padding,
      scale: scale,
      ox: padding + (innerW - usedW) / 2,
      oy: padding + (innerH - usedH) / 2,
    };
  }

  function projectReviewPoint(point, projection) {
    var local = projectPointMeters(point, projection.origin);
    return {
      x: projection.ox + (local.x - projection.bounds.minX) * projection.scale,
      y: projection.height - projection.oy - (local.y - projection.bounds.minY) * projection.scale,
    };
  }

  function reviewScaleMeters(projection) {
    var candidates = [1, 2, 5, 10, 20, 50, 100, 200];
    var maxMeters = (projection.width - projection.padding * 2) * 0.32 / projection.scale;
    var chosen = candidates[0];
    candidates.forEach(function (candidate) {
      if (candidate <= maxMeters) {
        chosen = candidate;
      }
    });
    return chosen;
  }

  function renderReviewMetric(label, value) {
    return (
      '<div class="web-console__helper">' +
      "<strong>" + escapeHtml(label) + ":</strong> " + escapeHtml(value) +
      "</div>"
    );
  }

  function renderReviewConfidenceBadge(lineInfo) {
    return (
      '<div class="web-console__review-badge web-console__review-badge--' +
      escapeHtml(lineInfo.tier) +
      '">' +
      escapeHtml(lineInfo.label) +
      "</div>"
    );
  }

  function renderGeometryReviewMarkup(draft) {
    if (!isReviewReady(draft)) {
      return '<div class="web-console__helper">Complete the name and geometry to unlock review.</div>';
    }

    var lineInfo = lineConfidenceInfo(draft.startFinish);
    var lines = [
      { label: "Start/Finish", line: draft.startFinish, color: "#38bdf8" },
    ];
    draft.sectors.filter(sectorIsComplete).forEach(function (sector, index) {
      lines.push({ label: "S" + String(index + 1), line: sector, color: "#f59e0b" });
    });

    var width = 240;
    var height = 160;
    var padding = 18;
    var projection = reviewProjection(lines, width, height, padding);
    var scaleMeters = reviewScaleMeters(projection);
    var scalePixels = scaleMeters * projection.scale;
    var svg = '<svg viewBox="0 0 ' + width + " " + height + '" aria-label="Track geometry review">';
    svg += '<rect x="0" y="0" width="' + width + '" height="' + height + '" rx="14" fill="#0b1827" stroke="#2a3c52"></rect>';
    svg += '<g class="review-north">';
    svg += '<line x1="' + (width - 24) + '" y1="42" x2="' + (width - 24) + '" y2="20" stroke="#dbeafe" stroke-width="2.5" stroke-linecap="round"></line>';
    svg += '<polygon points="' + (width - 24) + ',14 ' + (width - 29) + ',24 ' + (width - 19) + ',24" fill="#dbeafe"></polygon>';
    svg += '<text x="' + (width - 24) + '" y="56" fill="#dbeafe" font-size="10" text-anchor="middle">North</text>';
    svg += "</g>";
    lines.forEach(function (entry) {
      var start = projectReviewPoint(entry.line.p1, projection);
      var end = projectReviewPoint(entry.line.p2, projection);
      var startRadius = Math.max(6, pointSpreadMeters(entry.line.p1) * projection.scale);
      var endRadius = Math.max(6, pointSpreadMeters(entry.line.p2) * projection.scale);
      svg += '<circle class="review-uncertainty" cx="' + start.x.toFixed(1) + '" cy="' + start.y.toFixed(1) + '" r="' + startRadius.toFixed(1) + '" fill="' + entry.color + '" fill-opacity="0.16" stroke="' + entry.color + '" stroke-opacity="0.35"></circle>';
      svg += '<circle class="review-uncertainty" cx="' + end.x.toFixed(1) + '" cy="' + end.y.toFixed(1) + '" r="' + endRadius.toFixed(1) + '" fill="' + entry.color + '" fill-opacity="0.16" stroke="' + entry.color + '" stroke-opacity="0.35"></circle>';
      svg += '<line data-review-label="' + escapeHtml(entry.label) + '" x1="' + start.x.toFixed(1) + '" y1="' + start.y.toFixed(1) + '" x2="' + end.x.toFixed(1) + '" y2="' + end.y.toFixed(1) + '" stroke="' + entry.color + '" stroke-width="4" stroke-linecap="round"></line>';
      svg += '<circle cx="' + start.x.toFixed(1) + '" cy="' + start.y.toFixed(1) + '" r="4" fill="' + entry.color + '"></circle>';
      svg += '<circle cx="' + end.x.toFixed(1) + '" cy="' + end.y.toFixed(1) + '" r="4" fill="' + entry.color + '"></circle>';
      svg += '<text x="' + ((start.x + end.x) / 2).toFixed(1) + '" y="' + ((start.y + end.y) / 2 - 8).toFixed(1) + '" fill="#dbeafe" font-size="11" text-anchor="middle">' + escapeHtml(entry.label) + "</text>";
    });
    svg += '<line class="review-scale-bar" x1="' + padding + '" y1="' + (height - padding) + '" x2="' + (padding + scalePixels).toFixed(1) + '" y2="' + (height - padding) + '" stroke="#dbeafe" stroke-width="3" stroke-linecap="round"></line>';
    svg += '<text x="' + (padding + scalePixels / 2).toFixed(1) + '" y="' + (height - padding - 8) + '" fill="#dbeafe" font-size="10" text-anchor="middle">Scale ' + escapeHtml(String(scaleMeters)) + " m</text>";
    svg += "</svg>";

    svg += '<div class="web-console__review-metrics">';
    svg += renderReviewConfidenceBadge(lineInfo);
    svg += renderReviewMetric("Line length", lineInfo.lineLengthM.toFixed(1) + " m");
    svg += renderReviewMetric(
      "Crossing",
      headingArrow(draft.startFinish.heading) +
      " " +
      Math.round(draft.startFinish.heading) +
      "° " +
      compassLabel(draft.startFinish.heading),
    );
    svg += renderReviewMetric("Scale", String(scaleMeters) + " m");
    svg += "</div>";

    if (lineInfo.isShort && !lineInfo.ready) {
      svg += '<div class="web-console__helper web-console__track-msg--error">' +
        escapeHtml(lineInfo.label + ". Re-mark with a longer line or steadier GPS.") +
        "</div>";
    }

    return svg;
  }

  function renderTrackCreationMarkup(data) {
    var draft = normalizeTrackCreationState(data || {});
    var stage = currentTrackCreationStage(draft);
    var canCreate = isCreateReady(draft);
    var showAddSectorButton = draft.sectorsExpanded;
    var createLabel = draft.submitPending ? "Creating Track..." : "Create Track";

    return (
      '<div class="web-console__track-creation">' +
      '<h3 class="web-console__subheading">Track Creation</h3>' +
      renderTrackCreationSteps(stage) +
      '<div class="web-console__creation-stage">' +
      '<p class="web-console__helper web-console__helper--section">Name</p>' +
      '<input class="web-console__input" placeholder="Track name" value="' +
      escapeHtml(draft.name) +
      '" readonly />' +
      "</div>" +
      (draft.name.trim()
        ? (
          '<div class="web-console__creation-stage">' +
          '<p class="web-console__helper web-console__helper--section">Start/Finish</p>' +
          '<div class="web-console__helper">Name the track, then mark start/finish only when GPS is stable.</div>' +
          renderGpsBar(draft) +
          renderStartFinishSection(draft) +
          '<button type="button" class="web-console__collapse-toggle">' +
          escapeHtml(draft.sectorsExpanded ? "Hide Sector Splits (optional)" : "Show Sector Splits (optional)") +
          "</button>" +
          renderSectorList(draft) +
          (showAddSectorButton
            ? '<button type="button" class="web-console__secondary-button' +
              (draft.sectors.length >= 3 ? " web-console__secondary-button--disabled" : "") +
              '"' +
              (draft.sectors.length >= 3 ? " disabled" : "") +
              '>+ Add Sector</button>'
            : "") +
          "</div>"
        )
        : "") +
      (stage === "review"
        ? (
          '<div class="web-console__creation-stage">' +
          '<p class="web-console__helper web-console__helper--section">Review</p>' +
          '<div class="web-console__current-track web-console__current-track--nearby review-panel">' +
          renderGeometryReviewMarkup(draft) +
          "</div>" +
          '<div class="web-console__helper">A new track becomes current immediately after creation. Do not close or refresh this page during track creation.</div>' +
          '<button type="button" class="web-console__primary-button' +
          (canCreate ? "" : " web-console__primary-button--disabled") +
          '"' +
          (canCreate ? "" : " disabled") +
          ">" +
          escapeHtml(createLabel) +
          "</button>" +
          "</div>"
        )
        : "") +
      renderTrackMessage(draft.message) +
      "</div>"
    );
  }

  function renderWebConsoleMarkup(scenario) {
    var data = scenario || {};
    var tracks = Array.isArray(data.tracks) ? data.tracks : [];
    var status = normalizeStatus(data.status || {}, tracks, data);
    var settings = data.settings || {};
    var gpsFix = status.gps_fix ? "Yes" : "No";
    var recordingState = status.recording ? "REC" : "Idle";
    var currentLap = status.current_lap;
    var satellites = status.satellites;
    var bestLap = formatBestLap(status.best_lap_ms);
    var trackName = status.track ? escapeHtml(status.track) : "None";
    var recordingCta = getRecordingCtaConfig(status);
    var ssid = escapeHtml(settings.wifi_ssid || "");
    var wifiPass = escapeHtml(settings.wifi_pass || "");
    var brightness = Number(settings.brightness);
    var sessions = Array.isArray(data.sessions) ? data.sessions : [];

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
      (recordingCta.active ? " web-console__recording-cta--active" : "") +
      '"' +
      (recordingCta.disabled ? " disabled" : "") +
      ">" +
      recordingCta.label +
      "</button>" +
      '<div class="web-console__recording-reason">' +
      escapeHtml(status.recording_cta_reason) +
      "</div>" +
      "</div>" +
      '<div class="web-console__card" data-card="sessions">' +
      "<h2>Sessions</h2>" +
      '<ul class="web-console__list">' +
      renderSessionsMarkup(sessions) +
      "</ul>" +
      "</div>" +
      '<div class="web-console__card" data-card="tracks">' +
      "<h2>Tracks</h2>" +
      renderCurrentTrackMarkup(status) +
      renderNearbyTracksMarkup(status) +
      '<ul class="web-console__list">' +
      renderTracksMarkup(tracks, status) +
      "</ul>" +
      renderTrackCreationMarkup(data) +
      "</div>" +
      '<div class="web-console__card" data-card="settings">' +
      '<button type="button" class="web-console__secondary-button web-console__advanced-toggle" disabled>Advanced</button>' +
      '<div class="web-console__hint">Wi-Fi credential changes still require a reboot after saving.</div>' +
      "</div>" +
      "</div>"
    );
  }

  return {
    renderWebConsoleMarkup: renderWebConsoleMarkup,
  };
});
