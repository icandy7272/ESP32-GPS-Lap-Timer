# Fusion 360 User Parameters — 外壳尺寸

把这张表整体导入或者照抄进 Fusion 360 的 **Modify → Change Parameters → +User Parameter**。
所有占位值都是 spec sheet 的标称值或者从照片估算——**用游标卡尺测一遍，把不准的覆盖掉**。

> **Tip：** 在 Fusion 360 草图标注尺寸时，直接键入参数名（比如 `TFT_VISIBLE_W`），不要键入数字。
> 这样所有几何都自动跟着 Parameters 联动，改一个数字整个模型重生成。

## 1. 已测量尺寸（占位值，等你用卡尺覆盖）

### 1.1 TFT 模块（3.2" ZJY320S0800TG02）

| Name | Value | Comment |
|------|-------|---------|
| `TFT_PCB_W` | 89.45 mm | PCB long edge（**实测 2026-05-11**；spec 89.40 ±0.20） |
| `TFT_PCB_H` | 56.01 mm | PCB short edge（**实测 2026-05-11**；spec 56.00 ±0.20） |
| `TFT_PCB_THICKNESS` | 1.17 mm | Bare PCB thickness（**实测 2026-05-11**；spec 未单独标，§5 图里 1.30 是含玻璃截面） |
| `TFT_BACK_HEIGHT` | 8.28 mm | TFT 背面最高凸起（**实测 2026-05-11**；当前是 8-pin 直插排针根部，未来若改弯针 / 排线可降到 ~4mm，外壳深度可省 4mm） |
| `TFT_FRONT_HEIGHT` | 3.83 mm | TFT 正面玻璃+偏光片+LCM 框架凸出 PCB 正面（**实测 2026-05-11**；spec §3 标 3.75 MAX，吻合）。外壳上盖内表面到 PCB 正面的最小净空 |
| `TFT_VISIBLE_W` | 67.53 mm | 玻璃外形长边（**实测 2026-05-11**；spec §3 AA=64.80 是发光区，玻璃比 AA 大 ~1.4mm/侧。**外壳窗口按玻璃外形开，不能用 AA**） |
| `TFT_VISIBLE_H` | 52.07 mm | 玻璃外形短边（**实测 2026-05-11**；spec §3 AA=48.60） |
| `TFT_VISIBLE_OFFSET_X` | 7.5 mm | 玻璃左沿到 PCB 左沿（沿长边方向，**实测 2026-05-11**；非居中——另一侧 14.42mm。外壳窗口要按非居中开） |
| `TFT_VISIBLE_OFFSET_Y` | 2.37 mm | 玻璃上沿到 PCB 上沿（沿短边方向，**实测 2026-05-11**；另一侧 1.57mm） |
| `TFT_SCREW_HOLE_DIA` | 3.01 mm | **M3** 安装孔（**实测 2026-05-11**；spec §5 标 Φ2.50 是 M2.5，但实物是 M3——厂家未更新图纸）。建模间隙孔 = 3.01 + PRINT_TOLERANCE = 3.4mm |
| `TFT_SCREW_HOLE_DX` | 83.42 mm | 4 个安装孔在长边方向的孔距（**实测 2026-05-11**；spec 83.40 ±0.20 ✓） |
| `TFT_SCREW_HOLE_DY` | 50.07 mm | 4 个安装孔在短边方向的孔距（**实测 2026-05-11**；spec 50.00 ±0.20 ✓） |
| `TFT_OFFSET_LEFT` | 8.56 mm | TFT PCB 左边沿到洞洞板左边沿的距离（**实测 2026-05-11**；TFT 不居中洞洞板，偏右 ~3.3mm） |
| `TFT_OFFSET_TOP` | 12.9 mm | TFT PCB 上边沿到洞洞板上边沿的距离（**实测 2026-05-11**；TFT 偏下 ~5.9mm，远离 GPS 那一边） |
| `TFT_BACK_TO_PERFBOARD_GAP` | 10.56 mm | TFT PCB 背面 → 洞洞板正面 的总 Z 距离（**实测 2026-05-12**）。包括: 母座 (排母) 高 + TFT 排针根部塑料 + 任何空气间隙。注意 ≠ TFT_BACK_HEIGHT（后者 8.28 只是 TFT 自己的排针凸出 PCB 背面的高度，10.56 - 8.28 = 2.28mm 是 母座底部被塑料占去的不能被排针填的高度）|

### 1.2 ESP32-S3 DevKit + 洞洞板

