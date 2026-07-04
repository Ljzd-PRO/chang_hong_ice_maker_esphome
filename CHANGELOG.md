# 更新日志

## v0.2.3 - 2026-07-05

### 新增

- 明确记录长虹 `CH-Z6Y3` 为已验证适配机型。
- 主固件和调试固件新增 `Target Model` 诊断文本实体。

### 变更

- 更新固件注释和 README 兼容性说明，明确已验证目标机型为 `CH-Z6Y3`；不改变节点名、项目 ID、组件名或已有 Home Assistant 实体 ID。

## v0.2.2 - 2026-07-04

### 变更

- 增加状态机转移护栏，避免待机状态在没有明确命令上下文时被被动识别为小冰。
- 增加运行状态锁存，避免短暂 `unknown` 或慢速待机窗口残留证据覆盖已确认的大冰/小冰运行状态。
- 开机确认改为必须确认大冰，匹配实体机器“从待机启动必然先进入大冰”的行为。

### 测试

- 增加待机锁存和运行锁存边界场景的合成回归测试。
- 对真实机器完成 OTA 后实测：
  - 130 秒待机
  - 130 秒大冰
  - 130 秒小冰
  - 切回大冰后的 130 秒大冰
  - 关机后的 130 秒待机
- 5 组真实机器稳定性窗口中，外部 `State`、`Power`、`Large Ice` 均无异常跳变。

## v0.2.1 - 2026-07-04

### 变更

- 使用 1 秒特征投票窗口改进待机/小冰分类，特征包括：
  - `P2` 标准差
  - `P2-P4` 平均差分
  - `P5-P2` 平均差分
- 保留 16 秒待机慢闪检测作为兜底，不再把它作为每次区分待机/小冰的唯一依据。
- `Fast State Candidate` 改为表示经过连续确认后的短窗口特征候选。

### 新增

- 新增 `Feature State Candidate`、`Small Feature Score`、`Standby Feature Score`、`P2 StdDev`、`Delta P2 P4`、`Delta P5 P2` 诊断实体。
- 新增精简 DMA 调试 fixture，使 CI 可在不依赖本地 sniffer 归档的情况下验证待机/小冰特征分离。

## v0.2.0 - 2026-07-04

### 变更

- 将此前三档 `Mode` 选择实体替换为两个模板开关：
  - `Power`
  - `Large Ice`
- 待机启动行为改为匹配实体机器：每次从待机启动都先视为大冰。
- `Large Ice` 在待机/关闭状态下默认显示为 `ON`；非运行状态下拒绝小冰命令。
- 面板状态分类从单一 32 秒窗口改为两个窗口：
  - 2 秒快速窗口，用于大冰/小冰运行候选。
  - 16 秒待机窗口，用于电源灯慢闪确认。
- 增加快速状态候选、快速签名比例和待机窗口有效性诊断。

### 说明

- 本版本有意改变 Home Assistant 实体 ID。升级后可删除或忽略旧的 `select.chang_hong_ice_maker_esphome_mode` 实体。
- 待机确认仍慢于运行状态检测，因为它依赖电源灯慢闪。

## v0.1.0 - 2026-07-01

ChangHongIceMakerESPHome 项目的首个公开固件版本。

### 新增

- 新增 ESPHome 固件，可通过 ESP32-C3 将长虹制冰机面板接入 Home Assistant。
- 新增 `P1-P5 -> GPIO0-GPIO4` 直连 GPIO 面板集成，使用 ADC 签名识别状态。
- 新增 Home Assistant `select` 实体作为主运行模式：
  - `Off`
  - `Small Ice`
  - `Large Ice`
- 新增 Home Assistant `button` 实体，通过模拟原面板“选择”键长按实现 UV 切换。
- 新增 classified state、ADC signature、confidence、standby blink score、action state、action result、action busy、`P1-P5` 原始 ADC 值等诊断实体。
- 新增固件版本和 ESPHome 编译版本诊断实体。
- 新增固定 Wi-Fi 凭据存储逻辑，避免 OTA 后丢失已配网信息。
- 新增 Native API、OTA、Fallback AP 配网、captive portal 和 BLE Improv 配网支持。
- 新增启动、ADC 分类、GPIO 按键脉冲和拒绝命令的串口/调试日志。
- 新增本地 ESPHome external component C++ 实现。
- 新增 README 安装说明，包含接线、刷机、配网、Home Assistant 添加、面板照片、logo 和设备页面截图。
- 新增基于抓包数据的分类器回归测试；本地存在 sniffer 归档时可使用原始抓包数据。
- 新增 GitHub Actions 工作流，用于测试、固件构建和固件发布。

### 硬件说明

- 当前固件沿用开发期间实测可用的直连 GPIO 接线。
- 直连 GPIO 方案是已知风险方案，并且只针对已测试机器；长期安装建议增加串联电阻、钳位或隔离保护。
- 当前仍使用 `GPIO2` 连接 `P3`；如果某些开发板因此无法启动，可把 `P3` 改接到 `GPIO5`，并同步修改固件引脚映射。

### 发布产物说明

- GitHub release 固件产物使用 CI 占位 Wi-Fi、OTA 和 API encryption secrets 构建。
- CI 固件产物中写入的 `api_encryption_key` 是固定值，并在这些产物中共享；它不会按用户、设备或刷机次数随机生成。
- 实际安装时，应在本地 `secrets.yaml` 中生成私有 `api_encryption_key` 和 `ota_password`，然后自行构建和刷机。
- 不要公开包含真实 ESPHome API encryption key 的截图或日志。
