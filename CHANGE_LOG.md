# Change Log

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
- For real installations, generate a private `api_encryption_key` and `ota_password` in local `secrets.yaml`, then build and flash your own firmware.
- Do not publish screenshots or logs containing your real ESPHome API encryption key.
