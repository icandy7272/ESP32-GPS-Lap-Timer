"""
ESP32 GPS Lap Timer enclosure v3 — front + back shells, sealed, with lip slide-fit.

Mounting orientation (in kart):
  - +Y face (with 2 M12 button holes) points UP toward sky
  - +Z face (with TFT window) faces driver horizontally
  - GPS antenna patch on perfboard top edge, antenna face → +Y

Topology:
  - Front shell (~9mm deep) — TFT window on +Z face, 4 TFT M3 bosses,
    lip ring (LID_LIP=8mm) on -Z edge, 2 M2 through-holes on +Y corners
    with counter-bores.
  - Back shell (~37mm deep) — 5 solid faces, 2 corner bosses on +Y side
    with M2 brass-insert pilot holes, GPS RF window on +Y outer wall,
    2 M12 button holes on +Y face, 4 PCB self-tap posts inside, 4 cable-tie
    mount holes through floor (2 +Y holes vent through corner blocks).
    Anti-warp: 1× ridge on front lip -Y / 1× groove on back -Y wall.

Assembly:
  1. M3 screws fix TFT to front shell (4× M3×6 self-tap).
  2. Perfboard slides into back shell, 4× M2 self-tap into PCB posts.
  3. M12 buttons screw into back shell +Y face (panel mount).
  4. Front shell lip slides into back shell, mating connectors align.
  5. M2 machine screws (2× on +Y corners) thread from front through
     to back-shell brass inserts (heat-set into PLA/PETG).

All measurements per parameters.md / parameters.csv (verified 2026-05-11).
"""

import cadquery as cq
from pathlib import Path

# ============================================================
# §1.1 TFT (measured 2026-05-11)
# ============================================================
TFT_PCB_W = 89.45
TFT_PCB_H = 56.01
TFT_PCB_THICKNESS = 1.17
TFT_BACK_HEIGHT = 8.28
TFT_FRONT_HEIGHT = 3.83
TFT_VISIBLE_W = 67.53
TFT_VISIBLE_H = 52.07
TFT_VISIBLE_OFFSET_X = 7.5
TFT_VISIBLE_OFFSET_Y = 2.37
TFT_SCREW_HOLE_DIA = 3.01
TFT_SCREW_HOLE_DX = 83.42
TFT_SCREW_HOLE_DY = 50.07
TFT_OFFSET_LEFT = 8.56
TFT_OFFSET_TOP = 12.9

# ============================================================
# §1.2-1.5 (measured + placeholders)
# ============================================================
PCB_W = 100.0
PCB_H = 70.0
PCB_THICKNESS = 1.5

BATT_LENGTH_Y = 71.07
BATT_WIDTH_X = 40.78
BATT_DEPTH_Z = 19.50
BATT_BOT_TO_PERFBOARD_BACK = 23.11  # 洞洞板背面 → 电池底 (实测 2026-05-12)
                                    # 23.11 - 19.50 = 3.61mm 是 电池上面 ↔ 洞洞板背面 间隙 (线材/泡棉)
                                    # 用作 backside_h 的真值 (取代旧 max(BATT_D, ESP32...)=19.5 错误估算)

ESP32_BOT_TO_PERFBOARD_BACK = 15.63  # ESP32-S3 DevKit 最远面 (USB-C jack / 排针 / 模块本体)
                                      # → 洞洞板背面 Z 距离 (实测 2026-05-12)
                                      # 15.63 < BATT_BOT_TO_PERFBOARD_BACK(23.11) → 电池决定 backside_h

GPS_MODULE_W = 28.0
GPS_MODULE_H = 28.0
GPS_PATCH_HEIGHT = 11.0
GPS_PROTRUSION_Y = 17.0  # 实测 14.3，加 2.7mm 给按键背部留 Y 余量（防撞电池）

BTN_PANEL_HOLE_DIA = 12.88  # 实测螺纹 11.88 + 1mm 间隙 = 12.88
BTN_BACKSIDE_DEPTH = 13.0
BTN_FLANGE_DIA = 13.83  # M12 按键法兰盘直径（实测 2026-05-11）

# ============================================================
# Stack 关键 Z 距离 (全部实测, 跟 parameters.md/.csv 一一对应)
# ============================================================
TFT_BACK_TO_PERFBOARD_GAP = 10.56   # TFT PCB 背面 → 洞洞板正面 (实测 2026-05-12)
                                    # 包括: 排母高 + TFT 排针根部塑料 + 空气

# ============================================================
# §2 Design parameters
# ============================================================
WALL = 2.0
LID_LIP = 8.0              # lip 加长 (原 5 → 8mm) 提升接合面积 / 防尘 / 抗振
LID_LIP_GAP = 0.14         # 普通止口 (顶/左/右) 双边总间隙 (单边 0.07)
LID_LIP_GAP_BOT = 0.20     # 底部 (-Y, 有公凸条+母凹槽) 双边总间隙 (单边 0.10)
                           # 让"公止口底面↔母止口底面" 满足 0.15mm 总间隙
                           # = LID_LIP_GAP_BOT/2 + GROOVE_DEPTH - RIDGE_HEIGHT = 0.10 + 0.05 = 0.15
PRINT_TOLERANCE = 0.4
TFT_BEZEL_INSET = 1.0
LIP_END_CHAMFER = 0.3      # lip 末端 4 边 C0.3 倒角 — FDM 装配 lead-in

# GPS 天线 RF 窗口 (internal recess on +Y outer wall)
# Mounting: case +Y 面朝天, GPS 天线 patch 朝 +Y 方向 (用户确认)
# 模块姿态: 平放在 perfboard 顶边, 28×28 天线 face 朝 +Y 方向
# 模块 Z 位置: +Z 边距 perfboard 正面 +2.84mm (用户实测), Z span 28mm (= 28×28)
GPS_RECESS_W = 28.0        # X 宽 (= 天线 X size, 覆盖 GPS X=[1, 29])
GPS_RECESS_DEPTH = 1.0     # Y 深 (朝 +Y 切入 wall material 1mm, +Y 壁 2mm → 1mm)
                           # RF 减衰 ~1-2dB (vs 2mm 全壁)
GPS_ANTENNA_SIZE = 28.0    # 天线 28×28 (用户给的)
GPS_ANTENNA_Z_ABOVE_PCB_FRONT = 2.84  # 天线 +Z 边距 perfboard 正面 (用户实测)

# v3 new
CORNER_BOSS_DIA = 6.5      # 4-corner screw boss OD
INSERT_HOLE_DIA = 3.5      # M2 brass heat-set insert OD (pilot)
INSERT_DEPTH = 5.0
M2_THROUGH_DIA = 2.4
M2_HEAD_DIA = 4.5          # M2 socket head capscrew head ~4mm + clearance

# TFT boss (M3 self-tap into front shell, 固定 TFT 模块)
TFT_BOSS_OD = 5.5          # TFT 螺柱外径 (M3 通孔 Φ3.4 + 周壁 ≥1mm)
TFT_M3_PILOT_DIA = 2.5     # M3 self-tap 底孔 (= 0.83 × M3 螺纹外径 3.0)
TFT_BOSS_FLOOR = 1.0       # M3 self-tap 底孔顶部留 1mm 实心防穿透

# PCB 立柱 (M2 self-tap 固定洞洞板)
PCB_MOUNT_HOLE_EDGE_DIST = 3.4   # 洞洞板 4 角 现有孔中心到板边距离 (实测)
PCB_POST_DIA = 4.5         # 立柱外径. 当前 aspect 5:1 (Φ4.5 × 23mm), FDM 可能轻微 warp
                           # 如果 prototype 立柱歪 > 1°, 改 5.5mm 增加刚度
