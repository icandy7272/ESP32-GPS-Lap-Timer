/* Entry stub for the UI Preview Workbench that exposes a safe init hook. */
(() => {
  const init = () => {
    console.log("UI Preview Workbench stub init");
  };

  const install = () => {
    window.UiPreviewApp = window.UiPreviewApp || {};
    window.UiPreviewApp.init = init;
  };

  install();

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", init);
  } else {
    init();
  }
})();
