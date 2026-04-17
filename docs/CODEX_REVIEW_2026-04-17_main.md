# Codex Review Memo: `main` (2026-04-17)

本次 review 面向当前 `main` 分支的整体验收状态，而不是单一 diff。

这份文档是合并后的最终版，整合并复核了：

- 首轮整仓审查结论
- `docs/CODE_REVIEW_2026-04-17_main.md` 中可被当前代码证实的剩余问题

审查范围：

- 当前工作区 `main...origin/main`
- 固件主入口、GPS / lap timer / session / storage / Wi-Fi API / Web UI 关键链路
- 现有 host tests、生产构建、walking-test 构建

本次 review 没有修改源码，属于 review-only 交付。

## 验证记录

实际执行：

- `bash tools/run_host_tests.sh`
- `~/.platformio/penv/bin/pio run`
- `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1-walking-test`

结果：

- host tests：`11 passed, 0 failed`
- production 固件：`SUCCESS`
- walking-test 固件：`SUCCESS`

结论：当前 `main` 是可编译、可通过现有 host tests 的，但仍存在若干功能完整性和数据一致性问题。

## Findings

### 1. [P1] Catmull-Rom 过线时间插值取错了 4 点窗口，当前会把最新点重复当成 `p2/p3`

定位：

- `src/lap_timer/lap_timer_crossing.cpp:82-90`
- `src/lap_timer.cpp:43-58`
- `src/lap_timer/lap_timer_internal.h:40`

问题细节：

- `SPLINE_HISTORY` 明确是 `4`。
- `history_push()` 在缓冲区满时会把 4 个历史点保持为“最旧 -> 最新”的顺序。
- 但 `compute_crossing_time()` 在样条路径里取点时用了：
  - `p0 = history_get(1)`
  - `p1 = history_get(2)`
  - `p2 = history_get(3)`
  - `p3 = history_get(3)`

这不是一个合法的 4 点 Catmull-Rom 窗口，等价于：

- 丢掉了最旧控制点
- 把最新控制点重复用了两次

影响：

- 过线插值会退化成错误的曲线形态，`crossing_us` 不再对应真实轨迹穿线时刻。
- 这个时间戳会继续流入 `LapEvent` 和 `session.cpp` 的圈速计算，所以它不是局部显示误差，而是直接污染核心计时结果。
- 该问题在历史点缓冲满了以后就会持续存在，不是只影响首圈。

建议优先级：

- 这里应该改回标准 4 点窗口：`history_get(0), history_get(1), history_get(2), history_get(3)`。
- 这条建议优先于 UI 或导出层修复，因为它影响的是“圈速算得对不对”。

### 2. [P1] VBO 导出里的 `[laptiming]` 链路实际上没有实现完成圈写入，且文件结构与文档承诺不一致

定位：

- `src/storage.h:35-37`
- `src/session.cpp:273-275`
- `src/storage/storage_session.cpp:58-65`
- `src/storage/storage_session.cpp:87-89`
- `src/storage/storage_vbo.cpp:51-63`
- `src/storage/storage_vbo.cpp:117-141`
- `docs/PRD.md:323-333`
- `docs/ARCHITECTURE.md:447-449`

问题细节：

- `session.cpp` 在每圈完成后明确调用 `storage_write_lap_timing(lap)`。
- `storage.h` 注释也声明这里应该“Append a lap timing line”。
- 但 `storage_write_lap_timing()` 目前是空函数。
- 同时，`write_vbo_header()` 在文件头里直接写到 `[column names]` / `[data]`，并没有创建文档里描述的 `[laptiming]` 段。
- 关 session 时只是在文件尾部追加 `"[laptiming]"` 和起终线 / split 几何信息，并没有把每圈圈速或分段成绩落盘。

影响：

- 当前导出的 VBO 不满足 `PRD` / `ARCHITECTURE` 里定义的结构。
- “每完成一圈立即落盘圈速”的数据完整性承诺没有兑现。
- 如果后续工具或 Circuit Tools 工作流依赖该段落中的圈速数据，这里会直接缺失。

