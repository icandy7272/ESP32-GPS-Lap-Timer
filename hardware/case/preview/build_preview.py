"""
ESP32 GPS Lap Timer enclosure preview v2 (sandwich layout).

Topology (revealed 2026-05-11 via physical photos):

         ↑ +Y (sky)
         ┌──── GPS module (cuts +Y face) ────┐
         │      protrudes ~15 mm from PCB    │
         │                                   │
   +Z    │ ┌─────────────────────────────┐   │
  (driver)│ │ TFT glass + LCM            │   │
   ←──   │ │ TFT PCB                    │   │  ←── -Z (back, solid)
         │ │ TFT right-angle header     │   │
         │ │   ↑ SD card module nestled │   │
         │ │ Perfboard                  │   │
         │ │ ESP32 right-angle headers  │   │
         │ │ ESP32 DevKit + 18650×2     │   │
         │ │ Back wall                  │   │
         │ └─────────────────────────────┘   │
         └───────────────────────────────────┘
            ↓ -Y (ground)

X axis = left/right (TFT long edge)
Y axis = bottom/top (TFT short edge + GPS protrusion)
Z axis = back/front (sandwich stack depth)
"""

import cadquery as cq
from pathlib import Path

# ============================================================
# §1.1 TFT — 12 项 (实测 2026-05-11)
# ============================================================
TFT_PCB_W = 89.45
TFT_PCB_H = 56.01
TFT_PCB_THICKNESS = 1.17
TFT_BACK_HEIGHT = 8.28          # 排针根部（仅参考；实际 TFT 通过 TFT_HEADER_STANDOFF 架空）
TFT_FRONT_HEIGHT = 3.83
TFT_VISIBLE_W = 67.53
TFT_VISIBLE_H = 52.07
TFT_VISIBLE_OFFSET_X = 7.5
TFT_VISIBLE_OFFSET_Y = 2.37
TFT_SCREW_HOLE_DIA = 3.01
TFT_SCREW_HOLE_DX = 83.42
TFT_SCREW_HOLE_DY = 50.07

# ============================================================
# §1.2-1.5 — 占位 (待测)
# ============================================================
PCB_W = 100.0          # 洞洞板长边 (实测 2026-05-11)
PCB_H = 70.0           # 洞洞板短边 (实测 2026-05-11)
PCB_THICKNESS = 1.5    # 洞洞板厚度 (实测 2026-05-11)

# CK18650-1234S-NP 5V 5600mAh 电池组（整体封装尺寸）
BATT_LENGTH_Y = 71.07  # 沿 Y 方向（长轴）(实测 2026-05-11)
BATT_WIDTH_X = 40.78   # 沿 X 方向（短轴）(实测 2026-05-11)
BATT_DEPTH_Z = 19.50   # 沿 Z 方向（朝洞洞板背面外凸出）(实测 2026-05-11)

# ESP32-S3 WROOM-1 DevKit (竖放，长边沿 Y)
ESP32_LENGTH_Y = 65.0
ESP32_WIDTH_X = 25.0

USBC_PORT_W = 9.0
USBC_PORT_H = 3.5
GPS_MODULE_W = 28.0          # 实测 2026-05-11 (与 spec 一致)
GPS_MODULE_H = 28.0          # 实测 2026-05-11
GPS_PATCH_HEIGHT = 11.0      # 实测 2026-05-11 (天线 + 金属罩总高)
BTN_PANEL_HOLE_DIA = 13.0
BTN_BACKSIDE_DEPTH = 13.0  # M12 按键背深（实测 2026-05-11：螺纹 10.92mm + 螺母 ~2mm；线材侧出）
SD_PROTRUSION_X = 8.0      # SD 卡模块凸出洞洞板 +X 边缘的距离（占位）
SD_MODULE_W = 30.0         # SD 卡模块沿 X 方向尺寸
SD_MODULE_H_Y = 15.0       # SD 卡模块沿 Y 方向尺寸

