# Fusion 360 User Parameters — 外壳尺寸

把这张表整体导入或者照抄进 Fusion 360 的 **Modify → Change Parameters → +User Parameter**。
所有占位值都是 spec sheet 的标称值或者从照片估算——**用游标卡尺测一遍，把不准的覆盖掉**。

> **Tip：** 在 Fusion 360 草图标注尺寸时，直接键入参数名（比如 `TFT_VISIBLE_W`），不要键入数字。
> 这样所有几何都自动跟着 Parameters 联动，改一个数字整个模型重生成。

## 1. 已测量尺寸（占位值，等你用卡尺覆盖）

### 1.1 TFT 模块（3.2" ZJY320S0800TG02）

| Name | Value | Comment |
|------|-------|---------|
| `TFT_PCB_W` | 89.4 mm | PCB long edge (spec) |
| `TFT_PCB_H` | 56.0 mm | PCB short edge (spec) |
| `TFT_PCB_THICKNESS` | 1.6 mm | Bare PCB thickness |
| `TFT_BACK_HEIGHT` | 8.0 mm | Tallest component on TFT back side（背光升压电感 / 电容） |
| `TFT_VISIBLE_W` | 64.8 mm | Visible glass long edge (spec) |
| `TFT_VISIBLE_H` | 48.6 mm | Visible glass short edge (spec) |
| `TFT_VISIBLE_OFFSET_X` | 12.3 mm | 可视区左边距 PCB 左边的距离（如果居中 = (89.4-64.8)/2） |
| `TFT_VISIBLE_OFFSET_Y` | 3.7 mm | 可视区上边距 PCB 上边的距离 |
| `TFT_SCREW_HOLE_DIA` | 2.6 mm | M2.5 安装孔；建模时 +0.4mm 做间隙配合 |
| `TFT_SCREW_HOLE_DX` | 83.4 mm | 4 个安装孔在长边方向的孔距 |
| `TFT_SCREW_HOLE_DY` | 50.0 mm | 4 个安装孔在短边方向的孔距 |

### 1.2 ESP32-S3 DevKit + 洞洞板

| Name | Value | Comment |
|------|-------|---------|
| `PCB_W` | 70.0 mm | 洞洞板长边 |
| `PCB_H` | 50.0 mm | 洞洞板短边 |
| `PCB_THICKNESS` | 1.6 mm | 洞洞板厚度 |
| `PCB_USB_C_X` | 17.0 mm | USB-C 接口中心距 PCB 左边距离 |
| `PCB_USB_C_Z_OFFSET` | 1.5 mm | USB-C 中心线距 PCB 顶面高度 |
| `PCB_BOOT_BTN_X` | 25.0 mm | BOOT 按钮中心距 PCB 左边 |
| `PCB_RST_BTN_X` | 32.0 mm | RST 按钮中心距 PCB 左边 |
| `PCB_BOOT_RST_Y` | 6.0 mm | BOOT/RST 按钮共用的 Y 距板边 |
| `PCB_BTN_TOP_Z` | 3.0 mm | 微动按钮帽高于 PCB 表面 |

### 1.3 18650 电池组 + USB-C 充电小板

| Name | Value | Comment |
|------|-------|---------|
| `BATT_W` | 70.0 mm | 电池组长边 |
| `BATT_H` | 38.0 mm | 电池组高（单节 ~19，2P ~38） |
| `BATT_D` | 19.0 mm | 电池组厚 |
| `USBC_DAUGHTER_W` | 25.0 mm | 充电小板外形长 |
| `USBC_DAUGHTER_H` | 15.0 mm | 充电小板外形高 |
| `USBC_PORT_W` | 9.0 mm | USB-C 插头开口宽 |
| `USBC_PORT_H` | 3.5 mm | USB-C 插头开口高 |

### 1.4 BK-880 GPS 模块

| Name | Value | Comment |
|------|-------|---------|
| `GPS_MODULE_W` | 28.0 mm | GPS 模块长 |
| `GPS_MODULE_H` | 28.0 mm | GPS 模块宽 |
| `GPS_PATCH_HEIGHT` | 7.0 mm | 陶瓷贴片天线高出其 PCB 的高度 |
| `GPS_PATCH_HEADROOM` | 3.0 mm | 贴片天线上方留空（RF 间隙，**绝不放金属**） |

### 1.5 12mm 金属面板按键（电源 + REC 共 2 个）

| Name | Value | Comment |
|------|-------|---------|
| `BTN_PANEL_HOLE_DIA` | 13.0 mm | 面板开孔直径（12mm 螺纹 + 1mm 间隙） |
| `BTN_BACKSIDE_DEPTH` | 25.0 mm | 按键后端总长（含连接片），外壳内侧需要这么多空间 |

## 2. 设计参数（不用测，按需调）

| Name | Value | Comment |
|------|-------|---------|
| `WALL` | 2.0 mm | 外壳壁厚 |
| `LID_LIP` | 4.0 mm | 上盖与底壳台阶配合深度 |
| `LID_LIP_GAP` | 0.3 mm | 上盖滑入间隙 |
| `PRINT_TOLERANCE` | 0.4 mm | FDM 通用孔位放大量（滑动配合） |
| `SCREW_BOSS_DIA` | 5.5 mm | M2 螺柱外径 |
| `SCREW_HOLE_DIA` | 1.7 mm | M2 自攻螺纹底孔 |
| `PCB_STANDOFF_H` | 4.0 mm | PCB 与内壳底之间的间隙（走线 / 散热） |
| `TFT_BEZEL_INSET` | 1.0 mm | TFT 窗口边框相对可视区的悬出（视觉收边） |

## 3. 派生量（Fusion 360 里写表达式）

在 Fusion 360 的 Parameters 面板里，下面这些 Expression 直接引用上面的参数。改上面、自动重算下面：

| Name | Expression | Comment |
|------|-----------|---------|
| `inner_w` | `max(TFT_PCB_W; PCB_W; BATT_W) + 2 mm` | 内腔长（取最长那块板 + 余量） |
| `inner_h` | `max(TFT_PCB_H; PCB_H; BATT_H) + 2 mm` | 内腔宽 |
| `inner_d` | `TFT_PCB_THICKNESS + TFT_BACK_HEIGHT + PCB_STANDOFF_H + PCB_THICKNESS + BATT_D + GPS_PATCH_HEIGHT + GPS_PATCH_HEADROOM` | 内腔深（叠层之和） |
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
