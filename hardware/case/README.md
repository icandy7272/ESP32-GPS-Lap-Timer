# 外壳设计与打印（hardware/case）

3D 打印外壳的参数化建模 + 工作流。**主 CAD 用 Fusion 360**（你已经装了），所有尺寸沉淀
在 [`parameters.md`](parameters.md) 和 [`parameters.csv`](parameters.csv) 里——量完直接
对照表录入或 import，建模时引用参数名而不是硬编码数字。

## 文件结构

```
hardware/case/
├── README.md         ← 本文档（工作流）
├── parameters.md     ← 全部尺寸的真相清单 + 中文说明（人读）
├── parameters.csv    ← Fusion 360 直接 import 用（机器读）
├── case.scad         ← 历史包袱：原 OpenSCAD 模型，保留作设计意图参考文档
├── case_v1.f3d       ← Fusion 360 主源文件（建模后存进去）
├── case_v1.step      ← STEP 中性格式（git 友好的备份）
└── case_v1.stl       ← 上传 JLC 3DP 用的 mesh
```

## 工作流（Fusion 360 路径）

### 第 1 步：测量（30 分钟）

游标卡尺 + 一杯茶。把 [`parameters.md`](parameters.md) §1 的占位值全覆盖一遍。
**每项量 3 次取最大值**——FDM 容差喜欢宽松不要紧。

具体测哪些 → parameters.md §1 表格里每项都有注释说明。

### 第 2 步：把参数录入 Fusion 360（10 分钟）

打开 Fusion 360，新建一个空文档：

1. **Modify → Change Parameters**（或快捷键 `s` 输 "Parameter"）
2. 点 **+ User Parameter**
3. 逐项添加 parameters.md 里的参数（Name + Unit + Expression + Comment）
4. **§3 派生量**也录进去，Expression 引用前面的参数名

**如果你的 Fusion 360 版本支持 CSV import**（多数 Personal / Hobbyist license 都有）：

- Parameters 对话框右上角 → 文件夹图标 → Import
- 选 `parameters.csv`
- 32 项一次性导入，省 10 分钟手工

### 第 3 步：建模（建议顺序）

1. **底壳外壳**：草图画 `case_w × case_h` 矩形居中，Extrude `case_d * 0.7`
2. **挖空内腔**：Shell 操作，厚度 `WALL`，砸掉顶面
3. **TFT 窗口**：在前面贴一个草图，画 `TFT_VISIBLE_W + 2*TFT_BEZEL_INSET` × `TFT_VISIBLE_H + 2*TFT_BEZEL_INSET` 的矩形居中，Cut Through
4. **TFT 4 个螺柱**：基于 `TFT_SCREW_HOLE_DX` × `TFT_SCREW_HOLE_DY` 阵列，Extrude `SCREW_BOSS_DIA` 圆柱，再做 `SCREW_HOLE_DIA` 内孔
5. **侧面 USB-C 开口**：右侧面贴草图，矩形 `USBC_PORT_W × USBC_PORT_H` cut
6. **侧面按键孔（2 个）**：右侧面贴草图，圆 `BTN_PANEL_HOLE_DIA + PRINT_TOLERANCE` cut，两个间距按你实物布局
7. **背面 BOOT/RST 针孔（2 个 1.8mm 小孔）**：背面贴草图，两小圆 cut
8. **4 个角螺柱**：贴底面草图，4 个圆柱 + 内孔，给上盖固定螺丝用
9. **上盖**单独建一个 component：外形和底壳一样，下面 `LID_LIP` 长度做台阶配合，4 个对齐通孔给螺丝穿过
10. **干涉检查**：Inspect → Interference，确保所有内部组件之间没有挤压
11. **导出 STL**：右键 body → Save As Mesh → Format STL → 三角面精度 "High"
12. **保存源文件**：File → Save As → `case_v1.f3d`，commit 进 repo

### 第 4 步：下单或自打（3-5 天 / 一晚）

**外协（推荐前 3 版）：**

- **嘉立创 3D 打印**（jlc3dp.com） — 业内最便宜，PLA 单件 ~30 元、PETG ~50 元
- **打印宝**、淘宝小作坊 — 看店家口碑

