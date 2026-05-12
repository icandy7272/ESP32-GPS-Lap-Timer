# 外壳 3D 打印指南

> 配合 `hardware/case/preview/build_v3.py` 输出的 STL 文件使用。
> 测试材料: PETG (推荐) / PLA。其他材料未验证。

## 1. 准备文件

```bash
cd hardware/case/preview
python3 build_v3.py
```

产物（在 `hardware/case/preview/` 目录下）：

| 文件 | 用途 |
|---|---|
| `case_v3_front.stl` | 前壳 (橙色, 含 TFT 窗 + 4 个 M3 螺柱 + lip + 防翘凸条) |
| `case_v3_back.stl` | 后壳 (灰色, 含按键孔 + 4 个 PCB 立柱 + 2 个 M2 嵌件孔 + 电池围栏 + 防翘凹槽) |
| `case_v3_assembled.step` | 装配预览 (Fusion 360 检查用, 不用于打印) |
| `case_v3_exploded.step` | 爆炸视图 (Fusion 360 检查用) |
| `case_v3_front.step` / `case_v3_back.step` | 单体 STEP (CAM/SLA 备用) |

## 2. 切片参数 (Bambu / Cura / PrusaSlicer 通用)

### 必须 / 关键

| 参数 | 推荐值 | 备注 |
|---|---|---|
| **喷嘴** | 0.4mm | lip 壁厚 1mm = 2.5 perimeter, 0.4mm 喷嘴勉强够 |
| **层高** | 0.2mm | 凸条 1mm 厚 = 5 层, ridge geometry 能 resolve |
| **壁数 (perimeters / walls)** | **4** | lip 壁厚 1mm, 4 perimeter = 1.6mm wall thickness, 把 lip 整个填实 |
| **底层数 (top/bottom layers)** | 4 | 防止 case 顶/底面透光 + 强度 |
| **填充** | 30% gyroid 或 cubic | 强度足够 + 重量不大 (250g case 目标) |
| **打印温度 (PETG)** | 240°C | PETG 标准 |
| **热床 (PETG)** | 80°C | 防翘 |
| **打印温度 (PLA)** | 215°C | 备选 |
| **热床 (PLA)** | 60°C | 备选 |
| **冷却风扇** | PETG: 30-50% / PLA: 100% | PETG 弱化层间结合, 减少风扇 |

### 关键: 关掉这些

- ❌ **支撑 (supports)** — 设计零悬空, 不需要支撑。如果切片器误判 lip 内腔需要支撑, 关掉
- ❌ **Brim** — 会增加 lip 末端 X-Y 尺寸 0.5mm, 影响装配。不要 brim, 改用 raft (会被切走) 或直接打
- ❌ **缝隙补偿 (line width tweak)** — 用 slicer 默认值 0.42mm, 不要手动调

### 推荐: 这些可以打开

- ✅ **回抽 (retraction)** — PETG: 4-5mm @ 35mm/s; PLA: 6mm @ 40mm/s
- ✅ **Z-seam alignment: random** — 让接缝点分布在各层, 减少弱点
- ✅ **Adaptive layers (自适应层高)** — TFT 玻璃面顶部用 0.12, lip 区段用 0.2, 加快总时间

## 3. 打印朝向 (CRITICAL)

### 前壳 (`case_v3_front.stl`)

```
┌──────────────────────┐   ← TFT 玻璃面朝下 (面对打印板)
│   TFT 玻璃面 (-Z)    │     这样 TFT 窗口顶面 layer 0 = 最平
│   ↓↓↓ down ↓↓↓       │     避免支撑撑 TFT 窗口内侧
├──────────────────────┤
│   case body          │
│   ↑↑↑ up ↑↑↑         │
│   lip (8mm) 朝上      │     lip 朝上避免悬空 / 支撑
│   ridge 凸条朝 -Y     │
│   C0.3 末端倒角       │
└──────────────────────┘
```

**朝向**: TFT 玻璃面 (= 前壳 +Z 外面) 朝下 (= 接触打印板)。

