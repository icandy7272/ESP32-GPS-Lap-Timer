/* Bootstrap for shared-scenario controls and safe placeholder panel rendering. */
(function () {
  const FALLBACK_SCENARIO_ID = "ready-to-drive";
  let initialized = false;
  let scenariosApi = null;

  const state = {
    activeScenarioId: FALLBACK_SCENARIO_ID,
  };

  function createFallbackScenarioApi() {
    const readyScenario = {
      id: FALLBACK_SCENARIO_ID,
      label: "Ready To Drive",
      status: { gps_fix: true, satellites: 10, recording: false, track: "Ningbo Kart Center" },
      device: { screen: "driving", off_track: false },
    };

    return {
      SCENARIOS: [readyScenario],
      DEFAULT_SCENARIO_ID: FALLBACK_SCENARIO_ID,
      getScenarioById: function () {
        return readyScenario;
      },
    };
  }

  function resolveScenariosApi() {
    const api = window.UiPreviewScenarios;
    if (api && Array.isArray(api.SCENARIOS) && typeof api.getScenarioById === "function") {
      return api;
    }

    return createFallbackScenarioApi();
  }

  function renderScenarioControls() {
    const controlsRoot = document.getElementById("scenario-controls-root");
    if (!controlsRoot) {
      return;
    }

    const scenarios = scenariosApi.SCENARIOS || [];
    controlsRoot.innerHTML = "";

    const heading = document.createElement("h2");
    heading.className = "scenario-controls__title";
    heading.textContent = "Scenario Controls";

    const description = document.createElement("p");
    description.className = "scenario-controls__description";
    description.textContent = "Tooling-only controls. Select a scenario to refresh both previews.";

    const buttonsWrap = document.createElement("div");
    buttonsWrap.className = "scenario-controls__buttons";

    for (const scenario of scenarios) {
      const button = document.createElement("button");
      button.type = "button";
      button.className = "scenario-button";
      button.textContent = scenario.label || scenario.id;
      button.dataset.scenarioId = scenario.id;

      if (scenario.id === state.activeScenarioId) {
        button.classList.add("is-active");
      }

      button.addEventListener("click", function () {
        if (state.activeScenarioId === scenario.id) {
          return;
        }

        state.activeScenarioId = scenariosApi.getScenarioById(scenario.id).id;
        renderAll();
      });

      buttonsWrap.appendChild(button);
    }

    controlsRoot.appendChild(heading);
    controlsRoot.appendChild(description);
    controlsRoot.appendChild(buttonsWrap);
  }

  function renderWebConsolePreview(activeScenario) {
    const webRoot = document.getElementById("web-console-root");
    if (!webRoot) {
      return;
    }

    const renderer =
      window.UiPreviewWebConsole && window.UiPreviewWebConsole.renderWebConsoleMarkup;

    if (typeof renderer === "function") {
      webRoot.innerHTML = renderer(activeScenario);
      return;
    }

    const gpsState = activeScenario.status && activeScenario.status.gps_fix ? "GPS fixed" : "GPS searching";
    webRoot.innerHTML =
      '<div class="preview-fallback">' +
      '<h2>Web Console Preview</h2>' +
      "<p>Renderer placeholder active.</p>" +
      '<p><strong>Scenario:</strong> ' +
      (activeScenario.label || activeScenario.id) +
      "</p>" +
      '<p><strong>Status:</strong> ' +
      gpsState +
      "</p>" +
      "</div>";
  }

  function renderDeviceScreenPreview(activeScenario) {
    const deviceRoot = document.getElementById("device-screen-root");
    if (!deviceRoot) {
      return;
    }

    const renderer =
      window.UiPreviewDeviceScreen && window.UiPreviewDeviceScreen.renderDeviceScreenMarkup;

    if (typeof renderer === "function") {
      deviceRoot.innerHTML = renderer(activeScenario);
      return;
    }

    const screen = (activeScenario.device && activeScenario.device.screen) || "unknown";
    deviceRoot.innerHTML =
      '<div class="preview-fallback">' +
      '<h2>Device Screen Preview</h2>' +
      "<p>Renderer placeholder active.</p>" +
      '<p><strong>Scenario:</strong> ' +
      (activeScenario.label || activeScenario.id) +
      "</p>" +
      '<p><strong>Screen:</strong> ' +
      screen +
      "</p>" +
      "</div>";
  }

  function renderAll() {
    const activeScenario = scenariosApi.getScenarioById(state.activeScenarioId);
    state.activeScenarioId = activeScenario.id;
    renderScenarioControls();
    renderWebConsolePreview(activeScenario);
    renderDeviceScreenPreview(activeScenario);
  }

  function init() {
    if (initialized) {
      return;
    }
    initialized = true;

    scenariosApi = resolveScenariosApi();
    state.activeScenarioId = scenariosApi.getScenarioById(
      scenariosApi.DEFAULT_SCENARIO_ID || FALLBACK_SCENARIO_ID,
    ).id;

    renderAll();
  }

  window.UiPreviewApp = window.UiPreviewApp || {};
  window.UiPreviewApp.init = init;
  window.UiPreviewApp.getActiveScenarioId = function () {
    return state.activeScenarioId;
  };

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", init);
  } else {
    init();
  }
})();
