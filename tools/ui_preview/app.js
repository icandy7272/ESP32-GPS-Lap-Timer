/* Bootstrap for shared-scenario controls and safe placeholder panel rendering. */
(function () {
  let initialized = false;
  let scenariosApi = null;

  const state = {
    activeScenarioId: null,
  };

  function isValidScenariosApi(api) {
    if (!api || !Array.isArray(api.SCENARIOS) || api.SCENARIOS.length === 0) {
      return false;
    }
    if (typeof api.DEFAULT_SCENARIO_ID !== "string" || typeof api.getScenarioById !== "function") {
      return false;
    }

    const defaultScenario = api.getScenarioById(api.DEFAULT_SCENARIO_ID);
    return Boolean(defaultScenario && typeof defaultScenario.id === "string");
  }

  function resolveScenariosApiOrNull() {
    const api = window.UiPreviewScenarios;
    if (isValidScenariosApi(api)) {
      return api;
    }

    return null;
  }

  function renderUnavailableState() {
    const controlsRoot = document.getElementById("scenario-controls-root");
    const webRoot = document.getElementById("web-console-root");
    const deviceRoot = document.getElementById("device-screen-root");

    if (controlsRoot) {
      controlsRoot.innerHTML =
        '<h2 class="scenario-controls__title">Scenario Controls</h2>' +
        '<p class="scenario-controls__description">Scenario registry unavailable. Check scenarios.js load status.</p>';
    }
    if (webRoot) {
      webRoot.innerHTML =
        '<div class="preview-fallback"><h2>Web Console Preview</h2><p>Scenario data unavailable.</p></div>';
    }
    if (deviceRoot) {
      deviceRoot.innerHTML =
        '<div class="preview-fallback"><h2>Device Screen Preview</h2><p>Scenario data unavailable.</p></div>';
    }
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
    description.textContent =
      "Tooling-only controls. Select a shared firmware scenario to refresh both previews.";

    const activeScenario = scenariosApi.getScenarioById(state.activeScenarioId);
    const activeLabel = document.createElement("p");
    activeLabel.className = "scenario-controls__active";
    activeLabel.textContent = "Active scenario: " + (activeScenario.label || activeScenario.id);

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
    controlsRoot.appendChild(activeLabel);
    controlsRoot.appendChild(buttonsWrap);
  }

  function resolveWebConsoleRenderer() {
    const api = window.UiPreviewWebConsole;
    if (!api || typeof api.renderWebConsoleMarkup !== "function") {
      return null;
    }

    return api.renderWebConsoleMarkup;
  }

  function renderWebConsolePreview(activeScenario) {
    const webRoot = document.getElementById("web-console-root");
    if (!webRoot) {
      return;
    }

    const renderer = resolveWebConsoleRenderer();

    if (typeof renderer === "function") {
      const markup = renderer(activeScenario);
      if (typeof markup === "string" && markup.trim().length > 0) {
        webRoot.innerHTML = markup;
        return;
      }
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
      const markup = renderer(activeScenario);
      if (typeof markup === "string" && markup.trim().length > 0) {
        deviceRoot.innerHTML = markup;
        return;
      }
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
    if (!scenariosApi) {
      state.activeScenarioId = null;
      renderUnavailableState();
      return;
    }

    const activeScenario = scenariosApi.getScenarioById(state.activeScenarioId);
    if (!activeScenario || typeof activeScenario.id !== "string") {
      state.activeScenarioId = null;
      renderUnavailableState();
      return;
    }

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

    scenariosApi = resolveScenariosApiOrNull();
    if (scenariosApi) {
      state.activeScenarioId = scenariosApi.DEFAULT_SCENARIO_ID;
    }

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
