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

    server.send(200, "text/html", build_dashboard_html());
}
