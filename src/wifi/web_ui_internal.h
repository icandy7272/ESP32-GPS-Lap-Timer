#pragma once

#include <Arduino.h>

String build_web_ui_head_section();
String build_web_ui_body_section();
String build_web_ui_script_section();

const char* build_web_ui_script_core_fragment();
const char* build_web_ui_script_dashboard_fragment();
const char* build_web_ui_script_track_creation_review_fragment();
const char* build_web_ui_script_track_creation_fragment();
