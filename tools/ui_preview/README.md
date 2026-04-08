# UI Preview Workbench

This directory holds the static shell for the UI Preview Workbench described in the plan. It hosts the web console, device screen, and scenario control roots so the rest of the stack can plug in their own renderers without needing a full firmware build.

## Running locally

```sh
cd tools/ui_preview
python3 -m http.server 8000
```

Then visit `http://localhost:8000/index.html` in a browser to see the neutral preview shell.

## References

- `src/wifi_server.cpp`
- `src/display.cpp`

## Manual verification

- Start `python3 -m http.server 8000` while inside `tools/ui_preview`.
- Visit `http://localhost:8000/index.html` and confirm the shell renders without missing-file errors.
- Open DevTools and ensure `window.UiPreviewApp` exists before other scripts execute.
