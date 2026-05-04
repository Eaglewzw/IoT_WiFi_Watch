# IoT_WiFi_Watch

基于 ESP8266 NodeMCU 的物联网 WiFi 手表，使用 0.96 寸 OLED 显示屏，集成网络时间、天气预报、MQTT 智能家居控制和闹钟功能。代码在开源项目 WeatherStation 的基础上改进，并参考了其他开源设计。

## 主要功能

- **NTP 网络时间同步** — 通过 SNTP 协议从 `pool.ntp.org` 获取上海时区时间
- **心知天气 API** — 获取当天实时天气及未来三天预报，支持 39 种天气状态的中文/图标显示
- **MQTT 智能家居控制** — 通过 MQTT 协议远程控制台灯开关、空调风速，接收室内温湿度数据
- **三组事务闹钟** — 支持独立开关，闹钟数据通过 EEPROM 掉电保存
- **SmartConfig 配网** — 支持 ESP-Touch 快速配网，同时支持自动重连已保存的 WiFi
- **系统设置** — 屏幕亮度、蜂鸣器音量、自动息屏时间、电池信息、WiFi 管理

## 状态机架构

固件采用**两层状态机**架构，基于 OLEDDisplayUi 框架实现页面管理：

```
                         D7 按键 (Menu/Next)
                    ┌──────────────────────────┐
                    │                          │
                    ▼                          │
    ┌─────────────────────────────────────────────────────┐
    │              主状态机 (uiFrameIndex 0~4)              │
    │                                                      │
    │  ┌────────┐ ┌────────┐ ┌────────┐ ┌────────┐ ┌────────┐
    │  │ Frame0 │→│ Frame1 │→│ Frame2 │→│ Frame3 │→│ Frame4 │
    │  │ 主界面  │ │  闹钟  │ │  天气  │ │  MQTT  │ │  设置  │
    │  └────────┘ └────────┘ └────────┘ └────────┘ └────────┘
    │       │          │          │          │          │
    │     D3按键    D3按键     D6/D5     D3按键      D3按键
    │       │          │       上/下        │          │
    │       ▼          ▼          ▼          ▼          ▼
    │  DisplayFlag SwitchClock WeatherFlag Mqttflag  SetFlag
    │   (0~1)      (0~9)       (0~2)      (0~3)     (0~4)
    └─────────────────────────────────────────────────────┘
```

### 主状态机 — 5 个界面帧 (Frame)

通过 D7 按键在 5 个界面之间循环切换：

| Frame | 函数 | 说明 |
|-------|------|------|
| 0 | `draw_MeunFram` | 主界面：日期、时钟、当前气温、所在地区、WiFi/电量状态 |
| 1 | `draw_ClockFram` | 闹钟界面：三组事务闹钟的时/分/开关设置 |
| 2 | `draw_WeatherFram` | 天气界面：今天/明天/后天的天气图标与温度范围 |
| 3 | `draw_MqttFram` | MQTT 界面：连接控制、台灯开关、空调风速、室内温湿度 |
| 4 | `draw_SetFram` | 设置界面：亮度、音量、息屏时间、电池信息、WiFi 管理 |

### 子状态机

每个 Frame 内部通过 D3 (选择) 和 D6/D5 (上/下) 按键进入子状态：

**Frame 1 闹钟 (`SwitchClock` 0~9)**
- 状态 0: 保存闹钟数据到 EEPROM
- 状态 1~3: 第一组闹钟 → 时 / 分 / 开关
- 状态 4~6: 第二组闹钟 → 时 / 分 / 开关
- 状态 7~9: 第三组闹钟 → 时 / 分 / 开关

**Frame 2 天气 (`WeatherFlag` 0~2)**
- 状态 0: 今天天气 → 状态 1: 明天 → 状态 2: 后天

**Frame 3 MQTT (`Mqttflag` 0~3)**
- 状态 0: MQTT 连接/断开控制
- 状态 1: 台灯开关 (发布 `LED_ON` / `LED_OFF`)
- 状态 2: 空调风速 (发布 `Fan_Speed:xx`)
- 状态 3: 室内温湿度显示 (订阅接收)

**Frame 4 设置 (`SetFlag` 0~4)**
- 状态 0: 屏幕亮度 (0/25/50/75/100%)
- 状态 1: 蜂鸣器音量
- 状态 2: 自动息屏时间 (1/2/5/10 分钟)
- 状态 3: 电池性能
- 状态 4: WiFi 网络连接管理

### 按键映射

| 引脚 | 功能 | 作用域 |
|------|------|--------|
| D7 | Menu/Next | 主状态机切换 (Frame 0→1→2→3→4→0) |
| D3 | Select/Back | 子状态机切换 / 主界面息屏 / 闹钟静音 |
| D6 | Up | 子状态机内增加数值或选择"开" |
| D5 | Down | 子状态机内减少数值或选择"关" |

### 后台任务

在 `loop()` 中，除了状态机 UI 刷新外，还运行以下后台逻辑：

```
loop()
 ├── ui.update()                  // 刷新当前 Frame 显示
 ├── D7 按键检测                   // 主状态机切换
 ├── ClockCheck()                 // 闹钟时间匹配 → 触发 Beep_Flag
 ├── client.loop()                // MQTT 消息接收处理
 └── 定时天气更新 (每60分钟)        // GetCurrentWeather + GetForecastWeather
```

## 硬件平台

- **MCU**: ESP8266 NodeMCU
- **显示屏**: 0.96 寸 SSD1306 OLED (I2C, 地址 0x3C, SDA=D1, SCL=D2)
- **按键**: 4 个 (D3/D5/D6/D7)
- **LED**: D0 引脚 (闹钟提醒闪烁)
- **蜂鸣器**: 闹钟响铃

## 依赖库

- `ESP8266WiFi` — WiFi 连接与 SmartConfig
- `SSD1306Wire` + `OLEDDisplayUi` — OLED 显示与 UI 框架
- `ArduinoJson` — 天气 API JSON 解析
- `PubSubClient` — MQTT 客户端
- `EEPROM` — 闹钟数据持久化存储
- `TZ` / `sntp` — NTP 时间同步

## 项目结构

```
IoT_WiFi_Watch/
├── Firmware/                    # Arduino 固件代码
│   ├── Firmware.ino             # 主程序 (状态机、UI、网络通信)
│   ├── font.h                   # 数字字体 (0~9) 和中文点阵 (16x16, 85字)
│   ├── WeatherStationImages.h   # UI 图标位图数据 (PROGMEM)
│   └── images/                  # BMP 图片源文件
├── Hardware_PCB/                # PCB 硬件设计 (Altium Designer)
│   └── IoT_Watch/               # 原理图与 PCB 工程文件
├── docs/                        # ESP8266 参考文档
└── Watch.jpg                    # 运行效果图
```

## 效果图

![运行效果](Watch.jpg)
