# Claude Feedback (2026-04-10, change history + breadcrumb=70)

这份文档不是只看单次 crash，而是结合当前工作区的 `git` 修改记录，整理这轮排查到底改了什么、哪些结论已经比较稳、下一步最该先做什么。

---

## Current Findings

### 1. 这轮 crash 排查的关键改动基本都还没有形成独立 commit

从 `git log` 看，最近能追到这些文件的最新已提交节点主要还是：

- `313693c Refactor core modules and harden track session flows`
- 更早的 `3c060af refactor(display): split display.cpp into src/display/ by screen page`

也就是说，**这轮 watchdog / panic 排查的大部分变化还停留在工作区 diff 里**，不是一串可逐步回放的提交历史。

这点很重要，因为它意味着：

- 现在要看“修改记录”，主要应该看 `git diff`
- 不要误以为这些排查已经经过多轮 commit 级验证

---

### 2. 当前 crash 相关 diff 实际上分成了 4 波

`git diff --stat` 在 crash 相关文件上大致是：

- 9 files changed
- 138 insertions
- 31 deletions

按内容看，基本是这 4 波：

#### A. WiFi 栈压力缓解

涉及：

- `src/wifi/wifi_server.cpp`
- `src/wifi/api_status.cpp`
- `src/main.cpp`
- `src/types.h`

做了这些事：

- `wifi` 任务栈从 `8192` 提到 `12288`
- `is_throttled()` 不再拷贝整份 `SessionState`
- `api_status` 也改成只取所需字段，不再整块复制 session
- 为了 Web UI 加了 `gps_lat_deg / gps_lon_deg`

这波改动和你最早那次：

- `Stack canary watchpoint triggered (wifi)`

是直接对齐的，所以它更像是**上一阶段问题的定向缓解**。

#### B. 每核 breadcrumb / crash logger

涉及：

- `src/main.cpp`
- `src/lap_timer/lap_timer_events.cpp`
- `src/session.cpp`
- `src/display/display.cpp`
- `src/storage/storage_task.cpp`
- `src/delta.cpp`

做了这些事：

- 改成 `core0` / `core1` 两份 breadcrumb
- 启动时把 reset reason 和 breadcrumb 写到 `crash_log.txt`
- 在 lap/session/display/storage/delta 路径上加了分段标记

这波是**诊断性改动**，不是修复性改动。

#### C. Display 侧让步

涉及：

- `src/display/display.cpp`

做了这些事：

- `render_frame()` 不再一上来就整段持 SPI
- 仅在 `full_redraw` 时，把清屏和内容绘制拆成两段
- 中间 `xSemaphoreGive()` + `taskYIELD()`

这一波是基于之前 `breadcrumb=61` 的怀疑做的，目标是：

- 降低 display 长时间占住 SPI 的概率

但它**只覆盖 `full_redraw` 路径**，并不覆盖所有正常过圈后的 driving redraw。

#### D. Core 0 delta 重计算让步

涉及：

- `src/delta.cpp`

做了这些事：

- 在 `precompute_reference()` 两个循环里每 256 点 `taskYIELD()`
- 给 `memcpy` / `precompute_reference` 加了 `11 / 14` breadcrumb

这一波是基于“过圈后 Core 0 可能出现重计算尖峰”的怀疑做的，属于：

- 合理的削峰
- 但还不是已坐实的根因修复

---

### 3. 从修改历史上看，storage 目前只被“观察”，还没被真正“分解”

当前 storage 相关改动只有：

- `src/storage/storage_task.cpp`

新增：

- `70` before VBO write
- `71` after VBO write

但代码顺序是：

1. `format_vbo_line()`
2. `crash_bc_core1 = 70`
3. `xSemaphoreTake(spi_mutex, portMAX_DELAY)`
4. `s_vbo_file.write(...)`
5. `crash_bc_core1 = 71`
6. 之后可能再走 `flush_and_sync()`

这意味着 `70` 的信息量还不够。

它目前混在了一起：

- 卡在拿 `spi_mutex`
- 卡在 `s_vbo_file.write()`
- 刚写完但马上进入 `flush_and_sync()`

所以这次的修改历史告诉我们的不是：

> storage 根因已经被查清了

而是：

> storage 刚刚被提升成主嫌疑，但诊断粒度仍然太粗。

---

### 4. 从修改历史上看，display 还不能彻底排除

虽然现在最新线索是 `breadcrumb=70`，但结合当前代码状态，display 仍然不能完全洗清：

- `render_frame()` 的“拆成两段”只作用于 `df.full_redraw`
- 正常过圈并不一定触发 `full_redraw`
- driving screen 的 content draw 仍然整段持有 SPI
- `draw_driving_delta()` 里的 `pushSprite()` 仍然是重 SPI 路径

所以：

- `70` 可能是 storage 自己在写盘路径里崩
- 也可能只是 storage 先写了 `70`，然后堵在拿 SPI 锁，真正持锁太久的是 display

当前 breadcrumb 还不足以区分这两者。

---

