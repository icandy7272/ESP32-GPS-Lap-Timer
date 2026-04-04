# Code Review Findings Index

日期：2026-04-04
项目：`ESP32_track_GPS`
用途：保留原始 review 结论索引，修复执行请看
[CLAUDE_FIX_TASKS_2026-04-04.md](/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/docs/CLAUDE_FIX_TASKS_2026-04-04.md)

说明：

- 这份文件只保留问题索引，不再重复展开修复方案
- Claude 处理时应先验证 finding 在当前分支是否仍然成立
- 行号基于当时 review 的工作区版本，后续代码变动后可能失准

## Findings

1. `P2` [src/lap_timer.cpp]
位置：`src/lap_timer.cpp:473-483`
标题：`'No reference lap yet' is rendered as OFF TRACK`

2. `P0` [src/main.cpp]
位置：`src/main.cpp:66-106`
标题：`Full app modules never start`

3. `P1` [src/display.cpp]
位置：`src/display.cpp:264-307`
标题：`GPS status is never propagated to shared state`

4. `P1` [src/lap_timer.cpp]
位置：`src/lap_timer.cpp:303-323`
标题：`Debounce path confirms every tentative crossing`

5. `P1` [src/session.cpp]
位置：`src/session.cpp:80-88`
标题：`Recording enters active state even if SD session creation fails`

6. `P1` [src/storage.cpp]
位置：`src/storage.cpp:541-559`
标题：`Session save can collide and still report success`

7. `P1` [src/session.cpp]
位置：`src/session.cpp:72-90`
标题：`Starting a new session does not reset lap-timer state`

## 给 Claude 的最短要求

1. 逐条验证以上 finding 在当前分支是否仍然成立。
2. 若成立，按
[CLAUDE_FIX_TASKS_2026-04-04.md](/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/docs/CLAUDE_FIX_TASKS_2026-04-04.md)
执行最小修复。
3. 若已修复，不要重复改代码，直接给出证据并关闭该项。
