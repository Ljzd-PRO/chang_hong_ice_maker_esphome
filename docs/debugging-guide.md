# 调试与抓包教程

语言：中文 | [English](debugging-guide.en.md)

本文面向已经安装或准备安装 `ChangHongIceMakerESPHome` 的用户，说明如何用标准固件或调试固件抓取状态识别数据。抓包数据主要用于排查接线、状态误判、算法阈值和设备兼容性问题。

## 先选择抓包方式

| 方式 | 适合场景 | 数据内容 | 影响 |
| --- | --- | --- | --- |
| 标准固件 | 日常使用中偶发 `unknown`、状态跳变、控制失败 | HA 诊断实体、ESPHome 日志摘要、1 kHz 分类特征 | 不需要换固件；高频诊断实体默认禁用 |
| 调试固件 | 需要分析新机器、改算法、区分待机/小冰等疑难问题 | 每秒 D1-D4 结构化帧、均值/方差/边沿/支路差分、ADC 后端信息 | 需要 OTA 刷入调试固件；不建议长期运行 |

普通排查优先使用标准固件。只有标准固件数据不足以判断问题时，再切换到调试固件。

## 安全要求

- 已连接 `P1-P5` 后，优先用 Wi-Fi 日志，不要直接把 ESP32-C3 接电脑 USB。
- 如果必须使用 USB 串口，建议使用 USB 隔离器；否则先断开制冰机面板线。
- 不要在制冰机通电时插拔 `P1-P5`。
- 调试固件的 `Bias Mode` 默认保持 `Float`。`Internal Pullup` / `Internal Pulldown` 只用于短时探查，测试后立即点击 `Restore Floating Inputs`。
- ADC 数值是浮地直连方案下的相对特征，不是真实面板电压。

## 准备本地工具

如果只在 Home Assistant 页面查看实体，可以跳过本节。

如果要保存日志或运行分析脚本，需要在仓库根目录安装 ESPHome：

```sh
python3 -m venv .venv-esphome
.venv-esphome/bin/pip install esphome pyserial
```

ESPHome CLI 会读取 YAML 中的 `!secret`，所以本地需要有 `secrets.yaml`：

```sh
cp secrets.example.yaml secrets.yaml
```

如果你使用 Release 固件，只是为了查看日志而不是重新编译，可以把 `api_encryption_key`、`ota_password`、`fallback_ap_password` 填成 Release 页面公开的固定值；`wifi_ssid` 和 `wifi_password` 填当前网络信息即可。

确认设备在线：

```sh
.venv-esphome/bin/esphome logs chang-hong-ice-maker-esphome.yaml \
  --device chang-hong-ice-maker-esphome.local
```

## 使用标准固件抓取数据

标准固件适合抓取“实际使用时”的状态识别过程。它不会输出每一个 ADC 采样点，而是输出固件用于分类的摘要特征。

### 启用诊断实体

标准固件中高频诊断实体默认禁用。需要排查时，在 Home Assistant 中启用：

1. 打开 `Settings -> Devices & services`。
2. 进入 `Chang Hong Ice Maker ESPHome` 设备页面。
3. 打开实体列表，显示被禁用的实体。
4. 只启用本次需要的诊断实体。

建议启用：

```text
Classified State
Fast State Candidate
Feature State Candidate
ADC Signature
Fast Ratio 0HHHH
Fast Ratio MHMHH
Small Feature Score
Standby Feature Score
Standby Blink Score
Standby Window Valid
P2 StdDev
Delta P2 P4
Delta P5 P2
P1 Raw
P2 Raw
P3 Raw
P4 Raw
P5 Raw
Action State
Action Result
Action Busy
```

如果只是排查“是否稳定识别”，通常看 `State`、`Power`、`Large Ice`、`Classified State`、`ADC Signature`、`P2 StdDev`、`Delta P2 P4`、`Delta P5 P2` 就够了。

### 通过 HA 页面记录

适合快速反馈问题：

