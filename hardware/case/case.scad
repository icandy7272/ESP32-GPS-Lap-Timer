// =============================================================
// ⚠️  REFERENCE DOCUMENT, NOT PRIMARY CAD ⚠️
//
// As of 2026-05-09, the primary CAD tool for the enclosure is
// Fusion 360 (macOS Gatekeeper blocks the 2021.01 OpenSCAD build
// available via Homebrew Cask, and there is no current official
// OpenSCAD macOS package).  The Fusion 360 source of truth lives
// at hardware/case/case_v1.f3d once you build the model.
//
// This .scad file is kept as:
//   1. The canonical measurement worksheet — every dimension that
//      needs to be on the bench with a caliper is listed in §1
//      with a comment explaining what to measure.  The same data
//      is mirrored in parameters.md / parameters.csv for Fusion.
//   2. The design intent reference — why the case is shaped this
//      way (GPS face up, battery low, BOOT/RST pinholes, etc.).
//   3. Future-proofing — if OpenSCAD ever becomes usable on macOS
//      again, or you want to batch-generate variants, the model
//      is one F5 away.
//
// When the Fusion 360 parameters and these constants disagree,
// FUSION 360 IS THE TRUTH.  Update §1 here to match, but do not
// rely on this file alone.
// =============================================================
//
// ESP32-S3 GPS Lap Timer — enclosure parametric model (skeleton)
//
// Project:  ESP32_track_GPS
// Created:  2026-05-09
// Status:   reference doc; primary CAD is Fusion 360 (see README).
//
// Design intent (see docs/PRD.md §设备物理结构 and
// docs/WIRING.md §组装与振动加固):
//   - GPS antenna face up to sky (top lid is plastic only,
//     no metal sticker, no carbon fibre, no conductive paint).
//   - TFT face toward the driver (front).
//   - 18650 battery pack at the bottom of the stack for a low CG.
//   - USB-C charge port and the two 12mm metal panel buttons
//     accessible from the side.
//   - Bottom + top split, fastened with 4× M2 self-tap screws so
//     the case can be reopened during early-life debugging.
//
// Workflow:
//   1. Measure your actual hardware with a digital caliper.
//      Fill every "MEASURED_*" constant in §1 below — the file
//      will render placeholder geometry until you do.
//   2. Open this file in OpenSCAD (`brew install openscad` on macOS,
//      or download from openscad.org).  Press F5 to preview.
//   3. Iterate the dimensions, watch the preview update live.
//   4. Press F6 to render, File → Export STL.
//   5. Print v1 in PLA at 30% infill, 0.2mm layer, no supports if
//      possible (see hardware/case/README.md for slicer settings).
//   6. Test fit, log issues at the bottom of this file (FIT NOTES),
//      tweak, reprint.
//   7. Once stable, switch material to PETG for the final build.
// =============================================================

// =============================================================
// §1 MEASURED DIMENSIONS — FILL IN BEFORE PRINTING
// =============================================================
// Units are millimeters.  Measure each item three times with a
// digital caliper and record the LARGEST reading (FDM tolerances
// favour over- not under-sizing).  All zeros are placeholders;
// the model deliberately renders weird until they are filled.

// ---------- TFT panel (3.2" ZJY320S0800TG02) -----------------
// Spec sheet says PCB outline is 89.4 × 56.0 × 3.75mm and visible
// glass is 64.8 × 48.6mm, but YOUR module may differ slightly.
TFT_PCB_W              = 89.4;  // PCB long edge
TFT_PCB_H              = 56.0;  // PCB short edge
TFT_PCB_THICKNESS      = 1.6;   // bare PCB thickness
TFT_BACK_HEIGHT        = 8.0;   // tallest component on the BACK side
                                // (backlight inverter / inductor / cap).
                                // Measure from PCB top surface to the
                                // top of the tallest part.
TFT_VISIBLE_W          = 64.8;  // visible glass long edge
TFT_VISIBLE_H          = 48.6;  // visible glass short edge
TFT_VISIBLE_OFFSET_X   = 12.3;  // visible glass left edge offset from PCB
                                // left edge.  (89.4 - 64.8) / 2 ≈ 12.3
                                // if centred — verify with calipers.
