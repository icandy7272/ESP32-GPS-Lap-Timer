# UI Preview Workbench 设计文档

**日期**: 2026-04-08  
**状态**: 已确认设计，待进入实现计划  
**对应目标**: 让项目现有两套 UI 都能在电脑浏览器中直接验收与优化

## 1. 背景

当前项目里存在两套需要验收的 UI：

1. **Web 控制台**
   由 [`src/wifi_server.cpp`](../../../src/wifi_server.cpp) 直接拼接 HTML/CSS/JS，通过 ESP32 AP 提供页面与 `/api/*` 接口。
2. **设备屏幕 UI**
   由 [`src/display.cpp`](../../../src/display.cpp) 渲染到 320x240 TFT 屏幕，包含开机流程与运行时 3 个主页面。

当前问题不是“没有 UI”，而是 **UI 很难在电脑上集中查看、切场景、做验收**：

- Web 控制台依赖设备热点与接口返回
- 设备屏幕 UI 运行在 `TFT_eSPI` 上，无法直接在电脑浏览器中查看
- 要做 UI 验收时，需要反复烧录、连设备、切真实状态，成本高

本设计的目标是增加一套 **本地浏览器预览工作台**，让我们不依赖真机就能查看当前 UI 全貌，并作为后续 UI 优化的固定入口。

## 2. 目标

本次设计要实现的目标：

1. 在电脑浏览器中提供一个统一入口，同时预览 Web 控制台和设备屏幕 UI。
2. 通过共享场景数据快速切换不同状态，支持 UI 验收。
3. 尽量贴近当前实现，而不是另起一套完全脱离固件逻辑的展示页。
4. 预览资产保留在仓库内，后续继续复用，不做一次性临时脚本。
5. 第一轮交付后，能够基于该工作台继续做信息层级、布局、文案、状态展示优化。

## 3. 非目标

第一轮明确不做以下内容：

1. 不引入 React/Vue/Vite 等完整前端工程。
2. 不把固件 UI 逻辑抽象成真正的跨端共享渲染层。
3. 不为预览而重写固件显示模块或 WiFi 服务模块。
4. 不实现地图选点、OTA、轨迹回放等超出当前验收目标的新功能。
5. 不追求像素级复制真机效果，尤其设备屏幕侧仍为浏览器模拟。

## 4. 方案概述

采用一个 **统一预览工作台（UI Preview Workbench）**：

- 一个本地静态页面入口
- 同时展示：
  - `Web Console Preview`
  - `Device Screen Preview`
  - `Scenario Controls`
- 用一套共享的场景状态驱动两套 UI

该方案介于“纯样机”和“完全复用真实实现”之间：

- **Web 控制台侧**：尽量贴近现有 `wifi_server.cpp` 输出的页面结构与接口字段
- **设备屏幕侧**：在浏览器中按 `display.cpp` 的页面与状态规则进行模拟

这样可以兼顾：

- 立即可验收
- 后续可优化
- 不需要大规模重构当前项目结构

## 5. 仓库落地方式

建议新增一个轻量静态目录：

```text
tools/ui_preview/
├── index.html
├── styles.css
├── app.js
├── scenarios.js
├── web_console.js
├── device_screen.js
└── README.md
```

设计原则：

- 只使用原生 HTML/CSS/JS
- 无构建步骤
- 可通过简单静态服务器运行，例如：
  - `python3 -m http.server`
- 不依赖 ESP32 真机或真实接口

## 6. 页面结构

### 6.1 Workbench 主入口

主入口是一个统一的浏览器页面，包含三个区域：

1. **Web Console Preview**
   显示 Web 控制台当前页面及其交互
2. **Device Screen Preview**
   显示设备屏幕在不同状态下的浏览器模拟画面
3. **Scenario Controls**
   用于切换状态，不混入真实 UI 本体

布局原则：

- 桌面端优先双栏或主次分区布局
- 移动端允许上下堆叠
- 控制区与预览区视觉分离，避免“测试控件污染产品 UI”

