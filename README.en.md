# ChangHongIceMakerESPHome

<p align="center">
  <img src="docs/images/logo.png" alt="ChangHongIceMakerESPHome logo" width="220">
</p>

Language: [中文](README.md) | English

ESPHome firmware for integrating a Chang Hong `CH-Z6Y3` ice maker into Home Assistant. An ESP32-C3 connects to the original five-wire control panel and exposes remote state detection, power control, small/large ice switching, and UV button triggering.

> The current design is a tested direct-GPIO retrofit, not a universal safe electrical interface. For long-term installations, add series current limiting, voltage clamps, or isolation on every signal line.

## Compatibility

| Item | Details |
| --- | --- |
| Verified model | Chang Hong `CH-Z6Y3` |
| Control board | Original five-wire `P1-P5` panel |
| LEDs | No Water, Ice Full, Power, Small Ice, Large Ice |
| Buttons | Power, Select |
| Controller | ESP32-C3 Super Mini or compatible ESP32-C3 board |
| Home Assistant integration | ESPHome Native API |

Other models must be treated as unverified even if the panel looks similar. Check the `P1-P5` netlist, LED/button branches, default Large Ice startup behavior, and standby/small/large ADC signatures before reusing this firmware.

## What You Get

Home Assistant entities:

| Entity | Purpose |
| --- | --- |
| `Power` | Running / standby |
| `Large Ice` | Large / small ice. While standby, it stays on because the machine always starts into Large Ice |
| `UV Toggle` | Simulates a long Select press for UV sterilization |
| `State` | `standby`, `running_large`, `running_small`, `starting`, `stopping`, `unknown` |

<details>
<summary>View Home Assistant screenshots</summary>

![Home Assistant controls and sensor entities](docs/images/home-assistant-control-sensors.png)

![Home Assistant diagnostic entities](docs/images/home-assistant-diagnostics.png)

</details>

## Panel Photos

![Ice-maker external control panel](docs/images/ice-maker-panel.jpeg)

<details>
<summary>View panel PCB photos</summary>

![Panel PCB front side with LEDs, power/select buttons, and the 5-wire connector](docs/images/panel-pcb-front.jpeg)

![Panel PCB back side showing LED, button, and resistor branch traces](docs/images/panel-pcb-back.jpeg)

</details>

## Hardware Installation

Prepare:

- ESP32-C3 Super Mini or compatible development board
- Non-earthed two-prong USB-C adapter or power bank
- Thin wires, multimeter, insulation materials

Wiring:

```text
P1 -> GPIO0
P2 -> GPIO1
P3 -> GPIO2
P4 -> GPIO3
P5 -> GPIO4
```

Rules:

- Unplug the ice maker and wait at least 30 seconds before wiring.
- Do not connect ice-maker GND to ESP32-C3 GND.
- Power the ESP32-C3 from its own USB port. Do not power it from the panel.
- Do not connect or disconnect `P1-P5` while the ice maker is powered.
- Keep bare wires, solder joints, and the ESP32-C3 back side away from metal parts.
- If `P3 -> GPIO2` prevents boot, move P3 to GPIO5 and update the firmware pin mapping.

Powering matters. After `P1-P5` are connected, use a two-prong USB-C adapter or power bank for daily operation. Do not also connect the ESP32-C3 to a computer USB port: the computer ground reference may disturb the panel scan, causing bad state detection or unresponsive original buttons. If USB serial logs are required, use a USB isolator. Wi-Fi logs and OTA are preferred.

## Flashing

Flash the ESP32-C3 before connecting it to the ice maker.

### Recommended: Use Release Firmware

Most users should use the prebuilt firmware from GitHub Releases. This does not require installing ESPHome or compiling locally.