TFT_VISIBLE_OFFSET_Y   = 3.7;   // visible glass top offset from PCB top
TFT_SCREW_HOLE_DIA     = 2.6;   // ~M2.5; widen +0.4 for clearance
TFT_SCREW_HOLE_DX      = 83.4;  // hole-to-hole spacing along long edge
TFT_SCREW_HOLE_DY      = 50.0;  // hole-to-hole spacing along short edge

// ---------- ESP32-S3 DevKit + perfboard ----------------------
PCB_W                  = 70.0;  // your perfboard long edge
PCB_H                  = 50.0;  // your perfboard short edge
PCB_THICKNESS          = 1.6;
PCB_USB_C_X            = 17.0;  // USB-C centre X from PCB left edge
PCB_USB_C_Z_OFFSET     = 1.5;   // USB-C centre height above PCB top
PCB_BOOT_BTN_X         = 25.0;  // BOOT button centre X from PCB left
PCB_RST_BTN_X          = 32.0;  // RST button centre X from PCB left
PCB_BOOT_RST_Y         = 6.0;   // both buttons sit at this Y from edge
PCB_BTN_TOP_Z          = 3.0;   // button cap height above PCB

// ---------- 18650 battery + USB-C charger daughter board -----
BATT_W                 = 70.0;  // pack long edge
BATT_H                 = 38.0;  // 2P pack — measure your actual
BATT_D                 = 19.0;  // pack depth
USBC_DAUGHTER_W        = 25.0;  // charge port board outline
USBC_DAUGHTER_H        = 15.0;
USBC_PORT_W            = 9.0;   // USB-C plug opening width
USBC_PORT_H            = 3.5;   // USB-C plug opening height

// ---------- BK-880 GPS module --------------------------------
GPS_MODULE_W           = 28.0;
GPS_MODULE_H           = 28.0;
GPS_PATCH_HEIGHT       = 7.0;   // ceramic patch height above its PCB
GPS_PATCH_HEADROOM     = 3.0;   // air gap above patch (RF clearance)

// ---------- 12mm panel-mount metal buttons -------------------
// Standard M12 latching / momentary metal buttons take a
// 12mm-thread, 13mm-panel-hole.  The barrel sticks out the back
// by typically 22-25mm including the connector tab.
BTN_PANEL_HOLE_DIA     = 13.0;
BTN_BACKSIDE_DEPTH     = 25.0;  // free space needed behind button face

// =============================================================
// §2 DESIGN PARAMETERS — tweak as needed, no measurement
// =============================================================
WALL                   = 2.0;   // outer shell wall thickness
LID_LIP                = 4.0;   // overlap depth between top lid and base
LID_LIP_GAP            = 0.3;   // sliding clearance at the lip
PRINT_TOLERANCE        = 0.4;   // generic FDM hole oversize
SCREW_BOSS_DIA         = 5.5;   // outer diameter of M2 screw post
SCREW_HOLE_DIA         = 1.7;   // M2 self-tap pilot hole
PCB_STANDOFF_H         = 4.0;   // gap between PCB and inner case floor
TFT_BEZEL_INSET        = 1.0;   // bezel overhang past the visible edge
$fn                    = 48;    // arc segment count for cylinders

// =============================================================
// §3 DERIVED DIMENSIONS — auto-computed, do not edit
// =============================================================
// Internal usable cavity, inferred from the largest of each
// stacked layer.  Adjust if your physical stackup differs.
inner_w = max(TFT_PCB_W, PCB_W, BATT_W) + 2;
inner_h = max(TFT_PCB_H, PCB_H, BATT_H) + 2;
inner_d = TFT_PCB_THICKNESS + TFT_BACK_HEIGHT
        + PCB_STANDOFF_H + PCB_THICKNESS
        + BATT_D
        + GPS_PATCH_HEIGHT + GPS_PATCH_HEADROOM;

case_w = inner_w + 2 * WALL;
case_h = inner_h + 2 * WALL;
case_d = inner_d + 2 * WALL;

// =============================================================
// §4 MODULES
// =============================================================

// Outer prism with a hollow interior.  Used as the base for both
// the bottom case and the top lid.
module shell(width, height, depth, wall) {
    difference() {
        cube([width, height, depth]);
        translate([wall, wall, wall])
            cube([width - 2*wall, height - 2*wall, depth - wall + 0.01]);
    }
}

