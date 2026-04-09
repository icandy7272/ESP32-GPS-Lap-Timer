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
- 但产品口径、接线文档口径，以及发布前实机验证仍有明显缺口

---

## 当前技术问题

- 基于当前工作区代码和本地构建验证，之前跟踪的 `R1-R4` 已不再成立
- 当前仍保留的主要风险，已经转移到下面的产品 / 验收缺口与实机验证项
- 如果后续出现新的代码级问题，应在这里补充新的 `R*` 条目，而不是把已关闭问题保留在本文件中

---

## 当前产品 / 验收缺口

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

1. 先修正文档里的 `P2`
2. 然后完成发布前实机验证

---

## 给 Claude 的执行要求

1. 只处理这份文件里的当前问题，不要再回收历史已关闭项
2. 每修一条都说明：
   - 根因
   - 改动文件
   - 验证方法
   - 是否完全关闭
3. 如果某条准备延期，必须同步改 PRD / TODO / 文档口径
