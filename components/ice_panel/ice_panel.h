#pragma once

#include "esphome/components/wifi/wifi_component.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"

#include <array>
#include <cstdint>
#include <string>

namespace esphome {
namespace ice_panel {

enum class PanelState : uint8_t {
  UNKNOWN = 0,
  STANDBY,
  RUNNING_LARGE,
  RUNNING_SMALL,
  STARTING,
  STOPPING,
};

class IcePanel : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }

  bool request_power(bool target_on);
  bool request_large_ice(bool target_large);
  bool request_mode(const std::string &target_mode);
  bool request_uv_toggle();

  bool power_known() const;
  bool power_on() const;
  bool size_known() const;
  bool large_ice() const;
  bool mode_known() const;

  std::string state_text() const;
  std::string classified_state_text() const;
  std::string mode_text() const;
  std::string signature_text() const;
  std::string action_state_text() const;
  std::string action_result_text() const;
  bool action_busy() const;

  float confidence() const { return this->confidence_; }
  float blink_score() const { return this->blink_score_; }
  float ratio_large_signature() const { return this->ratio_large_signature_; }
  float ratio_mhmhh_signature() const { return this->ratio_mhmhh_signature_; }
  float raw_p1() const { return this->last_raw_[0]; }
  float raw_p2() const { return this->last_raw_[1]; }
  float raw_p3() const { return this->last_raw_[2]; }
  float raw_p4() const { return this->last_raw_[3]; }
  float raw_p5() const { return this->last_raw_[4]; }

 protected:
  static constexpr uint8_t PIN_COUNT = 5;
  static constexpr uint8_t BIN_COUNT = 64;
  static constexpr uint32_t SAMPLE_INTERVAL_US = 1000;
  static constexpr uint32_t BIN_INTERVAL_MS = 500;
  static constexpr uint32_t EVALUATE_INTERVAL_MS = 1000;
  static constexpr uint32_t LOG_INTERVAL_MS = 5000;
  static constexpr uint32_t FIXED_WIFI_SYNC_INTERVAL_MS = 10000;
  static constexpr uint32_t FIXED_WIFI_PREF_KEY = 0x1CE51CE5UL;
  static constexpr uint32_t RECENT_EVENT_VISIBLE_MS = 5000;
  static constexpr uint32_t SHORT_PRESS_COOLDOWN_MS = 800;
  static constexpr uint32_t UV_COOLDOWN_MS = 1500;

  enum class PulseKind : uint8_t {
    NONE = 0,
    SW1_OD,
    SW2_OD,
    SW2_HOLD_OD,
  };

  enum class OptimisticKind : uint8_t {
    NONE = 0,
    POWER,
    SIZE,
    MODE,
  };

  enum class PendingMode : uint8_t {
    NONE = 0,
    SMALL,
    LARGE,
  };

  struct Bin {
    bool valid{false};
    uint32_t total{0};
    uint32_t sig_0hhhh{0};
    uint32_t sig_mhmhh{0};
    std::array<uint64_t, PIN_COUNT> raw_sum{};
  };

  void configure_inputs_only_();
  void setup_fixed_wifi_preferences_();
  void service_fixed_wifi_preferences_();
  bool load_fixed_wifi_settings_(wifi::SavedWifiSettings *settings);
  bool save_fixed_wifi_settings_(const wifi::SavedWifiSettings &settings);
  static bool fixed_wifi_settings_valid_(const wifi::SavedWifiSettings &settings);
  static bool wifi_value_equals_(const char *lhs, const char *rhs);
  void restore_panel_inputs_();
  bool start_pulse_(PulseKind kind, uint8_t pin_index, uint32_t duration_ms);
  void finish_pulse_();
  void service_pending_mode_();
  void sample_once_();
  void advance_bin_if_needed_();
  void reset_bin_(uint8_t index);
  void evaluate_();
  void update_exposed_state_(PanelState classified, uint32_t now_ms);
  float calculate_blink_score_() const;
  void log_summary_(bool force = false);
  char bucket_(uint16_t value) const;
  bool signature_is_0hhhh_(const char *signature) const;
  bool signature_is_mhmhh_(const char *signature) const;
  bool optimistic_active_() const;
  bool cooldown_active_() const;
  bool set_pending_mode_target_(PendingMode target);
  void record_event_(const std::string &event);
  static const char *state_to_cstr_(PanelState state);
  static const char *pulse_to_action_cstr_(PulseKind kind);
  static const char *pending_mode_to_cstr_(PendingMode mode);
  static bool is_running_(PanelState state);

  std::array<uint8_t, PIN_COUNT> pins_{{0, 1, 2, 3, 4}};
  std::array<uint16_t, PIN_COUNT> last_raw_{{0, 0, 0, 0, 0}};
  char last_signature_[6]{'x', 'x', 'x', 'x', 'x', '\0'};

  std::array<Bin, BIN_COUNT> bins_{};
  uint8_t current_bin_{0};
  uint8_t valid_bins_{0};
  uint32_t current_bin_started_ms_{0};

  uint32_t last_sample_us_{0};
  uint32_t last_evaluate_ms_{0};
  uint32_t last_log_ms_{0};
  uint32_t last_fixed_wifi_sync_ms_{0};

  PanelState classified_state_{PanelState::UNKNOWN};
  PanelState exposed_state_{PanelState::UNKNOWN};
  float confidence_{0.0f};
  float blink_score_{0.0f};
  float ratio_large_signature_{0.0f};
  float ratio_mhmhh_signature_{0.0f};

  bool pulse_active_{false};
  PulseKind pulse_kind_{PulseKind::NONE};
  uint8_t pulse_pin_index_{0};
  uint32_t pulse_duration_ms_{0};
  uint32_t pulse_ends_ms_{0};
  uint32_t cooldown_until_ms_{0};

  OptimisticKind optimistic_kind_{OptimisticKind::NONE};
  PanelState optimistic_state_{PanelState::UNKNOWN};
  bool optimistic_power_on_{false};
  bool optimistic_large_ice_{false};
  PendingMode optimistic_mode_{PendingMode::NONE};
  PendingMode pending_mode_after_power_on_{PendingMode::NONE};
  uint32_t optimistic_until_ms_{0};
  std::string last_event_{"boot"};
  uint32_t last_event_ms_{0};

  ESPPreferenceObject fixed_wifi_pref_{};
  bool fixed_wifi_pref_ready_{false};
  bool fixed_wifi_have_saved_{false};
  wifi::SavedWifiSettings last_fixed_wifi_saved_{};
};

}  // namespace ice_panel
}  // namespace esphome
