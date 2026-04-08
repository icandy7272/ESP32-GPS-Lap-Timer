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

---

## 2026-04-06 当前 ship readiness 复审

这一节是基于当前本地仓库状态做的再次 review，目标不是重复历史 finding，而是告诉 Claude:

- 哪些问题已经修过，不要重复改
- 哪些问题现在仍然阻塞发版
- 哪些属于产品验收缺口
- 当前到底能不能进入 `ship`

### 本次复审结论

- 当前分支：`main`
- 本地构建：`~/.platformio/penv/bin/pio run` 已通过
- 但当前还不能认为“可以 ship”

原因分三类：

1. 还有真实的发版阻塞项
2. 还有 PRD 的关键闭环没有完成
3. 即使代码状态满足，按 `$ship` 规则也不能从 `main` 直接发

### 已确认不要重复修的项

#### C1

- 结论：`Driving screen still shows a synthetic lap time` 这条在当前分支看起来已经修掉了
- 证据：`src/display.cpp:277-288`

当前实现已经不是 `best_lap_time_ms + delta_ms` 的伪圈时，而是：

- 用 `current_lap_start_us`
- 用 `esp_timer_get_time() - current_lap_start_us`
- 实时显示当前圈已用时

Claude 处理方式：

- 先把这条标记为“已验证关闭”
- 除非当前工作分支又改回去了，否则不要重复动这个点

---

## 当前仍阻塞 ship 的问题

### SR1

- 级别：`P0`
- 标题：`录制在 SD 建 session 失败时仍会对外表现为成功`

位置：

- `src/session.cpp:81-102`
- `src/lap_timer.cpp:438-449`
- `src/wifi_server.cpp:517-525`
- `src/storage.cpp:131-174`

问题说明：

当前有三条“开始录制”路径：

- 按键/HTTP 调 `session_start_recording()`
- 首次过线自动开始录制
- 存储模块真正创建 `_recording.vbo.tmp`

但现在状态并不一致：

- `session_start_recording()` 先把 `session_state.is_recording = true`
- 然后才去调 `storage_start_session()`
- `storage_start_session()` 失败时，只打日志，不回滚 UI 状态
- `/api/recording` 也无条件返回 `{"ok":true,"recording":true}`
- 自动开始录制那条路径也直接把 `is_recording` 置真，然后忽略存储失败

结果是：

- 屏幕显示在录制
- Web API 也说在录制
- 但 `storage_task()` 因为 `session_active == false` 会直接丢弃后续 VBO 样本

Claude 任务：

1. 统一“录制成功”的真相来源
2. 明确 `storage_start_session()` 失败时的系统行为
3. 让按键、HTTP、自动开始三条路径使用同一套状态切换逻辑
4. 保证 UI、API、存储实际状态一致

推荐修法：

- 最简单的是：只有 `storage_start_session()` 成功后，才把 session 切到 recording
- 如果想保留“无存储降级模式”，那就必须新增显式状态位，不能继续复用 `is_recording`

完成标准：

- 模拟 `storage_start_session()` 返回 `false`
- 屏幕/API 不能再显示 `recording = true`
- 不能再出现“看起来在录，实际上没文件”的静默失败

### SR2

- 级别：`P1`
- 标题：`录制中 WiFi 下载会长时间占住 SPI，可能干扰实时写卡`

位置：

- `src/wifi_server.cpp:594-657`
- `src/storage.cpp:97-116`
- `src/lap_timer.cpp:370-379`
- `docs/PRD.md:324-348`

问题说明：

当前下载 VBO 文件时：

- `stream_file_from_sd()` 会先拿 `spi_mutex`
- 然后整个文件流式发送期间一直不释放
- 录制时写 VBO 也要抢同一把 SPI 锁

这和 PRD 里“记录模式下 WiFi 必须让位给 GPS/SD 实时链路”的要求不一致。

更糟的是：

- `lap_timer.cpp` 往 `vbo_write_queue` 入队时没有检查失败
- 一旦后端写入被拖慢，队列溢出时样本可能悄悄丢失

Claude 任务：

1. 决定录制中是否允许下载历史 VBO
2. 如果允许，必须把 SPI 占用改成更短粒度
3. 给 VBO 入队失败增加可见日志或计数

推荐修法：

- v1.0 最小闭环：录制中直接禁止 `/files/*` 下载
- 或者每个 chunk 单独加锁/解锁，但这条更复杂

完成标准：