| Name | Value | Comment |
|------|-------|---------|
| `PCB_W` | 100.0 mm | 洞洞板长边（**实测 2026-05-11**） |
| `PCB_H` | 70.0 mm | 洞洞板短边（**实测 2026-05-11**） |
| `PCB_THICKNESS` | 1.5 mm | 洞洞板厚度（**实测 2026-05-11**） |
| `ESP32_BOT_TO_PERFBOARD_BACK` | 15.63 mm | ESP32-S3 DevKit 最远面（朝后壳方向, 含 USB-C jack / 排针 / DevKit 模块本体）→ 洞洞板背面 Z 距离（**实测 2026-05-12**）。ESP32 比电池低: 15.63 < BATT_BOT_TO_PERFBOARD_BACK(23.11), 所以 backside_h 由电池决定, ESP32 不会被后壳挤压 |
| `PCB_USB_C_X` | 17.0 mm | USB-C 接口中心距 PCB 左边距离 |
| `PCB_USB_C_Z_OFFSET` | 1.5 mm | USB-C 中心线距 PCB 顶面高度 |
| `PCB_BOOT_BTN_X` | 25.0 mm | BOOT 按钮中心距 PCB 左边 |
| `PCB_RST_BTN_X` | 32.0 mm | RST 按钮中心距 PCB 左边 |
| `PCB_BOOT_RST_Y` | 6.0 mm | BOOT/RST 按钮共用的 Y 距板边 |
| `PCB_BTN_TOP_Z` | 3.0 mm | 微动按钮帽高于 PCB 表面 |

### 1.3 18650 电池组 + USB-C 充电小板

| Name | Value | Comment |
|------|-------|---------|
| `BATT_W` | 40.78 mm | 电池组沿外壳 X 方向（短轴）(**实测 2026-05-11**, CK18650-1234S-NP 5V 5600mAh) |
| `BATT_H` | 71.07 mm | 电池组沿外壳 Y 方向（长轴，竖放）(**实测 2026-05-11**) |
| `BATT_D` | 19.50 mm | 电池组厚度（朝洞洞板背面外凸出）(**实测 2026-05-11**) |
| `BATT_BOT_TO_PERFBOARD_BACK` | 23.11 mm | 洞洞板背面 → 电池底面 的 Z 距离（**实测 2026-05-12**）。**电池底不贴洞洞板背面** — 23.11 - BATT_D(19.50) = 3.61mm 是线材/焊点/泡棉占据的空间。这是 backside_h 的真实值（取代旧的 max(BATT_D, ESP32...) = 19.5 错误估算）|
| `USBC_DAUGHTER_W` | 25.0 mm | 充电小板外形长 |
| `USBC_DAUGHTER_H` | 15.0 mm | 充电小板外形高 |
| `USBC_PORT_W` | 9.0 mm | USB-C 插头开口宽 |
| `USBC_PORT_H` | 3.5 mm | USB-C 插头开口高 |

### 1.4 BK-880 GPS 模块

| Name | Value | Comment |
|------|-------|---------|
| `GPS_MODULE_W` | 28.0 mm | GPS 模块金属罩长边（**实测 2026-05-11**，与 spec 一致） |
| `GPS_MODULE_H` | 28.0 mm | GPS 模块金属罩短边（**实测 2026-05-11**） |
| `GPS_PATCH_HEIGHT` | 11.0 mm | GPS 模块整体厚度（陶瓷天线 + 金属罩）(**实测 2026-05-11**) |
| `GPS_PATCH_HEADROOM` | 3.0 mm | 贴片天线上方留空（RF 间隙，**绝不放金属**） |
| `GPS_PROTRUSION_Y` | 14.3 mm | GPS 模块凸出洞洞板顶边的距离（沿外壳 Y 方向）(**实测 2026-05-11**, 含焊接斜导致 ESP32 端凸出) |
| `GPS_LEFT_TO_PCB_LEFT` | 51.0 mm | GPS 模块左沿到洞洞板左沿（**实测 2026-05-11**；GPS 偏右洞洞板中心 +15mm，导致按键不能放 X=±28，已改为 X=±37 避开 GPS） |

### 1.5 12mm 金属面板按键（电源 + REC 共 2 个）

