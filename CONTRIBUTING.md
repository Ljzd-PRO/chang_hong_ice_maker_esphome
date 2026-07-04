# 贡献指南

感谢你愿意改进 ChangHongIceMakerESPHome。这个项目同时涉及 ESPHome、C++ 组件、Home Assistant 实体模型和真实家电硬件，所以贡献时最重要的是：小步修改、可验证、不要让固件在未知状态下主动乱发按键。

## 项目结构

| 路径 | 用途 |
| --- | --- |
| `chang-hong-ice-maker-esphome.yaml` | 主固件配置，面向日常 Home Assistant 使用。 |
| `chang-hong-ice-maker-debug.yaml` | 调试固件配置，面向高频采样、串口抓包和信号分析。 |
| `components/chang_hong_ice_maker_esphome/` | 主固件本地 ESPHome external component，包含状态识别和远程按键逻辑。 |
| `components/chang_hong_ice_maker_panel_debug/` | 调试固件组件，包含更详细的 ADC 统计和诊断实体。 |
| `tests/` | 分类器回归测试。Python 代码镜像了主固件的关键识别逻辑。 |
| `tests/fixtures/` | 可提交的小型测试 fixture。不要提交完整抓包大文件。 |
| `tools/` | 调试抓包、抓包分析和 Home Assistant 压力测试工具。 |
| `docs/` | 测试记录、图片、原理图和 README 引用资源。 |
| `CHANGELOG.md` | 发布说明来源。release workflow 会从这里提取对应 tag 的 release notes。 |

`secrets.yaml`、`.esphome/`、`.venv-*`、`debug_captures/` 都不应提交。

## 开发环境

建议使用独立虚拟环境：

```sh
python3 -m venv .venv-esphome
.venv-esphome/bin/pip install esphome
```

准备本地密钥文件：

```sh
cp secrets.example.yaml secrets.yaml
```

`secrets.yaml` 只用于本地编译和刷机，不要提交真实 Wi-Fi、OTA 密码或 API encryption key。

## 常用验证命令

提交前至少运行：

```sh
python3 -m unittest discover -s tests -v
.venv-esphome/bin/esphome config chang-hong-ice-maker-esphome.yaml
```

如果改了主固件 C++、YAML、依赖或实体，继续运行：

```sh
.venv-esphome/bin/esphome compile chang-hong-ice-maker-esphome.yaml
```

如果改了调试固件，运行：

```sh
.venv-esphome/bin/esphome config chang-hong-ice-maker-debug.yaml
.venv-esphome/bin/esphome compile chang-hong-ice-maker-debug.yaml
```

如果改了 release workflow，确认 `CHANGELOG.md` 中存在目标 tag 段落。release workflow 匹配格式是：

```md
## v1.0.0 - 2026-07-05
```

## 状态识别算法改动

主固件的状态识别逻辑在 C++ 组件中，测试镜像在 `tests/test_panel_classifier_from_captures.py`。修改算法时必须同步更新两边，否则测试不能证明固件行为。

改动原则：

- 不要只凭单个 ADC 原始值判断状态。当前直连浮地方案只能使用相对特征。
- `0HHHH` 是大冰运行的强特征。
- `MHMHH` 同时出现在待机和小冰，必须结合 `P2 StdDev`、`P2-P4`、`P5-P2` 或慢闪特征。
- `standby -> running_small` 不应由被动检测直接发生，因为 `CH-Z6Y3` 从待机启动必然先进入大冰。
- 诊断候选可以波动，但对外 `State`、`Power`、`Large Ice` 应尽量稳定。

新增或调整阈值时，请补充测试：

- 待机数据不能误报为小冰。
- 小冰数据能在短窗口内识别。
- 大冰数据能快速识别。
- 过渡窗口可以是 `unknown`，但稳定后必须恢复正确状态。

完整原始抓包通常较大，不要直接提交。需要加入 CI 的数据请提取成小型 fixture，并说明来源和用途。

如果本机有旧抓包归档，可用环境变量启用更完整测试：

```sh
ICE_PANEL_CAPTURE_ARCHIVE=/path/to/ice_panel_sniffer-captures.tar.gz \
  python3 -m unittest discover -s tests -v
```

## Home Assistant 实体改动

主实体是用户自动化依赖的稳定接口：

```text
switch.chang_hong_ice_maker_esphome_power
switch.chang_hong_ice_maker_esphome_large_ice
button.chang_hong_ice_maker_esphome_uv_toggle
sensor.chang_hong_ice_maker_esphome_state
```