PCB_POST_PILOT_DIA = 1.8   # M2 self-tap 底孔 (= 0.83 × M2 螺纹外径 2.0)
PCB_POST_FLOOR = 1.0       # M2 self-tap 底孔顶部留 1mm 实心

# 外壳挂载 (cable tie / M3 螺丝 通孔, 后壳 -Z 外底面)
MOUNT_HOLE_DIA = 4.0       # Φ4mm 通孔: 容纳 5mm 内宽 cable tie 或 M3 螺丝
MOUNT_HOLE_OFFSET_FROM_EDGE = 3.5  # 通孔中心距 case 外缘 3.5mm
                                   # 修复 (2026-05-13): 4 → 3.5
                                   #   旧 4mm 让 -Y mount hole Y=-43.035, hole +Y 边 -41.035
                                   #   距 -Y PCB post -Y 边 -40.815 仅 0.22mm (codex 抓的, 旧注释
                                   #   说 0.82mm 是算错). 改 3.5 后 hole +Y 边 -41.535,
                                   #   间隙 0.72mm (= 2 perimeter @ 0.4mm 喷嘴, FDM 临界但可接受).
                                   #   外侧壁仍有 3.5 - 2 = 1.5mm 厚 (3-4 perimeter).

# Stack 高度: 分项推导 (= 玻璃顶到电池底 ≈ 40.17mm)
# 早期端到端粗测 = 39mm, 跟分项推导差 1.17mm = 粗测误差, 用分项推导更准

# ============================================================
# Derived (sandwich) — 全部分项推导, 每项都跟一个实测对应
# ============================================================
# backside_h = 洞洞板背面 → 最远凸出物 的 Z 距离
# 取 电池 / ESP32 两侧的 max, 保证两侧组件都不被后壳挤压
# 当前: BATT=23.11 > ESP32=15.63 → 电池决定 (= 23.11)
backside_h = max(BATT_BOT_TO_PERFBOARD_BACK, ESP32_BOT_TO_PERFBOARD_BACK)
# frontside_h = 洞洞板正面 → 玻璃顶
frontside_h = TFT_BACK_TO_PERFBOARD_GAP + TFT_PCB_THICKNESS + TFT_FRONT_HEIGHT  # = 15.56
# inner_z = 后壳内底 → 前壳内表面
#   = backside_h + PCB_THICKNESS + frontside_h + AIR_GAP_TOP + AIR_GAP_BOT
AIR_GAP_TOP = 1.0     # 玻璃顶 ↔ 前壳 +Z 内表面 空气间隙
AIR_GAP_BOT = 0.7     # 电池底 ↔ 后壳 -Z 内表面 空气间隙
                      # 修复 (2026-05-13): 0.5 → 0.7
                      #   原 0.5 让 GPS 天线 -Z 边距后内壁差 0.05mm 是 nominal hard clash
                      #   (codex review 抓的). 0.7 给 GPS 底 +0.15mm 余量, 电池仍有 0.7mm
                      #   FDM 翘边间隙. 数学约束: backside_h + PCB_THICKNESS +
                      #   GPS_ANTENNA_Z_ABOVE_PCB_FRONT + AIR_GAP_BOT >= GPS_ANTENNA_SIZE
                      #   即 23.11 + 1.5 + 2.84 + AIR_GAP_BOT >= 28 → AIR_GAP_BOT >= 0.55
inner_z = backside_h + PCB_THICKNESS + frontside_h + AIR_GAP_TOP + AIR_GAP_BOT
inner_y = max(TFT_PCB_H, PCB_H, BATT_LENGTH_Y) + GPS_PROTRUSION_Y + 2
inner_x = max(TFT_PCB_W, PCB_W) + 2

case_x = inner_x + 2 * WALL
case_y = inner_y + 2 * WALL
case_z = inner_z + 2 * WALL

# Front shell depth: WALL (顶盖厚) + AIR_GAP_TOP (玻璃顶到内表面) + TFT_FRONT_HEIGHT (玻璃) + buffer
# = 2 + 1 + 3.83 + 2 = 8.83 mm
# Buffer 2mm 给 TFT 螺柱安装余量 (TFT PCB 正面到 lip 顶接合面之间)
FRONT_DEPTH_BUFFER = 2.0
FRONT_DEPTH = WALL + AIR_GAP_TOP + TFT_FRONT_HEIGHT + FRONT_DEPTH_BUFFER
# Back shell: 跟前壳本体在 seam 处接合 (lid body bottom = back top, 世界 Z 同点 ≈ 14.1)
# 旧公式 = case_z - FRONT_DEPTH + LID_LIP 错误地多加了 LID_LIP, 导致后壳外壁顶部
# 跟前壳本体在 Z=[seam, seam+LID_LIP] 区段重叠 (geometry bug). 修正后 lip 在 back
# inner cavity 里, back 外壁顶面 = lid body 底面 = seam.
BACK_DEPTH = case_z - FRONT_DEPTH

# Z key positions in world coords
back_inner_z = -case_z / 2 + WALL
# 修复 (2026-05-13): AIR_GAP_BOT 之前只在 inner_z 公式里加了 0.5mm 把 case 撑高,
# 但 pcb_back_face_z = back_inner_z + backside_h 没加, 导致:
#   1. 电池底直接贴后内壁 (FDM 翘边后无余量, 装配易卡)
#   2. GPS 天线 -Z 边沉入后壁 0.55mm (物理冲突)
#   3. TFT_BACK_TO_PERFBOARD_GAP CAD vs 实测差 0.5mm
# 修复后 pcb_back_face_z 抬高 0.5mm, 三个问题同时解决.
pcb_back_face_z = back_inner_z + AIR_GAP_BOT + backside_h
pcb_center_z = pcb_back_face_z + PCB_THICKNESS / 2
pcb_front_face_z = pcb_back_face_z + PCB_THICKNESS

# GPS 天线 Z 位置 (用户实测 2026-05-12: 天线 +Z 边距 perfboard 正面 2.84mm)
# 天线 28×28 face 朝 +Y, Z span 28mm
gps_antenna_z_top_w = pcb_front_face_z + GPS_ANTENNA_Z_ABOVE_PCB_FRONT  # 7.115
gps_antenna_z_bot_w = gps_antenna_z_top_w - GPS_ANTENNA_SIZE             # -20.885
# RF 窗口 Z (clip 到 cavity 内底, 0.05mm 越界算 FDM 公差吸收)
gps_window_z_bot_w = max(gps_antenna_z_bot_w, back_inner_z)              # -20.835
gps_window_z_top_w = gps_antenna_z_top_w                                 # 7.115

pcb_top_y = case_y / 2 - WALL - GPS_PROTRUSION_Y
pcb_center_y = pcb_top_y - PCB_H / 2

# TFT offset on perfboard
tft_on_pcb_offset_x = TFT_OFFSET_LEFT + TFT_PCB_W / 2 - PCB_W / 2
tft_on_pcb_offset_y = -(TFT_OFFSET_TOP + TFT_PCB_H / 2 - PCB_H / 2)
tft_pcb_center_x = tft_on_pcb_offset_x
tft_pcb_center_y = pcb_center_y + tft_on_pcb_offset_y

# TFT glass center
glass_offset_x = -TFT_PCB_W / 2 + TFT_VISIBLE_OFFSET_X + TFT_VISIBLE_W / 2
glass_offset_y = TFT_PCB_H / 2 - TFT_VISIBLE_OFFSET_Y - TFT_VISIBLE_H / 2
glass_center_x = tft_pcb_center_x + glass_offset_x
glass_center_y = tft_pcb_center_y + glass_offset_y

