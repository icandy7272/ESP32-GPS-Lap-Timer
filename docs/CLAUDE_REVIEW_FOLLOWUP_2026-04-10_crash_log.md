# Claude Review Follow-up (2026-04-10, crash_log)

这份说明是给 Claude 的后续修改指引，基于当前 `crash log / breadcrumb / watchdog` 相关 review finding 整理。

## 结论

这 3 条 finding 我都认同，建议继续修改，不需要回退。

补充一点判断：

- `~/.platformio/penv/bin/pio run` 目前通过了
- 所以这轮问题主要是行为正确性和诊断证据保真，不是编译层面的 breakage

建议优先级：

1. `src/main.cpp` 的 crash 证据保留问题
2. `src/main.cpp` 的 RTC watermark 有效性问题
3. `src/display/display.cpp` 的 full redraw 恢复问题

## 1. 关于 full redraw finding

对应位置：

- `src/display/display.cpp`
- 主要在 `render_frame()` 的两阶段重绘逻辑

### 我认同的点

这条是个真实回归，不只是理论上的。

当前流程是：

1. `df.full_redraw` 时先 `fillScreen(TFT_BLACK)`
2. 释放 `spi_mutex`
3. `taskYIELD()`
4. 再次尝试拿 `spi_mutex` 做 phase 2 内容绘制
5. 如果第二次超时，直接 `return`

这样的问题是：

- 屏幕已经被整屏清黑了
- 但 “还欠一次 full redraw” 这个意图没有被保留下来
- 下一帧如果 `screen_changed == false`，`compute_dirty()` 就会退回普通 dirty render

这对 `SCREEN_DRIVING` 尤其危险，因为 driving 页本来就是按局部区域补画的，下一帧不一定会把整屏重新补齐。

### 建议你修的时候把表述再说严谨一点

如果你要给 reviewer 或后续文档回复，建议不要泛泛说“可能 blank”。

更准确的说法是：

- “在 full redraw 的 clear pass 完成后，如果 content pass 因第二次 SPI lock 超时而跳过，driving screen 可能长期残留黑块，因为下一帧不会自动继续 full redraw。”

### 建议修改方向

推荐最小修法：

1. 增加一个 sticky 的 “pending full redraw” 状态
2. 只要 clear pass 做了、但 content pass 没做完，就把这个状态保留到下一帧
3. 下一帧即使 `screen_changed == false`，也仍然强制走 full redraw

可接受的等价修法：

1. 只有在能保证 content draw 也会执行时，才做 `fillScreen`
2. 或者把 clear/content 两阶段之间的失败恢复逻辑补齐

不建议只是“把第二次 timeout 调大一点”，因为那只是降低概率，不是修语义。

## 2. 关于 breadcrumb 提前清零 finding

对应位置：

- `src/main.cpp`
- `crash_bc_core0 / crash_bc_core1` 的读取与清零顺序

### 我认同的点

这条我完全认同，而且这是 crash logger 自己最该避免的问题。

现在的顺序是：

1. 从 RTC 读出 breadcrumb
2. 立刻把 RTC 里的 breadcrumb 清零
3. 再尝试打开并写入 `crash_log.txt`

问题在于，只要这次启动时 SD 写失败：

- 当前 boot 没把 crash 证据落盘
- 下一次 boot 也拿不到原始 breadcrumb

这会直接削弱这个功能存在的意义。

### 建议修改方向

推荐最小修法：

1. 启动时先把 `crash_bc_core0/core1` 读到局部变量
2. 尝试 `open -> write -> sync -> close`
3. 只有在确认 crash log 已成功持久化后，再清 RTC 里的 breadcrumb

建议再补一个布尔值，例如：

- `bool crash_log_persisted = false;`

只有它为真时才清零。

### 建议补的细节

如果写失败，建议至少保留一条串口告警，明确区分：

- “检测到上次 crash”
- “但这次没能把 crash log 写进 SD”

这样现场排查时不会误以为证据已经持久化了。

## 3. 关于 RTC watermark 可能跨 boot 污染 finding

对应位置：

- `src/main.cpp`
- `RTC_NOINIT_ATTR uint16_t wm_*`
- `loop()` 里每 5 秒的 snapshot

### 我认同的点

这条也成立。

当前问题不在于 `RTC_NOINIT_ATTR` 本身，而在于：

- 这些值没有明确的 per-boot 有效性边界
- 新一轮启动时没有先清成“未采样”
- 第一次刷新要等到 `loop()` 的下一次 5 秒周期

所以如果设备：

1. 这次 boot 读完旧 crash 信息后没有及时重置 `wm_*`
2. 又在第一次新采样之前再次 crash

那下一次 crash log 就可能带出更早一轮 boot 的 watermark。

### 建议你修的时候把表述说得更准确

建议不要写成“watermark 一定是错的”。

更严谨的表述是：

- “当前实现没有把 `wm_*` 与本次 boot 绑定；如果在首次刷新前再次崩溃，crash log 可能继承上一轮 boot 的旧值。”

### 建议修改方向

推荐最小修法：

1. 在消费完上一轮 crash 信息后，立刻把所有 `wm_*` 清成 0 或某个明确 sentinel
2. 在相关任务都创建完成后，主动做一次 watermark snapshot
3. 后续再保留现在每 5 秒一次的周期刷新

这样至少能保证：

- 新 boot 开始后不会继续带着上一轮的旧值运行
- 如果还没采到新值，log 里看到的是 “0/未采样”，而不是伪造的历史值

如果你想做得更稳，可以加：

- 一个 RTC 的 `wm_boot_marker`
- 或一个 `wm_valid` 标记

但对这轮修复来说，未必一定要上这个复杂度。

## 推荐修法

建议只收这 3 条，不要把修复扩散到别的模块。

推荐改动目标：

1. `src/main.cpp`
   - 调整 breadcrumb 清零时机
   - 给 `wm_*` 建立清晰的 boot 边界
   - 在任务启动完成后尽快做一次首轮 snapshot
2. `src/display/display.cpp`
   - 给 full redraw 增加失败后的恢复语义

## 验收标准

### 本地验证

1. `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`
2. `bash tools/run_host_tests.sh`

### 设备侧验证

1. 人为制造 screen change + SPI contention，确认即使第二次锁失败，后续帧也能恢复完整画面
2. 人为制造一次 crash 后，在首次重启时让 SD 写失败，确认 breadcrumb 不会因为这次失败被永久清掉
3. 在新的 boot 里、第一次 5 秒周期 snapshot 之前再次 reset，确认 crash log 不会把更早一轮 boot 的 watermark 冒充为本轮数据

## 说明

这份 follow-up 不是在否定这轮 crash diagnostics 的方向。

相反，我的判断是：

- “启动时落 crash 线索到 SD”
- “用 RTC 保存 breadcrumb / watermark”
- “把 display / wifi / storage 的 watchdog 证据链补齐”

这些方向都对。

当前需要补的，是证据保留语义和失败恢复语义，让这套诊断链在“出问题的时候”也尽量不丢最关键的信息。
