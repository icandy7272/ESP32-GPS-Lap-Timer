# Claude 修复任务总表

日期：2026-04-04
项目：`/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS`
来源：`docs/CODE_REVIEW_FINDINGS_2026-04-04.md`
用途：把 review findings 转成 Claude 可直接执行的修复任务拆分表

---

## 使用说明

这份文档不是让 Claude 盲改，而是按下面顺序执行：

1. 先验证对应 finding 在目标分支是否仍然成立
2. 如果仍然成立，再做最小修复
3. 每修一条都要给出验证结果
4. 如果当前分支已经修过，不要重复改，直接说明证据并关闭该 finding

---

## 总体优先级

### P0

- F2 `Full app modules never start`

### P1

- F3 `GPS status is never propagated to shared state`
- F4 `Debounce path confirms every tentative crossing`
- F5 `Recording enters active state even if SD session creation fails`
- F6 `Session save can collide and still report success`
- F7 `Starting a new session does not reset lap-timer state`

### P2

- F1 `'No reference lap yet' is rendered as OFF TRACK`

---

## F1

### 原始 finding

- 位置：`src/lap_timer.cpp:473-483`
- 级别：`P2`
- 标题：`'No reference lap yet' is rendered as OFF TRACK`

### Claude 任务

- 检查 `delta_valid` 和 `off_track` 是否被混用
- 确保“无参考圈”和“真的 off-track”是两个独立状态
- 确保 UI 只在真实偏离赛道时显示 `OFF TRACK`
- 确保无参考圈时显示 `---`

### 重点文件

- `src/lap_timer.cpp`
- `src/delta.cpp`
- `src/display.cpp`

### 完成标准

- `delta_valid = false && off_track = false` 时显示 `---`
- `delta_valid = false && off_track = true` 时显示 `OFF TRACK`
- 不允许再用 `!delta_valid` 推导 `off_track`

### 备注

我当前工作区核到的代码看起来这条已经大概率修过了，所以 Claude 先验证，不要直接改。

---

## F2

### 原始 finding

- 位置：`src/main.cpp:66-106`
- 级别：`P0`
- 标题：`Full app modules never start`

### Claude 任务

- 检查 `setup()` 是否真的只初始化了串口/LED/启动页
- 如果属实，把所有核心模块接起来：
  - 队列
  - mutex
  - `storage_init()`
  - `config_load()`
  - `delta_init()`
  - `session_init()`
  - `track_init()`
  - `lap_timer_init()`
  - `gps_init()`
  - `display_init()`
  - `button_init()`
  - `wifi_init()`
  - 各 FreeRTOS task 创建

### 重点文件

- `src/main.cpp`
- `src/session.cpp`
- `src/display.cpp`
- `src/gps.cpp`
- `src/wifi_server.cpp`
- `src/storage.cpp`

### 完成标准

- 固件启动后，所有模块都进入运行态
- 串口日志能看到完整启动路径
- 设备能实际工作，不再是“只亮屏但核心功能没启动”

### 验证建议

- 编译通过
- 上电日志能看到各子系统初始化
- Wi-Fi 页面、GPS、显示、存储至少都能进入基本工作状态

---

## F3

### 原始 finding

- 位置：`src/display.cpp:264-307`
- 级别：`P1`
- 标题：`GPS status is never propagated to shared state`

### Claude 任务

- 查找 `SessionState.gps_fix_ok` 和 `SessionState.gps_satellites` 的写入路径
- 如果没有从 `GpsPoint` 同步到 `SessionState`，就补上
- 同步点应当放在每次 GPS/LapTimer 更新共享状态的地方，而不是只在初始化时赋值

### 重点文件

- `src/lap_timer.cpp`
- `src/session.cpp`
- `src/types.h`
- `src/display.cpp`
- `src/wifi_server.cpp`

### 完成标准

- 屏幕 GPS 状态不再长期停在 `NO GPS`
- `/api/status` 能随实时 fix 和卫星数变化
- `reset_session_state()` 后，后续 GPS 更新能够重新填充这些字段

---

## F4

### 原始 finding

- 位置：`src/lap_timer.cpp:303-323`
- 级别：`P1`
- 标题：`Debounce path confirms every tentative crossing`

### Claude 任务