# 4-corner boss positions (X/Y)
# corner_boss_x 内嵌足够距离, 让 L-cut 后嵌件外侧壁 ≥ 2mm (FDM 安全壁厚)
# 旧 inner_x/2 - CORNER_BOSS_DIA/2 - 0.5 = ±47.25 → L-cut (X=±49.63) 后嵌件外
#    侧仅 0.63mm 壁 (Φ3.5 嵌件外缘 ±49.0), 热熔嵌件压入易变形/开裂
# 新 inner_x/2 - CORNER_BOSS_DIA/2 - 2.25 = ±45.5 → 嵌件外缘 ±47.25, L-cut 内
#    边 ±49.63, 外侧壁 2.38mm (= 6 perimeter @ 0.4mm 喷嘴) ✓
corner_boss_x = inner_x / 2 - CORNER_BOSS_DIA / 2 - 2.25
# corner_boss_y 故意往 -Y 一头挪，让 +Y 内壁附近的按键孔不被 corner boss 挡住
# 原: inner_y/2 - CORNER_BOSS_DIA/2 - 0.5 = +43.29 → 按键孔撞 corner boss
# 现: 取 PCB_H 一半 + 余量 → 既能稳定合盖，也避开按键孔区
corner_boss_y = PCB_H / 2 - 0.5  # = +34.5（4 角螺柱在 Y = ±34.5）

# 按键 X 位置：避开 corner boss 圆柱 + GPS (X=[+1,+29])
# 按键孔 cut 范围 X = ±37 ± 6.64 (Φ12.88 + 0.4 公差, R=6.64)
#   = [+30.36, +43.64]
#   - vs corner boss 圆柱 (中心 ±45.5, +34.5, R=3.25):
#       Y 不重叠 (boss Y max=37.75 vs 按键 Y min=44.04, 间隙 6.29mm) ✓
#   - vs corner block 矩形 (X=±[42.25, 51], Y=[31.25, 45.04]):
#       会切 block +Y+X 角一小块 (X=[42.25, 43.64] × Y=[44.04, 45.04]
#       × Z=[-9.08, +4.21]), 嵌件 pilot Z=[8.5, 13.5], 不撞嵌件 ✓
#   - vs GPS [+1, +29]: X 间隙 1.36mm ✓
btn_x_positions = (-37.0, +37.0)

print(f"case outer: {case_x:.2f} x {case_y:.2f} x {case_z:.2f}")
print(f"front shell: {case_x:.2f} x {case_y:.2f} x {FRONT_DEPTH:.2f} (+ lip {LID_LIP})")
print(f"back shell:  {case_x:.2f} x {case_y:.2f} x {BACK_DEPTH:.2f}")
print(f"TFT center: ({tft_pcb_center_x:.2f}, {tft_pcb_center_y:.2f})")
print(f"glass center: ({glass_center_x:.2f}, {glass_center_y:.2f})")

# ============================================================
# Helpers
# ============================================================

def make_lip_ring(outer_x, outer_y, inner_x_dim, inner_y_dim, height):
    """Rectangular ring extruded from Z=0 to Z=height."""
    outer = cq.Workplane("XY").box(outer_x, outer_y, height, centered=(True, True, False))
    inner = (
        cq.Workplane("XY")
        .box(inner_x_dim, inner_y_dim, height + 0.2, centered=(True, True, False))
        .translate((0, 0, -0.1))
    )
    return outer.cut(inner)


def cyl_along_y(diameter, length, x, y_min, z):
    """Cylinder along +Y, base at (x, y_min, z), top at (x, y_min+length, z)."""
    return (
        cq.Workplane("XY")
        .circle(diameter / 2)
        .extrude(length)
        .rotate((0, 0, 0), (1, 0, 0), -90)
        .translate((x, y_min, z))
    )


def cyl_along_x(diameter, length, x_min, y, z):
    """Cylinder along +X, base at (x_min, y, z), top at (x_min+length, y, z)."""
    return (
        cq.Workplane("XY")
        .circle(diameter / 2)
        .extrude(length)
        .rotate((0, 0, 0), (0, 1, 0), 90)
        .translate((x_min, y, z))
    )


# ============================================================
# FRONT SHELL
# Local coords: origin at bottom-center, top at Z=FRONT_DEPTH
# Lip extends from Z=-LID_LIP to Z=0
# ============================================================
front = cq.Workplane("XY").box(case_x, case_y, FRONT_DEPTH, centered=(True, True, False))

# Inner cavity (bottom open, top has WALL thickness)
front_inner = (
    cq.Workplane("XY")
    .box(inner_x, inner_y, FRONT_DEPTH - WALL, centered=(True, True, False))
)
front = front.cut(front_inner)

# TFT 窗 (cut through +Z top wall)
window_w = TFT_VISIBLE_W + 2 * TFT_BEZEL_INSET
window_h = TFT_VISIBLE_H + 2 * TFT_BEZEL_INSET
window_cut = (
    cq.Workplane("XY")
    .box(window_w, window_h, WALL + 1, centered=(True, True, True))
    .translate((glass_center_x, glass_center_y, FRONT_DEPTH - WALL / 2))
)
front = front.cut(window_cut)

# 注意：按键孔不在前壳上！前壳深度 8.83mm < 按键孔直径 13.4mm
# 按键孔放后壳 +Y 面（见下方 BACK SHELL 区段）。
#
# GPS RF 窗口 — Cut lip +Y 壁 in window-lip Z overlap
# 窗口主体在 back shell, 但跟 lip Z 范围有重叠的部分需要也切掉 lip 壁
# 不切的话, RF 经过 lip 1mm + back wall 1mm = 2mm PETG (= 没切等价)
gps_recess_cx_w_front = -PCB_W / 2 + 51 + GPS_MODULE_W / 2  # = 15
# Y 范围: 覆盖 lip +Y 壁 (Y=[43.965, 44.965])
front_y_min = inner_y / 2 - LID_LIP_GAP / 2 - WALL / 2 - 0.5  # 43.465
front_y_max = inner_y / 2 + GPS_RECESS_DEPTH                   # 46.035

# Z 范围 (front local): window-lip 重叠
# window Z = [gps_window_z_bot_w, gps_window_z_top_w] world
# lip Z = [lip末端, seam] world = [seam - LID_LIP, seam] = [6.005, 14.005]
# overlap = [max(window_bot, lip末端), min(window_top, seam)]
seam_z_w_local = case_z / 2 - FRONT_DEPTH                # = 14.005
lip_end_z_w = seam_z_w_local - LID_LIP                    # = 6.005
lip_overlap_min_w = max(gps_window_z_bot_w, lip_end_z_w)  # 6.005
lip_overlap_max_w = min(gps_window_z_top_w, seam_z_w_local)  # 7.115
# Convert to front local Z (= world Z - seam_z_w_local)
front_z_min_local = lip_overlap_min_w - seam_z_w_local - 0.05  # -8.05
front_z_max_local = lip_overlap_max_w - seam_z_w_local + 0.05  # -6.84

gps_recess_front = (
    cq.Workplane("XY")
    .center(gps_recess_cx_w_front, (front_y_min + front_y_max) / 2)
    .box(GPS_RECESS_W, front_y_max - front_y_min,
         front_z_max_local - front_z_min_local, centered=(True, True, False))
    .translate((0, 0, front_z_min_local))
)
front = front.cut(gps_recess_front)

# TFT 4 个 M3 螺柱 (从前壳 +Z 内壁朝 -Z 伸)
# 螺柱长 = AIR_GAP_TOP (玻璃到前壳内表面间隙) + TFT_FRONT_HEIGHT (玻璃凸出 PCB)
#       = 螺柱底端正好贴 TFT PCB 正面
tft_boss_h = AIR_GAP_TOP + TFT_FRONT_HEIGHT
tft_boss_top_local_z = FRONT_DEPTH - WALL
tft_boss_bot_local_z = tft_boss_top_local_z - tft_boss_h
for sx in (-1, 1):
    for sy in (-1, 1):
        cx = tft_pcb_center_x + sx * TFT_SCREW_HOLE_DX / 2
        cy = tft_pcb_center_y + sy * TFT_SCREW_HOLE_DY / 2
        boss = (
            cq.Workplane("XY")
            .center(cx, cy)
            .circle(TFT_BOSS_OD / 2)
            .extrude(tft_boss_h)
            .translate((0, 0, tft_boss_bot_local_z))
        )
        boss = (
            boss.faces("<Z")
            .workplane()
            .circle(TFT_M3_PILOT_DIA / 2)
            .cutBlind(-(tft_boss_h - TFT_BOSS_FLOOR))
        )
        front = front.union(boss)

