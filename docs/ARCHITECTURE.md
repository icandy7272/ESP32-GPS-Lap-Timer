# Kart GPS Lap Timer — 技术架构文档

**版本**: v0.1
**日期**: 2026-04-02
**对应 PRD**: v0.5

---

## 目录

1. [模块结构](#1-模块结构)
2. [数据流图](#2-数据流图)
3. [FreeRTOS 任务调度](#3-freertos-任务调度)
4. [核心数据结构](#4-核心数据结构)
5. [任务间通信](#5-任务间通信)
6. [GPIO 引脚分配](#6-gpio-引脚分配)
7. [断电保护机制](#7-断电保护机制)
8. [版本功能路线图](#8-版本功能路线图)

---

## 1. 模块结构

```
src/
├── main.cpp              # 入口：初始化所有任务和外设
├── gps.cpp / gps.h       # GPS UART 接收、NMEA 解析、PPS 中断捕获、GpsPoint 生成
├── lap_timer.cpp         # 过线检测（有符号交叉 + 样条插值）、圈速/扇区计时、无效圈过滤
├── delta.cpp             # Position-Based Delta 计算（参考多段线投影、航向过滤）
├── track.cpp             # 赛道定义加载/保存、赛道自动识别（5km 距离匹配）
├── storage.cpp           # SD 卡初始化、VBO 写入、fsync 调度、断电恢复、JSON 元数据
├── display.cpp           # TFT 屏幕渲染：驾驶界面、赛后回顾界面、状态界面
├── wifi_server.cpp       # ESP32 AP 热点、HTTP 路由、VBO 文件下载、赛道管理 API
├── button.cpp            # 按键去抖（50ms）、短按/长按事件分发
├── session.cpp           # Session 状态机：Ready → Recording → Finished
└── config.cpp            # settings.json 读写、运行时配置管理
```

### 各模块职责说明

| 模块 | 核心职责 | 依赖 |
|------|----------|------|
| `gps.cpp` | UART2 接收 NMEA 语句，解析 GGA/RMC，PPS 硬件中断捕获微秒时间戳，填充 GpsPoint，发送到 gps_queue | 无 |
| `lap_timer.cpp` | 消费 gps_queue，执行有符号线段交叉检测，样条插值回溯精确穿越时刻，管理圈/扇区状态，写入 LapRecord | track.cpp, delta.cpp |
| `delta.cpp` | 维护参考圈多段线，每次 GPS 更新时投影计算进度，查找参考圈同进度时间戳，输出 delta_ms | lap_timer.cpp |
| `track.cpp` | 从 SD 卡 `tracks/` 目录加载 TrackDefinition，按距离自动识别赛道，提供赛道增删接口 | storage.cpp |
| `storage.cpp` | 管理 SD 卡 SPI 初始化，VBO 临时文件写入，30s fsync，Session 结束时原子重命名，开机扫描 .tmp | 无 |
| `display.cpp` | 消费 display_queue，局部刷新 TFT（ILI9341 SPI），3 个界面切换，驾驶界面 10 FPS | button.cpp, session.cpp |
| `wifi_server.cpp` | ESP32 软 AP，HTTP GET /api/status, /api/tracks, /api/sessions, /files/{name}.vbo | storage.cpp, track.cpp |
| `button.cpp` | GPIO 中断 + 软件去抖 50ms，发布 ButtonEvent 到 button_queue | 无 |
| `session.cpp` | 维护 SessionState，持有 current_lap、best_lap、delta_ms，是各模块共享的状态中心 | 无 |
| `config.cpp` | 读写 SD 卡 `config/settings.json`，提供 WiFi 密码、屏幕亮度等运行时配置 | storage.cpp |

---

## 2. 数据流图

```
GPS 硬件
  │
  ├─ UART2 (115200 baud, 25Hz)
  │       │
  │       ▼
  │   [GPS Task - Core 0, 优先级 22]
  │   gps_parse_nmea()
  │   → 解析 GGA + RMC → 组装 GpsPoint (lat/lon/speed/heading)
  │       │
  │   PPS 硬件中断 (GPIO 16)
  │   → 捕获 esp_timer_get_time() → 写入 pps_sync_us
  │   → GpsPoint.timestamp_us = pps_sync_us + uart_offset_us
  │       │
  │       ▼ xQueueSend(gps_queue, &point, 0)
  │
  └─ gps_queue (depth=4, sizeof GpsPoint)
              │
              ▼
      [LapTimer Task - Core 0, 优先级 20]
              │
              ├─ 过线检测
              │  has_crossed_line() → 有符号叉积判断线段交叉
              │  → 航向窗口检查 (±60°)
              │  → Arming 状态检查 (>50m 后重新激活)
              │  → Catmull-Rom 样条插值回溯精确穿越时刻
              │  → 去抖验证 (连续 2 个 fix 确认方向)
              │  → xQueueSend(lap_event_queue, &event, 0)
              │
              ├─ Delta 计算
              │  project_to_polyline(current_point, ref_polyline)
              │  → 计算进度 progress (0.0~1.0)
              │  → 航向差 > 90° → 不更新
              │  → 横向距离 > 30m → 冻结 Delta，标记 OFF_TRACK
              │  → 查参考圈时间戳 → 计算 delta_ms
              │  → xSemaphoreTake(session_mutex)
              │     session_state.delta_ms = delta_ms
              │  xSemaphoreGive(session_mutex)
              │
              └─ xQueueSend(vbo_write_queue, &vbo_entry, 0)
                          │
          ┌───────────────┘
          │
          ▼
  [Storage Task - Core 1, 优先级 18]
  vbo_write_ring_buffer 消费
  → SD 卡 SPI 写入 _recording.vbo.tmp
  → 每 30s fsync()
  → Session 结束: close() + rename()

  [lap_event_queue]
          │
          ▼
  [Session Task - Core 1, 优先级 16]
  → 更新 LapRecord，计算圈状态 (timed/slow/short)
  → 判断是否新最佳圈 → 更新参考多段线
  → xSemaphoreTake(session_mutex)
     update SessionState
  xSemaphoreGive(session_mutex)

  [Display Task - Core 1, 优先级 10]
  → 每 100ms 读取 SessionState (加 mutex)
  → 局部刷新 TFT SPI
  → xSemaphoreTake(spi_mutex) → 写屏 → xSemaphoreGive(spi_mutex)

  [WiFi Task - Core 1, 优先级 5]
  → HTTP 请求处理
  → 读取 SessionState (加 mutex)
  → VBO 文件下载 (加 spi_mutex 读 SD)
```

---

## 3. FreeRTOS 任务调度

ESP32-S3 ESP-IDF FreeRTOS 优先级范围：0（最低）~ 24（最高）。中断服务例程（ISR）不计入此范围。

| 任务名称 | 运行核心 | 栈大小 (bytes) | 优先级 | 执行频率 | 主要工作 |
|----------|----------|---------------|--------|----------|----------|
| `task_gps` | Core 0 | 4096 | 22 | 阻塞在 UART RX，25Hz 数据到来时触发 | NMEA 解析，PPS 校时，组装 GpsPoint，发送到 gps_queue |
| `task_lap_timer` | Core 0 | 8192 | 20 | 每次 gps_queue 有数据时执行（约 40ms/次） | 过线检测，样条插值，Delta 计算，生成 VboEntry 和 LapEvent |
| `task_storage` | Core 1 | 6144 | 18 | 阻塞在 vbo_write_queue，有数据时立即写 | VBO 行格式化，SD 卡 SPI 写入，30s fsync，断电恢复 |
| `task_session` | Core 1 | 4096 | 16 | 阻塞在 lap_event_queue | 圈状态判断，更新 SessionState，参考圈管理，写入 LapRecord |
| `task_display` | Core 1 | 8192 | 10 | 100ms 周期（10 FPS） | 读取 SessionState，局部刷新 TFT，3 界面切换 |
| `task_button` | Core 1 | 2048 | 8 | 阻塞在 button_queue | 去抖 50ms，分发 ButtonEvent，触发界面切换 |
| `task_wifi` | Core 1 | 8192 | 5 | 事件驱动（HTTP 请求到来） | HTTP 路由处理，VBO 下载，赛道管理 API，记录模式下限制并发 |

**PPS 中断服务例程（ISR）**：运行在 Core 0，不计入 FreeRTOS 优先级体系。中断触发时调用 `esp_timer_get_time()` 捕获微秒时间戳，存入原子变量 `pps_sync_us`，然后设置 `pps_ready` 标志。

**看门狗保护（Task Watchdog Timer）**：
- `task_gps` 和 `task_lap_timer` 注册 ESP-IDF Task WDT（`esp_task_wdt_add()`），超时 5 秒
- 正常运行时每次循环调用 `esp_task_wdt_reset()` 喂狗
- 超时触发硬件重启（`esp_task_wdt_init(5000, true)` 的 panic 模式）
- 重启后 `storage_init()` 自动恢复 .tmp 文件（见第 7 节断电保护）
- Core 1 的任务（display/wifi/storage）暂不注册 WDT，避免 SD 卡长写入触发误报

---

## 4. 核心数据结构

以下为 C 语言结构体定义（`src/types.h`）：

```c
// GPS 原始测量点（由 task_gps 生成）
typedef struct {
    double   lat_deg;        // 纬度，WGS84 十进制度，北纬为正
    double   lon_deg;        // 经度，WGS84 十进制度，东经为正
    float    speed_kmh;      // 速度，km/h
    float    heading_deg;    // 航向，0~360°，正北为 0
    float    height_m;       // 海拔，WGS84，米
    int      satellites;     // 可见卫星数
    int64_t  timestamp_us;   // 微秒时间戳（PPS 校准后的 esp_timer 值）
    bool     pps_synced;     // 本 fix 是否使用了 PPS 校时
    bool     fix_3d;         // 是否获得 3D Fix（卫星 ≥ 6）
} GpsPoint;

// 检测线定义（起终线 / 扇区分割线）
typedef struct {
    double lat1_deg;         // 检测线端点 1 纬度
    double lon1_deg;         // 检测线端点 1 经度
    double lat2_deg;         // 检测线端点 2 纬度
    double lon2_deg;         // 检测线端点 2 经度
    float  valid_heading_deg; // 合法穿越方向（法线方向，度），±60° 窗口
} DetectionLine;

// 赛道定义（从 SD 卡 tracks/track_NNN.json 加载）
#define MAX_SECTORS 4        // 最多 4 个扇区（3 条分割线 + 1 条起终线）

typedef struct {
    char          id[32];              // 赛道 ID，ASCII，如 "track_001"
    char          name[64];            // 赛道名称，ASCII only
    DetectionLine start_finish;        // 起终线
    DetectionLine sectors[MAX_SECTORS - 1]; // 最多 3 条扇区分割线
    int           sector_count;        // 实际扇区数（1~4，即分割线数 0~3）
    double        center_lat_deg;      // 赛道中心纬度（用于自动识别）
    double        center_lon_deg;      // 赛道中心经度
    float         approx_length_m;     // 赛道近似长度（米）
} TrackDefinition;

// 圈速记录（每圈完成后由 task_session 生成）
typedef struct {
    int      lap_number;               // 圈号，从 1 开始
    int32_t  lap_time_ms;             // 圈时间，毫秒
    int32_t  sector_times_ms[MAX_SECTORS]; // 各扇区时间，毫秒；未完成扇区为 -1
    int      sector_count;             // 本圈实际完成的扇区数
    uint8_t  status;                   // LAP_STATUS_TIMED / SLOW / SHORT / NO_REF / OUT
    int64_t  finish_timestamp_us;      // 完成时刻（PPS 校准的微秒时间戳）
} LapRecord;

// 圈状态枚举
typedef enum {
    LAP_STATUS_TIMED  = 0,   // 正常计时圈
    LAP_STATUS_SLOW   = 1,   // 圈速 > 最佳圈 × 150%
    LAP_STATUS_SHORT  = 2,   // 圈时间 < 15s
    LAP_STATUS_NO_REF = 3,   // 无参考圈的首圈（作为初始参考）
    LAP_STATUS_OUT    = 4,   // 出发圈（首次穿越起终线之前）
} LapStatus;

// Session 全局状态（由 task_session 写，display/wifi 只读）
#define MAX_LAPS_PER_SESSION 100

typedef struct {
    int      current_lap;              // 当前圈号
    int      best_lap_number;          // 最佳圈圈号（-1 表示尚无）
    int32_t  best_lap_time_ms;        // 最佳圈时间（ms），-1 表示尚无
    int32_t  delta_ms;                // 当前 Delta（ms），正值 = 慢，负值 = 快
    bool     delta_valid;              // Delta 是否有效（有参考圈且 ON TRACK）
    bool     off_track;               // 是否偏离赛道 >30m
    bool     is_recording;            // 是否正在记录
    bool     gps_fix_ok;              // GPS 是否有 3D Fix
    int      gps_satellites;          // 当前卫星数
    LapRecord laps[MAX_LAPS_PER_SESSION]; // 圈速历史
    int      lap_count;               // 已完成圈数
    char     track_name[64];          // 当前赛道名称
} SessionState;

// VBO 写入队列条目（由 task_lap_timer 生成，task_storage 消费）
typedef struct {
    int      satellites;     // 卫星数
    int64_t  timestamp_us;   // 微秒时间戳（用于生成 VBO 时间列）
    double   lat_deg;        // 纬度，十进制度
    double   lon_deg;        // 经度，十进制度
    float    speed_kmh;      // 速度
    float    heading_deg;    // 航向
    float    height_m;       // 海拔
} VboEntry;

// VBO 写入环形缓冲（存放在 PSRAM，容量 ≥ 5s 数据 = 125 条）
#define VBO_RING_BUFFER_SIZE 256   // 256 条，约 10s 余量

typedef struct {
    VboEntry entries[VBO_RING_BUFFER_SIZE];
    uint16_t head;           // 写指针
    uint16_t tail;           // 读指针
    uint16_t count;          // 当前条目数
} VboRingBuffer;             // 通过 FreeRTOS Queue 实现，此结构仅供参考
```

---

## 5. 任务间通信

### FreeRTOS 队列与互斥锁一览

| 通信对象 | 类型 | 队列深度 / 说明 | 发送方 | 接收方 |
|----------|------|----------------|--------|--------|
| `gps_queue` | `QueueHandle_t` (GpsPoint) | 深度 4，丢弃旧数据 | `task_gps` | `task_lap_timer` |
| `vbo_write_queue` | `QueueHandle_t` (VboEntry) | 深度 256（PSRAM），约 10s 缓冲 | `task_lap_timer` | `task_storage` |
| `lap_event_queue` | `QueueHandle_t` (LapEvent) | 深度 16 | `task_lap_timer` | `task_session` |
| `btn_session_queue` | `QueueHandle_t` (ButtonEvent) | 深度 8 | `task_button` | `task_session` |
| `btn_display_queue` | `QueueHandle_t` (ButtonEvent) | 深度 8 | `task_button` | `task_display` |
| `session_mutex` | `SemaphoreHandle_t` (Mutex) | 互斥锁，保护 SessionState 读写 | 写：`task_session`, `task_lap_timer` | 读：`task_display`, `task_wifi` |
| `spi_mutex` | `SemaphoreHandle_t` (Mutex) | 互斥锁，保护 SPI 总线（SD + TFT 共用） | 写：`task_storage`（优先） | `task_display`（让步） |
| `pps_sync_us` | `_Atomic int64_t` | 原子变量，无锁 | PPS ISR | `task_gps` |

### 事件结构

```c
// 圈/扇区穿越事件（task_lap_timer → task_session）
typedef struct {
    uint8_t  event_type;     // LAP_EVENT_SECTOR / LAP_EVENT_FINISH
    int      sector_index;   // 扇区索引（0 = 起终线）
    int64_t  crossing_us;    // 精确穿越时刻（PPS 校准，微秒）
} LapEvent;

// 按键事件（task_button → task_display / task_session）
typedef struct {
    uint8_t  event_type;     // BUTTON_SHORT_PRESS / BUTTON_LONG_PRESS
    uint8_t  button_id;      // BUTTON_SWITCH（目前只有 1 个自复位按键）
} ButtonEvent;
```

### SPI 总线竞争策略

`task_storage` 和 `task_display` 共用同一 SPI 总线（HSPI，引脚见第 6 节）：

1. `task_storage` 需要写 SD 卡时：先 `xSemaphoreTake(spi_mutex, portMAX_DELAY)`，完成后 `xSemaphoreGive(spi_mutex)`
2. `task_display` 需要刷屏时：先 `xSemaphoreTake(spi_mutex, pdMS_TO_TICKS(10))`，超时则跳过本帧（不阻塞 SD 写入）
3. SD 写入任务优先级（18）高于 Display 任务（10），自然实现 SD 优先

---

## 6. GPIO 引脚分配

目标芯片：**ESP32-S3-N16R8**（鹿小班开发板）

### TFT 屏幕（中景园 ZJY320S0800TG02，ILI9341，SPI 4线）

| 信号 | GPIO 编号 | 方向 | 说明 |
|------|-----------|------|------|
| MOSI | GPIO 11 | OUT | SPI MOSI，与 SD 卡共用 |
| CLK  | GPIO 12 | OUT | SPI CLK，与 SD 卡共用 |
| MISO | GPIO 13 | IN  | SPI MISO，与 SD 卡共用（TFT 不需要但总线共享） |
| CS_TFT | GPIO 10 | OUT | TFT 片选，低有效 |
| DC   | GPIO 9  | OUT | 数据/命令选择（D/C） |
| RST  | GPIO 8  | OUT | TFT 硬件复位，低有效 |
| BLK  | GPIO 47 | OUT | 背光 PWM（LEDC 通道 0，1kHz，8bit），避开 GPIO46 strapping pin |

### SD 卡模块（SPI，与 TFT 共用总线）

| 信号 | GPIO 编号 | 方向 | 说明 |
|------|-----------|------|------|
| MOSI | GPIO 11 | OUT | 与 TFT 共用 |
| CLK  | GPIO 12 | OUT | 与 TFT 共用 |
| MISO | GPIO 13 | IN  | 与 TFT 共用 |
| CS_SD | GPIO 42 | OUT | SD 卡片选，低有效 |

### GPS 模块（北天 BK-880，UART2）

| 信号 | GPIO 编号 | 方向 | 说明 |
|------|-----------|------|------|
| GPS_TX | GPIO 17 | IN（ESP32 侧 RX） | NMEA 数据输入，115200 baud |
| GPS_RX | GPIO 18 | OUT（ESP32 侧 TX） | UBX 配置命令输出 |
| GPS_PPS | GPIO 16 | IN | 秒脉冲，上升沿触发硬件中断，精度 ±30ns |

### 按键

| 信号 | GPIO 编号 | 方向 | 说明 |
|------|-----------|------|------|
| BTN_REC   | GPIO 4 | IN | 自锁按键（录制开始/停止），内部上拉，低电平触发，软件去抖 50ms |
| BTN_SECTOR | GPIO 5 | IN | 瞬动按键（航段标记），内部上拉，低电平触发，软件去抖 50ms |

### 引脚总览

| GPIO | 功能 | 备注 |
|------|------|------|
| 2  | LED | 状态指示灯 |
| 4  | BTN_REC | 录制开始/停止（自锁） |
| 5  | BTN_SECTOR | 航段标记（瞬动） |
| 8  | TFT_RST | TFT 复位 |
| 9  | TFT_DC | TFT D/C |
| 10 | CS_TFT | TFT 片选 |
| 11 | SPI_MOSI | 共用 MOSI |
| 12 | SPI_CLK | 共用 CLK |
| 13 | SPI_MISO | 共用 MISO（SD 专用） |
| 16 | GPS_PPS | PPS 中断 |
| 17 | GPS_TX→ESP_RX | NMEA 输入 |
| 18 | GPS_RX→ESP_TX | UBX 配置输出 |
| 42 | CS_SD | SD 片选 |
| 47 | TFT_BLK | 背光 PWM（避开 GPIO46 strapping pin） |

---

## 7. 断电保护机制

卡丁车振动可能导致 Type-C 接触不良，必须防止 VBO 数据丢失。

### 写入时序

```
Session 开始（首次穿越起终线）
  │
  ▼
创建临时文件 _recording.vbo.tmp
写入 VBO 文件头（[header], [comments], [laptiming], [column names]）
  │
  ▼ 每次 GPS 更新（40ms）
  ├── vbo_write_queue ←──────────────────────────────────┐
  │                                                       │
  ▼ task_storage 消费队列                                 │
  格式化 VBO 数据行并 fwrite()                            │
  │                                                       │
  ▼ 每 30 秒                                              │
  fflush(fp)      ← 刷用户空间缓冲到内核                 │
  fsync(fileno(fp)) ← 内核缓冲刷到 SD 卡物理扇区        │
  │                                                       │
  ▼ Session 结束（用户关机）                               │
  fflush(fp) + fsync(fileno(fp))  ← 最终刷盘            │
  fclose(_recording.vbo.tmp)                             │
  fsync(dir_fd)  ← 刷目录元数据确保文件落盘              │
  rename("_recording.vbo.tmp",                           │
         "sessions/20260402_Track_143022_001.vbo")       │
  fsync(dir_fd)  ← 刷 rename 结果到 FAT32 目录表        │
  写入 sessions/20260402_Track_143022_001.json（元数据）
```

### 断电恢复序列

```
开机 → main.cpp → storage_init()
  │
  ▼
扫描 SD 卡根目录，查找 _recording.vbo.tmp
  │
  ├── 存在 .tmp 文件
  │       │
  │       ▼
  │   检查 sessions/ 目录是否存在同时间戳的 .vbo 最终文件
  │   ├── 存在最终文件：说明 rename 成功但 .tmp 删除失败 → 仅删除 .tmp
  │   └── 不存在最终文件：说明 rename 未完成 → 执行恢复
  │       从文件内容读取第一行时间戳（"File created on DD/MM/YYYY at HH:MM:SS"）
  │       构造恢复文件名：sessions/YYYYMMDD_UNKNOWN_HHMMSS_recovered.vbo
  │       rename("_recording.vbo.tmp", 恢复文件名)
  │       fsync(dir_fd)
  │       屏幕显示："已恢复上次未完成的 Session"（3 秒）
  │
  └── 不存在 .tmp 文件
          │
          ▼
      正常启动流程
```

### 数据完整性保证

- **最大数据丢失**：30 秒内的 GPS 数据（约 750 个 GPS fix，约 30KB VBO 数据）
- **已完成圈的圈速**：每完成一圈时追加写入 `[laptiming]` 区段（而非 Session 开始时一次性写入，因为圈速在完成前未知）。每次写入后立即 fflush+fsync 确保圈速数据落盘
- **Session 元数据 JSON**：仅在正常关机时写入，断电后 JSON 丢失可接受（VBO 数据完整，可用 vbo-tools 重新解析）

---

## 8. 版本功能路线图

### v1.0（MVP）

**目标**：可靠的圈速计时 + 实时 Delta 驾驶屏 + SD 卡 VBO 导出

| 模块 | 功能 |
|------|------|
| `gps.cpp` | UART2 接收，NMEA GGA/RMC 解析，PPS 硬件中断校时，25Hz |
| `lap_timer.cpp` | 有符号线段交叉过线检测，Catmull-Rom 样条插值精确计时，去抖验证，Arming 状态机，无效圈过滤（slow/short/no_ref） |
| `delta.cpp` | Position-Based Delta（参考多段线投影），航向过滤，OFF TRACK 检测 |
| `track.cpp` | 从 SD 卡加载赛道 JSON，按距离 <5km 自动识别，手动按键选择 |
| `storage.cpp` | VBO 写入（.tmp + 30s fsync + 原子重命名），断电恢复，Session JSON |
| `display.cpp` | 驾驶界面（Delta 大字 + 颜色背景），状态界面，开机流程，10 FPS 局部刷新 |
| `wifi_server.cpp` | AP 热点，Web 界面（赛道管理、VBO 下载、设置），记录模式限流 |
| `button.cpp` | 单按键去抖，短按切换界面（速度 >15km/h 时锁定驾驶界面） |
| `session.cpp` | Session 状态机 Ready → Recording → Finished，SessionState 管理 |
| `config.cpp` | settings.json（WiFi 密码、屏幕亮度） |

### v1.1

| 模块 | 新增功能 |
|------|----------|
| `display.cpp` | 赛后回顾界面（圈速列表滚动、理论最佳圈） |
| `session.cpp` | Session 自动开始/停止（速度 <5km/h 持续 10s → Idle） |
| `track.cpp` / `storage.cpp` | 历史最佳圈跨 Session 保留（`best_laps/track_NNN_best.json`） |
| `wifi_server.cpp` | Web 界面手动标记/取消无效圈，历史最佳圈切换 |

### v2.0

| 模块 | 新增功能 |
|------|----------|
| `track.cpp` | 卫星地图点选创建赛道（需预缓存地图瓦片或 Station 模式联网） |
| `wifi_server.cpp` | Web 界面赛后轨迹标记扇区，VBO 在线预览 |
| `ota.cpp`（新） | OTA 固件更新（通过 WiFi） |
| `imu.cpp`（新） | IMU（加速度计）G 值采集，存入 VBO 扩展列 |
| `profile.cpp`（新） | 多车手 Profile 管理，弯心最低速度，滚动圈速 |

---

*本文档与 PRD v0.5 保持同步。实现过程中如有引脚冲突或性能问题，请在此文档更新，并在 git commit 中注明变更原因。*
