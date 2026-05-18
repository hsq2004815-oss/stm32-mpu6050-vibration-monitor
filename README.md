# STM32 MPU6050 Vibration Monitor

基于 `STM32F407 + MPU6050 + ESP8266 + Qt` 的嵌入式振动与姿态监测系统，用于实时采集设备姿态角、振动能量和温度数据，并通过串口或 WiFi 传输到上位机进行可视化、阈值告警和辅助智能分析。

这个项目适合作为简历中的综合型项目展示，因为它同时覆盖了嵌入式驱动、通信协议、桌面端可视化、告警联动和 AI 辅助分析等多个能力点。

## 项目亮点

- 使用 `MPU6050` 采集三轴加速度与陀螺仪数据，完成姿态角解算与振动能量计算
- 基于 `STM32 HAL` 和 `I2C + DMA` 实现传感器数据的高效采集
- 支持 `USART 串口` 和 `ESP8266 WiFi TCP` 双通道数据传输
- 使用 `Qt Widgets + QCustomPlot` 实现上位机实时曲线监控
- 支持本地阈值告警、日志记录、LED/蜂鸣器联动和上位机告警展示
- 集成 `DeepSeek API` 的可选智能诊断，同时提供本地规则分析兜底

## 系统架构

```text
MPU6050
  -> STM32F407
     -> 姿态角解算 / 振动计算 / 告警判断
     -> USART1 输出调试与上位机数据
     -> USART2 连接 ESP8266
        -> TCP 发送到 Qt 上位机

Qt 上位机
  -> 串口接收 / TCP 接收
  -> 实时图表展示
  -> 阈值告警与日志记录
  -> DeepSeek / 本地规则分析
```

## 技术栈

### 下位机 / 嵌入式

- `STM32F407VETx`
- `STM32CubeMX`
- `Keil MDK / uVision5`
- `C`
- `STM32 HAL`
- `CMSIS / CMSIS-DSP`
- `MPU6050`
- `ESP8266`
- `I2C`
- `UART`
- `DMA`

### 上位机 / 桌面端

- `C++`
- `Qt Widgets`
- `QSerialPort`
- `QTcpServer / QTcpSocket`
- `QCustomPlot`
- `QNetworkAccessManager`
- `QSoundEffect`

### 可选智能分析

- `DeepSeek API`
- `HTTPS / SSL`
- 本地规则引擎兜底分析

## 核心功能

### 1. 传感器采集与姿态解算

- 读取 MPU6050 的加速度、角速度和温度数据
- 将原始数据换算为实际物理量
- 使用卡尔曼滤波估计 `X / Y` 角度
- 通过角速度积分估计 `Z` 轴偏航角

### 2. 振动特征提取

- 对加速度变化进行高通思路处理
- 分别计算 `X / Y / Z` 三轴振动分量
- 合成总振动能量，作为设备运行状态判断指标

### 3. 双通道通信

- 串口输出 `QT:x,y,z,vibration` 格式数据，便于上位机解析
- 通过 ESP8266 连接 WiFi，并以 `TCP Client` 模式发送到 Qt 上位机
- 支持 WiFi 断线重连和串口回退

### 4. 告警联动

- STM32 端对角度、振动、温度进行阈值判断
- 支持 LED 闪烁、蜂鸣器提醒
- 告警消息通过串口和 WiFi 同步发送到上位机
- Qt 端提供告警状态、日志和阈值设置界面

### 5. 上位机可视化与分析

- 实时绘制姿态角和振动能量曲线
- 支持串口连接与 TCP 监听
- 支持历史数据统计分析
- 可调用 DeepSeek API 做设备状态解释和维护建议

## 涉及到的知识点

- 嵌入式外设初始化与驱动开发
- `I2C` 传感器通信
- `UART` 串口通信
- `DMA` 异步数据采集
- 传感器数据标定与物理量换算
- 卡尔曼滤波与姿态角估算
- 振动特征提取与简单信号处理
- TCP/IP 网络通信
- 串口协议设计与上位机解析
- Qt 事件驱动编程
- 实时数据可视化
- 阈值告警系统设计
- AI 接口接入与本地兜底策略

## 项目结构

```text
.
├─ Project_job/                  # Qt 上位机工程
│  ├─ widget.cpp / widget.h      # 主界面、串口/TCP接收、图表与告警
│  ├─ deepseekanalyzer.cpp/.h    # DeepSeek API 调用与结果解析
│  ├─ qcustomplot.cpp/.h         # 图表组件
│  └─ Project_job.pro            # Qt 工程文件
├─ VibDemo_alarm_finished/       # STM32 固件工程
│  ├─ Core/
│  │  ├─ Src/main.c              # 主循环、DMA采集、数据发送
│  │  ├─ Src/MPU6050.c           # 传感器处理与姿态/振动计算
│  │  ├─ Src/esp8266.c           # WiFi与TCP发送
│  │  └─ Src/alarm.c             # 告警逻辑
│  ├─ MDK-ARM/                   # Keil 工程
│  └─ VibDemo.ioc                # CubeMX 配置
└─ README.md
```

## 如何运行

### 1. 固件部分

- 使用 `STM32CubeMX` 打开 [VibDemo.ioc](./VibDemo_alarm_finished/VibDemo.ioc)
- 使用 `Keil MDK` 打开 [VibDemo.uvprojx](./VibDemo_alarm_finished/MDK-ARM/VibDemo.uvprojx)
- 根据你的本地环境修改 [esp8266.h](./VibDemo_alarm_finished/Core/Inc/esp8266.h) 中的 WiFi 和上位机 IP 配置
- 编译并烧录到 `STM32F407VETx`

### 2. 上位机部分

- 使用 Qt Creator 打开 [Project_job.pro](./Project_job/Project_job.pro)
- 确保本机安装了 `Qt SerialPort`、`Qt Network`、`Qt Multimedia`
- 运行后可选择：
  - 通过串口接收 STM32 数据
  - 通过 `8080` 端口接收 ESP8266 转发的数据

### 3. DeepSeek 分析功能

- 在上位机界面输入你自己的 `DeepSeek API Key`
- 如果 SSL 不可用，程序会自动回退到本地规则分析

## 适合写进简历的描述

可以参考下面这段项目描述：

> 设计并实现基于 STM32F407、MPU6050、ESP8266 与 Qt 的设备振动姿态监测系统，完成传感器数据采集、卡尔曼滤波姿态解算、WiFi/串口双通道通信、上位机实时可视化与阈值告警，并接入 DeepSeek API 实现设备状态辅助诊断。

## 说明

- 仓库中的网络配置已替换为公开仓库安全占位值，避免泄露个人 WiFi 和本机 IP
- `README` 重点面向作品集展示，因此更强调架构、功能和技术能力说明