# Lip ring at bottom (-Z) of front shell — extends Z from -LID_LIP to 0
# 非对称设计:
#   +Y 顶部 / ±X 侧边: 单边 LID_LIP_GAP/2 = 0.07 (普通止口)
#   -Y 底部:          单边 LID_LIP_GAP_BOT/2 = 0.10 (留余量给凸条/槽配合)
GAP_NORMAL = LID_LIP_GAP / 2       # 0.07
GAP_BOTTOM = LID_LIP_GAP_BOT / 2   # 0.10
lip_outer_x_pos = +inner_x / 2 - GAP_NORMAL
lip_outer_x_neg = -inner_x / 2 + GAP_NORMAL
lip_outer_y_top = +inner_y / 2 - GAP_NORMAL
lip_outer_y_bot = -inner_y / 2 + GAP_BOTTOM
lip_outer_dx = lip_outer_x_pos - lip_outer_x_neg
lip_outer_dy = lip_outer_y_top - lip_outer_y_bot
lip_outer_cx = (lip_outer_x_pos + lip_outer_x_neg) / 2   # = 0
lip_outer_cy = (lip_outer_y_top + lip_outer_y_bot) / 2   # ≈ +0.015 (微偏 +Y)
lip_inner_dx = lip_outer_dx - WALL
lip_inner_dy = lip_outer_dy - WALL

lip_outer_box = (
    cq.Workplane("XY")
    .center(lip_outer_cx, lip_outer_cy)
    .box(lip_outer_dx, lip_outer_dy, LID_LIP, centered=(True, True, False))
    .translate((0, 0, -LID_LIP))
)
lip_inner_cut = (
    cq.Workplane("XY")
    .center(lip_outer_cx, lip_outer_cy)
    .box(lip_inner_dx, lip_inner_dy, LID_LIP + 0.2, centered=(True, True, False))
    .translate((0, 0, -LID_LIP - 0.1))
)
lip_ring = lip_outer_box.cut(lip_inner_cut)

# lip 末端 C0.3 倒角 (= LIP_END_CHAMFER): 装配 lead-in, 避免 lip 末端 sharp 角撞 base inner_wall 入口边
# 必须在 union 前 + M2 通孔 cut 前做, 否则 faces("<Z") 会包含 M2 孔的圆边, chamfer 失败
# 这会倒 lip 末端 outer + inner 两圈 (8 边), inner 圈在 lip 内部, 不影响装配
try:
    lip_ring = lip_ring.faces("<Z").chamfer(LIP_END_CHAMFER)
except Exception as e:
    print(f"[lip-end chamfer skipped] {e}")

front = front.union(lip_ring)

# 2 corner M2 through holes + sunk heads (仅上半 +Y 一侧，因为下方 boss 撞洞洞板取消了)
for sx in (-1, 1):
    sy = +1  # 只在 +Y 一侧
    cx = sx * corner_boss_x
    cy = sy * corner_boss_y
    # 通孔 through entire front + lip
    hole = (
        cq.Workplane("XY")
        .center(cx, cy)
        .circle(M2_THROUGH_DIA / 2)
        .extrude(FRONT_DEPTH + LID_LIP + 1)
        .translate((0, 0, -LID_LIP - 0.5))
    )
    front = front.cut(hole)
    # Sunk head (counter-bore) from front top
    head = (
        cq.Workplane("XY")
        .center(cx, cy)
        .circle(M2_HEAD_DIA / 2)
        .extrude(WALL + 0.5)
        .translate((0, 0, FRONT_DEPTH - WALL - 0.5))
    )
    front = front.cut(head)

# ============================================================
# 防翘 (Anti-warp) — 仅在 -Y 底部做一条横向凸条 / 凹槽 (其他 3 边纯 lip 配合)
#
# 前壳 (-Y 底部) 公凸条:
#   - 沿 lip 底部 -Y 外侧壁通长一条 (X 方向跨 lip 底部全宽)
#   - 高度 (径向, 朝 -Y): RIDGE_HEIGHT = 0.3 mm  (精确)
#   - 上下厚度 (Z 方向): RIDGE_Z_THICK = 1.0 mm  (跟 lip 主体一样)
#   - 下边缘 40° 导入斜面 (C0.29 chamfer)
#   - 上边缘 0° 水平面 (锁紧面)
#   - 根部 R0.29 圆角 (上边外角, 受 0.3mm 凸条高度限制不能取满 0.3)
#
# 后壳 (-Y 底部) 母凹槽:
#   - 沿 -Y inner_wall 通长一条
#   - 深度 (径向, 朝 -Y): GROOVE_DEPTH = 0.35 mm  (比凸条高 0.05 → 留 Y 余量)
#   - 上下宽度 (Z 方向): GROOVE_Z_THICK = 1.2 mm  (比凸条厚 0.2 → 上下各 0.1mm 余量)
#   - 内侧 (朝内腔的 +Y 面) 完全镂空 → 槽通到内腔, 不留底部材料
#   - 上缘 R0.2 导入圆角
#
# 间隙预算 (-Y 底部, 径向):
#   lip outer ↔ inner_wall  = GAP_BOTTOM = 0.10  (单边)
#   ridge tip ↔ groove bottom = GAP_BOTTOM + GROOVE_DEPTH - RIDGE_HEIGHT = 0.15 ✓
#   外壁剩余厚度             = WALL - GROOVE_DEPTH = 1.65 mm  (防尘 OK)
# ============================================================
RIDGE_HEIGHT = 0.3              # 公凸条径向凸出量 (精确, 朝 -Y)
RIDGE_Z_THICK = 1.0             # 公凸条 Z 上下厚度 (= lip 主体厚度)
RIDGE_TOP_FROM_LIP_TOP = 2.0    # 凸条上边距 lip 顶面 (Z=0 front local) 2.0mm 朝 -Z
RIDGE_BOT_FROM_LIP_TOP = 3.0    # 凸条下边距 lip 顶面 3.0mm 朝 -Z (= TOP + Z_THICK)
# 修复 (2026-05-13): 旧代码 chamfer/fillet 用 `0.3 - 0.01` 避免 cadquery 在
# face 宽度 = 凸条高 = 0.3 时 chamfer 整个面引起退化几何. 实际值是 0.29mm,
# 跟"C0.3 / R0.3"注释不一致 (codex 抓的). 改: 常量直接定 0.29, 代码不再减.
RIDGE_CHAMFER_DROP = 0.29       # 下缘 40° 导入 (≈ 0.29 distance, ≈ 40°)
RIDGE_ROOT_FILLET = 0.29        # 根部 R0.29 (受限于凸条 0.3mm 高, 不能取满 0.3)

GROOVE_DEPTH = 0.35             # 母凹槽径向深度 (= 凸条高 + 0.05 余量)
GROOVE_Z_THICK = 1.2            # 母凹槽 Z 上下宽度 (= 凸条厚 + 0.2 余量)
GROOVE_TOP_FROM_BACK_TOP = 2.0  # 凹槽上边距 back top (seam) 2.0mm 朝 -Z
GROOVE_BOT_FROM_BACK_TOP = 3.2  # 凹槽下边距 back top 3.2mm 朝 -Z

OVERLAP = 0.5                   # union/cut 强制融合用的体重叠量