**理由**:
- TFT 窗口在 +Z 外表面, 朝下打印让 layer 0 fillets 整个窗口边缘 = 最平
- lip 朝 +Z 打印, 4 个 lip 外壁是 vertical, 无悬空
- TFT 4 个 M3 螺柱朝下 = 悬空, 但是直径 5.5mm 短柱体打印不需要支撑

### 后壳 (`case_v3_back.stl`)

```
┌──────────────────────┐   ← 内腔开口朝上 (open top)
│   ↑↑↑ up ↑↑↑         │     PCB 立柱 + 电池围栏 + 嵌件孔 全部朝上
│   open cavity        │
│   ridge groove (-Y)  │
├──────────────────────┤
│   back floor (-Z)    │   ← 后壳底面朝下 (接触打印板)
│   ↓↓↓ down ↓↓↓       │
└──────────────────────┘
```

**朝向**: 后壳底面朝下 (= -Z 外面 = case 底)。内腔朝上。

**理由**:
- 后壳底面是最大平面, 朝下打印稳
- 4 个 PCB 立柱 (Φ4.5 × 23mm) 朝上, 自下而上打印, 无悬空
- 2 个 M2 嵌件孔 (corner boss) 朝上打印
- 凹槽 (在 -Y inner wall) 是 horizontal slot, 不需要支撑
- 凹槽开口朝 +Y (内腔方向), 朝上印, 槽底 (= -Y wall) 是 vertical, OK

## 4. 后处理 (Post-print)

### A. 嵌件热熔 (Heat-set M2 brass insert)

后壳上 2 个 corner boss 有 Φ3.5mm 嵌件孔 (深 5mm)。把 M2 黄铜嵌件 (`INSERT_HOLE_DIA = 3.5`, OD 3.5mm, 螺纹 M2) 用烙铁 (~280°C PETG / 250°C PLA) 压入孔, 让嵌件跟塑料融合。

**工具**: 烙铁头改造 / 嵌件专用工具 (淘宝 ~10 元)。
**手动检查**: 嵌件压入后跟孔顶齐平, 不要凸出也不要凹进。

### B. lip 滑配测试

front + back 分别打完, 先**不装电子件**, 直接合盖测试 lip 是否滑入:

| 现象 | 原因 | 解决 |
|---|---|---|
| ✅ lip 顺畅滑入, 0.5mm 内合上 | 间隙 OK | 进行下一步 |
| ⚠️ lip 卡住, 需要用力按下 | 单边间隙 < 0.07 实际 (FDM 太胖) | 改 `LID_LIP_GAP = 0.20` (= 单边 0.10), 重新打 |
| ⚠️ 凸条 ridge 卡在 inner_wall 入口 | C0.3 lead-in chamfer 不够大 | 改 `LIP_END_CHAMFER = 0.4`, 重新打 |
| ❌ lip 完全装不进 | 单边间隙 实际 < 0 (重叠) | 改 `LID_LIP_GAP = 0.25 ~ 0.30`, 重新打 |
| ⚠️ lip 装入后晃动 | 单边间隙 > 0.20 (FDM 太瘦) | 改 `LID_LIP_GAP = 0.10`, 重新打 |

### C. 凸条 ↔ 凹槽 配合测试

lip 装入后, 凸条 (front -Y lip 外侧) 应该卡在凹槽 (back -Y inner_wall 上 [2.0, 3.2] Z 位置 段) 里:

| 现象 | 原因 | 解决 |
|---|---|---|
| ✅ 凸条 click 进凹槽, 拆装 N 次顺滑 | 设计 OK | ✓ |
| ⚠️ 凸条没卡进, 装好后 lip 能上下晃 1mm | 凸条 high < 0.3 实际, 或凹槽深 < 0.35 实际 | 改 `RIDGE_HEIGHT = 0.4` 或 `GROOVE_DEPTH = 0.45` |
| ⚠️ 装配时 凸条 顶 在 inner_wall 顶, 装不进 | 40° lead-in 不够 / 凸条 Z 位置太高 | 改 `RIDGE_CHAMFER_DROP = 0.4`, 或 `RIDGE_TOP_FROM_LIP_TOP = 2.5` |

