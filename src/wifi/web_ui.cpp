// ============================================================
// GET / — HTML dashboard
// Embedded dashboard UI (HTML/CSS/JS) + root handler.
// ============================================================

#include "wifi_internal.h"
#include "web_ui_internal.h"

#include <WebServer.h>

namespace {

String build_dashboard_html() {
    String html;
    html.reserve(4096);
    html += "<!DOCTYPE html><html lang=\"en\">";
    html += build_web_ui_head_section();
    html += build_web_ui_body_section();
    html += build_web_ui_script_section();
    html += "</html>";
    return html;
}

}  // namespace

void handle_root() {
    if (is_throttled()) {
        server.send(503, "text/plain", "Recording in progress, try later");
        return;
    }

    // Dashboard HTML is regenerated on every firmware flash and embeds
    // both UI markup and inline JS feature flags (e.g.
    // REPEATABILITY_CHECK_ENABLED).  Without explicit cache-bust
    // headers, mobile browsers happily keep serving a stale copy of
    // the whole page — so a user who had the dashboard open across a
    // firmware update would silently keep seeing the old feature set.
    // The triple header covers the cache directives that various
    // browsers actually honour.
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    server.sendHeader("Pragma", "no-cache");
    server.sendHeader("Expires", "0");
    server.send(200, "text/html", build_dashboard_html());
}