# --- 前壳 -Y 底部公凸条 (single bar) ---
# Z 位置 (front local, lip 顶面 = Z=0):
#   ridge top    = 0 - 2.0 = -2.0
#   ridge bottom = 0 - 3.0 = -3.0
ridge_z_top_local    = -RIDGE_TOP_FROM_LIP_TOP   # = -2.0
ridge_z_bottom_local = -RIDGE_BOT_FROM_LIP_TOP   # = -3.0

# X 方向: 跟 lip 底部一样长 (跨 lip_outer_x_neg → lip_outer_x_pos)
ridge_x_dim = lip_outer_dx
ridge_cx = lip_outer_cx                                    # = 0
# Y 方向: 从 lip 底面 (Y=lip_outer_y_bot) 朝 -Y 凸 RIDGE_HEIGHT
#         +Y 端嵌入 lip 体 OVERLAP 强制 union 融合
ridge_y_dim = RIDGE_HEIGHT + OVERLAP
ridge_cy = lip_outer_y_bot - RIDGE_HEIGHT / 2 + OVERLAP / 2

ridge = (
    cq.Workplane("XY")
    .center(ridge_cx, ridge_cy)
    .box(ridge_x_dim, ridge_y_dim, RIDGE_Z_THICK, centered=(True, True, False))
    .translate((0, 0, ridge_z_bottom_local))
)

# 下缘 40° 导入斜面: 选 ridge 的 <Y 面 (-Y 外端面) 的 <Z 边 (最下边)
try:
    ridge = ridge.faces("<Y").edges("<Z").chamfer(RIDGE_CHAMFER_DROP)
except Exception as e:
    print(f"[ridge chamfer skipped] {e}")

# 根部 R0.29 圆角: 选 ridge 的 <Y 面 的 >Z 边 (上边外角, 跟 lip 体连接处)
try:
    ridge = ridge.faces("<Y").edges(">Z").fillet(RIDGE_ROOT_FILLET)
except Exception as e:
    print(f"[ridge root fillet skipped] {e}")

front = front.union(ridge)

# Translate front shell to world coords (so its top sits at +case_z/2)
front_world = front.translate((0, 0, case_z / 2 - FRONT_DEPTH))


# ============================================================
# BACK SHELL
# Local coords: origin at bottom-center, top at Z=BACK_DEPTH
# Inner cavity from Z=WALL upward
# ============================================================
back = cq.Workplane("XY").box(case_x, case_y, BACK_DEPTH, centered=(True, True, False))
back_inner = (
    cq.Workplane("XY")
    .box(inner_x, inner_y, BACK_DEPTH - WALL, centered=(True, True, False))
    .translate((0, 0, WALL))
)
back = back.cut(back_inner)

# ============================================================
# GPS RF 窗口 — internal recess 在 back shell +Y 外壁
# 位置 = 天线 X-Z 投影 (X=[1,29] × Z=[gps_window_z_bot_w, gps_window_z_top_w])
# 切 Y=[45.035, 46.035] (1mm 进 wall), 保留 1mm 外壁防尘
# 全覆盖天线 28×27.95mm (= ~100% 覆盖, 底部 0.05mm 越界算 FDM 公差吸收)
# ============================================================
gps_recess_cx_w = -PCB_W / 2 + 51 + GPS_MODULE_W / 2          # = 15 (天线 X 中心)
gps_recess_cy_w = inner_y / 2 + GPS_RECESS_DEPTH / 2 - 0.05    # = 45.485 (Y 中心 in wall)
back_window_h = gps_window_z_top_w - gps_window_z_bot_w        # 27.95
back_window_cz_w = (gps_window_z_bot_w + gps_window_z_top_w) / 2  # -6.86
back_window_cz_back = back_window_cz_w + case_z / 2            # back local Z

gps_recess_back = (
    cq.Workplane("XY")
    .center(gps_recess_cx_w, gps_recess_cy_w)
    .box(GPS_RECESS_W, GPS_RECESS_DEPTH + 0.1, back_window_h, centered=(True, True, True))
    .translate((0, 0, back_window_cz_back))
)
back = back.cut(gps_recess_back)

# 2 个 M12 按键圆孔 (后壳 +Y 顶面)
# X 位置在洞洞板左右边缘附近 (±37)，让接线柱沿 -Y 延伸时避开电池/ESP32 X 范围
# Z 位置: 按键孔顶边距 lip 末端 ≥ 2mm
#   关键: 按键 THREAD (Φ11.88, 长 13mm) 装入后会沿 -Y 进入 case 内腔
#   如果 thread Z 范围 跟 lip Z 范围 [BACK_DEPTH-LID_LIP, BACK_DEPTH] 重叠,
#   thread 会物理撞 lip +Y 内壁 → 按键装不进去!
#   旧公式 BACK_DEPTH - 2 - FLANGE/2 = 27.925 只 cover 法兰盘距 seam 约束,
#   忘了 thread 跟 lip 重叠这个硬冲突 (lip 在 seam 下方 8mm Z 范围)
btn_d = BTN_PANEL_HOLE_DIA + PRINT_TOLERANCE
# 修复 (2026-05-13): 旧公式两个 bug
#   (a) 用 BTN_PANEL_HOLE_DIA/2 而非 btn_d/2 算 cut top, 实际 cut top 比公式高 0.2mm
#   (b) 2mm gap 只 cover 到 lip末端, 没 cover 到 L-cut 底 (= lip末端 - 0.5)
#       结果: 按键 cut 顶距 L-cut 底只剩 1.30mm 桥 (codex 抓的)
# 新公式: gap 3.0mm 到 lip末端, 留 2.5mm 桥到 L-cut 底, 用 btn_d/2 正确算 cut 上沿
BTN_TO_LIP_END_GAP = 3.0   # 按键 cut 顶 ↔ lip末端 Z 距离 (= 桥 2.5mm + L-cut buffer 0.5mm)
btn_z_back_local = (BACK_DEPTH - LID_LIP) - BTN_TO_LIP_END_GAP - btn_d / 2
# AIR_GAP_BOT=0.7 时: (37.04 - 8) - 3 - 6.64 = 19.4 back local = -3.535 world
# 验证:
#   cut 顶 (world) = -3.535 + 6.64 = 3.105, L-cut 底 (world) = 5.605, 桥 2.50mm ✓
#   thread 顶 (world) = -3.535 + 5.94 = 2.405, PCB 正面 (world) = 4.375, 间隙 1.97mm ✓
#   cut 底 (world) = -3.535 - 6.64 = -10.18, back inner 底 = -20.935, 间隙 10.76mm ✓

# 按键孔: 圆柱 Φ13.28 (= BTN_PANEL_HOLE_DIA + PRINT_TOLERANCE)
# 必须是圆: M12 按键法兰盘 Φ13.83 是圆形, 孔的外接圆 ≤ 法兰盘内接圆 (= 13.83),
#   否则孔的"非圆"部分会露出在法兰盘外, 4 个角各漏 ~2.5mm 缝隙.
# Cut 长度: BTN_BACKSIDE_DEPTH + 1 = 14mm (覆盖整个螺纹+螺帽进 case 的深度).
#   修复 (2026-05-13): 旧 cut 长度 WALL+1=3mm 只覆盖 +Y 外壁穿透+1mm 进 cavity,
#   不够长. 按键螺纹 (Φ11.88 长 13mm) 在 Y < 44.035 时 X 边缘 (X=42.25-42.94)
#   会撞 corner block 的实心塑料 (block X=[42.25, 51], Y=[31.25, 45.035]).
#   按键根本插不进去. 改 14mm 后 cut 通道穿过 block 边缘 (block 内壁被掏出
#   Φ13.28 切片 1.39mm X × 11mm Y), 螺纹有通路. 嵌件 pilot Z=[8.5, 13.5] 距
#   cut Z [-9.475, 3.105] 间隙 5.4mm, 嵌件不受影响.
# FDM 考量: Φ13.28 横向孔印姿垂直时顶部圆弧有 >45° 悬垂, 但 13mm 桥接 PETG/PLA
#   常规 sag ≤0.3mm, 螺纹有 0.7mm 余量足够吸收. slicer 自动 bridge OK,
#   或手动添加 sacrificial support 印完去掉.
BTN_CUT_LENGTH = BTN_BACKSIDE_DEPTH + 1   # = 14mm