**首版材料：PLA**（便宜易打、出问题不心疼）。30% 填充、0.2mm 层高、不开支撑。

**稳定后：PETG**（耐热 75-90°C、抗冲击好，符合 [`docs/PRD.md §设备物理结构`](../../docs/PRD.md)）。
30% 填充、0.2mm 层高、加 brim 防翘边。

### 第 5 步：试装 → 记录 → 迭代

打印件到手后**先空腔试装**（板子不上电，纯机械配合）：

- TFT 4 颗螺丝孔对得上吗？
- ESP32 USB-C / BOOT / RST 都摸得到吗？
- 18650 电池仓塞得进吗？
- M12 按键能从外侧穿过去并锁紧吗？
- 合盖时有没有线被夹？

**每个问题写一行到 `parameters.md` 文件底部新加的"FIT NOTES v1"段**——下次改的时候不用记忆。

第 1 版基本会有 3-5 个问题，正常。改完 → v2 重打，一般 3-4 版稳定。

### 第 6 步：稳定后做最终件

- PETG 重打一次
- 装上车开几圈做振动测试（[`TODOS.md`](../../TODOS.md) 里 Stage A 后续验证那段）
- 如果你的卡丁车环境湿 / 灰，外壳合缝可以加一道硅胶 O 圈密封槽

## 软件要求

| 工具 | 用途 | 状态 |
|------|------|------|
| **Fusion 360** | 主 CAD（建模 / 装配 / 干涉检查 / 导出 STL） | 你已装 |
| 切片软件 | STL → G-code（仅自打需要） | PrusaSlicer / Bambu Studio / Cura 任一，免费 |
| OpenSCAD | 历史包袱，可选 | macOS Sequoia 上 GUI 不放行，已不再当主工具 |

如果你外协打印（嘉立创 / 打印宝），只需要 Fusion 360。

## 为什么不再走 OpenSCAD

最初的 [`case.scad`](case.scad) 文件是 OpenSCAD 路径的产物（参数 + 几何 + 渲染同源）。
但 macOS 在 Sequoia 上对 2021.01 的 OpenSCAD 包不放行（签名 / 公证早过期），新版本暂无
官方 macOS 包。Fusion 360 你已经装了，建模能力和易用性更强。

`case.scad` 仍保留在 repo 里，作用：

1. **尺寸真相文档**：§1 是一份带详细注释的尺寸清单，与 `parameters.md` 是同一份数据的两种格式
2. **设计意图参考**：注释里讲了为什么这么设计（GPS 朝天 / 电池在底 / BOOT/RST 留针孔等）
3. **未来兜底**：如果以后 OpenSCAD 在 macOS 重新可用，或者想批量参数化生成几个变种，文件还在

**Fusion 360 是真相，case.scad 是参考——两边数字不同步时，以 Fusion 360 的 Parameters 为准**。

## 常见坑

| 坑 | 解决 |
|------|------|
| 螺丝孔太紧 / 螺丝拧裂 PLA | 孔径加 +0.3mm，配自攻螺丝（M2 自攻孔 1.7mm） |
| TFT 窗口边沿粗糙 | 窗口边再加 0.5mm 倒角，掩盖层纹 |
| 卡扣易断 | 用 M2 螺丝替代卡扣（设计已经这么做） |
| 平面翘边 | PLA 床温 50-60°C、PETG 75-85°C，加 brim |
| 桥接面下垂 | 跨度 ≤6mm 通常没事；超过加内部支撑或调整模型方向 |
| 0.5mm 以下细节糊 | 0.4mm 喷嘴极限是 0.5mm 壁厚，更细换 0.2mm 喷嘴 |
| GPS 信号变差 | 顶盖**绝对不要**贴金属铭牌、刷导电涂层、用碳纤维板 |

## 参考

- [`docs/PRD.md §设备物理结构`](../../docs/PRD.md) — 整体形态约束
- [`docs/WIRING.md §组装与振动加固`](../../docs/WIRING.md) — 装配前热熔胶清单
- [`parameters.md`](parameters.md) — 详细尺寸表 + 中文说明
- [`parameters.csv`](parameters.csv) — Fusion 360 import 格式
- [`case.scad`](case.scad) — OpenSCAD 历史包袱（仅作设计意图参考）
