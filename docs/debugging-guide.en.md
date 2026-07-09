# Debugging And Capture Guide

Language: [中文](debugging-guide.md) | English

This guide is for users who need to collect state-detection data from `ChangHongIceMakerESPHome`. The data is useful for wiring checks, misclassification reports, classifier tuning, and model-compatibility validation.

## Choose A Capture Method

| Method | Use When | Data | Impact |
| --- | --- | --- | --- |
| Standard firmware | Normal operation has `unknown`, state jumps, or refused controls | HA diagnostic entities, ESPHome log summaries, 1 kHz classifier features | No firmware switch; high-frequency diagnostics are disabled by default |
| Debug firmware | You need deeper analysis for a new machine, classifier work, or standby/small confusion | D1-D4 structured frames with means, variance, edges, branch deltas, ADC backend data | Requires OTA to debug firmware; not intended for long-term use |

Use the standard firmware first. Switch to the debug firmware only when the standard diagnostic data is not enough.

## Safety

- After `P1-P5` are connected, prefer Wi-Fi logs instead of computer USB.
- If USB serial logs are required, use a USB isolator or disconnect the panel wires first.
- Do not connect or disconnect `P1-P5` while the ice maker is powered.
- Keep debug `Bias Mode` at `Float` by default. Use `Internal Pullup` / `Internal Pulldown` only for short tests, then click `Restore Floating Inputs`.
- ADC values are relative floating direct-GPIO features, not real panel voltages.

## Prepare Local Tools

Skip this section if you only inspect entities in Home Assistant.

To save logs or run the analysis scripts:

```sh
python3 -m venv .venv-esphome
.venv-esphome/bin/pip install esphome pyserial
```

ESPHome CLI reads `!secret` values from the YAML, so a local `secrets.yaml` is still required:

```sh
cp secrets.example.yaml secrets.yaml
```

If you use the release firmware only to view logs, not to recompile, set `api_encryption_key`, `ota_password`, and `fallback_ap_password` to the fixed public values from the release page. Set `wifi_ssid` and `wifi_password` to the current network values.

Check that the device is reachable:

```sh
.venv-esphome/bin/esphome logs chang-hong-ice-maker-esphome.yaml \
  --device chang-hong-ice-maker-esphome.local
```

## Capture With Standard Firmware

The standard firmware is best for capturing normal runtime behavior. It does not export every ADC sample; it exports the summary features used by the classifier.

### Enable Diagnostic Entities

High-frequency diagnostic entities are disabled by default. Enable only what you need in Home Assistant:

1. Open `Settings -> Devices & services`.
2. Open the `Chang Hong Ice Maker ESPHome` device.
3. Show disabled entities.
4. Enable the required diagnostic entities.

Recommended entities:

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

For basic stability checks, `State`, `Power`, `Large Ice`, `Classified State`, `ADC Signature`, `P2 StdDev`, `Delta P2 P4`, and `Delta P5 P2` are usually enough.

### Save ESPHome Logs

```sh
mkdir -p standard_logs
.venv-esphome/bin/esphome logs chang-hong-ice-maker-esphome.yaml \
  --device chang-hong-ice-maker-esphome.local \
  | tee standard_logs/standard-$(date +%Y%m%d-%H%M%S).log
```

Useful captures:

- Standby for 2 minutes
- Large Ice for 2 minutes
- Small Ice for 2 minutes
- A transition sequence: standby -> large -> small -> large -> standby

Important log fields:

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

### Reduce HA Recorder Data

Disable high-frequency diagnostic entities again after troubleshooting. Otherwise the Home Assistant recorder may store unnecessary ADC, ratio, score, and delta data.

You can also exclude them in HA `configuration.yaml`:

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

Restart Home Assistant after changing recorder configuration.

## Capture With Debug Firmware

The debug firmware is for temporary high-detail probing. It emits D1-D4 structured debug frames and exposes many debug entities in HA. Debug entities are enabled by default for easier short-term capture; do not leave the debug firmware in a production HA setup.