| Name | Value | Comment |
|------|-------|---------|
| `BTN_PANEL_HOLE_DIA` | 13.0 mm | 面板开孔直径（12mm 螺纹 + 1mm 间隙） |
| `BTN_BACKSIDE_DEPTH` | 13.0 mm | 按键背深（**实测 2026-05-11**，螺纹 10.92 + 螺母 ~2mm；线材需侧出避免占用 Y 方向） |
| `BTN_THREAD_DIA` | 11.88 mm | 按键螺纹外径（**实测 2026-05-11**，M12 实物略小于标称） |
| `BTN_PANEL_HOLE_DIA` | 12.88 mm | 外壳按键孔径（螺纹 11.88 + 1mm 间隙，建模时再 +0.4 印刷余量 = 13.28） |
| `BTN_FLANGE_DIA` | 13.83 mm | 按键法兰盘（按帽下面那个圆盘）直径（**实测 2026-05-11**） |
| **按键位置** | +Y 顶面，X=±43 | 按键朝车顶（+Y 面），X 在洞洞板左右边缘附近 ±43。接线柱沿 -Y 自然延伸，X 跟电池 [-4.78, +36] 和 ESP32 [-30.23, -5.23] 都不重叠（设计 2026-05-11） |
| `BATT_LEFT_TO_PCB_LEFT` | 14.0 mm | 电池塑料封装左沿到洞洞板左沿（沿 X，**实测 2026-05-11**, 推得电池中心相对洞洞板中心 +15.61，与 GPS 同侧 +X） |
| `ESP32_LEFT_TO_PCB_LEFT` | 55.23 mm | ESP32 DevKit PCB 左沿到洞洞板左沿（**实测 2026-05-11**, 推得 ESP32 中心 -17.73，在电池左侧） |
| `BATT_TOP_TO_PCB_TOP` | 1.0 mm | 电池上沿到洞洞板上沿（沿 Y，**实测 2026-05-11**, 含线材误差；电池基本贴洞洞板顶端） |
| `STACK_MEASURED_Z` | 39.0 mm | 整机焊好后 Z 方向总厚度（**实测 2026-05-11**, TFT 玻璃顶到电池底）。外壳 inner_z 取此值 + 余量 |

## 2. 设计参数（不用测，按需调）

| Name | Value | Comment |
|------|-------|---------|
| `WALL` | 2.0 mm | 外壳壁厚 |
| `LID_LIP` | 5.0 mm | 前壳 lip 凸出长度（插入后壳的深度，v3 用 5mm） |
| `LID_LIP_GAP` | 0.3 mm | lip 滑入间隙 |
| `PRINT_TOLERANCE` | 0.4 mm | FDM 通用孔位放大量（滑动配合） |
| `CORNER_BOSS_DIA` | 6.5 mm | 4 角螺柱外径（M2 嵌件用） |
| `INSERT_HOLE_DIA` | 3.5 mm | M2 黄铜热熔嵌件底孔直径（淘宝 ~3 元/10 个） |
| `INSERT_DEPTH` | 5.0 mm | 嵌件压入深度 |
| `M2_THROUGH_DIA` | 2.4 mm | M2 螺丝通孔 + 间隙（前壳穿过） |
| `M2_HEAD_DIA` | 4.5 mm | M2 内六角圆柱头沉孔直径 |
| `TFT_BEZEL_INSET` | 1.0 mm | TFT 窗口边框相对可视区的悬出（视觉收边） |

## 3. 派生量（Fusion 360 里写表达式）

在 Fusion 360 的 Parameters 面板里，下面这些 Expression 直接引用上面的参数。改上面、自动重算下面：

| Name | Expression | Comment |
|------|-----------|---------|
| `inner_w` | `max(TFT_PCB_W; PCB_W; BATT_W) + 2 mm` | 内腔长（取最长那块板 + 余量） |
| `inner_h` | `max(TFT_PCB_H; PCB_H; BATT_H) + 2 mm` | 内腔宽 |
| `inner_d` | `TFT_FRONT_HEIGHT + TFT_PCB_THICKNESS + TFT_BACK_HEIGHT + PCB_STANDOFF_H + PCB_THICKNESS + BATT_D + GPS_PATCH_HEIGHT + GPS_PATCH_HEADROOM` | 内腔深（叠层之和，从上盖内表面到底壳内表面） |
| `case_w` | `inner_w + 2 * WALL` | 外壳长 |
| `case_h` | `inner_h + 2 * WALL` | 外壳宽 |
| `case_d` | `inner_d + 2 * WALL` | 外壳深 |

> **Fusion 360 函数语法注意**：`max()` 用分号分隔参数（不是逗号），表达式里乘号是 `*`，单位标注末尾。

## 4. CSV 直接 import 选项

如果 Fusion 360 版本支持参数 CSV 导入（**Modify → Change Parameters → 文件夹图标 → Import**）：

```bash
# 在 Fusion 360 Parameters 对话框里点 import 按钮
# 选 hardware/case/parameters.csv
```

CSV 列序：`Name,Unit,Expression,Comment`，无 header 行。

如果你的版本不支持 CSV 导入（比如 Education 或较旧的 Personal license），就照本文档手工录入——一共 32 项已测尺寸 + 8 项设计参数，10 分钟搞完。