1. Open the [Latest Release](https://github.com/Ljzd-PRO/chang_hong_ice_maker_esphome/releases/latest).
2. Download `chang-hong-ice-maker-esphome.factory.bin`.
3. Open `https://web.esphome.io/` in a Web Serial capable Chrome or Edge browser.
4. Connect the ESP32-C3 and flash the local `factory.bin` file.
5. After flashing, disconnect USB and provision Wi-Fi as described below.

If ESPHome Web is unavailable, flash with `esptool.py`:

```sh
python3 -m pip install esptool
esptool.py --chip esp32c3 --port /dev/cu.usbmodemXXXX --baud 460800 \
  write_flash 0x0 chang-hong-ice-maker-esphome.factory.bin
```

Release artifacts:

| File | Purpose |
| --- | --- |
| `chang-hong-ice-maker-esphome.factory.bin` | First USB/serial flash image. Recommended for most users. |
| `chang-hong-ice-maker-esphome.ota.bin` | OTA update image for tools that support ESPHome OTA binary upload. |
| `chang-hong-ice-maker-esphome.bin` | Raw firmware image. Most users do not need it. |
| `chang-hong-ice-maker-esphome.build_info.json` | Build metadata. |
| `SHA256SUMS` | Artifact checksums. |

Release firmware uses fixed public credentials so it can be flashed and provisioned directly:

```yaml
fallback_ap_password: "ci-fallback-password"
api_encryption_key: "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8="
ota_password: "ci-ota-password"
```

These values are public. They are convenient on a trusted home network. If you want private API and OTA credentials, build from source instead.

### Advanced: Build From Source

Use this path if you are developing the firmware or want private credentials.

Install ESPHome:

```sh
python3 -m venv .venv-esphome
.venv-esphome/bin/pip install esphome
```

Create secrets:

```sh
cp secrets.example.yaml secrets.yaml
```

Edit `secrets.yaml`, then generate your own API encryption key:

```sh
openssl rand -base64 32
```

Validate, compile, and flash over USB:

```sh
ls -1 /dev/cu.usb* /dev/tty.usb*
.venv-esphome/bin/esphome config chang-hong-ice-maker-esphome.yaml
.venv-esphome/bin/esphome compile chang-hong-ice-maker-esphome.yaml
.venv-esphome/bin/esphome run chang-hong-ice-maker-esphome.yaml --device /dev/cu.usbmodemXXXX
```

Future OTA updates with the source build:

```sh
.venv-esphome/bin/esphome upload chang-hong-ice-maker-esphome.yaml --device chang-hong-ice-maker-esphome.local
```

## Wi-Fi Provisioning

Supported methods:

1. Preconfigured Wi-Fi: with a source build, Wi-Fi credentials can be written into `secrets.yaml` before flashing.
2. Fallback AP: after about 90 seconds without Wi-Fi, connect to `ChangHongIceMakerESPHome AP` and open `http://192.168.4.1/`.
3. BLE Improv: after about 90 seconds without Wi-Fi, open `https://www.improv-wifi.com/` in a Web Bluetooth capable Chrome/Edge browser and select `chang-hong-ice-maker-esphome`.

With release firmware, use Fallback AP or BLE Improv provisioning. The Fallback AP password is `ci-fallback-password`.

Bluetooth is used only for provisioning. Daily control uses ESPHome Native API over Wi-Fi. Successful Wi-Fi credentials are stored in a fixed location so OTA updates should not erase provisioning.

## Add To Home Assistant

1. Make sure the ESP32-C3 and Home Assistant are on the same LAN.
2. Open `Settings -> Devices & services`.
3. If `Chang Hong Ice Maker ESPHome` is discovered automatically, add it.
4. Otherwise, add the `ESPHome` integration manually.
5. Use `chang-hong-ice-maker-esphome.local` or the ESP32-C3 IP address as Host.
6. If using release firmware, enter the fixed public key: `AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=`.
7. If using a source build, enter your own `api_encryption_key` from `secrets.yaml`.

Main entities:

```text
switch.chang_hong_ice_maker_esphome_power
switch.chang_hong_ice_maker_esphome_large_ice
button.chang_hong_ice_maker_esphome_uv_toggle
sensor.chang_hong_ice_maker_esphome_state
```

If the same ESP32-C3 was previously added with older firmware, Home Assistant may keep old entity IDs. Rename them manually or remove and re-add the ESPHome device.

## Usage

### Start Ice Making

Turn on `Power`. The machine starts from standby into Large Ice, so `Large Ice` stays on.

### Switch Ice Size

Toggle `Large Ice` while running:

- On: Large Ice
- Off: Small Ice

Small Ice cannot be selected while standby. If `Large Ice` is turned off during standby, the firmware refuses the command and restores it to on.

### Stop / Standby

Turn off `Power`. The firmware simulates the original Power short press and returns the machine to standby.

### UV Sterilization

Click `UV Toggle`. The firmware simulates a long Select press for about 5 seconds.

The original panel has no reliable UV feedback, so this is a button, not a switch. Repeated clicks are queued as toggle actions. You can infer the final UV state from the click count only when you already know the initial UV state.

## Post-Install Check

After connecting the ESP32-C3 to the ice maker:

1. Unplug the ice maker and connect `P1-P5`.
2. Power the ESP32-C3 and wait for Wi-Fi.
3. Power the ice maker to standby.
4. Wait 5-20 seconds and confirm `State=standby`, `Power=off`, `Large Ice=on`.
5. Turn on `Power` and confirm the machine starts in Large Ice.
6. Toggle `Large Ice` while running and confirm panel LEDs match Home Assistant.
7. Turn off `Power` and confirm standby.
8. Click `UV Toggle` and confirm the UV function is triggered.

If the state stays `unknown`, check `P1-P5` order, whether P3/GPIO2 affects boot, ESP32-C3 power isolation, and whether the ice maker is in no-water, ice-full, or another abnormal state.

## Diagnostic Entities

Diagnostic entities are for wiring checks, state detection, and remote-button troubleshooting. Do not use them as the main state source for normal automations.
High-frequency diagnostic entities are disabled by default in the firmware to avoid filling the Home Assistant recorder database with ADC, ratio, score, and delta debug data. Enable only the entities you need from the HA device page while troubleshooting, then disable them again.

For complete capture procedures, see the [debugging and capture guide](docs/debugging-guide.en.md).

If these entities were already added and enabled by an older firmware, HA may keep the existing enabled state after OTA. Disable them manually in HA or exclude them in `recorder` if you need an immediate database reduction.

| Entity | Purpose |
| --- | --- |
| `Target Model` | Firmware-declared target model. It should be `CH-Z6Y3` |
| `Firmware Version` / `ESPHome Version` | Confirm firmware and ESPHome versions after OTA |
| `Action Busy` / `Action State` / `Action Result` | Inspect remote-button action, refusal, queueing, or timeout |
| `Classified State` | Internal classification, useful for comparing with public `State` |
| `Fast State Candidate` / `Feature State Candidate` | Short-window candidates. Brief fluctuation is normal |
| `ADC Signature` | Relative five-node ADC signature, such as `0HHHH` or `MHMHH` |
| `Fast Ratio 0HHHH` / `Fast Ratio MHMHH` | Main signature ratios in the recent window |
| `P2 StdDev` / `Delta P2 P4` / `Delta P5 P2` | Secondary features for separating standby from Small Ice |
| `Small Feature Score` / `Standby Feature Score` | Small/standby feature vote scores |
| `Standby Blink Score` / `Standby Window Valid` | Slow standby blink fallback detection |
| `P1 Raw` - `P5 Raw` | Raw ADC readings. They are relative only and must not be treated as real voltages |

Occasional candidate jumps do not matter if `State`, `Power`, and `Large Ice` stay stable. That means the public state machine is filtering short-window noise correctly.

## How It Works

The ESP32-C3 normally keeps `P1-P5` as high-impedance inputs and samples ADC at about `1 kHz`. Because this setup does not share ground with the ice maker, raw ADC values are relative features, not real voltages.

State detection:

- `0HHHH` is the strong Large Ice running signature.
- `MHMHH` appears in both standby and Small Ice, so the firmware also checks `P2 StdDev`, `P2-P4`, and `P5-P2`.
- Standby has a blinking power LED, so a 16-second slow standby window remains as fallback.
- Passive `standby -> running_small` transitions are blocked because `CH-Z6Y3` always starts into Large Ice.

Remote control briefly pulls one GPIO low in open-drain style to simulate the original buttons:

```text
Power short press:  GPIO1 low for about 100 ms
Select short press: GPIO2 low for about 80 ms
UV long press:      GPIO2 low for about 5000 ms
```

After the action, all GPIOs return to input mode.

## Logs

Serial settings:

```text
921600 baud
DEBUG level
```

Use USB logs only when `P1-P5` are disconnected or USB is isolated. With the panel connected, prefer Wi-Fi logs:

```sh
.venv-esphome/bin/esphome logs chang-hong-ice-maker-esphome.yaml --device chang-hong-ice-maker-esphome.local
```

To save standard-firmware logs or switch to the debug firmware for D1-D4 raw frames, follow the [debugging and capture guide](docs/debugging-guide.en.md).

Common fields:

```text
state                 public state
classifier            internal classification
fast                  fast candidate
feature               immediate feature candidate
sig                   ADC signature
fast_0HHHH            Large Ice signature ratio
fast_MHMHH            Small Ice / standby signature ratio
small_score           Small Ice feature score
standby_score         standby feature score
p2_stddev             P2 standard deviation
d_p2_p4               P2-P4 average delta
d_p5_p2               P5-P2 average delta
standby_valid         slow standby window validity
action_state          current action
action_result         latest action result
```

## Limitations

- Direct GPIO has electrical risk. Panel voltages may exceed ESP32-C3 GPIO limits.
- No reliable Home Assistant entities are provided for No Water or Ice Full.
- UV has no state feedback and is exposed only as a toggle button.
- If Home Assistant sends conflicting commands too quickly, the firmware refuses some power/ice-size requests to protect the original panel scan.
- Only `CH-Z6Y3` is declared as verified. Other models require validation.

## Reverse-Engineering Data

The panel netlist, capture tools, and reverse-engineering notes are in [ice_panel_sniffer](https://github.com/Ljzd-PRO/ice_panel_sniffer).

<details>
<summary>View equivalent schematics and PCB trace</summary>

![Ice-maker panel equivalent schematic: expanded netlist](docs/images/panel-schematic-expanded.png)

![Ice-maker five-wire panel interconnected equivalent schematic](docs/images/panel-schematic-interconnected.png)

![Ice-maker control panel PCB trace diagram](docs/images/panel-pcb-trace.png)

</details>

## Contributing

For code, classifier, or documentation contributions, see [CONTRIBUTING.md](CONTRIBUTING.md).

## License

BSD 3-Clause. See [LICENSE](LICENSE).