Current GitHub Releases primarily publish the standard firmware. The debug firmware usually needs a source checkout, a local `secrets.yaml`, and local compile/OTA.

### Flash Debug Firmware

OTA from the standard firmware:

```sh
.venv-esphome/bin/esphome upload chang-hong-ice-maker-debug.yaml \
  --device chang-hong-ice-maker-esphome.local
```

After flashing, the hostname becomes:

```text
chang-hong-ice-maker-debug.local
```

If mDNS is unreliable, use the device IP address.

### Capture Network Logs

Wi-Fi log capture is recommended:

```sh
.venv-esphome/bin/python tools/capture_debug_panel.py \
  --source esphome \
  --device chang-hong-ice-maker-debug.local \
  --duration-s 90 \
  --label debug_standby
```

You can type markers while capturing:

```text
mark standby_visible
mark small_visible
mark large_visible
quit
```

Output directory example:

```text
debug_captures/20260708-153000-debug_standby/
```

Files:

| File | Contents |
| --- | --- |
| `raw.log` | Raw ESPHome logs |
| `debug_frames.csv` | Parsed D1-D4 structured frames |
| `markers.csv` | User timestamp markers |

### Capture Matrix

Capture each state for at least 60-90 seconds:

```sh
.venv-esphome/bin/python tools/capture_debug_panel.py --source esphome \
  --device chang-hong-ice-maker-debug.local --duration-s 90 --label debug_standby

.venv-esphome/bin/python tools/capture_debug_panel.py --source esphome \
  --device chang-hong-ice-maker-debug.local --duration-s 90 --label debug_large

.venv-esphome/bin/python tools/capture_debug_panel.py --source esphome \
  --device chang-hong-ice-maker-debug.local --duration-s 90 --label debug_small
```

For transition bugs:

```sh
.venv-esphome/bin/python tools/capture_debug_panel.py --source esphome \
  --device chang-hong-ice-maker-debug.local --duration-s 180 --label debug_transitions
```

### D1-D4 Frame Meaning

| Frame | Main Data |
| --- | --- |
| `D1` | Sample count, signature, `0HHHH`/`MHMHH`/active ratios, P1-P5 means |
| `D2` | P1-P5 min, max, and standard deviation |
| `D3` | P1-P5 digital duty, edge counts, seven panel branch deltas |
| `D4` | ADC backend, calibration status, error rate, estimated mV means |

Fields that usually matter for standby vs Small Ice:

```text
signature
ratio_mhmhh
p2_stddev
delta_p2_p4
delta_p5_p2
p1_edges / p1_duty
```

## Analyze Debug Captures

```sh
.venv-esphome/bin/python tools/analyze_debug_captures.py \
  --debug-capture debug_captures/20260708-153000-debug_standby \
  --debug-capture debug_captures/20260708-153300-debug_large \
  --debug-capture debug_captures/20260708-153600-debug_small \
  --output debug_captures/analysis_report.md
```

If the older `ice_panel_sniffer` capture archive exists locally, the script also compares against the old 1 kHz data. If it does not exist, the report still analyzes the new debug captures.

## Restore Standard Firmware

After debugging:

```sh
.venv-esphome/bin/esphome upload chang-hong-ice-maker-esphome.yaml \
  --device chang-hong-ice-maker-debug.local
```

The hostname returns to:

```text
chang-hong-ice-maker-esphome.local
```

Home Assistant may keep old debug entities. Once the standard firmware works, remove the debug device or disable its entities.

## What To Include In A Bug Report

- Ice maker model. Currently only `CH-Z6Y3` is verified.
- Firmware version and ESPHome version.
- Wiring, especially `P1-P5 -> GPIO0-GPIO4`.
- ESP32-C3 power method and whether computer USB or a USB isolator is used.
- Visible panel state: standby, large, small, no water, ice full.
- HA `State`, `Power`, and `Large Ice` values.
- Standard firmware log, or the full `debug_captures/<timestamp-label>/` directory.
- HA screenshots if useful.

Do not upload `secrets.yaml` or anything containing your Wi-Fi password, private API key, or OTA password.