建议优先级：

- 先统一目标格式：是要在文件头保留 `[laptiming]` 区段并逐圈追加，还是改文档承认只写几何。
- 在此之前，不建议再把“已完成圈速已写入 VBO”当成已交付能力。

### 3. [P1] 停止录制时会先关闭 session 标志，再处理队列，导致尾部 GPS 点可能被静默丢弃

定位：

- `src/storage/storage_session.cpp:52-56`
- `src/storage/storage_task.cpp:17-22`
- `src/lap_timer/lap_timer_task.cpp:117`

问题细节：

- `lap_timer_task()` 对每个 GPS fix 都会 `forward_vbo_entry()` 入队。
- `storage_task()` 只有在 `s_session_active` 为真时才会消费并写入；否则直接 `continue` 丢弃。
- `storage_end_session()` 一进入就先把 `s_session_active = false`，然后才执行尾部写入、flush、sync、close、rename。

这意味着：

- stop 请求到来前已经排进 `vbo_write_queue`、但还没被 storage task 写到 SD 的数据，会在 stop 之后被直接跳过。
- 这是一个真实的异步关闭竞态，而不是理论边角情况。只要 stop 时队列里还有 backlog，就会截断 session 尾部。

影响：

- 会丢掉 session 最后一段轨迹点。
- 当 SD 忙、Web 下载正在占用 SPI、或者 storage task 暂时落后于 GPS 采样时，这个问题更容易出现。

建议优先级：

- stop 路径应该先停止新数据入队，再 drain 队列，最后 close/rename。
- 至少要保证“stop 之前已入队的数据必须全部落盘”。

### 4. [P2] `active_track` 在 Wi-Fi / lap-timer 路径之间无同步共享，手动切换或删除时存在撕裂读取风险

定位：

- `src/lap_timer.cpp:179-187`
- `src/wifi/api_tracks.cpp:260-267`
- `src/wifi/api_tracks.cpp:322-327`
- `src/lap_timer/lap_timer_task.cpp:102-107`

问题细节：

- `lap_timer_task()` 每个 GPS fix 都会直接读取 `s_track->start_finish` 和 `s_track->sectors[i]` 来做过线判断。
- 这份 `s_track` 实际上指向全局 `active_track`。
- 但 Web API 的手动切换和删除路径会在没有任何专用互斥保护的情况下直接：
  - `active_track = *track`
  - `memset(&active_track, 0, sizeof(TrackDefinition))`

影响：

- `TrackDefinition` 不是原子对象，跨核读写时可能被 lap timer 看到一份“写到一半”的赛道几何。
- 由于 lap timer 在未录制时也会继续跑 `process_line()`，这个风险不只是“UI 状态不一致”，还可能在手动切换/删除赛道的瞬间制造错误的检测线几何。
- 更糟的是，未录制状态下 lap timer 仍保留自动开 session 的能力，所以理论上存在被撕裂赛道数据诱发错误过线判定的可能。

建议优先级：

- 为 `active_track` 加一把专用 mutex，或改成“写 shadow copy + 原子切换指针”的模式。
- 这条优先级低于核心计时和导出完整性，但高于一般代码整洁问题。

### 5. [P2] 赛道创建 API 对起终线参数的服务端校验不完整，坏请求可以写入永久损坏的赛道

定位：

- `src/wifi/api_tracks.cpp:114-129`
- `src/wifi/api_tracks.cpp:132-173`
- `src/track/track_store.cpp:124-133`

问题细节：

- 服务端目前只检查：
  - `name` 存在且 ASCII-safe
  - `sf_lat1/sf_lon1` 不是同时为 `0`
  - 起终线两点距离至少 `1m`
- 但没有要求 `sf_lat2/sf_lon2/sf_heading` 必填，也没有做经纬度范围校验。

因此一个损坏或手工构造的请求只要提供：

- 一个非零的 `sf_lat1/sf_lon1`
- 缺失的 `sf_lat2/sf_lon2` 默认落成 `0.0`