- 录制中发起下载，不会长时间独占 SPI
- 不会再出现“WiFi 下载影响 SD 实时写卡”的路径
- 队列满时不能静默丢数据

### SR3

- 级别：`P1`
- 标题：`赛道名未做 ASCII / JSON / 文件名规范化，可能同时破坏 JSON、API 和 VBO 文件命名`

位置：

- `src/wifi_server.cpp:342-379`
- `src/track.cpp:556-618`
- `src/wifi_server.cpp:179-192`
- `src/storage.cpp:151-152`
- `src/storage.cpp:685-686`
- `docs/PRD.md:225`
- `docs/PRD.md:238-239`

问题说明：

当前 Web 端新增赛道时，`name` 基本是原样透传的：

- 写 track JSON 时直接拼进字符串
- `/api/status` 也直接塞进 JSON 响应
- 最终 session 文件名也直接使用 track name

但 PRD 对 v1.0 的要求其实很明确：

- 赛道名和文件名按 ASCII only 处理

所以现在只要名字里带这些内容，就可能出问题：

- 引号
- 反斜杠
- 斜杠
- 中文或其他非 ASCII
- 文件系统不安全字符

Claude 任务：

1. 定义 v1.0 赛道名输入约束
2. 给新增赛道接口加校验
3. 给 JSON 输出加转义
4. 给 session 文件名做安全规范化

推荐修法：

- `display_name` 可以保留用户输入的可显示版本
- 但 v1.0 更简单的是直接限制为 ASCII-safe name
- 至少要保证它既能安全写 JSON，也能安全当文件名

完成标准：

- 非法赛道名会被拒绝或规范化
- track JSON 不会因为名字字符而损坏
- `/api/status` 输出不会因为名字而变成非法 JSON
- VBO 文件名始终满足 `YYYYMMDD_TrackName_HHMMSS_NNN.vbo`

### SR4

- 级别：`P1`
- 标题：`PlatformIO 仍解析成无 PSRAM 目标，和当前实现依赖不一致`

位置：

- `platformio.ini:5-33`
- 本次本地构建输出：`ESP32-S3-DevKitC-1-N8 (8 MB QD, No PSRAM)`

问题说明：

虽然当前仓库能编译通过，但构建目标仍然被 PlatformIO 解析成：

- `No PSRAM`

而当前代码又依赖：

- `ps_malloc()`
- lap/reference buffer 存 PSRAM

这类问题的危险在于：

- 编译通过
- 运行时才 silently degrade

Claude 任务：

1. 确认目标板卡配置到底该怎么写
2. 让 PlatformIO 解析结果与真实硬件一致
3. 给 PSRAM 关键路径增加启动日志或失败降级提示

完成标准：

- 构建目标不再显示 `No PSRAM`
- 或者如果必须继续用这个 board，就要证明运行时 PSRAM 确实可用
- 与 `ps_malloc()` 相关的核心功能不能再处于“可能默默失效”的状态

---

## 当前产品 / 验收缺口

### PR8

- 级别：`P1`
- 标题：`多布局赛道自动识别仍未实现“候选列表 + 按键选择”`

位置：

- `src/track.cpp:147-160`
- `src/main.cpp:159-170`
- `src/lap_timer.cpp:615-636`
- `docs/PRD.md:214-219`

问题说明：

PRD 的 v1.0 要求是：

1. 5km 内找赛道
2. 如果只有 1 条，自动选中
3. 如果匹配到多条，要在屏幕上列出来让用户选

但当前实现是：

- 只返回最近的一条
- 即使代码已经统计出 `candidates > 1`
- 最终仍然直接自动选中

这会在同场馆多布局时选错赛道。

Claude 任务：

1. 先判断这条是否要保留在 v1.0
2. 如果保留，就补出最小候选选择流
3. 如果不保留，就回收 PRD 表述，不要让文档继续宣称已支持

最小闭环修法：

- 开机识别到多条候选时，显示列表
- 用现有按键做上下切换 / 确认
- 超时后可以保留默认项，但不能无提示静默选中

### PR9

- 级别：`P2`
- 标题：`v1.0 仍暴露 GPS 采样率设置，和 PRD 的固定 25Hz 相冲突`

位置：

- `docs/PRD.md:233-236`
- `src/wifi_server.cpp:874-883`
- `src/wifi_server.cpp:720-727`
- `src/gps.cpp:567-573`

问题说明：

PRD 写的是：

- v1.0 固定 25Hz
- 不提供可选项

但当前 Web 设置页仍允许改 `GPS Rate (Hz)`，而且这个设置真的会进入运行时配置。