### 5. “第二圈开始时崩” 和 `FSYNC_INTERVAL_MS = 30000` 这个组合很值得怀疑

当前：

- `src/storage/storage_internal.h` 里 `FSYNC_INTERVAL_MS = 30000`
- `src/storage/storage_session.cpp` 在 session start 时设置 `s_last_fsync_ms = millis()`
- `src/storage/storage_task.cpp` 每次写完后判断要不要 `flush_and_sync()`

所以如果第一圈时间大约接近 30 秒，那么：

- “第二圈开始时崩”

就和：

- “第一次周期性 `flush/sync` 正好触发”

这个窗口高度重合。

我不会把这件事直接写成已确认根因，但它是目前**最值得优先验证的假设之一**。

---

### 6. 当前挂载出来的 `crash_log.txt` 是空的，说明最新证据链还不完整

我本地看到：

- `/Volumes/SDCARD/crash_log.txt` 当前为 0 字节

所以现在我们手上的“`breadcrumb=70`”仍然更像是：

- 用户转述的最新结果

而不是：

- 当前 SD 卡上已经确认落盘的 `core0=... core1=...` 完整日志

这不影响判断方向，但意味着：

- 下一步诊断最好同时保留串口输出
- 不要只赌 `crash_log.txt`

---

## Codex Suggestions

### 1. 下一步不要继续大改 display，也不要直接重构 storage

从修改历史看，这一轮已经连续做了几次“基于新 breadcrumb 的结构性调整”：

- 先怀疑 wifi，做了栈和 session 读取收缩
- 再怀疑 display，做了 `full_redraw` 拆锁
- 同时给 delta 加了 `taskYIELD()`

而现在新线索已经转向 `70`。

这时候更合适的动作不是继续猜一个新主因然后改结构，而是：

- **先把 storage 这条路径打细**

---

### 2. 建议把 storage breadcrumb 至少细化成 6 段

建议在 storage 这里拆成：

1. `before take spi_mutex`
2. `after take spi_mutex`
3. `before s_vbo_file.write`
4. `after s_vbo_file.write`
5. `before flush_and_sync`
6. `after flush_and_sync`

这样下一次复现后，至少能区分：

- 等锁
- 文件写入
- 同步落盘

这比继续在 display 上做新改动的信息量大得多。

---

### 3. 建议同时加 task stack watermark，而不是只盯 breadcrumb

目前已经发生过：

- `wifi` stack canary
- `INT_WDT`
- `PANIC (Guru Meditation)`

所以仅靠 breadcrumb 不够。

建议一起观测：

- `storage`
- `display`
- `session`
- `wifi`

的 `uxTaskGetStackHighWaterMark()`。

原因很简单：

- `70` 如果是写盘前后栈爆，breadcrumb 只能告诉你“到过这儿”
- watermark 才能告诉你“有没有贴近栈底”

---

### 4. 如果细化后发现问题集中在 `flush_and_sync()`，再做一个最小 A/B 诊断

如果下一轮证据变成：

- `write()` 后能走过去
- 但总在 `flush/sync` 前后崩

那我建议做一个**诊断用**最小实验，而不是长期修复：

- 暂时把周期性 fsync 关掉，或把间隔调大很多
- 只用来验证“第二圈开始崩”是否消失

这一步的目的不是直接定方案，而是回答一个更小的问题：

- 触发点到底是普通 VBO `write`
- 还是周期性 `flush/sync`

---

### 5. 如果细化后发现总是卡在 `before take` 和 `after take` 之间，优先回头看 display content draw

如果下一次结果像这样：

- 一直停在 storage 的“准备拿锁”
- 从没进入 “after take”

那说明 storage 更像是在等别人释放 SPI。

这时应当优先回头看：

- `src/display/display.cpp`
- `src/display/display_driving.cpp`

尤其是：

- 正常 driving redraw
- `pushSprite()`
- 过圈后那一帧的持锁区

而不是继续怀疑 SdFat 本身。

---

## Recommended Next Step

最值得先做的一步是：

- **把 storage 路径的 breadcrumb 从 70/71 扩成细粒度阶段标记，同时加 task stack watermark 输出。**

为什么先做这一步：

- 当前修改历史已经说明，wifi / display / delta 都各自做过一轮缓解
- 但 storage 目前还只有“粗粒度命中”，诊断精度明显落后
- 继续改结构，风险比信息增益更大

做完后最关键要看两类证据：

1. 最新 crash 到底停在 storage 的哪一个子阶段
2. `storage/display/session/wifi` 哪个任务的剩余栈最低

如果这两件事拿到手，下一步就能更有把握地判断：

- 是锁竞争
- 是 SdFat 写入
- 是 `flush/sync`
- 还是栈问题

---

## One-line Summary

结合当前 `git diff` 看，这轮修改已经把嫌疑从 `wifi` 推到了 `display`，又从 `display` 推到了 `storage`；但 storage 目前只有粗粒度打点，还远没到该继续拍脑袋改结构的时候。下一步最该做的是把 `70` 这条路径彻底拆细。
