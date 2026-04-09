# 当前项目复审（仅保留仍然成立的问题）

复审日期：

- 2026-04-08

复审基线：

- 当前工作区代码
- `pio run -e esp32-s3-devkitc-1` 本地编译通过

说明：

- 这份文件只记录当前仍然成立的技术问题、产品/验收缺口、以及未完成的发布前验证
- 不包含历史已修复项

---

## 结论

- 当前项目还不建议直接按 v1.0 标准交付
- 编译已经能过，但赛道一致性、删除语义、存储收尾和产品验收口径仍有明显缺口

---

## 当前技术问题

### R1

- 级别：`P1`
- 标题：`晚到自动识别没有同步 active_track，赛道状态在显示 / 导出 / 删除路径上出现分裂`

位置：

- `src/main.cpp:136-189`
- `src/lap_timer.cpp:631-646`
- `src/storage.cpp:168-169`
- `src/wifi_server.cpp:509-517`

问题说明：

启动时会先把第一条赛道 preload 到 `active_track`，见 [main.cpp](/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/src/main.cpp#L136)。如果 boot 阶段 30 秒内没有拿到 GPS fix，系统仍会继续启动，并且后面仍可能把 preload 的第一条赛道当成“Track found”展示，见 [main.cpp](/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/src/main.cpp#L187)。

更关键的是，晚到 GPS fix 后，`lap_timer_task` 的自动识别只更新了 `s_track` 和 `session_state.track_name`，没有同步全局 `active_track`，见 [lap_timer.cpp](/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/src/lap_timer.cpp#L631)。但录制收尾导出的 `[laptiming]` 数据快照依赖的却仍是 `active_track`，见 [storage.cpp](/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/src/storage.cpp#L168)。Web 删除接口判断“当前活动赛道”也用的是 `active_track`，见 [wifi_server.cpp](/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/src/wifi_server.cpp#L509)。

结果就是：

- 屏幕上显示的赛道名
- `lap_timer` 真正用来判线的赛道
- 导出时写入的检测线
- Web 认为的“当前活动赛道”

可能不是同一条赛道。

这条会直接破坏“识别赛道 -> 计时 -> 导出 -> Web 管理”的一致性。

建议：

1. 统一当前活动赛道的单一真相源
2. 晚到自动识别时必须同时更新：
   - `active_track`
   - `lap_timer` 当前赛道
   - `session_state.track_name`
   - 导出用的 session track snapshot 逻辑
3. 只有真正完成识别后，才显示 `Track found`

### R2

- 级别：`P1`
- 标题：`录制中仍允许删除当前活动赛道，会直接 reset lap timer`

位置：

- `src/wifi_server.cpp:495-523`

问题说明：

`/api/tracks/delete` 当前没有像 `/api/tracks/select` 那样在录制中直接拒绝操作。只要被删的是 `active_track`，代码就会：

- 清空 `active_track`
- 调 `lap_timer_reset()`
- 把 `session_state.track_name` 改成 `No Track`

也就是说，用户完全可以在 `recording=true` 时，把正在使用的赛道删掉，导致当前 session 的赛道、计时器和后续导出状态突然断裂。

建议：

1. 录制中至少禁止删除当前活动赛道
2. 更稳妥的做法是：录制中禁止所有赛道增删改
3. API 错误信息要明确告诉前端“recording 中禁止此操作”

### R3

- 级别：`P1`
- 标题：`track_delete 在 SD 删除失败时仍返回成功并删除内存状态`

位置：

- `src/track.cpp:224-254`
- `src/wifi_server.cpp:513-523`

问题说明：

`track_delete()` 在 `sd.remove()` 失败时只打日志，但仍继续：

- 从内存数组里移除赛道
- 返回成功

上层 `wifi_server` 因此会把这次删除当成成功处理。这样会导致：

- 当前运行时列表里赛道消失
- Web UI 看到删除成功
- 但 SD 上文件其实还在
- 下一次重启后赛道又会回来

这会让用户以为设备“记不住删除操作”。

建议：

1. SD 删除失败时不要改内存状态
2. `track_delete()` 必须把失败向上返回
3. 上层 API 不要把这类失败伪装成 `track not found`

### R4

- 级别：`P1`
- 标题：`session 最终文件名的 collision probe 仍然和最终命名格式不一致`

位置：

- `src/storage.cpp:204-220`
- `src/storage.cpp:670-693`

问题说明：

`build_final_path()` 里用于找序号冲突的 probe 格式是：

- `sessions/YYYYMMDD_HHMMSS_Track_001.vbo`

但最终真正写出来的文件名格式却是：

- `sessions/YYYYMMDD_Track_HHMMSS_001.vbo`

也就是说，collision probe 查的不是最终真实路径。随后 `storage_end_session()` 虽然又会对 `final_path` 做一次存在检查，但只会简单追加一个 `_2`，并不能覆盖所有重复场景。

在这些情况下仍有风险：

- RTC 未同步时反复落到 `00000000_000000`
- 同一秒内重复创建 session
- 目标 `_2` 文件本身也已经存在

结果可能是：

- `rename()` 失败
- 录制结束后没有拿到最终文件名
- 数据仍滞留在临时文件

建议：

1. collision probe 必须直接使用最终命名格式
2. 序号逻辑要真正循环寻找可用名，而不是只追加一次 `_2`
3. rename 失败后的用户可见语义也要补齐

---

## 当前产品 / 验收缺口

### P1

- 级别：`P2`
- 标题：`多候选赛道选择仍未交付，PRD / TODO / 代码口径不一致`

位置：

- `docs/PRD.md:214-219`
- `TODOS.md:14-18`
- `src/track.cpp:147-160`
- `src/lap_timer.cpp:620-641`

问题说明：

PRD 明确写的是：

- 5km 内匹配到多条赛道时，要在屏幕上显示候选列表，并用按键选择

但当前代码只是：

- 统计候选数量
- 直接选最近的一条

同时 `TODOS.md` 又把这项写成：

- `Deferred to v1.1`

这表示现在存在三套口径：

1. PRD 说这是 v1.0 P0
2. 代码没有实现
3. TODO 说延期到 v1.1

建议：

1. 要么把功能补上
2. 要么正式从 v1.0 PRD 中回收
3. 不要继续保持“文档说有、代码没有、TODO 说以后再做”的状态

### P2

- 级别：`P2`
- 标题：`接线文档仍把 BK-880 描述成统一的 3.3V + PPS 方案，和当前实际模块情况不匹配`

位置：

- `docs/WIRING.md:39-43`
- `docs/WIRING.md:101-111`
- `docs/WIRING.md:251-257`

问题说明：

当前接线文档把 BK-880 统一写成：

- `VCC = 3.3V`
- `PPS = GPIO16 可直接接`
- `不可接 5V`

但就当前实际 bring-up 使用的 BK-880 模块变体来说，这套描述并不可靠，至少应该明确“不同板级变体可能不同”，否则用户会按错误接法接线，直接影响搜星、供电稳定性和 PPS 预期。

建议：

1. 文档中明确 BK-880 存在板级变体
2. 不要把 `3.3V 供电` 和 `PPS 可直接接出` 写成无条件结论
3. 对当前项目实际验证过的模块，单独写清楚供电和 PPS 能力

---

## 当前仍未完成的发布前验证

这些不是代码 bug，但按仓库自己的 `TODOS.md`，它们仍然是 v1.0 发布前应完成的验证：

- GPS 冷启动时间验证，见 [TODOS.md](/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/TODOS.md#L5)
- 计时精度对标验证，见 [TODOS.md](/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/TODOS.md#L6)
- 2 小时连续录制 endurance test，见 [TODOS.md](/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/TODOS.md#L7)
- 断电恢复测试，见 [TODOS.md](/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/TODOS.md#L8)
- 阳光下 TFT 可读性测试，见 [TODOS.md](/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/TODOS.md#L9)
- WiFi 距离测试，见 [TODOS.md](/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/TODOS.md#L10)
- 热测试，见 [TODOS.md](/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/TODOS.md#L11)
- 电池续航测试，见 [TODOS.md](/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/TODOS.md#L12)

---

## 建议处理顺序

1. 先修 `R1`
2. 再修 `R2`
3. 然后修 `R3`
4. 接着修 `R4`
5. 最后决定 `P1`、`P2` 是补实现还是改 PRD / 文档口径

---

## 给 Claude 的执行要求

1. 只处理这份文件里的当前问题，不要再回收历史已关闭项
2. 每修一条都说明：
   - 根因
   - 改动文件
   - 验证方法
   - 是否完全关闭
3. 如果某条准备延期，必须同步改 PRD / TODO / 文档口径