这不是“展示了个无效按钮”，而是会直接把产品承诺改掉。

Claude 任务：

1. 判断 v1.0 需求到底是固定 25Hz，还是文档需要改
2. 如果继续按 PRD，移除前端和后端这项设置
3. 如果要保留，就必须同步修改 PRD 和验收标准

### PR10

- 级别：`P2`
- 标题：`停车/状态/恢复提示还没有达到 PRD 所写的用户体验`

位置：

- `src/display.cpp:45-47`
- `src/display.cpp:195-215`
- `src/display.cpp:369-438`
- `src/display.cpp:649-688`
- `src/storage.cpp:327-384`
- `docs/PRD.md:376-412`
- `docs/PRD.md:250-253`

问题说明：

当前代码有这些差距：

- 只是“低速 3 秒后解锁”，不是 PRD 写的“停车 10 秒后自动切到赛后/状态页”
- 设备状态页没有电池状态
- 断电恢复后没有屏幕提示“已恢复上次未完成的 Session”

这几条单看都不是最底层技术阻塞，但合在一起说明：

- 当前 UI 还没完全达到 PRD 的交付口径

Claude 任务：

1. 逐条判断哪些是 v1.0 必做项
2. 能补的补最小闭环
3. 需要回收的需求就明确回收，不要继续挂在 PRD 里

### PR11

- 级别：`P2`
- 标题：`当前仓库还不具备完整 ship 资产：版本、changelog、tests、golden fixtures 都缺`

位置：

- 仓库根目录当前没有：`VERSION`
- 仓库根目录当前没有：`CHANGELOG.md`
- 仓库根目录当前没有：`TODOS.md`
- 仓库当前没有自动化测试目录
- 仓库当前没有 golden VBO fixtures
- `docs/PRD.md:312`
- `docs/PRD.md:572-580`

问题说明：

从“代码能编译”到“真的可 ship”，中间还差一层交付证明：

- PRD 明确要求 Circuit Tools 导入验证
- 要求 MyLaps 误差验证
- 要求断电恢复验证
- 还要求 golden test files 做回归

但当前仓库里还没有能承接这些验收项的测试资产和发版资产。

Claude 任务：

1. 不要把“能编译”误判成“可以 ship”
2. 至少补一套最小交付资产方案
3. 明确哪些验证要靠硬件实测，哪些可以先做离线回放/fixture

建议最小闭环：

- 加一个最小 `VERSION`
- 加 `CHANGELOG.md`
- 加一份 `TODOS.md` 记录剩余硬件验收项
- 补最少量的 golden/fixture 说明或样例数据

---

## 当前 ship 判断

### 结论

当前不建议进入正式 ship。

### 原因

1. `SR1` 是发版阻塞
2. `SR2`、`SR3`、`SR4` 都是高风险线上问题
3. `PR8`-`PR11` 说明产品验收和交付证据还没闭环
4. 当前分支就是 `main`，按 `$ship` 规则本身也会在 Step 1 直接中止

### 对 Claude 的执行顺序建议

1. 先验证并处理 `SR1`
2. 再处理 `SR2`、`SR3`、`SR4`
3. 然后确认 `PR8`、`PR9`、`PR10` 哪些要补、哪些要回收 PRD
4. 最后再补 `PR11` 这类 ship 资产

### Claude 每处理一条时固定输出

建议 Claude 对 `SR1`-`SR4`、`PR8`-`PR11` 都固定输出四件事：

1. `该问题在当前分支是否仍然成立`
2. `最小闭环修法是什么`
3. `我实际做了什么`
4. `我怎么验证它已经成立或关闭`

---

## 2026-04-07 最新 GitHub 全面复审补充

### 复审基线

- 本轮按当前工作区 `origin/main` 作为“最新 GitHub”代码复审
- 本地核对结果：`HEAD == origin/main == 9d4483886be2e4e9c8f73cce98629fc788bfa6b4`
- 已重新执行一次 `pio run`
- 当前工作区能编译通过，但仍不建议判定为可 ship

### LG1

- 级别：`P0`
- 标题：`延迟拿到 GPS fix 时，当前赛道会分裂成两套来源，后续手动切赛道和导出可能错位`

位置：

- `src/lap_timer.cpp:587-597`
- `src/lap_timer.cpp:631-649`
- `src/storage.cpp:167-168`

问题说明：

当前代码默认假设 `s_track` 始终指向 `active_track`。