1. 记录当前可见面板状态，例如“待机电源灯慢闪”、“大冰常亮”、“小冰常亮”。
2. 截图 HA 设备页的控制、传感器和诊断实体。
3. 等待 1-2 分钟，确认 `State`、`Power`、`Large Ice` 是否发生异常跳变。
4. 如果发生异常，记录大约时间点和面板实际状态。

### 保存 ESPHome 日志

适合提交 issue 或后续算法分析：

```sh
mkdir -p standard_logs
.venv-esphome/bin/esphome logs chang-hong-ice-maker-esphome.yaml \
  --device chang-hong-ice-maker-esphome.local \
  | tee standard_logs/standard-$(date +%Y%m%d-%H%M%S).log
```

抓包时建议分别记录：

- 待机 2 分钟
- 大冰运行 2 分钟
- 小冰运行 2 分钟
- 待机 -> 大冰 -> 小冰 -> 大冰 -> 待机的切换过程

标准固件日志中的重点字段：

```text
state                 对外状态
classifier            内部分类
fast                  快速候选
feature               直接特征候选
sig                   ADC 签名
fast_0HHHH            大冰签名占比
fast_MHMHH            小冰/待机签名占比
small_score           小冰特征得分
standby_score         待机特征得分
p2_stddev             P2 标准差
d_p2_p4               P2-P4 平均差分
d_p5_p2               P5-P2 平均差分
standby_valid         慢窗口待机是否有效
action_state          当前动作
action_result         最近动作结果
```

### 减少 HA 历史数据

排查结束后建议禁用刚才启用的高频诊断实体。否则 Home Assistant recorder 会长期保存这些数据。

也可以在 HA 的 `configuration.yaml` 中排除：

```yaml
recorder:
  exclude:
    entity_globs:
      - sensor.chang_hong_ice_maker_esphome_p?_raw
      - sensor.chang_hong_ice_maker_esphome_*ratio*
      - sensor.chang_hong_ice_maker_esphome_*score*
      - sensor.chang_hong_ice_maker_esphome_delta_*
      - sensor.chang_hong_ice_maker_esphome_*candidate*
    entities:
      - sensor.chang_hong_ice_maker_esphome_adc_signature
      - sensor.chang_hong_ice_maker_esphome_p2_stddev
```

修改 recorder 配置后需要重启 Home Assistant。

## 使用调试固件抓取数据

调试固件用于高频探查，不负责日常控制。它会输出 D1-D4 结构化调试帧，并在 HA 中暴露大量调试实体。调试实体默认启用，方便临时抓包；不要把调试固件长期留在正式 HA 环境中运行。

当前 GitHub Release 主要发布标准固件。调试固件通常需要从源码使用本地 `secrets.yaml` 编译和 OTA。

### 刷入调试固件

从标准固件 OTA 到调试固件：

```sh
.venv-esphome/bin/esphome upload chang-hong-ice-maker-debug.yaml \
  --device chang-hong-ice-maker-esphome.local
```

刷入后设备名会变为：

```text
chang-hong-ice-maker-debug.local
```

如果 mDNS 不稳定，可以改用设备 IP：

```sh
.venv-esphome/bin/esphome upload chang-hong-ice-maker-debug.yaml \
  --device 192.168.x.x
```

### 网络日志抓包

推荐使用 Wi-Fi 日志抓包：

```sh
.venv-esphome/bin/python tools/capture_debug_panel.py \
  --source esphome \
  --device chang-hong-ice-maker-debug.local \
  --duration-s 90 \
  --label debug_standby
```

抓包过程中可以输入标记：

```text
mark standby_visible
mark power_led_blinking
mark small_visible
mark large_visible
quit
```

如果设置了 `--duration-s`，到时间会自动结束；如果 `--duration-s 0`，需要输入 `quit` 结束。

输出目录类似：

```text
debug_captures/20260708-153000-debug_standby/
```

其中：

| 文件 | 内容 |
| --- | --- |
| `raw.log` | 原始 ESPHome 日志 |
| `debug_frames.csv` | 解析后的 D1-D4 结构化帧 |
| `markers.csv` | 用户输入的时间标记 |

### 串口抓包

只有在 USB 已隔离，或 `P1-P5` 未连接制冰机时，才建议用串口：

