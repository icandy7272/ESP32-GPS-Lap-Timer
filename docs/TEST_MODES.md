# 构建模式与测试参数

本项目 PlatformIO 有两个构建环境。**默认环境永远是生产模式**，测试模式必须用
`-e` 显式指定。

## 环境一览

| 环境名 | 用途 | 构建命令 |
|--------|------|----------|
| `esp32-s3-devkitc-1` | **生产**（默认） | `pio run` |
| `esp32-s3-devkitc-1-walking-test` | 步行测试（opt-in） | `pio run -e esp32-s3-devkitc-1-walking-test` |

上传同理，加 `-t upload`。

`pio run` 不带 `-e` 时只构建默认环境（因为 `platformio.ini` 里设了
`default_envs = esp32-s3-devkitc-1`），**绝不会**意外把步行测试参数烧到设备上。

## 生产模式参数

过线检测的标准阈值，适用于真实赛道 / 车辆驾驶场景：

| 常量 | 值 | 作用 |
|------|-----|------|
| `HEADING_WINDOW` | 60° | 过线方向允许的偏差窗口（±度） |
| `ARM_DISTANCE_M` | 10 m | 武装前累计移动距离 |
| `MIN_LAP_TIME_MS` (lap_timer) | 15000 ms | delta 引擎拒绝小于此值的圈 |
| `LAP_SHORT_THRESHOLD_MS` (session) | 15000 ms | session 层归类 `LAP_STATUS_SHORT` |
| `MIN_CROSSING_SPEED_KMH` | 1.0 km/h | 过滤 GPS 静止漂移（两种模式都保留） |

## 步行测试模式参数（WALKING_TEST_MODE）

为了**室内 / 低速步行**验证过线计时逻辑，放宽了以下阈值：

| 常量 | 值 | 原因 |
|------|-----|------|
| `HEADING_WINDOW` | 180° | GPS 步行速度下航向抖动大，等于禁用方向门 |
| `ARM_DISTANCE_M` | 3 m | 走几步即可武装 |
| `MIN_LAP_TIME_MS` (lap_timer) | 8000 ms | 步行绕一小圈 8-15 秒可以算有效（2026-04-18 从 5000 ms 上调，避免在起终线附近的折返触发假短圈） |
| `LAP_SHORT_THRESHOLD_MS` (session) | 8000 ms | 两层阈值同步切换，避免 split-brain |
| `MIN_CROSSING_SPEED_KMH` | 1.0 km/h | 保留 |

⚠️ **不要把此模式的固件带到真赛道。** 方向门禁用后，反向过线（折返）也会
被算作有效圈，数据会乱。

## 切换逻辑（一个宏门控）

所有测试参数都在 `#ifdef WALKING_TEST_MODE` 分支里，由编译器决定编译哪一套值。

- `src/lap_timer/lap_timer_internal.h` — 定义 `HEADING_WINDOW` 等
- `src/session.cpp` — 定义 `LAP_SHORT_THRESHOLD_MS`

**两处共用一个宏**，所以不会出现 lap_timer 接受但 session 拒绝的 split-brain。

## 其它与面包板相关的保守参数

独立于 `WALKING_TEST_MODE`，但焊到洞洞板/PCB 后也要改回：

| 参数 | 面包板 | 洞洞板/PCB | 文件 |
|------|--------|------------|------|
| `SD_SPI_MHZ` | 4 | 10 或 25 | `src/storage/storage_internal.h` |
| `f_cpu` | 160 MHz | 240 MHz | `platformio.ini`（`board_build.f_cpu`） |

原因：面包板接触电阻/噪声导致 BROWNOUT，降速降频是临时措施。焊接后阻抗改善，
恢复全速即可。

## 赛道部署检查清单

> ⚠️ 当前 repo 默认值仍是**面包板安全值**（`f_cpu=160MHz`、`SD_SPI_MHZ=4`）。
> 上赛道前需要手动编辑源码把下面两项改成生产值，再重新编译上传。
> `WALKING_TEST_MODE` 已经默认关闭（不需要额外操作）。

### 需要手动编辑的源码（焊到洞洞板/PCB 之后）

| 修改 | 文件 | 面包板值 → 生产值 |
|------|------|----------------|
| CPU 频率 | `platformio.ini`（`board_build.f_cpu`） | `160000000L` → `240000000L` |
| SD SPI 速度 | `src/storage/storage_internal.h`（`SD_SPI_MHZ`） | `4` → `25`（或先试 `10`） |

### 编译与上传（默认 env 即生产，无需 `-e`）

```sh
pio run -t upload
```

### 上电验证

1. 启动后串口应看到：
   - `[track] loaded: <name>`
   - `[BOOT][...][GPS][ok] track matched: <name>`
   - **无** `BROWNOUT` 字样（如有，检查 470µF 电容和电源接线）
2. 场地低速绕一圈，应触发：
   - `[xing] L0 ARMED at ...m`
   - `[xing] L0 CROSSED arm=...`
   - `[lap] first crossing — timer started`
3. 反向过线应被 `[xing] side_flip hdiff=... REJECTED` 丢弃
4. 长时间原地静止，应无任何 `[xing]` 日志（速度门槛生效）
5. SD 卡生成有效的 `.vbo` 文件（通过 WiFi 下载验证）

## 历史记录

- **2026-04-15**：首次引入 walking-test 参数（直接改常量，易 split-brain）
- **2026-04-16**：改成 `WALKING_TEST_MODE` 宏门控，lap_timer 和 session 同步
- **2026-04-16**：拆成独立 PlatformIO env，默认环境永远生产模式