但如果设备开机后 30 秒内没有拿到 fix，后面在 `lap_timer_task()` 里自动识别赛道时，会把 `s_track` 直接改成 `track` 模块内部数组里的对象，而不是 `active_track`。

这样一来：

- 之后 Web 手动切赛道时，`lap_timer_set_track()` 只会改 `active_track`
- 计时线程却可能还在继续用旧的 `s_track`
- `storage_start_session()` 又会从 `active_track` 快照检测线到导出文件

最终可能出现：

- 屏幕显示是 A 赛道
- 实际圈速检测按 B 赛道线跑
- 导出的 `[laptiming]` 又写成 C

这是当前最危险的一条，因为它会直接破坏“赛道选择 -> 计时 -> 导出”的一致性。

Claude 任务：

1. 收敛赛道单一真相源，只保留一个活动赛道对象
2. 自动识别和手动切换都必须走同一条更新路径
3. 导出快照必须和计时线程当前使用的赛道完全一致
4. 修完后补日志，明确打印“最终生效赛道”

验收标准：

- 开机 30 秒内无 fix，30 秒后才拿到 fix 时，自动识别仍能正确切赛道
- 之后手机再手动切换一次，计时、屏幕、导出三者保持一致
- 导出 VBO 的 `[laptiming]` 线坐标和当前活动赛道一致

### LG2

- 级别：`P1`
- 标题：`PlatformIO 目标板仍被解析为无 PSRAM，和代码实现假设冲突`

位置：

- `platformio.ini:5-33`
- `src/lap_timer.cpp:547-551`
- `src/delta.cpp:276-280`

问题说明：

我重新执行 `pio run` 后，PlatformIO 仍把当前环境解析成：

- `Espressif ESP32-S3-DevKitC-1-N8 (8 MB QD, No PSRAM)`

但当前项目实现明确依赖 `ps_malloc()`：

- `lap_timer.cpp` 用它存单圈轨迹
- `delta.cpp` 用它存参考圈、累计距离、航向和时间戳

也就是说：

- 现在“能编译”不等于“按目标硬件可稳定运行”
- 如果板卡定义不对，运行时很可能直接退化成没有参考圈 / 没有 delta

Claude 任务：

1. 把 PlatformIO 板卡配置修到和真实硬件一致
2. 不要只靠 `-DBOARD_HAS_PSRAM=1` 自欺欺人
3. 启动时打印并校验 PSRAM 是否真的可用
4. 如果仍要兼容无 PSRAM 目标，就明确提供降级行为和 UI 提示

验收标准：

- `pio run` 输出不再是 `No PSRAM`
- 启动日志明确显示可用 PSRAM 容量
- 参考圈和 delta 缓冲分配成功，不再只靠 warning 判断

### LG3

- 级别：`P1`
- 标题：`Session 文件重名探测仍然用错文件名格式，重复录制时仍可能保存失败`

位置：

- `src/storage.cpp:141-152`
- `src/storage.cpp:203-220`
- `src/storage.cpp:668-691`

问题说明：

当前重名探测和最终实际文件名不是同一个格式。

探测时查的是：

- `sessions/<session_start_ts>_<track>_<seq>.vbo`

最终生成的却是：

- `sessions/<date>_<track>_<time>_<seq>.vbo`

所以前面的存在性检查并不能真正防住最终 rename 撞名。

再叠加无 RTC 时会退回：

- `00000000_000000`

多次短时间录制或恢复场景下，仍可能出现：

- rename 失败
- `.tmp` 残留
- 用户以为“录完了”，但真实文件名并没有正确落盘

Claude 任务：

1. 重构文件命名逻辑，探测路径和最终路径必须共用同一函数
2. 无 RTC fallback 也要保证唯一性
3. `rename` 失败时不能只打日志，必须给出可恢复策略
4. 最好补一个最小离线验证，覆盖同秒重复 session

验收标准：

- 同一秒连续开始/停止多个 session，不会发生覆盖或假成功
- 无 RTC 时间时也能稳定生成唯一文件名
- rename 失败时用户态和日志都能明确感知，不会默默伪成功

### LG4

- 级别：`P1`
- 标题：`录制中仍允许删除赛道，会把当前计时上下文直接打断`

位置：

- `src/wifi_server.cpp:449-488`
- `src/wifi_server.cpp:495-523`

问题说明：

当前代码已经禁止“录制中切换赛道”，这是对的。

但删除赛道接口没有同等级保护。只要用户在录制中删掉当前活动赛道，代码就会：