# ============================================================
# §1.6 NEW — 母座架空高度 / 模块凸出 (占位)
# ============================================================
# TFT 在洞洞板上的位置（非居中，实测 2026-05-11）
TFT_OFFSET_LEFT = 8.56           # TFT PCB 左边沿到洞洞板左边沿
TFT_OFFSET_TOP = 12.9            # TFT PCB 上边沿到洞洞板上边沿 (GPS 那一边)

TFT_HEADER_STANDOFF = 8.0        # TFT 直角母座架空（让 TFT 离开洞洞板正面）
ESP32_HEADER_STANDOFF = 8.0      # ESP32 双直角母座架空
ESP32_DEVKIT_HEIGHT = 4.0        # ESP32 DevKit PCB + 元件凸起
SD_MODULE_HEIGHT = 5.0           # SD 卡模块本体高度（夹在 TFT_HEADER_STANDOFF 内）
GPS_PROTRUSION_Y = 14.3          # GPS 模块凸出洞洞板 +Y 边缘的距离 (实测 2026-05-11)

# ============================================================
# §2 设计参数
# ============================================================
WALL = 2.0
PRINT_TOLERANCE = 0.4

# ============================================================
# §3 派生量 (sandwich)
# ============================================================
backside_h = max(BATT_DEPTH_Z, ESP32_HEADER_STANDOFF + ESP32_DEVKIT_HEIGHT)
frontside_h = TFT_HEADER_STANDOFF + TFT_PCB_THICKNESS + TFT_FRONT_HEIGHT
inner_z_calculated = backside_h + PCB_THICKNESS + frontside_h
# 整机实测总厚度（2026-05-11 沿 Z 方向，从 TFT 玻璃顶到电池底）
STACK_MEASURED_Z = 39.0
# 取较大者 + 1mm 余量
inner_z = max(inner_z_calculated, STACK_MEASURED_Z) + 1
# Y 方向：max(TFT 短边, 洞洞板 H, 电池长) + GPS 凸出 + 余
inner_y = max(TFT_PCB_H, PCB_H, BATT_LENGTH_Y) + GPS_PROTRUSION_Y + 2
# X 方向：max(TFT 长边, 洞洞板 W) + 余
inner_x = max(TFT_PCB_W, PCB_W) + 2

case_x = inner_x + 2 * WALL
case_y = inner_y + 2 * WALL
case_z = inner_z + 2 * WALL

# 洞洞板 Z 位置（在 stack 后部）
back_inner_z = -case_z / 2 + WALL
pcb_back_face_z = back_inner_z + backside_h
pcb_center_z = pcb_back_face_z + PCB_THICKNESS / 2

# 洞洞板 Y 位置：顶边对齐 +Y 内壁减 GPS_PROTRUSION_Y
pcb_top_y = case_y / 2 - WALL - GPS_PROTRUSION_Y
pcb_center_y = pcb_top_y - PCB_H / 2

# TFT 在洞洞板上的中心偏移（实测：TFT 不居中洞洞板）
tft_on_pcb_offset_x = TFT_OFFSET_LEFT + TFT_PCB_W / 2 - PCB_W / 2
tft_on_pcb_offset_y = -(TFT_OFFSET_TOP + TFT_PCB_H / 2 - PCB_H / 2)

# TFT PCB 中心在外壳坐标系里的位置（洞洞板 X 居中外壳）
tft_pcb_center_x = 0 + tft_on_pcb_offset_x
tft_pcb_center_y = pcb_center_y + tft_on_pcb_offset_y

# 玻璃中心相对 PCB 中心的偏移
glass_offset_x = -TFT_PCB_W / 2 + TFT_VISIBLE_OFFSET_X + TFT_VISIBLE_W / 2
glass_offset_y = TFT_PCB_H / 2 - TFT_VISIBLE_OFFSET_Y - TFT_VISIBLE_H / 2

