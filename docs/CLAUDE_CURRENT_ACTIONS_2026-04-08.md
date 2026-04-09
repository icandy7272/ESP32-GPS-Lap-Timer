# 给 Claude 的当前最小待办

用途：

- 这份文件只保留“当前还需要处理的事项”
- 不保留大段历史 review 过程
- 如果需要追溯历史背景，再去看归档文件：
  `docs/CLAUDE_FIX_TASKS_2026-04-04.md`

当前建议：

- 先读这份小文件
- 不要先通读归档大文件
- 只有在你需要核对旧 finding、历史判断、或产品背景时，再去翻归档

---

## 已确认不要重复修的项

1. driving screen 右上角 `current lap time` 已不再是 synthetic value
2. `fix_3d = satellites >= 6` 这个明显错误的判断已经去掉，现已改为基于 `GGA fix_quality`
3. `platformio.ini` 改为 `esp32-s3-devkitc-1-n16r8` 方向正确

---

## 当前待处理项

### A1

- 级别：`P1`
- 标题：`gps_rate_hz 仍被硬编码成 25Hz`

位置：

- `src/gps.cpp`

问题：

- 当前 `uart_init()` 里仍然直接写死：
  - `GPS_FIX_RATE_HZ = 25`
  - `FIX_INTERVAL_US = 40000`
- 后面还会无条件下发 `ubx_cfg_rate(GPS_FIX_RATE_HZ)`

影响：

- Web / `settings.json` 的 `gps_rate_hz` 设置不生效
- 调试阶段无法稳定切到 `1Hz / 5Hz / 10Hz`
- 每次开机都可能把模块改回 25Hz

要求：

1. 恢复 `app_config.gps_rate_hz` 为运行时单一真相源
2. 仅对非法值做兜底
3. `FIX_INTERVAL_US` 与最终生效频率一致
4. 日志明确打印最终采用的 GPS rate

验收：

- 修改 `gps_rate_hz` 后，重启仍保持该值
- 日志可见最终采用频率

### A2

- 级别：`P2`
- 标题：`GNSS 配置仍然每次开机都会 save`

位置：

- `src/gps.cpp`

问题：

- 代码注释说“只在配置变化时保存”
- 但实际仍会在设置 rate 后直接 `need_save = true`
- 所以开机后仍可能每次都跑 `ubx_cfg_save()`

影响：

- 启动时间增加
- GNSS 模块非易失存储被不必要重复写入

要求：

1. `need_save` 真正绑定到“配置确实变化”
2. 若已在目标 baud / rate / sentence set，则不要重复 save
3. 日志区分：
   - `already configured`
   - `reconfigured`
   - `saved`

验收：

- 已完成配置后再次重启，不再每次都打印 `Saving config to flash...`

### A3

- 级别：`P2`
- 标题：`TFT SPI 频率宣称改成 10MHz，但实际编译值仍可能是 40MHz`

位置：

- `platformio.ini`
- `src/User_Setup.h`

问题：

- `platformio.ini` 传了 `-DSPI_FREQUENCY=10000000`
- 但 `src/User_Setup.h` 仍重新定义 `SPI_FREQUENCY 40000000`

影响：

- “为了飞线稳定而降速”的改动可能实际上没生效
- 配置存在双重定义和真相源冲突

要求：

1. 统一 `SPI_FREQUENCY` 的单一真相源
2. 不要让 `platformio.ini` 和 `src/User_Setup.h` 双重定义互相打架
3. 如果决定用 `10MHz`，确保最终编译值真的就是 `10MHz`

验收：

- 不再出现 `SPI_FREQUENCY` 重定义冲突
- 最终编译值和声明一致
- 真机显示稳定

### A4

- 级别：`P2`
- 标题：`Lap list 的 “No laps yet” 空状态文案可能残留`

位置：

- `src/display.cpp`

问题：

- lap list 现在只在进入页面时清一次屏
- `lap_count == 0` 时会画 `No laps yet` 然后直接返回
- 后续有圈后只画 header 和 rows，没有显式清掉旧文案

影响：

- 第一圈出现后，页面可能还残留 `No laps yet`

要求：

1. 给 empty-state 做独立清除逻辑
2. 覆盖 `0 laps -> 有 laps` 的状态切换
3. 不要依赖切屏回来一次来消除残影

验收：

- 初始显示 `No laps yet`
- 第一圈出来后该文案消失
- 翻页/返回第一页不出现旧字残影

---

## 建议处理顺序

1. 先修 `A1`
2. 再修 `A3`
3. 然后修 `A2`
4. 最后修 `A4`

---

## 输出要求

每修一条，请固定输出：

1. 根因是什么
2. 改了哪几个文件
3. 怎么验证
4. 这条现在是“已关闭”还是“仍有风险”