# 六角螺帽内腔台阶孔 (hex nut clearance recess)
# 用户实测 (2026-05-13): M12 按键螺帽六角 13.88mm across flats
#   → R_corner = 6.94 / cos(30°) = 8.01mm, 对角直径 16.03mm
# 螺帽对角伸到 X=±37+8.01 = ±45.01, 比 corner block X 边 ±42.25 多 2.76mm,
#   塞不进 Φ13.28 按键孔 (R 6.64). 必须开 Φ16.5 内腔台阶孔, 让螺帽从内腔装入
#   并旋转拧紧.
# Recess 几何:
#   Φ = 16.5 (= 对角 16.03 + 0.5mm tolerance, 任意旋转方向都装得下)
#   Y 深 = 5mm (= 螺帽厚 ~2mm + 3mm 装配/紧固余量)
#   Y 范围 = [case_y/2 - WALL - 5, case_y/2 - WALL] = [40.035, 45.035]
# 副作用:
#   切 corner block 在 X=[42.25, 45.26] × Y=[40.035, 45.035] × Z=[-11.79, 4.72]
#   = ~248 mm³ 体积 (~6% block, OK)
#   嵌件 pilot Z=[8.5, 13.5], recess Z 上沿 4.72, Z 间隙 3.78mm, 嵌件不受影响
BTN_NUT_RECESS_OD = 16.5
BTN_NUT_RECESS_DEPTH = 5.0
for bx in btn_x_positions:
    # 主按键孔 (Φ13.28, 14mm 长穿 +Y 壁 + 内腔通道)
    btn_cyl = cyl_along_y(btn_d, BTN_CUT_LENGTH, bx, case_y / 2 - BTN_CUT_LENGTH, btn_z_back_local)
    back = back.cut(btn_cyl)
    # 螺帽台阶孔 (Φ16.5, 5mm 深从内壁起)
    nut_recess = cyl_along_y(
        BTN_NUT_RECESS_OD,
        BTN_NUT_RECESS_DEPTH,
        bx,
        case_y / 2 - WALL - BTN_NUT_RECESS_DEPTH,
        btn_z_back_local,
    )
    back = back.cut(nut_recess)

# 2 corner bosses with M2 insert pilot holes (仅 +Y 一侧上 2 个)
# 下半 2 boss 取消：下方在洞洞板范围内放不下 boss（洞洞板 Y bottom = -41.965, 外壳 -Y 内壁 = -45.035）
# 下半合盖靠 8mm lip 配合 (LID_LIP) + 防翘 ridge/groove (-Y 底部)
corner_boss_h_back = BACK_DEPTH - WALL - 0.5  # boss 顶距 seam 0.5mm
for sx in (-1, 1):
    sy = +1  # 只在 +Y 一侧
    cx = sx * corner_boss_x   # ±45.5 (嵌件中心 X)
    cy = sy * corner_boss_y   # +34.5 (嵌件中心 Y)

    # 把 corner boss 扩展为"角块" — 同时 touch +X 和 +Y 两面内壁
    # 跟 PCB 立柱一样 integrate 到壁体, 消除独立瘦柱体的 FDM warp 风险
    # 嵌件压入时角块给周围更多塑料缓冲
    # 嵌件孔仍在原 (cx, cy), 用 workplane offset 维持
    block_x_inner = cx - sx * CORNER_BOSS_DIA / 2   # 离 ±X 壁远的那个 X face = ±42.25
    block_x_outer = sx * inner_x / 2                # ±X 内壁 = ±51 (8.75mm 跨度)
    block_y_inner = cy - sy * CORNER_BOSS_DIA / 2   # 离 +Y 壁远的那个 Y face = +31.25
    block_y_outer = sy * inner_y / 2                # +Y 内壁 = +45.035 (13.785mm 跨度)
    block_x_dim = abs(block_x_outer - block_x_inner)   # = 8.75 mm
    block_y_dim = abs(block_y_outer - block_y_inner)   # = 13.785 mm
    block_cx = (block_x_inner + block_x_outer) / 2     # = ±46.625
    block_cy = (block_y_inner + block_y_outer) / 2     # = +38.1425

    boss = (
        cq.Workplane("XY")
        .center(block_cx, block_cy)
        .rect(block_x_dim, block_y_dim)
        .extrude(corner_boss_h_back)
        .translate((0, 0, WALL))
    )
    # M2 黄铜嵌件孔 (圆形, 在原 boss 中心位置 (cx, cy), 不在 block 中心)
    boss = (
        boss.faces(">Z")
        .workplane()
        .center(cx - block_cx, cy - block_cy)
        .circle(INSERT_HOLE_DIA / 2)
        .cutBlind(-INSERT_DEPTH)
    )
    back = back.union(boss)

# ============================================================
# Corner block lip-passage clearance — cut a notch at each +Y corner block's
# outer corner (±X +Y), letting the front shell lip slide past during assembly.
#
# 没这个 cut 的话: lip 是 ring 形 (1mm 壁), Z=[6.005, 14.005] world.
# Lip 的 +X+Y 和 -X+Y 两个角材料跟 corner block X-Y 投影
# [42.25, 51]×[31.25, 45.035] 在 +Y 角完全重叠 → 装不进去.
#
# Cut 形状 (per corner): L 形 = A cut (X 壁过道) + B cut (+Y 壁过道)
#   A cut: X=[49.63, 51] × Y=[31.05, 45.235] × Z=[5.505, 14.505] (1.37mm 薄条)
#   B cut: X=[42.05, 51.2] × Y=[43.665, 45.035] × Z=[5.505, 14.505] (1.37mm 薄条)
# Lip wall 内缘 + 0.3mm LIP_PASS_CLEAR 余量, 配合 0.07mm 滑动 fit.
# M2 嵌件 pilot 在 (±45.5, +34.5):
#   - vs A cut 外侧壁: X 间隙 2.38mm (= 49.63 - (45.5 + 1.75)) ✓ FDM 安全壁厚
#   - vs B cut 外侧壁: Y 间隙 7.42mm (= 43.665 - (34.5 + 1.75)) ✓
# pilot 100% 保留, corner block 下半 (Z < 5.5) 仍完整.
# ============================================================
LIP_PASS_CLEAR = 0.3   # cut 内侧距 lip 壁内缘的额外间隙 (FDM tolerance 余量)

lip_outer_x_face = inner_x / 2 - GAP_NORMAL              # 50.93 (lip +X 外缘)
lip_outer_y_top  = inner_y / 2 - GAP_NORMAL              # 44.965 (lip +Y 外缘)
lip_inner_x_face = lip_outer_x_face - WALL / 2           # 49.93 (lip 壁内缘)
lip_inner_y_top  = lip_outer_y_top - WALL / 2            # 43.965

# Lip 4 壁的 cut 范围:
#   lip ±X 壁 路径: X=[lip_inner_x - 0.3, inner_x/2], Y=[block_y_inner, block_y_outer]
#   lip +Y 壁 路径: X=[block_x_inner, block_x_outer], Y=[lip_inner_y - 0.3, inner_y/2]
# 两个 cut 合起来在 corner block 上挖一个 L 形 (两条边的 strip)
cut_x_inner = lip_inner_x_face - LIP_PASS_CLEAR          # 49.63 (lip ±X 壁 cut 的内边)
cut_y_inner = lip_inner_y_top - LIP_PASS_CLEAR           # 43.665 (lip +Y 壁 cut 的内边)

