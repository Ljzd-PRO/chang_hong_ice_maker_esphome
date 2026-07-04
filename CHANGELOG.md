# Changelog

## v0.2.3 - 2026-07-05

### Added

- Documented Chang Hong `CH-Z6Y3` as the confirmed supported ice-maker model.
- Added `Target Model` diagnostic text sensors to the main and debug firmware.

### Changed

- Updated firmware comments and README compatibility guidance to name the verified `CH-Z6Y3` target without changing node name, project id, component name, or existing Home Assistant entity IDs.

## v0.2.2 - 2026-07-04

### Changed

- Added state-machine transition guards so standby cannot passively become Small Ice without an explicit command context.
- Added running-state latches so transient `unknown` or stale slow standby evidence does not override confirmed Large Ice or Small Ice operation.
- Changed power-on confirmation to require Large Ice, matching the physical machine behavior that startup from standby always enters Large Ice first.

### Tests

- Added synthetic regression tests for standby latch and running latch edge cases.
- OTA-tested the firmware on the real machine through:
  - 130s standby
  - 130s Large Ice
  - 130s Small Ice
  - 130s Large Ice after switching back
  - 130s standby after power off
- All five real-machine stability windows completed with zero external `State`, `Power`, or `Large Ice` violations.

## v0.2.1 - 2026-07-04

### Changed

- Improved standby vs small-ice classification with a 1-second feature-vote window using:
  - `P2` standard deviation
  - `P2-P4` average delta
  - `P5-P2` average delta
- Kept the 16-second standby blink detector as a fallback instead of relying on it for every standby/small distinction.
- Changed `Fast State Candidate` to represent a short-window feature candidate after consecutive confirmation.

### Added

- Added diagnostic entities for `Feature State Candidate`, `Small Feature Score`, `Standby Feature Score`, `P2 StdDev`, `Delta P2 P4`, and `Delta P5 P2`.
- Added a compact DMA debug fixture to classifier regression tests so CI can validate standby/small feature separation without the local sniffer archive.

## v0.2.0 - 2026-07-04

### Changed

- Replaced the previous three-option Home Assistant `Mode` select with two template switches:
  - `Power`
  - `Large Ice`
- Changed standby startup behavior to match the physical machine: every startup from standby is treated as Large Ice first.
- `Large Ice` now defaults to `ON` while the machine is standby/off; Small Ice commands are refused while not running.
- Reworked panel state classification from one 32-second window to two windows:
  - 2-second fast window for large/small running candidates.
  - 16-second standby window for power-LED blink confirmation.
- Added diagnostics for fast state candidate, fast signature ratios, and standby window validity.

### Notes

- This release intentionally changes Home Assistant entity IDs. Remove or ignore the old `select.chang_hong_ice_maker_esphome_mode` entity after upgrading.
- Standby confirmation is still slower than running-state detection because it depends on the slow blinking power LED.

## v0.1.0 - 2026-07-01

Initial public firmware release for the ChangHongIceMakerESPHome project.

### Added

- Added ESPHome firmware for bridging a ChangHong ice maker panel to Home Assistant with an ESP32-C3.
- Added direct GPIO panel integration for `P1-P5 -> GPIO0-GPIO4`, using ADC signatures for state recognition.
- Added Home Assistant `select` entity for the main operating mode:
  - `Off`
  - `Small Ice`
  - `Large Ice`
- Added Home Assistant `button` entity for UV toggle by simulating a long press on the original panel select button.
- Added diagnostic entities for classified state, ADC signature, confidence, standby blink score, action state, action result, action busy, and raw `P1-P5` ADC values.
- Added firmware version and ESPHome build version diagnostic entities.
- Added fixed Wi-Fi credential persistence logic to avoid losing provisioned Wi-Fi credentials after OTA updates.
- Added support for Native API, OTA, fallback AP provisioning, captive portal, and BLE Improv provisioning.
- Added serial/debug logging for startup, ADC classification, GPIO pulse actions, and refused commands.
- Added local ESPHome external component implementation in C++.
- Added README installation guide with wiring, flashing, provisioning, Home Assistant setup, panel photos, logo, and device-page screenshot.
- Added capture-based classifier regression tests using archived sniffer data when available.
- Added GitHub Actions workflows for tests, firmware builds, and firmware releases.

### Hardware Notes

- The current firmware follows the tested direct-GPIO wiring used during development.
- The direct-GPIO approach is accepted-risk and specific to the tested machine; long-term installations should add series resistance, clamping, or isolation before production use.
- `GPIO2` is still used for `P3`; if a board cannot boot with that wiring, move `P3` to `GPIO5` and update the firmware mapping.

### Release Artifact Notes

- GitHub release firmware artifacts are built with CI placeholder Wi-Fi, OTA, and API encryption secrets.
- The CI `api_encryption_key` baked into GitHub release firmware artifacts is fixed and shared across those artifacts; it is not randomly generated per user, per device, or per flash.
- For real installations, generate a private `api_encryption_key` and `ota_password` in local `secrets.yaml`, then build and flash your own firmware.
- Do not publish screenshots or logs containing your real ESPHome API encryption key.
