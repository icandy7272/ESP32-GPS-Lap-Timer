# ESP32-S3 GPS 轨迹记录仪 — 接线指南

> 适用硬件：ESP32-S3 N16R8 DevKit（乐鑫官方开发板，38/44 pin）
> 编写日期：2026-04-02

---

## 目录

1. [接线总览表](#接线总览表)
2. [TFT 屏接线](#tft-屏接线)
3. [SD 卡模块接线](#sd-卡模块接线)
4. [GPS 模块接线](#gps-模块接线)
5. [按钮接线](#按钮接线)
6. [电源接线](#电源接线)
7. [ASCII 连接示意图](#ascii-连接示意图)
8. [注意事项](#注意事项)

---

## 接线总览表

| 模块 | 模块引脚 | ESP32-S3 GPIO | 说明 |
|------|----------|---------------|------|
| **TFT (ILI9341)** | GND | GND | 接地 |
| TFT | VCC | 3.3V | 注意：ILI9341 供电为 3.3V，不可接 5V |
| TFT | CLK (SCLK) | GPIO12 | SPI 时钟，与 SD 共享 |
| TFT | MOS (MOSI) | GPIO11 | SPI 数据输出，与 SD 共享 |
| TFT | RES (RST) | GPIO8 | 屏幕复位 |
| TFT | DC | GPIO9 | 数据/命令选择 |
| TFT | CS | GPIO10 | SPI 片选（TFT 专用） |
| TFT | BLK | GPIO47 | 背光控制（PWM 调亮度） |
| **SD 卡模块** | GND | GND | 接地 |
| SD 卡 | VCC | 3.3V | 大多数 SPI SD 模块支持 3.3V；若模块自带稳压则可接 5V（见注意事项） |
| SD 卡 | MISO | GPIO13 | SPI 数据输入（SD 专用，TFT 无 MISO） |
| SD 卡 | MOSI | GPIO11 | SPI 数据输出，与 TFT 共享 |
| SD 卡 | SCK | GPIO12 | SPI 时钟，与 TFT 共享 |
| SD 卡 | CS | GPIO42 | SPI 片选（SD 专用） |
| **GPS BK-880** | GND | GND | 接地；若 GPS 独立供电，必须与 ESP32 共地 |
| GPS BK-880 | VCC | **看模块变体** | 当前项目实测这块 BK-880 载板优先按 **5V 供电**；不要默认所有 BK-880 都接 3.3V |
| GPS BK-880 | TX | GPIO17 | GPS 发送 → ESP32 接收（UART1 RX） |
| GPS BK-880 | RX | GPIO18 | GPS 接收 ← ESP32 发送（UART1 TX） |
| GPS BK-880 | PPS | GPIO16 | **仅在模块/线束确实引出 1PPS 时才连接**，否则留空 |
| **按钮 1（锁定）** | 一端 | GPIO4 | 另一端接 GND；代码启用 INPUT_PULLUP |
| **按钮 2（瞬动）** | 一端 | GPIO5 | 另一端接 GND；代码启用 INPUT_PULLUP |
| **LED 状态指示** | 正极 | GPIO2 | 负极接 GND，串接 220Ω 限流电阻 |
| **电源** | 5V 电池 USB-C | ESP32-S3 USB-C 口 | 直插即可，板载稳压转 3.3V |

---

## TFT 屏接线

**模块型号：** ZJY320S0800TG02，3.2 寸，驱动 IC：ILI9341，8 根连线

| # | 模块引脚标注 | 连接位置 | 说明 |
|---|-------------|----------|------|
| 1 | GND | ESP32-S3 GND | 接地 |
| 2 | VCC | ESP32-S3 3.3V | **必须接 3.3V**，ILI9341 不支持 5V 逻辑/供电 |
| 3 | CLK | GPIO12 | SPI 时钟线 |
| 4 | MOS | GPIO11 | SPI MOSI（主机发送，屏幕接收） |
| 5 | RES | GPIO8 | 复位引脚，低电平复位 |
| 6 | DC | GPIO9 | 数据/命令选择：高电平=数据，低电平=命令 |
| 7 | CS | GPIO10 | 片选，低电平有效 |
| 8 | BLK | GPIO47 | 背光使能。接 GPIO 可用 PWM 控制亮度；若不需调光也可直接接 3.3V 常亮 |

**要点：**
- ILI9341 逻辑电平为 3.3V，若 VCC 误接 5V 会损坏芯片
- BLK 接 GPIO47 后，代码中用 `ledcWrite()` 即可实现亮度调节
- TFT 仅作为输出设备，无需 MISO 线

---

## SD 卡模块接线

**接口：** SPI，6 根连线

| # | 模块引脚标注 | 连接位置 | 说明 |
|---|-------------|----------|------|
| 1 | GND | ESP32-S3 GND | 接地 |
| 2 | VCC | ESP32-S3 3.3V | 纯 SPI SD 模块接 3.3V；带板载 LDO 的模块可接 5V（看模块丝印） |
| 3 | MISO | GPIO13 | SD 卡数据输出，此引脚 TFT 不使用 |
| 4 | MOSI | GPIO11 | **与 TFT 共用**，同一根物理线 |
| 5 | SCK | GPIO12 | **与 TFT 共用**，同一根物理线 |
| 6 | CS | GPIO42 | SD 专用片选，与 TFT CS（GPIO10）独立 |

**共享 SPI 总线说明：**
- TFT 和 SD 卡共用 MOSI（GPIO11）和 SCK（GPIO12）
- 通过各自的 CS 引脚区分：操作 TFT 时拉低 GPIO10，操作 SD 时拉低 GPIO42
- 同一时刻只能有一个 CS 为低电平，不可同时操作两个设备
- Arduino/ESP-IDF 的 SPI 库会自动管理 CS，配置正确即可

---

## GPS 模块接线

**模块型号：** BK-880 GNSS 模块，UART 通信

> 重要：BK-880 存在不同板级变体，卖家页面、说明书、实物线束和实际供电方式可能不完全一致。
> 当前项目 bring-up 过程中验证到的这块模块，更稳妥的接法是：
> - 模块 **VCC 接 5V**
> - UART `TX/RX` 仍为 **3.3V TTL**，可直接接 ESP32-S3
> - `PPS` 是否可接，要看你的模块或附带线束是否真的把 1PPS 引出来

| # | 模块引脚标注 | 连接位置 | 说明 |
|---|-------------|----------|------|
| 1 | GND | ESP32-S3 GND | 接地；若 GPS 用外部 5V 供电，仍必须和 ESP32 共地 |
| 2 | VCC | **5V 或 3.3V，取决于模块变体** | 当前项目实测这块 BK-880 载板建议优先接 **5V** |
| 3 | TX | GPIO17 | GPS 模块发送 → ESP32 接收（**交叉连接**） |
| 4 | RX | GPIO18 | GPS 模块接收 ← ESP32 发送（**交叉连接**） |
| 5 | PPS | GPIO16 | **可选**；只有模块或线束确实引出 1PPS 时才接 |

**要点：**
- TX/RX 必须交叉：GPS 的 TX 接 ESP32 的 RX，GPS 的 RX 接 ESP32 的 TX
- 当前代码引脚定义见 [pins.h](/Users/wenchaodu/Documents/Claude_code_projects/ESP32_track_GPS/src/pins.h)：`RX=GPIO17`、`TX=GPIO18`、`PPS=GPIO16`
- 当前固件会自动尝试 `115200 / 38400 / 9600` 三种常见波特率；探测到后会尽量切到 `115200`
- 25Hz 下 NMEA 数据量较大，长期稳定运行仍建议最终工作在 `115200`
- 若模块没有导出 PPS，也可以先不接；固件仍可按 UART 模式运行
- 若 GPS 模块单独接 5V 供电，**一定要和 ESP32 共地**

---

## 按钮接线

**两个按钮均采用相同接线方式：**

```
GPIO4 ──────── 按钮一端
                  |
               [按钮]
                  |
GND  ──────── 按钮另一端
```

| 按钮 | 类型 | GPIO | 功能 |
|------|------|------|------|
| 按钮 1 | 12mm **锁定**（自锁）按钮 | GPIO4 | 录制开始/停止 |
| 按钮 2 | 12mm **瞬动**（自复位）按钮 | GPIO5 | 航段标记（Sector Mark） |

**代码初始化示例：**
```cpp
pinMode(4, INPUT_PULLUP);  // 按钮1，按下时读到 LOW
pinMode(5, INPUT_PULLUP);  // 按钮2，按下时读到 LOW
```

**要点：**
- 启用内部上拉（INPUT_PULLUP）后无需外接上拉电阻
- 未按下时 GPIO 读取为 HIGH（3.3V），按下后接 GND 读取为 LOW
- 建议在代码中加 20-50ms 消抖延迟

---

## 电源接线

**方案：5V USB-C 电池直接为 ESP32-S3 供电**

```
5V USB-C 电池
      |
      | USB-C 线
      |
ESP32-S3 USB-C 口
      |
   板载 LDO
      |
    3.3V（为所有外设供电）
```

- 5V 电池通过 USB-C 线直插 ESP32-S3 开发板的 USB-C 接口
- ESP32-S3 板载稳压器将 5V 转为 3.3V，输出到 3.3V 引脚
- TFT 走 3.3V；SD 卡按模块选择 3.3V 或 5V；**GPS BK-880 不要默认跟着接 3.3V**
- 对当前项目实测这块 BK-880 载板，更建议给 GPS 模块单独提供 **5V**
- 如果 GPS 走独立 5V 电源，务必把 `GPS GND`、`ESP32 GND`、外部 5V 电源地连接在一起

**3.3V 引脚电流预算（参考）：**

| 模块 | 典型电流 |
|------|---------|
| TFT ILI9341 | ~20 mA |
| SD 卡模块 | ~50-100 mA（写入时峰值） |
| GPS BK-880 | ~20-30 mA（仅在它确实从 ESP32 3.3V 取电时计入） |
| LED | ~5-10 mA |
| **合计** | ~100-160 mA |

ESP32-S3 板载 LDO 通常可提供 500mA，以上总负载在安全范围内。

---

## ASCII 连接示意图

```
                    ┌─────────────────────────────────┐
                    │       ESP32-S3 N16R8 DevKit      │
                    │                                  │
  ┌──────────────┐  │  GPIO11 ──── MOSI (SPI共享) ────┼──→ TFT MOS
  │  5V USB-C    │  │  GPIO12 ──── SCLK (SPI共享) ────┼──→ TFT CLK
  │   电池       │  │  GPIO10 ──── CS ────────────────┼──→ TFT CS
  └──────┬───────┘  │  GPIO9  ──── DC ────────────────┼──→ TFT DC
         │ USB-C    │  GPIO8  ──── RST ───────────────┼──→ TFT RES
         └──────────┤  GPIO47 ──── BLK ───────────────┼──→ TFT BLK
                    │  3.3V   ──────────────────────── ┼──→ TFT VCC
                    │  GND    ──────────────────────── ┼──→ TFT GND
                    │                                  │
                    │  GPIO11 ──── MOSI (共享) ────────┼──→ SD MOSI
                    │  GPIO12 ──── SCLK (共享) ────────┼──→ SD SCK
                    │  GPIO13 ──── MISO ───────────────┼──→ SD MISO
                    │  GPIO42 ──── CS ────────────────┼──→ SD CS
                    │  3.3V   ──────────────────────── ┼──→ SD VCC
                    │  GND    ──────────────────────── ┼──→ SD GND
                    │                                  │
                    │  GPIO17 ←─── RX ←──── GPS TX ───┼── GPS BK-880
                    │  GPIO18 ──── TX ────→ GPS RX ───┼── GPS BK-880
                    │  GPIO16 ←─── PPS ←─── GPS PPS ──┼── GPS BK-880
                    │  3.3V   ──────────────────────── ┼──→ GPS VCC
                    │  GND    ──────────────────────── ┼──→ GPS GND
                    │                                  │
                    │  GPIO4  ──┐                      │
                    │           ├── [按钮1 自锁]        │
                    │  GND    ──┘                      │
                    │                                  │
                    │  GPIO5  ──┐                      │
                    │           ├── [按钮2 瞬动]        │
                    │  GND    ──┘                      │
                    │                                  │
                    │  GPIO2  ──[220Ω]──[LED]── GND   │
                    │                                  │
                    └─────────────────────────────────┘

SPI 共享总线示意：
  GPIO11 (MOSI) ──┬──→ TFT MOS
                  └──→ SD MOSI

  GPIO12 (SCLK) ──┬──→ TFT CLK
                  └──→ SD SCK

  GPIO13 (MISO) ←── SD MISO   (TFT 无此引脚)

  CS 独立控制：
    GPIO10 → TFT CS（操作 TFT 时拉低）
    GPIO42 → SD CS （操作 SD 时拉低）
```

---

## 注意事项

### 1. 避开的 Strapping 引脚

ESP32-S3 启动时以下引脚电平会影响启动模式，**慎用或避用**：

| 引脚 | 说明 |
|------|------|
| GPIO0 | 启动模式选择（低电平=下载模式），避免接外部上拉/下拉 |
| GPIO3 | JTAG 相关，启动时需为低电平，谨慎使用 |
| GPIO45 | VDD_SPI 电压选择（影响 Flash），不可随意拉高/拉低 |
| GPIO47 | 已用作 TFT BLK；注意该引脚在某些版本中也是 strapping pin，启动完成后可正常使用 |

**GPIO47 特别说明：** 在 ESP32-S3 中，GPIO47 是 strapping pin（控制 ROM 消息打印），默认需为低电平。BLK 背光默认低电平（关背光）不影响启动，上电稳定后再用 PWM 控制，实测无问题。若遇到启动异常，可改接 GPIO47 或其他非 strapping 引脚。

### 2. 3.3V vs 5V 注意事项

| 模块 | 供电电压 | 说明 |
|------|---------|------|
| TFT ILI9341 | **3.3V** | 不可接 5V，会损坏驱动 IC |
| GPS BK-880 | **看模块变体** | 当前项目实测这块载板更适合 5V 输入；UART 仍是 3.3V TTL |
| SD 卡模块 | 看模块 | 纯 SD 卡槽接 3.3V；带 LDO 的 SD 模块可接 5V |
| 按钮 | 无需供电 | GPIO + GND 即可 |

### 3. 共享 SPI 总线注意事项

- MOSI（GPIO11）和 SCLK（GPIO12）由 TFT 和 SD 卡共享，使用同一根导线分别接到两个模块
- 同一时刻只能激活一个 CS：操作 TFT 时 GPIO10=LOW、GPIO42=HIGH；操作 SD 时 GPIO42=LOW、GPIO10=HIGH
- Arduino 的 `TFT_eSPI` 库和 `SD` 库均支持共享 SPI 总线，配置文件中指定各自 CS 引脚即可
- SPI 频率：TFT 可达 40-80 MHz；SD 卡建议 25 MHz 以下

### 4. GPS 串口接线验证

- 上电后若 GPS 无 NMEA 输出，首先检查 TX/RX 是否接反
- 当前实测这块 BK-880 在 5V 模块供电下，TX 仍输出约 3.0-3.3V TTL 电平，可直接接 ESP32-S3
- PPS 灯闪烁不等于线束一定已经把 1PPS 引脚导出；接线前先确认模块焊盘或附带线束定义
- 默认等待 30-60 秒冷启动定位；首次上电或室内环境可能更久

### 5. 走线建议

- SPI 线尽量短，避免长线干扰（特别是高速 TFT 刷新时）
- GPS 模块远离 ESP32 核心，减少 RF 干扰
- 电源线尽量粗短，建议在 3.3V 和 GND 之间就近放置 100μF 电解电容 + 100nF 陶瓷电容滤波

---

## GPIO 引脚汇总

| GPIO | 功能 | 说明 |
|------|------|------|
| GPIO2 | LED | 状态指示灯 |
| GPIO4 | 按钮 1 | 录制开始/停止（自锁） |
| GPIO5 | 按钮 2 | 航段标记（瞬动） |
| GPIO8 | TFT RST | 屏幕复位 |
| GPIO9 | TFT DC | 数据/命令选择 |
| GPIO10 | TFT CS | TFT 片选 |
| GPIO11 | SPI MOSI | TFT + SD 共享 |
| GPIO12 | SPI SCLK | TFT + SD 共享 |
| GPIO13 | SPI MISO | SD 卡专用 |
| GPIO16 | GPS PPS | 秒脉冲（可选，仅在模块/线束确实引出时使用） |
| GPIO17 | UART1 RX | 接 GPS TX |
| GPIO18 | UART1 TX | 接 GPS RX |
| GPIO42 | SD CS | SD 卡片选 |
| GPIO47 | TFT BLK | 背光 PWM 控制 |
