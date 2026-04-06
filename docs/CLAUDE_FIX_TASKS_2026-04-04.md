# Claude Review 与修复总表

日期：2026-04-04
项目：`/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS`
来源：

- 代码 review findings
- 需求完成度 / 产品验收 review

用途：作为给 Claude 的统一入口文档，包含：

- 代码 review 的修复任务拆分
- 需求与产品验收视角的缺口清单

---

## 使用说明

这份文档不是让 Claude 盲改，而是按下面顺序执行：

1. 先验证对应 finding 在目标分支是否仍然成立
2. 如果仍然成立，再做最小修复
3. 每修一条都要给出验证结果
4. 如果当前分支已经修过，不要重复改，直接说明证据并关闭该 finding

这份文档已经合并了原先分散的几份说明文件，后续以它为唯一入口即可。

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

---

## 需求与产品验收 Review

说明：

- 这一部分不是实现细节 code review，而是按 `docs/PRD.md` 对照当前工作区代码做的产品验收视角审查。
- 由于代码仍在变化，下面结论是**当前快照观察**；对每一项都应先复核当前分支是否仍然成立，再决定是否修复。
- findings 按严重级别排序，优先关注会导致主流程和 PRD 不一致的项。

### PR1

- 级别：`P0`
- 标题：`主流程仍依赖手动开始/停止录制，不符合 PRD 的自动起跑闭环`

位置：

- `src/session.cpp:59-62`
- `src/session.cpp:76-98`
- `src/session.cpp:250-264`
- `src/wifi_server.cpp:458-480`
- `docs/PRD.md:443-448`

结论：

PRD 写的是 `Ready -> 首次穿越起终线 -> 开始计时 + 开始写 VBO 临时文件`，但当前实现里，lap event 在 `SESSION_RECORDING` 之外会被直接忽略，而 recording 仍然依赖：

- 按键切换
- 或网页 `/api/recording` 手动 start

这意味着用户如果按 PRD 的理解“开机后直接下场跑”，当前固件可能根本没有开始记录 session，也不会消费圈事件。这已经是主流程和产品定义不一致，不是 UI 细节。

### PR2

- 级别：`P1`
- 标题：`赛道自动识别流程还没有达到 PRD 验收要求`

位置：

- `src/main.cpp:125-129`
- `src/main.cpp:154-157`
- `src/lap_timer.cpp:607-623`
- `src/track.cpp:147-160`
- `docs/PRD.md:215-218`
- `docs/PRD.md:437-441`
- `docs/PRD.md:581`

结论：

当前启动流程会先加载 SD 上的第一条赛道，并在 boot screen 上展示该赛道名。真正的自动识别则是在第一次 3D Fix 后，只做一次“5km 内最近赛道”匹配。

这和 PRD 的验收口径还有差距：

- 匹配多条时，PRD 要求显示列表并允许选择
- 匹配 0 条时，PRD 要求明确提示“用手机配置”
- 验收测试还要求 10 秒内正确识别目标赛道

目前没有“多候选选择”闭环，也没有“0 条匹配”的明确用户态反馈；同时 boot 阶段展示的还是预加载的第一条赛道，容易把“默认赛道”误当成“已识别赛道”。

### PR3

- 级别：`P1`
- 标题：`手机端新建赛道仍不支持扇区输入，v1.0 赛道创建需求未完成`

位置：

- `src/wifi_server.cpp:356-384`
- `src/wifi_server.cpp:805-816`
- `docs/PRD.md:221-229`

结论：

PRD 明确写的是 v1.0 手机 Web 界面要能手动输入“起终线和扇区坐标”。但当前网页表单只收：

- `track name`
- `start/finish` 两个点
- `start/finish heading`

后端保存时还把 `sector_count` 固定成 `1`，也就是“只有起终线，没有扇区”。

这意味着用户虽然能在手机上创建赛道，但创建出来的赛道无法满足 PRD 中“扇区计时（最多 4 个扇区）”的产品能力。

