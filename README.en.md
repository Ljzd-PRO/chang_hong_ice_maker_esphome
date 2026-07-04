# ChangHongIceMakerESPHome

<p align="center">
  <img src="docs/images/logo.png" alt="ChangHongIceMakerESPHome logo" width="220">
</p>

Language: [中文](README.md) | English

This is an ESPHome firmware project for integrating a Chang Hong `CH-Z6Y3` ice maker into Home Assistant. It uses an ESP32-C3 connected to the original five-wire control panel, enabling remote status detection, power control, small/large ice mode switching, and UV button triggering.

This project uses a direct-GPIO wiring method that has been tested on `CH-Z6Y3`. It is useful for the confirmed model retrofit and debugging setup, but it is not a universal safe electrical interface. For long-term production-style use, add current limiting, voltage clamps, or an isolated front end for every signal line.

## Related Repository

This repository focuses on the ESPHome / Home Assistant firmware implementation. For electrical details of the five-wire panel, the reverse-engineering capture process, confirmed netlist, schematics, and PCB trace diagrams, see [ice_panel_sniffer](https://github.com/Ljzd-PRO/ice_panel_sniffer).

## Supported Models

| Item | Details |
| --- | --- |
| Confirmed model | Chang Hong `CH-Z6Y3` |
| Panel type | Original five-wire `P1-P5` control panel |
| Panel features | No Water, Ice Full, Power, Small Ice, and Large Ice LEDs, plus Power and Select buttons |
| Panel netlist | Matches the five-wire netlist documented in [ice_panel_sniffer](https://github.com/Ljzd-PRO/ice_panel_sniffer) |

Other Chang Hong models should be treated as unverified even if the external panel looks similar. Before reusing this firmware, verify the `P1-P5` netlist, LED/button branches, default Large Ice startup behavior, and standby/small/large ADC signatures.

## Author And Project

- Author/Maintainer: [Ljzd-PRO](https://github.com/Ljzd-PRO) `<me@ljzd.link>`
- ESPHome project id: `ljzd-pro.chang_hong_ice_maker_esphome`

ESPHome's `project.name` field follows the `author_name.project_name` convention and is exposed through logger output, mDNS, and Native API device info. Full author contact information is kept in this README, ESPHome YAML comments, and the local external component source comments.

## Panel And PCB Photos

![Ice-maker external control panel](docs/images/ice-maker-panel.jpeg)

![Panel PCB front side with LEDs, power/select buttons, and the 5-wire connector](docs/images/panel-pcb-front.jpeg)

![Panel PCB back side showing LED, button, and resistor branch traces](docs/images/panel-pcb-back.jpeg)

## Features

- Provides two main Home Assistant control switches:
  - `Power`: running / standby
  - `Large Ice`: large / small ice; defaults to large while standby
- Provides a `UV Toggle` button that simulates a long press of the original Select button.
- Automatically classifies panel state:
  - `standby`
  - `running_small`
  - `running_large`
  - `starting`
  - `stopping`
  - `unknown`
- Exposes Home Assistant diagnostic entities such as target model, action state, fast state candidate, feature scores, ADC signature, confidence, and raw P1-P5 values.
- Exposes target model, firmware, and ESPHome build version diagnostic entities for OTA verification.
- Supports ESPHome Native API, OTA, serial logs, Fallback AP provisioning, and BLE Improv provisioning.

## How It Works

The original ice-maker panel is a five-wire scanning circuit with LEDs and buttons. The ESP32-C3 normally keeps P1-P5 as high-impedance inputs and reads ADC signatures to infer whether the machine is in standby, small-ice mode, or large-ice mode.

State detection uses a 1-second feature window plus a 16-second standby fallback window. Large ice is mainly detected by the `0HHHH` signature. Standby and small ice both produce `MHMHH`, so the firmware also compares P2 variation, `P2-P4`, and `P5-P2` deltas, then requires two consecutive matching candidates before confirming the state. The 16-second slow window remains as a fallback for confirming standby from the blinking power LED when short-window features are ambiguous.

For remote control, the firmware briefly switches one GPIO to open-drain low, simulating the original panel button:

```text
Power short press:  GPIO1 low for about 100 ms
Select short press: GPIO2 low for about 80 ms
UV long press:      GPIO2 low for about 5000 ms
```

After each action, all GPIOs are restored to input mode. The firmware also has a busy state and cooldown handling to avoid repeated Home Assistant clicks interfering with the original panel scan logic.

Power and ice-size commands are mutually exclusive controls: if an action is already running, waiting for confirmation, or in cooldown, new power/ice-size requests are usually refused. `UV Toggle` is the exception. Because it has no state feedback, repeated UV clicks are placed into a short queue and executed as sequential long presses; new UV requests are refused only when the queue is full.

## Hardware

Recommended hardware:

- ESP32-C3 Super Mini or a compatible ESP32-C3 development board
- Non-earthed two-prong USB-C power adapter or power bank
- Thin wires
- Multimeter
- Insulation tape, heat-shrink tubing, or other insulation/fixation material

Before wiring, make sure the ice maker is unplugged and wait at least 30 seconds.

## Wiring

Connect the panel signal lines as follows:

```text
P1 -> GPIO0
P2 -> GPIO1
P3 -> GPIO2
P4 -> GPIO3
P5 -> GPIO4
```

Notes:

- Do not connect the ice-maker GND to ESP32-C3 GND.
- Power the ESP32-C3 from its own USB port, not from the panel.
- Do not plug or unplug P1-P5 while the ice maker is powered.
- Keep bare wires, solder joints, and the ESP32-C3 back side away from metal parts.
- If P3/GPIO2 prevents the ESP32-C3 from booting, move P3 to GPIO5 and update the firmware pin mapping accordingly.

Powering matters. This direct-GPIO setup relies on the ESP32-C3 being powered from a floating supply. In testing, a power bank and a normal two-prong USB-C adapter both worked: state detection was stable and the original panel buttons still worked. A Windows PC USB connection did not work; the PC ground reference coupled through USB/GPIO/ADC paths and disturbed the panel scan, causing abnormal state detection and making the original panel buttons unresponsive.

For normal installation:

- Power the ESP32-C3 from a two-prong USB-C adapter or a power bank.
- Do not connect the ESP32-C3 to a computer USB port while P1-P5 are connected to the ice maker.
- Do not connect ESP32-C3 GND to the chassis, protective earth, or metal base of the ice maker.
- Do not power the ESP32-C3 from P1-P5; they are multiplexed panel scan lines, not stable supply rails.
- If USB serial debugging is required while P1-P5 are connected, use a USB isolator. Prefer Wi-Fi logs and OTA for normal debugging.

The current firmware uses direct GPIO wiring. The panel lines may exceed 3.3V, so the ESP32-C3 can be damaged. A safer long-term design should add series resistors and clamp protection on every P line, or use isolation/analog-switch circuitry.

## Flashing

Flash the ESP32-C3 before connecting it to the ice maker.

Install ESPHome:

```sh
python3 -m venv .venv-esphome
.venv-esphome/bin/pip install esphome
```

Copy and edit the secrets file:

```sh
cp chang_hong_ice_maker_esphome/secrets.example.yaml chang_hong_ice_maker_esphome/secrets.yaml
```

Example `secrets.yaml`:

```yaml
wifi_ssid: "YOUR_WIFI_SSID"
wifi_password: "YOUR_WIFI_PASSWORD"
fallback_ap_password: "CHANGE_ME_1234"
api_encryption_key: "REPLACE_WITH_BASE64_32_BYTE_KEY"
ota_password: "REPLACE_WITH_RANDOM_OTA_PASSWORD"
```

Generate an API encryption key:

```sh
openssl rand -base64 32
```

Validate and compile:

```sh
.venv-esphome/bin/esphome config chang_hong_ice_maker_esphome/chang-hong-ice-maker-esphome.yaml
.venv-esphome/bin/esphome compile chang_hong_ice_maker_esphome/chang-hong-ice-maker-esphome.yaml
```

Find the serial port:

```sh
ls -1 /dev/cu.usb* /dev/tty.usb*
```

Initial flashing:

```sh
.venv-esphome/bin/esphome run chang_hong_ice_maker_esphome/chang-hong-ice-maker-esphome.yaml --device /dev/cu.usbmodemXXXX
```

Future OTA updates:

```sh
.venv-esphome/bin/esphome upload chang_hong_ice_maker_esphome/chang-hong-ice-maker-esphome.yaml --device chang-hong-ice-maker-esphome.local
```

If mDNS is unstable, replace `--device` with the ESP32-C3 IP address.

## Wi-Fi Provisioning

The firmware supports three provisioning methods.

First: enter Wi-Fi SSID and password in `secrets.yaml` before flashing. The device will connect directly after boot.

Second: Fallback AP provisioning.

1. Power on the ESP32-C3.
2. If it cannot connect to an existing Wi-Fi network, wait about 90 seconds.
3. Connect your phone or computer to `ChangHongIceMakerESPHome AP`.
4. Open `http://192.168.4.1/`.
5. Enter the home Wi-Fi SSID and password.

Third: BLE Improv provisioning.

1. Power on the ESP32-C3.
2. If it cannot connect to an existing Wi-Fi network, wait about 90 seconds.
3. Open `https://www.improv-wifi.com/` in Chrome or Edge with Web Bluetooth support.
4. Select the Bluetooth device named `chang-hong-ice-maker-esphome`.
5. Enter the home Wi-Fi SSID and password.

Bluetooth is used only for provisioning. Daily control uses ESPHome Native API over Wi-Fi.

The firmware stores successful Wi-Fi credentials in a fixed preference location to avoid losing provisioning data after OTA updates. `secrets.yaml` is local and ignored by Git; do not commit real passwords.

## Add To Home Assistant

1. Make sure the ESP32-C3 and Home Assistant are on the same LAN.
2. In Home Assistant, open `Settings -> Devices & services`.
3. If `Chang Hong Ice Maker ESPHome` is discovered automatically, add it.
4. Otherwise, click `Add Integration` and choose `ESPHome`.
5. Enter Host as:
   - `chang-hong-ice-maker-esphome.local`, or
   - the ESP32-C3 IP address.
6. Enter the `api_encryption_key` from `secrets.yaml` when prompted.

After successful setup, the Home Assistant device name is:

```text
Chang Hong Ice Maker ESPHome
```

Main control and state entities:

```text
switch.chang_hong_ice_maker_esphome_power
switch.chang_hong_ice_maker_esphome_large_ice
button.chang_hong_ice_maker_esphome_uv_toggle
sensor.chang_hong_ice_maker_esphome_state
```

### Diagnostic Entities

These entities are marked with `entity_category: diagnostic` in YAML and appear in the `Diagnostics` section of the Home Assistant device page. They are not normal control surfaces. They exist for wiring checks, OTA version verification, state-classification debugging, and remote-button troubleshooting.

Normal automations should use only the main entities: `Power`, `Large Ice`, `UV Toggle`, and `State`. Diagnostic entities are useful for temporary debugging or advanced alerts, but short-window candidates, raw ADC values, and feature scores should not be treated as the real ice-maker state.

Full diagnostic entity IDs use the `chang_hong_ice_maker_esphome_` prefix. For example, `Action Busy` maps to `binary_sensor.chang_hong_ice_maker_esphome_action_busy`.

| Entity | Group | Meaning |
| --- | --- | --- |
| `Action Busy` | Action execution | Whether the firmware is currently simulating a button press, waiting for action confirmation, or holding pending UV toggles. When it is `on`, power/ice-size requests are usually refused; UV requests may be queued or refused when the queue is full. |
| `Action State` | Action execution | Current action state. Common values include `idle`, `pulse_sw1`, `pulse_sw2`, `pulse_uv`, `queued_uv_*`, `confirming_*`, `refused_*`, and `timeout_*`; `queued_uv_*` only refers to the UV long-press queue. |
| `Action Result` | Action execution | Latest action result or event. It is usually `boot` after startup; successful confirmation appears as `confirmed_*`; refused or timed-out actions appear as `refused_*` or `timeout_*`. |
| `Classified State` | State detection | Internal classification after the basic state-machine guardrails, but without Home Assistant optimistic display during command execution. Compare it with the public `State` when debugging. |
| `Fast State Candidate` | State detection | Fast state candidate from the 1-second feature window after consecutive confirmation. It may briefly fluctuate and is not the final public `State`. |
| `Feature State Candidate` | State detection | Immediate candidate from the recent 1-second feature window before consecutive confirmation. It is expected to fluctuate more than `Fast State Candidate`. |
| `Confidence` | State detection | Confidence of the current internal classification, in percent. Higher values mean recent samples better match a known state. |
| `ADC Signature` | Sampling feature | Relative ADC signature of the five panel nodes, such as `0HHHH` or `MHMHH`. The symbols `0/H/M/x` mean low/high/mid/other buckets, not real voltages. |
| `Fast Ratio 0HHHH` | Sampling feature | Percentage of the recent 1-second window matching `0HHHH`, mainly associated with large-ice running. |
| `Fast Ratio MHMHH` | Sampling feature | Percentage of the recent 1-second window matching `MHMHH`. This signature is shared by small ice and standby, so secondary features are required. |
| `Delta P2 P4` | Sampling feature | Average `P2-P4` delta over the recent 1-second window, used to separate small ice from standby. |
| `Delta P5 P2` | Sampling feature | Average `P5-P2` delta over the recent 1-second window, used to separate small ice from standby. |
| `P2 StdDev` | Sampling feature | Standard deviation of P2 raw readings over the recent 1-second window; one of the main small/standby features. |
| `Small Feature Score` | Sampling feature | Small-ice feature vote score from `0-3`; `2` or more is normally treated as a small-ice candidate. |
| `Standby Feature Score` | Sampling feature | Standby feature vote score from `0-3`; `2` or more is normally treated as a standby candidate. |
| `Standby Blink Score` | Slow-window fallback | Score for the slow power-LED blink pattern. Higher values indicate a stronger standby signature. |
| `Standby Window Valid` | Slow-window fallback | Whether the 16-second slow window currently confirms standby blink behavior. When it is on, standby classification is usually more reliable. |
| `P1 Raw` - `P5 Raw` | Raw sampling | Raw ADC readings for P1-P5, roughly `0-4095`. With direct floating GPIO wiring, these are only relative readings and must not be converted to real voltages. |
| `Target Model` | Model and version | Confirmed supported model declared by this firmware. The main firmware should report `CH-Z6Y3`. |
| `Firmware Version` | Model and version | Project firmware version from `project_version` in YAML. |
| `ESPHome Version` | Model and version | ESPHome build version running on the device, useful after OTA updates. |

If `Classified State` stays `unknown` and `Confidence`, `Fast Ratio 0HHHH`, `Fast Ratio MHMHH`, and `Standby Blink Score` all remain low, check P1-P5 wiring, GPIO mapping, and the actual ice-maker state.

If the same ESP32-C3 was added with older firmware, Home Assistant may keep old `entity_id` values. You can rename the entities manually or remove and re-add the ESPHome device.

<details>
<summary>View Home Assistant controls and sensors screenshot</summary>

![Home Assistant controls and sensor entities](docs/images/home-assistant-control-sensors.png)

</details>

<details>
<summary>View Home Assistant diagnostics screenshot</summary>

![Home Assistant diagnostic entities](docs/images/home-assistant-diagnostics.png)

</details>

## Usage

### Start Ice Making

In Home Assistant, turn on:

```text
Power
```

The ice maker always starts from standby into large-ice mode, so `Large Ice` stays on by default.

### Switch Ice Size

When the ice maker is running, toggle:

```text
Large Ice
```

On means large ice; off means small ice. The machine cannot be set to small ice while standby. If `Large Ice` is turned off during standby, the firmware refuses the command and restores it to on.

### Stop / Standby

Turn off:

```text
Power
```

The firmware simulates a Power short press and returns the ice maker to standby.

### UV Sterilization

Click:

```text
UV Toggle
```

The firmware simulates a long Select press for about 5 seconds. The original panel does not provide reliable UV state feedback, so this is a button, not a switch. Confirm the real UV state from the ice maker itself.

If `UV Toggle` is clicked repeatedly, the firmware queues the UV long-press requests and executes them in order. Since UV is a toggle action with no feedback, you can only infer the final UV state from the number of clicks when you already know the initial UV state.

## Post-Install Check

After connecting the device to the ice maker for the first time:

1. Unplug the ice maker and connect P1-P5.
2. Power the ESP32-C3 and wait for Wi-Fi connection.
3. Power the ice maker to standby.
4. Wait about 5-20 seconds and confirm `State` becomes `standby`, `Power` is off, and `Large Ice` is on.
5. Turn on `Power` in Home Assistant and confirm the ice maker starts in large-ice mode.
6. Toggle `Large Ice` while running and confirm the panel LEDs and Home Assistant state agree.
7. Turn off `Power` and confirm the ice maker returns to standby.
8. Click `UV Toggle` and confirm the long press triggers the UV function.

If the state remains `unknown`, first check whether P1-P5 are swapped, whether P3/GPIO2 affects boot, whether the ice maker is in an abnormal state, and whether the ESP32-C3 is still connected to Wi-Fi.

## Serial Debugging

Serial log settings:

```text
921600 baud
DEBUG level
```

Use USB logs only when P1-P5 are disconnected from the ice maker, or when the
USB connection is isolated. When P1-P5 are connected, prefer Wi-Fi logs to avoid
the computer USB ground reference disturbing the panel scan.

View logs through USB:

```sh
.venv-esphome/bin/esphome logs chang_hong_ice_maker_esphome/chang-hong-ice-maker-esphome.yaml --device /dev/cu.usbmodemXXXX
```

Or over the network:

```sh
.venv-esphome/bin/esphome logs chang_hong_ice_maker_esphome/chang-hong-ice-maker-esphome.yaml --device chang-hong-ice-maker-esphome.local
```

Common debug fields:

```text
state                 exposed state
classifier            internal classified state
fast                  fast state candidate after 1-second feature-window confirmation
feature               immediate 1-second feature-window candidate
sig                   current ADC signature
fast_0HHHH            1-second large-ice signature ratio
fast_MHMHH            1-second small/standby signature ratio
small_score           small-ice feature vote score
standby_score         standby feature vote score
p2_stddev             P2 standard deviation
d_p2_p4               P2-P4 average delta
d_p5_p2               P5-P2 average delta
standby_valid         whether the 16-second standby window is valid
blink                 standby blink score
action_state          current action state
action_result         latest action result
```

## Limitations

- Wiring, thresholds, and state-machine rules are based on Chang Hong `CH-Z6Y3` testing; other models require panel-netlist and state-signature validation.
- The current direct-GPIO design has electrical risk; ESP32-C3 GPIOs may see panel voltages above 3.3V.
- This project does not provide reliable Home Assistant entities for no-water or ice-full status.
- UV has no panel feedback, so it is exposed as a button rather than a real state switch. Repeated clicks are queued as toggle actions, but the firmware cannot know the final real UV state.
- Standby and small-ice running can both produce the `MHMHH` signature. The firmware first separates them with 1-second feature voting and keeps the 16-second power-LED blink window as a fallback; diagnostic candidates may fluctuate, but the public state should remain stable.
- If Home Assistant sends conflicting commands too quickly, the firmware refuses some power/ice-size commands to protect the original panel scanning logic.

## Appendix: Panel Schematics And PCB Trace

<details>
<summary>View reverse-engineering diagrams</summary>

These diagrams are mainly for maintenance, secondary development, and wiring verification. Most Home Assistant users only need the wiring and usage sections above.

The first diagram expands the confirmed netlist branch by branch:

![Ice-maker panel equivalent schematic: expanded netlist](docs/images/panel-schematic-expanded.png)

The second diagram keeps a single shared P1-P5 node set and shows how the five wires are reused for LED driving and button scanning:

![Ice-maker five-wire panel interconnected equivalent schematic](docs/images/panel-schematic-interconnected.png)

The third diagram is an approximate back-side copper trace reconstruction with front-side components mirrored onto the back-side view. It is not a production-ready Gerber file:

![Ice-maker control panel PCB trace diagram](docs/images/panel-pcb-trace.png)

</details>
