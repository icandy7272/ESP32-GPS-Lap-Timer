// ============================================================
// GET / — HTML dashboard
// Embedded dashboard UI (HTML/CSS/JS) + root handler.
// ============================================================

#include "wifi_internal.h"
#include "web_ui_internal.h"

#include <WebServer.h>
#include <string.h>

namespace {

constexpr size_t kWebUiChunkBytes = 2048;

void send_dashboard_content(const char* content, size_t len) {
    while (len > 0) {
        size_t chunk_len = len > kWebUiChunkBytes ? kWebUiChunkBytes : len;
        server.sendContent(content, chunk_len);
        content += chunk_len;
        len -= chunk_len;
    }
}

void send_dashboard_content(const char* content) {
    send_dashboard_content(content, strlen(content));
}

void send_dashboard_content(const String& content) {
    send_dashboard_content(content.c_str(), content.length());
}

void send_web_ui_script_section_streamed() {
    send_dashboard_content("<script>\n");
    send_dashboard_content(build_web_ui_script_core_fragment());
    send_dashboard_content(build_web_ui_script_dashboard_fragment());
    send_dashboard_content(build_web_ui_script_track_creation_review_fragment());
    send_dashboard_content(build_web_ui_script_track_creation_fragment());
    send_dashboard_content("</script>");
}

void send_dashboard_html_streamed() {
    send_dashboard_content("<!DOCTYPE html><html lang=\"en\">");
    send_dashboard_content(build_web_ui_head_section());
    send_dashboard_content(build_web_ui_body_section());
    send_web_ui_script_section_streamed();
    send_dashboard_content("</html>");
    server.sendContent("", 0);
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
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "text/html", "");
    send_dashboard_html_streamed();
}
