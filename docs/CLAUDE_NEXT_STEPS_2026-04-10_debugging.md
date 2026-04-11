# Claude Next Steps (2026-04-10)

这份文档补充两件事：

1. 当前这轮崩溃排查，建议优先往哪里继续收敛
2. 以后写 review / debug 文档时，除了 findings，还要把 Codex 的行动建议一起写进去

---

## 1. 当前建议的下一步

### A. 把 breadcrumb 改成“每核各一份”

当前 Core 0 和 Core 1 共用同一个 `RTC_NOINIT_ATTR crash_breadcrumb`，最后一次写入会覆盖前面的状态。

建议：

1. 改成 `core0_breadcrumb`
2. 改成 `core1_breadcrumb`
3. 启动时一起写入 `crash_log.txt`

这样下次看到：

- Core 0 停在什么阶段
- Core 1 停在什么阶段

就不用再靠“最后写入者是谁”来猜。

### B. 继续保留 `precompute_reference()` 的削峰思路

这个方向是合理的：

- `delta_set_reference()`
- `precompute_reference()`

都属于过圈后可能出现的重计算路径。

保留周期性 `taskYIELD()` 是可以的，但请把它视为：

- 削峰缓解

而不是：

- 已经坐实根因并完全修复

### C. 如果继续查 display，要把 breadcrumb 细化到 content draw

`breadcrumb=61` 目前只能说明：

- display 任务已经走到 `render_frame()` 前后

但现在的 breadcrumb 还不够细，不足以区分：

1. driving screen 内容绘制
2. status screen 内容绘制
3. lap list 内容绘制
4. `draw_driving_delta()` / `pushSprite()` 这种更重的 SPI 路径

建议下一步把 display 的标记细分到：

1. `render_frame()` 进入
2. `draw_driving_screen()` 前后
3. `draw_driving_delta()` 前后
4. `pushSprite()` 前后

### D. 加 task stack watermark，不要只靠 watchdog 猜

建议观测这些任务的 `uxTaskGetStackHighWaterMark()`：

1. `lap_timer`
2. `session`
3. `display`
4. `wifi`

这样能快速判断：

- 是不是某个任务本身离栈底太近
- 还是主要是跨核重负载/调度问题

### E. 长期方案：把 reference 重建从过圈热路径里拆出去

如果后续证据继续指向：

- “刚过圈时 Core 0 + Core 1 同时重负载”

那更稳的长期方案是：

1. 过圈时只记录“需要更新 reference”
2. 交给单独 worker task 异步做 `delta_set_reference()`

这通常比在热路径里继续堆 `yield()` 更稳。

---

## 2. 以后 review / debug 文档的写法要求

从这一轮开始，后续写给用户的 review 或 debug 文档，不要只写 findings。

每份文档至少要包含下面三部分：

### A. Current Findings

写当前明确成立的问题，标清：

1. 哪些是当前工作区现状
2. 哪些是旧 finding，已经过期

### B. Codex Suggestions

必须单独加一个小节，写 Codex 给出的建议动作。

要求：

1. 区分“低风险缓解”和“高置信根因”
2. 区分“立即建议”和“长期建议”
3. 不要只给结论，要给下一步动作

### C. Recommended Next Step

最后必须收束成：

1. 现在最值得先做的一步
2. 为什么先做它
3. 做完后看什么证据判断下一步

---

## 3. 推荐模板

以后可以按这个最小模板写：

### Current Findings

- 事实 1
- 事实 2
- 已过期 finding

### Codex Suggestions

- 建议 1：低风险缓解
- 建议 2：进一步诊断
- 建议 3：长期重构方向

### Recommended Next Step

- 先做什么
- 预期会拿到什么新证据

---

## 4. 一句话总结

当前这轮最值得继续推进的是：

- 每核独立 breadcrumb
- display content draw 更细粒度 breadcrumb
- task stack watermark

以后所有 review/debug 文档都要把：

- findings
- Codex suggestions
- next step

一起写进去，不要再只留 findings。
