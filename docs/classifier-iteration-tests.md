# Classifier Iteration Test Results

## 2026-07-04 - v0.2.2 State-Machine Guard

Environment:

- Firmware: `0.2.2`
- Device: `chang-hong-ice-maker-esphome.local`
- Test interface: ESPHome Native API
- Wiring: direct `P1-P5 -> GPIO0-GPIO4`

Validation performed after OTA:

| Step | Expected State | Duration | Violations |
|---|---:|---:|---:|
| Standby before startup | `State=standby`, `Power=false`, `Large Ice=true` | 130s | 0 |
| Power on, Large Ice | `State=running_large`, `Power=true`, `Large Ice=true` | 130s | 0 |
| Switch to Small Ice | `State=running_small`, `Power=true`, `Large Ice=false` | 130s | 0 |
| Switch back to Large Ice | `State=running_large`, `Power=true`, `Large Ice=true` | 130s | 0 |
| Power off, Standby | `State=standby`, `Power=false`, `Large Ice=true` | 130s | 0 |

Command confirmation times:

| Command | Confirmed In |
|---|---:|
| `Power=true` from standby | 2.01s |
| `Large Ice=false` from Large Ice | within 8s target |
| `Large Ice=true` from Small Ice | 2.01s |
| `Power=false` from running Large Ice | 2.01s |

Observations:

- `Feature State Candidate` and `Fast State Candidate` still fluctuate during standby, including transient `running_small`.
- The externally exposed `State`, `Power`, and `Large Ice` entities remained stable across all 130-second windows.
- `Standby Window Valid` can remain true during Small Ice, so running-state evidence must override stale slow standby evidence.