glass_center_x = tft_pcb_center_x + glass_offset_x
glass_center_y = tft_pcb_center_y + glass_offset_y

print(f"backside_h={backside_h:.2f}, frontside_h={frontside_h:.2f}")
print(f"inner: {inner_x:.2f} x {inner_y:.2f} x {inner_z:.2f}")
print(f"outer: {case_x:.2f} x {case_y:.2f} x {case_z:.2f}")
print(f"PCB center: y={pcb_center_y:.2f}, z={pcb_center_z:.2f}")

# ============================================================
# Build — 整个外壳作为单件（不分前 / 后壳，简化预览）
# ============================================================
case = cq.Workplane("XY").box(case_x, case_y, case_z, centered=(True, True, True))

# 挖内腔
inner_box = cq.Workplane("XY").box(inner_x, inner_y, inner_z, centered=(True, True, True))
case = case.cut(inner_box)

# ---- +Z 面：TFT 窗 ----
window_w = TFT_VISIBLE_W + 2 * 1.0  # TFT_BEZEL_INSET
window_h = TFT_VISIBLE_H + 2 * 1.0
tft_window_cut = (
    cq.Workplane("XY")
    .box(window_w, window_h, WALL + 1, centered=(True, True, True))
    .translate((glass_center_x, glass_center_y, case_z / 2))
)
case = case.cut(tft_window_cut)

# ---- +Y 面：实心塑料背壳（GPS 信号穿透） ----
# 按 PRD §设备物理结构，+Y 面不开 GPS 罩——PLA/PETG 在 1.575GHz 几乎透明
# GPS 模块焊接斜 10-30° 不影响接收（陶瓷天线半球形辐射）
# 这里故意留空，不做任何 cut。

# ---- +Y 面：2 个 M12 按键圆孔（GPS 罩两侧，朝天） ----
btn_d = BTN_PANEL_HOLE_DIA + PRINT_TOLERANCE
btn_x_positions = (-28.0, +28.0)
btn_z_center = pcb_center_z

def cyl_along_y(diameter, length, x, y_min, z):
    """沿 +Y 方向的圆柱，底面在 (x, y_min, z)。"""
    return (
        cq.Workplane("XY")
        .circle(diameter / 2)
        .extrude(length)
        .rotate((0, 0, 0), (1, 0, 0), -90)
        .translate((x, y_min, z))
    )

for bx in btn_x_positions:
    btn_cyl = cyl_along_y(btn_d, WALL + 1, bx, case_y / 2 - (WALL + 1), btn_z_center)
    case = case.cut(btn_cyl)

# ---- +X 面：完全封闭（防尘） ----
# 决定 2026-05-11: 取消 SD 卡口和 USB-C 充电口外露，外壳上 6 个面里 5 个实心
# 充电和换 SD 卡通过拆后盖完成（4 颗 M2 螺丝）。
# SD 卡读写主要靠 Wi-Fi web UI（docs/PRD.md），降低换卡频率。

# ---- 4 个 TFT 螺柱（M3 自攻）从 +Z 内壁伸出，托住 TFT PCB ----
# 螺柱从 +Z 内壁（case_z/2 - WALL）往内伸 = -Z 方向
# 螺柱长度 = TFT_FRONT_HEIGHT + 1（让 TFT PCB 背面正好坐在螺柱顶端）
tft_boss_h = TFT_FRONT_HEIGHT + 1
tft_boss_top_z = case_z / 2 - WALL
tft_boss_bot_z = tft_boss_top_z - tft_boss_h
for sx in (-1, 1):
    for sy in (-1, 1):
        boss = (
            cq.Workplane("XY")
            .center(tft_pcb_center_x + sx * TFT_SCREW_HOLE_DX / 2, tft_pcb_center_y + sy * TFT_SCREW_HOLE_DY / 2)
            .circle(5.5 / 2)
            .extrude(tft_boss_h)
            .translate((0, 0, tft_boss_bot_z))
        )
        # M3 自攻底孔 (Φ2.5)
        boss = (
            boss.faces("<Z")
            .workplane()
            .circle(2.5 / 2)
            .cutBlind(-(tft_boss_h - 1))
        )
        case = case.union(boss)