# Z range (back local): 覆盖整段 lip Z + 0.5mm 上下 buffer
lip_z_end_back  = (case_z / 2 - FRONT_DEPTH - LID_LIP) + case_z / 2   # = case_z - FRONT_DEPTH - LID_LIP (lip末端 back local)
lip_z_top_back  = (case_z / 2 - FRONT_DEPTH) + case_z / 2             # = case_z - FRONT_DEPTH (seam back local)
cut_z_bot_back  = lip_z_end_back - 0.5
cut_z_top_back  = lip_z_top_back + 0.5
cut_z_dim       = cut_z_top_back - cut_z_bot_back

for sx in (-1, 1):
    # Block bounds (sign-aware)
    block_x_inner_world = sx * (corner_boss_x - CORNER_BOSS_DIA / 2)  # ±42.25
    block_x_outer_world = sx * inner_x / 2                             # ±51
    block_y_inner_world = corner_boss_y - CORNER_BOSS_DIA / 2          # +31.25
    block_y_outer_world = inner_y / 2                                  # +45.035

    # Cut A: lip ±X 壁 passage (沿 Y 方向, 在 block +X 边/-X 边)
    # X 范围: lip wall X + 0.3 clearance 到 内壁
    # Y 范围: block 整段 Y + 0.2 buffer (确保切干净)
    cut_a_x_in = sx * cut_x_inner   # ±49.63
    cut_a_x_out = block_x_outer_world  # ±51
    cut_a_y_min = block_y_inner_world - 0.2
    cut_a_y_max = block_y_outer_world + 0.2
    cut_a = (
        cq.Workplane("XY")
        .center((cut_a_x_in + cut_a_x_out) / 2, (cut_a_y_min + cut_a_y_max) / 2)
        .box(abs(cut_a_x_out - cut_a_x_in), cut_a_y_max - cut_a_y_min, cut_z_dim, centered=(True, True, False))
        .translate((0, 0, cut_z_bot_back))
    )
    back = back.cut(cut_a)

    # Cut B: lip +Y 壁 passage (沿 X 方向, 在 block +Y 边)
    # X 范围: block 整段 X + 0.2 buffer
    # Y 范围: lip wall Y + 0.3 clearance 到 内壁
    cut_b_x_min = min(block_x_inner_world, block_x_outer_world) - 0.2
    cut_b_x_max = max(block_x_inner_world, block_x_outer_world) + 0.2
    cut_b_y_in = cut_y_inner   # +43.665
    cut_b_y_out = block_y_outer_world  # +45.035
    cut_b = (
        cq.Workplane("XY")
        .center((cut_b_x_min + cut_b_x_max) / 2, (cut_b_y_in + cut_b_y_out) / 2)
        .box(cut_b_x_max - cut_b_x_min, cut_b_y_out - cut_b_y_in, cut_z_dim, centered=(True, True, False))
        .translate((0, 0, cut_z_bot_back))
    )
    back = back.cut(cut_b)

# ============================================================
# 洞洞板固定：4 个塑料立柱 + M2 自攻螺丝 (方案 E)
# ============================================================
# 洞洞板 4 角现有孔实测 (2026-05-11):
#   ㉝ 孔径 = Φ2 mm
#   ㉞ 孔边缘到洞洞板边沿 = 2.4 mm → 孔中心到边沿 = PCB_MOUNT_HOLE_EDGE_DIST = 3.4mm
PCB_MOUNT_HOLE_OFFSET_X = PCB_W / 2 - PCB_MOUNT_HOLE_EDGE_DIST    # = 46.6
PCB_MOUNT_HOLE_OFFSET_Y = PCB_H / 2 - PCB_MOUNT_HOLE_EDGE_DIST    # = 31.6
# 立柱高度 = AIR_GAP_BOT + backside_h
# 立柱顶 (back local Z = WALL + PCB_POST_HEIGHT) 到达 pcb_back_face_z 世界坐标
# 配合 line 161 修复 AIR_GAP_BOT 应用一致性 (旧公式只用 backside_h, 立柱顶差 0.5mm)
PCB_POST_HEIGHT = AIR_GAP_BOT + backside_h

# 4 个立柱位置 (世界坐标)
pcb_post_positions = [
    (+PCB_MOUNT_HOLE_OFFSET_X, pcb_center_y + PCB_MOUNT_HOLE_OFFSET_Y),  # 上右
    (-PCB_MOUNT_HOLE_OFFSET_X, pcb_center_y + PCB_MOUNT_HOLE_OFFSET_Y),  # 上左
    (+PCB_MOUNT_HOLE_OFFSET_X, pcb_center_y - PCB_MOUNT_HOLE_OFFSET_Y),  # 下右
    (-PCB_MOUNT_HOLE_OFFSET_X, pcb_center_y - PCB_MOUNT_HOLE_OFFSET_Y),  # 下左
]

for px, py in pcb_post_positions:
    # 立柱 X 方向延伸到最近的内壁 (= 案 ±X 侧边内壳)
    # 把立柱跟侧壁焊成一体 → FDM warp 风险大降, 刚度大增, slicer 出线统一
    # X 范围: 从立柱内侧面 (远离墙) 一直到内墙表面
    # Y 范围: 保持 PCB_POST_DIA (4.5), 中心 = py (跟 M2 螺丝对齐)
    # Z 范围: 跟原来一致 (WALL → WALL+PCB_POST_HEIGHT)
    sx = 1 if px > 0 else -1
    post_x_inner_face = px - sx * PCB_POST_DIA / 2   # 远离墙的那个 X face
    post_x_outer_face = sx * inner_x / 2             # 内墙表面 (±51)
    post_x_dim = abs(post_x_outer_face - post_x_inner_face)   # = 6.65 mm
    post_cx = (post_x_inner_face + post_x_outer_face) / 2     # = ±47.675

    post = (
        cq.Workplane("XY")
        .center(post_cx, py)
        .rect(post_x_dim, PCB_POST_DIA)
        .extrude(PCB_POST_HEIGHT)
        .translate((0, 0, WALL))
    )
    # M2 自攻底孔 (圆形, 从立柱顶部往下钻)
    # 钻孔位置在 (px, py) = 原洞洞板 mount hole 位置, NOT 矩形立柱中心
    # 故 workplane center 偏移 (px - post_cx) 让 pilot 对齐 PCB 螺丝
    post = (
        post.faces(">Z")
        .workplane()
        .center(px - post_cx, 0)
        .circle(PCB_POST_PILOT_DIA / 2)
        .cutBlind(-(PCB_POST_HEIGHT - PCB_POST_FLOOR))
    )
    back = back.union(post)

# 电池围栏（4 条短墙围电池外形）
BATT_FENCE_HEIGHT = 5.0
BATT_FENCE_THICK = 2.0   # 1.5 → 2.0: 给 FDM 0.4 喷嘴 4 perimeter 完全填实
                          # (1.5 太薄, slicer 可能 fallback 到 2-3 perimeter 不稳)
BATT_FENCE_GAP = 0.3   # 电池滑入间隙
# 电池实测位置（2026-05-11）：
#   ㉚ 电池左沿到洞洞板左沿 = 14 mm  → 电池中心 X = +15.61 (相对洞洞板中心)
#   ㉜ 电池顶沿到洞洞板顶沿 = 1 mm   → 电池贴洞洞板顶端
batt_cx = +15.61
batt_cy = pcb_top_y - 1 - BATT_LENGTH_Y / 2  # 电池顶贴洞洞板顶 -1mm
batt_outer_x = BATT_WIDTH_X + 2 * BATT_FENCE_GAP
batt_outer_y = BATT_LENGTH_Y + 2 * BATT_FENCE_GAP

