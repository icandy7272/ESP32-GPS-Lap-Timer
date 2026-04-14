#include "web_ui_internal.h"

String build_web_ui_script_section() {
    String script;
    script.reserve(32768);
    script += "<script>\n";
    script += build_web_ui_script_core_fragment();
    script += build_web_ui_script_dashboard_fragment();
    script += build_web_ui_script_track_creation_review_fragment();
    script += build_web_ui_script_track_creation_fragment();
    script += "</script>";
    return script;
}
