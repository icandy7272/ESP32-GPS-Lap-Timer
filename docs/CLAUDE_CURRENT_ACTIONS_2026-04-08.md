# 给 Claude 的当前最小待办

用途：

- 这份文件只保留"当前还需要处理的事项"
- 不保留大段历史 review 过程
- 如果需要追溯历史背景，再去看归档文件：
  `docs/CLAUDE_FIX_TASKS_2026-04-04.md`

当前建议：

- 先读这份小文件
- 不要先通读归档大文件
- 只有在你需要核对旧 finding、历史判断、或产品背景时，再去翻归档

---

## 最后更新

- 时间：`2026-04-09`
- 基线：`main @ 4850b4f`（`feat(gps): add per-second fix rate and queue drop diagnostics`）

---

## 已确认不要重复修的项

1. driving screen 右上角 `current lap time` 已不再是 synthetic value
2. `fix_3d = satellites >= 6` 这个明显错误的判断已经去掉，现已改为基于 `GGA fix_quality`
3. `platformio.ini` 的 board 是 `esp32-s3-devkitc-1-n16r8` 方向正确
4. **A1 已关闭** — `src/gps.cpp:597-602` 已从 `app_config.gps_rate_hz` 读取 GPS 频率，1-25 Hz 兜底验证，`FIX_INTERVAL_US` 派生一致，日志打印最终采用的 rate。`src/wifi/api_settings.cpp` 也已正确解析/回显 `gps_rate_hz`。`src/storage.cpp` 的 VBO header 也用 `app_config.gps_rate_hz`。修复 commit：`645ae3d`
5. **A2 已关闭** — `need_save` 初始 false，只在 baud 切换成功时改 true。已在目标 baud/rate 时只刷 RAM 不刷 flash。日志区分 `reconfigured: saving to flash` vs `already configured (no flash save)`。修复 commit：`645ae3d`
6. **A3 已关闭** — `src/User_Setup.h:46` 已删除 `#define SPI_FREQUENCY 40000000`，改为注释"SPI_FREQUENCY is set via build_flags in platformio.ini"。`platformio.ini:44` 的 `-DSPI_FREQUENCY=10000000` 现在真正生效。面包板 flying-wire 阶段 SPI 10 MHz 跑稳。焊接/PCB 之后可以直接改 `platformio.ini` 一行升频。修复 commit：`645ae3d`
7. **A4 代码已写好，验证阻塞** — `src/display/display_lap_list.cpp:62-86` 的 `s_laplist_empty_drawn` 状态机完整：进入空状态时画 `No laps yet`，转到非空时 `fillRect` 清掉 header 下面的整块。header 本身不被清。修复 commit：`645ae3d`

---

## 当前待处理项

### V1 — A4 真机验证

- 级别：`P3`（代码已完成）
- 标题：`lap list 首圈文案清除的真机验证`
- 阻塞：等 SD 卡模块到货 + 定义一条真实赛道 + 实际跑一圈
- 当前状态：代码路径走读过，逻辑正确，`pio run` 通过。唯一缺口是没有端到端跑过 "`0 laps → 1 laps`" 这个过渡
- 验收：
  - 初始显示 `No laps yet`
  - 真机跑出第一圈后该文案消失
  - 翻页 / 返回第一页不出现旧字残影

### R1 — src/gps.cpp 模块化拆分

- 级别：`P2`
- 标题：`src/gps.cpp 740 行贴着 800 上限，按功能拆到 src/gps/`
- 背景：`src/display.cpp` 和 `src/wifi_server.cpp` 已经按同样的方式拆过（commits `3c060af`、`eed5eee`）。`src/gps.cpp` 是唯一仍然单文件的大模块
- 拟定切分：
  - `src/gps.h` 保持不变（公共 API）
  - `src/gps/gps_internal.h` — 跨文件 extern 状态、struct 定义、helper 声明
  - `src/gps/gps.cpp` — `gps_init`、`gps_task`、顶层 glue
  - `src/gps/gps_pps.cpp` — PPS ISR + state（`pps_isr`、`pps_read`、`is_pps_fresh`、`pps_init`）
  - `src/gps/gps_nmea.cpp` — NMEA 纯解析（checksum、field split、`parse_gga`、`parse_rmc`、`sync_rtc_from_rmc`、`detect_sentence_type`）
  - `src/gps/gps_fix.cpp` — fix 组装与分发（`assemble_point`、`process_sentence`、`send_fix_if_ready`、`feed_char`、`compute_timestamp`、NMEA buffer）
  - `src/gps/gps_ubx.cpp` — UBX 协议（`ubx_*` 全套 + `uart_detect_nmea` + `uart_init`）
- 要求：零行为变更，所有注释保留，公共 API 不变
- 验收：`pio run -e esp32-s3-devkitc-1` 绿

---

## 未来（非阻塞，不用现在做）

这里是 workbench / product 层面的后续方向，不是 bug。纯粹作为下次 session 的提示板：

- **LIVE sector 重设计**：当前 workbench 的 S3 "LIVE" 标签是硬编码文本，颜色继承主 delta。应该改成显示当前 sector 的 partial delta（数字）、颜色按自身 delta 符号着色。涉及 scenario schema + device_screen.js 渲染 + firmware 里 `lap_timer` / `session` 层新增 "current sector partial delta" 数据流。先在 workbench 里做假数据原型定稿，再回到 firmware 实算
- **Dashboard / Diagnostics 原型**：在 workbench 里加两个新场景/视图，承载之前讨论过的实时状态（卫星数、fix quality、speed、lap 等）+ 历史诊断（卫星数折线、rate actual vs configured、drops 计数、NMEA tail）。种子数据源已经就绪（`feat(gps): add per-second fix rate and queue drop diagnostics`）
- **push 到 origin**：当前 main 领先 `origin/main` 30+ commits，还没 push。用户明确要求时再推

---

## 输出要求

每修一条，请固定输出：

1. 根因是什么
2. 改了哪几个文件
3. 怎么验证
4. 这条现在是"已关闭"还是"仍有风险"