# 4 条墙：上 / 下 / 左 / 右
for side in ["top", "bottom", "left", "right"]:
    if side == "top":
        wall = (
            cq.Workplane("XY")
            .center(batt_cx, batt_cy + batt_outer_y / 2 + BATT_FENCE_THICK / 2)
            .box(batt_outer_x + 2 * BATT_FENCE_THICK, BATT_FENCE_THICK, BATT_FENCE_HEIGHT, centered=(True, True, False))
            .translate((0, 0, WALL))
        )
    elif side == "bottom":
        wall = (
            cq.Workplane("XY")
            .center(batt_cx, batt_cy - batt_outer_y / 2 - BATT_FENCE_THICK / 2)
            .box(batt_outer_x + 2 * BATT_FENCE_THICK, BATT_FENCE_THICK, BATT_FENCE_HEIGHT, centered=(True, True, False))
            .translate((0, 0, WALL))
        )
    elif side == "left":
        wall = (
            cq.Workplane("XY")
            .center(batt_cx - batt_outer_x / 2 - BATT_FENCE_THICK / 2, batt_cy)
            .box(BATT_FENCE_THICK, batt_outer_y, BATT_FENCE_HEIGHT, centered=(True, True, False))
            .translate((0, 0, WALL))
        )
    else:  # right
        wall = (
            cq.Workplane("XY")
            .center(batt_cx + batt_outer_x / 2 + BATT_FENCE_THICK / 2, batt_cy)
            .box(BATT_FENCE_THICK, batt_outer_y, BATT_FENCE_HEIGHT, centered=(True, True, False))
            .translate((0, 0, WALL))
        )
    back = back.union(wall)

# ============================================================
# 后壳 -Y 底部母凹槽 (single bar cut)
# 沿 -Y inner_wall 一条横槽, 内侧朝内腔完全镂空 (通槽, 无底部材料)
# Z 位置 (back local, back top = Z=BACK_DEPTH = seam):
#   groove top    = BACK_DEPTH - 2.0  → 跟 ridge top 同 world Z (锁紧面接触)
#   groove bottom = BACK_DEPTH - 3.2  → 比 ridge bottom 多 0.2mm 朝 -Z (40° lead-in 余量)
# ============================================================
groove_z_top_back    = BACK_DEPTH - GROOVE_TOP_FROM_BACK_TOP   # = BACK_DEPTH - 2.0
groove_z_bottom_back = BACK_DEPTH - GROOVE_BOT_FROM_BACK_TOP   # = BACK_DEPTH - 3.2

# Cut box: X 跨整个内腔宽 + 余量; Y 朝 -Y 切 GROOVE_DEPTH + 朝 +Y 嵌入内腔 OVERLAP
groove_x_dim = inner_x + 2 * OVERLAP                          # X 略大于内腔
groove_y_dim = GROOVE_DEPTH + OVERLAP                         # Y 切深 + 内腔重叠
groove_cy    = -inner_y / 2 - GROOVE_DEPTH / 2 + OVERLAP / 2  # 中心: 槽底 = -inner_y/2 - 0.35

groove_cut = (
    cq.Workplane("XY")
    .center(0, groove_cy)
    .box(groove_x_dim, groove_y_dim, GROOVE_Z_THICK, centered=(True, True, False))
    .translate((0, 0, groove_z_bottom_back))
)
back = back.cut(groove_cut)

# 凹槽上缘 R0.2 圆角原本要做的, 但 0.4mm FDM 喷嘴印不出 < 0.4mm 圆角.
# 切片器会量化成 0 或 1 个 layer step, 实际打印效果跟没有 fillet 一样.
# 故省略 — 凸条自己的 40° chamfer 已提供足够的 lead-in.

# ============================================================
# 4 个外壳挂载通孔 (cable tie / M3 螺丝 通过后壳 -Z 外底面)
# 位置: 4 个 case 角落
# 修复 (2026-05-13): codex 抓的 [P1]
#   旧 cut 长度 = WALL + 1 = 3mm, 只穿底面壁 (back local Z=[-0.5, 2.5]).
#   +Y mount hole 在 (±49.5, +43.535), 落在 +Y corner block X-Y 投影
#   ([42.25, 51]×[31.25, 45.035]) 内. corner block Z 范围 [WALL, BACK_DEPTH-0.5]
#   = [2, 36.54] back local 紧挨 cut top 之上 → 形成 cap, cable tie 穿不过.
#   修复: cut 长度 = BACK_DEPTH + 1, 贯穿整个后壳高度. -Y mount hole 不在 corner
#   block 投影内, 多余的 cut 长度只切 air, 无副作用. +Y mount hole 现在贯穿
#   corner block, 给 cable tie 留出通道. 嵌件 pilot (cx=45.5, cy=34.5) 距
#   +Y mount hole (49.5, 43.535) 距离 ~9.88mm, pilot R + hole R = 3.75 << 9.88,
#   嵌件不受影响.
# Note: 这 4 个 Φ4 通孔轻微破坏防尘 seal, 但案 -Z 底面朝下,
#       灰尘不易直接进入. kart 实际使用 OK.
# ============================================================
mount_hole_offset_x = case_x / 2 - MOUNT_HOLE_OFFSET_FROM_EDGE   # = 49.5 (MOUNT_HOLE_OFFSET=3.5)
mount_hole_offset_y = case_y / 2 - MOUNT_HOLE_OFFSET_FROM_EDGE   # = 43.535

for sx in (-1, 1):
    for sy in (-1, 1):
        mhx = sx * mount_hole_offset_x
        mhy = sy * mount_hole_offset_y
        # 通孔长度 sy-dependent:
        #   +Y 角: 贯穿整个 back shell 高度, 让 +Y 角的 corner block 也被开 Φ4 通道
        #   -Y 角: 只贯穿 back floor (WALL+1=3mm), 避免向上切穿防翘 groove
        #          (groove world Z=[10.905, 12.105] 在 -Y 内壁 Y=[-45.385, -45.035]
        #           会跟 -Y mount hole Y 范围 [-45.535, -41.535] 重叠, 长贯穿会
        #           在两 -Y 角各打断 4mm groove, 防翘 ridge 失去咬合)
        cut_length = (BACK_DEPTH + 1) if sy == +1 else (WALL + 1)
        mount_hole = (
            cq.Workplane("XY")
            .center(mhx, mhy)
            .circle(MOUNT_HOLE_DIA / 2)
            .extrude(cut_length)
            .translate((0, 0, -0.5))
        )
        back = back.cut(mount_hole)

# Translate back shell to world coords
back_world = back.translate((0, 0, -case_z / 2))


# ============================================================
# Export
# ============================================================
out_dir = Path(__file__).parent

cq.exporters.export(front_world, str(out_dir / "case_v3_front.step"))
cq.exporters.export(front_world, str(out_dir / "case_v3_front.stl"))
cq.exporters.export(back_world, str(out_dir / "case_v3_back.step"))
cq.exporters.export(back_world, str(out_dir / "case_v3_back.stl"))

# Assembled (mated)
assembled = cq.Assembly()
assembled.add(front_world, name="front_shell", color=cq.Color(0.75, 0.75, 0.78, 0.85))
assembled.add(back_world, name="back_shell", color=cq.Color(0.62, 0.62, 0.66, 0.85))
assembled.save(str(out_dir / "case_v3_assembled.step"))

# Exploded (front shell lifted 25mm)
exploded = cq.Assembly()
exploded.add(
    front_world.translate((0, 0, 25)),
    name="front_shell",
    color=cq.Color(0.86, 0.46, 0.20, 0.90),
)
exploded.add(back_world, name="back_shell", color=cq.Color(0.62, 0.62, 0.66, 0.90))
exploded.save(str(out_dir / "case_v3_exploded.step"))

print("done.")