// TFT viewable window — cut on the front face of the bottom case.
module tft_window() {
    inner_x = (inner_w - TFT_VISIBLE_W) / 2;
    inner_y = (inner_h - TFT_VISIBLE_H) / 2;
    translate([WALL + inner_x - TFT_BEZEL_INSET,
               WALL + inner_y - TFT_BEZEL_INSET,
               -0.01])
        cube([TFT_VISIBLE_W + 2*TFT_BEZEL_INSET,
              TFT_VISIBLE_H + 2*TFT_BEZEL_INSET,
              WALL + 0.02]);
}

// Side cutout for USB-C charge port (battery board side).
module usbc_cutout() {
    translate([-0.01,
               case_h / 2 - USBC_PORT_W / 2,
               case_d / 2 - USBC_PORT_H / 2])
        cube([WALL + 0.02, USBC_PORT_W, USBC_PORT_H]);
}

// Two 12mm panel-mount buttons on the side.
module button_holes() {
    button_y = [case_h * 0.30, case_h * 0.65];
    for (y = button_y) {
        translate([case_w + 0.01, y, case_d / 2])
            rotate([0, -90, 0])
                cylinder(d = BTN_PANEL_HOLE_DIA + PRINT_TOLERANCE,
                         h = WALL + 0.02);
    }
}

// Pin holes through the back wall for BOOT / RST access (poke
// with a paperclip).  Stays plugged with a silicone bung when
// not in use.
module boot_rst_pinholes() {
    z_centre = case_d / 2;
    for (x = [PCB_BOOT_BTN_X, PCB_RST_BTN_X]) {
        translate([WALL + x, case_h - WALL + 0.01, z_centre])
            rotate([90, 0, 0])
                cylinder(d = 1.8, h = WALL + 0.02);
    }
}

// Cylindrical screw post with a self-tap pilot hole down the
// middle.  Place at each of 4 corners for the lid screws.
module screw_boss(height) {
    difference() {
        cylinder(d = SCREW_BOSS_DIA, h = height);
        translate([0, 0, -0.01])
            cylinder(d = SCREW_HOLE_DIA, h = height + 0.02);
    }
}

module corner_bosses(height) {
    inset = SCREW_BOSS_DIA / 2 + 1;
    translate([inset, inset, WALL])              screw_boss(height);
    translate([case_w - inset, inset, WALL])     screw_boss(height);
    translate([inset, case_h - inset, WALL])     screw_boss(height);
    translate([case_w - inset, case_h - inset, WALL])
                                                 screw_boss(height);
}

// =============================================================
// §5 ASSEMBLIES
// =============================================================

module bottom_case() {
    difference() {
        union() {
            shell(case_w, case_h, case_d * 0.7, WALL);
            corner_bosses(case_d * 0.6);
        }
        tft_window();
        usbc_cutout();
        button_holes();
        boot_rst_pinholes();
    }
}

module top_lid() {
    // Solid plastic top — GPS antenna face.
    // Lip slides into bottom case for a snug seal.
    lid_d = case_d * 0.3;
    union() {
        difference() {
            cube([case_w, case_h, lid_d]);
            // Inset for the lip
            translate([WALL + LID_LIP_GAP,
                       WALL + LID_LIP_GAP,
                       lid_d - LID_LIP])
                cube([case_w - 2*(WALL + LID_LIP_GAP),
                      case_h - 2*(WALL + LID_LIP_GAP),
                      LID_LIP + 0.01]);
        }
        // 4 lid screw clearance holes (handled later when you
        // align with the bosses; for now just a placeholder).
    }
}

// =============================================================
// §6 PREVIEW
// =============================================================
// F5 to preview, F6 to render, then File → Export STL.
// The lid is shown floating above the base for inspection;
// they print as separate STLs.

bottom_case();
translate([0, 0, case_d * 0.7 + 10]) top_lid();

// =============================================================
// §7 FIT NOTES — append after each test print
// =============================================================
//
// v1 (date YYYY-MM-DD): _______________________________________
//   Issue:
//   Fix:
//
// v2 (date YYYY-MM-DD): _______________________________________
//   Issue:
//   Fix:
//
// =============================================================
