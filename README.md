# ESPHome Ice Maker Panel

This ESPHome project turns the reverse-engineered five-wire ice-maker panel
into a Home Assistant device.

It intentionally keeps the same accepted-risk direct GPIO mapping used during
the bench tests:

```text
P1 -> GPIO0 / ADC1_CH0
P2 -> GPIO1 / ADC1_CH1
P3 -> GPIO2 / ADC1_CH2
P4 -> GPIO3 / ADC1_CH3
P5 -> GPIO4 / ADC1_CH4
```

The firmware is passive by default. All panel pins are floating inputs except
while a Home Assistant action requests one of the verified direct open-drain
button simulations:

```text
Power short press:  P2/GPIO1 open-drain low for 100 ms
Select short press: P3/GPIO2 open-drain low for 80 ms
UV toggle:          P3/GPIO2 open-drain low for 5000 ms
```

## Home Assistant Entities

- `select.ice_maker_mode`: `Off`, `Small Ice`, or `Large Ice`.
- `button.ice_maker_uv_toggle`: long-presses Select for UV toggle. UV has no
  panel feedback, so it is intentionally not exposed as a stateful switch.
- `text_sensor.ice_maker_state`: `standby`, `running_large`, `running_small`,
  `starting`, `stopping`, or `unknown`.
- `binary_sensor.ice_maker_action_busy`: on while a GPIO pulse, startup follow-up,
  or optimistic confirmation window is active.
- `text_sensor.ice_maker_action_state`: `idle`, active pulse name, pending startup
  target, confirmation state, or recent refusal/timeout.
- `text_sensor.ice_maker_action_result`: last action, refusal, confirmation, or
  timeout event. Examples: `pulse_uv_begin`, `refused_unknown_state`,
  `confirmed_running_large`.
- Diagnostic entities expose the latest ADC signature, confidence, blink score,
  signature ratios, and raw P1-P5 ADC readings.

## State Recognition

The component samples P1-P5 at 1 kHz and keeps a 32-second rolling window of
0.5-second bins. It applies the direct-floating ADC signature model from the
bench captures:

```text
0: ADC < 100
H: ADC > 3995
M: ADC 1500..2500
x: other
```

Classifier:

```text
0HHHH ratio > 65% and no slow blink  -> running_large
MHMHH ratio > 55% with slow blink     -> standby
MHMHH ratio > 55% without slow blink  -> running_small
otherwise                            -> unknown
```

The standby/running-small distinction depends on the 4-second power LED blink,
so it needs several seconds of data and becomes most reliable after about
20-35 seconds.

## Setup

Install ESPHome into the dedicated venv:

```sh
python3 -m venv .venv-esphome
.venv-esphome/bin/pip install esphome
```

Edit `secrets.yaml` before adding the device to Home Assistant:

```yaml
wifi_ssid: "..."
wifi_password: "..."
api_encryption_key: "..."
ota_password: "..."
```

For serial-only smoke tests, the generated placeholder `secrets.yaml` is enough
to compile and flash, but Wi-Fi and Home Assistant discovery will not work until
real credentials are supplied.

During development, keep the real lab Wi-Fi in the local `secrets.yaml`. This
prevents OTA updates from booting back into placeholder credentials. Do not
commit the real `secrets.yaml`.

## Wi-Fi Provisioning

The firmware supports both fallback Wi-Fi provisioning and BLE Improv
provisioning. Daily control still uses ESPHome Native API over Wi-Fi; Bluetooth
is only for setting or replacing Wi-Fi credentials.

The `ice_panel` component also keeps an extra copy of the currently connected
Wi-Fi credentials in a fixed preference slot. On boot, that fixed slot is loaded
before ESPHome Wi-Fi starts, so credentials entered through fallback AP or BLE
Improv survive later firmware changes whose ESPHome config hash changes. When
the device successfully connects to Wi-Fi, the active STA credentials are copied
back to that fixed slot. Placeholder credentials are never saved.

Fallback AP path:

1. Power the ESP32-C3.
2. If it cannot join the saved Wi-Fi, wait about 90 seconds.
3. Connect a phone or computer to `Ice Maker Panel Fallback`.
4. Open `http://192.168.4.1/`.
5. Enter the home Wi-Fi SSID and password.

BLE Improv path:

1. Power the ESP32-C3.
2. If it cannot join the saved Wi-Fi, wait about 90 seconds.
3. On Android, macOS, or Windows, open Chrome or Edge.
4. Visit `https://www.improv-wifi.com/`.
5. Connect to the BLE device named like `ice-maker-panel`.
6. Enter the home Wi-Fi SSID and password.

The current configuration uses `esp32_improv.authorizer: none` because the ice
maker panel buttons are already reserved for machine control.

## Build And Flash

Flash while the ESP32-C3 is not connected to the ice maker:

```sh
.venv-esphome/bin/esphome config ice_panel_esphome/ice-maker.yaml
.venv-esphome/bin/esphome compile ice_panel_esphome/ice-maker.yaml
.venv-esphome/bin/esphome run ice_panel_esphome/ice-maker.yaml --device /dev/cu.usbmodem11301
```

If the serial port changes:

```sh
ls -1 /dev/cu.usb* /dev/tty.usb*
```

Serial logger settings:

```text
921600 baud
DEBUG level
```

## Hardware Test Flow

1. Flash and boot ESP32-C3 without the ice maker connected. Logs should show
   `Ice panel direct GPIO mode starting`; state should be `unknown`.
2. Turn off the ice maker AC power and wait 30 seconds.
3. Connect `P1-P5 -> GPIO0-GPIO4`; do not connect ice-maker GND.
4. Power the ice maker and wait in standby for at least 30 seconds. The state
   should settle to `standby` and blink score should rise.
5. Set `Mode` to `Small Ice` or `Large Ice` in Home Assistant. If the machine is
   off, the firmware sends a power short press first, then corrects the ice size
   after startup if needed.
6. Set `Mode` to `Off` to send the power short press from a running state.
7. Press `UV Toggle` twice if UV behavior needs to be checked. No real UV state
   is inferred from the panel.

Rapid or conflicting commands are intentionally guarded. While `Action Busy` is
on, duplicate requests for the same pending target are accepted as already
pending, but opposite power/size requests are refused until the previous pulse or
classifier confirmation finishes. This prevents rapid Home Assistant clicks from
creating overlapping or premature panel button pulses.

Action pulses also have a short cooldown after the pin returns to floating
input: 800 ms after short presses and 1500 ms after the UV long press. This
protects the original panel scanner from back-to-back Home Assistant calls.

## HA Stress Test

The local Docker Home Assistant test instance can be exercised with:

```sh
.venv-esphome/bin/python ice_panel_esphome/tools/ha_stress_test.py --scenario no-load
```

No-load expected behavior, with the ESP32-C3 not connected to the ice maker:

```text
select.ice_maker_panel_ice_maker_mode -> unknown
sensor.ice_maker_panel_state          -> unknown
mode changes                          -> refused_unknown_state
UV spam                               -> one pulse_uv, later calls refused_pulse_uv
UV during mode changes                -> mode calls refused_pulse_uv
```

Latest no-load validation:

```text
2026-06-30 11:19 Asia/Shanghai
ESP32-C3 IP: 192.168.100.24
Result: passed
Rapid mode while unknown: refused_unknown_state, no pulse
UV spam: one pulse_uv, later calls refused_pulse_uv
Second UV after cooldown: pulse_uv_begin -> pulse_uv_end
Final state: unknown, action_busy off
```

For connected hardware testing:

1. Turn off the ice maker AC power and wait 30 seconds.
2. Connect `P1-P5 -> GPIO0-GPIO4`; do not connect ice-maker GND.
3. Power the ice maker and wait 35 seconds in standby.
4. Record `State`, `Classified State`, `ADC Signature`, `Confidence`,
   `Standby Blink Score`, `Ratio 0HHHH`, and `Ratio MHMHH`.
5. Test `Off -> Large Ice -> Small Ice -> Large Ice -> Off`.
6. Test `UV Toggle` twice with at least 8 seconds between presses.
7. If recognition fails, keep the logs and tune the signature/blink thresholds
   before changing the Home Assistant entity model.

## Limitations

- This direct GPIO method can expose ESP32-C3 pins to panel voltages outside
  normal 3.3 V input limits. It is carried forward here only because the bench
  test accepted that risk.
- No-water and ice-full LEDs are not reliable state entities in the direct
  floating ADC method.
- If GPIO2 causes boot trouble, move P3 to GPIO5 and adjust the component pin
  mapping in code.