仍然可能因为“和 `(0,0)` 的距离大于 1m”而通过校验，并被永久保存为新赛道。

影响：

- 一旦前端 bug、弱网重试或手工调用 API 发送了半成品 JSON，设备会把无效检测线写入 `tracks/`。
- 后续自动识别、手动选择、计时和导出都会建立在这个坏赛道上。

建议优先级：

- 服务端必须把完整几何校验做成硬约束，不能只信前端。
- 至少补上：第二端点必填、heading 必填、lat/lon 范围合法、每条 split 同样校验。

### 6. [P2] 设置保存链路没有对字符串做 JSON escaping 或禁止字符校验，特殊字符会破坏 `settings.json`

定位：

- `src/wifi/api_settings.cpp:66-83`
- `src/wifi/api_settings.cpp:88-105`
- `src/config.cpp:55-80`
- `src/config.cpp:188-196`

问题细节：

- `/api/settings` 直接把用户传入的 `wifi_ssid` / `wifi_pass` 写进 `app_config`。
- `config_save()` 再把这两个字段直接拼进 JSON：
  - 没有 escaping
  - 也没有像 track name 那样做禁止字符校验
- 读取时又依赖“找到下一个 `\"` 就结束”的手写 parser。

所以只要用户输入里包含：

- `"` 或 `\`

就会出现以下任一问题：

- `settings.json` 变成非法 JSON
- 下次 `config_load()` 截断或回退默认值
- `/api/settings` 返回的 JSON 被破坏

影响：

- 这是持久化配置链路的真实坏输入缺口。
- 一旦写坏，设备重启后的 Wi-Fi 名称/密码和 UI 回显都可能不可信。

建议优先级：

- 两条路选一条即可：
  - 服务端强校验并拒绝会破坏 JSON 的字符
  - 或统一引入可靠的 JSON escaping / parsing

### 7. [P2] 历史 session 元数据链路只写不读，Dashboard 的 “Best Lap” 永远拿不到真实值

定位：

- `src/storage/storage_vbo.cpp:143-215`
- `src/wifi/api_sessions.cpp:117-123`
- `src/wifi/web_ui_script_dashboard.cpp:324-333`

问题细节：

- 结束 session 时，固件会写出同名 `.json` 元数据，其中包含 `best_lap_ms`。
- 但 `/api/sessions` 完全没有读取这个 `.json`，而是直接把每个 session 的 `best_lap_ms` 硬编码成 `-1`。
- Dashboard 的 session 卡片又明确依赖这个字段来渲染 “Best Lap”。

影响：

- Web UI 里的历史 session 最佳圈目前会长期显示 `--`，即便元数据文件已经写出来了。
- 这一段元数据链路对用户可见功能来说是半断开的。

额外问题：

- 同一份元数据里的 `total_time_s` 当前来自 `millis()/1000`（`src/storage/storage_vbo.cpp:186-193`），表示的是“开机到现在”的秒数，不是“本次 session 持续时间”。如果以后开始消费这个字段，结果也会是错的。

建议优先级：

- 要么让 `/api/sessions` 正式读取 metadata JSON。
- 要么删掉当前未接通、且部分字段定义错误的 metadata 承诺，避免系统里同时存在两套不一致的数据源。

### 8. [P3] `handle_finish_crossing()` 直接读取 `session_state` 控制标志，和既有加锁约定不一致

定位：

- `src/lap_timer/lap_timer_events.cpp:35-75`
- `src/session.cpp:128-141`

问题细节：

- `session_stop_recording()` 在 `session_mutex` 保护下写 `session_state.is_recording` 和 `session_state.session_stopped`。
- 但 `handle_finish_crossing()` 在 Core 0 上做“忽略 crossing / 自动开 session”的关键分支时，直接无锁读取这两个字段。
- 这和项目其它地方对 `session_state` 的使用方式不一致，也让这段代码对跨核可见性和时序细节产生了隐含依赖。

影响：

- 我不把它定性为已经坐实的必现 bug，因为在 ESP32-S3 这类平台上 1-byte flag 的读写通常不会炸出明显损坏。
- 但它确实让“stop 之后一定不会重新自动开始”这个行为保证，建立在未显式同步的前提上。
- 这条风险比前几条更偏并发稳健性，而不是当前必现的数据错误，所以降一档到 `P3`。

建议优先级：

- 最简单的收口方式是在 `handle_finish_crossing()` 里短超时拿一下 `s_session_mutex`，只读出这两个 flag 再做决策。

## Non-Blocking Notes

- `walking-test` 构建虽然成功，但当前会对 `src/wifi/wifi_internal.h:26-28` 的 `inline constexpr` 发出 “only available with -std=c++17” 警告。当前工具链把它当扩展接受了，不阻塞交付，但这说明项目对 C++ 标准版本的假设还不够明确。
- 现有 host tests 主要覆盖了纯逻辑模块。`storage_session.cpp`、`storage_task.cpp`、`api_tracks.cpp`、`api_settings.cpp` 这些本次真正暴露出问题的路径，没有对应的 host 级回归测试。
- 如果继续补测试，最值得优先加的是：
  - crossing-after-stop
  - stop-during-out-lap
  - auto-start 遇到 `storage_start_session()` 失败

## Positive Observations

- 启动链路、boot status / boot presenter / boot sequence 这组模块拆分得比较清晰，而且有成体系的 host tests 支撑。
- `serial_console` 的路径约束做得相对克制，至少避免了直接的目录穿越。
- `main` 当前两套 firmware profile 都能完整构建，说明工程并没有处于“review 只能停留在静态阅读”的状态。

## 建议处理顺序

1. 先修导出完整性问题：
   - `[laptiming]` 真正实现
   - stop 时 drain 队列，保证尾部样本不丢
2. 再补 API 防线：
   - track create 参数完整校验
   - settings JSON 的 escaping / 校验
3. 最后收口 Web/metadata：
   - `/api/sessions` 接 metadata
   - 修正 `total_time_s`
   - 为上述路径补 host tests 或最小集成测试

## Prior Draft Items Reviewed But Not Carried Forward

以下内容来自 `docs/CODE_REVIEW_2026-04-17_main.md`，我这次复核后没有继续保留为当前 findings：

- `storage_write_lap_timing(lap)` 的“dangling pointer”问题：
  - 这在长期设计上不够稳妥，但以当前代码看，callee 仍是 noop，而且已完成圈的 slot 不会再被别的路径回写，所以我没有把它当成当前主问题。
- `lon_amin` 的经度符号疑似写反：
  - 我没有采纳，因为 `docs/PRD.md:300-303` 明确规定 VBO 经度列要用 `十进制度 × -60`，东经为负，现实现与文档一致。
- `gps_fix.cpp` “queue-full 会丢最新点”：
  - 我没有采纳，因为 `src/gps/gps_fix.cpp:54-58` 的实际行为是先弹出一个旧点，再写入新点，丢的是最旧点。
- `compute_crossing_time()` fallback 可能对 `history_get(-1)` 下标下溢：
  - 这在当前调用图里不会发生；`process_line()` 进入时已经至少有 `prev/curr` 两个 fix，所以我没有把它列成当前 bug。
- `classify_lap()` 依赖调用者已持锁、`portMAX_DELAY` 的 SPI 使用、`volatile s_track_source` 这几条：
  - 更偏代码卫生或未来演进风险，暂未提升为当前主要 findings。

## Bottom Line

`main` 当前“能编、能过现有单测”，但距离“导出链路和 Web 管理链路可完全信任”还有明显差距。

最需要优先处理的不是样式问题，而是这几条真正会影响系统正确性的主问题：

- 过线样条插值当前取错控制点，核心计时结果本身就可能偏
- VBO 的 `[laptiming]` 承诺没有真正落地
- stop 路径会丢 session 尾部已入队的 GPS 数据
- `active_track` 的跨任务无锁共享仍然留下了竞态窗口

如果先把这些问题补齐，再修服务端输入校验，整个 `main` 的可靠性会明显上一个台阶。