```sh
.venv-esphome/bin/python tools/capture_debug_panel.py \
  --source serial \
  --port auto \
  --duration-s 90 \
  --label debug_standby_serial
```

### 建议抓包矩阵

每个状态至少抓 60-90 秒：

```sh
# 待机
.venv-esphome/bin/python tools/capture_debug_panel.py --source esphome \
  --device chang-hong-ice-maker-debug.local --duration-s 90 --label debug_standby

# 大冰运行
.venv-esphome/bin/python tools/capture_debug_panel.py --source esphome \
  --device chang-hong-ice-maker-debug.local --duration-s 90 --label debug_large

# 小冰运行
.venv-esphome/bin/python tools/capture_debug_panel.py --source esphome \
  --device chang-hong-ice-maker-debug.local --duration-s 90 --label debug_small
```

如果正在排查误判，额外抓切换过程：

```sh
.venv-esphome/bin/python tools/capture_debug_panel.py --source esphome \
  --device chang-hong-ice-maker-debug.local --duration-s 180 --label debug_transitions
```

建议在切换时输入标记：

```text
mark standby_before_start
mark pressed_power
mark large_visible
mark pressed_select_to_small
mark small_visible
mark pressed_select_to_large
mark large_visible_again
mark pressed_power_to_standby
mark standby_visible
```

### D1-D4 帧含义

| 帧 | 主要内容 |
| --- | --- |
| `D1` | 采样数、签名、`0HHHH`/`MHMHH`/活动比例、P1-P5 均值 |
| `D2` | P1-P5 最小值、最大值、标准差 |
| `D3` | P1-P5 数字占空比、边沿数、7 条面板支路差分 |
| `D4` | ADC 后端、校准状态、错误率、估算 mV 均值 |

目前用于区分待机和小冰的关键字段通常是：

```text
signature
ratio_mhmhh
p2_stddev
delta_p2_p4
delta_p5_p2
p1_edges / p1_duty
```

## 分析调试抓包

生成报告：

```sh
.venv-esphome/bin/python tools/analyze_debug_captures.py \
  --debug-capture debug_captures/20260708-153000-debug_standby \
  --debug-capture debug_captures/20260708-153300-debug_large \
  --debug-capture debug_captures/20260708-153600-debug_small \
  --output debug_captures/analysis_report.md
```

如果本机存在旧版 `ice_panel_sniffer` 抓包归档，脚本会一起对比旧 1 kHz 数据；如果不存在，报告仍会分析新的调试固件抓包。

报告重点看：

- 实际采样率是否稳定
- ADC error rate 是否接近 0
- 待机和小冰的 `MHMHH` 是否都很高
- `p2_stddev`、`delta_p2_p4`、`delta_p5_p2` 是否能区分待机/小冰
- 0.5 秒、1 秒、2 秒窗口是否足够稳定

## 恢复标准固件

调试完成后刷回标准固件：

```sh
.venv-esphome/bin/esphome upload chang-hong-ice-maker-esphome.yaml \
  --device chang-hong-ice-maker-debug.local
```

刷回后设备名恢复为：

```text
chang-hong-ice-maker-esphome.local
```

Home Assistant 可能保留调试固件生成的旧设备或实体。确认标准固件可用后，可以在 HA 中删除旧的 debug 设备，或禁用其实体。

## 提交问题时附带哪些数据

如果需要反馈识别问题，请尽量提供：

- 制冰机型号，当前只验证 `CH-Z6Y3`
- 固件版本和 ESPHome 版本
- 接线方式，尤其是 `P1-P5 -> GPIO0-GPIO4`
- ESP32-C3 供电方式，是否连接电脑 USB 或 USB 隔离器
- 面板肉眼状态：待机/大冰/小冰/缺水/冰满
- HA 中 `State`、`Power`、`Large Ice` 的实际显示
- 标准固件日志，或调试固件的整个 `debug_captures/<timestamp-label>/` 目录
- 如果可用，Home Assistant 设备页截图

不要把包含家庭 Wi-Fi 密码、私有 API key、OTA 密码的 `secrets.yaml` 上传到 issue 或公开仓库。