- 检查 `debounce_feed()` 是否只是简单倒计时确认
- 如果是，就让 debounce 真正依赖新样本和穿越几何关系
- 防止“刚开始 tentative crossing，哪怕下一两个点已经反弹回去，仍然会被确认”

### 重点文件

- `src/lap_timer.cpp`

### 完成标准

- tentative crossing 不会因为纯计数器归零就自动确认
- 反向抖动或线边 bounce-back 不会错误触发 lap/sector event
- 去抖路径仍然不应引入额外计时误差

### 验证建议

- 构造一组抖动点：跨线后立刻回弹
- 预期：不能发出 finish/sector event

---

## F5

### 原始 finding

- 位置：`src/session.cpp:80-88`
- 级别：`P1`
- 标题：`Recording enters active state even if SD session creation fails`

### Claude 任务

- 检查 `session_start_recording()` 里 `is_recording` 的赋值时机
- 如果 `storage_start_session()` 失败，不能仍然把 session 标记成 recording
- 明确失败时的策略：
  - 不进入 recording
  - 或者进入“无存储降级模式”，但 UI/API 必须说实话

### 重点文件

- `src/session.cpp`
- `src/storage.cpp`
- `src/wifi_server.cpp`
- `src/types.h`

### 完成标准

- SD 启动失败时，UI 不会假装正在录制
- 队列写入和状态展示一致
- 错误路径有明确日志

---

## F6

### 原始 finding

- 位置：`src/storage.cpp:541-559`
- 级别：`P1`
- 标题：`Session save can collide and still report success`

### Claude 任务

- 核对 collision probe 使用的文件名格式是否与最终 rename 目标完全一致
- 检查无 RTC/无有效时间戳时的 fallback 命名是否容易冲突
- 检查 `sd.rename()` 或等价重命名调用的返回值是否被校验
- 如果 rename 失败，不能继续打印/返回“session saved”

### 重点文件

- `src/storage.cpp`

### 完成标准

- 文件名碰撞能被正确检测
- rename 失败会明确报错
- 不会出现 temp file 没移动成功但日志仍写成功

### 验证建议

- 模拟重复文件名
- 模拟无 RTC 时间戳 fallback
- 确认失败时最终状态和日志一致

---

## F7

### 原始 finding

- 位置：`src/session.cpp:72-90`
- 级别：`P1`
- 标题：`Starting a new session does not reset lap-timer state`

### Claude 任务

- 检查开始新 session 时，除了 `SessionState`，是否也重置了：
  - lap timer 内部静态状态
  - arming 状态
  - 历史轨迹缓冲
  - 当前圈点缓存
  - delta elapsed 状态
  - 参考圈/最佳圈基线的保留策略

### 重点文件

- `src/session.cpp`
- `src/lap_timer.cpp`
- `src/lap_timer.h`
- `src/delta.cpp`

### 完成标准

- 同一次开机内，停止录制再重新开始，不会继承上一个 session 的内部状态
- 需要保留的状态和需要清空的状态有明确边界

### 建议实现方向

- 提供明确的 `lap_timer_reset()` 或等价接口
- 必要时补一个 `delta_reset_reference()` / `delta_free_reference()` 策略说明
- 避免把“新 session 应清空”和“跨 session 应保留”混在隐式逻辑里

---

## Claude 建议执行顺序

1. 先做 F2  
原因：如果主模块根本没启动，后面很多问题无法真实验证。

2. 再做 F3、F7  
原因：共享状态和 session 边界是很多显示/行为问题的基础。

3. 再做 F4、F5、F6  
原因：这些是运行时正确性和数据可靠性问题。

4. 最后做 F1  
原因：这是显示语义问题，严重程度最低，而且当前分支可能已经修过。

---

## Claude 每条任务都要输出的内容

每修一条，建议 Claude 固定输出这 4 项：

1. `是否确认 finding 仍然成立`
2. `实际修改了哪些文件`
3. `如何验证`
4. `还有没有残余风险`

---

## 最短转述版

可以直接把这句话发给 Claude：

请按 `docs/CLAUDE_FIX_TASKS_2026-04-04.md` 执行，不要直接相信旧 review，要先逐条验证 finding 在当前分支是否仍然成立；若成立则做最小修复并给出验证结果，若已修复则直接说明证据并关闭该项。
