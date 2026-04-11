# Claude Debug Notes (2026-04-10)

这份文档是给 Claude 的诊断说明，基于当前工作区代码和用户提供的串口日志整理。

目标有两个：

1. 说明当前“反复重启 / watchdog / wifi 崩溃”的最高优先级嫌疑点
2. 说明用户转述的某些旧 review finding，哪些已经不是当前工作区现状

---

## 1. 当前最高优先级问题：`wifi` 任务路径仍然有明显的栈压力风险

### 用户提供过的关键信号

用户之前提供过这类崩溃栈：

- `Stack canary watchpoint triggered (wifi)`
- 回溯落在：
  - `read_session_state()`
  - `is_throttled()`
  - `handle_api_status()`
  - `wifi_task()`

之后又补充了 crash log：

- `heap=310916 psram=8373647 reason=INT_WDT (interrupt watchdog)`

这个 `INT_WDT` 日志来自启动时的 crash logger，代码在：

- [main.cpp](/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/src/main.cpp#L127)

它表示“上一次启动是以 interrupt watchdog 的形式结束”，但**不等于根因已经从原来的 `wifi` 栈/内存问题切换成了全新的中断层问题**。

### 当前代码状态

现在的代码已经做了两件缓解：

1. `is_throttled()` 不再复制整个 `SessionState`，只读 `is_recording`
   - [wifi_server.cpp](/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/src/wifi/wifi_server.cpp#L70)
2. `wifi` 任务栈已经提升到 `12288`
   - [main.cpp](/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/src/main.cpp#L258)

这两个改动方向都是对的。

### 但问题没有被完全拿掉

当前代码里，`wifi` 路径仍然会按值复制整个 `SessionState`：

- `handle_api_status()`
  - [api_status.cpp](/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/src/wifi/api_status.cpp#L17)
- `read_session_state()`
  - [wifi_server.cpp](/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/src/wifi/wifi_server.cpp#L93)
- `POST /api/recording`
  - [api_recording.cpp](/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/src/wifi/api_recording.cpp#L27)
  - [api_recording.cpp](/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/src/wifi/api_recording.cpp#L30)
- `POST /api/tracks/delete`
  - [api_tracks.cpp](/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/src/wifi/api_tracks.cpp#L243)

而当前 `SessionState` 很大：

- 定义在 [types.h](/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/src/types.h#L74)
- 包含 `LapRecord laps[100]`
- 还新增了 `gps_lat_deg` / `gps_lon_deg`

按当前结构估算，`SessionState` 大约是 **4128 字节**

这意味着：

- `read_session_state()` 自己先在栈上放一份
- 调用方再放一份
- 再叠加 `WebServer` 调用链自己的栈占用

即使 `wifi` 栈从 8KB 提到 12KB，这条路径依然很重，尤其是高频接口。

### 为什么 `/api/status` 是最优先嫌疑点

Web UI 会持续轮询 `/api/status`：

- [web_ui.cpp](/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/src/wifi/web_ui.cpp#L154)
- [web_ui.cpp](/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/src/wifi/web_ui.cpp#L298)

也就是说，只要浏览器连上页面，`wifi` 任务就会反复打这条调用链。

因此，当前最高置信度判断仍然是：

**`wifi` 路由里对大号 `SessionState` 的按值复制，依旧是重启问题的主嫌疑。**

`INT_WDT` 更像是这类栈/内存踩踏后的二次表现，而不是一个完全无关的新根因。

### 建议 Claude 优先验证的方向

先不要分散到太多地方，优先验证：

1. `handle_api_status()` 是否还需要拿完整 `SessionState`
2. `read_session_state()` 是否应该继续返回完整结构体副本
3. `wifi` 任务的 stack high-water mark 在连上 Web UI 后还剩多少
4. `api_recording` / `api_tracks(delete)` 这些次级路由是否也应避免整结构复制

如果需要排优先级，建议先盯：

1. `/api/status`
2. `/api/recording`
3. `/api/tracks/delete`

### 一个不要误判的点

用户日志里那条：

- `addApbChangeCallback(): duplicate ...`

值得后续继续查，但**它不像当前反复重启的直接主因**。

当前更需要优先看的，还是 `wifi` 任务的栈/结构体复制问题。

---

## 2. 关于用户转述的 review finding：`MAX_SESSIONS = 50`

用户转述了一条旧 finding：

> Session listing now hides an arbitrary subset after 50 files

这个 finding 指向的是旧版 `api_sessions.cpp` 里：

- 固定 `MAX_SESSIONS = 50`
- 通过 `storage_list_sessions(names, MAX_SESSIONS)` 拿前 50 条

### 但这不是当前工作区现状

当前 `api_sessions.cpp` 已经不是那个实现了。

现在的代码是：

- 直接遍历 `sessions/` 目录
- 没有 `MAX_SESSIONS = 50`
- 没有固定数量上限

见：

- [api_sessions.cpp](/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/src/wifi/api_sessions.cpp#L20)

所以：

- 如果 Claude 看到的是“当前工作区”，这条 review finding **已经过期**
- 除非 Claude 在看的是更早的 diff / 更早的提交，否则不应再围绕这条 finding 继续改当前 `api_sessions.cpp`

---

## 3. 给 Claude 的简短结论

如果只用一句话概括：

**当前最值得优先追的不是 `api_sessions`，而是 `wifi` 路由里依旧存在的 `SessionState` 大结构按值复制；尤其是 `/api/status` 这条高频轮询路径。**

再补一句工作区状态说明：

**用户转述的 `MAX_SESSIONS = 50` review finding 对当前工作区已经不成立，属于旧问题，不应覆盖当前对重启问题的排查优先级。**