修改实体时请遵守：

- 不要轻易改已有实体 ID、语义或默认行为。
- 新增诊断实体要标记 `entity_category: diagnostic`。
- 会影响用户自动化的变更必须写入 `README.md` 和 `CHANGELOG.md`。
- 控制动作必须互斥或有明确队列策略，不能让快速点击产生不可预期的按键脉冲。
- UV 没有可靠反馈，只能作为按钮或明确标注为不可靠状态，不能伪装成真实状态开关。

## 实机调试安全

当前硬件方案是直连 `P1-P5 -> GPIO0-GPIO4`，有电气风险。实机测试时：

- 制冰机断电后等待至少 30 秒再接线。
- 不连接制冰机 GND 到 ESP32-C3 GND。
- 已连接 `P1-P5` 时，不要把 ESP32-C3 直接接电脑 USB；优先使用两脚 USB 电源或充电宝。
- 必须看 USB 串口时，使用 USB 隔离器。
- 不要在制冰机通电时插拔 `P1-P5`。
- 不要在状态为 `unknown` 且原因不明时增加新的主动控制逻辑。

OTA 到真实设备前，先编译通过。OTA 后至少做一次基本检查：

1. 待机 2 分钟，`State=standby`、`Power=off`、`Large Ice=on` 不应异常跳变。
2. 大冰运行 2 分钟，不应误判为小冰、待机或 `unknown`。
3. 小冰运行 2 分钟，不应误判为大冰、待机或 `unknown`。
4. 远程 `Power`、`Large Ice`、`UV Toggle` 动作后，GPIO 必须恢复输入状态。

## 调试固件和抓包

调试固件用于分析信号，不用于长期控制制冰机。常用流程：

```sh
.venv-esphome/bin/esphome upload chang-hong-ice-maker-debug.yaml --device chang-hong-ice-maker-esphome.local
.venv-esphome/bin/python tools/capture_debug_panel.py --port auto --duration-s 90 --label debug_standby
.venv-esphome/bin/python tools/analyze_debug_captures.py
```

抓包输出放在 `debug_captures/`，默认被 Git 忽略。只有经过裁剪、脱敏、体积小且能作为回归 fixture 的数据才适合提交到 `tests/fixtures/`。

调试结束后刷回主固件：

```sh
.venv-esphome/bin/esphome upload chang-hong-ice-maker-esphome.yaml --device chang-hong-ice-maker-debug.local
```

如果设备名已经恢复，也可以使用 IP 地址。

## 文档要求

README 面向最终用户，不要放开发日志、内部标识符解释或维护者自述。贡献者内容放在本文件或 `docs/`。

需要更新文档的常见情况：

- 接线或供电要求变化。
- Home Assistant 实体变化。
- 状态识别机制、限制或故障排查方式变化。
- release 产物、OTA、配网流程变化。
- 新增支持机型或改变适配范围。

## 发布流程

发布版本时：

1. 更新 `chang-hong-ice-maker-esphome.yaml` 中的 `project_version`。
2. 在 `CHANGELOG.md` 添加对应版本段落，例如 `## v1.0.0 - 2026-07-05`。
3. 确认 release notes 适合用户阅读，不只是提交摘要。
4. 运行测试和 ESPHome config/compile。
5. 提交后创建 annotated tag：

```sh
git tag -a v1.0.0 -m "Release v1.0.0"
```

6. push `main` 和 tag 后，GitHub Actions 会构建固件并创建或更新 Release：

```sh
git push origin main
git push origin v1.0.0
```

GitHub Release 的正文来自 `CHANGELOG.md` 中对应 tag 的版本段落，workflow 会自动追加固件产物列表。

## Pull Request 清单

提交 PR 前确认：

- [ ] 没有提交 `secrets.yaml`、`.esphome/`、`.venv-*`、完整抓包或本地缓存。
- [ ] `python3 -m unittest discover -s tests -v` 通过。
- [ ] `esphome config chang-hong-ice-maker-esphome.yaml` 通过。
- [ ] 改了固件逻辑时，已运行对应 compile。
- [ ] 改了状态识别时，C++ 和 Python 测试镜像已同步。
- [ ] 改了用户可见行为时，README 和 CHANGELOG 已更新。
- [ ] 实机测试结果写清楚，包括设备状态、持续时间、是否有误判或拒绝动作。
