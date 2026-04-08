/* Entry stub for the UI Preview Workbench. */
const bootstrapWorkbench = () => {
  console.log("UI Preview Workbench bootstrap stub");
};

if (document.readyState === "loading") {
  document.addEventListener("DOMContentLoaded", bootstrapWorkbench);
} else {
  bootstrapWorkbench();
}
