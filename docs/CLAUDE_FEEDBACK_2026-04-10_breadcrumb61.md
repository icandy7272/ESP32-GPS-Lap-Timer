# Claude Feedback (2026-04-10, breadcrumb=61)

这份说明是基于最新一次单条 crash log：

- `heap=310916 psram=8373647 reason=INT_WDT (interrupt watchdog) breadcrumb=61`

以及当前工作区代码状态整理的。

## 先说结论

你的大方向里，有一半我认同：

1. “刚跑完一圈时，系统进入 Core 0 + Core 1 的并发重负载窗口”
2. `precompute_reference()` 是值得削峰/让步的嫌疑点

但另一半结论我觉得还说得太满：

> `breadcrumb=61` => Core 1 `render_frame()` 持 `spi_mutex` 太久，就是当前 INT_WDT 的根因

这件事，**现有证据还不够直接支持**。

---

## 我认同的部分

### 1. `breadcrumb=61` 确实把注意力推到了 display 任务

当前 display breadcrumb 在：

- `60` frame start
- `61` about to render
- `62` frame done

代码位置：

- [display.cpp](/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/src/display/display.cpp#L290)
- [display.cpp](/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/src/display/display.cpp#L322)
- [display.cpp](/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/src/display/display.cpp#L324)

所以最新一次 crash 的最后写点落在 `61`，这至少说明：

- 最近这次崩溃前，display 任务已经走到 `render_frame(screen_changed)` 之前

### 2. `precompute_reference()` 确实是重计算路径

代码在：

- [delta.cpp](/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/src/delta.cpp#L64)

它会对整圈点集做：

- `haversine`
- cumulative distance
- heading 预计算

而这条路径又是在“新 best lap”时，从：

- [lap_timer_events.cpp](/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/src/lap_timer/lap_timer_events.cpp#L75)

触发的。

因此，你给 `precompute_reference()` 加周期性 `taskYIELD()`，我认为是合理的缓解措施。

---

## 我不完全认同的部分

## 1. `breadcrumb=61` 不等于“已确认死在 render_frame 持锁过久”

`61` 的语义只是：

- 最后一个成功写入 breadcrumb 的位置在 display 任务的 “about to render”

它并不直接等于：

- 真正导致 `INT_WDT` 的 PC 就在 `render_frame()` 内部
- 或者一定是 `spi_mutex` 持有时间过长

它只能说明：

- display 任务是当前异常窗口里最后一个来得及更新 breadcrumb 的任务

---

## 2. 你对 `render_frame()` 的修复，和“刚跑完一圈就崩”的主路径并不完全对齐

你当前的 display 修复是：

- 在 `df.full_redraw` 时，把 clear pass 和 content draw 拆成两段
- 中间释放一次 `spi_mutex` + `taskYIELD()`

代码位置：

- [display.cpp](/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/src/display/display.cpp#L200)

但这里有个关键点：

### `df.full_redraw` 不是“跑完一圈就会发生”的条件

`df.full_redraw` 来源于：

- [display.cpp](/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/src/display/display.cpp#L76)

```cpp
d.full_redraw = screen_changed;
```

而 `screen_changed` 来自：

- [display.cpp](/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/src/display/display.cpp#L297)

```cpp
bool screen_changed = handle_button_events() || first_frame;
```

也就是说：

- 首帧会 full redraw
- 按键切屏会 full redraw
- 但“正常刚跑完一圈”本身并不会自动导致 `full_redraw`

正常一圈完成时，更可能变化的是：

- `lap_number`
- `best_time`
- `delta`
- `background`

这些 dirty flags 走的是内容绘制分支，不是你拆出来的 “clear pass” 分支。

### 这意味着什么

这意味着你现在这条修复：

- **可能是好改动**
- 但**不一定正中这次 `breadcrumb=61` 的主触发路径**

换句话说：

如果你现在说“render_frame 清屏锁太久，所以我已经修中主因了”，这个证据链还不够闭合。

---

## 3. 如果 display 真是主嫌疑，更该继续看 content draw 持锁区

当前 `render_frame()` 仍然会在内容绘制整个阶段持有 `spi_mutex`：

- [display.cpp](/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/src/display/display.cpp#L212)

然后内部会调用：

- `draw_driving_screen()`
- `draw_status_screen()`
- `draw_lap_list_screen()`

对于“刚跑完一圈”的场景，最值得继续看的是：

- [display_driving.cpp](/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/src/display/display_driving.cpp#L89)

因为 driving screen 在 lap completion 那一帧会更新：

- lap 编号
- current time
- delta sprite
- best lap
- GPS 信息

如果 display 真在这次问题里是主因，我认为：

- 持锁的 content draw 区间

比：

- 只在 `full_redraw` 时拆开的 clear pass

更值得继续怀疑。

---

## 给你的建议

### 我建议保留的思路

1. 继续保留 `precompute_reference()` 的让步/削峰思路
2. 继续保留 display breadcrumb

### 我建议你修正的结论表述

不要把当前结论写成：

> `61` 已确认是 render_frame 持锁过久导致

更准确的说法应该是：

> `61` 说明最近一次崩溃窗口已经进入 display render 前后；display 是当前第一优先排查对象之一，但尚未被直接坐实为唯一根因。

### 如果你要继续追 display

建议继续验证的不是：

- “full redraw clear pass 是否过长”

而是：

- “刚跑完一圈时，driving screen 的 content draw 持锁区是否过长”

---

## 一句话总结

你已经抓到“过圈后并发重负载窗口”这个大方向，但 `breadcrumb=61` 目前还只能把 display/render 提升为高优先级嫌疑，而**不能单独证明你这次拆分 `full_redraw` 的修复已经对准主路径**。