# ============================================================
# 参考几何（透明，方便看内部布局）
# ============================================================
# 洞洞板 + 各模块作为单独的几何，用 Assembly 标色

perfboard = (
    cq.Workplane("XY")
    .box(PCB_W, PCB_H, PCB_THICKNESS, centered=(True, True, True))
    .translate((0, pcb_center_y, pcb_center_z))
)

tft_assembly = (
    cq.Workplane("XY")
    .box(TFT_PCB_W, TFT_PCB_H, TFT_PCB_THICKNESS + TFT_FRONT_HEIGHT, centered=(True, True, True))
    .translate((tft_pcb_center_x, tft_pcb_center_y, case_z / 2 - WALL - (TFT_PCB_THICKNESS + TFT_FRONT_HEIGHT) / 2))
)

gps_block = (
    cq.Workplane("XY")
    .box(GPS_MODULE_W, GPS_PROTRUSION_Y * 1.2, GPS_MODULE_H, centered=(True, True, True))
    .translate((0, pcb_top_y + GPS_PROTRUSION_Y * 0.5, pcb_center_z))
)

batt_block = (
    cq.Workplane("XY")
    .box(BATT_WIDTH_X, BATT_LENGTH_Y, BATT_DEPTH_Z, centered=(True, True, True))
    .translate((-20, pcb_center_y, pcb_back_face_z - BATT_DEPTH_Z / 2))
)

esp32_block = (
    cq.Workplane("XY")
    .box(ESP32_WIDTH_X, ESP32_LENGTH_Y, ESP32_HEADER_STANDOFF + ESP32_DEVKIT_HEIGHT, centered=(True, True, True))
    .translate((20, pcb_center_y, pcb_back_face_z - (ESP32_HEADER_STANDOFF + ESP32_DEVKIT_HEIGHT) / 2))
)

sd_block = (
    cq.Workplane("XY")
    .box(30, 15, SD_MODULE_HEIGHT, centered=(True, True, True))
    .translate((-10, pcb_center_y - 15, pcb_center_z + PCB_THICKNESS / 2 + SD_MODULE_HEIGHT / 2))
)

# ============================================================
# 导出
# ============================================================
out_dir = Path(__file__).parent
print(f"output: {out_dir}")

# 只导出外壳本体
cq.exporters.export(case, str(out_dir / "case_preview_base.step"))
cq.exporters.export(case, str(out_dir / "case_preview_base.stl"))

# 删掉旧的"上盖"文件（v1 遗留），避免误用
for stale in ("case_preview_lid.step", "case_preview_lid.stl"):
    p = out_dir / stale
    if p.exists():
        p.unlink()
        print(f"deleted stale: {stale}")

# 装配体：外壳 + 透明的内部模块参考
assembly = cq.Assembly()
assembly.add(case, name="case", color=cq.Color(0.7, 0.7, 0.75, 0.6))
assembly.add(perfboard, name="perfboard", color=cq.Color(0.0, 0.5, 0.2, 0.8))
assembly.add(tft_assembly, name="tft", color=cq.Color(0.0, 0.0, 0.0, 0.9))
assembly.add(gps_block, name="gps", color=cq.Color(0.5, 0.5, 0.5, 0.8))
assembly.add(batt_block, name="batt", color=cq.Color(0.1, 0.3, 0.8, 0.8))
assembly.add(esp32_block, name="esp32", color=cq.Color(0.2, 0.2, 0.2, 0.9))
assembly.add(sd_block, name="sd", color=cq.Color(0.2, 0.4, 0.9, 0.8))
assembly.save(str(out_dir / "case_preview_assembled.step"))

print("done.")