### 6.2 Web Console Preview

第一轮覆盖当前固件网页中的现有结构：

1. `Status`
2. `Sessions`
3. `Tracks`
4. `Settings`

交互层面至少支持：

- 开始/停止录制按钮状态切换
- Session 列表展示
- Track 列表、选择当前赛道、删除确认
- Add Track 表单展示与提交演示
- Settings 加载与保存演示

该区域不需要真的请求 `/api/*`，但数据结构应尽量对齐当前接口字段，方便后续映射真实实现。

### 6.3 Device Screen Preview

设备屏幕预览需要覆盖：

#### 开机阶段画面

1. Splash
2. GPS Searching
3. Track Found
4. Session Recovery
5. Ready

#### 运行阶段主屏

1. Driving
2. Status
3. Lap List

浏览器模拟中需要体现：

- 320x240 比例
- 驾驶页的顶部栏、Delta 主区、底部栏
- Status 页的信息列表
- Lap List 页的列表与翻页状态
- 不同背景颜色与文本状态

### 6.4 Scenario Controls

控制区用于一键切换共享场景。第一轮至少提供以下场景：

1. `Cold Boot`
2. `GPS Searching`
3. `Ready To Drive`
4. `Recording`
5. `Best Lap Improved`
6. `Off Track`
7. `Session Review`
8. `Heavy Track Library`

控制区不模拟“真实产品 UI”，而是作为验收工具存在。

## 7. 共享数据模型

需要一份共享场景对象，同时驱动 Web 控制台和设备屏幕。

建议按以下思路组织：

```js
{
  id: "recording",
  label: "Recording",
  status: {
    gps_fix: true,
    satellites: 10,
    recording: true,
    current_lap: 4,
    best_lap_ms: 52380,
    track: "Ningbo Kart Center"
  },
  sessions: [...],
  tracks: [...],
  settings: {
    wifi_ssid: "GPS-LapTimer",
    wifi_pass: "12345678",
    brightness: 200
  },
  device: {
    screen: "driving",
    delta_ms: -180,
    delta_valid: true,
    off_track: false,
    lap_count: 3,
    best_lap_number: 2,
    current_lap_time_ms: 41230,
    lap_list_scroll: 0
  }
}
```

约束：

1. Web 预览尽量直接消费接近 `/api/status`、`/api/sessions`、`/api/tracks`、`/api/settings` 的字段。
2. Device 预览从同一场景对象中映射显示状态。
3. 允许少量仅用于预览的辅助字段，但必须保持命名清晰，避免与固件真实字段混淆。

## 8. 与现有实现的对齐原则

### 8.1 Web 控制台对齐原则

以 [`src/wifi_server.cpp`](../../../src/wifi_server.cpp) 为准：

- 保留当前页面的主要信息架构
- 保留现有模块分组：状态、Session、Track、Settings
- 保留当前 API 语义与字段结构
- 可以在预览中做更适合桌面验收的轻量包装，但不改变功能边界

### 8.2 设备屏幕对齐原则

以 [`src/display.cpp`](../../../src/display.cpp) 为准：

- 保留屏幕枚举与页面覆盖范围
- 保留 Driving / Status / Lap List 的信息结构
- 保留关键状态文案，如 `NO GPS`、`OFF TRACK`、`READY`
- 保留基于速度的锁屏逻辑、Lap List 翻页语义等关键规则

说明：

浏览器模拟不需要精确复制 `TFT_eSPI` 的每一次刷新细节，但必须准确表达最终用户看到的页面状态和切换规则。

## 9. 第一轮优化范围

在工作台建好、现状映射完成后，再开始第一轮 UI 优化。

优化优先级：

1. 信息层级
2. 间距与版式
3. 按钮可操作性
4. 列表可读性
5. 文案一致性
6. 状态可辨识度

第一轮暂不优先做：

1. 大量新增功能
2. 与固件逻辑强耦合的流程重构
3. 复杂动画
4. 真机级硬件渲染还原

