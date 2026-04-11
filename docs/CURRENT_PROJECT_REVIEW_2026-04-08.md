# 当前项目复审（仅保留仍然成立的问题）

复审日期：

- 2026-04-09

复审基线：

- 当前工作区代码
- `pio run -e esp32-s3-devkitc-1` 本地编译通过

说明：

- 这份文件只记录当前仍然成立的技术问题、产品/验收缺口、以及未完成的发布前验证
- 不包含历史已修复项

---

## 结论

- 当前项目还不建议直接按 v1.0 标准交付
- 编译已经能过，当前工作区里此前跟踪的 `R1-R4` 也已收口
- 当前主要缺口已经收敛到发布前实机验证，而不是新的代码级阻塞项

---

## 当前技术问题

- 基于当前工作区代码和本地构建验证，之前跟踪的 `R1-R4` 已不再成立
- 当前仍保留的主要风险，已经转移到下面的产品 / 验收缺口与实机验证项
- 如果后续出现新的代码级问题，应在这里补充新的 `R*` 条目，而不是把已关闭问题保留在本文件中

---

## 当前产品 / 验收缺口

- 当前没有必须先修的代码级 `P*` 条目
- 近期已收口的口径：
  - `docs/WIRING.md` 已改为当前实测硬件方案：BK-880 走 `5V_SW`，蓝色 SD 模块走 `5V_SW`
  - 物理按键口径已统一为 `2` 个：自锁总开关 + 自复位录制键
  - `GPIO5` 仅保留为未来扩展位，不再作为当前硬件必接项

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

1. 完成发布前实机验证

---

## 给 Claude 的执行要求

1. 只处理这份文件里的当前问题，不要再回收历史已关闭项
2. 每修一条都说明：
   - 根因
   - 改动文件
   - 验证方法
   - 是否完全关闭
3. 如果某条准备延期，必须同步改 PRD / TODO / 文档口径