- 清空 `active_track`
- `lap_timer_reset()`
- 把 `track_name` 改成 `No Track`

结果是：

- 录制状态还在
- VBO 还在继续写
- 但当前圈计时语义被中途重置

这会把一个 session 切成“前半段有赛道，后半段无赛道”的异常状态。

Claude 任务：

1. 删除赛道接口和切赛道接口统一风控
2. 录制中至少禁止删除当前活动赛道
3. 更保守一点的话，录制中所有赛道增删改都直接锁死
4. 前端提示语也要同步补上

验收标准：

- 录制中删除当前赛道会被明确拒绝
- 不会出现 recording=true 但 lap timer 已 reset 的半断裂状态

### LG5

- 级别：`P1`
- 标题：`赛道删除在 SD 删除失败时仍然返回成功，会导致重启后“幽灵赛道”回来`

位置：

- `src/track.cpp:224-261`

问题说明：

当前 `track_delete()` 在 `sd.remove()` 失败后，只打印日志，然后仍然：

- 从内存数组里删掉赛道
- 返回 `true`

这会导致：

- Web UI 看到删除成功
- 当前运行时列表里也看不到了
- 但 SD 上文件其实还在
- 下一次重启，赛道又会重新被加载出来

这类问题很容易把用户搞糊涂，因为它看起来像“设备记不住删除操作”。

Claude 任务：

1. 把删除操作改成真正的原子语义
2. SD 删除失败时，不要改内存状态，也不要返回成功
3. 前端错误提示要明确告诉用户是“文件删除失败”，不是“track not found”

验收标准：

- SD 删除失败时，API 返回失败
- 内存和磁盘状态保持一致
- 重启后不会出现“刚删掉又回来了”的错觉

### LG6

- 级别：`P2`
- 标题：`PRD 里的多候选赛道选择仍未交付，当前实现只是自动选最近的一条`

位置：

- `docs/PRD.md:214-219`
- `src/lap_timer.cpp:620-650`
- `TODOS.md:16`

问题说明：

PRD 明确写的是：

- 5km 内若匹配多条赛道
- 需要在屏幕上列出候选
- 用按键选择

但当前实现只是：

- 统计候选数量
- 打一条日志
- 仍然直接选最近的那条

并且仓库自己的 `TODOS.md` 也把这项标成了：

- `Deferred to v1.1`

所以这里不能再说“v1.0 已完成”，而应该在两个方向里选一个：

1. 把功能真正做完
2. 或者正式从 v1.0 PRD 回收

Claude 任务：

1. 不要再让代码、PRD、TODO 三份口径继续冲突
2. 如果坚持 v1.0，就补最小可用候选选择闭环
3. 如果决定延期，就同步修改 PRD / TODO / 验收标准

验收标准：

- 文档和代码口径一致
- 不再存在“PRD 说有、代码没有、TODO 说以后再做”的三方分裂

### 本轮 ship 判断

结论：

- 当前依然不建议 ship

这轮新增/确认的主要阻塞是：

1. `LG1` 是真实的链路一致性问题，属于发版阻塞
2. `LG2` 说明当前“板卡配置正确”这件事还没有站住
3. `LG3`、`LG4`、`LG5` 都会影响用户数据或用户感知
4. `LG6` 说明产品验收口径仍未彻底收敛

### 对 Claude 的建议处理顺序

1. 先处理 `LG1`
2. 再处理 `LG2`
3. 然后处理 `LG3`、`LG4`、`LG5`
4. 最后决定 `LG6` 是补功能还是回收 PRD

### Claude 每条修复时建议固定补三样东西

1. `修复前为什么会出错`
2. `现在单一真相源是什么`
3. `我如何验证这条已经关闭`

## 2026-04-08 Claude 最新改动复审补充

这轮是对 Claude 已提交但尚未完全收敛的本地改动做的增量 review。

本次实际检查过的重点文件：

- `platformio.ini`
- `src/User_Setup.h`
- `src/display.cpp`
- `src/gps.cpp`

本地结果补充：

- `pio run -e esp32-s3-devkitc-1` 可通过
- 之前那个 `display.cpp` 里“右上角 current lap time 仍然是 synthetic value”的问题，这一版看起来已经修掉
- 但这轮新改动里仍然有 3 个值得继续修的点

### CG1

- 级别：`P1`
- 标题：`gps_rate_hz 运行时配置被硬编码覆盖，Web / settings.json 设了也不会生效`

位置：

- `src/gps.cpp:564-639`

问题说明：