## 10. 错误处理与降级策略

预览工作台需要在以下情况下保持可用：

1. **场景数据缺字段**
   - Web 预览和 Device 预览都要显示安全兜底值，例如 `--`、`No track`、`No sessions`
   - 不因为单个字段缺失导致整页报错
2. **未知场景**
   - 默认回退到 `Ready To Drive` 或一个最小安全场景
3. **长列表**
   - Session / Track / Lap List 需要验证在数量较多时仍可读，不出现明显布局崩坏
4. **设备屏幕特殊状态**
   - `NO GPS`
   - `OFF TRACK`
   - `delta 不可用`
   - `无圈速历史`

目标不是构建一个强健的产品级状态管理系统，而是保证验收工作台在典型错误和边界输入下依然可用于看 UI。

## 11. 验证与测试方式

第一轮至少需要具备以下验证方式：

1. **静态运行验证**
   - 通过简单本地 HTTP 服务打开页面
   - 页面可正常加载，无明显脚本错误
2. **场景切换验证**
   - 每个预设场景都能切换成功
   - 两套预览会同步响应场景变化
3. **结构对齐验证**
   - Web 预览的主要模块与 `wifi_server.cpp` 当前结构一致
   - Device 预览覆盖 `display.cpp` 当前所有主要页面
4. **视口验证**
   - 桌面端工作台布局可用
   - 窄屏下不发生完全不可读的重叠或溢出
5. **后续回归入口**
   - 新增或修改 UI 时，可以补一个场景后重新打开页面复看

## 12. 验收标准

满足以下条件时，认为第一轮目标达成：

1. 可以在电脑上通过一个本地 URL 打开 `UI Preview Workbench`。
2. 可以直接查看 Web 控制台的主要页面结构。
3. 可以直接查看设备屏幕的开机画面和 3 个主屏。
4. 可以通过场景切换快速查看至少 8 个典型状态。
5. Web 与设备屏幕预览都由共享场景驱动，而不是各自独立硬编码。
6. 预览不依赖 ESP32 真机即可运行。
7. 目录结构足够清晰，后续可继续扩展 UI 优化与状态用例。

## 13. 风险与处理

### 风险 1：预览和真实实现逐渐偏离

处理方式：

- 预览字段命名尽量贴近固件接口
- 设备屏幕状态与页面枚举以 `display.cpp` 为准
- 后续 UI 修改优先同时更新预览场景

### 风险 2：设备屏幕浏览器模拟失真

处理方式：

- 明确目标是“验收页面状态与信息架构”，不是模拟底层驱动
- 保持 320x240 视口与关键颜色、文案、布局关系

### 风险 3：为了预览引入过重工程

处理方式：

- 固定采用静态文件方案
- 不引入完整前端框架
- 不增加构建链

## 14. 未来扩展（不阻塞第一轮）

后续可以在同一工作台中逐步追加：

1. 设备外观与屏幕联动展示
2. 更多轨迹/圈速场景
3. UI 回归用例页面
4. 与真实 JSON 样本的对接
5. 预览页面中挂接现有 [`device_3d_view.html`](../../../device_3d_view.html) 作为硬件展示入口

## 15. 建议的后续实现顺序

实现计划建议按以下顺序展开：

1. 搭建 `tools/ui_preview/` 静态预览骨架
2. 建立共享场景模型
3. 实现 Web Console Preview
4. 实现 Device Screen Preview
5. 补齐典型场景
6. 在此基础上开始第一轮 UI 优化

---

## 决策摘要

本设计选择：

- **方案**：统一预览工作台
- **技术路线**：原生静态 HTML/CSS/JS
- **数据方式**：共享假数据场景驱动
- **对齐策略**：
  - Web 侧尽量贴近 `wifi_server.cpp`
  - 设备侧尽量贴近 `display.cpp`
- **第一轮目标**：先解决“能在电脑上完整验收 UI”，再在同一入口上继续做优化
