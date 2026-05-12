"""
ESP32 GPS Lap Timer enclosure v3 — front + back shells, sealed, with lip slide-fit.

Topology:
  - Front shell (~9mm deep) — TFT window, 2 button holes on +Y face,
    4 TFT M3 bosses, lip ring on -Z edge.
  - Back shell (~40mm deep) — solid 5 faces, 4 corner bosses with M2
    brass-insert pilot holes, accepts front-shell lip.

Assembly:
  1. M3 screws fix TFT to front shell (4× M3×6 self-tap).
  2. Perfboard slides into back shell.
  3. Front shell lip slides into back shell, mating connectors align.
  4. M2 machine screws (4×) thread from front shell through to back-shell
     brass inserts (heat-set into PLA/PETG).

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
                           # (lip 壁厚 1mm, 内外两圈各 0.3 = 0.6mm 消耗, 净剩 0.4mm)

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
MOUNT_HOLE_OFFSET_FROM_EDGE = 4.0  # 通孔中心距 case 外缘 4mm
                                   # (offset=5mm 会让 -Y 角 mount hole 撞 PCB post -Y; 4mm 留 0.82mm 间隙)

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
AIR_GAP_BOT = 0.5     # 电池底 ↔ 后壳 -Z 内表面 空气间隙
                      # 0.5mm 给 FDM 翘边 / 电池底面不平 / 双面胶 留余量
                      # (原 0.0 太紧, 实际装配易卡)
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
# Back shell: 跟前壳本体在 seam 处接合 (lid body bottom = back top, 世界 Z 同点 = 13.755)
# 旧公式 = case_z - FRONT_DEPTH + LID_LIP 错误地多加了 LID_LIP, 导致后壳外壁顶部
# 跟前壳本体在 Z=[seam, seam+LID_LIP] 区段重叠 (geometry bug). 修正后 lip 在 back
# inner cavity 里, back 外壁顶面 = lid body 底面 = seam.
BACK_DEPTH = case_z - FRONT_DEPTH

# Z key positions in world coords
back_inner_z = -case_z / 2 + WALL
pcb_back_face_z = back_inner_z + backside_h
pcb_center_z = pcb_back_face_z + PCB_THICKNESS / 2

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
corner_boss_x = inner_x / 2 - CORNER_BOSS_DIA / 2 - 0.5
# corner_boss_y 故意往 -Y 一头挪，让 +Y 内壁附近的按键孔不被 corner boss 挡住
# 原: inner_y/2 - CORNER_BOSS_DIA/2 - 0.5 = +43.29 → 按键孔撞 corner boss
# 现: 取 PCB_H 一半 + 余量 → 既能稳定合盖，也避开按键孔区
corner_boss_y = PCB_H / 2 - 0.5  # = +34.5（4 角螺柱在 Y = ±34.5）

# 按键 X 位置：避开 corner boss (X=±47.25) 且避开 GPS (X=[+1,+29])
# 按键物理体 X = X ± 5.94 (螺纹半径)
# X=±37 时按键 X 范围 [+31.06, +42.94]:
#   - vs corner boss [+44, +50.5]: 不重叠，余 1.06mm ✓
#   - vs GPS [+1, +29]: 不重叠，余 2.06mm ✓
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

# lip 末端 C0.5 倒角: 装配 lead-in, 避免 lip 末端 sharp 角撞 base inner_wall 入口边
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
#   - 下边缘 40° 导入斜面 (chamfer)
#   - 上边缘 0° 水平面 (锁紧面)
#   - 根部 R0.3 圆角 (上边外角)
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
RIDGE_CHAMFER_DROP = 0.3        # 下缘 40° 导入: 0.3 distance ≈ 40° (Δh=0.3 / Δv=0.358)
RIDGE_ROOT_FILLET = 0.3         # 根部 R0.3

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
    ridge = ridge.faces("<Y").edges("<Z").chamfer(RIDGE_CHAMFER_DROP - 0.01)
except Exception as e:
    print(f"[ridge chamfer skipped] {e}")

# 根部 R0.3 圆角: 选 ridge 的 <Y 面 的 >Z 边 (上边外角, 跟 lip 体连接处)
try:
    ridge = ridge.faces("<Y").edges(">Z").fillet(RIDGE_ROOT_FILLET - 0.01)
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

# 2 个 M12 按键圆孔 (后壳 +Y 顶面)
# X 位置在洞洞板左右边缘附近 (±43)，让接线柱沿 -Y 延伸时避开电池/ESP32 X 范围
# Z 位置: 让法兰盘上沿距前后壳接缝 ≥ 2mm
btn_d = BTN_PANEL_HOLE_DIA + PRINT_TOLERANCE
btn_z_back_local = BACK_DEPTH - 2 - BTN_FLANGE_DIA / 2  # 法兰盘上沿距 seam ≥ 2mm
for bx in btn_x_positions:
    btn_cyl = cyl_along_y(btn_d, WALL + 1, bx, case_y / 2 - (WALL + 1), btn_z_back_local)
    back = back.cut(btn_cyl)

# 2 corner bosses with M2 insert pilot holes (仅 +Y 一侧上 2 个)
# 下半 2 boss 取消：下方在洞洞板范围内放不下 boss（洞洞板 Y bottom = -41.965, 外壳 -Y 内壁 = -45.035）
# 下半合盖靠 5mm lip 配合 + L 块定位
corner_boss_h_back = BACK_DEPTH - WALL - 0.5  # boss 顶距 seam 0.5mm
for sx in (-1, 1):
    sy = +1  # 只在 +Y 一侧
    cx = sx * corner_boss_x
    cy = sy * corner_boss_y
    boss = (
        cq.Workplane("XY")
        .center(cx, cy)
        .circle(CORNER_BOSS_DIA / 2)
        .extrude(corner_boss_h_back)
        .translate((0, 0, WALL))
    )
    # Drill M2 insert pilot from top
    boss = (
        boss.faces(">Z")
        .workplane()
        .circle(INSERT_HOLE_DIA / 2)
        .cutBlind(-INSERT_DEPTH)
    )
    back = back.union(boss)

# ============================================================
# 洞洞板固定：4 个塑料立柱 + M2 自攻螺丝 (方案 E)
# ============================================================
# 洞洞板 4 角现有孔实测 (2026-05-11):
#   ㉝ 孔径 = Φ2 mm
#   ㉞ 孔边缘到洞洞板边沿 = 2.4 mm → 孔中心到边沿 = PCB_MOUNT_HOLE_EDGE_DIST = 3.4mm
PCB_MOUNT_HOLE_OFFSET_X = PCB_W / 2 - PCB_MOUNT_HOLE_EDGE_DIST    # = 46.6
PCB_MOUNT_HOLE_OFFSET_Y = PCB_H / 2 - PCB_MOUNT_HOLE_EDGE_DIST    # = 31.6
# 立柱高度 = backside_h (从后壳内底立到洞洞板背面贴电池处)
PCB_POST_HEIGHT = backside_h

# 4 个立柱位置 (世界坐标)
pcb_post_positions = [
    (+PCB_MOUNT_HOLE_OFFSET_X, pcb_center_y + PCB_MOUNT_HOLE_OFFSET_Y),  # 上右
    (-PCB_MOUNT_HOLE_OFFSET_X, pcb_center_y + PCB_MOUNT_HOLE_OFFSET_Y),  # 上左
    (+PCB_MOUNT_HOLE_OFFSET_X, pcb_center_y - PCB_MOUNT_HOLE_OFFSET_Y),  # 下右
    (-PCB_MOUNT_HOLE_OFFSET_X, pcb_center_y - PCB_MOUNT_HOLE_OFFSET_Y),  # 下左
]

for px, py in pcb_post_positions:
    post = (
        cq.Workplane("XY")
        .center(px, py)
        .circle(PCB_POST_DIA / 2)
        .extrude(PCB_POST_HEIGHT)
        .translate((0, 0, WALL))
    )
    # M2 自攻底孔 (从立柱顶部往下钻)
    post = (
        post.faces(">Z")
        .workplane()
        .circle(PCB_POST_PILOT_DIA / 2)
        .cutBlind(-(PCB_POST_HEIGHT - PCB_POST_FLOOR))
    )
    back = back.union(post)

# 电池围栏（4 条短墙围电池外形）
BATT_FENCE_HEIGHT = 5.0
BATT_FENCE_THICK = 1.5
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
# 位置: 4 个 case 角落, 避开 PCB 立柱 (X=±46.6) + corner boss (Y=+34.5)
# Note: 这 4 个 Φ4 通孔轻微破坏防尘 seal, 但案 -Z 底面朝下,
#       灰尘不易直接进入. kart 实际使用 OK.
# ============================================================
mount_hole_offset_x = case_x / 2 - MOUNT_HOLE_OFFSET_FROM_EDGE   # = 48
mount_hole_offset_y = case_y / 2 - MOUNT_HOLE_OFFSET_FROM_EDGE   # = 42

for sx in (-1, 1):
    for sy in (-1, 1):
        mhx = sx * mount_hole_offset_x
        mhy = sy * mount_hole_offset_y
        # 通孔: 穿过 back floor (Z=[0, WALL] back local), 朝 -Z 方向 cut
        mount_hole = (
            cq.Workplane("XY")
            .center(mhx, mhy)
            .circle(MOUNT_HOLE_DIA / 2)
            .extrude(WALL + 1)
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