### D. PCB 立柱试装

4 个立柱 (Φ4.5 × 23mm) 放洞洞板进去, M2 自攻螺丝 (6mm) 拧入立柱顶 Φ1.8 pilot:

| 现象 | 原因 | 解决 |
|---|---|---|
| ✅ M2 自攻 拧入顺畅, 锁紧后洞洞板不晃 | OK | ✓ |
| ⚠️ M2 自攻拧坏, 立柱破裂 | 立柱 PETG / PLA 自攻孔太紧 | 改 `PCB_POST_PILOT_DIA = 2.0` (= M2 螺纹外径 = 标准自攻松配) |
| ⚠️ 立柱 装好后 倾斜 1° | 立柱 height aspect ratio 5:1 太瘦 + FDM warp | 加冷却风扇时间 + 减慢打印速度 25%, 或换 thicker post (改 `PCB_POST_DIA = 5.5`) |

### E. M12 按键孔试装

后壳 +Y 上 2 个 Φ12.88mm 按键孔, 按 M12 金属按键试装:

| 现象 | 解决 |
|---|---|
| ✅ 按键螺纹 滑入孔, 螺母从背面拧紧 | ✓ |
| ⚠️ 按键 jam, 孔太小 | `BTN_PANEL_HOLE_DIA = 13.0` |
| ⚠️ 按键松动 | `BTN_PANEL_HOLE_DIA = 12.5` |

## 5. 装配顺序

```
1. 打 front + back 各一个
2. 后壳: 装 2 个 M2 嵌件 (heat-set)
3. 前壳: 装 TFT (4 个 M3 自攻 6mm 螺丝)
4. 后壳: 焊好的洞洞板 → 4 个塑料立柱 + M2 自攻 6mm 螺丝
5. 后壳: 18650 电池组 → 滑入电池围栏 (BATT_FENCE_GAP=0.3 间隙)
6. 后壳: 2 个 M12 按键 + 螺母 拧紧
7. lid 装入 base: lip 滑入, ridge 卡进 groove
8. 上盖: 2 颗 M2 内六角螺丝 (从前壳 +Z 沉头 → 穿过 lip → 嵌件)
9. 校验: TFT 排针 卡进洞洞板母座 (轻按压前壳让 TFT 跟洞洞板 connect)
10. kart 安装: 4 个 Φ4mm 通孔 (case 后壳底面 4 角) 穿 cable tie 或 M3 螺丝
              位置: (±49, ±43) 相对 case 中心, 跟 PCB 立柱有 0.82mm 间隙余量
```

## 5b. 挂载到 kart

后壳底面 (= -Z 外表面) 有 **4 个 Φ4mm 通孔** at 4 corners, 位置 `(±49, ±43)`:

- **Cable tie 安装**: 通孔过 cable tie (5mm 内宽适配), 绑到方向盘后部 / 转向柱
- **螺丝固定**: M3 螺丝 (Φ3 螺纹) 通过 Φ4 孔 (留 0.5mm 间隙), 拧入 kart 支架
- **VHB 双面胶**: 不用通孔, 直接用 3M 双面胶贴在底面 (Φ4 孔不影响)

⚠️ 通孔轻微破坏防尘 seal. 案 -Z 底面朝下不直接接灰尘, 实际使用 OK. 如果担心:
- 通孔 4 个填 RTV silicone (打完密封)
- 或仅用 +Y 端 2 个孔, 留 -Y 端 2 个孔自然封闭

## 6. 参数调整快查表

打印出来不 fit 时, 调以下参数后重新跑 `build_v3.py`:

| 调谁 | 当前值 | 影响 |
|---|---|---|
| `LID_LIP_GAP` | 0.14 | 普通止口 (顶+左+右) 总间隙. 增加 → lip 滑入更松 |
| `LID_LIP_GAP_BOT` | 0.20 | 底部止口 (-Y) 总间隙. 增加 → 凸条 ↔ 凹槽更松 |
| `RIDGE_HEIGHT` | 0.3 | 凸条 -Y 凸出量. 增加 → 锁更牢但装配更紧 |
| `GROOVE_DEPTH` | 0.35 | 凹槽深度. 增加 → 凸条到底间隙更大 |
| `LIP_END_CHAMFER` | 0.3 | lip 末端 4 边倒角. 增加 → 装配引导更好 |
| `RIDGE_CHAMFER_DROP` | 0.3 | 40° 导入斜面尺寸. 增加 → 凸条更易滑入凹槽 |
| `PRINT_TOLERANCE` | 0.4 | 未使用此处 (M3 孔 / 通孔间隙) |

## 6b. FDM 结构友好性设计 (已实施)

| 特征 | 实现 | 收益 |
|---|---|---|
| **PCB 立柱方形** (4.5×4.5, 圆形 pilot) | `rect()` 代替 `circle()` | 4 个垂直棱角抗 warp, slicer 直线 perimeter, 壁厚均匀 |
| **Corner boss 方形** (6.5×6.5, 圆形 insert 孔) | 同上 | 嵌件压入时方壁受力均匀, 不会椭圆变形 |
| **电池围栏 2.0mm** (原 1.5) | `BATT_FENCE_THICK = 2.0` | 4 perimeter (4 × 0.42 = 1.68mm) 完全填实 |
| **lip 末端 C0.3 倒角** | 已加 | 装配 lead-in, 避免 sharp corner 撞 base 入口 |
| **40° ridge lead-in chamfer** | 已加 | ridge 滑进 groove 平顺 |
| **R0.2 groove fillet 删除** | 已删 | <0.4mm FDM 无法分辨, 不浪费 code |

## 6c. 未来 prototype 后再 review

如果打印或装配遇到以下 failure mode, 再做对应优化:

| 现象 | 优化项 | 怎么改 |
|---|---|---|
| 立柱倾斜 > 1° | 加粗 PCB post | 改 `PCB_POST_DIA = 5.5` (需 mount hole Z 范围裁剪) |
| 立柱根部断裂 | 加底部 R fillet | cadquery edge select 选 post-floor 边, fillet R0.5 |
| Case 外观 sharp corner 不舒服 | 外角 R2 fillet | 在 front/back 外壳上加 R2 outer corner fillet |
| TFT 玻璃顶撞前壳内表面 | 增加 AIR_GAP_TOP | 改 `AIR_GAP_TOP = 1.5` |
| 电池底贴 后壳 内底 (FDM 翘) | 已加 0.5 gap | 已经 `AIR_GAP_BOT = 0.5` ✓ |
| 4 个 mount hole 漏灰尘 | 通孔 RTV silicone 填封 | 物理打完后封 |

## 7. 典型打印时间 (参考)

| 模式 | front | back | 总 |
|---|---|---|---|
| 0.4mm 喷嘴 / 0.2 层高 / 30% 填充 / 60mm/s | ~2.5h | ~6h | **~8.5h** |
| 0.4mm 喷嘴 / 0.12 层高 / 30% 填充 / 50mm/s (高质量) | ~5h | ~12h | **~17h** |

材料用量: ~50g front + ~120g back = **~170g** PETG / 卷。

## 8. 故障排查

### 翘边 (corner lifting)

- 加大 brim — 但会影响 lip 装配, 不推荐
- 改用 raft, 印完后切走
- PETG: 热床 80°C 不变, 看是否第一层 z-offset 不准
- 关掉风扇 (PETG 第一层)

### Layer adhesion 不好 (层间脱开)

- PETG: 减小风扇到 30%, 升高喷嘴 245°C
- 增加 pressure advance / linear advance

### TFT 窗口边缘毛刺

- 切片器 retract on perimeter ON
- Speed 减慢 30% (TFT 窗口边缘)

### lip 印出来椭圆 / 歪

- 检查 X-Y 轴 calibration
- 减慢 outer perimeter 30%
- 增加打印件高度方向的稳定 (避免 case_y 94mm 朝向 + 风扇直吹一面)