### PR4

- 级别：`P1`
- 标题：`录制过程中网页仍可切换当前赛道，会破坏计时和导出一致性`

位置：

- `src/wifi_server.cpp:394-426`
- `src/lap_timer.cpp:576-586`
- `src/storage.cpp:173-210`
- `src/storage.cpp:523-548`
- `docs/PRD.md:324`

结论：

PRD 对 Wi‑Fi 的约束很明确：记录模式下网页功能应尽量受限，主要用于停车/赛后操作。当前实现里，网页 `select track` 会直接调用 `lap_timer_set_track()`，而这会立刻重置 lap timer 状态。

与此同时，session 结束时 VBO `[laptiming]` 写入又依赖全局 `active_track`。这会带来产品级风险：

- 用户在录制中从手机切赛道
- 当前 session 的计时状态被重置
- 导出的 VBO 检测线坐标还可能对应“切换后的赛道”

这样会让“用户看到的当前赛道”“正在计时的赛道”“导出的检测线几何”三者失去一致性。

### PR5

- 级别：`P2`
- 标题：`PRD 承诺的 Session 元数据 JSON 仍未落地`

位置：

- `src/storage.cpp:173-219`
- `docs/PRD.md:460-464`
- `docs/PRD.md:493-495`

结论：

PRD 的正常结束流程和 SD 卡目录结构都写了：

- session 结束时除了 `.vbo`
- 还应写出对应的 session metadata `.json`

但当前 `storage_end_session()` 只完成：

- 写 `[laptiming]`
- flush/sync
- rename `.tmp -> .vbo`

没有看到 sidecar `.json` 写出逻辑。如果这项仍然是 v1.0 承诺，那当前实现还没完成交付。

### PR6

- 级别：`P2`
- 标题：`停车/状态界面仍未达到 PRD 描述的交互与信息完整度`

位置：

- `src/display.cpp:213-228`
- `src/display.cpp:332-390`
- `docs/PRD.md:374-395`
- `docs/PRD.md:398-412`

结论：

当前设备端已经有：

- 驾驶界面
- 状态界面
- 圈速列表界面

但从产品验收角度看，还有两个明显缺口：

1. 圈速列表页没有真正的“翻页/滚动”交互  
   代码里短按只会循环切屏，不会在 lap list 内翻页。

2. 状态页仍像占位版  
   PRD 里状态页至少包含：
   - Battery 状态
   - SD 剩余空间
   - Wi‑Fi 名称

   当前状态页只显示了 GPS、Track、Recording、Laps、固定 Wi‑Fi 文案、Uptime、FW。

### PR7

- 级别：`P2`
- 标题：`开机流程屏幕有框架，但仍未完全反映真实识别结果`

位置：

- `src/main.cpp:140-161`
- `src/display.cpp:563-598`
- `docs/PRD.md:414-417`

结论：

当前代码已经补了：

- Splash
- GPS searching
- Track found
- Ready

但它仍然没有完全满足 PRD 的含义，因为 `Track found` 画面展示的是启动时预加载的 `active_track`，不一定是实际自动识别得到的结果。产品上这会造成误导：用户看到“Track found”，但这不一定代表设备已基于当前位置完成识别。

### 产品向优先级

1. 先处理 `PR1`
原因：这是整条用户主路径是否成立的问题。

2. 再处理 `PR2`、`PR3`、`PR4`
原因：这些决定赛道识别、手机配置和录制数据一致性是否真正闭环。

3. 最后处理 `PR5`、`PR6`、`PR7`
原因：这些更偏交付完整度和文档一致性，但不会先于主流程阻断 MVP。

### 给 Claude 的额外要求

处理 `PR1`-`PR7` 时，建议固定输出：

1. `该问题在当前分支是否仍然成立`
2. `这是 v1.0 必做项、文档超前，还是正在进行中的改动`
3. `如果要修，最小闭环修复是什么`
4. `如果不修，PRD 需要如何回收或改写`
