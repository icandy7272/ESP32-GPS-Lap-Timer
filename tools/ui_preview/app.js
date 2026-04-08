/* Safe bootstrap stub that registers window.UiPreviewApp.init. */
(function () {
  function init() {
    console.log("UI Preview Workbench stub initialized");
  }

  window.UiPreviewApp = window.UiPreviewApp || {};
  window.UiPreviewApp.init = init;

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", init);
  } else {
    init();
  }
})();