当前 `uart_init()` 直接把：

- `GPS_FIX_RATE_HZ = 25`
- `FIX_INTERVAL_US = 40000`

写死了，完全没有再从 `app_config.gps_rate_hz` 读取实际配置。

这会导致：

- `config/settings.json` 里的 `gps_rate_hz` 失效
- Web 设置页里保存的 GPS 频率失效
- 设备每次开机都会强制把模块重新设成 25Hz

这不只是“设置项没接线”，还会直接影响当前硬件 bring-up，因为调试阶段用户本来可能希望临时切回：

- `1Hz`
- `5Hz`
- `10Hz`

来验证串口稳定性或搜星表现，但现在会被静默覆盖掉。

Claude 任务：

1. 恢复 `app_config.gps_rate_hz` 作为运行时单一真相源
2. 对非法值继续做兜底，但不要再无条件写死成 `25`
3. `FIX_INTERVAL_US` 必须和实际最终生效的 rate 保持一致
4. 串口日志里打印“最终采用的 GPS 频率”，便于现场确认

验收标准：

- 改 `config/settings.json` 或 Web 设置后，重启仍保持该频率
- 日志能明确看到最终 rate
- `1Hz/5Hz/25Hz` 三档至少人工验证一遍不会被偷偷改回 25Hz

### CG2

- 级别：`P2`
- 标题：`GNSS 配置仍然会在每次开机都保存到模块闪存，和“仅在变更时保存”的意图不一致`

位置：

- `src/gps.cpp:626-636`

问题说明：

代码注释写的是：

- `Save only if config changed`

但实际逻辑是：

- 只要执行到 `ubx_cfg_rate(GPS_FIX_RATE_HZ)`
- 就无条件 `need_save = true`

因此只要设备每次启动都会设置 rate，就每次都会：

- `Saving config to flash...`
- `ubx_cfg_save()`

这会带来两个问题：

1. 启动时间被平白拉长
2. 对 GNSS 模块非易失存储做了不必要的重复写入

虽然这不是立刻炸的 `P0/P1`，但和当前“每次上电都在反复训模块”的 bring-up 状态叠在一起，不建议继续保留。

Claude 任务：

1. 让“是否需要 save”真正和“配置是否发生变化”绑定
2. 如果只是检测到已经在目标 baud / 目标 rate / 目标 sentence set，就不要再次 save
3. 日志上区分清楚：
   `already configured`
   `reconfigured`
   `saved`

验收标准：

- 模块已配置完成后再次重启，不再每次都打印 `Saving config to flash...`
- 真正发生 baud / rate / sentence set 变化时才保存

### CG3

- 级别：`P2`
- 标题：`Lap list 空状态文案在首圈出现后可能残留在屏幕上`

位置：

- `src/display.cpp:524-537`

问题说明：

这轮为减少闪屏，把 lap list 页面改成了：

- 进入页面时清一次屏
- 后续增量刷新

但当 `lap_count == 0` 时，会在屏幕中间画：

- `No laps yet`

然后直接 `return`。

一旦后面开始有圈速，代码只会：

- 继续画 header
- 继续画 lap rows

却不会主动清掉原先中间那句空状态文案。

结果就是：

- 第一次进入 lap list 没圈时，显示 `No laps yet`
- 后面有圈之后，屏幕上可能仍残留这句字
- 直到切屏再回来才消失

这是典型的“优化清屏后引入的残影回归”。

Claude 任务：

1. 给 lap list 的 empty-state 单独做清理逻辑
2. 从 `0 laps -> 有 laps` 这个状态切换时，必须保证空状态文字被覆盖或清除
3. 不要靠“用户切屏回来一次”来消掉残影

验收标准：

- 初始 `No laps yet`
- 完成第一圈后进入 lap list，不再看到残留的 `No laps yet`
- 向后翻页/返回第一页也不会出现旧字残影

### 本轮正向结论

这次也有两点是正向进展，不要漏掉：

1. `platformio.ini` 切到 `esp32-s3-devkitc-1-n16r8` 是对的，方向正确
2. driving screen 右上角 current lap time 已从“伪造值”改成了基于 `current_lap_start_us` 的真实 elapsed time，这条旧 finding 可以视为已关闭

### 对 Claude 的建议处理顺序

1. 先修 `CG1`
2. 再修 `CG2`
3. 然后修 `CG3`
4. 修完后做一次真机回归，至少覆盖：
   `GPS 频率设置`
   `重启后是否重复 save`
   `lap list 首圈显示`
